/*
 * The trace reader is the boundary between what a pseudo-compiler decided and
 * what the machine does, and it enforces several contracts that are silent when
 * they hold and catastrophic when they do not: the geometry a placement pass
 * assumed, the reserved window the host core reads as an offload, and which
 * calls the host actually issues.
 *
 * All of those have already failed in this branch. A spawned definition counted
 * as a host instruction credits the compute tile with work that never reached
 * it; a call the host issues but never waits for ends a measurement with its
 * work in flight.
 */

#include <catch.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "champsim.h"
#include "instruction_producer.h"
#include "modules.h"
#include "nmfc/function_image.h"
#include "nmfc/nmfc_trace.h"
#include "nmfc/nmfc_vmem.h"

namespace
{
constexpr std::size_t TILES = 4;
constexpr unsigned BLOCK_BITS = 6;
constexpr unsigned GRAIN_BITS = 21;
constexpr unsigned MODE_BIT = 38;
constexpr std::uint64_t APERTURE = std::uint64_t{1} << 46;

using builder_t = champsim::modules::ModuleBuilder;

nmfc::record blank_rec(nmfc::op kind, std::uint64_t token)
{
  nmfc::record r{};
  r.kind = static_cast<std::uint8_t>(kind);
  r.token = token;
  return r;
}

/** Write a trace whose header matches the geometry below unless told otherwise. */
std::string write_trace(const std::string& name, const std::vector<nmfc::record>& records, std::uint32_t tiles = TILES,
                        std::uint32_t version = nmfc::TRACE_VERSION)
{
  const auto path = std::string{"/tmp/nmfc_test_"} + name + ".nmfc";
  std::ofstream out(path, std::ios::binary);
  nmfc::header h{};
  h.magic = nmfc::TRACE_MAGIC;
  h.version = version;
  h.record_size = sizeof(nmfc::record);
  h.num_regs = static_cast<std::uint32_t>(nmfc::MAX_FUNCTION_REGS);
  h.num_tiles = tiles;
  h.page_size = 4096;
  h.block_size = 1U << BLOCK_BITS;
  h.interleave_shift = GRAIN_BITS;
  h.num_asids = 1;
  h.num_records = records.size();
  out.write(reinterpret_cast<const char*>(&h), sizeof(h));
  for (const auto& r : records) {
    out.write(reinterpret_cast<const char*>(&r), sizeof(r));
  }
  return path;
}

struct producer_rig {
  nmfc::function_image_module* image;
  champsim::modules::instruction_producer* producer;

  producer_rig(const std::string& path, const std::string& tag)
  {
    auto ib = builder_t{"pimage" + tag, "FUNCTION_IMAGE_STORE"};
    image = nmfc::function_image_module::create_instance(ib, static_cast<champsim::modules::environment_module*>(nullptr));

    auto pb = builder_t{"prod" + tag, "NMFC_PRODUCER"}
                  .add_parameter("trace_file", path)
                  .add_parameter("image", image)
                  .add_parameter("nmfc_num_tiles", std::size_t{TILES})
                  .add_parameter("log2_block_size", BLOCK_BITS)
                  .add_parameter("nmfc_grain_bits", GRAIN_BITS)
                  .add_parameter("nmfc_mode_bit", MODE_BIT)
                  .add_parameter("log2_page_size", 12U)
                  .add_parameter("nmfc_aperture_base", APERTURE)
                  .add_parameter("nmfc_aperture_bytes", std::uint64_t{1} << 42);
    producer = champsim::modules::instruction_producer::create_instance(pb, static_cast<champsim::modules::packet_consumer*>(nullptr));
  }
};
} // namespace

TEST_CASE("A call becomes a load from the aperture slot that names its token")
{
  // This is how an offload reaches the host core without a new instruction
  // type: the tracking unit recognises the address, and the address encodes
  // which invocation it is.
  std::vector<nmfc::record> recs;
  auto call = blank_rec(nmfc::op::CALL, 5);
  call.aux1 = nmfc::encode_call_aux1(1, 1);
  recs.push_back(call);
  recs.push_back(blank_rec(nmfc::op::BODY, 5));
  recs.push_back(blank_rec(nmfc::op::RET, 5));
  recs.push_back(blank_rec(nmfc::op::HOST, 0));
  recs.push_back(blank_rec(nmfc::op::HOST, 0));

  producer_rig r{write_trace("call", recs), "_call"};

  const auto* first = r.producer->peek();
  REQUIRE(first != nullptr);
  REQUIRE(first->source_memory.size() == 1);
  REQUIRE(first->source_memory.front().to<std::uint64_t>() == APERTURE + (5ULL << BLOCK_BITS));

  // And the body it named is published, or the core would have nothing to run.
  REQUIRE(r.image->lookup(5) != nullptr);
}

TEST_CASE("A spawned definition publishes a body but issues no host instruction")
{
  // The host never starts this work, so charging it a host instruction would
  // credit the compute tile with work that by construction never reached it --
  // and inflate the baseline it is being compared against.
  std::vector<nmfc::record> recs;
  auto def = blank_rec(nmfc::op::CALL, 9);
  def.aux1 = nmfc::encode_call_aux1(2, 1);
  def.flag_bits = nmfc::FLAG_SPAWNED;
  recs.push_back(def);
  recs.push_back(blank_rec(nmfc::op::BODY, 9));
  recs.push_back(blank_rec(nmfc::op::RET, 9));

  auto host = blank_rec(nmfc::op::HOST, 0);
  host.instr.ip = 0x400;
  recs.push_back(host);
  recs.push_back(blank_rec(nmfc::op::HOST, 0));

  producer_rig r{write_trace("spawned", recs), "_spawned"};

  const auto* first = r.producer->peek();
  REQUIRE(first != nullptr);
  REQUIRE(first->ip.to<std::uint64_t>() == 0x400); // the host instruction, not the definition
  REQUIRE(r.image->lookup(9) != nullptr);          // but the body is available to spawn
}

TEST_CASE("A spawn record survives into the body it belongs to")
{
  std::vector<nmfc::record> recs;
  auto call = blank_rec(nmfc::op::CALL, 11);
  call.aux1 = nmfc::encode_call_aux1(1, 1);
  recs.push_back(call);
  auto spawn = blank_rec(nmfc::op::SPAWN, 11);
  spawn.aux0 = 77; // the invocation it starts
  recs.push_back(spawn);
  recs.push_back(blank_rec(nmfc::op::RET, 11));
  recs.push_back(blank_rec(nmfc::op::HOST, 0));
  recs.push_back(blank_rec(nmfc::op::HOST, 0));

  producer_rig r{write_trace("spawnrec", recs), "_spawnrec"};
  (void)r.producer->peek();

  const auto* body = r.image->lookup(11);
  REQUIRE(body != nullptr);
  REQUIRE(body->instrs.size() == 1);
  REQUIRE(body->instrs.front().is_spawn);
  REQUIRE(body->instrs.front().spawn_token == 77);
}

TEST_CASE("Host instructions pass through in order")
{
  std::vector<nmfc::record> recs;
  for (std::uint64_t i = 0; i < 4; ++i) {
    auto h = blank_rec(nmfc::op::HOST, 0);
    h.instr.ip = 0x1000 + i * 4;
    recs.push_back(h);
  }
  producer_rig r{write_trace("hosts", recs), "_hosts"};

  for (std::uint64_t i = 0; i < 3; ++i) {
    const auto* instr = r.producer->peek();
    REQUIRE(instr != nullptr);
    REQUIRE(instr->ip.to<std::uint64_t>() == 0x1000 + i * 4);
    r.producer->consume();
  }
}
