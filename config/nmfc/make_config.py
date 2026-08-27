#!/usr/bin/env python3
"""
Generate the NMFC and matched-baseline configurations.

The two configurations differ in exactly one place: whether the compute tile's
core is NMFC_HOST_CORE (offloads through the function network) or DEFAULT_CORE
(runs everything inline). Everything else -- cache geometry, the interleave
fabric, the memory tiles, the page allocator -- is shared, so a difference in
results is the architecture rather than the setup.

Module declaration order matters: the explicit environment resolves @-references
as it walks the children array, so anything referenced must already be built.
That is also why the fabric comes before the tiles: they register themselves
with it at construction rather than it holding forward references.

    python3 config/nmfc/make_config.py --tiles 4 --out-dir config/nmfc
"""

import argparse
import json
import os

CLOCK = {"time": "250p"}
BLOCK_OFFSET = {"bits": "6"}
PAGE_OFFSET = {"bits": "12"}


def channel(name, rq=32, wq=32, pq=16, offset=None, match=False, comment=None):
    entry = {
        "name": name,
        "module": "channel",
        "model": "DEFAULT_CHANNEL",
        "rq_size": rq,
        "wq_size": wq,
        "pq_size": pq,
        "offset_bits": offset or BLOCK_OFFSET,
        "match_offset_bits": match,
    }
    if comment:
        entry["_comment"] = comment
    return entry


def cache(name, sets, ways, *, upper, lower, translate=None, hit=5, fill=5,
          mshr=32, pq=16, offset=None, match=False, virtual_prefetch=False,
          prefetcher="no", replacement="lru", comment=None):
    entry = {
        "name": name,
        "module": "cache",
        "model": "DEFAULT_CACHE",
        "clock_period": CLOCK,
        "num_sets": sets,
        "num_ways": ways,
        "pq_size": pq,
        "mshr_size": mshr,
        "hit_latency": hit,
        "fill_latency": fill,
        "offset_bits": offset or BLOCK_OFFSET,
        "max_tag_bandwidth": {"bandwidth": 2},
        "max_fill_bandwidth": {"bandwidth": 2},
        "prefetch_as_load": False,
        "match_offset_bits": match,
        "virtual_prefetch": virtual_prefetch,
        "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
        "upper_levels": upper,
        "lower_level": lower,
        "lower_translate": translate or {"null": "channel"},
        "children": [
            {"name": f"{name}_prefetcher", "module": "prefetcher", "model": prefetcher},
            {"name": f"{name}_replacement", "module": "replacement", "model": replacement},
        ],
    }
    if comment:
        entry["_comment"] = comment
    return entry


def build(args, nmfc_enabled):
    tiles = args.tiles
    children = []

    # ---- channels ------------------------------------------------------
    # Compute tile's private hierarchy, unchanged from a stock machine.
    for name, rq, wq, pq, off, match in [
        ("cpu0_cpu0_L1I_channel", 64, 64, 32, BLOCK_OFFSET, True),
        ("cpu0_cpu0_L1D_channel", 64, 64, 8, BLOCK_OFFSET, True),
        ("cpu0_L1I_cpu0_L2C_channel", 32, 32, 16, BLOCK_OFFSET, False),
        ("cpu0_L1D_cpu0_L2C_channel", 32, 32, 16, BLOCK_OFFSET, False),
        ("cpu0_L1D_cpu0_DTLB_channel", 16, 16, 0, PAGE_OFFSET, True),
        ("cpu0_L1I_cpu0_ITLB_channel", 16, 16, 0, PAGE_OFFSET, True),
        ("cpu0_DTLB_cpu0_STLB_channel", 32, 32, 0, PAGE_OFFSET, False),
        ("cpu0_ITLB_cpu0_STLB_channel", 32, 32, 0, PAGE_OFFSET, False),
        ("cpu0_L2C_cpu0_STLB_channel", 32, 32, 0, PAGE_OFFSET, False),
        ("cpu0_STLB_cpu0_PTW_channel", 16, 0, 0, PAGE_OFFSET, False),
        ("cpu0_PTW_cpu0_L1D_channel", 64, 64, 8, BLOCK_OFFSET, True),
    ]:
        children.append(channel(name, rq, wq, pq, off, match))

    for t in range(tiles):
        children.append(channel(f"fabric_tile{t}_channel", 64, 64, 64,
                                comment=f"compute tile -> memory tile {t}"))
        children.append(channel(f"tile{t}_LLC_DRAM_channel", 64, 64, 64))
        if nmfc_enabled:
            children.append(channel(f"tile{t}_fc_dcache_channel", 64, 64, 0,
                                    comment=f"function core {t} -> its data cache"))
            children.append(channel(f"tile{t}_fc_icache_channel", 32, 0, 0))
            children.append(channel(f"tile{t}_LLC_fcD_channel", 64, 64, 0,
                                    comment=f"function core {t}'s data port into the slice"))
            children.append(channel(f"tile{t}_LLC_fcI_channel", 32, 0, 0))
            if args.mmu:
                children.append(channel(f"tile{t}_LLC_mmu_channel", 32, 0, 0,
                                        comment=f"page-table walk references from tile {t}'s MMU"))

    # ---- memory controllers, one channel each ---------------------------
    for t in range(tiles):
        if args.dram == "ramulator":
            # Each tile drives its own single-channel ramulator2 machine. The
            # address arriving here already had the tile-select field compacted
            # out, which is exactly the dense space a one-channel controller
            # wants. Needs: make NMFC_RAMULATOR=1
            children.append({
                "_comment": f"memory tile {t}: DRAM modeled by ramulator2",
                "name": f"tile{t}_DRAM", "module": "memory_controller", "model": "RAMULATOR_MC",
                "config": args.ramulator_config,
                "size": args.tile_bytes,
                "max_accept": {"bandwidth": 4},
                "ul_channels": [f"@tile{t}_LLC_DRAM_channel"],
            })
            continue
        children.append({
            "_comment": f"memory tile {t} owns exactly one DRAM channel",
            "name": f"tile{t}_DRAM", "module": "memory_controller",
            "model": "DEFAULT_MEMORY_CONTROLLER",
            "dbus_period": {"time": "312p"}, "mc_period": {"time": "625p"},
            "n_rp": 24, "n_rcd": 24, "n_cas": 24, "n_ras": 52,
            "refresh_period": {"time": "32000u"}, "refreshes_per_period": 8192,
            "rq_size": 64, "wq_size": 64, "channels": 1,
            "channel_width": {"bytes": "8"},
            "rows": 65536, "columns": 1024, "ranks": 1, "bankgroups": 8, "banks": 4,
            "ul_channels": [f"@tile{t}_LLC_DRAM_channel"],
        })

    # ---- the page allocator --------------------------------------------
    # Declared before the vmem, the fabric and the function cores, all of which
    # ask it the same question: which tile owns this address? It is the one
    # place that decides, and the three models differ in *when* the answer is
    # available -- before translation or only after it -- which is why they are
    # separate models rather than a parameter.
    children.append({
        "_comment": "Who owns an address, and when that is known. See inc/nmfc/tile_router.h.",
        "name": "ROUTER", "module": "tile_router", "model": args.router,
    })

    children.append({
        "_comment": "Congruent allocation: a virtual grain gets a frame from the tile "
                    "its own address names, which is what lets a function core decide "
                    "local-vs-migrate before translating.",
        "name": "VMEM", "module": "vmem", "model": "NMFC_VMEM",
        "page_table_page_size": {"bytes": "4Ki"}, "page_table_levels": 5,
        "minor_fault_penalty": {"time": "50000p"},
        "default_region": "standard",
        "router": "@ROUTER",
        "dram": "@tile0_DRAM",
    })

    children.append({
        "name": "cpu0_PTW", "module": "page_table_walker", "model": "DEFAULT_PTW",
        "clock_period": CLOCK, "mshr_size": 5, "latency": 0,
        "max_tag_check": {"bandwidth": 2}, "max_fill": {"bandwidth": 2},
        "upper_levels": ["@cpu0_STLB_cpu0_PTW_channel"],
        "lower_level": "@cpu0_PTW_cpu0_L1D_channel",
        "vmem": "@VMEM",
        "pscl_dims": [[5, 1, 2], [4, 1, 4], [3, 2, 4], [2, 4, 8]],
    })

    # ---- the function network ------------------------------------------
    # Declared before the tiles and the core, which register themselves with it.
    if nmfc_enabled:
        children.append({
            "name": "fn_image", "module": "function_image", "model": "FUNCTION_IMAGE_STORE",
            "soft_capacity": 65536,
        })
        children.append({
            "_comment": "Owns hop latency and the placement policy. Choosing a tile is "
                        "one add, because a function's copies sit on consecutive grains.",
            "name": "fn_fabric", "module": "function_fabric", "model": "FUNCTION_FABRIC",
            "router": "@ROUTER",
            "clock_period": CLOCK,
            "hop_latency": 8, "queue_size": 128, "max_deliver": {"bandwidth": 4},
            "placement_policy": args.placement,
        })

    # ---- memory tiles ---------------------------------------------------
    for t in range(tiles):
        upper = [f"@fabric_tile{t}_channel"]
        if nmfc_enabled:
            upper += [f"@tile{t}_LLC_fcD_channel", f"@tile{t}_LLC_fcI_channel"]
            if args.mmu:
                upper.append(f"@tile{t}_LLC_mmu_channel")
        children.append(cache(
            f"tile{t}_LLC", args.llc_sets // tiles, 16,
            upper=upper, lower=f"@tile{t}_LLC_DRAM_channel",
            hit=20, fill=20, mshr=64, pq=32,
            comment=f"LLC slice owned by memory tile {t}; indexed on the compacted address",
        ))

        if not nmfc_enabled:
            continue

        # The tile's own ports into its slice. These compact the address the way
        # the interleave fabric does, so both paths tag a line identically, and
        # they assert that nothing foreign to this tile ever crosses them.
        ports = [("fcD", f"@tile{t}_LLC_fcD_channel"), ("fcI", f"@tile{t}_LLC_fcI_channel")]
        if args.mmu:
            ports.append(("mmu", f"@tile{t}_LLC_mmu_channel"))
        for kind, lower in ports:
            children.append({
                "name": f"tile{t}_{kind}_port", "module": "channel", "model": "TILE_PORT",
                "clock_period": CLOCK, "tile": t, "lower": lower,
                "latency": 1, "queue_size": 32, "max_forward": {"bandwidth": 4},
                "strict_locality": True,
            })

        if args.mmu:
            # Mixed page sizes in one module: a small array for 4 KiB pages and a
            # grain-sized array probed alongside it. Deliberately small -- at
            # graph scale the regime is mostly-miss whatever the size, and a
            # generous TLB would flatter the design rather than measure it.
            children.append({
                "_comment": f"tile {t}: dual-page-size MMU. Walk references go to this tile's "
                            f"own slice, which congruent page-table placement makes local.",
                "name": f"tile{t}_mmu", "module": "channel", "model": "NMFC_MMU",
                "clock_period": CLOCK,
                "vmem": "@VMEM", "tile": t,
                "lower_level": f"@tile{t}_mmu_port",
                "small_sets": args.tlb_sets, "small_ways": 4,
                "huge_sets": args.tlb_sets // 2, "huge_ways": 4,
                "hit_latency": 1, "mshr_size": 32,
            })

        children.append(cache(
            f"tile{t}_fc_dcache", 64, 8,
            upper=[f"@tile{t}_fc_dcache_channel"], lower=f"@tile{t}_fcD_port",
            hit=2, fill=2, mshr=64, pq=0,
            comment="small and close: the LLC tag check is expensive and these kernels have tight working sets",
        ))
        children.append(cache(
            f"tile{t}_fc_icache", 16, 4,
            upper=[f"@tile{t}_fc_icache_channel"], lower=f"@tile{t}_fcI_port",
            hit=2, fill=2, mshr=8, pq=0,
        ))

        children.append({
            "_comment": f"multi-context, in-order, non-speculative near-memory engine on tile {t}",
            "name": f"tile{t}_fc", "module": "function_core", "model": "FUNCTION_CORE",
            "clock_period": CLOCK, "tile": t,
            "num_contexts": args.contexts,
            "issue_width": {"bandwidth": 4},
            "fabric": "@fn_fabric", "image": "@fn_image", "vmem": "@VMEM", "router": "@ROUTER",
            "dcache": f"@tile{t}_fc_dcache_channel",
            "icache": f"@tile{t}_fc_icache_channel",
            "fetch_bubble": 1,
            **({"mmu": f"@tile{t}_mmu"} if args.mmu else {}),
            "alu_latency": 1, "mul_latency": 3, "branch_latency": 1,
        })

    # ---- the compute tile ----------------------------------------------
    children.append({
        "_comment": "Routes an L2C miss to the slice owning the address, and compacts "
                    "the tile-select field so each slice uses all of its sets.",
        "name": "cpu0_fabric", "module": "channel", "model": "INTERLEAVE_FABRIC",
        "clock_period": CLOCK,
        "tiles": [f"@fabric_tile{t}_channel" for t in range(tiles)],
        "hop_latency": 4, "queue_size": 64, "max_forward": {"bandwidth": 4},
        "compact_tile_bits": True,
    })

    children.append(cache("cpu0_DTLB", 16, 4, upper=["@cpu0_L1D_cpu0_DTLB_channel"],
                          lower="@cpu0_DTLB_cpu0_STLB_channel", hit=1, fill=1, mshr=8, pq=0,
                          offset=PAGE_OFFSET))
    children.append(cache("cpu0_ITLB", 16, 4, upper=["@cpu0_L1I_cpu0_ITLB_channel"],
                          lower="@cpu0_ITLB_cpu0_STLB_channel", hit=1, fill=1, mshr=8, pq=0,
                          offset=PAGE_OFFSET, virtual_prefetch=True))
    children.append(cache("cpu0_L1D", 64, 12, upper=["@cpu0_PTW_cpu0_L1D_channel", "@cpu0_cpu0_L1D_channel"],
                          lower="@cpu0_L1D_cpu0_L2C_channel", translate="@cpu0_L1D_cpu0_DTLB_channel",
                          hit=2, fill=3, mshr=16, pq=8, match=True))
    children.append(cache("cpu0_L1I", 64, 8, upper=["@cpu0_cpu0_L1I_channel"],
                          lower="@cpu0_L1I_cpu0_L2C_channel", translate="@cpu0_L1I_cpu0_ITLB_channel",
                          hit=2, fill=2, mshr=8, pq=32, match=True, virtual_prefetch=True))
    children.append(cache("cpu0_L2C", 1024, 8, upper=["@cpu0_L1D_cpu0_L2C_channel", "@cpu0_L1I_cpu0_L2C_channel"],
                          lower="@cpu0_fabric", translate="@cpu0_L2C_cpu0_STLB_channel",
                          hit=10, fill=10, mshr=32, pq=16))
    children.append(cache("cpu0_STLB", 128, 12, upper=["@cpu0_DTLB_cpu0_STLB_channel", "@cpu0_ITLB_cpu0_STLB_channel",
                                                       "@cpu0_L2C_cpu0_STLB_channel"],
                          lower="@cpu0_STLB_cpu0_PTW_channel", hit=8, fill=8, mshr=16, pq=0,
                          offset=PAGE_OFFSET))

    producer = {
        "name": "cpu0_trace", "module": "instruction_producer",
        "model": "NMFC_PRODUCER" if nmfc_enabled else "INSTRUCTION_PRODUCER",
        "trace_file": "$trace0", "repeat": False,
    }
    if nmfc_enabled:
        producer["image"] = "@fn_image"
        producer["vmem"] = "@VMEM"

    core = {
        "name": "cpu0", "module": "core",
        "model": "NMFC_HOST_CORE" if nmfc_enabled else "DEFAULT_CORE",
        "clock_period": {"frequency": "4G"},
        "dib_set": 32, "dib_way": 8, "dib_window": 16, "dib_hit_buffer_size": 32,
        "dib_inorder_width": {"bandwidth": 5}, "dib_hit_latency": 1,
        "ifetch_buffer_size": 64, "decode_buffer_size": 32, "dispatch_buffer_size": 32,
        "register_file_size": 128, "rob_size": 352, "lq_size": 128, "sq_size": 72,
        "fetch_width": {"bandwidth": 6}, "decode_width": {"bandwidth": 6},
        "dispatch_width": {"bandwidth": 6}, "execute_width": {"bandwidth": 4},
        "lq_width": {"bandwidth": 2}, "sq_width": {"bandwidth": 2},
        "retire_width": {"bandwidth": 5}, "schedule_width": {"bandwidth": 128},
        "mispredict_penalty": 1, "decode_latency": 1, "dispatch_latency": 1,
        "schedule_latency": 0, "execute_latency": 0,
        "l1i": "@cpu0_L1I", "l1i_bandwidth": {"bandwidth": 2}, "l1d_bandwidth": {"bandwidth": 2},
        "fetch_queues": "@cpu0_cpu0_L1I_channel", "data_queues": "@cpu0_cpu0_L1D_channel",
        "children": [
            {"name": "cpu0_bp", "module": "branch_predictor", "model": "hashed_perceptron"},
            {"name": "cpu0_btb", "module": "btb", "model": "basic_btb"},
            producer,
        ],
    }
    if nmfc_enabled:
        core["fabric"] = "@fn_fabric"
        core["image"] = "@fn_image"
        core["ftu_size"] = args.ftu_size if args.ftu_size > 0 else args.tiles * args.contexts
    children.append(core)

    children.append({"name": "heartbeat", "module": "listener", "model": "HEARTBEAT",
                     "frequency": 1000000})
    controller = {"name": "phase_controller", "module": "phase_controller", "model": "PHASE_CONTROLLER",
                  "warmup_length": "$warmup_instructions",
                  "simulation_length": "$simulation_instructions"}
    if nmfc_enabled:
        # Host instructions retired is the wrong liveness metric for this
        # machine. When work is offloaded the host can retire almost nothing for
        # a long stretch while the memory tiles are fully busy -- a single
        # power-law hub becomes one very large invocation, and the default
        # 10M-cycle health window reads that as a stall and aborts a run that is
        # in fact progressing. Widened rather than removed: a genuinely wedged
        # run should still be caught.
        controller["health_period"] = 2000000000
    children.append(controller)

    label = "NMFC" if nmfc_enabled else "baseline"
    return {
        "_description": f"{label}: one compute tile and {tiles} memory tiles, each owning an "
                        f"LLC slice and one DRAM channel"
                        + (f", with a {args.contexts}-context function core" if nmfc_enabled else
                           " (the same machine, running everything on the host core)"),
        "environment": "ENVIRONMENT",
        "block_size": 64,
        "page_size": 4096,
        "num_cores": 1,
        "nmfc_num_tiles": tiles,
        "nmfc_grain_bits": args.grain_bits,
        "nmfc_mode_bit": args.mode_bit,
        # The offload aperture: a reserved window of virtual addresses that name
        # invocation tokens, the way an MMIO range names device registers. It
        # bounds nothing about the data -- graphs are as large as memory allows
        # and translation is untouched -- only how many token ids can be named.
        # It sits above the mode bit and above anything the generator emits, and
        # the producer refuses a trace whose own addresses land inside it rather
        # than reading them as invocations.
        "nmfc_aperture_base": 1 << 46,
        "nmfc_aperture_bytes": 1 << 42,
        "children": children,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tiles", type=int, default=4)
    parser.add_argument("--contexts", type=int, default=256)
    # The tracking unit is the ceiling on in-flight invocations for the whole
    # machine, so by default it is sized to the total context count. Left at a
    # small value it silently caps everything: a 1024-context machine with a
    # 64-entry unit can never exceed 64 outstanding, and every occupancy number
    # it reports describes the unit rather than the architecture.
    parser.add_argument("--ftu-size", type=int, default=0,
                        help="0 (default) sizes it to tiles x contexts")
    parser.add_argument("--grain-bits", type=int, default=21)
    parser.add_argument("--mode-bit", type=int, default=38)
    parser.add_argument("--llc-sets", type=int, default=2048)
    parser.add_argument("--placement", default="round_robin")
    parser.add_argument("--router", default="CONGRUENT_ROUTER",
                        choices=["CONGRUENT_ROUTER", "RELOCATION_ROUTER", "PHYSICAL_ROUTER"],
                        help="when the owning tile is decided, relative to translation")
    parser.add_argument("--dram", choices=["default", "ramulator"], default="default")
    parser.add_argument("--mmu", action="store_true", default=True,
                        help="model function-core translation (default)")
    parser.add_argument("--no-mmu", dest="mmu", action="store_false",
                        help="oracle translation instead: correct addresses, no modeled walk cost")
    parser.add_argument("--tlb-sets", type=int, default=32)
    parser.add_argument("--ramulator-config", default="config/nmfc/ramulator/tile_ddr5.yaml")
    parser.add_argument("--tile-bytes", type=int, default=4 << 30)
    parser.add_argument("--out-dir", default="config/nmfc")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    for enabled, name in [(True, "nmfc"), (False, "baseline")]:
        suffix = "" if args.dram == "default" else f"_{args.dram}"
        suffix += "" if args.mmu else "_oracle"
        path = os.path.join(args.out_dir, f"{name}_{args.tiles}tile{suffix}.json")
        with open(path, "w") as handle:
            json.dump(build(args, enabled), handle, indent=2)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
