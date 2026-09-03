# NMFC — CANONICAL DESIGN

**Status: Ratified against the user's rulings of 2026-09-02/03; this file is the design. DESIGN.md is the historical record.**

**What this document is.** One authoritative, self-contained statement of what the
Near-Memory Function Core machine *is*. It was produced because the design was
repeatedly re-derived wrong between turns, from the wrong sources. It is written to
be literal rather than short: numbers, sizes, encodings, invariants, and every
rejected alternative *with its reason* are all load-bearing and none of them are
summarised away.

**Two sections come first because they are what the document needs FROM the reader:**
**RULINGS NEEDED FROM THE USER** — **NOTHING REMAINS OPEN; the count is ZERO, down from
forty-nine** — because the user ruled twice, on **2026-09-02** (twenty-one numbered
rulings plus three) and on **2026-09-03** (the last ten), and both sets are applied
throughout this revision — and
**SELECTED CONFIGURATION FOR SIMULATION**, which holds every value this document used to
state as a design constant and now states as a *configured* value with its file and line.
Everything after them is the document proper.

---

## RULINGS NEEDED FROM THE USER

**THE USER RULED ON 2026-09-02 AND AGAIN ON 2026-09-03. BOTH SETS ARE APPLIED IN THIS
REVISION, AND NOTHING IS LEFT OPEN.** They are tier 1, they are the newest words in the
record, and they are binding.

**THE 2026-09-02 SET.** Twenty-one numbered rulings (`R1`–`R21`) plus one on the
instruction count, one on the memory geometry and one on the stress workload **closed
thirty-three of the forty-nine questions this section used to carry outright, and reduced
four more to a single residual clause each**. A thirty-fourth (the 4 MiB LLC slice, old
question 15) was closed not by a ruling but by running the two-command lookup the question
itself named — it returns nothing, so that configuration was never committed. Every
application is cited in the body as **user ruling 2026-09-02 R\<n\>**.

**The arithmetic, so it can be checked: 33 closed by ruling + 1 closed by lookup + 4
reduced to residues (`O4`, `O6`, `O7`, `O9`) + 11 untouched (`O1`, `O2`, `O3`, `O5`, `O8`,
`O10`–`O15`) = 49.**

**And then six of those eleven were closed IN EDITING.** They were never
rulings to ask for: two are simulator facts to be looked up (`O11`, `O10`), two are already
forced by rules this document has adopted (`O14` by Appendix 3 item 8, `O13` by R6–R10),
one is a document-internal inconsistency to reconcile (`O8`), and one is an editing chore
(`O2`). Their dispositions are recorded under the table. **One item was ADDED** — the
privilege level of `RESUME`, which R20 explicitly left as a question and which no `O`-row
carried. **Ten then remained: `O1`, `O3`–`O7`, `O9`, `O12`, `O15`, `O16`.**

**THE 2026-09-03 SET CLOSED ALL TEN.** The user ruled on every one of them. Their words
are quoted verbatim in the table below, they are **tier 1, newest, and binding**, and every
application in the body is cited as **user ruling 2026-09-03 O\<n\>**. Where a ruling has a
consequence the user did not spell out but which follows from it and from an existing
tier-1 rule, the consequence is drawn in the body and tagged
**[derived from ruling O\<n\>]** so it is never mistaken for the user's own words.

**THE ARITHMETIC OF THE CLOSE: 33 + 1 (lookup) + 6 (in editing) + 10 (2026-09-03) = 50,
against 49 original questions plus `O16`, which was added. Nothing is left.**

**THE FINAL COUNT IS ZERO.** This section is now a record of rulings, not a request for
them. **The ids are NOT renumbered and NOT retired**: every existing body citation of an
`O`-number still resolves here, and now resolves to a *ruling* rather than to a question.

---

### THE RULINGS, AND WHERE EACH ONE NOW LIVES

| ruling | what it says | closed | applied at |
|---|---|---|---|
| **R1** | "*delete it. CONT/extend is fine, and can stay.*" — `op::SPAWN` is deleted from ChampSim. | old q9, **L3** | I10, P.2, Appendix 2 D0 |
| **R2** | "*That is fine, relabel is fine. You are correct the defaults should be switched to the physical/nuca router.*" | old q10, **L4**; the relabel half of old q2/**L35** | A.4, F.10, L4, Appendix 2 D0 |
| **R3** | "*I was under the impression that was derived from invoking ramulator now, the `--llc-banks` should be inert. **ChampSim updates stop** until we deem it a good idea to go back.*" | old q11, **L5** — and, by the freeze, old q3, q4, q5, q7, q8, q16 | **D.2**, Appendix 2 **D0** |
| **R4** | "*ChampSim doesn't have a byte model, just a cycles-to-transmit model. No need to back port it, **SST is correct**.*" | old q12, **L7** | I5, J.2, M.4, L7 |
| **R5** | "*That sounds like a bug. Needs investigation. The image being written into memory should itself be trivial, done by the OS at load … If it is impacting us in any way, that suggests a bug.*" | old q14, **L14/L15** — as a **defect in the simulated environment**, not a design question | I14, C.5, L14, L15 |
| **R6–R10** | "*Why would we define values for these in the design doc? **Do ISAs define how many cores they support? NO!** We can build a table showing the selected values when simulating, but it should not be fixed parts of the design.*" | **all of old group B — q19–q29** | **SELECTED CONFIGURATION FOR SIMULATION**, below |
| **R11** | "*Lets do RISCV. x86 was chosen initially since initial develop was on PIN. RISCV is easier.*" | old q32's base-ISA half, **L46** (the *subset* stays open) | **I.0**, I7, K.6 |
| **R12** | "*I am fairly certain real machines have separate page tables per address space? TLBs are shared, page tables themselves should not be shared between address spaces?*" — **one page table PER ADDRESS SPACE, duplicated on every tile; TLBs shared.** | old q34, **L49**; the surface half of old q2/**L35** | **I3**, C.2, F.5a, F.8 |
| **R13** | "*I thought the mode bit was encoded in the page table itself, carried as an extra bit on any physical address? … 5-level page tables are common. PTE layout should inevitably be derived from that. Multi-size pages are already supported in modern hardware, this shouldn't be an open implementation question.*" | old q33 | **F.5a**, E.2 |
| **R14** | "*That was an artifact of the ChampSim design, since ChampSim doesn't model coherence, data, or atomics. By necessity, a core must poll said block to see if the writeback of the data has occurred (coherence propagated). Potentially using atomics.*" | old q30, **L47** | **I.11**, C.4, I.7 |
| **R15** | "*Yes, the same structure. … it must enforce atomics. It allows contexts to obtain/release/pass atomics quickly. Capacity is something that must be sized from experimentation. Full table must either be unachievable by construction or safely block (sleep context until possible). … chain bound must be experimentally derived.*" | old q31, **L48** | **H.7**, H.6, SELECTED CONFIGURATION |
| **R16** | "*Okay, sounds correct.*" — `compact`/`expand` are ratified as a **consequence of partitioning at the fabric** (I13, DESIGN §5.6). | old q35 | **C.2**, E.1 |
| **R17** | "*Okay, sounds correct.*" — a remap invalidates cached translations **exactly as a normal TLB shootdown does**; the generation counter and per-grain log are the **simulator's** cheap model of that, not hardware. | old q37 | **F.8** |
| **R18** | "*Possibly, maybe OOM. Unsure. … **channel is odd language here, should definitely be using 'tile'***" and "*the sim print a warning when it happens, but let's not hard-error or anything.*" | the failure-behaviour and vocabulary halves of old q38 | **F.8**, O.4 |
| **R19** | "*size it according to modern systems. Make sure it is not a bottleneck. Look at other modern systems.*" — directory **sizing is configuration**. | the sizing half of old q42 | **C.5**, SELECTED CONFIGURATION |
| **R20** | "*faults should go to the kernel handler, execute, then resume the work just like a normal system. On fault, it is probably necessary to pass the fault to the core via the FUT and have it handle it, then send a 'resume' instruction with the context handle to resume it.*" and "*RESUME needs an extra instruction (privileged???? this is real question, not sure if it needs to be or not).*" | the recoverable-fault half of old q39 | **I.6**, I.3, I.9, C.4 |
| **R21** | "*Undecided, once again something that must be experimentally derived. We start at common and implement tuning/algorithm adjustments as needed.*" | old q40 | **G.4**, SELECTED CONFIGURATION |
| **INSTRUCTION COUNT** | "*RETC and ENDC are the same instruction, with a return bit.*" → **twelve** base instructions; **RESUME** (R20) makes thirteen, one of them privileged. | old q18, **L44** | **I.3**, I.9, A.2, C.4 |
| **GEOMETRY** | "*Ranks are included. 32 banks per channel assumes DDR5 and one rank. … the entire system must adapt to an arbitrary bank count, tile-memory-sizing, grain-sizing. Please do not lock in any bank/rank/column/row/channel counts as if they were the only ones supported. **WE MUST SUPPORT ALL POSSIBLE VALUES FOR EACH, WITHIN A FULL 48-bit PHYSICAL ADDRESS SPACE.***" | old q28, **L8** | **I12**, E.3, E.4, D.2 |
| **L32** | the reshaped stress workload **works**: sum verified against the host, loads / migrations / instructions **25.0% on every tile**, zero stores, **196,904 migrations for 262,143 loads**. The rejected chase shape's 2.5:1 spread was predicted exactly from its addresses (12.5 / 37.5 / 37.5 / 12.5) — congruent routing, not a broken machine. | old q1, **L32** | **G.6**, H.9, N.4, L32 |

---

### THE ONE RULE THAT CLOSED THE MOST QUESTIONS

**Values are not design.** The user stated it as a rule, and it disposes of the whole of
what this section used to call "design values no source fixes":

> "*Why would we define values for these in the design doc? **Do ISAs define how many
> cores they support? NO!** We can build a table showing the selected values when
> simulating, but it should not be fixed parts of the design.*"
> — user ruling 2026-09-02, R6–R10

So: **sizes, counts, capacities, thresholds, chain bounds, backoff parameters and clocks
are CONFIGURATION.** The canon states the **mechanism** and the **constraint** the value
must satisfy; the value itself lives in **SELECTED CONFIGURATION FOR SIMULATION**, below,
per simulator, with its file and line. Every place in the body that used to state one of
these as a design constant now says *configuration; see SELECTED CONFIGURATION*.

**This is not a licence to leave a number unstated.** A constraint is still binding —
`G = row_bytes × banks_per_channel(all ranks) × total_channels` fixes what `G` *is* at any
geometry even though it fixes no digits — and a measurement is still quoted with the
configuration that produced it (Appendix 3 item 8).

---

### ALL TEN ARE RULED — user ruling 2026-09-03, verbatim, and where each one is applied

**NO RULING REMAINS OPEN.** The check is unchanged and only its expected *result* has
changed: `grep -nE '^\| \*\*O[0-9]+\*\* \|'` over the table below still yields
`O1, O3, O4, O5, O6, O7, O9, O12, O15, O16` — **ten rows, deliberate gaps where the six
closed-in-editing rows were, and EVERY ROW NOW READS RULED.** A row that reads anything
else is a regression, not a new question.

| # | the user's ruling, 2026-09-03, verbatim | what it settles | applied at |
|---|---|---|---|
| **O1** | "*Unhinted grains are up to the OS/hardware to place. So, presumably the OS could map it wherever was most convenient.*" | **RULED.** An unhinted grain's placement is the **address-space owner's free choice** — allocator convenience, nothing more. **No partition semantics attach to its virtual address.** `(va >> grain_bits) % num_tiles` is therefore permitted as *one* convenient default a placer may use and is **not** a rule, not a guarantee, and not something any other mechanism may rely on; F.3's delete-on-sight list stops naming it as an architectural partition and keeps naming it as a router. **#269's rejection of virtual-address partitioning stands untouched** — that is about the architecture reading the VA, not about an allocator picking a convenient frame. | **F.3**, **F.8**, A.4a, ledger **L38** |
| **O3** | "*I think this is just a simulator thing and not a meaningful design choice, so I say we describe it as implementation choice.*" | **RULED — option (a).** `funct7`/`funct3` values are **implementation choice**; **the canon does not fix them.** The canon fixes only the **count** (twelve base plus a privileged `RESUME`), the **membership** of the groups, and that `RESUME` gets a slot. SST's `nmfc_isa.h` is **one implementation's** choice, recorded in **SELECTED CONFIGURATION** and never quoted as canon. | **I.9**, SELECTED CONFIGURATION, ledger **L43** |
| **O4** | "*I think we want float, so C.*" | **RULED — option (c): RV64IMAFD.** Floating point is in the subset. **AND THE CONSEQUENCE, STATED CORRECTLY: it does NOT widen the context.** The context is **512 bits, BIT-PACKED** (#232, #238) — *not* eight 64-bit registers, so there is no per-register set to widen. A float occupies bits of the same 512 like any other value (`f64` = 64 bits, `f32` = 32); **the compiler packs them.** Invariants 2 and 11 and the 72-byte migration are **untouched**. A RISC-V encoding names `f0`–`f31` separately from `x0`–`x31`, so the packed file is presented under **two register namespaces over the same 512 bits** — a naming convention, never a second file. **And the namespaces do NOT alias, because a register name is not a fixed bit offset here:** the core implements **512 bits of live storage, not 64 architectural slots**, and the compiler binds each simultaneously-live name — `f` or `x` — to a **disjoint bit range** within them. What is bounded is **liveness in bits across both namespaces together** (invariant 2), never the size of the name space. **Consequence: the machine is an RV64IMAFD target under a register-pressure constraint, not a general-purpose RV64IMAFD core** — a stock unconstrained binary is rejected by the admission test, exactly as one with a stack or a spill already was (I7). **The admission test checks the IMAFD subset and counts liveness in BITS.** | **I.0**, I.7, **K.6**, ledger **L46** |
| **O5** | "*a.*" | **RULED — option (a).** **Three message classes on the ONE fabric — COHERENCE, MIGRATION, FILL — with per-destination queues (H.8).** Arbitration: **COHERENCE strictly first** (I14 makes NMFC priority an ORDER, not a tie-break, and a coherence response the order depends on may not sit behind a fill), **then MIGRATION and FILL at EQUAL WEIGHT** — and that equal weight is exactly what makes invariant 11's 72 B / 64 B byte parity hold. | **C.5**, **H.8**, **J.2** |
| **O6** | "*Presumably the vtile's home would be where we first tried to place it and discovered we couldn't. It should therefore be placed where the next-largest cluster of similar vtiles are, and if none exist, the least-loaded.*" | **RULED.** The spill target is **the tile holding the next-largest cluster of the same vtile**; **if no such cluster exists, the least-loaded tile.** Vocabulary is **tile** (R18). The simulator **warns and never hard-errors** (R18). | **F.8** |
| **O7** | "*I think B, anything else could delay quit until the user program decides to join, which could be forever.*" | **RULED — option (b).** On a **FATAL** fault, the program's outstanding FTU entries **close with a zeroed register file and an error flag**. `JOIN` returns **immediately** with the error. **Nothing waits on the user program**, which is the whole reason: any teardown that waits for a join can wait forever. I.4's "a join-expected entry never closes without returning its values" is satisfied **literally** — a zeroed file plus an error flag *is* a well-formed return. | **I.4**, **I.5**, **I.6**, C.4 |
| **O9** | "*Whichever can scale best. the maximal targeted system is a substantially beefy multi-core system with up to 32 memory tiles. We should expect a LOT of traffic.*" | **RULED by criterion**, and the choice the criterion forces is stated in the body with its scaling argument and prior art, tagged **[derived from ruling O9]**: **an EXACT BIT VECTOR over host cores and tiles, INCLUSIVE of the private caches above the fabric, with BACK-INVALIDATE on eviction.** The alternative (limited pointers with coarse-vector overflow, non-inclusive) loses at this scale and the reason is given. **The sizing target is now a stated number: up to 32 memory tiles plus a substantial host core count.** | **C.5**, SELECTED CONFIGURATION |
| **O12** | "*I think bimodal is fine, since it only ever speculatively issues a single fetch, never executes. It is also essentially free (built into the btb table, tracks a particular branch).*" | **RULED — option (a): ADOPTED.** A **block-granular BTB with a bimodal bit per entry**, used **ONLY** to issue a **single** speculative instruction-stream fetch. **It never executes on the prediction.** It is essentially free because it lives in the BTB entry that already exists. **H.1's clause narrows to "no predictor in the EXECUTION path".** The **never-mispredicts caveat on every measured function-core number in this document STANDS** — nothing about this changes what was measured. | **H.1**, **H.5**, D.6 |
| **O15** | "*a.*" | **RULED — option (a).** Parts **G, K, L and N are HISTORICAL OBSERVATIONS** of an earlier tree. Their **configurations are unreproducible from git**, they are **labelled as such** at every Part preamble and at N.0, and **ChampSim stays frozen** (R3). They are never quoted as evidence about the machine, only as observations of a run that happened. | **G**, **K**, **L**, **N** preambles; N.0; ledger L28c |
| **O16** | "*Yes, privileged.*" | **RULED — option (a): `RESUME` IS PRIVILEGED.** The **kernel** delivers the fault through the FTU and the **kernel** resumes the context — the `sret`/`mret` shape: the party that took delivery of the trap is the party that returns from it. `FORK`, `JOIN` and the rest stay user-level. **Every `[USER TO CONFIRM …]` tag in this document is REMOVED**, because `RESUME`'s privilege level was the only clause that carried one. | **I.3**, **I.6**, **I.9**, **C.4**, A.2 |

**NO RULINGS REMAIN OPEN.** There is no eleventh item, no residue of any of the ten, and
no `[FOR THE USER TO RULE]` tag left live anywhere in this document — the two that were
(`L38`/`O1` and `L43`/`O3`) are both closed above. **The document no longer needs anything
from the reader in order to be implemented.** What it still needs is *measurement*, which
is Part N's and Part O's business and is not a ruling.

### CLOSED IN EDITING — the six that were never rulings to ask for

**None of these was closed by a new user ruling.** Each was closed because asking it of the
user was a category error: it was a fact to look up, a consequence of a rule this document
has already adopted, or a document-internal inconsistency to reconcile. **The numbers are
retired, not reused**; the body statements they were attached to are updated in place.

| # | why it was not a ruling | how it is closed |
|---|---|---|
| **O11** *(closed)* | **A simulator fact, not a design decision.** What N.2 row 3 counts is a property of code that already ran; no answer the user could give would change what the past number means. Same class as R5's "*that sounds like a bug*" — look it up. | **ANSWERED BY LOOKUP: host instructions only.** The per-core instruction figure ChampSim reports is `NMFC_HOST_CORE::sim_instr()` = `num_retired - begin_phase_instr` (`inc/nmfc/nmfc_host_core.h:603`), incremented only at ROB retirement in `src/nmfc/nmfc_host_core.cc:1032`. **The function core keeps a separate counter it never adds in** — `instructions_` (`src/nmfc/function_core.cc:698, 1040`), printed on its own line and exported as its own JSON field at `:293, :328`. So **row 3 is host-only**, 0.31× is the host's share, and it says nothing directly about total work. Recorded at N.2 and I8. |
| **O14** *(closed)* | **Already forced closed by this document's own rule.** Appendix 3 item 8 forbids quoting a number whose unit and configuration cannot be named; no tier-1..3 source states 5.3×'s unit. Option (a) — "state the unit" — asks the user to supply a fact no source contains. | **RETIRED.** 5.3× is not quoted as a quantity anywhere in this document. It survives only as a **named historical artefact** of the earlier §21.3 write-up, always with "unit never stated at any tier, retired under Appendix 3 item 8". **7.38× is the TOP-DOWN kernel's algorithm penalty** and the one that closes *that* kernel's arithmetic (6.73 ÷ 7.38 = 0.91, N.5); the direction-optimising kernel's penalty is 1.08× against a 6.13× gain. **"The algorithm penalty" is per-kernel — never quote it bare** (A.7). |
| **O13** *(closed)* | **Already decided by R6–R10 and Appendix 3 item 8.** "Values are not design", plus the never-quote-bare rule, means a one-run result cannot stand as an invariant. Option (b) is just this document's own rules applied to itself. | **OPTION (b) TAKEN.** Invariant 8 is the **claim shape** — the machine is compared against the reference algorithm doing the same work on a standard core, and the comparison is quoted with its core model and its branch caveat. **The 5.67× lives in Part N (N.2), where the run is tabulated**, and Part B carries it only as the current instance. |
| **O10** *(closed)* | **A frozen-simulator artefact plus a tuning parameter.** The gate is one line of `nuca_router.cc` on a codebase R3 froze, and **R21** already rules that the NUCA algorithm starts at common values and is tuned from measurement. | **RECORDED AS A DEFECT, not ratified as a definition.** `nuca_router.cc:253` uses a denominator — *every grain ever observed* — that **grows as the run proceeds, so the gate loosens on its own**. That is a defect of the frozen implementation, in the class R5 established for L14/L15. Recorded at G.4 rule 3, G.5 and ledger **L42**. The unit and denominator are tuning parameters under R21: **configuration, not canon.** |
| **O8** *(closed)* | **A document-internal inconsistency, not a design question.** H.3 and DESIGN §7 differ in exactly one name — `FREE` versus `RUNNING` — and **H.8 already settles the other half**: the slot is released at departure, so `MIGRATING` occupies none. | **RECONCILED IN EDITING.** The canon list is **FREE, READY, RUNNING, BLOCKED, DONE**, with **`MIGRATING` recorded as a transition, not a slot-occupying state**, on H.8's authority. Both source lists were incomplete in the same way: H.3 omitted `RUNNING`, DESIGN §7 omitted `FREE`, and both promoted a transition to a state. Fixed at H.3 and O.4. |
| **O2** *(closed)* | **An editing chore, not a ruling.** Whether DESIGN.md is renumbered decides nothing about the machine, and the row already contained its own answer. | **`D:line` is and stays the reliable citation**, which is what the row said. Renumbering DESIGN.md is an author's task to do or drop; it is not on this list. Recorded at ledger L13. |

---

### EVERY IDENTIFIER USED ABOVE, DEFINED OR POINTED AT

| identifier | one-line meaning | full definition |
|---|---|---|
| **`N`** | the tile / channel / LLC-slice / memory-controller count. **One number**; a tile *is* a channel *is* a slice. | NOTATION table, D.1 |
| **`W`**, **`Dp`**, **`C`** | the barrel core's pipe **width**, pipe **depth** and **contexts per function core**. `W` is NOT `N`. | NOTATION table, H.2 |
| **`G`**, **`grain_bits`** | the **grain** — `row_bytes × banks_per_channel × total_channels` — and its log2. The unit of physical allocation, of tile assignment, of tagging, and the NMFC page size. **Not a constant.** | NOTATION table, E.3, E.4 |
| **`TILE_PORT`** / **`strict_locality`** | a tile's own port into its own LLC slice, and the assertion bolted to it that every address crossing it belongs to *this* tile. | F.6 |
| **`compact` / `expand`** | excising the tile-select field from an address on the way into a slice and reinserting it on the way out. Ratified by R16. | C.2, E.1 |
| **`FTU`** (`FUT`) | the **Function Tracking Unit** — the host-side structure tracking an outstanding offload from `FORK` to `JOIN`. One structure, two spellings. | A.4, I.5 |
| **`vgrain`** / **`asid`** | the virtual grain index `vaddr >> grain_bits`, and the address-space identifier — together the placement key `(asid << 48) \| vgrain` (`nuca_router.cc:198`). Under **R12** the `asid` also selects the page table. | I3, F.5a |
| **`RemapEvent`** | the broadcast that tells every page-table copy a mapping changed. Under **R17** this is a **TLB shootdown**; the generation counter and log are the simulator's model of one. | F.8 |
| tier 1 / 2 / 3 / 4 | session log / ChampSim / the written docs / SST-Rev, which never decides anything | AUTHORITY, below |
| **`D:nnn`**, **`#N`** | a line in `docs/nmfc/DESIGN.md`; a session-log item number | NOTATION table |

---

## SELECTED CONFIGURATION FOR SIMULATION

**READ THIS BEFORE READING A NUMBER ANYWHERE ELSE IN THIS DOCUMENT.** Under user ruling
2026-09-02 R6–R10, **none of the values below is part of the design.** The design states
the mechanism and the constraint; the table states what each simulator was configured to,
so that a measurement can be reproduced and two measurements can be compared. **A value
here is a point in a study, never a property of the machine.**

`[HOW TO USE IT]` If the body says *configuration; see SELECTED CONFIGURATION*, the
number is here. If you are writing a new configuration, the **design constraint** column
is what binds you; the two value columns are what has been run.

| value | ChampSim (file:line) | SST/Rev (file:line) | design constraint — this is the part that binds |
|---|---|---|---|
| **`N`** — tiles = channels = LLC slices = controllers = function cores | **4** — `config/nmfc/nmfc_4tile.json:7` (`nmfc_num_tiles`), and 4 in all 33 configs | **1**, env-overridable — `test/vanadis-nmfc.py:38` (`NMFC_TILES`); 1, 2 and 4 exercised in `test/coherent_memory.py` | **Any `N` ≥ 1.** A tile is one channel, one slice, one controller, one function core (D.1, E.6). `G` is a function of `N`, so changing it moves the page size. |
| **`W`** — barrel pipe width | **4** — `nmfc_4tile.json:957` (`issue_width.bandwidth`) | **4** — `src/NMFCTile.h:138` (`pipes`) | `C >= W(Dp + L/I)` (H.2). Nothing else fixes `W`. |
| **`Dp`** — barrel pipe depth / re-issue delay | **no such parameter** | **8** — `src/NMFCTile.h:139` (`depth`); `contexts < pipes × depth` is fatal at `src/NMFCTile.cc:41-49` | same inequality; `Dp` is the re-issue delay in cycles |
| **`C`** — contexts per function core | **1024** — `nmfc_4tile.json:956`; **256** — `nmfc_4tile_ramulator.json:777`; swept 64/128/256/512 (N.4) | **256** — `src/NMFCTile.h:137` | `C >= W(Dp + L/I)` is the floor. Above it, `C` is a capacity study (N.4). |
| **host cores** | **1** — `nmfc_4tile.json:6` (`num_cores`), all 33 configs | **1** Vanadis core — `test/vanadis-nmfc.py` | **Multi-core by construction** (#1, #19, #284: private L1+L2 *per* standard core, a shared LLC *across* cores). **A one-host configuration cannot demonstrate saturation** (#19) — quote every result on one as a result on an under-driven machine. |
| **fabric link width** | **none — priced in messages**, not bytes: `max_deliver` 4/class/cycle at `nmfc_4tile.json:718` | **32 B/cycle** — `src/NMFCCoherenceFabric.h:66` (`bytesPerCycle`) | "*of the same magnitude as modern processors*" (#288). **A migration is 72 B and a line fill 64 B, and they are alternatives, never both (I11)** — so the width must be one at which that parity is expressible. **R4: SST's byte model is the design's; ChampSim has a cycles-to-transmit model only and is not being back-ported.** |
| **fabric hop latency** | **8** cycles — `nmfc_4tile.json:716`; the host fabric is a **separate** `INTERLEAVE_FABRIC` at **4** — `:1733` (this two-fabric split is L25, and frozen under R3) | **4** tile↔tile — `NMFCCoherenceFabric.h:64`; **8** host↔tile — `:65` (`hostHops`) | **ONE fabric** carrying coherence, migration and LLC/DRAM access (I13). Latency is configuration; the single-fabric topology is not. |
| **fabric queue depth** | **128** — `nmfc_4tile.json:717` | control queue, `controlDeliver` 4/cycle — `NMFCCoherenceFabric.h:70` | queue **per destination**, never one shared queue (H.8) |
| **`G` / `grain_bits`** | declared **21** — `nmfc_4tile.json:8`; **derived 20** (`make_config.py:496-546` over `config/nmfc/ramulator/tile_ddr5.yaml:33`). **31 of 33 configs declare 21, and the two memory models the shipped configs actually use — ChampSim's DEFAULT memory controller (`make_config.py:171-172`) and the ramulator DDR5 device (`ramulator/tile_ddr5.yaml`) — BOTH require 20** — ledger L20. **[DISAMBIGUATED — this cell read "both devices require 20", which the next row's two *ramulator device files* (DDR5 and HBM3) make false: E.3 and E.4 both compute HBM3 at 1024 B × 64 banks × 4 channels = **256 KiB**, i.e. `grain_bits` **18**. "Both devices" here names the DEFAULT controller and ramulator DDR5, the pair L20's arithmetic runs on. HBM3 is a third geometry in the tree, and it derives 18 — which is the point, not an exception: `G` follows the device.]** | **1 MiB** at `N`=4 — `test/coherent_memory.py:59-82` (`grain()`), from `DRAM_ROW_BYTES` 8 KiB × `DRAM_BANKS` 32 × `ntiles`. **[CORRECTED — BOTH SST FACTORS ARE WRONG BY THE CONSTRAINT IN THE NEXT COLUMN, AND THEY CANCEL. E.4 rules DDR5's `row_bytes` = columns × channel width = 1024 × 4 B = **4096 B**, not 8 KiB (SST's constant is 2× too large); and `banks_per_channel` must include **ranks**, so the checked-in DDR5 channel is **64** flat, not the **32** per-rank figure SST uses (2× too small — see the very next row, which states the same 64-vs-32 split). 4096 × 64 = 8192 × 32 = **256 KiB**, so the product lands on the right 1 MiB at `N`=4 **by cancellation, not by derivation.** They will not cancel at any other geometry — a device with a different rank count or a different column count breaks the tie instantly, and HBM3's four levels break it in the tree today. **This is exactly the failure the GEOMETRY ruling exists to prevent: the sweep must be DERIVED from the device, never assembled from two hardcoded constants.** Tracked with divergence **S18**, which is the same constant in its other guise.]** | **`G = row_bytes_per_channel × banks_per_channel × total_channels`**, where `banks_per_channel` is the product of **every** organisation level between the channel and the row — **ranks included** — and `total_channels` is DEVICE channels (E.3, E.4). **Any geometry within a 48-bit physical address space must work**: arbitrary banks, ranks, bank groups, rows, columns and channels. **Never lock a count.** |
| **DRAM device geometry** | DDR5 `[1, 2, 8, 4, 65536, 1024]` — `config/nmfc/ramulator/tile_ddr5.yaml:33`; HBM3 in `tile_hbm3.yaml` | DDR5, timings generated — `config/tile_ddr5.yaml` | the same rule. The **32 banks/channel** figure in older text is DDR5 **at one rank** and is not a spec (GEOMETRY ruling). |
| **LLC slice size** | **512 KiB** — 512 sets × 16 ways × 64 B, `nmfc_4tile.json:730-731`; aggregate pinned at 2 MiB by `--llc-sets 2048` (D.5). **Part L and N.1 were measured at 4 MiB, which was never committed** (L28c) | **4 MiB** — `test/coherent_memory.py:175` (`slice_size`) | **"*modern LLC size / DRAM channel* as our indicator for LLC size per tile"** (#76) and "*the same magnitude as modern processors*" (#288). That rule yields **single-digit MiB per tile**; 512 KiB is about an order of magnitude below it. |
| **LLC slice banking** | **1** — `--llc-banks` defaults to 1 (`make_config.py:583`) and **no shipped config banks the slice**. **Under R3 this flag is INERT**: banking is derived from the DRAM device geometry ramulator declares, not from a flag. | banked to the channel's **per-rank** bank count (**32**, not the flat 64) — `test/coherent_memory.py:249-265` | **A cache bank and its DRAM bank must be the same partition of the address space** (#76, #144, #291 item 1). Bank on the **per-rank** count: two flat banks differing only in rank are the same bank index at the same address position (D.2, DESIGN §30.2). The count follows the device; **it is not a design constant.** |
| **FTU entries** | **1024** — `nmfc_4tile.json:2128`; **64** — `nmfc_4tile_ramulator.json:1835`; 2048/4096 sweeps in `phys_ft/` | — | it **refuses rather than evicts** (I.5), so it must be large enough not to bound the machine artificially — and **a full FTU is not evidence that the FTU binds** (#180, #171, H.9). §23.2's derivation prices 64 and 256 entries at ~65 B each. |
| **function-core I$ / D$** | I$ 16 sets × 4 ways, D$ 64 sets × 8 ways — `nmfc_4tile.json:851-852, 903-904` | I$ 32 KiB / D$ 16 KiB, 8-way — `test/coherent_memory.py:102-103`; per-tile slice banking in `test/tile_memory.py:243-265` | **both exist, separate, and heavily banked** (L6, D.3, #291 item 6). Capacity belongs in the **slice**, not in the function core's D$ (D.4). |
| **atomic-table capacity** | `lock_waiters_` is **unbounded and uncounted** — `src/nmfc/function_core.cc:404-467` | per-line hold state — `src/NMFCTile.h:415` region | **R15: capacity is sized from experimentation.** The constraint is absolute: **a full table must be unachievable by construction, or the context sleeps until an entry is free** — it must never be a resource held while waiting for a resource (I.1). |
| **atomic hand-off chain bound** | **unbounded**, no counter | **8** — `src/NMFCTile.h:142` (`maxAtomicForwards`), `src/NMFCTile.cc:32` | **R15: experimentally derived.** The bound itself is a **coherency guarantee, not an optimisation** — a word passed context to context is a word the rest of the machine cannot see, so after a fixed number of hand-offs it goes back to the data cache whether or not anyone is waiting (H.7). |
| **NUCA epoch and gates** | epoch **100000** migrations, `private_threshold` **0.90**, `imbalance_threshold` **1.10**, `pull_threshold` **16** — `src/nmfc/nuca_router.cc:65-68`; the 50-migration epoch of DESIGN §28.2 is a different run | — | **R21: start at the common published values and tune from measurement.** G.4's six rules are the design; the numbers are not. |
| **NUCA backoff** | not parameterised — the epoch count is the only knob | — | **R21.** The rule is "*a grain that has just moved sits still, for longer the more often it has moved*" (G.4 rule 6); the function, its parameters and its reset are configuration. |
| **directory entries / sharers** | ChampSim has **no MOESIF directory** | `std::set<uint32_t> sharers` per line, **unbounded** — `src/NMFCCoherenceFabric.h:156`; `directoryLatency` 4 — `:67` | **R19: size it to modern systems and make sure it is not a bottleneck.** The design part is the **state set (M O E S I F)**, its **location** (the L2↔LLC boundary, in the fabric), **strict NMFC priority as an order, not a tie-break** (I14, C.5), and — **RULED, user ruling 2026-09-03 O9** — an **exact bit vector over host cores and tiles, inclusive of the caches above the fabric, back-invalidating on eviction** (C.5). **The scaling target is now a number: "*up to 32 memory tiles*" on "*a substantially beefy multi-core system*", expecting "*a LOT of traffic*"** — so one sharer bit per host core plus 32 tile bits is what an entry must carry, and the entry count follows R19. |
| **fabric message classes and arbitration** | one `FUNCTION_FABRIC` plus a separate host `INTERLEAVE_FABRIC`, `max_deliver` 4 per class per cycle — `nmfc_4tile.json:718`; **frozen under R3** | `bytesPerCycle` 32, `controlDeliver` 4/cycle — `NMFCCoherenceFabric.h:66, :70` | **RULED — user ruling 2026-09-03 O5.** **Three classes on ONE fabric: COHERENCE, MIGRATION, FILL, per-destination queues; COHERENCE strictly first, then MIGRATION and FILL at EQUAL WEIGHT.** The classes and the order are **design** (C.5, H.8, J.2); the per-cycle delivery rates and queue depths are configuration. |
| **`funct7` / `funct3` field values** | **none** — ChampSim has no decoder, no opcode table, no assembler (I.10) | `/mnt/md0/NMFC-Rev/src/nmfc/include/nmfc_isa.h:21-104` — groups `0x0`–`0x5`, variants, and every funct7 constant; `nmfc.h`'s assembler macros emit from it | **RULED — user ruling 2026-09-03 O3: "*I think this is just a simulator thing and not a meaningful design choice, so I say we describe it as implementation choice.*"** **The canon assigns NO field values.** It fixes the **count** (twelve base plus a privileged `RESUME`), the **group membership**, and that `RESUME` takes a slot. The SST values in this row are **one implementation's choice**, recorded so the binaries already assembled against them stay valid — **never quoted as canon** (I.9, ledger L43). |
| **clocks** | **250 ps = 4 GHz** on host core, function cores, fabric, caches — `nmfc_4tile.json:519, 569, 583, …` | 1 GHz default — `test/coherent_memory.py:_cache(clock=…)`; tile clock from the tile component | no tier-1..3 source states a function-core clock. **Nothing in the design assumes the function core runs at the host's rate**; if it does not, every cycle comparison must say so. |
| **page-table levels / page size** | **5 levels**, 4 KiB base page — `nmfc_4tile.json:505-508` | `NMFCPageTable.h` | **R13: five levels, a PTE derived from that, multiple page sizes as modern hardware already does, and the mode bit stored in the PTE and carried as an extra bit on every physical address** (F.5a). The level count is standard, not novel. |
| **DRAM minor-fault penalty** | **50000 ps** — `nmfc_4tile.json:511` | — | a simulator knob; no design content |

**Two things this table is NOT.** It is not a recommended configuration — nothing here has
been tuned. And it is not a provenance record for the measurements in Parts G, K, L and N:
**no measurement in this document names the configuration file that produced it** — which
is **RULED, user ruling 2026-09-03 O15**: those Parts are **historical observations of an
earlier tree, their configurations unreproducible from git, and ChampSim stays frozen.**
See N.0 and each Part's preamble. **This table describes what CAN be run now; it does not
and cannot describe what produced Parts G, K, L and N.**

## AUTHORITY, NOTATION, AND HOW TO READ THIS DOCUMENT

**Authority order (the user's ruling, 2026-09-02T23:37:02Z, session log item #307,
verbatim):**

> "Clarifying: authority is 1. Session log 2. ChampSim 3. Current doc 4. SST
> implementation, in that order. Inside the session log, newest assertions take
> priority over older assertions."

| tier | source | rule |
|---|---|---|
| **1** | the session log — the user's own words, transcript `0906c103-1f73-4126-961b-1d122973881b.jsonl` | highest. **Newer overrides older, without exception.** |
| **2** | ChampSim, `nmfc/{src,inc,config,tools}/nmfc` | below tier 1. Where the user's words and ChampSim disagree, the user wins and the disagreement is recorded in the ledger. |
| **3** | the written docs — `docs/nmfc/DESIGN.md` and the rest | below ChampSim. |
| **4** | SST/Rev, `/mnt/md0/NMFC-Rev/src/nmfc` | lowest. **It never decides anything.** It is where regressions happened; it appears here only as a divergence checklist. |

**Provenance note, verified.** Tier 1 has exactly one member. Session
`0906c103` spans 2026-08-27T05:14:08Z → 2026-09-02T23:38:10Z and covers the
project's entire lifetime; `memory/nmfc-project.md:11` records the project as
"Started 2026-08-27". There is no second session and no corroborating witness. The
session contains **six** compaction boundaries, so early user words survive in
context only as model paraphrase; everything cited below as tier 1 comes from the
**raw** `type=="user"` records, not from a compaction summary.

**Citation convention.** `#N` = session-log item number in the chronological
extraction (item 1 is the founding design doc, item 307 the authority ruling); each
carries its ISO timestamp. `file:line` = ChampSim or SST source. `D:N` =
`docs/nmfc/DESIGN.md` line. `§N` = a DESIGN.md section.

**NOTATION — read this before any formula, because two symbols used to collide.**

| symbol | meaning | where |
|---|---|---|
| **`N`** | **the tile / channel / slice count.** One number: a tile *is* a channel *is* an LLC slice *is* a memory controller (D.1, E.6). `N` never means anything else in this document. | everywhere |
| **`W`** | **the barrel core's pipe width** — the number of duplicate pipes, each serving a *different* context in the same cycle. **The user wrote this as "N contexts" in #238**; it is renamed `W` here because `N` was already the tile count and the two collided in the context-sizing formula. | Part H only |
| **`C`** | contexts per function core | Part H |
| **`Dp`** | the barrel pipe **depth** (the re-issue delay) | Part H only |
| **`L`** | **memory latency**, in cycles, as it appears in the context-sizing formula `C >= W(Dp + L/I)`. **Not a cache level and not a line size.** | Part H only (H.2) |
| **`I`** | **instructions a context issues between misses** — the second term of `L/I` in the same formula. [CAUTION — this symbol is one dropped period away from the invariant ids `I1`-`I14` and the ISA sections `I.1`-`I.11`. It appears ONLY inside that formula; anywhere else, an `I` is an id.] | Part H only (H.2) |
| **`G`** | the **grain**: `row_bytes_per_channel × banks_per_channel × total_channels` — **`total_channels`, never `N`; `banks_per_channel` is the product of every organisation level between the channel and the row, which is three levels on DDR5 and four on HBM3 (E.4)**. Defined in full at E.3; used from Part B onward. **It is not a constant** — it changes with the memory organisation (E.3). [DISAMBIGUATED — the third factor was written three different ways in this document (`N` here, `num_channels` at E.3, `total_channels` at E.5) and its referent was never stated. **It is `total_channels` = `channels_per_instance × N`, i.e. DEVICE channels, not the tile count** — and it equals `N` only because E.6 requires each ramulator2 instance to declare exactly ONE channel internally, which is the constraint that makes tile = channel true (D.1). Write `total_channels`; the identity with `N` is a consequence of that constraint, not a definition. Since `G` fixes the NMFC page size, the tag granularity and the silo granularity, this is not a cosmetic difference.] | everywhere |
| **`M`** | the byte size of one *virtual* duplicate page's total physical footprint, `M = N × G` — see C.3 | C.3 only |
| **`D:nnn`** | a line number in `docs/nmfc/DESIGN.md` | citations |
| **`#N`** | session-log item number | citations |

In the only worked configuration in this document tile count and issue width are both
**4**, so every formula below is numerically correct either way and the collision was
invisible. On an eight-tile machine it would not have been.

**BRACKET TAGS — the complete vocabulary, so a reader knows whether an invariant is
being qualified or merely annotated.**

| tag | meaning | does it weaken the statement it sits in? |
|---|---|---|
| `[SHARPENED]` | a higher-authority source makes the statement **more** precise. The statement stands, harder. | no |
| `[DISAMBIGUATED]` | §0's wording was ambiguous in a way that already caused a regression; the ambiguity is removed. | no |
| `[CARRIED]` | carried forward verbatim from a lower-authority source with no change and no new authority behind it. | no |
| `[CAUTION]` | **either** a measurement that must not be designed around, **or** a hard requirement that is easy to miss. **Read the sentence; the tag does not tell you which.** Both uses appear. | no — it never weakens |
| `[NOTE]` | cross-reference or provenance remark. | no |
| `[CONFLICT — …]` | a lower-authority source implements something this statement forbids. The statement stands; the conflict is in the ledger. | no |
| `[UNRESOLVED — flagged, not decided]` | the evidence is genuinely incomplete. **The invariant still stands**; only its *supporting measurement* is in doubt. | no |
| `[REBUILT]` | (Part P) rejected, and then built again anyway. | n/a |
| `[FOR THE USER TO RULE]` | (Appendix 1) this document does not resolve it on its own authority. **NO INSTANCE IS STILL LIVE** — the last two, L38 (`O1`) and L43 (`O3`), were closed by the 2026-09-03 rulings. Where the tag still appears in a ledger row's heading it is the **record of what was asked**; the row's `RULED` bullet is what governs. | n/a — none live |
| **[RULED — user ruling 2026-09-02 R\<n\>]** | **the user has ruled and the statement is now settled at tier 1, newest.** The ruling's own words are quoted with the tag. **This is the strongest tag in the document**; a `[CONFLICT]`, `[UNRESOLVED]` or `[FOR THE USER TO RULE]` in the same passage is superseded by it. | no — it settles |
| **[RULED — user ruling 2026-09-03 O\<n\>]** | **the same thing, for the ten residual questions the user closed on 2026-09-03.** Tier 1, newest, binding, and it supersedes every `[STILL OPEN]`, `[FOR THE USER TO RULE]` and `[USER TO CONFIRM]` in the same passage. **This and the R-tag are jointly the strongest tags in the document.** | no — it settles |
| **[derived from ruling O\<n\>]** | a consequence the user did **not** spell out, drawn in this document from a ruling plus an existing tier-1 rule. **It is marked so it is never mistaken for the user's own words**, and the derivation is always shown beside it. | no — but it is the document's inference, not a quotation |
| **[USER TO CONFIRM …]** | **RETIRED. The vocabulary no longer contains this tag and no occurrence remains in the document.** It marked exactly one clause — `RESUME`'s privilege level, which R20 left as a question — and **user ruling 2026-09-03 O16 ("*Yes, privileged.*") answered it**. Every occurrence was removed in this revision. **The check is `grep -n 'USER TO CONFIRM' CANON.md`: every surviving hit must be a place that NAMES the retired tag in order to say it was removed — this row, the `[RULED — 2026-09-03]` row above it, the O16 ruling row, I.3's removal note, and Appendix 3 item 6. Any hit that is an actual bracketed tag sitting on a statement is a regression. The exact form the tag took was the label followed by the word *privileged*; no instance of that form survives.** | n/a — retired |

**HOW A TAG IS DELIMITED — a rendering rule, and it has already broken tags in this
document.** `[ADDED. A Markdown code span (single backticks) ENDS AT THE NEXT BACKTICK and
CANNOT CROSS A BLANK LINE.]` Most tags in this document are written
`` `[LABEL — text]` `` — outer backticks — and most of them also quote identifiers in
backticks inside the text. **When they do, the outer span terminates at the first inner
backtick and the tag renders as an interleaving of code and prose, with its closing `]`
orphaned**; when the tag also spans a blank line, its boundary becomes invisible
altogether. Both had happened, in the first ruling table, in the ID-SPACES note, in the
design-values table (now SELECTED CONFIGURATION), at E.3 and at F.6, and worst in C.4,
where the multi-paragraph tag
prescribing how to check mermaid was itself unreadable.

> **THE RULE, AND IT HAS BEEN APPLIED TO THE WHOLE DOCUMENT:**
> 1. **A tag containing no inner backticks** keeps the outer-backtick form — that is what
>    the vocabulary table above shows, and those render correctly.
> 2. **A tag that quotes an identifier drops its outer backticks entirely** and is written
>    as plain bracketed text: **[LABEL — text with `identifier` in it]**. The square
>    brackets are literal characters, the inner code spans work, and bold works inside.
>    **61 tags in this document were converted to this form**, because code spans do not
>    nest with each other and every one of those 61 was terminating early.
> 3. **A tag that spans a blank line** opens with a bold marker naming its extent
>    (**[LABEL — … A MULTI-PARAGRAPH TAG: it runs to the "END OF …" marker below.]**) and
>    closes with a bold **[END OF …]** marker. **Never delimit a multi-paragraph tag with
>    backticks** — a code span cannot cross a blank line at all. C.4's rendering correction
>    and E.4's bank-count correction are the worked examples.
> 4. **THE CHECK — and the parity check ALONE IS NOT IT.** [CORRECTED — this item used to
>    read only "no paragraph outside a fenced block contains an odd number of
>    single-backtick runs". **That check cannot detect the defect rule 2 exists for.** A tag
>    written **[LABEL — text with `id` in it]** wrapped in outer backticks has an EVEN
>    backtick count — outer-open, inner-open, inner-close, outer-close — so it passes
>    parity while rendering as alternating code and prose, with the identifier as plain
>    text and the surrounding words as code. **Seven such tags passed the parity check in
>    this document and were found only by reading.** Both checks are now required.]
>    - **Check 4a (parity, necessary, NOT sufficient):** no paragraph outside a fenced
>      block contains an odd number of single-backtick runs.
>    - **Check 4b (nesting, and this is the one that catches rule 2):** for every place
>      where **a backtick is immediately followed by an opening square bracket**, scan
>      forward to the first **closing square bracket immediately followed by a backtick**
>      and assert **no backtick occurs between the two**. A hit is a tag that opens a code
>      span and then quotes an identifier inside it — the exact defect rule 2 forbids.
>      **Fix it by rule 2: delete the outer backticks, keep the square brackets and the
>      inner code spans.** (Written as a regex it is one line; it is stated in words here
>      so that stating the check does not itself trip the check.)
>    - **Both are short scripts and both are cheaper than re-reading the document.** Run
>      4b whenever a tag is added or edited; it is the check that has actually found bugs.

**ID SPACES — `I3` and `I.3` are DIFFERENT THINGS.** Part B's invariants are `I1`–`I14`
(no period). Part I's ISA sections are `I.1`–`I.10` (with a period). They differ by one
character and both are cited by bare id throughout.

[CORRECTED — an earlier revision "mitigated" this by enumerating three dangerous pairs.
**Enumeration does not work here and has already failed in-document**: `I1`–`I10` ALL have
a dotted counterpart, so there are TEN collisions, and the one live miscitation in the
document landed in a pair the list omitted — J.4's "Full table in I5", where **`I5`** is
the migration invariant that holds the table and **`I.5`** is "The function tracking
unit", 2,400 lines away and holding no such table. A reader resolving it with the habit
this note teaches finds nothing and concludes the reference is stale.]

**The rule, without an exception list: EVERY `I<n>` for n = 1…10 collides with an
`I.<n>`.** When quoting, **keep the period, or write it in words** — "invariant 3" /
"section I.3". Never write a bare `I5`-style id in running prose where the surrounding
sentence does not already fix which Part it belongs to.

| bare id | Part B invariant | Part I ISA section |
|---|---|---|
| `I1` / `I.1` | an offload is an instruction | nothing blocks |
| `I2` / `I.2` | 512 bits in, 512 bits out | the host side |
| `I3` / `I.3` | one page table, duplicated | the function side |
| `I4` / `I.4` | placement at translation time | the two closing rules |
| `I5` / `I.5` | **migration is expected — and holds the three-migration-numbers table** | **the function tracking unit** |
| `I6` / `I.6` | NUCA/NUMA policy required | faults |
| `I7` / `I.7` | regfile, no stack | deliberately absent |
| `I8` / `I.8` | **the CLAIM SHAPE** — compare against the reference algorithm on identical work, and quote it with its core model (M.3) and its branch caveat; the 5.67× itself lives at N.2 | context registers |
| `I9` / `I.9` | congruence, checked every run | encoding |
| `I10` / `I.10` | extend, never fan out | what ChampSim builds instead |

[UPDATED — Part I now has an `I.0` and an `I.11`. **Neither collides**: Part B has no
`I0`, and its `I11` is the migration-parity invariant while `I.11` is the
memory-committing loop, which are far enough apart that a dropped period is obvious. The
ten collisions above are still the complete set.]

**Standing proposal, carried over from an earlier review and still unactioned: rename the
invariants `INV1`–`INV14`.** The enumerated-exceptions approach has now failed once
inside this document; the id space itself is the defect.

**How to read this document.** **RULINGS NEEDED FROM THE USER** is at the front and is
now a **record of rulings, not a request for them: NOTHING IS OPEN.** It holds the
**ten** items `O1`, `O3`–`O7`, `O9`, `O12`, `O15` and `O16` — the numbering deliberately
gapped so the body's existing citations stay valid — **every one of them RULED by the user
on 2026-09-03**, with his words quoted verbatim; plus a **CLOSED IN EDITING** table
recording the six (`O2`, `O8`, `O10`, `O11`, `O13`, `O14`) that were never rulings to ask
for. **If you are reading this document to find out what it still needs from you: it needs
nothing.**
**SELECTED CONFIGURATION FOR SIMULATION** follows it and holds every value this document
used to state as a design constant — under R6–R10 those are configuration, not design, and
an implementer reads that table to know what has actually been run. Part A is the machine
in plain terms — read it first, every time. Part B is the invariants; they are settled and
are not to be re-derived. Part C is diagrams. Parts D–O are the detail. Part P is the list
of things that must never be rebuilt. Appendix 1 is the conflict ledger — **49 rows, each
affected row now marked RULED with its R-number; no row has been deleted.** Appendix 2 is
the SST divergence checklist, **and its D0 records that ChampSim is frozen.** Appendix 3 is
the standing method rules, including the never-quote-bare list.

**Three sections are new and are where a re-derivation is most likely to start:**
**I.0** (what a function core executes underneath the twelve base instructions — **the
machine is RISC-V under user ruling 2026-09-02 R11**, and x86-64 in this document is the
trace toolchain's host and nothing else), **I.11** (the
memory-committing invocation loop, which is built as trace markers and has no opcode), and
**O.4** (what the machine must report, plus the four risks DESIGN §12 registers — one of
which, "the function core never mispredicts", is a caveat on every measured function-core
number here).

---

## PART A — THE SYSTEM IN ITS BASE FORM

Read this page whole. Nothing in the rest of the document contradicts it **on what the
machine is**. **Four** quantities on this page are stated at lower precision than the
body carries, and all four are flagged in place: **the migration budget in A.5** (an
aspiration, not a gate — invariant 5, J.4), **the §29.2 inversion figures in A.5** (quote
a ROW: 4.96× and +38.2%, never "five sixths", never a bare "38%" — N.3), **the placement
claim in A.4** (which describes the architecture, not what ChampSim's shipped
configurations actually do — see A.4's `[CONFLICT]` block), and **the 5.67× in A.7**
(which core model produced it — M.3, and its algorithm penalty is the *direction-
optimising* kernel's 1.08×, not the top-down kernel's 7.38×).
`[CORRECTED — an earlier revision of this preamble named only two, then three; A.7 quoted
the headline number bare on the one page the document orders read first, and A.5 carried
the uncorrected "five sixths … 38%" as an unflagged fourth.]`

### A.1 The problem

Many supercompute workloads are richly parallel across threads but each thread is
almost perfectly serialised by memory dependencies — graph traversal, pointer
chasing. With a fixed thread count and memory-level parallelism near 1, bandwidth
sits idle and compute sits idle at the same time. GPUs do not help: the individual
kernels are themselves serial (#1, 2026-08-27T05:17:16Z; DESIGN §1 D:186).

### A.2 The machine, literally

Take a **completely ordinary modern multicore memory system**: each host core has
private L1I, L1D and L2; there is a shared last-level cache across cores; below it
are memory controllers and DRAM channels. The **L2-to-LLC interconnect is a
fabric**, and coherence is enforced at exactly that boundary. Nothing in that
sentence is NMFC.

**NMFC changes exactly one thing *in the memory system*: the address partition moves
to the fabric.** The qualifier is load-bearing and must always be carried: NMFC *also*
adds a new kind of core (A.3), a host-side tracking unit (A.4) and **twelve base
instructions plus a privileged `RESUME`** (Part I). What it does **not** change is the memory hierarchy's shape. A
conventional machine routes an L2 miss into a shared LLC and only slices afterwards,
on the way to the controllers. NMFC slices **vertically**: the fabric picks the
tile, and that tile's LLC slice, memory controller and DRAM channel are one stack
owned by it. That is the whole architectural delta of the memory system (#284,
2026-09-02T02:32:58Z, the user's own all-caps statement; invariant 13).

Onto the top of each vertical stack — **on the slice's side of the fabric interface,
not the host's** — is bolted a new kind of core: the **function core**. It has its
own private instruction cache and data cache, both heavily banked, sitting above its
tile's LLC slice exactly as a core's L1s sit above an LLC. Because it sits there,
its fetches, loads, stores and page-table walks **never cross the fabric** — not by
exemption, but because there is no fabric between a core and the slice it sits on
(#283, 2026-09-02T02:18:18Z: "The nmfc core itself does NOT live across the fabric
from the memory tile. It is THERE, on the same end of the fabric as the slice of the
LLC, memory controller, and channel.").

There is **one** fabric. It carries coherence traffic, migration traffic, and
LLC/DRAM access. There is no second interconnect for NMFC traffic.

### A.3 What a function core runs

A **function core** time-multiplexes a large number of **stackless invocations**.
An invocation's entire architectural state is a **program counter** plus a **512-bit
register file** — 64 bytes, one cache block. No stack. Creating or tearing one down
is a slot write, which is what makes an arbitrary number of them affordable
(invariant 7; #1, 2026-08-27T05:17:16Z; H.3).

It is a **barrel core**: one context issues one instruction, then yields; several
different contexts are in the pipe at once, never the same context twice. **`W` pipes
is the width** — `W`, the barrel pipe width, **never `N`**, which is the tile count and
nothing else (NOTATION, H.2). The user wrote the width as "N contexts" at #238 and this
document renames it; on the one worked configuration both numbers are 4, so the
collision was invisible, and on an eight-tile machine `C >= W × (Dp + L/I)` (H.2) and
`tile = (pa >> log2 G) mod N` (E.1) take different numbers. Because at most one
instruction per context is ever in flight, no two
instructions in the pipe can be dependent — so there is no forwarding, no
interlocking, no hazard detection, no ROB, no rename, no load/store queue, and no
speculative execution (DESIGN §7 D:905, §25.2 D:2341-2348 — "the Denelcor HEP and Tera
MTA arrangement, and among shipping near-memory parts UPMEM's DPU").

A context that issues a load **sleeps** and wakes when the value arrives. That is
the mechanism that turns a purely serial pointer chase into channel saturation:
hundreds of serial kernels, time-multiplexed onto one channel, keep it busy
(#1, 2026-08-27T05:17:16Z; H.4; DESIGN §25.4).

[NOT A DESIGN NUMBER — how many is "hundreds" is **configuration**. `C`, `W` and `Dp` are
values, not design (user ruling 2026-09-02 R6–R10); **see SELECTED CONFIGURATION**. What
the design fixes is the inequality: §25.2's `C ≥ W(Dp + L/I)`, which gives 256 as the right
order at the one worked point.]

### A.4 How work gets there

A host core **forks** an invocation with a real instruction: it passes an entry PC
and a 512-bit context. The host's **function tracking unit (FTU)** — a structure
parallel to the LSQ, not a reuse of it — holds the outstanding invocation and, on
return, holds its 64-byte result until a **join** instruction collects it. Fork and
join are separate instructions, which is what lets one host keep hundreds of
invocations in flight rather than a reorder buffer's worth (invariant 1; #86,
2026-08-28T19:36:52Z; #221, 2026-09-01T04:08:22Z).

**There are TWO official invocation loops and both are kept.** `[ADDED — Part A described
only the first, so a reader taking this page as the base form omitted half of how work
comes back.]` **Register-returning**: the result comes home in the register file and
`JOIN` deposits it; concurrency is bounded by how many results the caller holds
un-joined. **Memory-committing**: nothing is returned — the invocation **writes a block
to memory** and the host reads it later, so it can fork as many as the tracking unit
holds. The second needs a commit primitive, a wait primitive, ownership by address, and
"never a double commit, never a double block" (#130/#131/#132, 2026-08-29T03:56-03:59;
DESIGN §4.3 D:369-415). **All four are built — as trace markers, not as instructions**;
full statement at C.4 and I.11.

**Where the invocation lands is decided by translation, not by the compiler.** The
entry PC is translated to a physical address before it crosses the fabric, and the
physical copy the OS hands back names the tile. Function code lives on **duplicate
pages** — one virtual address, one physical copy per channel — so choosing which
copy to hand back *is* the placement decision, made at run time by the owner of the
address space.

[CONFLICT — BLOCKING, and it is SUMMARISED here rather than argued here. [MOVED —
Part A is promised as "the machine in plain terms, read it first, every time", and this
block had grown to 79 lines of `placement_policy` enum values and config censuses before
the reader reached A.5. **The full evidence is unchanged and now lives in the ledger**,
which is what I13 already does successfully with the two-fabric conflict. Nothing was
deleted.]]

**In ChampSim the invocation's tile is decided by a COUNTER, in every shipped
configuration.** Placement does not live in the translation path there; it lives in
`FUNCTION_FABRIC`, whose header says so — "**Placement lives here**"
(`src/nmfc/function_fabric.cc:11-13`). Of its four `placement_policy` values only
`by_entry_pc` reads the entry PC, the default is `round_robin`, seven configs name that
default explicitly, and the other thirteen name `"first_touch"` — **a policy that was
removed for being unimplementable and is silently redirected** (`:52-57`). Under
`PHYSICAL_ROUTER`, the router in 15 configs, even the entry-PC arm returns a counter,
because that router reads a *second* `placement` parameter that no config sets. There is
a **third** round-robin `placement` parameter in `ADAPTIVE_ROUTER` which is unreachable,
while a statistic named after it — `GRAINS PER TILE` / `grains_per_tile` — is printed and
exported and **counts REMAPS, not grain placements** (that one is ledger **L42**, and it
is the second instance of L21's wired-to-a-counter-that-cannot-move shape).

**Full census, every source line, and the ruling: ledger L36 and L42.**

**Tier 1 wins: placement is a translation-time decision by the address space's owner
(I4).** Two consequences a reader of this page must carry:
1. **No result taken from a shipped config is a measurement of I4's placement.**
2. **Any sentence in Parts G, K or N contrasting "round-robin vs first-touch" must first
   say which of the three mechanisms actually ran.**

[AND IT IS NOT GOING TO BE FIXED — user ruling 2026-09-02 R3, "ChampSim updates stop."
Setting `placement_policy` explicitly and renaming the `_ft` directories (L36) are ChampSim
changes and are **frozen, not abandoned**. The default ROUTER does change, because R2
ordered that one specifically: `PHYSICAL_ROUTER`/`NUCA_ROUTER` becomes the default and
`CONGRUENT_ROUTER` is relabelled a control (L4). **The two consequences above therefore
stand indefinitely and must be carried at every quotation.** See Appendix 2 D0.]
Wherever this document says "first-touch" (I6, G.1, G.4 rule 5, J.4, R36, R37, R38), read
it as *the removed policy whose name resolves to `by_entry_pc`* — unless the context is
`PHYSICAL_ROUTER`'s `placement` parameter, which is a different `first_touch` entirely.

**`[READ A.4a NEXT. Everything above is about WHERE AN INVOCATION IS SENT. It says
nothing about WHERE A GRAIN OF DATA IS BACKED, which is a different decision, made in a
different file, by a different rule — and an earlier revision of this document summarised
the whole machine as "round-robin dispatch under three different names", which is true
only of this half.]`**

### A.4a WHERE A DATA GRAIN IS BACKED — the built rule is VIRTUAL-ADDRESS CONGRUENCE

**In plain terms: a grain of data is backed on the tile its own VIRTUAL address names,
`(virtual_address >> grain_bits) % num_tiles`, and this holds in all 33 checked-in
configurations by two independent paths** — the vmem's default for an unhinted grain, and
the annotator's hint, which computes the same arithmetic and rotates every region's base
specifically to *preserve* it. Both routers agree: `nuca_router.cc:110` and
`physical_router.cc:88` are literally `return map_.tile_of_virtual(vaddr);`.

**That is one of the two mechanisms F.3 orders "deleted on sight"** — `tile_of(virtual
address)` as a router, and a hint whose payload is a tile number. **They are not
vestigial and they are not a control: they are how every grain in every result in this
tree was placed.** And the counter is *not* the fix: R37 records that round-robin grain
placement was removed after **75.3% of all accesses routed to the wrong tile**, and
`nuca_router.cc:104-110` says what replaced it — congruence, with "**balance belongs to
`remap_grain()`**".

**`[RULED — user ruling 2026-09-03 O1, verbatim: "Unhinted grains are up to the
OS/hardware to place. So, presumably the OS could map it wherever was most convenient."
This was the last substantive placement question in the document and it is closed.]`**

**Reading (a) is the ruling, with a sharper edge than (a) had.** An unhinted grain's
placement is the **address-space owner's free choice** — pure allocator convenience — and
`(va >> grain_bits) % num_tiles` is one convenient choice among several the OS may make.
**F.3's delete-on-sight list therefore stops naming it as an architectural partition** and
goes on naming `tile_of(virtual_address)`-as-a-router, which is a different mechanism.

**And the ruling comes with a prohibition that reading (a) did not carry: NO PARTITION
SEMANTICS ATTACH TO AN UNHINTED GRAIN'S VIRTUAL ADDRESS.** Nothing may derive a tile from
a VA and depend on the answer. The built code is therefore **permitted as an allocator
default and forbidden as a router**, which is exactly the shape R2's "*relabel is fine*"
prescribes: **the mechanism stays, its status changes** (F.10).

**One thing does not change: never describe this machine's placement as round-robin.**
R37 removed a round-robin counter after **75.3% of all accesses routed to the wrong tile**,
and O1 does not restore it — "*wherever was most convenient*" is a free choice, not a
rotation.

**Every source line, both paths, the annotator's rotation pass, the `default_region`
census and the full statement of both readings: ledger L38, now RULED.** `[MOVED — this
section was 79 lines ending in a then-unruled question, on the page the document orders
read first. Nothing was deleted; the evidence is in the ledger row that already carried it,
and the ruling is at the end of that row.]`

### A.5 What happens when the data is elsewhere

Tiles are partitioned by **physical** address. When an invocation needs an address
its tile does not own, the invocation **migrates**: 72 bytes — the 512-bit register
file plus an 8-byte PC — travel over the fabric, and it resumes on the owning tile
(invariant 11; #89, 2026-08-28T19:50:34Z, "*each migration requires 72 bytes to transfer
across the fabric*"; #91, J.1).

Migration is not a tax. A real fabric moves nothing smaller than a cache line, so
the alternative — fetching the foreign line — costs 64 bytes plus a header. A
context either migrates to the data or fetches the data to itself; **never both**.
So migration traffic **subsumes** data traffic rather than adding to it (#175,
2026-08-29T17:37:11Z — the user's own reversal of #174; J.2). That is
what makes atomicity free: every access to an address range converges on the one
core that owns it, so a read-modify-write is serialised by a **local table** with no
travelling lock, no protocol, and no second copy to keep coherent.

Migration is **expected** and it is **evidence**: it says either the function or the
page was in the wrong place, and the placement policy is supposed to act on that.
The failure mode is frequency, not occurrence. Budget roughly one migration per
thousand instructions — for **latency**, not for bandwidth.

`[CAUTION — read the budget's status before writing a gate from this page.]` **That
one-per-thousand figure is an ASPIRATION, not a threshold any run has met**, and it is
**not a quantity to minimise**. The *enforced* test is a different one — the legitimacy
ceiling, migrations ≤ loads+stores. Three distinct migration numbers are live in this
document and an engineer writing a regression gate from Part A must take the
three-migration-numbers table in **invariant 5 (Part B)** — **not** section I.5, which is
the function tracking unit and holds no such table — and J.4, not this sentence: [CORRECTED — this
read "the table in `I5` (:403-407)". Two defects in one clause. (i) "`:403-407`" is not a
citation form this document defines (`#N`, `file:line`, `D:N`, `§N` — see the citation
convention in the front matter) and it resolves to the wrong text in both candidate files:
in this document those lines are A.4a prose about `tile_of_virtual`, and in DESIGN.md they
are the memory-committing wait primitive. (ii) it wrote a **bare `I5`**, which the front
matter forbids without exception and which this document records as the one collision that
has already gone wrong twice — J.4 was fixed for exactly this and A.5 was not updated to
match.] the run usually cited as a "measured pass"
misses this budget by about 150× while passing the ceiling, and **§29.2 removed 4.96× of
the migrations (1,703,838 → 343,858, the *edges-duplicate* row — 79.8% removed, about
**four fifths**) and the machine got 38.2% slower (91.0 → 125.8 ms, the SAME row).**
`[CORRECTED — this read "removed **five sixths** of the migrations and made the machine
**38%** slower". "Five sixths" is 83.3% and matches **neither** row of the §29 table
(4.96× is 79.8%; the *edges-duplicate + first-touch* row is 6.93×, 85.6%), and a bare
"38%" without its migration figure is on N.3's forbidden list. Invariant 5 corrects the
same sentence twice (Part B) and this instance was left behind; it is the **fourth**
lower-precision quantity on this page, and the preamble now names four, not three.]`
**This is one of the places where Part A is deliberately less precise than the body**;
the paragraph above is the design intent, the tables in invariant 5 (Part B) and J.4 are
the rule.

### A.6 What the compiler may and may not decide

The compiler has exactly **two** levers: **which page type** an object gets, and
**which vtile** groups objects that belong together. **Never which tile.**
(DESIGN §29 D:3301-3303, in the same words; invariant 12; #271,
2026-09-01T20:18:16Z.)

A **vtile** is a compiled-in **label naming a coherent set** — a *relation*, saying
"these pages belong together", not a location saying "this page goes on tile 3".
Pages carrying the same vtile are co-located wherever they end up; distinct vtiles
are unrelated and are spread to balance load, unless that vtile already has a home,
which its later pages follow. Nothing needs to be adjacent, aligned or contiguous
for two things to land together (#271; #277, 2026-09-01T22:04:12Z, "*grain-alignment only
saves space*"; DESIGN §5.0.3 D:556-570).

### A.7 What is measured

The claim is not "the machine is busy". It is that **the same traversal finishes
sooner with the function cores than without**, running **the same algorithm** on
**the same graph** from **the same source**, reaching **the same vertices**. On
GAPBS's reference direction-optimising BFS over a 645,268-vertex traversal, the
machine is **5.67× faster** than the reference on a standard core, with a **6.13×**
architecture gain on identical work. The offloaded *program*, run with the cores switched
off, executes **0.89×** the reference's instructions — **that ratio belongs to the
algorithm, not to the machine** (DESIGN §0.8 D:49-55, §21.3 D:1860-1906; the run is
tabulated at N.2). [CORRECTED — this sentence read "the machine is 5.67× faster … **while
executing 0.89× the instructions**", which pairs a cycle ratio taken from the
cores-ON row with an instruction ratio taken from the cores-OFF row. The machine's own
instruction ratio against the reference is **0.31×**. N.2 now labels every ratio with the
rows it comes from. **What row 3 counts was open item O11 and is now ANSWERED BY LOOKUP:
host instructions only** — ChampSim's per-core figure is the host ROB's `num_retired`
(`inc/nmfc/nmfc_host_core.h:603`, `src/nmfc/nmfc_host_core.cc:1032`), and the function
core's `instructions_` (`src/nmfc/function_core.cc:698, 1040`) is reported separately and
never summed into it. So **0.31× is the host's share**, not total retired work.]

`[CAUTION — NEVER QUOTE 5.67× WITHOUT BOTH PARAGRAPHS BELOW. THERE ARE TWO CAVEATS,
NOT ONE.]`

**CAVEAT 1 — which core model.** That figure was measured on the ChampSim core model,
whose contexts kept issuing past an outstanding load; **the canon core sleeps on one
(H.4).** The two have different memory-level parallelism at the same context count, so
**it is not a number Rev should be expected to hit, and no quotation of it may omit which
core produced it** (M.3; invariant 8's `[CARRIED]` caveat; DESIGN §0.8 D:56-60, §25.4
D:2447-2453).

**CAVEAT 2 — the absent branch-honesty sweep.** The function core **replays resolved
control flow, so it never mispredicts** (DESIGN §12 D:1016, verbatim: "*The function core
could look artificially good. It replays resolved control flow, so it never mispredicts.
`FLAG_TAKEN_TARGET` plus a configurable fetch bubble is the honesty knob, with a
sensitivity run.*"). ChampSim ships the knob — `FLAG_TAKEN_TARGET`
(`inc/nmfc/nmfc_trace.h:166-172`) and `fetch_bubble: 1` in every config — **and the
sensitivity run is not in the record** (O.4 item 2). So the measured speedup has not been
shown to survive an honest branch cost.

**Quoting it bare — or with only one of the two — is the one thing Appendix 3 item 8
forbids by name, and this is the page the document orders read first.** `[CORRECTED — this
caution carried only caveat 1. Appendix 3 item 8 rules that "a function-core speedup
carries TWO caveats, not one: which core model (M.3) and the absent branch-honesty sweep
(O.4)", and O.4 item 2 says to "carry this with M.3's core-model caveat: the two together
are what 'which core produced the number' means". A reader who obeyed the old instruction
still quoted the number one caveat short of the document's own rule. **Caveat 2 applies to
EVERY measured function-core number in this document, not only to 5.67×.**]`

The same architecture *lost* — 0.91× — when measured against a top-down-only kernel,
because that kernel did far more work than the reference needed (DESIGN §21.3
D:1860-1906; N.5's table).

`[DISAMBIGUATED — "the algorithm penalty" is NOT one number. It is a **per-kernel**
quantity, and this page previously stated 7.38 and 1.08 thirteen lines apart without
saying they belong to different kernels. **Both of N.5's rows, with the architecture gain
each penalty divides, so no ratio here is unintroduced:**]`

| kernel | architecture gain | **its** algorithm penalty | net |
|---|---:|---:|---:|
| **top-down only** | **6.73×** | **7.38×** | 6.73 ÷ 7.38 = **0.91× — a LOSS** |
| **direction-optimising** (the reference, and the one A.7's headline claim is about) | **6.13×** | **1.08×** | 6.13 ÷ 1.08 = **5.67×** |

**So: 7.38× is the TOP-DOWN kernel's algorithm penalty, and 6.73× is the top-down
kernel's architecture gain — the pair that closes the 0.91× loss.** The direction-
optimising kernel's penalty is **1.08×**, and its architecture gain is the **6.13×**
quoted three paragraphs above. **Never write "the algorithm penalty" bare; name the
kernel.** All four figures are defined at N.5 as cycle ratios over N.2's rows —
`algorithm penalty = row 2 cycles ÷ row 1 cycles`, i.e. what the decomposition costs
before the machine helps — and `net = architecture gain ÷ algorithm penalty` in both rows.

**5.3×** is the raw extra work the top-down kernel handed the
machine as counted in the earlier §21.3 write-up, **and no source at any tier states the
unit it counts** — so under Appendix 3 item 8, which forbids quoting a number whose unit
cannot be named, **5.3× is RETIRED** (this is what closed open item O14; option (a),
"state the unit", asked the user for a fact no source contains). It is not on the cycle
axis, it closes nothing, and it is named here only so the phrase is recognised where it
survives in older text. **Quote 7.38× when explaining the loss, and quote 5.3× not at
all.** [ADDED — this document ruled in four places that the two "measure different
things" and defined neither; N.5 now defines all three columns of the two-axis table as
cycle ratios over N.2's rows. **Note also that the 0.89× above is an INSTRUCTION ratio;
its reciprocal 1.12 is numerically near the direction-optimising kernel's 1.08× algorithm
penalty and is not the same quantity — different axis, different rows.**] Never compare the machine
against a weaker algorithm; it condemns hardware that is working (invariant 8, DESIGN
§0.8 D:49-55; #183, 2026-08-29T20:21:31Z, on what the baseline must be).

---

## PART B — THE INVARIANTS

These are settled. They are not re-derived, not traded away for a passing
measurement, and not rediscovered by hitting an assertion. **Read this part before
changing anything in `src/nmfc/` or `NMFC-Rev/src/nmfc/`.**

All fourteen of DESIGN.md §0's invariants are carried forward here. **None has been
dropped.** Where a higher-authority source corrects or sharpens one, the correction
is shown inline and marked `[SHARPENED]` with its source. Where §0's wording was
ambiguous in a way that has already caused a regression, the ambiguity is removed and
marked `[DISAMBIGUATED]`.

---

**I1 — An offload is an instruction.**
`FORK` takes a general register holding the callee's entry PC and a 512-bit context
register that **is** the callee's register file. `JOIN` retrieves it.
*(DESIGN §0.1 D:10-14; user #86, 2026-08-28T19:36:52Z, all-caps: "AN OFFLOAD IS AN
INSTRUCTION. AN INSTRUCTION. AN INSTRUCTION. IT PASSES TWO REGS, THE PC CONTAINED IN
ONE GENERIC REG and a 512-bit VECTOR REG AS THE OTHER. THE VECTOR REG IS EQUIVALENT
TO THE REGFILE FOR THE FUNCTION. ... IT IS THE FORK INSTRUCTION WE MENTIONED BEFORE.
... A JOIN IS THE INSTRUCTION THAT TRIES TO RETRIEVE THE VECTOR REG
POST-FUNCTION-EXECUTION.")*

**The offload aperture is a trace-record encoding, not a mechanism the machine has.**
ChampSim cannot express a new opcode in a fixed trace record, so it reserves a window
of virtual addresses that name invocation tokens, the way an MMIO range names device
registers, and its host core detects that window at `do_memory_scheduling`
(`inc/nmfc/nmfc_host_core.h:532-551`, `src/nmfc/nmfc_host_core.cc:669-676`). That is
a **simulator encoding of `FORK`**, not the architecture. The user corrected this
directly (#96, 2026-08-28T20:52:07Z: "You keep repeating that a load in the offload
aperture is a fork. Why? Why do you keep saying that?") and then ordered it written
down (#97, 2026-08-28T20:55:59Z: "Please, please, please write that down so you stop
quoting the wrong thing. **Your hack will make it into the design spec if you keep
doing this.**"). Anything built on "a load in the aperture is a fork" is wrong at the
root even when it is internally consistent. See ledger L1.

---

**I2 — 512 bits in, the same 512 bits out.**
The whole register file returns on completion. Register positions carry no meaning
across the boundary; the join knows how to interpret what came back. A function
needing more live values than the file holds **cannot be offloaded**.
*(DESIGN §0.2 D:16-18; user #99, 2026-08-28T21:46:18Z: "The 512 bits sent in will
also be what is returned. That is, the register file in it's entirety will be
returned on completion. Finally reg positioning does not matter, the join should know
how to understand the returned value. If the algorithm out of necessity requires 9
regs, then it cannot be used. ... If the reg itself is an artifact, and it isn't
actually used (never read or consumed by join) then it doesn't count.")*

`[SHARPENED — user #232, 2026-09-01T05:44:29Z]` **512 bits is not 8 registers.**
"The regfile for each context is 512 bits. Meaning that we are not limited to 8
8-byte regs. It could be 16 4-byte regs, 64 1-byte regs, or ANY combination.
**Bit-packing is the name of the game. This needs to be handled compile-side**
(I doubt the compiler understands reducing the width of an operand can yield a larger
regfile)." Repeated as a correction at #238: "Once again, NO. 512 bits of context.
The context is not 8 regs. Why do you keep reverting to that?"
The admission test is therefore a test on **bits**, not on a count of registers.

`[SHARPENED — user #191, 2026-08-29T23:48:57Z, on why 512 is non-negotiable]` "The regfile size
is non-negotiable. 512 bits is the max, it is the maximum register size in any ISA.
We can only return one value, and this has direct impact on STATE that is maintained
in each nmfc core. 8 bytes for 1024 contexts is 8 kiB. Double is 16 KiB. Two cycles
to transmit regfiles, not one. 8 KiB -> 16 KiB impacts regfile latency, it impacts
usability. ... **64 bytes is the natural amount to be able to transmit**, perhaps the
local regfile can be larger (that is, the return and start block is only half the
regfile). But that impacts migration, it impacts state requirements, latency, and
complexity."

`[DISAMBIGUATED — the "8 kiB" in that quotation is NOT the register file's size, and an
implementer sizing an SRAM array from it will build one eight times too small.]` The
context is **64 bytes** (I2, H.3), so **64 B × 1024 contexts = 64 KiB** of register file
per tile. The user's "8 bytes for 1024 contexts is 8 kiB" prices **one 8-byte lane**
across 1024 contexts, not the whole file. **The ruling does not depend on which base you
use, because the argument is a RATIO:** doubling the context width doubles the state
(8→16 KiB per lane, 64→128 KiB per tile), costs **two transmit cycles instead of one**,
and hurts regfile latency and usability. **Quote 64 KiB when sizing the array; quote the
doubling when arguing why 512 bits is the ceiling. Never quote "8 KiB" as the register
file.** Same reconciliation at H.3.

---

**I3 — Every tile walks locally. There is ONE page table PER ADDRESS SPACE, duplicated
on every tile. TLBs are shared.**
`[SHARPENED AND SETTLED — user ruling 2026-09-02 R12: "I am fairly certain real machines
have separate page tables per address space? TLBs are shared, page tables themselves
should not be shared between address spaces?" This is what real machines do, and it is
what this machine does. It closes ledger L49 and the surface half of L35.]`
An address space has **one** page table; that table is duplicated so that **every tile
holds a local copy of it**; and the **TLBs are shared**, as they are on any machine —
sharing a translation cache across address spaces is what an ASID tag is for, and is a
different thing from sharing a table. The `asid` is therefore part of every translation,
every remap and every shootdown, which is exactly what ChampSim already carries
(`nuca_router.cc:198` keys a grain as `(asid << 48) \| (vaddr >> grain_bits)`, and
`remap_grain(asid, vgrain, best)` takes it as its second argument).
**"One page table" never meant one for the machine**; that reading is dead, and the
duplicate-page footprint, the shootdown scope and the isolation story are all per address
space. The table lives on **duplicate pages** — the same page type function code uses — so every
tile holds a local copy. This is not merely one way to keep walks local: routing
happens *after* translation (I12), so a tile must resolve an address **before** it
can know whether it owns it, and only a duplicated table lets it resolve a *foreign*
address without leaving home. A single table on one channel reached over the fabric
is a bug, and **routing the walks is not the fix**.
*(DESIGN §0.3 D:20-28, §5.0.2 D:533-554; user #107, 2026-08-28T23:20:50Z, all-caps:
"DUPLICATE THE PAGE TABLE per channel. THAT WAS THE WHOLE DISCUSSION WE HAD. WHY DO
YOU KEEP DROPPING EVERYTHING? IT IS NOT ENOUGH TO ACKNOWLEDGE IT WHEN I BRING IT UP,
IT MUST BE IN YOUR MIND CONSTANTLY."; user #110, 2026-08-28T23:25:34Z: "Do we not
have duplicate page types? What are you doing? Do we not have a page type
specifically for this purpose? The one we write function instructions into?"; user
#283, 2026-09-02T02:18:18Z: "translations must NOT be foreign. ... Translation must
be possible LOCALLY, on the same memory TILE. No crossing of the fabric, no
migration.")*

`[DISAMBIGUATED]` The older framing **"N roots ⇒ table partitioned / one root ⇒ table
duplicated"** is not a live *pair* of options in the sense the hook presents it, but the
authority for saying so must be stated correctly, because it was stated wrongly once.

*What #265, 2026-09-01T19:25:50Z, actually says, in full:* "You are citing old outdated
design spec. I would appreciate you actually reading the design documents. **I can tell
because you mentioned per-slice page table roots, completely outdated.**" That is the
message an earlier draft of this document rested the rejection on.

**#269, 2026-09-01T20:11:19Z — forty-six minutes NEWER, and the message this document
itself calls "the single most complete statement" — presents both walk arrangements as
live and ends by saying the question is open**, verbatim: "For walks to be cheap, there
are two approaches: **Either an independent page-table root per tile (requires
virtual-address-partitioning out of necessity, exposing us to the undesireable
virtual-address-routing trap) or a single page table on duplicate pages across each
tile. Regardless, walks must remain local** ... **I bring up all these points because I
am not sure what the right final surface is.**"

**So the correct statement of the ruling, on the correct authority, is this:**
1. **One duplicated page table is the canon**, and it is settled — but it is settled by
   #269's *own internal logic* plus the newer #283, not by #265. #269 rejects
   virtual-address partitioning outright ("Physical-address partitioning is all-around
   better") and states in the same breath that the N-roots branch "**requires
   virtual-address-partitioning out of necessity**". A branch that requires a rejected
   mechanism is not available. And #283, 2026-09-02T02:18:18Z — newer still — requires
   local translation with "**No crossing of the fabric, no migration**", which a single
   duplicated table satisfies.
2. **The final translation SURFACE is now settled, and it was #269's own last sentence
   that had left it open.** [RULED — user ruling 2026-09-02 R12 for the surface (one
   table per address space, duplicated on every tile, TLBs shared) and R2 for the
   mechanism's status in ChampSim ("relabel is fine … the defaults should be switched to
   the physical/nuca router"). **`VIRTUAL_FIRST` is relabelled as an F.10 control and is
   not deleted**; the default becomes the physical/NUCA router. Ledger **L35** and **L4**
   are closed.] The single duplicated table is the canon; the N-roots branch stays dead
   because it requires the rejected virtual-address partitioning.
3. What is unambiguously dead is the *hook's* framing — presenting N-roots as a
   currently-selectable mode of this machine, and `VIRTUAL_FIRST` as a live routing
   order. See ledger L2 — `MEMORY.md:2`, the per-prompt hook, **and ChampSim's own
   module headers** still carry it.

---

**I4 — Placement is a translation-time decision by the address space's owner.**
The virtual address is translated to physical **before** it crosses the fabric, and
the physical copy handed back names the tile. This is what makes load balancing a
run-time decision rather than a layout the compiler baked in. **Nothing may consult
data the invocation has not touched yet.**
*(DESIGN §0.4 D:30-34; user #23, 2026-08-27T18:06:19Z: "1. vaddr must be converted to
paddr, we must translate the pc on dispatch to the physical space before crossing the
fabric. That is necessary. 2. **The pmem assigned maps which tile we go to, meaning
we can load-balance in real-time, not compile-time.** This was in the design doc.";
user #31, 2026-08-27T19:26:09Z: "the OS is going to need to understand that it is
aliasing that virtual address to N different physical pages (1 per channel). The OS
chooses which to hand back, which is where initial placement will happen."; user
#104, 2026-08-28T23:10:33Z: "mem[0] is not available until the function is executing,
something that cannot be known. The vmem->pmem translation should be placing it
according to the physical address of the PC that it was passed.")*

A placement policy that read the body to find the first address a function *would*
touch was built and removed: dispatch precedes execution, so that address does not
exist yet outside the simulator, and it flattered every result built on it by
removing migrations no real machine could have avoided
(`src/nmfc/function_fabric.cc:281-288`).

`[CONFLICT — BLOCKING. ChampSim decides the invocation's tile with a COUNTER in every
shipped configuration, not with the translation.]` The decision belongs to
`FUNCTION_FABRIC`'s `placement_policy` parameter (four values, default `round_robin`),
of which only the `by_entry_pc` arm reads the entry PC at all; and under
`PHYSICAL_ROUTER` — the router in 15 of the checked-in configs — even that arm returns a
round-robin counter, because the router's own `placement` parameter defaults to
`round_robin` and no config sets it. **The full census, the source lines, and the
`first_touch`-is-a-removed-policy redirect are in A.4's `[CONFLICT]` block; the ruling is
recorded as ledger L36.** I4 stands as tier 1; ChampSim does not implement it.

---

**I5 — Migration is expected, and it is evidence.**
The failure mode is **frequency, not occurrence**: roughly **one per thousand
instructions** is fine, and each costs **72 bytes** on the fabric. A migration says
either the function or the page was in the wrong place, and the routing policy is
supposed to act on that.
*(DESIGN §0.5 D:36-40; user #89, 2026-08-28T19:50:34Z: "1 v 1k is probably a fine
migration interval. **Each migration requires 72 bytes to transfer across the fabric,
so we must keep that in-mind. That is the primary constraint, and if you aren't
modelling it, then you don't understand it.**"; user #28, 2026-08-27T19:01:11Z, the
two feedback policies: "1. A migration is a hint that the page being accessed should
have been on the previous tile. 2. A migration is a hint that the function being
scheduled to the new tile should probably be co-scheduled with the other functions on
the present tile.")*

`[SHARPENED — user #88, 2026-08-28T19:45:29Z]` "you seem to be taking my 'doesn't
migrate' as a rule rather than a design principle. **Migration must happen, must be
handled.** A function that constantly migrates is bad. **One that executes 300K
instructions and migrates 10-20 times is perfectly acceptable. Don't twist my
words.**"

`[SHARPENED — DESIGN §30.1 D:3415-3419, measured]` **A migration rate is not itself a
cost.** **§29.2 removed 4.96× of the migrations (1,703,838 → 343,858, the *edges-duplicate* row) and the machine got 38.2% slower (91.0 → 125.8 ms, the SAME row).** `[CORRECTED — was "five sixths … 38%", a
fraction matching neither row of the §29 table; see Part L.]` A migration count is
evidence about *placement*; it is not a quantity to minimise. Every earlier alarm in DESIGN.md about migration rates was reading the
wrong number.

`[SHARPENED — user #291, 2026-09-02T12:37:03Z, the legitimacy ceiling]` "**Any
migration due to instruction fetch or translation is by construction wrong.** Then,
only DATA migrations should happen, and **never outnumber the number of loads/stores
issued by a function.**" This is a *different* test from the budget: the budget is
about latency amortisation, the ceiling is about legitimacy. Measured
pass **on the ceiling's SECOND clause only**: 396,161 migrations against 524,288 loads
= 0.76 per memory operation (DESIGN §31.4 D:3678-3682).

`[CAUTION — the first clause is NOT measured, and this document elsewhere says the
assertion that would measure it is not installed.]` #291 has two clauses. The **count**
clause (migrations ≤ loads+stores) is what 0.76/memop tests. The **legitimacy** clause —
"*any migration due to instruction fetch or translation is by construction wrong*" —
is policed by `strict_locality` on the `("mmu", …)` `TILE_PORT`, and F.6/L26 record that
**`make_config.py:265-267` skips that port entirely under `--walk-routing fabric`**
(`if args.mmu and args.walk_routing == "local"`), which **15 checked-in configs
select** (the census is in F.6; **13 is the unrelated `first_touch` count and an earlier
revision quoted it here**) — so the assertion that would catch a foreign walk is absent in
exactly the configurations that make foreign walks possible. **The record does not say which
walk-routing mode produced the 396,161-migration run.** Until it does, this is a pass on
the ceiling's arithmetic and **silence on its legitimacy half** — not a pass on #291.
State it that way wherever it is quoted (J.4, N.6). Ledger **L37**.

`[DISAMBIGUATED — three different migration numbers are live in this document and they
are NOT interchangeable. An implementer writing a regression gate must use the right
one.]`

| # | quantity | value | what it is | status |
|---|---|---|---|---|
| 1 | **the budget** | ~**1 migration per 1,000 instructions** (1e-3/instr) | a *latency* design target for a well-shaped function, stated by the user at #89 and #88 | **an aspiration, not a gate.** No measured run in the record meets it |
| 2 | **the legitimacy ceiling** | migrations **≤** loads+stores (measured 0.76/memop) | a *correctness* test in **two clauses**: (a) no fetch- or translation-induced migration at all, (b) data migrations ≤ memory ops | **a hard gate. Clause (b) measured and passing; clause (a) NOT measured — the assertion is uninstalled in the **15** `--walk-routing fabric` configs (F.6's census; L26, L37), and the run's mode is unrecorded.** |
| 3 | **the well-shaped-function target** | **0.0015 migrations/instruction** | the rate the spawn experiments reached; kept as a *target*, never as an endorsement of spawn (K.4) | a target for decomposition work |

`[THE BYTE COST IS SST'S, AND IS NOT BEING BACK-PORTED — user ruling 2026-09-02 R4:
"ChampSim doesn't have a byte model, just a cycles-to-transmit model. No need to back
port it, SST is correct."]` The 72 bytes are the design's cost and SST charges them
(`MigrationEvent::SIZE_BYTES = NMFC_CTX_BYTES + 8 = 72`, on the departing and the arriving
link). **ChampSim models time-to-transmit and not bytes, and that is now a declared
property of ChampSim rather than a defect to fix** — ledger L7 is closed on that basis, and
every ChampSim migration-cost number is read as a message-count model. **Any claim about
byte parity or I11 subsumption is taken from SST.**

`[CAUTION — the budget's own denominator is measured on a machine where ARRIVING IS
FREE.]` The cost that decides whether one-per-thousand is comfortable or fanciful is what
a context pays **after** it lands: F.7 drops its translations, so each arrival pays a
walk per distinct page it then touches. **The SST model charges that walk 30 flat cycles
and issues no memory references at all** — DESIGN §26.0.1 D:2680-2687 ("*the page walk
issues no references ... **so invariant 3's subject is free** ... **migration's after-cost
is therefore too cheap**"), and §26.3 D:2825-2830 names it as "**the term that decides
whether invariant 5's budget — roughly one migration per thousand instructions — is
comfortable or fanciful**", currently "charged as 30 flat cycles with no traffic behind
it". §26.5 lists it as **one of only two things outstanding**. **Every migration-rate
number in this document is measured against that machine.** Divergence **S39**.

**The run repeatedly called a "measured pass" (396,161 migrations / 2,625,144
instructions = 0.151 per instruction, i.e. 151 per thousand) passes test 2's ARITHMETIC
and misses test 1 by about 150×.** Saying "measured pass" without saying *which test* is how the
budget came to look satisfied when it is not. **And the budget is not a quantity to
minimise anyway** — see the next `[SHARPENED]` block: §29.2 removed **4.96×** of the
migrations (1,703,838 → 343,858) and the machine got **38.2%** slower (91.0 → 125.8 ms).
`[CORRECTED — was "five sixths", which matches neither row; Part L.]`

---

**I6 — Physical placement without a NUCA/NUMA policy is not the design.**
Working sets are moved together while access stays balanced across tiles.
**Round-robin, least-loaded and first-touch are not substitutes for it.**
*(DESIGN §0.6 D:42-44; user #106, 2026-08-28T23:18:33Z: "physical placement will not
give us what we want without NUCA/NUMA policies to make sure that migrations are
deliberate and efficient. We had a whole talk about this. **Move working sets
together, while also balancing access across all tiles?** It feels like you drop this
at every opportunity, and it is getting tiresome."; user #29, 2026-08-27T19:07:04Z:
"I am sure several NUCA/NUMA policies already published handle similar situations, so
we don't need to work from scratch."; user #35, 2026-08-28T00:17:20Z: "our adaptive
policy likely fails in the adversarial case because we don't know how to prevent
catastrophic cases like this, while a well-developed NUCA/NUMA algorithm will prevent
this.")*

`[SHARPENED — user #118, 2026-08-28T23:51:00Z, on what the policy must do]` "The OS
provides initial placement, but the key thing the OS supports are the page tables and
duplicate mappings (as well as remap support). **The NUCA/NUMA is supposed to be
migrating data that is used together across tiles.** Note that at the same time, **we
need a policy that allows for functions to migrate to their data. So, we have both
the source and sink that are moveable, and we need to partition them evenly across
the tiles.**"

`[NARROWED — user #175, 2026-08-29T17:37:11Z, which is EIGHTEEN HOURS NEWER than both
statements this invariant rests on (#106 and #118, both 2026-08-28) and therefore
governs them under #307. This document previously quoted the narrowing twice, in I11 and
J.2, and never carried it into the Parts that decide what gets built.]` Verbatim, and the
"except" clause is the load-bearing half:

> "So, if work migrates, **NUCA and NUMA are irrelevant except for the key point: If
> there is any runtime optimization to be made at all, it is to make sure migration
> remains classified as sub-optimal.**"

**What that changes and what it does not.**
- **It does not retire the invariant.** #175's own next sentences keep the mechanism —
  "*If a line is being used a lot, we would prefer it be in one location over another.
  This is what NUCA and NUMA are all about ... how to get NUCA/NUMA to hold when workload
  sources are also shifting*" — and the newer #291/#303 still treat imbalance as a defect
  to fix. "Physical placement without a NUCA/NUMA policy is not the design" stands.
- **It does narrow the MANDATE to one objective.** Once migration subsumes the data
  transfer it replaces (I11), NUCA/NUMA is no longer justified by bandwidth or by
  migration counts. **Its single remaining job is to keep migration classified as
  sub-optimal** — i.e. to make the co-located arrangement the one the machine prefers,
  not to minimise migrations, which §29.2 measured as actively harmful (38% slower).
- **The likely shape is stated in the same message**, and it is not a partitioning step:
  neighbour-touch recognition, co-location, and **the work follows the data** — "*No need
  to try to divy up work, it follows the data as placed by those algorithms.*" See G.5.

**Read I6 as: a NUCA/NUMA policy is required, and its objective — on the newest word —
is that one point, not a general placement mandate.**

---

**I7 — The function core has a register file and no stack.**
A function that spills cannot run. **Check the disassembly, not the source.**
*(DESIGN §0.7 D:46-47; user #1, 2026-08-27T05:17:16Z: "These functions have no stack,
instead relying main memory ... and a small local regfile (no more than a single
cache block)."; user #291 item 4, 2026-09-02T12:37:03Z: "Local functions are properly
constrainted to 512 bits of regfile space and no stack?")*

**The machine is RV64.** [SETTLED — user ruling 2026-09-02 R11: "Lets do RISCV. x86 was
chosen initially since initial develop was on PIN. RISCV is easier." The x86-64 that used
to carry this argument is HISTORY OF THE TRACE TOOLCHAIN and nothing else: development
began on PIN, which runs on an x86-64 host, so the traced binary is x86-64. It was never
the target. Ledger L46 is closed in both halves — the family by R11, and the **subset by
user ruling 2026-09-03 O4: `RV64IMAFD`** (I.0, K.6).]

Consequences that are not optional, stated on the machine's own ISA:
- **No frame pointer.** `s0`/`fp` is not established, because establishing it is a stack
  write.
- **No call from inside an offloaded function.** `jal`/`jalr` writes a return address; a
  function core has a register file and no stack to put one on.
- **An argument that arrives ON THE STACK is inadmissible — which is a rule about the
  stack, NOT a count of nine.** [CORRECTED — this clause read "**A ninth argument is
  inadmissible.** RV64's calling convention passes eight in `a0`–`a7`; the ninth arrives
  on the stack." That is a count of **64-bit registers**, exactly the reading I2 records
  as superseded (#238: "*The context is not 8 regs. Why do you keep reverting to that?*")
  and which I2 replaces with "*the admission test is therefore a test on bits*". Under
  bit-packing, **nine 32-bit arguments are 288 bits and are ADMISSIBLE**; the old clause
  ruled them out.] The binding test is **I2's**: the live set must fit in **512 bits**,
  counted in bits. What makes an argument inadmissible is that the ABI places it in
  memory rather than in the packed context — because a function core has no stack to read
  it from. **The stock RV64 convention's eight-`a`-register aperture is a TOOLCHAIN
  artefact, not the machine's limit** (R82 rejects that aperture explicitly); the compiler
  is expected to pack the context (#232, "*this needs to be handled compile-side*"), and
  a packed calling sequence that keeps 288 bits of arguments in the context passes.
- **Any `sd`/`sw` to a stack slot is a spill, and a function that spills cannot run.**
  This is the clause that actually excludes over-wide argument lists: a list that does not
  fit in 512 bits must spill, and the spill is what fails.

**How it is CHECKED is a property of the toolchain, not of the machine.** ChampSim makes
any body address outside a declared region fatal, with exactly two legal drops — the x86
`ret` pop and the x86-64 ABI callee-saved push/pop, both identified from the disassembly
and never guessed (`tools/nmfc/annotate.cc:684-719`). **Those two drops are artefacts of
tracing an x86-64 binary through PIN.** On an RV64 target the equivalent drops are the
`ra` restore and the callee-saved `s*` restores. *(The original x86-64 phrasing of these
consequences — "no frame pointer, `push %rbp` is a stack write"; "a seventh x86-64 argument
arrives on the stack" — is kept here only as the record of what the toolchain reads. Full
account in **I.0**.)*

---

**I8 — THE CLAIM SHAPE: the machine is compared against the REFERENCE algorithm doing the
SAME WORK on a standard core, and the comparison is quoted with its core model and its
branch caveat. Never against a weaker algorithm; that condemns hardware that is working.**
That is the invariant — a rule about what a claim must be, which is a property of the
machine's evaluation and not of any one run. **The digits live in Part N, where the run is
tabulated.**

[RESOLVED — this was open item **O13**, and it is closed in editing by option (b), because
R6–R10 and Appendix 3 item 8 already force it: Part B's preamble says its entries are "not
traded away for a passing measurement", and a one-run result is exactly a passing
measurement. **The current instance of this invariant is 5.67×** — the direction-optimising
BFS over a 645,268-vertex traversal, on the ChampSim core model (M.3), at a slice size no
checked-in config has or ever had (L28c), from a configuration no measurement in this
document names (**O15**). It is stated, with its three rows and both caveats, at **N.2**,
and A.7 carries it on the summary page under the same caveats. **Read the number there, not
here.**]

**The rules the claim shape carries, all of which are about claims and none of which is a
measurement:** the baseline is the reference algorithm, not a weaker one; the comparison is
on **identical work**; every quotation names **which core model** (M.3) and carries the
**absent branch-honesty sweep** (O.4 item 2); an **instruction** ratio is never paired with
a **cycle** ratio in one sentence (the 0.89×/5.67× conflation N.2 exists to stop); and the
figure that closes the loss arithmetic is the **algorithm penalty 7.38×** — N.5's row 2
cycles ÷ row 1 cycles — since `net = architecture gain ÷ algorithm penalty`, 6.73 ÷ 7.38 =
0.91. **The 5.3× from the earlier §21.3 write-up is retired** and is not a quantity this
document quotes: no tier-1..3 source states its unit, and Appendix 3 item 8 forbids
quoting a number whose unit cannot be named (this closed open item **O14**). See A.7, N.2
and N.5.
*(DESIGN §0.8 D:49-55, §21.3 D:1860-1906.)*

[CLASSIFICATION RESOLVED, AND HOW — the classification question is closed above by the
document's own rules rather than by a ruling. Part B's preamble says its entries "are not
re-derived, **not traded away for a passing measurement**"; every other I-number states a
property of the machine; the 5.67× stated the result of **one run**, on the ChampSim core
model that M.3 says the canon core deliberately differs from, at a slice size **no
checked-in config has and none ever had** (the `git log --all -S'"num_sets": 4096' --
config/nmfc` lookup was run for this revision and returns nothing — L28c), from a
configuration no measurement in this document names (**O15**). **A property of a run is
exactly what the preamble says an invariant is not**, so the number moved to N.2 and the
claim shape stayed. Nothing about the run is retracted; it is filed where runs are filed.]

`[CARRIED — the parity caveat, DESIGN §0.8 D:56-60 and §25.4 D:2447-2453]` "This
figure was measured with the ChampSim core model, whose contexts kept issuing past an
outstanding load; the Rev core sleeps on one. The two have different memory-level
parallelism at the same context count, so **the number is not a target Rev should be
expected to hit without saying which core produced it.**" See Part M.

`[CARRIED, SECOND CAVEAT — the branch-honesty caveat, DESIGN §12 D:1016]` The function
core **replays resolved control flow, so it never mispredicts**; `FLAG_TAKEN_TARGET` plus
a configurable fetch bubble is the honesty knob, ChampSim ships it (`fetch_bubble: 1` in
every config), **and the sensitivity run is not in the record** (O.4 item 2). **A
function-core speedup carries TWO caveats, not one — core model AND branch honesty**
(Appendix 3 item 8, A.7, M.3). This one was previously carried at O.4 and Appendix 3 only,
so I8 and A.7 both stated half the rule.

`[SHARPENED — user #183, 2026-08-29T20:21:31Z, what the baseline must be]` "Base
workload must mean the original amount of work (say, the full traversal of N nodes)
using the base algorithm with non-NMFC cores vs. the altered algorithm doing the same
amount of work (N node traversal) with the nmfc cores."

---

**I9 — A grain sits on the tile its *physical* address names.**
Congruence is the property the placement pass **maintains** — it chooses the frame,
and the frame it chooses is on the tile the vtile asked for — **not an arithmetic
shortcut on the virtual address**. Neither is it a counter: balancing is done by
remapping whole components in `remap_grain()`, **never by where a grain was first
touched**. **Check it, on every run.**
*(DESIGN §0.9 D:62-70.)*

The assertion that guarded this was gated on a routing order nothing used, so it had
**never executed once**, and the violation it would have caught cost **75% of
accesses routed to a tile their address never named and 27% of run time**
(DESIGN §18 D:1443-1523; `src/nmfc/nmfc_vmem.cc:540-547` records the same defect as
"75.3% of all accesses routed to the wrong tile").

[NOT A PAST DEFECT. IN THE CURRENT TREE THE ASSERTION IS STILL GATED — TWICE — AND IS
SKIPPED BY 29 OF THE 33 CONFIGURATIONS AND BY EVERY STANDARD PAGE. This was recorded
nowhere; an earlier revision of this document discussed only SST's `checkCongruent()`
(E.2a) and left the reader believing ChampSim's had been ungated.]

**Gate 1 — the routing order.** `src/nmfc/nmfc_vmem.cc:302`:

```cpp
if (router_->order() == nmfc::routing_order::VIRTUAL_FIRST && map_.is_nmfc(pa)) {
```

The `std::exit(-1)` at `:303-313` is inside it. `VIRTUAL_FIRST` is `CONGRUENT_ROUTER`
only (`tile_router.cc:36`), which **4** configs select. `PHYSICAL_ROUTER` (`:50`),
`NUCA_ROUTER` (`:73`) and `ADAPTIVE_ROUTER` (`:67`) all report `TRANSLATE_FIRST` — **22
configs, in which the assertion never runs** — and 7 configs have no router child at all.

**Gate 2 — the page's mode stamp.** `&& map_.is_nmfc(pa)`. **All 32 configs that
instantiate `NMFC_VMEM` set `"default_region": "standard"`** (`nmfc_vmem.cc:36`, `:74` —
the parameter means "*where unhinted pages go*"), so **every unhinted page is exempt by
construction.** This gate is *deliberate and correct* — `:298-301` explains it: a
`STANDARD` page "is block-interleaved across every channel on purpose ... **Asserting
congruence over both regions confuses 'the layout I chose' with 'the invariant I rely
on'**" — but it must be stated, because it means "checked on every run" is false of the
default page type even in the 4 configs that pass gate 1.

**The tree says so about itself.** `function_core.cc:941-946`, verbatim: "*The frame a
virtual grain gets is supposed to sit on the tile that grain's own address names;
**nothing checks that under `TRANSLATE_FIRST`, because the check in `NMFC_VMEM` is gated
on `VIRTUAL_FIRST`.** If the two disagree the routing decision is made on a tile the
placement pass never chose, and **the whole distribution moves**.*"

**WHAT IS ACTUALLY BUILT AS "CHECK IT ON EVERY RUN": AN ALWAYS-ON INSTRUMENT IN THE
FUNCTION CORE, previously absent from this document entirely.** `function_core.cc:938-961`
compares, **on every memory operation of every invocation and under every routing order**,
`map_.tile_of(physical)` against `map_.tile_of_virtual(virtual)`, and accumulates
`owner_hist_`, `virtual_hist_`, `migrate_target_hist_`, `incongruent_`,
`incongruent_nmfc_`, `nmfc_mode_ops_`, `standard_mode_ops_` (`:1310-1316`). It reports at
`:299-303`:

```
ROUTE physical-tile: … | virtual-tile: … | migrate-target: … | INCONGRUENT: n of m
ROUTE MODE nmfc-stamped: … standard-stamped: … | INCONGRUENT among nmfc-stamped: …
```

**Two lines, because the two failures have opposite fixes** (`:949-953`): `tile_of()`
reads the grain field for an NMFC-stamped address and the **block** field for a
`STANDARD` one, so "an unstamped page" and "a misplaced frame" are indistinguishable in a
single aggregate count. **`INCONGRUENT among nmfc-stamped` is the number that bears on
this invariant; the bare `INCONGRUENT: n of m` does not, and quoting it alone will report
a violation on a machine that is behaving exactly as designed.**

**HOW TO CHECK CONGRUENCE, OPERATIONALLY:** read `INCONGRUENT among nmfc-stamped` from
every run — it is always present and always meaningful. **Do not rely on the `NMFC_VMEM`
assertion**: in 29 of 33 configs it cannot fire, and where it can it covers only
NMFC-stamped pages. **The instrument samples where congruence is USED; the assertion
checks where it is ESTABLISHED. Both are wanted, and only the instrument is universal.**
`[RESOLVED BY THE FREEZE — user ruling 2026-09-02 R3, "ChampSim updates stop until we deem
it a good idea to go back." Ungating the assertion is a ChampSim change and is not one of
the two the user ordered (R1, R2), so it is not available.]` **The operative enforcement is
therefore the function core's always-on instrument**: read `INCONGRUENT among nmfc-stamped`
from every run. Invariant 9's "check it on every run" is satisfied by that instrument, and
the `NMFC_VMEM` assertion is a second, narrower check that fires in 4 of 33 configs and
covers only NMFC-stamped pages. **Ungating it is queued behind the freeze, not abandoned**
— ledger **L40**, status: frozen.

`[CAUTION — the check has a known hole, and it is in the step size.]` `checkCongruent()`
walked every region **by the grain**; a `STANDARD` region's unit is a **cache block**, so
that checked **one block in every 16,384** and called the rest congruent without looking
(DESIGN §32.1 D:3719-3721). **Step by the region's own unit, not by the grain**, or "check
it on every run" checks 0.006% of a block-spread region and reports a pass. Full context,
and the two other traps found making the second mapping reachable, in **E.2a**.

`[DISAMBIGUATED]` §0's phrase "the tile its own address names" is ambiguous exactly
where it must not be. It means the **physical** address. The
`VIRTUAL_FIRST`/`TRANSLATE_FIRST` gloss that appears in `nmfc/.claude/nmfc_invariants.sh:53-59`
is the rejected design and must be deleted (ledger L2).

---

**I10 — An invocation may EXTEND, never FAN OUT.**
A function that continues into another function is a **successor**: the context
carries forward, its slot is reserved in place, **one becomes one**. A function that
creates a second live invocation is a **spawn**, and spawns of spawns are unbounded
by construction — there is no admission control that makes them safe without a
per-core tracking unit *and* a depth bound, and "you may only spawn one deep" is not
a design, it is a constraint nobody can honour. **If work is discovered that the
function cannot carry out itself, the unit of work is shaped wrong: it does not own
the data it discovered. Reshape it rather than spawn.**
*(DESIGN §0.10 D:72-80.)*

*Tier-1 origin, user #181, 2026-08-29T19:39:56Z, verbatim:* "We already determined
spawn decomposition is deadlock captive. It makes sense if you extend an invocation
into a different function instead of returning (the context carries forward, reserve
in-place, essentially a successor). **It is not acceptable to be spawning two or more
contexts.** We don't have a way to manage that, unless we create a FTU inside each
nmfc core, and somehow guarantee a spawned piece of work can never deadlock. I don't
see that guaranteeable without uintuitive constraints (like you may only spawn
contexts one deep). I am a little concerned that your functions may be the wrong
shape. **You are saying you discover work, why is the function incapable of carrying
out that work itself? Why does it need to spawn more contexts?**"
*And earlier, user #87, 2026-08-28T19:41:51Z:* "Preferably it would be impossible to
deadlock by construction. **A spawn from a spawn is by nature unbounded.**"

[RULED — user ruling 2026-09-02 R1, verbatim: "delete it. CONT/extend is fine, and can
stay." SPAWN is DELETED from ChampSim; `CONT`/extend stays.] What is being deleted:
`op::SPAWN = 7` and its approving rationale in the trace format
(`inc/nmfc/nmfc_trace.h:101-114`), and `function_core::issue_spawn`, which was fully
implemented with **no admission control beyond the fabric queue and no FTU entry on the
host** (`src/nmfc/function_core.cc:870-901`). Nothing generated it —
`tools/nmfc/annotate.cc` never emitted a SPAWN record — so no trace loses anything.
**This is one of exactly two ChampSim changes the user ordered before the freeze**
(the other is R2's default router); see Appendix 2 **D0**. Ledger **L3**, RULED R1.
K.4's 0.0015 migrations/instruction measurement survives as a *target* for decomposition
work; the mechanism that produced it does not survive.

---

**I11 — Migration moves the work instead of the data, at parity.**
72 bytes of register file and PC against the **64-byte line** a foreign access would
have cost — and **the two are alternatives, never both**, so migration traffic
**subsumes** data traffic rather than adding to it. This is what makes local
atomicity free. Nor is arrival costly: **2.2–2.3 cycles measured, with a 100%
instruction-cache hit rate**, because the code is replicated on every channel and the
departing tile's slice never held the data anyway. **That figure is the translation
cold-start cost only, counted separately from the fabric hop — it is NOT the latency
of a migration.** The hop itself is the fabric's `hop_latency`, 8 cycles in the shipped
configuration (D.5, F.7, ledger L7). The only real cost is **time in
transit**, so functions still stay planted on one tile and do non-trivial work there
— budget roughly one migration per thousand instructions — **but for latency, not
bandwidth.**
*(DESIGN §0.11 D:82-91.)*

*Tier-1 origin — and it is a reversal the user made himself, nine minutes apart.*
First, #174, 2026-08-29T17:28:40Z: "migration is actually very bandwidth inefficient.
If we conclude we need any data size from a different channel, we must transmit the
full regfile + PC to a different core (72 B) ... Compared to just fetching the data
... 8 bytes."
Then, #175, 2026-08-29T17:37:11Z, **which supersedes it**: "The question is if any
real fabric supports a sub-cache-line data transfer, and I think the answer to that is
probably no. If this is the case, migration is actually interesting, because it says
'**transfer the work, not the data**' and costs roughly the same. Then, atomicity does
fall out for free this way, with no bandwidth overhead. **I was assuming migration
traffic was additive to data traffic, but they are actually subsuming.** ... So, if
work migrates, NUCA and NUMA are irrelevant except for the key point: **If there is
any runtime optimization to be made at all, it is to make sure migration remains
classified as sub-optimal.**"

`[SHARPENED — user #176, 2026-08-29T17:41:40Z]` "locality is not really an issue
either. The address is different, the local LLC slice was never going to have the
data, and **the instructions exist on every channel**, so it is only a matter of
whether the local I-cache has those instructions already or not. It is actually
really simple to start up a context, which is why it might be worth it to migrate."

`[CAUTION — DESIGN §25.3 D:2423-2429]` The 100% instruction hit rate is a
**measurement under the ChampSim model, not a property to design around**. Treating
it as an invariant is how the instruction side nearly went unmodelled on Rev. The
user said so directly (#250, 2026-09-01T17:38:01Z): "**100% instruction hit rate is
not an invariant. that is something you fabricated.** The instruction cache hit rate
will likely be 100%, but we should be modelling a cache. Anything else is frankly
inexcusable."

---

**I12 — Tiles are partitioned by *physical* address, and co-location is the vtile's
job.**
Virtual-address partitioning is **rejected**, for four stated reasons: it **(1) leaks
hardware-specific detail into the virtual address space**, **(2) exposes the tile
layout directly**, **(3) confines the compiler to a fixed mapping**, and **(4) lets a
program steer placement by choosing addresses**, which is unfriendly to a shared
system.

A hint is a **vtile** — a compiled-in label naming a **coherent set**. Pages carrying
the same vtile are co-located *wherever they sit*; distinct vtiles are unrelated —
**vtile 1 and vtile 5 have nothing to do with each other** — and are spread to
balance load, unless that vtile already has a home, which its later pages follow.
**Nothing has to be adjacent, aligned, or contiguous for two things to land
together.** Grain alignment is a **space** concern only — it stops a small object
obliging the allocator to spend a whole grain — and the one thing a grain genuinely
cannot do is carry two *types*, because half of it cannot be duplicated on every tile
while the other half is silo'd to one.
*(DESIGN §0.12 D:99-111, §5.0 D:494-512, §5.0.3 D:556-570.)*

*Tier-1 origin, user #269, 2026-09-01T20:11:19Z, the single most complete statement:*
"Tiles are partitioned via virtual addresses. This means that the destination tile is
a partition of the virtual address space. Identification of where data is located is
immediately known. It also means the compiler can only offer a fixed mapping. **This
is a bad idea. It leaks hardware-specific details into the virtual address space,
exposes the tile layout directly, and can be manipulated by programs in ways
unfriendly to shared systems. Physical-address partitioning is all-around better.**
While we have to translate on dispatch, translation is largely amortized since
dispatch is once-per-function. Translation was already necessary, so this just puts
migration triggers post-translation instead of pre-translation. **It allows for hints
about co-location to not be tied to particular virtual address ranges. It requires no
direct-mapped VA-PA space.**" *And the message's own closing sentence, which must not
be truncated away because it is what leaves the translation surface open (see I3 and
ledger L35):* "**I bring up all these points because I am not sure what the right final
surface is.** I was disappointed that you forgot the extensive conversations we had
about this exact topic."
*And the vtile definition, user #271, 2026-09-01T20:18:16Z:* "hints are meant to be
compiled-in to tell the OS what virtual pages belong to the same coherent-set. These
are essentially **virtual tiles** that can be associated with grain page(s), so pages
marked vtile 0 all get co-located with other vtile 0 pages. **vtile 1 and vtile 5 are
not related, so will be assigned to distribute load across the tiles unless existing
vtiles of 1 or 5 are already present on such tiles.**"
*And, user #277, 2026-09-01T22:04:12Z:* "You understand with hints, **grain-alignment
only saves space**, you can still indicate 'both grains should end up on tile N' by
hinting them with the same vtile?"

[SHARPENED — THE GEOMETRY RULE. User ruling 2026-09-02, on the memory organisation the
partition is taken over. It is stated here because the partition invariant is where a
locked-in count does its damage: `G` is a function of the geometry, and `G` fixes the NMFC
page size, the tag granularity and the silo granularity at once.]

> "**Ranks are included.** 32 banks per channel assumes DDR5 and one rank. **32 banks per
> channel is not part of the design or spec, that is part of the memory technology, the
> entire system must adapt to an arbitrary bank count, tile-memory-sizing, grain-sizing.
> Please do not lock in any bank/rank/column/row/channel counts as if they were the only
> ones supported. WE MUST SUPPORT ALL POSSIBLE VALUES FOR EACH, WITHIN A FULL 48-bit
> PHYSICAL ADDRESS SPACE.**"

**What that binds, in four clauses:**
1. **Ranks count.** `banks_per_channel` in the grain formula is the product of **every**
   organisation level between the channel and the row — ranks, bank groups and banks on
   DDR5; pseudo-channels, Sid, bank groups and banks on HBM3 — and the rule is
   **positional, not a named list** (E.4). This also closes ledger **L8**: #141's "32 banks
   per channel" was DDR5 at one rank, a device description, not a spec.
2. **No count is locked.** Banks, ranks, bank groups, rows, columns and channels are all
   **arbitrary**. Any statement of the form "the machine has 32 banks per channel" or "4
   channels" is a description of a configured device and belongs in **SELECTED
   CONFIGURATION**, never in the design.
3. **The DRAM physical address space is 48-bit, and every geometry must work inside it.**
   The **tile-select field and the grain offset live in that space** (C.2's bit figure),
   and the constraint `mode_bit >= grain_bits + log2 N` must hold **at every geometry**,
   not at the one that was measured.
   [DISAMBIGUATED — this clause previously said "the mode bit … live[s] in that space",
   which contradicts E.2 (`mode_bit + 1` bits, "exactly one bit wider than DRAM") and the
   R13 row in SELECTED CONFIGURATION ("carried as an **extra** bit on every physical
   address"). **Two widths, two names, and the ruling is about the first:**]
   - **The DRAM physical address space is the 48 bits the ruling names.** "ALL POSSIBLE
     VALUES … WITHIN A FULL 48-bit PHYSICAL ADDRESS SPACE" binds the *device* geometry:
     any bank/rank/bank-group/row/column/channel count whose product addresses up to
     2⁴⁸ bytes must work. Grain offset and tile-select are fields **inside** it.
   - **The CARRIED address is one bit wider, because the mode bit rides above it.**
     `mode_bit` is the position just above the top of DRAM, so at a full 48-bit DRAM
     space `mode_bit` = 48 and the carried address is **49 bits** — which is what E.2's
     `mode_bit + 1` says. **`tile_map.h`'s address word must therefore be sized to 49
     significant bits at the maximum geometry, not 48.** It is stripped at the DRAM port
     (`strip_mode()`), so **nothing below the port ever sees more than 48**.
   - **Neither statement gives ground.** The full 48-bit DRAM space is supported *and*
     the mode bit is outside it; there is no geometry at which supporting one costs the
     other, because the extra bit is above DRAM, not carved out of it.
4. **Tile memory size and grain size adapt with the device.** `G` is not a constant and
   never was (E.3). A tool, a config or a test that hardcodes a grain size is wrong at the
   next device — which is exactly the defect ledger L20 records (31 of 33 configs declare
   `grain_bits` 21, which **no** geometry in the tree derives: the default controller and
   ramulator DDR5 both give **20**, and ramulator HBM3 gives **18**). **Three geometries,
   three answers, and that is the rule working — the defect is the hardcoded 21.**

---

**I13 — This is a standard memory system with one change.**
A modern machine is: private L1I/L1D + L2 per core, a shared LLC across cores, then
memory controllers and channels. **The L2-to-LLC interconnect *is* a fabric, and
coherence is enforced at that boundary.** NMFC alters exactly one thing **in the memory system**: **the
address partition moves to the fabric**, slicing LLC → memory controller → channel
**vertically**, instead of waiting until after the LLC to slice. (The qualifier matters:
the machine also gains a function core, a tracking unit and an instruction set. What is
unchanged is the *hierarchy*. See A.2.) **Do not derive
anything from a picture more exotic than that.** The host reaches memory by the
ordinary path — L1, L2, fabric, the LLC slice owning the address — and no part of
that is NMFC-specific.

**The function core lives at the top of one vertical stack**, beside the fabric
interface into that LLC slice, **on the slice's side**. So its own path — fetch,
load, store, page walk — never crosses the fabric; **I3 is a consequence of where the
core sits, not a separate rule.** There is **one** fabric, carrying coherence,
migration, and LLC/DRAM access. **A model that puts a tile's own caches on a network
has moved the core off the tile no matter what the diagram says**, and a second
interconnect for NMFC traffic is what makes I11 unmeasurable.

`[CONFLICT — ChampSim implements exactly the topology this forbids. BLOCKING.]`
`config/nmfc/nmfc_4tile.json` declares **two entirely separate interconnects**:
`cpu0_fabric`, an `INTERLEAVE_FABRIC` (`hop_latency` 4, `queue_size` 64, `max_forward`
4, over `@fabric_tile0_channel … @fabric_tile3_channel`) which carries **only host L2C
misses to the LLC slices** (`src/nmfc/interleave_fabric.cc:2-8`: "One instance sits
below each compute tile's last private cache"); and `fn_fabric`, a `FUNCTION_FABRIC`
(`hop_latency` **8**, `queue_size` 128, `max_deliver` 4) with its own per-destination
deques for invocations, migrations and completions
(`src/nmfc/function_fabric.cc:484-497`). **They share no queue, no bandwidth budget and
no latency; a migration never touches `cpu0_fabric`.** Tier 1 wins: there is ONE fabric.
The consequence is not cosmetic — **J.2's parity/subsumption claim is only true if
migration and data contend for the same interconnect, so on ChampSim as shipped that
claim is not measurable at all.** See ledger **L25**.
*(DESIGN §0.13 D:113-130, §5.9 D:694-874.)*

*Tier-1 origin, user #284, 2026-09-02T02:32:58Z, verbatim and in the user's own
capitalisation:* "A REGULAR, STANDARD, MODERN MEMORY SYSTEM has private L1 (D/I) + L2
FOR EACH STANDARD CORE, A SHARED LLC ACROSS CORES, AND THEN THE MEMORY CONTROLLER.
THE L2s -> LLC is a FABRIC, LIKE THE ONE DESCRIBED. **THE ONLY THING WE ARE ALTERING
OVER A REGULAR MEMORY SYSTEM IS THAT WE DO THE ADDRESS PARTITION AT THE FABRIC,
SLICING THE LLC -> MEM CONTROLLER -> MEM CHANNEL STACK VERTICALLY INSTEAD OF WAITING
UNTIL POST-LLC for the slice. REAL MACHINE ONLY ENFORCE COHERENCE AT THE L2 <-> LLC
BOUNDARY.** IS THIS ALL MAKING SENSE? Our tiles live on each vertical stack, at the
very top, next to the fabric interface communicating to the LLC. Host goes through
the regular path. **The vast majority of traffic through the fabric is either
coherence, migration, or LLC/DRAM access.**"
*And, user #227, 2026-09-01T04:41:17Z:* migration is to be built "**preferably as a
generic fabric packet, not it's own channel**".
*And, user #236, 2026-09-01T06:01:18Z:* "we should do it right. Make it over the
fabric. **We don't want to attach any nmfc processor to a particular standard core.
That is part of the point.**"
*And, user #261, 2026-09-01T19:09:33Z:* "Tiles are address-partitioned. **Inside each
tile is the nmfc core + caches, LLC cache, memory controller, and memory channel.**"

---

**I14 — The tile sees the coherence traffic for its own slice, and wins it.**
Sitting at the fabric interface into the slice is what makes this true: a reference to
a block the function core is touching is **visible** to it, and can therefore be
treated as ownership. **NMFC cores take strict priority in MOESI.** Traffic between
invocations on one core pays **no coherence penalty at all** — they share a slice and
there is no second copy. What can cost something is a **host** reference to a block an
NMFC core is modifying, **and that direction only**: an NMFC core does not pay for
host modifications to its lines where that can be avoided. This is also why migration
stays cheap in practice rather than by assertion — offloading the shared and
memory-bound work to the cores is what makes host memory and coherence traffic
**rare**, and that is the headroom migration runs in.
*(DESIGN §0.14 D:132-143, §5.9 D:732-754.)*

*Tier-1 origin, user #284, 2026-09-02T02:32:58Z, continued:* "the placement of the
tiles is important, because that coherence traffic is visible to the memory tile, and
thus references to blocks that the nmfc is touching can be treated as ownership.
**NMFC cores get strict priority in MOESI, and inter-function traffic within the nmfc
core doesn't pay any coherency penalty, just host references to the blocks an nmfc is
modifying. Never an nmfc paying for host modifications to it's lines, if it can be
avoided.**"

**The justification is not that near-memory cores deserve to win arguments.** It is
that the function core is the **ordering point** for its address range **by
construction** — every invocation touching that range has migrated there — so it is
the party that can serialise **without a protocol**, and making it yield to a remote
requester would replace a free ordering point with an expensive one.

`[CAUTION]` The protocol must be **MOESIF or equivalent**, not MESI (user #285,
2026-09-02T02:54:13Z: "Implement MOESIF or something similar"). `O` is dirty+shared
owner; `F` is the clean designated forwarder. memHierarchy has `O` in its state enum
and **no coherence manager ever assigns it**, and has no `F` at all — which is why
memHierarchy is used **below** the coherence point and nowhere above it
(DESIGN §5.9 D:765-771). Priority is a **strict order**, not a tie-break, and MESI
cannot express it.

`[RULED A BUG, NOT A DESIGN QUESTION — user ruling 2026-09-02 R5, verbatim: "That sounds
like a bug. Needs investigation. **The image being written into memory should itself be
trivial, done by the OS at load. Amortized on any reasonable-length-running program. If it
is impacting us in any way, that suggests a bug.**"]`

The one measured table for this (DESIGN §5.9 D:773-783) reports `hostPaysSnoop` 9 against
`nmfcPaysSnoop` 24 — the *avoided* direction dominating the permitted one by 2.7×. **That
is a defect in the simulated environment, and the invariant is not weakened by it.**

**The diagnosis the ruling gives, and it names the suspect.** The 24 are believed to come
from L15: the loader writes the program image through the **host** cache, leaving static
data **Modified** in a host cache, so every first tile touch of that data is a snoop the
tile pays. **In a real system the OS writes the image at load, that cost is trivial, and it
amortises away on any program that runs for a reasonable length of time.** A ratio that
does not amortise means the model is charging something a real machine does not.

**Status: SUSPECTED BUG IN THE SIMULATED ENVIRONMENT, investigation queued.** Ledger
**L14** and **L15**, RULED R5. **I14 stands as written** — it is a statement about which
direction pays, and a measurement that points the other way is evidence about the
simulator, not about the invariant. **Do not quote the 9-vs-24 table as evidence for or
against I14 until the loader path is fixed.** Separately, DESIGN §5.9 D:830-838 records the
priority mechanism firing **once in 33,317 requests**, i.e. "exercised rather than merely
compiled, but it is not yet a measured effect, and **it should not be reported as one**";
that remains true.

---

## PART C — DIAGRAMS

Five diagrams. Each is a statement of the design, not an illustration of it; where a
diagram and prose could be read as disagreeing, the prose in Part B governs.

**Two terms these diagrams use before Part E defines them, stated here so Parts A–C can
be read on their own** (they are load-bearing from invariant I9 onward and were
previously first defined 364 lines after their first use in a routing formula):

- **`G`, the GRAIN** — the unit of physical allocation and of tile assignment under NMFC
  mode. `G = row_bytes_per_channel × banks_per_channel × total_channels`. **The third
  factor is `total_channels`, never `N`** — see the disambiguation below. It is the size at which a
  page type may be tagged, the size at which data is silo'd to one tile, and the NMFC
  data page size, **all three at once and not by coincidence** (E.3). **It is not a free
  parameter and not a constant**: it is derived from the DRAM organisation and changes
  with the machine — 512 KiB at two tiles and 1 MiB at four on the same device (E.3;
  DESIGN §5.2 D:607-623; #269, 2026-09-01T20:11:19Z, "*the size necessarily changes with
  the machine's memory configuration itself*"). [CAUTION — the tree does not agree with
  itself on `grain_bits`: the default controller and ramulator DDR5 both require 20 (and
  ramulator HBM3 requires 18) while 31 of 33 configs declare 21, which NO geometry derives.
  Ledger L20; the value itself is **configuration — see SELECTED CONFIGURATION** (user ruling 2026-09-02 R6–R10).]
  [CORRECTED — BLOCKING, AND THIS IS THE OCCURRENCE A READER HITS FIRST. This line read
  `G = row_bytes_per_channel × banks_per_channel × N` — the exact form E.3 marks BLOCKING
  and orders never written. E.3's **[DISAMBIGUATED — BLOCKING]** block: "This formula
  appeared three times in this document with three different third factors — `N` in the
  notation table, `num_channels` here, `total_channels` at E.5 ... **Write
  `total_channels`. Never write `N` in this formula**." The NOTATION table and E.3 itself
  were corrected; this Part C occurrence was not, and it is the one introduced as the
  statement that lets "Parts A–C be read on their own", i.e. the corrected form was
  unreachable at the point of first use. **`total_channels` = channels declared per
  Ramulator2 instance × `N`; it equals `N` only when each instance declares exactly one
  channel, which is a property of the device file and not of the formula.** E.3 records
  that reading `N` here once silently halved the channel count from 8 to 4.]
- **a GRAIN, the object** — one `G`-sized, `G`-aligned chunk of physical memory. "Grain
  *g*" means the chunk at physical offset `g × G`; under the NMFC layout it lives on tile
  `g mod N` (E.1).

Full derivation, the bank-count ruling, and the odd-tile-count caveat are in **E.3–E.5**.

### C.1 The physical machine

**Read this one before any other.** One fabric. Host cores on the left with the
ordinary private hierarchy. N vertical stacks on the right, each a tile. The function
core is **inside** the tile, above the slice, **on the slice's side of the fabric
interface** — never across it.

```mermaid
flowchart TB
  subgraph HOSTS["Host side — the ordinary path, nothing NMFC-specific"]
    direction LR
    subgraph H0["Host core 0 — OoO"]
      C0["core + FTU"] --> L1I0["L1I"]
      C0 --> L1D0["L1D"]
      L1I0 --> L20["L2 private"]
      L1D0 --> L20
    end
    subgraph H1["Host core 1 — OoO"]
      C1["core + FTU"] --> L1I1["L1I"]
      C1 --> L1D1["L1D"]
      L1I1 --> L21["L2 private"]
      L1D1 --> L21
    end
  end

  L20 --> FAB
  L21 --> FAB

  FAB{{"THE ONE FABRIC — the L2 to LLC interconnect<br/>address partition happens HERE<br/>THE COHERENCE POINT: the MOESIF directory sits HERE, and NOWHERE ELSE<br/>NMFC strict priority<br/>carries: coherence + migration + LLC/DRAM access"}}

  subgraph T0["TILE 0 — one vertical stack"]
    direction TB
    IF0["tile fabric PORT 0<br/>(a port, NOT a directory —<br/>the directory is in FAB)"]
    FC0["FUNCTION CORE 0<br/>barrel, C contexts<br/>512b regfile each, no stack"]
    FI0["fc I$ banked"]
    FD0["fc D$ banked"]
    MM0["tile MMU 0<br/>TLB + hardware walker<br/>WALKS THE LOCAL COPY<br/>OF THE ONE PAGE TABLE"]
    S0["LLC SLICE 0<br/>banked, aligned to DRAM banks"]
    M0["memory controller 0<br/>one ramulator2 instance"]
    D0[("DRAM CHANNEL 0")]
    IF0 --- S0
    IF0 <--> FC0
    FC0 --> FI0
    FC0 --> FD0
    FC0 --> MM0
    FI0 --> S0
    FD0 --> S0
    MM0 -->|"walk references —<br/>the 4th upper port of the slice"| S0
    S0 --> M0 --> D0
  end

  subgraph T1["TILE 1 — one vertical stack"]
    direction TB
    IF1["tile fabric PORT 1"]
    FC1["FUNCTION CORE 1"]
    FI1["fc I$"]
    FD1["fc D$"]
    MM1["tile MMU 1"]
    S1["LLC SLICE 1"]
    M1["memory controller 1"]
    D1[("DRAM CHANNEL 1")]
    IF1 --- S1
    IF1 <--> FC1
    FC1 --> FI1
    FC1 --> FD1
    FC1 --> MM1
    FI1 --> S1
    FD1 --> S1
    MM1 --> S1
    S1 --> M1 --> D1
  end

  subgraph TN["TILE N-1 — one vertical stack"]
    direction TB
    IFN["tile fabric PORT N-1"]
    FCN["FUNCTION CORE N-1"]
    FIN["fc I$"]
    FDN["fc D$"]
    MMN["tile MMU N-1"]
    SN["LLC SLICE N-1"]
    MN["memory controller N-1"]
    DN[("DRAM CHANNEL N-1")]
    IFN --- SN
    IFN <--> FCN
    FCN --> FIN
    FCN --> FDN
    FCN --> MMN
    FIN --> SN
    FDN --> SN
    MMN --> SN
    SN --> MN --> DN
  end

  FAB --- IF0
  FAB --- IF1
  FAB --- IFN
```

**What this diagram asserts, in words, so it cannot be misread:**
1. The function core's **memory** paths — fetch, load, store **and page walk** —
   terminate at **its own slice** and never touch the fabric. That is I3, obtained
   structurally. **The walk path is drawn**: `FC0 --> MM0 --> S0`. It is the slice's
   **fourth** upper port (D.5's port table: compute-tile fabric, fc D, fc I, **this
   tile's MMU**), and an earlier revision of this diagram asserted walk-locality while
   drawing no MMU node and no walk edge — asserting the invariant on a structure the
   picture omitted, which is the same shape as ledger L26's finding that the walk
   assertion is absent exactly where foreign walks are possible. C.2 shows what the MMU
   contains and why the walk resolves locally; **C.2's `PT` box connects to physical
   memory through THIS edge.**
2. The function core's **control** path — receiving an invocation, sending and receiving
   a migration, sending a completion — **does** cross the fabric, through the tile's
   fabric **port**. [CORRECTED — this assertion previously ended "That is the
   `IF0 <--> FC0` edge, and it is **the only edge in the picture that carries a context**",
   which is **false in the diagram it glosses and contradicted by its own first clause**.
   Three identical context edges are drawn — `IF0 <--> FC0`, `IF1 <--> FC1`,
   `IFN <--> FCN` — and the clause "*does cross the fabric*" requires the path
   `FC0 → IF0 → FAB → IF1 → FC1`, so the fabric links carry contexts too. Under the
   literal reading, tiles 1…N-1 receive no invocations and **migration between tiles is
   undrawable**.]

   **The correct statement, in two parts:**
   - **`IF<t> <--> FC<t>` is the only KIND of INTRA-TILE edge that carries a context.**
     Every other edge inside a tile (`FC → FI/FD/MM → S`) carries memory references. One
     such edge exists **per tile**, not one in the diagram.
   - **The fabric links `FAB --- IF<t>` carry contexts as well**, and they must: an
     invocation reaches tile *t* as `FAB → IF<t> → FC<t>`, and a migration from tile 0 to
     tile 1 is `FC0 → IF0 → FAB → IF1 → FC1`. **There is no other drawn path between two
     function cores.**

   **Without these edges nothing could ever reach a function core**, and a diagram that
   omits them (an earlier revision of this one did) makes A.4, A.5, I11 and C.4
   unreadable. **Memory never crosses; work does.**
3. The host's path to any slice is L1 → L2 → fabric → slice. There is **no**
   NMFC-specific host memory path.
4. **Where the coherence point is — ONE location, stated so an implementer knows how many
   directories to instantiate.** `[DISAMBIGUATED — three earlier readings of this diagram
   put the MOESIF directory in three different places, and assertions 2 and 4 of the
   earlier gloss contradicted each other six lines apart.]`
   - **There is exactly ONE MOESIF directory, and it is IN THE FABRIC** — the `FAB` node,
     at the L2↔LLC boundary. **Not N of them.** C.5 draws exactly one `DIR`, and it is
     the same object.
   - **The node inside each tile is a PORT, not a directory.** It was previously labelled
     "fabric interface", which collided with the phrase "the fabric interface, where the
     MOESIF directory sits" and made the picture read as N directories. It is renamed
     `tile fabric PORT` here for exactly that reason. **N ports, one directory.**
   - **A function core's requests do not traverse the directory**, because the core is
     already on the slice's side of it — its edges to memory are `FC0 → FI0/FD0/MM0 →
     S0`, none of which pass through `IF0`. The `IF<t> <--> FC<t>` edges carry
     **contexts, not memory references**, so a context crossing one is not a coherence
     event. **Assertion 2 and this assertion are about different traffic on different
     edges; they do not conflict.**
   - **AND THE CASE THIS BULLET DID NOT COVER, which its earlier "only edge" wording
     concealed: a MIGRATING context DOES cross `FAB`**, the node labelled "**THE COHERENCE
     POINT: the MOESIF directory sits HERE, and NOWHERE ELSE**". **That is still not a
     coherence event.** The directory orders **lines, by address**; a migration packet is
     72 bytes of register file and a PC, it names no line, it has no home, and it is not
     cached anywhere. **It traverses the same physical fabric as coherent traffic without
     entering the protocol** — the fabric is one wire carrying two kinds of message, and
     only one of them is coherent. **An implementer sizing the directory counts memory
     references only; an implementer sizing fabric BANDWIDTH counts both** (I11's 72-byte
     migration is real traffic on that link).
   - **The slice is where host and function-core traffic converge in ADDRESS SPACE** —
     one home per line, by address — which is what makes the function core the ordering
     point for its own range (I14). "The fabric is the coherence point" and "the slice is
     where they converge" are one statement about two adjacent structures.
   - **A design that puts a directory between a function core and its own slice has moved
     the core off the tile (R51).**
5. There is **one** fabric. A picture with a second network for NMFC traffic is
   wrong, and it is the specific error that makes I11 unmeasurable. **ChampSim as
   shipped has two — see I13's `[CONFLICT]` block and ledger L25.**
6. Putting a tile's caches *on* a network moves the core off the tile, whatever the
   diagram claims.

### C.2 The address path

Placement is decided at translation. Routing reads the **physical** address. There is
**one page table per address space**, and it lives on duplicate pages, so the tile's own
copy resolves even a *foreign* address without leaving the tile (I3, user ruling
2026-09-02 R12). **The TLB is shared and ASID-tagged** — sharing a translation *cache*
across address spaces is what an ASID is for, and is a different thing from sharing a
*table*.

```mermaid
flowchart TB
  VA["VIRTUAL ADDRESS<br/>the only thing a function or a host holds"]
  CTX["per-context translation slots<br/>1 code + a few data<br/>dropped on migration"]
  TLB["SHARED, ASID-TAGGED TLB — two arrays probed in parallel<br/>small = 4 KiB pages, huge = G-sized pages<br/>shared across address spaces, tagged by ASID"]
  PT["LOCAL COPY OF THIS ADDRESS SPACE'S PAGE TABLE<br/>ONE table PER ADDRESS SPACE,<br/>on DUPLICATE pages, so every tile holds a copy<br/>5 levels, multiple page sizes, mode bit in the PTE<br/>WALKS NEVER LEAVE THE TILE"]
  PA["PHYSICAL ADDRESS"]
  MODE{"mode bit<br/>one bit, one position above the top of DRAM<br/>stamped at ALLOCATION, never changes"}
  NMFCM["NMFC mode = 1<br/>tile = bits of pa above log2 G, mod N<br/>a whole grain on one channel"]
  STDM["STANDARD mode = 0<br/>tile = bits of pa above log2 block, mod N<br/>blocks spread over every channel"]
  FABP["THE FABRIC — partition happens here"]
  TILE["the owning tile's LLC slice / controller / channel"]

  ASID["ASID — names WHICH page table<br/>part of every translation, remap and shootdown"]

  VA --> CTX
  ASID --> TLB
  ASID --> PT
  CTX -->|miss| TLB
  TLB -->|miss| PT
  PT --> PA
  CTX -->|hit| PA
  TLB -->|hit| PA
  PA --> MODE
  MODE -->|bit set| NMFCM
  MODE -->|bit clear| STDM
  NMFCM --> FABP
  STDM --> FABP
  FABP --> TILE
```

**Ordering, stated literally because it has been inverted repeatedly:**
**translate first, then route.** A tile must resolve an address **before** it can
know whether it owns it. The reverse order — `tile_of(virtual_address)` deciding
local-vs-migrate before any translation — is the **rejected** design (P.1, R1).

**Where the page table physically LIVES, which this diagram does not draw.** The `PT`
box above is a *lookup*, not a structure floating free of memory: the table lives on
**duplicate pages**, which are ordinary physical memory, so every walk reference goes
down **this tile's** stack — MMU → LLC slice → controller → channel. That is the
`MM0 --> S0` edge in C.1, the slice's fourth upper port (D.5), and it is what makes
"walks never leave the tile" a structural fact rather than an assertion. **A walk
reference costs slice and channel bandwidth like any other reference**; DESIGN §26.3
notes the SST model charges 30 flat cycles and issues none, which is divergence S39.

**[ADDED — THE PHYSICAL ADDRESS AS A BIT FIELD. This figure was the single
highest-value thing missing from this document: the mode bit, the tile-select field, the
grain bits, the block bits and the compacted slice-local form were stated in prose in four
separate places (C.2, E.1, E.2, F.9) and drawn nowhere. Everything in it is read off
`inc/nmfc/tile_map.h:93-135, 145-204` and E.1's constraint; nothing is invented. It
replaces E.1's three-statement mode-bit reconciliation table, which existed only because
there was no picture.]**

```
                 <-- more significant                    less significant -->

FULL PHYSICAL ADDRESS, NMFC MODE (mode bit = 1)
+------+---------+-----------------------+------------+-------------------+
| ...0 |    1    |    frame / row bits   |  TILE      |   grain offset    |
+------+---------+-----------------------+------------+-------------------+
        ^                                 ^  log2 N   ^                   ^
        |                                 |  bits     |                   bit 0
        mode_bit                          |           grain_bits = log2 G
                                          tile field starts at grain_bits

FULL PHYSICAL ADDRESS, STANDARD MODE (mode bit = 0)
+------+---------+---------------------------------+------+--------------+
| ...0 |    0    |         frame / row bits        | TILE | block offset |
+------+---------+---------------------------------+------+--------------+
        ^                                            ^ log2 N            ^
        mode_bit                                     |  bits             bit 0
                                                     tile field starts at block_bits

SLICE-LOCAL ADDRESS — what a slice, controller and channel actually index
(compact() has excised the tile field; high part slid DOWN by log2 N)
+------+---------+-----------------------+-------------------+
| ...0 |  mode   |    frame / row bits   |   offset (as above)|
+------+---------+-----------------------+-------------------+
        ^         ^                       ^
        |         |                       shift = select_shift(mode)
        |         high part, shifted down by tile_bits = log2 N
        mode bit SURVIVES compaction and is preserved by expand()

DRAM-PORT ADDRESS — one step lower still
+----------------------------------------+-------------------+
|            frame / row bits            |      offset       |
+----------------------------------------+-------------------+
  mode bit STRIPPED here (strip_mode) and nowhere earlier.
```

**Reading the figure, with the four rules it makes visible:**

1. **`shift = select_shift(mode)`** is the *only* thing the mode bit changes about the
   layout: it is `grain_bits` when the bit is set and `block_bits` when it is clear
   (`tile_map.h:204`). The tile field is `log2 N` bits wide in both, and it always sits
   immediately above `shift`.
2. **The constraint is a picture, not a table.** `mode_bit >= grain_bits + log2 N` is
   exactly "the mode bit must lie strictly above the tile field **in the NMFC layout**",
   which is the taller of the two — and it is asserted in the code at
   `tile_map.h:100-101` in those words ("*the tile field must fit strictly below the mode
   bit in both layouts, or compaction would collide with the mode flag*"). The
   *convention* — "one position above the top of DRAM" — is a choice of where to put
   `mode_bit` that satisfies the constraint. The shipped **bit 38** against a 64 GiB
   (bit-36-top) model is two positions above: it satisfies the constraint and departs
   from the convention. **All three statements are the same figure with `mode_bit` at
   three different heights.**
3. **Compaction is a slide, not a re-encoding.** The field between `shift` and the mode
   bit moves down by `log2 N`; nothing below `shift` moves; the mode bit does not move.
   That is why `compact`/`expand` are exact inverses and pure functions of
   (address, tile) with no bookkeeping (E.1).
4. **The mode bit dies at the DRAM port and not one level earlier**, because the slice
   and the controller must still be able to tell the two layouts apart in order to
   compact and expand correctly; the DRAM must not, because a device decoder would
   collapse both modes onto the same row (E.2, E.2a).

Two further facts the diagram cannot show:
- The **tile field is compacted out** of the physical address on the way into a slice
  and re-inserted on the way out, because the tile-select bits sit inside a slice's
  set index and a slice would otherwise use only 1/N of its sets. `compact` and
  `expand` are exact inverses and pure functions of (address, tile), so no
  per-request bookkeeping exists. *(DESIGN §5.6 D:674, in those words, with the knob
  `compact_tile_bits`, default true.)*

  **`[ADDED — the bit manipulation, which this document asserted five times and never
  wrote down. It is fully specified in tier 2 and needs no derivation.]`**
  `inc/nmfc/tile_map.h:145-204`. Let `shift = select_shift(mode)` — **`grain_bits` when
  the mode bit is set, `block_bits` when it is clear** (`:204`), which is why "the
  compaction differs by mode, and the mode is in the address, so the fabric picks the
  right one with no extra state".

  ```
  compact(addr):  mode = addr & mode_mask
                  body = addr & (mode_mask - 1)
                  low  = body & ((1 << shift) - 1)
                  high = (body >> (shift + tile_bits)) << shift
                  return mode | high | low                       // tile field excised

  expand(c, t):   mode = c & mode_mask
                  body = c & (mode_mask - 1)
                  low  = body & ((1 << shift) - 1)
                  high = (body >> shift) << (shift + tile_bits)
                  return mode | high | (t << shift) | low        // tile field reinserted
  ```

  **`compact`/`expand` ARE A CONSEQUENCE OF PARTITIONING AT THE FABRIC, NOT A SEPARATE
  DESIGN CHOICE — user ruling 2026-09-02 R16, "Okay, sounds correct."** I13 moves the
  address partition to the fabric and slices LLC → controller → channel *vertically*. The
  tile-select bits therefore sit **inside** what would otherwise be a slice's set index, so
  a slice presented with the full physical address would use only `1/N` of its sets and a
  tile's DRAM would address only `1/N` of its rows. Excising the field on the way in and
  reinserting it on the way out is the only thing that makes a vertical slice a dense
  address space — so it follows from where the partition is, and DESIGN §5.6 D:674
  describes exactly this. **It is ratified as built** (`inc/nmfc/tile_map.h:145-204`,
  `compact_tile_bits` default true, invertibility unit-tested at
  `test/cpp/src/550-nmfc-tile-map.cc:106`).

  **The mode bit rides above the excised field and is preserved by both**, which is what
  makes the pair mode-correct without carrying the mode separately. A **virtual**
  compaction exists too — `compact_virtual` (`:169-174`), which always uses `grain_bits`.
  **The invertibility property DESIGN §12 D:1014 names as a unit test is a real,
  checked-in test**: `test/cpp/src/550-nmfc-tile-map.cc:106`, "Compaction is exactly
  invertible over a large address sample", with `:123` pinning that it preserves the mode
  flag and the low offset and `:138` that each tile gets a dense address space. **Ratify
  **RATIFIED — user ruling 2026-09-02 R16, "*Okay, sounds correct*": `tile_map.h`'s form IS
  the canon, and it is a CONSEQUENCE of partitioning at the fabric, not a separate design
  choice. See the paragraph below the figure.** The **mode bit survives compaction** and is
  stripped one level lower, at the DRAM port, because the DRAM does not need it and
  a device decoder would collapse the two modes onto the same row (DESIGN §5.3
  D:625-645; §3 D:255 — `NMFC_MEMORY_CONTROLLER` "reads and strips the mode bit, then
  applies the selected mapping"; E.2, E.2a).
- The **channel bits are removed** before a channel-local physical address is handed
  to that tile's ramulator2 instance (user #50, 2026-08-28T03:18:30Z).

### C.3 Page types, and where each lands

**THE USER SAID THREE TYPES. This diagram draws FOUR. Read why before using it.**

*User #269, 2026-09-01T20:11:19Z — the newest and most complete statement, verbatim:*
"**We have 3 page types. We have regular pages (striped across memory tiles), grains
(silo'd to one tile), and duplicates (grain * N), same data across all tiles.**"
*And earlier, user #56, 2026-08-28T03:49:08Z, counting them himself:* "Do we have 2
different modes or 3? **We have regular pages, we have pages that represent a ROW in a
channel, and we have congruent pages which own a slice on each channel
simultaneously.** Right?"

**The fourth box below is this document's own split of the user's single "regular"
class, and it is drawn on the model's authority, not the user's.** The user's "regular
pages (striped across memory tiles)" covers two *hardware* behaviours that the mode bit
already distinguishes and that E.1's routing rule reads differently:
**STANDARD** (mode bit 0, 4 KiB, blocks interleaved across every channel at 64 B) and
**REGULAR** (mode bit 1, `G`-sized, striped across tiles at grain granularity)
— the two encodings of DESIGN §5.3 D:625-645, with §5.4 D:647-659 fixing that NMFC data
takes `G`-sized pages and everything else keeps 4 KiB, and
`inc/nmfc/nmfc_trace.h:117-126` — the doc comment above the `region` enum — stating the same split in the enum's own words (the declaration itself is `:127-145`; **the two citations to this enum previously disagreed and are reconciled below**). They are
one *class* in the user's taxonomy and two *encodings* in the machine. **If you are
counting page types for a design document, the answer the user gave is three. If you are
implementing the mode bit, the answer is four boxes. And if you are reading the trace
format, the answer is a THIRD three — `STANDARD`, `NMFC`, `CODE` — which is not the user's
three: it splits his "regular" as this diagram does, and it has no value for his "grains"
at all.** The full four-box → three-value mapping is tabulated below the diagram. Nothing
here contradicts #269; the split is a finer cut of its first class.

**Their sizes are not free parameters** — they follow from the machine's memory
configuration. *That is the user's own statement, #269, and it should be cited to him
and not to a model-authored note:* "**All of these pages are different sizes, and the
size necessarily changes with the machine's memory configuration itself.** Note that the
address mapping is clever here: **duplicate and grain pages are sized specifically so
that they can control their internal layout, such that they can grab physical frames all
on the same tile.**" (Corroborated at DESIGN §5.0.1 D:516-519 and by the memory note
`nmfc-translation-design.md:16-19`, both tier 3.)

```mermaid
flowchart TB
  ALLOC["allocation — the OS, at translation time<br/>guided by the compiler's page TYPE and vtile LABEL"]

  ALLOC --> STD["STANDARD<br/>mode bit 0, 4 KiB pages<br/>blocks interleaved across EVERY channel<br/>at 64 B granularity"]
  ALLOC --> REG["REGULAR<br/>mode bit 1, G-sized<br/>grain g lands on tile g mod N<br/>striped across tiles at GRAIN granularity"]
  ALLOC --> GRN["GRAIN<br/>mode bit 1, G-sized<br/>SILO'D: the whole grain on ONE tile,<br/>the tile the vtile asked for"]
  ALLOC --> DUP["DUPLICATE<br/>mode bit 1, each copy is G-sized<br/>ONE VIRTUAL PAGE, N PHYSICAL COPIES<br/>ONE PER TILE, identical<br/>the replica set is an aligned N-run of grains<br/>READ-ONLY BY CONSTRUCTION"]

  STD --> STDU["for: small hot structures every core reads,<br/>e.g. a level frontier.<br/>WHY: under NMFC mode a 1 MiB frontier is ONE grain,<br/>lands entirely on one tile, and every invocation<br/>that reads it has to migrate there."]
  REG --> REGU["for: ordinary NMFC data with no co-location need"]
  GRN --> GRNU["for: data wanted spatially local to a function core"]
  DUP --> DUPU["for: what EVERY core needs —<br/>function instruction pages,<br/>THE PAGE TABLE,<br/>read-only data.<br/>Occupies M = N x G bytes of physical memory,<br/>only M/N = G of it is writeable."]
```

[CORRECTED — the STANDARD box previously ended "**— measured deadlock**", which no
source says. Its tier-2 source, `tools/nmfc/annotate.cc:84-90`, says: "*NMFC mode is
grain-granular, so a structure smaller than grain × tiles cannot span the machine — **a
1 MiB frontier on a 1 MiB grain lands entirely on one tile, and every invocation that
reads it has to migrate there.** That is what STANDARD mode is for: its blocks interleave
across every channel, so a small hot shared structure is spread instead of siloed. Which
mode a structure wants is a property of how it is used, so the program says.*" **A forced
migration on every read is what is measured; a deadlock is not.** The two checked-in
`standard` regions are `front` and `curr`, both 1 MiB
(`tools/nmfc/kernels/regions.txt`). The deadlock in this document's record is a different
event — the migration path holding a tile slot while waiting for fabric space, at cycle
9,100,426 (I.1, DESIGN §23.1 D:1997-2002) — and conflating them overstates the case for a
mechanism that does not need overstating. E.2a has the measurement that does justify it,
64 : 1.]

**"DUPLICATE = grain × N" means the REPLICA SET is N grains, NOT that the page is
`N × G` bytes.** One virtual page of size **`G`**; `N` physical copies of size `G` each;
total physical footprint `M = N × G`, of which only `M/N = G` is writeable. The
construction is in F.9: **a replica set is an aligned N-run of grains, so copy *t* is
`base + t` and lands on tile *t* by construction, with no per-tile table.** Reading
"grain × N" as a `N × G`-byte page allocates N× too much and breaks the `base + t`
construction outright.

**REGULAR and GRAIN differ in the ALLOCATOR, not in the hardware — and that distinction
must not be implemented as a second mode bit or a per-page silo flag, because neither
exists.** Both are mode bit 1, both are `G`-sized, and E.1's routing rule reads only the
mode bit and the physical address: `tile = (pa >> log2(G)) mod N`. A *single* `G`-sized
page is therefore on exactly one tile under either label. The difference appears only
across a **multi-grain allocation**: REGULAR means the allocator hands out consecutive
grains, so grain *g* lands on tile *g mod N* and the object is striped across tiles;
GRAIN means the allocator picks frames whose grain indices are all congruent to the
vtile's home, so the whole object sits on one tile. **The hardware cannot tell them
apart and does not need to.** They are drawn as separate boxes because the *compiler*
selects between them (F.2's page-TYPE lever), not because the machine has two modes.

`[AND NOTHING UPSTREAM CAN TELL THEM APART EITHER — THE GRAIN PAGE TYPE IS NOT
EXPRESSIBLE ANYWHERE IN THE SHIPPED TOOLCHAIN. Previously unrecorded. The sentence above
says the *hardware* cannot distinguish REGULAR from GRAIN, which is fine because the
compiler selects; this note says **the compiler cannot select**, which is not.]`

**`grep -rn vtile src/nmfc inc/nmfc tools/nmfc config/nmfc` returns ZERO HITS.** The
`vtile` label the box above depends on — and that F.2 lists as a compiler lever — exists
in this document and in DESIGN, and in no source file, header, tool or config.

**The manifest cannot say it.** `annotate.cc:83-104` parses a region line as
`<name> <base> <bytes> [standard|nmfc]` and **kills the run on any other third token**
(`:100`: "*region … has unknown mapping mode '…' (expected nmfc or standard)*"). **There
is no tile column and no vtile column.** Both checked-in manifests carry only that form
— `tools/nmfc/kernels/regions.txt` (7 regions; `front` and `curr` marked `standard`, the
rest defaulting to `nmfc`) and the stale root `regions.txt`.

**The hint cannot carry it either, because its tile is COMPUTED, never DECLARED.**
`annotate.cc:370`, for every page of every region without exception:

```cpp
const std::uint32_t tile = static_cast<std::uint32_t>((a >> opt.grain_bits) % opt.tiles);
```

**That is `g mod N` — the REGULAR/striped rule, by definition.** A siloed region would
need every one of its pages to carry the *same* tile; this line guarantees they carry
consecutive ones. And `place_regions` (`:111-141`) exists to *preserve* that striping
across the real→simulated rebase.

**The region enum has three values, not four.** `inc/nmfc/nmfc_trace.h:127-145`:
`STANDARD = 0`, `NMFC = 1`, `CODE = 2`.

**THE MAPPING FROM C.3's FOUR BOXES TO THE ENUM'S THREE VALUES, ALL FOUR OF THEM, so it
does not have to be inferred:**

| C.3's box | trace `region` value | how the enum's own comment identifies it |
|---|---|---|
| **STD** | `STANDARD = 0` | "*keeps the classic layout (channel bits just above the block offset) so streaming traffic spreads across every channel*" |
| **REG** | `NMFC = 1` | "*lifts the channel bits above the page offset … so a whole page lives on one tile … while the page's own blocks still spread across banks*" — **`NMFC` is REGULAR** |
| **GRN** | **no value — it does not exist in the enum** | **There is no GRAIN.** The mode bit cannot express siloing, `annotate.cc:370` computes every hint's tile as `g mod N`, and no manifest column can declare one (above) |
| **DUP** | **`CODE = 2`** | "*NMFC mode, and **replicated on every channel**. One virtual address, N physical pages — one per channel. Translating it is where the tile gets chosen … only sound because the pages are read-only*" — that is C.3's DUPLICATE box exactly |

[CORRECTED — this passage said "there is no GRAIN" and identified `STANDARD` and `NMFC`,
and then never said what `CODE` was, leaving one of the four boxes unmapped. A reader had
to infer `DUPLICATE = CODE` from an aside four paragraphs later ("`CODE` replication is
the one exception: it is siloing-by-replication and it does work"). The enum's own
docstring says it outright and is quoted in the table above.]

[CORRECTED, SECOND HALF — **the two citations to the same enum did not agree.** C.3's
preamble cited `inc/nmfc/nmfc_trace.h:118-133` for "the same split"; this line cited
`:127-145`. **They are the same enum**: `:117-126` is the doc comment that describes the
STANDARD/NMFC split in prose, and `:127-145` is the `enum class region` declaration
itself, whose `CODE` member carries its own 15-line comment. Cite `:117-145` for both, or
cite the comment and the declaration separately and say which is which — the preamble now
does the latter. **The deeper point is unaffected and worth restating: the split C.3 draws
(FOUR boxes) is not the split the enum encodes (THREE values), and the box with no
encoding is GRAIN — the one the whole siloing claim rests on.**]

**And `PAGE_HINT` advertises the mechanism it never emits.** `nmfc_trace.h:74-81`:
"*Virtual page `aux0` … should be backed by physical memory owned by tile `tile` … **This
is how the pseudo-compiler silos a data structure -- or a function's code -- onto one
memory tile.***" **Its only producer never emits a siloed tile.**

**CONSEQUENCE, and it bounds a large part of Parts G, K and N: every result in this tree
was measured on a REGULAR/striped layout.** The siloing property — the thing the grain
formula exists to *license* (E.3: "*the tagging granularity, the siloing granularity, and
the NMFC-data page size*" are one number) — **has never been exercised.** Any claim of the
form "co-location did not help" or "the policy had nothing to move" must say this, because
**the layout it would have moved data toward cannot be requested.** `CODE` replication is
the one exception: it is siloing-by-replication and it does work (`nmfc_producer.cc:269`).

**To build it** the change is small and in three places: a third manifest token
(`silo <tile>` or a `vtile` column), a `region::GRAIN`, and a branch at `annotate.cc:370`
that emits the declared tile instead of `(a >> grain_bits) % tiles`. **The receiving side
already works** — `nmfc_vmem.cc:555-559` honours `hint.tile` verbatim under
`TRANSLATE_FIRST` (A.4a). Ledger **L41**.

**Duplicates, precisely** (user #271, 2026-09-01T20:18:16Z, verbatim): "Duplicates are
not independently-writeable in user-space, that is the whole point. **They appear as
perfect duplicates. Kernels also have their writes duplicated. Page tables are
read-only. Although a duplicated page takes up M space, only M/N is write-able.**"
Read-only *by construction* is not the same as never written: a program **builds**
one and then stops writing it, and the "kernel writes are duplicated as well" clause
is what makes that possible — the MMU fans a write to a duplicate page out to every
copy, or the other N−1 tiles compute on garbage (DESIGN §29.3 D:3390-3395).

**Why duplication is sound at all:** N *writable* copies would need a coherence
protocol; N copies of read-only code need nothing. That is exactly why function
bodies can be aliased this way **and the data they chase cannot**
(`inc/nmfc/nmfc_trace.h:139-145`).

### C.4 An invocation's lifecycle

```mermaid
sequenceDiagram
  autonumber
  participant H as Host core
  participant K as Kernel handler on the host
  participant F as FTU on the host
  participant X as The fabric
  participant A as Tile A function core
  participant SA as Tile A slice + channel
  participant B as Tile B function core

  H->>F: FORK rH, rPC, ctx512 — allocate an entry, get a handle
  Note over F: refuses rather than evicts,<br/>FORK returns 0 when full.<br/>FORKQ probes how many are free.
  F->>X: invocation packet — token, origin, home host,<br/>entry PC already translated to tile A's copy
  X->>A: deliver into a free context slot<br/>refuse if none free — that refusal is the back-pressure
  Note over A: context = PC + 512-bit regfile.<br/>The 512 bits are the context's WHOLE capacity,<br/>how many are LIVE is the function's business (K.6).
  A->>SA: fetch — local, never crosses the fabric
  A->>SA: load — context SLEEPS, wakes on the value
  A->>A: address belongs to tile B
  Note over A,B: MIGRATION.<br/>72 B = 512-bit regfile + 8-byte PC.<br/>It REPLACES the 64-byte line a foreign<br/>access would have fetched — never both.<br/>Slot released AT DEPARTURE, before the<br/>fabric is even asked. Translations dropped,<br/>the PC is unchanged, because code is duplicated.
  A->>X: migration packet, 72 B
  X->>B: arrive after the fabric hop (8 cycles configured)<br/>take a slot, resume<br/>+2.2-2.3 cycles TRANSLATION cold start<br/>(a separate quantity, NOT the hop — see F.7)
  alt returns a value
    B->>X: END with the return bit SET — completion carrying the whole 512-bit regfile
    X->>F: park the 64 bytes in the FTU entry
    H->>F: JOIN rOK, ctxDST, rH — a TRY: deposits 512 bits, or reports failure
    Note over F: entry frees only at JOIN.<br/>A join-expected entry NEVER closes<br/>without returning its values.
  else fire-and-forget
    B->>X: END with the return bit CLEAR — ACK only, no register file
    X->>F: ACK closes the entry
    Note over F: a FORKF entry closes on its ACK,<br/>its return can never be read.
  else extends instead of returning
    B->>B: CONT rPC — successor.<br/>Inherits the SAME FTU entry.<br/>Allocates nothing, CANNOT be refused.<br/>ONE becomes ONE. Never fans out.
  else takes a RECOVERABLE fault — a page fault, say
    B->>X: fault, carrying the FTU handle
    X->>F: the FTU is the delivery path
    F->>K: TRAP on the host, exactly as a normal system
    Note over B: the context does not spin and does not die.<br/>It parks. Its slot is held, because it is<br/>going to resume in place.
    K->>K: kernel handler runs — fix the mapping
    K->>F: RESUME rH — privileged, names the context by its handle
    F->>X: resume packet
    X->>B: the context resumes at the faulting instruction
  else takes a FATAL fault — div0, a bad access
    B->>X: fatal fault
    X->>F: propagate to the host
    F->>K: kill the program and ALL of its contexts
    Note over F: every outstanding FTU entry of that program<br/>closes AT ONCE — register file ZEROED, ERROR FLAG set.<br/>A JOIN on any of them returns immediately with the error.<br/>Nothing waits on the user program (ruling O7).
  end
```

**[CORRECTED — THIS DIAGRAM DID NOT RENDER AT ALL. A MULTI-PARAGRAPH TAG: it runs to the
"END OF THE C.4 RENDERING CORRECTION" marker below.]**
`[SUB-TAG]` In any mermaid ≥10, four unescaped
`;` characters inside `Note` text are **statement separators in mermaid's sequence
lexer**, so the whole block failed to parse and displayed as an error box: all 6
participants, all 13 messages and the entire `alt`/`else`/`else` structure were invisible.
Verified by `mermaid.parse()` on **10.9.1 and 11.x** — `Parse error on line 11: ...her
than evicts;<br/>FORK returns 0 when / Expecting 'NEWLINE', ',', ... got 'INVALID'` — and
verified OK on both after replacing every `;` with `,`. The parse dies at the FIRST one,
so nothing downstream of it rendered. Round 2 recorded all five blocks as "no
parse-breaking construct, CHECKED AND CLEAN"; that was an eyeball check and it was wrong.
C.1, C.2, C.3 and C.5 do parse on both versions.

**Facts that existed ONLY inside those Notes, and were therefore invisible until now** —
they are why this counted as a blocking defect rather than a cosmetic one: "**Slot
released AT DEPARTURE, before the fabric is even asked**" (H.8's rule, and the deadlock
fix behind it) and "**+2.2-2.3 cycles TRANSLATION cold start (a separate quantity, NOT the
hop — see F.7)**", which was itself the round-1 correction.

**RULE FOR EVERY FUTURE EDIT TO THIS DOCUMENT: never type `;` inside mermaid `Note`,
label or message text. Use `,` or `&#59;`.** And do not certify a diagram by reading it —
extract the block and run `mermaid.parse()` on it.

`[AND THE RULE WAS IMMEDIATELY VIOLATED BY C.3, WHICH THE SAME REVISION CERTIFIED AS
PARSING. Fixed here.]` `sed -n '/```mermaid/,/```/p'` over all five blocks found **one**
remaining `;`, in C.3's `DUPU` node label — "*Occupies M = N x G bytes of physical
memory**;** only M/N = G of it is writeable*". It is now a comma. **Both statements in the
earlier certification cannot have been true at once**: either the rule is over-broad
(a `;` in a `flowchart` *node label* is not a statement separator the way it is in
`sequenceDiagram`, so C.3 did parse) or C.3 was broken. **The rule is kept as written —
document-wide and unconditional — because it costs nothing and the narrow version has
already been got wrong once.** The mechanical check is:

```
python3 - <<'CHECK'
FENCE = chr(96) * 3                 # three backticks, written this way so the
blocks, inb = [], False             # snippet can itself live in a fenced block
for i, l in enumerate(open('CANON.md'), 1):
    if l.strip().startswith(FENCE + 'mermaid'):
        inb, start, body = True, i, []
    elif inb and l.strip() == FENCE:
        inb = False; blocks.append((start, i, body))
    elif inb:
        body.append((i, l))
for s, e, body in blocks:
    for n, l in body:
        if ';' in l:         print('SEMI', n, l.strip()[:80])
        if l.count('"') % 2: print('ODDQUOTE', n, l.strip()[:80])
        for o, c in (('[', ']'), ('{', '}'), ('(', ')')):
            if l.count(o) != l.count(c):
                print('UNBALANCED', o, n, l.strip()[:80])
CHECK
```

**Run it, and `mermaid.parse()`, before certifying any block again.** As of this
revision all five blocks pass the check above: no `;`, balanced quotes, balanced
brackets, and `subgraph`/`end` balanced in the four flowcharts.

**[END OF THE C.4 RENDERING CORRECTION.]** `[DELIMITERS REPAIRED — this correction was
delimited by a single backtick opened at "CORRECTED — THIS DIAGRAM" and closed 55 lines
later at "…four flowcharts.]`" — but **a Markdown code span cannot cross a blank line**,
so the opening backtick closed at the next `` ` `` in the same paragraph and the tag
rendered as an interleaving of code and prose across four paragraphs, with three of them
carrying an odd number of backtick runs. **The three worst-affected were the three that
prescribe how to check mermaid** — the rule against `;`, the check script's introduction,
and the "run `mermaid.parse()` before certifying" instruction — i.e. the correction about
unreadable diagrams was itself unreadable. It is now bounded by bold markers, per the
convention added to the BRACKET TAGS table.]`

**THE FAULT PATH IS NEW IN THIS REVISION, AND IT IS TIER 1.** *User ruling 2026-09-02
R20, verbatim:* "**faults should go to the kernel handler, execute, then resume the work
just like a normal system. On fault, it is probably necessary to pass the fault to the core
via the FUT and have it handle it, then send a 'resume' instruction with the context handle
to resume it.**" And, on the instruction's privilege: "**RESUME needs an extra instruction
(privileged???? this is real question, not sure if it needs to be or not).**" The path
drawn above is: **tile faults → delivered to the host through the FTU as a trap → kernel
handler runs → `RESUME handle` → the context resumes.** `RESUME` is recorded as a
thirteenth instruction, **privileged** [RULED — user ruling 2026-09-03 O16, verbatim:
"**Yes, privileged.**"] — see **I.6** and **I.3**.

**On "all 512 bits are live on entry", which an earlier revision of this diagram
asserted.** The 512 bits are the context's *capacity*, transmitted whole because 64
bytes is the natural transmit unit (I2). **How many of them are live is a property of
the function, and it is exactly what K.6's admission test measures** — peak simultaneous
liveness in bits. Reading the lifecycle diagram as "every context is always at 512/512"
makes that test vacuous, which is precisely the state SST's implementation is in
(divergence S5: "the bits-used figure is always 512" because nothing produces a
non-default layout).

**The two invocation loops, both official, both kept** (user #130,
2026-08-29T03:56:01Z; user #135, 2026-08-29T05:04:43Z; tier-3 counterpart **DESIGN §4.3
D:369-415**, which adds the reason the second one is hard for a TRACE rather than for the
machine: "*in a traced program the call was synchronous, so the block was always already
written and the spin executed zero times ... a memory-committing loop replayed naively
lets the host read results it never waited for, and the optimism is invisible in every
number it produces*"):
- **register-returning** — the result comes home in the register file and `JOIN`
  deposits it. Concurrency is limited by how many results the caller holds un-joined.
- **memory-committing** — the value is not returned; it is **saved as a block in
  memory** and the standard core polls that block. Ownership is **by address**, and
  self-checking must ensure **never a double commit, never a double block** (user #131 and
  #132, 2026-08-29T03:58-03:59).

  `[THE "COMMIT" AND "WAIT" PRIMITIVES WERE A CHAMPSIM ARTEFACT AND ARE STRUCK — user
  ruling 2026-09-02 R14, verbatim: "That was an artifact of the ChampSim design, since
  ChampSim doesn't model coherence, data, or atomics. By necessity, a core must poll said
  block to see if the writeback of the data has occurred (coherence propagated).
  Potentially using atomics."]` **The real mechanism needs no instruction.** The function
  performs an **ordinary store**; **coherence makes that write visible** to the host at the
  L2↔LLC directory (I13, I14); the host **polls the block** with an ordinary load, and uses
  an **atomic** if it needs a read-modify-write on it. The markers `__nmfc_wait` and
  `NMFC_COMMIT` exist only because ChampSim models neither coherence nor data nor atomics
  and a trace cannot reconstruct a spin that executed zero times in the traced run. **They
  are a simulator encoding, not an absence in the ISA.** Full statement in **I.11**; ledger
  **L47**, RULED R14.

### C.5 Coherence

**Every box below is a real structure and every arrow is a real request path.** The
admission order and its three cases are a *legend* and are stated as prose underneath —
they were previously drawn as nodes hanging off the directory, which reads as "the
directory feeds an admission unit which feeds three cases", and there is no such unit.

```mermaid
flowchart TB
  subgraph ABOVE["PRIVATE caches — the HOST's sit above the coherence point"]
    HL1["host L1I / L1D"] --> HL2["host L2"]
  end

  subgraph TILE["INSIDE A TILE — already on the slice's side of the boundary"]
    FCC["function core I$ / D$<br/>private to its tile"]
  end

  DIR{{"THE COHERENCE POINT = the L2-to-LLC boundary AT THE FABRIC<br/>MOESIF directory<br/>M O E S I F<br/>O = dirty+shared owner, F = clean designated forwarder"}}

  HL2 --> DIR
  DIR --> SLICE["LLC slice — one home per line, BY ADDRESS.<br/>Host and function-core traffic converge HERE, in address space."]
  FCC -.->|"never crosses the boundary:<br/>same slice, no second copy,<br/>NO fabric crossing"| SLICE
  SLICE --> MEM["memory controller and channel"]
```

**Where the coherence point is, and why the two phrasings are not two answers.** The
**directory sits at the L2↔LLC boundary, in the fabric.** The **slice is where host and
function-core references converge on the same line.** The function core's caches are
private, like a host L1 — but they sit on the *slice's* side of the boundary, so a
function-core reference **does not have to be ordered by the directory to reach its own
slice**. That is why "the fabric is the coherence point" (I13) and "they converge in the
slice" (C.1) describe the same machine: adjacent structures, different jobs. **A design
that puts the MOESIF directory between the function core and its own slice has moved the
core off the tile (R51).**

`[BYPASS IS ABOUT ORDERING, NEVER ABOUT TRACKING — read this before building the
directory.]` **The function core's local path skips the directory's *arbitration*; it does
NOT skip the directory's *bookkeeping*.** When a tile's private I$/D$ or its held-word
(atomic) table retains a copy of a line, **the slice records that the tile holds it, in the
same entry and the same bit vector that names host sharers**, so that a later host
reference can be snooped against it. **A directory that tracks host caches only is the
exact defect H.7 records** — the host wrote `frontier_count = 0` between BFS levels, no
tile-side copy was named, no snoop was sent, and **every level expanded roughly three
times over while every vertex still came out correct.** H.7 requirement 1 ("*the tile is
told when a line is snooped away*") and requirement 2 ("*the directory then no longer
believes this agent holds anything, and the snoop never comes*") are **unsatisfiable
unless the tile is a tracked agent.** Bypass for order; track always.

**WHAT THIS SECTION SPECIFIES AND WHAT IS CONFIGURATION. NOTHING IN IT IS STILL OPEN.**
Everything above is **locative** — where the directory is and what its state set is — and
that is the design part: **M O E S I F**, at the **L2↔LLC boundary in the fabric**, with
**NMFC cores taking strict priority as an ORDER, not a tie-break**. Two further things are
design and are ruled below: the **sharer encoding and inclusivity** (user ruling 2026-09-03
**O9**) and the fabric's **message classes and arbitration** (**O5**, at C.5a). Only the
**sizes** are configuration.

`[SIZING IS CONFIGURATION — user ruling 2026-09-02 R19, verbatim: "size it according to
modern systems. **Make sure it is not a bottleneck.** Look at other modern systems."]` The
entry count is therefore not a design constant and does not belong in this document; **see
SELECTED CONFIGURATION**. The **constraint** that binds is R19's second sentence: the
directory must be sized so that it **is not the bottleneck** — which, combined with strict
priority being an order, means it must always be able to name the current owner of a line
the tile is working on. **A directory that has to evict an entry in order to admit one
cannot implement a strict order**, so "not a bottleneck" is a correctness requirement here,
not a performance preference.

**THE SHARER ENCODING AND THE INCLUSIVITY PROPERTY ARE RULED — BY CRITERION.**
`[RULED — user ruling 2026-09-03 **O9**, verbatim: "**Whichever can scale best. the maximal
targeted system is a substantially beefy multi-core system with up to 32 memory tiles. We
should expect a LOT of traffic.**" The user ruled the *criterion* and gave the scale it must
be met at; the choice that criterion forces is drawn below and tagged **[derived from ruling
O9]**, with its argument and its prior art, so it is never mistaken for the user's own
words.]`

**THE TARGET, NOW A NUMBER RATHER THAN AN ADJECTIVE: up to 32 memory tiles, a substantial
host core count, and heavy traffic.** That is the first time this document has had a
maximum to design the directory against, and it is what decides the question.

**`[derived from ruling O9]` THE CHOICE: an EXACT BIT VECTOR over host cores AND tiles,
INCLUSIVE of the private caches above the fabric, with BACK-INVALIDATE on eviction.**

| property | what it means here |
|---|---|
| **exact bit vector** | one bit per potential sharer — **every host core and every one of the up to 32 tiles**. A tile's bit is set by its own private I$/D$ fills and by its held-word (atomic) pins, exactly as a host core's is set by an L1/L2 fill. At 32 tiles plus 32 host cores that is **64 bits, 8 bytes, per tracked line**, alongside a 3-bit MOESIF state. |
| **inclusive** | **inclusive of EVERY private cache the fabric's directory serves — host-side AND tile-side.** Every line held in a host L1/L2, in a function core's I$ or D$, or in a tile's held-word table has a directory entry. The directory therefore **always knows every copy, on both sides of the boundary.** `[This clause is load-bearing: scoping inclusivity to host caches alone rebuilds H.7's host↔tile coherence hole verbatim.]` |
| **back-invalidate on eviction** | evicting a directory entry invalidates the copies it names — **including tile-side copies, which is the snoop H.7 requirement 1 delivers up to the function core so it can hand back a held word** — so "no entry" always means "no copy". A pinned tile-side line is a **request, not a right** (H.7 requirement 3): the directory may ask for it back, and the tile must yield. |

**Why it wins at 32 tiles under heavy traffic — three arguments, and the third is the one
that makes it a correctness matter rather than a performance one:**

1. **It never broadcasts.** A bit vector names the exact sharers, so an invalidation costs
   *k* messages for *k* sharers. **At 32 tiles plus a beefy host core count, on the one
   fabric that also carries migration and fill (I13, and O5's classes below), a
   broadcasting directory spends the fabric budget the machine exists to conserve.**
   "*Expect a LOT of traffic*" is precisely the regime where broadcast loses.
2. **Its cost is bounded and small at this scale, which is the whole reason the classical
   objection does not apply.** The standard argument against full bit vectors is that they
   are `O(cores)` per line and so do not scale — **the SGI Origin 2000 and the Stanford
   DASH prototype both moved to limited pointers and coarse vectors for exactly that
   reason, at machine sizes in the hundreds of nodes.** At **64 sharers** a full vector is
   **8 bytes on a 64-byte line: 12.5% overhead**, which is the regime every commercial
   multicore directory of the last two decades has stayed in. **The ruled maximum is inside
   the range where the exact answer is affordable**, and that is the fact that decides it.
3. **Inclusivity plus back-invalidate is what makes I14's strict order ENFORCEABLE.** I14
   makes NMFC priority an **order, not a tie-break**, which means the directory must be able
   to **name the current owner of a line a tile is working on, always.** A **non-inclusive**
   directory cannot promise that — it may have no entry for a line that is nonetheless
   cached — so it cannot implement a strict order at all. And R19's "*make sure it is not a
   bottleneck*" is, combined with the order, **a correctness requirement**: a directory that
   must evict an entry in order to admit one has an unenforceable order (stated above).
   **Back-invalidate is the mechanism that makes "never evicts to admit" true rather than
   hoped for.**

**The alternative, and exactly why it loses.** **Limited pointers with coarse-vector
overflow, non-inclusive, no back-invalidate** is smaller per entry — a handful of pointers
instead of 64 bits. It loses twice. **(a)** An overflowed line **degrades to broadcast**,
and the lines that overflow are the widely-shared ones, which under "*a LOT of traffic*"
are the hot ones — so the degradation lands exactly where it hurts most. **(b)** A
non-inclusive directory **cannot promise it can name the current owner**, which is what
I14's order needs, so the strict-priority property this whole section is built on becomes
unimplementable. **The per-entry saving is real and it buys a machine that cannot do the
one thing the design requires of it.**

**What this does NOT settle: the entry COUNT.** That is R19's, it is configuration, and it
is in **SELECTED CONFIGURATION**. O9 rules the encoding and the inclusivity property; R19
rules the size.

**And the other question that used to hang here** — `hostPaysSnoop` 9 against
`nmfcPaysSnoop` 24 — is **no longer a directory question**: it is ruled a **suspected bug in
the simulated environment** (user ruling 2026-09-02 R5; see I14 and ledger L14/L15).

**[RESOLVED 2026-09-03 — and the evidence is still NOT in the permitted direction.]** The
suspected bug was real and is fixed in SST commit
**`82357d0e7f92ac437ac45fce230efcde258139d4`**, with follow-up
`5e5fa669ce5041d0a366190f6cef61ebbdd7b400`: the program image is now placed by the host
MMU on the untimed init path instead of being executed store-by-store through the host's
caches by `RevLoader`. **The loader was not the explanation of the ratio.** It moved
`nmfcPaysSnoop` on `tile_bfs`/4 only 13 627 → 13 147 (−3.5 %) against `hostPaysSnoop`
270 → 273, i.e. 50.5× → 48.2×; and once `5e5fa66` stopped charging a clean `E` holder as a
dirty one, the same run reads `hostPaysSnoop` **62** against `nmfcPaysSnoop` **12 910** —
**208:1, worse.** So the caution stands and is now a measured statement rather than a
suspicion: **do not quote the snoop-direction table as evidence for or against I14.**
On the evidence the dominant term is the workload (a host producing into `.bss` and
function cores consuming it), and the second is a directory that sets `d.global = O` and
**never clears `d.owner`** (`NMFCCoherenceFabric.cc:603-609`), so the host L2 remains owner
of record for the rest of the run — 8 500 of 13 627 events find the entry already in `O`
and 64 % are repeat snoops of a line already snooped once. **That second term is a protocol
decision for this section, and it is open**; classic MOESI does keep the owner on a read,
so moving ownership to the reader is architecture and was not changed under R5. Ledger
**L14**/**L15** carry the full before/after.

---

#### C.5a THE FABRIC'S MESSAGE CLASSES AND THEIR ARBITRATION — RULED

`[RULED — user ruling 2026-09-03 **O5**: "**a.**", selecting option (a) of that row. This
is the arbitration rule invariant 11's byte-parity claim depends on, and it had never been
stated anywhere in this document.]`

**There is ONE fabric (invariant 13). It carries THREE message classes, in
per-destination queues (H.8):**

| class | what rides it |
|---|---|
| **COHERENCE** | directory requests and responses — invalidations, ownership transfers, snoop responses |
| **MIGRATION** | a context leaving one tile for another: **72 B** = 512-bit register file + 8-byte PC (invariant 11, J.1) |
| **FILL** | LLC/DRAM line traffic — **64 B** per line |

**THE ARBITRATION RULE, AND EACH HALF OF IT IS LOAD-BEARING:**

> **1. COHERENCE IS STRICTLY FIRST.** Not weighted, not a tie-break — **first**. **I14
> makes NMFC priority an ORDER**, and an order cannot be implemented if a coherence
> response the order depends on can sit behind a line fill. A directory that must wait on
> a fill queue to learn who owns a line has no order.
>
> **2. MIGRATION AND FILL ARE ARBITRATED AT EQUAL WEIGHT** — round-robin between the two
> classes at each destination queue. **This is the precondition of invariant 11's parity
> claim**, and it is the reason option (b) was refused: if MIGRATION went strictly ahead of
> FILL, a 72-byte migration and the 64-byte fill it replaces would no longer be priced the
> same by the fabric, and **the claim "they are alternatives, never both, and cost the same
> order" would become inexpressible** rather than merely unmeasured. See **J.2**, which
> states the parity claim and now names this rule as what it rests on.

**Three consequences, so this is not re-derived as a scheduling detail:**
- **The classes are DESIGN; the rates are CONFIGURATION.** How many messages of each class
  a destination accepts per cycle, and how deep its queues are, is R6–R10 configuration —
  **see SELECTED CONFIGURATION.** *That there are three classes, that coherence is first,
  and that the other two are equal* is not.
- **Per-destination queues, never one shared queue.** That is H.8's measured rule (128 of
  128 entries bound for one full tile), and it applies **per class per destination**: three
  classes × destinations, so one congested destination cannot own another destination's
  budget in any class.
- **A second network for NMFC traffic is still forbidden** (I13, J.2). Three classes on
  one fabric is not three fabrics; the classes share links, share the byte budget, and are
  the reason subsumption is measurable at all.

> **ADMISSION ORDER — NMFC CORES TAKE STRICT PRIORITY.**
> A strict order, **NOT** a tie-break. **MESI cannot express it.**
>
> | case | who pays |
> |---|---|
> | invocation vs invocation **on one core** | **nothing.** One slice, one copy, nothing to invalidate. |
> | a **HOST** reference to a block an NMFC core is modifying | **the host pays.** This is the permitted direction. |
> | an **NMFC** core touching a block a **host** modified | **avoided wherever possible.** An NMFC core does not pay for host activity on its own lines. |

**Which direction pays, and why.** The function core is the ordering point for its
address range **by construction** — every invocation touching that range has migrated
there — so it can serialise **without a protocol**. Making it yield to a remote
requester would replace a free ordering point with an expensive one.

**Where memHierarchy may be used.** Below the coherence point and nowhere above it.
Not preference: it implements MESI. `O` is in its state enum and no coherence manager
ever assigns it; there is no `F` at all. Those are the two states that decide what
host/NMFC sharing costs (DESIGN §5.9 D:765-771).

---

## PART D — MEMORY HIERARCHY AND SIZES

### D.1 The shape

**THE IDENTITY, STATED ONCE BECAUSE THE WHOLE DOCUMENT RESTS ON IT AND IT WAS NEVER
WRITTEN DOWN:**

> **tile = channel = LLC slice = memory controller = ramulator2 instance = function
> core.** One object, six names, counted by the same `N`.

That is why "one copy per channel" (A.4, I3) and "one copy per tile" (C.3) are the same
sentence; why "every channel walks locally" (**invariant 3's heading** — [CORRECTED —
this read "I2's heading", which is the register-file invariant, "512 bits in, the same
512 bits out", and has nothing to do with page walks. A reader following it found the
register file, nothing about walks, and discounted the identity — in the paragraph headed
"THE IDENTITY ... THE WHOLE DOCUMENT RESTS ON IT". **This is the second live instance of
the bare-`I<n>` collision the front matter says has already gone wrong once**; the first,
J.4's "Full table in I5", was fixed in the previous revision. The same sentence cites
invariant 3 correctly ten words earlier, which is how the slip survived review. Per the
front matter: **write "invariant 3" or "I.3", never a bare `I3`, in prose that does not
already fix the Part.**]) is satisfied by a per-tile table copy; and why `N` is a single
number throughout. *User #55, 2026-08-28T03:44:51Z,
in his own capitalisation:* "**We partition by CHANNEL** ... **EACH MEMORY SLICE OWNS ONE
RAMULATOR2 INSTANCE.**" *User #261, 2026-09-01T19:09:33Z:* "Tiles are address-partitioned.
**Inside each tile is the nmfc core + caches, LLC cache, memory controller, and memory
channel.**"

**TWO kinds of tile exist in this design, and only ONE of them is a memory tile.** This is
tier 1's own framing and its own vocabulary. *User #1, 2026-08-27T05:17:16Z, verbatim:*
"**Two distinct tiles exist in this design. Traditional compute tiles (high performance
OOO cores with private caches) and Memory Tiles.** Compute tiles are duplicated per
traditional core, as modern processors do. A fabric interconnect routes those compute
tiles to the memory tiles via a programmable fabric (address-based)." The same message
uses the term four more times ("*Function cores serve functions from any number of the
compute tiles*"; "*Standard compute tiles manage the lifecycle of these functions*"; "*a
standard compute tile could have 10s or 100s of functions in-flight*"; "*We need modified
compute tiles, memory tiles, and the interconnect fabric*"), and **nothing later in the
log retires it** — #261 and #284 describe memory tiles without denying compute tiles.

**What the identity actually asserts, and it is the whole of it:**
- A **compute tile** is a **host core plus its private caches** (L1I, L1D, L2) and its
  fabric port. **No function core, no LLC slice, no memory controller and no DRAM channel
  lives there.**
- A **memory tile** is **the** tile of this design: function core + private banked I$/D$,
  one LLC slice, one memory controller, one DRAM channel — one address-partitioned slice
  of memory, and `N` counts these (#55, #261).
- **The two are never interchangeable, and "tile" unqualified in this document means the
  MEMORY tile.** Where a sentence means the host side, it says "compute tile" or "host
  core" explicitly.
- **Nothing of the memory tile may be moved across the fabric to the compute side** — that
  is the error, and it is what R51 ("putting a tile's own caches on a network **has moved
  the core off the tile no matter what the diagram says**") and R52 ("the nmfc core itself
  does **NOT** live across the fabric from the memory tile. **It is THERE**", #283) forbid.
  **R51/R52 are about WHERE a memory tile's parts sit, not about how many classes of tile
  exist**, so they do not support — and were never evidence for — a one-class claim.

`[CORRECTED — TIER-1 CONTRADICTION, AND IT WAS IN THE PARAGRAPH HEADED "THE WHOLE DOCUMENT
RESTS ON IT". This block previously read "**There is exactly ONE kind of tile — a memory
tile.** The phrase 'compute tile' is **ChampSim's name for the HOST side's fabric port**,
and it does not name a second class of tile. **A reader who concludes there are two kinds
of tile has made error R51/R52.**" Three things were wrong with it. (1) It contradicts #1,
the founding design message, which says in as many words that two distinct tiles exist.
(2) The sourcing was backwards: "compute tile" is the **USER'S** term, used five times in
#1 alone; it is not ChampSim's coinage, and this document already relies on the user's
usage as tier-1 evidence two pages earlier — the host-core row of **SELECTED
CONFIGURATION** quotes #1's "*Compute tiles are duplicated per traditional
core*" for exactly that reason. The document was simultaneously quoting the phrase
as the user's word and declaring it ChampSim's. (3) The cross-reference was false: R51 is
about a tile's own caches on a network and R52 about the function core across the fabric —
a reader who checked the cited rows found nothing about how many classes of tile exist.
**The substantive point survives intact and is stated above**: a compute tile is a host
core plus its private caches, no function core lives there, and nothing of the memory tile
may be moved across the fabric. That point is exactly what #1 says. Only the "exactly ONE
kind of tile" formulation and the sourcing claim were wrong, and both are withdrawn.]`

[COUNT REMOVED — a previous correction to this paragraph asserted that
`grep -in 'compute[- ]tile'` "returns FOUR hits" and enumerated them. It no longer does,
and any such count goes stale on the next edit. **No occurrence count is stated here.**]

**The uses that matter are these.** **D.5's port table** ("4: compute-tile fabric, fc D,
fc I, this tile's MMU") — the memory tile's upper port that faces the compute side; and
**two quotations of ChampSim's `src/nmfc/interleave_fabric.cc:2-8`**, "*One instance sits
below each compute tile's last private cache*", one inside **I13's `[CONFLICT]` block**
and one inside **ledger L25**. Those two are the ones to read carefully, because they sit
in the two `[BLOCKING]` fabric discussions — exactly where a reader is counting how many
kinds of stack hang off the fabric. **In both, ChampSim's phrase means the same thing the
user's does: a HOST CORE's private-cache stack.** The agreement is the point; the earlier
reading treated it as a collision.

Identical to a modern machine down to the L2, then vertical. Per host core: private
L1I, private L1D, private L2. Per memory tile: a function core with private,
**heavily banked** I$ and D$; an LLC slice; a memory controller; a DRAM channel.

**Sizing rules the user stated, in his own words:**
- **LLC slice size.** "We size it up to something reasonable for the memory it backs
  (**modern LLC size / DRAM channel** as our indicator for LLC size per tile)"
  (#76, 2026-08-28T18:43:22Z).
- **Everything else.** "**cache sizing and fabric bandwidth should be of the same
  magnitude as modern processors**" (#288, 2026-09-02T03:50:47Z).
- **Both function-core caches must be banked.** "the bandwidth availability of each
  structure is critical, so **both the shared data and instruction caches must be
  HEAVILY banked. Sliced by address space, which just requires routing based on
  address (thus, a fabric).**" (#250, 2026-09-01T17:38:01Z).
- **Separate I and D, never unified.** "I would prefer separate data and instruction
  caches. **They are fundamentally different working sets.** We expect small
  instruction footprints, high locality, excellent caching efficiency. **Data will be
  exactly the opposite. They will conflict if cached together.**" (#249,
  2026-09-01T17:35:03Z).

### D.2 LLC banking, DERIVED FROM THE DRAM DEVICE — and `--llc-banks` is inert

This is a settled decision the user has asked for four separate times.

[RULED — user ruling 2026-09-02 R3, verbatim: "I was under the impression that was derived
from invoking ramulator now, **the `--llc-banks` should be inert. ChampSim updates stop**
until we deem it a good idea to go back."]

**Two things follow, and they are the whole of this section's status:**
1. **The bank count is DERIVED FROM THE DRAM DEVICE GEOMETRY that ramulator declares — it
   is not a knob and not a design constant.** A cache bank and its DRAM bank must be the
   same partition of the address space, so the slice's banking is whatever the device's
   organisation says it is (the per-rank count; see the build note at the end of this
   section). **`--llc-banks` is INERT**: it survives as a command-line flag
   (`config/nmfc/make_config.py:583`, default 1) and it does not express a design decision.
   Under the geometry ruling the count adapts to an **arbitrary** bank/rank/row/column
   organisation within a 48-bit physical address space (I12); **do not write a number here.**
   The values actually configured are in **SELECTED CONFIGURATION**.
2. **ChampSim is FROZEN.** "ChampSim updates stop until we deem it a good idea to go back."
   Building the banking in ChampSim is not on the table, and neither are the other ChampSim
   work items this document had queued. **Exactly two ChampSim changes were ordered before
   the freeze** — deleting `op::SPAWN` (R1) and switching the default router to
   physical/NUCA (R2). Everything else is frozen. **See Appendix 2 D0 for the complete
   list of what the freeze suspends.**

*User #76, 2026-08-28T18:43:22Z, the decision and its reason:* "we just bank the LLC
within the tile itself. ... The banking (address-routed) of the LLC allows for the
contexts to be less-likely to have a queue back up. It is also the likely way it
would actually be built. **The DRAM queue itself inside ramulator2 is split per DRAM
bank (that is what FRFCFS is). So, if we align the LLC slices to the DRAM banks (they
don't need to number the same, just be aligned) then the queueing to the DRAM itself
becomes very simple, we don't need to gather all the LLC misses into 1 queue and then
split them out again.**"

*User #74, 2026-08-28T07:37:30Z, where the binding must happen:* "If it is memory
backpressure, **we want all the binding to happen at the memory controller, not up
inside the nmfc channel to it's data cache. That is the whole point.** Potentially,
you could bank the data cache for additional concurrency via address slicing.
**Building a monolithic 512-entry queue seems like a bad idea.**"

*User #144, 2026-08-29T05:22:21Z, the implementation direction:* "I would recommend
**a fabric inside the memory tile that routes incoming requests + nmfc traffic into
the LLC banks, routing by destination DRAM bank.** The ramulator2 interface needs
updated to provide a variable number of input ports for each LLC output queue. **Try
with and without the function core's data cache.**"

*User #291 item 1, 2026-09-02T12:37:03Z, as a checklist item:* "LLC is banked, and
banks are aligned with ramulator2's DRAM banks?"

**Build status, as frozen.** ChampSim: the mechanism exists
(`config/nmfc/make_config.py:243-252, 452-472` — a post-pass that replaces each source
channel with an `INTERLEAVE_FABRIC` routing on the DRAM bank field) and **no shipped config
uses it**; the flag that would select it is inert, and the freeze means it stays that way.
SST: built, and it is where the design's form lives — `slices × banks-per-slice` is held
equal to the device's per-rank bank count so a cache bank and its DRAM bank are the same
partition (`test/coherent_memory.py:249-265`). **Take every bank-conflict number from SST
and say so.** Ledger **L5**, RULED R3.

**How many banks: a DERIVATION, not a constant.** DESIGN §16 D:1359-1361 prices one point:
"Concurrency should come from banking rather than from a monolithic structure: **8 banks of
today's 64 entries reaches the point where the memory controller binds**, without a
512-entry CAM that does not exist in silicon." **Read that as the shape of the argument —
banking rather than one monolithic queue, and enough banks that the memory controller is
what binds — not as the number.** The number follows the device (R3) and is
**configuration; see SELECTED CONFIGURATION**. Reproducing only the
"not-a-512-entry-CAM" half of that sentence, as an earlier revision of this document
did, drops the only quantity in it that says what to build.

**And the counter-measurement, which must be carried with the requirement.** DESIGN §19
D:1527 lists **LLC banking among six knobs that were changed and did not matter** on the
hot tile ("read/write queue depth 32 → 256, MSHRs 64 → 512, **LLC banking**, refresh,
the address mapping, and the scheduler ... **Every one of those cost a ten-minute
simulation to disprove**"). **These are not in conflict.** §19 measured banking as a
*bandwidth* knob on a channel that was starved by arrival rate, where nothing downstream
could matter (see N.7). §16's argument is about **queueing structure and buildability** —
where the binding happens (#74: "we want all the binding to happen at the memory
controller"), and whether a 512-entry monolithic queue is a thing that exists. **And under R3 nothing is
built in ChampSim: L5 is suspended (Appendix 2 D0), and this section's own clause 2 above
governs.** What survives the freeze is the *reason*, for whenever it lifts: **it is the
correct structure, and the user asked four times — but it is not to be expected to buy
bandwidth on a workload that is arrival-rate-bound, and it is not a reason to reopen
ChampSim.** [CORRECTED — this paragraph previously closed on the bare imperative "**Build
it**", which is the one sentence an implementer acts on and which contradicted both clause
2 sixty lines above and Appendix 2 D0's frozen-work table. The requirement is **frozen, not
abandoned**; the form of it lives in SST.]

**A build note that must survive:** the slice is banked by the **per-rank** bank count
(32 on the DDR5 device), not the flat per-channel count (64), on purpose — "two flat
banks differing only in rank are the same bank index at the same address position, so
banking the cache by 64 would split on a bit the DRAM does not use to select a bank"
(DESIGN §30.2 D:3435-3452).

### D.3 The function core's data cache — SETTLED, and the record has been read backwards

There is a supersession chain here that has already been resolved the wrong way once.
**Newest wins (user #307).**

| when | user's words | reading |
|---|---|---|
| **#75, 2026-08-28T07:54** (AskUserQuestion answer — eleven hours BEFORE #76, and the first word in the chain) | "**Consider scaling number of banks, the size of each d cache bank (scale latency appropriately), or just dropping the dcache entirely and switch context on any data load.** It may be either removing the cache or scaling it to something closer to an L2 may help. Alternatively, we could even try different block sizes, or organization (**perhaps context-mapped sets, a reserved fully-associative set with 2-4 ways of some size of block**." | the user's own list of the alternatives, given as options to try — **bank count, bank size (with latency scaled), drop it entirely, scale toward an L2, block size, context-mapped sets, a reserved fully-associative set.** Not a ruling; the option set the later rulings pick from. **Banking is in it from the very start.** |
| #76, 2026-08-28T18:43 | "Regarding any smaller local cache, **unsure if it is needed**. Each function has about 64 bytes of scratch space ... **The 11% data cache hitrate indicates no**, the only caveat I would give to that is two-fold: prefetching and replacement." | doubt, with two named caveats |
| #142, 2026-08-29T05:18 | "Didn't we already commit to a whole shape around this? Remember? The whole LLC-banking system? And **the removal of the data caches from the nmfc cores**? Was that dropped? I thought that was our default?" | remove it |
| #146, 2026-08-29T06:04 | "We already knew the dcache was binding, that is not news. **That is the whole reason we decided removing it was the best route -> otherwise we need to bank it as well.**" | remove it — **conditionally**: the alternative named in the same sentence is to bank it |
| #248, 2026-09-01T17:31 | "**A small L1 data cache is fine**, but we should probably just pass it off as the I and D buffers for the contexts." | keep it |
| #249, 2026-09-01T17:35 | "I would prefer **separate data and instruction caches**." | keep both, separate |
| #250, 2026-09-01T17:38 | "**both the shared data and instruction caches must be HEAVILY banked.** Sliced by address space" | keep both, **bank them** — this is the alternative #146 named |
| **#291 item 6, 2026-09-02T12:37 (NEWEST)** | "BTB is used in nmfc cores to queue up fetch in a private per-context slot? Similarly, data is delivered to instructions into a per-context data slot? **Each backed up by an I and D cache respectively? Then backed up by the LLC?**" | **both caches exist, and are the canon** |

**Ruling: the function core has BOTH a private I-cache and a private D-cache, both
heavily banked and address-sliced, backing per-context fetch and data slots, and both
backed by the tile's LLC slice.** The 2026-08-29 "remove the dcache" statements are
**superseded** by the 2026-09-01/09-02 statements, and were in any case conditional
("otherwise we need to bank it as well") on the alternative that was subsequently
chosen. `DESIGN §16 D:1363-1365`'s "dropping the data cache entirely is
contraindicated" reaches the same conclusion by a different route and is therefore not
in conflict. See ledger L6 — this reverses an earlier analysis of the same record.

### D.4 The measured capacity result — where capacity belongs

DESIGN §16 D:1329-1358, on a 548 KB tile against a hundreds-of-megabyte CSR:

| fc D$ configuration | cycles | vs default | D$ hit | port refused | DRAM reads |
|---|---:|---:|---:|---:|---:|
| 32 KB lat2 mshr64 | 2,792,466 | — | 11.0% | 82.3% | 596,951 |
| 32 KB lat2 mshr512 rq512 | 2,606,914 | −6.6% | 13.6% | 26.5% | 595,164 |
| 128 KB lat4 | 2,700,520 | −3.3% | 46.6% | 81.1% | 597,697 |
| 512 KB lat8 | 2,696,857 | −3.4% | 52.0% | 81.2% | 589,569 |
| 2 MB lat14 | 2,085,151 | −25.3% | 63.7% | 79.7% | 451,375 |
| 2 MB lat14 mshr512 rq512 | 2,024,762 | −27.5% | 63.6% | 25.8% | 450,006 |
| 8 MB lat18 | 1,770,470 | −36.6% | 71.0% | 78.5% | 361,203 |

Three findings, all standing:
1. **Capacity and concurrency are substitutes, not complements.** Widening the queues
   is worth 6.6% at 32 KB and only 2.2% at 2 MB.
2. **The capacity curve is sharply non-linear.** 128 KB and 512 KB quadruple the hit
   rate and buy 3%, because those hits were coming from the LLC anyway — DRAM traffic
   does not move. Only past 2 MB does the tile start capturing what was going to DRAM.
3. **The level does not matter; the total does.** 8 MB at the L1 and 8 MB in the slice
   land within 1.5% of each other (1,770,470 vs 1,744,780). **Placing it at the L1
   reduces the LLC slice to a 0.2% hit rate — 512 KB of dead silicon. Capacity belongs
   in the LLC slice**, which is also where it is physically defensible: a memory-side
   cache per channel, the role Infinity Cache and HBM-as-cache already play.

So the tile wants **a large LLC slice, a small banked data cache, and enough
outstanding-miss concurrency to keep the channel fed — in that order**, with the
concurrency coming from **banking** rather than a monolithic 512-entry CAM that does
not exist in silicon.

**Invalidation clause, and it is binding on old numbers.** Every result taken at
548 KB of tile capacity and 64-entry admission was **capacity-starved and
admission-starved at once**. Comparisons of placement policy, address mapping or
decomposition shape made in that regime were **measuring the hierarchy, not the
policy**, and need re-taking (DESIGN §16 D:1367-1372).

### D.4a WHAT ACTUALLY SETS THE RUN TIME: MEMORY ADMISSION — the §15 diagnosis, and a STRONGER invalidation clause

`[OMISSION CORRECTED — DESIGN §15 (D:1240-1317) was absent from this document in its
entirety. §16's invalidation clause above is carried; §15's is DIFFERENT and STRONGER, and
§15 also supplies the causal test, the five wrong answers, and the Little's-law rule that
belongs in Part O.]`

**The diagnosis, in one sentence** (D:1243-1244): "**each tile runs up to 1024 contexts
against a 64-entry L1 data request queue.**" That structural mismatch, not any of the
obvious things, is what set the run time.

**Where a resident context's cycles go.** Sampling settles it — 82% are spent in the ready
queue having failed to issue (D:1249-1257):

| bucket | share of resident context-cycles |
|---|---:|
| **ready, but the data port refused it** | **81.3 – 83.8%** |
| waiting on memory (blocked) | 16.1 – 18.4% |
| **execution latency** | **0.0%** |
| translation | 0.1 – 0.2% |
| atomic lock | 0.0% |
| **migration** | **0.0%** |

**Instruction-fetch retries are exactly zero**; every one of the 3.95 billion retries is
`dcache_->rq_occupancy() + ops > dcache_->rq_size()`. The core retires 0.21–0.36 ops/cycle
against a width of 4, so **it is not short of issue bandwidth — it has nowhere to put the
request.** Note what those zeros dispose of: **migration and execution latency contribute
0.0% of residency.**

**The causal test — widen the queue 8×** (D:1264-1268):

| | cycles | data retries | port-refused share |
|---|---:|---:|---:|
| rq = 64 | 2,792,466 | **3,952,205,189** | **83.8%** |
| rq = 512 | 2,606,914 | 848,821,225 | 24.2% |

**and the anomaly inverts with it** (D:1272-1275) — an MMU fix that had made the machine
*slower* becomes a gain once the port is not saturated:

| | pre-fix | fixed | gap |
|---|---:|---:|---:|
| rq = 64 | 2,691,129 | 2,792,466 | **+3.77%** |
| rq = 512 | 2,659,636 | 2,606,914 | **−1.98%** |

"*The regression was the saturated port converting a latency reduction into arrival
pressure it could not absorb.*"

**THE INVALIDATION CLAUSE, verbatim** (D:1277-1281): "**This invalidates comparisons taken
in the saturated regime.** Every number measured before this was taken at **rq=64**, where
the binding constraint was request admission rather than anything the experiment varied.
**Results that compare placement policies, mappings, or decomposition shapes need
re-taking with the port sized to the context count, or they are measuring the queue.**"
**This is broader than §16's clause above** — §16 invalidates on *capacity*, §15 on
*admission*, and the two regimes overlap but are not the same set of runs. **Both apply.**

**The five things wrongly blamed, each measured and believed** (D:1285-1305) — kept
because each is a live temptation:
- **DRAM.** Row-hit rate falls (9.09% → 8.66%) and correlates with cycles, but activates
  rise 0.35% against 6.8% more cycles — the DRAM did the same work. **Ranking runs by
  cycles/activate is circular: the numerator is the thing being explained.**
- **Atomic spinning.** Real, and fixed — worth **+1.1%, in the wrong direction**.
- **Atomic contention.** Real, and fixed: a 64-byte lock for a 4-byte update made a line of
  `parent[]` contend. **Waiting fell from 10,798,437 context-cycles to 1,465,999 and
  nothing moved** — it was 0.21% of residency. "*Comparing it against tile-cycles rather
  than context-cycles overstates it by the 1024 contexts a tile holds.*"
- **Tile imbalance.** Occupancy spread **triples (11% → 33%)**, which looks damning, but
  every tile still needs the same time to within **0.5%**: occupancy and residency co-vary
  exactly, **so no tile is a critical path.** (Read this against G.6's load-balancing
  finding: **a spread is not by itself a critical path**, and the stress workload's own
  2.5:1 spread turned out to be the workload's shape rather than the machine's — L32.)
- **The core scheduler.** Round-robin does give a context one issue slot per `N/4` cycles,
  but the width sits 90% idle and fetch retries are zero — **contexts are not queued behind
  each other, they are queued behind memory.**

**And the methodological rule it produced, which belongs to Part O and is stated there
too** (D:1307-1313): "*residency, occupancy and throughput are **one measurement in three
units, tied by Little's law**. **None can explain a change in the others, and every
argument built on their ratios was circular.** Only the sampled breakdown underneath them,
and the causal test of changing the suspected resource, actually attributed anything.*"

### D.5 As currently configured in ChampSim (tier 2, for reference, not as design)

`config/nmfc/nmfc_4tile.json`. **Read the three warnings before the table.**

`[CAUTION — the shipped headline config is STALE relative to its own generator.]`
Re-running `python3 config/nmfc/make_config.py` and diffing against the checked-in file:
the regenerated config adds `tile0_mode_port`…`tile3_mode_port` (`DRAM_MODE_PORT`) and
points each slice's `lower_level` at `@tileN_mode_port`. **The shipped
`nmfc_4tile.json` has zero occurrences of `mode_port` and wires
`tile0_LLC.lower_level = @tile0_LLC_DRAM_channel` directly** — so it does **not** strip
the mode bit at the DRAM boundary, which E.2 says is wrong two different ways.
`make_config.py:108-123` adds the port unconditionally when NMFC is enabled, so **the
shipped file cannot be reproduced by the current generator**.

`[CORRECTION — the staleness is not two files, it is THIRTY-ONE of thirty-three.]` An
earlier revision said "the newer sweep directories (`phys_ft/`, `ram/`) do carry the four
mode ports". `grep -c mode_port` over **all 33** checked-in configs returns **8** for
exactly two files — `phys_ft/nmfc_4tile.json` and `ram/nmfc_4tile_ramulator.json` — and
**0 for every other file in the tree**, including nine of the eleven files in the two
directories the earlier text exempted (`phys_ft/{baseline_4tile,bw4,bw8,ftu2048,ftu4096,
q128,q512,q2048}.json` and `ram/baseline_4tile_ramulator.json`), and all of `phys/`,
`nuca/`, `nuca_ft/`, `adapt/`, `cap/`, `ft/`, `wide/`, `mem_network_smoke.json` and the
three top-level files. **Assume a config does NOT strip the mode bit unless you have
grepped it. Regenerate before using any of them for anything.** Ledger **L27**.

`[CAUTION — the LLC slice size in this table is NOT the one Part L and N.1 were measured
at.]` This table records a **512 KiB** slice (2 MiB aggregate, `--llc-sets 2048`). Part
L's §29 inversion and N.1's stress workload were both run against **4 MiB slices
(16 MiB aggregate)** — L's mechanism is stated as "replicating an 8 MiB array onto four
**4 MiB** slices" and "aggregate capacity fell from about 16 MiB to about 4 MiB", and
N.1's stress set is "16 MiB of data against four **4 MiB** slices". **A reader who takes
this table as the configuration cannot reproduce Part L's mechanism at all** — the
replicated array does not fit in the aggregate. Ledger **L28**.

`[CORRECTION, and it is worse than a mismatch: NO CHECKED-IN CONFIG HAS A 4 MiB SLICE.]`
L28(c) previously framed this as D.5's table *diverging from* the configuration Part L
was measured at, which implies that configuration exists somewhere in the tree. It does
not. **Every one of the 33 configs has `tile0_LLC num_sets 512, num_ways 16` — a 512 KiB
slice and a 2 MiB aggregate** — including `cap/`, the only directory whose name matches
D.4's capacity study. `make_config.py:576` defaults `--llc-sets` to 2048 and `:240`
divides it by the tile count; a 4 MiB slice at four tiles needs `--llc-sets 16384`, which
**nothing in the tree uses**. So **Part L's mechanism and N.1's stress set cannot be
reproduced from any checked-in file at all** — they were run against a configuration that
was not committed. That bears directly on L34's provenance problem (three unreconciled
baseline migration counts) and on Part M's parity caveat. **Every capacity claim must
name its slice size, and Part L's must additionally name a config that no longer
exists.**

`[CAUTION — two grain sizes appear on one row below, and E.3 says the tooling refuses
exactly that.]` See the row's own note and ledger L20/L28.

`[CAUTION — the FTU is FOUR TIMES SMALLER than the context array it feeds, which is the
exact failure its own generator warns about, and the file cannot be regenerated.]` The
table below lists **FTU 1024** and **1024 contexts** as adjacent rows. They are not
adjacent facts: there are **four tiles**, so the machine has **4096 context slots against
a 1024-entry tracking unit** — a **4:1 cap** that bounds the whole machine at a quarter
of its own concurrency. `make_config.py:566-570` states the hazard in its own words:
"*The tracking unit is the ceiling on in-flight invocations for the whole machine ... Left
at a small value it silently caps everything: a 1024-context machine with a 64-entry unit
can never exceed 64 outstanding, and **every occupancy number it reports describes the
unit rather than the architecture**.*" And `:389` shows the generator would not produce
this file: `ftu_size = args.ftu_size if args.ftu_size > 0 else args.tiles * args.contexts`
— running `python3 config/nmfc/make_config.py --tiles 4 --contexts 1024` emits
`cpu0.ftu_size 4096`, while the checked-in file carries **1024** on both `cpu0` and
`cpu0_trace` with `num_contexts 1024` on each of `tile0_fc`…`tile3_fc`. **This is a
second, independent way the headline config is unreproducible from its own generator**
(the first is the mode ports above). **Every FTU-occupancy argument in N.4 and H.9 is
built on fullness of a unit that is undersized by construction**, which is a reason
to hold those numbers loosely alongside #180. (It is no longer a reason to doubt the
workload itself — L32 is closed and the run passes.) **The three configs that change
the cap are never mentioned elsewhere in this document:** `phys_ft/ftu2048.json` (2048),
`phys_ft/ftu4096.json` (4096) and `wide/nmfc_4tile.json` (**16384**); two more —
`nmfc_4tile_ramulator.json` and `nmfc_4tile_oracle.json` — carry **64**, which is the
generator's own worked example of the pathology.

| structure | geometry | note |
|---|---|---|
| host L1I | 64×8 = 32 KiB, hit 2 | stock |
| host L1D | 64×12 = 48 KiB, hit 2 | stock |
| host L2 | 1024×8 = 512 KiB, hit 10 | **its lower level is the fabric — there is no host-side LLC** |
| LLC slice per tile | 512×16 = 512 KiB, hit/fill 20, 64 MSHR, 32 PQ | aggregate 2 MiB fixed at `--llc-sets 2048` regardless of tile count, so the slice shrinks as tiles are added — this is what makes NMFC and baseline comparable |
| slice upper ports | 4: compute-tile fabric, fc D, fc I, this tile's MMU | one slice serves host and function-core traffic both |
| fc D$ | 64×8 = 32 KiB, hit 2, 64 MSHR | **1 bank — see D.2** |
| fc I$ | 16×4 = 4 KiB, hit 2, 8 MSHR | **1 bank — see D.2** |
| MMU TLB | small 32×4 = 128 entries (4 KiB), huge 16×4 = 64 entries (G), hit 1, 32 MSHR | two arrays probed in parallel |
| host core | ROB 352, LQ 128, SQ 72, RF 128, F/D/Disp 6, Exec 4, Retire 5, 4 GHz | `hashed_perceptron` + `basic_btb` |
| FTU | **1024** entries configured | **4:1 UNDERSIZED — read the caution above.** The generator would emit `tiles × contexts` = **4096**; this file says 1024, so the machine is capped at a quarter of its context array. `phys_ft/ftu2048.json` = 2048, `phys_ft/ftu4096.json` = 4096, `wide/nmfc_4tile.json` = 16384, and `nmfc_4tile_ramulator.json` / `nmfc_4tile_oracle.json` = **64** |
| function core | **1024** contexts configured, issue width 4, 250 ps | ×4 tiles = **4096 slots**, against the 1024-entry FTU above. See Part H |
| **placement** | `placement_policy: "round_robin"` on `fn_fabric` | **the invocation's tile is a COUNTER, not the translation — see A.4's `[CONFLICT]` and ledger L36.** The parameter has four values; the address is read on one of them, and not on this one |
| fabric | hop latency 8 cycles, queue 128 (→ 32 per destination at 4 tiles), 4 deliveries per class per cycle | 3 classes: invocations, migrations, completions |
| tiles / grain / mode bit | 4 / `grain_bits 21` = 2 MiB / bit 38 | **contradictory as written — see below.** The ramulator geometry derives **G = 1 MiB** at four tiles (E.3), and `config/nmfc/nmfc_4tile_ramulator.json` still declares `nmfc_grain_bits: 21`, which `src/nmfc/ramulator_mc.cc:280-287` rejects at construction: "nmfc_grain_bits is 21 (2097152 bytes) but this DRAM requires 1048576", then `std::exit(-1)`. **That file will not start.** Only the two files under `config/nmfc/ram/` carry `nmfc_grain_bits: 20`. [CORRECTION — "under `--dram default` nothing derives G, so 21 is merely unchecked, not endorsed" UNDERSTATES it: 21 is derivable from NO device in the tree, and BOTH the default controller and ramulator DDR5 require 20 (ramulator HBM3 requires 18).] The default memory controller is hardcoded at `make_config.py:171-172` and emitted verbatim into this file — `channel_width 8` bytes, `columns 1024`, `ranks 1`, `bankgroups 8`, `banks 4`, `channels 1`. Run `derive_geometry`'s own arithmetic (`make_config.py:528-536`) on it: `row_bytes = 1024 × 8 = 8192`; `banks_per_channel = 1 × 8 × 4 = 32`; `total_channels = 1 × 4 = 4`; **G = 8192 × 32 × 4 = 1 MiB → grain_bits 20.** The ramulator device gives the same answer by a different route (`ramulator/tile_ddr5.yaml:33,36`, `count: [1,2,8,4,65536,1024]`, `channel_width: 32` bits → `4096 × 64 × 4 = 1 MiB` → **20**). **21 comes from nowhere but `make_config.py:619-620`, which hardcodes `args.grain_bits = 21` under `--dram default` with no check at all.** **31 of the 33 checked-in configs carry 21; only the two under `ram/` carry 20.** So the shipped grain is not "unchecked", it is **wrong against the default controller AND ramulator DDR5, which both require 20 — and against ramulator HBM3, which requires 18 — and it is the tree-wide default.** **Derive it; never hand-set it (E.3).** *Second defect in the same file, and in `nmfc_4tile_oracle.json` and `ft/nmfc_4tile.json`:* **they contain no `module: tile_router` child at all**, so the `@ROUTER` reference the current `NMFC_VMEM` / `FUNCTION_FABRIC` / `FUNCTION_CORE` modules take cannot resolve. |

### D.6 The two caveats on the data cache — replacement and prefetch — AND THE USER'S ANSWER

D.3's chain opens with #76 naming exactly two caveats on the "is a data cache needed"
question: "**the only caveat I would give to that is two-fold: prefetching and
replacement.**" **The user answered both himself, in the very next message, and that
answer is uncontradicted anywhere later in the record.** It is architecture, not
speculation, and it is not in Part P because nothing here was rejected.

*User #77, 2026-08-28T19:03:26Z, verbatim and complete:*

> "**I would also want SHiP checked just to be sure regarding replacement.** Regarding
> prefetching, I am worried that even when prefetching from the dcache we still end up
> fetching lines that miss the LLC and increase bandwidth consumption without improving
> performance. **It would almost need to be a best-effort prefetch, where it only
> returns on LLC hit, and on LLC miss it drops.** A final thing: **I-cache prefetching is
> probably far-more pallatable than D-cache**, particularly for these workloads. **We
> could use a small FDIP or EIP** to fetch instructions before they are needed, since
> those should be highly predictable. The one caveat there is that FDIP would require a
> BTB + BP which we currently don't build since we don't predict branches. That being
> said, **if we aren't using the BTB for actual branch prediction, we could just encode
> it at the block-granularity and use a simple bimodal predictor plus that to predict
> instruction streams.** Just a thought. **EIP might be easier to implement** since it
> doesn't require any of that hardware."

*And the correction the next message makes, #78, 2026-08-28T19:07:54Z:*

> "Do you really not understand how prefetching works? **Consider a scan. No working set
> that is cacheable, yet a prefetcher can get a 300% performance increase.** Don't say
> stupid stuff. Regarding the I-cache, **are you saying each function consumes no more
> than 8 instructions? I find that unlikely.** are you serious?"

**What this settles, itemised:**

| item | ruling | authority |
|---|---|---|
| **Replacement policy** | **Check SHiP** before concluding the data cache is not worth having. The 11% hit rate is not evidence on its own until replacement has been tried — and N.6 records that no replacement policy pushed it past **13.9%**, which is the check being *done*, not a reason to skip it. **The 13.9% is tier 2 only — `config/nmfc/make_config.py:319-325`; it appears nowhere in DESIGN.md and nowhere in the session log.** | #77 for the ruling; `make_config.py:319-325` for the 13.9% |
| **A low hit rate does NOT imply a prefetcher is useless** | "Consider a scan. No working set that is cacheable, yet a prefetcher can get a 300% performance increase." **Hit rate and prefetchability are different properties.** Arguing from an 11% hit rate to "no prefetcher" is the specific error the user was correcting. | #78 |
| **D-cache prefetch semantics, if built** | **Best-effort admission: return on LLC hit, DROP on LLC miss.** The reason is bandwidth — a prefetch that misses the LLC spends channel bandwidth the machine exists to conserve, with no performance return. This is an *admission rule on the prefetch*, not a prefetcher choice. | #77 |
| **I-cache prefetch is the more attractive one** | "far-more pallatable than D-cache, particularly for these workloads" — instruction streams here are small, replicated and highly predictable. | #77 |
| **How to build it without a branch predictor** | Two named options. **EIP** is the easier: it needs none of the BTB/BP hardware. **FDIP** would normally need a BTB + branch predictor, which this machine does not have — but H.5's **shared BTB already exists for fetch-address generation**, so the user's own suggestion is to **encode the BTB at BLOCK granularity and pair it with a simple bimodal predictor** to predict *instruction streams*. **That predicts streams, not execution** — nothing issues on the answer — so it does not violate H.1's "no speculative execution". | #77 |
| **"a function is ≤ 8 instructions"** | **Rejected as an assumption.** "I find that unlikely. are you serious?" Do not size the instruction side on it. Note this is a *third* distinct meaning of "8" — see the note at I.10. | #78 |

**Status: none of this is built in ChampSim or SST.** It is not in Part P because it was
not rejected; it is specified work with the user's own preferred shapes named.

**AND THE BIMODAL PREDICTOR IN THE FDIP ROW IS NOW ADOPTED.** `[RULED — user ruling
2026-09-03 **O12**, verbatim: "**I think bimodal is fine, since it only ever speculatively
issues a single fetch, never executes. It is also essentially free (built into the btb
table, tracks a particular branch).**"]` It **is** a branch predictor, which is why H.1's
opening sentence had to be narrowed rather than reconciled away: **H.1 now reads "no branch
predictor IN THE EXECUTION PATH".** The adopted form is the **existing shared BTB
re-encoded at block granularity with one bimodal bit per entry** — one speculative fetch,
never an execution, one bit of cost. **The other rows in this table are still unbuilt and
unruled work**; only the FDIP predictor was ruled. See H.1 and H.5, and note that **the
never-mispredicts caveat on every measured function-core number is unaffected.**

TLB sizing is **deliberately small**: "at graph scale the regime is mostly-miss
whatever the size, and a generous TLB would flatter the design rather than measure
it" (`config/nmfc/make_config.py:288-291`). A 1024-entry array at 2 MiB reaches 2 GiB
— still about 2% of a 100 GiB graph. Huge pages **move the constant, not the
asymptote**.

---

## PART E — THE PARTITION, THE GRAIN FORMULA, AND THE BANK-SWEEP CAVEAT

### E.1 Where the partition is, and what it reads

The partition is applied **at the fabric** (I13), on the **physical** address (I12),
and the field it reads is chosen by the **mode bit**:

```
mode = 1  (NMFC)      tile = (pa >> log2(G))     mod N
mode = 0  (STANDARD)  tile = (pa >> log2(block)) mod N
```

`log2(G) >= log2(block)`. (`inc/nmfc/tile_map.h:93-135, 204`.)

**Why `N` must be a power of two — stated correctly, because the usual one-liner ("a
modulo would not be invertible") is not true as written.** `x mod N` is perfectly well
defined for any `N`. What actually requires a power of two is that **the tile index be a
contiguous BIT FIELD of the physical address**. Only then is `compact` — removing that
field on the way into a slice — and `expand` — re-inserting it on the way out — a pair of
**exact inverses and pure functions of (address, tile)**, with no per-request
bookkeeping (C.2, F.3). With a non-power-of-two `N` the tile index is an arithmetic
residue, not a field; there is nothing to excise, and a slice would have to carry state
to reconstruct the address it was handed. **The constraint is about slice indexing, not
about modular arithmetic.**

**Where the mode bit sits — three statements existed, and they were not the same
statement.** [SUPERSEDED IN FORM, NOT IN CONTENT — **C.2's bit-field figure is now the
primary statement of this**, and it shows in one picture that the three rows below are the
same layout with `mode_bit` at three different heights. This table is kept because it
names which of the three BINDS.] Reconciled:

| statement | reading |
|---|---|
| **the CONSTRAINT (binding):** `mode_bit >= log2(G) + log2(N)` | The bit must lie **strictly above the tile field**, or it would corrupt tile selection. This is the rule an implementation must satisfy. |
| **the PLACEMENT RULE (a convention, not a law):** "one position above the top of the DRAM range" | The natural place to put it, because it makes the physical address space exactly one bit wider than DRAM and nothing else. **It is a convention that satisfies the constraint; it is not itself the constraint.** |
| **the SHIPPED VALUE (tier 2, informational):** bit 38 in `nmfc_4tile.json` | With four tiles ChampSim models 64 GiB of DRAM (ledger L18), whose top is bit 36 — so bit 38 is **two** positions above, not one. It satisfies the constraint and departs from the convention. **Neither a bug nor a design statement; just what the file says.** |

**Build to the constraint. Prefer the convention. Do not read bit 38 as the
architecture.**

### E.2 The mode bit

**One physical address bit, one position above the top of the DRAM range, applied at
translation time, stamped at allocation, never changed.**

*User #10, 2026-08-27T06:41:05Z, who reduced it to this form himself:* "Regarding
mapping mode, I think this is actually even simpler than you designed: **we are really
talking about an extra bit of a physical address, applied at translation time beyond
the end of the address space. It really is that simple, isn't it?**"

*User #12, 2026-08-27T06:59:23Z, on why it never needs to change:* "remember, this is
at the allocation granularity. The OS would presumably be setting that mode bit,
meaning that we just enforce the bit never changes, or the OS needs to rewrite the
entire page if it does so (**or, more than likely, copy the page to a new frame with
the mode bit off and update page table**). Remember, if we stop using it and free it,
**that page is considered garbage anyways.**"

**Why an address bit and not a table or a PTE bit:** *caches tag by address*. A dirty
line evicted from L2 has no TLB entry behind it any more, so a PTE-carried bit would
have to be stashed in cache-block metadata to survive a writeback. An address bit is
already stored, already carried, and already evicted with the line — demand, prefetch
and writeback traffic all get it for free (DESIGN §5.3 D:635).

**The two halves are not aliases.** `{0, X}` and `{1, X}` name *different* DRAM cells,
because the mode changes the row/bank/channel assignment. **The physical address space
is therefore one bit wider than DRAM.**

`[DISAMBIGUATED — this sentence and the "already unique" sentence two paragraphs below
gave an implementer two different widths, and the bridging paragraph between them argued
non-aliasing, which is a different property. **They are about two different fields at two
different levels, and both are true. Size them separately:**]`

| what you are sizing | width | why |
|---|---|---|
| **the PHYSICAL ADDRESS carried by caches, the fabric and the slices** | `mode_bit + 1` bits, and the binding constraint is `mode_bit >= log2 G + log2 N` (E.1, `tile_map.h:100-101`). Under the *convention* — mode bit one position above the top of DRAM — that is exactly **one bit wider than DRAM**. | the mode bit is a real, stored, carried, evicted address bit (that is the whole reason it is an address bit and not a PTE bit), so everything above the DRAM port must carry it |
| **the FRAME NUMBER handed to a channel, below the DRAM port** | `log2(DRAM bytes) - log2(grain)` bits, **no mode bit** | `strip_mode()` removes it at the port (`tile_map.h`, `dram_mode_port.cc:1-33`) |

**And the reconciliation, stated so it is not re-derived:** "one bit wider" is a statement
about the **encodable range** the machine's structures must carry; "already unique across
both modes" is a statement about the **live set**, and it holds because F.9's allocator
hands grains out in aligned N-runs and a group is in exactly one mode, so `X` is never
live in both modes at once. **The address space is one bit wider than DRAM; the live
mapping from address to DRAM cell is injective without the bit. Both, at the same time.**
The bit is discardable *because the allocator guarantees it is redundant on live
addresses*, not because the space was never wider.

**Stripped at the DRAM boundary — and this paragraph and the one above it are NOT a
contradiction, though they read as one without the fact that reconciles them.** The
apparent problem: if `{0, X}` and `{1, X}` name different DRAM cells, how can the bit be
discarded before the DRAM sees it? **The answer is in F.9 and belongs here too: the
allocator hands grains out in aligned N-runs (*groups*), and a group is in exactly one
mode. So `X` is never live in both modes at once**, and after stripping there is no pair
of live addresses left to alias. Mixing modes inside a group is precisely what breaks
this, and it was measured: **6.7% of blocks colliding on a channel-local address**,
undetectable below the controller (R21, F.9). **The mode bit is discardable because the
allocator guarantees the two tilings never overlap in a live group — not because the two
halves were aliases all along.**

The DRAM does not need it: by the time a request
reaches a channel the routing and slicing are done and what is left is a frame number,
already unique across both modes because they draw from one pool of grains. Leaving it
in is wrong two different ways — ChampSim's stock mapping sizes its row field from
`lg2(rows)` and silently drops anything above it, collapsing an NMFC and a STANDARD
address at the same frame onto the same row and bank; ramulator2 decodes against a
device that has no such row at all (`src/nmfc/dram_mode_port.cc:1-33`).

### E.2a THE MODE BIT, MEASURED — DESIGN §32, and the three traps found making the other mode reachable

`[OMISSION CORRECTED — BLOCKING. DESIGN §32 (D:3684-3753), the final section of the
document, was absent from this canon in its entirety. E.2 above argues the whole case for
the mode bit with NO MEASUREMENT ANYWHERE. §32 supplies the only one there is, plus a
zero-mode-bit trap stated as a design fact, plus a hole in exactly the check invariant 9
orders run on every run.]`

**Until §32, only one of the two mappings could actually be carried.** The `STANDARD`
type existed in the page table and the fabric routed it, but no configuration could use
it, "*because the LLC slice and memory controller below each tile are declared with a
grain interleave and a block-spread address does not satisfy one*". DESIGN's own verdict
on that state: "**That is worse than not having written it: it read as implemented.**"

**What it took: a linker section (`.nmfc_std`, its own grain, because a grain carries one
type — R22), an `NMFC_STD_DATA` attribute, a region derived from the symbols — and three
things that were not obvious.**

**(1) THE ZERO-MODE-BIT TRAP — this is a design fact, not an SST detail** (D:3705-3712):

> "No configuration set one, and **`tileOfPhysical` reads a zero mode bit as 'there is one
> mapping and it is the NMFC one' — which is correct with one mapping and wrong the moment
> there are two.** `coherent_memory.mode_bit()` computes it once, from the memory size, and
> hands the same value to the fabric, the host MMU and every tile. **Each builds its own
> page table, so a component with a different mode bit computes different frames for the
> same address, and nothing detects that: the addresses stay legal, they simply are not the
> same addresses.**"

**The requirement: the mode bit is computed ONCE, from the memory size, and HANDED to
every component that builds a page table** — the fabric, the host MMU, and every tile.
**Never recomputed per component.** This is the same class of failure as Appendix 2 D7's
"two page tables that are copies built from different parameters", and it fails the same
way: silently, in legal addresses. It is also why a default-zero mode bit is not a safe
default — it is a *correct* answer with one mapping and a *wrong* one with two.

**(2) THE DOWNSTREAM RANGE CHECK IS GIVEN UP, DELIBERATELY AND VISIBLY** (D:3707-3717):

> "**The slice and controller ranges had to widen.** memHierarchy cannot express the union
> of two interleaves, so a configuration carrying block-spread data **gives up the
> downstream range check and leaves the fabric's routing as the only enforcement. This is a
> real loss of the check invariant 9 asks for, and it is confined to configurations that
> ask for it: nothing declares standard data by default, and every other run keeps the
> grain-interleaved check. `build()` says so on stderr rather than doing it quietly.**"

**Read all three clauses together.** It is a real loss of an I9 check; it is **scoped** to
configurations that declare standard data; **nothing declares it by default**; and it is
**announced on stderr**. That is a bounded, documented trade-off, not a defect awaiting
repair — see Appendix 2's **S13**, which this document previously mis-filed as `[WRONG]`.

**(3) THE CONGRUENCE CHECK'S STEP HAD TO CHANGE, AND THE OLD STEP WAS A HOLE IN I9's OWN
CHECK** (D:3719-3721):

> "`checkCongruent()` walked every region **by the grain**. **A standard region's unit is a
> cache block, so that checked one block in every 16,384 and called the rest congruent
> without looking.**"

**16,384 = 1 MiB grain / 64-byte block.** I9 orders congruence checked *on every run*; a
checker that steps by the grain over a block-spread region is checking 0.006% of it and
reporting a pass. **Step by the region's own unit, not by the grain.**

**AND THE MEASUREMENT — the only one that validates the mode bit's existence**
(D:3739-3753). A 4 KiB block-spread sum, 1,024 loads:

| tiles | migrations, 1,024 loads | |
|---:|---:|---|
| 1 | **0** | every block is local; there is one channel |
| 2 | **65** | |
| 4 | **65** | |

**Sixty-five, and the same at two tiles as at four, is what the mapping predicts** and is
the check that the model is not approximating it: a sequential scan crosses a block
boundary every 64 bytes, and **at two tiles or more consecutive blocks are always on
different tiles, so the count does not depend on how many there are.** Sixty-four
boundaries in 4 KiB, plus one arrival; the same array declared `grain` contributes the one.

> **DESIGN's closing sentence, and it is the one to quote when the mode bit is questioned**
> (D:3749-3753): "**That is §5.3's argument stated as a measurement rather than as a
> claim. The same 512 words cost 64 migrations block-spread and 1 grain-striped.** The mode
> is not a worse version of the NMFC mapping; **it is the mapping for data that has no
> function core to own it, and its cost when a function core does touch it is the reason
> the other mapping exists.**"

**64 : 1 is the mode bit's justification, measured.** It is also the quantitative form of
C.3's `STANDARD` row ("a 1 MiB frontier is ONE grain, lands entirely on one tile, and every
invocation migrates there") read from the other direction.

**One trap found in the same run and worth keeping as a test** (**DESIGN §32.2**,
D:3727-3737 — the subsection is titled "And then the measurement disagreed with the
mapping", and the two quotations above are both from it): the first run
reported **three** migrations where the mapping calls for **sixty-four**. The cause was the
TLB **caching the owning tile beside the frame** — right for every type whose page sits on
one tile, wrong for the one type whose owner changes *within* a page. **The tile is derived
from the frame — `tileOfPhysical(frame)`, the same function the fabric routes by — and
never cached beside it.** For every other type the two are equal by construction, which is
what `checkCongruent()` asserts, so the change is a no-op everywhere except where it was
wrong. (Same scar as Appendix 2 D7's "a per-page owner cached alongside the translation".)

### E.3 The grain formula

```
G = row_bytes_per_channel  ×  banks_per_channel  ×  total_channels

where  total_channels = channels_declared_per_ramulator2_instance  ×  N
                      = 1 × N = N,   BECAUSE of E.6's one-channel-per-instance rule
```

[DISAMBIGUATED — BLOCKING, and it was the highest-consequence ambiguity in Part E. This
formula appeared three times in this document with three different third factors — `N` in
the notation table, `num_channels` here, `total_channels` at E.5 — and **no statement of
what the factor refers to.** It is **device channels**, `total_channels`. It equals `N`
**only because E.6 D:2794 requires each ramulator2 instance to declare exactly one channel
internally**; that constraint is what makes tile = channel = controller = instance one
object (D.1), and E.3 previously never cited it. **Write `total_channels`. Never write
`N` in this formula** — on a machine that broke the one-channel rule the two would
diverge and `G`, the NMFC page size, would move with them. This is also why the HBM3 row
below reads "1 in the instance × 4 tiles" and why the retired DDR5 row's "8 channels"
against a four-tile machine is a device description, not a machine description.]

**The threshold is forced by geometry, not chosen.** A per-unit mapping mode is only
safe when the tagged unit owns **whole DRAM rows**, so two units in different modes
can never contend for the same bank and column slots. Under either mode a G-unit
consumes identical capacity from **disjoint** resources:

| mode | occupies | capacity |
|---|---|---|
| 0 STANDARD | one row index, **all banks, all channels** | G |
| 1 NMFC | `num_channels` consecutive rows, **all banks, one channel** | G |

That equality is exactly what licenses tagging the mode per unit rather than
partitioning the address space.

**Instantiations:**

**`banks_per_channel` in this table is the FULL per-channel count — the product of
EVERY organisation level between the channel and the row, whatever the device calls them
(E.4's positional rule, user #55: "RANK BITS AND BANKS ARE MEANT TO BE INCLUDED"). On DDR5
those levels are ranks × bankgroups × banks, three of them; on HBM3 they are
pseudo-channels × Sid × bankgroups × banks, four of them. The 32 that
appears elsewhere in this document is the *per-rank* count of the checked-in DDR5 device
and belongs to the SLICE BANKING decision (D.2), not to `G`.** An earlier revision of this
table computed the first row with 32 and therefore contradicted E.4 and ledger L8 thirty
lines below it.

**[#141 IS ANSWERED, AND SO IS THE WHOLE CLASS OF QUESTION. A MULTI-PARAGRAPH TAG: it runs
to the "END OF THE GEOMETRY RULING" marker below.]**

*User ruling 2026-09-02, verbatim:* "**Ranks are included. 32 banks per channel assumes
DDR5 and one rank. 32 banks per channel is not part of the design or spec, that is part of
the memory technology, the entire system must adapt to an arbitrary bank count,
tile-memory-sizing, grain-sizing. Please do not lock in any bank/rank/column/row/channel
counts as if they were the only ones supported. WE MUST SUPPORT ALL POSSIBLE VALUES FOR
EACH, WITHIN A FULL 48-bit PHYSICAL ADDRESS SPACE.**"

**So #141's "32 banks per channel" was a description of DDR5 at one rank, and it was never
a specification.** Ranks are in. The formula stands as written, and the tables below are
**instantiations of it at two devices** — not a menu of the geometries this machine
supports. **The machine supports all of them, inside a 48-bit physical address space.**
Ledger **L8** is closed on that basis, and every count in this Part is
**configuration; see SELECTED CONFIGURATION**. The full four-clause statement is at
invariant 12. **[END OF THE GEOMETRY RULING.]**

| device | row | banks/channel (**full: every level between channel and row — E.4's positional rule**) | channels | G |
|---|---|---|---|---|
| **DDR5-4800 ×8, 32-bit channel, 4 tiles — the checked-in `ramulator/tile_ddr5.yaml`, and the reference configuration** | 4096 B | **64** | 4 | **1 MiB** |
| **ChampSim's DEFAULT memory controller, 4 tiles** — hardcoded at `make_config.py:171-172` and emitted into every `--dram default` config: `channel_width 8` B, `columns 1024`, `ranks 1`, `bankgroups 8`, `banks 4`, `channels 1` | 8192 B | **32** (1 × 8 × 4) | 4 | **1 MiB** |
| **HBM3 as actually configured** — `config/nmfc/ramulator/tile_hbm3.yaml`, `count: [1, 2, 2, 4, 4, 16384, 256]`, `channel_width: 32` | **1024 B** (256 columns × 4 B) | **64** = **PseudoChannel 2 × Sid 2 × BankGroup 4 × Bank 4** — **four** levels, not three; the hierarchy is `Channel, PseudoChannel, Sid, BankGroup, Bank, Row, Column` (`ext/ramulator2/python/ramulator/dram/hbm3.py:12-21`), and **a pseudo-channel is not a channel** (E.4) | 1 in the instance × 4 tiles = **4** | **256 KiB** at four tiles |
| *(retired)* DESIGN §5.2's DDR5 worked example — 8 KiB row, "32 banks, 8 channels → G = 2 MiB" | 8 KiB | **64** under E.4's ruling, not 32 | 8 as written | **4 MiB, not 2 MiB — see below** |

**The two memory models the shipped configurations actually run on — ChampSim's DEFAULT
memory controller and the ramulator DDR5 device — both derive G = 1 MiB at four tiles, and
therefore `grain_bits 20`.** Thirty-one of the thirty-three checked-in configs declare
**21**, which is derivable from nothing (D.5, ledger L20).

**[DISAMBIGUATED — "both devices" is a two-reading phrase and one reading is false. The
tree holds THREE geometries, not two: the default controller, ramulator DDR5, and
ramulator HBM3. The first two give 1 MiB / `grain_bits` **20**; **HBM3 gives 256 KiB /
`grain_bits` 18** — the third row of the table above and E.4's table both compute it.
Whenever this document says "both devices require 20" it means the **default-controller /
DDR5 pair**, which is the pair L20's arithmetic runs on. **HBM3's 18 is not a defect and
not an exception: it is the geometry rule working.** What L20 records as a defect is that
**21 is derivable from NO geometry in the tree** — not that a second geometry disagrees
with the first.]**

**The HBM3 row supersedes the "1 KiB row, 16 banks, 32 pseudo-channels → 512 KiB"
line that earlier revisions carried: no checked-in file describes that device.** Derive
the row from the file you are actually running (`derive_geometry` in
`config/nmfc/make_config.py`), never from a datasheet paraphrase.

`[CORRECTION — DESIGN §5.2's DDR5 worked example is WRONG under E.4's ruling, and an
earlier revision of this table concealed that by silently re-parameterising it.]`
DESIGN.md:615 reads, verbatim: "**DDR5** — 8 KiB row per rank, 32 banks, 8 channels →
**G = 2 MiB**". An earlier revision of this table printed that row as "8 KiB row, 2 ranks
× 8 bankgroups × 4 banks, **4 channels** → 2 MiB" — announcing the bank correction
(32 → 64, per E.4 and ledger L8) while **silently halving the channel count from 8 to 4**,
which is the only reason the answer still came out 2 MiB. **Under E.4's ruling DESIGN
§5.2's stated DDR5 configuration gives 8 KiB × 64 × 8 = 4 MiB.** DESIGN §5.2 has two
worked examples; this document supersedes the HBM3 one on the record (below) and must
supersede this one the same way rather than quietly re-fitting it. **Ledger L8 rules on
64-vs-32 but does not reach the §5.2 example; it does now.** Neither §5.2 example
describes a device in this tree — use the first two rows.

**The same number is three things at once, and that is not a coincidence:** the
tagging granularity, the siloing granularity, and the NMFC-data page size. All three
are forced by the row argument.

**G IS NOT A CONSTANT.** *User #150, 2026-08-29T06:18:19Z:* "**Grains change size
depending on the org. Can you not remeber anything?**" It is 512 KiB at two tiles and
1 MiB at four on the same device. **A binary laid out for one machine is not laid out
for another**: running a 512 KiB layout on a 1 MiB machine put two page types in one
grain and the §5.2 check caught it (DESIGN §29.3 D:3397-3405). Derive it; never
hand-set it. ChampSim refuses a contradicting `--grain-bits` outright:
"`--grain-bits {} contradicts the DRAM, which requires {} ... Omit it: the device
decides.`" (`config/nmfc/make_config.py:607-611`). **But two checked-in files are
already in the state that refusal exists to prevent** — `nmfc_4tile_ramulator.json`
declares 21 against a device requiring 20 and **exits at construction**, and `annotate`
defaults to 20 while the top-level ChampSim config says 21. See D.5 and ledger L20/L28.

`[AND THE RULE IS BROKEN IN THE ONE PLACE IT CANNOT BE FIXED FROM A CONFIG FILE: THE
WORKLOAD. Previously unrecorded.]` **`tools/nmfc/kernels/bfs_nmfc.cc` hand-sets the grain
TWICE, at two different scopes, and they do not agree.**

- `:46` — `static constexpr uint32_t NMFC_GRAIN_BITS = 20;`, **compile-time**, under a
  comment at `:40-45` declaring the choice deliberate: "*Geometry is compile-time, and
  that is **a correctness requirement rather than a tuning choice**. Passed as runtime
  arguments it costs a hardware divide per neighbour and pushes the parameter list past
  the registers the ABI passes in, which puts arguments on a stack this machine does not
  have.*" **The reason is real. The consequence is that the binary carries a grain.**
- `:673` — a *separate* `uint32_t grain_bits = 20;` in `main`, settable with
  `--grain-bits` (`:679-681`).

**They do not cover the same allocations.** The runtime value reaches **only**
`AllocParent` (`:520-534`, the `parent` array). The compile-time constant aligns
everything else: `:577` `const std::size_t grain = std::size_t{1} << NMFC_GRAIN_BITS;`,
used for the **output pool** (`:584`) and **both frontier bitmaps** (`:595`, `:598-599`).

**So `--grain-bits 21` produces a binary containing two different grain sizes**: `parent`
aligned to 2 MiB, pool and bitmaps aligned to 1 MiB. **It cannot be laid out for the 2 MiB
grain that 31 of 33 configs declare.** Combined with ledger L20 — where the only route
past the two geometry contracts is `annotate --grain-bits 21` — the checked-in workload
**cannot honour the override that the checked-in configurations require.**

**RULE, restated so it binds the workload and not only the config:** *derive it; never
hand-set it* applies to **the binary too**. If geometry must be compile-time for the ABI
reason above (and the reason stands), then **the build must take `G` from the same
`derive_geometry` the configuration uses** — a generated header, not a literal — and
`--grain-bits` must either drive every allocation or be removed. **A workload with two
grains in it is the "binary laid out for one machine" failure with both machines inside
one binary.** See D.5 and ledger L20.

**THE DRAM MODEL IS GENERATED, NEVER TRANSCRIBED** (DESIGN §17 D:1373-1420). This is a
process rule with a correctness consequence, and it was previously absent from this
document entirely.

- **The ramulator YAML is emitted by the generator (`gen_tile_config`) and is never
  hand-edited.** A hand-written YAML is a transcription of a device, and a transcription
  is where a silent divergence between "the configuration" and "the geometry the rest of
  the machine derived" gets in.
- **`preset:` keys are silently ignored in this fork.** DESIGN §17 D:1396-1402:
  "**In this fork of ramulator2 the presets live in `python/ramulator/dram/ddr5.py`, not
  in a header** ... **Earlier revisions of ramulator2 did compile the presets into
  headers; this one does not, so `preset:` keys in a YAML are silently ignored rather
  than rejected.**" A config written on the assumption that a preset is being applied is
  running a different device from the one it names, with no error. (This corrects R72's
  trailing clause — see P.4.)
- **The settled 1 MiB grain follows from a DELIBERATE non-JEDEC choice, `rank: 2`.**
  DESIGN §17 D:1389-1391: "**The 1 MiB grain quoted in section 0 follows from `rank: 2`;
  the preset would give 512 KiB.** Anyone reading the config could not tell that a
  non-JEDEC organization was chosen, or why." **Record it as a choice, not as the
  device's natural geometry** — halving it back to one rank halves `G`, which relays out
  every binary (E.3's "G IS NOT A CONSTANT").
- **The 256/256 read/write buffers are hygiene, not a bandwidth result.** They remove
  essentially all controller refusals — **11.9M → 0.24M on the hot tile** — and DESIGN
  §17 D:1404-1408 says explicitly to "**record it as hygiene, not a bandwidth fix.**"
- **`channel_width: 32` is BITS, not bytes.** DESIGN §17 D:1420: "**Any statement about
  'percent of channel peak' has to be against 19.2** [GB/s], and a measured 9.8 GB/s is
  half the subchannel — not a quarter of a channel." Every bandwidth-utilisation
  percentage in this document (N.6's 77.9% / 20.2% included) is against that denominator.

### E.4 What counts as `banks_per_channel` — the user's ruling

*User #55, 2026-08-28T03:44:51Z, in his own capitalisation:* "Lets not think of DDR5,
lets think of any DRAM-like memory technology. We have channels, we have ranks, we
have banks + bankgroups, we have rows, we have columns. **Rows and columns are unique
per bank, which are unique per bankgroup, which are unique per rank, which are unique
per channel. Channel capacity is the addressable space on all RANKS under a channel.
We partition by CHANNEL. RANK BITS AND BANKS ARE MEANT TO BE INCLUDED** ... **EACH
MEMORY SLICE OWNS ONE RAMULATOR2 INSTANCE. THE SIZE OF THE ADDRESSABLE PHYSICAL
ADDRESS SPACE IS THE SUM OF THE ADDRESS SPACE ACROSS ALL CHANNELS. ALL RAMULATOR2
INSTANCES.** ... **IT IS IMPERATIVE YOU UNDERSTAND THE SIZE OF GRAIN YOU NEED.**"

**THE RULE IS POSITIONAL, NOT NOMINAL, AND THAT IS THE WHOLE OF IT:**

> **`banks_per_channel` = the product of EVERY organisation level between the channel and
> the row — that is, `count[1 .. len-3]`, everything after `count[0]` (channels in the
> instance) and before `count[-2]` (rows) and `count[-1]` (columns). It is NOT a fixed
> list of three named levels. Different devices have different numbers of levels between
> the channel and the row, and every one of them counts.**

Tier 2 computes exactly this and is the authority for it —
`config/nmfc/make_config.py:496-546`, `derive_geometry()`:
`channels_in_instance, rows_per_bank, columns = org[0], org[-2], org[-1]`, then
`banks_per_channel = 1; for level in org[1:-2]: banks_per_channel *= level`. Its docstring
states the reason in the user's own terms: "*Everything between the channel and the row is
addressable under that channel, so ranks, bank groups and banks all count toward
`banks_per_channel`.*"

Applied to the two devices in the tree:

| device | `count` | `count[0]` = channels in instance | levels between channel and row | `banks_per_channel` | `row_bytes` | `total_channels` at `N`=4 | `G` |
|---|---|---|---|---|---|---|---|
| **DDR5** `tile_ddr5.yaml` | `[1, 2, 8, 4, 65536, 1024]` | 1 | **Rank 2 × BankGroup 8 × Bank 4** — three levels | **64** | 1024 × 4 B = **4096 B** | 1 × 4 = **4** | **1 MiB** |
| **HBM3** `tile_hbm3.yaml` | `[1, 2, 2, 4, 4, 16384, 256]` | 1 | **PseudoChannel 2 × Sid 2 × BankGroup 4 × Bank 4** — **FOUR** levels | **64** | 256 × 4 B = **1024 B** | 1 × 4 = **4** | **256 KiB** |

**[CORRECTED — BLOCKING. A MULTI-PARAGRAPH TAG: it runs to the "END OF THE E.4 BANK-COUNT
CORRECTION" marker below.]**

It is the same class of error E.3 already caught once (the
"silently halving the channel count from 8 to 4" correction below). This section read "So
`banks_per_channel` = **ranks × bankgroups × banks**, everything between the channel and
the row", which names THREE levels — but E.3's HBM3 row computes 64 from **four**
(`2 × 2 × 4 × 4`), and nothing in this document said what the extra level was. An
implementer applying the prose rule to HBM3 would have taken `2 × 4 × 4 = 32` or
`2 × 2 × 4 = 16`, **halving or quartering `G`** — and `G` fixes the NMFC page size, the tag
granularity and the silo granularity at once (E.3). The generator's rule
(`product of org[1 .. size-3]`) was correct and was quoted alongside the wrong prose gloss;
the prose is now the positional rule and the named lists are shown per device.

**The extra level is named, from tier 2.** Ramulator2's HBM3 hierarchy is
`Channel, PseudoChannel, Sid, BankGroup, Bank, Row, Column`
(`ext/ramulator2/python/ramulator/dram/hbm3.py:12-21`) — seven levels, which is why
`count` has seven entries. So HBM3's `[1, 2, 2, 4, 4, 16384, 256]` reads Channel 1,
**PseudoChannel 2**, **Sid 2**, BankGroup 4, Bank 4, Row 16384, Column 256. DDR5's
hierarchy is `Channel, Rank, BankGroup, Bank, Row, Column`
(`ddr5.py:12-19`) — six levels, six entries, three between channel and row. **"Ranks ×
bankgroups × banks" was the DDR5 naming of the positional rule, not the rule.**

**AND `total_channels` IS SETTLED FOR A PSEUDO-CHANNEL DEVICE: A PSEUDO-CHANNEL IS NOT A
CHANNEL.** `derive_geometry` takes `channels_in_instance = org[0]` — the **Channel** level
only — so HBM3's `total_channels` is `1 × 4 = 4`, and its PseudoChannel and Sid levels fall
inside `banks_per_channel`. This is the answer to the question E.3's
`total_channels = channels_declared_per_ramulator2_instance × N` left open for devices with
a level between channel and rank. It follows from #55's own rule — "*Channel capacity is
the addressable space on all RANKS under a channel*", "**RANK BITS AND BANKS ARE MEANT TO
BE INCLUDED**" — applied positionally: everything under the channel and above the row is
included, whatever the device calls it.

**[END OF THE E.4 BANK-COUNT CORRECTION.]**

**Row bytes.** `row_bytes = columns × channel_width_bytes`. Multiplying columns by the
*transaction* size instead counts the burst twice — ramulator's mapper reduces the
column field by the internal prefetch while shifting the address down by the
transaction size, so the prefetch cancels. Getting this wrong reported 64 KiB rows and
256 GiB channels once. `[CITATION ADDED.]` The 64 KiB half is stated in tier 2 in those
words — `src/nmfc/ramulator_mc.cc:149-155`, "*Using the transaction size here instead
counts the burst twice, which is how a **4 KiB row was reported as 64 KiB**.*" **The
"256 GiB channels" half is stated nowhere and is a derivation from it**, shown here so it
is not re-derived: `channel_capacity_bytes = banks_per_channel × rows_per_bank ×
row_bytes` (`ramulator_mc.cc:156`), so on the reference device with the bad row size,
64 × 65,536 × 65,536 = **256 GiB** against the correct 64 × 65,536 × 4,096 = **16 GiB**.

Related fact the user supplied and which the geometry must respect: **"I am fairly
certain DDR5 only has 32 banks"** (#53, 2026-08-28T03:38:01Z — quoted verbatim; the user
wrote *banks*, and "per rank" is this document's reading of it, not his word), the channel
data width is 32-bit on
DDR5, 64-bit on DDR4 (#16, 2026-08-27T16:44:29Z), and the ramulator frontend must be
ticked at a frequency **we** define by the rate we tick it against our own simulation
speed — never a copied constant (#49, 2026-08-28T03:11:15Z: "Don't just copy an 8/3
tick because you saw it somewhere. That is lazy, and, in this case, blatantly
incorrect.").

#### E.4a The newest tier-1 bank count — #141 says 32 per CHANNEL, and it is ANSWERED (geometry ruling, 2026-09-02)

[ADDED — the canon previously ruled 64 on #55 and cited #141 nowhere at all
(`grep -c "#141\b"` returned 0), while E.3 legislated in advance that the 32 belonged
"never to `G`". Under #307 a newer tier-1 statement is not disposed of by silence.]

**The statement.** *User #141, 2026-08-29T05:16:38Z, verbatim:* "**Channel being busy
11.98% of the time seems odd. We have only 32 banks per channel right? So it should be
trivial to saturate the channel with 256 contexts.**"

**Why it matters, in one line.** `G = row_bytes × banks_per_channel × total_channels`
[CORRECTED — the third factor read `N`, the form E.3 marks BLOCKING and orders never
written; on both checked-in devices each Ramulator2 instance declares one channel, so
`total_channels` = `N` here and the arithmetic below is unchanged]. At four tiles on
the checked-in DDR5 file, 64 gives **G = 1 MiB / `grain_bits` 20** and 32 gives **G =
512 KiB / `grain_bits` 19**. This is the same number D.5 and ledger L20 spend a page on,
and it relays out every binary (E.3, "G IS NOT A CONSTANT").

**Chronology, which is the whole of the authority question.**

| # | when | what it says about banks |
|---|---|---|
| #53 | 2026-08-28T03:38:01Z | "I am fairly certain DDR5 **only has 32 banks**" — unit unstated |
| #55 | 2026-08-28T03:44:51Z | "**RANK BITS AND BANKS ARE MEANT TO BE INCLUDED**" — a rule for what goes into `G`, stated without a count |
| **#141** | **2026-08-29T05:16:38Z** | "We have only **32 banks per channel** right?" — **newest, and the only one that names a unit** |

**RULING — the two statements are not in conflict, and #55 is not overridden.**

1. **#55 is a rule about which fields count**, not a count: rank and bank bits are included
   in the span between the channel and the row. **That rule is untouched by #141** and
   remains the definition of `banks_per_channel` (E.4 above).
2. **#141 supplies a count for a specific machine, and asks for it** ("right?"). On the
   checked-in DDR5 file it is the **per-rank** figure: `tile_ddr5.yaml:33` is
   `count: [1, 2, 8, 4, 65536, 1024]` — 1 channel × **2 ranks** × 8 bankgroups × 4 banks =
   **64 flat banks per channel**, 32 per rank.
3. **The second rank is a DELIBERATE, non-JEDEC choice of ours** (E.3 above, DESIGN §17
   D:1389-1391: "The 1 MiB grain quoted in section 0 follows from `rank: 2`; the preset
   would give 512 KiB"). **On the JEDEC part the user was reasoning about, and on any
   one-rank configuration, 32 banks per channel is exactly right** — and #141's own
   subject, a channel-utilisation figure, is a per-device observation, not a change to the
   grain formula.
4. **So: the rule is #55's, the arithmetic is per-file, and both land on 64 for
   `tile_ddr5.yaml` only because we chose `rank: 2`.** `G` = 1 MiB at four tiles **on this
   file**; on the one-rank preset the same formula gives 512 KiB, and #141's 32 is then the
   per-channel count as written.

**`[RULED — user ruling 2026-09-02, the GEOMETRY ruling. The reconciliation above was
correct and has now been put to the user and confirmed.]`** "**Ranks are included. 32 banks
per channel assumes DDR5 and one rank. 32 banks per channel is not part of the design or
spec, that is part of the memory technology.**" So: **#55's rule is the design** (every
level between the channel and the row, ranks included); **#141's 32 was the device at one
rank**; and **neither number is a specification**. `G` is 1 MiB on `tile_ddr5.yaml` because
that file chooses `rank: 2`, 512 KiB on the one-rank preset, and something else again on
the next device — all three correct, none of them canon. **Ledger L8 is closed. Do not
quote any bank count without the file it came from, and do not put one in a design
sentence at all.**

**Never write "banks per channel" again without saying which.** Three different numbers are
live in this tree and all three are correct in their own frame: **32** per rank on DDR5,
**64** flat per channel on `tile_ddr5.yaml`, **32** flat per channel on the
`--dram default` controller (`1 × 8 × 4`, E.3's second table row) — where the user's #141
sentence is literally true of the machine and `G` still comes out 1 MiB because the row is
8 KiB. The bare phrase has already caused one silent halving of a channel count in this
document.

### E.5 THE BANK-SWEEP CAVEAT — state it exactly, and measure it rather than argue it

The formula's licensing argument is about **whole rows**, and the sweep property that
follows is narrower than it is usually stated. The claim to make is:

> **"A G-unit uses every bank of its channel evenly."**
> **NOT** "one row open in every bank."

**DEFINITION — a SWEEP, because this section's unit of account was previously used
undefined and both of its numbers were wrong as a result.**

> **A `sweep` is one contiguous pass over every flat bank of a channel, exactly once:
> `sweep = row_bytes × banks_per_channel`. It is a PROPERTY OF THE DEVICE and it is NOT
> a fixed 512 KiB.**

The mechanism (DESIGN §30.2 D:3435-3452): under `RoBaRaCoCh` the **rank bit sits below
the five bank bits**, so consecutive addresses walk every flat bank before repeating a
row.

**For the checked-in reference device** (`ramulator/tile_ddr5.yaml:33,36`,
`count: [1,2,8,4,65536,1024]`, `channel_width: 32` bits): `row_bytes = 1024 columns ×
4 B = 4096`; `banks_per_channel = 2 × 8 × 4 = 64` flat banks; **one sweep = 4096 × 64 =
256 KiB**. And since `G = row_bytes × banks_per_channel × total_channels` (E.3), the
identity that matters falls straight out:

```
G / sweep  =  total_channels  =  N        — for ANY device, at ANY tile count
                                            (the second equality holds because E.6
                                             pins channels_per_instance = 1; E.3)
```

**So at four tiles G = 1 MiB is FOUR sweeps, and every flat bank gets four rows.**

`[CORRECTION — an earlier revision of this section said "a contiguous 512 KiB sweeps
every flat bank exactly once" and "1 MiB at four tiles is two sweeps". Both numbers
belong to a device that is not the reference one.]` 512 KiB is a sweep only for an
**8 KiB-row** device — the retired DDR5 line E.3 supersedes — and "two sweeps" came from
dividing the 4 KiB-row device's grain (1 MiB) by the 8 KiB-row device's sweep (512 KiB).
**The two halves of that sentence were measured on different devices.** For the retired
8 KiB-row device the arithmetic is also four sweeps, not two, because G there is 2 MiB.

`[CORRECTION — and the odd-tile-count asymmetry DISAPPEARS under the definition above.]`
An earlier revision said "**at an odd tile count, G is 1.5 sweeps and a single G-unit
covers half the banks twice**". Since `G(N) = sweep × N` exactly, **G is a whole number of
sweeps at EVERY N** — G(3) = 768 KiB = 3 sweeps on the reference device. The 1.5 figure
requires an unstated second definition, *`sweep` := 512 KiB fixed*, which is what
Appendix 2's divergence **S18** presupposes ("`G = 256 KiB × ntiles` is not a whole number
of bank sweeps at 1 or 3 tiles"). **S18 is therefore a statement about SST's hardcoded
constant, not about this machine's geometry**, and it is re-tagged accordingly.

**What survives, and it is the part that mattered:** the sweep property is not a thing to
argue, it is a thing to measure. The implementation reports it on stderr rather than
hiding it, and **the statistic is the check**: a four-tile stress run reports
`nmfc_banks_never_accessed 0` with a bank access spread of **1.31**, and a 98.1% row
hit rate on channel 0 (DESIGN §31.4 D:3671-3676).

**The instrument that makes this checkable** is the `NMFCBankBalance` ramulator
controller plugin (`src/nmfc/nmfc_bank_balance.cc`), built precisely because "a tile
can be at 78% of its channel's peak bandwidth and still be leaving a great deal on the
floor if its requests pile onto a few banks: the aggregate looks respectable because
the busy banks are saturated, while the idle ones contribute nothing and never appear
in a bandwidth figure." It matters more here than in a conventional machine because
"the placement pass silos data at grain granularity, the allocator hands out grains in
groups, and the graph structures are grain-aligned and walked with regular strides.
**Every one of those is a chance for a stride to land on a subset of banks**, and the
symptom would be indistinguishable from 'the workload is just slow'."
*User #63, 2026-08-28T04:32:55Z, asked for exactly this:* "I would also ensure that
load balancing across the dram banks is happening, and that somehow requests aren't
all hammering a subset of the banks."

**AND THE MECHANISM HE ASKED FOR TO ACHIEVE IT, WHICH THIS DOCUMENT DROPPED ENTIRELY.**
[OMISSION CORRECTED — E.6 quoted only the second sentence of #58 (the "minimalist" half)
and dropped both the mechanism and the reason. `grep -i hash` over this document returned
only `hashed_perceptron` (the HOST branch predictor, D.5) and an SST TLB scar. The
requirement below is tier 1 and appeared nowhere.]

*User #58, 2026-08-28T04:03:24Z, verbatim and in full:*

> "Okay. Go for it. I think we should expect **low rowbuffer hit rates regardless**,
> unless you think there is substantial spatial locality in this graph workload? **I am
> more concerned with exploiting parallelism, which putting the bank bits lower and
> hashing them (as modern memory mappings do) will help.** A note: Instead of just
> hard-copying the Zen4 mapping, take a look at the minimalist mapping as well. That one
> is probably better, and **combined with the hashing should be flexible and convertible
> to nearly any dram geometry.**"

**Three separable requirements, and the first two are the ones that were lost:**
1. **Put the bank bits LOW in the physical address**, so consecutive addresses walk banks
   before repeating — which is precisely the property E.5's sweep argument depends on.
   **E.5 is built on a bank-sweep property that BANK-BIT PLACEMENT determines**, and this
   was the user's instruction for how to determine it.
2. **HASH the bank (and bankgroup) bits**, "as modern memory mappings do". The stated
   reason is **parallelism, not row-buffer hit rate** — the user says in the same breath
   to *expect* low row hit rates and not to chase them. **A mapping change argued from row
   hits is arguing from the metric he explicitly set aside.**
3. **Minimalist + hashing is the target because it is device-portable** — "flexible and
   convertible to nearly any dram geometry", which is the same property `G` needs (E.3's
   "G IS NOT A CONSTANT").

*And the immediately following challenge, user #59, 2026-08-28T04:05:27Z, verbatim:*
"**you understand how to hash the bank + bankgroup bits right?** I am worried you don't
know what you are doing." **The hash is over bank AND bankgroup bits together**, not over
the bank field alone.

*And the row-policy setting he asked for in the same exchange, user #64,
2026-08-28T04:34:05Z, verbatim:* "Yes, but since the base hit rate is so low, and
conflicts so high, maybe if you **switch it to a closedcap of 1 or 2, and set the gang to
that same size**, it might work better." **A closed-page cap of 1 or 2 with the gang set
to the same size.** [CORRECTED — this
was previously called "untried". It is HALF tried: `ramulator/tile_hbm3.yaml:23-24` carries
`row_policy: ClosedCAP` with **`cap: 4`**, and `gang_size: 4` below it, so the *gang matches
the cap* exactly as he instructed — but the cap is **4, not the 1 or 2 he named**. No DDR5
config carries a closed-page policy at all.]

`[CORRECTED — BLOCKING. This paragraph previously read "**Status: none of the three is
built**" and filed all three as open work. **All three are built, hashing is ON BY
DEFAULT, and a checked-in device file selects the mapper with both parameters set
explicitly.** E.6, nine lines further down, already said the mapper exists and that HBM3
selects it, so the document contradicted itself on the same page — and E.5 was the half
carrying the actionable verdict, so an engineer reading in order would have re-implemented
a built mechanism.]`

**STATUS: ALL THREE ARE BUILT, in `src/nmfc/nmfc_addr_mapper.cc`, as `NMFCMinimalist`.**

| # | the requirement | where it is built |
|---|---|---|
| 1 | **bank bits LOW** | `:20-25` states the bit order from the LSB — `gang of consecutive columns \| channel \| bankgroup \| bank \| rank \| remaining columns \| row` — implemented at `:134-141`. Bank and bankgroup sit **below the remaining column bits and below the row**, which is the property E.5's sweep argument needs. Its own comment gives the timing reason: "DDR5 charges `nCCDS` between bank groups against `nCCDL` within one ... **stepping to the next bank group is the cheapest move available and belongs lowest**. A rank change turns the data bus around and belongs highest." |
| 2 | **HASH the bank AND bankgroup bits together (#59)** | `:143-151`, folding row-bit parities into **both** indices: `bankgroup ^= taps(row, row_bits, 2 + bg, 5, 3) << bg;` and `bank ^= taps(row, row_bits, b, 5, 3) << b;`. **`hash_banks` defaults to `true`** (`:79` `bool m_hash_banks = true;`, `:91` `RAMULATOR_PARSE_PARAM(m_hash_banks, bool, "hash_banks").default_val(true)`). Sparse taps, three row bits per index bit, spaced so neighbouring index bits do not see the same row bits. |
| 3 | **MINIMALIST and device-portable** | `:20-21`: "**following the MINIMALIST mapper in ChampSimDevelop's ramulator2 clone rather than inventing one**". Device-agnostic by construction — `:101-107` finds the rank/bankgroup/bank levels **by name** in the device spec and sets each index to `-1` where the device has none; `:113` shifts past the channel because one instance per tile means the channel was already chosen. |

**#50's bijectivity requirement is built too, and it aborts rather than warns.**
`verify_bijective()` (`:174-202`) sweeps `groups × columns` addresses at setup and throws
"*address mapping is not one-to-one; two addresses claim one (bank, column)*" if any pair
is claimed twice — which is #50's "**We don't want to see any address aliasing,
unreachable areas of the address space, etc.**" `:50-52` also records that the reference
implementation's own hash **does nothing as written** (`gitBit` is called with its
arguments transposed, so it evaluates `(1 << row_bits) & tap` ≈ 0); **ours corrects the
argument order.** Do not "fix" it back toward the reference.

**A checked-in device file selects it with both parameters explicit.**
`config/nmfc/ramulator/tile_hbm3.yaml:26-29`:

```yaml
      addr_mapper:
        impl: NMFCMinimalist
        gang_size: 4
        hash_banks: true
```

**WHAT IS ACTUALLY OPEN — a much smaller list than "all of it":**
1. **No DDR5 config selects it.** `tile_ddr5.yaml:26` and all four
   `per_tile/tile_ddr5_tile*.yaml` still use stock `RoBaRaCoCh` — bank bits **above** the
   column bits, unhashed. **So the reference configuration does not run the mapping the
   user asked for, even though it exists.** That is a one-line change per file, not a
   build.
2. **#64's row policy is only half-tried.** He asked for "**a closedcap of 1 or 2, and set
   the gang to that same size**". `tile_hbm3.yaml:23-24` carries `row_policy: ClosedCAP`
   with **`cap: 4`** and `gang_size: 4` — the gang **does** match the cap, as instructed,
   but the cap is **4, not 1 or 2**. The value he named is untried.
3. **Nothing is measured.** No run in the record compares `NMFCMinimalist` against
   `RoBaRaCoCh` on the same device, so requirement 2's stated purpose — **parallelism, not
   row-hit rate** — has never been tested.

**It is NOT retired by #62** — see E.6, which closes the question of *chasing mappings as a
performance knob* and does not close the question of *building the mapping he asked for*.
Ledger **L39**.

### E.6 One ramulator2 instance per memory tile — the user's law

*User #49, 2026-08-28T03:11:15Z, all-caps in the original:*
1. "**EACH MEMORY CONTROLLER AND CHANNEL TO MEMORY IS A SEPARATE RAMULATOR2
   INSTANCE.**"
2. "**RAMULATOR2 CONTROLS THE MAPPING OF PHYSICAL ADDRESSES TO DRAM. BECAUSE ONE
   RAMULATOR2 INSTANCE EXISTS PER CHANNEL, RAMULATOR2 DOESN'T CONTROL THE CHANNEL
   MAPPING.**"
3. "**WITHIN A CHANNEL, THERE EXISTS A PHYSICAL ADDRESS THAT WILL BE USED TO SEND
   READ/WRITE REQUESTS TO THE DRAM. THIS COMES FROM CHAMPSIM. RAMULATOR2 NEEDS A WAY
   TO UNDERSTAND HOW IT SHOULD MAP THAT ADDRESS INTERNALLY TO ROWS, COLUMNS, RANKS,
   BANKS, and BANKGROUPS.**" And the preference, stated: keep the physical address
   **consistent within a tile** so ramulator2 can remain **unaware** of the two
   mapping modes. That is why the mode bit is stripped at the DRAM port (E.2).

*User #50, 2026-08-28T03:18:30Z:* geometry is **grabbed from ramulator2, never set
twice**; each instance declares **one channel internally** (more means subchannels,
which is unnecessary); **channel bits are removed from the physical address before it
is passed**, converting a global PA into a per-channel-local PA; and this is "a great
place to set up a unit test ... **We don't want to see any address aliasing,
unreachable areas of the address space, etc.**"

**DRAM address mapping is PROBABLY the wrong knob — CONDITIONALLY, on the user's own
hedge, which an earlier revision of this headline removed.**

`[CORRECTION — this section previously read "DRAM address mapping is the wrong knob. The
user tried it and closed it", hardening a hedged, outcome-conditional sentence into an
unconditional closure, and filed it in Part P under DO NOT REBUILD with a measurement the
user never gave.]` **#62, 2026-08-28T04:31:40Z, is two sentences and this is all of
them:**

> "**Regardless, it seems like messing with the mappings is the wrong knob. We can leave
> them as default if nothing comes of these last few experiments.**"

**Both qualifiers are load-bearing.** "*It seems like*" is a hedge, not a ruling. "*If
nothing comes of these last few experiments*" is an **outcome condition** on experiments
whose outcome this document does not record. **The correct statement is: leaving the
mappings at their defaults is licensed IF those experiments produced nothing; the user
did not rule the mapping question closed, and no measurement in #62 supports one.** (R73
in Part P appends "measured 6.7% row hits" to this row; that figure comes from
`src/nmfc/nmfc_addr_mapper.cc`, **not** from #62 — do not attribute it to him.)

**And do not read #62 as retiring #58.** #58 asks for a *specific mapping change* — bank
bits lower, hashed, minimalist — **for parallelism**; #62 sets aside *chasing mappings as
a performance knob*. E.5 records the first; this section records the second. **The canon
previously closed the mapping question while never recording the mapping change the user
actually asked for.**

ChampSim carries a `NMFCMinimalist` mapper, bijectivity-checked
at setup — **and it implements all three of #58's requirements, with hashing on by
default. E.5 now states that in full; read it there and do not re-derive it here.** **It
is selected by no DDR5 config** — `tile_ddr5.yaml:26` and all four
`per_tile/tile_ddr5_tile*.yaml` use `RoBaRaCoCh` — **but it IS selected by a checked-in
configuration:** `config/nmfc/ramulator/tile_hbm3.yaml:26-29` reads `addr_mapper: impl:
NMFCMinimalist`, `gang_size: 4`, `hash_banks: true` (with `row_policy: ClosedCAP`,
`cap: 4`, `dram impl: HBM3`). An earlier revision of this document said no checked-in
ramulator run used it, which is wrong for the HBM3 device.

**Do not re-open the mapping question AS A PERFORMANCE KNOB without a reason** — that is
what #62 sets aside. **Do build the bank-bit placement and hashing #58 asked for; that is
a different question — and IT IS ALREADY BUILT (E.5).** What remains open on it is only
**selecting it on DDR5, trying #64's cap of 1-2, and measuring any of it.** And **do know
which mapper the file you are running actually names.**

[EMPHASIS REPAIRED — this paragraph nested one bold run inside another. Markdown pairs the
markers sequentially, so the opener at "Do not" closed at the dash and the third marker, at
"it is BUILT", was left unmatched: **the emphasis landed on the "do not re-open" preamble
and came OFF the actionable verdict**, and the closing clause lost its marker entirely.
"It is BUILT" is precisely the fact E.5's own `[CORRECTED — BLOCKING]` block says an
engineer reading in order would otherwise miss, and would then "have re-implemented a
built mechanism". The sentence is now split so that no emphasis run contains another.]

---

## PART F — TRANSLATION AND PLACEMENT

### F.1 The order, and why it is that order

**Translate, then route.** Not the reverse.

The virtual address is translated to physical **before** anything crosses the fabric,
and the physical address names the tile. This is true on the dispatch path (the entry
PC is translated on dispatch, and the copy handed back chooses the tile) and on every
data access inside a function (the address is resolved, then routed, then accessed).
The reason is structural, not stylistic: **routing after translation means a tile must
translate an address before it can know whether it owns it.** That is precisely why
the page table must be resolvable locally for *foreign* addresses too, which is why
there is one table on duplicate pages (I3). Per-tile partitioned roots cannot do it —
discovering "this is not mine" would itself take a remote walk, which is the one thing
that must never happen (DESIGN §5.0.2 D:551-554).

**PRIOR ART FOR THIS DESIGN, which G.3 has for the NUCA policy and Part F did not have
for translation.** `[OMISSION CORRECTED — DESIGN §5.8 D:682-692 is a checked prior-art
table and no Part cited it. #29's rule — "several NUCA/NUMA policies already published
handle similar situations, so we don't need to work from scratch" — applies here too.]`

| work | what it supplies |
|---|---|
| **IMPICA** (arXiv 2012.03112) | In-DRAM pointer chasing with **no CPU TLB or walker**, a region-based page table, a decoupled address engine. **The closest ancestor**, and the reason `NMFC_FLAT_VMEM` exists as the IMPICA-style comparison (DESIGN §3 D:252). |
| **vPIM** (DAC'23) | Multi-stack PIM over a memory network, contention-aware hash page table, cores dedicated to pre-translation. **The closest topology.** |
| **Utopia** (MICRO'23) | The **restrictive/flexible mapping split** — which is exactly this design's VA→channel congruence at channel granularity against a flexible VA→frame choice. |
| **POM-TLB** (ISCA'17) | A very large in-memory TLB. Deferred; fits behind the same abstract base (F.10). |
| **Victima** (MICRO'23) | Repurposes L2 blocks for TLB entry clusters. Deferred. |
| **FlexPointer**, RMM, Direct Segments | Range-based translation. |
| **Neighborhood-aware** (MICRO'18) | Irregular GPU workloads, the same locality problem. |

**Read this before proposing a translation mechanism**, the way Part P is read before
proposing anything else. (DESIGN §5.8 D:682-692.)

### F.2 vtile — a LABEL naming a coherent set, NEVER a location

This is the single most-dropped fact in the record. State it four ways so it cannot be
lost:

1. **A vtile is a compiled-in label** that tells the OS which virtual pages belong to
   the same **coherent set**. A vtile can be associated with grain page(s).
2. **It is a *relation*, not a location.** It says "these pages belong together". It
   does **not** say "this page goes on tile 3". A program that writes a tile number is
   violating I12 and I4.
3. **Same vtile ⇒ co-located, wherever they sit.** Different vtiles are **unrelated** —
   vtile 1 and vtile 5 have nothing to do with each other — and are therefore spread
   to balance load, unless that vtile already has a home, in which case its later
   pages follow it there.
4. **The vtile replaces every alignment trick.** "with hints, **grain-alignment only
   saves space**, you can still indicate 'both grains should end up on tile N' by
   hinting them with the same vtile" (#277). Nothing has to be adjacent, aligned or
   contiguous for two things to land together, and a linker script written to force
   grain alignment for co-location is doing a job the hint already does.

**The compiler's complete lever set is two items: page TYPE and vtile LABEL. Never
which tile** (DESIGN §29 D:3301-3303).

**And the contract those two levers sit in, in the user's own newest words on it — #291,
2026-09-02T12:37:03Z:** "**the way the memory is sliced up is entirely up to the
compiler's hints and how the local va↔pa mapping happens in sst. If either disrespects
the other, we will see issues.**" Two halves, and **neither is sufficient alone**: the
compiler emits page types and vtiles; the OS/allocator honours them when it chooses
frames. **The named failure mode is the two disagreeing** — hints that the mapping
ignores, or a mapping that assumes a layout the hints did not request. That is a
*contract to test*, not a division of labour to assume; a test that only checks one side
will pass while the machine is misplacing everything. (This supersedes nothing; it is
newer and more specific than the DESIGN §29 statement above and than #119 below.) The compiler is not aware of tiles; it *may* be
aware of batches — "Just like a compiler understands pages, and assigns virtual
addresses, it doesn't need to understand DRAM or how virtual address map to physical
addresses" (#119, 2026-08-29T01:59:43Z).

### F.3 VIRTUAL-ADDRESS PARTITIONING — REJECTED, with all four reasons

*User #269, 2026-09-01T20:11:19Z.* Partitioning tiles by virtual address:

1. **leaks hardware-specific details into the virtual address space**;
2. **exposes the tile layout directly**;
3. **confines the compiler to a fixed mapping** — "the compiler can only offer a fixed
   mapping";
4. **lets programs manipulate placement in ways unfriendly to shared systems.**

And the positive case for physical: translation is **unavoidable anyway** (functions
operate in the virtual address space); **dispatch happens once per function, so the
cost is amortised**; it simply moves the **migration trigger to after translation**
rather than before it; **co-location hints stop being tied to particular virtual
address ranges**; and **nothing requires a direct-mapped VA↔PA space**.

**Everything that follows from the rejection, and must be deleted on sight:**
`tile_of(virtual_address)` as a router; `VIRTUAL_FIRST` / `TRANSLATE_FIRST` as two
live arrangements; per-channel / per-slice page-table roots; "the compiler places data
by choosing virtual addresses"; a hint whose payload is a tile number.

`[CONFLICT — BLOCKING. FOUR of those five are built, and they are how every grain in
every result in this tree is placed. Read A.4a before acting on this list.]`
`nuca_router.cc:110` and `physical_router.cc:88` are both `return
map_.tile_of_virtual(vaddr);` — item 1, by name. `nmfc::placement_hint{mode, fields.tile,
replicated}` (`nmfc_producer.cc:271`) is item 5, by name. `annotate.cc:111-141` rotates
every region's base until its grain index is congruent mod `tiles` — item 4, by name.
`VIRTUAL_FIRST` and `TRANSLATE_FIRST` are both live and both selected by shipped configs
(4 and 22 files) — **item 2**. **Only the page-table roots — item 3 — are actually gone
(F.5).** [CORRECTED — this read "item 3" for the two arrangements and then, in the next
sentence, "only the page-table roots are actually gone", which is ALSO item 3. The same id
was declared both built and gone four lines apart and an implementer acting on either
statement was acting on the wrong one. The list is: **1** `tile_of(virtual_address)` as a
router; **2** `VIRTUAL_FIRST` / `TRANSLATE_FIRST` as two live arrangements; **3**
per-channel / per-slice page-table roots; **4** the compiler placing data by choosing
virtual addresses; **5** a hint whose payload is a tile number. Built: 1, 2, 4, 5. Gone: 3.]
**Do not act on this paragraph by deleting code.** **And deletion is not on the table in
any case** — user ruling 2026-09-02 R2, "*relabel is fine*", is the standing instruction for
every mechanism in this class (F.10). Ledger **L38**.

**THE UNHINTED-GRAIN HALF OF THIS IS RULED, AND IT NARROWS THE DELETE-ON-SIGHT LIST.**
`[RULED — user ruling 2026-09-03 **O1**, verbatim: "**Unhinted grains are up to the
OS/hardware to place. So, presumably the OS could map it wherever was most convenient.**"]`

**The list above still stands in full as a list of ARCHITECTURAL constructs**, and every
one of the five is still rejected at tier 1 by #269. What O1 changes is the reading of item
**1** in one specific case, and the distinction is the whole of it:

| the mechanism | status |
|---|---|
| **`tile_of(virtual_address)` as a ROUTER** — the machine deriving a tile from a VA and depending on the answer | **REJECTED, unchanged.** #269, all four reasons. This is the architecture reading the virtual address. |
| **`(va >> grain_bits) % num_tiles` as one convenient DEFAULT an allocator may use for an unhinted grain** | **PERMITTED — O1.** The OS may "*map it wherever was most convenient*", and this is arithmetically the cheapest convenience available. **It is not a partition, it carries no semantics, and nothing may rely on it.** |

**The two look identical in a diff and are opposite in kind.** The test is *who reads the
virtual address and what depends on the answer*: an allocator choosing a frame reads it
once and nothing downstream may depend on the choice; a router reads it on every access and
**everything** depends on the answer. **The first is convenience; the second is the
partition #269 rejected.** F.8 carries the full statement and the prohibition that comes
with it.

**The surviving remnant is arithmetic only.** `compact` / `expand` — removing the
tile-select field on the way into a slice and re-inserting it on the way out — is
about **slice indexing** and has nothing to do with routing. It survives. So does the
grain-size derivation.

### F.4 IDENTITY / DIRECT MAPPING — REJECTED, with the reason

*User #6, 2026-08-27T05:59:37Z, declining the question entirely:* "**For this
architecture to work, the standard cores and nmfc need a unified address space. In
addition, we are talking about massive graphs here, having a direct-mapped space is
probably a deal-breaker.**"

Two reasons, both load-bearing: **(a)** the host cores and the function cores must
share **one unified virtual address space**, so that passing inputs to functions and
traversing graphs does not require a hard partition of the memory space — "a deal
breaker if you ask me" (#7, 2026-08-27T06:13:16Z); **(b)** at graph scale a
direct-mapped space does not fit.

**What was accepted instead: a unified virtual address space with real translation.**
And the corollary, from the same message: "We want to keep the execution space
virtual, otherwise passing inputs to functions, traversing graphs, etc.... requires a
hard partition of the memory space."

Closely related and also rejected: **fixed address spaces / apertures** as a placement
mechanism. *User #20, 2026-08-27T17:45:37Z:* "Woah woah woah. What is `aperture_bytes_`
doing? **I thought we rejected the idea of fixed address spaces?**"

### F.5 N PAGE-TABLE ROOTS — NOT AVAILABLE, and the authority for saying so

**The canon, under user ruling 2026-09-02 R12: there is exactly ONE PAGE TABLE PER
ADDRESS SPACE, and each address space's table lives on duplicate pages — one copy on every
tile. TLBs are SHARED and ASID-tagged.** [SHARPENED — this line previously read "there is
exactly one page table, and it lives on duplicate pages", which is the reading **I3**
explicitly kills: **"one page table" never meant one for the machine**. The section title's
"N page-table roots" means N roots **per tile**, which is the branch that stays rejected;
N roots **per address space** is what R12 requires. The R12 block below and **F.5a** carry
the full statement.]

**Why the alternative is not available, argued from the NEWEST statement rather than an
older one.** An independent page-table root per tile **requires virtual-address
partitioning**. That is not this document's inference; it is #269's own parenthesis:
"an independent page-table root per tile (**requires virtual-address-partitioning out of
necessity, exposing us to the undesireable virtual-address-routing trap**)". And #269 is
the message that rejects virtual-address partitioning outright ("Physical-address
partitioning is all-around better"). **A branch that requires a rejected mechanism is not
a live option.** Reinforced by the newer #283, 2026-09-02T02:18:18Z, which requires
translation to be possible locally with "No crossing of the fabric, no migration" — which
one duplicated table satisfies. (Structural corroboration at DESIGN §5.0.2 D:542-549,
tier 3: under routing-after-translation, discovering "this is not mine" would itself need
a remote walk.)

`[AUTHORITY CORRECTION — this section previously rested on #265, which is 46 minutes
OLDER than #269.]` *#265, 2026-09-01T19:25:50Z, in full:* "You are citing old outdated
design spec. I would appreciate you actually reading the design documents. **I can tell
because you mentioned per-slice page table roots, completely outdated.**" That is a real
tier-1 statement and it points the same way — **but it cannot be the authority that
retires a framing #269 then re-raised as one of two approaches.** #307 admits no
exceptions: newer overrides older. The ruling above is therefore built from #269 and
#283, and #265 is corroboration.

`[CLOSED — and by the user himself. #269, 2026-09-01T20:11:19Z, ended "**I bring up all
these points because I am not sure what the right final surface is.**" That sentence IS now
retracted, by a newer one.]` **User ruling 2026-09-02 R12:** "*I am fairly certain real
machines have separate page tables per address space? TLBs are shared, page tables
themselves should not be shared between address spaces?*" **The surface is: ONE page table
per address space, duplicated on every tile, with shared ASID-tagged TLBs.** That is the
branch the rejections left standing, it is what the invariants require, and it is now
stated on the user's own authority. Ledger **L35**, RULED R12. See **I3** and **F.5a**.

**Historical note, so the phrase is recognised where it survives.** Early in the
project the user did float the N-roots idea (#7, 2026-08-27T06:13:16Z: "Duplicating
these tables per-channel is fine, but note the address space is already by-necessity
partitioned -> there is no overhead here, we just have N (one per channel) roots
instead of 1"; and #32, 2026-08-27T20:26:07Z, asking whether the table should be
duplicated per channel or the machine should rely on TLBs). **Newest wins**: #265 and
#269 retire it. Any artefact still advertising "N page-table roots" — including
`MEMORY.md:2` and `nmfc/.claude/nmfc_invariants.sh:19-22` — is stale and is actively
re-teaching the rejected design. See ledger L2.

### F.5a The page table's FORMAT — standard, and DERIVED from that

`[RULED — user ruling 2026-09-02 R13, verbatim: "I thought the mode bit was encoded in the
page table itself, carried as an extra bit on any physical address? … **5-level page tables
are common. PTE layout should inevitably be derived from that. Multi-size pages are already
supported in modern hardware, this shouldn't be an open implementation question.**" The
question this closes had asked the document to invent a PTE. It does not need one.]`

**The translation structure is STANDARD, and the design's novelty is not here.** Stating it
in four clauses, because an implementer reading "specify the PTE" would otherwise think
something had to be designed:

1. **Five levels.** A 5-level radix page table, as modern 64-bit hardware already has, over
   a 48-bit physical address space (the geometry ruling, I12). Tier 2 already declares it:
   `page_table_levels: 5` and `page_table_page_size: 4Ki` in every config that instantiates
   `NMFC_VMEM`.
2. **The PTE layout is DERIVED from that**, not invented here. It is the ordinary shape —
   a physical frame number plus permission, validity, accessed/dirty and page-size bits —
   and any question of the form "what are the fields" is answered by the standard 5-level
   format, not by this document.
3. **Multiple page sizes, as modern hardware already supports them.** A large-page
   terminator at an upper level is how a walk ends early; that is how huge pages already
   work and it is why F.7's TLB probes **two arrays in parallel** — a 4 KiB array and a
   `G`-sized array. **`G` is not 2 MiB or any other fixed size** (E.3), so "the huge page
   size" is a device-derived quantity, not a constant.
4. **The mode bit lives IN THE PTE and rides on the physical address.** The bit that says
   whether a frame is NMFC-mode (grain-silo'd) or STANDARD-mode (block-spread) is **stored
   in the page-table entry** and is **carried as an extra bit on any physical address the
   translation produces**. It is stamped at allocation and never changes (E.2). It survives
   `compact`/`expand` — which is exactly why those two are mode-correct without carrying
   the mode separately (C.2) — and it is **stripped at the DRAM port and nowhere earlier**,
   because a device decoder would otherwise collapse both modes onto the same row.

**One table per address space** (I3, R12), so the ASID selects the table and is part of
every translation, every remap and every shootdown; the **TLB is shared and ASID-tagged**.
`N+1` copies of a given address space's table must agree and be built from one parameter
dict — a component given no `memSize` once derived a different remap budget from every
component that had one, which is why `memSize` is fatal at construction now (F.8, DESIGN
§30.3 D:3507-3516).

**What is NOT settled here, and it is not a format question:** nothing. The old ruling
question asking for a PTE specification is closed by R13.

### F.6 Walks must remain local

*User #269:* "**Regardless, walks must remain local**, which helps both with TLB
locality, reducing migration, and handling things like page-table-walks in hardware
where we don't want to pass control back to the kernel unless we see an actual fault."

Three things that buys: **TLB locality**; **translation never causes a migration**;
**the walker runs in hardware** and never hands control back to the kernel except on a
real fault. And the hard consequence, from #291: **a migration caused by instruction
fetch or by translation is by construction wrong.**

*User #283, 2026-09-02T02:18:18Z, restating it as a correction:* "translations must
NOT be foreign. As I have repeated, continuously. As all our design documentation
says, repeatedly. **Translation must be possible LOCALLY, on the same memory TILE. No
crossing of the fabric, no migration.**"

**Routing page-table walks over the fabric is a REJECTED fix.** "A single table on one
channel reached over the fabric is a bug, **and routing the walks is not the fix**"
(I3). The mechanism that makes locality true is the duplicate page type — the same one
function instructions use. There is **no new mechanism** here: "carving pages out of a
replica set is the whole of it" (`src/nmfc/nmfc_vmem.cc:438-460`).

**Historical note, so the phrase is recognised where it survives — and so that a reader
grepping the log for "walk" does not find an unretracted licence for the mechanism this
section forbids.** [ADDED — the tier-1 statement that ORIGINATED fabric-routed walks is
the user's own, and an earlier revision of this document cited it nowhere
(`grep -c "#33\b"` returned 0), presenting the mechanism throughout as ChampSim's
invention. A rejection with no supersession note reads, to the next reader, as the canon
overruling the user without saying so.]

*User #33, 2026-08-27T21:26:37Z, verbatim:* "Okay. One note, **the nmfcs are right next to
the fabric, so it is clear that if they need to walk somewhere else, they already have a
fabric that can serve that request.** Please proceed with the implementation"

**That sentence is where `--walk-routing fabric` comes from, and it is a licence, not an
inference.** `src/nmfc/physical_router.cc:20-23` restates it almost word for word — "Walk
references therefore go wherever the PTE lives, **over the memory network the tiles already
sit on**" — so the code is doing what it was told, at the time it was told.

**Newest wins (#307).** #33 is **superseded** by #283 (2026-09-02T02:18:18Z, six days
newer: "Translation must be possible **LOCALLY**, on the same memory TILE. **No crossing of
the fabric**, no migration") and by #269 ("**Regardless, walks must remain local**") and
#291. The observation in #33 remains true — the fabric *can* serve such a request — but
**"can" was retired as "should".** Do not resurrect the mechanism by re-finding #33.

`[CONFLICT — ChampSim ships the rejected fix, and EVERY single-root config uses it.
BLOCKING.]` `config/nmfc/make_config.py:578-579` offers
`--walk-routing {local,fabric}`, help text: "page-table walks stay on the tile
(partitioned table) or **route over the fabric (shared table)**".
`src/nmfc/physical_router.cc:20-23` states the design it implements: "The page table
collapses to one root ... **Walk references therefore go wherever the PTE lives, over the
memory network the tiles already sit on (`--walk-routing fabric`).**"

[CORRECTED — the count in this paragraph was 13 and had propagated to eight other places
in this document, including invariant 5, J.4, N.6 and ledger L26/L37. **13 is the
`first_touch` census** (A.4), not the walk-routing one; the two sets are different files.
Verified by `grep -l` over all 33 configs.]

**[ADDED — WHAT A `TILE_PORT` IS, AND WHAT `strict_locality` ASSERTS. These two
identifiers are named seven times across this document — in the front matter's ruling
questions, in invariant 5's legitimacy clause, here, and in J.4 — and were **defined
nowhere**. They are the SOLE enforcement mechanism for the legitimacy half of #291, so
ledger L26/L37 (both now **frozen** under R3) turn on installing something the document never
described. Everything below is read off `src/nmfc/tile_port.cc`; nothing is invented.]**

A **`TILE_PORT` is a memory tile's own port into its own LLC slice.** It exists because
the two paths into a slice speak different address spaces and must be made to agree:
a host reaches a slice through `INTERLEAVE_FABRIC`, which **compacts** the tile-select
field out of the address (C.2's figure) so the slice indexes a dense space; a function
core, MMU or cache sitting on that same tile reaches the slice **directly** and would
otherwise present an uncompacted address, so the two paths would tag the same line
differently and never see each other's data. The port is the adapter: it applies
`compact()` on the way down and `expand()` on the way back up
(`tile_port.cc:1-9, 85, 105`).

**`strict_locality` is the assertion bolted to that adapter, and it is a hard one.**
Every address crossing the port must belong to *this* tile. On each request the port
evaluates `map_.tile_of(address) != tile_` and, if so, increments a `FOREIGN ADDRESSES`
counter; **when `strict_locality` is set — and it defaults to true — it then prints
`ERROR: address … belongs to tile T, not tile t. A context reached memory without
migrating first.` and calls `std::exit(-1)`** (`tile_port.cc:152-168`, with the counter
reported at `:134` and `:139`). Cleared, it counts and continues.

**Why this is the enforcement mechanism and not merely a debug aid:** a reference that
crosses a tile boundary without a migration is exactly the failure invariant 5's
legitimacy clause forbids, and it is invisible in aggregate statistics — it looks like a
slightly slower run. Put the port on the **MMU** and it catches a **page-walk** reference
that leaves the tile, which is the specific thing #291 calls "*by construction wrong*".
**That is why omitting the port is not a configuration detail: it removes the only check
that the walk stayed home.** `make_config.py:271` emits it for the function-core path and
`:274`/`:331` set `strict_locality: True`; `make_config.py:265-267` gates the **`mmu`**
one behind `--walk-routing local`, which is the omission the census below counts.

**THE CENSUS, stated once, in the only form that is safe to quote — three disjoint groups
summing to 33:**

| group | how to test it | count | which |
|---|---|---|---|
| **`--walk-routing fabric`** — a per-MMU `INTERLEAVE_FABRIC`, **no `mmu` `TILE_PORT`** | `grep -l mmu_fabric` | **15** | `adapt/nmfc_4tile`, `nuca/nmfc_4tile`, `nuca_ft/{fast_epoch,nmfc_4tile}`, `phys/nmfc_4tile`, `phys_ft/{bw4,bw8,ftu2048,ftu4096,nmfc_4tile,q128,q2048,q512}`, `ram/nmfc_4tile_ramulator`, `wide/nmfc_4tile` |
| **`--walk-routing local`** — an `mmu` `TILE_PORT` with `strict_locality` | `grep -l mmu_port` | **3** | `nmfc_4tile`, `cap/nmfc_4tile`, `ft/nmfc_4tile` |
| **no NMFC MMU at all** — the question does not arise | `grep -L '"tile0_mmu"'` | **15** | the seven `baseline_*` files and the eight other non-MMU configs |

**So: 15 configs select `--walk-routing fabric`, and they are the 15 files that
instantiate a per-tile MMU alongside a one-root router.** E.g. `phys_ft/nmfc_4tile.json`'s
`tile0_mmu_fabric` spans `[@tile0_LLC_mmu0_channel … @tile3_LLC_mmu0_channel]`, and
`tile0_mmu`'s own comment reads "Walks route over the fabric, because a shared page table
puts a PTE on whichever tile its own address names."

**Two numbers that are NOT this one, and that an earlier revision confused with it:**
- **22** = files carrying `PHYSICAL_ROUTER` (15), `NUCA_ROUTER` (5) or `ADAPTIVE_ROUTER`
  (2) — ledger L4's census. **The seven that are not in the 15 are the `baseline_*`
  files, which carry a router but instantiate no NMFC MMU**, so the old quantifier
  ("*every* config carrying one of the three routers") was false as well as miscounted.
- **13** = `grep -l first_touch` (A.4). `ft/nmfc_4tile` is in the 13 and has **no MMU
  fabric at all**; `adapt/`, `nuca/` and `phys/nmfc_4tile` are in the 15 and **not** in
  the 13. **An audit driven by the wrong number opens the wrong files.**

Two consequences that matter more than the mechanism's existence:
1. **Under `--walk-routing fabric`, `make_config.py:265-267` skips the `("mmu", …)`
   `TILE_PORT` entirely** (`if args.mmu and args.walk_routing == "local"`), so
   **`strict_locality` never checks a walk reference** — the assertion that would catch a
   foreign walk is not installed in exactly the configurations that make foreign walks
   possible. That is the same failure shape as the congruence assertion that "had never
   executed once" (I9).
2. **No shipped config combines a one-root router with `--walk-routing local`**, so
   `take_pt_replica_page()` — the duplicated-replica-set mechanism this section cites as
   ChampSim's implementation — **is never exercised in any checked-in configuration.**

**Tier 1 wins: walks are local, over a duplicated table. `--walk-routing fabric` is the
rejected design, shipped and defaulted-into.** Ledger **L26**.

### F.7 Translation caching, and what migrates

Three tiers, and only the first two are per-context:

| tier | what | on migration |
|---|---|---|
| per-context translation slots | a few entries held inside the context itself — **"1 code + a few data" (C.2). The entry count and width are CONFIGURATION (user ruling 2026-09-02 R6–R10); the slots are NOT part of the 512-bit architectural context and NOT part of the migration payload** | **dropped** |
| tile TLB | two arrays probed in parallel: small (4 KiB) and huge (G) | stays with the tile |
| the walk | into the local copy of the one page table | stays with the tile |

`[UNRESOLVED — flagged, not decided. THREE STATEMENTS IN THIS DOCUMENT SIZE A CONTEXT AND
THEY DO NOT AGREE ABOUT THIS ROW.]` H.3's heading is "**Context state — no stack, 512
bits, and that is all**", and its body defines a context as "*a PC into its body plus at
most one cache block of registers*". J.1 fixes the migration payload at **exactly 72
bytes** — 64 of register file plus an 8-byte PC — and says translations "do not travel".
H.2's per-context state budget is **~87 B = 64 regfile + ~13 instruction buffer + ~10 data
buffer** (DESIGN §25.7 D:2569-2573) and **has no line for translation slots at all**.
**An implementer sizing per-context SRAM therefore has three incompatible answers and no
entry count.** The three are consistent under exactly one reading — that the slots are
per-context *microarchitectural* state which is neither part of the 512-bit
**architectural** context nor part of the migration payload, which is why H.3 and J.1 can
both be exactly right while H.2's 87 B is simply incomplete — **but no tier-1, tier-2 or
tier-3 source states that**, so it is recorded here as the likely reading and NOT written
as canon.

`[CLOSED IN THIS REVISION, and it needed no new ruling. Two things dispose of it.]`
**(1)** The **entry count and width are configuration**, not design — user ruling
2026-09-02 R6–R10, "*values … should not be fixed parts of the design*"; see SELECTED
CONFIGURATION. **(2)** The three statements are then not in conflict at all, because
tier 1 already says which budget the slots sit in: **F.7 says they are DROPPED on
migration** and **J.1 fixes the payload at exactly 72 bytes**, so they cannot be inside
either the 512-bit architectural context or the migration payload. **The reading above is
therefore the canon**: per-context translation slots are microarchitectural state outside
both. **H.2's ~87 B budget is incomplete** and must not be quoted as the whole per-context
cost, and a slot count must not be inferred from the 512-bit figure.

**Translations are dropped on migration, and the reason is structural, not
staleness.** A cached `va → pa` entry is only *usable* on the tile that owns `va`, and
after migrating every address the context is about to touch belongs to the new tile by
construction. So every carried entry is provably invalid. **There is no
`carry_translations` knob: building a switch for a provably-useless option is
clutter.**

*User #9, 2026-08-27T06:33:44Z:* "For designs where we store
translation data with a given context, **it does not need migrated. The translation
data is useless if migrated, so it shouldn't be migrated.**"

`[SUPERSESSION, stated because F.10 quotes the other side of it.]` **#9 reverses #8**
(2026-08-27T06:19:21Z), fourteen minutes earlier, whose AskUserQuestion answer put the
per-context translation cache "**In the context, migrates with it**". **Newer wins
(#307): translations are dropped.** F.10 carries #8 for its *swappable
`translation_engine`* requirement only, and now says so. R14 rejects the
`carry_translations` knob on the same authority.

`[AND §7.1 GAVE A SECOND REASON, WHICH THIS DOCUMENT DROPPED RATHER THAN RETIRED.]`
DESIGN §7.1 D:931 continues: "*The **code** entry is worse than unusable — the context's
instruction VA literally **changes** on migration, because it now runs copy `t'` at
`entry_pc_base + t' · G`.*" **That reason is DEAD, and dropping it silently weakened the
justification instead of correcting it.** Under the current layout the code entry is one
virtual page aliased to N frames, so **the instruction VA does not change** and a cached
code translation is invalid for the ordinary reason — the frame it names belongs to the
old tile — not for a special one. **The FIRST reason (every carried entry is provably
invalid, because every address the context is about to touch belongs to the new tile)
carries the whole argument on its own, and it is unaffected.** See H.8 and R110.

What replaces the knob is a **statistic**: translation cold-start cycles after
migration, counted separately from the fabric hop. Measured at **2.2–2.3 cycles** mean
cold start with a 100% instruction-cache hit rate (DESIGN §21.2 D:1850-1856).

**Mixed page sizes are required.** *User #10, 2026-08-27T06:41:05Z:* "**I think mixed
sizes are the way to go. It won't be possible to assess the translation system
realistically without them** (real systems still realistically need 4 KiB pages, so
it's not like we can fully switch everything over)." NMFC data uses G-sized pages;
everything else keeps 4 KiB. A machine that quietly made everything huge would flatter
itself.

**The page size must follow the page type.** *User #295, 2026-09-02T13:33:46Z:* "the
actual page size must be correct for the given page type. **Mapping everything to
4 KiB will break**, or at the very least incur significant translation overheads +
require the OS to make sure and reserve physical frames to ensure multiple 4 KiB virt
allocations land next to each other."

**Two-array probing is not an optimisation, it is a requirement.** A TLB is a cache
with fixed offset bits, so one array cannot hold both page sizes — real hardware
probes two in parallel for that reason. And a stock page-table walker that
unconditionally decrements the level cannot terminate early, so it cannot express a
huge page at all. Both are why `NMFC_MMU` exists as one module rather than as forks of
the walker and the cache (DESIGN §6 D:878-901).

**Huge-vs-small is decided from the placement HINT, not from the allocation.** A frame
does not exist on first touch, so asking whether one does answers "no" for every page
the first time it is seen — which walks it as a small page, fills the small array with
an entry nothing will reuse, then walks it again as a huge one on the very next
access, **two walks per grain permanently**. This was a real, measured defect
(`inc/nmfc/nmfc_vmem.h:102-112`).

**AND A RETRACTION THAT MUST TRAVEL WITH THIS SECTION, because a reader otherwise
re-derives a cost that does not exist.** `[OMISSION CORRECTED — DESIGN §27.2 D:3116-3150
is titled "Retracted: translation was not expensive, the TLB was unusable" and was never
cited here, so this document carried the mechanism without its most important measured
result.]` Two separate bugs made translation look ruinous — **95% of arrivals taking a
cold walk, page-table traffic at 0.61 references per instruction** — and both were
artefacts:
1. **The page size did not follow the page type.** `pageOf()` keyed grain and duplicate
   regions at `G` but keyed **REGULAR** pages — which are NMFC-mode — at 4 KiB, throwing
   away the reach the huge page exists to provide and silently demanding 256 adjacent
   4 KiB frames per silo. **That is the contiguity requirement the grain page was
   introduced to remove.**
2. **The TLB was indexed by the low bits of a sparse key.** A region-typed page is tagged
   in bits 40 and up, so `page % entries` was **zero for every grain and duplicate page in
   the machine** — all in slot 0, evicting each other on every fetch/data alternation.
   **64 entries and 4,096 entries gave bit-identical results**, because a larger table
   indexed the same way is the same table. It is a hash now.

| same graph, four tiles | before | after |
|---|---:|---:|
| walks | 14,260 | **12** |
| TLB hits | 66 | 14,320 |
| page-table references | 42,780 | **36** |

**On the large graph a ten-million-instruction run does 38 to 51 walks in total.**
**Translation is not a cost worth reporting on this workload**; it was a bug worth
finding, and invariant 11's "arrival is not costly" stands. (DESIGN §27.2 D:3140-3150.)
`[CAUTION — this is a statement about a workload with excellent grain-page reach, not a
general one. The cost that matters for the migration budget is the walk a context pays
AFTER it arrives, which F.7 drops its translations for; §26.3's "the walk issues
references" and divergence S39 are the live concern there, not this retraction.]`

### F.8 Remap and shootdown

Placement is not frozen at first touch: the OS may **remap** a grain. That is what a
NUCA policy actually does (Part G).

**A REMAP IS A NORMAL TLB SHOOTDOWN. THAT IS THE WHOLE MECHANISM.**
`[CONFIRMED — user ruling 2026-09-02 R17, "Okay, sounds correct."]` When the OS changes a
mapping it must invalidate the cached translations of that mapping, **exactly as any
machine does on any remap**: the shared ASID-tagged TLB, the per-context translation slots,
and every tile's copy of that address space's table. There is nothing NMFC-specific about
it, and nothing here to design.

**The generation counter and the per-grain log are the SIMULATOR'S CHEAP MODEL of that
shootdown — they are not hardware.** A generation counter is bumped whenever a mapping
changes; anything holding a `va → pa` answer compares it against what it last saw and reads
a per-grain log to find out which grains actually moved. **That exists because the
alternative — invalidating everything on any remap — is a model so coarse that it dominates
the very policy it prices: measured, it doubled the runtime for a 2% change in migrations**
(`inc/nmfc/nmfc_vmem.h:64-74`). **A reader must not carry the counter or the log into a
hardware specification.** The log's size and retention are **configuration**, and a
consumer that falls behind it takes the ordinary conservative answer — flush everything it
cached for that address space, which is what a real machine does when it cannot enumerate.
Ledger note: this closes the shootdown question.

**A frame is *chosen*, not moved — AT INITIAL PLACEMENT.** *User #274,
2026-09-01T20:40:26Z:* "**What do you mean the frame has to move? That doesn't make any
sense.**" Congruence is maintained by the placement pass choosing which frame to hand
back; it is not arithmetic on the virtual address and it is not a first-touch counter.

**AND WHAT THE PLACEMENT PASS MAY DO WITH AN UNHINTED GRAIN IS RULED.**
`[RULED — user ruling 2026-09-03 **O1**, verbatim: "**Unhinted grains are up to the
OS/hardware to place. So, presumably the OS could map it wherever was most convenient.**"
Ledger **L38** is closed; A.4a and F.3 carry the same statement.]`

**An unhinted grain's placement is the address-space owner's FREE CHOICE — allocator
convenience, and nothing else.** The OS may map it wherever suits it: the head of a free
list, a large contiguous run it is already holding, a tile it is balancing toward, or —
because it is arithmetically cheap and needs no state — `(va >> grain_bits) % num_tiles`.
**All of those are permitted and none of them is a rule.**

**THE PART THAT MATTERS, AND IT IS A PROHIBITION: no partition semantics attach to an
unhinted grain's virtual address.** Nothing downstream may *rely* on where an unhinted
grain landed, may *derive* a tile from its VA, or may treat a VA-derived placement as a
guarantee it can compute against. **A convenience an allocator happens to use is not an
architectural partition**, and #269's rejection of virtual-address partitioning
(F.3) is entirely untouched by this ruling — that rejection is about **the architecture
reading the VA**, which stays forbidden; O1 is about **an allocator picking a convenient
frame**, which was never the same thing. **The test that separates them: if the mechanism
would break when the OS chose differently, it is relying on a partition and it is wrong.**

**A REMAP IS THE OPPOSITE CASE, AND IT MOVES THE DATA.** #274 is about *initial*
congruent placement, where nothing exists yet to move. A remap changes the mapping of a
grain that already holds live data, and **the data must be copied or the program is
corrupted.** DESIGN §28.2 D:3232-3237, which this section previously omitted in its
entirety:

> "**A remap moves the data.** ... `nmfc_vmem.cc` changes `nmfc_grains_[key]` and is done,
> because nothing there reads page contents. **The tiles here execute against real
> memory, so a mapping change without a copy is corruption.** The fabric copies the grain
> — **`nucaCopyBytes` is a megabyte read and a megabyte written per move** — and only
> then broadcasts. **That cost is why the gates matter rather than being decoration.**"

**So: 1 MiB read + 1 MiB written per grain moved, at the settled `G`.** That is the price
tag on G.4's rules. **A policy built from the rules without the price reads them as
conservatism; they are arithmetic.** (SST charges these bytes on both the read and the
write, so its reported figure is 2×G per move — divergence S23.)

**A MOVE MUST REACH EVERY COPY OF THE TABLE.** This is where I3's duplicated table meets
the remap path, and it is a hard requirement, not an optimisation. DESIGN §28.2
D:3239-3245:

> "**A move reaches every copy of the table.** ... in this model there are **N+1
> `PageTable` objects that must agree: one per tile, one in the host's MMU, one in the
> fabric that decides. A `RemapEvent` goes to all of them and each flushes its cached
> translations for that grain. A copy that missed one would resolve to a frame that has
> been given back** — the same class of disagreement as §27.1, **which is why it is
> broadcast rather than recomputed.**"

**`N+1` tables, one `RemapEvent` broadcast to all of them, each flushing its cached
translations for that grain.** Recomputing per copy is forbidden: two copies that
recompute independently can disagree, and **both answers are legal physical addresses**,
so nothing downstream detects it (the scar recorded in Appendix 2 D7 as "two page tables
that are copies built from different parameters").

**Spill — and it is a TILE, not a channel.** `[VOCABULARY RULED — user ruling 2026-09-02
R18: "**channel is odd language here, should definitely be using 'tile'**." Every statement
about spill in this document now says *tile*. The two are the same object (D.1), but
"channel" invites the reader to think of a device where the design means a tile.]`

If a **tile's** free-frame list is exhausted, the grain spills to another **tile** — the
vtile does not get the home it asked for. **The cost of a spill is a migration and a broken
co-location, not a longer access**: there is no remote data path. **The spill rate is the
statistic that says siloing went too far.**

**What happens when it cannot spill at all.** `[RULED — user ruling 2026-09-02 R18:
"Possibly, maybe OOM. Unsure." and, on the simulator's behaviour, "**the sim print a
warning when it happens, but let's not hard-error or anything, at least at the moment.**"]`
**Spill may end in OOM**, and that is an acceptable outcome — an allocation that cannot be
satisfied is an allocation that cannot be satisfied, and this machine is not obliged to
invent capacity. **The simulator WARNS and NEVER hard-errors.** A run that spills, or that
runs out, keeps going and says so; an implementation that aborts on a spill is wrong,
because it turns a reportable statistic into a crash and hides the very rate that diagnoses
over-siloing.

**And the real failure is SCATTER, not exhaustion** (DESIGN §12 D:1012): "*an NMFC unit
needs N free rows on one **tile**, so a scattered free list can fail an allocation while
total free capacity is ample*". Mitigated with a `(tile, row)` free bitmap and reported as
**largest allocatable run per tile** — see O.4.

**WHICH TILE RECEIVES THE SPILL IS RULED.** `[RULED — user ruling 2026-09-03 **O6**,
verbatim: "**Presumably the vtile's home would be where we first tried to place it and
discovered we couldn't. It should therefore be placed where the next-largest cluster of
similar vtiles are, and if none exist, the least-loaded.**" R18 had already ruled the
failure behaviour and the vocabulary; this rules the target.]`

**THE RULE, IN TWO STEPS AND IN THAT ORDER:**

| order | target | when |
|---|---|---|
| **1** | **the tile holding the NEXT-LARGEST CLUSTER of the same vtile** | whenever any other tile already holds grains of this vtile |
| **2** | **the LEAST-LOADED tile** | only when no other tile holds any grain of this vtile |

**Read the ruling's first sentence, because it corrects a premise the O6 row carried.** The
row offered "the vtile's second-choice home, **recorded at allocation time**" as one option.
**The user's answer says the home *is* where placement was first attempted and failed** —
there is no separate second choice to record, and no allocation-time state to keep. The
spill target is **computed at spill time from where the vtile already lives.**

**Why the cluster rule and not the balance rule, stated so it is not "optimised" back to
least-loaded:** a spill's cost is **a migration and a broken co-location, not a longer
access** (above, and there is no remote data path). **Co-location is the quantity being
preserved**, so the spill goes where the most of this vtile's work already is — that
minimises the *number* of contexts that must migrate to reach it, which is what the cost
actually is. **Least-loaded optimises the wrong quantity** and is correct only in the case
where there is no co-location left to preserve, which is exactly the fallback. `[derived
from ruling O6 — the ordering is the user's; this paragraph is the document's reason for
it.]`

**Both steps are still under R18:** the vocabulary is **tile**, and **the simulator WARNS
and NEVER hard-errors** — including when step 2 itself cannot be satisfied, which is the
OOM case ruled acceptable above. **The spill rate, and now also the split between step-1
and step-2 spills, is the statistic that says siloing went too far** — a rising share of
step-2 spills means the vtile's clusters have themselves been scattered. See O.4.

### F.9 Allocation groups, and the aliasing rule

Two mapping modes are two different tilings of one physical space, and they line up
only at the granularity of an **aligned run of N grains — a *group***, which under
either mode occupies the same chunk indices on every channel. **Mixing modes inside a
group aliases: measured at 6.7% of blocks colliding on a channel-local address**,
which nothing below the controller could detect and which would show up as row-buffer
hits between unrelated pages (`src/nmfc/nmfc_vmem.cc:404-416`).

So: grains are handed out in groups; **grain g lives on tile g mod N** under the NMFC
layout; a replica set is an aligned N-run, so copy *t* is `base + t` and lands on tile
*t* **by construction, with no per-tile table**; and **all N copies of a duplicate are
made together** — a replicated grain existing on only some channels would silently
turn "choose a copy" back into "choose among the tiles that happen to have one", which
is the compile-time layout this exists to replace.

### F.10 The mechanisms must be RUNTIME-SWAPPABLE, and both must be measured

**This is a standing requirement of the project, stated three times, never retracted,
and previously absent from this document** — a reader could not have learned from the
canon that alternatives are supposed to remain selectable and comparable.

*User #30, 2026-08-27T19:12:26Z:* "**Can we not have these policies be swappable? I feel
like we need to evaluate both systems.** I think translate-then-route is flexible, it
offloads the complexity of compiler function duplication, address space layout, etc...
to the OS, who is by nature the proper owner of this. On the other hand, the relocation
table makes sense if migrations are rare. **So I think we need to be able to
runtime-config swap these policies and evaluate each.** I don't think needing to
translate before route is a big deal ... a single translation on dispatch should not be
seen as a big overhead."

*User #8, 2026-08-27T06:19:21Z (AskUserQuestion answer):* all three translation
mechanisms — **per-context translation cache, small TLB + local PTW (baseline), and a
flat/region page table** — to be built "**behind a swappable `translation_engine`
interface**". That much stands and is the requirement this section carries.

`[AUTHORITY CORRECTION — #8's OTHER clause is SUPERSEDED and this document previously
carried it as live. This is a second instance of the exact fault (an older tier-1
statement governing a newer one) that was corrected once already for #265/#269.]` #8's
answer to "where should the per-context translation cache sit" was "**In the context,
migrates with it (Recommended)**". **#9, 2026-08-27T06:33:44Z — FOURTEEN MINUTES
NEWER — reverses it directly:** "*For designs where we store translation data with a
given context, **it does not need migrated. The translation data is useless if migrated,
so it shouldn't be migrated.***" **Newest wins (#307): translations are DROPPED on
migration** — F.7 states the structural reason (after migrating, every address the
context is about to touch belongs to the new tile by construction, so every carried entry
is provably invalid) and R14 rejects a `carry_translations` knob on the same authority.
**The swappability requirement survives; "the per-context cache migrates with the
context" does not.** Anything reading #8 as licensing carried translations is reading
a fourteen-minute-old draft of the user's own position.

*User #98, 2026-08-28T21:12:32Z, defending it against scope creep:* "Okay, go for it.
**please don't let the swappable translation system go out of scope.** I noticed no
mention of the vmem→pmem placement. Everything was aperture-based and compile-time
placement."

**AND THE NEWEST AND MOST SPECIFIC AUTHORITY OF THE FOUR, WHICH NAMES THE POLICY BY NAME
AND WAS ABSENT FROM THIS DOCUMENT ENTIRELY.** [OMISSION CORRECTED — `grep` for "should
be supported" and for "vmem places tiles" both returned ZERO across this document. #118 is
cited four times elsewhere (I6, G.1, G.4 rule 7, R48) and a different clause is quoted
each time; this is the clause that licenses F.10, and it is newer than #30, #8 and #98,
all three of which speak generally about "policies" without naming one.]

*User #118, 2026-08-28T23:51:00Z, verbatim:*

> "We have been over this. Once again, you are trying my patience. What is the purpose of
> docs if you don't read or update them? **We are testing multiple policies. There is a
> policy where vmem places tiles. That policy should be supported, but we don't want to
> use it.**"

**That is the requirement stated at its sharpest, and it settles three things:**
1. **"We are testing multiple policies"** — the plural is the project's standing shape,
   not a phase that ended. Newest word on it, 2026-08-28T23:51.
2. **The VA-places-tiles policy MUST BE SUPPORTED.** That is `CONGRUENT_ROUTER` /
   `VIRTUAL_FIRST` — the branch F.3 rejects *architecturally*. **Rejected as the design
   and required as a control are not in tension; the user said both, and said them in
   this order.**
3. **"but we don't want to use it"** — so it must not be the **default**. Which is
   exactly what ChampSim ships (L4: `CONGRUENT_ROUTER` in four files including
   `nmfc_4tile.json`).

**This is the tier-1 authority for the "relabel, do not delete" ruling** recorded at
ledger **L4**, **L26** and **L31**, and for the table below. Those rulings previously
rested on #30, #8 and #98 — all three **older** than #118 and all three phrased about
"policies" in general.

**How this coexists with the rejections in F.3–F.5, which is the whole subtlety:**

| | |
|---|---|
| **what the rejections settle** | **which mechanism the machine IS.** Physical-address partitioning; translate-then-route; one duplicated page table. A workload, a measurement or a default configuration that assumes anything else is describing a different machine. |
| **what this requirement adds** | **the rejected branches remain BUILDABLE and SELECTABLE as controls, so the rejection stays measured rather than asserted.** "We need to evaluate both systems" is a statement about *evidence*, not about which design won. |
| **what it does NOT license** | shipping a rejected branch as the **default** (that is ledger L4's complaint), or citing a control's behaviour as the architecture. |

**Consequence for ledger L4.** L4 asks whether `CONGRUENT_ROUTER` should be deleted.
**On this requirement it must be RELABELLED, not deleted** — it *is* the "policy where
vmem places tiles" that **#118 says in so many words must be supported**, and deleting it
removes exactly the comparison #30, #98 and #118 asked for. What must change is which
router the **default** config names, because the same sentence says "**but we don't want
to use it**". Same reasoning for `--walk-routing fabric` (F.6): keep it as a labelled
control; do not let **15** configs select it silently (F.6's census — the 13 that an
earlier revision printed here is the `first_touch` count).

[CONFIRMED, IN EXACTLY THESE WORDS — user ruling 2026-09-02 R2: "**That is fine, relabel
is fine. You are correct the defaults should be switched to the physical/nuca router.**"
Both halves, in #118's own order: the mechanism is **relabelled and kept**, and the
**default moves** to `PHYSICAL_ROUTER`/`NUCA_ROUTER`. Ledger **L4** is closed. This is one
of exactly two ChampSim changes ordered before the freeze (R3, Appendix 2 D0).]

**And the freeze does not un-do it.** R3 stops ChampSim *updates*; R2 is an *ordered*
change and is exempt. **`--walk-routing`'s default is NOT exempt** — no ruling names it —
so `fabric` stays as a labelled control and its default change is queued behind the freeze
(L26, "frozen, not abandoned"). **The design requirement is unchanged in both cases: the
rejected branch is a control, never the machine.**

---

## PART G — NUCA / NUMA POLICY

> **[RULED — HISTORICAL OBSERVATIONS. user ruling 2026-09-03 **O15**: "**a.**", selecting
> option (a) of that row.] EVERY MEASUREMENT IN THIS PART IS A HISTORICAL OBSERVATION OF AN
> EARLIER TREE.** Its configuration is **unreproducible from git** — no measurement here
> names the configuration file that produced it, the 4 MiB LLC slice several of them were
> taken at was never committed (L28c, lookup run), and the checked-in configurations cannot
> run against the checked-in workload (L20, E.3). **ChampSim stays frozen** (user ruling
> 2026-09-02 R3), so re-taking them is not an available action and was not asked for.
> **Quote every number here as a recorded observation whose configuration is unknown, never
> as evidence about the machine and never as something a reader can re-run. Do not tune a
> parameter to reproduce one.** The standing statement of all four facts is **N.0**.

### G.1 What the policy is for

**Physical placement without a NUCA/NUMA policy is not the design** (I6). Round-robin,
least-loaded and first-touch are **not substitutes**; they are dispatch policies for
choosing *which tile starts an invocation*, which is a different question.

The user gave the policy's job twice, and both halves are required (#118,
2026-08-28T23:51:00Z): "**The NUCA/NUMA is supposed to be migrating data that is used
together across tiles.** Note that at the same time, **we need a policy that allows
for functions to migrate to their data. So, we have both the source and sink that are
moveable, and we need to partition them evenly across the tiles.**"

And the tension it must resolve (#28, 2026-08-27T19:01:11Z): "**the underlying tension
between putting everything on one tile (minimized migrations) and exploiting
parallelism (bandwidth utilization, tile contention).**"

`[NARROWED — and this is the NEWEST tier-1 word on what NUCA/NUMA is FOR. #106 and #118,
which the paragraphs above rest on, are both 2026-08-28. #175 is 2026-08-29T17:37:11Z,
eighteen hours later, and #307 admits no exceptions: newer overrides older.]`

> *User #175, verbatim:* "So, if work migrates, **NUCA and NUMA are irrelevant except for
> the key point: If there is any runtime optimization to be made at all, it is to make
> sure migration remains classified as sub-optimal.**"

**The policy's job, restated on the newest authority: ONE objective — keep migration
classified as sub-optimal.** The two halves quoted from #118 above are *how* (move data
that is used together; let functions move to their data; keep both spread), and they
stand as mechanism. What #175 removes is the general mandate: **NUCA/NUMA is not
justified here by bandwidth, and it is not justified by lowering a migration count** —
migration subsumes the transfer it replaces (I11, J.2), and §29.2 measured that removing
**4.96×** of the migrations (1,703,838 → 343,858) made the machine **38.2% slower**
(91.0 → 125.8 ms; Part L, and quote the row). `[CORRECTED — was "five sixths".]` A policy proposal
that argues from "this reduces migrations" is arguing from the retired premise.

**Nothing later retracts this.** #151 (2026-08-29T06:21) is older. #291 and #303 name
load balancing as a **defect to diagnose**, not as a placement mandate to restore. See
I6 and G.5.

### G.2 The evidence: a migration is a co-access constraint

**A migration says the address the context left and the address it came for belong
together.** That is a *constraint between two grains*, not a direction to drag one of
them toward the other. So the two grains are **united**, and it is the whole
**component** that gets placed.

*User #28, 2026-08-27T19:01:11Z, the two simultaneous policies:*
1. "A migration is a hint that **the page being accessed should have been on the
   previous tile**."
2. "A migration is a hint that **the function being scheduled to the new tile should
   probably be co-scheduled with the other functions on the present tile**."
"Over time, we should see a natural partition where each hot-set ends up partitioned
into a tile, and the consumers of said hot-sets follow them to those new locations."

*User #42, 2026-08-28T00:54:49Z, restating it after it was dropped:* "**the migration
itself is the feedback. If we are migrating, either the function was in the wrong spot
or the address that invoked the migration is in the wrong spot.** I told you this when
we first started."

**The co-access relation holds between addresses touched by the SAME invocation.**
Uniting whatever two grains happened to migrate consecutively anywhere in the machine
merges everything into one component and says nothing. The invocation token is
therefore not optional detail on the evidence path
(`inc/nmfc/tile_router.h:111-119`).

**PULL DOMINANCE — defined here because G.4's first rule gates on it and it appeared in
this document only as values.** [MODEL-AUTHORED DEFINITION, and it is flagged as such.
No tier-1, tier-2 or tier-3 source writes this formula down. What IS sourced is every
input to it: the values (`nuca_router.cc:28-32`, `:127-132`), the first-hop exclusion
(`function_core.cc:1143-1149`), the window rule (`nuca_router.cc:202-209`) and
Carrefour's gate (G.3). The definition below is this document reconciling them into one
statement so the rule can be implemented; it is not a quotation.]

> **`pull_dominance(grain)` = (migration-pulls credited to the grain's top tile) ÷
> (total migration-pulls credited to that grain), counted over the policy's decision
> WINDOW, excluding each invocation's FIRST hop.**

[DELIMITERS REPAIRED — the definition above opened a single-backtick code span before
the identifier `pull_dominance` and never closed it, so **the document's only formal
definition of the quantity G.4 rule 1 gates on rendered garbled**, the rest of the
blockquote being swallowed into an unterminated code span. The backtick now closes after
the function name and the `/` is written `÷` so it cannot be read as part of an
identifier. The formula is unchanged.]

Each term, so it can be implemented:
- **a "pull"** is one migration event, credited to the grain the invocation migrated
  **toward** — i.e. the grain whose owning tile the context went to. One event, one
  credit; it is a count of migrations, not of accesses.
- **"top tile"** is the destination tile with the most pulls for that grain in the
  window.
- **the window** is a bounded recent interval, **never a lifetime average** (G.4 rule 4
  — a lifetime average starts even and responds to nothing).
- **the first hop is excluded**, for the reason immediately below.
- **1.0** means every pull came from one tile (a private grain); **1/N** means uniform
  (a shared grain, or a scattered component — G.4 rules 1 and 2 say these look alike and
  must be treated differently).

The measured values quoted elsewhere in this document all use that definition: **0.670**
with the first hop wrongly included, **0.718** for scattered grains that never moved,
**0.77** for a shared grain that should not have been co-located. `[CAUTION]` ledger L16
records that the **built** classifier in `nuca_router.cc` does not compute this — its
`private_threshold_` is read from config and never referenced again. **The definition
above is the intent (G.4); it is not a description of running code.**

**Exclude the first hop.** An invocation is dispatched to a tile chosen by a policy
that has not seen its data, so its *opening* migration reports where dispatch put it
and nothing else — uniform noise credited as though it were locality. Measured on a
workload of disjoint clusters where every grain has exactly one true consumer,
including the first hop pulled mean pull dominance down to **0.670** and classified
two thirds of the grains as shared (`src/nmfc/function_core.cc:1143-1149`).

### G.3 Use the published algorithms

*User #29, 2026-08-27T19:07:04Z:* "I am sure several NUCA/NUMA policies already
published handle similar situations, **so we don't need to work from scratch**."
*User #35, 2026-08-28T00:17:20Z:* "you should be leaning on the developed NUCA/NUMA
algorithms, since they apply almost directly here, right? **Our adaptive policy likely
fails in the adversarial case because we don't know how to prevent catastrophic cases
like this, while a well-developed NUCA/NUMA algorithm will prevent this.**"

The two that matter and what each supplies:

- **R-NUCA (Hardavellas et al., ISCA'09)** — classify pages and give each class a
  fixed policy rather than chasing accessors: **instructions are replicated, private
  data is placed at its accessor, and shared read-write data is *interleaved***. That
  last is the case this design kept rediscovering the hard way: **an offline minimum
  cut and an online pull heuristic both lost to interleaving** on a graph whose hot
  array is shared by every tile. Replication of instructions is already handled — the
  duplicate page type *is* R-NUCA's instruction class.
- **Carrefour (Dashti et al., ASPLOS'13)** — supplies **the gate**: measure imbalance
  and the local-access ratio *before* choosing among co-location, interleaving and
  replication, and **apply nothing when the measurements say nothing is wrong**. That
  is the behaviour that makes the adversarial case safe rather than catastrophic.

Also named in the record as worth reading: AutoNUMA (migrates pages toward the thread
touching them *and* the thread toward its pages), thread clustering (EuroSys'07),
Mizan (EuroSys'13) for runtime vertex migration, D-NUCA (ASPLOS'02).

### G.4 The rules the policy must obey

**SEVEN rules, numbered 1-7 below, and the policy must obey all of them.** [ADDED — rule 6
previously called itself "the fifth rule of the five" inside this list of seven, so a
reader could not tell whether the specification had five rules or seven, nor which two of
the seven were not rules. It is seven. The count is stated here so the list cannot
disagree with itself again.]

1. **Shared grains are never co-located, however lopsided their pull looks.** 0.77
   dominance was real evidence and acting on it was still wrong, because the remaining
   0.23 was three other tiles that would then have to migrate.
   *(`src/nmfc/nuca_router.cc:28-32`, verbatim, which also states the classification rule
   it belongs to — "a grain is PRIVATE when essentially one tile wants it, SHARED
   otherwise". `adaptive_router.cc:230` reports the same measurement as 0.772.)*
2. **Place whole components, never grains one at a time.** A cluster's grains
   scattered over N tiles pull uniformly from all N, so the dominant puller is noise
   until a majority already sits on one tile — **scattered is a stable equilibrium.**
   Measured: dominance 0.718 and **20 of 345 grains ever moved**, against an oracle
   19.5× better. Moving a component as a unit is the symmetry break.
   *(`src/nmfc/nuca_router.cc:127-132`, verbatim, at the `unite()` call the rule
   governs.)*
3. **A component larger than a tile's fair share is the whole working set, not a hot
   set. Leave it interleaved.** [QUANTIFIED — an earlier revision stated this rule six
   times and defined "fair share" nowhere, so the gate could not be written and G.5's own
   acceptance test could not be run. **It IS defined at tier 2**: `src/nmfc/nuca_router.cc:253`
   refuses a component when **`members.size() > ceil(|observed grains| / tiles)`** — the
   component's **grain count** exceeds an equal split of every grain the router has
   observed so far. **Nothing at tier 1 or tier 3 states that a grain count is the right
   unit or that every observed grain is the right denominator, and the denominator grows
   during a run, so the gate loosens on its own.** [DEFECT RECORDED, NOT RATIFIED — this
   was open item **O10** and it is closed in editing, because it was never a ruling to ask
   for. A denominator that grows as the run proceeds, so that the gate loosens on its own,
   is a **defect of the frozen implementation**, in the same class R5 established for
   L14/L15 — and `nuca_router.cc` is on a codebase R3 froze, so it is not being changed.
   The unit and the denominator are **tuning parameters**, and **R21** already rules that
   the NUCA algorithm starts at common values and is tuned from measurement: they are
   configuration, not canon. **G.4 rule 3's canon statement is the rule — a component
   larger than a tile's fair share is the whole working set, leave it interleaved — not
   `:253`'s arithmetic.** Recorded at ledger **L42**.] BEWARE THE OTHER
   ONE: `nuca_router.cc:244` computes a **second, different** "fair share",
   `share = total_window_load / tiles`, and `:265` withholds a move when
   `window_[best] > slack_ × share`. That one is **traffic in a window**, it is rule 4's
   gate and not rule 3's, and it increments `withheld-imbalance`, not `too-large`.] On a graph where everything touches everything the
   co-access graph is fully connected, and collapsing it onto one tile is exactly the
   failure an offline minimum cut already demonstrated.
4. **Gate on a WINDOW, not on a lifetime average.** A lifetime average starts even and
   responds to nothing: measured, it **allowed 14 co-locations while the cumulative
   load still looked balanced, those 14 were the hottest grains in the machine, and
   the resulting per-tile occupancy was 71/75/538/73. The gate then refused 344
   further moves — correctly, and far too late to matter.**
   *(`src/nmfc/nuca_router.cc:202-209`, verbatim, on `imbalance()` — the function that
   asks "Carrefour's question, asked before anything is moved: is anything wrong?")*
5. **Balance is not first-touch's job.** Congruent placement at first touch is a
   *correctness* invariant (I9); deliberate balancing belongs to `remap_grain()`.
   A round-robin counter that never reads the address makes a grain's tile depend on
   the order it was first touched, and **grains carry very unequal traffic, so
   spreading them evenly by count spreads traffic unevenly — measured, 75.3% of all
   accesses routed to the wrong tile.**
   *(`src/nmfc/nuca_router.cc:104-110` for the removal and "Balance belongs to
   `remap_grain()`"; `src/nmfc/nmfc_vmem.cc:540-547` and DESIGN §18 D:1443-1523 for the
   75.3%.)*
6. **A GRAIN THAT HAS JUST MOVED SITS STILL — for longer the more often it has moved.**
   Without this the policy **oscillates**: it reacts to a pull, and the pull reverses,
   because the migrations now come from where the grain used to be. DESIGN §28.2
   D:3226-3231, verbatim: "a grain that has just moved **sits still, for longer the more
   often it has moved.** Without that **the policy oscillates** ... **Measured: at a
   50-migration epoch, 29 moves and 60 MB copied became 3 moves and 6 MB, with 56
   attempts withheld.**" **This rule was previously missing from this document altogether —
   a policy built without it oscillates, and `withheld-attempts` is the statistic that
   would have shown it.** [CORRECTED — this sentence read "**That is the fifth rule of the
   five**", inside a numbered list of **seven**. There is no five-rule specification: G.4
   is headed "The rules the policy must obey" and **all seven are rules the policy must
   obey**. The phrase was left over from an earlier revision in which this section carried
   five items and this one was the fifth added; rule 3's own inserted note already uses
   the seven-item numbering ("that one is rule 4's gate and not rule 3's"). **G.4 is a
   SEVEN-rule specification. An implementer writes all seven.**]

   `[THE BACKOFF FUNCTION ITSELF IS CONFIGURATION — user ruling 2026-09-02 R21, verbatim:
   "Undecided, once again something that must be experimentally derived. **We start at
   common and implement tuning/algorithm adjustments as needed.**"]` **The rule is the
   design; the function, its parameters and its reset are not.** Start from the common
   published form — exponential withholding capped at some maximum, reset when a grain has
   sat still for a full epoch — and tune it against measurement. **See SELECTED
   CONFIGURATION** for what is currently set. **Instrument it regardless**: count **moves,
   bytes copied, and attempts withheld**, per epoch. At the `G` of the measured
   configuration (1 MiB — F.8) the 60 MB → 6 MB figure is the whole point.
7. **A duplication policy is available and is often cleaner than a partial swap.**
   *User #118:* "sub-grain swaps are viable, important for data structures that might
   need to be present on all channels. **Then again, NUCA should have a duplication
   policy that does essentially the same thing and doesn't involve all the ugliness
   the scheme I just suggested has.**" But see Part L: duplication is an **option**,
   never the baseline.

### G.5 What the policy has actually said, and what that means

On the real graph, the component former **refused every component as larger than a
tile's fair share** — 87 in one run (DESIGN §21.1 D:1817-1818, `COMPONENTS seen: 87
placed: 0 too-large: 87`), 818 in another (DESIGN §27.1 D:3103-3104, §29 D:3288 and §29.3
D:3366 — three statements of the same run, all tier 3), **0 placed in both**. That looked
like a policy doing nothing. §29 then measured what doing otherwise costs and found it
was **right**: replication cut migrations 4.96× and made the machine **38.2% slower**
(Part N, K.3 — quote a row, never the range). So the refusal **retro-validates the
policy**.

`[BUT THE REFUSAL IS ALSO A SYMPTOM, AND THE PARAGRAPH NAMING WHAT IT IS A SYMPTOM OF WAS
DELETED. An earlier revision kept §21.1's counts and its retro-validation and dropped the
mechanism and the next step — so a document written to stop re-derivation had removed the
paragraph naming what has NOT been derived. Verified: 0 occurrences of "blob", "dense",
"too-large" or "clustering problem"; the single "clustering" hit was "thread clustering
(EuroSys'07)" in G.3's prior-art list.]`

**DESIGN §21.1 D:1813-1822, verbatim, and it is the open problem this Part exists to hand
forward:**

> "*`remap_grain()` moves a whole component rather than one grain at a time — the comment
> there records that placing grains individually cannot escape a random start. What it
> cannot currently do is ***place*** anything: **every component it forms exceeds a tile's
> fair share and is refused as "the whole working set"**, and the measured run shows
> **`COMPONENTS seen: 87 placed: 0 too-large: 87`**. **On a graph where everything touches
> everything the co-access relation is close to fully connected, and unioning on every
> migration collapses it into one blob.**
>
> So **the open question is not whether to migrate. It is how to form components that are
> smaller than the working set on a graph whose co-access relation is dense — which is a
> clustering problem with a real literature, and is where this work should go next.**"*

**THE MECHANISM OF THE FAILURE, stated so it is not re-derived:** the former **unions on
every migration**. On a dense co-access relation that is transitive closure by another
name, so **the first few migrations merge everything into one component**, which then
exceeds any tile's fair share and is refused. **The 87/818 counts are the count of times
that happened, not 87 distinct candidate placements.**

**TWO DISTINCT CONCLUSIONS, and they must not be merged — one is settled, one is open:**
1. **SETTLED (retro-validation):** *given components of that shape*, refusing to place
   them was right — §29 measured the alternative at 38.2% slower.
2. **OPEN (§21.1's named next step):** **how to form components that are smaller than the
   working set on a dense co-access graph.** This is a **clustering problem with a real
   literature** and it is **where this work should go next**. Nothing in the record has
   attempted it. **The retro-validation does not answer it and must not be quoted as
   though it does** — "the policy was right to refuse" is not "the components were the
   right components".

**The concrete next action it implies:** replace *union-on-every-migration* with a
clustering criterion that bounds component size — and the acceptance test is already
written, `COMPONENTS seen: n placed: >0 too-large: <n` on the same run. **The bound the
criterion must come in under is now stated rather than gestured at**: `too-large` is
incremented at `src/nmfc/nuca_router.cc:253` when a component's **grain count** exceeds
`ceil(|observed grains| / tiles)` (G.4 rule 3). `[ADDED — this experiment was named as the
next step with its gate undefined, so it could not be written. It can be now. **That bound is NOT ratified as canon — G.4 rule 3 records
its growing denominator as a defect of the frozen implementation, and R21 makes the unit
and denominator tuning parameters — but it IS the bound the 87/818 counts were produced
against, so it is the one a comparison must hold fixed.]` **Until `placed:`
is non-zero, the policy has never been observed acting**, and every statement in Parts G,
K and N about what the policy "did" is a statement about what it **declined** to do.
`[COMPOUNDING — per L41, the layout it would have placed data INTO cannot currently be
expressed either: GRAIN/siloing has no representation in the toolchain, so both ends of
this experiment are missing.]`

**The genuinely new problem is not a new mechanism, it is an old one under a harder
condition: the sources of work are themselves moving.** Classical NUCA assumes the
requesters sit still and the data is placed relative to them. **Here both move, and
the placement has to hold anyway.** The likely shape of the answer is
**neighbour-touch recognition** — blocks touched by the same function are co-located,
co-location is performed, **and the work follows the data wherever the placement
algorithm puts it. Work is never divided up explicitly.** There is no partitioning
step, no owner assignment, no decomposition the compiler has to get right. That is the
open research direction (DESIGN §21.1 D:1807-1813).

**And #175's OWN framing of what that direction is for, which this section previously
truncated away by quoting only the message's tail.** The full sequence, verbatim, in
order:

> "So, if work migrates, **NUCA and NUMA are irrelevant except for the key point: If
> there is any runtime optimization to be made at all, it is to make sure migration
> remains classified as sub-optimal.** If a line is being used a lot, we would prefer it
> be in one location over another. This is what NUCA and NUMA are all about, so it seems
> the remaining part of this work still collapses back to **how to get NUCA/NUMA to hold
> when workload sources are also shifting.** Likely, it is some sort of neighbor-touch
> recognition, where blocks that are touched by the same function are colocated,
> colocation happens, and the work follows the shifting data. **No need to try to divy up
> work, it follows the data as placed by those algorithms.**"

**The "irrelevant except" clause is not a preamble to the research direction; it is the
statement of the objective the direction serves** — and it is the newest tier-1 word on
NUCA/NUMA's role in the whole log (2026-08-29T17:37:11Z, eighteen hours after #106/#118).
Carried into I6 and G.1, where it changes what a policy is allowed to argue from.

### G.6 Diagnosing imbalance

*User #151, 2026-08-29T06:21:51Z, the diagnostic rule:* "**Tile imbalance is either
annotation or runtime problem. Either virtual tile work is not properly distributed,
or the OS/NUCA is not load-balancing the virtual tiles into real tiles.**"

*User #209, 2026-09-01T01:05:18Z:* "**We should not be accepting everything landing on
one tile as correct. That is, in fact, not correct. It is unacceptable.**"

`[RESOLVED. The complaint that opened this — user #303, 2026-09-02T23:28:06Z, "Is the
stress test working or not? **It seems like a failure.** … There is clearly a massive
load-balancing problem" — was the TRIGGER FOR A RESHAPE, not a ruling that the machine is
broken. The reshape was made, the workload was re-run, and it PASSES. This section records
both the failure and the fix, because the failure is the more instructive half.]`

**THE STRESS WORKLOAD WORKS. Ledger L32 is closed.**

**What was wrong: the workload had the REJECTED SHAPE.** Its first form gave each
invocation an index range to own and had it **chase data from there** — own an index range,
follow it into the data array. That is K.3's chase decomposition, and its 2.5:1 tile spread
(5.37 / 13.27 / 12.57 / 5.61 contexts) was **not a defect in the machine at all**: it was
**predicted exactly by the addresses**. Working the placement out by hand from the index
ranges gives **12.5% / 37.5% / 37.5% / 12.5%** across four tiles, and that is what the run
measured. **The routing was congruent and correct; the work was shaped so that congruent
routing lands it unevenly.** A machine that placed that workload evenly would have been the
one with the bug.

**What the reshape was.** The host resolves seven indices **into the 512-bit context**; the
function does **seven data loads** and returns the sum via **END with the return bit**; the
`FORK.R`/`JOIN` ring is sized to the FTU. Nothing about the machine changed.

**What it measures now, and these are settled numbers:**
- **Correctness: PASS.** The sum is verified against the host's own computation.
- **Balance: 25.0% on every tile** — loads, migrations and instructions all four-way even.
  There is no residual imbalance to diagnose.
- **Zero stores.**
- **196,904 migrations for 262,143 loads** — 0.75 per memory operation, inside invariant
  5's legitimacy ceiling by the same margin as the BFS run.

**Provenance, stated because O15 requires it of every number.** The reshaped kernels are in
the working tree at HEAD `21df518d` and are **not committed**:
`tools/nmfc/kernels/bfs_nmfc.cc` (modified) and `tools/nmfc/kernels/bfs_base.cc` (new),
built to `tools/nmfc/kernels/bfs_nmfc` and `bfs_base`. **Quote the build and the binary
with the numbers**; there is no commit hash to cite because ChampSim is frozen (R3) and the
tree was not committed before the freeze.

**What this rules:**
1. **The stress numbers are SETTLED, not provisional.** The occupancy table in H.9, the
   context sweeps in N.4, and the **11.3048 ms / 136.143 ms invariance** stand as evidence,
   with their build and binary noted as above. **The "provisional" caveats this document
   carried on them are removed.**
2. **The load-balancing defect is CLOSED, and its diagnosis is worth keeping**: an
   imbalance predicted exactly from the addresses is a workload-shape finding, not a policy
   finding. #151's rule below said so first — "*either virtual tile work is not properly
   distributed, or the OS/NUCA is not load-balancing*" — and the answer was the first of
   the two.
3. **The rejected shape stays rejected.** Chase decomposition (K.3) produced this, and the
   shape that works is the one K.5 describes: own a range and pull.

   `[AND THERE IS A CHECKED-IN MECHANISM THAT PRODUCES EXACTLY THIS SYMPTOM, WHICH THIS
   DOCUMENT DID NOT NAME. It is the first thing to check before re-deriving anything.]`
   Under **`NUCA_ROUTER` with `placement_policy: "first_touch"`**, every invocation
   sharing an entry PC is dispatched to **one tile**, by construction:
   - `nuca_router.cc:95-107` — `placement_for` returns
     `map_.tile_of_virtual(vaddr.to<std::uint64_t>())`, a pure function of the address it
     is handed;
   - the address it is handed is the **entry PC, unmodified** —
     `function_fabric.cc:115-121` says so in its own comment ("*The entry PC is **not**
     adjusted. A function's code is one virtual page that the OS aliases to a copy on
     every channel*"), and both callers set it verbatim
     (`nmfc_host_core.cc:1320`, `function_core.cc:884`: `msg.entry_pc = body->entry_pc`);
   - so **every invocation of one function starts at the same virtual address and is
     therefore sent to the same tile.** `tile_router.cc:43-48` documents the identical
     property of the congruent router in as many words: "*every invocation starts at the
     same virtual address, so every invocation is sent to the same tile*" — and adds that
     this is why "*this router is the wrong one to run replicated code under*".

   **Two checked-in configs select this pair** — `nuca_ft/nmfc_4tile.json` and
   `nuca_ft/fast_epoch.json` — and a parsed diff of `nuca/nmfc_4tile.json` against
   `nuca_ft/nmfc_4tile.json` differs **only** in this string plus the producer's
   `ftu_size`. **A run under `nuca_ft/` piles a whole function's invocations onto one
   tile before any policy has had a chance to act.** Whether the stress run used one of
   these is not recorded; **establish that first.** (And note that under `PHYSICAL_ROUTER`
   the failure is the opposite one — placement ignores the address entirely and is a bare
   counter. See A.4's `[CONFLICT]` and ledger **L36**.)
3. **The layout questions were already settled by the ChampSim runs.** The instruction is
   to *read* what those runs established — this Part, Part G.4, Part L, Part N — **not to
   re-derive it.** Re-deriving is the failure being complained about.

*User #157, 2026-08-29T07:05:54Z, on not conflating two problems:* "**I DO NOT CARE
THAT SOME DRAM QUEUES ARE EMPTY WHEN TALKING ABOUT BANDWIDTH UTILIZATION. IF ONE TILE
IS LOADED WITH EVERYTHING AND SATURATING AT HALF ITS EXPECTED BANDWIDTH, THAT IS A
PROBLEM. ... IT HAS NOTHING TO DO WITH LOAD BALANCING.** ... Secondly, the imbalance
is clear. I am convinced this is either an annotation issue or just a bug. **Stop
conflating this with the other issue.**"

---

## PART H — THE FUNCTION CORE

### H.1 What it is, and what it is not

**Multi-context, in-order per context, non-speculative. No ROB, no rename, no
load/store queue, no branch predictor, no speculative execution.** It is **designed,
not copied** from the standard core.

**[QUALIFIED — "NO BRANCH PREDICTOR" IS A STATEMENT ABOUT WHAT EXECUTES, AND THIS PART
LATER PROPOSES ADDING A PREDICTOR. Read both, in this order.]** The sentence above
describes the machine **as designed and as built**, and every clause of it holds for
execution: **nothing on a function core issues on a prediction.** But two structures on
the fetch side are *not* excluded by it, and both are endorsed elsewhere in this Part, so
an implementer taking the opening sentence as the whole spec builds the wrong thing:

- **A shared BTB exists and is settled.** H.5: #238 (2026-09-01T06:19) ruled out a branch
  predictor and speculative fetch; #245, **later the same day**, adds a small shared BTB
  used purely for **fetch-address generation**; #291 item 6 confirms it. Newest wins. It
  predicts nothing about execution.
- **A block-granular BTB with a bimodal bit is ADOPTED — and it IS a branch predictor,
  which is why the opening sentence had to be narrowed.** #77 proposed it, verbatim: "*if
  we aren't using the BTB for actual branch prediction, we could just **encode it at the
  block-granularity and use a simple bimodal predictor** plus that to predict instruction
  streams*". `[RULED — user ruling 2026-09-03 **O12**, verbatim: "**I think bimodal is
  fine, since it only ever speculatively issues a single fetch, never executes. It is also
  essentially free (built into the btb table, tracks a particular branch).**" Option (a)
  of that row: adopt it, and narrow H.1's clause.]` [CORRECTED — the previous revision
  reconciled the BTB half of this tension at H.5 and left the predictor half unreconciled,
  so Part H opened by forbidding a structure the same Part later proposed building. The
  ruling settles which way the reconciliation goes.]

> **THE CLAUSE, AS NARROWED BY O12: the function core has NO BRANCH PREDICTOR IN ITS
> EXECUTION PATH.** Nothing anywhere issues on a prediction, fetches down a predicted path
> beyond a single instruction, or squashes. **Every other clause of the opening sentence
> stands unchanged** — no ROB, no rename, no load/store queue, no speculative execution.

**WHAT IS ADOPTED, EXACTLY, AND ITS THREE LIMITS.** `[RULED — user ruling 2026-09-03
**O12**.]`

| | |
|---|---|
| **the structure** | the **shared BTB re-encoded at BLOCK granularity**, with **one bimodal bit per entry**. It is not a separate predictor array. |
| **what it may do** | issue **ONE** speculative instruction-stream fetch. |
| **what it may never do** | **execute.** "*It only ever speculatively issues a single fetch, never executes.*" |
| **what it costs** | **essentially nothing.** "*It is also essentially free (built into the btb table, tracks a particular branch).*" The bit rides in a BTB entry that already exists (H.5), so the structure's cost is one bit per entry and it still **does not scale with `C`**. |

**AND THE CAVEAT ON THE MEASURED NUMBERS IS UNAFFECTED — THIS DOES NOT REPAIR IT.** DESIGN
§12 D:1016's "*the function core replays resolved control flow, so it never mispredicts*"
is **caveat 2 on every measured function-core number in this document**, and it stays
exactly as it is. **Nothing about adopting a fetch-side predictor changes what was already
measured on a machine that had none**, and the predictor does not execute, so it could not
have changed those numbers even if it had been present. **Do not quote O12 as having closed
the never-mispredicts caveat.** It is still open, it is still a caveat, and it is O.4's
business.

*User #4, 2026-08-27T05:52:15Z, said this at the start:* "The NMFC cores themselves
are not just a blatant copy: **they work fundamentally differently than the standard
core**, and should be treated as such. **It isn't apparent if branch prediction,
aggressive reordering, or any advanced OOO features are even necessary.** While we
should be prepared to model them, we don't want to fall into the trap of
overcomplicating what the NMFCs actually are (**for example, the regfile of a standard
core doesn't even apply here, the architecture is entirely different, each function is
it's own context** and the current ChampSim core model doesn't do more than one context
at a time)."

**It is a component, one per memory tile, not a coprocessor attached to a host core.**
"'Reached over the fabric' describes the invocation, not the core's position." A
coprocessor subcomponent of a host core would share that core's memory and **make
locality free — which is the quantity the whole design exists to measure.** As the
design puts it: *the topology has to be wrong before the numbers can be.*
(DESIGN §25.1 D:2321-2338, and §25 D:2300-2304, which frames the whole Part: "*§24 plans
the work; this section says what the thing being built IS. Everything here is a statement
about the machine on Rev.*")

**WHAT WAS ALREADY SETTLED BEFORE THIS PART, AND WHAT IS ACTUALLY NEW.** DESIGN §25.0
D:2308-2319: `src/nmfc/function_core.cc` already states and models the shape — "multi-
context, in-order per context, non-speculative. No ROB, no rename, no branch predictor,
no load/store queue" — **read with the qualification above: no predictor in the execution
path** — plus fetch through an instruction-cache channel with a fetch
latency and a taken-branch bubble, **issue width as *contexts issuing per cycle***,
per-context translation, migration, op-class latencies, and a block-granular lock table
with ownership hand-off. **Those carry over.** What is genuinely new is narrower than it
looks: **real instructions in place of `body_instr` records, pipeline DEPTH as an explicit
quantity, the per-context instruction buffer, the register file's actual division, and one
change of policy — H.4's one-outstanding-load rule, which is not a port at all.**
[NOTE — "issue width as contexts issuing per cycle" is the tier-2/tier-3 statement that
ChampSim's `issue_width: {bandwidth: 4}` plays `W`'s role; the value is **configuration — see SELECTED CONFIGURATION**.]

### H.2 Barrel execution — the whole shape in one rule

*User #238, 2026-09-01T06:19:05Z, in his own words:*

> "A pipeline might be deceptively simple. **A single instruction executes, then we
> yield to a new context. Multiple different contexts in the pipe at once.** If we
> issue a load, **we sleep and wake upon arrival** (meaning the load must write to our
> regfile as desired while we slept). **Some number of contexts can be served at once
> on duplicate pipes (lets say, N contexts, never the same context). That is the width
> of the machine. No more than one instruction from each thread, executes then
> yields.** No branch predictor, no btb, no speculative fetch (beyond ... a
> **single-entry fetch queue per context** ... Allows for early fetch, allows the core
> to sleep until fetch completes, costs as much as the widest instruction * contexts,
> lets say 8 KiB?). The clearest caveat I see is **the context->context schedule delay
> must be the pipe depth**, unless we want to support context-conditional forwarding in
> each pipe."

That single rule is what removes the machinery: at most one instruction per context is
ever in flight, so **no two instructions in the pipe can be dependent** — therefore no
forwarding, no interlocking, no hazard detection.

**Definitions.** [DISAMBIGUATED — the user wrote the pipe width as "N contexts" in
#238, and `N` is already the TILE COUNT everywhere else in this document. The width is
written `W` here and the depth `Dp`. See the NOTATION table in the front matter. On the
one worked configuration both numbers are 4, which is why the collision was invisible;
on an eight-tile machine an implementer sizing contexts from the formula below had a
coin-flip chance of using the tile count.]

- **Width `W`** = number of duplicate pipes, each serving a *different* context in the
  same cycle. Never the same context twice. (**`W`, not `N`.** `N` is the tile count.)
- **Depth `Dp`** = the **re-issue delay**: a context cannot present its next instruction
  until its previous one has left the pipe. Context-conditional forwarding inside each
  pipe would relax `Dp`; **it is not planned**.

**The context count is derived, not a free knob:**
```
C  >=  W × ( Dp + L / I )        W = pipe width, NOT the tile count
```
for memory latency `L` and `I` instructions issued between misses. The `W × Dp` term is
the floor that keeps the pipes fed with **no memory system at all**; everything above
it is latency tolerance. `W=4, Dp=8, L≈100, I≈4` → ≈132; `W=4, Dp=8, L≈150, I≈3` → ≈232.
**256 is the right order of magnitude.**

`[AND THE SENTENCE DID NOT STOP THERE. The canon quoted §25.2 up to "256 is the right
order" and dropped the verdict in the same sentence, which is the half that DECIDES
something. Verified: 0 occurrences of "over-provisioned" in the document.]` DESIGN §25.2
D:2364-2367, the clause in full: "*... which is why 256 is the right order and **why the
cap configuration's 1024 is over-provisioned by about 4× at 21 KiB per tile of extra state
for it** (§25.7).*"

> **THE SIZING RULE, STATED SO IT CAN BE APPLIED: derive `C` from the formula, then check
> the state cost. 256 is derived. 1024 is ~4× the derived figure and costs ~21 KiB per
> tile in extra state to hold contexts the formula says will never be live.**

**And the cost of a context — the whole of it, not just the register file** (DESIGN §25.7
D:2569-2573, verbatim; **0 prior occurrences of "87 bytes", "87 KiB" or "21 KiB** in this
document):

> "*Per-context state, then: **64 bytes of register file, ~13 of instruction buffer, ~10
> of data buffer — about 87 bytes.** That is **22 KiB per tile at 256 contexts and 87 KiB
> at 1024**, the latter being **a structure that competes with the LLC slice rather than
> sitting beside it.***"

**`C` IS A PER-TILE NUMBER. Every row of this table is per tile.** The shipped config's
`num_contexts: 1024` is **1024 per tile**; the 4096 that appears in D.5 and L27 is the
MACHINE total, `1024 × 4 tiles`. Do not size an SRAM array from the machine total.

| contexts **per tile** | per-tile context state | machine total at `N`=4 | verdict |
|---|---|---|---|
| **256** | **22 KiB** | 1024 slots | derived from `C ≥ W(Dp + L/I)`; sits beside the LLC slice |
| **1024** — *the shipped config* | **87 KiB** | **4096 slots** | **~4× over-provisioned**; **competes with the LLC slice**. This is the row that judges every checked-in configuration |
| 4096 | ~348 KiB by the same 87 B/context | 16384 slots | **hypothetical — no configuration in the tree selects it.** 16× the derived count, and larger than the LLC slice beside it. Kept only as the far end of the scale |

**Use this to judge a configuration, which is what it is for.** [CORRECTED — BLOCKING,
AND IT WAS THE ONE ROW AN IMPLEMENTER SIZING SRAM WOULD HAVE ACTED ON. This table's third
row read "4096 | ~348 KiB | **no basis in the formula at all** — see D.5/L27, where a
headline config carries 4096 slots", and the note below it said "4096 is 16× the derived
count, and the state that backs it is larger than the cache slice it sits next to" — as
the verdict on the SHIPPED configuration. It is not. **D.5's row says `function core |
1024 contexts configured ... ×4 tiles = 4096 slots`: 4096 is the MACHINE-WIDE slot count
and 1024 is the per-tile `C`.** The table header is "per-tile context state", so a
machine-wide count was being priced as a per-tile one. The shipped config is therefore
**1024 × 87 B = 87 KiB per tile — the table's SECOND row — 4× the derived 256, not 16×**,
and **~348 KiB per tile is a figure no configuration in the tree produces.** The
over-provisioning verdict on the shipped config stands, and DESIGN §25.2's own words for
it are "**over-provisioned by about 4×**" — 4×, which is what the corrected reading gives.
The 4096-per-tile row is retained as the scale's far end and is now labelled hypothetical.
D.5 and L27 are correct as written and were not the source of the error.]

[AND THE ORIGINAL GAP THE NOTE RECORDED IS STILL CLOSED:` before this table existed, D.5
and L27 flagged a headline config's context provisioning and Part H supplied no cost for a
context and no rule condemning over-provisioning, so there was nothing to judge it by.
There is now — **row 2.** `]

**Do not read 87 B as a hardware spec.** It is `64 + ~13 + ~10`; the 64 is exact (H.3),
the other two are the buffers §25.3 sizes and they move with the fetch/data-buffer design.
**The load-bearing facts are the RATIO (1024 is 4×) and the THRESHOLD (at 1024 the array
competes with the LLC slice).**

**Prior art:** Denelcor HEP, Tera MTA; among shipping near-memory parts, UPMEM's DPU.

### H.3 Context state — no stack, 512 bits, and that is all

A context is **a PC into its body plus at most one cache block of registers**. There is
**no stack**. Creating and tearing one down is a slot write.

*User #76, 2026-08-28T18:43:22Z, spelling out the budget:* "Each function has about 64
bytes of scratch space to use, which captures any easy locality work might need within
the register file. **Just to reiterate, a function has access to about 512 bits, which
for standard pointers means about 8 regs, minus the PC and it ends up being 7 regs**,
plenty for nearly all functions. **These are within the core as SRAM.**"

**The "about 8 regs" clause in the quotation above is a GLOSS, and it is the exact
formulation invariant I2 exists to stop.** It is quoted here because the user wrote it,
not because it is the rule. The rule, from the newer #232 (2026-09-01T05:44:29Z): the
file is **512 bits**, divisible as 8×64, 16×32, 64×8 or any mixture; **bit-packing is
the name of the game and it is compile-side work.** Restated as a correction at #238:
"**Once again, NO. 512 bits of context. The context is not 8 regs. Why do you keep
reverting to that?**" **Read the "8 regs" line as "at 64 bits each, that is 8 of them" —
an arithmetic illustration of one packing, never the register file's structure.**

`[DISAMBIGUATED — "8" names FOUR unrelated quantities in this document, and collapsing
them is the specific regression I2 was written against.]`

| the number 8 | what it is | where |
|---|---|---|
| **512 bits ÷ 64 = 8** | one *possible* packing of the context register file. **Not its structure.** | I2, H.3 |
| **8 context registers `ctx0`–`ctx7`** | architectural host-side registers, 512 bits **each**, per software thread — a different object entirely, 4096 bits in total | I.8 |
| **`MAX_FUNCTION_REGS = 8`** | **ChampSim trace-format constant — TIER 2 IMPLEMENTATION LAYOUT, NOT THE DESIGN.** 8 × 64 bits = 512 = one cache block. **An encoding constant** — it is how a fixed-width trace record spells "one cache block", and the only place the number is defined in the tree. **It is not a statement that the context has eight registers**; SST expresses the same convenience as `NMFC_CTX_WORDS = 8` with an `in[0..7]` array, tier 4, and that is not the design either (Appendix 2 **S4**). | I.10, Appendix 2 S4 |
| **"each function consumes no more than 8 instructions"** | an assumption the user **rejected** — "I find that unlikely. are you serious?" | #78, D.6 |

**State cost. 64 bytes × contexts. At 1024 contexts that is 64 KiB of register file per
tile.** That is the number to size an SRAM array from.

`[AND THE REGISTER FILE IS NOT THE WHOLE CONTEXT — H.2 now carries the total.]` The
register file is 64 of about **87 bytes** per context (+~13 instruction buffer, +~10 data
buffer, DESIGN §25.7 D:2569-2573), so per-tile context state is **22 KiB at 256** and
**87 KiB at 1024**. **Size the SRAM array from 64 KiB; size the CONFIGURATION from 87
bytes**, because that is the number that decides whether the structure sits beside the LLC
slice or competes with it. See H.2.

`[DISAMBIGUATED — this paragraph previously set 64 KiB beside the user's "8 KiB" three
lines apart with no reconciliation, an 8× discrepancy for what read as one structure.]`
The user's own arithmetic on why 512 bits is the ceiling (#191, 2026-08-29T23:48:57Z)
reads "**8 bytes for 1024 contexts is 8 kiB. Double is 16 KiB.**" **That prices one
8-byte LANE across 1024 contexts, not the register file**; the file is 64 bytes per
context and therefore 64 KiB per tile, going to 128 KiB if the context doubled. **The
ruling is unaffected, because #191's argument is a ratio, not an absolute:** doubling the
context width doubles the state whichever base you count in, costs **two cycles to
transmit a regfile instead of one**, and hurts regfile latency and usability. **64 bytes
is the natural transmit unit.** **Size from 64 KiB; argue from the doubling; never quote
"8 KiB" as the size of the register file.**

**Context states: FREE, READY, RUNNING, BLOCKED, DONE — five, with `MIGRATING` recorded as
a TRANSITION and not as a slot-occupying state.**

**[RECONCILED IN EDITING — this was open item O8, and it was a document-internal
inconsistency rather than a design question, so it is fixed here rather than put to the
user.]** The two lists in the record differ in exactly one name and are incomplete in the
same way, and H.8 already settles the other half:

1. **The one real difference is `FREE` versus `RUNNING`.** This section's earlier list read
   `FREE, READY, BLOCKED, MIGRATING, DONE`; DESIGN §7 D:912 gives `READY, RUNNING, BLOCKED,
   MIGRATING, DONE`. **Neither name is wrong and neither list is complete**: a slot that
   holds no context is `FREE` (it is what H.3's "creating one is a slot write" writes into,
   and what H.9 counts when it reports occupancy), and a context actually issuing in the
   barrel is `RUNNING` (it is what H.2's `C >= W(Dp + L/I)` sizes for). **The canon list is
   the union: FREE, READY, RUNNING, BLOCKED, DONE.** Nothing at any tier asserted the two
   were alternatives; each source simply omitted the state the other kept.
2. **`MIGRATING` is not a state of a context slot, and H.8 is the authority.** H.8's rule
   is that the slot is **released at departure, before the fabric is even asked** — so a
   migrating context occupies nothing on the tile it left and does not yet exist on the
   tile it is going to. That is the definition of a transition, not of a state. **It is
   recorded as the FREE-ward edge out of READY or BLOCKED**, and the deadlock H.8 cites
   (124 of 124 occupied contexts on one tile migrating) is exactly the failure that
   treating it as an occupying state produces.
3. **The transitions, which no tier stated and which are what makes this a machine rather
   than a list of names:** `FREE → READY` on context creation (a slot write — `FORK`
   arriving, or a migration landing); `READY → RUNNING` on issue selection; `RUNNING →
   READY` on yield, which is every cycle, because the barrel issues one instruction per
   context and then yields (H.2); `RUNNING → BLOCKED` on the one outstanding miss H.4
   allows, or on a full atomic table (H.7, R15's "sleep the context until possible");
   `BLOCKED → READY` on fill or on the atomic becoming available; `RUNNING → DONE` on
   `END`, with or without the return bit; `DONE → FREE` when the result has been returned
   to the FTU or discarded. **The migration edge leaves from READY or BLOCKED and goes
   straight to FREE**, per H.8.

**What is still a measurement rather than a statement:** O.4's `cycles by context state`
is the instrument that shows which of these a context actually spends its time in, and it
has not been taken. **That is a thing to measure, not a thing to argue about** — and it can
now be reported against a named list.

### H.4 One outstanding load per context — a deliberate policy

*User #239, 2026-09-01T06:27:51Z:* "If we have multiple pending loads per context, we
need to both identify which is which and ensure the regfile is coherent. We don't have
a staging area for all these returning loads ... **I would opt for 1 outstanding
miss.** Likely we need a D-buffer, but **it just stores one slot, the data that will be
used when it wakes.** We can revisit later and size it up to 2, 4, or even 8 slots, but
**I doubt it will make a difference, just adding excessive state and complexity with no
benefit.**"

**This is the canon.** With one outstanding load there is nothing to disambiguate and
nothing to keep coherent. **All memory-level parallelism then comes from context
count**, so the `L/I` term in H.2 is paid in full per miss rather than amortised.

**The direct consequence for measurement, and it must never be dropped: §21.3's 5.67×
was measured on the ChampSim core model, whose contexts kept issuing past an
outstanding load and got intra-function MLP from an in-order scoreboard. The two core
models have different MLP at the same context count. Any comparison must say which
core produced the number.** (I8's caveat; DESIGN §25.4 D:2447-2453.) See Part M.

**The one-slot data buffer.** Its purpose is **not correctness** — the context is
always asleep when its load returns, so a fill could write the register file directly —
but **decoupling the fill from write-port contention with executing contexts**. If the
register file is given a dedicated fill port, the buffer can go away. ≈10 bytes per
context.

**The line store behind it is derived, not chosen.** A context may have at most one
load in flight, so `C` contexts need at most `C` lines of staging — the
one-outstanding-load rule is what bounds it. At 64-byte lines: 2 KiB at 32 contexts,
16 KiB at 256 (**DESIGN §25.5 D:2462-2465**, verbatim — "*Behind both buffers is a small
line store, and its size is not a free parameter … need at most `C` lines of staging --
the one-outstanding-load rule is what bounds it. At 64-byte lines that is 2 KiB at 32
contexts and 16 KiB at 256*"). `[CITATION ADDED — the derivation was given here and its
source was not. **256 is the derived context count (H.2, §25.2); the 32 is an
illustration, and the value is **configuration — see SELECTED CONFIGURATION** (user ruling 2026-09-02 R6–R10).]` **It is not a data cache**; it is where the line a load pulled in sits
until the context wakes and takes its value out of it. It is **shared**, so contexts
can evict each other's lines; and the **per-context data buffer above it stays**,
because it holds a value bound for a named register, which is a different thing from a
line held by address.

### H.5 Fetch: a per-context slot, and a shared BTB

**Per-context single-entry fetch buffer**, filled with the context's next instruction
**at the end of each dispatch**. Why per-context rather than a shared prefetch queue —
the only reason that matters: **a dedicated slot cannot be evicted before its owner is
rescheduled, and a shared one can.** Cost ≈13 bytes per context (one instruction word,
a PC, a valid bit).

This makes a **sequential** fetch free when it hits: the next address is known at the
previous dispatch, a whole re-issue window before it is needed, so the access completes
underneath the window. A **control transfer** differs only in *when* the target becomes
known — not at dispatch but when the branch resolves — so without help that fetch starts
late and the context pays the refill.

**A small SHARED branch target buffer removes that, and it is not speculation.**
*User #245, 2026-09-01T16:42:31Z:* "you are fetching the next instruction at the end of
each instruction's dispatch right? We know where the next instruction is (unless it is
a branch), so we can fetch the proper instruction and have it waiting? For that matter,
**a small shared BTB would likely make it nearly 100% accurate and we don't even stall
fetch on branches.**"

What the BTB supplies is **a fetch address a window earlier**. **Nothing executes on
the answer.** A wrong prediction means the buffer holds the wrong instruction, and the
cost of that is exactly the refill a machine with no predictor pays *every* time.
**Being wrong is free; being right is a saved bubble** — an asymmetry available only
because no instruction is ever issued speculatively.

**Shared, not per-context** — that is the whole economy of it. Every context runs the
same replicated code, and a tile runs a handful of distinct functions, so one entry
serves every context executing that branch, the structure is warm almost immediately,
and **it does not scale with `C`**. A per-context predictor would multiply by the
context count and be cold on every invocation — the wrong shape twice over. 64 entries
of tag + target + a last-outcome bit ≈ 1 KiB against 22 KiB of context state at 256
contexts. [UNSOURCED AT TIERS 1–3 — MODEL-AUTHORED, and flagged here because the rest of
this document flags transcriptions upward from tier 4 and this one was not. **No
session-log item and no DESIGN.md line states a BTB entry count**: DESIGN's only BTB
mention, D:3205, describes the mechanism and gives no size. **The 64 comes from tier 4** —
`NMFCTile.h:141` (`"btbEntries", "Shared branch target buffer entries; 0 disables it
(§25.3)", "64"`) and `NMFCTile.cc:31` — and tier 4 never decides anything; the same 64
appears in Appendix 2 at the tier-4 line "a shared 64-entry BTB". The **≈1 KiB** and the
comparison against 22 KiB are arithmetic on that unsourced 64. **Treat the SHAPE
(shared, small, does not scale with `C`) as canon and the NUMBER as a tier-4 default —
which, under R6–R10, is not something a ruling would fix anyway: an entry count is
CONFIGURATION. See SELECTED CONFIGURATION.** The 2026-09-03 ruling **O12** adds one
bimodal bit per entry and changes the indexing to block granularity; **it does not fix the
count either.**]

**Reconciling this with #238's "no BTB":** #238 (2026-09-01T06:19) ruled out a branch
predictor and speculative fetch; #245 (2026-09-01T16:42), **later the same day**, adds
a small shared BTB used purely for fetch address generation. Newest wins, and the two
are compatible because the BTB is not predicting *execution*. #291 item 6
(2026-09-02T12:37) confirms it in the newest statement of all: "**BTB is used in nmfc
cores to queue up fetch in a private per-context slot?**"

**Instruction-side stream prefetch is ADOPTED, in one specific form, and it belongs here.**
The BTB above supplies a fetch address one window early; the user's #77 goes further and
names how to prefetch *streams* — **EIP (easier, needs none of this hardware), or FDIP
built on this same shared BTB re-encoded at BLOCK granularity plus a simple bimodal
predictor.** "I-cache prefetching is probably far-more pallatable than D-cache,
particularly for these workloads."

`[RULED — user ruling 2026-09-03 **O12**, verbatim: "**I think bimodal is fine, since it
only ever speculatively issues a single fetch, never executes. It is also essentially free
(built into the btb table, tracks a particular branch).**"]` **The BTB described above is
re-encoded at BLOCK granularity and carries ONE BIMODAL BIT PER ENTRY.** That is the whole
addition. It is what H.1's narrowed clause — *no predictor in the **execution** path* — was
narrowed to permit.

**Three limits, and they are the ruling's own words turned into requirements:**
1. **It issues exactly ONE speculative fetch.** "*Only ever speculatively issues a single
   fetch*" — not a run-ahead stream, not a depth-`k` prefetch chain. One.
2. **It never executes.** "*Never executes.*" Being wrong still costs only the refill a
   predictor-less machine pays every time — the asymmetry above is unchanged and is what
   makes this safe.
3. **It is free because it is not a new structure.** "*Essentially free (built into the btb
   table, tracks a particular branch)*" — **one bit per BTB entry.** The shape rules above
   still hold: **shared, small, and it does not scale with `C`.** A separate bimodal table
   indexed independently of the BTB is **not** what was ruled and gives up the reason it
   was ruled in.

**Still unbuilt in both implementations**, and the entry count is still the tier-4 number
flagged above. **The never-mispredicts caveat on the measured numbers is untouched** —
see H.1. Full quotation and the matching D-cache rules are in **D.6**.

**The instruction cache is real and must be modelled.** *User #250,
2026-09-01T17:38:01Z:* "**100% instruction hit rate is not an invariant. that is
something you fabricated.** The instruction cache hit rate will likely be 100%, but we
should be modelling a cache. **Anything else is frankly inexcusable. Instructions live
in memory, a cache backs it. Instructions are just like regular fetches.**"

### H.6 Stores

**Stores do not sleep the context** — a store returns no value, so there is nothing to
wait for and nothing to disambiguate. **But an invocation cannot retire until its
stores have landed.**

The one ordering hazard is narrow and must be stated narrowly: **a context must observe
its own stores in program order.** Sharing between contexts is the **atomic table's**
business, not this one (H.7; one structure, user ruling 2026-09-02 R15). *User #240, 2026-09-01T06:31:37Z:* "We might run into
store-forwarding problems, but that is something that should theoretically never
happen? Assuming there is some sort of memory-ordering enforcement as the loads/stores
pass through to the LLC. **If that is not the case, we need a write-buffer as well.**"

The design's position: meet it **structurally** — one slice, requests already serialised
by the issue pipeline — and state as a **requirement on the slice** that *the tile's
memory path must process same-address requests in arrival order*. That is a thing to
**test when the slice is built**, not an assumption to make. The fallback if it cannot
be met is a store buffer with an address match, which is a real cost to avoid.

*(Tier 3, and it says all of the above in the same order: **DESIGN §25.5 D:2477-2496**,
"Stores, and what the memory path must guarantee" — including the reason the requirement
is narrow, "*because sharing between contexts is the lock table's business (§25.6), not
this one*" — **"the lock table" there is the atomic table, one structure (R15)**, and that it is "*stated here as a requirement on the slice, not as an
assumption about one, and it is the thing to test when the slice is built*".
`[OMISSION CORRECTED — H.6 previously stated a requirement on the slice with no source at
all, while its title-matching tier-3 section was one of the sections never cited.]`)*

### H.7 Atomics — enforced, and practically free. THE ATOMIC TABLE IS ONE STRUCTURE.

*User #238, 2026-09-01T06:19:05Z:* "Regarding atomics, **we enforce them, but they are
practically free. Atomics are supported instructions, and we have a unified atomic
table that enforces atomic relationships. Otherwise, if we just blatantly remove the
atomics, we must assume atomicity for all operations which could be devastating.**"

**THERE IS ONE ATOMIC TABLE. "The unified atomic table", "the lock table" and "the
held-word table" ARE THE SAME OBJECT, and this document now uses only the first name.**
`[RULED — user ruling 2026-09-02 R15, verbatim: "**Yes, the same structure.** … it must
enforce atomics. **It allows contexts to obtain/release/pass atomics quickly.** Capacity is
something that must be sized from experimentation. **Full table must either be
unachievable by construction or safely block (sleep context until possible).** … chain
bound must be experimentally derived." Ledger L48 is closed.]`

**What it is for, in the user's own three verbs: OBTAIN, RELEASE, PASS — quickly.** That is
the whole job. It enforces atomic relationships, and it makes acquiring, releasing and
handing off an atomic cheap enough that ownership rather than a protocol is what provides
atomicity.

**Its CAPACITY is configuration** — "sized from experimentation" — and so is the **hand-off
chain bound**, "experimentally derived". Neither is a design constant; **see SELECTED
CONFIGURATION** for what each implementation currently sets (ChampSim's `lock_waiters_` is
unbounded and uncounted; SST's `maxAtomicForwards` is 8).

**What a FULL table does, and this IS design.** It must be **either unachievable by
construction, or it safely blocks — the context sleeps until an entry is available.** Those
are the only two permitted behaviours, and the reason is I.1: a table that refuses an
atomic while the context keeps its other resources is a resource held while waiting for a
resource, the shape that deadlocked this machine at cycle 9,100,426. **Sleeping is not
"blocking" in I.1's forbidden sense** — a sleeping context has yielded its issue slot
exactly as a context sleeping on a load has, which is the machine's normal state (H.4);
what is forbidden is spinning while holding.

**Removing atomics is rejected:** it would not make them free, it would make atomicity
an *unchecked assumption* on every operation, which is unsound for anything not
actually owned.

**They must leverage the core, not the memory system.** *User #252,
2026-09-01T17:48:45Z:* "**Stop. If you just implement against memHierarchy, you are
using regular atomicity instead of leveraging the core. Why do you keep doing that?**"
LR/SC or a locking read through the memory system is the wrong mechanism: what makes
atomics nearly free here is **ownership** — a context migrates to its data, so every
atomic is local to one tile and the table needs **no cross-tile protocol**.

**Two tiles cannot hold atomics for the same line, by construction** (user #262,
2026-09-01T19:18:52Z), because the tiles are address-partitioned. A test that tries to
exercise "atomics across two tiles" is testing something the machine cannot express.

**Granularity: lock the operand, not the line.** An atomic updates one word, and two
atomics on different words are independent even when they share a line — there is no
coherence to preserve, because the block lives on exactly one tile and is touched by
exactly one core. **Locking the whole line instead made a line's worth of unrelated
counters contend for a critical section that spans a memory round trip, which is what a
graph kernel's `parent` array looks like** (`src/nmfc/function_core.cc:971-983`).

**Park, do not spin.** A retrying context still costs an examination slot every cycle,
and the issue loop examines at most as many entries as were queued when the cycle
began — so a ready queue full of contexts spinning on one hot block spends the whole
budget on contexts that cannot issue. **That makes contention superlinear rather than
merely serial.** Wake on release instead.

**Hand off with the value.** The holder passes both the lock *and* the value it already
has to the next waiter, so the waiter never refetches; ownership passes without the
lock ever becoming free, which is also what keeps the forwarded value correct — no
third context can slip in and change the address in between. **This is the whole point
of doing atomics at the memory: a queue of updates to one address costs one fetch and
then one ALU pass each, not a round trip apiece.**

*User #71, 2026-08-28T06:28:18Z, chose both halves of this when asked:* lock the word
rather than the enclosing block, **and** forward the resident value along the wait
queue so one fetch serves N queued read-modify-writes.

**THREE FACTS ABOUT THE HAND-OFF CHAIN THAT MUST TRAVEL WITH THE PARAGRAPH ABOVE.**
`[OMISSION CORRECTED — the sentence "a queue of updates to one address costs one fetch
and then one ALU pass each" was stated with no bound, no read-through rule and no
provenance, which is precisely what ledger L10 legislates against. All three are in
DESIGN and none had reached this document.]`

1. **The chain is BOUNDED, and the bound is a COHERENCY GUARANTEE, not an
   optimisation. The VALUE of the bound is configuration — "experimentally derived"
   (R15) — but its EXISTENCE is not negotiable.** DESIGN §25.6 D:2524-2530: "*A word passed context to context is a word
   the rest of the machine cannot see, so after a fixed number of hand-offs it goes back
   to the data cache whether or not anyone is still waiting.*" In the sixteen-invocation
   measurement the two writebacks are exactly that: "**a chain of eight, a writeback, a
   chain of six, a writeback**". **Without the bound, a steady arrival of contexts could
   hold a word out of the hierarchy indefinitely.** Build the counter.
2. **An ordinary load or store to a held word GOES THROUGH IT** (D:2536-2545). While a
   word is held the cache's copy is stale, so an access served by the cache would read
   *around* the atomic — and the window is not small, because the word can pass context
   to context without going back at all. **The entry therefore outlives its own
   writeback:** it is removed when the write lands, and if anything touched it in the
   meantime **the write is repeated**, so there is no moment when the value is neither in
   the tile nor in the cache. Measured: twelve invocations each doing an atomic add and
   then loading the same word cost **one memory read**, and **all twelve loads were served
   from the held copy**.
3. **Waiting for a second word while holding a first is REFUSED, not handled**
   (D:2547-2551). It is a resource held while waiting for a resource — I.1's shape, the
   one that deadlocked the machine at cycle 9,100,426. It can only arise from a function
   that takes a second word without releasing the first, so **the machine diagnoses it
   rather than adding a protocol to survive it.**

`[CAUTION — the hand-off's headline measurement is a CONTENDED COUNTER, and it does NOT
fire on the real workload. Quote it with its shape, per L10.]` DESIGN §27.3 D:3152-3160:
§25.6's "sixteen invocations, fourteen taken by hand-off" was "**a measurement of a
contended counter, which is not what a traversal mostly does**". **On BFS: 36,863 atomics
and TWO hand-offs.** Nearly every atomic is on a *different* word (`parent[n]` for
distinct `n`), so there is no chain to pass along, and the one genuinely shared word (the
frontier counter) is a small fraction of the total. **The optimisation is real and the
earlier measurement was not wrong — but "a queue of updates to one address costs one
fetch" describes a case this workload almost never presents.** Do not size or justify the
mechanism from the 16/14 figure.

**THE HOST↔TILE COHERENCE HOLE, AND THE THREE-PART FIX IT REQUIRES.**
`[OMISSION CORRECTED — H.7 previously specified the held-word mechanism with NO directory
obligation at all. A rebuilder following it builds the exact state that silently
triple-expanded every BFS level while passing its tests. **"The held-word table" below is
the atomic table under an older name — one structure, R15.**]` DESIGN §27.4 D:3164-3172:

> "The held-word table (§25.6) keeps an atomic's word **above** the data cache on purpose,
> and **nothing connected it to the directory.** Two tiles cannot contend for a line by
> construction — **but a host and a tile can, and here they did**: the host writes
> `frontier_count = 0` between levels, the write is applied to a copy no tile is reading,
> and **the tiles keep incrementing a stale value. Every level was expanded roughly three
> times over.** The traversal was still *correct* — every vertex agreed with the reference
> on whether it was reached — **which is what made it worth catching: a wrong count and a
> right answer is exactly the failure that survives a test suite.**"

**Three requirements, and the second and third were only found because the first was not
enough** (D:3174-3189):
1. **The tile is told when a line is snooped away.** The cache pushes the snoop up to its
   client, which hands back the word it was holding; the cache patches it into the line
   before answering. **A leaf cache does this only when `notifyClient` says its client
   keeps state above it** — so the flag is part of the design, not a memHierarchy detail.
2. **A held word's line is PINNED in the cache.** "*Releasing on snoop is useless if the
   line was quietly evicted first: the directory then no longer believes this agent holds
   anything, and the snoop never comes.*" **Pinning rides on the request that fills the
   line, so there is no window between the two.**
3. **A pin is a REQUEST, not a right.** Pinning every atomic's line filled whole sets and
   a fill then had nowhere to land. **A set with no free way asks the client to give the
   oldest pinned line up, and the fill retries.** "*Without that the cache wedges on a
   workload doing atomics to colliding addresses — **which is any workload doing enough
   atomics**.*"

**I14's strict priority and the atomic table are one mechanism, not two.** Keeping a
word above the data cache is only sound if the directory can reach it — which is why R19's
"make sure it is not a bottleneck" is a correctness requirement on the directory and not a
tuning preference (C.5).

**Coherency obligation on the data buffers.** *User #251, 2026-09-01T17:47:36Z:*
"non-atomics can conflict all they want, but **we do need a guarantee of coherency (so,
a context must not retain it's data-buffer bytes indefinitely, and must eventually go
back to the data cache). Atomics must be exclusive, which is where the data buffers can
already help us.**"

### H.8 Migration mechanics on the core side

- **The slot is released AT DEPARTURE**, before the fabric is even asked. A context's
  whole state is its register file; once it has decided to leave, that state is a
  *message* and does not need a tile slot to sit in. Holding the slot until the fabric
  accepted is **hold-and-wait**, and it deadlocked exactly as that always does.
  **Measured at cycle 9,100,426: four tiles at 0–1 free contexts, 983 tokens waiting,
  and 124 of 124 occupied contexts on one tile in the migrating state.**
  *User #209, 2026-09-01T01:05:18Z, stated the rule independently:* "**Migration must
  release it's slot when moving. By necessity, if a tile is full, it must either finish
  or migrate elsewhere. Deadlock should not be possible.**"
- **An age guarantee is not a substitute for releasing the slot.** Reserving a slot for
  the oldest waiter admits one context into a full tile but promises nothing about that
  context ever leaving, so the reserve is consumed once and gone.
- **Do not stop at the head of the outgoing queue.** A refusal says one destination is
  congested, not that the fabric is; stopping would let a context bound for a full tile
  pin every context behind it that has somewhere to go. **Three stages each needed the
  same fix: the core's outgoing deque, the fabric's queues, and the destination's
  context array.**
- **Queue per destination, not one shared queue.** A single shared queue lets one
  congested destination own all of it — **measured, 128 of 128 entries bound for one
  tile that had 0 free contexts**, and separately **128 queued invocations for a full
  tile 0 while tiles 1–3 sat at 1024/1024 free**.

  **AND PER CLASS, WITHIN EACH DESTINATION — RULED.** `[RULED — user ruling 2026-09-03
  **O5**: "**a.**" The one fabric carries **three** message classes — **COHERENCE,
  MIGRATION, FILL** — and the per-destination queues this bullet establishes are per class
  per destination. **COHERENCE is arbitrated strictly first** (I14 makes NMFC priority an
  order, and a coherence response the order depends on may not sit behind a fill);
  **MIGRATION and FILL are arbitrated at EQUAL WEIGHT**, which is the precondition of
  invariant 11's byte parity. Full statement, with both halves' reasons, at **C.5a**;
  the parity dependence at **J.2**.]` **The measured failure this bullet records is the
  same failure one class down:** one congested destination owning a shared budget. Splitting
  by destination fixes it across tiles; splitting by class fixes it across traffic types, so
  a burst of fills cannot starve the migration that would have freed the tile the fills are
  queued behind.
- **Drain returns first.** A completion frees a context on some tile; an invocation
  waiting for that tile would otherwise spin against a full core that the very message
  behind it was about to drain.
- **The PC does not change on migration.** A function's code is one virtual page
  aliased to one physical copy per channel, so the same address resolves to whichever
  copy lives on the arriving tile. **A context therefore never migrates for an
  instruction fetch, and there is no per-tile bias to apply to its program counter.**

  `[SUPERSESSION, PREVIOUSLY UNRECORDED — this bullet REVERSES three live DESIGN
  sections, and the canon stated it as if it had always been so. It is right on
  authority; it was wrong to state it silently.]`

  **THE RETIRED MECHANISM: `entry_pc_base + t · G`.** DESIGN, in three places, all
  outside the superseded §5.1-§5.7 band except the third:
  - **§8, D:958:** "*`aux0` is the entry PC **base**; **the dispatcher adds `t · G`**.*"
  - **§7.1, D:931:** "*The code entry is worse than unusable — **the context's
    instruction VA literally *changes* on migration**, because it now runs copy `t'` at
    **`entry_pc_base + t' · G`**.*"
  - **§5.5, D:663:** "*If a function's N copies sit on **N consecutive huge pages**, copy
    `t` lands on channel `t` automatically under page interleaving, and **the dispatcher
    forms `entry_pc_base + t · G`. Choosing the tile is one add on the dispatch path — no
    translation, no lookup**.*"

  **THE AUTHORITY, and it is tier 2 against tier 3, so it wins.**
  `src/nmfc/function_fabric.cc:115-121`, verbatim: "*The entry PC is **not** adjusted. A
  function's code is one virtual page that the OS aliases to a copy on every channel, so
  the same address resolves to whichever copy the destination tile owns. **Adding a
  per-tile bias here was the old layout, where the copies were N distinct virtual
  pages**, and under aliasing **it sends the invocation to an address that is not its code
  at all**.*"

  **WHAT CHANGED, in one line: N virtual pages became ONE virtual page aliased to N
  frames.** Under the old layout the tile was encoded in the *address*; under aliasing it
  is resolved by *translation*. Everything the two models disagree about follows from that
  single change.

  **THREE CONSEQUENCES, each of which a reader will otherwise re-derive from DESIGN:**
  1. **`aux0` in a `CALL` record is the entry PC, full stop — not a base to be offset.**
     A dispatcher that adds `t · G` under the current layout jumps outside the code page.
  2. **§5.5's "one add, no translation, no lookup" is DEAD, and F.1's "translate, then
     route" is what replaced it.** Placement is no longer free on the dispatch path; it
     costs a translation. Do not quote §5.5's cheapness argument.
  3. **It is what makes I5's legitimacy clause coherent.** #291's "*any migration due to
     instruction fetch or translation is by construction wrong*" is only *achievable*
     under aliasing — under `+ t·G` the instruction VA changes on every migration by
     construction, so fetch-induced translation work is unavoidable and the clause could
     not be met. **The newest tier-1 requirement presupposes the newest layout.**

  Part P carries the retired mechanism as **R110**.

### H.9 Occupancy is the instrument, and the tile is usually not the constraint

Measured at four tiles, 128 contexts each, 64-entry FTU, on a workload built to
saturate the machine (DESIGN §31.3 D:3631-3638):

| structure | mean while in use | peak | |
|---|---:|---:|---|
| **host tracking unit (FTU)** | **63.61 of 64** | 64 | **full 98.5% of the time it was in use** |
| fabric control queue | 0.08 of 64 | 5 | zero refusals |
| tile 0 contexts | 5.37 of 128 | 18 | |
| tile 1 contexts | 13.27 of 128 | 53 | |
| tile 2 contexts | 12.57 of 128 | 35 | |
| tile 3 contexts | 5.61 of 128 | 24 | |

**The tracking unit is 99% full while the tiles are 4–10% full.** Sweeping contexts per
tile 64 / 128 / 256 / 512 produced **identical** results in every statistic the tiles
emit — 11.3048 ms, 524,288 loads, 396,161 migrations, 2,625,144 instructions at every
point — and again at 4× the access set on a different build: **136.143 ms at 64
contexts and 136.143 ms at 512.**

**The reading this document previously drew from the table — "the tracking unit is the
concurrency of the whole machine" — is a TIER-3 inference from DESIGN.md measurements,
and TWO TIER-1 STATEMENTS SAY IT IS THE WRONG READING OF A FULL FTU. Both were absent
from this document. Tier 1 governs.**

*User #180, 2026-08-29T19:32:15Z, verbatim and directly on this:*

> "**1. Despite the FTU being full, most of it is still pending results. A larger FTU
> doesn't fix that problem. The work is being done, the standard core is not consuming it
> fast enough. That is not a problem with the functions, it is how the functions are being
> positioned in the code.** Again, don't blame migration yet, it is clear that occupancy
> is still low, and migration bottlenecks should still see high occupancy. **What we are
> looking at is a torrent of completing results with no consumer ready to process them and
> launch new ones.**"

*And #171, 2026-08-29T08:55:35Z, rejecting the inference in one line:*

> "**that makes no sense. if the tracking unit is capped, then we should see 1024 contexts
> somewhere out on the tiles. We don't.**"

**The corrected conclusion, which fits both the table and the user's words:**
1. **A full FTU is a symptom, and the diagnosis is what the entries are DOING.** Entries
   *outstanding* would mean the tiles are the constraint. **Entries holding COMPLETED
   results waiting for a `JOIN` means the host is not consuming, and the tiles are idle
   because nothing is being launched.** #180 says the measured case was the latter.
   **Instrument the split — outstanding vs returned-and-unjoined — or the occupancy
   number cannot be interpreted at all.** This is O.1's own rule applied to the FTU.
2. **"A larger FTU doesn't fix that problem."** Enlarging a unit that is full of finished
   work buys nothing; it enlarges the backlog.
3. **The fix named by tier 1 is the CODE'S SHAPE** — "how the functions are being
   positioned in the code", i.e. fork/join placement and batch shape (K.2, R35: the join
   landing one instruction behind the fork holds the machine at one invocation however
   much room it has). **Not the FTU's size.**
4. **A structural fact does survive:** a host-issued call does occupy a host-side entry
   until it retires, so the FTU **bounds** in-flight invocations. **Bounding and binding
   are different claims.** #171 gives the falsifier for "binding": if the FTU were the
   cap, contexts would be piled up on the tiles. They were not.

`[THE STRESS WORKLOAD IS SETTLED — user ruling 2026-09-02 on L32. The table above is from
the run's REJECTED FIRST SHAPE and its tile column, 5.37 / 13.27 / 12.57 / 5.61, is the
2.5× spread that prompted the reshape. **That spread was not a machine defect: it was
predicted exactly from the addresses (12.5 / 37.5 / 37.5 / 12.5), i.e. congruent routing
doing its job on a workload shaped wrong** — chase decomposition, K.3.]`

**The reshaped workload measures 25.0% on every tile — loads, migrations and instructions
all four-way even, zero stores, 196,904 migrations for 262,143 loads, and the sum verified
against the host.** The reshape: the host resolves seven indices into the 512-bit context,
the function does seven data loads and returns the sum via **END with the return bit**, and
the `FORK.R`/`JOIN` ring is the size of the FTU. **Full account in G.6.**

**The FTU / tile-occupancy READING is unaffected by the reshape**, which is why this table
is kept: the tracking unit was full while the tiles were 4–10% full under both shapes, and
that is the finding. **The invariance and occupancy numbers are settled evidence**, quoted
with their build and binary (working tree at HEAD `21df518d`, `tools/nmfc/kernels/bfs_nmfc`
and `bfs_base`, uncommitted — G.6). **The "provisional" caveat this section used to carry
is removed.**

**And the binding constraint is workload-dependent.** On BFS the FTU is 23.20 of 64,
peak 24, **never full** — its limit is its own dependency structure, a
level-synchronous frontier where the next level cannot start until this one finishes.
**Two workloads, two different binding constraints, and the run time alone
distinguishes neither.**

### H.9a The discriminator this Part demands WAS ALREADY RUN, and it came out "not the FTU"

[OMISSION CORRECTED — DESIGN §20.1 (D:1585-1690) was absent from this document in its
entirety. H.9 point 1 and O.1's `[CAUTION] both order an outstanding-vs-returned split
"before concluding anything", and neither records that the split had already been taken
and had already answered.]`

DESIGN §20.1 D:1591-1597, verbatim:

> "**`ftu_size` is not a concurrency limit.** `FTU IN FLIGHT peak: 1024 of 1024` looks
> like a saturated tracking unit, but **`DISPATCH STALLS: 0` says it never once gated a
> fork**, and **peak live bodies was 665, not 1024**. The FTU holds an entry from fork
> until *join* ... Most of those slots are finished work waiting to be collected.
> **Occupancy of a structure that holds results is not a measure of running work, and
> raising it does nothing.**"

**Three counters, and together they are the split H.9 and O.1 ask for:** `FTU IN FLIGHT`
(1024/1024 — the queue is full), `DISPATCH STALLS` (**0** — nothing ever waited for an
entry, so the unit never refused a fork), and **peak live bodies (665)** — the work
actually running. **A full unit with zero dispatch stalls is the signature of
returned-and-unjoined, not of outstanding**, which is exactly what #180 said and what
#171's falsifier predicts. **Instrument `DISPATCH STALLS` and peak live bodies alongside
occupancy; those three answer it without an ablation.** (Note ledger L21: ChampSim's
`DISPATCH BLOCKED` statistic is a live bug and always reports zero — the *retry* count on
the same line is the real one.)

**A structural caveat that reinforces it, not a counter-argument:** D.5 records that the
headline config sizes the FTU at **1024 against 4096 context slots**, so a full FTU there
is partly an artefact of a 4:1 undersizing its own generator warns about. **Both readings
say the same thing: do not conclude "the FTU binds" from fullness.**

### H.9b Concurrency's real limit on the measured workload: THE BATCH BARRIER, and the fix that worked

`[OMISSION CORRECTED — N.6 carried only the barrier's badness and R34 only the deeper
ring's failure. The measurement that fixed it, and its control, were absent.]`

**DEFINITION — WHAT "THE RING" IS, because this document prescribed it by name in five
places and defined it in none.** [ADDED. `grep -nE '\bring\b'` returned nine hits — R33
"**Use a ring**", R34 and I.2 "ring depth 4096", H.9b, N.6, O.1a, Appendix 2 D7 — **and not
one of them said what the structure is.** A fresh engineer had the measurement, the
control and the failure mode, and no statement of the thing to build. Compare
`pull_dominance` and `sweep`, both of which now carry formal definitions.]

> **THE RING IS A HOST-SIDE SOFTWARE PATTERN IN THE KERNEL'S FORK LOOP. IT IS NOT
> HARDWARE.** Nothing in `src/nmfc/` changes between the two shapes; the machine is
> identical. What changes is **how the host decides when to fork the next invocation.**
>
> **The structure it rings over is the OUTPUT SLOT POOL** — `LEVEL × SLOT` words of
> memory, one `SLOT` per outstanding invocation, into which a memory-committing function
> writes its result (`bfs_nmfc.cc:583`, `pool_bytes = LEVEL * SLOT * sizeof(NodeID)`).
> **"Depth" counts SLOTS, which is the same thing as the number of invocations the host
> keeps outstanding.**
>
> | | **BARRIER** (rejected, R33) | **RING** (the fix) |
> |---|---|---|
> | fork | `LEVEL` invocations | one, whenever a slot frees |
> | then | **wait for ALL of them** (`harvest()`) | **retire exactly ONE — the oldest** (`retire_oldest()`) |
> | outstanding over time | sawtooth: `LEVEL` → 0 → `LEVEL` | **flat at `LEVEL`, continuously** |
> | source | `bfs_nmfc.cc:388-390` | `bfs_nmfc.cc:394-396` |
>
> Both shapes build from **one source**: `EXTRA_CXXFLAGS=-DNMFC_BATCH_BARRIER` selects the
> barrier. The ring walks the pool with **two rotating pointers**, `fork_at` and
> `retire_at` (`:344-345`), deliberately **not** an index and a `%` — signed modulo touches
> the return register right after the call, and the annotation pass reads any post-call use
> of that register as retrieving a return value, inventing a second `JOIN` per invocation
> for a `void` function (`:337-342`; the same trap is recorded at I.2).

**AND `LEVEL`, `NMFC_LEVEL` AND "RING DEPTH" ARE ONE QUANTITY UNDER THREE NAMES.**
[DISAMBIGUATED — H.9b previously offered "1024 is the measured best of the two points
taken" while the table immediately above it compared barrier against ring, not two depths;
and R34 said "ring depth 4096" while H.9b said "`NMFC_LEVEL` … 4096", never equating
them.] `bfs_nmfc.cc:237-240`: `#ifndef NMFC_LEVEL / #define NMFC_LEVEL 1024 / #endif` then
`static constexpr int32_t LEVEL = NMFC_LEVEL;`. **`NMFC_LEVEL` is the build parameter,
`LEVEL` is the constant it defines, "ring depth" is the prose name, and all three mean *how
many invocations stay outstanding*.** It is a **compile-time constant of the workload**,
not a simulator config key — re-sweeping it means rebuilding the kernel.

DESIGN §20.1 D:1631-1645. Same scale-20 graph, same trace pipeline, same config; both
shapes build from one source (`EXTRA_CXXFLAGS=-DNMFC_BATCH_BARRIER` selects the old one),
so the comparison is reproducible:

| | barrier | ring |
|---|---:|---:|
| queue empty | 51.7% | **38.4%** |
| aggregate bandwidth | 31,149 MB/s (41%) | **39,063 MB/s (51%)** |
| cycles | 60,119,717 | **49,276,407 (−18.0%)** |
| host instructions | 12,218,028 | 12,092,046 |
| context occupancy | 20.6 / 39.1 / 20.2 / 29.6 | 19.5 / 24.6 / 19.4 / 20.9 |
| **migrations** | **31,217,817** | **31,245,056** |

**18% fewer cycles on 1% FEWER instructions, and the occupancy sawtooth flattened.**

**The migrations row is the CONTROL and it is the most important row in the table:**
"*migrations unchanged — **which is the control: batching never caused them, so a batching
fix must not move them**.*" **Any future concurrency fix that moves the migration count
has changed something else too, and its result is not attributable.**

**And the order of operations, stated by DESIGN and binding on anyone re-tuning this**
(D:1621-1623): "*the order is: **overlap the batches first**, which is where the idle time
lives and is a fork-pattern change in the kernel; then raise `num_contexts` to stop the
in-wave clipping. **Read the result off the queue-empty fraction, never off cycles
alone.***" `NMFC_LEVEL` is a build parameter precisely so this can be re-swept.

**THE DEPTH SWEEP IS A SEPARATE EXPERIMENT FROM THE TABLE ABOVE — two points, both with
the ring** (DESIGN §20.1 D:1659-1670):

| | **ring, `LEVEL` 1024** | ring, `LEVEL` 4096 |
|---|---:|---:|
| cycles | **49,276,407** | 53,857,690 (**+9.3%**) |
| aggregate | **39,063 MB/s (51%)** | 35,976 MB/s (47%) |
| queue empty | **38.4%** | 43.1% |
| occupancy | 19.5 / 24.6 / 19.4 / 20.9 | 18.8 / 32.4 / 21.1 / 30.8 |
| FTU in flight | 902 of 1024 | 3418 of 4096 |

**1024 is the measured best of the two points taken**, and 4096 is **R34**. **The deeper
ring is not a failure of the ring — the mechanism worked**, keeping 3.4× more outstanding;
idle time rose anyway. **The cause is FIFO retirement:** `retire_oldest()` blocks the host
on the *oldest* invocation while thousands of finished ones queue behind it, so
head-of-line blocking grows with depth. **Depth is therefore not a lever until retirement
stops being strictly FIFO** — the two must be re-swept together, not separately.

**Why `num_contexts` is a real but secondary limit:** peak-to-mean context occupancy was
**5–11×**, two of four tiles clipped at the 256 cap — **and every channel was idle ~64% of
the time regardless of whether the tile carried 17 contexts or 45.** Little's law confirms
contexts are not the constraint *within* a wave: t2 at 16.6 contexts and 371-cycle latency
yields 0.045 refs/cycle; t1 at 44.9 contexts and 1199-cycle latency yields 0.037 — **both
about 5.8 GB/s. Adding contexts to t1 bought latency, not throughput.**

---

## PART I — THE ISA

**TWO NAMING NOTES, BOTH OF WHICH HAVE ALREADY MISLED A READER.**

1. **`I.1`–`I.10` (with a period) are THIS PART'S sections. `I1`–`I14` (no period) are
   Part B's INVARIANTS.** They are different id spaces separated by one character, and
   both are cited by bare id across the document. **ALL TEN of `I1`–`I10` collide** — the
   full table is in the front matter, and this note previously listed only three pairs and
   omitted the one that actually went wrong (`I5` vs `I.5`, which are the migration
   invariant and the tracking unit respectively). **A dropped period sends the reader to
   an unrelated ruling.** Keep the period or write it in words.
2. **`FUT` and `FTU` are the SAME STRUCTURE — the function tracking unit.** The user
   spells it `FUT` in three quoted passages, including #136, the one that establishes why
   `JOIN` may choose its destination register. **This document uses `FTU` throughout; the
   quotations preserve whatever the user typed.** There is no second structure.

### I.0 What a function core executes UNDERNEATH these instructions

`[ADDED — this Part specifies the custom instructions and previously never said what
a function core runs beneath them, while arguing invariant 7's consequences from x86-64
and encoding the instructions for RISC-V, 3,900 lines apart and with nothing reconciling
them. **RULED — user ruling 2026-09-02 R11, verbatim: "Lets do RISCV. x86 was chosen
initially since initial develop was on PIN. RISCV is easier."** Ledger L46's base-ISA half
is closed.]`

**THE TARGET MACHINE IS RISC-V. This is settled at tier 1.** x86-64 appears in this
document for exactly one reason, and it is **history of the toolchain**: development began
on **PIN**, which instruments x86-64 binaries on an x86-64 host, so the traced program is
x86-64 and every x86-64 register name in this document belongs to that trace. **It was
never the target and it is not an alternative.**

| tier | what it says |
|---|---|
| **1** | #227, 2026-09-01T04:41:17Z, the user's own implementation order, step 6: "**Compile GAPBS BFS to RISC-V + our extension and run it.**" #216, 2026-09-01T02:01:31Z: "`riscv64-unknown-elf-gcc` is now installed." Nothing in the log names x86-64 as the target. |
| **2** | The whole ChampSim toolchain is **x86-64** and says so structurally: `tools/nmfc/kernels/trace.sh:12` uses Pin, `:32-34` reads `objdump` for `lock` prefixes, `:47-49` for `ret`, `:52-62` for `push`/`pop`; `annotate.cc:219` special-cases `rip`/`rsp`/`rflags`; `:695-719` drops the x86 `ret` stack pop and x86-64 ABI callee-saved saves. **ChampSim executes trace records, not instructions, so it has no target ISA at all** — the x86-64 is the *traced program's*. |
| **3** | §23 D:1988-1990: "Under ChampSim that could only ever be a trace-record encoding; **on a RISC-V target it is real, and this is the set.**" §23.7 D:2245 reserves `custom-0`. §26.6 D:2890 builds the host on **Vanadis, an out-of-order RISC-V core**. §22 D:1981 discusses "x86-64's callee-saved register preservation" — a property of the *tool*, not of the machine. |
| **4** | `S6` states the tile executes **RV64IM+A only** — no FP, CSR, FENCE, RVC, MULH\*, ecall — while `main` on the host is full RV64G. **Tier 4 never decides anything**, and here it is **overruled**: user ruling 2026-09-03 **O4** puts `F` and `D` in the subset, so SST's tile is now a divergence (Appendix 2 **S6**), not a candidate answer. |

**Consequently, and this changes how two passages are read:**
- **Invariant 7's consequences are ILLUSTRATIONS, not properties.** "No frame pointer,
  `push %rbp` is a stack write" and "a seventh x86-64 argument arrives on the stack" are
  true statements about the *traced* x86-64 binary, which is what the admission test
  actually reads. On RISC-V the same three constraints appear as: no frame pointer
  (`s0`/`fp`), no `jal` from inside a body (it writes a return address), and **an argument
  the ABI places on the stack cannot be read** — under the *stock* RV64 convention that is
  the ninth, because it passes eight in `a0`–`a7`. **That eight is the stock convention's,
  not the machine's**: the admission test is on **bits** (I2), so nine 32-bit arguments —
  288 bits — fit the context and pass under a packed calling sequence. **The rule is "a
  function that spills cannot run"; the register names and the number eight are the
  toolchain's.**
- **K.6's "x86-64 names far more than it holds at once" is likewise about the tool.** The
  admission test is a test on **bits** (I2, §22), and it is run on the disassembly the
  tracer produced.

**THE SUBSET IS RULED: `RV64IMAFD`.** [RULED — user ruling 2026-09-03 **O4**, verbatim:
"**I think we want float, so C.**" Option (c) of that row was `RV64IMAFD`. Ledger **L46**
is now closed in both halves — the family by R11, the subset by O4.]

**What each letter is doing here, so the set is not narrowed back by someone who thinks a
letter is decoration:**

| letter | why it is in | what dropping it would cost |
|---|---|---|
| **`I`** | the base | nothing runs |
| **`M`** | integer multiply/divide appears in ordinary index arithmetic — a row offset, a hash, a stride | every graph kernel in Part N stops being offloadable |
| **`A`** | **`A` is not optional.** H.7's atomic table and R15's hand-off chain are an **architectural feature the ISA must be able to name**; an atomic the ISA cannot express is a table with no instruction reaching it | the whole of H.7 |
| **`F` `D`** | **user ruling 2026-09-03 O4.** Float is in | any kernel with a floating-point weight — PageRank, SSSP with real weights, most of the rest of GAPBS beyond BFS — is inadmissible |

**AND NOW THE CONSEQUENCE, STATED CORRECTLY, BECAUSE THE OBVIOUS READING OF IT IS THE
DEFECT INVARIANT 2 EXISTS TO STOP.** The `O4` row as it was written said floating point
"*widens the 512-bit context register file's per-context save set and must be priced
against I2*". **It does not, and the reason is the 512-BIT RULE.**

> **THE 512-BIT RULE (user #232 2026-09-01T05:44:29Z, restated as a correction at #238,
> and restated AGAIN as a correction on 2026-09-03): the context is 512 BITS, BIT-PACKED.
> It is NOT eight 64-bit registers, not eight lanes, not `x1`–`x8`, and not "the integer
> file". "*Bit-packing is the name of the game.*" "*Why do you keep reverting to that?*"**

**So there is no per-register set to widen.** A floating-point value occupies **bits of the
same 512** exactly like any other value — an `f64` costs **64 bits**, an `f32` costs
**32** — and **the compiler packs them** alongside whatever integers and pointers the
function is holding. Concretely, and all three of these are untouched by O4:
- **Invariant 2 is untouched.** 512 bits in, the same 512 bits out. The number does not
  move because floats are bits like everything else.
- **Invariant 11 is untouched.** A migration is still **72 B** — 64 bytes of context plus
  an 8-byte PC — against the 64-byte line a foreign access would have cost. **Adding `F`
  and `D` adds ZERO bytes to a migration.**
- **H.3's state cost is untouched.** 64 bytes × contexts; 64 KiB per tile at 1024.

**THE ONE THING O4 DOES CHANGE ABOUT THE FILE IS ITS NAMING, NOT ITS SIZE.** A RISC-V
encoding names `f0`–`f31` in fields that are *separate* from `x0`–`x31`, so an
`fadd.d f3, f1, f2` and an `add x3, x1, x2` reach the compiled context through **two
register namespaces**. `[derived from ruling O4]` **Those two namespaces are a naming
convention over the SAME 512 bits, not two files.** The compiler's packing decides which
bits an `f`-name and which bits an `x`-name resolve to, exactly as it already decides that
for two `x`-names of different widths. **An implementation that builds a separate
floating-point register file has built a second context, has broken invariant 2, and has
made a migration bigger than 72 bytes.**

`[THE OBVIOUS OBJECTION, ANSWERED, BECAUSE IT IS ARITHMETIC AND IT LOOKS FATAL.]`
**"RV64IMAFD names 32 `x` plus 32 `f` architectural registers. At 64 bits each that is
4096 bits. Over one 512-bit file the two namespaces must alias, so `fadd.d f3,f1,f2` and
an integer use of `x1`/`x2`/`x3` collide."** **They do not collide, and the reason is that
a register NAME is not a fixed bit offset in this machine.**

1. **The function core does not implement 64 architectural registers. It implements 512
   bits of live storage.** There is no array of 64 slots for names to index into, so there
   is nothing for `f3` and `x3` to alias *to*. What exists is 512 bits and **a per-function
   binding from register name to bit range**, produced by the compiler as part of the
   packing (#232: "*this needs to be handled compile-side*") and carried with the offload.
2. **The compiler assigns DISJOINT bit ranges to every simultaneously-live name, across
   both namespaces.** If a function holds `f1`, `f2`, `f3` (three `f64`s, 192 bits) and
   `x1`, `x2`, `x3` (three 64-bit integers, 192 bits), that is **384 of the 512 bits in
   six disjoint ranges**, and `f3` and `x3` are two different ranges because the compiler
   made them so. Names in different namespaces are simply different names.
3. **What IS bounded is LIVENESS, not the name space** — which is invariant 2, unchanged
   and not a new restriction: a function whose simultaneously-live values, **counted in
   bits across BOTH namespaces together**, exceed 512 **cannot be offloaded** (#99: "*if
   the algorithm out of necessity requires 9 regs, then it cannot be used*", restated on
   bits at I2). Naming `f17` is free; keeping 64 architectural registers live at once is
   not, and never was.
4. **So a STOCK, unconstrained RV64IMAFD binary is not admissible as-is — and that was
   already true before O4.** The same toolchain constraint that forbids a stack, a frame
   pointer, a `jal` and a spill (I7) is the one that bounds live register state to 512
   bits. **The machine is an RV64IMAFD *target under a register-pressure constraint*, not
   a general-purpose RV64IMAFD core**, and the admission test (K.6) is exactly the check
   that a compiled body met the constraint. **A body that exceeds it is rejected, not
   mis-executed.**

**Neither horn of the objection is taken: the `f`-names do not overlay the `x`-names, and
the context is not wider than 512 bits.** The name space is large and the live set is
small — which is the ordinary situation for any register allocator, only with a bit budget
instead of a slot count.

**And the admission test follows from that** — it checks **the `IMAFD` subset** (an opcode
outside it is inadmissible) and it counts **liveness in BITS**, with an `f64` costing 64
and an `f32` costing 32. See **K.6**, which is where the test lives.

### I.1 The governing rule: NOTHING BLOCKS

**There are no blocking instructions.** Every action is a **try** that reports whether
it succeeded, paired with a **probe** that asks whether it would. **Software spins if
it wants to wait; the hardware never does it on software's behalf.**

*User #222, 2026-09-01T04:16:55Z:* "I think it needs to block, or it needs to return a
failure condition. **I think blocking at all is dangerous generically, which might mean
we don't support it. We have no blocking instructions, just variants that attempt the
action, and an instruction that probes if the action will work.**"

This is not a style preference; it is what the machine has already cost. **A blocking
instruction is a resource held while waiting for a resource** — the same shape as the
migration path that deadlocked the machine at cycle 9,100,426 with four tiles at 0–1
free contexts and 983 tokens waiting (DESIGN §23.1 D:1997-2002; §20.3). **Making it impossible to express is cheaper than
auditing for it.**

Consequence: **there is no blocking `JOIN`.** An earlier draft had `JOIN` (blocking)
and `PJOIN` (permissive) as separate instructions. **They are the same instruction —
the blocking one was only a spin the hardware performed for you.**

**Which name survived, stated explicitly because the mapping has to be given rather than
inferred.** #221 defines `PJOIN` as "permissive join, **returns 1 if the entry has
returned, 0 otherwise**" — a description that fits a *probe* at least as well as a *try*.
The shipped ISA resolves it as:

| #221's name | shipped name | what it does |
|---|---|---|
| `JOIN` (blocking) | **deleted** | the blocking form does not exist; nothing blocks |
| `PJOIN` (permissive) | **`JOIN`** | the **try**: attempts the deposit, moves 512 bits on success, sets `rOK` |
| — | **`JOINQ`** (new) | the **probe**: asks whether it has returned, **without moving 64 bytes** |

**`PJOIN` became `JOIN`; `JOINQ` is a new instruction that did not exist in #221.** The
split follows I.1's try/probe pairing rule, and the reason `JOINQ` earns its own
encoding is that a probe must not move the payload.

### I.2 The host side

*User #221, 2026-09-01T04:08:22Z, defining the shape:* "1. **Fork from reg** (provides
two regs, a PC + context reg). 2. **Fork from memory** (provides two regs, a PC +
context address). Then, within these two callback shapes: **FIRE + FORGET or RETURN +
JOIN.** The latter of these allocates a FTU entry. Now, we have join instructions:
**JOIN** (joins by blocking on an FUT entry having been returned, and offloads data to
the target register). ... **PJOIN** (permissive join, returns 1 if the entry has
returned, 0 otherwise). Finally, I think it is probably necessary for **even the FIRE +
FORGETs to allocate FUT entries as well, which means we must close them out on return
(an ending context returns an ACK).**"

```
FORK.R   rH, rPC, cCTX     try; rH = handle, or 0 if no FTU entry is free
FORK.M   rH, rPC, rADDR    try; the TILE fetches the 512-bit context from rADDR
FORKF.R  rH, rPC, cCTX     fire-and-forget
FORKF.M  rH, rPC, rADDR    fire-and-forget, context from memory
FORKQ    rN                probe: how many FTU entries are free
JOIN     rOK, cDST, rH     try: deposit 512 bits, rOK = 1 on success
JOINQ    rOK, rH           probe: has it returned, without moving 64 bytes
```

**Why `FORK` returns a handle, and it is measurement-driven.** Without one, `JOIN` can
only mean "the oldest", and FIFO retirement was measured **worse**: at ring depth 4096
the queue-empty fraction rose **38.4% → 43.1%** and cycles rose **9.3%**, because the
host blocked on one straggler while thousands of finished invocations queued behind it
(DESIGN §23.2 D:2019-2026, measuring §20.1).
**An addressable tracking unit is what makes out-of-order join expressible.**
*User #224, 2026-09-01T04:32:31Z:* "I think it does return a handle."

**All four fork forms return a handle**, fire-and-forget included — there is no use for
it today, but it keeps every context addressable, keeps the four encodings uniform, and
adding it later would break every fork.

**`FORK.M` is dereferenced by the TILE, not the host.** That is the entire point of the
form: **only an address crosses the fabric**, and the load happens where the context
probably already lives. *User #222:* "**That is the whole idea, if we were planning on
loading the data already then we would just use the register-variant.**"

**`FORKQ` returns a count, not a flag**, so software can size a batch instead of
probing per fork — batch shape was worth 18% of runtime (DESIGN §23.2 D:2036-2040,
measuring §20.1). It is a **sizing hint, never a
contract** — it can go stale between probe and fork, and that is fine, because `FORK`
returning 0 is already the authoritative answer.

`[DISAMBIGUATED — the user's requirement below says FORK; this document assigns it to
FORKQ. The reassignment is stated here rather than left to be inferred, because an
earlier revision presented the quotation under the FORKQ discussion as though the user
had said FORKQ.]`

*User #225, 2026-09-01T04:35:49Z, verbatim:* "I think **FORK** should provide what is
**architecturally correct**. That is to say, given the forks and joins before it, what
the current occupancy should be. **There is a real number that it should be, right?**"

**How that requirement is met, and why it lands on `FORKQ`.** Two messages are in play
and they are nine minutes apart: at **#224** the user said `FORK` "**does return a
handle**", and a single destination register cannot carry both a handle and an occupancy
count. The requirement in #225 is about **what the machine must be able to report** —
"there is a real number that it should be" — not about which opcode carries it. So:
- **`FORK` returns the handle** (#224), or **0** when no entry is free. That refusal is
  itself the authoritative answer about occupancy, and it cannot go stale.
- **`FORKQ` returns the count** — the architecturally-correct free-entry number #225
  asks for, as a **sizing hint, never a contract**.
- **`FORKQ` earns an encoding only because of fire-and-forget.** For return+join forks
  the occupancy *is* architectural (`forks − joins`) and **a value software could compute
  itself does not earn an instruction**; a `FORKF` entry is freed by an **ACK when the
  remote context ends**, which is asynchronous and unknowable from the instruction
  stream, so total FTU occupancy is **not** derivable and must be readable.

**This reading is the canon. If the user meant `FORK` itself to return an occupancy in
place of the handle, that overrides the above** — it is a genuine reinterpretation of
tier-1 wording by this document and is recorded as such.

### I.3 The function side

```
END.R                      end; RETURN BIT SET — return THIS context's register file
END                        end; return bit clear — ACK only, no register file
CONT     rPC               extend: carry my own context forward
CONT.M   rPC, rADDR        extend: the tile fetches a fresh context
```

**THERE IS ONE END INSTRUCTION AND IT CARRIES A RETURN BIT.** [RULED — user ruling
2026-09-02, verbatim: "**RETC and ENDC are the same instruction, with a return bit.**"
Ledger L44 is closed, and the count is settled at **TWELVE base instructions**. The
mnemonics `RETC` and `ENDC` survive only as the assembler surface for `END.R` and `END`,
and this document uses `END` with an explicit statement of the bit.]

*(The forms and their meanings: DESIGN §23.3 D:2095-2098. [CONFLICT — tier 3 lists
`RETC cCTX` at D:2095, WITH an operand.] `END` takes none, for the
reason below, and I.9's encoding agrees — funct3 = 0, rd/rs1/rs2 all `x0`. **Tier
3 is wrong here and the encoding is right**; recorded so the operand is not restored from
DESIGN.)*

[DISAMBIGUATED — `END` takes NO OPERANDS in either form. An earlier revision listed
`RETC cCTX`, which contradicts the encoding table in I.9 and cannot be assembled.] The
reason it takes none is structural: **`END` returns the register file of the context that
executes it**, and a context on a function core has exactly one register file. There is
nothing to name. `cCTX` is a *host-side* concept — context registers live on the host (I.8)
and a function core has none — so an operand naming one could not be resolved on the tile
at all. **Both forms are one opcode and a return bit**, and both are operandless; they
differ only in whether the completion message carries 64 bytes.

**THE TWELVE, IN FULL — this is the base set, and it is complete:**

```
FORK.R   FORK.M   FORKF.R   FORKF.M   FORKQ          host: offload and probe
JOIN     JOINQ                                        host: collect and probe
END(+ret bit)     CONT      CONT.M                    function: finish or extend
CXW      CXR                                          host: move 64 bits in and out of a context register
```

**Seven host-side, three function-side, two context-register moves. Twelve.**

*User #223, 2026-09-01T04:26:58Z:* "**RETC is probably good, we probably need two
variants (one that returns the regfile, one that just ends the context. Either the same
instruction and a bit indicates whether the context should return it's regfile back to
the core, or two separate instructions.** We actually also need a '**continue**'
instruction which just passed forward it's own context and is subsumed (so, pass a PC
and it's own context or the address of a new context)."
*And user #225:* "**CONT.M should replace the context wholesale.**"

**They are one opcode and a return bit** — they differ only in whether the
message carries 64 bytes, and the encoding makes that visible. `[The user's #223 wording
above left it as a choice between "the same instruction and a bit" and "two separate
instructions"; **user ruling 2026-09-02 chose the first**: "RETC and ENDC are the same
instruction, with a return bit."]` `CONT.M` **replaces the
register file wholesale**: the 512 bits fetched *are* the context; there is nothing to
merge them with.

**A function must have a terminating instruction.** *User #79, 2026-08-28T19:12:14Z,
all-caps:* "**PCs are important, each instruction get's it's own. Do you not simulate an
instruction to terminate a function? How do you know it is done?** ... **A 1-instruction
function by nature does NO WORK. THAT ONE INSTRUCTION WOULD BE THE RETURN CALL. PCs MUST
LIGN UP WITH ACTUAL MEMORY ADDRESSES.**" And #101: "you understand these functions need
a way to terminate themselves right?" And #222: "**A separate instruction. We probably
shouldn't reuse `ret`, since that will cause confusion and it genuinely behaves
differently.**"

**`CONT` is I10's successor, and it CANNOT FAIL.** It **inherits the existing FTU entry
rather than allocating one**, so it consumes no new resource and can never be refused —
which is exactly **why extension is safe where fan-out is not**. The handle the host
holds stays valid across an arbitrary chain of successors; whatever the last link
returns is what the `JOIN` receives. It is also the mechanism for splitting a function
too large for one 512-bit register file into a chain, each link admissible on its own,
**with no link able to be denied**. *(DESIGN §23.3 D:2126-2136 states all of this,
including that `CONT` is the mechanism for §22's option 2 — splitting a function too large
for the file into a chain.)*

**AND A THIRTEENTH, WHICH IS NOT PART OF THE BASE SET BECAUSE IT IS NOT A USER
INSTRUCTION: `RESUME`.**

```
RESUME   rH                PRIVILEGED; restart the faulted context named by handle rH
```

`[ADDED — user ruling 2026-09-02 R20, verbatim: "faults should go to the kernel handler,
execute, then resume the work just like a normal system. On fault, it is probably necessary
to pass the fault to the core via the FUT and have it handle it, **then send a 'resume'
instruction with the context handle to resume it**." And, on privilege: "**RESUME needs an
extra instruction (privileged???? this is real question, not sure if it needs to be or
not).**"]`

**IT IS PRIVILEGED, AND THAT IS RULED.** [RULED — user ruling 2026-09-03 O16, verbatim:
"**Yes, privileged.**" This answers the one clause R20 explicitly left open. The
`[USER TO CONFIRM …]` tag that used to sit on every mention of `RESUME` is removed from
this document entirely.] A recoverable fault is delivered
to the **kernel**, the kernel's handler runs, and the kernel resumes the faulted context —
which is exactly the `sret`/`iret` shape on any machine: **the party that took delivery of
the trap is the party that returns from it.** If `RESUME` were user-level, any user program
could restart any context whose handle it could name or guess, which is a protection hole
with nothing on the other side of the trade — user code has no reason to resume a context
it did not know had faulted. **`FORK` and `JOIN` stay user-level**: offloading is ordinary
user work and nothing about it touches the trap path.

**R20 asked the question; user ruling 2026-09-03 O16 answered it: "Yes, privileged."** The
paragraph above is no longer a recommendation — it is the ruling and its reason.

**What it does, in one line: it names a parked context by its FTU handle and tells it to
re-issue the instruction that faulted.** It allocates nothing, refuses nothing, and — like
`CONT` — cannot fail, because the entry it names already exists. The context's slot was
**held** across the fault, which is the one place a function core holds a slot for something
other than execution; a fault is not a migration and there is nothing to re-place. See
**I.6** for the fault path and **C.4** for the lifecycle.

### I.4 The two closing rules — deliberately asymmetric

The fork type and the end type are chosen independently and can disagree, so the
tracking unit must define what happens. **Every combination returns *something*.**
*(DESIGN §23.3 D:2107-2125 states both rules and the asymmetry, in the same words.)*

*User #230, 2026-09-01T05:26:54Z, stated both rules:* "**FAF entries should close upon
recieving an ACK, while expected-JOIN entries must never close without returning their
values. This means, strictly, that FAF entries should not expect their returns to be
read (because they can't be), and not returning anything to an expected-JOIN still
sends an ACK, and a zero'd register.**"

| | rule |
|---|---|
| **fire-and-forget entry** | closes on its **ACK**, and its return **can never be read**. An `END` with the return bit SET from such an invocation carries 64 bytes nothing will collect; they are **dropped**. `JOIN`/`JOINQ` on a fire-and-forget handle therefore cannot succeed — they **report failure**, which is the try/probe answer, **not a fault**. |
| **join-expected entry** | **never closes without returning its values.** It closes only at `JOIN`, and `JOIN` **always** hands back a register file. An invocation that ends with the return bit CLEAR still produces an ACK **and a zeroed register file**. It must not become uncollectable: an entry no instruction can ever free is a resource held forever. |

Both mismatches are **counted**, so they are visible in the statistics rather than
silent. **Neither faults.**

**AND THERE IS A THIRD CLOSURE, WHICH IS NOT A MISMATCH BUT A KILL — RULED.**
`[RULED — user ruling 2026-09-03 **O7**, verbatim: "**I think B, anything else could delay
quit until the user program decides to join, which could be forever.**"]`

| | rule |
|---|---|
| **fatal fault on the program** | **every outstanding entry of that program closes at once, with a ZEROED register file and an ERROR FLAG set.** A `JOIN` on any of them **returns immediately with the error**; it does not block, does not fault, and does not wait. **Nothing in the teardown waits on the user program**, because a dying program may never join. |

**This is what keeps the join-expected rule above LITERAL.** "*Never closes without
returning its values*" is satisfied by the kill path, because **a zeroed file plus an error
flag is a well-formed return** — so the rule needs no "while the program is live" caveat,
and no entry is ever left that only a dead program's instruction could free (which is the
shape **I.5** forbids). **The error flag is the only new state: one bit.** Full statement,
with the five-step path, at **I.6**.

`[CONFLICT — ChampSim closes a fire-and-forget entry at DISPATCH and sends no completion
at all, so no ACK ever closes it.]` Tier 1 (#221, quoted in I.2) is explicit: "**even the
FIRE + FORGETs to allocate FUT entries as well, which means we must close them out on
return (an ending context returns an ACK)**." ChampSim does the opposite:
`src/nmfc/nmfc_host_core.cc:1332-1340`, with its own rationale — "Fire and forget:
nothing downstream consumes a result, so the tracking slot frees now rather than on a
return that will never come" — sets `entry->returned = true; entry->join_seen = true;`
and calls `retire_if_done(idx)` **immediately after `fabric_->dispatch`**. The header
says the same (`inc/nmfc/nmfc_host_core.h:483-485`: "released when the invocation
returns — or **immediately at dispatch for a fire-and-forget call, which is why those
cost the host no tracking slot**"), and so does `inc/nmfc/nmfc_trace.h:161-165`. On the
tile side `src/nmfc/function_core.cc:1191` guards the completion with
`if (!ctx.body->no_return())`, **so a fire-and-forget invocation emits no completion
message whatsoever** — there is nothing for an ACK path to carry.

**Tier 1 wins: a fire-and-forget entry is allocated at fork and closed by an ACK when the
remote context ends.** Two things depend on it and both are lost under ChampSim's rule:
**(a)** `FORKQ` exists *precisely because* a `FORKF` entry's release is asynchronous and
therefore not derivable from the instruction stream (I.2) — under free-at-dispatch it
would be derivable, and `FORKQ` would not earn its encoding; **(b)** fire-and-forget
invocations become invisible to FTU occupancy, so the machine's in-flight count is
understated by exactly the fire-and-forget traffic. Ledger **L29**. I.10's encoding table
maps fire-and-forget to `FLAG_NO_RETURN` and does not record this divergence; it does
now.

### I.5 The function tracking unit

**Parallel to the LSQ, not a reuse of it** — offload concurrency is its own knob, not
an accident of load-queue depth.

*User #234, 2026-09-01T05:52:54Z, the mental model:* "We have an FTU that is **more or
less a cache** (storing 64 bytes X contexts is a lot). **Filled on completion of each
context. JOIN takes that, and fills it into a context reg** (small working set, 8 are
fine). From there on, the context regs are part of the register set, rename +
dependencies + etc...." — clarified at #235: "I wasn't implying that it was a cache. I
was making a comparison. **They are on the same order of magnitude.**"

*User #136, 2026-08-29T05:06:58Z, the semantics that matter:* "**The FUT holds the
return data, 64 bytes per context. It doesn't place it immediately on return. That
would be ruinous. Therefore, the join can choose where to deposit the returned
information (what reg), and thus it can parallelize dispatch and serialize
return-processing if it needs to.**"

**Sizing.** An entry is a returned register file plus a handful of bits — 512 bits of
payload, two of state, one of retirement mode, and a hart id — so **about 65 bytes**,
giving **4 KiB at 64 entries and 16 KiB at 256**. That is the same order as an L1D and
comfortably under one, which is reasonable for a specialised core. *(DESIGN §23.2
D:2058-2064, verbatim, including the note that "the simulator's entry is 80 bytes, but
half its metadata is instrumentation".)* [CAUTION — 64 and 256 are the sizes the
derivation prices, NOT the machine's FTU size, which no tier-1..3 source fixes: ChampSim
ships `ftu_size: 1024`. **The size is configuration** (user ruling 2026-09-02 R6–R10);
**see SELECTED CONFIGURATION.**]

**It cannot be made smaller by holding fewer payloads than entries**: every outstanding
invocation may complete before any `JOIN`, so the unit has to absorb all of them at
once. If it could not, a tile would have to hold a finished context until the host made
room — **a resource held while waiting for a resource, the shape that deadlocked the
machine at cycle 9,100,426. The 64 bytes per entry are what buy the tile its context
slot back the instant it returns.**

**It REFUSES rather than evicts.** A cache makes room by evicting; this array cannot,
because an entry holds the **only copy** of a returned register file and a join-expected
entry must never close without returning its values. So it fills and then refuses —
`FORK` returns 0 — and **refusal is architecturally visible because it cannot be handled
invisibly. That is why `FORKQ` exists.** *(DESIGN §23.2 D:2072-2079.)*

**THE ONE PATH THAT RECLAIMS AN ENTRY WITHOUT A `JOIN`, AND IT IS NOT AN EVICTION.**
`[RULED — user ruling 2026-09-03 **O7**.]` On a **fatal** fault the kernel kills the
program, and **every outstanding entry of that program closes at once — register file
zeroed, error flag set** — with any later `JOIN` returning the error immediately. **That is
a closure, not an eviction:** the entry is not being reused while its owner still expects
it; its owner has ceased to exist. **It is also what keeps this section's own prohibition
true.** "Refuses rather than evicts" means entries are freed only by the instruction that
collects them — which, without O7, would leave a killed program's entries permanently
unreclaimable, **the resource-held-forever shape this section forbids by name.** The kill
path is the answer, and the user's reason is that any alternative "*could delay quit until
the user program decides to join, which could be forever*". See I.4 and I.6.

**Sizing it wrongly bounds the whole machine.** ChampSim records the lesson in
its own CLI help: "The tracking unit is the ceiling on in-flight invocations for the
whole machine ... **Left at a small value it silently caps everything: a 1024-context
machine with a 64-entry unit can never exceed 64 outstanding, and every occupancy number
it reports describes the unit rather than the architecture.**"

`[CAUTION — that CLI text is tier 2, and tier 1 refuses the inference it invites.]` **A
full FTU is not by itself evidence that the FTU is the constraint.** *User #180,
2026-08-29T19:32:15Z:* "**Despite the FTU being full, most of it is still pending
results. A larger FTU doesn't fix that problem. The work is being done, the standard core
is not consuming it fast enough. That is not a problem with the functions, it is how the
functions are being positioned in the code.**" *And #171, 2026-08-29T08:55:35Z:*
"**if the tracking unit is capped, then we should see 1024 contexts somewhere out on
the tiles. We don't.**" **Size the unit so it does not bound the machine artificially —
and then diagnose a full one by splitting its occupancy into OUTSTANDING versus
RETURNED-AND-UNJOINED before concluding anything.** Full discussion in H.9.

### I.6 Faults — and the RESUME path, which is a normal system's path

*User #223, 2026-09-01T04:26:58Z:* "**A fault that is recoverable (say, a page fault)
should go back to the nmfc core that triggered it. A fault that is like div0 or an
exception should just kill the program outright, and all of it's contexts.**"
*And #222:* "**User-space processes die just like a segfault would cause. This requires
propagating the failure to the host core.**"

**THE RECOVERABLE PATH IS RULED, AND IT IS THE ORDINARY ONE.** `[user ruling 2026-09-02
R20, verbatim: "**faults should go to the kernel handler, execute, then resume the work
just like a normal system.** On fault, it is probably necessary to pass the fault to the
core via the FUT and have it handle it, then send a 'resume' instruction with the context
handle to resume it."]`

**The path, in five steps:**

| # | step | who |
|---|---|---|
| 1 | a context on a tile takes a recoverable fault — a page fault, say | the function core |
| 2 | the fault is carried to the host **through the FTU**, which is where the invocation's identity already lives; it arrives as a **trap** | the fabric, then the FTU |
| 3 | the **kernel handler** runs, exactly as it would for a fault taken by a host core — fix the mapping, or whatever the fault needs | the host |
| 4 | the kernel issues **`RESUME rH`**, naming the parked context by its FTU handle | the host, privileged (I.3) |
| 5 | the context re-issues the instruction that faulted and carries on | the function core |

**The context PARKS; it does not die and it does not spin.** Its slot is held across the
fault — the one place a function core holds a slot for something other than execution —
because it is going to resume **in place**. There is nothing to re-place: a fault is not a
migration.

**Three consequences worth stating so they are not re-derived:**
- **The FTU is the delivery path, and that is why it must hold the handle.** The tile does
  not know which host core to trap; the FTU entry does. It is the same addressability that
  makes out-of-order `JOIN` expressible (I.5).
- **Walks are still local (I3/F.6).** A fault is what happens when the *local* walk cannot
  complete — R20's "*handling things like page-table-walks in hardware where we don't want
  to pass control back to the kernel unless we see an actual fault*" (#269) is the same
  rule from the other side: **the kernel is entered on a fault and not otherwise.**
- **`RESUME` is a thirteenth instruction and it is not part of the base twelve**, because
  it is issued by the kernel, not by user code. See I.3 and I.9.

There is deliberately **no per-invocation fault status, no error code in the returned
register file, and no fault probe** — those only make sense with user-defined fault
handlers, which this machine does not have. *(DESIGN §23.4 D:2141-2147.)* **The kernel
handler is not a user-defined handler**; R20 puts fault handling exactly where a normal
system puts it.

**THE FATAL PATH IS NOW RULED TOO.** `[RULED — user ruling 2026-09-03 **O7**, verbatim:
"**I think B, anything else could delay quit until the user program decides to join, which
could be forever.**" Option (b) of that row. R20 closed the recoverable path; this closes
the fatal one, and Part I no longer has an open question in it.]`

**WHAT A FATAL FAULT DOES TO OUTSTANDING FTU ENTRIES: they close with a ZEROED REGISTER
FILE and an ERROR FLAG. `JOIN` returns IMMEDIATELY with the error. NOTHING WAITS ON THE
USER PROGRAM.**

| # | step | who |
|---|---|---|
| 1 | a context takes a **fatal** fault — `div0`, an exception, a segfault-class access | the function core |
| 2 | the failure is propagated to the host (#222: "*This requires propagating the failure to the host core*") | the fabric, then the FTU |
| 3 | the program and **all of its contexts** are killed (#223) | the kernel |
| 4 | **every outstanding FTU entry of that program closes at once: register file zeroed, error flag set** | the FTU |
| 5 | any `JOIN` on one of those handles — whenever it is issued, or never — **returns immediately with the error flag set**. `JOINQ` likewise reports the entry as returned. | the host |

**THE USER'S REASON IS THE DESIGN CONSTRAINT, AND IT RULES OUT OPTION (a) BY ITSELF:**
"*anything else could delay quit until the user program decides to join, which could be
forever.*" **A teardown that waits for a `JOIN` is a teardown a dying program can block
indefinitely** — and the program is being killed precisely because it can no longer be
trusted to make progress. **Nothing in the kill path may depend on the user program doing
anything.**

**Three consequences, and the first is why this shape and not a teardown-plus-fault:**
1. **I.4's rule holds LITERALLY, and does not have to be scoped.** "*A join-expected entry
   never closes without returning its values*" is satisfied: a **zeroed register file plus
   an error flag IS a well-formed return.** Option (a) would have required scoping that
   rule to "a live program", which is a caveat on an invariant; option (b) needs none.
   **The rule survives intact, which is the better outcome for a document that has to be
   read literally.**
2. **A joining host always gets a well-formed answer it can TEST.** It never faults on the
   `JOIN` itself, so the error path in user code is an ordinary branch on a flag rather
   than a second trap — and a program joining a handle from a *different*, already-killed
   program gets the same well-formed error rather than an undefined result.
3. **Nothing is held forever, so I.5's prohibition is satisfied.** Every entry is freed by
   the kill itself, not by an instruction the dead program will never execute. **The
   resource-held-forever shape I.5 forbids by name cannot arise on this path.**

**The zeroing is not decoration.** It is the same zeroed file I.4 already specifies for a
join-expected entry whose invocation ended with the return bit clear, so a host sees **one**
well-formed shape for "no values came back", distinguished by the error flag. **The flag is
the only new architectural state**, and it is one bit in an entry that already carries two
of state and one of retirement mode (I.5's ~65 bytes is unchanged).

**Both mismatches and the fatal closure are counted** (I.4, O.4), so a killed program's
entries are visible in the statistics rather than silently vanishing.

### I.7 Deliberately absent — and why

*(DESIGN §23.5 D:2149-2172 states all three absences and their reasons; the tier-1
quotations below are what it rests on.)*

`[THREE ADDITIONS TO THIS SECTION FROM THE 2026-09-02 AND 2026-09-03 RULINGS, all of which
change what "deliberately absent" covers.]`
1. **A COMMIT and a WAIT are absent and were never needed — R14.** "*That was an artifact
   of the ChampSim design, since ChampSim doesn't model coherence, data, or atomics. By
   necessity, a core must poll said block to see if the writeback of the data has occurred
   (coherence propagated). Potentially using atomics.*" **A commit is an ordinary store, a
   publication is coherence, and a wait is a poll** — so the memory-committing loop needs
   no instruction and this list is complete without one. See **I.11**; ledger L47 closed.
2. **`RESUME` is NOT absent — it is ADDED (R20), it is PRIVILEGED (user ruling 2026-09-03
   **O16**, "*Yes, privileged*"), and it takes reserved encoding space.**
   The `0x6`/`0x7` groups this section earmarks for `KILL` and mailboxes now also have to
   accommodate it (I.3, I.6). **Which slot it takes is an implementation choice, not a
   canon statement** — user ruling 2026-09-03 **O3** makes every `funct7`/`funct3` value
   implementation choice, so the canon says only that a slot exists and that the
   instruction is privileged.

3. **A SEPARATE FLOATING-POINT REGISTER FILE IS DELIBERATELY ABSENT — and this is new,
   because `F` and `D` are new (user ruling 2026-09-03 **O4**, "*I think we want float, so
   C*").** On a stock RV64 core, `F`/`D` bring `f0`–`f31` as **their own file**. **On a
   function core they do not**, because a context is **512 bits, bit-packed** (invariant 2,
   the 512-bit rule at I2/H.3) and a second file would be a second context. `[derived from
   ruling O4]` **`f0`–`f31` and `x0`–`x31` are two NAMESPACES over the same 512 bits**; the
   compiler's packing decides which bits each name resolves to. There is no `fcsr`, no
   rounding-mode state and no floating-point exception state either — **that is
   per-context state the 512 bits do not budget for**, and adding it would break invariant
   2 and grow a migration past 72 B (I.0, invariant 11). **A function needing dynamic
   rounding modes is in the same position as a function that spills: it cannot be
   offloaded.** Recorded here rather than in Part P because it was never proposed and
   rejected — it is a consequence of O4 that has to be stated before someone implements
   `F` the ordinary way.

**`KILL`.** *User #224, 2026-09-01T04:32:31Z:* "**Lets not build a KILL instruction
though. The idea that a context could be ended in an unsafe state is real, so the
context would need a specific instruction that marks 'I will accept a kill request at
this time'. A whole can of worms that doesn't appear strictly necessary at the moment.**"
Encoding space is **reserved**. A kill must be *cooperative*, which is a new
instruction, a protocol, and a liveness obligation on every function written thereafter.

**Mailboxes — `SEND` / `RECEIVE` / `PRECEIVE`.** Proposed by the user at #221 and left
undecided ("Not sure if they are yet"). They need **a context-to-location directory
updated on every migration** — 8.9M migrations on one measured BFS run (DESIGN §23.5
D:2159-2164; the same figure is K.3's, cited there) — **and a bounded
buffer that can fill, which is the hold-and-wait shape I.1 exists to forbid.** Encoding
space reserved, unbuilt until a workload demands it.

Functions already communicate through **memory**: *user #89, 2026-08-28T19:50:34Z:*
"we have a whole standard processor that is launching these functions. **These
functions can communicate via memory sharing you know?** ... The core could launch a
series of heterogeneous functions and then have this parent-child relationship happen
just as easily via memory-sharing/message passing."

**But the user attached a condition to that in the very next message, and dropping the
condition was what he was complaining about in it.** *User #91, 2026-08-28T19:55:42Z,
verbatim:* "It is an address, which will live on one of the memory slices, in memory.
... **If the functions are on the same tile, this is basically free. The concern would be
bouncing these across the coherence network if the functions exist on different tiles.
Again, that is a NUCA concern (which you have seemingly dropped entirely from your
considerations).**"

**So the statement is conditional, not absolute:**
- **Same tile:** memory sharing is essentially free. One slice, one copy, no coherence
  question (I14 case 1). **Ownership makes it coherent without protocol — here.**
- **Different tiles:** the shared line **bounces across the coherence network**, and that
  cost is real. **It is a NUCA/NUMA problem** — the two communicating functions belong on
  the same tile, and getting them there is exactly G.1's job ("we need a policy
that allows for functions to migrate to their data ... both the source and sink are
moveable").

**Mailboxes remain unbuilt and the reason above stands. But "memory sharing makes
mailboxes unnecessary" is only true once co-location holds**, and asserting the
conclusion without the condition is precisely the drop the user named.

**`context_id` and `tile_id` CSRs — PROPOSED, QUESTIONED BY THE USER, NOT BUILT. This is
not a ruling.** `[AUTHORITY CORRECTION — an earlier revision filed this in Part P as a
settled rejection with a rationale the user never gave. The cited message contains a
question.]`

*User #222, 2026-09-01T04:16:55Z, item 7, in full — this is the whole of what he said on
it:* "**I am not sure how context_id or tile_id are used practically by the core? FTU
occupancy makes sense and is buildable. context_id would be the context_id of what
specifically? same with tile_id, the tile_id of what?**"

**What that is:** a request for a use case and a referent, plus an explicit endorsement of
FTU occupancy as the thing that "makes sense and is buildable". **What it is not:** a
rejection, and it carries no reason about tile-id leakage.

**Status: unbuilt, because nothing has answered the question.** `FORKQ` covers the one
identified need. *This document's own argument, offered as such and NOT as the user's:* a
workload that can read a tile id can compute with it, which is the shape R8 and R9 forbid
— so if these are proposed again, that objection must be answered alongside the user's
two. **Do not attribute the leakage argument to the user.**

### I.8 Context registers

**Eight of them, `ctx0`–`ctx7`, 512 bits each, per software thread.** This is I1's
"512-bit vector register that *is* the callee's register file", made real.

`[READ THAT SENTENCE CAREFULLY — it is the one place in this document where "eight" and
"512 bits" appear together and are BOTH correct, and it is adjacent to the regression
invariant 2 exists to stop.]` **These are eight HOST-side architectural registers of 512
bits EACH — 4096 bits in total — each one a staging area for a whole context.** They are
**not** a division of one context into eight parts. **A context is 512 bits, BIT-PACKED,
and is not eight registers** (#232, #238; I2, H.3). The two facts live one table apart in
H.3's "the number 8 names FOUR unrelated quantities" and collapsing them is the specific
regression that table was written against.
*(DESIGN §23.6 D:2174-2237 is this whole subsection's tier-3 counterpart: the eight
registers, the negative prior-art check, the two instructions, the rejected bit-field
insert/extract, the `JOIN` renaming wrinkle and the rejected `a0`–`a7` aperture.)*

*User #231, 2026-09-01T05:37:55Z:* "**We need those 512 bit regs, so we might just need
to implement a specialized set of new regs. Far simpler than implementing a vector
extension.** The only tricky part is making sure the wider regs interact nicely with the
existing instructions that may interact with them, and **we may need a subset of
bit-manip instructions added so that values can be retrieved/set.**"
*And #233, 2026-09-01T05:47:14Z:* "**We need to make sure EXTRACTION from the regs is
possible. Regular bit manipulation can take you the rest of the way. Alignment is
something handled by the existing ISA. Let's not overdesign.**"

```
CXW      cD, lane, rS      cD[lane] <- rS     one 64-bit lane
CXR      rD, cS, lane      rD <- cS[lane]
```

**The lane is an ACCESS GRANULARITY, not the register's structure, and these two are
complete.** [SHARPENED — user #232, #238, and restated as a correction on 2026-09-03:
"512 bits is not 8 registers … Bit-packing is the name of the game … Why do you keep
reverting to that?" **A 512-bit context register is not eight lanes any more than it is
eight registers.** "Lane" here names **how wide one `CXW`/`CXR` moves data** — 64 bits at a
time, because that is what a GPR holds — and says nothing whatever about how the 512 bits
are divided. A context packed as sixteen 32-bit values is reached through exactly the same
two instructions.] Once 64 bits move in and out, any packing within them is reached with
the shifts and masks RV64I already has, and a field straddling a lane boundary is two moves
and the same arithmetic. **A bit-field insert and extract carrying an offset and a width
was considered and dropped: it would duplicate instructions that exist.**

**Prior art, checked and negative.** RISC-V has no such register. **Rev implements no
vector extension** — its `RegVEC` class and `RVVTypeOpv` format are declared and used
nowhere — nor does Vanadis, nor does anything in sst-elements; Vanadis's RoCC interface
passes only **128 bits** — `RoCCCommand(RoCCInstruction*, uint64_t rs1, uint64_t rs2)`
and `RoCCResponse(uint8_t rd, uint64_t rd_val)`,
`src/sst-elements/.../vanadis/rocc/vroccinterface.h:52-72`, i.e. two 64-bit operand values
in and one out. **That is why a context register cannot be a RoCC operand and is named by
a number instead** (I.9's operand convention). RVV with `VLEN=512` would be the standards-compliant answer
and means writing a vector unit from scratch first. **A small dedicated file is far less
work and is what the machine actually needs.**

**Rejected: using a fixed aperture of eight general registers (`a0`–`a7`) as the
context.** It costs no new state and, on a fork whose arguments are already in place, no
instructions. It was rejected because **a fixed aperture can hold one staged context,
and an addressable tracking unit that exists to allow out-of-order join then has one
landing pad to join into.** The instruction-count argument also reverses on the real
workload: the bottom-up BFS function has three arguments, two loop-invariant, so with
context registers the invariant lanes are written once outside the loop and the fork
costs one `CXW`, where an aperture must re-establish whatever the loop body clobbered.

**The `JOIN` renaming wrinkle, named so it is not rediscovered.** `JOIN` is a *try*: it
writes 512 bits on success and leaves the destination alone on failure. A machine that
renames must express that, and the clean formulation makes `JOIN` a read-modify-write —
`cDST_new = ok ? ftu_payload : cDST_old` — so the destination is a source as well. The
dependency on the old value is real only in the failure path, where software is going to
look at `cDST` again anyway, so the cost is **one extra source read rather than a squash
or a copy.** The alternative — an unconditional `JOIN` issued only after a successful
`JOINQ` — is race-free in a single instruction stream but reintroduces a sequence that
must not be interrupted.

**Open, and it is a compiler problem, not an architecture one:** a compiler does not
know that narrowing an operand yields a larger register file, so the packing — and the
admission decision that depends on it — is compiler work. **The architecture only has to
not prevent it, and 512 opaque bits plus a 64-bit access aperture does not.**

### I.9 Encoding

[REBUILT FROM SOURCES, and the provenance of every line is now shown. An earlier
revision presented this whole section — narrative, group table, variant bits and the
funct7 column — with NO citation at all. Its actual source is `nmfc_isa.h` in the SST
tree, which the front matter says "never decides anything", and Part I was the only Part
in the document with zero `D:` citations. Some of it turns out to be tier 3 after all;
the rest is now marked.]

**Read the tier column before implementing a row.**

| tier | means |
|---|---|
| **1** | the user's own words |
| **2** | ChampSim source |
| **3** | `docs/nmfc/DESIGN.md` |
| **`[SST-only — implementation choice]`** | **the only place this value is written down is `/mnt/md0/NMFC-Rev/src/nmfc/include/nmfc_isa.h`, authority tier 4, "it never decides anything".** It is reproduced because it is the only concrete encoding that exists and because re-deriving it would invalidate the binaries that already assemble against it — **not because it is canon.** [RULED — user ruling 2026-09-03 **O3**, verbatim: "*I think this is just a simulator thing and not a meaningful design choice, so I say we describe it as implementation choice.*" **The canon assigns no field values at all.** The tag was `[SST-only — implementation choice]`, which implied a ratification still pending; there is none pending, because none is wanted.] Ledger **L43**, CLOSED. The values are recorded in **SELECTED CONFIGURATION** as one implementation's. |

**The current encoding is RoCC-conformant, and it SUPERSEDES the earlier
funct3-as-group encoding.** *(The supersession itself is tier 3: DESIGN §26.6
D:2907-2917 states the reform — funct7 carries the group and the variant, funct3 carries
the RoCC operand flags — and gives the reason, that Vanadis reads funct3 as operand flags
and would have been told `FORK` touches no registers. **DESIGN §23.7 D:2239-2247 still
carries the superseded form** — "`funct3` 0-5" as the group selector and "Context-register
indices ride the five-bit register fields, read against a different file" — and nothing in
DESIGN records that §26.6 replaced it. That un-recorded supersession inside tier 3 is
ledger **L45**.)*

| element | value | tier and source |
|---|---|---|
| opcode | **`custom-0`, `0b0001011` = 0x0b** | **3** — §23.7 D:2245, "Reserved and taken: `custom-0` (`0b0001011`)" |
| `custom-1` left free; `custom-2`/`custom-3` avoided | RV128 claims 2 and 3 | `[SST-only — implementation choice]` — `nmfc_isa.h:18-20` |
| **`funct3` = RoCC operand flags** — `XD = 0x1` (writes rd), `XS1 = 0x2` (reads rs1), `XS2 = 0x4` (reads rs2) | Vanadis's bit order, which is what an instruction must agree with; the canonical RoCC diagram numbers them the other way | **3** for the rule — §26.6 D:2907-2912. `[SST-only — implementation choice]` for the three bit VALUES — `nmfc_isa.h:38-40` |
| **`funct7` = group in bits 6:4, variant in bits 3:0** | three bits for six groups, four for the widest variant (a context-lane move: a direction plus a lane) | **3** for the split — §26.6 D:2908. `[SST-only — implementation choice]` for the shift/mask constants — `nmfc_isa.h:50-52` |

**The six groups, and the two reserved.** `[SST-only — implementation choice]` for the group
*numbers*; the **reservation** of two slots for `KILL` and mailboxes is tier 3
(§23.7 D:2246-2247: "`funct3` 0-5, with 6 and 7 left for §23.5's `KILL` and mailboxes"),
and the *membership* of every group is tier 3 (§23.2 D:2011-2017 host side, §23.3
D:2095-2098 function side, §23.6 D:2191-2192 context registers).

```
NMFC_G_FORK  0x0   FORK.R  FORK.M  FORKF.R  FORKF.M
NMFC_G_PROBE 0x1   FORKQ   JOINQ
NMFC_G_JOIN  0x2   JOIN
NMFC_G_END   0x3   END  (+ RETURN BIT -- one instruction, two forms)
NMFC_G_CONT  0x4   CONT    CONT.M
NMFC_G_CTX   0x5   CXW     CXR
0x6, 0x7 reserved -- KILL, mailboxes, and RESUME (privileged)
```

**TWELVE. The count is settled.** [RULED — user ruling 2026-09-02, verbatim: "**RETC and
ENDC are the same instruction, with a return bit.**" Ledger **L44** is closed, and DESIGN
§23.7 D:2241 — "**Twelve** instructions fit one RISC-V `custom-*` opcode" — is the half of
tier 3 that was right. §23's thirteen-mnemonic enumeration counted the two END forms
separately.] The base set is:

**`FORK.R` `FORK.M` `FORKF.R` `FORKF.M` `FORKQ` `JOIN` `JOINQ` `END`(+ret bit) `CONT`
`CONT.M` `CXW` `CXR`.**

**Plus `RESUME` (R20), which makes thirteen — and it is PRIVILEGED**
`[RULED — user ruling 2026-09-03 O16: "Yes, privileged."]`, so it is not part of the
user-level twelve. It takes one of the reserved group slots (`0x6`/`0x7`); **which one the
canon does not say**, because user ruling 2026-09-03 O3 makes every field value an
implementation choice. See I.3 and I.6. **A.2, C.4 and this Part now all say twelve base
plus a privileged RESUME.**

Variant bits. [DISAMBIGUATED — an earlier revision claimed these were "consistent
across groups so a reader can decode without a table". They are not, in two ways, and
the table below is REQUIRED. The claim being corrected is `nmfc_isa.h:67`'s own comment
("Variant bits, consistent across groups so a reader can decode without a table"), which
that earlier revision had transcribed without naming — so the document corrected a source
it did not cite.]

`BIT_M = 0x1` — context from memory at rs2 rather than a context register.
  Groups: FORK (`0x01`, `0x03`), CONT (`0x41`).
`BIT_F = 0x2` — fire-and-forget. **FORK group only** (`0x02`, `0x03`).
`BIT_R = 0x1` — **THE RETURN BIT.** END group only: the ending message carries the register
  file (`0x31`) or does not (`0x30`). **This bit is the whole of the difference between the
  two END forms** — user ruling 2026-09-02, and it is why the base set is twelve and not
  thirteen.

**All three bit values, and every funct7 constant below, are `[SST-only — implementation choice]`**
(`nmfc_isa.h:71-104`). What each variant *distinguishes* is tier 3 — the memory fork form
(§23.2 D:2012), fire-and-forget (§23.2 D:2013-2014), the return bit (§23.3 D:2103-2104)
and the lane (§23.6 D:2191-2192) — but **no tier-1..3 source assigns any of them a
number.**

**Two documented exceptions, and both are deliberate:**
1. **`BIT_M` and `BIT_R` are the same bit position with different meanings in different
   groups.** Bit 0 means "from memory" in FORK and CONT, and "returns the regfile" in
   END. **The group must be decoded first; the variant bit is not group-independent.**
2. **The CTX group (`0x5`) does not use the variant field as a flag word at all.** `CXW`
   is `0x50 + 2n` and `CXR` is `0x51 + 2n`, so **bit 0 selects write-vs-read and bits 3:1
   carry the lane number `n`.** Neither `BIT_M` nor `BIT_R` applies. This is the right
   trade — the lane is a compile-time constant at every call site and putting it in a
   register would cost an instruction to produce a number the compiler already knows —
   but it means the CTX group is not decodable without this note.

**EVERY `funct7` AND `funct3` VALUE IN THE TABLE BELOW IS `[SST-only — implementation choice]`**
(`/mnt/md0/NMFC-Rev/src/nmfc/include/nmfc_isa.h:77-104` for funct7,
`:113-126` for the operand placement). **The mnemonic, its meaning and its operand roles
are tier 1 and tier 3 in every row**, and those citations are the last column. Ledger
**L43**.

| instruction | funct7 | funct3 | rd | rs1 | rs2 | source for the INSTRUCTION (not the encoding) |
|---|---|---|---|---|---|---|
| `FORK.R`  | `0x00` | 7 (XD\|XS1\|XS2) | rH | PC | ctx-reg **number** | **1** #221 ("Fork from reg… a PC + context reg"), #224 (returns a handle); **3** §23.2 D:2011 |
| `FORK.M`  | `0x01` | 7 | rH | PC | address, dereferenced by the TILE | **1** #221 ("Fork from memory… a PC + context address"), #222 ("that is the whole idea"); **3** §23.2 D:2012, D:2034-2037 |
| `FORKF.R` | `0x02` | 7 | rH | PC | ctx-reg number | **1** #221 ("FIRE + FORGET or RETURN + JOIN"); **3** §23.2 D:2013 |
| `FORKF.M` | `0x03` | 7 | rH | PC | address | **3** §23.2 D:2014 — the fourth form exists to keep the four encodings uniform (D:2028-2032) |
| `FORKQ`   | `0x10` | 1 (XD) | rN | x0 | x0 | **1** #225 ("there is a real number that it should be"); **3** §23.2 D:2015, D:2042-2056 (why a count, and why it earns an encoding) |
| `JOINQ`   | `0x11` | 3 (XD\|XS1) | rOK | handle | x0 | **1** #221 (`PJOIN`, "returns 1 if the entry has returned"); **3** §23.2 D:2017, D:2038-2039 ("answering 'has it returned' without moving 512 bits") |
| `JOIN`    | `0x20` | 7 | rOK | handle | dest ctx number | **1** #86 ("A JOIN IS THE INSTRUCTION THAT TRIES TO RETRIEVE THE VECTOR REG"), #136 (the join chooses where to deposit); **3** §23.2 D:2016 |
| `END` (ret bit clear; assembler `ENDC`) | `0x30` | 0 | x0 | x0 | x0 | **1** #223 ("one that just ends the context"); **1** user ruling 2026-09-02 (one instruction, a return bit); **3** §23.3 D:2096 |
| `END.R` (ret bit set; assembler `RETC`) | `0x31` | 0 | x0 | x0 | x0 | **1** #223 ("RETC is probably good"), #79 ("THAT ONE INSTRUCTION WOULD BE THE RETURN CALL"), #222 (not `ret`); **1** user ruling 2026-09-02; **3** §23.3 D:2095 |
| `CONT`    | `0x40` | 2 (XS1) | x0 | PC | x0 | **1** #223 ("a 'continue' instruction which just passed forward it's own context"), #181 (extension, never fan-out); **3** §23.3 D:2097, D:2126-2131 |
| `CONT.M`  | `0x41` | 6 (XS1\|XS2) | x0 | PC | address | **1** #225 ("CONT.M should replace the context wholesale"); **3** §23.3 D:2098-2102 |
| `CXW` lane *n* | `0x50 + 2n` | 6 | x0 | value | ctx number | **1** #231 ("a subset of bit-manip instructions added so that values can be retrieved/set"); **3** §23.6 D:2191 |
| `CXR` lane *n* | `0x51 + 2n` | 3 | rD | ctx number | x0 | **1** #233 ("We need to make sure EXTRACTION from the regs is possible"); **3** §23.6 D:2192 |
| **`RESUME`** — **PRIVILEGED** (RULED, user ruling 2026-09-03 **O16**) | **UNASSIGNED BY THE CANON, AND DELIBERATELY SO** (O3) — an implementation takes one of the reserved `0x6`/`0x7` groups | 2 (XS1) | x0 | FTU handle | x0 | **1** user ruling 2026-09-02 **R20** ("send a 'resume' instruction with the context handle to resume it"). **No tier-2, tier-3 or tier-4 source has it at all — it is new in this revision.** |

**Nothing in tier 1 or tier 2 names an encoding at all.** `grep -rn 'NMFC_G_FORK\|custom-0'
src/nmfc inc/nmfc tools/nmfc` returns **no hits** — ChampSim has no decoder, no opcode
table and no assembler (I.10). **So the instruction SET is ratified to tier 1 and tier 3
row by row, and the ENCODING is not ratified at all.**

**The operand convention is load-bearing: every operand is a VALUE in a general
register.** A context register is named by a **number held in a GPR**, not by a
five-bit field read against a different file, **because RoCC hands an accelerator
register *values* and only rd's index.** The old field form could not survive that.
That costs an `li` at the call site and buys **one instruction set that decodes on both
hosts.** *(Tier 3, verbatim: §26.6 D:2914-2917. **The five-bit field form it replaces is
still what §23.7 D:2247 says** — "Context-register indices ride the five-bit register
fields, read against a different file" — and DESIGN records the supersession nowhere.
Ledger **L45**.)*

`[DISAMBIGUATED — WHICH ORDER GOVERNS, because the assembly syntax in I.2/I.8 and the
table above DISAGREE on two instructions and an assembler cannot be written without a
rule.]`

> **THE TABLE GOVERNS. `funct7`/`funct3`/rd/rs1/rs2 as printed above is the encoding;
> the mnemonic operand order is assembler surface syntax and is NOT required to match
> it.**

**Two instructions match; two do not, and the two that do not are the two that were
never stated:**

| mnemonic as written | maps to | in order? |
|---|---|---|
| `FORK.R rH, rPC, cCTX` (I.2) | rd = `rH`, rs1 = PC, rs2 = ctx-reg number | **yes** |
| `CXR rD, cS, lane` (I.8) | rd = `rD`, rs1 = ctx number, rs2 = `x0` | **yes** |
| **`JOIN rOK, cDST, rH`** (I.2) | rd = `rOK`, **rs1 = handle (`rH`)**, **rs2 = dest ctx number (`cDST`)** | **NO — the 2nd mnemonic operand is rs2 and the 3rd is rs1, REVERSED** |
| **`CXW cD, lane, rS`** (I.8) | **rs1 = value (`rS`)**, **rs2 = ctx number (`cD`)**, rd = `x0` | **NO — reversed, and OPPOSITE to `CXR` in the same group (`NMFC_G_CTX`)** |

**Why the mnemonics read that way, so they are not "fixed" into the wrong thing.** Both
follow the ordinary *destination-first* assembly convention — `JOIN` names the register
file it deposits into before the handle it draws from; `CXW` names the context register it
writes before the value it writes. **RoCC's field order is a hardware constraint (rs1/rs2
are the two register *values* the accelerator receives, rd the one index it returns), and
the two conventions simply do not coincide for a write-to-context instruction.**

**For an assembler writer: emit from the TABLE.** For `JOIN`, `rs1` ← the handle operand
(3rd), `rs2` ← the destination-context operand (2nd). For `CXW`, `rs1` ← the value operand
(3rd), `rs2` ← the context operand (1st), lane → `funct7` bits 3:1. **`CXW` and `CXR` are
deliberately asymmetric in mnemonic order and symmetric in encoding**; that is a property
of the group, not a mistake to normalise away.

**Why the re-encoding was done, and why now.** The earlier encoding used `funct3` as the
group selector, which a RoCC host reads as **"uses no registers" for `FORK`**. Re-encoding
"once traces and tools exist" is churn that invalidates results — **which is exactly why
it was done before any result was taken from this ISA.** *(Tier 3: §26.6 D:2907-2912 for
the defect, D:2919-2921 for the timing argument, quoting §23.7 D:2241-2243 back at
itself.)*

**The lane is in `funct7`, not a register,** because it is a constant at every call site
and making it a register would cost an instruction to produce a number the compiler
already knows.

### I.10 What ChampSim actually builds instead, and how to read it

**ChampSim implements the *semantics* of this ISA but encodes them as trace-record kinds
plus the offload aperture, not as opcodes.** The mnemonics `FORKF`, `FORK.M`, `END`/`RETC`,
`ENDC`, `CONT`, `JOINQ`, `FORKQ`, `CXW`, `CXR` and `RESUME` **do not appear anywhere in the
ChampSim tree as instructions**; there is no decoder, no opcode table and no assembler
for any of them.

[CORRECTION — an earlier revision of this section claimed an "exhaustive grep ...
returns zero hits" for `FORK` as well. That is FALSE and a reader who re-ran it would
have distrusted the rest of the paragraph.] `FORK` does occur, three times, none of them
an instruction: `src/nmfc/nmfc_host_core.cc:143` prints the statistic line
`"{} FORK/JOIN forks: {} joins: {} already home at join: {} ({:.1f}%)"`;
`tools/nmfc/kernels/bfs_nmfc.cc:107` is the preprocessor guard `#ifdef NMFC_FORK_JOIN`;
and `src/nmfc/nmfc_producer.cc:162` refers to the generator's `--fork-window`. **The
substantive point survives — the ISA is encoded as trace-record kinds, not opcodes — but
state it as what it is.**

What exists is:

| architecture | ChampSim encoding |
|---|---|
| `FORK` | `op::CALL` record, `source_memory[0]` rewritten into the aperture at `base + (token << 6)` |
| fire-and-forget | `FLAG_NO_RETURN` on a CALL |
| fork-now/join-later | `FLAG_DEFERRED_JOIN` on a CALL |
| `JOIN` | `op::JOIN` record naming the **same** aperture address — an entry already existing *is* the distinction |
| `END` (return bit set) | `op::RET`, `aux0` = live regfile words |
| `CONT` | **not present** |
| spawn | `op::SPAWN` — **DELETED (user ruling 2026-09-02 R1, "delete it. CONT/extend is fine, and can stay"); see I10 and ledger L3** |

The trace opcodes are `HOST, CALL, BODY, RET, PAGE_HINT, ATOMIC, JOIN` — **`SPAWN` was the
eighth and is deleted under R1**;
the four flags are `FLAG_NO_RETURN, FLAG_TAKEN_TARGET, FLAG_DEFERRED_JOIN, FLAG_SPAWNED`;
the three regions are `STANDARD, NMFC, CODE`; `MAX_FUNCTION_REGS = 8` × 64 bits = **512
bits = 64 bytes = one cache block**, and that is the only place the number is defined
(`inc/nmfc/nmfc_trace.h:42-192`). **[TIER-2 IMPLEMENTATION LAYOUT, NOT THE DESIGN — and
this is the exact place the 512-bit rule gets lost.** The `8` is how a **fixed-width trace
record** spells "one cache block"; it is **not** a statement that the context is eight
registers. The design is **512 bits, BIT-PACKED** — "*It could be 16 4-byte regs, 64 1-byte
regs, or ANY combination. Bit-packing is the name of the game*" (#232), "*Once again, NO.
512 bits of context. The context is not 8 regs. Why do you keep reverting to that?*" (#238).
**SST's `NMFC_CTX_WORDS = 8` / `in[0..7]` context array is the same convenience one tier
lower** (tier 4, Appendix 2 **S4**), and is not the design either. **A reader who takes
either constant as the register file's structure has re-created the regression invariant 2
exists to stop.]** The record is 96 bytes, the header 64 bytes, and the
header's geometry fields are **a contract validated against the running configuration,
aborting on mismatch** — not documentation.

**Read that table as an encoding, never as the machine.** The aperture is how a fixed
trace record expresses a new instruction; the architecture's `FORK` is an instruction.

### I.11 THE MEMORY-COMMITTING LOOP HAS NO INSTRUCTIONS — RULED, and the primitives were a ChampSim artefact

[ADDED. C.4 records TWO official invocation loops, both kept. Part I gave instructions to
ONE of them, and I.7's "deliberately absent" list did not name the other's primitives, so
a reader counting the ISA against the design found a loop with no way to express itself.
`grep -in 'commit'` over Part I returned zero hits.]

**RULED, AND THE ANSWER IS (a): NO INSTRUCTIONS ARE NEEDED.** `[user ruling 2026-09-02
R14, verbatim: "**That was an artifact of the ChampSim design, since ChampSim doesn't model
coherence, data, or atomics. By necessity, a core must poll said block to see if the
writeback of the data has occurred (coherence propagated). Potentially using atomics.**"
Ledger **L47** is closed.]`

**THE REAL MECHANISM, in three lines, and it needs nothing new:**
1. **The function COMMITS with an ordinary store.** It writes the block. That is all a
   commit is.
2. **COHERENCE makes the write visible.** The store lands in the tile's slice; the
   directory at the L2↔LLC boundary propagates it; the host sees it. **This machine has
   coherence (I13, I14), so publication is a property of the memory system, not of an
   instruction.**
3. **The host POLLS the block** with an ordinary load until it sees the value — and uses an
   **atomic** if what it needs is a read-modify-write on that block rather than a read.
   That is what I.1 already requires: *"software spins if it wants to wait; the hardware
   never does it on software's behalf."*

**WHY THE PRIMITIVES EXIST AT ALL, AND WHY THEY ARE NOT AN ISA QUESTION.** ChampSim models
**neither coherence, nor data values, nor atomics**. A trace-driven simulator therefore
cannot observe "the writeback happened" and cannot replay a spin that executed zero times
in the traced run. `__nmfc_wait` and `NMFC_COMMIT` are **markers that let the annotator
reconstruct what a coherent machine would have done** — a simulator encoding, exactly like
the offload aperture (I1, ledger L1). **Reasoning from them back to the architecture is the
same error the user has corrected once already.**

**Option (b) — reserving `COMMITQ` encodings in group `0x6`/`0x7` — is NOT taken.** The
reserved space stays for `KILL`, mailboxes and `RESUME`.

**What is required** (tier 1 #130/#131/#132, 2026-08-29T03:56-03:59; tier 3 §4.3
D:369-415): the value is not returned, the invocation **writes a block to memory** and the
caller reads it later; ownership is **by address**; self-checking must ensure **never a
double commit, never a double block**. **The "symbol hook" and the "primitive meaning
block until address A is committed" belong to the TRACE, not to the machine** — that is
what R14 settles.

**What is built — at tier 2, and as trace markers rather than opcodes:**

| requirement | what exists | source |
|---|---|---|
| "I block until commit here" (the host) | **`__nmfc_wait(const void* p)`** — a `noinline, used` function that *touches* the address (a `volatile` load), so the wait has one identifiable PC and leaves an address in the trace to resolve against. "*A trace records the addresses an instruction accessed, not the values in its registers, so a marker that only received a pointer would leave nothing behind.*" | `tools/nmfc/kernels/nmfc_kernels.h:46-60`; `bfs_nmfc.cc:37`, called at `:361` |
| "I commit work here" (the function) | **`NMFC_COMMIT(p, v)`** — a two-byte `nopl 0x2a(%rax)` marker immediately before the publishing store. **It cannot be a call**: "*a call from inside an offloaded function pushes a return address, and a function core has a register file and no stack; it would also take the program counter outside the function's range and split the body in two.*" | `nmfc_kernels.h:63-81` |
| the wait site, captured | `readelf` for `__nmfc_wait` → `waits.txt` | `tools/nmfc/kernels/trace.sh:36-40` |
| the commit sites, captured | `objdump` for the marker → `commits.txt`; **"a commit is a commit by being marked"**, never inferred from whichever store came last | `trace.sh:42-46`; K.7's non-negotiables table |
| ownership by address | `annotate.cc:417-424` — which invocation committed which block, and the commit's own address is the terminator's, not the slot base | `tools/nmfc/annotate.cc:403-441` |
| never a double commit | fatal: "*invocation N commits block B, which invocation M committed and nothing has blocked on — a double commit*" | `annotate.cc:751-774` |
| never a double block | fatal: "*invocation N reached a second commit marker before publishing the first*"; and an invocation still held when the next starts "*was never waited on*" | `annotate.cc:611, 752-755` |

**So the loop is expressible, checked, and measured — and it correctly has no opcode.**
**Part I is complete without one**, and I.7's "deliberately absent" list is right not to
name a commit or a wait: they were never absent, because they were never instructions.

**Do not write "the memory-committing loop is unimplemented"** — it is implemented, in the
toolchain, and K.7 is where its non-negotiables live. **And do not write "the ISA is missing
a commit primitive"** — user ruling 2026-09-02 R14 says the primitive is a store, the
publication is coherence, and the wait is a poll.

---

## PART J — MIGRATION: SEMANTICS, COST, BUDGET

### J.1 The payload, exactly

**72 bytes = a 512-bit (64-byte) register file + an 8-byte program counter.**

*User #91, 2026-08-28T19:55:42Z, doing the arithmetic himself and correcting an error:*
"**512-bit vector + 8-byte pc is 72 bytes. Why did you list it as 80?** ... All of those
messages are roughly the same size, **72 bytes or less**."
*User #291 item 7, 2026-09-02T12:37:03Z, the newest restatement:* "Migration is modeled
properly? Goes to the right tile? **Transmits 512 bit regfile + PC (the exact same
amount that would be needed for a fabric transfer, 64 bytes + address)?**"

**What travels and what does not.** The body does **not** travel — a migrating context
carries a *pointer* to code that is already replicated on every channel. Translations do
**not** travel (F.7). The **PC does not change**, because the code is one virtual address
aliased per channel.

### J.2 Parity, and why it is parity — the user's own reversal

The claim is not that migration is cheap in the abstract. It is that **migration and the
foreign fetch it replaces are alternatives, never both**, and cost the same order.

*First position, user #174, 2026-08-29T17:28:40Z:* "migration is actually very bandwidth
inefficient. ... we must transmit the full regfile + PC to a different core (72 B) and
then spin up execution there. Compared to just fetching the data to the core from that
other channel, which would likely be 8 bytes (even if we transmit the entire cache line,
we are looking at 64 bytes). **The only reason we migrate is to enforce atomicity and
make sure coherence is a non-issue.** The question is, is it worth it?"

*Superseded nine minutes later by user #175, 2026-08-29T17:37:11Z — THIS IS THE CANON:*
"**The question is if any real fabric supports a sub-cache-line data transfer, and I
think the answer to that is probably no.** If this is the case, migration is actually
interesting, because it says '**transfer the work, not the data**' and costs roughly the
same. **Then, atomicity does fall out for free this way, with no bandwidth overhead. I
was assuming migration traffic was additive to data traffic, but they are actually
subsuming.** Fabric traffic is not more than it would have been without the migration
(or at least, within reason of each other. The PC bytes are additional, but we should
consider that the fabric transfers more than just the block when moving data as well).
So, if work migrates, NUCA and NUMA are irrelevant except for the key point: **If there
is any runtime optimization to be made at all, it is to make sure migration remains
classified as sub-optimal.**"

Three things follow, and all three are load-bearing:
1. **Migration traffic SUBSUMES data traffic; it is not additive.** A machine that
   migrates does not move more bytes across the fabric than one that fetches, within the
   margin of a PC and a header.
2. **Atomicity is therefore genuinely free.** The property that a read-modify-write is
   serialised by the one core owning the address — no travelling lock, no protocol, an
   unambiguous ordering point, and **no coherence question because no second copy
   exists** — costs no bandwidth at all. It falls out of a data movement the machine was
   going to perform either way.
3. **`[CORRECTED — this point previously merged TWO different claims into one sentence,
   and DESIGN §26.4 corrects exactly that framing by name. The merged form licenses a
   repair §26.4 forbids.]` PARITY and SUBSUMPTION are separate claims with separate
   tests.**

   - **PARITY (invariant 11) is a CROSS-MACHINE claim.** DESIGN §26.4 D:2851-2856,
     verbatim: "**Invariant 11's parity is a cross-machine claim.** 72 bytes of register
     file against the 64-byte line 'a foreign access would have cost' is a comparison
     against the ***baseline* machine — a conventional core pulling the line — not against
     an alternative path inside this one.** Testing it therefore needs **§24 step 0, the
     stock-GAPBS baseline on the same stack, and not a second mechanism in the tile.**"
     **So parity is tested by M.3's baseline reproduction, not by anything inside this
     machine.** DESIGN §26.4 lists this among three things that "look like omissions and
     are not" — alongside "**there is no remote data path, and there must not be**"
     (D:2845-2850: a foreign access migrates, it never fetches; a remote read would
     destroy the construction that makes tile-local atomicity sound, in exchange for
     nothing the architecture wants). **The merged sentence this point used to carry —
     "parity is only true if migration and data contend for the same interconnect" —
     invites building a remote data path in the tile so that parity can be measured inside
     one machine. That is the repair §26.4 forbids by name. Do not do it.**
   - **SUBSUMPTION is the claim that needs ONE interconnect**, and that requirement is
     real: migration traffic does not *add* to data traffic only if both contend for the
     same links and the same budget. That is §24 step 3 and §26.0.1 (D:2276-2280,
     D:2653-2661), and R50 states it correctly. A second network for NMFC traffic makes
     **subsumption** unmeasurable — not parity. This is why I13 insists on one fabric and
     why the user asked for migration "**preferably as a generic fabric packet, not it's
     own channel**" (#227).

   **Test parity against the baseline machine; test subsumption on one fabric with a byte
   cost. Neither test substitutes for the other.**

   **AND PARITY HAS A PRECONDITION INSIDE THIS MACHINE, WHICH IS NOW RULED.**
   `[RULED — user ruling 2026-09-03 **O5**: "**a.**"]` Parity is a cross-machine claim, but
   it is only *expressible* if this machine prices a 72-byte migration and the 64-byte fill
   it replaces the same way. **It does, because MIGRATION and FILL are arbitrated at EQUAL
   WEIGHT** on the one fabric's per-destination queues, with **COHERENCE strictly ahead of
   both** (C.5a, H.8). **Option (b) of that ruling — MIGRATION strictly ahead of FILL — was
   refused precisely here:** under it the fabric would systematically favour the 72 bytes
   over the 64, and the sentence "*they are alternatives, never both, and cost the same
   order*" would have no meaning to test. **Equal weight is not a scheduling preference; it
   is what makes invariant 11 a claim rather than a slogan.**
   `[RULED — SST CARRIES BOTH CLAIMS, AND CHAMPSIM IS NOT BEING REPAIRED. User ruling
   2026-09-02 R4, verbatim: "**ChampSim doesn't have a byte model, just a cycles-to-transmit
   model. No need to back port it, SST is correct.**" And R3: "**ChampSim updates stop.**"]`

   **What ChampSim is, stated as a property rather than as a defect.** It has two separate
   interconnects — `cpu0_fabric` (`INTERLEAVE_FABRIC`, hop 4, queue 64) for host L2C misses
   to the slices, and `fn_fabric` (`FUNCTION_FABRIC`, hop 8, queue 128) for invocations,
   migrations and completions on per-destination deques, sharing no queue, no bandwidth
   budget and no latency — and it prices the fabric in **messages and cycles to transmit**,
   never in bytes. **Both are now declared characteristics of the ChampSim model.** L7 and
   L25 are closed on that basis: neither is being fixed, because ChampSim is frozen and the
   user has said the byte model does not need back-porting.

   **Where each claim comes from, and this is the operative instruction:**
   - **I11 SUBSUMPTION comes from SST**, which charges `MigrationEvent::SIZE_BYTES = 72` on
     the same links coherence and line fills use, over **one** fabric. **Say so at every
     quotation.**
   - **I11 PARITY comes from the §24 step-0 baseline comparison (M.3)**, and no amount of
     fabric merging on either simulator produces it.
   - **ChampSim migration numbers are message-count and latency numbers.** They are not
     wrong; they are answering a different question. Never quote one as a bandwidth or
     subsumption result. See I13's `[CONFLICT]` block, ledger **L7** and **L25**.

### J.3 Starting a context elsewhere is nearly free

Three structural reasons plus a measurement:
1. **There is no data locality to abandon.** An LLC slice holds only the addresses its
   own tile owns. A context migrating from A to B is going *because* it needs an address
   B owns — an address A's slice was never going to hold and never did. **Nothing warm
   is left behind.**
2. **The code is already there.** Function bodies live on duplicate pages, so every
   channel holds a copy. Arriving does not fetch code across the fabric.
3. **There is no stack and no cached working set to reconstruct.** A context is a
   register file and a program counter, which is precisely what makes it portable.

Measured, four tiles: **migration cold start 2.2 / 2.3 / 2.2 / 2.3 cycles mean**, with
**100.00% function-core instruction-cache hit rate** on every tile (2, 8, 2, 2 misses
against 10^5–10^6 accesses).

*User #176, 2026-08-29T17:41:40Z:* "locality is not really an issue either. ... **It is
actually really simple to start up a context, which is why it might be worth it to
migrate.**"

**Caveat (H.5): the 100% figure is a measurement under that model, not a property to
design around.** Model the cache.

### J.4 The budget, and the ceiling — two different tests

**Budget: roughly one migration per thousand instructions**, and it is a **latency**
budget, not a bandwidth one. *User #89, 2026-08-28T19:50:34Z:* "**1 v 1k is probably a
fine migration interval.**" *User #88:* "One that executes 300K instructions and migrates
10-20 times is perfectly acceptable." *User #18, 2026-08-27T17:22:20Z, on the framing:*
"200k executed instructions vs. 3k migrations isn't that bad, we just transport the
function over the fabric, so this isn't the worst. **What can become really bad is when
we don't have enough functions to keep memory saturated while some functions migrate.**"

**Ceiling (legitimacy): only DATA migrations may happen, and they may never outnumber
the loads and stores that caused them.** *User #291, 2026-09-02T12:37:03Z:* "**Any
migration due to instruction fetch or translation is by construction wrong. Then, only
data migrations should happen, and never outnumber the number of loads/stores issued by
a function.**" Measured on the **count** clause: 396,161 migrations against 524,288 loads
= **0.76 per memory operation**.

`[CAUTION — that measures the SECOND clause only, and this document elsewhere says the
assertion policing the FIRST is not installed in the configurations where it matters.]`
#291's first clause — *no fetch- or translation-induced migration at all* — is enforced by
`strict_locality` on the MMU `TILE_PORT`, and **`make_config.py:265-267` omits that port
entirely under `--walk-routing fabric`** (`if args.mmu and args.walk_routing == "local"`),
which **15 checked-in configs select** (F.6's census, ledger L26). **The record does not name which
walk-routing mode produced the 396,161-migration run.** So: the arithmetic passes; the
legitimacy half is **unmeasured, not passed**. Ledger **L37**.

**These are different tests, they measure different things, and — read carefully — ONLY
ONE OF THEM PASSES ON THE RUN THAT IS USUALLY CITED.** The budget is about amortising
transit latency over work done after arrival. The ceiling is about whether a migration
was legitimate at all.

| | budget | ceiling |
|---|---|---|
| the test | migrations / **instructions** ≈ 1e-3 | migrations / **memory ops** ≤ 1 |
| what it is about | latency amortisation | legitimacy |
| the cited run (396,161 migrations, 524,288 loads, 2,625,144 instructions) | **0.151/instr = 151 per thousand. MISSES by ~150×.** | **0.76/memop — the COUNT clause passes. The LEGITIMACY clause is UNMEASURED on this run** (the assertion is uninstalled under `--walk-routing fabric`, and the run's mode is unrecorded — L26, L37) |
| status | **an aspiration for a well-shaped function.** No run in the record meets it | **a hard gate, half of which has never been checked** |

**So "measured pass" means the CEILING'S ARITHMETIC passed.** Saying it without naming the
test — and, now, without naming *which clause* — is
how the budget came to look satisfied. And see the paragraph immediately below: **the
budget is not a quantity to minimise anyway.** A third number, **0.0015
migrations/instruction**, is the rate a correctly-shaped function reached in the spawn
experiments (K.4) and is the target for *decomposition* work — it is not a gate either.
**Three numbers, three purposes: 1e-3 aspirational, ≤1/memop enforced, 0.0015 as the
shape target.** Full table in **invariant 5** (Part B) — **not** in section I.5, which is
the function tracking unit.

**And a third statement that overrides the alarm tone of both: a migration RATE is not a
cost.** **§29.2 removed 4.96× of the migrations (1,703,838 → 343,858, the *edges-duplicate* row) and the machine got 38.2% slower (91.0 → 125.8 ms, the SAME row).** `[CORRECTED — was "five sixths"; Part L.]`
A migration count is **evidence about placement**, not a quantity to minimise. Every
earlier alarm in DESIGN.md about migration rates was reading the wrong number
(DESIGN §30.1 D:3415-3419).

**How to judge migration's impact, in the user's words (#93, 2026-08-28T20:07:50Z):**
"migration is not the end of the world. **We want to keep DRAM saturated, that is the
main goal. So migration's impact is demonstrated by how much it limits DRAM parallelism
and the load it puts on the fabric.**"

### J.5 A reserved fast path — proposed, deferred, and mostly dismantled

*User #169, 2026-08-29T08:47:37Z:* "If migration is unavoidable for the work, then we
need **a fast-path for migrating between cores (context reserved on all nmfc cores, can
migrate quickly as-needed and hopefully at significantly reduced bandwidth cost).**"
*And earlier, #93, 2026-08-28T20:07:50Z, explicitly as future work:* "**don't implement
this yet**, but we should consider what a function whose context is reserved across all
memory slices would look like. If we assume migration is unavoidable, then reserve the
context in each nmfc. Migration becomes far easier, and it isn't clear whether we would
need to transmit the entire regfile or just the PC and part of the regfile to migrate.
**Presumably each nmfc can snoop, so coherence between the regfiles could be maintained**,
we just jump which core we are on as we migrate to each data location."

**Status: deferred, and most of its case has since been removed.** §21 removed the
bandwidth half (migration is not paying a bandwidth penalty — it subsumes). §21.2 removed
the restart half (arrival is 2.2–2.3 cycles). What is left is *time in transit*, a much
smaller and more specific claim. **If it is ever built it must be measured as a
hardware-axis change against a FIXED decomposition**, so that a cheaper migration is not
credited with work a better-shaped function would not have migrated for.

---

## PART K — WHAT A UNIT OF WORK MUST BE

> **[RULED — HISTORICAL OBSERVATIONS. user ruling 2026-09-03 **O15**: "**a.**", selecting
> option (a) of that row.] EVERY MEASUREMENT IN THIS PART IS A HISTORICAL OBSERVATION OF AN
> EARLIER TREE.** Its configuration is **unreproducible from git** — no measurement here
> names the configuration file that produced it, the 4 MiB LLC slice several of them were
> taken at was never committed (L28c, lookup run), and the checked-in configurations cannot
> run against the checked-in workload (L20, E.3). **ChampSim stays frozen** (user ruling
> 2026-09-02 R3), so re-taking them is not an available action and was not asked for.
> **Quote every number here as a recorded observation whose configuration is unknown, never
> as evidence about the machine and never as something a reader can re-run. Do not tune a
> parameter to reproduce one.** The standing statement of all four facts is **N.0**.

**What is NOT historical in this Part:** K.1, K.2, K.5, K.6 and K.7 are **rules**, not
measurements, and they stand on their own tier-1 authority. The banner above governs the
**numbers** quoted in K.3 and K.4 — the measured rejections of CHASE and SPAWN
decomposition — whose *conclusions* were ruled by the user and whose *figures* are
historical.

### K.1 The rule

**A unit of work must OWN THE DATA IT TOUCHES.**

That is the whole of it, and every other rule in this part is a consequence. If a
function discovers work it cannot carry out itself, **the unit of work is shaped wrong:
it does not own the data it discovered. Reshape it rather than spawn.**

### K.2 The shape, in the user's words

*User #84, 2026-08-28T19:30:56Z:* "I never said the chase/claim was the wrong thing to
offload. I simply asked if you were choosing the right shape for each function. **A
function that constantly migrates, or only exists for a single instruction before
retiring, is the wrong shape. Ideally a function is a small hot-loop that doesn't
migrate.**"

*User #116, 2026-08-28T23:41:28Z:* "**functions need to do a non-trivial amount of work
and not migrate often.**"

*User #169, 2026-08-29T08:47:37Z:* "**Functions should be planted for most of their
runtime in one tile. They should do non-trivial work.** Migrations happening at the same
rate as memory accesses leads to double fabric bw utilization."

*User #39, 2026-08-28T00:44:05Z, on heterogeneity:* "**The functions can be heterogeneous
you know, and you shouldn't assume that the existing shape is the one that would work
best for this architecture.**"
*And #93:* "**You know you can split the work into two or more sets of functions right?**"

*User #134, 2026-08-29T05:02:26Z, on size:* "**Filling the contexts is just a subdivision
of work**, so I don't see it as something inherent to the workload. ... If the return of
the function is instantly consumed (join) before the next launch (fork) then
serialization is inevitable."
*And #121, 2026-08-29T02:31:33Z:* "**If you are divying up work for the entire program
across only 5 invocations, that is bad too.**"

**THE SUPERSEDED ORIGINAL FRAMING, kept because its per-algorithm table is still the best
statement of what "the right shape" means for kernels this project has not run.**
`[OMISSION CORRECTED — DESIGN §14.1 D:1162-1238, "The original framing (superseded)", was
never cited. It is superseded by §14.0 and by K.1's ownership rule — the framing was
"amortise the fabric round trip, cap context residency", which is a COST argument, and
K.1's rule is an OWNERSHIP argument — but the material below is not superseded and exists
nowhere else in this document.]`

**The two forces it names**, both real and both still operative: **too fine** and the
fabric dominates (a dispatch and a return cost a hop each, ~16 cycles at the configured
settings, plus a context slot); **too coarse** and one invocation monopolises a context —
"*a power-law hub with 100,000 edges becomes a single invocation that occupies one slot
for its entire duration while the remaining contexts sit idle — **the machine has hundreds
of contexts precisely so that no one of them matters, and a hub defeats that.**"

| kernel | natural slice | what it costs |
|---|---|---|
| **BFS (top-down)** | one vertex's neighbour scan, **chunked by edge count** | chunking is mandatory on power-law graphs and harmful on road graphs; the neighbour claim is a real read-modify-write, so this is the kernel that exercises the atomicity argument |
| **PageRank (pull)** | one vertex's gather | chunking needs a **reduction**, which fire-and-forget cannot express and which puts work back on the host. One vertex per invocation is the honest unit |
| **Connected components** (Afforest / Shiloach–Vishkin) | a chunk of **edges**, fire-and-forget | hooking is `comp[u] = min(comp[u], comp[v])` — an atomic with no value the host consumes. **The best fit for this architecture of any kernel here** |
| **SSSP (delta-stepping)** | a chunk of the current bucket | returns are needed: a relaxation can insert into a later bucket and the host owns the bucket structure |
| **Betweenness centrality** | as BFS, both phases | the backward accumulation depends on the completed forward sweep, so there is a barrier the host must enforce |
| **Triangle counting** | one edge, intersecting two adjacency lists | **the worst case for siloing** — it touches *two* vertices' neighbour lists, so it migrates unless both endpoints are co-located. "*A partitioner that co-locates edges rather than vertices would change this kernel's answer entirely.*" |

**And its measured conclusion, which G.5 and Part L both depend on and neither cited.**
kron-20, 1,500 vertex visits, four tiles: stripe and block partitioning gave **4,110,058
and 4,126,307 migrations** — *block partitioning bought nothing*. "*That is not a defect
in the placement pass; it is the pass being asked to do something it cannot. Partitioning
the LAYOUT by vertex id only helps if the GRAPH has locality in vertex id, and a kronecker
graph's edges are essentially random with respect to it.*" **So: placement alone is not a
strategy.** Getting value from it needs one of — **reordering the graph** (GAPBS ships
relabelling; METIS or Afforest-style clustering is the serious version), **a graph with
natural locality** (the GAP road networks, which is why they belong in the sweep), or **a
different unit of placement entirely** — co-locating edges rather than vertices, which is
also what triangle counting wants. `[NOTE — this is the same conclusion G.5 reaches from
the other end: the open problem is forming components smaller than the working set on a
dense co-access graph, which is a clustering problem. §14.1 and §21.1 are two statements
of one finding.]`

### K.3 CHASE decomposition — REJECTED, with the measurement

A slice of a loop has to **chase**: it walks a row, then reaches for a neighbour's value,
and that value is on whichever tile owns the neighbour. So the context migrates once per
edge and **migration becomes the mechanism rather than the exception**.

`[CORRECTED — the headline previously read "measured at 0.38 migrations per instruction —
three quarters of all work was a context moving itself", which is arithmetically
impossible and mixes two rows of the table twelve lines below. 0.38/instr is **38%**, not
three quarters. The three-quarters number is **0.7428**, and it belongs to a different
row and a different workload. This is the section that rules "a parity target quoted
without its row is meaningless"; its own opening sentence broke that rule.]`

**Measured — two rows, named:**
- **ChampSim, chase decomposition, GAP BFS: 0.7428 migrations/instruction.** *That* is
  "roughly three quarters of all work was a context moving itself."
- **ChampSim, chase decomposition, synthetic scattered: 0.384 migrations/instruction** —
  about 38%.

**Both condemn the shape; neither is the other.** Quote whichever row your workload
matches, and quote the row. (Appendix 3 item 8 lists "any migration count" among the
numbers that must never appear bare — L34.)

Measured more finely (DESIGN §20.2 D:1707-1713), on the edge-range BFS function:
```
accesses per invocation                2,227.7
distinct tiles touched, mean           3.53 of 4
  invocations touching all four        4,130 of 6,224
  invocations touching exactly one            52
tile switches per invocation           1,559.3
accesses between switches                  1.43
```
**An invocation changes tiles every 1.43 memory accesses.** Predicted switches (9.7M) and
observed migrations (8.9M) agree, **and the placement fix did not move them — this is the
decomposition, not the placement.**

The switch lands on the ordinary load of the scattered vertex value; **0% of switches
occur at an atomic**, because the compare-and-swap follows the load to the same address.

**PLACEMENT CANNOT FIX A SHAPE — the sharpest single measurement of this Part's thesis,
and it belongs here.** DESIGN §27.1 D:3100-3103: "**Placement cannot fix a shape.**
`first_touch` dispatched **131,034 of 131,072** invocations to the tile holding the first
address they would touch — **as close to a perfect first placement as the policy can get
— and bought 6%.**" (Note that `first_touch` reading the address a function *would* touch
is itself the rejected oracle of R47; this is therefore an **upper bound** on what any
real placement policy could achieve against a badly-shaped decomposition, and the bound
is 6%.)

**The cross-simulator migration-rate table, which is what M.3's parity caveat is
ABOUT** (DESIGN §27.1 D:3087-3095) — every number is migrations per instruction:

| configuration | migrations/instr |
|---|---:|
| ChampSim, chase decomposition, GAP BFS | **0.7428** |
| ChampSim, chase decomposition, synthetic scattered | 0.384 |
| **Rev (SST), chase decomposition** | **0.150** |
| ChampSim, chase decomposition, oracle placement | 0.020 |
| ChampSim, spawn decomposition | **0.0015** |

**Read it as a spread of four hundred to one produced by DECOMPOSITION and PLACEMENT
ORACLES, not by simulators.** The Rev-vs-ChampSim gap on the *same* nominal shape (0.150
vs 0.384–0.7428) is exactly why no cross-simulator comparison is admissible before the
baseline reproduction step (M.3, R104). **A parity target quoted without its row is
meaningless.**

`[NOTE — this is the "chase decomposition" the memory notes record as having been
rejected and then REBUILT.]` The reason it is rejected is in the numbers above: it owns
an *edge* range and chases *vertices* it does not own.

### K.4 SPAWN decomposition — REJECTED, with the reason

The alternative once proposed was: let a function **spawn** a function rather than return
work to the host — `expand(v)` touches only v's own data and spawns one `touch(u)` per
neighbour; work crosses the machine as a **token** rather than as a context. It measured
extremely well: on kron-24, **2,890 migrations against the chase shape's 924,095 — 320×
fewer — and 6.19× against the baseline**.

**It is rejected anyway, and the user rejected it explicitly** (#181,
2026-08-29T19:39:56Z): "**We already determined spawn decomposition is deadlock captive.**
It makes sense if you extend an invocation into a different function instead of returning
(the context carries forward, reserve in-place, essentially a successor). **It is not
acceptable to be spawning two or more contexts.** We don't have a way to manage that,
unless we create a FTU inside each nmfc core, and somehow guarantee a spawned piece of
work can never deadlock. **I don't see that guaranteeable without uintuitive constraints
(like you may only spawn contexts one deep).**"

*And #87, 2026-08-28T19:41:51Z:* "**Preferably it would be impossible to deadlock by
construction. A spawn from a spawn is by nature unbounded.**"
*And #88, 2026-08-28T19:45:29Z:* "**I am not convinced any function that forks is the
right shape.** It may be that any function that needs to nested-fork to any depth is not
the correct shape for the function."

**What survives from the spawn experiments is the NUMBER, not the mechanism.** Treat
**0.0015 migrations per instruction** as the rate a correctly-shaped function should
reach — never as an endorsement of spawn.

**What replaced it: `CONT` — extend, never fan out.** One becomes one; the context
carries forward; the FTU entry is inherited, so nothing is allocated and nothing can be
refused.

### K.5 The shape that works: own a VERTEX range and pull

The reshape that both §20.2 and §21.1 point at, and that was built: **own a *vertex*
range and pull, rather than own an *edge* range and chase scattered values.**

Why it works, stated as a property rather than a trick:
- **Every value it reads and writes is inside its own slice**, so the writes are local
  for the whole invocation and **the vertex it claims is one it already owns**.
- **It claims what it discovers rather than handing a list to the host.** Nothing is
  handed anywhere.
- **Ownership removes the atomics.** A vertex belongs to exactly one invocation, so with
  aligned ranges each bitmap word belongs to exactly one invocation too and a plain OR is
  safe; the parent write needs no compare-and-swap for the same reason.
- It returns **one 64-bit word in the register file** instead of committing to memory.
- Measured: reaches identical vertex counts at three scales and executes **0.75×** the
  reference's instructions. *(**DESIGN D:1728** — "*counts at scales 16, 17 and 18 and executes 0.75x the reference's instructions*" — and **D:1916**, "*0.75x the reference's traced instructions -- below 1.0 precisely because …*". `[CITATION ADDED — this number carried none.]`)*

**Contrast, for the host's sake:** with the edge-range shape the host must walk a list of
claimed vertices — **measured at 55% of all host memory accesses, with the queue pushes
another 45%, while the tiles sat 38% idle** (`tools/nmfc/kernels/bfs_nmfc.cc:417-421`,
verbatim, in the comment on the bottom-up offload: "*Compare the top-down step, where it
must walk a list of claimed vertices -- measured at 55% of all host memory accesses, with
the queue pushes another 45%, while the tiles sat 38% idle*"). `[CITATION ADDED — this is
the entire measured case for the shape that works and it carried no source. **Tier 2 only**;
no DESIGN.md line and no session-log item states it.]` Owning the vertex range removes that
entirely: the host's whole job becomes reading one word per invocation and adding it up.

### K.6 Admission: a test on BITS, and on PEAK SIMULTANEOUS LIVENESS

A function is admissible iff **every opcode in it is in `RV64IMAFD`** *and* its **peak
simultaneous liveness fits in 512 bits.** Two tests, both mandatory, and until this
revision the document stated only the second.

**THE SUBSET TEST IS NEW AND IT IS RULED.** [RULED — user ruling 2026-09-03 **O4**,
verbatim: "**I think we want float, so C.**" The subset is `RV64IMAFD`; the family was
already RISC-V under R11. I.0 carries the full statement and the per-letter reasons.]
**The test rejects any opcode outside `IMAFD`** — no CSR access, no `FENCE`, no compressed
encodings, no `ecall`, nothing from `V`, nothing from any other extension. It is a test the
admission tool does not perform today in either implementation, and it is now specified:
**walk the disassembly of the body and fail on the first opcode outside the subset.**

**AND FLOAT DOES NOT CHANGE THE SECOND TEST'S ARITHMETIC — IT ONLY ADDS TERMS TO IT.**
`[derived from ruling O4]` The context is **512 bits, bit-packed** (invariant 2, the
512-bit rule) — **not eight registers**, so there is no per-register slot for a float to
widen. A live `f64` costs **64 bits**, a live `f32` costs **32 bits**, and they are counted
into exactly the same 512-bit budget as every integer and pointer. **An `f`-named value and
an `x`-named value compete for the same bits**, because `f0`–`f31` and `x0`–`x31` are two
namespaces over one packed file (I.0, I.7). A function holding six live pointers (384 bits)
and two live `f64` (128 bits) is at **512 of 512** and is admissible; adding one live
`f32` makes it 544 and it is **not**.

**It is NOT a count of the registers a function names.** x86-64 names far more than it
holds at once — it spends a whole register on a 32-bit value and has no choice about
pointers — and a tracer reports `rax`, `eax` and `al` as three ids for one register.
**Counting names rejected a bottom-up BFS function that names twelve while holding about
480 bits of state, which fits the 512-bit file.**

**Widths count in bits.** A 32-bit value costs 32, not a whole 64-bit slot — that is what
"512 bits divided however the machine likes" means, and it is why the test is on bits.
**The same rule applies to floating point under O4:** an `f32` costs 32 and an `f64` costs
64, in the same budget. **Nothing about a value's type buys it a slot of its own.**

**A register that is never read is not state the join consumes, so it takes no slot at
all** (user #99: "If the reg itself is an artifact, and it isn't actually used (never read
or consumed by join) then it doesn't count.").

**Rejection is fatal and must not be softened.** Truncating would drop dependencies and
flatter the scoreboard. If it does not fit, the function cannot run on this machine:
rewrite it, split it into a `CONT` chain, or reject it.

`[CONFLICT — the IMPLEMENTED admission test counts 64-bit SLOTS, not bits. The bit
accounting is computed and only printed; it gates nothing.]`
`tools/nmfc/annotate.cc:524-527` builds a pool of `opt.num_regs` (**8**) slot ids;
`:542-553` allocates **one whole slot per live value** and, when the pool empties, calls
`die("function ... holds more than "+num_regs+" values live at once; it cannot run on
this machine")`. The bit accounting at `:555-559`
(`bits += reg_bits[reg] != 0 ? reg_bits[reg] : 64U;`) feeds `peak_bits_local` → `peak_bits`
(`:570`), whose **only consumer is a stderr line** at `:927`
("peak simultaneous liveness: %zu values, %u bits of %u in the register file").

**Two concrete wrong answers follow, in opposite directions:**
- a function holding **twelve live 32-bit values** = **384 bits**, which K.6 admits, is
  **rejected as fatal**;
- a function holding **eight live 64-bit values plus one 8-bit value** = **520 bits**,
  which K.6 must reject, is rejected **for the wrong reason** (nine slots, not 520 bits) —
  and would be *admitted* by any variant that widened the pool.

**AND A THIRD, WHICH IS NEW WITH O4 AND WHICH NEITHER IMPLEMENTATION CAN GET RIGHT
TODAY:** a function holding **four live `f64` and four live pointers** is at **512 of
512** and is admissible, but a slot-counting test that allocates `f`-names out of a
separate pool from `x`-names sees **four of eight and four of thirty-two** and admits a
function twice that size. **The pools must be ONE pool measured in bits**, because the
register file is one file (I.0). `[derived from ruling O4]`

`[CONFLICT — NEITHER IMPLEMENTATION CHECKS THE SUBSET AT ALL, AND ONE OF THEM CONTRADICTS
IT.]` The admission tool tests neither `M`, nor `A`, nor `F`/`D` membership — it tests
liveness only — so a body containing an opcode outside `RV64IMAFD` is admitted silently.
And **SST's tile is `RV64IM+A` only** (Appendix 2 **S6**), so it would *trap* on the very
floating-point instructions O4 admits. **Both are gaps against the ruled subset**; the
tool must gain the subset test and the tile must gain `F`/`D` — packed into the one
512-bit context, never into a second file.

**Tier 1 wins: the test is on BITS** (#232: "It could be 16 4-byte regs, 64 1-byte regs,
or ANY combination. **Bit-packing is the name of the game**"; #99: a register never read
"doesn't count"). **The implemented slot count is the pre-#232 test and must be replaced
by a comparison of `peak_bits` against 512.** Note the same gap from the other end in SST
(divergence S5: nothing produces a non-default layout, so the bits-used figure is always
512). **Neither implementation currently exercises the canon's admission test.** Ledger
**L30**. R30 in Part P states the rule correctly; it is the code that does not.

### K.7 The compiler owns instructions and PCs — the model does not

*User #82, 2026-08-28T19:23:51Z, after the model was found fabricating bodies:* "I would
recommend at this point writing the code so that **you cannot omit instructions, you
cannot generate pcs, leave that all to the compiler. Write actual C++ that is the body of
each function. Insert it into GAPS hot loops. Make sure the functions are not in-lined.
Trace it via the regular ChampSim tracer** (note what PC indicates each function
invocation). **Then, post-trace, do the annotation, the address remapping, etc.... That
takes you out of the loop where you were controlling what was in each body, how the
functions were formatted, etc.... You only control the code + data layout, exactly what
you need.**"
*Preceded by #80/#81:* "I think we need to fix how we do this, because it is clear we
cannot trust you to make these traces." / "**YOU CANNOT BE TRUSTED.**"

So: **nothing hand-writes instructions.** Function bodies are real C++ compiled into the
benchmark; their program counters come from the compiler and linker; the annotation pass
reads what was actually executed. The previous generators were deleted deliberately —
**they invented program counters, and everything they invented was wrong in the same
direction.**

**The trace pipeline's non-negotiables, each of which is a correctness requirement rather
than a tuning choice:**

| requirement | why |
|---|---|
| `-no-pie` | traced program counters equal the symbol table's |
| `-fno-ipa-icf` | do NOT fold two identical functions into one symbol — that is the bug being fixed |
| `-fno-optimize-sibling-calls` | keep the return as a real `ret` instruction |
| `-fopenmp` | **REQUIRED**: GAPBS substitutes serial fallbacks for its atomics under `#if defined _OPENMP`, so building without it compiles the compare-and-swap into a plain load and store |
| `noinline, noipa, noclone, used, aligned(64)` on an offloadable function | the body must survive as a call; no rewritten signature; no specialised copies at other addresses; no removal when a caller looks dead; the entry lands on a line boundary |
| `omit-frame-pointer` on an offloadable function | `push %rbp` is a stack write, and the machine has no stack |
| capture symbols, atomics, rets, ABI saves, waits, commits and the region manifest **in the same run as the trace** | "Re-deriving them later from a rebuilt binary silently describes a different program — rebuilding to add one line moved every symbol once already, and the addresses looked perfectly valid while naming nothing in the trace." |
| an instruction is atomic **because it is locked** (read from the disassembly), and a commit is a commit **because it is marked** | never inferred from what the source appears to say, or from whichever store happened to come last |
| a body address no declared region covers is **fatal** — with exactly two legal drops, the `ret` pop and ABI callee-saved push/pop | a genuine spill writes live state to a stack slot with a `mov`; its PC is not in that set, so it still errors. **That is the admission test, and it stays.** |
| the written trace is **read back and validated, and DELETED on failure** | three bad traces shipped before this existed: one where every function shared a code address, one where returning cost no instruction, and one whose header claimed 100 MB for a 600 MB file |

**A ChampSim-specific artefact that must not leak into the architecture.** ChampSim does
not model data values, so a returned value that is spilled and reloaded must have the
spill store and the reload **dropped**: on this machine the value sat in the FTU, not in
memory. *User #138, 2026-08-29T05:13:44Z:* "it is important to drop the store/load of
those operations, since the system using the hardware actually stored it in the FUT, not
the stack. **Otherwise we invent memory ops that wouldn't have existed.**"
Likewise the **join goes at the USE, not at the spill**: "A fork parks its result in the
tracking unit; the join is the instruction that deposits it somewhere the program reads."

**And a limit ChampSim imposes on the memory-committing loop:** *user #129,
2026-08-29T03:50:38Z:* "**We need to be careful here, there is no way for ChampSim to
model the busy-spin polling for those done bits.**" Hence the explicit wait hook, which
must **touch** the address rather than merely receive it — a trace records the addresses
an instruction accessed, not the values in its registers.

---

## PART L — REPLICATION IS AN OPTION, NEVER THE BASELINE

> **[RULED — HISTORICAL OBSERVATIONS. user ruling 2026-09-03 **O15**: "**a.**", selecting
> option (a) of that row.] EVERY MEASUREMENT IN THIS PART IS A HISTORICAL OBSERVATION OF AN
> EARLIER TREE.** Its configuration is **unreproducible from git** — no measurement here
> names the configuration file that produced it, the 4 MiB LLC slice several of them were
> taken at was never committed (L28c, lookup run), and the checked-in configurations cannot
> run against the checked-in workload (L20, E.3). **ChampSim stays frozen** (user ruling
> 2026-09-02 R3), so re-taking them is not an available action and was not asked for.
> **Quote every number here as a recorded observation whose configuration is unknown, never
> as evidence about the machine and never as something a reader can re-run. Do not tune a
> parameter to reproduce one.** The standing statement of all four facts is **N.0**.

**This Part is the sharpest case of it:** the §29 inversion table below, and every ratio
computed from it, was measured at a **4 MiB LLC slice that `git log --all -S'"num_sets":
4096' -- config/nmfc` shows was NEVER COMMITTED** (L28c). **The rule the Part states —
replication is an option, never the baseline — is TIER 1 and is untouched by this**; it
rests on user #298, quoted immediately below, not on the table. **The table is why the rule
was reached, and it is history.**

*User #298, 2026-09-02T17:44:03Z, the newest statement on this and therefore binding:*
"That is okay, but pretty undesireable. **We need to make that sort of aggressive
duplication an option, not bake it into the baseline. It is unclear whether that many
migrations are a limitation or not.**"

**The mechanism is always present, because a page type is something a program declares
and the machine must honour it. Nothing declares it by default.**

**Why, measured — the §29 inversion.** 131,072 vertices, ~1M edges, four tiles, same
kernel and same graph throughout, memory operations identical at 3.35M:

| configuration | migrations | per instruction | per memory op |
|---|---:|---:|---:|
| baseline, round-robin | 1,703,838 | 0.1519 | 0.509 |
| baseline, first-touch | 1,539,777 | 0.1420 | 0.467 |
| edges duplicate, round-robin | 343,858 | 0.0352 | 0.103 |
| edges duplicate, first-touch | 245,709 | 0.0254 | 0.073 |

**Replication removes 4.96× of the migrations** (1,703,838 → 343,858); **placement adds
another 1.40× on top of it** (343,858 → 245,709, for 6.93× overall), **having been worth
only 1.11× before** (1,703,838 → 1,539,777). *(The table and all four ratios: **DESIGN
§29.1 D:3307-3324**, "What the type buys, and what it costs to buy it".)* And then:

**AND WHAT THE TYPE ACTUALLY IS, which is the half this Part needs and had not cited.**
§29.1 D:3307-3311: BFS's **edge array is read by every core on every edge and written by
nobody after construction** — which is §5.0.1's definition of a duplicate page, "what
every core needs: instruction pages, read-only data". **Declaring it so is a statement
about the DATA, not about where it goes**; the copies are placed by the address space's
owner, one per tile. That is why replication is expressible with the compiler's two
levers (A.6) and why it is **not** the rejected "compiler picks a tile": §29 D:3292-3299
records that the reshaping which suggests itself — bucketing a vertex's neighbours by the
tile owning `parent[n]` — is exactly what invariant 12 rejects, because it makes the
*program* compute tile arithmetic on addresses and bakes the tile count into its data
structure.

| configuration | migrations | LLC slice miss rate | run time |
|---|---:|---:|---:|
| baseline | 1,703,838 | **5.4%** | **91.0 ms** |
| edges duplicate | 343,858 | 28.8% | 125.8 ms |
| edges duplicate + first-touch | 245,709 | — | 124.5 ms |

`[CORRECTED — this headline took **7×** from the *edges-duplicate + first-touch* row and
**38%** from the *edges-duplicate* row. That is the exact defect K.3 was corrected for one
Part earlier ("*mixes two rows of the table twelve lines below*"), and the rule this Part
states is "**a parity target quoted without its row is meaningless**".]`

**THE TWO ROWS, EACH COMPLETE. Quote a ROW, never a range and never a blend:**

| row | migrations | vs baseline | run time | vs baseline |
|---|---:|---:|---:|---:|
| **edges duplicate** | 343,858 | **4.96× fewer** (79.8% removed — **four fifths**) | 125.8 ms | **+38.2%** |
| **edges duplicate + first-touch** | 245,709 | **6.93× fewer** (85.6% removed — **six sevenths**) | 124.5 ms | **+36.8%** |

**The canonical one-liner, and the only form to propagate:**

> **§29.2 removed 4.96× of the migrations (1,703,838 → 343,858, the *edges-duplicate* row) and the machine got 38.2% slower (91.0 → 125.8 ms, the SAME row).**

`[NEVER-QUOTE LIST — add these four forms, all of which were live in this document:
**"five sixths"** (83.3%), which matches **neither** row and had reached Part A, invariant
5 and J.4 — the pages a reader is told to read first; **"5–7×"**, which is a range
spanning two rows; **"seven times fewer *and* 38% longer"**, which is one number from each
row; and any bare **"38%"** without its migration figure. Note also that the *more*
migrations you remove, the *less* slower the machine gets (+38.2% at 4.96×, +36.8% at
6.93×) — so a blended quotation does not merely lose precision, it inverts the trend.]`

**The mechanism, named by the slice statistics:** LLC misses went from **213,183 to
1,673,842**, and cycles spent asleep on a load from **53.3M to 96.0M**. Replicating an
8 MiB array onto four 4 MiB slices means every tile must hold all of it in a slice that
previously held only its ~2 MiB share. **Aggregate capacity for that array fell from
about 16 MiB to about 4 MiB, and what the migrations had been costing in fabric traffic
came back as DRAM traffic, with interest.**

`[CAUTION — THREE different baseline round-robin migration counts exist for what DESIGN
presents as the same nominal workload, and they differ by about 4%. Say which run a
count came from.]`

| count | rate | where | used for |
|---|---:|---|---|
| **1,638,325** | 0.150/instr | DESIGN §27.1 D:3084 | **the value in the cross-simulator parity table** (D:3092, and K.3 above) |
| **1,703,838** | 0.1519/instr | DESIGN §29 D:3317, D:3332 | the §29 inversion table immediately above |
| **1,605,541** | — | DESIGN §29 D:3341 | the later build, the build-independence re-measurement below |

**These are three runs, not three readings of one run**, and the spread is real but
small — which is itself the build-independence point. **It is not reconciled anywhere,
and a number quoted without its run is not evidence.** Ledger **L34**.

**Build-independence, which is the more useful half of the result.** Re-measured on a
later build carrying a store buffer, a derived remap budget and a page-table ordering
fix: **91.0218 ms / 125.226 ms / 124.004 ms**, migrations 1,605,541 / 343,886 / 245,702,
slice miss rates 5.4% / 28.8% / 28.0%. **The answer moves in the fourth significant
figure. The mechanism is capacity, and capacity does not care what else was fixed.**

**This is I11 behaving exactly as specified.** If migration is at parity with the
transfer it replaces, then **removing migrations cannot buy time — it can only move the
cost somewhere else, and here it moved it somewhere worse.**

**When replication DOES pay, and when it cannot work at all:**
- The edge array is 8 MiB and becomes 32 MiB. The working set goes from ~13 MiB to
  ~37 MiB, **2.85×**. **Replication pays only when the replicated object is small
  against a slice** — which is exactly why R-NUCA replicates *instructions*, and why the
  page-type table lists "instruction pages" first.
- **It works at all only because the largest structure is read-only.** The mutable state
  — the parent array, the frontier and its counter — **cannot** be replicated, and it is
  exactly what the residual migrations are. **A workload whose dominant structure is
  written gets nothing from this.**
- "Read-only by construction" is not "never written": a program **builds** one and then
  stops. The kernel-writes-are-duplicated clause is what makes that possible, and the
  MMU must fan a write to a duplicate page out to **every** copy or the other N−1 tiles
  compute on garbage.

**Retro-validation.** R-NUCA interleaves shared read-write data and replicates only
instructions, and the component former **refused all 818 co-access components as larger
than a tile's fair share. It was right, and this experiment is the measurement of what
doing otherwise costs.**

**The related compile-time direction the user did endorse** (#296,
2026-09-02T14:01:00Z): "We found before that **the type of separability required is
difficult to manufacture for graph workloads**, but I think what you are getting at is
**smarter placement/replication when compiling to reduce migrations at minimal memory
overhead.**" — i.e. the *type* and the *vtile*, at minimal overhead. Never bucketing by
tile number.

---

## PART M — THE TWO HOSTS, AND THE PARITY CAVEAT

### M.1 What a host is, architecturally

A host core is **an ordinary high-performance out-of-order core**, plus exactly one new
structure: **the function tracking unit**, and the fork/join instructions that use it.

*User #4, 2026-08-27T05:52:15Z:* "**The standard core should be copied and extended into
a standard core with support for NMFCs.** Copy the existing core, potentially create a
new interface if new invocations and connections are needed, and extend the
implementation to implement the tracking."
*User #2, 2026-08-27T05:32:02Z:* the dispatch unit is "**just a unit parallel to the LSQ
and functional units/execution queues**", and — critically — "**the standard memory path
still exists (loads/stores operate like normal, are dispatched to memory tiles, check
LLC, go to DRAM, no interaction with NMFC core).**"

**The host is not attached to any particular function core.** *User #236,
2026-09-01T06:01:18Z:* "**We don't want to attach any nmfc processor to a particular
standard core. That is part of the point.**" A host reaches every tile over the one
fabric.

**Why fork/join rather than a register-only invocation.** The user considered making an
invocation a single ROB-scheduled instruction with register dependencies and concluded
against it (#19, 2026-08-27T17:34:22Z): "**fork/join detaches the source/dest from each
other** (so we can start something, do unrelated work, and retrieve the return value when
necessary, compared to a single instruction requiring us to have a place to use the
returning value immediately, which actually means **the regfile-based system has another
downside: both the input setup and output capture must be done alongside each nmfc
invocation, which the regfile is going to end up serializing** in all likelihood, **so
maybe fork/join is better overall**)."
And when the memory-return loop was later blamed for a problem fork/join did not cause,
the user restored it (#135, 2026-08-29T05:04:43Z): "**the join/fork isn't even wrong by
construction then.** The whole reason we pursued the memory return was that you claimed
the fork/join was the problem -> it doesn't seem to have been. **I am not saying drop
either, they both can be useful in their own ways.**"

**Register dependencies cause false serialisation, and that is a workload bug, not a
hardware one.** *User #121, 2026-08-29T02:31:33Z:* "**You understand register
dependencies are a thing right? So if that return is depended upon, the join comes
immediately, and it WILL block.**"

### M.2 Two hosts, one instruction set

| host | model | how NMFC attaches |
|---|---|---|
| **Rev** | in-order RISC-V | co-processor subcomponent carrying the FTU and the fork/join decode |
| **Vanadis** | **out-of-order** — ROB, rename, LSQ | **RoCC accelerator**, `custom-0` opcode, RoCC-conformant `funct3` operand flags |

**Vanadis OoO is not optional.** *User #288, 2026-09-02T03:50:47Z:* "**do the Vanadis
implementation first. Before any baselining, I want the full system implemented and
tested with a meaningful workload, then comes the actual measurements. Vanadis OOO is
imperative for weighing standard compute against the NMFC cores.** In addition, **cache
sizing and fabric bandwidth should be of the same magnitude as modern processors.**"
*And #285, 2026-09-02T02:54:13Z:* "I would also appreciate each rev core being beefed up
to something approaching the sizing of a modern high performance ooo core."

**One binary must decode on both**, which is why the ISA is RoCC-conformant (I.9). The
encoding change was made **before any result was taken from the ISA**, deliberately.

### M.3 The parity caveat — carry it with every number

**There are TWO caveats on any function-core number, and this section states the first.
The second — the absent branch-honesty sweep — is O.4 item 2, and it is not optional
either.** `[ADDED — this section was the target A.7's mandatory caution pointed at, and it
carried only the core-model caveat, so a reader who followed the pointer still got one of
two. Appendix 3 item 8: "a function-core speedup carries TWO caveats, not one: which core
model (M.3) and the absent branch-honesty sweep (O.4)."]`

**Caveat 2 in one paragraph, so this section is self-sufficient:** the function core
**replays resolved control flow, so it never mispredicts** (DESIGN §12 D:1016).
`FLAG_TAKEN_TARGET` plus a configurable fetch bubble is the honesty knob; ChampSim ships
it (`inc/nmfc/nmfc_trace.h:166-172`, `fetch_bubble: 1` in every config); **the sensitivity
run DESIGN asks for is not in the record.** Full statement and quotation at O.4 item 2.
**Both caveats travel together — "which core produced the number" means both of them.**

**Caveat 1: the 5.67× was measured on ChampSim's core model.** ChampSim's function-core contexts
**keep issuing past an outstanding load**: a load marks its destination register
not-ready, the PC keeps moving, and the context stops only when an instruction actually
needs a value that has not come back. That in-order scoreboard is where
**intra-function memory-level parallelism** came from in the measured runs — an
invocation with two independent loads issues both and gets MLP 2 with no reordering
hardware.

**The canon core sleeps on one outstanding load (H.4).** All MLP then comes from
**context count**, so the latency term is paid in full per miss rather than amortised.

**Therefore: §21.3's 5.67× is NOT a number Rev should be expected to reproduce at the
same context count, and any comparison between the two must say which core model
produced it.** This is not a discrepancy to be reconciled — **ChampSim and the canon
core genuinely differ here, by design.**

Two further rules that follow:
- *User #242, 2026-09-01T06:35:24Z:* "**I stopped you because you were sounding like you
  were taking truths about ChampSim's sim and asserting them as truths in rev's sim.**"
  Do not carry a ChampSim simulator fact into the other simulator as a fact.
- **No SST number may be compared to a ChampSim number until the baseline reproduction
  step is done** — stock GAPBS BFS, no NMFC, against ChampSim's 197,753,293-cycle stock
  baseline. That step was deliberately deferred, and it is a **binding precondition** on
  any cross-simulator claim.

### M.4 And the counter-rule: do not lie about what ChampSim modelled

*User #238, 2026-09-01T06:19:05Z, point 4:* "**Stop with your blatant lies about what was
modeled in ChampSim. Lets be clear: migration WAS MODELLED. fetch WAS MODELLED.**"

ChampSim models: the multi-context function core with per-context translation and
migration; the three-class fabric with per-destination queues and an age guarantee;
instruction fetch through a real I-cache channel; the dual-page-size MMU with a real
walker; the placement-aware allocator with grain groups, replica sets and remap; the
LLC-slice-per-tile hierarchy; ramulator2 per tile with derived geometry; the host core
fork/join tracking unit. **What it does not model is listed in Appendix 2's ChampSim
column, and "not modelled" is never to be asserted without checking the code
(#291: "Please do not rely on memories or doc, read the code.").**

**AND THE THREE THINGS IT DOES NOT MODEL ARE NOW DECLARED PROPERTIES, NOT DEFECTS.**
`[user ruling 2026-09-02 R14 and R4.]` R14: ChampSim "**doesn't model coherence, data, or
atomics**" — which is why the memory-committing loop needs trace markers there and no
instruction on the machine (I.11). R4: "**ChampSim doesn't have a byte model, just a
cycles-to-transmit model. No need to back port it, SST is correct**" — so its migration
costs are message and latency costs, and every byte-parity or subsumption claim comes from
SST (J.2). **Neither is going to be repaired: ChampSim is frozen (R3, Appendix 2 D0).**
State what each simulator models when quoting it, in both directions — that is the whole of
M.4's rule and it now cuts both ways.

---

## PART N — WORKLOADS AND THE SETTLED MEASUREMENTS

> **[RULED — HISTORICAL OBSERVATIONS. user ruling 2026-09-03 **O15**: "**a.**", selecting
> option (a) of that row.] EVERY MEASUREMENT IN THIS PART IS A HISTORICAL OBSERVATION OF AN
> EARLIER TREE.** Its configuration is **unreproducible from git** — no measurement here
> names the configuration file that produced it, the 4 MiB LLC slice several of them were
> taken at was never committed (L28c, lookup run), and the checked-in configurations cannot
> run against the checked-in workload (L20, E.3). **ChampSim stays frozen** (user ruling
> 2026-09-02 R3), so re-taking them is not an available action and was not asked for.
> **Quote every number here as a recorded observation whose configuration is unknown, never
> as evidence about the machine and never as something a reader can re-run. Do not tune a
> parameter to reproduce one.** The standing statement of all four facts is **N.0**.

**N.0 immediately below is the full statement of the four facts behind that banner**, and
it is where the other three Parts point. **The one exception in all four Parts is the
stress workload** (L32, G.6), which was reshaped, re-run and verified against the host and
whose build and binary ARE recorded.

### N.0 PROVENANCE WARNING — NO MEASUREMENT IN THIS DOCUMENT NAMES THE CONFIGURATION FILE THAT PRODUCED IT, AND THE CHECKED-IN CONFIGURATIONS CANNOT RUN AGAINST THE CHECKED-IN WORKLOAD

**[ADDED — BLOCKING FOR REPRODUCTION, AND IT IS ASSEMBLED ENTIRELY FROM FINDINGS THIS
DOCUMENT ALREADY CARRIES, WHICH IS WHY IT HAD TO BE STATED IN ONE PLACE. Parts G, K, L and
N quote dozens of measurements "from this tree" and **not one of them names the
configuration file it came from**, while the ledger separately establishes that the tree's
configurations and the tree's workload cannot together produce a run at all. Neither
finding is new; putting them next to each other is.]**

**Read this before quoting any number in Parts G, K, L or N.** Four facts, each already
ruled elsewhere in this document:

1. **L20 — the 31 `--dram default` configs are UNRUNNABLE against a default-built trace.**
   L20's ruling, verbatim: "**The 31 `--dram default` configs are therefore not 'silently
   wrong'; they are UNRUNNABLE against a default-built trace.**" Thirty-one of the 33
   configs declare `nmfc_grain_bits: 21`; only the two under `ram/` declare **20**, which
   is the value the default controller and ramulator DDR5 both derive; ramulator HBM3
   derives 18 (E.3).
2. **The only escape does not exist either.** The override is `annotate --grain-bits 21`,
   and **E.3 establishes that it "produces a binary containing two different grain sizes
   ... the checked-in workload cannot honour the override that the checked-in
   configurations require."**
3. **L28(c) — Part L's and N.1's runs used a 4 MiB LLC slice that was never committed.**
   Every checked-in config is 512 KiB per slice; no file in the tree passes the
   `--llc-sets 16384` a 4 MiB slice at four tiles needs.
4. **L34 — three baseline migration counts cannot be re-taken from any committed file.**

> **THE CONSEQUENCE, STATED PLAINLY: a fresh implementer cannot reproduce a single number
> in this document from the tree as committed.** That is a stronger statement than any one
> of the four rows makes, and it is the one an implementer needs.

**What this does NOT mean.** It does not mean the numbers are wrong — they were measured,
by this project, on configurations that existed at the time. It means **their provenance
is unrecorded**, and this document's own rule 4 ("*when quoting a number, quote its
provenance*") cannot currently be obeyed for any of them.

**THE RULE, AND IT IS NO LONGER PROVISIONAL — IT IS RULED.** `[RULED — user ruling
2026-09-03 **O15**: "**a.**", selecting option (a): confirm that Parts G, K, L and N are
historical observations of an earlier tree, quoted as such and never as evidence about the
machine. The alternative on offer — lifting the R3 freeze for this one purpose — was **not**
taken. **ChampSim stays frozen.**]`

**Every measurement quoted from Parts G, K, L and N is quoted as a RECORDED OBSERVATION
WHOSE CONFIGURATION IS UNKNOWN — never as something a reader can re-run, and never as
evidence about the machine. Do not tune a parameter to reproduce one.** Each of those four
Parts now opens with a banner saying so, so the rule cannot be missed by a reader who
enters the document at a Part rather than at the front.

**What "historical" does and does not mean, because the distinction is the whole ruling:**

| | |
|---|---|
| **it does NOT mean the numbers are wrong** | they were measured, by this project, on configurations that existed at the time. Nothing here says a figure is false. |
| **it DOES mean their provenance is unrecoverable** | this document's own rule 4 (*quote a number with its provenance*) **cannot be obeyed for any of them**, and under R3 it never will be. |
| **it DOES mean they are not evidence** | a claim about the machine may not rest on one. Where a Part's *rule* is tier 1 — L's "replication is an option", K's "a unit of work must own its data" — **the rule stands on the user's words and the historical number is only how it was reached.** |
| **the ONE exception** | **the stress workload** (L32, G.6): reshaped, re-run, verified against the host, **and its build and binary ARE recorded.** It is quotable normally. |

**And "recover and commit the configurations" is not on the table and never was**, for two
independent reasons already established: ChampSim is frozen (R3), and the 4 MiB slice was
**never committed in the first place** (L28c, lookup run). **O15 is closed.**

### N.1 What the workloads are, and how they must be built

**Real GAPBS, compiled with the hooks inside it.** *User #16, 2026-08-27T16:44:29Z:* "I
think GAPs is open source right? So it would probably be easier to **download gaps,
compile it with the pseudo-compiler hooks inside it, and use that trace instead of trying
to modify existing traces.** In addition, **we should consider, for each graph algorithm,
what the ideal slicing of work actually is.**"

[NOTE — the SAME construction was used before GAPBS, and DESIGN §27 D:3008-3021 records
it: `test/tile_bfs.c`, a CSR BFS with skewed degree, `parent[] claimed by AMOSWAP and the
frontier appended through an AMOADD counter. **One source builds two programs** —
`host_bfs.exe` runs the reference and nothing else, `tile_bfs.exe` runs the same traversal
offloaded and then runs the reference afterwards and compares — "*deliberate: invariant 8
says never to compare the architecture against a weaker algorithm, and the cheapest way to
honour it is to make the baseline the same source*". **It agrees with the reference at
one, two and four tiles.** The rule below is therefore not new with GAPBS; it is the same
rule applied to a real benchmark.]`

Two binaries from one source, so the with/without comparison runs the same graph and the
same source vertex through both:
- **`bfs_base`** — GAPBS's reference direction-optimising BFS, unaltered, on a machine
  with no function cores. **This is the thing an NMFC result has to beat.**
- **`bfs_nmfc`** — the same algorithm with its inner work lifted into offloadable
  functions. **The only difference is where the work runs.**

**The fairness properties are checked, not asserted:** both sides traverse the same graph
from the same source and **reach the same vertex count, checked at three scales**; both
run the **same algorithm** with the same alpha/beta direction-switch thresholds; and the
offloaded kernel executes **fewer** instructions than the reference, because ownership
removes the reference's atomic bit-set and its compare-and-swap. **The machine is not
being handed extra work to look good on, nor spared any.**

**A stress workload, built to saturate the machine — AND IT WORKS.** `[RULED — user ruling
2026-09-02 on L32. #303's "*Is the stress test working or not? **It seems like a failure.**
… There is clearly a massive load-balancing problem*" was the **trigger for a reshape**, not
a ruling that the machine is broken.]`

**The first shape was the REJECTED one** — own an index range and chase data from it, i.e.
K.3's chase decomposition — and its 2.5:1 tile spread was **predicted exactly from the
addresses**: 12.5 / 37.5 / 37.5 / 12.5 across four tiles is what congruent routing does with
that layout, and it is what the run measured. **The routing was right and the workload was
shaped wrong.**

**Reshaped**: the host resolves seven indices into the 512-bit context, the function does
seven data loads and returns the sum via **END with the return bit**, and the
`FORK.R`/`JOIN` ring is the size of the FTU. **It measures loads, migrations and
instructions at 25.0% on every tile, zero stores, 196,904 migrations for 262,143 loads, and
the sum verified against the host.** **Every measurement taken from this workload — the H.9
occupancy table, N.4's context sweeps, the 11.3048 ms and 136.143 ms invariance — is
SETTLED**, quoted with its build and binary (working tree at HEAD `21df518d`,
`tools/nmfc/kernels/bfs_nmfc` and `bfs_base`, uncommitted). See G.6 and ledger **L32**.

**WHAT THE STRESS WORKLOAD IS, from the section that constructs it.** `[OMISSION
CORRECTED — DESIGN §31.1 D:3572-3592 is the specification of this workload and was never
cited, while L32's open question is precisely "is it working".]` `tile_stress.c` is an
**indexed gather — `acc += data[idx[i]]`** — over an access set larger than the aggregate
LLC: **16 MiB against four 4 MiB slices, random indices, two dependent loads per element,
no reuse.** The second load cannot issue until the first returns, "*so a context sleeps
twice per element and nothing the core does can hide it*". **4,096 invocations of 64
elements each, 524,288 loads.** It is **a migration generator by construction** — random
indices into a 16 MiB array put three quarters of every access on another tile — and the
machine moves the work rather than the data **396,161 times in 11.3 ms**.

**And its first version did not run, which is the detail L32's diagnosis needs first.**
§31.1 D:3586-3592: it forked with `FORK.R` and **joined immediately**. A `JOIN` on an
invocation that has not returned answers 0 and retires nothing — I.1's try rule — "*so the
tracking unit filled, every later fork was refused, and the retry loop span eight million
times per invocation. **The comment above the loop said 'fire and forget' while the code
did the opposite.**" It is `nmfc_forkf_r()` now, with no join. **Before re-deriving
anything about this workload, establish which build produced the run in question** — and
note §31.2 D:3607-3612's own warning that `make` rebuilds `tile_stress.exe` at whatever
`STRESS_SIZE` defaults to, so **"the stress workload" names two different workloads**;
the binary reports its own size in its banner and it has to be read. [CAUTION — the
"four 4 MiB slices" above is the workload's own sizing statement and is the SAME 4 MiB
slice **that was NEVER COMMITTED** — the `git log --all -S'"num_sets": 4096' -- config/nmfc`
lookup was run for this revision and returns nothing (L28c). The slice size itself is
**configuration; see SELECTED CONFIGURATION**, where #76's rule ("*modern LLC size / DRAM
channel*") is recorded as the constraint the value must satisfy.]

*User #298, 2026-09-02T17:44:03Z, who asked for it:*
"Have you tried on **a medium-sized synthetic workload capable of stressing the machine
(large access set, maxed-out contexts)**?" Built as an indexed gather over an access set
larger than the aggregate LLC: 16 MiB of data against four 4 MiB slices, random indices,
**two dependent loads per element and no reuse** — the second load cannot issue until the
first returns, so a context sleeps twice per element and nothing the core does can hide
it. **It is a migration generator by construction, which is the point**: random indices
into a 16 MiB array mean three quarters of every access is on another tile.

**THE SCALE GATE — a workload must exceed `grain × tiles` before a tile-behaviour result
means anything.** `[OMISSION CORRECTED — DESIGN §20.1 D:1683-1689 was absent, and this is
the section on how workloads must be BUILT, so it is the one place it must appear.]`
DESIGN, verbatim:

> "Note also that **scale matters for any tile measurement**. At scale 17 the `parent`
> array is 0.5 MiB and `index` 1.00 MiB — at or below the 1 MiB grain — **so the hot
> randomly-accessed structure lands entirely on one tile and no amount of batching or
> placement can balance it. A working set must exceed `grain × tiles` before a
> tile-behaviour result means anything.**"

**At the `G` = 1 MiB and four tiles of the measured configuration, that threshold is 4 MiB,
per hot structure, not in aggregate** — and it moves with the device, because `G` is not a
constant (E.3, the geometry ruling; **configuration, see SELECTED CONFIGURATION**).** A structure smaller than one grain is *one grain* and lives on *one
tile* by construction (E.1); a structure smaller than `G × N` cannot cover every tile at
all. **This admits or rejects every tile measurement in the document**, and it is the
first thing to check against #303's load-balancing complaint (G.6) before any policy is
suspected — an imbalance below this threshold is the mapping working, not a defect.
Compare C.3's `STANDARD` row, which records the same arithmetic from the other side: a
1 MiB frontier under NMFC mode is one grain, lands on one tile, and every invocation
migrates there to test a bit.

**Comparability is mandatory.** *User #182, 2026-08-29T19:43:51Z:* "**Whatever you do, it
needs to be directly comparable to the base workload for comparison with/without the nmfc
cores.**" *And #183:* the base workload means "**the original amount of work (say, the
full traversal of N nodes) using the base algorithm with non-NMFC cores vs. the altered
algorithm doing the same amount of work (N node traversal) with the nmfc cores.**"

**Report from every core's perspective.** *User #18, 2026-08-27T17:22:20Z:* "**If we are
reporting IPC from the standard core's perspective, we must do it from each NMFC's
perspective as well.**" Reporting only the host's IPC describes a machine that is
deliberately doing its work elsewhere.
*And #123, 2026-08-29T03:01:31Z:* "**Please actual report total cycles to do the same
work.**"
*And #120, 2026-08-29T02:28:26Z:* plot bandwidth saturation per kernel, and **how many
kernels are in flight at once under each design.**

### N.2 THE HEADLINE RESULT — 5.67×, ON THE CHAMPSIM CORE MODEL

Scale-20 Kronecker graph, **645,268 vertices reached**, four memory tiles, ChampSim core
model:
| same 645,268-vertex traversal | instructions | cycles |
|---|---:|---:|
| reference DOBFS, no NMFC cores | 62,869,010 | 197,753,293 |
| same algorithm offloaded, no NMFC cores | 55,684,830 | 213,820,177 |
| **same algorithm offloaded, WITH NMFC cores** | 19,681,418 | **34,905,677** |

`[DISAMBIGUATED — the three ratios are each computed from a DIFFERENT pair of rows and an
earlier revision printed them unlabelled, which is how "0.89× the instructions" came to be
attached to the machine. Row numbers are added; the arithmetic is unchanged.]`

```
row 1 = reference DOBFS, no NMFC cores
row 2 = same algorithm offloaded, no NMFC cores
row 3 = same algorithm offloaded, WITH NMFC cores

architecture gain, identical work, cores on/off : 6.13x   = row 2 cycles / row 3 cycles
NET vs the reference algorithm                  : 5.67x   = row 1 cycles / row 3 cycles
work vs reference, in instructions              : 0.89x   = row 2 instr  / row 1 instr
```

**Read the third one carefully, because it is the one that gets misquoted.** `0.89×` is
**row 2 over row 1** — the instruction count of the *offloaded program with the cores
switched off*, against the reference. **It is a property of the algorithm, not of the
machine.** The machine's own row is row 3, and **row 3 / row 1 = 19,681,418 / 62,869,010 =
0.31×**, not 0.89×. `[CORRECTED — A.7 and invariant 8 both read "the machine is 5.67×
faster … while executing 0.89× the instructions", which pairs a row-3 cycle ratio with a
row-2 instruction ratio in one sentence. Both are fixed in this revision.]`

**[THE ACCOUNTING BEHIND ROW 3 — ANSWERED BY LOOKUP, NOT BY A RULING. It was open item
O11; the answer is HOST INSTRUCTIONS ONLY.]** This was never a design question — it is a
property of code that already ran, so it was read out of the code rather than asked of the
user, in the class R5 established for "*that sounds like a bug*". The per-core instruction
figure ChampSim reports is `NMFC_HOST_CORE::sim_instr()`, defined as
`num_retired - begin_phase_instr` at `inc/nmfc/nmfc_host_core.h:603`, and `num_retired` is
incremented **only** at ROB retirement, `src/nmfc/nmfc_host_core.cc:1032`. **The function
core keeps its own counter and never adds it in**: `instructions_` is incremented at
`src/nmfc/function_core.cc:698` and `:1040`, printed on its own `INSTRUCTIONS:` line at
`:293` and exported as its own `"instructions"` JSON field at `:328`. **Therefore row 3 is
host-only, and 0.31× is the HOST's share of the reference's instruction count — it says
nothing directly about total retired work.** A work-per-instruction, IPC or energy figure
derived from row 3 is a **host-side** figure and must be labelled one; a total-work figure
requires adding the per-tile `instructions` fields, which no measurement in this document
has done.

`[CAUTION — the parity caveat, stated inline rather than by reference.]`

**That figure was measured on the ChampSim core model, whose contexts kept issuing
past an outstanding load; the canon core sleeps on one (H.4).** The two have different
memory-level parallelism at the same context count, so **it is not a number Rev should
be expected to hit, and no quotation of it may omit which core produced it** (M.3;
invariant 8's `[CARRIED]` caveat; DESIGN §0.8 D:56-60, §25.4 D:2447-2453).

**What this supersedes, stated by the design itself:** the earlier bandwidth-utilisation
figure (77.9% of peak against a host's 20.2%) "was the right measurement for the premise
but **is not a speedup**" — **note that it remains the standing validation of the
PREMISE; it is superseded only as a claim of speed. See N.7.** And every intermediate
number in the sections before it
"compared NMFC configurations against **other NMFC configurations**". Those isolated real
defects, **but none of them established that the machine beats not having it. This
does.**

**Carry the parity caveat with the number, always (M.3).**

### N.3 THE §29 INVERSION — replication cut migrations 4.96× and was 38.2% SLOWER

Full table and mechanism in Part L. The one-line statement, which is the most useful
single sentence in the measurement record:

> **The migrations were not the limitation, and cutting them cost time.**

And the general rule it produced: **a migration count is evidence about placement, not a
quantity to minimise.** Every earlier alarm about migration rates was reading the wrong
number.

### N.4 THE OCCUPANCY RESULT — what a full FTU does and does not prove

`[ONE reason to hold this result carefully, and it is NOT the workload's status any more.]`
**The stress workload is SETTLED** — reshaped, verified against the host, and measuring
**25.0% on every tile** with **196,904 migrations for 262,143 loads** and zero stores (user
ruling 2026-09-02 on L32; full account in G.6). **The caveat that used to sit here, that
the run "seems like a failure", is withdrawn.** What still stands is **#180**
(2026-08-29T19:32:15Z), which refuses the inference this section once drew from a full FTU:
"**Despite the FTU being full, most of it is still pending results. A larger FTU doesn't
fix that problem.**" Full discussion in **H.9**; the section heading reflects it.

**The section's own thesis, which is stronger than "the sweep was flat".** DESIGN §31
D:3566-3570: §20 named "**contexts, and the shape of a function**" as the two levers left.
"**The first of those is not reachable from this host, and it took a workload built to
saturate the machine to show it.**" *(That is the finding: not that more contexts did not
help on this run, but that **contexts are not a lever this host can pull at all** — which
is why N.5's two-axis rule puts the remaining work on axis 2, the mapping. §31.1's
construction and the fork/join bug in its first version are in N.1.)*

Full table in H.9. The findings, as measured:
- Sweeping contexts per tile **64 / 128 / 256 / 512** gave **identical** results to seven
  figures — 11.3048 ms at every point — and again at 4× the access set, **136.143 ms at
  64 and at 512**. "**A machine whose answer does not change to seven figures when a
  resource is multiplied by eight is not using that resource.**" **Settled, with its build
  and binary noted** (working tree at HEAD `21df518d`, `tools/nmfc/kernels/bfs_nmfc` and
  `bfs_base`, uncommitted — G.6).
- Direct instrument: **the host tracking unit was 63.61 of 64, full 98.5% of the time it
  was in use, while the tiles were 4–10% full** and the fabric control queue was empty
  with zero refusals. **That separates what the sweep could not**: whatever is binding,
  it is not the fabric queue.
- **What a full FTU does NOT establish.** Per #180, entries can be full of *completed*
  results waiting for a `JOIN`, in which case the constraint is the host's consumption
  rate and the shape of the fork/join code — **and a larger FTU makes it worse, not
  better.** Per #171, if the FTU were the cap, the tiles would be piled with contexts;
  they were not. **The measurement to take is the split: OUTSTANDING vs
  RETURNED-AND-UNJOINED.** Until that is instrumented (O.1's own rule), the table above
  shows *where the queue is*, not *what the machine is waiting on*.
- **And it HAS been taken once, and it answered.** DESIGN §20.1 D:1591-1597 reports
  `FTU IN FLIGHT peak: 1024 of 1024` **with `DISPATCH STALLS: 0`** — it "*never once gated
  a fork*" — **and peak live bodies 665, not 1024.** "*Occupancy of a structure that holds
  results is not a measure of running work, and raising it does nothing.*" That is #180's
  reading confirmed by instrument. **Full account in H.9a.**
- **A structural reason to discount the fullness independently:** the headline config sizes
  the FTU at **1024 against 4096 context slots** (D.5, L27) — a 4:1 cap that
  `make_config.py:566-570` warns produces occupancy numbers describing "*the unit rather
  than the architecture*". **Both the tier-1 reading and the config arithmetic say the
  same thing: do not conclude "the FTU binds" from a full FTU.**
- **The tile column of that same table — 5.37 / 13.27 / 12.57 / 5.61 — is the run's
  REJECTED FIRST SHAPE**, a chase decomposition whose 2.5× spread was **predicted exactly
  from its addresses** (12.5 / 37.5 / 37.5 / 12.5). **Congruent routing was doing its job
  on a workload shaped wrong.** The reshaped workload is even to 25.0% on every tile
  (G.6), and the FTU-vs-tiles reading above holds under both shapes, which is why it
  survives the reshape rather than depending on it.
- On BFS the FTU is **never full** (23.20 of 64) and the limit is the workload's own
  level-synchronous dependency structure. **Two workloads, two different binding
  constraints, and the run time alone distinguishes neither.**

### N.5 The two-axis rule — never optimise both at once

*User #133, 2026-08-29T04:36:34Z, verbatim:* "there are two axis to this problem, and we
need to be careful to not try to optimize both at once. **1. The hardware itself, and the
performance achievable under ideal conditions. 2. The compiler, or the ability for
different workloads to utilize the hardware efficiently.** When optimizing hardware, we
need a series of workloads that exercise the full band of behaviors, and **we should not
bottleneck ourselves by our current ability to map existing algorithms to the hardware
using our pseudo-compiler.** 1 kind of requires 2 so we know where the design space
itself lies, but we need to be certain that whatever workloads we use to optimize the
hardware are actually using the hardware optimally. **We don't want to optimize around a
program that misuses the hardware.**"

**The result that makes this concrete:**

| kernel | architecture gain | algorithm penalty | net |
|---|---:|---:|---:|
| top-down only | 6.73× | 7.38× | **0.91× — a LOSS** |
| direction-optimising | 6.13× | 1.08× | **5.67×** (ChampSim core model — M.3) |

**WHAT THESE THREE COLUMNS ARE RATIOS OF — stated because the document ruled four times
(A.7, I8, here, R99) that 7.38× and 5.3× "measure different things" and never once said
what either measures.** [MODEL-AUTHORED DEFINITION, flagged as such, and it is a
RECONCILIATION of numbers already in this document, not a quotation. No tier-1, tier-2 or
tier-3 source defines the column heading "algorithm penalty". What IS sourced is every
input: N.2's three measured rows and the identity the table itself asserts.]

Using N.2's row numbering — row 1 = the reference algorithm with no NMFC cores; row 2 =
the same program decomposed for offload, cores **OFF**; row 3 = that program with the
cores **ON** — **all three columns are CYCLE ratios, and they are the same three cycle
ratios N.2 already prints:**

> - **architecture gain** = row 2 cycles ÷ row 3 cycles. *What the machine buys on
>   identical work.*
> - **net** = row 1 cycles ÷ row 3 cycles. *What the user gets, against the reference.*
> - **algorithm penalty** = row 2 cycles ÷ row 1 cycles. *What the decomposition costs
>   before the machine helps anything.*
>
> **The identity that closes the arithmetic is `net = architecture gain ÷ algorithm
> penalty`**, which is why 6.73 ÷ 7.38 = 0.91 and 6.13 ÷ 1.08 = 5.67. It closes because
> the row-3 term cancels.

**Checked against the direction-optimising row, which is the one whose three rows this
document tabulates:** row 2 ÷ row 1 = 213,820,177 ÷ 197,753,293 = **1.081** — the table's
1.08×. The top-down kernel's own three-row table is **not in this document**, so its 7.38×
is verifiable only through the identity, not independently.

**AND THE 5.3× IS NOT THIS QUANTITY, WHICH IS THE WHOLE POINT OF THE RULE.** 7.38× is a
ratio of **cycles**; the 5.3× carried from the earlier §21.3 write-up is described
everywhere in this document as "the raw extra work the top-down kernel handed the
machine", and **no tier-1..3 source states the UNIT it counts** — instructions, edges
examined, or vertex visits. It therefore cannot be placed on the cycle axis and cannot
close the arithmetic. **Under Appendix 3 item 8 — a number whose unit and configuration
cannot be named may not be quoted — 5.3× is therefore RETIRED, and that is what closed open
item O14**: the only two options were "state the unit" and "retire it", and no source
contains the unit to state, so the second is the only one that survives. **Quote 7.38× for
the loss; do not quote 5.3× as a quantity at all** — name it, when it must be named, as
"the retired raw extra-work count from the earlier §21.3 write-up, unit never stated at any
tier".

**THE CONFLATION THIS IS WRITTEN TO STOP.** A.7 and I8 also quote **0.89×** — an
*instruction* ratio, row 2 instructions ÷ row 1 instructions. Its reciprocal is **1.12**,
which sits near the 1.08 algorithm penalty and is a different ratio of different
quantities on a different axis. **1.08 is cycles; 0.89 is instructions; they are not each
other's inverse and neither is derivable from the other.** That is exactly the pairing
N.2's `[DISAMBIGUATED]` block was written to stop.

**The architecture delivered ~6× in both cases.** The first kernel handed it far more
work than the reference needed and gave the whole gain back. **The multiplier that closes
the arithmetic is the 7.38× in the table (6.73 / 7.38 = 0.91); the 5.3× that appears in
§21.3 and in older revisions of A.7 and I8 is the retired raw extra-work count and closes
nothing on its own.** Use 7.38× when explaining the loss, and say which you mean. **The hardware axis was
never the problem, and measuring it against a weaker algorithm would have condemned a
machine that was doing its job.**

**The gate that operationalises it.** Before a run's numbers may be quoted as evidence
*about the hardware*, three questions are asked: what fraction of the work left the host
(threshold ≥ 0.50), how many invocations the tiles held at once (≥ 64), and whether the
host stalled waiting for dispatch. **A run that fails is not a bad result; it is a result
about the mapping, and saying so is the whole point. Most of the conclusions reached
before this gate existed — that migration was ruinous, that the placement policy was
inert — came from runs that would have failed it**, offloading 7–11% of their work with
4 of 4000 contexts resident and 1% channel occupancy.

### N.6 Other measurements worth not re-deriving

**PROVENANCE RULE, and it is ledger L10's own rule applied here.** "Neither is wrong;
**quoting either without its provenance is.**" **The 77.9% and the 6.19× were both
produced by the SPAWN decomposition, which this document REJECTS (K.4, I10, R24).** DESIGN
§0's table (D:161-162) gives "NMFC, chase decomposition ... 73.8%" against "NMFC, spawn
decomposition ... **77.9%**"; DESIGN §14.0's table (D:1137) attributes the 6.19× cycle
speedup and the 2,890 migrations at 0.0015/instr to the **spawn** row. **Quote them with
the shape named, exactly as this document already does for 0.0015 in K.4.**

| result | value | note |
|---|---|---|
| bandwidth: host vs NMFC | **20.2%** of DRAM peak vs **77.9%** | GAP BFS kron-24, ramulator2, DDR5-4800, 4 channels, 19.2 GB/s each = 76.8 GB/s. **SPAWN decomposition** (chase gives 73.8%). `channel_width: 32` is bits, so the denominator is 19.2 GB/s per channel — E.3. |
| the same against ChampSim's own DRAM model | 13.7% vs 76.9%, cycle speedup **6.19×** | baseline OoO ROB 352. **SPAWN decomposition** — the same row that produced 2,890 migrations at 0.0015/instr |
| migration cold start | **2.2–2.3 cycles**, 100.00% fc I-cache hit | four tiles |
| migration legitimacy ceiling | **0.76 migrations per memory op** (396,161 / 524,288) | **the COUNT clause passes.** #291's other clause — no fetch- or translation-induced migration — is **unmeasured**: the assertion is not installed under `--walk-routing fabric` (**15** configs — F.6's census) and the run's mode is unrecorded. J.4, L26, L37 |
| fc D-cache hit rate | **11%**, no replacement policy pushed it past **13.9%** | the measurement behind the "is it worth it" question. **Source, previously absent at every tier in this document: `config/nmfc/make_config.py:319-325`**, the comment on the no-D-cache branch — "*the modelled cache hit 11% — no replacement policy pushed it past 13.9%, so there is no working set between the register file and the slice for it to hold*". **Tier 2 only: `grep -c '13\.9' DESIGN.md` = 0, and the session log never states it.** It is the number D.6's SHiP ruling turns on |
| capacity: 8 MB at L1 vs 8 MB in the slice | within **1.5%** (1,770,470 vs 1,744,780); slice falls to **0.2%** hit | capacity belongs in the slice |
| chase decomposition | **0.7428 migrations/instruction on GAP BFS**; **0.384 on the synthetic scattered set**; 1.43 accesses between tile switches | the rejected shape. **Two rows, two workloads — 0.7428 is the "three quarters of all work" figure, 0.384 is not.** K.3, L34 |
| congruence bug | **75.3%** of accesses routed to a tile their address never named; **27%** of run time | the assertion had never executed once |
| min-cut partitioning | 16% fewer migrations and **2.3× slower**, two tiles at occupancy 1 | concentrates and loses |
| the ring vs the batch barrier | the barrier left every DRAM channel **~64% idle**, occupancy sawtoothing, and the idle fraction did not care whether a tile held 17 or 45 contexts | set by batch cadence, not capacity |
| **the ring fix, measured** | barrier → ring: queue-empty **51.7% → 38.4%**, aggregate **31,149 → 39,063 MB/s**, cycles **60,119,717 → 49,276,407 (−18.0%)** on 1% *fewer* instructions | **migrations 31,217,817 vs 31,245,056 — UNCHANGED, and that is the control.** Batching never caused them, so a batching fix must not move them. H.9b |
| **FTU fullness is not FTU binding, measured directly** | `FTU IN FLIGHT` **1024 of 1024**, `DISPATCH STALLS` **0**, **peak live bodies 665** | the outstanding-vs-returned split H.9/O.1 demand — already run, and the answer is "not the FTU". H.9a |
| deeper ring (4096) | **+9.3% cycles**, queue-empty 38.4% → 43.1% | FIFO retirement makes head-of-line blocking grow with depth |
| bank balance, four tiles | `nmfc_banks_never_accessed` **0**, spread **1.31**, 98.1% row hit on channel 0 | the sweep property, measured |
| store buffer on the host L1 | folds 873,207 stores into 152,944 writes, **5.7× less L1→L2 write traffic**, run time **58.9138 ms either way** | the pessimism was entirely on one link and the write-back L2 absorbed it |
| an early "207× speedup" | **fabricated by unfinished work** — 3,051 of 45,285 invocations had executed when the trace ended | hence the unfinished-work accusation in the statistics |

**A measurement-integrity rule attached to that last row:** a phase that ends with most
of its work still in flight has not measured that work, and every rate derived from it is
wrong **in the flattering direction**.

### N.7 DOES IT DO THE THING? — the premise validation, and the project's one named open question

**This is DESIGN's second §0 (D:145-180) and it was absent from this document in its
entirety.** N.2 supersedes the bandwidth figure *as a speedup*; **it does not retire it as
the validation of the premise, and DESIGN keeps it as exactly that.**

The premise (A.1): a memory-bound, serially-dependent traversal leaves bandwidth idle and
compute idle at once. **The test is whether moving the work to the memory tiles extracts
more of the channel.** Measured, on the same traversal:

| | fraction of DRAM peak extracted |
|---|---:|
| out-of-order host core, **352-entry reorder buffer** | **13.7%** |
| the memory tiles | **76.9%** |

DESIGN D:169-172, verbatim: "An out-of-order core with a 352-entry reorder buffer extracts
13.7% ... The memory tiles extract 76.9% ... **That ratio is the architecture; the 6.19×
cycle speedup is a consequence of it.**"

**And the project's one named open question**, DESIGN D:174-178, verbatim: "**The
remaining 23% is the honest open question**, and §14.0 covers what has been ruled out: it
is **not** migration ... and it is **not** the fabric queues, the cache ports or the
tracking unit, **each of which reported itself the bottleneck and none of which was.**"

**Read that last clause as a standing warning about this machine's instrumentation**:
three structures have each, at some point, reported themselves the constraint and been
wrong. It is the same failure H.9/N.4 record for the FTU. **A structure reporting itself
full is a hypothesis, not a finding** (O.1).

**AND THE OTHER "IS IT READY" SECTION, WHICH IS NEWER AND ASKS A DIFFERENT QUESTION.**
`[OMISSION CORRECTED — DESIGN §30 D:3407-3411 was uncited, though §30.1 and §30.2 both
were, so this document quoted a section's children without its premise.]` §30 was written
**after §29 inverted itself under measurement**, and says why that is the moment to write
it: "*the model is now good enough that **its answers change conclusions**, and that is
exactly when its remaining weak points matter.*" **The two readiness questions are not the
same one**: §0's is "does the machine do the thing" (the 13.7% / 76.9% ratio above);
§30's is "**is the MODEL good enough that its answers can be trusted**" — settled items in
§30.1, unproven ones in §30.2, and two hazards found by writing checks rather than by
hitting them in §30.3. **Quote the right one.**

*(Carry the provenance: these figures come from the spawn decomposition — see N.6.)*

### N.8 WHAT LIMITS BANDWIDTH: ARRIVAL RATE, NOT THE MEMORY SYSTEM

**DESIGN §19 (D:1522-1578) was absent from this document, which carried its METHOD — the
headless replay, O.3 — and none of the settled negative results the method produced.**
Those results are what stop the memory system being re-tuned from scratch.

**Six knobs were changed and none of them mattered** (D:1525-1529, verbatim): "read/write
queue depth (32 → 256), MSHRs (64 → 512), **LLC banking**, refresh (worth +4.7%), the
address mapping (`MOP4CLXOR` was **worse**), and the scheduler (`FRFCFS-RowHit` lifted row
hit rate 71.6% → 83.3% and made throughput **slightly worse**). **Every one of those cost
a ten-minute simulation to disprove.**"

**The diagnosis:** the channel is **starved**, not slow. The read queue was **empty 39.3%
of cycles**, and **enlarging the queue moved latency (391 → 1921 cycles) without moving
throughput** — the signature of a queue that is deep because nothing is draining it, not
because too much is arriving. **Two named false-positive readings** live at D:1571-1577
and are exactly the readings a deep queue invites.

**How to tell the two apart, which is the method's whole purpose (O.3):** inside the
simulation a slow channel and a starved channel produce **identical** symptoms — a deep
queue, high latency, throughput that does not respond to queue size. **Only the headless
replay separates them**: replay the hot tile's own DRAM address stream through ramulator2
alone, through the same device, mapper and scheduler.

`[CORRECTED — an earlier revision stated the discriminator BACKWARDS: "if throughput is
the same, the memory system was never the limit." **That yields the opposite conclusion
from the experiment that produced the rule**, and it disagreed with O.3's own bullet,
which states the method correctly.]`

> **READ THE RULE IN THIS DIRECTION.**
> **Replay ≫ simulation** → the device can go faster than the simulation asked it to →
> **the channel is STARVED. The memory system was never the limit.**
> **Replay ≈ simulation** → the device is already at its ceiling on this stream →
> **the memory system IS the constraint**, and only then is a memory-side knob worth
> touching.

**THE NUMBERS THAT PRODUCED IT — the sweep, DESIGN §19 D:1543-1552.** `[ADDED. This
section previously announced that §19's "settled negative results" had been missing and
then supplied only the six-knob prose list; every quantity was still absent. Verified: the
canon contained **0** occurrences of 17.42, 16.59, 19.07, 16.77 and 10.50.]`
Command: `PYTHONPATH=ext/ramulator2/python tools/nmfc/dram_replay.py <trace> --sweep`.

| variant | GB/s | of 19.2 |
|---|---|---|
| as configured (RQ 256) | **17.42** | **91%** |
| RQ/WQ 32 | 16.59 | 86% |
| no refresh | 19.07 | 99% |
| rank 1 | 16.77 | 87% |
| ***the same stream, in simulation*** | ***10.50*** | ***55%*** |

**17.42 against 10.50 is the whole diagnosis.** The device, handed the *identical*
addresses, delivers 91% of peak; the simulation gets 55% out of the same stream. **The
gap is the starvation.** DESIGN's own summary, D:1553-1555: "**The addresses are fine and
the device is fine** — even at the original 32-entry queue it is worth 16.59 GB/s.
**Seconds per variant instead of ten minutes, and it retires every memory-side hypothesis
at once.**"

**The per-channel figure this settles, and it is the one to quote** (DESIGN §20
D:1581-1582): "*the channels are **idle 39% of the time** and worth **17.42 GB/s each when
fed**.*" **Not 19.2** — 19.2 is the subchannel peak and the denominator; 17.42 is what
this stream is actually worth on this device. A bandwidth claim measured against 19.2 and
one measured against 17.42 are different claims.

**AND THE COUNTER-MEASUREMENT THAT MUST TRAVEL WITH O.1a'S LITTLE'S-LAW RULE** (DESIGN §17
D:1414-1422). It is the only place in the record where the law was applied to a device
queue **and was wrong**, so it is the instance that makes the rule teachable:

> "*A 32-entry read queue appears by Little's law to cap a channel at `32 / read_latency`
> requests per cycle — **about 9.8 GB/s** at the ~500-cycle latency a loaded tile sees,
> against a 19.2 GB/s subchannel — which reads as a clear diagnosis of a half-idle channel.
> **It is not.** Replaying this machine's own captured address stream through the same
> device gives **16.59 GB/s with a 32-entry queue** and 17.42 GB/s with 256. **The queue
> was never the constraint, and the Little's-law argument fails because the latency plugged
> into it is itself a *consequence* of queueing, not a device property.**"*

**9.8 predicted, 16.59 measured.** The failure is not arithmetic; it is that **the latency
term was endogenous.** Before applying Little's law to any queue in this machine, ask
whether the latency you are dividing by is a property of the device or a product of the
occupancy you are trying to explain. See O.1a.

**And the 256/256 queues stay hygiene, not a bandwidth fix** — they remove controller
refusals (11.9M → 0.24M on the hot tile) and buy 17.42 against 16.59, which is 0.83 GB/s,
not a diagnosis (E.3, DESIGN §17 D:1404-1408).

**Two consequences that bind other sections:**
- **`FRFCFS-RowHit` raising row hit rate while lowering throughput** is the sharpest
  available proof that **row hit rate is not a proxy for bandwidth** on this workload.
- **LLC banking appearing in this list does NOT retire D.2's requirement.** §19 measured
  banking as a *bandwidth knob on a starved channel*, where nothing downstream could
  matter. D.2's case is structural — where the binding happens, and whether a monolithic
  512-entry queue is buildable. **Build it; do not expect bandwidth from it here.**

---

## PART O — INSTRUMENTATION OVER ABLATION, AND THE OTHER METHOD RULES

### O.1 The rule

*User #300, 2026-09-02T21:46:23Z, verbatim:* "**are you not printing stats on average
nmfc occupancy? I feel like instrumentation is the best way to actually answer this
question, not blind ablation.**"

**Why ablation is the worse instrument:** a sweep says a resource was not the constraint,
and it says so **the worst way — by elimination, and only for the resource that happened
to be swept.** The direct instrument is **three counters per structure**:
1. **entries live, summed over cycles** (the mean);
2. **the high-water mark** (the peak);
3. **the cycles anything was outstanding at all**, as the denominator.

The third matters specifically here because these programs offload a phase and then
verify the answer on the host, **so an average over the whole run is mostly an average
over an idle machine.**

That instrument answered in **one run** what an eightfold context sweep could not: the
tracking unit was 99% full while the tiles were 4–10% full, and the control queue was
empty with zero refusals — **so whatever was binding, it was not the fabric queue.**

`[CAUTION — and it is this rule turned on itself.]` **Three counters are not enough for
the FTU, and the missing fourth is the whole diagnosis.** An entry can be occupied
because work is **outstanding** (the tiles are the constraint) or because a result has
**returned and not yet been joined** (the host's consumption rate is). *User #180,
2026-08-29T19:32:15Z:* "**Despite the FTU being full, most of it is still pending
results. A larger FTU doesn't fix that problem.**" **Instrument the split, or the
occupancy number says where the queue is and not what the machine is waiting on** — which
is the same error as an ablation, arrived at with a better tool. See H.9 and ledger L33.

`[AND NOTE: THE SPLIT WAS ALREADY TAKEN ONCE, AND IT ANSWERED.]` DESIGN §20.1
(D:1591-1597) reports `FTU IN FLIGHT peak: 1024 of 1024` **with `DISPATCH STALLS: 0` and
peak live bodies 665** — a full unit that **never once gated a fork**, which is the
signature of *returned-and-unjoined*, not of *outstanding*. Those two extra counters
**are** the missing fourth instrument: add **dispatch stalls** and **peak live bodies**
to the three above and the question is answered in one run, without an ablation and
without enlarging anything. Full quotation, and the ChampSim caveat that its own
`DISPATCH BLOCKED` statistic is a live bug always reporting zero (ledger L21), in
**H.9a**.
And note DESIGN's own warning at N.7: the fabric queues, the cache ports and the tracking
unit **each reported itself the bottleneck and none of which was.**

### O.1a THE LITTLE'S-LAW RULE — residency, occupancy and throughput cannot explain each other

`[OMISSION CORRECTED — DESIGN §15's methodological conclusion (D:1307-1313) was absent,
and it is the rule that disqualifies most of the arguments this Part exists to prevent.]`
Verbatim:

> "**Residency, occupancy and throughput are one measurement in three units, tied by
> Little's law. None can explain a change in the others, and every argument built on their
> ratios was circular.** Only the sampled breakdown underneath them, and the causal test of
> changing the suspected resource, actually attributed anything."

**Two things follow and both are operational:**
1. **A ratio of two of those three is not evidence.** "Cycles per activate", "occupancy
   per unit throughput", "residency against tile-cycles" — each ranks runs by a quantity
   whose numerator is the thing being explained. DESIGN §15 records **cycles/activate**
   convicting the DRAM wrongly, and **tile-cycles instead of context-cycles overstating
   atomic contention by the 1024 contexts a tile holds**.
2. **What DOES attribute:** a **sampled breakdown** of where a context's cycles actually
   go (the residency table in D.4a), and a **causal test** — change the suspected resource
   and see whether the effect moves (rq 64 → 512 there; barrier → ring in H.9b, with
   migrations held as the control). **O.1's three counters tell you where the queue is;
   only these two tell you what the machine is waiting on.**

**THE WORKED FAILURE — carry it with the rule, because it is the only instance in the
record where the law was applied to a device queue and gave the wrong answer** (DESIGN §17
D:1414-1422, in full at N.8). A 32-entry read queue "**appears by Little's law to cap a
channel at `32 / read_latency` ... about **9.8 GB/s** at the ~500-cycle latency a loaded
tile sees**", which read as a clear diagnosis. **The headless replay measured 16.59 GB/s
on the same 32-entry queue.** The argument failed because **"the latency plugged into it
is itself a *consequence* of queueing, not a device property"** — the term was endogenous.
**Test before dividing: is this latency a property of the device, or a product of the
occupancy I am trying to explain?** If the latter, Little's law will confirm whatever you
already believe. See N.8.

### O.2 Time-weight the occupancy, or the machine looks busier than it is

Sampling occupancy per call to a module's `operate()` averages over **busy cycles only**
and reports a machine far busier than it is, because `operate()` is skipped on idle
cycles. Weight by elapsed time instead. (And count it **incrementally**: scanning a
1024-entry array every cycle to maintain a mean and a peak was **the single largest cost
in the host core's profile** — for a statistic.)

### O.3 The rest of the method record

- **Definitive attribution, not dismissal.** *User #72, 2026-08-28T06:46:45Z:* "I would
  like a definitive attribution. Has the balancing actually changed due to the fix? If
  not, it cannot be the culprit. **Please don't dismiss this, 7% is huge in
  microarchitecture, so dismissing this is dismissing beyond the threshold of publishable
  work.**"
- **Build a workload where the policy SHOULD win, then prove it does.** *User #41,
  2026-08-28T00:47:36Z:* "**You need a workload where we can say definitively that NUCA
  should be better, and then prove that it is. Your NUCA was applied to a system where
  not doing anything was the best choice, and that hid the fact that your policy does
  nothing, even when it should.**"
- **Freeze the build before a measurement campaign, and read the binary's banner for its
  workload size.** `make` rebuilds a stress binary at whatever the size default is, so
  **two runs of "the stress workload" are not necessarily the same workload.**
- **Unit-test every newly-authored module, to full functional coverage.** *User #44,
  2026-08-28T01:18:14Z:* "ChampSim has a whole unit-test system that I would appreciate
  if you actually used, write tests for all the custom modules to make sure they behave
  as expected. That would have caught most of these issues immediately ... **I am also
  concerned you might be violating addressing, translation constraints, teleporting data,
  violating thread safety, etc.... which is going to obviate any performance claims we
  make.**" And #66: "**full functional coverage of all our modules via the unit test. All
  the newly-authored modules that is, not the pre-existing default ChampSim models**
  (note that we have lcov support)."
- **Check bijectivity and reachability of every address transform.** *User #50,
  2026-08-28T03:18:30Z:* "**We don't want to see any address aliasing, unreachable areas
  of the address space, etc....**"
- **BOTH SIMULATORS BUILD AGAINST *OUR FORK* OF RAMULATOR2, AND WE CHANGED ITS
  INTERFACE. [ADDED — this standing build decision was absent from the entire document.
  `grep -c "#220\b"` and `grep -c "#255\b"` both returned 0. It was a round-2 finding and
  the round-2 edit pass did not apply it. It is the decision that S28 exists to protect.]**
  *User #220, 2026-09-01T03:44:09Z, choosing between two branches he stated himself:*
  "we were already going to fork this repo too. So the better question is: switch to our
  ramulator work and author our own fork of this and update the interface (probably easier
  than it appears) **or** keep this version of ramulator unique and we still fork this repo
  anyway. **Seems best to just take the hit now, rewrite the interface to work with our
  fork on our own fork of this repo.**"
  *And, fourteen hours later, on being told otherwise — #255, 2026-09-01T18:17:14Z, in his
  own capitals:* "**Did we not set up rev to use OUR FORK OF RAMULATOR? YOU KEEP ACTING
  LIKE IT IS STILL USING ITS OWN FORK. WE LITERALLY CHANGED THE INTERFACE TO USE OUR
  FORK.**"
  **Three operative consequences, all tier 1:**
  1. **There is ONE ramulator2 — ours.** Neither Rev nor ChampSim carries a private
     vendored copy. A statement that either "uses its own fork" is the specific error the
     user has already corrected once, loudly.
  2. **The interface is ours too, and it was deliberately changed.** Ramulator2 upstream's
     interface is not the contract; do not "fix" our call sites back toward it, and do not
     read an upstream signature as authority. (This is also why `preset:` keys behave the
     way E.3 records — **that** fork sentence, at E.3 and R72, is about preset locations
     and is NOT this decision; an earlier revision left "this fork of ramulator2" as the
     document's only trace of the word, so a reader searching for the build decision landed
     on an unrelated fact.)
  3. **Vendoring is not the alternative to moving code.** *User #258,
     2026-09-01T18:53Z:* "**Moving it just means making sure it is part of the fork. Why
     move it? Why?**" Bringing a file under the fork is not the same operation as
     relocating it, and relocation was refused.
  **Build it, do not copy it.** *User #219, 2026-09-01T03:37Z, in full — the canon
  previously carried only the second sentence:* "stop. **Just build the version that will
  match.** I highly discourage you just copying compiled shared libraries between
  codebases. That is very lazy, and the reason we found ourselves here in the first place."
  **"The version that will match" is the positive instruction and it is the load-bearing
  half** — R105 names only the prohibition. The failure it guards is Appendix 2's **S28**:
  the loader configuration beats `LD_LIBRARY_PATH`, so a stale memory-model shared library
  silently supplies the DRAM model and **whichever copy the loader finds first *is* the
  memory model the results came from.**
- **Replay headless to separate "slow channel" from "starved channel".** *User #167,
  2026-08-29T08:19:49Z:* "Can you not just **take a trace of accesses to the dram from the
  sim itself and run it through ramulator2 headless**? It would probably be a faster way
  to find the issue." Inside the simulation the two produce identical symptoms — a deep
  queue, high latency, throughput that does not respond to queue size — and only the
  replay separates them.
- **Design review before implementation, with real alternatives and prior-art checks.**
  *User #3, 2026-08-27T05:34:43Z:* "**I want to okay the design doc before you start, so
  don't dive in immediately.**" The record shows prior-art searches materially changing
  the design at least twice.
- **Do the whole plan-implement-test stack before reporting.** *User #286,
  2026-09-02T02:56:03Z:* "**Do not return to me with a plan. Do the full plan ->
  implement -> test stack before returning to me.**"
- **Write settled decisions down at the moment they settle, in §0.** Loading the design
  once at session start is not enough: *user #113, 2026-08-28T23:28:38Z:* "**It is not
  enough to load at the start of the session. This has been one session, and you have
  forgotten repeatedly.**" And *#107:* "**IT IS NOT ENOUGH TO ACKNOWLEDGE IT WHEN I BRING
  IT UP, IT MUST BE IN YOUR MIND CONSTANTLY.**"
- **Do not reason from the implementation back to the architecture.** When something in
  `src/nmfc/` looks like it needs a new mechanism, **first check whether the design
  already names one. It usually does.**
- **Purely additive integration.** The design must be built purely additively on
  ChampSim — the runtime module system registers interfaces and models by name, so no
  base header or `.cc` is edited. **The only permitted base change is one added Makefile
  glob line** for `src/nmfc/*.cc`. (The one fork of base code is the host core, because
  `operate()`, `initialize()`, `push_instruction()` and `print_deadlock()` are all
  `final` and subclassing cannot hook the pipeline at all.)

### O.4 WHAT THE MACHINE MUST REPORT — the statistics register

`[OMISSION CORRECTED — DESIGN §10 D:976-987 is the register of what this machine reports,
and it had **no counterpart anywhere in this document**. Part O's whole argument is
"instrumentation over ablation" (O.1); an instrumentation argument with no list of
instruments is an argument with nothing behind it. §11 "Layout" D:989-1005 was likewise
uncited and is the file map the list is emitted from.]`

Reported through the existing `module_lifecycle::end_phase(stat_report&)` path — **no
separate reporting mechanism, which is what makes it additive** (§3, I13's "purely
additive" rule).

| group | what it must report |
|---|---|
| **function core** | invocations completed; **cycles by context state**; mean and P99 residency; contexts occupied; issue-slot utilisation; migrations in and out; atomic conflicts; I$/D$ hit rates; **achieved MLP per context and per core** |
| **translation** | per-context cache hit rate **split by code vs data**; TLB hit rate **by page size**; walk count and latency distribution; **remote-walk rate**; **translation cold-start cycles after migration**; translation cycles as a share of context blocked time |
| **mapping / allocation** | allocations by mode; **NMFC-mode allocation failures and STANDARD fallbacks**; **largest allocatable NMFC run per TILE**; **spill rate**; per-tile free-frame imbalance; **a WARNING on every spill and on OOM — never a hard error** (user ruling 2026-09-02 R18) |
| **placement** | invocations per tile under each policy; migration rate |
| **fabric** | messages by class; queue occupancy; link utilisation; back-pressure stalls |
| **FTU** | offloads issued; in-flight mean and max; cycles stalled on back-pressure; **fire-and-forget share** |

**Four of these are load-bearing for questions this document leaves open, and three of the
four are not currently produced:**
- **`remote-walk rate`** is the instrument for invariant 5's *legitimacy* clause — the one
  L37 records as unmeasured because the assertion that would catch a foreign walk is
  uninstalled in the 15 `--walk-routing fabric` configs. **A rate does not need an
  assertion.** Report it and the clause becomes measurable without ungating anything.
- **`largest allocatable NMFC run per tile`** and **`spill rate`** are §12's answer to
  free-resource fragmentation (below). **Under user ruling 2026-09-02 R18 a spill — and an
  OOM — is WARNED, never fatal**, so these two are the only way it becomes visible; an
  implementation that aborts instead has destroyed its own diagnostic.
- **`fire-and-forget share`** is what makes L29's free-at-dispatch divergence visible in a
  number rather than in a code reading.
- **`cycles by context state`** is the context state machine's own instrument. **The state
  list is no longer contested** — H.3 reconciles it to **FREE, READY, RUNNING, BLOCKED,
  DONE**, with `MIGRATING` recorded as a transition because H.8 releases the slot at
  departure (this closed open item **O8**) — so this counter now has a named set of buckets
  to report against, and what it settles is where a context's time actually goes.

**And the four RISKS §12 D:1007-1018 names, three of which this document carried
nowhere.** `[OMISSION CORRECTED — §12 is a written risk register and was uncited.]`

1. **Free-resource fragmentation, and it is SCATTER, not exhaustion.** "*An NMFC unit
   needs N free rows on one tile, so **a scattered free list can fail an allocation
   while total free capacity is ample** — the familiar huge-page problem, not a
   mode-specific one.*" `[VOCABULARY — R18: "tile", not "channel".]` Mitigation: a
   `(tile, row)` free bitmap, with fallback to spill or to STANDARD mode; **failures and
   the largest allocatable run per tile are reported, so fragmentation is visible rather
   than silent.** F.8's "the spill rate is the statistic" is one of the three and not the
   diagnostic one.
2. **THE FUNCTION CORE COULD LOOK ARTIFICIALLY GOOD, AND THIS IS A CAVEAT ON EVERY
   MEASURED FUNCTION-CORE NUMBER IN THIS DOCUMENT.** §12 D:1016, verbatim: "*The function
   core could look artificially good. **It replays resolved control flow, so it never
   mispredicts.** `FLAG_TAKEN_TARGET` plus a configurable fetch bubble is the honesty
   knob, with a sensitivity run.*" ChampSim ships the knob — `FLAG_TAKEN_TARGET`
   (`inc/nmfc/nmfc_trace.h:166-172`, "*so a replayed dynamic trace does not silently hand
   the function core perfect branch prediction for free*") and `fetch_bubble: 1` in every
   config — **and the sensitivity run is not in the record.** Carry this with M.3's core-
   model caveat: **the two together are what "which core produced the number" means.**
   Added to Appendix 3 item 8.
3. **Warmup semantics.** "*Contexts drain across a phase boundary; the phase controller's
   progress unit stays host instructions retired.*" A function core's contexts do not
   respect a ChampSim phase boundary, so a warmup/measure split counts some invocations in
   both. **The progress unit is host instructions retired — not invocations, not tile
   cycles.**
4. **Address compaction must be exactly invertible, in both modes** — and the unit test
   §12 names exists: `test/cpp/src/550-nmfc-tile-map.cc:106`. See C.2.

**And the test suite is a source of design statements, not just of passes.**
`test/cpp/src/55{0..7}-nmfc-*.cc` — eight files, 67 `TEST_CASE`s — **pin behaviour, which
is a tier-2 statement of what the machine does.** This document cites them once (ledger
L23, to establish that the files exist). **Read them before re-deriving a mechanism**:
`550` pins the grain formula, both mapping modes, the `base + t` replica construction and
compaction; `552` pins the vmem's congruent allocation and `expand(compact(pa), t)`;
`553` pins the routers, which is where L2's disputed `VIRTUAL_FIRST`/`TRANSLATE_FIRST`
pair is actually exercised.

---

## PART P — REJECTED: DO NOT REBUILD

**This is the list that prevents regressions.** Every entry names a thing that was
considered and refused, the reason it was refused, and the source. If you find yourself
proposing one of these, you are re-deriving a settled decision from the wrong source —
stop and read Part B.

Marked `[REBUILT]` where the record shows it was rejected and then built again anyway.

### P.1 Address space, translation and placement

| # | Rejected | Reason | Source |
|---|---|---|---|
| R1 | **Virtual-address partitioning** — tiles as a partition of the VA space; `tile_of(virtual_address)` deciding routing | Four reasons: leaks hardware-specific detail into the VA space; exposes the tile layout directly; confines the compiler to a fixed mapping; lets a program steer placement by choosing addresses, which is unfriendly to a shared system | #269 (2026-09-01T20:11); I12; DESIGN §5.0 D:502-506 |
| R2 | **N page-table roots / an independent root per tile** | **It requires virtual-address partitioning** — which is **#269's own parenthesis**, not an inference — and VA partitioning is rejected in the same message, so the branch is not available. Corroborated structurally: under routing-after-translation, discovering "this is not mine" would itself need a remote walk. **`[AUTHORITY CORRECTION]` This row previously cited only #265, which is 46 minutes OLDER than #269 — and #269 presents both walk arrangements as live and ends "I am not sure what the right final surface is." The rejection stands on #269's own logic plus the newer #283; the SURFACE is recorded open at ledger L35.** | **#269 (2026-09-01T20:11, newest and controlling); #283 (2026-09-02T02:18)**; #265 (corroboration); I3; F.5; DESIGN §5.0.2 D:542-549 |
| R3 | **`VIRTUAL_FIRST` / `TRANSLATE_FIRST` as two live arrangements** | Not "one of two arrangements". One duplicated table is **the only one**, and no default may select the other. `[QUALIFIED — "delete the identifiers on sight" is too strong and F.10 says so: #118 requires the vmem-places-tiles policy to REMAIN SUPPORTED as a control ("That policy should be supported, but we don't want to use it"). **Delete them from PROSE that presents them as a live pair — I3, module headers, the hooks; RELABEL them in code as a control.**]` | I3; F.10; #118 (2026-08-28T23:51); DESIGN §5.1 D:576-585 |
| R4 | **One page table on one channel, reached over the fabric** | It is a bug — **"and routing the walks is not the fix"** | I3; #283 (2026-09-02T02:18) |
| R5 | **Routing page-table walks over the fabric** | Walks must be **local**; translation must never cause a migration or cross the fabric | #283; I3; F.6 |
| R6 | **Identity / direct-mapped memory** | The host and function cores need a **unified virtual address space**; and at graph scale a direct-mapped space is "probably a deal-breaker" | #6 (2026-08-27T05:59); #7 |
| R7 | **Fixed address spaces / apertures as a placement mechanism** | "I thought we rejected the idea of fixed address spaces?" — placement is a translation-time decision, not an address range | #20 (2026-08-27T17:45) |
| R8 | **The compiler placing data by choosing virtual addresses** | Placement belongs to the OS at translation time. A workload that sorts by `tile_of(&x)` has assumed the answer and is testing the allocator, not the architecture | I4; DESIGN §5.5 D:670, §4.2 D:465 |
| R9 | **A hint whose payload is a tile number** | A vtile is a **relation** — "these pages belong together" — not a location. Bucketing data by the tile owning an address bakes the tile count into the data structure and violates I12 and I4 | DESIGN §29 D:3290-3299 |
| R10 | **A linker script forcing grain alignment for co-location** | The vtile hint already does that job. Grain alignment is a **space** concern only | #277 (2026-09-01T22:04); I12 |
| R11 | **A mode TABLE for the mapping mode** | Caches tag by address; a table is not carried with an evicted line | DESIGN §5.3 D:627, D:635 |
| R12 | **A PTE-carried bit for the mapping mode** | A dirty line evicted from L2 has no TLB entry behind it, so the bit would have to be stashed in cache-block metadata to survive a writeback. An address bit is already stored, carried and evicted with the line | DESIGN §5.3 D:635 |
| R13 | **Converting a pool of allocations between modes** | "There is no such thing as converting a pool." A freed unit's contents are garbage; a live change is an ordinary page migration — standard NUMA/THP machinery | #12 (2026-08-27T06:59); DESIGN §5.3 D:641 |
| R14 | **A `carry_translations` knob** | Every carried entry is provably invalid after migration. "Building a switch for a provably-useless option is clutter" | #9 (2026-08-27T06:33); DESIGN §7.1 D:933 |
| R15 | **Mapping every page type at 4 KiB** | "Mapping everything to 4 KiB will break, or at the very least incur significant translation overheads + require the OS to make sure and reserve physical frames to ensure multiple 4 KiB virt allocations land next to each other" | #295 (2026-09-02T13:33) |
| R16 | **Making everything huge** (dropping 4 KiB pages) | Real systems still need 4 KiB pages; a machine that quietly made everything huge would **flatter itself** | #10 (2026-08-27T06:41); DESIGN §5.4 |
| R17 | **A big TLB as the answer at graph scale** | 1024 entries at 2 MiB reaches 2 GiB — about 2% of a 100 GiB graph. Huge pages **move the constant, not the asymptote** | DESIGN §5.4 D:659, §6 D:897 |
| R18 | **Asking whether a frame exists, to decide huge-vs-small** | A frame does not exist on first touch, so the answer is "no" for every page the first time — which walks it small, caches a dead entry, and walks it huge on every access after: **two walks per grain, permanently.** Ask the placement **hint** | `inc/nmfc/nmfc_vmem.h:102-112` |
| R19 | **Global TLB invalidation on any remap** | A shootdown model so coarse it dominates the policy it prices — **measured, it doubled the runtime for a 2% change in migrations.** Invalidate per grain, via a generation counter and a log | `inc/nmfc/nmfc_vmem.h:64-74` |
| R20 | **"A spill takes one extra fabric hop to a remote frame"** | **There is no remote data path.** A spill costs a **migration** and a broken co-location, not a longer access | DESIGN §5.7 D:678 |
| R21 | **Mixing mapping modes inside one allocation group** | The two modes are different tilings of one space and line up only at an aligned run of N grains. Mixing aliases — **measured at 6.7% of blocks colliding on a channel-local address**, undetectable below the controller | `src/nmfc/nmfc_vmem.cc:404-416` |
| R22 | **A grain carrying two page TYPES** | Half of it cannot be duplicated on every tile while the other half is silo'd to one | I12 |
| R23a | **Giving `.rodata` a vtile and co-locating it with the code** | **"This misreads what a vtile does. A vtile gathers a coherent set onto *one* tile; code is wanted on *all* of them. Duplication is not a stronger form of co-location, it is a different type"**, and the read-only restriction is what makes duplication sound. This is the sharpest statement in the record of the vtile-vs-duplicate distinction, and getting it wrong puts every constant on one tile — see the scar "`.rodata` left striped → every context on every other tile migrated on its first constant" (Appendix 2 D7) | DESIGN §26.1 D:2738-2742; C.3; F.2 |
| R23 | **Forking `PageTableWalker` and `CACHE`** to get huge pages and dual sizes | One `channel` model does it: a stock walker cannot terminate early so it cannot express a huge page, and a TLB is a cache with fixed offset bits so one array cannot hold both sizes | DESIGN §6 D:885, D:891 |

### P.2 Decomposition and the unit of work

| # | Rejected | Reason | Source |
|---|---|---|---|
| R24 | **SPAWN — a function creating a second live invocation** | **Deadlock-captive. A spawn from a spawn is by nature unbounded.** No admission control is safe without a per-core tracking unit *and* a depth bound, and "you may only spawn one deep" is a constraint nobody can honour. If work is discovered the function cannot do itself, **the unit of work is shaped wrong**. **`[NOW DELETED FROM THE TREE, not merely rejected on paper — user ruling 2026-09-02 R1: "delete it. CONT/extend is fine, and can stay."]`** | #181 (2026-08-29T19:39); #87; **R1**; I10 |
| R25 | **The CHASE decomposition** — own an edge range and chase scattered vertex values `[REBUILT]` | **0.7428 migrations per instruction on GAP BFS — three quarters of all work was a context moving itself — and 0.384 on the synthetic scattered set. Two rows, two workloads; 0.38 is NOT the three-quarters figure (K.3).** 1.43 accesses between tile switches. The placement fix did not move it: **this is the decomposition, not the placement** | DESIGN §14.0 D:1101-1106, §20.2 D:1707-1718 |
| R26 | **A function that exists for a single instruction before retiring** | "A 1-instruction function by nature does NO WORK. THAT ONE INSTRUCTION WOULD BE THE RETURN CALL" | #79 (2026-08-28T19:12); #84 |
| R27 | **Dividing a whole program's work across ~5 invocations** | "If you are divying up work for the entire program across only 5 invocations, that is bad too" | #121 (2026-08-29T02:31) |
| R28 | **One homogeneous set of functions** | "The functions can be heterogeneous you know" / "You know you can split the work into two or more sets of functions right?" | #39, #93 |
| R29 | **A shared frontier appended through an atomic counter** | One word contended by every claim on every tile: each one must reach that word and **the whole machine serialises on a single address.** A slot per invocation has no shared state at all | `tools/nmfc/kernels/bfs_nmfc.cc:68-89` |
| R30 | **Counting distinct registers touched, as the admission test** | Partial-width views are separate tracer ids (`rax`/`eax`/`al` = 3 ids, 1 register) → reported 17 and 21 where the answer is 8, and **rejected a function holding ~480 bits that fits.** Count **peak simultaneous liveness, in bits** | DESIGN §4.1 D:338-342; `tools/nmfc/annotate.cc:492-507` |
| R31 | **Truncating a function that does not fit the register file** | "do not truncate, which would drop dependencies and flatter the scoreboard" | `tools/nmfc/annotate.cc:546-549` |
| R32 | **A model that hand-writes instructions, PCs or function bodies** | It invented program counters, "and everything they invented was wrong in the same direction." The compiler owns instructions and PCs; the model controls only code and data **layout** | #82 (2026-08-28T19:23); #80, #81 |
| R33 | **A batch barrier around a level of offloaded work** | Left every DRAM channel **~64% idle**, occupancy sawtoothing between the cap and nothing, and **the idle fraction did not care whether a tile held 17 or 45 contexts — it was set by batch cadence, not capacity.** **Use a ring — defined in H.9b: a host-side fork-loop pattern over the output slot pool, retiring the OLDEST invocation to free one slot and reusing it immediately, so `LEVEL` invocations stay outstanding continuously. Not hardware; nothing in `src/nmfc/` differs** | DESIGN §20.1; `bfs_nmfc.cc:346-355`, `:388-396`; H.9b |
| R34 | **A deeper ring with FIFO retirement** | Depth 4096 (**depth = `NMFC_LEVEL` = `LEVEL` = outstanding invocations = slots in the pool; one quantity, four names — H.9b**): **+9.3% cycles, queue-empty 38.4% → 43.1%** — the host blocked on one straggler while thousands of finished invocations queued behind it. **The mechanism worked (3.4× more outstanding); FIFO retirement is what spoiled it, so depth cannot be re-swept until retirement changes** | DESIGN §20.1, §23.2; H.9b |
| R35 | **Consuming a fork's return immediately** | The join lands one instruction behind the fork and the machine holds one invocation however much room it has. Measured: **peak forks outstanding = 1**, median 2 host instructions fork→join | #134 (2026-08-29T05:02); DESIGN §14 |

### P.3 Placement policy

| # | Rejected | Reason | Source |
|---|---|---|---|
| R36 | **Round-robin, least-loaded or first-touch AS a NUCA policy** | They are dispatch policies. **Physical placement without a NUCA/NUMA policy is not the design** | I6; DESIGN §5.5 D:670 |
| R37 | **Round-robin placement of grains** (a counter that never reads the address) `[REBUILT]` | A grain's tile then depends on the order it was first touched, and **grains carry very unequal traffic, so spreading them evenly by count spreads traffic unevenly — 75.3% of all accesses routed to the wrong tile**. **WHAT REPLACED IT — previously unrecorded — is VIRTUAL-ADDRESS CONGRUENCE, `(va >> grain_bits) % num_tiles`, which is itself on F.3's delete-on-sight list. Read A.4a; do not "restore balance" by putting the counter back.** Balance belongs to `remap_grain()` | `src/nmfc/nmfc_vmem.cc:386-393, 531-538, 555-559`; `nuca_router.cc:104-110`; I9; A.4a; L38 |
| R38 | **First-touch as the definition of congruence** | Balancing belongs to `remap_grain()`, which moves whole components deliberately; it does not belong to first touch | I9 |
| R39 | **Placing grains one at a time** | Cannot escape a random start: a cluster scattered over N tiles pulls uniformly from all N, so **scattered is a stable equilibrium** — measured dominance 0.718 with **20 of 345 grains ever moved** against an oracle 19.5× better | DESIGN §21.1; `src/nmfc/nuca_router.cc:126-132` |
| R40 | **Co-locating a SHARED grain, however lopsided its pull** | 0.77 dominance was real evidence and acting on it was still wrong: **the remaining 0.23 was three other tiles that would then have to migrate** | `src/nmfc/nuca_router.cc:28-33` |
| R41 | **Co-locating a component larger than a tile's fair share** — where "fair share" is `ceil(\|observed grains\| / tiles)` in **grains**, `nuca_router.cc:253`, **not** the traffic-window share at `:244`/`:265` that gates R42 | That is not a hot set, it is **the whole working set**. On a graph where everything touches everything the co-access graph is fully connected, and collapsing it onto one tile is the failure an offline min-cut already demonstrated | `src/nmfc/nuca_router.cc:246-256` |
| R42 | **A lifetime-average imbalance gate** | Starts even and responds to nothing: **allowed 14 co-locations while cumulative load still looked balanced, those 14 were the hottest grains in the machine, occupancy became 71/75/538/73, and it then refused 344 further moves correctly and far too late to matter.** Gate on a window | `src/nmfc/nuca_router.cc:201-209` |
| R43 | **Offline balanced minimum-cut partitioning** | **16% fewer migrations and 2.3× slower**, with two tiles at an occupancy of one context. It has exactly one fixed point: everything on one tile, zero migrations, three quarters of the machine idle | `src/nmfc/adaptive_router.cc:1-34`; DESIGN §14 |
| R44 | **A hand-rolled adaptive pull policy with one action and no condition to decline** | "That is the whole of why it lost." On kron it concentrated work onto two tiles and ran **1.45× slower than plain round-robin**, with runtime tracking the occupancy spread almost exactly — even after hysteresis and a traffic-weighted balance term | `src/nmfc/nuca_router.cc:1-41` |
| R45 | **Uniting grains that merely migrated consecutively** | The co-access relation holds between addresses touched by **the same invocation**; uniting anything else merges everything into one component and says nothing | `inc/nmfc/tile_router.h:111-119` |
| R46 | **Counting the FIRST migration as locality evidence** | It reports where dispatch put the invocation and nothing else — uniform noise credited as locality. Including it pulled mean pull dominance to **0.670** and classified two thirds of grains as shared | `src/nmfc/function_core.cc:1143-1149` |
| R47 | **A placement policy that reads the data a function is about to touch** | **Dispatch happens before the function runs, so nothing in the machine knows what it will touch.** The simulator was reading the future out of a data structure the hardware does not have, and it flattered every result by removing migrations no real machine could have avoided | `src/nmfc/function_fabric.cc:281-288`; I4 |
| R48 | **Sub-grain swaps as the primary mechanism** | Viable and sometimes necessary, but a duplication policy reaches the same effect "without the bookkeeping a partial swap drags in" | #118 (2026-08-28T23:51) |
| R49 | **Aggressive duplication in the BASELINE** | It is an **option**. Measured, **one row**: **4.96× fewer migrations (1,703,838 → 343,858) and 38.2% slower (91.0 → 125.8 ms)**; adding first-touch gives 6.93× and +36.8%. **Never quote the 5–7× range** (Part L). The mechanism: aggregate capacity for the replicated array fell from ~16 MiB to ~4 MiB | #298 (2026-09-02T17:44); Part L |

### P.4 Topology, coherence, and the fabric

| # | Rejected | Reason | Source |
|---|---|---|---|
| R50 | **A second interconnect for NMFC traffic** | There is **one** fabric. A private NMFC channel is what makes the subsumption claim (I11) unmeasurable, because it only holds if migration and data contend for the same interconnect | I13; #227 (2026-09-01T04:41) |
| R51 | **Putting a tile's own caches on a network** | That has **moved the core off the tile no matter what the diagram says** | I13 |
| R52 | **The function core across the fabric from its tile** | "The nmfc core itself does NOT live across the fabric from the memory tile. **It is THERE**, on the same end of the fabric as the slice of the LLC, memory controller, and channel" | #283 (2026-09-02T02:18) |
| R53 | **The tile core above the directory** | "Tile core needs to exist below the directory, there is no reason to have it above the directory at all? **We want 1 core per memory tile**" | #247 (2026-09-01T17:25) |
| R54 | **Attaching a function core to a particular host core** (a coprocessor subcomponent) | "We don't want to attach any nmfc processor to a particular standard core. **That is part of the point.**" A coprocessor shares the host's memory and would **make locality free — the quantity the design exists to measure** | #236 (2026-09-01T06:01); DESIGN §25.1 |
| R55 | **The offload engine as a `channel` model** (intercepting at the memory path) | Replaced by a real forked host core. **`[WEAK SOURCE — this row's only citation is a MODEL-AUTHORED memory note, i.e. tier 3 by the prior-sessions extraction's own warning: "these files are model-authored ... They are not Tier 1. Treat them as Tier 3." No tier-1 or tier-2 citation was found for it.]` The rejection is almost certainly right — the founding items #1 and #2 place the NMFC core in the memory tile, and R54/R52/I13 forbid the coprocessor shape on tier-1 authority — but as written this Part-P row rests on the model's own paraphrase, which is the failure mode Part P exists to prevent.** Re-source it or fold it into R54. | memory note `nmfc-design-review-first.md:11` **(tier 3, model-authored)**; the substance is carried by R52/R54/I13 |
| R56 | **MESI** | It cannot express `O` (dirty+shared owner) or `F` (clean designated forwarder) — the two states that decide what host/NMFC sharing costs — **and it cannot express I14's strict priority, which is an order rather than a tie-break** | #285 (2026-09-02T02:54); DESIGN §5.9 D:765-771 |
| R57 | **memHierarchy above the coherence point** | Same reason: it implements MESI. `O` is in its enum and no coherence manager ever assigns it; there is no `F` at all | DESIGN §5.9 D:765-771 |
| R58 | **Implementing atomics against the memory system** (LR/SC, a locking read) | "you are using regular atomicity instead of leveraging the core." What makes atomics nearly free is **ownership**: a context migrates to its data, so the table needs no cross-tile protocol | #252 (2026-09-01T17:48) |
| R59 | **Removing atomics** | "if we just blatantly remove the atomics, we must assume atomicity for all operations **which could be devastating**" | #238 (2026-09-01T06:19) |
| R60 | **Locking the whole cache line for an atomic** | Made a line's worth of unrelated counters contend for a critical section spanning a memory round trip — which is what a graph kernel's parent array looks like. **Lock the operand** | `src/nmfc/function_core.cc:971-983` |
| R61 | **Spinning on a held lock** | A retrying context costs an examination slot every cycle and the issue loop's budget is bounded, so contention becomes **superlinear rather than merely serial.** Park and wake on release | `src/nmfc/function_core.cc:986-999` |
| R62 | **Holding a tile slot while waiting for fabric space** | Hold-and-wait. **Deadlocked at cycle 9,100,426: four tiles at 0–1 free contexts, 983 tokens waiting, 124 of 124 occupied contexts on one tile in the migrating state.** Release the slot at departure | `src/nmfc/function_core.cc:1156-1176`; #209 |
| R63 | **An age guarantee as a substitute for releasing the slot** | Reserving a slot for the oldest waiter admits one context into a full tile but **promises nothing about that context ever leaving** — the reserve is consumed once and gone | `src/nmfc/function_core.cc:1170-1173` |
| R64 | **One shared fabric queue across destinations** | One congested destination owns all of it — **measured, 128 of 128 entries bound for one tile that had 0 free contexts**, and 128 queued invocations for a full tile 0 while the others sat empty | `src/nmfc/function_fabric.cc:479-493` |
| R65 | **Stopping at the head of an outgoing migration queue** | A refusal names one congested destination, not a congested fabric; stopping lets a context bound for a full tile pin every context behind it that has somewhere to go | `src/nmfc/function_core.cc:1208-1237` |
| R66 | **Per-message serialisation as a bandwidth model** | Two messages in the same cycle each paid their own delay and neither waited for the other — **a bandwidth number with no contention behind it** | DESIGN §5.9 D:803-810 |
| R67 | **A unified I/D cache on the function core** | "They are **fundamentally different working sets.** We expect small instruction footprints, high locality, excellent caching efficiency. Data will be exactly the opposite. **They will conflict if cached together**" — a shared cache lets the data evict the code | #249 (2026-09-01T17:35) |
| R68 | **A monolithic 512-entry queue for concurrency** | "Building a monolithic 512-entry queue seems like a bad idea" — get concurrency from **banking**, address-sliced, not from a CAM that does not exist in silicon | #74 (2026-08-28T07:37); DESIGN §16 D:1359-1361 |
| R69 | **A single ramulator2 instance with many channels** | **Each memory tile owns exactly one ramulator2 instance.** "why are you making a single ramulator instance have 16 channels? What are you even doing?" | #51 (2026-08-28T03:33); #49; #55 |
| R70 | **Setting DRAM geometry twice** (hand-setting the grain) | "Don't bother setting the two separately, **that just invites mismatches.**" The device decides; a contradicting `--grain-bits` is refused | #50 (2026-08-28T03:18) |
| R71 | **Copying a clock ratio you saw somewhere** (the "8/3 tick") | "Don't just copy an 8/3 tick because you saw it somewhere. That is lazy, and, in this case, **blatantly incorrect**." We define the frequency ratio by the rate we tick the model against our own simulation speed | #49 (2026-08-28T03:11) |
| R72 | **Inventing a ramulator2 organisation** | "You understand there are pre-defined orgs right? **You should be using those, not inventing one wholesale.**" `[CORRECTION — an earlier revision appended "and the presets live in the compiled-in headers", which DESIGN explicitly corrects and which is operationally load-bearing.]` DESIGN §17 D:1396-1402: **"In this fork of ramulator2 the presets live in `python/ramulator/dram/ddr5.py`, not in a header ... Earlier revisions of ramulator2 did compile the presets into headers; this one does not, so `preset:` keys in a YAML are SILENTLY IGNORED rather than rejected."** So a `preset:` key written on the old advice runs a different device from the one it names, with no error. **Use the predefined orgs by generating the YAML (E.3), never by writing a `preset:` key.** | #147, #148 (2026-08-29T06:07-06:08); DESIGN §17 D:1396-1402 |
| R73a | **Building the fabric on Merlin** | "its clients must be network endpoints ... **The fabric here is an L2-to-LLC interconnect with a partition applied at it, which is a routing rule and a cost model, not a flit-level network.**" | DESIGN §26.2 D:2799-2803 |
| R73b | **Charging extra cycles in the tile when `tr.tile != tile_`** | "Still rejected ... **there is nothing to charge. A foreign translation does not produce a slow access, it produces a migration.**" | DESIGN §26.2 D:2804-2807 |
| R73c | **Leaving the host on a `tilebus` and letting the fabric carry NMFC traffic only** | **"The comfortable option and the one §24 step 3 forbids by name."** It also leaves the model without the L2-to-LLC boundary, **so invariant 14 stays unbuildable**. This is the drift a rebuilder falls into, and it is what ChampSim has effectively shipped — see ledger L25 | DESIGN §26.2 D:2808-2810; I13; I14 |
| R73d | **Per-link latencies on the shared bus** | Inexpressible: **"latency is a property of (cache, bus), never of (cache, slice)."** A model that needs per-link latency has the wrong topology | DESIGN §26.2 D:2811-2814 |
| R73 | **Chasing the DRAM address mapping as a performance knob** — `[QUALIFIED, NOT CLOSED. The user's sentence is hedged and outcome-conditional; an earlier revision of this row dropped both qualifiers and appended a measurement he never gave.]` | #62 **in full**: "*Regardless, **it seems like** messing with the mappings is the wrong knob. We can leave them as default **if nothing comes of these last few experiments**.*" — a hedge plus a condition whose outcome this document does not record. **The 6.7%-row-hits figure is from `nmfc_addr_mapper.cc`, NOT from #62; do not attribute it to the user.** And this row does **NOT** retire #58's request to **put the bank bits lower and hash them, for PARALLELISM** — a different question, still open, recorded at E.5 with #59 and #64 | #62 (2026-08-28T04:31); E.6; `src/nmfc/nmfc_addr_mapper.cc:9-18`. **Contrast #58, #59, #64 (2026-08-28T04:03-04:34) — E.5** |

### P.5 ISA and host side

| # | Rejected | Reason | Source |
|---|---|---|---|
| R74 | **"A load in the offload aperture is a fork" as a machine mechanism** `[REBUILT]` | The aperture is a **trace-record encoding**. "Your hack will make it into the design spec if you keep doing this" | #96, #97 (2026-08-28T20:52-20:55); I1 |
| R75 | **Any blocking instruction** | "a resource held while waiting for a resource" — the shape that deadlocked the machine. **Making it impossible to express is cheaper than auditing for it** | #222 (2026-09-01T04:16); DESIGN §23.1 |
| R76 | **A separate blocking `JOIN` and permissive `PJOIN`** | They are the same instruction; the blocking one was only a spin the hardware performed for you | DESIGN §23.1 D:2005-2008 |
| R77 | **A `KILL` instruction** | A context killed mid-update leaves memory in a state nobody can reason about, so a kill must be **cooperative** — a new instruction, a protocol, and a liveness obligation on every function written thereafter. **Encoding space reserved** | #224 (2026-09-01T04:32) |
| R78 | **Mailboxes (`SEND`/`RECEIVE`/`PRECEIVE`)** | Need a context-to-location directory updated **on every migration** (8.9M on one measured run) and a bounded buffer that can fill = hold-and-wait. Functions already communicate through memory, and ownership makes that coherent without protocol. **Encoding space reserved** | DESIGN §23.5 D:2173-2179; #89 |
| R79 | **`context_id` / `tile_id` CSRs** — **NOT A REJECTION. Unbuilt, pending an answer to the user's question.** | `[AUTHORITY CORRECTION — this row previously read "proposed and dropped" with a rationale the user never gave. #222 item 7 is a QUESTION: "I am not sure how context_id or tile_id are used practically by the core? FTU occupancy makes sense and is buildable. context_id would be the context_id of what specifically? same with tile_id, the tile_id of what?"]` Nothing uses them and nobody has named a referent, so nothing is built. **The "exposing a tile id invites the R8/R9 violation" argument is this document's, not the user's** — a real objection to answer, but do not attribute it to him. **This row is retained in Part P as a pointer, not as a settled rejection.** | #222 (2026-09-01T04:16); I.7 |
| R80 | **Per-invocation fault status / error codes / a fault probe** | They only make sense with user-defined fault handlers, which this machine does not have | #223; DESIGN §23.4 |
| R81 | **Reusing `ret` as the function-side return** | "We probably shouldn't reuse ret, since that will cause confusion and **it genuinely behaves differently**" | #222 |
| R82 | **A fixed aperture of eight general registers as the context** | A fixed aperture holds **one** staged context, so an addressable tracking unit that exists to allow out-of-order join then has one landing pad to join into. The instruction-count argument also reverses on the real workload | DESIGN §23.6 D:2246-2256 |
| R83 | **RVV with `VLEN=512` for the context registers** | Standards-compliant, but means writing a vector unit from scratch first — nothing in Rev, Vanadis or sst-elements has one | DESIGN §23.6 D:2192-2197; #231 |
| R84 | **Bit-field insert/extract with an offset and a width** | Would duplicate instructions RV64I already has. Only **extraction** must be possible; "Let's not overdesign" | #233 (2026-09-01T05:47) |
| R85 | **`funct3` as the ISA group selector** | A RoCC host reads `funct3` as the operand flags, so it would decode `FORK` as **"uses no registers"** | DESIGN §26 D:2903-2923 |
| R86 | **An FTU with fewer payloads than entries** | Every outstanding invocation may complete before any `JOIN`; otherwise a tile holds a finished context waiting for the host = the deadlock shape | DESIGN §23.2 D:2071-2077 |
| R87 | **An evicting, cache-like FTU** | An entry holds the **only copy** of a returned register file, and a join-expected entry must never close without returning it. It **refuses** | DESIGN §23.2 D:2079-2086 |
| R88 | **Splitting the FTU into join-tracked and fire-and-forget pools** | Buys replayability, but "partitions a resource for the benefit of a hint" | DESIGN §23.2 D:2094-2100 |
| R89 | **A regfile-only invocation** (ROB-scheduled, no fork/join) | Input setup and output capture must both happen alongside each invocation, which **the register file serialises** | #19 (2026-08-27T17:34) |
| R90 | **Subclassing the base OoO core** | `operate()`, `initialize()`, `push_instruction()` and `print_deadlock()` are all `final`; subclassing cannot hook the pipeline at all | `inc/nmfc/nmfc_host_core.h:2-17` |
| R91 | **Reusing the LSQ for offload tracking** | Offload concurrency is its own knob, not an accident of load-queue depth | `inc/nmfc/nmfc_host_core.h:479-485` |
| R92 | **Modifying base ChampSim headers or `.cc` files** | Purely additive integration; the sole permitted base change is **one Makefile glob line** | #1 (2026-08-27T05:17); memory note `nmfc-project.md:19` |
| R93 | **Editing an instruction record type to carry NMFC fields** | It is not to be modified — hence the aperture encoding | DESIGN §4 D:291 |
| R94 | **A per-context branch predictor on the function core** | Would multiply by the context count and be **cold on every invocation** — the wrong shape twice over. The BTB is **shared** | DESIGN §25.3 D:2409-2416 |
| R95 | **Multiple outstanding loads per context** | Needs disambiguation of which return is which and a way to keep the register file coherent across them; **neither is needed for correctness.** "I doubt it will make a difference, just adding excessive state and complexity with no benefit" | #239 (2026-09-01T06:27) |
| R96 | **Treating a 100% instruction hit rate as an invariant** | "**100% instruction hit rate is not an invariant. that is something you fabricated.**" Model the cache | #250 (2026-09-01T17:38) |

### P.6 Method and evaluation

| # | Rejected | Reason | Source |
|---|---|---|---|
| R97 | **Blind ablation as the instrument** | "instrumentation is the best way to actually answer this question, not blind ablation." An ablation answers by elimination and only for the resource swept | #300 (2026-09-02T21:46) |
| R98 | **Quoting a bandwidth-utilisation figure AS A SPEEDUP** — **note the narrowing; the measurement itself is NOT rejected** | `[CORRECTION — this row previously read "judging the machine by bandwidth utilisation", filed under DO NOT REBUILD, while quoting the very clause in which DESIGN ENDORSES that measurement.]` DESIGN §21.3 D:1887-1890: 77.9% vs 20.2% **"was the RIGHT MEASUREMENT FOR THE PREMISE** but is not a speedup". **DESIGN keeps it as the standing validation of the premise at D:145-180 — see N.7, where 13.7% vs 76.9% is called "the architecture" and the 6.19× "a consequence of it".** What is rejected is reporting a utilisation percentage as though it were a speed-up, and comparing NMFC configurations only against other NMFC configurations. **Take the measurement; do not call it a speedup.** | DESIGN §21.3 D:1887-1890; DESIGN §0 D:145-180; N.2, N.7 |
| R99 | **Measuring the architecture against a weaker algorithm** | Would have condemned a working machine: 6.73× architecture gain, **7.38× algorithm penalty**, net **0.91× loss** (6.73 / 7.38 = 0.91). **The 5.3× that appears in §21.3 and in older revisions of A.7 and I8 is the raw extra-work count from the earlier write-up, unit never stated at any tier, and is RETIRED under Appendix 3 item 8 — that is what closed open item O14, since "state the unit" asked for a fact no source contains. It does not close the arithmetic; use 7.38× as the penalty and say which you mean.** N.5 defines all three columns: architecture gain = row 2 ÷ row 3 cycles, net = row 1 ÷ row 3 cycles, **algorithm penalty = row 2 ÷ row 1 cycles** | I8; N.5; DESIGN §21.3 D:1895-1906 |
| R100 | **Averaging occupancy over busy cycles only** | Reports a machine far busier than it is, because the module is skipped on idle cycles. **Time-weight it** | `src/nmfc/function_core.cc:219-222` |
| R101 | **Quoting a run's numbers as hardware evidence without the two-axis gate** | Most of the conclusions reached before the gate existed — that migration was ruinous, that the placement policy was inert — **came from runs that would have failed it** | `tools/nmfc/qualify.py:2-17` |
| R102 | **Reporting a rate from a phase that ended with work in flight** | It counts work that was not done, and it is wrong **in the flattering direction** — this produced a reported **207× speedup** | `src/nmfc/function_image.cc:80-92` |
| R103 | **Reporting only host IPC** | "describes a machine that is deliberately doing its work elsewhere" | #18 (2026-08-27T17:22) |
| R104 | **Comparing an SST number to a ChampSim number before the baseline reproduction** | Different core models with different MLP at equal context count | I8 caveat; DESIGN §24 step 0 |
| R105 | **Copying compiled shared libraries between codebases** — the instruction is positive and the canon previously dropped its first clause: "**stop. Just build the version that will match.**" | "That is very lazy, and **the reason we found ourselves here in the first place**". The failure mode is Appendix 2 **S28** — the loader finds a stale library and *that* becomes the memory model | #219 (2026-09-01T03:37) |
| R105a | **Keeping a private/vendored ramulator2 in either simulator, or treating upstream's interface as the contract** | Both Rev and ChampSim build against **OUR fork**, whose interface **we changed** on purpose: "**Seems best to just take the hit now, rewrite the interface to work with our fork on our own fork of this repo**" (#220); "**WE LITERALLY CHANGED THE INTERFACE TO USE OUR FORK**" (#255). Bringing a file under the fork ≠ moving it (#258) | #220 (2026-09-01T03:44), #255 (2026-09-01T18:17), #258; O.3 |
| R110 | **Forming a function's entry PC as `entry_pc_base + t · G`** (a per-tile bias added by the dispatcher) `[RETIRED MECHANISM — it was the design, under the OLD layout of N distinct virtual pages]` | Function code is now **one virtual page aliased to one physical copy per channel**, so the tile is resolved by translation, not encoded in the address. Adding the bias "**sends the invocation to an address that is not its code at all**". Consequences: `aux0` is the entry PC, not a base; §5.5's "one add, no translation, no lookup" is dead and F.1's *translate, then route* replaced it; and **I5's legitimacy clause is only achievable under aliasing** | `src/nmfc/function_fabric.cc:115-121` (tier 2) **over** DESIGN §8 D:958, §7.1 D:931, §5.5 D:663 (tier 3); H.8, F.7 |
| R106 | **Multi-node scale-out** | "we are a single node and are staying that way, though we may simulate a system with some parallelism" | #217 (2026-09-01T02:03) |
| R107 | **Kneecapping the architecture and then declaring it does not work** | "If you don't understand the underlying principles here, you won't be able to fix it. Or worse, **you will claim this architecture doesn't work because you kneecap it at every concievable opportunity**" | #27 (2026-08-27T18:40) |

---

## APPENDIX 1 — CONFLICT LEDGER

Every place the sources disagree, which authority won, and why. Authority order:
**session log > ChampSim > docs > SST.** Rows marked **[FOR THE USER TO RULE]** are
user-vs-ChampSim conflicts, or genuinely open questions, that this document does not
resolve on its own authority.

**Count: 49 conflicts (L1–L49, no gaps).** [CORRECTED — this header read "37" while the
file carried 42 rows, so a reader auditing the ledger by its own count stopped five rows
early. `grep -cE '^\*\*L[0-9]+ —' docs/nmfc/CANON.md` is the check; run it after
adding a row. Seven rows are new this revision — **L43**–**L49**, all from Part I and the
mechanisms Part I depends on.]

**Twenty-six rows carry a `RULED` bullet dated 2026-09-02, naming the ruling that closed
them; two more — L2 (R12) and L13 — were closed in editing; and **three rows carry a
`RULED` bullet dated 2026-09-03**: **L38** (`O1`, the unhinted-grain default), **L43**
(`O3`, the encoding), and **L46** (`O4`, the RISC-V subset). **No row has been deleted.**

**NO `[FOR THE USER TO RULE]` TAG IS STILL LIVE ANYWHERE IN THIS APPENDIX.** The last two
— L38 (`O1`) and L43 (`O3`) — were closed by the 2026-09-03 rulings; L47's was stale, since
R14 closed it, and is corrected. **A row's tag text is left in place as the record of what
was asked; the `RULED` bullet at the end of the row is what governs.** The front matter's
**RULINGS NEEDED FROM THE USER** is now a record of rulings rather than a request for them,
and it holds **nothing open**. **That section is the index; this appendix is the evidence,
including the evidence behind rows that are now closed.**

`[HOW TO READ A CLOSED ROW.]` The row's original body is untouched — it is the record of
what the conflict was and what each tier said. **The `RULED` bullet at the end is what
governs.** Where a row is closed *by the ChampSim freeze* rather than by a ruling on its
substance, the bullet says **"frozen, not abandoned"**, and the design statement in the body
still stands as the thing to build when the freeze lifts.

---

**L1 — The offload aperture: mechanism or encoding?**
- *Tier 1 (#96 2026-08-28T20:52, #97 2026-08-28T20:55, #86 2026-08-28T19:36):* an
  offload is an **instruction**; "you keep repeating that a load in the offload aperture
  is a fork. Why?"; "Your hack will make it into the design spec if you keep doing this."
- *Tier 2 (ChampSim):* the aperture **is** a real host-core mechanism —
  `is_offload(addr)` on a bounded VA window, checked at `do_memory_scheduling`
  (`inc/nmfc/nmfc_host_core.h:532-551`), with an aperture ↔ token bijection
  (`base + (token << 6)`), and the code's own comment calls it "a property of the
  machine, not of the data".
- **RULING: tier 1 wins. The architecture's offload is `FORK`. ChampSim's aperture is
  how a fixed trace record encodes it — a simulator convenience, not the machine.**
  ChampSim is not *wrong* to have it; it is wrong to reason from it back to the
  architecture. Recorded in I1 and R74.

**L2 — "N page-table roots" is still being injected into every agent. [CRITICAL]** **[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#265 2026-09-01T19:25):* per-slice page-table roots are "completely
  outdated". *(#269 2026-09-01T20:11)* the whole VA-partitioning branch is rejected.
- *Tier 3, and actively harmful:* `MEMORY.md:2` still reads "restrictive VA→channel,
  flexible VA→frame, **N page-table roots**; why identity mapping was rejected" — and
  that line is injected into the system prompt of every agent working on this project.
  `nmfc/.claude/nmfc_invariants.sh:19-22` presents "N roots ⇒ table partitioned. One
  root ⇒ table DUPLICATED per channel" as a live pair of options, and `:53-59` presents
  `VIRTUAL_FIRST` ("the address IS the placement") as a live mode. That script fires on
  every `Edit|Write` under `src/nmfc/` and friends.
- *Tier 2 (ChampSim) — and THIS IS THE PART THE LEDGER MISSED, because tier 2 outranks
  every doc and hook named above:* the rejected framing is **load-bearing inside ChampSim
  itself**.
  - `src/nmfc/function_core.cc:17-20` — **the module header of the function core** —
    states it unconditionally: "Routing is decided on the **virtual** address, before any
    translation, which is what makes a migration decision independent of the MMU.
    Congruent allocation is the promise that the frame lands on the tile the VA named;
    TILE_PORT asserts it." **The code is in fact conditional**
    (`function_core.cc:912`: `const bool translate_first = router_->order() ==
    nmfc::routing_order::TRANSLATE_FIRST;`), **so the header teaches the rejected design
    as if it were the design.**
  - `inc/nmfc/nmfc_vmem.h:118-124` documents the **partitioned** table as the normal
    case: "An ordinary address sits in exactly one channel's partition of the table, so
    the root follows the address and the walk is local."
  - `inc/nmfc/tile_router.h:58-62` defines `routing_order::{VIRTUAL_FIRST,
    TRANSLATE_FIRST}` as a live pair with `order()` **pure virtual** at `:75` — i.e. **the
    identifiers R3 says to "delete on sight" are load-bearing abstractions in tier-2
    source.**
  - `CONGRUENT_ROUTER` returns `page_table_roots() == num_tiles`, and it is the router in
    the **default shipped config**.
- **RULING: tier 1 wins. There is ONE PAGE TABLE PER ADDRESS SPACE, duplicated on every
  tile; TLBs are SHARED and ASID-tagged.** [SHARPENED by user ruling 2026-09-02 R12 — this
  bullet previously read "There is ONE page table, on duplicate pages", which is the
  pre-R12 form and is the reading **I3** now kills: **"one page table" never meant one for
  the machine.** Executing the ACTION REQUIRED below against that older sentence would
  write the superseded statement back into the always-injected memory, which is exactly the
  re-injection this row exists to stop.]
  **ACTION REQUIRED (not a documentation nicety):** rewrite `MEMORY.md:2` **to the ruled
  sentence above, verbatim — one page table PER ADDRESS SPACE, duplicated on every tile,
  TLBs shared and ASID-tagged — and to nothing shorter**; regenerate
  `nmfc_invariants.sh` from DESIGN.md §0 using the same `awk` extract the
  `UserPromptSubmit` hook uses, or delete it. **The hook that exists to stop the design
  being dropped is currently a vector for dropping it.** Note also that it matches only
  `Edit|Write`, so an edit made through Bash — the default working mode here — does not
  fire it at all.
- **RULED — user ruling 2026-09-02 R12: "I am fairly certain real machines have separate page tables per address space? TLBs are shared, page tables themselves should not be shared between address spaces?"** **The surface is: ONE page table per address space, duplicated on every tile, with shared ASID-tagged TLBs.** The `asid` both selects the table and forms the placement key `(asid << 48) | vgrain`. The rejected branch — an independent page-table root **per tile** — stays rejected, because it requires the virtual-address partitioning #269 rejects outright. **The regenerated `MEMORY.md:2` and `nmfc_invariants.sh` must carry the per-address-space form**, not the bare "one page table". See **I3**, **F.5**, **F.5a** and ledger **L35**, **L49**.

**L3 — SPAWN: forbidden by the user, implemented in ChampSim.** **[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#181 2026-08-29T19:39):* "We already determined spawn decomposition is
  deadlock captive. ... It is not acceptable to be spawning two or more contexts."
- *Tier 2 (ChampSim):* `op::SPAWN = 7` exists with an approving rationale
  (`inc/nmfc/nmfc_trace.h:101-114`); `issue_spawn` is fully implemented with **no
  admission control beyond the fabric queue and no host FTU entry**
  (`src/nmfc/function_core.cc:870-901`); `spawned_`/`spawn_stalls_` are reported. There
  is **no `CONT`/extend mechanism in ChampSim at all**.
- *Mitigating:* `annotate.cc` never emits a SPAWN record and the producer's top-level
  switch has no `case` for it, so no current trace exercises it.
- **RULING: tier 1 wins — spawn is rejected (I10, R24).** The ChampSim mechanism is
  survival from a rejected shape. **For the user: delete `op::SPAWN` and its machinery
  from ChampSim, or keep it as a labelled dead branch?** The measurement it produced
  (0.0015 migrations/instruction) is worth keeping as a *target*, not as an endorsement.
- **RULED — user ruling 2026-09-02 R1: "delete it. CONT/extend is fine, and can stay."** `op::SPAWN` and `issue_spawn` are DELETED from ChampSim; `CONT`/extend stays. This is **one of exactly two ChampSim changes ordered before the freeze** (Appendix 2 D0). The 0.0015 migrations/instruction figure survives as a decomposition *target*; the mechanism does not. **CLOSED.**

**L4 — ChampSim's default router is the rejected design.** **[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1:* VA partitioning rejected (#269); routing is on the **physical** address
  after translation (I4, I12).
- *Tier 2:* `config/nmfc/nmfc_4tile.json` — the default shipped configuration — uses
  **`CONGRUENT_ROUTER`**, which is `VIRTUAL_FIRST` with N page-table roots and, in its
  own words, gives "the allocator **no placement freedom at all**".
  `PHYSICAL_ROUTER` / `NUCA_ROUTER` (`TRANSLATE_FIRST`, one root) exist and carry most of
  the tree. `[CORRECTION — an earlier revision said they were "used only in the sweep
  directories", which understates them and drove this row's proposed action on a false
  picture of how much would change.]` **Census of the `ROUTER` child across every JSON
  under `config/nmfc/`: PHYSICAL_ROUTER 15, NUCA_ROUTER 5, CONGRUENT_ROUTER 4,
  ADAPTIVE_ROUTER 2, and 7 files with no router child at all** (the stale group in L28).
  The four CONGRUENT files are `nmfc_4tile.json`, `baseline_4tile.json`,
  `cap/nmfc_4tile.json`, `cap/baseline_4tile.json`. **The ruling below is unchanged; the
  scope of the fix is four files, not most of the tree.**
- **RULING: tier 1 wins. The canon machine is physical partitioning, translate-then-route,
  one duplicated page table.** **For the user: should the default ChampSim config be
  switched to `PHYSICAL_ROUTER`/`NUCA_ROUTER`?** As it stands, anyone reading the default
  config is reading the rejected design — **in four files.**
  **On whether to DELETE `CONGRUENT_ROUTER`: F.10 says no, and the newest authority says
  it about this router by name.** *User #118, 2026-08-28T23:51:00Z:* "**We are testing
  multiple policies. There is a policy where vmem places tiles. That policy should be
  supported, but we don't want to use it.**" `CONGRUENT_ROUTER` **is** that policy.
  Reinforced by #30, #98 and #8, all older and all phrased generally. **Relabel it as a
  control and change the default** — both halves of #118's sentence, in order. Same
  reasoning for `--walk-routing fabric` (L26).
- *Also here:* `RELOCATION_ROUTER` is documented in `inc/nmfc/tile_router.h:20-26` and
  offered by `make_config.py:581` but **has no `register_module` anywhere** — selecting
  it yields an unconstructible configuration.
- **RULED — user ruling 2026-09-02 R2: "That is fine, relabel is fine. You are correct the defaults should be switched to the physical/nuca router."** Both halves of #118's sentence, in order: `CONGRUENT_ROUTER` is **relabelled a control and kept**, and the **default becomes `PHYSICAL_ROUTER`/`NUCA_ROUTER`** in the four files that select it. **The second of exactly two ChampSim changes ordered before the freeze** (Appendix 2 D0). **CLOSED.**

**L5 — LLC banking aligned to the DRAM banks: required, unbuilt in ChampSim.**
**[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1, four times:* #76 (2026-08-28T18:43), #144 (2026-08-29T05:22), #250
  (2026-09-01T17:38), #291 item 1 (2026-09-02T12:37).
- *Tier 2:* `--llc-banks` defaults to **1**; **no shipped config contains a
  `tile*_LLC_b*`.** The mechanism exists as a post-pass but is never exercised. Also:
  `bank_shift` silently falls back to **12** under `--dram default`, so banking without
  ramulator would route on the page-offset boundary rather than on device geometry.
- *Tier 4:* built in SST — slices × banks-per-slice equals the device's per-rank bank
  count, with a bank-balance plugin measuring it.
- **RULING: tier 1 wins — build it in ChampSim.** Rare case where SST leads.
  **For the user: is this still wanted on the ChampSim side, or is ChampSim now frozen
  as the parity reference?**
- **RULED — user ruling 2026-09-02 R3: "I was under the impression that was derived from invoking ramulator now, the `--llc-banks` should be inert. ChampSim updates stop until we deem it a good idea to go back."** Slice banking is **derived from the DRAM device geometry ramulator declares**, not from a flag; `--llc-banks` is **inert**; **ChampSim is FROZEN** and this is not built there. **Take every bank-conflict number from SST and say so.** See D.2, Appendix 2 D0. **CLOSED.**

**L6 — The function core's data cache: a supersession chain read backwards.**
- *Tier 1, older (#142 2026-08-29T05:18, #146 2026-08-29T06:04):* remove the data cache
  — explicitly conditional: "otherwise **we need to bank it as well**".
- *Tier 1, newer (#248 2026-09-01T17:31, #249 17:35, #250 17:38, **#291 item 6
  2026-09-02T12:37**):* a small L1 data cache is fine; separate I and D caches; **both
  heavily banked**; and the newest of all describes per-context fetch and data slots
  "**Each backed up by an I and D cache respectively? Then backed up by the LLC?**"
- **RULING: newest tier 1 wins. BOTH caches exist, separate and banked.** The removal
  ruling is superseded, and its own stated alternative — banking — is the path that was
  taken. A prior analysis of this record ruled the other way by treating the 2026-08-29
  statements as final; that is corrected here. See D.3.

**L7 — Migration's 72-byte cost is unimplemented in ChampSim.** **[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#89, #91, #291 item 7):* 72 bytes = 512-bit regfile + 8-byte PC, "and if you
  aren't modelling it, then you don't understand it."
- *Tier 2:* the migration payload is a whole context copied by value and delivered after
  `hop_latency` cycles (**8**, not the ~2 measured arrival). **There is no byte count,
  bandwidth budget, or serialisation model anywhere on the migration path** — grep for
  `byte` across `src/nmfc` finds only DRAM geometry and `grain_bytes`. Migration also
  generates no memory traffic at all, so "subsumes data traffic" is trivially true there
  rather than modelled.
- *Tier 4:* SST charges `MigrationEvent::SIZE_BYTES = NMFC_CTX_BYTES + 8 = 72` on both
  the departing and the arriving link.
- **RULING: tier 1 states a requirement ChampSim does not meet. It is UNIMPLEMENTED,
  not merely different. For the user: does ChampSim need the byte model retrofitted, or
  does SST now carry that claim?**
- **RULED — user ruling 2026-09-02 R4: "ChampSim doesn't have a byte model, just a cycles-to-transmit model. No need to back port it, SST is correct."** The byte model is **not** being retrofitted to ChampSim; **SST carries the claim**. ChampSim's migration costs are message-count and latency costs and are now a declared property of that model, not a defect. See I5, J.2, M.4. **CLOSED.**

**L8 — The grain formula's bank count: ranks in or out?**
- *Tier 1 (#55 2026-08-28T03:44, all-caps):* "**RANK BITS AND BANKS ARE MEANT TO BE
  INCLUDED**"; channel capacity is the addressable space on **all ranks** under a
  channel.
- *Tier 2:* agrees — `banks_per_channel` = product of ranks × bankgroups × banks
  (2 × 8 × 4 = 64), giving G = 4096 × 64 × 4 = **1 MiB** at four tiles.
- *Tier 3 (DESIGN §30.2 D:3435-3452):* says "the grain formula uses **32**, the per-rank
  count", while separately and correctly explaining that the **cache slice** is banked by
  32 because the rank bit is not a bank-select bit.
- *Tier 1, NEWER (#141 2026-08-29T05:16:38Z — 25.5 hours after #55):* "**We have only 32
  banks per channel right?** So it should be trivial to saturate the channel with 256
  contexts." **This is the newest tier-1 statement on the subject and the only one that
  names a unit. An earlier revision of this ledger row ruled on #55 alone and did not cite
  it.**
- **RULING, in two parts, because the two statements answer different questions.**
  **(a)** *Which fields count:* **tier 1 (#55) + tier 2 win — the grain formula uses the
  FULL per-channel bank count including ranks.** #141 does not disturb this; it supplies a
  count, not a rule.
  **(b)** *What that count is:* **it is per-file, and #141 is right about the device it
  describes.** `tile_ddr5.yaml:33` is `count: [1, 2, 8, 4, ...]` = 2 ranks × 8 bankgroups ×
  4 banks = **64 flat per channel, 32 per rank** — and the second rank is our own
  non-JEDEC choice (DESIGN §17 D:1389-1391), so on the JEDEC part and on any one-rank
  configuration #141's 32 is the flat per-channel figure. On the `--dram default`
  controller it is flat 32 per channel as written. Both implementations land on 1 MiB at
  four tiles on the files in the tree. See E.4a.
- **RESIDUAL, NOT RESOLVED BY THIS DOCUMENT:** if #141 meant a flat 32 for the machine we
  actually run, `G` is **512 KiB** and `grain_bits` **19**, not 20 — the same quantity
  L20/D.5 turn on. **Not put to the user.** See E.4a's caution and Appendix 1.
- The 32 belongs to the *slice banking* decision, not to G. See E.4, E.4a and E.5;
  DESIGN §30.2's wording should be fixed.
- **RULED — user ruling 2026-09-02, the GEOMETRY ruling: "Ranks are included. 32 banks per channel assumes DDR5 and one rank. 32 banks per channel is not part of the design or spec … WE MUST SUPPORT ALL POSSIBLE VALUES FOR EACH, WITHIN A FULL 48-bit PHYSICAL ADDRESS SPACE."** #55's positional rule is the design; #141's 32 was the device at one rank. **No bank/rank/row/column/channel count is canon.** See I12, E.3, E.4a. **CLOSED.**

**L9 — The 5.67× and the two core models.**
- *Tier 3 (I8, DESIGN §21.3):* 5.67×, measured on ChampSim.
- *Tier 1 (#239 2026-09-01T06:27):* the canon core has **one outstanding load** and
  sleeps on it.
- *Tier 2:* ChampSim's core keeps issuing past an outstanding load and gets
  intra-function MLP from an in-order scoreboard (`function_core.cc:10-15`).
- **RULING: not a contradiction — a genuine, deliberate difference between two core
  models. DO NOT RECONCILE THEM.** Every quotation of 5.67× must name the core model.
  See M.3.

**L10 — Which speedup number is "the" result: 5.67× or 6.19×?** *(Both carry the
core-model caveat wherever they are quoted — L9, M.3.)*
- 5.67× is against **GAPBS's reference algorithm** on a standard core, on identical work,
  with the ramulator-backed ChampSim machine. 6.19× is a cycle speedup measured against
  ChampSim's own DRAM model on kron-24, **and its 320×-fewer-migrations came from the
  now-rejected spawn shape.**
- **RULING: 5.67× is the headline; 6.19× is an internal comparison.** Neither is wrong;
  **quoting either without its provenance is.** The older memory note's "~5x speedup on a
  synthetic graph traversal" (4.98× scattered / 5.45× partitioned) is a Phase-3 synthetic
  record and is **not** the headline result.

**L11 — DESIGN.md's §0 has 14 invariants; several artefacts say otherwise.**
- `NMFC-Rev/README.md:47` says "all eleven"; `nmfc_invariants.sh` lists thirteen items
  that do not correspond to §0's numbering and **omits invariants 8, 12, 13 and 14** —
  i.e. omits physical partitioning, the one-fabric topology, and MOESI priority — while
  adding three non-§0 items.
- **RULING: fourteen. All fourteen are carried in Part B.** Anything reasoning from
  "eleven" or from the hook's thirteen is missing the invariants that were added last and
  therefore rank highest.

**L12 — DESIGN.md revision number and the artifact mirror.**
- `memory/nmfc-project.md:21` says "DESIGN.md (revision 3)" and points at an artifact
  mirror. `DESIGN.md:95` says "**Revision 6** ... Supersedes revisions 1–4."
- **RULING: revision 6 in the file. The artifact is a revision-3 snapshot — do not read
  the design from it.** Revision 5's status is never stated anywhere; recorded as an
  unexplained gap.

**L13 — Two sections are both numbered §0 in DESIGN.md, and §4's subsections are out of
file order.**
- `## 0. Invariants` at D:4 and `## 0. Does it do the thing?` at D:145; file order of
  §4's children is 4.1 → 4.3 → 4.1.1 → 4.2.
- **RULING: "§0" in any instruction means the INVARIANTS section.** Any citation of "§0"
  or "§4.1" without a line number is ambiguous. Recorded so a reader does not
  silently read the wrong section.
- **CLOSED IN EDITING — this was open item O2, and it was never a ruling to ask for.** It is a question about DESIGN.md's numbering, not about the machine: no answer changes what the machine is, and the row already contained its own answer. **`D:line` is and stays the reliable citation.** Renumbering DESIGN.md is an author's chore to do or drop, not a decision to put to the user, so it is off the STILL OPEN list.

**L14 — I14's priority: implemented, barely fires, and the measured table points the
wrong way.** **[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 3 measurements:* **one preemption in 33,317 requests**; three attempts at a
  contention phase produced none at all, because a 4 KiB host array sat in the L1 and a
  128 KiB one in the L2, and only a working set larger than the host's last private level
  generated fabric traffic concurrent with the function core's. The doc itself says it is
  "exercised rather than merely compiled, but **it is not yet a measured effect, and it
  should not be reported as one**."
- *And:* the one coherence table reports `nmfcPaysSnoop` **24** against `hostPaysSnoop`
  **9** — the *avoided* direction dominating the permitted one by 2.7×, **unremarked**.
- **RULING: the invariant stands (it is tier 1, #284). The evidence does not yet support
  reporting it as a measured effect, and the snoop-direction ratio is unexplained.
  For the user: is this a real violation of I14 or an artefact of a program whose static
  data arrives Modified (see L15)?**
- **RULED — user ruling 2026-09-02 R5: "That sounds like a bug. Needs investigation. The image being written into memory should itself be trivial, done by the OS at load. Amortized on any reasonable-length-running program. If it is impacting us in any way, that suggests a bug."** The 9-vs-24 ratio is a **suspected defect in the simulated environment** (the loader writing the image through the host cache — L15), **not** a counter-example to invariant 14. **I14 stands; investigation queued.** Do not quote the table as evidence either way until the loader path is fixed. **CLOSED as a design question; open as a BUG.**
- **[RESOLVED 2026-09-03] The bug was real, it was found and fixed, and it was NOT the explanation of the ratio.** Fixed in SST commit **`82357d0e7f92ac437ac45fce230efcde258139d4`** (`nmfc: the program image is placed by the loader, not executed by the core`), corrected by follow-up **`5e5fa669ce5041d0a366190f6cef61ebbdd7b400`**. **Root cause:** on Rev's memH path `RevLoader` executes one store per cache line of `.text`, `.rodata`, `.data` and the whole `.bss` zero-fill through the host's own L1/L2, queued during `RevCPU` construction, so the first 945 ns of every run is the loader — 40 of the first 45 cold host `GetX` on `tile_coh`/1 were the image itself. **Fix:** `NMFCHostMMU` performs the OS's half — it places every `PT_LOAD` (file bytes and zero-fill) on SST's untimed init path, translated, with a copy on every channel for a duplicate page, and retires `RevLoader`'s own writes of those same bytes; new counters `imageBytesPlaced` / `imageWritesRetired`. **No Rev source changed** (`src/rev` byte-identical) and **no architecture changed**; all three suites PASS.
- **Counters, before → after (`82357d0`):** `tile_bfs`/4 `nmfcPaysSnoop` **13 627 → 13 147** (−3.5 %), `hostPaysSnoop` **270 → 273**; `tile_bfs`/1 **12 962 → 12 572** (−3.0 %) and **271 → 273**; `tile_coh`/2 **22 → 20** and **9 → 9**. Simulated time: `tile_bfs`/4 1.633 22 → 1.609 79 ms (−1.43 %), `tile_bfs`/1 1.633 06 → 1.570 02 ms (−3.86 %), `tile_coh`/2 6.158 59 → 6.164 00 ms (**+0.09 %** — the loader had been pre-warming the host L2 with the whole image).
- **The ratio survived the fix: 50.5× → 48.2× on `tile_bfs`/4.** The follow-up `5e5fa66` then closed the counter defect that charged a clean `E` holder as a dirty one (the increment now happens after the state is known): `tile_bfs`/4 becomes `hostPaysSnoop` **62**, `nmfcPaysSnoop` **12 910**, with the clean cases split off into `hostPaysCleanSnoop` **211** / `nmfcPaysCleanSnoop` **237** (the old totals are the sums). **That makes the direction worse, not better — 208:1.**
- **What the measurement now says the ratio is.** (i) **Workload shape** — `tile_bfs`'s host builds the graph in `.bss` and the function cores consume it; 13 620 of 13 627 events land on `.bss`, 12 066 on `col` alone. (ii) **An ownership-policy defect**: the directory sets `d.global = O` and never clears `d.owner` (`NMFCCoherenceFabric.cc:603-609`), so the host L2 stays owner of record — 0 evictions and 0 writebacks on `tile_bfs`/4 — and 8 500 of 13 627 events find the directory already in `O`, 64 % of them repeat snoops of a line already snooped once. **That is a protocol decision, a C.5 matter, and it was deliberately left open** (R5: do not change architecture). (iii) The loader, now gone, was the **smallest** term. Ablation with no architecture changed — host L2 at 64 KiB, fc D-cache at 512 KiB — inverts the direction to **5 007 `hostPaysSnoop` against 1 405 `nmfcPaysSnoop`, 3.6:1 the permitted way**, which says the number is environment and policy, not the invariant.
- **STATUS: the bug half of this row is CLOSED. I14 stands, untouched. The instruction not to quote the snoop-direction table as evidence for or against I14 STANDS** — the loader path is fixed and the direction did not flip. Still open: the ownership policy above (C.5), and a workload in which the function core owns its working set.

**L15 — "A program's static data arrives Modified", which makes `F` unreachable early.**
- *Tier 3, measured:* the loader writes the whole image — text, rodata and bss — through
  the host's own cache, so **every static object is held dirty by the host before the
  program starts.** A function core's first read of any static data is therefore a
  downgrade from M, never a clean share, and `F` cannot occur on it at all. **The `O`
  state carries essentially all host-to-function traffic in a program that has not been
  running long.**
- **RULING: recorded as a real property of the simulated environment, not of the
  architecture.** It plausibly explains L14's ratio and should be checked against it
  before I14 is doubted.
- **RULED — user ruling 2026-09-02 R5, same statement.** "The image being written into memory should itself be trivial, done by the OS at load. Amortized on any reasonable-length-running program." **A model in which static data arrives Modified and stays costly is a BUG**, and it is the suspected cause of L14's ratio. **Investigation queued.**
- **[RESOLVED 2026-09-03] Confirmed at source and fixed in SST commit `82357d0e7f92ac437ac45fce230efcde258139d4`** (follow-up `5e5fa669ce5041d0a366190f6cef61ebbdd7b400`). The mechanism was exactly as recorded: `RevLoader.cc:379` writes each `PT_LOAD`'s file bytes and `:381-382` the `p_memsz - p_filesz` zero-fill through `RevMem`'s memH path (`RevMem.cc:528-530`), one cache line at a time. `NMFCHostMMU` now places those bytes untimed before the clock starts, so **the image no longer arrives in a host cache at all**.
- **Before → after:** leading cold host `GetX` on `tile_coh`/1 **45 → 4**, and the first non-loader record moves from t = 1 265 000 ps to t = 257 000 ps. The 4 that remain are the ELF program headers and `argv` `RevLoader` puts on the stack — process setup a real OS also performs through its own cache — plus 5 later `GetX` that are the program's own stores. Directory on `tile_coh`/2: `reqGetX` **99 → 30**, host L2 writebacks **80 → 11**, evictions **95 → 53**. `imageWritesRetired` equals the loader's line count exactly every time (40 on `tile_coh`, 6 697 on `tile_bfs`).
- **What did NOT change: `F` did not become more reachable.** `fwdFromF` is **8 → 8** on `tile_coh`/2 and **219 → 212** on `tile_bfs`/4; `fwdFromO` **13 → 11** and **12 642 → 12 448**. `O` still carries essentially all host-to-function traffic, for the reasons now recorded in L14 — the workload shape and the directory never releasing `d.owner`, not the loader.
- **Two consequences recorded rather than acted on.** (i) `tile_coh.c:29-41` points its `F` phase at `__heap_start` explicitly to reach around this bug; that comment is now false and the workaround unnecessary, but it is left in place so the suite's `fwdFromF` check measures the same thing before and after. (ii) The loader's duplicate-page stores were the tree's **only** run-time exercise of invariant 3's write path (`duplicateWrites`/`duplicateCopies` 34/34 on `tile_coh`/2 before, 0/0 after); `5e5fa66` restores that coverage with `tile_bfs_dup`, whose host builds a duplicate page at run time — **32 768 / 32 768**, gated by `run_coherent.sh`.
- **STATUS: CLOSED as a bug.** The property this row recorded is no longer true of the simulated environment.

**L16 — `NUCA_ROUTER`'s header describes a classifier the code does not contain.**
- *Tier 2:* the file's comment describes an R-NUCA PRIVATE/SHARED classification with
  "shared grains are never co-located". **The code has no classifier**;
  `private_threshold_`, `imbalance_threshold_` and `cooldown_cap_` are read from config
  and **never referenced again**; `imbalance()` is fully implemented and **never called**;
  `grain_state::{last_best, moves, cooldown}` are never read or written, so there is **no
  hysteresis and no cooldown**; and seven reported statistics are never incremented and
  print as constant zeros.
- **RULING: what is BUILT is component-based co-location gated by size, evidence and a
  windowed balance term. The safety property the header claims is enforced by no line of
  code** — the size budget is a *proxy* for it. Record as a known gap between the policy
  described and the policy running; the canon's statement of the policy is G.4, which is
  the intent.

**L17 — `NMFC_MMU` discards the page-fault penalty.**
- *Tier 2:* the walk path does `(void)penalty;` and retires with a zero fault duration;
  the function core's oracle path also discards it. So the configured 50 ns minor-fault
  penalty is charged **only** on the host walker path.
- **RULING: an unmodelled cost, recorded. It flatters the NMFC side of any measurement
  that faults.** Not a design decision; a modelling gap.

**L18 — The ChampSim allocator sizes its pool from ONE tile's controller.**
- *Tier 2:* `NMFC_VMEM` takes `dram: "@tile0_DRAM"` and sizes its whole grain pool from
  that one controller, then partitions those grains across all N tiles. With four tiles
  the machine models 64 GiB of DRAM and the allocator can hand out at most a quarter of
  it.
- **RULING: not a correctness bug — every frame still lands in a real channel — but
  "available physical memory" is N× smaller than the modelled device.** Recorded so a
  capacity experiment is not designed against the wrong number.

**L19 — Trace-format features that are BUILT but NEVER GENERATED.**
- *Tier 2:* the simulator implements `FLAG_NO_RETURN`, `FLAG_SPAWNED`,
  `FLAG_TAKEN_TARGET`, `op::SPAWN`, the MUL/DIV/FP op classes, and `header.max_outstanding`
  — **and `annotate.cc` emits none of them.** Consequences, stated plainly: every body
  instruction is ALU/LOAD/STORE/BRANCH at 1-cycle latency; every CALL carries
  `FLAG_DEFERRED_JOIN` and nothing else; **no taken-branch bubble is ever charged**, so
  the mechanism that exists precisely so "a replayed dynamic trace does not silently hand
  the function core perfect branch prediction for free" **is currently inactive**; and the
  `max_outstanding` deadlock guard is a no-op because the field is always 0.
- **RULING: recorded as a measurement caveat on every current ChampSim number.** The
  fetch-bubble gap is the one that matters most.

**L20 — grain 21 is derivable from NO DEVICE IN THE TREE, and 31 of 33 configs declare
it.**
- `[CORRECTION — this row previously read "both numbers are live in the tree
  simultaneously", treating 20 and 21 as symmetric alternatives. They are not: **20 is
  what the default controller and ramulator DDR5 both require — HBM3 requires 18 — and 21
  is what NOTHING in the tree produces.**]`
- *Tier 2, the arithmetic, from the generator's own function:* the **default** memory
  controller is hardcoded at `make_config.py:171-172` and emitted verbatim into
  `nmfc_4tile.json` — `channel_width 8` B, `columns 1024`, `ranks 1`, `bankgroups 8`,
  `banks 4`, `channels 1`. `derive_geometry` (`make_config.py:528-536`) then gives
  `row_bytes = 1024 × 8 = 8192`; `banks_per_channel = 1 × 8 × 4 = 32`;
  `total_channels = 1 × 4 = 4`; **G = 8192 × 32 × 4 = 1 MiB → grain_bits 20.** The
  ramulator device agrees by a different route (`ramulator/tile_ddr5.yaml:33,36`,
  `count: [1,2,8,4,65536,1024]`, `channel_width: 32` bits → `4096 × 64 × 4` = 1 MiB →
  **20**). **21 has exactly one source: `make_config.py:619-620` hardcodes
  `args.grain_bits = 21` under `--dram default`, with no check.**
- *Census:* **31 of the 33 checked-in configs carry 21; only the two under `ram/` carry
  20.** `annotate` defaults to 20, i.e. to the correct value.
- [CORRECTED — this row previously said the `--dram default` files "**run silently on a
  grain twice the device's**". **THEY DO NOT RUN AT ALL.** A *second*, trace-side geometry
  contract aborts on exactly this mismatch — the same mechanism I.10 describes correctly
  ("the header's geometry fields are a contract validated against the running
  configuration, **aborting on mismatch**"). The row asserted a silent-wrong-answer failure
  where the tree has a loud one, and buried the failure that IS silent.]
- *Tier 2, the second contract:* `annotate.cc:351` stamps
  `hdr.interleave_shift = opt.grain_bits`, whose default is **20** (`annotate.cc:38`).
  `nmfc_producer.cc:144-146` checks it: `if (header.interleave_shift !=
  map_.grain_bits()) { fail("interleave shift", header.interleave_shift,
  map_.grain_bits()); }` — and `fail` (`:128-133`) prints "*The placement pass targeted a
  different machine; **results would be meaningless***" and calls `std::exit(-1)`. **So a
  default-annotated trace against any of the 31 configs declaring `nmfc_grain_bits: 21`
  exits at trace open.** The function's own header comment: "*The geometry contract. **A
  silent mismatch here would invalidate every result.***"
- **RULING: 20 is correct for the default controller and ramulator DDR5 (HBM3 derives 18); 21 is a hardcoded default that
  contradicts them and is nonetheless the tree-wide value.** **BOTH geometry contracts
  catch it, and neither is bypassable** — the ramulator-backed file refuses at construction
  (L28a), and the `--dram default` files refuse at trace open. **The 31 `--dram default`
  configs are therefore not "silently wrong"; they are UNRUNNABLE against a default-built
  trace.**
- **THE FAILURE THAT IS ACTUALLY SILENT is the only way around them:**
  `annotate --grain-bits 21`. That satisfies both contracts by **laying the workload out
  at 2 MiB on a device whose grain is 1 MiB** — two page types per grain, the exact fault
  DESIGN §29.3 records catching once — and it runs to completion. **That is the failure
  this row must describe: not a silent run on a wrong grain, but a silent LAYOUT on a
  wrong grain, reached only by overriding the annotator.** And per D.5/finding on
  `bfs_nmfc.cc`, the checked-in workload cannot honour that override anyway: its grain is
  a **compile-time constant of 20** for the output pool and both frontier bitmaps, and
  `--grain-bits` reaches only the `parent` array (see D.5). **Derive the
  grain from the device (E.3) and pass it explicitly; never rely on either default, and
  fix `make_config.py:619-620` to derive it rather than assume 21.** Related: two `regions.txt` files exist and disagree; the root
  one is stale (4 regions, predating the byte-frontier work) and the `tools/nmfc/kernels/`
  one (7 regions) is current.
- **PARTLY RULED.** The *value* is configuration (user ruling 2026-09-02 R6–R10) and now lives in **SELECTED CONFIGURATION**; the *defect* — 31 of 33 configs declaring a `grain_bits` no device in the tree derives — is a ChampSim fix and is **frozen** under R3. The design statement is the GEOMETRY ruling: `G` adapts to the device and no count is locked (I12).

**L21 — A live statistics bug: `DISPATCH BLOCKED` always reports zero.**
- *Tier 2:* `function_fabric.cc:167-174` compares `dispatch_stalls_` against a snapshot
  taken immediately before, with no delivery in between, so `dispatch_blocked_cycles_`
  can never increment.
- **RULING: that statistic must not be quoted as evidence.** The retry count on the same
  line is real, and is exactly the quantity the code's own comment warns "compares to
  nothing" — it is how "207M dispatch stalls" once pointed at a queue that did not matter.

**L22 — Stores are not tracked to completion in the ChampSim function core.**
- *Tier 2:* stores are fire-and-forget — they never raise the pending-memory counter — so
  the end-of-body drain that waits on outstanding memory **cannot actually wait for
  them**, despite its comment saying it does.
- *Canon (H.6):* an invocation cannot retire until its stores have landed.
- **RULING: canon states the requirement; ChampSim does not enforce it. Recorded as a
  gap.**

**L23 — The ChampSim NMFC unit-test suite: THE FILES ARE PRESENT. The earlier premise
was false.**
- *Tier 3:* DESIGN §13 claims a green suite at 753 cases / 412,486 assertions.
- *Tier 2, re-checked:* **all eight files exist.** `ls test/cpp/src/*nmfc*` returns
  `550-nmfc-tile-map.cc` (185 lines, 10 `TEST_CASE`s), `551-nmfc-function-core.cc`
  (830, 16), `552-nmfc-vmem.cc` (498, 18), `553-nmfc-routers.cc` (104, 4),
  `554-nmfc-fabric.cc` (309, 6), `555-nmfc-producer.cc` (205, 5), `556-nmfc-ports.cc`
  (198, 4), `557-nmfc-mmu.cc` (219, 4) — **2,548 lines and 67 `TEST_CASE`s.** Build
  artefacts exist too (`.csconfig/test/550-nmfc-tile-map.o`,
  `.csconfig/test/553-nmfc-routers.d`).
- **RULING: `[CORRECTED]` An earlier revision of this ledger recorded the suite as
  "claimed, and not present" on the strength of an audit that did not find the files.
  That is wrong and it is corrected here.** What remains unverified is the **assertion
  count**: 67 `TEST_CASE`s is not 753 cases, and 412,486 assertions has not been
  reproduced. **Do not re-quote the assertion count without re-running the suite; do not
  repeat the claim that the tests are missing.** The user's requirement (#44, #66) — full
  functional coverage of every newly-authored module, with lcov — stands regardless, and
  67 cases across eight modules is a starting point, not coverage.

**L24 — `.bss` is touched in full at load (SST), bounding workload size.**
- *Tier 3/4:* startup cost is proportional to a program's static footprint before `main`
  runs.
- **RULING: a simulator artefact, not a machine property. It bounds how large a workload
  can be run rather than distorting one that does run.** Recorded because it will look
  like a machine property in a scaling study.

**L25 — ONE FABRIC, and ChampSim ships TWO. [BLOCKING] [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#284 2026-09-02T02:32, all-caps):* "**THE ONLY THING WE ARE ALTERING OVER A
  REGULAR MEMORY SYSTEM IS THAT WE DO THE ADDRESS PARTITION AT THE FABRIC** ... **The vast
  majority of traffic through the fabric is either coherence, migration, or LLC/DRAM
  access.**" *And #227 (2026-09-01T04:41):* migration is to be built "**preferably as a
  generic fabric packet, not it's own channel**". I13, R50.
- *Tier 2 (ChampSim):* `config/nmfc/nmfc_4tile.json` declares **two separate
  interconnects**. `cpu0_fabric` — `INTERLEAVE_FABRIC`, `hop_latency` **4**, `queue_size`
  64, `max_forward` 4, over `[@fabric_tile0_channel … @fabric_tile3_channel]` — carries
  **only host L2C misses to the LLC slices** (`src/nmfc/interleave_fabric.cc:2-8`: "One
  instance sits below each compute tile's last private cache"). `fn_fabric` —
  `FUNCTION_FABRIC`, `hop_latency` **8**, `queue_size` 128, `max_deliver` 4 — carries
  invocations, migrations and completions on its own per-destination deques
  (`src/nmfc/function_fabric.cc:484-497`). **They share no queue, no bandwidth budget and
  no latency. A migration never touches `cpu0_fabric`.**
- **RULING: tier 1 wins — there is ONE fabric.** This is not a modelling nicety.
  **J.2's parity/subsumption claim (I11) is only true if migration and data contend for
  the SAME interconnect, so on ChampSim as shipped that claim is not measurable at all.**
  Compounded by L7: ChampSim charges no bytes on the migration path either, so
  "subsumption" is trivially true there rather than modelled. **It is also exactly the
  rejected alternative R73c** — "leave the host on `tilebus` and let the fabric carry NMFC
  traffic only ... the one §24 step 3 forbids by name", which also leaves invariant 14
  unbuildable. **For the user: merge the two fabrics in ChampSim (one queue set, one
  bandwidth budget, one hop model), or accept that I11 is measured only on SST and say so
  wherever the claim appears?**
- **RULED by the freeze — user ruling 2026-09-02 R3 ("ChampSim updates stop") and R4 ("SST is correct").** Merging ChampSim's two fabrics is **not** happening. **Invariant 11's SUBSUMPTION is measured on SST**, which has one fabric and a byte cost; **PARITY comes from the §24 step-0 baseline (M.3)**. ChampSim's two-fabric topology is a declared property of that model. See J.2, M.4. **CLOSED.**

**L26 — WALKS MUST BE LOCAL, and ChampSim ships `--walk-routing fabric` — which every
single-root config selects. [BLOCKING] [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#283 2026-09-02T02:18):* "**translations must NOT be foreign** ... Translation
  must be possible **LOCALLY, on the same memory TILE. No crossing of the fabric, no
  migration.**" *And #291:* "**Any migration due to instruction fetch or translation is by
  construction wrong.**" I3, F.6, R4, R5.
- *Tier 1, SUPERSEDED (#33 2026-08-27T21:26:37Z):* "**the nmfcs are right next to the
  fabric, so it is clear that if they need to walk somewhere else, they already have a
  fabric that can serve that request.** Please proceed with the implementation."
  **This is where the mechanism came from — the code was authorised, not invented** — and
  an earlier revision of this ledger row omitted it entirely, quoting
  `physical_router.cc:20-23` as if the phrasing began in the code. **#283/#269/#291 are six
  days newer and retire it (#307).** Recorded so that re-finding #33 does not reopen a
  closed decision. F.6 carries it in full.
- *Tier 2 (ChampSim):* `config/nmfc/make_config.py:578-579` offers
  `--walk-routing {local,fabric}` — "page-table walks stay on the tile (partitioned table)
  or **route over the fabric (shared table)**". `src/nmfc/physical_router.cc:20-23`: "The
  page table collapses to one root ... **Walk references therefore go wherever the PTE
  lives, over the memory network the tiles already sit on (`--walk-routing fabric`).**"
  **The 15 checked-in configs that carry one of those routers AND a per-tile MMU**
  (`phys/`, `phys_ft/` ×8, `nuca/`, `nuca_ft/` ×2, `adapt/`, `ram/`, `wide/` — `grep -l
  mmu_fabric`, F.6's census) instantiate a per-MMU `INTERLEAVE_FABRIC`; the other 7
  router-carrying files are the `baseline_*` configs, which have no MMU. [CORRECTED — was
  "13 files", which is the `first_touch` census, not this one.] `phys_ft/nmfc_4tile.json`'s `tile0_mmu`
  comment reads "Walks route over the fabric, because a shared page table puts a PTE on
  whichever tile its own address names."
- *Two aggravating facts:* **(a)** under `fabric`, `make_config.py:265-267` skips the
  `("mmu", …)` `TILE_PORT` (`if args.mmu and args.walk_routing == "local"`), **so
  `strict_locality` never checks a walk reference** — the assertion is absent in exactly
  the configurations that make foreign walks possible, which is the I9 failure shape.
  **(b)** **No shipped config combines a one-root router with `--walk-routing local`**, so
  `take_pt_replica_page()` (`nmfc_vmem.cc:438-460`) — the duplicated-replica mechanism
  F.6 cites as ChampSim's implementation — **is never exercised in any checked-in
  configuration.**
- **RULING: tier 1 wins. Walks are local, over a duplicated table on duplicate pages.**
  **For the user: make `--walk-routing local` the only shipped setting for one-root
  routers and install the `mmu` TILE_PORT unconditionally — or keep `fabric` as a
  LABELLED CONTROL (F.10, on #118's "that policy should be supported, but we don't want to
  use it") that no default selects?** Either way the locality assertion
  must be installed in both modes, or the invariant is unchecked where it can be broken.
- *And a downstream consequence recorded separately as **L37**:* because the assertion is
  absent in these **15** configs, **the "measured pass" on #291's migration ceiling tests only
  its count clause**, never its legitimacy clause — and the record does not say which mode
  produced that run.
- **RULED by the freeze — user ruling 2026-09-02 R3.** `--walk-routing fabric` stays as a **labelled control** (never deleted — #118, and R2's "relabel is fine" is the same instruction applied to the router); **changing the default and installing the MMU `TILE_PORT` are ChampSim changes and are frozen.** The design requirement is unchanged and unambiguous: **walks must remain local** (I3, F.6). Status: **frozen, not abandoned.**

**L27 — The shipped headline configs are STALE relative to their own generator.**
- *Tier 2:* re-running `python3 config/nmfc/make_config.py` produces
  `tile0_mode_port`…`tile3_mode_port` (`DRAM_MODE_PORT`) with each slice's `lower_level`
  pointing at `@tileN_mode_port`. **The checked-in `config/nmfc/nmfc_4tile.json` has ZERO
  occurrences of `mode_port`** and wires `tile0_LLC.lower_level =
  @tile0_LLC_DRAM_channel`. `make_config.py:108-123` adds the port unconditionally when
  NMFC is enabled, **so the shipped file cannot be reproduced by the current generator.**
  Same defect in `config/nmfc/nmfc_4tile_ramulator.json`.
  [CORRECTION — this row previously read "The newer sweep directories (`phys_ft/`,
  `ram/`) do carry the four mode ports", which exempted two directories and framed the
  staleness as a two-file problem. `grep -c mode_port` over **all 33** checked-in configs
  returns **8 for exactly two files** — `phys_ft/nmfc_4tile.json` and
  `ram/nmfc_4tile_ramulator.json` — **and 0 for the other 31**, including **9 of the 11
  files in the two directories this row exempted** (`phys_ft/{baseline_4tile,bw4,bw8,
  ftu2048,ftu4096,q128,q512,q2048}.json`, `ram/baseline_4tile_ramulator.json`) and all of
  `phys/`, `nuca/`, `nuca_ft/`, `adapt/`, `cap/`, `ft/`, `wide/`, `mem_network_smoke.json`
  and the three top-level files. **The defect is 31 of 33, not 2 of 33.**]
- *A second, independent unreproducibility in the same headline file:* `make_config.py:389`
  sizes the tracking unit as `ftu_size = args.ftu_size if args.ftu_size > 0 else
  args.tiles * args.contexts`, so `--tiles 4 --contexts 1024` emits **`ftu_size 4096`**.
  The checked-in file carries **1024** on both `cpu0` and `cpu0_trace` against
  `num_contexts 1024` on each of four tiles — **a 4:1 cap, which is exactly the pathology
  `make_config.py:566-570` warns about in its own words** ("*Left at a small value it
  silently caps everything ... every occupancy number it reports describes the unit rather
  than the architecture*"). See D.5 and H.9a.
- **RULING: the shipped file does NOT strip the mode bit at the DRAM boundary, which E.2
  says is wrong two different ways** (ChampSim's stock mapping drops the high bit and
  collapses an NMFC and a STANDARD address onto the same row and bank; ramulator2 decodes
  against a device with no such row). **Regenerate before using either file. Do not read
  D.5's table as the generator's output.**

**L28 — Three more stale-config defects, and a capacity mismatch that breaks Part L's
reproducibility.** `[The three defects are (a), (b) and (c) below; (b) covers SIX files,
not three — see its correction.]`
- *Tier 2, (a) an unstartable config:* `config/nmfc/nmfc_4tile_ramulator.json` declares
  `nmfc_grain_bits: 21` (2 MiB) while its `tile0_DRAM` is `RAMULATOR_MC` on a DDR5 device
  whose derived grain is **1 MiB**. `src/nmfc/ramulator_mc.cc:280-287` prints
  "nmfc_grain_bits is 21 (2097152 bytes) but this DRAM requires 1048576" and calls
  `std::exit(-1)`. **Only `config/nmfc/ram/nmfc_4tile_ramulator.json` carries the correct
  `nmfc_grain_bits: 20`.** The refusal is the geometry contract working (E.3); the file is
  simply stale.
- *(b) unresolvable references — **SIX files, not three**:* the files that instantiate
  `NMFC_VMEM` and contain **no child with `module: tile_router` at all** are
  `nmfc_4tile_ramulator.json`, **`baseline_4tile_ramulator.json`**, `nmfc_4tile_oracle.json`,
  **`baseline_4tile_oracle.json`**, **`ft/baseline_4tile.json`** and `ft/nmfc_4tile.json`.
  In each, the `@ROUTER` reference taken by `NMFC_VMEM`, `FUNCTION_FABRIC` and
  `FUNCTION_CORE` cannot resolve. (These are **6 of the 7** router-less files counted in
  L4; the seventh is `mem_network_smoke.json`, which contains no `NMFC_VMEM` and so takes no
  `@ROUTER`.)
  [CORRECTED — this row named three files and called them "3 of the 7", and the
  document's own arithmetic already implied six: **33 configs; 32 contain `NMFC_VMEM`**
  (I9: "All 32 configs that instantiate `NMFC_VMEM`"); **26 contain `tile_router`** (L4's
  census, 15 + 5 + 4 + 2 = 26); 32 − 26 = **6**. The three omitted were the `baseline_*`
  files — omitted, it appears, because the row was written from the `nmfc_*` names. The
  `baseline_*` configs take `@ROUTER` too, so the defect is theirs as well. This document
  orders exactly this audit ("Assume a config does NOT strip the mode bit unless you have
  grepped it") and has now twice corrected itself for a census being wrong; the census
  command is
  `for f in $(find config/nmfc -name '*.json'); do grep -q NMFC_VMEM $f && ! grep -q tile_router $f && echo $f; done`.]
- *(c) the LLC capacity mismatch — and it is NOT a "mismatch", it is a MISSING
  CONFIGURATION:* **D.5's table records a 512 KiB slice / 2 MiB aggregate. Part L's §29
  inversion and N.1's stress workload were both measured at 4 MiB slices / 16 MiB
  aggregate** — L's mechanism is "replicating an 8 MiB array onto four **4 MiB** slices
  ... aggregate ... fell from about 16 MiB to about 4 MiB", and N.1's stress set is
  "16 MiB of data against four **4 MiB** slices".
  [CORRECTION — this row previously framed the 4 MiB configuration as something D.5's
  table *diverges from*, which implies it exists elsewhere in ChampSim. **It does not
  exist anywhere in the tree.** Every one of the 33 configs has `tile0_LLC num_sets 512,
  num_ways 16` — 512 KiB per slice, 2 MiB aggregate — **including `cap/`, the only
  directory whose name matches D.4's capacity study**. `make_config.py:576` defaults
  `--llc-sets` to 2048 and `:240` divides by the tile count; a 4 MiB slice at four tiles
  needs `--llc-sets 16384`, which nothing in the tree passes.]
- **RULING: recorded, and it is stronger than "a reader cannot reproduce it from D.5" —
  Part L's mechanism and N.1's stress set CANNOT BE REPRODUCED FROM ANY CHECKED-IN FILE.**
  They were run against a configuration that was never committed. **That compounds L34
  directly** (three unreconciled baseline migration counts, none of which names a config
  that exists) **and it bears on Part M's parity caveat**, since a cross-simulator
  comparison needs a reproducible reference. **Every capacity claim must name its slice
  size, and Part L's must additionally name a configuration that no longer exists.**
  **For the user: was the 4 MiB-slice configuration ever committed, and can it be
  recovered — or must Part L be re-measured?** D.5 now carries the warning.
- **PARTLY CLOSED BY LOOKUP.** The two-command check L28(c) named was run for this revision: `git log --all -S'"num_sets": 4096' -- config/nmfc` returns **nothing**, so **the 4 MiB LLC slice Part L and N.1 were measured at was NEVER COMMITTED under `config/nmfc`** and cannot be recovered from there. Re-measuring is blocked by the freeze (R3). What is left is the classification question — **item O15**, and it is now narrowed to *confirm historical, or lift the freeze*, since "recover and commit" is ruled out by this lookup and by R3 together.

**L29 — Fire-and-forget: tier 1 says an ACK closes the entry; ChampSim frees it at
DISPATCH and sends no completion at all. [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#221 2026-09-01T04:08):* "I think it is probably necessary for **even the FIRE
  + FORGETs to allocate FUT entries as well, which means we must close them out on return
  (an ending context returns an ACK).**" I.2, I.4.
- *Tier 2 (ChampSim):* `src/nmfc/nmfc_host_core.cc:1332-1340` — "Fire and forget: nothing
  downstream consumes a result, so the tracking slot frees now rather than on a return
  that will never come" — sets `entry->returned = true; entry->join_seen = true;` and
  calls `retire_if_done(idx)` **immediately after `fabric_->dispatch`**. Restated in
  `inc/nmfc/nmfc_host_core.h:483-485` ("**immediately at dispatch for a fire-and-forget
  call, which is why those cost the host no tracking slot**") and
  `inc/nmfc/nmfc_trace.h:161-165`. On the tile,
  `src/nmfc/function_core.cc:1191` guards the completion with `if (!ctx.body->no_return())`
  — **a fire-and-forget invocation emits no completion message whatsoever.**
- **RULING: tier 1 wins.** Two things depend on it: **(a)** `FORKQ` earns its encoding
  *precisely because* a `FORKF` entry's release is asynchronous and not derivable from the
  instruction stream (I.2) — free-at-dispatch would make it derivable; **(b)** under
  ChampSim's rule fire-and-forget invocations are invisible to FTU occupancy, so the
  machine's in-flight count is understated by exactly that traffic, which matters directly
  to H.9/N.4. **For the user: retrofit the ACK path in ChampSim, or accept the
  understatement and annotate every FTU occupancy number taken with fire-and-forget
  traffic present?**
- **RULED by the freeze — user ruling 2026-09-02 R3.** Retrofitting the ACK path is a ChampSim change and is not available. **Option (b) is operative: keep free-at-dispatch and mark every FTU number as excluding fire-and-forget traffic.** The canon is unchanged — tier 1 requires an ACK to close the entry (I.4) — and SST is where that is built. Status: **frozen.**

**L30 — Admission: the canon tests BITS, both implementations test something else.**
- *Tier 1 (#232 2026-09-01T05:44):* "The regfile for each context is 512 bits ... **It
  could be 16 4-byte regs, 64 1-byte regs, or ANY combination. Bit-packing is the name of
  the game.**" *And #99:* a register never read "**doesn't count**". K.6, R30.
- *Tier 2 (ChampSim):* `tools/nmfc/annotate.cc:524-527` builds a pool of `opt.num_regs`
  (**8**) slot ids; `:542-553` allocates **one whole slot per live value** and `die()`s
  when the pool empties ("holds more than 8 values live at once"). The bit accounting at
  `:555-559` feeds `peak_bits` (`:570`) whose **only** consumer is a stderr line at
  `:927`. **`peak_bits` gates nothing.**
- *Tier 4 (SST):* the opposite failure — divergence S5, nothing produces a non-default
  layout, so the bits-used figure is **always 512** and the bit test is never exercised
  either.
- **RULING: tier 1 wins — the test is `peak_bits <= 512`.** Concretely, the implemented
  test **rejects** a function holding twelve live 32-bit values (384 bits — admissible)
  and rejects a nine-value 520-bit function **for the wrong reason**. **Neither
  implementation currently runs the canon's admission test.** Replace the slot-count
  `die()` with a comparison of `peak_bits` against 512.

**L31 — The mechanisms must be RUNTIME-SWAPPABLE and both evaluated, and this document
had lost the requirement entirely.**
- *Tier 1, FOUR times, and the newest one names the policy:* **#118
  (2026-08-28T23:51:00Z), which is NEWER than the other three and is the specific
  authority this ledger row previously lacked** — "**We are testing multiple policies.
  There is a policy where vmem places tiles. That policy should be supported, but we don't
  want to use it.**" (`grep` for "should be supported" and for "vmem places tiles" over an
  earlier revision of this document returned **zero**; #118 was cited four times elsewhere
  and this clause was never quoted.) Then, older: #30 (2026-08-27T19:12) "**Can we not
  have these policies be swappable? I feel like we need to evaluate both systems** ...
  **we need to be able to runtime-config swap these policies and evaluate each.**"; #8
  (2026-08-27T06:19, AskUserQuestion) all three translation mechanisms built "**behind a
  swappable `translation_engine` interface**"; #98 (2026-08-28T21:12) "**please don't let
  the swappable translation system go out of scope.**"
- *`[AUTHORITY NOTE on #8, because half of it is superseded]`:* #8's *swappable interface*
  requirement stands. **#8's other clause — the per-context translation cache "In the
  context, migrates with it" — is REVERSED by #9 fourteen minutes later** ("*The
  translation data is useless if migrated, so it shouldn't be migrated*") and by F.7/R14.
  Cite #8 for swappability only.
- *Status:* **no statement of this requirement appeared anywhere in this document** —
  `grep -i swappable` returned nothing — and **ledger L4 proposed DELETING the alternative
  router**, which would remove exactly the comparison the user asked for.
- **RULING: the requirement stands, and #118 states it in so many words about the exact
  branch in question.** #265 and #269 reject one branch **architecturally**; that is a
  statement about which design the machine *is*, not a licence to delete the control.
  **Rejected branches remain buildable and runtime-selectable as labelled controls, so the
  rejection stays measured rather than asserted; what must never happen is a rejected
  branch being the DEFAULT** — which is #118's own two halves, "that policy should be
  supported" and "but we don't want to use it", in order. Recorded as **F.10**, and it
  revises L4's and L26's proposed actions from "delete" to "relabel and change the
  default".

**L32 — The stress workload: doubted, diagnosed, reshaped, and now PASSING.
[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#303 2026-09-02T23:28:06Z — the second-newest substantive item in the log, four
  minutes before the canonization order):* "**you seem to have learned nothing about layout
  from our ChampSim runs. We literally dealt with most of these issues already, and you
  are grinding for hours trying to reinvent them. Unacceptable. Is the stress test working
  or not? It seems like a failure.** You didn't say anything about it's status or what you
  were actually doing. **There is clearly a massive load-balancing problem, likely caused
  by your continued regression in understanding of the system.**"
- *Tier 3, and presented as settled:* H.9's occupancy table, N.4's context sweeps and the
  11.3048 ms / 136.143 ms invariance are all taken from this workload. **Its own tile
  column — 5.37 / 13.27 / 12.57 / 5.61 of 128 — is a 2.5× spread, which is the imbalance
  he is describing.**
- **RULING AS IT STOOD BEFORE 2026-09-02 (superseded, kept as the record): tier 1's newest
  word made every stress-run number provisional and the defect open.** Diagnose it with
  #151's rule — annotation problem or runtime problem (G.6). **That diagnosis is what
  produced the answer**: it was the annotation half, i.e. the workload's shape. Recorded in
  G.6, H.9, N.1 and N.4.
- **RULED — user ruling 2026-09-02 on the stress workload: it WORKS.** The sum is verified against the host; loads, migrations and instructions are **25.0% on every tile**; zero stores; **196,904 migrations for 262,143 loads**. The original 2.5:1 spread came from the **rejected chase shape** (own an index range, chase data) and was **predicted exactly from the addresses — 12.5 / 37.5 / 37.5 / 12.5 — i.e. congruent routing doing its job on a workload shaped wrong**. Reshaped (host resolves seven indices into the 512-bit context; the function does seven data loads and returns the sum via **END with the return bit**; the `FORK.R`/`JOIN` ring is the size of the FTU) it is even. **"It seems like a failure" was the trigger for the reshape, not a ruling that the machine is broken.** The invariance and occupancy numbers are **settled**, quoted with their build and binary (working tree at HEAD `21df518d`, `tools/nmfc/kernels/bfs_nmfc` and `bfs_base`, uncommitted). See G.6, H.9, N.4. **CLOSED.**

**L33 — "The FTU is the concurrency of the whole machine": a tier-3 inference that two
tier-1 statements refuse.**
- *Tier 3 (DESIGN §31.3, quoted in H.9):* the FTU at 63.61/64 while tiles sit at 4–10%,
  and identical results across an 8× context sweep.
- *Tier 1 (#180 2026-08-29T19:32):* "**Despite the FTU being full, most of it is still
  pending results. A larger FTU doesn't fix that problem. The work is being done, the
  standard core is not consuming it fast enough. That is not a problem with the functions,
  it is how the functions are being positioned in the code.**"
- *Tier 1 (#171 2026-08-29T08:55):* "**that makes no sense. if the tracking unit is
  capped, then we should see 1024 contexts somewhere out on the tiles. We don't.**"
- **RULING: tier 1 wins over tier 3. A full FTU BOUNDS in-flight invocations; it is not
  evidence that it BINDS.** The diagnosis is what the entries hold — **outstanding** work
  (tiles are the constraint) versus **returned-and-unjoined** results (the host's
  consumption rate and the fork/join code shape are). #180 says the measured case was the
  latter, and that enlarging the unit does not help it. **Instrument the split before
  concluding anything from an occupancy number.** Both statements were absent from this
  document; H.9, I.5, N.4 and O.1 are corrected. Note this compounds with L32 — the run in
  question is the suspect one.
- **CONSEQUENTLY RESOLVED.** The stress numbers are settled (L32, RULED), so the inference this row polices is testable rather than provisional — and it is still the wrong inference: **a full FTU is a symptom, and the diagnosis is what the entries are doing** (#180, #171; H.9). The split — outstanding vs returned-and-unjoined — remains the measurement to take.

**L34 — Three different baseline migration counts for one nominal workload, unreconciled.**
- *Tier 3:* DESIGN §27.1 D:3084 gives **1,638,325** at **0.150**/instr, and that 0.150 is
  the value used in the cross-simulator parity table at D:3092. DESIGN §29 D:3317/3332
  give **1,703,838** at **0.1519**. DESIGN §29 D:3341 gives **1,605,541** on the later
  build. This document carries the second and third and not the first.
- **RULING: these are three runs, not three readings of one run, and the ~4% spread is
  itself the build-independence point. But a migration count quoted without its run is not
  evidence.** Recorded in Part L.
- *And the provenance problem is worse than "unreconciled":* **none of the three names a
  configuration that exists in the tree.** All were measured at **4 MiB LLC slices**, and
  every one of the 33 checked-in configs has a **512 KiB** slice (L28c). **So these counts
  cannot be re-taken from any committed file to reconcile them.** (The earlier extraction flagged this as `[X-4]` with the
  instruction "**say which run a migration count came from**"; that instruction is adopted
  here as a rule.)

**L35 — The final translation SURFACE is open on the user's own last word.
[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#269 2026-09-01T20:11:19Z, the closing sentence of the most complete
  translation statement in the record):* "For walks to be cheap, there are two approaches:
  **Either an independent page-table root per tile (requires virtual-address-partitioning
  out of necessity, exposing us to the undesireable virtual-address-routing trap) or a
  single page table on duplicate pages across each tile. Regardless, walks must remain
  local** ... **I bring up all these points because I am not sure what the right final
  surface is.**"
- *What IS settled by the same message and by the newer #283:* virtual-address
  partitioning is rejected; the N-roots branch requires it and is therefore unavailable;
  translation must be possible locally with no fabric crossing. **The single duplicated
  page table is the only branch left standing, and it is what I3 requires.**
- *What is NOT settled:* the user's own last sentence says the surface is undecided, and
  **nothing later in the log retracts it.**
- **RULING: build the single duplicated table — it is the only option the rejections
  leave, and the invariants depend on it. But do NOT record the translation surface as
  closed on the user's authority.** An earlier revision of this document rejected N roots
  citing **#265, which is 46 minutes OLDER than #269** — an older tier-1 statement
  overriding a newer one, in exact violation of #307. Corrected in I3, F.5 and R2.
  **For the user: is the translation surface settled as the single duplicated page table,
  or still open?** **Answered — see the RULED bullet below.**
- **RULED — user ruling 2026-09-02 R12** for the surface (**one page table per address space, duplicated on every tile; TLBs shared**) and **R2** for the mechanism's status ("relabel is fine"): `VIRTUAL_FIRST` is a **labelled F.10 control**, not deleted, and the default moves to the physical/NUCA router. See I3, C.2, F.5a. **CLOSED.**

**L36 — Placement: the canon says TRANSLATION decides the tile; ChampSim decides it with a
COUNTER, in every shipped configuration. [BLOCKING] [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#23 2026-08-27T18:06, #31 19:26, #104 2026-08-28T23:10):* "**The pmem assigned
  maps which tile we go to, meaning we can load-balance in real-time, not compile-time**";
  "the OS ... **chooses which to hand back, which is where initial placement will
  happen**"; "**The vmem->pmem translation should be placing it according to the physical
  address of the PC that it was passed.**" I4, A.4.
- *Tier 2 (ChampSim) — and the canon never mentioned the parameter that decides it:*
  `src/nmfc/function_fabric.cc:11-13` says "**Placement lives here**", not in the
  translation path. `:19` documents four values —
  `placement_policy "round_robin" | "least_loaded" | "by_entry_pc" | "random"`; `:71`
  defaults to `round_robin`; `choose_tile()` at `:263-306` reads the entry PC on **one**
  arm (`:293`, `router_->placement_for`) and returns a bare counter otherwise
  (`:296-306`). **Seven checked-in configs name `round_robin`, including the headline
  `nmfc_4tile.json`.** The other thirteen name `"first_touch"`, which
  `function_fabric.cc:52-57` **silently redirects to `BY_ENTRY_PC`** with its own comment:
  "*first_touch is accepted and redirected rather than rejected: **it names a policy that
  was removed for being unimplementable**, and configurations still ask for it*" — so four
  directory names in the tree advertise a policy that does not exist. And under
  `PHYSICAL_ROUTER` (15 configs) even the entry-PC arm ignores the address:
  `physical_router.cc:46` reads a **second** parameter, `placement`, also defaulting to
  `round_robin`, and `:82-93` consults `vaddr` only under `first_touch`; **no config sets
  it**, so `phys_ft/`, `ram/` and `wide/` dispatch identically to `phys/`.
- **RULING: tier 1 wins. Placement is a translation-time decision by the address space's
  owner.** What ships is round-robin under three names. **Consequence for every result:
  no number taken from a shipped config measures I4's placement**, and any "round-robin
  vs first-touch" contrast in Parts G, K or N must first say which of the three mechanisms
  actually ran. **For the user: set `placement_policy`/`placement` explicitly in the
  shipped configs and retire the `first_touch` alias, or keep the alias and rename the
  four `_ft` directories to what they actually select?** Recorded in A.4, I4, D.5, G.6.
- **RULED by the freeze — user ruling 2026-09-02 R3.** Setting `placement_policy` explicitly in every shipped config, and renaming the `_ft` directories, are ChampSim changes and are frozen. **The canon is unchanged: translation decides the tile (I4), and no shipped ChampSim result measures it.** State that at every quotation. Status: **frozen.**

**L37 — "#291's ceiling passes": measured on one clause of two, over an assertion the same
document says is not installed.**
- *Tier 1 (#291 2026-09-02T12:37):* two clauses. "**Any migration due to instruction fetch
  or translation is by construction wrong. Then, only DATA migrations should happen, and
  never outnumber the number of loads/stores issued by a function.**"
- *Tier 3, reported four times as a pass* (I5, J.4 ×2 including a bold "PASSES" with no
  qualifier, N.6): 396,161 migrations against 524,288 loads = 0.76/memop. **That tests
  clause two.**
- *Tier 2, and it contradicts the pass on clause one:* under `--walk-routing fabric`,
  `make_config.py:265-267` skips the `("mmu", …)` `TILE_PORT`
  (`if args.mmu and args.walk_routing == "local"`), **so `strict_locality` never checks a
  walk reference — the assertion that would catch a foreign walk is absent in exactly the
  **15** configurations that make foreign walks possible** (L26; F.6's census — 13 is the
  unrelated `first_touch` count). **The record does not name
  which walk-routing mode produced the 396,161-migration run.**
- **RULING: the ceiling's COUNT clause is measured and passing; its LEGITIMACY clause is
  UNMEASURED, and asserting a pass over an assertion the document elsewhere says is
  missing is the I9 failure shape repeating.** **For the user: re-run the ceiling under
  `--walk-routing local` with the MMU `TILE_PORT` installed, so both clauses are actually
  tested?** Recorded in I5, J.4, N.6.
- **RULED by the freeze — user ruling 2026-09-02 R3.** The re-run is a ChampSim run under a configuration change and is not available. **Option (b) is operative: it is a pass on the ceiling's COUNT clause only, and silence on its legitimacy clause** — say so at every quotation (I5, J.4, N.6). O.4's `remote-walk rate` is the instrument that would close it without an assertion. Status: **frozen.**

**L38 — GRAIN PLACEMENT IS VIRTUAL-ADDRESS CONGRUENCE, which F.3 orders deleted on
sight. [BLOCKING] [FOR THE USER TO RULE]**
- *Tier 1 (#269 2026-09-01T20:11:19Z):* partitioning tiles by virtual address **leaks
  hardware-specific details into the virtual address space**, **exposes the tile layout**,
  **confines the compiler to a fixed mapping**, and **lets programs manipulate placement in
  ways unfriendly to shared systems**. *And I4:* placement is a **translation-time**
  decision by the address space's owner.
- *Tier 2 (ChampSim), in four places and unanimous:* `nmfc_vmem.cc:386-393` defaults an
  unhinted grain to `vgrain % map_.num_tiles()`; `:531-538` states it — "**Either way the
  frame lands on the tile the virtual address names**"; `:555-559` takes `hint.tile` under
  `TRANSLATE_FIRST` (**22 router-carrying configs — 15 `PHYSICAL_ROUTER`, 5 `NUCA_ROUTER`,
  2 `ADAPTIVE_ROUTER`**) and `placement_for` under `VIRTUAL_FIRST` (**the 4
  `CONGRUENT_ROUTER` files**; 7 configs carry no router child at all), **and both
  compute the same congruence**, because `annotate.cc:370` builds every hint as
  `(a >> opt.grain_bits) % opt.tiles` and `nmfc_producer.cc:271` forwards it unchanged;
  `nuca_router.cc:110` and `physical_router.cc:88` are literally
  `return map_.tile_of_virtual(vaddr);`. `annotate.cc:111-141` (`place_regions`) rotates
  each region's simulated base forward a grain at a time **specifically to preserve** that
  congruence — its own comment: "*every region is placed at a grain whose index is
  congruent, modulo the tile count, to the grain it really occupied. All tile relationships
  the program computed then survive the move as a single rotation.*" **Unhinted pages take
  the default by construction:** all 32 configs that instantiate `NMFC_VMEM` set
  `"default_region": "standard"` (`nmfc_vmem.cc:36`, `:74`, whose parameter documentation
  is "*where unhinted pages go*"); the 33rd, `mem_network_smoke.json`, has no vmem at all.
- *The document's own prohibition (F.3):* delete on sight "`tile_of(virtual_address)` as a
  router" and "a hint whose payload is a tile number". **Both are built and both are
  load-bearing.**
- *Aggravating:* **R37 records why the round-robin counter was removed (75.3% of accesses
  routed to the wrong tile) and never recorded what replaced it.** The canon carried no
  grain-placement rule at all — `grep` for `vgrain`, `hint.tile`, `placement_hint`,
  `default_region` returned zero — and summarised the machine as "round-robin dispatch
  under three different names", which describes only invocation dispatch (A.4).
- **RULING: NOT RULED HERE. This is a design question, not a transcription error**, and no
  tier-1 statement addresses grain *frame selection* as distinct from architectural
  partitioning. **For the user: is `(va >> grain_bits) % num_tiles` (a) a legitimate
  default frame-selection heuristic taken by the address-space owner at translation time —
  in which case F.3's delete-on-sight list must stop naming it — or (b) the rejected
  design, in which case the hint must be relabelled a control (F.10) and a placement rule
  that does not read the virtual address must be built?** Until ruled, **no document may
  describe this machine's data placement as round-robin**, and R37 must not be "fixed" by
  restoring the counter. See A.4a, F.3, R37.
- **RULED — user ruling 2026-09-03 O1, verbatim: "Unhinted grains are up to the OS/hardware to place. So, presumably the OS could map it wherever was most convenient."** **Reading (a), with a sharper edge than (a) had.** An unhinted grain's placement is the **address-space owner's FREE CHOICE — allocator convenience** — and `(va >> grain_bits) % num_tiles` is one convenient choice among several the OS may make. **F.3's delete-on-sight list stops naming it as an architectural partition** and goes on naming `tile_of(virtual_address)`-as-a-router, which is a different mechanism. **And the ruling carries a prohibition reading (a) did not: NO PARTITION SEMANTICS ATTACH TO AN UNHINTED GRAIN'S VIRTUAL ADDRESS** — nothing may derive a tile from a VA and depend on the answer. **#269's rejection of virtual-address partitioning is entirely untouched**: that is about the architecture reading the VA; O1 is about an allocator picking a convenient frame. The separating test: *if the mechanism would break when the OS chose differently, it is relying on a partition and it is wrong.* **The built code is therefore permitted as an allocator default and forbidden as a router — R2's "relabel is fine" applied exactly (F.10).** And unchanged: **never describe this machine's placement as round-robin**; R37's counter is not restored, because "wherever was most convenient" is a free choice, not a rotation. Applied at **F.3**, **F.8**, **A.4a**. **The `[FOR THE USER TO RULE]` tag on this row is CLOSED.**

**L39 — The DRAM address mapper #58 asked for: the canon files it as unbuilt; it is built,
on by default, and selected by a checked-in device file.**
- *Tier 1 (#58, #59, #64):* bank bits low; bank **and** bankgroup hashed; minimalist and
  device-portable; for **parallelism**, not row-hit rate.
- *Tier 2 (ChampSim):* `src/nmfc/nmfc_addr_mapper.cc` implements `NMFCMinimalist` — bit
  order from the LSB `gang | channel | bankgroup | bank | rank | columns | row`
  (`:20-25`, `:134-141`); row-bit parity folded into **both** indices at `:143-151`
  (`bankgroup ^= taps(...)`, `bank ^= taps(...)`); **`hash_banks` defaults to true**
  (`:57`, `:79`, `:91`); device-agnostic at `:101-107`/`:113`; **bijectivity swept and the
  run aborted at setup if it fails** (`:174-202`). `config/nmfc/ramulator/tile_hbm3.yaml:26-29`
  selects `impl: NMFCMinimalist`, `gang_size: 4`, `hash_banks: true`.
- **RULING: tier 1 and tier 2 AGREE. There was no conflict — the canon simply had it
  wrong**, and contradicted itself on the same page (E.5 said "none of the three is
  built"; E.6, nine lines later, said the mapper exists and HBM3 selects it). Corrected in
  E.5. **What remains open is only #64's cap-of-1-2 question and whether DDR5 should select
  it too** — `tile_ddr5.yaml:26` still uses stock `RoBaRaCoCh`.

**L40 — "Check congruence on every run" (I9): the assertion is still gated, in 29 of 33
configs and on every STANDARD page. [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 / canon (I9, DESIGN §0.9 D:62-70):* "A grain sits on the tile its physical
  address names ... **Check it, on every run.**"
- *Tier 2 (ChampSim):* `nmfc_vmem.cc:302` gates the `exit(-1)` on
  `router_->order() == VIRTUAL_FIRST && map_.is_nmfc(pa)`. `VIRTUAL_FIRST` is
  `CONGRUENT_ROUTER` only — **4 configs**; the other 22 router-carrying files report
  `TRANSLATE_FIRST` and 7 have no router. **All 32 configs with a vmem set
  `"default_region": "standard"`**, so unhinted pages are exempt by construction. The tree
  states the gap itself, `function_core.cc:941-946`: "**nothing checks that under
  `TRANSLATE_FIRST`, because the check in `NMFC_VMEM` is gated on `VIRTUAL_FIRST`.**"
- *Tier 2, the substitute, and it is a good one:* `function_core.cc:938-961`/`:1310-1316`
  compare `tile_of(physical)` against `tile_of_virtual(virtual)` on **every memory
  operation under every routing order** and report at `:299-303` — `INCONGRUENT: n of m`
  and, separately, `INCONGRUENT among nmfc-stamped:`. **The second line is the one that
  bears on I9**; the first legitimately counts STANDARD pages, whose tile is a block field
  and never was the grain field.
- *Note:* the mode gate is **deliberate and correct** (`nmfc_vmem.cc:298-301`); only the
  routing-order gate is questionable.
- **RULING: the invariant stands. Its ENFORCEMENT is not what this document said it was.**
  **For the user: ungate the assertion for `TRANSLATE_FIRST`, or accept the always-on
  instrument as I9's enforcement and record that?** The document previously discussed only
  SST's `checkCongruent()` (E.2a) and left ChampSim's gate unrecorded. See I9.
- **RULED by the freeze — user ruling 2026-09-02 R3.** Ungating the assertion is a ChampSim change and is not available. **The function core's always-on instrument is the operative enforcement**: read `INCONGRUENT among nmfc-stamped` from every run (I9). Ungating remains queued, not abandoned. Status: **frozen.**

**L41 — The GRAIN page type — siloing — cannot be expressed by any shipped tool, so every
result in the tree is a REGULAR/striped layout. [BLOCKING for any co-location claim]**
- *Canon (C.3, F.2, E.3):* GRAIN is a compiler-selected page type; the grain formula exists
  to license **siloing**; `vtile` is the label that selects it.
- *Tier 2 (ChampSim):* `grep -rn vtile src/nmfc inc/nmfc tools/nmfc config/nmfc` = **0
  hits**. `annotate.cc:83-104` accepts only `<name> <base> <bytes> [standard|nmfc]` and
  dies on any other token (`:100`); neither checked-in manifest has a tile column.
  `nmfc_trace.h:127-145` has three regions (STANDARD/NMFC/CODE) — **`NMFC` is REGULAR;
  there is no GRAIN**. Every hint's tile is **computed**: `annotate.cc:370`,
  `(a >> opt.grain_bits) % opt.tiles`, which is the striped rule by definition, and
  `place_regions` (`:111-141`) rotates region bases specifically to preserve it.
  `PAGE_HINT`'s own comment (`nmfc_trace.h:74-81`) advertises "**this is how the
  pseudo-compiler silos a data structure ... onto one memory tile**" — a mechanism its only
  producer never emits.
- **RULING: not a conflict — a MISSING CAPABILITY, and the canon's silence about it is the
  defect.** C.3 said the *hardware* cannot tell REGULAR from GRAIN apart (true, and fine,
  because the compiler selects); it did not say **nothing upstream can either**. **Every
  measurement in Parts G, K and N was taken on a striped layout**, so "co-location did not
  help" is not yet a statement about co-location. **The receiving side already works**
  (`nmfc_vmem.cc:555-559` honours `hint.tile`); the missing pieces are a manifest token, a
  `region::GRAIN`, and a branch at `annotate.cc:370`. See C.3.

**L42 — `ADAPTIVE_ROUTER`'s `GRAINS PER TILE` counts remaps, not grains, and its
`placement` parameter is unreachable.**
- *Tier 2:* `adaptive_router.cc:63` takes a **fourth** round-robin `placement` parameter
  (default `"round_robin"`), body at `:76-84`. **Neither `adapt/` config sets it, and
  `placement_for` is never called under either**: `nmfc_vmem.cc:556-558` calls it only
  under `VIRTUAL_FIRST` and `adaptive_router.cc:67` is `TRANSLATE_FIRST`; the only other
  caller, `function_fabric.cc:293`, is the `BY_ENTRY_PC` arm, and
  `adapt/nmfc_4tile.json:877` selects `"placement_policy": "round_robin"`.
- *So* `placed_[]` increments **only** at `:198`, inside `rebalance()`'s remap loop, and is
  printed at `:115` as `GRAINS PER TILE` and exported at `:125` as `grains_per_tile`.
- **RULING: the statistic must not be quoted as a placement distribution. It is `REMAPS`
  broken down by destination.** Same shape as **L21**'s always-zero `DISPATCH BLOCKED`;
  this is the second instance, so **audit every derived statistic in
  `adaptive_router.cc`/`function_fabric.cc` against the site that increments it.** The
  canon's "round-robin dispatch under three different names" (A.4) undercounts by one. See
  A.4.
- **A SECOND DEFECT IN THE SAME FILE, recorded here rather than ruled on (this closed open item O10).** `nuca_router.cc:253` refuses a component when `members.size() > ceil(|observed grains| / tiles)` — the denominator is **every grain the router has observed so far**, which **grows monotonically as the run proceeds, so the gate loosens on its own** and two components of identical size get different verdicts depending only on when they were seen. **This is a defect of the frozen implementation, in the class R5 established** ("*That sounds like a bug. Needs investigation.*"), **not a definition to ratify into G.4 rule 3.** It is not being fixed: R3 froze ChampSim. And the unit and denominator are tuning parameters under **R21** — "*we start at common and implement tuning/algorithm adjustments as needed*" — hence configuration, not canon. G.4 rule 3 and G.5 carry the same note; the 87/818 counts were produced against this gate and a comparison must hold it fixed.

**L43 — Part I's ENCODING has no tier-1 or tier-2 source, and its only complete statement
is in the tree the document says never decides anything. [FOR THE USER TO RULE]**
- *Tier 1:* names instructions (`FORK`, `JOIN`, `PJOIN`, `RETC`, `CONT`, `CXW`/`CXR` in
  substance) and **never names an encoding.**
- *Tier 2:* **nothing.** `grep -rn 'NMFC_G_FORK\|custom-0' src/nmfc inc/nmfc tools/nmfc`
  → no hits. ChampSim has no decoder, no opcode table and no assembler; it encodes the
  ISA as trace-record kinds plus the offload aperture (I.10).
- *Tier 3:* **more than the earlier revision credited.** §23.7 D:2245 reserves `custom-0`
  and reserves two group slots for `KILL` and mailboxes; **§26.6 D:2907-2917 states the
  whole RoCC reform** — funct7 = group (top three) + variant (low four), funct3 = the
  RoCC operand flags, a context register named by a number in a GPR. **What tier 3 does
  NOT give is a single numeric value**: no group numbers, no variant bits, no funct7
  constants, no funct3 encodings.
- *Tier 4:* `/mnt/md0/NMFC-Rev/src/nmfc/include/nmfc_isa.h:21-104` is the only complete
  encoding that exists, and `nmfc.h`'s assembler macros already emit from it.
- **RULING: the encoding is REPRODUCED, not ratified.** Every numeric value in I.9 is
  marked `[SST-only — implementation choice]`. It is reproduced because re-deriving it would
  invalidate binaries that already assemble against it — which is the exact churn §23.7
  D:2242-2243 warns about — **not because tier 4 decided anything.**
  **For the user, and the question is NARROWED — item O3 no longer asks "ratify or leave
  open"; it asks whether the canon assigns these fields AT ALL.** Under R6–R10 an
  encoding's field values are the kind of thing the canon does not fix, and the authority
  table says tier 4 "never decides anything", so ratifying tier-4 values as canon would
  override both. **What actually depends on the bits is small and stated:** the slot
  `RESUME` takes (I.3, I.6) and the SST binaries already assembled against this header.
  **No invariant depends on them.** Open item **O3**, option (a) recommended.
- *Also recorded:* the earlier revision reproduced `nmfc_isa.h:67`'s own comment
  ("*Variant bits, consistent across groups so a reader can decode without a table*") and
  then `[DISAMBIGUATED]` it as false **without naming the source it was correcting**. Now
  named, in I.9.
- **RULED — user ruling 2026-09-03 O3, verbatim: "I think this is just a simulator thing and not a meaningful design choice, so I say we describe it as implementation choice."** **Option (a): THE CANON ASSIGNS NO FIELD VALUES AT ALL.** `funct7` and `funct3` values are **implementation choice**. What the canon *does* fix is unchanged and is the whole of what it fixes: the **count** (twelve base plus a privileged `RESUME`, L44), the **membership of the groups** (tier 3), the **RoCC split** (funct7 = group + variant, funct3 = operand flags, §26.6), and that **`RESUME` takes a slot** — but not which one. **`nmfc_isa.h` is one implementation's choice**, recorded in **SELECTED CONFIGURATION** with its file and line, and **never quoted as canon**. The tag on every value in I.9 changes from `[SST-only — unratified]` — which implied a ratification still pending — to **`[SST-only — implementation choice]`**, because none is pending: none is wanted. **This is R6–R10 applied to an encoding**, which is exactly where the narrowed question pointed. Applied at **I.9**, **I.7**, **SELECTED CONFIGURATION**. **The `[FOR THE USER TO RULE]` tag on this row is CLOSED, and it was the last one live in this appendix.**

**L44 — Thirteen instructions or twelve? DESIGN.md says both. [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 3, list A (§23 enumerates THIRTEEN mnemonics):* §23.2 D:2011-2017 — `FORK.R`,
  `FORK.M`, `FORKF.R`, `FORKF.M`, `FORKQ`, `JOIN`, `JOINQ` (**7**); §23.3 D:2095-2098 —
  `RETC`, `ENDC`, `CONT`, `CONT.M` (**4**); §23.6 D:2191-2192 — `CXW`, `CXR` (**2**).
  **7 + 4 + 2 = 13.**
- *Tier 3, list B (§23.7 D:2241):* "**Twelve** instructions fit one RISC-V `custom-*`
  opcode with `funct3`/`funct7`."
- *Tier 2:* silent — no instruction encoding exists in ChampSim at all.
- *Tier 1:* silent on the count.
- **Candidate reconciliation, offered and NOT ruled:** §23.3 D:2103-2104 says "`RETC` and
  `ENDC` are **one opcode and a return bit**". Counting the END group as one instruction
  gives **12**; counting the two encodings the SST header actually assigns (`0x30`,
  `0x31`) gives **13**.
- **RULING: this document says THIRTEEN** — in A.2, C.4, I.3 and I.9 — because thirteen
  mnemonics have to be assembled and thirteen funct7 values exist. **The discrepancy is
  internal to tier 3 and is not resolved here.** For the user: thirteen, or twelve with
  END counted once? **Answered: twelve, END counted once — see the RULED bullet below.**
- **RULED — user ruling 2026-09-02: "RETC and ENDC are the same instruction, with a return bit."** **TWELVE**, and DESIGN §23.7 D:2241 was the half of tier 3 that was right; §23's thirteen-mnemonic enumeration counted the two END forms separately. The base set is `FORK.R` `FORK.M` `FORKF.R` `FORKF.M` `FORKQ` `JOIN` `JOINQ` `END`(+ret bit) `CONT` `CONT.M` `CXW` `CXR`. **Plus `RESUME` (R20), privileged, which makes thirteen and is not part of the user-level twelve.** See I.3, I.9. **CLOSED.**

**L45 — DESIGN.md still carries the SUPERSEDED encoding, and records nowhere that it was
superseded.**
- *Tier 3, §23.7 D:2245-2247:* "`funct3` 0-5, with 6 and 7 left for §23.5's `KILL` and
  mailboxes. **Context-register indices ride the five-bit register fields, read against a
  different file.**"
- *Tier 3, §26.6 D:2907-2917, later in the same document:* funct3 carries the RoCC operand
  flags — "the old encoding, which used funct3 as the group selector, would have told it
  that `FORK` (group 0) touches no registers" — and "a context register is now named by a
  **number in a general register** rather than by a five-bit field read against a
  different file".
- **RULING: §26.6 governs; §23.7's encoding paragraph is superseded.** This is a
  supersession *within one tier*, and DESIGN.md marks it nowhere — a reader who stops at
  §23.7 builds the encoding that cannot survive RoCC. **Mechanical action: add a
  supersession note at D:2239.** Recorded in I.9. (Not a user ruling; a documentation
  defect in tier 3, of the same class as **L11** and **L13**.)

**L46 — The function core's BASE instruction set is never stated, and the document argues
from two different ones. [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1:* #227 (2026-09-01T04:41:17Z) step 6 — "**Compile GAPBS BFS to RISC-V + our
  extension and run it**"; #216 (2026-09-01T02:01:31Z) — "`riscv64-unknown-elf-gcc` is now
  installed". **Never names x86-64 as a target.**
- *Tier 2:* the toolchain is entirely **x86-64** — Pin, `objdump` for `lock`/`ret`/`push`,
  `annotate.cc:219` on `rip`/`rsp`/`rflags`, `:695-719` dropping the x86 `ret` pop and the
  x86-64 ABI saves. **ChampSim executes trace records and has no target ISA of its own.**
- *Tier 3:* §23 D:1988-1990 — "on a **RISC-V** target it is real"; §23.7 reserves a RISC-V
  `custom-*` opcode; §26.6 builds the host on Vanadis, an out-of-order RISC-V core; and
  §22 D:1981 discusses "**x86-64's** callee-saved register preservation" as a property of
  the annotation tool.
- *Tier 4:* `S6` — the tile is **RV64IM+A** only.
- **RULING: the target is RISC-V; x86-64 is the trace toolchain's host.** Every x86-64
  consequence in invariant 7 and K.6 is therefore an illustration of the rule ("a function
  that spills cannot run"), not a property of the machine — restated on RISC-V in **I.0**.
  **For the user: confirm, and fix the SUBSET** — RV64IM+A, RV64I, or something else? That
  decides whether an offloadable function may multiply, divide or use floating point at
  all, which the admission test does not currently check. **Open item O4.**
- **PARTLY RULED — user ruling 2026-09-02 R11: "Lets do RISCV. x86 was chosen initially since initial develop was on PIN. RISCV is easier."** **The target is RISC-V; x86-64 is the PIN toolchain's host and is history, not an alternative.** Invariant 7's consequences are restated on RV64 (no `fp`, no `jal` from a body, a ninth argument on the stack, any stack store is a spill).
- **FULLY RULED — user ruling 2026-09-03 O4, verbatim: "I think we want float, so C."** **The subset is `RV64IMAFD`**, option (c) of that row. `I` is the base; `M` because integer multiply and divide appear in ordinary index arithmetic; **`A` is not optional**, because H.7's atomic table and R15's hand-off chain are an architectural feature the ISA must be able to name; **`F` and `D` because the user ruled them in.** **AND THE CONSEQUENCE, STATED CORRECTLY, BECAUSE THE O4 ROW STATED IT WRONG:** the row said float "*widens the 512-bit context register file's per-context save set*". **It does not.** The context is **512 bits, BIT-PACKED** (#232, #238, restated as a correction on 2026-09-03) — **not eight 64-bit registers** — so there is no per-register set to widen. An `f64` costs **64 bits** and an `f32` **32 bits** of the same 512, and the compiler packs them. **Invariant 2, invariant 11 and the 72-byte migration are untouched; `F`/`D` add ZERO bytes to a migration.** What O4 *does* change is naming: a RISC-V encoding names `f0`–`f31` separately from `x0`–`x31`, so **the packed file is presented under two register namespaces over the same 512 bits** — a naming convention, never a second file `[derived from ruling O4]`. **The namespaces do not alias**: the core implements 512 bits of live storage rather than 64 architectural slots, and the compiler binds every simultaneously-live `f`- or `x`-name to a **disjoint bit range**, so `f3` and `x3` are different names at different offsets, not one slot. **What is bounded is liveness in bits across both namespaces together (invariant 2), not the name space — so the machine is an RV64IMAFD target under a register-pressure constraint, and a stock unconstrained binary is REJECTED by the admission test, as one with a stack or a spill already was** (I.0's four-point answer, I7). **An implementation that builds a separate FP register file has built a second context and broken invariant 2.** **The admission test now checks the `IMAFD` subset and counts liveness in BITS** (K.6). Tier 4's `S6` (RV64IM+A only) is overruled and becomes a divergence. Applied at **I.0**, **I.7**, **K.6**. **CLOSED.**

**L47 — One of the two official invocation loops has no instructions.** **[RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#130, #131, #132, 2026-08-29T03:56-03:59):* the memory-committing loop needs a
  symbol hook where the spin begins, primitives meaning "I commit work here" and "I block
  until commit here", ownership **by address**, and **never a double commit, never a
  double block**. *#129:* "**there is no way for ChampSim to model the busy-spin polling
  for those done bits.**"
- *Tier 3 (§4.3 D:369-415):* the same two requirements, plus the reason — a traced
  synchronous call spun zero times, so "*a memory-committing loop replayed naively lets
  the host read results it never waited for, and the optimism is invisible in every number
  it produces*".
- *Tier 2:* **all of it is built, as trace markers rather than opcodes** —
  `__nmfc_wait(p)` (`tools/nmfc/kernels/nmfc_kernels.h:46-60`), `NMFC_COMMIT(p,v)` as a
  `nopl 0x2a(%rax)` marker before the publishing store (`:63-81`), captured by
  `trace.sh:36-46`, resolved and checked in `annotate.cc:403-441, 751-800`, with both the
  double commit and the double block **fatal**.
- *This document, before this revision:* Part I contained **zero** occurrences of
  "commit", and I.7's "deliberately absent" list did not name them — so the ISA appeared
  to be missing a loop the design keeps.
- **RULING: the loop is implemented and the ISA gap may be correct.** A blocking `WAIT`
  instruction is exactly the resource-held-while-waiting shape **I.1** forbids, so
  "commit = an ordinary store, wait = a software spin" is the reading consistent with the
  governing rule. **For the user: confirm that no instruction is needed, or reserve a
  group in `0x6`/`0x7` for a recognisable commit plus a `COMMITQ` probe?** Recorded in
  A.4, C.4 and **I.11**. **Answered by R14 — see the RULED bullet below.**
- **RULED — user ruling 2026-09-02 R14: "That was an artifact of the ChampSim design, since ChampSim doesn't model coherence, data, or atomics. By necessity, a core must poll said block to see if the writeback of the data has occurred (coherence propagated). Potentially using atomics."** **No instructions are needed.** A commit is an ordinary store; coherence publishes it; the host polls the block, with an atomic if it needs a read-modify-write. `__nmfc_wait` and `NMFC_COMMIT` are a **simulator encoding**, like the offload aperture (L1). Option (b) — reserving `COMMITQ` — is not taken. See I.11, C.4. **CLOSED.**

**L48 — The atomic table is named three times under three names, is never stated to be one
structure, and its one hard bound has no value at tiers 1–3. [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1 (#238, 2026-09-01T06:19:05Z):* "we have a **unified atomic table** that enforces
  atomic relationships." *(#71, #251, #252, #262 supply the granularity, the coherency
  obligation, "leverage the core, not the memory system", and the two-tiles-cannot-contend
  property.)*
- *This document:* calls it "the **lock table**" (H.6), "a **unified atomic table**" and
  "the **held-word table**" (H.7) and **never says they are one object.**
- *Tier 3 (§25.6 D:2524-2530):* "*a word passed context to context is a word the rest of
  the machine cannot see, so **after a fixed number of hand-offs** it goes back to the data
  cache whether or not anyone is still waiting*" — **and never gives the number.** H.7
  calls the bound "a **coherency guarantee**, not an optimisation".
- *Tier 2:* `function_core.cc:404-467` — `lock_waiters_` is a `std::vector` per block with
  **no bound and no hand-off counter at all.**
- *Tier 4:* `NMFCTile.h:142` / `NMFCTile.cc:32` — `maxAtomicForwards`, default **8**; the
  chain ends and the word is written back at `NMFCTile.cc:1044-1064`.
- **Also unspecified at every tier:** entry format, indexing, **capacity**, and what
  happens when the table is full — **a full table is the hold-and-wait shape I.1 forbids**,
  so this is not a detail.
- **RULING: not resolved here.** **For the user: declare the three names one structure and
  set the hand-off bound at 8 (tier 4's value, which the "chain of eight, a writeback,
  chain of six, a writeback" measurement is consistent with), or give a different bound —
  and state the capacity and the full-table behaviour.** **Answered by R15 — see the RULED bullet below.**
- **RULED — user ruling 2026-09-02 R15: "Yes, the same structure. … it must enforce atomics. It allows contexts to obtain/release/pass atomics quickly. Capacity is something that must be sized from experimentation. Full table must either be unachievable by construction or safely block (sleep context until possible). … chain bound must be experimentally derived."** **ONE structure** — "the unified atomic table", "the lock table" and "the held-word table" are the same object, and this document now says **the atomic table**. **Capacity and the hand-off chain bound are configuration**; the design part is that a full table is **unachievable by construction or the context sleeps**, never a resource held while waiting for a resource (I.1). See H.7. **CLOSED.**

**L49 — Address spaces and ASIDs are absent from this document, while three invariants
depend on them. [RULED 2026-09-02 — see the RULED bullet at the end of this row]**
- *Tier 1:* invariant 4 makes placement "a translation-time decision by **the address
  space's owner**" (#23, #31, #104); F.3's fourth rejection reason is that VA partitioning
  "lets a program steer placement… **unfriendly to shared systems**" (#269); invariant 3
  says there is **one** page table (#107, #110). **None of the three says whether "one"
  means one per address space or one for the machine.**
- *Tier 2:* ChampSim **already carries an ASID through placement** —
  `remap_grain(asid, vgrain, best)`, and `nuca_router.cc:198` keys a grain as
  `(asid << 48) | (vaddr >> grain_bits)`. The trace format carries `asid` in
  `PAGE_HINT`'s `aux1` (`inc/nmfc/nmfc_trace.h:74-81`).
- *Tier 3:* silent. No section discusses processes, isolation or context switching.
- *This document, before this revision:* `grep -in 'asid'` → 4 hits, all inside quoted
  ChampSim code; `'address space identifier'`, `'multi-tenan'`, `'isolation'`,
  `'context switch'`, `'virtualiz'` → **0 each.**
- **RULING: not resolved here, and it is first-order.** It changes the duplicate-page
  footprint (one table duplicated `N` ways **per address space**, or once), the shootdown
  scope, and whether F.3's "unfriendly to shared systems" objection has a mechanism behind
  it. **For the user: one page table per address space, duplicated N ways — or one for the
  machine?** **Answered by R12 — see the RULED bullet below.**

---
- **RULED — user ruling 2026-09-02 R12: "I am fairly certain real machines have separate page tables per address space? TLBs are shared, page tables themselves should not be shared between address spaces?"** **One page table PER ADDRESS SPACE, duplicated on every tile. TLBs are shared** (ASID-tagged). The `asid` is part of every translation, remap and shootdown. See I3, C.2, F.5a, F.8. **CLOSED.**

## APPENDIX 2 — DIVERGENCES: SST IMPLEMENTATION vs CANON

`/mnt/md0/NMFC-Rev/src/nmfc`, read 2026-09-02, file mtimes 2026-09-01T00:31 →
2026-09-02T18:32. **Authority tier 4 — lowest. Nothing here decides anything.** This is
a checklist for the next work session, not a description of the machine.

**Count: 39 divergences** (S39 added; **S13 re-tagged from `[WRONG]` to `[NOTE]`** — it is
a documented, scoped, announced trade-off, not a defect; **S18 re-tagged from `[WRONG]` to
`[ARTEFACT]`** — its warning tests a hardcoded constant, not the geometry).

**READ D0 FIRST.** `[ADDED this revision — user ruling 2026-09-02 R3: "ChampSim updates
stop until we deem it a good idea to go back."]` **ChampSim is frozen**, so "fix it in
ChampSim" — this document's standing answer to a dozen conflicts — is no longer available,
and **SST now carries several claims outright** rather than leading on them: the 72-byte
migration cost (R4), the one-fabric subsumption measurement, and DRAM-aligned slice
banking. D0 lists the two ChampSim changes the user ordered before the freeze and every
work item the freeze suspends.

Legend: **[GAP]** the canon requires something SST does not build. **[WRONG]** SST builds
something the canon forbids or does differently. **[ARTEFACT]** a modelling or accounting
error that distorts numbers. **[STALE]** a comment that contradicts the code, which is a
regression vector because the next reader believes it. **[NOTE]** a documented, scoped
trade-off that a reader must know about but must NOT go and "repair".

[AND THERE IS AN EXISTING AUDIT OF THIS EXACT KIND, WRITTEN IN TIER 3, WHICH THIS
APPENDIX RE-DERIVED FROM SCRATCH INSTEAD OF READING. `grep '§28'` over an earlier revision
returned only `§28.2`.] **DESIGN §28 D:3191-3281 — "Audit against the ChampSim machine",
with §28.1 "What is verified", §28.2 "Closed since" and §28.3 "What is still not"** — is
the same audit, and its instruction is the one this appendix follows: "*Read from the
code rather than from this document. **Where an answer is 'no', it is a gap, not a
decision, unless it says so.**"

**Reconciled against it, and it agrees with this appendix on every overlapping row:**
§28.1 verifies one ramulator instance per memory tile, LLC slice banks aligned to the
channel's bank count, the three-way address-partition agreement that §27.1's bug broke
(`tileOf` and the slice interleave both `(pa / G) % N`, while `PageTable::lookup` used
`(pa / 64) % N`), `MigrationEvent::SIZE_BYTES = NMFC_CTX_BYTES + 8 = 72`, **only data
migrates** (`migrate()` is called from `issueLoad`, `issueStore`, `issueAtomic` and
nowhere else — instruction fetch translates but never migrates), per-context fetch and
data slots with a shared BTB, and 512 bits with no stack enforced at issue through
`RegLayout::defines()`. §28.3's three open items are **the mode bit** (built since — E.2a),
**no spawn and no measured substitute** (which is K.4's open experiment, not a defect),
and **the bank-count contradiction** (ledger L8, **RULED by the 2026-09-02 GEOMETRY ruling** — no count is canon).

**Where this appendix adds to §28 it is because the tree moved**; where it repeats §28 it
should cite it rather than re-derive it. `[NOTE — §28.1's bank statement is itself the
source of the bank-count question, now closed by the GEOMETRY ruling: "*the device declares 2 ranks × 8 bank groups × 4 banks = 64
banks per channel while §5.2's arithmetic uses 32 — per rank. **The two should be
reconciled before any bank-conflict number is quoted.**"]`

`[TAGGING RULE, added because a row was mis-tagged once.]` **`[WRONG]` is an accusation.**
Before applying it, check whether the SST tree *states* the trade-off, *bounds* it, and
*announces* it — if all three, it is `[NOTE]`. Dropping those clauses turns a bounded
decision into a defect list item, and the dropped clauses are precisely the facts that
decide whether a reader should go and change it. See **S13**.

---

### D0 — CHAMPSIM IS FROZEN. This is the ChampSim-side counterpart to the list below.

[ADDED — user ruling 2026-09-02 R3, verbatim: "I was under the impression that was derived
from invoking ramulator now, the `--llc-banks` should be inert. **ChampSim updates stop
until we deem it a good idea to go back.**" This appendix exists to list what SST diverges
from the canon on; it now has to say what ChampSim's status is, because "fix it in
ChampSim" was this document's standing answer to a dozen conflicts and it is no longer
available.]

**CHAMPSIM CHANGES STOP.** Not "are deprioritised" — stop, until the user says otherwise.

**EXACTLY TWO CHANGES WERE ORDERED BEFORE THE FREEZE, and they are the only ChampSim work
this document sanctions:**

| # | change | ruling |
|---|---|---|
| **1** | **Delete `op::SPAWN`** and `function_core::issue_spawn`. `CONT`/extend stays. | **R1** — "*delete it. CONT/extend is fine, and can stay.*" |
| **2** | **Switch the default router to `PHYSICAL_ROUTER`/`NUCA_ROUTER`** in the four configs that select `CONGRUENT_ROUTER`, and **relabel** `CONGRUENT_ROUTER` as a control rather than deleting it. | **R2** — "*That is fine, relabel is fine. You are correct the defaults should be switched to the physical/nuca router.*" |

**WHAT THE FREEZE SUSPENDS — every ChampSim work item this document had queued, with the
ledger row it lives in.** None of these is withdrawn as a *requirement*; each is
**frozen, not abandoned**, and the design statement in its ledger row still stands.

| ledger | frozen work item | what governs meanwhile |
|---|---|---|
| **L5** | build LLC banking aligned to the DRAM banks | banking is **derived from the DRAM device geometry** (R3); `--llc-banks` is inert; take bank-conflict numbers from SST |
| **L7** | retrofit the 72-byte migration byte model | **not wanted** — R4: "*ChampSim doesn't have a byte model, just a cycles-to-transmit model. No need to back port it, SST is correct.*" Subsumption claims come from SST |
| **L25** | merge the two fabrics | two fabrics are a **declared property** of the ChampSim model; I11 subsumption is measured on SST |
| **L26** | change the `--walk-routing` default and install the MMU `TILE_PORT` unconditionally | `fabric` stays a **labelled control**; the canon requirement (walks are local) is unchanged |
| **L29** | build the fire-and-forget ACK path | free-at-dispatch stands; **annotate every FTU occupancy number as excluding fire-and-forget traffic** |
| **L36** | set `placement_policy` explicitly and rename the `_ft` directories | **no shipped ChampSim result measures I4's placement** — say so at every quotation |
| **L37** | re-run #291's ceiling under `--walk-routing local` | it is a pass on the **count** clause only, and silence on the legitimacy clause |
| **L40** | ungate the `NMFC_VMEM` congruence assertion for `TRANSLATE_FIRST` | the function core's always-on instrument is the enforcement: `INCONGRUENT among nmfc-stamped` |
| **L20**, **L27**, **L28** | fix `grain_bits` 21 vs the devices' 20, and the stale headline configs | quote every number with its build; **the 4 MiB slice was never committed** (L28c, lookup run) |
| **O15** *(RULED 2026-09-03)* | re-take Parts G/K/L/N against committed configurations | **NOT DONE AND NOT TO BE DONE.** User ruling 2026-09-03 **O15** selected option (a): those Parts are **historical observations**, their configurations **unreproducible from git**, labelled as such at each Part's preamble and at N.0 — **and ChampSim stays frozen.** The freeze was **not** lifted. |

**One consequence worth stating plainly.** Several conflicts in Appendix 1 were "tier 1
requires X, ChampSim does Y". **The freeze does not change which is canon** — tier 1 still
wins, and X is still what the machine is. It changes only what happens next: **nothing,
in ChampSim.** Where a claim can only be measured on a repaired ChampSim, it is measured on
SST or it is not measured, and this document says which.

---

### D1 — Not implemented, fatal on use

| # | divergence | canon reference |
|---|---|---|
| **S1** `[GAP]` | **`FORK.M` / `FORKF.M` are fatal at the tile** (`NMFCTile.cc:548-555`). Encodable, decoded by both hosts, carried across the fabric — and then it aborts. | I.2 — the memory fork form exists precisely so the context need not already be loaded |
| **S2** `[GAP]` | **`CONT.M` is fatal at the tile** (`NMFCTile.cc:1738-1746`). | I.3 — `CONT.M` replaces the context wholesale |
| **S3** `[GAP]` | ISA groups 0x6, 0x7 (reserved for KILL and mailboxes) are fatal. | I.7 — reserved, unbuilt: correct, but the reservation should be a clean refusal, not a crash |
| **S4** `[GAP]` | **SST IMPLEMENTATION LAYOUT, TIER 4 — NOT THE DESIGN.** SST's harness hard-codes exactly one register layout, `x1..x8 × 64-bit` (the same convenience its `NMFC_CTX_WORDS = 8` / `in[0..7]` context array expresses), and no compiler produces another. **Read it as a tier-4 implementation convenience and never as a statement about the context.** The design is **512 bits, BIT-PACKED** — not eight registers, not eight lanes, not `x1`–`x8` (user #232, #238; the 512-bit rule at I2 and H.3). | I2/I.8, H.3, K.6 — 512 bits divided per function; **bit-packing is compile-side work and is not done** |
| **S5** `[GAP]` | The **bit-level admission test is never exercised**: nothing produces a layout other than the default, so the bits-used figure is always 512. | K.6 — admission is a test on bits |
| **S6** `[WRONG — as of user ruling 2026-09-03 O4]` | The tile's core is **RV64IM+A only** — no FP, CSR, FENCE, RVC, MULH*, ecall — while `main` on the host is full RV64G. **The ruled subset is `RV64IMAFD`** (user ruling 2026-09-03 **O4**, "*I think we want float, so C*"), so this tile would **trap on the floating-point instructions the canon admits**. It changed from a declared asymmetry to a divergence when O4 was ruled. **And the fix is not a second register file:** `F`/`D` values are packed into the **same 512-bit context** under two register namespaces (I.0, I.7); a separate FP file breaks invariant 2 and grows a migration past 72 B. | **I.0** (the subset and the namespaces), **K.6** (admission), H.1 |
| **S7** `[GAP]` | `NMFCHostMMU` has **no TLB** and performs a full page-table lookup per request; a remap invalidates nothing there because nothing is cached. | F.7, F.8 |
| **S39** `[GAP]` **— the one fiction DESIGN says is still OUTSTANDING, and the term that decides invariant 5's budget** | **The tile's page walk issues NO MEMORY REFERENCES.** DESIGN §26.0.1 D:2680-2687: "*The page walk issues no references. `NMFCTile::translate` charges `walkLatency_` (**30 cycles**) and touches no memory ... **So invariant 3's subject is free.** The table being duplicated onto every tile is what makes a walk local, but ***local* costs nothing if the walk never issues**, and the pressure a walk puts on the tile's own cache and channel is what decides whether §7.1 — translations dropped on migration — is a footnote or the dominant term. **Migration's after-cost is therefore too cheap.**" And §26.3 D:2825-2830: "*This is **the term that decides whether invariant 5's budget — roughly one migration per thousand instructions — is comfortable or fanciful.** §7.1 drops a context's translations when it migrates, so **each arrival pays three local references per distinct page it then touches**, on top of the transit invariant 11 already accounts for. Whether that is a footnote is currently an assumption, and it is charged as **30 flat cycles with no traffic behind it**.*" §26.5 D:2870-2873 names it as **one of exactly two things that remain** (with §24 step 0, the baseline). **Every migration-rate number in this document is measured against a machine where arriving costs nothing after the hop.** *(And DESIGN's rule about the four fictions, D:2689-2691: "**Three of the four flatter the machine and one punishes it. That is the argument for fixing all of them before measuring anything, rather than the convenient ones.**")* | I5's budget; F.7 (translations dropped); C.2's "where the page table lives"; J.5 |
| **S8** `[GAP]` | `nmfc.NMFCFabric` (`NMFCFabricComponent`) is **compiled but unwired** in every configuration — dead code with different parameters from the live fabric (hop 8 vs 4, no NUCA, no page table, no MOESIF). Any doc sentence about "the fabric" is ambiguous between the two. | I13 — there is one fabric |

### D2 — Built differently from the canon

| # | divergence | canon reference |
|---|---|---|
| **S9** `[WRONG]` | **The migration slot-admission asymmetry.** Migration admission uses a **strict** `>` against tile capacity while invocation admission uses `>=`, so **a tile can be pushed one context past its announced capacity by a migration** — and the tile then **fatals** if it has no free slot (`NMFCTile.cc:1369-1376`). | H.8 — a full tile must always be able to evacuate; a fatal here is the deadlock shape wearing a different coat |
| **S10** `[WRONG]` | **A NUCA move of a GRAIN or DUPLICATE grain is recorded and never applied**: `PageTable::lookup` consults the remapped frame **only on the REGULAR path**. | G.4, F.8 — remap is how the policy acts |
| **S11** `[WRONG]` | `NMFCTile::heldWord` requires an **exact size match**, so a 4-byte access to an 8-byte held word reads *around* the held value. | H.7 — the lock is on the operand, and the held value must be authoritative |
| **S12** `[WRONG]` | `driveMoves()` processes only the **front** move per cycle, so concurrent grain moves are serialised. | G.4 — components move as a unit |
| **S13** `[NOTE — NOT a divergence. Re-tagged.]` | When a `standard` region exists, the slice/controller address-range check is widened to all of memory, leaving the fabric's routing as the only partition enforcement. [AUTHORITY CORRECTION — this row was tagged `[WRONG], which this appendix's own legend defines as "SST builds something the canon forbids or does differently". It is neither: it is a **documented, scoped, announced trade-off**, and the two clauses that make it one were dropped when the row was written.]` DESIGN §32.1 D:3707-3717, in full: memHierarchy **cannot express the union of two interleaves**, so a configuration carrying block-spread data gives up the downstream check — "*This is a real loss of the check invariant 9 asks for, **and it is confined to configurations that ask for it: nothing declares standard data by default, and every other run keeps the grain-interleaved check. `build()` says so on stderr rather than doing it quietly.***" **Scoped, defaulted-off, and announced. There is nothing here to go and repair**; what a reader must know is that a config declaring `NMFC_STD_DATA` has one enforcement point instead of two. See **E.2a**. | I9 — check congruence on every run; **E.2a** for the full §32 context, including the congruence check's step-size hole, which IS a real defect |
| **S14** `[WRONG]` | Slices are declared to memHierarchy as `"L1": "1"` even though they are the last level. | D.1 — the slice is the memory-side cache |
| **S15** `[WRONG]` | The two hosts **disagree on what a JOIN after an END-with-return-bit-clear does**. Rev routes it through the tracking unit's ACK path, making a join-expected entry RETURNED **with zeroes** and counting a mismatch. Vanadis/RoCC frees the entry directly — **no zeroed return, no mismatch statistic.** | I.4 — a join-expected entry NEVER closes without returning its values; an END with the return bit clear must still produce an ACK **and a zeroed register file** |
| **S16** `[WRONG]` | Context registers are **per software thread on Rev** but a **single file for one hardware thread on Vanadis**. | I.8 — per software thread, so two threads sharing a hart cannot see each other's contexts and a thread does not lose them when it migrates |
| **S17** `[WRONG]` | Rev has a source-operand interlock and a stall statistic; **Vanadis has neither** (documented as by design). Rev's mismatch statistics have no RoCC equivalents. | M.2 — one instruction set on two hosts; statistics should be comparable |
| **S18** `[ARTEFACT — re-tagged; the warning fires on a HARDCODED CONSTANT, not on the geometry]` | SST warns that `G = 256 KiB × ntiles` is "not a whole number of bank sweeps at 1 or 3 tiles" and proceeds. **That warning presupposes `sweep := 512 KiB fixed`.** On the checked-in reference device a sweep is `row_bytes × banks_per_channel` = `4096 × 64` = **256 KiB**, so `G / sweep = total_channels = N` **exactly, at every tile count** — G(3) = 768 KiB = 3 sweeps. **The condition the warning tests is never true for this device; it is testing SST's own constant.** Fix the constant (derive the sweep from the device) or delete the warning. | **E.5**, which now defines `sweep` and records that the odd-tile-count caveat was an artefact of the same fixed 512 KiB — it does not exist for either device in the tree |
| **S19** `[WRONG]` | The per-tile entry-point rewrite (`pc + t·G`) documented at `NMFCFabricComponent.h:60` is **implemented nowhere** — correctly, because code is on duplicate pages, but the doc still asserts it. | J.1 — the PC does not change on migration |

### D3 — Accounting and modelling artefacts that distort numbers

| # | divergence |
|---|---|
| **S20** `[ARTEFACT]` | **`translate()` is called twice per load/store/atomic** (once in the issue path, once in the issue helper), **doubling** the translation hit/miss/walk counters and, on a walk, **doubling the walk traffic**. Any translation number from SST is currently 2× high. |
| **S21** `[ARTEFACT]` | **Invocation transit is charged to the wrong agent** — in the Vanadis configuration it lands on the OS cache, not the host L2. |
| **S22** `[ARTEFACT]` | **Migration transit is charged twice** (departing and arriving links); completion once; invocation once. Deliberate per the design's reading of I11, but it must be stated whenever migration bytes are quoted. |
| **S23** `[ARTEFACT]` | **NUCA copy bytes are charged on both the read and the write**, so a grain move reports **2×G** bytes. |
| **S24** `[ARTEFACT]` | `InvocationEvent::origin` is never assigned; there is one host and the fabric returns everything to one control port. Multi-host routing is therefore untested. |
| **S25** `[ARTEFACT]` | Statistics emitted at teardown read **zero** because a co-processor's teardown runs after statistics collection — this silently zeroed the FTU and context peak counters. |
| **S26** `[ARTEFACT]` | Rev's `ECALL_exit` calls the host process's `exit()`, **discarding every statistic with no warning**. Worked around with a `_start` stub. |
| **S27** `[ARTEFACT]` | An unbalanced simulation-hold call ends a run at simulated time 0, or never ends it — **with no error either way**. |
| **S28** `[ARTEFACT]` | The loader configuration beats `LD_LIBRARY_PATH`, so a stale memory-model shared library can silently supply the DRAM model. **Whichever copy the loader finds first *is* the memory model the results came from.** **This is the exact failure #219/#220/#255 legislated against: build the matching version out of OUR fork of ramulator2, never copy a compiled `.so` between codebases (O.3, R105/R105a).** |

### D4 — Stale comments that actively teach the wrong design

| # | divergence |
|---|---|
| **S29** `[STALE]` | `NMFCTile.h:11-15` claims **no data memory, no LLC slice, no migration, no atomics**. **All four exist.** |
| **S30** `[STALE]` | `NMFCTile.h:280-286` claims routing is on the **virtual** address "and knowingly so". **The code routes on the frame** — which is correct per I12, so the comment teaches the rejected design. |
| **S31** `[STALE]` | `NMFCTile.cc:1360-1367` says "Nothing here has translations to drop yet" and "the instruction address itself changes". **The tile has both per-context translation slots and a TLB, and the migration explicitly keeps the PC unchanged.** Both halves are wrong, and the second contradicts J.1. |
| **S32** `[STALE]` | `src/nmfc/README.md:126` describes "§5's local translation **with a page-table root per channel**" — the third surviving instance of the rejected framing (see ledger L2). |
| **S33** `[STALE]` | `NMFC-Rev/README.md:97-100` says the context-register question is open, "provisionally the eight argument registers a0-a7". **It is decided: eight 512-bit context registers per software thread, with `CXW`/`CXR`. The `a0`-`a7` aperture is rejected** (R82). |
| **S34** `[STALE]` | `NMFC-Rev/README.md:47` says "DESIGN.md §0 invariants — **all eleven**". **There are fourteen** (ledger L11). |
| **S35** `[STALE]` | `src/nmfc/README.md:128` says "**No migration and no atomics**" while atomics are built and measured elsewhere in the same file. Migration is genuinely absent only in the sense of S1/S2. |
| **S36** `[STALE]` | Documented parameter defaults contradict the code (`NMFCRoCC` documents `maxQueue` 64; the code reads 8), and a component's documented fabric hop/host-hop/link-width/directory-latency defaults are all overridden by the configurations (4/8/32/4 vs 4/20/64/12). |

### D5 — Structural gaps the SST tree names itself

| # | divergence |
|---|---|
| **S37** `[GAP]` | The tile-memory configuration's own header states it does not build the machine: **the host has no L2, so there is no L2-to-LLC coherence boundary; ONE shared bus serves every tile, so a tile's core reaching its OWN slice contends with every other tile — an artefact, since the machine puts that path inside the tile; coherence runs over that bus rather than the fabric, so the fabric models a small minority of the traffic it should carry; there is no distance between tiles and no bytes-to-time on the fabric; and I14 is not modelled at all.** Its own instruction: "**Do not read the topology built below as a statement about the architecture.**" |
| **S38** `[GAP]` | **No SST number may be compared to a ChampSim number** until the baseline reproduction step is done (stock GAPBS BFS, no NMFC, against ChampSim's 197,753,293-cycle baseline). The SST docs say so themselves: "no cycle count taken against [the loopback stub] means anything at all"; "no timing measured here means anything yet." |

### D6 — What SST does build, and where it LEADS ChampSim

Recorded so the checklist is not read as "SST is behind on everything":

- **MOESIF coherence at the L2↔LLC fabric**, with `O` and `F` actually used — ChampSim
  has **no coherence protocol at all** (it substitutes partitioning, read-only-only
  replication, and word-granular local atomic locks).
- **Migration charged at 72 bytes** on both links, over the same links coherence and line
  fills use — the thing ledger L7 says ChampSim does not model.
- **LLC banking held equal to the device's per-rank bank count**, so a cache bank and its
  DRAM bank are the same partition (ledger L5).
- **A barrel function core with a per-context fetch buffer, a shared 64-entry BTB, and
  sleep-on-one-outstanding-load** — the canon core shape of Part H, which ChampSim's
  scoreboard model deliberately differs from.
- **Vanadis OoO host with NMFC as a RoCC accelerator**, one binary decoding on both hosts
  (I.9).
- **A store buffer on the host L1**, measured: folds 873,207 stores into 152,944 writes,
  5.7× less L1→L2 write traffic, **and 58.9138 ms either way** — the pessimism was
  entirely on one link and the write-back L2 absorbed it.

### D7 — Historical failures recorded in the SST tree, useful as cross-checks

These are not divergences; they are the scars, and each is a test the canon should keep:

- **Deadlock at cycle 9,100,426**, cited three times as the hold-and-wait failure: an FTU
  entry held while waiting for a tile context; a tile slot held while waiting for fabric
  space; a context holding one atomic word while blocking on another.
- **FIFO retirement at ring depth 4096**: queue-empty 38.4% → 43.1%, cycles **+9.3%**.
- **Three disagreeing implementations of the partition** existing at once; and a zero mode
  bit read as "everything is STANDARD", reintroducing a 64-byte partition.
- **An assertion that never executes is how 75.3% of accesses came to route to a tile
  their address never named.**
- **A TLB indexed by the low bits of a sparse key: 66 hits against 14,260 walks, and
  identical results at 64 and 4096 entries — which was the tell.** Hashing fixed it to 12
  walks and 36 references.
- **A per-page owner cached alongside the translation**: a function core summing a
  block-spread array **migrated three times instead of the sixty-odd the mapping calls
  for**, because every access after the first reused one entry. The tile must be derived
  from the frame and never cached beside it.
- **`.rodata` left striped** → every context on every other tile migrated on its first
  constant.
- **A store issued to the virtual address rather than the frame**: invisible while the
  mapping is the identity — which is **every single-tile configuration** — and at two
  tiles the stored value simply disappeared.
- **`grain_region` computed rather than read** — "the grain after the text" is a guess
  about section order that stopped being true the moment a section was added. **Read the
  symbols.**
- **A binary linked at one grain size run on a machine with another** put two page types
  in one grain; the grain check caught it. **G is not a constant** (E.3).
- **RoCC executing at push time** → "a JOIN reading a FORK's operands". Issue at ROB head.
- **Two page tables that are copies built from different parameters** — the fabric could
  move a grain into a slot the tiles refuse, and **both frames would be legal addresses,
  so nothing would say so.** Every page-table builder must be handed the same parameter
  dict, and a missing memory size must be fatal at construction rather than defaulted,
  "because the failure it prevents is invisible at run time".
- **Identity-mapped frames colliding with allocated ones** — the header had said "above
  anything the program image occupies" from the beginning and nothing checked it.

---

## APPENDIX 3 — HOW TO USE THIS DOCUMENT

1. **Before touching either implementation, read Part A and Part B.** Not "read once at
   the start of the session" — the record shows the design being dropped repeatedly
   *within* a single session, after being read and after being corrected.
2. **Before proposing a mechanism, check Part P.** If it is there, it is rejected, and
   the reason is in the same row. **Before adding a mechanism at all, check whether the
   design already names one. It usually does.**
3. **When ChampSim looks like it needs something new**, first check whether what you are
   looking at is a simulator convenience — an encoding, a policy enum, a trace record —
   rather than the machine. That is the recurring failure: reasoning from the
   implementation back to the architecture.
4. **When quoting a number, quote its provenance**: which core model, which DRAM model,
   which decomposition, which capacity regime. Several numbers in the record are correct
   and mutually incomparable.
5. **When the sources disagree**, the order is session log > ChampSim > docs > SST, and
   **inside the session log, newer overrides older without exception.** Appendix 1 records
   the disagreements already found; add to it rather than re-deriving.
6. **THERE ARE NO QUESTIONS FOR THE USER. THE COUNT IS ZERO.**
   **`RULINGS NEEDED FROM THE USER` at the FRONT of this document is now a RECORD of
   rulings, not a request for them** — `O1`, `O3`–`O7`, `O9`, `O12`, `O15`, `O16`: **TEN
   items, ALL RULED by the user on 2026-09-03**, each with his words quoted verbatim and
   the body sections it is applied at named. Read that; this item is the build-order view
   of the same list, and every line of it is now a *decision to implement* rather than a
   question to ask.
   [REWRITTEN THREE TIMES — **the user ruled on 2026-09-02 and closed thirty-four of the
   forty-nine questions this item used to index; six more were then closed IN EDITING**
   because they were never rulings to ask for (`O11` and `O10` were facts to look up,
   `O14` and `O13` were forced by rules this document had already adopted, `O8` was a
   document-internal inconsistency, `O2` was an editing chore); **one was ADDED**
   (`O16`, `RESUME`'s privilege level, which R20 left as a question and which no `O`-row
   carried); **and the user then ruled on all ten survivors on 2026-09-03.** The check is
   `grep -nE '^\| \*\*O[0-9]+\*\* \|'` over the front-matter table, which yields
   **O1, O3, O4, O5, O6, O7, O9, O12, O15, O16 — ten rows, with deliberate gaps** where the
   closed-in-editing rows were, so the body's existing citations stay valid, **and every
   row now reads RULED.** **If that table changes, this item changes with it — the table is
   the authority, this is a pointer.**]
   In the order they change what gets built — **all ruled, all implementable now**:
   - **O1 (L38) — the vmem default for an unhinted grain: the OS's FREE CHOICE.** "*Map it
     wherever was most convenient*"; **no partition semantics attach to the VA.** All
     placement work below it is now well-defined.
   - **O4 (L46) — the RISC-V subset: `RV64IMAFD`.** The admission test checks that subset
     **and counts liveness in BITS**; `F`/`D` values pack into the **same 512 bits** under a
     second register namespace, and add **zero** bytes to a migration.
   - **O3 (L43) — the canon assigns `funct7`/`funct3` NOTHING.** Field values are
     implementation choice; `nmfc_isa.h`'s are recorded in SELECTED CONFIGURATION.
   - **O16 — `RESUME` is PRIVILEGED.** Fault resumption is a kernel operation; every
     `[USER TO CONFIRM]` tag is gone.
   - **O5 — three message classes on the one fabric: COHERENCE, MIGRATION, FILL**,
     per-destination queues, **coherence strictly first, then migration and fill at EQUAL
     WEIGHT** — the precondition of invariant 11's parity.
   - **O9 — the directory is an EXACT BIT VECTOR over cores and tiles, INCLUSIVE, with
     BACK-INVALIDATE**, sized for **up to 32 memory tiles** under heavy traffic.
   - **O7 — a fatal fault closes every outstanding FTU entry with a ZEROED file and an
     ERROR FLAG**; `JOIN` returns the error immediately and **nothing waits on the user
     program.**
   - **O6 — a spill goes to the tile holding the NEXT-LARGEST CLUSTER of the same vtile**;
     if none, the **least-loaded**. Warn, never error.
   - **O12 — the block-granular BTB with a bimodal bit is ADOPTED**, fetch-side only: one
     speculative fetch, never an execution. **The never-mispredicts caveat still stands.**
   - **O15 — Parts G, K, L and N are HISTORICAL OBSERVATIONS.** Configurations
     unreproducible from git; labelled as such; **ChampSim stays frozen** (R3).

   **Closed in editing, and recorded in the front matter's CLOSED IN EDITING table rather
   than here**: `O11` (row 3 counts **host instructions only** — read out of
   `nmfc_host_core.h:603` and `function_core.cc:698, 1040`), `O14` (5.3× **retired** under
   Appendix 3 item 8), `O13` (invariant 8 is the **claim shape**; the 5.67× lives in N.2),
   `O8` (the context states are **FREE, READY, RUNNING, BLOCKED, DONE**, `MIGRATING` a
   transition), `O10` (the `:253` fair-share denominator is a **recorded defect**, and the
   unit is a tuning parameter under R21), `O2` (**`D:line` stays the citation**).

   **And the standing ones are CLOSED, not open**: L3 (R1, delete `op::SPAWN`), L4 (R2,
   default router), L5 (R3, banking derived + ChampSim frozen), L7 (R4, SST is correct),
   L14/L15 (R5, a suspected bug), L25/L26/L29/L36/L37/L40 (closed by the freeze),
   L32 (the stress workload works), L35/L49 (R12), L44 (twelve), L47 (R14), L48 (R15),
   L8 (the geometry ruling). **See Appendix 2 D0 for what the freeze suspends.**

7. **The immediate MECHANICAL actions**, in priority order:
   - **Fix `MEMORY.md:2` and `nmfc/.claude/nmfc_invariants.sh`** — they are injecting the
     rejected design into every agent (ledger L2). This is mechanical, and it is the
     highest-leverage single change in the list.
   - **Fix the three ChampSim source comments that teach the rejected design** (ledger
     L2, tier 2 and therefore above every document): `src/nmfc/function_core.cc:17-20`
     (routing on the virtual address, stated unconditionally over conditional code),
     `inc/nmfc/nmfc_vmem.h:118-124` (the partitioned table as the normal case), and
     `inc/nmfc/tile_router.h:58-62` (`VIRTUAL_FIRST`/`TRANSLATE_FIRST` as a live pair).
   - **Regenerate the stale configs — 31 of 33, not 2** (L27, L28, L20): only
     `phys_ft/nmfc_4tile.json` and `ram/nmfc_4tile_ramulator.json` carry the DRAM mode
     ports; `nmfc_4tile.json` additionally sizes its FTU 4:1 below what the generator
     would emit; the ramulator one exits at construction on a grain-bits contradiction and
     has no `tile_router` child at all; and **`make_config.py:619-620` hardcodes
     `grain_bits = 21` under `--dram default`, which NO geometry in the tree derives
     — derive it instead.**
   - **Set `placement_policy` / `placement` explicitly everywhere, and stop shipping the
     `first_touch` alias** (L36) — today the entry PC is read on one of four policy arms,
     no shipped config selects it under `PHYSICAL_ROUTER`, and four directory names
     advertise a policy `function_fabric.cc:52-57` says was removed.
   - **Replace `annotate.cc`'s slot-count admission test with `peak_bits <= 512`** (L30) —
     it currently rejects admissible functions and admits none for the right reason.
   - Fix DESIGN.md §30.2's grain-formula bank count wording (L8), and its "all eleven"
     descendants (L11).
   - **Add a supersession note at DESIGN.md `D:2239`** — §23.7 still teaches `funct3` as
     the group selector and context-register indices in five-bit fields, which **§26.6
     D:2907-2917 replaced**, and DESIGN records the replacement nowhere. A reader who
     stops at §23.7 builds the encoding that cannot survive RoCC (**L45**; not a user
     ruling, a tier-3 documentation defect of the same class as L11 and L13).
   - **Run the branch-honesty sensitivity sweep DESIGN §12 D:1016 asks for.** The function
     core replays resolved control flow and never mispredicts; `FLAG_TAKEN_TARGET` plus
     the fetch bubble is the knob, ChampSim ships it at `fetch_bubble: 1`, and **the sweep
     is not in the record** — so every function-core speedup here carries an unmeasured
     caveat (O.4, Appendix 3 item 8).
   - **Report `remote-walk rate`, `largest allocatable NMFC run per tile`, `spill rate`
     and `fire-and-forget share`** — four instruments DESIGN §10 D:976-987 registers and
     nothing emits. The first makes invariant 5's legitimacy clause measurable **without
     ungating any assertion** (L26, L37); the last makes L29's divergence visible as a
     number (O.4).
   **Added this revision, in the same priority order:**
   - **~~Rule L38 — the grain-placement question.~~ RULED — user ruling 2026-09-03 O1.**
     Everything in this tree places data by `(va >> grain_bits) % num_tiles`. **That is
     PERMITTED as an allocator default** — "*Unhinted grains are up to the OS/hardware to
     place … the OS could map it wherever was most convenient*" — **and forbidden as a
     router**, because no partition semantics attach to an unhinted grain's virtual
     address. **The mechanical action is R2's relabel, not a deletion:** mark the
     VA-derived placement a **control** (F.10), and make sure nothing downstream derives a
     tile from a VA and depends on the answer. Placement work below it is now well-defined
     (A.4a, F.3, F.8).
   - **Select `NMFCMinimalist` on the DDR5 devices** — the mapper #58 asked for is **built
     and on by default**, and only the HBM3 file selects it. One line in
     `tile_ddr5.yaml:26` and the four `per_tile/` files, then measure it (E.5, L39).
   - **Make GRAIN expressible** — a third manifest token, a `region::GRAIN`, and a branch
     at `annotate.cc:370`. The receiving side already works. **Until then no result in the
     tree is a measurement of siloing** (C.3, L41).
   - **Derive the workload's grain from the same source the config uses.**
     `bfs_nmfc.cc:46` hard-codes 20 for the pool and bitmaps while `--grain-bits` reaches
     only `parent`, so `--grain-bits 21` builds a binary with two grains in it (E.3, L20).
   - **Ungate the congruence assertion for `TRANSLATE_FIRST`, or record the function
     core's instrument as invariant 9's enforcement** (I9, L40).
   - **Audit every derived statistic against the site that increments it.** Two are already
     known wrong — `DISPATCH BLOCKED` (always zero, L21) and `GRAINS PER TILE` (counts
     remaps, L42).
   - Work Appendix 2 as a checklist, starting with **S1/S2** (the memory fork and continue
     forms are fatal), **S20** (translation counters are 2× high, so every SST translation
     number is currently wrong), **S9** (the migration admission asymmetry can fatal a
     tile), and **S15** (the two hosts disagree on a closing rule).

8. **Numbers that must never be quoted bare**, because each is correct only with its
   provenance. **[ADDED, and it applies to EVERY function-core number in this document
   rather than to one of them:` the function core in both models `replays resolved control
   flow, so it never mispredicts` — DESIGN §12 D:1016. `FLAG_TAKEN_TARGET` plus a
   configurable fetch bubble is the honesty knob, ChampSim ships it (`fetch_bubble: 1`),
   **and the sensitivity run DESIGN asks for is not in the record.** So a function-core
   speedup carries TWO caveats, not one: which core model (M.3) and the absent
   branch-honesty sweep (O.4). `]**
   The list: **5.67×** (which core model — M.3, and the branch caveat above); **77.9% / 20.2% / 6.19×** (which
   decomposition — they are SPAWN, N.6); **0.0015 migrations/instr** (spawn, as a target
   only — K.4); **any migration count** (which run — L34); **any FTU occupancy figure**
   (outstanding vs returned-and-unjoined, whether the run was the suspect stress run, and
   that the headline config undersizes the unit 4:1 — L32, L33, L27, H.9a);
   **any capacity claim** (which slice size — and note **no checked-in config has the
   4 MiB slice Part L and N.1 were measured at**, L28c); **7.38×, and NOT 5.3×, which is retired under item 8 of this list because no tier states its unit** (they are
   different quantities — A.7, N.5); **2.2–2.3 cycles** (translation cold start, not the
   fabric hop — F.7). **Added this revision:**
   - **the context register file: 64 KiB, not 8 KiB.** #191's "8 bytes for 1024 contexts
     is 8 kiB" prices **one lane**; the file is 64 B × contexts. Size from 64 KiB, argue
     from the doubling (I2, H.3).
   - **the chase decomposition's migration rate: 0.7428/instr on GAP BFS, 0.384 on the
     synthetic scattered set.** "**0.38 = three quarters of all work**" is two rows
     collapsed into one impossible sentence (K.3).
   - **"the measured pass" on #291's ceiling: name the CLAUSE.** The count clause passes
     at 0.76/memop; the legitimacy clause is unmeasured (I5, J.4, L37).
   - **a bank SWEEP is `row_bytes × banks_per_channel`, a device property — 256 KiB on the
     reference device, not a fixed 512 KiB** (E.5, S18).
   - **`grain_bits`: the default controller and ramulator DDR5 require 20, ramulator HBM3 requires 18, and NOTHING derives 21; 31 of 33 configs declare 21**
     (D.5, E.3, L20).
   **Added this revision:**
   - **the §29 inversion: QUOTE A ROW.** *edges-duplicate* = **4.96× fewer migrations,
     +38.2%** (125.8 vs 91.0 ms); *edges-duplicate + first-touch* = **6.93×, +36.8%**.
     **"Five sixths" matches neither row; "5–7×" spans both; "seven times fewer and 38%
     longer" takes one number from each** (Part L, N.3, K.3's rule).
   - **"banks per channel": say WHICH.** 32 per rank on DDR5; **64 flat per channel on
     `tile_ddr5.yaml`** (because we chose `rank: 2`, a non-JEDEC choice); 32 flat per
     channel on the `--dram default` controller. The user's newest word (#141) says 32 per
     channel (E.4a, L8).
   - **"`--walk-routing fabric` configs": 15, not 13.** 13 is the unrelated `first_touch`
     census and the two sets differ (F.6's three-group census, L26).
   - **`ADAPTIVE_ROUTER`'s `GRAINS PER TILE` / `grains_per_tile`** — it counts **remaps**,
     not grain placements (A.4, L42).
   - **`INCONGRUENT: n of m`** — use **`INCONGRUENT among nmfc-stamped`** for invariant 9;
     the bare figure legitimately counts STANDARD pages, whose tile is a block field (I9,
     L40).
   - **the per-context state budget: ~87 bytes**, not 64. 22 KiB/tile at 256, **87 KiB at
     1024**, where it competes with the LLC slice; **1024 is ~4× over-provisioned** (H.2,
     H.3).
   - **"the channel is worth 19.2 GB/s"** — 19.2 is the **subchannel peak / denominator**;
     **17.42 GB/s** is what this stream is worth on this device, and the channels are idle
     **39%** of the time (N.8).
   - **"the ring"** — a host-side fork-loop pattern over the output slot pool, **not
     hardware**; and **depth = `NMFC_LEVEL` = `LEVEL` = outstanding invocations**, one
     quantity under four names (H.9b, R33, R34).
   - **any "round-robin vs first-touch" placement comparison: name the mechanism.** In the
     shipped configs `first_touch` is a redirected alias and `PHYSICAL_ROUTER`'s placement
     is a counter (A.4, L36).

---

*End of CANON.md. Every assertion above carries a source. Where a source is a
transcript item, the number and timestamp locate it in
`0906c103-1f73-4126-961b-1d122973881b.jsonl`; where it is code or a document, the
`file:line` locates it. Nothing here was taken from the SST tree except Appendix 2 and
the rows explicitly labelled tier 4.*

*Two honesty notes on that claim. **(1)** One Part-P row — **R55** — has no tier-1 or
tier-2 citation; its only source is a model-authored memory note, which the
prior-sessions extraction warns is tier 3 ("these files are model-authored ... They are
not Tier 1"). It is flagged in place rather than silently kept. **(2)** Where this
document draws a conclusion the user did not state — the four-way page-type split in
C.3, the `FORK`→`FORKQ` assignment in I.2, the tile-id leakage argument in I.7 — it says
so at the point of the claim and names whose authority it is on. **Anywhere a bracket tag
reads `[AUTHORITY CORRECTION]`, an earlier revision of this document had an OLDER tier-1
statement overriding a NEWER one, which #307 forbids without exception; the correction
names both messages and their timestamps so the reader can check the ordering.***
