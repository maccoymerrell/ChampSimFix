#!/usr/bin/env python3
"""Single-core SPPAM design-space sweep generator (REAL ChampSim replay, DPC4 geometry).

Samples K knob-configs over the behavioral knob space (geometry FIXED at the validated
buildable baseline; PE off -- it's a multi-core throttle), and for every (config x trace)
writes a champsim replay config and a manifest row. Run the manifest as a slurm array
(sc_sweep_array.slurm); rank with sc_rank.py.

Usage: sc_sweep_gen.py WORK_DIR K [seed]
"""
import json, os, sys, random

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

def make_config(pref_model, knobs, trace):
    # Single core, DPC4 geometry: L2C 2048x16 (LRU) <- replay; LLC 4096x12 (DRRIP); DRAM 36/36/36.
    children = []
    children.append(_chan("REPLAY_0", 32, 32, 16, "TRACE_REPLAY", {"clock_period": P250, "entity_index": 0},
                          [{"name": "WL_0", "module": "workload_source", "model": "TRACE_REPLAY", "trace_path": trace}]))
    children.append(_chan("L2CLLC_0", 32, 32, 32))
    children.append(_cache("L2C_0", 2048, 16, 16, 32, 5, 5, "L2CLLC_0", pref_model, knobs, "lru", ["@REPLAY_0"]))
    children.append(_chan("LLCDRAM", 64, 64, 64))
    children.append(_cache("LLC", 4096, 12, 32, 64, 18, 17, "LLCDRAM", "no", None, "drrip", ["@L2CLLC_0"]))
    children.append({"name": "DRAM", "module": "memory_controller", "model": "DEFAULT_MEMORY_CONTROLLER",
                     "dbus_period": {"time": "208p"}, "mc_period": {"time": "416p"}, "n_rp": 36, "n_rcd": 36,
                     "n_cas": 36, "n_ras": 78, "refresh_period": {"time": "32000u"}, "rq_size": 64, "wq_size": 64,
                     "channels": 1, "channel_width": {"bytes": "8"}, "rows": 262144, "columns": 1024, "ranks": 1,
                     "bankgroups": 8, "banks": 4, "refreshes_per_period": 8192, "ul_channels": ["@LLCDRAM"]})
    return {"environment": "ENVIRONMENT", "block_size": 64, "page_size": 4096, "num_cores": 0,
            "heartbeat_frequency": 0, "children": children}

# Validated baseline = the module ctor defaults. The sweep overrides knobs below. Buildability
# (geometry-dependent) is recorded per run via the module's [SPPAM+] state print.
BASE = {}  # ctor sets the baseline; anchors/random override from here

# FULL CFG-routable knob space: geometry, pattern/confidence, lookahead, usefulness, scan, SPP,
# bandwidth (feedback/market/rank), and PE (enable + throttle/phase/sample + demand-weight + Eq4
# service constants). min_pattern_size is coupled (<= pattern_size) in the sampler.
SPACE = {
    # -- region/pattern geometry (trades against the buildable budget; state print flags it) --
    "region_bits": [11, 12],
    "region_sets": [64, 128],
    "region_ways": [12, 16],
    "region_tag_bits": [12, 16],
    "region_page_aligned_sets": [True, False],
    "within_page_shadow": [True, False],
    "pattern_size": [5, 6, 7],
    "pattern_context_bits": [0, 2],
    "pattern_context_src": [0, 1],
    # -- prediction / confidence --
    "min_confidence_to_prefetch": [25, 40, 50, 65, 80],
    "counter_up": [1, 2],
    "counter_down": [1, 2, 3],
    "table_or_counter": [False, True],
    # -- lookahead --
    "do_lookahead": [True, False],
    "lookahead_conf_cutoff": [4, 7, 10],
    "lookahead_conf_factor": [10, 13, 16],
    "lookahead_depth": [16, 50, 100, 200, 300],
    # -- usefulness / drop --
    "prob_drop_prefetches": [True, False],
    "global_or_pattern_usefulness": [True, False],
    "adaptive_usefulness": [True, False],
    "pattern_usefulness_cutoff": [4, 7, 10],
    # -- scan / training --
    "scan_distance_forward": [8, 16, 24, 32],
    "do_negative": [False, True],
    "scrape_full_window": [False, True],
    "train_demand_only": [False, True],
    # -- hybrid / SPP --
    "enable_spp": [True, False],
    "enable_fallthrough": [True, False],
    "fallthrough_explore_div": [8, 16, 32],
    "enable_hybrid_bidding": [False, True],
    "bid_by_value": [False, True],
    "enable_shadow_squash": [True, False],
    "spp_usefulness_feedback": [True, False],
    "spp_per_sig_usefulness": [False, True],
    "spp_per_sig_prior": [6.0, 12.0, 24.0],
    "spp_lookahead": [8, 16, 32],
    "spp_threshold": [0.15, 0.25, 0.40],
    "spp_share_region_table": [False, True],
    "spp_sig_bits": [10, 12, 14],
    "spp_pt_sets": [128, 256, 512],
    "spp_pt_ways": [4, 8],
    "spp_deltas_per_sig": [4, 6],
    "spp_conf_bits": [6, 7, 8],
    # -- bandwidth feedback / market / rank --
    "enable_bw_feedback": [True, False],
    "bw_mult": [False, True],
    "enable_bw_market": [False, True],
    "bw_market_target_util": [0.60, 0.70, 0.80],
    "enable_bw_rank": [False, True],
    "bw_rank_strength": [0.5, 1.0, 1.5],
    "bw_rank_lo": [0.40, 0.50],
    "bw_rank_hi": [0.80, 0.85],
    "enable_region_thrash_throttle": [False, True],
    # -- PE (I-POP latency throttle) --
    "enable_pe_management": [False, True],
    "pe_throttle_div": [4, 8, 16],
    "pe_phase": [512, 1024, 2048],
    "pe_sample_div": [4, 8, 16],
    "pe_pf_demand_weight": [0.25, 0.5, 0.75],
    "pe_serv_dram": [8.0, 12.0, 20.0],
    "pe_serv_llc": [2.0, 4.0, 8.0],
    "pe_dram_lat_threshold": [48.0, 64.0, 96.0],
}
MIN_PAT = [4, 5, 6]  # min_pattern_size candidates, filtered to <= pattern_size in the sampler

# Hand-picked anchors (reference points so the random cloud is interpretable).
ANCHORS = [
    ("baseline", {}),
    ("probdrop_off", {"prob_drop_prefetches": False}),
    ("sppam_only", {"enable_spp": False}),
    ("no_lookahead", {"do_lookahead": False}),
    ("aggressive", {"lookahead_depth": 300, "min_confidence_to_prefetch": 25, "prob_drop_prefetches": False}),
    ("conservative", {"lookahead_depth": 16, "min_confidence_to_prefetch": 80}),
    ("spp_persig", {"spp_per_sig_usefulness": True}),
    ("bw_off", {"enable_bw_feedback": False}),
    ("pe_on", {"enable_pe_management": True}),
    ("pe_aggr", {"enable_pe_management": True, "pe_throttle_div": 16, "pe_pf_demand_weight": 0.25}),
]

def main():
    work, K = sys.argv[1], int(sys.argv[2])
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 42
    rng = random.Random(seed)
    L = "/mnt/md0/ChampSim/l2c_trace_work/logs"; S = ".berti_plus.l1d_l2c.bin.zst"
    traces = sorted(p[:-len(S)] for p in os.listdir(L) if p.endswith(S))
    tracepath = {t: f"{L}/{t}{S}" for t in traces}

    # Build the config list: anchors first, then K random draws (dedup by knob-tuple).
    configs, seen = [], set()
    def add(cid, knobs):
        key = tuple(sorted(knobs.items()))
        if key in seen: return
        seen.add(key); configs.append((cid, {**BASE, **knobs}))
    for name, ov in ANCHORS:
        add(name, ov)
    i = 0
    while len([c for c in configs if c[0].startswith("r")]) < K:
        knobs = {k: rng.choice(v) for k, v in SPACE.items()}
        knobs["min_pattern_size"] = rng.choice([m for m in MIN_PAT if m <= knobs["pattern_size"]])
        add(f"r{i:04d}", knobs); i += 1
    # also one no-prefetch base per trace, for normalized ranking
    cfgdir = os.path.join(work, "cfg"); outdir = os.path.join(work, "out")
    os.makedirs(cfgdir, exist_ok=True); os.makedirs(outdir, exist_ok=True)
    json.dump({cid: knobs for cid, knobs in configs}, open(os.path.join(work, "configs.json"), "w"), indent=1)

    man = []
    for t in traces:
        # base (no prefetcher) for normalization
        bp = os.path.join(cfgdir, f"base__{t}.json"); bo = os.path.join(outdir, f"base__{t}.json")
        json.dump(make_config("no", None, tracepath[t]), open(bp, "w")); man.append((bp, bo))
        for cid, knobs in configs:
            cp = os.path.join(cfgdir, f"{cid}__{t}.json"); op = os.path.join(outdir, f"{cid}__{t}.json")
            json.dump(make_config("SPPAM_PLUS", knobs, tracepath[t]), open(cp, "w")); man.append((cp, op))
    with open(os.path.join(work, "manifest.tsv"), "w") as f:
        for cp, op in man:
            f.write(f"{cp}\t{op}\n")
    print(f"configs: {len(configs)} (+ base)  traces: {len(traces)}  total runs: {len(man)}")

if __name__ == "__main__":
    main()
