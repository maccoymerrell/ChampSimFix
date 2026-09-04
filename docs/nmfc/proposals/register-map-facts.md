# Register-map facts: what ratified RISC-V actually says

**Purpose.** The 2026-09-03 exchange on how a 512-bit bit-packed context is *named*
produced a provisional answer containing about two dozen RISC-V claims. This file
verifies each one against the ratified specification and records the correct
statement. It settles no design question; it removes the wrong premises from the
ones still open.

**Sources, with versions.** All citations are to the RISC-V Unprivileged ISA
Manual, ratified release **v20260120** (`docs.riscv.org/reference/isa/v20260120/unpriv/`),
and to the current `riscv/riscv-isa-manual` `main` sources from which it is built
(`src/unpriv/*.adoc`, fetched 2026-09-03). The V extension is cited from
**"V" Vector Extension v1.0** (ratified) as carried in that manual
(`src/unpriv/vector-common.adoc`). Chapter numbers are those of the v20260120
rendering. Non-RISC-V prior art is cited to its own vendor documentation.

**Note added 2026-09-03 `[user ruling 2026-09-03 (liveness)]`.** No C-fact in this file
asserts a cap on live values, and none is superseded. Read C12/C14 as they are written:
the gap is **naming**, i.e. how *directly* a 5-bit field can denote a slice. It is not a
capacity limit. The context is 512 independent bits on a strictly in-order core with no
renaming; a value narrower than a name is bit-packed inside one and reached by
shift-and-mask through a **scratch** name, so every width — the byte tier included —
remains reachable and the only cost is **instruction count** (about 2-3 extra ops per
packed access). Admission is a test on **bits of peak liveness plus the scratch bits the
packing needs**, never a count of values and never a 32-bit charge on a narrow one.

**Verdict key.** TRUE = the claim is correct as stated. NUANCED = the claim's
conclusion survives but its statement is imprecise, incomplete, or true only under
a condition that was not named. FALSE = the claim is wrong.

---

## 1. Host-side retrieval: getting the 512 bits back into usable registers

### C1. `fmv.d.x` / `fmv.x.d` move raw bits between an integer register and an FP register without conversion.
**TRUE, with a condition that was not stated.**
Ch. 21 ("D" Extension), §21.1.5: "FMV.X.D moves the double-precision value in
floating-point register *rs1* to a representation in IEEE 754-2008 standard encoding
in integer register *rd*. FMV.D.X moves the double-precision value encoded in IEEE
754-2008 standard encoding from the integer register *rs1* to the floating-point
register *rd*." Non-canonical NaN payloads are preserved; there is no conversion.
**The unstated condition: these two instructions are restricted to XLEN >= 64.**
On RV32D they do not exist; RV32 needs `fmvh.x.d` / `fmvp.d.x` from **Zfa**, or a
round trip through memory. NMFC is RV64, so the condition is met — but it must be
stated, because it is the reason the RV32 world grew a different instruction.

### C2. `fmv.w.x` / `fmv.x.w` do the same for 32-bit values.
**TRUE.** Ch. 20 ("F" Extension, v2.2). "FMV.X.W moves the single-precision value in
floating-point register *rs1* represented in IEEE 754-2008 encoding to the lower 32
bits of integer register *rd*. The bits are not modified in the transfer, and in
particular, the payloads of non-canonical NaNs are preserved." `FMV.W.X` is the
reverse. Both exist on RV32 and RV64.
Naming note: these were `fmv.x.s` / `fmv.s.x` before spec v2.2. The rename to `.w`
was made precisely to signal *moves 32 bits without interpreting them*, as against
`.s`, which would imply a single-precision **value**. Any NMFC document that writes
`fmv.x.s` is quoting a pre-2017 spec.

### C3. `fmv.x.w` sign-extends into the upper 32 bits on RV64.
**TRUE, and it is the one asymmetry that matters here.**
Ch. 20: "For RV64, the higher 32 bits of the destination register are filled with
copies of the floating-point number's sign bit." So `fmv.x.w` is **not** a bit-exact
64-bit move. It is a 32-bit move plus a sign-extension the ISA performs for you and
you cannot suppress. See C15 for why this matters to option 2.

### C4. NaN-boxing: a narrower float held in a wider `f` register must have its upper bits all 1s.
**TRUE, and the rule has two halves, only one of which was quoted.**
Ch. 21, §21.1.2 "NaN Boxing of Narrower Values".
*Write half:* "Any operation that writes a narrower result to an `f` register must
write all 1s to the uppermost FLEN-*n* bits to yield a legal NaN-boxed value."
*Read half:* "Apart from transfer operations described in the previous paragraph,
all other floating-point operations on narrower *n*-bit operations, *n*<FLEN, check
if the input operands are correctly NaN-boxed, i.e., all upper FLEN-*n* bits are 1.
If so, the *n* least-significant bits of the input are used as the input value,
otherwise the input value is treated as an *n*-bit canonical NaN."
The read half is the operative one for any scheme that lets integer code and float
code touch the same bits: an `f32` that arrived through a *non*-transfer path with
arbitrary upper bits is **silently read as canonical NaN**, not trapped.

### C5. `vmv.x.s`, `vslidedown`, `vfmv.f.s` extract elements of a vector register into scalar registers.
**TRUE, with exact semantics worth having in front of you.** V v1.0,
"Vector Permutation Instructions".
- *Integer Scalar Move*: `vmv.x.s rd, vs2  # x[rd] = vs2[0]`. "The `vmv.x.s`
  instruction copies a single SEW-wide element from index 0 of the source vector
  register to a destination integer register. If SEW > XLEN, the least-significant
  XLEN bits are transferred and the upper SEW-XLEN bits are ignored. **If SEW < XLEN,
  the value is sign-extended to XLEN bits.**" It "ignore[s] LMUL and vector register
  groups" and "performs its operation even if `vstart` >= `vl` or `vl`=0."
- *Floating-Point Scalar Move*: `vfmv.f.s rd, vs2  # f[rd] = vs2[0]`. "copies a
  single SEW-wide element from index 0 of the source vector register to a destination
  scalar floating-point register."
- *Vector Slidedown*: `vslidedown.vx vd, vs2, rs1, vm  # vd[i] = vs2[i+x[rs1]]` and
  `vslidedown.vi vd, vs2, uimm, vm  # vd[i] = vs2[i+uimm]`. The offset is "an unsigned
  integer in the `x` register specified by `rs1`, or **a 5-bit immediate, zero-extended
  to XLEN bits**."
**Two things the provisional answer got structurally right but stated loosely:**
only **element 0** is scalar-extractable; every other element needs a `vslidedown`
(or `vrgather.vi`) first. And `vmv.x.s` **sign-extends** a sub-XLEN element — for an
unsigned 32-bit field packed in the context, the host must mask after extracting.

### C6. `vfmv.f.s` NaN-boxes when SEW < FLEN and raises illegal-instruction when SEW > FLEN.
**FALSE as a claim about the ratified text.** The V v1.0 description of `vfmv.f.s`,
quoted in full at C5, says **neither**. It states only that a SEW-wide element is
copied to the destination `f` register. The NaN-boxing rule that *is* in the V spec
applies to the opposite direction — an `f` register used as a **scalar source operand**
to a vector instruction: "If FLEN > SEW, the value in the `f` registers is checked
for a valid NaN-boxed value, in which case the least-significant SEW bits of the `f`
register are used, else the canonical NaN value is used. Vector instructions where
any floating-point vector operand's EEW is not a supported floating-point type width
(which includes when FLEN < SEW) are reserved."
Practically, implementations NaN-box the `vfmv.f.s` destination and software should
assume it; but **do not cite the spec for it**, and do not build an argument on
`vfmv.f.s` trapping when SEW > FLEN.

### C7. Using the V extension for a 512-bit context requires VLEN >= 512.
**NUANCED — true for one register, and the requirement has a name that is not "V".**
V v1.0 §"Vector Extension for Application Processors": "The V vector extension depends
upon the **Zvl128b** and Zve64d extensions." So plain `V` guarantees only **VLEN >= 128**;
the accompanying note says 128 "was chosen as a compromise for application processors."
A minimum VLEN of 512 is the separately named extension **`Zvl512b`** (V v1.0,
"Zvl*: Minimum Vector Length Standard Extensions", which tabulates Zvl32b through
Zvl65536b). The general constraint is only "VLEN >= ELEN, ... a power of 2, and ...
no greater than 2^16."
**And 512 bits do not actually require VLEN >= 512:** a *register group* at LMUL=8
on VLEN=64, LMUL=4 on VLEN=128, or LMUL=2 on VLEN=256 also holds 512 bits. What
requires VLEN >= 512 is holding the context in **one architectural register name**,
which is what invariant I1's "a 512-bit vector register" asks for.
**Correct statement:** the host ISA for NMFC is not `RV64GCV`; it is `RV64GCV_Zvl512b`
(or a profile that implies it). Write the `Zvl512b` explicitly or the FORK/JOIN operand
is not architecturally guaranteed to exist.

### C8. Host retrieval is "solved" by the V extension.
**NUANCED.** It is solved, but V is the expensive way and it was presented as the only
way. The context is 64 bytes — exactly one cache block (I2, H.3). `vse64.v` to a
64-byte buffer followed by ordinary `ld`/`lw`/`flw`/`fld` retrieves any field at any
width with correct sign/zero extension and no SEW juggling, in one store plus one load
per field. The V path costs a `vsetvli` per element width plus a `vslidedown` per
element beyond index 0. Both work; the memory path is the one a compiler will emit,
and it is worth saying so, because C7 shows the V path also drags a `Zvl512b`
requirement onto the host.

---

## 2. Typing inside the core

### C9. RISC-V never infers an operation's type from its register operand; the opcode types the operation.
**TRUE for register *names*, and the strongest possible evidence for it is a ratified
extension the provisional answer did not cite.** Under **Zfinx** (Ch. 26, §26.1) the
`f` registers are deleted and floating-point instructions operate on the `x` registers:
"whenever such an instruction would have accessed an `f` register, it instead accesses
the `x` register with the same number." So `add x5, ...` and `fadd.s x5, ...` name the
**same architectural register** and differ only in opcode. The type demonstrably never
lived in the register name.
**Two nuances, one of which is load-bearing:**
1. The `x`/`f` split is a *coarse* type partition — you cannot write `fadd.d x1, x2, x3`
   in F/D — so a namespace does carry "integer vs float" as a *legality constraint*,
   which is what makes option 2's intuition feel right. But it is redundant with the
   opcode, never a substitute for it. Zfinx is the proof: remove the namespace and
   nothing about typing breaks.
2. **The V extension is a genuine counterexample to "the opcode types the operation."**
   Element width comes from `vtype.SEW`, set by `vsetvli` (V v1.0 §"Vector type register,
   vtype": "The value in `vsew` sets the dynamic *selected element width* (SEW)").
   The type there is neither in the opcode nor in the register name — it is in a **CSR**.
   This matters far beyond a footnote: **`vsetvli`/SEW is precisely the per-context mode
   register the user's hard constraint forbids.** It is not prior art *for* the design;
   it is prior art for the thing the design has ruled out, and it should be cited that way.

### C10. Option 1 (custom instructions that encode type) is redundant because the opcode already types the operation.
**TRUE, and it is the right conclusion for the wrong-sounding reason.** RISC-V opcodes
already carry the type (`add` / `addw` / `fadd.s` / `fadd.d` / `fadd.h`), so an
instruction that "encodes type into it" is describing what every RISC-V instruction
already is. The user's parenthetical — "assuming risc-v has some instructions which
infer based on target registers" — is the part to answer directly: **no base instruction
does, and the ISA went out of its way to make sure of it.** Option 1 is redundant.
**But the redundancy is only about *type*.** What option 1 could still encode that the
opcode cannot is **width-and-offset**, i.e. *which bits*. That is a live question (see
C16, and the `th.ext` / `cv.extract` prior art at C21–C22), and dismissing option 1
wholesale dismisses it too.

### C11. An `f32` living in a 32-bit slot needs no NaN-boxing.
**TRUE, with ratified precedent that also supplies the replacement rule.** NaN-boxing
exists only because FLEN is a fixed constant wider than the value (C4); with no upper
bits there is nothing to box. **Zfinx already made this exact call** and specified what
takes its place (Ch. 26, §26.1 "Processing of Narrower Values"):
- "Floating-point operands of width *w* < XLEN bits occupy bits *w*-1:0 of an `x`
  register. Floating-point operations on *w*-bit operands **ignore** operand bits
  XLEN-1:*w*."
- "Floating-point operations that produce *w* < XLEN-bit results fill bits XLEN-1:*w*
  with **copies of bit *w*-1 (the sign bit)**."
And the rationale is the NMFC argument almost verbatim: "Recoding is less practical for
Zfinx, though, since the same registers hold both floating-point and integer operands.
Hence, the need for NaN boxing is diminished."
**Correct statement:** dropping NaN-boxing is defensible and precedented — but it is a
*deviation from F/D*, not a property of F/D, and the ratified deviation comes packaged
with a stated substitute (ignore-on-read, sign-extend-on-write). Adopt the substitute
or state a different one; do not leave the upper-bit rule unspecified.

---

## 3. Can RV64 name a sub-XLEN register?

### C12. RV64 has no way to name a sub-XLEN register.
**TRUE — this is the real gap, and it is a gap in *naming*, not in *computation*.**
The register file is defined at a single width: "For RV32I, the 32 `x` registers are
each 32 bits wide, i.e., XLEN=32" (Ch. 2), with RV64I widening the same 32 names to 64.
There is no architectural name for a half, a byte, or a bit of an `x` register.
The claim as originally phrased — "the only thing standard RISC-V cannot express is a
register narrower than XLEN/FLEN" — is **NUANCED**, because RV64 expresses sub-XLEN
*widths* freely; see C13 and C14.

### C13. `addw` / `sext.w` do not name an upper half.
**TRUE, and the exact semantics explain why the ISA is built this way.**
Ch. 5 (RV64I): "`addw` and `subw` are RV64I-only instructions that are defined
analogously to `add` and `sub` but operate on 32-bit values and produce signed 32-bit
results. Overflows are ignored, and **the low 32-bits of the result is sign-extended to
64-bits and written to the destination register.**" `sllw`/`srlw`/`sraw` likewise
"operate on 32-bit values and sign-extend their 32-bit results to 64 bits."
The governing design note: "The compiler and calling convention maintain an invariant
that **all 32-bit values are held in a sign-extended format in 64-bit registers.** Even
32-bit unsigned integers extend bit 31 into bits 63 through 32."
**So the `*W` instructions are the opposite of a slice mechanism.** They exist to
guarantee the *whole* 64-bit register is a canonical restatement of one 32-bit value.
Bits 63:32 are not a second field; they are redundant copies of bit 31. `sext.w` is not
even a real instruction — it is the pseudo-instruction `addiw rd, rs, 0`.
**Consequence the design must absorb:** the RV64 sign-extension invariant and the NMFC
bit-packing goal are in **direct opposition**. Standard RV64 spends bits 63:32 to make
32-bit values canonical; NMFC wants those bits to hold a *different* value. Any option-2
map therefore breaks the invariant that every RV64 compiler back end, calling convention,
and `sltu`/branch idiom assumes. That is not fatal, but it is the actual cost of the
proposal and it had not been named.

### C14. Sub-XLEN *widths* are otherwise inexpressible.
**FALSE.** RV64 expresses sub-XLEN widths in several ratified ways; none of them is a
*name*, which is exactly why C12 stands:
- **Memory operands:** `lb`/`lbu`/`lh`/`lhu`/`lw`/`lwu`/`sb`/`sh`/`sw` move 8/16/32-bit
  fields with explicit sign or zero extension. A packed context spilled to a 64-byte
  buffer is fully addressable this way at no ISA cost (see C8).
- **Zbb:** `sext.b`, `sext.h`, `zext.h` — width conversion as first-class instructions.
- **Zbs:** `bext` / `bexti` extract a single bit named by a register or a 6-bit immediate.
- **Zbkb (ratified, part of the scalar-crypto Zkn/Zks bundle):** `pack`, `packh`, `packw`
  — explicit sub-register **packing**: on RV64, `pack` builds `{rs2[31:0], rs1[31:0]}`
  in one destination register, and `packh` builds a halfword from two bytes.
**Correct statement:** RISC-V can already *compute on* and *assemble* sub-register
fields. What it cannot do is let an instruction's 5-bit register field **denote** one.
The gap is architectural naming and nothing else — which is a stronger and more useful
framing of the design problem than "RISC-V can't do narrow registers."
`[user ruling 2026-09-03 (liveness)]` And "naming" here costs **instructions, not
capacity**: the shifts, masks and `or`s listed above, staged through a scratch name, reach
any field at any width, so no width is out of reach and no live value need be charged more
bits than it occupies.

---

## 4. Option 2's own arithmetic

### C15. `f<n>` and `x<n>` can be the same bits, making `fmv.*` a free alias.
**NUANCED, and the ratified answer is better than the proposed one.**
Aliasing the namespaces is not novel — **Zfinx is that design, ratified** (C9). But
Zfinx does **not** turn the transfer instructions into free aliases; it **removes them
from the ISA**: "The Zfinx extension adds all of the instructions that the F extension
adds, *except* for the transfer instructions `flw`, `fsw`, `fmv.w.x`, `fmv.x.w`,
`c.flw`, `c.flwsp`, `c.fsw`, and `c.fswsp`." Zdinx likewise drops `fld`, `fsd`,
`fmv.d.x`, `fmv.x.d`. Removing them recovers encoding space; aliasing them to no-ops
does not, and leaves an instruction whose documented behaviour is now a lie.
**And two of the four are not no-ops even under aliasing:**
- `fmv.x.w x5, f5` with `x5` and `f5` the same 64 bits **sign-extends bits 31:0 over
  bits 63:32** (C3) — it destroys whatever was packed in the upper half.
- `fmv.w.x f5, x5` **NaN-boxes**, writing all 1s to bits 63:32 (C4) — same destruction,
  different constant.
So "free alias" is true for the `.d` pair and **false for the `.w` pair**, in the most
damaging possible way: a silent overwrite of a neighbouring packed field.

### C16. Ratified RISC-V has a mechanism for naming a value wider than one register name.
**TRUE, and this is the single most relevant piece of prior art in the whole ISA.**
**Zdinx** (Ch. 26, §26.1) faces the identical problem — 64-bit doubles, 32-bit register
names — and solves it with **aligned register pairs**:
- "For RV32, double-precision operands in Zdinx are held in aligned `x`-register pairs.
  In other words, **register numbers must be even.** Use of misaligned (odd-numbered)
  registers for double-width floating-point operands is *reserved*."
- "Regardless of endianness, **the lower-numbered register holds the low-order bits** of
  double-width floating-point operands, and the higher-numbered register holds the
  high-order bits. ... bits 31:0 of a double-precision operand might be held in register
  `x14`, with bits 63:32 of that operand held in `x15`."
- "When a double-width floating-point result is written to `x0`, **the entire write takes
  no effect.** ... When `x0` is used as a double-width floating-point operand, the entire
  operand is zero — in other words, `x1` is not accessed."
This is a complete, ratified, shipping answer to "the number of the reg implies the
slice": a fixed slice width per name, an alignment rule for wider values, an explicit
endianness-independent ordering rule, and a specified `x0` interaction. **Any option-2
map should be written in Zdinx's shape, and any place it departs from Zdinx's shape
should say why.**

### C17. 5-bit register fields give 32 names per namespace, so 64 names over two namespaces.
**NUANCED — the encoding gives 32; the ISA gives you 31.**
The 5-bit fields are real (Ch. 2: "the five-bit *rs1* and *rs2* fields"), so the *encoding*
affords 32 values each. But **"Register `x0` is hardwired with all bits equal to 0"**
(Ch. 2, `norm:x0eq0`), and that is not decoration: `nop` is `addi x0,x0,0`, `ret` is
`jalr x0,ra,0`, `j` is `jal x0,imm`, every unconditional-branch and discard idiom, and
the whole HINT encoding space all depend on it. Redefining `x0` as a packed slice
invalidates the assembler, the linker relaxations, and every compiler back end.
**So the `x` namespace contributes 31 usable slice names, not 32.** `f0` **is** general
(the FP file has no hardwired-zero register), so `f` contributes 32.
**Correct arithmetic: 63 nameable slices, not 64.** At a uniform width that is
512/63 ≈ 8.1 bits per name — i.e. **a uniform 8-bit-per-name map does not fit the
nameable set**, which is exactly the kind of thing that has to be known before the
width mix is chosen rather than after.
Two further limits on the nameable set:
- **Compressed encodings** use 3-bit fields limited to eight registers: "CIW, CL, CS,
  CA, and CB are limited to just 8 of them ... which correspond to registers `x8` to
  `x15`" (Zca). If NMFC ever wants compressed encodings, the hot slices belong at
  indices 8–15. (Canon K.6 currently *excludes* compressed encodings from the admissible
  subset, so this is a note for the record, not a present constraint.)
- **RV64E** already demonstrates trading names for state: it "reduce[s] the integer
  register count to 16 general-purpose registers, (`x0`–`x15`)", and its stated
  motivation is startlingly close to NMFC's own — "interest in RV64E for microcontrollers
  within large SoC designs, and **to reduce context state for highly threaded 64-bit
  processors.**"

### C18. In a fixed-width-per-name map, the opcode's width suffix becomes redundant with the register name.
**Not in the provisional answer; it follows from it, and it is the first real design
consequence.** If slice 5 is 32 bits wide, then `fadd.d f5, f5, f5` is unrepresentable —
the name is too narrow for the opcode. Every instruction now carries width twice: once
in the opcode suffix (`.s`/`.d`, `add`/`addw`) and once implicitly in the register
number. The design must pick one of three, and say which:
1. **Mismatch is an illegal instruction** — decode checks the name's width against the
   opcode's. Safe, and it makes the map partly self-checking; costs a decode comparison
   against a constant table (which is in the *decoder*, not in the context, so the hard
   constraint is respected).
2. **The name wins, the suffix is ignored** — then `.s`/`.d` is dead encoding space and
   should be reclaimed, Zfinx-style.
3. **The opcode wins** — then the name does *not* determine the slice, and option 2 has
   collapsed back into needing a width source elsewhere.
Zdinx picks (1): odd-numbered registers for double-width operands are *reserved*.

---

## 5. Prior art

### C19. x86 sub-register aliasing (RAX/EAX/AX/AL) is prior art for register views.
**TRUE, and its known failure modes are the reason to read it carefully.**
Three specifics the citation should carry, because each is a decision NMFC must make:
- The write rules are **inconsistent by width**: writing `EAX` **zero-extends** into
  `RAX`, while writing `AX` or `AL` **preserves** the upper bits. Two different
  behaviours for two different views of one register.
- `AH`/`BH`/`CH`/`DH` are **non-contiguous** views (bits 15:8) and exist only for the
  legacy four registers — an irregularity that survives to this day.
- The preserve-on-narrow-write rule creates **partial-register dependencies**: the
  hardware must merge the new narrow value with the old wide one, which cost Intel a
  documented class of stalls and, later, dedicated merging micro-ops. **This is the
  hazard NMFC inherits if narrow writes preserve neighbours**, and it lands on a barrel
  core with W pipes where a merge is a read-modify-write of a shared 512-bit line.
  Disjoint slices avoid it; overlapping views do not.

### C20. ARM SVE has "typed views" of registers.
**FALSE as stated.** In SVE the element type is a **size specifier in the instruction**
(`.B`/`.H`/`.S`/`.D`), not a property of the register name: `Zn` is a size-agnostic
name and `ADD Z0.S, Z0.S, Z1.S` types the operation in the opcode/encoding. SVE is
therefore an example of C9's rule, not an exception to it.
**The real ARM prior art for option 2 is AArch64's SIMD&FP register file**, where
`Bn`/`Hn`/`Sn`/`Dn`/`Qn` are five architectural names for the same physical register at
8/16/32/64/128 bits — the **letter selects the width and the number selects the
register**, which is very nearly the user's "the number of the reg implies the slice."
Two differences that matter: all five views are **bottom-anchored** (they are nested
prefixes of one register, not disjoint slices of a big one), and **writing a narrow view
zeroes the upper bits** — AArch64 deliberately chose zeroing over x86's preserve, which
is how it avoids C19's partial-register hazard. AArch64's `Wn`/`Xn` pair follows the same
rule: writing `Wn` zeroes bits 63:32 of `Xn`.

### C21. The RISC-V P / Zpn packed-SIMD extension is prior art for sub-register slices.
**NUANCED — it is prior art for the *opposite* mechanism, and it is not ratified.**
*Status, authoritative:* the current manual contains `src/unpriv/zp.adoc`, whose entire
content is "**NOTE: This chapter is a placeholder for the forthcoming `P` and `Zp*`
extensions for packed SIMD within the `x` registers.** It is included so that chapter
numbering will not change once the extensions are ratified and incorporated." The RISC-V
specification dashboard puts P in *Development* as of July 2026. **Do not cite P as
ratified.**
*Mechanism:* P packs 8/16/32-bit lanes into the `x` registers and names the lane width
**in the opcode** — `ADD8`/`ADD16`, being renamed `PADD.B`/`PADD.H` — operating on all
lanes in parallel. It gives you **no name for an individual lane.** So P is evidence that
the RISC-V community's answer to "sub-register slices" has consistently been *the opcode
names the geometry*, never *the register name names the slice*. It also contemplates
register **pairs** for wider RV64 operands in a future `Zp*`, which is C16's pattern again.

### C22. Xpulp / CORE-V is prior art for naming a bit range.
**TRUE, and it is the closest thing in the RISC-V world to an instruction-encoded
(offset, width).** CORE-V CV32E40P (formerly PULP `Xpulp`):
- `cv.extract`:  `rD = Sext(rs1[min(Is3+Is2,31):Is2])`
- `cv.extractu`: `rD = Zext(rs1[min(Is3+Is2,31):Is2])`
- `cv.insert`:   `rD[min(Is3+Is2,31):Is2] = rs1[...]`, "with the rest of the bits of rD
  remaining untouched and keeping their previous value"
where `Is2` is the start bit and `Is3` the length−1, both immediates in the instruction.
**T-Head XuanTie `Xtheadbb`** does the same with two 6-bit immediates: `th.ext rd, rs1,
imm1, imm2` = "extract the bits *imm1*..*imm2* from register *rs1*, sign-extend the
value, and store the result in *rd*", i.e. `reg[rd] := sign_extend(reg[rs1][imm1:imm2])`,
plus a zero-extending `th.extu`. Both are shipping, both are upstream in binutils/LLVM.
Non-RISC-V equivalents: ARM `UBFX`/`SBFX`/`BFI`, x86 `BEXTR` (BMI1) and `PDEP`/`PEXT`
(BMI2), and 68000's `BFEXTU`/`BFEXTS`/`BFINS`.
**Why this belongs in the record:** every one of these is option 1 done properly —
the *instruction* carries (offset, width), no table is consulted, and the hard constraint
is respected. The provisional answer dismissed option 1 on the grounds that the opcode
already carries *type*; that is true and irrelevant to whether it should carry *extent*.
The honest comparison is now: **encode the slice in the register number (option 2, a
fixed ISA-wide map) versus encode it in immediate fields (the `th.ext` / `cv.extract`
shape)** — and the second costs instruction bits and an extra instruction per access,
while the first costs nameable-set flexibility. That trade was never put.

### C23. The H extension addresses sub-register slices.
**FALSE — the premise is wrong.** The **H** extension is the **hypervisor** extension:
it adds HS-mode, VS/VU modes, two-stage address translation, the `hgatp`/`hstatus` CSRs
and the `hlv`/`hsv`/`hlvx` explicit-access instructions. It has nothing to do with
register widths, views, or slices. (The intended reference may have been the P/Zpn
packed extension — C21 — or the Zbb/Zbs/Zbkb bit-manipulation family — C14.)

### C24. Other prior art worth having on the record.
Not in the provisional answer; each is a distinct answer to the same question.
- **Motorola 68000** — a *third* model: the register name is size-free (`D0`) and the
  size lives in the opcode (`MOVE.B`/`.W`/`.L`), with narrow writes **preserving** the
  upper bits. So the three possible homes for width — register name (x86, AArch64
  SIMD&FP), opcode (68k, SVE, RISC-V P), and mode register (RISC-V V's `vtype.SEW`) —
  have all been tried in shipping hardware, and only the third is ruled out here.
- **MIPS-I / SPARC V8 FP register pairing** — `$f0` is a 32-bit single; a double named
  `$f0` occupies `$f0`+`$f1`, with odd numbers illegal for doubles. Zdinx is this rule,
  re-ratified thirty years later, which is decent evidence it is the durable answer.
- **Tagged architectures** (Burroughs B5000/B6700, Symbolics Lisp machines, IBM
  System/38 and AS/400; the Mill's belt metadata) — type travels with the *value* rather
  than the name. This is the fourth possible answer, and it is the one the 512-bit budget
  forbids: tags cost bits inside the context.

---

## 6. What this changes

Three findings bear on the design rather than on the citations.

**6.1 The ruled subset name is self-contradictory, and the correct name exists.**
Canon O4 rules the subset `RV64IMAFD` while I.0/I.7/K.6 describe `f0`–`f31` and `x0`–`x31`
as "two register namespaces over one packed file" and assert "`F`/`D` add ZERO bytes to a
migration." **Under the ratified spec those two statements cannot both hold.** In F/D,
`f0`–`f31` is a *separate* architectural register file of 32 × FLEN bits — 256 bytes at
FLEN=64, on top of the `x` registers — and Zfinx's own rationale prices it: "the
additional 128 bytes of architectural state increases the minimal implementation cost."
An implementation that aliases `f<n>` onto `x<n>` is not implementing F/D.
**The thing the canon is describing already has a ratified name: `Zfinx` + `Zdinx`.**
The subset should be written **`RV64IMA_Zfinx_Zdinx`** (F and D are then *excluded* by
construction — the spec states the two worlds are mutually incompatible: "software that
assumes the presence of the F extension is incompatible with software that assumes the
presence of the Zfinx extension, and vice versa"). This is a naming correction that costs
the design nothing and buys it a ratified specification for the exact semantics it wants,
including the narrow-value rules (C11) and the wide-value pairing rules (C16).
*Second-order consequence:* under Zfinx there is only **one** namespace, so the `f`
namespace's 32 extra names (C17) are gone. If the design wants 63 names it must keep F/D
encodings and accept that it has forked the ISA; if it wants Zfinx's clean semantics it
has 31 names for 512 bits. **That choice has not been made and it is the real fork in
option 2.**

**6.2 "A float reg implies the type" is the weaker half of option 2.**
Type is already in the opcode (C9), and Zfinx demonstrates that deleting the `f`
namespace costs no typing at all. What the second namespace actually buys is **+32
names** — i.e. finer slicing of the 512 bits — and that is the argument that should be
made for keeping it. The half of option 2 that is genuinely load-bearing, and genuinely
supported by prior art, is the other half: **"the number of the reg implies the slice."**

**6.3 The RV64 sign-extension invariant is the proposal's real cost.**
C13's design note — "all 32-bit values are held in a sign-extended format in 64-bit
registers" — is assumed by every RV64 compiler back end and by the calling convention.
Bit-packing two live 32-bit values into one 64-bit name breaks it. Combined with C15's
`fmv.*.w` behaviour and C4's NaN-boxing, **three separate ratified mechanisms will
silently overwrite the upper half of a packed 64-bit name.** Whatever map is chosen, the
document needs one explicit rule — narrow writes zero, preserve, or sign-extend the rest
of the name — and it should probably be AArch64's *zero* rather than x86's *preserve*,
because preserve is what produces the partial-register merge hazard (C19) on a machine
whose whole point is many contexts sharing a wide file.

---

## Appendix: verdict table

| # | Claim | Verdict |
|---|---|---|
| C1 | `fmv.d.x`/`fmv.x.d` move raw bits | TRUE (RV64-only; unstated) |
| C2 | `fmv.w.x`/`fmv.x.w` move raw 32 bits | TRUE |
| C3 | `fmv.x.w` sign-extends on RV64 | TRUE |
| C4 | NaN-boxing: upper bits all 1s | TRUE (read half was omitted) |
| C5 | `vmv.x.s`/`vslidedown`/`vfmv.f.s` extract | TRUE (element 0 only; `vmv.x.s` sign-extends) |
| C6 | `vfmv.f.s` NaN-boxes / traps on SEW>FLEN | **FALSE** (spec says neither) |
| C7 | 512-bit context needs VLEN>=512 | **NUANCED** (`Zvl512b`; V implies only `Zvl128b`; LMUL groups) |
| C8 | V extension solves host retrieval | **NUANCED** (memory path is cheaper and needs no `Zvl512b`) |
| C9 | Type never comes from the register | TRUE for names; **NUANCED** (V's `vtype.SEW` is a mode register) |
| C10 | Option 1 is redundant | TRUE for *type*; **NUANCED** (not for *extent*) |
| C11 | `f32` in a 32-bit slot needs no NaN-boxing | TRUE (Zfinx precedent supplies the substitute rule) |
| C12 | RV64 cannot name a sub-XLEN register | TRUE |
| C13 | `addw`/`sext.w` don't name an upper half | TRUE (and they enforce the *opposite* invariant) |
| C14 | Sub-XLEN widths otherwise inexpressible | **FALSE** (loads/stores, Zbb, Zbs, Zbkb `pack`) |
| C15 | `f<n>`/`x<n>` aliasing makes `fmv.*` free | **NUANCED** (free for `.d`; destructive for `.w`; Zfinx *removes* them) |
| C16 | (new) Ratified mechanism for wide-value naming | Zdinx aligned register pairs |
| C17 | 32 names per namespace, 64 total | **NUANCED** (`x0` is hardwired zero → **63**) |
| C18 | (new) Width is encoded twice; a rule is required | Zdinx reserves the mismatch |
| C19 | x86 sub-register aliasing is prior art | TRUE (with three cautions incl. partial-register merges) |
| C20 | SVE has typed views | **FALSE** (size is in the instruction; AArch64 `Bn..Qn` is the right cite) |
| C21 | P/Zpn addresses sub-register slices | **NUANCED** (not ratified; opcode names lane width, no per-lane name) |
| C22 | Xpulp addresses sub-register slices | TRUE (`cv.extract`/`th.ext` = option 1 done properly) |
| C23 | H extension addresses sub-register slices | **FALSE** (H is the hypervisor extension) |
| C24 | (new) 68k / MIPS-SPARC pairing / tagged machines | four homes for width, three viable here |
