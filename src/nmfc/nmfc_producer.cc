/*
 * NMFC_PRODUCER — reads an NMFC trace and feeds a host core.
 *
 * It does three things the stock trace producer does not:
 *
 *   PAGE_HINT records are consumed, not emitted. They go to the page allocator
 *   as placement requests, which is how the pseudo-compiler's layout decision
 *   reaches the machine.
 *
 *   A CALL record is turned into a host load into the *offload aperture*, at an
 *   address that encodes the invocation token. The aperture is a property of
 *   the machine rather than of the compilation, so the generator leaves the
 *   address blank and it is filled in here.
 *
 *   The body following a CALL is buffered whole and published to the function
 *   image store. Because the trace guarantees a body is contiguous after its
 *   call, exactly one body is ever in hand, and the function core is never
 *   starved by trace supply -- it stalls on memory, which is the thing being
 *   measured.
 *
 * The header's geometry is a contract, not documentation: a trace placed for
 * eight tiles at page granularity, replayed on a sixteen-tile machine, would
 * silently invalidate the very result it was built to measure. Every field is
 * checked and a mismatch aborts.
 *
 * Parameters:
 *   trace_file            path to the .nmfc trace
 *   image                 @function_image
 *   vmem                  @vmem (must implement nmfc::page_placement_sink)
 *   nmfc_aperture_base    offload aperture base VA (global; default 2^46)
 */

#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include <fmt/core.h>

#include "instruction_producer.h"
#include "modules.h"
#include "nmfc/function_image.h"
#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_trace.h"
#include "nmfc/nmfc_types.h"
#include "nmfc/nmfc_vmem.h"
#include "origin.h"
#include "tracereader.h"

namespace
{

static_assert(sizeof(nmfc::trace_instr) == sizeof(input_instr), "the generator's instruction payload must match ChampSim's");

/** Process-wide instruction ids, matching what champsim::tracereader assigns. */
std::uint64_t& nmfc_instr_counter()
{
  static std::uint64_t counter = 0;
  return counter;
}

class nmfc_producer : public champsim::modules::instruction_producer
{
public:
  explicit nmfc_producer(champsim::modules::ModuleBuilder builder)
      : path_(builder.get_parameter<std::string>("trace_file")), image_(builder.get_parameter<nmfc::function_image_module*>("image")),
        map_(nmfc::tile_map_from(builder)),
        aperture_base_(builder.get_parameter<std::uint64_t>("nmfc_aperture_base", true, std::uint64_t{1} << 46)),
        aperture_bytes_(builder.get_parameter<std::uint64_t>("nmfc_aperture_bytes", true, std::uint64_t{1} << 42)),
        block_bits_(builder.get_parameter<unsigned>("log2_block_size", true, 6U)),
        page_bits_(builder.get_parameter<unsigned>("log2_page_size", true, 12U))
  {
    producer_group_ = builder.get_parameter<std::string>("producer_group", true, std::string{});

    auto* vmem = builder.get_parameter<champsim::modules::vmem_module*>("vmem", true, nullptr);
    placement_ = dynamic_cast<nmfc::page_placement_sink*>(vmem);
    if (vmem != nullptr && placement_ == nullptr) {
      fmt::print("[NMFC_PRODUCER] ERROR: the configured vmem does not accept placement hints; use NMFC_VMEM\n");
      std::exit(-1);
    }
  }

  const ooo_model_instr* peek() override
  {
    fill();
    return ready_.empty() ? nullptr : &ready_.front();
  }

  void consume() override
  {
    if (!ready_.empty()) {
      ready_.pop_front();
    }
  }

  [[nodiscard]] bool eof() const override { return exhausted_ && ready_.empty(); }

  [[nodiscard]] std::string describe() const override { return path_; }

private:
  void open_once()
  {
    if (opened_) {
      return;
    }
    opened_ = true;
    stream_.open(path_, std::ios::binary);
    if (!stream_) {
      fmt::print("[NMFC_PRODUCER] ERROR: cannot open trace {}\n", path_);
      std::exit(-1);
    }

    nmfc::header header{};
    stream_.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (stream_.gcount() != sizeof(header) || header.magic != nmfc::TRACE_MAGIC) {
      fmt::print("[NMFC_PRODUCER] ERROR: {} is not an NMFC trace\n", path_);
      std::exit(-1);
    }
    validate(header);
  }

  /** The geometry contract. A silent mismatch here would invalidate every result. */
  void validate(const nmfc::header& header) const
  {
    const auto fail = [&](const char* what, std::uint64_t in_trace, std::uint64_t configured) {
      fmt::print("[NMFC_PRODUCER] ERROR: {} is {} in {} but {} in this configuration.\n"
                 "  The placement pass targeted a different machine; results would be meaningless.\n",
                 what, in_trace, path_, configured);
      std::exit(-1);
    };

    if (header.version != nmfc::TRACE_VERSION) {
      fail("trace version", header.version, nmfc::TRACE_VERSION);
    }
    if (header.record_size != sizeof(nmfc::record)) {
      fail("record size", header.record_size, sizeof(nmfc::record));
    }
    if (header.num_tiles != map_.num_tiles()) {
      fail("tile count", header.num_tiles, map_.num_tiles());
    }
    if (header.interleave_shift != map_.grain_bits()) {
      fail("interleave shift", header.interleave_shift, map_.grain_bits());
    }
    if (header.block_size != (1u << block_bits_)) {
      fail("block size", header.block_size, 1u << block_bits_);
    }
    if (header.page_size != (1u << page_bits_)) {
      fail("page size", header.page_size, 1u << page_bits_);
    }
    if (header.num_regs > nmfc::MAX_FUNCTION_REGS) {
      fail("function regfile width", header.num_regs, nmfc::MAX_FUNCTION_REGS);
    }
  }

  [[nodiscard]] bool in_aperture(std::uint64_t addr) const { return addr >= aperture_base_ && addr < aperture_base_ + aperture_bytes_; }

  /**
   * The offload aperture is a reserved window of virtual addresses, the way an
   * MMIO range is: the host core gives them a meaning other than memory. A
   * program must therefore not map there -- and in simulation the generator is
   * the loader, so this is where that contract gets checked. Reading a program
   * address as an invocation loses the access silently, which is exactly the
   * kind of failure that produces a plausible-looking wrong number.
   */
  void check_outside_aperture(std::uint64_t addr, const char* what, std::uint64_t ip) const
  {
    if (addr != 0 && in_aperture(addr)) {
      fmt::print("[NMFC_PRODUCER] ERROR: {} at ip {:#x} references {:#x}, inside the offload aperture [{:#x}, {:#x}).\n"
                 "  The trace's address space overlaps a window the host core reserves for naming invocations,\n"
                 "  so that access would be read as an offload. Relocate the region in the generator, or move\n"
                 "  nmfc_aperture_base past the trace's addresses.\n",
                 what, ip, addr, aperture_base_, aperture_base_ + aperture_bytes_);
      std::exit(-1);
    }
  }

  /** Check every address a record carries. */
  void check_addresses(const nmfc::trace_instr& instr, const char* what) const
  {
    for (auto addr : instr.source_memory) {
      check_outside_aperture(addr, what, instr.ip);
    }
    for (auto addr : instr.destination_memory) {
      check_outside_aperture(addr, what, instr.ip);
    }
  }

  bool read_record(nmfc::record& rec)
  {
    stream_.read(reinterpret_cast<char*>(&rec), sizeof(rec));
    if (stream_.gcount() != static_cast<std::streamsize>(sizeof(rec))) {
      exhausted_ = true;
      return false;
    }
    return true;
  }

  /**
   * Keep at least two host instructions in hand, so branch targets can be
   * filled from each instruction's successor the way a trace reader does.
   */
  void fill()
  {
    open_once();
    while (!exhausted_ && ready_.size() < 2) {
      nmfc::record rec{};
      if (!read_record(rec)) {
        break;
      }
      handle(rec);
    }
    if (ready_.size() >= 2) {
      champsim::set_branch_targets(std::begin(ready_), std::end(ready_));
    }
  }

  void handle(const nmfc::record& rec)
  {
    switch (static_cast<nmfc::op>(rec.kind)) {
    case nmfc::op::PAGE_HINT:
      apply_hint(rec);
      return;
    case nmfc::op::HOST:
      check_addresses(rec.instr, "host instruction");
      ready_.push_back(inflate(rec.instr));
      return;
    case nmfc::op::CALL:
      handle_call(rec);
      return;
    case nmfc::op::JOIN: {
      // The join half of fork/join. It names the same aperture slot the call
      // did, so the tracking unit recognises it as a second reference to a
      // token already in flight without any extra encoding.
      auto instr = rec.instr;
      instr.source_memory[0] = aperture_base_ + (rec.token << block_bits_);
      ready_.push_back(inflate(instr));
      ++joins_;
      return;
    }
    case nmfc::op::BODY:
    case nmfc::op::RET:
    case nmfc::op::ATOMIC:
      fmt::print("[NMFC_PRODUCER] ERROR: body record for token {} outside a call in {}\n", rec.token, path_);
      std::exit(-1);
    }
  }

  void apply_hint(const nmfc::record& rec)
  {
    if (placement_ == nullptr) {
      return; // no allocator wants hints; the layout is whatever the vmem does
    }
    const auto fields = nmfc::decode_page_hint(rec.aux1);
    // CODE is NMFC mode plus replication: one virtual page, one physical copy
    // per channel, with the tile chosen when the address is translated.
    const bool replicated = fields.reg == nmfc::region::CODE;
    const auto mode = (fields.reg == nmfc::region::STANDARD) ? nmfc::mapping_mode::STANDARD : nmfc::mapping_mode::NMFC;
    placement_->hint_placement(fields.asid, rec.aux0, nmfc::placement_hint{mode, fields.tile, replicated});
    ++hints_;
  }

  /** Buffer the body, publish it, and emit the call as an aperture load. */
  void handle_call(const nmfc::record& call)
  {
    nmfc::function_body body{};
    body.token = call.token;
    body.func_id = nmfc::call_func_id(call.aux1);
    body.entry_pc = champsim::address{call.aux0};
    body.flag_bits = call.flag_bits;

    const auto expected = nmfc::call_body_length(call.aux1);
    body.instrs.reserve(expected);

    for (;;) {
      nmfc::record rec{};
      if (!read_record(rec)) {
        fmt::print("[NMFC_PRODUCER] ERROR: trace ended inside the body of token {}\n", call.token);
        std::exit(-1);
      }
      const auto kind = static_cast<nmfc::op>(rec.kind);
      if (kind == nmfc::op::RET) {
        body.live_regs = static_cast<std::uint8_t>(rec.aux0);
        break;
      }
      if (kind != nmfc::op::BODY && kind != nmfc::op::ATOMIC && kind != nmfc::op::SPAWN) {
        fmt::print("[NMFC_PRODUCER] ERROR: record kind {} interrupts the body of token {}; bodies must be contiguous\n", rec.kind, call.token);
        std::exit(-1);
      }
      check_addresses(rec.instr, "function body instruction");
      body.instrs.push_back(to_body_instr(rec));
    }

    if (body.instrs.size() != expected) {
      fmt::print("[NMFC_PRODUCER] ERROR: token {} declared a body of {} records but {} arrived\n", call.token, expected, body.instrs.size());
      std::exit(-1);
    }

    image_->publish(std::move(body));

    if ((call.token << block_bits_) >= aperture_bytes_) {
      fmt::print("[NMFC_PRODUCER] ERROR: token {} does not fit the {} byte offload aperture.\n"
                 "  Raise nmfc_aperture_bytes; the window must be able to name every token the trace uses.\n",
                 call.token, aperture_bytes_);
      std::exit(-1);
    }

    // A spawned body is defined here and started elsewhere, by a SPAWN inside
    // another function. The host never issues it, so it contributes no host
    // instruction -- counting one would credit the compute tile with work that
    // by construction never reached it.
    if ((call.flag_bits & nmfc::FLAG_SPAWNED) != 0) {
      ++spawned_definitions_;
      return;
    }

    // The call becomes a load from the aperture slot that names this token, so
    // the host core's tracking unit recognises it with no change to the
    // instruction type everything else already speaks.
    auto instr = call.instr;
    instr.source_memory[0] = aperture_base_ + (call.token << block_bits_);
    ready_.push_back(inflate(instr));
    ++calls_;
  }

  [[nodiscard]] nmfc::body_instr to_body_instr(const nmfc::record& rec) const
  {
    nmfc::body_instr out{};
    out.ip = champsim::address{rec.instr.ip};
    out.cls = static_cast<nmfc::op_class>(rec.op_class);
    out.flag_bits = rec.flag_bits;
    out.is_atomic = (static_cast<nmfc::op>(rec.kind) == nmfc::op::ATOMIC);
    out.is_spawn = (static_cast<nmfc::op>(rec.kind) == nmfc::op::SPAWN);
    out.spawn_token = out.is_spawn ? rec.aux0 : 0;

    for (std::size_t i = 0; i < nmfc::NUM_SOURCES; ++i) {
      if (rec.instr.source_memory[i] != 0) {
        out.mem[out.num_loads++] = champsim::address{rec.instr.source_memory[i]};
      }
      out.src_reg[i] = rec.instr.source_registers[i];
    }
    for (std::size_t i = 0; i < nmfc::NUM_DESTINATIONS; ++i) {
      if (rec.instr.destination_memory[i] != 0) {
        out.mem[out.num_loads + out.num_stores] = champsim::address{rec.instr.destination_memory[i]};
        ++out.num_stores;
      }
      out.dst_reg[i] = rec.instr.destination_registers[i];
    }
    return out;
  }

  [[nodiscard]] ooo_model_instr inflate(const nmfc::trace_instr& payload) const
  {
    input_instr native{};
    std::memcpy(&native, &payload, sizeof(native));
    const auto consumer_id = static_cast<champsim::origin::id_type>(consumer_ != nullptr ? consumer_->consumer_id() : 0);
    ooo_model_instr instr{champsim::origin{consumer_id, producer_id()}, native};
    instr.instr_id = nmfc_instr_counter()++;
    return instr;
  }

  std::string path_;
  nmfc::function_image_module* image_;
  nmfc::page_placement_sink* placement_ = nullptr;
  nmfc::tile_map map_;
  std::uint64_t aperture_base_;
  std::uint64_t aperture_bytes_;
  unsigned block_bits_;
  unsigned page_bits_;

  std::ifstream stream_;
  bool opened_ = false;
  bool exhausted_ = false;
  std::deque<ooo_model_instr> ready_;

  std::uint64_t calls_ = 0;
  std::uint64_t hints_ = 0;
  std::uint64_t joins_ = 0;
  std::uint64_t spawned_definitions_ = 0;
};

static champsim::modules::instruction_producer::register_module<nmfc_producer> nmfc_producer_reg("NMFC_PRODUCER");

} // anonymous namespace
