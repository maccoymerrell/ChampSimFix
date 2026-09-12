# Doing computer-architecture research in SST with Claude Code

This is a working guide for researchers who use the Structural Simulation
Toolkit (SST) and want to drive it with a coding agent. It covers both
directions of the work: **developing** a model — adding a component, changing
one, proving it does what you say — and **reverse-engineering** one you did not
write, such as the out-of-order RISC-V core that ships with SST. It also covers
the two things that decide whether any of it produces publishable numbers:
running simulations on a shared machine without wrecking it, and the statistics
that turn a few short simulations into a claim.

Everything here is drawn from one project's experience: a near-memory
accelerator built as a custom SST element, evaluated against a stock
out-of-order core, on a shared 72-physical-core machine, driven almost entirely
through Claude Code. Every rule comes with the reason it exists — what went
wrong before the rule, or what the rule saved. The rules are stated so that they
apply to any SST project; the project's own material appears only as **worked
examples**, marked as such, and a **Not generic** note flags anything of ours
that would not transfer.

Read the sections you need. They are ordered as the platform (A), then building
(B), then auditing (C), then running (D), then statistics (E), then the agent
(F), then a demo you can run (G).

---

## 0. What the setup is

Five pieces, of which two are SST's, one is yours, and two are the harness.

| piece | what it is | who provides it |
|---|---|---|
| **sst-core** | the discrete-event engine: components, subcomponents, links with latency, clocks, the event queue, the statistics system, the Python configuration model, partitioning and parallel execution | SST |
| **sst-elements** | the model library: memHierarchy (caches, coherence, memory controllers, DRAM backends), vanadis (an out-of-order RISC-V core) and other cores, miranda and prospero (request generators and trace readers), ariel (a binary front end), merlin/firefly/ember (network and MPI), and more | SST |
| **your element** | the thing you are studying, compiled as its own shared library and registered with SST, instantiated from Python beside the shipped components | you |
| **a functional front end** | something that executes the workload without modelling time, to produce traces, requests, or restart points for the timing model | you, or a shipped element |
| **the harness** | the agent, the rules it works under, and the machine governance: a limiter in front of the simulator, a place for output, and a campaign driver | you |

The important structural fact is the third row: SST is designed for additive
work. A custom element is a separate library that registers its components by
name, so your model sits beside memHierarchy's caches and vanadis's core in one
Python configuration without either tree being edited. That property is what
makes an agent safe to point at the code: it can add a component and a test
without touching the simulator everybody else's results depend on.

**Worked example.** In this project the custom element is a set of near-memory
"tiles", each owning a slice of the last-level cache, a memory controller and a
DRAM channel plus a small multi-context core; the functional front end is a
patched Spike that writes whole-program snapshots; the host processor, the cache
hierarchy, the coherence directory and the DRAM model are all stock
sst-elements. The agent's work is confined to the custom element, the workload
programs, the configuration scripts and the measurement tooling — plus, when the
audit demanded it, a branch of sst-elements for fixes to the shipped core.

**Not generic.** The Spike-based producer, the whole-program image format, and
the machine-wide limiter described in D are ours. Their *roles* are generic; the
implementations are not, and each section says what a stock install would use
instead.

---

## A. SST as a platform: what you get, where it is thin

### A.1 What the platform decides for you

Four things in sst-core will determine your results whether or not you think
about them.

**Links have latency, and that latency is the simulator's synchronisation
quantum.** Every link needs a non-zero latency; the minimum latency across a
partition boundary is how far ahead the two halves may run independently. This
is why a model built from many tightly-coupled components with sub-nanosecond
links barely speeds up under threads or ranks, no matter how many cores you own.

**A clocked component advances at whatever clock calls it, not at the rate its
own configuration file names.** This is the single most common source of
plausible-looking wrong timing in SST. If a device model performs one internal
step per call to its clock handler and you register that handler on a 1 GHz
clock, the device runs at 1 GHz — even if its own configuration describes a part
whose cycle time is 416 ps.

**Statistics exist only when you enable them, at the level you enable.** Every
statistic a component registers has an enable level; `enableAllStatistics` with
a load level below it silently returns nothing. Worse, enabling all statistics
on a component does **not** reach the statistics of components loaded into its
subcomponent slots — a prefetcher, a link manager, a TLB — so the numbers you
most want are the ones most likely to be missing.

**A statistic's name includes the slot its subcomponent was loaded into.** The
same counter in the same code reads `cpu:rocc.kills` under one host
configuration and `cpu:co_proc.kills` under another. A test that greps for one
name reads nothing under the other configuration — and reads it as zero, which
usually means "passes".

Those four are properties of the engine, so they apply to every SST project. The
last two have the same failure mode: a number that is quietly absent looks
exactly like a number that is quietly zero.

### A.2 How to read a shipped element

You will spend more time reading elements than writing them. The order that
works:

1. **`sst-info <library>.<Component>`.** It prints, from the element's own
   registration data: every parameter with its type, its documentation and *its
   default in brackets*; every port, including deprecated ones; every
   subcomponent slot with the interface it accepts; and every statistic with its
   units and enable level. This is the contract. Read it before the source.
2. **The defaults are the configuration you are publishing.** Any parameter you
   do not set is a decision someone else made, usually for a different purpose.
3. **The source file named by `sst-info`.** ELI output tells you which header the
   component was compiled from. Go there for the behaviour the parameter list
   cannot express: what the component does per cycle, in what order, and what it
   does not do at all.
4. **The subcomponent slots are where the interesting behaviour is delegated.**
   A cache's link management, its prefetcher, its replacement policy and a
   memory controller's timing backend are all slot-loaded. The component's own
   parameters tell you almost nothing about them.

Reading in that order costs an hour and routinely changes what you believe the
model does.

**Worked example, entirely generic.** `sst-info memHierarchy.MemController`
shows `backend` defaulting to `memHierarchy.simpleMem` — a fixed-latency memory.
A memory hierarchy configured without naming a backend therefore reports numbers
from a constant, not from DRAM. In the same listing, `mshr_num_entries` defaults
to `-1`, documented as "a very large MSHR", and `banks` defaults to `0`, "no
bank limits". None of those is wrong as a default; all three are wrong as an
unexamined baseline, because each removes a bottleneck real hardware has.

### A.3 Verifying what a model does rather than what its name says

A component named `BranchUnit` is not a branch predictor; a class named
`LoadStoreQueue` may be a FIFO. The name is a promise about the role, not about
the mechanism. Three checks, in increasing cost:

- **Read the per-cycle function.** For a core, the issue, execute and retire
  steps; for a cache, the request handler and the coherence transitions; for a
  queue, what is allowed to leave it and in what order.
- **Count something only that mechanism produces.** If the mechanism is real,
  some counter moves; if no such counter exists, that itself is the finding.
- **Write a microbenchmark that isolates it** (C.3) and move the parameter. If
  the timing does not move in the direction the mechanism implies, the mechanism
  is not there.

**Worked example.** In the shipped out-of-order core, three structures turned
out to be missing rather than mis-tuned: issue selected ready instructions from
the entire 352-entry reorder buffer (there was no scheduler and no issue queue,
so dispatch stalls were exactly zero, at any pressure); integer multiply ran at
the one-cycle integer-ALU latency (no multiplier as a distinct unit); and
loads and stores went from the scheduler straight into the queue with no
address-generation unit and no port limit. Each was established by reading the
per-cycle code, confirmed by a counter that could not move, and then priced with
a directed kernel.

### A.4 Where the platform is thin

This table is what to expect to build yourself. It is not a criticism of SST:
these are research models, and the gaps are where your contribution goes.

| you will probably have to build | why it is thin |
|---|---|
| an out-of-order core on par with a modern one | the shipped cores are research vehicles: expect gaps in the scheduler, the memory pipeline, branch prediction and translation |
| address translation with real cost | translation is commonly "free" by default, or priced without being performed |
| prefetchers you can defend | a few simple ones ship; published designs generally do not |
| absolute latencies you can source | many models take latency in *cycles*, so a figure is only a time once you name the clock |
| a way to reach the middle of a long program | cycle-accurate from the entry point is hopeless past a few billion cycles |
| statistics for the mechanism you added | your own counters are the only proof your mechanism does anything |
| machine governance | nothing stops you launching 200 simulators on a shared box |

Each row below is a section of this guide: the core and translation are C, the
latencies are A.1 and C.2, the program middle and the statistics are D and E,
and the governance is D.

---

## B. Developing a model

The loop is: design reviewed, test that fails first, build frozen, measurement
sampled, feature proved by its own counters, document written for a stranger.
Each step exists because skipping it cost something.

### B.1 Get the design agreed before code exists

For anything structural — a new component, a new protocol, a change to what the
machine *is* — write the design down, state the real alternatives with their
trade-offs, check the prior art, and get it agreed before writing model code.

**Why.** A wrong structural assumption is not a bug; it is weeks. In this
project two structural choices were reversed during review, before either was
built: an offload engine attached as a memory channel (rejected in favour of a
properly forked host core) and an identity-mapped physical address space
(rejected in favour of one virtual address space with real translation).
Reversing them on paper cost a day each. Reversing them in code, after
workloads and tests had been written against them, would have cost far more.
A literature check on TLB architectures for irregular workloads materially
changed the translation design, which is the second half of the same rule: a
design review is where prior art still has leverage.

**How it works with an agent.** Keep a single document that is the settled
design, with a numbered list of invariants and an explicit list of rejected
mechanisms; have the agent read it before designing anything (F.1–F.3). An
agent will otherwise happily re-derive a rejected mechanism from the code in
front of it, because the code is the most available evidence.

### B.2 A directed test that fails before and passes after

Every fix and every new mechanism lands with a test that (a) fails on the build
before the change and (b) passes on the build after, and asserts on the model's
own output or on a statistic rather than on a cycle count.

**Why.** Two distinct failures motivate this. First, a fix whose test was
written afterwards can be a fix for nothing: the reason to rebuild the tree with
the fix removed and reproduce the failure at the identical cycle is that it is
the only way to know the test is testing your change and not some neighbouring
defect. Second, a mechanism with no test stops being exercised silently — in
this project a coherence path (a write fanning out to duplicate pages) went to
zero coverage across every workload and every configuration when a loader change
stopped generating the only requests that reached it, and nothing failed.

**Two assertions worth writing routinely.** A suite that only checks answers
cannot tell a fast wrong machine from a fast right one, and a suite that only
checks that a program passed cannot tell a tested mechanism from a compiled one:

- **answers**: the program's own verification line, on every configuration, so
  that a protocol that is fast and wrong is caught;
- **coverage**: a statistic that must be non-zero, because the transition it
  counts must actually have happened, and a statistic that must be *zero*,
  because what it counts is a machine that has gone wrong.

**Worked example.** A coherence defect in the custom element lost writes when
several units of work wrote to different words of the same cache line. The
reproduction was 17 directed cases, each isolating one write type (plain store,
32-bit atomic OR, 64-bit atomic OR) under two dispatch patterns, at one, two and
four tiles. Loss was identical at all three tile counts, which proved the defect
was inside one tile and not in the fabric between them, and that single
observation removed most of the search space. The trace then named it exactly:
16 words held above the cache, one returned. The interface could only describe
one word, so the other 15 left with the line and could never be requested again.

### B.3 Fix at the cause, and treat a fix that serialises as provisional

The repair went into the message format — an acknowledgement that carries a
64-bit byte mask instead of one offset — not into a lock that stopped concurrent
writers. And the fix was checked for cost: the mask replaced a same-size field,
so the wire cost is zero.

**Why.** A correctness fix that serialises (a hold, a pin, a drain, a global
order) hides the defect and removes the optimisation the machine exists to
demonstrate. Such a fix is acceptable as a stopgap, but it must be labelled
provisional and replaced, or the machine quietly regresses to
worst-but-working. The corollary is honest about failure: one tempting variant
of the fix above — holding the line until its last word is returned — deadlocked,
and that is recorded next to the fix rather than omitted.

**And measure what the repair bought.** Removing this defect made the
previously-broken code path correct, and the whole traversal 1.5× *slower*,
because the phase that followed it inherited a dirtied array. Removing a
machine's defect removed a correctness barrier, not a cost. Publishing "fixed"
without that measurement would have been wrong in the direction that flatters.

### B.4 Freeze the build

Before a measurement campaign, record the identity of what you are measuring:
the branch and commit of every repository, and a checksum of every installed
element library. Re-record it whenever anything is rebuilt, and say which record
each number belongs to.

**Why.** SST loads element libraries from an install directory by name. A
rebuild between two points of a sweep changes the machine under half the
results, with nothing in the output to say so. The frozen record also forces an
honest statement of what changed: in this project each library change was listed
with the gate that holds it, which is how the reader knows the difference between
"rebuilt" and "changed".

The demo in G does exactly this in its first step, in about a second.

### B.5 Prove the feature with its own counters, never with a delta alone

A new mechanism lands with: a microbenchmark that exercises it; the mechanism's
own counters (issued, useful, late, useless; hits, misses, walks, latency); a
statement of where in the pipeline it acts; and a comparison against a reference
implementation where one exists. A surprising *null* result gets explained with
counters before it is reported.

**Why.** A cycle delta on your project's workloads is not evidence that a
mechanism works: it conflates "does nothing here" with "does nothing".
Three worked examples, all from this project, each of which would have been
misreported without counters:

- Two prefetchers were added and the workloads barely moved. The counters said
  why: 55–58 % of the L1 prefetcher's requests crossed a page boundary and were
  dropped by a translation buffer, and the L2 prefetcher's "useless" count was
  dominated by lines the L1 prefetcher had already brought in. Fixing the first
  and correcting the accounting of the second changed a purpose-built
  benchmark's speedup from 1.22× to 1.40×. The null result was an instrument
  fault, twice.
- A front-end instruction prefetcher moved the project's workloads by 0.1–0.3 %
  and a purpose-built instruction-footprint benchmark by 3.09×, with the
  mechanism's own counters — fetch stalls down 73.6 %, 99.57 % of prefetches
  later demanded — and an unchanged instruction-walk count proving it was not
  cheating on translation. Both numbers are the result; the small one is not a
  disappointment, it is the scope.
- A page-walk cache cached an *absent* upper-level entry as if it were valid,
  which poisoned every address under that prefix and made one workload look
  4.4 % faster than it was. Only a counter for "the walk found no entry" and a
  cross-check of the walker's answer against the model's own mapping caught it.

**The habit that catches the worst class of error:** add a counter whose value
must be zero if the mechanism is right — a mismatch between two paths that
should agree, a request beyond the address space, a walk that found nothing —
and gate the suite on it being zero. A counter that must be non-zero proves a
mechanism ran; a counter that must be zero proves it was not lying.

### B.6 Write it for a reader who was not there

Every design note, measurement report and results page is written as a textbook
section: what the thing is, how it works, what was measured, what the numbers
mean, what to conclude. No conversation, no internal names, no quotes from the
process. Every table gets a sentence before it saying what it shows and one
after saying what it means. Every term is defined on first use.

**Why.** Documents full of internal references are unreadable by the people who
have to evaluate the work, and that includes you in three weeks. This guide
follows its own rule.

---

## C. Reverse-engineering a model you did not write

You will inherit models: a core, a cache hierarchy, a DRAM backend, a network.
If your claim is "X is faster than a conventional machine", then the model of
the conventional machine is half your result, and you are responsible for it
whether you wrote it or not.

### C.1 Audit against a reference, feature by feature

Pick two or three real machines with published microarchitecture. Go through the
model structure by structure and, for each, record: what the model does, what
the references say, the source of each reference figure, and one of three
verdicts — on par, mis-tuned (a parameter fixes it), or absent (no parameter can
fix it). Where the references disagree, say which one you followed.

**Why.** Without an explicit reference, "on par with a modern core" is an
opinion, and every speedup you publish is measured against an unnamed baseline.
With one, the audit produces a work list instead of a worry.

**A rule about what to do with an absent feature:** if a missing feature might
substantially affect performance, flag it for implementation. Do not record it
as "not modelled" in a limitations paragraph. A limitations paragraph is where a
baseline goes to be weaker than the hardware it stands for, and every speedup
measured against it is inflated by exactly the amount you declined to model.

**Worked example — the audit of a shipped out-of-order core.** Against three
published cores, the configuration was mostly on par (6-wide, 352-entry reorder
buffer, 48 KiB L1D at 5 cycles, 2 MiB L2 at 16 cycles). What it was missing was
structural, and each item became work:

| finding | what the model did | what it became |
|---|---|---|
| no scheduler distinct from the reorder buffer | issue selected from all 352 entries; dispatch stalls exactly 0 | bounded issue queues: 97 integer, 64 floating-point, 108 memory, 97 branch |
| no multiplier | integer multiply at the 1-cycle ALU latency | one unit, 3-cycle latency |
| no address-generation units or ports | memory throughput limited only by queue issue width | 3 AGUs, 3 load ports, 2 store ports |
| translation free | addresses rewritten, nothing charged | two-level TLBs, a page-walk cache, real walks (C.2) |
| no branch direction prediction | a branch-target buffer only: no direction, no history, no return-address stack | a tagged predictor with speculative history and repair |
| load/store queue strictly in order | only the front of one queue could issue; no load could pass an older store | separate queues, speculative loads, forwarding, violation replay, a dependence predictor |
| a 32-bit add done at 64 bits | one instruction decoded to the 64-bit template | fixed, plus a conformance test for all fourteen narrow forms |

The effects were large and not all in the flattering direction: adding the
scheduler, multiplier, ports and translation *cost* the host 0.6–15.4 % cycles
and dropped its IPC 4–18 %, which raised the measured speedups slightly; adding
the branch predictor made the host much stronger and cut some speedups by more
than half (one workload fell from 26.2× to 9.3×). Those latter numbers are the
reason the audit is not optional. A branch predictor absent from the baseline was
inflating a headline result by a factor of nearly three.

### C.2 Distrust defaults

Any parameter you did not set is a default. Go through the ones that carry
timing and set them deliberately, from a source, or confirm the default is what
you want. These four are worth checking in every SST project:

| what to check | the failure it causes |
|---|---|
| the memory controller's timing backend | left unset, memory is a fixed-latency model, and every "DRAM" number is a constant |
| which clock advances each device model | a device stepping once per clock call runs at the caller's rate, not its own |
| queue and MSHR sizes documented as unlimited | removes a bottleneck real hardware has, silently |
| latencies expressed in cycles | a cycle count is a time only once you name the clock; a 13 ns cache is 13 ns at any frequency |

Each of these has bitten this project, and two are worth spelling out.

**Worked example — a DRAM device running at 42 % of itself.** The DRAM model
advances one device cycle per call to its clock handler. It was being called by
the memory controller's 1 GHz clock, while the part being modelled had a 416 ps
cycle time. The part therefore ran at 42 % of its own rate, and a 32-bit DDR5
subchannel's ceiling came out at 8.0 GB/s instead of 19.2. The fix is to derive
the device's clock from the device file's own timing rather than inheriting the
controller's, and a suite gate now compares the device's clock-call count
against channels × elapsed time ÷ cycle time, so it cannot drift again.

**Worked example — latency in cycles versus latency in time.** Expressing cache
and fabric latencies as cycle counts of whichever component they land on means
the modelled machine changes whenever a clock changes. The rule adopted was to
state each latency in nanoseconds, sourced from published figures for a cache of
that size, and convert to cycles with the owning component's own clock in one
helper. A last-level hit is then 18 ns because published caches of that size are
13–25 ns — a number a reader can argue with, which is the point.

### C.3 Microbenchmarks that isolate one feature

For each structure you add or suspect, write the smallest program whose time is
dominated by that structure, and check that moving the parameter moves the time
by the amount arithmetic says it should.

**Why.** On a real workload every structure's effect is mixed with every
other's, so a real workload cannot tell you whether a feature works. A kernel
can, exactly:

- a long dependent chain of multiplies: changing multiply latency from 1 to 3
  cycles lengthened a 20,000-multiply chain by exactly 40,000 cycles, and 4 to 7
  by exactly 80,000;
- eight independent loads from one base register: 3 load ports gave 2.62
  loads/cycle, 2 gave 1.82, 1 gave 0.95;
- 256 pages touched twice with a 16-entry second-level TLB: 793 walks and 2,379
  walk reads, exactly three reads per walk, and more than double the kernel's
  time;
- two code regions twice the size of the instruction cache, one straight-line
  and one call-heavy, for an instruction prefetcher.

Two cautions learned the hard way. A microbenchmark can measure the wrong thing:
the first version of the instruction-cache benchmark used a single accumulator
chain and moved 0.03 %, because it was measuring the adder, not the front end;
four independent chains fixed it. And a microbenchmark's *own* result is part of
the finding — an instruction prefetcher worth 3.09× on a large code footprint
and 0.1–0.3 % on the project's small-footprint workloads has told you both its
mechanism and its scope.

Also beware of features that mask defects. One suite failure disappeared when
the four new structures were turned on. That is masking, not fixing: the timing
moved enough to hide a race. An earlier cache-latency change had done the same
thing. Record it and chase the defect separately.

### C.4 Instrument, don't ablate

When you want to know what limits performance, add counters and read them. Do
not sweep sizes of every structure to see what moves.

**Why.** A knob sweep costs N simulations and answers a question about knobs,
while occupancy counters cost one simulation and answer the question about the
machine. In this project an agent once spent half an hour sweeping a ring-buffer
size on a configuration that had already been ruled out; the occupancy counters
in the same run already said the structure was never full. Ablation is also
dangerous in the other direction: a knob sweep produces a best setting for the
workload you ran, which is a tuning result masquerading as a design result.

The instrumentation that repays itself in almost any accelerator study:

- occupancy of every queue, split by *state* — how many entries hold work in
  flight versus work finished and waiting to be collected;
- how long an entry waits in each state;
- the cost of the coordination itself: cycles and instructions per invocation of
  the offload handshake;
- cycles the program spends idle while finished work exists.

The reason for the split is that it identifies the bottleneck without any sweep:
a tracking structure full of *outstanding* work with the accelerator busy means
the accelerator is the limit, while the same structure full of *finished* work
with the accelerator idle means the host is — the program is idling on returned
work, or the per-result overhead is too large, or the offloaded functions are too
small to be worth dispatching. Those are program-side defects that no amount of
machine tuning will fix, and without the split they look identical to a busy
machine.

### C.5 Profile the simulator before accepting its cost

If a configuration or a change makes the simulator much slower, profile it and
try to fix it before turning the slowdown into a planning constant. Report
"intrinsic" only after an attempt has failed, and say why.

**Why.** Slowdowns are usually your own inefficiency, not a property of the
machine being modelled. Two examples:

- A serial-link configuration ran 2.7–4.6× slower than its parallel twin, and
  the profile put 72 % of wall time inside the DRAM library against 18.6 % for
  the twin. The cause was idle channels being ticked every device cycle — a
  simulator artefact with no counterpart in the hardware. Reported as a fixed
  cost ratio, it would have shaped a whole campaign's plan around a bug.
- The shipped core's issue stage was gated on *retirement* rather than
  write-back, so a dependent chain advanced one link per drain of the reorder
  buffer: IPC 0.07–0.44 with a 352-entry window. Fixing it made the simulator
  2.0–9.5× faster **and** the model correct — and it invalidated every speedup
  measured before it, because the baseline had been crippled. A slow simulator
  and a wrong model are frequently the same bug.

When you do profile, expect this order of results: a *flat* profile means you
are done optimising and the remaining paths are structural. In this project's
final profile no symbol was above 3.2 %, with roughly 24 % in the core model,
16 % in the custom element, 13 % in the allocator, 10 % in the tiles, 8 % in the
memory hierarchy, and about 20 % inside sst-core itself (event queue, clock
dispatch, statistics). A flat profile is also the point at which a 1.3× target
is worth more effort than a 10× one, and the honest move is to write down that
the target was not met.

Two further notes, both generic. Turning off a debug build is worth more than
any micro-optimisation, and a wall time from a debug build should never be
quoted. And an allocator tuned for the pattern an event simulator actually has —
millions of small short-lived allocations per second — was worth 3–8 % of wall
time with the output byte-identical, which is the kind of change to make: it
alters how memory is found, never what the simulation does.

---

## D. Running SST at scale on a shared machine

### D.1 One machine-wide limiter, not one per campaign

Put a wrapper in front of the simulator binary, on `PATH`, that every job uses
without knowing it: it takes one slot of a machine-wide semaphore, waits while
the simulators already running hold too much memory or the load average is too
high, and kills its child at a deadline.

**Why.** Per-campaign caps do not compose. Three campaigns running at once, with
honest caps of 100, 96 and 48 processes, exceeded a 144-thread machine by 70 %;
the load average reached 128 and the box stopped responding for everyone on it.
The caps were individually reasonable and collectively catastrophic, because
nothing was counting. One global limiter cannot be got wrong by a new script,
and a new script is exactly what will get it wrong.

The wrapper this project uses is 40 lines of shell: file-lock slots in
`/dev/shm`, a memory-availability floor, a load cap, and `timeout` around the
real binary. Three details matter more than the implementation:

- **Count the right process name.** The simulator's process name may not be the
  name you typed; if your guard watches the wrong name it protects nothing.
- **Make the campaign's concurrency a request, not a guarantee.** Scripts ask
  for N parallel runs; the limiter decides.
- **Check other people's load before launching.** Your cap is not the machine's
  state. Read the load average first, and treat half the physical cores as the
  ceiling for total load from all users.

### D.2 A deadline on every process

Every simulation gets a wall-clock deadline, enforced by the wrapper, with a
raised limit available for the few runs that legitimately need it and a reason
stated when it is used.

**Why.** A hung run is indistinguishable from a slow one until it has eaten a
day. A deadline converts an unbounded failure into a data point. In this
project the default is 15 minutes, raised to 60 for an explicitly named
correctness run, and never higher.

### D.3 Where output goes

Run directories, statistics files, images, traces and scratch go on the data
array. The operating-system disk holds only documents.

**Why.** A sweep writes a directory per run and a sampled campaign writes
hundreds of megabytes to gigabytes of restart images. Pointed at the home
filesystem — which on a workstation is usually the OS SSD — that is both slow
and unkind to the disk everything else depends on. A second reason is
housekeeping: one directory root per campaign means bulk output can be deleted
in one command when the campaign is done.

There is a subtler reason to give every run its own directory. Several SST
elements write files into the *current working directory* under fixed names — a
guest's stdout, a DRAM model's statistics — so two concurrent runs in one
directory overwrite each other's output, and the result is not an error but a
plausible wrong file.

### D.4 Parallelism is how you spend an hour

Launch every point of a campaign at once, both arms together, up to the
machine-wide limit. Never run points in sequence because each one is "only" 20
minutes.

**Why.** A campaign that sat for seven and a half hours on one long point while
the machine was otherwise idle is a scheduling failure, not a simulation cost.
The same rule has a sharp edge: if one point would take more than about an hour
end to end, it is not launched end to end at all — it becomes parallel sampled
windows (E), or it is reported as not run.

**On parallelising SST itself:** threads and MPI ranks are available and are
worth trying, but their benefit is bounded by the minimum link latency across
the partition, because that is how far the halves can run independently. In this
project, threading a tightly-coupled configuration returned 1.55–1.62× at the
cost of a 21 % change in cycle counts, which failed the fidelity requirement. It
is almost always cheaper to run twenty independent simulations at once than to
make one simulation twenty times faster.

### D.5 Sampling for every performance number

Every performance number comes from sampled windows. End-to-end timing runs are
allowed only as correctness gates at the smallest size that shows the property,
or to validate the sampler itself, and both cases are opted into explicitly with
the reason stated.

**Why.** "No run over an hour" degrades into an allowance: the last campaign
run under that rule had a median point of 14 minutes, a 90th percentile of 38
and a maximum of 53, and diagnosis work ran several 15-to-50-minute runs in
sequence — four 50-minute runs to chase one wrong table lookup. The same points
as sampled windows take 2–10 minutes each and twenty run at once. Making
sampling the default and the full run the exception removes the judgement call
that keeps being made wrongly under time pressure.

The rule that makes this safe: a correctness check that is *per phase* runs
inside the sampled windows, so switching to sampling does not cost you the
checks.

---

## E. The data science

This section is about turning short simulations into a defensible claim. The
methods — SMARTS and SimPoint — are standard; what follows is how to apply them
when the two things you are comparing are not the same program.

### E.1 The sampling coordinate must be timing-independent

To measure a program in pieces, you need a coordinate that names the same place
in two runs. Choose one the machine's speed cannot change.

Retired-instruction count is the usual choice and it is the one to be careful
with. It fails in two distinct ways:

1. **A runtime that waits.** If the program polls a queue or retries a refused
   request, the number of instructions it executes depends on how fast the
   machine is. The count then differs between the functional run that places
   your windows and the timing run that measures them. The fix is to exclude the
   waiting loops from the count — the program's own symbols can mark them — which
   makes the coordinate a property of the program again.
2. **Two builds of one program.** An accelerated build marshals work, dispatches
   it, polls for it and reads answers back; the plain build just does the work.
   Those extra instructions are the accelerator's own overhead, so a window of
   *U* instructions on the accelerated build covers *less of the computation*
   than a window of *U* instructions on the plain build, by exactly the amount
   you are trying to measure.

Failure 2 has two remedies, and which one you want depends on your claim.

- **The program's own count of what it has done** — vertices settled, sums
  performed, operations completed — incremented identically in both builds. Two
  windows at the same coordinates then cover the same work, the accelerator's
  overhead sits inside the window where it belongs, and per-window ratios are
  meaningful. Use this when both arms compute the same thing the same way.
- **Per-arm absolute totals.** Estimate each arm's whole-program time on its own
  coordinate and compare the totals. Use this when the two arms run *different
  algorithms*, where "the same work" is not defined: if algorithm A gets 2× but
  takes 30 s and algorithm B gets 5× but takes 60 s, A is the faster program,
  and no window-by-window pairing is needed to see that.

**Not generic.** The work counter is a program-side convention — a 64-bit global
the program increments and the front end watches. It is cheap and it is not
free: you must add it to both builds, identically, and a driver should refuse to
compare two arms whose totals differ, since a work unit is the same unit in both
builds and two different totals mean the two builds did not compute the same
thing.

### E.2 Three phases: warm-up, region of interest, teardown

Every measured window has three parts and only the middle one is measured.

| phase | what happens | measured? |
|---|---|---|
| warm-up | state converges from the restart point: caches, translation, the accelerator's pipeline | no |
| region of interest | steady state; the numbers are the difference of two snapshots of every statistic, taken at the region's own boundaries | yes |
| teardown | the run continues until work the region left in flight has drained | no |

Two rules follow, and it is worth making the planner enforce rather than warn:
a region may not begin before a warm-up has elapsed from its restart point, and
a region may not end within a warm-up's distance of the program's own end.

**Warm-up is measured, not chosen.** The warm-up is the smallest one at which
the window reproduces the rate an uninterrupted run has over the same span. It
is a property of the working set and of the phase the window lands in, not of
the binary, so a warm-up measured at one size is not carried to another. In this
project one workload's insert phase settles after 6 million instructions while
its verification phase is still 4 % out at 16 million and needs 21 — so the
warm-up is set by the worst phase, and reported with the size it was measured
at.

**Window width is measured too, and getting it wrong is not a small error.** A
region narrower than the period of the machine's own oscillation does not
average over that oscillation; it reads whichever point of the cycle the warm-up
happened to leave the machine at, and the estimate is then wrong by factors
rather than by per cent. Measure the narrowest width at which consecutive spans
stop predicting one another, and give both arms the same width, because two
regions compare only if they cover the same work.

**A wide interval is a defect to explain, not a result to publish.** One estimate
came out at 181× with a 95 % interval of [104, 473] from windows of 4,000 work
units — narrower than the accelerator's own fill-and-drain cycle, with only 31
dispatches inside them. That interval was window-width noise, not program
variation. If your interval is wide, look at window width, at normalisation, and
at whether a phase boundary is inside a window, before you conclude the program
is variable.

### E.3 SMARTS and SimPoint, and why both

Two standard estimators, and it is worth running both:

- **SMARTS-style systematic sampling.** Many short windows placed
  systematically, one in each interval of the program, combined as a weighted
  mean with the variance of that mean. It assumes nothing about phases.
- **SimPoint-style phase weighting.** Cluster the program's intervals by
  execution signature (basic-block vectors), measure a representative of each
  cluster, and weight by cluster size. Where a program has phases of very
  different cost this gives a much narrower band, and it is the right tool when
  the program changes a lot between phases or when you need to compare across
  large changes to the machine.

Running both is a consistency check on each. In the demo the two agree within a
few per cent on one arm and differ by more on the other, which is itself
information about how phase-like that arm is.

**Not generic.** The restart mechanism this project uses — whole-program images
written by a patched functional simulator, carrying registers and the entire
address space but no timing state — is one implementation among several. What
matters is the property: the restart point must be *architectural* (no cache
contents, no queue state, no timing), so that a single set of restart points
survives every hardware change you want to sweep, and warm-up is the thing that
rebuilds the timing state. Stock alternatives are a trace front end, a request
generator, or an engine-level checkpoint.

### E.4 Validate the estimator where the truth exists

Take programs small enough to run to their own end, estimate them by sampling,
and divide. Report the residual — estimate over truth — per workload family, and
when you apply that band to a point you did not validate, label it `assumed` and
print the size it was measured at.

**Why.** Sampling error and systematic error are different quantities, and only
a comparison against a real answer can measure the second. In this project ten
points at 16–64 MiB, two arms each, were run both ways: all ten intervals
contained the truth on the span the estimate covers, every estimate was within
2.1 % of the whole run, and seven of ten within 1 %. The one point 2.0 % low was
explained, not excused: 2.3 % of that run is a verification phase after the last
unit of work, which has no extent on the sampling coordinate and is therefore
beyond any window's reach.

That last sentence is the real value of validation. It tells you what your
estimate structurally cannot see.

### E.5 Two intervals, and what each one says

Report both and never merge them, because they answer different questions.

| interval | what it is | what it does not cover |
|---|---|---|
| sampling interval | the spread of the measured regions' rates: the risk that the intervals you measured were not representative of those you did not | any systematic bias in the method |
| with the residual band | the above, divided by the measured envelope of estimate-over-truth for this workload family | whether the machine model itself is right |

For a **ratio** of two estimated totals, quote Fieller's interval, which is the
exact interval for a ratio; the delta method is an approximation that holds only
while the denominator's uncertainty is small, and where they disagree Fieller's
is the one to read. A ratio is neither safer nor more dangerous than its parts:
where both arms err in the same direction part of the error cancels, and where
they err in opposite directions it compounds — nothing in the method controls
which, so a residual band on a speedup has to cover both.

One honest complication worth knowing: a design that places one window in every
interval is *unaffected* by variation between intervals, but the sampling
interval is computed from the spread of the window rates, which on such a
program is dominated by exactly that variation. The reported interval can
therefore be several times the design's own measured error — 5.28 % reported
against 0.73 % measured, in one case. Publish the reported one, because it is
what its own formula saw, and say what the calibration measured beside it.

### E.6 The shape of the claim

State it as: **the best algorithm for the new machine, against the
state-of-the-art algorithm for the problem on a baseline you have audited.**

Three things that rule out:

- **A weakened baseline.** If the baseline core lacks a branch predictor, a
  scheduler, or address translation, your speedup includes the cost of features
  you declined to model. In this project, adding a real branch predictor cut one
  headline speedup from 26.2× to 9.3× — that factor of nearly three was the
  missing predictor, and it was on the way to being published.
- **A weakened baseline algorithm.** The comparison is against the best known
  algorithm for the problem on that machine, not against the accelerated
  algorithm with the accelerator switched off. The two arms need not be the same
  program, which is precisely why E.1's per-arm totals exist.
- **A result whose setup is inside the measurement.** If input data is generated
  by the program, the setup phase is in your cycles; in one workload it was 91 %
  of them. Whichever convention you choose — generate it, or place it untimed
  before the clock starts — state it, apply it to both arms, and re-check the
  numbers, because it moved one reported speedup from 15.1× to 60.2×.

And a rule about defaults that is really a rule about honesty: when you decide
one configuration is the right baseline, make it the *default* in the
configuration file, the script and the Makefile target. A decision that lives
only in a note gets dropped by the next script that reaches for a default; in
this project the in-order host stayed the default file for days after the
out-of-order one had been agreed as the baseline, and results were produced
against the wrong host in the meantime. Make the other choice the one that has
to be named.

### E.7 Where numbers become a page

Generate results pages from the same parsed tables the prose quotes, so a table
and its sentences cannot disagree. In this project the page generator reads the
measurement tables and the sampled estimates' CSV, computes every figure quoted
in the prose from those same rows, and splices sections into the page between
markers; confidence intervals come from the estimator's own columns rather than
being retyped.

**Why.** A page whose numbers were typed by hand disagreed with its own tables,
and the disagreement was found by a reader rather than by us. Anything a reader
can check, a script should compute.

---

## F. Doing this with Claude Code

An agent is very good at this work — reading a 20,000-line element and telling
you what its issue stage actually does, writing the directed test, running
twenty simulations and collecting the statistics — and it has one systematic
weakness: it does not remember your architecture between sessions, and it will
re-derive a decision from whatever code is in front of it. Everything below is
about that weakness.

### F.1 A canon, with an authority order

Keep one document that *is* the design: a numbered list of invariants, a list of
rejected mechanisms with the reason each was rejected, and a record of what is
still open. Declare an authority order — in this project: the owner's current
instructions, then the reference implementation, then the documentation, then
the SST tree, which decides nothing — and write it at the top of the canon.

**Why.** Hours were repeatedly lost re-deriving settled decisions from the wrong
source: a stale paragraph in an old document, or the SST implementation, which
contains conveniences that are not the architecture. The authority order also
tells the agent what to do when two sources disagree, which is otherwise a coin
flip.

**The rejected list is the half people skip and the half that saves the most
time.** An agent that does not know a mechanism was rejected will re-propose it,
with confidence, in a form you have already argued down.

### F.2 Memory files as the rulebook

Keep one short file per rule, each with three parts: the rule, **why** it exists
(what went wrong), and **how to apply** it. Link them from an index the agent
reads at the start of a session.

**Why.** A rule without its reason gets optimised away by a clever agent under
pressure — it will find a case the rule seems not to cover. A rule with its
failure attached survives, because the failure is the argument. This structure
also makes the rules reviewable: you can read thirty of them in ten minutes and
delete the ones that have stopped being true.

### F.3 A hook that injects the rules into every prompt

Use a prompt hook to inject, on every turn, a compact digest of the invariants:
each one's heading and first sentence, plus the corrections that have had to be
made more than once, plus a pointer to the full document.

**Why.** Writing a decision down is not the same as reasoning from it. A
document read when something breaks does nothing at the moment the agent is
designing something else — and that moment is when the settled decision gets
contradicted. Two calibrations from experience:

- **Size matters.** The first version of this hook injected 90 KB on every turn,
  which is far too expensive. The digest is about 4 KB — enough for the agent to
  notice it is about to contradict something settled and to go and read the file.
- **Do not compress away the corrections.** An invariant's first sentence is
  often not where the correction lives. Cutting the explicit "not X" lines for
  size is exactly how one settled decision got reverted the following day.

And a rule about the hook itself: **if a memory note or a hook ever states a
rejected design as current, fix the note, not just the turn.** Both of ours did,
once, and they were teaching the agent the rejected design on every prompt.

### F.4 One agent per phase, with effort matched to the phase

Structure work as a short sequence of phases — design, build, measure, document —
with one agent per phase. Give diagnosis, design and defect-fix phases the
highest reasoning effort, because a wrong answer there costs a whole re-run;
give verification, measurement and document phases medium; give mechanical steps
low. Pass each phase's output into the next phase's prompt as text.

**Why.** Token budgets are real and they are shared: exhausting a weekly limit
mid-campaign killed every agent at once and stopped work for two days. One agent
per phase, no adversarial review panels unless asked, short prompts, and no
status polling is the difference between finishing a campaign and finishing
half. Effort matched to the phase is the same economy applied inside the
campaign.

**What a phase prompt contains.** In this project's workflow scripts, every
phase's prompt is a shared rules block plus a task paragraph. The shared block
carries the machine limits verbatim (read the load average first; total load at
or below half the cores; a process cap that *scales down* when several agents run
at once — 16 for a single-flow workflow, 6 each for a three-way parallel one; a
deadline on every run; the sampling rule), the output discipline (every run
directory and scratch file under one campaign root; only final documents
elsewhere), and the quality gates, which are the sentences worth copying:

> Never change what the machine computes to make a number pass: defects are
> fixed at their cause with a test that fails before and passes after; every
> answer digest unchanged.

> Never change the machine in this workflow.

The second one is a scope fence. An agent measuring a workload will otherwise
fix the machine to make the measurement nicer, and you will not notice until the
numbers stop comparing with last week's.

### F.5 Prompts that point at files

Tell the agent which files to read; do not restate their contents. Give exact
paths, and say what to look for in each.

**Why.** Restating context is expensive, goes stale, and produces a second
source of truth that disagrees with the first. Pointing at files also means the
agent's answer is checkable against the same file you pointed at. Where a
specific prior number matters — an address, a run name, a rate — include it
inline, because a number in the prompt is cheap and a wrong recollection of it
is not.

### F.6 Write the rule the first time something goes wrong

Every one of the lessons below became a written rule the first time it cost
something, and the writing is the mechanism: a rule that exists only in a
conversation is gone by the next session.

| what went wrong | the rule it produced |
|---|---|
| three campaigns with separate process caps overloaded a shared machine and it had to be cleared by hand | one machine-wide limiter in front of the simulator; a campaign's concurrency is a request (D.1) |
| runs of 3.2 to 5.5 hours were launched, twice, for numbers sampling could have produced | nothing end to end over about an hour; every performance number sampled (D.4, D.5) |
| a published results page disagreed with its own tables | every figure in the prose computed from the same parsed rows as the tables (E.7) |
| a correctness fix serialised the mechanism it was repairing | fix at the cause; a fix that serialises is provisional and labelled (B.3) |
| a settled architectural decision was contradicted three steps after being written down | a canon with invariants and a rejected list, injected as a digest every turn (F.1, F.3) |
| a baseline configuration stayed the default after a better one had been agreed | a decision means the default artefact changes, not a note (E.6) |
| an agent swept a knob for half an hour to answer a question the counters already answered | instrument, do not ablate (C.4) |
| a 4.6× simulator slowdown was reported as intrinsic when it was idle work | profile and attempt a fix before accepting a cost (C.5) |
| sweeps wrote gigabytes to the OS disk | bulk output on the data array, documents only elsewhere (D.3) |
| documents quoted internal names and were unreadable to anyone outside the work | write for a reader who was not there (B.6) |

The table is short because rules are expensive: each one is something the agent
must hold. Prune them when they stop being true, and keep the reason attached to
the ones that stay.

---

## G. A demo you can run

`NMFC-Rev/tools/demo/run_demo.sh` performs the four steps of one measurement end
to end, in **63 seconds** measured on a 72-physical-core machine that was
already carrying a load average of 12 from other people's work, using at most
five concurrent simulator processes. Its README documents every step and what to
look at.

| step | what it does | generic, or ours |
|---|---|---|
| 0 | reads the machine's load, imposes a per-process deadline, refuses to start on a busy box | the discipline is generic; the limiter is ours (D.1) |
| 1 | records repository commits and library checksums and compares them against a frozen record | generic: `sst --version`, `sst-info`, git hashes, checksums (B.4) |
| 2 | runs one directed test — all fourteen narrow-width arithmetic instructions, 41 checks — in under a second | generic (B.2) |
| 3 | writes restart points for both arms of one small workload, then measures five short windows per arm, five at a time | the producer and driver are ours; SMARTS/SimPoint and the window structure are generic (E.2, E.3) |
| 4 | prints each arm's estimate, its interval, the relative error behind it, the speedup with Fieller's interval, and the counters that explain it | generic (E.5, C.4) |

The table's last two rows are the ones worth watching in the output: the interval
comes out around 10 % on a point this small, and the demo says so and explains
why rather than quoting the ratio alone; and the counters show the accelerator's
tracking structure nearly full of outstanding work with almost nothing sitting
finished, which is what "the accelerator is the limit, the host is keeping up"
looks like when you can see it.

---

## What this guide does not cover

Whether the model is right. Neither a sampling interval nor a validated residual
says anything about whether the machine you have modelled resembles a machine
that could be built; that is what the audit in C, the sourced parameters in C.2
and the directed tests in B.2 are for, and they are necessary rather than
sufficient. Also absent: SST's network elements and MPI-scale simulation, which
this project does not use, and the mechanics of building SST, which its own
documentation covers.
