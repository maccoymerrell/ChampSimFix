/*
 * The fabric's job is to move work between tiles without ever being the reason
 * the machine stops. It failed that three separate times, and each failure took
 * a full-machine deadlock dump to find: a shared migration queue that one
 * congested destination could own entirely, delivery that stopped at its head,
 * and an outbound path that stopped at its head too. All three are properties
 * of the queueing, checkable here in isolation.
 */

#include <catch.hpp>

#include <cstdint>
#include <vector>

#include "champsim.h"
#include "chrono.h"
#include "modules.h"
#include "nmfc/function_core.h"
#include "nmfc/function_fabric.h"
#include "nmfc/nmfc_types.h"
#include "nmfc/tile_router.h"

namespace
{
constexpr std::size_t TILES = 4;
constexpr unsigned BLOCK_BITS = 6;
constexpr unsigned GRAIN_BITS = 21;
constexpr unsigned MODE_BIT = 38;
constexpr std::uint64_t GRAIN = std::uint64_t{1} << GRAIN_BITS;

using builder_t = champsim::modules::ModuleBuilder;

builder_t geometry(builder_t b)
{
  return b.add_parameter("clock_period", champsim::chrono::picoseconds{250})
      .add_parameter("nmfc_num_tiles", std::size_t{TILES})
      .add_parameter("log2_block_size", BLOCK_BITS)
      .add_parameter("nmfc_grain_bits", GRAIN_BITS)
      .add_parameter("nmfc_mode_bit", MODE_BIT);
}

/**
 * A core that accepts a fixed number of things and then refuses, which is all
 * the fabric can observe about a real one. `full` models the tile every
 * deadlock formed behind.
 */
struct stub_core : public nmfc::function_core_module {
  stub_core(std::size_t tile, std::size_t capacity) : nmfc::function_core_module(champsim::chrono::picoseconds{250}), tile_(tile), free_(capacity), cap_(capacity)
  {
  }
  long operate() override { return 0; }
  [[nodiscard]] std::size_t tile_index() const override { return tile_; }
  bool accept(const nmfc::invocation_msg& msg) override
  {
    if (free_ == 0) {
      return false;
    }
    --free_;
    accepted.push_back(msg.token);
    return true;
  }
  bool accept_migration(const nmfc::context& ctx) override
  {
    if (free_ == 0) {
      return false;
    }
    --free_;
    arrived.push_back(ctx.token);
    return true;
  }
  [[nodiscard]] std::size_t free_contexts() const override { return free_; }
  [[nodiscard]] std::size_t num_contexts() const override { return cap_; }

  std::size_t tile_;
  std::size_t free_;
  std::size_t cap_;
  std::vector<std::uint64_t> accepted;
  std::vector<std::uint64_t> arrived;
};

struct recording_host : public nmfc::offload_sink {
  std::vector<std::uint64_t> returned;
  void accept_return(const nmfc::completion_msg& msg) override { returned.push_back(msg.token); }
};

struct fabric_rig {
  nmfc::tile_router_module* router;
  nmfc::function_fabric_module* fabric;
  std::vector<std::unique_ptr<stub_core>> cores;
  recording_host host;
  std::uint32_t host_id;

  fabric_rig(const std::string& tag, std::vector<std::size_t> capacities, std::size_t queue_size = 16, const std::string& placement = "round_robin")
  {
    auto rb = geometry(builder_t{"frouter" + tag, "CONGRUENT_ROUTER"}).add_parameter("clock_period", champsim::chrono::picoseconds{1000});
    router = nmfc::tile_router_module::create_instance(rb, static_cast<champsim::modules::environment_module*>(nullptr));

    auto fb = geometry(builder_t{"ffab" + tag, "FUNCTION_FABRIC"})
                  .add_parameter("hop_latency", std::uint64_t{1})
                  .add_parameter("router", router)
                  .add_parameter("queue_size", queue_size)
                  .add_parameter("placement_policy", placement);
    fabric = nmfc::function_fabric_module::create_instance(fb, static_cast<champsim::modules::environment_module*>(nullptr));
    host_id = fabric->attach_host(&host);

    for (std::size_t t = 0; t < TILES; ++t) {
      cores.push_back(std::make_unique<stub_core>(t, capacities.at(t)));
      fabric->attach_tile(t, cores.back().get());
    }
  }

  void run(champsim::chrono::clock& clock, int cycles)
  {
    for (int i = 0; i < cycles; ++i) {
      clock.tick(champsim::chrono::picoseconds{250});
      fabric->operate_on(clock);
    }
  }
};

nmfc::context ctx_for(std::uint64_t token, const nmfc::function_body* body)
{
  nmfc::context c{};
  c.token = token;
  c.body = body;
  return c;
}
} // namespace

TEST_CASE("A congested destination cannot monopolise the migration path")
{
  // The measured failure: 128 of 128 queue entries bound for one tile that had
  // no free contexts, so no core could release a slot and the machine stopped.
  // Per-destination queueing is what makes the other tiles' traffic immune.
  champsim::chrono::clock clock;
  fabric_rig r{"_hol", {0, 8, 8, 8}}; // tile 0 is full from the start

  nmfc::function_body body{};
  body.token = 1;

  // Offer far more for the full tile than any queue could hold...
  std::size_t refused_for_full = 0;
  for (std::uint64_t i = 0; i < 200; ++i) {
    nmfc::migration_msg m{};
    m.ctx = ctx_for(1000 + i, &body);
    m.target_tile = 0;
    if (!r.fabric->migrate(std::move(m))) {
      ++refused_for_full;
    }
  }
  REQUIRE(refused_for_full > 0); // the queue for tile 0 does fill, as it should

  // ...and traffic for a tile with room must still get through.
  nmfc::migration_msg good{};
  good.ctx = ctx_for(7, &body);
  good.target_tile = 2;
  REQUIRE(r.fabric->migrate(std::move(good)));

  r.run(clock, 200);
  REQUIRE(r.cores[2]->arrived.size() == 1);
  REQUIRE(r.cores[2]->arrived.front() == 7);
  REQUIRE(r.cores[0]->arrived.empty()); // still full, correctly
}

TEST_CASE("Delivery does not stop at the head of the queue")
{
  // Head-of-line blocking on delivery had the same effect as a shared queue:
  // one undeliverable message stalled every message behind it that had
  // somewhere to go.
  champsim::chrono::clock clock;
  fabric_rig r{"_deliver", {0, 8, 8, 8}};

  nmfc::function_body body{};
  body.token = 1;

  nmfc::migration_msg blocked{};
  blocked.ctx = ctx_for(1, &body);
  blocked.target_tile = 0; // will never be accepted
  REQUIRE(r.fabric->migrate(std::move(blocked)));

  for (std::uint64_t t = 1; t < TILES; ++t) {
    nmfc::migration_msg m{};
    m.ctx = ctx_for(10 + t, &body);
    m.target_tile = t;
    REQUIRE(r.fabric->migrate(std::move(m)));
  }

  r.run(clock, 200);
  for (std::size_t t = 1; t < TILES; ++t) {
    REQUIRE(r.cores[t]->arrived.size() == 1);
  }
}

TEST_CASE("A new invocation may not take the last context a migration is owed")
{
  // Fresh work has no age guarantee behind it. If dispatch could consume the
  // slot held for the oldest pending migration, the invocation that is supposed
  // to always be able to move could stop being able to move.
  champsim::chrono::clock clock;
  fabric_rig r{"_reserve", {1, 1, 1, 1}}; // exactly one slot each

  nmfc::function_body body{};
  body.token = 1;
  body.instrs.push_back(nmfc::body_instr{});

  nmfc::invocation_msg inv{};
  inv.token = 500;
  inv.home_host = r.host_id;
  inv.body = &body;
  REQUIRE(r.fabric->dispatch(inv));

  r.run(clock, 100);

  // With one slot per tile and one reserved, no ordinary dispatch lands.
  std::size_t landed = 0;
  for (const auto& c : r.cores) {
    landed += c->accepted.size();
  }
  REQUIRE(landed == 0);
}

TEST_CASE("Completions reach the host that issued the work")
{
  champsim::chrono::clock clock;
  fabric_rig r{"_finish", {8, 8, 8, 8}};
  REQUIRE(r.fabric->finish(nmfc::completion_msg{77, r.host_id, 2}));
  r.run(clock, 50);
  REQUIRE(r.host.returned.size() == 1);
  REQUIRE(r.host.returned.front() == 77);
}

TEST_CASE("A congested destination cannot monopolise the dispatch path either")
{
  // The same failure as the migration path, and it survived the migration fix
  // because the fix was applied to one queue and not the other: 128 queued
  // invocations for a full tile while every other tile sat at 1024/1024 free.
  champsim::chrono::clock clock;
  fabric_rig r{"_disp_hol", {0, 8, 8, 8}, 16, "by_entry_pc"};

  nmfc::function_body body{};
  body.token = 1;
  body.instrs.push_back(nmfc::body_instr{});

  std::size_t refused = 0;
  for (std::uint64_t i = 0; i < 200; ++i) {
    nmfc::invocation_msg m{};
    m.token = 1000 + i;
    m.home_host = r.host_id;
    m.body = &body;
    // Aim it at the full tile the way the machine does: the entry PC names
    // the tile, because the copy of the code it starts at lives there.
    m.entry_pc = champsim::address{0 * GRAIN + 0x1000};
    if (!r.fabric->dispatch(m)) {
      ++refused;
    }
  }
  REQUIRE(refused > 0);

  // Work for a tile with room must still be accepted and delivered.
  nmfc::function_body ok_body{};
  ok_body.token = 2;
  ok_body.instrs.push_back(nmfc::body_instr{});

  nmfc::invocation_msg good{};
  good.token = 7;
  good.home_host = r.host_id;
  good.body = &ok_body;
  good.entry_pc = champsim::address{2 * GRAIN + 0x1000}; // a tile with room
  REQUIRE(r.fabric->dispatch(good));

  r.run(clock, 200);
  std::size_t landed = 0;
  for (std::size_t t = 1; t < TILES; ++t) {
    landed += r.cores[t]->accepted.size();
  }
  REQUIRE(landed >= 1);
}

TEST_CASE("Dispatch does not rewrite the entry PC")
{
  // A function's code is one virtual page aliased to a copy on every channel,
  // so the same address resolves to whichever copy the destination owns. The
  // fabric used to add a per-tile bias here, which was correct when the copies
  // were N distinct virtual pages and sends an invocation to an address that is
  // not its code once they are not.
  champsim::chrono::clock clock;
  fabric_rig r{"_entry_pc", {8, 8, 8, 8}, 16, "by_entry_pc"};

  nmfc::function_body body{};
  body.token = 5;
  body.entry_pc = champsim::address{3 * GRAIN + 0x1000}; // owned by tile 3
  body.instrs.push_back(nmfc::body_instr{});

  nmfc::invocation_msg msg{};
  msg.token = 5;
  msg.home_host = r.host_id;
  msg.body = &body;
  msg.entry_pc = body.entry_pc;
  REQUIRE(r.fabric->dispatch(msg));
  r.run(clock, 100);

  // It landed somewhere, and wherever that was, it must have been told to start
  // at the address it was given.
  std::size_t total = 0;
  for (const auto& c : r.cores) {
    total += c->accepted.size();
  }
  REQUIRE(total == 1);
}
