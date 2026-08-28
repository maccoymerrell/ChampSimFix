/*
 * PHYSICAL_ROUTER — translate first, then read the tile out of the physical
 * address.
 *
 * The other branch of the design. Under congruent routing the tile is a field
 * of the *virtual* address, which costs nothing to read but leaves the OS no
 * say: a grain must be backed on the tile its own address already named, and it
 * can never be moved without changing an address the program can see. Here the
 * tile is a field of the *physical* address, so the OS may put any grain on any
 * tile and move it later -- which is what makes adaptive repartitioning
 * possible at all.
 *
 * What that costs, stated plainly rather than discovered later:
 *
 *   Routing waits on translation. A context cannot know whether its next
 *   address is local until it has translated it. In practice that is the same
 *   translation it was going to do anyway, one step earlier -- and it is why
 *   the per-context translation cache matters more here than there.
 *
 *   The page table collapses to one root. Choosing a per-channel root *is* the
 *   routing decision, so it cannot be made before the walk. Walk references
 *   therefore go wherever the PTE lives, over the memory network the tiles
 *   already sit on (--walk-routing fabric).
 *
 * Parameters:
 *   placement    "round_robin" | "first_touch"  where a new grain is backed
 *   nmfc geometry (nmfc_num_tiles, nmfc_grain_bits, nmfc_mode_bit, log2_block_size)
 */

#include <cstddef>
#include <string>
#include <fmt/core.h>

#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_vmem.h"
#include "nmfc/tile_map.h"
#include "nmfc/tile_router.h"

namespace
{

class physical_router : public nmfc::tile_router_module
{
public:
  explicit physical_router(champsim::modules::ModuleBuilder builder)
      : nmfc::tile_router_module(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")), map_(nmfc::tile_map_from(builder)), policy_(builder.get_parameter<std::string>("placement", true, std::string{"round_robin"}))
  {
  }

  [[nodiscard]] nmfc::routing_order order() const override { return nmfc::routing_order::TRANSLATE_FIRST; }

  /**
   * Where this address actually lives right now.
   *
   * A context that routes must still translate -- this cannot substitute for
   * that. What it is for is *dispatch*: a migration says the function and the
   * address it wanted belong on the same tile, and that can be satisfied by
   * moving the page or by starting the function somewhere else. Answering with
   * the address's natural tile, as this used to, sends an invocation to a tile
   * chosen by an address bit rather than by where its data is, and the pull
   * evidence the other half of the policy collects is then just a record of
   * where dispatch happened to guess.
   */
  [[nodiscard]] std::size_t owner_of(champsim::origin origin, champsim::address vaddr) const override
  {
    if (placement_ != nullptr) {
      if (const auto home = placement_->grain_mapping_on(origin.asid(), vaddr.to<std::uint64_t>(), 0); home.has_value()) {
        return map_.tile_of(*home);
      }
    }
    return map_.tile_of_virtual(vaddr); // not yet backed; the natural tile is as good a guess as any
  }

  /**
   * Where a newly faulted grain is backed. Free, unlike under congruence, and
   * therefore an actual policy.
   *
   * Round-robin is the neutral control: it spreads grains evenly with no regard
   * for who touches them, which is what an adaptive policy has to beat to have
   * earned anything.
   */
  [[nodiscard]] std::size_t placement_for(champsim::origin /*origin*/, champsim::address vaddr) override
  {
    if (policy_ == "first_touch") {
      // Without a requester on this interface, first touch degenerates to the
      // address's own tile -- which is congruence, and is offered only so the
      // two can be compared under the same routing rule.
      return map_.tile_of_virtual(vaddr);
    }
    const auto tile = next_tile_;
    next_tile_ = (next_tile_ + 1) % map_.num_tiles();
    return tile;
  }

  /** One. Choosing a per-channel root would require the answer the walk produces. */
  [[nodiscard]] std::size_t page_table_roots() const override { return 1; }

  void attach_placement(nmfc::page_placement_sink* placement) override { placement_ = placement; }

private:
  nmfc::tile_map map_;
  nmfc::page_placement_sink* placement_ = nullptr;
  std::string policy_;
  std::size_t next_tile_ = 0;
};

static nmfc::tile_router_module::register_module<physical_router> physical_router_reg("PHYSICAL_ROUTER");

} // anonymous namespace
