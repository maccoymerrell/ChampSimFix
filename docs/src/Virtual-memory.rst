.. _Virtual_memory:

=====================================
Virtual Memory System
=====================================

ChampSim includes a virtual memory system that simulates address translation, page tables, and TLB behavior.

----------------------------------
Virtual Memory Manager
----------------------------------

The ``VirtualMemory`` class manages the mapping between virtual and physical addresses.

.. doxygenclass:: VirtualMemory
   :members:

Key Features
~~~~~~~~~~~~

* Virtual-to-physical page mapping
* Page allocation and deallocation
* Page table hierarchy simulation
* ASLR (Address Space Layout Randomization) support
* Memory region management

Address Translation
~~~~~~~~~~~~~~~~~~~

The virtual memory system translates virtual addresses to physical addresses:

1. Extract virtual page number from address
2. Look up page table entry
3. Return corresponding physical page number
4. Combine with page offset to form physical address

Page Tables
~~~~~~~~~~~

ChampSim simulates a page table hierarchy:

* Support for multi-level page tables
* Lazy allocation of page table pages
* Page table walk simulation
* TLB integration for fast translation

----------------------------------
Page Table Walker
----------------------------------

The ``PageTableWalker`` handles TLB misses and performs page table walks.

.. doxygenclass:: PageTableWalker
   :members:

Page Walk Process
~~~~~~~~~~~~~~~~~

When a TLB miss occurs:

1. Initiate page table walk
2. Access each level of page table hierarchy
3. Load page table entries from memory
4. Cache translation in TLB
5. Continue with translated address

PTW Builder
~~~~~~~~~~~

.. doxygenclass:: champsim::ptw_builder
   :members:

The PTW builder allows configuration of:

* Page table levels
* TLB organization
* Walk latencies
* Cache coherence with page table pages

----------------------------------
TLB (Translation Lookaside Buffer)
----------------------------------

The TLB caches recent address translations:

* Separate instruction and data TLBs
* Configurable size and associativity
* LRU or other replacement policies
* Support for huge pages
* Multi-level TLB hierarchy

TLB Organization
~~~~~~~~~~~~~~~~

* **ITLB**: Instruction TLB for code addresses
* **DTLB**: Data TLB for load/store addresses
* **STLB**: Shared second-level TLB
* **Page sizes**: 4KB, 2MB, 1GB pages

----------------------------------
Memory Regions
----------------------------------

ChampSim supports different memory regions:

* Code segments
* Data segments
* Stack regions
* Heap allocations
* Memory-mapped I/O (if configured)

Each region can have different properties:

* Read/write/execute permissions
* Cacheability
* Physical address mapping strategy
