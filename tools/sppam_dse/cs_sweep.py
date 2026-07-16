#!/usr/bin/env python3
"""ChampSim DSE sweep harness (real timing, replaces the surrogate's latency model).

Each (knob-config x trace-set) becomes ONE single-head explicit config = a replay->
L2C(SPPAM_PLUS, knobs)->LLC->DRAM chain (single-core: one trace; multi-core mix: a
shared LLC+DRAM fed by N L2Cs). Configs are written to WORK/cfg/ with a manifest; run
them in PARALLEL (one champsim process per config -> uses all cores, ~1x wall-clock per
config instead of N x serial in a single multi-head process). Rank with cs_rank.py.

Usage (edit CONFIGS / TRACESETS below, then):
  python3 cs_sweep.py WORK_DIR            # writes WORK/cfg/*.json + manifest.tsv, prints run cmd
"""
import json, os, sys

CHAMPSIM = "/mnt/md0/ChampSim/ChampSimRuntime/bin/champsim"
P250 = {"time": "250p"}; OFF = {"bits": "6"}; BW = {"bandwidth": 1}

def _chan(n, rq, wq, pq, model="DEFAULT_CHANNEL", extra=None, kids=None):
    d = {"name": n, "module": "channel", "model": model, "rq_size": rq, "wq_size": wq, "pq_size": pq,
         "offset_bits": OFF, "match_offset_bits": False}
    if extra: d.update(extra)
    if kids: d["children"] = kids
    return d

def _cache(n, sets, ways, pq, mshr, hit, fill, lower, pref_model, knobs, repl, uppers):
    pref = {"name": n + "_pref", "module": "prefetcher", "model": pref_model}
    if knobs: pref.update(knobs)
    return {"name": n, "module": "cache", "model": "DEFAULT_CACHE", "clock_period": P250, "num_sets": sets,
            "num_ways": ways, "pq_size": pq, "mshr_size": mshr, "hit_latency": hit, "fill_latency": fill,
            "offset_bits": OFF, "max_tag_bandwidth": BW, "max_fill_bandwidth": BW, "prefetch_as_load": False,
            "match_offset_bits": False, "virtual_prefetch": False,
            "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]}, "upper_levels": uppers,
            "lower_level": "@" + lower, "lower_translate": {"null": "channel"},
            "children": [pref, {"name": n + "_repl", "module": "replacement", "model": repl}]}

def make_config(pref_model, knobs, traces):
    """One single-head config (single- or multi-core) at the DPC4 geometry."""
    children = []; uppers = []; eidx = 0
    for c, tr in enumerate(traces):
        rc, l2, l2llc = f"REPLAY_{c}", f"L2C_{c}", f"L2CLLC_{c}"
        children.append(_chan(rc, 32, 32, 16, "TRACE_REPLAY", {"clock_period": P250, "entity_index": eidx},
                              [{"name": f"WL_{c}", "module": "workload_source", "model": "TRACE_REPLAY", "trace_path": tr}]))
        children.append(_chan(l2llc, 32, 32, 32))
        children.append(_cache(l2, 2048, 16, 16, 32, 5, 5, l2llc, pref_model, knobs, "lru", ["@" + rc]))
        uppers.append("@" + l2llc); eidx += 1
    children.append(_chan("LLCDRAM", 512, 512, 512))  # DRAM request-queue channel capped at 512
    N = len(traces)
    children.append(_cache("LLC", 4096 * N, 12, 32 * N, 64 * N, 17, 18, "LLCDRAM", "no", None, "drrip", uppers))
    children.append({"name": "DRAM", "module": "memory_controller", "model": "DEFAULT_MEMORY_CONTROLLER",
                     "dbus_period": {"time": "208p"}, "mc_period": {"time": "416p"}, "n_rp": 36, "n_rcd": 36,
                     "n_cas": 36, "n_ras": 78, "refresh_period": {"time": "32000u"}, "rq_size": 64, "wq_size": 64,
                     "channels": 1, "channel_width": {"bytes": "8"}, "rows": 262144, "columns": 1024, "ranks": 1,
                     "bankgroups": 8, "banks": 4, "refreshes_per_period": 8192, "ul_channels": ["@LLCDRAM"]})
    return {"environment": "ENVIRONMENT", "block_size": 64, "page_size": 4096, "num_cores": 0,
            "phase_controller_model": "REPLAY_PHASE_CONTROLLER",  # paced replay: no core-centric livelock abort
            "heartbeat_frequency": 0, "children": children}

# ---- edit these for a sweep ----
L = "/mnt/md0/ChampSim/l2c_trace_work/logs"; S = ".berti_plus.l1d_l2c.bin.zst"
def p(n): return f"{L}/{n}{S}"
CONFIGS = [  # (name, prefetcher_model, knob_dict)
    ("base", "no", {}),
    ("center", "SPPAM_PLUS", {}),
    ("pe_on", "SPPAM_PLUS", {"enable_pe_management": True}),
    ("no_lookahead", "SPPAM_PLUS", {"do_lookahead": False}),
    ("sppam_only", "SPPAM_PLUS", {"enable_spp": False}),
]
TRACESETS = {  # name -> [traces] (1 = single-core, N = mix; core 0 = victim)
    "gcc": [p("602.gcc_s-1850B")],
    "mcf": [p("605.mcf_s-1644B")],
    "xalan": [p("623.xalancbmk_s-10B")],
    "mixA": [p("cc-5"), p("605.mcf_s-1644B"), p("gms.triangle_count.soc-pokec-relationships.slice-117"), p("649.fotonik3d_s-1176B")],
}

def main():
    work = sys.argv[1]
    cfgdir = os.path.join(work, "cfg"); outdir = os.path.join(work, "out")
    os.makedirs(cfgdir, exist_ok=True); os.makedirs(outdir, exist_ok=True)
    man = []
    for ts_name, traces in TRACESETS.items():
        for cfg_name, model, knobs in CONFIGS:
            tag = f"{ts_name}.{cfg_name}"
            cfg_path = os.path.join(cfgdir, tag + ".json")
            out_path = os.path.join(outdir, tag + ".json")
            json.dump({"_cores": len(traces), "_traceset": ts_name, "_config": cfg_name,
                       **make_config(model, knobs, traces)}, open(cfg_path, "w"))
            man.append((cfg_path, out_path))
    with open(os.path.join(work, "manifest.tsv"), "w") as f:
        for cfg_path, out_path in man:
            f.write(f"{cfg_path}\t{out_path}\n")
    print(f"wrote {len(man)} configs to {cfgdir}")
    print(f"# run in parallel (MAXPAR cores), e.g.:")
    print(f"SIM=8000000 MAXPAR=40 bash {os.path.dirname(os.path.abspath(__file__))}/cs_run.sh {work}")

if __name__ == "__main__":
    main()
