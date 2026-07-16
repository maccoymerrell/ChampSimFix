#!/usr/bin/env python3
"""Rank screen configs by mean AVG-LATENCY ratio vs NOPREF (the optimization target).
Reads per-trace CSVs (each containing all configs incl NOPREF) from a dir/glob.
lat_x_nopref = avg_latency / NOPREF avg_latency  (lower = better).
Usage: rank_latency.py 'WORK/csv/*.csv' [--vs CENTER]
"""
import csv, glob, sys, argparse
from collections import defaultdict

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("glob")
    ap.add_argument("--vs", default="CENTER")
    args = ap.parse_args()

    lat = defaultdict(list)     # config -> [lat_x_nopref per trace]
    miss = defaultdict(list)    # config -> [miss_x_nopref per trace]
    perwl = defaultdict(dict)   # config -> {trace: lat_x_nopref}
    state = {}
    files = sorted(glob.glob(args.glob))
    if not files:
        print(f"no files match {args.glob}", file=sys.stderr); sys.exit(1)
    ntraces = 0
    for path in files:
        rows = {r["name"]: r for r in csv.DictReader(open(path))}
        if "NOPREF" not in rows:
            continue
        nl = float(rows["NOPREF"]["avg_latency"])
        nm = int(rows["NOPREF"]["l2_misses"]) / max(1, int(rows["NOPREF"]["demands"]))
        if nl <= 0:
            continue
        ntraces += 1
        wl = path.split("/")[-1].split(".")[0]
        for c, r in rows.items():
            if c == "NOPREF":
                continue
            lr = float(r["avg_latency"]) / nl
            mr = (int(r["l2_misses"]) / max(1, int(r["demands"]))) / nm if nm else 0
            lat[c].append(lr); miss[c].append(mr); perwl[c][wl] = lr
            state[c] = float(r.get("state_kib", 0))

    mean = lambda d, c: sum(d[c]) / len(d[c]) if d[c] else 9
    configs = sorted(lat, key=lambda c: mean(lat, c))
    base = args.vs if args.vs in lat else None

    print(f"# {ntraces} traces. metric = mean(avg_latency / NOPREF avg_latency), LOWER=BETTER")
    if base:
        b = mean(lat, base)
        print(f"# vs {base}: mean_lat={b:.4f}. Δlat<0 = improvement. n_worse = traces made worse vs {base}.")
        print(f"{'config':22} {'mean_lat':>9} {'Δlat':>8} {'mean_miss':>9} {'n_worse':>8} {'state':>7}")
        bl = perwl[base]
        for c in configs:
            dl = mean(lat, c) - b
            nworse = sum(1 for w, v in perwl[c].items() if w in bl and v > bl[w] + 1e-6)
            print(f"{c:22} {mean(lat,c):9.4f} {dl:+8.4f} {mean(miss,c):9.4f} {nworse:8d} {state[c]:7.2f}")
    else:
        print(f"{'rank':>4} {'config':22} {'mean_lat':>9} {'mean_miss':>9} {'state':>7}")
        for i, c in enumerate(configs):
            print(f"{i:4d} {c:22} {mean(lat,c):9.4f} {mean(miss,c):9.4f} {state[c]:7.2f}")

if __name__ == "__main__":
    main()
