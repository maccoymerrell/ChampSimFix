# NMFC — Near-Memory Function Cores: Architecture Reference

NMFC is a memory system that runs work where the data already is. It takes an ordinary
multicore hierarchy and moves one thing: the address partition, from below the last-level
cache to the fabric at the L2↔LLC boundary, so that an LLC slice, a memory controller and a
DRAM channel form one vertical stack — a **tile**. On top of every tile sits a **function
core**: a barrel-scheduled, non-speculative core that time-multiplexes a large number of
stackless invocations, each of which is nothing but a program counter and a 512-bit register
file. A host core offloads an invocation with one instruction and collects it with another;
when an invocation reaches for an address its tile does not own, the invocation moves to the
owning tile rather than the data moving to it. This reference specifies the architecture:
the memory system and the tile (§2), the interface a compiler and an operating system
program against (§3), the mechanisms end to end (§4), and how to use the machine well (§5).

**Scope and conventions.** This document states mechanism and constraint. Where a magnitude
is needed, it names the constraint that binds it — `C ≥ W(Dp + L/I)`, `mode_bit ≥ log2 G +
log2 N` — and not a value. Every configuration value in the machine lives in §6 and nowhere
else; the values there are what one simulation is set to, not properties of the
architecture. Three quantities are fixed by the design rather than configured, and appear
freely throughout: a context is **512 bits**, the block it is transmitted in is **64 bytes**,
and a migration is **72 bytes**. `N`, `W`, `Dp`, `C`, `G` and `block` are symbols that §6 gives
values to. `L` and `I` are not: they are the memory latency a tile sees and the instructions a
context issues between misses, both measured on a workload rather than set. Terms of art are set
in bold at their definition and collected in the Glossary.

---

## 1. Overview

#### The workload this machine is built for

Many supercompute workloads are richly parallel across threads and almost perfectly serial
within any one of them. Graph traversal and pointer chasing are the canonical cases: each
step's address is the previous step's result, so a thread issues one memory reference, waits
out the full latency, and only then knows what to issue next. At a fixed thread count the
memory-level parallelism of such a program sits near 1, and the machine idles twice over —
the channels are never busy enough to be the limit, and the arithmetic units have nothing to
do while the address chain resolves. Throughput-oriented accelerators do not repair this,
because the individual kernels are themselves serial.

NMFC addresses that shape directly. It moves the work to the point in the memory system
where the data already is, and runs enough independent work at each of those points to keep
the channel underneath it saturated.

#### The one change to the memory system

Take a completely ordinary modern multicore memory system. Each host core has a private L1I,
a private L1D and a private L2. A last-level cache is shared across cores. Below it are
memory controllers and DRAM channels. The **fabric** is the L2-to-LLC interconnect — the
network an L2 miss crosses to reach the cache that owns its address — and coherence is
enforced at exactly that boundary. Nothing in that description is NMFC.

NMFC changes exactly one thing *in the memory system*: **the address partition moves to the
fabric.** A conventional machine routes an L2 miss into a shared LLC and slices only
afterwards, on the way to the controllers. NMFC slices **vertically**: the fabric picks the
destination, and the LLC slice, memory controller and DRAM channel below that destination
are one stack owned by it. The shape of the hierarchy does not change. There is one fabric.
A host core reaches memory by the ordinary path — L1, L2, fabric, the slice that owns the
address — and no part of that path is NMFC-specific.

#### What is added around that change

| | unchanged from a conventional machine | added by NMFC |
|---|---|---|
| **hierarchy** | private L1I/L1D/L2 per host core; a shared LLC across cores; memory controllers; DRAM channels; one fabric at the L2↔LLC boundary; coherence enforced there and nowhere else | the address partition is taken **at** the fabric, so LLC slice, controller and channel form one vertical stack |
| **cores** | out-of-order host cores, with their ordinary path to memory | a **function core** at the top of every vertical stack, on the slice's side of the fabric interface |
| **host structures** | load/store queue, MMU, caches | a host-side **function tracking unit**, parallel to the load/store queue and not a reuse of it (§3.1) |
| **instructions** | the base ISA | **fourteen** instructions: seven host-side offloads, three function-side, two host-side context-register lane moves, plus `RESUME` and `KILL` (§3.1) |

#### The tile

A **tile** is the vertical stack, and its parts are one object under several names:

> **tile = DRAM channel = LLC slice = memory controller = function core.**
> One object, counted by `N`.

That identity is not a modelling convenience. It is why "one copy per channel" and "one copy
per tile" are the same sentence, why every tile can resolve a translation without leaving
home, and why `N` is a single number throughout.

A second kind of tile exists and is never interchangeable with the first. A **compute tile**
is a host core plus its private caches and its fabric port; no function core, no LLC slice,
no memory controller and no DRAM channel lives there. Throughout this document, *tile*
unqualified means the memory tile; where a sentence means the host side it says *compute
tile* or *host core*. Nothing belonging to a memory tile may be moved to the other side of
the fabric: a design that places a tile's caches on a network has moved the core off the
tile, whatever its diagram claims.

#### What an invocation, a context and an offload are

An **invocation** is one unit of work running on a function core. Its entire architectural
state is a program counter plus a 512-bit register file — 64 bytes, one cache block — and
there is no stack. Those 512 bits are the invocation's **context**: the whole of its live
state, transmitted as one block when the invocation is created, returned as one block when
it completes, and carried as one block when it moves between tiles. Creating an invocation
or tearing one down is a slot write, which is what makes an arbitrary number of them
affordable. The context is 512 bits, **bit-packed**; it is not a fixed number of registers,
and how the bits are named is §3.2's subject.

An **offload** is an instruction. A host core *forks* an invocation by naming an entry
program counter and a 512-bit context; the invocation runs on some tile's function core; a
*join* collects the 512 bits back. Fork and join are separate instructions, and that
separation is the whole reason one host core can hold many invocations in flight at once
rather than a reorder buffer's worth. The **function tracking unit** (**FTU**), a host-side
structure specified in §3.1, holds each outstanding invocation from its fork to its join.

#### The two invocation loops

Both loops are first-class and both are part of the design.

- **Register-returning.** The invocation computes a result, the result comes home in the
  register file, and `JOIN` deposits it in the caller's context. Concurrency is bounded by
  how many results the caller holds un-joined.
- **Memory-committing.** Nothing is returned. The invocation stores a block, coherence
  publishes it at the L2↔LLC boundary, and the host reads it later with an ordinary load.
  This loop needs no instruction of its own. Concurrency is bounded by how many entries the
  FTU holds.

The rest of this document is organised as the machine is: §2 describes the architecture; §3
describes the interface a compiler and an operating system program against; §4 walks the
mechanisms end to end; §5 states how to use the machine well; §6 collects every
configuration value in one table.

---

## 2. Architecture

### 2.1 The memory system and the fabric

#### The vertical stack

Below the fabric, the machine is `N` independent vertical stacks. Each stack is one tile,
and each tile holds, from the fabric downward: a fabric **port**; a **function core** with
its own private instruction cache and data cache; the tile's memory management unit, with a
translation lookaside buffer and a hardware page-table walker; one **LLC slice**, banked in
alignment with the DRAM device's banks; one memory controller; and one DRAM channel.

The function core sits at the top of that stack, beside the fabric interface into the slice
and **on the slice's side of it** — never across it. The node inside the tile that faces the
fabric is a port, not a directory; there is one directory, and it is in the fabric (§2.5).

#### Which paths cross the fabric, and which cannot

Where the function core sits decides this, and it decides it structurally rather than by
exemption. The core's memory paths terminate at its own slice because there is no fabric
between a core and the slice it sits on. Its control paths — the ones that move work between
tiles — must cross.

| path | crosses the fabric | why |
|---|---|---|
| function-core instruction fetch | no | the I-cache misses into this tile's own slice |
| function-core load / store | no | the D-cache misses into this tile's own slice |
| function-core page-table walk | no | the walker's references go to this tile's own slice, on a dedicated upper port of it |
| host core's reference to any address | yes | the ordinary path: L1 → L2 → fabric → the slice owning the address |
| an invocation arriving at a tile | yes | it comes from a host core's fork |
| a context migrating between tiles | yes | it leaves one tile's function core and arrives at another's |
| an invocation's completion returning to its host | yes | it is delivered to the host-side FTU |

Memory never crosses; work does.

#### One fabric

There is exactly one fabric, and it carries three classes of message: **COHERENCE**, the
traffic of the directory at the L2↔LLC boundary; **MIGRATION**, a context leaving one tile for
another; and **FILL**, the LLC and DRAM line traffic. Those three are the *data* classes, and
§2.5 states how they are arbitrated. The control traffic the instruction set generates — the
invocation packet a fork sends, the completion an end sends, a resume packet, a kill packet — is
not a fourth data class; what binds it is that it may not displace COHERENCE and may not be
queued behind a congested destination's fills. Where control traffic is queued is left to an
implementation.

A second interconnect reserved for NMFC traffic is forbidden. The three classes share links and
share the byte budget, and that sharing is the whole reason **subsumption** — that a migration
replaces a line fill rather than adding to it (§2.6) — is a measurable claim rather than an
assertion.

#### The function core's caches

The function core has a private instruction cache and a private data cache. They are
**separate and never unified**: instruction and data are fundamentally different working
sets here — small instruction footprints with high locality against a data stream with
almost none — and cached together they conflict. Both are **heavily banked and sliced by
address**, because the bandwidth available from each structure is what decides whether the
channel below stays fed. The instruction cache is a real cache: instructions live in memory
and are fetched like any other data, and a very high hit rate is an expected outcome of small
replicated code, never an assumption.

#### Where capacity belongs

Capacity belongs in the LLC slice, not in the function core's data cache. Capacity and
concurrency are substitutes rather than complements, so widening the tile's outstanding-miss
queues buys a great deal at a small data cache and very little at a large one; growth in the
data cache multiplies its own hit rate while leaving DRAM traffic untouched, because the slice
serves those hits anyway, and only a data cache large enough to capture what would otherwise
go to DRAM changes the channel's load — which is the slice under another name.

**The level does not matter, the total does**, and that is what settles where the capacity is
spent rather than how much of it there is: the same total capacity at the function core's data
cache and in the slice perform within noise of each other, so nothing is bought by moving it up.
Two things are lost by moving it up. Putting the capacity at the data cache drives the slice's
hit rate to nearly zero, which is the slice paid for and not used; and a large per-tile array
that sits behind a private cache's latency and porting is a harder structure to build than a
memory-side cache on the channel, which is a role real machines already give the last level. So
the rule is a rule about placement, and the performance argument is what says placement is free
to be decided on other grounds.

So the tile wants, in this order: **a large LLC slice, a small banked data cache, and enough
outstanding-miss concurrency to keep the channel fed** — with the concurrency obtained by
banking rather than by a monolithic queue that does not exist in silicon.

#### The grain, `G`

The **grain**, written `G`, is the unit in which physical memory is allocated and assigned to
tiles. **A grain** — the object — is one `G`-sized, `G`-aligned chunk of physical memory;
grain *g* is the chunk at physical offset `g × G`.

`G` is derived from the DRAM organisation:

```
G = row_bytes_per_channel × banks_per_channel × total_channels
```

where `banks_per_channel` is the product of **every** organisation level between the channel
and the row — ranks and bank groups included, whatever the device calls them — and
`total_channels` is the count of **device** channels: the channels one memory controller
instance declares, times `N`. Each instance declares exactly one channel, so `total_channels`
equals `N`, and that one-channel rule is what makes tile, channel, controller and slice a
single object.

`G` is therefore **not a free parameter and not a constant**. It moves when the memory
configuration moves: a different device, a different rank count or a different tile count
gives a different `G`. Any bank, rank, bank-group, row, column and channel count that
addresses the physical space must work, and no count is fixed by the design. A tool, a
configuration or a test that hardcodes a grain size is wrong at the next device.

#### `G` is three things at once

The grain is the unit of physical allocation and tile assignment, the unit at which a page's
mapping mode may be tagged, and the NMFC data page size — all three at once, and not by
coincidence. The middle one forces the other two to agree with it.

The machine has two mappings from physical address to DRAM cell: **block-interleaved**, which
spreads consecutive blocks across every channel the way a conventional machine does, and
**grain-partitioned**, which places a whole grain on one tile. §2.3 says which pages get
which and §2.4 says how the choice is carried.

Tagging a mapping mode per unit is only sound when the tagged unit owns **whole DRAM rows**,
so that two units in different modes can never contend for the same bank and column slots. At
the grain, and only at the grain, the two modes consume identical capacity from disjoint
resources:

| mode | what one grain occupies | capacity |
|---|---|---|
| block-interleaved | one row index, across all banks of all channels | `G` |
| grain-partitioned | as many consecutive rows as there are channels, across all banks of **one** channel | `G` |

That equality is what licenses tagging the mode per unit instead of partitioning the address
space in two, and it is why the allocation unit, the tagging unit and the NMFC page size are
one number rather than three.

---

### 2.2 Tiles and the function core

#### What the function core is

The function core is **multi-context, in-order per context, and non-speculative**. It is
designed for this role rather than adapted from the host core: there is no reorder buffer, no
rename, no load/store queue, no branch predictor in the execution path, and no speculative
execution. **Nothing on a function core ever issues on a prediction.**

It is a component of a memory tile — one per tile — and not a coprocessor attached to a host
core. The fabric carries the invocation to it, not the core to the host: being reached over
the fabric describes the work, never the core's position. A function core that shared a host
core's memory would make locality free, and locality is the quantity the whole design exists
to measure.

#### Barrel execution

The core is a **barrel core**, and one rule describes the whole of it:

> One context issues one instruction, then yields. `W` pipes each serve a *different*
> context in the same cycle, never the same context twice.

`W` is the **pipe width**: the number of duplicate pipes. Because at most one instruction per
context is ever in flight, **no two instructions in the pipe can be dependent** — and that
single property is what removes the machinery. There is nothing to forward, nothing to
interlock, no hazard to detect, and nothing to squash.

`Dp` is the **pipe depth**, and it is the re-issue delay: a context cannot present its next
instruction until its previous one has left the pipe. Context-conditional forwarding inside
each pipe would relax `Dp`; it is not part of the design.

The context count `C` is derived from those two and from the memory system, not chosen:

```
C  ≥  W × ( Dp + L / I )
```

where `L` is the memory latency a tile sees and `I` is the instructions a context issues
between misses. Neither is a configured value: `L` follows from the device and the load on it,
`I` from the compiled function and its input, and both are measured on a run rather than set,
which is why they have no row in §6 while `W`, `Dp` and `C` do. The `W × Dp` term is the floor
that keeps the pipes fed with no memory system at all; everything above it is latency tolerance.
`C` is a per-tile quantity: it counts the **context slots** on one function core.

#### Sleeping on a load

A context that issues a load **sleeps**, and wakes when the value arrives. That is the
mechanism that turns a purely serial pointer chase into a saturated channel: many serial
kernels, time-multiplexed onto one channel, keep it busy even though not one of them could.

#### Context state

A context slot is in exactly one of five states. The list is closed: every cycle a slot spends
is charged to one of these five, which is what makes §5.4's per-state report total.

| state | meaning | entered on | left on |
|---|---|---|---|
| `FREE` | the slot holds no context | a context is returned or discarded; a context departs on a migration | a context is created in it |
| `READY` | runnable, waiting for an issue slot | creation (an arriving fork or migration); yield; a fill or an atomic release; a `RESUME` | issue selection; departure on a migration |
| `RUNNING` | issuing in the barrel | issue selection | yield — every cycle |
| `BLOCKED` | asleep, the slot held | the one outstanding load; a full atomic table; a recoverable fault, across which the context **parks** (§3.5) | the fill arrives; the atomic becomes available; the `RESUME` that follows the kernel's handling of a fault |
| `DONE` | finished, result not yet handed off | `END` | the result reaches the FTU, or is discarded |

**Parking is not a sixth state.** A context that takes a recoverable page fault keeps its slot
and stays `BLOCKED` until a `RESUME` makes it runnable again — the one case in which a slot is
held for something other than an outstanding memory reference, and the reason it is held is that
the context is going to resume in place (§3.5).

**Migration is a transition, not a state.** A context's slot is released at departure and it
does not yet exist on the tile it is going to, so the migration edge leaves `READY` or
`BLOCKED` and goes straight to `FREE` (§2.6). What causes a context that is already asleep to
depart, and what becomes of the fill it was waiting for, is not yet specified.

#### One outstanding load per context

A context may have **at most one load in flight**. With one, there is nothing to disambiguate
and nothing to keep coherent, so the core needs no load queue and no per-context staging
area. All memory-level parallelism then comes from the context count — the `L/I` term above
is paid in full per miss rather than amortised within a context, which is precisely why `C`
is sized from it.

Two structures sit behind the rule. A **one-slot per-context data buffer** holds the value a
sleeping context will use when it wakes; its purpose is not correctness — the context is
always asleep when its load returns — but decoupling the fill from write-port contention with
executing contexts. Behind the buffers is a small **shared line store** holding the line a
load pulled in until its owner wakes and takes the value out of it, sized at `C` lines because
a context may have at most one load in flight. It is not a data cache: it is shared, so
contexts may evict each other's lines, and a value bound for a named register is a different
thing from a line held by address.

#### Fetch

Each context has a **single-entry fetch buffer**, filled with its next instruction at the end
of each dispatch. Per-context rather than a shared prefetch queue, for one reason that
decides it: a dedicated slot cannot be evicted before its owner is rescheduled, and a shared
one can.

This makes a sequential fetch free when it hits. The next address is known a whole re-issue
window before it is needed, so the access completes underneath the window. A control transfer
differs only in *when* the target becomes known — at branch resolution rather than at
dispatch — so without help that fetch starts late and the context pays the refill.

A small **shared, block-granular branch target buffer, carrying one bimodal bit per entry**,
removes that cost. It supplies a fetch address one window earlier, and it may issue **exactly
one** speculative instruction-stream fetch. It never executes: nothing issues on its answer,
nothing runs down a predicted path beyond that single fetch, and nothing is ever squashed.
Being wrong costs exactly the refill a machine with no predictor pays every time, and being
right saves a bubble — an asymmetry available only because no instruction is ever issued
speculatively.

It is **shared, not per-context**, and that is its whole economy: every context on a tile
runs the same replicated code, so one entry serves every context executing that branch, the
structure is warm almost immediately, and it does not scale with `C`.

A context whose fetch buffer has not yet been filled is not a candidate for issue selection, so
it consumes no issue bandwidth while it waits. Which of the five states such a context is
counted in is not yet specified.

#### Stores

Stores do not sleep the context: a store returns no value, so there is nothing to wait for
and nothing to disambiguate. **An invocation cannot retire until its stores have landed** —
*retire* meaning the point at which its completion is released to the fabric, so a host that
sees an invocation end sees everything that invocation wrote. What counts a context's
outstanding stores, and where that count lives, is not yet specified.

The one ordering requirement is narrow and is stated narrowly: **a context observes its own
stores in program order.** Sharing between contexts is the atomic table's business, not this
one. The requirement is met structurally — one slice, requests already serialised by the
issue pipeline — and it is stated as a requirement on the slice: *the tile's memory path must
process same-address requests in arrival order.* The fallback, if that cannot be met, is a
store buffer with an address match, which is a real cost worth avoiding.

#### Atomics

Atomics are enforced, and they are nearly free, because **atomicity here comes from
ownership, not from a protocol**. Every access to an address range converges on the one
function core that owns that range, since a context must migrate there to touch it; a
read-modify-write is therefore serialised locally, with no travelling lock, no cross-tile
transaction and no second copy. One structure per tile — the **atomic table** — obtains,
releases and passes atomics, at the granularity of the operand word rather than the line.
§4.3 specifies it, together with the obligation it places on the directory.

---

### 2.3 The address space: pages, sizes and types

#### Three page sizes

The machine has **three page sizes**: `4 KiB`, `G`, and `N·G`. Only `4 KiB` is a constant.
`G` is a function of the device geometry (§2.1) and `N·G` follows it, so all three sizes move
together whenever the memory configuration moves. The two larger sizes are what let a page
control its own internal layout — a page whose size is a whole number of grains can be given
physical frames that all land on the same tile, or one on each tile, because the page
boundary and the partition boundary are the same boundary.

#### Four page types

| type | virtual size | physical footprint | where the bytes land | what it is for |
|---|---|---|---|---|
| **HOST** | `4 KiB` | `4 KiB` | block-interleaved across every tile after the fabric, at the block, exactly as a conventional machine interleaves across channels | ordinary host data, and any small hot structure every core reads — the mapping a program gets unless something asks for otherwise |
| **GRAIN** | `G` | `G` | **one tile**: the one its vtile names, or — unhinted — wherever the address space's owner finds convenient | NMFC data with an owner, wanted spatially local to one function core |
| **STRIPED** | `N·G` | `N·G` | **one grain on every tile**, all `N` of them | NMFC data with no owner, spread for bandwidth |
| **DUPLICATE** | `G` | `N·G` | **one full identical copy per tile**, all `N` the same bytes | what every core needs locally: function bodies, read-only data, and the page table itself |

The **address space's owner** is the operating system or runtime that maps an address space
and chooses the frames behind it. A **vtile** is a compiled-in label naming a coherent set of
pages — a relation between pages, not a location; §3.3 states what a program may say with it.

#### One addressing mode, one page table, one walk

All four types share **one addressing mode, one page table per address space, and one walk**.
A page's type is not a second translation path: it is a size class, plus — for the one pair
the size class cannot separate — one bit. The size is carried in the page-table entry exactly
as any multi-size page table carries it.

**The mode bit follows the size class.** `4 KiB` implies **block-interleaved**: consecutive
blocks land on consecutive tiles. `G` and `N·G` imply **grain-partitioned**: a whole grain
lands on one tile. The fabric reads the mode off the size class, so the address space's
owner has no separate mode decision to get wrong, and **a `4 KiB` page in grain-partitioned mode, or a `G`
page block-interleaved, is not expressible** — which is the point of tying the two together.
The mode is stamped at allocation and never changes; a mapping that must change mode is a
copy of the page to a new frame, not an edit of the old one. §2.4 states where the bit rides
in the physical address and where it is stripped.

**The replicate bit separates DUPLICATE from GRAIN.** The size class does not separate every
pair: GRAIN and DUPLICATE share the `G` virtual size, and one further page-table-entry bit —
the **replicate bit** — distinguishes them. With the replicate bit set, the frame field names
the **base of a replica set** rather than a single frame, and copy *t* is `base + t`, landing
on tile *t* by construction with no per-tile table. That is one bit and one re-reading of an
existing field, and it is the entire cost of holding four types in three sizes. A reader who
expects the size class alone to name the type will get DUPLICATE wrong; it is the one place
in this model that is not self-evident.

#### STRIPED and DUPLICATE

These two are the pair most easily confused, and one sentence separates them: **they share
the `N·G` physical footprint and differ only in virtual extent.** STRIPED is `N·G` virtual
over `N·G` physical — one virtual grain per physical grain, ordinary memory that happens to
be spread across every channel. DUPLICATE is `G` virtual over `N·G` physical — one virtual
grain over `N` physical grains that all hold the same bytes, so the page costs `N·G` bytes of
physical memory and presents `G` bytes of writeable data.

A duplicate is **read-only by construction**, and that is a statement about user space, not
about the bytes never changing. No copy is privileged and no copy is separately addressable: a
program builds a duplicate page and then stops writing it, and a kernel's write to it is fanned
out by the memory management unit to **every** copy, so that the `N` copies stay identical and
no tile computes on stale bytes. What ordering a fan-out write has against other traffic, and
what happens if one copy's write fails, is not yet specified. What is settled is why `N` copies
work at all: `N` *writable* copies, each independently modifiable, would need a coherence
protocol, whereas `N` identical copies of data nobody writes need nothing. That is exactly why
function bodies can be aliased this way and the data they chase cannot.

#### The group, and the aliasing rule

The two mapping modes are two different tilings of one physical space, and they line up only
at the granularity of an **aligned run of `N` grains — a *group*** — which under either mode
occupies the same chunk indices on every channel. Mixing modes inside a group **aliases**:
blocks of unrelated pages collide on a channel-local address, which nothing below the memory
controller can detect and which surfaces as row-buffer hits between pages that have nothing
to do with each other.

Grains are therefore handed out in groups, and a group is in exactly one mode. This is a
guarantee the address space's owner makes, and it is the reason the mode bit can be discarded
at the DRAM port (§2.4): the two tilings never overlap in a live group, so after the bit is
stripped there is no pair of live addresses left to alias.

#### A striped page is a huge page

**A striped page is one contiguous `N·G` physical extent**, and it is allocated, freed and
shot down exactly as any huge page is. **One grain per tile is a consequence of contiguity
under the partition**, not a placement the address space's owner arranges: `N` consecutive grains from an
aligned base land one on each tile because that is what the partition arithmetic does with
consecutive grains. There is no cross-tile allocation transaction, no ordering rule and no
race for a group index, because there is no cross-tile allocation — it is one extent claimed
once, exactly as an ordinary huge page is claimed. Deallocation is likewise ordinary: the page
is unmapped, its extent returned and its translations shot down, and because the extent is
one object there is no partially-freed run and no group left holding live grains of one mode
beside free grains of another. If `N·G` consecutive frames are not available, **the
allocation fails the way a huge-page allocation fails on any system** — compaction, whatever
fallback the address space's owner already has, or failure. "Partial success" is not a state
a single contiguous extent has.

A duplicate's replica set is the same construction with the same bytes in every grain: an
aligned run of `N` grains with copy *t* at `base + t`, all `N` made together. A replicated
grain existing on only some channels would silently turn "choose a copy" back into "choose
among the tiles that happen to have one".

That granularity is the direct cost of making spread a property of the page rather than of
placement policy, and it is the reason to prefer a GRAIN page for any object that has an
owner: a grain page needs one free grain anywhere.

#### Why a small shared structure wants a HOST page

The HOST row is the one a programmer is most likely to get wrong. Under grain-partitioned
mode a structure smaller than a grain lands **entirely on one tile**, so every invocation on
every other tile that reads it must migrate there — a forced migration on every read of a
structure the whole machine touches. A block-interleaved page spreads the same structure
across every channel instead. Which mapping a structure wants is a property of how it is
used, so the program says which; §3.3 and §5.3 give the decision procedure.

---

### 2.4 Translation and placement

#### The order: translate, then route

Every address a function or a host holds is virtual. It is translated to a physical address
**before** anything crosses the fabric, and the **physical** address names the tile. This
holds on the dispatch path — the entry program counter of an invocation is translated before
the invocation is sent, and the physical copy handed back names the tile that will run it —
and on every data access inside a function: resolve, then route, then access.

The order is structural rather than stylistic. Routing after translation means **a tile must
resolve an address before it can know whether it owns it**, so the page table has to be
resolvable *locally* for foreign addresses as well as local ones. Most of what follows in
this section is a consequence of that one sentence.

Partitioning tiles by *virtual* address is not used, on four grounds: it leaks
hardware-specific detail into the virtual address space; it exposes the tile layout to
software; it confines the compiler to a fixed mapping, since the only placement expressible
is the one the address arithmetic already dictates; and it lets a program steer placement by
choosing addresses, which is hostile in a shared system. Translation is unavoidable in any
case, because functions operate in the virtual address space, and dispatch happens once per
invocation, so the translation on that path is amortised over the whole invocation. Ordering
translation first moves only *when* the migration trigger is evaluated, not whether one
exists — and it frees co-location hints from particular virtual address ranges, which is what
makes placement a run-time decision instead of a layout baked into a linker script.

One prohibition carries that into implementation: **nothing may derive a tile from a virtual
address and depend on the answer.** The distinction that matters is who reads the virtual
address and what depends on the answer. An address space's owner choosing a frame may read a
virtual address once — including using the grain index of a virtual address as a convenient default,
which needs no state and is arithmetically the cheapest choice available — provided nothing
downstream relies on the choice. A router reads it on every access, and everything depends on
the answer. The first is convenience; the second is a partition, and it is what is rejected.
**The test is whether a mechanism would break had the address space's owner chosen
differently.** If it would, it is relying on a partition and it is wrong.

#### One page table per address space, duplicated on every tile

An address space has **one** page table. That table lives on **duplicate** pages, so **every
tile holds a local copy of it**. That is what lets a tile resolve a *foreign* address without
leaving home, which routing-after-translation requires.

A walk therefore never crosses the fabric. Its references descend the walking tile's own
stack — memory management unit, LLC slice, memory controller, channel — and cost slice and
channel bandwidth like any other reference. The walk is not circular even though the table sits
on duplicate pages: a walk descends **physical** frame numbers, so no translation is needed to
reach the table, and the replica arithmetic that picks a tile's copy — base plus tile index — is
arithmetic on a frame number rather than a lookup. Where a tile obtains the physical root of the
copy it walks, and what binds an ASID to that root, is not yet specified. A single table on one tile reached over the
fabric is not an implementation of this design, and routing the walks over the fabric is not
a repair for it: a walk that crosses the fabric is a translation that can force a
**migration** (§2.6), and a migration caused by translation is by construction wrong.

**TLBs are shared and ASID-tagged.** An **ASID** (address-space identifier) names *which*
page table a translation belongs to; sharing a translation *cache* across address spaces is
what an ASID is for, and is a different thing from sharing a *table*. The ASID selects the
table and is part of every translation, every remap and every shootdown. The walker is
hardware, and it hands control back to the kernel on a genuine fault and not otherwise.

#### The table's shape, and the three places it is not stock

The translation structure is a **multi-level radix table**, and its **fields are ordinary**:
a physical frame number, permission bits, a valid bit, accessed and dirty bits, and a size.
This design does not invent a page-table-entry format. The level count and the base page size
are configuration (§6).

The **shape** is standard. Three things are not, and an implementation built as a stock radix
table over a `4 KiB` base page cannot map a single NMFC page.

| departure | what a stock table does | what this table does |
|---|---|---|
| **the terminator sizes** | expresses large pages only at the sizes its level geometry happens to produce | terminates at `4 KiB`, at `G` and at `N·G`. Neither `G` nor `N·G` is a size a stock geometry expresses, because both are derived from the DRAM organisation. **The level geometry is therefore a design item, not a lookup.** |
| **the replicate bit** | has no such field | carries one bit separating a **DUPLICATE** page from a **GRAIN** page, which share the `G` virtual size class and cannot be told apart by size |
| **the walker's output** | returns the frame the entry names | for a duplicate page, the frame field names the **replica set's base**, and the walker adds **its own tile index** to it, so tile `t` resolves to `base + t`. A standard walker does not compute that. |

A **large-page terminator** at an upper level is how a walk ends early, exactly as huge pages
already work; the entry carries the size. Because `G` is device-derived, "the huge page size"
is a quantity the memory configuration sets, not a constant an implementation may bake in. A
stock geometry of nine index bits per level terminates only at `4 KiB`, `2 MiB`, `1 GiB` and
`512 GiB`, and none of those is `G` at any device this design configures; what level geometry
does produce terminators at `G` and `N·G` is not yet specified, which is what "a design item,
not a lookup" means above.

The **mode bit** — the physical address bit that says whether a frame is grain-partitioned or
block-interleaved — lives in the page-table entry and rides on every physical address the
translation produces. It is stamped at allocation by the page's size class and never changes
(§2.3).

#### The tile's one translation cache

**A tile has exactly one translation cache: its shared, ASID-tagged TLB.** It probes **three
arrays in parallel, one per page size** — `4 KiB`, `G` and `N·G` — because a TLB is a cache
with fixed offset bits and one array cannot hold entries of three different page sizes. A
context consults that TLB directly and walks the tile's local copy of the table on a miss,
exactly as a regular core does. The TLB stays with the tile, and so does the walk.

There is **no per-context translation cache, and building one is a mistake this design names
explicitly.** The contexts on a tile run the same function code over one address space, which
is precisely the case in which a shared TLB pays: sharing is what makes the second context's
translation free, and there is minimal address-space contention to thrash it. A per-context
cache in front of a shared TLB re-privatises exactly the entries that were worth sharing, and
throws them away on every migration. Translation state on a tile is therefore **per-tile, not
per-context**, and a context's state has no line for it.

**Nothing translational travels with a migrating context**, and the reason is structural
rather than one of staleness. A cached virtual-to-physical entry is only usable on the tile
that owns that virtual address; after migrating, every address the context is about to touch
belongs to the *new* tile by construction, so any entry carried across would be provably
invalid on arrival. What an arriving context meets is the destination tile's shared TLB: cold
for that context until its first walk fills it, and already warm for every page some other
context on that tile has walked. That is the point of sharing.

#### Mixed sizes are a requirement, and the size follows the type

A machine that quietly made every page huge would flatter itself. Real systems still need
`4 KiB` pages, so a translation system cannot be assessed realistically without them: NMFC
data uses `G`-sized and `N·G`-sized pages and everything else keeps `4 KiB`.

The converse error is worse. **The page size must follow the page type.** Mapping an NMFC
page at `4 KiB` throws away the reach the large page exists to provide and silently requires
the address space's owner to reserve a long run of adjacent `4 KiB` frames so that one
grain's worth of virtual space lands contiguously — which is the contiguity requirement the
grain page was introduced to remove.

**Whether a page is walked as small or large is decided from the page's declared type, never
from whether a frame is already present.** A frame does not exist on first touch, so a walker
that asks whether one exists answers "no" for every page the first time it sees it: it walks
the page as small, fills the small array with an entry nothing will reuse, and walks it again
as large on the very next access — two walks per grain, permanently.

#### Where the partition is, and what it reads

The partition is applied **at the fabric**, on the **physical** address, and the field it
reads is selected by the mode bit. The **block** is the cache line — the unit the hierarchy
moves, the unit a fill carries, and the unit a HOST page is interleaved at; the three are one
quantity, and `block` is a symbol whose value §6 gives.

| mode | which page types | the tile index |
|---|---|---|
| grain-partitioned | GRAIN, STRIPED, DUPLICATE | `tile = (pa >> log2 G) mod N` — the field immediately above the grain offset |
| block-interleaved | HOST | `tile = (pa >> log2 block) mod N` — the field immediately above the block offset |

`log2 G ≥ log2 block` always — a grain is many blocks — so the grain-partitioned tile field
sits at or above the block-interleaved one.

**`N` is a power of two.** The usual justification — that a modulo would not be invertible —
is not the real one, since `x mod N` is well defined for any `N`. What actually requires a
power of two is that **the tile index be a contiguous bit field of the physical address**.
Only then are `compact` and `expand` — excising that field on the way into a slice and
re-inserting it on the way out — a pair of **exact inverses and pure functions of (address,
tile)**, with no per-request bookkeeping. With a non-power-of-two `N` the tile index is an
arithmetic residue rather than a field: there is nothing to excise, and a slice would have to
carry state to reconstruct the address it was handed. The constraint is about slice indexing,
not about modular arithmetic.

#### The physical address as a bit field

The **mode bit** must lie **strictly above the tile field in both layouts** — that is, above
the taller of the two, which is the grain-partitioned one. Stated as a constraint:

```
mode_bit  >=  log2 G + log2 N
```

Three statements about this bit are easy to confuse, and only the first binds. **The
inequality above is the rule**, and an implementation must meet it. **The convention** is to put
the bit one position above the top of the DRAM range, which makes the carried physical address
exactly one bit wider than DRAM and nothing else; it satisfies the rule and is not the rule. And
**the configured position** (§6) is a third thing again — a number that satisfies the inequality
and need not sit exactly where the convention would put it. Build to the inequality, prefer the
convention, and read §6's value as neither.

Separately from where the bit sits, the machine's addresses must accommodate **any** device
geometry inside a 48-bit physical space: arbitrary banks, ranks, bank groups, rows, columns and
channels. That is a bound on what an implementation must be able to carry, not a configured
width.

#### `compact` and `expand`

Because the partition happens at the fabric, the last-level cache, memory controller and
channel are sliced *vertically*: the tile-select bits sit **inside** what would otherwise be
a slice's set index. A slice presented with the full physical address would use only `1/N` of
its sets, and a tile's DRAM would address only `1/N` of its rows. Excising the field on the
way in and reinserting it on the way out is the only thing that makes a vertical slice a
dense address space, so `compact`/`expand` follow from where the partition is rather than
being a separate design choice.

Let `shift` be `log2 G` when the mode bit is set and `log2 block` when it is clear, and let
`tile_bits = log2 N`:

```
compact(addr):   mode = addr & mode_mask
                 body = addr & (mode_mask - 1)
                 low  = body & ((1 << shift) - 1)
                 high = (body >> (shift + tile_bits)) << shift
                 return mode | high | low                    // tile field excised

expand(c, t):    mode = c & mode_mask
                 body = c & (mode_mask - 1)
                 low  = body & ((1 << shift) - 1)
                 high = (body >> shift) << (shift + tile_bits)
                 return mode | high | (t << shift) | low     // tile field reinserted
```

Three properties follow, and each is load-bearing. **Compaction is a slide, not a
re-encoding**: the field between `shift` and the mode bit moves down by `tile_bits`, nothing
below `shift` moves, and the mode bit does not move. **The mode bit survives compaction** and
is preserved by `expand`, which is what makes the pair mode-correct without carrying the mode
alongside the address — the compaction differs by mode, the mode is *in* the address, and the
fabric therefore picks the right one with no extra state. And **the mode bit is stripped at
the DRAM port and nowhere earlier**: the slice and the controller must still be able to tell
the two layouts apart in order to compact and expand correctly, while the DRAM must not see
the bit, because a device decoder would otherwise collapse both modes onto the same row.
Below that port the channel bits are removed as well, so each channel is handed a
channel-local physical address.

#### Placement is a translation-time decision by the address space's owner

The virtual address is translated to physical before it crosses the fabric, and **the
physical copy handed back names the tile.** For an invocation, that address is the entry
program counter; because function code lives on duplicate pages, **choosing which copy to
hand back *is* the placement decision.** This is what makes load balancing a run-time
decision rather than a layout the compiler baked in.

Two consequences bound what a placement policy may do. **Nothing may consult data the
invocation has not touched yet**: dispatch precedes execution, so the first address a
function *would* touch does not exist at the moment the tile is chosen, and a policy that
reads it removes migrations that no real machine could have avoided. And **a frame is
*chosen*, not moved, at initial placement**: co-location is a property the placement pass
maintains by picking which frame to hand back, not arithmetic on the virtual address and not
a first-touch counter.

#### Unhinted objects

An object with no vtile is placed **wherever the address space's owner finds convenient**:
the head of a free list, a large contiguous run it already holds, a tile it is balancing
toward, or the grain index of the virtual address, which needs no state at all. All of those
are permitted and **none of them is a rule.** An unhinted object is still GRAIN or STRIPED,
and that choice belongs to the owner too. There is no "spread by default" placement rule to
fall back on, because spread is a page *type* here and not a policy.

The prohibition rides with the freedom: **no partition semantics attach to an unhinted
object's virtual address.** Nothing downstream may rely on where it landed, derive a tile
from its virtual address, or treat a virtual-address-derived placement as a guarantee it can
compute against.

#### Remap and shootdown

Placement is not frozen at first touch. The address space's owner may **remap** a grain —
change which physical frame backs a virtual grain — and that is what the placement policy of
§4.4 does.

**A remap is a normal TLB shootdown, and that is the whole mechanism.** When the mapping
changes, the cached translations of that mapping are invalidated exactly as any machine
invalidates them on any remap. There is no per-context translation state to invalidate, and
there is nothing NMFC-specific to design.

**A remap moves the data.** Initial placement chooses a frame when nothing exists yet to
move; a remap changes the mapping of a grain that already holds live data, so the data must
be copied or the program is corrupted. The cost is fixed by the grain: **`G` bytes read and
`G` bytes written per grain moved**, and only then is the mapping change published. That
price tag is why a placement policy's gates are arithmetic rather than conservatism.

**A remap must reach every holder of a copy of the table**, and the set is named by
enumeration rather than by an arithmetic expression: **every tile** flushes its cached
translations for that grain and takes the updated entry into its local copy of the table;
**the host's memory management unit** does the same; and so does **the fabric**. One
broadcast reaches all of them, and it carries the ASID, the virtual grain and the new entry,
because the ASID is what says which address space's table is being changed. **Recomputing the
new mapping independently per copy is forbidden:** two copies that recompute can disagree, both
answers are legal physical addresses, and nothing downstream detects the disagreement — a copy
that missed the update resolves to a frame that has been given back. Over-broadcasting costs a
flush; under-broadcasting costs correctness.

The broadcast is the shootdown, and there is nothing NMFC-specific in it: an address space's
owner changes a mapping and invalidates the cached translations of that mapping, exactly as it
does on a machine with no function cores. None of the fourteen instructions performs it — it is
not an offload operation — and how a kernel invokes it, what privilege that takes, what
acknowledges completion, and how it orders against a context that is in flight or parked, are
the same questions a conventional machine answers and are not yet answered here.

**A mapping change travels on exactly one path — this one.** The page table is itself a
duplicate page, and ordinary kernel writes to a duplicate page are fanned out to every copy by
the memory management unit (§2.3) — but that rule governs ordinary writes, and a mapping change
is not an ordinary write to the table, it is a remap. The two mechanisms therefore do not
overlap: a byte written into a duplicate page as data takes the fan-out, an entry changed as a
mapping takes the broadcast, and nothing is applied twice. A striped page's remap is not a
special case: every holder of a copy is updated, exactly as for any other page.

#### Spill

If a tile's free-frame list is exhausted, a grain **spills** to another tile: the vtile does
not get the home it asked for. **The cost of a spill is a migration and a broken co-location,
not a longer access** — there is no remote data path, so a grain that landed on the wrong
tile does not become slow to reach, it becomes a reason to migrate.

A vtile's **cluster** on a tile is the set of that vtile's grains currently backed on that
tile; it is a per-tile count, and it is a different object from the *component* of §4.4, which
is a set of grains united by co-access constraints and may span vtiles. The spill target is
computed at spill time from where the vtile already lives, in this order: the tile holding the
**next-largest cluster of the same vtile**, whenever any other tile already holds grains of this
vtile; and the **least-loaded** tile only when no other tile holds any grain of it. Answering
the first question needs a per-vtile, per-tile grain count that the address-space owner keeps as
it allocates; the form of that structure, and what maintaining it costs, is not yet specified. Co-location is the quantity being preserved, so the spill goes where
most of this vtile's work already is: that minimises the *number* of contexts that must
migrate to reach it, which is what the cost actually is. Least-loaded optimises the wrong
quantity, and is right only in the case where there is no co-location left to preserve —
which is exactly the fallback.

A spill may end in exhaustion, and that is an acceptable outcome: an allocation that cannot
be satisfied cannot be satisfied. **A spill warns; it never hard-errors.** An implementation
that aborts on a spill turns a reportable statistic into a crash and hides the very rate that
diagnoses the problem. **Siloing** is the concentration of one vtile's grains onto few tiles —
the thing co-location is for, and the thing that exhausts a tile's free list when it is carried
too far — and **the spill rate is the statistic that says siloing has gone too far**. The split
between the two spill targets says something different and finer: a rising share of
least-loaded spills means the vtile's clusters have themselves been scattered, so there was no
co-location left to preserve. Exhaustion is not usually reached by filling a tile, either. The
real failure mode is **scatter**: a unit of work needs contiguity on one tile, so a scattered
free list can fail an allocation while total free capacity is ample. The instruments that
diagnose both are in §5.4.

---

### 2.5 Coherence

#### One coherence point

**There is one coherence point in this machine: the boundary between the host cores' private
L2 caches and the last-level cache, in the fabric.** One directory sits there and nowhere
else. That is where a real machine enforces coherence, and moving the address partition to
the fabric does not move the coherence point.

The structure inside a tile is a **port**, not a directory: `N` ports, one directory. A
function core's private instruction and data caches are private like a host L1 — but they sit
on the *slice's* side of the boundary, so a function-core reference does not have to be
ordered by the directory in order to reach its own slice. **A design that puts a directory
between a function core and its own slice has moved the core off the tile**, whatever the
diagram says. Two phrasings therefore describe the same machine: the directory sits at the
L2-to-LLC boundary in the fabric, and host and function-core references converge on the same
line in the slice. Adjacent structures, different jobs.

#### MOESIF, not MESI

The protocol is **MOESIF or equivalent**. Beyond the familiar Modified, Exclusive, Shared and
Invalid, two states carry the design.

| state | meaning | why this design needs it |
|---|---|---|
| **O** — Owned | dirty **and** shared: this agent holds the only up-to-date copy and other agents hold clean copies of it | lets a tile keep a modified line while a host reads it, without a writeback on every share |
| **F** — Forward | clean **designated forwarder**: one of several clean sharers is named as the one that answers | keeps a shared read off the memory channel that the machine exists to conserve |

A plain MESI protocol cannot express the admission order below. MESI-only structures are
usable **below** the coherence point and nowhere above it.

#### The directory's format

**An exact bit vector over host cores and tiles, inclusive of every private cache the
directory serves, with back-invalidation on eviction.**

- **Exact bit vector.** One bit per potential sharer — every host core and every tile. A
  tile's bit is set by its own private instruction- and data-cache fills **and by its
  atomic-table pins**, exactly as a host core's bit is set by an L1 or L2 fill. The entry
  also carries the line's protocol state.
- **Inclusive** of **every** private cache the directory serves, host-side **and** tile-side.
  Every line held in a host L1 or L2, in a function core's instruction or data cache, or
  pinned in a tile's atomic table has a directory entry, so the directory always knows every
  copy on both sides of the boundary.
- **Back-invalidate on eviction.** Evicting a directory entry invalidates the copies it
  names, **including tile-side copies** — which is the snoop that reaches a function core and
  makes it hand back a held word. So **"no entry" always means "no copy."**

Scoping inclusivity to host caches alone reopens a real coherence hole: a host writes a small
shared value, no tile-side copy is named, no snoop is sent, and function cores keep reading a
stale value while every individual result still looks correct. A pinned tile-side line is a
**request, not a right**: the directory may ask for it back, and the tile must yield, handing
back any word it is holding so the line can be patched before the snoop is answered (§4.3).

Three arguments fix the exact vector, and the third makes it a correctness matter rather than
a performance one. **It never broadcasts** — a bit vector names the exact sharers, so an
invalidation costs *k* messages for *k* sharers, and on one fabric that also carries migration
and line fills a broadcasting directory spends exactly the budget this machine exists to
conserve. **Its cost is bounded and small at the scale this design targets** — the classical
`O(sharers)` objection moved large-scale directory machines to limited pointers and coarse
vectors at node counts in the hundreds, while a machine with tens of tiles and tens of host
cores is inside the range where the exact answer costs a small fraction of a cache line. And
**inclusivity plus back-invalidation is what makes the strict order below enforceable**: the
directory must be able to name the current owner of a line a tile is working on, always, and a
non-inclusive directory may hold no entry for a line that is nonetheless cached. The
alternative — limited pointers with coarse-vector overflow, non-inclusive — is smaller per
entry and loses twice: an overflowed line degrades to broadcast and the lines that overflow
are the hot ones, and the strict order becomes unimplementable.

#### Bypass is about ordering, never about tracking

**A function core's local path skips the directory's *arbitration*; it does not skip the
directory's *bookkeeping*.** When a tile's private caches or its atomic table retain a copy of
a line, the directory records that the tile holds it, in the same entry and the same bit
vector that names host sharers, so that a later host reference can be snooped against it. The
two halves of the atomic table's contract (§4.3) — that the tile is told when a line is
snooped away, and that after handing it back the directory no longer believes the tile holds
anything — are unsatisfiable unless the tile is a tracked agent. **Bypass for order; track
always.**

#### The admission order: the tile takes strict priority

| case | who pays |
|---|---|
| invocation against invocation **on one core** | **nothing.** One slice, one copy, nothing to invalidate |
| a **host** reference to a block a function core is modifying | **the host pays.** This is the permitted direction |
| a **function core** touching a block a host modified | **avoided wherever possible.** A function core does not pay for host activity on its own lines |

The justification is not that near-memory cores deserve to win arguments. **The function core
is the ordering point for its address range by construction** — every invocation touching that
range has migrated there — so it serialises without a protocol, and making it yield to a
remote requester replaces a free ordering point with an expensive one.

**The order says who pays, never who may refuse.** It is an order over *arbitration*: when a
host reference and a tile reference contend, the host waits. It does not give a tile the right
to hold a line against a back-invalidation, and the two rules therefore do not conflict — a pin
is a request in both directions, to the directory above and to the tile's own data cache below
(§4.3). What happens to a read-modify-write whose word is handed back mid-sequence is not yet
specified. This is also why
migration stays cheap in practice rather than by assertion: offloading the shared,
memory-bound work to the tiles is what makes host memory and coherence traffic rare, and that
rarity is the headroom migration runs in.

Sizing follows from the order. **A directory that must evict an entry in order to admit one
has an unenforceable order**, since with strict priority it must always be able to name the
current owner of a line a tile is working on. Back-invalidation is the mechanism that makes
"never evicts to admit" true rather than hoped for.

#### Ownership follows the reference

**An NMFC read of a line a host holds dirty transfers the ownership to the tile.** The
directory sends the holder a transfer snoop rather than a plain downgrade. The host **drops to
shared and keeps its readable copy** — it read or wrote the line a moment ago, and taking the
copy away would only make it fetch the line again — and the **requesting tile becomes the
line's owner of record**, holding the dirty copy. **No forwarder is nominated.** Nominating the
host would put it straight back in the path of the tile's next reference to that line; with no
forwarder, that reference is answered by the slice under the tile's own stack, which is the
whole point of the transfer.

The tile's state is therefore **`O`** — dirty and shared — and not `E` or `M`, and the reason
is a correctness one rather than a preference. A cache that holds a line exclusively may write
it **silently**, with no message to the directory. Granting the tile exclusivity while the host
still holds a copy would lose the host's copy on the tile's first write. `O` gives the tile the
ownership and the dirty data while keeping the host's shared copy valid, and it makes the tile's
first write an **upgrade** — which invalidates the host properly, through the directory. **When
the transferred line reaches the tile's slice is not yet specified.**

This is the third row of the admission-order table made mechanical, and it is the sense in which
a reference to a block a function core is touching is *treated as ownership*. A directory that
leaves the host as owner of record makes every later function-core read of the same line another
snoop of the host, so the avoided direction is paid once per **read**, and the cost grows with
the traffic. Moving the ownership makes it paid once per **line**, at the moment the tile takes
it, and never again — so what remains of the avoided direction is bounded by the number of lines
the function cores take, not by how often they read them. **The reverse direction is unchanged:
a host reference to a line a function core holds dirty is the permitted direction, and the host
pays for it.**

#### Three message classes on one fabric

There is **one fabric**, carrying three message classes in queues that are **per destination
and per class**: **COHERENCE** (directory requests and responses — invalidations, ownership
transfers, snoop responses), **MIGRATION** (a context leaving one tile for another: 72 bytes,
a 512-bit register file plus an 8-byte program counter), and **FILL** (LLC and DRAM line
traffic, 64 bytes per line).

The arbitration rule has two halves, and each is load-bearing.

1. **COHERENCE is arbitrated strictly first.** Not weighted, not a tie-break — first. The
   admission order above is an *order*, and an order cannot be implemented if a coherence
   response the order depends on can sit behind a line fill.
2. **MIGRATION and FILL are arbitrated at equal weight** — round-robin between the two
   classes at each destination queue. This is the precondition of the parity claim in §2.6:
   if migration went strictly ahead of fill, a 72-byte migration and the 64-byte fill it
   replaces would no longer be priced the same by the fabric, and the statement that they are
   alternatives costing the same order would become inexpressible rather than merely
   unmeasured.

Three consequences, so this is not re-derived as a scheduling detail. **The classes are
design; the rates are configuration** — how many messages of each class a destination accepts
per cycle, and how deep its queues are, is configuration; *that* there are three classes, that
coherence is first, and that the other two are equal is not. **Per destination and per class,
never one shared queue** — one shared queue lets a single congested destination own all of it,
and one shared class lets a burst of fills starve the migration that would have freed the tile
the fills are queued behind. And **a second network for NMFC traffic is forbidden**: the
classes share links and share the byte budget, and that sharing is the reason subsumption
(§2.6) is a measurable claim at all.

---

### 2.6 Migration

#### What a migration is

Tiles are partitioned by physical address. When an invocation needs an address its tile does
not own, the invocation **migrates**: it moves to the owning tile and resumes there. There is
**no remote data path** — a foreign access migrates, it never fetches — and that absence is
what makes the tile an unambiguous ordering point for its own address range.

#### The payload

**A migration is exactly 72 bytes**: the context's register file, 512 bits, plus the program
counter, 8 bytes. One message, one class. Nothing else travels.

- **The body does not travel.** A migrating context carries a *pointer* to code that is
  already replicated on every channel, on duplicate pages. Arriving does not fetch code across
  the fabric.
- **Translations do not travel** (§2.4). There is no per-context translation state to carry.
- **The program counter does not change.** A function's code is **one virtual page aliased to
  one physical copy per channel**, so the same virtual address resolves to whichever copy the
  arriving tile owns. There is no per-tile bias to apply, and **a context therefore never
  migrates for an instruction fetch.** Under a layout in which the `N` copies were `N`
  distinct virtual pages, the instruction virtual address would change on every migration by
  construction, and fetch-induced translation work would be unavoidable.

#### Parity and subsumption

Two claims live here, and they are separate claims with separate tests.

**Parity.** A real fabric moves nothing smaller than a cache line, so the alternative to
migrating a context is fetching a 64-byte line. 72 bytes of register file and program counter
against that 64-byte line is a comparison against the *baseline* machine — a conventional core
pulling the line — and it is tested by reproducing that baseline on the same stack, never by
building a second mechanism inside this machine. It is a claim about bytes, and it is a claim
about the same bytes crossing the same fabric on both sides of the comparison: the architecture
fixes that there is one fabric, three classes and an arbitration order, and fixes no topology at
all, so hop counts and hop latency are properties of an implementation and of §6 rather than of
this claim. In particular it is **not** a reason to add a
remote data path so that both options can be measured in one machine: a remote read would
destroy the construction that makes tile-local atomicity sound, in exchange for nothing the
architecture wants.

**Subsumption.** A context either migrates to the data or fetches the data to itself — **never
both** — so migration traffic **subsumes** data traffic rather than adding to it. A machine
that migrates does not move more bytes across the fabric than one that fetches, within the
margin of a program counter and a header. This is the claim that requires one fabric:
migration traffic does not *add* to data traffic only if both contend for the same links and
the same budget.

**Subsumption is what makes local atomicity free.** A read-modify-write is serialised by the
one core that owns the address: no travelling lock, no protocol, an unambiguous ordering
point, and no coherence question because no second copy exists — at the cost of a data
movement the machine was going to perform either way.

#### Arrival is cheap, structurally

Three structural reasons, none of which depends on a measurement. **There is no data locality
to abandon**: a slice only ever held the addresses its own tile owns, and a context migrating
is going *because* it needs an address the destination owns. **The code is already there**, on
duplicate pages. And **there is no stack and no cached working set to reconstruct**, because a
context is a register file and a program counter, which is precisely what makes it portable.
What an arriving context meets is the destination's shared TLB, cold for it until its first
walk; that walk is the term that decides whether the budget below is comfortable, and it is a
real memory reference like any other.

#### The slot is released at departure

**A context's tile slot is released at departure, before the fabric is even asked.** Once a
context has decided to leave, its whole state is a message and does not need a tile slot to
sit in. Holding the slot until the fabric accepts the message is hold-and-wait, and it
deadlocks exactly as hold-and-wait always does: a full tile whose every occupied context is
waiting to leave can never free one. **A migrating context therefore occupies nothing on the
tile it left and does not yet exist on the tile it is going to.** An age guarantee is not a
substitute: reserving a slot for the oldest waiter admits one context into a full tile but
promises nothing about that context ever leaving, so the reserve is consumed once and gone.

#### Queueing rules

Three rules, each of which fixes a distinct way for one congested destination to stall the
machine. **Queue per destination and per class** — a single shared queue lets one congested
destination own all of it. **Do not stop at the head of the outgoing queue** — a refusal says
one destination is congested, not that the fabric is, and stopping at the head lets a context
bound for a full tile pin every context behind it that has somewhere to go; the same fix is
needed at all three stages, the core's outgoing queue, the fabric's queues, and the
destination's context array. And **completions drain before invocations** — a completion frees
a context on some tile, and an invocation waiting for that tile would otherwise spin against a
full core that the very message behind it was about to drain.

#### Migration is expected, and it is evidence

**Migration must happen and must be handled.** A function that constantly migrates is badly
placed or badly shaped; one that executes a long stretch of instructions and migrates a
handful of times is exactly what this design expects. **The failure mode is frequency, not
occurrence.**

A migration says either the function or the page was in the wrong place, which makes a
migration a **co-access constraint**: two addresses that one invocation needed and that did
not share a tile. The placement policy is meant to act on that (§4.4). **A migration rate is
not itself a cost.** Removing migrations by replicating data can make a machine slower,
because the replication costs more than the migrations did. A migration count is **evidence
about placement**, not a quantity to minimise.

Two tests apply, they measure different things, and they are not interchangeable.

| | the budget | the legitimacy ceiling |
|---|---|---|
| the test | migrations per **instruction** | migrations per **memory operation**, plus a categorical clause |
| the form | enough work done after arrival to amortise the transit | **(a)** no migration caused by instruction fetch or by translation, at all; **(b)** data migrations never outnumbering the loads and stores that caused them |
| what it is about | latency amortisation | legitimacy — whether the migration should have happened at all |
| status | a **design target** for a well-shaped function, not a gate | a **hard gate** |

Clause (a) is categorical because both of its causes are structurally impossible in a correct
implementation: instruction fetch cannot migrate a context, because code is aliased to a copy
on every channel and the program counter does not change; and translation cannot migrate a
context, because the page table is duplicated on every tile and a walk never leaves home. A
machine that reports either is not slow — it is wrong, and the enforcement for it belongs on
the tile's own port into its slice, where an address that does not belong to this tile can be
caught at the moment it crosses.

The right way to judge migration's impact is not the count. **The goal is keeping the DRAM
channels saturated**, so migration's impact is what it does to memory-level parallelism and to
the load on the fabric — which is what §5.4's instruments read.

---

## 3. Interface

A program sees three surfaces and no more. **Fourteen instructions** (§3.1), of which eleven
issue on a host core and three on a function core: seven offload instructions, two that move one
64-bit lane of a host context register, and `RESUME` and `KILL`, which close an invocation that
will not close itself, all run on the host; `END`, `CONT` and `CONT.M` run on a function core.
**One context format**
(§3.2): a context is 512 bits, and a register number names a range of those bits. **Two
declarations about data** (§3.3): a program says what *type* a page is and which *vtile* it
belongs to, and says nothing else about where its bytes go. Everything a compiler writer or an
operating-system writer needs follows from those three, plus the admission test of §3.4 that
decides whether a compiled body may run at all.

### 3.1 The instruction set

#### The governing rule: nothing blocks

**There are no blocking instructions.** Every action that can fail is a **try** that reports
whether it succeeded, paired where useful with a **probe** that asks whether it would.
Software spins if it wants to wait; the hardware never spins on software's behalf.

This is not a stylistic preference. A blocking instruction is a resource held while waiting
for a resource, which is the shape that deadlocks a machine whose contexts, FTU entries and
fabric queues are all finite. Making the shape inexpressible is cheaper than auditing for it.
The rule is why there is one `JOIN` rather than a blocking form and a permissive form, and it
is why `JOINQ` earns an encoding of its own — a probe must not move the payload it is asking
about.

#### The base ISA underneath

A function core executes RISC-V: the 64-bit integer base, multiply/divide, atomics, and both
floating-point widths. Where a machine-readable spelling is needed it is
`RV64IMA_Zfinx_Zdinx`, because floating-point opcodes take their operands from the same file
as integer opcodes — `f`*n* and `x`*n* name the same bits, with the type taken from the opcode
(§3.2). The consequences are worth stating in the negative, because they are what a stock
toolchain will otherwise assume.

- **There is no separate floating-point register file.** A second file would be a second
  context: it would break the rule that 512 bits go out and the same 512 bits come back, and
  it would enlarge the 72-byte migration payload.
- **There is no `fcsr`, no rounding-mode state and no floating-point exception state.** That
  is per-context state the 512 bits do not budget for. The tile defines the dynamic rounding
  mode as round-to-nearest-even, so ordinary compiler output — which emits no rounding suffix
  and assembles to the dynamic encoding — runs unchanged. A function that sets a rounding mode
  at run time is rejected at build time rather than executed with the wrong one.
- **The floating-point loads, stores and inter-file moves are not in the set** — `flw`, `fld`,
  `fsw`, `fsd` and the `fmv.x.*` / `fmv.*.x` family. Float data arrives through `ld`, `lw` and
  the ordinary stores, because there is one file and nothing to move between.
- **Absent by design:** control-and-status-register access, `FENCE`, compressed encodings,
  `ecall`, and everything from the vector extension.

Everything else about the base ISA is stock. The additions are the fourteen instructions
below.

#### The set

**Twelve base instructions, plus `RESUME` and `KILL`.** Two terms are needed to read the
table. A **handle** is the name of one outstanding invocation: `FORK` returns one, and `JOIN`,
`JOINQ`, `RESUME` and `KILL` each take one. It is the only per-invocation name the machine
has. The **function tracking unit** (**FTU**) is the host-side structure that issues handles
and holds outstanding invocations; it is specified after the table. Operand naming: `r`*x* is
a general register, `c`*x* is one of the host's context registers (§3.2), and *lane* is a
compile-time constant selecting 64 bits of a context register.

| instruction | operands | semantics | privilege | notes |
|---|---|---|---|---|
| `FORK.R` | `rH, rPC, cCTX` | Try. Allocate an FTU entry and dispatch an invocation at `rPC` with the 512-bit context in `cCTX`. `rH` receives the handle, or 0 if no entry is free. | user | The refusal is the authoritative occupancy answer and cannot go stale |
| `FORK.M` | `rH, rPC, rADDR` | As `FORK.R`, but **the tile** fetches the 512-bit context from the address in `rADDR`. | user | Only an address crosses the fabric, and the tile issues the load |
| `FORKF.R` | `rH, rPC, cCTX` | Fire-and-forget fork from a context register. Returns a handle whose result can never be read. | user | The entry still exists and still has to be closed (§3.5) |
| `FORKF.M` | `rH, rPC, rADDR` | Fire-and-forget fork, context fetched by the tile. | user | Exists so that all four fork forms are uniform |
| `FORKQ` | `rN` | Probe. `rN` receives the number of free FTU entries. | user | A sizing hint, never a contract: it may go stale between probe and fork |
| `JOIN` | `rOK, cDST, rH` | Try. If the invocation named by `rH` has returned, deposit its 512 bits into `cDST` and set `rOK` to 1. On failure `rOK` is 0 and `cDST` is untouched. | user | The only instruction that frees a join-expected entry |
| `JOINQ` | `rOK, rH` | Probe. `rOK` reports whether the invocation has returned, **without moving the 64 bytes**. | user | Separate from `JOIN` precisely so a probe moves no payload |
| `CXW` | `cD, lane, rS` | Write one 64-bit lane of context register `cD` from `rS`. | user | The lane is an access granularity, not the context's structure |
| `CXR` | `rD, cS, lane` | Read one 64-bit lane of context register `cS` into `rD`. | user | |
| `END` (return bit) | none | End the context executing it. With the **return bit set**, the completion carries the whole 512-bit register file. With it **clear**, the completion is an acknowledgement only. | user | One opcode, two forms; assembler surfaces are conventionally `RETC` and `ENDC` |
| `CONT` | `rPC` | Extend. Carry this context forward into a successor beginning at `rPC`. | user | Inherits the existing entry; allocates nothing, so it cannot fail |
| `CONT.M` | `rPC, rADDR` | Extend, replacing the register file **wholesale** with 512 bits the tile fetches from `rADDR`. | user | The fetched bits *are* the new context; there is nothing to merge |
| `RESUME` | `rH` | Restart the parked context named by `rH`; it re-issues the instruction that faulted. | **privileged** | Allocates nothing, refuses nothing, cannot fail |
| `KILL` | `rH` | End one of the caller's own outstanding invocations, wherever it is. | user | Broadcast on the control path; the entry frees on the acting tile's kill-ACK (§3.5). A handle the caller's own FTU does not currently hold is a no-op |

#### Why the set has this shape

**`FORK` returns a handle so that `JOIN` can name an entry out of order.** Without a handle,
join could only mean "the oldest", and a single straggler blocks every finished invocation
behind it. All four fork forms return one — fire-and-forget included — which keeps every
invocation addressable and the four encodings uniform.

**`FORKQ` returns a count rather than a flag**, so software sizes a batch instead of probing
once per fork. It earns an encoding only because a fire-and-forget entry is released
asynchronously, when its invocation ends, so its occupancy is not derivable from the
instruction stream — whereas for join-expected forks occupancy is just forks minus joins.

**`END` takes no operands in either form.** It returns the register file of the context
executing it, and a context has exactly one. There is nothing to name — and a context register
is a host-side object that a function core could not resolve in any case.

**`CONT` cannot fail.** It inherits the existing FTU entry instead of allocating one, so it
consumes no new resource and can never be refused. That is precisely why extension is safe
where fan-out is not (§5.1). A handle stays valid across an arbitrary chain of successors, and
whatever the last link returns is what the `JOIN` receives. `CONT` is also the mechanism for
splitting a function too large for one 512-bit context into a chain of links, each admissible
on its own and none of them refusable.

**The two memory forms name an address the executing party does not resolve.** `FORK.M` sends
the address instead of the context so that only the address crosses the fabric and the load is
issued by the tile; the tile is chosen by translating `rPC`, not `rADDR`. `CONT.M` does the same
inside a running invocation, and the 512 bits it fetches replace the register file entirely, so
there is nothing to merge and nothing of the old context to preserve. What either does when the
address it is handed belongs to a tile other than the one performing the fetch is not yet
specified, and neither is what becomes of an outstanding load, an atomic-table hold or an
unlanded store at the instant a `CONT` extends.

#### The function tracking unit

The **FTU** is a host-side structure parallel to the load/store queue, not a reuse of it:
offload concurrency is its own resource and not an accident of load-queue depth. It holds an
outstanding invocation from `FORK` to `JOIN`, and on return it holds that invocation's 64-byte
result until a `JOIN` collects it. Holding the result is what buys the tile its context slot
back the instant the invocation returns; a unit that could not absorb a completion would force
a tile to hold a finished context until the host made room, which is a resource held while
waiting for a resource. An entry is a returned register file plus a handful of bits: state,
retirement mode, the identity of the **hart** that forked it — a hart is one hardware thread of
a host core, the RISC-V name for the thing an instruction stream runs on — and one error flag.

**The unit refuses rather than evicts.** A cache makes room by evicting; this array cannot,
because an entry holds the only copy of a returned register file and a join-expected entry
must never close without returning its values. So it fills, and then `FORK` returns 0. That
refusal is architecturally visible because it cannot be handled invisibly — which is why
`FORKQ` exists. **It cannot be made smaller by holding fewer payloads than entries**, since
every outstanding invocation may complete before any join.

The FTU is also the delivery path for faults (§3.5), because it is where an invocation's
identity already lives: a tile does not know which host core to trap, and the entry does.

#### Encodings

**Field encodings are implementation-defined.** The set fits inside one RISC-V `custom`
opcode. What the architecture fixes is the count, the group membership, and that `RESUME` and
`KILL` each occupy a slot; it assigns no `funct` values. Eight groups exist. Six are occupied by
the twelve base instructions — fork, probe, join, end, cont, and context-register move — and the
remaining two are the reserved space out of which `RESUME` and `KILL` each take a slot, together
with anything a later revision adds. Neither `RESUME` nor `KILL` belongs to one of the six named
groups.

Two encoding properties are architectural rather than incidental, because software depends on
them. **Every operand is a value in a general register**, including the number of a context
register, which is why `CXW`, `CXR`, `FORK.R` and `JOIN` name a context register by a number
held in a general register rather than by a field read against a second file. And **the lane
of `CXW`/`CXR` is an immediate, not a register**, because it is a constant at every call site
and placing it in a register would cost an instruction to produce a number the compiler
already knows.

### 3.2 The context and register naming

#### The context is 512 bits

**A context is 512 bits, bit-packed.** It is not eight registers. The same 512 bits are
divisible as eight 64-bit values, sixteen 32-bit values, sixty-four bytes, or any mixture, and
which division applies is the compiled function's business rather than the machine's.

512 bits is 64 bytes, which is one cache block and the natural transmit unit. Doubling the
context would double the per-tile register state, cost two transmit cycles instead of one, and
degrade register-file latency; the width is a ceiling, not a target.

**512 bits in, the same 512 bits out.** The whole register file returns on completion.
Register positions carry no meaning across the boundary — the join knows how to interpret what
came back — which is what lets the completion be a fixed-size block rather than a described
one.

#### The register number is the bit range

Nothing outside the instruction encoding and the 512 bits participates in resolving a register
name. There is no per-function map, no per-context map, no map cache, and nothing to fetch
after a migration; a migration therefore stays 72 bytes.

The map is generated by one rule: **the two halves of `x`*n* are `x`*2n* (low) and `x`*2n+1*
(high), and `x8`–`x15` are the eight 64-bit tiles of the context, in order.** Applied once,
that rule generates `x16`–`x31` as the sixteen 32-bit tiles. Read upward, it says what
`x1`–`x7` would be — `x1` the whole 512 bits, `x2`/`x3` its halves, `x4`–`x7` its quarters —
and since no operation in the subset is wider than 64 bits, those seven names denote nothing
this machine can compute on. They are illegal.

*In this section a* **context tile** *is a slice of the 512-bit context;* tile *unqualified
remains the memory tile of §2.1.*

| encoding | context tile | width | bit range | notes |
|---|---|---|---|---|
| `x0` / `f0` | `zero` | any | **none** | Reads 0 at whatever width the instruction needs; `f0` reads +0.0. Writes are discarded. **Costs none of the 512 bits** |
| `x1`–`x7` / `f1`–`f7` | — | — | **none** | **Reserved. Illegal as any operand; the decoder traps.** These are the map's nodes wider than 64 bits |
| `x8`–`x15` / `f8`–`f15` | `d0`–`d7` | 64 | `[64k, 64k+64)`, *k* = *n* − 8 | The **D** tiling: eight 64-bit tiles, covering 512 of 512 |
| `x16`–`x31` / `f16`–`f31` | `w0`–`w15` | 32 | `[32m, 32m+32)`, *m* = *n* − 16 | The **W** tiling: sixteen 32-bit tiles, covering 512 of 512 |

`f`*n* ≡ `x`*n*: the same bits, with the type taken from the opcode. Both tilings cover the
512 bits exactly, `w`*2k* ∪ `w`*2k+1* = `d`*k* for every *k*, every slice is naturally
aligned, and **no slice crosses a 64-bit word boundary** — which is what buys a single-word
read and a simple write-enable path.

A **lane** is the third division to appear over these bits, and it belongs to the host side:
lane *n* is bits `[64n, 64n+64)` — the same bit range `d`*n* names on a function core, because
both are the *n*th aligned 64-bit word of one 512-bit object. That correspondence is what a
compiler needs in order to stage a context on the host and know where the callee will read it.
It is not a claim that the context is divided into lanes: a function may pack those 64 bits as
two 32-bit values, eight bytes or a set of bit fields, and the host still reaches them with the
same two instructions.

```
bit 0                                                                                        511
    |-----d0----|-----d1----|-----d2----|-----d3----|-----d4----|-----d5----|-----d6----|-----d7----|
    |  w0 |  w1 |  w2 |  w3 |  w4 |  w5 |  w6 |  w7 |  w8 |  w9 | w10 | w11 | w12 | w13 | w14 | w15 |
    |---lane 0--|---lane 1--|---lane 2--|---lane 3--|---lane 4--|---lane 5--|---lane 6--|---lane 7--|

    d0..d7  =  x8..x15    (64-bit context tiles)
    w0..w15 =  x16..x31   (32-bit context tiles)
    lane n  =  bits [64n, 64n+64) — the width CXW and CXR move on the host side,
               not a claim about how the function divides those bits
```

The anchoring at `x8` and `x16` is deliberate. In the standard register convention `x8`–`x15`
are `s0`, `s1` and `a0`–`a5`: nothing fixed by an ABI, nothing clobbered by a jump, so every
64-bit name is allocatable and the argument registers land as 64-bit names, which is the right
default for pointers. Reserving `x1`–`x7` also makes the return-address and stack-pointer
names illegal, which turns a compiler convention into a decode-time tripwire. `d`*k* and
`w`*m* are documentation names; an assembler accepts `x8`…`x31`.

#### A name buys directness, not capacity

A value narrower than the narrowest name is **packed** with other values inside one name and
reached by shift-and-mask through a spare name. Packing costs instructions — a shift and a
mask on a read, a shift, a mask and a combine on a write — and costs nothing in capability.

**Liveness is bounded by 512 bits plus scratch, never by a count of names.** The core is
strictly in-order with no renaming, so every one of the 512 bits is independent, and the base
integer shifts and masks reach any bit field. Four 8-bit values inside one 32-bit name is
expressible today, provided the function also holds a spare name in which to stage the
shifting and masking. What the five-bit register field bounds is how many values can be named
*directly*; it does not bound how many can be live.

#### Width comes from the operand's role

Width is a property of the operand's role in the instruction, not of the name that supplies
it.

| role | width | name required |
|---|---|---|
| address or base of a load, store, atomic, or indirect jump | always 64 | a 64-bit name |
| data destination of a load | the opcode's width | **any** name; the loaded value is extended into it |
| data source of a store | the opcode's width | a name at least as wide as the opcode |
| any floating-point source or destination | exactly the width the mnemonic names | exactly that width |
| the integer operand of a floating-point instruction | the width the mnemonic names; a compare or classify **result** is 0/1 and fits any name | as the mnemonic names |
| branch sources | always 64 | any name |
| integer ALU, shift and multiply/divide operands | 32 if and only if the opcode is a `*W` form, else 64 | any name |

Three rules make that table exact.

**The read port** reads each source from exactly its own name's bits and then adjusts it to
the operand width. An integer source narrower than the operand width is **sign-extended**,
which is the standard RV64 invariant maintained at the read port instead of in storage; one
wider is truncated, which arises only for a `*W` opcode and is what a stock core does. A
floating-point source is read at exactly its operand width, with no extension and no NaN-box
check, because the role table requires the name to be exactly that width.

**The write port** puts the result into exactly the destination name's bits and **never
modifies a bit outside them**. Where the execution width is narrower than the destination
name, the result is extended to fill it, exactly as a stock RV64 core extends into a 64-bit
register. This is forced rather than chosen: the bits above a 32-bit name are another
architectural value, not spare room, so neither preserving them nor zeroing them is available.

**The integer ALU always executes at 64 bits.** There is one execution unit per pipe and no
width-selected duplicate of it, so a narrower ALU would buy nothing to trade against. `*W`
opcodes behave exactly as stock RV64 defines them, and no other opcode has a 32-bit mode.
Width therefore never becomes a function of which name the register allocator happened to
pick. Floating point is unaffected: the floating-point unit computes at the opcode's format,
and float is not a width mode on the integer ALU.

**NaN-boxing does not exist on a function core, in either direction.** There is no wider
container, so there is nothing to box on a write and nothing to check on a read. A host core,
being a stock core, does both — which is one of the reasons a function is not host-executable
(§3.4).

Width conversions need no new instruction: narrow-to-wide signed is a move into a 64-bit name;
narrow-to-wide unsigned is the shift-pair idiom written to a 64-bit destination; wide-to-narrow
is free, because the low half of `d`*k* **is** `w`*2k*; and narrow-to-wide float is the
ordinary convert.

#### What the decoder traps

1. An address or base operand that is not a 64-bit name.
2. An operand whose width disagrees with the mnemonic — a 64-bit floating-point add naming a
   32-bit context tile, or a convert whose result width does not match its destination name.
   Comparison, classify and store-conditional results are 0/1 and may be any width.
3. Any of `x1`–`x7` / `f1`–`f7` as any operand.
4. A `jal` or `jalr` whose destination register is anything other than `x0`. This is a check
   on the *form* — an instruction that saves a return address — rather than on which name it
   saves it into, so it also catches a link formed in a name trap 3 would allow. `j`, `jr`,
   indirect jumps and every conditional branch stay legal, so switch tables and computed jumps
   still work.
5. Anything outside the subset.
6. A 32-bit-named operand where the role table requires 64 and no rule above caught it — the
   catch-all that makes the decoder total, so an unlisted encoding fails closed rather than
   executing at a guessed width.
7. A 32-bit-named source on the non-`*W` shift, high-multiply, unsigned-divide and
   unsigned-remainder forms — the opcodes whose 64-bit result truncated to 32 bits is not the
   32-bit result. A function wanting the 32-bit answer emits the `*W` form, which is legal on
   any name; one wanting the 64-bit answer widens into a 64-bit name first.

**`x0` and `f0` are exempt**, because they name no bits and so have no width to disagree with.
The exemption is load-bearing: without it, every branch-against-zero, every immediate load,
every register move and every unconditional jump in the machine would be illegal.

Two things are deliberately legal. **Mixed-width integer ALU operands**: an add with a 64-bit
destination and 32-bit sources executes at 64 with both sources sign-extended; an add with a
32-bit destination also executes at 64 and the write port keeps the low 32 bits, which is the
truncation the programmer asked for by naming a 32-bit destination. And **`*W` opcodes on a
32-bit destination**, because a `*W` opcode is defined as a 32-bit operation canonicalised
into its destination, and when the destination is 32 bits that canonicalisation is the
identity.

**What the decoder cannot see** is misuse of a *legal* name: a 64-bit value placed in a 32-bit
name, or `d`*k* and `w`*2k* live at the same instant under two names over the same bits. A
total map leaves nothing undefined for a trap to fire on, so this class is caught by the
admission test (§3.4) or not at all.

#### No return address, no stack, and the host side

Because `x1` and `x2` are illegal names, the stack-pointer adjustment and the return-address
save a compiler emits are decode-illegal, and `ret` — an indirect jump through the link
register — is illegal. **A body ends with `END`.** Stated honestly: the link check catches
every call an ordinary compiler emits, but it does not make a call impossible, since a link
can be formed by hand out of legal instructions. The no-stack property is a compiler-discipline
invariant with a decode-time tripwire, and it is checked at build time by admission.

**On the host side** there are eight architectural **context registers**, `c0`–`c7`, of 512
bits *each* — 4,096 bits in total. They are staging areas for whole contexts, not a division of
one context into eight parts; a context is still 512 bits, bit-packed. They are **per software
thread**, which makes them ordinary architectural registers: they are saved and restored with
the rest of a thread's state on a context switch, and their count and width are architecture
rather than configuration, so §6 gives them no row. `CXW` and `CXR` move one 64-bit **lane** at
a time, because that is what a general register holds. The lane is an access granularity and
says nothing about how the 512 bits are divided: a context packed as sixteen 32-bit values is
reached through exactly the same two instructions. Once 64 bits move in and out, any packing
inside them is reached with the shifts and masks the base ISA already has, and a field
straddling a lane boundary is two moves and the same arithmetic — which is why a bit-field
insert and extract carrying an offset and a width is not part of the set.

### 3.3 Declaring pages: types and vtiles

**A compiler declares a page's TYPE and its vtile LABEL. It never names a tile.** That is the
complete lever set. A program that writes a tile number is computing on the hardware's layout,
which is exactly what partitioning by physical address exists to prevent (§2.4). The four
types and their footprints are defined in §2.3; this section states what a program may say
with them.

**How a type is chosen.** Read by every core and written by nobody after construction —
function bodies, read-only data, the page table — is **DUPLICATE**. Has an owner and wants to
be spatially local to one function core — **GRAIN**, co-located by vtile. NMFC data with no
owner that wants bandwidth — **STRIPED**. Ordinary host data, or small, hot and read by
everybody — **HOST**. §5.3 gives the decision procedure with its costs.

**How a vtile is used.** Pages carrying the same vtile are co-located wherever they end up.
Distinct vtiles are unrelated, and the address space's owner is free to place them apart —
except that a vtile which already has a home draws its later pages there. **Nothing has to be
adjacent, aligned or contiguous for two objects to land together**: two grains land together
because they carry the same vtile, never because they are neighbours. Grain alignment saves
*space* only, by stopping a small object from obliging its owner to spend a whole grain,
and a linker script written to force grain alignment for co-location is doing a job the label
already does. The one thing a grain genuinely cannot do is carry two page types, since half of
it cannot be duplicated on every tile while the other half sits on one.

**The contract with the address space's owner.** The compiler emits page types and vtiles; the
owner honours them when it chooses frames. Neither half is sufficient alone, and the failure
mode is the two disagreeing — hints the mapping ignores, or a mapping that assumes a layout
the hints did not request. It is a contract to test on both sides at once; a test that checks
only one side passes while the machine misplaces everything.

### 3.4 Function entry and admission

#### Entry

An invocation begins at a translated entry program counter with a 512-bit context already in
place. **There is no prologue and nothing to set up: the context *is* the register file.** A
function core has no stack, so there is no frame to establish and no arguments to spill or
reload; the fork put every argument in the context, and the body's first instruction is work.

**The machine assigns no meaning to any bit position in a context, and defines no calling
convention over it.** A context is 512 opaque bits: the caller writes them lane by lane with
`CXW` and the body reads them at whatever widths and offsets it was compiled for, so the layout
is an agreement between one function and its callers, fixed by the compiler when it compiles
both, and not a property of the architecture. That is deliberate — a fixed argument mapping
would spend bits on positions a given function does not want, and the whole budget is bits — but
it means a caller and a callee compiled separately must be compiled against the same declared
layout for that function. Nothing in the machine checks that they were.

#### Admission

**Admission** is a static test on a compiled function body, run at build time. **It is fatal.**
A function that fails it cannot run on this machine: rewrite it, split it into a `CONT` chain
(§5.2), or reject it. Truncating a function to make it fit would drop dependencies and flatter
every measurement taken afterwards, so a rejection is never softened.

**A function is admissible if and only if all four of the following hold.**

1. **Subset and legality.** Every opcode is in the subset, and every operand satisfies the
   decoder's legality rules of §3.2 — an opcode can be in the subset with its operands still
   illegal. No reserved name is used, and nothing arrives on or touches a stack, because there
   is not one.
2. **Bits.** Peak simultaneous liveness **in bits**, plus the scratch bits the packing needs,
   is at most 512. Every value is charged its own width: a 64-bit float costs 64, a 32-bit
   float 32, a byte 8. **There is no count of values anywhere in the test**, and no type buys
   a slot of its own — integer and floating-point names compete for the same bits. A register
   that is never read is not state a join consumes and costs nothing.
3. **A verified non-overlapping placement** exists over the function's live ranges. This is a
   compiler-correctness check that two simultaneously-live values do not share bits — never a
   capacity check. Capacity is test 2, and test 2 counts bits.
4. **No illegal name** — none of `x1`–`x7` / `f1`–`f7` as any operand. The decoder also traps
   this at run time; admission checks it so the failure arrives at build time.

#### Peak is one maximum, not two

**The compiler runs first and admission checks what it produced.** The placement pass chooses
the packing, the staging slices and the bin assignment and emits a listing; admission is a test
on that listing and produces a verdict, not a layout. The two are therefore not circular: a
failed admission is a failed candidate, and the compiler may run the pass again with different
choices and present a new listing, up to whatever bound it sets itself, before rejecting the
function (§5.2).

Liveness is computed over that post-register-allocation listing — the same listing test 1 walks,
so the test needs no second input. Program points are the instructions of that listing in order,
a value's live range is half-open from its definition to its last read, and a register never
read has an empty range. A value whose last read is an instruction may share bits with a value
defined at that same instruction: the core is strictly in-order and reads all of an
instruction's sources before writing its destination, so the reuse is safe.

```
peak = max over program points p of ( live_bits(p) + scratch_bits(p) )
```

**Not** the maximum of the live bits plus the maximum of the scratch bits. The two differ
whenever the busiest staging happens away from the fullest instant, and the tighter reading is
the correct one: the staging slice and the live data occupy the same 512 bits at the same
instant.

#### Scratch, sized

**A staging slice is 64 bits and anchor-aligned**, whatever the width of the field it stages,
because shift-and-mask runs on full 64-bit operations and a stage narrower than the name it
unpacks cannot hold the intermediate. How many at once is a property of the instruction, not
of the packing in the abstract: for each instruction, one slice per distinct packed source
operand, plus one if the destination is a packed field, and a read-modify-write of a packed
destination reuses that destination's own stage. Since an instruction has at most two sources
and one destination, **at most three slices are live at once and the scratch term never
exceeds 192 bits.** If a function packs nothing anywhere, the scratch term is zero and the
test is a plain bit sum.

**Scratch is placed, not merely charged.** The staging slices are objects in the 512 bits with
live ranges of their own — the shift-and-mask sequence that uses them — and test 3 checks them
for disjointness against every value live across that sequence, exactly as it checks data
against data. A placement that is disjoint on data while a stage clobbers a live value is the
error test 3 exists to catch.

#### Packed fields

**A packed field lies wholly within one 64-bit anchor** and may sit at any bit offset inside
it; no natural alignment is required, and a three-bit tag may start at bit five. It may not
straddle the anchor boundary, because a straddling field costs two extracts, two masks and an
extra combine — which would make the scratch term instruction-dependent in a way no compiler
could predict. Directly-named values are unaffected: every name's slice is naturally aligned
and never straddles.

Test 2 therefore carries a feasibility clause that is still a bit test: the sum is necessary
but not sufficient, and the live values must also fit into eight 64-bit bins with no field split
across a bin, **as packed by first-fit-decreasing on width**. The packer is named because it is
part of the test: the test's answer is defined against it, so a function it cannot pack is
inadmissible whether or not some other packer could have placed the same values. Because every
width is at most 64 and every bin is exactly 64, this bites only on mixtures of widths that do
not divide 64 evenly. Bin-infeasibility is capacity, so it belongs to test 2 and not to test 3.

#### Two properties the machine enforces at run time

**A function must not be host-executable, and a host core refuses to execute function text.**
Identical encodings mean different things on the two cores: `x16` is a 32-bit context tile on
a function core and a 64-bit register on a host, and a function core neither NaN-boxes nor
checks boxes while a host does both. A host that fetched function text would therefore not
fault — it would compute a different answer, silently, which is the worst failure mode the
design has. The refusal is a requirement on the machine and not a documentation rule, and it
happens at **fetch**, before any body instruction executes, because fetch is the last point
before the first wrong result and because a build-time check cannot see a jump taken at run
time. What the host fetches and refuses on is not fixed by the architecture. **The selected
implementation uses a marker word that every function carries as its first instruction word**
(§6): a function core executes it as a no-op, both host models refuse it as an illegal
instruction, and a fork whose target does not carry it is **refused rather than faulted**, so a
mislinked call answers instead of computing. An attribute on the translation is the other
available construction; it catches an entry into the middle of a function body, which a marker
at the entry does not, at the cost of moving the check into the page table and the operating
system. **How far into a function body such a refusal reaches is not yet specified.**

**The fork type and the end type are chosen independently, so every combination returns
something.** A fire-and-forget entry closes on its acknowledgement and its return can never be
read, so a `JOIN` on such a handle **reports failure rather than faulting** — a try's answer,
not a fault. A join-expected entry never closes without returning its values, so an `END` with
the return bit clear still produces an acknowledgement and a **zeroed** register file. Both
mismatches are counted, so they are visible rather than silent. The full set of closures is
§3.5's table.

### 3.5 Faults, `RESUME` and `KILL`

#### A recoverable fault takes an ordinary system's path

A context on a function core takes a page fault. The fault is carried to the host **through
the FTU**, which is where the invocation's identity already lives, and arrives as a trap. The
kernel handler runs exactly as it would for a fault taken by a host core — fix the mapping, or
whatever the fault needs — and then issues `RESUME rH`, naming the context by its handle. The
context re-issues the instruction that faulted and carries on.

**The context parks.** It does not spin and it does not die: it stays `BLOCKED` and keeps its
slot across the fault — the one place a function core holds a slot for something other than an
outstanding memory reference — because it is going to resume in place. Parking is therefore a
condition of a blocked context and not a state of its own, and the cycles it spends are reported
as blocked cycles (§5.4). A fault is not a migration: there is nothing to re-place. The FTU is
the delivery path because it is the only structure that knows which host core to trap; a tile
does not. Walks stay local (§2.4), so **the kernel is entered on a fault and not otherwise**: a
fault is precisely what happens when the local walk cannot complete.

What is not yet specified is the operating system's side of this: what happens to an outstanding
invocation when the hart that forked it is descheduled, when its process is switched out, or
when an address space is torn down while contexts of it are live on tiles.

#### `RESUME`

**`RESUME` is privileged**, because the party that took delivery of the trap is the party that
returns from it — the same shape as a return-from-trap on any machine. If it were user-level,
any program could restart any context whose handle it could name or guess, with nothing on the
other side of the trade. It allocates nothing, refuses nothing, and cannot fail, because the
entry it names already exists. `FORK` and `JOIN` stay user-level: offloading is ordinary user
work and touches no trap path.

There is deliberately **no per-invocation fault status, no error code in the returned register
file, and no fault probe.** Those only make sense with user-defined fault handlers, which this
machine does not have.

#### A fatal fault

A fatal fault — a divide by zero, an exception, an access of the kind that would segfault a
host process — kills the program and all of its contexts, and **nothing in the teardown waits
on the user program**, because a program being killed is precisely a program that can no
longer be trusted to make progress.

**Every outstanding entry of that program closes at once, with a zeroed register file and an
error flag set.** A `JOIN` on any of them returns immediately with the error rather than
blocking or faulting, and `JOINQ` reports the entry as returned. The zeroing is not
decoration: it is the same well-formed "no values came back" shape that an `END` with the
return bit clear already produces, distinguished by the flag. So the rule that a join-expected
entry never closes without returning its values holds **literally**, with no caveat for a dead
program, and a joining host always gets an answer it can test with an ordinary branch rather
than a second trap. **The error flag is the only new architectural state: one bit.** The
teardown needs no instruction; it is the kernel iterating its own entries and applying the
closure.

#### `KILL`

**`KILL rH` is that same closure issued by the program instead of the kernel, and it is
unprivileged.** Privilege has nothing here to protect: a handle is issued by the program's own
FTU, the unit acts only on entries it owns, and a handle drawn from another address space is not
a handle here but a number that names nothing of the caller's. The instruction is therefore
closer to cancelling one's own thread than to signalling another process, and it sits in the
same privilege class as `FORK` and `JOIN` — the exact opposite of `RESUME`'s case, for the exact
opposite reason: nothing about ending your own invocation touches the trap path. How an entry
records the address space it belongs to, as distinct from the hart that forked it, is not yet
specified.

It ends one of the caller's own invocations **wherever it is** — running, sleeping on its one
outstanding load, or parked across a fault — releasing any atomic-table hold it holds and any
slot a parked context was holding. It allocates nothing and cannot be refused, since it names
an entry that already exists.

**The kill is broadcast.** `KILL` goes out on the control path — the same path an invocation
packet, a completion and a `RESUME` packet already take — to **every tile**. The tile holding
the context acts on it; every other tile drops it, having no such handle to act on. The packet
therefore needs no addressee: the FTU entry keeps no tile field and a migrating context needs no
forwarding, so a tile may go on releasing a departing context's slot at departure while
retaining nothing. A directed kill is an optimisation available only where the destination is
already known, and is never something the architecture depends on.

**The entry is freed on the acting tile's kill-ACK** — not at the `KILL`, and not at a `JOIN`.
The entry *closes* at the `KILL`, with the register file zeroed and the error flag set, and
stays **readable** until the ACK arrives, so a `JOIN` in that window returns the error. The
acknowledging tile is by construction the tile that held the context, so until one acknowledges,
the kill is outstanding. The same ACK is what frees a killed **fire-and-forget** entry, which no
other event would, so the ordinary paths leave no entry that no instruction can free. Two cases
are not yet specified: what re-delivers a kill that reaches a tile before the context it names
arrives there, and what frees an entry whose context no tile turns out to be holding.

**A handle names an FTU entry**, and it names nothing else. The unit holds a fixed number of
entries; a cleared entry is recycled, and a handle that afterwards names a recycled entry names
the **new** invocation, exactly as a register name after a write names the new value. There is
no generation bit in the architecture, and the consequence is worth stating plainly rather than
leaving implied: **a handle is meaningful only while its holder knows the entry has not been
reclaimed**, and re-using one across a reclamation reaches whatever now occupies that entry.
Guarding against that is the program's business, as guarding a reclaimed file descriptor is.

What the architecture does supply is that **a handle the caller's FTU does not currently hold is
a no-op** — not a fault and not an error return. That covers an entry already freed and a number
that never named an entry of this program's, and it is what makes `KILL` safe to issue from a
teardown path that cannot know which of its invocations have already ended. **A `RESUME` naming an entry a `KILL` has closed finds no entry and does nothing**, so
neither path waits on the other. There is **no all-kill form**: one handle per instruction, no
wildcard and no process argument, because a wildcard would be the one form that could reach
beyond the caller.

What `KILL` does not protect is the program's own invariants. A kill issued against a healthy
invocation half-way through updating a shared structure leaves that structure half-updated,
and that is the caller's problem. The machine's own state is left clean: the register file is
discarded, the entry closes with a well-formed error return, the atomic-table hold is
released, and the invocation's stores are ordinary stores that either committed or did not.

#### The fault and closure paths

Every way an invocation leaves a function core, and what each does to the context, to the FTU
entry, and to a later `JOIN`.

| path | trigger | the context | the entry | what a later `JOIN` sees |
|---|---|---|---|---|
| **Return** | `END`, return bit set, join-expected fork | ends; slot freed | holds the 64 bytes until collected; frees at the `JOIN` | deposits 512 bits, `rOK` = 1 |
| **Acknowledgement** | `END`, return bit clear, join-expected fork | ends; slot freed | holds a **zeroed** register file | deposits 512 zeroed bits, `rOK` = 1; the mismatch is counted, not faulted |
| **Fire-and-forget closure** | `END`, either form, fire-and-forget fork | ends; slot freed | closes on the acknowledgement; a returned register file is dropped | **reports failure**, not a fault; `JOINQ` likewise |
| **Extension** | `CONT` / `CONT.M` | continues as a successor | **inherited** — nothing allocated, nothing refusable | unaffected; the last link's return is what arrives |
| **Recoverable fault** | page fault | **parks** — stays `BLOCKED`, slot held | holds the invocation's identity; the trap is delivered through it | not yet returned, until `RESUME` runs the context to its end |
| **Fatal fault** | divide by zero, exception, bad access | program and all its contexts killed | **every** outstanding entry of the program closes: zeroed file, error flag | returns immediately with the error; `JOINQ` reports returned |
| **`KILL rH`** | the program ends its own invocation | ended wherever it is; atomic hold and any parked slot released | closes at the `KILL` — zeroed file, error flag — and is **freed on the acting tile's kill-ACK** | returns the error until the ACK frees the entry; a handle the FTU does not hold makes the `KILL` a no-op |

Fatal-fault closures and `KILL` closures take the same path but say different things about a
run — one is the machine killing a program, the other is a program ending its own work — so
they are counted separately, as are both fork/end mismatches.

---

## 4. How it works

Sections 2 and 3 state what the machine is made of and what a program may say to it. This
section traces the four paths on which everything else rests: the life of one invocation from
fork to closure, the life of one load from virtual address to value, the mechanism that makes
a read-modify-write cost what an ordinary access costs, and the policy that decides where a
page lives.

### 4.1 Life of an invocation

**Fork.** `FORK` allocates an FTU entry and returns a handle. If no entry is free it returns 0
and the host continues; it does not wait. The entry program counter is translated before
anything crosses the fabric, and the physical copy the translation hands back names the tile
the invocation will run on — and because function bodies live on duplicate pages, **choosing
which copy to hand back *is* the placement decision** (§2.4). `FORK.M` passes an address in
place of a context: the destination tile fetches the 512 bits itself, so only an address
crosses the fabric and the fetch happens where the data most likely already lives.

**Dispatch.** The invocation packet carries the handle's token, the origin, the home host and
the already-translated entry program counter. The destination admits the invocation into a
free context slot or refuses it. Every address the invocation goes on to translate is
translated against its own address space, and the ASID is what names that address space to the
tile's shared TLB and to its copy of the table (§2.4); how the ASID reaches the tile — carried
on the invocation and migration packets, or held with the slot — is not yet specified. **That refusal is the machine's back-pressure**, and it is
not hidden behind a queue that grows until something else breaks: a refused invocation is a
fact the fabric reports back, and the fabric does not stall at the head of an outgoing queue
on account of it (§2.6).

**Run.** The context begins at the entry program counter with its 512 bits already in place;
there is no prologue. Instruction fetch is served from the tile's own instruction cache and
never crosses the fabric. A load sleeps the context, which wakes when the value arrives; a
store does not sleep it, though an invocation cannot retire — cannot release its completion —
until its stores have landed (§2.2). If the invocation needs an address its tile does not own,
it migrates and resumes at the same program counter (§4.2).

**Ending.** `END` ends the context that executes it; its **return bit** selects whether the
completion carries the whole 512 bits or is an acknowledgement only. `CONT` is not an ending:
it carries the current context forward into a successor at a new program counter, inheriting
the same FTU entry, so one invocation becomes one invocation and nothing is allocated. Five
things end an invocation's run — an `END` with the return bit set, an `END` with it clear, a
fault, a `KILL`, and, as the one that ends nothing, a `CONT`. §3.5's table gives what each
does to the entry and to a later `JOIN`.

Two properties hold across the whole of that table. **An entry frees at its `JOIN`** — which
is why a join-expected entry never closes without returning its values, and why the FTU
refuses rather than evicts. The two closures that reclaim an entry without a join, the
fatal-fault teardown and `KILL`, are closures and not evictions, because the entry is not
reused while its owner still expects it; a zeroed register file with the error flag set is a
well-formed return.

The second invocation loop needs no instruction at all. In the **register-returning** loop the
result comes home in the register file and `JOIN` deposits it. In the **memory-committing**
loop nothing is returned: the invocation performs an ordinary store, coherence makes that
store visible at the directory on the L2↔LLC boundary, and the host reads the block later with
an ordinary load — using an atomic if what it needs is a read-modify-write on that block.
Publication is a property of the memory system, not of an opcode. Ownership in that loop is by
address, and its obligations are never a double commit and never a double block.

Back-pressure is therefore visible at exactly three points and hidden at none: `FORK`
returning 0, the count `FORKQ` reports, and a destination refusing an arriving context.

### 4.2 Life of a load

A function core resolves an address before it can know whether it owns it. The order is
translate, then route, and every data access, every instruction fetch and every page-table
reference follows it.

1. **Probe the tile's TLB.** One shared, ASID-tagged TLB, three arrays in parallel, one per
   page size. Every context on the tile shares it, which is exactly the case where sharing
   pays: the contexts on a tile run the same code over one address space, so the second
   context's translation is free.
2. **On a miss, walk the local copy of the page table.** The walk is hardware and its
   references go down this tile's own stack, so a walk never leaves the tile and costs slice
   and channel bandwidth like any other reference. A walk that cannot complete is a fault and
   takes the path of §3.5.
3. **Read the physical address and the mode bit.** The mode bit is stamped at allocation,
   never changes, and rides above the tile field in both layouts.
4. **Select the tile field by mode.** Grain-partitioned: the field immediately above `log2 G`,
   so one whole grain sits on one tile. Block-interleaved: the field immediately above the
   block offset.
5. **Compare that field against this tile.** Exactly two branches follow.
6. **If the address is local**, `compact` excises the tile field and the reference is issued
   to the tile's data cache and its slice. The context sleeps and wakes when the value
   arrives. Because the function core sits on the slice's side of the fabric interface, no
   part of this path crosses the fabric.
7. **If the address is foreign**, the context migrates: its slot is released at departure, the
   72 bytes cross the fabric, the context takes a slot on the destination, and it resumes at
   the same program counter.

A context has **one outstanding load** at a time. With one there is nothing to disambiguate
and nothing to keep coherent, and all memory-level parallelism comes from the context count
rather than from per-context lookahead. That is the mechanism that turns a serial pointer
chase into a saturated channel: the tile does not make one thread's misses overlap, it runs
enough threads that the channel never idles.

Two consequences of step 7 are worth naming as consequences. **A context never migrates in
order to fetch an instruction**, because the program counter does not change across a
migration and the code is aliased to a copy on every tile. And **a context never migrates in
order to translate**, because walks are local. The only legitimate cause of a migration is
data, which is what §2.6's legitimacy ceiling gates on.

The order of operations is also what makes the duplicated page table necessary rather than
merely convenient. Routing after translation means a tile must be able to resolve a *foreign*
address locally, since that is how it discovers the address is foreign. Per-tile partitioned
roots cannot express this: discovering "this is not mine" would itself require a remote walk,
which is the one reference the design does not allow.

### 4.3 The atomic table

The **atomic table** is one structure per tile with one job, stated in three verbs: obtain,
release and pass an atomic, quickly. It enforces atomic relationships rather than assuming
them — removing atomics would not make them free, it would make atomicity an unchecked
assumption on every operation.

The architecture fixes what the table must guarantee and leaves its structure open. Its
capacity, its entry format, how a data-cache access is checked against the words it holds, and
the messages by which a pin is requested and surrendered are all implementation, and its
capacity is explicitly a quantity to size from measurement (§6). What follows is the set of
obligations an implementation of it must meet.

**Atomicity comes from ownership, not from a protocol.** Every access to an address range
converges on the one core that owns it, because a context must migrate to that tile to touch
the range at all. A read-modify-write is therefore serialised locally: no travelling lock, no
cross-tile protocol, no second copy, and no coherence question, because there is no second copy
to keep coherent. Two tiles cannot hold atomics for the same line by construction. The data
movement that establishes this is movement the machine was going to perform anyway (§2.6), so
ownership costs no bandwidth beyond what the access already cost.

**Granularity is the operand word, not the line.** Two atomics on different words are
independent even when they share a line, because that block lives on exactly one tile and is
touched by exactly one core. Locking the enclosing line instead makes a line's worth of
unrelated counters — the shape of a graph kernel's parent array — contend for a critical
section that spans a memory round trip.

**Contexts park; they do not spin.** A retrying context still costs an examination slot every
cycle, and the issue loop examines at most as many entries as were queued when the cycle
began, so a ready queue full of contexts spinning on one hot word spends the whole issue
budget on contexts that cannot issue: contention becomes superlinear rather than merely
serial. A waiter sleeps and wakes on release. A full table has exactly two permitted
behaviours — it is unachievable by construction, or the context sleeps until an entry is
available. Sleeping is not blocking in the forbidden sense; a sleeping context has yielded its
issue slot exactly as one sleeping on a load has. What is forbidden is spinning while holding.

**Hand-off passes the value with the lock.** The holder gives the next waiter both the lock and
the value it already has, so the waiter never refetches and ownership passes without the lock
ever becoming free — which is also what keeps the forwarded value correct, since no third
context can change the address in between. A queue of updates to one address therefore costs
one fetch plus an ALU pass each, rather than a round trip apiece. Three obligations ride with
that, and none of them is optional.

1. **The chain is bounded, and the bound is a coherency guarantee rather than an
   optimisation.** A word passed context to context is a word the rest of the machine cannot
   see, so after a fixed number of hand-offs it goes back to the data cache whether or not
   anyone is still waiting. Without the bound, a steady arrival of contexts holds a word out
   of the hierarchy indefinitely.
2. **An ordinary load or store to a held word goes through the table.** While a word is held,
   the cache's copy is stale, and an access served by the cache would read around the atomic.
   The entry therefore outlives its own writeback: it is removed when the write lands, and if
   anything touched the word in the meantime the write is repeated, so there is no moment at
   which the value is neither in the tile nor in the cache.
3. **Waiting for a second word while holding a first is refused, not handled.** It is a
   resource held while waiting for a resource, and it can only arise from a function that takes
   a second word without releasing the first, so the machine diagnoses it rather than adding a
   protocol to survive it.

**The directory obligation.** A held word sits above the tile's data cache on purpose, so the
coherence directory must know the tile is holding it. Two tiles cannot contend for a line, but
a host core and a tile can — a host writing a shared counter between phases while tiles
increment a copy no snoop ever reached produces a wrong count and a right answer, which is the
failure that survives a test suite. Three requirements close it, and each is necessary on its
own. The tile is **told when a line is snooped away**, and hands back the word it holds so the
cache can patch it into the line before answering. A held word's line is **pinned**, on the
same request that fills it, so there is no window in which the line is evicted first and the
snoop consequently never comes. And a pin is a **request, not a right**, in both directions: a data-cache set with no free way
asks the atomic table to give up its oldest pinned line and the fill retries, and a
back-invalidation from the directory above is likewise obeyed rather than refused. Without the
first, a cache wedges on any workload doing enough atomics to colliding addresses; without the
second, the directory could not name every copy and §2.5's order would be unenforceable. The
directory's strict priority for tile references and the atomic table are therefore one
mechanism, not two: keeping a word above the data cache is only sound if the directory can
reach it, and a pin is what makes it reachable rather than what makes it unreachable.

### 4.4 Placement policy

Physical placement without a placement policy is not this design. Round-robin, least-loaded
and first-touch are *dispatch* policies — they answer which tile *starts* an invocation — and
they are not substitutes for deciding where a page lives.

**The policy has one objective: keep migration classified as sub-optimal.** It is not to
minimise migrations. Migration subsumes the transfer it replaces, so a lower migration count
buys no bandwidth, and removing migrations can leave the machine slower than it was; a
proposal that argues from "this reduces migrations" is arguing from a premise the architecture
does not hold. The mechanism has two halves and both are required: move data that is used
together, and let functions move to their data, with source and sink both spread across the
tiles. The tension the policy resolves is between putting everything on one tile, which
minimises migration, and exploiting every channel, which is the reason the machine exists.

**The evidence it acts on is the migration itself.** A migration says that the address the
context left and the address it came for belong together: it is a co-access constraint between
two grains, not a direction to drag one grain toward the other. The constraint holds between
addresses touched by the *same* invocation — uniting whatever two grains happened to migrate
consecutively anywhere in the machine merges everything into one component and says nothing —
so the invocation's identity is part of the evidence, not optional detail. Grains related by
such constraints form a component, and it is the component that gets placed.

Where a rule gates on how one-sided a grain's traffic is, the quantity is **pull dominance**,
and it is defined once here.

> `pull_dominance(grain)` = the migration pulls credited to that grain's **top tile**, divided
> by the total migration pulls credited to that grain, counted over the policy's decision
> **window**, excluding each invocation's first hop.

A **pull** is one migration event, credited to the grain the invocation moved *toward* — one
event, one credit, a count of migrations and not of accesses. The **top tile** is the
destination tile with the most pulls for that grain in the window. The result is a share: `1.0`
means every pull came from one tile, which is a private grain, and `1/N` means uniform, which is
either a genuinely shared grain or a scattered component — rules 1 and 2 below say that those
two look alike and must be treated differently. An invocation's **first** hop is excluded
because an invocation is dispatched to a tile chosen by a policy that has not seen its data, so
its opening migration reports where dispatch put it and nothing else, and counting it credits
uniform noise as though it were locality.

The **window** is a bounded recent interval, and it is the same interval the policy reports its
statistics over — its **epoch**. Its length is configuration, and §6 states it in migrations.

**Use the published algorithms.** This is a NUCA/NUMA problem with published solutions, and
two of them supply what the policy needs. R-NUCA (Hardavellas et al., ISCA'09) supplies the
shape: classify pages and give each class a fixed policy rather than chasing accessors —
instructions replicated, private data placed at its accessor, shared read-write data
interleaved. The first of those is already the duplicate page type. The last is the case this
design keeps rediscovering, because on a graph whose hot array every tile reads, both an
offline minimum cut and an online pull heuristic lose to plain interleaving. Carrefour (Dashti
et al., ASPLOS'13) supplies the gate: measure imbalance and the local-access ratio *before*
choosing among co-location, interleaving and replication, and apply nothing when the
measurements say nothing is wrong. That gate is what makes the adversarial case safe rather
than catastrophic.

**Seven rules bind the policy, and an implementation obeys all seven.**

1. **Shared grains are never co-located, however lopsided their pull looks.** A grain pulled
   overwhelmingly by one tile is still shared if the remainder is other tiles, and those tiles
   are the ones that then have to migrate. Lopsided evidence can be real and acting on it still
   wrong.
2. **Place whole components, never grains one at a time.** A cluster whose grains are scattered
   over every tile pulls uniformly from all of them, so the dominant puller is noise until a
   majority already sits on one tile: scattered is a stable equilibrium, and moving a component
   as a unit is the symmetry break.
3. **A component larger than a tile's fair share is the whole working set, not a hot set.
   Leave it interleaved.** On a graph where everything touches everything the co-access graph
   is fully connected, and collapsing it onto one tile is a failure an offline minimum cut
   already demonstrates.
4. **Gate on the window, not on a lifetime average.** A lifetime average starts even and
   responds to nothing: it permits the earliest and hottest co-locations while the cumulative
   picture still looks balanced, and then refuses everything afterwards, correctly and far too
   late to matter. This rule's own gate is on load: it withholds a move when the window's
   traffic to the destination tile already exceeds a tile's fair share of the window by more
   than a slack factor.
5. **Balance is not first-touch's job.** First touch's job is to honour the declaration:
   a page carrying a vtile is backed on the tile that vtile already lives on, and a page
   carrying none is backed wherever the address space's owner finds convenient (§2.4).
   Deliberate balancing belongs to remap. A counter that never reads the declaration makes a
   grain's tile depend on the order it was first touched, and grains carry very unequal traffic,
   so spreading them evenly by count spreads traffic unevenly.
6. **A grain that has just moved sits still, for longer the more often it has moved.** Without
   this the policy oscillates: it reacts to a pull, and the pull reverses, because the
   migrations now come from where the grain used to be. The backoff function and its parameters
   are configuration; the rule is not, and moves, bytes copied and attempts withheld are
   instrumented per epoch regardless.
7. **A duplication policy is available, and is often cleaner than a partial swap** for a
   structure that must be present on every channel. Replication is an option a program
   declares, never the baseline.

Two costs bound what the policy may do, and both are stated in §2.4. **A remap moves the
data** — `G` bytes read and `G` bytes written per grain — before the mapping change is
published, which is why the gates above are arithmetic rather than conservatism; once the copy
is done the remap is an ordinary TLB shootdown, broadcast to every holder of a copy of the
table rather than recomputed at each one. And **a spill is a broken co-location, not a slower
access**: it costs a migration for every context that must now cross to reach the grain, it
is reported and never fatal, and its target follows the quantity being preserved.

**Congruence** is the property that a page is backed on the tile its declaration asked for:
the vtile's home for a page carrying a vtile, and the frame the owner chose for one that
carries none. **The placement pass maintains it by choosing which frame to hand back**, not by
arithmetic on a virtual address and not with a first-touch counter. It is worth checking on
every run rather than reasoning about, because a violation is invisible in aggregate
performance — it looks like a slightly slower run, never like a failure.

---

## 5. Using it

Sections 5.1 to 5.3 state what a program must do to run well on this machine: how to shape a
unit of work, how to size it against the register file, and how to declare the pages its data
lives on. Section 5.4 states how to read what the machine reports back. The first three are
not advice — each is either enforced at admission or paid for at run time — and the fourth is
the difference between a measurement that means something and one that does not.

### 5.1 What to offload

**The rule: a unit of work must own the data it touches.** Every other rule in this section is
a consequence of it. An invocation runs on the tile that owns the addresses it references, and
when it reaches for an address that tile does not own it migrates. Ownership is therefore not
a stylistic preference; it is the property that decides whether a function runs in one place
or spends its life in transit. If a function discovers work it cannot carry out itself, the
unit is shaped wrong: it does not own the data it discovered. Reshape it. Do not hand the work
to a second invocation.

**Extend, never fan out.** A function that continues into another function is a **successor**:
the context carries forward, the FTU entry is inherited rather than allocated, one becomes one,
and the extension can never be refused. A function that creates a second live invocation is a
**spawn**, and spawns of spawns are unbounded by construction; nothing makes them safe without
a tracking unit on every function core *and* a depth bound, and a depth bound of one is a
constraint no program can honour. The instruction set therefore provides extension and provides
no spawn.

**The right shape.** A well-shaped function is a small hot loop that does a non-trivial amount
of work and stays planted on one tile for most of its runtime. Two shapes are wrong, at
opposite ends: a function that migrates constantly, and a function that exists for a single
instruction before retiring. Nothing requires a program to use one function — functions may be
heterogeneous, and a workload may be split into sets of functions with different shapes and
different data.

**Two forces bound the size of a unit.** Too fine, and the fabric dominates: a dispatch and a
return cost a hop each, and every invocation occupies a context slot from the moment it arrives
until it ends. Too coarse, and one invocation monopolises a slot for its whole duration. The
machine holds many contexts precisely so that no single one of them matters, and a unit of work
carrying a power-law hub's worth of edges defeats that. Chunking a unit by quantity of work
rather than by object is therefore mandatory on power-law inputs — and harmful on inputs with
natural locality, where it splits a unit that was already local.

**The shape that works: own a range and pull, rather than own a scattered set and chase.** A
range-owning function has four traits a chasing function does not. Every value it reads and
writes lies inside its own tile's slice, so its accesses are local for the whole invocation. It
claims what it discovers instead of handing a list back. Ownership removes the atomics — an
item belongs to exactly one invocation, so with aligned ranges a bitmap word belongs to exactly
one invocation too, and a plain read-modify-write in the clear is safe where a compare-and-swap
would otherwise be needed. And it returns one word in the register file, which is cheaper for
the host than a list the host must walk with memory traffic that competes with the work the
machine was offloaded to do.

**Placement alone is not a strategy.** Partitioning a layout by index helps only if the data
has locality in that index; on an input whose edges are essentially random with respect to
vertex identity, a partitioner has nothing to partition on. Extracting value from placement
needs one of three things: reordering the data so that locality exists in the index, a workload
that has natural locality already, or a different unit of placement — for a graph, co-locating
edges rather than vertices.

**Natural units of work, by kernel.** The unit named in each row is the one that owns what it
touches; the third column is what that choice costs the program around it. A **fire-and-forget**
invocation is one forked in a form whose return can never be read: it closes on its
acknowledgement rather than on a join, and it is the right form exactly when the result of the
unit is a memory effect the host will observe by ordinary loads.

| kernel | natural unit of work | what it costs |
|---|---|---|
| BFS, top-down | one vertex's neighbour scan, chunked by edge count | chunking is mandatory on power-law inputs and harmful on inputs with natural locality; the neighbour claim is a real read-modify-write, so this is the kernel that exercises local atomicity |
| BFS, bottom-up | a range of vertices, pulling each vertex's neighbours | the shape that works: local reads, no list handed back, one word returned |
| PageRank, pull | one vertex's gather | chunking needs a reduction, which a fire-and-forget invocation cannot express and which puts work back on the host. One vertex per invocation is the honest unit |
| connected components | a chunk of edges, fire-and-forget | hooking is a minimum-update with no value the host consumes, so nothing has to be joined. The best fit of the kernels listed here |
| SSSP, delta-stepping | a chunk of the current bucket | returns are needed: a relaxation can insert into a later bucket, and the host owns the bucket structure |
| betweenness centrality | as BFS, in both phases | the backward accumulation depends on the completed forward sweep, so the host enforces a barrier between them |
| triangle counting | one edge, intersecting two adjacency lists | the worst case for ownership: it touches two vertices' neighbour lists, so it migrates unless both endpoints are co-located. A partitioner that co-locates edges rather than vertices changes this kernel's answer entirely |

### 5.2 Sizing a function

**The budget is 512 bits plus scratch, counted in bits** (§3.4). It is not a count of
registers, not a count of values, and never a charge of a full 64 bits for a narrow value.

**How to spend it.** The 512 bits carry two complete tilings, and a name in either tiling is
*direct access*: one instruction reads or writes the whole slice.

| what the function holds | how it is reached | bits charged | cost per access |
|---|---|---|---|
| a 64-bit value in a 64-bit name (`x8`–`x15`) | directly | 64 | one instruction |
| a 32-bit value in a 32-bit name (`x16`–`x31`) | directly | 32 | one instruction |
| a value narrower than a name, or several values sharing a name | packed, and reached by shift-and-mask through a spare name | its own width | a shift and a mask to read; a shift, a mask and a combine to write |
| a constant zero | `x0` | 0 | one instruction |

Eight values fit directly as 64-bit names and sixteen as 32-bit names, and that is a statement
about **names**, never about how many values may be live.

Three consequences a compiler writer meets immediately.

- **A function holding exactly 512 bits of data and packing anything does not fit.** Seven live
  64-bit values plus eight live bytes is 448 + 64 = 512 bits of data; the bytes are packed, so
  the instant that touches one of them stands at 512 + 64 = 576 against 512, and the function is
  inadmissible. Twelve live 16-bit values plus five live 64-bit values is 192 + 320 = 512 and is
  inadmissible for the same reason. Both become admissible the moment the function frees one
  64-bit anchor.
- **The rejection is arithmetic, never a count of values.** A function holding a hundred live
  bytes with room to stage them is admitted. Nine live 64-bit values is 576 bits and is refused;
  nine live 32-bit values is 288 bits and fits with room to spare.
- **Bin feasibility is a capacity rejection like any other.** Because a packed field lies
  wholly within one 64-bit anchor (§3.4), the live values must also *fit* into eight 64-bit
  bins with no field split across a bin, as packed by first-fit-decreasing on width. The packer
  is named by the test, so the answer is exact with respect to the test even where some other
  packing of the same values would have fitted.

**What makes a function inadmissible in practice: it spills.** A function core has a register
file and no stack. An argument a host toolchain's calling convention would place in memory
cannot be read, because there is nowhere to read it from, and any store to a stack slot is a
spill. The stock eight-argument register aperture of such a convention is a toolchain artefact
and not the machine's limit — the machine defines no argument mapping at all (§3.4), so a
function is free to declare a layout that keeps nine 32-bit arguments, 288 bits, inside the
context, and that function passes.

**The placement pass runs before admission and must fail closed.** First-fit-decreasing by
width at each birth, plus a disjointness check over the whole placement, with relocation and
repacking moves permitted: compaction costs one instruction per move, repacking costs the
shift-and-mask sequence priced above. The pass proposes a listing; admission (§3.4) is the test
on it. When the pass cannot produce a layout it must not admit the function: it may retry with
bounded backtracking, or it may reject, and it may not guess.

**What to do when a function does not fit: split it into a chain of successors.** Each link is
admissible on its own, the FTU entry is inherited rather than allocated, and no link in the
chain can be refused. The handle stays valid across an arbitrary chain, and whatever the last
link returns is what the join receives.

### 5.3 Page-type choices, and when to duplicate

**The decision procedure, in order.** Ask the four questions in this sequence and take the
first type that answers yes.

| ask | type | what it costs |
|---|---|---|
| Is it read by every core and written by nobody after construction? | **DUPLICATE** | `N·G` bytes of physical memory for `G` of writeable data; the object must stop being written |
| Does it have an owner, and should it be spatially local to one function core? | **GRAIN** | the object is reachable without migration only from its own tile |
| Is it NMFC data with no owner that wants bandwidth? | **STRIPED** | one contiguous `N·G` extent, claimed the way a huge page is claimed, and failing the way a huge-page allocation fails |
| Otherwise — ordinary host data, or a small hot structure every core reads | **HOST** | none beyond the conventional mapping |

**Duplication is an option a program declares, never the baseline**, and aggressive
duplication is undesirable as a default even where it helps a particular kernel. What it costs
is `N·G` bytes for `G` bytes of writeable data and the requirement that the object stop being
written; what it buys is that the object is local on every tile, so no invocation migrates to
reach it. What it does *not* buy is a benefit measured in migrations avoided: a duplicated
object multiplies the footprint competing for capacity in every slice at once.

**Why a small hot shared structure wants HOST pages.** At grain-sized pages a structure smaller
than `G` lands entirely on one tile, and every invocation that reads it has to migrate there. A
`4 KiB` block-interleaved page spreads the same structure across every channel instead, which
is the conventional behaviour and the right one for an object with no owner and no bandwidth
demand of its own.

**And the cost of STRIPED is its contiguity.** A striped object is one contiguous `N·G` extent,
so its allocation fails on fragmentation exactly as a huge page's does. For an object that has
an owner, prefer GRAIN, which needs one free grain anywhere.

### 5.4 Reading the machine

**Migration rate is evidence about placement, not a cost** (§2.6). The two tests — the latency
budget, in migrations per instruction, and the legitimacy ceiling, in migrations caused by
fetch or translation and in migrations per memory operation — answer different questions and
are not interchangeable. Never report a pass without naming which test, and, for the ceiling,
which clause. A run can pass the ceiling's arithmetic comfortably while missing the budget by
orders of magnitude, and reporting a bare "measured pass" is how a budget comes to look
satisfied when it is not. The budget's own denominator deserves the same care: it is
comfortable only if what a context pays *after* it lands — a walk per distinct page it then
touches, on a shared TLB that is cold for it — is charged honestly.

**A full tracking unit is not evidence that the tracking unit binds.** Its entries may be full
of *completed* results waiting to be joined, in which case the constraint is the host's
consumption rate and the shape of the fork/join code — and a larger unit makes that worse, not
better, by enlarging the backlog. The measurement that separates the two cases is the split of
occupancy into **outstanding** versus **returned-and-unjoined**; two further counters, dispatch
stalls and peak live bodies, answer it in a single run. The falsifier is structural: if the unit
were the cap, the tiles would be piled with contexts.

**Occupancy is the instrument, and the tile is usually not the constraint.** Ablation is the
worse instrument, because a sweep says a resource was not the constraint only by elimination
and only for the resource that happened to be swept. Three rules govern the counters
themselves.

1. **Three counters per structure, and a fourth where entries can hold finished work:** entries
   live summed over cycles (the mean), the high-water mark (the peak), and the cycles anything
   was outstanding at all (the denominator). The third matters because a program that offloads
   a phase and then verifies on the host will otherwise be averaged over an idle machine.
2. **Time-weight the occupancy, or the machine looks busier than it is.** A structure sampled
   once per unit of work it performs — rather than once per cycle of elapsed time — is averaged
   over its busy cycles only, and reports a machine far busier than it was. Weight by elapsed
   time. Count incrementally as well: rescanning a large context array every cycle to maintain a
   mean and a peak can cost more than the work being measured.
3. **Residency, occupancy and throughput are one measurement in three units, tied by Little's
   law, and none can explain a change in the others.** **Residency** here is the time one
   context spends in a slot, from the moment it is admitted to the moment the slot is released
   — the quantity §5.4's function-core row asks for as a mean and a P99, and the one Little's
   law ties to that core's occupancy and its completion rate. A ratio of two of the three is not
   evidence — its numerator is the thing being explained. What does attribute is a sampled
   breakdown of where a context's cycles actually go, and a causal test that changes the
   suspected resource and sees whether the effect moves. Before dividing by a latency, ask
   whether that latency is a property of the device or a product of the occupancy being
   explained.

**Distinguish a slow channel from a starved one, because they produce identical symptoms** — a
deep queue, high latency, and throughput that does not respond to queue size. The discriminator
is a replay: take the tile's own DRAM address stream and drive it through the memory device on
its own, at full rate, with the same device timings, the same address-to-bank-and-row mapping
and the same request scheduler the full run used, so that the only thing removed is the rate at
which requests arrived. Replay throughput much greater than the simulation's means the channel
is **starved** and the memory system was never the limit; replay
throughput close to the simulation's means the device is already at its ceiling on this stream,
the memory system **is** the constraint, and only then is a memory-side knob worth touching. A
corollary worth stating on its own: row-buffer hit rate is not a proxy for bandwidth, since a
scheduler change can raise the hit rate and lower throughput at the same time.

**Two axes, and never optimise both at once.** The first is the hardware's achievable
performance under ideal conditions; the second is a given workload's ability to use it. Two
programs are involved and they must be named: the **reference algorithm** is the kernel as it
would ordinarily be written for a conventional machine, and the **decomposed program** is the
same work restructured into invocations for offload. Three cycle counts follow — the reference
algorithm, the decomposed program with the function cores off, and the decomposed program with
them on — and the pair of results closes arithmetically over them. **Architecture gain** is the
decomposed program with the cores off over the same program with them on: what the machine buys
on identical work. **Algorithm penalty** is the decomposed program with the cores off over the
reference algorithm: what the decomposition costs before the machine helps anything. **Net** is
the reference algorithm over the decomposed program with the cores on: what the user gets. The
identity `net = architecture gain ÷ algorithm penalty` closes because the common term cancels. A machine can deliver a large architecture gain and a net loss, and
that is a result about the decomposition, not about the hardware. Before a run's numbers are
quoted as evidence *about the hardware*, three questions gate them: what fraction of the work
left the host, how many invocations the tiles held at once, and whether the host stalled
waiting for dispatch.

**The claim shape.** The machine is compared against the reference algorithm doing the same
work on a standard core: the same input, the same starting point, the same set of items
reached. Never against a weaker algorithm — that condemns hardware that is working. Three
obligations ride with every quotation. It names which core model produced it, since core models
differ in whether a context keeps issuing past an outstanding load and therefore differ in
memory-level parallelism at the same context count. It carries the **branch-honesty caveat**: a
model driven from a recorded trace replays control flow that has already been resolved and so
never mispredicts, which the architecture's function core does — it has a real branch target
buffer with one bimodal bit and pays a refill when that bit is wrong (§2.2) — so such a model
must charge a fetch bubble in its place, and the sensitivity of the result to the size of that
bubble must be reported beside it. And it never pairs an instruction ratio with a cycle ratio in
one sentence, because the two are not each other's inverse.

**What the machine must report.**

| group | what it must report |
|---|---|
| **function core** | invocations completed; cycles by context state, over `FREE`, `READY`, `RUNNING`, `BLOCKED` and `DONE`; mean and P99 residency; contexts occupied; issue-slot utilisation; migrations in and out; atomic conflicts; instruction- and data-cache hit rates; achieved memory-level parallelism, per context and per core |
| **translation** | shared-TLB hit rate **by page size** — the `4 KiB`, `G` and `N·G` arrays separately — and split code against data; walk count and latency distribution; remote-walk rate; translation cold-start cycles after a migration; translation cycles as a share of context blocked time |
| **mapping and allocation** | allocations **by page type**; allocation failures by page type; how much `N·G`-aligned contiguity remains in the physical extent space; the largest allocatable run per tile; spill rate; per-tile free-frame imbalance. A spill and an out-of-memory are **warned, never fatal** — an implementation that aborts has destroyed its own diagnostic |
| **placement** | invocations per tile under each policy; migration rate |
| **fabric** | messages by class; queue occupancy; link utilisation; back-pressure stalls |
| **tracking unit** | offloads issued; in-flight mean and maximum; cycles stalled on back-pressure; fire-and-forget share; **closures by kind** — at a join, at a fire-and-forget acknowledgement, by fatal-fault teardown, and by `KILL`, with the last two counted separately because they take the same path and say opposite things about the run; kills that found no entry; and both mismatch counts |

Two of these rows carry more weight than their length suggests. The **remote-walk rate** is the
instrument for the legitimacy ceiling's first clause, and a rate needs no assertion to be
reported, so reporting it makes the clause measurable without gating anything. And **`N·G`
contiguity** is the only instrument in the list that measures the striped-allocation condition:
a machine can report a large allocatable run on every tile and still have no `N·G`-aligned
extent free.

---

## 6. Configuration

None of the values in this section is part of the architecture. The design states the mechanism
and the constraint that binds it; the table states what one simulation is configured to, so
that a measurement can be reproduced and two measurements can be compared. A value here is a
point in a study, never a property of the machine. Where a mechanism in sections 1 to 5 needed
a magnitude, it named the constraint; this is where the numbers that satisfy those constraints
live, and they live nowhere else.

**Selected simulation values.** The middle column is the part that binds an implementation.

| quantity | constraint that binds | selected simulation value |
|---|---|---|
| **`N`** — tiles = DRAM channels = LLC slices = memory controllers = function cores | any `N` ≥ 1; a power of two, so that the tile index is a contiguous bit field. `G` is a function of `N`, so changing `N` moves the page size | 4 |
| **`W`** — barrel pipe width | `C ≥ W(Dp + L/I)`. Nothing else fixes `W` | 4 |
| **`Dp`** — pipe depth, and therefore the re-issue delay | the same inequality; `Dp` is the re-issue delay in cycles | 8 |
| **`C`** — contexts per function core | `C ≥ W(Dp + L/I)` is the floor; above it, `C` is a capacity study | 1024 |
| **host cores** | multi-core by construction — private L1I/L1D/L2 per host core, a shared LLC across cores. A single-host configuration cannot demonstrate saturation, and every result on one is a result on an under-driven machine | 1 |
| **fabric link width** | the same magnitude as a modern processor's. A migration is 72 bytes and the line fill it replaces is 64, and the two are alternatives and never both, so the width must be one at which that parity is expressible | 32 B/cycle |
| **fabric hop latency** | one fabric carrying three classes. The architecture fixes that there is one fabric and how its classes are arbitrated; it fixes no topology, so both the topology and the per-hop latency are an implementation's | 8 cycles |
| **fabric queue depth** | one queue per destination and per class, never one shared queue | 128 entries |
| **`G` / `grain_bits`** | `G = row_bytes_per_channel × banks_per_channel × total_channels`, where `banks_per_channel` is the product of every organisation level between the channel and the row — ranks and bank groups included — and `total_channels` is device channels. Any geometry inside the physical address space must work. Derive `G`; never hand-set it | 1 MiB (`grain_bits` = 20) |
| **DRAM device geometry** | the same rule: arbitrary banks, ranks, bank groups, rows, columns and channels, with no count locked, and every geometry supportable inside a 48-bit physical address space | DDR5 — 2 ranks × 8 bank groups × 4 banks = 64 banks per channel; 65,536 rows × 1,024 columns × 4 B = 4 KiB per row per channel; 4 channels. 16 GiB per channel, 64 GiB in the machine |
| **`block`** — the cache line, and therefore the granularity a HOST page is interleaved at, the size of a FILL message, and the transmit unit of a context | one quantity, not three: the line the hierarchy moves is the unit the fabric fills and the unit a 512-bit context occupies. `log2 G ≥ log2 block` | 64 B |
| **LLC slice size** | a modern LLC divided by its channel count — the same magnitude as a modern processor's per-channel share | 512 KiB (512 sets × 16 ways × 64 B) |
| **LLC slice banking** | a cache bank and its DRAM bank are the same partition of the address space, so the slice banks on the channel's per-rank bank count. The count follows the device and is not a design constant | derived from the device: 32 |
| **tracking-unit entries** | it refuses rather than evicts, so it must be large enough not to bound the machine artificially — and a full unit is still not evidence that it binds | 1024 |
| **function-core I$ / D$** | separate and never unified, both heavily banked and address-sliced. Capacity belongs in the LLC slice, not here | I$ 16 sets × 4 ways; D$ 64 sets × 8 ways; 64 B lines |
| **atomic-table capacity** | a full table is either unachievable by construction or sleeps the context until an entry frees. It must never become a resource held while waiting for a resource | sized from experiment; no value fixed |
| **atomic hand-off chain bound** | a coherency guarantee, not an optimisation: after a fixed number of hand-offs the word returns to the data cache whether or not anyone is waiting | 8 hand-offs |
| **placement-policy epoch and gates** | start from the common published values and tune against measurement. The policy's rules are the design; the numbers are not. The epoch is the decision window and the reporting interval, one interval; the private threshold is the pull-dominance share above which rule 1 calls a grain private; the imbalance threshold is the slack factor on a tile's fair share of window traffic that rule 4 withholds a move above. What the pull threshold gates is not yet specified | epoch 100,000 migrations; private threshold 0.90; imbalance threshold 1.10; pull threshold 16 |
| **directory entries and sharer width** | an exact bit vector over host cores and tiles, inclusive of every private cache it serves, back-invalidating on eviction, and sized so that it never evicts an entry in order to admit one | one bit per host core plus one per tile, at a scaling target of up to 32 tiles |
| **page-table levels and base page size** | a multi-level radix table with the size class carried in the entry, as any multi-size table carries it, terminating at `4 KiB`, `G` and `N·G`. The level count is standard; the geometry that produces the two device-derived terminators is a design item and is not yet fixed (§2.4) | 5 levels, 4 KiB base page |
| **the three page sizes** | exactly three size classes — `4 KiB`, `G`, `N·G` — of which only `4 KiB` is a constant; the other two follow the device | 4 KiB / 1 MiB / 4 MiB |
| **mode-bit position** | `mode_bit ≥ log2 G + log2 N`, strictly above the tile field in both layouts — bit 22 at the geometry above. The convention is one position above the top of DRAM, which at 64 GiB of DRAM is bit 36 and makes the carried address exactly one bit wider than DRAM. The inequality binds; the convention does not | bit 38 — two positions above the top of DRAM, so it satisfies the constraint and departs from the convention |
| **the function-entry marker word** | the architecture fixes that a host core refuses function text **at fetch** (§3.4); it does not fix what fetch refuses on. Where an implementation uses an entry marker, the marker is one word, it sits at the function's first instruction, a function core executes it as a no-op, both host models refuse it, and a fork checks it before it dispatches. The value is an implementation's and carries no meaning | `0xe000000b` — one variant of a reserved encoding group |
| **`KILL`'s field value, and `JOIN`'s answer encoding** | field encodings are implementation-defined (§3.1): the architecture fixes the instruction count, the group membership, and that `KILL` takes a slot in the reserved space beside `RESUME`. `JOIN`'s closure must be a **well-formed answer a program can test**, which makes the answer a field rather than a flag — a killed invocation must be distinguishable from a clean one | `KILL`: `funct7` = `0x60`, `funct3` = the single-source form, `rd` = `x0`, `rs1` = the handle. `JOIN`: `OK` = `0x1`, `ERROR` = `0x2`, so a killed entry answers `OK\|ERROR` = `0x3` with a zeroed register file |
| **clocks** | nothing in the design assumes the function core runs at the host's rate. If it does not, every cycle comparison says so | 4 GHz (250 ps) for host cores, function cores, fabric and caches |

**What is not configuration, and therefore has no row above.** Each of the following is a
property of the architecture, identical in every configuration, and a machine that changes one
is a different machine:

- the register map — `x8`–`x15` as the eight 64-bit context tiles, `x16`–`x31` as the sixteen
  32-bit context tiles, `x0` reading zero at any width, and `x1`–`x7` reserved and trapping;
- the eight host-side context registers, 512 bits each, per software thread;
- the 512-bit context, the 64-byte transmit unit, and the 72-byte migration payload;
- the number of page types and the number of page size classes, the rule that the mode bit
  follows the size class, and the replicate bit that separates the two types sharing a size;
- the fabric's three message classes and their arbitration order — coherence strictly first,
  migration and fill at equal weight;
- the MOESIF state set, and the directory's location at the L2-to-LLC boundary;
- the instruction count, the group membership, and which instructions are privileged.

The table above describes what can be configured. It is not a recommended configuration, and
nothing in it is tuned.

---

## A. Glossary

**Address space's owner** — the operating system or runtime that maps an address space and
chooses the frames behind it; the party that honours a program's page types and vtiles (§2.3).

**Admission** — the fatal build-time test on a compiled function body: subset legality, 512
bits of liveness plus scratch, a verified non-overlapping placement, and no reserved name
(§3.4).

**ASID** — address-space identifier; names which page table a translation belongs to, and tags
every entry in a tile's shared TLB (§2.4).

**Atomic table** — the one structure per tile that obtains, releases and passes atomics at the
granularity of the operand word (§4.3).

**Barrel core** — a core in which one context issues one instruction and then yields, so no two
instructions in flight can be dependent (§2.2).

**Block** — the cache line: the unit the hierarchy moves, the size of a FILL message, the
granularity a HOST page is interleaved at, and the size of one context (§2.4).

**Block-interleaved** — the mapping that spreads consecutive blocks across every tile; the mode
of `4 KiB` pages (§2.3).

**Cluster** — the grains of one vtile backed on one tile; the quantity a spill target is chosen
by (§2.4). Not a component.

**Component** — a set of grains united by co-access constraints, and the unit the placement
policy places (§4.4). Not a cluster.

**Congruence** — the property that a page is backed on the tile its declaration asked for: the
vtile's home, or the frame the address space's owner chose for an unhinted page (§4.4).

**`C`** — the number of context slots on one function core, bounded below by `W(Dp + L/I)`.

**`compact` / `expand`** — the pure inverse functions that excise the tile field from a physical
address on the way into a slice and reinsert it on the way out (§2.4).

**Compute tile** — a host core with its private caches and its fabric port. Never a memory tile.

**Context** — the whole architectural state of an invocation apart from its program counter:
512 bits, bit-packed, transmitted as one 64-byte block (§3.2).

**Context register** — one of eight host-side 512-bit staging registers, `c0`–`c7`, written and
read one 64-bit lane at a time by `CXW` and `CXR` (§3.2).

**Context tile** — a slice of the 512-bit context named by a register number: `d0`–`d7` are the
eight 64-bit tiles (`x8`–`x15`), `w0`–`w15` the sixteen 32-bit tiles (`x16`–`x31`) (§3.2).

**`Dp`** — pipe depth, which is the re-issue delay of a context in the barrel (§2.2).

**DUPLICATE page** — `G` virtual over `N·G` physical: one identical copy per tile, read-only by
construction. Function bodies, read-only data and the page table (§2.3).

**Fabric** — the single L2-to-LLC interconnect, where the address partition is taken and where
the one coherence directory sits (§2.1, §2.5).

**Function core** — the multi-context, in-order, non-speculative core on the slice's side of
each tile's fabric interface (§2.2).

**Function tracking unit (FTU)** — the host-side structure that issues handles, holds each
outstanding invocation from fork to join, parks its returned 64 bytes, and delivers its faults
(§3.1).

**`G`, grain** — the unit of physical allocation and tile assignment, and the NMFC data page
size; derived from the DRAM organisation, never chosen (§2.1).

**GRAIN page** — `G` virtual over `G` physical, on one tile (§2.3).

**Grain-partitioned** — the mapping that places a whole grain on one tile; the mode of `G` and
`N·G` pages (§2.3).

**Group** — an aligned run of `N` grains, the granularity at which the two mapping modes line
up and therefore the unit that must be homogeneous in mode (§2.3).

**Handle** — the name of one outstanding invocation, returned by `FORK` and consumed by `JOIN`,
`JOINQ`, `RESUME` and `KILL`. It names an FTU entry, and only while that entry has not been
recycled (§3.1, §3.5).

**Hart** — one hardware thread of a host core: the thing an instruction stream runs on, and what
an FTU entry records as the party to trap (§3.1).

**HOST page** — `4 KiB` virtual over `4 KiB` physical, block-interleaved: the ordinary mapping
(§2.3).

**Invocation** — one unit of work on a function core: a program counter and a 512-bit context,
with no stack (§1).

**Lane** — the 64-bit granularity at which `CXW` and `CXR` move bits on the host side. Not a
tiling of the context (§3.2).

**Migration** — an invocation moving to the tile that owns an address it needs and resuming
there: 72 bytes, the slot released at departure (§2.6).

**Mode bit** — the physical-address bit, stamped at allocation from the page's size class, that
says which mapping a frame uses; stripped at the DRAM port (§2.3, §2.4).

**Park** — what a context does across a recoverable fault: it keeps its slot and stays blocked
until a `RESUME` (§3.5).

**Pull dominance** — the share of a grain's migration pulls credited to its busiest destination
tile, over the policy's window, excluding each invocation's first hop (§4.4).

**`N`** — the number of tiles, a power of two.

**Replicate bit** — the page-table-entry bit that separates a DUPLICATE page from a GRAIN page
and makes the frame field name a replica set's base (§2.3).

**Residency** — the time one context spends in a slot, from admission to the release of the
slot; tied to occupancy and completion rate by Little's law (§5.4).

**Retire** — of an invocation: to release its completion, which cannot happen until its stores
have landed (§2.2).

**Return bit** — the bit of `END` that selects whether the completion carries the whole 512 bits
or is an acknowledgement only (§3.1).

**Slot** — one context's state on a function core: one register file and one program counter.

**Siloing** — the concentration of one vtile's grains onto few tiles; the spill rate is the
statistic that says it has gone too far (§2.4).

**Spill** — a grain placed on a tile other than the one its vtile asked for; a broken
co-location, never a slower access (§2.4).

**STRIPED page** — `N·G` virtual over `N·G` physical: one contiguous extent, one grain per tile
(§2.3).

**Successor** — the invocation a `CONT` carries a context forward into; it inherits the FTU
entry and can never be refused (§3.1).

**Tile** — one DRAM channel, one LLC slice, one memory controller and one function core, as a
single object counted by `N` (§1).

**Vtile** — a compiled-in label naming a coherent set of pages: a relation saying that pages
belong together, never a location (§3.3).

**Window** — the bounded recent interval the placement policy gates on and reports over; also
called its epoch. Its length is configuration (§4.4, §6).

**`W`** — the barrel's pipe width: the number of duplicate pipes, each serving a different
context in the same cycle (§2.2).
