# Near-Memory Functional Cores: a hardware-threadlet extension to RISC-V

*Purpose statement and state of the work, November 2026.*

## The problem

In modern processors, many programs spend most of their time waiting on memory. Graph traversals, hash tables, scattered reductions and other algorithms display attractive asymptotic complexity while relying on a quick round trip between the core executing the algorithm and the main memory answering its requests. As the datasets these algorithms operate on grow, the processor's SRAM caches fail to hold their working sets, and the algorithm becomes bound entirely by memory latency.

A careful distinction must be made between memory latency and bandwidth. Bandwidth, the bytes served per unit time, has a fixed limit for any system; for DRAM, as that limit is approached, latency rises asymptotically with queueing. Bandwidth is easy to scale: modern processors carry tens of memory controllers addressing hundreds of independent DRAM banks. That cannot lower the system's baseline latency, only reduce the variation in latency due to load.

Algorithms such as graph traversals and hash tables are effectively serialised across their operations and have little memory-level parallelism (MLP). With one memory request in flight and nothing to execute until it returns, the program stalls for the full memory latency, hundreds to thousands of cycles, performs a few instructions, and issues the next request.

A single thread of such an algorithm is serialised, but many threads of it are not. With many contexts each waiting on its own serialised access, the latency is amortised by exploiting MLP across threads rather than within them: more threads, more requests in flight, higher memory utilisation, shorter runtimes.

Accelerators such as GPUs run many uniform operations at once, and they suffer two constraints here. The uniformity of their execution limits an algorithm's use of the hardware to its ability to express itself as single-instruction multiple-data work; sparse vector operations, loops of indeterminate length and pointer chases all leave GPU hardware idle. And coherence: the more aggressively a process is multithreaded, the more coherence traffic it generates, because coherence is the overhead of inter-thread communication through a shared memory system. Scaling the active thread count strains the memory system before substantial bandwidth is extracted.

## The proposal

We propose a hardware-threadlet extension to RISC-V that addresses these problems with one mechanism. Two pieces of prior work matter.

1. **Many-context cores**, in the manner of Niagara, keep many contexts (64 and more) partly active within one core rather than devoting hardware to keeping one to four contexts fully active. This escapes the limit of multicore processors, which manage 128 to 256 cores of one or two contexts each, and lets many threads stay alive at once to exploit the inter-thread MLP above. It leaves the coherence problem unsolved: contexts on one such core communicate freely, but combining several cores brings the coherence traffic back.

2. **Near-memory computing** reduces the latency, and sometimes raises the bandwidth, of memory access by co-location. Large-dataset workloads get little from processor caches, so bypassing them avoids a tag check at every level, and sitting on the memory controller's side of the fabric avoids the routing latency across it.

The near-memory functional core (NMFC) extension combines the two: Niagara-style cores at the memory. The ingredient that makes this feasible is **address-space partitioning**. Real systems already bank their caches and split the last-level cache into slices across the die and the fabric. Placing each slice beside one off-chip memory controller together with an NMFC gives a core dedicated to one portion of the physical address space, backed by its slice of the last-level cache. Such a core does not belong to a thread; it aggregates work from every thread that wants that portion of memory. The pieces of work, kernels, execute side by side in the NMFC, where ordering and atomics for that address space are resolved locally in the memory queue that owns the line, and the kernels themselves generate no coherence traffic among one another.

Because these kernels are memory-bound, a very large number of contexts can be kept active with little instruction-fetch bandwidth and execution time. Many contexts mean high bandwidth utilisation without overprovisioning hardware as a GPU does. Across the tens of memory controllers of a server, this scales to tens of thousands of contexts.

The NMFCs appear to software as a small extension of the RISC-V instruction set. A thread on an ordinary host core dispatches work with `FORK`, handing a 512-bit context and the starting program counter of a kernel, and optionally retrieves the returned context with `JOIN`. The context is the kernel's whole register file: 512 bits in, the same 512 bits out.

It would be prohibitive to confine a context to one memory tile, or to expose physical memory to the programmer. Instead, the address space's owner distributes virtual pages to physical frames across the tiles, and when a kernel touches an address another tile owns, the threadlet, with its small context, migrates across the fabric to that tile's NMFC and continues there. Migration traffic replaces coherence traffic; the replacement pays off in proportion to how many memory requests a kernel makes before it has to move, and how well the load stays balanced across tiles. Kernel code pages and the page tables are duplicated on every tile, so that each tile translates locally, redirects a migrating context itself, and fetches kernel code from its own memory.

## Where the work stands

The architecture and its RISC-V extension are implemented in SST as a cycle-accurate model: an out-of-order host with a modern front end and memory-dependence prediction, four tiles with the function cores, a directory-based coherence fabric with per-port bandwidth, DDR5 with per-bank controller queues and 16 GiB per tile, local translation with grain-sized leaves, and the memory queues that order, forward and resolve atomics at each tile. A second, functional model runs the same binaries and must agree with the cycle model on every answer and every instruction count; it does, across 6,369 randomised stress programs. Because the programs run for billions of instructions, every performance number comes from statistically sampled windows cut from whole-program images that carry the machine's warm state, with the sampler validated against uninterrupted runs.

Initial results, each the machine's best program against the best known algorithm for the host core on the same input and ending in the same state, quoted with 95 % intervals:

| workload | machine configuration | speedup over the host |
|---|---|---|
| Two-dimensional reacting-flow solver (state-to-state air chemistry), 180×75 cells, 20 steps | 4 pipes per tile, 128 contexts | 1.17× [1.08, 1.27] |
| the same, kernels redesigned for the 512-bit context | 8 pipes per tile, 192 contexts, 64 KiB data cache | 2.41× [2.21, 2.62] |
| Shuffled reduction, chained hash table, breadth-first search, at working sets above 4 MiB | earlier revision of the model | up to 8×, 4× and 7× respectively |

The last row predates corrections to the fabric, controller, translation and sampling models and is being re-measured on the completed machine. The first two rows carry the result we regard as central so far: the solver moved from parity with the host to 2.4 times faster without a change to any machine mechanism, by redesigning the kernel bodies to fit the context and by choosing the configuration that keeps the pipes full. What still separates the tiles' instruction count from the host's is not floating-point work, which is a tenth of what the tiles execute, but integer address arithmetic and the reloading of loop-invariant constants that eight 64-bit lanes cannot hold; that is a measured statement about what the next revision of the core should carry.

A programming library is in progress to integrate the NMFC cores with algorithms written at the level of OpenMP: the programmer writes ordinary loops over ordinary data plus a few directives, and a source-to-source compiler derives ownership, the splitting and packing of each loop body into 512 bits, placement, dispatch and the machine's optimisation choices, each of the latter set from measured counters. The compiler reproduces the hand-written reduction kernels instruction for instruction and gives the hand-written answers on the graph search and the hash table; the reacting-flow solver above was compiled through it. The next step is comparison against GPUs and many-core server processors in the same SST framework. Those results will say what this architecture costs against those alternatives, and how readily existing algorithms map onto it.
