#!/usr/bin/env python3
"""Rank configs in an aggregated all.csv by mean L2 miss rate over the indicator set.

For each config: mean over traces of (L2 demand miss rate / no-pref), mean
bandwidth (pf/demand), and state. Sorted by mean miss rate (the objective).
Optionally prints the per-behavior breakdown for the top-N.

Usage: rank_configs.py all.csv [--set indicator_set.txt] [--top N] [--breakdown]
"""
import argparse, csv, os
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))


def labels(path):
    m = {}
    for line in open(path):
        line = line.strip()
        if line and not line.startswith("#"):
            p = line.split()
            if len(p) >= 2:
                m[os.path.basename(p[1]).replace(".bin.zst", "")] = p[0]
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("all_csv")
    ap.add_argument("--set", default=os.path.join(HERE, "indicator_set.txt"))
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--breakdown", action="store_true")
    ap.add_argument("--vs", default=None, help="show signed Δ mean-miss vs this config (OFAT screen)")
    args = ap.parse_args()

    lab = labels(args.set)
    by_trace = defaultdict(dict)
    names = []
    for r in csv.DictReader(open(args.all_csv)):
        by_trace[r["trace"]][r["name"]] = r
        if r["name"] not in names:
            names.append(r["name"])
    cfgs = [n for n in names if n != "NOPREF"]

    miss = defaultdict(list)   # config -> [miss_x_nopref per trace]
    bw = defaultdict(list)
    perwl = defaultdict(dict)  # config -> {behavior: miss}
    state = {}
    for t, rows in by_trace.items():
        if "NOPREF" not in rows:
            continue
        nop = int(rows["NOPREF"]["l2_misses"]) / int(rows["NOPREF"]["demands"])
        for c in cfgs:
            if c not in rows:
                continue
            d = int(rows[c]["demands"])
            mr = (int(rows[c]["l2_misses"]) / d) / nop if nop else 0
            miss[c].append(mr)
            bw[c].append(int(rows[c]["pf_issued"]) / d)
            perwl[c][lab.get(t, t)] = mr
            state[c] = float(rows[c]["state_kib"])

    mean = lambda c: sum(miss[c]) / len(miss[c]) if miss[c] else 9
    meanbw = lambda c: sum(bw[c]) / len(bw[c]) if bw[c] else 0
    ranked = sorted(cfgs, key=mean)
    if args.vs and args.vs in miss:
        base = mean(args.vs); basebw = meanbw(args.vs)
        print(f"# Δ mean-miss vs {args.vs} (mean_miss={base:.4f}, bw={basebw:.2f}); negative Δ = improvement")
        print(f"{'config':28} {'mean_miss':>9} {'Δmiss':>8} {'mean_bw':>8} {'Δbw':>7} {'state':>7}")
        for c in ranked:
            print(f"{c:28} {mean(c):9.4f} {mean(c)-base:+8.4f} {meanbw(c):8.2f} {meanbw(c)-basebw:+7.2f} {state[c]:7.1f}")
    else:
        print(f"{'rank':>4} {'config':28} {'mean_miss':>9} {'mean_bw':>8} {'state':>7}")
        for i, c in enumerate(ranked[:args.top]):
            print(f"{i+1:>4} {c:28} {mean(c):9.4f} {meanbw(c):8.2f} {state[c]:7.1f}")

    if args.breakdown:
        behs = sorted({b for c in cfgs for b in perwl[c]})
        print("\nper-behavior miss (top configs):")
        print(f"{'config':28} " + " ".join(f"{b[:8]:>8}" for b in behs))
        for c in ranked[:args.top]:
            print(f"{c:28} " + " ".join(f"{perwl[c].get(b,0):8.3f}" for b in behs))


if __name__ == "__main__":
    main()
