#!/bin/bash
# Fires before any edit under src/nmfc, inc/nmfc, config/nmfc or tools/nmfc.
#
# The design is written down and was still dropped repeatedly inside a single
# session -- after being read, and after being corrected. Loading it once at the
# start does not work, because the forgetting happens at the moment of the edit.
# So it is restated at that moment instead.
path=$(python3 -c "import json,sys; d=json.load(sys.stdin); print(d.get('tool_input',{}).get('file_path',''))" 2>/dev/null)
case "$path" in
  */src/nmfc/*|*/inc/nmfc/*|*/config/nmfc/*|*/tools/nmfc/*) ;;
  *) exit 0 ;;
esac
cat <<'EOF'
NMFC invariants (docs/nmfc/DESIGN.md §0) -- settled, do not re-derive:
 1. An offload is an INSTRUCTION: FORK rPC,v512 / JOIN v512. The aperture is a
    trace-record encoding, not a mechanism the machine has.
 2. 512 bits in, the same 512 bits back. Whole register file. Positions carry
    no meaning. Needs more live values than the file holds => cannot offload.
 3. Every channel translates LOCALLY. N roots => table partitioned. One root =>
    table DUPLICATED per channel via the replicated page type (the one function
    code uses). One table reached over the fabric is a bug; routing walks is
    not the fix.
 4. Placement is a TRANSLATION-TIME decision by the address space's owner. VA is
    translated to physical before crossing the fabric; the copy handed back
    names the tile. Never consult data the invocation has not touched yet.
 5. Migration is expected and is EVIDENCE. ~1 per 1000 instructions is fine;
    each costs 72 bytes. Frequency is the failure mode, not occurrence.
 6. Physical placement WITHOUT a NUCA/NUMA policy is not the design. Move
    working sets together while balancing access. Round-robin, least-loaded and
    first-touch are not substitutes.
 7. The function core has a register file and NO STACK. A function that spills
    cannot run. Check the disassembly, not the source.
 8. The WORKLOAD COMPUTES NO TILES (§4.2). It issues work; the OS places it;
    the policy moves BOTH ends -- data used together is gathered, and functions
    migrate to their data -- and partitions them evenly. Sorting work by
    tile_of() in the kernel bakes in a layout and tests the allocator instead
    of the architecture. Grain-granular NUCA is a tile SWAP, not a fresh
    allocation; prefer a duplication policy over sub-grain swaps.
Before adding a mechanism, check whether the design already names one.
EOF
