#!/bin/bash
# Clone the GAP Benchmark Suite and expose the one thing its kernels need in
# order to be instrumented.
#
# GAPBS keeps its CSR row index private, and a traced kernel has to record the
# address it loads a vertex's row bounds from -- that address is the whole
# point, since it is what the placement pass then assigns to a tile. So this
# adds two const accessors and nothing else. The kernels are untouched; the
# instrumented BFS lives in tools/nmfc/gapbs_bfs.cc beside its original, so
#
#     diff ext/gapbs/src/bfs.cc tools/nmfc/gapbs_bfs.cc
#
# shows exactly what the hooks added and nothing more.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EXT="$ROOT/ext"

mkdir -p "$EXT"
if [ ! -d "$EXT/gapbs" ]; then
  git clone --depth 1 https://github.com/sbeamer/gapbs.git "$EXT/gapbs"
fi

python3 "$ROOT/tools/nmfc/patch_gapbs.py" "$EXT/gapbs/src/graph.h"
echo "gapbs ready at $EXT/gapbs"
