#!/bin/bash
# Produce a trace and everything needed to interpret it, together.
#
# The symbol addresses, the atomic instruction sites and the region manifest
# describe one particular build. Re-deriving them later from a rebuilt binary
# silently describes a different program -- rebuilding to add one line moved
# every symbol once already, and the addresses looked perfectly valid while
# naming nothing in the trace. So everything is captured in one run, into one
# directory, and the binary itself is kept beside it.
set -euo pipefail

PIN_ROOT=${PIN_ROOT:-/mnt/md0/PIN/pin-external-4.2-99776-g21d818fa2-gcc-linux}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRACER="$HERE/../../../tracer/pin/obj-intel64/champsim_tracer.so"

OUT=${1:?usage: trace.sh <outdir> [bfs_nmfc args...]}
shift
mkdir -p "$OUT"

make -C "$HERE" >/dev/null
cp "$HERE/bfs_nmfc" "$OUT/bfs_nmfc"

# Offloadable functions: name, start, size. Mangled names carry the signature,
# so match on the demangled form and keep the address from the same line.
readelf -sW "$OUT/bfs_nmfc" | awk '$4=="FUNC" && $8 ~ /^_Z[0-9]+nmfc_/ {printf "%s %s %s\n", $8, $2, $3}' > "$OUT/symbols.txt"

# Atomic sites inside those functions, taken from the disassembly rather than
# assumed: an instruction is atomic because it is locked, not because we think
# the source said so.
objdump -d --no-show-raw-insn "$OUT/bfs_nmfc" | awk '/^ *[0-9a-f]+:.*lock/ {gsub(":","",$1); print $1}' > "$OUT/atomics.txt"

export OMP_NUM_THREADS=1
setarch -R "$PIN_ROOT/pin" -t "$TRACER" \
  -o "$OUT/trace.champsimtrace" -t ${NMFC_TRACE_INSTRS:-200000000} \
  -start_symbol __champsim_start_trace -stop_symbol __champsim_stop_trace \
  -- "$OUT/bfs_nmfc" --manifest "$OUT/regions.txt" "$@"

echo "--- captured in $OUT ---"
wc -l < "$OUT/symbols.txt" | xargs echo "  offloadable functions:"
wc -l < "$OUT/atomics.txt" | xargs echo "  atomic sites:"
wc -l < "$OUT/regions.txt" | xargs echo "  regions:"
ls -la "$OUT/trace.champsimtrace" | awk '{print "  trace bytes:", $5}'
