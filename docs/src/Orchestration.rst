.. _Orchestration:

====================================
Simulation Orchestration
====================================

ChampSim's simulation loop is itself modular. The orchestrator knows nothing about
instructions, cores, or traces: it ticks *operables*, asks *phase controllers* when the
run is done, and reports progress through *listeners*. Work flows from *packet producers*
into *packet consumers* as opaque **packets** — instructions for a core, but equally
packets for a network consumer or records for a memory-stream consumer. With the right
modules defined, an explicit configuration can assume any shape (an OoO CPU simulator,
a memory-only system, a network simulator) without modifying ChampSim source.

The legacy configuration format hides all of this: it always builds the classic
out-of-order CPU simulator with the default orchestration modules.

------------------------------------------
The Simulation Loop
------------------------------------------

Each phase, the orchestrator:

1. Collects every **operable** in the environment (see :ref:`Orch_Discovery`) and calls
   ``begin_phase`` on modules that opt into phase notifications.
2. Ticks a global picosecond clock by the smallest clock period among the operables, and
   lets every operable catch up to it (``operable::operate_on``). Operables in slower
   clock domains naturally run on fewer ticks.
3. Sums the *progress* returned by every ``operate()`` call and passes it to every phase
   controller's ``advance()``.
4. Ends the phase when all controllers report completion, or aborts when any reports
   failure.

Phases are described by ``champsim::phase_info``: a name, an ``is_warmup`` flag, an
``roi`` flag, and a length denominated in **each consumer's own progress unit**
(instructions for a core). ``roi`` defaults to ``!is_warmup``; setting it explicitly
allows unmeasured non-warmup phases (e.g. a fast-forward region between warmup and the
measured region). Statistics are collected only for ROI phases.

------------------------------------------
Packets: Producers and Consumers
------------------------------------------

A **packet** is one discrete unit of work. Producers produce packets; consumers execute them.
The orchestration layer never sees the packet type — a consumer and its producers agree on
it by construction.

Packet producers
^^^^^^^^^^^^^^^^^^^^

``champsim::modules::packet_producer`` is the base contract every producer satisfies: the
packet-agnostic lifecycle plus the producer identity stamped on its packets:

.. code-block:: cpp

    struct packet_producer {
      virtual bool eof() const = 0;              // no more packets, ever
      virtual std::string describe() const;      // e.g. the trace path, for reports
      uint32_t producer_id() const;                // which producer produced the packet
      const std::string& producer_group() const;   // the "producer_group" sharing label, if any
    };

The typed pull protocol lives on a template subclass:

.. code-block:: cpp

    template <typename Packet>
    struct typed_packet_producer : packet_producer {
      virtual const Packet* peek() = 0;   // next packet without consuming; nullptr = none now
      virtual void consume() = 0;        // discard the peeked packet
      std::optional<Packet> next();       // peek + consume in one step
    };

``peek()`` is always safe to call — a null return is the emptiness signal, so exhausted
and empty producers need no special-casing. Paced consumers can ``peek()`` a packet, wait
until it is due, and only then ``consume()`` it.

``champsim::modules::instruction_producer`` extends ``typed_packet_producer<ooo_model_instr>``
with the execution-driven feedback hooks (``retire_instruction``, ``squash_instruction``,
``branch_mispredict``). It is the registered interface a core attaches; the shipped
``INSTRUCTION_PRODUCER`` model reads a trace file:

* ``trace_file`` (string) — path to the trace
* ``producer_group`` (optional string) — sharing label: producers with the same label share
  one producer id; unlabeled producers each get their own
* ``cloudsuite``, ``repeat`` (optional booleans)

Producers are declared as ``children`` of their consumer and are bound to it after
construction.

Packet consumers
^^^^^^^^^^^^^^^^^^^^

Any module that executes packets inherits the ``champsim::modules::packet_consumer``
mixin. It is the orchestration layer's entire view of "the thing doing work":

.. code-block:: cpp

    struct packet_consumer {
      int consumer_id() const;                     // hardware-context id, framework-assigned
      virtual uint64_t sim_progress() const;       // cumulative packets completed
      virtual bool producers_eof() const;             // all attached producers exhausted
      virtual consumer_health check_health(uint64_t elapsed);  // periodic self-check
      virtual void reset_health();                 // re-baseline at phase start
      virtual bool has_pending_work() const;       // scheduled future work (paced gaps)
      // report formatting: producer_finish_message, phase_complete_message
    };

Two contracts deserve emphasis:

* **Health is the consumer's own judgment.** The consumer knows its expected progress
  rate (a core knows what a plausible IPC floor is; a packet injector knows its schedule),
  so the phase controller delegates the liveness decision to ``check_health``: it is
  called every ``health_period`` cycles, and a ``stalled`` verdict aborts the run.
  ``core_module`` implements the instruction-rate policy.
* **Scheduled quiet time is not a deadlock.** ``has_pending_work()`` returns true when the
  consumer knows more work arrives at a known future time (a paced producer waiting out a
  gap). While any consumer — or any operable, see below — reports pending work, zero
  global progress does not advance the deadlock counter.

Consumer ids are framework-assigned by enumerating consumers in declaration order, dense
from 0; they are never set in a configuration. The consumer count — derived automatically
by counting the consumers in the config — sizes per-consumer tables such as ``ship`` and
``drrip`` replacement state.

------------------------------------------
Phase Controllers
------------------------------------------

A phase controller decides when a phase is complete and when a run is unhealthy. It is
an ordinary module (interface ``phase_controller``, parent: the environment):

.. code-block:: cpp

    struct phase_controller {
      virtual void begin_phase(const std::string& name, bool is_warmup, uint64_t length) = 0;
      virtual status advance(long progress) = 0;   // CONTINUE, COMPLETE, or ABORT
      virtual std::vector<unsigned> newly_completed_consumers() const = 0;
      virtual void end_phase() = 0;
      virtual std::vector<phase_info> get_phases() const;  // non-empty = owns run structure
    };

The generic shipped model is ``PHASE_CONTROLLER``. It is packet-agnostic and owns only
generic mechanics:

* **Completion** — a consumer completes when its ``sim_progress()`` delta reaches the
  phase length (in its own packet unit), or when one of its producers signals end-of-stream.
* **Deadlock** — ``deadlock_cycles`` consecutive zero-progress cycles abort the run,
  unless a consumer or operable reports ``has_pending_work()``.
* **Health** — every ``health_period`` cycles each governed consumer's ``check_health``
  runs; a ``stalled`` verdict aborts.

Parameters::

    {
        "name": "pc", "module": "phase_controller", "model": "PHASE_CONTROLLER",
        "deadlock_cycles": 500,
        "health_period": 10000000,
        "eof_policy": "complete_all",
        "warmup_length": "$warmup_instructions",
        "simulation_length": "$simulation_instructions"
    }

By default a trace producer replays (``repeat: true``): it reopens the trace at the end and
never signals end-of-stream, so it feeds instructions until the consumer reaches the phase
length. ``eof_policy`` matters only for a *finite* producer that does signal end-of-stream (a
non-repeating trace, or a bounded generator): ``"complete_all"`` (default) ends the phase
for every consumer at that first signal; ``"complete_consumer"`` retires only the consumer
whose producer ended and lets the others run on to their own phase lengths (heterogeneous
mixes). Any value other than the exact string ``"complete_consumer"`` is treated as
``"complete_all"``.

Instead of ``warmup_length``/``simulation_length``, a controller may declare an
arbitrary phase list::

    "phases": [
        {"name": "Warmup",      "is_warmup": true,  "length": 10000000},
        {"name": "FastForward", "is_warmup": false, "roi": false, "length": 50000000},
        {"name": "Measured",    "is_warmup": false, "length": 100000000}
    ]

**Multiple controllers.** A configuration may declare any number of phase controllers.
Each may name the consumer ids it governs (``"consumers": [0, 1]``; default: all), so
different completion and health policies can apply to different parts of a heterogeneous
system. Composition rules: the phase aborts if *any* controller aborts, completes when
*all* controllers report complete, and the phase list is taken from the first controller
with a non-empty ``get_phases()`` (conflicting non-empty lists are a config error).

When a configuration declares no controller, the orchestrator creates a default
``PHASE_CONTROLLER`` and the ``-w``/``-i`` CLI options define the phases — this is the
legacy environment's path.

------------------------------------------
Listeners
------------------------------------------

Listeners are compile-time instrumentation, not modules. They live in
``inc/listeners/`` and observe typed **events** emitted at hook points throughout the
simulator (``inc/events.h`` — ``BEGIN_PHASE`` from the run loop, ``RETIRE`` from a core).
The set of listeners is a ``std::tuple`` in ``inc/event_listeners.h``; a hook site fires an
event with ``handle_event<Event::X>(args...)``, which dispatches to every activated
listener's ``handle_event<Event::X>`` specialization. Adding an event means extending the
enum and placing a hook; adding a listener means dropping a struct into ``inc/listeners/``
and adding it to the tuple. Users are not expected to author listeners in the common course
of using ChampSim, so they are not configured as modules.

The always-on ``Heartbeat`` (``inc/listeners/heartbeat.h``) consumes ``RETIRE`` to print the
periodic ``Heartbeat CPU N instructions: ... cumulative IPC: ...`` line.

Selection:

* Activate a compiled-in listener on the command line: ``--listeners Heartbeat``. The
  heartbeat is index 0 and always active.
* The heartbeat interval comes from the root-level ``"heartbeat_frequency"`` key;
  ``--hide-heartbeat`` quiets every core so it stops emitting ``RETIRE`` events.

------------------------------------------
Idle Cycle Skipping
------------------------------------------

Operables may skip cycles in which they have no work. The hook:

.. code-block:: cpp

    class operable {
      // Called before each local cycle is simulated, with current_time already
      // advanced to the cycle under consideration.
      //   return 0 -> simulate this cycle (operate() runs)
      //   return n -> skip n local cycles without simulation
      virtual long poll_cycle() { return 0; }
    };

A skipped cycle advances ``current_time`` and contributes no progress — exactly like an
idle ``operate()`` — so observable behavior is unchanged. Implementations must uphold
two rules:

* **Parent-ticked hooks continue.** Any submodule hook that is contractually per-cycle
  must still fire on skipped cycles. The cache's ``poll_cycle`` calls
  ``prefetcher_cycle_operate`` (and keeps its upper-level round-robin aligned) while
  skipping, so prefetcher-internal clocks observe an unbroken cycle stream.
* **Push-fed operables skip at most one cycle at a time.** A module that can receive work
  by external push (channel enqueue, returned-queue push) cannot know that nothing will
  arrive; sleeping further would add latency. Returns greater than 1 are reserved for
  self-contained modules with no external inputs.

All shipped operables implement the hook (cache, core, memory controller, page-table
walker). Skipping is enabled by default; the root config key ``"cycle_skip": false``
disables it globally, which is the switch used to A/B-verify that skipping does not change
results.

``operable::has_pending_work()`` is the related liveness contract: return true only for
**timer-scheduled** work that completes at a known future time without external input
(a DRAM refresh in flight, a busy bank). Do *not* return true for work that waits on
another module's response — that is exactly the state the deadlock detector exists to
catch.

.. _Orch_Discovery:

------------------------------------------
Discovery: Views and Enrollment
------------------------------------------

The environment exposes every constructed module through ``view(interface_name)`` /
``typed_view<T>(interface_name)``. Two aggregate keys cut across interfaces:

* ``"operable"`` — every instance that inherits ``champsim::operable``, whether the
  *interface* or only the *model* inherits it. Anything in this view is ticked
  automatically by the orchestrator.
* ``"packet_consumer"`` — every instance that inherits ``packet_consumer``.

Submodule-created instances participate: when a parent constructs its children (a core
its producers, a cache its prefetchers), each instance self-enrolls with the environment,
appended after the top-level modules. An operable packet producer nested under a consumer
is therefore found and ticked with no additional wiring. ``get_num(name)`` always agrees
with ``view(name).size()``.

Modules opt into further orchestration roles by inheriting mixins:

* ``champsim::module_phase`` — receives ``begin_phase(warmup, roi)`` / ``end_phase``.
* ``champsim::module_stat`` — contributes lines and JSON to the per-phase statistics.

------------------------------------------
Root Configuration Key Reference
------------------------------------------

``environment``
    Environment model: ``"LEGACY_ENVIRONMENT"`` (default; classic config format) or
    ``"ENVIRONMENT"`` (explicit format).
``cycle_skip``
    Enable idle cycle skipping (default ``true``).
``heartbeat_frequency``
    Interval, in each consumer's own progress unit, for the default heartbeat listener.
``block_size``, ``page_size``
    System-wide geometry, published to all modules via the builder globals.

Any other non-reserved top-level scalar is likewise published as a global; see
:doc:`Explicit-configuration-format` for globals and lexical scoping.
