#!/usr/bin/env python3
"""Generate EXPLICIT (module-graph) ChampSim runtime configs for the DPC4 baselines,
with our modules swapped in: L1I=epi, L1D=berti_plus, L2C=SPPAM_PLUS_V2 (module defaults), BP=hashed_perceptron.

Emits the full explicit form (channels + caches + core + PTW + memory_controller + vmem
+ phase_controller), NOT the legacy sets/ways/latency form. Models champsim_config_explicit.json.

DPC4 spec (from /mnt/md0/ChampSim/ChampSimDPC4/dpc4/*.baseline.json), latency split hit=floor,fill=ceil:
  L1I 128x8 lat4(2+2) mshr8 pq32 epi ; L1D 64x12 lat5(2+3) mshr16 pq8 berti_plus
  L2C 2048x16 lat10(5+5) mshr32 pq16 SPPAM_PLUS_V2 ; LLC 4096x12(1C)/16384x12(4C) lat35(17+18) drrip
  DRAM fullBW 4800 (dbus208p mc416p nCAS/nRCD/nRP36 nRAS78) / limitBW 800 (dbus1250p mc2500p nCAS6 nRAS13)
"""
import json, argparse

def off(b): return {"bits": str(b)}
def bw(n): return {"bandwidth": n}

def channel(name, rq, wq, pq, offbits, match):
    return {"name": name, "module": "channel", "model": "DEFAULT_CHANNEL",
            "rq_size": rq, "wq_size": wq, "pq_size": pq, "offset_bits": off(offbits), "match_offset_bits": match}

def cache(name, sets, ways, pq, mshr, hit, fill, offbits, tbw, uppers, lower, ltrans, pref, repl,
          match=False, vpref=False, pref_extra=None):
    prefc = {"name": name + "_prefetcher", "module": "prefetcher", "model": pref}
    if pref_extra: prefc.update(pref_extra)
    return {"name": name, "module": "cache", "model": "DEFAULT_CACHE", "clock_period": {"time": "250p"},
            "num_sets": sets, "num_ways": ways, "pq_size": pq, "mshr_size": mshr,
            "hit_latency": hit, "fill_latency": fill, "offset_bits": off(offbits),
            "max_tag_bandwidth": bw(tbw), "max_fill_bandwidth": bw(tbw), "prefetch_as_load": False,
            "match_offset_bits": match, "virtual_prefetch": vpref,
            "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
            "upper_levels": uppers, "lower_level": lower, "lower_translate": ltrans,
            "children": [prefc, {"name": name + "_replacement", "module": "replacement", "model": repl}]}

def core(i, llc_sets_note):
    p = f"cpu{i}"
    # exact DPC4 baseline ooo_cpu (8-wide, rob 576, rf 288, lq 240, sq 112, scheduler 160, DIB sets 512)
    return {"name": p, "module": "core", "model": "DEFAULT_CORE", "clock_period": {"frequency": "4G"},
            "dib_set": 512, "dib_way": 8, "dib_window": 16, "dib_hit_buffer_size": 32,
            "dib_inorder_width": bw(5), "dib_hit_latency": 1,
            "ifetch_buffer_size": 64, "decode_buffer_size": 32, "dispatch_buffer_size": 32,
            "register_file_size": 288, "rob_size": 576, "lq_size": 240, "sq_size": 112,
            "fetch_width": bw(8), "decode_width": bw(8), "dispatch_width": bw(8), "execute_width": bw(8),
            "lq_width": bw(2), "sq_width": bw(2), "retire_width": bw(8), "schedule_width": bw(160),
            "mispredict_penalty": 1, "decode_latency": 1, "dispatch_latency": 1,
            "schedule_latency": 0, "execute_latency": 0,
            "l1i": f"@{p}_L1I", "l1i_bandwidth": bw(2), "l1d_bandwidth": bw(2),
            "fetch_queues": f"@{p}_{p}_L1I_channel", "data_queues": f"@{p}_{p}_L1D_channel",
            "children": [
                {"name": f"{p}_bp", "module": "branch_predictor", "model": "hashed_perceptron"},
                {"name": f"{p}_btb", "module": "btb", "model": "basic_btb"},
                {"name": f"{p}_trace_producer", "module": "instruction_producer", "model": "INSTRUCTION_PRODUCER",
                 "trace_file": f"$trace{i}", "cloudsuite": False, "repeat": True}]}

def per_core(i, llc_sets, llc_ways, llc_pq, llc_mshr):
    """Returns (channels, caches, ptw) SEPARATELY so build() can emit in @-reference
    (document) order: channels, then DRAM+VMEM, then PTW/caches/cores that reference them."""
    p = f"cpu{i}"; ch = []; ca = []
    # channels (mirror champsim_config_explicit.json queue sizes)
    ch += [channel(f"{p}_PTW_{p}_L1D_channel", 64, 64, 8, 6, True),
           channel(f"{p}_DTLB_{p}_STLB_channel", 32, 32, 0, 12, False),
           channel(f"{p}_ITLB_{p}_STLB_channel", 32, 32, 0, 12, False),
           channel(f"{p}_L1D_{p}_L2C_channel", 32, 32, 16, 6, False),
           channel(f"{p}_L1D_{p}_DTLB_channel", 16, 16, 0, 12, True),
           channel(f"{p}_L1I_{p}_L2C_channel", 32, 32, 16, 6, False),
           channel(f"{p}_L1I_{p}_ITLB_channel", 16, 16, 0, 12, True),
           channel(f"{p}_L2C_LLC_channel", 32, 32, 32, 6, False),
           channel(f"{p}_L2C_{p}_STLB_channel", 32, 32, 0, 12, False),
           channel(f"{p}_STLB_{p}_PTW_channel", 16, 0, 0, 12, False),
           channel(f"{p}_{p}_L1I_channel", 64, 64, 32, 6, True),
           channel(f"{p}_{p}_L1D_channel", 64, 64, 8, 6, True)]
    # caches (DPC4 geometry)
    ca.append(cache(f"{p}_L1I", 128, 8, 32, 8, 2, 2, 6, 2, [f"@{p}_{p}_L1I_channel"],
                    f"@{p}_L1I_{p}_L2C_channel", f"@{p}_L1I_{p}_ITLB_channel", "epi", "lru", match=True, vpref=True))
    ca.append(cache(f"{p}_L1D", 64, 12, 8, 16, 2, 3, 6, 2, [f"@{p}_PTW_{p}_L1D_channel", f"@{p}_{p}_L1D_channel"],
                    f"@{p}_L1D_{p}_L2C_channel", f"@{p}_L1D_{p}_DTLB_channel", "berti_plus", "lru", match=True, vpref=True))
    ca.append(cache(f"{p}_L2C", 2048, 16, 16, 32, 5, 5, 6, 1, [f"@{p}_L1D_{p}_L2C_channel", f"@{p}_L1I_{p}_L2C_channel"],
                    f"@{p}_L2C_LLC_channel", f"@{p}_L2C_{p}_STLB_channel", "SPPAM_PLUS_V2", "lru"))
    ca.append(cache(f"{p}_ITLB", 16, 4, 0, 8, 1, 1, 12, 2, [f"@{p}_L1I_{p}_ITLB_channel"],
                    f"@{p}_ITLB_{p}_STLB_channel", {"null": "channel"}, "no", "lru", match=True, vpref=True))
    ca.append(cache(f"{p}_DTLB", 16, 4, 0, 8, 1, 1, 12, 2, [f"@{p}_L1D_{p}_DTLB_channel"],
                    f"@{p}_DTLB_{p}_STLB_channel", {"null": "channel"}, "no", "lru", match=True))
    ca.append(cache(f"{p}_STLB", 128, 12, 0, 16, 4, 4, 12, 1,
                    [f"@{p}_DTLB_{p}_STLB_channel", f"@{p}_ITLB_{p}_STLB_channel", f"@{p}_L2C_{p}_STLB_channel"],
                    f"@{p}_STLB_{p}_PTW_channel", {"null": "channel"}, "no", "lru"))
    ptw = {"name": f"{p}_PTW", "module": "page_table_walker", "model": "DEFAULT_PTW", "clock_period": {"time": "250p"},
           "mshr_size": 5, "latency": 0, "max_tag_check": bw(2), "max_fill": bw(2),
           "upper_levels": [f"@{p}_STLB_{p}_PTW_channel"], "lower_level": f"@{p}_PTW_{p}_L1D_channel",
           "vmem": "@VMEM", "pscl_dims": [[5, 1, 2], [4, 1, 4], [3, 2, 4], [2, 4, 8]]}
    return ch, ca, ptw

def dram(bw_variant):
    if bw_variant == "full":  # 4800 MT/s
        d = dict(dbus="208p", mc="416p", nrp=36, nrcd=36, ncas=36, nras=78)
    else:                      # limit: 800 MT/s (same abs latency, 6x less BW)
        d = dict(dbus="1250p", mc="2500p", nrp=6, nrcd=6, ncas=6, nras=13)
    return {"name": "DRAM", "module": "memory_controller", "model": "DEFAULT_MEMORY_CONTROLLER",
            "dbus_period": {"time": d["dbus"]}, "mc_period": {"time": d["mc"]},
            "n_rp": d["nrp"], "n_rcd": d["nrcd"], "n_cas": d["ncas"], "n_ras": d["nras"],
            "refresh_period": {"time": "32000u"}, "rq_size": 64, "wq_size": 64, "channels": 1,
            "channel_width": {"bytes": "8"}, "rows": 262144, "columns": 1024, "ranks": 1,
            "bankgroups": 8, "banks": 4, "refreshes_per_period": 8192, "ul_channels": ["@LLC_DRAM_channel"]}

def build(ncores, bw_variant):
    llc_sets, llc_ways = 4096 * ncores, 12
    llc_pq, llc_mshr = 32 * ncores, 64 * ncores
    # collect per-core pieces, then emit in @-reference (document) order
    all_ch, all_ca, all_ptw, core_l2_llc = [], [], [], []
    for i in range(ncores):
        ch, ca, ptw = per_core(i, llc_sets, llc_ways, llc_pq, llc_mshr)
        all_ch += ch; all_ca += ca; all_ptw.append(ptw)
        core_l2_llc.append(f"@cpu{i}_L2C_LLC_channel")
    children = []
    # 1) all channels
    children += all_ch
    # LLC->DRAM channel (DRAM request queue) capped at 512 (the legacy expansion leaves it unbounded)
    children.append(channel("LLC_DRAM_channel", 512, 512, 512, 6, False))
    # 2) DRAM then VMEM (VMEM references @DRAM; PTW/LLC below reference these)
    children.append(dram(bw_variant))
    children.append({"name": "VMEM", "module": "vmem", "model": "DEFAULT_VMEM",
                     "page_table_page_size": {"bytes": "4Ki"}, "page_table_levels": 5,
                     # DPC4 minor_fault_penalty = 200 CYCLES @ 4GHz = 200*250ps = 50000ps (NOT 200ns)
                     "minor_fault_penalty": {"time": "50000p"}, "randomization_seed": {"optional_uint64": 1},
                     "dram": "@DRAM"})
    # 3) LLC (shared) + per-core caches + PTWs (all reference channels/VMEM, defined above)
    children.append(cache("LLC", llc_sets, llc_ways, llc_pq, llc_mshr, 17, 18, 6, 1,
                          core_l2_llc, "@LLC_DRAM_channel", {"null": "channel"}, "no", "drrip"))
    children += all_ca
    children += all_ptw
    # 4) cores (reference caches + channels)
    for i in range(ncores):
        children.append(core(i, llc_sets))
    children.append({"name": "phase_controller", "module": "phase_controller", "model": "PHASE_CONTROLLER",
                     "deadlock_cycles": 500000, "warmup_length": "$warmup_instructions",
                     "simulation_length": "$simulation_instructions"})
    return {"_description": f"DPC4 {ncores}C {bw_variant}BW baseline, EXPLICIT form, L1I=epi L1D=berti_plus L2C=SPPAM_PLUS_V2 BP=hashed_perceptron",
            "environment": "ENVIRONMENT", "block_size": 64, "page_size": 4096,
            "num_cores": ncores, "heartbeat_frequency": 100000, "children": children}

if __name__ == "__main__":
    import os
    OUT = "/mnt/md0/ChampSim/ChampSimRuntime/dpc4_sppam"
    os.makedirs(OUT, exist_ok=True)
    for fn, nc, v in [("1C.fullBW.sppam.json", 1, "full"),
                      ("1C.limitBW.sppam.json", 1, "limit"),
                      ("4C.sppam.json", 4, "full")]:
        json.dump(build(nc, v), open(f"{OUT}/{fn}", "w"), indent=2)
        print(f"wrote {fn}")
