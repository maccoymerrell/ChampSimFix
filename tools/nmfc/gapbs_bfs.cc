// Copyright (c) 2015, The Regents of the University of California (Regents)
// See ext/gapbs/LICENSE.txt for license details
//
// GAP Benchmark Suite BFS, with NMFC pseudo-compiler hooks.
//
// This is Scott Beamer's direction-optimizing BFS from ext/gapbs/src/bfs.cc,
// unchanged except for the marked `// NMFC:` blocks. Diff it against the
// original to see exactly what was added:
//
//     diff ext/gapbs/src/bfs.cc tools/nmfc/gapbs_bfs.cc
//
// It matters that this is the real kernel. A hand-written top-down BFS on a
// power-law graph spends its whole life in one hub's neighbour list; the
// direction-optimizing switch to a bottom-up sweep is precisely the mechanism
// that makes BFS on such a graph tractable, and a trace that lacks it is not a
// trace of BFS. The two phases also want *different* offload slicing, which is
// the reason both are instrumented:
//
//   TDStep  -- top-down: for each frontier vertex, scan its out-neighbours and
//              claim each one with a compare-and-swap. The claim is a genuine
//              read-modify-write, so this is the phase that exercises the
//              atomicity argument. Slicing is per vertex, or per edge chunk
//              when a hub would otherwise monopolise a context.
//
//   BUStep  -- bottom-up: for each *unvisited* vertex, scan its in-neighbours
//              until one is on the frontier, then stop. Invocations are short
//              and terminate early, so per-vertex slicing is the natural unit
//              and chunking would only add fabric traffic.
//
// Single-threaded: run with OMP_NUM_THREADS=1. A trace is a total order, and
// two threads interleaving their bodies would break the contiguity the reader
// relies on.

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <iostream>
#include <vector>

#include "benchmark.h"
#include "bitmap.h"
#include "builder.h"
#include "command_line.h"
#include "graph.h"
#include "platform_atomics.h"
#include "pvector.h"
#include "sliding_queue.h"
#include "timer.h"

// NMFC: the pseudo-compiler hooks.
#include "gapbs_hooks.h"
#include "partition.h"

using namespace std;

namespace
{
// NMFC: what the pseudo-compiler decided, threaded through to the kernels.
struct nmfc_options {
  std::int64_t chunk = 0;        // 0 = one invocation per vertex
  std::int64_t fork_window = 1;  // 1 = blocking call
  std::uint32_t tiles = 4;
  unsigned grain_bits = 21;
  std::string partition = "stripe";
  unsigned partition_passes = 3; // restreaming passes for --partition mincut
  std::int64_t budget = 20000;   // stop tracing after this many invocations
  bool relabelled = false;
  nmfc::part::result<NodeID> plan{};
};
nmfc_options g_nmfc;
std::vector<std::uint64_t> g_outstanding; // forked, not yet joined

/** Emit the joins for everything forked so far. */
void nmfc_drain_joins()
{
  auto& tracer = nmfc::gapbs::tracer::instance();
  for (const auto token : g_outstanding) {
    tracer.emit_join(token);
  }
  g_outstanding.clear();
}

/** Close an invocation, and join it now or later depending on the window. */
void nmfc_finish(std::uint64_t token)
{
  auto& tracer = nmfc::gapbs::tracer::instance();
  tracer.end_call(token);
  if (g_nmfc.fork_window <= 1) {
    return; // blocking call: the call itself is the wait
  }
  g_outstanding.push_back(token);
  if (static_cast<std::int64_t>(g_outstanding.size()) >= g_nmfc.fork_window) {
    nmfc_drain_joins();
  }
}
} // namespace

int64_t TDStep(const Graph &g, pvector<NodeID> &parent,
               SlidingQueue<NodeID> &queue) {
  int64_t scout_count = 0;
  auto& T = nmfc::gapbs::tracer::instance();           // NMFC
  namespace R = nmfc::gapbs;                           // NMFC
  #pragma omp parallel
  {
    QueueBuffer<NodeID> lqueue(queue);
    #pragma omp for reduction(+ : scout_count) nowait
    for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
      NodeID u = *q_iter;

      // NMFC: the host pops the next frontier entry, then offloads the scan.
      // The degree decides how many invocations that becomes: chunking exists
      // because one power-law hub would otherwise hold a single context for
      // the entire scan while every other context sits idle.
      T.host(&(*q_iter), R::R_VERTEX, R::R_VERTEX);
      const int64_t degree = g.out_degree(u);
      const int64_t per_call = (g_nmfc.chunk > 0) ? g_nmfc.chunk : std::max<int64_t>(degree, 1);
      int64_t in_chunk = 0;
      std::uint64_t token = T.begin_call(1, false, g_nmfc.fork_window > 1);
      T.body_load(&g.nmfc_out_index()[u], R::R_ROW_BEGIN, R::R_VERTEX);
      T.body_load(&g.nmfc_out_index()[u + 1], R::R_ROW_END, R::R_VERTEX);
      const NodeID* edge_base = g.out_neigh(u).begin();
      int64_t edge_index = 0;

      for (NodeID v : g.out_neigh(u)) {
        // NMFC: the neighbour id streams within the row; the parent slot is
        // the scattered, dependent access, and claiming it is a real
        // read-modify-write rather than a plain load.
        T.body_load(edge_base + edge_index, R::R_NEIGHBOUR, R::R_ROW_BEGIN, in_chunk > 0);
        T.body_atomic(&parent[v], R::R_VALUE, R::R_NEIGHBOUR);

        NodeID curr_val = parent[v];
        if (curr_val < 0) {
          if (compare_and_swap(parent[v], curr_val, u)) {
            lqueue.push_back(v);
            scout_count += -curr_val;
          }
        }

        ++edge_index;
        // NMFC: chunk boundary -- close this invocation and start another, so a
        // hub becomes many bounded invocations instead of one long one. The
        // next chunk's bounds arrive as arguments rather than being re-loaded:
        // a compiler splitting a loop hoists them, and re-reading them would
        // charge the model for work it would not do.
        if (++in_chunk >= per_call && edge_index < degree) {
          nmfc_finish(token);
          T.host(&(*q_iter), R::R_VERTEX, R::R_VERTEX);
          token = T.begin_call(1, false, g_nmfc.fork_window > 1);
          in_chunk = 0;
        }
      }
      nmfc_finish(token);                              // NMFC
    }
    lqueue.flush();
  }
  nmfc_drain_joins();                                  // NMFC: level barrier
  return scout_count;
}

int64_t BUStep(const Graph &g, pvector<NodeID> &parent, Bitmap &front,
               Bitmap &next) {
  int64_t awake_count = 0;
  next.reset();
  auto& T = nmfc::gapbs::tracer::instance();           // NMFC
  namespace R = nmfc::gapbs;                           // NMFC
  #pragma omp parallel for reduction(+ : awake_count) schedule(dynamic, 1024)
  for (NodeID u=0; u < g.num_nodes(); u++) {
    if (parent[u] < 0) {
      // NMFC: bottom-up invocations are short and stop at the first frontier
      // neighbour, so per-vertex is the natural slice and chunking would only
      // add fabric traffic for work that was about to end anyway.
      T.host(&parent[u], R::R_VERTEX, R::R_VERTEX);
      const std::uint64_t token = T.begin_call(2, false, g_nmfc.fork_window > 1);
      T.body_load(&g.nmfc_out_index()[u], R::R_ROW_BEGIN, R::R_VERTEX);
      const NodeID* edge_base = g.in_neigh(u).begin();
      int64_t edge_index = 0;

      for (NodeID v : g.in_neigh(u)) {
        T.body_load(edge_base + edge_index, R::R_NEIGHBOUR, R::R_ROW_BEGIN, edge_index > 0);
        T.body_load(&parent[v], R::R_VALUE, R::R_NEIGHBOUR);
        ++edge_index;
        if (front.get_bit(v)) {
          T.body_store(&parent[u], R::R_VALUE);        // NMFC: the claim
          parent[u] = v;
          awake_count++;
          next.set_bit(u);
          break;
        }
      }
      nmfc_finish(token);                              // NMFC
    }
  }
  nmfc_drain_joins();                                  // NMFC: level barrier
  return awake_count;
}

void QueueToBitmap(const SlidingQueue<NodeID> &queue, Bitmap &bm) {
  #pragma omp parallel for
  for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
    NodeID u = *q_iter;
    bm.set_bit_atomic(u);
  }
}

void BitmapToQueue(const Graph &g, const Bitmap &bm,
                   SlidingQueue<NodeID> &queue) {
  #pragma omp parallel
  {
    QueueBuffer<NodeID> lqueue(queue);
    #pragma omp for nowait
    for (NodeID n=0; n < g.num_nodes(); n++)
      if (bm.get_bit(n))
        lqueue.push_back(n);
    lqueue.flush();
  }
  queue.slide_window();
}

pvector<NodeID> InitParent(const Graph &g) {
  pvector<NodeID> parent(g.num_nodes());
  #pragma omp parallel for
  for (NodeID n=0; n < g.num_nodes(); n++)
    parent[n] = g.out_degree(n) != 0 ? -g.out_degree(n) : -1;
  return parent;
}

pvector<NodeID> DOBFS(const Graph &g, NodeID source, int alpha = 15,
                      int beta = 18) {
  pvector<NodeID> parent = InitParent(g);
  parent[source] = source;

  // NMFC: declare the parent array now that it exists. It is the scattered,
  // dependent access -- the one the design is about -- so its placement is the
  // most consequential decision the pass makes.
  {
    auto& tracer = nmfc::gapbs::tracer::instance();
    const auto vertices = static_cast<std::uint64_t>(g.num_nodes());
    const auto tiles = g_nmfc.tiles;
    std::function<std::uint32_t(std::uint64_t)> by_vertex;
    if (g_nmfc.relabelled) {
      const auto* const boundaries = &g_nmfc.plan.tile_begin;
      by_vertex = [boundaries](std::uint64_t offset) {
        const auto id = static_cast<std::int64_t>(offset / sizeof(NodeID));
        const auto it = std::upper_bound(std::begin(*boundaries), std::end(*boundaries), id);
        return static_cast<std::uint32_t>(std::distance(std::begin(*boundaries), it) - 1);
      };
    } else if (g_nmfc.partition == "block") {
      by_vertex = [vertices, tiles](std::uint64_t offset) {
        const auto vertex = offset / sizeof(NodeID);
        return static_cast<std::uint32_t>((vertex * tiles) / std::max<std::uint64_t>(vertices, 1));
      };
    }
    tracer.declare_region("parent", &parent[0], vertices * sizeof(NodeID), by_vertex);
  }
  SlidingQueue<NodeID> queue(g.num_nodes());
  queue.push_back(source);
  queue.slide_window();
  Bitmap curr(g.num_nodes());
  curr.reset();
  Bitmap front(g.num_nodes());
  front.reset();
  int64_t edges_to_check = g.num_edges_directed();
  int64_t scout_count = g.out_degree(source);
  while (!queue.empty()) {
    if (scout_count > edges_to_check / alpha) {
      int64_t awake_count, old_awake_count;
      QueueToBitmap(queue, front);
      awake_count = queue.size();
      queue.slide_window();
      do {
        old_awake_count = awake_count;
        awake_count = BUStep(g, parent, front, curr);
        front.swap(curr);
      } while ((awake_count >= old_awake_count) ||
               (awake_count > g.num_nodes() / beta));
      BitmapToQueue(g, front, queue);
      scout_count = 1;
    } else {
      edges_to_check -= scout_count;
      scout_count = TDStep(g, parent, queue);
      queue.slide_window();
    }
  }
  #pragma omp parallel for
  for (NodeID n = 0; n < g.num_nodes(); n++)
    if (parent[n] < -1)
      parent[n] = -1;
  return parent;
}

// NMFC: the placement pass. Under page-granularity interleaving a virtual
// address already names a tile, so placement is page assignment and nothing
// else -- which is why it can be applied to GAPBS's own arrays in place,
// without relaying or reordering anything.
//
//   stripe -- leave the natural layout. Contiguous arrays spread over every
//             channel, which is what you want for bandwidth and exactly wrong
//             for locality.
//   block  -- give each contiguous quarter of the vertex space to one tile.
//             The arrays are already ordered by vertex, so a block partition
//             needs no reordering; whether it *helps* depends entirely on
//             whether the graph has locality in vertex id.
void nmfc_declare_layout(const Graph& g)
{
  auto& tracer = nmfc::gapbs::tracer::instance();
  const auto vertices = static_cast<std::uint64_t>(g.num_nodes());
  const auto edges = static_cast<std::uint64_t>(g.num_edges_directed());
  const bool block = (g_nmfc.partition == "block");
  const bool mincut = g_nmfc.relabelled;
  const auto tiles = g_nmfc.tiles;

  // After relabelling each tile owns a contiguous id range, so "which tile owns
  // this vertex" is a search over the boundaries rather than a division.
  const auto* const boundaries = &g_nmfc.plan.tile_begin;
  const auto tile_of_id = [boundaries](std::int64_t id) {
    const auto it = std::upper_bound(std::begin(*boundaries), std::end(*boundaries), id);
    return static_cast<std::uint32_t>(std::distance(std::begin(*boundaries), it) - 1);
  };

  using owner_fn = std::function<std::uint32_t(std::uint64_t)>;
  const owner_fn none{};

  const owner_fn by_vertex = [vertices, tiles](std::uint64_t offset) {
    const auto vertex = offset / sizeof(NodeID*);
    return static_cast<std::uint32_t>((vertex * tiles) / std::max<std::uint64_t>(vertices, 1));
  };
  const owner_fn by_parent_slot = [vertices, tiles](std::uint64_t offset) {
    const auto vertex = offset / sizeof(NodeID);
    return static_cast<std::uint32_t>((vertex * tiles) / std::max<std::uint64_t>(vertices, 1));
  };
  const owner_fn by_edge = [edges, tiles](std::uint64_t offset) {
    const auto edge = offset / sizeof(NodeID);
    return static_cast<std::uint32_t>((edge * tiles) / std::max<std::uint64_t>(edges, 1));
  };

  // A vertex's adjacency must live on the vertex's own tile, or scanning a
  // neighbour list migrates before it has learned anything. Under relabelling
  // the edge ranges are contiguous per tile, so the owner of an edge offset is
  // the owner of the vertex whose list contains it.
  const auto* const* new_index = g.nmfc_out_index();
  const owner_fn by_owning_vertex = [new_index, vertices, tile_of_id](std::uint64_t offset) {
    const auto edge = static_cast<std::int64_t>(offset / sizeof(NodeID));
    const auto* target = new_index[0] + edge;
    const auto it = std::upper_bound(new_index, new_index + vertices + 1, target);
    return tile_of_id(std::distance(new_index, it) - 1);
  };
  const owner_fn by_new_vertex = [tile_of_id](std::uint64_t offset) { return tile_of_id(static_cast<std::int64_t>(offset / sizeof(NodeID*))); };
  const owner_fn by_new_parent = [tile_of_id](std::uint64_t offset) { return tile_of_id(static_cast<std::int64_t>(offset / sizeof(NodeID))); };

  tracer.declare_region("out_index", g.nmfc_out_index(), (vertices + 1) * sizeof(NodeID*), mincut ? by_new_vertex : (block ? by_vertex : none));
  tracer.declare_region("out_neighbors", g.nmfc_out_neighbors(), edges * sizeof(NodeID), mincut ? by_owning_vertex : (block ? by_edge : none));
  (void)by_parent_slot;
  // One grain, not one per tile: the OS aliases this virtual page to a copy on
  // every channel, so there is nothing here for the compiler to lay out.
  tracer.declare_code("code", reinterpret_cast<const void*>(nmfc::gapbs::FUNC_CODE_BASE), std::uint64_t{1} << g_nmfc.grain_bits);
}

/**
 * Rebuild the graph under a partition's relabelling.
 *
 * Isomorphic to the original -- BFS visits the same structure and returns the
 * same tree -- but each tile's vertices now occupy a contiguous id range, so a
 * block layout places them and a vertex's adjacency list sits with its own
 * vertex. That is the whole point: the assignment becomes an address.
 */
Graph nmfc_relabel(const Graph& g, const nmfc::part::result<NodeID>& p)
{
  const auto n = g.num_nodes();
  const auto m = g.num_edges_directed();
  auto** index = new NodeID*[static_cast<std::size_t>(n) + 1];
  auto* neighs = new NodeID[static_cast<std::size_t>(m)];

  const auto* const* old_index = g.nmfc_out_index();
  std::int64_t cursor = 0;
  for (std::int64_t i = 0; i < n; ++i) {
    index[i] = neighs + cursor;
    const auto v = p.new_to_old[static_cast<std::size_t>(i)];
    for (auto it = old_index[v]; it != old_index[v + 1]; ++it) {
      neighs[cursor++] = p.old_to_new[static_cast<std::size_t>(*it)];
    }
    // Sorted adjacency keeps the sequential scan a function core does over a
    // neighbour list actually sequential.
    std::sort(index[i], neighs + cursor);
  }
  index[n] = neighs + cursor;
  return Graph(n, index, neighs);
}

int main(int argc, char* argv[]) {
  // NMFC: pull our own flags out first, so GAPBS's command line is unchanged.
  std::string out_nmfc = "gapbs_bfs.nmfc";
  std::string out_baseline = "gapbs_bfs.champsimtrace";
  std::vector<char*> passthrough{argv[0]};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string{}; };
    if (arg == "--out-nmfc") { out_nmfc = next(); }
    else if (arg == "--out-baseline") { out_baseline = next(); }
    else if (arg == "--tiles") { g_nmfc.tiles = static_cast<std::uint32_t>(std::stoul(next())); }
    else if (arg == "--grain-bits") { g_nmfc.grain_bits = static_cast<unsigned>(std::stoul(next())); }
    else if (arg == "--partition") { g_nmfc.partition = next(); }
    else if (arg == "--partition-passes") { g_nmfc.partition_passes = static_cast<unsigned>(std::stoul(next())); }
    else if (arg == "--chunk") { g_nmfc.chunk = std::stoll(next()); }
    else if (arg == "--fork-window") { g_nmfc.fork_window = std::stoll(next()); }
    else if (arg == "--budget") { g_nmfc.budget = std::stoll(next()); }
    else { passthrough.push_back(argv[i]); }
  }

  CLApp cli(static_cast<int>(passthrough.size()), passthrough.data(), "nmfc-gapbs-bfs");
  if (!cli.ParseArgs())
    return -1;
  Builder b(cli);
  Graph g = b.MakeGraph();
  std::cerr << "nmfc: graph " << g.num_nodes() << " vertices, " << g.num_edges_directed() << " directed edges\n";

  // NMFC: balanced minimum edge cut, then relabel so the cut is expressible as
  // an address. Every cut edge is a potential migration, so this is the pass
  // that decides how much the fabric has to carry.
  NodeID source = cli.start_vertex();
  if (source < 0) {
    source = 0;
    const auto* const* idx0 = g.nmfc_out_index();
    while (source + 1 < g.num_nodes() && idx0[source + 1] == idx0[source]) {
      ++source; // skip isolated vertices
    }
  }

  if (g_nmfc.partition == "mincut") {
    g_nmfc.plan = nmfc::part::build<NodeID>(g.nmfc_out_index(), g.num_nodes(), g_nmfc.tiles, g_nmfc.partition_passes);
    g = nmfc_relabel(g, g_nmfc.plan);
    g_nmfc.relabelled = true;
    source = g_nmfc.plan.old_to_new[static_cast<std::size_t>(source)];
  }

  auto& tracer = nmfc::gapbs::tracer::instance();       // NMFC
  tracer.open(out_nmfc, g_nmfc.tiles, g_nmfc.grain_bits);
  tracer.open_baseline(out_baseline);
  tracer.set_budget(g_nmfc.budget);
  nmfc_declare_layout(g); // parent is declared inside DOBFS, where it exists

  // The source must name the same *vertex* in both layouts, or the NMFC trace
  // and its baseline describe different traversals and nothing compares.
  std::cerr << "nmfc: bfs source " << source << " (degree " << g.out_degree(source) << ")\n";
  DOBFS(g, source);

  nmfc_drain_joins();                                   // NMFC
  tracer.close();
  std::cerr << "nmfc: slice=" << (g_nmfc.chunk > 0 ? "edge-chunk" : "vertex")
            << " chunk=" << g_nmfc.chunk
            << " partition=" << g_nmfc.partition
            << " fork-window=" << g_nmfc.fork_window << "\n";
  return 0;
}
