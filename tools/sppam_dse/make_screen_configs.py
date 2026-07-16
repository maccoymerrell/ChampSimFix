#!/usr/bin/env python3
"""Comprehensive OFAT screen: current default (CENTER) + one-knob perturbations
across EVERY knob group, so nothing is missed. Run through the sweep, rank by
Δ mean-miss vs CENTER (rank_configs.py --vs CENTER). CENTER = all defaults.
"""
import json, sys

# CENTER is the current default config (empty -> all param defaults).
CENTER = {"name": "CENTER"}

PERTURB = [
    # --- SPPAM structural ---
    ("pattern_size4", dict(pattern_size=4, min_pattern_size=4)),
    ("pattern_adapt_8_4", dict(pattern_size=8, min_pattern_size=4)),
    ("region_ways16", dict(region_ways=16)),
    ("region_ways24", dict(region_ways=24)),
    ("region_sets128", dict(region_sets=128)),
    ("region_1K", dict(region_bits=10)),
    ("region_1K_128sets", dict(region_bits=10, region_sets=128)),
    ("region_512_256sets", dict(region_bits=9, region_sets=256)),
    ("region_4K", dict(region_bits=12)),
    ("no_page_aligned", dict(region_page_aligned_sets=False)),
    ("no_within_page_shadow", dict(within_page_shadow=False)),
    # --- SPPAM confidence / counters ---
    ("min_conf40", dict(min_confidence_to_prefetch=40)),
    ("min_conf60", dict(min_confidence_to_prefetch=60)),
    ("counter_up2", dict(counter_up=2)),
    ("counter_down1", dict(counter_down=1)),
    ("counter_mode", dict(table_or_counter=True)),
    # --- SPPAM lookahead / degradation ---
    ("la_cutoff5", dict(lookahead_conf_cutoff=5)),
    ("la_cutoff9", dict(lookahead_conf_cutoff=9)),
    ("la_factor11", dict(lookahead_conf_factor=11)),
    ("la_factor15", dict(lookahead_conf_factor=15)),
    ("la_depth50", dict(lookahead_depth=50)),
    ("no_lookahead", dict(do_lookahead=False)),
    # --- SPPAM usefulness / drop / degree ---
    ("no_prob_drop", dict(prob_drop_prefetches=False)),
    ("pat_useful_cut5", dict(pattern_usefulness_cutoff=5)),
    ("pat_useful_cut9", dict(pattern_usefulness_cutoff=9)),
    ("global_usefulness", dict(global_or_pattern_usefulness=False)),
    ("no_adaptive_useful", dict(adaptive_usefulness=False)),
    ("l2_degree4", dict(prefetch_to_l2_degree=4)),
    # --- SPPAM directionality / scrape ---
    ("scan_fwd8", dict(scan_distance_forward=8)),
    ("scan_fwd24", dict(scan_distance_forward=24)),
    ("bidirectional", dict(do_negative=True)),
    ("scrape_full_window", dict(scrape_full_window=True)),
    ("train_demand_only", dict(train_demand_only=True)),
    # --- SPP ---
    ("spp_off", dict(enable_spp=False)),
    ("spp_thresh40", dict(spp_threshold=0.4)),
    ("spp_thresh60", dict(spp_threshold=0.6)),
    ("spp_lookahead8", dict(spp_lookahead=8)),
    ("spp_lookahead2", dict(spp_lookahead=2)),
    ("spp_sig12", dict(spp_sig_bits=12)),
    ("spp_sig6", dict(spp_sig_bits=6)),
    ("spp_pt512", dict(spp_pt_sets=512)),
    ("spp_pt128", dict(spp_pt_sets=128)),
    ("spp_deltas6", dict(spp_deltas_per_sig=6)),
    ("spp_deltas2", dict(spp_deltas_per_sig=2)),
    ("spp_conf10", dict(spp_conf_bits=10)),
    ("spp_conf5", dict(spp_conf_bits=5)),
    ("spp_notshared", dict(spp_share_region_table=False)),
    # --- bandwidth management ---
    ("no_bw_feedback", dict(enable_bw_feedback=False)),
    ("bw_mult", dict(bw_mult=True)),
    ("bw_normal_table", dict(prefetch_degrees_bw=[0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 4, 4, 4, 8, 8])),
    ("no_market", dict(enable_bw_market=False)),
    ("no_pe_mgmt", dict(enable_pe_management=False)),
]

cfgs = [dict(CENTER)]
for label, ch in PERTURB:
    c = dict(CENTER); c.update(ch); c["name"] = label
    cfgs.append(c)
cfgs.append({"name": "NOPREF", "scan_forward": False, "scan_backward": False})

json.dump(cfgs, sys.stdout)
print(f"{len(cfgs)} configs ({len(PERTURB)} perturbations)", file=sys.stderr)
