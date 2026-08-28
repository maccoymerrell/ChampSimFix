/*
 * ADAPTIVE_ROUTER — placement decided at runtime, from where the work actually
 * went.
 *
 * Routing is physical, so the OS may put a grain anywhere and move it later.
 * This is the policy that spends that freedom.
 *
 * The evidence is migration. When a context on tile `from` has to move to `to`
 * in order to reach an address, that is a statement about placement: the grain
 * it wanted was on the wrong tile *for that consumer*. Counting those per grain
 * gives, over an epoch, the tile that most wanted each grain -- and moving the
 * grain there removes every future migration by that consumer at a stroke.
 *
 * The obvious failure of that rule on its own is that it has one fixed point:
 * everything on one tile, zero migrations, three quarters of the machine idle.
 * A balanced minimum cut computed offline lands exactly there -- measured, on
 * kron-24: 16% fewer migrations and 2.3x slower, with two tiles at an occupancy
 * of one context. So the pull term is bounded by a balance term, and a grain is
 * only moved when its destination is not already carrying more than its share.
 *
 * Related work worth reading before trusting any of this: R-NUCA (ISCA'09) for
 * OS page-granular classification and placement; Carrefour (ASPLOS'13), which
 * is the closest statement of the same tension -- it chooses among co-location,
 * interleaving and replication by weighing locality against bandwidth
 * imbalance rather than minimising remote accesses; Linux AutoNUMA, which
 * migrates pages toward the thread touching them *and* the thread toward its
 * pages; and Mizan (EuroSys'13) for runtime vertex migration in a partitioned
 * graph system.
 *
 * Parameters:
 *   epoch_migrations   migrations observed between placement passes
 *   pull_threshold     migrations a tile must contribute before a grain moves
 *   imbalance_slack    how far above an even share a tile may be pushed
 *   placement          initial placement before any evidence exists
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>
#include <fmt/core.h>

#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_vmem.h"
#include "nmfc/tile_map.h"
#include "nmfc/tile_router.h"

namespace
{

class adaptive_router : public nmfc::tile_router_module
{
public:
  explicit adaptive_router(champsim::modules::ModuleBuilder builder)
      : nmfc::tile_router_module(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")), map_(nmfc::tile_map_from(builder)), epoch_(builder.get_parameter<std::uint64_t>("epoch_migrations", true, std::uint64_t{100000})),
        pull_threshold_(builder.get_parameter<std::uint64_t>("pull_threshold", true, std::uint64_t{16})),
        slack_(builder.get_parameter<double>("imbalance_slack", true, 1.25)),
        cooldown_cap_(builder.get_parameter<std::uint32_t>("cooldown_cap", true, std::uint32_t{64})),
        initial_(builder.get_parameter<std::string>("placement", true, std::string{"round_robin"})), placed_(map_.num_tiles(), 0), load_(map_.num_tiles(), 0)
  {
  }

  [[nodiscard]] nmfc::routing_order order() const override { return nmfc::routing_order::TRANSLATE_FIRST; }

  /** Only a hint here; anything that routes must translate. */
  [[nodiscard]] std::size_t owner_of(champsim::origin /*origin*/, champsim::address vaddr) const override { return map_.tile_of_virtual(vaddr); }

  [[nodiscard]] std::size_t page_table_roots() const override { return 1; }

  void attach_placement(nmfc::page_placement_sink* placement) override { placement_ = placement; }

  [[nodiscard]] std::size_t placement_for(champsim::origin /*origin*/, champsim::address vaddr) override
  {
    const auto tile = initial_ == "first_touch" ? map_.tile_of_virtual(vaddr) : next_tile_;
    if (initial_ != "first_touch") {
      next_tile_ = (next_tile_ + 1) % map_.num_tiles();
    }
    ++placed_[tile];
    return tile;
  }

  void note_migration(champsim::origin origin, champsim::address vaddr, std::size_t from, std::size_t to, std::uint64_t /*token*/) override
  {
    // Credit the tile that *wanted* the grain, not the one that has it.
    const auto key = key_of(origin, vaddr);
    auto& pull = pulls_[key];
    if (pull.empty()) {
      pull.assign(map_.num_tiles(), 0);
    }
    ++pull[from];

    // Load, as opposed to grain count. Balancing grains does not balance work:
    // measured here, an even 112/135/93/104 grains produced context occupancies
    // of 179/340/106/158, because on a power-law graph one hot grain carries
    // more traffic than twenty cold ones. So the balance term weighs the
    // traffic a tile is actually serving.
    ++load_[to];
    ++volume_[key];

    if (++observed_ % epoch_ == 0) {
      rebalance();
    }
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (observed_ == 0) {
      return;
    }
    out.line(fmt::format("{} MIGRATIONS OBSERVED: {} REMAPS: {} REFUSED FOR BALANCE: {}", NAME, observed_, remapped_, refused_for_balance_));
    out.line(fmt::format("{} GRAINS PER TILE: {}", NAME, fmt::join(placed_, " ")));
    out.line(fmt::format("{} MIGRATION LOAD PER TILE: {}", NAME, fmt::join(load_, " ")));
    const auto dominance = dominance_count_ == 0 ? 0.0 : dominance_sum_ / static_cast<double>(dominance_count_);
    out.line(fmt::format("{} PULL DOMINANCE: {:.3f} (uniform would be {:.3f}) REMAPPED AGAIN: {} of {}", NAME, dominance, 1.0 / double(map_.num_tiles()),
                         rethrashed_, remapped_));
    out.line(fmt::format("{} HELD FOR CONFIRMATION: {}", NAME, unconfirmed_));
    auto json = out.json();
    json.add("migrations_observed", observed_);
    json.add("remaps", remapped_);
    json.add("refused_for_balance", refused_for_balance_);
    json.add("grains_per_tile", placed_);
    json.add("migration_load_per_tile", load_);
    json.add("pull_dominance", dominance);
    json.add("remapped_again", rethrashed_);
  }

private:
  [[nodiscard]] std::uint64_t key_of(champsim::origin origin, champsim::address vaddr) const
  {
    return (static_cast<std::uint64_t>(origin.asid()) << 48) | (vaddr.to<std::uint64_t>() >> map_.grain_bits());
  }

  /**
   * Move grains toward whoever kept coming for them, so long as the
   * destination can take them without becoming the machine.
   */
  void rebalance()
  {
    if (placement_ == nullptr) {
      return;
    }
    const auto tiles = map_.num_tiles();
    const auto total = std::max<std::uint64_t>(std::accumulate(std::begin(load_), std::end(load_), std::uint64_t{0}), 1);
    const auto ceiling = static_cast<std::uint64_t>(slack_ * static_cast<double>(total) / static_cast<double>(tiles));

    for (auto& [key, pull] : pulls_) {
      if (pull.empty()) {
        continue;
      }
      const auto best = static_cast<std::size_t>(std::distance(std::begin(pull), std::max_element(std::begin(pull), std::end(pull))));
      if (pull[best] < pull_threshold_) {
        continue; // not enough evidence to be worth a shootdown
      }

      // How lopsided the evidence actually is. With N tiles, a grain that every
      // tile wants equally scores 1/N and carries no information at all -- and
      // a 2 MiB grain holds half a million vertices, so whether the pull is
      // signal or noise is a question about granularity, not about the policy.
      // Recorded rather than assumed, because a policy that acts on noise looks
      // exactly like a policy that is working until you measure this.
      const auto total_pull = std::accumulate(std::begin(pull), std::end(pull), std::uint64_t{0});
      dominance_sum_ += static_cast<double>(pull[best]) / static_cast<double>(std::max<std::uint64_t>(total_pull, 1));
      ++dominance_count_;

      auto& st = state_[key];
      if (st.cooldown > 0) {
        // Recently moved. Let the consequences of that move settle before
        // believing what the next epoch says about it.
        --st.cooldown;
        std::fill(std::begin(pull), std::end(pull), 0);
        continue;
      }
      if (st.last_best != best) {
        // First epoch to name this tile. Remember it and require a second one
        // to agree: a single epoch's winner is the thing that oscillates.
        st.last_best = best;
        ++unconfirmed_;
        std::fill(std::begin(pull), std::end(pull), 0);
        continue;
      }
      // The balance term. Without it this rule has one fixed point -- every
      // grain on whichever tile pulled hardest first -- and that is the
      // configuration a minimum cut already showed to be 2.3x slower.
      // Would this move push the destination past its share of the *traffic*?
      if (load_[best] + volume_[key] >= ceiling) {
        ++refused_for_balance_;
        std::fill(std::begin(pull), std::end(pull), 0);
        continue;
      }
      const auto asid = static_cast<std::uint32_t>(key >> 48);
      const auto vgrain = key & ((std::uint64_t{1} << 48) - 1);
      if (placement_->remap_grain(asid, vgrain, best)) {
        ++remapped_;
        ++placed_[best];
        // Each move buys a longer wait before the next, so a grain that cannot
        // settle stops paying for shootdowns it will only undo.
        st.cooldown = std::min<std::uint32_t>(1U << st.moves, cooldown_cap_);
        if (++st.moves > 1) {
          ++rethrashed_;
        }
      }
      std::fill(std::begin(pull), std::end(pull), 0);
    }
  }

  nmfc::tile_map map_;
  std::uint64_t epoch_;
  std::uint64_t pull_threshold_;
  double slack_;
  std::uint32_t cooldown_cap_;
  std::string initial_;
  std::vector<std::uint64_t> placed_;
  /** Migrations served per tile, and per grain: the balance term's real units. */
  std::vector<std::uint64_t> load_;
  std::unordered_map<std::uint64_t, std::uint64_t> volume_;
  nmfc::page_placement_sink* placement_ = nullptr;

  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> pulls_;
  std::size_t next_tile_ = 0;
  std::uint64_t observed_ = 0;
  std::uint64_t remapped_ = 0;
  std::uint64_t refused_for_balance_ = 0;
  /**
   * What a grain's evidence has said before, and whether it is allowed to move.
   *
   * The measurement that forced this: pull dominance is 0.772 against a uniform
   * 0.250, so the evidence is emphatically not noise -- and yet 86% of remaps
   * were moving a grain that had already been moved. Both are true because
   * acting on the evidence changes it. Move a grain to the tile that kept
   * coming for it and its consumers now run there; their other accesses pull
   * them away, so next epoch the pull comes from somewhere else and the grain
   * chases it. The policy was oscillating, not learning.
   *
   * Two standard remedies, and the reason NUMA page migration has both:
   * confirm the evidence across epochs before acting, and make a grain that has
   * already moved wait longer each time before it may move again.
   */
  struct grain_state {
    std::size_t last_best = std::numeric_limits<std::size_t>::max();
    std::uint32_t moves = 0;
    std::uint32_t cooldown = 0;
  };
  std::unordered_map<std::uint64_t, grain_state> state_;
  double dominance_sum_ = 0.0;
  std::uint64_t dominance_count_ = 0;
  std::uint64_t rethrashed_ = 0;
  std::uint64_t unconfirmed_ = 0;
};

static nmfc::tile_router_module::register_module<adaptive_router> adaptive_router_reg("ADAPTIVE_ROUTER");

} // anonymous namespace
