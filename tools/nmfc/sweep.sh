#!/bin/bash
# Generate a workload pair and run both architectures over it.
#
# Both traces encode the same vertex visits, so each is run to end-of-trace and
# total cycles is the directly comparable number. Comparing IPC would be wrong:
# the NMFC host stream is far shorter for the same work, because the body is
# offloaded rather than retired on the host.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/nmfc_results}"
TILES="${TILES:-4}"
VERTICES="${VERTICES:-2000000}"
DEGREE="${DEGREE:-16}"
ROOTS="${ROOTS:-8000}"

mkdir -p "$OUT/traces" "$OUT/results"
cd "$ROOT"

generate () { # name partition locality
  ./tools/nmfc/nmfc_gen \
    --out-nmfc "$OUT/traces/$1.nmfc" --out-baseline "$OUT/traces/$1.champsimtrace" \
    --vertices "$VERTICES" --degree "$DEGREE" --roots "$ROOTS" --tiles "$TILES" \
    --graph kron --partition "$2" --locality "$3" --seed 1
}

generate scatter     stripe 0.0
generate partitioned silo   0.95

for workload in scatter partitioned; do
  for arch in nmfc baseline; do
    trace="$OUT/traces/$workload.nmfc"
    [ "$arch" = baseline ] && trace="$OUT/traces/$workload.champsimtrace"
    bin/champsim --config "config/nmfc/${arch}_${TILES}tile.json" \
      -w 1 -i 100000000 --json "$OUT/results/${workload}_${arch}.json" \
      "$trace" > "$OUT/results/${workload}_${arch}.txt" 2>&1
    echo "$workload/$arch done"
  done
done

python3 "$ROOT/tools/nmfc/report.py" "$OUT/results"
