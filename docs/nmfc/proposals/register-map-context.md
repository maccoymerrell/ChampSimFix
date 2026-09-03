# PROPOSAL WORKSPACE — HOW A REGISTER NAME BECOMES BITS IN THE 512-BIT CONTEXT

**Status: NOT CANON. This file is the RECORD GATHERED, plus the fit-list and the
constraint-list a proposal must satisfy. It proposes nothing. Nothing here may be
cited as a decision.**

**What is being decided.** Given that a context is 512 bits, bit-packed, and that a
function core executes `RV64IMAFD`, *what does the 5-bit register field in an ordinary
RISC-V instruction mean?* On a stock core it names one of 32 XLEN-wide registers. Here
there is no such file, and the answer must come from **the instruction alone**.

**The candidate on the table is the user's option 2**, verbatim (2026-09-03):

> "clever aliasing system -> all ISA regs map to certain bit ranges inside the 512-bit
> regfile, so utilizing a float reg implies the type, while the number of the reg implies
> the slice."

**The candidate already REJECTED is the per-function register map**, user ruling
2026-09-03, verbatim:

> "I really don't like your idea. It introduces a third piece of memory every context
> needs. So now we have the map, instruction, and potentially data that must be referenced
> all at the same time. That frankly seems foolish."

---

## §0 THE ONE THING TO READ FIRST — THE REJECTED MECHANISM IS ALREADY IN THE TREE

**The per-function register map the user just rejected is not a new idea being floated.
It is written into DESIGN.md at tier 3 and BUILT in the SST tree at tier 4.** Any
proposal that replaces it is also a *deletion*, and the deletion has to be stated.

**Tier 3 — `docs/nmfc/DESIGN.md` §25.7, D:2560-2567, verbatim:**

> "The core therefore resolves a register operand through the function's layout rather
> than through a fixed register count. That layout is **per function, not per context**:
> many contexts run the same function, so it is one small table entry beside the
> instruction cache, indexed by the function a context is running. It adds nothing to the
> 512 bits and does not scale with `C`.
>
> Deciding the layout is the compiler's problem and is not solved -- §24 step 5. The
> architecture's obligation is only to not prevent it."

**Tier 4 — `/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCRegLayout.h`, header comment, verbatim:**

> "A context is 512 bits (§25.7). It is not eight registers, or sixteen, or any other
> count: the division is decided per function at compile time, and the core resolves a
> register operand through the function's layout rather than through a fixed register
> file. That is why the admission test in §4.1 is a test on bits.
>
> The layout is **per function, not per context** -- many contexts run the same function
> -- so it costs one table entry beside the instruction cache and does not scale with the
> context count."

and its two structures, verbatim:

```c
/// Where one architectural register lives inside the 512 bits.
struct RegField {
  uint16_t offset = 0;  ///< bit offset into the context
  uint8_t  width  = 0;  ///< bits; 0 means the layout does not define it
};

/// One function's division of its 512 bits, indexed by RISC-V register number.
struct RegLayout {
  static constexpr uint32_t NUM_NAMES = 32;  ///< a RISC-V register field is 5 bits
  RegField field[NUM_NAMES]{};
  bool     hasZero = true;  ///< x0 reads as zero and costs none of the 512 bits
```

**And the indirection is on the decode path today** —
`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCTile.cc:460-475`, under the banner comment
`// registers, always through the function's layout (§25.7)`:

```c
uint64_t NMFCTile::readReg( TileContext& c, uint32_t r ) const {
  if( r == 0 && layout_.hasZero )
    return 0;
  if( !layout_.defines( r ) )
    illegal( c, 0, "reads a register the function's layout does not define" );
  return c.regs.read( layout_.field[r] );
}
```

**The table is per tile, and it is the "resident-function table":**
`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCTile.h:448-450`, verbatim:

> "/// One entry per resident function (§25.7). There is one until a compiler
>  /// emits layouts; the lookup exists so that adding more changes nothing else.
>  `RegLayout layout_;`"

**So the ruling has three consequences that are not optional:**

1. **DESIGN §25.7 D:2560-2564 is overruled at tier 1 and must be marked.** Its "one small
   table entry beside the instruction cache" is exactly the "third piece of memory" the
   ruling names. The ruling is newer and higher-tier.
2. **`NMFCRegLayout.h`, `NMFCTile::readReg`/`writeReg`, and `NMFCTile.h:448-450` become a
   divergence** — the same status Appendix 2 gives `S5`/`S6` — the moment a replacement is
   adopted. They are not evidence for the rejected design; tier 4 "never decides anything".
3. **The replacement inherits `RegLayout`'s one genuinely load-bearing behaviour**: a
   register the mapping does not define is a **hard error**, not a silent zero. See
   constraint C14.

---

## §1 THE 512-BIT RULE — WHAT THE CONTEXT IS

**CANON Part B, I2 (CANON-DRAFT.md:754), verbatim:**

> "**I2 — 512 bits in, the same 512 bits out.**
> The whole register file returns on completion. Register positions carry no meaning
> across the boundary; the join knows how to interpret what came back. A function needing
> more live values than the file holds **cannot be offloaded**."

**Its `[SHARPENED]` block, CANON-DRAFT.md:765-772, quoting user #232, 2026-09-01T05:44:29Z,
verbatim:**

> "**512 bits is not 8 registers.** 'The regfile for each context is 512 bits. Meaning
> that we are not limited to 8 8-byte regs. It could be 16 4-byte regs, 64 1-byte regs, or
> ANY combination. **Bit-packing is the name of the game. This needs to be handled
> compile-side** (I doubt the compiler understands reducing the width of an operand can
> yield a larger regfile).' Repeated as a correction at #238: 'Once again, NO. 512 bits of
> context. The context is not 8 regs. Why do you keep reverting to that?'
> The admission test is therefore a test on **bits**, not on a count of registers."

**Restated as a boxed rule at I.0 (CANON-DRAFT.md, Part I), verbatim:**

> "**THE 512-BIT RULE (user #232 2026-09-01T05:44:29Z, restated as a correction at #238,
> and restated AGAIN as a correction on 2026-09-03): the context is 512 BITS, BIT-PACKED.
> It is NOT eight 64-bit registers, not eight lanes, not `x1`–`x8`, and not 'the integer
> file'. '*Bit-packing is the name of the game.*' '*Why do you keep reverting to that?*'**"

**Why 512 and not more — user #191, 2026-08-29T23:48:57Z, quoted at I2, verbatim:**

> "The regfile size is non-negotiable. 512 bits is the max, it is the maximum register size
> in any ISA. We can only return one value, and this has direct impact on STATE that is
> maintained in each nmfc core. 8 bytes for 1024 contexts is 8 kiB. Double is 16 KiB. Two
> cycles to transmit regfiles, not one. 8 KiB -> 16 KiB impacts regfile latency, it impacts
> usability. ... **64 bytes is the natural amount to be able to transmit**, perhaps the
> local regfile can be larger (that is, the return and start block is only half the
> regfile). But that impacts migration, it impacts state requirements, latency, and
> complexity."

**H.3 (CANON-DRAFT.md:5101), on the "8 regs" gloss, verbatim:**

> "**The 'about 8 regs' clause in the quotation above is a GLOSS, and it is the exact
> formulation invariant I2 exists to stop.** It is quoted here because the user wrote it,
> not because it is the rule. ... **Read the '8 regs' line as 'at 64 bits each, that is 8
> of them' — an arithmetic illustration of one packing, never the register file's
> structure.**"

**H.3's four-way disambiguation of the number 8** (CANON-DRAFT.md:5101 table) — reproduced
because a proposal that says "eight" will be read against it:

| the number 8 | what it is |
|---|---|
| 512 bits ÷ 64 = 8 | one *possible* packing. **Not its structure.** |
| 8 context registers `ctx0`–`ctx7` | host-side architectural registers, 512 bits **each**, per software thread — 4096 bits in total |
| `MAX_FUNCTION_REGS = 8` | ChampSim's trace-format constant. **An encoding constant.** |
| "no more than 8 instructions" | an assumption the user **rejected** |

**DESIGN §25.7, D:2554-2558, verbatim:**

> "**A context is 512 bits.** It is not eight registers, or sixteen, or any other count.
> How the 512 bits divide is decided per function at compile time -- eight 64-bit values,
> sixteen 32-bit, sixty-four 8-bit, or any mixture -- which is why §4.1's admission test is
> a test on *bits*, and why narrowing an operand is what buys a larger file."

**DESIGN §23.6, D:2183-2187, verbatim:**

> "**A context register is 512 bits, not eight registers.** How those bits divide is the
> callee's business, decided per function: eight 64-bit values, sixteen 32-bit, sixty-four
> 8-bit, or any mixture. Bit-packing is the point -- narrowing an operand is what buys a
> larger register file, and §22's admission test is a test on *bits* for exactly that
> reason."

**Where the PC lives — DESIGN §25.7, D:2574-2577, verbatim:**

> "The program counter is separate from the 512 bits, which invariant 11's 72-byte
> migration confirms arithmetically: 64 bytes of register file plus an 8-byte program
> counter."

---

## §2 NO STACK, NO SECOND FILE, NO PER-CONTEXT MODE STATE

**I7 (CANON-DRAFT.md:1045), verbatim:**

> "**I7 — The function core has a register file and no stack.**
> A function that spills cannot run. **Check the disassembly, not the source.**"

and its consequences, verbatim:

> "- **No frame pointer.** `s0`/`fp` is not established, because establishing it is a stack
>    write.
>  - **No call from inside an offloaded function.** `jal`/`jalr` writes a return address; a
>    function core has a register file and no stack to put one on.
>  - **A ninth argument is inadmissible.** RV64's calling convention passes eight in
>    `a0`–`a7`; the ninth arrives on the stack.
>  - **Any `sd`/`sw` to a stack slot is a spill, and a function that spills cannot run.**"

**I.7 item 3 (CANON-DRAFT.md:6345), the newest and most directly binding passage in the
whole record, verbatim:**

> "3. **A SEPARATE FLOATING-POINT REGISTER FILE IS DELIBERATELY ABSENT — and this is new,
> because `F` and `D` are new (user ruling 2026-09-03 **O4**, '*I think we want float, so
> C*').** On a stock RV64 core, `F`/`D` bring `f0`–`f31` as **their own file**. **On a
> function core they do not**, because a context is **512 bits, bit-packed** (invariant 2,
> the 512-bit rule at I2/H.3) and a second file would be a second context. `[derived from
> ruling O4]` **`f0`–`f31` and `x0`–`x31` are two NAMESPACES over the same 512 bits**; the
> compiler's packing decides which bits each name resolves to. There is no `fcsr`, no
> rounding-mode state and no floating-point exception state either — **that is per-context
> state the 512 bits do not budget for**, and adding it would break invariant 2 and grow a
> migration past 72 B (I.0, invariant 11). **A function needing dynamic rounding modes is
> in the same position as a function that spills: it cannot be offloaded.** Recorded here
> rather than in Part P because it was never proposed and rejected — it is a consequence of
> O4 that has to be stated before someone implements `F` the ordinary way."

**This passage is doing three separate jobs and all three bind a proposal:**
- it *already states the two-namespaces-over-one-file structure the user's option 2 asks
  for* — so option 2 is **not new architecture**; what is new is that the mapping becomes
  **fixed by the ISA** instead of chosen per function;
- it forbids `fcsr` and every other per-context mode word, on the general ground that
  per-context state outside the 512 bits is forbidden — the same ground the 2026-09-03
  ruling rests on;
- it makes "cannot be expressed ⇒ cannot be offloaded" the *standard remedy*, so a
  proposal is allowed to leave things inexpressible.

---

## §3 THE SUBSET, AND WHAT AN OPCODE ALREADY DETERMINES

**I.0 (CANON-DRAFT.md:5816), verbatim:**

> "**THE SUBSET IS RULED: `RV64IMAFD`.** `[RULED — user ruling 2026-09-03 **O4**, verbatim:
> '**I think we want float, so C.**' Option (c) of that row was `RV64IMAFD`. Ledger **L46**
> is now closed in both halves — the family by R11, the subset by O4.]`"

**And the naming consequence, I.0, verbatim:**

> "**THE ONE THING O4 DOES CHANGE ABOUT THE FILE IS ITS NAMING, NOT ITS SIZE.** A RISC-V
> encoding names `f0`–`f31` in fields that are *separate* from `x0`–`x31`, so an
> `fadd.d f3, f1, f2` and an `add x3, x1, x2` reach the compiled context through **two
> register namespaces**. `[derived from ruling O4]` **Those two namespaces are a naming
> convention over the SAME 512 bits, not two files.** The compiler's packing decides which
> bits an `f`-name and which bits an `x`-name resolve to, exactly as it already decides that
> for two `x`-names of different widths. **An implementation that builds a separate
> floating-point register file has built a second context, has broken invariant 2, and has
> made a migration bigger than 72 bytes.**"

**Note for the proposal, stated as a fact about RISC-V and not as a citation:** in
`RV64IMAFD` the **opcode already carries the type and the operation width** — `add` vs
`addw`, `fadd.s` vs `fadd.d`, `flw` vs `fld`, `fmv.x.w` vs `fmv.x.d`. Nothing in the base
ISA infers a type from a register number. **What standard RISC-V cannot express is a
register NARROWER than XLEN/FLEN** — there is no name for "the low 12 bits of x5". That
gap, and only that gap, is what a nameable-slice scheme has to close.

**The moves between namespaces that already exist and cost nothing to keep:** `fmv.d.x`,
`fmv.x.d`, `fmv.w.x`, `fmv.x.w` are in `F`/`D` and are therefore in the ruled subset. **If
an `f`-name and an `x`-name are defined to be the same bits, these become identity moves.**
That is a property to state, not to assume: it is a design choice the proposal makes, not
something the record already settles.

---

## §4 THE ACCESS APERTURE — CXW/CXR, AND WHAT IT IS *NOT*

**I.8 (CANON-DRAFT.md:6442), verbatim:**

> "**Eight of them, `ctx0`–`ctx7`, 512 bits each, per software thread.** This is I1's
> '512-bit vector register that *is* the callee's register file', made real."

**User #231, 2026-09-01T05:37:55Z, quoted at I.8, verbatim:**

> "**We need those 512 bit regs, so we might just need to implement a specialized set of new
> regs. Far simpler than implementing a vector extension.** The only tricky part is making
> sure the wider regs interact nicely with the existing instructions that may interact with
> them, and **we may need a subset of bit-manip instructions added so that values can be
> retrieved/set.**"

**User #233, 2026-09-01T05:47:14Z, verbatim:**

> "**We need to make sure EXTRACTION from the regs is possible. Regular bit manipulation
> can take you the rest of the way. Alignment is something handled by the existing ISA.
> Let's not overdesign.**"

**The two instructions, I.8 / DESIGN §23.6 D:2190-2192, verbatim:**

```
CXW      cD, lane, rS      cD[lane] <- rS     one 64-bit lane
CXR      rD, cS, lane      rD <- cS[lane]
```

**I.8, verbatim:**

> "**The lane is an access granularity, not the register's structure, and these two are
> complete.** Once 64 bits move in and out, any packing within them is reached with the
> shifts and masks RV64I already has, and a field straddling a lane boundary is two moves
> and the same arithmetic. **A bit-field insert and extract carrying an offset and a width
> was considered and dropped: it would duplicate instructions that exist.**"

**THE HOST HAS NO VECTOR UNIT, AND THIS CORRECTS AN ASSUMPTION.** I.8's prior-art check,
verbatim:

> "**Prior art, checked and negative.** RISC-V has no such register. **Rev implements no
> vector extension** — its `RegVEC` class and `RVVTypeOpv` format are declared and used
> nowhere — nor does Vanadis, nor does anything in sst-elements; Vanadis's RoCC interface
> passes only **128 bits** — `RoCCCommand(RoCCInstruction*, uint64_t rs1, uint64_t rs2)`
> and `RoCCResponse(uint8_t rd, uint64_t rd_val)`,
> `src/sst-elements/.../vanadis/rocc/vroccinterface.h:52-72`, i.e. two 64-bit operand
> values in and one out. **That is why a context register cannot be a RoCC operand and is
> named by a number instead** (I.9's operand convention). RVV with `VLEN=512` would be the
> standards-compliant answer and means writing a vector unit from scratch first. **A small
> dedicated file is far less work and is what the machine actually needs.**"

> **CONSEQUENCE — RECORD THIS BEFORE ANSWERING THE HOST HALF OF THE USER'S QUESTION.**
> `V` is **not** in the ruled subset (`RV64IMAFD`, O4), and the machine has **no vector
> unit on either host**. So `vmv.x.s`, `vslidedown` and `vfmv.f.s` are **not available**
> and must not be offered as the host-side retrieval path. **Host retrieval is `CXR` to a
> GPR, then RV64I shifts and masks, then `fmv.d.x`/`fmv.w.x` if the value is wanted in an
> `f` register.** The `fmv.*` half of the provisional answer stands; the V-extension half
> does not.

**Where the lane number lives — I.9, verbatim:**

> "**The lane is in `funct7`, not a register,** because it is a constant at every call site
> and making it a register would cost an instruction to produce a number the compiler
> already knows."

and, from I.9's variant-bit notes, verbatim:

> "2. **The CTX group (`0x5`) does not use the variant field as a flag word at all.** `CXW`
> is `0x50 + 2n` and `CXR` is `0x51 + 2n`, so **bit 0 selects write-vs-read and bits 3:1
> carry the lane number `n`.** Neither `BIT_M` nor `BIT_R` applies."

**Tier 4 confirms the field budget** — `/mnt/md0/NMFC-Rev/src/nmfc/include/nmfc_isa.h`:

```c
#define NMFC_CX_LANE_SHIFT 1
#define NMFC_CX_LANE_MASK  0x7
```

— **three bits, eight lanes.** A proposal that changes the *nameable* granularity of the
context has to say whether CXW/CXR's 64-bit lane granularity changes with it. The record
says the lane is "an access granularity, not the register's structure", so the two are
formally independent; that independence is a thing to preserve deliberately, not to assume.

**The operand convention, I.9, verbatim:**

> "**The operand convention is load-bearing: every operand is a VALUE in a general
> register.** A context register is named by a **number held in a GPR**, not by a five-bit
> field read against a different file, **because RoCC hands an accelerator register *values*
> and only rd's index.** The old field form could not survive that."

---

## §5 THE RETURN BIT, AND THE TWELVE

**I.9, verbatim:**

> "`BIT_R = 0x1` — **THE RETURN BIT.** END group only: the ending message carries the
> register file (`0x31`) or does not (`0x30`). **This bit is the whole of the difference
> between the two END forms** — user ruling 2026-09-02, and it is why the base set is twelve
> and not thirteen."

**And the base set, I.9, verbatim:**

> "**`FORK.R` `FORK.M` `FORKF.R` `FORKF.M` `FORKQ` `JOIN` `JOINQ` `END`(+ret bit) `CONT`
> `CONT.M` `CXW` `CXR`.**"

> "**Plus `RESUME` (R20), which makes thirteen — and it is PRIVILEGED**"

**Encoding space left: `funct7` groups `0x6` and `0x7`, reserved for `KILL`, mailboxes and
`RESUME`** (I.9; DESIGN §23.7 D:2246-2247). **That is the entire budget available to any
proposal that wants a new instruction, and `RESUME` already has a claim on it.**

**And every field value is an implementation choice** — user ruling 2026-09-03 **O3**,
quoted at I.9, verbatim:

> "*I think this is just a simulator thing and not a meaningful design choice, so I say we
> describe it as implementation choice.*" — "**The canon assigns no field values at all.**"

---

## §6 MIGRATION, AND WHAT THE 72 BYTES ARE MADE OF

**I11 (CANON-DRAFT.md:1272), verbatim:**

> "**I11 — Migration moves the work instead of the data, at parity.**
> 72 bytes of register file and PC against the **64-byte line** a foreign access would have
> cost — and **the two are alternatives, never both**, so migration traffic **subsumes**
> data traffic rather than adding to it."

**I.0's arithmetic on why `F`/`D` cost nothing, verbatim:**

> "- **Invariant 11 is untouched.** A migration is still **72 B** — 64 bytes of context plus
>   an 8-byte PC — against the 64-byte line a foreign access would have cost. **Adding `F`
>   and `D` adds ZERO bytes to a migration.**"

**I3 (CANON-DRAFT.md:797) and I11 together fix where code lives, verbatim (I11):**

> "**2.2–2.3 cycles measured, with a 100% instruction-cache hit rate**, because the code is
> replicated on every channel and the departing tile's slice never held the data anyway."

> **CONSEQUENCE.** "Code is on duplicate pages, resident on every tile" is **true of the
> instruction stream** and is *why* a compile-time constant travelling with the code was
> proposed. The ruling says that is not enough: the objection is not that the map is
> unavailable, it is that it is **a third thing to reference at the same time**. A proposal
> may not answer the ruling by pointing at duplicate pages again.

---

## §7 ADMISSION — THE TEST THE MAPPING FEEDS

**K.6 (CANON-DRAFT.md:7232), verbatim:**

> "A function is admissible iff **every opcode in it is in `RV64IMAFD`** *and* its **peak
> simultaneous liveness fits in 512 bits.** Two tests, both mandatory, and until this
> revision the document stated only the second."

> "**Widths count in bits.** A 32-bit value costs 32, not a whole 64-bit slot — that is what
> '512 bits divided however the machine likes' means, and it is why the test is on bits.
> **The same rule applies to floating point under O4:** an `f32` costs 32 and an `f64` costs
> 64, in the same budget. **Nothing about a value's type buys it a slot of its own.**"

> "**An `f`-named value and an `x`-named value compete for the same bits**, because `f0`–`f31`
> and `x0`–`x31` are two namespaces over one packed file (I.0, I.7). A function holding six
> live pointers (384 bits) and two live `f64` (128 bits) is at **512 of 512** and is
> admissible; adding one live `f32` makes it 544 and it is **not**."

> "**A register that is never read is not state the join consumes, so it takes no slot at
> all**"

> "**Rejection is fatal and must not be softened.** Truncating would drop dependencies and
> flatter the scoreboard. If it does not fit, the function cannot run on this machine:
> rewrite it, split it into a `CONT` chain, or reject it."

**K.6's third wrong answer, which is the one a fixed aliasing table must not reproduce,
verbatim:**

> "**AND A THIRD, WHICH IS NEW WITH O4 AND WHICH NEITHER IMPLEMENTATION CAN GET RIGHT
> TODAY:** a function holding **four live `f64` and four live pointers** is at **512 of
> 512** and is admissible, but a slot-counting test that allocates `f`-names out of a
> separate pool from `x`-names sees **four of eight and four of thirty-two** and admits a
> function twice that size. **The pools must be ONE pool measured in bits**, because the
> register file is one file (I.0)."

**DESIGN §22, D:1928-1932, verbatim — where the bit-width comes from:**

> "**And it charged every value a whole 64-bit slot.** The file is 512 bits divided however
> the machine likes, so a 32-bit `NodeID` costs 32. Pin records partial-width views as
> distinct ids and the regmap keeps their names, so the width survives the trace and is only
> lost when canonicalising. `annotate` now carries it and reports liveness in bits."

**DESIGN §22's measured result, verbatim:**

> "With those, `nmfc_bu` takes **three** arguments -- `parent + lo`, `index + lo` and the
> frontier, so it never forms an absolute vertex index -- and measures **8 values, 480 bits
> of 512**. It is admissible, and it emits no callee-saved pushes at all. `nmfc_expand`, the
> edge-range shape, sits at 8 values and 384 bits."

**DESIGN §22's lesson, verbatim:**

> "It is that **a function's admissibility is a property of its generated code, not of its
> source**"

**DESIGN §22 rules out the escape hatch, verbatim:**

> "2. **Widen the file.** Ruled out on cost: per-context state doubles, a transfer becomes
> two cycles, and migration and latency follow."

**And the implemented tool is a slot counter, not a bit counter** —
`tools/nmfc/annotate.cc:524-527` builds a pool of `opt.num_regs` (8) slot ids, `:542-553`
allocates one whole slot per live value and calls `die(...)` when the pool empties;
`:555-559` computes `bits += reg_bits[reg] != 0 ? reg_bits[reg] : 64U;` whose only consumer
is a stderr line at `:927`. **This is ledger L30; it gates nothing today.**

**Part P R30 (CANON-DRAFT.md:8539), verbatim:**

> "| R30 | **Counting distinct registers touched, as the admission test** | Partial-width
> views are separate tracer ids (`rax`/`eax`/`al` = 3 ids, 1 register) → reported 17 and 21
> where the answer is 8, and **rejected a function holding ~480 bits that fits.** Count
> **peak simultaneous liveness, in bits** |"

> **NOTE THE SHAPE OF R30, BECAUSE A FIXED ALIASING TABLE REVIVES IT IN MIRROR IMAGE.**
> R30 rejects counting *names*. A scheme in which each name is a fixed bit-slice makes the
> nameable set finite and overlapping — so the compiler's allocation problem becomes
> *which names may be live together*, which is a **conflict-graph colouring over an
> interference structure the ISA fixes**, not a sum of widths. The admission test then has
> two candidate readings — "does the packing fit in 512 bits" and "is there an assignment
> of live values to non-overlapping names" — **and these are not the same test.** A
> proposal must say which one K.6 becomes, because K.6 currently states only the first.

---

## §8 THE COMPILER'S HALF — WHAT IS ALREADY ACKNOWLEDGED UNSOLVED

**I.8's closing paragraph, verbatim:**

> "**Open, and it is a compiler problem, not an architecture one:** a compiler does not know
> that narrowing an operand yields a larger register file, so the packing — and the
> admission decision that depends on it — is compiler work. **The architecture only has to
> not prevent it, and 512 opaque bits plus a 64-bit access aperture does not.**"

**DESIGN §23.6, D:2201-2205, verbatim:**

> "**What is not solved is the compiler's half.** A compiler does not know that narrowing an
> operand yields a larger register file, so the packing -- and the admission decision that
> depends on it -- is work for §24 step 5. Nothing here decides it; the architecture only
> has to not prevent it, and 512 opaque bits plus a 64-bit aperture does not."

**DESIGN §24 step 5, D:2288-2296, verbatim — and it is the strongest existing argument
FOR a fixed nameable set:**

> "5. **A compilation pipeline.** This is where the move off ChampSim pays off, and it may
> be cheap. Under ChampSim, admissibility was archaeology: compile for x86-64, disassemble,
> count live values, discover the compiler had pinned a register to hold the constant `1`,
> rewrite the source to trick it (§22). On RISC-V we own the ABI, so `-ffixed-x{n}` can
> constrain the compiler to exactly the register budget and a function that does not fit
> **fails to build or spills visibly**. That turns the §4.1 admission test from a post-hoc
> analysis into a build error. Try this before committing to a custom backend."

> **CONSEQUENCE.** `-ffixed-x{n}` works on **register names**, which is exactly what a fixed
> aliasing table provides and what a per-function bit-map does not. A proposal should say
> whether it keeps step 5 buildable with a stock toolchain, because that is the cheapest
> path the record has identified and it is stated as "try this first".

---

## §9 EVERY EXISTING MECHANISM THE PROPOSAL MUST FIT

Each row is a structure that exists in the record and that a register-naming scheme
touches. **A proposal is incomplete until it says what happens to each.**

| # | mechanism | where it is | what the proposal must say about it |
|---|---|---|---|
| **M1** | **The resident-function table** (`RegLayout layout_`, one entry per resident function beside the I-cache) | DESIGN §25.7 D:2560-2564; `NMFCTile.h:448-450`; `NMFCRegLayout.h` | It is the rejected mechanism. Say explicitly that it is **deleted**, and that DESIGN §25.7's paragraph is overruled by the 2026-09-03 ruling. Do not leave it standing as an alternative. |
| **M2** | **`readReg`/`writeReg` — the decode-path indirection** | `NMFCTile.cc:460-475` | What replaces the `layout_.field[r]` lookup. If it becomes a fixed decode of the 5-bit field plus the namespace, say so; that is a wiring change, not a table lookup. |
| **M3** | **`Context512::read`/`write` — bit-offset extract/insert over 8×64 storage** | `NMFCRegLayout.h` | These are storage mechanics and are **reusable unchanged** if slices are still (offset,width). Say whether the straddle path (`w0`,`w0+1`) survives, i.e. whether any nameable slice crosses a 64-bit word. |
| **M4** | **`CXW`/`CXR`, 64-bit lane, lane in `funct7` bits 3:1** | I.8, I.9; DESIGN §23.6 D:2190-2192; `nmfc_isa.h` (`NMFC_CX_LANE_SHIFT/MASK`) | Whether the host still addresses the context in 64-bit lanes while the *core* names sub-64-bit slices. The record says the lane is "an access granularity, not the register's structure", so the answer can be yes — but it must be stated, along with how a host stages an `f32`-in-a-32-bit-slot argument. |
| **M5** | **The `x0` rule** | `RegLayout::hasZero`; `readReg` line 462 | `x0` reads as zero and costs none of the 512 bits. A fixed table must reserve name 0 the same way, in both namespaces if `f0` aliases `x0`. |
| **M6** | **The illegal-register trap** | `NMFCTile::illegal`, `NMFCTile.cc:477-489` | An undefined register is a **hard fatal error** naming invariant 7, not a read of zero. Under a fixed table every name is always defined, so this check's *purpose* has to be re-homed — probably onto admission (§7) — or it is silently lost. |
| **M7** | **The admission tool** | `tools/nmfc/annotate.cc:524-559`, `:927`; K.6; DESIGN §22 | K.6 already requires it to be rewritten to compare `peak_bits` against 512 in one pool. Say what it computes under the new scheme, and whether it stays a sum of bits or becomes an assignment/colouring problem (§7's note). |
| **M8** | **The `RETC`/`ENDC` return bit** | I.9 (`BIT_R`); user ruling 2026-09-02 | The 512 bits come back **whole and uninterpreted** when the bit is set. A scheme in which the *meaning* of the bits is fixed by the ISA rather than by the function is a change to what the joiner can assume — say whether the join now knows more than it did, and be careful: I2 says "register positions carry no meaning across the boundary". |
| **M9** | **`JOIN` as a read-modify-write try** | I.8; DESIGN §23.6 D:2208-2221 | `cDST_new = ok ? ftu_payload : cDST_old`. Unaffected by a naming change, but confirm it. |
| **M10** | **`CONT` / `CONT.M`** | I.9; I10 | A successor carries the context forward. If the register map is fixed by the ISA, a successor **inherits the same map for free** — this is a genuine simplification over M1, where a successor running a different function needed a different table entry. State it. |
| **M11** | **Migration payload = 64 B context + 8 B PC = 72 B** | I11; DESIGN §25.7 D:2574-2577 | Must remain exactly 72 B. A fixed table adds nothing; confirm rather than assume. |
| **M12** | **The two host implementations and RoCC's 128-bit operand path** | I.8; I.9; `vroccinterface.h:52-72` | Every operand is a value in a GPR; a context register is named by a number in a GPR. A naming scheme for the *function core's* registers must not require the *host* to name them. |
| **M13** | **`-ffixed-x{n}` as the admission gate** | DESIGN §24 step 5 D:2288-2296 | Whether a stock RISC-V toolchain can still be constrained to the nameable set, turning admission into a build error. |
| **M14** | **Encoding space** | I.9; DESIGN §23.7 D:2246-2247; `nmfc_isa.h` | Only `funct7` groups `0x6`/`0x7` are free, and `RESUME` (privileged) already claims one. Any new instruction must fit there, and O3 says the canon assigns no field values. |
| **M15** | **Appendix 2 divergence bookkeeping** | Appendix 2 `S5`, `S6` | `S5` says the bit-level admission test is never exercised because nothing produces a non-default layout. Under a fixed ISA table there *are* no layouts to produce — say whether `S5` closes, changes, or is replaced. |

---

## §10 EVERY CONSTRAINT THE PROPOSAL MUST NOT VIOLATE

**This is the list the task asked for. Numbered so a proposal can be checked against it row
by row.** Each carries its authority; tier 1 rows are user words or user rulings and cannot
be traded.

### The hard constraint from the 2026-09-03 ruling

- **C1 — NO STATE OUTSIDE THE 512-BIT CONTEXT AND THE INSTRUCTION ENCODING.** No
  per-function table, no per-context mode or geometry register, nothing fetched from
  memory in order to decode. The register name in the instruction, together with its
  namespace (`x` vs `f`) and the opcode, must **fully determine** (bit offset, width,
  type). *Tier 1 — user ruling 2026-09-03: "It introduces a third piece of memory every
  context needs. So now we have the map, instruction, and potentially data that must be
  referenced all at the same time. That frankly seems foolish."*
- **C2 — NO THIRD REFERENCED OBJECT AT DECODE TIME.** The objection is specifically to
  *simultaneity*: instruction + data is two; a map is a third. Answering with "the map is a
  compile-time constant on a duplicate page resident on every tile" **does not answer it**
  and must not be re-offered. *Tier 1 — same ruling.*

### The 512 bits

- **C3 — THE CONTEXT IS EXACTLY 512 BITS, BIT-PACKED.** Not eight 64-bit registers, not
  eight lanes, not `x1`–`x8`, not "the integer file". *Tier 1 — I2, #232, #238, restated
  2026-09-03. I.0's boxed 512-bit rule.*
- **C4 — THE FILE MAY NOT BE WIDENED.** Ruled out on cost: per-context state doubles, a
  transfer becomes two cycles, migration and latency follow. *Tier 1 — #191; tier 3 —
  DESIGN §22.*
- **C5 — 512 BITS IN, THE SAME 512 BITS OUT, AND THE PC IS NOT AMONG THEM.** The PC is
  carried beside the file. *Tier 1 — I2, I1; tier 3 — DESIGN §25.7 D:2574-2577.*
- **C6 — A MIGRATION IS EXACTLY 72 BYTES.** 64 B of context + 8 B of PC. Nothing the scheme
  adds may travel with a context. *Tier 1 — I11.*
- **C7 — ONE FILE, TWO NAMESPACES. NO SEPARATE FLOATING-POINT REGISTER FILE.** `f0`–`f31`
  and `x0`–`x31` are naming conventions over the same 512 bits. A second file is a second
  context, breaks I2, and grows a migration past 72 B. *Tier 1 (ruling O4) + I.7 item 3,
  I.0.*
- **C8 — NO `fcsr`, NO ROUNDING-MODE STATE, NO FP EXCEPTION STATE.** That is per-context
  state the 512 bits do not budget for. A function needing dynamic rounding modes cannot be
  offloaded. *Tier 1 (derived from O4) — I.7 item 3.*

### The core

- **C9 — NO STACK. A FUNCTION THAT SPILLS CANNOT RUN.** No frame pointer, no `jal`/`jalr`
  from inside a body, no ninth argument, no `sd`/`sw` to a stack slot. A naming scheme may
  not create a spill slot by another name. *Tier 1 — I7.*
- **C10 — THE BASE ISA IS `RV64IMAFD` AND NOTHING ELSE.** No CSR access, no `FENCE`, no
  compressed encodings, no `ecall`, **nothing from `V`**, nothing from any other extension.
  The admission test rejects the first opcode outside the subset. *Tier 1 — ruling O4;
  K.6, I.0.*
- **C11 — NO VECTOR EXTENSION EXISTS ON EITHER HOST.** `V` is not in the subset, Rev
  implements none, Vanadis implements none, RoCC carries 128 bits. **`vmv.x.s`,
  `vslidedown`, `vfmv.f.s` are unavailable and may not appear in any retrieval path.**
  *Tier 1 (O4) + I.8's prior-art check.*
- **C12 — NOTHING BLOCKS.** Every action is a try paired with a probe; software spins if it
  wants to wait. *Tier 1 — I.1, #222.*
- **C13 — NOTHING SPECULATIVE IN THE EXECUTION PATH.** No ROB, no rename, no LSQ, no
  speculative execution; the one narrow exception is the block-granular BTB with a bimodal
  bit, which issues **one** fetch and never executes. *Tier 1 — ruling O12; H.1.*

### Failure behaviour

- **C14 — AN UNDEFINED REGISTER IS A HARD ERROR, NEVER A SILENT ZERO.** This is invariant 7
  enforced by the machine: a function that needs more than the file holds finds out
  immediately. If a fixed table makes every name always defined, the check must be
  re-homed onto admission rather than deleted. *Tier 4 behaviour resting on tier 1 I7 —
  `NMFCRegLayout.h` header; `NMFCTile.cc:462-474`, `:477-489`.*
- **C15 — REJECTION IS FATAL AND MUST NOT BE SOFTENED.** No truncation: it would drop
  dependencies and flatter the scoreboard. Rewrite, split into a `CONT` chain, or reject.
  *Tier 1 — K.6; Part P R31.*
- **C16 — "CANNOT BE EXPRESSED ⇒ CANNOT BE OFFLOADED" IS THE STANDARD REMEDY, AND IT IS
  ALLOWED.** A proposal need not make every function expressible. It may not, however,
  make a *previously admissible* function inexpressible without saying so. *I.7 item 3;
  I7; K.6.*

### Admission

- **C17 — ADMISSION COUNTS PEAK SIMULTANEOUS LIVENESS IN BITS, IN ONE POOL.** `f64` costs
  64, `f32` costs 32, a pointer costs 64, a `NodeID` costs 32. `f`-names and `x`-names
  compete for the same bits. Separate pools admit a function twice the legal size. *Tier 1
  — #232, K.6.*
- **C18 — NOT A COUNT OF NAMES.** Counting distinct registers touched is rejected: it
  reported 17 and 21 where the answer is 8 and rejected a ~480-bit function that fits.
  *Part P R30; DESIGN §4.1, §22.* **A scheme whose admission test is "how many names does
  it use" re-introduces R30 and must justify why it is not the same error.**
- **C19 — A REGISTER NEVER READ COSTS NOTHING.** *Tier 1 — #99, K.6.*
- **C20 — ADMISSIBILITY IS A PROPERTY OF THE GENERATED CODE, NOT OF THE SOURCE.** The
  admission test has to be run against the disassembly, and read. *Tier 3 — DESIGN §22.*
- **C21 — THE TWO REAL FUNCTIONS MUST STILL FIT.** `nmfc_bu`: 8 values, **480 of 512 bits**.
  `nmfc_expand`: 8 values, **384 bits**. Any scheme that cannot express these has failed a
  measured case, not a hypothetical one. *Tier 3 — DESIGN §22.*

### The ISA surface

- **C22 — TWELVE USER-LEVEL INSTRUCTIONS PLUS A PRIVILEGED `RESUME`.** Growing the set is a
  change to a settled count and must be argued as such. *Tier 1 — ruling 2026-09-02;
  I.9.*
- **C23 — THE ONLY FREE ENCODING SPACE IS `funct7` GROUPS `0x6` AND `0x7`, AND `RESUME`
  ALREADY CLAIMS ONE.** Reserved for `KILL`, mailboxes and `RESUME`. *Tier 3 — DESIGN
  §23.7 D:2246-2247; I.9.*
- **C24 — THE CANON ASSIGNS NO FIELD VALUES.** Every `funct7`/`funct3` value is an
  implementation choice. A proposal must not present a bit assignment as canon. *Tier 1 —
  ruling O3.*
- **C25 — EVERY OPERAND IS A VALUE IN A GENERAL REGISTER; A CONTEXT REGISTER IS NAMED BY A
  NUMBER IN A GPR.** RoCC hands an accelerator values and only `rd`'s index. *Tier 3 —
  DESIGN §26.6 D:2914-2917; I.9.*
- **C26 — NO BIT-FIELD INSERT/EXTRACT INSTRUCTION CARRYING AN OFFSET AND A WIDTH.**
  Considered and dropped: it duplicates instructions RV64I already has. *Tier 1 — #233
  "Let's not overdesign"; I.8; DESIGN §23.6; `nmfc_isa.h` comment.* **A proposal that
  re-introduces a runtime (offset,width) operand is re-opening a closed decision.**
- **C27 — CXW/CXR ARE COMPLETE AS THE HOST-SIDE APERTURE.** Once 64 bits move in and out,
  any packing inside them is reached with the shifts and masks RV64I already has; a
  straddling field is two moves and the same arithmetic. *Tier 1 — #231, #233; I.8.*
- **C28 — THE LANE IS AN INSTRUCTION FIELD, NOT A REGISTER.** It is a compile-time constant
  at every call site. *Tier 3 — I.9; `nmfc_isa.h`.*

### Provenance discipline

- **C29 — TIER 4 NEVER DECIDES ANYTHING.** `NMFC_CTX_WORDS = 8`, `NMFC_CTX_LANES = 8`,
  `x1..x8`, `Context512::WORDS`, `RegLayout::defaultLayout()` are **this implementation's
  packing on a Rev RV64 core**, not the design. The header says so itself: "Nothing may
  treat '8 registers', '8 words' or '8 lanes' as a design constant." *`nmfc_isa.h`
  `NMFC_CTX_WORDS` block; CANON front matter.*
- **C30 — A FIXED ALIASING TABLE AT ONE WIDTH IS THE SST LAYOUT AGAIN AND IS NOT AN
  ANSWER.** `x1..x8 = f0..f7` as eight 64-bit slices, with `addw` for low halves, is
  `RegLayout::defaultLayout()` with the table deleted. It satisfies C1 and violates C3 in
  substance: it makes the context eight 64-bit registers. **The design question is the MIX
  of widths among the nameable set and the aliasing pattern** — that is what the user's
  option 2 asks for, and a single-width table does not answer it.
- **C31 — DESIGN §25.7 D:2560-2564 IS NOW OVERRULED AND MUST BE MARKED, NOT QUIETLY
  DROPPED.** The document's own rule is that a superseded passage is recorded as
  superseded; an un-recorded supersession inside tier 3 is what ledger L45 exists to
  record. A proposal that replaces the per-function layout creates a new ledger entry of
  the same shape.
- **C32 — DO NOT ATTRIBUTE REASONS THE USER DID NOT GIVE.** The ruling's stated reason is
  the third referenced object. It is not "tables are slow", not "decode latency", not
  "it leaks into migration". Arguing against the rejected design on grounds the user did
  not state, or *for* the new one on those grounds, is the failure I.7's `context_id`
  paragraph exists to flag.

---

## §11 WHAT THE RECORD DOES **NOT** SETTLE — the genuinely open questions

Recorded so a proposal does not present an answer to these as if it were already canon.

1. **The mix of widths among the 32 names in each namespace.** Nothing in the record picks
   one. The only measured evidence is DESIGN §22: `nmfc_bu` at 480 bits over 8 values,
   `nmfc_expand` at 384 over 8, and a 32-bit `NodeID` as the value whose narrowing bought
   the headroom.
2. **Whether `f<n>` and `x<n>` are the same bits.** I.7 says the two namespaces are "over
   the same 512 bits" and that the *compiler's packing* decided which bits each name
   resolves to. It does **not** say the ISA makes them coincide. If a proposal makes them
   coincide, `fmv.d.x`/`fmv.x.d` become identity moves — that is a **consequence of the
   proposal**, not a fact already in the record.
3. **Whether a nameable slice may straddle a 64-bit boundary.** `Context512::read`/`write`
   support it; nothing requires it.
4. **What the admission test becomes.** K.6 states a sum-of-bits test. A fixed overlapping
   name set may make it an assignment problem instead. §7's note sets this out; the record
   does not decide it.
5. **Whether `CXW`/`CXR`'s 64-bit lane granularity changes.** The record says the lane is
   an access granularity independent of the register's structure, which permits leaving it
   alone — it does not require it.
6. **Prior art has not been checked in-record for this specific question.** I.8's
   prior-art check covers 512-bit context *registers* and is negative. **Nothing in
   CANON-DRAFT.md or DESIGN.md mentions register aliasing at all** — `grep -i alias` over
   both returns only address-aliasing and page-aliasing hits. So x86's `RAX/EAX/AX/AL`,
   RVV's `vsetvli`/SEW and SVE's typed views are **unchecked claims** in this record, and
   the design-review rule requires them to be checked before they are leaned on.
