/*
 * NMFC trace container format.
 *
 * This header is deliberately standalone: it includes nothing from ChampSim, so
 * the offline trace generator (tools/nmfc) and the in-simulator trace reader
 * (src/nmfc/nmfc_producer.cc) share one definition of the on-disk layout without
 * dragging the simulator into the generator's build.
 *
 * A trace is a 64-byte header followed by a stream of fixed 96-byte records.
 * Each record embeds a classic ChampSim `input_instr` payload (identical
 * field-for-field, so a host record inflates through the existing
 * ooo_model_instr constructor) plus the NMFC annotation the pseudo-compiler
 * emits.
 *
 * Record ordering contract
 * ------------------------
 * Host instructions appear in program order. A CALL record is a host
 * instruction that offloads a function; it is immediately followed by that
 * invocation's complete dynamic body (BODY records) terminated by a RET record,
 * all carrying the same token. The body is therefore contiguous, so the reader
 * buffers exactly one body at a time and publishes it whole. PAGE_HINT records
 * are not instructions; they may appear anywhere and are consumed by the reader.
 *
 * Geometry agreement
 * ------------------
 * The header records the page size, block size, tile count and interleave shift
 * the generator's placement pass assumed. The simulator compares them against
 * its own configuration at open time and refuses to run on a mismatch: a trace
 * placed for 8 tiles at page granularity, replayed on a 16-tile machine, would
 * silently invalidate every siloing result it was built to measure.
 */

#ifndef NMFC_TRACE_H
#define NMFC_TRACE_H

#include <cstddef>
#include <cstdint>

namespace nmfc
{

inline constexpr unsigned long long TRACE_MAGIC = 0x3143'5254'4346'4d4eULL; // "NMFCTRC1" little-endian
inline constexpr std::uint32_t TRACE_VERSION = 1;

// Must match champsim's trace_instruction.h. Duplicated (not included) so the
// generator builds standalone; nmfc_types.h static_asserts that they agree.
inline constexpr std::size_t NUM_DESTINATIONS = 2;
inline constexpr std::size_t NUM_SOURCES = 4;

// The maximum function regfile, in 64-bit words. The design constrains a
// function's whole local state to at most one cache block, so eight words at 64 B.
inline constexpr std::size_t MAX_FUNCTION_REGS = 8;

/** What a record means. */
enum class op : std::uint8_t {
  /** An ordinary host instruction, executed by the compute tile's OoO core. */
  HOST = 0,
  /**
   * A host instruction that offloads a function. The reader rewrites its
   * source_memory[0] to the offload aperture address that encodes `token`, so
   * the host core's function tracking unit recognises it without any change to
   * ooo_model_instr.
   *   aux0 = function entry PC (virtual) -- its physical page picks the tile
   *   aux1 = (func_id << 32) | body_length_in_records
   */
  CALL = 1,
  /** One dynamic instruction of the body of invocation `token`. */
  BODY = 2,
  /**
   * End of the body of invocation `token`.
   *   aux0 = live regfile words to carry home (sizes the return message)
   */
  RET = 3,
  /**
   * Placement hint, not an instruction. Virtual page `aux0` of address space
   * `asid` should be backed by physical memory owned by tile `tile`, in the
   * given mapping region. This is how the pseudo-compiler silos a data
   * structure -- or a function's code -- onto one memory tile.
   *   aux0 = virtual page number
   *   aux1 = (asid << 40) | (region << 32) | tile      (see enum region)
   */
  PAGE_HINT = 4,
  /**
   * A body instruction with atomic read-modify-write semantics on
   * source_memory[0]. Serialized against other atomics to the same block by the
   * owning function core, which is what makes atomicity local here.
   */
  ATOMIC = 5,
};

/**
 * Which DRAM address mapping backs a page.
 *
 * The two regions exist because one interleave granularity cannot serve both
 * jobs. STANDARD keeps the classic layout (channel bits just above the block
 * offset) so streaming traffic spreads across every channel. NMFC lifts the
 * channel bits above the page offset and pushes bank/bankgroup bits down below
 * it, so a whole page lives on one tile -- which is what makes siloing
 * expressible -- while the page's own blocks still spread across banks.
 */
enum class region : std::uint8_t {
  STANDARD = 0,
  NMFC = 1,
};

/** Coarse operation class, for a config-driven execute-latency table. */
enum class op_class : std::uint8_t {
  ALU = 0,
  MUL = 1,
  DIV = 2,
  FP = 3,
  FP_DIV = 4,
  BRANCH = 5,
  LOAD = 6,
  STORE = 7,
};

/** Per-record flag bits. */
enum flags : std::uint8_t {
  /**
   * Fire-and-forget offload (CALL only): the function produces no value the
   * host consumes, so the host's tracking slot frees at dispatch instead of at
   * return. Lets the generator model push-style offloads (worklist appends,
   * atomic counter updates) that should not cost the host an in-flight slot.
   */
  FLAG_NO_RETURN = 1U << 0,
  /**
   * The body instruction begins a new basic block reached by a taken branch
   * (BODY only). The function core charges a configurable fetch bubble, so a
   * replayed dynamic trace does not silently hand the function core perfect
   * branch prediction for free.
   */
  FLAG_TAKEN_TARGET = 1U << 1,
};

/**
 * The classic ChampSim trace payload. Field-for-field identical to
 * `input_instr`; kept separate so this header stands alone.
 *
 * The register fields are load-bearing for the function core: it runs a tiny
 * in-order scoreboard over them, so an invocation with two independent loads
 * issues both and gets MLP 2 without any reordering hardware. Intra-function
 * MLP therefore costs no extra trace field.
 */
struct trace_instr {
  unsigned long long ip;

  unsigned char is_branch;
  unsigned char branch_taken;

  unsigned char destination_registers[NUM_DESTINATIONS]; // NOLINT(*-avoid-c-arrays)
  unsigned char source_registers[NUM_SOURCES];           // NOLINT(*-avoid-c-arrays)

  unsigned long long destination_memory[NUM_DESTINATIONS]; // NOLINT(*-avoid-c-arrays)
  unsigned long long source_memory[NUM_SOURCES];           // NOLINT(*-avoid-c-arrays)
};

/** One NMFC trace record: a classic instruction plus its NMFC annotation. */
struct record {
  trace_instr instr;
  std::uint64_t token;    // invocation id; 0 for host instructions and hints
  std::uint64_t aux0;     // see enum op
  std::uint64_t aux1;     // see enum op
  std::uint8_t kind;      // one of nmfc::op
  std::uint8_t op_class;  // one of nmfc::op_class
  std::uint8_t flag_bits; // OR of nmfc::flags
  std::uint8_t pad[5];    // NOLINT(*-avoid-c-arrays)
};

/**
 * Fixed 64-byte file header. The geometry fields are a contract, not
 * documentation: the reader validates every one of them against the running
 * configuration and aborts on disagreement.
 */
struct header {
  unsigned long long magic;
  std::uint32_t version;
  std::uint32_t record_size;
  std::uint32_t num_regs;          // function regfile width, in 64-bit words
  std::uint32_t num_tiles;         // memory tiles the placement pass targeted
  std::uint32_t page_size;         // bytes
  std::uint32_t block_size;        // bytes
  std::uint32_t interleave_shift;  // log2 of the tile interleave granularity
  std::uint32_t num_asids;         // address spaces present in the stream
  std::uint64_t num_records;
  std::uint64_t num_calls;
  std::uint8_t reserved[8];        // NOLINT(*-avoid-c-arrays)
};

static_assert(sizeof(trace_instr) == 64, "trace_instr must match input_instr's layout");
static_assert(sizeof(record) == 96, "NMFC record layout is part of the file format");
static_assert(sizeof(header) == 64, "NMFC header layout is part of the file format");

/** Decompose a PAGE_HINT's aux1. */
struct page_hint_fields {
  std::uint32_t tile;
  region reg;
  std::uint32_t asid;
};

inline constexpr page_hint_fields decode_page_hint(std::uint64_t aux1)
{
  return page_hint_fields{static_cast<std::uint32_t>(aux1 & 0xffff'ffffULL), static_cast<region>((aux1 >> 32) & 0xffULL),
                          static_cast<std::uint32_t>(aux1 >> 40)};
}

inline constexpr std::uint64_t encode_page_hint(std::uint32_t asid, region reg, std::uint32_t tile)
{
  return (static_cast<std::uint64_t>(asid) << 40) | (static_cast<std::uint64_t>(reg) << 32) | tile;
}

/** Decompose a CALL's aux1. */
inline constexpr std::uint32_t call_func_id(std::uint64_t aux1) { return static_cast<std::uint32_t>(aux1 >> 32); }
inline constexpr std::uint32_t call_body_length(std::uint64_t aux1) { return static_cast<std::uint32_t>(aux1 & 0xffff'ffffULL); }
inline constexpr std::uint64_t encode_call_aux1(std::uint32_t func_id, std::uint32_t body_length)
{
  return (static_cast<std::uint64_t>(func_id) << 32) | body_length;
}

} // namespace nmfc

#endif // NMFC_TRACE_H
