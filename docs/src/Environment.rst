.. _Environment:

=====================================
Environment and Configuration
=====================================

The environment in ChampSim orchestrates all simulation components and manages their lifecycle.

----------------------------------
Environment Interface
----------------------------------

The ``champsim::environment`` class provides the abstract interface for accessing simulation components.

.. doxygenclass:: champsim::environment
   :members:

----------------------------------
Environment Components
----------------------------------

The environment manages the following components:

CPU Cores
~~~~~~~~~

Access to all CPU cores in the system:

.. code-block:: cpp

   auto& cores = env.cpu_view();
   for (auto& cpu : cores) {
       // Access each core
       auto ipc = cpu.get_ipc();
   }

Cache Hierarchy
~~~~~~~~~~~~~~~

Access to all cache levels:

.. code-block:: cpp

   auto& caches = env.cache_view();
   for (auto& cache : caches) {
       // Access each cache
       auto hit_rate = cache.get_hit_rate();
   }

Cache levels include:

* L1 Instruction Cache (L1I)
* L1 Data Cache (L1D)
* L2 Cache (unified or split)
* Last-Level Cache (LLC)
* Victim caches (if configured)

Page Table Walkers
~~~~~~~~~~~~~~~~~~

Access to PTW components:

.. code-block:: cpp

   auto& ptws = env.ptw_view();
   for (auto& ptw : ptws) {
       // Access each PTW
       auto tlb_misses = ptw.get_misses();
   }

Memory Controllers
~~~~~~~~~~~~~~~~~~

Access to DRAM controllers:

.. code-block:: cpp

   auto& controllers = env.dram_view();
   for (auto& dram : controllers) {
       // Access each controller
       auto bandwidth = dram.get_bandwidth_utilization();
   }

----------------------------------
Configuration System
----------------------------------

ChampSim uses JSON for configuration.

Configuration File Format
~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: json

   {
     "executable_name": "champsim",
     "num_cores": 1,
     "physical_memory": {
       "size": 4294967296
     },
     "cores": [
       {
         "frequency": 4000,
         "ifetch_buffer_size": 64,
         "decode_buffer_size": 32
       }
     ],
     "caches": [
       {
         "name": "L1D",
         "type": "data",
         "sets": 64,
         "ways": 8,
         "latency": 4,
         "prefetcher": "ip_stride",
         "replacement": "lru"
       }
     ]
   }

Configuration Parameters
~~~~~~~~~~~~~~~~~~~~~~~~

**Core Configuration:**

* ``frequency``: Core clock frequency in MHz
* ``ifetch_buffer_size``: Instruction fetch buffer size
* ``decode_buffer_size``: Decode buffer size
* ``dispatch_buffer_size``: Dispatch buffer size
* ``rob_size``: Reorder buffer size
* ``lq_size``: Load queue size
* ``sq_size``: Store queue size
* ``fetch_width``: Instructions fetched per cycle
* ``decode_width``: Instructions decoded per cycle
* ``dispatch_width``: Instructions dispatched per cycle
* ``execute_width``: Instructions executed per cycle
* ``retire_width``: Instructions retired per cycle

**Cache Configuration:**

* ``name``: Cache identifier
* ``type``: Cache type (instruction/data/unified)
* ``sets``: Number of sets
* ``ways``: Associativity
* ``latency``: Hit latency in cycles
* ``mshr_size``: MSHR entries
* ``prefetcher``: Prefetcher module name
* ``replacement``: Replacement policy name
* ``prefetch_as_load``: Treat prefetches as loads
* ``virtual_prefetch``: Use virtual addresses for prefetch
* ``prefetch_activate``: Policy for prefetch activation

**DRAM Configuration:**

* ``channels``: Number of memory channels
* ``ranks``: Ranks per channel
* ``banks``: Banks per rank
* ``rows``: Rows per bank
* ``columns``: Columns per row
* ``channel_width``: Data width in bytes
* ``io_frequency``: I/O bus frequency
* ``tCAS``: CAS latency
* ``tRCD``: RAS to CAS delay
* ``tRP``: Row precharge time
* ``tRAS``: Row active time

----------------------------------
Builder Pattern
----------------------------------

Builders construct configured components:

Core Builder
~~~~~~~~~~~~

The core builder configures CPU cores:

.. doxygenclass:: champsim::core_builder
   :members:

Example usage:

.. code-block:: cpp

   champsim::core_builder builder;
   builder.rob_size(256)
          .lq_size(64)
          .sq_size(48)
          .fetch_width(6)
          .decode_width(6)
          .retire_width(5);

   auto core = builder.build();

Cache Builder
~~~~~~~~~~~~~

The cache builder configures cache levels:

.. doxygenclass:: champsim::cache_builder
   :members:

Example usage:

.. code-block:: cpp

   champsim::cache_builder builder;
   builder.name("L2")
          .sets(512)
          .ways(8)
          .latency(12)
          .mshr_size(16)
          .prefetcher<ip_stride>()
          .replacement<lru>();

   auto cache = builder.build();

PTW Builder
~~~~~~~~~~~

The PTW builder configures page table walkers:

.. doxygenclass:: champsim::ptw_builder
   :members:

----------------------------------
Environment Lifecycle
----------------------------------

Initialization Phase
~~~~~~~~~~~~~~~~~~~~

1. Parse configuration file
2. Create all components using builders
3. Connect components (cache hierarchy, memory)
4. Initialize modules (branch predictor, prefetchers, etc.)
5. Load trace files

Warmup Phase
~~~~~~~~~~~~

1. Execute warmup instructions
2. Fill caches with working set
3. Train branch predictors
4. Do not collect statistics

Simulation Phase
~~~~~~~~~~~~~~~~

1. Reset statistics counters
2. Execute region of interest (ROI) instructions
3. Collect performance metrics
4. Track all cache/memory behavior

Completion Phase
~~~~~~~~~~~~~~~~

1. Drain all pending requests
2. Finalize statistics
3. Call module ``final_stats()`` functions
4. Print statistics (JSON or plain text)
5. Clean up resources

----------------------------------
Multi-Core Simulation
----------------------------------

ChampSim supports multi-core simulation:

Core Count Configuration
~~~~~~~~~~~~~~~~~~~~~~~~

Specify number of cores in configuration:

.. code-block:: json

   {
     "num_cores": 4,
     "cores": [
       { "frequency": 4000 },
       { "frequency": 4000 },
       { "frequency": 3000 },
       { "frequency": 3000 }
     ]
   }

Shared Resources
~~~~~~~~~~~~~~~~

Cores can share resources:

* Last-Level Cache (LLC)
* Memory controllers
* Memory buses
* Interconnect

Private Resources
~~~~~~~~~~~~~~~~~

Each core typically has private:

* L1 Instruction Cache
* L1 Data Cache
* L2 Cache (often)
* Branch predictors
* TLBs

Coherence
~~~~~~~~~

Cache coherence (if enabled):

* MESI or MOESI protocol
* Invalidations and updates
* Directory or snooping
* Coherence traffic

----------------------------------
Trace Files
----------------------------------

Input trace files specify program behavior:

Trace Format
~~~~~~~~~~~~

ChampSim traces contain:

* Instruction addresses
* Branch information
* Memory addresses accessed
* Register dependencies (optional)

Trace Loading
~~~~~~~~~~~~~

Traces are loaded and replayed:

1. Open trace file
2. Decompress (if gzipped/xzipped)
3. Read instructions
4. Feed to core for execution

Multiple Traces
~~~~~~~~~~~~~~~

For multi-core simulation:

* Each core can have its own trace
* Traces run independently
* Share memory and caches
* Synchronize on barriers (optional)
