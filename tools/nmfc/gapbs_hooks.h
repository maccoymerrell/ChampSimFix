/*
 * Pseudo-compiler hooks for the GAP Benchmark Suite.
 *
 * Rather than synthesising a graph workload, this instruments the real thing:
 * a GAPBS kernel is compiled against this header, run on a real graph, and
 * emits an NMFC trace of what it actually did. Addresses are the process's own,
 * so the memory behaviour is the benchmark's rather than a model of it.
 *
 * WHAT "PSEUDO-COMPILATION" MEANS HERE. Two decisions a real compiler would
 * make, made explicitly:
 *
 *   SLICING -- what one offloaded function is. For BFS that is one vertex's
 *   neighbour scan; for PageRank one vertex's gather. The kernel says so by
 *   bracketing the region with begin_call/end_call, so changing the slicing is
 *   a change to where those brackets go, which is the point: it is a knob.
 *
 *   PLACEMENT -- which tile each page lives on. declare_region records an
 *   array and an ownership rule, and the trace carries PAGE_HINT records the
 *   simulator's allocator honours. Because a virtual address already names a
 *   tile under page-granularity interleaving, placement really is just address
 *   assignment, and this is where it is decided.
 *
 * Single-threaded by construction: build the kernel with OMP_NUM_THREADS=1 (or
 * without OpenMP). A trace has to be a total order, and interleaving two
 * threads' bodies would break the contiguity the reader relies on.
 *
 * Usage in a kernel:
 *
 *     nmfc::gapbs::tracer::instance().open("bfs.nmfc", tiles, grain_bits);
 *     nmfc::gapbs::tracer::instance().declare_region("out_index", base, bytes, owner_fn);
 *     ...
 *     auto token = T.begin_call(entry_pc);
 *     T.body_load(&g.out_index_[u], R_ROW_BEGIN);
 *     ...
 *     T.end_call(token, live_regs);
 */

#ifndef NMFC_GAPBS_HOOKS_H
#define NMFC_GAPBS_HOOKS_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "nmfc_trace.h"

namespace nmfc::gapbs
{

/** Registers the function's own tiny register namespace, mirroring the model. */
enum : unsigned char {
  R_NONE = 0,
  R_VERTEX = 1,
  R_ROW_BEGIN = 2,
  R_ROW_END = 3,
  R_NEIGHBOUR = 4,
  R_VALUE = 5,
  R_ACC = 6,
  R_INDEX = 7,
};

/** Where a kernel's code is presumed to live: N copies on N consecutive grains. */
inline constexpr std::uint64_t FUNC_CODE_BASE = 0x0000'1000'0000ULL;
inline constexpr std::uint64_t HOST_CODE_BASE = 0x0000'0040'0000ULL;

class tracer
{
public:
  static tracer& instance()
  {
    static tracer singleton;
    return singleton;
  }

  void open(const std::string& path, std::uint32_t tiles, unsigned grain_bits, unsigned page_bits = 12, unsigned block_bits = 6)
  {
    path_ = path;
    tiles_ = tiles;
    grain_bits_ = grain_bits;
    page_bits_ = page_bits;
    block_bits_ = block_bits;
    out_.open(path, std::ios::binary);
    if (!out_) {
      std::fprintf(stderr, "nmfc: cannot open %s\n", path.c_str());
      std::exit(1);
    }
    nmfc::header blank{};
    out_.write(reinterpret_cast<const char*>(&blank), sizeof(blank));
    enabled_ = true;
  }

  /**
   * Also emit a matched baseline: the same instructions, inline on the host,
   * with no call or return. Written from the same record stream, which is what
   * makes "the same work" a fact rather than a claim.
   */
  void open_baseline(const std::string& path)
  {
    baseline_.open(path, std::ios::binary);
    if (!baseline_) {
      std::fprintf(stderr, "nmfc: cannot open %s\n", path.c_str());
      std::exit(1);
    }
  }

  /**
   * Stop emitting after this many invocations.
   *
   * A real kernel on a real graph runs far past what is worth simulating, and
   * it has to keep running correctly afterwards or the traversal it traces is
   * not the one the benchmark performs. So the budget stops the *recording*,
   * not the kernel.
   */
  void set_budget(std::uint64_t invocations) { budget_ = invocations; }

  [[nodiscard]] bool enabled() const { return enabled_ && !budget_spent_; }
  [[nodiscard]] std::uint64_t baseline_instructions() const { return baseline_count_; }

  /**
   * Declare an array and how its pages are owned.
   *
   * `owner` maps a byte offset within the region to a tile. Passing nullptr
   * means "leave it to natural interleaving", which is the striped baseline
   * that any contiguous allocation already gets.
   */
  void declare_region(const std::string& name, const void* base, std::uint64_t bytes, const std::function<std::uint32_t(std::uint64_t)>& owner)
  {
    if (!enabled_ || budget_spent_) {
      return;
    }
    // Rebase into a layout we control. The kernel's *access pattern* is the
    // thing worth capturing; its addresses are wherever malloc and mmap happened
    // to land, which is precisely what the placement pass exists to decide.
    //
    // Placement is expressed by *choosing the address*, not by hinting against
    // it. A virtual address already names its tile, in the tile-select field the
    // function core reads to decide local-vs-migrate before it ever translates.
    // A hint that contradicted that field would put the data on one tile and
    // send the invocation to another -- so a grain destined for tile t is given
    // a virtual grain whose select field *is* t, and the hint then merely agrees.
    const auto real = reinterpret_cast<std::uint64_t>(base);
    const std::uint64_t grain = std::uint64_t{1} << grain_bits_;
    const auto num_grains = (bytes + grain - 1) / grain;
    const auto start = next_region_base(bytes);

    // Grains are handed out per tile, so a skewed partition simply leaves gaps
    // in the other tiles' arenas rather than landing on the wrong one.
    std::vector<std::uint64_t> grain_va(num_grains);
    std::vector<std::uint64_t> next_slot(tiles_, 0);
    std::uint64_t hinted = 0;

    for (std::uint64_t k = 0; k < num_grains; ++k) {
      const auto tile = owner ? (owner(k * grain) % tiles_) : static_cast<std::uint32_t>(k % tiles_);
      const auto slot = next_slot[tile]++;
      const auto vaddr = start + (slot * tiles_ + tile) * grain;
      grain_va[k] = vaddr;

      // The invariant this whole layout exists to hold. If it ever fails, the
      // simulator would catch it later as a locality assertion inside a tile
      // port, a long way from the cause.
      if (natural_tile(vaddr) != tile) {
        std::fprintf(stderr, "nmfc: FATAL: region %s grain %lu wants tile %u but address %#lx names tile %u\n", name.c_str(), k, tile, vaddr,
                     natural_tile(vaddr));
        std::exit(1);
      }

      // One hint per grain, not per page. The allocator keys placement by
      // grain, so the other 511 pages of a 2 MiB grain rewrite the same entry
      // -- at kron-24 that was half a million redundant records in the trace.
      emit_hint(vaddr >> page_bits_, tile);
      ++hinted;
    }

    regions_.push_back(region_map{real, real + bytes, start, std::move(grain_va)});
    std::fprintf(stderr, "nmfc: region %-14s base %#018lx  %8.1f MiB  %lu page hints\n", name.c_str(), start, double(bytes) / (1024 * 1024), hinted);
  }

  /**
   * Translate a real address into the simulated layout.
   *
   * Anything outside a declared region passes through: stack and scratch are
   * not what the placement pass is about, and leaving them alone keeps their
   * behaviour honest.
   */
  [[nodiscard]] std::uint64_t rebase(const void* address) const
  {
    const auto raw = reinterpret_cast<std::uint64_t>(address);
    for (const auto& region : regions_) {
      if (raw >= region.real_begin && raw < region.real_end) {
        const auto delta = raw - region.real_begin;
        return region.grain_va[delta >> grain_bits_] + (delta & ((std::uint64_t{1} << grain_bits_) - 1));
      }
    }
    return raw;
  }

  /** The tile a virtual address already names, with no placement applied. */
  [[nodiscard]] std::uint32_t natural_tile(std::uint64_t vaddr) const { return static_cast<std::uint32_t>((vaddr >> grain_bits_) % tiles_); }

  // ---- host stream ----

  /** One ordinary host instruction: loop overhead, frontier bookkeeping. */
  void host(const void* load_address = nullptr, unsigned char src = R_NONE, unsigned char dst = R_NONE, bool branch = false, bool taken = false)
  {
    if (!enabled_ || budget_spent_) {
      return;
    }
    auto rec = blank(nmfc::op::HOST, 0);
    rec.instr.ip = next_host_pc();
    rec.instr.is_branch = branch ? 1 : 0;
    rec.instr.branch_taken = taken ? 1 : 0;
    if (load_address != nullptr) {
      rec.instr.source_memory[0] = rebase(load_address);
    }
    rec.instr.source_registers[0] = src;
    rec.instr.destination_registers[0] = dst;
    write(rec);
    write_baseline(rec);
  }

  // ---- one offloaded function ----

  /**
   * Begin an invocation. Everything emitted until end_call is its body, which
   * is what makes the slicing decision explicit and movable.
   */
  std::uint64_t begin_call(std::uint32_t func_id = 1, bool no_return = false, bool deferred_join = false)
  {
    if (!enabled_ || budget_spent_) {
      return 0;
    }
    if (budget_ != 0 && calls_ >= budget_) {
      // Stop both streams here, so the NMFC trace and its baseline cover
      // exactly the same work. The kernel carries on correctly, untraced.
      if (!budget_spent_) {
        budget_spent_ = true;
        std::fprintf(stderr, "nmfc: budget of %lu invocations reached; recording stops here\n", budget_);
      }
      return 0;
    }
    const auto token = ++token_counter_;
    body_.clear();
    call_ = blank(nmfc::op::CALL, token);
    call_.instr.ip = next_host_pc();
    call_.instr.source_registers[0] = R_VERTEX;
    call_.instr.destination_registers[0] = R_ACC;
    call_.aux0 = code_base();
    call_.func_id_ = func_id;
    if (no_return) {
      call_.flag_bits |= nmfc::FLAG_NO_RETURN;
    }
    if (deferred_join) {
      call_.flag_bits |= nmfc::FLAG_DEFERRED_JOIN;
    }
    in_call_ = true;
    body_pc_ = FUNC_CODE_BASE;
    return token;
  }

  void body_load(const void* address, unsigned char dst, unsigned char src = R_NONE, bool loop_back = false)
  {
    body_memory(address, dst, src, /*store=*/false, /*atomic=*/false, loop_back);
  }

  void body_store(const void* address, unsigned char src, bool loop_back = false) { body_memory(address, R_NONE, src, /*store=*/true, /*atomic=*/false, loop_back); }

  /**
   * A read-modify-write the function core serialises against other atomics to
   * the same block. BFS's parent[] compare-and-swap is exactly this, and it is
   * the case the design's atomicity claim is about.
   */
  void body_atomic(const void* address, unsigned char dst, unsigned char src, bool loop_back = false)
  {
    body_memory(address, dst, src, /*store=*/false, /*atomic=*/true, loop_back);
  }

  void body_alu(unsigned char dst, unsigned char src_a, unsigned char src_b = R_NONE, nmfc::op_class cls = nmfc::op_class::ALU, bool loop_back = false)
  {
    if (!in_call_) {
      return;
    }
    auto rec = blank(nmfc::op::BODY, call_.token);
    rec.instr.ip = next_body_pc(loop_back);
    rec.op_class = static_cast<std::uint8_t>(cls);
    rec.instr.source_registers[0] = src_a;
    rec.instr.source_registers[1] = src_b;
    rec.instr.destination_registers[0] = dst;
    if (loop_back) {
      rec.flag_bits |= nmfc::FLAG_TAKEN_TARGET;
    }
    body_.push_back(rec);
  }

  /** Close the invocation and write the call, its body, and its return. */
  void end_call(std::uint64_t token, std::uint8_t live_regs = R_ACC)
  {
    if (!enabled_ || !in_call_ || token == 0 || token != call_.token) {
      return;
    }
    call_.aux1 = nmfc::encode_call_aux1(call_.func_id_, static_cast<std::uint32_t>(body_.size()));
    write(call_);
    for (const auto& rec : body_) {
      write(rec);
      // The baseline runs the body inline, so it gets the instructions but
      // neither the call nor the return -- those are the offload, not the work.
      write_baseline(rec);
    }
    auto ret = blank(nmfc::op::RET, token);
    ret.instr.ip = next_host_pc();
    ret.aux0 = live_regs;
    write(ret);

    in_call_ = false;
    ++calls_;
  }

  /**
   * Wait for a forked invocation.
   *
   * Emitted after however many calls the window holds, which is the point: the
   * host forks a batch and only then blocks, so in-flight invocations are
   * bounded by the tracking unit rather than by how far the reorder buffer can
   * see past a blocking call.
   */
  void emit_join(std::uint64_t token)
  {
    if (!enabled_ || token == 0) {
      return;
    }
    auto rec = blank(nmfc::op::JOIN, token);
    rec.instr.ip = next_host_pc();
    rec.instr.destination_registers[0] = R_ACC;
    write(rec);
    ++joins_;
  }

  void close()
  {
    if (!enabled_) {
      return;
    }
    nmfc::header header{};
    header.magic = nmfc::TRACE_MAGIC;
    header.version = nmfc::TRACE_VERSION;
    header.record_size = sizeof(nmfc::record);
    header.num_regs = static_cast<std::uint32_t>(nmfc::MAX_FUNCTION_REGS);
    header.num_tiles = tiles_;
    header.page_size = 1U << page_bits_;
    header.block_size = 1U << block_bits_;
    header.interleave_shift = grain_bits_;
    header.num_asids = 1;
    header.num_records = records_;
    header.num_calls = calls_;

    out_.seekp(0);
    out_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out_.close();
    if (baseline_.is_open()) {
      baseline_.close();
      std::fprintf(stderr, "nmfc: wrote baseline -- %lu instructions\n", baseline_count_);
    }
    enabled_ = false;
    std::fprintf(stderr, "nmfc: wrote %s -- %lu records, %lu invocations, %lu joins\n", path_.c_str(), records_, calls_, joins_);
  }

private:
  /** A record plus the one field the on-disk format packs elsewhere. */
  struct record_ex : nmfc::record {
    std::uint32_t func_id_ = 1;
  };

  static record_ex blank(nmfc::op kind, std::uint64_t token)
  {
    record_ex rec{};
    std::memset(static_cast<nmfc::record*>(&rec), 0, sizeof(nmfc::record));
    rec.kind = static_cast<std::uint8_t>(kind);
    rec.op_class = static_cast<std::uint8_t>(nmfc::op_class::ALU);
    rec.token = token;
    return rec;
  }

  void body_memory(const void* address, unsigned char dst, unsigned char src, bool store, bool atomic, bool loop_back)
  {
    if (!in_call_) {
      return;
    }
    auto rec = blank(atomic ? nmfc::op::ATOMIC : nmfc::op::BODY, call_.token);
    rec.instr.ip = next_body_pc(loop_back);
    rec.op_class = static_cast<std::uint8_t>(store ? nmfc::op_class::STORE : nmfc::op_class::LOAD);
    if (store) {
      rec.instr.destination_memory[0] = rebase(address);
    } else {
      rec.instr.source_memory[0] = rebase(address);
    }
    rec.instr.source_registers[0] = src;
    rec.instr.destination_registers[0] = dst;
    if (loop_back) {
      rec.flag_bits |= nmfc::FLAG_TAKEN_TARGET;
    }
    body_.push_back(rec);
  }

  void emit_hint(std::uint64_t vpage, std::uint32_t tile)
  {
    auto rec = blank(nmfc::op::PAGE_HINT, 0);
    rec.aux0 = vpage;
    rec.aux1 = nmfc::encode_page_hint(0, nmfc::region::NMFC, tile);
    write(rec);
  }

  void write(const nmfc::record& rec)
  {
    out_.write(reinterpret_cast<const char*>(&rec), sizeof(nmfc::record));
    ++records_;
  }

  void write_baseline(const nmfc::record& rec)
  {
    if (!baseline_.is_open()) {
      return;
    }
    baseline_.write(reinterpret_cast<const char*>(&rec.instr), sizeof(rec.instr));
    ++baseline_count_;
  }

  std::uint64_t next_host_pc()
  {
    const auto here = host_pc_;
    host_pc_ += 4;
    if (host_pc_ > HOST_CODE_BASE + 0x1000) {
      host_pc_ = HOST_CODE_BASE;
    }
    return here;
  }

  std::uint64_t next_body_pc(bool loop_back)
  {
    if (loop_back) {
      body_pc_ = FUNC_CODE_BASE + 8; // back to the loop head
    }
    const auto here = body_pc_;
    body_pc_ += 4;
    return rebase(reinterpret_cast<const void*>(here));
  }

  /** Where the code region actually landed, for the call's entry PC. */
  [[nodiscard]] std::uint64_t code_base() const { return rebase(reinterpret_cast<const void*>(FUNC_CODE_BASE)); }

  /** One declared array, and where it lives in the simulated layout. */
  struct region_map {
    std::uint64_t real_begin;
    std::uint64_t real_end;
    std::uint64_t sim_base;
    /** Where each source grain landed. Placement is expressed here, not in the hint. */
    std::vector<std::uint64_t> grain_va;
  };

  /** Hand out grain-aligned simulated bases, one region after another. */
  std::uint64_t next_region_base(std::uint64_t bytes)
  {
    const std::uint64_t grain = std::uint64_t{1} << grain_bits_;
    const auto base = region_cursor_;
    // Round up to a whole number of grains, and leave a grain of separation so
    // two regions never share one.
    region_cursor_ += ((bytes + grain - 1) / grain + 1) * grain * tiles_;
    return base;
  }

  std::vector<region_map> regions_;
  std::uint64_t region_cursor_ = 0x0000'2000'0000ULL;

  std::ofstream out_;
  std::ofstream baseline_;
  std::uint64_t baseline_count_ = 0;
  std::string path_;
  bool enabled_ = false;
  bool budget_spent_ = false;
  bool in_call_ = false;

  std::uint32_t tiles_ = 4;
  unsigned grain_bits_ = 21;
  unsigned page_bits_ = 12;
  unsigned block_bits_ = 6;

  record_ex call_{};
  std::vector<record_ex> body_;
  std::uint64_t body_pc_ = FUNC_CODE_BASE;
  std::uint64_t host_pc_ = HOST_CODE_BASE;

  std::uint64_t token_counter_ = 0;
  std::uint64_t records_ = 0;
  std::uint64_t calls_ = 0;
  std::uint64_t joins_ = 0;
  std::uint64_t budget_ = 0;
};

} // namespace nmfc::gapbs

#endif // NMFC_GAPBS_HOOKS_H
