.. _Statistics:

=====================================
Statistics and Reporting
=====================================

ChampSim collects detailed performance statistics and provides multiple output formats for analysis.

----------------------------------
Core Statistics
----------------------------------

The ``core_stats`` class tracks CPU core performance metrics.

.. doxygenclass:: core_stats
   :members:

Key Metrics
~~~~~~~~~~~

* **Instructions retired**: Total instructions completed
* **IPC (Instructions Per Cycle)**: Performance metric
* **Branch predictions**: Correct/incorrect predictions
* **Branch misprediction rate**: Percentage of mispredicted branches
* **Cache access statistics**: Hits, misses per cache level
* **Load/Store queue occupancy**: Average queue utilization
* **ROB occupancy**: Reorder buffer usage
* **Cycles spent**: Total simulation cycles

----------------------------------
Cache Statistics
----------------------------------

The ``cache_stats`` class tracks cache performance for each cache level.

.. doxygenclass:: cache_stats
   :members:

Cache Metrics
~~~~~~~~~~~~~

* **Hits**: Accesses that hit in the cache
* **Misses**: Accesses that miss in the cache
* **Evictions**: Cache lines evicted
* **Writebacks**: Dirty lines written back
* **Prefetch accuracy**: Useful prefetches / total prefetches
* **Coverage**: Misses covered by prefetches
* **Bandwidth utilization**: Actual usage vs. available bandwidth
* **Average access latency**: Mean latency per access

Per-Access-Type Statistics
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Statistics are broken down by access type:

* ``LOAD``: Regular load operations
* ``RFO``: Read-for-ownership (atomic operations)
* ``PREFETCH``: Prefetch requests
* ``WRITE``: Store operations
* ``TRANSLATION``: Page table accesses

----------------------------------
DRAM Statistics
----------------------------------

The ``dram_stats`` class tracks main memory performance.

.. doxygenclass:: dram_stats
   :members:

DRAM Metrics
~~~~~~~~~~~~

* **Read/write requests**: Total memory operations
* **Row buffer hits**: Accesses to open rows
* **Row buffer misses**: Accesses requiring row changes
* **Bank conflicts**: Requests to busy banks
* **Average queue depth**: Mean request queue occupancy
* **Channel utilization**: Per-channel bandwidth usage

----------------------------------
Statistics Printers
----------------------------------

ChampSim supports multiple output formats for statistics.

Abstract Printer Interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: stats_printer
   :members:

Plain Text Printer
~~~~~~~~~~~~~~~~~~

The plain text printer outputs human-readable statistics:

* Section headers for each component
* Tabular format for metrics
* Summary statistics
* Easy to parse with scripts

.. code-block:: text

   CPU 0 cumulative IPC: 1.234 instructions: 100000000 cycles: 81037277

   BRANCH PREDICTOR
   Correct: 12345678 (98.5%)
   Wrong: 187654 (1.5%)

JSON Printer
~~~~~~~~~~~~

The JSON printer outputs machine-readable statistics:

* Structured JSON format
* Hierarchical organization
* Easy integration with analysis tools
* Supports automated processing

.. code-block:: json

   {
     "cpu": [
       {
         "ipc": 1.234,
         "instructions": 100000000,
         "cycles": 81037277,
         "branch_prediction": {
           "correct": 12345678,
           "wrong": 187654
         }
       }
     ]
   }

----------------------------------
Event Counters
----------------------------------

The ``event_counter`` class provides a simple counter for tracking events.

.. doxygenclass:: event_counter
   :members:

Usage Example
~~~~~~~~~~~~~

.. code-block:: cpp

   event_counter my_counter;
   my_counter++; // Increment counter

   // At end of simulation
   std::cout << "Events: " << my_counter << std::endl;

----------------------------------
Custom Statistics
----------------------------------

Modules can implement custom statistics reporting:

Branch Predictor Statistics
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Branch predictors may report:

* Prediction accuracy by branch type
* History table utilization
* Training overhead
* Branch target buffer hit rate

Prefetcher Statistics
~~~~~~~~~~~~~~~~~~~~~~

Prefetchers may report:

* Prefetch accuracy (useful / total)
* Prefetch coverage (hits on prefetched lines)
* Prefetch lateness/timeliness
* Bandwidth consumption

Replacement Policy Statistics
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Replacement policies may report:

* Eviction reasons (capacity, conflict)
* Set utilization
* Thrashing detection
* Adaptive policy decisions

----------------------------------
Final Statistics Hooks
----------------------------------

Modules can implement final statistics functions:

* ``prefetcher_final_stats()``: Called for prefetchers
* ``replacement_final_stats()``: Called for replacement policies

These functions are invoked at the end of simulation to output module-specific statistics.
