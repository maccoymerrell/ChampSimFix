/*
 * NMFC_MMU had no coverage at all, which is uncomfortable for the module that
 * decides what physical address every function-core access actually touches.
 *
 * It is two faces on one object: a translation engine the function cores call
 * directly, and a channel model the host's caches translate through. The things
 * worth pinning are the ones whose failure is silent -- a translation that comes
 * back for a different address than was asked for, a huge mapping reported as a
 * small one, or a page that moved without the cached answer being discarded.
 */

#include <catch.hpp>

#include <cstdint>
#include <set>
#include <vector>

#include "champsim.h"
#include "channel.h"
#include "chrono.h"
#include "dram_stats.h"
#include "modules.h"
#include "nmfc/nmfc_vmem.h"
#include "nmfc/tile_map.h"
#include "nmfc/tile_router.h"
#include "nmfc/translation_engine.h"

namespace
{
constexpr std::size_t TILES = 4;
constexpr unsigned BLOCK_BITS = 6;
constexpr unsigned GRAIN_BITS = 21;
constexpr unsigned MODE_BIT = 38;
constexpr unsigned LOG2_PAGE = 12;

using builder_t = champsim::modules::ModuleBuilder;

nmfc::tile_map the_map() { return nmfc::tile_map{TILES, BLOCK_BITS, GRAIN_BITS, MODE_BIT}; }

builder_t geometry(builder_t b)
{
  return b.add_parameter("clock_period", champsim::chrono::picoseconds{250})
      .add_parameter("nmfc_num_tiles", std::size_t{TILES})
      .add_parameter("log2_block_size", BLOCK_BITS)
      .add_parameter("nmfc_grain_bits", GRAIN_BITS)
      .add_parameter("nmfc_mode_bit", MODE_BIT)
      .add_parameter("log2_page_size", LOG2_PAGE);
}

struct sized_dram_557 : public champsim::modules::memory_controller_module {
  explicit sized_dram_557(builder_t b)
      : champsim::modules::memory_controller_module(b.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        bytes_(b.get_parameter<std::uint64_t>("bytes"))
  {
  }
  long operate() final { return 0; }
  [[nodiscard]] std::size_t get_num_channels() const final { return 1; }
  [[nodiscard]] stats_type get_sim_stats(std::size_t) const final { return {}; }
  [[nodiscard]] champsim::data::bytes size() const final { return champsim::data::bytes{static_cast<long long>(bytes_)}; }
  std::uint64_t bytes_;
};
static champsim::modules::memory_controller_module::register_module<sized_dram_557> sized_dram_557_reg("SIZED_DRAM_557");

champsim::modules::channel_module* make_channel(const std::string& name)
{
  auto b = builder_t{name, "DEFAULT_CHANNEL"}
               .add_parameter("rq_size", std::size_t{64})
               .add_parameter("wq_size", std::size_t{64})
               .add_parameter("pq_size", std::size_t{64})
               .add_parameter("offset_bits", champsim::data::bits{BLOCK_BITS})
               .add_parameter("match_offset_bits", false);
  return champsim::modules::channel_module::create_instance(b, static_cast<champsim::modules::environment_module*>(nullptr));
}

struct mmu_rig {
  champsim::modules::vmem_module* vmem;
  nmfc::page_placement_sink* placement;
  champsim::modules::channel_module* lower;
  champsim::modules::channel_module* mmu_channel;
  nmfc::translation_engine* engine;
  champsim::operable* operable;

  explicit mmu_rig(const std::string& tag, std::size_t tile = 0)
  {
    auto dram_b = builder_t{"d557" + tag, "SIZED_DRAM_557"}
                      .add_parameter("clock_period", champsim::chrono::picoseconds{1000})
                      .add_parameter("bytes", std::uint64_t{512} << 20);
    auto* dram = champsim::modules::memory_controller_module::create_instance(dram_b, static_cast<champsim::modules::environment_module*>(nullptr));

    auto router_b = geometry(builder_t{"r557" + tag, "CONGRUENT_ROUTER"}).add_parameter("clock_period", champsim::chrono::picoseconds{1000});
    auto* router = nmfc::tile_router_module::create_instance(router_b, static_cast<champsim::modules::environment_module*>(nullptr));

    auto vmem_b = geometry(builder_t{"v557" + tag, "NMFC_VMEM"})
                      .add_parameter("router", router)
                      .add_parameter("dram", dram)
                      .add_parameter("page_size", 4096U)
                      .add_parameter("minor_fault_penalty", champsim::chrono::clock::duration{100})
                      .add_parameter("page_table_levels", std::size_t{5})
                      .add_parameter("page_table_page_size", champsim::data::bytes{4096})
                      .add_parameter("default_region", std::string{"standard"});
    vmem = champsim::modules::vmem_module::create_instance(vmem_b, static_cast<champsim::modules::environment_module*>(nullptr));
    placement = dynamic_cast<nmfc::page_placement_sink*>(vmem);

    lower = make_channel("l557" + tag);
    auto mmu_b = geometry(builder_t{"m557" + tag, "NMFC_MMU"})
                     .add_parameter("tile", tile)
                     .add_parameter("vmem", vmem)
                     .add_parameter("lower_level", lower)
                     .add_parameter("small_sets", std::size_t{32})
                     .add_parameter("small_ways", std::size_t{4})
                     .add_parameter("huge_sets", std::size_t{16})
                     .add_parameter("huge_ways", std::size_t{4})
                     .add_parameter("hit_latency", std::uint64_t{1})
                     .add_parameter("mshr_size", std::size_t{32});
    mmu_channel = champsim::modules::channel_module::create_instance(mmu_b, static_cast<champsim::modules::environment_module*>(nullptr));
    engine = dynamic_cast<nmfc::translation_engine*>(mmu_channel);
    operable = dynamic_cast<champsim::operable*>(mmu_channel);
  }

  /** Run the MMU, answering every walk reference it makes. */
  void run(champsim::chrono::clock& clock, int cycles)
  {
    for (int i = 0; i < cycles; ++i) {
      clock.tick(champsim::chrono::picoseconds{250});
      for (auto& req : lower->get_rq()) {
        lower->get_returned().push_back_grow(champsim::response{req});
      }
      lower->get_rq().clear();
      operable->operate_on(clock);
    }
  }
};

champsim::address page_in(std::uint64_t vgrain, std::uint64_t page) { return champsim::address{(vgrain << GRAIN_BITS) | (page << LOG2_PAGE)}; }
} // namespace

TEST_CASE("The MMU is reachable as both a channel and a translation engine")
{
  // One module, two faces: function cores call it directly, host caches
  // translate through it. If either cast stops working the wiring silently
  // falls back to something else.
  mmu_rig r{"_faces"};
  REQUIRE(r.engine != nullptr);
  REQUIRE(r.operable != nullptr);
  REQUIRE(r.placement != nullptr);
}

TEST_CASE("A translation answers the address it was asked about, with its tag")
{
  // The tag is opaque to the MMU and is how a function core knows which of its
  // contexts a completion belongs to. Returning the wrong one would hand a
  // context another context's physical address.
  mmu_rig r{"_answer"};
  champsim::chrono::clock clock;
  const auto va = page_in(64, 3);
  r.placement->hint_placement(0, champsim::page_number{va}.to<std::uint64_t>(), nmfc::placement_hint{nmfc::mapping_mode::NMFC, 0});

  REQUIRE(r.engine->request_translation(0xABCD, champsim::origin{0, 0}, va));
  r.run(clock, 400);

  auto& done = r.engine->translation_completions();
  REQUIRE_FALSE(done.empty());
  const auto& first = done.front();
  REQUIRE(first.tag == 0xABCD);

  // It resolved at grain granularity, and the page it names must contain the
  // address that was asked about.
  const auto shift = first.huge ? GRAIN_BITS : LOG2_PAGE;
  REQUIRE(first.vpage == (va.to<std::uint64_t>() >> shift));
}

TEST_CASE("Repeating a translation is answered from the array, without a walk")
{
  // The second ask must not go to memory. If it did, the arrays would be
  // decoration and every latency measured through them would be wrong.
  mmu_rig r{"_reuse"};
  champsim::chrono::clock clock;
  const auto va = page_in(70, 1);
  r.placement->hint_placement(0, champsim::page_number{va}.to<std::uint64_t>(), nmfc::placement_hint{nmfc::mapping_mode::NMFC, 0});

  REQUIRE(r.engine->request_translation(1, champsim::origin{0, 0}, va));
  r.run(clock, 400);
  r.engine->translation_completions().clear();

  std::size_t walks = 0;
  REQUIRE(r.engine->request_translation(2, champsim::origin{0, 0}, va));
  for (int i = 0; i < 200; ++i) {
    clock.tick(champsim::chrono::picoseconds{250});
    walks += r.lower->get_rq().size();
    for (auto& req : r.lower->get_rq()) {
      r.lower->get_returned().push_back_grow(champsim::response{req});
    }
    r.lower->get_rq().clear();
    r.operable->operate_on(clock);
  }

  REQUIRE_FALSE(r.engine->translation_completions().empty());
  REQUIRE(walks == 0);
}

TEST_CASE("Distinct virtual pages get distinct translations")
{
  // An MMU that returned one frame for two pages would be aliasing the whole
  // address space, and every result computed on top of it would be meaningless.
  mmu_rig r{"_distinct"};
  champsim::chrono::clock clock;
  std::set<std::uint64_t> frames;

  for (std::uint64_t g = 0; g < 8; ++g) {
    const auto va = page_in(100 + g * TILES, 0); // same tile each time
    r.placement->hint_placement(0, champsim::page_number{va}.to<std::uint64_t>(), nmfc::placement_hint{nmfc::mapping_mode::NMFC, 0});
    REQUIRE(r.engine->request_translation(g, champsim::origin{0, 0}, va));
    r.run(clock, 400);
  }
  for (const auto& done : r.engine->translation_completions()) {
    frames.insert(done.ppage);
  }
  REQUIRE(frames.size() == 8);
}
