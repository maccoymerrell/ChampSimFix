.. _Orchestration:

====================================
Simulation Orchestration
====================================

ChampSim's simulation loop is itself modular. The orchestrator knows nothing about
instructions, cores, or traces: it ticks *operables*, asks *phase controllers* when the
run is done, and reports progress through *listeners*. Work flows from *workload sources*
into *source consumers* as opaque **tokens** — instructions for a core, but equally
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
Tokens: Sources and Consumers
------------------------------------------

A **token** is one discrete unit of work. Sources produce tokens; consumers execute them.
The orchestration layer never sees the token type — a consumer and its sources agree on
it by construction.

Workload sources
^^^^^^^^^^^^^^^^^^^^

``champsim::modules::workload_source`` carries the token-agnostic lifecycle:

.. code-block:: cpp

    struct workload_source {
      virtual bool eof() const = 0;              // no more tokens, ever
      virtual std::string describe() const;      // e.g. the trace path, for reports
    };

The typed pull protocol lives on a template subclass:

.. code-block:: cpp

    template <typename Token>
    struct typed_workload_source : workload_source {
      virtual const Token* peek() = 0;   // next token without consuming; nullptr = none now
      virtual void consume() = 0;        // discard the peeked token
      std::optional<Token> next();       // peek + consume in one step
    };

``peek()`` is always safe to call — a null return is the emptiness signal, so exhausted
and empty sources need no special-casing. Paced consumers can ``peek()`` a token, wait
until it is due, and only then ``consume()`` it.

``champsim::modules::instruction_source`` is ``typed_workload_source<ooo_model_instr>``
plus the execution-driven feedback hooks (``retire_instruction``, ``squash_instruction``,
``branch_mispredict``). The shipped ``TRACE_WORKLOAD_SOURCE`` model reads a trace file:

* ``trace_file`` (string) — path to the trace
* ``stream`` (optional string) — sharing label: sources with the same label share one
  framework-assigned stream (address space); unlabeled sources each get their own
  (see :ref:`Orch_Origin`)
* ``cloudsuite``, ``repeat`` (optional booleans)

Sources are declared as ``children`` of their consumer and are bound to it after
construction (the protected ``consumer_`` member).

Source consumers
^^^^^^^^^^^^^^^^^^^^

Any module that executes tokens inherits the ``champsim::modules::source_consumer``
mixin. It is the orchestration layer's entire view of "the thing doing work":

.. code-block:: cpp

    struct source_consumer {
      int consumer_id() const;                     // hardware-context id, framework-assigned
      virtual uint64_t sim_progress() const;       // cumulative tokens completed
      virtual bool source_eof() const;             // all attached sources exhausted
      virtual source_health check_health(uint64_t elapsed);  // periodic self-check
      virtual void reset_health();                 // re-baseline at phase start
      virtual bool has_pending_work() const;       // scheduled future work (paced gaps)
      // report formatting: source_finish_message, phase_complete_message, progress_message
    };

Two contracts deserve emphasis:

* **Health is the consumer's own judgment.** The consumer knows its expected progress
  rate (a core knows what a plausible IPC floor is; a packet injector knows its schedule),
  so what was once a central "livelock detector" is now ``check_health``: the phase
  controller calls it every ``health_period`` cycles, and a ``stalled`` verdict aborts
  the run. ``core_module`` implements the classic instruction-rate policy.
* **Scheduled quiet time is not a deadlock.** ``has_pending_work()`` returns true when the
  consumer knows more work arrives at a known future time (a paced source waiting out a
  gap). While any consumer — or any operable, see below — reports pending work, zero
  global progress does not advance the deadlock counter.

Consumer ids are assigned at startup by enumeration in configuration order — they are
unique and dense in ``[0, num_consumers)`` by construction and never appear in a
configuration. Consumers that mirror another consumer's identity rather than being
hardware contexts of their own may ``pin_consumer_id``; pinned consumers are skipped by
the enumeration. The ``num_consumers`` global (readable by any module through the
``ModuleBuilder`` fall-through) sizes per-consumer tables such as ``ship`` and ``drrip``
replacement state. The explicit environment derives it by counting consumer models in the
config (models carrying the ``source_consumer`` mixin, via
``interface_registry::model_is_consumer``); each consumer is counted exactly once
regardless of how many sources it holds, and the source and stream totals are published
separately as ``num_sources`` and ``num_streams``. Override it with a root-level
``"num_consumers"`` key (for example when a model mirrors another consumer's identity via
``pin_consumer_id``).

.. _Orch_Origin:

------------------------------------------
Provenance: ``champsim::origin``
------------------------------------------

Every token, request, and packet carries a ``champsim::origin`` (``inc/origin.h``): the
two identities that describe where a unit of work came from.

* The **consumer** — which hardware context injected it (a core, an injector, a port).
  Schemes partitioning a hardware resource key on it: per-"core" replacement tables,
  prefetch attribution, cache stat keys, phase tracking.
* The **stream** — which workload / address space it belongs to. Translation keys on it:
  virtual memory, page-table walks. This is the ASID.

Access goes through methods, under both canonical and domain-familiar names — the same
identity either way:

.. code-block:: cpp

    origin.consumer() == origin.cpu()    // hardware context
    origin.stream()   == origin.asid()   // address space

The methods are the patch point: schemes read the coordinate they mean, and future
behaviors (remapping, validation, virtualization layers) can be added without touching
every access site.

**Stamping rules.** A source stamps tokens with ``{consumer, stream}``. Both ids are
assigned by the framework at startup: consumers enumerate densely in configuration
order, and every source receives its own stream (its own address space) unless sources
share a ``stream`` label, in which case they share one id. In the classic
one-trace-per-core shape the enumeration makes the two coordinates numerically equal,
matching historic ChampSim exactly. Two traces into one core means one consumer and two
address spaces by default; cloudsuite trace records override the stream with their own
asid. Replacement hooks receive the full ``origin``; virtual memory is keyed by
``origin.asid()``. Page-table walkers resolve each walk's root from the requesting
token's stream and tag their PSCLs with it, so one walker serves any number of address
spaces — it is hardware owned by a consumer, not by an address space.

**Identity mixins.** Consumer-ness and source-ness are symmetric mixins, attachable to
any model of any interface: ``source_consumer`` marks a hardware context that consumes
workloads (a core; also e.g. a replay channel that drives phase completion), and
``stream_source`` marks a holder of a stream identity (every ``workload_source``; also
any model that synthesizes its own address-space traffic). Both are enumerated by the
same startup pass, and both support pinning for models that mirror another holder's
identity rather than owning a slot. The environment publishes three globals before
construction — ``num_consumers``, ``num_sources``, and ``num_streams`` (distinct
stream ids after label sharing) — each counted in its own space and each overridable by
a root config key of the same name; the startup enumeration cross-checks the consumer
and stream counts so per-identity tables can never be silently undersized.

------------------------------------------
Phase Controllers
------------------------------------------

A phase controller decides when a phase is complete and when a run is unhealthy. It is
an ordinary module (interface ``phase_controller``, parent: the environment):

.. code-block:: cpp

    struct phase_controller {
      virtual void begin_phase(const std::string& name, bool is_warmup, uint64_t length) = 0;
      virtual status advance(long progress) = 0;   // CONTINUE, COMPLETE, or ABORT
      virtual std::vector<unsigned> newly_completed_sources() const = 0;
      virtual void end_phase() = 0;
      virtual std::vector<phase_info> get_phases() const;  // non-empty = owns run structure
    };

The generic shipped model is ``PHASE_CONTROLLER`` (``INSTRUCTION_PHASE_CONTROLLER``
remains as an alias). It is token-agnostic and owns only generic mechanics:

* **Completion** — a consumer completes when its ``sim_progress()`` delta reaches the
  phase length (in its own token unit), or when its sources hit EOF.
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

``eof_policy`` selects what happens when a consumer's sources are exhausted:
``"complete_all"`` (default; the classic trace-driven behavior — the first exhausted
source ends the phase for everyone) or ``"complete_source"`` (only the exhausted source
completes; for heterogeneous runs). Any value other than the exact string
``"complete_source"`` is treated as ``"complete_all"``.

Instead of ``warmup_length``/``simulation_length``, a controller may declare an
arbitrary phase list::

    "phases": [
        {"name": "Warmup",      "is_warmup": true,  "length": 10000000},
        {"name": "FastForward", "is_warmup": false, "roi": false, "length": 50000000},
        {"name": "Measured",    "is_warmup": false, "length": 100000000}
    ]

**Multiple controllers.** A configuration may declare any number of phase controllers.
Each may name the consumer ids it governs (``"sources": [0, 1]``; default: all), so
different completion and health policies can apply to different parts of a heterogeneous
system. Composition rules: the phase aborts if *any* controller aborts, completes when
*all* controllers report complete, and the phase list is taken from the first controller
with a non-empty ``get_phases()`` (conflicting non-empty lists are a config error).

When a configuration declares no controller, the orchestrator creates a default
``PHASE_CONTROLLER`` and the classic ``-w``/``-i`` CLI options define the phases — this
is the legacy environment's path.

------------------------------------------
Listeners
------------------------------------------

Listeners observe run-wide events for reporting. They are modules (interface
``listener``):

.. code-block:: cpp

    struct listener {
      virtual void begin_phase(bool is_warmup);
      virtual void progress(const source_consumer& consumer,
                            uint64_t total_progress, uint64_t total_cycles);
    };

Consumers emit progress through ``champsim::modules::emit_progress`` (cores emit on
retirement). The shipped ``HEARTBEAT`` model prints a periodic line per consumer; the
*consumer* formats the line via ``source_consumer::progress_message`` since only it knows
its token unit — cores produce the classic
``Heartbeat CPU N instructions: ... cumulative IPC: ...`` string, and other consumer
types report in their own vocabulary.

Selection:

* Declare listener children in an explicit config
  (``{"name": "hb", "module": "listener", "model": "HEARTBEAT", "interval": 10000000}``).
* Request extra models on the command line: ``--listeners MODEL_NAME``.
* When a config declares none, a default ``HEARTBEAT`` is created with its interval taken
  from the root-level ``"heartbeat_frequency"`` key; ``--hide-heartbeat`` suppresses it.

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
walker). The root config key ``"cycle_skip": false`` disables skipping globally — the A/B
switch used to verify behavior identity. Shipped implementations are verified
byte-identical with skipping on and off, at 10–25% lower simulation wall-clock time.

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
* ``"source_consumer"`` — every instance that inherits ``source_consumer``.

Submodule-created instances participate: when a parent constructs its children (a core
its sources, a cache its prefetchers), each instance self-enrolls with the environment,
appended after the top-level modules. An operable workload source nested under a consumer
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
``num_consumers``
    Override the derived consumer count used to size per-consumer tables.
``block_size``, ``page_size``
    System-wide geometry, published to all modules via the builder globals.

Any other non-reserved top-level scalar is likewise published as a global; see
:doc:`Explicit-configuration-format` for globals and lexical scoping.
