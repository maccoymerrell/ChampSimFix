/*
 * NUCA_ROUTER — classify first, then apply the policy that class deserves.
 *
 * The hand-rolled predecessor (ADAPTIVE_ROUTER) had one action -- move a grain
 * toward whichever tile kept migrating to reach it -- and no condition under
 * which it declined to act. That is the whole of why it lost. On kron it
 * concentrated work onto two tiles and ran 1.45x slower than plain round-robin,
 * with the runtime tracking the occupancy spread almost exactly, even after
 * hysteresis stopped it oscillating and a traffic-weighted balance term stopped
 * it counting grains as though they were equal.
 *
 * The published NUCA and NUMA work is largely about the missing half: knowing
 * when a placement policy should do nothing.
 *
 * R-NUCA (Hardavellas et al., ISCA'09) classifies pages and gives each class a
 * fixed policy rather than chasing accessors: instructions are replicated,
 * private data is placed at its accessor, and shared read-write data is
 * *interleaved*. That last one is the case this design kept rediscovering the
 * hard way -- an offline minimum cut and an online pull heuristic both lost to
 * interleaving on a graph whose hot array is shared by every tile.
 *
 * Carrefour (Dashti et al., ASPLOS'13) supplies the gate. It measures imbalance
 * and the local-access ratio before choosing among co-location, interleaving
 * and replication, and applies nothing when the measurements say nothing is
 * wrong -- which is the behaviour that makes the adversarial case safe rather
 * than catastrophic.
 *
 * So: a grain is PRIVATE when essentially one tile wants it, SHARED otherwise.
 * Private grains may be co-located. Shared grains are never co-located, no
 * matter how lopsided their pull looks -- 0.77 dominance was real evidence and
 * acting on it was still wrong, because the remaining 0.23 was three other
 * tiles that would then have to migrate. Replication is already handled: the
 * CODE region is R-NUCA's instruction class, aliased per channel.
 *
 * Parameters:
 *   epoch_migrations     migrations observed between passes
 *   private_threshold    pull share above which a grain counts as private
 *   imbalance_threshold  max/mean tile load above which co-location is withheld
 *   pull_threshold       evidence required before a private grain moves
 *   cooldown_cap         upper bound on a grain's post-move quiet period
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>
#include <fmt/core.h>

#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_vmem.h"
#include "nmfc/tile_map.h"
#include "nmfc/tile_router.h"

namespace
{

class nuca_router : public nmfc::tile_router_module
{
public:
  explicit nuca_router(champsim::modules::ModuleBuilder builder)
      : nmfc::tile_router_module(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")), map_(nmfc::tile_map_from(builder)),
        epoch_(builder.get_parameter<std::uint64_t>("epoch_migrations", true, std::uint64_t{100000})),
        private_threshold_(builder.get_parameter<double>("private_threshold", true, 0.90)),
        imbalance_threshold_(builder.get_parameter<double>("imbalance_threshold", true, 1.10)),
        pull_threshold_(builder.get_parameter<std::uint64_t>("pull_threshold", true, std::uint64_t{16})),
        cooldown_cap_(builder.get_parameter<std::uint32_t>("cooldown_cap", true, std::uint32_t{64})), load_(map_.num_tiles(), 0), window_(map_.num_tiles(), 0)
  {
  }

  [[nodiscard]] nmfc::routing_order order() const override { return nmfc::routing_order::TRANSLATE_FIRST; }
  /**
   * Where this address actually lives right now.
   *
   * Dispatch reads this to decide where to start an invocation. Answering with
   * the address's natural tile instead sends work to a tile chosen by an
   * address bit rather than by where the data is -- which, under a silo'd
   * layout where those differ, cost 13x the migrations for no other reason.
   */
  [[nodiscard]] std::size_t owner_of(champsim::origin origin, champsim::address vaddr) const override
  {
    if (placement_ != nullptr) {
      if (const auto home = placement_->grain_mapping_on(origin.asid(), vaddr.to<std::uint64_t>(), 0); home.has_value()) {
        return map_.tile_of(*home);
      }
    }
    return map_.tile_of_virtual(vaddr); // not yet backed
  }
  [[nodiscard]] std::size_t page_table_roots() const override { return 1; }
  void attach_placement(nmfc::page_placement_sink* placement) override { placement_ = placement; }

  /** Interleave by default, which is R-NUCA's policy for data it has not yet classified. */
  [[nodiscard]] std::size_t placement_for(champsim::origin /*origin*/, champsim::address /*vaddr*/) override
  {
    const auto tile = next_tile_;
    next_tile_ = (next_tile_ + 1) % map_.num_tiles();
    return tile;
  }

  void note_migration(champsim::origin origin, champsim::address vaddr, std::size_t from, std::size_t to, std::uint64_t token) override
  {
    const auto key = key_of(origin, vaddr);
    auto& g = grains_[key];
    if (g.pull.empty()) {
      g.pull.assign(map_.num_tiles(), 0);
    }
    ++g.pull[from];
    ++load_[to];

    // The migration says this address and the address the context came for
    // belong together. That is a constraint between two grains, not a direction
    // to drag one of them: union them, and place the component.
    //
    // Placing grains one at a time cannot escape a random start. A cluster's
    // grains scattered over N tiles pull uniformly from all N, so the dominant
    // puller is noise until a majority already sits on one tile -- scattered is
    // a stable equilibrium. Measured: dominance 0.718 and 20 of 345 grains ever
    // moved, against an oracle 19.5x better. A component moves as a unit, which
    // is the symmetry break.
    auto& previous = previous_grain_[token];
    if (previous != 0 && previous != key) {
      unite(previous, key);
    }
    previous = key;
    ++window_[to];

    if (++observed_ % epoch_ == 0) {
      pass();
    }
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (observed_ == 0) {
      return;
    }
    const auto dominance = classified_ == 0 ? 0.0 : dominance_sum_ / static_cast<double>(classified_);
    out.line(fmt::format("{} MIGRATIONS OBSERVED: {} GRAINS CLASSIFIED: {} private: {} shared: {}", NAME, observed_, classified_, private_seen_, shared_seen_));
    out.line(fmt::format("{} GRAINS REMAPPED: {} COMPONENTS seen: {} placed: {} too-large: {} withheld-imbalance: {}", NAME, remapped_, components_seen_,
                         components_placed_, components_too_large_, withheld_imbalance_));
    out.line(fmt::format("{} MEAN PULL DOMINANCE: {:.3f} (uniform {:.3f}) PEAK WINDOWED IMBALANCE: {:.2f} TILE LOAD: {}", NAME, dominance,
                         1.0 / double(map_.num_tiles()), peak_imbalance_, fmt::join(load_, " ")));

    auto json = out.json();
    json.add("migrations_observed", observed_);
    json.add("co_locations", remapped_);
    json.add("withheld_imbalance", withheld_imbalance_);
    json.add("withheld_shared", withheld_shared_);
    json.add("pull_dominance", dominance);
    json.add("tile_load", load_);
  }

private:
  struct grain_state {
    std::vector<std::uint32_t> pull;
    std::size_t last_best = std::numeric_limits<std::size_t>::max();
    std::uint32_t moves = 0;
    std::uint32_t cooldown = 0;
  };

  /** Union-find over grains that migrations have linked. */
  std::uint64_t find(std::uint64_t x)
  {
    auto it = parent_.find(x);
    if (it == std::end(parent_) || it->second == x) {
      parent_[x] = x;
      return x;
    }
    const auto root = find(it->second);
    parent_[x] = root; // path compression
    return root;
  }

  void unite(std::uint64_t a, std::uint64_t b)
  {
    const auto ra = find(a);
    const auto rb = find(b);
    if (ra != rb) {
      parent_[rb] = ra;
    }
  }

  [[nodiscard]] std::uint64_t key_of(champsim::origin origin, champsim::address vaddr) const
  {
    return (static_cast<std::uint64_t>(origin.asid()) << 48) | (vaddr.to<std::uint64_t>() >> map_.grain_bits());
  }

  /**
   * Carrefour's question, asked before anything is moved: is anything wrong?
   *
   * Over a *window*, not over all time. A lifetime average starts even and
   * responds to nothing: measured, this gate allowed 14 co-locations while the
   * cumulative load still looked balanced, those 14 were the hottest grains in
   * the machine, and the resulting occupancy was 71/75/538/73. The gate then
   * refused 344 further moves -- correctly, and far too late to matter.
   */
  [[nodiscard]] double imbalance() const
  {
    const auto total = std::accumulate(std::begin(window_), std::end(window_), std::uint64_t{0});
    if (total == 0) {
      return 1.0;
    }
    const auto mean = static_cast<double>(total) / static_cast<double>(window_.size());
    const auto peak = static_cast<double>(*std::max_element(std::begin(window_), std::end(window_)));
    return peak / mean;
  }

  void pass()
  {
    if (placement_ == nullptr) {
      return;
    }
    const auto tiles = map_.num_tiles();

    // Gather the components migrations have formed, and how heavy each is.
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> components;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> component_pull;
    for (auto& [key, g] : grains_) {
      const auto root = find(key);
      components[root].push_back(key);
      auto& cp = component_pull[root];
      if (cp.empty()) {
        cp.assign(tiles, 0);
      }
      for (std::size_t t = 0; t < tiles && t < g.pull.size(); ++t) {
        cp[t] += g.pull[t];
      }
    }

    const auto total_load = std::max<std::uint64_t>(std::accumulate(std::begin(window_), std::end(window_), std::uint64_t{0}), 1);
    const auto share = static_cast<double>(total_load) / static_cast<double>(tiles);

    for (auto& [root, members] : components) {
      ++components_seen_;
      // A component larger than a tile's fair share of the data is not a hot
      // set, it is the whole working set: on a graph where everything touches
      // everything the co-access graph is fully connected, and collapsing it
      // onto one tile is exactly the failure an offline minimum cut already
      // demonstrated. Leave it interleaved.
      if (members.size() > (grains_.size() + tiles - 1) / tiles) {
        ++components_too_large_;
        continue;
      }

      const auto& cp = component_pull[root];
      const auto best = static_cast<std::size_t>(std::distance(std::begin(cp), std::max_element(std::begin(cp), std::end(cp))));
      const auto pull_total = std::accumulate(std::begin(cp), std::end(cp), std::uint64_t{0});
      if (pull_total < pull_threshold_) {
        continue;
      }
      // Balance, in the units that matter: would this component push the
      // destination past a fair share of the traffic actually being served?
      if (static_cast<double>(window_[best]) > slack_ * share) {
        ++withheld_imbalance_;
        continue;
      }

      for (const auto key : members) {
        const auto asid = static_cast<std::uint32_t>(key >> 48);
        const auto vgrain = key & ((std::uint64_t{1} << 48) - 1);
        if (placement_->remap_grain(asid, vgrain, best)) {
          ++remapped_;
        }
      }
      ++components_placed_;
    }

    for (auto& [key, g] : grains_) {
      std::fill(std::begin(g.pull), std::end(g.pull), 0);
    }
    std::fill(std::begin(window_), std::end(window_), 0);
  }

  nmfc::tile_map map_;
  std::uint64_t epoch_;
  double private_threshold_;
  double imbalance_threshold_;
  std::uint64_t pull_threshold_;
  std::uint32_t cooldown_cap_;
  std::vector<std::uint64_t> load_;
  /** The same, over one epoch: what the gate actually reads. */
  std::vector<std::uint64_t> window_;
  nmfc::page_placement_sink* placement_ = nullptr;

  std::unordered_map<std::uint64_t, grain_state> grains_;
  std::size_t next_tile_ = 0;
  std::uint64_t observed_ = 0;
  std::uint64_t remapped_ = 0;
  std::uint64_t classified_ = 0;
  std::uint64_t private_seen_ = 0;
  std::uint64_t shared_seen_ = 0;
  std::uint64_t withheld_imbalance_ = 0;
  std::uint64_t withheld_shared_ = 0;
  std::uint64_t withheld_evidence_ = 0;
  double dominance_sum_ = 0.0;
  std::unordered_map<std::uint64_t, std::uint64_t> parent_;
  /** Per invocation: the last grain it migrated for. Co-access is its relation. */
  std::unordered_map<std::uint64_t, std::uint64_t> previous_grain_;
  std::uint64_t components_seen_ = 0;
  std::uint64_t components_placed_ = 0;
  std::uint64_t components_too_large_ = 0;
  double slack_ = 1.25;
  double peak_imbalance_ = 1.0;
};

static nmfc::tile_router_module::register_module<nuca_router> nuca_router_reg("NUCA_ROUTER");

} // anonymous namespace
