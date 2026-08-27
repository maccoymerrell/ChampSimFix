# Near-Memory Function Core (NMFC) — Design

**Base:** `ChampSimFix` @ `a2684f5c` (branch `nmfc`), cloned into `ChampSimArchWork/nmfc`.
**Revision 6** — §13 records what is actually built. Earlier:  mapping mode becomes a physical address bit, stamped once at allocation; mixed page sizes; `NMFC_MMU` replaces two would-be forks. Supersedes revisions 1–4.

---

## 1. What we are modeling

Supercompute workloads with abundant *thread* parallelism but near-zero *memory-level* parallelism per thread: graph traversal, pointer chasing, sparse indexing. With a fixed thread count and MLP ≈ 1, bandwidth sits idle and compute sits idle. GPUs don't help — the kernels themselves are serial.

A **memory tile** owns a memory channel, an LLC slice, and a **function core** that time-multiplexes an arbitrarily large number of stackless kernel invocations. Each invocation sleeps on a memory request and wakes on its return, so one function core keeps one channel saturated out of purely serial work.

The experiment: **traditional multicore vs. NMFC on the same graph workloads**, sweeping graph size, degree, locality, function length, context count, and translation mechanism.

---

## 2. Topology

```
   ┌──────────── COMPUTE TILE k (× num_compute_tiles) ─────────────┐
   │  NMFC_HOST_CORE (extended O3_CPU)                              │
   │      ├── ROB / LQ / SQ                    (unchanged)          │
   │      └── FTU: function tracking unit ───────────────────────┐  │
   │                                                              │  │
   │  L1I · L1D ──lower_translate──► [NMFC_MMU]                   │  │
   │       └──► L2C ──► [INTERLEAVE_FABRIC] ────────────────┐    │  │
   └────────────────────────────────────────────────────────┼────┼──┘
                                    ordinary loads + stores │    │ invoke / return
                                                            │    ▼
                                                            │  ╔════════════════════╗
                                                            │  ║  FUNCTION_FABRIC   ║
                                                            │  ╚═════════╤══════════╝
   ┌────────────────────────────────────────────────────────┼────────────┼──────────┐
   │  MEMORY TILE t (× num_memory_tiles)                    │            ▼          │
   │                                                        │   FUNCTION_CORE       │
   │   NMFC_MMU t ◄──────── translate ──────────────────────┼──   N contexts        │
   │   (4 KiB + G arrays)                                   │      │        │       │
   │        │                                               ▼      ▼        ▼       │
   │        └──── walk ──────────────────► LLC SLICE t ◄── fc I$   fc D$            │
   │                                            │                                   │
   │                                    MEMORY_CONTROLLER t ──► DRAM channel t      │
   └────────────────────────────────────────────────────────────────────────────────┘
```

* **Memory network** — ordinary loads/stores. `INTERLEAVE_FABRIC` (one per compute tile) routes an L2C miss to the LLC slice of the tile owning the address. No function-core involvement.
* **Function network** — `FUNCTION_FABRIC` carries invocations, migrations, and returns.

---

## 3. Additive integration

Interfaces registered by name, models registered by name, an explicit JSON environment that builds an arbitrary graph of them. **Nothing edits an existing header or `.cc`.** The one exception is build wiring: the `Makefile` globs `src/*.cc` and `src/listeners/*.cc`, so it needs **one added line** for `src/nmfc/*.cc`.

### New interfaces

| Interface | Parent | Mixins | Role |
|---|---|---|---|
| `function_core` | `environment_module` | `operable` | Executes function contexts near memory. One per memory tile. |
| `function_fabric` | `environment_module` | `operable` | Routes invocations, migrations, returns. Owns hop latency, link bandwidth, and the dispatch placement policy. |
| `function_image` | `environment_module` | — | Compiler-placed function bodies, keyed by invocation token. |

`translation_engine` is **not** a registered interface. It is a plain abstract base class that `NMFC_MMU` implements, reached by `dynamic_cast` from a channel reference — swappable without occupying a registry slot.

### New models of existing interfaces

| Interface | Model | Role |
|---|---|---|
| `core` | `NMFC_HOST_CORE` | O3_CPU copied and extended with the function tracking unit (§4). **The only fork.** |
| `channel` | `NMFC_MMU` | Mixed-page-size MMU serving both host caches and function cores (§6). Operable. |
| `channel` | `INTERLEAVE_FABRIC` | Address-interleaved router; owns tile-bit compaction. Operable. |
| `vmem` | `NMFC_VMEM` | Congruent frame allocator over a `(channel, row)` free bitmap; stamps the mapping-mode bit once, at allocation (§5.3). |
| `vmem` | `NMFC_FLAT_VMEM` | IMPICA-style region + flat page table. |
| `memory_controller` | `NMFC_MEMORY_CONTROLLER` | Reads and strips the mode bit, then applies the selected mapping (§5). Replaced by `RAMULATOR_MC` in Phase 2. |
| `instruction_producer` | `NMFC_PRODUCER` | Reads the NMFC trace; emits host instructions, publishes bodies and page hints. |
| `function_core` | `FUNCTION_CORE` | The default function core model (§7). |
| `function_fabric` | `FUNCTION_FABRIC` | The default fabric model. |
| `function_image` | `FUNCTION_IMAGE_STORE` | The default body store. |
| `listener` | `NMFC_STATS` | Run-wide NMFC instrumentation via the hook system. |

**Construction order.** `@name` references resolve in declaration order, so the fabric is declared before the tiles; tiles self-register with it at construction.

---

## 4. The host core: `NMFC_HOST_CORE`

`O3_CPU` is copied into `inc/nmfc/nmfc_host_core.h` / `src/nmfc/nmfc_host_core.cc` and extended. (`operate()`, `initialize()`, `push_instruction()` and `print_deadlock()` are `final`, so subclassing could not hook the pipeline in any case.) The copy stays line-for-line identical to upstream except at marked `// NMFC:` blocks, so `diff` against `src/ooo_cpu.cc` remains the maintenance tool.

### The function tracking unit (FTU)

Parallel to the LSQ, not a reuse of it — offload concurrency is its own knob (`ftu_size`).

```
struct ftu_entry { uint64_t token, instr_id; size_t rob_slot; bool dispatched, returned; };
```

1. `do_memory_scheduling` sees a source-memory address inside the **offload aperture** and diverts it: no LQ entry, an FTU entry instead. `token = (addr − aperture_base) >> log2(block_size)`; the aperture is large enough that tokens never alias.
2. The FTU hands the invocation to `FUNCTION_FABRIC`, which picks the target tile by policy (§5.5).
3. On return the FTU bumps the owning instruction's `completed_mem_ops` exactly as `LSQ_ENTRY::finish` does, so retirement is unchanged.
4. A CALL flagged `FLAG_NO_RETURN` completes at dispatch.

Back-pressure chains end to end: no free context → the fabric's inbound queue fills → the FTU cannot dispatch → the ROB stalls.

**Cost:** ~1,800 lines forked. This is now the *only* fork in the project.

---

## 5. Address space, mapping mode, and translation

### 5.1 One virtual address space, split restrictively

The execution space stays **virtual and unified** — host cores and function cores use the same VAs and the same page tables. Translation splits into a restrictive half and a flexible half:

| | mechanism | cost |
|---|---|---|
| **VA → channel** | *restrictive*: `tile_of()`, deterministic | shift + mask; no lookup |
| **VA → frame within that channel** | *flexible*: ordinary demand paging against that channel's own root | a walk, mostly to memory |

This is [Utopia](https://arxiv.org/pdf/2211.12205)'s restrictive/flexible split at channel granularity. Three properties fall out:

1. **Routing never waits on translation.** Local-vs-migrate is decided on the VA the context already holds.
2. **Translation never causes a migration.** The frame is on the tile already running the context, so a walk is always local.
3. **No hard partition is visible to software.** A contiguous virtual allocation automatically stripes across every channel. *Siloing* means deliberately choosing VAs with matching tile bits — an allocator choice, not a limit on allocation size.

Channel *t*'s page table covers exactly `{ va : tile_of(va) == t }` — a **partition, not a replication**, so total PTE count is unchanged. That set is strided, so each channel indexes its table by the compacted VA:

```
compact(x)      = (x & low_mask) | ((x >> tile_bits) & ~low_mask)      // low_mask = (1 << shift) - 1
expand(c, tile) = (c & low_mask) | (tile << shift) | ((c & ~low_mask) << tile_bits)
```

Exact inverses; the same pair serves LLC-slice indexing (§5.6).

### 5.2 The tagging granularity has a formula

A per-unit mapping mode is only safe when the tagged unit owns **whole DRAM rows**, so two units in different modes cannot contend for the same bank/column slots. That threshold is:

```
G = row_bytes_per_channel × banks_per_channel × num_channels
```

* **DDR5** — 8 KiB row per rank, 32 banks, 8 channels → **G = 2 MiB**
* **HBM3** — 1 KiB row per pseudo-channel, 16 banks, 32 pc → **G = 512 KiB**

The two modes consume identical capacity from disjoint resources, which is exactly what licenses per-unit tagging:

| mode | occupies | capacity |
|---|---|---|
| `0` STANDARD | one row index, **all banks, all channels** | G |
| `1` NMFC | `num_channels` consecutive rows, **all banks, one channel** | G |

### 5.3 Mapping mode is one bit of the physical address

Not a table, not a PTE bit — **an extra physical address bit, one position above the top of the DRAM range, applied at translation time.**

```
mode_bit    = lg2(dram_size)
tile_of(pa) = pa[mode_bit] ? (pa >> log2(G)) % N          // NMFC:     page-granular, silo'd
                           : (pa >> log2(block)) % N      // STANDARD: block-granular, spread
```

This is strictly better than a mode table or a PTE-carried bit, for one specific reason: **caches tag by address.** A dirty line evicted from L2C has no TLB entry behind it any more, so a PTE-carried bit would have to be stashed in cache-block metadata to survive a writeback. An address bit is already stored, already carried, and already evicted with the line — demand, prefetch and writeback traffic all get it for free with no extra plumbing.

**The two halves are not aliases.** `{0, X}` and `{1, X}` name *different* DRAM cells, because the mode changes the row/bank/channel assignment. The physical address space is therefore one bit wider than DRAM.

**The mode is stamped once, at allocation, and never changes.** That is an invariant `NMFC_VMEM` asserts, not a policy it manages. Changing a live allocation's mode is not a mapping operation at all — it is an ordinary page migration: allocate a frame in the target mode, copy, update the PTE, free the old frame. Standard OS machinery, identical to NUMA page migration or THP promotion, and expressible as ordinary work in the trace if Phase 3 ever wants to charge for it.

A *freed* G-unit carries no obligation at all: its contents are garbage, so it returns to the free list and the next allocation stamps whatever mode it likes. **There is no such thing as converting a pool.**

What remains is ordinary **free-resource fragmentation**, of the familiar huge-page kind. A STANDARD unit occupies one row index across every channel; an NMFC unit occupies N rows on one channel. Satisfying an NMFC request therefore needs N free rows on the chosen channel, which a scattered free list may lack even when total free capacity is ample. The allocator tracks a `(channel, row)` free bitmap — small, exact, and the same structure a real OS keeps for contiguity. On failure the request either spills (§5.7) or falls back to STANDARD for that unit, losing siloing but never correctness.

**Implementation note.** Stock `DRAM_ADDRESS_MAPPING` sizes its top (row) field from `lg2(rows)` and silently drops anything above it, so `NMFC_MEMORY_CONTROLLER` must read and strip the mode bit before slicing.

### 5.4 Mixed page sizes

Real systems still need 4 KiB pages, so the translation system cannot be assessed honestly without them. NMFC data uses **G-sized huge pages**; everything else keeps **4 KiB**.

Three granularities then coincide, which is not a coincidence — all three are forced by the row argument in §5.2:

* the **tagging** granularity (= G),
* the **siloing** granularity (an NMFC-mode unit is on one channel, so this is G),
* the **page size** for NMFC data.

Congruence for NMFC data becomes `tile_of(va) = (va >> log2(G)) % N`. Translation reach improves by `G / 4 KiB` — **512× at 2 MiB**, which directly attacks the "TLBs are useless at graph scale" problem, and matches what real graph frameworks already do with hugepages.

It does not rescue the premise: a 1024-entry TLB at 2 MiB reaches 2 GiB, still ~2% of a 100 GiB graph. Mostly-miss remains the regime. It moves the constant, not the asymptote.

### 5.5 Placement is a dispatch decision

Function code is duplicated on every channel by definition. If a function's N copies sit on **N consecutive huge pages**, copy *t* lands on channel *t* automatically under page interleaving, and the dispatcher forms `entry_pc_base + t · G`. Choosing the tile is one add on the dispatch path — no translation, no lookup — so the policy is free to be as clever as we like. `FUNCTION_FABRIC` owns it:

* `round_robin` — trivially balanced
* `least_loaded` — fewest occupied contexts
* `first_touch` — the tile owning the invocation's first data address; minimizes migrations, may imbalance
* `random` — the control

The compiler places *data* by choosing virtual addresses; the dispatcher places *invocations* by choosing code copies. Load balancing therefore belongs to the OS, not the compiler.

### 5.6 Slice indexing

Tile-select bits sit inside an LLC slice's set index, so a slice would otherwise use 1/N of its sets. `INTERLEAVE_FABRIC` compacts on the way down and expands on the way back, using the §5.1 pair. Both are pure functions of `(address, tile)`, so no per-request bookkeeping. Knob: `compact_tile_bits` (default true). The compaction differs by mode, and the mode is in the address, so the fabric picks the right one with no extra state.

### 5.7 Spill

If a channel's free-frame list is exhausted, the page spills to another channel. A spilled page still routes to `tile_of(va)`; that tile's translation returns a remote frame and the access takes one extra fabric hop. Rare, always correct, needs no filter. **The spill rate is the statistic that says siloing went too far.**

### 5.8 Prior art

| Work | Relevance |
|---|---|
| [IMPICA](https://arxiv.org/pdf/2012.03112) | In-DRAM pointer chasing without CPU TLB/walkers; region-based page table; decoupled address engine. Closest ancestor. |
| [vPIM](https://dl.acm.org/doi/abs/10.1109/DAC56929.2023.10247745) (DAC'23) | Multi-stack PIM over a memory network; contention-aware hash page table; cores dedicated to pre-translation. Closest topology. |
| [Utopia](https://arxiv.org/pdf/2211.12205) (MICRO'23) | Restrictive/flexible mapping split. Our VA→channel congruence at channel granularity. |
| [POM-TLB](https://dl.acm.org/doi/10.1145/3140659.3080210) (ISCA'17) | Very large in-memory TLB. Deferred; fits behind the same abstract base. |
| [Victima](https://arxiv.org/abs/2310.04158) (MICRO'23) | Repurposes L2 blocks for TLB entry clusters. Deferred. |
| [FlexPointer](https://dl.acm.org/doi/full/10.1145/3579854), RMM, Direct Segments | Range-based translation. |
| [Neighborhood-aware](https://www.csa.iisc.ac.in/~arkapravab/papers/micro2018_neighborhood.pdf) (MICRO'18) | Irregular GPU workloads; same locality problem. |

---

## 6. `NMFC_MMU`: one module instead of two forks

Mixed page sizes are blocked by two things in the base, both verified:

1. **`DEFAULT_PTW` cannot terminate a walk early.** `handle_fill` unconditionally does `translation_level − 1`, and `finish_packet` decides last-step by `translation_level <= 0`. There is no way to express "this PDE is a leaf", which is exactly what a huge page is.
2. **A TLB is a `CACHE` with fixed `offset_bits`**, indexed and tagged on `addr >> 12`. One array cannot hold both sizes; real hardware probes two arrays in parallel for precisely this reason.

The obvious fix would be forking both `PageTableWalker` and `CACHE`. Instead, **one new module registered as a `channel` model**:

* Host caches point at it via `lower_translate`, exactly as they point at a TLB channel today — nothing above it changes.
* Inside: a 4 KiB array and a G array probed in parallel, walk caches, and a walker that terminates at whichever level the vmem declares a leaf.
* It exposes a direct call API (the `translation_engine` abstract base) that function cores use, so host and function cores share one implementation.

`channel_module` has ~12 trivial pure virtuals against `cache_module`'s ~25, and the framework explicitly supports an operable channel model. **Net effect: no fork of `CACHE`, no fork of `PageTableWalker`.**

Configurations to compare, all behind the same base class:

| Model | Mechanism | The bet |
|---|---|---|
| `NMFC_MMU` (radix) | Small dual-size TLB + walker against that channel's root; walk references hit the local LLC slice. | None — the honest baseline. Deliberately small, because at graph scale a big TLB is not the answer. |
| `NMFC_MMU` + `NMFC_FLAT_VMEM` | IMPICA's region-based table: region → flat large-page level → 4 KiB level. A vmem model; the MMU's walker follows whatever shape the vmem declares. | Attack walk *depth* rather than TLB reach. Two references instead of four, and PT congruence becomes free because each region is per-channel. |
| per-context cache | Two to four translation entries held in the context itself. | A shared tile TLB is multiplexed by hundreds of unrelated contexts and thrashes; an individual context has excellent locality. The axis with no clear prior art. |

---

## 7. The function core: designed, not copied

**Multi-context, in-order per context, non-speculative.** No ROB, no rename, no branch predictor, no LSQ.

```
token, origin, body handle, pc (index into body),
regfile[≤8 × 64b]      // one cache block — the whole local-state budget
scoreboard[≤8]         // per-register ready bit
ctx_xlat               // a few translation entries, LOCAL TO THIS TILE (§7.1)
state ∈ {READY, RUNNING, BLOCKED, MIGRATING, DONE}, wake_time
```

Per cycle: promote contexts whose `wake_time` passed; drain I$/D$ and MMU completions, waking contexts and clearing scoreboard bits; then issue up to `issue_width` READY contexts:

* **route** — `tile_of(va)` on the instruction or data address. Remote → **migrate** (no translation involved).
* **translate** — consult `ctx_xlat`; on a miss ask the tile's `NMFC_MMU` and block.
* **fetch** — if the ip's block differs from the last fetch block, access the I$ and block. `FLAG_TAKEN_TARGET` charges a configurable fetch bubble, so replaying resolved control flow does not silently gift the function core a perfect branch predictor.
* **compute** — advance PC, re-arm at `now + latency[op_class]`.
* **memory** — issue to the D$ with the translated address, mark the destination register not-ready, **yield the slot immediately** (sleep-on-request).
* **atomic** — serialize against other atomics on the same block via a per-tile lock table. Because all work for an address range converges on one function core, atomicity is a local table rather than a coherence protocol.
* **end of body** → return the live regfile words via the fabric.

**Intra-function MLP comes free.** The trace record already carries source and destination register ids, so a tiny in-order scoreboard lets an invocation with two independent loads issue both — MLP 2 with no reordering hardware.

**Caches.** A small private I$ and D$ per function core, backed by the tile's LLC slice.

### 7.1 Translations are dropped on migration

Not carried. The reason is structural, not staleness: under congruence a cached `va → pa` entry is only *usable* on the tile that owns `va`, and after migrating to `t'` every address the context is about to touch belongs to `t'` by construction. The code entry is worse than unusable — the context's instruction VA literally **changes** on migration, because it now runs copy `t'` at `entry_pc_base + t' · G`.

So every carried entry is guaranteed invalid, and there is no `carry_translations` knob: building a switch for a provably-useless option is clutter. The migration payload is `(token, pc, regfile, scoreboard, origin, home_host)`.

What replaces the knob is a statistic: **translation cold-start cycles after migration**, counted separately from the fabric hop. That cost is now unavoidable, and it is a direct argument for the shallow page table — fewer references to warm back up.

---

## 8. Trace format

64-byte header, fixed 96-byte records, in `inc/nmfc/nmfc_trace.h`, shared verbatim by generator and reader.

```c
struct record {
  trace_instr instr;     // 64 B, field-identical to ChampSim's input_instr
  uint64 token;          // invocation id; 0 for host instructions and hints
  uint64 aux0;           // CALL: entry PC base.  PAGE_HINT: virtual page.  RET: live reg words.
  uint64 aux1;           // CALL: (func_id<<32)|body_len.  PAGE_HINT: (asid<<40)|(region<<32)|tile.
  uint8  kind;           // HOST | CALL | BODY | RET | PAGE_HINT | ATOMIC
  uint8  op_class;       // ALU | MUL | DIV | FP | FP_DIV | BRANCH | LOAD | STORE
  uint8  flag_bits;      // FLAG_NO_RETURN | FLAG_TAKEN_TARGET
  uint8  pad[5];
};
```

`PAGE_HINT`'s `region` field selects the mapping mode (§5.3) and therefore the page size: STANDARD → 4 KiB, NMFC → G.

**Ordering contract:** a `CALL` is immediately followed by its invocation's complete dynamic body, same token. Contiguity means the reader buffers exactly one body and publishes it whole — the function core is never starved by trace supply; it stalls on memory, which is the thing being measured. `aux0` is the entry PC *base*; the dispatcher adds `t · G`.

**Header geometry fields** — page size, block size, tile count, interleave shift, regfile width, address-space count — are a contract, validated against the running configuration and aborting on mismatch.

**Not carried:** register values. The function core replays recorded addresses; ChampSim models no data values anywhere.

---

## 9. Phases

**Phase 1 — models and interfaces.** Three interfaces; `NMFC_HOST_CORE`, `NMFC_MMU`, `FUNCTION_CORE`, `FUNCTION_FABRIC`, `FUNCTION_IMAGE_STORE`, `INTERLEAVE_FABRIC`, `NMFC_VMEM`, `NMFC_FLAT_VMEM`, `NMFC_MEMORY_CONTROLLER`, `NMFC_PRODUCER`; per-context translation caching; hooks and statistics; explicit configs for a 4-compute / 8-memory-tile system and a matched traditional baseline; Catch2 tests under `test/cpp/src/`.

**Phase 2 — ramulator2.** Fresh clone of `CMU-SAFARI/ramulator2`. A `RAMULATOR_MC` model, one per memory tile, configured from per-tile YAML so HBM / DDR5 / LPDDR is a config switch. The MC↔LLC↔function-core coupling becomes an interface the function core can query for channel occupancy and row-buffer state.

**Phase 3 — pseudo-compiler and evaluation.** `tools/nmfc/` builds CSR graphs, runs BFS / PageRank / pointer-chase traversals, and emits both an NMFC trace and a matched baseline trace of the same computation. Pseudo-compilation is the placement pass: VA selection, function copy layout, mode assignment. Then the sweep.

---

## 10. Statistics

Through the existing `module_lifecycle::end_phase(stat_report&)` path.

* **Function core** — invocations completed; cycles by context state; mean/P99 residency; contexts occupied; issue-slot utilisation; migrations in/out; atomic conflicts; I$/D$ hit rates; achieved MLP per context and per core.
* **Translation** — per-context cache hit rate split by code vs data; TLB hit rate by page size; walk count and latency distribution; remote-walk rate; **translation cold-start cycles after migration**; translation cycles as a share of context blocked time.
* **Mapping / allocation** — allocations by mode; NMFC-mode allocation failures and STANDARD fallbacks; largest allocatable NMFC run per channel; spill rate; per-channel free-frame imbalance.
* **Placement** — invocations per tile under each policy; migration rate.
* **Fabric** — messages by class; queue occupancy; link utilisation; back-pressure stalls.
* **FTU** — offloads issued; in-flight mean/max; cycles stalled on back-pressure; fire-and-forget share.

---

## 11. Layout

```
inc/nmfc/     nmfc_trace.h  nmfc_types.h  tile_map.h  nmfc_hooks.h
              nmfc_host_core.h  nmfc_mmu.h  translation_engine.h
              function_core.h  function_fabric.h  function_image.h
src/nmfc/     nmfc_registry.cc  nmfc_host_core.cc  nmfc_mmu.cc  function_core.cc
              function_fabric.cc  function_image.cc  interleave_fabric.cc
              nmfc_vmem.cc  nmfc_flat_vmem.cc  nmfc_memory_controller.cc  nmfc_producer.cc
config/nmfc/  nmfc_4c8m.json  baseline_4c.json  (+ sweep templates)
tools/nmfc/   generator (graph build, traversal, placement pass, trace emit)
docs/nmfc/    this document
test/cpp/src/ 5xx-nmfc-*.cc
Makefile      + one line: src_sources also globs src/nmfc/*.cc
```

---

## 12. Risks

* **`NMFC_HOST_CORE` is a ~1,800-line fork.** Now the only one. Mitigation: literal copy plus marked `// NMFC:` blocks so `diff` against upstream stays the maintenance tool.
* **Translation could dominate every result.** That is why the mechanism sits behind one abstract base with several configurations. If the radix baseline buries everything, that is the finding that motivates the others.
* **Per-context translation caching may not work for pointer chasing.** Every hop is a new page. Expected, and reported honestly: the code entry hits, the data entries mostly do not, and CSR-style traversal is where it earns or loses its keep.
* **Free-resource fragmentation.** An NMFC unit needs N free rows on one channel, so a scattered free list can fail an allocation while total free capacity is ample — the familiar huge-page problem, not a mode-specific one. Mitigation: a `(channel, row)` free bitmap, with fallback to spill or to STANDARD mode; failures and the largest allocatable run per channel are reported, so fragmentation is visible rather than silent.
* **Siloing exhausts a channel's frames.** Handled by spill; spill rate and free-frame imbalance are the evidence.
* **Address compaction must be exactly invertible, in both modes.** A unit test pins `expand(compact(x), tile_of(x)) == x` over a large address sample, for each mode.
* **The function core could look artificially good.** It replays resolved control flow, so it never mispredicts. `FLAG_TAKEN_TARGET` plus a configurable fetch bubble is the honesty knob, with a sensitivity run.
* **Warmup semantics.** Contexts drain across a phase boundary; the phase controller's progress unit stays host instructions retired.

---

## 13. As built

Status as of the end of the first implementation pass. Everything below is on
branch `nmfc`; the full ChampSim suite plus the NMFC tests is green at 753 test
cases and 412,486 assertions.

### Phase 1 — complete

| Piece | Where | Notes |
|---|---|---|
| `tile_map` | `inc/nmfc/tile_map.h` | Address layout, mode bit, compaction. Invertibility pinned over 200k addresses in both modes. |
| `INTERLEAVE_FABRIC` | `src/nmfc/interleave_fabric.cc` | Memory network, as a `channel` model. |
| `TILE_PORT` | `src/nmfc/tile_port.cc` | Keeps both paths into a slice in one address space; asserts locality. |
| `FUNCTION_FABRIC` | `src/nmfc/function_fabric.cc` | Three message classes, four placement policies. |
| `FUNCTION_CORE` | `src/nmfc/function_core.cc` | Multi-context, in-order, non-speculative. |
| `FUNCTION_IMAGE_STORE` | `src/nmfc/function_image.cc` | Bodies, with occupancy high-water. |
| `NMFC_VMEM` | `src/nmfc/nmfc_vmem.cc` | Congruent allocation, mode stamping, spill. |
| `NMFC_HOST_CORE` | `src/nmfc/nmfc_host_core.cc` | The one fork, with the FTU. |
| `NMFC_PRODUCER` | `src/nmfc/nmfc_producer.cc` | Trace reader; enforces the geometry contract. |

### Phase 2 — complete

`RAMULATOR_MC` (`src/nmfc/ramulator_mc.cc`) drives a per-tile single-channel
ramulator2 machine through its `External` frontend — current ramulator2 ships
one, so no custom frontend was needed. Entirely opt-in: the file compiles to
nothing without `NMFC_WITH_RAMULATOR`, and the Makefile block is guarded, so the
default build needs nothing in `ext/`.

Cross-check on the partitioned workload: **620,645** cycles against the built-in
DRAM model's **646,546**, a 4% difference, with no clock mismatch at 417 ps
versus DDR5-4800.

### Phase 3 — generator and first results

`tools/nmfc/nmfc_gen` builds a CSR graph and emits *both* traces from one
traversal, so the two runs touch the same addresses in the same order.
`--locality` controls what fraction of a vertex's neighbours share its
partition; `--partition silo` gives vertex v's whole footprint to tile v % tiles
through per-tile arenas.

2M-vertex kron graph, mean degree 16, 8000 visits, 1 compute tile against 4
memory tiles. Both traces run to end-of-trace, so total cycles is the
comparable number — IPC is not, because the NMFC host stream is far shorter for
the same work.

| workload | NMFC cycles | baseline cycles | speedup | migrations/invocation |
|---|---|---|---|---|
| scattered | 679,452 | 3,386,272 | **4.98×** | 14.4 |
| partitioned | 646,546 | 3,522,385 | **5.45×** | 1.4 |

Two readings. The placement pass works — partitioning cut migration traffic by
**10×**. And migration was *not* the bottleneck at this scale: removing 90% of it
moved the speedup only from 4.98× to 5.45×, because with four tiles and an
8-cycle hop the cost is dominated by memory latency rather than by the network.
Where the leverage actually is, is the kind of thing the sweep exists to find.

### Known gaps

* **Function-core translation is an oracle.** Addresses are correct, but the
  walk costs nothing. `ctx_xlat` and its clear-on-migration path exist and the
  cold-start statistic is wired, but that number currently measures only the
  cold instruction fetch. `NMFC_MMU` (§6) is what closes this, and until it does
  the NMFC side is flattered by an unknown amount.
* **`NMFC_MMU` and `NMFC_FLAT_VMEM` are unbuilt**, so mixed page sizes and the
  per-context translation cache are unmeasured.
* **The workload is synthetic.** The GAP suite traces on this machine are real
  BFS/BC/PageRank and should be the validation target for the generator.
* **One compute tile.** Multi-tile contention is unexercised.
