# THE REGISTER MAP — recommended design

**PROPOSAL — NOT YET IN THE CANON.** Nothing in this file may be cited as a decision.
It answers one question: *what does the 5-bit register field of an ordinary
`RV64IMAFD` instruction mean inside a function core, when the answer must come from the
instruction alone?* It is written to be ruled on, and §10 lists the rulings it needs.

**Provenance.** Three proposals were written and judged:
`register-map-alias-tiled.md` (mean 8.00), `register-map-alias-hierarchical.md` (7.17),
`register-map-alias-plus-width-in-opcode.md` (6.17). This document is not a fourth
proposal. It takes **`alias-tiled`'s argument** — the completeness analysis, the
coverage table, the placement lemma, the fit-list discipline and the prior-art survey —
and **`alias-hierarchical`'s map** — the index anchoring at `x8`–`x15`/`x16`–`x31`, the
reserved low names, and the read/write port rules — because the judges found
`alias-tiled`'s index assignment to be its clearest defect and `alias-hierarchical` had
already solved it. Eleven further defects the judges found in both are repaired here;
each repair is marked **[FIX]** with the flaw it closes.

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

**Prerequisites.** `register-map-facts.md` (verdicts **fact-C1**–**fact-C24**) and
`register-map-context.md` (mechanisms **M1**–**M15**, constraints **cons-C1**–**cons-C32**).
Every claim about RISC-V below carries a chapter of the ratified Unprivileged ISA Manual
**v20260120** or names the extension; where this design departs from ratified behaviour
it says so in the same sentence.

---

## §1 THE PROBLEM

### 1.1 The user's question, verbatim (2026-09-03)

> "presumably risc-v has a way to move values directly between floating point and integer
> regs right? So we don't run into any trouble retrieving values and then inserting them
> into the proper float regs (when retrieving the 512 bit regfile post-execution). The
> system inside the nmfc core is far trickier, any set of bits can be interpreted as any
> type. Two ways around that: 1. custom instructions that encode type into them (assuming
> risc-v has some instructions which infer based on target registers), 2. clever aliasing
> system -> all ISA regs map to certain bit ranges inside the 512-bit regfile, so
> utilizing a float reg implies the type, while the number of the reg implies the slice."

### 1.2 The hard constraint, verbatim (user ruling, 2026-09-03)

The per-function register map — DESIGN §25.7's "one small table entry beside the
instruction cache", built in the SST tree as `RegLayout` — was rejected:

> "I really don't like your idea. It introduces a third piece of memory every context
> needs. So now we have the map, instruction, and potentially data that must be referenced
> all at the same time. That frankly seems foolish."

So: **no state outside the 512-bit context and the instruction encoding** (cons-C1). The
register name, its namespace (`x` vs `f`), and the opcode must fully determine
(bit offset, width, type), with nothing fetched to decode (cons-C2).

### 1.3 What is actually out of reach, and which of it the ruling is responsible for

**[FIX — judges: "'64x8, any mix' is inexpressible, and that is canon rather than a
nicety … it belongs in §1"; "a canon-level conflict … under-framed".]**
**[FIX — review of 2026-09-03, and this is the largest correction in the document: the
previous draft of this section asserted "at most two widths can be complete (8 + 16 = 24
names), and any third is a fragment. This is arithmetic, not a design choice." **That is
false arithmetic.** 8 + 16 + 32 = 56, and 56 ≤ 63. Three complete widths are nameable on
this document's own budget. The corrected argument is below, and it is a subset argument
plus a design choice — exactly what the previous draft denied.]**

Canon I2's `[SHARPENED]` block quotes user #232 directly: the context "could be 16 4-byte
regs, **64 1-byte regs**, or ANY combination." Three separate things stand between that
sentence and the map recommended here, and they are of three different kinds. They must
not be merged into one "impossibility", because only the first is one.

> **(a) The counting bound — it bounds DIRECT NAMING, not capability.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
> **The heading and conclusion of this item are struck.** A 5-bit field bounds how many
> slices can be **named directly**; it does not make any slice unreachable. Sixty-four live
> bytes pack eight-to-a-tile across the eight 64-bit names and are read and written by
> shift-and-mask through a scratch name — plain RV64I, ~2-3 extra ops per access. **The byte
> tier is REACHABLE**, and canon I2's *"64 1-byte regs, or ANY combination"* stands
> unnarrowed. Read the arithmetic below as what it is: a count of **names**, which is a
> statement about instruction count. The struck text follows. A register field is
> five bits (Ch. 2, "the five-bit *rs1* and *rs2* fields"), so each namespace affords 32
> encodings. `x0` is hardwired zero (Ch. 2) and cannot be a slice without invalidating
> `nop`, `j` and the whole HINT space (fact-C17), leaving **31** names in the `x`
> namespace and 32 in `f` — **63 at the very most**, and 63 only if the two namespaces
> name *different* bits. Complete coverage at width *w* costs 512/*w* names: 8 at 64, 16
> at 32, 32 at 16, **64 at 8**. Sixty-four names do not exist. **"64 one-byte registers"
> cannot be named ~~by~~ DIRECTLY UNDER any scheme in which the register number alone denotes
> the slice — they are expressed by packing, at ~2-3 ops per access.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
> fact-C17 reaches the same result from the other side — 512/63 ≈ 8.1 bits per name, so a
> uniform byte map does not fit the nameable set. **This one is arithmetic.**
>
> **(b) What counting does NOT forbid.** 8 + 16 + 32 = **56 names**, and 56 ≤ 63.
> Complete coverage at 64, 32 **and** 16 bits simultaneously is inside the budget — e.g.
> `x8`–`x15` as the eight 64-bit tiles, `x16`–`x31` as the sixteen 32-bit tiles and
> `f0`–`f31` as the thirty-two 16-bit tiles: 56 of 63 names, 7 spare. A three-tier map is
> **arithmetically available**. It is declined here for the two reasons that follow, and
> those reasons are contestable in a way arithmetic is not.
>
> **(c) The subset argument — why the 16-bit tier is not taken.** `RV64IMAFD` contains
> **no register-to-register arithmetic narrower than 32 bits**: there is no RV16I, and
> `F`'s narrowest operation is 32 bits (`Zfh` is outside the ruled subset, O4). A complete
> 16-bit tier would name bits that no instruction in the ruled subset can compute on — it
> would buy load/store/move granularity only, and §2.3 records that memory already has
> that granularity for free. This is a **subset** argument and it is contingent: it
> dissolves the day O4 is amended to admit `Zfh` or a 16-bit integer tier.
>
> **(d) The design choice — `f`_n_ ≡ `x`_n_ spends the second namespace.** §3.2 makes the
> two namespaces name identical bits, which is what makes K.6's "third wrong answer"
> unrepresentable and gives the allocator one pool instead of two. Its price is exactly
> the 32 names the 63-name budget counted: after it the budget is 31, and 8 + 16 = 24 of
> 31 is the most that fits. facts §6.1 calls this "**the real fork in option 2**" and says
> in terms that the choice has not been made. **§10 question 6 puts it to the user**, and
> the arithmetic of (b) is why it is a fork rather than a consequence.

So the price is three-part, and **no part of it is a loss of capability** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`:
the byte tier is not **directly nameable** under any name-denotes-slice scheme, so byte
values are packed and reached through a scratch name at ~2-3 ops per access (this belongs
to the **name density**, not to the 2026-09-03 ruling, and it costs instructions, not
bits); the 16-bit tier is directly nameable but computationally inert under **ruling O4**
(a consequence of the subset); and the second namespace's 32 names are given up by **this
document's own recommendation** in §3.2. Each part changes how *directly* a value is
reached. **Capacity is 512 bits in every case.** Recorded this way per cons-C32 — the ruling's stated reason was
the third referenced object, and nothing below argues against the rejected map on any
other ground.

What *is* recommended: **eight 64-bit names and sixteen 32-bit names, both complete, over
the same 512 bits.** #232's "16 4-byte regs" is exact. #232's "8 8-byte regs" is exact.
Every mixture of the two is exact, **and so is #232's "64 1-byte regs"** — packed, and
reached by shift-and-mask through a scratch name. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` Values narrower than 32 bits
have no **direct name**; they are ~~charged 32 bits~~ **charged their own width, like every
other value**, and cost about 2-3 extra ops per access. **There is no regression against
K.6's bit test**: admission is bits of peak liveness plus the scratch bits the packing
needs. Every "charged 32" and "regression" claim in §3.8 and §9.2 is struck. Seven encodings are **reserved** against the day a measured function
wants one (§10 question 2).

### 1.4 The argument in five lines

1. A name, **together with the opcode that uses it**, buys a slice — that is cons-C1's
   wording, and §3.4's width rules do use the opcode. Under §3.2's map the name *alone*
   fixes (offset, width) and the opcode supplies only type and operation; that is this
   document's recommendation, not something the ruling forces (§7.4 point 5 records the
   one thing a width-carrying opcode genuinely buys, and §1.3(b) records that a third
   complete tier is nameable). Two names on one range buy nothing. **The question is never
   "how many names" — it is "which ranges".**
2. A width is **cheapest to use** where its names cover the file: bits no *w*-wide name
   reaches can still hold a *w*-wide value — packed, and reached by shift-and-mask through a
   scratch name at ~2-3 ops per access. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **No bit is unreachable; bit-packing
   buys every one of the 512.** Coverage buys directness, not capacity.
3. Buying a width *completely* makes *names × width = 512*, so **the name budget becomes
   the bit budget** and cannot bind before it. Buying it partially strands bits.
4. 64 is not optional (addresses are 64 bits; `nmfc_bu` holds seven 64-bit values). 32 is
   the measured one (DESIGN §22's headroom came from narrowing a `NodeID` to 32 bits).
   8 + 16 = 24 of 31 names buy both completely.
5. Therefore admission is **K.6's bit test, verbatim and in bits**: peak simultaneous
   liveness **plus the scratch bits the packing needs ≤ 512**, one pool, each value charged
   its **own** width, not a count of names (cons-C17, cons-C18). `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` The
   ~~"every value narrower than 32 bits charged 32"~~ clause is **struck**; there is no
   regression against K.6 and cons-C16 is not engaged. What the map's two widths decide is
   **instruction count** on sub-name accesses.

---

## §2 WHAT STANDARD RISC-V ALREADY PROVIDES, AND WHAT IT CANNOT

### 2.1 The half of the question that is already solved: moving bits between namespaces

The user's first sentence is correct and the mechanism is ratified.

| move | instruction | spec | exact behaviour |
|---|---|---|---|
| 64 bits, integer → float | `fmv.d.x` | Ch. 21 §21.1.5 | raw bits, no conversion, non-canonical NaN payloads preserved. **RV64 only** — RV32D has no such instruction and needs Zfa's `fmvp.d.x` (fact-C1) |
| 64 bits, float → integer | `fmv.x.d` | Ch. 21 §21.1.5 | raw bits, no conversion |
| 32 bits, integer → float | `fmv.w.x` | Ch. 20 | raw 32 bits; **NaN-boxes**, writing all 1s to bits FLEN−1:32 (Ch. 21 §21.1.2) |
| 32 bits, float → integer | `fmv.x.w` | Ch. 20 | raw 32 bits; **sign-extends** bit 31 into bits 63:32 on RV64 (fact-C3) |

Two corrections to the provisional answer of 2026-09-03 belong here:

- **The mnemonics are `.w`, not `.s`.** They were renamed at spec v2.2 precisely to signal
  *moves 32 bits without interpreting them* rather than *moves a single-precision value*
  (fact-C2). A document writing `fmv.x.s` is quoting a pre-2017 spec.
- **The V-extension half of the provisional answer is WITHDRAWN.** `vmv.x.s`,
  `vslidedown` and `vfmv.f.s` are real (fact-C5) but unavailable: `V` is outside the ruled
  subset (O4), **Rev implements no vector unit, Vanadis implements none**, and RoCC carries
  128 bits (cons-C11, I.8's prior-art check). Two further facts make the retraction
  cheap rather than painful: holding 512 bits in **one** architectural vector register
  requires **`Zvl512b`**, not plain `V` — plain `V` guarantees only VLEN ≥ 128 via `Zvl128b`
  (fact-C7); and even where `V` exists, a 64-byte store followed by ordinary loads beats the
  `vsetvli`-plus-`vslidedown` path (fact-C8). §4 gives the path that is actually available.

### 2.2 The half that is already solved and was mis-stated: typing

> **RISC-V never infers an operation's type from a register name. The opcode types the
> operation.** `add`/`addw`, `fadd.s`/`fadd.d`, `flw`/`fld` — the type and the operation
> width are in the mnemonic, always.

The strongest evidence is a ratified extension: under **Zfinx** (Ch. 26 §26.1) the `f`
registers are deleted and floating-point instructions operate on the `x` registers —
"whenever such an instruction would have accessed an `f` register, it instead accesses the
`x` register with the same number." So `add x5, …` and `fadd.s x5, …` name the **same
architectural register** and differ only in opcode (fact-C9). **Remove the namespace and
nothing about typing breaks.**

Two nuances that bear directly on the user's option 2:

- The `x`/`f` split is a *coarse legality constraint* — `fadd.d x1, x2, x3` is not
  writable in F/D — which is what makes "utilizing a float reg implies the type" feel
  right. It is redundant with the opcode, never a substitute for it.
- **The V extension is the one genuine counterexample**, and it is the mechanism this
  design is forbidden to use: element width comes from `vtype.SEW`, set by `vsetvli` —
  a **CSR** (fact-C9). That is precisely the per-context mode register cons-C1 forbids.
  It is prior art *for the thing that was rejected*, and §6 cites it that way.

**Consequence: the user's option 1 is redundant *for type*.** An instruction that "encodes
type into it" is describing what every RISC-V instruction already is (fact-C10). §7.2
records the one thing option 1 could still have bought — *extent* — and why that too is
closed.

### 2.3 The half that is genuinely missing

> **RV64 has no name for a register narrower than XLEN.** The register file is defined at
> one width: "For RV32I, the 32 `x` registers are each 32 bits wide" (Ch. 2), with RV64I
> widening the same 32 names to 64. There is no architectural name for a half, a byte, or
> a bit of an `x` register (fact-C12). **That gap, and only that gap, is what this design
> closes.**

What RISC-V *can* already do, so that the gap is not overstated (fact-C14):

- **Memory operands** move 8/16/32-bit fields with explicit extension: `lb`/`lbu`/`lh`/
  `lhu`/`lw`/`lwu`/`sb`/`sh`/`sw`. A 64-byte buffer is fully field-addressable at no cost.
- **Zbb** has `sext.b`/`sext.h`/`zext.h`; **Zbs** has `bext`/`bexti`; **Zbkb** (ratified)
  has `pack`/`packh`/`packw`, which *assemble* sub-register fields explicitly.
- So RISC-V can **compute on** and **assemble** sub-register fields. What it cannot do is
  let a 5-bit register field **denote** one.

And the one ratified fact that is in direct opposition to this whole design, which must be
absorbed rather than sidestepped (fact-C13, facts §6.3):

> Ch. 5's design note: "The compiler and calling convention maintain an invariant that
> **all 32-bit values are held in a sign-extended format in 64-bit registers.** Even 32-bit
> unsigned integers extend bit 31 into bits 63 through 32."

`addw`/`subw`/`sllw`/`srlw`/`sraw` exist to *enforce* that invariant — they sign-extend a
32-bit result into 64 bits — so they are the opposite of a slice mechanism. Bits 63:32 of a
stock RV64 register are not a second field; they are redundant copies of bit 31. Bit-packing
wants those bits for a different value. §3.4 shows where the invariant goes: **it moves from
the register file to the read port**, and no compiler assumption is lost.

### 2.4 The ratified precedent for "the number of the reg implies the slice"

**Zdinx** (Ch. 26 §26.1) faces the identical problem — 64-bit doubles, 32-bit names — and
answers it completely (fact-C16): a fixed width per name; **aligned register pairs** for
wider values, with odd-numbered registers *reserved*; an explicit endianness-independent
rule that "the lower-numbered register holds the low-order bits"; and a specified `x0`
interaction — "when a double-width floating-point result is written to `x0`, the entire
write takes no effect … when `x0` is used as a double-width operand, the entire operand is
zero." **Every rule in §3 is written in Zdinx's shape, and §3.7 says where it departs.**

---

## §3 THE MECHANISM

### 3.1 One generative rule

> **THE HEAP RULE. The two halves of `x`_n_ are `x`_2n_ (low) and `x`_2n+1_ (high).
> `x8`–`x15` are the eight 64-bit tiles of the context, in order.**

That is the entire map. The rule applied once to `x8`–`x15` generates `x16`–`x31` as the
sixteen 32-bit tiles; applied again it would generate `x32`–`x63` as the thirty-two 16-bit
tiles, **and a five-bit field cannot reach them *in the `x` namespace*.** The recursion
stops at 31 there. It does **not** stop for the machine as a whole: §1.3(b) shows the
32 names of the `f` namespace could carry the whole 16-bit tier, and §1.3(c)/(d) give the
two non-arithmetic reasons that tier is declined. **The map has two complete widths
because the ruled subset has no arithmetic below 32 bits and because §3.2 spends the
second namespace on identity aliasing — not because five bits ran out.**

Read upward instead of downward and the rule also says what `x1`–`x7` are: `x1` is the
whole 512 bits, `x2`/`x3` its 256-bit halves, `x4`–`x7` its 128-bit quarters. **No operation
in `RV64IMAFD` is wider than 64 bits** (`Q` is outside the subset, cons-C10), so those seven
names denote nothing this machine can compute on and are **reserved — illegal instruction**.
They are not spare capacity being wasted; they are the part of the tree the ISA cannot use.

### 3.2 The table

| encoding | ABI name | width | bit range | note |
|---|---|---|---|---|
| `x0` / `f0` | `zero` | **any** | none | reads 0 at whatever width the instruction needs (`f0` reads **+0.0**); writes discarded at any width; costs none of the 512 (**M5**). `x0` is ratified Ch. 2. **`f0` is a DEVIATION** — ratified `F`/`D` has no hardwired-zero FP register and `f0` = `ft0` is an ordinary temporary (fact-C17: "`f0` **is** general"). See §8.4 deviation 2 |
| `x1`–`x7` / `f1`–`f7` | — | — | none | **reserved — illegal instruction.** Tree nodes wider than 64 bits (§3.1). See §10 q2 |
| `x8`–`x15` / `f8`–`f15` | `d0`–`d7` | **64** | `[64k, 64k+64)`, *k* = *n*−8 | the **D** tiling — **complete**, 512 of 512 |
| `x16`–`x31` / `f16`–`f31` | `w0`–`w15` | **32** | `[32m, 32m+32)`, *m* = *n*−16 | the **W** tiling — **complete**, 512 of 512 |

`d`/`w` are chosen to match RISC-V's own width letters — `ld`/`lw` — so `lw w3, 0(d1)`
reads as what it is. Composition, verified: `x8..x15` tile the 512 bits exactly; `x16..x31`
tile them exactly; `w`_2k_ ∪ `w`_2k+1_ = `d`_k_ for every *k*; every slice is naturally
aligned and **no slice crosses a 64-bit word boundary.**

**`f`_n_ ≡ `x`_n_ is CHOSEN, and it is the document's most consequential choice.**
**[FIX — review of 2026-09-03: the previous draft called this "forced rather than chosen"
and used a subset argument to do it, three sections after §1.3 had denied that a subset
argument was what was doing the work. facts §6.1 is explicit that this is an open fork:
"If the design wants 63 names it must keep F/D encodings and accept that it has forked the
ISA; if it wants Zfinx's clean semantics it has 31 names for 512 bits. **That choice has
not been made and it is the real fork in option 2.**" It is now §10 question 6.]**

*Given* the two tilings, an `f` name has nowhere new to point **at these two widths**: a
differently-aligned 64-bit range overlaps two `d` names, reintroduces straddling and
destroys the buddy property §3.8 depends on. But it could point at a **narrower** tile —
§1.3(b)'s complete 16-bit tier is exactly that, and it costs 32 `f` names and no `x`
names. What rules it out is ruling O4's subset (no arithmetic below 32 bits, §1.3(c)),
not the geometry.

So the identity aliasing is a **purchase**, and here is what it buys and what it costs:

- **Buys:** one allocation pool. K.6's "third wrong answer" — an allocator drawing
  `f`-names from a pool separate from `x`-names and admitting a function twice the legal
  size — becomes **structurally unrepresentable**. It also makes `fmv.d.x d1, d1` a no-op
  rather than a move, and it is **Zfinx's ratified rule verbatim** (Ch. 26 §26.1,
  fact-C9) for the case where the two namespaces coexist.
- **Costs:** 32 of the 63 names, i.e. every tier below 32 bits that §1.3(b) showed to be
  nameable; and — stated plainly because §8.4 must count it — **this machine is not
  implementing `F`/`D`.** facts §6.1: "In F/D, `f0`–`f31` is a separate architectural
  register file of 32 × FLEN bits — 256 bytes at FLEN=64, on top of the `x` registers …
  **An implementation that aliases `f<n>` onto `x<n>` is not implementing F/D.**" §3.7
  declines the *rename* to `RV64IMA_Zfinx_Zdinx` for a good reason, but declining the
  rename does not make the deviation go away. It is §8.4 deviation 1.

### 3.3 Why the index anchoring is `x8`/`x16` and not `x1`/`x9`

**[FIX — judges on `alias-tiled`: "THE INDEX ASSIGNMENT IS UNANALYSED AND FALSIFIES THE ONE
DAY-ONE CLAIM … no stock GCC or LLVM will allocate general values into `gp` or `tp`, `sp` is
ABI-fixed, and `ra` is clobbered by any `jal`."]** `alias-tiled` placed `d0`–`d7` at
`x1`–`x8` = `ra, sp, gp, tp, t0, t1, t2, s0` and then claimed `-ffixed-x9…x31` yields eight
usable 64-bit names. It yields at most four. `alias-hierarchical` anchored correctly and this
document adopts its anchoring; four things fall out and all four are wanted:

1. **Every D name is allocatable.** `x8`–`x15` = `s0, s1, a0`–`a5`. Nothing is ABI-fixed,
   nothing is clobbered by a jump.
2. **The RV64 argument registers `a0`–`a5` are 64-bit names**, which is the right default
   for pointers; `a6`/`a7` = `x16`/`x17` = `w0`/`w1` are 32-bit names, the right default for
   a seventh and eighth integer argument in a bit-packed world.
3. **Decode is two OR'd bits.** `n[4]` selects the width; legality is `n[4] | n[3]`
   (plus the zero-detect). §3.9 gives it. `alias-tiled`'s anchoring needed two magnitude
   comparators for the same job.
4. **`x1`–`x7` reserved makes `ra` and `sp` illegal names**, which is a real I7 tripwire —
   §3.6.

Also, at no cost: `x8`–`x15` is exactly the register set the compressed encodings can reach
(Zca: "CIW, CL, CS, CA, and CB … correspond to registers `x8` to `x15`", fact-C17). K.6
excludes RVC today, so this is a note for the record, not a claim.

### 3.4 Execution semantics — widths come from the operand's ROLE

**[FIX — judges on `alias-tiled`: the equal-width rule needed a carve-out for `addi rd, rs, 0`
that made "legality depend on an immediate's VALUE — `addi d1, w3, 0` legal, `addi d1, w3, 5`
illegal". `alias-hierarchical`'s port rules delete the carve-out entirely, and are taken here.]**

**[FIX — review of 2026-09-03, and this is the second-largest correction in the document.
The previous draft derived ONE execution width per instruction from the DESTINATION name
and applied it to EVERY source. That is wrong for every instruction whose operands do not
share one width, and the document's own examples are among them: `lw w3, 0(d1)` computed
its address at 32 bits and truncated the pointer; `sltiu w0, d1, 1` — the ubiquitous null
test — compared only the low 32 bits and called a non-null pointer null; and
`fcvt.d.s d1, w3`, the document's own recommended narrow→wide float conversion,
sign-extended an `f32` bit pattern to 64 bits before the FPU saw it. The rules below split
by **operand role**, which is the repair.]**

> **W1 — OPERAND WIDTH IS PER OPERAND, AND ITS SOURCE IS THE ROLE.** For each register
> operand of an instruction, the width at which it is read or written is fixed by the
> operand's **role**, in this order:
>
> | role | width | name required |
> |---|---|---|
> | **address / base** operand of any load, store, atomic, or `jalr` | **always 64** | a `d` name (§3.5 rule 1a) |
> | **data** operand of a load or store | the **opcode's** width (`lb`/`lh`/`lw`/`ld`, `flw`/`fld`, `lr.w`/`amo*.d`, …) | a name of that width, except that `lb`/`lh`/`lbu`/`lhu` may target any name ≥ 8/16 bits and extend into it |
> | any **floating-point** source or destination of an `F`/`D` instruction | the width the **mnemonic** names for that operand (`.s` = 32, `.d` = 64; `fcvt.*` names each operand separately) | a name of exactly that width (§3.5 rule 1b) |
> | the **integer** source or destination of an FP instruction (`fcvt.w.d`, `fcvt.l.s`, `fmv.x.w`, `fmv.d.x`, `feq`/`flt`/`fle`/`fclass` results) | the width the mnemonic names (`.w` = 32, `.l`/`.x.d` = 64); a compare/`fclass` **result** is 0/1 and fits any name | as the mnemonic names, except the 0/1 result |
> | **branch** sources (`beq`…`bgeu`) | **always 64** | any name |
> | **integer ALU / shift / M** operands and destination | the **integer execution width**, defined by W1b | any name |
>
> **W1b — INTEGER EXECUTION WIDTH.** For an integer arithmetic, logical, shift or `M`
> instruction the execution width is **32 if the opcode is a `*W` form** (`addw`, `sllw`,
> `mulw`, `divuw`, `addiw`, …), and otherwise the width of the **widest register operand,
> destination or source**. `x0` contributes nothing to that maximum; when every operand is
> `x0` the width is 64. `lui`/`auipc` execute at 64.
>
> **W2 — READ PORT.** Each source is read from **exactly its own name's bits**, then
> adjusted to its operand width from W1:
> - an **integer** source narrower than the operand width is **sign-extended** — §2.3's
>   ratified RV64 invariant, and the obligations it puts on the compiler are unchanged
>   from stock RV64 (see "Unsigned values" below);
> - an **integer** source wider than the operand width is **truncated** — which happens
>   only for a `*W` opcode, exactly as on a stock RV64 core;
> - a **floating-point** source is read at exactly its operand width with **no extension
>   and no NaN-box check**, because W1 requires the name to be exactly that width.
>
> **W3 — WRITE PORT.** A write puts the result into **exactly the destination name's
> bits**, and never modifies a bit outside them. Where the execution width is *narrower*
> than the destination name — which happens only for `*W` opcodes and for `lb`/`lh`/`lw`
> into a wider name — the result is **sign-extended (or zero-extended, for `lbu`/`lhu`/
> `lwu`) to fill the destination name**, which is ratified RV64 behaviour verbatim. It
> never NaN-boxes.

**Why sign-extend an integer source on read.** §2.3's RV64 invariant — all 32-bit values
held sign-extended in 64-bit registers — and bit-packing are not in opposition; they are in
different *places*. W2 maintains the invariant **at the read port instead of in the register
file**: the ALU sees exactly the bits a stock RV64 core would have presented to it, and the
storage costs 32 bits instead of 64. Every compiler assumption about sign-extended 32-bit
operands survives; only the register allocator changes.

**This is an invention, not a relocation, and the previous draft mis-cited it.**
**[FIX — review of 2026-09-03: the previous draft called W2 "Zfinx's narrow-value rule
(Ch. 26 §26.1, 'fill bits XLEN−1:*w* with copies of bit *w*−1') *relocated*". fact-C11
quotes both halves of §26.1 and they are not symmetric: the **read** rule is "Floating-point
operations on *w*-bit operands **ignore** operand bits XLEN-1:*w*", and the sentence quoted
is the separate rule for what a narrow FP **result** does to the upper bits on write. Zfinx
ignores upper source bits; it never manufactures them. W2 has ratified *precedent* in RV64's
own sign-extension invariant (Ch. 5, fact-C13) and none in Zfinx.]**

**Sign-extension is order-preserving for unsigned comparison**, which is why RV64 chose it:
`sext` maps [0, 2³¹) to itself and [2³¹, 2³²) to a strictly higher contiguous range, so
`bltu` on two sign-extended 32-bit names gives the right answer without a `zext.w`.

**Unsigned values, and when the back end must materialise a zero-extension.** The rule is
**identical to stock RV64 and is not new here**: a 32-bit *unsigned* quantity held
sign-extended is wrong for any 64-bit-wide unsigned operation, so it must either be
computed with the `*W` form (`divuw`, `remuw`, `mulw`, `srlw`) — which W1b forces to 32-bit
execution width and which is what stock codegen already emits for `unsigned int` — or be
explicitly zero-extended first. The zero-extension idiom is written on a `d`
destination, where 64-bit execution width makes `shamt = 32` legal:
`slli d1, w3, 32 ; srli d1, d1, 32` (or `zext.w` with Zbb). It cannot be written on a `w`
destination, because at 32-bit execution width `shamt[5] = 1` is reserved exactly as in
ratified RV32I — and it is never needed there, since a 32-bit value in a 32-bit name is
already canonical. §3.10 states the same rule from the back end's side.

**W3 is forced, not chosen.** x86 preserves the upper bits on a narrow write, AArch64 zeroes
them (fact-C19, fact-C20); **this map can do neither, because the bits above `w`_2k_ are
another architectural value, named `w`_2k+1_, not spare room.** W3 confines every ratified
extension mechanism inside the destination name:

| hazard | why it cannot reach a neighbour |
|---|---|
| `addw`'s sign-extension into 63:32 (Ch. 5, fact-C13) | the extension fills **the destination name** and stops. On a `d` destination that is ratified RV64 verbatim; on a `w` destination the extension field has zero width (§3.5) |
| `fmv.x.w`'s sign-extension into 63:32 (fact-C3) | same: it fills the destination name and stops. On a `w` destination the field has zero width |
| Zfinx §26.1's narrow-FP-result sign-extension | cannot arise: W1 makes every FP destination name exactly the operation's width, so there are no bits XLEN−1:*w* to fill. `fadd.s` on a `d` name is illegal (§3.5 rule 1b) |

**NaN-boxing is ABOLISHED on the tile, in both directions, and that is a deviation.**
**[FIX — review of 2026-09-03: the previous draft's hazard table said NaN-boxing "cannot
fire" because "an `f32` lives in a `w` tile, so FLEN = 32 for that instruction". That is
only true when the *destination* is a `w` name, and §3.5 rule 1b blesses `fcvt.d.s d1, w3`,
`fcvt.l.s d1, w3` and `feq.s w0, w3, w4` — 32-bit float sources under a 64-bit destination.
fact-C4's **read** half, which the previous draft never quoted, says: "all other
floating-point operations on narrower *n*-bit operations, *n*<FLEN, check if the input
operands are correctly NaN-boxed … otherwise the input value is treated as an *n*-bit
canonical NaN." Under the old W2 a valid positive `f32` in `w3` arrived with zeros in 63:32,
failed that check, and was read as canonical NaN.]**

The repair is W1's FP row plus W2's third bullet: **an FP operand's width comes from the
mnemonic, its name must be exactly that width, and it is read raw.** There is no wider
container, so there is nothing to box on write (fact-C11) and nothing to check on read.
Stated as the ratified deviation it is:

- **Write half (Ch. 21 §21.1.2):** not performed. `fmv.w.x w3, d1` writes 32 bits and
  fills nothing above them.
- **Read half (fact-C4):** not performed. A 32-bit FP source is used as-is; it is never
  reinterpreted as canonical NaN.
- **`flw` into a `w` name does not box**, and `flw` is kept (§3.7).
- **Consequence for the host:** the host *is* a stock RV64GC core with FLEN = 64 and it
  *does* box and check. §4.2's `fmv.w.x fa0, t0` on the host boxes correctly, as ratified.
  The tile and the host therefore have **different FP-operand semantics for `f32`**; this
  is §8.4 deviation 5 and §9.12 prices it beside the `rm = DYN` divergence.

**[FIX — judges: "§11.1's PROPOSED SUBSET RENAME TO `RV64IMA_Zfinx_Zdinx` IMPORTS A RATIFIED
RULE THAT DESTROYS DATA HERE" — Zfinx §26.1's narrow-result sign-extension would overwrite the
neighbouring tile.]** The hazard table's third row is the answer: under W1 that rule cannot
fire, because every FP destination name is exactly the operation's width. This is why **no
rename of the subset is needed and none is proposed** — see §3.7. It does *not* follow that
the design is `F`/`D`-conformant; §3.2 and §8.4 record that it is not.

### 3.5 Legality — what a decoder traps

**[FIX — review of 2026-09-03: the previous draft's rule 1 was an open-ended prose list
ending in an ellipsis, and it left the whole `fmv` family, `flw`/`fsw`, the 32-bit atomics,
`sc.d`'s `rd`, `fclass`'s integer destination, `fsgnj*`/`fmin`/`fmax`, and the `rs3` operand
of the fused multiply-adds unspecified — `fmv.d.x w0, d1` was legal under it, which is
exactly the silent 64→32 loss the rule exists to stop. It is replaced by the per-operand-class
table below, which is total over `RV64IMAFD`. The draft also could not agree how many rules
it had ("six" in §3.8, "five" in §5.4 and §8.4); **there are seven**, and every count in the
document now says seven.]**

A function core raises `illegal instruction` on each of:

**1a. An address/base operand that is not a `d` name.** Every base register of a load, a
store, an atomic, and `jalr` is a 64-bit address (W1) and must be a `d` name. `lw w3, 0(w5)`
is **illegal**; `lw w3, 0(d1)` is legal. *(This was in the Appendix's one-page summary and
missing from the normative list; it is normative here.)*

**1b. An operand-class disagreement.** The table is total over the subset; every column is
"the name must be of this width".

| instruction class | `rd` / data | `rs1` | `rs2` | `rs3` |
|---|---|---|---|---|
| `ld`/`sd`, `fld`/`fsd`, `lr.d`/`sc.d`/`amo*.d` | **`d`** | base: **`d`** | data (stores/AMO): **`d`**; `sc.d` also writes `rd` (see 1c) | — |
| `lw`/`lwu`/`sw`, `flw`/`fsw`, `lr.w`/`sc.w`/`amo*.w` | **`w`** for `flw`/`fsw`/`lr.w`/`amo*.w`; `lw`/`lwu` may target `d` or `w` (W3 extends into it); `sw` data: **`w`** | base: **`d`** | data (stores/AMO): **`w`** | — |
| `lb`/`lbu`/`lh`/`lhu`/`sb`/`sh` | load: `d` or `w` (W3 extends into it); store data: `d` or `w` (low bits used) | base: **`d`** | — | — |
| `fadd.d`, `fsub.d`, `fmul.d`, `fdiv.d`, `fsqrt.d`, `fsgnj[n,x].d`, `fmin.d`, `fmax.d` | **`d`** | **`d`** | **`d`** | — |
| `fadd.s` … `fmax.s` (the `.s` forms of the same) | **`w`** | **`w`** | **`w`** | — |
| `fmadd.d`/`fmsub.d`/`fnmadd.d`/`fnmsub.d` | **`d`** | **`d`** | **`d`** | **`d`** |
| `fmadd.s`/`fmsub.s`/`fnmadd.s`/`fnmsub.s` | **`w`** | **`w`** | **`w`** | **`w`** |
| `feq.d`/`flt.d`/`fle.d` | **any** (0/1) | **`d`** | **`d`** | — |
| `feq.s`/`flt.s`/`fle.s` | **any** (0/1) | **`w`** | **`w`** | — |
| `fclass.d` / `fclass.s` | **any** (a 10-bit mask; a `w` name holds it) | **`d`** / **`w`** | — | — |
| `fcvt.<a>.<b>` (every form) | the width `<a>` names: `.w`/`.wu`/`.s` → **`w`**; `.l`/`.lu`/`.d` → **`d`** | the width `<b>` names, same reading | — | — |
| `fmv.x.w` / `fmv.w.x` | **`w`** | **`w`** | — | — |
| `fmv.x.d` / `fmv.d.x` | **`d`** | **`d`** | — | — |
| `auipc` | **`d`** (a 64-bit PC-relative address, Ch. 5) | — | — | — |
| every other `I`/`M` opcode | **any** | **any** | **any** | — |

*Reading of the `fmv` rows, because the previous draft left it open:* `fmv` moves **raw
bits** and converts nothing (fact-C1), so both of its operands are the same width and both
names must be that width. `fmv.d.x w0, d1` is **illegal** — it would silently drop 32 bits.
Under `f`_n_ ≡ `x`_n_ these four instructions are architecturally no-ops on identical names
(`fmv.d.x d1, d1`) and genuine 64/32-bit moves on different ones; §3.7 keeps them because
they cost nothing, not because anything needs them.

**1c. `sc.d`/`sc.w`'s `rd`** is a 0/1 success flag and may be **any** width; the addressed
data operand follows row 1 or 2. `sc.d rd` is *not* a 64-bit datum and is not covered by the
`d`-name requirement.

**2. `x1`–`x7` or `f1`–`f7` as any operand** (§3.1). **This makes `ret` illegal** — see §3.6,
which states how a body actually terminates.

**3. `jal` or `jalr` with `rd` ≠ `x0`** — §3.6.

**4. A `*W` opcode with a destination wider than 64, or a shift with `shamt[5] = 1` at
32-bit execution width** — the latter is reserved exactly as in ratified RV32I (Ch. 2), and
it is why the `zext.w` idiom is written on a `d` destination (§3.4, §3.10).

**5. An RV64-only opcode at 32-bit integer execution width.** `lwu`, `fcvt.l.*`/`fcvt.lu.*`,
`fmv.x.d`/`fmv.d.x` and the 64-bit atomics have **no RV32 meaning**, so W1b's "32 if the
widest operand is 32" cannot be applied to them: their operand widths are fixed by rules
1a–1c above and a narrower name is illegal, not a narrower execution. *(Stated because the
previous draft defined 32-bit execution width only for the `*W` family and left these
undefined.)*

**6. Anything outside the subset** (cons-C10): CSR access, `FENCE`, RVC, `ecall`, anything
from `V`.

**7. A `w`-named operand where W1 requires 64 and no rule above has already caught it** —
the catch-all that makes the decoder total. It exists so that an unlisted encoding fails
closed rather than executing at a guessed width.

`lui` needs no rule: on a `d` destination it is ratified RV64I (the 32-bit U-immediate
sign-extended to 64); on a `w` destination the execution width is 32 and it is ratified RV32I
`lui` exactly. `x0` and `f0` are exempt from every width rule — they denote no bits, so they
have no width to disagree with. That exemption is load-bearing: without it `beqz` (`beq rs,
x0`), `li`, `mv`, `snez` and `j` (`jal x0`) would all be illegal, and this machine's loops are
built from them (**M5**).

**What is deliberately NOT illegal.**

- **Mixed-width INTEGER ALU operands.** `add d1, w3, w4` is legal and means: W1b takes the
  widest operand (64, the destination), W2 reads `w3` and `w4` sign-extended to 64, the ALU
  adds at 64, W3 writes `d1`. Symmetrically `add w0, d1, d2` executes at 64 — the widest
  operand is a source — and W3 keeps the low 32 bits, which is the truncation the programmer
  asked for by naming a 32-bit destination.

  **[FIX — review of 2026-09-03: the previous draft claimed of mixed widths "Under W1–W3
  there is no way for it to be unsafe, so there is nothing to trap." That was true only in
  the widening direction. Under the old destination-derived W1, `sltu w0, d1, d2` compared
  only the low 32 bits of two pointers and `sltiu w0, d1, 1` reported a non-null pointer as
  null — silently, on operand shapes stock codegen emits. W1b's "widest register operand"
  is the repair: a comparison of two `d` names executes at 64 whatever the destination's
  width, because the 0/1 result fits any name.]**

  The width-conversion idioms fall out with **no new instruction and no carve-out**
  (cons-C26 stands):

  | conversion | instruction(s) | why it is ratified behaviour |
  |---|---|---|
  | narrow → wide, signed | `mv d1, w3` (= `addi d1, w3, 0`) | W1b: widest operand is `d1`, so execution width 64; W2 sign-extends the source — `sext.w`'s semantics, obtained for free |
  | narrow → wide, unsigned | `slli d1, w3, 32` ; `srli d1, d1, 32` | exactly what RV64 without Zbb emits for `zext.w`. **Must be written on a `d` destination**: on a `w` destination the execution width is 32 and `shamt[5] = 1` is reserved (§3.5 rule 4). §3.10 restates the obligation from the back end's side |
  | wide → narrow | **nothing** — the low half of `d`_k_ **is** `w`_2k_ | free truncation by aliasing, the one thing x86 users expect from aliasing and the one thing a tiling gives for nothing |
  | narrow → wide, float | `fcvt.d.s d1, w3` | W1's FP row reads `w3` as a raw 32-bit `f32` (no sign-extension, no NaN-box check) and writes a 64-bit `f64` into `d1`. `fmv` never converted anything; `fcvt` is the correct instruction in stock RISC-V too |
  | 32-bit unsigned arithmetic | the `*W` form (`divuw`, `remuw`, `srlw`), or an explicit `zext.w` first | identical obligation to stock RV64 — §3.4, "Unsigned values" |

- **`*W` opcodes on a `w` destination.** `addw`, `subw`, `sllw`, `srlw`, `sraw`, `addiw`,
  `slliw`, `srliw`, `sraiw`, `mulw`, `divw`, `divuw`, `remw`, `remuw` are legal on **both**
  `d` and `w` destinations. On `d` they are ratified RV64I/M verbatim. On `w` the execution
  width is 32 and the sign-extension field has **zero width**, so `addw w0, w1, w2` computes
  exactly what `add w0, w1, w2` computes.

  **[FIX — judges on `alias-tiled` §3.3: "This inverts: `addw` is legal ONLY on a `d` name, so
  an `int` living in a `w` name must be computed with `add`, and stock codegen's `addw` for
  that int is precisely the illegal form … 'legal by construction' is the opposite of the
  truth."]** The repair is to read the `*W` suffix for what Ch. 5 says it is: not "a 32-bit
  operation" but "*a 32-bit operation whose result is canonicalised into a 64-bit register*".
  When the register is 32 bits the canonicalisation is the identity. Defining `*W` on a `w`
  name is therefore not new semantics — it is the ratified semantics with a zero-width
  extension field, and it is what makes §3.10's toolchain claim **true** rather than backwards.
  §10 question 3 offers the strict alternative and explains why it buys nothing.

### 3.6 `jal`/`jalr` and invariant 7

**[FIX — judges on `alias-hierarchical`: "§1.3(e) and §4.6 do not enforce C9/I7, and the
document claims twice that they do … `jal x10, off` followed by `jalr x0, x10, 0` is a legal
call/return pair."]**

Rule 3 of §3.5 is `rd` ≠ `x0` ⇒ illegal, which is a check on **the link register itself**, not
on the name `x1`. It makes every link-forming form of `jal`/`jalr` illegal while leaving `j`
(`jal x0, imm`), `jr` (`jalr x0, rs1, 0`) and every conditional branch legal — so switch
tables and computed jumps still work and **no ABI-conforming call can be encoded**.

Stated honestly, because the judges caught both siblings overclaiming here: **this catches
every call a compiler emits and every ABI-conforming call written by hand. It does not make a
call impossible** — `auipc d1, 0` / `addi d1, d1, 12` / `jr d2` forms a link by hand, and
banning `auipc` would cost every PC-relative constant. I7 remains a compiler-discipline
invariant with a decode check that catches the emitted form; it is not enforcement, and §7's
row for it says so.

The same honesty applies to spills. Reserving `x1`–`x7` makes `sp` (`x2`) an illegal name, so
`addi sp, sp, -16` and `sd ra, 8(sp)` are **both** decode-illegal — stronger than
`alias-hierarchical`, where only the store died. But a function holding a scratch pointer in
`d0` can still store through it. **"A function that spills cannot run" is enforced against the
ABI idiom and against nothing else** (cons-C9, I7).

**How a body TERMINATES, because rule 2 kills `ret`.**
**[FIX — review of 2026-09-03: `ret` is `jalr x0, 0(x1)`. Rule 3 passes (`rd` = `x0`) but
`rs1` = `x1`, which rule 2 makes illegal as any operand — so **the last instruction of every
function a stock toolchain compiles is decode-illegal**, and no `-ffixed` flag can suppress
an epilogue. The previous draft cited preserving `ret` in §1.3 as a reason `x0` could not be
a slice while its own rule 2 killed `ret` anyway, and never said what a body ends with.]**

> **A function-core body ends with `END`, or with `RETC` = `END` with `BIT_R` set** (user
> ruling 2026-09-02; **M8**). It never ends with `ret`, because there is no return address:
> there was no call, `x1` is not a name, and I2 returns the 512 bits rather than a value in
> a link register.

Two consequences that are build-system obligations, not architecture:

1. **A stock-toolchain body needs its trailing `ret` rewritten.** The day-one path (§3.10)
   compiles ordinary C and therefore emits an epilogue. `annotate`'s post-pass — which
   already walks the body and already refuses functions (§5.2) — must **replace the
   terminating `jalr x0, 0(x1)` with `END`/`RETC`**, and must **reject** any body whose
   `ret` is not the sole terminator (a tail call, a `jr` through a computed target, an
   epilogue that restores callee-saved registers first). This is a required step; without it
   the day-one path produces binaries that trap on their last instruction.
2. **The core does not treat a trailing `ret` as an implicit end-of-body.** Guessing would
   re-introduce exactly the silent behaviour cons-C15 forbids. It traps, and the trap is the
   signal that step 1 was not run.

**§10 question 7** asks whether the rewrite belongs in `annotate` (recommended, it is where
the body is already being walked) or in a linker/objcopy pass.

### 3.7 Floating point: what changes, and what does not

**[FIX — judges on both siblings: "MAKING `rm = DYN` ILLEGAL BREAKS EVERY STOCK-COMPILED
FLOATING-POINT INSTRUCTION … GCC and LLVM emit no rounding-mode suffix in ordinary codegen and
the assembler encodes `rm = 0b111` (DYN) by default."]**

- **`rm = DYN` (`0b111`) is DEFINED as RNE, not reserved — and it is not free.** There is no
  `fcsr` (cons-C8), so DYN has no referent; the only rounding modes reachable are the static
  ones encoded in the instruction, and RNE is RISC-V's default. Defining it makes stock FP
  codegen *decodable*. **This is a stated deviation from Ch. 20/21**, where DYN reads
  `fcsr.frm` and is an illegal instruction when `frm` holds a reserved value.

  **[FIX — review of 2026-09-03: the previous draft said this "costs nothing, breaks
  nothing." It costs the one thing canon protects. CANON I.7 item 3, quoted verbatim in
  `register-map-context.md` §2, says: "There is no `fcsr`, no rounding-mode state … A
  function needing dynamic rounding modes is in the same position as a function that spills:
  **it cannot be offloaded.**" cons-C16 makes "cannot be expressed ⇒ cannot be offloaded" the
  standard remedy and cons-C15 says rejection must not be softened.]**

  What defining DYN as RNE actually buys and costs:

  - **Buys:** every stock FP instruction is decodable, because GCC and LLVM emit no rounding
    suffix in ordinary codegen and the assembler encodes `0b111` by default. Rejecting DYN —
    which both sibling proposals did — makes *all* stock FP codegen illegal, which is a
    larger loss.
  - **Costs, and this is the thing to state rather than hide:** **the same instruction
    encoding now computes different results on the host and on the tile.** The host is a
    stock RV64GC core, has an `fcsr`, and honours `frm`; the tile silently rounds RNE. **No
    admission check can see the divergence**, because every stock FP instruction carries DYN
    and is therefore indistinguishable from one that genuinely wanted RNE. A function that
    called `fesetround()` and then offloaded gets a different answer on the tile, with no
    diagnostic.
  - **The remedy canon would prefer**, and it is available: `annotate` rejects any function
    whose translation unit is compiled with `-frounding-math` or which reaches
    `fesetround`/`fegetround`. That is a build-time gate, not a decode gate, and it is a
    weaker guarantee than I.7's. **§9.12 carries the divergence on the price list** rather
    than letting §3.7's "costs nothing, breaks nothing" stand.
- **FP exception flags are not accrued and are not observable.** cons-C8 forbids the state. A
  function that reads `fflags` needs a CSR access and is already inadmissible (§3.5 rule 6).
- **`fmv.d.x`, `fmv.x.d`, `fmv.w.x`, `fmv.x.w`, `flw`, `fsw`, `fld`, `fsd` are KEPT, and the
  ruled subset `RV64IMAFD` is NOT narrowed.** Both siblings proposed deleting these eight —
  as Zfinx and Zdinx do (fact-C15) — and then proposed amending ruling O4 to rename the subset.
  **Neither is necessary.** Under W1–W3 (with W1b) each of the eight is well defined and harmless:
  `fld f8, 0(d1)` is `ld x8, 0(d1)` spelled differently; `fmv.d.x f8, x9` is a 64-bit move;
  `fmv.x.w` and `fmv.w.x` extend only within their destination name (§3.4). Deleting them
  would recover eight encodings that nothing needs and would cost a user-ruling amendment.
  **Keeping them means ruling O4's opcode list stands untouched and §10 has one fewer
  question.** It does **not** mean the design is `F`/`D`-conformant: §3.2's `f`_n_ ≡ `x`_n_
  and §3.4's abolition of NaN-boxing are deviations from `F`/`D` whether or not the subset is
  renamed, and §8.4 lists all of them. What is avoided by declining the rename is importing
  Zfinx §26.1's narrow-result sign-extension, which would clobber a neighbouring tile.

### 3.8 Admission

> **[CORRECTED - user ruling 2026-09-03 (liveness)]** **A function is admissible iff (1)
> every opcode in it is in `RV64IMAFD` and its body satisfies §3.5's seven legality rules —
> no reserved name, no stack; (2) its peak simultaneous liveness, counting **each value at
> its own width**, **plus the scratch bits its packing needs**, is **≤ 512 bits**, with at
> least one spare name free for staging; and (3) a **verified non-overlapping placement
> exists** over its live ranges, where a value with no direct name of its width is placed
> **packed** inside a wider name.**
>
> ~~`64a + 32b ≤ 512` … where *b* counts values of 32 bits or fewer, **each charged 32**~~
> — **STRUCK.** Nothing is charged 32; that priced a value at the width of the name that
> would hold it, which is exactly the cap the ruling forbids.

**This IS K.6 in bits, and the intermediate draft's "704" counterexample is withdrawn.**
**[SUPERSEDED - user ruling 2026-09-03 (liveness). K.6 charges a value its actual width, and
so does this map: seven live 64-bit values plus eight live 8-bit values is 448 + 64 = **512
bits**, admitted at exactly 512 of 512, with the eight bytes packed into a `d` name's spare
capacity — except that at exactly 512 there is no spare bit for staging, so the honest form
of this case is that it admits only if the schedule leaves scratch room, which is a
*scratch-accounting* question and not a charge of 32 per byte. The draft that computed
"448 + 8×32 = 704 and REJECTS it" was applying the struck charge-32 rule. There is no
regression, cons-C16 is not engaged here, and cons-C15's fatality now bites only on genuine
overflow of 512 bits.]**

What is and is not inherited from K.6:

- **Inherited:** the test is on **bits**, in **one pool**, and it is peak simultaneous
  liveness, not a count of registers touched. cons-C18's R30 error is not re-introduced —
  at 64 and 32 bits the name count *is* the bit count, because both tilings are complete.
  cons-C19 survives: a value never read is never live and costs nothing. cons-C15 survives:
  rejection is fatal, no truncation, no softening.
- **Changed, and it is an instruction-count cost, not a regression** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`: a value
  narrower than 32 bits has no direct name, so it is **packed** inside one and reached by
  shift-and-mask through a scratch name — about **2-3 extra ops per access**, and its own
  width in bits. ~~Functions whose liveness K.6 admits in bits become inadmissible here~~ —
  **struck; none does.** What §10 question 2 (a narrow tier) and A2 (extent instructions)
  buy is **speed**, not capability. The one genuinely new accounting item is the **scratch
  bits** the packing needs, which admission must charge alongside the live bits.
- **Added, and it is a strengthening:** the bit sum alone is **not sufficient** — the
  counterexample below proves it — so admission also requires a *placement*. That makes the
  test "is there an assignment of live values to non-overlapping names", which
  `register-map-context.md` §7 correctly flags as a **different test** from a bit sum. It is
  the mirror of Part P R30 in shape but not in error: R30 counted *names touched* and so
  rejected functions that fit; this counts *bits* and adds a feasibility check that only ever
  rejects functions the geometry genuinely cannot place. cons-C18's justification — "at both
  widths the name count is the bit count" — covers the sum; **it does not cover the
  feasibility check, and the feasibility check is justified separately, by the counterexample
  below.**

The two measured functions (cons-C21):

| function | DESIGN §22 | decomposition | test | result |
|---|---|---|---|---|
| `nmfc_bu` | 8 values, **480 bits** | *a*=7, *b*=1 (7×64 + 1×32 is the unique multiset) | 448 + 32 = 480 ≤ 512 | **admissible**, one `w` tile spare |
| `nmfc_expand` | 8 values, **384 bits** | *a*=4, *b*=4 | 256 + 128 = 384 ≤ 512 | **admissible**, 128 bits spare |

**Placement, and the limit of the guarantee.**

> **Placement lemma (single program point).** Let values have widths *w*₁ ≥ … ≥ *w*ₙ, each in
> {64, 32}, with Σ*w*ᵢ ≤ 512. Place them in that order, each at the lowest free bit offset.
> Every value lands on a *w*ᵢ-aligned offset and no two overlap. *Proof:* before placing value
> *i* the occupied region is the prefix [0, Σ_{j<i} *w*ⱼ); every earlier width is a power of
> two ≥ *w*ᵢ, hence a multiple of it, so the next free offset is already *w*ᵢ-aligned. No gap
> is ever created. ∎

**[FIX — judges, both siblings, and this is the most important repair in the document: "THE
BUDDY THEOREM IS APPLIED TO THE WRONG PROBLEM … register allocation over live ranges is the
interleaved-free case … §7.3's bits test and 'is there an assignment to non-overlapping names'
are therefore different tests."]** The lemma is about **one instant**. Register allocation is
allocation over **time**, and the two are not the same test. Counterexample, verified: place
eight 32-bit values first-fit into `w0`–`w7`; let the ones at `w0`, `w2`, `w4`, `w6` survive
and the rest die; five 64-bit values are then born. Peak liveness is 4×32 + 5×64 = 448 ≤ 512,
so the bit test admits — but the four survivors block four distinct `d` tiles and only four
remain for five values. **The correct statements are:**

1. `64a + 32b ≤ 512` is a **necessary** condition, exact at any single program point.
2. Over live ranges it is **not sufficient**, and first-fit-decreasing at each birth is a
   heuristic, not an exact allocator. Mixed-width allocation with alignment over an interval
   graph is the classic register-pairing problem.
3. **Relocation repairs every gap the geometry can open, and costs one instruction per move
   with no scratch**: in the counterexample, `mv w1, w2` and `mv w5, w6` compact the survivors
   into `d0` and `d2` and free six `d` tiles for five values.
4. **Therefore `annotate` must produce and verify a PLACEMENT, not merely a sum** (§5.2). A
   permissive admission test is a defect, not a nuisance: under I7 there is no spill to fall
   back on, and cons-C15 makes rejection fatal.
5. The gap is narrow. A stress test over ~400,000 random interval instances that pass the
   closed form found a placement by exhaustive backtracking every time at widths {64, 32}
   (reported in the `alias-hierarchical` judging; **not independently reproduced here**, and
   cited as evidence of low risk, not as a proof).

**The one error the machine cannot catch, re-homed.** `d0` and `w0`/`w1` are the same bits. If
an allocator makes a value live in `d0` and another live in `w1`, they clobber, and no decode
check can see it — a fixed table makes every name always defined, so **M6**'s illegal-register
trap cannot fire. cons-C14 requires the check be re-homed rather than deleted: it moves onto
**admission, as a disjointness check on the verified placement**, and onto the seven legality
rules of §3.5, which trap at the tile. It is stated here as an obligation, not asserted as met.

### 3.9 Decode

Today (**M2**) the decode path is a memory reference —
`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCTile.cc:460-475`, `return c.regs.read( layout_.field[r] );`
— where `layout_` is the third referenced object. Under this map the same line is a wire
pattern on the instruction's own bits:

```
    width  = n[4] ? 32 : 64
    offset = n[4] ? ((n & 15) << 5)      // w_(n-16) = bits [32(n-16), +32)
                  : ((n &  7) << 6)      // d_(n-8)  = bits [64(n-8),  +64)
    legal  = n[4] | n[3]                 // n = 1..7 illegal;  n = 0 is the zero name
```

One 2:1 mux on the width, one 2:1 mux on a shifted field, one OR gate, one zero-detect.
**No memory is read, nothing is indexed by the function, and nothing depends on the context**
(cons-C1, cons-C2). As a ROM it is 31 × (9-bit offset + 1-bit class) = **310 bits, once per
tile**, shared by every context and every function on it — the same kind of object as the
opcode decoder's own truth table: combinational, fixed at tape-out, not addressed by the
program, not fetched, not per function, not per context. **The instruction and the data remain
the only two objects referenced.**

**Datapath.** Storage is unchanged (**M3**, `Context512`, 8 × 64). Because tiles are aligned
and ≤ 64 bits, the extract factorises into three selects: an 8:1 select of 64-bit words
(**3 mux levels**), a 2:1 intra-word select of the low or high half on `offset[5]`
(**1 level**), and W2's extension select on the upper 32 bits only — a 2:1 choice between the
word's upper half and 32 copies of bit 31 (**1 level**, and none on the lower half).
**Five mux levels on a 64-bit read path against three for a fixed 8 × 64 file: +2**, both
resolved from an offset known at decode, a stage before the read. The extension select is the
same structure a load unit's `lw`/`lb` sign-extension already is, moved onto the register read
port — which is exactly where §3.4 said the RV64 sign-extension invariant goes. A write uses **byte enables** (64 per
context line) and needs **no read-modify-write and no merge**, because every tile is a whole
number of bytes on a byte boundary and a neighbour's bytes are simply not written.

**No slice straddles a 64-bit word** (context-file §11 open question 3, answered: *no*). So
`Context512::read`/`write`'s straddle path — the `w0+1` branch — becomes dead code: correct,
unreachable, removable.

**Hazards and forwarding.** **[FIX — judges, both siblings: "'No new hazard class' is wrong …
dependence checking can no longer compare 5-bit register numbers for equality — it must compare
bit-range overlap."]** The judges are right about the general case and the canon answers it
outright: CANON.md:474-478, verbatim — *"Because at most one instruction per context is ever in
flight, no two instructions in the pipe can be dependent — so there is **no forwarding, no
interlocking, no hazard detection**, no ROB, no rename, no load/store queue, and no speculative
execution."* The barrel core issues one instruction per context and yields (H.2). **There is no
bypass network to make partial-width and no comparator to widen.** Where a non-barrel
implementation is ever built, the required change is small and should be stated so it is not
rediscovered: dependence becomes a byte-range overlap test, implemented as **3-bit lane equality
AND a 2-bit half-mask intersection** — five bits per operand — and the bypass needs byte-masked
merge. §5.3 handles the one remaining piece of per-context state that names touch, the scoreboard.

### 3.10 The compiler's half

DESIGN §23.6 and I.8 call the packing an unsolved compiler problem and say the architecture's
obligation is only to not prevent it. This map does better than not prevent it: **it turns the
packing into a register-class assignment.** Two allocatable classes — `D` (8 registers) and `W`
(16) — with `W` declared as sub-register indices of `D`. LLVM's `RegUnit` machinery models
overlapping register classes of exactly this shape; it is how x86's `RAX`/`EAX`/`AX`/`AL` are
allocated.

**[FIX — review of 2026-09-03: the previous draft added "and the RISC-V back end already
carries width-split GPR classes for `Zfinx`/`Zdinx`", hedged as an unverified upstream claim,
and then leaned the section's thesis on it. The claim does not describe this map's geometry
and must not be offered as reuse. Zfinx's `GPRF32`/`GPRF64` are **same-numbered** aliases —
one file modelled at two widths, a naming trick, not a sub-register relation. Zdinx's
`GPRPair` is a **super**-register over **adjacent even/odd** numbers. Here the whole (`x8`) is
numbered **below** its two parts (`x16`, `x17`) and the pairing stride is 2 across a 16-name
class. That is expressible in TableGen, but it is a **new register file, new `SubRegIndices`
and a new encoding map** — new back-end work, not a switch to flip.]**

So the honest form of the claim: *the allocator machinery exists and is well understood; the
description of this file does not, and writing it is the work.* What is genuinely reused is
LLVM's overlapping-class allocation, register-unit interference and sub-register liveness —
which is the hard part, and which is why this is still a much smaller job than "choose 32
(offset, width) pairs per function".

What the back end must actually do, stated precisely because both siblings got this wrong in
opposite directions:

- **Register-class assignment alone gets `int` arithmetic right**, because §3.5 keeps `*W`
  legal on `w` names: stock `addw`/`sllw`/`mulw` for an `int` in a `w` name compute the right
  answer with no instruction-selection change.
- **The canonicalisation peepholes must be suppressed.** `sext.w` (= `addiw rd, rs, 0`) on a
  `w` name is a redundant move; the `zext.w` idiom `slli`/`srli` by 32 is **illegal** on a `w`
  destination, because at 32-bit execution width `shamt[5] = 1` is reserved exactly as in
  ratified RV32I (Ch. 2, §3.5 rule 4). A 32-bit value in a 32-bit name is already canonical
  and needs neither. **The idiom is not banned outright** — written on a `d` destination
  (`slli d1, w3, 32 ; srli d1, d1, 32`) it is legal and is exactly how a `w` value is widened
  to an unsigned 64-bit one (§3.4, §3.5's conversion table). **The obligation on the back end
  is: materialise a zero-extension whenever a value in a `w` name feeds a 64-bit-execution-width
  *unsigned* operation, and write it into a `d` destination.** That obligation is identical to
  stock RV64's; only the "into a `d` destination" clause is new. This is a peephole change,
  not a dual-XLEN subtarget for instruction *selection* — but see §8.2, which prices the
  dual-width **datapath** it does require in hardware.
- **`long`/pointer values go in `D`, `int`/`unsigned`/`float` in `W`**, and a misassignment
  produces a truncation the machine cannot detect — see §3.8's re-homed check.

**The `-ffixed` day-one gate (M13, DESIGN §24 step 5) — what it really delivers.**
**[FIX — judges: "THE 'STOCK, UNMODIFIED TOOLCHAIN ON DAY ONE' FALLBACK DOES NOT WORK FOR
FLOATING POINT, AND ITS FAILURE MODE IS SILENT … `-ffixed-x9 … -ffixed-x31` does not reserve
the `f`-namespace, so a stock compiler will happily allocate `f1`-`f8` … and silently corrupt."]**

> **The day-one path is integer-only, and it must be spelled with `-march`, not with
> `-ffixed`.** Compile `-march=rv64ima` so that **no `f` register is ever allocated** — this
> is the only reliable way to close the silent-corruption hole, because `f`_n_ ≡ `x`_n_ and
> `-ffixed-x`_n_ does not reserve `f`_n_. Then reserve the non-`D` names:
> `-ffixed-x1 -ffixed-x5 -ffixed-x6 -ffixed-x7 -ffixed-x16 … -ffixed-x31`
> (`x2`/`x3`/`x4` = `sp`/`gp`/`tp` are already fixed by the ABI), plus `-fomit-frame-pointer`
> and `-fcall-used-x8 -fcall-used-x9` so that `s0`/`s1` are not callee-saved and no prologue
> store is emitted.
> **That yields eight 64-bit names = 512 bits, and a function that does not fit fails to build
> or spills visibly**, which is what DESIGN §24 step 5 asked for. **LLVM has no `-fcall-used`**;
> under Clang the honest floor is `-ffixed-x8 -ffixed-x9` as well, i.e. **six** names = 384
> bits — enough for `nmfc_expand` (384) and **not** for `nmfc_bu` (480).
> **Reaching the `W` tier, or using floating point at all, needs the register-class split
> above. No flag can retype a register.**

This is the 8 × 64 packing, i.e. `RegLayout::defaultLayout()` without the table — **a
fallback, not the design** (cons-C30). It is more than the record could previously promise and
less than the design wants.

**[FIX — review of 2026-09-03: the label is not enough on its own. A reviewer observed that
every path this document could actually deliver routed through 8 × 64 — the scoreboard
recommendation (§5.3), the tile build order (**M15**), and the compiler (this section) — so
"fallback" was doing no work. §5.3 and **M15** are corrected; this section keeps the 8 × 64
path because it is a real day-one capability, and it now carries the same warning they do:]**

> **The 8 × 64 path is a bootstrapping schedule, not a specification.** Anything that treats
> it as the target — a tile built with only a `D` tier, a scoreboard defined at lane
> granularity *because* there are eight lanes, an admission tool that charges 64 bits *because*
> the day-one compiler does — has reverted to the formulation I2 exists to forbid (#238:
> *"Once again, NO. 512 bits of context. The context is not 8 regs. Why do you keep reverting
> to that?"*). **The `W` tier is part of the design and part of the first build.** The only
> thing that is genuinely deferred is a *compiler* that can target it, and §10 question 8
> tracks that as a tooling item.

---

## §4 HOST-SIDE RETRIEVAL

### 4.1 The rule

**`V` is unavailable and must not appear in any retrieval path** (cons-C11, §2.1). The path
is: **`CXR` to a GPR, then RV64I shifts and masks, then `fmv.d.x`/`fmv.w.x` if the value is
wanted in an `f` register** — and on the host those `fmv` instructions are the real, ratified
ones (Ch. 20, Ch. 21), because **the host is a stock RV64GC core with a genuine 64-bit `f`
file**. Nothing in §3 touches the host; the map is internal to the function core.

This is the record's own position, not a new one: #231 — "we may need a subset of bit-manip
instructions added so that values can be retrieved/set" — and #233 — "**We need to make sure
EXTRACTION from the regs is possible. Regular bit manipulation can take you the rest of the
way. Alignment is something handled by the existing ISA. Let's not overdesign.**"

### 4.2 The aperture lines up with the map for free

`CXW`/`CXR` move one 64-bit lane, the lane number in `funct7` bits 3:1 (**M4**, cons-C28,
`NMFC_CX_LANE_MASK = 0x7`). Under this map:

> **lane *k* is exactly tile `d`_k_ = `x`_(8+k)_, and its halves are `w`_2k_ = `x`_(16+2k)_
> and `w`_2k+1_ = `x`_(16+2k+1)_.**

Neither had to move to make this so. The record's position that "the lane is an access
granularity, not the register's structure" is preserved — the structure now simply agrees with
it. Because **no tile straddles a lane** (§3.9), every named value is reachable in exactly one
`CXR`; the two-`CXR`-and-splice case an arbitrary bit-packed map required cannot arise.

| tile | lane | extraction on the host |
|---|---|---|
| `d`_k_ | *k* | `CXR t0, cS, k` |
| `w`_2k_ (low half) | *k* | `CXR t0, cS, k` ; `sext.w t0, t0` (signed) or `slli`/`srli` by 32 (unsigned) |
| `w`_2k+1_ (high half) | *k* | `CXR t0, cS, k` ; `srai t0, t0, 32` (signed) or `srli` (unsigned) |
| an `f64` in `d`_k_ | *k* | `CXR t0, cS, k` ; `fmv.d.x fa0, t0` |
| an `f32` in `w`_m_ | *m*>>1 | `CXR`, isolate the half as above, then `fmv.w.x fa0, t0` — the host has FLEN = 64, so this **NaN-boxes correctly**, as ratified (Ch. 21 §21.1.2) |

### 4.3 Staging arguments in, and the cost of doing it badly

**[FIX — judges on `alias-tiled`: "COSTING IS ASYMMETRIC WHERE IT MATTERS … seven host
instructions to stage one 32-bit argument … A function passing eight 32-bit arguments pays
roughly 30-40 host instructions of packing. That number should be in the cost section."]**

The cost depends entirely on the idiom, and the difference is ~2×, so both belong here.

**[FIX — review of 2026-09-03: the previous draft's "good idiom" was WRONG, and wrong in a
way this document had already disproved four sections earlier. It was
`slli t0, a1, 32 ; add t0, t0, a0` annotated "(a0 already zero-extended)". §2.3 quotes the
governing ratified invariant (Ch. 5, fact-C13): "**all 32-bit values are held in a
sign-extended format in 64-bit registers.** Even 32-bit unsigned integers extend bit 31 into
bits 63 through 32." A 32-bit argument in `a0` arrives **sign**-extended, so whenever its bit
31 is 1 the `add` propagates ones and a carry across bits 63:32 and destroys `w`_2k+1_. The
draft contradicted itself four lines later — its "bad idiom" block does `slli`/`srli` "#
zero-extend the argument", which is precisely the step the good idiom omitted.]**

**The good idiom — build the lane, then write it once.** Two 32-bit arguments per lane. Only
the **low** half needs zero-extending; the high half's upper bits are shifted out anyway:

```
    slli  t0, a0, 32
    srli  t0, t0, 32            # zero-extend the LOW argument  (Zbb: zext.w t0, a0)
    slli  t1, a1, 32            # high argument; its bits 63:32 are discarded by the shift
    or    t0, t0, t1            # { w_2k+1 : w_2k }
    CXW   c1, k, t0
```

**5 instructions per lane = 2.5 per 32-bit argument** on a stock RV64GC host (which has no
`Zbb`); **4 per lane = 2 per argument** if the host has `Zbb` and can use `zext.w`. Eight
32-bit arguments cost **20 instructions and 4 `CXW`s** (16 with `Zbb`), against **8 `CXW`s**
for eight 64-bit arguments.

**So packing IS a tax on the host, and the corrected numbers say so.** Per *bit* moved: eight
64-bit arguments move 512 bits in 8 instructions (64 bits/instruction); eight 32-bit arguments
move 256 bits in 20 (12.8 bits/instruction). The previous draft's conclusion — "packing is
*cheaper* than not packing, per bit moved" — was an artefact of the missing zero-extension and
is **withdrawn**. What survives is the comparison that actually matters: **20 host instructions
against the alternative of not being able to offload the function at all**, since without
packing a function needing nine live values does not fit in 512 bits. The tax is real, it is
paid once per offload in the caller's frame, and it is small beside a `FORK` (§8.3).

**The bad idiom — read-modify-write a lane whose other half is already live:**

```
    CXR   t1, c1, k             # read the lane back
    srli  t1, t1, 32
    slli  t1, t1, 32            # keep w_2k+1, clear w_2k
    slli  t0, a1, 32
    srli  t0, t0, 32            # zero-extend the argument
    or    t0, t0, t1
    CXW   c1, k, t0
```

**7 instructions for one argument.** A caller that writes lanes in order never pays this; a
caller that patches one half of an already-staged lane pays it every time. **State the rule in
the ABI: stage a lane's two halves together.**

Worked example, the whole sequence for a pointer and a 32-bit `NodeID` under the stock
argument convention (§4.4), with the result read back as an `f32`:

```
    CXW     c1, 2, a0           # a0 -> d2 = lane 2   (64-bit pointer)
    slli    t0, a1, 32
    srli    t0, t0, 32          # zero-extend the NodeID -- a1 arrives SIGN-extended (Ch. 5)
    slli    t1, a2, 32
    or      t0, t0, t1          # { arg3 : NodeID }
    CXW     c1, 3, t0           # -> d3 = lane 3, halves w6 and w7
    FORK.R  ...
    ...
    CXR     t0, c1, 1           # lane 1 = { w3 : w2 }
    slli    t0, t0, 32
    srli    t0, t0, 32          # isolate w2
    fmv.w.x fa0, t0             # host FLEN = 64: NaN-boxes, as ratified
```

The three extra integer instructions are the price the record already accepted when it said
"regular bit manipulation can take you the rest of the way" (#233) and refused a bit-field
insert/extract instruction (cons-C26). It is host code, in the caller's frame, with a stack and
31 real registers — not the constrained side.

### 4.4 The calling convention, and why I2 still holds

**[FIX — judges, both siblings: "THE FORK/JOIN CALLING CONVENTION IS CLAIMED FIXED BUT NEVER
SPECIFIED … the proposal introduces widths that the host must now agree about, and it must say
how."]**

> **The ISA fixes the GEOMETRY — which bits a name denotes. It does not fix the ASSIGNMENT —
> which value a function chose to put in which name.** That remains the function's own ABI,
> known to its caller, exactly as before. **I2 is preserved literally: register positions still
> carry no architectural meaning across the boundary**, and the 512 bits still come back whole
> and uninterpreted (**M8**).

What genuinely changes: the caller and the callee must now agree on **width** as well as
position, because a value's width is now visible in the name. So a convention must be
*published with the function*, and the natural one is the RV64 ABI read through §3.2's table:

| role | RV64 ABI register | this map | lane |
|---|---|---|---|
| integer/pointer arguments 1–6 | `a0`–`a5` = `x10`–`x15` | `d2`–`d7` (64-bit) | 2–7 |
| integer arguments 7–8 | `a6`, `a7` = `x16`, `x17` | `w0`, `w1` (32-bit) | **0 — the two halves of `d0`** |
| scratch (nothing is callee-saved; there is no stack) | `s1` = `x9` | `d1` | 1 |
| ~~scratch~~ `s0` = `x8` = `d0` | — | **NOT free when arguments 7–8 are used** | 0 |

**[FIX — review of 2026-09-03: the previous draft's table listed `s0`/`s1` = `d0`/`d1` as
scratch **in the row below** the one that put arguments 7–8 into `w0`/`w1`. `w0 ∪ w1` **is**
`d0` **is** `s0` — the same 64 bits. The convention collided with itself, and its worked
arithmetic collided too: it said "a function taking eight integer arguments is at 6×64 + 2×32
= 448 bits with `d1` (bits 64–127) free", when `d0` is the occupied one and `d1` is free
*because* `a6`/`a7` landed in `d0`, not because `d0` is scratch. This is §9.5's
undetectable-overlap error class, and the previous draft baked it into the convention it asked
the user to ratify.]**

Corrected, stated as a rule rather than a table row:

> **Under the stock RV64 ABI mapping, lane 0 (`d0` = `w0` ∪ `w1`) is scratch ONLY for a
> function taking six or fewer integer arguments.** A function taking a seventh occupies
> `w0`; an eighth occupies `w1`; with both, `d0` is fully occupied and the only free 64-bit
> tile is `d1`. An allocator that treats `s0` as scratch in that case silently clobbers two
> arguments, and no decode check can see it (§9.5).

Two further consequences worth stating plainly:

- **Under the stock convention the host stages argument *k* into lane *k*+2**, not lane *k*.
  That costs nothing — `CXW` takes an explicit lane number — but it is the sort of thing that
  is discovered painfully if it is not written down. A custom back end may renumber so that
  argument 0 lands in `d0` = lane 0, which also dissolves the collision above; **§10 question
  4** puts that choice, and the collision is now the strongest argument for renumbering.
- **A function taking eight integer arguments is at 6×64 + 2×32 = 448 bits**, occupying
  `d2`–`d7` and `d0`'s two halves, with **`d1` (bits 64–127) free** — admissible, with one
  64-bit tile of working space.

**Arguments that do not arrive in registers.**
**[FIX — review of 2026-09-03: the previous draft wrote "a ninth argument arrives on the stack
and is therefore inadmissible, unchanged (I7, cons-C9)". Canon I7 carries a `[CORRECTED]`
block on exactly that sentence: "An argument that arrives **on the stack** is inadmissible —
**which is a rule about the stack, not a count of nine.** [CORRECTED — this clause read 'A
ninth argument is inadmissible.' … That is a count of 64-**bit registers**, exactly the reading
I2 records as superseded (#238) … Under bit-packing, **nine 32-bit arguments are 288 bits and
are admissible**; the old clause ruled them out." The previous draft reproduced the exact
regression canon had already corrected, and it is false on this document's own map — nine
32-bit arguments fit in `w0`–`w8` with seven `w` names to spare.]**

The correct statement: **an argument that arrives on the stack is inadmissible, because there
is no stack (I7, cons-C9) — and that is a rule about the stack, not a count.** Whether a ninth
argument arrives on the stack is a property of the *convention*, not of this map. Under the
stock RV64 ABI it does, so a stock-toolchain caller cannot pass nine. Under a custom NMFC
convention that packs arguments into `w` names, **nine 32-bit arguments are 288 bits and are
admissible**; sixteen are 512 and are the ceiling. §10 question 4 is where that convention is
chosen.

### 4.5 The RETURN-VALUE convention

**[FIX — review of 2026-09-03: the previous draft had no return-value convention anywhere.
§4.4's table gave arguments and scratch and stopped; §4.3's worked example simply read lane 1
back with no stated rule; and §10's questions did not ask for one. Both the host caller
emitting `CXR` and the function-core back end deciding where to leave a result are blocked on
it — a strictly larger gap than the argument renumbering the draft *did* escalate.]**

I2 is what makes this a *convention* rather than an architectural rule: **all 512 bits come
back, whole and uninterpreted**, so the architecture never needs to know where the result is.
The caller does. The recommendation, chosen to mirror the argument convention exactly:

| result | this map | lane | host reads it with |
|---|---|---|---|
| one 64-bit integer or pointer, or one `f64` | `d2` (= `a0` under the stock ABI) | 2 | `CXR t0, cS, 2` |
| one 32-bit integer or `f32` | `w4` (the low half of `d2`) | 2 | `CXR` + `sext.w` / `slli`+`srli` |
| two results (the RV64 ABI's `a0`/`a1` pair) | `d2`, `d3` | 2, 3 | two `CXR`s |
| a result too wide for one name | not expressible — §9.7 | — | — |

Rationale, in one line each: it is the stock RV64 ABI's own answer (`a0` is both argument 1 and
result 1) read through §3.2, so a stock-toolchain body needs no change; the result lands in a
name the callee could not have been passed a *seventh or eighth* argument in, so it never
collides with §4.4's `d0` case; and reading it costs one `CXR`.

**This needs a ruling, not a recommendation — §10 question 7**, together with the argument
renumbering of question 4, because the two must be decided at the same time or the host and
the back end will be written against different conventions.

### 4.6 Where the map lives on the host

**Nowhere referenced.** The offsets appear in the caller's code as immediates in `slli`/`srli`,
exactly as a struct field offset does. There is no table, no descriptor, and nothing fetched —
the same answer §3.9 gives on the tile, arrived at from the other end.

---

## §5 INTERACTION WITH EXISTING MECHANISMS

The context file's fit-list, row by row: *"a proposal is incomplete until it says what happens
to each."* Three rows get their own subsection first, because they are where the judges found
the real holes.

### 5.1 The resident-function table (M1) — deleted, and the deletion must be marked

`RegLayout` is **deleted**, not kept as an alternative and not kept as an optimisation. Three
consequences that are not optional:

1. **DESIGN.md §25.7, D:2560-2567 is overruled and must be MARKED superseded, not quietly
   dropped** (cons-C31). Its "one small table entry beside the instruction cache … It adds
   nothing to the 512 bits and does not scale with `C`" is exactly the third referenced object
   the 2026-09-03 ruling names. The ruling is newer and higher-tier.
2. **CANON.md:9819's clause is superseded, and it is TIER 1.** Verbatim: *"**The namespaces do
   not alias**: the core implements 512 bits of live storage rather than 64 architectural
   slots, and the compiler binds every simultaneously-live `f`- or `x`-name to a **disjoint bit
   range**, so `f3` and `x3` are different names at different offsets, not one slot."* §3.2
   contradicts the second clause. The first — *512 bits of live storage, not 64 slots* — is
   preserved and strengthened. This is a supersession **inside tier 1**, it needs a user ruling
   rather than a document, and §10 question 5 asks for it.
3. **The tier-4 artefacts become divergences** in Appendix 2's sense the moment a replacement
   is adopted, and are then rewritten: `/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCRegLayout.h`,
   `/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCTile.h:448-450`,
   `/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCTile.cc:460-475`. **[FIX — `alias-tiled` §12.1 gave these
   as `src/nmfc/src/...` relative to the ChampSim tree, where they do not exist; that directory
   holds `function_core.cc`, `nmfc_mmu.cc` and siblings. The files are in the Rev tree, at the
   absolute paths above.]** Tier 4 decides nothing (cons-C29); these are edits, not evidence.
4. **`RegLayout`'s one load-bearing behaviour is PARTLY inherited, and the part that is lost
   must be named.** `RegLayout::illegal()` was a **run-time** trap: a function reaching a
   register the mapping did not define found out immediately (cons-C14). Under a total map
   nothing can fire. What is inherited is §3.5 rule 2 (`x1`–`x7` illegal) — which catches a
   *name* outside the map, at run time. What is **not** inherited is the guarantee against
   **over-liveness**, which now rests entirely on `annotate`'s build-time
   placement-disjointness check (§3.8, §5.2). Its run-time failure mode is a silent wrong
   result. §9.5 and **M6** say so; the constraint row scores it **not met as a machine
   guarantee**.

### 5.2 The admission tool (M7) — what `annotate` must compute

`/mnt/md0/ChampSim/ChampSimArchWork/nmfc/tools/nmfc/annotate.cc:524-559` today builds a pool of
`opt.num_regs` (8) **slot ids**, allocates one whole slot per live value, and calls `die(...)`
when the pool empties. It also computes `bits += reg_bits[reg] != 0 ? reg_bits[reg] : 64U;` at
`:555-559` — and throws that quantity away on a stderr line at `:927`. That is ledger **L30**;
it gates nothing today, and K.6 already requires the rewrite.

Under this map the rewrite is:

1. **Classify each live value's width** into {64, 32}. **The width source named by the
   previous draft does not exist on the ruled target.**
   **[FIX — review of 2026-09-03: the previous draft said to take the widths "the tracer
   already carries (DESIGN §22)". That quantity is
   `/mnt/md0/ChampSim/ChampSimArchWork/nmfc/tools/nmfc/annotate.cc:461-470`'s `width_of`
   lambda, and it parses **x86-64 register names** — `rax`/`eax`/`al`, `r8d`/`r8w`/`r8b`,
   `zmm`/`ymm`/`xmm`. The target was ruled RISC-V (CANON.md R11), and §2.3 of this document
   proves at fact-C12 that **RV64 register names carry no width at all**. On a RISC-V trace
   every value falls to `width_of`'s `else` branch and is charged 64 bits, `annotate.cc:485`'s
   `std::max` over aliased views is a maximum over one value, and neither measured
   decomposition in the table above is reproducible from the tool as it stands. **The
   admission gate has no working input on the ruled target.**]**

   The width must instead come from one of three places, and the choice is a real one:

   | source | what it gives | cost |
   |---|---|---|
   | **the opcode that defines the value** — `lw`/`ld`, `addw`/`add`, `flw`/`fld`, `fadd.s`/`fadd.d` | exactly the RV64 signal that replaces the x86 name suffix, and it is available in any RISC-V trace | a per-opcode width table in `annotate`, ~150 entries; a value defined by `ld` and only ever used 32 bits wide is over-charged |
   | **front-end type information** (DWARF, or an LLVM IR pass) | the declared width, which is the tightest | requires the build to carry debug info or a compiler pass; not available for hand-written asm |
   | **the register class the back end assigned** (`D` vs `W`, §3.10) | exact by construction, and it is the same fact the placement needs | only exists once the register-class split of §3.10 lands; unavailable on the day-one path |

   **Recommendation: the opcode table now, the register class once §3.10's back end exists.**
   Until one of them is implemented, `annotate` must **charge every value 64 bits and say so**
   — a conservative gate that rejects some admissible functions is sound; the current silent
   fall-through to 64 with an x86 parser in front of it is not, because it looks like a
   measurement. **§10 question 8** records this as the tooling blocker it is.
2. **Compute peak simultaneous liveness in bits — each value at its own width — add the
   scratch bits the packing needs, and test `≤ 512`** `[CORRECTED - user ruling 2026-09-03
   (liveness)]`. The `bits` figure at `:555-559` stops being a stderr line and becomes the
   gate. It is never `64a + 32b`, and nothing is rounded up to a nameable width.
3. **Produce a placement** — first-fit-decreasing by width at each birth, with relocation moves
   permitted where a buddy pair must be recovered (§3.8 point 3).
4. **Verify the placement**: every pair of simultaneously-live values occupies **disjoint bit
   ranges**, and every value occupies one name for its whole live range or is explicitly moved.
   **[FIX — this step is the one both siblings omitted, and it is what makes the tool sound
   rather than permissive; see §3.8. A permissive admission test is a defect, because I7 leaves
   no spill and cons-C15 makes rejection fatal.]**
5. **Report the placement**, so that cons-C20 is checkable: admissibility is a property of the
   generated code, and the placement is the evidence.

`die()` stays, with a message naming the bit total and the failing placement rather than the
slot count. The existing linear-scan structure survives; this is a smaller change than the one
K.6 already demands.

### 5.3 The scoreboard — the one piece of per-context state a register name touches

**[FIX — judges on `alias-tiled`, the single largest hole they found: "THE SCOREBOARD IS MISSING
FROM A FIT-LIST THAT CLAIMS TO BE COMPLETE … under aliasing a 'per-register ready bit' is
ill-defined: a pending load into `w6` must make `d3` not-ready … This also puts an asterisk on
'migration 72 B exactly'."]** The mechanism is real: `DESIGN.md:775` specifies
`scoreboard[≤8] // per-register ready bit`, `:790` makes it the source of intra-function MLP,
and `:798` puts it in the migration payload `(token, pc, regfile, scoreboard, origin,
home_host)`. The canon answers it, and the answer is that **on the canon core the structure
does not exist in the form the objection assumes.**

**H.4 is canon, and it is decisive.** User #239, quoted at CANON.md:5299-5320: *"If we have
multiple pending loads per context, we need to both identify which is which and ensure the
regfile is coherent. We don't have a staging area for all these returning loads … **I would opt
for 1 outstanding miss.** Likely we need a D-buffer, but **it just stores one slot, the data
that will be used when it wakes.**"* CANON.md adds: *"With one outstanding load there is nothing
to disambiguate and nothing to keep coherent"*, and *"the context is always asleep when its load
returns"*.

> **On the canon core the scoreboard is not a per-register bit vector. It is ONE pending
> destination — a 5-bit name plus a namespace bit — and a one-slot data buffer.** The context is
> `BLOCKED` while the load is in flight and issues nothing, so there is no readiness to test on
> any other name, no overlap to reduce over, and no growth. When the fill lands, the pending
> name is resolved through §3.9's same fixed decode and written at that name's width.
> **Per-context state added: zero. Migration payload: unchanged.**

**Where the multi-bit scoreboard does exist**, it is the *other* core model — H.4 is explicit
that DESIGN §7's scoreboard belongs to the ChampSim core "whose contexts kept issuing past an
outstanding load and got intra-function MLP from an in-order scoreboard", and that the two core
models have different MLP at the same context count. For that model the correct structure under
this map is:

**[FIX — review of 2026-09-03: the previous draft recommended lane granularity and gave as one
of its three reasons that it "preserves `DESIGN.md:775`'s `scoreboard[≤8]` **verbatim**" — i.e.
it used the 8-register artefact as a *design constraint*, on the one structure in the machine
where a register name is visible. That is the reversion I2 forbids (#238), inside a document
that quotes #238 two sections later. The two options are re-stated below without that reason,
and the recommendation now turns on the false-dependency cost alone.]**

> **The correct structure is 16 ready bits at `W` granularity**, one per 32-bit tile, with a
> read of a `d` name taking the OR of its two. A pending load resolves its destination through
> §3.9's decode and marks exactly the tiles it will write. This is **exact**: no name stalls on
> a load it does not depend on. It costs **+8 bits per context** against the eight-bit form
> (+1 KiB per tile at C = 1024, against 64 KiB of context state — 1.5 %) and **+1 byte** in the
> migration message, which would make it 73 B and would need cons-C6 re-ratified.
>
> **The conservative alternative is eight ready bits, one per 64-bit lane**: a pending load
> into any tile marks its containing lane not-ready and a read of any name in that lane stalls.
> It adds **zero** state and **zero** migration bytes. Its cost is a false dependency: a load
> into `w0` needlessly stalls a read of `w1`, i.e. **half the file's names can be spuriously
> blocked**, and on a function that keeps two independent 32-bit values in one lane — which is
> what this whole design exists to make possible — the stall is exactly the case bit-packing
> was supposed to win.
>
> **Recommendation: `W` granularity (16 bits), and pay the byte** on the ChampSim core model,
> *unless* the user prefers to hold cons-C6's 72 B fixed, in which case lane granularity is the
> fallback and its false-dependency cost is the price. **This is §10 question 9**, because it
> is the one place in the document where a recommendation would change a ratified constant.
> **On the canon core the question does not arise at all** — one pending destination, above —
> and that is the configuration I11's 72 B was measured on.

**Consequence for I11, stated rather than assumed.** I11's 72 bytes are *"64 bytes of register
file plus an 8-byte program counter"* (DESIGN §25.7 D:2574-2577). The `token`, `origin`,
`home_host` and `scoreboard` of DESIGN §7.1's payload are message envelope, not the 72 bytes.
**On the canon core — one pending destination — nothing the scheme adds travels and cons-C6
holds exactly.** On the ChampSim core model, `W` granularity adds one byte to the *envelope*
and none to the 72; whether the envelope is inside cons-C6's 72 B is the question §10 q9 also
settles.

### 5.4 The rest of the fit-list

| # | mechanism | what happens to it |
|---|---|---|
| **M2** | `readReg`/`writeReg` decode indirection | **Replaced by §3.9's wire pattern.** `layout_.field[r]` becomes one mux on `n[4]` and an OR gate. A wiring change, not a lookup |
| **M3** | `Context512::read`/`write` | **Reusable unchanged**, and simplified: no tile straddles a 64-bit word, so the `w0+1` branch is dead code (§3.9). The write path moves to byte enables |
| **M4** | `CXW`/`CXR`, 64-bit lane in `funct7[3:1]` | **Unchanged, and now aligned:** lane *k* ≡ `d`_k_ (§4.2). Granularity and structure agree without either moving. Every named value is one `CXR`. `NMFC_CX_LANE_SHIFT`/`MASK` keep their values (cons-C24: field values are implementation choices) |
| **M5** | the `x0` rule | **Preserved exactly**, and load-bearing rather than vestigial. `x0` reads zero and discards writes at any width, costing none of the 512; it is exempt from every width rule, or `beqz` and `j` (`jal x0`) would be illegal. `f0` ≡ `x0` reads **+0.0** and discards writes — **a DEVIATION, not an inherited rule.** **[FIX: the previous draft cited "Zdinx's ratified rule (fact-C16)". fact-C16 is about `x0` under Zdinx — a machine with **no `f` namespace at all** — so it ratifies nothing about `f0`. The ratified fact about `f0` is the opposite one, fact-C17: "`f0` **is** general (the FP file has no hardwired-zero register)." `f0` = `ft0` is the first FP temporary stock codegen allocates.]** Making `f0` a hardwired zero, together with rule 2's `f1`–`f7`, removes eight general FP registers and means stock FP code touching `ft0`–`ft7` either has its results discarded or traps. It is a legitimate design choice and it is **§8.4 deviation 2**; §9.9's day-one FP hole has this as its second cause |
| **M6** | the illegal-register trap | **Re-homed to BUILD TIME, and DOWNGRADED — the run-time guarantee is gone.** Every name is now always defined, so "undefined register" cannot fire at decode. §3.5's **seven** legality rules trap **at the tile**, but they catch operand-width disagreements, not **over-liveness**: the placement-disjointness check of §3.8/§5.2 is a **tool obligation** that rejects at build time and has no run-time counterpart. **[FIX — review of 2026-09-03: the previous draft scored this "met, re-homed". `RegLayout::illegal()` fired at run time when a function reached a register the mapping did not define — "invariant 7 enforced by the machine: a function that needs more than the file holds finds out immediately." Nothing at run time enforces it now. A function that over-lives produces a **silent wrong result**, which is the failure mode cons-C14 and cons-C15 jointly exist to forbid. §9.5 said this honestly; the row did not, and now does.]** |
| **M8** | the `END` return bit (`RETC`) | **I2 preserved literally.** `RETC` is `END` with `BIT_R` set — user ruling 2026-09-02, *"RETC and ENDC are the same instruction, with a return bit"* — and it returns the 512 bits whole and uninterpreted. The ISA now fixes the *geometry*, never the *assignment*; positions still carry no architectural meaning across the boundary. What the caller must additionally know is **width**, and §4.4/§4.5 say where that convention lives. **New obligation:** `ret` (= `jalr x0, 0(x1)`) is decode-illegal under §3.5 rule 2, so `annotate` must rewrite a stock body's terminating `ret` into `END`/`RETC` and reject any body whose `ret` is not the sole terminator (§3.6, §10 q7). The core does not treat a trailing `ret` as an implicit end-of-body |
| **M9** | `JOIN` as a read-modify-write try | **Unaffected**, confirmed: `cDST_new = ok ? ftu_payload : cDST_old` moves 512 bits and never inspects them |
| **M10** | `CONT` / `CONT.M` | **Simplified.** A successor inherits the same map because there is only one map. Under **M1** a successor running a different function needed a different table entry and a way to know which; that problem disappears rather than being solved |
| **M11** | 72-byte migration | **Exactly 72 B, unchanged** — 64 B of context + 8 B of PC (cons-C6). The scheme adds zero bits to anything that travels; §5.3 shows the scoreboard does not grow either |
| **M12** | the two hosts and RoCC's 128-bit path | **Unaffected.** Every operand remains a value in a GPR; a context register is still named by a number in a GPR (cons-C25). The map is internal to the function core and no host instruction names a tile |
| **M13** | `-ffixed-x{n}` as the admission gate | **Half-solved, and the half is stated precisely** (§3.10): `-march=rv64ima` plus `-ffixed` plus `-fcall-used-x8/x9` gives a correct eight-name 512-bit **integer-only** machine on day one under GCC (six names, 384 bits, under Clang). The `W` tier and floating point need a register-class split. **No flag can retype a register, and `-ffixed-x`_n_ does not reserve `f`_n_** (the second cause of the FP hole is §8.4 deviation 2: `f0`–`f7` are the stock FP temporaries and are a zero plus seven illegals). And no flag suppresses an epilogue: the trailing `ret` must be rewritten (**M8**, §3.6) |
| **M14** | encoding space | **Untouched.** No new instruction, no `funct7` group consumed; `0x6`/`0x7` remain for `KILL`, mailboxes and `RESUME` (cons-C23). cons-C22 holds: twelve user-level instructions plus privileged `RESUME`. **And no instruction is removed either** (§3.7), so ruling O4's **opcode list** stands unamended. Its **semantics** do not: §8.4 lists ten deviations from the ratified manual, of which `f`_n_ ≡ `x`_n_ (not implementing `F`/`D`) is the largest |
| **M15** | Appendix 2 `S5` | **Changes rather than closes.** `S5` records that the bit-level admission test is never exercised "because nothing produces a layout other than the default". There are now no layouts to produce, and **the tile has 32-bit names in hardware from day one**, so the bits-used figure stops being constant the moment a function uses a `w` name. Restate `S5` as: *the `W` tier is implemented in the tile; what is missing is a compiler that targets it, so the divergence is that no test exercises a `w` name.* **[FIX — review of 2026-09-03: the previous draft restated `S5` as "*the tile implements the D tier only; the W tier is unbuilt*", which specifies the hardware to be built as **eight 64-bit registers** — the formulation I2 forbids (#238), reached by the same route §5.3 and §3.10 were reaching it. The `W` tier is not a later phase; it is 24 gates (§3.9) and it is the design. What is genuinely deferred is the *toolchain*, and that is §10 question 8.]** |

### 5.5 Constraint check, row by row

| | verdict |
|---|---|
| **C1** no state outside the context and the encoding | **met** — §3.9: one mux, one OR gate; nothing fetched, nothing per function, nothing per context |
| **C2** no third referenced object at decode | **met** — the map is combinational, of the same kind as the opcode decoder's truth table; instruction + data remain the only two |
| **C3** 512 bits, bit-packed, not eight registers | **met at every width** `[CORRECTED - user ruling 2026-09-03 (liveness)]` — two complete tilings give 24 **direct** names, every mixture of 64- and 32-bit values is directly placeable, and #232's "16 4-byte regs" is exact. **"64 1-byte regs" is also expressible** — packed, and reached by shift-and-mask through a scratch name at ~2-3 ops per access; the old "unreachable by counting (§1.3a)" verdict is struck, since counting bounds direct names only. **A complete 16-bit tier IS nameable (§1.3b) and is declined by choice**, not by arithmetic — §10 q2 and q6. And three places where the previous draft let 8×64 back in as a constraint (§5.3, **M15**, §3.10) are corrected |
| **C4** the file may not be widened | **met** — 512 exactly |
| **C5** 512 in, 512 out, PC beside | **met** — unchanged |
| **C6** migration exactly 72 B | **met** — §5.3, zero bits added, scoreboard included |
| **C7** one file, two namespaces, no separate FP file | **met, and made structural** — §3.2: the namespaces are the *same names* |
| **C8** no `fcsr`, no rounding or FP-exception state | **met as to STATE; the remedy is softened** — no `fcsr` exists and static `rm` only, so the state constraint holds. But I.7 item 3's remedy for a function needing dynamic rounding is *"it cannot be offloaded"*, and §3.7 instead **defines DYN as RNE**, which makes such a function offloadable and silently wrong (host honours `frm`, tile does not). Stated as a deviation from Ch. 20/21 **and** as a host/tile divergence on the price list (§9.12), with a build-time gate as the partial remedy |
| **C9** no stack, no spill | **partially enforced, and said so** — §3.6: `sp` and `ra` are illegal names and no link-forming `jal`/`jalr` can be encoded, so every ABI-conforming call and stack access traps at decode; a hand-rolled link or a scratch-pointer store is not caught. **Not claimed as enforcement** |
| **C10** subset is `RV64IMAFD` and nothing else | **met as to the OPCODE LIST; NOT met as to semantics** — nothing is added or removed (§3.7), so ruling O4 needs no amendment. **[FIX — review of 2026-09-03: the previous draft scored this "met, with no deviation and no amendment" and named two deviations. There are ten, and the largest is that `f`_n_ ≡ `x`_n_ means **this is not an implementation of `F`/`D` at all** (facts §6.1: "An implementation that aliases `f<n>` onto `x<n>` is not implementing F/D").]** §8.4 lists all ten. A stock `F`/`D` implementation and this one disagree about what `f8` is |
| **C11** no vector extension anywhere | **met** — §2.1 and §4.1 withdraw the V-based retrieval path explicitly |
| **C12** nothing blocks | **met** — untouched |
| **C13** nothing speculative | **met** — untouched; §3.9 adds no forwarding, no rename and no hazard logic |
| **C14** undefined register is a hard error | **NOT met as a machine guarantee; re-homed to build time** — **M6**, §9.5. `RegLayout::illegal()` fired at run time; a total map has nothing to fire on. Over-liveness is caught only by `annotate`'s placement-disjointness check, and its run-time failure mode is a silent wrong result. **Stated as an obligation, not scored as met** |
| **C15** rejection is fatal, no truncation | **met** — §3.8, §5.2 |
| **C16** regressions must be stated | **met, and the statement is corrected** — §9. **[FIX — review of 2026-09-03: the previous draft asserted "no previously admissible function becomes inadmissible, because no instruction is removed from the subset", which conflates the opcode subset with the liveness test and is contradicted three paragraphs later by §9.2 and §9.3.]** ~~Functions **do** become inadmissible: every value narrower than 32 bits is charged 32…~~ **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** — **no function becomes inadmissible on width.** Nothing is charged 32 and nothing is rounded up; seven 64-bit values plus eight bytes is 512 bits under K.6 **and here**. The opcode subset is unchanged and so is the liveness test — it is K.6 in bits, plus the scratch bits a packing needs. The only regressions left to state under C16 are the semantic deviations of §8.4 |
| **C17** peak liveness in bits, one pool | **met, and verbatim** `[CORRECTED - user ruling 2026-09-03 (liveness)]` — §3.8 keeps K.6's shape (bits, one pool, peak simultaneous liveness), §3.2 makes the single pool structural, and **each value is charged its own width**, exactly as K.6 does. ~~it charges every sub-32-bit value 32 bits~~ is struck, and with it the claimed divergence. The one addition is the **scratch bits** a packing needs, which admission counts alongside the live bits |
| **C18** not a count of names | **met for the SUM; the added feasibility check is justified separately** — at 64 and 32 bits the name count *is* the bit count, so the R30 error is not re-introduced. But §3.8 also requires a **placement**, which is "is there an assignment to non-overlapping names" — a different test from a bit sum, flagged as such in `register-map-context.md` §7. It is justified not by the name-count identity but by §3.8's own counterexample, which shows the sum alone admits functions the geometry cannot place. Unlike R30 it never rejects a function that *can* be placed |
| **C19** a register never read costs nothing | **met** |
| **C20** admissibility is a property of the generated code | **met** — §5.2 works on emitted widths and reports the placement |
| **C21** the two measured functions must still fit | **met** — §3.8's table |
| **C22** twelve instructions plus `RESUME` | **met** — none added, none removed |
| **C23** only `funct7` `0x6`/`0x7` free | **met** — none consumed |
| **C24** the canon assigns no field values | **met with a note** — §3.2 fixes the index ranges because compiler and core must agree on them; they are an **ABI constant visible to software**, not a `funct7` value, and are marked as such |
| **C25** every operand is a value in a GPR | **met** — **M12** |
| **C26** no bit-field insert/extract with runtime (offset, width) | **met** — §3.5 uses only ratified idioms; §7.2 records the `th.ext`/`cv.extract` alternative as closed |
| **C27** `CXW`/`CXR` complete as the aperture | **met, and improved** — §4.2: one `CXR` per named value, never two |
| **C28** the lane is an instruction field | **met** — unchanged |
| **C29** tier 4 never decides anything | **met** — §5.1 rewrites the tier-4 artefacts rather than citing them |
| **C30** a fixed table at one width is the SST layout again | **met, after three corrections** — two complete widths, both built. The previous draft labelled §3.10's 8 × 64 path a *fallback* and then routed the scoreboard (§5.3), the tile build order (**M15**) and the compiler (§3.10) through it anyway, so the label did no work. All three are corrected: the scoreboard's recommendation no longer cites `scoreboard[≤8]` as a reason, the `W` tier is part of the first build, and §3.10 carries an explicit warning that the 8 × 64 path is a bootstrapping schedule and not a specification |
| **C31** the superseded passage must be marked | **met** — §5.1, including a tier-1 clause the fit-list did not list |
| **C32** do not attribute reasons the user did not give | **observed** — the rejected map is never argued against on latency, area or migration grounds; §8 prices this design without pricing that one, and §1.3 credits the ruling with the capability loss rather than blaming the loss on the map |

---

## §6 PRIOR ART

The context file's §11 item 6 records that **nothing in CANON.md or DESIGN.md mentions register
aliasing at all** — `grep -i alias` over both returns only address- and page-aliasing — so every
claim below was checked in `register-map-facts.md` before being leaned on, per the design-review
rule. Each entry says what it *teaches*, not merely that it exists.

**1. RISC-V `Zfinx` / `Zdinx` (ratified, Ch. 26) — three of this design's rules are quotations.**
Zfinx deletes the `f` file and makes FP instructions "access the `x` register with the same
number" — §3.2's rule, ratified. Zdinx solves *a value wider than a name* with **aligned register
pairs**, an explicit "the lower-numbered register holds the low-order bits" ordering, and a
specified `x0` behaviour — the model for §3.2's `f0` ≡ `x0` = +0.0 and for §9.7's answer to
values wider than 64 bits. Zfinx's "Processing of Narrower Values" supplies the substitute for
NaN-boxing that fact-C11 demands, which §3.4 relocates to the read port. **Caution recorded:
ratified Zfinx §26.1 sign-extends a narrow FP result into bits XLEN−1:*w*, which under a tiling
would overwrite a neighbour — §3.4 shows the rule is vacuous here because every FP destination
name is exactly the operation's width, and §3.7 declines the subset rename that would have
imported it.**

**2. ARM AArch32 VFP/NEON — `S`/`D`/`Q`. The closest match in shipping silicon, and it hit this
exact wall.** 32 doubleword registers `D0`–`D31` (2048 bits); `Q0`–`Q15` name 128-bit pairs;
`S0`–`S31` name the 32-bit halves of `D0`–`D15` only. **Complete tiling at 128 and 64, and a
fragment at 32** — because a 5-bit field affords 32 names and 2048/32 = 64 would be needed. ARM's
resolution was to confine the narrow tier to a contiguous low region. That is §1.3's arithmetic
and §10 question 2's answer, twenty years earlier, at four times the size — and it is the
counter-example to any claim that a tiled register bank is exotic.

**3. ARM AArch64 — `Bn`/`Hn`/`Sn`/`Dn`/`Qn`. The same vendor deliberately abandoning tiling for
hierarchy, which is exactly the choice §7.3 is about.** Five bottom-anchored views of one 128-bit
`Vn`; different-numbered registers no longer alias; a write to a narrow view **zeroes** the rest
(fact-C20), which is how AArch64 avoids x86's partial-register hazard. ARM bought this with a
doubled register file (32 × 128) so that hierarchy's stranding cost nothing — **the escape route
NMFC does not have**, because cons-C4 forbids widening the file. *Hierarchy is the right answer
when you can afford the bits; complete tiling is the answer when 512 is fixed.*
Note also that fact-C20 corrects a claim in the 2026-09-03 provisional answer: **SVE does not
have "typed views"** — `Zn` is size-agnostic and `.B`/`.H`/`.S`/`.D` are size specifiers in the
instruction, so SVE is an example of §2.2's rule, not an exception to it.

**4. x86-64 — `RAX`/`EAX`/`AX`/`AL`, the cautionary half.** Three specifics, each a decision made
differently here (fact-C19): the write rules are **inconsistent by width** (writing `EAX`
zero-extends, writing `AX` or `AL` preserves); `AH`–`DH` are **non-contiguous** views of bits 15:8
surviving from 1978; and preserve-on-narrow-write produced a documented class of **partial-register
merge stalls** and, later, dedicated merging micro-ops. §3.4's W3 cannot inherit that hazard —
there is nothing to merge, because a tile's neighbours are other architectural names and are
simply not written — and §3.9 records that the canon core has no bypass network at all.

**5. IBM System/360 and MIPS-I / SPARC V8 FP pairing.** A 64-bit double named by an even register
number, the odd one implied, misaligned numbers illegal. Thirty years before Zdinx re-ratified it —
decent evidence that **pairing is the durable answer to "a value wider than a name"** (§9.7).

**6. SPARC V9 floating point — the identical 5-bit arithmetic.** `%f0`–`%f31` are 32-bit singles,
`%d0`,`%d2`,… 64-bit doubles on even numbers, `%q0`,`%q4`,… 128-bit quads on multiples of four.
V9 widened the file to `%d0`–`%d62` and **the upper 32 doubles have no single-precision names at
all**, because the 5-bit field ran out. Complete at the wide widths, fragmentary at the narrow
one, fragment confined to one end — the same shape, again.

**7. IBM POWER VSX — two namespaces over one file, in shipping hardware.** The 64 `VSR`s are the
32 FPRs (as doubleword 0 of `VSR0`–`VSR31`) plus the 32 VMX registers. This is canon I.7's "two
namespaces over one file" built at scale — and IBM had to specify *which half* of the VSR an FPR
occupies, exactly as Zdinx specifies which register holds the low bits. **Any aliasing proposal
owes that sentence; §3.2's table is it.**

**8. Intel MMX over the x87 stack — aliasing two namespaces with different geometry.**
`MM0`–`MM7` alias the mantissa fields of `ST(0)`–`ST(7)`; the mismatch required `EMMS` and a mode
discipline that outlived its usefulness. §3.2 avoids the failure by making the two namespaces
**geometrically identical** rather than merely overlapping — no mode, no transition instruction,
nothing to forget.

**9. Motorola 68000 — the third home for width.** The register name is size-free (`D0`) and the
size lives in the opcode (`MOVE.B`/`.W`/`.L`), with narrow writes **preserving** the upper bits
(fact-C24). This is the model `register-map-alias-plus-width-in-opcode.md` follows, and §7.4 says
why it is not taken.

**10. RISC-V `P`/`Zp*` (NOT ratified) and CORE-V `cv.extract` / T-Head `th.ext`.** `P` is a
**placeholder chapter** in the current manual — "a placeholder for the forthcoming `P` and `Zp*`
extensions" — and the RISC-V dashboard puts it in *Development*; **do not cite it as ratified**
(fact-C21). Mechanically it packs lanes into `x` registers and names the lane width **in the
opcode**, with no name for an individual lane — so the RISC-V community's answer to sub-register
slices has consistently been *the opcode names the geometry*, never *the register name names the
slice*. CORE-V `cv.extract`/`cv.extractu`/`cv.insert` and T-Head `th.ext`/`th.extu` carry
**(offset, width) as immediates in the instruction** (fact-C22) — the user's option 1 done
properly, both shipping and both upstream in binutils/LLVM. §7.2 records why it is closed here.

**11. RISC-V `V`'s `vtype.SEW` — the mechanism that is ruled out.** Element width lives in a
**CSR**, set by `vsetvli` (fact-C9). It is the third home for width and it is precisely the
per-context mode register cons-C1 forbids. **Cited so that nobody offers it as prior art *for*
this design: it is prior art for the thing that was rejected.**

**12. Tagged architectures** — Burroughs B5000/B6700, Symbolics, IBM System/38 and AS/400, the
Mill's belt metadata — put the type on the **value**. That is the fourth home for width, and the
512-bit budget forbids it: tags cost bits inside the context (fact-C24).

**13. RV64E — trading names for state, with NMFC's own motivation.** RV64E "reduce[s] the integer
register count to 16 general-purpose registers (`x0`–`x15`)", and its stated motivation is
"interest in RV64E for microcontrollers within large SoC designs, and **to reduce context state
for highly threaded 64-bit processors**" (fact-C17). This design does the mirror-image trade —
keeping 31 names and shrinking what each denotes — for the same reason.

**What prior art actually settles.** Width has four possible homes: the **register name** (x86,
AArch64, AArch32, SPARC, this design), the **opcode** (68k, SVE, RISC-V `P`), a **mode register**
(RVV), and the **value** (tagged machines). Three are ruled out here by cons-C1 and the 512-bit
budget. And of the shipping designs that put width in the *name* and had to cover a file wider
than 32 × width, **every one made the same choice this design makes: complete tilings at the wide
widths, and any narrow tier confined to one end of the file as an acknowledged fragment.**

---

## §7 ALTERNATIVES CONSIDERED, AND WHY REJECTED

### 7.1 The per-function register map — REJECTED BY THE USER, 2026-09-03

The mechanism written into DESIGN.md §25.7 at tier 3 and built as `RegLayout` at tier 4: a
per-function table of (offset, width) beside the instruction cache, indexed by the function a
context is running, resolving every register operand through `layout_.field[r]`.

> **The user's objection, verbatim (2026-09-03):** *"I really don't like your idea. It introduces
> a third piece of memory every context needs. So now we have the map, instruction, and
> potentially data that must be referenced all at the same time. That frankly seems foolish."*

**The objection is specifically to simultaneity** (cons-C2): instruction + data is two; a map is a
third. **Answering it by pointing at duplicate pages — "the map is a compile-time constant
travelling with the code, resident on every tile" — does not answer it and must not be
re-offered**, because the objection is not that the map is unavailable.

Two things must be said honestly and neither is an argument against the ruling:

- **The rejected map was strictly more expressive**, and §1.3/§9 price what is given up: it
  named a 48-bit pointer, a 12-bit index and a 3-bit tag **directly**, in one lookup instead of
  a shift-and-mask. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **It was not more expressive**: both hold 512 bits and both
  express #232's "64 1-byte regs". The `alias-tiled` §8.2 enumeration (~13,091 against ~2,137)
  counts **directly nameable** width-multisets — instruction count, not capability — and the
  honest statement of the difference is **a map lookup per sub-name access under the rejected
  map, against ~2-3 ops per packed access here.**
- **This document argues against it on no other ground** (cons-C32). Not latency, not area, not
  migration. The ruling's stated reason is the third referenced object, and that is the only
  reason in play.

### 7.2 Option 1 — custom instructions that encode type — REDUNDANT, twice over

The user's own parenthetical asks the right question: *"assuming risc-v has some instructions
which infer based on target registers"*. **No base instruction does, and the ISA went out of its
way to make sure of it** — Zfinx is the proof (§2.2, fact-C9/C10). RISC-V opcodes already carry
type and operation width (`add`/`addw`, `fadd.s`/`fadd.d`), so an instruction that "encodes type
into it" is describing what every RISC-V instruction already is. **Option 1 is redundant for
type.**

But the redundancy is *only* about type, and the honest version of option 1 is a different
instruction: one that carries **extent** — (offset, width) as immediates, the `th.ext` /
`cv.extract` shape (fact-C22). That is a genuine alternative to naming the slice, and it is
rejected on two grounds already in the record rather than on taste:

- **cons-C26 closed it:** *"A bit-field insert and extract carrying an offset and a width was
  considered and dropped: it would duplicate instructions that exist"* (I.8), under #233's
  *"Let's not overdesign."*
- **cons-C23 leaves no room to reopen it:** only `funct7` groups `0x6` and `0x7` are free, and
  privileged `RESUME` already claims one.

And a cost the trade-off deserves: an immediate-carried (offset, width) costs **instruction bits
and an extra instruction per access**, where a fixed map costs **nameable-set flexibility**. On a
machine whose whole value is offload cheapness, paying an extract per operand is the wrong side of
that trade.

### 7.3 `alias-hierarchical` in its pure form (H-a) — the archetype, and it strands the file

The x86/AArch64 archetype over 512 bits: eight 64-bit lanes, each also nameable at 32, 16 and 8
bits **anchored at the lane's low end**. 8 × 4 = 32 names, one lost to `x0` → 31, the same budget.

**Within a lane every name contains the next, so no two names in a lane are disjoint, so at most
one value can be NAMED DIRECTLY in a lane — eight directly-named values, whatever their widths.**
`[SUPERSEDED - user ruling 2026-09-03 (liveness)]` ~~Eight 8-bit values use 64 of 512 bits and a ninth is inadmissible. Bit-packing buys
*nothing*~~ — **struck.** A ninth value packs alongside one of the eight and is reached through a
scratch name; H-a holds the same 512 bits as anything else. Its real defect is that it pays ~2-3
extra ops on almost every access past the eighth value — a **speed** indictment, which is
cons-C30's "fixed aliasing at one width is the SST layout again" reappearing in a scheme that
looks like it has four widths.

| width | this design's coverage | H-a's coverage | reachable at all? |
|---|---|---|---|
| 64 | **512/512 = 100 %** | 512/512 = 100 % | yes, 8 names |
| 32 | **512/512 = 100 %** | 256/512 = 50 % | yes, 16 names |
| 16 | 0 % — **declined, not impossible** (7 names reserved; a complete tier needs 32 and they exist in the `f` namespace) | 128/512 = 25 % | **yes — §1.3(b); declined by §1.3(c)/(d), §10 q2/q6** |
| 8 | 0 % (7 names reserved) | 56/512 = 10.9 % | **yes — by packing, at ~2-3 ops per access; 64 DIRECT names would be needed and 63 exist (§1.3a)** `[corrected - user ruling 2026-09-03 (liveness)]` |

`[CORRECTED - user ruling 2026-09-03 (liveness)]` The "reachable at all?" column is a
**directness** column: every width is reachable at every coverage figure above, because a
value with no name of its width is packed inside a wider one. Coverage percentages measure
how often an access is one instruction instead of three.

H-a is *broader* below 32 bits and it does not help, because coverage at a width is only worth
having when it is coverage of bits some other value could otherwise have used. **The concrete
case: 4 × 64 + 5 × 32 = 416 bits, comfortably inside the file.** This design names all nine
directly; **H-a names eight and packs the ninth** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` (~~H-a rejects it — nine values,
eight lanes~~: struck, it admits it). By exhaustive enumeration H-a **names directly** ~495
packings against this design's ~2,137 for the same 31 names. And because there is no stack
(cons-C9), a genuine overflow of 512 bits is a **rejection** rather than a spill — but **name**
pressure is not bit pressure: a scheme that covers less of the file at 32 bits **does run
slower**, by ~2-3 ops per packed access, and makes nothing inadmissible.

**What is taken from `alias-hierarchical` rather than rejected:** its actual proposal was not H-a
— it was D+W complete plus a narrow buddy subtree — and §3.2's map, §3.3's anchoring and §3.4's
port rules are its work. §10 question 2 keeps its narrow subtree available as the shape for the
seven reserved names.

### 7.4 `alias-plus-width-in-opcode` — the 68k answer, and it costs too much for what it buys

Eight 64-bit anchors, with width and slice index carried in a **new custom major opcode** (~50
instruction forms, an elastic `funct` field, three aligned-extract networks and a segmented ALU).

Rejected on five grounds, of which the first is structural:

1. **Its discriminator against the user's option 2 is a strawman.** Its §2 rejects "a fixed mixed
   table" by requiring `64n₆₄ + 32n₃₂ + 16n₁₆ + 8n₈ = 512` for one chosen tuple and observing
   that `nmfc_bu` needs seven 64-bit names and no such tuple has them. **That equation assumes
   the widths PARTITION the file.** The user asked for the opposite — *x86 RAX/EAX/AX/AL-style
   aliasing generalised* — where `EAX` and `RAX` are the same bits and no sum constraint applies.
   This design names eight complete 64-bit slices **and** sixteen complete 32-bit slices that
   overlap them, in 24 names, at zero opcode cost, and holds `nmfc_bu`'s 7×64 + 1×32 easily.
2. **There is no way to branch on a packed slice.** Its narrow tier has no compare-and-branch and
   no flag; branches stay base-tier and compare **whole 64-bit anchors**, so testing one packed
   field costs a move into a dedicated anchor — 12.5 % of the context per live branch condition,
   on a machine whose premise is that a 32-bit value costs 32 bits. Under this design the problem
   cannot arise: a 32-bit value **has its own name**, and `beq w3, w4` compares exactly it.
3. **It amends ruling O4 twice** — adding a tier and deleting five ratified instruction families
   (`addw`/`addiw`/`sext.w`, `flw`/`fsw`, every `.s` form) — which makes stock RV64 codegen for
   any `int` or `float` expression illegal and **kills the `-ffixed` day-one gate outright**, the
   cheapest path the record has identified. This design amends nothing (§3.7).
4. **It spends a custom major opcode and ~50 forms** against #233's standing *"Let's not
   overdesign"*, and prices the datapath as "area, not state" on a machine whose tiles-per-channel
   count is set by area.
5. **What it genuinely buys** — sub-32-bit widths and exact 12-bit fields — is real, and it is
   also a standing counterexample to any claim that sub-32-bit slices are *impossible* rather
   than declined: putting the width in the opcode reaches widths the name cannot, at the cost
   of a custom major opcode. §1.3 no longer makes that claim. The same capability is held open
   at **zero** opcode cost by §10 question 2's seven reserved names, and at zero opcode cost
   for a *complete* 16-bit tier by §10 question 6's fork.

### 7.5 Single-width fixed aliasing — this is the SST layout again

`x1`–`x8` = `f0`–`f7` as eight 64-bit slices, with `addw` for low halves, is
`RegLayout::defaultLayout()` with the table deleted. It satisfies cons-C1 and **violates cons-C3
in substance: it makes the context eight 64-bit registers**, which is the exact formulation
invariant I2 exists to stop — *"Once again, NO. 512 bits of context. The context is not 8 regs.
Why do you keep reverting to that?"* (#238). `NMFC_CTX_WORDS = 8` and `x1..x8` are tier 4's
packing on a Rev core and decide nothing (cons-C29). **It is retained here only as the day-one
toolchain fallback (§3.10), explicitly labelled as a fallback and not as the design.**

### 7.6 `alias-tiled` as written — the argument is adopted, three defects are not

Its analysis is the backbone of this document. Three things are changed:

- **The index assignment.** `d0`–`d7` at `x1`–`x8` = `ra, sp, gp, tp, t0, t1, t2, s0` falsifies
  its own day-one claim (§3.3), and puts arguments `a0`–`a7` = `x10`–`x17` in the **32-bit** tier,
  incoherent with its 64-bit fallback story. Replaced by `alias-hierarchical`'s anchoring.
- **The strict equal-width rule.** It made mixed-width operands illegal and then needed a carve-out
  for `addi rd, rs1, 0`, so legality depended on **an immediate's value** — `addi d1, w3, 0` legal,
  `addi d1, w3, 5` illegal. Replaced by the port rules (§3.4), which delete the carve-out and the
  rule together. Its rule-1 claim that stock `addw`-for-`int` codegen is "legal by construction"
  was backwards; §3.5 makes it true instead.
- **The `H` tier.** Seven names spent on a fragment with no ratified arithmetic (there is no
  RV16I), no measured demand — neither `nmfc_bu` nor `nmfc_expand` uses one — an unnameable eighth
  halfword, a hard collision with the eighth `d` name, an odd-count correction term in the
  admission formula, and a 16-bit-granular scoreboard. **Deferred to §10 question 2**: reserving
  the seven forecloses nothing. It does **not** come free — every sub-32-bit value is then charged
  32 bits and functions K.6 admits are rejected (§3.8, §9.2) — and a *complete* narrow tier is
  §10 question 6's fork, not a seven-name fragment.

---

## §8 COST

### 8.1 State

| where | this design | for comparison: the rejected per-function map |
|---|---|---|
| **per context** | **0 bits added.** A context is 512 bits; a context + PC is 576 bits = 72 B | 0 added (the table was not per context) |
| **per context, scoreboard** | **0 bits added** on the canon core — one pending destination, §5.3. On the ChampSim core model, 8 lane-granular ready bits, i.e. `scoreboard[≤8]` **verbatim** | unchanged, and equally undefined under aliasing |
| **per function** | **0 bits.** There is no per-function object of any kind | `RegLayout` = 32 × (`uint16` offset + `uint8` width) = **768 bits (96 B)** as built; 512 bits packed minimally — *almost exactly one context, per resident function* |
| **per tile** | **310 bits** of decode ROM (31 × 10), or ≈ 20 gates of mux and OR, **shared by every context and every function on the tile** | the same path, **plus** one `RegLayout` per resident function beside the I-cache, plus the lookup |
| **per tile, as a fraction** | 39 B against 64 KiB of context state at C = 1024 → **0.06 %** | 96 B × (resident functions) on top |
| **migration** | **72 B exactly** — 64 B context + 8 B PC (cons-C6, **M11**). Nothing the scheme adds travels | 72 B (the table was resident, not carried) |

### 8.2 Time

| path | cost |
|---|---|
| **decode** | **+0 cycles.** The map resolves from the instruction's own 5-bit field in parallel with opcode decode, from one mux and an OR gate (§3.9). It cannot extend a stage that already decodes a 32-bit instruction word |
| **register read** | **+2 mux levels** over a fixed 8 × 64 file — a 2:1 half select on `offset[5]` and a 2:1 sign-extension select on the upper 32 bits, both fed by an offset resolved a stage earlier (§3.9). Real, and not a cycle |
| **register write** | **+0.** Byte enables off the same decode; no read-modify-write, no merge, no partial-register hazard (§3.9) |
| **hazard / forwarding** | **+0, and the reason is canon, not assertion.** CANON.md:474-478: one instruction per context in flight ⇒ no forwarding, no interlocking, no hazard detection. A non-barrel implementation would owe a 5-bit overlap comparator and a byte-masked bypass (§3.9) |
| **memory** | **unchanged.** Nothing is fetched to decode a register name (cons-C1, cons-C2) |
| **the ALU and the M unit** | **NOT free, and the previous draft priced this at zero.** See below |

**The dual-width datapath, which the previous draft did not price.**
**[FIX — review of 2026-09-03: the previous draft charged +2 mux levels on register read and
+0 everywhere else, which prices the *register file* and not the *execution units*. W1b's "the
instruction executes as though XLEN equalled 32" is not a truncation of the 64-bit answer for
several opcodes, and each of those needs real RV32 logic.]**

| what needs 32-bit behaviour | why it is not a truncation of the 64-bit result | cost |
|---|---|---|
| `slt` / `sltu` | the sign bit and the unsigned ordering are at bit 31, not bit 63 | a second comparator tap, ~1 gate level on the flag |
| register shifts (`sll`/`srl`/`sra`) | the shift-amount mask is `rs2[4:0]` at XLEN = 32 and `rs2[5:0]` at 64. **The previous draft stated this for immediate shifts (`shamt[5]` reserved) and never for register shifts** | one AND on the shift amount, driven by the same width signal |
| `mulh` / `mulhu` / `mulhsu` | a *different product tap* — bits 63:32 of a 32×32 product, not bits 127:64 of a 64×64 one | the multiplier already computes the low half; the high-half select becomes 2:1 |
| `div` / `divu` / `rem` / `remu` | the overflow special case is −2³¹ ÷ −1 at 32 bits and −2⁶³ ÷ −1 at 64, and the quotient of two sign-extended 32-bit values is **not** the sign-extension of the 32-bit quotient in the overflow case | one extra compare against the 32-bit sentinel in the divider's special-case logic |
| RV64-only opcodes with **no** RV32 meaning — `lwu`, `fcvt.l.*`/`fcvt.lu.*`, `fmv.x.d`/`fmv.d.x`, the 64-bit atomics | there is nothing to execute at 32 bits | **no cost — they are illegal at a narrow name (§3.5 rule 5)**, which is why that rule exists |

**Net:** a handful of extra gate levels and one extra mux inside units that already exist,
driven by a single width bit resolved at decode. It is **not** a second ALU and it is **not** a
dual-XLEN subtarget — every RV64 unit already contains its 32-bit case, because RV64 already
has `addw`, `mulw` and `divw`. But "+0 on everything but register read" was wrong, and an RTL
engineer reading the old table would have under-built the M unit.

**Barrel-depth check, because it is the number that would actually bite.** Canon H.2 makes the
re-issue depth `Dp` a first-class parameter through **`C ≥ W × (Dp + L/I)`** (CANON.md:192-193), so a
deeper register-read path would raise the required context count `C` and multiply the tile's dominant
SRAM term — 64 KiB of context state at C = 1024. **`Dp`
is unchanged**: the two extra mux levels sit inside a register-read stage that already exists, fed by an
offset resolved at decode. Stated because `alias-tiled` asserted "+2 mux levels, hidden behind
decode" without ever connecting it to `Dp`, `C` or that inequality.

### 8.3 Host instruction count per offload

**[FIX — review of 2026-09-03: every figure in this table was understated, because §4.3's
"good idiom" omitted the zero-extension that Ch. 5's sign-extension invariant makes mandatory.
Corrected below; the conclusion changes with it.]**

| what the host does | cost (stock RV64GC) | with `Zbb` |
|---|---|---|
| stage one 64-bit argument | 1 `CXW` | 1 |
| stage two 32-bit arguments into one lane (**the correct idiom**, §4.3) | **5 instructions = 2.5 per argument** | 4 = 2 per argument |
| stage one 32-bit argument into a lane whose other half is already live | **7 instructions** — avoid by staging lanes whole (§4.3) | 6 |
| read back one 64-bit result | 1 `CXR` | 1 |
| read back one 32-bit result | `CXR` + 2 | `CXR` + 1 |
| read back one `f32` result into an `f` register | `CXR` + 2 + `fmv.w.x` | `CXR` + 1 + `fmv.w.x` |

**Eight 32-bit arguments cost 20 instructions and 4 `CXW`s** (16 with `Zbb`), against **8
`CXW`s** for eight 64-bit arguments.

**Packing IS a tax on the host, per bit moved**, and the previous draft's claim that it was
*cheaper* is withdrawn: 64 bits per instruction unpacked against 12.8 packed. The tax is
2.5 instructions per 32-bit argument, paid once per offload, in the caller's frame, with a
stack and 31 real registers. **The comparison that matters is not packed-versus-unpacked
staging — it is offloading versus not being able to**, because a function needing nine live
values does not fit 512 bits unpacked. A `FORK` and a round trip to a tile dominate twenty
integer instructions by orders of magnitude.

### 8.4 ISA surface

| | change |
|---|---|
| new instructions | **none** — cons-C22 intact: twelve user-level plus privileged `RESUME` |
| instructions removed | **none** — §3.7; ruling O4's **opcode list** stands unamended and needs no amendment. Its **semantics** are amended in ten places, below |
| `funct7` space consumed | **none** — cons-C23: `0x6`/`0x7` still free for `KILL`, mailboxes, `RESUME` |
| encoding format changes | **none.** Not one bit of instruction encoding moves, so a stock assembler emits every encoding and a stock `objdump` decodes it. **It does not follow that a stock toolchain is *correct* here** — see the two rows below |
| assembler register names | **`d0`–`d7` and `w0`–`w15` are DOCUMENTATION-ONLY aliases** for `x8`–`x15` and `x16`–`x31`. No binutils patch is required or assumed; every code sample in this document is written in them for readability, and every flag recipe (§3.10) and ABI table (§4.4) is spelled in `x` numbers, which are the same names. A back end that wants them as recognised register names must patch binutils — optional, and it buys only diagnostics. **[FIX — review of 2026-09-03: the previous draft's samples were all in `d`/`w` names that no stock assembler accepts, beside a claim that a stock assembler emits them; §9.11 conceded only a *display* problem. An asm-printer author needed this ruling before the first line.]** |
| disassembly | **stock `objdump` is correct but misleading** — §9.11. It prints `x23` and implies 64 bits where the ABI means `w7` at 32 |
| new legality checks | **seven** (§3.5) |
| stated deviations from the ratified manual | **ten** — listed below. The previous draft said "two", and §5.5's C10 row said "no deviation"; both are corrected |
| ABI constants newly fixed | the index ranges of §3.2, and the argument/return conventions of §4.4/§4.5 once §10 q4 and q7 are ruled — visible to software, so published once, and marked as ABI rather than as canon (cons-C24) |

**The ten deviations, in descending order of consequence.** An implementer building from this
document must implement each of them; a stock `RV64IMAFD` core does not.

| # | deviation | ratified text it departs from | consequence if implemented as ratified instead |
|---|---|---|---|
| 1 | **`f`_n_ ≡ `x`_n_ — there is no separate FP register file.** This machine is **not implementing `F`/`D`** | Ch. 20/21; facts §6.1: "An implementation that aliases `f<n>` onto `x<n>` is not implementing F/D" | +256 B of architectural state per context, which cons-C4 forbids and which would break I2 and I11 |
| 2 | **`f0` is hardwired: reads +0.0, discards writes.** `f1`–`f7` illegal as any operand | ratified `F`/`D` has no hardwired-zero FP register; fact-C17: "`f0` **is** general". `f0`–`f7` = `ft0`–`ft7` are the stock FP temporaries | stock FP code touching `ft0` has its results silently discarded; `ft1`–`ft7` trap. Second cause of §9.9's day-one FP hole |
| 3 | **NaN-boxing abolished, both halves** — no boxing on write, no box check on read (§3.4) | Ch. 21 §21.1.2, fact-C4 (write half and read half) | an `f32` in a `w` name has zeros above it, fails the ratified box check and is read as **canonical NaN** — the hole the previous draft left open |
| 4 | **FLEN is per-instruction (the mnemonic's width), not an implementation constant** | Ch. 20/21 define FLEN as fixed for an implementation | every narrow FP result would sign-extend or box into a neighbouring tile |
| 5 | **The tile and the host have different `f32` semantics** (3 above) and **different rounding** (6 below) | — | silent numeric divergence between a function run locally and the same function offloaded; §9.12 |
| 6 | **`rm = DYN` (`0b111`) defined as RNE** rather than reading `fcsr.frm` | Ch. 20/21; and CANON I.7 item 3, whose remedy is "it cannot be offloaded" | rejecting DYN makes all stock FP codegen illegal; accepting it as ratified needs an `fcsr`, which cons-C8 forbids |
| 7 | **XLEN is per-instruction** — W1b executes at 32 when the widest operand is 32, importing RV32I/RV32M semantics for `slt`, register shifts, `mulh*`, `div`/`rem` | Ch. 2/5 define XLEN as fixed | a 32-bit name would be read and written at 64 bits, i.e. no bit-packing at all |
| 8 | **`*W` opcodes legal on a 32-bit name**, read as the ratified operation with a zero-width extension field (§3.5) | Ch. 5 defines `*W` on 64-bit registers | stock `int` codegen (`addw`, `mulw`) becomes illegal, which kills the day-one path — §10 q3 |
| 9 | **`x1`–`x7` illegal as any operand**; RV64's 31 general registers become 24 plus a zero. **This makes `ret` illegal** (§3.6) | Ch. 2 | `sp`/`ra` would be ordinary names and the I7 tripwire of §3.6 would be gone; but every stock body's terminating `ret` must be rewritten (§3.6) |
| 10 | **`jal`/`jalr` with `rd` ≠ `x0` illegal; `auipc` restricted to a `d` destination** (§3.5 rules 3, 1b) | Ch. 5 | ABI-conforming calls would be encodable, breaking I7's no-stack invariant at the decode level |

---

## §9 WHAT THIS CANNOT DO

Read this as the price list. **Free truncation is not on it** — the low half of `d`_k_ **is**
`w`_2k_, so narrowing a 64-bit value to 32 costs no instruction.

**[FIX — review of 2026-09-03: the previous draft's preamble also said "no previously
admissible function becomes inadmissible, because nothing is removed from `RV64IMAFD`", and
§5.5's C16 row said the same. That conflates the **opcode subset** with the **liveness test**,
and it is contradicted by §9.2 and §9.3 three paragraphs below. Functions do become
inadmissible.]**

**Both the subset and the admission test are unchanged.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` No opcode is removed
from `RV64IMAFD`, so no function becomes inadmissible for containing an instruction; and
~~every value narrower than 32 bits is now charged 32, so functions K.6 admits in bits are
rejected here~~ is **struck**. Seven 64-bit values plus eight bytes is **512 bits under K.6 and
512 here.** cons-C15's fatality applies only to a genuine overflow of 512 bits (live plus
scratch). **What §9 prices from here on is instruction count, not admission.**

**9.1 #232's "64 1-byte regs" is EXPRESSIBLE; what is not available is a direct NAME for each
byte.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` The struck version read *"inexpressible, and that one IS arithmetic."* The
arithmetic is real but it counts **names**: 64 byte slices would need 64 names and a 5-bit field
affords at most 63 across both namespaces. Sixty-four live bytes nonetheless **fit and run** —
eight per 64-bit name, read and written by shift-and-mask through a scratch name, ~2-3 ops per
access. **Canon I2's "ANY combination" is not narrowed, there is nothing to escalate, and §10
question 1 is withdrawn as an acknowledgement request** (it survives only as a question about how
many extra ops byte-heavy functions would pay). The name density belongs to this design, not to
the 2026-09-03 ruling, and it costs speed, not capability (cons-C32).

**What is NOT provable, and what the previous draft wrongly claimed was:** that at most two
widths can be complete. **8 + 16 + 32 = 56 ≤ 63**, so a complete 16-bit tier *is* nameable
(§1.3b). It is declined here because the ruled subset has no 16-bit arithmetic (§1.3c) and
because §3.2 spends the `f` namespace on identity aliasing (§1.3d) — a subset constraint and a
design choice, both of which the user can revisit. **§10 question 6.**

**9.2 No name is narrower than 32 bits in the recommended map — and this costs INSTRUCTIONS,
not admission.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` ~~and this is a REGRESSION against K.6~~ — struck. A boolean, a
byte counter, a level number and a 3-bit tag each cost **their own width in bits**, plus the
scratch space their packing stages through, plus ~2-3 ops per access. **There is no ceiling on
live values** — the struck text said "sixteen, whatever their widths", which is exactly the
count-of-values framing the ruling forbids; the context is 512 independent bits on a strictly
in-order core with no renaming, so as many values may be live as fit in bits with room to stage.
K.6's bit test and this one agree. §3.8's "704" counterexample is withdrawn. Seven names are reserved against a measured demand (§10 q2) and a
complete 16-bit tier is available at the cost of §10 q6's fork; until one is taken, this is the
floor.

**9.3 No non-power-of-two width and no unaligned placement have DIRECT NAMES.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
A 48-bit pointer and a 12-bit index have no name of their width, so each is packed and reached by
shift-and-mask through a scratch name at ~2-3 ops per access. **Nine 48-bit values are 432 bits
and they FIT** — the struck text said they *"do not fit, because they need nine 64-bit tiles"*,
which charges each value the width of a name. The rejected map reaches them with a lookup instead
of the shifts. The ~13,091 → ~2,137 figure counts **direct nameability**, not capacity.

**9.4 The width mix is frozen ISA-wide and cannot be retuned per function.** A function that is all
64-bit leaves 16 names unused; a function wanting twelve 16-bit values **has them, packed**, and
pays ~2-3 ops per access rather than being denied them `[corrected - user ruling 2026-09-03
(liveness)]`. **Per-function retuning is precisely what the 2026-09-03 ruling removed**, and this is
where the removal is felt. Recorded, not argued.

**9.5 Overlap is undetectable by the machine, and this is a DOWNGRADE of cons-C14.** `d0` and
`w0`/`w1` are the same bits. An allocator that makes both live simultaneously produces a silent
wrong answer, and no decode check can see it (§3.8). `RegLayout::illegal()` used to catch the
analogous error **at run time**; a total map has nothing to fire on. The check re-homes onto
admission as a **placement-disjointness verification** — a compiler-correctness obligation and a
tool obligation, **not a hardware guarantee**. §5.5's C14 row now scores it *not met as a
machine guarantee* rather than *met, re-homed*. **The concrete instance to watch is §4.4's
`d0`:** under the stock ABI, `s0` looks like scratch and is occupied whenever a seventh or
eighth integer argument is passed.

**9.6 The admission guarantee is exact at a program point, not proved over live ranges.** §3.8. The
bit test is necessary and not sufficient; the tool must verify a placement; relocation moves repair
the gaps the geometry can open; the empirical evidence for the gap being narrow is a stress test
reported in the judging and **not independently reproduced here**.

**9.7 No name is wider than 64 bits.** A 128-bit value has no name. The remedy is Zdinx's — an
aligned pair `d`_2k_:`d`_2k+1_ with the low-numbered name holding the low bits, odd numbers reserved
(fact-C16). **It is not proposed**, because nothing in the record asks for a 128-bit value and the
rule would cost a legality check and an alignment constraint for no measured benefit. Under §3.1's
heap rule the names for such pairs already exist as `x4`–`x7`; that is the shape if it is ever wanted.

**9.8 Invariant 7 is enforced against the ABI idiom and nothing more.** §3.6. Every call a compiler
emits and every ABI-conforming stack access traps at decode; a hand-rolled link through `auipc` and
a store through a scratch pointer do not. **Not claimed as enforcement.**

**9.9 A stock toolchain reaches the D tier only, and only for integers — and there are TWO
causes, not one.** §3.10. Floating point on day one is unavailable because (i) `-ffixed-x`_n_
does not reserve `f`_n_ while `f`_n_ ≡ `x`_n_, so a stock compiler allocating an `f` register
would **silently corrupt** a reserved tile; and (ii) §3.2/§3.5 make `f0` a hardwired zero and
`f1`–`f7` illegal, i.e. **the entire stock FP temporary set `ft0`–`ft7` either discards results
or traps** (§8.4 deviation 2). The previous draft named only (i). `-march=rv64ima` is the only
reliable day-one spelling and closes both. The `W` tier needs a register-class split in the back
end — **not** a change to the tile, which implements `W` in hardware from day one (**M15**); no
flag can retype a register.

**9.10 It does not solve the compiler's packing problem.** It converts "choose 32 (offset, width)
pairs per function" into "allocate over two nested register classes", which every back end already
knows how to do — but somebody still has to do it, and I.8's *"open, and it is a compiler problem"*
stays open.

**9.11 Disassembly lies by default.** Stock `objdump` prints `x23` and implies 64 bits where the
ABI means `w7` at 32 — correct decoding, misleading display. A one-page alias table fixes the
display; nothing fixes a reader who does not know the map. **`d`/`w` are documentation-only
names** (§8.4): the assembler accepts only `x8`…`x31`, every flag recipe and ABI table in this
document is spelled that way, and a binutils patch to recognise `d`/`w` is optional and buys
only diagnostics.

**9.12 The tile and the host compute different floating-point answers, silently.** Two
independent causes, both from §3.7/§3.4: `rm = DYN` means RNE on the tile and reads `fcsr.frm`
on the host, so a caller that set a rounding mode gets a different result from an offloaded
function than from a local call; and NaN-boxing is abolished on the tile and honoured on the
host, so a non-canonically-boxed `f32` is a valid operand on one and canonical NaN on the other.
**No admission check can see either**, because every stock FP instruction carries DYN and no
instruction announces its boxing assumptions. CANON I.7 item 3's remedy for the first is
*"it cannot be offloaded"*; §3.7 softens that to a build-time gate (reject any function reaching
`fesetround`, or any translation unit compiled `-frounding-math`). **This is the deviation with
the worst failure mode in the document, and it is here rather than in a footnote for that
reason.**

---

## §10 OPEN QUESTIONS FOR THE USER

Nine, and each one genuinely needs a ruling rather than more analysis. Q6–Q9 are new; the
previous draft either did not ask them or asked a narrower question in their place.

### Q1 — ~~Acknowledge that the ruling makes the BYTE tier impossible~~ **WITHDRAWN**

> **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** *(dated 2026-09-03)* **This question is
> withdrawn: there is nothing to acknowledge.** The byte tier is **reachable**. Sixty-four live
> bytes pack eight per 64-bit name and are read and written by shift-and-mask through a scratch
> name — plain RV64I, no new instruction, ~2-3 extra ops per access. Canon I2's *"16 4-byte regs,
> 64 1-byte regs, or ANY combination"* **stands unnarrowed**, and no ledger entry against it is
> owed. What the 5-bit field bounds is how many slices can be named **directly**, which is a
> question of instruction count. If a version of Q1 survives, it is: *how often do offloaded
> functions touch sub-name values, i.e. how much speed would a byte tier or A2's extent
> instructions actually buy?* — and that needs measurement, not a ruling.

**The struck question, retained for the record.** Canon I2 quotes you directly: the context
"could be 16 4-byte regs, **64 1-byte regs**, or ANY combination." Of those, one is not
**directly nameable**:

- **"8 8-byte regs" and "16 4-byte regs" — delivered exactly**, and every mixture of the two.
- **"64 1-byte regs" — not DIRECTLY nameable by any scheme in which the register number alone
  denotes the slice; fully expressible by packing.** 64 byte slices would need 64 names; a 5-bit
  field affords 32 per namespace and `x0` is hardwired, so 63 exist. fact-C17 gets there
  independently: 512/63 ≈ 8.1 bits per name. **The arithmetic is about names, and names are
  instruction count** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` (§1.3a, corrected).
- **A complete 16-bit tier — REACHABLE, and declined by choice.** 8 + 16 + 32 = 56 ≤ 63. It is
  declined here for two reasons, neither of them arithmetic: `RV64IMAFD` has no arithmetic
  below 32 bits (ruling O4), and §3.2 spends the `f` namespace on identity aliasing. **Q6 puts
  that second reason to you.**

**[FIX — review of 2026-09-03, and this correction is why the question is reworded. The previous
draft's Q1 asked you to ratify a **"provable" impossibility that is not proved**: it claimed "at
most two widths can be complete … this is arithmetic, not a design choice", which is false —
8 + 16 + 32 = 56 fits in 63. It also restated your ruling as "the register name **alone** must
determine the slice", dropping the ruling's own clause "*together with its namespace (`x` vs `f`)
**and the opcode***". Asking you to ratify a narrowing of a tier-1 `[SHARPENED]` block as "the
price of the ruling", when part of the narrowing is this document's design choice, is the shape
cons-C32 exists to stop. Corrected above.]**

**Why it no longer needs you.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **Nothing is narrowed.** I2 stands as written; the
fixed map loses no sub-32-bit expressiveness, and the per-function map would regain none —
**both hold 512 bits**, and they differ only in whether a sub-name access costs a map lookup or
~2-3 shift-and-mask ops. **Recommendation (a) stands, on the simpler ground that it costs no
capability and adds no third object**, and no ledger entry against I2 is owed. The 16-bit tier
remains a live question at Q2 and Q6 — as a **speed** question.

### Q2 — The seven reserved names `x1`–`x7` / `f1`–`f7`

**The question.** Leave them **reserved, illegal** (recommended), or spend them now on a narrow
tier — and if so, in which shape?

**Recommendation: reserve — but with the cost stated, because the previous draft understated it.**
Nothing in the record measures a demand: neither `nmfc_bu` nor `nmfc_expand` uses a sub-32-bit
value. Reserving buys four things: the admission test keeps **one** clause and no correction term;
there is no invented 16-bit arithmetic (RISC-V has never ratified an RV16I, so any `add` at 16 bits
is behaviour no RISC-V document specifies); there is no collision between a narrow tier and the
eighth `d` name; and #233's *"Let's not overdesign"* is honoured. **Reserved names can be defined
later; defined names cannot be undefined.**

**What reserving costs, stated and corrected** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`**:** ~~every sub-32-bit value is
charged 32 bits, so functions K.6 admits are rejected~~ — **struck.** Reserving costs
**instructions**: every access to a sub-32-bit value goes through a shift-and-mask sequence
staged in a scratch name, ~2-3 ops, instead of naming the value directly. Nothing is rejected
that K.6 admits, admission **is** K.6 in bits, and §9.2's "regression" is withdrawn. Note
also that seven names give at most a **fragment**; a *complete* 16-bit tier needs 32 names and is
Q6's fork, not this question.

**If you want the tier now, the shape is settled and it is `alias-hierarchical`'s**: a **complete
buddy subtree of one 32-bit region**, six names spent as `x2`,`x3` = the two 16-bit halves of `w15`
(bits 480–495, 496–511) and `x4`–`x7` = their four bytes, with `x1` reserved. It is preferable to
seven flat 16-bit names because it is *complete* — the placement lemma extends to it, admission gains
~~only the clause "sub-32-bit values total ≤ 32 bits"~~ **no new admission clause at all
`[corrected - user ruling 2026-09-03 (liveness)]` — a narrow tier buys direct names, not
capacity** — all irregularity is confined to 32 bits of
the file, and it yields **bytes**, which is the record's own narrow example (DESIGN §22's
byte-per-vertex frontier). Its costs, stated: `x2` = `sp` stops being an illegal name, weakening the
§3.6 tripwire to the store alone; and 16-bit arithmetic still needs a ruling.

**What would settle it: one measured function that wants a byte.** Until then, reserve.

### Q3 — `*W` opcodes on a 32-bit name: permissive (recommended) or strict?

**The question.** §3.5 makes `addw`/`sllw`/`mulw`/… legal on a `w` destination, meaning the plain
32-bit operation, because at 32-bit execution width the sign-extension field has zero width. The
alternative is to make them **illegal** on a `w` name, as `alias-tiled` did.

**Recommendation: permissive.** It is what makes the toolchain story true: stock RV64 codegen emits
`addw` for every `int` expression, so under the permissive rule **register-class assignment alone
produces correct `int` arithmetic**, and the back-end work drops to suppressing the `sext.w`/`zext.w`
canonicalisation peepholes. Under the strict rule, instruction selection must become width-aware per
register class before any `int` code runs at all. And the trap strict buys **fires only on correct
programs**: it catches `addw` on a 32-bit value, which computes the right answer; it does *not* catch
the error that matters — a 64-bit value misassigned to a 32-bit name, where plain `add` is legal
under both rules and truncates silently either way.

### Q4 — Does the NMFC calling convention renumber arguments?

**The question.** Under the stock RV64 ABI read through §3.2, argument 0 is `a0` = `x10` = `d2` =
**lane 2**. A custom back end could renumber so argument 0 lands in `d0` = lane 0.

**A new argument for renumbering, from §4.4.** The stock mapping **collides with itself**:
arguments 7 and 8 are `a6`/`a7` = `w0`/`w1`, and `w0 ∪ w1` **is** `d0` **is** `s0`, the register
the same ABI offers as scratch. An allocator that treats `s0` as free clobbers two arguments, and
§9.5's error class means nothing catches it. Renumbering so that arguments occupy `d0`, `d1`, …
in order dissolves the overlap by making the argument region contiguous from lane 0.

**Why it needs you.** It is pure convention — it changes only host staging code and the published
per-function ABI, and nothing in the architecture. But it must be decided **once**, together with
**Q7's return-value convention**, before host code and the back end are written to different
assumptions. **Recommendation: keep the stock ABI mapping** (argument *k* → lane *k*+2) for as long
as the day-one `-ffixed` path is in use, **with §4.4's corrected rule published alongside it** —
lane 0 is scratch only for a function taking six or fewer integer arguments — and renumber when the
custom back end lands.

### Q5 — Ratify the tier-1 supersession of CANON.md:9819

**The question.** CANON.md:9819 states, verbatim: *"**The namespaces do not alias**: … the compiler
binds every simultaneously-live `f`- or `x`-name to a **disjoint bit range**, so `f3` and `x3` are
different names at different offsets, not one slot."* §3.2 makes `f`_n_ ≡ `x`_n_ — the same bits, the
ratified Zfinx rule — and therefore contradicts that clause.

**Why it needs you.** It is a tier-1 clause, and the reason it can be superseded is itself a tier-1
ruling: the sentence describes *the compiler doing the binding*, and the 2026-09-03 ruling took the
binding away from the compiler. **The first half of the sentence — 512 bits of live storage, not 64
architectural slots — is preserved and strengthened**, and identity aliasing makes K.6's "third wrong
answer" structurally unrepresentable. This needs a ruling and a ledger entry of the L45 shape, marked
rather than quietly dropped (cons-C31). The same ledger entry should carry DESIGN §25.7 D:2560-2567's
supersession (§5.1).

**Note what is NOT being asked.** Both sibling proposals asked you to amend ruling **O4** and rename
the subset to `RV64IMA_Zfinx_Zdinx`. §3.7 shows the rename is unnecessary and mildly dangerous — an
implementer reading "Zfinx" would implement §26.1's narrow-result sign-extension, which under a
tiling would clobber a neighbour. **`RV64IMAFD`'s opcode list stands unamended, and no instruction
is deleted.** Its *semantics* deviate in ten places (§8.4), and Q5's ledger entry should say so —
declining the rename does not make the design `F`/`D`-conformant.

### Q6 — The namespace fork: does `f`_n_ ≡ `x`_n_, or do the two namespaces name different bits?

**[NEW — this is the question `register-map-facts.md` §6.1 says is the real one, and the previous
draft did not ask it. It presented `f`_n_ ≡ `x`_n_ as "forced rather than chosen" in §3.2, three
sections after §1.3 had proved an impossibility on the 63-name budget that only exists if the two
namespaces name *different* bits.]**

**The question.** facts §6.1, verbatim: *"under Zfinx there is only one namespace, so the `f`
namespace's 32 extra names (C17) are gone. If the design wants 63 names it must keep F/D encodings
and accept that it has forked the ISA; if it wants Zfinx's clean semantics it has 31 names for 512
bits. **That choice has not been made and it is the real fork in option 2.**"* And facts §6.2:
*"What the second namespace actually buys is **+32 names** — i.e. finer slicing of the 512 bits —
and that is the argument that should be made for keeping it."*

| | **(a) `f`_n_ ≡ `x`_n_ — recommended** | **(b) the `f` namespace names different bits** |
|---|---|---|
| names available | 31 | 63 |
| tiers reachable | 64 and 32, both complete (24 names) | 64, 32 **and 16**, all complete (56 names, §1.3b) |
| allocation | one pool; K.6's "third wrong answer" is unrepresentable | two pools; the allocator must not double-count, and K.6 warns about exactly that failure |
| 16-bit arithmetic | n/a | **does not exist in `RV64IMAFD`** — the tier would be load/store/move only unless O4 is amended to add `Zfh` or a 16-bit integer tier |
| `fmv.*` | free aliases, architecturally no-ops on identical names | real 32/64-bit moves between disjoint tiles — genuinely useful |
| ISA relationship | not implementing `F`/`D` (§8.4 dev. 1), Zfinx-shaped | not implementing `F`/`D` either, and further from any ratified shape |
| admission | one bit budget | one bit budget, but the sub-32-bit tier needs its own clause |

**Recommendation: (a)**, because the 16-bit tier that (b) buys has no arithmetic to run on it
under ruling O4, and because a single pool removes an error class the canon explicitly names.
**But it is a choice, it costs 32 names, and it should be ruled rather than assumed.** If you
want (b), it is a strictly larger change: O4 must be amended for 16-bit arithmetic to be worth
having, §3.8's admission test gains a clause, and the scoreboard grows.

### Q7 — The return-value convention, and how a body terminates

**[NEW — the previous draft had neither. Both block the host caller and the back end.]**

**Two rulings, and they must be made together with Q4 because a back end cannot be written
against half a convention.**

**(a) Where does a result live?** §4.5 recommends `d2` (= `a0` read through §3.2) for one 64-bit
result or one `f64`, `w4` for one 32-bit result, `d2`/`d3` for a pair — the stock RV64 ABI's own
answer, so a stock body needs no change, and it never collides with §4.4's seventh/eighth-argument
case. I2 is untouched either way: all 512 bits come back and the architecture never inspects them.

**(b) How does a body end?** `ret` is `jalr x0, 0(x1)`, and §3.5 rule 2 makes `x1` illegal, so
**every stock-compiled body's last instruction traps** (§3.6). A body ends with `END`/`RETC`.
The ruling needed is *where the rewrite happens*: **in `annotate`** (recommended — it already
walks the body and already rejects functions, so it can also reject a body whose `ret` is not the
sole terminator), or in a separate linker/objcopy pass. The core must **not** treat a trailing
`ret` as an implicit end-of-body; guessing is the silent behaviour cons-C15 forbids.

### Q8 — The admission tool has no working width input on a RISC-V target

**[NEW — a tooling blocker, escalated because it gates whether a function may run at all.]**

**The question.** §5.2 step 1 needs each live value's width. `annotate.cc:461-470`'s `width_of`
parses **x86-64 register names** (`rax`/`eax`/`al`, `r8d`, `zmm`), and the ruled target is RISC-V
(CANON.md R11), whose register names carry no width at all (fact-C12). On a RISC-V trace every
value falls through to 64 bits. The three candidate replacements are tabulated in §5.2: the
**defining opcode** (`lw` vs `ld`, `addw` vs `add`), **front-end type information**, or **the
register class the back end assigned**.

**Recommendation: the opcode table now, the register class once §3.10's back end exists** — and
until one of them lands, `annotate` should charge every value 64 bits **and say so**, because a
conservative gate is sound and a silent fall-through that looks like a measurement is not. **This
also means the two measured decompositions in §3.8's table (`nmfc_bu` a=7 b=1, `nmfc_expand` a=4
b=4) are not currently reproducible from the tool**, and should be re-measured once a width source
exists.

### Q9 — Scoreboard granularity on the ChampSim core model: 16 bits and 73 B, or 8 bits and a false dependency?

**[NEW — the previous draft recommended lane granularity partly *because* it "preserves
`scoreboard[≤8]` verbatim", which is the 8-register formulation being used as a constraint.]**

**The question, and it only arises on the ChampSim core model** — on the canon core the
scoreboard is one pending destination and there is nothing to decide (H.4, §5.3).

- **16 ready bits at `W` granularity** — exact, no false stalls, +8 bits per context (+1 KiB per
  tile at C = 1024, 1.5 % of context state) and **+1 byte in the migration envelope**, which
  needs cons-C6's 72 B re-ratified.
- **8 ready bits at lane granularity** — zero added state, zero added bytes, but **a load into
  `w0` stalls a read of `w1`**: the false dependency is precisely between the two halves of a
  lane, which is the case bit-packing exists to create.

**Recommendation: 16 bits, unless holding cons-C6 at exactly 72 B matters more than the stall.**

---

## APPENDIX — THE MAP ON ONE PAGE

```
bit  0                                                                                          511
     |-----d0----|-----d1----|-----d2----|-----d3----|-----d4----|-----d5----|-----d6----|-----d7----|
     |  w0 |  w1 |  w2 |  w3 |  w4 |  w5 |  w6 |  w7 |  w8 |  w9 | w10 | w11 | w12 | w13 | w14 | w15 |
     |lane0|     |lane1|     |lane2|     |lane3|     |lane4|     |lane5|     |lane6|     |lane7|

 THE HEAP RULE:  the halves of x_n are x_2n (low) and x_2n+1 (high);  x8..x15 are the 64-bit tiles.

 x0  / f0        zero at any width  /  +0.0 ; writes discarded ; costs none of the 512
                 (f0 as a zero is a DEVIATION -- ratified F/D's f0 = ft0 is general)
 x1  - x7        RESERVED, ILLEGAL  -- tree nodes wider than 64 bits (see open question 2)
                 this makes `ret` = jalr x0,0(x1) ILLEGAL: a body ends with END/RETC (§3.6, q7)
 x8  - x15  =    d0 - d7   (64 bits)      d_k = bits [64k, 64k+64)   = CXW/CXR lane k
 x16 - x31  =    w0 - w15  (32 bits)      w_m = bits [32m, 32m+32)   = half of d_(m>>1)
 f_n        =    x_n       (same bits, Zfinx's shape) ; f8-f15 are .d, f16-f31 are .s
                 this is a CHOICE costing 32 names, not a forced move -- open question 6
 d/w are DOCUMENTATION-ONLY names; the assembler accepts only x8..x31 (§8.4)

 DECODE:   width = n[4] ? 32 : 64      offset = n[4] ? (n&15)<<5 : (n&7)<<6      legal = n[4]|n[3]

 EXECUTION (widths come from the OPERAND'S ROLE, not from the destination alone -- §3.4):
   W1   address/base operand ........ always 64, and must be a d name
        load/store data operand ..... the opcode's width
        any FP operand .............. the width the mnemonic names (.s=32, .d=64); name must match
        branch sources .............. always 64
        integer ALU/shift/M ......... W1b: 32 if the opcode is a *W form, else the WIDEST
                                      register operand (destination or source); x0 does not count
   W2   read each source from its own name's bits; integer sources sign-extend up / truncate down
        to the operand width; FP sources are read RAW -- no extension, no NaN-box check
   W3   write EXACTLY the destination name's bits, never a neighbour; where the execution width
        is narrower than the destination (only *W and narrow loads), fill it as ratified RV64 does

 LEGALITY (seven rules, §3.5):
   1a  every address/base operand -> a d name        1b  the per-operand-class table (total)
   1c  sc.w/sc.d's rd is a 0/1 flag, any width       2   x1-x7 / f1-f7 illegal as any operand
   3   jal/jalr with rd != x0 illegal                4   shamt[5]=1 reserved at 32-bit width
   5   RV64-only opcodes illegal at a narrow name    6   anything outside RV64IMAFD
   7   catch-all: a w name where 64 is required
   auipc -> d      rm=DYN means RNE (a HOST/TILE DIVERGENCE, §9.12)
   addw/sllw/mulw/... legal on d (RV64I verbatim) and on w (zero-width extension field)

 ADMISSIBLE  <=>  every opcode in RV64IMAFD, the seven legality rules hold,
                  peak( live bits + scratch bits ) <= 512, each value at its OWN width,
                  one spare name free for staging        [user ruling 2026-09-03 (liveness)]
                  -- K.6 verbatim in bits; nothing is charged 32; no cap on live values
                  AND a verified non-overlapping placement exists over the live ranges
                      (a value with no name of its width is placed PACKED inside a wider
                       name and reached by shift-and-mask: ~2-3 extra ops per access)

 nmfc_bu      a=7 b=1  -> 480 / 512   admissible, one w tile spare   (re-measure: q8)
 nmfc_expand  a=4 b=4  -> 384 / 512   admissible, 128 bits spare     (re-measure: q8)
```
