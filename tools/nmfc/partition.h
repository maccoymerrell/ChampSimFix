/*
 * Min-cut partitioning and relabelling for the NMFC placement pass.
 *
 * Placement decides which memory tile owns a vertex. Every edge whose endpoints
 * land on different tiles is a *potential migration*: an invocation walking it
 * has to pick itself up and cross the fabric. So the placement question is
 * exactly balanced minimum edge cut -- equal work per channel, as few crossings
 * as possible -- and on a graph with no locality in vertex id (kron being the
 * standard example) neither a block nor a striped layout expresses anything at
 * all about which vertices belong together.
 *
 * Two things have to happen for a partition to become a layout:
 *
 *   1. Assign vertices to tiles.  Restreaming linear deterministic greedy
 *      (LDG): each vertex goes to whichever tile already holds most of its
 *      neighbours, damped by how full that tile is. One pass is O(E); running
 *      several lets later decisions see earlier ones, which is most of what
 *      separates streaming partitioners from random assignment on power-law
 *      graphs. It is not METIS -- it is one pass over the edges with no
 *      coarsening -- but it needs no dependency and it scales to the sizes
 *      that make this architecture interesting.
 *
 *   2. *Relabel* so the assignment is expressible as an address.  Placement
 *      granularity is a 2 MiB grain -- 524,288 vertices of parent[] -- so a
 *      per-vertex assignment cannot be honoured by scattering individual
 *      vertices. Giving each tile a contiguous range of new vertex ids makes
 *      the assignment fall out of a block layout for free, and makes a
 *      vertex's adjacency list contiguous with its own tile's range.
 *
 * The relabelled graph is isomorphic to the original, so the kernel's results
 * are unchanged; only the addresses it touches move.
 */

#ifndef NMFC_PARTITION_H
#define NMFC_PARTITION_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace nmfc::part
{

/** A partition, plus the relabelling that turns it into an address layout. */
template <typename NodeID_>
struct result {
  std::vector<std::uint32_t> owner;    ///< original vertex id -> tile
  std::vector<NodeID_> old_to_new;     ///< original id -> relabelled id
  std::vector<NodeID_> new_to_old;     ///< relabelled id -> original id
  std::vector<std::int64_t> tile_begin; ///< tiles+1 boundaries, in relabelled ids
  std::uint64_t cut_edges = 0;
  std::uint64_t total_edges = 0;

  [[nodiscard]] double cut_fraction() const { return total_edges == 0 ? 0.0 : double(cut_edges) / double(total_edges); }
};

/**
 * Restreaming LDG.
 *
 * Score for placing v on tile t is |N(v) inside t| * (1 - size_t/capacity).
 * The first factor is the cut objective; the second is the balance constraint,
 * and it is what stops a power-law graph from collapsing every hub -- and then
 * everything a hub touches -- onto one tile.
 */
template <typename NodeID_, typename Index_>
result<NodeID_> build(Index_ index, std::int64_t num_nodes, std::uint32_t tiles, unsigned passes)
{
  result<NodeID_> out;
  out.owner.assign(static_cast<std::size_t>(num_nodes), 0);
  // A hard n/tiles cap makes the balance term dominate every decision, which
  // is a balanced partition that has stopped listening to the graph.
  const double slack = 1.05;
  const auto capacity = slack * static_cast<double>(num_nodes) / double(tiles);

  // Seed with a block assignment, so the first refining pass has something
  // to agree or disagree with rather than an empty machine.
  for (std::int64_t v = 0; v < num_nodes; ++v) {
    out.owner[static_cast<std::size_t>(v)] = static_cast<std::uint32_t>((v * tiles) / std::max<std::int64_t>(num_nodes, 1));
  }

  std::vector<std::int64_t> size(tiles, 0);
  std::vector<double> score(tiles, 0.0);
  std::vector<std::uint32_t> best_owner;
  std::uint64_t best_cut = ~std::uint64_t{0};

  for (unsigned pass = 0; pass < passes; ++pass) {
    std::fill(std::begin(size), std::end(size), 0);
    for (std::int64_t v = 0; v < num_nodes; ++v) {
      std::fill(std::begin(score), std::end(score), 0.0);
      for (auto it = index[v]; it != index[v + 1]; ++it) {
        score[out.owner[static_cast<std::size_t>(*it)]] += 1.0;
      }

      std::uint32_t best = 0;
      double best_score = -1.0;
      for (std::uint32_t t = 0; t < tiles; ++t) {
        const auto weighted = score[t] * (1.0 - double(size[t]) / capacity);
        // Ties go to the emptier tile, which keeps a zero-degree run balanced
        // instead of piling it all on tile 0.
        if (weighted > best_score || (weighted == best_score && size[t] < size[best])) {
          best_score = weighted;
          best = t;
        }
      }
      out.owner[static_cast<std::size_t>(v)] = best;
      ++size[best];
    }

    std::uint64_t cut = 0;
    std::uint64_t total = 0;
    for (std::int64_t v = 0; v < num_nodes; ++v) {
      for (auto it = index[v]; it != index[v + 1]; ++it) {
        ++total;
        if (out.owner[static_cast<std::size_t>(*it)] != out.owner[static_cast<std::size_t>(v)]) {
          ++cut;
        }
      }
    }
    out.total_edges = total;
    if (cut < best_cut) {
      best_cut = cut;
      best_owner = out.owner;
    }
    std::fprintf(stderr, "nmfc: partition pass %u -- edge cut %.1f%%, sizes", pass + 1, 100.0 * double(cut) / double(std::max<std::uint64_t>(total, 1)));
    for (std::uint32_t t = 0; t < tiles; ++t) {
      std::fprintf(stderr, " %ld", static_cast<long>(size[t]));
    }
    std::fprintf(stderr, "\n");
  }
  out.owner = best_owner;
  out.cut_edges = best_cut;
  std::fprintf(stderr, "nmfc: partition keeping best pass -- edge cut %.1f%%\n", 100.0 * out.cut_fraction());

  // Contiguous id ranges per tile: this is the step that turns an assignment
  // into something a block layout can express.
  out.tile_begin.assign(tiles + 1, 0);
  for (std::int64_t v = 0; v < num_nodes; ++v) {
    ++out.tile_begin[out.owner[static_cast<std::size_t>(v)] + 1];
  }
  for (std::uint32_t t = 0; t < tiles; ++t) {
    out.tile_begin[t + 1] += out.tile_begin[t];
  }

  out.old_to_new.assign(static_cast<std::size_t>(num_nodes), 0);
  out.new_to_old.assign(static_cast<std::size_t>(num_nodes), 0);
  auto cursor = out.tile_begin;
  for (std::int64_t v = 0; v < num_nodes; ++v) {
    const auto slot = cursor[out.owner[static_cast<std::size_t>(v)]]++;
    out.old_to_new[static_cast<std::size_t>(v)] = static_cast<NodeID_>(slot);
    out.new_to_old[static_cast<std::size_t>(slot)] = static_cast<NodeID_>(v);
  }
  return out;
}

} // namespace nmfc::part

#endif // NMFC_PARTITION_H
