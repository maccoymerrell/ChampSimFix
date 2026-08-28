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
 * Two functions, not one, because the work has two access patterns that want
 * different homes.
 *
 * nmfc_scan streams a vertex's neighbour list -- contiguous, sequential, and on
 * whichever tile owns that stretch of the edge array. It touches parent[] only
 * to compute which tile owns each neighbour's entry, and sorts the neighbours
 * into per-tile buckets.
 *
 * nmfc_claim takes a bucket whose entries all belong to one tile and does the
 * compare-and-swaps. Every address it touches is on its own tile by
 * construction, so it never migrates; its accesses are scattered within one
 * channel, which is what spreads them over that channel's banks.
 *
 * Splitting this way is the point: one function cannot be both a sequential
 * streamer and a local random-access loop, and asking it to be both is what
 * made the single-function version change tiles every few instructions.
 */

/** Which tile owns an address, under the congruent mapping the simulator uses. */
static inline uint32_t nmfc_tile_of(const void* p, uint32_t grain_bits, uint32_t tiles)
{
  return static_cast<uint32_t>((reinterpret_cast<uintptr_t>(p) >> grain_bits) % tiles);
}

/**
 * Sort u's neighbours into per-tile buckets by the tile owning parent[v].
 * `bucket` is `tiles` lanes of `cap` entries; `count` is per-lane occupancy.
 */
NMFC_FUNCTION
const NodeID* nmfc_scan(const NodeID* first, const NodeID* last, const NodeID* parent, NodeID* const* lane, int32_t* count, int32_t cap,
                        uint32_t grain_bits, uint32_t tiles)
{
  for (const NodeID* p = first; p != last; ++p) {
    const NodeID v = *p;
    const uint32_t t = nmfc_tile_of(&parent[v], grain_bits, tiles);
    int32_t& n = count[t];
    if (n == cap) {
      return p; // this lane is full; the caller drains and resumes here
    }
    lane[t][n] = v;
    ++n;
  }
  return last;
}

/**
 * Claim every vertex in one bucket. All of `parent` touched here lives on this
 * function's own tile, so this loop does not migrate.
 */
NMFC_FUNCTION
int32_t nmfc_claim(const NodeID* bucket, int32_t n, NodeID* parent, NodeID u, NodeID* claimed)
{
  int32_t got = 0;
  for (int32_t i = 0; i < n; ++i) {
    const NodeID v = bucket[i];
    const NodeID curr = parent[v];
    if (curr < 0 && compare_and_swap(parent[v], curr, u)) {
      claimed[got++] = v;
    }
  }
  return got;
}

static int64_t TDStepOffloaded(const Graph& g, NodeID* parent, SlidingQueue<NodeID>& queue, NodeID* const* lane, int32_t* count, int32_t cap,
                               NodeID* const* out, uint32_t grain_bits, uint32_t tiles)
{
  int64_t scout_count = 0;
  QueueBuffer<NodeID> lqueue(queue);
  for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
    const NodeID u = *q_iter;
    auto neigh = g.out_neigh(u);
    const NodeID* first = neigh.begin();
    const NodeID* last = neigh.end();

    // Drain in passes so a full lane never costs a neighbour. A vertex with
    // more neighbours on one tile than a bucket holds takes several passes;
    // nothing is dropped.
    const NodeID* p = first;
    while (p != last) {
      for (uint32_t t = 0; t < tiles; ++t) {
        count[t] = 0;
      }
      // Producer: streams the edge list, sorts neighbours by owning tile.
      p = nmfc_scan(p, last, parent, lane, count, cap, grain_bits, tiles);
      // Consumers: one per tile, each entirely local to that tile.
      for (uint32_t t = 0; t < tiles; ++t) {
        if (count[t] == 0) {
          continue;
        }
        const int32_t got = nmfc_claim(lane[t], count[t], parent, u, out[t]);
        for (int32_t i = 0; i < got; ++i) {
          lqueue.push_back(out[t][i]);
          scout_count += g.out_degree(out[t][i]);
        }
      }
    }
  }
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

static NodeID* DOBFS(const Graph& g, NodeID source, uint32_t grain_bits, uint32_t tiles)
{
  NodeID* parent = AllocParent(g, grain_bits);
  parent[source] = source;
  SlidingQueue<NodeID> queue(g.num_nodes());
  queue.push_back(source);
  queue.slide_window();

  // The consumer reads its bucket and updates parent entries. Both must be on
  // its own tile or it ping-pongs once per element -- which is what the first
  // version of this split did, changing tile every five instructions despite
  // being written to never migrate. So each lane is placed on the tile it
  // serves: one grain per lane, ordered so lane t lands on tile t.
  const std::size_t grain = std::size_t{1} << grain_bits;
  const int32_t cap = static_cast<int32_t>(grain / sizeof(NodeID));
  NodeID* bucket_base = nullptr;
  NodeID* claimed_base = nullptr;
  if (posix_memalign(reinterpret_cast<void**>(&bucket_base), grain, grain * tiles) != 0 ||
      posix_memalign(reinterpret_cast<void**>(&claimed_base), grain, grain * tiles) != 0) {
    std::fprintf(stderr, "nmfc: cannot allocate tile-local scratch\n");
    std::exit(1);
  }
  // Lane order is rotated so that lane t is the grain whose tile number is t.
  std::vector<NodeID*> lane(tiles), out(tiles);
  const uint32_t bucket_home = nmfc_tile_of(bucket_base, grain_bits, tiles);
  const uint32_t claimed_home = nmfc_tile_of(claimed_base, grain_bits, tiles);
  for (uint32_t t = 0; t < tiles; ++t) {
    lane[t] = bucket_base + static_cast<std::size_t>((t - bucket_home + tiles) % tiles) * (grain / sizeof(NodeID));
    out[t] = claimed_base + static_cast<std::size_t>((t - claimed_home + tiles) % tiles) * (grain / sizeof(NodeID));
  }
  std::vector<int32_t> count(tiles);

  __champsim_start_trace();
  while (!queue.empty()) {
    TDStepOffloaded(g, parent, queue, lane.data(), count.data(), cap, out.data(), grain_bits, tiles);
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
  std::vector<char*> pass{argv[0]};
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--grain-bits" && i + 1 < argc) {
      grain_bits = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else if (a == "--tiles" && i + 1 < argc) {
      tiles = static_cast<uint32_t>(std::stoul(argv[++i]));
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
  NodeID* parent = DOBFS(g, source, grain_bits, tiles);
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
