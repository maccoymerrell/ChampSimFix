# The near-memory function-core architecture — September 2026

This document describes one computer: a conventional processor with small engines placed
beside its memory, able to run short pieces of the program's own code where the data
already is. It states what the machine is, how one engine works inside, which parts of it
a cycle-accurate simulator models today and which are designed but not yet modelled, what
the three workloads measure on it, and what the next piece of work is.

It is written for a reader who has seen none of this before. Every term is defined where
it is first used, every table has a sentence before it and after it, and every performance
number says where it came from. Nothing here is asserted that was not either measured or
explicitly marked as a design not yet built.

---

## 0. Terms

**Host.** An ordinary out-of-order 64-bit RISC-V processor running the program, at 3.0 GHz
in every measurement below. It does everything a processor normally does; the engines are
not a replacement for it.

**Tile.** A unit sitting beside one memory channel. It contains a **function core**, that
core's private instruction and data caches, a slice of the shared last-level cache, a
memory controller and a memory channel. The machine measured here has four tiles.

**Function core**, also **engine** or **tile core**. A small in-order processor that runs
short functions close to memory. It holds many **contexts** and runs them concurrently.

**Context.** One invocation's entire state: 512 bits of register file, a program counter,
and the two small holding registers described in §2.1. There is no stack. A context is
created when work arrives at a tile and destroyed when that work ends.

**Invocation.** One call of a function on an engine. The host starts one with a `FORK`
instruction and collects its result with a `JOIN`.

**Tracking unit.** The structure on the host that holds one entry per invocation from its
`FORK` until its `JOIN`. An entry is *not* a running context: it is also held while an
invocation has finished and is waiting for the host to collect it.

**Line.** 64 bytes, the unit a cache moves. **Set**: the group of places in a cache where
an address may sit. **Way**: one of those places. **Bank**: an independently addressed
piece of a cache or a memory array that performs one access per cycle.

**Pipe.** One execution datapath inside an engine. An engine has several, and in one cycle
each may start an instruction belonging to a *different* context.

**Migration.** When an invocation running on one tile reaches for memory a different tile
owns, the invocation moves to that tile and continues there. It is the normal mechanism,
not an exception.

**Work unit.** The program's own count of what it has finished — a vertex settled, a sum
performed, a table operation completed. It is the axis two builds of one program are
compared on, because an instruction count is not the same quantity in two different builds.

**Arm.** One build of one workload. Every comparison below is a **baseline** arm, in which
the whole computation runs on the host, against an **offloaded** arm built from the same
source in which one named step runs as a function on the tiles.

---

## 1. The machine in one page

### 1.1 The shape

The host sits above a coherence fabric. Below the fabric are four tiles. A tile owns a
fraction of the **physical** address space outright: one slice of the last-level cache, one
memory controller, one memory channel, and the function core that sits in front of them.
Which tile owns an address is a property of the physical frame, never of the virtual
address, so a request must be translated before anybody can know where it goes.

```
                 ┌──────────────────────────────┐
                 │   HOST  (out-of-order core)  │   FORK / JOIN
                 │   private L1, L2             │   tracking unit
                 └──────────────┬───────────────┘
                                │
                 ╔══════════════╧═══════════════╗
                 ║  COHERENCE FABRIC (directory) ║
                 ╚══╦═══════╦═══════╦═══════╦═══╝
                    │       │       │       │
               ┌────┴──┐┌───┴───┐┌──┴────┐┌─┴─────┐
               │ TILE 0││ TILE 1││ TILE 2││ TILE 3│
               │ core  ││ core  ││ core  ││ core  │   contexts, pipes
               │ I$ D$ ││ I$ D$ ││ I$ D$ ││ I$ D$ │   banked
               │ LLC   ││ LLC   ││ LLC   ││ LLC   │   slice, banked
               │ MC    ││ MC    ││ MC    ││ MC    │   = DRAM banks
               │ DRAM  ││ DRAM  ││ DRAM  ││ DRAM  │   one channel each
               └───────┘└───────┘└───────┘└───────┘
```

The configuration every measurement in §4 was taken at is one line of that picture made
concrete, and it is configuration rather than design: four tiles, 128 contexts per engine,
a 4 MiB last-level slice per tile, one 32-bit DDR5-4800 subchannel per tile (19.2 GB/s a
tile, 76.8 GB/s over the four), and invocations placed on the tile that owns the first
address their context names. No count in this document is locked; all of them are swept.

### 1.2 Page types, and the one rule about duplicated pages

Three placements exist for tile-owned memory, and they decide which tile holds what.

| page type | placement | what it is for |
|---|---|---|
| striped | spread across the tiles, one grain at a time | ordinary data, when no locality is wanted |
| grain | every frame on one tile | data that should sit beside one engine |
| duplicate | one identical copy per tile | what every engine needs locally: code, the page table, data that is read-only for the whole run |

A duplicated page has one virtual address and **four real physical addresses**, one per
tile. That is the whole of the rule that follows from it:

> **A function core must never write a duplicate page.** A host core may, freely: the
> coherence fabric either gives the host ownership of the block or broadcasts the write,
> and the copies stay identical. A write from an engine would need a cross-tile coherence
> mechanism this machine does not have, so an engine's store or atomic that resolves to a
> duplicated page is refused as a fault and counted. The counter is gated to zero in both
> test suites, and a directed test shows the fault, shows that no copy diverges, and shows
> that a host store still reaches every copy.

One consequence is worth stating because it shapes programs: a table the computation
rewrites cannot be duplicated. If a program wants a replicated table it also writes, the
answer is to put the bits beside the data they describe so that the owner writes them
locally, and to let probes from other tiles migrate. Duplication is for things that do not
change while the computation runs.

There is one legal write to a duplicated page from inside a tile, and it is privileged, not
a kernel store: after a page is remapped, the tile rewrites the affected leaves of its own
copy of the page table. §2.5 gives that request its own class and its own reserved
capacity, precisely so that it is never confused with the refused kernel store.

### 1.3 The 512-bit context and its lanes

A context is **512 bits, bit-packed**, laid out by the program. It is not "eight
registers": eight 64-bit values is one possible view of those bits, never the architecture.

An ordinary RISC-V instruction's five-bit register field names a **bit range** of the 512,
not an entry in a file:

| register name | what it names | width |
|---|---|---|
| `r0` | reads zero | — |
| `r1` | the whole 512 bits, for save and restore | 512 |
| `r2`–`r7` | the two 256-bit halves and the four 128-bit quarters | 256 / 128 |
| `r8`–`r15` (`d0`–`d7`) | the eight 64-bit lanes | 64 |
| `r16`–`r31` (`w0`–`w15`) | the sixteen 32-bit lanes | 32 |

The two lane tilings are **overlapping views of the same bits**: `w2k` is the low half and
`w2k+1` the high half of the same 64 bits that `dk` names. There is no renaming, because
the pipes are in-order and a name is a bit range, so there is no map to rename through.

No operation in the present instruction set is wider than 64 bits, so `r1`–`r7` name bits
nothing can compute on, and the decoder traps any instruction that uses one as an operand.
The map is verified lane by lane in both directions and in both simulators — the
cycle-accurate model and the functional one that produces its starting images agree on all
sixteen checks, and each of the seven reserved names kills a build with the same diagnostic
in both. Where the whole file moves, no register is named at all: `FORK` carries it in,
`JOIN` carries it back, the continue instruction hands it to a successor, and a migration
carries it between tiles as the 64 bytes of register file and the program counter, and nothing else: the instruction slot and the data slot stay behind, because an instruction or a value fetched on one tile belongs to that tile's view of memory, and the context re-fetches its instruction and re-issues its memory operation on the tile it arrives at.

### 1.4 The instruction set

Fourteen instructions, of which twelve are the user-level base set:

`FORK.R` `FORK.M` `FORKF.R` `FORKF.M` `FORKQ` `JOIN` `JOINQ` `END` (with a return bit)
`CONT` `CONT.M` `CXW` `CXR` — plus `KILL`, which is also unprivileged and tears down a
failed program's contexts, and `RESUME`, which is privileged and returns from a fault.

`FORK` takes a general register holding the callee's entry address and a 512-bit context
register that *is* the callee's register file; `JOIN` retrieves it. The `.M` forms take the
context from memory instead of from a context register; the fire-and-forget forms do not
reserve a result. `FORKQ` and `JOINQ` ask the machine a question — how many entries the
tracking unit has free, and whether a given invocation has finished — which is how a
program discovers the machine's capacity at run time instead of being compiled against a
constant. `END` finishes an invocation, with or without returning the register file.
`CONT` hands a context to a successor invocation without returning to the host. `CXW` and
`CXR` move bits between the host's ordinary registers and a context register.

Underneath those fourteen, a function core executes ordinary RISC-V: loads, stores,
integer arithmetic, branches and atomics. There is no floating point, no vector unit and no
system instruction on the engines.

### 1.5 The tracking unit, sized to the contexts

An entry in the tracking unit is held from `FORK` to `JOIN`. Entries are therefore the
invocations that may exist at once, and an invocation that cannot exist cannot occupy a
context. A unit smaller than the machine's context count leaves part of the machine
unreachable however much work a program has, so the hardware's depth is **derived from the
machine being built** as the larger of two floors: the machine's own contexts (tiles ×
contexts per tile), and the count the binaries in use were compiled against. Nothing caps
it. The fabric's control queue follows the unit, because a host credit is a place in that
queue and leaving it at a smaller number would move the same cap one component down.

| machine | contexts | tracking unit | control queue |
|---|---|---|---|
| 4 tiles × 128 contexts (the measured machine) | 512 | 512 | 512 |
| 4 tiles × 32 contexts (configuration default) | 128 | 256 | 256 |
| 1 tile × 32 contexts (unit tests) | 32 | 256 | 256 |

The number in the instruction-set header stays, because a program that builds a
fire-and-forget ring has to compile against something; what changed is that the hardware no
longer takes that number as its own size. The functional simulator that writes starting
images derives the same depth the same way, because an image names entries by index and a
timing model must refuse an image whose handles mean something else.

**An entry is not a live context.** This is the single most important reporting rule in the
document, and §4.7 is about what happens when it is forgotten: across the measurements
below the tracking unit routinely holds many entries while the engines hold very few live
contexts, because most of those entries are invocations that have already finished and are
waiting to be collected. Context parallelism is reported per engine, split by state.

---

## 2. Inside one tile core

The tile core becomes **two independent pipelines that meet in one place**. The centre of the core is the **context array**: every context with its 512-bit register file, program counter, instruction slot and data slot. The slots belong to the context, not to a pipe. Each cycle the scheduler picks ready contexts out of the array into the pipes, and a context is ready only when its instruction is in its slot and it has no memory operation outstanding, so the pipe takes its instruction *from the slot* and never from a cache; if a context is scheduled it is certain to execute. The pipes issue what fills the slots: instruction fetches, speculatively at decode from the program counter or the shared branch-target buffer and non-speculatively at writeback, and data requests at writeback. Every one of those requests passes the translation path first, and none counts as issued until it is translated: a request whose translation faults or names another tile is dropped outright, and the context faults or migrates and re-attempts. Translated instruction fetches go to the banked instruction cache, which fills the context's instruction slot; translated data requests go through the delivery window into the memory queue that owns their physical address, then the data-cache bank, the last-level slice and memory, and the returned data fills the context's data slot, which is what wakes it.

```
   CONTEXT  (512 bits, PC, two slots)
      │                                 ▲
      │ instruction side                │ data side
      ▼                                 │
  [instruction slot] ◄── banked I$ ◄── I-translation
      │  decode: PC+4, or BTB target      (shared BTB, one
      │                                    speculative fetch)
      ▼
   issue into one of N pipes (M stages)
      │
      │  a load or store puts its VIRTUAL address in the load slot
      ▼
  ╔═════════ TRANSLATION PATH (virtually indexed) ═══════════╗
  ║  translation queues → shared TLB → page-table walker     ║
  ║  result: physical frame, page type, owning tile           ║
  ╚════════════════════════╤═════════════════════════════════╝
                           │ checks here: foreign tile → migrate;
                           │ kernel write to a duplicate page → fault
                           ▼
  ┌──────────── DELIVERY WINDOW (one structure) ─────────────┐
  │ decode bank bits; deliver the OLDEST entry per bank that  │
  │ has credit; at most one per bank per cycle                │
  └──┬──────────┬──────────┬──────────────────────┬──────────┘
     ▼          ▼          ▼                      ▼
  ┌─────┐   ┌─────┐    ┌─────┐               ┌─────┐
  │MEMQ0│   │MEMQ1│    │MEMQ2│   ...         │MEMQb│   each owns one
  │order│   │order│    │order│               │order│   bank's addresses:
  │ fwd │   │ fwd │    │ fwd │               │ fwd │   order, forwarding,
  │ RMW │   │ RMW │    │ RMW │               │ RMW │   atomicity
  └──┬──┘   └──┬──┘    └──┬──┘               └──┬──┘
     ▼         ▼          ▼                     ▼
  ╔═══════ BANKED DATA CACHE — one coherence client ═════════╗
  ║  bank array + tags; read-modify-write executed here;      ║
  ║  a snoop acts HERE, never on a queue                      ║
  ╚════════════════════════╤═════════════════════════════════╝
                           ▼
            LLC slice → memory controller → channel
```

The structural novelty is that the two sides share nothing. A translation miss occupies
translation hardware and leaves every memory queue free; a bank conflict occupies a memory
queue and leaves translation free. That is worth having only if it can be shown, so the
counters are arranged so that a stall on one path appearing as a stall on the other is a
detectable defect rather than an argument.

### 2.1 The two context slots

A context has exactly two small private holding registers, and that count is structure. Each context is an independent thread with its own program counter, its own instruction in hand and its own outstanding access; contexts do not execute in step and no two need be at the same instruction. The engine is a fine-grained multithreaded core, not a vector or single-instruction-multiple-thread machine.

**The instruction slot** holds what the context will execute next. A context cannot be
scheduled without its instruction in hand. At the end of a dispatch the tile already knows
where the next instruction is — the program counter plus four, or, if the instruction just
decoded is a branch the shared branch-target buffer knows, the predicted target — and that
address is sent to the instruction path immediately, a full re-issue window before the
context can use the answer. Nothing executes on the prediction: a wrong target means the
slot holds an instruction the context will not use, and the cost is exactly the refill a
machine with no predictor pays every time. The buffer is shared by every context on the tile, as a target buffer indexed by program counter is in any multithreaded core; contexts run independent instruction streams, each with its own program counter and its own slots, and sharing the buffer is what keeps it from growing with the context count.

Because the slot holds the next instruction and one speculative fetch covers the whole
re-issue window, the tile needs **no decoupled fetch engine and no run-ahead fetch stream**.
That is a deliberate absence, not an omission.

Whether the slot holds one instruction or a short aligned block is open, and it is a trade
between per-context state, which multiplies by the context count, and instruction-cache
traffic, which multiplies by the pipe count. The code says what the trade looks like:
counted statically over the three function images in the tree, straight-line runs between
one control transfer and the next have a mean of 4.6 to 4.8 instructions, a **median of
three**, and a longest run of 13 to 36. A whole kernel is eight or nine cache lines of code.

| slot holds | per-context state | at 256 contexts | at 1024 contexts |
|---|---|---|---|
| 1 instruction | 87 B | 21.8 KiB | 87 KiB |
| 4 instructions | 99 B | 24.8 KiB | 99 KiB |
| 8 instructions | 115 B | 28.8 KiB | 115 KiB |
| 16 instructions (one line) | 147 B | 36.8 KiB | 147 KiB |

Those are state costs, not results; runs with a median of three instructions mean a block
much larger than a few instructions mostly pays for instructions a taken branch discards.
The structure is that the slot holds a contiguous aligned run with a valid mask and a
consumed offset; the size is configuration, swept at 1, 4, 8 and 16, and the rule written
before the numbers exist is to take the smallest block whose waiting-for-instruction cycles
are within measurement noise of the largest. One instruction remains a legal setting and is
the reference point, because it is what the model does today.

**The load slot** holds the one memory operation a context is waiting on, whatever its kind: a load, a store, a read-modify-write atomic, or either half of a load-reserved / store-conditional pair. It carries the operation, the virtual address, the access width and sign-extension class, the destination register name, and for a store or an atomic the value operand. It is the context's *single* outstanding memory operation, so on return there is nothing to disambiguate and no staging area is needed, and the context cannot begin its next instruction until that operation has returned its value or, for a store, has committed in its memory queue. The operation is not issued until it is translated; before that it can be dropped freely, because nothing downstream has seen it, and the context re-attempts it after a fault is handled or after it migrates. That is why a load-reserved's serialisation point begins only when its entry enters the memory queue: a context has not claimed an atomic attempt until then. A context that migrates carries no slot with it: the operation whose translation named the other tile was dropped before it was issued, and the context re-issues it on arrival. A load wakes the context when its data has been written into the named bits; an atomic is performed in the memory queue that owns its address and wakes the context the same way, with the old value or the store-conditional's result in the named bits. Atomics are ordinary traffic on this path, and the queues exist in large part to make them cheap.

One outstanding operation per context is policy, not an accident. A load and an atomic occupy the slot until their value returns. The default applies to stores as well: a store occupies the slot exactly as a load does, so a context has at most
one memory request anywhere in the machine at any time and its own accesses are in program
order whatever the delivery window does. A relaxed rule — a store released at queue
admission, with a small per-context count of outstanding stores that must reach zero before
the invocation retires — is a configuration switch with both arms correct, because it buys
store throughput at the price of two new obligations: a context with outstanding stores
cannot migrate until they have drained, and per-destination order then rests entirely on
the delivery window's selection rule. The switch carries its own before-and-after
measurement; it does not become the default by assertion.

The load slot is also where three decisions are taken, because all three are properties of
the *translation result* rather than of the virtual address: a translation naming another
tile turns the access into a migration; a translation naming a duplicated page on a store
or an atomic is refused and counted; a translation that misses leaves the slot occupied
while the walk runs.

A data prefetcher could fill the load slot speculatively from previous addresses. It is
deliberately undesigned, and the slot is left free for one.

### 2.2 The pipes and the issue rule

An engine has **N pipes of M stages**. Each pipe is M stage registers; a stage register is
either empty or holds one in-flight instruction with its owning context, its decoded
operands, its destination name and the cycle it entered. An instruction advances one stage
per cycle and leaves at stage M. Nothing is squashed and nothing is replayed, so a stage
register never needs a recovery copy.

Three stages are named, because three events depend on which stage they happen in, and
those three are the whole of the pipes' interaction with the rest of the core:

1. **decode** — operands are read from the 512-bit context and register names are checked;
   the branch-target-buffer lookup and the next instruction fetch are launched;
2. **address** — a memory instruction's virtual address is complete and leaves for the
   translation path; a load's context yields here;
3. **writeback** — the result is written into the context and the context becomes eligible
   again.

**The issue rule, which is the default and the working assumption: one instruction from
each of N different contexts may start per cycle, and a context issues at most once every
M cycles.** Two instructions in the pipes are therefore never from the same context, never
dependent, and the pipes need no forwarding, no interlocking and no hazard detection: a
pipe is a shift register with an arithmetic unit hung off it. Reaching N issues per cycle
needs at least N×M contexts that are not waiting for anything, which is the floor the
context count is chosen against.

The alternative is to **bind a context to one pipe** and give the pipe cross-stage
forwarding, so that a context can issue on consecutive cycles; a binding ends at a memory
instruction, at an empty instruction slot, at a mispredicted control transfer, at a
starvation limit, or when the context has nothing to issue. It costs per *pipe* — an owner
field, a consecutive-issue count, operand comparators and forwarding multiplexers — and not
per context, so it does not grow with the context count.

**The second alternative is not built on an argument; its build is gated on a measurement,
and the measurement runs on the model that exists today.** It can only help in a cycle
where two things are true at once: a pipe slot would otherwise go unused, *and* some
context is blocked on nothing but its re-issue window. If the engine is issue-saturated,
removing a window gains no work, because another context would have used the slot; if the
engine is starved because its contexts are all waiting on memory or on instructions,
removing a window also gains nothing, because the contexts are not ready. So the
discriminator is a conjunction rather than a histogram: count unused issue slots in cycles
where at least one context was window-blocked, against unused slots in cycles where none
was, and against the offered slots. Beside it go a census of context-cycles by cause of
not being ready — no instruction in hand, asleep on a load, waiting on a page-table walk,
other — and a banded histogram of load-free run lengths with the cause each run ended.
Those counters plus window-blocked cycles plus running occupancy **partition** a resident
context's cycles, so a gap is a modelling error rather than a finding. Both alternatives
stay in the model behind a parameter, so the comparison can be re-run; the loser is not
deleted.

### 2.3 The translation path

At the address stage a memory instruction's virtual address, address-space identifier,
width, operation and owning context enter a **translation queue**. Queues are indexed by
virtual address, because that is what a context presents. They are drained into the tile's
shared, address-space-tagged translation buffer, which is banked; a hit produces a physical
frame, the page type and the owning tile after a fixed latency, and about five cycles ahead
of the tag check is accepted. A miss starts a page-table walk; the page table is on
duplicated pages, so a walk never leaves home, and the request waits in its translation
queue rather than occupying anything on the data side.

Two numbers here are load-bearing and must be derived rather than written down.

**The completion rate is derived from the pipe count, not set to one.** The tile is
supposed to carry one instruction fetch and one data access per pipe per cycle; a
translation stage that completes one translation per cycle caps the whole data path at one
memory operation per cycle however many banks exist, and a cross-connection that cannot
limit the machine because the stage in front of it is serialised has not been shown to be
adequate. So the rate tracks the pipes, and the sizing arithmetic of §2.4 is read against
that rate. Whether a queue drains strictly in order — and therefore whether a miss blocks
hits behind it — is stated per configuration and counted as head-blocked cycles.

**Where a walk's own reads go is a design choice, and it is left open for measurement.** A walk's memory references can take one of two paths, and both are configurations of the model. Through the *data cache*: the reads pass the delivery window and a memory queue like any access, page-table lines compete with data for the cache, and to keep a full window from stalling the walk that would free it each memory queue reserves at least one entry for a walk, with a counter for cycles a walk could not be admitted. Directly to the *last-level slice*: every walk step pays the slice's latency, but translation traffic never pressures the data cache and the reservation is unnecessary. The trade is latency against data-cache congestion, and which wins depends on the translation buffer's hit rate and on the ratio of walk reads to data requests: if the buffer hits nearly always the choice is immaterial; if walks are frequent and the ratio approaches one, the data cache is severely pressured; in between, whether a page-table hit or a data hit is worth more decides it. The analysis is the counters on each path across the workloads: buffer hit rate, walk reads per thousand data requests, page-table line reuse in the data cache, walk latency on each path, and the data-cache misses the page-table lines cause.

### 2.4 The cross-connection: one window, oldest-per-bank

Translation queues are indexed by virtual address; memory queues are indexed by physical
address; the mapping between them is the page table, which is arbitrary and changes at page
granularity. Written as a connection, any source may on any access need any destination.
That is the one genuinely hard part of the tile, and the accepted answer is not a crossbar.

The observation that shrinks it: **the width of the connection is set by the rate at which
physical addresses are produced, not by the number of places they can come from.** A
context has at most one memory access outstanding and a pipe starts at most one memory
instruction per cycle, so the number of requests needing delivery in a cycle is bounded by
the translation completion rate — a small number — whether the engine holds 256 contexts or
1024. The all-to-all is between *completions* and banks.

Four mechanisms were compared. `P` is the number of requests that may complete translation
in a cycle, `B` the number of banks and therefore of memory queues, `W` a staging width.

| mechanism | deliveries per cycle | latency added | several requests want one queue in one cycle | state per context | growth |
|---|---|---|---|---|---|
| crossbar with per-crosspoint buffers | min(P, B) | 1 cycle | each bank takes one, the rest wait in their own buffer | none | `P × B` buffers — a product, as state |
| one FIFO per pipe with an arbiter per bank | min(P, B) in theory, about 0.59 of it under uniform traffic | 1 cycle | one wins; **everything behind the losers also waits** | none, unless order forces a per-context binding | `P` FIFOs, `P × B` request bits |
| a ring, one stop per queue | ring-limited | up to `B − 1` cycles, `B/2` on average | serialised by ring position, no wide arbiter | none | one stop and one cycle per bank |
| **one window, oldest-per-bank drain** | min(W, B) | 1 cycle | the oldest goes; the others **stay in the window** and go next cycle | none | `W` entries, a `W × B` decode |

**The accepted mechanism is the last one.** Completions enter a single window of `W`
entries, default the pipe width. Each cycle the window decodes the bank bits of every entry
it holds and, for each bank that has a free queue entry, delivers **the oldest window entry
naming that bank** — at most one delivery per bank per cycle. Four properties decide it.

It is one structure, which matters because the mechanism it replaces failed by having
several structures that had to agree. It preserves order per destination for free, because
oldest-first per bank means two accesses to one address cannot invert, and that is what lets
ordering, forwarding and atomicity be local properties of one queue in §2.5; reordering
*across* banks is invisible, because different banks are disjoint addresses. Its drain is
deliberately **not** in order: a strict FIFO drain of one queue into many banks delivers one
request per cycle and blocks on every conflict, and allowing the window to skip a blocked
entry costs exactly the decode that is already there. And its cost contains no product of
two growing numbers *as state* — the `W × B` term is wiring and logic inside one cycle,
which at `W` of 4 to 16 and `B` of 8 to 16 is an arbiter, not a network.

Its capacity is arithmetic rather than hope. With `k` requests in the window and uniformly
distributed destinations, the expected number of distinct banks among them is
`B(1 − (1 − 1/B)^k)`:

| banks `B` | k = 1 | k = 2 | k = 4 | k = 8 |
|---|---|---|---|---|
| 4 | 1.00 | 1.75 | 2.73 | 3.60 |
| 8 | 1.00 | 1.88 | 3.31 | 5.25 |
| 16 | 1.00 | 1.94 | 3.64 | 6.45 |

Read against the offered load — pipes times the fraction of instructions that touch memory,
which is 11 % of the static instructions in the two graph kernels and 24 % in the mixed
image — a window of two over eight banks already exceeds the plausible demand at four pipes
and a window of four is comfortable, and the rule of thumb is that banks should be at least
twice the window depth.

The escalation path, if measurement says the window is the constraint, is the same structure
replicated rather than a new one: first widen the window, which is configuration; then split
it by destination group, `G` windows each owning `B/G` queues with the group chosen by
high-order bank bits, each group proposing at most one entry per bank and the bank taking
one of the `G` proposals. Order per destination survives, because a destination belongs to
exactly one group. Widening the window continuously toward `B` walks the design from this
mechanism to the crossbar with every point correct, so the value of a crossbar is measured
rather than assumed.

One counter decides whether the connection ever limits the machine and the others explain
it: cycles in which a request was ready, its destination had room, and it was nevertheless
not delivered. If that is zero the connection is not the constraint and nothing else about
it needs defending. Beside it: window occupancy and full cycles; deliveries per cycle as a
histogram; entries that lost to an older entry for the same bank; entries that were oldest
but had no credit; delivery latency, mean and maximum; per-bank delivery counts, whose
spread is the imbalance; and the longest any entry waited while younger entries went, which
is the check that oldest-per-bank starves nothing. Two of those separate "the connection
limited the machine" from "the banks did", which is the distinction that decides whether
widening anything is worth doing.

### 2.5 The memory queues: one per bank, and the only ordering point

There are as many memory queues as data-cache banks, and queue `b` owns exactly the
addresses bank `b` holds. The bank index is a decode of physical address bits — the same
function the cache already uses — so **two accesses to the same address are always in the
same queue**, and a queue assigns an increasing sequence number to every request it admits.
That sequence *is* the tile's order for those addresses. There is no second party to agree
with and no protocol to run. Everything else in this section is a consequence.

**Issue condition.** An entry may go to the bank when no older entry in the same queue
overlaps its byte range. The check is a comparison against at most the queue's depth of
older entries, local to one queue. Entries to disjoint addresses proceed in any order, so a
hot word does not stall a queue.

**Store-to-load forwarding is a comparator across one short queue.** A load whose bytes are
fully covered by the newest older store or completed atomic in the same queue is answered
from that entry and never touches the bank. A partially covered load is not forwarded: it
waits and reads the array. Both are counted, and whether byte-merging logic is worth
building is decided by how large the partial count turns out to be.

**A load-reserved / store-conditional pair is a serialisation point in the queue, and nothing else.** The load-reserved opens it and the store-conditional closes it; while it is open, every other entry to that address waits behind it in the same queue, so nothing intervenes and the store-conditional completes. There is no reservation unit and no reservation table: the queue's order *is* the reservation.

**A read-modify-write atomic is one entry, performed at the bank by a small arithmetic unit beside it.** Such a unit is ordinary in real memory systems: RISC-V implementations execute their atomic operations with an arithmetic unit inside the data cache, graphics processors execute atomics in their last-level cache slices, the AMBA CHI interconnect defines far atomics performed at the home node, and PCI Express defines atomic operations completed at the target; the operation set is nine operations at two widths, so the unit is an adder, a comparator and a few logic gates. The entry reaches the head for its
address; the bank reads the word, a small arithmetic unit beside the bank applies the
operation, the bank writes the result back, all as one indivisible bank occupancy; the old
value returns to the context, which wakes. If the line is absent the bank acquires it first
and *then* performs the operation, so **the read-modify-write never spans a cache miss**.
Two consequences: a queue of `k` atomics to one word costs `k` bank read-modify-write
cycles rather than `k` memory round trips, which is the benefit wanted from atomics at the
memory; and because the word is never outside the coherent hierarchy there is no ownership
token, no hand-off chain, no bound on how long a value may stay out, no line to pin and no
write to repeat. Two atomics to different words of one line do not overlap, so they
proceed in either order and the line is locked for one access rather than for a critical
section.

**A coherence request acts on the bank, never on a queue.** A queued write is not visible
to anybody until it reaches the bank, and an access that has not been performed cannot be
snooped — so a request arriving while a store or atomic is queued is answered from the line
as the bank has it, and the queued entry re-acquires the line when its turn comes. Keeping
coherence requests out of the queues is also what keeps the wait-for graph acyclic: an
entry in a queue holds only its position; a queue's head depends only on the bank below it;
a bank depends only on its array or the fabric; a context waiting for a fill holds only its
own slot; and an incoming coherence request needs no queue entry at all, so no resource
waits on a resource above it. The mechanism this replaces failed that test in two places at
once — an unbounded waiter list, and pins that made a cache fill depend on a client
voluntarily surrendering a line.

There is one case where a coherence request waits, and it is bounded by construction: while
a bank is mid-read-modify-write on the line. The deferral is at most one bank occupancy,
because the operation never spans a miss, and it is counted along with the cycles deferred.
This is the one place where the tile's strict-priority position in the coherence protocol is
visible as a mechanism rather than a claim, and it is stated as a bounded deferral with a
counter rather than as a property asserted to be preserved. The complementary case — a
queued write whose line was taken between acquisition and its turn — re-acquires and is also
counted, so the cost of yielding is visible wherever it is paid.

**When a queue is full, the machine slows down and nothing breaks.** The chain is: a queue
with no free entry withholds credit for its bank; the delivery window keeps the entry,
neither retrying nor dropping it; a full window stops accepting from the translation path; a
stalled translation path leaves the virtual address in the context's load slot and the
context issues nothing, exactly as a context asleep on a load issues nothing; the context
wakes when the request moves. No timeout, no retry counter, no capacity fault anywhere. A
queue depth of two is a legitimate configuration that runs correctly and slowly, and a depth
sweep shows a curve rather than a cliff — which is the property the replaced mechanism did
not have, and it is a test rather than a claim.

**Request classes.** Every memory request in the tile goes through this one mechanism:
program loads, stores and atomics; the page-table reads a walk makes; the privileged
page-table writes that follow a remap; and the line-sized transfers an invocation's arrival
and departure make. The privileged page-table write is named explicitly as its own class
precisely because it is the one legal write from inside a tile to a duplicated page: it is
exempt from the refusal of §1.2, it can never arise from a kernel store, and like a walk it
gets reserved capacity so that it cannot be starved by the core's own traffic. A line-sized
transfer is not expressible as one word-sized entry, so its representation is stated
explicitly — a line payload, or a stated number of entries — and an eight-by-64 view of a
context is never allowed to become the architecture.

**Widths are derived, never written.** The physical address a queue entry carries is sized
from the machine's geometry at the maximum configuration, not fixed at a convenient number:
the carried address is 49 significant bits at a full 48-bit physical space, and the extra
bit is stripped only at the memory port, below the queues. Queue and bank counts are bounded
by the cache they sit in front of — banks must not exceed sets, and sets must divide by
banks — and that derivation is part of the configuration, not a separate assumption.

**Five counters must read exactly zero, because they are the signatures of the failure modes
this design claims to have removed:** a request delivered to a queue that does not own its
physical address; a context's two same-address accesses completing out of program order; any
memory value held above the data cache; a kernel store or atomic reaching a duplicated page;
and a request admitted without a physical address. The rest are ordinary measurements —
admissions per queue and class, forwarded loads, partial forwards, order stalls and their
cycles, the longest run of consecutive same-address entries, atomics and how many were bank
hits, bank lock cycles, coherence requests to the bank and those deferred, queued writes that
had to re-acquire, per-queue occupancy and full cycles, and context-cycles spent waiting for
a queue.

### 2.6 Why this is simpler than what it replaces

The mechanism being replaced kept an atomic's word *above* the data cache, in the tile, so
it could be handed from context to context without a cache access. One entry per held word
carried the owning context and its token, the live value and a valid bit, a count of
hand-offs, the address and size, an owned flag, a writeback-in-flight flag, a re-dirtied
flag, and a queue of parked waiters. Around it sat an index from cache line to held words, a
pin set inside the data cache, an upward notification path from the cache to its client, and
the states that existed only to manage the edges.

The fields are not the point; the interactions are. Keeping a word above the cache required
the cache to tell its client when a line was snooped and the client to hand the word back to
be patched into the line; the line to be pinned so the notification could arrive at all, and
the pin to ride on the fill so there was no window between them; a protocol for a pin that
cannot be granted, in which a set with no free way asks the client to surrender its oldest
pinned line and the fill retries; an entry that outlives its own writeback, so a write is
repeated if anything touched the word meanwhile; a bound on hand-offs so a word could not be
kept out of the hierarchy indefinitely; and rules for an owner that migrated or was killed
while its word was held. Every ordinary load and store also had to consult the table,
because a word held above the cache makes the cache's own copy stale. Two of its statistics
counted events that should not be able to happen at all — a writeback whose entry a snoop
had already taken, and a writeback whose invocation had left its slot — and they existed
because three separate structures could act on one word independently.

Counted as things that must agree with each other, the replaced mechanism had four: the
table, the cache's pins, the notification path, and the directory. The new one has **one**:
a queue entry against another queue entry in the same queue, resolved by position. Ordering,
forwarding and atomicity stop being mechanisms and become properties of an array.

Cost then grows in entries rather than in interactions, which is the scalability claim in
full:

| what is added | what it costs | what it does not cost |
|---|---|---|
| one more pipe | one more instruction and one more data request offered per cycle; the window tracks the pipe count | no new structure and no new interaction |
| one more bank | one more memory queue, one decode output, one credit counter, one arithmetic unit | nothing in the other queues: they share nothing |
| one more context | one slot pair | nothing in the ordering machinery: no waiter lists, no pins |
| one more in-flight request | one queue entry | no change to forwarding, atomicity or coherence |
| four times the banks | a split window — the same structure replicated | still no crossbar |

The honest part of the trade must be reported with the win: **bank accesses go up and fabric
traffic goes down.** A load that the old mechanism answered from a word above the cache with
no cache access at all becomes a bank hit. The counter pair that states it is bank accesses
against the tile's total requests to its last-level slice, and the claim — that moving work
from the fabric into a banked local array is the right direction — is checkable rather than
rhetorical.

### 2.7 The banked caches

An engine must fetch one instruction and perform one data access **per pipe per cycle**. An
array of that throughput is built as independent single-ported banks over disjoint sets, not
as one array with many ports, which costs area quadratically. So each of the tile's
instruction and data caches takes a bank count, the bank is the line address modulo the
count — a partition of the sets — and a count that does not divide the sets is refused
rather than rounded, because a bank holding one set more than its neighbour makes a conflict
stop meaning one thing. The count comes from the pipe count the tile is already given.

**A bank reads one line per cycle, not one request.** An array access returns a whole line,
so every request for that line in that cycle is answered by the one read. Charging per
request instead was tried and the counters rejected it immediately: on a small graph search
the instruction cache reported 290,192 conflicts against 196,627 accesses, because four
pipes fetching four instructions out of one 64-byte line collided three times. That is not a
cache, it is a missing line buffer. With one line per bank read the same run reports **zero**
instruction-cache conflicts.

Four counters sit on every cache: accesses granted a bank; conflicts, counted once per cycle
a request waits, so the figure is the delay the banking costs rather than the number of
unlucky pairs; banks busy, summed over the cycles in which any was, so its own count is the
denominator; and accesses answered by a read another request had already paid for.

### 2.8 Last-level banks are the memory's banks

One rule, in two halves: a cache's banks should be the same partition of the address space
the structure below it already uses, and a bank reads one line per cycle.

The memory device modelled is DDR5-4800 with eight bank groups of four banks — 32 banks per
rank — and the address is decoded row, bank, rank, column, channel, so the rank bit sits
*below* the bank bits and two banks differing only in rank are the same bank index at the
same position of the address. Each tile's last-level slice is banked by that same count, so
a bank of the cache and the bank of memory behind it are one partition: a request that has
picked a cache bank has already picked its memory bank. The count is one named constant read
from the device description, and a gate checks that it still equals the device's count rather
than trusting two assignments to stay in step.

The second half is **not implemented**: the memory controller should keep one low-complexity
queue per bank with no traffic crossing between them, and it keeps a single read buffer and a
single write buffer per channel instead, scheduled first-ready over per-bank device state.
Splitting them changes the memory timing every figure in §4 was taken under, and it is a
change to a third-party device model rather than to this machine's own code, so it is named
as the remaining piece rather than slipped in.

### 2.9 The memory link, and CXL as the alternative

The link between a tile's memory controller and its memory device is **configuration, not
design**. Two settings exist. The **parallel bus** is a pass-through — one DDR5-4800
subchannel per tile, no link modelled — and it is the default every workload number in §4
was taken on. The **serial link** models a x16 CXL 4.0 attachment with twelve DDR5-4800
subchannels behind an expander: much more bandwidth, and more latency in front of it. At
pin parity with one memory channel, a x16 serial attachment on a PCIe 7 generation carries
roughly 213 GB/s against roughly 32 GB/s for one DDR5-8400 channel, so about twelve
average-rate channels are needed behind it to saturate the link. The added access latency
costs host cores about a third of their performance and costs the engines little, because an
engine tolerates latency with context count.

§4.5 gives what the two settings measure. The important property is that the parallel
setting is byte-identical pass-through: every parallel column there equals the same point in
the workload sweeps exactly, which is what confirms the claim.

### 2.10 Structure and configuration

Sizes and counts are never design constants. The split below is what may change per run and
what is the design.

**Configuration** — swept, with the measurement that sets each: contexts per tile; pipes;
pipe depth; instructions in the instruction slot; branch-target-buffer entries; data-cache
banks and therefore memory queues; memory-queue depth; window width; reserved entries for a
walk and for a privileged page-table write; translation queues, their depth and the
translation latency; translation-buffer size and banking; the store slot-release rule;
instruction-cache banks; bank access latency; last-level slice size, banking and latency;
memory device and link.

**Structure** — changing one is a different design. A context has exactly two slots. A
context cannot be scheduled without its instruction in hand. Translation is virtually
indexed and happens before anything enters the data path. Memory queues are physically
indexed and each owns a disjoint fraction of the tile's physical addresses. A memory queue
serves its entries in order and is the only ordering point. An atomic is a read-modify-write
at the bank. Nothing holds memory state above the data cache. A coherence request acts on
the bank, never on a queue. Delivery is at most one request per queue per cycle,
oldest-first per destination. Backpressure is credit, and a blocked context waits in its own
slot. The branch-target buffer is shared and issues exactly one speculative fetch, which
never executes. There is no decoupled instruction-fetch engine on the tile, no data
prefetcher, no reorder buffer, no renaming, no speculative execution, and no structure that
lets a load issue before an older store's address is known.

---

## 3. What the simulator models today, and what is designed but not modelled

Everything in §1 is modelled and measured. Most of §2.1 is modelled; §2.2 through §2.5 are
designed and not yet modelled, and the mechanism they replace is what the model
contains in their place. The table states it plainly, one row per mechanism.

| mechanism | in the model today | designed, not yet modelled |
|---|---|---|
| Host: out-of-order core, two-stage front end with override predictor, wrong-path modelling, two-level TLB and walker at the cache management units, data prefetchers, memory-dependence prediction | **yes**, each with its own counters | — |
| Coherence: directory at the fabric, four tiles each with a last-level slice, memory controller and channel | **yes** | — |
| 512-bit context; the lane map including both tilings and the reserved names | **yes**, verified lane by lane in both simulators | an operation that moves the whole 512 bits (`r1` as an operand); it is an instruction-set extension, not a naming change |
| Instruction slot, shared branch-target buffer, one speculative fetch at decode, no decoupled fetch engine | **yes**, holding one instruction | the block form of the slot, and the sweep at 1, 4, 8, 16 that picks its size |
| One outstanding memory access per context; the load slot | **yes** | the relaxed store rule as a switch, with its own measurement |
| Pipes | as a **re-issue delay only**: ready contexts are picked per cycle and each issue sets a next-eligible cycle | `N × M` stage registers with named decode, address and writeback stages; the readiness and conjunction counters; the pipe-bound alternative with forwarding, whose build is gated on those counters |
| Translation | **inline and synchronous**: the address is computed and translated in the same cycle as the access | virtually-indexed translation queues, a completion rate derived from the pipe count, head-blocked counting, reserved capacity for a walk |
| Getting a translated request to its bank | nothing: a request goes straight to the cache interface | the delivery window, oldest-per-bank, with its counters and the split-window escalation |
| Ordering, forwarding, atomicity | a **word-keyed table above the data cache**, with a pin set inside the cache, an upward notification path, a byte-masked merge on every external request, a hand-off bound, and an unbounded waiter list | physically-indexed memory queues, one per bank: sequence order, forwarding inside one queue, read-modify-write at the bank, coherence requests at the bank with a bounded deferral |
| Backpressure anywhere in the data path | **none**: the model cannot run out of memory-path capacity, so it cannot show what happens when it does | credit backpressure end to end, a depth sweep that shows a curve, and the directed test that proves no deadlock |
| Tile instruction and data caches, banked one bank per pipe, a bank reading one line per cycle, four counters each | **yes** | the per-bank arithmetic unit for read-modify-write, the bank index on the request interface, and credit return |
| Last-level slice banked to the memory device's bank count, checked by a gate | **yes** | one low-complexity queue per bank at the memory controller; a single read and write buffer per channel is what exists |
| Tracking unit derived to cover every context; the control queue following it; the host counting cycles its unit is full | **yes** | — |
| Duplicate pages: a kernel store or atomic refused and counted, zero-gated, with a directed test; the host's legal fan-out to every copy | **yes** | the request class for the privileged page-table rewrite inside the new queues, with its reserved capacity |
| Migration on a foreign translation result | **yes** | the rule for a context that migrates with a store still in a queue, under the relaxed store switch only |
| Memory link: parallel pass-through and a serial CXL attachment, as configuration | **yes** | a workload that can saturate the serial link; the x32 variant |
| Data prefetching into the load slot | no | deliberately undesigned; the slot is left free for it |

Three further pieces are worth naming as absent on purpose rather than missing. There is no
reorder buffer, renaming or speculative execution on an engine, and no structure that lets a
load issue before an older store's address is resolved — the queues hold only requests whose
physical address is already known, issue nothing on a prediction and never replay. There is no per-context branch predictor: the target buffer is one shared structure indexed by program counter that any context consults for its own next instruction; contexts do not share an instruction stream. And there is no data prefetcher.

---

## 4. What the workloads measure

### 4.1 How a performance number is taken

Two of the numbers below come from programs run uninterrupted to their own end; most come
from **sampled** simulation, and the distinction matters.

A cycle-accurate simulator models a clock edge at a time. It is the only way to get a cycle
count that means anything, and it is slow: this machine runs a few hundred thousand
simulated cycles per second of wall clock, so a program of a hundred billion cycles cannot
be run at all. The answer is a **functional** simulator that computes what the program
computes and models no time whatsoever; it runs the whole program in minutes and, at a fixed
interval, writes a **whole-program image** — registers and the whole address space,
everything needed to restart from that exact instruction. The cycle-accurate model starts
from an image, runs a **warm-up** that is measured by nothing, then a **region of interest**
whose cycles are the difference between two snapshots of every counter, then a teardown. One
region goes in each interval, and the regions are combined into an estimate for the whole
program with an interval that says how much the regions disagreed. Two configurations are
compared by running **the same regions** on each.

The coordinate that places a region is the program's own **work** count, not an instruction
count, because two builds of one program retire different instructions for the same work.
Sampled windows across two arms therefore cover the same computation and pair region for
region, which is also what says *where* in the program an offload wins.

A second coordinate now exists beside the first: **counted instructions**, the instructions
retired outside a program's wait loops. It is the only axis available where the work axis
does not apply — one arm's absolute cost on its own, a program with no work counter, a phase
the work counter does not advance in, or a comparison between two *different* programs — and
it was validated by running four programs uninterrupted to their ends and dividing the
estimate by the truth.

| program | what it computes | uninterrupted cycles | counted instructions |
|---|---|---|---|
| shuffled sum, offloaded | 524,288 chains of four dependent loads, 64 chains an invocation | 9,955,355 | 187,860 |
| shuffled sum, host alone | the same 524,288 chains | 63,174,430 | 37,272,417 |
| graph search, offloaded | a traversal of 65,536 vertices | 21,823,805 | 23,032,314 |
| graph search, host alone | the same traversal | 23,510,293 | 27,653,831 |

Against those four answers the estimator reaches 0.98 of the truth on the shuffled sum's
speedup (6.2237 estimated against 6.3458 measured, 95 % interval [5.9915, 6.4559], which
contains the truth) and 0.98 on the graph search's (1.0594 against 1.0773). The same
validation also found the warm-up that each arm needs, and one of the four needs **ten
intervals out of twenty** — half its program — because the pure-host shuffled sum walks a
permuted structure the size of its whole working set and is half again as slow until the
shared cache holds it. That is a property of validating at the smallest size that runs in
minutes: a warm-up is a fixed number of cache misses, and the program is what grows.

Two limits are stated rather than hidden. The counted-instruction axis is coarse where an
arm offloads almost everything — the offloaded shuffled sum's whole program is 187,853
counted instructions, so one interval of a twenty-image set is 9,393 of them and one turn of
the tracking unit is 6,912, which is why the design that reaches the truth there uses six
regions rather than twenty. And a warm-up measured at one phase does not transfer to
another.

### 4.2 The graph search

The program searches an undirected random graph of 64-byte vertex records with mean degree
eight, choosing at each level between scanning the frontier forward and scanning unvisited
vertices backward. The step the offloaded build gives to the tiles is a **vertex range**: an
invocation owns a contiguous block of vertices and generates its own work from its two
bounds.

The first table is one step offloaded, each size run to its own end except the largest, with
the earlier campaign's figure beside it for continuity:

| working set | host, ms | offloaded, ms | speedup | earlier campaign |
|---|---|---|---|---|
| 0.25 MiB | 0.1042 | 0.1398 | 0.75x | 0.73x |
| 1.00 MiB | 0.4669 | 0.5197 | 0.90x | 0.89x |
| 4.00 MiB | 1.7231 | 0.5636 | 3.06x | 3.03x |
| 16.00 MiB | 17.6775 | 5.4987 | 3.21x | 3.68x |
| 32.00 MiB | 30.3138 | 6.6652 | 4.55x | 6.50x |
| 64.00 MiB, sampled | 222.45 est | 148.40 est | 1.499x | 1.490x |

Below about 4 MiB the offload does not pay, because the host's own caches hold the graph.
The 64 MiB row is a whole-program ratio from sampled windows rather than a single step's
speedup, so it is not comparable with the five above it.

The program was then redesigned so that every level runs on the engines, and three costs
that had previously been charged to offloading were removed — all three in the *program*,
none in the machine: the host no longer empties a shared record between levels, a claim is
recorded where the claimed vertex lives rather than in one place, and a level too small to
be worth a fork wave is not offloaded at all. Traversal time in host cycles, summed over the
levels that settle at least one vertex, each level measured in its own window:

| program | 256 KiB | 1 MiB | 4 MiB | 16 MiB | 32 MiB | 64 MiB |
|---|---|---|---|---|---|---|
| host alone, the baseline | 361,101 | 1,595,518 | 6,211,294 | 52,892,031 | 95,991,586 | 263,095,327 |
| one step on the engines | 1,286,926 | 1,476,781 | 1,761,110 | 7,422,814 | 21,950,151 | 45,520,182 |
| every level on the engines, redesigned | 1,533,140 | 1,771,073 | 1,767,127 | 7,138,470 | 14,243,477 | 35,970,469 |
| redesigned, small levels placed by count | 1,286,441 | 1,473,376 | 1,608,669 | 7,036,950 | 14,005,272 | 35,712,189 |
| speedup of the last row over the baseline | 0.28 | 1.08 | **3.86** | **7.52** | **6.85** | **7.37** |

The redesigned program is the fastest at every size, and from 4 MiB up it is 1.10, 1.06,
1.57 and 1.27 times the one-step build. Against the traversal on the host alone it reaches
**7.37 times** at the largest size measured. At the two smallest sizes both offloaded builds
lose to the host and neither should be used.

Where the gain is, at 32 MiB, is one level: the largest top-down level costs 3.42 million
cycles in the redesigned program where it costs 11.05 million on the host. And the largest
single item turned out not to be a property of offloading at all — half the travel previously
charged to a migrating claim was the program's own doing, because a claimer had already moved
to the vertex it was claiming and then moved again to record the claim somewhere else.
Migrations per edge fell from 0.045 to 0.019 at 16 MiB and from 0.145 to 0.093 at 32 MiB, and
a load on an engine that waited 98.1 cycles now waits 20.4 — *below* the one-step build's 21.5
— while the engines sustain 3.30 instructions per cycle of their four issue slots against
2.86.

The general rule that came out of it is worth more than the numbers: **a level's record must
be owned by the same unit of work that owns the vertices.** What made the earlier arrangement
slow was not that the engines are slow but that one array was placed without asking who would
write it.

One cost is left and it cannot move to an engine: between two levels the host turns the record
the last level wrote into the tables the next one probes, and that pass is **10.2 %** of the
traversal at 4 MiB against **1.8 %** for issuing the next level's invocations. It is a scan
because the only account of which vertices a level settled is a bitmap, and a bitmap must be
read in full to be searched; it is proportional to the graph rather than to the frontier, so a
level that settles nine vertices out of sixty-five thousand still costs 6,678 cycles to
summarise. The fix is algorithmic — give the level a compacted account as well, a per-owner
list of vertex numbers appended by the same unit of work that sets the bit, and keep both,
choosing between them by the count the program already has.

### 4.3 The shuffled sum

The program sums a large array through a permutation, so consecutive iterations touch
unrelated cache lines and the access stream is a dependent chase rather than a stream. Two
memory formulations are swept: **grain**, where each region sits on a single tile, and
**striped**, where one region is spread a grain at a time across all four. `K` is how many
chain elements one invocation walks.

| formulation | K | 256 KiB | 1 MiB | 4 MiB | 16 MiB | 32 MiB | 64 MiB |
|---|---|---|---|---|---|---|---|
| grain | 64 | 2.02x | 2.14x | 4.56x | 4.56x | 6.85x | 7.33x |
| grain | 256 | 0.82x | 2.14x | 4.54x | 4.57x | 6.87x | 7.33x |
| striped | 64 | 1.22x | 1.32x | 4.64x | 6.36x | **10.11x** | **10.14x** |
| striped | 256 | 1.18x | 1.32x | 4.65x | 6.37x | 10.12x | 10.12x |

All 72 runs check their own answer, and every checksum is byte-identical to the same run's in
the previous campaign; nothing in the sweep moved by more than 1.2 per cent. Striping wins at
and above 4 MiB because it uses all four memory channels; the invocation's chain length barely
matters, which says the program is waiting on memory and not on dispatch.

### 4.4 The chained hash table

The table is 65,536 buckets of separately chained entries. `P0` to `P4` are load points —
0.125, 0.333, 2.333, 10.333 and 21 entries per bucket, with longest chains of 3, 5, 11, 26 and
41 — and `B` is how many operations one invocation batches. **sep** runs lookups and inserts
in separate passes; **int** interleaves them.

| load point | phase | B | host, ms | offloaded, ms | speedup |
|---|---|---|---|---|---|
| P0 | sep | 128 | 0.179 | 0.279 | 0.64x |
| P0 | int | 32 | 0.341 | 0.404 | 0.85x |
| P1 | sep | 128 | 0.449 | 0.653 | 0.69x |
| P2 | sep | 128 | 7.302 | 4.123 | 1.77x |
| P2 | int | 128 | 9.636 | 5.461 | 1.76x |
| P3 | sep | 128 | 175.562 | 19.664 | 8.93x |
| P3 | int | 128 | 165.019 | 24.997 | 6.60x |
| P4 | sep | 128 | 1326.890 sampled | 52.187 | **25.43x** |
| P4 | int | 128 | 1047.814 sampled | 55.682 | 18.82x |

The shape is the result: below a load factor of about two the offload does not pay, and above
it the gain grows with **chain length** rather than with the size of the table. A short chain
is one dependent load the host's own caches can hold; a chain of 41 is a walk that belongs
beside the memory.

### 4.5 The memory link

The two link settings are compared at points chosen to be bandwidth-bound and latency-bound.

| workload | point | working set | parallel bus | serial link | change |
|---|---|---|---|---|---|
| shuffled sum | striped, K = 64 | 64 MiB | 10.14x | **19.84x** | +95.6 % |
| shuffled sum | striped, K = 64 | 32 MiB | 10.11x | **19.50x** | +92.8 % |
| graph search | one step offloaded | 32 MiB | 4.55x | 6.56x | +44.1 % |
| graph search | one step offloaded | 16 MiB | 3.21x | 3.34x | +3.9 % |
| hash table | P3, sep, B = 128 | 16 MiB | 8.93x | 8.97x | +0.5 % |

A serial attachment with twelve channels behind it nearly doubles the shuffled sum's speedup,
because that workload is bandwidth-bound and the host pays the added latency while the engines
do not. It changes the hash table by half a per cent, because that workload is latency-bound
on both arms. The engines' insensitivity to link latency is the property being demonstrated;
the host's sensitivity is the price.

### 4.6 The three changes of this round, measured

Three changes to the machine are recent enough that their before-and-after belongs here. Each
landed with a directed test, its own counters and a measurement on the workloads that exercise
it, and the result is that two of the three are free and one corrects a configuration error.

The tracking unit now covers every context, and on the graph search at 16 MiB that changes
nothing measurable: 343.08 cycles per vertex settled before against 344.30 after, each inside
the other's interval. The counters say why — the unit's occupancy peaked at 24 entries of the
256 it had, and no invocation was ever refused. On the shuffled sum at 32 MiB the unit
**was** full for 5,552,276 of 46,333,871 host cycles, and enlarging it still changed nothing:
the two runs are bit-identical window by window, because the program holds 256 invocations
outstanding and then collects one before starting the next, so it never asks for a 257th. A
full unit costs nothing unless an invocation is attempted while it is full, and none ever was.
Enlarging it was still right — a machine that cannot address half its own contexts is a
configuration error whatever today's program does — and a program reaches past the old number
only by being rebuilt, or by asking the machine at run time how deep its unit is, which is
what `FORKQ` is for.

Banking the tile's instruction and data caches is free to measurement on both workloads:
342.93 cycles per vertex settled against 344.30 unbanked on the graph search, and 111.709
cycles per sum against 111.740 on the shuffled sum — each far inside its own interval and of
the wrong sign to be a cost. What the banking does is visible in its own counters, summed over
the measured regions across all four tiles:

| point | I-cache accesses | I-cache conflicts | answered by another's read | D-cache accesses | D-cache conflicts | data banks busy, busiest cycle |
|---|---|---|---|---|---|---|
| graph search, 16 MiB | 19,642,983 | 0 | 1,504,765 | 6,841,233 | 59,201 | 4 of 4 |
| shuffled sum, 32 MiB | 8,783,606 | 0 | 2,789,689 | 4,848,551 | 263,548 | 4 of 4 |

The instruction cache never conflicts on either workload, because a bank read returns a line
and four pipes fetching out of one line share it; between a seventh and a third of its
accesses ride along on another's read. The data cache conflicts on 0.9 % of accesses in the
search and 5.4 % in the shuffled sum, and in both its busiest cycles use all four banks, which
is the throughput it was sized for.

The last-level slice's banks are now checked against the memory device's in both
configurations rather than assumed: 32 banks per rank, 32 slice banks, one partition. Measured
on the four-tile machine running the small search, the slice reports 118 bank conflicts and the
memory behind it reports an access spread across banks of 1.25 with **no bank never accessed**,
which is the statement that the two partitions are one and that the traffic uses all of it.

A defect the new counters found is worth recording because of what it says about reading
counters. The tracking unit keeps a count of entries holding finished work the host has not
collected, and that count was wrapping to 2^64−1 on every run started from an image — which is
every sampled region of every measurement in this project. An image's returned entries were
being installed by assigning state directly instead of through the one function that maintains
the count, so a restored entry was in the returned state and the count did not know it, and the
first collection decremented a count that had never been incremented. Nothing in the machine's
behaviour reads that count, so no cycle figure anywhere is affected; the companion counter for
host idleness happened to read almost the same number before and after, because on that program
there genuinely is finished work waiting nearly all the time. A conclusion drawn from it was not
wrong — it just was not being measured. The fix routes the restore through the state transition,
and a directed test resumes from an image and requires the count to have stayed inside the
unit's capacity, which a wrapped count cannot pass by accident.

### 4.7 The open question: dispatch and utilisation

This is where the machine now stands, and it is the thing the next round is about.

**The engines are nearly empty.** On the graph search at 16 MiB, measured over the sampled
regions of the whole program, the mean number of live contexts per engine, split by state, is:

| engine | live contexts of 128 | executing | runnable, not issuing | asleep on a load |
|---|---|---|---|---|
| tile 0 | 2.86 | 0.33 | 1.60 | 0.87 |
| tile 1 | 2.40 | 0.27 | 1.23 | 0.85 |
| tile 2 | 0.65 | 0.08 | 0.27 | 0.28 |
| tile 3 | 0.65 | 0.08 | 0.27 | 0.28 |

The three states exclude one another and sum to the live count. Six and a half contexts are
live across the whole machine, of the 512 that exist. On the shuffled sum at 32 MiB the
engines hold 5.1 to 6.0 live contexts each of 128, nearly three quarters of them asleep on a
load. Those two averages are taken over the *whole* program, including the phases in which
the host is doing its own work; averaged over only the windows in which a level is actually
running on the engines, the graph search holds 44 to 51 live contexts per engine of 128, so
the engines do fill when work is present. Both readings are true and they are different
quantities — the first says how much of the program's time the engines are used at all, the
second says how well they are used while they are used.

**An entry in the tracking unit is not a live context.** On the same graph-search measurement
the unit holds 23.3 entries while the machine holds 6.5 live contexts: roughly seventeen of
those entries are invocations that have already finished and are waiting for the host to
collect them. On the shuffled sum the unit holds 31.7. The redesigned search at 32 MiB holds
208 of 256 entries outstanding with a finished result waiting in 58.9 % of cycles, and that is
*not* by itself a host bottleneck: the number of vertex ranges equals the unit's entry count at
every size from 16 MiB up, so every range of a level is started before any of them returns and
an uncollected entry blocks no start. What it does cost is the tail of a level, because the
level ends when the host has collected the last of its 256 — a few thousand cycles a level,
and under thirty thousand over a traversal.

**The counter that does point at the host is idleness with work waiting.** On the graph search
the host ran no offload instruction while finished work was waiting for 93.8 % of ticks. Put
beside the engines' 6.5 live contexts of 512, the reading is that on this program the machine
is limited by the rate at which the host starts and collects work, not by the engines'
capacity to run it — and that rate has two components worth separating: the per-invocation
cost on the host of writing a context, starting it, testing for completion and reading the
result back, and the rule by which a start instruction may issue at all.

The general statement of the problem, then: **one host core keeps far fewer contexts alive
than the machine has places for.** The architecture is defined; what is not yet known is how
to keep it full. Three directions follow from the counters rather than from argument — longer
lived invocations, so that a context stays live instead of being created and destroyed per
level; an instruction layout in the program that keeps starts and collections flowing instead
of batching them into waves with a barrier at each end; and a cheaper per-invocation path on
the host. None of the three is a change to the tile core, which is why §5 treats utilisation
and the tile's internals as separate pieces of work.

---

## 5. What comes next

The majority of the work is not defining the architecture. It is using it, and finding where
the bottlenecks emerge — in hardware or in software — and overcoming them. Five pieces follow,
in the order their dependencies allow.

**1. Instrument the pipes on the model that exists.** The readiness and conjunction counters
of §2.2 need no new mechanism: they turn two existing counts that measure scheduler
examinations into context-cycle censuses, and add the conjunction that decides the pipe
question. They come first because they are the only part of the tile design that can run today
and because they decide what the pipe model must support. One of them — the histogram of
load-free runs — is validated against a kernel whose memory accesses occur at known fixed
intervals, so the instrument is checked before it is used to decide anything.

**2. Build the pipe stages, the default rule first.** `N × M` stage registers, the three named
stages, and the default issue rule. A directed test asserts that no two stage registers in one
cycle hold the same context, which is the rule that removes forwarding and is checkable rather
than assumed. The pipe-bound alternative is built only if the conjunction counter says there
were slots to recover; if it is built, it stays behind a parameter beside the default so the
comparison can be re-run.

**3. Build the memory path once, and delete the mechanism it replaces in the same change.**
Translation queues with a completion rate derived from the pipe count and reserved capacity for
a walk; the delivery window; the per-bank memory queues with ordering, forwarding,
read-modify-write at the bank and coherence requests at the bank. The word-keyed table above
the data cache goes in the same change, with its directed tests rewritten to assert the new
counters and the absence of the old ones, so that no interval exists in which both mechanisms
are present. The existing tests keep their programs and their assertions: the kernel that
writes different words of one line must still see every word survive, the kernel that loads a
word it has just atomically updated must still see its own update, and the regression in which
a host write between phases must be seen by the engines must still hold — each now a
consequence of where the ordering point sits rather than of three mechanisms cooperating.

**4. Sweep the questions the design deliberately left open,** each as a configuration sweep
with every point correct rather than by removing a mechanism: the instruction slot at 1, 4, 8
and 16 instructions; the window width from the pipe count up to the bank count, which walks
continuously from the accepted mechanism to the crossbar and prices the difference; queue depth
from 2 to 32, which must show a curve; bank count at 8 and 16; and the store slot-release rule.
A null result is a result and is explained with counters — which constraint actually bound, not
prose.

**5. Attack utilisation, which is where the measured machine is losing most.** The engines hold
single-digit live contexts of 512 over whole programs while the host is idle with finished work
waiting for 94 % of ticks. That is not a tile-core problem and it will not be fixed by anything
in §2. The work is: report live contexts per engine by state and the host's per-invocation cost
split into its parts, never from tracking-unit occupancy; price long-lived invocations that stay
resident across phases against the fork-wave-per-phase shape the programs use now; examine the
rule by which a start instruction may issue, which currently halves the rate at which the host
can feed the engines; and carry the graph search's remaining algorithmic cost — the
whole-graph scan between levels that is 10 % of the traversal — by giving a level a compacted
account of what it settled beside the bitmap.

Two pieces of the machine are named as remaining rather than worked around: one low-complexity
queue per bank at the memory controller, which is the second half of the bank-alignment rule
and changes the memory timing every figure here was taken under; and an operation that moves
the whole 512-bit context, which would let `r1` be an operand and is an instruction-set
extension rather than a naming change, with a legality equation to re-derive so that whatever
stays reserved still traps.

Three standing method rules apply to all of it, and they are what make the results above
comparable. Every performance number comes from the sampler, with windows covering the same
program *work* across arms rather than the same instruction count, every point of a sweep
launched together rather than serially, and no end-to-end run of about an hour. A new feature
is proven with its own counters on a workload that exercises it, and a surprising null effect
is explained with counters rather than accepted. And a fix keeps the mechanism it touches: a
fix that serialises — a hold, a pin, a drain — is provisional until the real mechanism replaces
it, and it carries a before-and-after on the workloads that exercise it.

---

## 6. Where the numbers in this document come from

Every figure in §4 is one of three kinds, and each is identified where it appears: a program
run uninterrupted to its own end, which is used for correctness gates and for validating the
sampler; an estimate from sampled windows with its interval; or a counter summed over those
windows. Every run checks its own answer and prints a pass line, and a run that does not is not
a measurement. The two campaigns whose figures appear side by side in §4.2 share every
component below the coherence fabric as the same binaries, and the check that says so is that
every answer digest is byte-identical between them.

Sizes and counts in this document are configuration. Where a default is given it is the value
the measurements were taken at, not a constraint on the design.
