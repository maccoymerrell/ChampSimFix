#!/usr/bin/env python3
"""Generate a one-factor-at-a-time (OFAT) SPPAM config array for main-effects
analysis: a baseline (original SPPAM defaults) plus one config per single-knob
change. Each config's name encodes (axis | change), so aggregated results show
the marginal effect of each knob relative to the baseline.

Usage: make_ofat_configs.py > ofat_configs.json
"""
import json
import sys

# (axis, change-label, overrides) — each is the baseline with ONE thing changed.
VARIATIONS = [
    # Scraping triggers
    ("scrape", "count6",       {"scrape_access_count": 6}),
    ("scrape", "count28",      {"scrape_access_count": 28}),
    ("scrape", "idle250",      {"scrape_idle_time": 250}),
    ("scrape", "idle4000",     {"scrape_idle_time": 4000}),
    ("scrape", "onEvict",      {"scrape_on_evict": True}),
    ("scrape", "noIdle",       {"scrape_on_idle": False}),
    # Pattern resolution
    ("pattern", "size4",       {"pattern_size": 4, "min_pattern_size": 4}),
    ("pattern", "size8",       {"pattern_size": 8, "min_pattern_size": 8}),
    ("pattern", "conf30",      {"min_confidence_to_prefetch": 30}),
    ("pattern", "conf70",      {"min_confidence_to_prefetch": 70}),
    ("pattern", "counterMode", {"table_or_counter": True}),
    # Table sizes
    ("tables", "pt32",         {"pattern_table_sets": 32}),
    ("tables", "pt128",        {"pattern_table_sets": 128}),
    ("tables", "patConf32",    {"pattern_conf_ways": 32}),
    # Region capacity (find the knee where shrinking the region map starts to hurt)
    ("regioncap", "regWays4",  {"region_ways": 4}),
    ("regioncap", "regWays6",  {"region_ways": 6}),
    ("regioncap", "regWays8",  {"region_ways": 8}),
    ("regioncap", "regWays16", {"region_ways": 16}),
    ("regioncap", "regWays24", {"region_ways": 24}),
    ("regioncap", "regSets32", {"region_sets": 32}),
    ("regioncap", "regSets128", {"region_sets": 128}),
    ("regioncap", "regSets256", {"region_sets": 256}),
    # Region size
    ("region", "page8K",       {"page_bits": 13}),
    ("region", "page16K",      {"page_bits": 14}),
    # Directionality
    ("direction", "bidir",     {"do_negative": True, "scan_backward": True}),
    # Aggression
    ("aggression", "scan8",    {"scan_distance_forward": 8}),
    ("aggression", "scan32",   {"scan_distance_forward": 32}),
    ("aggression", "noLookahead", {"do_lookahead": False}),
    ("aggression", "lookahead8",  {"lookahead_depth": 8}),
    ("aggression", "l2deg1",   {"prefetch_to_l2_degree": 1}),
    ("aggression", "noDrop",   {"prob_drop_prefetches": False}),
    # The scrape-window alignment fix
    ("windowfix", "fixed",     {"scrape_full_window": True}),
]


def main():
    configs = [{"name": "baseline"}]
    for axis, label, ov in VARIATIONS:
        c = dict(ov)
        c["name"] = f"{axis}|{label}"
        configs.append(c)
    json.dump(configs, sys.stdout, indent=2)
    print(f"\n# {len(configs)} configs", file=sys.stderr)


if __name__ == "__main__":
    main()
