# SAMPLING INFRASTRUCTURE

**Proposal, 2026-09-05. Read-only work: nothing was built, nothing was run, nothing under
`src/nmfc`, `src/sst-elements` or `CANON.md` was modified.** Three designs were written
against a four-part survey and each was put through three verification lenses (canon, statistics
and validity, engineering). This document is the decision paper: the ruling, what an image must
contain, the three designs with their verdicts, a recommendation, the open questions, and a work
plan for the recommendation.

Every fact below is either read out of a file in this checkout, recomputed here from measured
numbers in the results tree, or **flagged `UNVERIFIED` in place**. Sizes, counts and thresholds
are configuration, not design; where a number appears it is an input to be measured, not a
commitment.

---

# 1. THE RULING, AND WHAT IT ASKS FOR

The user's words, 2026-09-05, verbatim and highest authority:

> "beyond a certain point, running end-to-end is going to stop being possible for work sizes.
> At that point, we need to be prepared with infrastructure for either simpoints or smarts
> (smarts is probably better here, assuming we can write an image of the entire system and
> play-forward at a large number of small intervals). Simpoints are probably better when making
> larger changes, since making an image compatible across large changes is likely more trouble
> than it is worth. Again, this is when we are dealing with > 2B instructions per program."

Decomposed, the ruling asks five separable questions. They are separable, and the whole value of
this paper is in keeping them apart.

| # | question the ruling asks | where it is answered |
|---|---|---|
| Q1 | What is in an image of this machine, and where may one be taken? | Section 2 |
| Q2 | Who writes an image (the producer), and how fast? | Section 3, per design |
| Q3 | Who reads one (the consumer), and what does it have to warm? | Section 3, per design |
| Q4 | Does an architectural image remove the compatibility objection to SMARTS? | Section 4.3 |
| Q5 | Where is the 2B threshold, in this machine's own sweep? | Section 4.4 |

The ruling's own reasoning is correct on Q4's premise and, this paper argues, inverted on its
conclusion. That is the single most important finding here and it is set out in 4.3.

---

# 2. WHAT AN IMAGE OF THIS MACHINE MUST CONTAIN

## 2.1 The distinction, stated once

**An ARCHITECTURAL image carries only what the instruction set can observe.** For this machine
that is: the host's committed integer and floating-point registers and its floating-point status
state, the committed program counter, the eight 512-bit **bit-packed** context registers
`ctx0..ctx7` (`NMFC_NUM_CTX_REGS` 8, `NMFC_CTX_BITS` 512, `nmfc_isa.h:308-309`), the tracking
unit's entries (architecturally readable, because `FORKQ` answers `TrackingUnit::freeEntries()`
and `JOINQ` answers per handle), memory contents, and the address-space mapping. It survives
every microarchitectural change so long as the ISA, the binary and the memory layout are
unchanged. It requires warm-up on restore.

**A MICROARCHITECTURAL image carries the structures that make the machine fast:** cache tags and
data, TLBs, the BTB, the MOESIF directory's sharer sets, in-flight fabric transactions, MSHRs,
queue occupancies, resident tile-context pipeline state, the reorder buffer and load/store queue,
ramulator2's bank and row state, and the NUCA policy's evidence tables. It restores in O(1) with
no warm-up and is invalidated by any change to the structures it encodes.

**The brittleness the ruling is worried about is a property of WHAT is saved, not of WHICH
sampling method saves it.** Both SMARTS and SimPoint can be driven from either image class. This
sentence is the pivot of the whole paper.

## 2.2 The quiesce rule, stated once

An image point is legal only where the state it omits is not needed. For this machine that gives
one rule with two admissible strengths.

**Class Q (drained, FTU-empty).** The host stops forking, JOINs every join-expected handle, then
polls `FORKQ` until free equals capacity. That one test covers both retirement modes, because
`freeEntries()` is true machine state precisely because fire-and-forget entries are freed by an
ACK the instruction stream cannot see (`NMFCTracking.h:99-105`). FTU-empty then implies, by
construction: no live tile context; no migration in the fabric (a migrating context lives in no
component's context array, canon H.8, the slot is released at departure); no unlanded tile store
(`RETC`/`ENDC` with `pendingStores > 0` parks the context in `DRAINING` and `finishContext` runs
only when the last store response arrives, canon H.6); and no context-owned held word, because
`finishContext` releases the line before it sends the completion, which matters because canon H.7
keeps an atomic's word **above** the data cache on purpose and a cache flush would miss it.
**No new instruction is needed and none is added.** Canon I.7 records that a COMMIT and a WAIT
are deliberately absent under ruling R14, and this rule uses only instructions that exist.

**Class L (live, in-flight carried).** Every additional requirement of class Q is dropped except
the ones that have no representation: no `GrainMove` in FLUSH/COPY/REMAP (a half-copied grain is
not representable as memory plus a mapping), no context mid-instruction, no held word, no pending
fault handler, no outstanding kill-ACK. In exchange the image carries the tracking unit's live
entries and the outstanding invocations.

**Neither ruling O7's fatal-fault teardown nor `KILL` can be used to reach a quiesce point.**
Both close outstanding entries with a zeroed register file and an error flag. They destroy the
invocation's values, however convenient the bulk closure looks.

## 2.3 The state inventory

| carried in an architectural image | rebuilt by warm-up | never exists in an image |
|---|---|---|
| Host integer + FP registers, read through the retire ISA table | Host ROB, rename and issue ISA tables, LSQ, decoders, branch predictor | The producer's own microarchitecture |
| `fp_flags` / fcsr accrued-exception and rounding state | Host L1I/L1D/L2 contents | |
| Committed PC, per hardware thread | Tile L1I/L1D lines, tags, coherence states, MSHRs, `pinned_` | |
| `ctx0..ctx7`, 512 bits each, carried as opaque bits | The four memHierarchy LLC slices | |
| The tracking unit, entry by entry, with its state, retirement mode, gen, hart, returned 512 bits and error bit | The MOESIF directory (restores all-Invalid: `DirLine` is `{global, owner, forwarder, sharers}` and line bytes appear at the fabric only inside a live `Txn`) | |
| (Class L only) the outstanding-invocation set: handle, asid, PC, 512 bits, wantsReturn, retirement | Per-tile TLBs and BTBs, and the walk output latch | |
| Memory contents, keyed by (asid, **virtual** address) | All fabric queues, transactions, `blocked_` deques, `alias_`, per-tile credits | |
| The region declaration list, verbatim | Every microarchitectural field of `TileContext` beyond the 512 bits and the PC | |
| Demand-paging `present` bits per region | The atomic table `held_` with its waiter deques | |
| OS-side `brk`, region map, fd table, tid address, **by value** | ramulator2's bank, row-buffer and scheduler state (it has no serialization path, and needs none) | |
| A header: format version, ELF build id, simulator build hash, geometry digest | The NUCA policy's evidence tables | |

## 2.4 The one architectural gift, and the one architectural debt

**The gift.** `PageTable::tileOfPhysical(pa)` is `((pa & ~modeBit)/grain) % tiles` for
grain-partitioned space and `(pa/block) % tiles` for the host block-interleaved half
(`NMFCPageTable.h:279-289`). It reads nothing but the address and configuration constants. So an
image restored at the same physical addresses under the same declarations puts every byte on the
tile it was on, with **no placement table for data at all**. This is exactly why PHYSICAL-address
partitioning was ruled and virtual-address partitioning rejected: it makes the image
self-describing about placement.

**The debt.** In the SST implementation the VA to PA map is a C++ object, not memory.
`regions_`, each region's home tile and `frameBase`, and `vtileHome_` are produced
deterministically by `place()`/`allocate()` from the declaration list, so they can be REBUILT and
must not be carried. But `remap_`, `remapSlot_`, `generation_`, `Region::present` and the fabric's
`alias_` are consequences of what the run did and nothing rebuilds them. Architecturally the page
table is on duplicate pages and would ride the memory image for free (canon I3); in SST the walk
touches page-table addresses only to charge cache and channel occupancy and resolves from the C++
object (`NMFCTile.cc:1898-1948`). **Any claim that "the memory image carries the mapping" is true
of the architecture and false of the implementation. Say which you mean, every time.**

## 2.5 A defect that must be fixed before any format is frozen

The fabric sends a `RemapEvent` to every tile control link and one to `remapHost_`, and in the
out-of-order configuration `remapHost` is wired **only to the data MMU**
(`NMFCCoherenceFabric.cc:1735-1750`; `vanadis-nmfc.py:185`). The instruction MMU and the node-OS
MMU never apply a grain move, so after any NUCA move the three host copies of the page table are
no longer identical. That is benign today only because `PageTable::movable()` restricts moves to
STRIPED regions and those two MMUs touch duplicate and host pages. An image format that saves one
table and restores three papers over the divergence; one that saves three preserves it. **Wire all
three to the broadcast, then save one.** This is a change to the machine made for the image's sake
and it is reviewed on its own merits, not slipped in as image plumbing.

## 2.6 The image is the memory image

| component of the image | BFS 32G | shuffled-sum 64G |
|---|---:|---:|
| Memory contents (touched, not declared) | ~33 MiB | ~65 MiB |
| Tracking unit, 256 entries at ~65 B | 16.25 KiB | 16.25 KiB |
| Outstanding set, at most 256 at ~88 B (class L only) | 22 KiB | 22 KiB |
| Tile contexts, class L, 4 x 128 at 72 B architectural | 36 KiB | 36 KiB |
| `ctx0..ctx7` | 512 B | 512 B |
| Host architectural registers | ~0.5 KiB | ~0.5 KiB |
| Region declarations, `remap_`, present bits | a few tens of KiB | a few tens of KiB |
| **everything that is not memory** | **~150 KiB** | **~150 KiB** |

Three orders of magnitude. **Design the format around a memory dump with a small structured
header, and do not let the header design drive the schedule.** Never size an image from
`memSize`: the configuration declares 4 GiB minus 1 (`vanadis-nmfc.py:54`) and touches 33 to 65
MiB. Note also that `BackingMalloc` allocates on READ as well as write, so a stray probe of
untouched memory permanently adds a 1 MiB granule to every subsequent image; image-size drift at a
fixed working set is a symptom, not noise.

---

# 3. THE THREE CANDIDATE DESIGNS

Common ground, established by the survey and not in dispute between the three:

- **SST-native checkpointing cannot be used today, and the gap is not marginal.** None of the nine
  NMFC element classes implements `serialize_order` (only the 27 wire Event classes do), Vanadis
  has none across 250 sources, and `ramulator2Memory` derives from `SimpleMemBackend` whose
  `ImplementVirtualSerializable` makes `cls_id()` call `serializable_abort`. So a
  `--checkpoint-sim-period` run on the default DRAM path dies at the first checkpoint, while the
  NMFC and Vanadis components would fail **silently**, restoring empty tiles and an empty
  directory with no diagnostic. The facility is marked EXPERIMENTAL by the tool's own help output,
  no sst-elements test suite exercises it, and `SST_ELI_IS_CHECKPOINTABLE` is declarative only.
- **The default `mmap` backing does not carry memory.** `BackingMMAP::serialize_order` writes
  exactly one byte of a raw `uint8_t*` and its restore path is commented out;
  `BackingMMAP::printToFile` is an empty function. `BackingMalloc` serializes chunk by chunk and
  dumps a self-describing sparse stream. Switching is a prerequisite, not an optimisation, and
  whether it changes timing must be measured.
- **Vanadis already ships an architectural snapshot of the right SHAPE and the wrong CONTENT.** It
  is program-triggered (RV64 syscall 500), taken at a quiesced point (all threads halted, LSQ
  empty in both directions), and restores by writing architectural registers and calling
  `handleMisspeculate(rob[0] + 4)`, restoring no ROB, rename map or predictor. That last fact is
  the proof that the register half of an image is microarchitecture-independent. But it drops
  `fp_flags` entirely, its load path hard-codes 35 and 32 register counts against a save path that
  derives them, and it restores memory by re-reading the ELF, so it is valid at process start and
  nowhere else. **Reuse its quiesce rule and its restart route; do not reuse its file.**

---

## 3.1 DESIGN A: NMFC-IMG, a virtual-address-keyed architectural image

**Mechanism.** One simulator-independent image format carrying only what the ISA can observe,
keyed by (asid, virtual address), plus two run-history sections nothing can rebuild (the live
handle set, and the NUCA `remap_` map). Placement is **re-derived** by the loader from the
declaration list at the target geometry, never carried. Two classes: Q (drained) and L (live),
same format.

**Producer.** Three, one format: (1) spike with an NMFC custom-0 extension, primary, rate
`UNVERIFIED`; (2) QEMU user mode for the baseline arm, **measured at 806 MIPS on this machine**
(14,421 intervals of 1e8 instructions in 1,789 s), with the full BBV to SimPoint 3.2 pipeline
already run end to end on RISC-V here; (3) one SST detailed pass as the always-available fallback.

**Consumer.** A new `nmfc.NMFCImageLoader`, not `--load-checkpoint`. Header verification and
refusal; declaration-driven `place()`/`allocate()` at the target geometry; memory installed
through the backing store, not the host cache; architectural state installed; outstanding set
re-admitted; warm-up; measure. A **reload** operation lets one SST process consume a batch of
images, which is the difference between SMARTS costing 1.5 h and 13 h at n = 4,268.

**Sampling policy.** Both, on the same images. SMARTS stratified with random offsets (the NUCA
epoch is an explicit periodicity and a resonant period biases the estimate in a way the confidence
interval will not reveal). SimPoint on host basic blocks concatenated with a per-variant FORK
entry-PC histogram, each L2-normalised separately, tile counts and migration counts deliberately
excluded from the vector and kept as per-interval validation statistics instead.

**What survives which change.** The design ran all 31 recorded design changes of the last nine
days and classified each. 24 of 31 leave the image loadable and valid with no format change; 2
more survive only because the advisory tier is droppable; 5 invalidate it, of which 2 are program
changes (the ISA under R11, the entry-marker word under Q3) and 3 are simulator correctness fixes
(L61, L64, L65). Zero invalidate it for a **cache geometry, directory format, queue depth, context
count, FTU capacity, host model, placement policy or memory-link** reason. That claim is the
design's real contribution and it holds, with one correction: L65 is a fabric queue-accounting
change and it sits in the does-not-survive column, so "zero microarchitectural" is false as
written; the honest figure is 24 of 29 distinct items.

**Cost.**

| | as proposed | as recomputed by the lenses |
|---|---|---|
| Producer, baseline arm | 806 MIPS measured: 2B = 2.5 s, 10B = 12.4 s, 50B = 62 s | correct |
| Producer, offloaded arm | spike, `UNVERIFIED`; SST fallback = one end-to-end run | fallback is per (workload, size, **configuration**) for class L, not amortisable |
| Warm window W | 1.0e6 cycles with advisory tier, 3.2e6 without | NUCA term taken from the **frozen ChampSim** default of 100,000 migrations; the SST machine's default is **2,000** (`NMFCCoherenceFabric.cc:124`), so W is ~8.8e5 either way and the advisory tier saves ~3 minutes over 4,268 samples |
| Per-sample, 1M cycles/s, IPC 0.30, U = 1e5 | 1.10 s + 0.05 s load + 10/64 s amortised startup = 1.31 s | 1.23 s + an **unpriced** teardown for reload |
| SMARTS n = 4,268 | 1.55 h serial, 2.9 min on 32 cores | 4.2 h serial at the corrected W without the advisory tier |
| SimPoint k = 15, I = 1e8 | 1.39 h serial, 5.6 min on 15 cores | robust to the W correction (336 s per interval against 334) |
| Storage | 17.9 GB per point at a 4 MiB delta | the 4 MiB delta is **underived**; at full images it is 138 GiB per point |
| **Effort** | **80 engineer-days over 11 items** | reduced form is ~40 days |

**Risks.** The in-flight reconstruction is a stated approximation whose bias points at the
project's own host-bottleneck conclusion; W is derived, not measured; the header refusal gate is
the only defence against a stale image becoming a published number.

**Verdicts.** Canon lens: **does not survive.** Statistics lens: **does not survive.** Engineering
lens: **does not survive.** Fatal findings, quoted:

> "THE ESTIMATOR'S DENOMINATOR IS A TIMING-DEPENDENT SPIN LOOP, AND THE FUNCTIONAL PRODUCER CANNOT
> SUPPLY IT. ... on BFS `JOINQ is 84,295 of the 84,999 NMFC instructions out-of-order and 189,364
> of 190,068 in-order`, i.e. 99.2% poll, and `the poll count differs 2.25x` between the two hosts
> while `the program's own waitcycles ... agree within half a percent`."

> "THE BUILD-HASH GATE NULLIFIES THE ENTIRE COMPATIBILITY CLAIM, BY THE DESIGN'S OWN
> COUNTERFACTUAL. ... A build hash over `src/nmfc` changes on every commit in that tree ... So the
> operational compatibility rate is 0 of 31, not 26 of 31, unless the override is used."

> "TIER B HOLDS THE ONLY STATE THAT WARM-UP CANNOT REBUILD, AND IT IS DISCARDED ON EXACTLY THE RUNS
> THE SWEEP EXISTS FOR. ... `parent_`, the union-find over grains, is NEVER cleared and NEVER
> decays ... run length monotonically drives the policy toward inertness, an irreversible state
> transition whose position depends on TOTAL HISTORY LENGTH."

> "THE DEFAULT SAMPLER HAS NO LEGAL IMAGE POINTS ON TWO OF THE THREE WORKLOADS ... `shuffle_sum.c`
> has exactly ONE such drain ... at the end of `offloaded_sum`, which `main` calls once."

---

## 3.2 DESIGN B: Checkpoint Ladder, drain the host and never the FTU

**Mechanism.** One detailed end-to-end run per (program, size, build) writes a ladder of SST-native
checkpoints. The NMFC side and the whole memory system are captured **live** and
microarchitecturally; the **host** is captured architecturally, by draining its pipeline before the
snapshot, because the ROB holds polymorphic `VanadisInstruction*` across roughly 78 classes. That
asymmetry is what makes the Vanadis job finite. Later compatible builds replay from rungs instead
of re-running.

**Producer.** The detailed model itself, plus a trigger on retired host instructions (an NMFC
observer subcomponent raises SIGUSR1 with `--sigusr1=sst.rt.checkpoint`, which needs no sst-core
patch), plus a per-interval SimPoint signature emitted as a side file. 200 rungs by default.

**Consumer.** `sst ... <ckptdir>/<prefix>.sstcpt`. The Python is never re-run, Params are
deliberately not serialized, and neither `init()` nor `setup()` runs on the restart path, so a
restore side-car carries the warm and measure windows and any re-appliable constant.

**Sampling policy.** Both, but priced honestly: restoring a full-system SST image costs seconds
(`UNMEASURED`, estimated 3 s), so U is bounded from below at about 9e5 instructions and the
reachable SMARTS regime is U ~ 1e7, not the classic 1e3. The escape is parallelism: 144 cores and
34 TB free, and every SST run is serial, so replays are a job farm.

**What survives which change.** The narrowest window of the three, and the design says so. It
requires an identical component graph, an identical serialized member set and order in every class
(`cls_id` is a hash of `typeid(T).name()` and encodes nothing about layout), identical element
libraries, an identical ELF, and every configuration value that became a member (Params are not
serialized, so a changed parameter is **ignored, not rejected**). Free: behaviour that reads only
restored state, which is most policy work.

**Cost.**

| instructions | end-to-end producer @220k, IPC 0.30 | @1M | per-sample (U = 1e7, W = 1e6, 1M) | 200-rung storage |
|---|---:|---:|---:|---:|
| 2B | 8.4 h | 1.9 h | 39.7 s | 12.6 GB / point |
| 10B | 42.1 h | 9.3 h | 39.7 s | 12.6 GB / point |
| 50B | 8.8 d | 46.3 h | 39.7 s | 12.6 GB / point |

Sample counts: SimPoint k = 10 to 30; SMARTS n = 267 / 1,067 / 4,268 at V = 0.25 / 0.50 / 1.00,
eps = 3%. Replay campaign at 50B, against 46.3 h serial: k = 30 is 140x serial and ~2,100x at
16-way; n = 300 is 14x serial and ~220x at 16-way. **Effort 99 engineer-days over 15 items**
(the design says ~97; the items sum to 99).

**Risks.** Silent restore failure is the default and the design is unusually clear about it.
ramulator2 and the host pipeline restart cold. The drain perturbs the producer's own numbers at
200 points, so no run in the design produces an unperturbed control.

**Verdicts.** Canon lens: **does not survive.** Statistics lens: **does not survive.** Engineering
lens: **SURVIVES**, with must-fixes, and it is the only design that survives any lens. Fatal
findings, quoted:

> "THE HOST DRAIN STARVES THE FTU AND EMPTIES THE TILES. ... for the whole drain the FTU takes in
> no new work, OUTSTANDING entries complete into RETURNED and can never be freed, resident tile
> contexts finish without replacement, and the data-port backlog, 81.3-83.8% of resident
> context-cycles per `CANON.md:3278-3292`, drains away. The sign of the distortion is fixed by
> construction."

> "THE ESTIMATOR IS UNBIASED ONLY WHERE THE DESIGN IS WORTHLESS. It multiplies the REPLAYED build's
> CPI by the PRODUCER build's instruction total. Those agree only when the builds are the same,
> which is the one case with no value."

> "THE ESTIMAND IS NOT COMPUTABLE IN THE RULING'S REGIME. 'Total host instructions, counted
> exactly' requires a completed end-to-end run. The ruling is about sizes where that stops being
> possible."

The engineering lens's survival verdict came with the one measurement in this whole exercise that
directly prices the ruling's own concern. Over the repository's 86 commits, classifying each by
whether it touches a serialized member declaration or a graph-defining config: 7 commits (8%)
removed, reordered or retyped a member (silent invalidation); 18 (21%) added members (compatible
only under append-and-version discipline); 8 more touched the graph configs; 53 (62%) were
compatible. **Mean length of a compatible run: 9.9 commits counting hard breaks only, 4.4 counting
hard breaks plus graph changes, 1.6 if additive discipline lapses. Medians 5, 2.5, 0.** Against a
corrected break-even of B >= 3 at 50B, B >= 7 at 10B and B >= 30 at 2B (the design's own
break-even omitted the engineering cost entirely), the ladder pays at 50B, is marginal at 10B and
does not pay at 2B.

---

## 3.3 DESIGN C: DCP, the Deferred-Completion Producer

**Mechanism.** A spike RoCC extension executes the program functionally but not eagerly: `FORK`
allocates a functional tracking-unit entry and records the entry PC and the 512 input bits
**without running the body**; the body runs at the moment its result is first architecturally
observed. The producer's live handle set at any instruction is therefore the program's own
in-flight set, so an image can carry a **populated** tracking unit. The claim is that this moves
the zero-in-flight bias from an occupancy error (time constant 19,289 to 36,704 host cycles) to a
phase error (time constant 1,205 to 1,443), about 15x shorter.

**Producer.** Spike with an NMFC custom-0 extension, subclassing `extension_t` directly rather
than `rocc_t` because of a verified encoding trap: spike puts `xs2` at instruction bit 12 and `xd`
at 14, while NMFC and Vanadis put `xd` at 12 and `xs2` at 14, so under an unmodified `rocc_t`
`FORKQ`, `JOINQ` and `CXR` would never write their destination register. A poll loop that hangs,
not a crash.

**Consumer.** Three additions, all claimed to land in `src/nmfc`: memory through the existing
untimed PT_LOAD placement path, host architectural state via a loader invoked from `NMFCRoCC`, and
the live handle set pre-loaded into the tracking unit with a rate-limited dispatch burst.

**Sampling policy.** Both from one producer pass. SMARTS with U = 1e5 retired host instructions,
W = 3 x W_FTU, n = 1,024 on V = 0.5. SimPoint at I = 1e8, k = 20.

**What survives which change.** The image breaks on the geometry the partition is defined over
(N, G, mode bit, physBase, the declaration list, the ELF, `NMFC_CTX_BYTES`) and on nothing else.
Cache geometry, fabric latencies, DRAM timing, DDR against CXL, host core width, in-order against
out-of-order, contexts per tile, FTU size upward and the coherence protocol's implementation are
all free.

**Cost.**

| | as proposed | as recomputed by the lenses |
|---|---|---|
| Producer | 5 / 20 / 50 MIPS parametric: 50B = 2.8 h / 41.7 min / 16.7 min | host instructions only; **every tile-side body instruction is missing**, and the design's own conclusion is that above 10B the producer is the entire cost |
| W_FTU | 19,289 to 36,704 host cycles | derived by dividing occupancy by an endogenous fork rate; lambda cancels, so the "15x" is exactly `L_FTU/L_tile` (13.367 against 13.366, and 17.956 against 17.95) |
| Per-sample @1M, U = 1e5 | 0.350 s | plus an unpriced per-restore fixed cost (process startup, graph construction, ramulator2 init from YAML, ~2.2M page-table lookups for the untimed placement) |
| SMARTS n = 1,024 total | 1,629 s @220k; 358 s @1M | I/O omitted (~172 s), SimPoint omitted (its own 1,662 s at 16-way), restore fixed cost omitted (~5,120 s serial at 5 s each) |
| Speedup at 2B, 220k | 150x | ~83x with I/O; ~16x with SimPoint included |
| **Effort** | **53 engineer-days over 12 items** | the 10-day spike item prices a plugin that structurally cannot run bodies |

**Risks.** The design names the right gating experiment (restart three ways and report when the
occupancy traces converge) and then prices the whole plan as though the answer were known.

**Verdicts.** Canon lens: **does not survive.** Statistics lens: **does not survive.** Engineering
lens: **does not survive.** Fatal findings, quoted:

> "MODE D RESTORES THE WRONG ONE OF THE TWO AVAILABLE STATES, BY THE DESIGN'S OWN ARITHMETIC. ...
> `returned-and-unjoined share of FTU residency = (54.0-4.04)/54.0 = 92.5%` ... Restoring all of
> them as OUTSTANDING puts 54 (hash) or 251 (shuffled sum) invocations onto tiles that really held
> 4.04 and 14.0: an overstatement of tile-side in-flight work by 13.4x and 18.0x. Those are the
> SAME two ratios the design sells as its 15x advantage, it has them backwards."

> "THE FORKF HOLE IS NOT HYPOTHETICAL ... The ENTIRE insert path of the chained hash table is
> fire-and-forget, and phase (a) is 'all I inserts, then L = I/2 lookups' ... two thirds of the
> operations."

> "CONSUMER ITEM 1 RESTS ON A PATH THE DEFAULT HOST DOES NOT USE ... `vanadis-nmfc.py:173-179`
> sets, for ALL THREE host MMUs, `"imagePlacement": "host_os"`, not `"mmu"`."

> "CONSUMER ITEM 2 IS NOT REACHABLE FROM A RoCC SUBCOMPONENT. ... In `vanadis.h`,
> `register_files`, `retire_isa_tables` and `thread_decoders` are inside the `private:` block."

> "THE 15x ... IS AN OCCUPANCY RATIO WEARING A TIME-CONSTANT COSTUME, AND IT COLLAPSES
> ALGEBRAICALLY."

---

## 3.4 What all three designs got wrong in the same place

Nine independent verdicts, three designs, and four findings recur in every one of them. They are
the real output of this exercise.

**F1. Host instructions retired cannot be the denominator, and cannot pair the two arms.** On the
offloaded arm the host instruction stream is overwhelmingly a poll: canon N.9a measures `JOINQ` at
84,295 of 84,999 NMFC instructions on BFS out-of-order, and the poll count differs 2.25x between
the two host models for **byte-identical tile work** while the program's own `waitcycles` agree
within half a percent. The workloads' own source says it first: an instruction count that does not
separate the two "says the offloaded build executed more instructions than the baseline when what
it did was wait." Canon rules host instructions retired as the progress unit **within** a run
(`DESIGN.md:881`); it does not license using it as a cross-model or cross-arm coordinate, and the
two arms are different binaries built from one source by `#if NMFC_BFS_HOST`.

**F2. Instruction counts do not name the same program point in a functional producer and the
detailed model.** Any producer fast enough to be worth building does not spin, so its retired count
at an image point is a different program point. That breaks class-L image placement, SimPoint
interval boundaries and SMARTS stratum offsets simultaneously. **Program landmarks do not break**:
a BFS level boundary, a shuffled-sum batch, a hash-table barrier are model-invariant.

**F3. The warm-up length is the number that decides the project and nobody has measured it.** Every
design asserts a bracket, prices the plan on it, and then concedes the bracket is unbacked. Canon
already forbids finding it by sweeping (R97, and the 2026-09-04 ablation ban): the instrument is
the counters that already exist.

**F4. Every design priced one arm.** I8 is a ratio against a reference doing the same work, and the
baseline arm needs its own producer, its own points and its own detailed simulation. Section 4.4
shows the baseline arm is also the arm that hits the wall first.

---

# 4. RECOMMENDATION

## 4.1 What to build

**Build the reduced form of Design A: a virtual-address-keyed ARCHITECTURAL image, class Q only,
anchored on PROGRAM LANDMARKS, with a WORK-denominated estimator and SimPoint as the default
sampler. Run the warm-up convergence measurement FIRST, before the format is frozen, and let it
decide whether the rest is funded.**

Concretely, that is Design A with five amendments, each forced by a fatal finding:

1. **Class Q only.** Class L has no producer that is simultaneously fast and valid. A fast
   producer either executes bodies eagerly (Design A's spike, so every entry is RETURNED and the
   outstanding set is empty) or defers them (Design C, so restoring the set as OUTSTANDING
   over-dispatches 13 to 18x against the measured 92.5% returned-and-unjoined split). The only
   producer that can emit a correct live set is the detailed model itself, and its output is
   configuration-specific and therefore not amortisable across a sweep.
2. **Landmark anchoring, not instruction counts.** Fixes F2 outright and costs nothing the design
   argued for well: its own cross-arm pairing already used landmarks and already admitted the
   anchor is not ruled.
3. **A work denominator.** The workloads already count vertices settled, chains summed and probes
   retired, and I8 is stated in those terms. Aggregate as a ratio of sums over sampled windows,
   never a mean of per-window ratios, and never credit an unfinished invocation's work inside its
   own window (Part P R102).
4. **No advisory NUCA tier.** At the SST machine's own `nucaEpoch` default of 2,000 the tier saves
   about three minutes over 4,268 samples and costs 5 engineer-days plus a permanent provenance
   flag on every result. Delete it. Whether the NUCA union-find ratchet needs to be carried at all
   is Open Question 4, and it must be settled by measurement, not by a tier label.
5. **A scoped build gate.** An unscoped hash over `src/nmfc` refuses on every commit and makes the
   compatibility score theoretical. Until someone defines the subset that changes what a **correct**
   image contains, the gate refuses and says so; that is honest, and it is the right default.

## 4.2 Why not the other two

**Design B (the ladder) is the right substrate for the wrong question.** It buys deep fidelity and
pays with a narrow compatibility window, and its own engineering lens measured the window: 4.4
compatible builds per ladder, median 2.5, against a corrected break-even of 3 at 50B, 7 at 10B and
30 at 2B. It pays at 50B and does not pay at the sizes we are actually at. It also cannot answer
the ruling's own question, because its estimand needs a completed end-to-end run to supply the
instruction total. Keep it in the drawer: if the campaign ever becomes "the same machine, a
different policy, fifty times, at 50B", this is the design to take out again.

**Design C's central mechanism is refuted by measurement.** The FTU is 92.5% returned-and-unjoined
on the hash table and 94.4% on the shuffled sum, so a design whose whole point is "get the live
count right" gets the total right and the split maximally wrong. Its own headline W numbers come
from the one workload whose entire insert path is fire-and-forget and therefore invisible to it.
Two of its three consumer items do not compile against the default host's configuration. The parts
worth salvaging are the spike encoding trap (a real, verified silent-failure hazard for whoever
writes that extension) and the eligibility/write-set idea, which is a good instrument for a
question nobody has asked yet.

## 4.3 Q4: does an architectural image remove the compatibility objection to SMARTS?

**For the IMAGE, yes, decisively. For SMARTS, no.**

The ruling's premise is right: an image that must stay compatible across large changes is more
trouble than it is worth. But the objection is **mislocated**. It is true of microarchitectural
images and false of architectural ones, so it selects the image CLASS, not the sampling METHOD.
The evidence is the compatibility ledger in 3.1: 24 of 29 recorded design changes leave a
virtual-keyed architectural image loadable and valid, and the survivors include every change of
cache geometry, directory format, queue depth, context count, FTU capacity, host model, placement
policy and memory link. The single change that would have killed a physical-keyed image is the
geometry ruling itself ("we must support all possible values within a full 48-bit physical address
space"), because `tileOfPhysical` divides by grain and modulos by tile count. That one ruling
settles the keying question on its own.

Once the images are architectural, SMARTS's remaining costs are not compatibility costs, and all
four are still binding:

| SMARTS's remaining cost | why it binds here |
|---|---|
| It needs a LIVE in-flight image to be unbiased | No fast producer can make one (F1 above, and the 92.5% split) |
| Storage scales linearly in n | n = 4,268 at 33 to 65 MiB per image is 138 GiB per point at full images, and the delta size that made 17.9 GB plausible was never derived |
| Per-sample restore cost sets a floor on U | Restore is seconds; break-even is U ~ 9e5 instructions, so the reachable regime is U ~ 1e7, not the classic 1e3. At U = 1e7, n = 1,067 replays 1.1e10 instructions, **more than a whole 10B run** |
| Its warm state is per-CONFIGURATION | SimPoint pays one selection pass per WORKLOAD and the same k points serve every configuration in a sweep. For a campaign over tile counts, DDR against CXL and problem sizes, that is the dominant term |

**So the ruling's conclusion inverts.** An architectural image does not rescue SMARTS; it makes
SimPoint cheap. SMARTS remains the technique that reports a confidence interval, and that is worth
keeping, but its affordable regime here starts around 1e11 instructions and its samples must be
long (U ~ 1e7), which is a stratified-interval sampler rather than SMARTS in its classic form.

## 4.3.1 When SimPoint is still the better tool

Three cases, and the second is specific to this machine.

**Large ISA, page-model or context-format changes.** R11 (RV64 replaces x86-64), Q3's entry-marker
word, R115's retirement of identity-mapped placement, any re-encoding of a funct7: every one of
these invalidates an image outright, and the defence is a refusal rather than compatibility. When
images are being regenerated anyway, the cheaper thing to regenerate wins, and a SimPoint selection
pass is one functional run per workload against SMARTS's per-configuration warm state.

**Phase-structured programs, of which level-synchronous BFS is exactly one.** The top-down levels
are host code with no offload and, at G/4 and G, **zero migrations** (`mig/load 0.000`, measured);
the bottom-up levels are the offloaded step. A systematic sampler at a fixed period either aliases
with the level structure or spends most of its samples in host code that the machine is not about.
Clustering finds that structure and weights it; uniform coverage dilutes it. The same argument
applies to the hash table's separated phase, where inserts are fire-and-forget and lookups are a
join ring: two structurally different regimes separated by a full drain.

**Cross-configuration sweeps.** The k points are chosen on a vector that deliberately excludes tile
counts, migration counts and placement, so they serve every configuration in the sweep and their
validity at each is checkable (compare the representative interval's migration rate and
port-refused share against the full run's). That property is worth protecting and is the reason
those terms stay out of the vector.

## 4.4 Q5: the threshold rule, from this machine's measured sweep

**No sweep point that has ever completed on the default out-of-order host reaches 2B retired host
instructions. The largest is 3.5e7, which is one fifty-seventh of 2B.**

Measured, out-of-order host, from
`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/results/ooo/`:

| workload | largest COMPLETED OoO point | baseline retired host insns | offloaded retired host insns | status of the next point up |
|---|---|---:|---:|---|
| Hash table (`hashtable.md` Table 2) | P2, 4.00 MiB, load factor 2.333 | **22,616,398** | **35,245,417** | P3, P4 absent from the OoO tables |
| BFS (`RESULTS-OOO.md`) | 4G, 4.00 MiB, V = 65,536 | not archived (fc insns 2,885,154) | not archived | 16G and 32G: *capped, rc=124, 2400 s* |
| Shuffled sum (`RESULTS-OOO.md`) | 3, 4.00 MiB | not archived (29.27 ms, 8.78e7 cycles) | not archived (3.76 ms) | 16 MiB: **baseline dropped**; 32 MiB: **baseline not attempted** |

**The wall today is a 4 MiB working set for a complete two-arm point.** The budget arithmetic
confirms it and generalises it:

| | 2,400 s budget at 220k cycles/s | at 1M cycles/s |
|---|---:|---:|
| simulated cycles reachable | 5.28e8 | 2.40e9 |
| retired host instructions at IPC 0.124 (BFS 4G) | 6.5e7 | 3.0e8 |
| at IPC 0.30 | 1.58e8 | 7.2e8 |
| at IPC 0.49 (shuffled sum) | 2.6e8 | 1.2e9 |
| **as a fraction of 2B** | **1/31 to 1/8** | **0.15 to 0.6** |

**So the infrastructure is needed roughly an order of magnitude BEFORE the ruling's 2B threshold,
and the speed campaign does not close the gap:** even at 1M cycles/s the budget reaches at most
1.2B, on the friendliest workload, and 0.3B on BFS. The threshold that actually binds is the
2,400-second wall-clock budget, not an instruction count.

Extrapolating to where 2B is first crossed, using the in-order sweep's measured time growth as the
size proxy (**UNVERIFIED**, an extrapolation, not a measurement):

| workload | first point to cross 2B retired host instructions | arm | estimate | in the sweep today? |
|---|---|---|---:|---|
| Shuffled sum | 64 MiB (point 5) | **baseline** | ~2.3e9 | **yes, and never attempted** |
| Hash table | 32 G, load factor 21 (P4) | **baseline** | ~3.3e9 | **yes, in-order only** |
| BFS | roughly 128 G to 256 G, V ~ 3.4e7 to 6.7e7 | baseline | ~2e9 | **no**: node[] would be 2 to 4 GiB, which does not fit the declared 4 GiB physical space |

**The threshold rule, stated for use.** A sweep point needs sampling infrastructure when its
end-to-end simulated cycle count on either arm exceeds `budget_seconds x cycles_per_second`. Today
that is 5.28e8 cycles. Points already over it: BFS 16G and 32G, shuffled sum 16 MiB and above,
hash table P3 and P4. **On two of the three workloads the BASELINE arm crosses first**, which has
a consequence none of the three designs addressed: the arm that hits the wall first is a plain
RV64 program with no NMFC instructions, whose BBVs QEMU already produces at a measured 806 MIPS
and whose architectural image is an ordinary process image. **The half of the problem that binds
first is the easy half, and every design in this paper was built around the hard half.** That is
the strongest single argument for staging the work as Section 6 stages it.

## 4.5 What to measure before building anything

One experiment decides the project, and it can be run today on a point that still completes end to
end (BFS 4G, shuffled sum 4 MiB, hash P2).

From one landmark, restart two ways (drained image, and the uninterrupted run through the same
landmark) and record, cycle by cycle: the FTU live split into OUTSTANDING and RETURNED, per-tile
time-weighted context occupancy, the port-refused share of resident context-cycles, migrations per
load, and the per-tile slice hit rate. Report the instruction count at which the traces converge.
**That number is W.** If W over U exceeds about 10:1 the sampling speedup ceiling is small enough
that the whole exercise should be reconsidered.

This is instrumentation, not ablation (canon O.1, R97, and the 2026-09-04 ablation ban). It uses
counters that already exist. The one addition worth making is the per-tile slice hit rate, which no
design's convergence list included and which is the term the warm-up most plausibly cannot cover:
the aggregate LLC is 16 MiB (4 slices at 4 MiB, `coherent_memory.py:226`) against working sets of
4 to 65 MiB, so at the small sweep points the steady state is a **cache-resident** regime that a
cold start cannot reach inside a short window.

---

# 5. OPEN QUESTIONS

Each is one sentence, each has a default, and the default is what happens if no ruling comes.

1. **Denominator.** Should the sampled rate be denominated in units of WORK the program counts
   (vertices settled, chains summed, probes retired) rather than retired host instructions?
   *Default: yes, work; host instructions retired stays the progress unit within an arm, as canon
   rules.*
2. **Cross-arm anchor.** Should the two arms be paired on a PROGRAM LANDMARK present in both builds
   (BFS level, shuffled-sum batch, hash-table barrier) rather than on any instruction count?
   *Default: yes, landmark; canon has not ruled this and it is the ruling this work most needs.*
3. **Image class.** Do we fund class L (live in-flight) at all, or build class Q only until the
   convergence measurement says class L is worth its producer problem?
   *Default: class Q only.*
4. **NUCA state.** Is the placement policy's accumulated evidence (`remap_`, and the `parent_`
   union-find that never decays) carried in the image, refused on a mismatch, or changed so that it
   is bounded and re-learnable?
   *Default: carry `remap_` and refuse on a geometry or policy mismatch; treat the union-find
   ratchet as its own design question, because a policy whose state is not re-learnable is a
   constraint on every future sampler.*
5. **Producer.** Do we fund the spike NMFC extension now, or start with one SST detailed pass as
   the producer and fund spike only after its rate is measured?
   *Default: SST pass first; spike is a funded follow-on gated on a measured rate, because its rate
   is UNVERIFIED and above 10B instructions it becomes the entire cost.*
6. **Which arm first.** Given that the baseline arm crosses 2B first on two of three workloads, do
   we build baseline-arm sampling (QEMU BBVs plus an ordinary process image) before NMFC-arm
   sampling?
   *Default: yes; it is the cheaper half and it is the half that binds first.*
7. **Budget.** Does the 2,400-second wall-clock budget move, and if so to what?
   *Default: it does not move; the infrastructure is justified at the current budget and raising
   the budget only postpones the same wall.*
8. **Build gate scope.** Does a simulator-build mismatch refuse an image unconditionally, or do we
   define the subset of `src/nmfc` that changes what a CORRECT image contains?
   *Default: refuse unconditionally and say so in every result, until someone defines the subset.*
9. **The host-MMU divergence.** Do we wire the instruction MMU and node-OS MMU to the fabric's
   remap broadcast before freezing the image format?
   *Default: yes, as a change to the machine reviewed on its own merits, not as image plumbing.*
10. **Gate on the measurement.** Is the warm-up convergence measurement funded and reported before
    any format is frozen, with authority to stop the project?
    *Default: yes; it is work item W0 and no other item starts until it reports.*
11. **Trigger form.** Is the landmark trigger a syscall (as in Vanadis's own snapshot) rather than
    a fifteenth NMFC instruction?
    *Default: a syscall; canon I.7 and ruling R14 refused a COMMIT and a WAIT, and adding a
    machine instruction for the sampler's convenience is the same move one simulator later.*

---

# 6. WORK PLAN FOR THE RECOMMENDATION

Ordered. Each item names its files and its acceptance test. **Effort is engineer-days.** Every
size, count and threshold introduced here is configuration and belongs beside `FTU_ENTRIES` and
`CONTROL_QUEUE` in `nmfc_sizes.py` and `include/nmfc_isa.h`, readable by the report scripts with a
plain `python3 -c`.

| # | item | files | days |
|---|---|---|---:|
| W0 | **The gating measurement.** From one landmark on a point that still runs end to end, restart drained and compare against the uninterrupted run: FTU OUTSTANDING/RETURNED split, per-tile time-weighted occupancy, port-refused share, migrations per load, per-tile slice hit rate. Report the instruction count at which they converge. | new `test/sample_convergence.sh`, `test/converge.py`; read-only against `src/nmfc` | 4 |
| W1 | **Fix the three-way host-MMU remap divergence.** Wire `immu` and `osmmu` to the fabric's remap broadcast, then save one table. | `NMFCCoherenceFabric.cc:1735-1750`, `NMFCHostMMU.{h,cc}`, `test/vanadis-nmfc.py:185` | 2 |
| W2 | **Backing switch to malloc, with an identity regression.** The default `mmap` backing cannot carry memory (`printToFile` is empty; `serialize_order` writes one byte of a raw pointer). | `test/coherent_memory.py` MemController params; a check in `test/run_coherent.sh` | 1 |
| W3 | **Landmark trigger and the host-side quiesce.** A guest syscall carrying the NMFC quiesce request; the checks for all-harts-halted plus empty LSQ, no NMFC instruction awaiting an answer, no `killPending` entry, empty `handlers_`. | `NMFCRoCC.{h,cc}`, `NMFCTracking.h`, `include/nmfc.h`; the three workloads' landmark calls | 6 |
| W4 | **Image format and `libnmfcimg`.** Versioned TLV with header refusal gates (format version, ELF build id, simulator build hash, geometry digest over N, G, mode bit, page-type set, FTU capacity, contexts per tile, host model), tier-A sections only. | new `include/nmfc_image.h`, `src/NMFCImage.{h,cc}`; a spec section under `docs/nmfc/` (not `CANON.md`) | 8 |
| W5 | **Memory extraction and virtual re-keying.** Emit per (asid, region) virtual bytes; on load, walk the freshly placed page table and lay them at the frames `place()` chose, writing through the backing store and not the host cache. | new `tools/img_rekey`; `NMFCHostMMU.{h,cc}` | 6 |
| W6 | **Cache flush responder.** `NMFCCache` gains a flush handler that writes back every M and O line; the fabric passes it through and waits for acks. Note the existing memHierarchy flush is a one-shot shutdown flush in the untimed complete phase and does not reach the NMFC caches at all. | `NMFCCache.{h,cc}`, `NMFCCoherenceFabric.{h,cc}`, `NMFCCoh.h` | 5 |
| W7 | **The loader component.** Header verification and refusal; declaration-driven `place()`/`allocate()` at the target geometry; present bits; memory install; architectural-state install; warm-up phase boundary; measure-window close. | new `src/NMFCImageLoader.{h,cc}`, `libnmfc.cc`, `test/vanadis-nmfc.py` | 10 |
| W8 | **Work counters and the estimator.** Instrument the three workloads' existing work counts as first-class per-window statistics; build the ratio-of-sums estimator with its confidence interval, paired across arms on landmarks; report the in-flight count at both window boundaries. | `test/{tile_bfs_sweep.c, shuffle_sum.c, tile_htab.c}` counters; new `test/sample_estimate.py` | 6 |
| W9 | **SimPoint pipeline.** BBV emission (host basic blocks concatenated with the per-variant FORK entry-PC histogram, each L2-normalised separately, weight reported), clustering with the SimPoint 3.2 binary already on disk, mapping centroids to landmarks, cluster weighting, and the per-interval validation statistics. | new `test/ladder_simpoint.sh`, `test/sample_select.py`; reuse `contrib/plugins/bbv.c` format and `SimPoint.3.2/bin/simpoint` | 6 |
| W10 | **The per-sample validation gate.** Every sample runs the workload's own expected-result check over its own work; a header mismatch refuses loudly; slide distance and placement disagreement rate are reported on every run. | `test/sample_estimate.py`; the workloads' existing check paths; new `test/image_check.sh` | 4 |
| | **total** | | **58** |

## 6.1 Acceptance tests

| # | acceptance test |
|---|---|
| W0 | The report names a converged instruction count for each of the five traces on at least two workloads, with the trace plots attached. **Gate: if W over U exceeds 10:1, the project stops here and the finding is the deliverable.** |
| W1 | After a forced grain move, all three host page tables report identical (asid, vgrain) to tile maps; with no move, the coherent suite is byte-identical to the frozen build. |
| W2 | The coherent suite's statistics are byte-identical between `backing: mmap` and `backing: malloc`. If they are not, the difference is characterised before anything else proceeds. |
| W3 | Image points land exactly at the program's declared landmarks; `FORKQ` reads the unit's capacity at every one; the suite's answers and digests are unchanged. |
| W4 | A header written by format version N and read by N+1 with an added tag defaults the tag correctly; a mismatched ELF id, geometry digest or build hash is refused with a message naming the field that differs. An image with a live entry count above the target unit's capacity is refused. |
| W5 | Dump-then-load at the same geometry reproduces every byte. At a different tile count the loader places the same virtual bytes at freshly placed frames and `PageTable::checkCongruent()` passes on every tile. |
| W6 | After the flush, the backing store contains every line that was M or O above it; a data diff against a run that never flushed shows no difference. |
| W7 | **Image round trip:** run end to end, take an image at landmark T, restart from it, run to program end, and the workload's own digests (BFS depth and parent FNV-1a, the shuffled sum's checksum and sixteen sampled chains, the hash table's analytic check) match the uninterrupted run exactly. |
| W8 | **Sampled estimate within its own interval:** on a point that still runs end to end, the sampled speedup estimate contains the end-to-end speedup inside its reported confidence interval, on all three workloads. The interval is reported, never omitted, and the in-flight counts at both window boundaries appear in the output. |
| W9 | The k representative landmarks reproduce the full run's migration rate and port-refused share within a stated tolerance, at **two different tile counts**, from one selection pass. Where they do not, the run says so and re-clusters rather than reporting a number. |
| W10 | A deliberately corrupted image is refused rather than run. A sample whose expected-result check fails is a hard failure, not a warning. |

## 6.2 What travels with every number this produces

Sampling changes nothing about the two caveats that already travel with every function-core result:
the core model (canon M.3) and the absent branch-honesty sensitivity run (O.4 item 2). A sampled
36x is a 36x with the same two caveats, plus three more that are this infrastructure's own: the
warm-up length W and how it was measured, whether the sample's landmarks were validated at that
configuration, and the in-flight counts at both window boundaries.

---

# 7. WHAT IS UNVERIFIED IN THIS PAPER

Listed once, so no reader has to hunt for the caveats.

- **Spike's instruction rate.** Not measured here, no rate claim exists in its tree, and above
  about 10B instructions it is the entire cost of any spike-based plan.
- **SST image restore time.** Estimated at 3 seconds in Design B and never measured. It sets the
  floor on U and therefore the entire SMARTS regime.
- **SST process startup time.** Estimated at 10 seconds and never measured. It is the whole
  justification for a reloadable loader.
- **V, the coefficient of variation of the per-window metric,** for all three workloads. Every
  SMARTS sample count in this paper is parametric on it.
- **W, the warm-up length.** Derived three different ways by three designs and measured by none.
  Work item W0 exists to fix this.
- **The 2B crossing points in 4.4.** Extrapolated from the in-order sweep's measured time growth as
  a size proxy. The measured half of that table (the completed OoO points, the capped points, and
  the budget arithmetic) is not an extrapolation.
- **Delta image sizes.** Asserted at 4 MiB in Design A with no derivation, against a 1 MiB backing
  granule and a BFS level that sweeps a 32 MiB array.
- **The claim that an unserialized SST component restores empty without an error.** The code path
  was read, never executed. It is the basis of every "SST fails silently" argument here.
- **All prior-art descriptions.** SMARTS, SimPoint, live-points and gem5's drain interface are
  described from model knowledge; no paper was opened in this work, so no citation is given beyond
  the technique names.
