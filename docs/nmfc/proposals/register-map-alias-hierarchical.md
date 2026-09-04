# PROPOSAL — `alias-hierarchical`: a fixed, nested, ISA-defined name table over the 512 bits

**Status: PROPOSAL. NOT CANON. Nothing here may be cited as a decision until ruled.**

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

**What it answers.** Given a context of **512 bits, bit-packed** (I2, I.0's 512-bit rule),
what does the 5-bit register field of an ordinary `RV64IMAFD` instruction mean, when the
answer must come from **the instruction alone** (hard constraint C1/C2, user ruling
2026-09-03)?

**The shape of the answer.** The ISA defines **one fixed table of 32 register names to bit
ranges**, nested x86-style: 64-bit names, 32-bit names inside them, 16-bit names inside
those, 8-bit names inside those. The table is the same in every decoder on every tile,
identical for the `x` and `f` namespaces, and is **never stored anywhere** — it is a wire
pattern, not a memory. The register number gives **offset and width**; the opcode gives
**type and operation width**; the two compose rather than conflict.

**Prerequisites.** `register-map-facts.md` (claims C1–C24, findings 6.1–6.3) and
`register-map-context.md` (mechanisms M1–M15, constraints C1–C32). This document uses
those identifiers throughout. Where it departs from the facts file's recommendation it
says so and says why.

---

## §1 THE MAP

### 1.1 The table

One table, 32 entries, indexed by the 5-bit register field. **Identical for `x` and `f`:
`f<n>` and `x<n>` are the same bits.** Bit 0 is the least significant bit of the context.

| name | width | bits (global) | 64-bit lane | bits within lane | parent |
|---|---|---|---|---|---|
| `x0` / `f0` | — | — | — | — | **reads zero, writes discarded** |
| `x1` / `f1` | — | — | — | — | **reserved — illegal instruction** |
| `x2` / `f2` | 16 | `[495:480]` | 7 | `[47:32]` | `x31` |
| `x3` / `f3` | 16 | `[511:496]` | 7 | `[63:48]` | `x31` |
| `x4` / `f4` | 8 | `[487:480]` | 7 | `[39:32]` | `x2` |
| `x5` / `f5` | 8 | `[495:488]` | 7 | `[47:40]` | `x2` |
| `x6` / `f6` | 8 | `[503:496]` | 7 | `[55:48]` | `x3` |
| `x7` / `f7` | 8 | `[511:504]` | 7 | `[63:56]` | `x3` |
| `x8`–`x15` / `f8`–`f15` | **64** | `[64k+63 : 64k]`, `k = n−8` | `k` | `[63:0]` | — (roots) |
| `x16`–`x31` / `f16`–`f31` | **32** | `[32m+31 : 32m]`, `m = n−16` | `m>>1` | `[31:0]` or `[63:32]` | `x(8 + (m>>1))` |

**Composition, verified:** `x8..x15` tile the 512 bits exactly; `x16..x31` tile the 512
bits exactly; `x16 ∪ x17 = x8`, …, `x30 ∪ x31 = x15`; `x2 ∪ x3 = x31`; `x4 ∪ x5 = x2`;
`x6 ∪ x7 = x3`. Every slice is **naturally aligned** (offset is a multiple of its width)
and **no slice straddles a 64-bit word** — every narrow name lies inside lane 7.

**The forest.** Eight buddy trees, one per 64-bit region. Regions 0–6 have depth 2
(64 → two 32s). Region 7 has depth 4 (64 → two 32s; the upper 32 → two 16s → four 8s).
Two names conflict **iff one is an ancestor of the other** — the interference structure is
a forest, not an arbitrary graph. §7 shows this is what keeps admission a sum of bits.

**No slice has two names.** The map is injective: 30 names denote 30 distinct slices,
`x0` denotes zero, `x1` denotes nothing. Aliasing here means *nesting*, never *synonymy*.

### 1.2 Decode

```
  n[4]      = 1   ->  width 32,  offset = (n & 15) << 5
  n[4:3]    = 01  ->  width 64,  offset = (n &  7) << 6
  n[4:2]    = 001 ->  width  8,  offset = 480 + ((n & 3) << 3)
  n[4:1]    = 0001->  width 16,  offset = 480 + ((n & 1) << 4)
  n = 1                          illegal instruction
  n = 0                          zero / discard
```

A 3-input priority encode on `n[4:2]` selects one of four constant shift amounts; the
offset is the register field shifted by that constant. **There is no table lookup, no ROM,
and no serial memory access on the decode path.** The five intra-word offsets actually
generated are `{0, 32, 40, 48, 56}`, so the extract is a 5-way mux after the 8-way lane
mux, not a barrel shifter.

Contrast M1/M2, the mechanism this replaces: `layout_.field[r]` is an indexed read of a
32 × 24-bit array whose *result* selects the mux — a memory access in series with the
register read. (**Stated as a consequence, not as a reason. C32: the user's stated reason
for rejecting M1 is the third referenced object, not latency. This paragraph is an
observed effect of the replacement and is not offered as an argument for it.**)

### 1.3 Why this mix — the four decisions

**(a) The top level is 8 × 64, complete.** 64 is the widest datum in `RV64IMAFD` (`Q` is
outside the subset, C10), so nothing above 64 can be computed on. And the coverage must be
complete because **`nmfc_bu` holds seven live 64-bit values** (§1.4), so at least seven
disjoint 64-bit names must exist; eight is the only tiling.

**(b) The 32-bit level is complete — 16 names, not 8.** This is the single most expensive
decision (16 of 31 names) and the one most worth defending. The cheap alternative — one
32-bit name per 64-bit region, aliasing the low half — is exactly x86's defect (facts
C19): the upper half of every 64-bit region becomes unnameable at 32 bits, so **256 of the
512 bits could never hold a 32-bit value**, and a function holding sixteen live 32-bit
values (512 bits, which K.6 admits) would be inexpressible. It is also the level where the
`[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **"could never hold" is wrong.** No bit
of the 512 is ever out of reach: a value narrower than a name is packed inside one and
reached by shift-and-mask through a scratch name, so an incomplete 32-bit level would cost
**instructions** (about 2-3 extra ops per access to a value living in an unnameable half),
never capacity, and sixteen live 32-bit values remain expressible under either alternative.
The argument for completeness survives intact on that corrected basis — it is an
instruction-count argument, and a strong one. It is also where the
record's only *measured* payoff appeared: DESIGN §22's headroom came from narrowing a
`NodeID` from 64 to 32, and both admissible functions are pure 64/32 mixes. Buying
completeness here costs eight names and buys the case the evidence points at.

**(c) The narrow level is deliberately shallow and concentrated — one 32-bit region,
subdivided completely.** Seven names remain after (a) and (b). Spending six of them on a
**complete** buddy subtree of `x31` (two 16s, four 8s) is chosen over the alternative
"four 16s over a whole 64-bit region plus two 8s", which covers twice the territory but is
**incomplete**: three live bytes and one short would sum to 40 bits inside a 64-bit region
and still not be placeable, because only one of the four 16s is split. Completeness is
what preserves the buddy-allocation property, and therefore what keeps the admission test
a sum of bits instead of a packing search (§7). Bytes are also the commoner narrow datum
than shorts — DESIGN §22's "byte-per-vertex frontier" is the record's own example — so
four byte names beat four short names at equal cost.

This is x86's concentration discipline (only four of sixteen x86 registers ever had
`AH`-class names) applied deliberately rather than inherited by accident.

**(d) The narrow region sits at the top, and `x8..x15` are the 64-bit names.** Two
consequences fall out and both are wanted:

- **The RV64 ABI's argument registers `a0`–`a5` are `x10`–`x15`, all 64-bit names.** Both
  measured functions take three arguments or fewer, and a pointer needs 64 bits. `a6`/`a7`
  (`x16`/`x17`) land as 32-bit names, which is the right default for a fourth-and-later
  integer argument in a bit-packed world.
- **`x8`–`x15` is exactly the register set the compressed encodings can reach** (facts
  C17: "CIW, CL, CS, CA, and CB … correspond to registers `x8` to `x15`"). K.6 excludes
  RVC today, so this is a note for the record, not a present claim — but it costs nothing
  to put the widest names where a future `Zca` would want them.

Putting the narrow subtree in lane 7 rather than lane 0 keeps lanes 0–6 structurally
uniform: a function that never uses a sub-32-bit name never has to reason about the
irregularity, and the host stages its arguments into the low lanes in order.

**(e) `x1` is reserved, and this is load-bearing.** Heap position 1 over region `x31`
would denote the 32-bit region that `x31` already names; the map does not give one slice
two names, so the position is reserved instead. Reserving `x1` has a consequence that is
worth more than the name: `x1` is `ra`, so **`jal ra, …` and `ret` (= `jalr x0, x1, 0`)
are illegal instructions**, while `j` (= `jal x0, imm`) and every conditional branch remain
legal. Invariant 7's "no call from inside an offloaded function" (C9) stops being a
compiler convention and becomes a decode check. §4.6 completes this with the address-operand
rule, which does the same for spills.

### 1.4 The measured functions fit

DESIGN §22 reports `nmfc_bu` at **8 values, 480 bits** and `nmfc_expand` at **8 values,
384 bits**, in a world of 64-bit pointers and 32-bit `NodeID`s. With eight values and only
those two widths the mixes are uniquely determined:

| function | mix | names | check |
|---|---|---|---|
| `nmfc_bu` | 7 × 64 + 1 × 32 = 480 | `x8`–`x14`, and one of `x30`/`x31` | ✅ 480 ≤ 512 |
| `nmfc_expand` | 4 × 64 + 4 × 32 = 384 | `x8`–`x11`, `x24`–`x27` | ✅ 384 ≤ 512 |

C21 is satisfied with headroom in both. (The mixes are *derived* from the reported totals,
not quoted from the record; the record states the totals only.)

---

## §2 THE ONE SEMANTIC RULE

> **The map changes where a value is stored. It does not change how anything is computed.**
> Every `RV64IMAFD` instruction means exactly what the ratified manual says it means,
> applied to operands read out of their names and results written into their names.

Two port rules make that true:

- **READ.** A source name narrower than XLEN/FLEN is **sign-extended** to the operation's
  width. A source name of the operation's width is read as-is.
- **WRITE.** A result wider than the destination name is **truncated** to the name's width.
  A result narrower than the name is sign-extended to it. **A write touches only the bits
  of the name it names, and no others.**

Everything else in §4 is a consequence of these two lines plus the floating-point
exception in §4.4.

**Why sign-extension on read is the right rule, and what it buys.** Facts C13 records the
RV64 design note: "The compiler and calling convention maintain an invariant that **all
32-bit values are held in a sign-extended format in 64-bit registers.** Even 32-bit
unsigned integers extend bit 31 into bits 63 through 32." Facts §6.3 concludes that
bit-packing and that invariant are "in direct opposition", because RV64 spends bits 63:32
to make a 32-bit value canonical while NMFC wants those bits for a different value.

**They are not in opposition; they are in different places.** Sign-extend-on-read
maintains the invariant **at the read port instead of in the register file**. The ALU sees
exactly the bits a stock RV64 core would have presented to it, and the storage costs 32
bits instead of 64. Every compiler back-end assumption about sign-extended 32-bit operands
survives unchanged; only the register allocator changes. This is Zfinx's narrow-value rule
(facts C11: "fill bits XLEN-1:w with copies of bit w-1") *inverted* — Zfinx pays the bits
to store the extension, this map performs it on the way out — and it is the reason facts
§6.3's cost does not have to be paid.

**Why writes must not zero or preserve.** Facts §6.3 recommends AArch64's *zero* over
x86's *preserve*, to avoid x86's partial-register merge hazard (facts C19). **Neither is
available here, and both would be catastrophic.** In AArch64, the bits above `Wn` are not
an architectural name — zeroing them destroys nothing. Here they are `x17`, holding
another live value. A write must be confined to its own slice, and this is not a third
option in x86's and AArch64's sense: it follows from the slices being **names** rather
than **views**. The x86 hazard does not follow it into this design either — the merge
hazard is a *rename* artefact, and C13 forbids rename; physically every slice is
byte-aligned (§1.1), so the file needs only byte write-enables, which every SRAM has.

---

## §3 ENCODING AND FORMAT

**This proposal adds no instruction, consumes no encoding space, and assigns no field
value.**

- The 5-bit `rd`/`rs1`/`rs2`/`rs3` fields are unchanged in position and width. Only their
  *interpretation inside the function core* changes (M2).
- The NMFC instruction set stays at **twelve user-level plus privileged `RESUME`** (C22).
- `funct7` groups `0x6` and `0x7` remain free for `KILL`, mailboxes and `RESUME` (C23).
- Nothing here is a canonical field value (C24); the map is a *semantics* statement, and
  the only numbers in it are bit offsets inside the context, not encoding bits.
- **C26 is respected**: no instruction carries a runtime `(offset, width)` operand. The
  offset and width are consequences of a register number that was already in the
  instruction.

**Scope.** The map governs **the function core's decode of the instruction stream it
fetches**, and nothing else. The host is a stock RV64 core with 32 full-width `x`
registers and a real `f` file; `CXW`/`CXR` move 64-bit lanes between them and the context
(M4, §6). A context register is still named by a **number in a GPR** on the host side
(C25, M12) — this proposal does not touch that path.

---

## §4 DECODE AND EXECUTION

### 4.1 Integer operations — the suffix stays meaningful

`add x16, x17, x18` (three 32-bit names): sources sign-extended to 64, 64-bit add, result
truncated to 32. That is correct modular 32-bit arithmetic. So is `addw x16, x17, x18` —
`addw`'s sign-extension of its 32-bit result is discarded by the truncation. **On a 32-bit
destination `add` and `addw` are indistinguishable.**

`add x8, x16, x9` (64-bit destination, one 32-bit source): `x16` arrives sign-extended,
which is exactly `base + (int32)index` — the dominant pattern in `nmfc_bu`. Stock
behaviour, stock result.

`srliw x16, x17, 3`: `srliw` shifts the **low 32 bits** logically, so it is still needed
and still means what it means; plain `srli` on a sign-extended source would shift the
extension in. **The `*W` opcodes are required exactly where stock RV64 requires them**, and
for the same reason. This resolves facts C18 without taking any of its three branches:

> C18 assumed the width is "encoded twice" and one encoding must win. It is not encoded
> twice. **The opcode's width governs the computation; the name's width governs the
> storage.** They are orthogonal, they compose, and no legality check between them is
> needed on the integer side.

**Signed and unsigned both work, provided both operands share a width.** Sign-extension
is order-preserving for unsigned comparison of equal-width values (the low half maps to
itself, the high half maps above it), which is the same reason RV64's own sign-extension
invariant is safe for `sltu`/`bltu` on unsigned 32-bit values. `bltu x16, x17` is correct.
**Mixing widths in a comparison is the compiler's problem**, exactly as mixing an `lw`
result with an `lwu` result is on a stock core (§8.4).

`M` follows the same rules. `mulh`/`mulhu`/`mulhsu` produce the high 64 bits of a 128-bit
product; into a narrower name they truncate, which is defined and almost certainly not what
the compiler wanted — see §12.6.

### 4.2 Loads and stores

The load's own extension happens first, then the write rule applies. Into a 64-bit name,
every load behaves exactly as ratified. Into a narrower name:

- `lw` and `lwu` into a 32-bit name are **indistinguishable** — the extension each performs
  lies outside the destination name and is truncated away.
- `lb` and `lbu` into an 8-bit name are likewise indistinguishable.
- The *signedness* of a narrow value is therefore not a property of how it was loaded; it
  is a property of how it is read back (§2: sign-extended), and an unsigned narrow value
  costs one `andi`/`slli`+`srli` per use that needs the zero-extended form. §12.7.

`sb`/`sh`/`sw`/`sd` store the low bits of the sign-extended source, as ratified.

### 4.3 `x0` and `f0`

`x0` reads zero and writes are discarded, at every width, in **both** namespaces (M5, C7).
Because `f0` is `x0`, **`f0` reads as `+0.0` in every IEEE format** — the all-zero pattern
is positive zero at 64, 32 and 16 bits — which makes a free zero constant available to
`fadd`, `fsgnj`, `fmin`/`fmax` and comparison idioms. The cost is F/D's general-purpose
`f0`; a compiler targeting the function core must treat it as hardwired.

### 4.4 Floating point — width equality, and no NaN-boxing

**Rule: for a floating-point operand, the opcode's format width must EQUAL the name's
width. A mismatch is an illegal instruction.** So:

| opcode format | legal names |
|---|---|
| `.d` (f64) | `x8`–`x15` only |
| `.s` (f32) | `x16`–`x31` only |
| `.h` (f16) | **no opcode exists** — `Zfh` is outside `RV64IMAFD` (C10) |
| 8-bit float | no such format in RISC-V |

**Why floating point is stricter than integer.** Two's complement is closed under
truncation and sign-extension, so §2's rules give an integer operation a *correct* answer
at any width. IEEE-754 is not: truncating an `f64` to 32 bits does not produce an `f32`,
it produces a bit pattern. The asymmetry is forced by the number systems, not chosen. This
is Zdinx's shape (facts C16): a name/width mismatch is **reserved**, not silently
reinterpreted.

**Three things fall out, and all three are wanted.**

1. **NaN-boxing disappears entirely, and so does its hazard.** An `f32` only ever lives in
   a 32-bit name, where all of its bits are the value; there are no upper bits to box.
   Facts C4's *read* half — an incorrectly-boxed narrow float is silently read as canonical
   NaN, not trapped — is the most dangerous rule in this area for any scheme that lets
   integer code and float code touch the same bits, and **this map makes it unreachable**.
   Facts C11 requires a stated substitute for the dropped boxing rule; the substitute is
   width equality, and it is stronger than Zfinx's ignore-on-read.
2. **`fcvt.*` becomes the only way to change floating-point width — which is what it
   already is.** `fcvt.d.s f8, f16` is well-formed: source `.s` on a 32-bit name,
   destination `.d` on a 64-bit name. `fcvt.s.d f16, f8` likewise. There is no other path
   between formats and none is needed.
3. **The int↔float conversions split cleanly.** `fcvt.w.s`, `fcvt.l.d`, `fcvt.s.w`,
   `fcvt.d.l` and friends have one floating-point side and one integer side: the floating
   side obeys width equality above, the integer side obeys §2's truncate/sign-extend rules.
   `feq`/`flt`/`fle` write 0/1 to an integer name of any width. `fclass` writes a 10-bit
   mask and therefore needs a name of 16 bits or more; into an 8-bit name it truncates.

**Rounding modes and exception flags.** C8 forbids `fcsr`. Every RISC-V FP instruction
already carries a 3-bit `rm` field, so the five **static** rounding modes (`RNE`, `RTZ`,
`RDN`, `RUP`, `RMM`) cost nothing and are available. **`rm = DYN` (`0b111`) is an illegal
instruction** — it names `fcsr.frm`, which does not exist. Accrued exception flags
(`fflags`) have nowhere to be recorded and are **discarded**; a function that needs to
observe them is in C16's position and cannot be offloaded. This is I.7 item 3 made
mechanical.

### 4.5 `fmv.*` and the FP load/store forms

Facts C15 shows why "`f<n>`/`x<n>` alias, so `fmv.*` is a free alias" is the wrong
statement: `fmv.x.w` sign-extends bits 31:0 over 63:32 and `fmv.w.x` NaN-boxes bits 63:32,
and under a naive aliasing scheme each of those **silently overwrites a neighbouring packed
field**.

**Under this map they cannot.** `fmv.w.x f16, x17` names a 32-bit destination; the boxing
bits it would write lie outside `x16` and are discarded by the write rule. `fmv.x.w x16,
f17` likewise. And `fmv.w.x f8, x9` — a `.w` opcode on a 64-bit name — is **illegal** under
§4.4. The width-equality rule neuters both hazards without a special case.

That leaves the four `fmv.*` and the four FP load/store forms as **legal but redundant**:
`addi rd, rs, 0` copies at every width under §2's rules, and `ld`/`lw`/`sd`/`sw` move the
same bits into the same names as `fld`/`flw`/`fsd`/`fsw`. Ratified `Zfinx`/`Zdinx` handles
exactly this by **removing** them ("The Zfinx extension adds all of the instructions that
the F extension adds, *except* for the transfer instructions `flw`, `fsw`, `fmv.w.x`,
`fmv.x.w` …"; Zdinx likewise drops `fld`, `fsd`, `fmv.d.x`, `fmv.x.d`).

**This proposal does not decide it, because C10 rules the subset and that is a tier-1
ruling.** Both readings are compatible with everything above:

- *Literal `RV64IMAFD` (C10 as written):* the eight instructions stay, with the semantics
  above. They are redundant and harmless. Nothing else changes.
- *Facts §6.1's naming correction accepted:* the subset is written
  `RV64IMA_Zfinx_Zdinx`, the eight are removed, and eight encodings are recovered. **No
  function becomes inexpressible** — every one of the eight has a same-bits integer
  equivalent, so the change is a peephole rewrite, not a loss of expressiveness (C16 is
  not even engaged).

**This is a question for the user**, and it is the one place this proposal touches a
ruling. It is raised in §13.

### 4.6 Address operands must be 64-bit names

**Rule: `rs1` of every load, store and AMO must be one of `x8`–`x15`. Otherwise the
instruction is illegal.**

An address is 64 bits. A narrower name sign-extends to a defined but useless value, and the
rule turns a whole class of packing errors into an illegal instruction instead of a
translation fault. It also completes §1.3(e): `x2` is `sp` in the RV64 ABI and is a 16-bit
name here, so **`sd x9, 0(sp)` — a stack spill — is an illegal instruction**. Invariant 7's
"a function that spills cannot run" (C9) becomes a decode check rather than a convention or
a fault.

Cost: no legitimate use is lost, because no legitimate address fits in fewer than 64 bits.

### 4.7 The complete list of illegal instructions this map introduces

1. Any use of `x1`/`f1` as `rd`, `rs1`, `rs2` or `rs3` (§1.3e).
2. A `.d` floating-point opcode naming a non-64-bit name, or `.s` naming a non-32-bit name
   (§4.4).
3. `rm = DYN` on any floating-point instruction (§4.4).
4. `rs1` of a load, store or AMO that is not a 64-bit name (§4.6).

All four are decidable from the instruction word alone, in the same 3-bit priority encode
that produces the offset. This is where M6/C14's hard-error behaviour is re-homed — see
§9 (M6) for what remains uncheckable and where that obligation goes instead.

---

## §5 THE TWO NAMESPACES

`f<n>` and `x<n>` denote the same bits, at the same width, with the same nesting. Three
consequences must be stated plainly, because two of them contradict things that are easy
to assume.

**5.1 The namespace carries nothing.** The user's option 2 says "utilizing a float reg
implies the type". **Under this map it does not, and it does not need to.** The opcode
already types the operation (`add` vs `fadd.s` vs `fadd.d`), and facts C9's evidence is
decisive: ratified **Zfinx** deletes the `f` registers entirely and floating-point
instructions operate on the `x` registers with the same numbers, so type demonstrably never
lived in the register name. The half of option 2 that is load-bearing is the other half —
**"the number of the reg implies the slice"** — and that is what this map implements.
Presenting the namespace as a type carrier would be a claim the ISA does not support.

**5.2 There are 31 nameable slices, not 63.** Facts C17 is exact: the `x` namespace
contributes 31 usable names (`x0` is hardwired zero and cannot be redefined without
invalidating `nop`, `j`, `ret` and the whole HINT space), and a *separate* `f` namespace
would contribute 32 more. Making them coincide gives up those 32 names. **That is the
right trade, and the reason is in K.6.** Disjoint namespaces would require the 512 bits to
be **statically partitioned** into an integer region and a floating-point region at ISA
design time — there is no other way for two disjoint name sets to tile one file. A function
with no floating point would then be unable to use the FP region, and a function that is
all floating point could not use the integer region. That is precisely K.6's **third wrong
answer**: "a slot-counting test that allocates `f`-names out of a separate pool from
`x`-names … admits a function twice that size. **The pools must be ONE pool measured in
bits**, because the register file is one file." Coinciding namespaces make one pool
structural rather than merely asserted.

**5.3 What this is, named correctly.** One register file, two encodings, no transfer
instructions needed: **this proposal is `Zfinx`/`Zdinx` with a sub-XLEN name hierarchy
added on top.** Facts §6.1 reaches the same conclusion from the other direction and shows
that I.0/I.7's "two namespaces over one packed file" and O4's `RV64IMAFD` cannot both be
literally true, because F/D's `f0`–`f31` is architecturally a second file. Saying so gives
the design a ratified specification for exactly the semantics it wants (facts C11's narrow
rules, facts C16's mismatch-is-reserved shape) and costs it nothing. §13 raises it.

---

## §6 HOST-SIDE RETRIEVAL

**C11 is absolute: there is no vector unit on either host.** `vmv.x.s`, `vslidedown` and
`vfmv.f.s` do not exist here and appear nowhere below. The path is I.8's:
**`CXR` to a GPR, then RV64I shifts, then `fmv.d.x`/`fmv.w.x` if the value is wanted in a
host `f` register.**

**What the fixed map changes for the host: the offsets are compile-time constants known to
the ISA, not to a per-function table.** Under M1 the host had to know the callee's layout
to stage an argument or interpret a result — the rejected "third piece of memory" existed
on the host side too. Under this map the recipes below are the whole story, forever.

### 6.1 Reading a name out of a returned context

| name | recipe (signed) | recipe (unsigned) |
|---|---|---|
| `x8+k` (64) | `CXR rd, cS, k` | same |
| `x16+m` (32), `m` even | `CXR rd, cS, m>>1` ; `sext.w rd, rd` | `CXR` ; `slli rd,rd,32` ; `srli rd,rd,32` |
| `x16+m` (32), `m` odd | `CXR rd, cS, m>>1` ; `srai rd, rd, 32` | `CXR` ; `srli rd, rd, 32` |
| `x2` (16) | `CXR rd, cS, 7` ; `slli rd,rd,16` ; `srai rd,rd,48` | `slli 16` ; `srli 48` |
| `x3` (16) | `CXR rd, cS, 7` ; `srai rd, rd, 48` | `srli rd, rd, 48` |
| `x4` (8) | `CXR rd, cS, 7` ; `slli 24` ; `srai 56` | `slli 24` ; `srli 56` |
| `x5` (8) | `CXR` ; `slli 16` ; `srai 56` | `slli 16` ; `srli 56` |
| `x6` (8) | `CXR` ; `slli 8` ; `srai 56` | `slli 8` ; `srli 56` |
| `x7` (8) | `CXR` ; `srai rd, rd, 56` | `srli rd, rd, 56` |

One `CXR` plus at most two RV64I shifts, for every name. `sext.w` is the RV64I
pseudo-instruction `addiw rd, rs, 0`; `srai` with a shift amount of 32–56 is RV64I's 6-bit
`shamt`. This is C27 and user #233 exactly — "**Regular bit manipulation can take you the
rest of the way**" — and it needs no new instruction, which is why C26 stays closed.

### 6.2 Getting a value into a host floating-point register

The host is a **stock RV64GC core** with a real `f` file. The aliasing in §5 is a property
of the *function core*, not of the host, so the host's `fmv.*` behave exactly as ratified
and are exactly what the user's original question asked for:

- `fmv.d.x fa0, rd` — moves 64 raw bits into a host `f` register, no conversion, payloads
  preserved. Requires XLEN ≥ 64, which the host is (facts C1).
- `fmv.w.x fa0, rd` — moves 32 raw bits and NaN-boxes bits 63:32. On the host that is
  correct and harmless: nothing else is packed in those bits.
- The reverse, `fmv.x.d` / `fmv.x.w`, retrieves them. Note facts C3: `fmv.x.w`
  **sign-extends** into bits 63:32 on RV64 and this cannot be suppressed — on the host it
  is a 32-bit move, not a bit-exact 64-bit one. Mask if the upper bits matter.

**So the answer to the user's host-side question is yes**, and it needs neither the V
extension nor a new instruction: `CXR` → shift → `fmv.d.x`/`fmv.w.x`.

### 6.3 The alternative memory path, and why it is not the default

Facts C8 notes the context is exactly one 64-byte cache block, so a `CXR`-per-lane into a
64-byte buffer followed by ordinary `ld`/`lw`/`lh`/`lb`/`lbu` retrieves any field at any
width with correct sign *or* zero extension and no shift sequence at all — and because all
slices are byte-aligned (§1.1), **every name has an address in that buffer**. It costs one
store and one load per field instead of one or two shifts. It is worth knowing; the shift
path is what I.8 specifies and what a compiler should emit for a single field.

### 6.4 Writing arguments in

`CXW cD, lane, rS` writes a whole 64-bit lane (M4). Composing several narrow arguments
into one lane is the usual shift-and-or in a GPR before the `CXW`; updating one narrow
field of a lane already written is `CXR`, mask, or, `CXW`. This is unchanged by the
proposal — the lane is an access granularity, not the register's structure — and §11 open
question 5 is answered: **`CXW`/`CXR` keep 64-bit lanes and are untouched.**

---

## §7 ADMISSION

### 7.1 The problem §7 of the context file raised

> "A scheme in which each name is a fixed bit-slice makes the nameable set finite and
> overlapping — so the compiler's allocation problem becomes *which names may be live
> together*, which is a **conflict-graph colouring over an interference structure the ISA
> fixes**, not a sum of widths. … **A proposal must say which one K.6 becomes.**"

**Answer: it stays a sum of widths, with one extra clause.** The reason is structural, and
it is the payoff of insisting on a *complete buddy* hierarchy in §1.3.

### 7.2 The lemma

In a **buddy system** — a hierarchy in which every subdivided block splits into two
naturally-aligned halves — allocating blocks of power-of-two sizes **in decreasing size
order** never fragments: after placing only blocks of size ≥ *w*, all of which are
naturally aligned, the free space is a disjoint union of naturally-aligned blocks each of
size ≥ *w*, so if the remaining total fits, a home exists. This is the classical property
of Knowlton's buddy allocator (1965; Knuth, *TAOCP* vol. 1, §2.5), and it is exactly why
§1.3(c) insisted the narrow subtree be **complete** rather than wider and ragged.

### 7.3 The test

> **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** *(dated 2026-09-03)*
>
> **The `S_narrow ≤ 32` clause, the round-up to a nameable width, and the whole-region
> `x31` charge are struck.** They price a value at the width of the *name* that would hold
> it, which is exactly the cap the ruling forbids. The context is 512 independent bits on a
> strictly in-order core with no renaming; a value narrower than a name is **packed** with
> others inside one name and reached by **shift-and-mask through a scratch name** (plain
> RV64I: `srli`/`slli`/`andi`/`or`). **Eight live bytes are 64 bits and are admissible**, as
> is any other width or mixture — the byte tier is **reachable**, and canon I2's *"64 1-byte
> regs, or ANY combination"* stands unnarrowed.
>
> **The correct test:**
> ```
>   max over program points of ( bits of simultaneously-live values
>                                + the scratch bits the packing needs )  <=  512
>   and at least one spare name is available for staging
>   and every opcode is in RV64IMAFD, no reserved name, no stack
> ```
> Peak liveness in **bits**, one pool, each value charged its **own** width — never rounded
> up, never charged 32 — plus the staging space the shift-and-mask sequences require. What
> the map's name shape decides is **instruction count only** (~2-3 extra ops per packed
> access), and §7.2's buddy-placement theorem below is a statement about **direct
> nameability**, not about capacity.

The superseded text follows, retained for the record. Round every live value's width **up** to the nearest of {8, 16, 32, 64}. Let

- `S_narrow` = the summed widths of values below 32 bits,
- `S_wide` = the summed widths of values of 32 bits and above.

**A function is admissible iff** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`

```
    S_narrow <= 32                                    [STRUCK - no such cap exists]
  and
    S_wide + (S_narrow > 0 ? 32 : 0) <= 512           [STRUCK - nothing is charged 32]
```

**and every opcode is in the ruled subset** (K.6's first test, unchanged).

The second clause charges the whole 32-bit region `x31` as soon as any sub-32-bit value is
live, because that region is the only sub-32 territory in the map and nothing else can use
what is left of it.

This was checked exhaustively against exact placement in the map for every mixture of
0–9 values of 64 bits, 0–19 of 32, 0–4 of 16 and 0–6 of 8: **zero disagreements.**

### 7.4 Every case the record states, checked

| case | bits | test | placement |
|---|---|---|---|
| `nmfc_bu`, 7 × 64 + 1 × 32 (C21) | 480 | **admit** | `x8`–`x14`, `x31` |
| `nmfc_expand`, 4 × 64 + 4 × 32 (C21) | 384 | **admit** | `x8`–`x11`, `x24`–`x27` |
| K.6: six pointers + two `f64` | 512 | **admit** | eight 64-bit names, one pool |
| K.6: …plus one live `f32` | 544 | **reject** | ✅ matches K.6 |
| K.6: four `f64` + four pointers | 512 | **admit** | ✅ one pool, K.6's third wrong answer cannot arise |
| L30: twelve live 32-bit values | 384 | **admit** | `x16`–`x27`; the case the current tool wrongly rejects |
| L30: eight 64-bit + one 8-bit | 520 | **reject** | ✅ and for the right reason — 520 > 512, not "nine slots" |
| sixteen live 32-bit values | 512 | **admit** | `x16`–`x31` |
| seven 64-bit + four bytes | 480 | **admit** | `x8`–`x14`, `x4`–`x7` |
| **eight live bytes** | 64 | **admit** | `[CORRECTED - user ruling 2026-09-03 (liveness)]` 64 bits of liveness plus staging space; packed into one 64-bit name and reached by shift-and-mask through a scratch name, ~2-3 ops per access. The old row read "reject, `S_narrow = 64 > 32`" and is struck along with §12.2. |

### 7.5 Why this is not R30 in mirror image

C18 warns that a scheme whose admission test is "how many names does it use" re-introduces
R30 — the rejected test that counted distinct registers touched, reported 17 and 21 where
the answer is 8, and rejected a ~480-bit function that fits.

**The test above counts no names.** It sums widths in bits, in one pool, over `f`-named and
`x`-named values alike, and it rejects only on bits. The name set never appears in it. Its
one departure from K.6's plain sum is the two roundings — up to 8 bits minimum, and the
`x31` charge — and both are *width* facts about the map, not counts of names. A function
that names twenty registers over its lifetime while holding 480 bits at its peak is
admitted, which is R30's own failing case.

### 7.6 What changes in `annotate` (M7, L30)

`tools/nmfc/annotate.cc:524–553` allocates one whole 64-bit slot per live value out of a
pool of `opt.num_regs` (8) and dies when the pool empties; the bit accounting at `:555–559`
feeds a stderr line at `:927` and gates nothing. The replacement is:

1. Delete the slot pool and the `die()` on pool exhaustion.
2. Keep the existing linear-scan liveness (a value takes a name at first definition and
   gives it back after last read; a register never read takes nothing — C19).
3. Round each live value's width up to {8, 16, 32, 64} — `reg_bits[reg]` already carries
   the width, from Pin's partial-width views (DESIGN §22).
4. `[CORRECTED - user ruling 2026-09-03 (liveness)]` Sum each live value at its **own**
   width at each program point, take the peak, add the **scratch bits the packing needs**,
   and test `peak + scratch ≤ 512` with at least one spare name for staging. (The struck
   version computed `S_narrow`/`S_wide` and applied §7.3's caps.)
5. Add K.6's **first** test, which neither implementation performs today: reject the first
   opcode outside the ruled subset.
6. Rejection stays fatal, with no truncation (C15).

The register-renaming pass at `:560–566`, which rewrites source and destination register
numbers to slot ids, becomes a rewrite to **map names**: a value of rounded width *w* gets
a free name of width *w*, chosen in decreasing-width order per §7.2.

---

## §8 THE COMPILER'S PACKING DISCIPLINE

The architecture's obligation is to not prevent the packing (DESIGN §23.6, I.8); the
packing itself is compiler work and is acknowledged unsolved (§8 of the context file). What
the fixed map changes is that the compiler now has a **concrete, finite, ISA-defined target**
instead of an open bit-allocation problem. Five rules define it.

**8.1 The overlap rule — the one that must not be broken.**
**A value must never be live in two names that overlap.** Two names overlap iff one is an
ancestor of the other in §1.1's forest — nothing else. Concretely: if `x8` is live, `x16`
and `x17` are unavailable; if `x31` is live, `x2`–`x7` and `x15` are unavailable; if `x4`
is live, `x2`, `x31` and `x15` are unavailable. Violating this silently corrupts a live
value; **nothing in the machine can detect it** (§12.5).

**8.2 Allocation is buddy allocation, in decreasing width order.**
Assign the 64-bit values first, then the 32-bit, then 16, then 8, each to any free name of
its width. §7.2 guarantees this greedy order succeeds whenever §7.3's test passes, so the
allocator needs no search and no backtracking.

**8.3 Narrowing is a deliberate act with a silent cost.**
Placing a value in a name narrower than its computed width truncates on every write, with
no signal. That is the compiler asserting the value fits. DESIGN §22's discipline — read
the generated code, not the source — is unchanged and is now more important, not less.

**8.4 Signedness discipline.**
A narrow name reads back sign-extended (§2). An unsigned narrow value therefore costs one
masking instruction (`andi rd, rs, 255` for a byte; `slli`+`srli` for 16 or 32 bits) at
each use that needs the zero-extended form. Comparisons and unsigned operations are correct
between names of **equal** width without any masking; mixing widths requires the mask. This
is the same discipline stock RV64 requires when mixing `lw` and `lwu` results, and the same
one x86 requires after a partial-register write.

**8.5 What a stock toolchain can do today — and this is the practical headline.**
DESIGN §24 step 5 identifies `-ffixed-x{n}` as the cheapest path the record has found:
constrain a stock RISC-V compiler to a register budget so that a function that does not fit
**fails to build or spills visibly**, turning admission into a build error. That path
requires the nameable set to be *register names*, which is what this map provides and what
M1's per-function bit-map did not (M13).

The **eight 64-bit names `x8`–`x15` behave exactly like stock RV64 registers** — full
width, no truncation, no extension, §2's rules are the identity on them. So:

```
  -ffixed-x0 … -ffixed-x7  -ffixed-x16 … -ffixed-x31   -fomit-frame-pointer
```

aims a **stock, unmodified** RV64 compiler at the map's 64-bit level and gets code that is
correct on the function core with no back-end work at all. That is the 8 × 64 packing —
DESIGN §22's `nmfc_expand` fits, `nmfc_bu` fits — and it is buildable today. Everything
narrower than 64 bits needs the register allocator to understand the hierarchy, which is
the unsolved half; but **the floor is now buildable, and it was not before.**

Caveats to state with it: `x8` is `s0`/`fp`, so `-fomit-frame-pointer` is required (and C9
forbids a frame pointer anyway); `a0`–`a5` are 64-bit names so up to six arguments pass
normally; `sp`, `ra`, `gp` and `tp` have no meaning here and `x1`/`x2` uses are illegal
instructions by §4.6 and §1.3(e), which is what makes a spill or a call fail loudly rather
than quietly.

---

## §9 INTERACTION WITH EVERY EXISTING MECHANISM

| # | mechanism | what happens |
|---|---|---|
| **M1** | resident-function table (`RegLayout layout_`) | **DELETED.** DESIGN §25.7 D:2560–2567 ("one small table entry beside the instruction cache … indexed by the function a context is running") is **overruled** by the 2026-09-03 ruling and must be marked as superseded, not quietly dropped (C31). `NMFCRegLayout.h`'s `RegField`/`RegLayout`, `defaultLayout()`, `hasZero`, `defines()`, `bitsUsed()` and `NMFCTile.h:448–450` become divergences of the same class as `S5`/`S6` the moment this is adopted. A new ledger entry of L45's shape records the supersession. |
| **M2** | `readReg`/`writeReg` decode indirection (`NMFCTile.cc:460–475`) | The `layout_.field[r]` lookup is replaced by §1.2's shift — a wiring change, not a table lookup. `readReg` gains the sign-extension of §2; `writeReg` gains the truncation. The `layout_.defines(r)` guard is replaced by §4.7's four illegality checks. |
| **M3** | `Context512::read`/`write` (bit-offset extract/insert over 8 × 64) | **Reusable unchanged**, with a simplification: **no nameable slice straddles a 64-bit word** (§1.1), so the `word[w0+1]` straddle branches in both functions become unreachable. Every slice is naturally aligned and byte-aligned, so a hardware file needs only byte write-enables. §11 open question 3 is answered: **no, a slice never straddles.** |
| **M4** | `CXW`/`CXR`, 64-bit lane in `funct7[3:1]` | **Unchanged.** The lane stays an access granularity and stays 64 bits; the core's names are finer. `NMFC_CX_LANE_SHIFT`/`MASK` are untouched. What improves is the *host's* side: the lane and bit offset of every name are ISA constants (§6), so the host no longer needs the callee's layout to stage an argument. §11 open question 5: answered, no change. |
| **M5** | the `x0` rule | Preserved exactly, and extended: name 0 reads zero and costs none of the 512 bits **in both namespaces**, so `f0` is `+0.0` (§4.3). `RegLayout::hasZero` disappears with the struct; the behaviour is unconditional. |
| **M6** | the illegal-register trap (`NMFCTile::illegal`) | **Re-homed, not deleted.** Under a fixed map most names are always defined, so the old check ("reads a register the function's layout does not define") loses its subject. Four runtime checks take its place (§4.7): reserved `x1`, FP width mismatch, `rm = DYN`, and a non-64-bit address operand — and each is a real packing error caught at decode. The check that **cannot** be re-homed is the overlap rule (§8.1), which is invisible at runtime and becomes an admission and compiler-correctness obligation (§7, §12.5). `NMFCTile::illegal`'s message must change: it currently reports `layout_.bitsUsed()`, which will no longer exist. |
| **M7** | the admission tool (`annotate.cc:524–559`, `:927`) | Rewritten per §7.6: peak liveness in bits, one pool, **each value at its own width**, plus the scratch bits the packing needs, plus K.6's subset test. `[CORRECTED - user ruling 2026-09-03 (liveness)]` — the round-up to nameable widths and the `S_narrow ≤ 32` clause are struck. Closes L30 in the direction K.6 already required, and fixes the two wrong answers L30 records. |
| **M8** | the `RETC`/`ENDC` return bit (`BIT_R`) | **Unchanged in hardware, and this needs care.** I2 says "register positions carry no meaning across the boundary; the join knows how to interpret what came back." That remains literally true of the *hardware*: `END`+ret moves 512 opaque bits and interprets none of them. What changes is on the *software* side — the join site's knowledge of positions now comes from the ISA rather than from the callee's layout, so the calling convention for an offloaded function is fixed once instead of per function. **The hardware knows no more than it did; the compiler on both sides knows the same thing without being told.** |
| **M9** | `JOIN` as a read-modify-write try (`cDST_new = ok ? payload : cDST_old`) | **Unaffected. Confirmed:** `JOIN` moves whole 512-bit values and never inspects a field. |
| **M10** | `CONT` / `CONT.M` | **Genuine simplification.** Under M1, a successor running a different function needed that function's layout entry to be resident on the tile. Under a fixed map a successor inherits the map for free, because there is nothing to inherit. |
| **M11** | migration = 64 B context + 8 B PC = 72 B | **Exactly 72 B, confirmed and strengthened.** The map adds no bytes because it is not data. Under M1, a migrating context implicitly required the destination tile to hold the same function's layout entry — a residency requirement, even if not a payload. That requirement disappears. |
| **M12** | the two hosts, RoCC's 128-bit operand path | **Untouched.** Every operand is a value in a GPR; a context register is named by a number in a GPR (C25). The 5-bit fields this proposal reinterprets are inside instructions the *function core* fetches, never inside a RoCC instruction the host issues. |
| **M13** | `-ffixed-x{n}` as the admission gate | **Newly viable, and this is the strongest practical argument for the proposal.** See §8.5: the eight 64-bit names are stock RV64 registers, so a stock toolchain constrained to `x8`–`x15` emits correct code today. M1's per-function bit-map could not be expressed to any compiler's `-ffixed-` flag. |
| **M14** | encoding space (`funct7` `0x6`/`0x7`) | **Untouched.** No new instruction, no new group, no field value assigned (C22, C23, C24). |
| **M15** | Appendix 2 divergences `S5`, `S6` | **`S5` closes and is replaced.** S5 records that the bit-level admission test is never exercised because nothing produces a non-default layout; with no layouts to produce, the observation has no subject. Its replacement is narrower and precise: *the tile decodes names 1–8 as eight 64-bit slices (`defaultLayout()`), where the map says the 64-bit names are 8–15 and names 1–7 are reserved or narrow* — a renumbering plus the §2 port rules plus §4.7's checks. **`S6` is unaffected** and still stands: the tile is RV64IM+A and must gain `F`/`D`, packed into the same 512 bits (§5). |

---

## §10 PRIOR ART

Every citation below is checked against the vendor or ratified specification, per the
design-review rule and §11 item 6 of the context file (which records that **nothing in
CANON-DRAFT.md or DESIGN.md mentions register aliasing at all**, so all of this is new to
the record).

**x86 / x86-64 — `RAX` ⊃ `EAX` ⊃ `AX` ⊃ `AL`.** The direct ancestor: one register file,
several architectural names at several widths, the name determining both width and offset.
Three documented defects, and this map rejects all three (facts C19):
- *Inconsistent write rules by width* — writing `EAX` zero-extends into `RAX`, writing `AX`
  or `AL` preserves. Here **one rule**: a write touches exactly its own name (§2).
- *Non-contiguous views* — `AH`/`BH`/`CH`/`DH` are bits 15:8 and exist only for four legacy
  registers. Here **every name is contiguous and naturally aligned** (§1.1).
- *Partial-register merge stalls* — preserve-on-narrow-write forces hardware to merge a
  narrow write with the old wide value, which cost Intel a class of stalls and later
  dedicated merging micro-ops. Here there is **no wide value to merge with**: the
  neighbouring bits are a different architectural name, the machine has no rename (C13),
  and byte-aligned slices need only byte write-enables.

What this map **keeps** from x86 is the thing x86 got right and AArch64 did not: **the
upper half of a wide name is itself nameable** (`x17` is not dark the way AArch64's
`Xn[63:32]` is), which is what makes complete 32-bit coverage possible at all. And it keeps
x86's *concentration* discipline — only part of the file is subdivided to the finest level
(§1.3c).

**AArch64 SIMD&FP — `Bn`/`Hn`/`Sn`/`Dn`/`Qn`.** Five architectural names for one physical
register at 8/16/32/64/128 bits: **the letter selects the width, the number selects the
register** — very nearly the user's "the number of the reg implies the slice" (facts C20).
Two differences, both deliberate here: AArch64's views are **bottom-anchored** (nested
prefixes of one register, so the upper bits have no name), and **a narrow write zeroes the
upper bits**. This map uses **disjoint buddy siblings** instead of prefixes, and a write
zeroes nothing — because in this map the upper bits are another live value, not padding.
AArch64's `Wn`/`Xn` pair follows the same zeroing rule and the same reasoning applies.

**Intel SSE/AVX/AVX-512 — `XMMn` ⊂ `YMMn` ⊂ `ZMMn`.** Bottom-anchored nesting again, and a
second independent demonstration of the same lesson: legacy SSE *preserves* the upper 128
bits and that produced the well-documented SSE/AVX transition penalty; VEX/EVEX encodings
*zero* them and do not. Two vendors, two decades apart, both concluding that
preserve-on-narrow-write is the wrong rule.

**RISC-V `Zfinx` / `Zdinx` (ratified, Ch. 26).** The most important citation, and the one
that makes this proposal a variation on ratified practice rather than an invention:
- *One file, two encodings.* "Whenever such an instruction would have accessed an `f`
  register, it instead accesses the `x` register with the same number." This **is** §5's
  structure, ratified.
- *The transfer instructions are removed, not aliased.* Zfinx drops `flw`, `fsw`,
  `fmv.w.x`, `fmv.x.w`; Zdinx drops `fld`, `fsd`, `fmv.d.x`, `fmv.x.d`. §4.5.
- *Narrow values get an explicit rule.* "Floating-point operations on *w*-bit operands
  **ignore** operand bits XLEN-1:*w*" and results "fill bits XLEN-1:*w* with **copies of
  bit *w*-1**." §2's read rule is that rule performed at the port instead of in storage.
- *And the rationale is NMFC's own:* "Recoding is less practical for Zfinx … since the same
  registers hold both floating-point and integer operands. Hence, the need for NaN boxing
  is diminished."

**RISC-V `Zdinx` aligned register pairs.** The ratified answer to "a value wider than one
name": even-numbered registers only, low-order bits in the lower-numbered register,
misaligned use **reserved**, and a specified `x0` interaction (facts C16). This map does not
need pairs — its widest name is 64 and `RV64IMAFD`'s widest datum is 64 — but it adopts
Zdinx's *shape* for the one place a mismatch can arise: **a floating-point opcode whose
format width does not equal its name's width is illegal**, exactly as an odd-numbered
register for a double-width operand is reserved (§4.4).

**MIPS-I and SPARC V8 floating-point register pairing.** `$f0` is a 32-bit single; a double
named `$f0` occupies `$f0`+`$f1`; odd numbers are illegal for doubles. Zdinx is this rule
re-ratified thirty years later, which is reasonable evidence it is the durable answer to
name-width mismatch. **IBM System/360**'s even/odd general-register pairs for 64-bit
operands are the same idea a further thirty years earlier.

**RISC-V `RV64E`.** Reduces the integer register count to 16 (`x0`–`x15`), and its stated
motivation is nearly NMFC's: microcontrollers in large SoCs, and "**to reduce context state
for highly threaded 64-bit processors**" (facts C17). Direct precedent for trading names for
per-context state — this map trades in the other direction, spending names to *shrink* the
state each value occupies.

**Buddy memory allocation — Knowlton (CACM, 1965); Knuth, *TAOCP* vol. 1 §2.5.** Not an
ISA, but the load-bearing citation for §7.2: the property that power-of-two blocks
allocated in decreasing size order never fragment is what keeps the admission test a sum of
bits rather than a search. The register map is a buddy system over 512 bits, and inherits
the theorem.

**Motorola 68000 — `MOVE.B`/`.W`/`.L` on `D0`.** The *third* home for width: the name is
size-free and the size lives in the opcode, with narrow writes **preserving** the upper
bits. Cited as the model **not** followed — it puts width entirely in the opcode, which
cannot name a slice, and it preserves, which is x86's hazard.

**RISC-V `V` — `vtype.SEW` set by `vsetvli`.** The **fourth** home for width: a per-context
mode register. Cited precisely because it is **the thing the hard constraint forbids**
(facts C9, nuance 2). `V` is the counterexample to "the opcode types the operation", and it
is prior art *against* this design space, not for it. It is also unavailable: C10/C11.

**Tagged architectures — Burroughs B5000/B6700, Symbolics Lisp machines, IBM System/38 and
AS/400.** Type travels with the value rather than the name. The fifth possible answer, and
the one the 512-bit budget forbids outright: tags cost bits inside the context (facts C24).

**The competing answer, stated fairly: instruction-encoded `(offset, width)`.** CORE-V
`cv.extract` / `cv.extractu` / `cv.insert` (start bit and length−1 as immediates), T-Head
`Xtheadbb`'s `th.ext` / `th.extu` (two 6-bit immediates), ARM `UBFX`/`SBFX`/`BFI`, x86
`BEXTR` (BMI1) and `PDEP`/`PEXT` (BMI2), 68000 `BFEXTU`/`BFEXTS`/`BFINS` (facts C22). All
are shipping; the two RISC-V ones are upstream in binutils and LLVM. This is **option 1 done
properly**, and it beats this proposal on one axis: it can name **any** bit range, including
unaligned and non-power-of-two fields (§12.4). It loses on three:
1. It costs instruction bits and an extra instruction per access, on a machine whose whole
   argument is that a function must fit in 512 bits and a handful of instructions;
2. it needs new encodings, and only `funct7` groups `0x6`/`0x7` are free with `RESUME`
   already claiming one (C23);
3. **it is already closed.** C26: a bit-field insert/extract carrying an offset and a width
   "was considered and dropped: it would duplicate instructions that exist" (user #233,
   "Let's not overdesign"). Re-opening it is a decision for the user, not for a proposal.

**RISC-V `P` / `Zpn` packed SIMD.** Not ratified — the manual carries only a placeholder
chapter. It packs 8/16/32-bit lanes into the `x` registers and names the lane width **in the
opcode**, operating on all lanes at once; it gives **no name for an individual lane** (facts
C21). Evidence that the RISC-V community's consistent answer to sub-register slices has been
"the opcode names the geometry", never "the register name names the slice" — which is what
makes the user's option 2 a genuine departure rather than a re-derivation.

---

## §11 COST

| what | cost |
|---|---|
| **State per function** | **0 bits.** No table, no layout, nothing beside the I-cache. Compare M1: `RegField` is 16 bits of offset + 8 of width × 32 names + a flag ≈ **769 bits (~97 bytes) per resident function per tile**. |
| **State per context** | **512 bits, unchanged** (C3). The map is not per context and cannot be. |
| **State per tile** | **0 bits.** The map is a wire pattern replicated in gates in each decoder, not a memory. Nothing to load, fill, invalidate, or keep coherent across tiles. |
| **Decode latency** | A 3-input priority encode on `n[4:2]` selecting one of four constant shift amounts, in parallel with the fetch-to-decode path; then the 8-way lane mux and a 5-way intra-word extract (offsets `{0,32,40,48,56}` only) that the register read needs anyway. **No memory access enters the decode path**, where M1 placed a 32 × 24-bit indexed read in series with the mux select. *(Recorded as a consequence. C32: this is not the user's stated reason for rejecting M1 and is not offered as one.)* |
| **Register-file ports** | Every slice is naturally aligned and byte-aligned within one 64-bit word, so writes need only **byte write-enables** and reads need no straddle path (M3). |
| **Migration** | **72 B: 64 B of context + 8 B of PC. Unchanged** (C6, I11). Nothing the scheme adds travels, and — unlike M1 — nothing must be resident at the destination for the arriving context to decode. |
| **Encoding space** | **Zero.** No instruction added, no `funct7` group consumed, no field value assigned (C22, C23, C24). |
| **Nameable set** | 32 names: 1 zero, 1 reserved, 8 × 64, 16 × 32, 2 × 16, 4 × 8. **31 usable slices**, not 63 — the price of making `f<n>` and `x<n>` coincide (§5.2). |
| **Instructions per access** | **Zero extra.** A named slice is an operand, not something to extract. Compare the `th.ext`/`cv.extract` alternative: one extra instruction per access, plus one to insert. |
| **Host retrieval** | One `CXR` plus at most two RV64I shifts per field (§6.1). |
| **Compiler cost** | An unsigned narrow value costs one masking instruction per use that needs the zero-extended form (§8.4). Everything else is allocation, and allocation is greedy with no search (§7.2, §8.2). |

---

## §12 WHAT THIS CANNOT DO

**12.1 No 16-bit or 8-bit floating point.** `Zfh` and `Zfbfmin` are outside the ruled
subset (C10), so a 16-bit name has **no** floating-point opcode and an 8-bit name has no
IEEE format at all. The map *reserves the shape* — a 16-bit name is exactly where an `f16`
would go — but admitting `f16` is a subset change and needs a ruling. **The task's
"f16/f32/f64 by name width" is therefore f32/f64 only, today.** Saying otherwise would be
inventing an extension the record has not ruled.

**12.2 `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` — there is no such cap, and this
entry is withdrawn.** The struck text read: *"At most 32 bits of sub-32-bit values, in one
place. `S_narrow ≤ 32` (§7.3). A function holding eight live bytes — 64 bits, which K.6's
plain bit test admits — cannot be expressed."* **It can.** Eight live bytes pack into one
64-bit name and are read and written by shift-and-mask through a scratch name; the cost is
about 2-3 extra ops per access, not a rejection. **K.6's arithmetic and this map do not
disagree** — both count bits — so there is no ledger entry to make and C16 is not invoked
here. What remains true, and is the honest residue of the paragraph, is that 31 names
cannot *directly name* every slice at four widths (that would need 8+16+32+64 = 120 names),
so the seven remaining names trade **directness** against completeness (§1.3c). Directness
is instruction count. Capacity is 512 bits under every arrangement.

**12.3 No value narrower than 8 bits gets a name.** A live boolean costs 8 bits, not 1.
"Bit-packing is the name of the game" (#232) is **bounded below at a byte** by this map.
Admission must round up (§7.3), which means K.6's pure sum can over-admit a function full of
sub-byte values.

**12.4 No name for an arbitrary bit range.** Every slice is a naturally-aligned power of
two. A 12-bit field, a 3-bit tag, an unaligned 24-bit field: not nameable, and no
combination of names produces one. This is precisely where the `th.ext`/`cv.extract` shape
(§10) would have won, and it is the honest cost of choosing option 2 over option 1. A
function needing such a field must widen it to the next nameable width and pay the bits, or
pack it by hand with shifts inside a wider name — which works, and costs instructions rather
than architecture.

**12.5 The overlap rule is not machine-checkable.** Nothing in the core can detect that a
compiler kept `x8` and `x16` live at the same time; the second write silently corrupts the
first value. This is the one part of M6/C14's hard-error behaviour that **cannot** be
re-homed onto the decoder, and it moves to admission and to compiler correctness (§8.1). M1
had the same hole in a different shape — it could detect an *undefined* name but not an
*overlapping* liveness either, because its fields were disjoint by construction and it
simply could not express the error. This map can express it, so it must be checked
elsewhere. **Stated as a real regression in machine-enforced safety.**

**12.6 Silent truncation on a narrow write.** `add x16, x8, x9` truncates a 64-bit sum to
32 bits with no signal. A dynamic check ("the discarded bits are all copies of the sign
bit") is implementable and cheap, but it would fault on legitimate unsigned values and it
would be a runtime rejection where C15 puts rejection at admission. Not proposed; recorded
as an option.

**12.7 Unsigned narrow values cost an instruction.** Sign-extend-on-read is one convention
and it matches RV64's; the other convention would have cost signed values instead. Either
way one of the two pays (§8.4).

**12.8 The RV64 ABI's register roles are gone.** `sp`, `gp`, `tp` and the `s`/`t`
conventions do not survive: `x1` (`ra`) is reserved, `x2` (`sp`) is a 16-bit name, `x5`–`x7`
(`t0`–`t2`) are byte names. This is mostly a feature (§1.3e, §4.6: calls and spills become
illegal instructions rather than silent misbehaviour) but it means a stock toolchain can be
aimed only at the 64-bit level (§8.5), and any hand-written assembly must abandon ABI names.

**12.9 Half the nameable set is spent on coincidence.** 31 names, not 63 (§5.2). The
alternative — disjoint `x` and `f` namespaces — is rejected here because it forces a static
integer/floating-point partition of the 512 bits, which is K.6's third wrong answer made
structural. But the record does not settle it (§11 item 2 of the context file), and this
proposal *decides* it rather than inheriting it.

**12.10 One name denotes nothing.** `x1` is reserved. Its `ra`-suppressing side effect is
worth more than a 31st slice would be, but it is still a name spent on nothing.

**12.11 The map cannot grow upward, and only awkwardly downward.** 64 is the top because
`RV64IMAFD`'s widest datum is 64 (`Q` is out of subset). Adding a 4-bit level would need 8
more names for one 32-bit region and the names do not exist. **The width mix is not
tunable after ratification** — that is the whole difference from M1, and it is the cost side
of the hard constraint, not a defect of this particular mix.

**12.12 It does not shrink a migration.** A function using 384 of 512 bits still migrates
72 B. The map tells the hardware what the bits *mean*; I2 and I11 say the whole file moves
regardless, and nothing here changes that.

---

## §13 WHAT THIS PROPOSAL DOES NOT DECIDE — questions for the user

1. **The four `fmv.*` and four FP load/store forms: keep or remove?** §4.5. Keeping them is
   literal `RV64IMAFD` (C10, a tier-1 ruling) and is harmless under this map. Removing them
   is what ratified `Zfinx`/`Zdinx` do and recovers eight encodings, and costs no
   expressiveness. **This proposal will not overturn a ruling; it asks.**
2. **The subset's name.** Facts §6.1 shows `RV64IMAFD` and "one file, two namespaces" cannot
   both be literally true, and that the thing the canon describes already has a ratified
   name: `RV64IMA_Zfinx_Zdinx`. This is a naming correction that costs nothing and buys a
   ratified specification for exactly these semantics. It touches ruling O4 and therefore
   needs the user.
3. **The narrow-level mix.** Two 16s and four 8s over one 32-bit region (chosen, §1.3c)
   versus four 16s and two 8s over one 64-bit region (twice the territory, but incomplete
   and therefore breaking §7.2's theorem). The completeness argument decides it here; the
   record does not (§11 item 1 of the context file).
4. ~~**Whether `S_narrow ≤ 32` is an acceptable cap** (§12.2).~~ **CLOSED, not open**
   `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`: no such cap exists, K.6's plain bit
   test does not over-admit, and no ledger entry is required. The live question in its place
   is how many extra ops per packed access byte-heavy functions actually pay — a question of
   **instruction count**, answerable only by measurement.
5. **Whether to add the truncation check** of §12.6.

## §14 CONSTRAINT CHECK

| | constraint | status |
|---|---|---|
| C1 | no state outside the 512 bits and the encoding | ✅ the map is a wire pattern; name + opcode fully determine offset, width, type |
| C2 | no third referenced object at decode | ✅ instruction + data only; no duplicate-page argument is offered |
| C3 | exactly 512 bits, bit-packed | ✅ four widths, 31 slices; not eight registers (see C30) |
| C4 | the file may not be widened | ✅ unchanged |
| C5 | 512 in, 512 out; PC beside | ✅ unchanged |
| C6 | migration exactly 72 B | ✅ §11 |
| C7 | one file, two namespaces, no separate FP file | ✅ §5; `f<n>` **is** `x<n>` |
| C8 | no `fcsr`, no rounding-mode or FP exception state | ✅ §4.4: static `rm` only, `DYN` illegal, flags discarded |
| C9 | no stack; a function that spills cannot run | ✅ **strengthened**: `ra` and `sp` uses are illegal instructions (§1.3e, §4.6) |
| C10 | base ISA `RV64IMAFD`, nothing else | ✅ no extension used; `Zfh` explicitly *not* assumed (§12.1) |
| C11 | no vector extension on either host | ✅ no `vmv.x.s`/`vslidedown`/`vfmv.f.s` anywhere (§6) |
| C12 | nothing blocks | ✅ untouched |
| C13 | nothing speculative | ✅ untouched; and no rename is required (§10, x86 merge hazard) |
| C14 | an undefined register is a hard error | ⚠️ **partly re-homed**: four decode checks (§4.7) replace it; the overlap rule moves to admission (§12.5), which is a stated regression |
| C15 | rejection is fatal, never softened | ✅ §7.6 keeps `die()` on the bit test |
| C16 | "cannot be expressed ⇒ cannot be offloaded" is allowed | ✅ invoked in §12.1 and §12.4, and each is *stated*. `[CORRECTED - user ruling 2026-09-03 (liveness)]` **§12.2 is withdrawn as an invocation** — no width is inexpressible; a sub-name value is packed and reached through a scratch name at ~2-3 extra ops per access. |
| C17 | admission counts peak liveness in bits, one pool | ✅ §7.3; one pool is structural, not asserted (§5.2) |
| C18 | not a count of names | ✅ §7.5; no name appears in the test |
| C19 | a register never read costs nothing | ✅ §7.6 step 2 |
| C20 | admissibility is a property of the generated code | ✅ §8.3 |
| C21 | `nmfc_bu` (480) and `nmfc_expand` (384) must still fit | ✅ §1.4, §7.4 |
| C22 | twelve instructions plus privileged `RESUME` | ✅ none added |
| C23 | only `funct7` `0x6`/`0x7` free | ✅ none consumed |
| C24 | the canon assigns no field values | ✅ §3 |
| C25 | every operand is a value in a GPR | ✅ untouched (M12) |
| C26 | no runtime `(offset, width)` bit-field instruction | ✅ none proposed; the alternative is cited and left closed (§10) |
| C27 | `CXW`/`CXR` are complete as the host aperture | ✅ §6.1 uses only `CXR` plus RV64I shifts |
| C28 | the lane is an instruction field, not a register | ✅ unchanged (M4) |
| C29 | tier 4 never decides anything | ✅ `NMFC_CTX_WORDS`/`x1..x8`/`defaultLayout()` are treated as this implementation's packing and are contradicted deliberately (M15) |
| C30 | a fixed table at ONE width is the SST layout again | ✅ four widths, complete 64/32 coverage, a nested narrow subtree; the 8 × 64 packing is one *usable subset* of the map (§8.5), not the map |
| C31 | DESIGN §25.7 D:2560-2564 must be marked superseded | ✅ M1 states it; a new ledger entry of L45's shape is required |
| C32 | do not attribute reasons the user did not give | ✅ the decode-latency and residency advantages are labelled consequences, not reasons, in §1.2 and §11 |
