#!/usr/bin/env python3
"""Rank a ChampSim DSE sweep (cs_sweep.py output) by REAL latency.
Per config: single-core -> L2C AMAT; multi-core -> victim (core 0) AMAT + harmonic
speedup of per-core AMAT vs the 'base' (no-pf) config for the same trace-set.
AMAT = hit_latency + miss_rate*(L2C miss latency - hit_latency). Usage: cs_rank.py WORK_DIR
"""
import json, glob, sys, os
from collections import defaultdict
from statistics import harmonic_mean

HIT = 5.0

def l2c_amat(cache):
    lh, lm = cache["LOAD"]["hit"][0], cache["LOAD"]["miss"][0]
    rh, rm = cache["RFO"]["hit"][0], cache["RFO"]["miss"][0]
    dem = lh + lm + rh + rm
    if dem == 0:
        return None, 0.0
    mr = (lm + rm) / dem
    return HIT + mr * (cache["miss latency"] - HIT), 100 * mr

def main():
    work = sys.argv[1]
    # gather per (traceset, config) -> list of per-core AMAT (core order = L2C_0, L2C_1, ...)
    rows = {}
    for out in sorted(glob.glob(os.path.join(work, "out", "*.json"))):
        try:
            top = json.load(open(out))
            j = top[0]["roi"]["cache"]["DEFAULT_CACHE"]
        except Exception:
            continue
        ts, cfg = os.path.basename(out)[:-5].split(".", 1)
        ncore = 0
        amats = []
        while f"L2C_{ncore}" in j:
            a, mr = l2c_amat(j[f"L2C_{ncore}"]); amats.append((a, mr)); ncore += 1
        rows[(ts, cfg)] = amats

    tsets = sorted({ts for ts, _ in rows})
    cfgs = sorted({cfg for _, cfg in rows})
    for ts in tsets:
        ncore = len(rows.get((ts, cfgs[0]), [1]))
        print(f"\n=== {ts} ({ncore}-core) ===")
        base = rows.get((ts, "base"))
        if ncore == 1:
            print(f"{'config':16} {'AMAT':>8} {'miss%':>7} {'vs base':>8}")
            b = base[0][0] if base and base[0][0] else None
            for cfg in cfgs:
                r = rows.get((ts, cfg))
                if not r or r[0][0] is None: continue
                a, mr = r[0]
                rel = f"{a/b:.3f}" if b else "-"
                print(f"{cfg:16} {a:8.2f} {mr:6.1f}% {rel:>8}")
        else:
            print(f"{'config':16} {'victim_AMAT':>11} {'harm_speedup':>13}")
            for cfg in cfgs:
                r = rows.get((ts, cfg))
                if not r: continue
                vic = r[0][0]
                if base and all(b[0] and x[0] for b, x in zip(base, r)):
                    sp = harmonic_mean([b[0] / x[0] for b, x in zip(base, r)])
                else:
                    sp = float("nan")
                print(f"{cfg:16} {vic:11.2f} {sp:13.3f}")

if __name__ == "__main__":
    main()
