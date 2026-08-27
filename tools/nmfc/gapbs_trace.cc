/*
 * gapbs_trace — the pseudo-compiler, over real GAP Benchmark Suite graphs.
 *
 * Uses GAPBS's own builders and readers to get a graph -- kronecker, uniform
 * random, or a real graph file -- then lays it out, runs a kernel, and emits an
 * NMFC trace of what actually happened alongside a matched baseline trace of
 * the identical work.
 *
 * WHY IT RELAYS THE GRAPH RATHER THAN TRACING GAPBS IN PLACE. GAPBS keeps its
 * row index as an array of pointers into a flat edge array, and both are
 * private. More to the point, *choosing the layout is the pseudo-compiler's
 * job* -- placement under page-granularity interleaving is address assignment
 * and nothing else -- so the layout has to be ours. The graph structure,
 * degree distribution and traversal order are all still GAPBS's.
 *
 * THE TWO DECISIONS A REAL COMPILER WOULD MAKE, made explicitly:
 *
 *   SLICING. What one offloaded function is. `--slice vertex` makes it one
 *   vertex's neighbour scan; `--slice edge-chunk` cuts a high-degree vertex's
 *   scan into fixed-size pieces so one hub cannot monopolise a context. The
 *   right answer differs per algorithm and per graph, which is why it is a knob
 *   rather than a constant.
 *
 *   PLACEMENT. `--partition stripe` leaves arrays contiguous, so page
 *   interleaving spreads them across every channel -- good for bandwidth,
 *   useless for locality. `--partition block` gives vertex v's row bounds, edge
 *   list and value to tile v*N/V, which is contiguous by construction and needs
 *   no reordering. Whether that *helps* then depends entirely on whether the
 *   graph has locality in vertex id, which is precisely why road graphs and
 *   kronecker graphs should behave differently.
 *
 * Build: make -C tools/nmfc gapbs_trace   (after cloning ext/gapbs)
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "benchmark.h"
#include "builder.h"
#include "command_line.h"
#include "graph.h"
#include "pvector.h"

#include "gapbs_hooks.h"

namespace
{

using nmfc::gapbs::tracer;
namespace hooks = nmfc::gapbs;

constexpr std::uint64_t OFFSETS_BASE = 0x0000'2000'0000ULL;
constexpr std::uint64_t EDGES_BASE = 0x0000'4000'0000ULL;
constexpr std::uint64_t VALUES_BASE = 0x0000'8000'0000ULL;

/**
 * The graph as the pseudo-compiler chose to lay it out.
 *
 * Addresses are *simulated* rather than the host's: the trace is replayed
 * against a modelled machine, so what matters is that they are consistent,
 * grain-aligned, and chosen by us. Real host pointers would place the arrays
 * wherever malloc happened to, which is the one thing the placement pass exists
 * to take control of.
 */
struct layout {
  std::vector<std::int64_t> offsets; // offsets[v] .. offsets[v+1]
  std::vector<std::int32_t> edges;
  std::int64_t vertices = 0;

  std::uint32_t tiles = 4;
  unsigned grain_bits = 21;
  bool block_partition = false;

  // Simulated bases, one per array.
  std::uint64_t offsets_base = OFFSETS_BASE;
  std::uint64_t edges_base = EDGES_BASE;
  std::uint64_t values_base = VALUES_BASE;

  /** Which partition owns a vertex under a block partition. */
  [[nodiscard]] std::uint32_t owner(std::int64_t vertex) const
  {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(vertex) * tiles) / static_cast<std::uint64_t>(std::max<std::int64_t>(vertices, 1)));
  }

  [[nodiscard]] std::uint64_t grain() const { return std::uint64_t{1} << grain_bits; }

  /**
   * Place an object belonging to `tile` inside that tile's arena.
   *
   * A tile's arena is the strided set of grains congruent to it, so an offset
   * within the arena maps to an address by expanding the grain index.
   */
  [[nodiscard]] std::uint64_t arena_address(std::uint64_t base, std::uint32_t tile, std::uint64_t offset) const
  {
    return base + ((offset / grain()) * tiles + tile) * grain() + (offset % grain());
  }

  [[nodiscard]] std::uint64_t offsets_addr(std::int64_t vertex) const
  {
    if (!block_partition) {
      return offsets_base + static_cast<std::uint64_t>(vertex) * 8;
    }
    const auto tile = owner(vertex);
    const auto first = first_of_partition(tile);
    return arena_address(offsets_base, tile, static_cast<std::uint64_t>(vertex - first) * 16);
  }

  [[nodiscard]] std::uint64_t offsets_end_addr(std::int64_t vertex) const { return offsets_addr(vertex) + 8; }

  [[nodiscard]] std::uint64_t edges_addr(std::int64_t vertex, std::int64_t index_in_row) const
  {
    if (!block_partition) {
      return edges_base + static_cast<std::uint64_t>(offsets[static_cast<std::size_t>(vertex)] + index_in_row) * 4;
    }
    const auto tile = owner(vertex);
    const auto base_edge = offsets[static_cast<std::size_t>(first_of_partition(tile))];
    const auto within = offsets[static_cast<std::size_t>(vertex)] - base_edge + index_in_row;
    return arena_address(edges_base, tile, static_cast<std::uint64_t>(within) * 4);
  }

  [[nodiscard]] std::uint64_t values_addr(std::int64_t vertex) const
  {
    if (!block_partition) {
      return values_base + static_cast<std::uint64_t>(vertex) * 8;
    }
    const auto tile = owner(vertex);
    const auto first = first_of_partition(tile);
    return arena_address(values_base, tile, static_cast<std::uint64_t>(vertex - first) * 8);
  }

  [[nodiscard]] std::int64_t first_of_partition(std::uint32_t tile) const
  {
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(tile) * static_cast<std::uint64_t>(vertices)) / tiles);
  }

  /** Total bytes each array spans once placement has stretched it. */
  [[nodiscard]] std::uint64_t span_of(std::uint64_t base, const std::function<std::uint64_t(std::int64_t)>& address_of) const
  {
    std::uint64_t high = base;
    for (std::int64_t v = 0; v < vertices; v += std::max<std::int64_t>(vertices / 4096, 1)) {
      high = std::max(high, address_of(v) + 64);
    }
    if (vertices > 0) {
      high = std::max(high, address_of(vertices - 1) + 64);
    }
    return high - base;
  }
};

struct options {
  std::string out_nmfc = "gapbs.nmfc";
  std::string out_baseline = "gapbs.champsimtrace";
  std::uint32_t tiles = 4;
  unsigned grain_bits = 21;
  std::string partition = "stripe"; // "stripe" | "block"
  std::string slice = "vertex";     // "vertex" | "edge-chunk"
  std::int64_t chunk = 32;          // neighbours per invocation under edge-chunk
  std::int64_t max_visits = 20000;
};

} // namespace

int main(int argc, char** argv)
{
  options opt;

  // Pull our own flags out before GAPBS parses the rest, so its command line
  // (-g scale, -u scale, -f file) works unchanged.
  std::vector<char*> passthrough;
  passthrough.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string{}; };
    if (arg == "--out-nmfc") {
      opt.out_nmfc = next();
    } else if (arg == "--out-baseline") {
      opt.out_baseline = next();
    } else if (arg == "--tiles") {
      opt.tiles = static_cast<std::uint32_t>(std::stoul(next()));
    } else if (arg == "--grain-bits") {
      opt.grain_bits = static_cast<unsigned>(std::stoul(next()));
    } else if (arg == "--partition") {
      opt.partition = next();
    } else if (arg == "--slice") {
      opt.slice = next();
    } else if (arg == "--chunk") {
      opt.chunk = std::stoll(next());
    } else if (arg == "--visits") {
      opt.max_visits = std::stoll(next());
    } else {
      passthrough.push_back(argv[i]);
    }
  }

  CLApp cli(static_cast<int>(passthrough.size()), passthrough.data(), "nmfc-gapbs-trace");
  if (!cli.ParseArgs()) {
    return 1;
  }

  Builder b(cli);
  Graph g = b.MakeGraph();
  std::cerr << "nmfc: graph " << g.num_nodes() << " vertices, " << g.num_edges_directed() << " directed edges\n";

  // ---- relay into our own layout ----
  layout place;
  place.vertices = g.num_nodes();
  place.tiles = opt.tiles;
  place.grain_bits = opt.grain_bits;
  place.block_partition = (opt.partition == "block");
  place.offsets.resize(static_cast<std::size_t>(place.vertices) + 1, 0);

  std::int64_t running = 0;
  for (std::int64_t v = 0; v < place.vertices; ++v) {
    place.offsets[static_cast<std::size_t>(v)] = running;
    running += g.out_degree(static_cast<NodeID>(v));
  }
  place.offsets[static_cast<std::size_t>(place.vertices)] = running;
  place.edges.reserve(static_cast<std::size_t>(running));
  for (std::int64_t v = 0; v < place.vertices; ++v) {
    for (NodeID u : g.out_neigh(static_cast<NodeID>(v))) {
      place.edges.push_back(static_cast<std::int32_t>(u));
    }
  }

  auto& trace = tracer::instance();
  trace.open(opt.out_nmfc, opt.tiles, opt.grain_bits);
  trace.open_baseline(opt.out_baseline);

  // ---- placement pass ----
  const auto declare = [&](const char* name, std::uint64_t base, const std::function<std::uint64_t(std::int64_t)>& address_of) {
    const auto bytes = place.span_of(base, address_of);
    if (!place.block_partition) {
      trace.declare_region(name, reinterpret_cast<const void*>(base), bytes, nullptr);
      return;
    }
    // Under a block partition the arena expansion already put each grain on its
    // owner's channel, so the hint just states what the address already says.
    trace.declare_region(name, reinterpret_cast<const void*>(base), bytes,
                         [&place, base](std::uint64_t offset) { return static_cast<std::uint32_t>(((base + offset) >> place.grain_bits) % place.tiles); });
  };
  declare("offsets", place.offsets_base, [&](std::int64_t v) { return place.offsets_addr(v); });
  declare("edges", place.edges_base, [&](std::int64_t v) { return place.edges_addr(v, std::max<std::int64_t>(g.out_degree(static_cast<NodeID>(v)) - 1, 0)); });
  declare("values", place.values_base, [&](std::int64_t v) { return place.values_addr(v); });

  // The function's code: N copies on N consecutive grains, so copy t is on
  // channel t and choosing a tile is one add on the dispatch path.
  trace.declare_region("code", reinterpret_cast<const void*>(hooks::FUNC_CODE_BASE), place.grain() * opt.tiles, nullptr);

  // ---- the traversal ----
  //
  // A serial top-down BFS: pop a vertex, scan its neighbours, and for each one
  // read and conditionally claim its parent slot. The claim is a real
  // read-modify-write, which is the case the design's atomicity argument is
  // about -- every access to a given vertex converges on one function core, so
  // it is a local lock table rather than a coherence protocol.
  std::vector<std::uint8_t> visited(static_cast<std::size_t>(place.vertices), 0);
  std::vector<std::int64_t> frontier;
  std::vector<std::int64_t> next_frontier;

  const auto source = static_cast<std::int64_t>(cli.start_vertex() >= 0 ? cli.start_vertex() : 0);
  frontier.push_back(source);
  visited[static_cast<std::size_t>(source)] = 1;

  std::int64_t visits = 0;
  std::int64_t invocations = 0;

  while (!frontier.empty() && visits < opt.max_visits) {
    for (const auto u : frontier) {
      if (visits >= opt.max_visits) {
        break;
      }
      ++visits;

      const auto degree = place.offsets[static_cast<std::size_t>(u) + 1] - place.offsets[static_cast<std::size_t>(u)];
      const auto per_call = (opt.slice == "edge-chunk") ? opt.chunk : degree;
      const auto chunks = degree == 0 ? std::int64_t{1} : (degree + per_call - 1) / per_call;

      for (std::int64_t chunk = 0; chunk < chunks; ++chunk) {
        // Host side: take the next work item off the frontier, then offload.
        trace.host(reinterpret_cast<const void*>(place.values_addr(u)), hooks::R_VERTEX, hooks::R_VERTEX);

        const auto token = trace.begin_call();
        ++invocations;

        // The row bounds. Adjacent words, so the second usually hits the block
        // the first brought in.
        trace.body_load(reinterpret_cast<const void*>(place.offsets_addr(u)), hooks::R_ROW_BEGIN, hooks::R_VERTEX);
        trace.body_load(reinterpret_cast<const void*>(place.offsets_end_addr(u)), hooks::R_ROW_END, hooks::R_VERTEX);

        const auto begin = chunk * per_call;
        const auto end = std::min(degree, begin + per_call);
        for (std::int64_t i = begin; i < end; ++i) {
          const auto edge_index = place.offsets[static_cast<std::size_t>(u)] + i;
          const auto neighbour = place.edges[static_cast<std::size_t>(edge_index)];
          const bool more = (i + 1 < end);

          // Sequential within the row: these stream.
          trace.body_load(reinterpret_cast<const void*>(place.edges_addr(u, i)), hooks::R_NEIGHBOUR, hooks::R_ROW_BEGIN);

          // The scattered, dependent access the whole architecture exists to
          // overlap -- and a read-modify-write, not a plain load.
          trace.body_atomic(reinterpret_cast<const void*>(place.values_addr(neighbour)), hooks::R_VALUE, hooks::R_NEIGHBOUR);
          trace.body_alu(hooks::R_ACC, hooks::R_VALUE, hooks::R_ACC, nmfc::op_class::ALU, more);

          if (visited[static_cast<std::size_t>(neighbour)] == 0) {
            visited[static_cast<std::size_t>(neighbour)] = 1;
            next_frontier.push_back(neighbour);
          }
        }
        trace.end_call(token);
      }
    }
    frontier.swap(next_frontier);
    next_frontier.clear();
  }

  trace.close();

  std::cerr << "nmfc: " << visits << " vertex visits, " << invocations << " invocations"
            << " (slice=" << opt.slice << ", partition=" << opt.partition << ")\n";
  return 0;
}
