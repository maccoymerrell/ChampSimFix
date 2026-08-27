/*
 * NMFC runtime types: what a function body is once it is off the disk, what a
 * hardware context holds, and the three message kinds the function fabric
 * carries.
 *
 * These are the types the models agree on, so they live in one header rather
 * than being redeclared per module. Everything here is allocation-free per
 * instruction; the one owning container is function_body::instrs, which is
 * filled once when the trace reader publishes a body and read many times as
 * the invocation executes.
 */

#ifndef NMFC_TYPES_H
#define NMFC_TYPES_H

#include <array>
#include <cstdint>
#include <vector>

#include "address.h"
#include "chrono.h"
#include "nmfc/nmfc_trace.h"
#include "origin.h"

namespace nmfc
{

/** Memory operations one body instruction may carry. Matches the trace record. */
inline constexpr std::size_t MAX_MEM_OPS = NUM_DESTINATIONS + NUM_SOURCES;
inline constexpr std::size_t MAX_SRC_REGS = NUM_SOURCES;
inline constexpr std::size_t MAX_DST_REGS = NUM_DESTINATIONS;

/**
 * One dynamic instruction of a function body.
 *
 * Register ids follow ChampSim's convention: 0 means "no register", so valid
 * ids for a function are 1..num_regs. The reader validates that against the
 * trace header's regfile width, since an out-of-range id would silently corrupt
 * the scoreboard rather than fail.
 */
struct body_instr {
  champsim::address ip{};

  // Loads occupy [0, num_loads); stores occupy [num_loads, num_loads + num_stores).
  std::array<champsim::address, MAX_MEM_OPS> mem{};
  std::uint8_t num_loads = 0;
  std::uint8_t num_stores = 0;

  std::array<std::uint8_t, MAX_SRC_REGS> src_reg{};
  std::array<std::uint8_t, MAX_DST_REGS> dst_reg{};

  op_class cls = op_class::ALU;
  std::uint8_t flag_bits = 0;
  bool is_atomic = false;

  [[nodiscard]] std::size_t num_mem_ops() const { return static_cast<std::size_t>(num_loads) + num_stores; }
  [[nodiscard]] bool taken_target() const { return (flag_bits & FLAG_TAKEN_TARGET) != 0; }
};

/**
 * A whole invocation's dynamic body, as the pseudo-compiler emitted it.
 *
 * Owned by the function_image store and referenced (never copied) by the
 * context executing it, so a migration moves a pointer rather than the code —
 * which is the point of backing the function on every channel.
 */
struct function_body {
  std::uint64_t token = 0;
  std::uint32_t func_id = 0;
  champsim::address entry_pc_base{}; // the dispatcher adds tile * grain to pick a copy
  std::uint8_t live_regs = 0;        // words to carry home on return
  std::uint8_t flag_bits = 0;        // FLAG_NO_RETURN et al, from the CALL record
  std::vector<body_instr> instrs;

  [[nodiscard]] bool no_return() const { return (flag_bits & FLAG_NO_RETURN) != 0; }
};

/** What a context is doing right now. */
enum class ctx_state : std::uint8_t {
  FREE,      // slot unoccupied
  READY,     // wants an issue slot
  BLOCKED,   // asleep on a memory request or a translation
  MIGRATING, // handed to the fabric, awaiting delivery elsewhere
  DONE,      // body finished, return pending
};

/**
 * One translation entry carried in a context.
 *
 * These are tile-local by construction: under congruent allocation a mapping is
 * only usable on the tile that owns the virtual address. They are therefore
 * cleared on migration rather than carried — see DESIGN.md §7.1. The cost that
 * replaces them is counted as translation cold-start cycles.
 */
struct ctx_translation {
  std::uint64_t vpage = 0;
  std::uint64_t ppage = 0;
  /** log2 of the page size this resolved at: the two sizes coexist. */
  unsigned shift = 12;
  bool valid = false;

  [[nodiscard]] bool covers(std::uint64_t vaddr) const { return valid && (vaddr >> shift) == vpage; }
  [[nodiscard]] std::uint64_t translate(std::uint64_t vaddr) const { return (ppage << shift) | (vaddr & ((std::uint64_t{1} << shift) - 1)); }
};

/** Per-context translation entries: the code page plus a few MRU data pages. */
inline constexpr std::size_t MAX_CTX_DATA_XLAT = 4;

struct ctx_xlat_cache {
  ctx_translation code{};
  std::array<ctx_translation, MAX_CTX_DATA_XLAT> data{};
  std::uint8_t next_victim = 0;

  void clear()
  {
    code = ctx_translation{};
    for (auto& entry : data) {
      entry = ctx_translation{};
    }
    next_victim = 0;
  }
};

/**
 * A hardware context: one in-flight function invocation.
 *
 * No stack, so creating and tearing one down is a slot write. The whole
 * architectural state is a PC into the body plus at most one cache block of
 * registers, which is what makes an arbitrary number of them affordable.
 */
struct context {
  std::uint64_t token = 0;
  champsim::origin origin{};
  std::uint32_t home_host = 0; // which compute tile to return to

  const function_body* body = nullptr;
  std::uint32_t pc = 0; // index into body->instrs

  /**
   * Added to every instruction address to select this tile's copy of the code.
   *
   * The copies sit on consecutive grains, so running on tile t means executing
   * at ip + code_bias. This is also why a carried code translation would be
   * worse than stale after a migration: the instruction's virtual address
   * genuinely changes.
   */
  std::uint64_t code_bias = 0;

  /** When this context arrived on its current tile, for the residency statistic. */
  champsim::chrono::clock::time_point arrived{};

  std::array<std::uint64_t, MAX_FUNCTION_REGS> regs{};
  std::array<bool, MAX_FUNCTION_REGS> ready{};
  std::uint8_t live_regs = 0;

  ctx_xlat_cache xlat{};

  ctx_state state = ctx_state::FREE;
  champsim::chrono::clock::time_point wake_time{};

  // The instruction block currently fetched, so a tight loop does not re-fetch
  // per instruction. Invalid until the first fetch.
  std::uint64_t fetched_block = 0;
  bool has_fetched = false;

  // Outstanding memory operations for the instruction at `pc`. The context
  // wakes when this reaches zero.
  std::uint8_t pending_mem = 0;

  // Waiting on the MMU rather than on data. Kept apart from pending_mem so the
  // statistics can separate time lost to translation from time lost to memory.
  bool awaiting_translation = false;

  // The atomic lock this context currently holds, if any. An in-order context
  // has at most one outstanding atomic, so one slot is enough -- and holding it
  // here means the lock can be released when the context leaves, not only when
  // its response arrives.
  std::uint64_t held_lock = 0;
  bool holds_lock = false;

  // Bookkeeping for the migration cold-start statistic.
  std::uint32_t migrations = 0;

  void reset()
  {
    *this = context{};
  }

  /** Everything that survives a hop between tiles. Translations do not. */
  void prepare_for_migration()
  {
    xlat.clear();
    has_fetched = false;
    fetched_block = 0;
    pending_mem = 0;
    awaiting_translation = false;
    state = ctx_state::MIGRATING;
    ++migrations;
  }
};

// ---- fabric messages ----

/** Compute tile → memory tile: start this invocation. */
struct invocation_msg {
  std::uint64_t token = 0;
  champsim::origin origin{};
  std::uint32_t home_host = 0;
  champsim::address entry_pc{}; // already resolved to the chosen tile's copy
  const function_body* body = nullptr;
};

/** Memory tile → memory tile: this context's next address lives elsewhere. */
struct migration_msg {
  context ctx{};
  std::size_t target_tile = 0;
};

/** Memory tile → compute tile: the invocation finished. */
struct completion_msg {
  std::uint64_t token = 0;
  std::uint32_t home_host = 0;
  std::uint8_t live_regs = 0;
};

/** Anything the fabric is holding until its delivery time. */
template <typename Payload>
struct in_flight {
  Payload payload{};
  champsim::chrono::clock::time_point deliver_at{};
};

/** How the dispatcher chooses which copy of a function to invoke. */
enum class placement_policy : std::uint8_t {
  ROUND_ROBIN,
  LEAST_LOADED,
  FIRST_TOUCH,
  RANDOM,
};

} // namespace nmfc

#endif // NMFC_TYPES_H
