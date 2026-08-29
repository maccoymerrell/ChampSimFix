// Top-down BFS whose inner work is an offloadable function.
//
// This is GAPBS's TDStep with the per-vertex neighbour scan lifted into a real
// function, compiled as its own symbol. Everything the simulator will see --
// the instructions, their program counters, the addresses they touch -- comes
// from the compiler and the running program. This file decides only *what the
// function is*, which is the shape question, and nothing about how it is
// encoded.
//
// The function is one vertex's neighbour scan: the neighbour list is
// contiguous, so it is a hot loop over one region, while parent[v] for each
// neighbour is scattered and is what forces movement. That is deliberate --
// the loop is the work, and the scatter is the cost being measured.

#include "benchmark.h"
#include "bitmap.h"
#include "builder.h"
#include "command_line.h"
#include "graph.h"
#include "platform_atomics.h"
#include "pvector.h"
#include "sliding_queue.h"

#include "nmfc_kernels.h"

#include <cstdio>
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>

extern "C" {
__attribute__((noinline, used)) void __champsim_start_trace(void) { asm volatile(""); }
__attribute__((noinline, used)) void __champsim_stop_trace(void) { asm volatile(""); }
}

/**
 * Geometry is compile-time, and that is a correctness requirement rather than
 * a tuning choice. Passed as runtime arguments it costs a hardware divide per
 * neighbour and pushes the parameter list past the registers the ABI passes
 * in, which puts arguments on a stack this machine does not have.
 */
static constexpr uint32_t NMFC_GRAIN_BITS = 20;
// No tile count here on purpose. The grain is a page size, which a compiler may
// know; how many channels back it, and which one backs any given grain, is the
// operating system's business and this program never asks.

/**
 * One offloaded function: claim up to six vertices, all owned by one tile.
 *
 * The work arrives in registers. A fork carries a 512-bit vector that is the
 * callee's entire register file, so an invocation can be handed its operands
 * outright instead of being pointed at a buffer -- and a function that reads
 * no buffer cannot be sitting on the wrong tile for it. Every address this
 * touches is inside `parent`, on its own tile, so it never migrates.
 *
 * Six is the register budget, not a tuning constant. Live at the compare and
 * swap: parent, u, three packed argument words, the result mask, the vertex
 * and the value read back. That is eight, which is the whole register file.
 * A fourth packed word would need nine and the function would be rejected.
 *
 * Returns a bitmask of which of the six were claimed, so the caller knows what
 * to put in the next frontier without reading anything back from memory.
 */
/**
 * One offloaded function: expand a vertex.
 *
 * It reads the vertex's neighbour list and claims every unvisited neighbour,
 * writing the winners out and returning where it stopped. That is the whole
 * per-edge body of the traversal -- the scan, the test, the compare and swap
 * -- so nearly all of the work leaves the host.
 *
 * Splitting the scan off and offloading only the compare and swap was tried
 * and measured: it left ninety percent of the traversal on the host, four
 * contexts busy of four thousand, and the memory channel one percent occupied.
 * A function has to be given the work, not a fragment of it.
 *
 * The neighbour list is contiguous, so reading it is sequential; parent[v] is
 * scattered, so claiming migrates. Whether that migration costs anything is a
 * question about whether the channel is busy, not a reason to move less work.
 *
 * Live at the compare and swap: p, e, parent, u, w, v, curr, and rax for the
 * swap itself. Eight. Returning the end pointer is what keeps the output base
 * from having to stay live alongside them.
 */
NMFC_FUNCTION
NodeID* nmfc_expand(const NodeID* first, const NodeID* last, NodeID* parent, NodeID u, NodeID* out)
{
  NodeID* w = out;
  for (const NodeID* p = first; p != last; ++p) {
    const NodeID v = *p;
    const NodeID curr = parent[v];
    if (curr < 0 && compare_and_swap(parent[v], curr, u)) {
      *w++ = v;
    }
  }
  return w;
}

/** Expansions issued before any result is read. */
static constexpr int32_t ISSUE = 32;
/**
 * Neighbours per invocation, which also bounds its output.
 *
 * A whole vertex per invocation overruns the output on a hub -- a kron vertex
 * can have tens of thousands of neighbours and claim most of them -- and it
 * makes the work per invocation as skewed as the degree distribution.
 * Chunking bounds both, and gives the machine more invocations to overlap.
 */
static constexpr int32_t CHUNK = 2048;

/**
 * The host walks the frontier and hands each vertex to a function.
 *
 * It issues a group of expansions before reading any of their results: the
 * return is a register dependency, so consuming it on the next line puts the
 * join one instruction behind the fork and nothing overlaps.
 */
static int64_t TDStepOffloaded(const Graph& g, NodeID* parent, SlidingQueue<NodeID>& queue, NodeID* out)
{
  int64_t scout_count = 0;
  QueueBuffer<NodeID> lqueue(queue);
  NodeID* end[ISSUE];
  NodeID* base[ISSUE];
  int32_t k = 0;

  const auto harvest = [&]() {
    for (int32_t i = 0; i < k; ++i) {
      for (NodeID* q = base[i]; q != end[i]; ++q) {
        lqueue.push_back(*q);
        scout_count += g.out_degree(*q);
      }
    }
    k = 0;
  };

  for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
    const NodeID u = *q_iter;
    auto neigh = g.out_neigh(u);
    for (const NodeID* p = neigh.begin(); p != neigh.end();) {
      const NodeID* const stop = (neigh.end() - p > CHUNK) ? p + CHUNK : neigh.end();
      base[k] = out + static_cast<std::size_t>(k) * CHUNK;
      end[k] = nmfc_expand(p, stop, parent, u, base[k]);
      p = stop;
      if (++k == ISSUE) {
        harvest();
      }
    }
  }
  harvest();
  lqueue.flush();
  return scout_count;
}

/**
 * parent[] is allocated grain-aligned on purpose.
 *
 * The function decides a neighbour's tile from the address of its parent entry.
 * The annotation pass later remaps arrays to grain-aligned bases in the
 * simulated space, so a grain-aligned real base makes the two tile numberings
 * differ by a constant rotation -- which leaves every bucket homogeneous, even
 * though the tile it names changes. An unaligned base would split a bucket
 * across two simulated tiles and quietly break the property the split exists
 * to create.
 */
static NodeID* AllocParent(const Graph& g, uint32_t grain_bits)
{
  void* raw = nullptr;
  const std::size_t bytes = static_cast<std::size_t>(g.num_nodes()) * sizeof(NodeID);
  const std::size_t grain = std::size_t{1} << grain_bits;
  if (posix_memalign(&raw, grain, ((bytes + grain - 1) / grain) * grain) != 0) {
    std::fprintf(stderr, "nmfc: cannot allocate parent grain-aligned\n");
    std::exit(1);
  }
  NodeID* parent = static_cast<NodeID*>(raw);
  for (NodeID n = 0; n < g.num_nodes(); n++) {
    parent[n] = g.out_degree(n) != 0 ? -g.out_degree(n) : -1;
  }
  return parent;
}

/**
 * Where each array actually lives, so the annotation pass can place it.
 *
 * The pass has to remap real addresses into the simulated layout, and it is
 * not going to guess which allocation was which. This is the only thing the
 * benchmark tells it beyond the instruction stream, and it is the part of the
 * problem we legitimately control: layout, not content.
 */
struct region_note {
  const char* name;
  const void* base;
  std::size_t bytes;
};

static void write_manifest(const char* path, const std::vector<region_note>& regions)
{
  std::FILE* f = std::fopen(path, "w");
  if (f == nullptr) {
    std::fprintf(stderr, "nmfc: cannot write manifest %s\n", path);
    std::exit(1);
  }
  for (const auto& r : regions) {
    std::fprintf(f, "%s %llx %llx\n", r.name, (unsigned long long)(uintptr_t)r.base, (unsigned long long)r.bytes);
  }
  std::fclose(f);
  std::fprintf(stderr, "nmfc: wrote %s (%zu regions)\n", path, regions.size());
}

static NodeID* DOBFS(const Graph& g, NodeID source, uint32_t grain_bits, uint32_t tiles, const char* manifest)
{
  NodeID* parent = AllocParent(g, grain_bits);
  parent[source] = source;
  SlidingQueue<NodeID> queue(g.num_nodes());
  queue.push_back(source);
  queue.slide_window();

  const std::size_t grain = std::size_t{1} << NMFC_GRAIN_BITS;

  // Output space for a group of expansions. Which tile backs it is the
  // operating system's decision, not this program's.
  NodeID* out = nullptr;
  const std::size_t out_bytes = static_cast<std::size_t>(ISSUE) * CHUNK * sizeof(NodeID);
  if (posix_memalign(reinterpret_cast<void**>(&out), grain, grain * ((out_bytes + grain - 1) / grain)) != 0) {
    std::fprintf(stderr, "nmfc: cannot allocate expansion output\n");
    std::exit(1);
  }

  write_manifest(manifest, {
    {"index", (const void*)g.nmfc_out_index(), (static_cast<std::size_t>(g.num_nodes()) + 1) * sizeof(void*)},
    {"neighbors", (const void*)g.nmfc_out_neighbors(), static_cast<std::size_t>(g.num_edges_directed()) * sizeof(NodeID)},
    {"parent", parent, static_cast<std::size_t>(g.num_nodes()) * sizeof(NodeID)},
    {"out", out, grain * ((out_bytes + grain - 1) / grain)},
  });

  __champsim_start_trace();
  while (!queue.empty()) {
    TDStepOffloaded(g, parent, queue, out);
    queue.slide_window();
  }
  __champsim_stop_trace();

  for (NodeID n = 0; n < g.num_nodes(); n++) {
    if (parent[n] < -1) {
      parent[n] = -1;
    }
  }
  return parent;
}

int main(int argc, char* argv[])
{
  uint32_t grain_bits = 20;
  uint32_t tiles = 4;
  std::string manifest = "regions.txt";
  std::vector<char*> pass{argv[0]};
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--grain-bits" && i + 1 < argc) {
      grain_bits = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else if (a == "--tiles" && i + 1 < argc) {
      tiles = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else if (a == "--manifest" && i + 1 < argc) {
      manifest = argv[++i];
    } else {
      pass.push_back(argv[i]);
    }
  }
  CLApp cli(static_cast<int>(pass.size()), pass.data(), "nmfc-bfs");
  if (!cli.ParseArgs()) {
    return -1;
  }
  Builder b(cli);
  Graph g = b.MakeGraph();
  SourcePicker<Graph> sp(g, cli.start_vertex());
  const NodeID source = sp.PickNext();
  std::fprintf(stderr, "nmfc: %ld vertices, %ld edges, source %d, grain 2^%u, %u tiles\n", (long)g.num_nodes(), (long)g.num_edges_directed(),
               (int)source, grain_bits, tiles);
  NodeID* parent = DOBFS(g, source, grain_bits, tiles, manifest.c_str());
  int64_t reached = 0;
  for (NodeID n = 0; n < g.num_nodes(); n++) {
    if (parent[n] >= 0) {
      ++reached;
    }
  }
  std::fprintf(stderr, "nmfc: reached %ld vertices\n", (long)reached);
  free(parent);
  return 0;
}
