# DESIGN A — FIXED ALIASING

> **[SUPERSEDED IN PART — READ THIS FIRST.]** `register-map-final.md` is the later work and carries
> this document forward with corrections. **Two of them change numbers that appear below and must
> not be quoted from here:**
>
> 1. **"81 of 13,091 — a factor of 162" (§2.3, §11, §13.2, QA/QB) is a CATEGORY ERROR.** The 81
>    counts (*a*, *b*) pairs over a **two-width** alphabet; the 13,091 counts multisets over a
>    **four-width** one. Counted like for like — of the 13,091 four-width multisets, how many does
>    design A place under its own charge-32 rule — the answer is **2,685, a factor of 4.9**, and the
>    loss is **79.5%, not 99.4%**. The same defect runs through **657** (= 81 + 8 × 72) and **969**
>    (tuples over {64,32,16}); the like-for-like figures are **4,233** and **9,132**.
>    See `register-map-final.md` §5.2.
> 2. **§13.1's claim that the byte-tier loss is "a consequence of the 2026-09-03 ruling, not of this
>    design" is WRONG.** It is a consequence of the **name-denotes-slice family**, and binds design
>    A, B2 and design B's free map alike. `register-map-final.md` §3A builds **design A2**, which
>    respects the 2026-09-03 ruling in full *and* reaches the byte tier — a counterexample that
>    settles the attribution.
>
> Also superseded there: **W1b is struck** (§3 of that document), the `annotate` width-charging
> claim (Q8) is corrected, `rv64ima_zfinx_zdinx` replaces `rv64ima` as the day-one spelling, and
> §10.3's *"no partial-register hazard"* is reconciled with §5.3's merge.

**PROPOSAL — NOT YET IN THE CANON.** Nothing here may be cited as a decision. This is one of
the two designs the user asked for on 2026-09-03: *"I would prefer a proposal for 1 or 2
suggested designs is written up for the end of this, with full consideration of implementation
complexity, performance impact, and overall simplicity."*
(`register-map-fallback-user.md`, "Clarification (user, 2026-09-03, verbatim)").

**What it is.** The register name, its namespace, and the opcode determine (bit offset, width,
type) with **nothing fetched to decode**. There is no per-function map, no per-context map, no
map cache, no handle→address translation and no post-migration fetch. The map is 24 wires and
an OR gate, fixed at tape-out, shared by every context and every function on the tile.

**What it is against.** `register-map.md`'s synthesised "Heap Rule" map, **tightened by that
document's own review**. Three things change substantively:

1. Its §10 nine open questions are resolved to recommendations; **three remain open**, because
   only three genuinely change the design (§9).
2. Its two capability limits — the byte-tier cap and the charge-32 rule — are promoted from
   price-list lines to **stated limits of the design** (§2). They narrow canon I2's *"ANY
   combination"*, and that is a decision the user must acknowledge, not a footnote.
3. Its packing-count figure is **corrected**. §9.3 of `register-map.md` claims this map admits
   "~2,137" width-multisets against the per-function map's "~13,091". The 2,137 belongs to
   `alias-tiled`'s map, which has narrow-tier fragments this one does not. **The correct
   number for this design is 81** — reproduced two independent ways in §2.3, alongside the
   13,091, which does reproduce exactly.

**Prerequisites.** `register-map-facts.md` (fact-C1–C24), `register-map-context.md`
(M1–M15, cons-C1–C32), `register-map.md` (the synthesis and its review).

---

## §0 THE MAP ON ONE PAGE

```
bit  0                                                                                          511
     |-----d0----|-----d1----|-----d2----|-----d3----|-----d4----|-----d5----|-----d6----|-----d7----|
     |  w0 |  w1 |  w2 |  w3 |  w4 |  w5 |  w6 |  w7 |  w8 |  w9 | w10 | w11 | w12 | w13 | w14 | w15 |
     |lane0|     |lane1|     |lane2|     |lane3|     |lane4|     |lane5|     |lane6|     |lane7|

 THE HEAP RULE:  the halves of x_n are x_2n (low) and x_2n+1 (high); x8..x15 are the 64-bit tiles.

 x0 / f0     zero at any width / +0.0 ; writes discarded ; costs none of the 512   [DEVIATION for f0]
 x1 - x7     RESERVED, ILLEGAL as any operand -- tree nodes wider than 64 bits
             consequence: `ret` = jalr x0,0(x1) is illegal; a body ends with END/RETC
 x8 - x15  = d0 - d7   (64 bits)   d_k = bits [64k, 64k+64)  = CXW/CXR lane k
 x16- x31  = w0 - w15  (32 bits)   w_m = bits [32m, 32m+32)  = half of d_(m>>1)
 f_n       = x_n       (same bits) ; f8-f15 carry .d, f16-f31 carry .s

 DECODE:  width64 = ~n[4]     legal = n[4] | n[3]     zero = (n == 0)
          offset[8:6] = n[4] ? n[3:1] : n[2:0]     offset[5] = n[4] & n[0]     offset[4:0] = 0

 ADMISSIBLE  <=>  every opcode in RV64IMAFD
                  AND the seven legality rules hold (§4)
                  AND 64a + 32b <= 512, a <= 8, b <= 16   (b counts values of 32 bits OR FEWER,
                                                           EACH CHARGED 32 -- a stated limit, §2.2)
                  AND a verified non-overlapping placement exists over the live ranges (§7.3)

 STATE OUTSIDE THE INSTRUCTION: none.
 MIGRATION: 72 B, unchanged.   POST-MIGRATION FETCH: none.
```

---

## §1 THE MAP

### 1.1 The generative rule

> **THE HEAP RULE. The two halves of `x`_n_ are `x`_2n_ (low) and `x`_2n+1_ (high). `x8`–`x15`
> are the eight 64-bit tiles of the context, in order.**

Applied once to `x8`–`x15` it generates `x16`–`x31` as the sixteen 32-bit tiles. Applied again
it would generate `x32`–`x63` as thirty-two 16-bit tiles, which a 5-bit field cannot reach in
the `x` namespace. Read upward, it says what `x1`–`x7` are: `x1` is the whole 512 bits,
`x2`/`x3` its 256-bit halves, `x4`–`x7` its 128-bit quarters. **No operation in `RV64IMAFD` is
wider than 64 bits** (`Q` is outside the subset, cons-C10), so those seven names denote nothing
this machine can compute on and are reserved.

### 1.2 The table

| encoding | ABI name | width | bit range | note |
|---|---|---|---|---|
| `x0` / `f0` | `zero` | **any** | none | reads 0 at whatever width the instruction needs (`f0` reads **+0.0**); writes discarded; costs none of the 512 (**M5**). `x0` is ratified (Unpriv. Ch. 2). **`f0` is a DEVIATION** — fact-C17: "`f0` **is** general" |
| `x1`–`x7` / `f1`–`f7` | — | — | none | **reserved — illegal instruction** (tree nodes wider than 64 bits) |
| `x8`–`x15` / `f8`–`f15` | `d0`–`d7` | **64** | `[64k, 64k+64)`, *k* = *n*−8 | the **D** tiling — complete, 512 of 512 |
| `x16`–`x31` / `f16`–`f31` | `w0`–`w15` | **32** | `[32m, 32m+32)`, *m* = *n*−16 | the **W** tiling — complete, 512 of 512 |

Composition, verified: `x8..x15` tile the 512 bits exactly; `x16..x31` tile them exactly;
`w`_2k_ ∪ `w`_2k+1_ = `d`_k_ for every *k*; every slice is naturally aligned and **no slice
crosses a 64-bit word boundary**. `d`/`w` are **documentation-only names**; the assembler
accepts `x8`…`x31` and every flag recipe and ABI table below is spelled in `x` numbers.

`f`_n_ ≡ `x`_n_ is Zfinx's ratified rule for the case where the two namespaces coexist (Ch. 26
§26.1, fact-C9). It buys one allocation pool, which makes K.6's "third wrong answer" — an
allocator drawing `f`-names from a pool separate from `x`-names and admitting a function twice
the legal size — **structurally unrepresentable**. It costs 32 of the 63 nameable encodings, and
it means **this machine is not implementing `F`/`D`** (facts §6.1). It is a choice, not a forced
move, and it is retained question **QB** (§9).

### 1.3 Why the anchoring is `x8`/`x16`

Four things fall out of anchoring at `x8` rather than `x1`, and all four are wanted:

1. **Every D name is allocatable.** `x8`–`x15` = `s0, s1, a0`–`a5`. Nothing ABI-fixed, nothing
   clobbered by a jump. (`alias-tiled` anchored at `x1` and its `d0`–`d7` covered `ra, sp, gp,
   tp` — no stock GCC or LLVM allocates general values into `gp`/`tp`, so it yielded at most
   four usable names, not eight.)
2. **`a0`–`a5` are 64-bit names**, the right default for pointers; `a6`/`a7` = `w0`/`w1` are
   32-bit, the right default for a seventh and eighth integer argument in a bit-packed world.
3. **Decode is two OR'd bits** (§1.4). `alias-tiled`'s anchoring needed two magnitude
   comparators for the same job.
4. **`x1`–`x7` reserved makes `ra` and `sp` illegal names**, an I7 tripwire (§4 rule 2).

At no cost: `x8`–`x15` is exactly the set the compressed encodings reach (Zca, fact-C17). K.6
excludes RVC, so this is a note, not a claim.

### 1.4 Decode

```
    width64     = ~n[4]                              // 1 inverter
    offset[8:6] = n[4] ? n[3:1] : n[2:0]             // 3 x 2:1 mux
    offset[5]   = n[4] & n[0]                        // 1 AND
    offset[4:0] = 0                                  // wires
    legal       = n[4] | n[3]                        // 1 OR
    zero        = ~(n[4]|n[3]|n[2]|n[1]|n[0])        // 1 five-input NOR
```

**≈7 primitive gates, one logic level after the select**, or as a ROM 31 × (9-bit offset +
1 class bit) = **310 bits, once per tile**, shared by every context and every function on it.
Compare the object it replaces: `RegLayout` = 32 × (`uint16` offset + `uint8` width) = **768
bits per resident function** (`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCRegLayout.h:40-42`), fetched
at every register access (`NMFCTile.cc:461-475`, `return c.regs.read( layout_.field[r] );`).

**Nothing is read, nothing is indexed by the function, nothing depends on the context**
(cons-C1, cons-C2). The instruction and the data remain the only two referenced objects.

---

## §2 THE TWO STATED LIMITS

These are limits of **this design**, stated here rather than in the price list, because they
narrow a tier-1 `[SHARPENED]` block (canon I2, quoting user #232: the context *"could be 16
4-byte regs, **64 1-byte regs**, or ANY combination"*, CANON.md:5215-5233). They have different
provenance and must not be merged.

### 2.1 Limit 1 — the byte tier is unreachable, and that IS arithmetic

> **No name is narrower than 32 bits. "64 one-byte registers" cannot be named by any scheme in
> which the register number alone denotes the slice.**

A register field is five bits (Ch. 2), so each namespace affords 32 encodings; `x0` is hardwired
zero and cannot be a slice without invalidating `nop`, `j` and the HINT space (fact-C17), leaving
**63 at the very most**, and 63 only if the two namespaces name different bits. Complete coverage
at width *w* costs 512/*w* names: 8 at 64, 16 at 32, 32 at 16, **64 at 8**. Sixty-four names do
not exist. fact-C17 reaches the same result from the other side: 512/63 ≈ 8.1 bits per name.

**Provenance: this is a consequence of the 2026-09-03 ruling**, not of this design — it holds for
every name-denotes-slice scheme, and the ruling's stated reason was the third referenced object
(cons-C32). It is the price of deleting the map.

**What is NOT forbidden by counting, and must not be claimed to be:** 8 + 16 + 32 = **56 ≤ 63**.
A complete 16-bit tier is arithmetically available (e.g. `f0`–`f31` as thirty-two 16-bit tiles).
It is declined here for two contestable reasons — `RV64IMAFD` has no register-to-register
arithmetic below 32 bits (no RV16I; `F`'s narrowest operation is 32 bits; `Zfh` is outside ruling
O4), and §1.2 spends the `f` namespace on identity aliasing. **That second reason is retained
question QB.**

### 2.2 Limit 2 — every sub-32-bit value is charged 32 bits, and that is a REGRESSION

> **Admission charges every live value narrower than 32 bits a full 32 bits. This is not K.6
> verbatim.**

K.6 charges a value its actual width — its own worked example prices "eight live 64-bit values
plus one 8-bit value" at 520 bits, eight bits for the byte, not thirty-two
(`register-map-context.md` §7). The concrete regression, verified:

| function shape | under K.6 | under this design | verdict |
|---|---|---|---|
| 7 × 64-bit + 8 × 8-bit | 448 + 64 = **512** → admitted at exactly 512 of 512 | 448 + 8×32 = **704** → **rejected** | a function K.6 admits cannot run |
| 9 × 48-bit (432 bits of data) | 432 → admitted, placed exactly | needs nine 64-bit tiles; only eight exist → **rejected** | ditto |
| 12 × 16-bit + 5 × 64-bit | 192 + 320 = **512** → admitted | 12×32 + 320 = **704** → **rejected** | ditto |

Under cons-C15 that rejection is **fatal** — there is no spill, no truncation, no softening. The
function is rewritten, split into a `CONT` chain, or refused.

**Provenance: this one is THIS DESIGN'S choice**, not the ruling's. It follows from declining the
16-bit tier (§2.1) and from reserving `x1`–`x7` (§8, Q2). A different fixed-aliasing map could buy
some of it back; the per-function map bought all of it.

**The ceiling on live values is sixteen, whatever their widths.** A boolean, a byte counter, a
level number and a 3-bit tag each cost 32 bits.

### 2.3 What the two limits cost, measured

The comparable measure is **how many distinct width-multisets each scheme can place**. Counting
every multiset over widths {64, 32, 16, 8} that sums to ≤ 512 bits, subject to each scheme's own
name budget:

| scheme | rule | admissible width-multisets |
|---|---|---|
| **Design A** | *a* ≤ 8 sixty-fours, *b* ≤ 16 thirty-twos, 64*a* + 32*b* ≤ 512 | **81** `[NOT COMMENSURABLE — like-for-like is 2,685; see the banner]` |
| Design A + the Q2 narrow subtree (§8) | + 2 × 16-bit and 4 × 8-bit names carved out of `w15` | 657 `[like-for-like: 4,233]` |
| three complete tiers (QB option (b)) | 8 / 16 / 32 names at 64 / 32 / 16 | 969 `[like-for-like: 9,132]` |
| **the rejected per-function map** | 31 names, each freely any of the four widths, ≤ 512 bits | **13,091** |

**Design A's 81 is exact and has a closed form**: the multisets are the integer pairs (*a*, *b*)
with 0 ≤ *a* ≤ 8, 0 ≤ *b* ≤ 16 and 2*a* + *b* ≤ 16, which is
17+15+13+11+9+7+5+3+1 = **81**. The enumeration agrees.

**The 13,091 reproduces `alias-tiled` §8.2's figure exactly** under the rule stated above, which
is why it is trusted here. **The "~2,137" that `register-map.md` §7.3 and §9.3 attribute to a
"fixed two-width map" does not describe this design** — it is `alias-tiled`'s number, for a map
carrying seven 16-bit and seven byte names. ~~Corrected: **the ratio is 13,091 → 81, a factor of
162, not a factor of 6.**~~ **[WITHDRAWN — see the banner at the head of this file.** The 81 and the
13,091 count objects over different alphabets and cannot be divided. **Like for like the ratio is
13,091 → 2,685, a factor of 4.9**, because §2.2's charge-32 rule means a narrow value is charged 32
and *still runs*. `register-map-final.md` §5.2.**]**

**Against it, the two measured functions still fit** (cons-C21, DESIGN §22):

| function | measured | decomposition | test | result |
|---|---|---|---|---|
| `nmfc_bu` | 8 values, 480 bits | *a*=7, *b*=1 | 448 + 32 = 480 ≤ 512 | **admissible**, one `w` tile spare |
| `nmfc_expand` | 8 values, 384 bits | *a*=4, *b*=4 | 256 + 128 = 384 ≤ 512 | **admissible**, 128 bits spare |

Both decompositions need re-measuring once `annotate` has a working width input on a RISC-V
target (§8, Q8). Neither uses a sub-32-bit value, which is why nothing in the record yet
measures the cost of Limit 2 on real code.

**This is what must be acknowledged:** I2's "ANY combination" becomes **"any combination of
64- and 32-bit values, with everything narrower charged 32."** ~~81 of 13,091.~~ **2,685 of 13,091
[corrected — see the banner].** Say yes; or reinstate the map (Design B) and pay the third
referenced object; or take **design A2** (`register-map-final.md` §3A), which reaches the byte tier
with no third object at the price of reopening R84.

---

## §3 EXECUTION SEMANTICS — WIDTH COMES FROM THE OPERAND'S ROLE

> **W1 — OPERAND WIDTH IS PER OPERAND, AND ITS SOURCE IS THE ROLE.**
>
> | role | width | name required |
> |---|---|---|
> | **address / base** of any load, store, atomic, or `jalr` | **always 64** | a `d` name |
> | **data** operand of a load or store | the **opcode's** width (`lb`/`lh`/`lw`/`ld`, `flw`/`fld`, `lr.w`/`amo*.d`) | a name of that width, except `lb`/`lh`/`lbu`/`lhu`, which may target any name and extend into it |
> | any **FP** source or destination of an `F`/`D` instruction | the width the mnemonic names (`.s` = 32, `.d` = 64; `fcvt.*` names each operand separately) | exactly that width |
> | the **integer** operand of an FP instruction (`fcvt.w.d`, `fmv.x.w`, `feq`/`fclass` results) | the width the mnemonic names (`.w` = 32, `.l`/`.x.d` = 64); a compare/`fclass` **result** is 0/1 and fits any name | as the mnemonic names, except the 0/1 result |
> | **branch** sources | **always 64** | any name |
> | **integer ALU / shift / M** operands and destination | the **integer execution width**, W1b | any name |
>
> **W1b — INTEGER EXECUTION WIDTH.** 32 if the opcode is a `*W` form (`addw`, `sllw`, `mulw`,
> `divuw`, `addiw`, …); otherwise the width of the **widest register operand, destination or
> source**. `x0` contributes nothing to that maximum; when every operand is `x0` the width is
> 64. `lui`/`auipc` execute at 64.
>
> **W2 — READ PORT.** Each source is read from **exactly its own name's bits**, then adjusted
> to its W1 width: an **integer** source narrower than the operand width is **sign-extended**
> (RV64's ratified invariant, Ch. 5, fact-C13); wider is **truncated** (only for a `*W` opcode,
> exactly as on a stock RV64 core); a **floating-point** source is read at exactly its operand
> width with **no extension and no NaN-box check**, because W1 requires the name to be exactly
> that width.
>
> **W3 — WRITE PORT.** A write puts the result into **exactly the destination name's bits and
> never modifies a bit outside them.** Where the execution width is narrower than the
> destination name — only for `*W` opcodes and for `lb`/`lh`/`lw` into a wider name — the result
> is sign-extended (zero-extended for `lbu`/`lhu`/`lwu`) to fill the destination name, which is
> ratified RV64 behaviour verbatim. It never NaN-boxes.

**Why W1b is per-operand and not per-destination.** A destination-derived width is wrong for
every instruction whose operands do not share one width, and the failures are silent and
common: `lw w3, 0(d1)` would compute its address at 32 bits and truncate the pointer;
`sltiu w0, d1, 1` — the ubiquitous null test — would compare only the low 32 bits and call a
non-null pointer null; `fcvt.d.s d1, w3` would sign-extend an `f32` bit pattern before the FPU
saw it. The role table is the repair.

**Why sign-extend an integer source on read.** Ch. 5's invariant ("all 32-bit values are held in
a sign-extended format in 64-bit registers") and bit-packing are not in opposition; they are in
different *places*. **W2 maintains the invariant at the read port instead of in the register
file.** The ALU sees exactly the bits a stock RV64 core would present; the storage costs 32 bits
instead of 64; every compiler assumption about sign-extended 32-bit operands survives. Sign
extension is order-preserving for unsigned comparison, which is why RV64 chose it: `bltu` on two
sign-extended 32-bit names gives the right answer without a `zext.w`.

**W3 is forced, not chosen.** x86 preserves the upper bits on a narrow write, AArch64 zeroes them
(fact-C19, fact-C20). **This map can do neither, because the bits above `w`_2k_ are another
architectural value named `w`_2k+1_, not spare room.** W3 confines every ratified extension
mechanism inside the destination name.

**NaN-boxing is abolished on the tile, in both directions.** There is no wider container, so
nothing to box on write (fact-C11) and nothing to check on read (fact-C4). Without this, a valid
positive `f32` in `w3` — zeros above it — fails the ratified box check and is read as canonical
NaN. **The host still boxes and checks**, because it is a stock RV64GC core with FLEN = 64, so
the tile and the host have different `f32` semantics; §13.8 prices it.

**Width conversions fall out with no new instruction** (cons-C26 stands):

| conversion | instruction(s) | why it is ratified behaviour |
|---|---|---|
| narrow → wide, signed | `mv d1, w3` (= `addi d1, w3, 0`) | W1b: widest operand is `d1`, execution width 64; W2 sign-extends — `sext.w`'s semantics, free |
| narrow → wide, unsigned | `slli d1, w3, 32 ; srli d1, d1, 32` | what RV64 without Zbb already emits for `zext.w`. **Must be written on a `d` destination** — at 32-bit execution width `shamt[5] = 1` is reserved exactly as in ratified RV32I |
| wide → narrow | **nothing** — the low half of `d`_k_ **is** `w`_2k_ | free truncation by aliasing |
| narrow → wide, float | `fcvt.d.s d1, w3` | W1 reads `w3` as a raw 32-bit `f32`; `fcvt` is the correct instruction in stock RISC-V too |

---

## §4 LEGALITY — THE SEVEN RULES A DECODER TRAPS

**1a. An address/base operand that is not a `d` name.** `lw w3, 0(w5)` illegal; `lw w3, 0(d1)`
legal.

**1b. An operand-class disagreement.** The table is total over `RV64IMAFD`:

| instruction class | `rd` / data | `rs1` | `rs2` | `rs3` |
|---|---|---|---|---|
| `ld`/`sd`, `fld`/`fsd`, `lr.d`/`sc.d`/`amo*.d` | **`d`** | base **`d`** | data **`d`**; `sc.d`'s `rd` see 1c | — |
| `lw`/`lwu`/`sw`, `flw`/`fsw`, `lr.w`/`sc.w`/`amo*.w` | **`w`**, except `lw`/`lwu` may target `d` or `w` (W3 extends into it) | base **`d`** | data **`w`** | — |
| `lb`/`lbu`/`lh`/`lhu`/`sb`/`sh` | `d` or `w` | base **`d`** | — | — |
| `.d` FP arithmetic, `fsgnj*.d`, `fmin.d`, `fmax.d` | **`d`** | **`d`** | **`d`** | — |
| the `.s` forms of the same | **`w`** | **`w`** | **`w`** | — |
| `fmadd.d`/`fmsub.d`/`fnmadd.d`/`fnmsub.d` | **`d`** | **`d`** | **`d`** | **`d`** |
| the `.s` fused forms | **`w`** | **`w`** | **`w`** | **`w`** |
| `feq`/`flt`/`fle`, `.d` / `.s` | **any** (0/1) | **`d`** / **`w`** | **`d`** / **`w`** | — |
| `fclass.d` / `fclass.s` | **any** (10-bit mask) | **`d`** / **`w`** | — | — |
| `fcvt.<a>.<b>` | the width `<a>` names | the width `<b>` names | — | — |
| `fmv.x.w` / `fmv.w.x` | **`w`** | **`w`** | — | — |
| `fmv.x.d` / `fmv.d.x` | **`d`** | **`d`** | — | — |
| `auipc` | **`d`** (a 64-bit PC-relative address) | — | — | — |
| every other `I`/`M` opcode | **any** | **any** | **any** | — |

`fmv` moves raw bits and converts nothing (fact-C1), so both operands are the same width and
both names must be that width: `fmv.d.x w0, d1` is **illegal** — it would silently drop 32 bits.

**1c. `sc.d`/`sc.w`'s `rd`** is a 0/1 success flag and may be **any** width.

**2. `x1`–`x7` or `f1`–`f7` as any operand.** This makes `sp` (`x2`) and `ra` (`x1`) illegal
names, so `addi sp, sp, -16` and `sd ra, 8(sp)` are both decode-illegal — and it makes
**`ret` = `jalr x0, 0(x1)` illegal** (§8, Q7).

**3. `jal` or `jalr` with `rd` ≠ `x0`.** A check on the *link register*, not on the name `x1`.
It kills every link-forming form while leaving `j`, `jr` and every conditional branch legal, so
switch tables and computed jumps still work and **no ABI-conforming call can be encoded**.
Stated honestly: this catches every call a compiler emits; it does not make a call *impossible*
(`auipc d1, 0` / `addi d1, d1, 12` / `jr d2` forms a link by hand, and banning `auipc` would
cost every PC-relative constant). I7 stays a compiler-discipline invariant with a decode check.

**4. A `*W` opcode with a destination wider than 64, or a shift with `shamt[5] = 1` at 32-bit
execution width** (reserved exactly as in ratified RV32I).

**5. An RV64-only opcode at a narrow name.** `lwu`, `fcvt.l.*`/`fcvt.lu.*`, `fmv.x.d`/`fmv.d.x`
and the 64-bit atomics have no RV32 meaning, so W1b's "32 if the widest operand is 32" cannot be
applied to them.

**6. Anything outside the subset** (cons-C10): CSR access, `FENCE`, RVC, `ecall`, anything from
`V`.

**7. A `w`-named operand where W1 requires 64 and no rule above caught it** — the catch-all that
makes the decoder total, so an unlisted encoding fails closed rather than executing at a guessed
width.

**Exempt:** `x0` and `f0` denote no bits, so they have no width to disagree with. Load-bearing:
without the exemption `beqz`, `li`, `mv`, `snez` and `j` (`jal x0`) would all be illegal, and
this machine's loops are built from them (**M5**).

**Deliberately legal: mixed-width integer ALU operands.** `add d1, w3, w4` executes at 64 (W1b's
widest operand), reads both sources sign-extended, writes `d1`. `add w0, d1, d2` also executes at
64 — the widest operand is a source — and W3 keeps the low 32 bits, which is the truncation the
programmer asked for by naming a 32-bit destination.

**Deliberately legal: `*W` opcodes on a `w` destination.** Read the `*W` suffix for what Ch. 5
says it is — not "a 32-bit operation" but "*a 32-bit operation whose result is canonicalised into
a 64-bit register*". When the register is 32 bits the canonicalisation is the identity, so
`addw w0, w1, w2` computes exactly what `add w0, w1, w2` computes. This is why stock `int`
codegen works under register-class assignment alone (§8, Q3).

---

## §5 THE SCOREBOARD AND THE OVERLAPPING-NAME DEPENDENCE CHECK

These are **part of the design**, not caveats. Under aliasing, "a per-register ready bit" is
ill-defined — a pending load into `w6` must make `d3` not-ready — and dependence checking can no
longer compare 5-bit register numbers for equality. Both are specified here.

### 5.1 On the canon core: one pending destination

**H.4 is canon and it is decisive.** User #239, CANON.md:5299-5320: *"I would opt for 1 outstanding
miss. Likely we need a D-buffer, but it just stores one slot, the data that will be used when it
wakes."* CANON.md adds: *"With one outstanding load there is nothing to disambiguate and nothing
to keep coherent"*, and *"the context is always asleep when its load returns"*.

> **The scoreboard is not a bit vector. It is ONE pending destination and a one-slot data
> buffer.** The context is `BLOCKED` while the load is in flight and issues nothing, so there is
> no readiness to test on any other name, no overlap to reduce over, and no growth. On the fill,
> the pending name resolves through §1.4's decode and is written at that name's width.

This structure **already exists in the tree**: `dbufReg` (5-bit name) and `dbufValue`
(`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCTile.h:85-86`), written at `NMFCTile.cc:827-828`, replayed
at `NMFCTile.cc:1504`. **What this design adds to it: three bits** — the fill's width/extension
class (`{b,h,w,d} × {signed, unsigned}`), which a stock in-order core already carries and which
`writeReg(c, c.dbufReg, c.dbufValue)` currently does not have. **Per-context state added by the
map itself: zero. Migration payload: 72 B, unchanged.**

### 5.2 On the ChampSim core model: sixteen ready bits at `W` granularity

H.4 is explicit that DESIGN §7's `scoreboard[≤8]` (`DESIGN.md:775`) belongs to the *other* core
model — contexts that keep issuing past an outstanding load and get intra-function MLP from an
in-order scoreboard. For that model the design is:

> **Sixteen ready bits, one per 32-bit tile.** A read of a `d` name takes the OR of its two. A
> pending load resolves its destination through §1.4's decode and marks exactly the tiles it will
> write. **This is exact: no name stalls on a load it does not depend on.**

Cost: **+8 bits per context** against an eight-bit form; 16 × 1024 = 2 KiB per tile at C = 1024
against 64 KiB of context state (**3.1 %**, of which the delta is **+1 KiB = 1.5 %**), and **+1
byte in the migration *envelope***.

**I11 is untouched either way.** I11's 72 bytes are *"64 bytes of register file plus an 8-byte
program counter"* (DESIGN §25.7, D:2574-2577). The `token`, `origin`, `home_host` and `scoreboard`
of DESIGN §7.1's payload are envelope, not the 72 bytes. **The 72-byte payload is unchanged in
every configuration.**

**The eight-bit lane-granular alternative is rejected**, and not because eight is inconvenient:
a load into `w0` would stall a read of `w1`, so the false dependency falls **exactly between the
two halves of a lane** — the case bit-packing exists to create. Choosing lane granularity because
`scoreboard[≤8]` says eight would be using the 8-register artefact as a design constraint, on the
one structure in the machine where a register name is visible. That is the reversion I2 forbids
(#238: *"Once again, NO. 512 bits of context. The context is not 8 regs."*).

### 5.3 The overlapping-name dependence check

On the canon core there is nothing to check: CANON.md:474-478, verbatim — *"Because at most one
instruction per context is ever in flight, no two instructions in the pipe can be dependent — so
there is no forwarding, no interlocking, no hazard detection, no ROB, no rename, no load/store
queue, and no speculative execution."* **There is no bypass network to make partial-width and no
comparator to widen.**

For any non-barrel implementation, the check is specified rather than left to be rediscovered.
Dependence becomes bit-range overlap, and under this map it factors into five bits per operand:

```
    lane[2:0] = n[4] ? n[3:1] : n[2:0]          // ALREADY COMPUTED -- it is offset[8:6]
    mask[1:0] = n[4] ? {n[0], ~n[0]} : 2'b11    // 1 inverter + 2 mux
    overlap(a,b) = (lane_a == lane_b) && |(mask_a & mask_b)
```

`lane` is free: §1.4's decode already produces it as `offset[8:6]`. `mask` is two gates. The
comparator is a 3-bit equality plus a 2-bit AND-OR — **≈7 gates against ≈6 for the 5-bit equality
it replaces, and the same number of logic levels.** The bypass path needs a byte-masked merge:
eight 2:1 byte muxes on a 64-bit forward. **This is a small, bounded, fully specified change, not
a new hazard class in the sense of an open problem** — but it is a change, and a core that keeps
a 5-bit equality comparator here is wrong.

---

## §6 HOST-SIDE PACKING — CXW/CXR, PER LANE AND PER FORK

**`V` is unavailable and appears in no retrieval path** (cons-C11, and fact-C7: holding 512 bits
in one architectural vector register needs `Zvl512b`, not plain `V`). The path is `CXW`/`CXR` plus
RV64I shifts and masks, then `fmv.d.x`/`fmv.w.x` if the value is wanted in an `f` register — and
on the host those `fmv`s are the real ratified ones, because the host is a stock RV64GC core.
This is the record's own position: #233, *"We need to make sure EXTRACTION from the regs is
possible. Regular bit manipulation can take you the rest of the way … Let's not overdesign."*

### 6.1 The aperture already lines up

`CXW`/`CXR` move one 64-bit lane, the lane number in `funct7[3:1]`
(`/mnt/md0/NMFC-Rev/src/nmfc/include/nmfc_isa.h:101-102`, `NMFC_CX_LANE_SHIFT 1`,
`NMFC_CX_LANE_MASK 0x7`). Under this map:

> **lane *k* is exactly tile `d`_k_ = `x`_(8+k)_, and its halves are `w`_2k_ and `w`_2k+1_.**

Neither moved to make this so. Because no tile straddles a lane, **every named value is reachable
in exactly one `CXR`**; the two-`CXR`-and-splice case an arbitrary bit-packed map required cannot
arise (cons-C27, cons-C28 preserved; `NMFC_CX_LANE_SHIFT`/`MASK` keep their values).

### 6.2 Staging in — the cost per lane

**The correct idiom: build the lane, then write it once.** Two 32-bit arguments per lane; only
the **low** half needs zero-extending, because the high half's upper bits are shifted out. This
matters: a 32-bit argument arrives in `a0` **sign**-extended (Ch. 5, fact-C13), so the naive
`slli t0,a1,32 ; add t0,t0,a0` propagates ones and a carry across bits 63:32 and destroys
`w`_2k+1_ whenever bit 31 is set.

```
    slli  t0, a0, 32
    srli  t0, t0, 32            # zero-extend the LOW argument   (Zbb: zext.w t0, a0)
    slli  t1, a1, 32            # high argument; bits 63:32 discarded by the shift
    or    t0, t0, t1            # { w_2k+1 : w_2k }
    CXW   c1, k, t0
```

| what the host does | stock RV64GC | with `Zbb` |
|---|---|---|
| stage one 64-bit argument | **1** (`CXW`) | 1 |
| stage **two** 32-bit arguments into one lane | **5 = 2.5 per argument** | 4 = 2 per argument |
| stage **one** 32-bit argument, paired tile **dead** in the callee | **1** (`CXW` of the sign-extended GPR; the garbage upper half is never read, cons-C19) | 1 |
| stage **one** 32-bit argument, paired tile must read as **zero** | **3** | 2 |
| stage one 32-bit argument into a lane whose other half is **already live** | **7** — the read-modify-write case; avoid by staging lanes whole | 6 |
| read back one 64-bit result or `f64` | **1** (`CXR`) | 1 |
| read back a 32-bit result from the **low** half, signed | `CXR` + `sext.w` = **2** | 2 |
| read back a 32-bit result from the **low** half, unsigned | `CXR` + `slli` + `srli` = **3** | `CXR` + `zext.w` = 2 |
| read back a 32-bit result from the **high** half, either sign | `CXR` + `srai`/`srli` = **2** | 2 |
| deliver a 32-bit result into an `f` register | + `fmv.w.x` = **+1** (the host has FLEN = 64 and NaN-boxes correctly, as ratified) | +1 |

**The rule for the ABI, because the 7-instruction case is avoidable and the 1-instruction case is
easy to miss: stage a lane's two halves together, and never patch one half of a live lane.**

### 6.3 The cost per FORK

| offload shape | staging cost | vs. 64-bit-only |
|---|---|---|
| 8 × 64-bit arguments | **8 instructions** (8 `CXW`) | baseline |
| **8 × 32-bit arguments** | **20 instructions** (4 `CXW` + 16 shift/or) | **+12** |
| 8 × 32-bit arguments, host has `Zbb` | **16 instructions** (4 `CXW` + 12) | **+8** |
| 6 × 64-bit + 2 × 32-bit (the stock ABI's `a0`–`a7`, §8 Q4) | 6 `CXW` + 5 = **11** | +3 |
| 3 × 64-bit + 5 × 32-bit | 3 `CXW` + 5 + 5 + 1 = **14** | +6 |

**Per bit moved, packing is a tax and the record must say so:** eight 64-bit arguments move 512
bits in 8 instructions (64 bits/instruction); eight 32-bit arguments move 256 bits in 20 (12.8
bits/instruction). *(`register-map.md` §4.3 previously concluded that packing was cheaper per bit;
that was an artefact of the missing zero-extension and is withdrawn.)*

**The comparison that matters is not packed-versus-unpacked staging — it is offloading versus not
being able to.** A function needing nine live values does not fit 512 bits unpacked. The tax is
**+12 instructions per FORK in the worst realistic case**, paid once, in the caller's frame, on a
host with a stack and 31 real registers, against a round trip that crosses the fabric twice
(72 B each way at 32 B/cycle, `NMFCCoherenceFabric.h:66`) and executes a function body at
near-memory latency.

### 6.4 The calling convention, and why I2 still holds

> **The ISA fixes the GEOMETRY — which bits a name denotes. It does not fix the ASSIGNMENT —
> which value a function put in which name.** That stays the function's own ABI, known to its
> caller. **I2 is preserved literally: register positions carry no architectural meaning across
> the boundary**, and all 512 bits come back whole and uninterpreted (**M8**, **M9**).

What genuinely changes is that caller and callee must now agree on **width** as well as position,
so a convention is published with the function. Under the stock RV64 ABI read through §1.2:

| role | RV64 ABI register | this map | lane |
|---|---|---|---|
| integer/pointer arguments 1–6 | `a0`–`a5` = `x10`–`x15` | `d2`–`d7` | 2–7 |
| integer arguments 7–8 | `a6`, `a7` = `x16`, `x17` | `w0`, `w1` | **0 — the two halves of `d0`** |
| scratch | `s1` = `x9` | `d1` | 1 |
| one 64-bit result or `f64` | `a0` = `x10` | `d2` | 2 |
| one 32-bit result | low half of `a0` | `w4` | 2 |
| two results | `a0`/`a1` | `d2`, `d3` | 2, 3 |

> **The trap, stated as a rule: lane 0 (`d0` = `w0` ∪ `w1` = `s0`) is scratch ONLY for a function
> taking six or fewer integer arguments.** A seventh occupies `w0`, an eighth `w1`; with both,
> `d0` is fully occupied and the only free 64-bit tile is `d1`. An allocator that treats `s0` as
> scratch in that case silently clobbers two arguments, **and no decode check can see it** (§13.3).

**A stack argument is inadmissible because there is no stack (I7, cons-C9) — that is a rule about
the stack, not a count of nine.** Under the stock ABI a ninth argument goes to the stack, so a
stock-toolchain caller cannot pass nine. Under a custom convention packing into `w` names, **nine
32-bit arguments are 288 bits and are admissible**; sixteen are 512 and are the ceiling. Canon I7
carries a `[CORRECTED]` block on exactly this point (CANON.md:1111-1115).

**Where the map lives on the host: nowhere referenced.** The offsets appear in the caller's code
as immediates in `slli`/`srli`, exactly as a struct field offset does.

---

## §7 ADMISSION — THREE TESTS, ONE OF WHICH IS A HEURISTIC

> **A function is admissible iff (1) every opcode in it is in `RV64IMAFD` and its body satisfies
> §4's seven legality rules; (2) its peak simultaneous liveness satisfies `64a + 32b ≤ 512` with
> *a* ≤ 8 and *b* ≤ 16, where *b* counts values of 32 bits **or fewer, each charged 32**; and
> (3) a verified non-overlapping placement exists over its live ranges.**

### 7.1 The subset and legality test

Mechanical: walk the body, check each opcode against `RV64IMAFD`, check each operand against §4.
`annotate` already walks the body and already refuses functions
(`/mnt/md0/ChampSim/ChampSimArchWork/nmfc/tools/nmfc/annotate.cc:542-553`).

### 7.2 The bits test

**Inherited from K.6 unchanged:** the test is on **bits**, in **one pool**, and it is peak
simultaneous liveness, not a count of registers touched. Part P R30's error is not re-introduced —
at 64 and 32 bits the name count *is* the bit count, because both tilings are complete. cons-C19
survives: a value never read is never live and costs nothing. cons-C15 survives: rejection is
fatal.

**Changed:** the charge-32 rule (§2.2). **Necessary but not sufficient** — see §7.3.

**The tool has no working width input on a RISC-V target, and this is a live blocker.**
`annotate.cc:461-470`'s `width_of` parses **x86-64** register names (`rax`/`eax`/`al`, `r8d`,
`zmm`/`ymm`/`xmm`); the ruled target is RISC-V (CANON.md R11) and RV64 register names carry no
width at all (fact-C12). On a RISC-V trace every value falls to the `else` branch and is charged
64. The `bits` figure at `:555-559` is computed and thrown away on a stderr line at `:927`
(ledger L30). Resolution in §8, Q8.

### 7.3 The verified placement — and why the sum alone is not enough

**The single-instant lemma.** Let values have widths *w*₁ ≥ … ≥ *w*ₙ, each in {64, 32}, with
Σ*w*ᵢ ≤ 512. Place them in that order, each at the lowest free bit offset. Every value lands on a
*w*ᵢ-aligned offset and no two overlap. *Proof:* before placing value *i* the occupied region is
the prefix [0, Σ_{j<i} *w*ⱼ); every earlier width is a power of two ≥ *w*ᵢ, hence a multiple of
it, so the next free offset is already *w*ᵢ-aligned. No gap is ever created. ∎

**The lemma is about one instant. Register allocation is allocation over time, and the two are
not the same test.** Verified counterexample:

> Place eight 32-bit values first-fit into `w0`–`w7`. Let the values in `w0`, `w2`, `w4`, `w6`
> survive and the rest die; five 64-bit values are then born. Peak liveness is 4×32 + 5×64 = **448
> ≤ 512**, so the bits test admits. But `w0` blocks `d0`, `w2` blocks `d1`, `w4` blocks `d2` and
> `w6` blocks `d3`, leaving **four free `d` tiles for five values**. The function does not fit.

**Therefore the design requires a placement, not merely a sum.** A permissive admission test is a
defect, not a nuisance: under I7 there is no spill to fall back on, and cons-C15 makes rejection
fatal.

**The allocator, stated as what it is:**

> **First-fit-decreasing by width at each birth, with a disjointness check over the whole
> placement, and relocation moves permitted. This is a HEURISTIC, not an exact allocator.**

- **Relocation repairs every gap the geometry can open, at one instruction per move and no
  scratch.** In the counterexample, `mv w1, w2` and `mv w5, w6` compact the survivors into `d0`
  and `d2`, freeing six `d` tiles for five values.
- **The general problem is mixed-width allocation with alignment over an interval graph** —
  `register-map.md`:754 names it "the classic register-pairing problem". *This document does not
  reproduce a hardness proof and does not claim one.* What is proved above is the only thing the
  design needs: **the sum is not sufficient, so a placement must be verified.**
- **Empirical risk, cited as evidence and not as proof:** a stress test over ~400,000 random
  interval instances that pass the closed form found a placement by exhaustive backtracking every
  time at widths {64, 32}. Reported in the `alias-hierarchical` judging; **not independently
  reproduced here.**
- **When FFD fails, the tool must not admit.** It may retry with backtracking, or reject. It may
  not guess.

### 7.4 The one error the machine cannot catch

`d0` and `w0`/`w1` are the same bits. If an allocator makes a value live in `d0` and another live
in `w1`, they clobber, and **no decode check can see it** — a total map makes every name always
defined, so `RegLayout::illegal()`'s run-time trap (`NMFCTile.cc:464`, `:472`) has nothing to fire
on. cons-C14 required the check be re-homed rather than deleted, and it re-homes onto **admission,
as a disjointness check on the verified placement**. That is a build-time obligation with **no
run-time counterpart**, and its failure mode is a silent wrong result. §13.3.

---

## §8 THE SIX QUESTIONS THAT RESOLVE

`register-map.md` §10 asked nine. Six do not change the design once answered, and are resolved
here as recommendations:

| # | question | **recommendation** | one-line reason |
|---|---|---|---|
| **Q2** | spend the seven reserved names `x1`–`x7` on a narrow tier, or reserve? | **Reserve.** | Nothing in the record measures a demand for a sub-32-bit value, and **reserved names can be defined later; defined names cannot be undefined.** *(If it is ever wanted, the shape is settled and it is `alias-hierarchical`'s: `x2`,`x3` = the two 16-bit halves of `w15` and `x4`–`x7` = their four bytes — a complete buddy subtree of one 32-bit region, so the lemma extends, admission gains exactly one clause "sub-32-bit values total ≤ 32 bits", and §2.3's ~~81 becomes 657~~ **2,685 becomes 4,233 [corrected]**. Its cost: `x2` = `sp` stops being illegal, weakening §4 rule 2 to the store alone.)* |
| **Q3** | `*W` opcodes on a 32-bit name: permissive or strict? | **Permissive.** | Stock RV64 codegen emits `addw` for every `int` expression, so permissive makes **register-class assignment alone produce correct `int` arithmetic**; strict forces width-aware instruction selection before any `int` code runs, and the trap it buys fires only on *correct* programs — it never catches the error that matters, a 64-bit value misassigned to a 32-bit name, where plain `add` truncates silently under both rules. |
| **Q4** | does the NMFC convention renumber arguments? | **Keep the stock ABI mapping (argument *k* → lane *k*+2) while the day-one path is in use; renumber to lane *k* when the custom back end lands** — and publish §6.4's rule alongside it either way. | It is pure convention, it changes only host staging code, and the day-one `-ffixed` path needs the stock mapping; renumbering later dissolves the `d0` self-collision at the same time as the back end that would exploit it. |
| **Q7** | the return-value convention, and how a body terminates | **(a) `d2` for one 64-bit result or `f64`, `w4` for one 32-bit result, `d2`/`d3` for a pair.** **(b) A body ends with `END`/`RETC`; `annotate` rewrites a stock body's terminating `ret` and rejects any body whose `ret` is not the sole terminator.** | (a) is the stock RV64 ABI's own answer read through §1.2, so a stock body needs no change and it never collides with the seventh/eighth-argument case; (b) belongs in `annotate` because it already walks the body — and the core must **not** treat a trailing `ret` as an implicit end-of-body, because guessing is the silent behaviour cons-C15 forbids. |
| **Q8** | the admission tool has no working width input on RISC-V | **The defining opcode now (`lw` vs `ld`, `addw` vs `add`, `flw` vs `fld` — a ~150-entry table); the register class the back end assigned once §10.5's back end exists. Until one lands, charge every value 64 bits and SAY SO.** | The opcode is exactly the RV64 signal that replaces the x86 name suffix and is available in any RISC-V trace; a conservative gate that rejects some admissible functions is sound, and a silent fall-through that looks like a measurement is not. **Consequence: §2.3's two measured decompositions are not currently reproducible from the tool and must be re-measured.** |
| **Q9** | scoreboard granularity on the ChampSim core model | **Sixteen bits at `W` granularity** (§5.2). | Lane granularity's false dependency falls exactly between the two halves of a lane — the case bit-packing exists to create — and the cost is +1 KiB per tile (1.5 %) and one byte of **envelope**; **the 72-byte migration payload is unchanged either way**, so I11 is not at risk and cons-C6 needs no re-ratification. |

---

## §9 THE THREE THAT REMAIN

### QA — Acknowledge the two stated limits

**The question.** §2 states them: **the byte tier is unreachable** (arithmetic, and a consequence
of the 2026-09-03 ruling rather than of this design), and **every sub-32-bit value is charged 32
bits** (this design's choice, a genuine regression against K.6 with a fatal failure mode). Together
they narrow canon I2's *"64 1-byte regs, or ANY combination"* to **"any combination of 64- and
32-bit values."** Measured: **81 admissible width-multisets against the per-function map's
13,091** (§2.3).

**Why it needs you.** It is a tier-1 `[SHARPENED]` block being narrowed. The alternatives are real:
**(a)** this design, keeping the decode path at two referenced objects; **(b)** reinstate the
per-function map — Design B — regaining all 13,091 and paying the third referenced object you
called foolish; **(c)** something else. **Recommendation: (a)**, recorded as a decision with a
ledger entry of the same weight as the ruling that caused it, not as an omission.

### QB — The namespace fork: does `f`_n_ ≡ `x`_n_?

**The question.** `register-map-facts.md` §6.1, verbatim: *"If the design wants 63 names it must
keep F/D encodings and accept that it has forked the ISA; if it wants Zfinx's clean semantics it
has 31 names for 512 bits. **That choice has not been made and it is the real fork in option 2.**"*

| | **(a) `f`_n_ ≡ `x`_n_ — recommended** | **(b) the `f` namespace names different bits** |
|---|---|---|
| names | 31 | 63 |
| tiers, all complete | 64 and 32 (24 names) | 64, 32 **and 16** (56 names) |
| width-multisets (§2.3) | **81** | **969** |
| allocation | one pool; K.6's "third wrong answer" unrepresentable | two pools; the allocator must not double-count, which is the failure K.6 names |
| 16-bit arithmetic | n/a | **does not exist in `RV64IMAFD`** — the tier is load/store/move only unless O4 is amended for `Zfh` or a 16-bit integer tier |
| `fmv.*` | free aliases, no-ops on identical names | real 32/64-bit moves between disjoint tiles — genuinely useful |
| admission | one clause | one bit budget plus a clause for the narrow tier |

**Recommendation: (a)** — the tier (b) buys has no arithmetic to run on it under ruling O4, and a
single pool removes an error class the canon explicitly names. **But it costs 32 names and 888 of
the 969 multisets, and it should be ruled rather than assumed.** (b) is a strictly larger change:
O4 must be amended for the tier to be worth having, and admission and the scoreboard both grow.

### QC — Two tier-1 supersessions this design requires

Both are clauses of yours that this design contradicts. Neither can be quietly dropped (cons-C31).

**(i) CANON.md:9819 — "The namespaces do not alias."** Verbatim: *"the compiler binds every
simultaneously-live `f`- or `x`-name to a **disjoint bit range**, so `f3` and `x3` are different
names at different offsets, not one slot."* §1.2 makes `f`_n_ ≡ `x`_n_ and contradicts the second
clause. **The first half — *512 bits of live storage, not 64 architectural slots* — is preserved
and strengthened.** The reason it can be superseded is itself a tier-1 ruling: the sentence
describes *the compiler doing the binding*, and the 2026-09-03 ruling took the binding away from
the compiler. The same ledger entry should carry DESIGN §25.7 D:2560-2567's supersession.
**This is a consequence of QB(a); if you rule QB(b), it does not arise.**

**(ii) CANON I.7 item 3 — the remedy for dynamic rounding.** Verbatim: *"There is no `fcsr`, no
rounding-mode state … A function needing dynamic rounding modes is in the same position as a
function that spills: **it cannot be offloaded.**"* This design **defines `rm = DYN` (`0b111`) as
RNE** instead, because GCC and LLVM emit no rounding suffix in ordinary codegen and the assembler
encodes DYN by default — rejecting it makes **all** stock FP codegen illegal.

> **The cost, stated plainly because it is the worst failure mode in the design: the same
> instruction encoding computes different results on the host and on the tile, and no admission
> check can see it**, because every stock FP instruction carries DYN and is indistinguishable
> from one that genuinely wanted RNE. A function that called `fesetround()` and then offloaded
> gets a different answer, silently. The partial remedy is a **build-time** gate — `annotate`
> rejects any function reaching `fesetround`/`fegetround`, or any translation unit compiled
> `-frounding-math` — which is weaker than I.7's.

**Recommendation: supersede, with the build-time gate as the stated remedy** — because the
alternative rejects every stock floating-point function, which is a larger loss than a gated
divergence. **But I.7 item 3 says "cannot be offloaded" and this softens it, so it is your call,
not the document's.** *(`register-map.md` §3.7 called this "costs nothing, breaks nothing"; that
is withdrawn.)*

---

## §10 IMPLEMENTATION COMPLEXITY

### 10.1 Decoder

| item | estimate |
|---|---|
| name→(offset, width, legal) logic | **≈7 primitive gates, 1 logic level** (§1.4): 3 × 2:1 mux, 1 AND, 1 OR, 1 five-input NOR. As a ROM instead: **310 bits per tile**, shared by every context and every function |
| the seven legality rules (§4) | rule 2 and rule 3 are 3 gates each; rule 4 is one AND on `shamt[5]`; rules 1a/1b/5/7 are a **per-opcode required-width field in the existing opcode decode table** (2 bits per operand slot × 3 slots = 6 bits per entry) compared against `width64` — **≈6 bits per opcode-table row plus 3 comparators**, not a new table |
| net | **one extra column in the opcode ROM, and about twenty gates.** No new decode stage |

### 10.2 Register read port

| item | estimate |
|---|---|
| structure | 8:1 select of 64-bit words (**3 mux levels**) → 2:1 half select on `offset[5]` (**1**) → 2:1 sign-extension select on the upper 32 bits (**1**) |
| total | **5 mux levels against 3 for a fixed 8 × 64 file: +2** |
| what feeds it | an offset resolved at decode, a stage earlier — the selects are not on the critical path from the instruction word |
| precedent | the extension select is the same structure a load unit's `lw`/`lb` sign-extension already is, moved onto the register read port — which is exactly where §3 said the RV64 invariant goes |

### 10.3 Register write port

| item | estimate |
|---|---|
| structure | **byte enables** — 64 per 512-bit context line. Generation is a 6:64 decoder off `offset[8:3]` plus a width-driven spread of 4 or 8 bytes: **≈100 gates** |
| what it avoids | **no read-modify-write, no merge, no partial-register hazard** — every tile is a whole number of bytes on a byte boundary, so a neighbour's bytes are simply not written |
| net | **+0 levels** |

### 10.4 Scoreboard and hazard logic

| item | estimate |
|---|---|
| canon core (barrel, one outstanding miss) | **+3 bits per context** — the fill's width/extension class beside the existing `dbufReg`/`dbufValue` (`NMFCTile.h:85-86`). **The map itself adds zero.** |
| ChampSim core model | **16 ready bits per context** (one per 32-bit tile), +8 against an eight-bit form = **+1 KiB per tile at C = 1024, 1.5 % of 64 KiB of context state**; total scoreboard 2 KiB/tile = 3.1 % |
| dependence comparator (non-barrel only) | **≈7 gates per operand pair against ≈6**, same levels; `lane[2:0]` is free (it is `offset[8:6]`), `mask[1:0]` is 2 gates (§5.3) |
| bypass (non-barrel only) | eight 2:1 byte muxes on a 64-bit forward path |
| barrel depth `Dp` | **unchanged.** The two extra read-port mux levels sit inside a stage that already exists, so `C ≥ W × (Dp + L/I)` (CANON.md:192-193) is not perturbed and the dominant SRAM term does not move |

### 10.5 Execution units — the part that is NOT free

W1b makes XLEN per-instruction, and for several opcodes 32-bit behaviour is **not** a truncation
of the 64-bit answer:

| what needs real 32-bit logic | why | cost |
|---|---|---|
| `slt` / `sltu` | the sign bit and the unsigned ordering are at bit 31, not 63 | a second comparator tap, ~1 gate level on the flag |
| **register** shifts `sll`/`srl`/`sra` | the shift-amount mask is `rs2[4:0]` at XLEN 32 and `rs2[5:0]` at 64 | one AND on the shift amount, off the same width bit |
| `mulh`/`mulhu`/`mulhsu` | a **different product tap** — bits 63:32 of a 32×32 product, not 127:64 of a 64×64 | the multiplier already computes the low half; the high-half select becomes 2:1 |
| `div`/`divu`/`rem`/`remu` | the overflow special case is −2³¹ ÷ −1 at 32 bits, not −2⁶³ ÷ −1, and the quotient of two sign-extended 32-bit values is not the sign-extension of the 32-bit quotient in that case | one extra compare against the 32-bit sentinel in the divider's special-case logic |
| RV64-only opcodes with no RV32 meaning | there is nothing to execute at 32 bits | **none — they are illegal at a narrow name** (§4 rule 5), which is why that rule exists |

**Net: a handful of gate levels and one extra mux inside units that already exist**, driven by a
single width bit resolved at decode. It is **not** a second ALU and **not** a dual-XLEN subtarget —
every RV64 unit already contains its 32-bit case, because RV64 already has `addw`, `mulw` and
`divw`. But an RTL engineer reading "+0 on everything but register read" would under-build the M
unit.

### 10.6 Admission tool (`annotate`)

| item | estimate |
|---|---|
| replace the slot pool with a width classifier | `annotate.cc:524-559`: the pool of `opt.num_regs` (8) slot ids and its `die(...)` go; `width_of` at `:461-470` is replaced by a **~150-entry RISC-V opcode→width table** (Q8) — **the single largest tooling item, and it is required by K.6 anyway** |
| make the bits figure the gate | `:555-559` already computes `bits`; `:927` throws it away on stderr. **~10 lines** |
| FFD placement + relocation | **~120 lines**: sort live values by width at each birth, place at the lowest free aligned offset, emit `mv` relocations where a buddy pair must be recovered |
| placement disjointness verifier | **~40 lines**, and it is the step that makes the tool sound (§7.4) — every pair of simultaneously-live values on disjoint bit ranges, every value on one name for its whole live range or explicitly moved |
| `ret` → `END`/`RETC` rewrite + terminator check | **~30 lines** (Q7b) |
| **total** | **~350 lines in one file, plus the opcode table.** Smaller than the rewrite K.6 already demands |

### 10.7 Compiler back end

| item | estimate |
|---|---|
| day-one path, **no back-end work** | `-march=rv64ima` (so **no `f` register is ever allocated** — the only reliable spelling, because `f`_n_ ≡ `x`_n_ and `-ffixed-x`_n_ does not reserve `f`_n_), plus `-ffixed-x1 -ffixed-x5 -ffixed-x6 -ffixed-x7 -ffixed-x16 … -ffixed-x31`, `-fomit-frame-pointer`, `-fcall-used-x8 -fcall-used-x9`. **Yields eight 64-bit names = 512 bits, integer only.** Under Clang there is no `-fcall-used`, so the honest floor is **six names = 384 bits** — enough for `nmfc_expand`, **not** for `nmfc_bu` |
| the `W` tier | **a new register file description in TableGen**: two allocatable classes, `D` (8) and `W` (16), with `W` declared as sub-register indices of `D`. LLVM's `RegUnit` machinery models overlapping classes of exactly this shape (it is how `RAX`/`EAX` are allocated) — **so the hard part, overlapping-class allocation and sub-register liveness, is reused** |
| what is **not** reusable, stated because it is easy to mis-sell | **Zfinx's `GPRF32`/`GPRF64` are same-numbered aliases** — one file at two widths, a naming trick, not a sub-register relation. **Zdinx's `GPRPair` is a super-register over adjacent even/odd numbers.** Here the whole (`x8`) is numbered **below** its parts (`x16`, `x17`) and the pairing stride is 2 across a 16-name class. Expressible in TableGen; **new register file, new `SubRegIndices`, new encoding map** |
| instruction selection | **unchanged for `int`**, because Q3 keeps `*W` legal on `w` names |
| peepholes to suppress | `sext.w` on a `w` name is a redundant move; the `slli`/`srli`-by-32 `zext.w` idiom is **illegal on a `w` destination** (`shamt[5]` reserved at 32-bit execution width) and must be written on a `d` destination. **The obligation is identical to stock RV64's; only "into a `d` destination" is new** |
| **estimate** | **a register-file `.td` plus a peephole pass. Weeks, not months** — and the day-one path runs with none of it |

### 10.8 SST model (`/mnt/md0/NMFC-Rev/src/nmfc/src/`)

| file | change | estimate |
|---|---|---|
| `NMFCRegLayout.h` | delete `RegLayout` (lines 38-72) and `defaultLayout()`; replace with a **constexpr `nameToField(uint32_t n)`** of ~8 lines. Keep `Context512`; its straddle branches (`read`'s `w0+1`, `write`'s `b0 + width > 64`) become **provably dead** and are removed | −60, +15 lines |
| `NMFCTile.h:448-450` | delete the `RegLayout layout_;` member and its comment | −3 lines |
| `NMFCTile.cc:461-475` | `readReg`/`writeReg` gain the operand **role** and width, because W1 makes width per-operand: signature becomes `readReg(c, r, role)` / `writeReg(c, r, role, v)` | +30 lines |
| **31 call sites** of `readReg`/`writeReg` in `NMFCTile.cc` (33 occurrences less the 2 definitions) | each passes its role — mechanical, and the compiler finds every one | ~31 one-line edits |
| `NMFCTile.cc:1504` | the D-buffer replay needs the fill's width class (§10.4) | +1 field, +1 argument |
| legality checks | the seven rules of §4 in the decode path | +80 lines |
| **floating point** | **the SST tile implements none today** — `grep -c 'fadd\|0x53\|fmv'` over `NMFCTile.cc` returns **0**, confirming Appendix 2 `S6` ("RV64IM+A only") in the tree. F/D is unimplemented work **regardless of which register-map design wins**, and must not be charged to this one | out of scope |
| `Appendix 2 S5` | restate: the `W` tier **is** in the tile from day one (it is 24 gates); what is missing is a compiler that targets it, so the divergence is that no test exercises a `w` name | doc |
| **total** | **≈150 lines net across three files, plus 31 mechanical call-site edits** | |

### 10.9 What is deleted

`RegLayout` goes, and with it: 768 bits per resident function beside the I-cache, the lookup at
every register access, the per-function table's population path, the successor-inherits-a-layout
problem under `CONT`/`CONT.M` (**M10** — a successor running a different function needed a
different entry and a way to know which; the problem disappears rather than being solved), and
`RegLayout::illegal()`'s run-time trap (**which is a loss**, §7.4). **DESIGN.md §25.7 D:2560-2567
and CANON.md:9819 must be MARKED superseded, not quietly dropped** (cons-C31).

---

## §11 PERFORMANCE IMPACT

| axis | impact | number |
|---|---|---|
| **decode latency** | **+0 cycles.** The map resolves from the instruction's own 5-bit field in parallel with opcode decode, from ≈7 gates one level deep. It cannot extend a stage that already decodes a 32-bit instruction word | **0** |
| **register read latency** | **+2 mux levels**, both fed by an offset resolved a stage earlier. Real, and not a cycle | **+2 levels, +0 cycles** |
| **register write latency** | **+0** — byte enables off the same decode, no read-modify-write | **0** |
| **barrel re-issue depth `Dp`** | **unchanged**, so `C ≥ W × (Dp + L/I)` does not move and the tile's dominant 64 KiB SRAM term does not grow | **0** |
| **hazard / forwarding** | **+0 on the canon core** — one instruction per context in flight, so there is nothing to compare (CANON.md:474-478). On a non-barrel core, +1 gate per comparator, +0 levels | **0 / +1 gate** |
| **memory references to decode a register name** | **zero.** This is the whole point: the instruction and the data remain the only two referenced objects | **0** |
| **migration payload** | **72 B exactly**, unchanged — 64 B context + 8 B PC (cons-C6, **M11**). The scheme adds zero bits to anything that travels; on the ChampSim core model the 16-bit scoreboard adds **one byte of envelope**, not of payload | **72 B** |
| **post-migration fetch** | **NONE.** A context arriving on a tile it has never visited is immediately executable. No cold-touch fetch, no handle→address translation, no handle-reuse identity check, no map cache, no cross-page reload | **0 accesses, 0 cycles** |
| **host packing per FORK, 8 × 32-bit arguments** | **20 instructions (4 `CXW` + 16 shift/or)** against 8 `CXW` for eight 64-bit arguments: **+12**, or **+8** with `Zbb`. Per bit moved: 12.8 bits/instruction packed against 64 unpacked | **+12 instructions, once per offload** |
| **host result read-back** | 1 `CXR` for a 64-bit result; 2 for a 32-bit signed result; 3 for a 32-bit unsigned result (2 with `Zbb`); +1 to land it in an `f` register | **1–4 instructions** |
| **admission loss from the two limits** | ~~**81 admissible width-multisets against the per-function map's 13,091 — a factor of 162**~~ **[WITHDRAWN — like for like it is 2,685 of 13,091, a factor of 4.9; see the banner]** (§2.3, both reproduced here). Concretely: 7 × 64-bit + 8 × 8-bit is 512 bits under K.6 and **rejected** here; nine 48-bit values are 432 bits and **rejected**; the ceiling is **sixteen live values, whatever their widths** | ~~**factor 162 on placeable shapes**~~ **factor 4.9 [corrected]** |
| **admission loss from the placement requirement** | Only ever rejects functions the geometry genuinely cannot place, and relocation (1 instruction per move) repairs the gaps the counterexample opens. Empirically narrow — ~400,000 random instances, no failure — **not reproduced here** | **near zero, unproved** |
| **floating-point divergence** | The tile rounds RNE where the host honours `frm`, and abolishes NaN-boxing where the host honours it. **No admission check can see either.** Priced, not free | **silent, gated at build time** |

**The shape of the trade in one line: every cost is paid once, at compile time or in the caller's
frame, and none of it is paid per instruction on the tile.**

---

## §12 SIMPLICITY

### 12.1 State outside the instruction: none

| kind of state | this design |
|---|---|
| per context | **0 bits added.** A context is 512 bits; a context + PC is 576 bits = 72 B |
| per context, scoreboard | **0 added** on the canon core (+3 bits for the fill's width class, which a stock in-order core already carries). +8 bits on the ChampSim core model |
| **per function** | **0 bits. There is no per-function object of any kind** |
| per tile | **310 bits** of decode ROM, or ≈20 gates — **39 B against 64 KiB of context state at C = 1024, 0.06 %** — shared by every context and every function on the tile |
| fetched at decode | **nothing** |
| carried on migration | **nothing** |
| cached per core | **nothing** |
| needing an identity check on reuse | **nothing** |

The decode ROM is the same *kind* of object as the opcode decoder's own truth table:
combinational, fixed at tape-out, not addressed by the program, not fetched, not per function, not
per context.

### 12.2 What an implementer must hold in their head

**Five things, and they fit on the §0 page:**

1. **One sentence of geometry** — the Heap Rule. `x8`–`x15` are the eight 64-bit tiles; the halves
   of `x`_n_ are `x`_2n_ and `x`_2n+1_; `f`_n_ ≡ `x`_n_; `x1`–`x7` are reserved.
2. **One line of decode** — `width64 = ~n[4]`, `offset[8:6] = n[4] ? n[3:1] : n[2:0]`,
   `offset[5] = n[4] & n[0]`, `legal = n[4] | n[3]`.
3. **Four width rules** — W1 (width comes from the operand's role), W1b (integer execution width),
   W2 (read from your own bits, sign-extend up, truncate down; FP raw), W3 (write your own bits and
   never a neighbour).
4. **Seven legality rules** — §4.
5. **Three admission tests** — subset+legality, `64a + 32b ≤ 512` with everything narrow charged 32,
   and a verified placement.

### 12.3 What a reader does NOT have to reason about

No map lifetime. No cache coherence for a map. No handle allocation, no handle reuse, no
handle→address translation. No first-visit fetch. No cross-page reload. No port width on a
map cache, and no banking-per-width forced by one. No interaction between a map's residency and a
context's migration. No question of what a `CONT` successor's map is. **None of those objects
exist.**

### 12.4 Where the complexity actually went

It did not vanish; it moved, and it moved to three places that are cheaper:

1. **Into the opcode table** — one required-width column, ~6 bits per row.
2. **Into `annotate`** — ~350 lines, and K.6 already demanded most of it.
3. **Into the LLVM back end** — one register-file `.td` and a peephole pass, deferred behind a
   day-one `-march`/`-ffixed` path that needs neither.

And one piece of it went somewhere genuinely worse: **the over-liveness check moved from run time
to build time and lost its run-time counterpart** (§7.4, §13.3). That is the one place where this
design is less safe than the mechanism it replaces.

---

## §13 WHAT IT CANNOT DO

**Free truncation is not on this list** — the low half of `d`_k_ **is** `w`_2k_, so narrowing a
64-bit value to 32 costs no instruction. Nor is any opcode: **no instruction is removed from
`RV64IMAFD`, so ruling O4's opcode list stands unamended.** What changed is the liveness test and
ten points of semantics.

**13.1 "64 one-byte registers" is inexpressible, and that one is arithmetic.** §2.1. No scheme in
which the register number alone names the slice can do it. **This is a consequence of the
2026-09-03 ruling, not of this design.**

**13.2 No name is narrower than 32 bits, and the charge-32 rule is a fatal regression against
K.6.** §2.2. A boolean, a byte counter and a 3-bit tag each cost 32 bits; the ceiling is **sixteen
live values**. 7 × 64-bit + 8 × 8-bit is 512 under K.6 and **704 here**. Measured across all
shapes: **81 of 13,091.** *This one is this design's choice, and QA asks you to accept it.*

**13.3 Overlap is undetectable by the machine — a DOWNGRADE of cons-C14.** §7.4. `d0` and
`w0`/`w1` are the same bits; an allocator making both live produces a silent wrong answer.
`RegLayout::illegal()` caught the analogous error **at run time**; a total map has nothing to fire
on. **Not a hardware guarantee.** The concrete instance to watch is §6.4's `d0`: under the stock
ABI `s0` looks like scratch and is occupied whenever a seventh or eighth integer argument is
passed.

**13.4 No non-power-of-two width, and no unaligned placement.** A 48-bit pointer costs 64; a
12-bit index costs 32. **Nine 48-bit values are 432 bits of data and do not fit**, because they
need nine 64-bit tiles and eight exist. The per-function map placed them exactly.

**13.5 The width mix is frozen ISA-wide and cannot be retuned per function.** A function that is
all 64-bit leaves sixteen names unused; a function wanting twelve 16-bit values cannot have them
though the bits exist. **Per-function retuning is precisely what the 2026-09-03 ruling removed**,
and this is where the removal is felt. Recorded, not argued.

**13.6 The admission guarantee is exact at a program point, not proved over live ranges.** §7.3.
The bit test is necessary and not sufficient; FFD is a heuristic; the empirical evidence for the
gap being narrow is **not reproduced here**.

**13.7 Invariant 7 is enforced against the ABI idiom and nothing more.** §4 rule 3. Every call a
compiler emits and every ABI-conforming stack access traps at decode; a hand-rolled link through
`auipc` and a store through a scratch pointer do not. **Not claimed as enforcement.**

**13.8 The tile and the host compute different floating-point answers, silently — the worst
failure mode in the design.** Two independent causes: `rm = DYN` means RNE on the tile and reads
`fcsr.frm` on the host; and NaN-boxing is abolished on the tile and honoured on the host, so a
non-canonically-boxed `f32` is a valid operand on one and canonical NaN on the other. **No
admission check can see either.** Canon I.7 item 3's remedy for the first is *"it cannot be
offloaded"*; this design softens it to a build-time gate. **QC(ii).**

**13.9 A stock toolchain reaches the `D` tier only, and only for integers — two causes.**
(i) `-ffixed-x`_n_ does not reserve `f`_n_ while `f`_n_ ≡ `x`_n_, so a stock compiler allocating an
`f` register would **silently corrupt** a reserved tile; (ii) `f0` is a hardwired zero and
`f1`–`f7` are illegal, so **the entire stock FP temporary set `ft0`–`ft7` either discards results
or traps.** `-march=rv64ima` is the only reliable day-one spelling and closes both. The `W` tier
needs a register-class split in the **back end** — not a change to the tile, which implements `W`
in hardware from day one.

**13.10 It does not solve the compiler's packing problem.** It converts "choose 32 (offset, width)
pairs per function" into "allocate over two nested register classes", which every back end already
knows how to do — but somebody still has to do it, and I.8's *"open, and it is a compiler
problem"* stays open.

**13.11 Disassembly lies by default.** Stock `objdump` prints `x23` and implies 64 bits where the
ABI means `w7` at 32 — correct decoding, misleading display. `d`/`w` are documentation-only names;
a binutils patch is optional and buys only diagnostics.

**13.12 Ten deviations from the ratified manual, which an implementer must build and a stock
`RV64IMAFD` core does not have.** In descending order of consequence: (1) `f`_n_ ≡ `x`_n_ — **this
machine is not implementing `F`/`D`**; (2) `f0` hardwired to +0.0 and `f1`–`f7` illegal; (3)
NaN-boxing abolished in both directions; (4) FLEN is per-instruction, not an implementation
constant; (5) tile and host disagree on `f32` semantics and on rounding; (6) `rm = DYN` defined as
RNE; (7) **XLEN is per-instruction** (W1b imports RV32I/RV32M semantics for `slt`, register
shifts, `mulh*`, `div`/`rem`); (8) `*W` opcodes legal on a 32-bit name; (9) `x1`–`x7` illegal as
any operand, which makes `ret` illegal; (10) `jal`/`jalr` with `rd` ≠ `x0` illegal and `auipc`
restricted to a `d` destination.

---

## APPENDIX — THE THREE OPEN QUESTIONS, RESTATED FOR A ONE-WORD ANSWER

| | question | recommendation |
|---|---|---|
| **QA** | Accept that the context is *"any combination of 64- and 32-bit values, with everything narrower charged 32"* — **81 placeable shapes against the per-function map's 13,091** — or reinstate the map? | **Accept.** |
| **QB** | Does `f`_n_ ≡ `x`_n_ (24 names, two complete tiers, one allocation pool, ~~81~~ **2,685** shapes), or do the namespaces name different bits (56 names, three complete tiers, two pools, ~~969~~ **9,132** shapes, and a 16-bit tier with no arithmetic to run on it unless O4 is amended)? | **`f`_n_ ≡ `x`_n_.** |
| **QC** | Ratify two tier-1 supersessions: (i) CANON.md:9819 *"the namespaces do not alias"*, and (ii) I.7 item 3's *"a function needing dynamic rounding modes … cannot be offloaded"*, softened here to a build-time gate. | **Supersede both**, with the divergence on the price list rather than in a footnote. |
