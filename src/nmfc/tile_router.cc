/*
 * CONGRUENT_ROUTER — the routing rule the design was built on.
 *
 * The tile is a field of the virtual address, so answering costs a shift and a
 * mask and needs no translation, no lookup and no state. That is the property
 * everything else leans on: a context decides local-vs-migrate before it has
 * translated anything, page tables split N ways with every walk local, and the
 * allocator is left with no placement freedom whatsoever -- a frame must sit on
 * the tile its own virtual address already named.
 *
 * That last clause is the whole of the trade. It is why placement here is a
 * compile-time decision, and why nothing can be re-placed at runtime without
 * changing an address the program can see.
 *
 * Parameters: the standard nmfc geometry (nmfc_num_tiles, nmfc_grain_bits,
 * nmfc_mode_bit, log2_block_size).
 */

#include <cstddef>

#include "nmfc/nmfc_config.h"
#include "nmfc/tile_map.h"
#include "nmfc/tile_router.h"

namespace
{

class congruent_router : public nmfc::tile_router_module
{
public:
  explicit congruent_router(champsim::modules::ModuleBuilder builder)
      : nmfc::tile_router_module(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")), map_(nmfc::tile_map_from(builder))
  {
  }

  [[nodiscard]] nmfc::routing_order order() const override { return nmfc::routing_order::VIRTUAL_FIRST; }

  [[nodiscard]] std::size_t owner_of(champsim::origin /*origin*/, champsim::address vaddr) const override { return map_.tile_of_virtual(vaddr); }

  /** Forced, not chosen: congruence is the invariant, so this must agree with owner_of. */
  [[nodiscard]] std::size_t placement_for(champsim::origin origin, champsim::address vaddr) override { return owner_of(origin, vaddr); }

  /** One root per channel: the tile is known before the walk begins. */
  [[nodiscard]] std::size_t page_table_roots() const override { return map_.num_tiles(); }

private:
  nmfc::tile_map map_;
};

static nmfc::tile_router_module::register_interface tile_router_iface_reg("tile_router", "tile routers");
static nmfc::tile_router_module::register_module<congruent_router> congruent_router_reg("CONGRUENT_ROUTER");

} // anonymous namespace
