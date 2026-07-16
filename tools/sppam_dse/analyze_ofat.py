#!/usr/bin/env python3
"""Main-effects analysis of an OFAT SPPAM sweep.

Reads the combined all.csv (trace x config x metrics; produced by
aggregate_sppam.py), then for each one-knob-change config reports its marginal
effect = mean metric across traces minus the baseline's mean. Ranks changes by
impact on coverage, accuracy, and latency saved, so it's clear which design axes
matter and in which direction.

Usage: analyze_ofat.py all.csv
"""
import argparse
import csv
import statistics
from collections import defaultdict

METRICS = ["coverage", "accuracy", "pf_per_demand", "latency_saved", "avg_latency"]


def load(path):
    by_cfg = defaultdict(dict)  # name -> {trace: row}
    for r in csv.DictReader(open(path)):
        by_cfg[r["name"]][r["trace"]] = r
    return by_cfg


def mean_metric(rows, metric):
    return statistics.mean(float(r[metric]) for r in rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("all_csv")
    ap.add_argument("--metric", default="coverage", choices=METRICS, help="rank by this metric")
    args = ap.parse_args()

    by_cfg = load(args.all_csv)
    if "baseline" not in by_cfg:
        raise SystemExit("no 'baseline' config in data")

    # Means per config (over the traces shared with baseline for a fair delta).
    base_traces = set(by_cfg["baseline"].keys())
    base_mean = {m: mean_metric(list(by_cfg["baseline"].values()), m) for m in METRICS}
    ntraces = len(base_traces)

    print(f"# OFAT main effects over {ntraces} traces")
    print(f"# baseline: " + "  ".join(f"{m}={base_mean[m]:.4g}" for m in METRICS))
    print()

    # Effect of each change = mean(config) - mean(baseline) on shared traces.
    effects = []
    for name, tr in by_cfg.items():
        if name == "baseline":
            continue
        shared = [tr[t] for t in base_traces if t in tr]
        if not shared:
            continue
        row = {"name": name}
        for m in METRICS:
            row[m] = mean_metric(shared, m)
            row["d_" + m] = row[m] - base_mean[m]
        # win rate: fraction of traces where coverage strictly beats baseline
        wins = sum(1 for t in base_traces if t in tr and float(tr[t]["coverage"]) > float(by_cfg["baseline"][t]["coverage"]))
        row["cov_winrate"] = wins / ntraces
        effects.append(row)

    # Ranked table by the chosen metric's absolute delta.
    effects.sort(key=lambda r: -abs(r["d_" + args.metric]))
    w = max(len(e["name"]) for e in effects)
    print(f"{'change'.ljust(w)}  d_coverage  d_accuracy  d_pf/dem  d_lat_saved  cov_winrate")
    for e in effects:
        print(f"{e['name'].ljust(w)}  {e['d_coverage']:+.4f}     {e['d_accuracy']:+.4f}     "
              f"{e['d_pf_per_demand']:+.3f}    {e['d_latency_saved']:+.3f}      {e['cov_winrate']:.2f}")


if __name__ == "__main__":
    main()
