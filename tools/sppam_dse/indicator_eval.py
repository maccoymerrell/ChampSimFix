#!/usr/bin/env python3
"""Evaluate a config set across the indicator set in OUTCOME terms.

For each indicator trace and each config, reports the L2 demand miss rate
relative to no-prefetch, and the downstream bandwidth (prefetches per demand and
useless prefetches per demand). A "NOPREF" reference config (scanning off) is
added automatically for normalization.

Usage:
  indicator_eval.py CONFIGS.json [--set indicator_set.txt] [--max-records N] [--metric missrate|bw|useless]
"""
import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "sppam_dse")


def load_set(path):
    out = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        label, trace = (parts[0], parts[1]) if len(parts) >= 2 else (os.path.basename(parts[0]), parts[0])
        out.append((label, trace))
    return out


def run(trace, cfg_path, max_records):
    cmd = [BIN, "--trace", trace, "--configs", cfg_path]
    if max_records:
        cmd += ["--max-records", str(max_records)]
    with tempfile.NamedTemporaryFile("w+", suffix=".csv", delete=False) as f:
        out = f.name
    subprocess.run(cmd + ["--out", out], check=True, stderr=subprocess.DEVNULL)
    rows = {r["name"]: r for r in csv.DictReader(open(out))}
    os.unlink(out)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("configs")
    ap.add_argument("--set", default=os.path.join(HERE, "indicator_set.txt"))
    ap.add_argument("--max-records", type=int, default=3000000)
    ap.add_argument("--metric", choices=["missrate", "bw", "useless"], default="missrate")
    args = ap.parse_args()

    cfgs = json.load(open(args.configs))
    names = [c.get("name", f"cfg{i}") for i, c in enumerate(cfgs)]
    # add NOPREF reference
    cfgs = cfgs + [{"name": "NOPREF", "scan_forward": False, "scan_backward": False}]
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(cfgs, f)
        cfg_path = f.name

    label_w = max(len(l) for l, _ in load_set(args.set))
    hdr = {"missrate": "L2 miss rate / no-pref", "bw": "prefetches / demand", "useless": "useless pf / demand"}[args.metric]
    print(f"# metric: {hdr}  (lower is better)")
    print(f"{'behavior'.ljust(label_w)}  " + "  ".join(n[:12].rjust(12) for n in names) + "   nopref_mr")

    for label, trace in load_set(args.set):
        if not os.path.exists(trace):
            print(f"{label.ljust(label_w)}  (missing)")
            continue
        rows = run(trace, cfg_path, args.max_records)
        nop = rows["NOPREF"]
        nop_mr = int(nop["l2_misses"]) / int(nop["demands"])
        cells = []
        for n in names:
            r = rows[n]
            d = int(r["demands"])
            if args.metric == "missrate":
                v = (int(r["l2_misses"]) / d) / nop_mr if nop_mr else 0
            elif args.metric == "bw":
                v = int(r["pf_issued"]) / d
            else:
                v = int(r["pf_useless"]) / d
            cells.append(f"{v:12.3f}")
        print(f"{label.ljust(label_w)}  " + "  ".join(cells) + f"   {nop_mr:9.3f}")

    os.unlink(cfg_path)


if __name__ == "__main__":
    main()
