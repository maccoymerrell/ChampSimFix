#!/bin/bash
# Clone and build ramulator2 for the NMFC memory-controller model.
#
# Two local wrinkles this works around, neither of them ramulator2's fault:
#   - its CMake needs Python development headers for the nanobind bindings, and
#     the system python has none, so we point it at a python that does;
#   - it runs clang-format over its generated C++, and clang-format 13 rejects
#     the InsertBraces key its .clang-format uses. The step is guarded by
#     if(CLANG_FORMAT), so telling CMake it is absent skips it and leaves the
#     vendored tree unpatched. It is cosmetic; nothing about the library changes.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EXT="$ROOT/ext"
PYTHON="${NMFC_PYTHON:-$(command -v python3)}"

mkdir -p "$EXT"
if [ ! -d "$EXT/ramulator2" ]; then
  git clone --depth 1 https://github.com/CMU-SAFARI/ramulator2.git "$EXT/ramulator2"
fi

cd "$EXT/ramulator2"
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DPython_EXECUTABLE="$PYTHON" \
  -DCLANG_FORMAT=CLANG_FORMAT-NOTFOUND
cmake --build build -j "$(nproc)"

echo "built: $EXT/ramulator2/libramulator.so"
