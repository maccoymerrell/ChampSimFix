#!/usr/bin/env python3
"""Summarise a sweep: cycles to complete identical work, and why."""
import re
import sys
import os

def cycles(path):
    with open(path) as handle:
        found = re.findall(r"cycles:\s*(\d+)", handle.read())
    return int(found[-1]) if found else None

def scalar(path, pattern):
    with open(path) as handle:
        match = re.search(pattern, handle.read())
    return int(match.group(1)) if match else 0

def main():
    results = sys.argv[1] if len(sys.argv) > 1 else "nmfc_results/results"
    print(f"{'workload':<14}{'NMFC cyc':>12}{'base cyc':>12}{'speedup':>10}"
          f"{'invocations':>13}{'migrations':>12}{'mig/inv':>9}")
    print("-" * 82)
    for workload in ("scatter", "partitioned"):
        nmfc = os.path.join(results, f"{workload}_nmfc.txt")
        base = os.path.join(results, f"{workload}_baseline.txt")
        if not (os.path.exists(nmfc) and os.path.exists(base)):
            continue
        n, b = cycles(nmfc), cycles(base)
        invocations = scalar(nmfc, r"fn_fabric DISPATCHED:\s*(\d+)")
        migrations = scalar(nmfc, r"MIGRATED:\s*(\d+)")
        per = migrations / invocations if invocations else 0
        print(f"{workload:<14}{n:>12,}{b:>12,}{b / n:>9.2f}x"
              f"{invocations:>13,}{migrations:>12,}{per:>9.1f}")

if __name__ == "__main__":
    main()
