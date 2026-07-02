# Design: Full Orchestration Modularization + Operable Cycle-Skip

Status: IMPLEMENTED (see "Implementation notes" at the end for the delta
between this design and what landed; §10 discussion points remain open)
Branch: `frontend_modularization`
Scope: the two work forks requested — (A) fully modular simulation orchestration
around generalized "tokens"/sources/consumers, and (B) the operable sleep/skip
optimization — plus the supporting cleanups they force.

---

## 0. Audit of the current working tree

The five modified tracked files and the untracked source files were reviewed,
built, and tested (all 670 unit tests / 11,757 assertions pass). Verdict: **not
spurious** — every hunk serves a coherent feature set:

| Change | Purpose | Keep? |
|---|---|---|
| `inc/modules.h`: per-instance `dynamic_cast` for `to_operable`/`to_source_consumer` | Lets a *model* be operable/consumer when its *interface* is not (logging_channel, replay_channel). This is a prerequisite of the auto-discovery goal. | Yes |
| `src/environment.cc`, `src/legacy_environment.cc`: null-filter the `view("operable"/"source_consumer")` results | Follows from the above; the legacy `source_consumer` branch previously leaked null pointers that would be dereferenced. Bug fix. | Yes |
| `inc/modules.h`: `prefetcher::intern_` moved to `protected` | Ported upstream prefetchers (ampm, pythia, berti_plus, sppam_plus) query dynamic cache state through it. | Yes |
| `inc/environment.h` + `src/environment.cc` + `src/champsim.cc`: `phase_controller_model` top-level config key | Selected `REPLAY_PHASE_CONTROLLER` for core-less replay runs. **REMOVED** — replay/logging are local (non-shipping) modules; the generic controller covers paced sources, and explicit configs declare controllers as children. | Removed |
| `src/legacy_environment.cc`: `channel_log` / `zstd_level` config keys | Research feature (L2C access-trace capture for the DPC4 sweep). **REMOVED** — an exclave existing solely for a local module; the legacy env is back to the pure old config surface. | Removed |
| Untracked `logging_channel.{h,cc}`, `replay_channel.{h,cc}`, `replay_phase_controller.cc` | Local (non-shipping) research modules. `replay_workload` was the concrete motivation for the token abstraction: it had to `throw` from `next_instruction()` and smuggle its real tokens through a `peek()/pop()` side channel. The files stay as local modules only; `replay_phase_controller.cc` is deleted (redundant under consumer-owned health). | Local only |
| PDFs, `slurm-1582.out`, `test_result.txt`, `pythia_smoke/` | Research artifacts, not code. Candidates for deletion/.gitignore. | Cosmetic |

---

## 1. Pain points the redesign resolves

Verified against the tree (file:line refs in the architecture reports):

1. **Work token hardcoded as `ooo_model_instr`.** `workload_source::next_instruction()`
   returns it; `core_module::push_instruction()` takes it. Non-instruction sources
   cannot use the interface (replay throws).
2. **EOF protocol is crash-prone.** `eof()` must be polled before
   `next_instruction()`, but `bulk_tracereader::eof()` is only true *after* a failed
   read → empty trace = SIGSEGV (verified: `./bin/champsim --config test/config/default.json -w0 -i1 -- /dev/null`
   crashes). Up to 1 buffered instruction is silently dropped at real EOF.
3. **Livelock policy lives in the controller and is instruction-calibrated**
   (thresholds {0.01, 0.02, 0.05} progress/cycle). This forced a whole parallel
   `REPLAY_PHASE_CONTROLLER` and the artificial `return max(progress,1)` liveness
   hack inside `replay_channel::operate()`.
4. **`notify_trace_eof` is phase-global**: the first consumer to hit EOF completes
   *every* entity.
5. **Entity ids are unchecked**: `core_module::entity_index() == get_cpu_num()`;
   `replay_channel` defaults to 0; duplicates silently share a completion slot.
6. **Sources are second-class**: `workload_source` has no `register_interface`;
   submodule-created instances are invisible to `view()`; `num_sources` is counted
   *textually* from JSON before construction.
7. **Run structure has three owners** (main.cc, controller, environment):
   main.cc re-injects CLI trace lists into phases, assumes exactly one
   warmup+one sim phase for the banner, and only honors `.front()` of the
   controller view.
8. **`module_phase` reaches only operables** — `collect_module_phase` dynamic_casts
   the operable view; `interface_registry::get_to_module_phase` is dead code. A
   non-operable module can never see phase boundaries.
9. **Aggregate views are inconsistent**: `view("source_consumer")` works but
   `get_num("source_consumer")` returns 0 (banner prints "Trace sources: 0").
10. **Progress conflates workload with housekeeping**: DRAM refresh emits progress
    with empty queues (in default legacy configs the deadlock abort can
    mathematically never fire); test mocks return constant 1; replay fakes 1.
11. **Every operable ticks every cycle** — no idle/sleep concept; `do_cycle`
    re-sorts and full-operates all operables every global quantum.

---

## 2. Token abstraction ("discrete unit of work")

**Vocabulary**: a *token* is one discrete unit of work flowing from a
*source* (provider) to a *source consumer*. For an OoO core the token is an
instruction; for a replayed memory stream it is an access record; for a network
simulator it would be a packet/flit.

**Mechanism — typed sources under an untyped lifecycle base.** No type erasure
on the hot path; consumers and sources pair on the token type by construction
(the consumer declares which sources it accepts):

```cpp
// Generic lifecycle: everything the ORCHESTRATION layer needs. Token-free.
struct workload_source : module_base<workload_source, source_consumer> {
  [[nodiscard]] virtual bool eof() const = 0;
  virtual std::string describe() const { return {}; }   // e.g. trace path, for stats
  virtual ~workload_source() = default;
};

// Typed pull protocol: everything the CONSUMER needs.
template <typename Token>
struct typed_workload_source : workload_source {
  // Non-destructive: materialize the next token without consuming it.
  // nullptr <=> no token available now (exhausted or not yet generated).
  virtual const Token* peek() = 0;
  virtual void consume() = 0;                 // discard the peeked token
  std::optional<Token> next() {               // one-shot convenience
    if (const Token* t = peek()) { auto v = *t; consume(); return v; }
    return std::nullopt;
  }
};

using instruction_source = typed_workload_source<ooo_model_instr>;
```

- `peek()/consume()` replaces both the crash-prone `eof()`-then-`next_instruction()`
  protocol (a null peek *is* the safe emptiness signal — fixes the empty-trace
  SIGSEGV and the dropped-final-instruction bug) and the replay channel's private
  `peek()/pop()` side channel (paced consumers only consume when a token is due).
- Instruction-specific feedback hooks (`retire_instruction`, `squash_instruction`,
  `branch_mispredict`) move from the generic base onto `instruction_source`
  (no-op defaults kept), since they are statements about instruction pipelines.
- `TRACE_WORKLOAD_SOURCE` becomes an `instruction_source`; `replay_workload`
  becomes a `typed_workload_source<l2c_access_record>` and loses its throw-stub.
- `core_module::push_instruction`/`instructions_requested` remain: they are the
  *core interface* — a core is an instruction consumer, that is its job. The
  generic orchestration layer never touches them (already true today).

**Progress** stays a consumer-defined scalar: `source_consumer::sim_progress()`
counts tokens retired/injected/delivered in whatever unit the consumer defines;
`phase_info.length` is denominated in those units per consumer. This is what
lets an instruction stream into a core and a record stream into a cache mesh in
one run: the controller only ever compares each consumer's own counter against
the phase length.

## 3. Livelock moves into source consumers; deadlock stays generic

`source_consumer` grows a health protocol; the controller keeps only the
generic mechanics:

```cpp
struct source_consumer {
  enum class health { healthy, warning, critical, stalled };
  // Periodic self-check (controller calls every health_period cycles).
  // Consumers judge their own progress rate over the window.
  virtual health check_health() { return health::healthy; }
  // True when this consumer knows more work is scheduled to arrive
  // (e.g. a paced replay gap). Vetoes the zero-progress deadlock abort.
  virtual bool has_pending_work() const { return false; }

  virtual int source_id() const { return -1; }         // was entity_index()
  virtual uint64_t sim_progress() const { return 0; }
  virtual bool source_eof() const { return true; }
  // finish/complete messages unchanged
};
```

- `core_module::check_health()` implements exactly the old controller livelock
  policy (rate over a window vs thresholds), with thresholds/period now
  ModuleBuilder parameters (defaults preserve today's values → identical
  abort behavior for legacy runs).
- `replay_channel::check_health()` returns healthy during recorded gaps and its
  `has_pending_work()` replaces the `return max(progress,1)` hack — the
  deadlock detector aborts only when progress has been zero for N cycles *and*
  no consumer reports scheduled future work.
- The controller consolidates: one generic `PHASE_CONTROLLER` model
  (deadlock + completion + health polling). `INSTRUCTION_PHASE_CONTROLLER`
  stays registered as an alias so existing configs keep working;
  `REPLAY_PHASE_CONTROLLER` is deleted (its only reason to exist is gone).

## 4. Sources, consumers, controllers as first-class config citizens

- **Register every interface** (`workload_source` included) in the interface
  registry, and add a process-wide *module directory*: `create_instance`
  records every constructed instance (interface, name, typed ptr + its
  operable/source_consumer/module_phase/module_stat casts). Environment
  `view()` aggregates delegate to the directory, so **submodule-created
  instances participate**: an operable source nested under a consumer is
  auto-ticked (visibility goal), a `module_phase` consumer that is not operable
  still gets phase hooks (fixes the dead `to_module_phase` path), and
  `get_num()`/`view()` agree (fixes "Trace sources: 0").
- **Connection forms**: submodule nesting (today's form — already supports an
  arbitrary, parameterized number of sources per consumer) stays the primary
  form; `@`-references keep working for peer wiring. Nested instances become
  `@`-referenceable since they now enter the directory.
- **Multiple phase controllers**: the orchestrator runs *all* declared
  controllers each cycle. Each controller may name the source_ids it governs
  (param `sources`, default = all). Combination rules: ABORT if any aborts;
  phase COMPLETE when every controller reports COMPLETE; the phase *list* is
  taken from the first controller with a non-empty `get_phases()` (error if two
  controllers declare conflicting non-empty lists).
- **Per-source EOF**: `notify_trace_eof()` is removed from the controller
  interface. The controller polls each consumer's `source_eof()` itself and
  completes *that* source. A controller param `eof_policy` selects
  `"complete_all"` (default — matches today/upstream: first exhausted trace ends
  the phase for everyone) or `"complete_source"` (heterogeneous runs).
- **Uniqueness**: duplicate non-negative `source_id`s across consumers are a
  construction-time error.

## 5. Orchestrator cleanup (`champsim::main`, `run_phase`, `main.cc`)

- `run_phase` shrinks to: tick clock → `do_cycle` → `controller.advance(progress)`
  → completion notifications. The `per_cycle_hook` (EOF scan) disappears into the
  controller; the completion-message lambdas move behind the consumer interface.
- `phase_info` loses `trace_index`/`trace_names` (trace-centric baggage). Stats
  gets equivalent lines from `workload_source::describe()` via each consumer, in
  source_id order — plain output for legacy runs stays byte-identical.
- `main.cc` keeps full CLI compatibility (`-w/-i`, `$var` substitution, positional
  traces) but stops co-owning run structure: banner counts come from the (fixed)
  aggregates, the phase list comes from controllers or the CLI fallback, and the
  trace list flows to sources only (legacy env) / `$traceN` (explicit).
- `environment_module::get_phase_controller_model()` is absorbed: a fully
  config-declared controller child is the explicit-config path; the legacy env and
  controller-less explicit configs get the generic default controller.

## 6. `cpu` → `source_id` generalization

- Orchestration layer: `entity_index()` → `source_id()`; controller maps,
  completion callbacks, messages, and docs all say *source*. Core keeps
  `get_cpu_num()` (a core-module concept) and implements
  `source_id() = get_cpu_num()` by default, overridable via builder param
  `source_id` in explicit configs.
- Globals: `num_sources` already replaced `num_cpus`; the deprecated `num_cpus`
  alias is retained one more cycle (ship/drrip read `num_sources` already).
- **Data-path field names are retained for now**: `request::cpu`,
  `triggering_cpu` in replacement/prefetcher hooks, `vmem` ASID parameters.
  Renaming them breaks every shipped and third-party module for zero behavioral
  gain; they are documented as "source id of the requestor". Flagged as a
  possible follow-up major-version rename — **discussion point**.
- Width: orchestration ids are `int`/`uint32_t` end-to-end (the `uint8_t`
  bottlenecks in `get_cpu_num`/tracereader remain core/trace-format concerns).

## 7. Legacy environment transparency

The legacy env's contract: old ChampSim config format in, classic OoO-CPU
system out, zero new required keys. Under this design it simply constructs the
same module set it does today (cores + TRACE_WORKLOAD_SOURCEs + caches +
channels + PTW + vmem + DRAM), never exposes token/source/controller options,
and inherits identical defaults (health thresholds = old livelock values,
`eof_policy = complete_all`, generic default controller). CLI validation
(`num_cores` == trace count) is untouched. The `channel_log` research knob is
the one pre-existing deviation (kept, flagged).

## 8. Operable cycle-skip (fork B)

**Hook** (names final at implementation):

```cpp
class operable {
  // Called before each local cycle would be simulated. Return:
  //   0  -> run operate() for this cycle
  //   n  -> skip n local cycles: current_time advances n*clock_period
  //         with no simulation. The operable will not be reconsidered
  //         until the global clock reaches its new current_time.
  virtual long poll_cycle() { return 0; }
};

long operable::operate_on(const clock& clock) {
  long progress{0};
  while (current_time < clock.now()) {
    if (long skip = poll_cycle(); skip > 0) {
      current_time += skip * clock_period;
      continue;
    }
    progress += _operate();
  }
  return progress;
}
```

Default `poll_cycle() = 0` → all existing operables behave exactly as today
until they opt in. A root config switch (`"cycle_skip": false`) forces 0
globally for A/B verification.

**The parent-ticked-hook rule** (why skipping is still behavior-identical):
when a parent module skips its own simulation, any submodule hook that is
contractually per-cycle **must still be ticked from `poll_cycle()`**. Concretely
`CACHE::poll_cycle()` calls `impl_prefetcher_cycle_operate()` on skipped cycles,
so prefetcher-internal clocks (sppam_plus `real_cycle_`), lookahead machines
(ip_stride), and anything issued via `prefetch_line()` observe an unchanged
cycle stream. A prefetch issued during a skipped cycle lands in `internal_PQ`,
the next poll sees a non-empty queue and returns 0 — first tag-checkable cycle
is identical to today's schedule.

**Per-operable skip definitions** (all shipped operables):

| Operable | `poll_cycle()` returns 1 (skip) iff | Must still do while skipping | Notes |
|---|---|---|---|
| `CACHE` | lower `returned` empty ∧ (`lower_translate` null or its `returned` empty) ∧ `inflight_fills`/`inflight_tag_check`/`translation_stash`/`internal_PQ` empty ∧ all upper RQ/WQ/PQ empty | `impl_prefetcher_cycle_operate()`; the `upper_levels` round-robin `rotate` (keeps arbitration alignment with the non-skip baseline) | MSHR-only-pending state is skippable: the wake event is arrival on `returned` |
| `O3_CPU` | every pipeline buffer + ROB + LQ/SQ + `input_queue` empty ∧ both bus `returned` empty ∧ all sources `eof()` | — | i.e. only post-EOF drain; a live core is never idle because `fill_from_sources` refills each cycle. Big win for multi-core runs with early-finishing traces |
| `MEMORY_CONTROLLER` | all upper RQ/PQ/WQ empty ∧ every channel idle (no valid `bank_request`, no `active_request`, RQ/WQ empty, not under/approaching refresh this cycle) | — | skip=1 only: new work can arrive on the upper channels any cycle; refresh timing must fire on the exact cycle it does today |
| `PageTableWalker` | `MSHR`/`finished`/`completed` empty ∧ all upper RQ empty ∧ lower `returned` empty | — | |
| `logging_channel` | always (operate() is a no-op) | — | skip=1 only — its `current_time` timestamps enqueues; it must never run ahead of the global clock |
| `replay_channel` | next record not yet due ∧ own RQ/WQ/PQ… stays 0 until the fake-progress hack is removed (§3), then: skip while `next_due > current` | — | its wake time is exactly known; still capped at 1 to keep `source_eof`/health observation per-cycle |
| `DRAM_CHANNEL` | n/a — not orchestrator-enrolled; parent steps it | — | covered by the MC's idle check |

**Why n>1 is in the API but unused by shipped modules**: any operable whose
work arrives by external push (channel `add_*`, `returned` push) cannot know
locally that nothing will arrive for n cycles; sleeping past an arrival would
add latency and change stats. The n>1 form exists for extension modules with
no external inputs (self-paced generators, timers). The real win is already in
skip=1: a cheap emptiness poll replaces the full operate() walk (bandwidth
object churn, span scans, sort-driven cache pressure).

**Interaction with progress/deadlock**: a skipped cycle contributes 0 progress —
identical to what the idle `operate()` returns today for every shipped operable,
so deadlock/livelock observable behavior is unchanged (and §3's
`has_pending_work` handles the replay-gap case properly).

## 9. Verification plan

1. Unit tests: all existing must pass; new tests for peek/consume protocol
   (incl. empty trace), health policy, per-source EOF policies, multi-controller
   combination, poll_cycle semantics (skip arithmetic, parent-ticked hooks),
   directory-based discovery.
2. **Stat identity, orchestration fork**: baseline binary (pre-change) vs new
   binary, legacy config, DPC4 + SPEC + IPC1 traces, `-w 5000000 -i 5000000`;
   full stdout diff (modulo wall-clock "Simulation time" text and the fixed
   "Trace sources" banner line) + `--json` diff must be empty.
3. **Stat identity, skip fork**: new binary with `cycle_skip` on vs off, same
   runs; diff must be empty. Then off-vs-baseline as a cross-check.
4. Speed: report wall-clock deltas for skip on/off (expect the largest wins on
   memory-idle phases, post-EOF cores, and replay runs).

## 10. Brought forward for discussion (not implemented without a nod)

1. **Data-path renames** (`request::cpu`, `triggering_cpu`, replacement/prefetcher
   signatures) — behavior-neutral but breaks every out-of-tree module. Defer?
2. **Heartbeat generalization**: `Event::RETIRE` payload is
   `deque<ooo_model_instr>` iterators and the Heartbeat listener is cpu-indexed
   and instruction-denominated, living outside the module system. A generic
   `TOKEN_RETIRE {source_id, count}` event would let non-core consumers
   heartbeat — changes output wording, so gated on your call.
3. **DRAM refresh as "progress"**: refresh alone keeps the deadlock detector
   fed (it can never fire in default legacy configs). Correcting this changes
   abort behavior in pathological configs; recommend fixing alongside §3 since
   `has_pending_work` gives us the honest signal, but it is a behavior change.
4. ~~`channel_log`/`zstd_level` in the legacy env config surface~~ — resolved:
   removed along with every other exclave existing solely for the local
   replay/logging modules (`phase_controller_model` knob, the
   `REPLAY_PHASE_CONTROLLER` alias).
5. Artifacts in repo root (PDFs, slurm out, pythia_smoke/): delete or .gitignore?

---

## Implementation notes (what landed)

Everything in §2–§9 is implemented, with these concretizations:

- **Tokens (§2)**: `workload_source` (lifecycle: `eof()`, `describe()`) →
  `typed_workload_source<Token>` (`peek()`/`consume()`/`next()`) →
  `instruction_source` (feedback hooks). `TRACE_WORKLOAD_SOURCE` and the
  replay `TRACE_REPLAY` source are ports; the tracereader protocol is now
  `optional`-shaped end to end (empty-trace SIGSEGV fixed; a repeat-wrapped
  empty trace fails loudly; the final buffered instruction is no longer
  dropped at true EOF — one-instruction behavioral fix in run-to-EOF mode).
- **Health (§3)**: `source_consumer::{check_health(elapsed), reset_health,
  has_pending_work}`; the classic livelock thresholds moved verbatim into
  `core_module::check_health`. One generic `PHASE_CONTROLLER`
  (`INSTRUCTION_PHASE_CONTROLLER` registered as an alias);
  `replay_phase_controller.cc` deleted. `has_pending_work()` is the general
  paced-source liveness signal (any injector with scheduled gaps needs it —
  network traffic, timers); its only in-tree user today is a local module.
- **First-class citizens (§4)**: `workload_source`/`prefetcher`/`replacement`/
  `branch_predictor`/`btb` interfaces registered; nested instances self-enroll
  via `ModuleBuilder::set_owner_of_submodules` +
  `environment_module::enroll_nested_instance` (both environments), appended
  after top-level modules in views so pre-existing operable ordering — and
  therefore behavior — is preserved. `get_num()` now agrees with `view()`
  ("Trace sources: 0" banner bug fixed). Multiple controllers run
  concurrently; a controller's `sources` param partitions governed source ids;
  `eof_policy` selects `complete_all` (classic default) vs `complete_source`.
- **Orchestrator (§5)**: `run_phase` drives N controllers (abort if any,
  complete when all); per-cycle EOF hook and `notify_trace_eof()` removed —
  controllers poll `source_eof()`. `phase_info` reduced to
  {name, is_warmup, length}; trace identity now flows from
  `workload_source::describe()` into the stats (plain/JSON output unchanged).
- **source_id (§6)**: `entity_index()` → `source_id()` throughout; the replay
  channel accepts `source_id` (with `entity_index` as a config alias). Data
  path names retained per §6.
- **Cycle-skip (§8)**: `operable::poll_cycle()` (0 = run, n = skip n local
  cycles), consulted in `operate_on` after entering the cycle so the hook sees
  operate()'s timestamp; root config `"cycle_skip": false` disables globally.
  Implementations: CACHE (idle check; ticks `impl_prefetcher_cycle_operate`
  and the upper-level rotate on skipped cycles), O3_CPU (post-EOF drain only),
  MEMORY_CONTROLLER (upper queues + per-channel
  `DRAM_CHANNEL::would_do_work_at`, incl. refresh due-time and unsettled
  write mode; advances parent-ticked channel clocks on skip), PTW,
  logging_channel (always skip 1), replay_channel (skip through recorded gaps).
- **Verification (§9)**: 681 unit tests pass. Six-scenario matrix (gcc,
  bwaves, bc.kron, client_001, gcc+ip_stride, 4-core mix; 5M+5M): cycle_skip
  on vs off byte-identical with 13.6–24.9% wall-clock gains; three-way
  baseline (pre-change HEAD) comparison run for the combined forks.

Follow-ups not yet done: user-facing docs for the new interfaces (docs/src
never described the orchestration layer).

## Second batch (decisions from §10, all landed)

- **The origin pair (`inc/origin.h`)**: every token/request carries a
  `champsim::origin` — a value object holding the two provenance coordinates
  that the old single `cpu` field conflated. `consumer()`/`cpu()` (aliases:
  the hardware context — dense ids, keys per-consumer tables, stats, phase
  tracking) and `stream()`/`asid()` (aliases: the address space — keys vmem
  and translation). Method-mediated access is the patch seam. Sources stamp
  the pair; a source's `stream_id` defaults to its consumer's id, so classic
  configs are numerically unchanged; cloudsuite records override the stream
  (their asid finally reaches vmem). Replacement hooks take `origin`
  (`origin.cpu()` where `triggering_cpu` was); vmem keys by `origin.asid()`;
  `num_consumers` global replaces `num_sources` (root-key override supported);
  consumer ids are validated unique + dense at startup; `get_cpu_num`,
  `num_cpus`, and the dead `champsim::deadlock` type are removed.
- **ROI flag**: `phase_info{name, is_warmup, roi, length}`; controllers accept
  `"roi"` per phase (default `!is_warmup`); stats collection gates on roi, so
  unmeasured fast-forward phases are expressible.
- **Refresh is not progress**: DRAM refresh housekeeping no longer feeds the
  deadlock detector. The stall protection it provided moved to
  `operable::has_pending_work()` (timer-scheduled work only — refresh in
  flight, busy banks, active dbus); the controller's zero-progress veto
  consults operables as well as consumers. The detector works again in legacy
  configs without spurious aborts behind refresh stalls.
- **Listeners are modules**: `listener` interface (config-declarable,
  `--listeners` instantiates models by name, default `HEARTBEAT` created from
  root `heartbeat_frequency` unless a config declares listeners or
  `--hide-heartbeat` — the flag finally does something). The instruction-typed
  `Event::RETIRE`/global-tuple machinery is deleted; consumers emit generic
  progress (`emit_progress`), and each consumer formats its own heartbeat line
  via `source_consumer::progress_message` — cores produce the byte-identical
  legacy string.
- **Local-module exclaves removed** (`phase_controller_model`,
  `REPLAY_PHASE_CONTROLLER`, legacy-env `channel_log`); replay/logging sources
  are local-only (`.git/info/exclude`) along with the PDF/smoke artifacts.
- **Tests**: externally-driven DUT tests for each new feature — origin
  semantics and end-to-end stamping (086), vmem asid keying (803), cache
  idle-skip with prefetcher-tick continuity and timing equivalence (424),
  DRAM pending-work (703), core health policy (914), ROI phases (915),
  nested-instance discovery (503), plus the earlier poll-cycle (002),
  heartbeat module (020), controller policy (910/911), and token protocol
  (913) suites. 696 cases total.
