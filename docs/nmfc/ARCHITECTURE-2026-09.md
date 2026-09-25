# The near-memory function-core architecture — September 2026

This document describes one computer: a conventional processor with small engines placed
beside its memory, able to run short pieces of the program's own code where the data
already is. It states what the machine is, how one engine works inside, which parts of it
a cycle-accurate simulator models today and which are designed but not yet modelled, what
the three workloads measure on it, and what the next piece of work is.

It is written for a reader who has seen none of this before. Every term is defined where
it is first used, every table has a sentence before it and after it, and every performance
number says where it came from. Nothing here is asserted that was not either measured or
explicitly marked as a design not yet built.

---

## 0. Terms

**Host.** An ordinary out-of-order 64-bit RISC-V processor running the program, at 3.0 GHz
in every measurement below. It does everything a processor normally does; the engines are
not a replacement for it.

**Tile.** A unit sitting beside one memory channel. It contains a **function core**, that
core's private instruction and data caches, a slice of the shared last-level cache, a
memory controller and a memory channel. The machine measured here has four tiles.

**Function core**, also **engine** or **tile core**. A small in-order processor that runs
short functions close to memory. It holds many **contexts** and runs them concurrently.

**Context.** One invocation's entire state: 512 bits of register file, a program counter,
and the two small holding registers described in §2.1. There is no stack. A context is
created when work arrives at a tile and destroyed when that work ends.

**Invocation.** One call of a function on an engine. The host starts one with a `FORK`
instruction and collects its result with a `JOIN`.

**Tracking unit.** The structure on the host that holds one entry per invocation from its
`FORK` until its `JOIN`. An entry is *not* a running context: it is also held while an
invocation has finished and is waiting for the host to collect it.

**Line.** 64 bytes, the unit a cache moves. **Set**: the group of places in a cache where
an address may sit. **Way**: one of those places. **Bank**: an independently addressed
piece of a cache or a memory array that performs one access per cycle.

**Pipe.** One execution datapath inside an engine. An engine has several, and in one cycle
each may start an instruction belonging to a *different* context.

**Migration.** When an invocation running on one tile reaches for memory a different tile
owns, the invocation moves to that tile and continues there. It is the normal mechanism,
not an exception.

**Work unit.** The program's own count of what it has finished — a vertex settled, a sum
performed, a table operation completed. It is the axis two builds of one program are
compared on, because an instruction count is not the same quantity in two different builds.

**Arm.** One build of one workload. Every comparison below is a **baseline** arm, in which
the whole computation runs on the host, against an **offloaded** arm built from the same
source in which one named step runs as a function on the tiles.

---

## 1. The machine in one page

### 1.1 The shape

The host sits above a coherence fabric. Below the fabric are four tiles. A tile owns a
fraction of the **physical** address space outright: one slice of the last-level cache, one
memory controller, one memory channel, and the function core that sits in front of them.
Which tile owns an address is a property of the physical frame, never of the virtual
address, so a request must be translated before anybody can know where it goes.

```
                 ┌──────────────────────────────┐
                 │   HOST  (out-of-order core)  │   FORK / JOIN
                 │   private L1, L2             │   tracking unit
                 └──────────────┬───────────────┘
                                │
                 ╔══════════════╧═══════════════╗
                 ║  COHERENCE FABRIC (directory) ║
                 ╚══╦═══════╦═══════╦═══════╦═══╝
                    │       │       │       │
               ┌────┴──┐┌───┴───┐┌──┴────┐┌─┴─────┐
               │ TILE 0││ TILE 1││ TILE 2││ TILE 3│
               │ core  ││ core  ││ core  ││ core  │   contexts, pipes
               │ I$ D$ ││ I$ D$ ││ I$ D$ ││ I$ D$ │   banked
               │ LLC   ││ LLC   ││ LLC   ││ LLC   │   slice, banked
               │ MC    ││ MC    ││ MC    ││ MC    │   = DRAM banks
               │ DRAM  ││ DRAM  ││ DRAM  ││ DRAM  │   one channel each
               └───────┘└───────┘└───────┘└───────┘
```

The configuration every measurement in §4 was taken at is one line of that picture made
concrete, and it is configuration rather than design: four tiles, 128 contexts per engine,
a 4 MiB last-level slice per tile, one 32-bit DDR5-4800 subchannel per tile (19.2 GB/s a
tile, 76.8 GB/s over the four), and invocations placed on the tile that owns the first
address their context names. No count in this document is locked; all of them are swept.

### 1.2 Page types, and the one rule about duplicated pages

Three placements exist for tile-owned memory, and they decide which tile holds what.

| page type | placement | what it is for |
|---|---|---|
| striped | spread across the tiles, one grain at a time | ordinary data, when no locality is wanted |
| grain | every frame on one tile | data that should sit beside one engine |
| duplicate | one identical copy per tile | what every engine needs locally: code, the page table, data that is read-only for the whole run |

A duplicated page has one virtual address and **four real physical addresses**, one per
tile. That is the whole of the rule that follows from it:

> **A function core must never write a duplicate page.** A host core may, freely: the
> coherence fabric either gives the host ownership of the block or broadcasts the write,
> and the copies stay identical. A write from an engine would need a cross-tile coherence
> mechanism this machine does not have, so an engine's store or atomic that resolves to a
> duplicated page is refused as a fault and counted. The counter is gated to zero in both
> test suites, and a directed test shows the fault, shows that no copy diverges, and shows
> that a host store still reaches every copy.

One consequence is worth stating because it shapes programs: a table the computation
rewrites cannot be duplicated. If a program wants a replicated table it also writes, the
answer is to put the bits beside the data they describe so that the owner writes them
locally, and to let probes from other tiles migrate. Duplication is for things that do not
change while the computation runs.

There is one legal write to a duplicated page from inside a tile, and it is privileged, not
a kernel store: after a page is remapped, the tile rewrites the affected leaves of its own
copy of the page table. §2.5 gives that request its own class and its own reserved
capacity, precisely so that it is never confused with the refused kernel store.

### 1.3 The 512-bit context and its lanes

A context is **512 bits, bit-packed**, laid out by the program. It is not "eight
registers": eight 64-bit values is one possible view of those bits, never the architecture.

An ordinary RISC-V instruction's five-bit register field names a **bit range** of the 512,
not an entry in a file:

| register name | what it names | width |
|---|---|---|
| `r0` | reads zero | — |
| `r1` | the whole 512 bits, for save and restore | 512 |
| `r2`–`r7` | the two 256-bit halves and the four 128-bit quarters | 256 / 128 |
| `r8`–`r15` (`d0`–`d7`) | the eight 64-bit lanes | 64 |
| `r16`–`r31` (`w0`–`w15`) | the sixteen 32-bit lanes | 32 |

The two lane tilings are **overlapping views of the same bits**: `w2k` is the low half and
`w2k+1` the high half of the same 64 bits that `dk` names. There is no renaming, because
the pipes are in-order and a name is a bit range, so there is no map to rename through.

No operation in the present instruction set is wider than 64 bits, so `r1`–`r7` name bits
nothing can compute on, and the decoder traps any instruction that uses one as an operand.
The map is verified lane by lane in both directions and in both simulators — the
cycle-accurate model and the functional one that produces its starting images agree on all
sixteen checks, and each of the seven reserved names kills a build with the same diagnostic
in both. Where the whole file moves, no register is named at all: `FORK` carries it in,
`JOIN` carries it back, the continue instruction hands it to a successor, and a migration
carries it between tiles as the 64 bytes of register file and the program counter, and nothing else: the instruction slot and the data slot stay behind, because an instruction or a value fetched on one tile belongs to that tile's view of memory, and the context re-fetches its instruction and re-issues its memory operation on the tile it arrives at.

### 1.4 The instruction set

The extension adds **fifteen instructions** to an otherwise ordinary 64-bit RISC-V machine,
and one further word that marks the entry point of a function. Eight of the fifteen execute
on the host and start, probe, collect or cancel an invocation; four execute on a function
core and end, extend or pause one (`WAIT` sleeps a context until a line it read changes); two move 64 bits between a host general register and a host
context register; one, `RESUME`, is privileged and belongs to the fault path. Twelve of them
form the user-level base set; `KILL` is user-level as well but was added after that set was
closed, and `RESUME` is not user-level at all.

Every host instruction is a **try**. It reports in a general register whether it succeeded,
and none of them blocks. A program that wants to wait writes the loop itself, which is why
each try has a probe beside it that asks the same question without moving anything. On a
function core the same loop can sleep instead of polling: `WAIT` sleeps the context on the
line its last read named, so a loop whose answer has not changed issues nothing until the line
is written. It is still a loop the program writes, and the instruction still completes.

#### 1.4.1 Where the extension sits in the encoding space

The whole extension occupies **one** opcode: RISC-V `custom-0`, `0b0001011` = 0x0b, in bits
6:0 of every instruction word. The table below says what the four custom opcodes are used
for here.

| opcode | bits 6:0 | used for |
|---|---|---|
| `custom-0` | `0b0001011` (0x0b) | the whole of this extension |
| `custom-1` | `0b0101011` (0x2b) | free, held for a second reservation |
| `custom-2` | `0b1011011` (0x5b) | avoided: claimed by the 128-bit base instruction set |
| `custom-3` | `0b1111011` (0x7b) | avoided, for the same reason |

Only one opcode is taken because the instruction is identified by `funct7`, not by the
opcode, and `funct7` has room to spare.

Inside that opcode the two function fields are split unusually, and the split is what lets
the same instructions run on an out-of-order host through a coprocessor interface.
**`funct3` is not a selector**: it carries three flags saying which register fields the
instruction actually uses, so that a host which hands a coprocessor operand *values* knows
which values to hand over. **`funct7` identifies the instruction**: its top three bits are a
group and its low four bits a variant within that group.

| field | bits | meaning |
|---|---|---|
| `funct3` bit 0 | 12 | the instruction writes `rd` |
| `funct3` bit 1 | 13 | the instruction reads `rs1` |
| `funct3` bit 2 | 14 | the instruction reads `rs2` |
| `funct7` bits 6:4 | 31:29 | the group |
| `funct7` bits 3:0 | 28:25 | the variant within the group |

Six of the eight `funct3` combinations occur — `000`, `001`, `010`, `011`, `110`, `111` —
because no instruction in the set reads `rs2` without also reading `rs1`.

Three bits of group are exactly enough for the six groups the set needs, and four bits of
variant are exactly enough for the widest one, a context-lane move that carries a direction
and a lane number. That gives 128 `funct7` values, of which **31 are taken and 97 are free**.

| group | `funct7` range | instructions | variants used | variants free |
|---|---|---|---|---|
| `000` fork | 0x00–0x0f | `FORK.R` `FORK.M` `FORKF.R` `FORKF.M` | 0–3 | 12 |
| `001` probe | 0x10–0x1f | `FORKQ` `JOINQ` | 0–1 | 14 |
| `010` join | 0x20–0x2f | `JOIN` | 0 | 15 |
| `011` end | 0x30–0x3f | `END`, `END.R` | 0–1 | 14 |
| `100` continue | 0x40–0x4f | `CONT` `CONT.M` `WAIT` | 0–2 | 13 |
| `101` context lane | 0x50–0x5f | `CXW` and `CXR`, eight lanes each | 0–15 | none |
| `110` control | 0x60–0x6f | `KILL` `RESUME` | 0–1 | 14 |
| `111` marker | 0x70–0x7f | the function-entry marker | 0 | 15 |

The context-lane group is the only one that is full, because its variant field is not a flag
word: it is a lane number and a direction, and all sixteen combinations name something.

#### 1.4.2 The instruction format

Every instruction of the extension, without exception, is an ordinary RISC-V **R-type** word:
four fixed fields and three register fields in their usual places, so a host's fetch, rename
and register read need no special case for it.

| bits 31:25 | 24:20 | 19:15 | 14:12 | 11:7 | 6:0 |
|---|---|---|---|---|---|
| `funct7` | `rs2` | `rs1` | `funct3` | `rd` | opcode |
| group and variant | second source | first source | operand flags | destination | `0b0001011` |

Unused register fields are encoded as `x0`, and the corresponding `funct3` flag is clear, so
an unused field is both harmless to read and marked as unread.

The one field with internal structure is `funct7`, drawn here over the instruction word's own
bit numbers.

| bits 31:29 | 28:25 |
|---|---|
| group | variant |

In the context-lane group the variant divides again: three bits of lane number and one bit of
direction. Nothing else in the extension subdivides its variant.

| bits 31:29 | 28:26 | 25 |
|---|---|---|
| group `101` | lane, 0–7 | 0 = write the lane, 1 = read it |

That layout is why a lane move needs no extra register: the lane is a constant at every call
site, so it costs a field instead of an instruction.

#### 1.4.3 The instructions

The fifteen entries follow. Each gives the assembly syntax, the encoding with every field's
value, which side of the machine executes it, what its operands carry, what it does, what it
can raise, and anything that would otherwise be a surprise. In the encodings below, a field
written as a name is supplied by the programmer and a field written in binary or hex is fixed
by the instruction.

There is no assembler mnemonic for any of this in the toolchain as it stands. Programs emit
the words through the assembler's generic `.insn r` directive, which takes the opcode, the
two function fields and the three register fields directly; the header that defines the
encoding and the C wrappers over it are what call sites actually use. The syntax given in
each entry is the intended mnemonic form, and it follows the usual destination-first
convention, which for two instructions is *not* the field order — `JOIN` and `CXW` are called
out where that happens.

---

**`FORK.R rH, rPC, cCTX`** — start an invocation from a context register.

*Encoding.* `funct7` = 0x00 (group `000`, variant `0000`) · `rs2` = `cCTX` · `rs1` = `rPC` ·
`funct3` = `111` (writes `rd`, reads `rs1`, reads `rs2`) · `rd` = `rH` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operands.* `rs1` holds the callee's entry address. `rs2` holds a **number**, 0–7, naming one
of the host's eight 512-bit context registers — not a register field, a value, because the
coprocessor interface hands over operand values and only the index of `rd`. `rd` receives the
handle.

*Operation.*

```
if the word at rs1 is not the function-entry marker:   rd <- 0; done
if the tracking unit has no free entry:                rd <- 0; done
if the fabric has no free context credit:              rd <- 0; done
h <- tracking unit allocates an entry, return mode = join
send { h, entry = rs1, the 512 bits of context register rs2 } to a tile
rd <- h
```

The 512 bits are copied out of the context register at this instruction; nothing later can
change what the invocation received. A tile that accepts the message allocates a context slot
for it, and the fabric credit spent here is what that slot costs.

*Exceptions.* None. All three failures above are **refusals**, answered with a zero handle,
because a handle of zero is not a valid handle and a program can test for it. A context
number outside 0–7 names nothing the machine has and is reported as a program error rather
than executed.

*Notes.* Zero is reserved as the "no handle" answer, so a live handle is never zero. The
refusal for a target that is not a function is what stops a wild pointer being executed as a
kernel: the first word of every function is the marker of §1.4.3's last entry, and dispatch
reads it before anything is sent.

---

**`FORK.M rH, rPC, rADDR`** — start an invocation from a context in memory.

*Encoding.* `funct7` = 0x01 (group `000`, variant `0001`) · `rs2` = `rADDR` · `rs1` = `rPC` ·
`funct3` = `111` · `rd` = `rH` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operands.* `rs1` the entry address; `rs2` the address of a 64-byte context; `rd` the handle.

*Operation.* Exactly `FORK.R`, with one difference: the message carries the **address**, and
**the tile reads the 64 bytes**, not the host. Only an address crosses the fabric, and the
load happens where the context probably already lives.

*Exceptions.* None on the host. The dereference happens on the function core, so a bad address
is that core's fault: a recoverable one parks the context and is handled as §1.4.3's `RESUME`
entry describes, a fatal one tears the invocation down and the handle answers with the error
flag.

*Notes.* Because another core reads those bytes, the stores that built them must be visible
first; the wrapper emits a release fence ahead of the instruction, and no correct use of the
form omits it. `FORK.R` needs none — a context register is not memory.

---

**`FORKF.R rH, rPC, cCTX`** — start an invocation whose result nobody will collect.

*Encoding.* `funct7` = 0x02 (group `000`, variant `0010`) · `rs2` = `cCTX` · `rs1` = `rPC` ·
`funct3` = `111` · `rd` = `rH` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operation.* `FORK.R`, with the entry's return mode set to *acknowledge* instead of *join*.
The entry is still allocated and still occupies the tracking unit; it closes when the
invocation's acknowledgement arrives, rather than at a `JOIN`.

*Exceptions.* None; the same three refusals.

*Notes.* It still returns a handle. There is no use for it today, but it keeps every
invocation addressable and the four fork encodings uniform, and adding it later would have
broken every fork. `JOIN` and `JOINQ` on such a handle cannot succeed, because there is
nothing to collect.

---

**`FORKF.M rH, rPC, rADDR`** — fire-and-forget, context read from memory by the tile.

*Encoding.* `funct7` = 0x03 (group `000`, variant `0011`) · `rs2` = `rADDR` · `rs1` = `rPC` ·
`funct3` = `111` · `rd` = `rH` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operation.* The two variant bits are independent: bit 0 says the context comes from memory,
bit 1 says the result is not to be collected. This is both.

*Exceptions.* As `FORK.M`. The same release fence applies.

---

**`FORKQ rN`** — how much room the tracking unit has.

*Encoding.* `funct7` = 0x10 (group `001`, variant `0000`) · `rs2` = `x0` · `rs1` = `x0` ·
`funct3` = `001` (writes `rd` only) · `rd` = `rN` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operation.* `rd <- the number of free tracking-unit entries`. Nothing else is read or
changed.

*Exceptions.* None. It cannot fail.

*Notes.* A **count**, not a flag, so a program can size a batch instead of probing once per
fork. It is a sizing hint and never a contract: it may be stale by the next instruction, and
a `FORK` answering zero is the authoritative answer. It earns its own encoding because of
fire-and-forget: an entry freed by a remote acknowledgement is released asynchronously, so
occupancy cannot be computed from the instruction stream. This is one of the two instructions
with which a program discovers the machine's real capacity instead of being compiled against
a constant.

---

**`JOIN rOK, cDST, rH`** — try to collect an invocation's 512 bits.

*Encoding.* `funct7` = 0x20 (group `010`, variant `0000`) · `rs2` = `cDST` · `rs1` = `rH` ·
`funct3` = `111` · `rd` = `rOK` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operands.* **The mnemonic order is not the field order.** `rs1` is the handle, which is the
*third* operand written; `rs2` is the destination context-register number, which is the
*second*. The mnemonic is destination-first, as assembly conventions are, and the field order
is fixed by the coprocessor interface; they do not coincide for an instruction that writes a
context register.

*Operation.*

```
e <- the tracking-unit entry named by rs1
if there is none:                    rOK <- 0; done
if e was killed:                     ctx[rs2] <- 64 zero bytes
                                     rOK <- ok | error; done
if e is fire-and-forget:             rOK <- 0; done
if e has not returned:               rOK <- 0; done          # destination untouched
ctx[rs2] <- e's 512 bits             # all zero if the invocation ended in error
free e
rOK <- ok, or ok | error
```

The answer is a bit field: bit 0 is *collected*, bit 1 is *ended in error*. Zero means *not
back yet*; 1 means the 512 bits are the function's; 3 means the invocation was killed or died
and the 512 bits delivered are zero.

*Exceptions.* **None — a `JOIN` never faults on itself.** That is deliberate: a joining
program always gets a well-formed answer it can test, so the error path is an ordinary branch
on a flag rather than a second trap. A handle naming no live entry, and a handle naming a
fire-and-forget invocation, answer zero; the simulators can be configured to stop on either,
since both are program errors rather than machine states, but the architecture's answer is
zero. A destination number outside 0–7 is a program error, and it is examined only once the
entry is one this `JOIN` would collect.

*Notes.* On a *not back yet* answer the destination context register is **not written**: an
invocation that has not returned must not leave a plausible-looking value behind. A machine
that renames must therefore treat the destination as a source as well.

---

**`JOINQ rOK, rH`** — has it returned?

*Encoding.* `funct7` = 0x11 (group `001`, variant `0001`) · `rs2` = `x0` · `rs1` = `rH` ·
`funct3` = `011` (writes `rd`, reads `rs1`) · `rd` = `rOK` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operation.*

```
e <- the entry named by rs1
if there is none:        rOK <- 0
else if e was killed:    rOK <- 1        # a killed entry reports as returned
else if fire-and-forget: rOK <- 0        # its result can never be read
else                     rOK <- (e has returned) ? 1 : 0
```

*Exceptions.* None.

*Notes.* The probe beside `JOIN`, and the reason it earns an encoding is that **it moves no
64 bytes**. Polling with `JOIN` would move a register file on every failed attempt. Together
with `FORKQ` it is how a program asks the machine about its own state at run time.

---

**`END` / `END.R`** — finish this invocation. The assembler spellings are `ENDC` and `RETC`.

*Encoding.* `funct7` = 0x30 for `END`, 0x31 for `END.R` (group `011`, variant `0000` or
`0001`) · `rs2` = `x0` · `rs1` = `x0` · `funct3` = `000` (no register used) · `rd` = `x0` ·
opcode = `0b0001011`.

*Executed by* a function core, user-level.

*Operands.* **None, in either form.** `END` returns the register file of the context that
executes it, and a context has exactly one; there is nothing to name. Context registers are a
host-side structure, and a function core has none, so an operand naming one could not be
resolved here at all.

*Operation.*

```
r <- funct7 bit 0                       # the return bit
if this context has stores outstanding: # it cannot retire before they land
    remember r; stop executing; the last store response finishes it
else:
    send { handle, r ? the 512 bits : nothing } to the host
    free the context slot and return the fabric credit
```

*Exceptions.* On a **host** core this word is illegal: `END` belongs to a function core, and a
host has no invocation to end. Both host models reject it.

*Notes.* Bit 0 of the variant — **the return bit** — is the whole of the difference between
the two forms, which is why they are one instruction and the base set is twelve rather than
thirteen. The bit and the fork form are chosen independently and may disagree, and every
combination still answers: a fire-and-forget invocation that sets the bit has its 64 bytes
dropped, and a join-expected invocation that clears it still produces an acknowledgement and
a **zeroed** register file, so that no entry can become uncollectable.

---

**`CONT rPC`** — hand this context to a successor.

*Encoding.* `funct7` = 0x40 (group `100`, variant `0000`) · `rs2` = `x0` · `rs1` = `rPC` ·
`funct3` = `010` (reads `rs1`) · `rd` = `x0` · opcode = `0b0001011`.

*Executed by* a function core, user-level.

*Operands.* `rs1` holds the successor's entry address. It must be a **64-bit** register name:
an address is 64 bits whatever the data width is.

*Operation.*

```
pc <- rs1
```

and nothing else moves. The context slot, the 512 bits and the tracking-unit entry on the
host all stay exactly as they are.

*Exceptions.* A 32-bit or reserved name in `rs1` is a decode error. On a host core the
instruction is illegal.

*Notes.* **It cannot be refused**, and that is the point: it inherits the entry rather than
allocating one, so it consumes no new resource. The handle the host holds stays valid across
an arbitrary chain of successors, and whatever the last link returns is what the `JOIN`
receives. It is also how a function too large for one 512-bit register file is split into a
chain whose links are individually admissible, with no link able to be denied.

---

**`CONT.M rPC, rADDR`** — a successor with a fresh context.

*Encoding.* `funct7` = 0x41 (group `100`, variant `0001`) · `rs2` = `rADDR` · `rs1` = `rPC` ·
`funct3` = `110` (reads `rs1` and `rs2`) · `rd` = `x0` · opcode = `0b0001011`.

*Executed by* a function core, user-level.

*Operands.* `rs1` the successor's entry address, `rs2` the address of a 64-byte context. Both
must be 64-bit names.

*Operation.*

```
pc  <- rs1
the 512 bits <- the 64 bytes at rs2      # fetched by this tile, replacing the file wholesale
```

The context keeps its slot and waits for the fetch; the successor begins with the new
register file and no trace of the old one.

*Exceptions.* As `CONT`, plus whatever the fetch of the new context raises: a recoverable
fault parks the context, a fatal one ends the invocation with the error flag.

*Notes.* The 512 bits fetched **are** the context — there is nothing to merge them with,
which is why the replacement is wholesale rather than a partial update.

---

**`WAIT rA`** — sleep until the line named by `rA` is written or lost.

*Encoding.* `funct7` = 0x42 (group `100`, variant `0010`) · `rs2` = `x0` · `rs1` = `rA` ·
`funct3` = `010` (reads `rs1` only) · `rd` = `x0` · opcode = `0b0001011`. With `rA` = `x11` the
word is `0x8405a00b`. Group `100` is refused by every host decoder, so `WAIT` is illegal on a
host without a new rule. Variant `0011` stays reserved (kept for a possible form that delivers
the written word).

*Executed by* a function core, user-level.

*Operands.* `rs1` holds a virtual address and must be a 64-bit name. Only the line it falls in
matters. There is no expected value and no destination.

*Operation.*

```
translate rs1 as a load's address is        # a fault drops it and the context re-attempts;
                                            # another tile's line drops it and the context migrates
if the load slot's arm is valid and names the same physical line:
    the context sleeps (WAITING, no issue slot) until an invalidation of that line, or KILL
else:
    complete at once                        # a permitted wake-up
pc <- pc + 4
```

*What arms the slot.* Every load and read-modify-write atomic writes the **arm** — its
physical line and a valid bit — when its translation completes. A store, a load-reserved and a
store-conditional clear it at their translation; so do a dropped translation and departure
(retirement, migration, kill: the arm never travels). Any invalidation of the line clears it,
whether the arming operation is still in flight or has returned, which is what closes the lost
wake-up without an atomic step: a write performed after the arming read has cleared the arm
before the `WAIT` looks at it.

*Semantics.* `WAIT` issues no request: no memory-queue entry, no serialisation point, no
delivery-window slot, no credit. Spurious wake-ups are part of the definition (an eviction of
the line, a write to another word of it, a migration or a restore that empties the arm), so a
program re-reads its word after every `WAIT`:

```
retry:  ld    d1, 0(d0)        # the read that decides "not yet" arms the slot
        bne   d1, d2, work
        wait  d0
        j     retry
```

The only guarantee is that a context is never left asleep after a write to the line performed
after the load that armed it.

*Rules the loop follows.* The arming load or atomic is the last memory operation before the
`WAIT` (a store in between clears the arm and costs one turn of the loop, never a hang); the
condition lives in one line; `rA` names the arming line (the compare is on physical lines);
a `WAIT` after an open load-reserved completes at once, so no context sleeps holding a
serialisation point; and a waiter must never be what its writer needs — resident waiters per
tile stay below the tile's contexts, as they must for a polling loop.

*Exceptions.* None of its own. A translation fault parks the context and `RESUME` re-attempts
the `WAIT`; a line another tile owns migrates the context (register file and program counter)
and the `WAIT` re-issues there, where it finds the arm empty and completes at once.

*Notes.* It holds only its own context while it sleeps, exactly as a polling loop does, so it
adds no hold-and-wait edge; it is not a blocking instruction in the sense the canon rejects.
The functional model gives it the same meaning without time: every load and read-modify-write
atomic records its line as the invocation's arm, a `WAIT` whose line matches parks the
invocation at the `WAIT`, and only a store or atomic performed to that line wakes it. The
cycle model's counters and zero gates are in §2.1; its measured cost on a workload is in §4.7.

---

**`CXW cD, lane, rS`** — write one 64-bit lane of a context register.

*Encoding.* `funct7` = 0x50 + 2·lane (group `101`, variant = lane in bits 3:1, direction bit
0 clear); 0x50 for lane 0 through 0x5e for lane 7 · `rs2` = `cD` · `rs1` = `rS` ·
`funct3` = `110` (reads `rs1` and `rs2`) · `rd` = `x0` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operands.* **The mnemonic order is not the field order, and it is the opposite way round
from `CXR`.** `rs1` is the value, written *third*; `rs2` is the context-register number,
written *first*; the lane is not a register at all but the `funct7` field above.

*Operation.*

```
ctx[rs2] lane <- rs1                    # 64 bits, at bits [64*lane, 64*lane+64)
```

Nothing is answered: the destination field is `x0`.

*Exceptions.* A context number outside 0–7 is a program error. The lane cannot be out of
range: it is three bits and the register has eight lanes. On a function core the instruction
is illegal — context registers exist only on the host, which is why functions do not nest.

*Notes.* Lane *k* is exactly the 64-bit name `d`*k* that the callee sees, and the two 32-bit
names `w`2*k* and `w`2*k*+1 are its halves, so every named value in the callee's file is
reachable in exactly one move. There is deliberately **no** bit-field insert: once 64 bits can
be moved, any packing inside them is reached with shifts and masks the base instruction set
already has, and a field straddling a lane boundary is two moves and the same arithmetic.
Staging two 32-bit arguments means building the lane in a general register first and writing
it once — never patching one half of a live lane, whose other half is another architectural
value and not spare room.

---

**`CXR rD, cS, lane`** — read one 64-bit lane of a context register.

*Encoding.* `funct7` = 0x51 + 2·lane (group `101`, variant = lane in bits 3:1, direction bit
0 set); 0x51 for lane 0 through 0x5f for lane 7 · `rs2` = `x0` · `rs1` = `cS` ·
`funct3` = `011` (writes `rd`, reads `rs1`) · `rd` = `rD` · opcode = `0b0001011`.

*Executed by* the host, user-level.

*Operation.*

```
rD <- ctx[rs1] lane
```

*Exceptions.* A context number outside 0–7 is a program error. Illegal on a function core.

*Notes.* Here the mnemonic order *does* match the field order. `CXW` and `CXR` are
deliberately asymmetric in mnemonic order and symmetric in encoding: the direction is one bit
and everything else about them is the same.

---

**`KILL rH`** — end one of my own outstanding invocations.

*Encoding.* `funct7` = 0x60 (group `110`, variant `0000`) · `rs2` = `x0` · `rs1` = `rH` ·
`funct3` = `010` (reads `rs1`) · `rd` = `x0` · opcode = `0b0001011`.

*Executed by* the host, and it is **unprivileged**: a handle is issued by the program's own
tracking unit and names nothing outside it, so this is cancelling one's own work, not signalling
somebody else's.

*Operation.*

```
e <- the entry named by rs1
if there is none, or it is already killed:   do nothing
else:
    e's 512 bits <- zero
    e's error flag <- set
    e is marked returned and stays READABLE
    broadcast the kill: the tile holding that context ends it, the others drop the message
    e is freed when that tile's acknowledgement arrives
```

*Exceptions.* **None, and it cannot fail.** A stale, already-joined or already-killed handle
is a **no-op** — not a fault, not a trap, not an error return — which is what makes it safe
to issue from a teardown that cannot know which of its invocations have already ended. A
recycled handle is not a special case: it names the new invocation.

*Notes.* The entry does not disappear at the instruction. It closes, and until the tile
acknowledges it is readable and answers: `JOINQ` reports it as returned, `JOIN` collects the
zeroed file with the error flag. That is what keeps the rule that a join-expected entry never
closes without returning something literally true, and it is also why the same acknowledgement
frees a killed *fire-and-forget* entry, which no `JOIN` could ever reclaim. A context parked
across a recoverable fault is killable too, and killing it releases the slot it was holding.

---

**`RESUME rH`** — restart a context parked by a recoverable fault.

*Encoding.* `funct7` = 0x61 (group `110`, variant `0001`) · `rs2` = `x0` · `rs1` = `rH` ·
`funct3` = `010` (reads `rs1`) · `rd` = `x0` · opcode = `0b0001011`.

*Executed by* the host, and it is **privileged**.

*Operation.* It names a parked context by its handle and tells it to re-issue the instruction
that faulted. It allocates nothing, refuses nothing and cannot fail: the entry already exists,
and the context's slot was held across the fault — the one place a function core holds a slot
for something other than execution. A `RESUME` naming an entry a `KILL` has already closed
finds nothing and does nothing.

*Exceptions.* **Executing it from user code traps**, and that trap is the privilege check.
A recoverable fault is delivered to the kernel through the tracking unit, the kernel's handler
runs, and the kernel resumes the context — the same shape as returning from any other trap,
where the party that took delivery is the party that returns from it. If it were user-level,
any program could restart any context whose handle it could name or guess, and user code has
no reason to resume a context it did not know had faulted.

*Notes.* There is no operating system in the configurations measured here, so no kernel code
exists to issue one: both host models decode the instruction and both trap it, and the
kernel's own resume is modelled inside the tracking unit that took delivery of the fault. The
machine's fault path is otherwise ordinary — the fault travels to the host through the
tracking unit, because the tile does not know which host to trap and the entry does.

---

**The function-entry marker** — not one of the fourteen, but part of the encoding.

*Encoding.* `funct7` = 0x70 (group `111`, variant `0000`) · `rs2` = `x0` · `rs1` = `x0` ·
`funct3` = `000` · `rd` = `x0` · opcode = `0b0001011`. The whole word is `0xe000000b`.

*What it is.* The **first instruction word of every function**. On a function core it executes
as a no-op and costs one instruction slot per invocation. On a host core it is an **illegal
instruction** and traps. At `FORK` it is checked: dispatch reads the word at the target
address and refuses the fork when it is absent.

*Why it exists.* A function-core binary is not host-executable, and nothing at run time could
otherwise tell: the same bytes mean different things on the two cores, because a register
number on a function core is a bit range of the 512-bit context and on the host is an ordinary
register. `add x3, x8, x9` assembles, disassembles and executes on a stock 64-bit RISC-V core,
while on a function core `x3` is a reserved name that traps. A host that fell into a function
by a wild pointer or a bad function table would otherwise compute a plausible wrong answer,
quietly. The marker is in the extension's own opcode for exactly that reason: an innocuous
word would let the host run on.

*Reserved space.* Variants 1–15 of the marker group are reserved and are rejected by both
function-core decoders.

#### 1.4.4 Encoding summary

Every field of every instruction, in one table, in encoding order rather than mnemonic order.
The lane *n* runs 0–7.

| instruction | `funct7` | group | variant | `funct3` | `rd` | `rs1` | `rs2` |
|---|---|---|---|---|---|---|---|
| `FORK.R` | 0x00 | `000` | `0000` | `111` | handle | entry address | context number |
| `FORK.M` | 0x01 | `000` | `0001` | `111` | handle | entry address | context address |
| `FORKF.R` | 0x02 | `000` | `0010` | `111` | handle | entry address | context number |
| `FORKF.M` | 0x03 | `000` | `0011` | `111` | handle | entry address | context address |
| `FORKQ` | 0x10 | `001` | `0000` | `001` | free count | `x0` | `x0` |
| `JOINQ` | 0x11 | `001` | `0001` | `011` | answer | handle | `x0` |
| `JOIN` | 0x20 | `010` | `0000` | `111` | answer | handle | context number |
| `END` | 0x30 | `011` | `0000` | `000` | `x0` | `x0` | `x0` |
| `END.R` | 0x31 | `011` | `0001` | `000` | `x0` | `x0` | `x0` |
| `CONT` | 0x40 | `100` | `0000` | `010` | `x0` | successor address | `x0` |
| `CONT.M` | 0x41 | `100` | `0001` | `110` | `x0` | successor address | context address |
| `WAIT` | 0x42 | `100` | `0010` | `010` | `x0` | address | `x0` |
| `CXW` lane *n* | 0x50 + 2*n* | `101` | *n*`0` | `110` | `x0` | value | context number |
| `CXR` lane *n* | 0x51 + 2*n* | `101` | *n*`1` | `011` | value | context number | `x0` |
| `KILL` | 0x60 | `110` | `0000` | `010` | `x0` | handle | `x0` |
| `RESUME` | 0x61 | `110` | `0001` | `010` | `x0` | handle | `x0` |
| marker | 0x70 | `111` | `0000` | `000` | `x0` | `x0` | `x0` |

Reading the table with §1.4.2's format gives the 32-bit word directly, and §1.4.5 lists the
word each row assembles to.

#### 1.4.5 What a function core runs underneath these instructions

A function core is a 64-bit RISC-V machine with integer, multiply/divide and atomic
instructions, and with floating-point *opcodes* that operate on the same register names as
everything else rather than on a second register file. Its decoder implements:

- the integer base: `lui`, `auipc`, `jal`, `jalr`, the branches, the register–immediate and
  register–register operations, and their 32-bit `*w` forms;
- multiply and divide, including the 32-bit forms; the high-half multiplies are absent from
  the cycle-accurate core and present in the functional one, which is a divergence rather than
  a decision, recorded in §1.4.6;
- loads and stores of every width, including the floating-point ones;
- **atomics of both kinds**: the load-reserved / store-conditional pair and the
  read-modify-write operations;
- floating-point arithmetic in both widths, with the operand's *type* coming from the opcode
  and the operand's *bits* from the ordinary register name — so there is no second file, no
  boxing of a 32-bit value inside 64 bits, and a migration carries no more state because a
  function computes in floating point.

It implements **no compressed instructions**, **no fences** and **no system instructions** at
all: no control registers, no environment call, no breakpoint. Anything outside the subset
fails at decode rather than executing at a guessed width.

The register-name rule is the part a reader has to know before reading any function's
assembly, and it is decoded, not conventional. A five-bit register field names a bit range of
the 512-bit context, exactly as §1.3's table sets out — that table writes the names `r0`–`r31`
and assembly writes them `x0`–`x31`, and they are the same five-bit field: `x0` reads zero;
`x1`–`x7` name the whole file and its halves and quarters; `x8`–`x15` are the eight 64-bit
lanes `d0`–`d7`; `x16`–`x31` are the sixteen 32-bit lanes `w0`–`w15`. From that the decoder enforces four rules:

1. **`x1`–`x7` are illegal as any operand and trap.** No operation in the subset is wider than
   64 bits, so those seven names denote nothing the machine can compute on. The consequence is
   worth stating plainly: the conventional return-address and stack-pointer registers are
   `x1` and `x2`, so a prologue, an epilogue and a `ret` are all decode-illegal. There is no
   stack, and a body ends with `END`.
2. **An address or base operand must be a 64-bit name**, `x8`–`x15`. An address is 64 bits
   whatever the data width is, and a 32-bit name cannot hold one.
3. **An operand whose opcode fixes its width must be named by a slice of exactly that width.**
   The bits above a 32-bit name are another architectural value, not spare room.
4. **A jump may not form a link**: `jal` and `jalr` are legal only with `rd` = `x0`, which is
   an unconditional jump and an indirect jump. There is nowhere to save a return address.

Two further rules live in the memory path rather than the decoder, and both are properties of
this machine rather than of RISC-V. A **function core may not write a duplicated page** — a
page held as one copy per tile: such a store resolves to the writing tile's own copy, the
other copies would diverge with nothing able to observe it, and keeping them equal would need
exactly the coherence across copies that duplication exists to avoid. The store is therefore
refused: not sent, memory unchanged, counted, and the invocation continues. The same applies
to an atomic that writes; a load-reserved, which only reads, is left alone. A store by the
**host** to the same page is legal and reaches every copy. And a translation the tile cannot
complete is a **fault**, which parks the context and enters the path `RESUME` returns from.

One extension instruction also lives in the memory path. **`WAIT` is the function core's one
sleeping instruction**, and it is defined by what the loads before it did: every load, integer
or floating point, and every read-modify-write atomic leaves its physical line in the load
slot as the **arm**; a store, a load-reserved and a store-conditional clear it. `WAIT`
translates its operand like a load and sleeps only if the arm still names that line. Nothing
else in the subset reads the arm, and no load is ever answered from it.

#### 1.4.6 How these encodings were checked, and where the decoders disagree

The encoding is decoded independently in four places: the host attachment and the tile of the
cycle-accurate model, and the host and the function core of the functional model that writes
the cycle-accurate model's starting images. All four read their field values from one header, **and all
four now read which encodings exist from one table in that header**, `NMFC_F7_DEFINED`; the
functional model's private copy is compared against it at build time over all 128 values,
because a copied table is only honest if something compares the copies. With `WAIT` the table
accepts the continue group's variants 0 to 2, so **31** `funct7` values are defined and 0x43 is
reserved; `check_isa.sh` compares the two copies over all 128 values, and the unit test
`isa_f7` checks that 0x42 is defined, that 0x43 is not, and that `NMFC_WAIT_INSN(x11)`
assembles to `0x8405a00b` (C code spells it `NMFC_WAIT_LINE(addr)`).

What was checked for this document:

1. **Assembled.** One word per instruction — all fourteen, both `END` forms, the lane-0 and
   lane-7 forms of `CXW` and `CXR`, and the marker — through the ordinary assembler, from the
   same header the simulators decode with. Eighteen words, then disassembled back to the words
   below.
2. **Decoded.** Every assembled word was taken apart again into opcode, `funct3`, `funct7`,
   group and variant, and recomposing `funct7` from the group and the variant reproduced the
   word exactly. All eighteen matched the instruction intended.
3. **Copies compared.** The functional model's copy of the encoding was checked against the
   header: sixteen constants, the marker's `funct7`, and the marker's whole 32-bit word. It
   matches.
4. **Register maps compared.** The two register-name decoders — the tile's and the functional
   function core's — were run over all 32 five-bit encodings and agree on every one: same bit
   offset, same width, same legality, same zero.
5. **Routing read.** Each word was traced through the dispatch of all four decoders, and
   through the host core's own decoder, which routes this opcode to its coprocessor and reads
   `funct3` as the three operand flags in the same bit order the header assigns them.

The words, which are what any future decoder must agree with:

| instruction | word | instruction | word |
|---|---|---|---|
| `FORK.R` | `0x00c5f50b` | `CONT` | `0x8005a00b` |
| `FORK.M` | `0x02c5f50b` | `CONT.M` | `0x82c5e00b` |
| `FORKF.R` | `0x04c5f50b` | `CXW` lane 0 | `0xa0c5e00b` |
| `FORKF.M` | `0x06c5f50b` | `CXW` lane 7 | `0xbcc5e00b` |
| `FORKQ` | `0x2000150b` | `CXR` lane 0 | `0xa205b50b` |
| `JOINQ` | `0x2205b50b` | `CXR` lane 7 | `0xbe05b50b` |
| `JOIN` | `0x40c5f50b` | `KILL` | `0xc005a00b` |
| `END` | `0x6000000b` | `RESUME` | `0xc205a00b` |
| `END.R` | `0x6200000b` | marker | `0xe000000b` |
| `WAIT` | `0x8405a00b` | | |

Those words use `x10` as the destination and `x11`, `x12` as the sources wherever the
instruction has one; the register fields are not part of the instruction's identity.

**All four decoders agree on all eighteen defined words**, and on `WAIT`'s, added since: the
continue group is now decoded on its whole variant by both function-core models (a decoder that
told `CONT.M` from `CONT` by the M bit alone would have run 0x42 as `CONT`), both hosts refuse
0x42 as a function-side instruction, and every decoder refuses 0x43. Four disagreements were found
beyond them. All four are now closed, and the first of them is what the one shared table is for.

- **The reserved encodings, which are the space a later instruction is added in.** An
  instruction is identified by `funct7`: three bits of group and four of variant, of which
  thirty values name something and ninety-eight name nothing. Each decoder was spending part of
  that space, each of them reasonably and each of them differently — the functional model read
  "variant 0 is `KILL`, anything else is `RESUME`" where both timing hosts read "variant 1 is
  `RESUME`, anything else is `KILL`", and the probe group's undefined variants decoded as a
  second `FORKQ` or `JOINQ` on one model and were refused on the others. Fourteen words of the
  control group were a privileged trap on one model and an unprivileged kill of whatever handle
  the `rs1` field named on the other two. Nothing in the tree emitted those words, which is
  exactly why it survived, and it matters because a reserved encoding that decodes as its
  neighbour cannot be used later: a program assembled against the later meaning would run on an
  older machine and silently compute something else. **There is now one table, and every
  decoder refuses an undefined value before it looks at the group.** A unit test checks the
  table against the instruction set's own list of thirty in both directions, with the three old
  rules beside it as controls that each accept fourteen words naming nothing, and three directed
  tests issue a reserved variant on purpose in the three groups that had a default, all three
  meant to abort.
- **High-half multiply, and the division that overflows.** The cycle-accurate tile rejected
  `MULH`, `MULHSU` and `MULHU` as illegal while the functional function core implemented them —
  a compiler emits them without being asked, for an overflow-checked multiply or a division
  turned into a multiply by a reciprocal — and it computed the most negative integer divided by
  −1 with the C++ operator, for which that pair is undefined and ends the process. Both are
  fixed, and a directed test asserts twenty answers against arithmetic on the ratified
  definition rather than against a recording of either model, running the two overflow pairs
  first because they are the cases that used to end a simulation.
- **Operand flags on the out-of-order host.** That host's coprocessor instruction declared
  `rd`, `rs1` and `rs2` used unconditionally, ignoring the `funct3` flags that say which of them
  the instruction reads, so an instruction reading neither source was renamed against whatever
  producers happened to be writing the register numbers sitting in its unused fields, and waited
  for them — a false dependence, invisible in every answer it gives and worth a different number
  of cycles depending on surrounding code it does not read. The unused slots now name the
  register that ignores writes, `x0`, rather than being removed, because the issue path reads
  its input slots by index; no binary in the tree changes, since its assembler macros already
  emit `x0` there.

#### 1.4.7 The host's cache-block clean: one standard instruction the machine relies on

The host is an ordinary 64-bit RISC-V core, and one standard instruction outside the base set
matters to this machine: **`cbo.clean`** from the RISC-V cache-block management extension
(Zicbom). Arm's `DC CVAC` and x86's `CLWB` do the same thing. It is not part of the extension
above and takes none of its encodings.

| instruction | encoding | operands | what the model does |
|---|---|---|---|
| `cbo.clean (rs1)` | I-type, opcode MISC-MEM `0b0001111` (0x0f), `funct3` = `010`, `imm[11:0]` = 0x001, `rd` = `x0`; `0x0015200f` with `rs1` = `x10` | `rs1`: any address in the 64-byte block | writes the block back to the last-level slice it belongs to, if the host holds it dirty, and leaves the host's copy in place, clean and shared |

**Why the machine needs it.** A `FORK.M` invocation starts by reading a 64-byte context block
that the host wrote. Without a clean, that line sits modified in the host's second-level cache
until a tile reads it, and the read is a coherence recall: the directory snoops the host,
the host sends the line across its own link, and ownership moves to the tile. With a clean
issued after the block is written, the slice under the block holds the current bytes, the
directory records the host as a sharer with no forwarder named, and the tile's read is
answered by the slice.

**What each part of the model does with it.**

- *The host core.* The decoder turns it into a store with no data. It takes a store-queue slot
  when it is renamed, leaves the queue from the head of the reorder buffer on a store port, and
  is sent as a flush that keeps the line. It holds a store-buffer entry until the first-level
  data cache acknowledges it, so a store fence waits for it. It is not an access: the queue
  never forwards from it, never holds a load behind it, and never charges a younger load an
  ordering violation because of it. `cbo.inval`, `cbo.flush` and `cbo.zero` are decode faults.
- *Translation.* The host's memory-management unit translates it like a store.
- *The first-level data cache* is write-through and holds nothing dirty. It keeps its copy and
  passes the clean down, behind any store to the same line that is still in flight, and it
  takes a miss-status register for it as a store does, so later stores to the line wait
  behind it.
- *The second-level cache*, the host's agent on the fabric, performs it. A line held M or O is
  sent to the directory in a `Clean` message carrying the line, and the local copy becomes S.
  A line held clean (S, F or E) or not held is left alone, as Zicbom specifies. Either way the
  request is answered at once, like a posted store.
- *The directory* treats a `Clean` as a writeback that does not evict: the line is written to
  its slice, and a transaction on the line still waiting for data takes the bytes. The sender
  becomes a sharer, the owner is cleared, the line is recorded clean and shared, and no
  forwarder is named, so the next reader is answered by the slice.

**The existing paths it maps onto.** On the host's side it is the transition the `FetchXfer`
snoop already makes of a host line (M or O to S, the data leaving with its dirty flag), started
by the holder rather than by a reader. On the directory's side it is the `PutM` writeback path,
including its rule for a writeback that crosses a snoop. That rule has one addition. The host
can clean a line and then answer a snoop of it from the copy it kept, so the same bytes reach
the transaction twice from the same cache. That is counted as `cleansCrossed`, not as two
suppliers.

**What it costs.** In the host's memory unit it takes:

- a store-queue slot from rename to commit;
- a store port at commit;
- a store-buffer entry for one first-level-to-second-level round trip;
- a miss-status register in the first-level cache for the same time;
- one tag access in the second-level cache.

For a dirty line it also puts 72 bytes (an 8-byte header and the line) on the second-level
cache's outbound fabric link, and makes one slice write. It makes no memory write.

**Ordering.** A clean changes no value anywhere, so a program is correct with or without it,
and it needs no fence for correctness. The runtime issues one per prepared context block
after writing it, and the single release before the forks orders it with everything else.

**The functional model** runs the extension too. A clean of a line the host holds dirty
updates the recency record that an image carries: the line is marked clean and shared for the
host, and most recently used and dirty in the last level. A restored image therefore
installs it in the state the cycle model's clean leaves it in.

**Tests and counters.** The directed test `host_clean.c` runs three contended cases:

- a clean crossing a tile's `FORK.M` read of the same block;
- several tiles forking from cleaned blocks on one page;
- a line the host keeps writing and cleaning while invocations add to it and read it back.

It passes at 1, 2 and 4 tiles, and again with the cleans compiled out. The counters are:

| component | counters |
|---|---|
| core | `cleans_executed`, in the load/store queue, whose statistics the standard configuration does not enable; the first-level cache's `cleansPassedDown` counts the same cleans |
| first-level data cache | `cleansPassedDown` |
| second-level cache | `cleans`, `cleansWrittenBack` |
| directory | `cleansArrived`, `cleansApplied`, `cleansCrossed`, `cleansStale` |

`cleansStale` counts a clean that arrives after its line has changed hands. It is zero by
construction, because the sender's link delivers the clean before any snoop answer the sender
gives after it, and it is in the suite's zero-gate register. With the cleans in place, no
read of a cleaned block recalls a line from the host.

### 1.5 The tracking unit, sized to the contexts

An entry in the tracking unit is held from `FORK` to `JOIN`. Entries are therefore the
invocations that may exist at once, and an invocation that cannot exist cannot occupy a
context. A unit smaller than the machine's context count leaves part of the machine
unreachable however much work a program has, so the hardware's depth is **derived from the
machine being built** as the larger of two floors: the machine's own contexts (tiles ×
contexts per tile), and the count the binaries in use were compiled against. Nothing caps
it. The fabric's control queue follows the unit, because a host credit is a place in that
queue and leaving it at a smaller number would move the same cap one component down.

| machine | contexts | tracking unit | control queue |
|---|---|---|---|
| 4 tiles × 128 contexts (the measured machine) | 512 | 512 | 512 |
| 4 tiles × 32 contexts (configuration default) | 128 | 256 | 256 |
| 1 tile × 32 contexts (unit tests) | 32 | 256 | 256 |

The number in the instruction-set header stays, because a program that builds a
fire-and-forget ring has to compile against something; what changed is that the hardware no
longer takes that number as its own size. The functional simulator that writes starting
images derives the same depth the same way, because an image names entries by index and a
timing model must refuse an image whose handles mean something else.

**An entry is not a live context.** This is the single most important reporting rule in the
document, and §4.7 is about what happens when it is forgotten: across the measurements
below the tracking unit routinely holds many entries while the engines hold very few live
contexts, because most of those entries are invocations that have already finished and are
waiting to be collected. Context parallelism is reported per engine, split by state.

### 1.6 The fabric, the caches' miss-status files, and the host's predictors

Three parts of the machine outside the tile core were rebuilt in the same week, each because
what was there could not answer a question that was being asked of it. They are recorded in
*Three corrections to the simulated machine: the interconnect, the caches' miss-status
registers, and the load-store dependence predictor*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/fix3/MACHINE-FIX-3.md`), and every quantity
below carries where it came from.

**The fabric is a port per endpoint per direction.** It used to charge one serialisation
budget per attached cache, which carried both directions at once, charged only the sending
end, let writebacks and snoop responses cross for free, and gave the directory a latency but
no throughput — so a destination every agent was sending to looked idle, and a large part of
the traffic a machine of function cores makes was invisible. Now a packet is serialised onto
the source endpoint's outbound link, crosses the distance, and is serialised off the
destination's inbound link; both links are held for the serialisation and every endpoint's
links are independent of every other's.

| quantity | value | where it comes from |
|---|---|---|
| port width, one direction | 32 B per port cycle | a 256-bit flit: Arm's CMN-700 carries one flit per link per direction per cycle and is configurable at 256 bits in the server parts this machine is sized against; Intel's mesh links from Skylake-SP onward, and the ring before them, are 32 B per cycle per direction |
| port clock | 2 GHz | within the published operating range of those parts (Skylake-SP's mesh runs at about 2.4 GHz and later generations lower it). **Assumed**: this machine has no interconnect clock of its own, and 2 GHz is deliberately below the fastest cited |
| bandwidth per port per direction | 64 GB/s | 32 B × 2 GHz |
| endpoints | 10 | two per tile for four tiles, plus the host's second-level cache and the operating system's attachment |
| machine aggregate | 1280 GB/s | 10 × 2 × 64 GB/s, written into every run's own statistics file so a timing figure never has its interconnect width looked up elsewhere |
| memory behind it | 76.8 GB/s | four DDR5-4800 channels at 19.2 GB/s, from the device file the memory controllers are configured from |
| hop latency, tile to tile | 1 ns | a published mesh hop is 0.4–0.6 ns, rounded up to the shortest time this component can charge |
| host to any tile | 4 ns | the host is not on a memory stack and pays a fixed toll to reach one. **Assumed** |
| directory | one home node per slice, one transaction per 2 GHz cycle | Intel's caching/home agent and Arm's fully-coherent home node are both per-slice structures |

The aggregate is 16.7 times the memory behind it, which is the relation an on-die interconnect
has to its memory system and the thing the previous model did not have. Everything crossing is
counted per port, per direction, and by kind — **control** (the machine's own dispatch:
invocation, completion, migration, kill, fault, resume), **coherence** (protocol messages
carrying no line) and **data** (line payload) — and the three are never added into one figure,
which is what the old accounting made impossible. Writebacks and snoop responses were the one exception, and it was a defect: they booked their
link but were acted on the moment they were sent, so a link offered more of them than it could
carry delivered them all anyway while its booking ran ahead of the clock. They now cross their
sender's link one packet at a time, in the order sent, and are acted on only when their last byte
has arrived (NMFC-Rev `0f27940`). Nothing waits on anything but the clock, so the change cannot
deadlock the protocol; §2.2 gives what it did to the busiest window measured.

**What the counters say when one port is busy, and how to read a port's name.** A port's `in`
direction is the endpoint *driving* the fabric, not traffic arriving at it; and in the machine
before this rebuild a single port carried both directions at once. A reading of "the host's
inbound port at 89 %" taken on that machine is therefore neither inbound nor one direction, which
is the first thing the rebuild was for. Read correctly, the busiest window measured so far is the
**16 MiB** graph search's level 7, in which a tile data cache's pooled miss-status file — 32
entries, the structure §1.6's per-bank derivation replaces — was full for 511,347 of 686,408
cycles. The link carrying the load is **the host's second-level cache's**, configured in that
machine at 64 B per 1 GHz cycle (`bytesPerCycle=64`), at 89.2 % occupancy with 110,758,839 cycles
of packets waiting behind it. What fills it is **line data**: 201,269 packets of 72 B holding it
402,538 cycles, 65.8 % of its busy time, against 209,509 coherence packets of 8 B and **zero**
control bytes — returned contexts ride the operating system's attachment, at 0.1 % occupancy. The
sender is the host's own second-level cache, answering 200,504 snoop-fetches against 4,152 misses
of its own, and the requesters are the four tile data caches through the home nodes. The binding
resource is **the port, not the directory**: the four home nodes, configured at two transactions
per fabric cycle each (`directoryRate=1`, `directoryClock=2GHz`), run at 9 % with **zero** cycles
of waiting. The reading taken beside it on the corrected machine — busiest port 29 %, tiles issuing into
29.9 % of pipe slots with 15.5 contexts ready — was an average over a window two thirds of whose
tile-cycles held no context at all, and the busiest port read low because the backlog that
emptied the tiles had been booked during the warm-up, outside the measured region. The cause was
the writeback and snoop-response defect above, not a limit in the tile; §2.2 gives the account.

**A miss-status file is per bank, and its size is derived from the contexts it serves.** Such
a register holds one outstanding miss — the tag being fetched, the requests waiting on it, and
how the fill will land — and all three are properties of one bank, so a banked cache has one
file per bank and a full file stops only that bank. The tile's data cache had 32 registers in
one pool against 128 contexts each entitled to one outstanding operation, and the last-level
slice had the memory library's default of −1, which that library's own documentation describes
as "a very large MSHR": an unbounded number of outstanding misses, which is not a structure
anything is built as. The cap was real and was read before anything was changed: on the graph
search at 32 MiB every tile's data cache had no register free for about 14 % of the widest
traversal level's cycles, and that level is 51 % of the traversal.

| cache | banks, and where the count comes from | registers per bank | total |
|---|---|---|---|
| tile data cache | 4 — one per pipe, because a tile of four pipes performs four data accesses per cycle and an array of that throughput is built as that many single-ported banks | ceil(128 / 4) × 1.5 = 48 | 192 |
| tile instruction cache | 4 — one per pipe, four instruction fetches per cycle | 48 | 192 |
| last-level slice | 32 — the DDR5 device behind it has 8 bank groups of 4 banks per rank, and the slice is banked to that count by the same address bits | ceil(4 × 128 / 32) × 1.5 = 24 | 768 |

128 is the contexts a tile carries in this configuration; the 1.5 is the over-provision for
the banks' unequal shares of a real access stream, and it is argued rather than chosen — this
machine's own partition test measures a spread of 1.31 across a tile's banks, and 1.5 is the
smallest round number above it. The slice is the exception to the per-bank arrangement,
because its cache keeps one pooled file in a library outside this tree; it is given the *sum*
of the files it would have had, which buys a bound derived from the contexts it serves and
does not buy the per-bank independence the tile's caches now have. Each file is counted
separately — waiting cycles, depth every cycle, deepest ever — so a cache with one hot bank is
told apart from a cache short of registers everywhere.

**The host's predictors, and one reporting rule.** The host's memory-dependence predictor is
now the path-history predictor, and it is the default. The machine ran a two-bit counter before
this work; a path-history predictor was built beside it and, first measured, held fewer loads and
violated far more, and the cause was in the wiring rather than the table — the path history
advanced when a memory instruction entered the queue, so a load whose address resolves many cycles
later was predicted and trained on a route through instructions it was never reached along. A load
now takes a token for the history as it enters the queue and carries it in its own entry, and the
directed test that drives the predictor in the queue's order separates the two paths through one
load exactly where the live-history arrangement, instantiated beside it as the control, separates
nothing. It has now been measured the way a default is owed: one host-only program, the chained
hash table at 16 MiB, and eight sampled windows that every arm ran identically, from the same plan
with the same warm-up and the same measured width. Whole-program cycles are 153,357,038 for the
path-history predictor, 159,972,624 for the store-address predictor and 211,491,906 for the two-bit
counter — **1.3791× faster than the counter**, Fieller interval [1.3154, 1.4457], and **1.0431×
faster than the store-address predictor**, [1.0019, 1.0866], both intervals excluding 1. The
counters say why: the path-history arm holds 2,835,355 loads against the store-address arm's
3,226,392, twelve per cent *fewer*, while taking 23,498 memory-order violations against 182,668 and
4,776 replays against 75,885; the counter arm holds a third as many loads and pays 4,938,411
violations. Capacity is not what separates them — the path-history predictor peaks at 165 live
entries of the 20,480 it has. The store-address arm reproduced its earlier total to the cycle,
which is the check that the machine under the three arms did not move between the two
measurements.

> **The reporting rule for the bimodal control.** A figure that quotes the bimodal branch
> predictor as a control must print `execute_squash` beside `branch_mispredicts`. That control
> has no execute-time repair, so its mispredict count alone understates what it costs: the
> work thrown away when a wrong path is discovered later appears in the other counter, and a
> comparison against the override predictor that prints only the first is comparing two
> different quantities.

---

## 2. Inside one tile core

The tile core is **two independent pipelines that meet in one place**. The centre of the core is the **context array**: every context with its 512-bit register file, program counter, instruction slot and data slot. The slots belong to the context, not to a pipe. Each cycle the scheduler picks ready contexts out of the array into the pipes, and a context is ready only when its instruction is in its slot and it has no memory operation outstanding, so the pipe takes its instruction *from the slot* and never from a cache; if a context is scheduled it is certain to execute. The pipes issue what fills the slots: instruction fetches, speculatively at decode from the program counter or the shared branch-target buffer and non-speculatively at writeback, and data requests at writeback. Every one of those requests passes the translation path first, and none counts as issued until it is translated: a request whose translation faults or names another tile is dropped outright, and the context faults or migrates and re-attempts. Translated instruction fetches go to the banked instruction cache, which fills the context's instruction slot; translated data requests go through the delivery window into the memory queue that owns their physical address, then the data-cache bank, the last-level slice and memory, and the returned data fills the context's data slot, which is what wakes it.

```
   CONTEXT  (512 bits, PC, two slots)
      │                                 ▲
      │ instruction side                │ data side
      ▼                                 │
  [instruction slot] ◄── banked I$ ◄── I-translation
      │  decode: PC+4, or BTB target      (shared BTB, one
      │                                    speculative fetch)
      ▼
   issue into one of N pipes (M stages)
      │
      │  a load or store puts its VIRTUAL address in the load slot
      ▼
  ╔═════════ TRANSLATION PATH (virtually indexed) ═══════════╗
  ║  translation queues → shared TLB → page-table walker     ║
  ║  result: physical frame, page type, owning tile           ║
  ╚════════════════════════╤═════════════════════════════════╝
                           │ checks here: foreign tile → migrate;
                           │ kernel write to a duplicate page → fault
                           ▼
  ┌──────────── DELIVERY WINDOW (one structure) ─────────────┐
  │ decode bank bits; deliver the OLDEST entry per bank that  │
  │ has credit; at most one per bank per cycle                │
  └──┬──────────┬──────────┬──────────────────────┬──────────┘
     ▼          ▼          ▼                      ▼
  ┌─────┐   ┌─────┐    ┌─────┐               ┌─────┐
  │MEMQ0│   │MEMQ1│    │MEMQ2│   ...         │MEMQb│   each owns one
  │order│   │order│    │order│               │order│   bank's addresses:
  │ fwd │   │ fwd │    │ fwd │               │ fwd │   order, forwarding,
  │ RMW │   │ RMW │    │ RMW │               │ RMW │   atomicity
  └──┬──┘   └──┬──┘    └──┬──┘               └──┬──┘
     ▼         ▼          ▼                     ▼
  ╔═══════ BANKED DATA CACHE — one coherence client ═════════╗
  ║  bank array + tags; read-modify-write executed here;      ║
  ║  a snoop acts HERE, never on a queue                      ║
  ╚════════════════════════╤═════════════════════════════════╝
                           ▼
            LLC slice → memory controller → channel
```

The structural novelty is that the two sides share nothing. A translation miss occupies
translation hardware and leaves every memory queue free; a bank conflict occupies a memory
queue and leaves translation free. That is worth having only if it can be shown, so the
counters are arranged so that a stall on one path appearing as a stall on the other is a
detectable defect rather than an argument.

**Everything in this section is built and running, and the section describes what exists.**
It was designed first, in *The tile core: contexts, pipes, translation and memory queues*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/tilecore/TILE-CORE-DESIGN.md`), which states
each structure, the counters it owes and the directed test that reaches it; it was then built
in two steps, the second recorded in *The tile core's memory path: what it is, what replaced
what, and what the counters say*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/tilecore2/TILE-CORE-BUILD-2.md`), which wires
the path into the tile and deletes the mechanism it replaces — the table of words held above
the data cache, with its cache pins, its snoop merges and its unbounded waiter list — in the
same change, so that no interval exists in which the tree holds two mechanisms for one job.
Three defect sweeps have run over it since, and the whole stress population has since been run
under both simulators as a cross-check. Every seed is built once, so the cycle model and the
functional model execute the same bytes: 10,382 cross-model runs over 6,369 seeds, comparing
45,545 quantities — four per seed, plus the entire final memory image on the 4,013 seeds with no
exchange phase — across every risk class the generator makes, from plain atomics to contended
atomics, migration, exchange and migrating pairs. There was **one** disagreement, and it was a
mechanism no seed reaches, so it was the mechanism's own directed test that found it: the
functional model *performed* a kernel store to a duplicate page, where the ratified definition
requires the store to be refused, counted and to leave memory unchanged. The definition decides,
not a majority of two models, and the functional model was fixed (commit `a660cbf`). Two further
apparent disagreements were faults in the comparison rather than in either model and are now
checked identities — issued instructions minus migrations equals instructions executed, and
`FORK.R` executed minus refusals equals forks taken — and the cycle model's 150,087,496
function-core instructions and 1,372,952 migrations reconcile exactly against a functional model
that has neither. The whole population fitted in 2.6 process-hours against a 32-hour budget, so
nothing was sampled. What each defect sweep found is stated beside the mechanism it is about
rather than collected at the end, and what is still designed and not built is now a short list,
which §3 gives.

### 2.1 The two context slots

A context has exactly two small private holding registers, and that count is structure. Each context is an independent thread with its own program counter, its own instruction in hand and its own outstanding access; contexts do not execute in step and no two need be at the same instruction. The engine is a fine-grained multithreaded core, not a vector or single-instruction-multiple-thread machine.

**The instruction slot** holds what the context will execute next. A context cannot be
scheduled without its instruction in hand. At the end of a dispatch the tile already knows
where the next instruction is — the program counter plus four, or, if the instruction just
decoded is a branch the shared branch-target buffer knows, the predicted target — and that
address is sent to the instruction path immediately, a full re-issue window before the
context can use the answer. Nothing executes on the prediction: a wrong target means the
slot holds an instruction the context will not use, and the cost is exactly the refill a
machine with no predictor pays every time. The buffer is shared by every context on the tile, as a target buffer indexed by program counter is in any multithreaded core; contexts run independent instruction streams, each with its own program counter and its own slots, and sharing the buffer is what keeps it from growing with the context count.

Because the slot holds the next instruction and one speculative fetch covers the whole
re-issue window, the tile needs **no decoupled fetch engine and no run-ahead fetch stream**.
That is a deliberate absence, not an omission.

Whether the slot holds one instruction or a short aligned block is open, and it is a trade
between per-context state, which multiplies by the context count, and instruction-cache
traffic, which multiplies by the pipe count. The code says what the trade looks like:
counted statically over the three function images in the tree, straight-line runs between
one control transfer and the next have a mean of 4.6 to 4.8 instructions, a **median of
three**, and a longest run of 13 to 36. A whole kernel is eight or nine cache lines of code.

| slot holds | per-context state | at 256 contexts | at 1024 contexts |
|---|---|---|---|
| 1 instruction | 87 B | 21.8 KiB | 87 KiB |
| 4 instructions | 99 B | 24.8 KiB | 99 KiB |
| 8 instructions | 115 B | 28.8 KiB | 115 KiB |
| 16 instructions (one line) | 147 B | 36.8 KiB | 147 KiB |

Those are state costs, not results; runs with a median of three instructions mean a block
much larger than a few instructions mostly pays for instructions a taken branch discards.
The structure is that the slot holds a contiguous aligned run with a valid mask and a
consumed offset; the size is configuration, swept at 1, 4, 8 and 16, and the rule written
before the numbers exist is to take the smallest block whose waiting-for-instruction cycles
are within measurement noise of the largest. One instruction remains a legal setting and is
the reference point, because it is what the model does today.

**The load slot** holds the one memory operation a context is waiting on, whatever its kind: a load, a store, a read-modify-write atomic, or either half of a load-reserved / store-conditional pair. It carries the operation, the virtual address, the access width and sign-extension class, the destination register name, and for a store or an atomic the value operand. It is the context's *single* outstanding memory operation, so on return there is nothing to disambiguate and no staging area is needed, and the context cannot begin its next instruction until that operation has returned its value or, for a store, has committed in its memory queue. The operation is not issued until it is translated; before that it can be dropped freely, because nothing downstream has seen it, and the context re-attempts it after a fault is handled or after it migrates. That is why a load-reserved's serialisation point begins only when its entry enters the memory queue: a context has not claimed an atomic attempt until then. A context that migrates carries no slot with it: the operation whose translation named the other tile was dropped before it was issued, and the context re-issues it on arrival. A load wakes the context when its data has been written into the named bits; an atomic is performed in the memory queue that owns its address and wakes the context the same way, with the old value or the store-conditional's result in the named bits. Atomics are ordinary traffic on this path, and the queues exist in large part to make them cheap.

One outstanding operation per context is policy, not an accident. A load and an atomic occupy the slot until their value returns. The default applies to stores as well: a store occupies the slot exactly as a load does, so a context has at most
one memory request anywhere in the machine at any time and its own accesses are in program
order whatever the delivery window does. A relaxed rule — a store released at queue
admission, with a small per-context count of outstanding stores that must reach zero before
the invocation retires — is a configuration switch with both arms correct, because it buys
store throughput at the price of two new obligations: a context with outstanding stores
cannot migrate until they have drained, and per-destination order then rests entirely on
the delivery window's selection rule. The switch carries its own before-and-after
measurement; it does not become the default by assertion.

The load slot is also where three decisions are taken, because all three are properties of
the *translation result* rather than of the virtual address: a translation naming another
tile turns the access into a migration; a translation naming a duplicated page on a store
or an atomic is refused and counted; a translation that misses leaves the slot occupied
while the walk runs.

**The load slot keeps an arm after its operation completes** — a physical line address and a
valid bit, 44 bits per context and no data. A load or a read-modify-write atomic writes it when
its translation completes; a store, a load-reserved or a store-conditional clears it at theirs;
a dropped translation and departure clear it. Every invalidation broadcast from a data-cache
bank is compared with every armed slot (one equality compare per context, gated by the valid
bit), and a match clears the arm whether the operation is still in flight or has returned. This
is what `WAIT` sleeps on (§1.4.3): a waiting context holds no issue slot and wakes when its
arm is cleared. The comparison is linear in contexts and there is no structure that records
who waits on what. Nothing answers a load from the arm; the zero-gated `slotAnsweredLoads`
says so on every run.

**A `WAIT` uses the load slot as a load does, then leaves it.** It fills the slot and passes
through the translation queues; a fault parks the context, and a line owned by another tile
migrates it. Once translated it never enters the delivery window or a memory queue: the
decision is one step in the context array. If the arm is valid and names the translated line,
the context enters **`WAITING`**, a state the scheduler never picks; otherwise it completes at
once and the cause is counted. A context in `WAITING` leaves it when a broadcast clears its
arm, or through the one release sequence when it is killed. A context arriving by migration or
by restore from an image starts with an empty arm.

**Counters.** Issued, translated, migrated and slept (`waitIssued`, `waitTranslated`,
`waitMigrations`, `waitSlept`); context-cycles asleep (`waitCycles`, a bucket of the census
partition, §2.2); wakes by cause — a remote write, a local write, an eviction, an arm already
invalid or on another line at issue, a migration, a restore, a kill; arms cleared while their
operation was in flight (`waitArmClearedInFlight`); contexts woken per broadcast
(`waitWakeFanout`, bins 1, 2–7, 8 or more); broadcasts by kind and the port cycles they cost
(§2.7); eviction-wake chains (`waitEvictWakeChains`); re-loads after a local-write wake and
their sleep cycles (`waitLocalWakeReloads`, the forwarding counter); downgrades of a line with
sleepers (`snoopDowngradesOnSleepers`, `snoopDowngradeSleepers`). **Five zero gates**, in both
suites: a `WAIT` admitted to a memory queue (`waitQueueAdmits`); a context asleep before its
`WAIT` translated (`waitSleptUntranslated`); a context asleep with a serialisation point open
(`waitSleptWithOpenPoint`); a load answered from the arm (`slotAnsweredLoads`); and a context
still asleep 2,000 cycles after a write to its line was performed (`waitMissedWakeups`), which
the simulator checks at every write performed at a bank and at the directory whenever a host
write starts, and which stops the run.

A data prefetcher could fill the load slot speculatively from previous addresses. It is
deliberately undesigned, and the slot is left free for one.

**How an invocation's context arrives, and what `FORK.M` costs a tile.** A `FORK.R` arrival
brings its 512 bits with it and asks only for its first instruction. A `FORK.M` arrival has a
context slot and no register file until the tile has read its 64-byte block. That read is
translated like any load. It then joins the tile's queue of *physically addressed* requests:
page-walk reads, page-table writes and context transfers, which carry a physical address
already and so skip the translation queues. That queue is served in order into the delivery
window, where it has a slot of its own beside the shared width (§2.4). The tile counts the three
parts of a `FORK.M` start:
translation (`forkMCtxTranslate`), the read (`forkMCtxRead`, of which `forkMCtxPathWait` is
the wait for a window slot) and the instruction fetch after it (`forkMCtxToIssue`).

On the compiled reduction the read is almost all queueing: each cycle the delivery window is
refilled first from the translation path and only then from the queue of physically addressed
requests, and the window is full in about 72 % of tile cycles. The table's `FORK.M` rows below
were taken while the translation buffer still missed often and that queue was full of walk reads;
the reading then was that the walks were what the context read waited behind. Removing the walks
(§2.3: one leaf per grain and a fully associative buffer, 443,955 tile walks to 98 on this run)
showed otherwise — the wait grew to 6,380 cycles, because the contexts that had been parked on
walks issue loads instead and the translation path takes every slot the window frees. The window
now gives that queue a slot of its own (§2.4), and the wait is one cycle.

| point | form | fork to first issue | translation | read (of which: wait for a window slot) | to first issue | block reads recalled from the host |
|---|---|---:|---:|---:|---:|---:|
| directed test, 4 tiles, 1,920 forks, an idle machine | `FORK.M`, block not cleaned | 32.7 | 5.5 | 19.1 | 8.1 | 1,920 |
| same | `FORK.M`, block cleaned | 34.6 | 5.1 | 21.4 | 8.1 | 0 |
| reduction, 4 MiB, uninterrupted run | `FORK.R` | 10.5 | — | — | — | 1,144 |
| same | `FORK.M`, not cleaned | 1,183.4 | 0.2 | 1,173.3 (1,085.1) | 9.9 | 6,202 |
| same | `FORK.M`, cleaned | 1,060.0 | 0.2 | 1,050.5 (957.2) | 9.3 | 1,164 |
| same, one leaf per grain (§2.3) | `FORK.M`, cleaned | 6,487.6 | 0.2 | 6,478.0 (6,380.2) | 9.4 | — |
| same, the window's physically addressed slot (§2.4) | `FORK.M`, cleaned | **107.9** | 0.2 | 98.8 (**1.0**) | 8.9 | — |
| reduction, 16 MiB, sampled regions | `FORK.R` | 8.8 | — | — | — | 516 |
| same | `FORK.M`, not cleaned | 703.5 | — | 1,007.0 | — | 5,561 |
| same | `FORK.M`, cleaned | 739.0 | — | 962.3 | — | 490 |

Notes on the table:

- All figures are mean cycles per fork.
- The last column is `nmfcPaysSnoop` over the same span. On the reduction, `FORK.R` has
  about 1,150 recalls of its own at 4 MiB, none of them a block read. The cleaned `FORK.M`
  build is at the same level.
- At 16 MiB, the read column comes from the 11 of 32 regions measured after that counter was
  added.

The `cbo.clean` of §1.4.7 removes every recall and leaves the start latency where it was. On an
idle machine the read is the order of a last-level hit either way. On the reduction a context
read waits about 1,000 cycles for a window slot, and a cleaned block saves at most about 120 of
them. The program's time moves by 1.0043, Fieller interval [0.9536, 1.0577] (uncleaned ÷
cleaned, 16 MiB, 32 regions per arm).

Two changes to the machine were named to shorten the wait, and both are made. Grain-sized
leaves (§2.3) removed almost every walk read and made the program 0.2 % faster, and made this
start six times slower, which showed the second was the one that mattered: a slot of the delivery
window that only the physically addressed class (walk reads, page-table writes, context transfers)
may take, beside the shared width the translated class keeps whole (§2.4). With it the start is
107.9 cycles, of which the wait for a window slot is 1.0, and the program runs 2.3 % faster
(1.12421 to 1.09808 ms, answer unchanged). What remains is the read itself, about 98 cycles on a
loaded tile against about 19 on an idle one, spent in the memory queue, the bank and the slice.

### 2.2 The pipes and the issue rule

An engine has **N pipes of M stages**. Each pipe is M stage registers; a stage register is
either empty or holds one in-flight instruction with its owning context, its decoded
operands, its destination name and the cycle it entered. An instruction advances one stage
per cycle and leaves at stage M. Nothing is squashed and nothing is replayed, so a stage
register never needs a recovery copy.

Three stages are named, because three events depend on which stage they happen in, and
those three are the whole of the pipes' interaction with the rest of the core:

1. **decode** — operands are read from the 512-bit context and register names are checked;
   the branch-target-buffer lookup and the next instruction fetch are launched;
2. **address** — a memory instruction's virtual address is complete and leaves for the
   translation path; a load's context yields here;
3. **writeback** — the result is written into the context and the context becomes eligible
   again.

**The issue rule, which is the default and the working assumption: one instruction from
each of N different contexts may start per cycle, and a context issues at most once every
M cycles.** Two instructions in the pipes are therefore never from the same context, never
dependent, and the pipes need no forwarding, no interlocking and no hazard detection: a
pipe is a shift register with an arithmetic unit hung off it. Reaching N issues per cycle
needs at least N×M contexts that are not waiting for anything, which is the floor the
context count is chosen against.

The alternative is to **bind a context to one pipe** and give the pipe cross-stage
forwarding, so that a context can issue on consecutive cycles; a binding ends at a memory
instruction, at an empty instruction slot, at a mispredicted control transfer, at a
starvation limit, or when the context has nothing to issue. It costs per *pipe* — an owner
field, a consecutive-issue count, operand comparators and forwarding multiplexers — and not
per context, so it does not grow with the context count.

**The second alternative is not built on an argument; its build is gated on a measurement,
and the measurement runs on the model that exists today.** It can only help in a cycle
where two things are true at once: a pipe slot would otherwise go unused, *and* some
context is blocked on nothing but its re-issue window. If the engine is issue-saturated,
removing a window gains no work, because another context would have used the slot; if the
engine is starved because its contexts are all waiting on memory or on instructions,
removing a window also gains nothing, because the contexts are not ready. So the
discriminator is a conjunction rather than a histogram: count unused issue slots in cycles
where at least one context was window-blocked, against unused slots in cycles where none
was, and against the offered slots. Beside it go a census of context-cycles by cause of
not being ready — no instruction in hand, asleep on a load, waiting on a page-table walk,
asleep at a `WAIT` (added with that instruction), other — and a banded histogram of load-free run lengths with the cause each run ended.
Those counters plus window-blocked cycles plus running occupancy **partition** a resident
context's cycles, so a gap is a modelling error rather than a finding.

**The stage registers are built, the counters were taken, and the alternative is refused on
that evidence rather than deferred.** The pipes are `N × M` stage registers with the three
named stages, and the array is checkable: `pipeIssues × depth` equals `pipeStageOccupancy`
plus `pipeDrainDeficit` exactly — 27,554,702 × 8 = 220,437,616 = 220,437,531 + 85 over eight
measured windows of the graph search at 16 MiB — which is what says the array advances once
per cycle. `pipeSameContextInFlight`, the counter that would fire if two instructions in the
pipes came from one context, is gated to zero in both suites, and is zero over 273 million
context-cycles of that workload.

On the same run the first leg of the conjunction looks like a large opportunity and the
second leg closes it. The tile offered 649,287,536 issue slots and used 27,554,702;
257,028,078 of the unused ones — 39.6 % — fell in cycles where some context was blocked on
nothing but its re-issue window. But `runInstrBeyondM` is **zero** and every load-free run is
one instruction long, so a pipe that let a context issue on consecutive cycles would have no
consecutive instruction of that context to issue: the gain the mechanism exists for is not
present to be taken. What ends the runs is the instruction slot — 16.1 million of the 27.6
million instructions are followed by a run that ended because the context's next instruction
had not arrived — so on this workload the tile's front end, not its issue rule, is what a
context waits for. `pipeBind = 1` is therefore **refused at construction** rather than
accepted and quietly ignored, and the parameter stays so that the question can be re-read on
a workload with long arithmetic between memory accesses, which is the one case the
measurement does not cover.

**Why the pipes sat idle in the busiest window measured: the tiles were empty, and a fabric
defect emptied them.** Level 7 of the graph search at 16 MiB, re-run on the current machine with
the full census, took 2,851,003 host cycles (3 GHz). Of its 8.55 million idle issue slots, 7.67
million fall in cycles when the tile holds no context at all: the tiles hold work in 49.5 % of
their cycles, and when they do the four pipes use 88.3 % of their slots over the window and issue
4.00 of 4 per cycle through the body of the level, with about 20 contexts able to issue and
waiting for a pipe. The census was counting "ready" correctly; the earlier figures of 15.5 ready
contexts per tile and 29.9 % of pipe slots were averages over cycles, two thirds of which held no
context. The tiles were empty because the host retired no instruction for about 430 µs (1.3
million host cycles): its 351-entry reorder buffer full, its L1 data cache holding 10 misses and
its L2 14, and no packet anywhere in the fabric, the slices, memory or the tiles. Those misses
were waiting to be let onto **the host L2's outbound fabric link**, 32 B per 2 GHz port cycle —
64 B per 1 GHz fabric cycle, so a 72-byte line packet takes two cycles (`portWidth=32` and
`portClock=2GHz` in `src/nmfc/test/coherent_memory.py`; the width is sourced in §1.6 from Arm
CMN-700's 256-bit flit and Intel's mesh links, the clock is stated there as assumed). In level 6,
which is this window's warm-up, the tiles' reads forwarded to the host L2 — 768,067 snoop fetches
— asked for 141 % of that link, and because snoop responses were acted on as they were sent they
were all delivered at that rate while the link's booking ran about 450,000 cycles ahead of the
clock. The host's first ordinary request at the level-6/7 boundary did wait for its booking, so it
paid the whole backlog — a largest single wait of 489,894 cycles, where no other port waited more
than 70 — and no level-7 work was forked meanwhile. With writebacks and snoop responses crossing
their link one packet at a time (§1.6, NMFC-Rev `0f27940`) the window takes 1,590,118 host cycles
(−44 %) and the link's largest wait is 308 cycles. That is level 6 paying for its own traffic, not
a speedup: levels 6 and 7 together from the same image take 2,393.2 µs against 2,286.5 (+4.7 %),
and the whole program moves by 1.0067×, 95 % Fieller interval [0.986, 1.018], over the same 20
sampled regions paired. The directed test is section 6 of `unit/fabric_ports.cc`: first the
contended case, a host link offered twice its capacity in snoop data followed by its own request,
with the old handling as the failing control; then tile against tile, host against tile and
ordering on one link; 20 of 20 checks pass.

**What limits that window now is the design, at two floors within 4 % of each other.** The host
L2's outbound link is 100 % busy through the tile phase: the directory names the host L2 as the
supplier of 215,463 lines the tiles read, and at two fabric cycles each they need 430,926. The
pipes — 4 pipes of 8 stages per tile, the `NMFC_PIPES` and `NMFC_DEPTH` defaults in
`src/nmfc/test/vanadis-nmfc.py` — need 415,001 cycles for 6,640,015 instructions at 16 per cycle,
and are 95 % used. Lifting the link floor means supplying a clean line to a tile from its own
last-level slice rather than forwarding it from the host L2, a change to the directory's
forwarding policy that needs its own measurement across the workloads. Lifting the pipe floor
with 8 pipes needs at least 8 × 8 = 64 contexts per tile with nothing to wait on, where this level
gives each tile 64 invocations with about 29 of them asleep on memory, so it needs at least twice
as many, smaller ranges. Neither change is made. The drain, as the last ranges finish alone, is a
further 15 % of the window.

**The floating-point units and the divider.** Every pipe carries a pipelined floating-point
unit for add, subtract, multiply, fused multiply-add, compare, convert and move, and the tile
has **one iterative divider**, shared by every context and every pipe, for divide and square
root. The costs are Arm Neoverse N2's (*Software Optimization Guide*, PJDOC-466751330-18256,
issue 5.0, table 3-19), taking the worst case of each data-dependent range and holding the
divider for the reciprocal of the table's lowest throughput, rounded up:

| operation | latency (cycles) | issue | resource |
|---|---|---|---|
| FADD, FSUB, FMIN/FMAX, FSGNJ, compare, move | 2 | every cycle | the pipe's FP unit |
| FMUL | 3 | every cycle | the pipe's FP unit |
| FMADD, FMSUB, FNMADD, FNMSUB | 4 | every cycle | the pipe's FP unit |
| FCVT, FCLASS | 3 | every cycle | the pipe's FP unit |
| FDIV.S | 10 | one per 5 cycles | the tile's divider |
| FDIV.D | 15 | one per 7 cycles | the tile's divider |
| FSQRT.S | 9 | one per 2 cycles | the tile's divider |
| FSQRT.D | 16 | one per 8 cycles | the tile's divider |

The pipelined operations are all shorter than the 8-stage pipe, and a context cannot issue
again until its instruction leaves the pipe, so they cost nothing beyond the pipe; on a pipe
shallower than an operation's latency the context waits for the difference. A divide or square
root takes the divider that frees first, starts when it does, holds it for its issue interval
and is written back its latency after it starts. Until then its context **holds no issue slot**
— it waits as it waits for a load — and every other context issues. So the divider accepts at
most one operation every interval, and a tile whose contexts all divide runs at the divider's
rate: on the directed test, 32 contexts doing 64 double-precision divides each took 14,555 tile
cycles against the divider's 2,048 × 7 = 14,336, and 3,365 cycles when the divide was replaced by
a fused multiply-add (4.3×). One context alone pays each divide's latency beyond the pipe, 64 ×
(15 − 8) = 448 cycles exactly. Spread over four tiles, each tile's divider served its own quarter.
The divider count is configuration (`fpDividers`); one per tile is the design, because an in-order
multithreaded core shares one iterative divider among its threads, as N2 has one, on one pipe.
Nothing but a tile's own contexts can reach its divider, so it has no cross-agent case. The counters are
`fpDivOps`, `fpDivQueued`, `fpDivQueueCycles`, `fpDivBusyCycles`, `fpLongOps` and the census
bucket `ctxNotReadyFpUnit`, which the readiness partition includes.

**The integer multiplier and the integer divider.** Every pipe carries a pipelined integer
multiplier, and the tile has **one iterative integer divider**, shared by every context and
every pipe, for divide and remainder. It is a separate unit from the floating-point divider:
N2 divides integers on its M0 pipe and floating point on V0, so neither delays the other. The
costs are N2's (the same guide, table 3-7), the worst case of each data-dependent range. An
integer divide "blocks any subsequent divide operations until complete" (the table's note 1),
so the divider is held for the whole latency, which is also the reciprocal of the table's lowest
throughput. RISC-V's remainder is the same iterative operation as its divide and costs the same:

| operation | latency (cycles) | issue | resource |
|---|---|---|---|
| MUL, MULW | 2 | every cycle | the pipe's multiplier |
| MULH, MULHU, MULHSU | 3 | every cycle | the pipe's multiplier |
| DIVW, DIVUW, REMW, REMUW | 12 (N2: 5 to 12) | one per 12 cycles | the tile's integer divider |
| DIV, DIVU, REM, REMU | 20 (N2: 5 to 20) | one per 20 cycles | the tile's integer divider |

The multiplies are shorter than the pipe and cost nothing beyond it, as the pipelined
floating-point operations do. A divide or remainder waits for its divider and its result
exactly as a floating-point divide does, holding no issue slot. On the directed test, 32
contexts doing 64 64-bit divides each on one tile took 41,142 tile cycles against the divider's
2,048 × 20 = 40,960, and 3,866 cycles with the divide replaced by a multiply — the same as with an
add (3,874) — so the multiply is covered by the pipe and the divide is pinned to the divider
(10.6×). The 32-bit form took 24,773 against 2,048 × 12 = 24,576, and the remainder the same as
the divide. One context alone pays each divide's latency beyond the pipe, 64 × (20 − 8) = 768
cycles exactly. A loop that uses both dividers in every iteration took 41,157 cycles, under the
40,960 + 14,336 one shared unit would need, and spread over four tiles each tile's integer
divider served its own quarter. Before this, an integer divide cost one pipe pass and the
contended test ran in 3,872 cycles. The divider count is configuration (`intDividers`); nothing
but a tile's own contexts reaches it. The counters are `intDivOps`, `intDivQueued`,
`intDivQueueCycles`, `intDivBusyCycles`, `intMulOps`, `intMulLongOps` and the census bucket
`ctxNotReadyIntDiv`, which the readiness partition includes.

The host's floating point is Arm Neoverse V2's (*Software Optimization Guide*,
PJDOC-466751330-593177, issue 3.0, table 3-11): add 2, multiply 3, fused multiply-add 4 cycles,
pipelined on two units, and **two** dividers (V2 divides on two of its four vector pipes), each
held as the tile's is: FDIV.S 10 cycles and held 3, FDIV.D 15 and 7, FSQRT.S 9 and 2, FSQRT.D 16
and 8. The host used to run square root on its adders at 3 cycles and to accept a divide every
cycle. Its one integer divider (V2's M0 pipe; the guide's table 3-4 gives the same numbers as
N2's) takes 12 cycles for the 32-bit form and 20 for the 64-bit form and is held for each; it
used to take 12 for either and accept a divide every cycle. Its multiply unit is unchanged (3
cycles, pipelined). Measured over 4,096 independent 64-bit divides it now spends 20.4 cycles on
each, against 2.0 before.

### 2.3 The translation path

At the address stage a memory instruction's virtual address, address-space identifier,
width, operation and owning context enter a **translation queue**. Queues are indexed by
virtual address, because that is what a context presents. They are drained into the tile's
shared, address-space-tagged translation buffer, which holds 4 KiB and `G` entries side by
side (below); a hit produces a physical frame, the page type and the owning tile after a fixed
latency, and about five cycles ahead of the tag check is accepted. A miss starts a page-table walk; the page table is on
duplicated pages, so a walk never leaves home, and the request waits in its translation
queue rather than occupying anything on the data side.

Two numbers here are load-bearing and must be derived rather than written down.

**The completion rate is derived from the pipe count, not set to one.** The tile is
supposed to carry one instruction fetch and one data access per pipe per cycle; a
translation stage that completes one translation per cycle caps the whole data path at one
memory operation per cycle however many banks exist, and a cross-connection that cannot
limit the machine because the stage in front of it is serialised has not been shown to be
adequate. So the rate tracks the pipes, and the sizing arithmetic of §2.4 is read against
that rate. Whether a queue drains strictly in order — and therefore whether a miss blocks
hits behind it — is stated per configuration and counted as head-blocked cycles.

**Where a walk's own reads go is a design choice, both arms are built, and it is under open
analysis rather than settled.** A walk's memory references can take one of two paths, and both are configurations of the model. Through the *data cache*: the reads pass the delivery window and a memory queue like any access, page-table lines compete with data for the cache, and to keep a full window from stalling the walk that would free it each memory queue reserves at least one entry for a walk, with a counter for cycles a walk could not be admitted. Directly to the *last-level slice*: every walk step pays the slice's latency, but translation traffic never pressures the data cache and the reservation is unnecessary. The trade is latency against data-cache congestion, and which wins depends on the translation buffer's hit rate and on the ratio of walk reads to data requests: if the buffer hits nearly always the choice is immaterial; if walks are frequent and the ratio approaches one, the data cache is severely pressured; in between, whether a page-table hit or a data hit is worth more decides it. The analysis is the counters on each path across the workloads: buffer hit rate, walk reads per thousand data requests, page-table line reuse in the data cache, walk latency on each path, and the data-cache misses the page-table lines cause.

Three things about that analysis are settled and one is not. The model **refuses** a
configuration that asks for the reservation where there is nothing to reserve — a non-zero
`walkReserve` with the slice path is rejected at construction — and it refuses a translation
completion rate other than the pipe count, so neither arm can be measured as a machine other
than the one it says it is. A translation that misses leaves its queue and waits in a
walk-pending array, so the hits behind it proceed; the one case where a miss does cost them
is that array being full, and `xlatHeadBlockedCycles` counts exactly those cycles. What is
**not** settled is which path is better: on the two sampled points taken so far the two arms
are indistinguishable, and the first reading of that was worth nothing, because in the
configuration the comparison is run in all four walk-source counters read zero while 1,007
real walks were being performed — the walks were going somewhere none of the four bins named.
A fifth bin, `walkReadsUnattributed`, and an accounting gate that requires every walk read to
be binned now make that failure loud: the flat suite reports 195 of 195 walk reads attributed.
The arm that would separate the two paths is a workload whose own data traffic fills the
queues while a walk needs to issue, and neither point measured does that.

**One leaf per grain.** The machine has three page sizes — 4 KiB host pages, `G`-sized grain
pages (one tile) and `N × G` striped pages (one grain per tile) — with the size in the page-table
entry, and mapping every page type at 4 KiB is rejected: it multiplies translation work and makes
the contiguity of consecutive small pages the operating system's problem. Until this round the
model's table nevertheless wrote a 4 KiB leaf for every page of every type (37.9 MB for one copy at
18 GiB mapped). It now writes the sizes the design has.

- *The table's geometry is derived from `G`.* A radix table can end a walk early only at a level
  whose span is the page's size, as an Sv39 megapage is a leaf one level up. With nine index bits
  at every level those spans are 4 KiB, 2 MiB, 1 GiB…, and `G` is none of them (1 MiB on the
  DDR5 configuration, 256 KiB on HBM3). So level 0 gets `log2(G / 4 KiB)` index bits — 8 at
  `G` = 1 MiB — and every level above it keeps nine: level 1 spans exactly `G`, level 2 `512 G`.
  The entry format is RISC-V's, unchanged, and a level narrower than the others is not new (x86
  PAE's top level has four entries). A grain that is not a power of two has no level spanning it
  and keeps 4 KiB leaves; the table and every walker derive the geometry from the same `G`
  (`PageTableDesc::deriveFromGrain`, `src/nmfc/src/NMFCPageWalk.h`).
- *Which pages get one leaf.* A grain page is one leaf per grain. A duplicate page is one leaf in
  each tile's copy, naming that tile's replica. Undeclared memory, which is grain-partitioned at
  its own address, is one leaf per grain. A **striped page is `N` leaves, one per grain**, not one
  `N × G` leaf: the placement policy moves one grain of a striped page at a time, after which the
  page's grains are no longer one contiguous extent, and an `N × G` entry could not say so without
  being split — which is what an operating system does to a huge page when part of it migrates. The
  unit a striped page is translated in is the unit it is remapped in. A host page is a 4 KiB leaf,
  and so is every page of a grain that a region boundary cuts, because a leaf maps a naturally
  aligned block: a `G` leaf needs a region base on a grain boundary and no second region in the
  grain. A region that does not fill its last grain is still one leaf there when nothing else is
  declared in the grain, and the tail resolves through the region as the leaf maps it
  (`PageTable::leafShift`, `inLastLeaf`). The linker places code at 0x10000, which is not a grain
  boundary, so the duplicate region holding code and constants keeps 4 KiB leaves; aligning it is
  a layout change and is not made here.
- *The walk ends at the first leaf it reads* and returns the level, which is the page size. On
  both walkers (each tile's, and the host's at its cache management units) a leaf read at level 1
  ends the walk; `walkLeafSizeMismatch` (tiles) and `walk_leaf_size_mismatch` (host) count a walk
  that ended at a different size from the one the table maps the page with, and both are zero
  gates.
- *A remap rewrites one leaf.* A moved grain of a striped page is one entry in each copy, so the
  tile's privileged page-table write is one eight-byte write where it was 256
  (`ptRewrites` = `remapsApplied` in the directed test below).

**The translation buffers hold both sizes in one array, each entry tagged with its own.** Real
MMUs do this one of two ways: split arrays per size, as x86's first-level data TLBs are, or one
array whose entries carry a page-size field that masks the compare. The tile and the host follow
the second, after Arm's Neoverse V2, whose first-level data TLB is one fully associative 48-entry
array caching 4 KB, 16 KB, 64 KB, 2 MB and 512 MB mappings side by side, and whose 2,048-entry
8-way second level holds every block size up to 1 GB (Arm Neoverse V2 Core Technical Reference
Manual, 102375 issue 03, §6.1, Table 6-1). One array rather than one per size because a split
fixes the ratio of small to large entries at design time, and here the ratio moves with the
program: a tile translates `G` pages for data and 4 KiB pages for the code at 0x10000 and for
host data. A lookup does not know the page's size, so it compares under each size the table has.

- *The tile's buffer* keeps its configured 64 entries (`tlbEntries`) and becomes fully
  associative with least-recently-used replacement (`MixedSizeTLB`). It was direct-mapped under a
  hash of a key the page table computed from the region list — the size of the page was known
  before the lookup, which no hardware can know, and each entry was keyed at a grain although the
  walk that filled it had read a 4 KiB leaf. Every hit is now checked against the table
  (`xlatTlbStaleHits`, a zero gate), and a remap's shootdown removes every entry overlapping the
  moved grain and drops any walker output latch that holds its old frame.
- *The host's* two levels keep their sizes (48 entries fully associative, 2,048 entries 8-way) and
  tag each entry with its size; in the set-associative second level each size indexes the sets with
  its own page number, and both probes of a lookup are one lookup charged once. The host MMUs now
  shoot down a moved grain's entries (`tlb_shootdowns`); before, they kept them, which cost no
  correctness because the host's frames come from the table, but made a moved grain free to
  translate.

**Tested contended first.** The unit test `unit/leaves.cc` (in `run_nmfc.sh`) puts a host 4 KiB
page and a grain page in one set of the set-associative array and one fully associative array,
walks every tile's copy page by page against the table (frame and size), and moves one grain of a
striped page and checks that exactly one eight-byte entry of each copy changed. The coherent suite
runs the machine's cases with a one-entry tile buffer, so that a walk is almost always in flight:
a tile walking a grain's leaf while the grain is remapped under it with the host writing the grain
throughout (`tile_nuca_move`, two and four tiles: 79 and 78 walks raced a remap, no leaf mismatch,
the answer exact), and four tiles walking the leaves of one striped page at once beside host pages
(`tile_pages`, four tiles: every tile ended walks at `G` leaves, and both sizes were hit on the
tiles and on the host). `xlatTlbStaleHits`, `walkLeafSizeMismatch` and `walk_leaf_size_mismatch`
are in the suite's zero-gate register.

**What it changes, measured.** Each arm ran the same binary and image with one library changed.

| point | measure | before | after |
|---|---|---:|---:|
| compiled reduction, `FORK.M`, 4 MiB, uninterrupted | tile walk reads per 1,000 tile instructions | 37.8 | 0.010 |
| same | tile walks / translation-buffer hits | 443,955 / 12,961,877 | 98 / 13,442,503 |
| same | host walks | 16,483 | 42 |
| same | fork to first issue (of which: wait for a window slot), cycles | 1,060.0 (957.2) | 6,487.6 (6,380.2) |
| same | simulated time | 1.12636 ms | 1.12421 ms |
| compiled reduction, `FORK.R`, 4 MiB, uninterrupted | simulated time | 1.10359 ms | 1.06702 ms (−3.3 %) |
| graph search, 16 MiB, level-7 window | host cycles | 1,584,221 | 1,576,599 (−0.48 %) |
| same | tile walk reads per 1,000 tile instructions | 75.1 | 0.002 |
| chained hash table, smallest point, whole run | simulated time | 453.0 µs | 445.5 µs (−1.7 %) |
| same | tile walks | 145,306 | 20 |
| shuffled sum, suite size, 4 tiles | host cycles | 478,999 | 451,308 (−5.8 %) |
| same | tile walks | 112,575 | 14 |
| graph search, suite size, 4 tiles | host cycles | 1,642,444 | 1,641,573 |
| same | host walks | 95 | 6 |

Every answer is unchanged. Nearly all of the walks removed are not 4 KiB leaves being walked
where a grain leaf would do: the old buffer already held one entry per grain. They were conflict
misses of a direct-mapped array. The reduction's table is 40 grain pages, and every tile
translates all of them because a tile translates before it migrates; in the after arm the fully
associative 64 entries never evicted anything (`xlatTlbEvictions` = 0), so everything the old
array walked for beyond a first touch was a collision in its hash.
The grain leaves are what make that array honest (an entry covers a grain because the walk read
a grain leaf), make the table small and make a remap one write.

**What got slower, and why.** The `FORK.M` start went from about 1,060 cycles to about 6,490,
while the program ran 0.2 % faster. The start is almost all the context read's wait for a slot in
the delivery window, and the counters say the walk reads were never what filled it: the window is
full in 72 % of all tile cycles in both arms (`xcWindowFullCycles` 3,243,450 and 3,219,912 of
about 4.5 million), and each cycle it is refilled
first from the translation path and only then from the queue of physically addressed requests
(walk reads, page-table writes, context transfers). With the walks gone, the contexts that used to
be parked on them issue loads instead, the translation path fills every slot the window frees, and
the context read — the only request left in the physically addressed queue — waits longer. That
is a fixed priority starving one class, and the change that ends it is the other one named in §2.1:
an entitlement for a context transfer to a window slot, as the walk path and the close of an
atomic pair are already entitled. It is made in §2.4: the physically addressed class has a window
slot of its own, and the start is 107.9 cycles.

### 2.4 The cross-connection: one window, oldest-per-bank

Translation queues are indexed by virtual address; memory queues are indexed by physical
address; the mapping between them is the page table, which is arbitrary and changes at page
granularity. Written as a connection, any source may on any access need any destination.
That is the one genuinely hard part of the tile, and the accepted answer is not a crossbar.

The observation that shrinks it: **the width of the connection is set by the rate at which
physical addresses are produced, not by the number of places they can come from.** A
context has at most one memory access outstanding and a pipe starts at most one memory
instruction per cycle, so the number of requests needing delivery in a cycle is bounded by
the translation completion rate — a small number — whether the engine holds 256 contexts or
1024. The all-to-all is between *completions* and banks.

Four mechanisms were compared. `P` is the number of requests that may complete translation
in a cycle, `B` the number of banks and therefore of memory queues, `W` a staging width.

| mechanism | deliveries per cycle | latency added | several requests want one queue in one cycle | state per context | growth |
|---|---|---|---|---|---|
| crossbar with per-crosspoint buffers | min(P, B) | 1 cycle | each bank takes one, the rest wait in their own buffer | none | `P × B` buffers — a product, as state |
| one FIFO per pipe with an arbiter per bank | min(P, B) in theory, about 0.59 of it under uniform traffic | 1 cycle | one wins; **everything behind the losers also waits** | none, unless order forces a per-context binding | `P` FIFOs, `P × B` request bits |
| a ring, one stop per queue | ring-limited | up to `B − 1` cycles, `B/2` on average | serialised by ring position, no wide arbiter | none | one stop and one cycle per bank |
| **one window, oldest-per-bank drain** | min(W, B) | 1 cycle | the oldest goes; the others **stay in the window** and go next cycle | none | `W` entries, a `W × B` decode |

**The accepted mechanism is the last one.** Completions enter a single window of `W`
entries, default the pipe width. Each cycle the window decodes the bank bits of every entry
it holds and, for each bank that has a free queue entry, delivers **the oldest window entry
naming that bank** — at most one delivery per bank per cycle. Four properties decide it.

It is one structure, which matters because the mechanism it replaces failed by having
several structures that had to agree. It preserves order per destination for free, because
oldest-first per bank means two accesses to one address cannot invert, and that is what lets
ordering, forwarding and atomicity be local properties of one queue in §2.5; reordering
*across* banks is invisible, because different banks are disjoint addresses. Its drain is
deliberately **not** in order: a strict FIFO drain of one queue into many banks delivers one
request per cycle and blocks on every conflict, and allowing the window to skip a blocked
entry costs exactly the decode that is already there. And its cost contains no product of
two growing numbers *as state* — the `W × B` term is wiring and logic inside one cycle,
which at `W` of 4 to 16 and `B` of 8 to 16 is an arbiter, not a network.

Its capacity is arithmetic rather than hope. With `k` requests in the window and uniformly
distributed destinations, the expected number of distinct banks among them is
`B(1 − (1 − 1/B)^k)`:

| banks `B` | k = 1 | k = 2 | k = 4 | k = 8 |
|---|---|---|---|---|
| 4 | 1.00 | 1.75 | 2.73 | 3.60 |
| 8 | 1.00 | 1.88 | 3.31 | 5.25 |
| 16 | 1.00 | 1.94 | 3.64 | 6.45 |

Read against the offered load — pipes times the fraction of instructions that touch memory,
which is 11 % of the static instructions in the two graph kernels and 24 % in the mixed
image — a window of two over eight banks already exceeds the plausible demand at four pipes
and a window of four is comfortable, and the rule of thumb is that banks should be at least
twice the window depth.

The escalation path, if measurement says the window is the constraint, is the same structure
replicated rather than a new one: first widen the window, which is configuration; then split
it by destination group, `G` windows each owning `B/G` queues with the group chosen by
high-order bank bits, each group proposing at most one entry per bank and the bank taking
one of the `G` proposals. Order per destination survives, because a destination belongs to
exactly one group. Widening the window continuously toward `B` walks the design from this
mechanism to the crossbar with every point correct, so the value of a crossbar is measured
rather than assumed.

One counter decides whether the connection ever limits the machine and the others explain
it: cycles in which a request was ready, its destination had room, and it was nevertheless
not delivered. If that is zero the connection is not the constraint and nothing else about
it needs defending. Beside it: window occupancy and full cycles; deliveries per cycle as a
histogram; entries that lost to an older entry for the same bank; entries that were oldest
but had no credit; delivery latency, mean and maximum; per-bank delivery counts, whose
spread is the imbalance; and the longest any entry waited while younger entries went, which
is the check that oldest-per-bank starves nothing. Two of those separate "the connection
limited the machine" from "the banks did", which is the distinction that decides whether
widening anything is worth doing.

**The window is built, and that first counter had to be written a second time before it meant
anything.** `deliveryLimitedCycles` was originally computed from a predicate asking whether an
entry sitting *beyond* the window would have been delivered by a wider one. The window refuses
an entry once its occupancy reaches its width, so nothing is ever beyond it, and the predicate
was false on every cycle of every run ever made — from which an earlier record concluded that
the cross-connection was never the constraint, a conclusion resting on a counter that could
not fire. The structure built is one arbitration window whose capacity *is* its width; a
completion that cannot enter waits in its translation queue instead. The counter now asks what
this structure can answer: the window was full at the end of the cycle and the translation
path was holding a completion whose latency had been paid. Nothing about the machine's timing
changed, and the reading is now a measurement rather than a tautology. It is zero on the two
sampled points taken, which says the connection did not limit them.

**Two classes fill the window, and the second has a slot of its own.** Requests reach the window
from two places. Every context's load, store and atomic comes from the translation path. Page-walk
reads, the page-table rewrite after a remap, and the 512-bit context read of an arriving `FORK.M` or
`CONT.M` invocation already carry a physical address, so they come from the tile's in-order queue
of physically addressed requests. Until this change the tile refilled the window from the
translation path first and from that queue only with what was left. That is a fixed priority, and
under a window that the running contexts keep full, the lower class waits as long as the higher one
has traffic.

The counters that show it, on the compiled reduction at 4 MiB (four tiles, 128 contexts, run whole,
`FORK.M` with cleaned blocks), with the fixed priority:

| quantity | value | counter |
|---|---:|---|
| cycles a context read spent waiting for a window slot, summed over its 5,120 reads | 32,661,510 (6,379 per read) | `ctxReadPathCycles` |
| of which at the head of that queue, the window having no slot it could take | 2,652,063 | `ctxReadWaitWindow` |
| the window's contents on those cycles: translated entries / physically addressed entries | 3.98 / 0.015 per cycle | `ctxReadWaitWinXlat`, `ctxReadWaitWinPhys` |
| of which behind another context read at the head of the queue / behind a walk read | 29,710,567 / 298,880 | `ctxReadWaitBehindCtx`, `ctxReadWaitBehindWalk` |
| slots the translation path filled while a physically addressed request waited before and after the refill | 865,312 | `xcXlatFillsWhilePhysWaited` |

So the window was full of translated requests when a context read reached the head of its queue
(3.98 of 4 slots), the read waited there about 518 cycles, and the reads that arrived meanwhile
queued behind it. Walk reads were not what the reads waited behind.

Two remedies were considered. A round-robin between the classes gives each the first pick on
alternate cycles. It bounds the wait while slots are being freed, but when the window is full of
entries waiting for memory-queue credit neither class can enter, and the physically addressed class
still has no unit it can always reach. The arbitration built is the one link layers use to keep one
traffic class from starving another: **a dedicated credit per class beside a shared pool.** Intel
QuickPath keeps buffers per message class in its VN0 virtual network beside the shared VNA pool
("An Introduction to the Intel QuickPath Interconnect", Intel, 2009), and PCI Express keeps
separate flow-control credits for posted, non-posted and completion traffic in every virtual channel
(PCI Express Base Specification 3.0, "Virtual Channel (VC) Mechanism" and "Ordering and Receive
Buffer Flow Control"). Here:

- The physically addressed class has `xcPhysSlots` slots beyond the configured width (default 1),
  which only it may take. It uses them first and then competes for the width behind the translated
  class.
- The translated class keeps the whole configured width and cannot take the class's slots, so a
  program that issues no physically addressed request sees exactly the window it had.
- Delivery is unchanged: oldest entry per bank, one per bank per cycle, credit from the queue. A
  context read in its slot still yields its bank to an older load for the same bank.
- `xcPhysSlots=0` is the fixed-priority window as it was. The statistics files of runs at 0 are the
  earlier machine's, cycle for cycle, apart from the new counters.
- The slot is counted in the reservation floor (§2.5).

Before and after, in cycles of the 1 GHz tile:

| point | measure | fixed priority | the class's own slot |
|---|---|---:|---:|
| compiled reduction, `FORK.M` (cleaned), 4 MiB, 4 tiles, 128 contexts, run whole | fork to first issue, mean | 6,487.6 | **107.9** |
| same | of which the wait for a window slot | 6,380.2 | **1.0** |
| same | simulated time | 1.12421 ms | 1.09808 ms (−2.3 %) |
| same | window full, cycles | 3,219,912 | 3,044,438 |
| compiled reduction, `FORK.R`, 4 MiB | simulated time, statistics | 1.4555 ms | identical apart from the new counters |
| graph search, 16 MiB, level-7 window (image 6) | host cycles | 7,266,440 | 7,266,440; one cycle less of a full window per tile, nothing else |
| chained hash table, smallest point, 4 tiles | simulated time, statistics | 482.619 µs | identical apart from the new counters |

Every answer is unchanged. The contended directed test, `tile_ctxread`, runs 96 invocations
streaming loads on one tile while rounds of 16 invocations are started from 64-byte blocks. The
variants and their results (mean fork to first issue, maximum in brackets):

| variant | fixed priority | the class's own slot |
|---|---:|---:|
| host starts with `FORK.M` against the load storm | 195.3 (1,613) | 126.1 (274) |
| half started by the host, half by the tile's own `CONT.M` | 209.4 (2,821) | 114.1 (232) |
| all started by `CONT.M` on the tile (tile against tile): the read's wait in the queue | 65.8 per read | 1.2 per read |
| host `FORK.M` with a one-entry translation buffer (walk reads against context reads) | 24,292 (134,197) | 158.0 (381) |
| the same, at the smallest stages (window 1, translation queue 1, memory queue 3) | 485.3 (1,374) | 31.8 (55) |
| no load storm (the control) | 22.4 (100) | 22.4 (100) |

The load storm's own progress is unchanged: the program's simulated time moves by at most 0.02 %,
except where the walk reads themselves had been starved. With the one-entry translation buffer the
fixed priority also held back the walk path: up to 64,551 walk reads were waiting in the physically
addressed queue at once, the program took 880.2 µs, and with the class's slot it takes 680.1 µs
with at most 61 walk reads waiting. That the walk path can queue so many reads is itself not
bounded by anything in the machine, and is noted in §3 as open.

### 2.5 The memory queues: one per bank, and the only ordering point

There are as many memory queues as data-cache banks, and queue `b` owns exactly the
addresses bank `b` holds. The bank index is a decode of physical address bits — the same
function the cache already uses — so **two accesses to the same address are always in the
same queue**, and a queue assigns an increasing sequence number to every request it admits.
That sequence *is* the tile's order for those addresses. There is no second party to agree
with and no protocol to run. Everything else in this section is a consequence.

**Issue condition.** An entry may go to the bank when no older entry in the same queue
overlaps its byte range. The check is a comparison against at most the queue's depth of
older entries, local to one queue. Entries to disjoint addresses proceed in any order, so a
hot word does not stall a queue.

**Store-to-load forwarding is a comparator across one short queue.** A load whose bytes are
fully covered by the newest older store or completed atomic in the same queue is answered
from that entry and never touches the bank. A partially covered load is not forwarded: it
waits and reads the array. Both are counted, and whether byte-merging logic is worth
building is decided by how large the partial count turns out to be.

**A load-reserved / store-conditional pair is a serialisation point in the queue, and nothing else.** The load-reserved opens it and the store-conditional closes it; while it is open, every other entry to that address waits behind it in the same queue, so nothing intervenes and the store-conditional completes. There is no reservation unit and no reservation table: the queue's order *is* the reservation.

That is the whole implementation, and three rules make it work; each of the three was written
after a defect that stopped a program, and all three are now structure rather than policy.

**A point has exactly four ends: its own store-conditional, the line being taken away, the
owning context's next memory operation that is not that store-conditional, and the departure of
the context that opened it.** None needs a timer.

*The owner's next memory operation.* In RISC-V a reservation is a passive monitor: it blocks
nobody, and a program may branch away after a load-reserved without ever issuing the
store-conditional. The ordinary compare-and-swap loop does exactly that when its compare fails.
The unprivileged specification's LR/SC section expects the store-conditional to be the next
memory operation after the load-reserved (a constrained LR/SC loop holds no other load, store or
atomic), and it lets an intervening memory operation invalidate the reservation. This machine's
point is not passive, since every other access to the word waits behind it. Left open after the
program has moved on, it turned a legal program into a hold on everyone else. It also held the
context itself, because that context's next access to the word waited behind its own point. So
the point closes at the owner's next memory operation that is not the store-conditional to its
address. This covers a load, a store, a read-modify-write, a load-reserved of any word (the same
one again included), and a store-conditional to another address. A context that reads, compares
and branches away therefore blocks others only until its next memory access. A `WAIT` does not
close the point. It issues no request, and a context cannot sleep on it with a point open: the
load-reserved clears the load slot's arm, so a `WAIT` after it completes at once (the zero gate
`waitSleptWithOpenPoint`), and any operation that could arm it again is a load or an atomic, which
does close the point. The rule is
applied where every memory operation of a context is issued (`closePointsOnNextOp` in
`src/nmfc/src/NMFCTile.cc`) and counted in `memqLrscPointsClosedByNextOp`. The close is recognised
by the load-reserved's virtual address, which the point records. A context has at most one memory
operation anywhere in the machine, so its load-reserved has been performed, and its point opened,
before its next operation can issue.

*The one case this leaves.* A context that never touches memory again after its load-reserved and
never leaves the tile holds its point. Such a context computes only on its own 512 bits, and
nothing another agent does can change what it computes. So either it reaches a memory operation or
its end within a number of instructions fixed by its own registers, which is a bounded hold like
any long pair, or it never terminates. In that case the invocation never returns, and the host
waiting for it would wait for ever on any machine. A `KILL` ends it, and departure closes the
point. This is not a hazard the machine must answer.

*Departure.* A context leaves a tile by retiring, by migrating or by being killed. All three run
through one release sequence that moves the slot's generation token on, and a point owned by a
token that has moved is owned by nobody. It is therefore closed at the instant of departure, every
entry waiting behind it proceeds in the same cycle, and the close is counted. A lost claim that
was its invocation's last memory operation ends this way.

*The timer.* The timer that used to close abandoned points defaulted to the snoop-deferral bound,
queue depth times bank access latency. That bound is derived for a different question and is
shorter than a pair's own interval, so it was closing pairs that were about to succeed: six of
them in 260 on the one program that then performed the pair. It remains as a labelled safety
configuration, **off by default**, and `memqLrscPointsTimedOut` is a zero gate in both suites.

*Before and after.* The contended test is `tile_lrsc_away` (`src/nmfc/test/tile_lrsc_away.c`,
kernels in `nmfc_away.S`). It has three parts:

- phase 1: eight invocations make sixteen increments each with a compare-and-swap loop whose
  expected value is a plain load, re-reading the word after each failed compare;
- phase 2: the eight invocations claim a 64-slot array from staggered starts, abandoning every
  slot they find taken;
- phase 3: phase 1's loop while the host stores to another word of the same line.

The `_race` build adds the host against the tile:

- phase 4: the host's `amoadd` on the word;
- phase 5: the host's own abandoning compare-and-swap loop on the word;
- phase 6: the host claiming slots of the array with the same loop.

Every phase is exact arithmetic. The runs below are uninterrupted correctness runs. "Floor" is a
memory queue of 3 entries, a delivery window of 1 slot and a translation queue of 1 entry. "Asks"
and "tells" are the two ways the tile's data cache reports a line it is losing: asking first, or
telling afterwards. "Stop" means a run that did not complete within its wall-clock deadline.

| Program | Configuration | Before (the point outlives the pair) | After |
|---|---|---|---|
| `tile_lrsc_away` phase 1, 2 or 3 alone | one tile, default stages, asks | **stop** (each) | pass |
| `tile_lrsc_away`, phases 1–3 | 1, 2 and 4 tiles, default or floor, asks or tells; the flat configuration at default and floor | stop in phase 1 | **pass in all 14** |
| `tile_lrsc_away_race`, phases 1–6 | 1, 2 and 4 tiles, default stages, asks | stop in phase 1 | **pass** |
| the compiled graph search, the claim as a plain compare-and-branch, every phase on the tiles | 4,096 vertices, four tiles | **stop** (no completion in 600 s) | pass: the same depth digest and level sizes; 2,628 points: 2,582 closed by their own store-conditional, 42 by the claimer's next memory operation, 4 by departure |
| the compiled graph search, 512 claimers of one vertex | the same | — | pass: the vertex settled once, 0 retries; 526 points: 513 closed by their own store-conditional, 13 by the next operation |
| `tile_lrsc`, `tile_lrsc_race` (every pair closed by its own store-conditional) | one tile, default, asks | pass | pass, with **byte-identical statistics files** apart from the two new counters (the next-operation count reads 0) |

At 65,536 vertices the compiled graph search, with the plain claim, gives the same depth digest
and level sizes as before. Its top-down kernels make 160,840 migrations in 9,347,975 instructions,
0.0172 per instruction, against 160,827 in 9,346,984 with the write-back. Its bottom-up kernels
make 5,462 in 1,546,528, which is unchanged. 58,747 points: 58,604 closed by their own
store-conditional, 143 by the next operation, none by a timer. The compiler's claim no longer
stores back the value it read on a failed compare (`tools/nmfcc/nmfcc/gkern.py`). That
store-conditional existed only to close the point, and the machine now closes it.

**While a point is open, one unit of each stage on the path to its queue is held for the
close.** Three stages lie between a context and the memory queue that owns its address — its
translation queue, the delivery window, and the queue itself — and while a point is open every
request to that address crossing any of them is a request the point will hold. A stage filled
entirely with such requests is a stage the closing store-conditional cannot enter, and each of
the three could be filled that way: the program that performs the pair completed at a queue
depth of 16 and did not complete at 8, 4 or 2, nor at a window of one, and at the default
depth twenty-eight contending invocations were enough. So one entry of the translation queue,
one slot of the delivery window and one entry of the memory queue are reserved for the close
while a point is open — the same reservation the walk path already has, and for the same
reason — and a store-conditional may leave its translation queue out of turn, since a
translation queue is indexed by the page and therefore holds every contender for one word in
arrival order. Every part of it is off when no point is open, so a program that performs no
pair sees each stage at its full configured size and is byte-identical. Nothing is drained and
nothing is serialised: the order accesses to an address are performed in remains the memory
queue's, assigned on admission there.

*Before and after.* The rule was built without one, so the machine was given a name for it —
`pointReserve`, default unchanged — and seven unit checks proving the name selects the older stage
(commit `f34d150`). Paired at the configured depths on the directed two-agent pair test, the two
arms produce **byte-identical statistics files**: 51,369 and 51,531 host cycles at one tile and at
four, 26,519 tile cycles standalone, with 507 points opened and 384 closed by their own
store-conditional. At the configuration everything here is measured under, the rule costs exactly
zero. At a memory-queue depth of 8 it is the difference between finishing and not: with the rule
the test passes in 26,635 cycles, and without it no invocation ever returns and the run does not
end. Depth 2 fails either way and is now refused at construction (below).

**The reserved unit is a unit of its own: an escape slot at the window and the translation queue, and a floor at the memory queue.** A reservation is only a reservation when the stage keeps a unit for everything else and the entitled request can always reach its unit. The first version carved the close's unit out of each stage's configured size once a point opened, and that fails twice. At a window of one slot the refusal `occupancy + 1 ≥ width` is true of an *empty* window, so while any point was open the window admitted nothing but the close — a closure rather than a reservation. And at any size the unit is carved out too late: a point opens when its load-reserved reaches the bank, and by then the window and the translation queue can already be full of contenders for the same word, every one of which the point then holds. Eight contenders on a tile alone, at the default window of 4 and a memory-queue depth of 3 or 4, stopped exactly so: four contenders in the window, the close in its translation queue with nowhere to go. So at the window and the translation queue the close's unit is now an **escape unit beyond the configured size**, as an escape channel in deadlock-free routing is a buffer of its own rather than one taken from the shared pool (Duato, *IEEE TPDS* 4(12), 1993): ordinary traffic keeps the whole configured width or depth whether or not a point is open, and while one is open the close (and, at the window, the walk path's traffic, which a point never holds) may take one unit more. With no point open the unit does not exist, so a program that performs no pair runs on exactly the configured stage. The memory queue keeps its reservation inside its depth, because it does not have the hole: the load-reserved that opens its point holds one of its general entries and frees it on completion, and from then on the rule keeps that entry for the close. Its floor is therefore still its reserved units plus one, **walkReserve + 2** — 3 with walks through the data cache, 2 with walks sent to the slice — and a smaller queue is refused at construction with the limit named. The window's and the translation queue's floor is 1. The window's slot for the physically addressed class (§2.4) is counted beside it and does not change it: it is beyond the width like the escape slot, and only that class may take it, so at a width of 1 with a point open the window holds one ordinary entry, the close and one walk read or context transfer, and no class can take another's unit. A memory queue of 2 with a walk reservation fails either way: its one general entry is either the close's (nothing else enters) or anybody's (a held request can take it). The window's default stays max(pipes, 2), so a one-pipe tile's configuration does not change.

**What the floor proves is deadlock-freedom, and only that.** At or above it the close always
finds a memory-queue entry, because the only traffic that can take the reserved unit first is a
walk, which no point holds and which completes; the window's escape slot can be taken only by
closes and walk traffic, both of which are always delivered, and a close holding an entry may pass
an older request for the same bank that has none; the translation queue's escape entry can be
taken only by a store-conditional from the context that owns an open point (the point records its
owner's slot and generation), so one whose own point was already broken is ordinary traffic there
and cannot hold the close's entry — it could before, and twenty-eight contenders on a tile alone
reached it; and a store-conditional may leave its translation queue ahead of requests the point
holds. Starvation is not excluded by the floor. Between the window's two classes it is excluded by
the physically addressed class's own slot (§2.4); among one class's requests it is not. The machine
as configured sits well above the floor: 16 entries per memory queue (`dataQueueDepth`), 8 per
translation queue (`xlatQueueDepth`), both defaults in `src/nmfc/src/NMFCTile.h`, and a window of
max(pipes, 2). On the eight-contender pair test (`tile_lrsc`, 8 invocations × 16 increments, run
whole) at that configuration the escape units change nothing: 41,175 host cycles on the full
machine, 41,833 on two tiles and 26,481 tile cycles on a tile alone in both builds. At the smallest
stages now admitted (window 1, translation queue 1, memory queue 3) the test passes in 44,761 host
cycles on the full machine and 26,796 tile cycles alone; at window 2 and translation queue 2, in
47,433 and 26,792.

**The walk path keeps its reservation while a point is open.** The memory queue's half of the rule
used to refuse every request but the close once its general capacity was one short — walk reads
included, although they are admitted against entries of their own and a point never holds one. At a
depth of 3 on the full machine that refused the walk the point's own context needed before it could
reach its close, and the two-contender test stopped with no increment made; exempting the walk
path's traffic, as the window already did, lets it finish in 24,252 host cycles against 24,246 with
the rule off. The same correction turns the eight-contender test at a queue depth of 8 on the full
machine from a failure into a pass with the rule on (it still fails with the rule off).

**The close is not stranded behind the requests its point holds.** Eight contenders on the full machine, with walks through the data cache, stopped at a memory-queue depth of 3 with the rule on. The stopped state: the four walk slots (`walkSlots`, one per translation queue, four at four pipes) each held a request whose walk had finished — three contenders' load-reserveds and the point owner's store-conditional, which had missed the translation lookaside buffer like the others; the window held three contenders, all refused credit by the point, and its remaining slot was the close's. The finished walks are drained into the window each cycle, and the drain stopped at the first request the window refused. The first was a contender, refused because the free slot was the close's; the close, third in the array, was never offered the slot kept for it. The drain now passes over a request the window refuses and offers the next, as the translation queues' drain already did; each entry is a different context's operation, so nothing that has an order is reordered. The walk path's own queue into the window follows the same rule: a request refused for the close's slot no longer stops the walk reads behind it. The earlier reading that four contexts were waiting at the data-cache bank was an instrument defect: a walk read reaching the bank marked the context it walks for as waiting at the bank, although that context's operation was still in the walk-pending array. The census now moves a context only on its own operation.

Before and after, on `tile_lrsc` (eight invocations × sixteen increments on one word, the host writing the same line in phase 5, run whole; host cycles on the full machine and on two tiles, tile cycles on a tile alone; window 4, translation queue 8, walks through the data cache; one binary per build with only `pointReserve` changed; a run not complete at 1 ms of simulated time, about seventy times the passing time, is a stop):

| memory-queue depth | machine | before, rule on | before, rule off | after, rule on | after, rule off |
|---|---|---|---|---|---|
| 3 | full machine | **stop** | stop | **40,593** | stop |
| 3 | tile alone | **stop** | stop | **26,788** | stop |
| 3 | two tiles | **stop** | stop | **42,025** | stop |
| 4 | full machine | 40,013 | stop | 40,959 | stop |
| 4 | tile alone | **stop** | stop | **26,744** | stop |
| 4 | two tiles | **stop** | stop | **41,500** | stop |
| 8 | full machine | 40,983 | 40,983 | 40,983 | 40,983 |
| 8 | tile alone | 26,597 | stop | 26,597 | stop |
| 12 | full machine | 41,091 | 41,091 | 41,091 | 41,091 |
| 12 | tile alone | 26,481 | 26,481 | 26,481 | 26,481 |

With the rule off every stop is the wedge the rule exists to prevent: the window full of contenders the point holds, the close in its translation queue. Where both builds complete, the statistics files differ only in the corrected census counters and in the window-full count, which now includes the escape slot. At depth 4 on the full machine the rule-on run ends 946 host cycles (2.4 %) later. A build without the escape units reproduces the old 40,013 exactly, so the difference comes from the escape slot: it lets one more ordinary request into the window while a point is open, and that changes which contender wins the word next. At depth 3 the same change is 1,117 cycles faster than that build.

**A load-reserved for another address may not take over an open point.** A queue holds one
point at a time; a load-reserved arriving for a different address in the same queue used to
take it, and two pairs on two words of one queue could then stop each other for ever with
every step legal — and, worse than stopping, produce wrong answers. One stress seed opened
10,668,525 points and closed 256. Such a load-reserved now waits, and the same seed closes
256 of 256 in about a second.

**The pair among many contenders of one tile.** It used to stop at 28 and 32 contenders on one
word at the default configuration, and that was not a fairness limit. On the full machine it was
the walk-pending drain above: a build with only that repair passes 28. On a tile alone it was a
store-conditional whose point had been broken holding the translation queue's reserved entry: a
build with every repair except the owner test at translation still stops at 28. With both
repairs, 28 contenders complete in 118,052 host cycles on the full machine and 50,862 tile cycles
alone, and 32 in 132,010 and 55,762. Among one tile's contexts the queue's order decides who
goes next; the contended tests below measure the attempts that takes (at most two for 32
contenders with no other agent) rather than bound them with a counter.

**The owner's pair is served before another agent's request for its line.** A pair completes
against another agent that keeps writing its line only if the line stays with the pair for the
pair's own window once the load-reserved has been answered, and the pair's store-conditional is
performed before the other agent's next request. Without that, a host writing a line without
pause kept a tile's claim on it from ever succeeding, and a host's pair and a tile's could take
the word from each other for ever. Real cores give their reservation exactly this. RISC-V
guarantees eventual success only to a constrained LR/SC loop: at most 16 instructions, only
base-ISA integer instructions between the halves, and no loads, stores, backward branches,
`FENCE` or `SYSTEM` instructions (unprivileged ISA, "Eventual Success of Store-Conditional
Instructions"). Implementations meet that guarantee by holding the reserved line against
incoming probes for a bounded number of cycles: Rocket's L1 data cache holds it for
`lrscCycles` = 80 cycles, sized for "14 mispredicted branches + slop". An Arm core's exclusive
monitor is backed by the same bounded hold on the line. x86 holds the line for the whole of a
locked read-modify-write.

*What the machine did, read from counters.* An instrumented build of the previous machine, with
no change of behaviour, ran the contended tests. The worst was a function core incrementing a
word by a constrained pair while the host stored to another word of the same line with no pause
between stores (`tile_lrsc_writer`, below; one tile, a cache that asks first):

- In 2 ms of simulated time, 50,762 points opened. 526 closed by their own store-conditional and
  50,253 were broken by the host's request for the line, and the run did not complete.
- The host's request won against the open point before the pair's store-conditional existed.
  49,751 of the breaks came before the store-conditional reached the queue: the tile deferred a
  request only while one of its writes was queued against the line, and an open point is not a
  write.
- The pair's window was about 30 cycles. The load-reserved's answer reached its context 14
  cycles after the point opened, and the store-conditional reached the queue 16 cycles after
  that (`memqLrscLrAnswerCycles`, `memqLrscPairWindowCycles`).
- The tile's deferral was `snoopDeferLimit`, queue depth × bank access latency: 32 cycles at the
  default, 6 at a queue of 3. It expired every time it applied (502 of 502).
- It could not have done otherwise. While the tile deferred, the data cache held back the tile's
  own requests for that line, the write the deferral was waiting for among them
  (`snoopWindowWriteDeferrals`, 10,603). A deferral that holds the owner's write back until it
  gives the line up cannot help the owner.
- The host's request completed 3.3 cycles (at the 2 GHz fabric) after it reached the tile's
  cache (`hostSnoopTileCycles`).

The same shape stopped the host's `amoadd` against the tile's pair at two tiles and the floor
stages: 53,840 of 53,842 points broken before their store-conditional arrived, and 6 of 60,563
host store-conditionals succeeding. It also stopped `WAIT`'s test T12 with a cache that tells
afterwards: 42,973 of 42,988 points broken, and 1 of 64,459 host store-conditionals succeeding.
In the claim array at the floor, a store-conditional passed its check at the bank, the deferral
expired, the host's pair took the line and succeeded, and the tile's write then re-acquired the
line and landed. One slot was claimed twice (`memqLrscScLandedAfterBreak` 1).

*What is built.* Six rules, each at the place the behaviour belongs:

1. **An answered point holds a request for its line for the pair's window.** The window is
   derived from the tile's parameters (`lrscHoldCycles`, `src/nmfc/src/NMFCTile.cc`), term by
   term:
   - (14 + 1) × *issue interval*: the owner's instructions after the answer, the
     store-conditional included. 14 is the constrained loop's instructions between the halves,
     Rocket's figure. The issue interval is the pipe depth (one issue per context per `depth`
     cycles), or ceil(contexts / pipes) cycles when the context array oversubscribes the pipes,
     because the ready queue is first-in first-out.
   - `xlatLatency`: the store-conditional's translation. It may leave its translation queue out
     of turn and take that queue's escape entry.
   - 2: one cycle through the delivery window, on its escape slot, and one to its memory
     queue's head, where the point holds everything else to the word behind it.

   This gives 127 cycles at the default 32 contexts per tile (depth 8, four pipes, translation
   5) and 487 at 128. The longest window measured in any test is 84 cycles, in the compiled graph
   search at 128 contexts. The hold never outlasts the point: it ends at the store-conditional,
   at the owner's next memory operation, at its departure, or at the bound.
2. **The close is served first.** A point whose store-conditional has gone to the data cache
   holds the request until the write is answered. That is one cache access, because the write is
   a hit (rule 4).
3. **The other agent is served before the next pair.** A point opened after the request
   arrived does not hold it, as Rocket's reservation backs off after its hold. So a stream of
   pairs cannot keep the other agent waiting: it waits at most for the pairs already open when
   it arrived. A point whose load-reserved has not been answered does not hold it either,
   because that read may itself be waiting at the directory behind this very request.
4. **The load-reserved takes the line for ownership, and the store-conditional is a conditional
   write at the data cache.** The load-reserved, and a read-modify-write's read half, fetch the
   line in a state the cache may write, as the host's load-reserved already did. The
   store-conditional is performed only if the line is still in the cache, writable, when the
   write reaches it. Otherwise nothing is written and the pair fails (`condWritesFailed`, and
   `memqLrscScFailedAtCache` when it closes a point). The pair's check therefore holds until its
   write is performed, and `memqLrscScLandedAfterBreak` is now a zero gate.
5. **The data cache serves the tile's hits while asking the tile about the same line.** A
   function core keeps no copy of any line, and the answer to the request is built from the line
   after the tile has answered, so nothing is left above the cache. A miss is still held back,
   because it would need the fabric for the line the directory is waiting on
   (`snoopWindowClientHits`).
6. **A data cache that tells the tile afterwards holds the line itself.** Such a cache cannot
   ask, so it holds a line it has just answered a load-reserved from, for the window the
   load-reserved carries plus its own access latency and one cycle (`lrscHoldDeferrals`,
   `lrscHoldCycles`). The store-conditional's write, an eviction of the line or the end of the
   window ends the hold. The request that waited is then handled as if it had just arrived, and
   no new hold starts on a line while a request is waiting for it. SST's own stock L1 has the
   same mechanism (`llsc_block_cycles`, "Number of cycles to prevent competing access to an
   LL/LR line. Encourages forward progress").

None of the six is a timeout, a retry counter or a capacity fault. Each wait is bounded in cycles
by a named quantity, and nothing gives up (NMFC-Rev `eb3eb02`; the test and the suite gates
`2ac2f9f`).

*A defect in the host core the contended tests exposed.* With the tile's side repaired, one host
increment was lost in the host-against-tile phase at two tiles. The trace showed the order in
which the host's requests reached its cache: a load-reserved, then a store-conditional sent
before that load-reserved had been answered. Instrumenting the out-of-order core's load/store
queue showed why. The load-reserved had been sent to memory on a path the core then squashed.
The reservation it opened at the cache survived the squash, and the correct path's
store-conditional, arriving after it, was validated by a reservation its own load-reserved never
made. The tile's snoop had broken the real one in between. The core now sends a load-reserved,
or a locked load, to memory only when it is at the head of the reorder buffer, as stores already
are, and as BOOM performs its atomics and load-reserveds non-speculatively. Across three tile
counts the instrumented trace then shows no store-conditional sent while a load-reserved was
unanswered (0 of 340, 341 and 224, against 5 of 245 before at two tiles).

The same instrumentation found that the queue restarted its age count after a full pipeline
clear while retired stores were still in the store queue: 170 such clears in one run of the race
test and 225 in the dictionary at its smallest size. A load issued after the clear then looked
older than a retired store it follows, and neither waited for nor forwarded from it. The count
no longer restarts. Both changes are in `lsq/vbasiclsq.h` of the element tree (`875e7b274`).

The host's own reservation has no hold. Its pairs complete in every test because the tile's
pairs are bounded. A host that had to make progress against a tile claiming the same word
without end would need the same hold at the host's cache, which is Rocket's `lrscCycles` itself.
That is not built.

*Before and after.* Every run is an uninterrupted correctness run. "Before" is the machine at the
previous head, with the instrumentation above and no change of behaviour. The graph-search row
uses the core before its fix; the test program rows use either core, because their host issues no
pair. "Floor" is a memory queue of 3, a delivery window of 1 and a translation queue of 1.
"Asks" and "tells" are the two ways the tile's data cache reports a line it is losing: asking
first, or telling afterwards. "Stop" means no completion by the wall-clock deadline or within
2 ms of simulated time.

The continuous-writer test is `tile_lrsc_writer` (`src/nmfc/test/tile_lrsc_writer.c`, kernel in
`nmfc_writer.S`). It has four phases:

- phase 1: one claimer, making 16 increments of a word by a constrained pair, while the host
  stores to another word of the same line without pause until it returns;
- phase 2: 32 claimers on the same word, against the same writer;
- phase 3: the 32 claimers with the host idle (tile against tile);
- phase 4: one claimer while the host reads the claimed word and stores to its neighbour on
  every pass.

Each phase is exact arithmetic on both words and reports the most attempts any one increment
needed.

| Program | Configuration | Before | After |
|---|---|---|---|
| `tile_lrsc_writer` | 1 tile, default, asks | **stop** in phase 2 (50,253 points broken, 526 closed in 2 ms) | pass; most attempts per increment 1 / 8 / 2 / 1 by phase |
| `tile_lrsc_writer` | 1 tile, default, tells | pass; most attempts 6 / **45** / 2 / 6 | pass; 1 / 9 / 2 / 1 |
| `tile_lrsc_writer` | 1, 2 and 4 tiles × asks, tells × default, floor (12), and 128 contexts | 4 of 12 pass, needing 25 to 45 attempts for one increment in phase 2; **8 stop** (400 s) | **13/13 pass**; phases 1 and 4 need 1 attempt, phase 3 needs 2, phase 2 needs 5 to 11 |
| `tile_lrsc_away_race` phases 1–6 | 1, 2, 4 tiles × asks, tells × default, floor | passes only at the default stages with asks | **12/12 pass** |
| the same, phase 4 alone (host `amoadd`) | 2 tiles, floor, asks | **stop** (53,840 of 53,842 points broken; 6 of 60,563 host store-conditionals succeed) | pass |
| the same, phase 6 alone (host claims) | 1 tile, floor, asks | **fail**: a slot claimed twice (`memqLrscScLandedAfterBreak` 1) | pass, 0 |
| T12 (`WAIT` between the halves; the host adding) | 1 tile, tells | **stop** (42,973 of 42,988 points broken; 1 of 64,459 host store-conditionals succeeds) | pass; now gated at 1, 2 and 4 tiles |
| `tile_lrsc_race` | 1 tile, tells | stop (recorded as a known gap) | pass |
| the compiled graph search, the host read-modify-writing the claimed line on every probe with no cap | 4,096 vertices, 4 tiles, 128 contexts | **stop** (no completion in 900 s) | pass: 10,655 host read-modify-writes all kept; 533 points, 513 closed by their own store-conditional, 20 by the next operation, none broken |
| `memqLrscScLandedAfterBreak`, `memqLrscSpuriousSuccess`, `memqLrscPointsTimedOut` | every run above | 1 / 0 / 0 | **0 / 0 / 0** (all three zero gates) |

*The cost.* The host's latency to a line a tile holds is measured from the directory's request to
the tile's cache to the transaction's completion (`hostSnoopTileCycles`, cycles of the 2 GHz
fabric). The resident dictionary at load point 0 (8,192 insertions, four tiles, run whole) gives
the same answer before and after (answer digest `0x155f0b7b`):

| Dictionary, load point 0 | Before: mean / max | After: mean / max | Host work cycles, before → after |
|---|---|---|---|
| 32 contexts per tile | 3.46 / 36 (1,183 requests) | 3.19 / 38 (1,187) | 620,053 → 619,768 |
| 128 contexts per tile | 3.67 / 37 (1,409 requests) | 3.43 / 35 (1,395) | 691,766 → 692,466 |

The dictionary performs no pair on the tiles, so the hold never applies to it: 5 and 1 requests
waited for a pair. The small fall in the mean comes from rule 5, the tile's own hits being served
while it is asked. Where the tile does claim, the host pays up to one pair per write, as intended:
in the continuous-writer test at one tile, 30.7 cycles on average and at most 38, against 3.3
before, while the claims that previously never completed now complete. The host's rule costs
host atomics their speculation. The race test takes 121.9 µs instead of 116.3 µs (+4.8 %), and
the graph search with the hammering host 863.0 µs instead of 861.6 µs (+0.2 %).

**A read-modify-write atomic is one entry, performed at the bank by a small arithmetic unit beside it.** Such a unit is ordinary in real memory systems: RISC-V implementations execute their atomic operations with an arithmetic unit inside the data cache, graphics processors execute atomics in their last-level cache slices, the AMBA CHI interconnect defines far atomics performed at the home node, and PCI Express defines atomic operations completed at the target; the operation set is nine operations at two widths, so the unit is an adder, a comparator and a few logic gates. The entry reaches the head for its
address; the bank reads the word, a small arithmetic unit beside the bank applies the
operation, the bank writes the result back, all as one indivisible bank occupancy; the old
value returns to the context, which wakes. If the line is absent the bank acquires it first
and *then* performs the operation, so **the read-modify-write never spans a cache miss**.
Two consequences: a queue of `k` atomics to one word costs `k` bank read-modify-write
cycles rather than `k` memory round trips, which is the benefit wanted from atomics at the
memory; and because the word is never outside the coherent hierarchy there is no ownership
token, no hand-off chain, no bound on how long a value may stay out, no line to pin and no
write to repeat. Two atomics to different words of one line do not overlap, so they
proceed in either order and the line is locked for one access rather than for a critical
section.

**A coherence request acts on the bank, never on a queue.** A queued write is not visible
to anybody until it reaches the bank, and an access that has not been performed cannot be
snooped — so a request arriving while a store or atomic is queued is answered from the line
as the bank has it, and the queued entry re-acquires the line when its turn comes. Keeping
coherence requests out of the queues is also what keeps the wait-for graph acyclic: an
entry in a queue holds only its position; a queue's head depends only on the bank below it;
a bank depends only on its array or the fabric; a context waiting for a fill holds only its
own slot; and an incoming coherence request needs no queue entry at all, so no resource
waits on a resource above it. The mechanism this replaces failed that test in two places at
once — an unbounded waiter list, and pins that made a cache fill depend on a client
voluntarily surrendering a line.

A coherence request waits in three cases, each bounded by construction. While a bank is
mid-read-modify-write on the line: at most one bank occupancy, because the operation never spans
a miss (its read takes the line for ownership, so its write is a hit). While the owner's pair is
open over the line: at most the pair's window after its load-reserved was answered, and one cache
access for its store-conditional (above). And while this tile's writes queued against the line
drain: at most `snoopDeferLimit` cycles from the request's arrival. Each is counted along with the
cycles deferred.
This is the one place where the tile's strict-priority position in the coherence protocol is
visible as a mechanism rather than a claim, and it is stated as a bounded deferral with a
counter rather than as a property asserted to be preserved. The complementary case — a
queued write whose line was taken between acquisition and its turn — re-acquires and is also
counted, so the cost of yielding is visible wherever it is paid.

**A host core and a function core updating one word.** The queue's order is an order over
*this tile's* accesses, so everything above rests on the tile keeping the line. If another
agent is given the line while a point is open over it, that agent may write bytes the queue
will never see, and a store-conditional closing the point afterwards would report that nothing
had touched the address — the one thing the pair promises it cannot do. Three rules close
that, and together they are what lets the two kinds of core share a word. First, **a line the
tile loses breaks every point over it**, in both places a line is yielded, and the
store-conditional then fails; a failed store-conditional is a legal outcome and the program's
retry loop absorbs it. Second, **the tile is told when it loses a line** in every
configuration: where the tile's data cache is this tree's own it is *asked*, which is what
makes the bounded deferral above possible, and where it is a stock coherent cache the
invalidation is now forwarded to the tile, which is strictly less — there is nothing left to
defer by the time the notification arrives, so such a cache holds the line of a load-reserved it
has just answered itself, for the pair's window, as a stock core's L1 holds its reserved line
(rule 6 above). The notification is the half correctness needs; without it the tile was never
told at all. Third, **a host performs
its own read-modify-write by taking the line**, in its own cache, with the directory
arbitrating — the machine's one serialising mechanism is ownership of the address by the agent
performing the operation, and a host core owns an address the way any conventional core does.
Its reservation lives in the cache that is its presence on the fabric, one line address per
client port, broken by any snoop or eviction of that line; a downgrade breaks it too, which is
conservative and never incorrect, because a spurious failure is a legal outcome of the pair.
The cross-agent directed test has the host's atomic additions and the tile's pairs competing
on one word and on one line, and it is exact arithmetic that a single lost update destroys:
the tile's 783 points, 512 closed by their own half and 274 broken by the host taking the
line, no spurious success and no timed-out point.

One half of this is not repaired and is recorded as it stands: the in-order host still cannot
perform its own atomic, because the repair reaches a second defect in the stock cache, where
an unlock is queued behind the coherence request that is waiting for it. The figure above is
the out-of-order host, which is this machine's host.

**When a queue is full, the machine slows down and nothing breaks.** The chain is: a queue
with no free entry withholds credit for its bank; the delivery window keeps the entry,
neither retrying nor dropping it; a full window stops accepting from the translation path; a
stalled translation path leaves the virtual address in the context's load slot and the
context issues nothing, exactly as a context asleep on a load issues nothing; the context
wakes when the request moves. No timeout, no retry counter, no capacity fault anywhere. A
queue at the smallest depth the reservation's floor admits is a legitimate configuration that
runs correctly and slowly, and a depth sweep — taken before the floor existed, down to a depth
of two — shows a curve rather than a cliff — which is the property the replaced mechanism did
not have, and it is a test rather than a claim.

**Request classes.** Every memory request in the tile goes through this one mechanism:
program loads, stores and atomics; the page-table reads a walk makes; the privileged
page-table writes that follow a remap — one eight-byte entry per copy for a moved grain,
since a grain is one leaf (§2.3); and the line-sized transfers an invocation's arrival
and departure make. The privileged page-table write is named explicitly as its own class
precisely because it is the one legal write from inside a tile to a duplicated page: it is
exempt from the refusal of §1.2, it can never arise from a kernel store, and like a walk it
gets reserved capacity so that it cannot be starved by the core's own traffic. A line-sized
transfer is not expressible as one word-sized entry, so its representation is stated
explicitly — a line payload, or a stated number of entries — and an eight-by-64 view of a
context is never allowed to become the architecture.

**Widths are derived, never written.** The physical address a queue entry carries is sized
from the machine's geometry at the maximum configuration, not fixed at a convenient number:
the carried address is 49 significant bits at a full 48-bit physical space, and the extra
bit is stripped only at the memory port, below the queues. Queue and bank counts are bounded
by the cache they sit in front of — banks must not exceed sets, and sets must divide by
banks — and that derivation is part of the configuration, not a separate assumption.

**Five counters must read exactly zero, because they are the signatures of the failure modes
this design claims to have removed:** a request delivered to a queue that does not own its
physical address; a context's two same-address accesses completing out of program order; any
memory value held above the data cache; a kernel store or atomic reaching a duplicated page;
and a request admitted without a physical address. The rest are ordinary measurements —
admissions per queue and class, forwarded loads, partial forwards, order stalls and their
cycles, the longest run of consecutive same-address entries, atomics and how many were bank
hits, bank lock cycles, coherence requests to the bank and those deferred, queued writes that
had to re-acquire, per-queue occupancy and full cycles, and context-cycles spent waiting for
a queue.

**`WAIT` takes nothing from the memory queues, and a write performed in one is heard by the
waiters.** A `WAIT` is never admitted (`waitQueueAdmits` is a zero gate). Every write the bank
performs — a store, a successful store-conditional, the write half of a read-modify-write
(whose own context is excluded, so its own arm stands), a context write, a page-table write —
queues an invalidation broadcast of its line (§2.7). What `WAIT` does concentrate is the
re-read: one write can wake every context asleep on a line, and all of their re-loads go to
the one queue that owns it, where the issue condition performs them one bank access each. On
the dictionary with 128 resident workers per tile, each publication woke all 128, and the
sampled spans that hold the publications counted 1,422,148 ordering stalls over 124 such
broadcasts, about 90 stall-cycles per re-load (§4.7).

### 2.6 Why this is simpler than what it replaces

The mechanism this replaced — deleted from the tree in the change that landed the queues —
kept an atomic's word *above* the data cache, in the tile, so
it could be handed from context to context without a cache access. One entry per held word
carried the owning context and its token, the live value and a valid bit, a count of
hand-offs, the address and size, an owned flag, a writeback-in-flight flag, a re-dirtied
flag, and a queue of parked waiters. Around it sat an index from cache line to held words, a
pin set inside the data cache, an upward notification path from the cache to its client, and
the states that existed only to manage the edges.

The fields are not the point; the interactions are. Keeping a word above the cache required
the cache to tell its client when a line was snooped and the client to hand the word back to
be patched into the line; the line to be pinned so the notification could arrive at all, and
the pin to ride on the fill so there was no window between them; a protocol for a pin that
cannot be granted, in which a set with no free way asks the client to surrender its oldest
pinned line and the fill retries; an entry that outlives its own writeback, so a write is
repeated if anything touched the word meanwhile; a bound on hand-offs so a word could not be
kept out of the hierarchy indefinitely; and rules for an owner that migrated or was killed
while its word was held. Every ordinary load and store also had to consult the table,
because a word held above the cache makes the cache's own copy stale. Two of its statistics
counted events that should not be able to happen at all — a writeback whose entry a snoop
had already taken, and a writeback whose invocation had left its slot — and they existed
because three separate structures could act on one word independently.

Counted as things that must agree with each other, the replaced mechanism had four: the
table, the cache's pins, the notification path, and the directory. The new one has **one**:
a queue entry against another queue entry in the same queue, resolved by position. Ordering,
forwarding and atomicity stop being mechanisms and become properties of an array.

Cost then grows in entries rather than in interactions, which is the scalability claim in
full:

| what is added | what it costs | what it does not cost |
|---|---|---|
| one more pipe | one more instruction and one more data request offered per cycle; the window tracks the pipe count | no new structure and no new interaction |
| one more bank | one more memory queue, one decode output, one credit counter, one arithmetic unit | nothing in the other queues: they share nothing |
| one more context | one slot pair | nothing in the ordering machinery: no waiter lists, no pins |
| one more in-flight request | one queue entry | no change to forwarding, atomicity or coherence |
| four times the banks | a split window — the same structure replicated | still no crossbar |

The honest part of the trade must be reported with the win: **bank accesses go up and fabric
traffic goes down.** A load that the old mechanism answered from a word above the cache with
no cache access at all becomes a bank hit. The counter pair that states it is bank accesses
against the tile's total requests to its last-level slice, and the claim — that moving work
from the fabric into a banked local array is the right direction — is checkable rather than
rhetorical.

### 2.7 The banked caches

An engine must fetch one instruction and perform one data access **per pipe per cycle**. An
array of that throughput is built as independent single-ported banks over disjoint sets, not
as one array with many ports, which costs area quadratically. So each of the tile's
instruction and data caches takes a bank count, the bank is the line address modulo the
count — a partition of the sets — and a count that does not divide the sets is refused
rather than rounded, because a bank holding one set more than its neighbour makes a conflict
stop meaning one thing. The count comes from the pipe count the tile is already given.

**A bank reads one line per cycle, not one request.** An array access returns a whole line,
so every request for that line in that cycle is answered by the one read. Charging per
request instead was tried and the counters rejected it immediately: on a small graph search
the instruction cache reported 290,192 conflicts against 196,627 accesses, because four
pipes fetching four instructions out of one 64-byte line collided three times. That is not a
cache, it is a missing line buffer. With one line per bank read the same run reports **zero**
instruction-cache conflicts.

Four counters sit on every cache: accesses granted a bank; conflicts, counted once per cycle
a request waits, so the figure is the delay the banking costs rather than the number of
unlucky pairs; banks busy, summed over the cycles in which any was, so its own count is the
denominator; and accesses answered by a read another request had already paid for.

**Every bank broadcasts `INV(line)`** — a line address and an invalid bit, no data — to the
context array on four events: a coherence request that takes the line (the data cache tells the
tile the snoop's kind, and a downgrade, which leaves a shared copy that still hears the next
write, broadcasts nothing); a write performed at the bank (a store, a successful
store-conditional, a read-modify-write's write half, which excludes its own context so its own
arm stands); and **every eviction**, which the data cache now reports to the tile, because once
the bank has let a line go it can no longer hear writes to it. A broadcast is queued on the
owning bank and delivered one per bank per cycle on the bank's return port; the ones caused by
an access ride that access's return, and only a snoop's takes a port cycle of its own
(`invSnoopOnlyPortCycles`). The model is conservative here: it keeps a separate queue per bank
and delivers every broadcast from it, so one caused by an access waits behind a snoop's as a
snoop's would, and the wait is counted (`invPortWaitCycles`: zero in every directed test,
34,569 cycles over the dictionary's measured regions). The cache may ask the tile before a
snooped line goes (the default) or tell it afterwards, as a stock coherent cache does; both are
modelled and both are tested.

**Evictions and the snoop's kind are new reports from the cache.** Every message the data
cache sends the tile about a snoop now carries one bit of kind — the line is taken, or it is
downgraded and a shared copy stays — and the cache reports every eviction as a notice that
needs no answer, naming the line whose fill took the way. Before this, the tile heard only a
snooped line's address and nothing of evictions. The two optimisation points these reports
open are built at their defaults and counted, not decided: **every eviction wakes** (on the
dictionary, 1 eviction wake of 3,073 wakes over a whole run and none over the sampled regions
and spans, because every tile's waiters sit on one line that is re-read on every wake; the
directed set-overflow test gives 7,108–7,268 eviction wakes and 6,675–6,856 chains), and **a
downgrade wakes nobody** (the dictionary's regions and spans saw 2,149 downgrades, none of a
line with a sleeper; a directed test in which the host reads the line ten times and writes it
once wakes each sleeper once).
**The one limit this creates** is the bank's capacity: more distinct waited lines than a set
has ways (eight in a 16 KiB, 8-way, 4-bank cache) make each woken waiter's re-load evict
another waiter's line, and the waiters poll at the miss rate — counted as `waitWakeEviction`
and `waitEvictWakeChains`, never a deadlock.

### 2.8 Last-level banks are the memory's banks, and the controller keeps a queue per bank

One rule, in two halves: a cache's banks should be the same partition of the address space
the structure below it already uses, and the structure below keeps one queue per partition,
so traffic for one bank never waits behind traffic for another.

The memory device modelled is DDR5-4800, two ranks of eight bank groups of four banks, on a
32-bit subchannel: four 16 Gb x8 devices per rank (JEDEC JESD79-5's 16 Gb x8 organisation —
8 bank groups × 4 banks × 65,536 rows × 1,024 columns of 8 bits, a 1 KB page), so **16 GiB per
tile and 64 GiB at four tiles**. The machine's memory size is derived from that file — each tile
brings one channel of the device's capacity — and the address width with it. The controller's address mapper lays a tile's local address out as, from
the bottom: six bits of burst, six bits of column (64 lines, 4 KiB — one row of one bank),
one rank bit, three bank-group bits, two bank bits, then the row. Each tile's last-level slice
has 32 banks and picks its bank from **the device's bank-group and bank bits** (local address
bits 13–17): the line index shifted past one row's column lines and the rank bit, modulo 32.
So a slice bank owns exactly two DRAM banks — the same bank in each rank — and a request that
has picked a cache bank has picked its DRAM bank's queue. The shift is derived from the device
file, and the cache refuses a shift and bank count that do not partition its sets; at the
default 4 MiB, 16-way slice the bank bits are the top five set bits. Two configurations cannot be
aligned and say so rather than pretend: a device with more banks than the slice has set bits for
(sixteen bank groups — 64 banks per rank against 4,096 sets), and the serial memory link, whose
expander interleaves lines over its own channels. In both the slice keeps line-interleaved banks.

This was not true until this round. The slice had the right *count* of banks and picked one as
the line index modulo 32, which is the device's **column** bits: the 32 slice banks between
them covered half of one DRAM row. The count matched and the partition did not.

**The controller keeps one queue per DRAM bank over one shared read queue.** Each of the
channel's 64 banks keeps its reads in arrival order. Each bank offers its oldest entry whose
next command is ready, the channel issues the oldest offer, and an entry whose row it opened is
served before anything may close that row. The entries come from one read queue per channel, and
any bank may fill it. Posted writes are held in a separate write queue, filed under their bank.

**Sizing the queues.** The read queue is sized from what can send the channel reads:
P = Q × d × T + H.

- Q is the tile's memory queues (4).
- d is their depth (`dataQueueDepth`, 16).
- T is the tiles whose memory the channel holds (1).
- H is the host L2's miss-status registers (64).

That gives **128**, which is the larger of the two depths of Arm's CoreLink DMC-620
out-of-order request queue (`DMC_QUEUE_DEPTH`, 64 or 128; TRM ARM 100568, section 1.5).
`coherent_memory.py` derives P for each run. Any bank may hold all of P. The measured per-bank
peaks are most of what a channel holds: 20–27 reads on the shuffled sum, 29–57 in the graph
search's level 7, 108 in a one-bank storm. Dedicated queues that deep would be
64 × 128 = 8,192 entries a channel. The structure is therefore FR-FCFS over one queue with a
head per bank.

**The write queue.** Reads and writes are held apart, as in the read and write pending queues
of Intel's Xeon E5 memory controller (RPQ and WPQ; uncore manual 329468). The write queue has
as many entries as the read queue. No product publishes that size; it was chosen by a sweep in
which 96 and above reached parity. The bus turns to writes above 0.8 of the write queue, or when
no read is waiting, and turns back below 0.2. **A drain empties the batch it began with**:
while the bus is on writes and reads are waiting, a new write does not join the batch.

**Where a request waits when the queue is full.** A read that finds the read queue full, or a
write that finds the write queue full or a drain in progress, waits in its bank's arrival queue.
That queue is the last-level bank's own path to its DRAM bank. One request moves in per cycle,
so the controller never refuses a request and a full bank stalls only its own traffic.

The organisation is the per-bank pending-reference queues of Rixner et al. (ISCA 2000). The
controller it replaced is ramulator2's generic one: one 32-entry read buffer and one 32-entry
write buffer per channel, ramulator2's defaults, which no named product publishes. Behind that
buffer memHierarchy keeps a single in-order queue, so one full buffer stopped every bank. That
controller is still selectable (`NMFC_BANK_QUEUES=0`, which also restores line-interleaved slice
banks), so that a before and an after are two settings of one build. `NMFC_BANK_QUEUE_DEPTH=<d>`
selects dedicated per-bank queues of d reads as a diagnostic.

**What a bank conflict costs**: a precharge and an activate before the column command, about
28 ns over a row hit (tRP + tRCD = 34 + 34 device cycles of 0.417 ns, the JEDEC DDR5-4800 timings
in `src/nmfc/config/tile_ddr5.yaml`), and back-to-back conflicts in one bank are limited by the
row cycle, tRC = 111 cycles, 46 ns a line — about 1.4 GB/s from one bank against the subchannel's 19.2 GB/s.

**What it does, measured.** A directed test on one tile makes it concrete: a storm of
invocations each reading its own row of **one** DRAM bank, against victims reading the same
number of lines each in its own row of the other banks, with the host and the tile each taking
both roles. At 32 contexts per tile the old controller was never the limit either — a channel's
reads come from its own tile's contexts, one outstanding each, so it never held more than its
32-entry buffer — and the victims ran at their solo time under both. At 128 contexts, with a
96-invocation storm, the difference is the mechanism's: tile victims under a tile storm took
29,903 cycles against 21,943 alone, where the single-queue controller took 267,704; the host
reading the victims' lines under the storm took 21,089 cycles against 110,522. That needs the
tile's own memory queues deep enough for its contexts (four queues of 32): at their default of
16 entries (`dataQueueDepth`, `src/nmfc/src/NMFCTile.h`) the storm's entries hold all 64 slots while they wait on the one bank, and the victims wait
for a slot in the tile — 250,146 cycles — whatever the controller does. A host storm never
fills the channel: the host core has 20 first-level miss registers, the middle of the published
range (Golden Cove's 12–16 fill buffers, Zen 4's 24 miss-address buffers), as sourced in
`src/nmfc/test/coherent_memory.py`.

**Performance, against the single-queue controller.** All three measurements used the same
images and one build, with `NMFC_BANK_QUEUES=0` as the before arm.

- **The shuffled sum's offloaded phase** (striped, 32 MiB, 4 tiles, measured whole) took
  **994,413 cycles against 995,883**.
- **The same program as a whole,** measured on six paired sampled windows plus that phase, gives
  before/after = **1.015**, with a 95 % Fieller interval of [0.980, 1.076].
  - Five windows agree to within 0.05 %.
  - The sixth is the host's cold-start construction: 19.2 M cycles against 22.4 M. It writes
    heavily, and the deeper write queue cuts its write-mode time from 14.2 M to 3.3 M device
    cycles.
- **The graph search's 16 MiB level-7 window** took **1,584,221 cycles against 1,590,118**.
  - One channel held up to 138 reads there, because other tiles' reads of that tile's memory
    are not counted in T. As a result, 25 of its 11,762 reads (0.2 %) waited for the 128-entry
    read queue.

**The earlier version.** It had 8 reads per bank, after DRAMsim3's per-bank command queue, and a
32-entry write buffer, and it made the offloaded phase 5.3 % slower (1,048,456 cycles). The
counters show that the depth was not the cause. With the shared read queue and the same
32-entry write buffer, the phase still took 1,047,249 cycles, although no read ever waited
outside the scheduler. The cause was the write drains: the arrival queues refilled the small
buffer during each drain, so each drain lasted longer while the reads waited. On the same phase:

| write queue | batch rule | phase cycles |
|---|---|---|
| 32 | on | 1,019,772 |
| 64 | off | 1,032,957 |
| 64 | on | 1,003,768 |
| 96 | on | 991,677 |
| 128 | on | 994,413 |

With the adopted queues, write-mode time falls from 3.2–3.7 M device cycles per channel to
1.9 M, and the mean read latency falls from about 650 device cycles to about 380.

**One variant was rejected.** A variant held the writes in the same 128 entries as the reads,
as the DMC-620's single queue does. It was as fast on both workloads. But in the storm test the
storm's writes filled the queue and held other banks' reads out, and the tile victims took
3.8× their solo time, against 1.3× with separate queues.

**One discrepancy is recorded and not changed.** `coherent_memory.py` computes `DRAM_ROW_BYTES`
as columns × 4 bytes × 2, 8 KiB, where the controller maps one bank's row in one rank as 64
lines, 4 KiB, because a column address selects a column as wide as the data bus and not one
beat. The grain G is derived from the 8 KiB figure, so a G-unit is four sweeps of the banks
rather than two; it still spans every bank evenly, and correcting it moves every placement, so
it is a separate change.

**The slice's size is one declaration.** Its size (4 MiB by default) and its ways (16) are
stated once, in `src/nmfc/test/nmfc_sizes.py`; `NMFC_SLICE_SIZE` overrides the size. The
machine builder sizes every slice from it, and the compiler reads the same numbers for its
placement rule (§3), so a configuration with larger or smaller slices moves the machine and
the compiled programs together. With the variable unset, the configuration is identical to
the one before the change. The bank alignment above needs the device's bank bits inside the
set index. An 8 MiB slice has 8,192 sets and aligns (shift 7), as the 4 MiB slice does. A
2 MiB slice has 2,048 sets, fewer than 32 banks shifted past a row and the rank bit, so it
keeps line-interleaved banks (shift 0), as the rule already states for a slice too small to
align. Its access latency is a separate setting (`NMFC_SLICE_NS`, 8 ns).

### 2.9 The memory link, and CXL as the alternative

The link between a tile's memory controller and its memory device is **configuration, not
design**. Two settings exist. The **parallel bus** is a pass-through — one DDR5-4800
subchannel per tile, no link modelled — and it is the default every workload number in §4
was taken on. The **serial link** models a x16 CXL 4.0 attachment with twelve DDR5-4800
subchannels behind an expander: much more bandwidth, and more latency in front of it. At
pin parity with one memory channel, a x16 serial attachment on a PCIe 7 generation carries
roughly 213 GB/s against roughly 32 GB/s for one DDR5-8400 channel, so about twelve
average-rate channels are needed behind it to saturate the link. The added access latency
costs host cores about a third of their performance and costs the engines little, because an
engine tolerates latency with context count.

§4.5 gives what the two settings measure. The important property is that the parallel
setting is byte-identical pass-through: every parallel column there equals the same point in
the workload sweeps exactly, which is what confirms the claim.

### 2.10 Structure and configuration

Sizes and counts are never design constants. The split below is what may change per run and
what is the design.

**Configuration** — swept, with the measurement that sets each: contexts per tile; pipes;
pipe depth; instructions in the instruction slot; branch-target-buffer entries; data-cache
banks and therefore memory queues; memory-queue depth; window width; reserved entries for a
walk and for a privileged page-table write; translation queues, their depth and the
translation latency; translation-buffer size and banking; the store slot-release rule;
instruction-cache banks; bank access latency; last-level slice size (one declaration, which the
compiler's placement rule also reads), banking and latency;
memory device and link.

**Resources and their limits** — each with its capacity and where the number comes from:

| resource | capacity | source |
|---|---|---|
| floating-point unit, per pipe | one operation per cycle, pipelined; add 2, multiply 3, fused multiply-add 4, convert 3 cycles | Arm Neoverse N2 SOG, table 3-19 |
| divider, per tile (`fpDividers`) | one, shared by every context; FDIV.D 15 cycles held 7, FSQRT.D 16 held 8, FDIV.S 10 held 5, FSQRT.S 9 held 2 | N2 SOG, table 3-19, worst case of each range |
| host floating point | two pipelined units (add 2, multiply 3, fused multiply-add 4) and two dividers held as the tile's | Arm Neoverse V2 SOG, table 3-11 |
| integer multiplier, per pipe | one operation per cycle, pipelined; MUL 2, MULH 3 cycles | Arm Neoverse N2 SOG, table 3-7 |
| integer divider, per tile (`intDividers`) | one, shared by every context, separate from the floating-point divider; 32-bit divide or remainder 12 cycles, 64-bit 20, held for the whole latency | N2 SOG, table 3-7, worst case of each range, note 1 |
| host integer divider | one; 12 cycles (32-bit) and 20 (64-bit), held for each | Arm Neoverse V2 SOG, table 3-4 |
| memory, per tile | one DDR5-4800 channel: 2 ranks × 8 bank groups × 4 banks × 65,536 rows × 4 KiB = 16 GiB; 64 GiB at four tiles | the device file; JEDEC JESD79-5, 16 Gb x8 device |
| physical address | 36 bits at 64 GiB (the smallest power of two covering the memory); the host core refuses any access outside it | derived from the memory size |
| physical arena of frames | from 0x80000000 to the top of memory (62 GiB at 64 GiB); a configuration whose arena overlaps the host's identity-mapped window [0x60000000, 0x80000000) is refused | the host operating system's stack top 0x7ffffff0 and program headers at 0x60000000 |
| grain `G` | row 4 KiB × every bank of the channel (2 × 8 × 4 = 64) × tiles = 1 MiB at four tiles | canon E.3, from the device file |
| page-table copy, per tile | as many of the tile's grains at the top of memory as a bound on the table's size needs (one grain up to about 500 MiB of mapped 4 KiB pages; 37 at 18 GiB) | derived from the declared regions |
| armed lines, per context | one: the condition a `WAIT` sleeps on must live in one line | one arm per load slot (§2.1) |
| invalidation broadcasts, per bank | one per cycle; a busy port delays delivery (`invPortWaitCycles`) | the bank's return port (§2.7) |
| waited lines held without eviction wakes | eight per set (16 KiB, 8 ways, 4 banks); beyond it the waiters' re-loads evict each other and they poll at the miss rate (`waitEvictWakeChains`) | the tile data-cache configuration |
| contexts a writer can use on a tile full of waiters | the tile's contexts minus its waiters; a waiter must never be what its writer needs | the tile's context count |
| a `FORK.M` context read's place in the tile | one entry in the in-order queue of physically addressed requests (page-walk reads, page-table writes, context transfers), then one delivery-window slot and one line-class memory-queue entry | this model (§2.1) |
| delivery-window slots of the physically addressed class (`xcPhysSlots`) | one beyond the configured width, which only walk reads, page-table writes and context transfers may take; that class also competes for the width behind translated requests | a dedicated credit per class beside a shared pool: Intel QuickPath VN0 / VNA (Intel, 2009); PCI Express per-virtual-channel flow-control credits (Base Specification 3.0) (§2.4) |
| the host's cache-block clean (`cbo.clean`) | one 64-byte line per instruction; a store-queue slot, a store port and a store-buffer entry until the first-level cache answers; for a dirty line, 72 B on the host L2's outbound link and one slice write | RISC-V Zicbom; this model (§1.4.7) |

**Structure** — changing one is a different design. A context has exactly two slots. A
context cannot be scheduled without its instruction in hand. Translation is virtually
indexed and happens before anything enters the data path. Memory queues are physically
indexed and each owns a disjoint fraction of the tile's physical addresses. A memory queue
serves its entries in order and is the only ordering point. An atomic is a read-modify-write
at the bank. Nothing holds memory state above the data cache. The load slot keeps an arm — a
physical line address and a valid bit, written at translation — after its operation
completes; it never holds data; a bank broadcasts an invalidation of a line to every context,
and nothing records who waits on what. A coherence request acts on
the bank, never on a queue. Delivery is at most one request per queue per cycle,
oldest-first per destination. Backpressure is credit, and a blocked context waits in its own
slot. The branch-target buffer is shared and issues exactly one speculative fetch, which
never executes. There is no decoupled instruction-fetch engine on the tile, no data
prefetcher, no reorder buffer, no renaming, no speculative execution, and no structure that
lets a load issue before an older store's address is known.

---

## 3. What the simulator models today, and what is designed but not modelled

Everything in §1 is modelled and measured. **The whole of §2 is now built**: the pipes, the
translation path, the delivery window and the memory queues were the designed half of this
document a week ago and are the machine today, and the mechanism they replaced — the table of
words held above the data cache — has been deleted rather than left switchable. What remains
designed and not modelled is three things: two sizing options inside the tile core and the
two comparison machines. The memory controller's per-bank queues, and last-level banks that are
the DRAM's banks rather than merely as many, were built this round (§2.8). The table states it plainly,
one row per mechanism, and a row that changed this week says what it changed from.

| mechanism | in the model today | designed, not yet modelled |
|---|---|---|
| Host: out-of-order core, two-stage front end with override predictor, wrong-path modelling, two-level TLB and walker at the cache management units, data prefetchers, memory-dependence prediction | **yes**, each with its own counters. The memory-dependence default is now the path-history predictor, measured over eight paired windows at 1.3791× the two-bit counter [1.3154, 1.4457] and 1.0431× the store-address predictor [1.0019, 1.0866]; the other two remain selectable by name. A load-reserved (or a locked load) goes to memory only at the head of the reorder buffer, as BOOM performs its atomics: sent speculatively, one on a squashed path left a reservation that validated the correct path's store-conditional and lost an update against a tile (§2.5); host atomics lose their speculation (the host-against-tile race test 4.8 % longer). The load/store queue's age count no longer restarts after a full clear while retired stores remain | — |
| Fabric: a port per endpoint per direction, 32 B at 2 GHz, ten endpoints, traffic counted per port and per direction as control, coherence and data; the directory one home node per slice at one transaction per port cycle | **yes** (§1.6). Writebacks and snoop responses now cross their link one packet at a time and are acted on when they arrive; acting on them when sent let the host L2's link (32 B at 2 GHz) be booked about 450,000 cycles ahead in the graph search's level 6 and emptied the tiles for 45 % of level 7 (§2.2) | a directory that supplies a clean line to a tile from the tile's own slice rather than forwarding it from the host L2, whose link now binds that level |
| Caches' miss-status files: one file per bank, sized from the contexts the cache serves | **yes** (§1.6), on both tile caches; the last-level slice holds the sum in one pool | the per-bank arrangement inside the slice, which lives in a library outside this tree |
| Coherence: directory at the fabric, four tiles each with a last-level slice, memory controller and channel | **yes** | — |
| 512-bit context; the lane map including both tilings and the reserved names | **yes**, verified lane by lane in both simulators | an operation that moves the whole 512 bits (`r1` as an operand); it is an instruction-set extension, not a naming change |
| Instruction slot, shared branch-target buffer, one speculative fetch at decode, no decoupled fetch engine | **yes**, holding one instruction | the block form of the slot, and the sweep at 1, 4, 8, 16 that picks its size |
| One outstanding memory access per context; the load slot | **yes** | the relaxed store rule as a switch, with its own measurement |
| Pipes | **yes** — `N × M` stage registers with named decode, address and writeback stages, the readiness and conjunction counters, and the two identities that say the array advances once per cycle | — |
| The pipe-bound alternative: a context bound to one pipe, with forwarding | **refused on evidence** (§2.2) and refused at construction, not deferred: on the graph search every load-free run is one instruction long, so the mechanism has no consecutive instruction to issue. The parameter stays so the question can be re-read on a workload with long arithmetic between accesses | — |
| Translation | **yes** — queues indexed by the virtual page, a completion rate derived from the pipe count and any other rate refused, a walk-pending array so a miss does not block the hits behind it, head-blocked cycles counted. **New this round (§2.3):** one leaf per grain — a table whose level 1 spans exactly `G`, grain and duplicate pages one leaf, a striped page one leaf per grain, host pages and grains a region boundary cuts 4 KiB; walks that end at the first leaf and return its size; translation buffers holding both sizes in one array with a size tag per entry, after Neoverse V2 (the tile's 64 entries now fully associative, where they were direct-mapped under a hash of a key computed from the region list); a remap one eight-byte write per copy. Tile walks on the compiled reduction at 4 MiB fell from 443,955 to 98, and on the graph search's level 7 from 1,973,566 to 28; the programs ran 0.2 % to 3.3 % faster with every answer unchanged; `xlatTlbStaleHits` and the two leaf-size mismatch counters are zero gates | an `N × G` leaf for a striped page none of whose grains has moved; the duplicate region holding code aligned to a grain, so that code is one leaf (the linker places it at 0x10000) |
| The walk's own reads: through the data cache against reserved capacity, or straight to the last-level slice | **yes, both arms**, each with the refusals that stop it being measured as the other machine. **Under open analysis**: indistinguishable on the two sampled points so far, because the reservation was never contended, and the first reading of that rested on four walk-source counters that read zero while 1,007 walks ran — now a fifth bin and an accounting gate | the workload that separates them: one whose data traffic fills the queues while a walk needs to issue |
| Getting a translated request to its bank | **yes** — the delivery window, oldest-per-bank, one delivery per bank per cycle, with the limit counter rewritten so that it can fire at all. **New this round (§2.4):** the window's two classes — translated requests, and the physically addressed ones (walk reads, page-table writes, context transfers) — are no longer filled in a fixed priority; the second has a slot of its own beyond the width (`xcPhysSlots`, a dedicated credit per class beside a shared pool, after Intel QuickPath's VN0/VNA and PCI Express's per-channel credits), counted in the reservation floor. The `FORK.M` start on the compiled reduction at 4 MiB fell from 6,487.6 to 107.9 cycles (its wait for a slot from 6,380.2 to 1.0) and the program ran 2.3 % faster; the graph search's level-7 window, the hash table and the `FORK.R` reduction are unchanged; answers unchanged. Contended test `tile_ctxread` (host `FORK.M`, tile `CONT.M`, both, walk reads, the smallest stages, a control) in the coherent suite, gated against the fixed-priority window on the same run | the split-window escalation, if measurement ever says the window is the constraint; a bound on the walk reads the walk path can have waiting (with a one-entry translation buffer and the fixed priority, 64,551 were queued at once) |
| Ordering, forwarding, atomicity | **yes** — physically-indexed memory queues, one per bank: sequence order, forwarding from the newest older overlapping entry, read-modify-write at the bank, coherence requests at the bank with a bounded deferral. The word-keyed table above the data cache, its cache pins, its snoop merge and its unbounded waiter list are **deleted** | — |
| The load-reserved / store-conditional point: four ends (its own store-conditional, the line taken away, the owner's next memory operation that is not that store-conditional, departure), a unit of each stage reserved for the close, no takeover by another address | **yes**. A pair abandoned after a failed compare ends at the owner's next memory operation, as a RISC-V reservation lapses; the contended test (`tile_lrsc_away`: a compare-and-swap loop that branches away, a claim array, the host on the line) passes at 1, 2 and 4 tiles, at the default stages and the floor, under both notification modes, and the compiled graph search now emits the plain compare-and-branch. Also a cross-agent directed test in which a host and a tile update one word, and eight contenders at the memory queue's floor on one tile, two tiles and a tile alone in the suites. The close's unit is an escape slot beyond the window's width and an escape entry beyond the translation queue's depth (floor 1), entitled at translation only to the point owner's store-conditional; the memory queue keeps it inside its depth (floor walkReserve + 2); walk traffic keeps its memory-queue entry while a point is open, and the walk-pending drain does not stop at a request the window refuses. 28 and 32 contenders complete at the default configuration (§2.5; NMFC-Rev `43d62d2`). **The owner's pair is served before another agent's request for its line**: an answered point holds the request for the pair's window (`lrscHoldCycles`, derived: 127 cycles at 32 contexts, 487 at 128), its store-conditional at the data cache for one access, and a point opened after the request arrived not at all; the load-reserved fetches for ownership, the store-conditional is a conditional write at the data cache, the data cache serves the tile's hits while asking it, and a cache that tells afterwards holds the line itself. A host writing the line without pause (`tile_lrsc_writer`) no longer stops a tile's claim: at 1, 2 and 4 tiles, both notification modes, default and floor, the most attempts one increment needs is measured at 1 to 11; `tile_lrsc_away_race` passes in all 12 configurations and T12 with a cache that tells afterwards is gated; `memqLrscScLandedAfterBreak` is a zero gate | the host's own reservation has no hold: a host pair against a tile claiming the same word without end has no bound (§2.5) |
| Backpressure anywhere in the data path | **yes** — credit end to end, and the depth sweep shows a curve rather than a cliff: queue-full cycles 0, 1, 368, 6,417 as the queue goes 32, 8, 4, 2 with the answer unchanged | — |
| Tile instruction and data caches, banked one bank per pipe, a bank reading one line per cycle, four counters each | **yes**, including the per-bank arithmetic unit that performs a read-modify-write and the bank index on the request interface | — |
| Last-level slice banked by the memory device's bank bits; one queue per DRAM bank at the controller | **yes** — the slice bank is the device's bank-group and bank bits (it was its column bits until this round). The controller keeps a queue per bank over one shared read queue per channel, sized P = memory queues × their depth × tiles per channel + the host L2's miss registers = 128, the DMC-620's larger queue depth. Posted writes are held in a separate write queue of P entries, drained in batches that empty the batch they began with. Measured against the single queue: victims of a one-bank storm at 1.3× their solo time, where the single queue slowed them 9×; the shuffled sum's offloaded phase at 994,413 cycles against 995,883; the graph search's level-7 window at 1,584,221 against 1,590,118 (§2.8). The single-queue controller it replaces (one 32-entry read and one 32-entry write buffer per channel, ramulator2's defaults) stays selectable (`NMFC_BANK_QUEUES=0`) | T counts only the channel's own tile, so remote tiles' reads can exceed P: this happened for 0.2 % of reads on one channel at level 7 |
| Tracking unit derived to cover every context; the control queue following it; the host counting cycles its unit is full | **yes** | — |
| Floating-point costs: a pipelined unit per pipe, one iterative divider per tile, a divide's context waiting without an issue slot; the host's units and two dividers | **yes** (§2.2). Until this round every tile floating-point operation, divide and square root included, cost one pipe pass, and the host accepted a divide every cycle and ran square root on its adders. The integer workloads' tile statistics are byte-identical across the change | — |
| Integer costs: a pipelined multiplier per pipe, one iterative integer divider per tile separate from the floating-point divider, the host's one integer divider held per width | **yes** (§2.2). An integer divide or remainder cost one pipe pass on the tile, and the host's divider accepted one every cycle. None of the three workloads divides on a tile; every divide they execute is the host's, in printing results in decimal and, for the graph search, in its generator's modulo, and these move their end-to-end time by 0.3 % (graph search), 0.4 % (shuffled sum) and 1.1 % (hash table). Their tile statistics are byte-identical with the host's old divider | — |
| Memory size: one 16 GiB channel per tile, the address width derived from it, page-table copies that span as many of a tile's grains as they need, backing allocated only for pages touched | **yes** (§2.8). The machine had a flat 4 GiB; the host could not issue an address above 32 bits; a page-table copy was limited to one grain, which refused any program mapping more than about 500 MiB; and the loader wrote the zeros of every declared `.bss` page into the simulator's backing store. A test places, touches and reads back 4.5 GiB on each of four tiles; the simulator's resident memory is 1.24 GB for it and unchanged (165–177 MB) for the three workloads. The arena of frames started at 256 MiB, inside reach of the host's identity-mapped window (its program headers at 0x60000000 and stack below 0x80000000): a test placing 2 GiB of frames found all 510 of the host's pattern words in the window overwritten by tile stores. The arena now starts at 0x80000000, the same test reads every word back, and an overlapping arena is refused at configuration | — (the walker's 4 KiB-only leaves are replaced by one leaf per grain, §2.3; a copy of the table at 18 GiB mapped is about 150 KB where it was 37.9 MB) |
| Duplicate pages: a kernel store or atomic refused and counted, zero-gated, with a directed test; the host's legal fan-out to every copy | **yes**, including the privileged page-table write as its own request class with its own reserved capacity, gated so that nothing else can reach the exemption | — |
| The host's cache-block clean (`cbo.clean`, RISC-V Zicbom): a dirty line written back to its slice and kept clean and shared, the directory naming no forwarder | **yes** (§1.4.7), with a directed test of three contended cases, a control with the cleans compiled out, and `cleansStale` in the zero-gate register. The runtime cleans every prepared `FORK.M` block. This removes every recall of a block from the host and did not shorten the start; the window's slot for the physically addressed class did (§2.1, §2.4) | `cbo.flush`, `cbo.inval` and `cbo.zero`, which decode as faults |
| Migration on a foreign translation result | **yes**, taken at the translation result, with the program counter carried back so the instruction re-issues | the rule for a context that migrates with a store still in a queue, under the relaxed store switch only |
| Memory link: parallel pass-through and a serial CXL attachment, as configuration | **yes** | a workload that can saturate the serial link; the x32 variant |
| `WAIT`: the load slot's arm (a physical line and a valid bit written at the arming operation's translation), the instruction translated like a load and decided in the context array, the bank's invalidation broadcast on a line-taking snoop, a performed write and every eviction, one per bank per cycle; both ways a data cache can tell the tile it is losing a line (ask first, tell afterwards) | **yes** (§1.4.3, §2.1, §2.7), with the design's directed tests contended first — host, same-tile and migrating-in writers, one, two and four tiles, both notification modes — and five zero gates, one of which (`waitMissedWakeups`) stops a run at the first context left asleep after a write to its line. The workloads' tile statistics are byte-identical across the change. With a cache that tells afterwards, a tile's load-reserved/store-conditional pair whose window is longer than about five instructions livelocks against a host pair on the same word; that is the pair's missing fairness bound, which the row above reports, and not `WAIT`'s. The functional model's half is built too: the producer arms on every load and read-modify-write atomic, parks an invocation at a `WAIT` whose arm matches, wakes it only on a store or atomic performed to its armed line, and writes an image holding such a worker as format version 6, which records the armed line; the cycle model places a restored waiter on the tile owning that line, so its re-load does not migrate. **Measured** (§4.7): resident dictionary workers on `WAIT` at 128 contexts per engine are correct at the smallest size against the functional run at 32 and 128 contexts, with every zero gate at zero, and the tile instructions they issue while waiting fall from 1,118,398 to 1,024 over the sampled regions | forwarding the written word to a woken context. Its counter (`waitLocalWakeReloads`) is built; the dictionary cannot measure it, because every wake there is the host's write and none is a write on the same tile |
| Sampling on the total: images and regions placed on the counted host instructions plus the function cores' executed instructions; each tile's count of instructions executed (issued, less those dropped to be re-issued after a migration or fault); a region closed on the total and the run ended with it | **yes** (§4.1), with the function-core count equal to the producer's exactly and a consumer gate from the entry point and from images | images that hold the invocations in flight part-done, which long invocations forked together need (§4.1) |
| The compiled programs' host work per invocation: each run a descriptor written by the plan in fork order, so staging is three loads and three lane writes; a take reading only the result lane the program consumes; cuts and bottom-up descriptors computed once per set of bounds; the log lines a build option, off in measured builds | **yes** (§4.1, §4.7), with directed tests of the waiting section's extent, one refusal counted per refused fork, and a full tracking unit, on the graph search and the dictionary | a cheaper cut search for skewed degree distributions (interpolation was tried and lost); descriptors for the dictionary's consumers, which do not pay while each construct runs once |
| The compiler's placement of a read-only array its constructs reach (rule R7): replicated on duplicate pages, one copy per tile, or owned with own-and-pull | **yes**, as a capacity rule read from the configured slice: replicate when one copy and what the replicated phase keeps resident beside it (the kernel's text and the reduction targets) fit a slice plus one of its ways, `copy + other <= slice + slice / ways`. The margin is calibrated by the two measured sides of the crossover: a copy of exactly one 4 MiB slice won (owned / replicated 2.51 [2.18, 2.84]) and a copy of two slices lost (38.2 % slower). It replaces a table that replicated only an array of exactly the one measured size, 4 MiB. The same source compiled against 2, 4 and 8 MiB slices chooses as the rule says, and the compilation record names the numbers compared; at 4 MiB slices both named builds of the measured pair are unchanged | a paired measurement inside the unmeasured interval, between one slice plus a way and two slices, which today stays owned |
| Data prefetching into the load slot | no | deliberately undesigned; the slot is left free for one |
| A barrel multi-context core as a comparison arm: many contexts without the position and without the unit of work | no | **designed and reviewed**, not modelled — the arm that would say which of the three differences between host and engine produced a ratio |
| A graphics processor as a comparison arm, running the same three problems on the same inputs | no | **designed and reviewed**, not modelled |

Three further pieces are worth naming as absent on purpose rather than missing. There is no
reorder buffer, renaming or speculative execution on an engine, and no structure that lets a
load issue before an older store's address is resolved — the queues hold only requests whose
physical address is already known, issue nothing on a prediction and never replay. There is no per-context branch predictor: the target buffer is one shared structure indexed by program counter that any context consults for its own next instruction; contexts do not share an instruction stream. And there is no data prefetcher.

---

## 4. What the workloads measure

**Every performance figure in this section predates the tile core, the fabric and the
miss-status corrections.** The numbers below were taken on the machine as it stood before the
memory path of §2 was built, before the interconnect became a port per endpoint per direction,
and before the caches' miss-status files were made per-bank and sized from the contexts they
serve. Each of those three changes moves timing, and two of them move it on every workload, so
no figure here should be read as a measurement of the machine described in §1 and §2. They are
kept because they are the machine's history and because the questions they answer — where the
time goes, what limits the work — are unchanged by the corrections. Every one of them will be
taken again, on the same programs and the same sampling plan, when the owner declares the
machine complete. No new number appears here in the meantime.

### 4.1 How a performance number is taken

Two of the numbers below come from programs run uninterrupted to their own end; most come
from **sampled** simulation, and the distinction matters.

A cycle-accurate simulator models a clock edge at a time. It is the only way to get a cycle
count that means anything, and it is slow: this machine runs a few hundred thousand
simulated cycles per second of wall clock, so a program of a hundred billion cycles cannot
be run at all. The answer is a **functional** simulator that computes what the program
computes and models no time whatsoever; it runs the whole program in minutes and, at a fixed
interval, writes a **whole-program image** — registers and the whole address space,
everything needed to restart from that exact instruction. The cycle-accurate model starts
from an image, runs a **warm-up** that is measured by nothing, then a **region of interest**
whose cycles are the difference between two snapshots of every counter, then a teardown. One
region goes in each interval, and the regions are combined into an estimate for the whole
program with an interval that says how much the regions disagreed. Two configurations are
compared by running **the same regions** on each.

The coordinate that places a region is the program's own **work** count, not an instruction
count, because two builds of one program retire different instructions for the same work.
Sampled windows across two arms therefore cover the same computation and pair region for
region, which is also what says *where* in the program an offload wins.

A second coordinate now exists beside the first: **counted instructions**, the instructions
retired outside a program's wait loops. It is the only axis available where the work axis
does not apply — one arm's absolute cost on its own, a program with no work counter, a phase
the work counter does not advance in, or a comparison between two *different* programs — and
it was validated by running four programs uninterrupted to their ends and dividing the
estimate by the truth.

| program | what it computes | uninterrupted cycles | counted instructions |
|---|---|---|---|
| shuffled sum, offloaded | 524,288 chains of four dependent loads, 64 chains an invocation | 9,955,355 | 187,860 |
| shuffled sum, host alone | the same 524,288 chains | 63,174,430 | 37,272,417 |
| graph search, offloaded | a traversal of 65,536 vertices | 21,823,805 | 23,032,314 |
| graph search, host alone | the same traversal | 23,510,293 | 27,653,831 |

Against those four answers the estimator reaches 0.98 of the truth on the shuffled sum's
speedup (6.2237 estimated against 6.3458 measured, 95 % interval [5.9915, 6.4559], which
contains the truth) and 0.98 on the graph search's (1.0594 against 1.0773). The same
validation also found the warm-up that each arm needs, and one of the four needs **ten
intervals out of twenty** — half its program — because the pure-host shuffled sum walks a
permuted structure the size of its whole working set and is half again as slow until the
shared cache holds it. That is a property of validating at the smallest size that runs in
minutes: a warm-up is a fixed number of cache misses, and the program is what grows.

Two limits are stated rather than hidden. The counted-instruction axis is coarse where an
arm offloads almost everything — the offloaded shuffled sum's whole program is 187,853
counted instructions, so one interval of a twenty-image set is 9,393 of them and one turn of
the tracking unit is 6,912, which is why the design that reaches the truth there uses six
regions rather than twenty. And a warm-up measured at one phase does not transfer to
another.

**Waits the regions would miss are measured whole.** A wait retires no counted instruction, so it
has no width on the axis the regions are placed on and a region contains one only by chance. The
functional producer lists every wait episode with the instruction at which it entered the wait,
and episodes are grouped by that instruction, one group per wait routine. A routine entered n
times in a program of N counted instructions, sampled with regions U wide, is *rare* when
n × U / N < 1 — fewer than one of its entries is expected in a region — and then each of its
entries opens a span of 16,384 counted instructions, overlapping spans merged, and every span is
measured whole from the newest image at least a warm-up before it. The estimate is the spans'
cycles plus the rest of the axis at the regions' rate (`tools/sampling/waits.py`; `run_sampled.sh`
passes it its U). The rule replaced a fixed limit of 64 entries, which asked neither how long the
program is nor how wide the regions are. A region that wholly contains a span is kept, with the
span's measured counts taken out of every column; only a region that overlaps a span in part is
set aside, since otherwise a start-up region measured whole is lost with the span inside it. On
the dictionary at 677,205 inserts (U = 12,053), the two changes move the resident build's estimate
from 0.985 to 0.991 of its uninterrupted 42,067,209 cycles, interval [40,996,235, 42,387,361], and
leave the wave build at 1.001 of 38,751,307, interval [38,150,958, 39,463,258]; both intervals
contain the whole run. That left the resident build 0.9 % low, and the shortfall was at the
program's end: its last span's width is the producer's count of what the program has left, 9,654
counted instructions, but the host's final polling loop runs more turns in the cycle model than
in the producer, so the span closed at that count after 256,086 cycles with the final drain still
running.

**A region that reaches the program's end closes at the program's exit.** Past the program's last
counted instruction no count closes a region where the program ends, because the machine's count
runs ahead of the producer's wherever the host polls. The image records the program's total on
the region's axis; when a region's end is at or beyond it, the run is configured to close the
region when the host core issues the program's exit system call (an `exit_group`, or the `exit` of
the last running thread), and at no count. The core tells the host unit at that cycle; the unit
takes the closing snapshot there, and the region's row records `closed_by` = `exit` and the
invocations still outstanding. There is no teardown after such a region. A directed test
(`src/nmfc/test/run_exit_close.sh`, in the coherent suite) runs a program that ends in a drain of
four resident workers, polled partly outside the wait range: the region closed at the exit ends
15 cycles before the run does and covers 18,545 counted instructions against its width of 5,956,
145,542 cycles; the same region closed at its count covers 10,621 cycles and misses the drain.
Re-validated on the dictionary at 677,205 inserts, with the builds and images the uninterrupted
runs were made with, 20 regions each:

| build | uninterrupted | estimate | ratio | 95 % interval | contains it |
|---|---:|---:|---:|---|---|
| resident | 42,067,209 | 42,125,747 | 1.0014 | [41,430,184, 42,821,310] | yes |
| wave | 38,751,307 | 38,816,908 | 1.0017 | [38,160,758, 39,473,058] | yes |

The resident build's last span now measures 690,035 cycles and 34,339 counted instructions, and
the spans hold 102 % of its uninterrupted waiting cycles (1,054,252 against 1,029,301). The wave
build's last span had drained inside its region before; it now also runs on to the exit, 9,800
cycles more.

**An image carries what the program last used, not only its architectural state (format 5).**
An image restored with every cache, translation buffer and branch-target buffer empty took up to
1.65 times the uninterrupted run's cycles over the same instructions, on a program whose last
phase reads every line once: lines held Modified in the 16 MiB last level (four 4 MiB slices) were
in DRAM, and no warm-up inside such a phase can refill them. A format-5 image therefore carries a
recency record in a form independent of any cache's geometry (`tools/sampling/IMAGE.md` §6b and
§7a): four lists in order of last use — every agent's 64-byte lines, the host's own lines, the
host's data pages and its taken branches — with each line's dirty horizon, the deepest position
it reached in its list after its last write, and for the host's lines a read horizon counted from
the last read. Replayed oldest first into any least-recently-used cache, a list leaves each set
holding what the uninterrupted run held, and a line is dirty in a cache of C lines exactly when
the larger of its horizon and its position is below C; this is the memory timestamp record of
Barr, Falsafi and Hoe (ISCA 2005), captured at the image instead of run between regions as
SMARTS's functional warming is (Wunderlich et al., ISCA 2003). Before the clock starts the restore
installs data pages in the host's 2,048-entry second-level TLB, each at the size of the leaf
that maps it (a `G` entry for a grain-partitioned page, §2.3); host lines in its 2 MiB 16-way L2
(32,768 lines) as Modified, shared or exclusive by their horizons, with the directory recording
each copy and the L2's record of which first-level cache holds a line set from the read horizon;
every agent's lines in the slice that owns them (262,144 lines in all); the page table's leaf
lines, which for a grain page is the one line holding its grain's entry; and the taken branches in the fetch branch-target buffer. The record is capped at 524,288
lines (twice the 16 MiB last level), 131,072 host lines, 16,384 pages and 16,384 branches; the
producer runs 2.1 to 2.5 times slower and images grow 10 to 35 %. Restored from each of 19
images, the dictionary now takes 0.997 to 1.026 times the uninterrupted cycles, and every sampled
estimate's interval contains its whole run at both sizes. What no image can carry — the core's
predictors and queues, the memory-dependence predictor, the prefetchers' training and the DRAM
row buffers — is warmed for a length measured per program: the smallest distance from the restore
beyond which every band of pooled excess is within 1 % (`tools/sampling/warmup_rule.py`, recorded
in `warmups.json`). `NMFC_WARM=0` gives the cold restore.

**An image can hold a worker asleep at a `WAIT` (format 6).** The functional producer parks an
invocation at a `WAIT` whose arm matches and wakes it only when a store or atomic is performed
to the armed line, so an image may be taken while workers sleep. Format 6 is format 5 with
96-byte tracking-unit records: the 88 bytes of format 5 followed by the armed line's virtual
address, non-zero exactly when the entry is parked at a `WAIT` and not yet woken; an image in
which nothing waits is still written as format 5, byte for byte. The image checker requires of
every such record that the word at its program counter is a `WAIT`, inside the kernel wait
range, naming a register whose line is the armed one, in readable memory, on an outstanding
entry. The cycle model restores such a worker **on the tile that owns its armed line**, whatever
the placement policy (`placedAtArm`), so the worker's re-read after its `WAIT` completes (the
arm does not survive a restore) is local; restored without the line, the same image's four
workers made three migrations. Because the loop around a `WAIT` runs once per wake, real or
spurious, its instructions are counted apart as every wait loop's are, and a `WAIT` outside
the kernel wait section stops the producer.

**The axis counts the function cores' instructions too: the total.** The counted-instruction
axis counts the host's instructions only, and a program whose host forks and collects while
its function cores do the work has almost nothing on it: the compiled dictionary at 677,205
insertions retires 137,384 counted instructions while its function cores execute 184,255,498,
so its axis had no positions where its time goes, its warm-up was 48 % of the axis, and it
could not be sampled at all. The default axis is now the **total**: the counted host
instructions plus the function cores' instructions executed outside the kernel wait section
(a resident worker's poll of its queue, the one function-core loop whose length is the
machine's). The functional producer places images on it (`--interval-total`); because it runs
each invocation to its end inside its `FORK`, an image is taken after the host instruction that
carried the total to a multiple, never inside an invocation, and records where it really is.
In the cycle model each tile counts an instruction when it issues and takes it back when the
instruction is dropped to be issued again after a migration or a fault, so the count is of
instructions executed; the host unit's window adds the tiles' count to the core's, closes the
region when the total reaches its end on whichever side executes, and ends the run there, so
the tiles stop with the region instead of draining its invocations. The function-core count is
the producer's exactly wherever each invocation's work is fixed by the program: 1,552,308 in
both simulators over a whole program of the offloaded graph search, 3,683,846 over the compiled
dictionary at 8,192 insertions (16,771 instructions issued twice after a migration were taken
back) and 184,255,498 at 677,205. The hand-written hash table is not such a program — its
lookups walk chains in the order concurrent inserts landed — and differs by at most 4.8 in
100,000, less than its host's own counted instructions do. On the total there is no second
stratum for sparse waits, because a wait is time in which invocations execute and so has width.
The counted axis stays selectable. Re-validated on this build (`tools/sampling/PROGRESS-AXIS.md`
§8.8), with each program's uninterrupted run:

| program | N (total) | W | U | regions | estimate ÷ whole run | interval contains it |
|---|---:|---:|---:|---:|---:|---|
| dictionary, 8,192 inserts, wave | 6,430,328 | 262,144 | 16,368 | 60 | 0.9965 | yes |
| dictionary, 8,192 inserts, resident | 6,467,723 | 262,144 | 16,363 | 60 | 1.0267 | yes |
| dictionary, 677,205 inserts, wave | 214,601,898 | 4,194,304 | 4,193,632 | 20 | 1.0285 | yes |
| dictionary, 677,205 inserts, resident | 219,768,822 | 3,145,728 | 8,387,200 | 20 | 0.9879 | yes |
| compiled reduction, 4 MiB, replicated | 6,217,337 | 262,144 | 130,656 | 20 | 1.1235 | no |
| compiled dictionary, 677,205 insertions, folded | 184,392,882 | 1,048,576 | 524,258 | 20 | 1.1066 | no |

The replicated reduction's estimate was 0.23 of its run on the counted axis, which had 53,881
positions for a program whose time is on its function cores. What is left in the last two rows
is not the axis. An image of the eager producer holds every forked invocation complete, so a run
restored from it has in flight only the invocations not yet forked, where the uninterrupted run
has many part-done. Short invocations refill within a few rows and the difference is gone; long
invocations forked together do not, and restored from the compiled dictionary's last four
images, deep in its lookup phase, the folded build runs 1.04, 1.15, 1.48 and 2.9 times the
uninterrupted cycles and never converges. No warm-up removes that. What would is an image that
holds the invocations in flight part-done, each with its context and program counter, restored
as a migrated context is; the cycle model already restores a mid-invocation context (a worker
parked at a `WAIT`), and the missing piece is a producer that leaves invocations part-done and
a rule for how far each has got, which is a design question.

**The answer path at 677,205 insertions.** The compiled dictionary with 128 contexts per engine,
its answers folded into result lanes against the same source compiled to write an answer
stream, was sampled on the total with the fewest regions whose calibrated design reaches a 3 %
standard error: three per arm, the first interval measured whole. Result lanes ÷ answer stream
(time) came out 1.098, Fieller interval [0.902, 1.373]. The two arms' first two regions have the
same rate to 0.1 %, and the whole difference is the third region, restored from image 17 in the
lookup phase, which runs 11 % slow on the folded build and 25 % fast on the stream build — the
defect above. Both arms were also run uninterrupted, as validation runs of the sampler: **result
lanes 35,469,623 cycles, answer stream 36,578,222, a ratio of 0.9697** — the folded path is 3.0 %
faster, as it was at 152,917 insertions (0.9814), and both sampled intervals contain their runs.
The compiler's rule stands.

**A measured build prints nothing the program did not ask for.** The compiled programs used to
print a line for every run-time choice, a summary per phase and the ring's counters, and those
characters were host instructions charged to the program: 17 to 23 per invocation on the graph
search. They are now a build option, on in a checking build and off in a measured one, and with
them off the emitted code also drops the bookkeeping kept only to be printed (per-owner trip
counts, retry totals). A number taken from a measured build therefore counts only the program's
own host work and the library's staging, taking and planning.

### 4.2 The graph search

The program searches an undirected random graph of 64-byte vertex records with mean degree
eight, choosing at each level between scanning the frontier forward and scanning unvisited
vertices backward. The step the offloaded build gives to the tiles is a **vertex range**: an
invocation owns a contiguous block of vertices and generates its own work from its two
bounds.

The first table is one step offloaded, each size run to its own end except the largest, with
the earlier campaign's figure beside it for continuity:

| working set | host, ms | offloaded, ms | speedup | earlier campaign |
|---|---|---|---|---|
| 0.25 MiB | 0.1042 | 0.1398 | 0.75x | 0.73x |
| 1.00 MiB | 0.4669 | 0.5197 | 0.90x | 0.89x |
| 4.00 MiB | 1.7231 | 0.5636 | 3.06x | 3.03x |
| 16.00 MiB | 17.6775 | 5.4987 | 3.21x | 3.68x |
| 32.00 MiB | 30.3138 | 6.6652 | 4.55x | 6.50x |
| 64.00 MiB, sampled | 222.45 est | 148.40 est | 1.499x | 1.490x |

Below about 4 MiB the offload does not pay, because the host's own caches hold the graph.
The 64 MiB row is a whole-program ratio from sampled windows rather than a single step's
speedup, so it is not comparable with the five above it.

The program was then redesigned so that every level runs on the engines, and three costs
that had previously been charged to offloading were removed — all three in the *program*,
none in the machine: the host no longer empties a shared record between levels, a claim is
recorded where the claimed vertex lives rather than in one place, and a level too small to
be worth a fork wave is not offloaded at all. Traversal time in host cycles, summed over the
levels that settle at least one vertex, each level measured in its own window:

| program | 256 KiB | 1 MiB | 4 MiB | 16 MiB | 32 MiB | 64 MiB |
|---|---|---|---|---|---|---|
| host alone, the baseline | 361,101 | 1,595,518 | 6,211,294 | 52,892,031 | 95,991,586 | 263,095,327 |
| one step on the engines | 1,286,926 | 1,476,781 | 1,761,110 | 7,422,814 | 21,950,151 | 45,520,182 |
| every level on the engines, redesigned | 1,533,140 | 1,771,073 | 1,767,127 | 7,138,470 | 14,243,477 | 35,970,469 |
| redesigned, small levels placed by count | 1,286,441 | 1,473,376 | 1,608,669 | 7,036,950 | 14,005,272 | 35,712,189 |
| speedup of the last row over the baseline | 0.28 | 1.08 | **3.86** | **7.52** | **6.85** | **7.37** |

The redesigned program is the fastest at every size, and from 4 MiB up it is 1.10, 1.06,
1.57 and 1.27 times the one-step build. Against the traversal on the host alone it reaches
**7.37 times** at the largest size measured. At the two smallest sizes both offloaded builds
lose to the host and neither should be used.

Where the gain is, at 32 MiB, is one level: the largest top-down level costs 3.42 million
cycles in the redesigned program where it costs 11.05 million on the host. And the largest
single item turned out not to be a property of offloading at all — half the travel previously
charged to a migrating claim was the program's own doing, because a claimer had already moved
to the vertex it was claiming and then moved again to record the claim somewhere else.
Migrations per edge fell from 0.045 to 0.019 at 16 MiB and from 0.145 to 0.093 at 32 MiB, and
a load on an engine that waited 98.1 cycles now waits 20.4 — *below* the one-step build's 21.5
— while the engines sustain 3.30 instructions per cycle of their four issue slots against
2.86.

The general rule that came out of it is worth more than the numbers: **a level's record must
be owned by the same unit of work that owns the vertices.** What made the earlier arrangement
slow was not that the engines are slow but that one array was placed without asking who would
write it.

One cost is left and it cannot move to an engine: between two levels the host turns the record
the last level wrote into the tables the next one probes, and that pass is **10.2 %** of the
traversal at 4 MiB against **1.8 %** for issuing the next level's invocations. It is a scan
because the only account of which vertices a level settled is a bitmap, and a bitmap must be
read in full to be searched; it is proportional to the graph rather than to the frontier, so a
level that settles nine vertices out of sixty-five thousand still costs 6,678 cycles to
summarise. The fix is algorithmic — give the level a compacted account as well, a per-owner
list of vertex numbers appended by the same unit of work that sets the bit, and keep both,
choosing between them by the count the program already has.

### 4.3 The shuffled sum

The program sums a large array through a permutation, so consecutive iterations touch
unrelated cache lines and the access stream is a dependent chase rather than a stream. Two
memory formulations are swept: **grain**, where each region sits on a single tile, and
**striped**, where one region is spread a grain at a time across all four. `K` is how many
chain elements one invocation walks.

| formulation | K | 256 KiB | 1 MiB | 4 MiB | 16 MiB | 32 MiB | 64 MiB |
|---|---|---|---|---|---|---|---|
| grain | 64 | 2.02x | 2.14x | 4.56x | 4.56x | 6.85x | 7.33x |
| grain | 256 | 0.82x | 2.14x | 4.54x | 4.57x | 6.87x | 7.33x |
| striped | 64 | 1.22x | 1.32x | 4.64x | 6.36x | **10.11x** | **10.14x** |
| striped | 256 | 1.18x | 1.32x | 4.65x | 6.37x | 10.12x | 10.12x |

All 72 runs check their own answer, and every checksum is byte-identical to the same run's in
the previous campaign; nothing in the sweep moved by more than 1.2 per cent. Striping wins at
and above 4 MiB because it uses all four memory channels; the invocation's chain length barely
matters, which says the program is waiting on memory and not on dispatch.

### 4.4 The chained hash table

The table is 65,536 buckets of separately chained entries. `P0` to `P4` are load points —
0.125, 0.333, 2.333, 10.333 and 21 entries per bucket, with longest chains of 3, 5, 11, 26 and
41 — and `B` is how many operations one invocation batches. **sep** runs lookups and inserts
in separate passes; **int** interleaves them.

| load point | phase | B | host, ms | offloaded, ms | speedup |
|---|---|---|---|---|---|
| P0 | sep | 128 | 0.179 | 0.279 | 0.64x |
| P0 | int | 32 | 0.341 | 0.404 | 0.85x |
| P1 | sep | 128 | 0.449 | 0.653 | 0.69x |
| P2 | sep | 128 | 7.302 | 4.123 | 1.77x |
| P2 | int | 128 | 9.636 | 5.461 | 1.76x |
| P3 | sep | 128 | 175.562 | 19.664 | 8.93x |
| P3 | int | 128 | 165.019 | 24.997 | 6.60x |
| P4 | sep | 128 | 1326.890 sampled | 52.187 | **25.43x** |
| P4 | int | 128 | 1047.814 sampled | 55.682 | 18.82x |

The shape is the result: below a load factor of about two the offload does not pay, and above
it the gain grows with **chain length** rather than with the size of the table. A short chain
is one dependent load the host's own caches can hold; a chain of 41 is a walk that belongs
beside the memory.

### 4.5 The memory link

The two link settings are compared at points chosen to be bandwidth-bound and latency-bound.

| workload | point | working set | parallel bus | serial link | change |
|---|---|---|---|---|---|
| shuffled sum | striped, K = 64 | 64 MiB | 10.14x | **19.84x** | +95.6 % |
| shuffled sum | striped, K = 64 | 32 MiB | 10.11x | **19.50x** | +92.8 % |
| graph search | one step offloaded | 32 MiB | 4.55x | 6.56x | +44.1 % |
| graph search | one step offloaded | 16 MiB | 3.21x | 3.34x | +3.9 % |
| hash table | P3, sep, B = 128 | 16 MiB | 8.93x | 8.97x | +0.5 % |

A serial attachment with twelve channels behind it nearly doubles the shuffled sum's speedup,
because that workload is bandwidth-bound and the host pays the added latency while the engines
do not. It changes the hash table by half a per cent, because that workload is latency-bound
on both arms. The engines' insensitivity to link latency is the property being demonstrated;
the host's sensitivity is the price.

### 4.6 The three changes of this round, measured

Three changes to the machine are recent enough that their before-and-after belongs here. Each
landed with a directed test, its own counters and a measurement on the workloads that exercise
it, and the result is that two of the three are free and one corrects a configuration error.

The tracking unit now covers every context, and on the graph search at 16 MiB that changes
nothing measurable: 343.08 cycles per vertex settled before against 344.30 after, each inside
the other's interval. The counters say why — the unit's occupancy peaked at 24 entries of the
256 it had, and no invocation was ever refused. On the shuffled sum at 32 MiB the unit
**was** full for 5,552,276 of 46,333,871 host cycles, and enlarging it still changed nothing:
the two runs are bit-identical window by window, because the program holds 256 invocations
outstanding and then collects one before starting the next, so it never asks for a 257th. A
full unit costs nothing unless an invocation is attempted while it is full, and none ever was.
Enlarging it was still right — a machine that cannot address half its own contexts is a
configuration error whatever today's program does — and a program reaches past the old number
only by being rebuilt, or by asking the machine at run time how deep its unit is, which is
what `FORKQ` is for.

Banking the tile's instruction and data caches is free to measurement on both workloads:
342.93 cycles per vertex settled against 344.30 unbanked on the graph search, and 111.709
cycles per sum against 111.740 on the shuffled sum — each far inside its own interval and of
the wrong sign to be a cost. What the banking does is visible in its own counters, summed over
the measured regions across all four tiles:

| point | I-cache accesses | I-cache conflicts | answered by another's read | D-cache accesses | D-cache conflicts | data banks busy, busiest cycle |
|---|---|---|---|---|---|---|
| graph search, 16 MiB | 19,642,983 | 0 | 1,504,765 | 6,841,233 | 59,201 | 4 of 4 |
| shuffled sum, 32 MiB | 8,783,606 | 0 | 2,789,689 | 4,848,551 | 263,548 | 4 of 4 |

The instruction cache never conflicts on either workload, because a bank read returns a line
and four pipes fetching out of one line share it; between a seventh and a third of its
accesses ride along on another's read. The data cache conflicts on 0.9 % of accesses in the
search and 5.4 % in the shuffled sum, and in both its busiest cycles use all four banks, which
is the throughput it was sized for.

The last-level slice's banks are now checked against the memory device's in both
configurations rather than assumed: 32 banks per rank, 32 slice banks, one partition. Measured
on the four-tile machine running the small search, the slice reports 118 bank conflicts and the
memory behind it reports an access spread across banks of 1.25 with **no bank never accessed**,
which is the statement that the two partitions are one and that the traffic uses all of it.

A defect the new counters found is worth recording because of what it says about reading
counters. The tracking unit keeps a count of entries holding finished work the host has not
collected, and that count was wrapping to 2^64−1 on every run started from an image — which is
every sampled region of every measurement in this project. An image's returned entries were
being installed by assigning state directly instead of through the one function that maintains
the count, so a restored entry was in the returned state and the count did not know it, and the
first collection decremented a count that had never been incremented. Nothing in the machine's
behaviour reads that count, so no cycle figure anywhere is affected; the companion counter for
host idleness happened to read almost the same number before and after, because on that program
there genuinely is finished work waiting nearly all the time. A conclusion drawn from it was not
wrong — it just was not being measured. The fix routes the restore through the state transition,
and a directed test resumes from an image and requires the count to have stayed inside the
unit's capacity, which a wrapped count cannot pass by accident.

### 4.7 The open question: dispatch and utilisation

This is where the machine now stands, and it is the thing the next round is about.

**The engines are nearly empty.** On the graph search at 16 MiB, measured over the sampled
regions of the whole program, the mean number of live contexts per engine, split by state, is:

| engine | live contexts of 128 | executing | runnable, not issuing | asleep on a load |
|---|---|---|---|---|
| tile 0 | 2.86 | 0.33 | 1.60 | 0.87 |
| tile 1 | 2.40 | 0.27 | 1.23 | 0.85 |
| tile 2 | 0.65 | 0.08 | 0.27 | 0.28 |
| tile 3 | 0.65 | 0.08 | 0.27 | 0.28 |

The three states exclude one another and sum to the live count. Six and a half contexts are
live across the whole machine, of the 512 that exist. On the shuffled sum at 32 MiB the
engines hold 5.1 to 6.0 live contexts each of 128, nearly three quarters of them asleep on a
load. Those two averages are taken over the *whole* program, including the phases in which
the host is doing its own work; averaged over only the windows in which a level is actually
running on the engines, the graph search holds 44 to 51 live contexts per engine of 128, so
the engines do fill when work is present. Both readings are true and they are different
quantities — the first says how much of the program's time the engines are used at all, the
second says how well they are used while they are used.

**An entry in the tracking unit is not a live context.** On the same graph-search measurement
the unit holds 23.3 entries while the machine holds 6.5 live contexts: roughly seventeen of
those entries are invocations that have already finished and are waiting for the host to
collect them. On the shuffled sum the unit holds 31.7. The redesigned search at 32 MiB holds
208 of 256 entries outstanding with a finished result waiting in 58.9 % of cycles, and that is
*not* by itself a host bottleneck: the number of vertex ranges equals the unit's entry count at
every size from 16 MiB up, so every range of a level is started before any of them returns and
an uncollected entry blocks no start. What it does cost is the tail of a level, because the
level ends when the host has collected the last of its 256 — a few thousand cycles a level,
and under thirty thousand over a traversal.

**The counter that does point at the host is idleness with work waiting.** On the graph search
the host ran no offload instruction while finished work was waiting for 93.8 % of ticks. Put
beside the engines' 6.5 live contexts of 512, the reading is that on this program the machine
is limited by the rate at which the host starts and collects work, not by the engines'
capacity to run it — and that rate has two components worth separating: the per-invocation
cost on the host of writing a context, starting it, testing for completion and reading the
result back, and the rule by which a start instruction may issue at all.

**Long-lived workers on `WAIT`, measured against waves.** The dictionary (the line-resident
bucket table, separated phases, 677,205 inserts, four tiles, out-of-order host) was run in two
dispatch shapes with 128 workers per tile: **waves**, a fork per chunk and a join, and
**resident workers**, forked once, each sleeping at a `WAIT` on the table's tail word between
chunks. The worker's loop stores its progress (which clears its arm), loads the tail (which
arms it), works if the tail moved, and otherwise `WAIT`s on the tail and reloads it. Both arms
take the same input and give the same answer; each is estimated on its own counted-instruction
axis from its own images (20 regions of 12,053 counted instructions, warm-up 100,000, all 32
sparse waits measured whole), and the ratio's interval is Fieller's. The pair cost 104 runs,
about 10 minutes of wall time.

| arm | counted instructions | IPC | estimated cycles | 95 % interval |
|---|---:|---:|---:|---|
| waves | 123,589,977 | 3.2916 | 39,644,118 | [38,987,022, 40,301,214] |
| resident on `WAIT` | 123,517,869 | 3.0064 | 42,142,808 | [41,407,350, 42,878,267] |
| **waves ÷ resident** | | | **0.9407** | **[0.9191, 0.9629]** |

The sampler's second estimator, absolute totals from the same regions, gives 0.9129
[0.8910, 0.9354], the same direction. Resident workers are about 6 % slower. The same
workload's resident workers that polled instead, measured the same way at 32 contexts per
engine, were 6.4 % slower (0.936). `WAIT` removed the polling almost entirely and the loss
stayed, so the polling was not the loss:

| counter, sampled regions / spans | polling resident (32 contexts) | resident on `WAIT` (128) | waves (128) |
|---|---:|---:|---:|
| tile instructions issued in the wait section | 1,118,398 / 72,931 | 1,024 / 66,048 | 0 / 0 |
| memory-queue ordering stalls | 5,894,762 / 822,158 | 101,833 / 1,422,148 | 127,649 / 32,962 |
| host memory-wait cycles | 175,489 / 1,615,960 | 181,099 / 1,125,326 | 64,801 / 13,947 |
| host cycles in the spans | 852,212 | 1,237,142 | 2,256,590 |

The host's memory wait sits almost wholly in the opening region in both arms (181,060 and
64,774 cycles); in the other 19 regions it is 0 or 13 cycles. Those 19 regions are the host
filling the next chunk while the tiles are nearly idle (0.85–0.92 instructions issued per tile
per cycle in both arms), and they take about 9 % more cycles in the resident arm, 75,719
against 69,080, for the same counted instructions. The polling build's regions have the same
cycle counts region by region whether or not its workers were polling in them. The host
counters that differ in those regions are the first-level cache's miss-status occupancy
(1.37×), cycles held by a full reorder buffer (1.19×) and fetch-queue flushes (1.43×); they
locate the difference on the host's fill path and do not yet name its cause. Where the
resident shape wins is in the waits: 1.24 M host cycles in its sparse waits against the waves'
2.26 M, memory-bound (reading 512 progress words) where the waves' are join-bound.

| live contexts per tile, regions / spans | resident on `WAIT` | waves |
|---|---|---|
| live, of 128 | 124.3 / 119.3 | 29.2 / 50.6 |
| executing | 0.85 / 3.60 | 0.92 / 1.69 |
| runnable, not issuing | 24.7 / 100.9 | 26.5 / 46.7 |
| asleep on a load | 1.42 / 9.47 | 1.65 / 2.00 |
| asleep at a `WAIT` | 97.2 / 4.8 | 0 / 0 |
| fetching, walking, other | 0.12 / 0.52 | 0.12 / 0.19 |

Each publication woke all 128 workers of a tile (124 broadcasts, 15,872 wakes, over the spans),
and their re-reads of the one tail line account for the spans' 1.42 M ordering stalls: the
fan-out limit §2.5 names, the one place where `WAIT` gathers traffic that polling spread out.
The ratio is a measurement of this workload, not a verdict on dispatch shape: which shape a
task uses is chosen per task from its own measured pair. At the smallest size (8,192 inserts,
four tiles) the resident build's answer digest equals the functional run's at 32 and 128
contexts, the table verifies, and missed wake-ups, sleeps before translation and sleeps with a point open are
all zero; both runs are in the coherent suite.

The general statement of the problem, then: **one host core keeps far fewer contexts alive
than the machine has places for.** The architecture is defined; what is not yet known is how
to keep it full. Three directions follow from the counters rather than from argument — longer
lived invocations, so that a context stays live instead of being created and destroyed per
level; an instruction layout in the program that keeps starts and collections flowing instead
of batching them into waves with a barrier at each end; and a cheaper per-invocation path on
the host. None of the three is a change to the tile core, which is why §5 treats utilisation
and the tile's internals as separate pieces of work.


**The host's work per invocation in the compiled programs.** Counted host instructions (those
outside the waiting section) from the functional simulator, per invocation, before and after the
library's host path was reworked:

| program | invocations | staging | taking | planning per phase | planning once per program | log lines | total |
|---|---:|---:|---:|---:|---:|---:|---:|
| graph search, 65,536 vertices, before | 1,018 | 25.5 | 25.6 | 37.7 | 39.5 | 19.9 | 148.2 |
| graph search, 65,536 vertices, after | 1,018 | 14.0 | 12.0 | 15.8 | 34.3 | 0 | 76.1 |
| dictionary, 8,192 insertions, before | 1,792 | 32.9 | 34.5 | 5.2 | — | 2.4 | 75.0 |
| dictionary, 8,192 insertions, after | 1,792 | 32.9 | 31.0 | 3.7 | — | 0 | 67.5 |

The hand-written graph search takes a result in 15 counted instructions and stages its lanes
inside its waiting section (about 24 per fork, uncounted), but rewrites its duplicated tables
every level: 403,456 counted instructions at this size, 788 per invocation, which the compiled
program does not pay. The hand-written dictionary does its hashing and queueing on the host: 1,021
counted instructions per invocation, 128 per operation. The compiled graph search's remaining cost
is mostly its one-time weighted cuts (22.6 per invocation). Uninterrupted cycle-accurate runs
give the same answers before and after: the graph search at 4,096 vertices runs 2.3 % faster
(4,391,035 to 4,289,344 host cycles); the dictionary at 152,917 insertions 0.3 % faster
(7,835,957 to 7,811,670); the dictionary at 8,192 insertions 0.8 % slower (828,456 to 834,933),
where a build that only adds the log lines back lands at 831,715, so at that size the end moves
with any change in the host's timing rather than with its amount of work.

---

## 5. What comes next

The majority of the work is not defining the architecture. It is using it, and finding where
the bottlenecks emerge — in hardware or in software — and overcoming them. Five pieces follow,
in the order their dependencies allow.

**The first three of them are done.** The instrument was built and was what closed the pipe
question; the pipe stages exist; and the memory path landed in one change with the mechanism
it replaces deleted in the same change, as step 3 required. They are kept here as written
because each says what the step was for and what it was allowed to cost, and §2 and §3 say
what came of it. Steps 4 and 5 are the live ones, and §2's own open questions — the block
instruction slot, the relaxed store rule, the walk path, the window against the bank count,
and a fairness bound at the serialisation point — are the substance of step 4.

**1. Instrument the pipes on the model that exists.** The readiness and conjunction counters
of §2.2 need no new mechanism: they turn two existing counts that measure scheduler
examinations into context-cycle censuses, and add the conjunction that decides the pipe
question. They come first because they are the only part of the tile design that can run today
and because they decide what the pipe model must support. One of them — the histogram of
load-free runs — is validated against a kernel whose memory accesses occur at known fixed
intervals, so the instrument is checked before it is used to decide anything.

**2. Build the pipe stages, the default rule first.** `N × M` stage registers, the three named
stages, and the default issue rule. A directed test asserts that no two stage registers in one
cycle hold the same context, which is the rule that removes forwarding and is checkable rather
than assumed. The pipe-bound alternative is built only if the conjunction counter says there
were slots to recover; if it is built, it stays behind a parameter beside the default so the
comparison can be re-run.

**3. Build the memory path once, and delete the mechanism it replaces in the same change.**
Translation queues with a completion rate derived from the pipe count and reserved capacity for
a walk; the delivery window; the per-bank memory queues with ordering, forwarding,
read-modify-write at the bank and coherence requests at the bank. The word-keyed table above
the data cache goes in the same change, with its directed tests rewritten to assert the new
counters and the absence of the old ones, so that no interval exists in which both mechanisms
are present. The existing tests keep their programs and their assertions: the kernel that
writes different words of one line must still see every word survive, the kernel that loads a
word it has just atomically updated must still see its own update, and the regression in which
a host write between phases must be seen by the engines must still hold — each now a
consequence of where the ordering point sits rather than of three mechanisms cooperating.

**4. Sweep the questions the design deliberately left open,** each as a configuration sweep
with every point correct rather than by removing a mechanism: the instruction slot at 1, 4, 8
and 16 instructions; the window width from the pipe count up to the bank count, which walks
continuously from the accepted mechanism to the crossbar and prices the difference; queue depth
from 2 to 32, which must show a curve; bank count at 8 and 16; and the store slot-release rule.
A null result is a result and is explained with counters — which constraint actually bound, not
prose.

**5. Attack utilisation, which is where the measured machine is losing most.** The engines hold
single-digit live contexts of 512 over whole programs while the host is idle with finished work
waiting for 94 % of ticks. That is not a tile-core problem and it will not be fixed by anything
in §2. The work is: report live contexts per engine by state and the host's per-invocation cost
split into its parts, never from tracking-unit occupancy; price long-lived invocations that stay
resident across phases against the fork-wave-per-phase shape on each workload, as the
dictionary's pair now has (§4.7: resident workers on `WAIT` 6 % slower, the difference on the
host's fill path, cause not yet named); examine the
rule by which a start instruction may issue, which currently halves the rate at which the host
can feed the engines; and carry the graph search's remaining algorithmic cost — the
whole-graph scan between levels that is 10 % of the traversal — by giving a level a compacted
account of what it settled beside the bitmap.

Two pieces of the machine are named as remaining rather than worked around: one low-complexity
queue per bank at the memory controller, which is the second half of the bank-alignment rule
and changes the memory timing every figure here was taken under; and an operation that moves
the whole 512-bit context, which would let `r1` be an operand and is an instruction-set
extension rather than a naming change, with a legality equation to re-derive so that whatever
stays reserved still traps.

Three standing method rules apply to all of it, and they are what make the results above
comparable. Every performance number comes from the sampler, with windows covering the same
program *work* across arms rather than the same instruction count, every point of a sweep
launched together rather than serially, and no end-to-end run of about an hour. A new feature
is proven with its own counters on a workload that exercises it, and a surprising null effect
is explained with counters rather than accepted. And a fix keeps the mechanism it touches: a
fix that serialises — a hold, a pin, a drain — is provisional until the real mechanism replaces
it, and it carries a before-and-after on the workloads that exercise it.

---

## 6. Where the numbers in this document come from

Every figure in §4 is one of three kinds, and each is identified where it appears: a program
run uninterrupted to its own end, which is used for correctness gates and for validating the
sampler; an estimate from sampled windows with its interval; or a counter summed over those
windows. Every run checks its own answer and prints a pass line, and a run that does not is not
a measurement. The two campaigns whose figures appear side by side in §4.2 share every
component below the coherence fabric as the same binaries, and the check that says so is that
every answer digest is byte-identical between them.

Sizes and counts in this document are configuration. Where a default is given it is the value
the measurements were taken at, not a constraint on the design.

The mechanisms described in §1.6 and §2 come from four records, and each states its own tests,
its own counters and the price of every batch it ran:
*The tile core: contexts, pipes, translation and memory queues*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/tilecore/TILE-CORE-DESIGN.md`), the design;
*The tile core's memory path: what it is, what replaced what, and what the counters say*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/tilecore2/TILE-CORE-BUILD-2.md`), the build;
*A host and a function core sharing one word, and a reservation that ends with its owner*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/complete1/FIX-R1-R2.md`), the coherence rules
of §2.5; and *Three corrections to the simulated machine*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/fix3/MACHINE-FIX-3.md`), the fabric, the
miss-status files and the host's dependence predictor. The host's cache-block clean (§1.4.7)
and the `FORK.M` start-latency table of §2.1 come from *The host's cache-block clean, and what
a FORK.M start costs* (`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/libbuild/CLEAN.md`).
The 4 MiB rows of that table are uninterrupted runs, one of them the warm-up measurement's own,
repeated with the split counters added; they are read for the division of the start latency
and not quoted as program times.
One leaf per grain and the two-size translation buffers (§2.3), with the before/after table there
and the `FORK.M` row of §2.1, come from *One leaf per grain*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/complete5/LEAVES.md`). Its 4 MiB reduction rows
and the hash table's are uninterrupted runs of a few minutes, read for their counters and for how
the start latency divides, each arm the same binary with one library changed; the graph search's
figure is its level-7 sampled window; the suite rows are the coherent suite's own runs.
The total axis of §4.1, its validation table and the answer-path pair at 677,205 insertions come
from *The sampling axis counts every instruction executed*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/complete5/AXIS.md`). The whole-run cycle counts
there are uninterrupted runs made to validate the sampler, three of them longer than the
900-second limit and run under its one-hour allowance for that purpose; the pair's two
uninterrupted runs are also validation runs, and they, not the three-region estimate, decide it.
The host-work-per-invocation table of §4.7 and the measured-build paragraph of §4.1 come from
*The library runtime's host work per invocation*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/complete5/RUNTIME.md`). The per-invocation
counts are functional runs, split by function from the basic-block vector; the cycle counts are
single uninterrupted runs at the smallest sizes (and at 152,917 insertions for the dictionary),
run whole as correctness gates because the sampler cannot yet restore the compiled dictionary's
long invocations.
The abandoned-pair rule of §2.5, its before-and-after table and the two host-against-tile results
beside it come from *A point does not outlive its pair*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/complete6/ABANDON.md`). Every figure there is a
directed correctness run, uninterrupted, with its statistics file under
`/mnt/md0/nmfc-work/complete6/lrsc/runs`; the compiled graph search at 65,536 vertices is one
uninterrupted run made to compare its answers and migration rate with the build that wrote the
value back, not a performance number.
The forward-progress rules of §2.5 (the owner's pair served first), their diagnosis, test table
and cost rows, and the two host-core changes come from *A pair against a continuous writer*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/complete6/PROGRESS.md`). Every figure there is
a directed correctness run, uninterrupted or stopped at 2 ms of simulated time, with its
statistics file under `/mnt/md0/nmfc-work/complete6/fp/runs`; the dictionary rows are two
uninterrupted runs per build at load point 0, read for their counters and answers, not a sampled
performance number.
The delivery window's two classes of §2.4, their diagnosis, the before-and-after tables and the
new `FORK.M` row of §2.1 come from *One slot of its own for the physically addressed class*
(`/home/maccoy-merrell/.claude/jobs/0906c103/tmp/complete6/WINDOW.md`). Every figure there is an
uninterrupted run read for its counters and answers, with its statistics file under
`/mnt/md0/nmfc-work/complete6/window`; the reduction's rows are uninterrupted correctness runs at
4 MiB and the level-7 row is one sampled window run whole, not a sampled performance estimate.
