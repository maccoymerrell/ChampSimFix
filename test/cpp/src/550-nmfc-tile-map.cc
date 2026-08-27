#include <catch.hpp>

#include <cstdint>
#include <random>
#include <set>

#include "nmfc/tile_map.h"

namespace
{
// A DDR5-shaped machine: 8 tiles, 64 B blocks, 2 MiB grain, 256 GiB of DRAM so
// the mapping-mode bit sits at 38.
constexpr std::size_t TILES = 8;
constexpr unsigned BLOCK_BITS = 6;
constexpr unsigned GRAIN_BITS = 21;
constexpr unsigned MODE_BIT = 38;

constexpr nmfc::tile_map make_map() { return nmfc::tile_map{TILES, BLOCK_BITS, GRAIN_BITS, MODE_BIT}; }

constexpr std::uint64_t MODE_MASK = std::uint64_t{1} << MODE_BIT;
} // namespace

TEST_CASE("The grain is the product of row, banks, and channels")
{
  // DDR5: 8 KiB row per rank, 32 banks, 8 channels
  REQUIRE(nmfc::grain_bytes(8 * 1024, 32, 8) == 2 * 1024 * 1024);
  // HBM3: 1 KiB row per pseudo-channel, 16 banks, 32 pseudo-channels
  REQUIRE(nmfc::grain_bytes(1024, 16, 32) == 512 * 1024);

  REQUIRE(nmfc::exact_log2(2 * 1024 * 1024) == 21);
  REQUIRE(nmfc::exact_log2(512 * 1024) == 19);
}

TEST_CASE("The mapping mode round-trips through the address")
{
  constexpr auto uut = make_map();
  const std::uint64_t bare = 0x1234'5670ULL;

  const auto standard = uut.with_mode(bare, nmfc::mapping_mode::STANDARD);
  const auto nmfc_mode = uut.with_mode(bare, nmfc::mapping_mode::NMFC);

  REQUIRE_FALSE(uut.is_nmfc(standard));
  REQUIRE(uut.is_nmfc(nmfc_mode));
  REQUIRE(uut.mode_of(standard) == nmfc::mapping_mode::STANDARD);
  REQUIRE(uut.mode_of(nmfc_mode) == nmfc::mapping_mode::NMFC);

  // Stamping a mode disturbs nothing below the mode bit, and stripping it
  // recovers what the DRAM geometry is actually indexed by.
  REQUIRE(uut.strip_mode(standard) == bare);
  REQUIRE(uut.strip_mode(nmfc_mode) == bare);
}

TEST_CASE("STANDARD mode spreads consecutive blocks across every tile")
{
  constexpr auto uut = make_map();
  const std::uint64_t base = 0; // mode bit clear

  // Consecutive blocks walk the tiles in order and wrap.
  for (std::size_t i = 0; i < 2 * TILES; ++i) {
    REQUIRE(uut.tile_of(base + (i << BLOCK_BITS)) == i % TILES);
  }

  // A whole 2 MiB page therefore touches every tile — which is exactly why
  // page-level siloing is not expressible in this mode.
  std::set<std::size_t> touched;
  for (std::uint64_t off = 0; off < (std::uint64_t{1} << GRAIN_BITS); off += (std::uint64_t{1} << BLOCK_BITS)) {
    touched.insert(uut.tile_of(base + off));
  }
  REQUIRE(touched.size() == TILES);
}

TEST_CASE("NMFC mode keeps a whole grain on one tile")
{
  constexpr auto uut = make_map();
  const std::uint64_t base = MODE_MASK | (std::uint64_t{5} << GRAIN_BITS); // grain-aligned, tile 5

  REQUIRE(uut.tile_of(base) == 5);

  // Every block inside the grain belongs to the same tile.
  std::set<std::size_t> touched;
  for (std::uint64_t off = 0; off < (std::uint64_t{1} << GRAIN_BITS); off += (std::uint64_t{1} << BLOCK_BITS)) {
    touched.insert(uut.tile_of(base + off));
  }
  REQUIRE(touched.size() == 1);
  REQUIRE(*touched.begin() == 5);

  // Consecutive grains walk the tiles, so a contiguous allocation still stripes
  // evenly across the machine without anyone asking it to.
  for (std::size_t i = 0; i < 2 * TILES; ++i) {
    REQUIRE(uut.tile_of(MODE_MASK + (static_cast<std::uint64_t>(i) << GRAIN_BITS)) == i % TILES);
  }
}

TEST_CASE("A function's N code copies on N consecutive grains land on N distinct tiles")
{
  constexpr auto uut = make_map();
  // The dispatcher forms entry_pc_base + t * grain and expects copy t on tile t.
  const std::uint64_t entry_pc_base = MODE_MASK | (std::uint64_t{0x40} << GRAIN_BITS);

  for (std::size_t t = 0; t < TILES; ++t) {
    const auto dispatch_address = entry_pc_base + (static_cast<std::uint64_t>(t) * uut.grain());
    REQUIRE(uut.tile_of(dispatch_address) == t);
  }
}

TEST_CASE("Compaction is exactly invertible over a large address sample")
{
  constexpr auto uut = make_map();
  std::mt19937_64 gen{0xC0FFEE};
  std::uniform_int_distribution<std::uint64_t> dist{0, (std::uint64_t{1} << MODE_BIT) - 1};

  for (int i = 0; i < 200000; ++i) {
    const std::uint64_t body = dist(gen);

    for (auto mode : {nmfc::mapping_mode::STANDARD, nmfc::mapping_mode::NMFC}) {
      const auto addr = uut.with_mode(body, mode);
      const auto round_tripped = uut.expand(uut.compact(addr), uut.tile_of(addr));
      REQUIRE(round_tripped == addr);
    }
  }
}

TEST_CASE("Compaction preserves the mode flag and the low offset")
{
  constexpr auto uut = make_map();

  // The memory controller below the slice still needs the mode to pick a
  // layout, so compaction must not consume it.
  const std::uint64_t nmfc_addr = MODE_MASK | (std::uint64_t{3} << GRAIN_BITS) | 0x1234;
  REQUIRE(uut.is_nmfc(uut.compact(nmfc_addr)));
  REQUIRE((uut.compact(nmfc_addr) & 0xFFF) == 0x234);

  const std::uint64_t std_addr = (std::uint64_t{3} << BLOCK_BITS) | 0x3F;
  REQUIRE_FALSE(uut.is_nmfc(uut.compact(std_addr)));
  REQUIRE((uut.compact(std_addr) & 0x3F) == 0x3F);
}

TEST_CASE("Compaction gives each tile a dense address space")
{
  constexpr auto uut = make_map();

  // The point of compaction: an LLC slice indexes on the compacted address, so
  // the grains that land on one tile must compact to *consecutive* values. If
  // they did not, a slice would use only 1/num_tiles of its sets.
  const std::size_t tile = 5;
  std::uint64_t previous = 0;
  for (std::size_t i = 0; i < 32; ++i) {
    const std::uint64_t addr = MODE_MASK | ((static_cast<std::uint64_t>(i) * TILES + tile) << GRAIN_BITS);
    REQUIRE(uut.tile_of(addr) == tile);

    const auto compacted = uut.compact(addr);
    if (i > 0) {
      REQUIRE(compacted - previous == uut.grain());
    }
    previous = compacted;
  }
}

TEST_CASE("Virtual routing agrees with physical routing under congruent allocation")
{
  constexpr auto uut = make_map();

  // A function core decides local-vs-migrate on the VA, before translation.
  // Congruent allocation is the promise that the frame lands on the same tile,
  // so the two answers must agree for every grain.
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint64_t vaddr = (static_cast<std::uint64_t>(i) << GRAIN_BITS) | 0xABC;
    const auto tile = uut.tile_of_virtual(vaddr);

    // Any frame the allocator may hand back for this VA, so long as it is
    // congruent, routes to the same place.
    const std::uint64_t frame = MODE_MASK | ((static_cast<std::uint64_t>(i * 7 + 1) * TILES + tile) << GRAIN_BITS);
    REQUIRE(uut.tile_of(frame) == tile);
  }
}

TEST_CASE("The tile map works through champsim::address")
{
  const auto uut = make_map();
  const champsim::address addr{MODE_MASK | (std::uint64_t{6} << GRAIN_BITS) | 0x40};

  REQUIRE(uut.is_nmfc(addr));
  REQUIRE(uut.tile_of(addr) == 6);
  REQUIRE(uut.expand(uut.compact(addr), uut.tile_of(addr)) == addr);
}
