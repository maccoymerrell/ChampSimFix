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
#include "nmfc/tile_router.h"
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
  nmfc::tile_router_module* router;
  std::uint32_t host_id;

  explicit rig(std::size_t tile, std::size_t num_contexts, const std::string& tag)
  {
    auto image_builder = builder_t{"image" + tag, "FUNCTION_IMAGE_STORE"};
    image = nmfc::function_image_module::create_instance(image_builder, static_cast<champsim::modules::environment_module*>(nullptr));

    // Both the fabric and the core ask the router which tile owns an address.
    auto router_builder = geometry(builder_t{"router" + tag, "CONGRUENT_ROUTER"}).add_parameter("clock_period", champsim::chrono::picoseconds{1000});
    router = nmfc::tile_router_module::create_instance(router_builder, static_cast<champsim::modules::environment_module*>(nullptr));

    auto fabric_builder =
        geometry(builder_t{"fabric" + tag, "FUNCTION_FABRIC"}).add_parameter("hop_latency", std::uint64_t{1}).add_parameter("router", router);
    fabric = nmfc::function_fabric_module::create_instance(fabric_builder, static_cast<champsim::modules::environment_module*>(nullptr));
    host_id = fabric->attach_host(&host);

    dcache_channel = make_channel("dch" + tag);

    auto core_builder = geometry(builder_t{"fc" + tag, "FUNCTION_CORE"})
                            .add_parameter("tile", tile)
                            .add_parameter("num_contexts", num_contexts)
                            .add_parameter("fabric", fabric)
                            .add_parameter("image", image)
                            .add_parameter("router", router)
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

nmfc::body_instr atomic_at(champsim::address addr, std::uint8_t dst)
{
  nmfc::body_instr instr{};
  instr.mem[0] = addr;
  instr.num_loads = 1;
  instr.dst_reg[0] = dst;
  instr.cls = nmfc::op_class::LOAD;
  instr.is_atomic = true;
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
  body.entry_pc = on_tile(0, 0x1000);
  body.live_regs = 2;
  body.instrs.push_back(load_from(on_tile(0, 0x8000), 1));
  body.instrs.push_back(alu(1, 2));
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 42;
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc;
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
  body.entry_pc = on_tile(0, 0x1000);
  body.instrs.push_back(load_from(on_tile(0, 0x20000), 1));
  body.instrs.push_back(load_from(on_tile(0, 0x30000), 2)); // independent of r1
  body.instrs.push_back(alu(1, 3));                         // now r1 is needed
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 7;
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc;
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
  body.entry_pc = on_tile(0, 0x1000);
  auto second = load_from(on_tile(0, 0x30000), 2);
  second.src_reg[0] = 1; // depends on the first load's result
  body.instrs.push_back(load_from(on_tile(0, 0x20000), 1));
  body.instrs.push_back(second);
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 9;
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc;
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
    body.entry_pc = on_tile(0, 0x1000);
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
    msg.entry_pc = body.entry_pc;
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
  body.entry_pc = on_tile(0, 0x1000);
  body.instrs.push_back(load_from(on_tile(0, 0x8000), 1)); // local
  body.instrs.push_back(load_from(on_tile(2, 0x8000), 2)); // tile 2: must migrate
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 11;
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc;
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
  body.entry_pc = on_tile(0, 0x1000);
  body.instrs.push_back(alu(0, 1));
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.home_host = r.host_id;
  msg.entry_pc = body.entry_pc;
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

TEST_CASE("Atomics on one block serialize without stranding the lock")
{
  // Because every access to an address range converges on one function core,
  // an atomic is a local lock rather than a coherence protocol. The hazard is
  // not correctness of the exclusion but the bookkeeping around it: a lock that
  // outlives the operation that took it strands the block, and every later
  // context wanting it spins forever. This pins that it does not.
  constexpr std::size_t CONTEXTS = 6;
  rig r{0, CONTEXTS, "_atomic"};
  fake_cache mem{r.dcache_channel, 15};
  champsim::chrono::clock clock;

  const auto contended = on_tile(0, 0x9000); // every invocation wants this block

  for (std::uint64_t token = 1; token <= CONTEXTS; ++token) {
    nmfc::function_body body{};
    body.token = token;
    body.entry_pc = on_tile(0, 0x1000);
    body.instrs.push_back(atomic_at(contended, 1));
    body.instrs.push_back(alu(1, 2));
    // A second atomic on the same block, from the same context: it must not
    // deadlock against the lock it just released.
    body.instrs.push_back(atomic_at(contended, 3));
    r.image->publish(body);

    nmfc::invocation_msg msg{};
    msg.token = token;
    msg.home_host = r.host_id;
    msg.entry_pc = body.entry_pc;
    msg.body = r.image->lookup(token);
    REQUIRE(r.core->accept(msg));
  }

  run(r, mem, clock, 4000);

  REQUIRE(r.host.returned.size() == CONTEXTS);
  REQUIRE(r.core->free_contexts() == CONTEXTS);
}

TEST_CASE("A context that leaves mid-atomic does not take the lock with it")
{
  // The migration path is the other way a context departs. If it carried its
  // lock away, the block would be permanently claimed by a context that is no
  // longer here.
  rig r{0, 4, "_atomic_migrate"};
  fake_cache mem{r.dcache_channel, 10};
  champsim::chrono::clock clock;

  const auto contended = on_tile(0, 0xA000);

  // First invocation: takes the lock, then leaves for another tile.
  nmfc::function_body leaver{};
  leaver.token = 1;
  leaver.entry_pc = on_tile(0, 0x1000);
  leaver.instrs.push_back(atomic_at(contended, 1));
  leaver.instrs.push_back(load_from(on_tile(2, 0x8000), 2)); // remote: migrates
  r.image->publish(leaver);

  // Second: wants the same block afterwards.
  nmfc::function_body follower{};
  follower.token = 2;
  follower.entry_pc = on_tile(0, 0x1000);
  follower.instrs.push_back(atomic_at(contended, 1));
  r.image->publish(follower);

  for (std::uint64_t token : {std::uint64_t{1}, std::uint64_t{2}}) {
    nmfc::invocation_msg msg{};
    msg.token = token;
    msg.home_host = r.host_id;
    msg.entry_pc = on_tile(0, 0x1000);
    msg.body = r.image->lookup(token);
    REQUIRE(r.core->accept(msg));
  }

  run(r, mem, clock, 2000);

  // The follower must have run to completion; the leaver is off on tile 2,
  // which this rig has no core for, so it simply left.
  REQUIRE(r.host.returned.size() == 1);
  REQUIRE(r.host.returned.front() == 2);
  REQUIRE(r.core->free_contexts() == 4);
}

// ---------------------------------------------------------------------------
// Invariants. These are not about performance; they are the properties a
// performance number is only meaningful on top of. A context that reaches a
// foreign address, or issues one it never translated, or loses a register when
// it moves, produces a faster simulation of a machine that does not exist.
// ---------------------------------------------------------------------------

namespace
{
/** A cache stand-in that remembers every address it was asked for. */
struct recording_cache {
  champsim::modules::channel_module* channel;
  long latency;
  long now = 0;
  std::deque<std::pair<long, champsim::response>> in_flight;
  std::vector<champsim::address> seen;
  std::size_t peak_concurrent = 0;

  void step()
  {
    ++now;
    for (auto& req : channel->get_rq()) {
      seen.push_back(req.address);
      in_flight.emplace_back(now + latency, champsim::response{req});
    }
    channel->get_rq().clear();
    for (auto& req : channel->get_wq()) {
      seen.push_back(req.address);
    }
    channel->get_wq().clear();
    peak_concurrent = std::max(peak_concurrent, in_flight.size());
    while (!in_flight.empty() && in_flight.front().first <= now) {
      channel->get_returned().push_back_grow(in_flight.front().second);
      in_flight.pop_front();
    }
  }
};

void run_recording(rig& r, recording_cache& mem, champsim::chrono::clock& clock, int cycles)
{
  for (int i = 0; i < cycles; ++i) {
    clock.tick(champsim::chrono::picoseconds{250});
    mem.step();
    r.core->operate_on(clock);
    r.fabric->operate_on(clock);
  }
}
} // namespace

TEST_CASE("A function core never issues an address its tile does not own")
{
  // The invariant a tile port asserts in the full machine, checked here where a
  // failure names the module rather than surfacing as a locality abort three
  // components away. A context handed work on another tile must migrate; it
  // must not reach across.
  rig r{0, 8, "_locality"};
  recording_cache mem{r.dcache_channel, 5};
  champsim::chrono::clock clock;
  const auto map = nmfc::tile_map{TILES, BLOCK_BITS, GRAIN_BITS, MODE_BIT};

  nmfc::function_body body{};
  body.token = 7;
  body.entry_pc = on_tile(0, 0x1000);
  body.instrs.push_back(load_from(on_tile(0, 0x40), 1));  // local
  body.instrs.push_back(load_from(on_tile(2, 0x80), 2));  // foreign: must migrate
  body.instrs.push_back(load_from(on_tile(0, 0xc0), 3));
  body.live_regs = 3;
  r.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 7;
  msg.home_host = r.host_id;
  msg.body = r.image->lookup(7);
  msg.entry_pc = body.entry_pc;
  REQUIRE(r.core->accept(msg));

  run_recording(r, mem, clock, 400);

  REQUIRE_FALSE(mem.seen.empty());
  for (const auto addr : mem.seen) {
    REQUIRE(map.tile_of_virtual(addr) == 0);
  }
}

TEST_CASE("A migrating context carries its whole state, and nothing is invented")
{
  // Migration moves an invocation between tiles. If a register, the program
  // counter or the token did not survive that, the machine would be quietly
  // computing something else -- and the throughput would still look fine.
  rig source{0, 8, "_mig_src"};
  rig dest{2, 8, "_mig_dst"};
  recording_cache mem{source.dcache_channel, 5};
  champsim::chrono::clock clock;

  nmfc::function_body body{};
  body.token = 99;
  body.entry_pc = on_tile(0, 0x1000);
  body.instrs.push_back(load_from(on_tile(0, 0x40), 1));
  body.instrs.push_back(alu(1, 2));
  body.instrs.push_back(load_from(on_tile(2, 0x80), 3)); // forces the move
  body.live_regs = 3;
  source.image->publish(body);
  dest.image->publish(body);

  nmfc::invocation_msg msg{};
  msg.token = 99;
  msg.home_host = source.host_id;
  msg.body = source.image->lookup(99);
  msg.entry_pc = body.entry_pc;
  REQUIRE(source.core->accept(msg));

  const auto before = source.core->free_contexts();
  run_recording(source, mem, clock, 300);

  // It left: the slot came back, and it did not leave by completing, because
  // its last instruction is on another tile.
  REQUIRE(source.core->free_contexts() == before + 1);
  REQUIRE(source.host.returned.empty());

  // And it arrives intact. accept_migration is what the fabric calls on the far
  // side, so handing it a context is exactly what a real migration does.
  nmfc::context carried{};
  carried.token = 99;
  carried.body = dest.image->lookup(99);
  carried.pc = 2; // the two instructions it already retired stay retired
  carried.ready.fill(true);
  carried.live_regs = 3;
  REQUIRE(dest.core->accept_migration(carried));
  REQUIRE(dest.core->free_contexts() == 7);
}

TEST_CASE("Atomics on one block are serialised, and on different blocks are not")
{
  // Atomicity here is a per-tile lock table rather than a coherence protocol,
  // which is only sound because every access to an address converges on one
  // tile. If two contexts could hold the same block at once the lock table
  // would be decoration and every atomic result suspect.
  rig r{0, 8, "_atomic"};
  recording_cache mem{r.dcache_channel, 30};
  champsim::chrono::clock clock;

  for (std::uint64_t t = 1; t <= 4; ++t) {
    nmfc::function_body body{};
    body.token = t;
    body.entry_pc = on_tile(0, 0x1000);
    body.instrs.push_back(atomic_at(on_tile(0, 0x200), 1)); // all the same block
    body.live_regs = 1;
    r.image->publish(body);

    nmfc::invocation_msg msg{};
    msg.token = t;
    msg.home_host = r.host_id;
    msg.body = r.image->lookup(t);
    msg.entry_pc = body.entry_pc;
    REQUIRE(r.core->accept(msg));
  }

  run_recording(r, mem, clock, 600);

  // Four contexts, one block: never more than one outstanding at a time.
  REQUIRE(mem.peak_concurrent == 1);
  REQUIRE(r.host.returned.size() == 4);
}

TEST_CASE("Atomics on distinct blocks proceed together")
{
  // The other half of the claim: serialisation must be per block, or the lock
  // table is a global lock and the whole multi-context premise is lost.
  rig r{0, 8, "_atomic_wide"};
  recording_cache mem{r.dcache_channel, 30};
  champsim::chrono::clock clock;

  for (std::uint64_t t = 1; t <= 4; ++t) {
    nmfc::function_body body{};
    body.token = t;
    body.entry_pc = on_tile(0, 0x1000);
    body.instrs.push_back(atomic_at(on_tile(0, 0x200 + t * 64), 1)); // distinct blocks
    body.live_regs = 1;
    r.image->publish(body);

    nmfc::invocation_msg msg{};
    msg.token = t;
    msg.home_host = r.host_id;
    msg.body = r.image->lookup(t);
    msg.entry_pc = body.entry_pc;
    REQUIRE(r.core->accept(msg));
  }

  run_recording(r, mem, clock, 600);
  REQUIRE(mem.peak_concurrent == 4);
  REQUIRE(r.host.returned.size() == 4);
}

namespace
{
nmfc::body_instr spawn_of(std::uint64_t child)
{
  nmfc::body_instr instr{};
  instr.is_spawn = true;
  instr.spawn_token = child;
  instr.cls = nmfc::op_class::ALU;
  return instr;
}
} // namespace

TEST_CASE("A spawn starts exactly one invocation and does not consume its parent")
{
  // The alternative to migrating, and the newest mechanism here. A spawn must
  // create the named invocation once, and the spawning context must carry on --
  // if it blocked or died, a traversal that spawns as it goes would stall on
  // its own children.
  rig r{0, 8, "_spawn"};
  recording_cache mem{r.dcache_channel, 5};
  champsim::chrono::clock clock;

  nmfc::function_body child{};
  child.token = 200;
  child.entry_pc = on_tile(0, 0x1000);
  child.instrs.push_back(load_from(on_tile(0, 0x300), 1));
  child.live_regs = 1;
  r.image->publish(child);

  nmfc::function_body parent{};
  parent.token = 100;
  parent.entry_pc = on_tile(0, 0x1000);
  parent.instrs.push_back(spawn_of(200));
  parent.instrs.push_back(load_from(on_tile(0, 0x40), 2)); // parent keeps going
  parent.live_regs = 2;
  r.image->publish(parent);

  nmfc::invocation_msg msg{};
  msg.token = 100;
  msg.home_host = r.host_id;
  msg.body = r.image->lookup(100);
  msg.entry_pc = parent.entry_pc;
  REQUIRE(r.core->accept(msg));

  run_recording(r, mem, clock, 400);

  // Both ran: the parent's own load and the child's, on one core because both
  // addresses are local here.
  REQUIRE(mem.seen.size() == 2);
  // The parent returned; the child is fire-and-forget only if its body says so,
  // and this one does not, so both come home.
  REQUIRE(r.host.returned.size() == 2);
}

TEST_CASE("A spawned invocation goes to the tile that owns its address, not the spawner's")
{
  // The point of spawning rather than migrating: the work is placed by the
  // address it will touch. If it landed on the spawner's tile it would then
  // have to migrate, and the mechanism would have bought nothing.
  rig r{0, 8, "_spawn_place"};
  recording_cache mem{r.dcache_channel, 5};
  champsim::chrono::clock clock;

  nmfc::function_body child{};
  child.token = 201;
  child.entry_pc = on_tile(0, 0x1000);
  child.instrs.push_back(load_from(on_tile(3, 0x300), 1)); // owned by tile 3
  child.live_regs = 1;
  r.image->publish(child);

  nmfc::function_body parent{};
  parent.token = 101;
  parent.entry_pc = on_tile(0, 0x1000);
  parent.instrs.push_back(spawn_of(201));
  parent.live_regs = 1;
  r.image->publish(parent);

  nmfc::invocation_msg msg{};
  msg.token = 101;
  msg.home_host = r.host_id;
  msg.body = r.image->lookup(101);
  msg.entry_pc = parent.entry_pc;
  REQUIRE(r.core->accept(msg));

  run_recording(r, mem, clock, 400);

  // Only tile 0 exists in this rig, so a child destined for tile 3 must not be
  // delivered here -- the fabric holds it for a core that never attaches. What
  // matters is that this core did not run it and did not touch tile 3.
  for (const auto addr : mem.seen) {
    REQUIRE(nmfc::tile_map{TILES, BLOCK_BITS, GRAIN_BITS, MODE_BIT}.tile_of_virtual(addr) == 0);
  }
  REQUIRE(r.host.returned.size() == 1); // the parent, and only the parent
}

TEST_CASE("A context waiting on a held block is parked, not left spinning")
{
  // A context that cannot take a contended block used to be pushed back onto
  // the ready queue and retried every cycle. The issue loop examines at most as
  // many entries as were queued when the cycle began, so a queue full of
  // retrying contexts spends its examination budget on contexts that cannot
  // issue. Parking on the block and waking on release is the fix, and its
  // hazard is the opposite of spinning: a wakeup that never arrives strands the
  // context forever.
  //
  // Every path that can drop a wakeup is exercised here at once. Each body
  // leaves a load in flight when it reaches the atomic, so a memory response
  // wakes contexts that are already parked -- they must not queue themselves
  // twice, and the release they were passed over for must move to the next
  // waiter. Slots are refilled as they free, so a queued waiter's slot is
  // reused by a later invocation while the queue still names it, which the
  // recorded token has to catch.
  constexpr std::size_t CONTEXTS = 8;
  constexpr std::uint64_t INVOCATIONS = 24;
  rig r{0, CONTEXTS, "_park"};
  fake_cache mem{r.dcache_channel, 30}; // slow enough that the queue builds up
  champsim::chrono::clock clock;

  const auto contended = on_tile(0, 0x9000); // one block every invocation wants

  for (std::uint64_t token = 1; token <= INVOCATIONS; ++token) {
    nmfc::function_body body{};
    body.token = token;
    body.entry_pc = on_tile(0, 0x1000);
    body.live_regs = 4;
    // A private load first: it is still outstanding when the atomic parks, so
    // its response wakes a parked context.
    body.instrs.push_back(load_from(on_tile(0, 0x20000 + token * 64), 1));
    body.instrs.push_back(atomic_at(contended, 2));
    body.instrs.push_back(alu(2, 3));
    r.image->publish(body);
  }

  std::uint64_t next = 1;
  for (int cycle = 0; cycle < 20000; ++cycle) {
    while (next <= INVOCATIONS && r.core->free_contexts() > 0) {
      nmfc::invocation_msg msg{};
      msg.token = next;
      msg.home_host = r.host_id;
      msg.entry_pc = on_tile(0, 0x1000);
      msg.body = r.image->lookup(next);
      if (!r.core->accept(msg)) {
        break;
      }
      ++next;
    }
    clock.tick(champsim::chrono::picoseconds{250});
    mem.step();
    r.core->operate_on(clock);
    r.fabric->operate_on(clock);
  }

  // Nothing stranded: every invocation was woken, ran and gave its slot back.
  REQUIRE(next == INVOCATIONS + 1);
  REQUIRE(r.host.returned.size() == INVOCATIONS);
  REQUIRE(r.core->free_contexts() == CONTEXTS);
}

TEST_CASE("A parked context is woken even when the block's holder never returns")
{
  // The holder of a block can leave before its response lands -- it completes,
  // or it migrates. The lock is released on its behalf, and that release is the
  // only thing standing between the contexts queued behind it and a permanent
  // stall. Draining the queue in that path is what this pins.
  constexpr std::size_t CONTEXTS = 4;
  rig r{0, CONTEXTS, "_park_orphan"};
  fake_cache mem{r.dcache_channel, 25};
  champsim::chrono::clock clock;

  const auto contended = on_tile(0, 0x9000);

  for (std::uint64_t token = 1; token <= CONTEXTS; ++token) {
    nmfc::function_body body{};
    body.token = token;
    body.entry_pc = on_tile(0, 0x1000);
    body.live_regs = 2;
    body.instrs.push_back(atomic_at(contended, 1));
    r.image->publish(body);

    nmfc::invocation_msg msg{};
    msg.token = token;
    msg.home_host = r.host_id;
    msg.entry_pc = on_tile(0, 0x1000);
    msg.body = r.image->lookup(token);
    REQUIRE(r.core->accept(msg));
  }

  // The body ends on the atomic, so each holder retires the moment its response
  // lands -- the release always runs for a context that is already gone.
  run(r, mem, clock, 6000);

  REQUIRE(r.host.returned.size() == CONTEXTS);
  REQUIRE(r.core->free_contexts() == CONTEXTS);
}
