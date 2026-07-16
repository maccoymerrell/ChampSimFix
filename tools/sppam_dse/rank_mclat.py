#!/usr/bin/env python3
"""Rank throttle configs on multi-core victim mixes by LATENCY.
Per-core speedup = base_avg_latency / avg_latency (>1 = faster than no-pref).
Reports mean HARMONIC speedup (system fairness, victim-sensitive) and mean VICTIM
(core-0) speedup across mixes. Higher = better (= lower latency). Usage:
  rank_mclat.py 'csv/*.csv' [--vs CENTER]
"""
import csv, glob, sys, argparse
from collections import defaultdict
from statistics import harmonic_mean

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("glob"); ap.add_argument("--vs", default="CENTER")
    a = ap.parse_args()
    harm = defaultdict(list); vic = defaultdict(list); sysl = defaultdict(list)
    files = sorted(glob.glob(a.glob))
    if not files:
        print(f"no files: {a.glob}", file=sys.stderr); sys.exit(1)
    for path in files:
        rows = list(csv.DictReader(open(path)))
        bycfg = defaultdict(list)
        for r in rows: bycfg[r["name"]].append(r)
        for cfg, rs in bycfg.items():
            if cfg == "NOPREF": continue
            rs = sorted(rs, key=lambda r: int(r["core"]))
            sp = [float(r["base_avg_latency"]) / float(r["avg_latency"]) for r in rs]
            harm[cfg].append(harmonic_mean(sp)); vic[cfg].append(sp[0])
            # system-wide latency ratio (total demand-latency weighted) vs baseline
            tot = sum(float(r["avg_latency"]) * int(r["demands"]) for r in rs)
            btot = sum(float(r["base_avg_latency"]) * int(r["demands"]) for r in rs)
            sysl[cfg].append(btot / tot if tot else 0)
    m = lambda d, c: sum(d[c]) / len(d[c]) if d[c] else 0
    cfgs = sorted(harm, key=lambda c: -m(harm, c))
    base = a.vs if a.vs in harm else None
    n = len(files)
    print(f"# {n} mixes. speedup=base_lat/lat (HIGHER=better). harm=fairness, vic=core0 victim, sys=aggregate")
    if base:
        bh, bv, bs = m(harm, base), m(vic, base), m(sysl, base)
        print(f"# vs {base}: harm={bh:.3f} vic={bv:.3f} sys={bs:.3f}")
        print(f"{'config':16} {'harm':>7} {'Δharm':>7} {'victim':>7} {'Δvic':>7} {'sys':>7}")
        for c in cfgs:
            print(f"{c:16} {m(harm,c):7.3f} {m(harm,c)-bh:+7.3f} {m(vic,c):7.3f} {m(vic,c)-bv:+7.3f} {m(sysl,c):7.3f}")
    else:
        print(f"{'rank':>4} {'config':16} {'harm':>7} {'victim':>7} {'sys':>7}")
        for i, c in enumerate(cfgs):
            print(f"{i:4} {c:16} {m(harm,c):7.3f} {m(vic,c):7.3f} {m(sysl,c):7.3f}")

if __name__ == "__main__":
    main()
