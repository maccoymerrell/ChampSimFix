# PROPOSAL — `alias-plus-width-in-opcode`

**Status: A PROPOSAL. NOT CANON. Nothing here may be cited as a decision.**
Written against `register-map-facts.md` (RISC-V claims verified, verdicts C1–C24)
and `register-map-context.md` (the record, the fit-list M1–M15, the constraint-list
C1–C32). Where those files and this one use the same label, this document writes
**F-C*n*** for a facts-file verdict and **X-C*n*** for a context-file constraint, so
`F-C16` is Zdinx's register pairs and `X-C16` is "cannot be expressed ⇒ cannot be
offloaded". Bare `M*n*` is the context file's mechanism table.

**Per X-C24 (ruling O3), every bit position, field value and mnemonic below is an
ILLUSTRATIVE IMPLEMENTATION CHOICE.** What is being proposed is the *shape*: three
fields per operand (name, lane, width), all three read out of the instruction, none
read from anywhere else. The particular bits are shown because a proposal that does
not close its bit budget has not been checked.

---

## §0 THE PROPOSAL IN ONE PARAGRAPH

The 512 bits are divided by the ISA into **eight naturally-aligned 64-bit anchors**.
`x0`–`x7` and `f0`–`f7` are two names for the same eight anchors — anchor *n* is bits
`[64n, 64n+64)` — and names 8–31 in either namespace are **illegal**. That is the
aliasing half, and on its own it is exactly the layout X-C30 already rules out. The
second half is what makes it not that: a new instruction tier carries, **in the
instruction word**, a **2-bit width** *w* ∈ {8, 16, 32, 64} and a **3-bit lane** per
operand, so an operand denotes the slice

```
        bit offset = 64·name + w·lane          width = w
```

Every operand of every instruction in the new tier resolves that way. A 32-bit value
costs 32 bits and sixteen of them fit; a byte costs 8 and sixty-four fit; and the mix
is not chosen at ISA-design time, because there is no mix — the width comes from the
instruction and any legal combination of widths is expressible. **Nothing outside the
instruction word is consulted: no per-function table, no per-context mode register, no
memory reference.** The width lives where RISC-V already puts a width — the opcode —
and the lane lives where AArch64 already puts a lane — the encoding.

**What this asks the user to rule on** is stated in full at §18. The short form: it
adds an instruction tier to the function core, which X-C10 ("the base ISA is
`RV64IMAFD` and nothing else") currently forbids, and it spends one custom major
opcode, which the tier-4 header deliberately kept free. Those are the two prices.

---

## §1 THE MAP

### 1.1 Anchors

| anchor | bit range | `x`-name | `f`-name |
|---|---|---|---|
| 0 | 0–63 | `x0` (reads zero — §6.3) | `f0` |
| 1 | 64–127 | `x1` | `f1` |
| 2 | 128–191 | `x2` | `f2` |
| 3 | 192–255 | `x3` | `f3` |
| 4 | 256–319 | `x4` | `f4` |
| 5 | 320–383 | `x5` | `f5` |
| 6 | 384–447 | `x6` | `f6` |
| 7 | 448–511 | `x7` | `f7` |

Eight anchors × 64 bits = 512 bits exactly. The anchor count is not a design choice:
it is 512 ÷ 64, forced the moment the name width is 64.

`f`*n* and `x`*n* are **the same bits**, per the user's option 2 and the task
statement. This is Zfinx's structure, ratified (F-C9): under Zfinx the `f` registers
are deleted and floating-point instructions address the `x` registers with the same
number. The difference here is that the `f` names are *kept* rather than deleted,
because keeping them costs nothing in a tier that has no separate file and because
`f0` is the only base-tier name that reaches anchor 0 (§6.3).

### 1.2 Slices

A **slice** is `(name, lane, w)` with `w` ∈ {8, 16, 32, 64} and `lane` < 64/*w*:

```
  offset(name, lane, w) = (name << 6) | (lane << log2 w)
```

| *w* | lanes per anchor | slices in the context | lane bits used |
|---|---|---|---|
| 64 (D) | 1 | 8 | 0 |
| 32 (W) | 2 | 16 | 1 |
| 16 (H) | 4 | 32 | 2 |
| 8 (B) | 8 | 64 | 3 |

**120 distinct slices** are nameable, all overlapping, all naturally aligned. Every
slice lies wholly inside one anchor and wholly inside one byte boundary. `lane` ≥
64/*w* is a **reserved encoding** and raises illegal-instruction — Zdinx's rule for
misaligned register pairs (F-C16), generalised.

### 1.3 The three properties that follow immediately

1. **No slice straddles a 64-bit word.** `offset` is a multiple of *w* and *w* divides
   64. This answers context-file §11 question 3 with a flat **no**, and it retires the
   straddle path in `Context512::read`/`write` (M3).
2. **No slice straddles a byte.** *w* ≥ 8 and slices are *w*-aligned. The context
   write port therefore needs **byte enables and nothing finer** — 8 per anchor, 64
   per context — never bit-level write enables.
3. **The map is arithmetic, not tabular.** `offset` is a 3-bit field shifted left by
   an amount selected from {3,4,5,6} by a 2-bit field, OR'd with a 3-bit field shifted
   left by 6. There is nothing to look up.

---

## §2 WHY THIS IS NOT X-C30's REJECTED TABLE

X-C30 rules out "a fixed aliasing table at one width" as `RegLayout::defaultLayout()`
with the table deleted, and says the design question is "the MIX of widths among the
nameable set and the aliasing pattern."

**This proposal does not choose a mix. It removes the need to.** The discriminating
test is a single number:

| | a 32-bit value costs | 32-bit values that fit | `nmfc_bu` (7×64 + 1×32 = 480 b) |
|---|---|---|---|
| X-C30's table (8 × 64-bit names) | **64 bits** | 8 | fits at 512/512, no slack |
| a fixed *mixed* table, e.g. 4×64 + 4×32 + 4×16 + 8×8 | 32 bits, if a 32-bit name is free | 4 | **REJECTED** — only four 64-bit names exist |
| **this proposal** | **32 bits** | **16** | fits at 480/512, 32 bits slack |

The middle row is the important one. A frozen mixed table must satisfy
`64·n64 + 32·n32 + 16·n16 + 8·n8 = 512` for one chosen `(n64, n32, n16, n8)`, and any
single choice serves one shape of function. `nmfc_bu` — a **measured** case, X-C21 —
needs seven 64-bit names, and a table with seven 64-bit names has 32 bits left over
for every narrower name in the entire ISA. **This proposal's nameable set is the union
of all such tables**, because the width is not in the table; it is in the instruction.

That is the same claim the 512-bit rule makes in the user's own words — "It could be
16 4-byte regs, 64 1-byte regs, or **ANY combination**" (#232) — and carrying the
width in the instruction is the only mechanism in this design space that delivers
*any* combination without a per-function map.

---

## §3 THE ENCODING

### 3.1 The observation the format is built on

Fixing the nameable set at 8 names per namespace means a register operand needs only
**3 bits, not 5**. RISC-V's `rd`, `rs1` and `rs2` fields are 5 bits each, so the
format has **6 spare bits already inside the standard register fields** and does not
have to find them elsewhere. Add `funct3` (3 bits) and that is 9 spare bits — exactly
the 3 lanes × 3 bits the worst case (B-width) needs.

The second observation: **RISC-V already puts a 2-bit width at `inst[26:25]`.** In
OP-FP, `funct7` decomposes as `funct5` (`inst[31:27]`) : `fmt` (`inst[26:25]`), with
`fmt` = 00 S, 01 D, 10 H, 11 Q; the R4-type FMA instructions use the identical split
with `rs3` in `inst[31:27]`. The format below is **that layout with `fmt` re-read as a
slice width**. Nothing about the field positions is novel.

The third: **RISC-V already carries a width in `funct3` for every load and store** —
`lb`=000, `lh`=001, `lw`=010, `ld`=011, and `funct3[2]` the unsigned flag. That is the
task's suggested precedent and it is exact; §6.1 explains why this proposal needs the
two width bits and *not* the unsigned bit.

### 3.2 Format **NW-R** — three operands, register–register

Opcode: **custom-1**, `0b0101011` (`0x2b`). Tier 4 already reserves custom-0 (`0x0b`)
for the twelve NMFC instructions and its header says custom-1 "is left free for a
second reservation" (`nmfc_isa.h:18`). This is that reservation.

```
 31    27 26 25 24 23 22 20 19 18 17 15 14  12 11 10 9  7 6      0
+--------+-----+-----+-----+-----+-----+------+-----+----+--------+
|   fn   |  w  |l2lo | n2  |l1lo | n1  |funct3|ldlo | nd | opcode |
|   5    |  2  |  2  |  3  |  2  |  3  |  3   |  2  |  3 |   7    |
+--------+-----+-----+-----+-----+-----+------+-----+----+--------+
                                          |
                     funct3 = { ld[2], l1[2], l2[2] }   -- the third lane bit of each
```

`5 + 2 + 2 + 3 + 2 + 3 + 3 + 2 + 3 + 7 = 32`. The budget closes exactly.

| field | bits | meaning |
|---|---|---|
| `opcode` | `[6:0]` | custom-1 |
| `nd` | `[9:7]` | destination anchor name — the **low 3 bits of RISC-V's `rd` field** |
| `ld[1:0]` | `[11:10]` | destination lane, low 2 bits — the spare 2 bits of `rd` |
| `funct3` | `[14:12]` | `{ld[2], l1[2], l2[2]}` — high lane bit of each operand |
| `n1` | `[17:15]` | source-1 name — low 3 bits of `rs1` |
| `l1[1:0]` | `[19:18]` | source-1 lane, low 2 bits — spare 2 bits of `rs1` |
| `n2` | `[22:20]` | source-2 name — low 3 bits of `rs2` |
| `l2[1:0]` | `[24:23]` | source-2 lane, low 2 bits — spare 2 bits of `rs2` |
| `w` | `[26:25]` | 00 = B(8), 01 = H(16), 10 = W(32), 11 = D(64) — OP-FP's `fmt` position |
| `fn` | `[31:27]` | function code — R4-type's `rs3` position |

**Why the names sit at the low bits of `rd`/`rs1`/`rs2`:** the tile's existing
register-read wiring is indexed from those exact positions, so the base tier and the
NW tier share one operand-select path. The disassembler and `-ffixed-x{n}` reasoning
(M13) also keep working, because a name's bit position never moves with the width.

**Why the lane's high bit is in `funct3`:** it is the only 3-bit field left, and
scattering an element index across non-adjacent encoding bits is exactly what AArch64
does with the H:L:M bits of its by-element SIMD forms (§16.4). The alternative — fusing
name and lane into one 6-bit slice index per operand, `offset = index·w` — covers an
identical set of slices in identical space and is one gate cheaper; it was not chosen
because the name's field position then moves with the width, which costs the two
properties in the paragraph above.

### 3.3 The elastic-`fn` rule — how one opcode carries the whole tier

`fn` at 5 bits is 32 function codes. That does not hold RV64I's ten ALU ops, M's eight,
F/D's twenty-odd, the immediate forms, the loads and the conversions. There are two
ways to pay, and the proposal takes the first:

**(a) Elastic `fn` — recommended, costs one opcode.** The lane fields are only fully
used at *w* = 8. Define three 3-bit **lane wells**

```
  Wd = { inst[14], inst[11], inst[10] }
  W1 = { inst[13], inst[19], inst[18] }
  W2 = { inst[12], inst[24], inst[23] }
```

and let `k` = the `w` field value (0 = B … 3 = D). Then

```
  lane_d = Wd[2-k : 0]     lane_1 = W1[2-k : 0]     lane_2 = W2[2-k : 0]
  fn     = { inst[31:27], Wd[2 : 3-k], W1[2 : 3-k], W2[2 : 3-k] }      -- 5 + 3k bits
```

At *w* = D no lane bits are needed and all nine well bits join `fn`:

| *w* | lane bits/operand | `fn` width | function codes |
|---|---|---|---|
| B (8) | 3 | 5 | 32 |
| H (16) | 2 | 8 | 256 |
| W (32) | 1 | 11 | 2 048 |
| D (64) | 0 | 14 | 16 384 |

The allocation is principled rather than opportunistic: **narrow slices are packed data
being crunched and need a lean core of arithmetic; wide slices are where addresses,
constants, floating point and conversions live, and that is exactly where the codes
appear.** Decode cost: three 4:1 muxes on 3-bit fields, all combinational on
instruction bits.

`fn[4:0]` — the **core**, present at every width — is the set available unconditionally:

| `fn[4:0]` | ops | count |
|---|---|---|
| 0–9 | `add sub sll slt sltu xor srl sra or and` | 10 |
| 10–17 | `mul mulh mulhsu mulhu div divu rem remu` | 8 |
| 18–27 | reserved | 10 |
| 28 | **ESC0** — slice move / load-immediate (§3.4) | 1 |
| 29 | **ESC1** — slice load / store | 1 |
| 30 | **ESC2** — floating point, FMA, conversions | 1 |
| 31 | **ESC3** — reserved | 1 |

Higher `fn` bits (present only at *w* ≥ H) select **banks** of 32 codes each; bank 0 is
the core above, and the immediate forms (§3.5) live at bank ≥ 1.

**(b) A second custom major opcode — the simpler alternative, priced at §4.** Fix the
lane fields at 3 bits, keep `fn` = 5 everywhere, and put the immediate/FP/load formats
in a second opcode. Field positions then never move and the decoder is trivially
simpler. The cost is that custom-0 is taken and custom-1 is the header's declared
second reservation, so the second opcode must be custom-2 or custom-3 — **both of
which the RISC-V spec earmarks for RV128 and which `nmfc_isa.h:19` deliberately
avoids.** Spending one forfeits that headroom.

### 3.4 The escape formats

Each escape re-reads the same 32 bits under its own field layout. The payload is 25
bits (32 minus the 7-bit opcode), of which the escape spends 5 on the `fn` core code
that selected it, leaving **20 bits** for the escape's own fields. The point of the
table is that every escape closes inside those 20 at the widths it is legal at; the
exact sub-field assignments are implementation choice (X-C24).

| escape | what it is | operands | bits used (of 20) at B / H / W / D |
|---|---|---|---|
| **ESC0a `SMOV`** | slice move with **two** widths: `(nd,ld,wd) ← ext/trunc (n1,l1,ws)` | 2 slices, 2 widths, sign flag, 3-bit sub-op | **20 / 18 / 16 / 14** — fits at every width, exactly at B |
| **ESC0b `SLI`** | immediate → slice | 1 slice, width, sub-op; rest is immediate | immediate is **11 / 12 / 13 / 14** bits |
| **ESC1 `NLD`/`NST`** | slice ↔ memory; address is a D-anchor, *w* is both the memory and the slice width | dest slice, addr name, width, 2-bit sub-op; rest is offset | offset is **7 / 8 / 9 / 10** bits signed (±64 B … ±512 B) |
| **ESC2a FP 3-op** | `fadd fsub fmul fdiv fsgnj{,n,x} fmin fmax feq flt fle`, with static `rm` | 3 slices, width, `rm`, 3-bit op | needs 26 / 23 / **20** / **17** — **fits only at W and D**, which is exactly where F/D has types |
| **ESC2b FMA** | `fmadd fmsub fnmadd fnmsub`, R4-type | 4 slices, width, 2-bit op | needs 28 / 24 / **20** / **16** — fits at W and D; `rm` (3 more bits) fits **only at D**, so W-width FMA is RNE-only |
| **ESC2c `fcvt`/`fsqrt`/`fclass`** | 2-operand; `fcvt` carries two widths | 2 slices, 2 widths, sub-op | same budget as `SMOV`: fits at every width |

Two things fall out of that table rather than being imposed. **FP forms fit at exactly the
two widths RISC-V has float types for** — `f32` and `f64`; there is no `f16` in
`RV64IMAFD` and no `f8` anywhere — so §6.4's reservation of FP codes at *w* ∈ {B, H} is
what the encoding was going to do anyway. And **`SMOV` fits at every width with zero
slack at B**, which is the binding constraint on the whole escape design: it is why the
sub-op field is three bits and not four.

**`SMOV` is the workhorse and deserves naming.** Every operand of NW-R shares one
width, so a width-*changing* move is not expressible in NW-R; `SMOV` is the one
instruction that is, and it is the whole repacking mechanism: it is `MOVSX`/`MOVZX` on
x86, `SXTB`/`UXTH`/`UMOV`/`INS` on AArch64, and `pack`/`packh`/`sext.b`/`zext.h` in
Zbkb/Zbb (F-C14). Without it the tier is unusable; with it, repacking is one
instruction per field.

### 3.5 Immediate forms (`NW-I`)

Two operands and an immediate, at bank ≥ 1, with the third name field and the third lane
well reclaimed for the immediate:

| *w* | lane bits | immediate | notes |
|---|---|---|---|
| B (8) | 3 | **3 bits** | exactly a byte shift amount — `slli.b`, `srli.b`, `srai.b`. A general `addi.b` is technically encodable and nearly useless |
| H (16) | 2 | **5 bits** | covers shift amounts (4 bits) and small constants |
| W (32) | 1 | **7 bits** | shift amounts (5 bits) and ±64 |
| D (64) | 0 | **9 bits** | **not needed** — at D width the whole anchor is a stock 64-bit register and the base tier's `addi`/`slli` with `imm12`/`shamt6` already covers it |

**The immediate grows with the width, and that is the right direction**: wide slices hold
addresses and constants and are served by the base tier's full `imm12`; narrow slices hold
packed data and are served by shift amounts, which is what a 3-bit field is exactly the
size of. A constant too wide for its field costs one `SLI` (§3.4), which is
loop-invariant in every case that matters.

### 3.6 Worked examples

```
  add.b   x3[5], x1[2], x7[0]     ; anchor 3 byte 5  <-  anchor 1 byte 2 + anchor 7 byte 0
                                  ;   w=00  nd=3 ld=5  n1=1 l1=2  n2=7 l2=0  fn=0
                                  ;   offsets: 232, 80, 448 -- all by shift, no lookup

  add     x3, x1, x7              ; base tier, D width, whole anchors, stock encoding
  add.d   x3[0], x1[0], x7[0]     ; the identical operation in the NW tier

  smov    x2[1]:w  <- x5[3]:h, u  ; zero-extend anchor 5 halfword 3 into anchor 2 word 1
  fadd.d  f4[0], f4[0], f6[0]     ; ESC2a, rm = RNE
  fmul.w  f1[1], f1[1], f2[0]     ; a 32-bit float in the HIGH half of anchor 1 --
                                  ;   inexpressible in base F/D at any encoding (§7.2)
```

The last line is the point of the whole proposal. `fmul.s f1, f1, f2` in base `F`
operates on the low 32 bits of an FLEN-64 register and NaN-boxes the upper 32 (F-C4),
so it cannot address the high half and destroys it on write. The NW tier addresses it
by lane.

---

## §4 OPCODE-SPACE COST, COUNTED

RISC-V's 32-bit encoding space is `inst[1:0] = 11` with `inst[6:2]` selecting one of
**32 major opcodes**. Four are architecturally reserved for custom use:

| space | opcode | status here |
|---|---|---|
| custom-0 | `0b0001011` | **taken** — the twelve NMFC instructions (`NMFC_OPCODE 0x0b`) |
| custom-1 | `0b0101011` | **this proposal takes it** — `nmfc_isa.h:18` reserved it for exactly this |
| custom-2/rv128 | `0b1011011` | free, but RV128-earmarked; `nmfc_isa.h:19` avoids it on purpose |
| custom-3/rv128 | `0b1111011` | same |

**Cost of variant (a), the recommended one: one major opcode = 1/32 = 3.125% of the
32-bit encoding space, and it is the one the tree already set aside.** Custom-2 and
custom-3 remain untouched, so the RV128 headroom the header preserved survives.

**Cost of variant (b): two major opcodes = 6.25%, and the second must be RV128-earmarked
space.**

Inside custom-1, variant (a) uses:

| region | encodings | used by |
|---|---|---|
| `w` = B | 32 | 18 core ops + 4 escapes + 10 reserved |
| `w` = H | 256 | core + 7 banks (immediates, extended integer) |
| `w` = W | 2 048 | core + 63 banks (immediates, FP, FMA, loads, conversions) |
| `w` = D | 16 384 | core + 511 banks (as W, plus imm12 forms) |

**Nothing is taken from `funct7` groups `0x6`/`0x7` of custom-0**, which X-C23 reserves
for `KILL`, mailboxes and `RESUME`. This proposal does not compete with that budget at
all; it spends major-opcode space, which no constraint currently rations but which is
finite and which §18 asks the user to rule on.

---

## §5 THE PROOF THAT NO STATE IS CONSULTED

X-C1 and X-C2 are the hard constraint: nothing outside the 512-bit context and the
instruction encoding, and specifically no *third* object referenced at decode time.

**Operand resolution, in full:**

```
    k      = inst[26:25]                       -- 2 bits, from the instruction
    w      = 8 << k                            -- a decode of those 2 bits
    name   = inst[9:7] | inst[17:15] | inst[22:20]     -- 3 bits each, from the instruction
    lane   = {funct3 bit, 2 spare bits}        -- 3 bits each, from the instruction
    offset = (name << 6) | (lane << (3 + k))   -- one shift, one OR
```

Every input is a field of the instruction word. The output addresses the context. There
is no index, no table, no cache, no CSR, no mode register, no memory reference, and no
value carried across instructions. **Three objects are referenced at decode: the
instruction, the context, and (for loads/stores only) the datum — which is the same
count as a stock RISC-V core and one fewer than the rejected mechanism.** The comparison
in that last clause is stated as arithmetic, not as an argument; see §14.4 and X-C32.

**Bits of state added by this proposal, exhaustively: zero.** Per function: zero. Per
context: zero — the context is 512 bits before and after. Per tile: zero, and the
resident-function table's `RegLayout` entry is *deleted* (M1). The map is wires in the
decoder, which is area, not state, and area does not migrate.

---

## §6 SEMANTICS

### 6.1 Reading a slice

An operand read delivers the *w* bits at `offset` into a *w*-wide datapath. **The ALU
executes at width *w*, modulo 2^*w*, and there is no extension bit in the encoding.**

The reason is that RISC-V already puts signedness in the opcode wherever it matters:
`srl`/`sra`, `slt`/`sltu`, `div`/`divu`, `rem`/`remu`, `mulh`/`mulhu`/`mulhsu`. For
`add`, `sub`, `and`, `or`, `xor`, `sll` and `mul`-low, the result's low *w* bits do not
depend on how the operands were extended, so an extension bit would be dead encoding.
This is why the load/store `funct3` precedent (F-C14) contributes only its **two width
bits** here and not its unsigned bit: a load's destination is wider than its source and
no later opcode can disambiguate, whereas an NW operand's destination is exactly *w*.

`SMOV` is the exception and carries an explicit sign/zero flag, because it is the one
instruction whose destination width differs from its source width (§3.4).

**This is the substitute rule F-C11 demands.** Zfinx dropped NaN-boxing and replaced it
with "ignore bits above *w* on read, sign-extend above *w* on write" because its
registers hold both integer and float operands. Here there *are* no bits above *w* — a
slice is exactly *w* bits — so the read half is trivially satisfied and the write half
is answered by §6.2. An `f32` in a 32-bit slice therefore needs no NaN-boxing (F-C11),
and the upper-bit rule is not left unspecified.

### 6.2 Writing a slice — **PRESERVE**

**A *w*-bit write modifies exactly the *w* bits at `offset` and leaves every other bit
of the context unchanged.**

F-C19/C20 and facts §6.3 require this document to state one explicit rule and note that
three ratified mechanisms will otherwise silently clobber a packed neighbour. Of the
three candidates:

| rule | prior art | effect here |
|---|---|---|
| **zero** the rest of the name | AArch64 `Wn`, x86 `EAX` | destroys the neighbouring slice |
| **sign-extend** over the rest | RV64 `addw`/`sext.w`, Zfinx narrow results | destroys the neighbouring slice |
| **preserve** the rest | x86 `AL`/`AX`, Motorola 68000 `MOVE.B`/`.W` | correct |

**Only preserve is compatible with bit-packing**, because the neighbouring slice is a
live value and the whole premise is that it is. Zero and sign-extend are the RV64
sign-extension invariant (F-C13) doing exactly what facts §6.3 warns it will do, and
that invariant is in direct opposition to this design; adopting preserve is how the
opposition is resolved, and it is the deliberate departure the record needs recorded.

**The hazard preserve carries, and why it lands softer here.** x86's preserve-on-narrow
-write produced partial-register merge stalls and, later, dedicated merging micro-ops
(F-C19). That hazard is a consequence of **register renaming**: a narrow write and a
wide read of the same physical register must be merged, and the merge is a µop. The
NMFC tile has **no rename, no ROB and no speculation** (X-C13, ruling O12), so the merge
is a byte-enabled write into a 512-bit SRAM line — 8 enables per anchor, 64 per context,
and §1.3 property 2 guarantees byte granularity suffices. The 68000 carried the same
preserve rule for two decades with no such stall, for the same reason: nothing to rename.

**The residual cost, stated rather than waved away.** If the tile forwards results
between pipeline stages, forwarding must become byte-granular: a *w*-bit result merging
into a 64-bit consumer needs 8 byte-muxes per forwarding path instead of one 64-bit mux.
Whether that cost is real depends on the tile's pipeline — a barrel-threaded core whose
context's instructions are ≥ W cycles apart may forward nothing at all. **The record
does not fix the tile's pipeline depth, so this is priced conditionally and flagged as
needing the tile spec.**

### 6.3 `x0`, and the one wart

`x0` must keep reading zero at the base tier: `nop` is `addi x0,x0,0`, `ret` is
`jalr x0,ra,0`, `j` is `jal x0,imm`, and every discard idiom and the whole HINT space
depend on it (F-C17). X-C10 keeps the base ISA, so the base ISA's `x0` comes with it.
M5 asks what happens to anchor 0 and whether `f0` follows.

**Ruling proposed:**

- **Base tier:** `x0` reads zero and discards writes, exactly as ratified. `f0` reaches
  anchor 0's real bits — the FP file has no hardwired-zero register (F-C17), so nothing
  is broken by this and anchor 0 is not stranded.
- **NW tier:** name 0 is anchor 0 in **both** namespaces. The NW encodings are not base
  encodings and no `nop`/`ret`/`j` idiom reads them, so nothing needs a zero source.

**This is a wart and it should be named as one.** `x0` and `f0` denote the same 64 bits
but behave differently, and name 0 behaves differently in the two tiers. The exact
precedent is x86-64, where the byte-register field value 4 names `AH` without a REX
prefix and `SPL` with one — **the same field value naming different bits depending on an
encoding bit**, shipping since 2003. The alternative — anchor 0 reads zero in both
namespaces and both tiers — strands 64 bits, 12.5% of the context, and is rejected on
X-C3 grounds.

`SMOV` and `SLI` give anchor 0 a full complement of moves, and under aliasing
`fmv.d.x f0, x3` is a legal base-tier 64-bit move from anchor 3 to anchor 0 (§7.2).

### 6.4 Reserved encodings

Every reserved encoding raises **illegal-instruction**, which on this machine is the
`NMFCTile::illegal` fatal path (M6). Reserved, exhaustively:

1. `lane` ≥ 64/*w* in any operand — Zdinx's misaligned-pair rule (F-C16).
2. Base-tier register names 8–31 in either namespace (§7.1).
3. FP function codes at *w* ∈ {B, H} — there is no `f16` (Zfh is outside `RV64IMAFD`)
   and no `f8` in any RISC-V extension.
4. `rm` = 111 (DYN) in any FP encoding — it means "read `fcsr`", and X-C8 forbids an
   `fcsr`. §6.5.
5. Unallocated `fn` codes.

All five are decided by comparing instruction bits against constants. None reads state.

### 6.5 Rounding

FP encodings carry a **static 3-bit `rm`** at D width and are **RNE-only** at W width
(§3.4, ESC2b). `rm` = DYN is reserved per §6.4 item 4. This is X-C8 enforced by the
encoding rather than asserted in prose: with no `fcsr` there is nothing for DYN to read,
and *"a function needing dynamic rounding modes is in the same position as a function
that spills: it cannot be offloaded"* (I.7 item 3) becomes a decode-time illegal
instruction instead of undefined behaviour.

There is also no FP exception state, per X-C8. Accrued flags are simply not recorded;
a function that inspects `fflags` uses a CSR access, which X-C10 already rejects.

### 6.6 The packing lemma

> **Lemma.** Any multiset of widths drawn from {8, 16, 32, 64} whose sum is ≤ 512 packs
> into the eight 64-bit anchors with every value naturally aligned and **zero bits
> wasted**.

*Proof.* Place values in descending width order. Every width divides 64 and every width
divides every larger width, so after all values of width *v* are placed, the free space
in each partially-filled anchor is a multiple of *v* and *v*-aligned. Hence the next
(smaller, dividing) width always finds an aligned hole while total free space remains.
Greedy descending therefore wastes nothing while the sum is ≤ 512. ∎

This is buddy allocation, and it is the property that makes the fixed anchor grid cost
nothing in packing density. Its consequences are §10 (the admission test stays a sum of
bits) and §15 (the price against the rejected per-function map is exactly the
round-up-to-a-power-of-two, and nothing else).

The two measured functions (X-C21): `nmfc_bu` at 8 values / **480 bits** packs with 32
bits of slack; `nmfc_expand` at 8 values / **384 bits** packs with 128 bits of slack.
Both fit for any width distribution drawn from {8,16,32,64}, by the lemma, without
needing to know which distribution it is.

---

## §7 WHAT HAPPENS TO THE BASE TIER

The base tier is `RV64IMAFD` executed on the anchors at D width. It is not decoration:
it is where addresses, branches, `lui`/`auipc`, and all 64-bit arithmetic live, and it
is what lets a stock toolchain emit most of a function (§10.3, M13).

### 7.1 Register names 8–31 are illegal

There are eight anchors. `add x9, x1, x2` names an anchor that does not exist and raises
illegal-instruction. This is the re-homing of M6/X-C14: `RegLayout`'s "a register the
layout does not define is a hard error, not a silent zero" survives verbatim, with the
test changed from a table lookup to `name >= 8`.

RV64E is the precedent, ratified: it "reduce[s] the integer register count to 16
general-purpose registers, (`x0`–`x15`)", and its stated motivation is *"to reduce
context state for highly threaded 64-bit processors"* (F-C17) — the same motivation,
one halving further along.

### 7.2 The base instructions that change meaning or leave

| instruction | status | reason |
|---|---|---|
| `add`, `sub`, `sll`, `slt`, `xor`, `srl`, `sra`, `or`, `and`, and the `i` forms | **kept**, D width, whole anchors | operate on 64-bit names, which anchors are |
| `mul`, `div`, `rem` and family | **kept**, D width | same |
| `lui`, `auipc`, `jal`, `jalr`, `beq`…`bgeu` | **kept** — but `jal`/`jalr` writing a link register is already forbidden by X-C9/I7 | `jal x0, imm` (an unconditional jump discarding the link) stays legal and is how branches beyond ±4 KiB are taken |
| `ld`, `sd` | **kept** — a full-anchor load/store | 64-bit memory access, 64-bit destination |
| `lb`, `lh`, `lw`, `lbu`, `lhu`, `lwu` | **kept, but they write a WHOLE anchor** | their ratified semantics sign- or zero-extend to 64 bits (F-C13/C14). They cannot land in a sub-D slice; that is `NLD` (ESC1), and without ESC1 a narrow load costs a scratch anchor and an `SMOV` (§15.3) |
| `sb`, `sh`, `sw` | **kept** — they store the low *n* bits of an anchor | matches lane 0 only; other lanes need `NST` |
| `addw`, `subw`, `sllw`, `srlw`, `sraw`, `addiw`, `slliw`… | **REMOVED** | F-C13: they sign-extend a 32-bit result over bits 63:32, which **destroys lane 1** of the anchor. Their entire purpose is to make bits 63:32 redundant copies of bit 31, which is the exact opposite of packing. 32-bit arithmetic is `add.w x3[l], …` in the NW tier |
| `fmv.d.x`, `fmv.x.d` | **kept, redefined** as a 64-bit anchor-to-anchor move; identity when the two numbers match | F-C1: they move raw bits with no conversion. Under aliasing `fmv.d.x f0, x3` copies anchor 3 to anchor 0, which is how anchor 0 is written from the integer side (§6.3). This is a *departure from Zfinx*, which **removes** the transfer instructions (F-C15); the reason to keep them is `f0` |
| `fmv.w.x`, `fmv.x.w` | **REMOVED** | F-C15, in the most damaging possible way: `fmv.x.w` sign-extends bits 31:0 over 63:32 and `fmv.w.x` writes all 1s to 63:32 (NaN-boxing, F-C3/C4). Both silently overwrite lane 1 of a packed anchor. Zfinx removes them too, and recovers the encoding space (F-C15) |
| `fadd.d`, `fmul.d`, … (`.d` forms) | **kept**, D width, whole anchors | FLEN = 64 = anchor width; no boxing question arises |
| `fadd.s`, `fmul.s`, … (`.s` forms) | **REMOVED** | F-C4's *write* half: any operation writing a narrower result to an `f` register must write all 1s to the upper FLEN−*n* bits. That is a guaranteed clobber of lane 1. F-C4's *read* half is worse — an `f32` that arrived by a non-transfer path with arbitrary upper bits is **silently read as canonical NaN**, not trapped. 32-bit FP is `fadd.w f3[l], …` in the NW tier |
| `flw`, `fsw` | **REMOVED** | `flw` NaN-boxes on load (same clobber); `fsw` stores the low 32 with no lane selection |
| `fld`, `fsd` | **kept** — whole-anchor FP load/store | |
| CSR access, `FENCE`, `ecall`, compressed encodings, anything from `V` | **already excluded** | X-C10, X-C11 — unchanged by this proposal |

**Five ratified instructions are removed and the reason is the same each time:** they
enforce the RV64 sign-extension invariant or the NaN-boxing invariant, both of which
canonicalise a 64-bit register around one 32-bit value and both of which are the direct
opposite of bit-packing. Facts §6.3 predicted exactly this — *"three separate ratified
mechanisms will silently overwrite the upper half of a packed 64-bit name"* — and the
answer is to delete them, not to hope. Their work is done by the NW tier at *w* = W,
which addresses both halves by lane.

Removing them is a use of X-C16 ("cannot be expressed ⇒ cannot be offloaded"), but a
mild one: **no function loses expressiveness**, because every removed instruction has a
strictly more capable NW-tier replacement. What is lost is the ability to drop
stock-compiled object code in unchanged (§17).

### 7.3 The two tiers are one instruction stream

There is no mode, no prefix and no switch. The base tier and the NW tier are
distinguished by the major opcode, exactly as OP and OP-FP are, and a function
interleaves them freely. **That is not incidental — a mode bit would be per-context
state and would violate X-C1 as squarely as a table does.**

---

## §8 DECODE AND EXECUTION CHANGES (what tier 4 becomes)

### 8.1 `NMFCRegLayout.h`

`struct RegField` and `struct RegLayout` are **deleted**. So is
`RegLayout::defaultLayout()`, and note that deleting it is a *change*, not just a
removal: `defaultLayout()` maps `x1`–`x8` to offsets 0–448 with `x0` reading zero, which
is **off by one** from this proposal's `x0`–`x7` at offsets 0–448. The tier-4 default
layout is not this proposal's map and must not be mistaken for it.

`struct Context512` **survives unchanged as storage mechanics** (M3) — it is the 512
bits and the extract/insert arithmetic over them. Its straddle path (`word[w0+1]`,
both branches) becomes **dead code** by §1.3 property 1 and should be replaced by an
assertion that `offset % width == 0`, so that the dead path cannot silently come back.

### 8.2 `NMFCTile::readReg` / `writeReg`

Today (`NMFCTile.cc:460-475`):

```c
uint64_t NMFCTile::readReg( TileContext& c, uint32_t r ) const {
  if( r == 0 && layout_.hasZero ) return 0;
  if( !layout_.defines( r ) ) illegal( c, 0, "reads a register the function's layout does not define" );
  return c.regs.read( layout_.field[r] );          // <- the table indirection
}
```

Under this proposal the signature gains the two fields that already exist in the
instruction, and the table disappears:

```c
uint64_t NMFCTile::readSlice( TileContext& c, uint32_t name, uint32_t lane, uint32_t k ) const {
  // k is inst[26:25]; width = 8 << k; lane must be < 64 >> (3+k)
  if( lane >= ( 8u >> k ) )  illegal( c, 0, "names a lane the width does not have" );
  const RegField f{ uint16_t( ( name << 6 ) | ( lane << ( 3 + k ) ) ), uint8_t( 8u << k ) };
  return c.regs.read( f );
}
```

`RegField` becomes a *value computed from the instruction* rather than a *row read from
a table*. That is the entire architectural change on the decode path, and it is why §5's
proof is three lines long.

`name >= 8` is caught at decode (§7.1) rather than here, because in the NW tier the name
field is 3 bits and cannot express it; only the base tier's 5-bit fields can.

### 8.3 `NMFCTile.h:448-450`

```c
  /// One entry per resident function (§25.7).
  RegLayout layout_;
```

is **deleted**, along with the resident-function table concept it implements. This is
M1, and it is the deletion the 2026-09-03 ruling requires be stated rather than left
standing as an alternative.

### 8.4 What the decoder gains

- a second major-opcode arm for custom-1;
- three lane-well splitters (three 4:1 muxes on 3-bit fields) for the elastic-`fn` rule;
- three offset computations, `(name << 6) | (lane << (3+k))`, each one shift and one OR;
- a *w*-wide ALU path, i.e. an ALU whose carry chain can be broken at 8/16/32-bit
  boundaries — the standard SIMD segmented-adder trick, or four narrower adders;
- byte enables on the context write port (8 per anchor, 64 per context).

**It gains no memory, no CSR, no table and no port.**

### 8.5 Appendix 2 bookkeeping (M15)

Divergence `S5` — "the bit-level admission test is never exercised because nothing
produces a non-default layout" — **closes**, because there are no layouts to produce.
It is replaced by a new divergence of the same shape: *the tile decodes only the base
tier; the NW tier is specified and unimplemented.* A new ledger entry of `L45`'s shape
records that DESIGN §25.7 D:2560-2564 is overruled (X-C31).

---

## §9 HOST-SIDE RETRIEVAL

**`V` is not available** — not in the subset (X-C10), not implemented by Rev, not by
Vanadis, and RoCC carries only 128 bits (X-C11, I.8's prior-art check). So `vmv.x.s`,
`vslidedown` and `vfmv.f.s` do not appear below, and facts F-C5/C7/C8 are cited only for
what they rule out. F-C8's memory path *does* survive, but it gets its data through
`CXR` rather than through a vector store; §9.3.

### 9.1 `CXW`/`CXR` are unchanged, and now they line up

`CXW cD, lane, rS` and `CXR rD, cS, lane` keep 64-bit lanes with the lane in `funct7`
bits 3:1 — three bits, eight lanes (`NMFC_CX_LANE_SHIFT/MASK`). M4 and context-file §11
question 5 ask whether that granularity changes: **it does not**, and the record's
"the lane is an access granularity, not the register's structure" is preserved.

What changes is that the two now **coincide**: `CXR`'s lane *n* is exactly anchor *n*.
Under the rejected per-function map the host had to know a function's layout to stage an
argument or read a result; under this proposal the grid is fixed by the ISA, so the host
knows where every slice is by construction. **This is a consequence of the proposal, not
a virtue the record asked for** (X-C32).

### 9.2 Extracting one field — 3 or 4 instructions

For slice (anchor *n*, lane *l*, width *w*), with `s = 64 − (l+1)·w`:

```
    CXR    t0, cS, n            ; 64 bits of anchor n into a host GPR
    slli   t0, t0, s            ; push the field to the top
    srli   t0, t0, 64-w         ; zero-extend   (or srai for sign-extend)
    fmv.d.x fa0, t0             ; only if the value is wanted in a host f register
```

This is I.8's promise made concrete — *"any packing within them is reached with the
shifts and masks RV64I already has"* (#233) — and X-C27 is satisfied without adding a
host-side bit-field instruction, which X-C26 closed.

Two cautions from the facts file apply on the host and only on the host:

- **`fmv.w.x` and `fmv.x.w` are legal and correct on the host** and illegal inside the
  function core (§7.2). The host has a real FLEN-64 `f` file, so NaN-boxing is the
  right behaviour there. The asymmetry is deliberate and should be stated in any ABI
  note: *the same mnemonic is required on one side of the boundary and forbidden on the
  other.*
- **`fmv.x.w` sign-extends bits 31:0 over 63:32 on RV64** (F-C3). Packing an `f32` bit
  pattern back into a slice must mask: `fmv.x.w t0, fa0 ; slli t0,t0,32 ; srli t0,t0,32`.

### 9.3 Reading everything — 16 instructions, then one load per field

```
    CXR t0, cS, 0 ; sd t0,  0(sp)
    CXR t0, cS, 1 ; sd t0,  8(sp)
    …                                    ; 8 x (CXR + sd) = 16 instructions
```

After that the context is 64 contiguous bytes — **exactly one cache block**, I2/H.3 —
and any field at any width is one ordinary `lb`/`lh`/`lw`/`ld`/`lbu`/`lhu`/`lwu` or
`fld` with correct sign or zero extension for free, at a constant offset the compiler
computes from `(n, l, w)`. This is F-C8's memory path, which the facts file correctly
identifies as the one a compiler will emit; the only correction is that it is fed by
`CXR` rather than by `vse64.v`, because there is no vector unit.

**Cost comparison, per join:** 3–4 instructions for the first field and each subsequent
one via §9.2; 16 instructions plus 1 per field via §9.3. The crossover is at 5–6 fields.

### 9.4 Staging arguments — the reverse

`CXW` writes a whole 64-bit anchor, so the host assembles an anchor in a GPR and writes
it once:

```
    slli t0, a0, 0 ; slli t1, a1, 32 ; or t0, t0, t1 ; CXW cD, 2, t0
```

An all-D-width context is 8 `CXW` and no shifts. A *k*-field anchor is roughly
`2k` instructions plus one `CXW`. M4's open question — *how does a host stage an
`f32`-in-a-32-bit-slot argument* — has a fixed answer for the first time: `fmv.x.w`,
mask (F-C3), shift into lane, OR, `CXW`.

---

## §10 ADMISSION — WHAT K.6 BECOMES

### 10.1 It stays a sum of bits

**The admission test is unchanged except for one rounding rule:**

> A function is admissible iff every opcode in it is in the ruled subset *and* its peak
> simultaneous liveness, **counting each value's width rounded up to the next member of
> {8, 16, 32, 64}**, is ≤ 512 bits.

By the packing lemma (§6.6), a rounded multiset summing to ≤ 512 always packs into the
anchor grid with zero waste, so **feasibility of the sum implies feasibility of an
assignment.** The two candidate readings the context file's §7 note distinguishes —
"does the packing fit in 512 bits" versus "is there an assignment of live values to
non-overlapping names" — **are provably the same test here**, and K.6 does not have to
choose. That is the single most useful consequence of restricting widths to powers of
two.

X-C17 survives verbatim: one pool, measured in bits, `f`-names and `x`-names competing
for the same bits. K.6's third wrong answer — separate `f` and `x` pools admitting a
function twice the legal size — remains wrong for exactly the reason it already was, and
this proposal makes it *structurally* impossible rather than merely forbidden, because
`f`*n* and `x`*n* are the same anchor.

### 10.2 Why this is not R30 in mirror image

X-C18 warns that a scheme whose admission test is "how many names does it use"
re-introduces R30, which reported 17 and 21 where the answer was 8 and rejected a
480-bit function that fits.

**This test counts bits, not names**, and the lemma is what licenses that. R30's error
was that partial-width views (`rax`/`eax`/`al`) were counted as three registers where
they are one; here the overlapping views are precisely what the counting *ignores* —
liveness is summed over values, and the name set is only checked for feasibility, which
the lemma discharges. A fixed *mixed-width* table would have re-introduced R30 in mirror
image (its test genuinely would have been a colouring over a fixed interference
structure); **carrying the width in the instruction is what avoids it**, because the
interference structure is not fixed.

The one place a name count reappears is **the D-width ceiling: there are exactly eight
64-bit names.** A function with nine live 64-bit values is inadmissible, but it is
inadmissible on bits too (9 × 64 = 576 > 512), so the two tests agree and no new
rejection is created.

### 10.3 The toolchain gate (M13) — with an honest wrinkle

DESIGN §24 step 5 names `-ffixed-x{n}` as the cheapest admission gate: constrain the
compiler to the register budget and *"a function that does not fit fails to build or
spills visibly"*, turning admission from post-hoc analysis into a build error.

**This proposal keeps that buildable, and it is the strongest practical argument for
aliasing over a per-function bit-map** — `-ffixed` works on register *names*, which this
scheme provides and a bit-map does not. `-ffixed-x8` … `-ffixed-x31` and `-ffixed-f8` …
`-ffixed-f31` express the nameable set exactly.

**The wrinkle, which must not be glossed.** Under the standard RV64 ABI, `x1`–`x7` are
`ra`, `sp`, `gp`, `tp`, `t0`, `t1`, `t2`. A stock GCC will freely allocate only `t0`–`t2`
as general registers. `gp` and `tp` can be freed (`-mno-relax` removes gp-relative
relaxation; `tp` is only needed for TLS), but `ra` and `sp` will not be allocated as data
registers by any stock back end — which is consistent, since X-C9 forbids both a link
register and a stack anyway, but it means **the practical stock-toolchain reach at D
width is five anchors (`gp`, `tp`, `t0`, `t1`, `t2`), not eight.**

The `f` namespace is better behaved: `f0`–`f7` are `ft0`–`ft7`, all caller-saved
temporaries, and a stock back end will allocate all eight. So `-ffixed-f8` … `-ffixed-f31`
gives the full anchor set to FP-typed values today.

**Conclusion, stated as a limit rather than a claim:** step 5 remains buildable and
remains the right first thing to try, but reaching all eight anchors from integer code
needs either a small ABI note (a `-mabi`-level statement that `ra`/`sp` are data
registers in an offloaded function) or the custom back end step 5 hoped to avoid. That
is a smaller ask than a custom back end for the whole NW tier, which is needed regardless
(§17).

### 10.4 `annotate.cc` (M7)

`tools/nmfc/annotate.cc:524-559` builds a pool of `opt.num_regs` (8) slot ids and
allocates one whole slot per live value, calling `die()` when the pool empties; the bit
computation at `:555-559` feeds only a stderr line at `:927`. This is ledger L30 and it
gates nothing.

Under this proposal the rewrite K.6 already requires is also *sufficient*: replace the
slot pool with `peak_bits ≤ 512`, and add the round-up to {8,16,32,64}. **No colouring
pass, no interference graph, no assignment search** — the lemma says the sum is the test.
That is a smaller rewrite than the one a fixed mixed-width table would have forced.

---

## §11 EVERY EXISTING MECHANISM (M1–M15)

| # | mechanism | what this proposal does to it |
|---|---|---|
| **M1** | resident-function table (`RegLayout layout_`) | **DELETED.** DESIGN §25.7 D:2560-2564 — "one small table entry beside the instruction cache" — is the "third piece of memory" the 2026-09-03 ruling names, and is overruled at tier 1. It is not left standing as an alternative. `NMFCRegLayout.h`'s `RegField`/`RegLayout`/`defaultLayout()` and `NMFCTile.h:448-450` become a recorded divergence, X-C31 shape (§8.5). |
| **M2** | `readReg`/`writeReg` decode indirection | Replaced by `readSlice`/`writeSlice` taking `(name, lane, k)` **from the instruction**. `layout_.field[r]` becomes `(name<<6)|(lane<<(3+k))` — a wiring change, not a table lookup (§8.2). |
| **M3** | `Context512::read`/`write` bit-offset extract/insert | **Reused unchanged** as storage mechanics. The straddle path is dead: no nameable slice crosses a 64-bit word or even a byte (§1.3). Replace it with `assert(offset % width == 0)` so it cannot silently return. |
| **M4** | `CXW`/`CXR`, 64-bit lane in `funct7[3:1]` | **Unchanged** — 8 lanes, lane in `funct7`, `NMFC_CX_LANE_SHIFT/MASK` untouched. The core names sub-64-bit slices while the host still addresses 64-bit lanes; the record's independence of the two is preserved. New: lane *n* ≡ anchor *n*, so a host stages an `f32` argument by `fmv.x.w` + mask + shift + `CXW` (§9.4). |
| **M5** | the `x0` rule | Base tier: `x0` reads zero, `f0` reaches anchor 0. NW tier: name 0 is anchor 0 in both namespaces. The question M5 poses — does `f0` follow `x0`? — is answered **no**, with the 64-bit cost of answering yes stated (§6.3). |
| **M6** | the illegal-register trap | **Re-homed, not lost.** Its purpose (X-C14: a function needing more than the file holds finds out immediately) moves to (i) decode — base names ≥ 8, `lane ≥ 64/w`, reserved `fn`, `rm`=DYN, all fatal via `NMFCTile::illegal`; and (ii) admission — a ninth 64-bit value fails to build under `-ffixed` (§10.3). Under a fixed grid every well-formed encoding is defined, so the *check* changes shape; the *behaviour* does not. |
| **M7** | `annotate.cc` admission tool | Becomes `peak_bits ≤ 512` in one pool, with each width rounded up to {8,16,32,64}. Stays a sum of bits; does not become an assignment or colouring problem, by the lemma (§6.6, §10.4). |
| **M8** | the `RETC`/`ENDC` return bit | **Unchanged, and I2 survives.** The ISA fixes the *grid* (anchors, natural alignment, power-of-two widths); it does not fix *which value lives in which slice*, which stays the function's ABI exactly as today. So "register positions carry no meaning across the boundary" is intact: the joiner learns the geometry, never the contents. What it gains is that extraction is a fixed 2-shift idiom (§9.2) rather than an arbitrary one — a narrowing of what the host must know, not a widening of what the ISA promises. |
| **M9** | `JOIN` as read-modify-write | **Unaffected.** `cDST_new = ok ? ftu_payload : cDST_old` moves 512 bits and never inspects them. Confirmed, not assumed. |
| **M10** | `CONT` / `CONT.M` | **A genuine simplification.** A successor inherits the same map for free, because there is no map. Under M1 a successor running a *different* function needed a different table entry loaded before its first instruction decoded; here nothing is loaded. |
| **M11** | migration = 64 B context + 8 B PC = 72 B | **Exactly 72 B, unchanged.** The proposal adds zero bits of per-context state (§5, §14.1), so there is nothing new to carry. Confirmed by construction, not assumed. |
| **M12** | the two hosts, RoCC's 128-bit operand path | **Unaffected.** Every NMFC operand remains a value in a GPR and a context register remains named by a number in a GPR. The NW tier names the *function core's* slices inside the function core's own instruction stream; **the host never names a slice**, it names a 64-bit lane (§9). RoCC's `RoCCCommand(rs1, rs2)` / `RoCCResponse(rd, rd_val)` shape is untouched. |
| **M13** | `-ffixed-x{n}` as the admission gate | **Kept buildable** — this is aliasing's advantage over a bit-map, since `-ffixed` works on names. With the honest wrinkle that the stock ABI reaches five of eight anchors from integer code and eight of eight from FP code (§10.3). |
| **M14** | encoding space | `funct7` groups `0x6`/`0x7` of custom-0 are **untouched** and remain available for `KILL`, mailboxes and `RESUME`. This proposal spends **major-opcode** space instead: custom-1, which `nmfc_isa.h:18` reserved for a second reservation (§4). Per X-C24 no field value here is canon. |
| **M15** | Appendix 2 `S5`/`S6` | `S5` **closes** — there are no layouts to produce, so "the bit-level admission test is never exercised" is no longer a divergence. It is replaced by "the tile decodes only the base tier; the NW tier is specified and unimplemented" (§8.5). |

---

## §12 EVERY CONSTRAINT (X-C1 – X-C32)

| # | constraint | verdict |
|---|---|---|
| **C1** | no state outside the 512 bits and the encoding | **MET** — §5's five-line proof. Zero bits of state added at every level. |
| **C2** | no third referenced object at decode | **MET** — instruction + context, plus the datum for loads only. The answer is not "the map is on a duplicate page"; there is no map. |
| **C3** | 512 bits, bit-packed, not eight registers | **MET in substance, and this is the constraint to check hardest.** The *names* are eight 64-bit anchors, but the *nameable slices* are 120 at four widths, a 32-bit value costs 32 bits and sixteen fit, and any combination of {8,16,32,64} widths packs with zero waste (§6.6). §2 gives the discriminating test against X-C30. |
| **C4** | the file may not be widened | **MET** — 512 bits, unchanged. |
| **C5** | 512 in / 512 out, PC not among them | **MET** — the proposal touches naming only; the PC is beside the file as before. |
| **C6** | migration is exactly 72 B | **MET** — M11. |
| **C7** | one file, two namespaces, no separate FP file | **MET, and made structural.** `f`*n* and `x`*n* are the same anchor by ISA definition, so a separate FP file is not merely forbidden, it is unrepresentable. This is Zfinx's ratified structure (F-C9). |
| **C8** | no `fcsr`, no rounding-mode state, no FP exception state | **MET, and enforced by the encoding** — `rm` is static and `rm`=DYN is a reserved encoding (§6.4, §6.5). "Cannot be offloaded" becomes an illegal instruction rather than undefined behaviour. |
| **C9** | no stack, a function that spills cannot run | **MET** — no spill slot is created by another name. The nearest thing is scratch-slice pressure (§17.1), whose remedy is X-C16, never a stack. |
| **C10** | the base ISA is `RV64IMAFD` and nothing else | **NOT MET — this is the ask.** The proposal adds an instruction tier and removes five ratified instructions (§7.2). §18 puts it to the user as the first ruling required. |
| **C11** | no vector extension on either host | **MET** — §9 uses `CXR` + RV64I shifts + `fmv.d.x`. `vmv.x.s`/`vslidedown`/`vfmv.f.s` appear nowhere in any path. |
| **C12** | nothing blocks | **MET** — naming has no bearing on it; no new blocking structure. |
| **C13** | nothing speculative | **MET, and load-bearing.** The absence of rename is exactly what makes preserve-on-narrow-write safe (§6.2). The proposal adds no ROB, rename, LSQ or speculation. |
| **C14** | an undefined register is a hard error, never a silent zero | **MET, re-homed** — M6. |
| **C15** | rejection is fatal, no truncation | **MET** — nothing here softens a rejection; the round-up in §10.1 makes admission *stricter*, never looser. |
| **C16** | "cannot be expressed ⇒ cannot be offloaded" is allowed | **USED, and the uses are itemised in §17.** No *previously admissible* function is made inexpressible: every removed instruction has a strictly more capable NW replacement (§7.2). |
| **C17** | admission counts peak liveness in bits, one pool | **MET** — §10.1. |
| **C18** | not a count of names | **MET** — §10.2, and the lemma is what licenses it. |
| **C19** | a register never read costs nothing | **MET** — unchanged; liveness is over values, not names. |
| **C20** | admissibility is a property of generated code | **MET** — unchanged, and `-ffixed` makes it a build-time property of the generated code (§10.3). |
| **C21** | `nmfc_bu` (480 b) and `nmfc_expand` (384 b) must still fit | **MET** — §6.6, both fit for any width distribution over {8,16,32,64}, with 32 and 128 bits of slack. **But see §17.1: `nmfc_bu`'s 32 bits of slack is less than one anchor, so it has no 64-bit scratch.** |
| **C22** | twelve user instructions plus privileged `RESUME` | **UNCHANGED — argued, not assumed.** The twelve are the *host↔fabric interface*; the NW tier is inside the function core's instruction stream and is invisible to the host, adding nothing a host program can name. The count of twelve is not touched. The honest counterpoint is that the *function core's* decoder grows by roughly fifty forms, which is a real widening even though it is not a widening of the twelve — §18 ruling 2. |
| **C23** | only `funct7` groups `0x6`/`0x7` are free, `RESUME` claims one | **MET, untouched** — M14. The proposal spends major-opcode space, a different budget. |
| **C24** | the canon assigns no field values | **MET** — stated at the top and again at §3.2 and §4: every bit position and mnemonic is illustrative implementation choice. |
| **C25** | every operand is a value in a GPR; a context register is named by a number in a GPR | **MET** — M12. The host never names a slice. |
| **C26** | no bit-field insert/extract carrying an offset and a width | **MET BY THE LETTER, AND THE TENSION IS FLAGGED.** X-C26's rejected instruction carried a **runtime** (offset, width) as operand *values*, duplicating shifts and masks. Here `(name, lane, w)` are **encoding immediates naming a register slice**, never runtime values, and no shift-and-mask instruction is duplicated — `SMOV` is a register-to-register move, not a data-manipulation primitive. The closed decision is not re-opened. **But #233's "Let's not overdesign" is a live objection to a fifty-form tier**, and §18 ruling 2 puts it to the user rather than answering it here. |
| **C27** | `CXW`/`CXR` are complete as the host aperture | **MET** — §9.2 does everything with `CXR` plus RV64I shifts. No host-side aperture instruction is added. |
| **C28** | the lane is an instruction field, not a register | **MET, and extended.** `CXW`/`CXR`'s lane stays in `funct7`; the NW tier's lane is likewise an instruction field and never a register. The principle — a constant the compiler already knows should not cost an instruction to produce — is the same principle applied one level down. |
| **C29** | tier 4 never decides anything | **MET** — `NMFC_CTX_WORDS`, `NMFC_CTX_LANES`, `x1..x8`, `defaultLayout()` are cited only as the implementation to be *changed* (§8.1), and §8.1 notes explicitly that `defaultLayout()`'s `x1`–`x8` is **off by one** from this proposal's `x0`–`x7` and must not be mistaken for it. |
| **C30** | a fixed table at one width is the SST layout and is not an answer | **MET — §2, with a numeric discriminator.** The names are at one width; the *operations and slices* are at four. A 32-bit value costs 32 bits, not 64, and sixteen fit, not eight. |
| **C31** | DESIGN §25.7 D:2560-2564 is overruled and must be marked | **MET** — M1 and §8.5 mark it and create the ledger entry. |
| **C32** | do not attribute reasons the user did not give | **OBSERVED, and the two places it bites are flagged in line.** The ruling's stated reason is the third referenced object, and that is the only ground on which this proposal argues against the per-function map (§5). Decode latency, table lookup cost and migration leakage are reported as **measurements of this design** in §14 and are explicitly *not* used as arguments (§14.4). The `CXR`-lane alignment in §9.1 is likewise labelled a consequence, not a virtue. |

---

## §13 THE OPEN QUESTIONS (context-file §11)

| # | open question | this proposal's answer |
|---|---|---|
| 1 | the mix of widths among the names | **Dissolved rather than answered.** There is no mix to choose: eight names at 64 bits, and the width comes from the instruction. Every mix is available at every instant, and by the lemma every mix packs without waste. This is the proposal's central claim. |
| 2 | whether `f`*n* and `x`*n* are the same bits | **Yes, by ISA definition** — the user's option 2 and the task statement. This is a *consequence of the proposal*, not a fact already in the record. It makes `fmv.d.x`/`fmv.x.d` anchor-to-anchor moves (identity when the numbers match), and it makes `fmv.w.x`/`fmv.x.w` destructive, which is why they are removed (§7.2). |
| 3 | whether a nameable slice may straddle a 64-bit boundary | **No** — §1.3 property 1, and the straddle path in `Context512` becomes dead code. |
| 4 | what the admission test becomes | **A sum of bits, unchanged, plus a round-up to {8,16,32,64}.** The lemma proves the sum-feasibility and assignment-feasibility readings coincide, so K.6 does not have to choose (§10.1). |
| 5 | whether `CXW`/`CXR`'s 64-bit lane granularity changes | **No** — M4. It is left alone deliberately, and it now coincides with the anchor grid. |
| 6 | prior art unchecked in-record | **Checked, §16.** The facts file corrected two of the three original claims (SVE's "typed views" is false; RVV's `vsetvli`/SEW is prior art for the *forbidden* mechanism), and §16 adds five ISAs the record did not have — AArch64 by-element SIMD, Motorola 68000, DEC Alpha's `EXTBL`/BWX history, PowerPC `rlwinm`, and x86's AH/SPL encoding-dependent naming. |

---

## §14 COST

### 14.1 Bits of state

| where | before | after | delta |
|---|---|---|---|
| per **context** | 512 b register file + 64 b PC | 512 b + 64 b | **0** |
| per **function** | one `RegLayout`: 32 × (16-bit offset + 8-bit width) + a flag ≈ **97 bytes** | nothing | **−97 B per resident function** |
| per **tile** | one resident-function table, ≥ 1 entry | nothing | **−(97 B × resident functions)** |
| per **migration** | 72 B | 72 B | **0** |
| decoder | — | ~50 instruction forms, 3 shift/OR offset units, 3 lane-well muxes, byte enables on the write port | **area, not state** |

The map is combinational logic. It does not migrate, does not scale with the context
count `C`, and does not appear in the 87-bytes-per-context accounting of DESIGN §25.7.

### 14.2 Decode latency

Operand resolution is `(name << 6) | (lane << (3+k))`: a 3-bit field into a 4:1-muxed
shifter (shift amounts 3, 4, 5, 6 selected by the 2-bit `w` field), OR'd with a 3-bit
field shifted by a constant. **Two gate levels, combinational on instruction bits, three
of them in parallel.** The elastic-`fn` variant adds three 4:1 muxes on 3-bit fields
ahead of that, for one more gate level.

There is no serial dependency of the form *"read a table, then compute an address"*: the
offset is available in the same cycle the instruction bits are.

### 14.3 Datapath and register-file cost

- **Read port:** extract *w* bits at a byte-aligned offset from a 512-bit line. Because
  slices are naturally aligned and ≥ 8 bits (§1.3), this is a 64:1 byte selector per
  output byte over 8 output bytes — the same structure a byte-addressable load unit
  already has, not a general barrel shifter.
- **Write port:** byte enables, 8 per anchor, 64 per context. **No bit-level enables.**
- **ALU:** a segmented adder whose carry chain breaks at 8/16/32-bit boundaries, or four
  narrower adders. This is the standard packed-SIMD structure; RISC-V's own P/Zpn
  proposal assumes the same hardware (F-C21).
- **Forwarding, conditional:** if the tile forwards, forwarding becomes byte-granular
  (§6.2). Cost unknown until the tile's pipeline is fixed; flagged, not estimated.

### 14.4 A note the constraint list requires

X-C32 forbids arguing against the rejected per-function map on grounds the user did not
give. **The numbers in §14.1 and §14.2 are reported as properties of this design and are
not offered as arguments against the rejected one.** The only ground on which this
proposal argues against the per-function map is the ground the ruling states: it is a
third object referenced at decode time. Everything else in this section is a
measurement.

---

## §15 THE PRICE, AGAINST PURE ALIASING AND AGAINST THE REJECTED MAP

**"Pure aliasing"** here means the natural competitor: a fixed, ISA-wide table assigning
each nameable slot a fixed width and offset, using **stock `RV64IMAFD` encodings only**
and adding no instructions. It is the version of the user's option 2 that costs no
opcode space.

### 15.1 Head to head

| | pure aliasing (fixed mixed table) | **alias + width-in-opcode** | rejected per-function map |
|---|---|---|---|
| opcode space | **0** | 1 custom major (or 2) | 0 |
| state outside the context | 0 | **0** | ~97 B per resident function — **rejected** |
| objects referenced at decode | 2 | **2** | 3 |
| instructions per sub-width ALU op | 1 *if a name of that width exists*, otherwise **inexpressible** | **1, always** | 1 |
| instructions to move a value between widths | 2–3 (shift/mask through a name of the right width, which must exist and be free) | **1** (`SMOV`) | 1 |
| widths expressible | only the frozen mix | **any multiset over {8,16,32,64}** | any multiset over any widths, at any offset |
| packing waste | whatever the frozen mix does not match | **0** for power-of-two widths (§6.6) | 0 |
| a 12-bit value costs | depends on the mix | **16 bits** | 12 bits |
| `nmfc_bu` (7×64 + 32) | **REJECTED** by any mix with < 7 64-bit names | **fits**, 32 b slack | fits, 32 b slack |
| toolchain gate `-ffixed-x{n}` | works | **works** | does not work (names have no fixed width) |
| admission test | a colouring over a fixed interference structure | **a sum of bits** | a sum of bits |

### 15.2 The instruction-count answer, stated plainly

**Against pure aliasing, the dynamic instruction-count overhead of this proposal is zero
or negative.** In every case pure aliasing can express, this scheme uses the same one
instruction, because the width rides in the opcode rather than in the name. In the cases
pure aliasing *cannot* express — which is most of them, since one frozen mix serves one
shape of function — the comparison is not a count but an admission failure (§2).

**The overheads are elsewhere**, and they are these:

| overhead | size | when it bites |
|---|---|---|
| one custom major opcode | 1/32 of the encoding space | always; §4 |
| decoder area | ~50 forms + 3 offset units | always; §14 |
| immediates shrink with width | base-tier `imm12` at D; **7 bits at W, 5 at H, 3 at B** (§3.5) | a constant too wide for the field costs one `SLI`: **+1 instruction**, loop-invariant in every case that matters, so ≈ 0 amortised |
| B-width immediates are 3 bits | — | exactly a byte shift amount, and little else. Byte-slice arithmetic against a constant needs one `SLI` in the prologue |
| narrow memory access without ESC1 | — | a narrow load becomes base load into a scratch anchor + `SMOV`: **+1 instruction and a 64-bit scratch anchor.** ESC1 removes this and is why it is in the proposal |
| FMA at W width has no `rm` | — | RNE only; a function needing another mode at `f32` uses `f64` or is inadmissible (X-C16) |
| round-up to a power of two | ≤ 2× per value | a 12-bit value costs 16, a 33-bit value costs 64 |

### 15.3 Against the rejected map — the exact trade

Code density is **identical**: both resolve an operand to `(offset, width)` with no extra
instruction, and both let one instruction operate at any width. The differences are two,
and only two:

1. **The rejected map packs non-power-of-two widths exactly**; this proposal rounds up to
   {8,16,32,64}. On a function of 12-bit fields that is a 33% bit cost; on the two
   measured functions, whose widths are 64 and 32, it is **zero**.
2. **The rejected map is a third object at decode**; this is not.

That is the whole trade, and stating it that narrowly is the point: **this proposal buys
X-C1 for a round-up, and nothing else.**

---

## §16 PRIOR ART

Every item is cited to shipping hardware or a ratified specification. The facts file
verified the RISC-V half (F-C1 – F-C24); the non-RISC-V items marked **new** were not in
the record and close context-file §11 question 6.

### 16.1 "The register name is the slice" — the aliasing half

- **x86 / x86-64 `RAX`/`EAX`/`AX`/`AL`** (F-C19). The canonical sub-register alias. Three
  cautions this proposal answers: write rules inconsistent by width (`EAX` zero-extends,
  `AX`/`AL` preserve) — answered by one uniform rule, preserve (§6.2); `AH`/`BH`/`CH`/`DH`
  are non-contiguous bits 15:8 for four legacy registers only — answered by making every
  slice naturally aligned with no irregular views; partial-register merge stalls —
  answered by X-C13's absence of rename (§6.2).
- **x86-64 `AH` vs `SPL`** — **new.** Byte-register field value 4 names `AH` without a REX
  prefix and `SPL` with one: the *same field value* denotes different bits depending on an
  encoding bit. This is exactly this proposal's two-tier `x0` (§6.3), shipping since 2003.
- **AArch64 SIMD&FP `Bn`/`Hn`/`Sn`/`Dn`/`Qn`** (F-C20, the correct ARM cite). Five
  architectural names for one register at 8/16/32/64/128 bits: the letter selects the
  width, the number selects the register. Two differences: all five views are
  bottom-anchored nested prefixes, not disjoint slices; and a narrow write **zeroes** the
  upper bits, which AArch64 chose over x86's preserve precisely to dodge the merge hazard.
  This proposal takes the opposite decision for the reason in §6.2 — zeroing destroys a
  packed neighbour, and there is no rename to protect against.
- **Zdinx / Zfinx aligned register pairs** (F-C16, F-C9). Ratified RISC-V. Zdinx solves
  "a value wider than one name" with even-numbered pairs, an explicit endianness-independent
  ordering rule, reserved misaligned encodings, and a specified `x0` interaction. This
  proposal's `lane ≥ 64/w` reserved rule (§6.4) is Zdinx's reserved-misaligned rule
  generalised, and its `f`≡`x` structure is Zfinx's, ratified.
- **MIPS-I / SPARC V8 FP register pairing** (F-C24). `$f0` is a 32-bit single; a double
  named `$f0` occupies `$f0`+`$f1`; odd numbers illegal for doubles. Zdinx is this rule
  re-ratified thirty years later.

### 16.2 "The opcode carries the width" — the other half

- **RISC-V loads and stores**, ratified. `funct3` = {unsigned, width[1:0]} for
  `lb`/`lh`/`lw`/`ld`/`lbu`/`lhu`/`lwu` and `sb`/`sh`/`sw`/`sd`. The task's suggested
  precedent, and exact. §6.1 explains why only the two width bits carry over.
- **RISC-V OP-FP `fmt`**, ratified. `funct7` = `funct5`:`fmt[1:0]` with `fmt` at
  `inst[26:25]` selecting S/D/H/Q, and the R4-type FMA forms using the identical split
  with `rs3` at `inst[31:27]`. **The NW-R format is bit-for-bit that layout with `fmt`
  re-read as a slice width** (§3.1).
- **Motorola 68000 `MOVE.B`/`.W`/`.L`** (F-C24) — **the closest single precedent for the
  combination this proposal makes.** The register name is size-free (`D0`), the size is in
  the opcode, and **a narrow write preserves the upper bits**. Two decades of shipping
  hardware with the preserve rule and no partial-register stall — because there was nothing
  to rename, which is §6.2's argument with the dates attached.
- **ARM SVE (`Z0.S`) and NEON (`V0.4S`)** (F-C20 corrects the record here). The element
  size is a specifier in the instruction, never a property of the register name. SVE is an
  example of F-C9's rule, not an exception to it.
- **RISC-V P / Zpn**, **not ratified** (F-C21): `src/unpriv/zp.adoc` is a placeholder and
  the dashboard puts P in Development as of July 2026. Its mechanism names the lane *width*
  in the opcode (`PADD.B`/`PADD.H`) and gives **no name for an individual lane** — so P is
  prior art for the width half and explicitly *not* for the lane half. **Do not cite it as
  ratified.**

### 16.3 "The lane is in the encoding" — the sub-slice half

- **AArch64 by-element Advanced SIMD** — **new, and the single best precedent.**
  `MUL V0.4S, V1.4S, V2.S[3]`, `FMLA V0.4S, V1.4S, V2.S[2]`, `INS V0.B[3], W1`,
  `UMOV W0, V1.S[2]`. The element index is an immediate **scattered across the H, L and M
  bits of the encoding**, and *how many of those bits are index bits depends on the size
  field* — for 16-bit elements the index is three bits (H:L:M) and the `Rm` register field
  is correspondingly narrowed to four bits, `V0`–`V15`. That is **precisely** this
  proposal's elastic-`fn` trade (§3.3): name bits and lane bits are drawn from one pool and
  the split moves with the width. Shipping in every ARMv8 core.
- **Intel MMX / SSE / SSE4.1 `pinsrw`, `pextrw`, `pinsrb`, `pextrb`, `pinsrd`, `pextrd`** —
  **new.** A lane index as an `imm8` in the encoding, shipping since 1999.
- **PowerPC `rlwinm` / `rlwimi`** — **new.** `rlwinm rA,rS,SH,MB,ME` carries a shift amount
  and a mask-begin/mask-end pair as encoding immediates: a full (offset, width) field
  extract-or-insert in one instruction, since 1990. The same family as `th.ext` below.
- **CORE-V `cv.extract`/`cv.extractu`/`cv.insert` and T-Head `th.ext`/`th.extu`** (F-C22).
  (offset, width) as instruction immediates; both shipping, both upstream in binutils and
  LLVM. Non-RISC-V equivalents: ARM `UBFX`/`SBFX`/`BFI`, x86 `BEXTR`/`PDEP`/`PEXT`, 68000
  `BFEXTU`/`BFEXTS`/`BFINS`. **This is option 1 done properly**, and it is the alternative
  this proposal does *not* take: it costs an extra instruction per access and a data
  dependency, where naming the slice in the operand field costs neither.
- **DEC Alpha `EXTBL`/`INSBL`/`MSKBL`, and the BWX extension that replaced them** —
  **new, and the empirical argument.** Alpha shipped without byte loads and stores; a byte
  access was an aligned load plus `EXTBL`, where the byte position came **from a register**.
  It was slow enough that DEC added the Byte/Word Extension (`LDBU`/`LDWU`/`STB`/`STW`) from
  the 21164A onward. **The historical lesson is exactly the one this proposal rests on: the
  sub-slice position belongs in the encoding, not in a register and not in a table.**

### 16.4 The mechanisms this proposal deliberately does not use

- **RVV `vsetvli` / `vtype.SEW`** (F-C9, nuance 2). Element width from a **CSR** — the type
  is neither in the opcode nor in the register name. **This is prior art for the thing the
  2026-09-03 ruling forbids**, and it is cited here for that reason and no other. It is also
  why §7.3 says the two tiers must be distinguished by opcode and never by a mode bit.
- **Tagged architectures** — Burroughs B5000/B6700, Symbolics Lisp machines, IBM System/38
  and AS/400, the Mill's belt metadata (F-C24). Type travels with the value. Forbidden here
  by arithmetic: tags cost bits inside the 512.
- **A per-function register map.** Rejected 2026-09-03 and deleted by this proposal (M1).

### 16.5 The four homes for width, and where this proposal lives

F-C24 observes that width has been given three homes in shipping hardware — the register
name (x86, AArch64 SIMD&FP), the opcode (68000, SVE, RISC-V P), and a mode register (RVV's
`vtype.SEW`) — with a fourth, the value itself (tagged machines). The ruling forbids the
third; the 512-bit budget forbids the fourth.

**This proposal takes the first two together**: the name gives the anchor, the opcode gives
the width, and a third encoding field gives the lane. No shipping ISA combines all three in
exactly this way — AArch64 by-element comes closest, and its registers are architecturally
separate rather than slices of one context. That is the novel part, and it should be
labelled novel rather than dressed as precedent.

---

## §17 WHAT THIS CANNOT DO

### 17.1 There may be no scratch anchor — the sharpest limit

`nmfc_bu` is measured at **8 values, 480 of 512 bits** (X-C21, DESIGN §22 D:1953). Under
the anchor grid that is seven 64-bit values and one 32-bit value, leaving **32 bits of
slack — half an anchor.** Any sequence needing a full 64-bit temporary (materialising a
constant wider than the immediate field, a base-tier narrow load, staging a value between
two widths without `SMOV`) **has nowhere to put it** in that function.

This is why ESC0 (`SMOV`, `SLI`) and ESC1 (`NLD`/`NST`) are in the proposal rather than
optional: each of them removes a case that would otherwise require a scratch anchor. With
them, the residual cases are constants too wide for the width's immediate field. Without
them, `nmfc_bu` — the measured case — is the function that breaks first.

The remedy when a function genuinely needs scratch it does not have is X-C16: it cannot be
offloaded, or it is split into a `CONT` chain. **Not a stack** (X-C9).

### 17.2 Widths that are not powers of two, and widths below 8

A 12-bit field costs 16 bits; a 33-bit value costs 64; a 1-bit predicate costs 8. Up to
**2× rounding loss per value** against the rejected per-function map, which could name a
12-bit field in 12 bits at an arbitrary offset. A bitmask of flags must live in a B slice
and be reached with the shifts and masks RV64I already has — which is fine, and is I.8's
own answer, but it is a cost the per-function map did not have.

The floor of 8 bits is not arbitrary: it is what keeps the context write port at byte
enables (§1.3 property 2) and what keeps every slice inside one byte.

### 17.3 Non-aligned slices

A value cannot begin at bit 5. Natural alignment is what buys the packing lemma, the dead
straddle path, and the byte enables; it is paid for here.

### 17.4 More than eight 64-bit values

There are exactly eight anchors. A ninth live 64-bit value is inadmissible — but it is
inadmissible on bits as well (576 > 512), so no new rejection is created (§10.2). What *is*
new is that the D-width name set is **exactly saturated**: a function using all 512 bits at
64-bit width has no name left over for anything.

### 17.5 Instructions that cannot be expressed

- **`fmv.w.x` / `fmv.x.w`, `fadd.s` and every `.s` form, `flw`/`fsw`, and the `*W` integer
  forms** are removed (§7.2). Each has a strictly more capable NW replacement, so no
  function loses expressiveness — but **stock-compiled object code cannot be dropped in
  unchanged**, and any hand-written assembly in the record that uses them must be rewritten.
- **Base-tier register names 8–31** are illegal, so stock-compiled code that allocates
  beyond `x7`/`f7` does not run. This is the intended admission gate (§10.3) and the reason
  a stock toolchain reaches only five anchors from integer code without an ABI note.
- **Wide immediates at narrow widths.** The immediate field is 3 bits at B, 5 at H and 7 at
  W (§3.5). Three bits is exactly a byte shift amount and nothing more, so a byte-slice add
  against a constant costs one `SLI` first.
- **FMA at *w* = B or H** — there is no `f8` or, in `RV64IMAFD`, any `f16`. Not a loss.
- **Dynamic rounding modes**, `fcsr`, `fflags` — X-C8, already forbidden, now a decode-time
  illegal instruction rather than undefined behaviour (§6.5).

### 17.6 Dynamic width

The width is an encoding constant, so a loop whose element width varies at run time is not
expressible. This is the direct price of X-C1 forbidding a mode register: RVV's `vsetvli`
is exactly the mechanism that would express it, and exactly the mechanism the ruling
forbids (§16.4). A function needing it is in X-C16's position.

### 17.7 It does not solve the compiler's half

DESIGN §23.6 and I.8 both record that a compiler does not know that narrowing an operand
yields a larger register file, and that the packing — and the admission decision resting on
it — is compiler work. **This proposal does not change that.** It makes every packing
*expressible* in one instruction per operation, and it makes the admission test a sum of
bits with a provable feasibility guarantee (§10.1), which is a smaller compiler problem
than a colouring. It does not decide the packing, and it does not remove the need for a
back end that knows about slices (§10.3).

### 17.8 It does not reduce the size of the ISA

Roughly fifty instruction forms are added to the function core's decoder. That is the
substance of #233's "Let's not overdesign" objection and it is not answered here; it is put
to the user at §18 ruling 2.

---

## §18 WHAT MUST BE RULED

Six things, in descending order of consequence. None is decided by this document.

1. **Amend X-C10 / ruling O4.** The function core's ISA becomes `RV64IMAFD` *restricted*
   (names 8–31 illegal; the five clobbering instruction families of §7.2 removed) *plus*
   the NW tier. X-C10 today says "and nothing else". **Without this ruling the proposal
   cannot be built**, and the honest fallback is pure aliasing at a frozen mix, which §2
   shows rejects `nmfc_bu`.
2. **Weigh #233's "Let's not overdesign" against ~50 new forms.** X-C26's *letter* is met —
   no runtime (offset, width) operand, no duplication of shift-and-mask — but its spirit is
   in tension. The minimum viable subset is NW-R's 18 core ops plus ESC0 (`SMOV`, `SLI`);
   ESC1 (narrow load/store) and ESC2 (sub-D floating point) are each defensible separately,
   and dropping ESC2 alone removes half the forms at the cost of no `f32` packing.
3. **Approve spending custom-1**, which `nmfc_isa.h:18` reserved for a second reservation,
   and decide between variant (a) — one opcode, elastic `fn`, moving field boundaries — and
   variant (b) — two opcodes, fixed fields, the second necessarily taken from RV128-earmarked
   space (§4).
4. **Approve the write rule: PRESERVE** (§6.2). Facts §6.3 requires one explicit rule and
   this is the only one compatible with bit-packing. The x86 hazard it inherits is answered
   by X-C13's absence of rename, with a residual forwarding cost that needs the tile's
   pipeline spec.
5. **Approve `x0`'s two-tier behaviour** (§6.3): zero at the base tier, anchor 0 in the NW
   tier, `f0` reaching anchor 0 in both. The alternative strands 64 bits.
6. **Record the supersessions** (X-C31): DESIGN §25.7 D:2560-2564 overruled; `RegLayout`,
   `defaultLayout()`, `NMFCTile::readReg`/`writeReg` and `NMFCTile.h:448-450` become
   divergences; Appendix 2 `S5` closes and is replaced (§8.5).

**And one thing this document asserts rather than asks**, because the record already
settles it: the map costs **zero bits of state per function, per context, per tile and per
migration** (§5, §14.1), and every field it needs is a field of the instruction word.
That is X-C1, and it is the only reason this proposal exists.
