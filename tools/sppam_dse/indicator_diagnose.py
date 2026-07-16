#!/usr/bin/env python3
"""Diagnose, per indicator behavior, WHY demand misses do/don't drop for one config.

Decomposes the outcome:
  miss_x_nopref   L2 demand miss rate vs no-prefetch (the outcome; <1 good, >1 = net harm)
  actual_reduct   1 - l2_misses/baseline_misses (misses actually removed)
  coverage        covered_timely / baseline_misses (predictions that landed in time)
  pollution_gap   coverage - actual_reduct (gains eaten by prefetch-induced misses)
  accuracy        useful prefetches / issued
  region_miss     fraction of demands whose region wasn't resident (thrashing)
  pf_per_dem      prefetch effort

Usage: indicator_diagnose.py all.csv [--config NAME] [--set indicator_set.txt]
"""
import argparse
import csv
import os
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))


def labels(path):
    m = {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        p = line.split()
        if len(p) >= 2:
            m[os.path.basename(p[1]).replace(".bin.zst", "")] = p[0]
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("all_csv")
    ap.add_argument("--config", default=None, help="config name to diagnose (default: first non-NOPREF)")
    ap.add_argument("--set", default=os.path.join(HERE, "indicator_set.txt"))
    args = ap.parse_args()

    lab = labels(args.set)
    by_trace = defaultdict(dict)
    for r in csv.DictReader(open(args.all_csv)):
        by_trace[r["trace"]][r["name"]] = r
    cfg = args.config
    if cfg is None:
        cfg = next(n for n in next(iter(by_trace.values())) if n != "NOPREF")

    print(f"# diagnosing config: {cfg}")
    print(f"{'behavior':16} {'miss_x_nop':>10} {'actual_red':>10} {'coverage':>9} {'pollu_gap':>9} {'accuracy':>9} {'region_miss':>11} {'pf/dem':>7}")
    for t in sorted(by_trace, key=lambda x: lab.get(x, x)):
        rows = by_trace[t]
        if cfg not in rows or "NOPREF" not in rows:
            continue
        r, nop = rows[cfg], rows["NOPREF"]
        d = int(r["demands"]); bm = int(r["baseline_misses"]); lm = int(r["l2_misses"])
        nop_mr = int(nop["l2_misses"]) / int(nop["demands"])
        mxn = (lm / d) / nop_mr if nop_mr else 0
        red = 1 - lm / bm if bm else 0
        cov = float(r["coverage"])
        acc = float(r["accuracy"])
        rmiss = float(r["region_miss_rate"])
        pfd = int(r["pf_issued"]) / d
        print(f"{lab.get(t,t)[:16]:16} {mxn:10.3f} {red:10.3f} {cov:9.3f} {cov-red:9.3f} {acc:9.3f} {rmiss:11.3f} {pfd:7.2f}")


if __name__ == "__main__":
    main()
