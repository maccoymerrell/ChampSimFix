/*
 * The routing rule is the design's central commitment, and the three models
 * differ in ways that break other modules if they drift: when the tile is
 * known, how many page-table roots that licenses, and whether the allocator has
 * any freedom. These pin each of those down.
 */

#include <catch.hpp>

#include <cstdint>

#include "champsim.h"
#include "chrono.h"
#include "modules.h"
#include "nmfc/tile_map.h"
#include "nmfc/tile_router.h"

namespace
{
constexpr std::size_t TILES = 4;
constexpr unsigned BLOCK_BITS = 6;
constexpr unsigned GRAIN_BITS = 21;
constexpr unsigned MODE_BIT = 38;

using builder_t = champsim::modules::ModuleBuilder;

nmfc::tile_map the_map() { return nmfc::tile_map{TILES, BLOCK_BITS, GRAIN_BITS, MODE_BIT}; }

nmfc::tile_router_module* make_router(const std::string& model, const std::string& tag)
{
  auto b = builder_t{model + tag, model}
               .add_parameter("clock_period", champsim::chrono::picoseconds{1000})
               .add_parameter("nmfc_num_tiles", TILES)
               .add_parameter("log2_block_size", BLOCK_BITS)
               .add_parameter("nmfc_grain_bits", GRAIN_BITS)
               .add_parameter("nmfc_mode_bit", MODE_BIT);
  return nmfc::tile_router_module::create_instance(b, static_cast<champsim::modules::environment_module*>(nullptr));
}

/** A virtual address in grain `g`. */
champsim::address grain_addr(std::uint64_t g) { return champsim::address{g << GRAIN_BITS}; }
} // namespace

TEST_CASE("A congruent router answers before translation, and forces placement")
{
  auto* r = make_router("CONGRUENT_ROUTER", "_c1");
  const auto map = the_map();

  REQUIRE(r->order() == nmfc::routing_order::VIRTUAL_FIRST);

  // The property everything else leans on: the tile is a field of the virtual
  // address, so it is knowable with no lookup and no translation.
  for (std::uint64_t g = 0; g < 4 * TILES; ++g) {
    const auto va = grain_addr(g);
    REQUIRE(r->owner_of(champsim::origin{0, 0}, va) == map.tile_of_virtual(va));
    // And placement is not a choice: a frame must land where the address said,
    // or a context routes to one tile and finds its data on another.
    REQUIRE(r->placement_for(champsim::origin{0, 0}, va) == r->owner_of(champsim::origin{0, 0}, va));
  }
}

TEST_CASE("Routing on the virtual address is what licenses per-channel page tables")
{
  // These two answers are not independent. A router that can name the tile
  // before a walk starts may split the table N ways and keep every walk local;
  // one that cannot must collapse it to a single root, because choosing the
  // root *is* the routing decision. Coupling them here means a new model cannot
  // quietly claim one without the other.
  auto* congruent = make_router("CONGRUENT_ROUTER", "_c2");
  REQUIRE(congruent->order() == nmfc::routing_order::VIRTUAL_FIRST);
  REQUIRE(congruent->page_table_roots() == TILES);

  for (const auto* model : {"PHYSICAL_ROUTER", "NUCA_ROUTER"}) {
    auto* r = make_router(model, "_c2");
    REQUIRE(r->order() == nmfc::routing_order::TRANSLATE_FIRST);
    REQUIRE(r->page_table_roots() == 1);
  }
}

TEST_CASE("A physical router spreads placement instead of deriving it")
{
  // Freedom is the whole point of this model: the same virtual address may be
  // backed anywhere, which is the precondition for ever moving it.
  auto* r = make_router("PHYSICAL_ROUTER", "_p1");
  std::array<std::size_t, TILES> counts{};
  for (std::uint64_t g = 0; g < 8 * TILES; ++g) {
    counts.at(r->placement_for(champsim::origin{0, 0}, grain_addr(g)))++;
  }
  for (const auto n : counts) {
    REQUIRE(n == 8); // round robin, so every tile takes an equal share
  }
}

TEST_CASE("A router that cannot move pages ignores the evidence that it should")
{
  // note_migration is advisory. A static router must tolerate it and stay
  // static: nothing in the core should have to know which model it is talking
  // to before reporting a migration.
  auto* r = make_router("CONGRUENT_ROUTER", "_c3");
  for (std::uint64_t i = 0; i < 1000; ++i) {
    r->note_migration(champsim::origin{0, 0}, grain_addr(i % 16), i % TILES, (i + 1) % TILES, i);
  }
  REQUIRE(r->owner_of(champsim::origin{0, 0}, grain_addr(3)) == the_map().tile_of_virtual(grain_addr(3)));
}
