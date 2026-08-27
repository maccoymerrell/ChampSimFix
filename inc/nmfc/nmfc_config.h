/*
 * Shared configuration reading for NMFC modules.
 *
 * Every module that touches the address layout — the interleave fabric, the
 * memory controllers, the page allocator, the function cores — has to agree on
 * exactly the same tile map. Deriving it here, from root-level globals, means
 * there is one place a disagreement could come from instead of six.
 *
 * The globals live at the root of the configuration, so `get_parameter`
 * fall-through reaches them from any depth without the environment injecting
 * them module by module:
 *
 *   "nmfc_num_tiles":  8         memory tiles; must be a power of two
 *   "nmfc_grain_bits": 21        log2 of the grain (see nmfc::grain_bytes)
 *   "nmfc_mode_bit":   38        mapping-mode bit, above the top of DRAM
 *
 * `block_size` and `log2_block_size` are already published by the environment.
 */

#ifndef NMFC_CONFIG_H
#define NMFC_CONFIG_H

#include <cstddef>

#include "modules.h"
#include "nmfc/tile_map.h"

namespace nmfc
{

/** Build the system-wide tile map from root globals. */
inline tile_map tile_map_from(const champsim::modules::ModuleBuilder& builder)
{
  const auto num_tiles = builder.get_parameter<std::size_t>("nmfc_num_tiles", true, std::size_t{8});
  const auto block_bits = builder.get_parameter<unsigned>("log2_block_size", true, 6U);
  const auto grain_bits = builder.get_parameter<unsigned>("nmfc_grain_bits", true, 21U);
  const auto mode_bit = builder.get_parameter<unsigned>("nmfc_mode_bit", true, 38U);
  return tile_map{num_tiles, block_bits, grain_bits, mode_bit};
}

/** Read a latency expressed in cycles of this module's own clock. */
inline champsim::chrono::clock::duration cycles_from(const champsim::modules::ModuleBuilder& builder, const std::string& name, std::uint64_t default_cycles)
{
  const auto period = builder.get_parameter<champsim::chrono::picoseconds>("clock_period");
  return builder.get_parameter<std::uint64_t>(name, true, default_cycles) * period;
}

} // namespace nmfc

#endif // NMFC_CONFIG_H
