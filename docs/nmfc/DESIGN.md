# Near-Memory Function Core (NMFC) — Design

**Base:** `ChampSimFix` @ `a2684f5c` (branch `nmfc`), cloned into `ChampSimArchWork/nmfc`.
## 0. Invariants

These are settled. They are not re-derived, not traded away for a passing
measurement, and not rediscovered by hitting an assertion. Read this section
before changing anything in `src/nmfc/`.

1. **An offload is an instruction.** `FORK rPC, v512` takes a general register
   holding the callee's entry PC and a 512-bit vector register that *is* the
   callee's register file. `JOIN v512` retrieves it. The offload aperture is
   how that gets written into a fixed ChampSim trace record; it is a file
   format, not a mechanism. See §4.

2. **512 bits in, the same 512 bits out.** The whole register file returns.
   Register positions carry no meaning across the boundary. A function needing
   more live values than the file holds cannot be offloaded. See §4.1.

3. **Every channel walks locally.** There is **one page table**, and it lives
   on duplicate pages -- the same page type function code uses -- so every tile
   holds a local copy. That is not merely one way to keep walks local: routing
   happens *after* translation (invariant 12), so a tile must resolve an address
   before it can know whether it owns it, and only a duplicated table lets it
   resolve a foreign address without leaving home. A single table on one channel
   reached over the fabric is a bug, and routing the walks is not the fix. (The
   older "N roots partitioned / one root duplicated" framing belongs to the
   virtual-address partitioning that invariant 12 rejects.) See §5.0.2.

4. **Placement is a translation-time decision by the address space's owner.**
   The virtual address is translated to physical before it crosses the fabric,
   and the physical copy handed back names the tile. This is what makes load
   balancing a run-time decision rather than a layout the compiler baked in.
   Nothing may consult data the invocation has not touched yet.

5. **Migration is expected, and it is evidence.** The failure mode is
   frequency, not occurrence: roughly one per thousand instructions is fine,
   and each costs 72 bytes on the fabric. A migration says either the function
   or the page was in the wrong place, and the routing policy is supposed to
   act on that.

6. **Physical placement without a NUCA/NUMA policy is not the design.**
   Working sets are moved together while access stays balanced across tiles.
   Round-robin, least-loaded and first-touch are not substitutes for it.

7. **The function core has a register file and no stack.** A function that
   spills cannot run. Check the disassembly, not the source.

8. **The machine is 5.67x faster than the reference algorithm on a standard
   core**, on the same 645,268-vertex traversal, running the same
   direction-optimising BFS and executing 0.89x its instructions. The
   architecture gain on identical work is 6.13x. The same architecture measured
   *0.91x -- a loss --* against a top-down-only kernel, because that kernel
   handed it 5.3x more work than the reference needed. Never compare the machine
   against a weaker algorithm; it condemns hardware that is working. See §21.3.
   This figure was measured with the ChampSim core model, whose contexts kept
   issuing past an outstanding load; the Rev core sleeps on one (§25.4). The two
   have different memory-level parallelism at the same context count, so the
   number is not a target Rev should be expected to hit without saying which
   core produced it.

9. **A grain sits on the tile its *physical* address names.** Congruence is the
   property the placement pass maintains -- it chooses the frame, and the frame
   it chooses is on the tile the vtile asked for -- not an arithmetic shortcut
   on the virtual address. Neither is it a counter: `NUCA_ROUTER` balances by
   remapping whole components in `remap_grain()`, never by where a grain was
   first touched. **Check it, on every run.** The assertion that guarded this
   was gated on a routing order nothing used, so it had never executed once, and
   the violation it would have caught cost 75% of accesses routed to a tile
   their address never named and 27% of run time. See §18.

10. **An invocation may extend, never fan out.** A function that continues into
   another function is a *successor*: the context carries forward, its slot is
   reserved in place, one becomes one. A function that creates a second live
   invocation is a spawn, and spawns of spawns are unbounded by construction --
   there is no admission control that makes them safe without a per-core
   tracking unit and a depth bound, and "you may only spawn one deep" is not a
   design, it is a constraint nobody can honour. If work is discovered that the
   function cannot carry out itself, the unit of work is shaped wrong: it does
   not own the data it discovered. Reshape it rather than spawn. See §20.2.

11. **Migration moves the work instead of the data, at parity.** 72 bytes of
   register file and PC against the 64-byte line a foreign access would have
   cost -- and the two are alternatives, never both, so migration traffic
   *subsumes* data traffic rather than adding to it. This is what makes local
   atomicity free. Nor is arrival costly: 2.2-2.3 cycles measured, with a
   100% instruction-cache hit rate, because the code is replicated on every
   channel and the departing tile's slice never held the data anyway. The only
   real cost is time in transit, so functions still stay planted on one tile and
   do non-trivial work there -- budget roughly one migration per thousand
   instructions -- but for latency, not bandwidth. See §20, §21.

---

**Revision 6** — §13 records what is actually built. Earlier:  mapping mode becomes a physical address bit, stamped once at allocation; mixed page sizes; `NMFC_MMU` replaces two would-be forks. Supersedes revisions 1–4.

---

12. **Tiles are partitioned by *physical* address, and co-location is the
   vtile's job.** Virtual-address partitioning is rejected: it leaks the tile
   layout into the virtual address space, confines the compiler to a fixed
   mapping, and lets a program steer placement by choosing addresses. A hint is
   a **vtile** -- a compiled-in label naming a coherent set. Pages carrying the
   same vtile are co-located *wherever they sit*; distinct vtiles are unrelated
   and are spread to balance load, unless that vtile already has a home, which
   its later pages follow. Nothing has to be adjacent, aligned, or contiguous
   for two things to land together. Grain alignment is a **space** concern only
   -- it stops a small object obliging the allocator to spend a whole grain --
   and the one thing a grain genuinely cannot do is carry two *types*, because
   half of it cannot be duplicated on every tile while the other half is silo'd
   to one. See §5.0.

13. **This is a standard memory system with one change.** A modern machine is:
   private L1I/L1D + L2 per core, a shared LLC across cores, then memory
   controllers and channels. The L2-to-LLC interconnect **is** a fabric, and
   coherence is enforced at that boundary. NMFC alters exactly one thing: the
   **address partition moves to the fabric**, slicing LLC -> memory controller
   -> channel *vertically*, instead of waiting until after the LLC to slice. Do
   not derive anything from a picture more exotic than that. The host reaches
   memory by the ordinary path -- L1, L2, fabric, the LLC slice owning the
   address -- and no part of that is NMFC-specific.

   **The function core lives at the top of one vertical stack**, beside the
   fabric interface into that LLC slice, on the slice's side. So its own path --
   fetch, load, store, page walk -- never crosses the fabric; invariant 3 is a
   consequence of where the core sits, not a separate rule. There is **one**
   fabric, carrying coherence, migration, and LLC/DRAM access. A model that puts
   a tile's own caches on a network has moved the core off the tile no matter
   what the diagram says, and a second interconnect for NMFC traffic is what
   makes §21 unmeasurable. See §5.9.

14. **The tile sees the coherence traffic for its own slice, and wins it.**
   Sitting at the fabric interface into the slice is what makes this true: a
   reference to a block the function core is touching is *visible* to it, and
   can therefore be treated as ownership. **NMFC cores take strict priority in
   MOESI.** Traffic between invocations on one core pays no coherence penalty at
   all -- they share a slice and there is no second copy (§21, §25.6). What can
   cost something is a **host** reference to a block an NMFC core is modifying,
   and that direction only: an NMFC core does not pay for host modifications to
   its lines where that can be avoided. This is also why migration stays cheap
   in practice rather than by assertion -- offloading the shared and
   memory-bound work to the cores is what makes host memory and coherence
   traffic *rare*, and that is the headroom migration runs in. See §5.9.

## 0. Does it do the thing?

The premise is that a workload with high thread parallelism and MLP about one
per thread leaves memory bandwidth on the floor, and that a function core
time-multiplexing many stackless invocations can pick it up. That is a claim
about *bandwidth utilisation*, not about speedup, and it went unmeasured for a
long time in favour of numbers that were easier to produce.

GAP BFS on kron-24, memory modelled by **ramulator2**: one instance per memory
tile, DDR5-4800, a 32-bit channel each, so 19.2 GB/s per channel and 76.8 GB/s
across the four. Grain derived from that device rather than set beside it (§5.2),
which puts it at 1 MiB.

| configuration | cycles | DRAM requests | B/cycle | of peak |
|---|---|---|---|---|
| host only | 10,733,144 | 648,992 | 3.87 | **20.2%** |
| NMFC, chase decomposition | 2,713,767 | 601,172 | 14.18 | 73.8% |
| NMFC, spawn decomposition | 2,552,945 | 596,611 | 14.96 | **77.9%** |

The same measurement against ChampSim's own DRAM model, at the grain that was
hardcoded before the geometry was derived, gave 13.7% and 76.9% -- so the
conclusion survives both a real memory model and a corrected grain, which is
most of the reason to trust it.

An out-of-order core with a 352-entry reorder buffer extracts 13.7% of the
available bandwidth from this workload. The memory tiles extract 76.9% of it,
doing the same work against the same memory system. That ratio is the
architecture; the 6.19x cycle speedup is a consequence of it.

The remaining 23% is the honest open question, and §14.0 covers what has been
ruled out: it is not migration, which the spawn decomposition reduced to 0.0015
per instruction, and it is not the fabric queues, the cache ports or the
tracking unit, each of which reported itself the bottleneck and none of which
was.

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

* **Memory network** — ordinary loads/stores. `INTERLEAVE_FABRIC` (one per compute tile) routes an L2C miss to the LLC slice of the tile owning the address. This is the **ordinary L2-to-LLC path of any modern machine**; the only thing NMFC changes is that the address partition is applied *here*, so the slice, its controller and its channel form one vertical stack (§5.9, invariant 13).
* **Function network** — `FUNCTION_FABRIC` carries invocations, migrations, and returns.

> **Two corrections to the above.** First, an earlier version of the first bullet
> ended "no function-core involvement", which is wrong and was actively
> misleading: the LLC slice is where the two meet. Host requests arriving over
> the fabric and function-core requests arriving from `fc D$` land in the *same*
> slice, which is the coherence point, and the function core's ability to see a
> reference to a block it is touching is exactly what invariant 14's ownership
> and MOESI priority are built on. Second, the split into two networks is a
> property of this ChampSim topology, not of the architecture: §24 step 3
> requires them to be **one** fabric on Rev, because migration that cannot
> contend with data traffic cannot be shown to subsume it (§21).

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

### What an offload *is*, and what the simulator *encodes*

Read this before §4's implementation, because the two are repeatedly confused
— including in this document's own diagrams, more than once.

**The architecture.** An offload is an instruction.

```
FORK  rPC, v512     ; rPC  = a general register holding the callee's entry PC
                    ; v512 = a 512-bit vector register: the callee's regfile
JOIN  v512          ; retrieves that regfile once the invocation returns
```

The vector operand is not an argument *to* the regfile, it **is** the regfile:
§7 sizes a function's entire local state at `8 × 64b`, one cache block, 512
bits. A fork hands over all of it; a join takes all of it back. That is why
every fabric message is about 72 bytes — 64 of regfile plus a program counter
— and why migration costs the same as a fork.

**The encoding.** ChampSim's trace record has no field for a fork, and
`ooo_model_instr` is not to be modified. So the producer names an invocation
by a source-memory address inside a reserved *offload aperture*, and
`do_memory_scheduling` diverts it into the FTU instead of the load queue.

The aperture is how a fork is written down in a fixed 64-byte trace record.
It is not a mechanism in the machine. The machine has no aperture, the
function core does not detect calls by address range, and a design that
reasons from "a load in the aperture is a fork" has mistaken a file format for
an instruction set. Anything built on that reading — a tile-side calling
convention, a claim about what the core detects — is wrong at the root even
when it is internally consistent.



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

## 4.1 What fits in a function

The vector operand is the whole of a function's state, and it travels in both
directions: 512 bits go in with the fork, and the same 512 bits come back on
completion. Register *positions* carry no meaning across that boundary — the
join knows how to read what it gets — so a function is free to use its eight
words however it likes.

That gives an admission test for a candidate function, and it is measurable
rather than a matter of taste.

**Count peak simultaneous liveness, not registers touched.** A function may
name nine registers over its lifetime while never holding more than eight live
values at once; a compiler with eight registers would allocate it without
spilling. Counting distinct registers instead reported 17 and 21 for the two
BFS kernels, which is an artifact of the tracer recording partial-width views
as separate ids -- `rax`, `eax` and `al` are three ids for one register.

**A register that is never read does not count.** It holds nothing the join
will consume.

**Exclude the program counter, the stack pointer and the flags.** The PC is
carried beside the regfile, not in it; this machine has no stack, so a
function that genuinely needs one cannot run here; flags are internal to an
instruction's execution.

Then the rule is simple. If peak liveness exceeds the regfile, the function is
rejected -- unless the excess is an artifact of how it was written, in which
case it is rewritten. `nmfc_claim` needed nine because it kept its base
pointer live purely to compute `w - base` at the end; returning the end
pointer and letting the caller subtract made it eight, and the caller's
subtraction is host work that the trace already accounts for. Both BFS kernels
now sit at exactly eight.

One caveat worth keeping in view: on x86-64 `lock cmpxchg` pins its comparand
in `rax`, which is also the return register, so a compare-and-swap costs a
register the algorithm did not ask for. Whether that pressure is real depends
on whether the modelled ISA has the same constraint. It is a property of the
host the traces are collected on, not of the machine being designed.

---

## 4.3 Two invocation loops

An invocation returns its 512-bit register file either way. What differs is
whether that is where the *result* lives, and the two cases need different
things from the trace.

**Register-returning.** The value comes home in the vector register. `FORK`
starts it, `JOIN` retrieves it, and the join is a register dependency: the
consumer of the returned value is where the host waits. Nothing extra is
needed to express this -- the compiler already emits a use of the return
value, and that use is the join.

Its limit is structural. A register dependency sits wherever the compiler put
it, which for `x = f(...)` is the instruction after the call, so the host can
have no more outstanding than its reorder buffer reaches -- measured here at
fifteen to eighteen invocations against a tracking unit of 1024.

**Memory-committing.** The value is not returned; the invocation writes a block
to memory and the caller reads it later. The caller has nothing to wait on at
the call site, so it can fork as many as the tracking unit holds. A standard
core consuming this result busy-spins on the block until it is committed.

That spin is the problem, and it is a problem *for the trace*, not for the
machine. In a traced program the call was synchronous, so the block was always
already written and the spin executed zero times. Nothing about the wait
survives into the trace, and a trace-driven simulator cannot reconstruct it
because it does not model values -- so a memory-committing loop replayed
naively lets the host read results it never waited for, and the optimism is
invisible in every number it produces.

So the trace needs two things the register loop does not:

* **A symbol hook marking where the spin begins.** A named, non-inlined
  function the host calls before touching an invocation's output, so the
  annotation pass knows the wait site exactly rather than inferring it from
  whichever load happens to come first.
* **A primitive meaning "block until address A is committed by invocation
  B".** The annotation pass knows which invocation wrote which addresses, so
  it can resolve A to B; the marker tells it where the block belongs. What the
  simulator sees is an ordinary deferred join, placed where the host actually
  needed the answer.

The hook must *touch* the address it waits on, because a trace records
addresses and not register values -- a marker that merely takes a pointer
argument leaves nothing behind to resolve.

---

## 4.1.1 Two axes, optimised separately

There are two questions and they must not be answered with the same
experiment.

**Axis 1 -- the hardware.** What the machine achieves under ideal conditions.
Optimising it needs workloads that exercise the full band of behaviours and
that are known, by construction, to use the machine well: nearly all the work
offloaded, enough invocations resident to fill the contexts, invocations large
enough that dispatch and migration amortise, and no serialisation left on the
host.

**Axis 2 -- the mapping.** How well a real algorithm can be expressed on that
machine by the pseudo-compiler. This is a question about programs, and its
answer is allowed to be bad without implicating the hardware.

Axis 1 needs axis 2 to know where the design space is, but a hardware
conclusion drawn from a workload that misuses the hardware is a conclusion
about the workload. That failure has already happened here, repeatedly and
expensively: kernels offloading seven to eleven percent of their work, with a
join one instruction behind every fork, produced runs at four resident
contexts of four thousand and one percent channel occupancy. Read as hardware
results they said migration was ruinous, the placement policy was inert, and
larger invocations did not help. Every one of those was an artifact of the
mapping. The kernel that offloads seventy-five percent migrates three hundred
and sixty times more and is the fastest measured.

So before any run is used to judge the hardware, it has to pass a
qualification: what fraction of the work left the host, how many invocations
were resident, and whether the host ever blocked. A run that fails those is
evidence about axis 2 only, and must be labelled that way rather than quoted
as a property of the machine.

---

## 4.2 Placement policies: what moves, and who decides

Several policies are under test. They are not variations on one idea; they
differ in *what is allowed to move*, and the workload must not assume any of
them.

**The OS's standing job, under every policy.** Initial placement; the page
tables; duplicate mappings; and remap support. That is the mechanism the
policies are built on, and it does not change between them.

**A policy where the vmem places by address.** Supported, and it must keep
working -- under congruence the virtual address names the tile and placement
is exact. It is not the one we are trying to show off, and a workload written
around it has assumed the answer: sorting work by `tile_of(&x)` in the program
is the compiler baking in a layout, which §5 rules out. Under the physical
routers the virtual address names no tile and that sort means nothing.

**The policy we care about: both ends move.** Data used together is migrated
onto the same tile, *and* functions migrate to their data. Source and sink are
both moveable, and the job is to partition them evenly across the tiles rather
than to pile either onto one. A policy that moves only one end can always be
defeated by the other.

**Grain-granular NUCA is a swap, not an allocation.** When a whole grain is the
unit, moving one is closer to a tile swap -- exchange which channel backs it --
than to allocating a fresh page and copying. That is cheaper and it is why the
grain is the unit.

**Sub-grain swaps are viable and sometimes necessary**, for structures that
must be present on every channel. But the same effect is better had from a
*duplication* policy in NUCA, which reaches it without the bookkeeping a
partial swap drags in. Prefer duplication; keep the swap as the fallback it is.

**What this means for a workload.** It computes no tiles. It issues work, the
OS places it, the policy moves both ends, and the migrations are the evidence
the policy runs on. A kernel that sorts by tile is testing the allocator, not
the architecture.

---

## 5. Address space, mapping mode, and translation

### 5.0 Partitioning is physical, and §5.1 below is superseded

**Tiles are partitioned by physical address.** Everything from §5.1 to §5.7 is
written against virtual-address partitioning, which was considered and
**rejected**. It is kept because the compaction arithmetic and the grain-size
derivation survive, but its routing model does not: if a passage reads as though
`tile_of(va)` decides routing, it is describing the rejected design.

**Why not virtual.** Partitioning the virtual address space leaks
hardware-specific detail into it, exposes the tile layout directly, confines the
compiler to a fixed mapping, and hands programs a lever on placement that is
unfriendly to a shared system.

**Why physical.** Translation is unavoidable anyway -- functions operate in the
virtual address space -- and dispatch happens once per function, so the cost is
amortised. Partitioning physically simply moves the **migration trigger to after
translation** rather than before it. Two things follow: co-location hints stop
being tied to particular virtual address ranges, and nothing requires a
direct-mapped VA-to-PA space.

### 5.0.1 Three page types

Their sizes are not free parameters. They follow from the machine's memory
configuration, and the grain and duplicate sizes are chosen so that a page
controls its own internal layout -- which is what lets it take physical frames
that all sit on one tile.

| type | placement | what it is for |
|---|---|---|
| **regular** | striped across memory tiles | ordinary data |
| **grain** | silo'd to one tile | data that should be spatially local to an NMFC core |
| **duplicate** | grain x N, identical on every tile | what every core needs: instruction pages, read-only data |

**Duplicates are read-only by construction, not by convention.** User space
cannot write one independently -- the copies appear identical -- and kernel
writes are duplicated as well. A duplicated page occupies M bytes of which only
M/N is writeable. Page tables are read-only and live on duplicate pages, which
is ordinary page-table behaviour rather than a special case made for them.

### 5.0.2 Translation, and why walks stay local

Local TLBs help, and large grain pages make TLB locality good.

**Walks must remain local.** That is what preserves TLB locality, keeps
translation from causing migration, and lets the walker run in hardware without
handing control back to the kernel except on a real fault. Two arrangements make
walks cheap:

- ~~an **independent page-table root per tile**~~ -- **dead.** It *requires*
  virtual-address partitioning, so it drags back the design rejected above. It
  is listed only so that meeting the phrase elsewhere identifies it as this
  corpse and not as an option (§5.1);
- **a single page table living on duplicate pages**, giving every tile a local
  copy. **This is the design, and there is no second candidate.**

It is not merely the surviving alternative: it is what makes physical
partitioning work.
Routing after translation means a tile must translate an address *before* it can
know whether it owns it, so it must be able to resolve a foreign address
locally. Per-tile partitioned roots cannot -- discovering "this is not mine"
would itself take a remote walk, which is the one thing that must never happen.

### 5.0.3 Hints are virtual tiles

A hint is **compiled in**, and it tells the OS which virtual pages belong to the
same *coherent set*. These are virtual tiles, and a vtile can be associated with
grain pages.

- pages marked vtile *k* are co-located with the other vtile *k* pages;
- distinct vtiles are unrelated -- vtile 1 and vtile 5 have nothing to do with
  each other -- so they are placed to spread load across the tiles;
- unless pages of that vtile are already resident somewhere, in which case new
  pages of it follow them there.

This is what co-location looks like once placement is physical: a label the
compiler attaches to pages, not a range of virtual addresses the hardware layout
has been leaked into.

### 5.1 REJECTED: virtual-address partitioning (only the arithmetic survives)

> **Everything this subsection used to contain has been deleted, not merely
> flagged.** It described VA-partitioned routing -- `tile_of(va)` deciding
> local-vs-migrate before translation, a page table *partitioned* across N
> per-channel roots (`VIRTUAL_FIRST`) or *duplicated* one-per-channel
> (`TRANSLATE_FIRST`), and a compiler placing data by choosing virtual
> addresses. **None of that is the design.** It was kept for a while behind a
> "SUPERSEDED" banner and was cited as current several times anyway, which is
> what banners are worth. The live statements are:
>
> - routing is on the **physical** address, after translation -- §5.0, invariant 12;
> - there is **one** page table, on duplicate pages, so every tile holds a copy
>   -- §5.0.2, invariant 3. Not "one of two arrangements": the only one;
> - co-location is a **vtile**, a compiled-in label, not a virtual address range
>   -- §5.0.3, invariant 12;
> - placement is chosen **at translation time by the address space's owner** --
>   invariant 4.
>
> If you are reading `VIRTUAL_FIRST`, `TRANSLATE_FIRST`, per-channel roots, or
> "the compiler places data by choosing virtual addresses" anywhere, in any
> file, it is this dead design and it should be deleted on sight.

What survives is one piece of arithmetic, which is about **slice indexing** and
has nothing to do with routing. Tile-select bits sit inside an LLC slice's set
index, so a slice would otherwise use 1/N of its sets; the fabric compacts them
out on the way down and back in on the way up (§5.6):

```
compact(x)      = (x & low_mask) | ((x >> tile_bits) & ~low_mask)   // low_mask = (1 << shift) - 1
expand(c, tile) = (c & low_mask) | (tile << shift) | ((c & ~low_mask) << tile_bits)
```

Exact inverses, pure functions of `(address, tile)`, no per-request bookkeeping.

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

Congruence for NMFC data is maintained by the placement pass choosing a frame on the tile the vtile asked for (invariant 9); it is not arithmetic on the virtual address (§5.1 is dead). Translation reach improves by `G / 4 KiB` — **512× at 2 MiB**, which directly attacks the "TLBs are useless at graph scale" problem, and matches what real graph frameworks already do with hugepages.

It does not rescue the premise: a 1024-entry TLB at 2 MiB reaches 2 GiB, still ~2% of a 100 GiB graph. Mostly-miss remains the regime. It moves the constant, not the asymptote.

### 5.5 Placement is a dispatch decision

Function code is duplicated on every channel by definition. If a function's N copies sit on **N consecutive huge pages**, copy *t* lands on channel *t* automatically under page interleaving, and the dispatcher forms `entry_pc_base + t · G`. Choosing the tile is one add on the dispatch path — no translation, no lookup — so the policy is free to be as clever as we like. `FUNCTION_FABRIC` owns it:

* `round_robin` — trivially balanced
* `least_loaded` — fewest occupied contexts
* `first_touch` — the tile owning the invocation's first data address; minimizes migrations, may imbalance
* `random` — the control

The dispatcher places *invocations* by choosing code copies. The compiler does **not** place data by choosing virtual addresses -- that was §5.1's rejected design; it attaches a **vtile** and the OS chooses the frame at translation time (§5.0.3, invariants 4 and 12). Load balancing therefore belongs to the OS, not the compiler. Note also that the four policies listed above are not a NUCA policy and do not substitute for one (invariant 6).

### 5.6 Slice indexing

Tile-select bits sit inside an LLC slice's set index, so a slice would otherwise use 1/N of its sets. `INTERLEAVE_FABRIC` compacts on the way down and expands on the way back, using the §5.1 pair. Both are pure functions of `(address, tile)`, so no per-request bookkeeping. Knob: `compact_tile_bits` (default true). The compaction differs by mode, and the mode is in the address, so the fabric picks the right one with no extra state.

### 5.7 Spill

If a channel's free-frame list is exhausted, the page spills to another channel -- the vtile does not get the home it asked for, and its frame lands elsewhere. The old wording here said the access then "takes one extra fabric hop" to a remote frame. **That is wrong and belongs to §5.1's dead design**: there is no remote data path (§26.4). Routing is on the physical address, so a spilled page simply resolves to a different tile and a context reaching it *migrates*, exactly as for any other address it does not own. The cost of a spill is therefore a migration and a broken co-location, not a longer access. **The spill rate is the statistic that says siloing went too far.**

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

### 5.9 The memory hierarchy, and where coherence lives

This section exists because the architecture kept being reconstructed from the
pieces instead of read, and the reconstructions came out more exotic than the
machine. The machine is ordinary. State it plainly first.

**The baseline.** A modern memory system is private L1I/L1D and L2 per core, a
shared LLC across cores, then memory controllers and channels. The **L2-to-LLC
interconnect is a fabric**, and it is where coherence is enforced: above it
caches are private, at it they are made consistent, below it there is one copy.
Nothing in that sentence is NMFC.

**The one change.** NMFC applies the **address partition at the fabric**. A
conventional machine routes an L2 miss into a shared LLC and only slices
afterwards, on the way to the controllers. NMFC slices *vertically*: the fabric
picks the tile, and LLC slice, memory controller and DRAM channel below it are
one stack owned by that tile. That is the whole architectural delta of the
memory system. Every other property in this document is a consequence of it or
of what is then placed on top.

**Where the function core sits.** At the top of one vertical stack, beside the
fabric interface into that slice -- on the slice's side of the fabric, not the
host's. Its private `fc I$` and `fc D$` sit above the slice exactly as a core's
L1s sit above an LLC, and its page-table walker reads through the same slice
(§2's diagram, `NMFC_MMU t --walk--> LLC SLICE t`). Consequences:

- Its fetches, loads, stores and walks **never cross the fabric**. Not because
  they are exempted, but because there is no fabric between a core and the slice
  it sits on. This is invariant 3, obtained structurally.
- The host reaches the same slice by the **ordinary path** -- L1, L2, fabric.
  There is no NMFC-specific host memory path, and none is wanted.
- Host requests and function-core requests **converge in the slice**. It is the
  coherence point for both.

**What the fabric carries**, therefore, is coherence, migration, and LLC/DRAM
access -- and it is one fabric, not a memory network with a private NMFC channel
beside it (§24 step 3, §2's correction note).

#### Coherence: the tile sees it, and wins it

The convergence above is not an inconvenience to be routed around, it is the
mechanism. Because the function core sits at the fabric interface into its own
slice, **coherence traffic for the blocks in that slice is visible to it**. A
reference to a block the core is currently touching arrives where the core can
see it, and can therefore be treated as ownership rather than as a request to be
serviced blindly.

**NMFC cores take strict priority in MOESI.** The asymmetry is deliberate and it
is one-directional:

| | cost |
|---|---|
| invocation ↔ invocation, same core | **nothing.** One slice, one copy, no second cache to invalidate. §21's "no coherence question because no second copy exists", and what makes §25.6's atomics a local table rather than a protocol. |
| host reads/writes a block an NMFC core is modifying | the **host** pays. |
| NMFC core touches a block a host has modified | avoided wherever it can be. An NMFC core does not pay for host activity on its own lines. |

The justification is not that near-memory cores deserve to win arguments. It is
that the function core is the *ordering point* for its address range by
construction -- every invocation touching that range has migrated there (§21) --
so it is the party that can serialise without a protocol, and making it yield to
a remote requester would replace a free ordering point with an expensive one.

#### As built

`src/nmfc/` implements this. The pieces:

| component | what it is |
|---|---|
| `nmfc.NMFCCache` | A private cache with MOESIF line states. One of these is an L1, an L2 or a function core's I$/D$ depending only on what it is wired between. |
| `nmfc.NMFCCoherenceFabric` | The L2-to-LLC fabric: the MOESIF directory, the address partition, distance in hops, bandwidth in bytes-per-cycle, and invariant 14's admission order. |
| `nmfc.NMFCMemLink` | A `StandardMem` interface onto an `NMFCCache`, so Rev and `NMFCTile` attach to it unchanged. |
| `test/coherent_memory.py` | The hierarchy: host L1 and L2, per-tile fc caches, one slice/controller/channel stack per tile below the fabric. |

**memHierarchy is used below the coherence point and nowhere above it.** Not
preference: it implements MESI. `O` is in its state enum (`memTypes.h`) and no
coherence manager ever assigns it -- grep the `coherencemgr/` directory and
nothing sets it -- and there is no `F` at all. Those are the two states that
decide what host/NMFC sharing costs, and MESI cannot express invariant 14's
priority either, which is a strict order rather than a tie-break.

Measured on `test/tile_coh.c`, two tiles, one host:

| | count | what it means |
|---|---|---|
| `fwdFromO` | 15 | reads answered by a dirty holder. Under MESI each is a DRAM write followed by a read. |
| `fwdFromF` | 8 | clean reads answered by a nominated cache instead of the slice. |
| `upgrades` | 7 | writes that held the line and not the permission: no data moved. |
| `downgrades` | 23 | M->O and E->S. |
| `invalidations` | 14 | writes taking shared copies back. |
| `hostPaysSnoop` | 9 | host requests that had to snoop a function core -- invariant 14's permitted direction. |
| `nmfcPaysSnoop` | 24 | the direction to be avoided. |

The host's L1 is unified rather than split I/D, because Rev exposes a single
memory interface; splitting it would need a change to Rev and the boundary that
matters for coherence is the one below. It is write-through, so it never holds a
dirty line and a snoop reaching the L2 has only to take the L1's copy back
rather than fetch data through it.

#### One fabric, carrying everything

`NMFCFabricComponent` is out of the coherent path. Invocation, completion and
migration go over `NMFCCoherenceFabric`'s links -- the same links coherence and
line fills use -- because §24 step 3 requires it by name: "migration must be a
generic fabric packet, not its own channel... the claim that migration
*subsumes* the data movement it replaces only holds if migration and data
contend for the same interconnect." A migration is charged 72 bytes on the
departing tile's link and again on the arriving one, and those are the function
core's *data* ports, deliberately: an arriving context queues behind that tile's
line fills, which is what invariant 11 is an argument about.

Making that mean anything needed one correction to the fabric first. Its
bandwidth model charged each message its own serialisation delay in isolation,
so two messages in the same cycle each paid their own and neither waited for the
other -- a bandwidth *number* with no contention behind it. Each port now
carries an occupancy: a message arriving while the wire is busy waits, and
`linkQueueCycles` counts it. Without that, putting control traffic on the same
links would have changed nothing at all.

#### What building it found

**A program's static data arrives Modified.** The loader writes the whole image
-- `.text`, `.rodata` and `.bss` -- through the host's own cache, so every
static object is held dirty by the host before the program starts. A function
core's first read of any static data is therefore a downgrade from M, never a
clean share, and `F` cannot occur on it at all. The directory trace says it
plainly:

```
DIR GetX 0x1002c0 src=2 owner=-1 global=I      <- the loader, writing .bss
DIR GetS 0x1002c0 src=1 owner=2  global=M      <- the function core, later
```

This is worth knowing before any sharing statistic is read: it means the O state
carries essentially all host-to-function traffic in a program that has not been
running long, and it is why `tile_coh.c` has to reach past `__heap_start` to
find a line nobody has written.

**Invariant 14's priority is implemented and barely fires.** One preemption in
33,317 requests. With a single host that spins on the tracking unit while an
invocation runs, the two almost never have a request ready in the same cycle,
and three attempts at a contention phase produced none at all -- 4 KiB of host
array sat in the L1, 128 KiB sat in the L2, and only a working set larger than
the host's *last private level* generated fabric traffic concurrent with the
function core's. The mechanism is exercised rather than merely compiled, but it
is not yet a measured effect, and it should not be reported as one.

**The bandwidth model created no contention.** Described above: per-message
serialisation latency is not a shared link. It is worth naming separately
because it is the failure mode where a statistic exists, looks plausible, and
measures nothing -- `serialCycles` was accumulating correctly the whole time.

**A store used the virtual address.** `NMFCTile::issueStore` issued its
`Write` against `addr` while every other memory reference in the tile -- the
load, the fetch, the held-word writeback -- used `tr.frame`. It is invisible
while the mapping is the identity, which is every single-tile configuration, and
at two tiles with a grain region silo'd to a tile the stored value simply
disappeared: `rev-test-tiles.py` had been failing `tile_mem`'s two store checks,
reading 0 where it wanted 6112. One line. It is the §18 shape again -- a defect
sitting behind the only configuration that would have executed it.

**Two more bugs the heavier configuration found**, both of the kind that a small
test hides. A leaf cache was recording a core's memory interface as a holder and then
back-invalidating into it; the interface silently dropped the message and the
snoop never completed, which presents as a slow simulation rather than as an
error. And writebacks were posted, which means the interface does not track them
while the slice answers anyway -- an unmatched response, and fatal. The first is
why `NMFCMemLink` now refuses anything that is not a response instead of
ignoring it.

#### Why migration stays cheap, stated as a dependency rather than a hope

Invariant 5 budgets roughly one migration per thousand instructions and
invariant 11 argues each is at parity with the line fetch it replaces. Both are
claims about a fabric with headroom, and the headroom has a source: **offloading
the shared and memory-bound work to the cores is what makes host memory and
coherence traffic rare in the first place.** The loop closes -- a good
decomposition (§14, §20.2) quiets the host, a quiet host leaves the fabric to
migration, and migration is what the decomposition needed to be cheap. A bad
decomposition breaks it at both ends simultaneously, which is worth knowing when
a migration statistic looks wrong: the fault is more often in the shape of the
function than in the fabric.

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

* **translate** — consult `ctx_xlat`; on a miss, the tile's `NMFC_MMU` walks the local copy of the one page table (§5.0.2) and the context blocks. This happens **first**: a tile cannot know whether it owns an address until it has resolved one (invariant 3).
* **route** — on the **physical** address the translation returned. Not this tile → **migrate**. An earlier version of this line read "`tile_of(va)` on the instruction or data address... no translation involved", which is §5.1's rejected design and the reverse of the actual order (invariants 3 and 12).
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

---

## 14. Slicing: what one offloaded function should be

> **Corrected by measurement.** Everything below the next subsection was written
> assuming an offloaded function is a *slice of a bulk-parallel loop* that
> returns to the host. Every row of the table that follows makes that
> assumption, and it is the wrong one. It is kept because the forces it
> describes are real, but the decomposition it recommends is not.

### 14.0 The shape matters more than the placement

A slice of a loop has to *chase*: it walks a row, then reaches for a neighbour's
value, and that value is on whichever tile owns the neighbour. So the context
migrates, once per edge, and migration becomes the mechanism rather than the
exception -- measured at 0.38 migrations per instruction, which is to say three
quarters of all work was a context moving itself.

The alternative is to split along the boundary the data already has, and to let
a function *spawn* a function rather than return work to the host:

* `expand(v)` touches v's row bounds and v's edge list -- all of it v's own, so
  all of it local -- and spawns one `touch(u)` per neighbour.
* `touch(u)` reads and updates u's value: one access, on u's tile, dispatched
  there by the fabric.

Neither function ever needs an address it does not own, so neither migrates. The
work still crosses the machine; it crosses as a token rather than as a context.
On the synthetic control, against the same trace's host-only baseline:

| shape | placement | cycles | migrations | per instruction | vs baseline |
|---|---|---|---|---|---|
| chase | scattered | 3,835,161 | 582,687 | 0.384 | — |
| chase | **oracle** | 3,436,574 | 29,875 | 0.020 | — |
| **spawn** | scattered | **1,087,161** | **27,721** | **0.0152** | **12.6x** |

The spawn decomposition under a *scattered* placement beats the chase
decomposition under the *oracle* placement. And on a graph with no locality at
all -- the case a placement policy provably cannot help -- spawn gets 0.0130
migrations per instruction and 11.8x, against chase's 0.365 and 4.03x.

The real kernel says the same thing. GAP BFS on kron-24, same graph, same
source, same budget, both runs verified to have finished the work they were
given:

| shape | baseline | cycles | vs baseline | migrations | per instruction |
|---|---|---|---|---|---|
| chase | 11,460,114 | 2,142,594 | 5.35x | 924,095 | 0.7428 |
| **spawn** | 11,969,338 | **1,934,783** | **6.19x** | **2,890** | **0.0015** |

Three hundred and twenty times fewer migrations. The cycle gain is smaller than
the synthetic control's because at this occupancy the real kernel is not
migration-bound -- but the mechanism the placement chapters exist to manage has
essentially stopped happening.

Two conclusions follow, and they reorder the rest of this document:

1. **Locality is not the first-order concern it appeared to be.** Every
   placement result here -- the offline minimum cut, the adaptive policy, the
   R-NUCA classification -- was addressing a problem the decomposition created.
   With the right shape, migrations are rare whatever the graph looks like, and
   there is correspondingly little for a placement policy to do: the adaptive
   router reports zero remaps on the spawn traces because there is no co-access
   evidence left to act on.

2. **Migration is a fallback, not a mechanism.** It is what a function does when
   it must carry accumulated state somewhere. Reaching for a remote address is
   not that case, and a decomposition that makes it that case is wrong.

The forces below still apply *within* a function -- a spawn costs a fabric hop
like a call does, so `touch` must be worth dispatching -- but the question they
answer is "how big is one function", not "how do I cut up this loop".

### 14.1 The original framing (superseded)

The offload unit is a compiler decision, and it is not the same decision for
every algorithm. Two forces pull against each other:

* **Too fine** and the fabric dominates. A dispatch and a return cost a hop each
  (~16 cycles at the current settings) plus a context slot; an invocation that
  performs three memory accesses spends more time being delivered than working.
* **Too coarse** and one invocation monopolises a context. A power-law hub with
  100,000 edges becomes a single invocation that occupies one slot for its
  entire duration while the remaining contexts sit idle — the machine has
  hundreds of contexts precisely so that no one of them matters, and a hub
  defeats that.

So the target is: **enough memory operations to amortise the fabric round trip,
capped so that no invocation exceeds a fair share of context residency.** The
cap is what a skewed degree distribution forces; the floor is what a
low-degree graph forces. Neither is a constant, which is why `--slice` is a
knob and why the sweep has to cover both graph families.

### Per algorithm

| Kernel | Natural slice | Why, and what it costs |
|---|---|---|
| **BFS** (top-down) | one vertex's neighbour scan, **chunked by edge count** | Chunking is mandatory on power-law graphs and harmful on road graphs, where a degree-3 vertex does not fill a round trip. The neighbour claim is a real read-modify-write, so this is also the kernel that exercises the atomicity argument. |
| **PageRank** (pull) | one vertex's gather | Chunking needs a *reduction*: partial sums must be combined somewhere, which the fire-and-forget path cannot express and which puts work back on the host. One vertex per invocation is the honest unit. |
| **Connected components** (Afforest / Shiloach–Vishkin) | a chunk of **edges**, fire-and-forget | Hooking is `comp[u] = min(comp[u], comp[v])` — an atomic with no value the host consumes. `FLAG_NO_RETURN` frees the tracking slot at dispatch, so the host never blocks on it. The best fit for the architecture of any kernel here. |
| **SSSP** (delta-stepping) | a chunk of the current bucket | Returns are needed: a relaxation can insert into a later bucket, and the host owns the bucket structure. |
| **Betweenness centrality** | as BFS, in both phases | The backward accumulation depends on the completed forward sweep, so there is a barrier between them that the host must enforce. |
| **Triangle counting** | one edge, intersecting two adjacency lists | The largest natural invocation here, and the worst case for siloing: it touches *two* vertices' neighbour lists, so it migrates unless both endpoints are co-located. A partitioner that co-locates edges rather than vertices would change this kernel's answer entirely. |

### What the generator exposes

`tools/nmfc/gapbs_trace` makes both decisions explicit rather than implicit:

* `--slice vertex` — one invocation per vertex, whatever its degree.
* `--slice edge-chunk --chunk N` — cap an invocation at N neighbours, so a hub
  becomes many invocations instead of one long one.
* `--partition stripe` — contiguous arrays, spread across every channel by page
  interleaving. Good bandwidth, no locality.
* `--partition block` — vertex v's row bounds, edge list and value all go to
  tile `v·N/V`. Contiguous by construction, so it needs no graph reordering —
  but it only *helps* to the extent the graph has locality in vertex id, which
  is why road graphs and kronecker graphs should behave differently and why
  both belong in the sweep.

### What the first real GAPBS runs say

kron-20, 1500 vertex visits, 4 tiles, translation modeled:

| layout | slice | migrations | per invocation | context occupancy |
|---|---|---|---|---|
| stripe | vertex | 4,110,058 | ~3,015 | 15.3 / 256 |
| block  | vertex | 4,126,307 | ~3,028 | 15.6 / 256 |

**Block partitioning bought nothing.** That is not a defect in the placement
pass; it is the pass being asked to do something it cannot. Partitioning the
*layout* by vertex id only helps if the *graph* has locality in vertex id, and
a kronecker graph's edges are essentially random with respect to it. The
synthetic generator's `--locality` knob simulated a graph that had already been
partitioned; GAPBS's kron has not been.

So the honest conclusion is sharper than "siloing helps": **placement alone is
not a strategy.** Getting value from it requires one of

* **reordering the graph** so that partitions are contiguous in vertex id —
  GAPBS ships relabelling, and a real partitioner (METIS, or Afforest-style
  clustering) would be the serious version;
* **a graph with natural locality**, which is exactly what the road networks in
  the GAP suite are and why they belong in the sweep alongside kron and urand;
* **or a different unit of placement entirely** — co-locating edges rather than
  vertices, which is also what triangle counting would want.

The 6% context occupancy in both rows is the other half of the story: with
~3,000 hops per invocation the machine spends its time in the fabric rather
than at memory, so almost nothing is resident. Migration is not a cost here, it
is the workload.

## 15. What sets the run time: memory admission, not any of the obvious things

A defect that made the MMU walk every grain twice was fixed, and the machine
got 3.77% *slower*. Attributing that took five wrong answers, and the right
one is a structural mismatch: **each tile runs up to 1024 contexts against a
64-entry L1 data request queue.**

Sampling where a resident context's cycles actually go settles it. 82% of
them are spent in the ready queue having failed to issue, and splitting the
retry counter shows why:

| bucket | share of resident context-cycles |
|---|---|
| ready, but the data port refused it | 81.3 - 83.8% |
| waiting on memory (blocked) | 16.1 - 18.4% |
| execution latency | 0.0% |
| translation | 0.1 - 0.2% |
| atomic lock | 0.0% |
| migration | 0.0% |

Instruction-fetch retries are exactly zero; every one of the 3.95 billion
retries is `dcache_->rq_occupancy() + ops > dcache_->rq_size()`. The core is
not short of issue bandwidth (it retires 0.21-0.36 ops/cycle against a width
of 4) and does not head-of-line block -- `port_blocked_` feeds a statistic,
nothing more. It simply has nowhere to put the request.

Widening that queue 8x is the causal test:

| | cycles | data retries | port-refused share |
|---|---|---|---|
| rq = 64 | 2,792,466 | 3,952,205,189 | 83.8% |
| rq = 512 | 2,606,914 | 848,821,225 | 24.2% |

and the anomaly inverts with it:

| | pre-fix | fixed | gap |
|---|---|---|---|
| rq = 64 | 2,691,129 | 2,792,466 | +3.77% |
| rq = 512 | 2,659,636 | 2,606,914 | **-1.98%** |

So the MMU fix is worth about 2%, as a 72% cut in page walks should be. The
regression was the saturated port converting a latency reduction into
arrival pressure it could not absorb.

**This invalidates comparisons taken in the saturated regime.** Every number
measured before this was taken at rq=64, where the binding constraint was
request admission rather than anything the experiment varied. Results that
compare placement policies, mappings, or decomposition shapes need re-taking
with the port sized to the context count, or they are measuring the queue.

Five things were wrongly blamed on the way, each recorded because each was
measured and believed:

- **DRAM.** Row-buffer hit rate falls (9.09% -> 8.66%) and correlates with
  cycles, but activates rise 0.35% against 6.8% more cycles, so the DRAM did
  the same work. Ranking runs by cycles/activate is circular -- the numerator
  is the thing being explained.
- **Atomic spinning.** Real and fixed: blocked contexts were retried every
  cycle instead of parking. Worth +1.1%, in the wrong direction.
- **Atomic contention.** Real and fixed: a 64-byte lock for a 4-byte update
  made a line of `parent[]` contend, and each grant re-fetched. Waiting fell
  from 10,798,437 context-cycles to 1,465,999, and nothing moved -- it was
  0.21% of residency. Comparing it against tile-cycles rather than
  context-cycles overstates it by the 1024 contexts a tile holds.
- **Tile imbalance.** Occupancy spread triples (11% -> 33%), which looks
  damning, but every tile still needs the same time to within 0.5%:
  occupancy and residency co-vary exactly, so no tile is a critical path.
- **The core scheduler.** Round-robin does give a context one issue slot per
  N/4 cycles, but the width sits 90% idle and fetch retries are zero, so
  contexts are not queued behind each other -- they are queued behind memory.

The methodological lesson is the one that cost the most time: residency,
occupancy and throughput are one measurement in three units, tied by Little's
law. None can explain a change in the others, and every argument built on
their ratios was circular. Only the sampled breakdown underneath them, and
the causal test of changing the suspected resource, actually attributed
anything.


## 16. Sizing the tile: capacity first, concurrency second

Section 15 found that request admission gates the machine. Sweeping the
tile's data path shows admission is the *second* lever, and a smaller one.

The tile as configured holds 512KB of LLC slice, 32KB of data cache and 4KB
of instruction cache -- about 548KB against a kron-2^20 CSR of hundreds of
megabytes. Every measurement taken in that configuration is dominated by
capacity misses.

| fc_dcache | cycles | vs today | L1 hit | port refused | DRAM reads |
|---|---|---|---|---|---|
| 32KB lat2 mshr64 (today) | 2,792,466 | -- | 11.0% | 82.3% | 596,951 |
| 32KB lat2 mshr512 rq512 | 2,606,914 | -6.6% | 13.6% | 26.5% | 595,164 |
| 128KB lat4 | 2,700,520 | -3.3% | 46.6% | 81.1% | 597,697 |
| 512KB lat8 | 2,696,857 | -3.4% | 52.0% | 81.2% | 589,569 |
| 2MB lat14 | 2,085,151 | -25.3% | 63.7% | 79.7% | 451,375 |
| 2MB lat14 mshr512 rq512 | 2,024,762 | -27.5% | 63.6% | 25.8% | 450,006 |
| 8MB lat18 | 1,770,470 | -36.6% | 71.0% | 78.5% | 361,203 |

Three things fall out.

**Capacity and concurrency are substitutes, not complements.** Widening the
queues is worth 6.6% at 32KB and only 2.2% at 2MB: fewer misses means less
to admit. Chasing admission alone leaves most of the gain on the table.

**The capacity curve is sharply non-linear.** 128KB and 512KB quadruple the
hit rate and buy 3%, because those hits were coming from the LLC anyway --
DRAM traffic does not move. Only past 2MB does the tile start capturing
what was going to DRAM, and traffic falls 24%, then 39% at 8MB.

**The level does not matter; the total does.** 8MB placed at the L1 and 8MB
placed in the LLC slice land within 1.5% of each other (1,770,470 vs
1,744,780). Placing it at the L1 reduces the LLC slice to a 0.2% hit rate --
512KB of dead silicon. Capacity belongs in the LLC slice, which is also
where it is physically defensible: a memory-side cache per channel, the role
Infinity Cache and HBM-as-cache already play.

So the tile wants a large LLC slice, a small data cache, and enough
outstanding-miss concurrency to keep the channel fed -- in that order.
Concurrency should come from banking rather than from a monolithic
structure: 8 banks of today's 64 entries reaches the point where the memory
controller binds, without a 512-entry CAM that does not exist in silicon.

Dropping the data cache entirely is contraindicated by the same data: the
gradient at this level points toward more capacity, not less. A small cache
in front of a large slice is fine, because the slice does the work.

**Consequence for every prior measurement.** Results before this were taken
at 548KB of tile capacity and 64-entry admission -- capacity-starved and
admission-starved at once. Comparisons of placement policy, address mapping
or decomposition shape made in that regime were measuring the hierarchy, not
the policy, and need re-taking.

## 17. The DRAM model is generated, never transcribed

`config/nmfc/ramulator/tile_ddr5.yaml` is emitted by
`config/nmfc/ramulator/gen_tile_config.py`, which builds it through
ramulator2's own preset tables. It is not edited by hand.

**Why this rule exists.** The file was originally hand-written. Its timings
were right -- `DDR5_4800AN` value for value -- but its organization carried
`rank: 2` where every JEDEC preset carries `rank: 1`, and nothing recorded that
as a choice. A dual-rank DIMM is a real part, so this is not a wrong number; it
is an *unattributed* one, which is worse in a different way. Rank sets
`banks_per_channel` (64 here, not 32), `banks_per_channel` sets `grain`, and
`banks_per_channel` sets `grain` -- which is exactly as intended, since grain is
derived from the device rather than set beside it (section 5.2), and `annotate`
takes it as `--grain-bits`. Re-deriving grain from a changed org and
re-annotating is ordinary work, not breakage.

What the undocumented rank costs is attribution, not correctness. The 1 MiB
grain quoted in section 0 follows from `rank: 2`; the preset would give 512 KiB.
Anyone reading the config could not tell that a non-JEDEC organization was
chosen, or why. Generating the file fixes that: the preset name is the default,
and a departure has to be passed as an explicit flag, which is a record.

In this fork of ramulator2 the presets live in `python/ramulator/dram/ddr5.py`,
not in a header: `DRAMSpec::load_config` reads only the flat, fully-expanded
form -- `org.count`, the `timing` array, `command_cycles`, `read_latency`, and
the whole timing-constraint table -- and the Python is what expands a preset
name into it, including the secondary timings it derives (`nRRDS`, `nRRDL`,
`nFAW`, `nRFC`). Anything hand-written is a transcription of generated output,
and transcriptions drift. Earlier revisions of ramulator2 did compile the
presets into headers; this one does not, so `preset:` keys in a YAML are
silently ignored rather than rejected.

**The two values that are genuinely ours to choose**, and why each is set where
it is:

`read_buffer_size` / `write_buffer_size` -- ramulator's defaults are 32 and 32,
and the original file restated those defaults rather than choosing them. They
should be chosen, and 256/256 is what we run, which removes essentially all
controller refusals (11.9M to 0.24M on the hot tile).

**It is worth recording what this did *not* buy, because the obvious argument
for it is wrong.** A 32-entry read queue appears by Little's law to cap a
channel at `32 / read_latency` requests per cycle -- about 9.8 GB/s at the
~500-cycle latency a loaded tile sees, against a 19.2 GB/s subchannel -- which
reads as a clear diagnosis of a half-idle channel. It is not. Replaying this
machine's own captured address stream through the same device (section 19) gives
**16.59 GB/s with a 32-entry queue** and 17.42 GB/s with 256. The queue was never
the constraint, and the Little's-law argument fails because the latency plugged
into it is itself a *consequence* of queueing, not a device property.

Size the queues anyway -- 32 was ramulator's default rather than a chosen number,
and refusals are noise in every measurement taken through the controller
(11.9M to 0.24M on the hot tile) -- but record it as hygiene, not a bandwidth
fix. What actually limits bandwidth is in section 19.

`clock_ratio` (frontend and memory system) -- **inert on our path**, and
required by the schema. ramulator's own `Simulation::run()` interleaves ticks
using the frontend:memory ratio, but `ramulator_mc` never calls `run()`: it
ticks the frontend and the memory system once each per `operate()` and sets the
ChampSim module's clock period to the DRAM `tCK`. The values are still set to
the true core:DRAM ratio (5:3 for a 4.0 GHz core against 2.4038 GHz DDR5-4800)
so the file does not misdescribe the machine to a reader who assumes it is live.

**The channel is one 32-bit DDR5 subchannel, not a 64-bit channel.**
`channel_width: 32` is bits, so peak is 4800 MT/s x 4 B = 19.2 GB/s per tile,
not the 38.4 GB/s a full DIMM channel would give. Any statement about "percent
of channel peak" has to be against 19.2, and a measured 9.8 GB/s is half the
subchannel -- not a quarter of a channel.

## 18. Tile imbalance was a placement bug

The memory-committing BFS run split DRAM traffic 21 / 53 / 21 / 4 across four
tiles. The imbalance has two possible homes -- the trace does not distribute
virtual tile work evenly, or the runtime does not place it evenly -- and it was
the second, in two lines of code.

**The trace was never the problem.** Measured three ways over the whole
annotated stream:

| measure | t0 | t1 | t2 | t3 |
|---|---|---|---|---|
| BODY memory addresses | 24.4% | 25.8% | 24.7% | 25.2% |
| per region, incl. the 120 MiB streamed one | 23.9% | 27.3% | 24.7% | 24.2% |
| the 4 MiB region carrying 52% of all accesses | 24.7% | 25.0% | 25.2% | 25.0% |
| distinct 64 B blocks | 115,003 | 126,564 | 116,501 | 119,174 |
| reuse (accesses per block) | 29.4 | 28.2 | 29.4 | 29.3 |

All 36,297 page hints agreed exactly with the congruent placement
`vgrain % num_tiles`; none disagreed.

**The bug.** `NUCA_ROUTER::placement_for()` returned a round-robin counter with
both parameters commented out -- it never read the address -- and
`translate_nmfc` threw the hint away with `(void)hint`. So an NMFC grain's tile
was decided by *the order it happened to be touched first*.

Why that produces imbalance rather than balance: the placement pass interleaves
regions across grains precisely so any contiguous range covers every tile
evenly, and reading the tile out of the address is what carries that property
through translation. Round-robin over first-touch order discards it. Grains
carry very unequal traffic -- one 4-grain region holds 52% of all accesses --
so distributing grains evenly *by count* distributes traffic unevenly.

Instrumenting the function core to record, per memory op, the physical tile, the
virtual tile, and whether they agree:

| | t0 | t1 | t2 | t3 |
|---|---|---|---|---|
| virtual tile (what the address names) | 23.8% | 25.6% | 24.4% | 26.1% |
| physical tile (what routing uses) | 24.6% | **35.6%** | 24.1% | **15.7%** |
| observed migration arrivals | 25.1% | 32.6% | 24.2% | 18.1% |

**75.3% of all accesses routed to a tile their address never named**, and the
arrivals track the physical answer to within a point. Every access was correctly
NMFC-stamped (`standard-stamped: 0`), so this was purely frame misplacement, not
a mode error. Nothing caught it because the congruence assertion in `NMFC_VMEM`
is gated on `routing_order::VIRTUAL_FIRST` and `NUCA_ROUTER` is
`TRANSLATE_FIRST` -- **that check has never executed in any run.**

**The fix, and why it branched on routing order.** *Historical: `VIRTUAL_FIRST`
no longer exists (§5.1). Only the `TRANSLATE_FIRST` half below is the design,
and the branch itself is an artefact of the period when both were live.* The two
orders genuinely disagreed about who owns placement. Under `VIRTUAL_FIRST` a
core routed on the virtual address *before* translating, so the address *was*
the placement and a hint naming a different tile was invalid -- a unit test
asserted exactly this. Under `TRANSLATE_FIRST` the core routes on the frame, so
the hint is a real choice the placement pass gets to make. `translate_nmfc` now picks accordingly,
and `placement_for` returns `map_.tile_of_virtual(vaddr)`. Balance is not that
function's job; it belongs to `remap_grain()`, which moves a whole component
deliberately once migrations show which grains belong together.

Result, at identical instruction count (5,825,008):

| | before | after |
|---|---|---|
| incongruent routing | 75.3% | **0.29%** |
| per-tile MB/s | 4333 / 10498 / 4326 / 856 | 5884 / 5847 / 5762 / 6050 |
| traffic spread | 12.3x | **1.05x** |
| context occupancy | 7.9 / 128.2 / 38.5 / 68.1 | 18.8 / 44.9 / 16.6 / 34.5 |
| cycles | 36,002,362 | **26,131,589 (-27.4%)** |
| aggregate bandwidth | 20,013 MB/s (26%) | 23,543 MB/s (31%) |

The residual 0.29% is ~21k accesses on each of tiles 1-3 and none on tile 0 --
the signature of replicated grains, whose copies deliberately do not sit on the
tile the address names. Unconfirmed, but expected rather than leftover.

**Migrations did not improve** (8.68M to 8.92M). That is a separate defect, and
it is section 20.

## 19. What limits bandwidth: arrival rate, not the memory system

A memory tile reached 10.5 GB/s against a 19.2 GB/s subchannel and would not
move. Six things were changed and none of them mattered: read/write queue depth
(32 -> 256), MSHRs (64 -> 512), LLC banking, refresh (worth +4.7%), the address
mapping (`MOP4CLXOR` was *worse*), and the scheduler (`FRFCFS-RowHit` lifted row
hit rate 71.6% -> 83.3% and made throughput slightly *worse*). Every one of
those cost a ten-minute simulation to disprove.

**The method that actually settled it.** Inside a simulation, "this channel is
slow" and "this channel is starved" look identical -- a deep queue, high
latency, and throughput that ignores queue size. The way to separate them is to
capture the exact address stream the channel was asked for and drive the same
device with it as fast as a frontend can inject:

    # capture: set access_trace on RAMULATOR_MC, one LD/ST line per transaction
    PYTHONPATH=ext/ramulator2/python tools/nmfc/dram_replay.py <trace> --sweep

Replaying the hot tile's own stream through the same device, same
`RoBaRaCoCh` mapper, same `FRFCFS` scheduler:

| variant | GB/s | of 19.2 |
|---|---|---|
| as configured (RQ 256) | 17.42 | 91% |
| **RQ/WQ 32** | **16.59** | **86%** |
| no refresh | 19.07 | 99% |
| rank 1 | 16.77 | 87% |
| *the same stream, in simulation* | *10.50* | *55%* |

The addresses are fine and the device is fine -- even at the original 32-entry
queue it is worth 16.59 GB/s. Seconds per variant instead of ten minutes, and it
retires every memory-side hypothesis at once.

**Where the time goes.** Instrumenting the gap between consecutive column
commands on the hot tile:

    gaps at <=8 cycles (nBL, the bus limit)  96.5%
    gaps at 9-12 (nCCDL)                      2.2%
    gaps at 13-32                             0.4%
    gaps over 32                              1.0%   <- 642 cycles mean, 45% of elapsed time
    read queue empty                         39.3% of cycles

When the channel has work it runs flat out, back-to-back at the bus limit. It
simply has no work 39% of the time. Arrivals are **bursty**: time-averaged queue
occupancy is 103 while the queue is empty 39% of the time, so a burst is ~170
deep. That is why a deep queue and an idle channel coexist, and why enlarging
the queue moved latency (391 -> 1921 cycles) without moving throughput -- it
buffers bursts harder without changing the average arrival rate.

**Two readings that look like the answer and are not.** First, "the controller
issues nothing on 83% of cycles" is not a stall: `nCCDS` is 8, so a column
command can only issue once per 8 cycles and a *saturated* DDR channel shows
~87.5% of cycles with none. Second, "only 6 of 44 banks holding requests have a
schedulable one" is the same fact seen from the other side -- after any column
command the bus blocks all of them for 8 cycles, so a sample lands inside that
window 7 times in 8. Both are the bus limit being enforced, not a defect.

## 20. The two levers left: contexts, and the shape of a function

Section 19 leaves the machine arrival-limited: the channels are idle 39% of the
time and worth 17.42 GB/s each when fed. Two levers reach that, and they are
independent.

### 20.1 Concurrency: the barrier, not the capacity

Concurrency is what turns an idle channel into a busy one, but the machine is
not short of it. Every capacity knob measured has headroom, and two of them are
easy to misread:

`ftu_size` is **not** a concurrency limit. `FTU IN FLIGHT peak: 1024 of 1024`
looks like a saturated tracking unit, but `DISPATCH STALLS: 0` says it never
once gated a fork, and peak live bodies was 665, not 1024. The FTU holds an
entry from fork until *join*, and `nmfc_host_core.cc` is explicit about why:
"a forked invocation keeps its slot until its join has been seen: the result
exists, but nothing has asked for it yet." Most of those slots are finished work
waiting to be collected. Occupancy of a structure that holds results is not a
measure of running work, and raising it does nothing.

`num_contexts` is a real limit, but only inside a wave. After the placement fix:

| tile | contexts mean | peak | queue empty | read latency | MB/s |
|---|---|---|---|---|---|
| t0 | 18.8 | 205 / 256 | 63.4% | 431 | 5884 |
| t1 | 44.9 | **256 / 256** | 64.6% | 1199 | 5847 |
| t2 | 16.6 | 165 / 256 | 63.7% | 371 | 5762 |
| t3 | 34.5 | **256 / 256** | 63.6% | 864 | 6050 |

Peak-to-mean is 5-11x, two tiles clip at the cap, and **every channel is idle
~64% of the time regardless of whether the tile carries 17 contexts or 45**.
That last fact is the whole story: idle time is set by the batch cadence, not by
how many contexts a tile can hold.

The workload is bulk-synchronous. The host forks a batch, the tiles fill, the
batch drains, and the host busy-spins on the commit block (§4.3) before forking
again. Nothing arrives at the channel across that gap. Little's law confirms
contexts are not the constraint within a wave: t2 at 16.6 contexts and 371-cycle
latency yields 0.045 refs/cycle, t1 at 44.9 contexts and 1199-cycle latency
yields 0.037 -- both about 5.8 GB/s. Adding contexts to t1 bought latency, not
throughput, because inside a wave the channel is already busy.

So the order is: **overlap the batches first**, which is where the idle time
lives and is a fork-pattern change in the kernel; then raise `num_contexts` to
stop the in-wave clipping. Read the result off the queue-empty fraction (§19),
never off cycles alone.

**Done, and it worked.** `TDStepOffloaded` now treats the slot pool as a ring:
instead of forking LEVEL invocations and waiting for all of them, it retires the
single oldest invocation to free one slot and reuses it immediately, so LEVEL
invocations stay outstanding continuously. Both shapes build from one source --
`EXTRA_CXXFLAGS=-DNMFC_BATCH_BARRIER` selects the old one -- so the comparison is
reproducible. On a scale-20 graph, same trace pipeline, same config:

| | barrier | ring |
|---|---|---|
| queue empty | 51.7% | **38.4%** |
| aggregate bandwidth | 31,149 MB/s (41%) | **39,063 MB/s (51%)** |
| cycles | 60,119,717 | **49,276,407 (-18.0%)** |
| host instructions | 12,218,028 | 12,092,046 |
| context occupancy | 20.6/39.1/20.2/29.6 | 19.5/24.6/19.4/20.9 |
| migrations | 31,217,817 | 31,245,056 |

18% fewer cycles on 1% *fewer* instructions, the occupancy sawtooth flattened,
and migrations unchanged -- which is the control: batching never caused them, so
a batching fix must not move them.

**Two traps found building this, both worth remembering.** An offloadable
function that nothing calls is not inert: `NMFC_FUNCTION` carries `used`, so an
`nmfc_expand_r` left outside its `#ifdef` was emitted anyway, and the annotation
pass then found two functions and paired two joins to every invocation. And
annotate reads *any* post-call use of the return register as retrieving a
result -- so writing `pool + (next % LEVEL) * SLOT` after the call, where signed
modulo touches that register, invented a second join per invocation for a
function that returns void. The kernel now walks the ring with rotating pointers.
The second is a latent trap rather than a kernel mistake: annotate already knows
which invocations commit to memory, and should suppress register-join detection
for them.

**Ring depth is not a further lever, and the obvious argument for it is wrong.**
At LEVEL=1024 the tracking unit holds ~902 invocations while only ~84 are
resident on cores, which reads as tiles starved of startable work. Supplying
more does not help -- it hurts:

| | LEVEL 1024 | LEVEL 4096 |
|---|---|---|
| cycles | **49,276,407** | 53,857,690 (+9.3%) |
| aggregate | **39,063 MB/s (51%)** | 35,976 MB/s (47%) |
| queue empty | **38.4%** | 43.1% |
| occupancy | 19.5/24.6/19.4/20.9 | 18.8/32.4/21.1/30.8 |
| FTU in flight | 902 of 1024 | 3418 of 4096 |

The deeper ring really did keep 3.4x more outstanding, so the mechanism worked;
idle time still rose and the occupancy imbalance came back. The likely reason is
that the ring retires strictly FIFO: the host blocks on the *oldest* invocation,
so a deeper ring does not only add supply, it lengthens the queue of finished
work stuck behind one straggler. Head-of-line blocking grows with depth. If that
is right, the fix is an out-of-order harvest -- retire whichever slot has
committed rather than the oldest, which BFS permits because the frontier is a
set -- but that is a change to the §4.3 invocation loop's contract, not a
parameter, and it is untested.

`NMFC_LEVEL` is a build parameter so this can be re-swept, and 1024 is the
measured best of the two points taken.

Note also that **scale matters for any tile measurement**. At scale 17 the
`parent` array is 0.5 MiB and `index` 1.00 MiB -- at or below the 1 MiB grain --
so the hot randomly-accessed structure lands entirely on one tile and no amount
of batching or placement can balance it. A working set must exceed
`grain x tiles` before a tile-behaviour result means anything.

### 20.2 The shape of a function, and the fabric cost of getting it wrong

A function is supposed to be **planted on one tile for most of its runtime and
to do non-trivial work there**. The current BFS decomposition does neither:

    accesses per invocation                2,227.7
    distinct tiles touched, mean           3.53 of 4
      invocations touching all four        4,130 of 6,224
      invocations touching exactly one     52
    tile switches per invocation           1,559.3
    accesses between switches              1.43

An invocation changes tiles every 1.43 memory accesses. Predicted switches
(9.7M) and observed migrations (8.9M) agree, and the placement fix did not move
them -- this is the decomposition, not the placement.

**Why this costs time in transit and almost nothing else.** A migration carries
72 bytes (512-bit register file + 8-byte PC), but it *replaces* the line the
context would otherwise have fetched rather than adding to it (§21), so the
fabric does not move materially more traffic. Nor is there startup cost worth
the name: arriving on a new tile is measured at **2.2-2.3 cycles**, and the
function core's instruction cache hits **100.00%** on every tile. That is not an
accident of this workload -- §21.2 explains why it is structural.

What remains is time in transit: a context being carried is a context not
issuing references. The residency split is where that shows up, not the byte
count, and the §5 budget of roughly one migration per thousand instructions is
exceeded by 150x.

**Built.** `nmfc_bu` is that function: it takes `parent + lo` and `index + lo`,
so every vertex it reads and writes is inside its own slice, it claims what it
discovers rather than handing a list to the host, and it returns one 64-bit
frontier word in the register file instead of committing to memory. Ownership
removes the atomics -- no `set_bit_atomic`, no compare-and-swap -- because a
vertex belongs to exactly one invocation. It drives the reference's alpha/beta
loop, so the kernel is now the *same algorithm* as `bfs_base` and a
base-versus-NMFC comparison finally means something; it reaches identical vertex
counts at scales 16, 17 and 18 and executes 0.75x the reference's instructions.
Fitting it into the 512-bit register file is section 22.

The cause is legible in the kernel. `nmfc_expand` walks a contiguous neighbour
chunk -- one grain, one tile -- and then reads and CASes `parent[v]` for each
neighbour, where `v` is a graph-scattered vertex id uniform over the whole array
and therefore over every tile. The switch lands on the ordinary load of
`parent[v]`; 0% of switches occur at an ATOMIC record, because the CAS follows
the load to the same address. The fix is a decomposition whose unit of work owns
its data -- a function that owns a *vertex range* and consumes incoming edges,
rather than one that owns an edge range and chases scattered vertices.

### 20.3 If migration is unavoidable: a reserved fast path

Some shapes genuinely cannot be siloed, and for those the answer is to make
migration cheap rather than rare. Reserve a context slot on every function core
so an invocation always has somewhere to land, and move it over a path priced
well below a general fabric message. The full 72 bytes is the *cold* cost --
what a context needs on arrival at a tile it has never run on. A context
returning to a tile that still holds its reserved slot needs far less, and the
saving is the point of the mechanism.

This is a hardware-axis change (section 4.1.1) and must be measured as one:
against a fixed decomposition, so that a cheaper migration is not credited with
work a better-shaped function would not have migrated for.

Note what §21 does to the case for it. Migration is not paying a bandwidth
penalty -- it subsumes the data movement it replaces -- so a fast path is not
recovering wasted bytes. It would be recovering *restart* cost and time in
transit, which is a smaller and much more specific claim, and one worth
establishing before building anything.

## 21. Migration subsumes data movement, and what that leaves for NUCA

An earlier draft of this section argued that migration is bandwidth-inefficient:
72 bytes of register file and PC to move a context, against 8 bytes to fetch the
word it wanted. That comparison was wrong, and the error is worth keeping
because it inverts the conclusion.

**No real fabric moves less than a cache line.** A foreign access does not cost
8 bytes; it costs a line, 64 bytes, plus whatever header the fabric puts on it.
Against that, a migration is 72 bytes -- the same order, and the PC is the only
genuinely additional payload, which is small beside the framing a data transfer
carries anyway.

**And the traffic is subsuming, not additive.** This is the part the earlier
draft got backwards. A context either migrates to the data or fetches the data
to itself; it never does both. So migration traffic is not *added* to data
traffic, it *replaces* it. A machine that migrates does not move more bytes
across the fabric than one that fetches -- within the margin of a PC and a
header. Migration is therefore not a tax paid for locality. It is "transfer the
work, not the data", at parity.

**Which makes atomicity genuinely free.** The property that a read-modify-write
is serialized by the one core owning the address -- no travelling lock, no
protocol, an unambiguous ordering point, and no coherence question because no
second copy exists -- costs no bandwidth at all. It falls out of a data movement
the machine was going to perform either way.

### 21.1 What this leaves for NUCA and NUMA

If work follows data at parity, then NUCA and NUMA are not there to *avoid*
migration, and most of the apparatus usually associated with them is beside the
point. Exactly one thing still matters:

> **Keep migration classified as sub-optimal.** If a line is used heavily, it
> should live in one place rather than being chased. The work then follows it.

That is what NUCA and NUMA have always been for, so the remaining problem is not
a new mechanism but an old one under a harder condition: **the sources of work
are themselves moving.** Classical NUCA assumes the requesters sit still and the
data is placed relative to them. Here both move, and the placement has to hold
anyway.

The likely shape of the answer is neighbour-touch recognition: blocks touched by
the same function are colocated, colocation is performed, and the work follows
the data wherever the placement algorithm puts it. Work is never divided up
explicitly -- there is no partitioning step, no owner assignment, no decomposition
the compiler has to get right. The function goes where its data went.

**We already have the mechanism, and it is inert.** `NUCA_ROUTER::note_migration`
does exactly this recognition: a migration says the address the context left and
the address it came for belong together, so the two grains are united, and
`remap_grain()` moves a whole component rather than one grain at a time -- the
comment there records that placing grains individually cannot escape a random
start. What it cannot currently do is *place* anything: every component it forms
exceeds a tile's fair share and is refused as "the whole working set", and the
measured run shows `COMPONENTS seen: 87 placed: 0 too-large: 87`. On a graph
where everything touches everything the co-access relation is close to fully
connected, and unioning on every migration collapses it into one blob.

So the open question is not whether to migrate. It is how to form components
that are smaller than the working set on a graph whose co-access relation is
dense -- which is a clustering problem with a real literature, and is where this
work should go next.

### 21.2 Starting a context on another tile is nearly free

The remaining objection to migration is that a context must be "spun up" on the
destination -- that it arrives cold and has to rebuild state. It does not, and
the reason is structural rather than lucky.

**There is no data locality to abandon.** An LLC slice holds only the addresses
its own tile owns. A context migrating from A to B is going because it needs an
address B owns -- an address A's slice was never going to hold and never did.
Nothing warm is being left behind; the departing tile had nothing relevant.

**The code is already there.** Function bodies live in the replicated page type,
so every channel holds a copy (§0 invariant 3). Arriving does not fetch code
across the fabric; it reads the local copy. The only question is whether this
tile's instruction cache happens to hold those lines already, and with few
distinct functions and many invocations it does.

**Measured, on the four-tile run:**

| | t0 | t1 | t2 | t3 |
|---|---|---|---|---|
| migration cold start, mean cycles | 2.2 | 2.3 | 2.2 | 2.3 |
| fc instruction cache hit rate | 100.00% | 100.00% | 100.00% | 100.00% |
| icache misses (of ~10^5-10^6 accesses) | 2 | 8 | 2 | 2 |

Two cycles and a warm instruction cache. A context is a register file and a
program counter; it has no stack (§0 invariant 7) and no cached working set to
reconstruct, which is precisely what makes it portable.

**So the case for migrating is stronger than it first looks.** It costs the same
bytes as fetching the line would have, it starts in single-digit cycles, and it
buys free atomicity and no coherence problem. What it costs is the flight time,
and that is a latency to be amortised over the work done after arriving -- which
is exactly why §20.2's requirement is that a function do *non-trivial work* per
tile, and not that it avoid migrating.

## 21.3 The result: 5.67x against the reference algorithm

The comparison the architecture has to answer is not "is the machine busy" or
"what fraction of DRAM peak" -- it is whether the same traversal finishes sooner
with the cores than without. Scale-20 Kronecker graph, 645,268 vertices reached,
GAPBS's reference direction-optimising BFS on one standard core against the same
algorithm offloaded to four memory tiles:

| same 645,268-vertex traversal | instructions | cycles |
|---|---|---|
| reference DOBFS, no NMFC cores | 62,869,010 | 197,753,293 |
| same algorithm offloaded, no NMFC cores | 55,684,830 | 213,820,177 |
| **same algorithm offloaded, WITH NMFC cores** | 19,681,418 | **34,905,677** |

    architecture gain (identical work, cores on/off) : 6.13x
    NET vs the reference algorithm                   : 5.67x faster
    work vs reference (instructions)                 : 0.89x

Three properties make this a real comparison rather than a flattering one. Both
sides traverse the same graph from the same source and reach the same vertex
count, checked at three scales. Both run the *same algorithm* -- alpha/beta
direction switching with the same thresholds -- so the machine is not being
credited for an algorithmic difference. And the offloaded kernel executes
*fewer* instructions than the reference (0.89x), because ownership removes the
reference's `set_bit_atomic` and its compare-and-swap on `parent`; the machine
is not being handed extra work to look good on, nor spared any.

**What this supersedes.** Section 0 reported bandwidth utilisation -- 77.9% of
peak against a host's 20.2% -- which was the right measurement for the premise
but is not a speedup, and every intermediate number in sections 18 through 20
compared NMFC configurations against other NMFC configurations. Those isolated
real defects, but none of them established that the machine beats not having it.
This does.

**And it is the same architecture that lost.** The top-down kernel measured
*slower* than the reference on this workload:

| kernel | architecture gain | algorithm penalty | net |
|---|---|---|---|
| top-down only | 6.73x | 7.38x | **0.91x (a loss)** |
| direction-optimising | 6.13x | 1.08x | **5.67x** |

The architecture delivered ~6x in both cases. The first kernel handed it 5.3x
more work than the reference needed and gave the whole gain back. That is the
sharpest statement of section 4.1.1's two axes this work has produced: the
hardware axis was never the problem, and measuring it against a weaker algorithm
would have condemned a machine that was doing its job.

## 22. The admission test, and what it says about the pull shape

Sections 20.2 and 21.1 both point at one reshape: own a *vertex* range and pull,
rather than own an *edge* range and chase scattered `parent[v]`. It was built.
`nmfc_bu` claims only vertices it owns, needs no atomics because ownership makes
them unnecessary, returns its 64-bit frontier word in the register file rather
than committing to memory, and drives the reference's alpha/beta loop. It
reaches identical vertex counts to GAPBS at scales 16, 17 and 18 and executes
**0.75x** the reference's traced instructions -- below 1.0 precisely because
ownership removes the reference's `set_bit_atomic` and its compare-and-swap.

Getting an honest answer about whether it fits took fixing the tool twice.

**The admission test was measuring registers touched.** `annotate` emplaced one
slot per canonical register and never released it, so a function was rejected
for *naming* more than eight registers over its lifetime. Section 4.1 says the
opposite in as many words -- count peak simultaneous liveness, not registers
touched -- and even predicts the failure, noting that counting names reported 17
and 21 for these kernels. It now does linear-scan allocation over the buffered
body: a value takes a slot at its first definition and gives it back after its
last read, two values never live together share one, and only *concurrent*
overflow fails. A register never read takes no slot, as section 4.1 requires.

**And it charged every value a whole 64-bit slot.** The file is 512 bits divided
however the machine likes, so a 32-bit `NodeID` costs 32. Pin records
partial-width views as distinct ids and the regmap keeps their names, so the
width survives the trace and is only lost when canonicalising. `annotate` now
carries it and reports liveness in bits.

**With both corrected, the first attempt still did not fit** -- 10 values and
608 bits against 512 -- and the two obvious source-level reductions did nothing,
because they do not change what the compiler keeps. What worked came from
reading the generated code:

  - **Do not make the compiler hold a constant.** `bits |= 1ULL << k` pins a
    register to the literal 1 for the variable shift. Accumulating with
    `bits = (bits << 1) | found` and running `k` *downwards* puts the bits in
    the same places and needs no such register.
  - **A byte-per-vertex frontier, not a bitmap.** A bitmap test costs a shift, a
    mask and a scratch register for the extracted word; a byte test is one
    scaled load. Eight times the memory for a structure that is transient
    per level, in exchange for register-file headroom available nowhere else.

With those, `nmfc_bu` takes **three** arguments -- `parent + lo`, `index + lo`
and the frontier, so it never forms an absolute vertex index -- and measures
**8 values, 480 bits of 512**. It is admissible, and it emits no callee-saved
pushes at all. `nmfc_expand`, the edge-range shape, sits at 8 values and 384
bits.

**The register file did not need widening.** That matters, because widening is
not free in the way a paper constraint would be: the file is the state each core
holds *per context*, so at the 256 contexts the headline runs used it is 16 KiB
per tile and doubling the file makes it 32 KiB -- and at the 1024 the cap
configuration allows, 64 KiB becomes 128 KiB, which is no longer a register file
next to an LLC slice but a structure competing with it. A transfer also takes
two cycles rather than one, and migration, latency and complexity all move with
it. The shape had to fit 512 bits, and it does.

The lesson is narrower than "the shape does not fit". It is that **a function's
admissibility is a property of its generated code, not of its source**, and the
three things that decided it here -- a pinned constant, a bitmap's scratch
register, and an absolute index that could be folded into a pointer -- are all
invisible above the disassembly. The admission test has to be run, and read.

Two options remain open and neither is now required:

  1. **Split into chained successors.** Invariant 9 permits extension, so a
     function too large for the file can become two. Not needed here.
  2. **Widen the file.** Ruled out on cost: per-context state doubles, a
     transfer becomes two cycles, and migration and latency follow.

The tooling fixes stand regardless of which is chosen: the admission test now
measures what section 4.1 says it should, and `--abisaves` lets `annotate` drop
x86-64's callee-saved register preservation the way it already drops the
return's stack pop, while a genuine spill -- a `mov` to a stack slot, whose PC is
in no such list -- still fails. That distinction is the admission signal and it
is preserved.

## 23. The instruction set

Offload is an instruction (§0 invariant 1). Under ChampSim that could only ever
be a trace-record encoding; on a RISC-V target it is real, and this is the set.

### 23.1 The governing rule: nothing blocks

**There are no blocking instructions.** Every action is a *try* that reports
whether it succeeded, paired with a *probe* that asks whether it would. Software
spins if it wants to wait; the hardware never does it on software's behalf.

This is not a style preference, it is what this machine has already cost us. The
migration path held a tile slot while waiting for fabric space, and the machine
deadlocked at cycle 9,100,426 with four tiles at 0-1 free contexts and 983
tokens waiting (§20.3). An instruction that blocks is a resource held while
waiting for a resource, which is the same shape. Making it impossible to express
is cheaper than auditing for it.

A consequence worth stating plainly: **there is no blocking `JOIN`.** An earlier
draft had `JOIN` (blocking) and `PJOIN` (permissive) as separate instructions.
They are the same instruction -- the blocking one was only a spin the hardware
performed for you.

### 23.2 Host side

    FORK.R   rH, rPC, cCTX     try; rH = handle, or 0 if no FTU entry is free
    FORK.M   rH, rPC, rADDR    try; the TILE fetches the 512-bit context from rADDR
    FORKF.R  rH, rPC, cCTX     fire-and-forget
    FORKF.M  rH, rPC, rADDR
    FORKQ    rN                probe: how many FTU entries are free
    JOIN     rOK, cDST, rH     try: deposit 512 bits, rOK = 1 on success
    JOINQ    rOK, rH           probe: has it returned, without moving 64 bytes

**`FORK` writes a handle, and this is measurement-driven.** Without one, `JOIN`
can only mean "the oldest", and FIFO retirement was measured *worse*: at ring
depth 4096 the queue-empty fraction rose 38.4% -> 43.1% and cycles rose 9.3%,
because the host blocked on one straggler while thousands of finished
invocations queued behind it (§20.1). An addressable tracking unit is what makes
out-of-order join expressible.

**All four fork forms return a handle**, fire-and-forget included. There is no
use for it today -- faults are process-fatal (§23.4) so there is nothing to
attribute -- but it keeps every context addressable, keeps the four encodings
uniform, and adding it later would break every fork.

**`FORK.M` is dereferenced by the tile, not the host.** That is the entire point
of the form: only an address crosses the fabric, and the load happens where the
context probably already lives. If the host were going to load it anyway, use
the register form.

**`JOINQ` earns its encoding** by answering "has it returned" without moving 512
bits to do so. `FORKQ` returns a *count* rather than a flag so software can size
a batch instead of probing per fork -- batch shape was worth 18% of runtime
(§20.1).

**`FORKQ` reports true machine state, not an architectural quantity, and that
distinction is the reason it exists.** For return+join forks the occupancy *is*
architectural -- `FORK` allocates, `JOIN` frees, both retired by the host, so it
is exactly `forks - joins`. But a value software could compute itself does not
earn an instruction. Fire-and-forget is what makes the question real: `FORKF`
allocates architecturally and is freed by an ACK when the remote context ends,
which is asynchronous and unknowable from the instruction stream. So total FTU
occupancy is *not* derivable, and reporting it is the only version of this probe
worth encoding.

The cost is that `FORKQ` is a snapshot and can go stale between the probe and
the fork. That is acceptable and does not need fixing: under §23.1's try/probe
rule `FORK` returning 0 is already the authoritative answer, and software must
handle a failed fork whatever the probe said. `FORKQ` is a sizing hint, never a
contract.

**The tracking unit is a sized array, and it refuses rather than evicts.** An
entry is a returned register file plus a handful of bits -- 512 bits of payload,
two of state, one of retirement mode, and a hart id -- so about 65 bytes, giving
4 KiB at 64 entries and 16 KiB at 256. (The simulator's entry is 80 bytes, but
half its metadata is instrumentation: a fork cycle for telemetry and a
generation counter for catching stale handles, neither of which hardware needs.)
That is the same order as an L1D and comfortably under one, which is a
reasonable thing for a specialised core to carry. Worth being explicit that it
cannot be made smaller by holding fewer payloads than entries: every
outstanding invocation may complete before any `JOIN`, so the unit has to be
able to absorb all of them at once. If it could not, the tile
would have to hold a finished context until the host made room -- a resource
held while waiting for a resource, which is the shape that deadlocked the
machine at cycle 9,100,426 (§20.3). **The 64 bytes per entry are what buy the
tile its context slot back the instant it returns.**

It is the same order of magnitude as a cache and nothing like one in behaviour.
A cache makes room by evicting; this array cannot, because an entry holds the
only copy of a returned register file and a join-expected entry must never close
without returning its values (§23.3). So it fills and then *refuses* -- `FORK`
returns 0 -- and refusal is architecturally visible because it cannot be handled
invisibly. That is why `FORKQ` exists.

Spilling completed register files to memory, and caching them here, is the one
design that would relax that. It trades area for a memory write on every return
that overflows and a miss path on `JOIN`. Not needed at any size measured so
far; revisit only if the array's area is what binds.

The alternative -- splitting the tracking unit into a join-tracked pool and a
fire-and-forget pool, so the first is exactly `forks - joins` -- buys
replayability: the same instruction stream gives the same answer every run,
which is worth something for debugging and for trace-driven comparison. It was
considered and rejected, because it partitions a resource for the benefit of a
hint. Revisit it only if non-determinism in `FORKQ` actually obstructs a
measurement.

### 23.3 Function side

    RETC     cCTX              end; return the register file to the FTU
    ENDC                       end; ACK only, no register file returned
    CONT     rPC               extend: carry my own context forward
    CONT.M   rPC, rADDR        extend: the tile fetches a fresh context

`CONT.M` replaces the register file *wholesale*. The 512 bits fetched are the
context; there is nothing to merge them with, since the outgoing context is the
thing being replaced.

`RETC` and `ENDC` are one opcode and a return bit. They differ only in whether
the message carries 64 bytes, and the encoding should make that visible.

**The fork type and the end type are chosen independently, so they can
disagree, and the tracking unit has to define what happens.** The host picks
`FORK` or `FORKF`; the function picks `RETC` or `ENDC`. Every combination
returns *something*, and the two closing rules are deliberately not symmetric:

  - **A fire-and-forget entry closes on its ACK, and its return can never be
    read.** `FORKF` does not expect a return, so a `RETC` from such an
    invocation carries 64 bytes nothing will ever collect; they are dropped and
    the entry closes on the ACK as usual. `JOIN` and `JOINQ` on a
    fire-and-forget handle therefore cannot succeed and never will -- they
    report failure, which is the try/probe answer (§23.1), not a fault.
  - **A join-expected entry never closes without returning its values.** It
    closes only at `JOIN`, and `JOIN` always hands back a register file. An
    invocation that ends with `ENDC` still produces an ACK *and* a zeroed
    register file. It must not become uncollectable: an entry no instruction can
    ever free is a resource held forever, which is the shape §23.1 exists to
    forbid.

Both mismatches are counted, so they are visible in the statistics rather than
silent. Neither faults -- there is no per-invocation fault status to fault with
(§23.4).

**`CONT` is invariant 10's successor, and it cannot fail.** It inherits the
existing FTU entry rather than allocating one, so it consumes no new resource
and can never be refused -- which is exactly why extension is safe where fan-out
is not. The handle the host holds stays valid across an arbitrary chain of
successors; whatever the last link returns is what the `JOIN` receives.

That also makes it the mechanism for §22's option 2: a function too large for
the 512-bit register file can be split into a chain, each link admissible on its
own, with no link able to be denied.

### 23.4 Faults

A recoverable fault -- a page fault -- is the MMU's business and never becomes
architectural. A fatal fault -- divide by zero, an unmapped access -- kills the
process and every context it owns, exactly as a segfault does.

There is deliberately **no per-invocation fault status**, no error code in the
returned register file, and no fault probe. Those only make sense with
user-defined fault handlers, which this machine does not have.

### 23.5 Deliberately absent

**`KILL`.** The orchestrating core cannot terminate a context. There may be a
use for it, and the encoding space is reserved, but it cannot be added naively:
a context killed mid-update leaves memory in a state nobody can reason about, so
a kill must be *cooperative* -- the context declares "I will accept a kill
request here", which is a new instruction, a protocol, and a liveness obligation
on every function written thereafter. Not needed yet, and expensive to get
wrong.

**Mailboxes** (`SEND`/`RECEIVE`/`PRECEIVE`). Functions already communicate
through memory, and ownership makes that coherent without protocol. A mailbox
needs a context-to-location directory updated on every migration -- 8.9M
migrations on one measured BFS run -- and a bounded buffer that can fill, which
is the hold-and-wait shape §23.1 exists to forbid. Encoding space reserved,
unbuilt until a workload demands it.

**`context_id` and `tile_id` CSRs.** Proposed and dropped: nothing uses them. A
function addresses nothing by its own context id, and it must never make a
placement decision from its tile id (§0 invariant 8 -- the workload computes no
tiles), so exposing it invites exactly the violation the invariant forbids.
`FORKQ` covers the one real need, FTU occupancy.

### 23.6 Context registers

`cCTX` is a **context register**: eight of them, `ctx0`-`ctx7`, 512 bits each,
per software thread. This is invariant 1's "512-bit vector register that *is*
the callee's register file", made real. RISC-V has no such register, and
nothing in reach has built one -- Rev implements no vector extension (its
`RegVEC` class and `RVVTypeOpv` format are declared and used nowhere), nor does
Vanadis, nor does anything in sst-elements. RVV with `VLEN=512` would be the
standards-compliant answer and means writing a vector unit from scratch first.
A small dedicated file is far less work and is what the machine actually needs.

**A context register is 512 bits, not eight registers.** How those bits divide
is the callee's business, decided per function: eight 64-bit values, sixteen
32-bit, sixty-four 8-bit, or any mixture. Bit-packing is the point -- narrowing
an operand is what buys a larger register file, and §22's admission test is a
test on *bits* for exactly that reason.

Two instructions reach them:

    CXW      cD, lane, rS      cD[lane] <- rS     one 64-bit lane
    CXR      rD, cS, lane      rD <- cS[lane]

The lane is an access granularity, not the register's structure, and these two
are complete. Once 64 bits move in and out, any packing within them is reached
with the shifts and masks RV64I already has, and a field straddling a lane
boundary is two moves and the same arithmetic. A bit-field insert and extract
carrying an offset and a width was considered and dropped: it would duplicate
instructions that exist.

**What is not solved is the compiler's half.** A compiler does not know that
narrowing an operand yields a larger register file, so the packing -- and the
admission decision that depends on it -- is work for §24 step 5. Nothing here
decides it; the architecture only has to not prevent it, and 512 opaque bits
plus a 64-bit aperture does not.

**Context registers are ordinary architectural registers from `JOIN` onward,**
with one wrinkle worth naming before it is rediscovered. `JOIN` is a *try*: it
writes 512 bits on success and leaves the destination alone on failure. A
machine that renames has to express that, and the clean formulation is to make
`JOIN` a read-modify-write --

    cDST_new = ok ? ftu_payload : cDST_old

-- so the destination is a source as well. The dependency on the old value is
real only in the failure path, where software is going to look at `cDST` again
anyway, so the cost is one extra source read rather than a squash or a copy.
The alternative, an unconditional `JOIN` issued only after a successful
`JOINQ`, is race-free in a single instruction stream but reintroduces a
sequence that must not be interrupted, which is not worth the saving.

Nothing in the Rev model exercises any of this: Rev is in-order and does not
rename, and every context-register write in the implementation happens
synchronously at issue, which is precisely why no context-register scoreboard
exists. The first thing that would break that is a memory form of `CXW`, which
is one more reason it is not there.

**Why a register file rather than a fixed aperture.** The alternative was to
declare the context to be eight fixed general registers (RISC-V's `a0`-`a7`),
which costs no new state and, on a fork whose arguments are already in place,
no instructions. It was rejected: a fixed aperture can hold one staged context,
and an addressable tracking unit that exists to allow out-of-order join
(§23.2) then has one landing pad to join into. The instruction-count argument
also reverses on the real workload -- the bottom-up BFS function has three
arguments, two loop-invariant, so with context registers the invariant lanes
are written once outside the loop and the fork costs one `CXW`, where an
aperture must re-establish whatever the loop body clobbered.

### 23.7 Encoding

Twelve instructions fit one RISC-V `custom-*` opcode with `funct3`/`funct7`.
Reserve one now: re-encoding later, once traces and tools exist, is the sort of
churn that invalidates results rather than merely costing time.

Reserved and taken: `custom-0` (`0b0001011`), `funct3` 0-5, with 6 and 7 left
for §23.5's `KILL` and mailboxes. Context-register indices ride the five-bit
register fields, read against a different file.

## 24. Implementation plan on Rev/SST

The order below is the agreed plan. None of these steps are trivial, and the
temptation to describe an early one as "the first milestone" should be resisted:
the thing worth measuring -- §21.3's 5.67x -- requires all of them.

**Step 0, deferred deliberately.** Reproducing the *baseline* (stock GAPBS BFS,
no NMFC, on Rev against ramulator2 with the DDR5-4800AN device) against
ChampSim's 197,753,293 cycles needs none of the work below and would confirm the
two simulators are comparable. It is not blocking, and doing it now would
derail. It must happen before any Rev number is compared to a ChampSim one.

1. **Host instructions and the tracking unit.** `FORK.{R,M}`, `FORKF.{R,M}`,
   `FORKQ`, `JOIN`, `JOINQ` (§23.2) on the Rev core, and the FTU behind them.

2. **The NMFC core and its instructions.** The function core -- register file,
   no stack, many contexts -- plus `RETC`/`ENDC` and `CONT`/`CONT.M` (§23.3).
   Its shape is §25. An earlier draft of this step said `RevCoProc` was the
   natural attach point; it is not, and §25.1 says why -- a co-processor is a
   subcomponent of a host core and shares its memory, which would make locality
   free. It is a component of its own, reached over the fabric.

3. **The system architecture.** Memory tiles; LLC slices with the DRAM bank
   alignment derived from device geometry (§17, and the grain arithmetic in
   §5.2); a local TLB and local I-cache per tile; and migration.

   **Migration must be a generic fabric packet, not its own channel.** A
   dedicated channel would be easier and would quietly falsify §21: the claim
   that migration *subsumes* the data movement it replaces, rather than adding
   to it, only holds if migration and data contend for the same interconnect.

4. **Sample programs in assembly** -- written *alongside* steps 1 and 2, not
   after step 3. A vertical slice (`FORK.R` -> one tile -> `RETC` -> `JOIN`)
   exercises the thin path through all three and gives a skeleton to thicken.
   Completing the FTU in isolation, with nothing to fork to, tests nothing.

5. **A compilation pipeline.** This is where the move off ChampSim pays off, and
   it may be cheap. Under ChampSim, admissibility was archaeology: compile for
   x86-64, disassemble, count live values, discover the compiler had pinned a
   register to hold the constant `1`, rewrite the source to trick it (§22). On
   RISC-V we own the ABI, so `-ffixed-x{n}` can constrain the compiler to
   exactly the register budget and a function that does not fit **fails to build
   or spills visibly**. That turns the §4.1 admission test from a post-hoc
   analysis into a build error. Try this before committing to a custom backend.

6. **Compile GAPBS BFS to RISC-V plus the extension, and run it.** Only here is
   §21.3's number reproducible, and only against a step-0 baseline taken on the
   same stack.

## 25. The shape of the function core

Section 24 plans the work; this section says what the thing being built *is*.
Everything here is a statement about the machine on Rev. Where the ChampSim
model is mentioned it is named as such, because a property of that simulator is
not evidence about this one -- and in one place below the two deliberately
disagree.

### 25.0 What the ChampSim model already settled

Not much of the core was open. `src/nmfc/function_core.cc` states its own
shape -- "multi-context, in-order per context, non-speculative. No ROB, no
rename, no branch predictor, no load/store queue" -- and models fetch through
an instruction-cache channel with a fetch latency and a taken-branch bubble,
issue width as *contexts issuing per cycle*, per-context translation,
migration, op-class latencies, and a block-granular lock table with ownership
hand-off. Those carry over.

What is new on Rev is narrower than it first appears: real instructions in
place of `body_instr` records, pipeline **depth** as an explicit quantity, the
per-context instruction buffer, the register file's actual division, and one
change of policy in §25.4 that is not a port at all.

### 25.1 A component, never a coprocessor

The function core is its own SST component, one per memory tile, and a host
*invokes* it over the fabric. It is not attached to a host core and does not
share one's memory.

**"Reached over the fabric" describes the invocation, not the core's position.**
The core is on the memory side, at the top of its tile's vertical stack, beside
the fabric interface into its own LLC slice (§5.9, invariant 13). What crosses
the fabric to get to it is an invocation, a migration or a completion -- never
one of its own loads.

This is not tidiness. A `RevCoProc` is a subcomponent of a host core and shares
that core's `RevMem`, so modelling the function core that way would hand it the
host's memory for free and make locality free with it -- which is the quantity
the whole design exists to measure. The topology has to be wrong before the
numbers can be.

### 25.2 Barrel execution

A context issues **one instruction, then yields**. Several contexts are in the
pipe at once, and never the same context twice.

That single rule is what removes the machinery: with at most one instruction
per context in flight, no two instructions in the pipe can be dependent, so
there is no forwarding, no interlocking and no hazard detection to build. It is
the Denelcor HEP and Tera MTA arrangement, and among shipping near-memory parts
it is UPMEM's DPU.

**Width `N`** is the number of duplicate pipes, each serving a different
context in the same cycle.

**Depth `D`** is the re-issue delay. A context cannot present its next
instruction until its previous one has left the pipe, because the value may be
needed. Supporting context-conditional forwarding inside each pipe would relax
that; it is not planned, and the cost of not having it is stated next.

**The context count follows from `N` and `D`, and is not a free knob:**

    C >= N x (D + L / I)

for memory latency `L` and `I` instructions issued between misses. The `N x D`
term is the floor needed to keep the pipes fed with no memory system at all;
everything above it is latency tolerance. At `N`=4, `D`=8, `L`~100, `I`~4 this
is ~132, and at `L`~150, `I`~3 it is ~232 -- which is why 256 is the right
order and why the cap configuration's 1024 is over-provisioned by about 4x at
21 KiB per tile of extra state for it (§25.7).

### 25.3 Fetch: a per-context buffer and a shared target buffer

**No speculative execution.** Nothing runs on a guess, there is no wrong-path
work to squash and no state to roll back.

Each context owns a **single-entry instruction buffer**, filled with its next
instruction at the end of each dispatch. The reason it is per context rather
than a shared prefetch queue is the only reason that matters: a dedicated slot
cannot be evicted before its owner is rescheduled, and a shared one can. Cost
is one instruction word, a program counter and a valid bit -- about 13 bytes
per context.

**Instructions live in memory and a cache backs them.** A fetch is a memory
access like any other, issued to an instruction cache, and what it costs is
measured rather than assumed. The instruction and data caches are **separate**,
because the two working sets are nothing alike -- code is small and reused, the
data a near-memory kernel streams is neither -- and a shared cache would let the
data evict the code. Both are banked, and the level below them is sliced by
address with routing in front, because what these structures have to supply is
bandwidth: a barrel core with `C` contexts asks for far more concurrent
accesses than one bank can answer.

The per-context buffer is what makes a sequential fetch free *when it hits*.
The address of the next instruction is known when the previous one dispatches, a
whole re-issue window (`D` cycles) before it is needed, so the access completes
underneath the window and the context never waits. A control transfer is
different only in *when* its target becomes known: not at dispatch but when the
branch resolves, which is a window later, so without help that fetch starts late
and the context pays for the refill. A miss costs whatever the miss costs, and
that is now a thing the model reports.

**A small shared branch target buffer removes that, and it is not speculation.**
What it supplies is a fetch address a window earlier than the branch would
otherwise give one. Nothing executes on the answer: a wrong prediction means
the buffer holds the wrong instruction, and the cost of that is exactly the
refill a machine with no predictor pays *every* time. Being wrong is free;
being right is a saved bubble. That asymmetry is a property of the barrel
arrangement -- it is only available because no instruction is ever issued
speculatively.

**Shared, not per context**, and that is the whole economy of it. Every context
runs the same replicated code (invariant 3) and a tile runs a handful of
distinct functions, so one entry serves every context executing that branch,
the structure is warm almost immediately, and it does not scale with `C`. A
per-context predictor would multiply by the context count and be cold on every
invocation, which is the wrong shape twice over. Sixty-four entries of tag,
target and a last-outcome bit is about a kilobyte against the 22 KiB of context
state at 256 contexts.

The measured behaviour on a four-iteration loop is the classic one: the first
execution of the branch misses, the middle iterations are predicted, and the
exit mispredicts -- two refills instead of three, with the saving growing with
trip count.

**On the instruction hit rate.** Invariant 11 records 100% from the ChampSim
runs. That is a *measurement made under that model*, not a property to design
around, and treating it as one is how the instruction side nearly went
unmodelled here. On Rev it is measured: 904 hits in 906 fetches on the
array-summing kernel, the two misses compulsory. High, as expected, and
established rather than assumed -- and it is only defensible at all because the
data cannot evict the code.

### 25.4 One outstanding miss, and sleeping on it

**A context has at most one outstanding load. It sleeps on issuing one and
wakes when the value arrives.**

This is where Rev deliberately differs from the ChampSim model, which marked
the destination register not-ready, kept the program counter moving, and
stopped only when an instruction needed a value that had not come back. That
in-order scoreboard is where intra-function memory-level parallelism came from
in the measured runs.

Giving it up is a choice, made because several outstanding loads per context
require disambiguating which return is which and keeping the register file
coherent across them, and neither is needed for correctness. One outstanding
miss needs neither: there is nothing to disambiguate.

**It has a price and the price is quantified above.** All memory-level
parallelism now comes from context count, so the `L / I` term in §25.2 is paid
in full per miss rather than amortised across several. **This means §21.3's
5.67x is not a number Rev should be expected to reproduce at the same context
count**, and any comparison between the two must say which core model produced
it. The buffer can be sized to 2, 4 or 8 later if a measurement asks for it;
the expectation is that it will not.

The returned value lands in a **one-slot data buffer** per context. Its purpose
is not correctness -- the context is always asleep when its load returns, so
the fill could write the register file directly -- but decoupling the fill from
write-port contention with executing contexts. If the register file is given a
dedicated fill port, this buffer can go away. About 10 bytes per context: the
value, a destination field and a valid bit.

**Behind both buffers is a small line store, and its size is not a free
parameter.** A context may have at most one load in flight, so `C` contexts
need at most `C` lines of staging -- the one-outstanding-load rule is what
bounds it. At 64-byte lines that is 2 KiB at 32 contexts and 16 KiB at 256,
alongside the 22 KiB of context state. It is not a data cache in the sense §25
denies the core: it is where the line a load pulled in sits until the context
wakes and takes its value out of it, which is also why sequential access hits
it seven times in eight.

Two things that store is *not*, worth saying because "it is just the buffers"
would otherwise cover them. It is shared, so contexts can evict each other's
lines where strictly private slots could not. And the per-context data buffer
above it stays: it holds a value bound for a named register, which is a
different thing from a line held by address.

### 25.5 Stores, and what the memory path must guarantee

**Stores do not sleep the context.** A store returns no value, so there is
nothing to wait for and nothing to disambiguate -- the reasoning behind the
one-outstanding-load rule does not reach them. An invocation still cannot
retire until its stores have landed.

That leaves one hazard: a context storing to an address and later loading it
before the store is visible. The requirement is narrower than memory ordering
in general -- **a context must observe its own stores in program order** --
because sharing between contexts is the lock table's business (§25.6), not
this one.

The design meets it structurally rather than with a store buffer: there is one
slice, requests are already serialised by the issue pipeline, and **the tile's
memory path must process same-address requests in arrival order**. That is
stated here as a requirement on the slice, not as an assumption about one, and
it is the thing to test when the slice is built. If it ever cannot be met, the
fallback is a store buffer with an address match, and that is a real cost to
avoid.

### 25.6 Atomics are supported, not removed

The function core implements atomics, enforced by a per-tile table of locked
blocks with ownership hand-off -- the holder passes both the lock and the value
it already has to the next waiter, so the waiter never refetches.

**Removing atomics would not make them free, it would make atomicity an
unchecked assumption on every operation**, which is unsound for anything not
actually owned. What makes them nearly free here is ownership: a context
migrates to its data, so every atomic is local to one tile and the table needs
no cross-tile protocol.

**It must not be built on the memory system's atomics.** Load-reserved and
store-conditional, or a locking read through the cache, would work and would be
less code -- and would measure a coherence protocol's mechanism rather than
this one, which is the entire quantity in question. Exclusivity among the
contexts *on this tile* is exclusivity full stop, because ownership means no
other tile can be contending for the line. Store-conditional then needs no
reservation of its own: holding the line *is* the reservation, and `SC`
succeeds exactly when the context still holds it.

**Measured.** Sixteen invocations doing an atomic add on one counter cost **two
memory reads and two writebacks**; the other fourteen took the line by hand-off
and touched memory not at all. Every increment was counted once and every
invocation saw a distinct prior value.

**The hand-off chain is bounded, and that bound is a coherency guarantee.** A
word passed context to context is a word the rest of the machine cannot see, so
after a fixed number of hand-offs it goes back to the data cache whether or not
anyone is still waiting. In the measurement above that is exactly what the two
writebacks are: a chain of eight, a writeback, a chain of six, a writeback.
Without the bound, a steady arrival of contexts could hold a word out of the
hierarchy indefinitely.

**The held unit is a word, not a line.** Two atomics on different words of the
same line are genuinely independent and serialising them would be an invention
of the implementation rather than of the design.

**An ordinary load or store to a held word goes through it.** While a word is
held, the cache's copy is stale, so an access that went to the cache would read
around the atomic -- and the window is not small, because the word can be handed
from context to context without going back at all. The entry therefore also
outlives its own writeback: it is removed when the write lands, and if anything
touched it in the meantime the write is repeated, so there is no moment when the
value is neither in the tile nor in the cache. Measured: twelve invocations each
doing an atomic add and then loading the same word cost **one memory read**, and
all twelve loads were served from the held copy.

**Waiting for a second word while holding a first is refused, not handled.**
It is a resource held while waiting for a resource -- §23.1's shape, and the one
that deadlocked the machine at cycle 9,100,426. It can only arise from a
function that takes a second word without releasing the first, so the machine
diagnoses it rather than adding a protocol to survive it.

### 25.7 The register file, and what a register is

**A context is 512 bits.** It is not eight registers, or sixteen, or any other
count. How the 512 bits divide is decided per function at compile time -- eight
64-bit values, sixteen 32-bit, sixty-four 8-bit, or any mixture -- which is why
§4.1's admission test is a test on *bits*, and why narrowing an operand is what
buys a larger file.

The core therefore resolves a register operand through the function's layout
rather than through a fixed register count. That layout is **per function, not
per context**: many contexts run the same function, so it is one small table
entry beside the instruction cache, indexed by the function a context is
running. It adds nothing to the 512 bits and does not scale with `C`.

Deciding the layout is the compiler's problem and is not solved -- §24 step 5.
The architecture's obligation is only to not prevent it.

**Per-context state**, then: 64 bytes of register file, ~13 of instruction
buffer, ~10 of data buffer -- about 87 bytes. That is 22 KiB per tile at 256
contexts and 87 KiB at 1024, the latter being a structure that competes with
the LLC slice rather than sitting beside it.

The program counter is separate from the 512 bits, which invariant 11's
72-byte migration confirms arithmetically: 64 bytes of register file plus an
8-byte program counter.

## 26. What the Rev model cannot currently show

Steps 1 through 4 of §24 are built and the suite passes, so the temptation is to
start measuring. It is the wrong move, and this section says why: **the model's
memory hierarchy is not the one in §5.9, and four of the machine's costs are
fictional in ways that do not cancel.** A number taken now is a sum of errors of
unknown relative size, which is worse than no number, because it looks like a
result.

### 26.0 The hierarchy is wrong above the slice

> **Largely fixed.** `coherent_memory.py` and `rev-test-coherent.py` build the
> §5.9 hierarchy: host L1 and L2, a MOESIF directory on the L2-to-LLC fabric,
> and one private slice/controller/channel stack per tile below it. All three
> defects below are addressed there. The old flat configuration
> (`tile_memory.py`, `rev-test-tilemem.py`, `rev-test-tiles.py`) is kept for
> comparison and still has them -- and `rev-test-tiles.py` fails `tile_mem`'s
> store checks at two tiles, which the new hierarchy passes at one, two and
> four. What remains unbuilt is distance and bandwidth on the *NMFC control*
> path: invocation, completion and migration still ride `NMFCFabricComponent`,
> so the first fiction in §26.0.1 stands for them. The rest of this subsection
> is what was wrong, kept because it is what the fix was measured against.

Before the fictions, the structure. §5.9 says the machine is a standard memory
system with the partition moved to the fabric. The Rev model was not that
machine in three respects, all of them above the LLC slice.

**The host has no L2.** `rev-test-tilemem.py` builds `hostL1` and connects it
straight to `tilebus`. So the model has no L2-to-LLC boundary -- which is
precisely where coherence is enforced (invariant 13) -- and the host's private
hierarchy is one level deep instead of two.

**Coherence does not use the fabric.** memHierarchy runs MESI between the caches
and the slices over `tilebus`; `NMFCFabricComponent` carries none of it. Since
coherence is one of the three things the fabric carries and, with migration and
LLC/DRAM access, the bulk of what crosses it, the fabric is currently modelling a
small minority of its own traffic.

**One `tilebus` is shared by every tile.** Each tile's icache and dcache hang
above it and every slice below it, so tile 0's core reaching tile 0's *own* slice
traverses the same component as tile 1 reaching its own. That contention is an
artefact of the configuration, not of the machine: the path from a function core
to its slice is inside the tile.

Together these mean the model's fabric is a side-channel that a handful of NMFC
packets use, rather than the L2-to-LLC interconnect that everything crosses.
That is the single largest gap, and the three fictions below are mostly its
consequences.

### 26.0.1 The four fictions, and their signs

**Migration never meets the traffic it is supposed to displace.**
`NMFCFabricComponent` carries invocations, completions and migrations with a
flat `hopLatency` of 8 cycles and a `maxDeliver` of 4 events per cycle. It counts
bytes -- `bytesInv`, `bytesMig` -- but never converts bytes to time, so a
72-byte migration and an 8-byte credit cost the same. Host memory traffic and
coherence, meanwhile, are on `tilebus`. **Migration is therefore too cheap**, and
§24 step 3 named this exact failure in advance: "Migration must be a generic
fabric packet, not its own channel... the claim that migration *subsumes* the
data movement it replaces, rather than adding to it, only holds if migration and
data contend for the same interconnect." They do not contend.

**Every tile is equidistant from every other.** The fabric charges one flat
`hopLatency` regardless of which tile a packet leaves and which it arrives at,
so a migration to a neighbour and a migration across the machine cost the same.
**Migration therefore buys nothing**, which biases the other way -- and it
leaves invariant 6 with nothing to stand on, because a NUCA policy that cannot
shorten a distance is only a load balancer. Note what this is *not*: it is not
that a core reaching its own channel costs what reaching somebody else's costs.
A core never reaches somebody else's channel (§26.4). The distance that is
missing is between *tiles*, and it is the only distance a function core can ever
experience.

**Invariant 14 was not modelled at all.** ~~There is no MOESI priority~~ --
built; see §5.9's as-built subsection. The directory is MOESIF, the asymmetry is
counted in both directions (`hostPaysSnoop`, `nmfcPaysSnoop`), and NMFC
arrivals are admitted ahead of host arrivals. The caveat that survives is that
the **priority itself is barely exercised**: one preemption in 33,317 requests,
because a single host spinning on the tracking unit almost never has a fabric
request ready in the same cycle as a function core. It is not a number to
report as an effect yet. The original objection -- that every number involving
a shared line was a number about memHierarchy's protocol rather than this one,
which is what §25.6 already says about atomics -- no longer applies above the
slice.

**The page walk issues no references.** `NMFCTile::translate` charges
`walkLatency_` (30 cycles) and touches no memory; the code says so at the point
it happens -- "the references it makes into this tile's own slice are not
modelled yet." So invariant 3's subject is free. The table being duplicated onto
every tile is what makes a walk local, but *local* costs nothing if the walk
never issues, and the pressure a walk puts on the tile's own cache and channel
is what decides whether §7.1 -- translations dropped on migration -- is a
footnote or the dominant term. **Migration's after-cost is therefore too
cheap**, the same direction as the first.

Three of the four flatter the machine and one punishes it. That is the argument
for fixing all of them before measuring anything, rather than the convenient
ones.

### 26.1 `.rodata` is striped, and that is a bug

> **Fixed.** `nmfc.ld` now places `.rodata` with `.text` inside a declared
> `__dup_start`/`__dup_end` span, and `tile_memory.dup_region()` reads those
> symbols rather than inferring the region from the executable `PT_LOAD` --
> whether the linker merges read-only data into the read-execute segment is its
> decision, not ours. Both `rev-test-tiles.py` and `rev-test-coherent.py`
> declare the region from it. The rest of this subsection is the argument, kept
> because the reasoning is what makes the fix obviously right rather than
> merely applied.

Not a fidelity gap -- a defect that would fire as soon as a real program ran.

`nmfc.ld` puts `.rodata` in the grain after the silo'd data, alongside `.data`
and `.bss`, where it is **regular: striped across tiles**. But `.rodata` is
read-only and every tile that runs the code wants it. That is the definition of
a duplicate page, and it is the same argument that already made `.text` one.

The consequence is worse than a slow access, because there is no such thing as a
slow foreign access on this machine: `issueLoad` translates, and if the answer
is not this tile it calls `migrate`. So a context reading a constant from a
tile that does not own it **migrates**. And `.rodata` in these programs is far
smaller than one grain -- 512 KiB at N=2 -- so all of it lands on a single tile,
and every context on every other tile migrates on its first constant.

It is not hypothetical. In `tile_mem.exe`, `.text` is at `0x10000`,
`.nmfc_grain` at `0x80000`, and `.rodata` at `0x100000` -- **512 bytes of
constants sitting alone in a 512 KiB grain**, which one tile owns and every
other tile has to leave home to read.

Nothing has caught it because the test programs barely read constants. That is
§18 again, exactly: a defect sitting behind a path the suite never takes, of the
kind that shows up later as a statistic nobody can explain.

The fix needs no new grain. A grain carries one *type*, not one section, and
`.rodata` and `.text` are the same type -- so `.rodata` moves up beside the text
and one duplicate region covers both. It does have to *leave* the grain it is
in: `.rodata` at `0x100000` and `.bss` at `0x100200` currently share one, which
is legal only while both are regular, and stops being legal the moment one of
them is duplicated and the other stays writable.

One detail is worth fixing at the same time. `text_region()` infers the
duplicate region from the executable `PT_LOAD` segment, which works only for as
long as the linker chooses to merge read-only data into the read-execute
segment -- a decision that is `ld`'s, not ours, and that differs between
contiguous and non-contiguous layouts. The linker script should **declare** the
region instead, with `__dup_start`/`__dup_end` around the two sections, and the
Python should read the symbols. The existing docstring already states the stake:
"a region that does not cover the text would leave some of it striped."

*Alternative considered and rejected:* give `.rodata` a vtile and co-locate it
with the code. This misreads what a vtile does. A vtile gathers a coherent set
onto **one** tile; code is wanted on **all** of them. Duplication is not a
stronger form of co-location, it is a different type, and the read-only
restriction is what makes it sound.

### 26.2 Build the standard hierarchy, then move the partition to the fabric

An earlier draft of this section proposed retiring both `tilebus` and
`NMFCFabricComponent` and putting memory traffic and NMFC traffic on one merlin
network. It was wrong twice over, and both errors are worth keeping on the
record because they came from reconstructing §5.9 instead of reading it.

The first error: putting a tile's own icache and dcache on a network **moves the
core off the tile**. It sits at the top of a vertical stack, beside the fabric
interface into its slice; a model in which its own loads traverse a NIC and a
router has changed the machine, and it takes invariant 3 with it -- a walk that
goes out over a network is not a local walk, whatever the page table is
replicated onto.

The second, larger error: treating "put the host's memory traffic on the fabric"
as a change NMFC introduces. It is not a change at all. **That is where host
memory traffic already goes in any machine** -- L1, L2, the L2-to-LLC fabric,
the LLC. The Rev model omitted it, so restoring it is not architecture work, it
is building the baseline the architecture is a delta against.

So the work is not a new interconnect. It is: build the ordinary hierarchy, then
apply the one change §5.9 describes.

**1. Give the host its L2, and put coherence at the L2-to-LLC boundary.** This
is the piece whose absence makes everything above the slice wrong. It is also
the piece that makes invariant 14 expressible: there is no "host reference to a
block an NMFC core is modifying" to prioritise until the host's private
hierarchy ends where it should.

**2. Privatise the tile stack.** Each tile gets its own small bus -- the tile's
internal crossbar, joining its two L1-equivalents (`fc I$`, `fc D$`) and the
walker to its own slice, shared with nothing -- and below it the slice,
controller and channel it already has. `tile_memory.py`'s single shared
`tilebus` becomes N private ones. Local surgery; changes no result by itself.

**3. Make the fabric the L2-to-LLC interconnect.** Not a second network: the one
that exists, now carrying what it should -- coherence, migration, and LLC/DRAM
access. Host L2 misses enter it and are routed to the slice owning the address,
which is the `INTERLEAVE_FABRIC` role from §2 that the Rev build never
reproduced. This is the step where migration and data finally contend, which is
what §24 step 3 requires and the only condition under which §21 is testable.

**4. Give the fabric its two missing costs.** *Distance*: a hop count between
tiles, so a migration to a neighbour and one across the machine differ, which is
what gives invariant 6's NUCA policy a gradient. *Bandwidth*: bytes converted to
time, since invariant 11 is arithmetic on 72 bytes against 64 and arithmetic on
quantities the model does not charge for is not a measurement.

**5. Then invariant 14.** MOESI priority and ownership-from-observation, at the
slice, once there is a coherence boundary and a fabric for it to happen on. This
is last because it is the only one of the five that cannot be done earlier: it
needs 1 and 3 to exist first.

*Alternatives considered:*

- **Merlin.** Rejected on the structural ground above, and on a second: its
  clients must be network endpoints, and the temptation it creates -- attach the
  memory path, it is right there -- is exactly the error this subsection records.
  The fabric here is an L2-to-LLC interconnect with a partition applied at it,
  which is a routing rule and a cost model, not a flit-level network.
- **Charge extra cycles in the tile when `tr.tile != tile_`.** Still rejected,
  and for a plainer reason than before: there is nothing to charge. A foreign
  translation does not produce a slow access, it produces a migration, and
  migration has a cost model already. The gap is that it is flat.
- **Leave the host on `tilebus` and let the fabric carry NMFC traffic only.**
  The comfortable option and the one §24 step 3 forbids by name. It also leaves
  the model without the L2-to-LLC boundary, so invariant 14 stays unbuildable.
- **Per-link latencies on the shared bus.** Inexpressible, and moot once the
  shared bus is gone: latency is a property of (cache, bus), never of
  (cache, slice).

### 26.3 The walk issues references

The walk becomes real reads -- three levels, Sv39 -- issued into the tile's own
data path and terminating, at worst, at the tile's own channel. Local is not a
property the walk has to be granted: the table is on duplicate pages so every
tile holds a copy, and after §26.2 the whole stack the references travel down is
on the tile, so there is no route by which a walk could leave even if something
tried to send it. The walker has no cache of its own, so those references compete
with data for the dcache, and that competition is part of what is being measured
rather than an artefact to be engineered away.

This is the term that decides whether invariant 5's budget -- roughly one
migration per thousand instructions -- is comfortable or fanciful. §7.1 drops a
context's translations when it migrates, so each arrival pays three local
references per distinct page it then touches, on top of the transit that
invariant 11 already accounts for. Whether that is a footnote is currently an
assumption, and it is charged as 30 flat cycles with no traffic behind it.

### 26.4 What must not change, and is not a gap

Three things below look like omissions and are not. Naming them here so they do
not get helpfully repaired later.

**The core is on the tile, and its memory path is not an interconnect problem.**
Invariant 13. The tile's stack looks under-modelled next to a real network and
it is not: it is short because there is nothing between a core and its channel
but the tile's own crossbar, slice and controller. Any proposal that makes the
core's own accesses more elaborate should be read as a proposal to move the core
off the tile, and rejected on that basis before its details are considered.

**There is no remote data path, and there must not be.** A foreign access
migrates; it never fetches. This is not an unimplemented branch -- it is what
makes tile-local atomicity sound (§25.6): two tiles cannot hold the same line,
by construction, and a remote read would destroy that construction in exchange
for nothing the architecture wants.

**Invariant 11's parity is a cross-machine claim.** 72 bytes of register file
against the 64-byte line "a foreign access would have cost" is a comparison
against the *baseline* machine -- a conventional core pulling the line -- not
against an alternative path inside this one. Testing it therefore needs §24
step 0, the stock-GAPBS baseline on the same stack, and not a second mechanism
in the tile.

### 26.5 Order

**All five steps of §26.2 are done**, and so is §26.1. Host L1 and L2; private
tile stacks; the fabric as the L2-to-LLC interconnect with the partition applied
at it; distance in hops and bandwidth as a shared link occupancy; the MOESIF
directory with invariant 14's admission order; and invocation, completion and
migration on those same links, so `NMFCFabricComponent` is out of the coherent
path entirely. `.rodata` is a duplicate page.

What remains: **the walk** (§26.3), which still charges 30 flat cycles and
issues nothing, and **§24 step 0**, the baseline -- which is what any of these
numbers finally get compared against, and which needs a core the Rev model
cannot be. See §26.6.

Steps 1 through 3 are not architecture work and should not be described as
though they were. They restore a standard memory hierarchy that the Rev model
never had, and the architecture is a one-line delta against it (§5.9). Only step
4 onwards is NMFC.

None of this is §24 step 5. The compilation pipeline is still the next *feature*;
this section is the cost of the model being honest enough for step 6 to mean
anything.

### 26.6 The out-of-order host, built

Invariant 8's comparison is against "a standard core", and the ChampSim figure
was taken against an out-of-order core with a 352-entry reorder buffer. **Rev is
in-order**: it has harts, a `machine` string and a `memCost` range, and no
reorder buffer, no rename, no load/store queue. Sizing Rev's memory system to a
modern big core bounds its memory-level parallelism honestly and does not turn
it into one.

**`vanadis` is an out-of-order RISC-V core, it is in sst-elements, and NMFC now
runs on it.** `test/vanadis-nmfc.py` builds the machine; `src/NMFCRoCC.{h,cc}`
is the accelerator. The core is configured to the structures that matter:

| | value | |
|---|---|---|
| `reorder_slots` | 352 | the size invariant 8's baseline used |
| `physical_integer_registers` | 288 | rename |
| issue / decode / retire per cycle | 6 | |
| load / store queue | 192 / 114 | Golden Cove's counts |
| clock | 3.0 GHz | |

and it has separate instruction and data memory interfaces, so the host finally
gets the **split L1I/L1D** Rev could not provide.

#### One instruction set, two hosts

The encoding was reformed rather than duplicated. It is now RoCC-conformant:
**funct7 carries the group** (top three bits) **and the variant** (low four),
and **funct3 carries the RoCC operand flags** -- which of rd, rs1 and rs2 the
instruction uses. Vanadis reads funct3 exactly that way, so the old encoding,
which used funct3 as the group selector, would have told it that `FORK`
(group 0) touches no registers.

A context register is now named by a **number in a general register** rather
than by a five-bit field read against a different file, because RoCC hands an
accelerator operand *values* and only rd's index. That costs an `li` at the call
site and buys one instruction set that decodes on both hosts.

§23.6 warns that re-encoding "once traces and tools exist" is churn that
invalidates results. That is exactly why it was done now: no result has been
taken from this ISA yet, and the same binaries run unmodified on Rev and on
Vanadis.

#### Four bugs in Vanadis that NMFC required fixing

All four are in the sst-elements fork, and each is the kind that only a program
actually using the coprocessor would meet.

1. **RoCC results were written to the wrong physical register.** `vanadis.cc`
   did `setIntReg(resp->rd, ...)`, but `resp->rd` is the *ISA* register number
   -- it is all `RoCCInstruction` carries, and `RoCCResponse::rd` is a `uint8_t`,
   which a physical index would not fit in -- while `setIntReg` indexes the
   physical file and asserts against `count_int_regs`. Correct only until rename
   moved something. It now asks the instruction for its own renamed destination.

2. **RoCC instructions issued speculatively.** This is the serious one, and it
   is architectural rather than clerical: a coprocessor instruction is
   *architecturally visible the moment it issues* -- an NMFC `FORK` puts an
   invocation on the fabric and there is no taking it back. A branch mispredict
   flushed instructions that had already executed, and their responses then
   wrote physical registers that had been recovered and reissued. RoCC
   instructions now issue only when they are the oldest entry in the reorder
   buffer, which is how a real core handles an operation it cannot squash.

3. **`useMMU: false` left a null MMU that every syscall dereferenced.**
   `ProcessInfo::virtToPhys` and `removeThread` both call through it
   unconditionally. A machine may perfectly well translate somewhere other than
   the OS -- NMFC translates in the memory interface, because placement is the
   address space owner's decision and the host and the near-memory cores have to
   agree on the answer (invariant 4).

4. **An unconditional whole-register-file dump on every RoCC issue**, which
   dominated the run time of any program that used the interface.

#### What else it took

`m_stack_top` is hard-coded at `0x7ffffff0` and the program headers default to
`0x60000000`, so the machine's physical address space had to be **4 GiB**; the
DDR5 device modelled here has far more capacity than that (§17), and what was
limiting was the range the controllers declared. Vanadis also redirects the
guest's stdout to a file named `stdout-<pid>`, which is where a test's result
is.

**Ending the program needed two different things from one binary**, and getting
it wrong was instructive. Rev enters at `main` with `ra = 0`, and *returning*
from main is what ends the thread cleanly -- which is what writes the
statistics. Vanadis enters at the ELF entry point, where a return jumps to
address 0 and faults. Making the tests call `rt_exit()` fixed Vanadis and
silently broke Rev: its `ECALL_exit` calls `exit()` on the host process, so
every run still printed PASS and not one wrote a statistic. The suite caught it
only because it checks that the MOESIF transitions were exercised, and they had
all gone to zero.

The resolution needs no fork of either core: the ELF entry is a `_start` stub
that calls main and exits through the OS, and Rev is pointed at `main` with its
`startAddr` parameter, which overrides the entry point when it is non-zero. One
binary, each host taking the path it needs, and `_start` simply unreachable on
Rev.

One bug of ours surfaced only here: with a **split** L1 pair, an inner cache's
miss did not say which port it came in on, so the L2 answered the instruction
cache's fetch on the data cache's port and the core fetched the same line
forever. Rev's unified L1 could not reach it.

#### Sizing

Caches and fabric are at modern magnitudes throughout: 48 KiB 12-way L1D at 5
cycles, 32 KiB 8-way L1I, 2 MiB 16-way L2 at 16, 4 MiB slices, and 48/16/64
outstanding misses respectively -- a big core keeps dozens of misses in flight,
and a cache that allows eight is a different machine no matter how large it is.
The fabric is 64 B/cycle with a four-cycle tile hop and a twenty-cycle toll for
a host that is not on a stack.

#### Result

The whole program matrix runs on the out-of-order host -- `tile_mem`,
`tile_slice`, `tile_atomic`, `tile_edge` and `tile_coh`, at one and two tiles --
against the MOESIF fabric and ramulator2 underneath. `run_vanadis.sh` is the
suite. The step-1 programs stay on Rev's `run_nmfc.sh`, because they exercise
the tracking unit against a loopback stub rather than a tile.

What remains before §24 step 0: nothing in the machine. The baseline is stock
GAPBS with no NMFC at all, and it can be run on this core, against this memory
system, whenever the workload is ready.

## 27. The first workload, and the first thing it said

`test/tile_bfs.c` is a breadth-first search over a CSR graph: skewed degree, a
row lookup and then an edge chase, `parent[]` claimed by AMOSWAP so that a
vertex enters the frontier exactly once, and the frontier appended through an
AMOADD counter. It is the shape §1 names -- graph traversal, pointer chasing,
sparse indexing -- rather than another microbenchmark.

One source builds two programs. `host_bfs.exe` runs the reference algorithm on
the host and nothing else; `tile_bfs.exe` runs the same traversal over the same
graph with one invocation per frontier vertex, and then runs the reference
afterwards and compares. That is deliberate: invariant 8 says never to compare
the architecture against a weaker algorithm, and the cheapest way to honour it
is to make the baseline the same source.

It agrees with the reference at one, two and four tiles.

### 27.1 The first numbers were an artefact, and the artefact was a page table

**Retracted.** This subsection first reported 55,455 migrations -- one per 7.8
instructions, "129x over invariant 5's budget" -- and concluded that the kernel
was shaped wrong. The kernel *is* a chase, and §14.0 and §20.2 do say a chase
migrates. But the number was not measuring that. It was measuring a bug, and
the bug was in `PageTable`.

`PageTable` was constructed with a "block size" of 64 and used it for two
things it must never be used for:

```
pageOf( addr )  ->  addr / 64          // the TLB and per-context translation key
lookup(addr).tile -> (addr / 64) % N   // which tile owns a REGULAR page
```

A page table maps at *page* granularity. Keying translations on 64-byte cache
lines makes every line its own page, so the tile TLB and the four entries a
context carries can never hit on the next access to the same array -- which is
what produced 87,039 walks. And deciding ownership per cache line means a
context believed it owned one line in N while the fabric, the LLC slices and
the memory controllers all partition at the **grain** (1 MiB at four tiles). So
a context walking a contiguous array that lives entirely on one tile migrated
every 64 bytes, arrived at a tile that did not hold the data either, and its
access went back over the fabric to the real owner. The migrations bought
nothing because they were computed from a partition nothing else used.

This is invariant 9's failure mode exactly -- "a grain sits on the tile its
physical address names" -- and §18's lesson repeated: the congruence check
exists, is called from `NMFCTile`, and only inspects GRAIN regions, so REGULAR
pages were never checked at all.

Ownership is now decided at the grain, matching the fabric and the memory
system, and a translation is keyed on a 4 KiB page. Same graph, same kernel,
four tiles:

| | before | after |
|---|---|---|
| instructions | 431,462 | 302,467 |
| **migrations** | **55,455** | **3,068** |
| migrations per instruction | 0.1285 | **0.0101** |
| walks | 87,039 | 12,320 |
| page-table references | 261,117 | 36,960 |
| cold arrivals | 50,798 | 1,948 |
| loads + stores + atomics | 83,843 | 83,747 |

The memory-operation count is unchanged -- the program does the same work -- so
the 52,000 migrations that disappeared were entirely artefact. Instructions fell
30% because a migration re-executes the instruction that caused it.

**And the graph was too small to mean anything.** At 4096 vertices the whole
working set is about 420 KiB -- one grain, therefore one tile. Placement had
nothing to decide and migration nowhere to go, and with `first_touch` the run
recorded *zero* migrations, which is not a result, it is a degenerate
configuration. `tile_bfs_big.exe` sizes the graph at 131,072 vertices so its
data spans roughly thirteen grains across the tiles and exceeds the LLC slices.

On that graph, four tiles, with translation fixed (§27.2):

| | instructions | memory ops | migrations | per instruction | per memory op |
|---|---|---|---|---|---|
| round-robin | 10,941,373 | 3,299,133 | 1,638,325 | 0.150 | 0.497 |
| first-touch | 10,842,812 | 3,299,777 | 1,539,777 | 0.142 | 0.467 |

and the parity target §14.0 already measured for a chase:

| | migrations per instruction |
|---|---|
| ChampSim, chase, GAP BFS | 0.7428 |
| ChampSim, chase, synthetic scattered | 0.384 |
| **Rev, chase, this BFS** | **0.150** |
| ChampSim, chase, oracle placement | 0.020 |
| ChampSim, spawn | 0.0015 |

Rev's chase is better than ChampSim's chase and the same order of magnitude,
which is what it should be: it is the same shape of function.

**Placement cannot fix a shape.** `first_touch` dispatched 131,034 of 131,072
invocations to the tile holding the first address they would touch -- as close
to a perfect first placement as the policy can get -- and bought **6%**. The
NUCA policy, meanwhile, examined 818 co-access components and declined to move
every one of them, because each was larger than a tile's fair share of the
grains: on this graph everything touches everything, the co-access graph is
fully connected, and collapsing it is the failure an offline minimum cut already
demonstrated. Both results say the same thing §14.0 said -- "the spawn
decomposition under a *scattered* placement beats the chase decomposition under
the *oracle* placement" -- and this model now reproduces it independently.

What survives from the original conclusion is only this: a chase migrates
because it reaches for data it does not own, and the remedy is a differently
shaped unit of work. That was true before the bug and is true after it. It just
was not what these numbers were showing.

### 27.2 Retracted: translation was not expensive, the TLB was unusable

**Retracted twice over.** This subsection reported that 95% of arrivals took a
cold walk and that page-table traffic ran at 0.61 references per instruction,
"comparable to the program's own". Both were artefacts, and of two separate
mistakes in the same area.

**The page size did not follow the page type.** §5.4 says NMFC data uses G-sized
huge pages and *everything else* keeps 4 KiB. `pageOf()` keyed grain and
duplicate regions at G but keyed REGULAR pages -- which are NMFC-mode -- at
4 KiB. That is wrong twice: it throws away the translation reach the huge page
exists to provide, and it silently demands the OS keep 256 consecutive 4 KiB
frames adjacent to make one G-sized silo, which is the contiguity requirement
the grain page was introduced to remove.

**And the TLB was indexed by the low bits of a sparse key.** A region-typed page
is tagged in bits 40 and above, so `page % entries` is *zero for every grain and
duplicate page in the machine*. They all landed in slot 0 and evicted each other
on every alternation between an instruction fetch and a data access. No table
size changes that -- 64 entries and 4096 entries gave bit-identical results,
because a larger table indexed the same way is the same table. The index is a
hash now.

Same graph, four tiles, before and after:

| | before | after |
|---|---|---|
| walks | 14,260 | **12** |
| TLB hits | 66 | 14,320 |
| page-table references | 42,780 | **36** |

On the large graph, a ten-million-instruction run does **38 to 51 walks in
total**. Translation is not a cost worth reporting here; it was a bug worth
finding. Invariant 11's "arrival is not costly" stands, and this model no longer
contradicts it.

### 27.3 The hand-off chain does not fire on this workload

§25.6 measured sixteen invocations on one counter costing two memory reads,
fourteen taken by hand-off. On BFS: 36,863 atomics and **2 hand-offs**. Nearly
every atomic is on a *different* word -- `parent[n]` for distinct n -- so there
is no chain to pass along, and the one genuinely shared word (the frontier
counter) is a small fraction of the total. The optimisation is real and the
earlier measurement was not wrong; it was a measurement of a contended counter,
which is not what a traversal mostly does.

### 27.4 A coherence hole the workload found

The held-word table (§25.6) keeps an atomic's word **above** the data cache on
purpose, and nothing connected it to the directory. Two tiles cannot contend for
a line by construction -- but a host and a tile can, and here they did: the host
writes `frontier_count = 0` between levels, the write is applied to a copy no
tile is reading, and the tiles keep incrementing a stale value. Every level was
expanded roughly three times over. The traversal was still *correct* -- every
vertex agreed with the reference on whether it was reached -- which is what made
it worth catching: a wrong count and a right answer is exactly the failure that
survives a test suite.

Three things were needed, and the second and third were only found because the
first was not enough:

1. **The tile is told when a line is snooped away.** The cache pushes the snoop
   up to its client, which hands back the word it was holding; the cache patches
   it into the line before answering. A leaf cache does this only when
   `notifyClient` says its client keeps state above it.
2. **A held word's line is pinned in the cache.** Releasing on snoop is useless
   if the line was quietly evicted first: the directory then no longer believes
   this agent holds anything, and the snoop never comes. Pinning rides on the
   request that fills the line, so there is no window between the two.
3. **A pin is a request, not a right.** Pinning every atomic's line filled whole
   sets, and a fill then had nowhere to land. A set with no free way asks the
   client to give the oldest pinned line up and the fill retries. Without that
   the cache wedges on a workload doing atomics to colliding addresses -- which
   is any workload doing enough atomics.

## 28. Audit against the ChampSim machine

Read from the code rather than from this document. Where an answer is "no", it
is a gap, not a decision, unless it says so.

### 28.1 What is verified

| | |
|---|---|
| **One ramulator instance per memory tile** | Yes. `coherent_memory.build()` creates a `memHierarchy.MemController` per tile, each with its own `memHierarchy.ramulator2` backend and its own `statsFile`. |
| **LLC slice banks aligned to DRAM banks** | Now. Each slice was banked `DRAM_BANKS / ntiles`, so a four-tile machine had 8-bank slices over 32-bank channels and a cache bank stopped being the same partition as the DRAM bank behind it. A tile owns a *whole* channel, so the divisor was wrong; slices are banked to the channel's bank count. (The device declares 2 ranks x 8 bank groups x 4 banks = 64 banks per channel while §5.2's arithmetic uses 32 -- per rank. The two should be reconciled before any bank-conflict number is quoted.) |
| **Address partitioning across tiles** | Now, and this was the §27.1 bug: three components decide it and one disagreed. `NMFCCoherenceFabric::tileOf` and the slice/controller interleave both use `(pa / G) % N`; `PageTable::lookup` used `(pa / 64) % N`. They agree now, and the memHierarchy ranges are declared so the slice *asserts* the partition rather than trusting it. |
| **Migration payload** | `MigrationEvent::SIZE_BYTES = NMFC_CTX_BYTES + 8 = 72` -- the 512-bit register file and the program counter, exactly invariant 11's arithmetic. `migrate()` sends the translation's tile, refuses to migrate a context to itself, and releases any held word before leaving. |
| **Only data migrates** | `migrate()` is called from exactly three places: `issueLoad`, `issueStore`, `issueAtomic`. Instruction fetch translates but never migrates; the page walk issues physical addresses and never translates. Migrations (3,068) are well under memory operations (83,747). |
| **Per-context fetch and data slots** | `ibufValid/ibufPC/ibufInsn` and `dbufValid/dbufReg/dbufValue` per context; the shared BTB drives `requestFetch` a re-issue window ahead (`btbLookups`, `btbCorrect`). Separate `fc I$` and `fc D$` components per tile, both above that tile's LLC slice. |
| **512 bits, no stack** | Every register access goes through `RegLayout::defines()`, and a function touching a register the layout does not define is refused at issue. The default layout is eight 64-bit lanes over 64 bytes. |

### 28.2 Closed since

**The placement policy exists (invariant 6).** `NMFCCoherenceFabric` runs
R-NUCA's classification behind Carrefour's gate, ported from `nuca_router.cc`
and adapted where Rev differs:

* the fabric is where it lives, because it is the only thing that sees every
  migration -- and a migration is the evidence (invariant 5);
* a migration unites *two* grains rather than pulling one, and a component
  moves as a unit. Placing grains singly cannot escape a random start: a
  cluster scattered over N tiles pulls uniformly from all N, so the dominant
  puller is noise until a majority already sits somewhere;
* a component larger than a tile's fair share of the grains is the working set,
  not a hot set, and stays interleaved -- R-NUCA's answer for shared read-write
  data, and the failure an offline minimum cut already demonstrated;
* the imbalance gate is measured over a **window**. A lifetime average starts
  even and responds to nothing, which in the model this came from let the
  fourteen hottest grains be co-located before the gate noticed;
* a grain that has just moved sits still, for longer the more often it has
  moved. Without that the policy oscillates -- it reacts to a pull, and the
  pull reverses because the migrations now come from where the grain used to
  be. Measured: at a 50-migration epoch, 29 moves and 60 MB copied became 3
  moves and 6 MB, with 56 attempts withheld.

**A remap moves the data.** This is where Rev and the trace-driven model part
company: `nmfc_vmem.cc` changes `nmfc_grains_[key]` and is done, because nothing
there reads page contents. The tiles here execute against real memory, so a
mapping change without a copy is corruption. The fabric copies the grain --
`nucaCopyBytes` is a megabyte read and a megabyte written per move -- and only
then broadcasts. That cost is why the gates matter rather than being decoration.

**A move reaches every copy of the table.** There is one page table and it lives
on duplicate pages (invariant 3), so in this model there are N+1 `PageTable`
objects that must agree: one per tile, one in the host's MMU, one in the fabric
that decides. A `RemapEvent` goes to all of them and each flushes its cached
translations for that grain. A copy that missed one would resolve to a frame
that has been given back -- the same class of disagreement as §27.1, which is
why it is broadcast rather than recomputed.

**Congruence is checked for every region type**, not only grain.

**Mixed page sizes were already there** and this audit got that wrong the first
time: `pageOf()` keys grain and duplicate regions at G -- those are §5.4's huge
pages -- and regular regions at 4 KiB.

**The measurement path is the out-of-order host.** `run_workload.sh` runs the
BFS and its baseline on Vanadis and prints migrations per instruction against
§14.0's ChampSim figures. The Rev configurations remain the fast functional
path, which is what they are good for.

### 28.3 What is still not

**No mapping-mode bit (§5.3).** The bit exists so a STANDARD allocation and an
NMFC one can coexist without contending for the same bank and column slots
(§5.2). Implementing it means the memory controller must read the bit, strip it
and apply one of *two* address mappings; memHierarchy's does one. Everything
here is NMFC-mapped, so nothing is currently wrong -- but a machine running an
ordinary program beside an offloaded one is not modelled, and that is the case
the bit exists for.

**No spawn, and no measured substitute.** ChampSim's `function_core.cc` has
`issue_spawn` and §14.0's 0.0015 migrations per instruction depends on it.
Invariant 10 forbids it here, which is a decision. What has not been done is
measuring the sanctioned alternative: §20.2 says reshape the unit of work, and
for this BFS that means bucketing a vertex's neighbours by tile on the host and
forking one invocation per (vertex, tile) -- so an invocation only ever touches
data it owns and never migrates at all. That experiment is the one §27.1's
surviving conclusion asks for, and it is workload design rather than machine
work.

**The device declares more banks than §5.2's arithmetic uses.** 2 ranks x 8
groups x 4 banks = 64 per channel; the grain formula uses 32, which is the
per-rank count. They should be reconciled before any bank-conflict number is
quoted.
