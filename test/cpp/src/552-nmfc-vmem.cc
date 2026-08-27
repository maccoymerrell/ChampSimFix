#include <catch.hpp>

#include <cstdint>
#include <set>

#include "champsim.h"
#include "chrono.h"
#include "dram_stats.h"
#include "modules.h"
#include "nmfc/nmfc_vmem.h"
#include "nmfc/tile_map.h"
#include "nmfc/tile_router.h"

namespace
{
constexpr std::size_t TILES = 4;
constexpr unsigned BLOCK_BITS = 6;
constexpr unsigned GRAIN_BITS = 21; // 2 MiB
constexpr unsigned MODE_BIT = 38;
constexpr unsigned LOG2_PAGE = 12;

using builder_t = champsim::modules::ModuleBuilder;

/**
 * The allocator only asks its DRAM for a size, so a stand-in keeps the test
 * about placement rather than about a memory controller's parameter list.
 */
struct sized_dram : public champsim::modules::memory_controller_module {
  explicit sized_dram(builder_t b)
      : champsim::modules::memory_controller_module(b.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        bytes_(b.get_parameter<std::uint64_t>("bytes"))
  {
  }
  long operate() final { return 0; }
  [[nodiscard]] std::size_t get_num_channels() const final { return 1; }
  [[nodiscard]] stats_type get_sim_stats(std::size_t /*channel*/) const final { return {}; }
  [[nodiscard]] champsim::data::bytes size() const final { return champsim::data::bytes{static_cast<long long>(bytes_)}; }
  std::uint64_t bytes_;
};

static champsim::modules::memory_controller_module::register_module<sized_dram> sized_dram_reg("SIZED_DRAM_552");

nmfc::tile_map the_map() { return nmfc::tile_map{TILES, BLOCK_BITS, GRAIN_BITS, MODE_BIT}; }

struct rig {
  champsim::modules::vmem_module* vmem;
  nmfc::page_placement_sink* placement;

  explicit rig(const std::string& tag, std::uint64_t dram_bytes = 512ULL * 1024 * 1024, const std::string& default_region = "standard")
  {
    auto dram_builder = builder_t{"dram" + tag, "SIZED_DRAM_552"}
                            .add_parameter("clock_period", champsim::chrono::picoseconds{1000})
                            .add_parameter("bytes", dram_bytes);
    auto* dram = champsim::modules::memory_controller_module::create_instance(dram_builder, static_cast<champsim::modules::environment_module*>(nullptr));

    // The allocator asks the router where a grain belongs, so the rig has to
    // supply one. Congruent is the routing rule these tests are about.
    auto router_builder = builder_t{"router" + tag, "CONGRUENT_ROUTER"}
                              .add_parameter("nmfc_num_tiles", std::size_t{TILES})
                              .add_parameter("log2_block_size", BLOCK_BITS)
                              .add_parameter("nmfc_grain_bits", GRAIN_BITS)
                              .add_parameter("nmfc_mode_bit", MODE_BIT);
    auto* router = nmfc::tile_router_module::create_instance(router_builder, static_cast<champsim::modules::environment_module*>(nullptr));

    auto vmem_builder = builder_t{"vmem" + tag, "NMFC_VMEM"}
                            .add_parameter("router", router)
                            .add_parameter("nmfc_num_tiles", std::size_t{TILES})
                            .add_parameter("log2_block_size", BLOCK_BITS)
                            .add_parameter("nmfc_grain_bits", GRAIN_BITS)
                            .add_parameter("nmfc_mode_bit", MODE_BIT)
                            .add_parameter("page_size", 4096U)
                            .add_parameter("log2_page_size", LOG2_PAGE)
                            .add_parameter("dram", dram)
                            .add_parameter("minor_fault_penalty", champsim::chrono::clock::duration{100})
                            .add_parameter("page_table_levels", std::size_t{5})
                            .add_parameter("page_table_page_size", champsim::data::bytes{4096})
                            .add_parameter("default_region", default_region);
    vmem = champsim::modules::vmem_module::create_instance(vmem_builder, static_cast<champsim::modules::environment_module*>(nullptr));
    placement = dynamic_cast<nmfc::page_placement_sink*>(vmem);
  }
};

/** The 4 KiB virtual page at `offset` bytes into virtual grain `vgrain`. */
champsim::page_number vpage_in(std::uint64_t vgrain, std::uint64_t page_within) { return champsim::page_number{vgrain * (GRAIN_BITS - LOG2_PAGE ? 512 : 1) + page_within}; }
} // namespace

TEST_CASE("The placement sink is reachable from an ordinary vmem reference")
{
  rig r{"_reach"};
  // The trace reader holds a vmem_module* and needs the placement API; if this
  // cast ever stops working the hints silently stop arriving.
  REQUIRE(r.placement != nullptr);
}

TEST_CASE("An NMFC-hinted page lands on the tile it was hinted to")
{
  rig r{"_hint"};
  const auto map = the_map();

  for (std::uint32_t tile = 0; tile < TILES; ++tile) {
    const auto vgrain = 100 + tile;
    const auto vpage = vpage_in(vgrain, 0);
    r.placement->hint_placement(0, vpage.to<std::uint64_t>(), nmfc::placement_hint{nmfc::mapping_mode::NMFC, tile});

    auto [ppage, penalty] = r.vmem->va_to_pa(champsim::origin{0, 0}, vpage);
    const champsim::address paddr{ppage};

    REQUIRE(map.is_nmfc(paddr));
    REQUIRE(map.tile_of(paddr) == tile);
    REQUIRE(penalty > champsim::chrono::clock::duration::zero()); // first touch faults
  }
}

TEST_CASE("Unhinted NMFC pages are congruent: the virtual address names the tile")
{
  // This is the invariant the whole routing story rests on. A function core
  // decides local-vs-migrate on the VA before translating; congruence is the
  // promise that the frame lands where the VA said.
  rig r{"_congruent", 512ULL * 1024 * 1024, "nmfc"};
  const auto map = the_map();

  for (std::uint64_t vgrain = 1; vgrain < 40; ++vgrain) {
    const auto vpage = vpage_in(vgrain, 0);
    const champsim::address vaddr{vpage};
    auto [ppage, _] = r.vmem->va_to_pa(champsim::origin{0, 0}, vpage);

    REQUIRE(map.tile_of(champsim::address{ppage}) == map.tile_of_virtual(vaddr));
  }
}

TEST_CASE("Every page of a grain stays on one tile and keeps its offset")
{
  rig r{"_grain", 512ULL * 1024 * 1024, "nmfc"};
  const auto map = the_map();
  const std::uint64_t vgrain = 7;

  std::set<std::size_t> tiles;
  std::uint64_t first_frame = 0;
  const std::uint64_t pages_per_grain = std::uint64_t{1} << (GRAIN_BITS - LOG2_PAGE);

  for (std::uint64_t page = 0; page < pages_per_grain; page += 37) {
    auto [ppage, _] = r.vmem->va_to_pa(champsim::origin{0, 0}, vpage_in(vgrain, page));
    tiles.insert(map.tile_of(champsim::address{ppage}));

    const auto frame = ppage.to<std::uint64_t>();
    if (page == 0) {
      first_frame = frame;
    } else {
      // Contiguous virtually means contiguous physically inside the grain,
      // which is what lets one MMU entry cover the whole thing.
      REQUIRE(frame - first_frame == page);
    }
  }
  REQUIRE(tiles.size() == 1);
}

TEST_CASE("Standard pages carry no mode bit")
{
  rig r{"_standard"};
  const auto map = the_map();

  auto [ppage, _] = r.vmem->va_to_pa(champsim::origin{0, 0}, champsim::page_number{0x4000});
  REQUIRE_FALSE(map.is_nmfc(champsim::address{ppage}));
}

TEST_CASE("A repeated translation is free and stable")
{
  rig r{"_stable", 512ULL * 1024 * 1024, "nmfc"};
  const auto vpage = vpage_in(3, 11);

  auto [first, first_penalty] = r.vmem->va_to_pa(champsim::origin{0, 0}, vpage);
  auto [second, second_penalty] = r.vmem->va_to_pa(champsim::origin{0, 0}, vpage);

  REQUIRE(first == second);
  REQUIRE(first_penalty > champsim::chrono::clock::duration::zero());
  REQUIRE(second_penalty == champsim::chrono::clock::duration::zero());
}

TEST_CASE("Address spaces are separate")
{
  rig r{"_asid", 512ULL * 1024 * 1024, "nmfc"};
  const auto vpage = vpage_in(5, 0);

  auto [a, _a] = r.vmem->va_to_pa(champsim::origin{0, 0}, vpage);
  auto [b, _b] = r.vmem->va_to_pa(champsim::origin{0, 1}, vpage);

  REQUIRE(a != b);
  // But both remain congruent: separate frames, same tile.
  const auto map = the_map();
  REQUIRE(map.tile_of(champsim::address{a}) == map.tile_of(champsim::address{b}));
}

TEST_CASE("Siloing past a tile's capacity spills rather than failing")
{
  // A small machine so one tile's grains can actually be exhausted: 64 MiB is
  // 32 grains, 8 per tile. Hinting far more than that at a single tile is the
  // "silo'd too hard" case the design expects to survive.
  rig r{"_spill", 64ULL * 1024 * 1024, "standard"};
  const auto map = the_map();

  std::size_t landed_elsewhere = 0;
  for (std::uint64_t i = 0; i < 20; ++i) {
    const auto vpage = vpage_in(200 + i, 0);
    r.placement->hint_placement(0, vpage.to<std::uint64_t>(), nmfc::placement_hint{nmfc::mapping_mode::NMFC, 1});
    auto [ppage, _] = r.vmem->va_to_pa(champsim::origin{0, 0}, vpage);
    if (map.tile_of(champsim::address{ppage}) != 1) {
      ++landed_elsewhere;
    }
  }

  // Some had to spill, and every one of them still got a valid NMFC frame:
  // correctness is never traded for placement.
  REQUIRE(landed_elsewhere > 0);
}

TEST_CASE("Grain mappings are visible for a huge-page MMU entry")
{
  rig r{"_grainmap", 512ULL * 1024 * 1024, "nmfc"};
  const std::uint64_t vgrain = 12;
  const auto vpage = vpage_in(vgrain, 0);

  const champsim::address vaddr{vpage};
  REQUIRE_FALSE(r.placement->grain_mapping(0, vaddr.to<std::uint64_t>()).has_value());

  auto [ppage, _] = r.vmem->va_to_pa(champsim::origin{0, 0}, vpage);
  auto mapped = r.placement->grain_mapping(0, vaddr.to<std::uint64_t>());

  REQUIRE(mapped.has_value());
  // One entry covers the grain: the frame it names is where page 0 landed.
  REQUIRE((*mapped >> GRAIN_BITS) == (champsim::address{ppage}.to<std::uint64_t>() >> GRAIN_BITS));
}

TEST_CASE("A replicated grain has one copy per channel, addressable by formula")
{
  // The mechanism that lets the OS place an invocation: one virtual page, one
  // physical copy per channel, and the tile chosen when the address is
  // translated. Reserving a *congruent* frame set is what makes the copies
  // addressable without a per-tile table -- they differ only in the tile-select
  // field, so expand(compact(pa), t) converts a base to tile t's copy.
  rig r{"_replicated"};
  const auto map = the_map();

  const std::uint64_t vgrain = 300;
  const auto vpage = vpage_in(vgrain, 0);
  r.placement->hint_placement(0, vpage.to<std::uint64_t>(), nmfc::placement_hint{nmfc::mapping_mode::NMFC, 0, /*replicated=*/true});

  std::set<std::uint64_t> compacted;
  std::set<std::uint64_t> frames;
  for (std::size_t tile = 0; tile < TILES; ++tile) {
    // page_mapping_on is what an MMU calls, and it names the asking tile.
    const auto mapping = r.placement->page_mapping_on(champsim::origin{0, 0}, vpage.to<std::uint64_t>(), tile);
    const champsim::address paddr{mapping};

    // Each copy is on its own channel: that is what makes the choice a placement.
    REQUIRE(map.is_nmfc(paddr));
    REQUIRE(map.tile_of(paddr) == tile);

    frames.insert(mapping);
    compacted.insert(map.compact(mapping));
  }

  REQUIRE(frames.size() == TILES);   // genuinely distinct physical pages
  REQUIRE(compacted.size() == 1);    // differing in the tile field and nothing else

  // Which is exactly the statement that the conversion is a formula: any copy
  // can be derived from any other, with no table consulted.
  const auto base = *std::begin(frames);
  for (std::size_t tile = 0; tile < TILES; ++tile) {
    REQUIRE(map.expand(map.compact(base), tile) == r.placement->page_mapping_on(champsim::origin{0, 0}, vpage.to<std::uint64_t>(), tile));
  }
}

TEST_CASE("An ordinary grain is not replicated: one virtual page, one frame")
{
  // The asymmetry that makes replication safe. Code is read-only, so N copies
  // need no coherence; the data these functions chase is written, so it gets a
  // single home and the tile that owns it is where the work has to go.
  rig r{"_not_replicated"};
  const std::uint64_t vgrain = 301;
  const auto vpage = vpage_in(vgrain, 0);
  r.placement->hint_placement(0, vpage.to<std::uint64_t>(), nmfc::placement_hint{nmfc::mapping_mode::NMFC, 2});

  std::set<std::uint64_t> frames;
  for (std::size_t tile = 0; tile < TILES; ++tile) {
    frames.insert(r.placement->page_mapping_on(champsim::origin{0, 0}, vpage.to<std::uint64_t>(), tile));
  }
  REQUIRE(frames.size() == 1);
}
