#!/usr/bin/env python3
"""
Emit one memory tile's ramulator2 config from named JEDEC presets.

The device numbers are not ours to invent. ramulator2 keeps the JEDEC
organizations and speed bins in python/ramulator/dram/ddr5.py and expands them
-- including the secondary timings it derives (nRRDS, nRRDL, nFAW, nRFC) and the
whole timing-constraint table -- into the flat form the C++ loader reads. This
builds the config through that path so the file we ship is generated from a
preset name rather than transcribed by hand.

Two parameters here are ours to choose, and both are sized deliberately:

  read_buffer_size / write_buffer_size
      ramulator's defaults are 32 and 32. A 32-entry read queue caps a channel
      at (32 / read_latency) requests per cycle by Little's law, which for this
      device at the latencies a loaded memory tile sees works out below half the
      subchannel's peak -- the queue binds before the DRAM does, and the symptom
      is a channel that looks only half busy while refusing requests. Size it so
      DRAM timing is the binding constraint, which is the whole point of a
      near-memory study.

  clock_ratio
      Inert on our path and required by the schema. ramulator's own run() loop
      uses the frontend:memory ratio to interleave ticks, but the NMFC adapter
      never calls run(): it ticks the frontend and memory system once per
      operate() and sets the ChampSim module's period to the DRAM tCK. The
      values are still set to the true core:DRAM frequency ratio so the file
      does not misdescribe the machine to anyone reading it.

Usage:
    PYTHONPATH=ext/ramulator2/python python3 config/nmfc/ramulator/gen_tile_config.py \
        --read-buffer 128 --write-buffer 64 -o config/nmfc/ramulator/tile_ddr5.yaml
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "ext", "ramulator2", "python"))

import ramulator  # noqa: E402
from ramulator.export import dict_to_yaml  # noqa: E402


def build(args):
    # A departure from the preset organization is asked for by name, so it shows
    # up in the command that produced the file. rank sets banks_per_channel,
    # which sets grain -- re-derive it and re-run annotate --grain-bits to match.
    org_overrides = {} if args.rank is None else {"rank": args.rank}
    dram = ramulator.dram.DDR5(org_preset=args.org_preset, timing_preset=args.timing_preset, **org_overrides)


    controller = ramulator.controller.GenericDDR(
        wr_low_watermark=0.2,
        wr_high_watermark=0.8,
        read_buffer_size=args.read_buffer,
        write_buffer_size=args.write_buffer,
        priority_buffer_size=args.priority_buffer,
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=ramulator.refresh_manager.AllBank(scatter_interval=0, debug=False),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
        # Registered from src/nmfc/, so it has no python component to construct.
        # ChildList passes a raw mapping through untouched.
        controller_plugins=[{"impl": "NMFCBankBalance"}],
        dram=dram,
    )

    memory_system = ramulator.memory_system.GenericDRAM(
        clock_ratio=args.mem_clock_ratio,
        channel_mapper=ramulator.channel_mapper.CacheLineInterleave(interleave_bits=0),
        controllers=[controller],
    )

    frontend = ramulator.frontend.External(clock_ratio=args.core_clock_ratio)

    return {"frontend": frontend.to_config(), "memory_system": memory_system.to_config()}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--org-preset", default="DDR5_16Gb_x8")
    p.add_argument("--timing-preset", default="DDR5_4800AN")
    p.add_argument("--read-buffer", type=int, default=128)
    p.add_argument("--write-buffer", type=int, default=64)
    p.add_argument("--priority-buffer", type=int, default=1568)
    p.add_argument("--rank", type=int, default=None,
                   help="override the preset rank count; changes banks_per_channel and so grain")
    # 4.0 GHz core : 2.4038 GHz DRAM (tCK 416 ps) = 1.664; 5:3 is 1.667.
    p.add_argument("--core-clock-ratio", type=int, default=5)
    p.add_argument("--mem-clock-ratio", type=int, default=3)
    p.add_argument("-o", "--out")
    args = p.parse_args()

    text = dict_to_yaml(build(args))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"wrote {args.out}  ({args.org_preset} / {args.timing_preset}, "
              f"RQ={args.read_buffer} WQ={args.write_buffer})")
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
