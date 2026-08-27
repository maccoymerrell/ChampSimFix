#!/usr/bin/env python3
"""
Expose GAPBS's CSR row index so a kernel can be instrumented against it.

An instrumented kernel records the *address* each load reads from, and those
addresses are what the placement pass assigns to tiles. GAPBS keeps out_index_
and out_neighbors_ private, so this adds two const accessors immediately before
the private section and changes nothing else -- no behaviour, no layout, no
other access.

Idempotent: re-running on an already-patched file is a no-op.
"""

import io
import sys

ANCHOR = " private:\n  bool directed_;"

ADDITION = """ public:
  // NMFC: read-only views of the CSR arrays, so an instrumented kernel can
  // record the addresses it loads row bounds and neighbours from. Those
  // addresses are the point -- they are what the placement pass assigns to a
  // memory tile. Nothing else is exposed and no behaviour changes.
  const DestID_* const* nmfc_out_index() const { return out_index_; }
  const DestID_* nmfc_out_neighbors() const { return out_neighbors_; }

 private:
  bool directed_;"""


def main():
    path = sys.argv[1]
    src = io.open(path, encoding="utf-8").read()

    if "nmfc_out_index" in src:
        print("graph.h already patched")
        return

    if ANCHOR not in src:
        print("ERROR: graph.h does not look like the CSRGraph this expects; "
              "the accessor patch needs updating for this GAPBS revision", file=sys.stderr)
        sys.exit(1)

    io.open(path, "w", encoding="utf-8").write(src.replace(ANCHOR, ADDITION, 1))
    print("patched graph.h with the nmfc accessors")


if __name__ == "__main__":
    main()
