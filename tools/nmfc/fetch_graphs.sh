#!/bin/bash
# Fetch the GAP suite's real-world graphs and convert them to the serialized
# form the kernels read.
#
# kron and urand generate locally and neither has any locality: kron is
# power-law with no relationship between vertex id and structure, urand is
# uniform random by construction. They are the cases where a placement policy
# provably cannot help, which makes them good at falsifying one and useless for
# validating one.
#
# road is the opposite -- a planar graph whose vertices really do have
# neighbourhoods -- so it is the workload where placement has something to find.
# roadU is the symmetrized form BFS wants.
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
G="$ROOT/ext/gapbs"
RAW="$G/benchmark/graphs/raw"
OUT="$G/benchmark/graphs"
mkdir -p "$RAW" "$OUT"

if [ ! -f "$RAW/USA-road-d.USA.gr" ]; then
  [ -f "$RAW/USA-road-d.USA.gr.gz" ] || \
    wget -q -P "$RAW" http://www.dis.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.USA.gr.gz
  gunzip -c "$RAW/USA-road-d.USA.gr.gz" > "$RAW/USA-road-d.USA.gr"
fi
[ -f "$OUT/roadU.sg" ] || (cd "$G" && ./converter -sf "$RAW/USA-road-d.USA.gr" -b "$OUT/roadU.sg")
echo "road ready: $OUT/roadU.sg"
