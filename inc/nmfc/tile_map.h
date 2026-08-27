/*
 * NMFC address mapping: which memory tile owns an address, and the compaction
 * that removes the tile-select field so each tile sees a dense address space.
 *
 * This sits on every memory request, so it is header-only, allocation-free, and
 * arithmetic only. It is also the single place the address layout is written
 * down: the interleave fabric, the LLC slices, the memory controllers, the
 * function cores, and the page allocator all route through it, so there is one
 * definition to get right rather than six to keep in agreement.
 *
 * Layout
 * ------
 * The mapping mode is one physical address bit, one position above the top of
 * the DRAM range, stamped at allocation and never changed thereafter:
 *
 *   mode = 0  STANDARD  tile-select bits sit just above the block offset, so a
 *                       page spreads across every channel at block granularity.
 *                       Maximum bandwidth for streaming traffic; no siloing.
 *
 *   mode = 1  NMFC      tile-select bits sit just above the grain offset, so a
 *                       whole grain-sized unit lives on one channel while its
 *                       blocks still spread across that channel's banks.
 *                       Siloing is expressible; a hot structure concentrates.
 *
 * Both modes consume exactly one grain of capacity from disjoint DRAM
 * resources, which is what licenses tagging the mode per unit rather than
 * partitioning the address space. See docs/nmfc/DESIGN.md §5.
 *
 * Compaction
 * ----------
 * Tile-select bits fall inside an LLC slice's set index, so a slice would
 * otherwise use only 1/num_tiles of its sets. compact() removes the field and
 * expand() puts it back; both are pure functions of (address, tile), so the
 * fabric needs no per-request bookkeeping. The mode bit is left in place, since
 * the memory controller below still needs it to pick a layout.
 */

#ifndef NMFC_TILE_MAP_H
#define NMFC_TILE_MAP_H

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "address.h"

namespace nmfc
{

/** Which DRAM address layout backs an address. Mirrors nmfc::region in nmfc_trace.h. */
enum class mapping_mode : std::uint8_t { STANDARD = 0, NMFC = 1 };

/** ceil-free integer log2 for exact powers of two. */
constexpr unsigned exact_log2(std::uint64_t value)
{
  unsigned bits = 0;
  while ((value >>= 1U) != 0U) {
    ++bits;
  }
  return bits;
}

constexpr bool is_power_of_two(std::uint64_t value) { return value != 0 && (value & (value - 1)) == 0; }

/**
 * The grain: the smallest unit that can carry its own mapping mode.
 *
 * A per-unit mode is only safe when the unit owns whole DRAM rows, so two units
 * in different modes can never contend for the same bank and column slots. That
 * threshold is forced by the geometry, not chosen:
 *
 *     G = row_bytes_per_channel × banks_per_channel × num_channels
 *
 * DDR5 (8 KiB row, 32 banks, 8 channels) lands on 2 MiB; HBM3 (1 KiB row, 16
 * banks, 32 pseudo-channels) lands on 512 KiB. The same number is also the
 * siloing granularity and the NMFC-data page size — all three are the same
 * quantity seen from different sides.
 */
constexpr std::uint64_t grain_bytes(std::uint64_t row_bytes_per_channel, std::uint64_t banks_per_channel, std::uint64_t num_channels)
{
  return row_bytes_per_channel * banks_per_channel * num_channels;
}

class tile_map
{
public:
  /**
   * \param num_tiles   memory tiles; must be a power of two (a modulo would not be invertible)
   * \param block_bits  log2 of the cache block size — where STANDARD tile bits sit
   * \param grain_bits  log2 of the grain (see grain_bytes) — where NMFC tile bits sit
   * \param mode_bit    the mapping-mode bit position, above the top of the DRAM range
   */
  constexpr tile_map(std::size_t num_tiles, unsigned block_bits, unsigned grain_bits, unsigned mode_bit)
      : num_tiles_(num_tiles), tile_bits_(exact_log2(static_cast<std::uint64_t>(num_tiles))), block_bits_(block_bits), grain_bits_(grain_bits),
        mode_bit_(mode_bit), tile_mask_(num_tiles - 1), mode_mask_(std::uint64_t{1} << mode_bit)
  {
    assert(is_power_of_two(static_cast<std::uint64_t>(num_tiles)));
    assert(grain_bits >= block_bits);
    // The tile field must fit strictly below the mode bit in both layouts, or
    // compaction would collide with the mode flag.
    assert(mode_bit >= grain_bits + tile_bits_);
    assert(mode_bit < 64);
  }

  [[nodiscard]] constexpr std::size_t num_tiles() const { return num_tiles_; }
  [[nodiscard]] constexpr unsigned tile_bits() const { return tile_bits_; }
  [[nodiscard]] constexpr unsigned block_bits() const { return block_bits_; }
  [[nodiscard]] constexpr unsigned grain_bits() const { return grain_bits_; }
  [[nodiscard]] constexpr unsigned mode_bit() const { return mode_bit_; }
  [[nodiscard]] constexpr std::uint64_t grain() const { return std::uint64_t{1} << grain_bits_; }

  // ---- mode ----

  [[nodiscard]] constexpr bool is_nmfc(std::uint64_t addr) const { return (addr & mode_mask_) != 0; }
  [[nodiscard]] constexpr mapping_mode mode_of(std::uint64_t addr) const { return is_nmfc(addr) ? mapping_mode::NMFC : mapping_mode::STANDARD; }

  /** Stamp a mapping mode onto an address that does not yet carry one. */
  [[nodiscard]] constexpr std::uint64_t with_mode(std::uint64_t addr, mapping_mode mode) const
  {
    return mode == mapping_mode::NMFC ? (addr | mode_mask_) : (addr & ~mode_mask_);
  }

  /** Drop the mode flag, yielding the address the DRAM geometry is indexed by. */
  [[nodiscard]] constexpr std::uint64_t strip_mode(std::uint64_t addr) const { return addr & ~mode_mask_; }

  // ---- routing ----

  /** The tile owning this address, read straight out of the address. No lookup. */
  [[nodiscard]] constexpr std::size_t tile_of(std::uint64_t addr) const
  {
    return static_cast<std::size_t>((addr >> select_shift(is_nmfc(addr))) & tile_mask_);
  }

  /**
   * The tile owning a *virtual* address in NMFC mode. Function cores decide
   * local-vs-migrate with this, on the VA they already hold, before any
   * translation has happened — which is why a migration decision never waits on
   * the MMU. Congruent allocation guarantees the frame lands on the same tile.
   */
  [[nodiscard]] constexpr std::size_t tile_of_virtual(std::uint64_t vaddr) const { return static_cast<std::size_t>((vaddr >> grain_bits_) & tile_mask_); }

  // ---- compaction ----

  /** Remove the tile-select field. The mode flag is preserved in place. */
  [[nodiscard]] constexpr std::uint64_t compact(std::uint64_t addr) const
  {
    const std::uint64_t mode = addr & mode_mask_;
    const std::uint64_t body = addr & (mode_mask_ - 1);
    const unsigned shift = select_shift(mode != 0);
    const std::uint64_t low = body & ((std::uint64_t{1} << shift) - 1);
    const std::uint64_t high = (body >> (shift + tile_bits_)) << shift;
    return mode | high | low;
  }

  /**
   * Remove the tile-select field from a *virtual* address.
   *
   * Channel t's page table covers exactly the virtual addresses whose frames
   * come from channel t -- a partition of the address space, not a replication
   * of it, so the total page-table size is unchanged. That set is strided, so
   * the table is indexed by the address with the tile field taken out, which
   * makes it dense and, more importantly, makes every page-table page cover
   * only one tile's addresses. Without that, an upper-level page spans every
   * tile and a walk cannot be local.
   */
  [[nodiscard]] constexpr std::uint64_t compact_virtual(std::uint64_t vaddr) const
  {
    const std::uint64_t low = vaddr & ((std::uint64_t{1} << grain_bits_) - 1);
    const std::uint64_t high = (vaddr >> (grain_bits_ + tile_bits_)) << grain_bits_;
    return high | low;
  }

  /** Reinsert the tile-select field. Exactly inverts compact(). */
  [[nodiscard]] constexpr std::uint64_t expand(std::uint64_t compacted, std::size_t tile) const
  {
    const std::uint64_t mode = compacted & mode_mask_;
    const std::uint64_t body = compacted & (mode_mask_ - 1);
    const unsigned shift = select_shift(mode != 0);
    const std::uint64_t low = body & ((std::uint64_t{1} << shift) - 1);
    const std::uint64_t high = (body >> shift) << (shift + tile_bits_);
    return mode | high | (static_cast<std::uint64_t>(tile & tile_mask_) << shift) | low;
  }

  // ---- champsim::address convenience ----

  [[nodiscard]] std::size_t tile_of(champsim::address addr) const { return tile_of(addr.to<std::uint64_t>()); }
  [[nodiscard]] std::size_t tile_of_virtual(champsim::address addr) const { return tile_of_virtual(addr.to<std::uint64_t>()); }
  [[nodiscard]] bool is_nmfc(champsim::address addr) const { return is_nmfc(addr.to<std::uint64_t>()); }
  [[nodiscard]] champsim::address compact(champsim::address addr) const { return champsim::address{compact(addr.to<std::uint64_t>())}; }
  [[nodiscard]] champsim::address expand(champsim::address addr, std::size_t tile) const
  {
    return champsim::address{expand(addr.to<std::uint64_t>(), tile)};
  }

private:
  /** Where the tile-select field starts, which is the only thing the mode changes. */
  [[nodiscard]] constexpr unsigned select_shift(bool nmfc) const { return nmfc ? grain_bits_ : block_bits_; }

  std::size_t num_tiles_;
  unsigned tile_bits_;
  unsigned block_bits_;
  unsigned grain_bits_;
  unsigned mode_bit_;
  std::uint64_t tile_mask_;
  std::uint64_t mode_mask_;
};

} // namespace nmfc

#endif // NMFC_TILE_MAP_H
