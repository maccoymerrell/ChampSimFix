#!/usr/bin/env python3
"""Expand an SPPAM design-space grid spec into a list of named configs.

Grid spec (JSON):
{
  "base": { <param overrides applied to every config> },
  "axes": {
    "<axis name>": [ <value>, <value>, ... ],
    ...
  }
}

Each axis value is either:
  * a scalar  -> sets the param named by the axis (e.g. "pattern_size": [4,6,8]), or
  * an object -> merged into the config, letting one axis set several coupled
    params at once; an optional "_label" names it in the config id
    (e.g. "pattern": [{"pattern_size":6,"min_pattern_size":6,"_label":"p6"}]).

Output is the JSON array consumed by `sppam_dse --configs`. The cartesian
product of all axes is produced; each config gets a "name" encoding its point.

Usage:
  sppam_grid.py spec.json                         # print config array
  sppam_grid.py spec.json --count                 # just the number of configs
  sppam_grid.py spec.json --shard N --outdir DIR  # write DIR/chunk_XXXX.json
"""
import argparse
import itertools
import json
import os
import sys


def axis_assignment(axis, value):
    """Return (overrides_dict, label_str) for one axis value."""
    if isinstance(value, dict):
        ov = {k: v for k, v in value.items() if k != "_label"}
        label = value.get("_label")
        if label is None:
            label = axis + "=" + "_".join(f"{k}{v}" for k, v in ov.items())
        return ov, label
    return {axis: value}, f"{axis}={value}"


def expand(spec):
    base = spec.get("base", {})
    axes = spec.get("axes", {})
    axis_names = list(axes.keys())
    value_lists = [axes[a] for a in axis_names]

    configs = []
    for combo in itertools.product(*value_lists) if axis_names else [()]:
        cfg = dict(base)
        labels = []
        for axis, value in zip(axis_names, combo):
            ov, label = axis_assignment(axis, value)
            cfg.update(ov)
            labels.append(label)
        cfg["name"] = ";".join(labels) if labels else cfg.get("name", "base")
        configs.append(cfg)
    return configs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec")
    ap.add_argument("--count", action="store_true", help="print config count only")
    ap.add_argument("--shard", type=int, metavar="N", help="configs per chunk file")
    ap.add_argument("--outdir", help="directory for chunk files (with --shard)")
    args = ap.parse_args()

    with open(args.spec) as f:
        spec = json.load(f)
    configs = expand(spec)

    if args.count:
        print(len(configs))
        return

    if args.shard:
        if not args.outdir:
            ap.error("--shard requires --outdir")
        os.makedirs(args.outdir, exist_ok=True)
        n = 0
        for i in range(0, len(configs), args.shard):
            chunk = configs[i:i + args.shard]
            path = os.path.join(args.outdir, f"chunk_{i // args.shard:04d}.json")
            with open(path, "w") as out:
                json.dump(chunk, out)
            n += 1
        print(f"{len(configs)} configs -> {n} chunk(s) in {args.outdir}", file=sys.stderr)
        return

    json.dump(configs, sys.stdout, indent=2)


if __name__ == "__main__":
    main()
