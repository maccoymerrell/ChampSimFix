.. _Channel:

=====================================
Channel and Communication
=====================================

The channel system manages communication between different levels of the cache hierarchy.

----------------------------------
Channel Overview
----------------------------------

The ``channel`` class manages queues for requests between cache levels and implements queue management, bandwidth constraints, and collision detection.

.. doxygenclass:: channel
   :members:

----------------------------------
Queue Types
----------------------------------

Each channel has three types of queues:

Read Queue (RQ)
~~~~~~~~~~~~~~~

The read queue handles demand read requests:

* Load instructions from CPU
* Cache line fills from lower levels
* Instruction fetches
* Translation requests

Characteristics:

* Prioritized over prefetch requests
* Subject to bandwidth constraints
* Tracked for occupancy statistics
* FIFO or priority-based ordering

Prefetch Queue (PQ)
~~~~~~~~~~~~~~~~~~~

The prefetch queue handles speculative prefetch requests:

* Hardware prefetcher-generated requests
* Lower priority than demand requests
* Can be throttled based on accuracy
* May be dropped under high load

Characteristics:

* Separate from demand traffic
* Bandwidth-limited
* Can be filtered or merged
* Tracked for prefetch effectiveness

Write Queue (WQ)
~~~~~~~~~~~~~~~~

The write queue handles store and writeback requests:

* Store instructions from CPU
* Dirty cache line evictions
* Writebacks from upper levels
* Write-through traffic (if enabled)

Characteristics:

* Write-back or write-through policy
* Coalescing of writes to same block
* Separate bandwidth allocation
* Writeback buffer integration

----------------------------------
Queue Management
----------------------------------

Request Insertion
~~~~~~~~~~~~~~~~~

When inserting a request into a queue:

1. Check bandwidth availability
2. Check queue occupancy
3. Check for address collisions
4. Insert or merge with existing request
5. Update statistics

Collision Detection
~~~~~~~~~~~~~~~~~~~

The channel detects and handles address collisions:

* **Read-Read collision**: Merge reads to same address
* **Read-Write collision**: Enforce ordering
* **Write-Write collision**: Coalesce writes
* **Prefetch collision**: Drop redundant prefetches

Request Scheduling
~~~~~~~~~~~~~~~~~~

Requests are scheduled based on:

* Request type priority (demand > prefetch)
* Age (older requests first)
* Bank/row locality in DRAM
* Bandwidth availability

----------------------------------
Bandwidth Management
----------------------------------

Each queue has bandwidth constraints:

.. doxygenclass:: bandwidth
   :members:

Bandwidth Allocation
~~~~~~~~~~~~~~~~~~~~

Bandwidth is allocated per cycle:

* Maximum requests per cycle
* Maximum bytes per cycle
* Separate limits for each queue type
* Can be configured per cache level

Bandwidth Enforcement
~~~~~~~~~~~~~~~~~~~~~

The channel enforces bandwidth limits:

1. Track bandwidth usage per cycle
2. Only schedule requests within limits
3. Reset bandwidth counters each cycle
4. Queue requests that exceed bandwidth

----------------------------------
Statistics Tracking
----------------------------------

The channel tracks detailed statistics:

Queue Statistics
~~~~~~~~~~~~~~~~

* Queue occupancy (average, max)
* Queue full cycles
* Requests processed
* Requests merged
* Requests dropped

Collision Statistics
~~~~~~~~~~~~~~~~~~~~

* Address collisions detected
* Collision types (RR, RW, WW)
* Merged requests
* Ordering enforcements

Bandwidth Statistics
~~~~~~~~~~~~~~~~~~~~

* Bandwidth utilization
* Stall cycles due to bandwidth
* Peak bandwidth usage
* Per-queue-type bandwidth

----------------------------------
Integration with Caches
----------------------------------

The channel integrates with the cache hierarchy:

Upper Level
~~~~~~~~~~~

Requests from upper cache levels:

* Arrive in the channel's input queues
* Wait for processing by this cache
* May be serviced from this cache or forwarded

Lower Level
~~~~~~~~~~~

Requests to lower cache levels:

* Issued from this cache on miss
* Sent to lower level's channel
* Responses return through the hierarchy

----------------------------------
Deadlock Prevention
----------------------------------

The channel implements deadlock prevention:

* Resource limits to prevent queue overflow
* Progress guarantees for demand traffic
* Prefetch throttling under pressure
* Buffer reservation for responses

Livelock Prevention
~~~~~~~~~~~~~~~~~~~

* Aging mechanism for stalled requests
* Priority elevation for old requests
* Forced scheduling of starved requests
