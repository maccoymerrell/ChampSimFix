.. _DRAM_controller:

=====================================
DRAM Controller
=====================================

The DRAM controller in ChampSim manages memory access to the main memory system, implementing bank-level scheduling, row buffer management, and command queuing.

----------------------------------
Overview
----------------------------------

The ``MEMORY_CONTROLLER`` class simulates a realistic DRAM system with:

* Multiple channels, ranks, and banks
* Row buffer management with open/closed page policies
* Command queuing (activate, precharge, read, write)
* Timing constraints (tRCD, tRP, tCAS, tRAS)
* Bank group and rank parallelism
* Address mapping and bit swizzling

----------------------------------
DRAM Controller Class
----------------------------------

.. doxygenclass:: MEMORY_CONTROLLER
   :members:

----------------------------------
DRAM Address Mapping
----------------------------------

The DRAM controller uses sophisticated address mapping to distribute memory requests across channels, ranks, banks, and rows. The mapping can be configured to optimize for different access patterns.

Address Slicing
~~~~~~~~~~~~~~~~

Memory addresses are sliced into the following components:

* **Channel**: Which memory channel handles the request
* **Rank**: Which rank within the channel
* **Bank Group**: Groups of banks for improved parallelism
* **Bank**: Which bank within the bank group
* **Row**: Row address within the bank
* **Column**: Column address within the row

XOR Addressing
~~~~~~~~~~~~~~

ChampSim supports XOR-based address hashing to reduce bank conflicts and improve parallelism. This technique XORs different portions of the address to derive bank/channel indices.

----------------------------------
Bank Scheduling
----------------------------------

The DRAM controller implements a bank-level scheduler that:

1. Tracks the state of each bank (idle, active row, etc.)
2. Manages row buffer hits and misses
3. Issues precharge commands when needed
4. Schedules activate, read, and write commands
5. Enforces timing constraints between commands

Row Buffer Management
~~~~~~~~~~~~~~~~~~~~~

The controller maintains a row buffer for each bank:

* **Row Buffer Hit**: Request to the currently open row (fast access)
* **Row Buffer Miss**: Request to a different row (requires precharge + activate)
* **Row Buffer Conflict**: Request that conflicts with the open row

----------------------------------
Timing Parameters
----------------------------------

The DRAM controller enforces the following timing constraints:

* **tRCD**: RAS-to-CAS delay (activate to read/write)
* **tRP**: Row precharge time
* **tCAS**: CAS latency (read command to data)
* **tRAS**: Row active time (minimum activate period)
* **tRTP**: Read to precharge delay
* **tWR**: Write recovery time
* **tRRD**: Row-to-row activation delay

----------------------------------
Statistics
----------------------------------

.. doxygenclass:: dram_stats
   :members:

The DRAM controller tracks performance metrics including:

* Read/write request counts
* Row buffer hit rates
* Bank conflicts
* Average access latency
* Queue occupancy
