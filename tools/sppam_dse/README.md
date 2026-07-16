# sppam_dse — trace-driven SPPAM design-space evaluator

A fast, standalone tool for exploring the **SPPAM** prefetch *prediction
mechanism* against the raw L2C access traces produced by the `LOGGING_CHANNEL`
module (`../../scripts/decode_l2c_trace.py` documents the trace format). It
reimplements the original SPPAM prediction core (region access-maps → scraping →
confidence-pattern prediction → lookahead → usefulness-driven degree) with the
timing/bandwidth/dueling/fairness machinery removed, all knobs exposed as runtime
parameters, so you can sweep the design space in seconds instead of full
cycle-accurate simulations.

## Build

```
make            # produces ./sppam_dse  (needs g++ C++17 and the zstd CLI on PATH)
```

## What it models

- **L2** (the prefetch target, default 2048×16 LRU) per core.
- **LLC** (residency filter, default 4096×12) and a **DRAM bandwidth→latency
  curve** — *shared* across cores. Each cache line carries a `ready_at` cycle, so
  coverage is **timeliness-aware** (a late prefetch gives partial credit) and the
  shared DRAM curve captures bandwidth contention.
- **N cores**, each with a private L2 + SPPAM predictor, sharing the scaled LLC
  and DRAM — for multi-core interaction studies.

A paired **no-prefetch baseline** (same geometry) defines the demand-miss set and
the no-prefetch latency.

## Metrics (CSV columns)

- `coverage` = baseline demand-misses turned into *timely* config hits / baseline misses.
- `coverage_incl_late` = also counts late (in-flight) prefetch hits.
- `accuracy` = useful prefetches / prefetches issued (useful = prefetched line later demand-hit before eviction).
- `pf_per_demand` = prefetch overhead.
- `avg_latency`, `baseline_avg_latency`, `latency_saved` = modeled demand latency with vs without SPPAM.

## Usage

```
# Single core, default config
./sppam_dse --trace run.l1d_l2c.bin.zst

# A grid of configs in one pass (one decompression), to CSV
./sppam_dse --trace run.bin.zst --configs grid_configs.json --out results.csv

# Multi-core: N traces -> N cores sharing the (scaled) LLC + DRAM
./sppam_dse --traces c0.zst,c1.zst,c2.zst,c3.zst --configs grid_configs.json --out r.csv

# Fast iteration: cap records
./sppam_dse --trace run.bin.zst --max-records 200000
```

A config file is a JSON array of objects; each overrides any subset of the fields
in `params.h` (and gets a `name`). Example:
```json
[
  {"name": "orig"},
  {"name": "fixed_window", "scrape_full_window": true},
  {"name": "aggressive", "scan_distance_forward": 32, "min_confidence_to_prefetch": 40}
]
```

## The six design axes → parameters

| Axis | Parameters (see `params.h`) |
|---|---|
| Scraping triggers | `scrape_on_{idle,count,evict}`, `scrape_idle_time`, `scrape_{min,access}_count`, `*_after_scrape`, `scrape_full_window` |
| Pattern resolution | `pattern_size`, `min_pattern_size`, `table_or_counter`, `min_confidence_to_prefetch` |
| Table sizes | `pattern_table_{sets,ways}`, `pattern_conf_{sets,ways}`, `cpt_{sets,ways}` |
| Region size | `page_bits`, `region_{sets,ways}` |
| Directionality | `do_negative`, `separate_negative_tables`, `scan_{forward,backward}`, `scan_distance_*`, `*_momentum_min` |
| Aggression | `min_confidence_to_prefetch`, `prefetch_degrees_usefulness`, `lookahead_*`, `prob_drop_prefetches`, `prefetch_drop_chance_usefulness`, `prefetch_to_l2_degree` |

Cache/DRAM model: `l2_*`, `llc_*`, `llc_scale_with_cores`, `l2_hit_latency`,
`llc_hit_latency`, `dram.{base_latency,service_cycles,tau,util_cap}`,
`enable_timing` (false → pure binary coverage). Input handling:
`train_on_prefetch`, `count_prefetch_as_demand`, `include_translation`,
`include_writeback`.

### `scrape_full_window` — the alignment fix

The original SPPAM caps the scrape window at the region size, which
truncates/misaligns access patterns in the last ~`2*pattern_size` blocks of each
region and never trains forward-shadow (cross-boundary) patterns — so those
entries are trained under one bit-encoding and applied under another.
`scrape_full_window: false` (default) reproduces the original; `true` extends the
window over the full shadow map (the fix). It is exposed as a knob so the bug's
effect can be measured per workload.

## Sweeping (Slurm)

```
# 1. Define a grid (cartesian product over axes); see grid_example.json
# 2. Submit: one array task per (trace x config-chunk), all chunks resumable
SPEC=grid_example.json bash submit_sppam_sweep.sh

# Multi-core sweep: GROUPS_FILE has one comma-separated trace-set per line
SPEC=grid.json GROUPS_FILE=sets.txt bash submit_sppam_sweep.sh

# 3. Combine per-task CSVs -> all.csv (long) + summary.csv (per-config means)
python3 aggregate_sppam.py <WORK>/csv
```

Key knobs (env): `WORK`, `TRACES_DIR`/`TRACES_GLOB`, `GROUPS_FILE`, `CHUNK`
(configs per task — bounds memory), `MAXPAR`, `MAX_RECORDS`, `PARTITION`,
`DRY_RUN`. The grid expander (`sppam_grid.py`) supports object-valued axes (one
axis sets several coupled params, with a `_label`).

## Files

| File | Role |
|---|---|
| `trace_reader.h` | streaming `.bin.zst` reader |
| `lru_table.h`, `conf_table.h` | pointer-based LRU / confidence tables |
| `cache.h`, `dram_model.h` | L2/LLC model with `ready_at`; DRAM BW→latency curve |
| `params.h` | all runtime knobs + JSON loader |
| `sppam_predictor.h` | the SPPAM prediction core |
| `evaluator.h` | core / shared-mem / system (multi-core) + metrics |
| `main.cc` | CLI, k-way cycle merge, one-pass multi-config |
| `sppam_grid.py`, `submit_sppam_sweep.sh`, `run_sppam_array.slurm`, `aggregate_sppam.py` | sweep harness |
