#!/usr/bin/env python3
"""Rank the single-core SPPAM sweep by geomean L2C-AMAT ratio vs the no-prefetch base.

Per (config, trace): AMAT = HIT + miss_rate*(L2C miss_lat - HIT). Per config: geometric mean
over all traces of AMAT/base_AMAT (lower = better). Reports the ranking with each config's
knob diff from baseline and trace coverage. Usage: sc_rank.py WORK_DIR [--all]
"""
import json, glob, os, sys, math

HIT = 5.0

def l2c_amat(out):
    try:
        c = json.load(open(out))[0]["roi"]["cache"]["DEFAULT_CACHE"]["L2C_0"]
    except Exception:
        return None
    lh, lm = c["LOAD"]["hit"][0], c["LOAD"]["miss"][0]
    rh, rm = c["RFO"]["hit"][0], c["RFO"]["miss"][0]
    dem = lh + lm + rh + rm
    if dem == 0:
        return None
    mr = (lm + rm) / dem
    return HIT + mr * (c["miss latency"] - HIT)

def main():
    work = sys.argv[1]
    show_all = "--all" in sys.argv
    cfgs = json.load(open(os.path.join(work, "configs.json")))
    outdir = os.path.join(work, "out")
    # base AMAT per trace
    base = {}
    for f in glob.glob(os.path.join(outdir, "base__*.json")):
        t = os.path.basename(f)[len("base__"):-len(".json")]
        a = l2c_amat(f)
        if a:
            base[t] = a
    # per-config ratios
    rows = []
    for cid, knobs in cfgs.items():
        ratios, n = [], 0
        for t, b in base.items():
            f = os.path.join(outdir, f"{cid}__{t}.json")
            if not os.path.exists(f):
                continue
            a = l2c_amat(f)
            if a and b > 0:
                ratios.append(a / b); n += 1
        if ratios:
            g = math.exp(sum(math.log(r) for r in ratios) / len(ratios))
            wins = sum(1 for r in ratios if r < 1.0)
            rows.append((g, cid, n, wins, knobs))
    rows.sort()
    base_knobs = cfgs.get("baseline", {})
    print(f"traces with base: {len(base)} | configs scored: {len(rows)}")
    print(f"{'rank':>4} {'geomean':>8} {'cov':>5} {'win%':>5}  config")
    shown = rows if show_all else rows[:25]
    for i, (g, cid, n, wins, knobs) in enumerate(shown, 1):
        diff = {k: v for k, v in knobs.items() if base_knobs.get(k) != v} if cid != "baseline" else {}
        diff_s = ", ".join(f"{k}={v}" for k, v in sorted(diff.items()))
        print(f"{i:>4} {g:8.4f} {n:>5} {100*wins/n:4.0f}%  {cid:14} {diff_s[:90]}")

if __name__ == "__main__":
    main()
