/*
 * The three channel models between a requester and its DRAM. None of them had
 * any coverage, and each enforces something the rest of the machine assumes:
 *
 *   TILE_PORT        a function core touches only its own tile, and the address
 *                    the slice sees is compacted so a slice uses all its sets.
 *   INTERLEAVE_FABRIC a request reaches the slice that owns its address.
 *   DRAM_MODE_PORT   the memory controller receives a channel-local physical
 *                    address, with the mapping-mode tag off.
 *
 * All three are round trips: whatever is done to an address on the way down has
 * to be undone on the way back, or the cache above cannot match the response to
 * the request it sent.
 */

#include <catch.hpp>

#include <cstdint>
#include <vector>

#include "champsim.h"
#include "channel.h"
#include "chrono.h"
#include "modules.h"
#include "nmfc/tile_map.h"

namespace
{
constexpr std::size_t TILES = 4;
constexpr unsigned BLOCK_BITS = 6;
constexpr unsigned GRAIN_BITS = 21;
constexpr unsigned MODE_BIT = 38;
constexpr std::uint64_t GRAIN = std::uint64_t{1} << GRAIN_BITS;

using builder_t = champsim::modules::ModuleBuilder;

nmfc::tile_map the_map() { return nmfc::tile_map{TILES, BLOCK_BITS, GRAIN_BITS, MODE_BIT}; }

builder_t geometry(builder_t b)
{
  return b.add_parameter("clock_period", champsim::chrono::picoseconds{250})
      .add_parameter("nmfc_num_tiles", std::size_t{TILES})
      .add_parameter("log2_block_size", BLOCK_BITS)
      .add_parameter("nmfc_grain_bits", GRAIN_BITS)
      .add_parameter("nmfc_mode_bit", MODE_BIT);
}

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

/** An NMFC-mode physical address on `tile`, `offset` into its grain. */
champsim::address nmfc_addr(std::size_t tile, std::uint64_t offset)
{
  return champsim::address{(std::uint64_t{1} << MODE_BIT) | (static_cast<std::uint64_t>(tile) * GRAIN) | offset};
}

champsim::request read_of(champsim::address addr)
{
  champsim::request req{};
  req.address = addr;
  req.v_address = addr;
  req.is_translated = true;
  return req;
}
} // namespace

TEST_CASE("A tile port compacts on the way down and expands on the way back")
{
  // The slice is indexed by the compacted address, because the tile-select bits
  // sit inside its set index and a slice would otherwise use 1/N of its sets.
  // The cache above still holds the original, so the response has to carry it.
  auto* lower = make_channel("tp_lower");
  auto b = geometry(builder_t{"tp", "TILE_PORT"})
               .add_parameter("tile", std::size_t{2})
               .add_parameter("lower", lower)
               .add_parameter("latency", std::uint64_t{1})
               .add_parameter("queue_size", std::size_t{32})
               .add_parameter("max_forward", champsim::bandwidth::maximum_type{4})
               .add_parameter("strict_locality", true);
  auto* port = champsim::modules::channel_module::create_instance(b, static_cast<champsim::modules::environment_module*>(nullptr));

  const auto map = the_map();
  const auto addr = nmfc_addr(2, 0x400);
  REQUIRE(port->add_rq(read_of(addr)));

  champsim::chrono::clock clock;
  auto* operable = dynamic_cast<champsim::operable*>(port);
  REQUIRE(operable != nullptr);
  clock.tick(champsim::chrono::picoseconds{250});
  operable->operate_on(clock);
  clock.tick(champsim::chrono::picoseconds{250});
  operable->operate_on(clock);

  REQUIRE(lower->get_rq().size() == 1);
  const auto seen = lower->get_rq().front().address;
  REQUIRE(seen == map.compact(addr));      // the slice sees a dense address
  REQUIRE(seen != addr);                   // and it is genuinely different

  // Answer it, and the original address must come back.
  lower->get_returned().push_back_grow(champsim::response{lower->get_rq().front()});
  lower->get_rq().clear();
  for (int i = 0; i < 8; ++i) {
    clock.tick(champsim::chrono::picoseconds{250});
    operable->operate_on(clock);
  }
  REQUIRE_FALSE(port->get_returned().empty());
  REQUIRE(port->get_returned().front().address == addr);
}

TEST_CASE("An interleave fabric sends a request to the slice that owns it")
{
  // Routing is read straight out of the physical address, so a request never
  // waits on a lookup to find its slice.
  std::vector<champsim::modules::channel_module*> slices;
  for (std::size_t t = 0; t < TILES; ++t) {
    slices.push_back(make_channel("if_slice" + std::to_string(t)));
  }
  auto b = geometry(builder_t{"if", "INTERLEAVE_FABRIC"})
               .add_parameter("tiles", slices)
               .add_parameter("hop_latency", std::uint64_t{1})
               .add_parameter("queue_size", std::size_t{64})
               .add_parameter("max_forward", champsim::bandwidth::maximum_type{4})
               .add_parameter("compact_tile_bits", true);
  auto* fabric = champsim::modules::channel_module::create_instance(b, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* operable = dynamic_cast<champsim::operable*>(fabric);
  REQUIRE(operable != nullptr);

  const auto map = the_map();
  for (std::size_t t = 0; t < TILES; ++t) {
    REQUIRE(fabric->add_rq(read_of(nmfc_addr(t, 0x80))));
  }

  champsim::chrono::clock clock;
  for (int i = 0; i < 12; ++i) {
    clock.tick(champsim::chrono::picoseconds{250});
    operable->operate_on(clock);
  }

  for (std::size_t t = 0; t < TILES; ++t) {
    INFO("slice " << t);
    REQUIRE(slices[t]->get_rq().size() == 1);
    // Compacted, and unambiguously this tile's address rather than a neighbour's.
    REQUIRE(slices[t]->get_rq().front().address == map.compact(nmfc_addr(t, 0x80)));
  }
}

TEST_CASE("A DRAM mode port strips the tag going down and restores it coming back")
{
  // The tag is above every field a DRAM address mapping has. Left on, an
  // NMFC-mode address and the STANDARD-mode one at the same frame collapse onto
  // one row. Stripped and not restored, the cache above cannot match the
  // response to its outstanding request.
  auto* lower = make_channel("dmp_lower");
  auto b = geometry(builder_t{"dmp", "DRAM_MODE_PORT"}).add_parameter("lower", lower);
  auto* port = champsim::modules::channel_module::create_instance(b, static_cast<champsim::modules::environment_module*>(nullptr));

  const auto map = the_map();
  const auto tagged = nmfc_addr(1, 0x2c0);
  REQUIRE(map.is_nmfc(tagged));

  REQUIRE(port->add_rq(read_of(tagged)));
  REQUIRE(lower->get_rq().size() == 1);
  const auto handed_down = lower->get_rq().front().address;
  REQUIRE_FALSE(map.is_nmfc(handed_down));            // the tag is gone
  REQUIRE(handed_down == map.strip_mode(tagged));     // and nothing else changed

  lower->get_returned().push_back_grow(champsim::response{lower->get_rq().front()});
  lower->get_rq().clear();
  REQUIRE(port->get_returned().size() == 1);
  REQUIRE(port->get_returned().front().address == tagged); // restored for the cache above
}

TEST_CASE("A DRAM mode port leaves a standard-mode address alone")
{
  // Only NMFC-mode addresses carry the tag, so a STANDARD one must pass through
  // untouched -- and must not come back with a tag it never had.
  auto* lower = make_channel("dmp2_lower");
  auto b = geometry(builder_t{"dmp2", "DRAM_MODE_PORT"}).add_parameter("lower", lower);
  auto* port = champsim::modules::channel_module::create_instance(b, static_cast<champsim::modules::environment_module*>(nullptr));

  const auto plain = champsim::address{0x4'0000ULL};
  REQUIRE_FALSE(the_map().is_nmfc(plain));

  REQUIRE(port->add_rq(read_of(plain)));
  REQUIRE(lower->get_rq().front().address == plain);

  lower->get_returned().push_back_grow(champsim::response{lower->get_rq().front()});
  lower->get_rq().clear();
  REQUIRE(port->get_returned().front().address == plain);
}
