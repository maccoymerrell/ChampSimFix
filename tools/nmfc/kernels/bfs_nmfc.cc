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

extern "C" {
__attribute__((noinline, used)) void __champsim_start_trace(void) { asm volatile(""); }
__attribute__((noinline, used)) void __champsim_stop_trace(void) { asm volatile(""); }
}

/**
 * One offloaded function: claim every unvisited neighbour of `u`.
 *
 * Returns the number of vertices claimed, writing them to `claimed`. The
 * caller owns that buffer; the function does not allocate, because a function
 * core has a regfile and no stack.
 */
NMFC_FUNCTION
int32_t nmfc_expand(const NodeID* first, const NodeID* last, NodeID* parent, NodeID u, NodeID* claimed)
{
  int32_t n = 0;
  for (const NodeID* p = first; p != last; ++p) {
    const NodeID v = *p;
    const NodeID curr = parent[v];
    if (curr < 0 && compare_and_swap(parent[v], curr, u)) {
      claimed[n++] = v;
    }
  }
  return n;
}

static int64_t TDStepOffloaded(const Graph& g, pvector<NodeID>& parent, SlidingQueue<NodeID>& queue, std::vector<NodeID>& scratch)
{
  int64_t scout_count = 0;
  QueueBuffer<NodeID> lqueue(queue);
  for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
    const NodeID u = *q_iter;
    auto neigh = g.out_neigh(u);
    const NodeID* first = neigh.begin();
    const NodeID* last = neigh.end();
    if (static_cast<std::size_t>(last - first) > scratch.size()) {
      scratch.resize(static_cast<std::size_t>(last - first));
    }
    const int32_t n = nmfc_expand(first, last, parent.data(), u, scratch.data());
    for (int32_t i = 0; i < n; ++i) {
      lqueue.push_back(scratch[i]);
      scout_count += g.out_degree(scratch[i]);
    }
  }
  lqueue.flush();
  return scout_count;
}

static pvector<NodeID> InitParent(const Graph& g)
{
  pvector<NodeID> parent(g.num_nodes());
  for (NodeID n = 0; n < g.num_nodes(); n++) {
    parent[n] = g.out_degree(n) != 0 ? -g.out_degree(n) : -1;
  }
  return parent;
}

static pvector<NodeID> DOBFS(const Graph& g, NodeID source)
{
  pvector<NodeID> parent = InitParent(g);
  parent[source] = source;
  SlidingQueue<NodeID> queue(g.num_nodes());
  queue.push_back(source);
  queue.slide_window();
  std::vector<NodeID> scratch(1024);

  __champsim_start_trace();
  while (!queue.empty()) {
    TDStepOffloaded(g, parent, queue, scratch);
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
  CLApp cli(argc, argv, "nmfc-bfs");
  if (!cli.ParseArgs()) {
    return -1;
  }
  Builder b(cli);
  Graph g = b.MakeGraph();
  SourcePicker<Graph> sp(g, cli.start_vertex());
  const NodeID source = sp.PickNext();
  std::fprintf(stderr, "nmfc: %ld vertices, %ld edges, source %d\n", (long)g.num_nodes(), (long)g.num_edges_directed(), (int)source);
  pvector<NodeID> parent = DOBFS(g, source);
  int64_t reached = 0;
  for (NodeID n = 0; n < g.num_nodes(); n++) {
    if (parent[n] >= 0) {
      ++reached;
    }
  }
  std::fprintf(stderr, "nmfc: reached %ld vertices\n", (long)reached);
  return 0;
}
