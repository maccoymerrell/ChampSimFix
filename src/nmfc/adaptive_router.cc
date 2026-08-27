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
        initial_(builder.get_parameter<std::string>("placement", true, std::string{"round_robin"})), placed_(map_.num_tiles(), 0)
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

  void note_migration(champsim::origin origin, champsim::address vaddr, std::size_t from, std::size_t /*to*/) override
  {
    // Credit the tile that *wanted* the grain, not the one that has it.
    auto& pull = pulls_[key_of(origin, vaddr)];
    if (pull.empty()) {
      pull.assign(map_.num_tiles(), 0);
    }
    ++pull[from];

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
    auto json = out.json();
    json.add("migrations_observed", observed_);
    json.add("remaps", remapped_);
    json.add("refused_for_balance", refused_for_balance_);
    json.add("grains_per_tile", placed_);
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
    const auto total = std::max<std::uint64_t>(std::accumulate(std::begin(placed_), std::end(placed_), std::uint64_t{0}), 1);
    const auto ceiling = static_cast<std::uint64_t>(slack_ * static_cast<double>(total) / static_cast<double>(tiles));

    for (auto& [key, pull] : pulls_) {
      if (pull.empty()) {
        continue;
      }
      const auto best = static_cast<std::size_t>(std::distance(std::begin(pull), std::max_element(std::begin(pull), std::end(pull))));
      if (pull[best] < pull_threshold_) {
        continue; // not enough evidence to be worth a shootdown
      }
      // The balance term. Without it this rule has one fixed point -- every
      // grain on whichever tile pulled hardest first -- and that is the
      // configuration a minimum cut already showed to be 2.3x slower.
      if (placed_[best] >= ceiling) {
        ++refused_for_balance_;
        std::fill(std::begin(pull), std::end(pull), 0);
        continue;
      }
      const auto asid = static_cast<std::uint32_t>(key >> 48);
      const auto vgrain = key & ((std::uint64_t{1} << 48) - 1);
      if (placement_->remap_grain(asid, vgrain, best)) {
        ++remapped_;
        ++placed_[best];
      }
      std::fill(std::begin(pull), std::end(pull), 0);
    }
  }

  nmfc::tile_map map_;
  std::uint64_t epoch_;
  std::uint64_t pull_threshold_;
  double slack_;
  std::string initial_;
  std::vector<std::uint64_t> placed_;
  nmfc::page_placement_sink* placement_ = nullptr;

  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> pulls_;
  std::size_t next_tile_ = 0;
  std::uint64_t observed_ = 0;
  std::uint64_t remapped_ = 0;
  std::uint64_t refused_for_balance_ = 0;
};

static nmfc::tile_router_module::register_module<adaptive_router> adaptive_router_reg("ADAPTIVE_ROUTER");

} // anonymous namespace
