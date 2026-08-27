#include <catch.hpp>

#include <cstdint>
#include <deque>
#include <vector>

#include "channel.h"
#include "chrono.h"
#include "modules.h"
#include "nmfc/function_core.h"
#include "nmfc/function_fabric.h"
#include "nmfc/function_image.h"
#include "nmfc/nmfc_types.h"
#include "nmfc/tile_map.h"

namespace
{
constexpr std::size_t TILES = 4;
constexpr unsigned BLOCK_BITS = 6;
constexpr unsigned GRAIN_BITS = 21;
constexpr unsigned MODE_BIT = 38;
constexpr std::uint64_t GRAIN = std::uint64_t{1} << GRAIN_BITS;

using builder_t = champsim::modules::ModuleBuilder;

/** Give every module the same address layout, shadowing whatever globals hold. */
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

/** Records what came back to the compute tile. */
struct recording_host : public nmfc::offload_sink {
  std::vector<std::uint64_t> returned;
  void accept_return(const nmfc::completion_msg& msg) override { returned.push_back(msg.token); }
};

/**
 * Stands in for the tile's data cache: accepts requests and answers them a
 * fixed number of cycles later. Fixed latency is the point — it makes the
 * scheduler's behaviour, not a cache model's, the thing under test.
 */
struct fake_cache {
  champsim::modules::channel_module* channel;
  long latency;
  long now = 0;
  std::deque<std::pair<long, champsim::response>> in_flight;
  std::uint64_t served = 0;
  std::size_t peak_concurrent = 0;

  void step()
  {
    ++now;
    for (auto& req : channel->get_rq()) {
      in_flight.emplace_back(now + latency, champsim::response{req});
      ++served;
    }
    channel->get_rq().clear();
    channel->get_wq().clear();
    peak_concurrent = std::max(peak_concurrent, in_flight.size());

    while (!in_flight.empty() && in_flight.front().first <= now) {
      channel->get_returned().push_back_grow(in_flight.front().second);
      in_flight.pop_front();
    }
  }
};

struct rig {
  nmfc::function_image_module* image;
  nmfc::function_fabric_module* fabric;
  nmfc::function_core_module* core;
  champsim::modules::channel_module* dcache_channel;
  recording_host host;
  std::uint32_t host_id;

  explicit rig(std::size_t tile, std::size_t num_contexts, const std::string& tag)
  {
    auto image_builder = builder_t{"image" + tag, "FUNCTION_IMAGE_STORE"};
    image = nmfc::function_image_module::create_instance(image_builder, static_cast<champsim::modules::environment_module*>(nullptr));

    auto fabric_builder = geometry(builder_t{"fabric" + tag, "FUNCTION_FABRIC"}).add_parameter("hop_latency", std::uint64_t{1});
    fabric = nmfc::function_fabric_module::create_instance(fabric_builder, static_cast<champsim::modules::environment_module*>(nullptr));
    host_id = fabric->attach_host(&host);

    dcache_channel = make_channel("dch" + tag);

    auto core_builder = geometry(builder_t{"fc" + tag, "FUNCTION_CORE"})
                            .add_parameter("tile", tile)
                            .add_parameter("num_contexts", num_contexts)
                            .add_parameter("fabric", fabric)
                            .add_parameter("image", image)
                            .add_parameter("dcache", dcache_channel)
                            .add_parameter("icache", static_cast<champsim::modules::channel_module*>(nullptr))
                            .add_parameter("fetch_latency", std::uint64_t{1});
    core = nmfc::function_core_module::create_instance(core_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  }
};

/** An address on the given tile, offset bytes into its grain. */
champsim::address on_tile(std::size_t tile, std::uint64_t offset) { return champsim::address{static_cast<std::uint64_t>(tile) * GRAIN + offset}; }

nmfc::body_instr load_from(champsim::address addr, std::uint8_t dst)
{
  nmfc::body_instr instr{};
  instr.mem[0] = addr;
  instr.num_loads = 1;
  instr.dst_reg[0] = dst;
  instr.cls = nmfc::op_class::LOAD;
  return instr;
}

nmfc::body_instr alu(std::uint8_t src, std::uint8_t dst)
{
  nmfc::body_instr instr{};
  instr.src_reg[0] = src;
  instr.dst_reg[0] = dst;
  instr.cls = nmfc::op_class::ALU;
  return instr;
}

/** Run the rig forward, stepping the fake cache alongside the modules. */
void run(rig& r, fake_cache& mem, champsim::chrono::clock& clock, int cycles)
{
  for (int i = 0; i < cycles; ++i) {
    clock.tick(champsim::chrono::picoseconds{250});
    mem.step();
    r.core->operate_on(clock);
    r.fabric->operate_on(clock);
  }
}
} // namespace

TEST_CASE("A function core runs an invocation to completion and returns it")
{
  rig r{0, 8, "_complete"};
  fake_cache mem{r.dcache_channel, 20};
  champsim::chrono::clock clock;

  nmfc::function_body body{};
  body.token = 42;
  body.entry_pc_base = on_tile(0, 0x1000);
  body.live_regs = 2;
  body.instrs.push_back(load_from(on_tile(0, 0x8000), 1));
  body.instrs.push_back(alu(1, 2));
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 42;
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc_base;
  msg.body = r.image->lookup(42);

  REQUIRE(r.core->accept(msg));
  REQUIRE(r.core->free_contexts() == 7);

  run(r, mem, clock, 200);

  REQUIRE(r.host.returned.size() == 1);
  REQUIRE(r.host.returned.front() == 42);
  // The slot is recycled, and the body released with it.
  REQUIRE(r.core->free_contexts() == 8);
  REQUIRE(r.image->lookup(42) == nullptr);
}

TEST_CASE("A context sleeps on a request rather than blocking the core")
{
  // Two independent loads with no dependency between them: the in-order
  // scoreboard should let both be outstanding at once, which is the whole
  // mechanism that turns serial kernels into memory-level parallelism.
  rig r{0, 8, "_mlp"};
  fake_cache mem{r.dcache_channel, 30};
  champsim::chrono::clock clock;

  nmfc::function_body body{};
  body.token = 7;
  body.entry_pc_base = on_tile(0, 0x1000);
  body.instrs.push_back(load_from(on_tile(0, 0x20000), 1));
  body.instrs.push_back(load_from(on_tile(0, 0x30000), 2)); // independent of r1
  body.instrs.push_back(alu(1, 3));                         // now r1 is needed
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 7;
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc_base;
  msg.body = r.image->lookup(7);
  REQUIRE(r.core->accept(msg));

  run(r, mem, clock, 300);

  REQUIRE(r.host.returned.size() == 1);
  REQUIRE(mem.served == 2);
  REQUIRE(mem.peak_concurrent == 2); // both in flight together: MLP 2
}

TEST_CASE("A dependent load chain serializes, one request at a time")
{
  // The control for the case above: each load feeds the next, so nothing the
  // scheduler does can overlap them.
  rig r{0, 8, "_chain"};
  fake_cache mem{r.dcache_channel, 30};
  champsim::chrono::clock clock;

  nmfc::function_body body{};
  body.token = 9;
  body.entry_pc_base = on_tile(0, 0x1000);
  auto second = load_from(on_tile(0, 0x30000), 2);
  second.src_reg[0] = 1; // depends on the first load's result
  body.instrs.push_back(load_from(on_tile(0, 0x20000), 1));
  body.instrs.push_back(second);
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 9;
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc_base;
  msg.body = r.image->lookup(9);
  REQUIRE(r.core->accept(msg));

  run(r, mem, clock, 300);

  REQUIRE(r.host.returned.size() == 1);
  REQUIRE(mem.served == 2);
  REQUIRE(mem.peak_concurrent == 1); // strictly serialized
}

TEST_CASE("Many serial contexts overlap into deep memory parallelism")
{
  // The architecture's actual claim: each invocation is a serial pointer chase
  // with MLP 1, but enough of them time-multiplexed keep many requests in
  // flight at once.
  constexpr std::size_t CONTEXTS = 32;
  rig r{0, CONTEXTS, "_deep"};
  fake_cache mem{r.dcache_channel, 40};
  champsim::chrono::clock clock;

  for (std::uint64_t token = 0; token < CONTEXTS; ++token) {
    nmfc::function_body body{};
    body.token = token;
    body.entry_pc_base = on_tile(0, 0x1000);
    // A three-hop dependent chain: MLP 1 within the invocation.
    auto hop2 = load_from(on_tile(0, 0x40000 + token * 256), 2);
    hop2.src_reg[0] = 1;
    auto hop3 = load_from(on_tile(0, 0x80000 + token * 256), 3);
    hop3.src_reg[0] = 2;
    body.instrs.push_back(load_from(on_tile(0, 0x10000 + token * 256), 1));
    body.instrs.push_back(hop2);
    body.instrs.push_back(hop3);
    r.image->publish(body);

    nmfc::invocation_msg msg{};
    msg.token = token;
    msg.home_host = r.host_id;
    msg.entry_pc = body.entry_pc_base;
    msg.body = r.image->lookup(token);
    REQUIRE(r.core->accept(msg));
  }

  run(r, mem, clock, 2000);

  REQUIRE(r.host.returned.size() == CONTEXTS);
  REQUIRE(mem.served == 3 * CONTEXTS);
  // Serial kernels, deep aggregate parallelism -- the point of the design.
  // One in-flight request per context is the ideal, and the scheduler hits it:
  // every context always has its single outstanding load in the memory system.
  REQUIRE(mem.peak_concurrent == CONTEXTS);
}

TEST_CASE("A context migrates when its next address lives on another tile")
{
  rig r{0, 8, "_migrate"};
  fake_cache mem{r.dcache_channel, 10};
  champsim::chrono::clock clock;

  nmfc::function_body body{};
  body.token = 11;
  body.entry_pc_base = on_tile(0, 0x1000);
  body.instrs.push_back(load_from(on_tile(0, 0x8000), 1)); // local
  body.instrs.push_back(load_from(on_tile(2, 0x8000), 2)); // tile 2: must migrate
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 11;
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc_base;
  msg.body = r.image->lookup(11);
  REQUIRE(r.core->accept(msg));

  run(r, mem, clock, 200);

  // Tile 2 has no core attached in this rig, so the fabric holds the migration
  // rather than delivering it -- but the context has left tile 0, which is the
  // behaviour under test, and it never issued the foreign address locally.
  REQUIRE(r.core->free_contexts() == 8);
  REQUIRE(mem.served == 1);
  REQUIRE(r.host.returned.empty());
}

TEST_CASE("A full function core refuses work instead of dropping it")
{
  rig r{0, 2, "_full"};

  nmfc::function_body body{};
  body.token = 100;
  body.entry_pc_base = on_tile(0, 0x1000);
  body.instrs.push_back(alu(0, 1));
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc_base;
  msg.body = r.image->lookup(100);

  msg.token = 100;
  REQUIRE(r.core->accept(msg));
  msg.token = 101;
  REQUIRE(r.core->accept(msg));
  REQUIRE(r.core->free_contexts() == 0);

  // The third is refused, which is the back-pressure that reaches the issuing
  // core's ROB rather than an error to work around.
  msg.token = 102;
  REQUIRE_FALSE(r.core->accept(msg));
}
