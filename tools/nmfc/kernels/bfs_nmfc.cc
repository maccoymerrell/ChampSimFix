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
static constexpr uint32_t NMFC_TILES = 4;
static_assert((NMFC_TILES & (NMFC_TILES - 1)) == 0, "tile count must be a power of two for the mask to be exact");

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
/** One claim, always inlined: the helper exists for legibility, not as a call. */
static inline __attribute__((always_inline)) uint32_t claim_one(NodeID* parent, NodeID u, NodeID v, uint32_t bit)
{
  if (v < 0) {
    return 0; // an unused slot in a short group
  }
  const NodeID curr = parent[v];
  return (curr < 0 && compare_and_swap(parent[v], curr, u)) ? bit : 0;
}

NMFC_FUNCTION
uint32_t nmfc_claim6(NodeID* parent, NodeID u, uint64_t a, uint64_t b, uint64_t c)
{
  // Unrolled so each packed word dies as soon as it is consumed. Written as a
  // loop over an index instead, the compiler kept all three live across every
  // iteration, needed a ninth register and built a stack frame to spill into --
  // on a machine that has no stack.
  uint32_t got = 0;
  got |= claim_one(parent, u, static_cast<NodeID>(a & 0xffffffffULL), 1u << 0);
  got |= claim_one(parent, u, static_cast<NodeID>(a >> 32), 1u << 1);
  got |= claim_one(parent, u, static_cast<NodeID>(b & 0xffffffffULL), 1u << 2);
  got |= claim_one(parent, u, static_cast<NodeID>(b >> 32), 1u << 3);
  got |= claim_one(parent, u, static_cast<NodeID>(c & 0xffffffffULL), 1u << 4);
  got |= claim_one(parent, u, static_cast<NodeID>(c >> 32), 1u << 5);
  return got;
}

/** Which tile owns an address, under the congruent mapping the simulator uses. */
static inline uint32_t tile_of(const void* p)
{
  return static_cast<uint32_t>((reinterpret_cast<uintptr_t>(p) >> NMFC_GRAIN_BITS) & (NMFC_TILES - 1));
}

static constexpr int GROUP = 6;

/**
 * How many invocations the host tries to keep outstanding.
 *
 * A fork does not wait; a join does. Written as `got = claim(...)` with the
 * result used on the next line, the two collapse and the machine holds exactly
 * one invocation while having room for a thousand. Issuing a window of
 * independent calls first and consuming their results afterwards is what lets
 * the reorder buffer carry several at once.
 */
static constexpr int WINDOW = 64;

struct pending_group {
  NodeID u;
  uint64_t a, b, c;
  NodeID v[GROUP];
};

/**
 * The host walks the frontier and the edge lists itself.
 *
 * That is deliberate: the scan is sequential and prefetchable, which an
 * out-of-order core with a memory hierarchy already handles well. What it
 * handles badly is the scattered read-modify-write on parent[], and that is
 * what goes near the memory. Grouping by owning tile is a shift and a mask per
 * neighbour.
 */
static int64_t TDStepOffloaded(const Graph& g, NodeID* parent, SlidingQueue<NodeID>& queue)
{
  int64_t scout_count = 0;
  QueueBuffer<NodeID> lqueue(queue);
  NodeID stage[NMFC_TILES][GROUP];
  int32_t n[NMFC_TILES];
  pending_group win[WINDOW];
  uint32_t res[WINDOW];
  int nw = 0;

  const auto drain = [&]() {
    // Issue first, consume second. Nothing in this loop depends on the result
    // of the previous call, so they can all be in flight together.
    for (int i = 0; i < nw; ++i) {
      res[i] = nmfc_claim6(parent, win[i].u, win[i].a, win[i].b, win[i].c);
    }
    for (int i = 0; i < nw; ++i) {
      for (int k = 0; k < GROUP; ++k) {
        if ((res[i] & (1u << k)) != 0) {
          lqueue.push_back(win[i].v[k]);
          scout_count += g.out_degree(win[i].v[k]);
        }
      }
    }
    nw = 0;
  };

  const auto submit = [&](uint32_t t, NodeID u) {
    pending_group& gp = win[nw];
    gp.u = u;
    for (int i = 0; i < GROUP; ++i) {
      gp.v[i] = (i < n[t]) ? stage[t][i] : -1;
    }
    const auto pack = [&](int i) {
      return (static_cast<uint64_t>(static_cast<uint32_t>(gp.v[i + 1])) << 32) | static_cast<uint32_t>(gp.v[i]);
    };
    gp.a = pack(0);
    gp.b = pack(2);
    gp.c = pack(4);
    n[t] = 0;
    if (++nw == WINDOW) {
      drain();
    }
  };

  for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
    const NodeID u = *q_iter;
    for (uint32_t t = 0; t < NMFC_TILES; ++t) {
      n[t] = 0;
    }
    auto neigh = g.out_neigh(u);
    for (const NodeID* p = neigh.begin(); p != neigh.end(); ++p) {
      const NodeID v = *p;
      const uint32_t t = tile_of(&parent[v]);
      stage[t][n[t]++] = v;
      if (n[t] == GROUP) {
        submit(t, u);
      }
    }
    for (uint32_t t = 0; t < NMFC_TILES; ++t) {
      if (n[t] != 0) {
        submit(t, u);
      }
    }
  }
  drain();
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

  write_manifest(manifest, {
    {"index", (const void*)g.nmfc_out_index(), (static_cast<std::size_t>(g.num_nodes()) + 1) * sizeof(void*)},
    {"neighbors", (const void*)g.nmfc_out_neighbors(), static_cast<std::size_t>(g.num_edges_directed()) * sizeof(NodeID)},
    {"parent", parent, static_cast<std::size_t>(g.num_nodes()) * sizeof(NodeID)},
  });

  __champsim_start_trace();
  while (!queue.empty()) {
    TDStepOffloaded(g, parent, queue);
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
