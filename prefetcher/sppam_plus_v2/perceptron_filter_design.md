# Perceptron prefetch filter — design spec

A single *learned* gate that supersedes every heuristic throttle. All the signals we already
collect (per-IP usefulness/timeliness, depth, engine, spatial position) become **features**; the
final keep/drop/redirect decision is the sign of a hashed-perceptron score. Related work:
PPF (Bhatia et al., ISCA'19). Implementation template already in-tree: the `hashed_perceptron`
branch predictor.

## 1. Where it sits (flow)
```
predictor (SPPAM / SPP / branch-graph) emits candidate {block, engine, depth, trigger_pc, offset}
      -> residency filter  (KEEP as-is: exact, cheap, orthogonal — never make the perceptron
                             waste capacity learning "already cached / in-flight")
      -> build feature vector
      -> score y = sum_i W_i[h_i(features)]
      -> keep if y >= TAU_KEEP     (else drop, but issue 1/N anyway = exploration, see §6)
      -> [optional 3-way] y>=TAU_HI -> L2 ; TAU_LO<=y<TAU_HI -> redirect to LLC ; else drop
      -> enqueue
```
On resolution (demand-hit / evicted-unused / PE settle), **train** the weights that were read at
issue. The perceptron is the LAST gate; the predictors keep their internal confidence only to
decide *what to propose*, not whether to issue.

## 2. Perceptron form: hashed perceptron (not a dense dot product)
A set of small weight tables, one (or a few) per feature; score = sum of the indexed weights;
weights are signed saturating counters (~6 bits, like PPF's 5-bit). Cheap, and matches the
existing BP.

## 3. Features (each -> one small weight table)
Core (the requested set):
| feature | table | size | captures |
|---|---|---|---|
| trigger PC hash | `W_pc` | ~512–1024 | per-PC base reliability (subsumes IP-filter usefulness) |
| page/region offset | `W_off` | bpr (32) | global spatial hot/cold offsets |
| engine (SPPAM/SPP/BG) | `W_eng` | 3–4 | per-engine base trust |
| lookahead depth | `W_dep` | 16 | "deep = less reliable" (subsumes depth throttle) |
| PC usefulness bucket | `W_use` | 4–8 | pre-aggregated per-IP useful fraction |
| PC timeliness bucket | `W_tim` | 4–8 | pre-aggregated untimely fraction (right addr, too early) |

Productive interaction tables (ablate — likely 1–2 of these earn their keep):
`W_pc^dep` (PC XOR depth), `W_pc^off` (PC XOR offset), `W_eng^dep` (engine XOR depth).

Note the deliberate redundancy: `W_pc` will *learn* a PC's usefulness/timeliness, while
`W_use`/`W_tim` feed it pre-aggregated — the buckets generalize faster on cold PCs, the PC table
captures the residual. Keep both only if the ablation says so.

## 4. Output
- Binary keep/drop by `y >= TAU_KEEP`. **Depth needs no separate cap** — a deep block off a
  low-value PC simply scores low and is dropped.
- Optional 3-way (two thresholds) folds in the set-duel L2->LLC redirect.

## 5. Training label — the "outcome = PE?" question
**Recommendation: usefulness is the robust base; PE is the correction — start usefulness-only,
then layer PE in behind a toggle.**
- **Usefulness** (demand-used = +1, evicted-unused = -1): low-noise, trivially attributed (we
  already resolve it via the sample table), is exactly what PPF trains on. Ship this first.
- **PE = I_UPF - I_POLL - I_LAT** (sign): the *true* objective (net latency, our stated target)
  and catches **pollution** that usefulness misses — a "used" prefetch that evicted something more
  valuable is net-negative but usefulness scores it +1. We already sample PE in the glue
  (`pfht_` + `i_upf/i_poll/i_lat`), so the teacher exists.
- **Risk with PE-only:** it is sampled + noisy; sign flips on marginal prefetches would thrash
  the weights. Guard with a **training margin**: only update when `|PE| > THETA_PE` (skip the
  ambiguous middle). This is the same "don't train on low-confidence" idea as the perceptron's own
  `THETA_TRAIN`.
- Practical path: (a) usefulness label working end-to-end; (b) A/B PE-sign vs usefulness as the
  label; (c) if PE wins, consider magnitude-weighted updates (bigger step on large |PE|).

## 6. The selection-bias problem (must handle, or it self-reinforces)
We only observe outcomes of prefetches we ISSUE. A dropped prefetch yields no label, so the gate
can't learn it was actually good -> risk of runaway over-dropping. Fixes:
1. **Exploration trickle (reuse what we have):** issue 1/N of "drop" decisions anyway — the very
   "never fully gate" rule we already use becomes the exploration source of counterfactual labels.
2. **Counterfactual (later):** track dropped blocks; if one is soon demanded, that's a
   "should-have-kept" example -> train toward keep. The residency shadow map already sees some of
   this. Start with (1).

## 7. Cold start
Zero weights -> `y = 0`. Set `TAU_KEEP <= 0` (or a small positive keep-bias) so a cold perceptron
is **permissive** (issue by default) and learns to drop. Optionally hold gating off per-feature
until it has seen K updates.

## 8. Storage
- Weight tables: ~6–8 tables; PC tables ~512–1024 x ~6b, small tables tiny -> ~3–4 KiB.
- Training buffer: extend the (now tiny, 128-entry) sample entry to carry the feature indices
  (~6–8 x ~10b) so resolution updates the right weights -> ~1–1.5 KiB.
- Net ~4–5 KiB, but it **retires** the IP-filter counter arrays + pattern-validation table + SPP
  usefulness counters + set-duel + PE-management throttle -> roughly state-neutral or better.
  (PPF was ~7 KiB total.)

## 9. What it supersedes / keeps
Supersedes: IP-filter (volume + depth + backward gate), SPP-usefulness throttle, set-duel L2->LLC,
per-source PE-management throttle, and — critically — **both redundant IP filters** (predictor's
`pf_sample_`/`ip_*` AND the glue's `pfht_`/`ip_*`): one gate replaces the pair. Pattern-validation
`pv_feed_confidence` is a gray area — it also shapes SPPAM's *prediction* confidence, not just the
gate; start by keeping it internal to SPPAM and let the perceptron own the final decision, then
measure whether it can be dropped.
Keeps: the residency filter (exact, orthogonal), and each engine's internal confidence for
*generating* candidates.

## 10. Prototype plan (DSE first)
1. Add `enable_perceptron_filter` gate in the DSE predictor: feature build -> score -> keep/drop +
   exploration trickle; stash feature indices in the sample entry; train at resolution on
   **usefulness**.
2. Ablate features (drop each, measure), sweep `TAU_KEEP`, `THETA_TRAIN`, weight bits, table sizes.
3. Swap in the **PE-sign** label (from the existing PE sampling) behind a toggle; A/B vs usefulness.
4. Bench vs the current throttle stack: coverage / accuracy / latency + honest state. Target:
   match-or-beat while collapsing the heuristics into one gate.
5. If it wins in DSE, port to the module (it also resolves the two-filter redundancy for free).

## Open questions
- Does PE-sign beat usefulness enough to justify the noisier teacher? (§5)
- Are the pre-aggregated `W_use`/`W_tim` buckets redundant with `W_pc`? (§3 ablation)
- Can pattern-validation be fully retired, or is its prediction-confidence role load-bearing? (§9)
- 3-way (LLC-redirect) output worth it, or does binary + residency suffice? (§4)
