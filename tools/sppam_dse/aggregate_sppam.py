#!/usr/bin/env python3
"""Combine per-task SPPAM sweep CSVs into a long table and a per-config summary.

Each per-task CSV (WORK/csv/<label>.<ncore>c.<chunk>.csv) holds one row per
config for one trace-set. This script tags each row with its trace label and
core count, concatenates everything into all.csv, and writes summary.csv with
per-config means across trace-sets (the headline coverage/accuracy/latency).

Usage:
  aggregate_sppam.py WORK/csv [--out-all all.csv] [--out-summary summary.csv]
"""
import argparse
import csv
import glob
import os
import re
import statistics
from collections import defaultdict

NUMERIC = ["coverage", "coverage_incl_late", "accuracy", "pf_per_demand",
           "avg_latency", "baseline_avg_latency", "latency_saved", "region_miss_rate"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csvdir")
    ap.add_argument("--out-all", default=None)
    ap.add_argument("--out-summary", default=None)
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.csvdir, "*.csv")))
    if not files:
        raise SystemExit(f"no CSVs in {args.csvdir}")

    rows = []
    for path in files:
        m = re.match(r"(?P<label>.+)\.(?P<nc>\d+)c\.\d+\.csv$", os.path.basename(path))
        label = m.group("label") if m else os.path.basename(path)
        with open(path) as f:
            for r in csv.DictReader(f):
                r["trace"] = label
                rows.append(r)

    if not rows:
        raise SystemExit("no rows")

    fields = ["trace"] + list(rows[0].keys() - {"trace"})
    # stable, readable column order
    order = ["trace", "name", "state_kib", "cores", "demands", "baseline_misses", "l2_misses", "covered_timely",
             "covered_late", "coverage", "coverage_incl_late", "pf_issued", "pf_useful",
             "pf_useless", "accuracy", "pf_per_demand", "avg_latency",
             "baseline_avg_latency", "latency_saved", "region_miss_rate"]
    fields = [c for c in order if c in rows[0] or c == "trace"]

    all_path = args.out_all or os.path.join(args.csvdir, "..", "all.csv")
    with open(all_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    # Per-config summary: mean of numeric metrics across traces.
    by_cfg = defaultdict(list)
    for r in rows:
        by_cfg[r["name"]].append(r)
    summary_path = args.out_summary or os.path.join(args.csvdir, "..", "summary.csv")
    with open(summary_path, "w", newline="") as f:
        cols = ["name", "n_traces"] + [f"mean_{c}" for c in NUMERIC]
        w = csv.writer(f)
        w.writerow(cols)
        for name, rs in sorted(by_cfg.items()):
            means = [statistics.mean(float(r[c]) for r in rs) for c in NUMERIC]
            w.writerow([name, len(rs)] + [f"{m:.6g}" for m in means])

    print(f"wrote {all_path} ({len(rows)} rows) and {summary_path} ({len(by_cfg)} configs)")


if __name__ == "__main__":
    main()
