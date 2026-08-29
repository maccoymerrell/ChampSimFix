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
__attribute__((noinline, used)) void __nmfc_wait(const void* p) { (void)*static_cast<const volatile int*>(p); }
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
 * One offloaded function: expand a chunk of a vertex's neighbours.
 *
 * It returns nothing and waits for nothing. Claimed vertices are written into
 * the output slot it was handed, terminated by -1, and the host reads the
 * slots once the level is done.
 *
 * Returning a value is what kept the machine empty: a return is a register
 * dependency, so the join lands one instruction behind the fork and the host
 * could never have more than a reorder buffer's worth outstanding. With
 * nothing to wait for it can fork a whole level.
 *
 * The output is *per invocation* on purpose. Appending to a shared frontier
 * through an atomic counter was tried and is far worse than it looks: one word,
 * contended by every claim on every tile, so each one has to reach that word
 * and the whole machine serialises on a single address. A slot per invocation
 * has no shared state at all.
 *
 * The terminator is a register budget, not a style choice. Live at the compare
 * and swap: p, e, parent, u, w, v, curr, and the register the swap pins --
 * eight, the whole file. Returning a count would need the slot base to stay
 * live alongside them, which is nine.
 */
NMFC_FUNCTION
void nmfc_expand(const NodeID* first, const NodeID* last, NodeID* parent, NodeID u, NodeID* out)
{
  NodeID* w = out;
  for (const NodeID* p = first; p != last; ++p) {
    const NodeID v = *p;
    const NodeID curr = parent[v];
    if (curr < 0 && compare_and_swap(parent[v], curr, u)) {
      *w++ = v;
    }
  }
  // Publish. The marker names this store as the commit: the block is complete
  // and whoever waits on this address may proceed.
  NMFC_COMMIT(w, -1);
}

/** Neighbours per invocation. Bounds the work, and the output slot with it. */
static constexpr int32_t CHUNK = 2048;
/** Invocations forked before the host reads any of their slots. */
static constexpr int32_t LEVEL = 1024;
/**
 * Slot stride, padded to a cache block.
 *
 * Ownership of a committed result is by address, at block granularity, so two
 * slots must never share a block. CHUNK+1 entries is 8196 bytes, which is not
 * a multiple of 64: one slot's last block is the next slot's first, and two
 * invocations then claim the same block. The pairing check caught it on its
 * first run.
 */
static constexpr int32_t SLOT = ((CHUNK + 1) * static_cast<int32_t>(sizeof(NodeID)) + 63) / 64 * 64 / static_cast<int32_t>(sizeof(NodeID));

/**
 * A step: fork expansions until the slot pool is full, then read the slots.
 *
 * The host touches no invocation's result while forking, so nothing serialises
 * the forks against each other and a poolful can be resident at once.
 */
static int64_t TDStepOffloaded(const Graph& g, NodeID* parent, SlidingQueue<NodeID>& queue, NodeID* pool)
{
  int64_t scout_count = 0;
  QueueBuffer<NodeID> lqueue(queue);
  int32_t issued = 0;

  const auto harvest = [&]() {
    for (int32_t i = 0; i < issued; ++i) {
      const NodeID* const slot = pool + static_cast<std::size_t>(i) * SLOT;
      // The wait site. On real hardware the core spins here until the block is
      // committed; in the traced run it is already there, which is exactly why
      // the site has to be marked rather than inferred.
      __nmfc_wait(slot);
      for (const NodeID* q = slot; *q >= 0; ++q) {
        lqueue.push_back(*q);
        scout_count += g.out_degree(*q);
      }
    }
    issued = 0;
  };

  for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
    const NodeID u = *q_iter;
    auto neigh = g.out_neigh(u);
    for (const NodeID* p = neigh.begin(); p != neigh.end();) {
      const NodeID* const stop = (neigh.end() - p > CHUNK) ? p + CHUNK : neigh.end();
      nmfc_expand(p, stop, parent, u, pool + static_cast<std::size_t>(issued) * SLOT);
      p = stop;
      if (++issued == LEVEL) {
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

  // The next frontier, built by the functions themselves: element 0 is the
  // count, the vertices follow. Which tile backs it is the operating system's
  // decision, not this program's.
  NodeID* pool = nullptr;
  const std::size_t pool_bytes = static_cast<std::size_t>(LEVEL) * SLOT * sizeof(NodeID);
  if (posix_memalign(reinterpret_cast<void**>(&pool), grain, grain * ((pool_bytes + grain - 1) / grain)) != 0) {
    std::fprintf(stderr, "nmfc: cannot allocate the output pool\n");
    std::exit(1);
  }

  write_manifest(manifest, {
    {"index", (const void*)g.nmfc_out_index(), (static_cast<std::size_t>(g.num_nodes()) + 1) * sizeof(void*)},
    {"neighbors", (const void*)g.nmfc_out_neighbors(), static_cast<std::size_t>(g.num_edges_directed()) * sizeof(NodeID)},
    {"parent", parent, static_cast<std::size_t>(g.num_nodes()) * sizeof(NodeID)},
    {"pool", pool, grain * ((pool_bytes + grain - 1) / grain)},
  });

  __champsim_start_trace();
  while (!queue.empty()) {
    TDStepOffloaded(g, parent, queue, pool);
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
