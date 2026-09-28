.. _Cache_model:

=====================================
Cache Model
=====================================

The cache model in ChampSim is split into an **interface** and a **default implementation**:

* ``champsim::modules::cache_module`` is the abstract interface that defines the contract
  all cache implementations must fulfill. Prefetchers and replacement policies interact
  with their parent cache through this interface (e.g. ``get_mshr_occupancy_ratio()``,
  ``get_mshr_size()``, ``is_virtual_prefetch()``).

* ``CACHE`` is the default implementation of ``cache_module`` (registered as
  ``"DEFAULT_CACHE"``). It provides the standard set-associative cache model with
  configurable sets, ways, latencies, and queue sizes.

--------------------------------------
Configuration Parameters
--------------------------------------

These parameters can be set in the JSON configuration file (see
:ref:`Explicit_Config` or :ref:`Creating_Config`):

``num_sets``
    Number of cache sets.

``num_ways``
    Number of ways per set.

``mshr_size``
    Capacity of the miss status holding register (MSHR).

``pq_size``
    Prefetch queue capacity.

``hit_latency``
    Latency (in cycles) for a cache hit.

``fill_latency``
    Latency (in cycles) for a cache fill.

``offset_bits``
    Block offset bits (typed as ``{"bits": "6"}``).

``max_tag_bandwidth``
    Maximum tag lookups per cycle.

``max_fill_bandwidth``
    Maximum fills per cycle.

``prefetch_as_load``
    If ``true``, prefetch requests are treated as demand loads.

``match_offset_bits``
    If ``true``, matching includes the offset bits.

``virtual_prefetch``
    If ``true``, prefetcher operates on virtual addresses.

``pref_activate_mask``
    List of access types (e.g. ``["LOAD", "PREFETCH"]``) that trigger the prefetcher. ``LOAD`` and
    ``PREFETCH`` cover both sides; ``INSTRUCTION_LOAD``, ``DATA_LOAD``, ``INSTRUCTION_PREFETCH``, and
    ``DATA_PREFETCH`` select one. See `Access types`_.

--------------------------------------
Access types
--------------------------------------

Every request carries an ``access_type``: ``LOAD``, ``RFO``, ``PREFETCH``, ``WRITE``, or
``TRANSLATION``. Loads and prefetches also say which side they serve. The core issues instruction
fetches as ``INSTRUCTION_LOAD`` and memory operands as ``DATA_LOAD``, and a miss keeps its type as it
travels down the hierarchy. A cache's prefetches take the side of the access it is serving:
``INSTRUCTION_PREFETCH`` (whose ``ip`` is the prefetched line's address, since an instruction line's
PC is its address) or ``DATA_PREFETCH``.

``LOAD`` and ``PREFETCH`` are generic. In comparisons they equal either specialization
(``access_type::LOAD == access_type::DATA_LOAD``), while the specializations differ from each other
(``access_type::INSTRUCTION_LOAD != access_type::DATA_LOAD``). A prefetcher that checks
``type == access_type::LOAD`` therefore sees both sides, and one that needs the side compares against
the specific type or calls ``is_instruction_access(type)``. This equality is not transitive, so
containers keyed by ``access_type`` order on the underlying value.

Statistics are recorded per specific type. The ``LOAD`` and ``PREFETCH`` rows and JSON groups report
the sum of their specializations, and the specializations are reported alongside them.

--------------------------------------
Submodules
--------------------------------------

Each ``CACHE`` instance has two submodules attached through its ``"children"`` array:

* A **prefetcher** (``"module": "prefetcher"``), e.g. ``ip_stride``, ``next_line``, ``no``.
* A **replacement policy** (``"module": "replacement"``), e.g. ``lru``, ``srrip``, ``drrip``.

--------------------------------------
Internal Queues and Buffers
--------------------------------------

``MSHR``
    Miss Status Holding Register — tracks outstanding misses. Sized by ``mshr_size``.

``inflight_fills``
    Fills currently being written into the cache from the lower level.

Each cache also has request queues (RQ), write queues (WQ), and prefetch queues (PQ)
on its upper-level channels.

--------------------------------------
Key Public Methods
--------------------------------------

Lifecycle hooks
^^^^^^^^^^^^^^^^

``initialize()``, ``begin_phase()``, ``end_phase()``, ``end_simulation()``
    Called by the simulation framework at appropriate lifecycle points.

``operate()``
    Main per-cycle function. Processes tag lookups, fills, and prefetches each cycle.

Prefetcher interface
^^^^^^^^^^^^^^^^^^^^^^^

``prefetch_line(addr, fill_this_level, metadata)``
    Issue a prefetch request. Called from within a prefetcher module.

``invalidate_entry(addr)``
    Invalidate the cache line at the given address.

Status queries
^^^^^^^^^^^^^^^^

``get_mshr_occupancy()`` / ``get_mshr_size()`` / ``get_mshr_occupancy_ratio()``
    Query MSHR utilization.

``get_rq_occupancy()`` / ``get_wq_occupancy()`` / ``get_pq_occupancy()``
    Query request, write, and prefetch queue occupancies.

``num_sets()`` / ``num_ways()`` / ``get_offset_bits()``
    Cache geometry accessors.

``is_virtual_prefetch()``
    Whether the prefetcher operates on virtual addresses.

Statistics
^^^^^^^^^^^^

``get_sim_stats()`` / ``get_roi_stats()``
    Return performance counters for the full simulation and region-of-interest phases.

--------------------------------------
Module Interface Hooks
--------------------------------------

The ``CACHE`` class delegates to its submodules through these internal hooks:

**Prefetcher hooks:**
    ``impl_prefetcher_initialize()``, ``impl_prefetcher_cache_operate()``,
    ``impl_prefetcher_cache_fill()``, ``impl_prefetcher_cycle_operate()``,
    ``impl_prefetcher_final_stats()``, ``impl_prefetcher_branch_operate()``

**Replacement hooks:**
    ``impl_initialize_replacement()``, ``impl_find_victim()``,
    ``impl_update_replacement_state()``, ``impl_replacement_cache_fill()``,
    ``impl_replacement_final_stats()``

