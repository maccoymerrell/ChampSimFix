# PROPOSAL — `alias-tiled`: the 512 bits named as complete tilings

**Status: NOT CANON. A proposal, written to be built or rejected.** It answers one
question — *what does the 5-bit register field of an ordinary RISC-V instruction mean
inside a function core?* — under the 2026-09-03 ruling that no third object may be
referenced at decode time. It is the user's **option 2** ("all ISA regs map to certain
bit ranges inside the 512-bit regfile ... the number of the reg implies the slice") with
one design decision made: **the slices at each width TILE the file — disjoint, aligned,
and complete.**

> **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** *(dated 2026-09-03)*
>
> Everything below that argues from a **cap on live values** is struck, not softened: "the byte tier is unreachable", the **charge-32** rule, "at most 16 live values", "2,685 of 13,091", "a factor of 4.9", "a factor of 162", and any claim that Design A **narrows** invariant I2's *"ANY combination"*. The user's ruling, verbatim: *"We are operating on a 512-bit value. Literally every bit is independent because we are strictly in-order. Bit-packing is a thing, so we can have as many live values as we want as long as we have scratch space in the file to manipulate said value (for example, if we want to have 4 8-bit values in one vector, we can, but we must have another reg where we can stage the proper shifting and masking required)."*
>
> **The correct rule.**
> 1. Design A fixes what a 5-bit register field can **NAME DIRECTLY** — eight 64-bit tiles (`x8`-`x15`) and sixteen 32-bit tiles (`x16`-`x31`). It does **not** bound how many values are **LIVE**.
> 2. A value narrower than a name is **packed** with others inside one name and accessed by shift-and-mask through a **SCRATCH** name — standard RV64I (`srli`/`slli`/`andi`/`or`), no new instructions.
> 3. Admission counts **BITS OF PEAK LIVENESS plus the scratch bits the packing needs** (at least one spare name for staging). Never a count of values. Never "charge 32".
> 4. The byte tier and **any** width are **REACHABLE**. Canon I2's *"16 4-byte regs, 64 1-byte regs, or ANY combination"* (#232) stands **unnarrowed**.
> 5. Nameability affects **INSTRUCTION COUNT ONLY**: a packed sub-name value costs about **2-3 extra ops per access**. **A2 (extent instructions) is a speed optimisation, never a capability fix.**
> 6. **The expressiveness comparison collapses. A and B have IDENTICAL capacity — 512 bits — under every design.** They differ only in instruction count for sub-name values: **A ≈ 2-3 extra ops per packed access; B a map lookup per access.** The width-multiset counts below measure directness, not capability, and none of them is a capacity bound.
> 7. The compiler's job is **packing plus scratch allocation**; the admission tool checks **bits plus scratch**, the `RV64IMAFD` subset, no reserved names, no stack.
>
> **Consequence for the recommendation: unchanged, and its reasoning is simpler.** Design A still wins — *same capacity as B*, no third object, no per-context state. Where this page said the loss "belongs to the 5-bit register field": it belongs to nothing. There is no loss of capability, only of directness.

**Companion documents, read first and assumed here:**
`register-map-facts.md` (what ratified RISC-V actually says; claims below cite its
verdicts as **C1**–**C24**) and `register-map-context.md` (the record, the fit-list
**M1**–**M15**, the constraint list **C1**–**C32**; where a number is ambiguous this
document writes **fact-C**_n_ or **cons-C**_n_).

**What it costs to read this:** §1 is the whole argument in one page. §2–§6 are the
mechanism. §7 is the fit-list. §8 is the comparison the task asked for. §11 is where it
fails.

---

## §1 THE ARGUMENT IN ONE PAGE

**1. Each name buys exactly one (offset, width).** A register map is a function from
name to bit-range. Spending two names on one range buys nothing. So *any* scheme —
tiled, hierarchical, ad hoc — offers the compiler exactly as many distinct
(offset, width) pairs as it has names. **The design question is never "how many", it is
"which".**

**2. There are 31 names, not 63.** `x0` is hardwired zero and cannot be a slice
(fact-C17: `nop`, `j`, `ret` and the whole HINT space depend on it), which leaves
`x1`–`x31`. The `f` namespace's 32 further encodings **cannot add a single range**,
because a complete tiling is already *complete*: every 64-bit tile and every 32-bit tile
already has an `x`-name, and there is nothing left for an `f`-name to point at. §3.6
derives this rather than assuming it, and the conclusion is that `f`_n_ and `x`_n_ must
denote **the same bits** — which is the ratified **Zfinx** rule (fact-C9), arrived at
from the tiling rather than borrowed from it.

**3. A width is only useful if its names cover the file.** A value of width *w* can live
only where a *w*-wide name points. If the *w*-names cover *k* of the 512 bits, then
512 − *k* bits cannot hold a *w*-wide value **under a name of their own**. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` They are
**not unreachable**: a value living there is packed inside a wider name and read and written
by shift-and-mask through a scratch name, so the cost of incomplete coverage is **about 2-3
extra ops per access**, not lost bits. Bit-packing still buys exactly what it always bought —
all 512 bits. **Complete coverage at width *w* costs 512/*w* names**: 8 at 64 bits, 16 at 32,
32 at 16, 64 at 8 — and that is a budget for **directness**, not for capacity.

**4. Therefore the map is nearly forced.** 8 + 16 = **24 of the 31 names** buy complete
coverage at 64 and 32 bits. Completing a third width would cost 32 more names and there
are 7. **Two widths can be complete; a third is a fragment; a fourth is nothing.** The
only free choice in the entire design is where the last seven names point.

**5. And the payoff is that the admission test survives verbatim.** At a completely
tiled width, *names × width = 512* — the name budget and the bit budget are **the same
constraint**. So for any function whose values are 64- and 32-bit, admissibility is
exactly K.6's rule, unchanged: **peak simultaneous liveness ≤ 512 bits** (cons-C17), and
not a count of names (cons-C18, Part P R30). Only sub-32-bit values add a second test.
§6 proves it and gives the allocator.

**6. What is given up is stated in §11, and it is directness, not capability.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
The struck version of this point read: *"No non-power-of-two width, no unaligned placement,
no byte tier, at most seven values narrower than 32 bits, and the 'sixty-four 1-byte regs'
of user #232 is inexpressible."* **Every one of those is expressible.** What the map fixes
is which slices a 5-bit field names **directly**; anything else — a 48-bit value, a 3-bit
tag, sixty-four bytes — is packed inside a name and reached by shift-and-mask through a
**scratch** name, at about 2-3 extra ops per access. The rejected per-function map buys a
map lookup in place of those ops; **its capacity is the same 512 bits.** What is given up is
instruction count on sub-name accesses, and the honest form of cons-C32's price is that,
not a loss of expressiveness.

---

## §2 MECHANISM

### 2.1 Tiles

The 512 bits are read as three aligned tilings. Every tile is a power of two bits wide
and is aligned to its own width, so **no tile crosses a 64-bit boundary** and every tile
is a whole number of bytes on a byte boundary. This is not decoration; §4 spends it.

| tiling | width | count | offsets | covers |
|---|---|---|---|---|
| **D** | 64 bits | 8 | 0, 64, 128, … 448 | 512 of 512 — **complete** |
| **W** | 32 bits | 16 | 0, 32, 64, … 480 | 512 of 512 — **complete** |
| **H** | 16 bits | **7** of the 32 | 384, 400, … 480 | 112 of 512 — **a fragment** |

`D`, `W`, `H` are chosen to match RISC-V's own width letters — `ld`/`lw`/`lh` — so that
`lw w3, 0(d1)` reads as what it is.

Because the widths are powers of two and the tilings are aligned, the three tilings
**nest exactly**: `W[2k]` and `W[2k+1]` are the two halves of `D[k]`; `H[j]` lies inside
exactly one `W` and one `D`. The file is a binary tree and the tilings are three of its
levels. **This matters for §8: a tiling is not the opposite of a hierarchy, it is a
hierarchy with every sibling named.**

### 2.2 Names

The recommended assignment. **Status: the *shape* — which widths, complete or fragmentary,
and the nesting — is the proposal. The specific index ranges are an ABI constant that the
core and the compiler must agree on and that must be published once**; they are of the
kind cons-C24 calls an implementation choice, but unlike a `funct7` value they are
visible to software, so they are fixed here rather than left open.

| encoding | ABI name | width | bit range | note |
|---|---|---|---|---|
| `x0` | `zero` | **any** | none | reads 0 **at whatever width the instruction needs**, writes discarded at any width; costs none of the 512 (**M5**) |
| `x1`–`x8` | `d0`–`d7` | 64 | `[64k, 64k+64)` for `d`_k_ | the **D** tiling, complete |
| `x9`–`x24` | `w0`–`w15` | 32 | `[32k, 32k+32)` for `w`_k_ | the **W** tiling, complete |
| `x25`–`x31` | `h0`–`h6` | 16 | `[384+16j, +16)` for `h`_j_ | the **H** fragment, bits 384–495 |
| `f0` | — | — | none | `f0` ≡ `x0`: reads +0.0, writes discarded (Zdinx's rule, fact-C16) |
| `f1`–`f24` | `d0`–`d7`, `w0`–`w15` | 64 / 32 | **identical to `x1`–`x24`** | an FP *spelling* of the same tile |
| `f25`–`f31` | — | — | none | **reserved, illegal** — no FP operation is 16 bits wide |

Three properties to read off the table:

- **Nothing is unnameable at 64 or 32 bits.** Every one of the 512 bits is inside exactly
  one `d` name and exactly one `w` name.
- **`f`_n_ and `x`_n_ are the same bits** (n = 1…24). This is the ratified Zfinx rule —
  "whenever such an instruction would have accessed an `f` register, it instead accesses
  the `x` register with the same number" (fact-C9) — and §3.6 shows the tiling leaves no
  alternative.
- **The seven `h` names sit at the top, contiguously from a 128-bit boundary.** Reason:
  halfword values then fill `D[6]` before touching `D[7]`, so *c* live halfwords cost
  ⌈*c*/2⌉ word-tiles and never strand a word-tile they only half-use. Starting anywhere
  else costs one more blocked tile for the same data. The eighth halfword of that region
  (bits 496–511) has no name, because 31 − 24 = 7 names remain and an eighth would have to
  be `x0`. It is nameable at 32 (`w15`) and at 64 (`d7`); only the halfword view is missing.

### 2.3 The one rule that makes it an ISA

> **THE WIDTH-FROM-NAME RULE. An instruction executes as though XLEN (and FLEN) equalled
> the width of the tile its destination name denotes. Every register operand of an
> arithmetic, logical, compare or move instruction must name a tile of that same width;
> an address operand is always a `d` name.**

This is the whole of the execution semantics, and it is chosen because **every width in
the table is a ratified RISC-V configuration**:

| name width | the instruction executes as | ratified anchor |
|---|---|---|
| 64 (`d`) | **RV64** — bit-for-bit the ratified behaviour, including `addw`'s sign-extension into 63:32 | Ch. 5 (fact-C13) |
| 32 (`w`) | **RV32** — 32-bit wraparound, 5-bit shift amounts, no `*W` opcodes, no sign-extension anywhere | Ch. 2 |
| 32 (`w`, FP) | **FLEN=32**, i.e. `F` without `D`: no NaN-boxing, because there are no upper bits (fact-C11) | Ch. 20, Ch. 26 §26.1 |
| 16 (`h`) | no ratified base exists → **only width-agnostic operations are defined** (§3.4) | — |

So a function core is not a new machine per width: it is an RV64 core for `d` names, an
RV32 core for `w` names, and a mover for `h` names. Nothing about a value's *type* comes
from the name — the opcode types the operation, as it always did (fact-C9) — and the
`f`/`x` spelling is a legality constraint on the opcode, never a type inference.

### 2.4 Why the widths are 64 / 32 / 16 and not something else

- **64 is not optional.** Addresses are 64 bits in RV64; `nmfc_bu` carries two pointers
  and a 64-bit frontier word (DESIGN §22). Complete coverage at 64 costs 8 names, the
  cheapest complete tiling there is.
- **32 is the measured one.** DESIGN §22's headroom came from a **32-bit `NodeID`**:
  `nmfc_bu` is 480 bits over 8 values, which decomposes uniquely as 7×64 + 1×32, and
  `nmfc_expand` is 384 over 8. Incomplete coverage at 32 does not break those two — it
  breaks the next one along, and §8.2 gives it: **4×64 + 5×32 = 416 bits**, well inside
  the file, placeable here and not placeable by a scheme that names only half the word
  tiles.
- **16 is what is left.** Seven names. It is a fragment and is presented as one.
- **8 is not affordable.** A complete byte tiling is 64 names. A fragment of 3 or 4 byte
  names was considered and rejected in §11.3: a byte value fits an `h` name at a cost of
  8 wasted bits, and spending scarce names on a second fragmentary tier buys less than
  spending them on the first.

---

## §3 ENCODING AND FORMAT

### 3.1 No encoding changes. None.

**Not one bit of instruction encoding moves.** No new opcode, no new `funct7` group, no
new immediate field, no consumption of the reserved space (**M14**, cons-C23 — groups
`0x6`/`0x7` remain free for `KILL`, mailboxes and `RESUME`). A stock RISC-V assembler
emits the instructions; a stock `objdump` disassembles them. What changes is the
*interpretation* of `rd`/`rs1`/`rs2` **inside a function core, and nowhere else** — the
host, the fabric, the coherence protocol and the twelve NMFC instructions are untouched
(cons-C22 holds: the instruction count does not change).

The cost of that is a readability hazard worth naming: stock `objdump` prints `x23`
where the ABI means `w14`, and prints it as if it were 64 bits wide. A one-page
disassembler alias table fixes the display; nothing fixes a reader who does not know the
map.

### 3.2 What each field means

| field | meaning inside a function core |
|---|---|
| `rd`, `rs1`, `rs2` in an integer instruction | index into the `x` table of §2.2 → (offset, width) |
| `rd`, `rs1`, `rs2`, `rs3` in an FP instruction | index into the `f` table → the same (offset, width) for 1–24; **illegal** for 0 and 25–31 |
| `rm` (rounding mode) | **static modes only.** `rm = 111` (DYN) reads `fcsr`, which does not exist (cons-C8) → **illegal instruction** |
| everything else | unchanged |

### 3.3 Legality — the rules a decoder checks

A function core traps `illegal instruction` (**M6**, re-homed — see §7) on each of:

1. **Width disagreement between opcode and name.** `add` is legal at any name width and
   means "add at that width". `addw`/`subw`/`sllw`/`srlw`/`sraw`/`addiw`/`slliw`/`mulw`/
   `divw`/`divuw`/`remw`/`remuw` are RV64I/RV64M-only opcodes: they are legal **only when
   the destination is a `d` name**, exactly as ratified (they exist to sign-extend a
   32-bit result into a 64-bit register — fact-C13 — and a 32-bit name has nowhere to put
   the extension). `ld`/`sd` require a `d` data operand; `lwu` likewise. `fadd.d` and the
   rest of `D` require a `d` name; `fadd.s` and the rest of `F` require a `w` name.
   The shift-amount field follows the same rule: a `w` name takes RV32's 5-bit `shamt`
   and `shamt[5] = 1` is reserved, exactly as ratified.
   *This is C18 option 1, and it is Zdinx's own choice: "use of misaligned (odd-numbered)
   registers for double-width floating-point operands is reserved" (fact-C16).*
2. **Mixed widths among register operands.** `add d1, w3, w4` is illegal. §3.5 gives the
   conversion idioms, and §3.5.1 lists the opcodes that name their own widths and are
   therefore exempt.
   **`x0` and `f0` are exempt too, and this is load-bearing.** They denote no bits, so they
   have no width to disagree with: `x0` supplies zero at whatever width the instruction
   needs and discards writes at any width. Without this exemption `beqz` (`beq rs, x0`),
   `li`, `mv`-from-zero, `snez` and `j` (`jal x0`) would all be illegal, and this machine's
   loops are built from them.
3. **An `h` name in an instruction with no 16-bit meaning** (§3.4).
4. **`f0`, `f25`–`f31` as an FP operand**, except `f0` as the zero source/discard
   destination of §2.2.
5. **`rm = DYN`.**
6. **Anything outside the subset** (cons-C10) — CSR, `FENCE`, RVC, `ecall`, `V`.

Rules 1–3 are new, and they are why the ISA is self-checking: a mis-typed register is a
trap at the tile, not a silent wrong answer. Note what rule 1 buys in practice — **a
stock RV64 compiler already emits `addw` for `int` and `add` for `long`**, so a back end
that assigns `int`-typed values to `w` names and `long`-typed values to `d` names emits
legal code *by construction*, and one that gets it wrong fails loudly.

### 3.4 What an `h` name can do

There is no 16-bit arithmetic in `RV64IMAFD` — no `addh`, no `Zbb`, no `P`
(fact-C21: `P` is not ratified and names lane width in the opcode anyway). So an `h`
name is defined for exactly the operations whose semantics are width-agnostic:

- **`lh` / `lhu` / `sh`** — with a 16-bit destination the extension has no target, so
  `lh` and `lhu` coincide; the ABI's canonical spelling is `lhu`, and `lh` on an `h`
  destination is legal and identical.
- **`and` / `or` / `xor`** and their immediate forms, **`beq` / `bne` / `blt` / `bge` /
  `bltu` / `bgeu`**, **`slt` / `sltu`**, **`mv`** — all at 16 bits.
- **`add` / `sub` / shifts** at 16 bits under the width-from-name rule, with 4-bit shift
  amounts. *This is the one place the proposal defines behaviour RISC-V has never
  ratified* — there is no RV16I. An implementation that would rather not build a 16-bit
  adder may make these illegal too and lose nothing but convenience; §11.4 records the
  choice as open.

Everything else — `mul`, `div`, all of `F`/`D`, `ld`, `lw`, `amo*` — is **illegal on an
`h` name**. An `h` name is for a value that is carried, compared and stored, not
computed: a flag, a small counter, a level number, a status word.

### 3.5 Changing width

Under rule 2, mixed-width arithmetic is illegal, so a narrow value must be widened before
it meets a wide one. **No instruction is added for this** (cons-C26 stands: no runtime
(offset, width) operand, no bit-field insert/extract). The idioms are the ones stock RV64
already uses when it lacks `Zbb`:

| conversion | instruction(s) | why it is exactly ratified behaviour |
|---|---|---|
| narrow → wide, **signed** | `mv d1, w3` (`addi d1, w3, 0`) | `addi` reads its source and sign-extends to the destination's XLEN; here XLEN differs between the two, which is the one carve-out from rule 2 and is stated as such |
| narrow → wide, **unsigned** | `mv d1, w3` ; `slli d1, d1, 32` ; `srli d1, d1, 32` | what RV64 without `Zbb` emits for `zext.w` today |
| wide → narrow | `mv w3, d1` | truncation to the destination's width; **it writes only `w3`'s 32 bits and cannot touch a neighbour** (§3.7) |

**The carve-out, stated once:** `mv`/`addi rd, rs1, 0` is the width-conversion move and is
the *only* instruction permitted to name operands of different widths. Loads and stores
are the other exception and are not a carve-out at all — an address operand is a `d` name
by rule, and the data operand's width is the access width, which is what `lb`/`lh`/`lw`/
`ld` have always meant.

**3.5.1 Instructions that already name their own widths.** A family of `RV64IMAFD`
opcodes states the width of *each* operand in the mnemonic, and for these the opcode
governs and rule 2 does not apply — the name widths must simply **agree with what the
opcode already says**:

| opcode family | required name widths |
|---|---|
| `fcvt.d.s` / `fcvt.s.d` | destination and source as the mnemonic states: `fcvt.d.s d1, w3` is the legal form, and it is the *only* way to change a float's width |
| `fcvt.w.d`, `fcvt.l.s`, `fcvt.d.l`, `fcvt.s.w`, and the `u` forms | the integer name is `w` for `.w`/`.wu` and `d` for `.l`/`.lu`; the float name is `w` for `.s` and `d` for `.d` |
| `feq.d` / `flt.s` / `fle.d`, `fclass.d` | float operands as stated; the integer destination may be any width and receives 0/1 zero-extended into it |
| `lb`…`ld`, `sb`…`sd`, `lr`/`sc`/`amo*` | the address operand is a `d` name; the data operand's width is the access width the opcode names |

**Consequence worth stating: `fcvt.d.s` and `fcvt.s.d` are the width-conversion moves for
floating point, and they are not optional.** With `fmv.*` deleted (§3.6) and `f`-names
identical to `x`-names, an `f32` in a `w` tile and an `f64` in a `d` tile are converted
only by `fcvt`, which is the correct instruction for the job in stock RISC-V as well —
`fmv` never converted anything.

### 3.6 Why `f`_n_ ≡ `x`_n_ is forced, not chosen

§11 open question 2 of the context file asks whether `f` and `x` names coincide. Under a
complete tiling the answer is not a preference:

- The `D` and `W` tilings are complete, so **every 64-bit tile and every 32-bit tile
  already has an `x` name.** There is no unnamed 64- or 32-bit tile for an `f` name to
  point at.
- An `f` name could point at a *differently aligned* range — a 64-bit slice at offset 32,
  say — but that breaks the tiling: it overlaps two `d` names, reintroduces straddling
  (§4.3), and destroys the buddy property §6 depends on.
- An `f` name could point at a 16- or 8-bit tile, but **no operation in `F`/`D` is
  narrower than 32 bits**, so the name would be unusable. (`Zfh` would change this. It is
  not in the subset and this proposal does not ask for it; §11.6.)

So the `f` namespace can only re-spell tiles that already have `x` names, and the least
surprising re-spelling is the identity — which is Zfinx's ratified rule verbatim. Two
consequences follow, and both are improvements:

- **K.6's "third wrong answer" becomes unrepresentable.** The canon warns that a
  slot-counting test allocating `f`-names from a separate pool "admits a function twice
  that size" (cons-C17). Under identity aliasing there **is** no second pool: `f5` and
  `x5` are one tile, so an allocator that treats them as independent produces code that
  visibly clobbers itself at the first test, rather than a function that silently does not
  fit.
- **The four `fmv` transfer instructions are deleted**, as Zfinx and Zdinx delete them
  (fact-C15). They would be no-ops on identical bits, and the two `.w` forms are worse
  than no-ops — `fmv.x.w` sign-extends bits 31:0 over 63:32 and `fmv.w.x` NaN-boxes the
  same range (fact-C3, fact-C4), each **destroying a neighbouring tile's value.** Deleting
  them removes the hazard outright. `flw`/`fsw`/`fld`/`fsd` go with them, again as Zfinx
  does: `lw`/`ld` already move those bits.
  **This narrows the ruled subset** — see §11.1 and the amendment proposed in §12.

### 3.7 The write rule

> **A write to a name writes exactly that name's bits. It never sign-extends, zero-extends
> or NaN-boxes beyond them, and never modifies any bit outside them.**

This is the single rule fact-§6.3 demands ("the document needs one explicit rule"), and
under a tiling it is forced rather than chosen: the bits above a `w` tile are **another
value**, named `w`_k+1_, not spare room. x86 preserves, AArch64 zeroes (fact-C19,
fact-C20); **a tiled map can do neither, because there is nothing there to preserve or
zero that belongs to this name.**

The three ratified mechanisms that would otherwise clobber a neighbour are all disarmed
by construction, and this is the proposal's cleanest result:

| hazard | why it cannot fire |
|---|---|
| `addw`'s sign-extension into 63:32 (fact-C13) | `addw` is legal only on a `d` name, where 63:32 belong to that same name — ratified behaviour, unchanged |
| NaN-boxing a narrow float (fact-C4) | an `f32` lives in a `w` tile with **no upper bits**; FLEN is 32 for that instruction, so the rule does not apply (fact-C11, and Zfinx's own rationale) |
| `fmv.x.w`'s sign-extension / `fmv.w.x`'s boxing (fact-C15) | those instructions are deleted (§3.6) |

---

## §4 DECODE AND EXECUTION

### 4.1 What replaces the table lookup

Today (**M2**) the decode path is a memory reference:

```c
uint64_t NMFCTile::readReg( TileContext& c, uint32_t r ) const {
  if( r == 0 && layout_.hasZero ) return 0;
  if( !layout_.defines( r ) ) illegal( c, 0, "reads a register the function's layout does not define" );
  return c.regs.read( layout_.field[r] );          // <- layout_ is the third object
}
```

Under `alias-tiled` the same line is arithmetic on the instruction's own bits:

```
  width_class(i) = (i <= 8)  ? W64
                 : (i <= 24) ? W32
                             : W16                  // two 5-bit compares against constants
  offset(i)      = (i <= 8)  ? (i - 1)  << 6
                 : (i <= 24) ? (i - 9)  << 5
                             : 384 + ((i - 25) << 4)
```

Two magnitude comparators on a 5-bit field, one 5-bit subtract, one shift by a constant
per class, one 3:1 select. **No memory is read, nothing is indexed by the function, and
nothing depends on the context** (cons-C1, cons-C2). Equivalently, as a ROM: 31 entries ×
(9-bit offset + 2-bit class) = **341 bits, once per tile**, shared by every context and
every function on it. That object is the same kind of thing as the opcode decoder's own
truth table — combinational, fixed at tape-out, not addressed by the program, not fetched,
not per function, not per context. It is not a third referenced object; the instruction
and the data remain the only two.

### 4.2 The read and write datapath

The 512 bits are stored as they are today (**M3**, `Context512`, 8 × 64). Because tiles are
aligned and ≤ 64 bits, **the extract factorises**:

```
  read  = mask( shift_within_word( word[ offset >> 6 ], offset[5:4] ), width )
  write = word[ offset >> 6 ] with byte-enables { offset[5:3], width>>3 }
```

- **Read:** an 8:1 select of 64-bit words (3 mux levels) followed by an intra-word shift
  of 0/16/32/48 bits (a 4:1 mux, 2 levels) and a mask. **Five mux levels on a 64-bit
  path.** A fixed 8 × 64 register file needs three of them; this proposal costs **two more
  mux levels**, which is the honest hardware price of naming anything narrower than a
  word, and it is off the critical path in any case because `offset` is known at decode,
  a stage before the read.
- **Write:** because every tile is a whole number of bytes on a byte boundary, a write is
  expressed with **byte enables — 64 per context line** — and needs **no read-modify-write
  and no merge.** This is worth stating against fact-C19: the partial-register stalls x86
  paid came from *merging* a narrow write into a wide value; here there is nothing to
  merge, because the neighbouring bytes belong to a different architectural name and are
  simply not written. The rejected per-function map could place a field at bit 13 and
  would have needed **bit**-granular enables or a read-modify-write; `alias-tiled` is
  strictly cheaper hardware.

### 4.3 Straddling: no

Context-file §11 open question 3 asks whether a nameable slice may cross a 64-bit
boundary. **Under `alias-tiled`, no slice ever does** — an aligned power-of-two tile of
≤ 64 bits cannot. So `Context512::read`/`write`'s straddle path (`word[w0+1]`, the
`got < f.width` branch) becomes **dead code**: correct, unreachable, and removable. The
storage mechanics of **M3** are otherwise reusable unchanged.

### 4.4 Latency

**Decode: +0 cycles.** The map resolves from the instruction word in parallel with opcode
decode, from a comparison and a shift; it cannot extend a stage that already decodes a
32-bit instruction word.
**Register read: +2 mux levels** as counted above, well inside a cycle at any clock this
machine targets, and hidden behind decode.
**Register write: +0** — byte enables are combinational off the same decode.
**Everything else: unchanged.** No new pipeline stage, no new hazard class (§4.2), no
speculation of any kind (cons-C13).

### 4.5 A worked function

`nmfc_bu` as DESIGN §22 measures it — 8 values, **480 of 512 bits**, which decomposes
uniquely as **7 × 64 + 1 × 32** (no other multiset of eight widths from {8,16,32,64} sums
to 480). Assign the seven 64-bit values to `d0`–`d6` and the 32-bit `NodeID` to `w14`
(bits 448–479, inside the free tile `d7`), leaving `w15` (bits 480–511) and every `h`
name free:

```
        # d0 = parent+lo   d1 = index+lo   d2 = frontier   d3..d6 = working pointers/counters
        # w14 = NodeID (32-bit)
  loop: lw    w14, 0(d1)          # RV32 semantics: 32 bits, no sign-extension, w14 is 32 wide
        slli  d3, d3, 3           # RV64 semantics on a d name
        add   d4, d0, d3
        ld    d5, 0(d4)
        beq   w14, x0,  done      # beqz -- x0 supplies zero at the operands' width
        addw  d6, d6, d3          # legal: destination is a d name, RV64I behaviour verbatim
        j     loop                # jal x0 -- x0 is still the zero register (M5)
```

Two things to read off it: the code is ordinary RV64 assembly that a stock assembler
accepts, and every width rule of §3.3 is satisfied without effort because the values
already had those widths in the source.

`nmfc_expand` — 8 values, **384 bits** — reads most naturally as 4 × 64 + 4 × 32 (`d0`–`d3`
and any four `w` names outside `d0`–`d3`, e.g. `w8`–`w11`), and 128 bits stay free. Exactly one other
decomposition of eight values into 384 bits exists — 5 × 64 + 1 × 32 + 2 × 16 — and it
fits too (a=5, b=1, c=2: 11 ≤ 15 and 5 ≤ 7). The record gives the totals, not the
per-value widths, and this document does not invent them.

---

## §5 THE HOST-SIDE RETRIEVAL PATH

### 5.1 The rule, and the correction it carries

**`V` is not available and must not appear here** (cons-C11): it is outside the ruled
subset, Rev implements no vector unit, Vanadis implements none, and RoCC carries 128 bits.
So `vmv.x.s`, `vslidedown` and `vfmv.f.s` are not the retrieval path — that half of the
2026-09-03 provisional answer is withdrawn. (fact-C7 adds a second, independent reason:
holding 512 bits in **one** architectural vector register requires `Zvl512b`, not plain
`V`; and fact-C8 notes that even where `V` exists, a 64-byte store followed by ordinary
loads beats it. Neither applies here — there is no vector unit to use.)

**The path is `CXR`, then RV64I shifts and masks, then `fmv.*` if an `f` register is
wanted** — and on the host those `fmv` instructions are the real, ratified ones, because
the **host** is a stock RV64GC core with a genuine 64-bit `f` file. Nothing in §3.6
touches the host; only the function core deletes them.

### 5.2 The aperture lines up with the tiling for free

`CXW`/`CXR` move one **64-bit lane**, the lane number living in `funct7` bits 3:1
(**M4**, cons-C28, `NMFC_CX_LANE_MASK = 0x7`). Under `alias-tiled`:

> **lane *k* is exactly tile `d`_k_.**

The aperture's granularity and the widest tiling coincide, and neither had to move to make
it so. The record's position that "the lane is an access granularity, not the register's
structure" is preserved — it just happens that the structure now agrees with it. And
because no tile straddles a lane (§4.3), **every named value is reachable in exactly one
`CXR`**; the two-`CXR`-and-splice case that a bit-packed arbitrary map required cannot
arise.

| tile | lane | extraction on the host |
|---|---|---|
| `d`_k_ | *k* | `CXR t0, cS, k` |
| `w`_2k_ | *k* | `CXR t0, cS, k` ; `slli t0,t0,32` ; `srli t0,t0,32` (or `sext.w` if signed) |
| `w`_2k+1_ | *k* | `CXR t0, cS, k` ; `srli t0, t0, 32` |
| `h`_j_ | 6 or 7 | `CXR t0, cS, 6|7` ; `srli t0, t0, s` ; mask 16 bits |

### 5.3 Staging an argument in, and reading a result out

```
  # host: pass a 64-bit pointer in d0 and a 32-bit NodeID in w14.
  # lane 7 holds { w15 : w14 } -- w14 is the LOW half, bits 448..479.
  CXW   c1, 0, a0                  # lane 0 <- pointer                      (d0)
  CXR   t1, c1, 7                  # read lane 7 back
  srli  t1, t1, 32
  slli  t1, t1, 32                 # keep w15, clear w14's half
  slli  t0, a1, 32
  srli  t0, t0, 32                 # zero-extend the NodeID to 64 bits
  or    t0, t0, t1
  CXW   c1, 7, t0                  # lane 7 <- { w15 : new w14 }            (w14)
  FORK.R ...

  # host: after the join, read an f32 result out of w2
  CXR   t0, c1, 1                  # lane 1 = { w3 | w2 }
  slli  t0, t0, 32
  srli  t0, t0, 32                 # w2
  fmv.w.x fa0, t0                  # host FLEN=64: NaN-boxes correctly, as ratified (fact-C2/C4)
```

Note the read-modify-write in the *staging* sequence: packing two values into one lane
costs the host three extra integer instructions, which is the price the record already
accepted when it said "regular bit manipulation can take you the rest of the way" (#233)
and refused a bit-field insert/extract instruction (cons-C26). It is host code, in the
caller's frame, with a stack and 31 real registers — not the constrained side.

### 5.4 Where the map lives on the host

**Nowhere referenced.** The offsets appear in the caller's code as immediates in `srli`
and `slli`, exactly as a struct field offset does. There is no table, no descriptor and
nothing fetched — which is the same answer as §4.1 gives on the tile, arrived at from the
other end.

---

## §6 ADMISSION

### 6.1 The claim

> **For any function whose live values are all 32 or 64 bits wide, admissibility under
> `alias-tiled` is exactly K.6's rule, unchanged: peak simultaneous liveness ≤ 512 bits.**

Not "approximately", not "with a caveat". This is the property a complete tiling buys, and
it is the reason to buy one.

**Why.** At a completely tiled width *w*, the number of names is 512/*w*, so
*names × w = 512*: the name budget **is** the bit budget, and cannot bind before it. And
the tiles form a binary buddy system, so a bit-sum-feasible set of values is always
placeable:

> **Placement lemma.** Let values have widths *w*₁ ≥ *w*₂ ≥ … ≥ *w*ₙ, each a power of two
> in {64, 32}, with Σ*w*ᵢ ≤ 512. Place them in that order, each at the lowest free bit
> offset. Then every value lands on a *w*ᵢ-aligned offset and no two overlap.
> *Proof.* By induction: before placing value *i*, the occupied region is the prefix
> [0, Σ_{j<i} *w*ⱼ), and Σ_{j<i} *w*ⱼ is a sum of multiples of *w*ᵢ (every earlier width is
> ≥ *w*ᵢ and a power of two, hence a multiple of it), so the next free offset is already
> *w*ᵢ-aligned. No gap is ever created, so the total placed equals the total width. ∎

First-fit-decreasing is therefore an **exact** allocator, not a heuristic, and it runs in
O(*n* log *n*). This is the *opposite* of the usual register-allocation situation, and it
is worth being explicit about why: interference here is purely geometric, and the geometry
is a buddy tree.

### 6.2 The exact test, including narrow values

> **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** *(dated 2026-09-03)*
>
> **The `c ≤ 7` cap, the `⌈c/2⌉` name deductions and the 16-bit rounding penalty are
> struck.** They price a value at the width of the *name* that would hold it and cap the
> number of narrow live values — exactly the cap the ruling forbids. The correct test is
> peak simultaneous liveness **in bits, each value at its own width, plus the scratch bits
> the packing needs**, with at least one spare name for staging:
> ```
>   max over program points of ( live bits + scratch bits )  <=  512
>   and every opcode is in RV64IMAFD, no reserved name, no stack
> ```
> There is no bound on *c*. Sixty-four live bytes are 512 bits of liveness (and so need
> their staging space accounted, like any other packing); seven is not a ceiling on
> anything. `2a + b ≤ 16` and `a ≤ 8` describe how many values can be **named directly** at
> 32 and 64 bits — an instruction-count statement, not an admission test. §6.1's
> first-fit-decreasing placer remains exactly right for **direct name assignment**, and the
> values it cannot place directly are packed instead.

The superseded text follows, retained for the record. Let **a** = live values needing 64 bits, **b** = needing 32, **c** = needing ≤ 16 (the `h`
tier). With the §2.2 assignment:

> ~~**Admissible ⟺  c ≤ 7  ∧  2a + b ≤ 16 − ⌈c/2⌉  ∧  a ≤ 8 − ⌈⌈c/2⌉/2⌉**~~ **[STRUCK - user ruling 2026-09-03 (liveness)]** — a directness rule, never an admission rule.

or, in the bit language K.6 uses:

> ~~**64a + 32b + 16c ≤ 512 − 16·(c mod 2)**, with **c ≤ 7**.~~ **[STRUCK]** The bit test is
> `live bits + scratch bits ≤ 512`, with each value charged its **own** width, no rounding
> penalty and **no cap on c**.

The two forms agree on every case (checked exhaustively for a ≤ 9, b ≤ 19, c ≤ 9; the bit
form implies `a ≤ 8` because 64a ≤ 512). The correction term is one 16-bit rounding penalty for
an odd halfword count — and it does not exist: `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` an unpaired `h` value's sibling
halfword is **not unreachable**, it is packed and reached through a scratch name, and there
is **no cap of seven** on anything. **The test is `live bits + scratch bits ≤ 512` at every
value of c, i.e. K.6 in bits, always.**

Checks against the two measured functions (cons-C21):

| function | decomposition | test | result |
|---|---|---|---|
| `nmfc_bu` | a=7, b=1, c=0 | **480 live bits ≤ 512** | **admissible**, 32 bits spare for staging — matches DESIGN §22 |
| `nmfc_expand` | a=4, b=4, c=0 | **384 live bits ≤ 512** | **admissible**, 128 spare — matches DESIGN §22 |

`[CORRECTED - user ruling 2026-09-03 (liveness)]` Both rows now test **bits plus scratch**;
the name-count arithmetic they used to show (`2a + b ≤ 16`) is a directness check, and both
functions happen to be directly nameable as well as admissible.

### 6.3 What this does to K.6, R30 and the tool

- **cons-C17 survives literally.** The test is peak simultaneous liveness **in bits, in
  one pool**. §3.6 makes the one-pool property structural: `f` and `x` names are the same
  tiles, so there is no second pool to count separately, and K.6's "third wrong answer"
  cannot be written down.
- **cons-C18 is not re-introduced.** The context file warns (§7, R30 note) that a fixed
  name set could turn admission into "how many names does it use" — the mirror image of the
  error that reported 17 and 21 for a function of 8. It does not here, because at 64 and 32
  the name count is *arithmetically identical* to the bit count. The only place a name
  count appears is `c ≤ 7`, and that is a statement about a **fragmentary** tier, declared
  as one.
- **cons-C19 survives.** A value never read is never live, takes no tile, and costs nothing.
- **cons-C15 survives.** Failure is fatal; there is no truncation and no softening. A
  function at `c = 8` is rejected, not squeezed.
- **`annotate` (M7)** stops being a slot counter. It already computes `bits` at
  `tools/nmfc/annotate.cc:555-559` and throws it away at a stderr line; under this proposal
  that quantity becomes the gate — **bits of peak liveness plus the scratch bits the packing
  needs, each value at its own width** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` (the struck version added "a three-way
  classification of each live value's width and the closed form of §6.2"; that closed form is
  a directness rule and gates nothing). The linear-scan allocator it already has becomes the
  first-fit-decreasing placer of §6.1 — **the same pass, with `w`-sorted placement and the
  §2.2 name table appended.** That is a smaller change than the one K.6 already requires.

### 6.4 The compiler's half

DESIGN §23.6 and I.8 call the packing an unsolved compiler problem and say the
architecture's obligation is only to not prevent it. `alias-tiled` does better than not
prevent it: **it turns the packing into a register-class assignment**, which is the
cheapest thing a back end knows how to do.

- Three allocatable classes — `D` (8 registers), `W` (16), `H` (7) — with the nesting
  declared as sub-register indices. LLVM's `RegUnit` machinery models exactly this
  (it is how x86's `RAX`/`EAX`/`AX`/`AL` are allocated), and the RISC-V back end already
  carries width-split GPR classes for `Zfinx`/`Zdinx` (`GPRF32`, `GPRF64`, `GPRPair`) —
  so the machinery is present upstream, not hypothetical. *(Upstream-code claim, not a
  spec claim: verify against the tree in use before relying on it.)*
- The type information the allocator needs — is this value 32 bits or 64? — is information
  **the front end already has**, and which the back end already acts on when it chooses
  `addw` over `add`. That is why §3.3 rule 1 was written to agree with existing codegen
  rather than to fight it.
- **`-ffixed-x{n}` (M13, DESIGN §24 step 5) still works, and now says what it means.**
  `-ffixed-x9 … -ffixed-x31` leaves `x1`–`x8`: eight 64-bit names with **bit-exact ratified
  RV64 semantics**, so a stock, unmodified toolchain produces correct function-core code on
  day one. Be precise about what that path is: it is the 8 × 64 packing, i.e. `RegLayout::
  defaultLayout()` without the table — a **fallback, not the design** (cons-C30). Reaching
  the `w` and `h` tiers needs the register-class split above; `-ffixed` can reserve a
  register but cannot retype one, and no flag will do this. DESIGN §24 step 5's "try this
  before committing to a custom backend" therefore gets you a *working* machine at 8 × 64
  and no further — which is more than the record could previously promise, and less than
  the design wants.

---

## §7 EVERY EXISTING MECHANISM, ANSWERED

The context file's fit-list, row by row. "A proposal is incomplete until it says what
happens to each."

| # | mechanism | what happens to it |
|---|---|---|
| **M1** | resident-function table (`RegLayout layout_`) | **DELETED.** Not kept as an alternative, not kept as an optimisation. DESIGN §25.7 D:2560-2567 is overruled by the 2026-09-03 ruling and must be **marked superseded**, not quietly dropped (cons-C31; ledger entry proposed in §12). |
| **M2** | `readReg`/`writeReg` decode indirection | **Replaced by §4.1's arithmetic.** `layout_.field[r]` becomes two comparators, a subtract and a shift. A wiring change, not a lookup. |
| **M3** | `Context512::read`/`write` | **Reusable unchanged**, and simplified: no tile straddles a 64-bit word, so the `w0+1` path is dead code (§4.3). The write path can move to byte enables. |
| **M4** | `CXW`/`CXR`, 64-bit lane in `funct7[3:1]` | **Unchanged, and now aligned:** lane *k* ≡ tile `d`_k_ (§5.2). Granularity and structure agree without either moving. Every named value is one `CXR`. |
| **M5** | the `x0` rule | **Preserved exactly.** `x0` reads zero, writes are discarded, and it costs none of the 512 bits. It is *load-bearing* here, not vestigial: `j` is `jal x0` and this machine's loops need it (fact-C17), and it is **width-polymorphic** — exempt from the equal-width rule, or `beqz` would be illegal (§3.3 rule 2). `f0` ≡ `x0` reads **+0.0** and discards writes — Zdinx's ratified rule (fact-C16), and the only FP zero constant available once `fmv.w.x` is deleted. |
| **M6** | the illegal-register trap | **Re-homed, not lost.** Every name is now always defined, so "undefined register" cannot fire; its purpose — *a function that needs more than the file holds finds out immediately* — moves to (a) the six legality rules of §3.3, which trap a width-mismatched or reserved name **at the tile**, and (b) admission (§6), which rejects at build time. cons-C14's requirement, that this is a hard error and never a silent zero, is met by both. |
| **M7** | `annotate` / the admission tool | **Rewritten as §6.3 describes** — the bit count it already computes becomes the gate, plus a width classification and first-fit-decreasing placement onto the §2.2 table. Smaller than the rewrite K.6 already demands. |
| **M8** | the `END` return bit / what the join may assume | **I2 is preserved: register positions still carry no meaning across the boundary.** What the ISA now fixes is how a *name* resolves inside the core; it does **not** fix which value a function chose to put in `d3`. That remains the function's own ABI, known to its caller, exactly as before. The 512 bits still come back whole and uninterpreted. |
| **M9** | `JOIN` as a read-modify-write try | **Unaffected**, confirmed: `cDST_new = ok ? ftu_payload : cDST_old` moves 512 bits and never inspects them. |
| **M10** | `CONT` / `CONT.M` | **Simplified.** A successor inherits the same map because there is only one map. Under M1 a successor running a different function needed a different table entry and a way to know which; that problem disappears rather than being solved. |
| **M11** | 72-byte migration | **Exactly 72 B, unchanged** — 64 B of context + 8 B of PC. The scheme adds **zero** bits to anything that travels (cons-C6, §10). |
| **M12** | the two hosts and RoCC's 128-bit path | **Unaffected.** Every operand remains a value in a GPR; a context register is still named by a number in a GPR. The tiling is internal to the function core and no host instruction names a tile. |
| **M13** | `-ffixed-x{n}` as the admission gate | **Half-solved, and the half is stated** (§6.4): stock toolchain + `-ffixed` gives a correct 8 × 64 machine immediately; the `w`/`h` tiers need a register-class split in the back end. No flag can retype a register. |
| **M14** | encoding space | **Untouched.** No new instruction, no `funct7` group consumed; `0x6`/`0x7` remain for `KILL`, mailboxes, `RESUME` (cons-C23). The proposal *shrinks* the instruction set by four `fmv` and four FP load/store forms (§3.6), which is a subset narrowing, not an encoding claim. |
| **M15** | Appendix 2 `S5` | **Changes rather than closes.** `S5` records that the bit-level admission test is never exercised "because nothing produces a layout other than the default". Under `alias-tiled` there are no layouts to produce, and the tile has narrow names **in hardware from day one**, so the bits-used figure stops being constant the moment a function uses a `w` name. `S5` should be restated as: *the tile implements the D tier only; the W and H tiers are unbuilt* — a bounded, closable implementation gap instead of an open-ended one. |

**And two rows the fit-list does not have, which this proposal creates:**

- **The `f`-namespace clause of canon I.0.** CANON-DRAFT.md:9804 states, verbatim: "**The
  namespaces do not alias**: the core implements 512 bits of live storage rather than 64
  architectural slots, and the compiler binds every simultaneously-live `f`- or `x`-name to
  a **disjoint bit range**, so `f3` and `x3` are different names at different offsets, not
  one slot." **§3.6 contradicts the second clause of that sentence.** The first clause —
  512 bits of live storage, not 64 slots — is *preserved and strengthened*; but "`f3` and
  `x3` are different names at different offsets" is a consequence of the compiler doing the
  binding, and the 2026-09-03 ruling took the binding away from the compiler. This is a
  supersession inside tier 1 and must be recorded as one, not glossed (cons-C31).
- **The subset name.** §11.1 and §12.
---

## §8 `alias-tiled` AGAINST `hierarchical`

### 8.1 What is being compared

**Hierarchical** is the x86 / AArch64 archetype: the names of one register form a **nested
chain** of bottom-anchored views — `RAX` ⊃ `EAX` ⊃ `AX` ⊃ `AL`, or `Qn` ⊃ `Dn` ⊃ `Sn` ⊃
`Hn` ⊃ `Bn` (fact-C19, fact-C20). Over 512 bits the natural instantiation is:

> **H-a.** Eight 64-bit lanes; each lane also nameable at 32, 16 and 8 bits, anchored at
> the lane's low end. 8 × 4 = 32 names, one lost to `x0` → **31**, the same budget.

Two other instantiations are worth naming, because they close off the space:

> **H-b — the complete tree.** Name every node at 64, 32 and 16: 8 + 16 + 32 = **56
> names**. There are 31. **H-b does not fit**, and the `f` namespace cannot help, because
> `F`/`D` has no operation narrower than 32 bits (§3.6).

> **H-c — hierarchical above, chained below.** Name every sibling at 64 and 32 (24 names),
> then chain the last 7 downward. **H-c is `alias-tiled`**, with a different answer to the
> only open question — how the last seven names are spent. This is the important structural
> point: *tiled* and *hierarchical* are not rival philosophies. An aligned power-of-two
> tiling **is** a hierarchy (§2.1); the question is only which nodes of the tree you buy.

So the honest comparison is `alias-tiled` (V-A) against **H-a**, the archetype that spends
its names on depth instead of breadth.

### 8.2 The four axes

**(i) Directly nameable packings.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **This axis does not measure capability.**
Every scheme in the table below holds the same 512 bits and can express every width, because
a value that no name fits is packed and reached by shift-and-mask through a scratch name.
What the counts measure is how many width-multisets each scheme places **without paying
those ~2-3 ops per access** — directness, i.e. instruction count. Read every figure in this
subsection that way; **none of them is an admission bound, and the "max bits placeable"
column is 512 in every row, which is the point.** Counting every multiset of live-value
widths over {64, 32, 16, 8} that each scheme can place **directly** (exhaustive enumeration,
`a ≤ 8`, `b ≤ 16`, total ≤ 64 values):

| scheme | admissible packings | max live values by width (64 / 32 / 16 / 8) | max bits placeable |
|---|---|---|---|
| **`alias-tiled` V-A** | **2137** | 8 / 16 / 7 / 7 | 512 |
| **hierarchical H-a** | **495** | 8 / 8 / 8 / 8 | 512 |
| *per-function map (rejected)* | *13091* | *8 / 16 / 31 / 31* | *512* |

H-a's number has a one-line explanation: **within a lane every name contains the next**, so
no two names in a lane are disjoint, so **at most one value can be named directly in a
lane** — eight directly-named values, whatever their widths. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` The struck version
concluded *"the machine holds eight values… a ninth is inadmissible… bit-packing buys
nothing"*: **wrong on all three counts.** A ninth value is packed alongside one of the eight
and reached through a scratch name, so H-a holds the same 512 bits as any other scheme and
bit-packing buys everything it ever bought. H-a's real defect is that it pays ~2-3 extra ops
on nearly every access once more than eight values are live — a **speed** indictment, which
is cons-C30's "fixed aliasing at one width is the SST layout again" reappearing in a
scheme that looks like it has four widths.

The concrete case: **4 × 64 + 5 × 32 = 416 bits**, comfortably inside the file.
`alias-tiled` names all nine directly; **H-a names eight and packs the ninth** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
(the struck version said H-a *rejects* it — it does not; both admit it, and H-a pays ~2-3
ops per access on the packed value). The general statement is that a name whose siblings are
unnamed makes those siblings' bits **indirect**, not stranded: invariant I2's bits are all
still spent, just reached through shifts and masks.

**(ii) Compiler pressure.**

| | `alias-tiled` | hierarchical H-a |
|---|---|---|
| what the allocator sees | 3 register classes with a nesting relation; **complete** at the two widths that matter | 8 chains; every value consumes a whole chain |
| the failure mode | out of bits (a real constraint) | out of **direct names** at 12 % file occupancy — pays ~2-3 ops per access thereafter, and only runs out of bits at 512 like everything else `[corrected - user ruling 2026-09-03 (liveness)]` |
| does narrowing a value help? | **yes** — 2 × 32 fit where 1 × 64 did, which is the entire point of #232 | **yes, in bits** — narrowing always buys room; what it does not buy under H-a is a *direct name*, so the saving is paid back in shift-and-mask ops `[corrected]` |
| back-end machinery | register classes + sub-register indices; RISC-V already carries width-split GPR classes for `Zfinx`/`Zdinx` | identical machinery, fewer allocatable units |
| stock-toolchain fallback | `-ffixed-x9…x31` → 8 × 64, bit-exact RV64 | same |
| allocation algorithm | **first-fit-decreasing is exact** (§6.1 lemma) | trivial (8 slots) and trivially inadequate |

The decisive asymmetry: **there is no stack** (cons-C9), so a function that genuinely
exceeds 512 bits of liveness is *rejected*, not spilled. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` But name pressure is
**not** bit pressure: the struck version said a scheme stranding half the file at 32 bits
*"makes admissible functions inadmissible"* — **it does not.** It makes them slower, by
~2-3 ops per access to a packed value. Rejection comes from bits alone.

**(iii) Decode ROM size.** Hierarchical wins, and it is worth saying by how little:

| | table form | logic form | read-path cost |
|---|---|---|---|
| `alias-tiled` | 31 × (9-bit offset + 2-bit class) = **341 bits** | 2 comparators + 5-bit subtract + 3:1 select of shifted values | 8:1 word select **+ 4:1 intra-word shift** + mask = **5 mux levels** |
| hierarchical H-a | 31 × (3-bit lane + 2-bit level) = **155 bits** | a wire split of the index into {lane, level}, ≈ 0 gates (with one fixup where `x0` steals an encoding) | 8:1 word select + mask = **3 mux levels** — every name starts on a 64-bit boundary |

So `alias-tiled` costs **186 more bits of ROM (23 bytes) per tile and two more mux levels
on the register read path.** Both are real; neither is a cycle. The ROM is 0.07 % of one
tile's context state at C = 1024, and the two mux levels sit behind a decode stage that has
already resolved the offset. **Trading 23 bytes and two mux levels for 4.3× the *directly nameable* packings**
`[SUPERSEDED - user ruling 2026-09-03 (liveness)]` — not for admissible ones, since both schemes admit the same functions — **is
still not a close call**, because the 4.3× is bought back in instructions on every access
the narrower scheme cannot name. But the cost is not zero and should not be presented as
zero.

**(iv) Distinct (offset, width) available to the compiler.** **Both give 31.** They cannot
give anything else: a map is a function from name to range, and there are 31 names (§1.1).
The `f` namespace adds none, under either scheme, for the reason in §3.6.

**This is the finding that reframes the whole question.** The schemes are not distinguished
by how many ranges they offer — they cannot be — but by **which**, and the only property
that matters is **coverage**:

| width | `alias-tiled` coverage | H-a coverage |
|---|---|---|
| 64 | 512/512 = **100 %** | 512/512 = **100 %** |
| 32 | 512/512 = **100 %** | 256/512 = 50 % |
| 16 | 112/512 = 21.9 % | 128/512 = 25 % |
| 8 | 0 % | 56/512 = 10.9 % |

H-a is *broader* below 32 bits and it does not help, because a narrow name whose siblings
are unnamed cannot be used *alongside* anything: coverage at a width is only worth having
when it is coverage of bits some other value could otherwise have used.

### 8.3 The rule this yields, stated once

> **Buying a width completely converts its name constraint into the bit constraint
> (names × width = 512). Buying it partially strands bits. With 31 names you can buy two
> widths completely, and which two is decided by measurement, not taste: 64 because
> addresses are 64 bits, 32 because DESIGN §22's headroom came from a 32-bit `NodeID`.**

Everything else in this proposal is a consequence of that sentence.

---

## §9 PRIOR ART

The context file's §11 item 6 records that **nothing in CANON-DRAFT.md or DESIGN.md
mentions register aliasing at all**, and that the design-review rule requires the claims to
be checked before they are leaned on. They are checked in `register-map-facts.md`; what
follows adds the cases that bear specifically on *tiling*, and marks each with what it
teaches.

**1. ARM AArch32 VFP/NEON — `S`/`D`/`Q`. The closest match in shipping silicon, and it hit
this exact wall.** The register bank is 32 doubleword registers `D0`–`D31` (2048 bits);
`Q0`–`Q15` are 128-bit names, each covering `D2n`:`D2n+1`; `S0`–`S31` are 32-bit names
covering the halves of `D0`–`D15`. So: **complete tiling at 128 and 64, and a *fragment* at
32 — because a 5-bit field affords 32 names and 2048/32 = 64 would be needed.** ARM's
resolution was to confine the narrow tier to a contiguous low region of the file. That is
§2.2's problem and §2.2's answer, twenty years earlier, at four times the size. It also
supplies the counter-example to the claim that a tiled register bank is exotic: NEON code
has used it in production for two decades.

**2. ARM AArch64 — `Bn`/`Hn`/`Sn`/`Dn`/`Qn`. The same vendor deliberately abandoning
tiling for hierarchy, which is exactly the choice this section is about.** In AArch64 the
five letters are five bottom-anchored views of one 128-bit register `Vn`; different-numbered
registers no longer alias, and a write to a narrow view **zeroes** the rest (fact-C20).
ARM bought this with a doubled register file (32 × 128) so that hierarchy's stranding cost
nothing — *the escape route NMFC does not have*, because cons-C4 forbids widening the file.
**Hierarchy is the right answer when you can afford the bits; `alias-tiled` is the answer
when 512 is fixed.**

**3. SPARC V9 floating point — the identical 5-bit arithmetic.** `%f0`–`%f31` are 32-bit
singles; `%d0`, `%d2`, … are 64-bit doubles on even numbers; `%q0`, `%q4`, … are 128-bit
quads on multiples of four. V9 widened the file to 64 doubles (`%d0`–`%d62`) and **the
upper 32 have no single-precision names at all**, because the 5-bit field ran out. Again:
complete at the wide widths, fragmentary at the narrow one, fragment confined to one end.

**4. RISC-V `Zfinx` / `Zdinx` (ratified) — the one-file rule and the narrow-value rule.**
Zfinx deletes the `f` file and makes FP instructions "access the `x` register with the same
number" (fact-C9) — §3.6's rule, ratified. Zdinx solves *wider than a name* with **aligned
register pairs**, an explicit low-register-holds-low-bits ordering, and a specified `x0`
behaviour (fact-C16) — the model for §2.2's `f0` ≡ `x0` = +0.0 and for §11.5's answer to
values wider than 64 bits. And Zfinx's "Processing of Narrower Values" supplies the
replacement for NaN-boxing that fact-C11 demands (§3.7). **Three of this proposal's rules
are quotations.**

**5. x86-64 — `RAX`/`EAX`/`AX`/`AL`, the cautionary half.** Hierarchical, bottom-anchored,
with two irregularities worth carrying: the write rules differ by width (writing `EAX`
zero-extends; writing `AX` or `AL` preserves), and `AH`/`BH`/`CH`/`DH` are non-contiguous
views of bits 15:8 surviving from 1978. The preserve-on-narrow-write rule produced
**partial-register merge stalls** (fact-C19). §3.7 and §4.2 show why `alias-tiled` cannot
inherit that hazard: there is nothing to merge, because a tile's neighbours are other
architectural names and are simply not written.

**6. IBM System/360 and MIPS-I / SPARC V8 — even/odd register pairs.** A 64-bit double
named by an even register number, the odd one implied; misaligned numbers illegal. Thirty
years before Zdinx re-ratified it, which is decent evidence that **pairing is the durable
answer to "a value wider than a name"** (§11.5).

**7. IBM POWER VSX — two namespaces over one file, done in shipping hardware.** The 64
`VSR`s are the 32 FPRs (as doubleword 0 of `VSR0`–`VSR31`) plus the 32 VMX registers
(`VSR32`–`VSR63`). It is the "two namespaces over one file" of canon I.7 built at scale —
and note that IBM had to specify *which half* of the VSR an FPR occupies, exactly as
Zdinx specifies which register holds the low bits. Any aliasing proposal owes that
sentence; §2.2's table is it.

**8. Intel MMX over the x87 stack — the cautionary tale of aliasing two namespaces with
different geometry.** `MM0`–`MM7` alias the mantissa fields of `ST(0)`–`ST(7)`, and the
mismatch required `EMMS` and a mode discipline that outlived its usefulness. `alias-tiled`
avoids the failure by making the two namespaces **geometrically identical** rather than
merely overlapping (§3.6) — no mode, no transition instruction, nothing to forget.

**9. RISC-V `P`/`Zp*` (NOT ratified) and CORE-V `cv.extract` / T-Head `th.ext`.** `P` packs
lanes into `x` registers and names the lane width **in the opcode**, with no name for an
individual lane (fact-C21). CORE-V and T-Head carry **(offset, width) as immediates in the
instruction** (fact-C22) — option 1 done properly, and the genuine alternative to this
proposal. It is not pursued here for two reasons that are in the record rather than in
taste: cons-C26 already closed it ("a bit-field insert and extract carrying an offset and a
width was considered and dropped"), and cons-C23 leaves no encoding space to reopen it in.

**10. RISC-V `V`'s `vtype.SEW` — the mechanism that is ruled out.** Element width lives in
a **CSR** set by `vsetvli` (fact-C9). It is the third home for width, and it is precisely
the per-context mode register cons-C1 forbids. Cited here so that nobody offers it as
prior art *for* this design: it is prior art for the thing that was rejected.

**11. Tagged architectures** — Burroughs B5000/B6700, Symbolics, System/38 — put the type
on the *value*. That is the fourth home for width, and the 512-bit budget forbids it: tags
cost bits inside the context (fact-C24).

**Summary of what prior art actually settles.** Width has four possible homes — the
register name (x86, AArch64, AArch32, SPARC), the opcode (68k, SVE, RISC-V `P`), a mode
register (RVV), and the value (tagged machines). Three are ruled out here by cons-C1 and the
512-bit budget. Of the shipping designs that put width in the **name**, every one that had
to cover a file wider than 32 × width made the same choice this proposal makes: **complete
tilings at the wide widths, a fragment at the narrow one, confined to one end of the file.**

---

## §10 COST

### 10.1 State

| where | `alias-tiled` | for comparison: the rejected per-function map |
|---|---|---|
| **per context** | **0 bits added.** A context is 512 bits; a context + PC is 576 bits = 72 B | 0 added (the table was not per context) |
| **per function** | **0 bits.** There is no per-function object of any kind | `RegLayout` = 32 × (`uint16` offset + `uint8` width) = **768 bits (96 B)** as built; **512 bits** packed minimally — *almost exactly one context, per resident function* |
| **per tile** | **341 bits** of decode ROM (31 × 11), or ≈ 50 gates of comparator + subtract + shift, **shared by every context and every function** | the same 341-bit path, **plus** one `RegLayout` per resident function beside the I-cache, plus the lookup |
| **per tile, as a fraction** | 43 B against 64 KiB of context state at C = 1024 → **0.065 %** | 96 B × (resident functions) on top |
| **migration** | **72 B exactly** — 64 B context + 8 B PC (cons-C6, **M11**). Nothing the scheme adds travels | 72 B (the table did not travel; it was resident) |

### 10.2 Time

| path | cost |
|---|---|
| **decode** | **+0 cycles.** The map resolves from the instruction's own 5-bit field, in parallel with opcode decode, from two comparisons and a shift (§4.1) |
| **register read** | **+2 mux levels** over a fixed 8 × 64 file — an 8:1 word select plus a 4:1 intra-word shift, with the shift amount known a stage earlier (§4.2, §8.2 iii) |
| **register write** | **+0.** Byte enables, no read-modify-write, no merge, no partial-register hazard (§4.2) |
| **memory** | **unchanged.** Nothing is fetched to decode a register name (cons-C1, cons-C2) |

### 10.3 ISA surface

| | change |
|---|---|
| new instructions | **none** (cons-C22 intact: twelve user-level + privileged `RESUME`) |
| `funct7` space consumed | **none** (cons-C23: `0x6`/`0x7` still free) |
| encoding format changes | **none** (§3.1) |
| instructions **removed** from the function-core subset | `fmv.d.x`, `fmv.x.d`, `fmv.w.x`, `fmv.x.w`, `flw`, `fsw`, `fld`, `fsd` — as `Zfinx`/`Zdinx` remove them (§3.6) |
| new legality checks | six (§3.3) |

---

## §11 WHAT THIS CANNOT DO

Read this section as the price list. *Free truncation is not on it* — the low half of `d`_k_
**is** `w`_2k_, so narrowing a 64-bit value to 32 costs no instruction, which is the one
thing x86 users expect from aliasing and the one thing tiling gives for nothing.

**11.1 It narrows the ruled subset.** `RV64IMAFD` was ruled by O4; §3.6 deletes eight
instructions from it. Any function using an FP load, an FP store or an `fmv` transfer must
be re-emitted with `lw`/`ld`/`sw`/`sd` — mechanical, and exactly the `Zfinx` ABI
difference, but it makes a previously admissible function inadmissible until it is
recompiled, which cons-C16 requires be said in those words. The correct name for the result
is **`RV64IMA_Zfinx_Zdinx`** (facts §6.1), and §12 proposes the amendment.

**11.2 No non-power-of-two width has a name of its own, and this costs instructions, not
capacity.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` A 48-bit pointer, a 12-bit index, a 3-bit tag: none has a name, so
each is packed and reached by shift-and-mask through a scratch name at ~2-3 ops per access.
**Nine 48-bit values are 432 bits and they fit** — the struck version said they *"do not
fit, because they need nine 64-bit tiles"*, which charges every value the width of a name.
The per-function map places them with a map lookup instead of the shifts; **the capacity is
the same 512 bits either way.** §8.2's 13091-vs-2137 counts directness, not capability.

**11.3 No byte tier of *names* — and #232's example is fully expressible.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
"It could be 16 4-byte regs, **64 1-byte regs**, or ANY combination." Sixteen 4-byte
registers is `w0`–`w15` and is exact. **Sixty-four 1-byte registers is also expressible**:
eight bytes pack into each of the eight 64-bit tiles and each is read and written by
shift-and-mask through a scratch name — plain RV64I, ~2-3 ops per access, no new
instruction. The struck version said it *"is not expressible and cannot be made so"* and
set a *"ceiling of seven live values narrower than 32 bits"*; **both are wrong and are
struck.** What 64 names would buy is **directness**, and 31 names cannot buy it at four
widths. **Canon I2's "ANY combination" stands unnarrowed.**

**11.4 A 16-bit name has no ratified arithmetic.** There is no RV16I. §3.4 defines `add`,
`sub`, shifts and compares at 16 bits under the width-from-name rule, which is the one
place this proposal specifies behaviour no RISC-V document specifies. An implementation may
instead make them illegal and use `h` names only for carry/compare/load/store; the choice
is open (§12) and neither reading is ratified.

**11.5 No name is wider than 64 bits.** A 128-bit value has no name. The remedy is
Zdinx's — an **aligned pair** `d`_2k_:`d`_2k+1_ with the low-numbered name holding the low
bits, odd numbers reserved (fact-C16) — and it is *not proposed here*, because nothing in
the record asks for a 128-bit value and adding the rule costs a legality check and an
alignment constraint for no measured benefit. If it is ever wanted, the shape is settled
prior art.

**11.6 The `f` namespace contributes nothing.** Thirty-two encodings buy zero additional
ranges (§3.6). Seven of them (`f25`–`f31`) are reserved dead space, because `F`/`D` has no
16-bit operation. `Zfh` would turn them into an FP view of the `h` tier; `Zfh` is not in the
subset, and this proposal does not ask for it.

**11.7 The width mix is frozen ISA-wide.** A function that is all 64-bit leaves 23 names
unused; a function that wants twelve halfwords cannot have them, though the bits exist.
Per-function retuning is precisely what the 2026-09-03 ruling removed, and this is where
the removal is felt. Recorded, not argued (cons-C32).

**11.8 A stock toolchain reaches only the `D` tier.** `-ffixed` reserves registers; it
cannot retype them. Everything above 8 × 64 needs a register-class split in the back end
(§6.4). DESIGN §24 step 5's cheapest path still works and still stops short.

**11.9 It does not solve the compiler's packing problem.** It converts it from "choose 32
(offset, width) pairs per function" into "colour a three-class interference graph", which
is a problem every back end already solves — but somebody still has to do the work, and
I.8's "open, and it is a compiler problem" stays open.

**11.10 Disassembly lies by default.** Stock `objdump` prints `x23` and implies 64 bits
where the ABI means `w14` at 32. An alias table fixes the display; nothing fixes a reader
who does not know the map (§3.1).

**11.11 The `h` tier's position is fixed at bits 384–495, and it overlaps the eighth
`d` name.** A function wanting eight live 64-bit values **and** a halfword cannot name both
directly — those are the same bits. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` It is **not inadmissible**: eight live
64-bit values are already 512 bits, so a ninth value of any width exceeds the file by plain
arithmetic, and any function under 512 bits places its halfword inside a `d` name and reaches
it by shift-and-mask. The struck formula `a ≤ 8 − ⌈⌈c/2⌉/2⌉` is a directness rule, not an
admission rule.

**11.12 One halfword of the file (bits 496–511) has no 16-bit name**, because only seven
names remain after 24 are spent and an eighth would have to be `x0`. It is nameable at 32
(`w15`) and 64 (`d7`); only the direct halfword view is missing, and a halfword living there
is reached by shift-and-mask through a scratch name at ~2-3 ops per access `[user ruling
2026-09-03 (liveness)]`. **No bit of the file is out of reach.**

---

## §12 WHAT TO BUILD, WHAT TO MARK, AND WHAT IS STILL OPEN

### 12.1 The build

| file | change |
|---|---|
| `src/nmfc/src/NMFCRegLayout.h` | **Delete `RegLayout`.** Replace with a `constexpr` map: `struct RegName { uint16_t offset; uint8_t width; };` and `constexpr RegName decodeX(uint32_t i)` / `decodeF(uint32_t i)` implementing §2.2 arithmetically. Keep `Context512`; drop its straddle branch (§4.3) or leave it as unreachable-by-construction with an assertion. Rename the file `NMFCRegMap.h` so no reader thinks a layout survives. |
| `src/nmfc/src/NMFCTile.h` | Delete `RegLayout layout_;` and the "one entry per resident function" comment (`:448-450`). Nothing replaces it. |
| `src/nmfc/src/NMFCTile.cc` | `readReg`/`writeReg` (`:460-475`) call `decodeX`. `illegal` (`:477-489`) keeps its shape and gains the six causes of §3.3; its message stops quoting `layout_.bitsUsed()` and starts quoting the name's width and the opcode's. |
| `src/nmfc/include/nmfc_isa.h` | `NMFC_CTX_WORDS`/`NMFC_CTX_LANES` keep their meaning (storage and aperture) and gain one line: **lane *k* is tile `d`_k_** (§5.2). The "nothing may treat 8 as a design constant" rule stands — the `D` tiling is 8 *because 512/64 = 8*, which is arithmetic, not an assumption. |
| `tools/nmfc/annotate.cc` | `:524-559` — the slot pool becomes width classification + first-fit-decreasing placement (§6.1); the `bits` figure at `:555-559` stops being a stderr line and becomes the gate, tested by §6.2's closed form. |
| new | an ABI header defining `d0`–`d7`, `w0`–`w15`, `h0`–`h6` as assembler aliases, and a `.md` note for reading disassembly. |

**Order to build in**, so that each step is testable: (1) the `constexpr` map and the
decode change, with the existing 8 × 64 functions running unmodified through the `D` tier —
this is a pure refactor and must change no behaviour; (2) the legality checks; (3) a
hand-written function using `w` names, which is the first thing that exercises the `W`
tier; (4) `annotate`'s admission rewrite; (5) the `h` tier.

### 12.2 What must be marked superseded (cons-C31)

1. **DESIGN.md §25.7, D:2560-2567** — "one small table entry beside the instruction
   cache … It adds nothing to the 512 bits and does not scale with `C`." **Overruled** by
   the 2026-09-03 ruling. Mark, do not delete.
2. **CANON-DRAFT.md:9804**, the clause "`f3` and `x3` are different names at different
   offsets, not one slot." **Superseded** by §3.6 if this proposal is adopted; the rest of
   that sentence — 512 bits of live storage, not 64 slots — is preserved and strengthened.
3. **`NMFCRegLayout.h`, `NMFCTile.h:448-450`, `NMFCTile.cc:460-475`** — become
   divergences in Appendix 2's sense the moment a replacement is adopted, and are then
   fixed by §12.1. They are not evidence for the rejected design (tier 4 decides nothing).
4. **Appendix 2 `S5`** — restate as §7's **M15** row.
5. **A new ledger entry** of the same shape as L45, recording that a tier-3 passage and a
   tier-1 clause were overruled by a tier-1 ruling, with this file as the replacement.
6. **The subset name** — propose amending O4's `RV64IMAFD` to **`RV64IMA_Zfinx_Zdinx`**
   (§11.1, facts §6.1). This is a naming correction that costs the design nothing and buys
   it a ratified specification for the semantics it already wants. It is an *amendment to a
   user ruling* and therefore needs the user, not this document.

### 12.3 Constraint check, row by row

| | verdict |
|---|---|
| **C1** no state outside the context and the encoding | **met** — §4.1: two comparators, a subtract, a shift; nothing fetched, nothing per function, nothing per context |
| **C2** no third referenced object at decode | **met** — the decode map is combinational, of the same kind as the opcode decoder's truth table; instruction + data remain the only two objects |
| **C3** 512 bits, bit-packed, not eight registers | **met** — three tilings, 31 names, 2137 packings; the 8 in the `D` tier is 512/64, an arithmetic consequence, and `w`/`h` names make the file demonstrably not eight registers |
| **C4** the file may not be widened | **met** — 512 exactly |
| **C5** 512 in, 512 out, PC beside | **met** — unchanged |
| **C6** migration exactly 72 B | **met** — §10.1, zero bits added |
| **C7** one file, two namespaces, no separate FP file | **met, and made structural** — §3.6: the namespaces are the *same names* |
| **C8** no `fcsr`, no rounding state | **met** — static `rm` only; `rm = DYN` is illegal (§3.2) |
| **C9** no stack, no spill | **met** — nothing here creates a spill slot; §11 records that pressure failures are rejections, not spills |
| **C10** subset is `RV64IMAFD` and nothing else | **deviates, declared** — §11.1 removes eight instructions and proposes the `Zfinx`/`Zdinx` renaming; nothing is *added* |
| **C11** no vector extension anywhere | **met** — §5.1 withdraws the V-based retrieval path explicitly |
| **C12** nothing blocks | **met** — untouched |
| **C13** nothing speculative | **met** — untouched |
| **C14** undefined register is a hard error | **met, re-homed** — **M6**: legality traps at the tile, admission rejects at build |
| **C15** rejection is fatal, no truncation | **met** — §6.3 |
| **C16** "cannot be expressed ⇒ cannot be offloaded" is allowed, but regressions must be stated | **met** — §11 is the statement, and §11.1 (the ruled-subset narrowing) names the one previously-admissible class that regresses. `[CORRECTED - user ruling 2026-09-03 (liveness)]` **No width regresses**: §§11.2, 11.3 and 11.11 are struck as capability claims and restated as instruction-count claims. |
| **C17** admission counts peak liveness in bits, one pool | **met** — §6.2; and §3.6 makes the single pool structural |
| **C18** not a count of names | **met** — at 64 and 32 the name count *is* the bit count; the only name count is `c ≤ 7`, declared as a fragment |
| **C19** a register never read costs nothing | **met** |
| **C20** admissibility is a property of the generated code | **met** — §6.4 works on the emitted widths, not the source types |
| **C21** the two measured functions must still fit | **met** — §6.2's table: `nmfc_bu` 480 bits with a `w` tile spare, `nmfc_expand` 384 with 128 spare |
| **C22** twelve instructions plus `RESUME` | **met** — none added (§10.3) |
| **C23** only `funct7` `0x6`/`0x7` free | **met** — none consumed |
| **C24** the canon assigns no field values | **met with a note** — §2.2 fixes index ranges because compiler and core must agree on them; they are an ABI constant, not a `funct7` value, and are marked as such |
| **C25** every operand is a value in a GPR | **met** — **M12**, unchanged |
| **C26** no bit-field insert/extract with runtime (offset, width) | **met** — §3.5 uses only ratified idioms; §9 item 9 records the `th.ext` alternative as closed |
| **C27** `CXW`/`CXR` complete as the aperture | **met, and improved** — §5.2: one `CXR` per named value, never two |
| **C28** the lane is an instruction field | **met** — unchanged |
| **C29** tier 4 never decides anything | **met** — §12.1 rewrites the tier-4 artefacts rather than citing them |
| **C30** a fixed table at one width is the SST layout again | **met** — three widths, and §6.4 marks the 8 × 64 stock-toolchain path explicitly as a *fallback*, not the design |
| **C31** the superseded passage must be marked | **met** — §12.2, including one the fit-list did not list |
| **C32** do not attribute reasons the user did not give | **observed** — the rejected map is never argued against on latency, area or migration grounds; §8 and §11 price this proposal without pricing that one |

### 12.4 Still open after this proposal

1. **The last seven names.** Seven halfwords (V-A, recommended); or four halfwords and
   three bytes; or six halfwords and one reserved. §8.3's rule does not decide a
   *fragment*, and no measurement in the record does either — DESIGN §22's two functions
   use none of them.
2. **16-bit arithmetic** — defined (§3.4) or illegal (§11.4). Needs a hardware opinion.
3. **The `h` region's placement** — bits 384–495 is argued in §2.2 but the argument is
   about tile-blocking efficiency, not about any measured function.
4. **Whether `d`-pairs are ever wanted** for values wider than 64 bits (§11.5).
5. **The subset renaming** (§12.2 item 6) — a user decision.
6. **The compiler back end.** §6.4 says which machinery is needed and claims it exists
   upstream; nobody has built it, and the claim about `GPRF32`/`GPRF64`/`GPRPair` should be
   verified against the actual tree before it is relied on.

---

## APPENDIX — THE MAP ON ONE PAGE

```
bit   0                                                                                             511
      |-----d0----|-----d1----|-----d2----|-----d3----|-----d4----|-----d5----|-----d6----|-----d7----|
      |  w0 |  w1 |  w2 |  w3 |  w4 |  w5 |  w6 |  w7 |  w8 |  w9 | w10 | w11 | w12 | w13 | w14 | w15 |
                                                                              |h0|h1|h2|h3|h4|h5|h6|--|

 x0            zero at any width      f0            +0.0, writes discarded
               (no bits, exempt from the equal-width rule)
 x1 - x8   =   d0 - d7  (64 bits)     f1 - f8   =   d0 - d7   (fadd.d, fmul.d, ...)
 x9 - x24  =   w0 - w15 (32 bits)     f9 - f24  =   w0 - w15  (fadd.s, fmul.s, ...)
 x25- x31  =   h0 - h6  (16 bits)     f25- f31      reserved, illegal

 d_k = bits [64k, 64k+64)      lane k of CXW/CXR
 w_k = bits [32k, 32k+32)      = the low/high half of d_(k>>1)
 h_j = bits [384+16j, +16)     j = 0..6, inside d6 and d7

 an instruction executes at the width of its destination name:
   d -> RV64 semantics        w -> RV32 semantics (and FLEN=32 for F)        h -> moves, logic, compares
   addw/mulw/... : d only     fadd.d : d only      fadd.s : w only           rm=DYN : illegal
   a write touches exactly its own bits -- no sign-extension, no NaN-boxing, no neighbour

 admissible  <=>  peak( live bits + scratch bits ) <= 512, each value at its OWN width
                  and one spare name free for staging      [user ruling 2026-09-03 (liveness)]
                  -- no cap on narrow values, nothing charged 32, K.6 verbatim, always

 DIRECTLY NAMEABLE (an instruction-count rule, NOT admission):
                  c <= 7 and 2a + b <= 16 - ceil(c/2) and a <= 8 - ceil(ceil(c/2)/2)
                  anything outside it is packed: ~2-3 extra ops per access
```
