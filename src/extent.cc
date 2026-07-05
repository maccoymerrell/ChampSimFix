#include "extent.h"

#include "address.h"
#include "champsim.h"
#include "modules.h"

// The cached extents in extent.h hard-code the 64-bit address width so the
// header need not depend on address.h (which includes extent.h).
static_assert(champsim::address::bits == champsim::data::bits{64});

void champsim::refresh_address_extents()
{
  auto& g = champsim::modules::ModuleBuilder::globals();
  auto log2_page = g.get_parameter<unsigned>("log2_page_size", true, 12u);
  auto log2_block = g.get_parameter<unsigned>("log2_block_size", true, 6u);
  detail::cached_page_number_extent = dynamic_extent{address::bits, data::bits{log2_page}};
  detail::cached_page_offset_extent = dynamic_extent{data::bits{log2_page}, data::bits{}};
  detail::cached_block_number_extent = dynamic_extent{address::bits, data::bits{log2_block}};
  detail::cached_block_offset_extent = dynamic_extent{data::bits{log2_block}, data::bits{}};
}
