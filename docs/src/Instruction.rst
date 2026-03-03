.. _Instruction:

=====================================
Instruction Representation
=====================================

ChampSim represents instructions at two levels: trace instructions (as read from trace files) and microarchitectural instructions (as executed by the core).

----------------------------------
Trace Instructions
----------------------------------

The ``input_instr`` struct represents instructions as they appear in trace files.

.. doxygenstruct:: input_instr
   :members:

Trace Format
~~~~~~~~~~~~

ChampSim trace files contain:

* Instruction pointer (IP)
* Branch/jump information
* Source registers
* Destination registers
* Memory addresses accessed
* Load/store operations

The trace format is designed to capture memory-level behavior without exposing proprietary instruction encodings.

----------------------------------
Microarchitectural Instructions
----------------------------------

The ``ooo_model_instr`` struct represents instructions within the out-of-order core.

.. doxygenstruct:: ooo_model_instr
   :members:

Key Fields
~~~~~~~~~~

* **ip**: Instruction pointer
* **instr_id**: Unique instruction identifier for program order tracking
* **rob_index**: Position in the reorder buffer
* **source_registers**: Physical source register numbers
* **destination_registers**: Physical destination register numbers
* **source_memory**: Load addresses
* **destination_memory**: Store addresses
* **is_branch**: Branch instruction flag
* **branch_taken**: Branch direction
* **branch_target**: Branch target address

----------------------------------
Branch Types
----------------------------------

ChampSim distinguishes between different branch types:

.. code-block:: cpp

   enum branch_type {
       BRANCH_DIRECT_JUMP,      // Direct unconditional jump
       BRANCH_INDIRECT,          // Indirect unconditional jump
       BRANCH_CONDITIONAL,       // Conditional branch
       BRANCH_DIRECT_CALL,       // Direct function call
       BRANCH_INDIRECT_CALL,     // Indirect function call
       BRANCH_RETURN,            // Function return
       BRANCH_OTHER              // Other/unknown branch type
   };

Branch Prediction
~~~~~~~~~~~~~~~~~

Each branch type may be handled differently by the branch predictor:

* **Direct jumps**: Target known statically
* **Indirect branches**: Target from register, requires BTB
* **Conditional branches**: Direction prediction needed
* **Calls**: Recorded in return address stack
* **Returns**: Predicted using return stack

----------------------------------
Program Order Tracking
----------------------------------

ChampSim uses ``champsim::program_ordered<>`` to track instruction dependencies:

* Instructions are ordered by ``instr_id``
* Dependencies enforce correct execution order
* Load/store ordering is preserved
* Branch mispredictions trigger pipeline flushes

----------------------------------
Register Dependencies
----------------------------------

Instructions track both architectural and physical registers:

* **Architectural registers**: Programmer-visible registers
* **Physical registers**: Renamed registers in out-of-order core
* **Register renaming**: Maps architectural to physical registers
* **Register allocation**: Manages free physical register pool

Register Renaming Process
~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Fetch instruction with architectural registers
2. Allocate physical registers for destinations
3. Map source architectural registers to physical registers
4. Track dependencies via physical register numbers
5. Free physical registers on instruction commit

----------------------------------
Memory Operations
----------------------------------

Instructions may have memory operations:

* **Loads**: Read from memory addresses
* **Stores**: Write to memory addresses
* **RFO (Read-For-Ownership)**: Atomic read-modify-write
* **Address translation**: Virtual to physical address

Memory Dependency Tracking
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Load/Store Queue (LSQ) tracks memory dependencies:

* Load-after-store dependencies
* Store-after-store ordering
* Memory fence instructions
* Speculative load execution
