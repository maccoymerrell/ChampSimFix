# Near-Memory Function Cores: purpose, approach, and the state of the work

*A summary written in the form of a research proposal, November 2026.*

---

## 1. What this project is for

Modern computers spend most of their time, and most of their energy, moving data to a processor rather than computing on it. For the workloads that matter most in large-scale analysis — graph traversals, hash tables, scattered reductions, stencil solvers over large meshes — a single out-of-order core waits on memory for the bulk of every cycle, and adding more such cores adds more waiting. The question this project asks is direct:

> **If small, simple, heavily multithreaded processors sit at the memory itself, and the main processor can hand them units of work with a single instruction, how much faster does the whole machine finish the same work, on what kinds of programs, and what does the programmer have to write to get it?**

What we are getting out of the project is therefore four things, each of which stands on its own:

1. **An architecture, specified to the instruction.** A near-memory function core (NMFC) attached to each memory channel; an instruction set extension through which an ordinary host core offloads a unit of work by handing over a 512-bit register file and an entry address, and receives the same 512 bits back; a placement model in which physical memory is partitioned among the tiles and work moves to the data rather than data to the work; and the memory-ordering, atomics and coherence rules that make it correct. The specification is ratified in a canon of invariants and rejected alternatives, so that every later design decision is checked against it.

2. **A cycle-accurate model of that architecture, and a measurement method that can be trusted.** A simulator built on SST with a modern out-of-order host (TAGE-SC-L branch prediction, a path-indexed memory-dependence predictor, decoupled front end), the tiles, a directory-based coherence fabric with per-port bandwidth, DDR5 memory with per-bank controller queues, and a functional second simulator that runs the same binaries and must agree with the cycle model on every answer and every instruction count. Because the programs of interest run for billions of instructions, every performance number comes from statistically sampled windows cut from whole-program images, with the sampler itself validated against uninterrupted runs.

3. **A programming model at the level of OpenMP, with a compiler behind it.** The programmer writes ordinary loops over ordinary data plus a few directives saying what is parallel, what is reduced and what must be ordered. A source-to-source compiler and runtime derive everything else: which tile owns which data, how a loop body is split and packed into 512 bits, where arrays are placed, how work is dispatched and collected, and which of the machine's optimisation knobs to use — each knob chosen from measured counters rather than guessed. The library exists so that the designer's whole job is finding which *algorithm* works on this machine, not how to drive it.

4. **Workload studies with an honest claim shape.** For each workload, the machine running the best algorithm we can design for it is compared against a single out-of-order host core running the best known algorithm for that core, on the same input, ending in the same state, with every speedup quoted with its statistical interval and the resource that bounds it named with its capacity and source. A result that looks good against a weaker baseline is not a result.

The intended output is a body of evidence, machine and method included, from which a paper or a design decision can be made: where near-memory function cores win, by how much, why, and what the program has to look like.

---

## 2. Background and the design in brief

The host is an out-of-order 64-bit RISC-V core. Below it, across a coherence fabric, sit four tiles; each tile owns a fraction of the *physical* address space outright — a slice of the last-level cache, a memory controller, a DDR5 channel — and a small in-order multithreaded engine in front of them. The engine holds many independent contexts, each with its own program counter, a 512-bit register file packed by the program, one instruction slot and one outstanding memory operation. There is no stack: a function that does not fit its 512 bits cannot run, and the compiler's job includes making it fit.

The host starts work with `FORK`, which hands a context and an entry address to the tile that owns the first address the context names, and collects it with `JOIN`. An invocation that reaches for memory another tile owns *migrates* there, carrying its 72 bytes of state instead of pulling a 64-byte line across the fabric; migration is the machine's ordinary mechanism for bringing work to data, reported as evidence about placement and never treated as a cost to minimise or a design goal. Pages come in four types — host, grain (every frame on one tile), striped, and duplicate (one read-only copy per tile, written only by the host at setup). Translation happens before anything crosses the fabric, with one page table per address space duplicated on every tile and grain-sized leaves so that a tile's working set translates in a handful of entries.

Inside a tile, a unified memory queue per data-cache bank is the only ordering point: it enforces forwarding, performs atomics at the bank, and implements load-reserved/store-conditional as a serialisation point in queue order with no separate reservation unit. A `WAIT` instruction lets a long-lived worker sleep on its load slot until a cache line is written, with the bank broadcasting an invalidation to every slot, so that waiting for work costs no memory traffic. Every mechanism's first test is the contended case it exists for — two agents on one word, host against tile, tile against tile.

The design is not fixed at its current sizes. Pipes per tile, contexts per engine, cache sizes and the derived quantities that depend on them are configuration, and for floating-point work the design point under study is eight pipes with 192 contexts and a 64 KiB data cache rather than the four-pipe default.

---

## 3. Hypotheses

1. **Memory-bound work.** For programs whose time on a conventional core is dominated by cache misses, a unit of work that owns its data and runs next to it will finish the same work several times faster than the host alone, bounded by aggregate memory bandwidth rather than by one core's miss-handling capacity.

2. **Parallel compute.** For work that is parallel but compute-heavy, a tile with P pipes and enough contexts in flight to fill them retires about P instructions per cycle, against the four to six of an out-of-order core, so such work is not excluded from the tiles; where the pipes cannot be kept full, that is a design problem to be fixed, not a limit to be reported.

3. **Programmability.** The machine's placement, packing and dispatch decisions can be derived by a compiler from ordinary loop bodies well enough that the hand-written programs are reproduced instruction for instruction, so that the programmer's burden is that of OpenMP.

4. **Dispatch is the bottleneck to watch.** With one host core feeding the tiles, the rate at which work is started and collected limits how many contexts are live; the architecture's open problem is keeping contexts in flight, and designs are judged by live contexts by state, not by tracking-unit occupancy.

---

## 4. What has been built

**The machine.** The tile core as specified — a context array scheduled into N pipes of M stages, instruction and load slots per context, virtually indexed translation queues, physically indexed per-bank memory queues with forwarding, bank-side read-modify-write, and the load-reserved point that closes at the owner's store-conditional or its next memory operation. Per-operation floating-point and integer-divide costs from a named real core, with a shared divider per tile. A fabric with per-endpoint ports in each direction and traffic counted by kind. A DDR5 model with one read queue per channel scheduled with a head per bank and a separate write queue, slice banks aligned with the DRAM's banks, and 16 GiB per tile. Grain-sized translation leaves. Forward-progress holds for the reserved pair on both the tile and the host, sized from each side's own pair window. The `WAIT` instruction in both simulators.

**The method.** Images at uniform points on an axis of total instructions executed, host and tiles together, carrying a hardware-independent record of cache, TLB and predictor recency so a restored window starts warm; invocations held part-done in the image; stretches the sampler would otherwise miss — a program's opening, closing and the moments it runs out of work — measured whole; a short-program rule derived from the validated points. Every sampled estimate is validated against an uninterrupted run before it is used, and five of six validated points contain their whole runs within their intervals. The two simulators agree on every answer across 6,369 randomised stress programs and on instruction counts to an identity.

**The library.** A clang-based compiler and a host runtime. From the plain one-directive reduction listing it emits kernels whose 524 instruction words are identical to the hand-written ones; from the graph-search and dictionary listings it produces programs that give the hand-written answers at every size, with the dictionary's host cost per invocation at 77 instructions against 15,000 for the hand-written program. Ghost bands for stencil reads, per-context memory for body-local arrays, exact floating-point reductions, inlined transcendentals, heavy-first ordering and a replication rule expressed in terms of the cache-slice size are built. Each optimisation knob — fork form, dispatch shape, answer path, membership representation, replication — has been measured with a paired, interval-bearing comparison and the compiler's default set from it.

**The workloads.** Three built by hand and reproduced by the compiler: a direction-optimising breadth-first search, a chained hash table, and a shuffled reduction. A fourth — a two-dimensional reacting-flow solver with state-to-state air chemistry, originally a MATLAB code of some 2,400 lines plus generated GPU kernels — ported to a standalone C program, shown bit-identical to the original over 40 solver iterations and 173 recorded kernel calls, building for x86 and for both RISC-V targets with bit-identical results, and then compiled onto the machine through the library.

---

## 5. Results so far

All figures are the machine's best program against the host's best on the same input, ending in the same state, with 95 % intervals from paired sampled windows. The machine configuration is four tiles, 128 contexts per engine unless stated.

| workload and point | machine ÷ host | interval | note |
|---|---|---|---|
| Reacting-flow solver, 180×75, 20 steps, kernels as ported, 4 pipes | 0.94 | [0.886, 0.997] | the first point: bodies never designed for 512 bits inflated tile instructions 2.3× |
| Same, kernels redesigned for the context, 4 pipes | **1.17** | [1.080, 1.268] | inflation brought to 1.32× |
| Same, redesigned kernels, 8 pipes, 192 contexts, 64 KiB data cache | **2.41** | [2.214, 2.619] | the floating-point design point |
| Dictionary: resident workers on `WAIT` vs per-phase waves | 0.94 | [0.919, 0.963] | polling removed entirely; the loss is in the host's chunk filling, not the waiting |
| Reduction: replicated vs owned read-only array, 4 MiB | 2.51 | [2.18, 2.84] | copies win while one copy fits a slice |
| Reduction: FORK.R vs memory-form FORK.M | 1.23 | [1.128, 1.341] | the register form wins; the memory form's start waits on the tile |
| Dictionary: folded answers vs an answer stream | 1.03 | [1.023, 1.036] | folding wins |

Earlier results on a superseded version of the machine — 3.9 to 8.0× on the reduction, 2.3 to 4.1× on the hash table, 3.8 to 7.4× on the graph search above 4 MiB — predate the fabric, controller, translation and sampler corrections above and are not quoted as current. Re-measuring the three original workloads on the completed machine is the next campaign.

Two findings are worth stating as findings. First, the reacting-flow result moved from parity to 2.4× without a single change to the machine's mechanisms: it moved by redesigning the kernel bodies for 512 bits and by choosing the configuration that keeps the pipes full, which is the project's central claim about where the leverage lies. Second, the remaining gap between the tiles' instruction count and the host's is not floating-point work, which is a tenth of what the tiles execute, but integer address arithmetic and the reloading of loop-invariant constants that eight 64-bit lanes cannot hold — a measured statement about what the next revision of the core should carry.

---

## 6. Methodological commitments

- The comparison is always the machine's best algorithm against the host's best algorithm on the same input, never against a weaker baseline.
- Every number is sampled, validated and quoted with its interval; nothing is quoted from an end-to-end run except correctness gates and the sampler's own validation.
- Every limit is attributed to a named resource with its configured capacity and its source; models are based on named real systems.
- A mechanism's first directed test is the contended, cross-agent case; a correctness fix keeps the mechanism it repairs, and a hold, pin or drain is provisional.
- Algorithms are designed for the machine; the imaging and sampling infrastructure exists to evaluate alternatives, and "this does not offload well" is the start of a design pass, not its conclusion.
- Optimisation points are derived empirically, never decided in advance; only semantic questions come to the owner.

---

## 7. What remains

- The campaign on the three original workloads against the completed machine, at the sizes and configurations above.
- The reacting-flow solver's remaining inflation: integer address work and constant reloading, which point at the register file's width and at a constant pool as the next core questions.
- The 16-pipe design point, built and passing its suites but unmeasured.
- The fairness bound at the serialisation point: deadlock-freedom and forward progress against a continuous writer are shown; starvation-freedom is not.
- A directory supply policy that serves clean lines from a tile's own slice rather than from the host's cache, measured across workloads.
- The library's dispatch shape, open until the resident form's host-side loss is explained, and its per-invocation host cost, now 76 instructions on the graph search against the hand-written 40.

---

## 8. Scale of the effort to date

Roughly 250 commits across the simulator, its element library, the memory model, the compiler and the documents; two test suites of about 600 simulations each that pass at every configuration; six validated sampling points; four workloads; and a canon of 121 rejected alternatives recording what was tried and why it was set aside.
