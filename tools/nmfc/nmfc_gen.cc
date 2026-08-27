/*
 * nmfc_gen — the pseudo-compiler.
 *
 * Builds a synthetic CSR graph, runs a traversal over it, and emits two traces
 * of *the same computation*:
 *
 *   - an NMFC trace, where each vertex visit is offloaded as a function whose
 *     dynamic body follows its CALL record;
 *   - a matched baseline ChampSim trace, where the identical instructions run
 *     inline on the host core.
 *
 * Emitting both from one traversal is the point. The two runs touch the same
 * addresses in the same order with the same dependency structure, so any
 * difference between them is the architecture and not the workload.
 *
 * "Pseudo-compilation" is the placement pass: choosing virtual addresses. Under
 * page-granularity interleaving a virtual address already names a tile, so
 * laying an array out contiguously stripes it across every channel, while
 * laying it out with a stride keeps a partition on one channel. The generator
 * emits PAGE_HINT records recording what it chose, and the simulator refuses to
 * run a trace whose geometry disagrees with its own configuration.
 *
 * The kernel is a real graph visit with a serial dependency chain, which is the
 * workload class the whole design exists for:
 *
 *     load  r1 <- offsets[v]        the row pointer
 *     load  r2 <- offsets[v+1]      its end, usually the same cache block
 *     for each neighbour:
 *       load r3 <- edges[i]         sequential within the row
 *       load r4 <- values[r3]       scattered: the expensive, serialising one
 *       alu  r5  = combine(r4, r5)
 *     store values[v] <- r5
 *
 * Build: make -C tools/nmfc
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../../inc/nmfc/nmfc_trace.h"

namespace
{

// ---- virtual address plan -------------------------------------------------
//
// Every base is grain-aligned, so a base's own tile is just its grain index
// modulo the tile count. The function's copies sit on consecutive grains
// starting at FUNC_CODE_BASE, which is what makes "copy t is on tile t" true
// by construction rather than by bookkeeping.

constexpr std::uint64_t HOST_CODE_BASE = 0x0000'0040'0000ULL;
constexpr std::uint64_t FUNC_CODE_BASE = 0x0000'1000'0000ULL;
constexpr std::uint64_t OFFSETS_BASE = 0x0000'2000'0000ULL;
constexpr std::uint64_t EDGES_BASE = 0x0000'4000'0000ULL;
constexpr std::uint64_t VALUES_BASE = 0x0000'8000'0000ULL;

constexpr std::uint64_t WORD = 8;

// Registers, in the function's own tiny namespace. 0 means "none", following
// ChampSim's convention, so ids run 1..num_regs.
constexpr unsigned char R_VERTEX = 1;
constexpr unsigned char R_ROW_BEGIN = 2;
constexpr unsigned char R_ROW_END = 3;
constexpr unsigned char R_NEIGHBOUR = 4;
constexpr unsigned char R_VALUE = 5;
constexpr unsigned char R_ACC = 6;
constexpr unsigned char R_INDEX = 7;

// Host-side registers for the loop that issues offloads.
constexpr unsigned char H_FRONTIER = 1;
constexpr unsigned char H_VERTEX = 2;
constexpr unsigned char H_RESULT = 3;
constexpr unsigned char H_COUNT = 4;

struct options {
  std::string out_nmfc = "workload.nmfc";
  std::string out_baseline = "workload.champsimtrace";
  std::uint64_t vertices = 1u << 18;
  std::uint64_t degree = 16;
  std::uint64_t roots = 4096;
  std::uint32_t tiles = 8;
  unsigned grain_bits = 21;
  unsigned page_bits = 12;
  unsigned block_bits = 6;
  std::string graph = "kron";
  std::string partition = "stripe"; // "stripe" | "silo"
  double locality = 0.0;            // fraction of edges kept inside a partition
  std::uint64_t seed = 1;
  bool no_return = false;
};

/** A CSR graph. Deliberately the same shape a real graph framework would use. */
struct csr_graph {
  std::vector<std::uint64_t> offsets; // vertices + 1
  std::vector<std::uint32_t> edges;
};

/**
 * Kronecker-ish: a skewed degree distribution, which is what makes real graph
 * traversal irregular. "uniform" is the control -- same edge count, flat
 * degree, so any difference between them is locality rather than volume.
 */
csr_graph build_graph(const options& opt)
{
  std::mt19937_64 rng{opt.seed};
  csr_graph graph;
  graph.offsets.resize(opt.vertices + 1, 0);

  std::vector<std::uint64_t> degrees(opt.vertices, 0);
  if (opt.graph == "uniform") {
    std::fill(std::begin(degrees), std::end(degrees), opt.degree);
  } else {
    // A lognormal draw normalised to the requested mean gives the heavy tail
    // without needing a full Kronecker generator.
    std::lognormal_distribution<double> dist{0.0, 1.6};
    double total = 0.0;
    std::vector<double> raw(opt.vertices);
    for (auto& value : raw) {
      value = dist(rng);
      total += value;
    }
    const double scale = static_cast<double>(opt.degree * opt.vertices) / total;
    for (std::uint64_t v = 0; v < opt.vertices; ++v) {
      degrees[v] = static_cast<std::uint64_t>(raw[v] * scale);
    }
  }

  std::uint64_t running = 0;
  for (std::uint64_t v = 0; v < opt.vertices; ++v) {
    graph.offsets[v] = running;
    running += degrees[v];
  }
  graph.offsets[opt.vertices] = running;

  graph.edges.resize(running);
  std::uniform_int_distribution<std::uint32_t> pick{0, static_cast<std::uint32_t>(opt.vertices - 1)};
  std::uniform_real_distribution<double> coin{0.0, 1.0};

  // Locality is the whole point of the placement pass. A partitioner's output
  // is a graph where most of a vertex's neighbours share its partition; with
  // locality 0 the neighbours are uniform and a silo'd layout buys nothing,
  // because the accesses are scattered no matter where the data sits.
  const auto partitions = static_cast<std::uint32_t>(opt.tiles);
  for (std::uint64_t v = 0; v < opt.vertices; ++v) {
    const auto home = static_cast<std::uint32_t>(v % partitions);
    for (auto i = graph.offsets[v]; i < graph.offsets[v + 1]; ++i) {
      auto neighbour = pick(rng);
      if (coin(rng) < opt.locality) {
        // Snap into the source's partition, keeping the draw otherwise uniform.
        neighbour = neighbour - (neighbour % partitions) + home;
        if (neighbour >= opt.vertices) {
          neighbour = home;
        }
      }
      graph.edges[i] = neighbour;
    }
  }
  return graph;
}

// ---- record construction --------------------------------------------------

nmfc::record blank(nmfc::op kind, std::uint64_t token)
{
  nmfc::record rec{};
  rec.kind = static_cast<std::uint8_t>(kind);
  rec.op_class = static_cast<std::uint8_t>(nmfc::op_class::ALU);
  rec.token = token;
  return rec;
}

void set_srcs(nmfc::record& rec, std::initializer_list<unsigned char> regs)
{
  std::size_t i = 0;
  for (auto reg : regs) {
    if (i < nmfc::NUM_SOURCES) {
      rec.instr.source_registers[i++] = reg;
    }
  }
}

void set_dsts(nmfc::record& rec, std::initializer_list<unsigned char> regs)
{
  std::size_t i = 0;
  for (auto reg : regs) {
    if (i < nmfc::NUM_DESTINATIONS) {
      rec.instr.destination_registers[i++] = reg;
    }
  }
}

/** Writes records to a file, counting what went by for the header. */
class trace_writer
{
public:
  explicit trace_writer(const std::string& path) : out_(path, std::ios::binary)
  {
    if (!out_) {
      std::cerr << "cannot open " << path << " for writing\n";
      std::exit(1);
    }
  }

  void reserve_header() { out_.write(reinterpret_cast<const char*>(&header_), sizeof(header_)); }

  void write(const nmfc::record& rec)
  {
    out_.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    ++records_;
    if (rec.kind == static_cast<std::uint8_t>(nmfc::op::CALL)) {
      ++calls_;
    }
  }

  void write_baseline(const nmfc::trace_instr& instr) { out_.write(reinterpret_cast<const char*>(&instr), sizeof(instr)); }

  void finish(nmfc::header header)
  {
    header.num_records = records_;
    header.num_calls = calls_;
    out_.seekp(0);
    out_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out_.close();
  }

  [[nodiscard]] std::uint64_t records() const { return records_; }
  [[nodiscard]] std::uint64_t calls() const { return calls_; }

private:
  std::ofstream out_;
  nmfc::header header_{};
  std::uint64_t records_ = 0;
  std::uint64_t calls_ = 0;
};

// ---- placement ------------------------------------------------------------

/**
 * Where an array element lives, given the partitioning choice.
 *
 * "stripe" is the natural layout: contiguous virtual addresses, which under
 * page-granularity interleaving already spread evenly over every channel.
 *
 * "silo" is what the pseudo-compiler is for: vertex v's data is placed in the
 * grain belonging to v's partition, so a whole partition of the graph lives on
 * one tile and a traversal that stays inside it never migrates. The cost is
 * capacity balance, which is what the vmem's spill rate reports.
 */
struct placement {
  const options& opt;

  [[nodiscard]] std::uint64_t grain() const { return std::uint64_t{1} << opt.grain_bits; }

  [[nodiscard]] std::uint64_t values_addr(std::uint32_t vertex) const
  {
    if (opt.partition == "silo") {
      const auto tile = vertex % opt.tiles;
      const auto within = vertex / opt.tiles;
      // Walk grains that belong to `tile`: those are the ones whose grain index
      // is congruent to the base's index plus a multiple of the tile count.
      const auto grain_index = (within * WORD) / grain();
      const auto offset_in_grain = (within * WORD) % grain();
      return VALUES_BASE + (grain_index * opt.tiles + tile) * grain() + offset_in_grain;
    }
    return VALUES_BASE + static_cast<std::uint64_t>(vertex) * WORD;
  }

  [[nodiscard]] std::uint64_t offsets_addr(std::uint32_t vertex) const { return OFFSETS_BASE + static_cast<std::uint64_t>(vertex) * WORD; }
  [[nodiscard]] std::uint64_t edges_addr(std::uint64_t index) const { return EDGES_BASE + index * 4; }

  /** The tile a virtual address names, under the NMFC layout. */
  [[nodiscard]] std::uint32_t tile_of(std::uint64_t vaddr) const { return static_cast<std::uint32_t>((vaddr >> opt.grain_bits) % opt.tiles); }
};

/** Every grain an array occupies, so the trace can declare its region and tile. */
void emit_hints_for(trace_writer& writer, const placement& place, std::uint64_t base, std::uint64_t bytes, nmfc::region region)
{
  const auto grain = place.grain();
  for (std::uint64_t offset = 0; offset < bytes; offset += grain) {
    const auto vaddr = base + offset;
    auto rec = blank(nmfc::op::PAGE_HINT, 0);
    rec.aux0 = vaddr >> place.opt.page_bits;
    rec.aux1 = nmfc::encode_page_hint(0, region, place.tile_of(vaddr));
    writer.write(rec);
  }
}

// ---- the kernel -----------------------------------------------------------

/**
 * The dynamic body of one vertex visit.
 *
 * Emitted identically for both traces: as BODY records in the NMFC trace, and
 * as ordinary instructions inline in the baseline. That identity is what makes
 * the comparison mean something.
 */
std::vector<nmfc::record> visit_body(const csr_graph& graph, const placement& place, std::uint32_t vertex, std::uint64_t token, std::uint64_t max_neighbours)
{
  std::vector<nmfc::record> body;
  std::uint64_t pc = FUNC_CODE_BASE;
  const auto step = [&pc]() { const auto here = pc; pc += 4; return here; };

  // The row pointer and its end. Adjacent words, so the second usually hits the
  // block the first just brought in -- real behaviour worth keeping.
  auto row_begin = blank(nmfc::op::BODY, token);
  row_begin.instr.ip = step();
  row_begin.instr.source_memory[0] = place.offsets_addr(vertex);
  row_begin.op_class = static_cast<std::uint8_t>(nmfc::op_class::LOAD);
  set_srcs(row_begin, {R_VERTEX});
  set_dsts(row_begin, {R_ROW_BEGIN});
  body.push_back(row_begin);

  auto row_end = blank(nmfc::op::BODY, token);
  row_end.instr.ip = step();
  row_end.instr.source_memory[0] = place.offsets_addr(vertex + 1);
  row_end.op_class = static_cast<std::uint8_t>(nmfc::op_class::LOAD);
  set_srcs(row_end, {R_VERTEX});
  set_dsts(row_end, {R_ROW_END});
  body.push_back(row_end);

  const auto begin = graph.offsets[vertex];
  const auto end = std::min(graph.offsets[vertex + 1], begin + max_neighbours);

  for (auto i = begin; i < end; ++i) {
    // The neighbour id: sequential within the row, so these stream.
    auto edge = blank(nmfc::op::BODY, token);
    edge.instr.ip = step();
    edge.instr.source_memory[0] = place.edges_addr(i);
    edge.op_class = static_cast<std::uint8_t>(nmfc::op_class::LOAD);
    set_srcs(edge, {R_ROW_BEGIN, R_INDEX});
    set_dsts(edge, {R_NEIGHBOUR});
    body.push_back(edge);

    // The neighbour's value: a scattered dependent load. This is the access
    // the whole architecture exists to overlap, and it depends on the edge
    // load above, so nothing within one iteration can hide it.
    auto value = blank(nmfc::op::BODY, token);
    value.instr.ip = step();
    value.instr.source_memory[0] = place.values_addr(graph.edges[i]);
    value.op_class = static_cast<std::uint8_t>(nmfc::op_class::LOAD);
    set_srcs(value, {R_NEIGHBOUR});
    set_dsts(value, {R_VALUE});
    body.push_back(value);

    auto combine = blank(nmfc::op::BODY, token);
    combine.instr.ip = step();
    combine.op_class = static_cast<std::uint8_t>(nmfc::op_class::ALU);
    set_srcs(combine, {R_VALUE, R_ACC});
    set_dsts(combine, {R_ACC});
    body.push_back(combine);

    auto advance = blank(nmfc::op::BODY, token);
    advance.instr.ip = step();
    advance.instr.is_branch = 1;
    advance.instr.branch_taken = (i + 1 < end) ? 1 : 0;
    advance.op_class = static_cast<std::uint8_t>(nmfc::op_class::BRANCH);
    set_srcs(advance, {R_INDEX, R_ROW_END});
    set_dsts(advance, {R_INDEX});
    if (i + 1 < end) {
      // A taken backward branch re-enters the loop head, so the next
      // instruction is a branch target and the core charges a fetch bubble
      // rather than getting perfect prediction free from a replayed trace.
      advance.flag_bits |= nmfc::FLAG_TAKEN_TARGET;
      pc = FUNC_CODE_BASE + 8; // back to the loop head
    }
    body.push_back(advance);
  }

  // Write the accumulated result back to this vertex.
  auto writeback = blank(nmfc::op::BODY, token);
  writeback.instr.ip = step();
  writeback.instr.destination_memory[0] = place.values_addr(vertex);
  writeback.op_class = static_cast<std::uint8_t>(nmfc::op_class::STORE);
  set_srcs(writeback, {R_ACC});
  body.push_back(writeback);

  return body;
}

} // namespace

int main(int argc, char** argv)
{
  options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string{}; };
    if (arg == "--out-nmfc") {
      opt.out_nmfc = next();
    } else if (arg == "--out-baseline") {
      opt.out_baseline = next();
    } else if (arg == "--vertices") {
      opt.vertices = std::stoull(next());
    } else if (arg == "--degree") {
      opt.degree = std::stoull(next());
    } else if (arg == "--roots") {
      opt.roots = std::stoull(next());
    } else if (arg == "--tiles") {
      opt.tiles = static_cast<std::uint32_t>(std::stoul(next()));
    } else if (arg == "--grain-bits") {
      opt.grain_bits = static_cast<unsigned>(std::stoul(next()));
    } else if (arg == "--graph") {
      opt.graph = next();
    } else if (arg == "--partition") {
      opt.partition = next();
    } else if (arg == "--locality") {
      opt.locality = std::stod(next());
    } else if (arg == "--seed") {
      opt.seed = std::stoull(next());
    } else if (arg == "--no-return") {
      opt.no_return = true;
    } else if (arg == "--help") {
      std::cout << "nmfc_gen --out-nmfc F --out-baseline F [--vertices N] [--degree N] [--roots N]\n"
                   "         [--tiles N] [--grain-bits N] [--graph kron|uniform]\n"
                   "         [--partition stripe|silo] [--locality 0..1] [--seed N] [--no-return]\n";
      return 0;
    } else {
      std::cerr << "unknown option: " << arg << "\n";
      return 1;
    }
  }

  const placement place{opt};
  std::cerr << "building " << opt.graph << " graph: " << opt.vertices << " vertices, mean degree " << opt.degree << "\n";
  const auto graph = build_graph(opt);
  std::cerr << "  " << graph.edges.size() << " edges, locality " << opt.locality << ", partition " << opt.partition << "\n";

  trace_writer nmfc_out{opt.out_nmfc};
  nmfc_out.reserve_header();
  std::ofstream baseline{opt.out_baseline, std::ios::binary};
  if (!baseline) {
    std::cerr << "cannot open " << opt.out_baseline << "\n";
    return 1;
  }

  // Declare the layout the placement pass chose. The simulator validates the
  // geometry in the header and refuses a trace placed for a different machine.
  emit_hints_for(nmfc_out, place, FUNC_CODE_BASE, place.grain() * opt.tiles, nmfc::region::NMFC);
  emit_hints_for(nmfc_out, place, OFFSETS_BASE, (opt.vertices + 1) * WORD, nmfc::region::NMFC);
  emit_hints_for(nmfc_out, place, EDGES_BASE, graph.edges.size() * 4, nmfc::region::NMFC);
  emit_hints_for(nmfc_out, place, VALUES_BASE, opt.vertices * WORD * (opt.partition == "silo" ? opt.tiles : 1), nmfc::region::NMFC);

  std::mt19937_64 rng{opt.seed ^ 0x5DEECE66DULL};
  std::uniform_int_distribution<std::uint32_t> pick_root{0, static_cast<std::uint32_t>(opt.vertices - 1)};

  std::uint64_t host_pc = HOST_CODE_BASE;
  std::uint64_t baseline_instrs = 0;
  const auto host_step = [&host_pc]() { const auto here = host_pc; host_pc += 4; if (host_pc > HOST_CODE_BASE + 0x1000) { host_pc = HOST_CODE_BASE; } return here; };

  for (std::uint64_t n = 0; n < opt.roots; ++n) {
    const auto vertex = pick_root(rng);
    const auto token = n + 1; // token 0 means "not an invocation"
    auto body = visit_body(graph, place, vertex, token, /*max_neighbours=*/32);

    // --- host stream: pull the next vertex off the frontier, then offload ---
    auto frontier = blank(nmfc::op::HOST, 0);
    frontier.instr.ip = host_step();
    frontier.instr.source_memory[0] = place.values_addr(vertex);
    set_srcs(frontier, {H_FRONTIER});
    set_dsts(frontier, {H_VERTEX});
    nmfc_out.write(frontier);
    baseline.write(reinterpret_cast<const char*>(&frontier.instr), sizeof(frontier.instr));
    ++baseline_instrs;

    auto call = blank(nmfc::op::CALL, token);
    call.instr.ip = host_step();
    // source_memory[0] is left for the reader to fill with the offload aperture
    // address that encodes this token: the aperture is a property of the
    // machine, not of the compilation.
    set_srcs(call, {H_VERTEX});
    set_dsts(call, {H_RESULT});
    call.aux0 = FUNC_CODE_BASE;
    call.aux1 = nmfc::encode_call_aux1(/*func_id=*/1, static_cast<std::uint32_t>(body.size()));
    if (opt.no_return) {
      call.flag_bits |= nmfc::FLAG_NO_RETURN;
    }
    nmfc_out.write(call);

    // --- the body: offloaded in one trace, inline in the other ---
    for (const auto& rec : body) {
      nmfc_out.write(rec);
      baseline.write(reinterpret_cast<const char*>(&rec.instr), sizeof(rec.instr));
      ++baseline_instrs;
    }

    auto ret = blank(nmfc::op::RET, token);
    ret.instr.ip = host_step();
    ret.aux0 = R_ACC; // live regfile words to carry home
    nmfc_out.write(ret);

    // --- host stream: consume the result and loop ---
    auto consume = blank(nmfc::op::HOST, 0);
    consume.instr.ip = host_step();
    consume.instr.is_branch = 1;
    consume.instr.branch_taken = (n + 1 < opt.roots) ? 1 : 0;
    set_srcs(consume, {H_RESULT, H_COUNT});
    set_dsts(consume, {H_COUNT});
    nmfc_out.write(consume);
    baseline.write(reinterpret_cast<const char*>(&consume.instr), sizeof(consume.instr));
    ++baseline_instrs;
  }

  nmfc::header header{};
  header.magic = nmfc::TRACE_MAGIC;
  header.version = nmfc::TRACE_VERSION;
  header.record_size = sizeof(nmfc::record);
  header.num_regs = static_cast<std::uint32_t>(nmfc::MAX_FUNCTION_REGS);
  header.num_tiles = opt.tiles;
  header.page_size = 1u << opt.page_bits;
  header.block_size = 1u << opt.block_bits;
  header.interleave_shift = opt.grain_bits;
  header.num_asids = 1;
  nmfc_out.finish(header);
  baseline.close();

  std::cerr << "wrote " << opt.out_nmfc << ": " << nmfc_out.records() << " records, " << nmfc_out.calls() << " invocations\n";
  std::cerr << "wrote " << opt.out_baseline << ": " << baseline_instrs << " instructions\n";
  return 0;
}
