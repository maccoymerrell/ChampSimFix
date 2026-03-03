.. _Operable:

=====================================
Operable Interface
=====================================

The operable interface defines the lifecycle and timing model for all simulated components in ChampSim.

----------------------------------
Operable Base Class
----------------------------------

All cycle-driven components inherit from the ``operable`` class.

.. doxygenclass:: operable
   :members:

----------------------------------
Component Lifecycle
----------------------------------

Each operable component follows a defined lifecycle:

Initialization
~~~~~~~~~~~~~~

1. Component construction
2. Configuration parameter setup
3. Connection to other components
4. Module initialization (``initialize_*()`` functions)

Simulation Phases
~~~~~~~~~~~~~~~~~

The simulation runs in phases:

* **Warmup phase**: Bring caches to steady state
* **ROI phase**: Region of interest for measurements
* **Post-ROI phase**: Drain outstanding requests

Phase Hooks
^^^^^^^^^^^

Components can implement phase hooks:

.. cpp:function:: void begin_phase()

   Called when a new simulation phase begins. Components can reset statistics, change behavior, or prepare for the new phase.

.. cpp:function:: void end_phase()

   Called when a simulation phase ends. Components can finalize phase-specific statistics or clean up state.

----------------------------------
Cycle-by-Cycle Operation
----------------------------------

The main simulation loop calls ``operate()`` on each component every cycle.

The Operate Function
~~~~~~~~~~~~~~~~~~~~

.. cpp:function:: void operate()

   This function is called once per cycle and should:

   1. Process any pending work for this cycle
   2. Update internal state
   3. Interact with other components
   4. Advance time-based state machines

Operation Order
~~~~~~~~~~~~~~~

Components operate in a defined order each cycle:

1. **Cores**: Fetch, decode, execute, commit
2. **Caches**: Process pending requests, check tags
3. **Prefetchers**: Generate prefetch requests
4. **Replacement**: Update replacement state
5. **Memory controller**: Schedule DRAM commands
6. **Statistics**: Update counters

----------------------------------
Timing Model
----------------------------------

ChampSim uses a discrete event timing model:

Cycle Accuracy
~~~~~~~~~~~~~~

* All components advance in lock-step
* One global clock for all components
* Cycle-accurate simulation
* No sub-cycle timing

Latency Modeling
~~~~~~~~~~~~~~~~

Latencies are modeled with:

* Multi-cycle operations
* State machines for complex operations
* Queues for pipelined stages
* Cycle counters for timed events

Example: Cache Access
^^^^^^^^^^^^^^^^^^^^^

1. **Cycle 0**: Request arrives, check tags
2. **Cycle 1-N**: Tag lookup latency
3. **Cycle N**: Hit/miss determined
4. **Cycle N+1**: Data return (on hit) or forward (on miss)

----------------------------------
Component Interactions
----------------------------------

Components interact through well-defined interfaces:

Request-Response Model
~~~~~~~~~~~~~~~~~~~~~~~

1. Component A sends request to Component B
2. Request enters Component B's queue
3. Component B processes request in ``operate()``
4. Component B sends response back to Component A
5. Response tracked until completion

Callback Model
~~~~~~~~~~~~~~

Some interactions use callbacks:

* Cache fill notifications to prefetchers
* Branch resolution to predictors
* TLB miss handlers
* Interrupt handlers

----------------------------------
Waitable Interface
----------------------------------

The ``waitable`` interface supports asynchronous operations:

.. doxygenclass:: waitable
   :members:

Usage Pattern
~~~~~~~~~~~~~

.. code-block:: cpp

   // Initiate asynchronous operation
   waitable<response_type> future = start_operation();

   // Later, check if complete
   if (future.ready()) {
       auto result = future.get();
       // Process result
   }

----------------------------------
Repeatable Interface
----------------------------------

The ``repeatable`` interface supports reusable simulation objects:

.. doxygenclass:: repeatable
   :members:

Use Cases
~~~~~~~~~

* Simulation checkpointing
* Multiple simulation runs
* Parameter sweeps
* Reproducible results

----------------------------------
Builder Pattern
----------------------------------

Complex components use builders for configuration:

Core Builder
~~~~~~~~~~~~

.. doxygenclass:: champsim::core_builder
   :members:

Cache Builder
~~~~~~~~~~~~~

.. doxygenclass:: champsim::cache_builder
   :members:

PTW Builder
~~~~~~~~~~~

.. doxygenclass:: champsim::ptw_builder
   :members:

Builder Pattern Benefits
~~~~~~~~~~~~~~~~~~~~~~~~

* Separates construction from representation
* Allows complex configuration
* Provides compile-time and runtime validation
* Supports different configuration sources (JSON, command-line)

----------------------------------
Error Handling
----------------------------------

Components should handle errors gracefully:

Assertion Checks
~~~~~~~~~~~~~~~~

* Validate invariants
* Check preconditions
* Detect impossible states
* Help with debugging

Deadlock Detection
~~~~~~~~~~~~~~~~~~

.. doxygenclass:: deadlock_detector
   :members:

The deadlock detector monitors component progress:

* Tracks forward progress
* Detects circular dependencies
* Reports deadlock conditions
* Aids in debugging hangs
