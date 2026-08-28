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

/**
 * Geometry is compile-time, and that is a correctness requirement rather than
 * a tuning choice.
 *
 * Passed as runtime arguments, the tile count made the compiler emit a
 * hardware divide in the inner loop -- once per neighbour -- and pushed the
 * parameter list past the six registers the ABI passes in, so two arguments
 * arrived on the stack and three callee-saved registers were spilled to make
 * room. A function core has a regfile and no stack, so that function could not
 * have run on the machine it was written for. As constants the modulo is a
 * mask and the whole working set stays in registers.
 */
static constexpr uint32_t NMFC_GRAIN_BITS = 20;
static constexpr uint32_t NMFC_TILES = 4;
static constexpr int32_t NMFC_CAP = static_cast<int32_t>((std::size_t{1} << NMFC_GRAIN_BITS) / sizeof(NodeID));
static_assert((NMFC_TILES & (NMFC_TILES - 1)) == 0, "tile count must be a power of two for the mask to be exact");

/** Which tile owns an address, under the congruent mapping the simulator uses. */
static inline uint32_t nmfc_tile_of(const void* p)
{
  return static_cast<uint32_t>((reinterpret_cast<uintptr_t>(p) >> NMFC_GRAIN_BITS) & (NMFC_TILES - 1));
}

/**
 * Sort u's neighbours into per-tile buckets by the tile owning parent[v].
 * `bucket` is `tiles` lanes of `cap` entries; `count` is per-lane occupancy.
 */
NMFC_FUNCTION
const NodeID* nmfc_scan(const NodeID* first, const NodeID* last, const NodeID* parent, NodeID* const* lane)
{
  for (const NodeID* p = first; p != last; ++p) {
    const NodeID v = *p;
    NodeID* l = lane[nmfc_tile_of(&parent[v])];
    const NodeID n = *l; // a lane carries its own occupancy in its first word
    if (n == NMFC_CAP - 1) {
      return p; // this lane is full; the caller drains and resumes here
    }
    l[n + 1] = v;
    *l = n + 1;
  }
  return last;
}

/**
 * Claim every vertex in one bucket, compacting the winners to its front.
 *
 * Four arguments, not five, and the result overwrites the input rather than
 * going to a second buffer. That is a register-budget decision: a function
 * core has eight registers and no stack, and five arguments plus a return
 * value plus the loop's temporaries needed nine. All of `parent` touched here
 * lives on this function's own tile, so this loop does not migrate.
 */
NMFC_FUNCTION
int32_t nmfc_claim(NodeID* bucket, int32_t n, NodeID* parent, NodeID u)
{
  NodeID* w = bucket;
  for (NodeID* p = bucket, *e = bucket + n; p != e; ++p) {
    const NodeID v = *p;
    NodeID& slot = parent[v];
    const NodeID curr = slot;
    if (curr < 0 && compare_and_swap(slot, curr, u)) {
      *w++ = v;
    }
  }
  return static_cast<int32_t>(w - bucket);
}

static int64_t TDStepOffloaded(const Graph& g, NodeID* parent, SlidingQueue<NodeID>& queue, NodeID* const* lane)
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
      for (uint32_t t = 0; t < NMFC_TILES; ++t) {
        *lane[t] = 0;
      }
      // Producer: streams the edge list, sorts neighbours by owning tile.
      p = nmfc_scan(p, last, parent, lane);
      // Consumers: one per tile, each entirely local to that tile.
      for (uint32_t t = 0; t < NMFC_TILES; ++t) {
        const int32_t n = *lane[t];
        if (n == 0) {
          continue;
        }
        const int32_t got = nmfc_claim(lane[t] + 1, n, parent, u);
        for (int32_t i = 0; i < got; ++i) {
          lqueue.push_back(lane[t][1 + i]);
          scout_count += g.out_degree(lane[t][1 + i]);
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

  // The consumer reads its bucket and updates parent entries. Both must be on
  // its own tile or it ping-pongs once per element -- which is what the first
  // version of this split did, changing tile every five instructions despite
  // being written to never migrate. So each lane is placed on the tile it
  // serves: one grain per lane, ordered so lane t lands on tile t.
  const std::size_t grain = std::size_t{1} << NMFC_GRAIN_BITS;
  NodeID* bucket_base = nullptr;
  NodeID* claimed_base = nullptr;
  if (posix_memalign(reinterpret_cast<void**>(&bucket_base), grain, grain * tiles) != 0 ||
      posix_memalign(reinterpret_cast<void**>(&claimed_base), grain, grain * tiles) != 0) {
    std::fprintf(stderr, "nmfc: cannot allocate tile-local scratch\n");
    std::exit(1);
  }
  // Lane order is rotated so that lane t is the grain whose tile number is t.
  std::vector<NodeID*> lane(tiles), out(tiles);
  const uint32_t bucket_home = nmfc_tile_of(bucket_base);
  const uint32_t claimed_home = nmfc_tile_of(claimed_base);
  for (uint32_t t = 0; t < tiles; ++t) {
    lane[t] = bucket_base + static_cast<std::size_t>((t - bucket_home + tiles) % tiles) * (grain / sizeof(NodeID));
    out[t] = claimed_base + static_cast<std::size_t>((t - claimed_home + tiles) % tiles) * (grain / sizeof(NodeID));
  }
  std::vector<int32_t> count(tiles);

  write_manifest(manifest, {
    {"index", (const void*)g.nmfc_out_index(), (static_cast<std::size_t>(g.num_nodes()) + 1) * sizeof(void*)},
    {"neighbors", (const void*)g.nmfc_out_neighbors(), static_cast<std::size_t>(g.num_edges_directed()) * sizeof(NodeID)},
    {"parent", parent, static_cast<std::size_t>(g.num_nodes()) * sizeof(NodeID)},
    {"bucket", bucket_base, grain * tiles},
    {"claimed", claimed_base, grain * tiles},
  });

  __champsim_start_trace();
  while (!queue.empty()) {
    TDStepOffloaded(g, parent, queue, lane.data());
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
