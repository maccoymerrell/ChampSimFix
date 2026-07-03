# Simulation Performance Analysis: Where the Cycles Go

Status: Tiers 1, 2A, 2B IMPLEMENTED (commits 0a597614, a5fd98ac, 63a90492);
Tier 3 and the opt-in quiesce policy remain open.
Measured after implementation (5M+5M, byte-identical stats):
gcc_s 167s -> 75s (2.2x), bc.kron 172s -> 58s (3.0x). Every batch was gated
on the full unit suite plus byte-identical stats+JSON diffs.

Design rule adopted during implementation: derived bookkeeping (counters,
bitmaps, change detectors) must live behind the owning class. DRAM_CHANNEL's
RQ/WQ/bank_request and O3_CPU's ROB/LQ are private; inspection goes through
const views (rq(), wq(), bank_requests(), rob(), lq()) and mutation through
APIs that maintain or re-derive the state (insert_rq/insert_wq,
modify_rq/modify_wq, modify_rob/modify_lq). Nothing public can
desynchronize the optimization state.
Method: callgrind instruction-count profiles (gcc_s compute-bound, bc.kron
memory-bound, 4-core mix; `-g` build, identical codegen) + a 40-finding
algorithmic audit of every operate path, each finding adversarially re-verified
against source. perf was unavailable (kernel.perf_event_paranoid=4).

---

## 1. Measured reality

Throughput (5M+5M verified runs, cycle_skip on):

| Scenario | Sim cycles | Wall | Host time / sim cycle |
|---|---|---|---|
| gcc_s 1-core | 21.0M | 167s | 7.9 µs (~53,000 host instructions) |
| bwaves 1-core | 7.6M | 76s | 10.0 µs |
| bc.kron 1-core | 35.0M | 172s | 4.9 µs |
| client 1-core | 6.3M | 53s | 8.5 µs |
| 4-core mix | 39.3M | 1303s | 33.2 µs (≈ 4.2× the 1-core cost) |

Tens of thousands of host instructions per simulated cycle, for a handful of
actual simulation events. The distribution (callgrind, % of total instructions):

| Cost center | gcc_s | bc.kron | What it is |
|---|---|---|---|
| O3 backend stages (schedule+execute+complete+lsq, incl. their STL expansion) | ~30% | ~37% | full-structure scans per cycle |
| `champsim::bandwidth` calls | ~10% | ~10% | out-of-line trivial methods |
| `RegisterAllocator` accessors | ~9% | ~7% | out-of-line trivial methods + per-entry re-polls |
| malloc/free/new | ~6% | ~5% | copy-not-move value flow |
| CACHE::operate + poll | ~2% | ~4% | per-cycle rescans |
| frontend stages, DRAM, PTW, orchestrator | remainder | | |

Call frequencies (gcc_s, per simulated cycle, 1-core / 9 operables):

- `bandwidth::has_remaining()` — **950 calls/cycle** (out-of-line, in
  src/bandwidth.cc, so every check is a real call that itself calls
  `amount_remaining()`)
- `RegisterAllocator::count_free_registers()` — 173/cycle,
  `isAllocated` 167/cycle, `isValid` 85/cycle (all out-of-line)
- malloc + free — **36 heap operations/cycle**

The stalled-cycle picture (audit, ROB full, head load waiting on DRAM, zero
events this cycle): complete_inflight walks 352 ROB entries, execute walks 352
(re-polling register validity of unchanged registers), schedule walks up to 352
(recomputing register-need for already-scheduled entries), operate_lsq visits
all 128 LQ capacity slots, frontend prefix re-scans ~100-190 — **~1,100–1,300
container visits plus ~1,000 array loads to conclude "nothing happened"**, every
cycle, for the entire 200–400-cycle miss. bc.kron retires 0.17 IPC: it pays
this nearly every cycle, which is why a workload doing almost nothing per cycle
still costs 5 µs each.

The multi-core multiplier, measured in the 4-core mix: phase completion waits
for the slowest consumer (bc.kron), and completed cores keep executing at full
price — cpu3 retired **41.7M instructions against a 5M quota** (8.3×), cpu1
25.9M (5.2×). 90.5M instructions were simulated to measure 20M: a 4.5× pure
work multiplier on top of the per-cycle overhead, and those cores are never
idle so `poll_cycle` cannot help them.

## 2. The systemic flaw

Per-cycle cost is proportional to structure **capacity**, not to **work done**.
Four reinforcing patterns:

1. **Scan-per-cycle instead of track-on-change.** Every stage re-derives "what
   is ready" each cycle by walking its container: three full ROB walks (the
   deque's chunk-map indirection per step), a 128-slot LQ walk (including empty
   `optional` slots, with no break when bandwidth is exhausted), DRAM occupancy
   recomputed by `count_if` over full RQ+WQ every cycle, `min_element` over all
   bank slots and over full capacity-sized request vectors, a refresh loop over
   every bank every cycle, cache translation sweeps re-visiting every in-flight
   entry (`issue_translation` re-scans entries already issued; `extract_if`
   re-tests entries whose event_cycle hasn't arrived). Tellingly, the
   event-wakeup fields on `ooo_model_instr` (`num_reg_dependent`,
   `registers_instrs_depend_on_me`) are **dead code in this fork** — wakeup was
   replaced by per-cycle polling and the fields were never removed.
2. **Trivial accessors out-of-line.** `bandwidth`, `RegisterAllocator`,
   `chrono::clock` methods live in .cc files: 100–1000 real function calls per
   cycle for one-instruction bodies the compiler would erase if it could see
   them. Same class: channel queue accessors re-resolved through the vtable
   15–20 times per cache per cycle for deques that are stable members.
3. **Copy-not-move value flow.** Each instruction is fully copied (≈5 heap
   vectors) twice on the input path alone; each packet is copied 2–3× per queue
   hop (`do_add_queue`, `add_pq`, response construction); `stable_partition`
   on 1–6-element windows allocates a temporary buffer per call. Result: 36
   heap ops per simulated cycle.
4. **No completion latch.** A drained core (multi-core, finished quota with
   EOF'd sources) still pays the full 7-deque + 128-slot poll every cycle; and
   under the default (upstream-compatible) completion policy it doesn't drain
   at all — it keeps running the trace at full cost until the slowest core
   finishes.

This is also exactly why `poll_cycle` disappointed: skipping only helps a
module with *nothing* in flight. The hot modules always have something in
flight — the waste is inside their operate loops, proportional to capacity.

## 3. Fix plan (ranked, identity-first)

All fixes below were audited with explicit invariant arguments; the standard
verification (unit suite + byte-identical 5M+5M matrix vs. pre-change binary)
gates each batch. No event-driven rewrite anywhere — the cycle loop stays; the
work inside it becomes proportional to occupancy/ready-work.

**Tier 1 — mechanical, provably identical (est. 20–30% combined):**
- Inline `bandwidth`, `RegisterAllocator`, `chrono` methods (move to headers).
- Move-not-copy: instruction input path (`push_instruction`,
  `initialize_instruction`), packet queue hops, `transform_while_n` with move
  iterators; delete the dead `channels_bandwidth_consumed` debug vector and the
  dead wakeup fields.
- Hoist invariants: `HIT_LATENCY/clock_period` division (per cache per cycle),
  DRAM watermarks, `pref_activate_mask` test, lru_table `%`→mask, `clock.now()`
  in `operate_on`.
- Cache channel deque references per phase (kill the per-cycle vtable churn in
  CACHE, MC `initiate_requests`, PTW).
- Skip translation sweeps when `lower_translate == nullptr` (L2C/LLC never
  translate; guard is airtight).
- Trivial loop repairs: `operate_lsq` break on exhausted bandwidth,
  `promote_to_decode` redundant pre-scan removal, `do_execution` guards for
  instructions with no memory ops, dispatch's per-iteration 128-slot free-LQ
  `count_if` → maintained counter, drained-core latch in `O3_CPU::poll_cycle`.

**Tier 2 — incremental state with verified invariants (the big one; est. 2–3×
on top of Tier 1 for the core-bound case):**
- The three ROB walks: first-unscheduled/first-unexecuted prefix caches +
  change detectors (scan only when a completion/schedule/ready-time event since
  last cycle can have changed the answer), completion bitmap scanned with ctz.
  The scheduler's register-scarcity break must keep its exact semantics
  (documented quirk: it currently indexes the RAT with renamed physical ids —
  behavior-bearing, preserved bit-for-bit).
- LQ occupancy bitmaps (lowest-free-index preserved), per-address
  youngest-store index for forwarding, block→slot index for memory returns.
- DRAM: occupancy counters (kills `swap_write_mode`'s 128-visit count_if),
  unchecked-entry counters for the collision scans, refresh-pending counter,
  `populate_dbus` min-ready lower-bound cache, skip `schedule_packet` when
  nothing unscheduled (all verified as provable no-op skips).
- `event_counter` single-lookup increment (currently two searches per stat
  event).

**Tier 3 — needs care, do last with dedicated A/B:**
- DRAM address-field memoization on requests (the scheduler comparator is a
  non-strict `<=`; scan order must not change).
- In-place small stable-partition to kill hidden temp-buffer allocations.
- Skipped: operables-sort memoization (~ns yield, real reordering risk), DIB
  same-tag fill skip (subtle, marginal).

**Multi-core-specific (opt-in, changes semantics):** a completion policy for
finished consumers — `on_complete: "continue"` (default, upstream-compatible:
finished cores keep generating contention) vs `"quiesce"` (stop fetching at
quota, drain, then the drained latch makes the core nearly free). In the
measured mix this alone is a ~4.5× total-work reduction. Not byte-identical by
design — the co-runner contention seen by slow cores changes — so it ships as
an explicit config option, never a default.

## 4. Expected outcome

Tier 1 + Tier 2 attack ~55–65% of measured host work with mechanisms whose
per-cycle cost is O(ready work) instead of O(capacity); conservative estimate
**2–4× single-core**, with the same factor carrying to multi-core, and the
quiesce option offering a further multiple for imbalanced mixes. Full findings
(40, with per-finding cost models, fix invariants, and identity-risk notes)
are preserved in the audit record.
