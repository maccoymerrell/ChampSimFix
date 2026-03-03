.. _Timing:

=====================================
Timing and Chrono
=====================================

ChampSim uses a discrete event simulation model with cycle-accurate timing.

----------------------------------
Simulation Clock
----------------------------------

The ``champsim::chrono`` namespace provides timing utilities.

.. doxygennamespace:: champsim::chrono
   :members:

Clock Model
~~~~~~~~~~~

ChampSim uses a cycle-based clock:

* Single global clock for all components
* All components advance in lock-step
* Cycle counter tracks simulation progress
* No sub-cycle timing

Time Units
~~~~~~~~~~

Time is measured in multiple units:

.. code-block:: cpp

   using picoseconds = std::chrono::duration<uint64_t, std::pico>;
   using nanoseconds = std::chrono::duration<uint64_t, std::nano>;
   using cycles = std::chrono::duration<uint64_t, std::ratio<1>>;

Converting Between Units
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cpp

   // Convert cycles to picoseconds (assuming 2 GHz)
   auto ps = std::chrono::duration_cast<picoseconds>(cycles{1000});

   // Convert nanoseconds to cycles
   auto cy = std::chrono::duration_cast<cycles>(nanoseconds{500});

----------------------------------
Timing Parameters
----------------------------------

Different components have different timing characteristics:

CPU Core Timing
~~~~~~~~~~~~~~~

* **Clock frequency**: Core operating frequency (e.g., 4 GHz)
* **Cycle time**: Inverse of frequency (e.g., 250 ps)
* **Pipeline stages**: Fixed latency per stage
* **Instruction latencies**: Variable per instruction type

Cache Timing
~~~~~~~~~~~~

* **Hit latency**: Cycles for cache hit (e.g., L1: 4 cycles)
* **Miss latency**: Base latency for cache miss
* **Tag lookup**: Cycles for tag array access
* **Data access**: Cycles for data array access
* **Fill latency**: Cycles to write new cache line

DRAM Timing
~~~~~~~~~~~

* **tRCD**: Row-to-column delay (e.g., 14 cycles)
* **tRP**: Row precharge time (e.g., 14 cycles)
* **tCAS**: Column access strobe (e.g., 14 cycles)
* **tRAS**: Row active time (e.g., 34 cycles)
* **tRFC**: Refresh cycle time
* **tRRD**: Row-to-row activation delay
* **tFAW**: Four activate window

----------------------------------
Latency Modeling
----------------------------------

Different types of latency in ChampSim:

Fixed Latency
~~~~~~~~~~~~~

Operations with constant latency:

* Register operations (1 cycle)
* ALU operations (1 cycle)
* Cache hits (L1: 4-5 cycles)
* Branch resolution (1 cycle)

Example Implementation
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cpp

   class fixed_latency_op {
       uint64_t completion_cycle;

   public:
       void start(uint64_t current_cycle) {
           completion_cycle = current_cycle + LATENCY;
       }

       bool ready(uint64_t current_cycle) {
           return current_cycle >= completion_cycle;
       }
   };

Variable Latency
~~~~~~~~~~~~~~~~

Operations with data-dependent latency:

* Cache misses (depends on lower level)
* DRAM accesses (depends on bank state)
* TLB misses (depends on page walk)
* Prefetch requests (depends on timing)

Queuing Latency
~~~~~~~~~~~~~~~

Time spent waiting in queues:

* Queue insertion latency
* Queue processing latency
* Bandwidth contention
* Resource conflicts

----------------------------------
Bandwidth Constraints
----------------------------------

.. doxygenclass:: bandwidth
   :members:

Bandwidth Types
~~~~~~~~~~~~~~~

* **Port bandwidth**: Accesses per cycle to a structure
* **Bus bandwidth**: Data transferred per cycle
* **Memory bandwidth**: Bytes per cycle to/from DRAM
* **Prefetch bandwidth**: Prefetch requests per cycle

Bandwidth Enforcement
~~~~~~~~~~~~~~~~~~~~~

Bandwidth limits are enforced by:

1. Tracking requests per cycle
2. Checking against limit
3. Queuing excess requests
4. Distributing across multiple cycles

Example: Cache Port Bandwidth
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cpp

   class cache_port {
       bandwidth bw;

   public:
       cache_port(uint32_t max_per_cycle) : bw(max_per_cycle) {}

       bool can_access() {
           return bw.has_remaining();
       }

       void access() {
           bw.consume(1);
       }

       void reset() {
           bw.reset(); // Called each cycle
       }
   };

----------------------------------
Event Scheduling
----------------------------------

ChampSim uses implicit event scheduling:

Current Cycle
~~~~~~~~~~~~~

The simulation maintains a global cycle counter:

.. code-block:: cpp

   extern uint64_t current_cycle;

Components use this to:

* Track operation completion times
* Schedule future events
* Measure latencies

Future Events
~~~~~~~~~~~~~

Events scheduled for future cycles:

* Instruction completion at cycle N
* Cache fill arrival at cycle N+L
* DRAM response at cycle N+T

Implementation Pattern
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cpp

   struct timed_event {
       uint64_t completion_cycle;
       event_data data;

       bool ready() const {
           return current_cycle >= completion_cycle;
       }
   };

   std::priority_queue<timed_event> event_queue;

----------------------------------
Synchronization
----------------------------------

Multi-Component Synchronization
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All components operate synchronously:

1. All components operate for cycle N
2. All components complete cycle N
3. Advance to cycle N+1
4. Repeat

This ensures:

* Deterministic simulation
* No race conditions
* Reproducible results
* Simple debugging

Cross-Frequency Domains
~~~~~~~~~~~~~~~~~~~~~~~

If components have different frequencies:

* Use frequency ratios
* Scale cycle counters
* Maintain clock domain crossings
* Model synchronization latency

----------------------------------
Performance Measurement
----------------------------------

Key Timing Metrics
~~~~~~~~~~~~~~~~~~

* **IPC (Instructions Per Cycle)**: Core performance
* **CPI (Cycles Per Instruction)**: Inverse of IPC
* **MPKI (Misses Per Kilo Instructions)**: Cache effectiveness
* **Bandwidth utilization**: Actual vs. peak bandwidth
* **Queue occupancy**: Resource utilization

Cycle Accounting
~~~~~~~~~~~~~~~~

Track cycles spent in different states:

* Fetch stalls
* Decode stalls
* Execution cycles
* Memory wait cycles
* Branch misprediction recovery

Critical Path Analysis
~~~~~~~~~~~~~~~~~~~~~~

Identify performance bottlenecks:

* Longest latency operation
* Most frequent stall cause
* Resource contention points
* Bandwidth limitations
