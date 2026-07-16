#!/usr/bin/env python3
"""Comprehensive OFAT screen of the ENTIRE throttling space, evaluated by AVG LATENCY
(the optimization target -- see memory sppam-dse-objective). CENTER = current defaults
(hybrid SPPAM+SPP enabled). Every throttle knob is perturbed one-at-a-time, plus key
multi-throttle corners. Rank with rank_latency.py --vs CENTER.
"""
import json, sys

# CENTER: current default throttle stack + the buildable hybrid geometry.
CENTER = {"name": "CENTER",
          "region_bits": 11, "region_sets": 64, "region_ways": 12,
          "pattern_size": 6, "min_pattern_size": 6, "enable_spp": True}

def c(name, **ch):
    d = dict(CENTER); d.update(ch); d["name"] = name; return d

PERTURB = [
    # ---- PE management (PE = i_upf - i_lat, direct latency throttle) ----
    c("pe_off", enable_pe_management=False),
    c("pe_div4", pe_throttle_div=4),
    c("pe_div16", pe_throttle_div=16),
    c("pe_div2", pe_throttle_div=2),
    c("pe_phase512", pe_phase=512),
    c("pe_phase2048", pe_phase=2048),
    # ---- Bandwidth feedback (lookahead/degree scaled by aggregate DRAM util) ----
    c("fb_off", enable_bw_feedback=False),
    c("bw_mult", bw_mult=True),
    c("bw_light", prefetch_degrees_bw=[0,0,0,0,1,1,1,1,2,2,2,4,4,4,8,8]),
    c("bw_heavy", prefetch_degrees_bw=[0,0,0,1,1,2,2,4,4,8,8,12,12,16,16,16]),
    # ---- Bandwidth market (demand-clearing admission) ----
    c("market_on", enable_bw_market=True),
    c("market_on_t60", enable_bw_market=True, bw_market_target_util=0.60),
    c("market_on_t80", enable_bw_market=True, bw_market_target_util=0.80),
    # ---- Rank-order bandwidth throttle ----
    c("rank_on", enable_bw_rank=True),
    c("rank_on_s18", enable_bw_rank=True, bw_rank_strength=1.8),
    # ---- Region-thrash throttle ----
    c("thrash_on", enable_region_thrash_throttle=True),
    # ---- SPP efficiency throttle (usefulness-modulated lookahead) ----
    c("uf_off", spp_usefulness_feedback=False),
    c("persig_on", spp_usefulness_feedback=False, spp_per_sig_usefulness=True),
    # ---- SPPAM usefulness / drop / degree ----
    c("prob_drop_off", prob_drop_prefetches=False),
    c("adaptive_useful_off", adaptive_usefulness=False),
    c("global_useful", global_or_pattern_usefulness=False),
    c("pat_useful_cut5", pattern_usefulness_cutoff=5),
    c("pat_useful_cut9", pattern_usefulness_cutoff=9),
    c("l2_degree4", prefetch_to_l2_degree=4),
    c("l2_degree2", prefetch_to_l2_degree=2),
    # ---- SPPAM lookahead ----
    c("no_lookahead", do_lookahead=False),
    c("la_cutoff5", lookahead_conf_cutoff=5),
    c("la_cutoff9", lookahead_conf_cutoff=9),
    # ---- Combination mechanism ----
    c("fallthrough_off", enable_fallthrough=False),
    c("bidding_on", enable_hybrid_bidding=True),
    c("shadow_off", enable_shadow_squash=False),
    c("spp_off", enable_spp=False),                 # SPPAM-only
    # ---- SPP aggressiveness ----
    c("spp_la8", spp_lookahead=8),
    c("spp_la2", spp_lookahead=2),
    c("spp_thresh40", spp_threshold=0.40),
    c("spp_thresh15", spp_threshold=0.15),
    # ---- Multi-throttle corners ----
    c("all_throttle_off", enable_pe_management=False, enable_bw_feedback=False,
      enable_bw_market=False, enable_bw_rank=False, spp_usefulness_feedback=False,
      prob_drop_prefetches=False),
    c("pe_only", enable_bw_feedback=False),
    c("fb_only", enable_pe_management=False),
    c("pe_fb_market", enable_bw_market=True),
    c("pe_fb_rank", enable_bw_rank=True),
    c("pe_fb_thrash", enable_region_thrash_throttle=True),
    c("aggressive", pe_throttle_div=4, enable_bw_market=True,
      prefetch_degrees_bw=[0,0,0,1,1,2,2,4,4,8,8,12,12,16,16,16]),
    c("pe_fb_persig", spp_usefulness_feedback=False, spp_per_sig_usefulness=True),
]

cfgs = [dict(CENTER)] + PERTURB
cfgs.append({"name": "NOPREF", "scan_forward": False, "scan_backward": False})
json.dump(cfgs, sys.stdout)
print(f"{len(cfgs)} configs ({len(PERTURB)} perturbations)", file=sys.stderr)
