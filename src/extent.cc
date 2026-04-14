#include "extent.h"

#include "address.h"
#include "champsim.h"

// Extent constructors use the deprecated globals because extents are part of the
// address infrastructure and are constructed globally before any environment exists.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

champsim::page_number_extent::page_number_extent() : dynamic_extent(champsim::address::bits, champsim::data::bits{LOG2_PAGE_SIZE}) {}

champsim::page_offset_extent::page_offset_extent() : dynamic_extent(champsim::data::bits{LOG2_PAGE_SIZE}, champsim::data::bits{}) {}

champsim::block_number_extent::block_number_extent() : dynamic_extent(champsim::address::bits, champsim::data::bits{LOG2_BLOCK_SIZE}) {}

champsim::block_offset_extent::block_offset_extent() : dynamic_extent(champsim::data::bits{LOG2_BLOCK_SIZE}, champsim::data::bits{}) {}

#pragma GCC diagnostic pop

namespace
{
template <typename T>
auto size(const T& ext)
{
  return champsim::to_underlying(ext.upper) - champsim::to_underlying(ext.lower);
}
} // namespace

std::size_t champsim::size(dynamic_extent ext) { return ::size(ext); }

std::size_t champsim::size(page_offset_extent ext) { return ::size(ext); }

std::size_t champsim::size(page_number_extent ext) { return ::size(ext); }

std::size_t champsim::size(block_offset_extent ext) { return ::size(ext); }

std::size_t champsim::size(block_number_extent ext) { return ::size(ext); }
