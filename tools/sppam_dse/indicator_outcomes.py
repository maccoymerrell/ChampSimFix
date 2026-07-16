#!/usr/bin/env python3
"""Print indicator-set results from an aggregated all.csv in OUTCOME terms.

Maps each trace to its behavior label (from indicator_set.txt), normalizes the L2
demand miss rate against the NOPREF config, and tabulates miss-rate-x-nopref and
bandwidth (prefetches/demand) per behavior x config. The config set must include
a config named NOPREF (scanning off) for normalization.

Usage: indicator_outcomes.py all.csv [--set indicator_set.txt]
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
            base = os.path.basename(p[1]).replace(".bin.zst", "")
            m[base] = p[0]
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("all_csv")
    ap.add_argument("--set", default=os.path.join(HERE, "indicator_set.txt"))
    args = ap.parse_args()

    lab = labels(args.set)
    by_trace = defaultdict(dict)  # trace -> {config: row}
    names = []
    for r in csv.DictReader(open(args.all_csv)):
        by_trace[r["trace"]][r["name"]] = r
        if r["name"] not in names:
            names.append(r["name"])
    cfgs = [n for n in names if n != "NOPREF"]

    # state per config
    sample = next(iter(by_trace.values()))
    print("state/config (KiB, lean): " + "  ".join(f"{n}={float(sample[n]['state_kib']):.1f}" for n in cfgs))

    def table(title, fn):
        w = max((len(lab.get(t, t)) for t in by_trace), default=8)
        print(f"\n# {title}  (lower is better)")
        print(f"{'behavior'.ljust(w)}  " + "  ".join(n[:11].rjust(11) for n in cfgs) + "   nopref_mr")
        for t in sorted(by_trace, key=lambda x: lab.get(x, x)):
            rows = by_trace[t]
            if "NOPREF" not in rows:
                continue
            nop = rows["NOPREF"]
            nop_mr = int(nop["l2_misses"]) / int(nop["demands"])
            cells = "  ".join(f"{fn(rows[n], nop_mr):11.3f}" if n in rows else f"{'-':>11}" for n in cfgs)
            print(f"{lab.get(t, t)[:w].ljust(w)}  {cells}   {nop_mr:9.3f}")

    table("L2 miss rate / no-pref", lambda r, nop: (int(r["l2_misses"]) / int(r["demands"])) / nop if nop else 0)
    table("prefetches / demand", lambda r, nop: int(r["pf_issued"]) / int(r["demands"]))


if __name__ == "__main__":
    main()
