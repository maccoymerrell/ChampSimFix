# THE REGISTER MAP — FINAL PROPOSAL

**PROPOSAL — NOT IN THE CANON UNTIL RULED.** Nothing in this file may be cited as a decision.
It is the deliverable asked for on 2026-09-03, verbatim: *"I would prefer a proposal for 1 or 2
suggested designs is written up for the end of this, with full consideration of implementation
complexity, performance impact, and overall simplicity."*
(`register-map-fallback-user.md`, "Clarification (user, 2026-09-03, verbatim)".)

**What this file supersedes among the proposals.** It is the head of the pass. It carries
`final-A-aliasing.md` and `final-B-context-map.md` forward with **twenty-six corrections** — ten of
them made in this revision, against this file's own earlier text — **eight of which change a number
the reader is asked to rule on**, and it adds a **third design, A2**, that the two source documents
cost nowhere. Every correction is checkable and §5.6 lists them in one place, with a full CANON.md
citation audit. The two source documents remain the long form; where they disagree with
this one, **this one is the later work**.

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

**Prerequisites.** `register-map-facts.md` (fact-C1–C24), `register-map-context.md` (M1–M15,
cons-C1–C32), `final-A-aliasing.md`, `final-B-context-map.md`.

**Standard.** Every claim cites a spec chapter, a `file:line`, a measured number, or a user
statement. Numbers that are configuration are labelled `[CONFIGURATION]`. Numbers computed here
are labelled **[recomputed]** and the enumeration that produces them is stated so it can be run
again.

---

## §0 ONE PAGE FOR THE USER

### The designs, in plain terms

**A — FIXED ALIASING.** *The register number **is** the bit range.* `x8`–`x15` are the eight
64-bit tiles of the 512; `x16`–`x31` are the sixteen 32-bit tiles; the two halves of `x`*n* are
`x`*2n* and `x`*2n+1*; `x0` reads zero; `x1`–`x7` would name tree nodes wider than 64 bits, which
nothing in the subset can compute on, so they are illegal. Decode is **eight gates, one logic
level**. Nothing is fetched, nothing is cached, nothing travels, nothing can go stale. Migration
stays 72 B and a context arriving on a tile it has never visited is **immediately executable**.

**A2 — FIXED ALIASING PLUS EXTENT INSTRUCTIONS.** *The register number is still the bit range;
below 32 bits, the instruction's own immediate is.* Two new instructions in RISC-V's `custom-1`
opcode — an extract and an insert — carry a **9-bit extent descriptor `{index:6, width_code:3}`**
that names a naturally-aligned slice of the 512. **That descriptor carries the same information in
the same nine bits as design B's F2 map entry (§4.1) — the same two fields, the same widths, the
same shift function — moved out of a fetched table and into the instruction word** *(field order,
code numbering and the shift constant differ; §3A.1 states the difference)* — which is the
strongest available answer to your ruling, because the ruling's own list of objects
(*"the map, instruction, and potentially data"*) already contains the instruction. It reaches the
byte tier **in one instruction where design A reaches it in about three**, at **+1 instruction per
packed read and +1 per packed write against A's ~2-3, and zero on every other access** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
(A reaches the byte tier too, by packing and shift-and-mask through a scratch name; **A2 is a speed
optimisation, never a capability fix**): a function that fits under A emits no extent instruction at all, and both
measured functions (§5.4) fit. **It costs a tier-1 reopening** — cons-C26/R84, *"NO BIT-FIELD
INSERT/EXTRACT INSTRUCTION CARRYING AN OFFSET AND A WIDTH… Let's not overdesign"* (#233) — and
cons-C22's settled count of twelve instructions. **§3A is the design; Q5 is the ruling it needs.**

**B — THE MAP AS AN EXTENSION OF THE CONTEXT.** Your sketch, built out and costed. Each function
publishes a 40–76 B map beside its own code on its duplicate page; each tile caches eight of them
in a 576 B on-core file; a context carries a **3-bit index** to its map, never the map. Migration
stays 72 B. Three things came out of building it that are worth keeping whatever you rule:

1. **The entry PC does not survive migration.** `MigrationEvent` carries `handle`, `pc`, `ctx[8]`,
   `faultAddr`, `origin`, `from`, `to`, `wantsReturn` — **no entry PC and no function id**
   (`NMFCFabric.h:94-123`) — and `handleMigration` sets `c.pc = mig.pc`, a *resumption* PC
   (`NMFCTile.cc:1385`). So the handle-indexed array of your requirement 1 cannot identify its own
   function on arrival, which is the case it exists to serve.
2. **`CONT` changes the function under an unchanging handle, and it cannot fail** (CANON.md:6243-6250).
   So the identity check is not only about handle *reuse*; it must run at every `CONT` too, which
   no handle-indexed cache does by construction.
3. **Once the identity tag your requirement 3 demands exists, keying on the function is strictly
   better than keying on the handle.** On the record's worst measured migration rate — 196,904
   migrations for 262,143 loads (CANON.md:4977) — function-keyed is **4 cold fills for the whole
   run**; handle-keyed is **≈125,080**, roughly doubling arrival cost on 63.5% of migrations.
   Your requirement 4 — *"the function should never have to fetch its regfile map"* — is met only
   by the function-keyed form.

**B2 — WIDTH CLASSES.** Not a design in its own right: **A plus a two-bit field.** Four fixed layouts chosen
per context, of which **class 0 *is* design A**. It costs 2 bits in the context's tile-local slot
and one mux level, it buys **direct names for** a byte tier and a halfword tier — not the tiers
themselves, which A already holds by packing `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` — and, like A and unlike B1, it
**references no object at decode**. `A ⊂ B2 ⊂ B1` in **directness** and cost; **in capacity they
are equal at 512 bits** — **but `A2` is not on that line**: it is incomparable
with B2, reaching further for a different kind of price (§3A.8, Q1).

### The scores

Three lenses per design, scored on the two long-form documents. **A2's row is scored here for the
first time**, on the same three criteria, by the same three lenses, against §3A.

> **[SCORED BEFORE THIS FILE'S CORRECTIONS — AND NOT RE-SCORED. Read the table as an ordering, not
> as a measurement.]** Every row was produced against the two long-form documents, i.e. before the
> twenty-six corrections in §5.6. Three of those corrections bear on rows nobody went back and
> re-scored. **Correction 1** (~~factor 162 → 4.9~~ — itself now superseded: **there is no
> expressiveness penalty at all**, `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`) removes the whole of the penalty A was
> being forgiven for. **Correction 11** withdraws B's *"only candidate that meets C14"* claim, which
> is what its architect lens was paying for. **Correction 10** finds that the RFT as specified matches
> ≈ 19 instructions, so B1's *"four cold fills for the entire run"* and *"0 memory accesses, 0 added
> cycles"* — the evidence behind its **performance 8.0** — do not hold. **The performance column is
> the weakest of the three, and it is the one the headline below leans on.**

| design | lens | implementation complexity | performance impact | overall simplicity |
|---|---|---|---|---|
| **A** | RTL engineer | **8** | **8** | **7** |
| **A** | compiler engineer | **6** | **8** | **6** |
| **A** | architect | **8** | **8** | **7** |
| **A** | **mean** | **7.3** | **8.0** | **6.7** |
| **A2** | RTL engineer | **7** | **8** | **6** |
| **A2** | compiler engineer | **5** | **8** | **5** |
| **A2** | architect | **7** | **8** | **6** |
| **A2** | **mean** | **6.3** | **8.0** | **5.7** |
| **B** (B1b) | RTL engineer | **5** | **8** | **5** |
| **B** (B1b) | compiler engineer | **3** | **8** | **3** |
| **B** (B1b) | architect | **4** | **8** | **3** |
| **B** (B1b) | **mean** | **4.0** | **8.0** | **3.7** |

**Grand means: A = 7.3, A2 = 6.7, B = 5.2.** All three were **scored 8.0 on performance** and they
separate on complexity and simplicity. **That tie is the least load-bearing number on this page and
should not be promoted into a finding:** it inherits §5.3's `[ASSUMPTION]` that the register-read
stage has two mux levels of timing slack — no fmax, no process node, no FO4 figure and no slack
demonstration exists anywhere in the record — and B1's 8.0 rests on cold-fill figures correction 10
withdrew. **What survives is the ordering, not the tie:** nothing in the record points at the
register map as where this machine's cycles go, so the choice is being made on complexity and
simplicity because that is where the evidence is — not because speed was measured and found equal.

**A2 loses 0.6 to A and beats B by 1.5**, and the whole of its 0.6 is bought back in expressiveness: it places **13,809** of the **17,361**
width-multisets **it can name DIRECTLY** against A's **2,685** **[recomputed, §5.2]** — `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
**A expresses all 17,361 as well, by packing, at ~2-3 ops per access.** The counts are an
instruction-count measure and **not** a capacity measure. B2 was not scored as a design in its own right; the one lens that
priced it in isolation is the architect on B, whose text reads *"complexity 4 (B2 alone would be
8) … simplicity 3 (B2 alone would be 8)"*. Three of the six lenses independently concluded that
B2, not B1, is the variant worth putting to you — a finding this document adopts.

### Recommendation, and the one reason it turns on

> **RULE IN DESIGN A, WITH BOTH ESCAPE HATCHES RESERVED: the two-bit class field (A = class 0)
> AND the `custom-1` opcode (A2's extent instructions).**

**Adding A2 did not change the verdict, and it is worth saying why, because it nearly did.** A2
scores 6.7 to A's 7.3 and **names directly** 5.1× as many width-multisets (13,809 against 2,685,
§5.2) — `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **a speed difference; both reach every multiset that fits in 512 bits.** It loses on three things and
only three: it **reopens a tier-1 rejection** (cons-C26/R84) and a tier-1 count (cons-C22, twelve
instructions), which is a ruling only you can make; it adds a decision to the compiler that A does
not have (use an extent instruction, or pack it by hand in ~2-3 ops) `[corrected - user ruling
2026-09-03 (liveness)]`; and **nothing in the record has ever asked for it** — the two decompositions in §5.4 show no sub-32-bit value, **so on today's evidence A2's
entire benefit is hypothetical while its costs are certain.**

> **[AND THAT EVIDENCE IS WEAKER THAN "neither measured function carries a sub-32-bit value", WHICH
> IS HOW AN EARLIER REVISION PUT IT.]** §3.8 states the consequence of its own correction:
> *"§5.4's two measured decompositions are not currently reproducible from the tool and must be
> re-measured."* The tool that produced them charges **16 bits for `a0`** and 128 for `x10`+
> (`annotate.cc:461-470`, executed) — **it cannot see widths at all.** So *"neither function carries
> a narrow value"* is **not a measurement**; it is the absence of one. **Q1 and Q5 are both deferred
> on this, and the honest statement of the ground is "nothing in the record has ever asked for it,
> and nothing in the record could currently tell us if something had."** §10 names the width
> histogram that would settle it — a change to an existing tool, not a design question.

**Rule A now; A2 is what you build the day a measured function needs a byte.**

**The one reason, stated the right way round — and an earlier revision of this page had it
backwards.** It said *"A is the only design in which decoding a register name references no object at
all."* **That is false by this document's own §5.3 row** — *memory references to decode a register
name*: **A ZERO, A2 ZERO, B2 ZERO** — and by §4.8, which says of B2 that *"there is no
table, nothing is fetched… the ruling's objection does not apply to it at all."* **The true statement
is the inverse: B1 is the only design that references an object.**

> **Your 2026-09-03 ruling eliminates exactly one candidate — B1 — and it eliminates it decisively,
> in B's own words:** *"Design B does not remove the third object. **It is the third object,
> rebuilt.**"* **It does not choose among A, A2 and B2; all three satisfy it in full.**

**A is chosen among those three on the other two criteria, not on the ruling.** Of the three it is
the only one that adds **no per-context bit, no envelope bit, no mux level on any path, no ROM beyond
310 bits, and no tier-1 reopening.** B2 buys a width menu for 2 bits and one mux level on every
decode forever; A2 buys **one-instruction access to the byte tier** — which A already reaches in
~2-3 ops — for two instructions and two tier-1 reopenings `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`. Those are real
trades either of which you could reasonably take — **and the ruling is not what decides between them.
Saying that it was made a live three-way engineering choice look like a settled question.**

**Reserving the class field is why this recommendation costs you nothing later.** B2 is A plus two
bits, and those two bits fit in the migration *envelope* beside `handle` and `origin`, which
already travel and are already free (`NMFCFabric.h:107-108` — `SIZE_BYTES = NMFC_CTX_BYTES + 8` is
the **payload**, and the 72 B is the payload). So ruling A in now does **not** foreclose the byte
tier: it defers it behind a purely additive field, which is the same discipline as A's own
reserved names — *defined names cannot be undefined; reserved ones can be defined later.*

> **[AND IT IS COUPLED TO Q2, WHICH AN EARLIER REVISION MISSED.]** B2's class inventories are written
> in §4.7 over **two** namespaces: class 3 is `x8`–`x11` = 4 × 64 plus `f0`–`f31` = 32 × 8, which
> needs **36 names**. Ruling Q2 *"yes"* leaves **31 encodable names in one namespace**, so class 3 as
> written is **unrepresentable**, and with it the *"32 byte names over half the file"* that §0's Q1
> row and §10 credit B2 with. **The repair is arithmetic, not a ruling, and it is in §4.7 now:** a
> complete one-namespace byte class is `5 × 64` (bits 0–319) + `24 × 8` (bits 320–511) = **29 names,
> 512 bits**, five pointers and twenty-four byte names. **The byte class itself gets stronger** —
> **1,400** **directly nameable** width-multisets against the two-namespace class 3's **1,015** —
> while **the four-class union gets 32 weaker, 4,175 → 4,143**, because no one-namespace class can
> **directly name** thirty-two simultaneous byte values **[all recomputed]** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` (it can
> still **hold** them, packed). **So Q2 = yes reshapes B2's byte tier rather
> than deleting it, and the class field is worth within 1% of the same either way** — which is why Q2
> can be ruled without deciding Q1. What is now wrong wherever it appears is the phrase *"32 byte
> names over half the file"*.

### What you must acknowledge

> **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** *(dated 2026-09-03)* **Nothing, under A.**
> Both "limits" this section asked you to acknowledge are struck. **The byte tier is REACHABLE**
> under design A — sixty-four live bytes pack eight per 64-bit name and are read and written by
> shift-and-mask through a **scratch** name, plain RV64I, no new instruction — and **no value is
> charged 32**: admission counts **bits of peak liveness plus the scratch bits the packing needs**,
> each value at its own width, with at least one spare name for staging. **I2's "16 4-byte regs, 64
> 1-byte regs, or ANY combination" stands unnarrowed**, and under cons-C15 nothing is rejected that
> K.6 admits. The 5-bit field decides only how *directly* a value is named, which is **instruction
> count**: ~2-3 extra ops per access to a packed sub-name value. **Where this page said the loss
> "belongs to the 5-bit register field": it belongs to nothing — there is no loss of capability,
> only of directness.** The struck text follows for the record.

**~~Under A — the byte-tier cap and the charge-32 rule.~~** ~~Together they narrow I2's *"64 1-byte
regs, or ANY combination"* (#232, CANON.md:809-816) to **"any combination of 64- and 32-bit values,
with everything narrower charged 32."** Two different provenances, and they must not be merged: the
byte-tier cap is **arithmetic, and it belongs to the family of schemes in which the 5-bit register
field names the operand — not to your ruling**; the charge-32 rule is **this design's own choice**,
and under cons-C15 a function it rejects cannot run at all.~~ **[EVERY CLAUSE STRUCK.]**

> **[THE CAP — AND IT IS A CAP ON DIRECT NAMES ONLY.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` The arithmetic below counts
> **names**; it bounds instruction count, not what the file can hold. "Misses by 33 names" is true
> and means: 33 byte values are reached by shift-and-mask instead of by name.**]** A register field is
> five bits and `x0` is hardwired zero, so a **two-namespace** scheme has **63** nameable slices
> against the **64** a complete byte tier needs — *"misses by exactly one name"*, which is the figure
> §2.2 and every restatement here used. **Under the recommended Q2 = yes there is one namespace, so
> the count is 31, not 63, and I2's "64 1-byte regs" misses by 33 names.** The verdict is unchanged
> and in fact stronger; the arithmetic behind it is not the arithmetic this page was quoting, and
> anywhere the figure 63 appears it is the **Q2 = no** figure. **The exact statement of the invariant:
> the cap binds every scheme in which the operand is named by the 5-bit register field — which
> includes design A, B2, *and* B1's free map — and it is escaped only by naming the operand somewhere
> else, which is what A2 does.**

> **[ATTRIBUTION — CORRECTED AGAIN, AND THE BILL IS ZERO.]** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` `final-A-aliasing.md`
> §13.1 calls the byte-tier loss *"a consequence of the 2026-09-03 ruling, not of this design"*; an
> earlier revision of this page repeated it; a later revision reattributed it to the 5-bit register
> field. **All three are wrong, because there is no byte-tier loss to attribute.** Design A reaches
> the byte tier by packing, at ~2-3 ops per access; A2 reaches it in one instruction. **What your
> ruling costs on this axis is nothing, and what the register field costs is instructions, not the
> tier.** Both bills are real; neither is a loss of capability.

> **CORRECTION YOU SHOULD SEE BEFORE RULING — NOW ITSELF SUPERSEDED, AND THE REPLACEMENT IS
> SIMPLER.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **There is no expressiveness loss to price.** Every count in the paragraph
> below — 81, 2,685, 13,091, 17,361, factor 162, factor 4.9, factor 6.5, "79.5%", "99.4%" — counts
> **directly nameable** width-multisets, i.e. how often an access is one instruction instead of
> ~2-3. **A, A2, B2 and B1 all hold 512 bits and express every width**, because a value no name fits
> is packed and reached through a scratch name. **The comparison collapses: identical capacity, and
> the only difference is instruction count on sub-name accesses — A ≈ 2-3 extra ops, B1 a map
> lookup.** Read §5.2's table under that heading and it is still useful; read it as capability and
> it is wrong. The struck paragraph follows.
>
> ~~`final-A-aliasing.md` §2.3 prices this loss at
> *"81 against 13,091 — a factor of 162."* **That is a category error of 33×, in the pessimistic
> direction**, and every restatement of it inherits the error. The 81 counts (a, b) pairs over a
> **two-width** alphabet; the 13,091 counts multisets over a **four-width** one. They are not the
> same kind of object. Counted like for like — how many four-width multisets does A actually place
> under its own charge-32 rule — the answer is **2,685**, which is **20.5% of the 13,091 a
> one-namespace free map reaches (a factor of 4.9)** and **15.5% of the full 17,361-multiset universe
> (a factor of 6.5)** **[all recomputed, §5.2]**. A's §2.2 already implies it: a narrow value is
> charged 32 and *still runs*. **The loss is 79.5% against B1 under the same Q2 ruling, not 99.4%**,
> and that is a materially easier thing to accept.~~ **[STRUCK — the loss is zero.]**

**Under B — the third referenced object.** In B's own words. What softens it is real and none of
it is a rebuttal: memory is touched `N × F` times for a whole program (four times on the measured
stress run), and after that the map is an on-core 576 B array read like a decode ROM. What
survives is the part you objected to: the object exists, it has to be filled, kept, invalidated,
and got wrong — four new run-time failure modes that A does not have.

**One further correction, because it weakens B's best argument.** B claims to restore cons-C14 as
a machine guarantee. It restores **half** of it. The fill-time check detects a *malformed map* — an
overlap the emitter wrote — and the run-time trap fires on a name the map leaves *undefined*. It
does **not** detect over-liveness: an allocator that runs out of bits and coalesces two
simultaneously-live values onto one name emits a perfectly legal, non-overlapping map, the tile
accepts it, and the wrong answer is as silent as under A. The undefined-name trap is a real gain
and is the honest basis for Q4; "the only candidate that meets C14" is not.

**Under A — SW1, a silent-wrong-answer class, and it belongs on this page.** §5.5 says of it that it
*"must appear here rather than in a footnote"* and §3.6a calls `d0` *"the one place design A is less
safe than the mechanism it replaces."* `d0` and `w0`/`w1` are the same bits; two values simultaneously
live in `d0` and in `w1` clobber each other, **and no decode check in any of the four candidates can
see it** — a total map leaves `RegLayout::illegal()` nothing to fire on. **Its only defence is a
~40-line placement verifier in the admission tool that does not exist yet** (§6.2, §9 Step 6.4), and
whose owner is not decided (§9.1). *Two honest qualifications, both stated at §3.6a:* **B1 does not
catch SW1 either**, so it is not a reason to prefer any candidate over any other; and **the concrete
instance §3.6a names — `s0` = `x8` = `d0` clobbered by a 7th/8th integer argument in `a6`/`a7` =
`w0`/`w1` — is not reachable on either toolchain path §3.7 specifies**, because the day-one path
`-ffixed`es `x16`–`x31` out and the `W`-tier path declares `W` as sub-register indices of `D`, which
a stock allocator will not co-allocate. **What that means is that the class is real and the published
example of it is not; the verifier is still required, for hand-written bodies, for the tool's own FFD
placement, and for any back end that does not declare the sub-register relation.**

**Under A — a compiler bill this page did not carry.** §3.7 commits ruling A in to **a new register
file description, new `SubRegIndices`, a new encoding map, and a post-RA gate that turns any
surviving stack access into a compile error** — the last of which §3.7 itself calls *"the gap nobody
has costed"*, because under `-ffixed` an exhausted `D` class makes LLVM spill to the stack, which
under I7 and cons-C15 is fatal and arrives as ordinary correct-looking codegen with no diagnostic.
**§3.7's own estimate line says the "weeks, not months" figure has "nothing behind it; treat it as an
engineer's guess."** Two further items on the critical path are not written anywhere and not in any
checked estimate: **the per-opcode required-width table** the decoder's legality rule 2 needs, and the
**~150-entry opcode→width table** `annotate` needs (§9.1).

**Under A and A2 alike — the timing label, which §5.3 says must not be a footnote.** *"+2 mux levels,
+0 cycles"* is an **assumption, not a result**: there is no fmax, no process node, no FO4 figure and
**no demonstration anywhere in the record that the register-read stage has two levels of slack**, and
the two levels sit after the context SRAM output on the data path into the ALU — conventionally the
tight path. **Every zero in §5.3's table but three inherits it.** It does not separate the candidates.
It does mean the performance column is a claim about a machine nobody has timed, and **a timing budget
is the single cheapest measurement that would retire it.**

### The five questions

**Only these five change the design** — everything else in §§3–4 is a recommendation. **But they are
not five of a kind, and reading them as five equal rows misspends the attention this page is asking
for:**

- **Q2, Q3 and Q4 decide what is built now.** Q2 changes the namespace, the admission pool and the
  day-one toolchain spelling, and it supersedes canon. Q3 is a supersession of a canon ruling.
  Q4 ruled *"requirement"* eliminates A, A2 and B2 together and reinstates the third object.
- **Q1 and Q5 reserve future options and change no step of §9's build order.** Q1's recommended
  answer is explicitly a non-choice — *"reserving is not choosing"* — and Q5's own recommendation
  says answering **no** *"is a complete answer that closes A2 and changes nothing else."*
- **The body ranks one of them above the rest, and it is not the one the narrative above dwells on.**
  §5.5 and §10 both say of **Q3**: *"This is the worst failure mode in either design: the same
  encoding computes different results on host and tile, and nothing can see it."* **If you rule one
  question carefully, rule Q3.**

| | question | recommendation |
|---|---|---|
| **Q1** *(reserves an option; changes nothing built now)* | **Where do you stop?** The ladder is no longer a line — it is `A ⊂ {B2, A2} ⊂ B1`. Counted on one universe of **17,361** width-multisets: **A** places **2,685** and references nothing. **B2** places **4,143** (**4,175** if Q2 is ruled *no*), reaches a complete byte class over the top 192 bits, references nothing, and taxes **every** decode on the tile forever with one mux level. **A2** places **13,809** under its closed-form rule, reaches the byte tier at any aligned offset, references nothing, and taxes **only the functions that use it** — for two instructions in `custom-1` and two tier-1 reopenings. **B1** places **13,091** under Q2 = yes and **17,360** under Q2 = no, and **is** the third object. **[all recomputed, §5.2]** | **A now, and reserve BOTH hatches** — the class field costs 2 free envelope bits, `custom-1` costs an opcode `nmfc_isa.h:18-20` already holds free. Nothing in the record measures a demand for either. **If one day exactly one is built, A2 is the better buy** — 3.3× B2's expressiveness for zero cost on the common path — **provided you are willing to reopen R84 and cons-C22 (Q5).** `[user ruling 2026-09-03 (liveness)]` **These multiset counts are DIRECT-NAMING figures, not capacity: A, A2, B2 and B1 all hold 512 bits and express every width, so "places" means "names in one instruction" and the rest are packed at ~2-3 ops per access.** |
| **Q2** *(decides what is built now; supersedes canon in four places, plus O4's spelling)* | **Does `f`*n* ≡ `x`*n*?** **Yes:** 24 names, two complete tiers, one allocation pool, 2,685 shapes — **and it supersedes four canon statements, not one** (the full ledger is §6.3): CANON.md:9849's *"the namespaces do not alias"*, the O4 ruling row's restatement of it at CANON.md:127, and I.0's four-point answer at CANON.md:6029-6060 (*"the `f`-names do not overlay the `x`-names"*, whose point 1 also states the *"per-function binding from register name to bit range… carried with the offload"* that design A deletes outright). **It also amends O4's spelling**: `f`*n* ≡ `x`*n* **is** Zfinx, and the ratified spec makes F/D and Zfinx mutually exclusive, so `RV64IMAFD` becomes `RV64IMA_Zfinx_Zdinx` (§3.7, §6.8, facts §6.1). O4's substance — *"I think we want float, so C"* — is untouched; its opcode-list spelling is not. **No:** 56 names, three complete tiers, two pools, **9,165** shapes **[recomputed]** — and a 16-bit tier with no arithmetic to run on it unless O4 is amended anyway. | **Yes, `f`*n* ≡ `x`*n*** — it makes K.6's "third wrong answer" (two pools admitting a function twice the legal size) structurally unrepresentable. **But rule it knowing it carries the O4 spelling amendment with it**; an earlier revision of this page adopted Zfinx semantics while asserting O4 stood unamended, which facts §6.1 records as impossible. `[user ruling 2026-09-03 (liveness)]` **These multiset counts are DIRECT-NAMING figures, not capacity: A, A2, B2 and B1 all hold 512 bits and express every width, so "places" means "names in one instruction" and the rest are packed at ~2-3 ops per access.** |
| **Q3** *(decides what is built now; the body's own worst failure mode)* | **Supersede I.7 item 3?** It says *"a function needing dynamic rounding modes … **cannot be offloaded**."* A and B both define `rm = DYN` as RNE instead, because every stock FP instruction carries DYN. The replacement is a build-time gate — and **the gate cannot see the case that matters**: the divergence is caused by the **caller's** `fcsr.frm`, in another translation unit, which `annotate` never walks. **§5.5 and §10 both rank this the worst failure mode in either design: the same encoding computes different results on host and tile, and nothing can see it.** | **Your call, not the document's.** Recommended: supersede, with the divergence on the price list. Rejecting DYN rejects all stock FP codegen; accepting it means a program that called `fesetround()` gets a different answer on the tile, silently. **This is the row to spend your attention on.** |
| **Q4** *(decides what is built now)* | **Is the run-time undefined-register trap a requirement or a preference?** Only B1 has it, and only the undefined-*name* half (over-liveness — SW1, §3.6a — stays silent under all four). `RegLayout::illegal()` fires today at `NMFCTile.cc:464`/`:472`. Under A, A2 and B2 it has nothing to fire on and the check re-homes to build time. | **Preference.** If you rule it a **requirement**, it eliminates A, A2 and B2 together and forces B1 — so it is worth ruling explicitly rather than by omission. |
| **Q5** *(reserves an option; changes nothing built now)* | **Do you reopen R84 / cons-C26 — a bit-field insert/extract carrying an offset and a width — AND cons-C22, the settled count of twelve user-level instructions?** **Both are tier 1 and A2 needs both**; without both it cannot be built whatever it scores. On your own words at #233: *"We need to make sure EXTRACTION from the regs is possible. Regular bit manipulation can take you the rest of the way… Let's not overdesign."* Answering **no** kills A2 outright, whatever it scores. The one fact that bears on reopening, and that the record does not currently contain: **R84's stated reason — "it duplicates instructions RV64I already has" — is true of extract and false of insert.** RV64I reaches a packed field in **2** instructions and writes one back in **5–8 plus a 64-bit scratch name** (§3A.4), because `andi`'s immediate is 12 bits and there is no bit-field insert in the base ISA. Under a 512-bit budget with no stack, that scratch name is the cost that decides it. | **Not the document's call.** *If you answer no*, A2 is closed and the ladder is `A ⊂ B2 ⊂ B1` as before — nothing else in this proposal changes. *If you answer yes*, reserve `custom-1` now and build nothing until a measurement asks. **The narrow form that satisfies #233 literally — extract only, no insert (A2-r, §3A.7) — costs one instruction instead of two and is worth ruling on separately.** |

---

## §1 THE PROBLEM

### 1.1 The question, in one sentence

A context is **512 bits, bit-packed** (I2, #232/#238, restated 2026-09-03), and a function core
executes `RV64IMAFD` (ruling O4, CANON.md:9849). An ordinary RISC-V instruction names its operands
with **5-bit fields**. On a stock core each field selects one of 32 XLEN-wide registers. **Here
there is no such file.** So: *what does the 5-bit register field mean?*

### 1.2 What the record had already settled before this pass

- The context is **512 bits, bit-packed, packed compile-side**, and admission is **in bits**. Not
  eight registers — I2's `[SHARPENED]` block quotes #238 verbatim: *"Once again, NO. 512 bits of
  context. The context is not 8 regs. Why do you keep reverting to that?"* (CANON.md:814-815).
- **512 in, 512 out** (I2); **migration is 512 bits + PC = 72 B** (I11, CANON.md:1331-1334);
  **no stack** (I7); **`RV64IMAFD`** (O4); code lives on duplicate pages resident on every tile;
  **the type comes from the opcode**, never from the register name (fact-C9, and Zfinx is the
  ratified proof).
- The **per-function map is already built** — `RegLayout` in `NMFCRegLayout.h:39-73`, consulted on
  the decode path at `NMFCTile.cc:461-475` (`return c.regs.read( layout_.field[r] );`), one entry
  per resident function at `NMFCTile.h:448-450`, described at DESIGN §25.7 **D:2425-2429**. So any
  replacement is also a *deletion*, and the deletion has to be marked (cons-C31).

### 1.3 The ruling that set the problem, verbatim

> "I really don't like your idea. **It introduces a third piece of memory every context needs.**
> So now we have the map, instruction, and potentially data that must be referenced all at the
> same time. That frankly seems foolish." — user, 2026-09-03

The stated ground is **simultaneity**, and cons-C32 forbids attributing any other. It is not
"tables are slow", not "decode latency", not "it leaks into migration". A proposal may not answer
it by pointing at duplicate pages again (`register-map-context.md` §6, the standing prohibition):
the objection was to the *third object*, not to its availability.

### 1.4 The clarification that set the deliverable, verbatim

> "I want to clarify, **the map is best thought of as an extension of the context.** Ideally it
> doesn't follow with migration, **it is retrieved post-migration on the new core.** However, if
> the handle-index cache doesn't seem viable (**it would need a port for the width of the machine
> or we would need contexts aligned to widths so the entire system could be banked-per-width**
> (potentially necessary anyway)). All things to consider."

Three obligations follow and all three are discharged: the handle-index cache is **costed and
found not viable** (§4.5, and the reason is not the port — it is the identity, §4.2); the port
question is **answered** (§4.6); and **banked-per-width is evaluated as a design in its own
right** (§4.7), which is what that file's own Consequences section required.

### 1.5 What the answer must satisfy

Thirty-two constraints are listed at `register-map-context.md` §10. The ones that do real work
here, and that separate the candidates rather than being met by all of them:

| | constraint | which candidate it separates |
|---|---|---|
| **cons-C1/C2** | no state outside the 512 bits and the encoding; **no third referenced object at decode** | **A, A2 and B2 meet it. B1 does not, and says so.** A2 meets it in the strongest form available — *"the encoding"* is where its geometry literally is |
| **cons-C3** | 512 bits bit-packed, **not eight registers** | separates every candidate from `defaultLayout()` (cons-C30) |
| **cons-C6** | migration is **exactly 72 B** | met by all four under their recommended identity mechanism; **A and A2 add nothing to the envelope either** |
| **cons-C14** | an undefined register is a **hard error** | **only B1, and only half of it** (§0, §4.4). **A2 does not restore it** — an instruction always carries a defined descriptor, so there is no undefined name to trap on (§3A.1) |
| **cons-C15** | rejection is **fatal**; no truncation, no spill | bites only on a genuine overflow of 512 bits (live plus scratch). `[CORRECTED - user ruling 2026-09-03 (liveness)]` ~~makes A's charge-32 rule a real cost~~ — **there is no charge-32 rule**; A charges each value its own width |
| **cons-C17/C18** | peak liveness **in bits, one pool**; not a count of names | A meets it because both its tilings are complete; a name count would re-introduce R30 |
| **cons-C21** | `nmfc_bu` (480 bits) and `nmfc_expand` (384 bits) must still fit | all four admit both (§5.4), and **A2 compiles both to design-A code exactly** |
| **cons-C22/C26** | **twelve user-level instructions**; **no bit-field insert/extract carrying an offset and a width** | **A, B2 and B1 all meet both. A2 meets neither** — it is the only candidate that needs a tier-1 reopening, and that is Q5 |
| **cons-C31** | supersessions must be **marked** | **A must mark five** (§6.3 — DESIGN §25.7, CANON.md:9849, CANON.md:127, CANON.md:6029-6060, and O4's spelling; an earlier revision said two and named one, by a wrong line number); **A2 must mark seven**; B1 marks none and *completes* DESIGN §25.7 instead |

---

## §2 WHAT RISC-V PROVIDES, AND WHAT IT CANNOT

Drawn from `register-map-facts.md`, which verified two dozen claims against the ratified
Unprivileged ISA Manual **v20260120**. Only the load-bearing findings are restated.

### 2.1 The gap is in *naming*, and nothing else

**RV64 cannot let a 5-bit register field denote a sub-XLEN slice** (fact-C12). The register file
is defined at one width — Ch. 2 — and there is no architectural name for a half, a byte, or a bit.

But **RV64 computes on sub-XLEN widths freely** (fact-C14, which corrects the claim that it
cannot): `lb`/`lh`/`lw`/`lbu`/`lhu`/`lwu` and their stores move 8/16/32-bit fields with explicit
extension; `Zbb` has `sext.b`/`sext.h`/`zext.h`; `Zbs` has `bext`/`bexti`; `Zbkb` has `pack`,
`packh`, `packw`, which *assemble* sub-register fields. **So the design problem is architectural
naming and nothing else** — a sharper framing than "RISC-V can't do narrow registers", and the one
this proposal is built on.

### 2.2 The arithmetic that caps every scheme

A register field is five bits (Ch. 2), so each namespace affords 32 encodings. **`x0` is hardwired
zero** and cannot be a slice without invalidating `nop` (`addi x0,x0,0`), `j` (`jal x0`), `ret`,
every discard idiom and the whole HINT space (fact-C17). `f0` **is** general — the FP file has no
hardwired zero.

> **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** *(dated 2026-09-03)* **This arithmetic caps
> DIRECT NAMING, not capability.** A complete byte tier needs 64 **names** and no register-field
> scheme has them — but every scheme **holds** sixty-four live bytes, packed eight per 64-bit name
> and reached by shift-and-mask through a **scratch** name (plain RV64I, ~2-3 ops per access).
> **#232's "64 1-byte regs" is REACHABLE under both Q2 answers**, and the "shortfall" column below
> counts how many byte values must be reached indirectly — an **instruction-count** figure. Canon
> I2's *"ANY combination"* stands unnarrowed. Read the table with that heading; the struck
> conclusion is marked in place.
>
> **A complete byte tier needs 64 names. No register-field scheme has them.** Complete coverage at
> width *w* costs 512/*w* names: 8 at 64, 16 at 32, 32 at 16, **64 at 8**. The nameable count depends
> on **Q2**, and both answers fall short:
>
> | | nameable slices | shortfall against a byte tier |
> |---|---|---|
> | **two namespaces** (Q2 = no): `x1`–`x31` + `f0`–`f31` | **63** — 512/63 ≈ 8.1 bits per name | **misses by exactly one NAME** — the 64th byte is packed and reached through a scratch name `[user ruling 2026-09-03 (liveness)]` |
> | **one namespace** (Q2 = yes, the recommendation): `x1`–`x31` | **31** | **misses by 33 NAMES** — those 33 bytes are packed, ~2-3 ops per access `[user ruling 2026-09-03 (liveness)]` |
>
> ~~**#232's "64 1-byte regs" is unreachable under both**, and under the recommended ruling it is
> unreachable by a wide margin, not by one.~~ **[STRUCK - user ruling 2026-09-03 (liveness)]** —
> **it is reachable under both.** What differs is how many of the 64 bytes carry a direct name: 63
> under Q2 = no, 31 under Q2 = yes, and the rest are packed at ~2-3 ops per access. *(An earlier revision of this page quoted only the 63 and
> the "misses by exactly one name", which is the **Q2 = no** figure; wherever 63 appears in §4.1,
> §5.1 or §5.2 it is likewise the two-namespace figure and is labelled so from here on.)*

**The invariant, stated exactly and corrected** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`**:** the cap binds **how many slices
can be named directly** in every scheme where the operand is named by the 5-bit register field —
design A, B2, **and B1's free map**, a fetched table indexed by that same field. It is escaped by
naming the operand somewhere else, which is what **A2** does with an immediate — **and that buys
one instruction per access, not a capability.** It is **not** a consequence of the 2026-09-03
ruling, and it caps nothing the machine can hold: **512 bits, every width, under every one of
them.**

### 2.3 The one family that reaches the byte tier **IN ONE INSTRUCTION** without a third object — **this is design A2**

> **[CORRECTED - user ruling 2026-09-03 (liveness)]** Design A reaches the byte tier too, by packing
> and shift-and-mask through a scratch name (~2-3 ops per access). What this family buys is the same
> access in **one** instruction. It is a speed optimisation, never a capability fix.

`register-map-facts.md` C22 records that the honest comparison *"was never put"*, and it is put
here. **CORE-V `cv.extract`/`cv.extractu`/`cv.insert`** and **T-Head `th.ext`/`th.extu`** encode
`(offset, width)` in **immediate fields of the instruction**: `th.ext rd, rs1, imm1, imm2` =
`reg[rd] := sign_extend(reg[rs1][imm1:imm2])`. Both ship, both are upstream in binutils and LLVM.
Non-RISC-V equivalents: ARM `UBFX`/`SBFX`/`BFI`, x86 `BEXTR`/`PDEP`/`PEXT`, 68000 `BFEXTU`/`BFINS`.

**This family references no third memory object, so it survives the 2026-09-03 ruling**, and C10
is right that dismissing option 1 on *type* grounds dismisses *extent* too. **An earlier revision of
this page priced it in four lines and rejected it. That pricing was wrong on two of its four rows,
in the direction that made the rejection look easy**, so the family is built out as a design — **A2,
§3A** — and the four rows are corrected here:

| | cost |
|---|---|
| **encoding space** | ~~*"`(offset, width)` over 512 bits needs 9 + 6 = 15 bits, so a full-generality form does not fit an existing format."*~~ **WITHDRAWN.** True only of a *free* (offset, width). **Force natural alignment** — every slice of width *w* starts at a multiple of *w*, which §3.6's own placement lemma already guarantees — and the offset collapses to `index << (width_code + 3)`. The descriptor is **`{index:6, width_code:3}` = 9 bits**, which fits an I-type immediate **with 3 bits to spare** (§3A.2). It carries **the same information in the same 9 bits as** design B's F2 map entry (§4.1) — *the same two fields, in the instruction instead of in a fetched table* (field order, code numbering and shift constant differ; §3A.1). |
| **instruction count** | ~~*"an `add` of two packed values is four instructions instead of one… a ~3× dynamic instruction count on the body."*~~ **WITHDRAWN as stated.** That figure assumes *every* operand is packed. **Composed with the Heap Rule, no 64-bit or 32-bit value is ever packed** — those are names, and they decode and read exactly as under design A. Only sub-32-bit values pay, at **+1 instruction per packed read and +1 per packed write**, hoisted to live-range boundaries rather than paid per operand (§3A.4). **A function that fits under design A emits no extent instruction at all**, and both measured functions (§5.4) fit. |
| **the closed decision it reopens** | **STANDS, and it is now the whole of the objection.** cons-C26, tier 1: *"NO BIT-FIELD INSERT/EXTRACT INSTRUCTION CARRYING AN OFFSET AND A WIDTH. Considered and dropped: it duplicates instructions RV64I already has"* — R84, CANON.md:8801, authority #233 *"Let's not overdesign"*; `nmfc_isa.h:95-99` carries the same reasoning in the tree. **And cons-C22, also tier 1**: twelve user-level instructions is a settled count. A2 needs both reopened. **That is Q5, and it is the user's ruling, not this document's.** |
| **what it would buy** | **Speed**, not reach `[corrected - user ruling 2026-09-03 (liveness)]`: any power-of-two width at any aligned offset in **one instruction**, including the byte tier — **13,809 of the 17,361 width-multisets named directly against design A's 2,685 [recomputed, §5.2]**, where A expresses the other 14,676 by packing at ~2-3 ops per access. It is the family that comes **closest** to I2's *"64 1-byte regs, or ANY combination"* with no third referenced object — *closest, not all the way: the one multiset nobody places is the literal 64 × 8, because A2 cannot spare its 32-bit extent scratch at 512-of-512 occupancy (§3A.5, §5.2).* |

**Verdict: NOT rejected — promoted to a design and scored.** The two rows above that carried
numbers were wrong, and correcting them removes both quantitative grounds for the old rejection.
What survives is a **tier-1 user ruling**, which is exactly the kind of objection a proposal must
put back to the user rather than argue past. §3A builds it; §5 scores it at **6.7 against A's 7.3**;
Q5 asks the only question that decides it.

**And it settles the attribution — by dissolving it.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` `final-A-aliasing.md` §13.1
calls the byte-tier loss *"a consequence of the 2026-09-03 ruling, not of this design"*; this page
reattributed it to the name-denotes-slice family. **Both are moot: there is no byte-tier loss.**
Design A reaches the byte tier by packing; A2 reaches it in one instruction rather than ~3. What is
forced within the name-denotes-slice family is **how directly** a sub-name value is reached, and
that is instructions. This proposal chooses that family knowingly, for the reason above.

### 2.4 Five ratified facts every candidate is built on

1. **Type is in the opcode, never in the register name** (fact-C9). Zfinx (Ch. 26 §26.1) deletes
   the `f` registers and runs FP on the `x` registers: `add x5,…` and `fadd.s x5,…` name the same
   architectural register. Removing the namespace costs no typing at all. **What a second
   namespace actually buys is +32 names**, and that is the argument for keeping it — not typing.
2. **The V extension is prior art for the mechanism this machine forbids** (fact-C9 nuance).
   `vtype.SEW` is a **CSR** — a per-context mode register — set by `vsetvli`. cons-C8 rules it out.
   Cite it as the excluded option, never as support.
3. **Zdinx is the ratified answer to "a value wider than one name"** (fact-C16): aligned register
   pairs, even numbers only, odd reserved, lower-numbered register holds the low-order bits, and a
   double-width write to `x0` takes no effect at all. **Scope, because it is load-bearing and the
   rest of this document previously got it wrong: fact-C16 states the pairing rule "for RV32". At
   XLEN = 64 a double fits one register and Zdinx pairs nothing** — so Zdinx is prior art for the
   *shape* of the rule, not machinery `rv64ima_zfinx_zdinx` hands the back end for free (§3.7). **Any option-2 map should be written in
   Zdinx's shape and should say where it departs.** Design A's Heap Rule is Zdinx's rule inverted —
   the *whole* is numbered below its parts — and that inversion is exactly what costs the LLVM back
   end its work (§3.7).
4. **RV64's sign-extension invariant is in direct opposition to bit-packing** (fact-C13). Ch. 5:
   *"all 32-bit values are held in a sign-extended format in 64-bit registers."* Bits 63:32 are not
   a second field; they are redundant copies of bit 31. Design A resolves this by **maintaining the
   invariant at the read port instead of in the register file** (§3.3), which is the single most
   important semantic decision in this document.
5. **Three ratified mechanisms will silently overwrite the upper half of a packed 64-bit name**
   (facts §6.3): `fmv.x.w` sign-extends bits 31:0 over 63:32 (fact-C3); `fmv.w.x` NaN-boxes, writing
   all 1s there (fact-C4); and the `*W` forms canonicalise. *(Under the recommended
   `rv64ima_zfinx_zdinx` spelling the first two are moot — **Zfinx and Zdinx remove the whole `fmv`
   transfer family from the ISA**, fact-C15 — which is one more reason the spelling is the right one.
   The `*W` case remains and is the one that forces the rule.)* **A narrow-write rule is mandatory**, and
   it must be *neither* x86's preserve (fact-C19 — partial-register merge stalls) *nor* AArch64's
   zero (fact-C20 — it would erase a neighbour). It has to be "write exactly your own bits", which
   is W3 in §3.3.

---

## §3 DESIGN A — FIXED ALIASING, IN FULL

**This section carries `final-A-aliasing.md` forward with four substantive amendments**, marked
**[AMENDED]**. Three of them make the design simpler and one makes it safer.

### 3.1 The map on one page

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
 f_n       = x_n       (same bits) ; f8-f15 carry .d, f16-f31 carry .s        [Q2]

 DECODE:  width64 = ~n[4]     zero = (n == 0)     legal = n[4] | n[3] | zero    [CORRECTED]
          offset[8:6] = n[4] ? n[3:1] : n[2:0]     offset[5] = n[4] & n[0]     offset[4:0] = 0

 ADMISSIBLE  <=>  every opcode in RV64IMAFD
                  AND the seven legality rules hold (S 3.4)
                  AND peak( live bits + scratch bits ) <= 512, each value at its OWN width,
                      one spare name free for staging   [user ruling 2026-09-03 (liveness)]
                      -- nothing charged 32; no cap on the number of live values; a value
                         with no name of its width is PACKED and reached by shift-and-mask
                         through a scratch name: ~2-3 extra ops per access
                      (DIRECT NAMEABILITY, an instruction-count rule and NOT admission:
                       a <= 8 sixty-fours, b <= 16 thirty-twos, 64a + 32b <= 512)
                  AND a verified non-overlapping placement exists over the live ranges (S 3.6)

 STATE OUTSIDE THE INSTRUCTION: none.
 MIGRATION: 72 B, unchanged.   POST-MIGRATION FETCH: none.
```

**Verified exhaustively over all 31 encodings.** `n` = 8…15 gives `offset[8:6] = n[2:0]` and
`offset[5] = 0`, so `d0`…`d7` land at 0, 64, …, 448. `n` = 16…31 gives `offset[8:6] = n[3:1]` and
`offset[5] = n[4] & n[0]`, so `w0`…`w15` land at 0, 32, …, 480. Both tilings are **exactly
complete**; `w`*2k* ∪ `w`*2k+1* = `d`*k* for every *k*; **every slice is naturally aligned and none
straddles a 64-bit word.** Read upward, the Heap Rule says what `x1`–`x7` are — `x1` is the whole
512, `x2`/`x3` its 256-bit halves, `x4`–`x7` its 128-bit quarters — and **nothing in `RV64IMAFD` is
wider than 64 bits** (`Q` is outside the subset, cons-C10), so they denote nothing this machine can
compute on. Reserving them is a *consequence of the rule*, not an ad-hoc carve-out.

**Why the anchoring is `x8`/`x16`, and it matters.** `x8`–`x15` = `s0, s1, a0`–`a5`: nothing
ABI-fixed, nothing clobbered by a jump, **every D name allocatable by a stock compiler**. (An
anchoring at `x1` puts `d0`–`d7` over `ra, sp, gp, tp`, which no stock GCC or LLVM will allocate,
yielding four usable names rather than eight.) It also makes decode two OR'd bits rather than two
magnitude comparators, makes `a0`–`a5` 64-bit — the right default for pointers — and makes `ra` and
`sp` illegal names, an I7 tripwire. At no cost: `x8`–`x15` is exactly the set the compressed
encodings reach (Zca, fact-C17), though K.6 excludes RVC so that is a note, not a claim.

### 3.2 Decode cost

```
    width64     = ~n[4]                              // 1 inverter
    offset[8:6] = n[4] ? n[3:1] : n[2:0]             // 3 x 2:1 mux
    offset[5]   = n[4] & n[0]                        // 1 AND
    offset[4:0] = 0                                  // wires
    zero        = ~(n[4]|n[3]|n[2]|n[1]|n[0])        // 1 five-input NOR
    legal       = n[4] | n[3] | zero                 // 1 three-input OR   [CORRECTED]
```

> **[CORRECTED — the published equation made `x0` illegal.]** Earlier revisions printed
> `legal = n[4] | n[3]`, which evaluates to **0 for `n` = 0**. §3.4's `x0`/`f0` exemption is
> *"load-bearing: without the exemption `beqz`, `li`, `mv`, `snez` and `j` would all be illegal, and
> this machine's loops are built from them (M5)"* — so the equation as printed contradicts the rule
> it is supposed to implement, and §9 Step 1 asks for exactly this equation as a `constexpr` decoder.
> **`zero` must feed `legal`** (or the exemption must be checked ahead of the legality test). The
> gate count goes **7 → 8**; nothing else changes, because `zero` was already being computed.

**≈8 primitive gates, one logic level after the select**; as a ROM instead, 31 × 10 bits = **310
bits per tile**, shared by every context and every function on it — **39 B against 64 KiB of
context state at `C` = 1024, 0.06%.** Against the object it replaces: `RegLayout` is
32 × (`uint16` offset + `uint8` width) = **768 bits per resident function**
(`NMFCRegLayout.h:33-42`), consulted at **every** register access (`NMFCTile.cc:461-475`).

### 3.3 Execution semantics — **[AMENDED: W1b is struck]**

> **W1 — OPERAND WIDTH IS PER OPERAND, AND ITS SOURCE IS THE ROLE.**
>
> | role | width | name required |
> |---|---|---|
> | **address / base** of any load, store, atomic, or `jalr` | **always 64** | a `d` name |
> | **data** destination of a **load** | the **opcode's** width | **any name. The loaded value is extended into it by W3** — sign for `lb`/`lh`/`lw`, zero for `lbu`/`lhu`/`lwu`. **[AMENDED — see below]** |
> | **data** source of a **store** | the **opcode's** width | **a name at least as wide as the opcode**: `sd` needs a `d` name; `sw`/`sh`/`sb` take any name and store its low bits. **[AMENDED]** |
> | any **FP** source or destination | the width the mnemonic names (`.s` = 32, `.d` = 64) | exactly that width |
> | the **integer** operand of an FP instruction | the width the mnemonic names (`.w` = 32, `.l`/`.x.d` = 64); a compare/`fclass` **result** is 0/1 and fits any name | as the mnemonic names, except the 0/1 result |
> | **branch** sources | **always 64** | any name |
> | **integer ALU / shift / M** operands and destination | **32 iff the opcode is a `*W` form, else 64 — stock RV64's own rule** | any name |
>
> **W2 — READ PORT.** Each source is read from **exactly its own name's bits**, then adjusted to
> the operand width: an **integer** source narrower than the operand width is **sign-extended**
> (RV64's ratified invariant, Ch. 5, fact-C13); a **floating-point** source is read at exactly its
> operand width with **no extension and no NaN-box check**, because W1 requires the name to be
> exactly that width.
>
> **W3 — WRITE PORT.** A write puts the result into **exactly the destination name's bits and
> never modifies a bit outside them.** Where the execution width is narrower than the destination
> name, the result is sign-extended (zero-extended for `lbu`/`lhu`/`lwu`) to fill it, which is
> ratified RV64 behaviour verbatim. It never NaN-boxes.

> **[AMENDED — the load/store data rule, which as written broke stock integer codegen.]** Earlier
> revisions exempted only `lb`/`lh`/`lbu`/`lhu` from *"a name of that width"*, which made **`lw`/`lwu`
> into a `d` name illegal** under §3.4 rules 2 and 6. That is the ordinary RV64 pattern for loading an
> `int` that is then used as a 64-bit value or an address base — and it directly contradicts §3.4's
> conclusion that *"this is why stock `int` codegen works under register-class assignment alone."*
> **The exemption is now total for loads and one-sided for stores**, and both halves fall straight out
> of W3 rather than being new rules:
>
> - **A load may target any name.** `lw d1, 0(d2)` sign-extends the loaded word across all 64 bits of
>   `d1`, which is **ratified RV64 behaviour verbatim** (Ch. 5); `lw w3, …` writes 32 bits into a
>   32-bit name, where the extension is the identity. `lwu d1, …` is the ratified zero-extending form
>   and is now reachable — under the old rule it was illegal everywhere it was useful. **No neighbour
>   is ever touched, because the destination name's bits are exactly the destination name's bits.**
> - **A store's data source must be at least the opcode's width.** `sd w3, …` stays **illegal**: it
>   would store 32 bits of `w3` and 32 bits of its neighbour `w`*2k+1*, which is the one direction
>   that reads a bit the instruction did not name. `sw d1, …` is legal and stores `d1`'s low half —
>   the truncation the programmer asked for, exactly as §3.4 already treats `add w0, d1, d2`.
>
> **Cost: zero dynamic instructions**, which is what §5.3's `+0` row assumed all along; under the old
> rule every `lw` feeding a 64-bit use would have needed an extra `mv d, w`, and that cost was never
> in any table.

> **[AMENDED — the largest simplification in this document.]** `final-A-aliasing.md` §3 defines
> **W1b**: integer execution width is *"the width of the widest register operand, destination or
> source"*, which makes **XLEN a function of register allocation**. That rule is struck, for three
> independent reasons — expressibility, redundancy, and a correctness class of its own — and **the
> tile's answers become stock RV64's answers on the values W2
> presents — which is a stronger statement than the "the answers do not change" an earlier revision
> made, and unlike that one it is true (see (ii)).**
>
> **(i) It is not expressible in a standard back end.** An LLVM TableGen `RegisterClass` carries a
> single `RegSizeInBits`, and an instruction's operand class is fixed at its definition — so there
> is no selectable form for an R-type drawing `rd` from a 64-bit class and `rs1`/`rs2` from a
> 32-bit one. Expressing it needs a distinct pseudo per operand-class combination (2³ per R-type)
> across the whole integer opcode set, and the natural ISel spelling
> `(add (sext i32:$a) (sext i32:$b))` materialises a 64-bit temp and burns a `d` tile — the exact
> pressure the design cannot afford.
>
> **(ii) It is redundant — but the argument an earlier revision gave for that was false, and the
> false version is corrected here because it is the one correctness claim on this page that was
> presented as a measurement.** That revision said computing at 64 bits on sign-extended 32-bit names
> and truncating into the destination gives *"exactly"* the 32-bit answer for `add`, `slt`, `sltu`,
> `div`, `divu`, `rem`, `remu`, with *"the only divergence"* being `mulh`/`mulhu`/`mulhsu`.
> **Re-enumerated over 200,169 operand pairs per opcode — 200,000 random plus the 169 corner pairs
> over {0, ±1, INT32_MIN, INT32_MAX, −1, −2, 31, 32, 33, 63, 64} — **it is wrong about five
> opcodes**: `divu` and `remu`, which it called exact, and `sll`, `srl` and `sra`, which it dismissed
> on the grounds that *"a compiler emitting a 32-bit shift emits `sllw`/`srlw`/`sraw`"* — a codegen
> argument, not the enumeration it was presented as [recomputed]:**
>
> | | exact | diverges |
> |---|---|---|
> | **result** | `add`, `sub`, `and`, `or`, `xor`, `slt`, `sltu`, `mul`, `div`, `rem` — **0 failures each** | `sll` (96,941), `srl` (142,205), `sra` (93,753), `mulh` (200,041), `mulhu` (200,041), `mulhsu` (100,000/100,000), `divu` (49,857), `remu` (49,845) |
>
> `divu`/`remu` diverge on **exactly** the pairs whose dividend has bit 31 set and whose divisor does
> not — a predicate verified over 400,000 pairs with **zero** mismatches, i.e. a quarter of the random
> space. Counterexample: `x` = `0xC386BBC4`, `y` = `0x1027C4D1` gives `divu`₃₂ = `0xC` against
> `0xD89D1498` truncated, and `remu`₃₂ = `0x1A981F8` against `0x45C8BAC`. **This is precisely why
> RV64M defines `DIVUW`/`REMUW` and RV64I defines `SLLW`/`SRLW`/`SRAW` as separate opcodes.**
>
> **The conclusion survives, on a different and stronger argument, and the difference matters.** The
> question W1b was answering is *"does the tile give the 32-bit answer?"* — and after striking W1b the
> right question is *"does the tile give the answer a stock RV64 core gives on the same architectural
> values?"* **It does, identically, by construction:** W2 presents a `w` name to the ALU
> sign-extended, which is exactly the 64-bit architectural value RV64 holds for a 32-bit quantity
> (Ch. 5, fact-C13), and W1 executes at the width the opcode names — stock RV64's own rule. **So no
> compiler assumption is violated:** a back end that wants 32-bit semantics emits the `*W` form, on the
> tile as on the host, and it emits it for the same reason. The divergence table above is not a list of
> tile bugs; it is a restatement of why RV64 has `*W` opcodes at all.
>
> **The residual risk, and it is closed by a decode rule rather than left to codegen.** A non-`*W`
> `sll`/`srl`/`sra`/`mulh`/`mulhu`/`mulhsu`/`divu`/`remu` with a `w`-named source is a **legal
> encoding whose result is not the 32-bit answer** — harmless when the program meant 64-bit
> arithmetic, a silent wrong answer when it meant 32-bit. No stock back end emits it for a 32-bit type
> (it would be equally wrong on a stock RV64 core), but *"no compiler emits that"* is a codegen
> argument, not a machine guarantee, and cons-C15's discipline is to fail closed. **§3.4 gains rule 7
> for exactly this list.** It costs one comparison against an eleven-entry opcode set — the eight
> register-register forms plus `slli`/`srli`/`srai`, whose 6-bit RV64 shamt reaches 63 — and removes
> the class outright.
>
> **And W1b created a silent-wrong-answer class of its own — SW2 in §3.6.** Under W1b the execution
> width is *"the width of the widest register operand"*, so **XLEN becomes a function of register
> allocation**: recompile with different pressure, the allocator picks a `d` name where it once
> picked a `w`, and the same source computes at 64 bits where it computed at 32 — a different answer,
> from an unchanged program, with nothing in the machine able to see it. That is a second reason to
> strike it, independent of the two above, and **striking W1b removes the class entirely.**
>
> **What striking W1b deletes:** `final-A-aliasing.md` §10.5 in its entirety — *the section that
> document itself labels "the part that is NOT free"* — namely the second 32-bit comparator tap for
> `slt`/`sltu`, the width-dependent shift-amount mask, the second product tap for `mulh*`, and the
> 32-bit sentinel in the divider's overflow path. Also deviations 7 (XLEN per-instruction) and
> legality rules 4 and 5. **Ten ISA deviations become nine, and
> the "not free" execution-unit work becomes zero.**

**Why sign-extend an integer source on read.** Ch. 5's invariant and bit-packing are not in
opposition; they are in different *places*. **W2 maintains the invariant at the read port instead
of in the register file.** The ALU sees exactly the bits a stock RV64 core would present; the
storage costs 32 bits instead of 64; every compiler assumption about sign-extended 32-bit operands
survives.

**W3 is forced, not chosen.** x86 preserves the upper bits on a narrow write; AArch64 zeroes them
(fact-C19, fact-C20). **This map can do neither, because the bits above `w`*2k* are another
architectural value named `w`*2k+1*, not spare room.**

**NaN-boxing is abolished on the tile, in both directions** (fact-C11's Zfinx precedent supplies
the substitute rule). There is no wider container, so nothing to box on write and nothing to check
on read. Without this, a valid positive `f32` in `w3` — zeros above it — fails the ratified box
check and is read as canonical NaN. **The host still boxes and checks**, because it is a stock
RV64GC core with FLEN = 64; §5.5 prices the divergence.

**Width conversions fall out with no new instruction** (cons-C26 stands): narrow→wide signed is
`mv d1, w3`; narrow→wide unsigned is the `slli`/`srli`-by-32 idiom RV64 already emits, **written on
a `d` destination**; **wide→narrow is free**, because the low half of `d`*k* **is** `w`*2k*;
narrow→wide float is `fcvt.d.s`, the correct instruction on a stock core too.

### 3.4 Legality — seven rules a decoder traps

1. **An address/base operand that is not a `d` name.** `lw w3, 0(w5)` illegal.
2. **An operand-class disagreement**, from a per-opcode required-width column total over
   `RV64IMAFD`. `fmv` moves raw bits and converts nothing (fact-C1), so both operands must be the
   same width. **`fadd.d w0, w1, w2` is illegal** — the mnemonic names 64-bit operands and a `w` name
   is 32; **`fcvt.s.w d1, w3` is illegal** — the `.s` result is 32 bits, so `rd` must be a `w` name.
   *(An earlier revision used `fmv.d.x w0, d1` as the worked example. Under the recommended
   `rv64ima_zfinx_zdinx` spelling that instruction **does not exist**: Zdinx removes `fmv.d.x`/`fmv.x.d`
   from the ISA, as Zfinx removes `fmv.w.x`/`fmv.x.w` — fact-C15. The `fmv` family cannot illustrate a
   legality rule in a subset that has deleted it.)* `sc.w`/`sc.d`'s `rd` is a 0/1 flag and may be any
   width, as may `feq`/`flt`/`fle` and `fclass` results.
3. **`x1`–`x7` or `f1`–`f7` as any operand.** This makes `sp` (`x2`) and `ra` (`x1`) illegal, so
   `addi sp, sp, -16` and `sd ra, 8(sp)` are decode-illegal, and it makes **`ret` = `jalr x0,0(x1)`
   illegal**.
4. **`jal` or `jalr` with `rd` ≠ `x0`.** A check on the *link register*, not on the name `x1`. It
   kills every link-forming form while leaving `j`, `jr` and every conditional branch legal, so
   switch tables and computed jumps still work. **Stated honestly:** this catches every call a
   compiler emits; it does not make a call *impossible* — `auipc`/`addi`/`jr` forms a link by hand,
   and banning `auipc` would cost every PC-relative constant. **I7 stays a compiler-discipline
   invariant with a decode tripwire, and is not claimed as enforcement.**
5. **Anything outside the subset** (cons-C10): CSR access, `FENCE`, RVC, `ecall`, anything from `V`.
6. **A `w`-named operand where W1 requires 64 and no rule above caught it** — the catch-all that
   makes the decoder total, so an unlisted encoding fails closed rather than executing at a guessed
   width.
7. **[NEW]** **A `w`-named source on a non-`*W` `sll`, `srl`, `sra`, `slli`, `srli`, `srai`, `mulh`,
   `mulhu`, `mulhsu`, `divu` or `remu`.** These are the eight opcodes (plus the three shift-immediate
   forms) whose 64-bit result truncated into 32 bits is **not** the 32-bit result — measured, §3.3.
   Executing them at 64 on a sign-extended `w` is correct 64-bit arithmetic and wrong 32-bit
   arithmetic, and nothing downstream can tell which the program meant. **A compiler that wants the
   32-bit answer emits `sllw`/`srlw`/`sraw`/`mulw`/`divuw`/`remuw`, which stay legal on any name; a
   compiler that wants the 64-bit answer widens into a `d` name first.** Trapping the middle case is a
   one-comparison check that converts a silent wrong answer into a build-time rejection. *(`mulh*` has
   no `*W` form; the stock RV64 idiom is a widening `mul` on sign-extended operands into a `d` name
   plus `srai 32`, which this rule leaves untouched.)*

**Exempt: `x0` and `f0` denote no bits**, so they have no width to disagree with. Load-bearing:
without the exemption `beqz`, `li`, `mv`, `snez` and `j` would all be illegal, and this machine's
loops are built from them (**M5**).

**Deliberately legal: mixed-width integer ALU operands.** `add d1, w3, w4` executes at 64, reads
both sources sign-extended, writes `d1`. `add w0, d1, d2` also executes at 64 and W3 keeps the low
32 bits — the truncation the programmer asked for by naming a 32-bit destination. **Deliberately
legal: `*W` opcodes on a `w` destination.** Ch. 5 defines `*W` as *"a 32-bit operation whose result
is canonicalised into a 64-bit register"*; when the register is 32 bits the canonicalisation is the
identity, so `addw w0,w1,w2` computes exactly what `add w0,w1,w2` computes. **This is why stock
`int` codegen works under register-class assignment alone.**

### 3.5 The scoreboard, and the overlapping-name dependence check

**On the canon core, H.4 is decisive.** User #239 (CANON.md:5323-5324): *"I would opt for 1
outstanding miss. Likely we need a D-buffer, but it just stores one slot, the data that will be
used when it wakes."* CANON adds: *"the context is always asleep when its load returns."*

> **The scoreboard is not a bit vector. It is ONE pending destination and a one-slot data buffer.**
> The context is `BLOCKED` while the load is in flight and issues nothing, so there is no readiness
> to test on any other name, no overlap to reduce over, and no growth.

This structure **already exists**: `dbufReg` and `dbufValue` at `NMFCTile.h:85-86`, written at
`NMFCTile.cc:827-828`, replayed at `:1504`. **What the design adds: three bits** — the fill's
width/extension class, which a stock in-order core already carries. **Per-context state added by
the map itself: zero. Migration payload: 72 B, unchanged.**

**On the ChampSim core model** (whose contexts keep issuing past an outstanding load — H.4 is
explicit that DESIGN §7's `scoreboard[≤8]` belongs to that model): **sixteen ready bits, one per
32-bit tile.** A read of a `d` name ORs its two. This is **exact**: no name stalls on a load it does
not depend on. Cost: 16 × 1024 = 2 KiB per tile = **3.1% of 64 KiB of context state**, of which the
delta against an eight-bit form is **+1 KiB = 1.5%**, plus **one byte of migration *envelope***.
**The 72-byte payload is unchanged in every configuration** — I11's 72 B is *"64 bytes of register
file plus an 8-byte program counter"*, DESIGN §25.7 **D:2439-2441**.

**The eight-bit lane-granular alternative is rejected**, and not because eight is inconvenient: a
load into `w0` would stall a read of `w1`, so the false dependency falls **exactly between the two
halves of a lane** — the case bit-packing exists to create. Choosing lane granularity because
`scoreboard[≤8]` says eight would be using the 8-register artefact as a design constraint on the
one structure where a register name is visible. That is the reversion I2 forbids.

**The dependence check, for any non-barrel implementation.** On the canon core there is nothing to
check — CANON.md:474-478, verbatim: *"Because at most one instruction per context is ever in
flight, no two instructions in the pipe can be dependent — so there is no forwarding, no
interlocking, no hazard detection, no ROB, no rename, no load/store queue, and no speculative
execution."* Elsewhere, dependence becomes bit-range overlap and factors into five bits per operand:

```
    lane[2:0] = n[4] ? n[3:1] : n[2:0]          // FREE -- it IS offset[8:6]
    mask[1:0] = n[4] ? {n[0], ~n[0]} : 2'b11    // 1 inverter + 2 mux
    overlap(a,b) = (lane_a == lane_b) && |(mask_a & mask_b)
```

**≈7 gates against ≈6 for the 5-bit equality it replaces, same logic levels.**

> **[AMENDED — a contradiction in the source document, resolved.]** `final-A-aliasing.md` §10.3
> lists *"no partial-register hazard"* among what the write port avoids, while §5.3 requires *"a
> byte-masked merge: eight 2:1 byte muxes on a 64-bit forward."* Both cannot be true of one machine.
> The **write port** genuinely has no read-modify-write — the minimum name is 32 bits, 32-bit
> aligned, so a neighbour's bytes are simply not written. The **bypass path** is a different
> structure, and fact-C19 names exactly this as the x86 failure: *"Disjoint slices avoid it;
> overlapping views do not."* Design A has overlapping views by construction.
>
> **Corrected statement, and now priced rather than waved at.** *Write port: +0, and that half of
> §10.3 is right.* *Bypass network: a real cost, on a bypassed core only, and here is its size.*
> The merge granularity is **not** the byte — it is the **minimum name width, 32 bits**, because no
> value smaller than a `w` tile exists to forward. So §5.3's *"eight 2:1 byte muxes"* over-states it
> by 4×; the correct structure is **two independent 32-bit selects per 64-bit forwarded source**
> instead of one 64-bit select. Three costs follow, and only the third is structural:
>
> | | delta against a name-equality bypass |
> |---|---|
> | mux bits | **+0.** Two 32-bit `(F+1):1` selects are the same 64 bits of mux as one 64-bit select |
> | select logic | **×2 per source** — two priority encoders over the forwarding depth instead of one — plus §3.5's lane+mask comparator (**≈7 gates against ≈6**, same logic levels). At `W` = 4 pipes, 3 source slots and an assumed forwarding depth `F` = 2, that is **24 overlap comparators ≈ 170 gates and 12 extra 32-bit priority selects** `[ESTIMATE — the record specifies no pipeline for a non-barrel implementation, so `F` is assumed, not measured]` |
> | **the structural cost** | **one 64-bit read can take its two halves from two different producers.** A name-equality bypass never has to compose a result from more than one source; this one does. That composition **is** x86's partial-register merge (fact-C19), and it is the thing that cost Intel a documented class of stalls — inherited here in its mild form, because the merge is 32-bit-aligned and never sub-word |
>
> **On the canon barrel core all three are zero** — CANON.md:474-478, at most one instruction per
> context in flight, so there is no bypass network at all. **It is not in the §5.3 performance
> table because that table describes the canon core**, and it is written down here so that a later
> non-barrel implementation does not discover it.

### 3.6 Admission — three tests, one of which is a heuristic

> **[CORRECTED - user ruling 2026-09-03 (liveness)]** **A function is admissible iff (1) every
> opcode is in `RV64IMAFD` and its body satisfies §3.4 — no reserved name, no stack; (2) peak
> simultaneous liveness, **each value at its own width**, **plus the scratch bits its packing
> needs**, is **≤ 512 bits**, with at least one spare name free for staging; and (3) a verified
> non-overlapping placement exists over its live ranges, where a value with no name of its width is
> placed **packed** inside a wider name and reached by shift-and-mask (~2-3 ops per access).**
>
> ~~(2) `64a + 32b ≤ 512` with *a* ≤ 8 and *b* ≤ 16, where *b* counts values of 32 bits **or fewer,
> each charged 32**~~ — **STRUCK.** That is a **direct-nameability** rule: it bounds instruction
> count, never admission. **Design A's admission is identical to A2's** (§3A.5) except that A stages
> a packed access through shift-and-mask instead of an extent instruction; the scratch reservation
> is the same obligation in both.

**Test 2 is K.6's test with one change.** It is on **bits**, in **one pool**, and it is peak
*simultaneous liveness*, not a count of registers touched — so Part P R30's error is not
re-introduced, and at 64 and 32 bits the name count *is* the bit count because both tilings are
complete. cons-C19 survives (a value never read is never live); cons-C15 survives (rejection is
fatal). **The change is nothing** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` — ~~the charge-32 rule~~ is struck, so test 2 is K.6
verbatim, with one addition: it must also charge the **scratch bits** a packing stages through
(§3A.5's reservation applies to A as well, since A stages the same accesses in shift-and-mask).

**Test 3 exists because the sum alone is not sufficient, and that is proved rather than asserted.**

*The single-instant lemma.* Let values have widths *w*₁ ≥ … ≥ *w*ₙ, each in {64, 32}, with
Σ*w*ᵢ ≤ 512. Place them in that order, each at the lowest free bit offset. Every value lands on a
*w*ᵢ-aligned offset and no two overlap. *Proof:* before placing value *i* the occupied region is
the prefix [0, Σ_{j<i} *w*ⱼ); every earlier width is a power of two ≥ *w*ᵢ, hence a multiple of it,
so the next free offset is already *w*ᵢ-aligned. No gap is ever created. ∎

*The lemma is about one instant; allocation is over time.* Verified counterexample: place eight
32-bit values first-fit into `w0`–`w7`; let those in `w0`, `w2`, `w4`, `w6` survive and the rest
die; five 64-bit values are then born. Peak liveness is 4×32 + 5×64 = **448 ≤ 512**, so the sum
admits — but `w0` blocks `d0`, `w2` blocks `d1`, `w4` blocks `d2`, `w6` blocks `d3`, leaving **four
free `d` tiles for five values.** The function does not fit.

**The allocator, stated as what it is: first-fit-decreasing by width at each birth, with a
disjointness check over the whole placement, and relocation moves permitted. A HEURISTIC, not an
exact allocator.** In the counterexample, `mv w1, w2` and `mv w5, w6` compact the survivors into
`d0` and `d2`, freeing six `d` tiles for five values.

> **[AMENDED — the relocation claim needs a precondition.]** `final-A-aliasing.md` §7.3 says
> relocation *"repairs every gap the geometry can open, at one instruction per move and no
> scratch."* **That fails at exactly 512-of-512 occupancy**, where no free tile exists to move into
> and compaction needs a swap (three moves and a scratch name, or a rotate). **Correct statement:
> relocation repairs every gap at one instruction per move provided the live sum is strictly below
> 512.** At exactly 512 the placement must be right the first time.

**The general problem is mixed-width allocation with alignment over an interval graph — the classic
register-pairing problem. This document does not reproduce a hardness proof and does not claim
one.** What is proved is the only thing the design needs: the sum is not sufficient, so a placement
must be verified. Empirically, a stress test over ~400,000 random interval instances passing the
closed form found a placement by exhaustive backtracking every time at widths {64, 32}; that is
**cited as evidence, not proof, and is not independently reproduced here**. **When FFD fails, the
tool must not admit.** It may retry with backtracking, or reject. It may not guess.

### 3.6a The two silent-wrong-answer classes, named

**These are not traps. Nothing fires. They are wrong answers that no check in the machine can
see**, and the document calls them by that name from here on so that no reader mistakes them for
something the decoder catches.

> **SW1 — OVERLAPPING-NAME OVER-LIVENESS.** `d0` and `w0`/`w1` are the same bits. If an allocator
> makes a value live in `d0` and another live in `w1`, they clobber. **No decode check can see it**
> — a total map makes every name always defined, so `RegLayout::illegal()`'s run-time trap
> (`NMFCTile.cc:464`, `:472`) has nothing to fire on. cons-C14 required the check be **re-homed**
> rather than deleted; it re-homes onto admission, as a disjointness check on the verified
> placement (§3.6 test 3). **That is a build-time obligation with no run-time counterpart.**
>
> *Status:* **live under A, A2 and B2. Live under B1 too** — a bit-exhausted allocator that
> coalesces two simultaneously-live values onto one name emits a perfectly legal, non-overlapping
> map, and the tile accepts it (§4.4). **No candidate in this document catches SW1.**
> *Concrete instance often cited:* **`d0`** — under the stock ABI mapping `s0` = `x8` = `d0` reads as
> scratch to every allocator, and is **fully occupied whenever a seventh or eighth integer argument
> is passed** in `a6`/`a7` = `w0`/`w1` (§6.4).
>
> > **[AMENDED — that instance is not reachable on either toolchain path §3.7 specifies, and §5.1
> > says the opposite of §3.6a. Both are resolved here.]** §5.1's allocator row states that because
> > the 8 `D` and 16 `W` names *"physically overlap"*, a stock allocator's aliasing machinery (LLVM
> > `RegUnit`s, GCC IRA conflict sets) surfaces over-liveness *"as an ordinary allocation failure"*;
> > §3.6a states SW1 is undetectable and the placement verifier is the only defence. **They are both
> > right about different things, and the difference is who did the allocation.**
> > **(a)** On the **day-one `-ffixed` path** (§3.7) `x16`–`x31` are fixed out, so `a6`/`a7` cannot
> > hold anything and the named instance cannot arise at all.
> > **(b)** On the **`W`-tier path**, `W` is declared as `SubRegIndices` of `D` (§3.7), so LLVM will
> > not co-allocate `x8` with `x16`/`x17` and the named instance cannot arise either.
> > **(c)** What remains, and it is why the verifier is still required:** hand-written bodies; the
> > **admission tool's own FFD placement** (§3.6), which is not a stock allocator and has no
> > `RegUnit`s; any back end that does not declare the sub-register relation; and any path that
> > `-ffixed`es names in one place and allocates them in another. **SW1 is a real class with no
> > reachable example on either specified toolchain path** — which is a materially weaker statement
> > than *"design A's flagship silent-wrong-answer class"*, and the honest one.
>
> *Only defence where it is reachable:* the placement verifier in the admission tool (§6.2, ~40
> lines, **owner undecided — §9.1**). It is the one place design A is less safe than the mechanism
> it replaces.

> **SW2 — XLEN AS A FUNCTION OF REGISTER ALLOCATION.** Under the struck rule W1b, integer execution
> width was *"the width of the widest register operand"*, so a recompilation that changed register
> pressure changed the arithmetic width, and the same source produced a different answer with
> nothing in the machine able to see it.
>
> *Status:* **removed.** W1b is struck (§3.3), and with it this class. It is recorded because the
> first two reasons given for striking W1b were expressibility and redundancy; **this third one is
> a correctness reason, and it is the strongest of the three.** Any future proposal to reinstate a
> width-from-allocation rule reinstates SW2 with it.

**Neither class is priced in §5.3's performance table, because neither costs a cycle. They cost
correctness, and §5.5 is where they are weighed.**

### 3.7 The toolchain — **[AMENDED: the day-one path is better than stated]**

> **[AMENDED.]** `final-A-aliasing.md` §10.7 says `-march=rv64ima` is *"the only reliable
> spelling"*, because `-ffixed-x`*n* does not reserve `f`*n* while `f`*n* ≡ `x`*n*, so a stock
> compiler allocating an `f` register would silently corrupt a reserved tile. **That is true of
> `F`/`D` and false of the thing this design actually specifies.** `rv64ima_zfinx_zdinx` **is**
> Q2(a): Zfinx deletes the `f` registers and runs FP on the `x` registers with `f`*n* ≡ `x`*n*
> (Ch. 26 §26.1, ratified). Both are supported by GCC and LLVM. **Under it there is only one
> namespace, so `-ffixed-x8` reserves the register for floating point too** — which closes the
> corruption hole outright and gives the day-one path **floating point instead of integer only**.
> Zfinx also supplies the ratified narrow-value rules this design needs (fact-C11: ignore-on-read,
> sign-extend-on-write). **`-march=rv64ima_zfinx_zdinx` is the correct day-one spelling and it should
> replace `rv64ima` everywhere.**
>
> **[CORRECTED — "Zdinx adds double-width aligned pairs" is false at XLEN = 64.]** fact-C16 scopes
> the pairing rule verbatim to RV32: *"**For RV32**, double-precision operands in Zdinx are held in
> aligned `x`-register pairs. In other words, register numbers must be even."* **At XLEN = 64 a
> double occupies one `x` register and Zdinx introduces no pairing at all**, which makes
> `rv64ima_zfinx_zdinx` a *simpler* target than the sentence implied, not a more complex one.
>
> **[AND IT AMENDS O4's SPELLING, WHICH THIS PAGE PREVIOUSLY DENIED — this is carried by Q2.]**
> facts §6.1 quotes the ratified spec: *"software that assumes the presence of the F extension is
> incompatible with software that assumes the presence of the Zfinx extension, and vice versa"*, and
> concludes that *"an implementation that aliases `f<n>` onto `x<n>` is not implementing F/D"*. This
> document sets `f`*n* = `x`*n* (§3.1), abolishes NaN-boxing on the Zfinx precedent (§3.3), and makes
> `rv64ima_zfinx_zdinx` the day-one spelling — **so `RV64IMAFD` is not the subset being built.**
> **O4's substance stands**: floating point is in, and every operation `F`/`D` provides is still
> provided. **O4's spelling does not**: it becomes **`RV64IMA_Zfinx_Zdinx`**. That is a tier-1
> supersession; it is marked in §6.3's ledger and it rides with **Q2(a)** rather than being a separate
> ruling. §6.8's *"ruling O4's opcode list stands unamended"* and §3A.8's *"`RV64IMAFD` (O4) |
> unamended"* were wrong and are corrected there.

| item | estimate |
|---|---|
| **day-one path, no back-end work** | `-march=rv64ima_zfinx_zdinx`, plus `-ffixed-x1 -ffixed-x5 -ffixed-x6 -ffixed-x7 -ffixed-x16 … -ffixed-x31`, `-fomit-frame-pointer`, `-fcall-used-x8 -fcall-used-x9`. **Yields eight 64-bit names = 512 bits, integer and floating point.** Clang has no `-fcall-used`, so its honest floor is **six names = 384 bits** — enough for `nmfc_expand`, not for `nmfc_bu`. (`x2`/`x3`/`x4` are already fixed in the RISC-V back end, so omitting them is correct, not an oversight. With `a6`/`a7` fixed out, the day-one path passes at most **six** integer arguments.) |
| **the `W` tier** | a new register-file description in TableGen: two allocatable classes, `D` (8) and `W` (16), `W` declared as sub-register indices of `D`. **The right analogy is ARM's SPR/DPR relation** — 32-bit singles paired even/odd into 64-bit doubles, `ssub_0`/`ssub_1` — which LLVM has allocated in production for fifteen years with sub-register liveness on. |
| **what is NOT reusable** | **Zfinx's `GPRF32`/`GPRF64` are same-numbered aliases** — one file at two widths, a naming trick, not a sub-register relation. **And Zdinx's `GPRPair` is not available to borrow from either: at XLEN = 64 Zdinx instantiates no pairing at all** (fact-C16 scopes it to RV32), so this target has no existing even/odd super-register machinery to lean on. Here the whole (`x8`) is numbered **below** its parts (`x16`, `x17`) and the pairing stride is 2 across a 16-name class. Expressible — the working analogy is ARM's SPR/DPR, above — but **a new register file, new `SubRegIndices` and a new encoding map, built rather than inherited.** |
| **instruction selection** | **unchanged for `int`** — `*W` stays legal on `w` names (§3.4). With W1b struck, there is no mixed-width form to select for. |
| **peepholes** | `sext.w` on a `w` name is a redundant move; the `zext.w` idiom must land on a `d` destination. **Identical to stock RV64's obligation; only "into a `d` destination" is new.** |
| **estimate** | a register-file `.td` plus a peephole pass. **The estimate "weeks, not months" is stated in the source document with nothing behind it; treat it as an engineer's guess, not a measurement.** |
| **the gap nobody has costed** | Under `-ffixed`, when the 8-name `D` class is exhausted **LLVM spills to the stack** — which under I7 and cons-C15 is fatal, and which arrives as ordinary correct-looking codegen with no diagnostic. **A post-RA pass that turns any surviving stack access into a compile error is required and is not in any estimate.** |

### 3.8 The admission tool — **[AMENDED: the stated failure mode is wrong, and unsafe]**

> **[AMENDED.]** `final-A-aliasing.md` §7.2 and Q8 say: *"On a RISC-V trace every value falls to
> the `else` branch and is charged 64"*, and resolve it as *"charge every value 64 bits and say
> so"* — a conservative gate. **I executed the cited lambda (`annotate.cc:461-470`) against RISC-V
> spellings and that is not what it does:**
>
> | spelling | branch taken | charged |
> |---|---|---|
> | `a0`, `a7`, `t1`, `s2`, `ra`, `sp`, `fp` | `n.size() == 2` | **16 bits** |
> | `x10` … `x31` | `n.size() > 2 && n[0]=='x'` | **128 bits** |
> | `zero` | `n.size() > 2 && n[0]=='z'` | **512 bits** |
> | `s10`, `a10`-style 3-char names | falls through | 64 bits |
>
> **The commonest register spelling on RISC-V is charged 16 bits — permissive by 4×.** Under
> cons-C15 an over-permissive admission test **silently admits functions that cannot run**, with no
> spill to fall back on. This is worse than the document states, in the unsafe direction, and the
> stated interim behaviour ("charge 64") **does not exist in the tool** and requires a code change
> the ~350-line budget does not contain.

**Resolution.** The width source on RV64 is **the defining opcode** — `lw` vs `ld`, `addw` vs
`add`, `flw` vs `fld` — a ~150-entry table, available in any RISC-V trace, and it is exactly the
signal that replaces the x86 name suffix. Until it lands, **charge 64 and make that true in the
code**. **Consequence: §5.4's two measured decompositions are not currently reproducible from the
tool and must be re-measured.**

---

## §3A DESIGN A2 — FIXED ALIASING PLUS EXTENT INSTRUCTIONS

**New in this revision. Neither source document costs it.** `register-map-facts.md` C10 and C22
name the family; §2.3 of this document priced it in four lines and rejected it, and **two of those
four lines carried numbers that were wrong in the direction that made the rejection look easy.**
This section is the design those corrections oblige.

**In one sentence:** *design A, unchanged, plus two instructions whose immediate field carries the
same nine bits design B would have fetched from a table.*

### 3A.1 The design on one page

```
 EVERYTHING IN S3 IS UNCHANGED.  The Heap Rule, the seven legality rules, W1/W2/W3, the
 admission test, the decode gates, the read port, the write port, the toolchain.  A
 function that fits under design A compiles to design-A code and executes design-A
 instructions.  A2 adds a way to reach BELOW 32 bits, and nothing else.

 THE EXTENT DESCRIPTOR                         9 bits, carried in the instruction
     { index:6 , wcode:3 }
     wcode  0 = 8   1 = 16   2 = 32   3 = 64      4-7 RESERVED  (see 3A.6)
     offset = index << ( wcode + 3 )              naturally aligned, FORCED by the encoding
     legal  <=>  index < ( 512 >> ( wcode + 3 ) ) <=>  index < 64/32/16/8 for wcode 0/1/2/3

 THE TWO INSTRUCTIONS                          custom-1, I-type, TILE-ONLY
     nx.xtr   rd,  {index,wcode}, sx      rd  <- ext_sx( ctx[ offset +: width ] )
     nx.ins   rs1, {index,wcode}          ctx[ offset +: width ] <- rs1[ width-1 : 0 ]

 THE COMPOSITION -- this is the whole design, and it is checkable in two lines:
     { index = k, wcode = 3 }  ==  bits [64k, 64k+64)  ==  d_k  ==  x_(8+k)      (S3.1)
     { index = m, wcode = 2 }  ==  bits [32m, 32m+32)  ==  w_m  ==  x_(16+m)     (S3.1)
 So the extent descriptor is not a second naming mechanism beside the Heap Rule.  It IS
 the Heap Rule, written out, continued below 32 bits where the 5-bit register field runs
 out of names (S2.2: 63 nameable, 64 needed).  wcode 2 and 3 are redundant with the
 register field and exist only so the encoding is total.

 STATE OUTSIDE THE INSTRUCTION: none.        MIGRATION: 72 B, unchanged.
 POST-MIGRATION FETCH: none.                 PER-CONTEXT STATE ADDED: zero bits.
```

> **The descriptor carries design B's F2 map entry, moved — the same information in the same nine
> bits, not the same bit pattern.** §4.1 specifies F2 as `entry = { width_code:3, index:6 }` with
> `bit offset = index << (width_code + 2)` and codes `0 = UNDEFINED, 1 = 8 … 4 = 64`; A2's is
> `{ index:6, wcode:3 }` with `offset = index << (wcode + 3)` and codes `0 = 8 … 3 = 64`. **Reversed
> field order, code numbering shifted by one, a different shift constant, and no `UNDEFINED` code**
> (below). *An earlier revision called it "byte-for-byte" and §2.3 called it "exactly" design B's
> entry; neither is true, and the defensible claim — the same two fields, the same widths, the same
> shift function, in the same nine bits — is the one that carries the argument anyway.* **The
> substantive difference is that B fetches its entry from a 40–76 B image and caches it in a 576 B
> on-core file, and A2 reads its own out of the instruction word already in the fetch buffer.** That is the sharpest available answer to
> the 2026-09-03 ruling: the ruling's own list of objects is *"the map, instruction, and potentially
> data"*, and **A2 puts the map inside the instruction, which is the one object on that list the
> machine was always going to reference.**
>
> **One thing A2 does not inherit from F2: code `0 = UNDEFINED`.** An instruction always carries a
> defined descriptor, so there is no undefined name to trap on. **A2 therefore does not restore the
> half of cons-C14 that B1 restores** (§4.4), and inherits SW1 unchanged (§3.6a). That is stated
> here rather than left to be discovered in Q4.

### 3A.2 The encoding, and why `custom-1` is forced

```
  31    21 20   15 14   12 11    7 6           0
  |  imm[11:0]   |  rs1  | funct3 |   rd   |   0101011   |      RISC-V custom-1 = 0x2b
                                                              (stock RV64: illegal instruction)

  imm[11:0] = { sx:1 , rsv:2 , index:6 , wcode:3 }
              imm[11] = sx      1 = sign-extend the extracted field, 0 = zero-extend
                                MUST be 0 on nx.ins (reserved; traps if 1)
              imm[10:9] = rsv   MUST be 0 (two spare bits, deliberately unspent)
              imm[8:3]  = index
              imm[2:0]  = wcode

  funct3 = the RoCC operand flags, which happen to separate the two forms exactly:
              nx.xtr   funct3 = NMFC_XD  = 0x1    writes rd, reads nothing
              nx.ins   funct3 = NMFC_XS1 = 0x2    reads rs1, writes nothing
           (nmfc_isa.h:38-40.  The coincidence is worth keeping even though A2 needs no
            RoCC conformance -- see below -- because it costs nothing to be conformant.)
```

**The immediate is a bit field, not a number.** I-type immediates are sign-extended by the base ISA;
this one never is, because nothing arithmetic is done with it. That is a departure and it is listed
in §3A.8.

**Why the 9-bit descriptor and not the 12-bit `(offset:9, width:3)` form.** The literal form the
request names — nine bits of absolute offset plus a three-bit width code — also fits, **exactly**,
filling the immediate with nothing left over. It is the wrong choice for three reasons and the
alignment discipline costs nothing:

| | `(offset:9, wcode:3)` = 12 bits | `(index:6, wcode:3)` = 9 bits **[recommended]** |
|---|---|---|
| spare immediate bits | **0** — no room for `sx`, so signed and unsigned extract need two `funct3` values, which RoCC conformance then cannot supply | **3** — `sx` plus two genuinely spare |
| alignment | **unaligned extents are expressible**, so a field can straddle a 64-bit word | **impossible to express an unaligned extent.** The encoding cannot say it |
| the straddle path | **resurrects it.** `Context512::read`'s two-word splice (`NMFCRegLayout.h:95-98`) and `write`'s `if( b0 + f.width > 64 )` (`:110`) stay live, and §9 Step 1's deletion of both is withdrawn. §6.1's *"every named value is reachable in exactly one `CXR`"* fails, and the host pays two `CXR`s and a splice for a straddling field | **stays dead.** Every extent lies inside one 64-bit word, so the deletion in §9 Step 1 still holds and §6.1 is unaffected |
| what it buys | nothing design A2 wants. §3.6's placement lemma already places every value at a naturally-aligned offset, so **no admissible function has an unaligned value to name** | — |

**So: 9 bits, aligned, 3 spare.** The alignment is not a restriction the design accepts; it is a
restriction the design's own allocator already obeys, written into the encoding so that violating it
is unrepresentable rather than merely wrong.

**Why `custom-0` cannot host these instructions, and it is a forced conclusion, not a preference.**

1. **`funct7` has no room.** cons-C23: the only free encoding space in `custom-0` is `funct7` groups
   `0x6` and `0x7` (`nmfc_isa.h:61`, *"reserved — §23.5 keeps encoding space for KILL and mailboxes"*),
   and a group carries **4 variant bits = 16 encodings** (`NMFC_VAR_MASK 0xf`). The descriptor needs
   **512**. It does not fit, and consuming both groups would still not fit.
2. **The descriptor cannot go in a register field.** cons-C25, tier 3: *"EVERY OPERAND IS A VALUE IN
   A GENERAL REGISTER; A CONTEXT REGISTER IS NAMED BY A NUMBER IN A GPR."* Putting the descriptor in
   `rs2`'s five bits reads it as a field rather than a value and breaks the rule the whole `custom-0`
   encoding is built on.
3. **`funct3` is spoken for.** R85, CANON.md:8801: *"`funct3` as the ISA group selector — a RoCC host
   reads `funct3` as the operand flags, so it would decode `FORK` as 'uses no registers'."*

**`custom-1` = `0b0101011` = `0x2b` is free, and the tree already says so for exactly this purpose.**
`nmfc_isa.h:18-20`: *"custom-1 (`0b0101011`) is left free for a second reservation; custom-2/3 are
avoided because they are claimed by RV128."* CANON.md:6719 records it as
`[SST-only — implementation choice]`. **A2 is that second reservation.** Consequences, stated
precisely:

- **cons-C23 is untouched.** A2 consumes **zero** of the reserved `funct7` groups; `KILL` (R77) and
  mailboxes (R78) keep their space intact.
- **cons-C22 is not.** Twelve user-level instructions becomes **fourteen** (thirteen under A2-r,
  §3A.7). C22 is **tier 1** — *"Growing the set is a change to a settled count and must be argued as
  such"* — and this is the argument, put as Q5.
- **The only spare opcode is spent.** After A2 there is no `custom-*` left that RV128 does not claim.
- **A2's instructions are TILE-ONLY, so no RoCC obligation attaches.** They operate on the 512-bit
  context, which the host does not have; the host reaches it through `CXW`/`CXR` (§6.1) and never
  issues an extent. **A useful consequence: `0x2b` is an illegal instruction on a stock RV64 core and
  is not routed to Vanadis's `rocc` slot (which takes `0x0b`, CANON.md:7750), so an offloaded body
  executed on the host faults loudly instead of computing something.** *This is a property to verify
  before building, not an assumption to rely on: it must be checked that no host-side fallback path
  ever executes a tile body — `RESUME` is privileged and re-enters on a tile, but nothing in the
  record states the invariant explicitly.*

### 3A.3 How it composes with the Heap Rule — the common case pays nothing, and "nothing" is literal

**A2 changes no path that design A uses.** Point by point, because "purely additive" is a claim that
has to be checkable:

| design-A structure | changed by A2? |
|---|---|
| the 7-gate name decode (§3.2) | **no.** Extent instructions are a different opcode; the register-name decoder never sees one |
| the read port (§3.3 W2) | **no** for named operands. The extent unit is a **second read port** on the context array, not a widening of the first (§3A.6) |
| the write port (§3.3 W3) | **rule unchanged in substance, subject widened.** W3 says *"write exactly the destination NAME's bits and never a bit outside them"*; under A2 it reads *"write exactly the destination EXTENT's bits"*, and **a name is an extent** — `d_k` = `{k, 3}`, `w_m` = `{m, 2}`. The write-enable granularity goes from 32 bits to 8 (§3A.6) |
| the seven legality rules (§3.4) | **no.** A2 adds three of its own (§3A.6), which apply only to extent instructions |
| the admission test (§3.6) | **one clause added** (§3A.5), which is inert when no value is narrower than 32 bits |
| the scoreboard / D-buffer (§3.5) | **no.** An extent's destination is a name, so the pending-destination register and the 16 ready bits are unchanged |
| `CXW`/`CXR`, lanes, migration, `CONT`, the FTU, the twelve instructions' semantics (§6) | **no** |
| migration payload | **72 B.** Nothing is added to payload or envelope — **A2 is the only variant other than A that adds nothing even to the envelope** (B2 adds two class bits) |

**And the instruction stream pays nothing either, for anything design A can express.** A 64-bit
value is a `d` name and a 32-bit value is a `w` name; **neither is ever packed**, because packing a
value the register field can already name buys nothing and costs an instruction. So:

> **A2's entire cost, in hardware and in instructions, is borne by exactly the functions design A
> rejects outright.** Under cons-C15 a rejected function *cannot run at all*; under A2 it runs, at
> +1 instruction per packed read and +1 per packed write.

**Evidence, such as the record has.** `nmfc_bu` (8 values, 480 bits) and `nmfc_expand` (8 values,
384 bits) are the two measured functions (cons-C21, §5.4). **Neither decomposition shows a
sub-32-bit value, so on the record as it stands both emit zero extent instructions and A2 is
bit-for-bit design A on both.** That is also the honest limit of the evidence, and the limit is
tighter than it looks: **the tool that produced those decompositions cannot see widths at all**
(§3.8 — `annotate.cc:461-470` charges 16 bits for `a0`), so what the record supports is *nothing has
ever demanded what A2 buys*, not *nothing needs it*.

### 3A.4 Instruction-count overhead, against both alternatives

**The comparison that matters is not A2-versus-A** — where A2 costs instructions and A costs 32 bits
per narrow value — **but A2 versus the thing R84 says already suffices.** cons-C26's stated reason is
*"it duplicates instructions RV64I already has"*, and cons-C27 says *"any packing inside them is
reached with the shifts and masks RV64I already has."* **That is true of extraction and false of
insertion**, and the asymmetry is the one quantitative fact that bears on Q5.

**Extract *w* bits at offset *o* from a 64-bit name, in base RV64I** — two instructions, no scratch:

```
    slli  t, d_k, 64-o-w          # signed:   ...then srai
    srai  t, t,   64-w            # unsigned: ...then srli
```

**Insert the low *w* bits of a value into offset *o* of a 64-bit name, in base RV64I** — there is no
bit-field insert in the base ISA, and `andi`'s immediate is **12 bits, sign-extended** (Ch. 2), so a
mask for a field anywhere above bit 11 cannot be an immediate. Without a materialised mask:

```
    srli  s, d_k, o+w             # s   = the bits ABOVE the field, right-aligned
    slli  s, s,   o+w             #       ...back in place
    slli  d_k, d_k, 64-o          # d_k = the bits BELOW the field
    srli  d_k, d_k, 64-o          #       ...back in place
    or    d_k, d_k, s             #       field now zeroed, neighbours intact
    slli  t, t, 64-w              # t   = the field, junk above w shifted out
    srli  t, t, 64-w-o            #       ...landing at offset o in one shift
    or    d_k, d_k, t
```

**Eight instructions and one 64-bit scratch name (`s`).** Seven when the source is known clean above
bit *w*−1 (the two `t` shifts collapse to one `slli`); **five at either extreme** — a field at bit 0
needs no low part, a field at the top of the word needs no high part — and those extremes are
exactly the cases design A already names, so they are not the interesting ones. Shift amounts are
taken mod 64 on RV64 (Ch. 2), which is why the extremes must be special-cased rather than falling
out of the general form. The alternative — holding the mask in a register — costs four instructions
**and a whole 64-bit tile for the constant**, which under a 512-bit budget with no stack (I7) is the
more expensive of the two.

`[CORRECTED - user ruling 2026-09-03 (liveness)]` The third column below is **design A's own
packed path** — the middle column *is* what design A does when a value has no name of its width:
shift-and-mask through a scratch name, ~2-3 ops per access. Design A does **not** have a fourth
option of "name it and charge 32"; the "0 instructions" column is only available to a value that
already fits a `d` or `w` name, and its cost is one whole name, never a 32-bit charge on a narrow
value.

| access to a packed narrow value | **A2** | **base RV64I on a packed tile — this is DESIGN A's path** (R84's premise) | design A, when the value **fits a name** (one whole name spent) |
|---|---|---|---|
| read | **1** | **2** | 0 instructions, one whole name |
| write | **1** | **5–8, plus one 64-bit scratch name** | 0 instructions, one whole name |
| `x = x + 1` on a packed byte | **3** (`xtr`, `addi`, `ins`) | **8–11** | 1 |
| `a = b + c` on three packed bytes | **4** (`xtr`, `xtr`, `add`, `ins`) | **10–13** | 1 |

> **The finding, and it is the whole of what bears on Q5: R84's reason is half right.** *"It
> duplicates instructions RV64I already has"* is **true of `nx.xtr`, which saves one instruction**,
> and **false of `nx.ins`, which saves four to seven and, more importantly, saves a 64-bit scratch
> name that a 512-bit register file cannot spare.** Your own #231 asked for *"a subset of bit-manip
> instructions added so that values can be **retrieved/set**"*; #233 then narrowed it to *"we need to
> make sure **EXTRACTION** from the regs is possible. Regular bit manipulation can take you the rest
> of the way."* **On the tile side, regular bit manipulation takes you the rest of the way for reads
> and not for writes.** *(#233's context is `CXW`/`CXR` on the host — CANON.md:6630-6640, where the
> instruction sequence follows immediately — and on the host the claim is correct, because the host
> has a stack and thirty-one 64-bit registers to hold masks in. cons-C27 is scoped to the host
> aperture and is not disturbed by this.)*

**The overhead is per live-range boundary, not per operand.** A packed value read once and used
*k* times costs one `nx.xtr`, not *k*: the extracted copy is an ordinary value in an ordinary name
and every subsequent use is an ordinary instruction. In a loop over a packed structure, the extract
and insert hoist to the loop's entry and exit whenever the value is loop-invariant. **The dynamic
figure is therefore bounded above by the static one and usually well below it, which is the opposite
of the ~3× §2.3 previously asserted.**

### 3A.5 Admission — one added clause, and the scratch reservation is the real cost

> **A2's admission test is §3.6's, with test 2 replaced:**
>
> **(2′)** Peak simultaneous liveness satisfies `Σ w_i ≤ 512` over the true widths
> `w_i ∈ {64, 32, 16, 8}` — **no charge-32 rule** — where every value is placed at a naturally
> aligned offset; **plus 32 bits of extent scratch reserved at every instant at which an extent
> access occurs.**

**Why the scratch exists, and it is not avoidable.** RISC-V computes register-to-register. A packed
8-bit value cannot be an ALU operand; it must be extracted into a **name**, and the name is itself
part of the 512. The smallest name is a `w` tile, and every extent-accessed value is by construction
narrower than 32 bits (a 64-bit value at a 64-aligned offset *is* a `d` name; a 32-bit value at a
32-aligned offset *is* a `w` name — §3A.1's composition), **so 32 bits of scratch always suffices and
64 is never needed.**

**The exact rule and the closed form differ, and both are given because the difference is 2,021
multisets.**

| form | rule | places, of 17,361 | share | factor lost |
|---|---|---|---|---|
| **exact** | `max over instants t of ( live bits at t + 32 if an extent access occurs at t ) ≤ 512` | **≤ 17,360** *(an upper bound; the rule is a predicate on a liveness schedule and cannot be enumerated over multisets — §5.2)* | ≤ 99.994% | ≥ 1.0× |
| **closed form [recommended for the tool]** | `Σ w_i ≤ 512` if no value is narrower than 32 bits, else `Σ w_i ≤ 480` | **13,809** | **79.5%** | **1.26×** |
| design A, **directly nameable only** `[corrected - user ruling 2026-09-03 (liveness)]` | `64a + 32b ≤ 512` — ~~everything narrower charged 32~~; a **naming** rule, not an admission rule | **2,685** *(directly nameable; A **admits** the same set A2 does, packing the rest at ~2-3 ops per access)* | 15.5% *(of direct names)* | 6.47× *(on directness, not capability)* |

**[recomputed]** — same enumeration as §5.2, same universe, same convention: multisets over
{64, 32, 16, 8} summing to at most 512 bits, **with no cap on the number of values**, because an
extent is named by an immediate and is not a register name (this is the correction that moves A2's
closed form from 11,070 to 13,809; the old figure applied design A's ≤ 31-name cap to a scheme that
does not have one). It reproduces four figures the record already carries — 13,091 for the ≤ 31-name
sub-universe, 2,685 for A, 9,132 for the three-tier variant under that cap, 4,233 for §7.4's subtree
variant — which is why the A2 rows computed alongside them are trusted.

**The closed form is conservative and the tool should use it**, because cons-C15 makes an
over-permissive test fatal (§3.8) and the exact form needs a liveness walk the admission tool does
not have today. **The gap is real and should not be papered over:** B1's own headline worked case —
*seven live 64-bit values plus eight live 8-bit values = 512 bits exactly* (§4.5) — is **rejected by
A2's closed form** (512 > 480) and admitted by A2's exact form **only if the byte accesses never
coincide with all seven 64-bit values being live.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **Design A is in exactly the same
position, not worse**: ~~Design A rejects it outright at 704 bits~~ is **struck** — A computes 512
bits, packs the eight bytes into a `d` name, and admits it under the same scratch condition, paying
~2-3 ops per byte access instead of one. So the ordering on that one case is `B1 admits > {A2, A}
conditionally`, and what separates A2 from A there is **instruction count, not admission.**

**Everything else in §3.6 stands unchanged**: the single-instant lemma (all widths remain powers of
two, so first-fit-decreasing still lands every value on an aligned offset), the counterexample
showing the sum is not sufficient, the relocation rule and its below-512 precondition, and the
requirement that a placement be **verified**, not assumed. **A2 makes the allocator's job harder in
one specific way and easier in another**, and both belong on the record:

- **harder:** the allocator now chooses *per value* between naming it (0 instructions, one whole
  name spent) and packing it (its own width, +2 instructions per live range against the ~2-3 the
  same access costs under A) `[corrected - user ruling 2026-09-03 (liveness)]`. That is a knapsack layered on top
  of the mixed-width-with-alignment pairing problem §3.6 already describes.
- **easier — but by less than this claimed** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`**:** ~~Under design A, a function 33 bits
  over budget is rejected under cons-C15 and cannot be offloaded at all; under A2 the allocator packs
  a narrow value and the function runs.~~ **Struck: design A packs too**, by hand, in ~2-3 ops. A
  function is over budget only when its **true** live bits plus scratch exceed 512, and that is
  fatal under A **and** A2 alike. What A2 converts into a graded cost is the *instruction* price of
  packing, not an admission failure.

### 3A.6 The hardware — one execute-stage unit, and where the write port changes

**Extract.** The context array's read path already ends in *"8:1 word select → 2:1 half select on
`offset[5]` → 2:1 extension select"* (§5.3). An aligned extent extract is the same shape with a
wider funnel:

```
    word    = index >> ( 3 - wcode )       // 8:1 select of a 64-bit word  -- the SAME select
                                           //    the named read port already performs
    within  = ( index << ( wcode + 3 ) ) & 63
    field   = aligned funnel of the word by `within`   // 8:1 byte / 4:1 half / 2:1 word
                                                       //    = 3 mux levels, 64 bits wide
    rd      = sx ? sign_extend( field, width ) : zero_extend( field, width )
```

**+1 mux level against the named read port's own path, on the extent instruction only.** The unit is
a **second read port** on the context array, in the execute stage beside the ALU, not a widening of
the register read port — so §5.3's *"+2 mux levels"* on the named path is unchanged. *(The context
array already carries multiple read ports for `W` = 4 pipes; adding one more is the same kind of
provisioning, and unlike design B's map file it is a port on a structure that already exists.)*

**Insert, and this is the only place A2 touches a structure design A also touches.** The write path's
enable granularity goes from **16 word-enables (32-bit, one per `w` tile)** to **64 byte-enables**.

> **There is still no read-modify-write, and that is why `wcode` stops at 8 bits.** Because the
> encoding forces natural alignment and the narrowest extent is a byte, an insert writes a whole
> number of byte lanes and never has to merge with bits it is not writing. **Defining `wcode` 4–7 as
> sub-byte widths (4, 2, 1 bits) would reintroduce exactly the x86 partial-register merge fact-C19
> names as the failure mode to avoid** — so they are **reserved and deliberately unspent**, and this
> is the reason. §4.1 already records that F2's alignment *"buys the straddle-free property back, and
> with it the byte-enable write path"*; A2 buys the same property from the same discipline.

**Three legality rules, applying only to extent instructions** — §3.4's seven become ten under A2:

7. **`wcode` ∈ {4, 5, 6, 7}** — reserved. Traps.
8. **`index ≥ 512 >> (wcode + 3)`** — the extent would run off the end of the context. A 3-input
   check on the top bits of `index`, gated by `wcode`. Traps. *(Load-bearing: without it,
   `{index = 63, wcode = 1}` names bits [1008, 1024) of a 512-bit file.)*
9. **`rsv ≠ 0`, or `sx = 1` on `nx.ins`** — reserved encodings. Trap, so the spare bits stay
   genuinely spare and can be defined later without an ambiguity. §3.4 rule 3 (`x1`–`x7` illegal)
   applies to the `rd`/`rs1` of an extent unchanged.

**Total added hardware.** One aligned funnel (3 mux levels, 64 bits) plus an extension mux; a 9-bit
descriptor decode (a 3-bit variable shift of a 6-bit field, ≈2 mux levels); byte-enable generation on
the write path; three trap conditions. **As a decode ROM, nothing — the descriptor is arithmetic, not
a lookup.** Against design B1's RFT + 576 B map file + fill FSM + refcounts + a `C`-wide invalidation
broadcast, it is not close.

### 3A.7 A2-r — the extract-only variant, which satisfies #233 literally

**If Q5 is answered "extraction only", A2 still exists, and it is worth pricing separately** because
it is the form the user's own words at #233 permit: *"We need to make sure EXTRACTION from the regs
is possible."*

**A2-r drops `nx.ins`.** Packed values become **read-only**: they are placed by the host's `CXW`
staging at `FORK`, read on the tile by `nx.xtr`, and results are written to **named** tiles only.
Thirteen instructions instead of fourteen, one trap condition fewer, and the write path keeps its
32-bit enables — **the byte-enable change disappears entirely, so A2-r touches nothing design A
touches.**

| | A2 | **A2-r** |
|---|---|---|
| instructions added (cons-C22) | 2 → **fourteen** | 1 → **thirteen** |
| write path | 64 byte-enables | **unchanged, 16 word-enables** |
| legality rules added | 3 | **2** (rule 9's `sx`-on-insert clause vanishes) |
| covers | any packed value | **inputs only** — a narrow value produced on the tile must be written back by hand, i.e. through design A's own shift-and-mask (~2-3 ops) rather than an insert `[corrected - user ruling 2026-09-03 (liveness)]`; ~~must go to a `w` name and be charged 32~~ |
| the shape it serves | anything | *many small arguments in, wide results out* — which is the `FORK` staging case §6.1 prices at 20 instructions for eight 32-bit arguments |
| places, of 17,361 | **13,809** | **[NOT COMPUTED — it depends on the ratio of read-only to written narrow values in real code, which nothing in the record measures. Do not quote a number for this row.]** |

**A2-r's honest weakness:** a narrow value that is written on the tile falls back to design A's
~~charge-32 rule~~ **shift-and-mask cost** `[corrected - user ruling 2026-09-03 (liveness)]`, so A2-r's benefit depends entirely on a program property nobody here has measured.
**It is the right thing to build if Q5 is answered "extraction only" and the wrong thing to argue for
in the absence of that ruling.**

### 3A.8 What A2 costs that design A does not — the complete list

| | cost |
|---|---|
| **two tier-1 reopenings** | **cons-C26 / R84** (the mechanism) and **cons-C22** (the count of twelve). Neither is a trade this document can make; both are Q5. **This is the whole of the objection to A2 that survives §2.3's corrections.** |
| **the last spare opcode** | `custom-1` is spent. `custom-2`/`custom-3` are claimed by RV128 (`nmfc_isa.h:18-20`), so after A2 there is no clean second reservation left |
| **ISA deviations** | **nine become ten** (§5.5): the tile's instruction set is no longer *"`RV64IMAFD`, minus the excluded subset, plus the twelve"* |
| **a non-arithmetic immediate** | I-type immediates are sign-extended by the base ISA; A2's is a bit field and never is |
| **a compiler decision design A does not have** | pack-or-name, per value — a knapsack on top of §3.6's pairing problem (§3A.5) |
| **byte-enable write path** | 16 word-enables become 64 (A2 only; A2-r keeps 16) |
| **a second context read port** | in the execute stage, on a structure that already has several |
| **cons-C14 is not restored** | A2 has no `UNDEFINED` code and no run-time undefined-name trap; SW1 (§3.6a) is inherited from A unchanged |
| **seven supersessions to mark** (cons-C31) | **design A's five** — DESIGN §25.7 **D:2425-2429**, CANON.md:9849, CANON.md:127, CANON.md:6029-6060 and **O4's spelling** (§6.3) — plus **cons-C26/R84** and **cons-C22**. *(An earlier revision counted four, because design A's ledger was itself short by three.)* |
| **nothing in the record asks for it** | §5.4's two decompositions show no sub-32-bit value, so **A2's benefit is entirely hypothetical on today's evidence while its costs are certain.** **Weaker than it sounds:** §3.8 records that those decompositions *"are not currently reproducible from the tool and must be re-measured"*, because `annotate`'s width lambda charges 16 bits for `a0` and cannot see widths at all. **The evidence is an absence of measurement, not a measurement of absence.** This is the same objection that defers B2, and it applies to A2 with equal force |

**What A2 does *not* cost, and each of these is a real difference from design B:** no per-context
state, no envelope bits, no migration change, no fetched object, no cache, no fill FSM, no eviction
policy, no identity check, no staleness, no linker obligation, no page alignment, no ASID hole, no
kernel invalidation, and **no new run-time failure mode** — the three added legality rules are decode
traps on malformed encodings, which is the same class of thing §3.4 already has.

---

## §4 DESIGN B — THE MAP AS AN EXTENSION OF THE CONTEXT, IN FULL

Your sketch, built out. **This section carries `final-B-context-map.md` forward with four
substantive amendments**, marked **[AMENDED]**. Three of them are defects in the recommended
configuration; the fourth promotes B2 from an open question to a design.

### 4.1 The one-sentence statement, and the format

> **The map is per-FUNCTION state, reached through the context, resident in the function's own
> read-only code image, cached on each tile in a 576-byte on-core table, and named by a three-bit
> index that lives in the context's tile-local scheduling slot — never in its 512 bits and never on
> the wire.**

"An extension of the context" is exact, and it is worth being precise about *which* extension: the
map is not part of the context's **value** (I2's 512 bits are untouched) and not part of its
**identity** (the handle is). It is part of how the value is **read** — the same kind of thing as
the function's code, and it lives in the same place.

**A complete map is indexed by the pair (namespace, number) — 64 encodings, 63 needing an entry**
(`x0` denotes no bits; `f0` is general). **That is the Q2 = no shape, and it is the one B1 is costed
in throughout §4** — under the recommended Q2 = yes there is one namespace and the map is **32
encodings, 31 entries**, which halves the image (40 B → 22 B), halves the on-core file
(576 B → 288 B), and lowers B1's **direct nameability** from 17,360 to 13,091 (§5.2) `[corrected - user ruling 2026-09-03 (liveness)]` — its capacity is 512 bits either way. Every "63" in §§4–5 is
the two-namespace figure and should be read as such. The tree's `RegLayout` is **half a map**:
`NUM_NAMES = 32`, the `x` namespace only (`NMFCRegLayout.h:39-40`), which was correct under RV64IM+A
and is **exactly the one-namespace shape Q2 = yes asks for**.

**Recommended format, F2 — aligned powers of two, 9 bits per entry:**

```
  +0    header            4 B     magic/version:8, flags:8, bits_used:10, names_defined:6
  +4    x-half           36 B     32 entries x 9 bits
  +40   f-half           36 B     32 entries x 9 bits          (present iff flags bit0)

  entry = { width_code:3 , index:6 }
          0 = UNDEFINED (a reference traps -- cons-C14)   1 = 8   2 = 16   3 = 32   4 = 64
          bit offset = index << (width_code + 2)
```

**F2's offset compression is the whole saving.** A naturally aligned slice of width *w* starts at a
multiple of *w*, so the offset is `index × w` with `index < 512/w`: **a single 6-bit index field
covers all four widths**, and the decoder recovers the offset with one variable shift where the
shift amount *is* the width code. No adder, no table. **40 B integer-only, 76 B with `F`/`D`.**

**Why F2 and not F0 (free offset, free width, 128 B).** F0 is the only format that expresses
everything I2 names — a 48-bit pointer beside a 12-bit index and a 3-bit tag. It costs two lines
always, a decoder that shifts across a 64-bit word boundary — i.e. `Context512::read`'s two-word
splice branch (`NMFCRegLayout.h:95-98`) stays **live** rather than becoming dead code — and a host
path where a value may straddle a `CXW`/`CXR` lane and cost two `CXR`s and a splice. **F2 buys the
straddle-free property back, and with it the byte-enable write path.**

> **THE ONE RULE THAT MAKES B DIFFERENT FROM A: no two defined names in one map may overlap.** The
> tile verifies it **once, at fill** — a 512-bit occupancy accumulate over 64 entries, ~64 cycles of
> a small state machine, off the issue path — and a map that violates it is an illegal-map trap.
> This is the same computation `annotate` must already do to emit the map.

### 4.2 Where the authoritative copy lives, and the identity problem

**The place is already built.** Maps sit beside the function's code on its **DUP** page — *"NMFC
mode, and replicated on every channel… only sound because the pages are read-only"* (CANON.md:2113)
— and DESIGN §26.1 has already moved `.rodata` into the same `__dup_start`/`__dup_end` span. So the
storage is a page type this machine has, replicated on every tile, local to every tile that can run
the function, with a measured **100.00% I-cache hit rate** (CANON.md:8267). **This is not an answer
to the ruling and is not offered as one** — the standing prohibition at `register-map-context.md`
§6 is respected; the objection was simultaneity, not availability, and §4.8 answers it on its own
ground.

**P1 — a header immediately before the entry PC.** `map_addr = entry_pc - 4 - len`. Self-identifying
by construction: the image's address *is* the entry PC minus a constant, so no tag is stored and no
forgery is possible. **What it does not give: it is reachable only from the entry PC**, and a
migrating context has a resumption PC.

**P2 — your page-header cheat.** `map_addr = pc & ~4095` (the page is 4 KiB —
`src/nmfc/nmfc_vmem.cc:72-73` `[CONFIGURATION]`). **It solves the identity problem outright**, and
that is a strength no other variant has. Its four prices: 40–76 B per 4 KiB page = **1.0–1.9% of the
code region**, paid `N` times over because a duplicate page's physical footprint is `M = N × G`;
**one map per page, not per function**, so either every function on a page shares a superset layout
(which makes one function's admissibility depend on its neighbours' and breaks separate
compilation) or **every function is page-aligned** (up to 4 KiB of internal fragmentation ×`N` =
**16 KiB per function at `N` = 4, and 128 KiB at `N` = 32**); your own **cross-page reload** caveat;
and **the thrash case your sketch does not name — a loop straddling a page boundary alternates
between two headers and reloads every iteration**, so a one-entry derived-map register thrashes at
100% and P2 does not remove the cache, only the identity problem.

> **[AMENDED — P1 and P2 cannot both be sound as recommended.]** `final-B-context-map.md` §3.4
> recommends *"P1 as the authority, P2 as the cold-arrival fallback"*, and §3.3 offers page-aligned
> functions as *"the honest choice"*. Under page-alignment **only a function's first page carries a
> header**, so a cold arrival whose resumption PC lands on page 2+ computes `pc & ~4095` and reads
> **instruction bytes as a map** → illegal-map fault. **A function longer than 1024 instructions
> cannot cold-arrive**, and canon explicitly refuses to assume functions are that small — user #78,
> CANON.md:3011-3013: *"are you saying each function consumes no more than 8 instructions? I find that
> unlikely. are you serious?"* Under the superset alternative, the same context decodes through the
> exact P1 map on a hit and a *different* superset map on a cold arrival — **a silent wrong result
> across a migration**. **The only sound combination is: page-align functions AND repeat the exact
> per-function map image at the base of every page the function occupies.** That is stated nowhere
> in the source document, and it changes P1's advertised *"code-space overhead ~0"* to **40–76 B per
> page × `N` physical copies.**

### 4.3 The per-core cache, and why the handle index is not the key

**Your requirement 1 is an array indexed by context handle; requirement 3 adds a tag.** Two things
have to be true for that and neither is:

**Finding 1 — the arriving identity does not exist.** `MigrationEvent` (`NMFCFabric.h:94-123`)
carries no entry PC and no function id; `handleMigration` sets `c.pc = mig.pc`
(`NMFCTile.cc:1385`), under the comment *"It resumes at the instruction that sent it here."* At
`FORK` the entry PC **is** available (`admit()`, `NMFCTile.cc:544-580`, `c.pc = inv.pc`); at the
destination of a migration it is not. **That is the case the whole design exists to serve.**

**Finding 2 — `CONT` changes the function under a constant handle and cannot fail.**
CANON.md:6243-6250, verbatim: *"`CONT` is I10's successor, and it **CANNOT FAIL**. It inherits the
existing FTU entry rather than allocating one… It is also the mechanism for splitting a function too
large for one 512-bit register file into a chain."* **A `CONT` chain is a sequence of different
function bodies, with different maps, under one unchanging handle.** So the check must run at every
`CONT` — which no handle-indexed cache does by construction.

**Finding 3 — once the tag exists, the handle index is strictly worse than the tag itself.** A cache
keyed on `handle` with tag `fid`, and a cache keyed on `fid`, do the same comparison; the second is
smaller (a tile runs a handful of distinct functions, CANON.md:5386-5392, against up to 1024 live
handles) and **shares one fill across every context of that function**.

**The arithmetic, on the record's worst measured migration rate** (CANON.md:4972-4978, *"these are
settled numbers"*: 196,904 migrations for 262,143 loads, seven loads per invocation, 25.0% balance
on every tile):

```
   invocations            = 262,143 / 7                        = 37,449
   migrations/invocation  = 196,904 / 37,449                    = 5.26
   distinct tiles visited = 4 x (1 - (3/4)^6.26)                = 3.34   [ESTIMATE, not measured]
```

| | handle-keyed | function-keyed |
|---|---|---|
| cold fills, whole run | 37,449 × 3.34 = **≈125,080** | 4 tiles × 1 function = **4** |
| fills per migration | 0.635 | 0.00002 |
| added arrival cost | one local access on **63.5%** of migrations, against a measured **2.2–2.3-cycle** arrival (CANON.md:1335-1337, 8267) — **roughly a doubling** | **none** after the first invocation |

> **The handle index is not viable, and it does not need to be — the tag your requirement 3 already
> demands is a strictly better key than the handle it would have tagged.** And your requirement 4 —
> *"excluding startup or a cold touch, the function should never have to fetch its regfile map"* —
> is met **only** by the function-keyed form: under handle-keying, every new invocation is a cold
> touch.

**The recommended structure.** Three parts; only the third is consulted at decode.

```
  (1) RFT -- resident-function table.  SHARED, F = 8 entries.
      { valid | asid | lo_pc | hi_pc | map_index }  ~16 B  ->  128 B per tile
      Consulted at admit, at migration arrival, at CONT, at RESUME.  NEVER at decode.
  (2) MAP FILE -- SHARED, 8 x 72 B = 576 B, banked by NAME, W-replicated = 2.25 KiB.
      Written once at fill; read-only thereafter.
  (3) PER-CONTEXT: mapIndex:3 + mapValid:1, in the tile-local slot beside ibufPC/dbufReg.
      4 bits x C = 512 B per tile at C = 1024.
```

**Why `F` = 8.** Canon's own economy for a structure of exactly this shape, CANON.md:5386-5392 (the
shared BTB): *"Shared, not per-context — that is the whole economy of it… a tile runs a handful of
distinct functions… and it does not scale with `C`."* The map file is **576 B against 22 KiB of
context state at 256 contexts**, smaller than a structure canon has already accepted, for the same
reason. **`F` is `[CONFIGURATION]`, the shape is the claim — and the canon passage it rests on is
itself flagged `[UNSOURCED AT TIERS 1-3 — MODEL-AUTHORED]`, which the source document does not
mention.** Every cold-fill number is linear in `F`, and **nothing in the record measures it.**

> **[AMENDED — the RFT range cannot be built from the specified header.]** §4.3 of the source makes
> the RFT a PC-**range** CAM and says *"`hi_pc` comes from the map header."* But §2.4 fully
> specifies the header as 4 bytes — magic:8, flags:8, bits_used:10, names_defined:6 — with **no
> function-extent field**; the only length in the design is P1's back-pointer, which is the *map
> image's* byte length (40–76 B), not the function's. So `hi_pc` is either unavailable or set to
> `lo_pc + 76` ≈ 19 instructions. **Any resumption PC further into the body misses the RFT and
> refills — i.e. essentially every migration arrival, which is the case the design exists to
> serve.** The fix is one 16-bit extent field in the header. As written, §5.3's *"four cold fills
> for the entire run"* and §10.2's *"0 memory accesses, 0 added cycles"* do not hold.

### 4.4 What B restores of cons-C14 — **[AMENDED: half, not all]**

| error | design A | design B1 |
|---|---|---|
| a name the function was not allocated | every name always defined ⇒ **undetectable** | `width_code = 0` ⇒ **traps at decode**, exactly as `NMFCTile.cc:460-475` does today |
| **two live values on overlapping bits** | `d0` and `w0`/`w1` are the same bits ⇒ **undetectable** | **also undetectable** — see below |
| a stack access (`sp` = `x2`) | illegal ISA-wide | **undefined in the map** unless the function asked for it ⇒ traps, **per function rather than ISA-wide** |

> **[AMENDED.]** The source document's §2.5 table and §14.2 score B as *"the only candidate that
> meets C14"*, with over-liveness *"rejected at fill"*. **The fill-time check detects a malformed
> map — an overlap the emitter wrote — which is an artefact of the tool, not a property of the
> program.** The same cell then concedes the actual case: *"the only remaining error is two values
> sharing one name, which is a use-after-free of a register"* — **and that is precisely what an
> allocator does when it runs out of bits and coalesces two simultaneously-live values onto one
> name.** The map it emits is perfectly non-overlapping, the tile accepts it, and the wrong answer
> is silent, exactly as under A. **What B genuinely restores is the undefined-NAME trap.** That is a
> real and worthwhile gain and it is the honest basis for Q4; *"the only candidate that meets C14"*
> is not, and should not be read as if over-liveness were caught.

Two further run-time failure modes are real and are B's alone: **a stale entry** after a
recompilation that keeps the entry PC and length (R4's kernel invalidation is the remedy, and it is
**new privileged host work not in the record** — `FENCE` is outside the subset so no body can
flush); and **the ASID hole** — handles are FTU indices and entry PCs are virtual, while neither
`MigrationEvent` nor `TileContext` carries an ASID, so two address spaces can hit each other's maps.

> **[AMENDED — the recommended eviction policy is hold-and-wait, which canon forbids by name.]**
> §4.4 and Q5 recommend *"refcount and refuse… refusing rather than evicting is already this
> machine's idiom for the FTU (I.5)."* **The FTU analogy does not transfer.** The FTU refuses at
> `FORK`, *before* anything is committed and where the requester can back off (CANON.md:203,
> *"refuses rather than evicts"*). Here the arriving context has **already** consumed a context slot
> — `handleMigration` pops `freeSlots_` and fatals if none is free (`NMFCTile.cc:1368-1380`) — so
> stalling it on one of `F` = 8 map slots is **a resource held while waiting for a resource**, which
> is I.1's definition of the shape that deadlocked this machine: CANON.md:6080-6081, *"A blocking
> instruction is a resource held while waiting for a resource — the same shape as the migration path
> that deadlocked the machine at cycle 9,100,426 with four tiles at 0–1 free contexts and 983 tokens
> waiting."* **And it collides directly with `CONT`, which cannot fail**: with eight resident
> functions each holding a live reference, a ninth `CONT` target has no slot and the `CONT`ing
> context stalls on a referencing context that may itself be stalled on a `CONT`. **Evict-and-
> broadcast — clear `mapValid` on every context pointing at the slot, a `C`-wide 3-bit compare — has
> no such property and is the correct choice.** Design A has no analogue because it has nothing to
> allocate.

### 4.5 What B costs, and what it buys

| | number |
|---|---|
| per-context state | **4 bits** (index + valid) + **6 B** of pre-resolved operand fields = +6.5 B on ~87 B (DESIGN §25.7 **D:2434-2437**) = **+7.5%** |
| per-tile state | RFT 128 B + map file 2.25 KiB (`W`-replicated) + index array 512 B at `C` = 1024 ≈ **2.9 KiB**, **+3.3%** against 87 KiB of context state |
| migration | **72 B, unchanged** under ID-1 (identity derived at the destination). Under ID-2 the *envelope* grows 8 B and `SIZE_BYTES` must grow with it or the model lies (`NMFCFabric.h:107-108`) |
| map traffic in steady state | **zero** |
| cold fills, whole program | `N × F` — **tens of line reads for an entire run** |
| **what it buys** | **a map lookup in place of shift-and-mask** on a sub-name access `[CORRECTED - user ruling 2026-09-03 (liveness)]`. Its old worked case is withdrawn: ~~seven live 64-bit values plus eight live 8-bit values = 512 bits, which K.6 admits at exactly 512 of 512; design A computes 448 + 8×32 = 704 and rejects it… "runs on the tile" against "cannot be offloaded"~~ — **design A computes 512 and admits it too**, packing the eight bytes into a name and reaching them by shift-and-mask through a scratch name. **The comparison is a percentage after all, and a small one: ~2-3 ops per packed access against one lookup.** |

**A per-context copy of the map (B1a) is rejected on arithmetic.** At `C` = 1024 a 76 B per-context
map is **+76 KiB against 87 KiB — it very nearly doubles the per-context state of the tile**, to buy
a copy of a table identical for every context running the same function. DESIGN §25.7's own sentence
about the rejected mechanism — *"many contexts run the same function, so it is one small table
entry… and does not scale with `C`"* — is the argument against it, and it was written first.

### 4.6 The port-width question, answered

Your concern was exact: `W` = 4 pipes each decode an instruction **of a different context**, hence
potentially a different map; worst-case operand count is four (`fmadd.d`), so **16 map entries could
be demanded in one cycle from up to 4 distinct maps.**

**The answer is banking, and it is free.** Hold the map file as **64 banks — one per architectural
name — each 8 entries × 9 bits = 9 B.** An operand read is: the (namespace, number) pair selects the
*bank*; the context's 3-bit index selects the *row*. **That is an 8:1 select of 9 bits — 3 mux
levels, not a 512:1 select.** A 9-bit × 8-row bank is 72 flops, so provisioning 16 reads per cycle
by multi-porting or replicating a 72-flop array **costs nothing**. This is your own "banked"
intuition applied to the map rather than the register file, and unlike banking the register file it
is free, because the thing being banked is 576 B.

> **[AMENDED — the source document's headline answer to your question is a throughput error.]**
> §6.4(iii) claims that resolving the map at fetch-buffer fill drops demand *"from 16 entries/cycle
> to ~4"*, concluding in bold that *"the map cache does not need a port for the width of the
> machine… the width of the machine never sees the map at all."* **In steady state the fetch rate
> equals the issue rate.** Every issued instruction consumes its context's single-entry fetch buffer
> (H.5, CANON.md:5362, *"filled with the context's next instruction at the end of each
> dispatch"*), which must be refilled — so `W` = 4 buffers fill per cycle, each needing up to 4
> lookups = **16 entries/cycle, identical to resolving at issue.** Resolving early buys **latency
> slack, not throughput**, and the document conflates one context's `Dp` = 8 window with aggregate
> bandwidth. **The correct answer to your question is the banking above, which is stronger anyway:
> you do not need to move the lookup, you need 16 reads out of a 576 B flop array, and that is a
> handful of gates.**

**Resolving early is still worth doing, for a different reason.** The instruction word sits in the
context's own buffer a whole re-issue window (`Dp` = 8) before it issues; widening that buffer by
4 × (9-bit offset + 3-bit width code) = **48 bits = 6 B** takes the lookup off the issue path
entirely. This is the same trade canon already made for the BTB — *"what the BTB supplies is a fetch
address a window earlier"* (CANON.md:5380). **One correctness obligation the source document does
not state:** the BTB's justification is that *"being wrong is free"* (CANON.md:5383), so a
mispredicted fetch buffer holds an instruction that never issues — **and pre-resolving its operands
against a map that leaves one undefined would fire an architectural trap on a non-executed
instruction.** The pre-resolution must carry a **poison bit** and defer the trap to issue. Small, but
it must be written down, because without it B's two strongest claims — restored cons-C14 and
zero-cost early resolution — are in conflict.

### 4.7 B2 — width classes, and "banked per width" evaluated on its own terms

**A pure single-width class is unusable, and the reason is one sentence.** *Every offloadable
function on this machine dereferences a pointer, and a pointer is 64 bits* — canon's own
decomposition is *"own an edge range and chase scattered vertex values"* (CANON.md:8723). Under a
pure 32-bit class **no name can be a load's base address**; under a pure 64-bit class the machine is
`RegLayout::defaultLayout()`, which cons-C30 names as the SST layout again and #238 forbids. **So a
class must be a MIXED layout — B2 is not "contexts aligned to widths", it is "contexts aligned to
layouts".**

**Therefore B2 is design A, parameterised — and design A is B2 with `K` = 1.**

> **[AMENDED — the class table was written for Q2 = no, and this document recommends Q2 = yes.]**
> Classes 2 and 3 as originally tabulated name `f0`–`f15` and `f0`–`f31` **as names distinct from the
> `x` names**. Under Q2 = yes those *are* `x0`–`x31`, and class 3 needs **4 + 32 = 36 names against 31
> encodable** — it is unrepresentable, and with it the *"32 byte names over half the file"* quoted in
> §0 and §10. **The repair is arithmetic:** with one namespace, a complete class is any `n₆₄ × 64 +
> n₈ × 8 = 512` with `n₆₄ + n₈ ≤ 31`, whose byte-maximal solution is **5 × 64 + 24 × 8 = 29 names**.
> Both tables are given, because which one applies is Q2's to decide.

**Under Q2 = yes (recommended) — one namespace, 31 encodable names:**

| class | layout | names | complete? | places (§5.2) | for |
|---|---|---|---|---|---|
| **0 — D+W** | `x8`–`x15` = 8 × 64; `x16`–`x31` = 16 × 32 | 24 | both tilings complete | **2,685** *(directly nameable)* | **design A verbatim** — pointers and `int`s, the default |
| **1 — D** | `x8`–`x15` = 8 × 64; `x16`–`x31` illegal | 8 | complete | **495** | all-64 functions; the day-one `-ffixed` path reaches exactly this |
| **2′ — D+H** | `x8`–`x11` = 4 × 64 (bits 0–255); `x12`–`x27` = 16 × 16 (bits 256–511) | 20 | complete | **2,295** | four pointers and sixteen halfwords |
| **3′ — D+B** | `x8`–`x12` = 5 × 64 (bits 0–319); `x13`–`x31` + `x1`–`x5` = **24 × 8** (bits 320–511) | 29 | complete | **1,400** | **five pointers and twenty-four byte names.** *(Class 3′ is the one class that must re-use `x1`–`x5`, which the Heap Rule reserves; a class redefines the whole map, so this is legal, and it is the price of a byte tier in one namespace.)* |
| | **union** | | | **4,143** | |

**Under Q2 = no — two namespaces, 63 encodable names (the original table):**

| class | layout | names | complete? | places (§5.2) | for |
|---|---|---|---|---|---|
| **0 — D+W** | `x8`–`x15` = 8 × 64; `x16`–`x31` = 16 × 32; `f`*n* ≡ `x`*n* | 24 | both tilings complete | **2,685** | design A verbatim |
| **1 — D** | `x8`–`x15` = 8 × 64; `x16`–`x31` illegal | 8 | complete | **495** | all-64 functions |
| **2 — D+H** | `x8`–`x11` = 4 × 64 (bits 0–255); `f0`–`f15` = 16 × 16 (bits 256–511) | 20 | complete | **2,295** | four pointers and sixteen halfwords |
| **3 — D+B** | `x8`–`x11` = 4 × 64 (bits 0–255); `f0`–`f31` = **32 × 8** (bits 256–511) | 36 | complete | **1,015** | four pointers and thirty-two byte names |
| | **union** | | | **4,175** | |

**[all recomputed, §5.2's enumeration and convention — and every figure is a DIRECT-NAMING count,
not a capacity bound: `[user ruling 2026-09-03 (liveness)]` every class holds 512 bits and expresses
every width, packing what it cannot name at ~2-3 ops per access.]** **The two unions differ by 32 multisets —
under 1%** — so the class field is worth very nearly the same under either Q2 ruling, which is why Q2
can be ruled on its own merits without deciding Q1. **The direction is not the obvious one:** the
one-namespace byte class 3′ *places more than* the two-namespace class 3 (1,400 against 1,015),
because five 64-bit names serve more shapes than four; the union nonetheless falls, because **only a
two-namespace class can **directly name** thirty-two simultaneous byte values** `[corrected - user
ruling 2026-09-03 (liveness)]` — every class can **hold** them, packed — and no other class names them.

**Cost.** 2 bits in the tile-local context slot (**256 B per tile at `C` = 1024**); decode becomes a
**4:1 mux on the class instead of A's 2:1 on `n[4]` — +1 mux level**; as a ROM, 4 × 63 × 10 =
**2,520 bits per tile** — `4 × 63 × 10`, **the Q2 = no figure**; under the recommended Q2 = yes it is
`4 × 31 × 10` = **1,240 bits** — shared, combinational, the same kind of object as the opcode
decoder's truth table (A's is 310 bits, or 150 under one namespace). **Declared at `FORK` in two spare bits of the invocation, and carried in the
migration *envelope*** — where `handle`, `origin`, `from`, `to` and `wantsReturn` already travel and
are already free, and where `SIZE_BYTES = NMFC_CTX_BYTES + 8` is the **payload**, not the envelope.

> **[AMENDED — this is the promotion.]** `final-B-context-map.md` files the envelope-carried class
> as open question Q6, and §7.5 says *"the class is not in the 72 B, so it is re-acquired exactly as
> B1's map index is — the same RFT"* — which contradicts §13's claim that B2 *"genuinely removes the
> third referenced object."* **Only the envelope-carried form makes §13's claim true**, and it is
> free by the document's own argument about `handle`. **B2 with the class in the envelope needs no
> RFT, no fill FSM, no refcount, no eviction, no kernel invalidation, no linker obligation, no page
> alignment, no ASID hole, and nothing that can go stale.** It should be B2's definition, not an open
> question, and three of the six judging lenses independently reached the same conclusion.

**What B2 loses, and it must be stated:** no per-function packing at all — the width mix is chosen
from a menu; **under Q2 = no, classes 2 and 3 have no 64-bit `f` name, so a function in either cannot
do double arithmetic** (a direct consequence of ruling O4 that the source document's class table does
not mention — *under Q2 = yes this disappears, because the `d` names of classes 2′ and 3′ carry
doubles like any other 64-bit value*); and **cons-C14 is not restored** — every name in a class is always defined, so A's
regression is inherited verbatim.

**"Banked per width", taken literally, is evaluated and rejected — and the arithmetic matters.** Your
phrasing suggests going further: partition the tile's contexts by class so the register file and
datapath are **physically banked**, each bank one geometry and one fixed extract network. What it
buys: the per-class mux disappears and a bank's read port is exactly its class's width. **What it
costs decides it: contexts become statically partitioned, so a workload whose functions are all one
class uses `C/K` of the tile.**

| | `K` = 4 banks | is the barrel fed? floor is `C ≥ W(Dp + L/I)` ≈ **132** at `W`=4, `Dp`=8, `L`≈100, `I`≈4 (DESIGN §25.2) |
|---|---|---|
| `C` = 1024 `[CONFIGURATION]` | 256 usable | **yes**, but latency tolerance falls from 7.8× the floor to **1.9×** |
| `C` = 256 `[CONFIGURATION]` | **64 usable** | **NO — 64 < 132.** The tile cannot keep its pipes fed |

> **Verdict: bank the MAP by name — that is free. Do NOT bank the CONTEXT ARRAY by class.** Static
> partitioning turns a workload-shape property into a hard capacity limit, and at the derived context
> count it falls below H.2's own floor. The class field rides with the context in a single
> unpartitioned array; the geometry it selects is a **mux, not a bank**.
>
> *(Correction to the source document's supporting citation: it writes *"`NMFCTile.cc:41-49` already
> makes `contexts < pipes × depth` fatal"* against 64 usable contexts. That check is
> `pipes × depth` = 4 × 8 = **32**, which 64 passes. The verdict is right; the analytic floor of ≈132
> is the argument, and the code check is not.)*

### 4.8 The honest statement — the third referenced object

The ruling, once more: *"It introduces a third piece of memory every context needs. So now we have
the map, instruction, and potentially data that must be referenced all at the same time."*

**Design B1 does not remove the third object. It is the third object, rebuilt.** Every softening is
real and none is a rebuttal:

| the objection | what B1 actually does |
|---|---|
| "a third piece of **memory**" | **True at fill, false in steady state.** Memory is referenced `N × F` times for a whole program. After that the map is an **on-core 576 B array read like a decode ROM** |
| "every context **needs**" | **False as stated, true in substance.** Contexts of one function share one 72 B entry and a 3-bit index; the per-context cost is 4 bits + 6 B |
| "referenced **all at the same time**" | **This is the part that survives.** Moving the read a window earlier means it is never *simultaneous* with the data access — but the object exists, and it has to be filled, kept, invalidated, and got wrong |

**B2 is different, and this is the cleanest thing here to rule on.** A width class is **2 bits in the
context's own tile-local slot** — the same kind of thing as `ibufPC` or `holdsLine` — and the
geometry is a wire pattern. **There is no table, nothing is fetched, nothing goes stale, nothing
needs invalidating, and the ruling's objection does not apply to it at all.** What it costs is
per-function packing: a function picks a layout from a menu of `K` instead of writing its own.

---

## §5 HEAD-TO-HEAD, ON THE THREE CRITERIA YOU NAMED

### 5.1 Implementation complexity

| | **A** | **A2** | **B2** | **B1b** |
|---|---|---|---|---|
| new hardware structures | **none** — 310 bits of decode ROM, or ≈8 gates | **one execute-stage unit** — an aligned funnel (3 mux levels, 64 bits) on a second context read port, plus byte-enable generation. **No ROM: the descriptor is arithmetic, not a lookup** | **none** — 2,520 bits of decode ROM | **RFT (128 B) + map file (2.25 KiB) + fill FSM + refcounts + a `C`-wide invalidation broadcast** |
| new state per context | **0** (+3 bits of fill class a stock in-order core already carries) | **0** — as A | **2 bits** | 4 bits + 6 B of pre-resolved fields |
| new state per tile | 39 B | **39 B** — as A | 315 B | ≈2.9 KiB at `C` = 1024 |
| new messages | none | **none, and none in the envelope either** — the only variant besides A that adds neither | none (2 bits in the envelope) | none under ID-1 |
| decode | +1 mux level, ≈8 gates | **as A on the named path, unchanged**; +1 mux level on the extent path only | +2 mux levels | a banked 8:1 read (3 levels), or 0 if resolved a window early |
| new ISA rules | 6 legality rules, **9** deviations *(was 7 and 10 before W1b was struck)* | **9 legality rules, 10 deviations, and the instruction count goes 12 → 14** | as A, ×`K` | **6 inherited deviations + 1 stronger form** |
| new run-time failure modes | **none** — *and that is also its weakness* | **none** — its three added rules are decode traps on malformed encodings, the same class §3.4 already has | **one** (class mismatch at `FORK`) | **four**: stale map, evicted slot, unresolvable identity, invalidation on code load |
| **tier-1 decisions reopened** | **none** | **two — cons-C26/R84 and cons-C22.** The only candidate that needs a ruling before it can be built at all | **none** | **none** (it *completes* DESIGN §25.7) |
| build-system obligations | rewrite trailing `ret`; register-class split in the back end; a no-spill post-RA gate | as A, **plus a pack-or-name decision per value** | + declare a class | rewrite `ret`; **emit the map**; linker places it; page-align functions; **kernel invalidates it** |
| compiler back end | new register file, new `SubRegIndices`, new encoding map — the ARM SPR/DPR shape, which LLVM has allocated in production for fifteen years. **Plus a post-RA no-spill gate (§3.7's "gap nobody has costed"), which is on the critical path and in no estimate** | as A, plus two intrinsics/patterns | as A | **materially less** on the target description — 63 opaque uniform names (31 under Q2 = yes), no `SubRegIndices` at all… |
| …**and materially more on the allocator** | 8 `D` and 16 `W` names **physically overlap**, so **when `W` is declared as `SubRegIndices` of `D` (§3.7)** a stock allocator's existing aliasing machinery (LLVM `RegUnit`s, GCC IRA conflict sets) makes **512 bits an automatic capacity limit** and over-liveness surfaces as an ordinary allocation failure. **This is the resolution of the apparent conflict with §3.6a**, which calls SW1 undetectable: a *stock allocator with the sub-register relation declared* does not produce SW1; the admission tool's own FFD placement, hand-written bodies, and any back end that omits the relation still can, and that is what the ~40-line verifier is for | **as A, plus a knapsack** — pack-or-name per value, on top of the pairing problem. **But running out of bits stops being fatal**: A rejects under cons-C15, A2 packs and runs | as A | **the 63 names (31 under Q2 = yes) are disjoint by construction, so nothing in the allocator's pressure model — which counts registers, never bits — sees the 512-bit ceiling.** An allocator handed 63 free names will produce 20 live 64-bit values = 1280 bits, and with no spill there is no legal recovery. **Bit-weighted pressure is a new allocator, not a new emitter** |
| SST delta | ≈150 lines net across three files + 31 mechanical call-site edits (§9) | **as A, +≈60 lines** — one decode case, one funnel, three legality rules; and `NMFCRegLayout.h`'s straddle branches still delete | + a class field | a rewrite of the decode path, a fill FSM, an RFT, and a linker script |

**The compiler comparison is the one place the source documents disagree with each other, and both
are half right.** `final-B-context-map.md` §9.2 says B's back-end work is *"materially less"*; its
own §12.4 then concedes the placement problem is *"harder"* under B. The row above resolves it: **B
is easier on the target description and harder on the allocator**, and for a real back end the
allocator dominates.

### 5.2 ~~The expressiveness cost~~ **THE DIRECTNESS COST, recomputed**

> **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** *(dated 2026-09-03)* **There is no
> expressiveness cost, and this section's whole framing is corrected.** The context is 512
> independent bits on a strictly in-order core with no renaming; a value narrower than a name is
> **packed** with others inside one name and reached by shift-and-mask through a **scratch** name
> (plain RV64I: `srli`/`slli`/`andi`/`or`). **Every scheme in the table below therefore places every
> one of the 17,361 multisets that fits in 512 bits, subject only to leaving room to stage.** What
> the table actually measures — and it is still worth measuring — is **how many multisets each
> scheme names DIRECTLY**, i.e. how often an access costs one instruction instead of ~2-3. Read
> every "places", "share" and "factor lost" as **directness**, never as capability:
>
> - **A and B1 have IDENTICAL capacity: 512 bits.** The comparison this section was built to
>   sharpen **collapses**; what remains is A ≈ 2-3 extra ops per packed access against B1's map
>   lookup per access, and A2's one instruction.
> - **"Factor 6.47", "factor 4.9", "factor 162", "79.5% lost", "15.5%"** — none is an admission
>   loss. **Design A loses no function that K.6 admits.**
> - The row rules that read *"charge each value up to the narrowest name width the scheme
>   provides"* and *"the charge-32 rule"* describe **naming**, not admission.
> - **`{64 × 8 bits}` — I2's "64 1-byte regs" — IS placeable under design A**, packed eight per
>   64-bit name; see the corrected note below.

**The struck framing, retained for the record: the expressiveness cost, recomputed.**

> **[THE TABLE IS REBUILT, BECAUSE THE OLD ONE WAS NOT COMMENSURABLE — WHICH IS THE DEFECT §5.2
> EXISTS TO CORRECT.]** The previous table claimed *"one universe, one question, every row
> comparable"* and was not: **A's 2,685 was computed under Q2 = yes** (one namespace, 24 names) while
> **B2's 4,340 required Q2 = no** (its classes 2 and 3 name `f0`–`f31`, §4.7); and the universe was
> **capped at ≤ 31 names**, which is *A's own cap*, so B1's row scored 100% of a universe defined by
> its competitor's limit, B2's class 3 (36 names) was scored under a cap it exceeds, and **I2's
> literal "64 1-byte regs" was excluded from the universe outright** — the case that started the
> argument. **The universe below is the bit budget alone.**

**The measure.** Count every multiset over widths {64, 32, 16, 8} that sums to **≤ 512 bits**, with
**no cap on the number of values** — the bit budget is the only thing invariant 2 actually states.
That universe is **17,361** (the empty multiset included, as in every figure the record already
carries). Then ask, **of those, how many each scheme can place**, with each scheme's own name cap
applied *inside* its own row, where it belongs.

| scheme | Q2 | places, of 17,361 | share | factor lost |
|---|---|---|---|---|
| **the free map (B1, F2), two namespaces** | **no** | **17,360** | **99.994%** | 1.00× |
| **A2, exact per-instant scratch rule** | either | **≤ 17,360** *(upper bound — see below)* | ≤ 99.994% | ≥ 1.00× |
| **A2, closed-form scratch rule** *(the one the tool should use)* | either | **13,809** | **79.5%** | **1.26×** |
| **the free map (B1, F2), one namespace** | **yes** | **13,091** | 75.4% | 1.33× |
| three complete tiers — 8/16/32 names at 64/32/16 | **no** | **9,165** | 52.8% | 1.89× |
| A + a narrow subtree carved out of `w15` (§7.4) | yes | **4,233** | 24.4% | 4.10× |
| **B2, `K` = 4 classes, two namespaces** | no | **4,175** | 24.0% | 4.16× |
| **B2, `K` = 4 classes, one namespace** *(the recommended pairing)* | **yes** | **4,143** | 23.9% | 4.19× |
| **Design A** | **yes** | **2,685** | **15.5%** | **6.47×** |

**[all recomputed]** — enumeration over `(a,b,c,d)` with `64a+32b+16c+8d ≤ 512`. Each row's rule:
**A** applies `a ≤ 8`, `b+c+d ≤ 16`, `64a+32(b+c+d) ≤ 512` — ~~the charge-32 rule~~ **a
DIRECT-NAMING rule** `[corrected - user ruling 2026-09-03 (liveness)]`; A's *admission* rule is
`Σ w_i + scratch ≤ 512` like every other row's. **A2 closed form**
applies `64a+32b+16c+8d ≤ 512` when `c+d = 0` and `≤ 480` otherwise, the 32 being §3A.5's extent
scratch name, **with no name cap, because an extent is named by an immediate and is not a register
name at all** — which is why A2's row was understated by 2,739 under the old ≤ 31 cap. **B1** applies
only its name cap: **63** names under Q2 = no, **31** under Q2 = yes. **The three-tier and B2 rows**
charge each value up to the narrowest name width the scheme provides that is ≥ its own, count names
per tier, and require the charged bits to fit 512. **The enumeration reproduces four figures the
record already carries — 13,091 for the ≤ 31-name sub-universe, 2,685 for A, 9,132 for the
three-tier variant *under the old ≤ 31 cap* (9,165 without it), and 4,233 for §7.4's subtree
variant** — which is why the rows computed alongside them are trusted. **The one figure it does not
reproduce is B2's 4,340**, which comes out at **4,175** (two namespaces) or **4,143** (one) under the
convention that reproduces the other four; 4,340 is not obtainable under any of the three placement
conventions tried and is withdrawn.

> **~~THE ONE MULTISET NOBODY PLACES~~ — EVERY SCHEME PLACES IT.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` `{64 × 8 bits}` —
> **I2's "64 1-byte regs" exactly** — is 512 bits of liveness and is **expressible under design A**:
> eight bytes packed into each of the eight 64-bit names, read and written by shift-and-mask through
> a scratch name. What no scheme has is **64 direct names** (B1's free map has 63, a register-field
> scheme 63 or 31 by §2.2), so those accesses cost ~2-3 ops each rather than one. The one genuine
> tension at exactly 512 of 512 is **staging room**: a schedule that is full to the last bit at the
> instant it needs to shift has nowhere to stage, which is a **scratch-accounting** obligation on
> the compiler (and A2's 32-bit extent scratch is the same obligation in another form) — not a
> capability cap. **The honest form of the "byte-tier cap": there is no cap; there is a naming
> shortfall that costs instructions, and a scratch requirement that admission must count.**

> **A2's exact row is an upper bound, not an enumeration.** The exact rule is *"live bits at instant
> `t`, plus 32 if an extent access occurs at `t`, ≤ 512"* — a predicate on a liveness schedule, not
> on a multiset, so it cannot be counted in this universe without a program. It is bounded above by
> 17,360 (the 64-byte case is excluded as above) and below by the closed form's 13,809. **Any number
> quoted between those two is a guess and this document does not quote one.**

> **What was wrong with the number you would otherwise have ruled on.** `final-A-aliasing.md` §2.3
> tabulates **81** for design A against **13,091** and calls it *"a factor of 162… stated in the
> strongest form available rather than the flattering one."* The rows are not commensurable: **81
> counts (a, b) pairs over a two-width alphabet {64, 32}**; **13,091 counts multisets over a
> four-width alphabet.** The document's own §2.2 explains why they cannot be compared — a narrow
> value is **charged 32 and still runs** — so `{7×64, 1×8}` charges 480 and is admitted, and the 81
> **[AND ALL OF IT IS NOW SUPERSEDED: there is no charge-32 rule, `{7×64, 1×8}` is 456 bits, and
> neither 81 nor 2,685 measures capability — user ruling 2026-09-03 (liveness).]**
> never counted it. The same defect runs through the other rows: **969 reproduces exactly as tuples
> `(a,b,c)` over {64,32,16}**, and **657 reproduces exactly as `81 + 8 × 72`** — the 81 pairs plus,
> for each pair having at least one `w` tile, the eight non-empty narrow sub-multisets. All three are
> alphabet-local tuple counts. **[all recomputed]**
>
> **[STRUCK - user ruling 2026-09-03 (liveness): A's admission loss is ZERO; the figures below are
> directness.]** **A's ~~loss~~ *directness shortfall* is 79.5% against a one-namespace free map (2,685 of 13,091), or 84.5% against the full
> 17,361-multiset universe — not 99.4%.** Overstating it by 33× is the one way this document could
> have produced a wrong ruling, because it is exactly the size of error that pushes a reader back onto
> Design B — the map already called foolish. **Both denominators are given because neither is
> obviously the right one:** 13,091 is what B1 reaches under the same Q2 ruling A is recommended
> with, and 17,361 is everything invariant 2's bit budget permits.

**What the count does not capture, and must be said beside it.** Non-power-of-two widths are outside
the universe entirely. **Nine 48-bit values are 432 bits of data; A needs nine 64-bit tiles and has
eight, so it rejects them. F2 rounds 48 to 64 and rejects them too. Only F0 places them exactly** —
which is why F0-versus-F2 is a real choice inside B and not a formatting detail.

### 5.3 Performance impact

| axis | **A** | **A2** | **B2** | **B1b** |
|---|---|---|---|---|
| **decode latency** | **+0 cycles** — ≈8 gates one level deep, in parallel with opcode decode | **as A**; one added opcode case | +1 mux level | +0 if resolved a window early; 3 levels otherwise |
| **register read latency** | **+2 mux levels `[ASSUMPTION — see below]`** (8:1 word select → 2:1 half select on `offset[5]` → 2:1 extension select) | **as A** on the named path. The extent unit is a **separate execute-stage read port**, +1 level on its own path only | same | same |
| register write latency | **+0** — write enables off the same decode, **no read-modify-write** | **+0, and still no read-modify-write** — enable granularity 32 → 8 bits, which is why `wcode` stops at a byte (§3A.6) | +0 | +0 |
| **memory references to decode a register name** | **ZERO** | **ZERO** | **ZERO** | **zero in steady state**; `N × F` fills for a whole program |
| **post-migration fetch** | **NONE.** A context arriving on a tile it has never visited is immediately executable | **NONE** — nothing is carried and nothing is retrieved | **NONE** (class in the envelope) | RFT hit: 0 accesses. Miss: 1–2 local lines + a ≤64-cycle verify, off the issue path |
| **migration payload** | **72 B exactly** | **72 B exactly, and 0 bits of envelope** | **72 B** (2 bits of envelope) | **72 B** under ID-1 |
| barrel depth `Dp`, hence `C ≥ W(Dp + L/I)` | **unchanged** — the extra mux levels sit inside a stage that already exists | unchanged | unchanged | unchanged; honest worst case **132 → 136** (+4 contexts, +348 B) if the lookup does not fit |
| hazard / forwarding | **+0 on the canon core** (CANON.md:474-478); on a non-barrel core, the lane+mask comparator and a **32-bit-granular bypass merge**, priced in §3.5's amendment and deliberately not in this table because this table describes the canon core | **as A** — an extent's destination is an ordinary name, so nothing new reaches the scoreboard or the bypass | same as A | **+0, and B dissolves A's overlapping-name hazard class entirely** — a credit B never claims |
| **dynamic instructions on the body** | **+0 for every 64- and 32-bit value; ~2-3 per access to a packed sub-32-bit value** (shift-and-mask through a scratch name) `[corrected - user ruling 2026-09-03 (liveness)]` | **+0 for every 64- and 32-bit value.** For a packed sub-32-bit value, **+1 per read and +1 per write, hoisted to live-range boundaries** — i.e. it buys back A's ~2-3, on functions design A runs perfectly well (§3A.3–3A.4) ~~and paid only by functions design A rejects outright~~ | +0 | +0 |
| host packing per `FORK`, 8 × 32-bit args | **20 instructions** (4 `CXW` + 16 shift/or) against 8 `CXW` for eight 64-bit: **+12**, or **+8** with `Zbb`. 12.8 bits/instruction packed against 64 unpacked | **same as A — extent instructions are tile-only and do not reach the host's staging** (§3A.2) | same | same, **plus** the offsets are per-function data, so a host `FORK` site must bake in the callee's map or read it at run time |
| **admission loss** `[CORRECTED - user ruling 2026-09-03 (liveness)]`: **ZERO in every column — all four hold 512 bits and express every width.** The figures below are **DIRECT-NAMING** counts *(of 17,361; §5.2)*, i.e. how often an access is one instruction instead of ~2-3 | **2,685 — factor 6.47 on directness** | **13,809 — factor 1.26** (closed form; ≤ 17,360 exact) | **4,143** one namespace / 4,175 two — factor ≈ 4.2 | **17,360 under Q2 = no, 13,091 under Q2 = yes — factor 1.00 / 1.33** |

**The shape of A's trade in one line: every cost is paid once, at compile time or in the caller's
frame, except the ~2-3 ops per access to a value packed below a name** `[corrected - user ruling
2026-09-03 (liveness)]`. **A2's is the same line with one clause added: *it buys those accesses down
to one instruction* — ~~except for functions design A cannot run at all~~, which is struck: **there
are no such functions.**

> **`[ASSUMPTION]` — THE ONE LABEL THIS TABLE NEEDS, AND IT IS NOT A FOOTNOTE.** *"+2 mux levels,
> +0 cycles"* is **an assumption, not a result.** There is no fmax, no process node, no FO4 figure,
> and **no demonstration anywhere in the record that the register-read stage has two levels of
> slack.** The two levels sit **after** the context SRAM output, on the data path into the ALU —
> conventionally the tight path, not a slack one. **Every zero in the table except the migration
> payload, the memory-reference count and the dynamic-instruction row inherits this assumption**,
> and it is shared by A, A2, B2 and B1 alike, so it does not separate them — **but it does mean the
> table's "+0 cycles" is a claim about a machine nobody has timed.** The honest form of the row is
> *"+2 mux levels, believed to fit"*; if it does not fit, the register-read stage splits and `Dp`
> grows by one, which by H.2's floor `C ≥ W(Dp + L/I)` costs **+4 contexts**, the same order as
> B1's worst case already in the table. **A timing budget is the single cheapest measurement that
> would retire this label, and nothing in the record contains one.**
>
> **A second honesty note, also against A:** the **bypass merge** is priced in §3.5's amendment and
> is deliberately absent from this table, which describes the canon barrel core where it is zero.
>
> **And one against B**, which the source document itself states and which should not be lost: the
> ≤64-cycle fill verify sits on the **arriving context's** own critical path while being described
> as *"off the issue path"*. Both are true; only the second is in the table.

### 5.4 The measured cases, and what they do not settle

| function | measured | under A | under A2 | under B |
|---|---|---|---|---|
| `nmfc_bu` | 8 values, **480 bits** | *a*=7, *b*=1 → 448 + 32 = 480 ≤ 512 — **admissible**, one `w` tile spare | **identical to A. Zero extent instructions emitted** — no value is narrower than 32 bits, so nothing is packed | admissible |
| `nmfc_expand` | 8 values, **384 bits** | *a*=4, *b*=4 → 256 + 128 = 384 ≤ 512 — **admissible**, 128 bits spare | **identical to A. Zero extent instructions emitted** | admissible |

> **[READ THIS TABLE WITH §3.8's CORRECTION IN HAND.]** Both decompositions came from `annotate`,
> whose width lambda (`annotate.cc:461-470`, executed) charges **16 bits for any two-character ABI
> name** — `a0`, `t1`, `s2` — 128 for `x10`+ and 512 for `zero`. **It cannot see widths.** So the
> "8 values, 480 bits" and "8 values, 384 bits" rows **are not currently reproducible and must be
> re-measured** once the opcode→width table (§9.1 U2) lands. They are used here because they are what
> the record holds, and they are labelled because they are the evidence Q1 and Q5 are deferred on.

**cons-C21 is met by every candidate.** But **neither decomposition shows a sub-32-bit value**, so
**nothing in the record measures how often real code touches a sub-name value** — the ~2-3 ops per
packed access `[corrected - user ruling 2026-09-03 (liveness)]`; ~~the cost of A's charge-32 rule~~
is struck, since no such rule exists — which is precisely the measurement that would decide Q1,
**and now Q5 as well. And because the tool cannot see widths,
the absence of narrow values in these two rows is not evidence that they contain none.** The two functions are also the
strongest evidence *for* A2's composition claim and the strongest evidence *against* building it
yet: **A2 is bit-for-bit design A on both**, which is the point of §3A.3 — and also means the record
contains no case in which A2 would emit a single instruction. And both decompositions must be
re-measured once `annotate` has a working width input (§3.8).

### 5.5 Overall simplicity

**A's real claim, and it is true: state outside the instruction, none. A2 makes the same claim
without an asterisk**, which is worth seeing side by side.

| kind of state | **A** | **A2** |
|---|---|---|
| per context | **0 bits added** | **0 bits added** |
| **per function** | **0 bits. There is no per-function object of any kind** | **0 bits. Same** |
| per tile | **310 bits** of decode ROM — 39 B against 64 KiB of context state, **0.06%** | **310 bits. The extent descriptor is decoded arithmetically (`index << (wcode+3)`), so it adds no ROM at all** |
| fetched at decode / carried on migration / cached per core / needing an identity check on reuse | **nothing / nothing / nothing / nothing** | **nothing / nothing / nothing / nothing** |
| **where the geometry below 32 bits is written** | **in the compiler's packing plan, and in the shift-and-mask sequences it emits** `[corrected - user ruling 2026-09-03 (liveness)]` — ~~nowhere, and the byte tier is unreachable~~ is struck: **the byte tier is reachable** (§2.2), at ~2-3 ops per access, and the geometry lives in the emitted code rather than in any table | **in the instruction's own immediate**, which is the second of the ruling's three objects and was always going to be referenced |

**What a reader does not have to reason about under A:** no map lifetime; no cache coherence for a
map; no handle allocation, reuse, or handle→address translation; no first-visit fetch; no cross-page
reload; no port width on a map cache and no banking-per-width forced by one; no interaction between
a map's residency and a context's migration; no question of what a `CONT` successor's map is.
**None of those objects exist.** That list is the payoff of your ruling, and it is long.

**Where A is not simple, stated against itself.** The *mechanism* is simple; the *specification* is
not. To implement correctly an engineer holds: one sentence of geometry, one line of decode, three
width rules (W1/W2/W3 — **four before W1b was struck**), seven legality rules including an
operand-class column total over `RV64IMAFD` (**and it is four operand slots, not three — `fmadd`,
`fmsub`, `fnmadd` and `fnmsub` have an `rs3`, so the check is 8 bits per opcode-table row, not 6**),
three admission tests one of which is a documented heuristic, and **nine deviations from the
ratified manual**. FLEN becomes per-instruction, which no shipping ISA does. It is structurally
simple and semantically dense, and both halves should be said.

**Two things A gives up that must appear here rather than in a footnote:**

1. **It loses a run-time safety property — this is SW1 (§3.6a), a silent-wrong-answer class and not
   a trap.** `RegLayout::illegal()` fires today; a total map leaves it nothing to fire on, so
   over-liveness becomes a silent wrong answer. The instance usually cited is **`d0`**: under the
   stock ABI mapping, `s0` = `x8` = `d0` reads as scratch to every allocator and is **fully occupied
   whenever a seventh or eighth integer argument is passed** in `a6`/`a7` = `w0`/`w1`. No decode check
   can see it. **Three qualifications, all of which weaken the charge and none of which removes it:**
   **B1 does not catch it either** (§4.4), so SW1 is not a reason to prefer any candidate over any
   other; **that instance is unreachable on both toolchain paths §3.7 specifies** (§3.6a's amendment —
   the day-one path fixes `a6`/`a7` out, and the `W`-tier path declares the sub-register relation a
   stock allocator honours); and **§5.1's allocator row is right that a stock LLVM/GCC allocator with
   `SubRegIndices` declared surfaces over-liveness as an ordinary allocation failure.** What survives
   is the class itself, for hand-written bodies, for the admission tool's **own** FFD placement, and
   for any back end that omits the relation — **which is a reason to build the placement verifier
   (§6.2, ~40 lines, owner undecided, §9.1 U3), not a reason to choose a different design.**
2. **It gains two silent host/tile divergences no check can see.** `rm = DYN` is RNE on the tile and
   reads `fcsr.frm` on the host; NaN-boxing is abolished on the tile and honoured on the host, so a
   non-canonically-boxed `f32` is a valid operand on one and canonical NaN on the other. **The
   proposed remedy — reject any function reaching `fesetround`/`fegetround`, or any unit compiled
   `-frounding-math` — does not cover its own failure story**: the divergence is caused by the
   **caller's** `fcsr.frm`, set in another translation unit, which `annotate` never walks. That is
   Q3, and it is your call.

**B1b's simplicity, by its own table, is the worst of the four**, and its own §13 says so. **B2's
sits almost exactly where A's does** — 2,520 bits of ROM against 310, one more mux level, one new
failure mode, and none of B1's caching machinery.

**A2's is the interesting one, and it splits.** *Structurally* it is **simpler than B2**: no ROM at
all, no per-context bit, no envelope bit, no mux level on any path a design-A program uses, and one
fewer new run-time failure mode. *As a specification* it is the most complex of the four: on top of
A's *"one sentence of geometry, one line of decode, three width rules, seven legality rules, three
admission tests, nine deviations"* it adds **two instructions, an encoding, three more legality
rules, a tenth deviation, and one more admission clause** — and, uniquely, **it cannot be built at
all without reopening two tier-1 decisions.** *(Under A2-r, §3A.7: one instruction, two legality
rules, and the write path untouched.)* **The engineer's job is bigger; the machine's is not.** Both
halves are the honest answer, and Q5 is which one you are weighing.

### 5.6 Every correction this document makes, in one place

**Corrections 1–16 are against the two long-form source documents; corrections 17–26 are against
this file's own earlier revisions.** Six of 1–16 and five of 17–26 change a number the reader is
asked to rule on.

| # | where | correction |
|---|---|---|
| 1 | A §2.3, §11, §13.2, QA | ~~**"factor 162" is a category error.** Like for like: 2,685 of 13,091, factor 4.9 — or 2,685 of 17,361, factor 6.47.~~ **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** — **the correction is now larger: there is no expressiveness loss at all.** 81, 657, 969, 2,685, 13,091 and 17,361 all count **directly nameable** width-multisets; every scheme holds 512 bits and expresses every width, so **no ratio between them measures capability.** A pays ~2-3 ops per packed access; B1 pays a map lookup |
| 2 | A §3, §4 r4/r5, §10.5, dev. 7 | **W1b is struck.** Not expressible in a standard back end; redundant; and it created SW2. **The redundancy argument an earlier revision gave was FALSE and is replaced** — see correction 17. Deletes the whole "not free" execution-unit section |
| 3 | A §10.7, §13.9 | **`rv64ima_zfinx_zdinx`, not `rv64ima`,** is the day-one spelling. One namespace, so `-ffixed-x`*n* reserves `f`*n* too; the day-one path gains floating point |
| 4 | A §7.2, Q8 | `annotate.cc:461-470` **does not charge 64 on RISC-V** — it charges **16** for two-character ABI names, 128 for `x10`+, 512 for `zero`. **Permissive by 4× on the commonest spelling** |
| 5 | A §10.9, §5.2; B §4.3, §6.3 | **DESIGN.md line numbers.** §25.7 is **D:2417-2442**; the per-function-layout passage is **D:2425-2429** (not D:2560-2567, which is §26.1 `.rodata`); the 72-byte sentence is **D:2439-2441** (not D:2574-2577); the 87-byte figure is **D:2434-2437**. Also `c.pc = mig.pc` is at **`NMFCTile.cc:1385`**, not `:1384`; and `RegLayout` spans **`NMFCRegLayout.h:39-73`** with `Context512` at **`:84-118`** |
| 6 | A §10.3 vs §5.3 | **"No partial-register hazard" contradicts "a byte-masked merge."** Write port +0; **bypass network is a real cost on any non-barrel core** and is missing from the table |
| 7 | A §10.1 | The operand-width check is **4 slots, 8 bits** per opcode-table row, not 3 and 6 — `fmadd` has an `rs3` |
| 8 | A §7.3 | Relocation repairs every gap **only while the live sum is strictly below 512**; at exactly 512 no free tile exists to move into |
| 9 | B §3.4, §4.3, §10.3 | **P1+P2 as recommended is unsound.** Page-alignment alone breaks any function > 1 page; a shared superset breaks separate compilation and diverges silently across a migration. The sound form repeats the exact map at **every** page, at 40–76 B × `N` |
| 10 | B §4.3, §4.4, Q5 | **The RFT range has no extent field** in the specified header, so it matches ≈19 instructions; and **"refcount and refuse" is hold-and-wait**, which I.1 forbids by name and which collides with `CONT`-cannot-fail. **Evict-and-broadcast instead** |
| 11 | B §2.5, §14.1, §14.2, Q9 | **B restores half of cons-C14** — the undefined-name trap. **Over-liveness stays silent**, because a bit-exhausted allocator emits a legal non-overlapping map |
| 12 | this document, §2.3 | **The extent family was rejected on two numbers that were wrong.** *"15 immediate bits, does not fit an existing format"* — false under natural alignment, where the descriptor is **9 bits with 3 to spare** (§3A.2). *"~3× the dynamic instruction count on the body"* — false when composed with the Heap Rule, where 64- and 32-bit values are names and pay **zero** (§3A.4). **The family is promoted to design A2 (§3A) and scored at 6.7 against A's 7.3.** What survives of the rejection is a tier-1 ruling, asked as **Q5** |
| 13 | §0, A §13.1 | ~~**The byte-tier loss is attributed to the wrong thing** … it is a property of the name-denotes-slice family~~ **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** — **there is no byte-tier loss to attribute.** Design A reaches the byte tier by packing (~2-3 ops per access); A2 reaches it in one instruction. What the name-denotes-slice family fixes is **direct nameability**, i.e. instruction count, binding A, B2 and B1's free map alike (§2.2) |
| 14 | A §10.3 vs §5.3, refining correction 6 | **The bypass merge granularity is 32 bits, not 8.** §5.3's *"eight 2:1 byte muxes"* over-states it by 4×: no value smaller than a `w` tile exists to forward. Priced in §3.5 — **+0 mux bits, ×2 select logic, and one structural cost (a 64-bit read whose halves come from two different producers)**, all zero on the canon barrel core |
| 15 | §3.6, §5.5, §6.4 | **The two silent-wrong-answer classes are now named SW1 and SW2 (§3.6a) and are no longer called "traps."** Nothing fires on either. **SW1 is live under all four candidates, B1 included** — the earlier text let *"the trap, as a rule"* read as if the machine caught it |
| 16 | §5.3 | **"+2 mux levels, +0 cycles" is labelled `[ASSUMPTION]` in the table itself**, not only in a note. No fmax, no node, no FO4, no slack demonstration exists in the record; every zero in the table but three inherits it. It does not separate the candidates, but it is a claim about a machine nobody has timed. **Now also carried in §0's "what you must acknowledge", which §5.3 asked for and §0 did not do** |
| **17** | §3.3 (ii), and correction 2 | **The correctness leg of striking W1b was false as stated, and it was presented as a measurement.** *"Computing at 64 and truncating gives exactly the 32-bit answer for `add`, `slt`, `sltu`, `div`, `divu`, `rem`, `remu`… the only divergence is `mulh*`"* — **re-enumerated over 200,169 pairs per opcode: `divu` (49,857), `remu` (49,845), `sll` (96,941), `srl` (142,205), `sra` (93,753) and `mulhsu` all diverge**, and `divu`/`remu` do so on exactly the pairs whose dividend has bit 31 set and whose divisor does not (predicate verified, 0 mismatches in 400,000). Counterexample `0xC386BBC4 ÷ 0x1027C4D1`: `0xC` against `0xD89D1498`. **This is why RV64 has `*W` opcodes at all.** The strike still stands, on a **different and stronger** argument: after it, the tile computes what a stock RV64 core computes on the values W2 presents. **§3.4 gains rule 7** so the residual case fails closed instead of relying on codegen **[recomputed]** |
| **18** | §3.1, §3.2, §9 Step 1 | **The published decode made `x0` illegal.** `legal = n[4] OR n[3]` evaluates to 0 for `n` = 0, contradicting §3.4's `x0`/`f0` exemption, which §3.4 itself calls *"load-bearing"*. Corrected to `legal = n[4] OR n[3] OR zero`; **≈7 gates becomes ≈8** |
| **19** | §3.3 W1, §3.4 | **W1's load/store data row broke stock `int` codegen.** Exempting only `lb`/`lh`/`lbu`/`lhu` made **`lw`/`lwu` into a `d` name illegal**, contradicting §3.4's *"this is why stock `int` codegen works under register-class assignment alone"* and hiding an `mv` per load that no table carried. **Loads may now target any name (W3 does the extension — ratified RV64 behaviour); a store's source must be at least the opcode's width, so `sd w3` stays illegal** |
| **20** | §3.4 rule 2 | **The worked example named an instruction the recommended subset deletes.** `fmv.d.x w0, d1` does not exist under `rv64ima_zfinx_zdinx` — Zdinx removes the `fmv.d` transfers as Zfinx removes the `fmv.w` ones (fact-C15). Replaced with `fadd.d w0, w1, w2` and `fcvt.s.w d1, w3` |
| **21** | §3.7, §6.8, §3A.8, §6.3, Q2 | **The document adopted Zfinx semantics while asserting O4 stood unamended, which facts §6.1 records as impossible.** `RV64IMAFD` becomes **`RV64IMA_Zfinx_Zdinx`** under the recommended Q2(a); O4's substance is untouched, its spelling is a **tier-1 supersession** and is now item (v) in §6.3's ledger. Also: **"Zdinx adds double-width aligned pairs" is false at XLEN = 64** — fact-C16 scopes that rule to RV32 |
| **22** | §6.3, §1.5, §3A.8, §9 Step 8 | **The supersession ledger was short by three and named its one entry by a wrong line number.** Design A supersedes **five** things, not two: DESIGN §25.7 D:2425-2429; CANON.md:9849; the **O4 ruling row at CANON.md:127**; **I.0's four-point answer at CANON.md:6029-6060**, whose point 1 states the *"per-function binding… carried with the offload"* that design A **deletes outright**; and **O4's spelling** |
| **23** | §5.2, §0, §10 Q1, §3A.5, §5.3 | **[NOW ITSELF SUPERSEDED - user ruling 2026-09-03 (liveness): the table measures DIRECT NAMING, and there is no expressiveness cost to make commensurable — every scheme holds 512 bits.]** **The expressiveness table was not commensurable — the defect §5.2 exists to correct.** A's 2,685 was computed under Q2 = yes and B2's 4,340 under Q2 = no; the universe was capped at **≤ 31 names**, which is *A's own cap*, so B1 scored 100% of its competitor's universe, B2's 36-name class 3 was scored under a cap it exceeds, and **I2's literal "64 1-byte regs" was excluded from the universe outright.** Rebuilt on the bit budget alone: **universe 17,361**; A **2,685**; B2 **4,143**/**4,175**; A2 closed form **13,809** (it has no name cap — an extent is named by an immediate); B1 **13,091**/**17,360**. **B2's 4,340 is not reproducible under any convention that reproduces the other four anchors and is withdrawn.** **The one multiset nobody places is `{64 × 8}` — I2's literal case [all recomputed]** |
| **24** | §0, §5.1, §3.6a, §5.5 | **§5.1 and §3.6a gave opposite answers on whether an allocator detects SW1, and the flagship instance is unreachable on both specified toolchain paths.** Resolved: a stock allocator **with `SubRegIndices` declared** does surface it as an allocation failure; the day-one `-ffixed` path cannot reach the `a6`/`a7` case at all. **SW1 remains real for hand-written bodies, the tool's own FFD placement, and any back end omitting the relation** — and it is now in §0's acknowledge list, which §5.5 had asked for |
| **25** | §0, §9.1 | **The unresolved build items were spread through prose inside quoted line estimates.** Collected in **§9.1**: the two unwritten opcode tables (U1, U2), the undecided owner of the placement pass (U3), the uncosted no-spill gate (U4), the unspecified tile ABI (U5), the ChampSim scoreboard with no build order (U6), and the missing timing budget (U7). **`readReg(…, Role)` cannot compute W1's width from a role and gains the opcode's required width** (§9 Step 3) |
| **26** | CANON citations throughout | **Fifteen CANON.md line citations were wrong, by ~20–30 lines each, and none had been audited** — unlike the DESIGN.md citations, which correction 5 checked. All corrected against a 10,372-line CANON.md and listed below |

**The CANON.md citation audit (correction 26), in full.** Every citation in this file was checked
against CANON.md at 10,372 lines. Fifteen were wrong; the rest verified. Corrected:

| claim | was | is |
|---|---|---|
| #238 *"the context is not 8 regs"* | 5220-5226 | **814-815** (and 5244-5245) |
| I2 *"64 1-byte regs, or ANY combination"* | 5215-5233 | **809-816** |
| *"`CONT` … CANNOT FAIL"* (×3) | 6223-6231 | **6243-6250** |
| *"the namespaces do not alias"* (×5) | 9819 | **9849** |
| ruling O4 | 9818 | **9849** (body) and **127** (the ruling row) |
| 196,904 migrations for 262,143 loads | 4957 | **4977** |
| *"these are settled numbers"* | 4945-4960 | **4972-4978** |
| 100.00% fc I-cache hit (×2) | 8247 | **8267** |
| *"own an edge range and chase scattered vertex values"* | 8703 | **8723** |
| I.1's blocking-instruction / 9,100,426 passage | 6060-6063 | **6080-6081** |
| BTB *"Shared, not per-context"* (×2) | 5366-5372 | **5386-5392** |
| H.5 *"at the end of each dispatch"* | 5341-5345 | **5362** |
| *"being wrong is free"* | 5359-5364 | **5383** |
| BTB *"a fetch address a window earlier"* | 5361 | **5380** |
| #239 *"1 outstanding miss"* | 5299-5320 | **5323-5324** |
| #78 *"are you serious?"* | 2992 (which is user **#77**) | **3011-3013** (and the summary row at 5256) |
| I11's parity sentence (×2) | 1331-1332 | **1331-1334** |

**Verified correct and unchanged:** 203, 474-478, 1109-1115, 1335-1337, 2113, 6029-6060, 6630-6640,
6719, 7750, 8801.

---

## §6 INTERACTION WITH EVERY EXISTING MECHANISM

The fit-list from `register-map-context.md` §9, answered for the recommended design (A) with B's
answer beside it where they differ.

### 6.1 `CXW`/`CXR` — the host-side aperture (**M4**, **M12**, cons-C25, cons-C27, cons-C28)

`CXW`/`CXR` move **one 64-bit lane**, the lane number in `funct7[3:1]`
(`nmfc_isa.h:101-102`, `NMFC_CX_LANE_SHIFT 1`, `NMFC_CX_LANE_MASK 0x7`). Under design A:

> **Lane *k* is exactly tile `d`*k* = `x`*(8+k)*, and its halves are `w`*2k* and `w`*2k+1*.**

**Neither moved to make this so.** Because no tile straddles a lane, **every named value is
reachable in exactly one `CXR`** — the two-`CXR`-and-splice case an arbitrary bit-packed map would
require cannot arise. `NMFC_CX_LANE_SHIFT`/`MASK` keep their values; cons-C27 and cons-C28 stand
unamended. (Under B's F2 this also holds; **under F0 it does not** — an unaligned 64-bit value spans
two lanes and costs two `CXR`s and a splice on the host.)

**The staging idiom, and the trap in it.** Build the lane, then write it once. Only the **low** half
needs zero-extending, because the high half's upper bits are shifted out — and a 32-bit argument
arrives in `a0` **sign**-extended (Ch. 5), so the naive `slli t0,a1,32 ; add t0,t0,a0` propagates
ones and a carry across bits 63:32 and **destroys `w`*2k+1*** whenever bit 31 is set.

```
    slli  t0, a0, 32
    srli  t0, t0, 32            # zero-extend the LOW argument   (Zbb: zext.w t0, a0)
    slli  t1, a1, 32            # high argument; bits 63:32 discarded by the shift
    or    t0, t0, t1            # { w_2k+1 : w_2k }
    CXW   c1, k, t0
```

**Cost:** 1 instruction per 64-bit argument; **5 per lane of two 32-bit arguments** (4 with `Zbb`);
**+12 instructions per `FORK` in the worst realistic case**, paid once, in the caller's frame.
Read-back is 1 `CXR` for a 64-bit result, 2 for a signed 32-bit, 3 for an unsigned 32-bit (2 with
`Zbb`), +1 to land it in an `f` register. **The rule for the ABI, because the 7-instruction
read-modify-write case is avoidable: stage a lane's two halves together, and never patch one half of
a live lane.**

**Per bit moved, packing is a tax and the record must say so:** eight 64-bit arguments move 512 bits
in 8 instructions (64 bits/instruction); eight 32-bit arguments move 256 bits in 20 (12.8
bits/instruction). *(`register-map.md` §4.3's contrary conclusion was an artefact of the missing
zero-extension and is withdrawn.)* **The comparison that matters is not packed-versus-unpacked
staging — it is offloading versus not being able to.**

**Where the map lives on the host: nowhere referenced.** Under A the offsets appear in the caller's
code as immediates in `slli`/`srli`, exactly as a struct field offset does. **Under B they do not:
argument and return offsets become per-function data, so a host `FORK` site must either bake in the
callee's map — cross-module coupling, no separate compilation of host and offload, no recompiling a
callee without recompiling every caller — or read the map at run time and pack generically through
an aperture that moves one lane per instruction.** `final-B-context-map.md` §9.4 M8 scores this as
*"B improves the host's half"*; the coupling is the other side of it and appears in none of its cost
tables.

### 6.2 The admission tool (**M7**)

`tools/nmfc/annotate.cc` today builds a pool of `opt.num_regs` (8) **slot ids**, allocates one whole
slot per live value, and `die()`s when the pool empties (`:524-559`); it computes
`bits += reg_bits[reg] != 0 ? reg_bits[reg] : 64U;` at `:555-559` and **throws the number away on a
stderr line at `:927-928`**. K.6 already requires the rewrite. Under A:

| item | what it is | estimate |
|---|---|---|
| replace the slot pool with a width classifier | `width_of` at `:461-470` → a **~150-entry RISC-V opcode→width table** (§3.8). **The single largest tooling item, and K.6 requires it anyway** | ~150 entries |
| make the bits figure the gate | `:555-559` already computes it; `:927` discards it | ~10 lines |
| FFD placement + relocation | sort live values by width at each birth, place at the lowest free aligned offset, emit `mv` relocations | ~120 lines |
| **placement disjointness verifier** | every pair of simultaneously-live values on disjoint bit ranges; every value on one name for its whole live range or explicitly moved. **This is the step that makes the tool sound (§3.6) and it is the only thing standing between an allocator bug and a silent wrong answer** | ~40 lines |
| `ret` → `END`/`RETC` rewrite + terminator check | it already walks per function | ~30 lines |
| **total** | | **~350 lines in one file, plus the opcode table** |

**Three obligations the estimate does not contain and must (§9.1 U1–U4):** the **~150-entry
opcode→width table** in the first row is **not written anywhere**, and it is the row where
over-permissiveness is fatal under cons-C15; the **relocation emitter** — `annotate` is a *trace
annotator* whose header says *"Nothing here invents an instruction, a program counter or a register"*
and whose only output is a binary trace (`:339`), **so it cannot emit `mv` relocations as it stands**,
and §9 Step 6.3 does not decide whether it grows one or the placement moves to the back end; and the
**no-spill post-RA gate** (§3.7). Under B the placement *is* the output
rather than an internal artefact, which makes cons-C20 trivially checkable — the evidence is a table
in the binary — and that is a genuine advantage of B.

### 6.3 The resident-function table (**M1**)

`NMFCTile.h:448-450`, verbatim: *"One entry per resident function (§25.7). There is one until a
compiler emits layouts; the lookup exists so that adding more changes nothing else."*

**Under A it is DELETED**, and with it: 768 bits per resident function beside the I-cache, the lookup
at every register access, the table's population path, **the successor-inherits-a-layout problem
under `CONT`/`CONT.M` (M10 — a successor running a different function needed a different entry and a
way to know which; the problem disappears rather than being solved)**, and `RegLayout::illegal()`'s
run-time trap, **which is a loss**.

**Under B1 it is KEPT and completed** — the RFT *is* DESIGN §25.7's *"one small table entry beside
the instruction cache, indexed by the function a context is running"*, and what the record lacked
(an identity tag, a fill path, a migration story, a cost) is supplied. **DESIGN §25.7 is not
superseded under B; it is finished.**

**cons-C31: under A, two tier-1 supersessions must be MARKED, not quietly dropped.**
(i) **DESIGN §25.7 D:2425-2429** — the per-function-layout paragraph. (ii) **CANON.md:9849 — "The
namespaces do not alias"**, verbatim: *"the compiler binds every simultaneously-live `f`- or
`x`-name to a **disjoint bit range**, so `f3` and `x3` are different names at different offsets, not
one slot."* Design A makes `f`*n* ≡ `x`*n* and contradicts the second clause. **The first half — 512
bits of live storage, not 64 architectural slots — is preserved and strengthened.** The reason it
can be superseded is itself a tier-1 ruling: that sentence describes *the compiler doing the
binding*, and the 2026-09-03 ruling took the binding away from the compiler. **This is a consequence
of Q2(a); if you rule Q2(b), it does not arise.**

### 6.4 The `END` return bit and the calling convention (**M8**, **M9**)

> **The ISA fixes the GEOMETRY — which bits a name denotes. It does not fix the ASSIGNMENT — which
> value a function put in which name.** That stays the function's own ABI, known to its caller.
> **I2 is preserved literally: register positions carry no architectural meaning across the
> boundary**, and all 512 bits come back whole and uninterpreted.

`JOIN` as a read-modify-write try (`cDST_new = ok ? ftu_payload : cDST_old`) moves 512 bits and never
inspects them — **unaffected**.

What genuinely changes is that caller and callee must now agree on **width** as well as position, so
a convention is published with the function. Read through §3.1, the stock RV64 ABI gives:

| role | RV64 ABI | this map | lane |
|---|---|---|---|
| integer/pointer arguments 1–6 | `a0`–`a5` = `x10`–`x15` | `d2`–`d7` | 2–7 |
| integer arguments 7–8 | `a6`, `a7` = `x16`, `x17` | `w0`, `w1` | **0 — the two halves of `d0`** |
| scratch | `s1` = `x9` | `d1` | 1 |
| one 64-bit result or `f64` | `a0` | `d2` | 2 |
| one 32-bit result | low half of `a0` | `w4` | 2 |
| two results | `a0`/`a1` | `d2`, `d3` | 2, 3 |

> **SW1 (§3.6a) in its concrete form — and it is a silent wrong answer, not a trap: lane 0
> (`d0` = `w0` ∪ `w1` = `s0`) is scratch ONLY for a function taking six or fewer integer
> arguments.** A seventh occupies `w0`, an eighth `w1`; with both, `d0` is fully occupied and the
> only free 64-bit tile is `d1`. An allocator that treats `s0` as scratch in that case silently
> clobbers two arguments. **Nothing fires. No decode check can see it, under any of the four
> candidates.** The only defence is the admission tool's placement verifier (§6.2).
>
> **[AMENDED — THE TILE ABI IS NOT SPECIFIED HERE, AND UNTIL IT IS, THIS ROW IS A HAZARD RATHER THAN
> A CONVENTION.]** The table above is *the stock RV64 ABI read through §3.1*, which is a derivation,
> not a decision. **On neither toolchain path §3.7 specifies does the collision actually arise** — the
> day-one path `-ffixed`es `x16`–`x31` out and therefore passes **at most six** integer arguments, and
> the `W`-tier path declares `W` as sub-register indices of `D`, so a stock allocator will not
> co-allocate `s0` with `a6`/`a7`. **The collision is reachable only on a path that un-fixes
> `x16`–`x31` without declaring the sub-register relation**, which is precisely the configuration a
> tile ABI has to forbid. **Two things are therefore owed and neither is in §§3–5:**
> **(a)** a published tile calling convention that either moves `a6`/`a7` off lane 0 or declares
> `d0` non-scratch whenever seven or eight integer arguments are passed; and
> **(b)** the placement verifier that catches it if the convention is violated.
> **Both are listed in §9.1 as unresolved build items.** *(§6.4 defers the convention itself to a
> later pass; what this document can state is the constraint it must satisfy, and that the day-one
> path meets it by having no seventh argument at all.)*

**A body ends with `END`/`RETC`.** `annotate` rewrites a stock body's terminating `ret` and rejects
any body whose `ret` is not the sole terminator; **the core must not treat a trailing `ret` as an
implicit end-of-body**, because guessing is the silent behaviour cons-C15 forbids.

**A stack argument is inadmissible because there is no stack — that is a rule about the stack, not a
count of nine** (I7's `[CORRECTED]` block, CANON.md:1109-1115). Under a custom convention packing
into `w` names, **nine 32-bit arguments are 288 bits and are admissible**; sixteen are 512 and are
the ceiling.

### 6.5 Migration (**M11**, cons-C6)

**72 B exactly, unchanged, under every candidate.** `MigrationEvent::SIZE_BYTES = NMFC_CTX_BYTES + 8`
(`NMFCFabric.h:107-108`) is the **payload**; `handle`, `pc`, `origin`, `from`, `to` and `wantsReturn`
are **envelope, already travelling, already free**. Design A adds nothing to either. **B2's two class
bits go in the envelope by the same argument.** The ChampSim core model's 16-bit scoreboard adds
**one byte of envelope**, not of payload.

**Why the map must not travel, stated positively** — and it is the strongest reason your
"retrieved post-migration" instinct was right: carrying a 40–76 B map alongside 72 B would grow the
message by **56–106%**, and **I11 is a parity argument** — 72 B against the 64 B line a foreign
access would have cost, *"and the two are alternatives, never both"* (CANON.md:1331-1334). At 128 B
the parity is gone and the invariant's own justification fails.

**Arrival cost, measured: 2.2–2.3 cycles with a 100.00% instruction-cache hit rate, four tiles**
(CANON.md:1335-1337, 8267). **Under A a context arriving on a tile it has never visited is
immediately executable** — no cold-touch fetch, no handle→address translation, no handle-reuse
identity check, no map cache, no cross-page reload.

### 6.6 The scoreboard (**cons-C6**, H.4, DESIGN §7)

Fully specified in §3.5. In summary: **on the canon core, one pending destination and a one-slot data
buffer — the structure already in the tree, plus three bits** for the fill's width/extension class.
**On the ChampSim core model, sixteen ready bits at `W` granularity**, +1 KiB per tile (1.5%) and one
byte of envelope. **Two structures are specified and only the first has a build order** — §9 Step 5
implements the canon core's three bits and nothing implements the sixteen ready bits, the OR on a `d`
read, or the sixteen-bit set on a `d`-name write, **in the tree this document lives in** (§9.1 U6). **The 72-byte payload is unchanged in every configuration, so I11 is not at risk
and cons-C6 needs no re-ratification.**

**A note in B's favour that B never claims:** because B's non-overlap rule forbids overlapping names
by construction, **B dissolves A's new overlapping-name hazard class entirely.** Under A the
dependence comparator must widen from a 5-bit equality to §3.5's lane+mask form; under B it stays a
name equality.

### 6.7 `CONT` / `CONT.M` (**M10**)

**Under A this is free and it is a genuine simplification:** the map is fixed by the ISA, so a
successor **inherits the same geometry for free**, and the question "what is a `CONT` successor's
map?" does not exist.

**Under B1 it is the hardest case.** `CONT` changes the function under an unchanging handle and
**cannot fail** (CANON.md:6243-6250), so identity must be re-derived mid-invocation on the
successor's PC — and the recommended eviction policy collides with that guarantee (§4.4's
amendment). `CONT.M` is worse: user #225, *"`CONT.M` should replace the context wholesale."*

### 6.8 Encoding space, the twelve instructions, and Appendix 2 (**M14**, **M15**, cons-C22–C24)

**Untouched by A, B2 and B1.** No instruction is added or removed; no encoding bit moves; the
twelve user-level instructions plus privileged `RESUME` stand; `funct7` groups `0x6`/`0x7` are not
consumed. **No opcode is removed from `RV64IMAFD`, so ruling O4's opcode list stands unamended** —
what changes is the liveness test and eight points of semantics.

**A2 is the exception, and it is the only place A2 touches the ISA surface at all.** Its bill, in
full (§3A.2):

| | A2's cost against the ISA surface |
|---|---|
| **cons-C22 — twelve user-level instructions** | **BROKEN: twelve become fourteen** (`nx.xtr`, `nx.ins`); thirteen under A2-r. **Tier 1** — *"Growing the set is a change to a settled count and must be argued as such."* That argument is §3A and the ruling is Q5 |
| **cons-C23 — `funct7` groups `0x6`/`0x7` are the only free space** | **UNTOUCHED.** A2 consumes **none** of it. `KILL` (R77) and mailboxes (R78) keep their reservations whole — because the descriptor cannot fit in a 4-bit variant field anyway, which is *why* A2 lives in `custom-1` |
| **cons-C24 — the canon assigns no field values** | **RESPECTED.** Every bit position in §3A.2 is an implementation choice and is labelled one, not presented as canon |
| **cons-C25 — every operand is a value in a GPR** | **RESPECTED, and it is the constraint that forced the encoding.** The descriptor is an *immediate*, not a register field read against another file |
| **cons-C26 — no bit-field insert/extract with an offset and a width** | **BROKEN. This is the design.** Tier 1, R84, #233. Q5 |
| **the spare opcode** | `custom-1` = `0x2b` spent, on the tree's own earmark (`nmfc_isa.h:18-20`). `custom-2`/`custom-3` are claimed by RV128, so **nothing clean is left afterwards** |
| **the base subset (O4)** | **A2 removes no opcode and adds none inside the base ISA.** The subset is nevertheless `RV64IMA_Zfinx_Zdinx` rather than `RV64IMAFD` under the recommended Q2(a) — that is **design A's** supersession (v), not A2's (§6.3, §6.8) |

**Appendix 2 `S5`** (*the bit-level admission test is never exercised because nothing produces a
layout other than the default*) **changes rather than closing** under A: there are no layouts to
produce, and the divergence becomes *no test exercises a `w` name*. **The `W` tier is in the tile
from day one — it is 24 gates.** What is missing is a compiler that targets it. **`S6`** (RV64IM+A
only) is already overruled by O4; note that **the SST tile implements no floating point today** —
`grep -c 'fadd\|0x53\|fmv'` over `NMFCTile.cc` returns **0** — so `F`/`D` is unimplemented work
**regardless of which register-map design wins** and must not be charged to either.

---

## §7 REJECTED ALONG THE WAY

### 7.1 The per-function map fetched at decode — the mechanism the ruling killed

**Your objection, verbatim, 2026-09-03:**

> "I really don't like your idea. **It introduces a third piece of memory every context needs.** So
> now we have the map, instruction, and potentially data that must be referenced all at the same
> time. That frankly seems foolish."

**Rejected on that ground and no other** (cons-C32). It is the mechanism already in the tree —
`RegLayout` at `NMFCRegLayout.h:39-73`, consulted at `NMFCTile.cc:461-475`, one entry per resident
function at `NMFCTile.h:448-450`, described at DESIGN §25.7 D:2425-2429. **It is the most expressive
candidate in the record** — **in DIRECT NAMING, not in capacity** `[corrected - user ruling
2026-09-03 (liveness)]`: **17,360 of 17,361** named directly on power-of-two multisets under two
namespaces (everything but I2's literal 64 × 8, which it too must pack; everything at all under F0),
**against a capacity of 512 bits that design A shares exactly** —
and that is exactly why the trade had to be measured rather than asserted. **Design B is this
mechanism rebuilt to your clarification** — shrunk to 576 B on-core, filled `N × F` times for a whole
program, never on the wire — and its own conclusion is that the simultaneity objection survives.

### 7.2 Custom typed instructions carrying (offset, width) — **NOT REJECTED. This is now §3A.**

**Moved out of this section in this revision.** An earlier revision of this page rejected the
`th.ext`/`cv.extract` family here on two quantitative grounds — *"15 immediate bits, does not fit an
existing format"* and *"~3× the dynamic instruction count on the body"* — **and both were wrong**
(§2.3, corrections 12 and 13 in §5.6). Correcting them left only a **tier-1 user ruling**, which is
not a thing a proposal may resolve on its own, so the family is built out as **design A2 (§3A)**,
scored in §5, and put to you as **Q5**.

**What remains true and is worth keeping here:** the family is real, shipping, upstreamed prior art
(CORE-V `cv.extract`/`cv.insert`, T-Head `th.ext`/`th.extu`, ARM `UBFX`/`BFI`, x86 `BEXTR`,
68000 `BFEXTU`, fact-C22); it **references no third memory object**; and **fact-C10's dismissal of
"option 1" on *type* grounds was correct and irrelevant to *extent*.** What is *not* true, and was
asserted here, is that it is the only family reaching *non-power-of-two* widths that this machine
could use — A2's aligned form does not reach them either, and §5.2 records that **only design B's F0
format places a 48-bit value exactly.**

### 7.3 Single-width aliasing — `x1..x8` as eight 64-bit slices

**Rejected by cons-C30, and it is the one rejection that is not a trade.** *"A FIXED ALIASING TABLE
AT ONE WIDTH IS THE SST LAYOUT AGAIN AND IS NOT AN ANSWER."* It is `RegLayout::defaultLayout()`
(`NMFCRegLayout.h:65-72`) with the table deleted: it satisfies cons-C1 and **violates cons-C3 in
substance, because it makes the context eight 64-bit registers** — the exact formulation #238
forbids: *"Once again, NO. 512 bits of context. The context is not 8 regs. Why do you keep reverting
to that?"* **The design question is the MIX of widths among the nameable set and the aliasing
pattern**, and a single-width table does not answer it. It survives only as **B2's class 1**, where
it is one option among four rather than the whole machine, and where it is exactly what the day-one
`-ffixed` toolchain reaches.

### 7.4 The three intermediate aliasing variants

| variant | what it was | why it lost |
|---|---|---|
| **`alias-tiled`** | separate 64/32/16/8 tiers anchored at `x1`, with narrow-tier fragments | **The anchoring kills it.** Its `d0`–`d7` cover `ra`, `sp`, `gp`, `tp` — no stock GCC or LLVM allocates general values into `gp`/`tp`, so it yields **at most four usable 64-bit names, not eight**, and needs two magnitude comparators where the Heap Rule needs two OR'd bits. Its **~2,137** figure was mis-attributed to design A in `register-map.md` §7.3/§9.3 and is corrected here |
| **`alias-hierarchical`** | the buddy-subtree idea: carve a complete narrow subtree out of one 32-bit region | **Not rejected — deferred, and it is Q1's middle option.** `x2`,`x3` = the two 16-bit halves of `w15`; `x4`–`x7` = their four bytes. A complete buddy subtree of one 32-bit region, so the placement lemma extends and admission gains exactly one clause. **It buys 2,685 → 4,233 directly nameable multisets [recomputed] — instruction count, not capacity
`[corrected - user ruling 2026-09-03 (liveness)]`.** Its cost: `x2` = `sp` stops being illegal, weakening the I7 tripwire to the store alone. **Reserved names can be defined later; defined names cannot be undefined** |
| **`alias-plus-width-in-opcode`** | the register names the slice, the opcode names the width | **Redundant with what the opcode already does** (fact-C9, fact-C18): every RISC-V instruction already carries width in its suffix, so this encodes width **twice** and must then pick which wins. fact-C18 shows the three possible answers and that **Zdinx picks "mismatch is reserved"**, which is design A's legality rule 2 — i.e. the useful half of this variant **is already in A**, and the rest is dead encoding space |

### 7.5 The handle-indexed map cache (your requirement 1)

**Rejected on its own premises, not against them** (§4.3). Not because of the port — the port is
answered by banking a 576 B array (§4.6) — but because **the identity it would need does not survive
migration** (Finding 1), **`CONT` changes the function underneath it** (Finding 2), and **once the
tag your requirement 3 demands exists, the tag is a strictly better key than the handle it would
have tagged** (Finding 3): 4 cold fills for a whole run against ≈125,080. **Your requirement 4 is
met only by the function-keyed form.**

### 7.6 Banking the context array per width

**Rejected on the barrel floor** (§4.7). `K` = 4 static banks gives `C/K` usable contexts to a
single-class workload; at `C` = 256 that is **64 against H.2's floor of ≈132**, and the tile cannot
keep its pipes fed. **Bank the map by name — that is free. Do not bank the context array.**

---

## §8 PRIOR ART

**Why this section exists.** `register-map-context.md` §11 item 6 records that *"prior art has not
been checked in-record for this specific question… x86's `RAX/EAX/AX/AL`, RVV's `vsetvli`/SEW and
SVE's typed views are **unchecked claims** in this record, and the design-review rule requires them
to be checked before they are leaned on."* They are checked in `register-map-facts.md` and
summarised here, corrections included.

### 8.1 The four homes for width, and only three are available

Every shipping machine puts width in one of four places. **Three have been tried and are available
here; the fourth is ruled out.**

| home | shipping examples | available here? |
|---|---|---|
| **the register NAME** | x86 `RAX`/`EAX`/`AX`/`AL`; AArch64 SIMD&FP `Bn`/`Hn`/`Sn`/`Dn`/`Qn`; AArch64 `Wn`/`Xn` | **yes — this is design A** |
| **the OPCODE** | 68000 `MOVE.B`/`.W`/`.L`; ARM SVE `.B`/`.H`/`.S`/`.D`; RISC-V `P`/`Zpn`'s `PADD.B`/`PADD.H`; RV64's own `*W` forms | **yes, and it is already there** — every RISC-V instruction carries width in its suffix (fact-C18) |
| **an IMMEDIATE FIELD of the instruction** — the only home that carries *offset* as well as width | CORE-V `cv.extract`/`cv.insert`; T-Head `th.ext`/`th.extu`; ARM `UBFX`/`SBFX`/`BFI`; x86 `BEXTR`; 68000 `BFEXTU`/`BFINS` (fact-C22) | **yes — this is design A2 (§3A)**, and it is the only home that reaches below 32 bits without a third object. **It is also the home the four-row version of this table omitted**, which is how the family went uncosted for a whole pass |
| **a MODE REGISTER** | **RISC-V V's `vtype.SEW`**, set by `vsetvli` | **NO — cons-C8 forbids per-context mode state.** This is prior art *for the thing ruled out*, and must be cited that way |
| **a TAG ON THE VALUE** | Burroughs B5000/B6700; Symbolics Lisp machines; IBM System/38 and AS/400; the Mill's belt metadata | **NO — tags cost bits inside the 512** |

### 8.2 The closest ratified precedent, and the one this design should have been written against

**Zdinx (Ch. 26 §26.1) is the single most relevant piece of prior art in the whole ISA** (fact-C16).
It faces the identical problem — 64-bit doubles, 32-bit register names — and solves it with **aligned
register pairs**: *"register numbers must be even"*, odd numbers **reserved**; *"the lower-numbered
register holds the low-order bits"*, endianness-independent; and *"when a double-width floating-point
result is written to `x0`, the entire write takes no effect… when `x0` is used as a double-width
operand, the entire operand is zero — in other words, `x1` is not accessed."*

**That is a complete, ratified, shipping answer to "the number of the reg implies the slice."** It
supplies a fixed slice width per name, an alignment rule for wider values, an explicit ordering rule,
and a specified `x0` interaction. **It is prior art for the shape and not a component to reuse: the
rule quoted is stated "for RV32", and at XLEN = 64 Zdinx pairs nothing** (fact-C16, §3.7). **Design A's Heap Rule is Zdinx's rule inverted** — Zdinx numbers
the *parts* and derives the whole from a pair; A numbers the *whole* (`x8`) **below** its parts
(`x16`, `x17`) with a pairing stride of 2 across a 16-name class. **That inversion is the source of
essentially all of A's back-end cost** (§3.7), and it is the one place the design departs from
Zdinx's shape without the departure being forced.

**MIPS-I and SPARC V8 FP register pairing are the same rule, thirty years earlier** — `$f0` is a
32-bit single, a double named `$f0` occupies `$f0`+`$f1`, odd numbers illegal for doubles.
**Zdinx re-ratifying it in 2026 is decent evidence it is the durable answer.**

### 8.3 The two failure modes to inherit deliberately, and the one to avoid

**x86's sub-register aliasing is real prior art and its known failures are the reason to read it
carefully** (fact-C19):

1. **Write rules inconsistent by width** — writing `EAX` **zero-extends** into `RAX`; writing `AX` or
   `AL` **preserves** the upper bits. Two behaviours for two views of one register. **A avoids this
   by having exactly one rule (W3: write your own bits and never a neighbour).**
2. **`AH`/`BH`/`CH`/`DH` are non-contiguous views** (bits 15:8) existing only for the legacy four
   registers — an irregularity that survives to this day. **A's slices are all naturally aligned and
   generated by one rule, so there is no irregular case.**
3. **Preserve-on-narrow-write creates partial-register dependencies** — the hardware must merge the
   new narrow value with the old wide one, *"which cost Intel a documented class of stalls and, later,
   dedicated merging micro-ops."* fact-C19's conclusion is verbatim against this map: **"Disjoint
   slices avoid it; overlapping views do not."** **Design A has overlapping views by construction.**
   It is moot on the canon barrel core — one instruction per context in flight, so there is no bypass
   network at all (CANON.md:474-478) — and it is a real cost on any other implementation (§3.5's
   amendment).

**AArch64 chose zeroing over preserving precisely to avoid (3)** — writing `Wn` zeroes bits 63:32 of
`Xn`, and the `Bn`…`Qn` views are bottom-anchored nested prefixes rather than disjoint slices
(fact-C20). **NMFC can do neither**, because the bits above `w`*2k* are another architectural value.
That is why W3 is *"write exactly your own bits"* and not either precedent.

### 8.4 Two corrections the record needed

- **ARM SVE does *not* have typed register views** (fact-C20, FALSE as stated). The element type is a
  **size specifier in the instruction** (`.B`/`.H`/`.S`/`.D`); `Zn` is size-agnostic. SVE is an
  example of "the opcode types the operation", **not an exception to it.** The right ARM citation for
  this design is **AArch64's SIMD&FP file**, where the letter selects the width and the number selects
  the register — very nearly *"the number of the reg implies the slice."*
- **RISC-V `P`/`Zpn` is not ratified** and is prior art for the *opposite* mechanism (fact-C21). The
  manual's `zp.adoc` is a placeholder: *"This chapter is a placeholder for the forthcoming `P` and
  `Zp*` extensions."* `P` packs lanes into the `x` registers and names the lane width **in the
  opcode**, giving **no name for an individual lane** — evidence that the RISC-V community's answer
  to sub-register slices has consistently been *the opcode names the geometry*. **The `H` extension
  is the hypervisor extension and has nothing to do with register widths** (fact-C23).

### 8.5 The precedent for trading names for state

**RV64E** reduces the integer register count to sixteen (`x0`–`x15`), and its stated motivation is
startlingly close to this machine's own: *"interest in RV64E for microcontrollers within large SoC
designs, and **to reduce context state for highly threaded 64-bit processors**"* (fact-C17). **A
ratified extension already trades architectural names for per-context state on exactly the grounds
NMFC is built on.** Design A trades in the opposite direction — it spends names to *divide* a fixed
state — but the precedent that the name count is negotiable is ratified and should be cited.

---

## §9 BUILD ORDER FOR DESIGN A IN SST

Concrete files, in dependency order. Nothing here needs the compiler back end; steps 1–5 are
runnable against hand-written and `-ffixed`-constrained bodies.

### Step 1 — the map itself

**`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCRegLayout.h`**

- **Delete `struct RegLayout` (lines 39–73)**, including `defaultLayout()` (65–72), `defines()`,
  `bitsUsed()` and `hasZero`.
- **Keep `struct RegField` (33–36)** as the internal (offset, width) pair — `Context512` takes it.
- **Add** a `constexpr` decoder of ~8 lines implementing §3.2, returning `{offset, width, legal,
  zero}` from a 5-bit name. It is the whole map.
- **Keep `struct Context512` (84–118)**, and **delete its straddle branches** — `read`'s
  `word[w0 + 1] << got` (line 97) and `write`'s `if( b0 + f.width > 64 )` (line 110). Under this map
  no slice crosses a 64-bit word, so both are **provably dead**. Deleting them is what buys the
  single-word read and the write-enable path.
- **Net: −60, +15 lines.**

### Step 2 — remove the per-function table

**`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCTile.h:448-450`** — delete `RegLayout layout_;` and its
comment. **−3 lines.** This is the tier-4 half of the M1 deletion; the tier-3 half is the DESIGN
§25.7 D:2425-2429 supersession (§6.3).

### Step 3 — the decode path

**`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCTile.cc:461-475`** — `readReg`/`writeReg` gain **the operand's
required width**, because W1 makes width per-operand:

```
  uint64_t readReg ( TileContext& c, uint32_t r, Role role, unsigned reqWidth ) const;
  void     writeReg( TileContext& c, uint32_t r, Role role, unsigned reqWidth, uint64_t v );
```

> **[CORRECTED — `Role` alone cannot compute the width, so the two-argument form in an earlier
> revision is not implementable.]** `Role` was never defined, and role is not sufficient in any
> definition: W1 makes integer ALU width *"32 iff the opcode is a `*W` form"*, load/store data width
> **the opcode's**, and FP width **the mnemonic's** — all opcode properties, not role properties. Role
> still carries what only role knows (address/base, branch source, `rs3`), but the width must come
> from **the per-opcode required-width table of §3.4 rule 2**, which is Step 4's deliverable and does
> not exist yet (§9.1). **The 31 mechanical call-site edits were sized on the two-argument form and
> should be re-sized on this one**; they remain mechanical, and the compiler still finds every one.

`layout_.field[r]` becomes the constexpr decode from step 1; `illegal()` stays and is re-purposed
onto §3.4's seven rules. **+30 lines.**

**The 31 call sites** — `grep -c 'readReg\|writeReg' NMFCTile.cc` = **33**, less the 2 definitions.
Each passes its role. **Mechanical, and the compiler finds every one** because the signature changed.

### Step 4 — legality

**`NMFCTile.cc`, in the decode path** — §3.4's seven rules, with the per-opcode required-width column
as **8 bits per opcode-table row** (four operand slots: `rd`, `rs1`, `rs2`, `rs3`). **+80 lines.**
`illegal()` at `:477-489` is the existing reporting path and does not change.

> **[THE TABLE ITSELF IS NOT WRITTEN, AND THE "+80 lines" IS NOT A CHECKED ESTIMATE.]** §3.4 rule 2
> is *"a per-opcode required-width column total over the subset"*; **that column exists nowhere in
> this document or in any source document.** It is on the critical path twice over — it is the
> decoder's legality, and Step 3's `readReg` signature cannot be written without it — and the second,
> separate table §3.8 needs (**a ~150-entry opcode→width table for `annotate`**) is also unwritten and
> is the one where over-permissiveness is *fatal* under cons-C15. **Neither line estimate has been
> checked against real content.** Both are listed in §9.1.

### Step 5 — the D-buffer's width class

**`NMFCTile.h:85-86`** (`dbufReg`, `dbufValue`) gains **3 bits** — the fill's width/extension class.
Written at **`NMFCTile.cc:827-828`** beside `c.dbufReg = info.destReg`; consumed at
**`NMFCTile.cc:1504`**, where `writeReg( c, c.dbufReg, c.dbufValue )` needs the class to know how to
extend into the destination name. **+1 field, +1 argument.**
*(Note: `NMFCTile.cc:820` already applies `signExtend(v, info.size*8)` before `dbufValue`, so part of
the work is done; the three bits carry the destination-name adjustment, not the load's own.)*

### Step 6 — the admission tool

**`/mnt/md0/ChampSim/ChampSimArchWork/nmfc/tools/nmfc/annotate.cc`**, in the order that keeps it
sound at every intermediate commit:

1. **`:461-470`** — replace `width_of` with the ~150-entry RISC-V opcode→width table. **Until it
   lands, make the fall-through charge 64 and say so** — today it charges **16** for `a0`, which is
   permissive by 4× (§3.8). This is the largest tooling item and K.6 requires it anyway.
2. **`:555-559`, `:927-928`** — make `peak_bits` the **gate** rather than a stderr line. ~10 lines.
3. **`:524-559`** — delete the `opt.num_regs` slot pool and its `die()`; replace with FFD placement
   over (offset, width) with relocation. ~120 lines. **This step needs an emitter `annotate` does not
   have** (§6.2) — either it grows one, or the placement moves to the back end. **That choice is not
   made anywhere in this document, and neither is the backtracking bound, the insertion point for
   relocation `mv`s, or the algorithm at exactly 512-of-512** (§3.6's amendment says only that *"the
   placement must be right the first time"* there). **§9.1 U3** — and the verifier in step 4 sits
   downstream of all of it.
4. **the disjointness verifier** — ~40 lines, and it is what makes the tool sound.
5. **`ret` → `END`/`RETC`** rewrite and the sole-terminator check. ~30 lines.

### Step 7 — what does not change, and must be checked rather than assumed

| | why it is untouched |
|---|---|
| `nmfc_isa.h:101-102` (`NMFC_CX_LANE_SHIFT`, `NMFC_CX_LANE_MASK`) | lane *k* **is** `d`*k*; no tile straddles a lane (§6.1) |
| `NMFCFabric.h:94-123` (`MigrationEvent`) | nothing added to payload or envelope (§6.5) |
| `NMFCContextRegs.h` | the eight-lane storage is unchanged; **its header comment already says the eight is a packing, not a structure**, which stays true |
| the twelve instructions, `funct7` `0x6`/`0x7` | nothing added or consumed (§6.8) |

### Step 8b — **only if Q5 is answered yes**: design A2

Purely additive to steps 1–7; **nothing above changes.** In dependency order:

1. **`nmfc_isa.h`** — a second reservation block for `custom-1` (`0x2b`), the two `funct3` values and
   the `{sx, rsv, index, wcode}` immediate layout of §3A.2. **The `funct7` group table is not
   touched**, and the comment at `:95-99` (*"There is deliberately no bit-field insert or
   extract"*) is **rewritten with a pointer to the ruling**, not deleted — it is the tier-1 record.
2. **`NMFCRegLayout.h`** — one `constexpr` extent decoder beside step 1's name decoder:
   `offset = index << (wcode + 3)`, `width = 8 << wcode`. **~8 lines. The straddle branches stay
   deleted**, because §3A.2's alignment makes them unreachable for extents too.
3. **`NMFCTile.cc` decode** — one opcode case, the three legality rules of §3A.6, and the extent
   read/write paths through `Context512`. **+≈50 lines.**
4. **`Context512::write`** — enable granularity 32 bits → 8. **No read-modify-write is introduced**;
   if it would be, `wcode` has been extended below a byte and §3A.6 forbids it.
5. **`annotate`** — the pack-or-name decision and the closed-form scratch clause of §3A.5
   (`≤ 480` when any value is narrower than 32 bits). **This is the largest item and it is a
   compiler question, not a simulator one.**
6. **Two more supersession ledger entries** (cons-C31): **cons-C26/R84** and **cons-C22**.

**Total: ≈60 lines in SST on top of design A's ≈150**, plus the allocator work.

### Step 8 — the divergences to file

- **Appendix 2 `S5`** — restate: the `W` tier **is** in the tile from day one; what is missing is a
  compiler that targets it, so the divergence becomes *no test exercises a `w` name*.
- **Appendix 2 `S6`** — already overruled by O4. Note separately that **the SST tile implements no
  floating point today** (`grep -c 'fadd\|0x53\|fmv' NMFCTile.cc` = 0), which is work owed
  **regardless of which design wins.**
- **Five supersession ledger entries** (cons-C31), the full list at §6.3: DESIGN §25.7
  **D:2425-2429**; and — **only if Q2 is ruled (a)** — CANON.md:9849's *"the namespaces do not
  alias"*, its restatement in the **O4 ruling row at CANON.md:127**, **I.0's four-point answer at
  CANON.md:6029-6060** (whose point 1 states the *"per-function binding… carried with the offload"*
  that design A deletes outright), and **O4's spelling**, `RV64IMAFD` → `RV64IMA_Zfinx_Zdinx`.

**Total SST delta: ≈150 lines net across three files, plus 31 mechanical call-site edits, plus ~350
lines in `annotate`.** Smaller than the rewrite K.6 already demands — **and see §9.1, because three
of the items inside those totals have no content behind them yet.**

### §9.1 UNRESOLVED BUILD ITEMS — engineering decisions, not user rulings

**None of these changes which design is built, so none is a question in §10. All of them are on the
critical path, and collecting them here is the alternative to leaving them in prose where an
estimate can be quoted past them.**

| | what is unresolved | why it blocks | where it is |
|---|---|---|---|
| **U1** | **The per-opcode required-width column** over the subset — `{rd, rs1, rs2, rs3}` × required width, ~8 bits per opcode-table row | §3.4 rule 2 **is** this table; Step 3's `readReg` signature cannot be written without it; the "+80 lines" is unbacked | §3.4, §9 Step 4 |
| **U2** | **The ~150-entry opcode→width table** for `annotate` | §3.8's whole resolution; **over-permissiveness here is fatal under cons-C15**, and the tool is permissive by 4× today | §3.8, §6.2, §9 Step 6.1 |
| **U3** | **Who owns the placement pass.** §9 Step 6.3 says the FFD-with-relocation step *"needs an emitter `annotate` does not have — either it grows one, or the placement moves to the back end"*, **and does not choose.** Unspecified alongside it: any bound on permitted backtracking; where relocation `mv`s are inserted and how they interact with liveness and the D-buffer; and what algorithm applies at exactly 512-of-512, where §3.6's amendment says only that *"the placement must be right the first time"* | **The ~40-line disjointness verifier — the only defence against SW1 — sits downstream of all of it** | §3.6, §6.2, §9 Step 6.3 |
| **U4** | **The post-RA no-spill gate.** §3.7 calls it *"the gap nobody has costed"*: under `-ffixed`, an exhausted `D` class makes LLVM spill, which I7 and cons-C15 make fatal, and which arrives as correct-looking codegen with no diagnostic | Without it the day-one path silently produces inadmissible binaries | §3.7 |
| **U5** | **The tile calling convention.** §6.4's table is the stock RV64 ABI *derived* through §3.1, not a decision; `a6`/`a7` = `w0`/`w1` are the two halves of `s0` = `d0` | Neither §3.7 path can reach the collision, but nothing forbids a third path that can | §6.4 |
| **U6** | **A build order for the ChampSim core model's scoreboard.** §3.5 specifies **two** scoreboards — the canon core's one pending destination plus three bits, and the ChampSim model's **sixteen ready bits at 2 KiB per tile with a lane+mask overlap check**. §9 is titled *"build order for design A in SST"* and implements only the first (Step 5). **This document lives in the ChampSim tree** | Unassigned: who ORs the two ready bits on a `d` read, and who sets all 16 bits touched by a `d`-name write | §3.5, §6.6, §9 Step 5 |
| **U7** | **A timing budget** for the register-read stage | It is the single cheapest measurement that would retire §5.3's `[ASSUMPTION]`, on which every `+0 cycles` in that table depends | §5.3 |

**U1, U2 and U3 are the three that are inside a quoted line estimate without content behind them.**
The honest form of §9's totals is *"≈150 lines plus two unwritten tables and an undecided pass
owner"*, and that is how they should be read.

---

## §10 THE FIVE OPEN QUESTIONS

Restated for a one-word answer. **Only these five change the design**; everything else in §§3–4 is
resolved as a recommendation, and every unresolved *build* item — the ones that need an engineer, not
a ruling — is collected in **§9.1** rather than hidden in prose.

**They are not five of a kind. Q2, Q3 and Q4 decide what is built now; Q1 and Q5 reserve options and
change no step of §9's build order.** And the body's own ranking puts **Q3** first: §5.5 and this
table both call its DYN/NaN-box divergence *"the worst failure mode in either design: the same
encoding computes different results on host and tile, and nothing can see it."* If you rule one
question carefully, rule that one.

| | question | recommendation |
|---|---|---|
| **Q1** *(reserves an option; changes no step of §9's build order)* | **Where do you stop?** The ladder is **not a line**: it is `A ⊂ {B2, A2} ⊂ B1`, and B2 and A2 are incomparable. Counted on one universe of **17,361** width-multisets (§5.2): **A** places **2,685** and references no object. **B2** places **4,143** under the recommended Q2 = yes (**4,175** under Q2 = no), reaches a complete byte class over the top 192 bits — *five 64-bit names plus twenty-four byte names, not the "32 byte names over half the file" an earlier revision quoted, which needs 36 names and two namespaces (§4.7)* — references no object, and costs 2 bits per context, **one mux level on every decode forever**, and 2,520 bits of ROM instead of 310. **A2** places **13,809** under its closed-form rule (**≤ 17,360** under the exact rule), reaches the byte tier at any aligned offset, references no object, adds **0 bits per context and 0 bits of envelope** — for two instructions in `custom-1`, +1 instruction per packed read and write, and **two tier-1 reopenings**. **B1** places **13,091** under Q2 = yes and **17,360** under Q2 = no, and **is** the third object, with four run-time failure modes, a linker obligation and new privileged kernel work. **No scheme here places all 17,361: the missing one is I2's literal 64 × 8** (§5.2). **[all recomputed]** | **A now, with BOTH hatches RESERVED** — the two-bit class field and the `custom-1` opcode. Both reservations are free (`nmfc_isa.h:18-20` already holds `custom-1`; the class bits ride in an envelope that already travels), and **reserving is not choosing**. Nothing in the record measures a demand for a sub-32-bit value — **and per §3.8 nothing in the record currently could**, since the tool that produced §5.4's decompositions charges 16 bits for `a0` and cannot see widths at all. **When one appears, the choice between B2 and A2 turns on its shape:** a few functions wanting one fixed byte-heavy *layout* → **B2**; arbitrary mixes, or a function that A rejects by a handful of bits → **A2**, which is also the only one of the two that converts a fatal admission failure into a graded cost. `[user ruling 2026-09-03 (liveness)]` **These multiset counts are DIRECT-NAMING figures, not capacity: A, A2, B2 and B1 all hold 512 bits and express every width, so "places" means "names in one instruction" and the rest are packed at ~2-3 ops per access.** |
| **Q2** *(decides what is built now; supersedes canon in four places)* | **Does `f`*n* ≡ `x`*n*?** **Yes:** 24 names, two complete tiers, **one allocation pool**, 2,685 shapes. **It supersedes four canon statements, not the one an earlier revision marked** — CANON.md:9849's *"the namespaces do not alias"*; the **O4 ruling row's** restatement of it at CANON.md:127 (*"the namespaces do NOT alias, because a register name is not a fixed bit offset here"*); **I.0's four-point answer at CANON.md:6029-6060**, which closes *"the `f`-names do not overlay the `x`-names"* (:6059) and whose point 1 states a *"per-function binding from register name to bit range… carried with the offload"* that design A **deletes outright** rather than merely contradicts; and **O4's spelling itself** — `f`*n* ≡ `x`*n* **is** Zfinx, the ratified spec makes F/D and Zfinx mutually exclusive (facts §6.1), so the subset becomes **`RV64IMA_Zfinx_Zdinx`**. O4's substance is untouched: float is in, and every `F`/`D` operation is still provided. **No:** 56 names, three complete tiers, two pools, **9,165** shapes — and a 16-bit tier with **no arithmetic to run on it** (no RV16I; `F`'s narrowest operation is 32 bits; `Zfh` is outside O4). **[recomputed]** | **Yes.** One pool makes K.6's "third wrong answer" — two pools admitting a function twice the legal size — **structurally unrepresentable**, and it is what makes `rv64ima_zfinx_zdinx` the day-one spelling (§3.7). **Rule it knowing it carries the O4 spelling amendment**: an earlier revision adopted Zfinx semantics while asserting O4 stood unamended, which facts §6.1 records as impossible. Ruling (b) is a strictly larger change: O4 must be amended for the 16-bit tier to be worth having, and admission and the scoreboard both grow. `[user ruling 2026-09-03 (liveness)]` **These multiset counts are DIRECT-NAMING figures, not capacity: A, A2, B2 and B1 all hold 512 bits and express every width, so "places" means "names in one instruction" and the rest are packed at ~2-3 ops per access.** |
| **Q3** *(decides what is built now — and the body ranks it the worst failure mode in either design)* | **Supersede CANON I.7 item 3?** It says *"a function needing dynamic rounding modes… **cannot be offloaded**."* Both designs instead define `rm = DYN` as RNE, because GCC and LLVM emit no rounding suffix and the assembler encodes DYN by default — **rejecting it makes all stock FP codegen illegal**. The replacement is a build-time gate, and **the gate cannot see the case that matters**: the divergence comes from the **caller's** `fcsr.frm`, set in a translation unit `annotate` never walks. | **Supersede, with the divergence on the price list rather than in a footnote — but it is your call, not the document's**, because I.7 says "cannot be offloaded" and this softens it. *(`register-map.md` §3.7 called this "costs nothing, breaks nothing"; that is withdrawn.)* **This is the worst failure mode in either design: the same encoding computes different results on host and tile, and nothing can see it.** |
| **Q4** *(decides what is built now)* | **Is the run-time undefined-register trap a REQUIREMENT or a preference?** `RegLayout::illegal()` fires today (`NMFCTile.cc:464`, `:472`). Under A, A2 and B2 a total map leaves it nothing to fire on and the check re-homes to build time. **Only B1 keeps it — and only the undefined-*name* half; over-liveness (SW1, §3.6a) stays silent under all four** (§4.4). | **Preference.** If you rule it a **requirement**, it eliminates A, A2 and B2 together and forces B1 — i.e. it reinstates the third referenced object — so it is worth ruling explicitly rather than by omission. `register-map-context.md` §0 point 3 calls it *"`RegLayout`'s one genuinely load-bearing behaviour"*, which is why it is asked. |
| **Q5** *(reserves an option; changes no step of §9's build order)* | **Do you reopen R84 / cons-C26 (a bit-field insert/extract carrying an offset and a width) and cons-C22 (twelve user-level instructions)?** Both are **tier 1**. Without both, design A2 cannot be built whatever it scores, and Q1's ladder collapses back to `A ⊂ B2 ⊂ B1`. **The one fact bearing on it that the record does not currently hold:** R84's stated reason — *"it duplicates instructions RV64I already has"* — is **true of extract and false of insert.** Base RV64I reads a packed field in **2** instructions and writes one back in **5–8 plus a 64-bit scratch name**, because `andi`'s immediate is 12 bits and there is no bit-field insert in the base ISA (§3A.4). Under 512 bits with no stack, that scratch name is the cost that decides it. Your #231 asked for instructions *"so that values can be retrieved/**set**"*; #233 narrowed it to *"**EXTRACTION**… regular bit manipulation can take you the rest of the way"* — which is true on the host, where masks live in spare registers, and is what cons-C27 is scoped to. | **Not the document's call, and it should be ruled explicitly rather than by omission** — because answering *no* is a complete answer that closes A2 and changes nothing else in this proposal. If *yes*: reserve `custom-1` now, build nothing until a measurement asks, and rule separately on **A2-r** (extract only, §3A.7 — thirteen instructions instead of fourteen, the write path untouched, and it is the form #233's own words permit). |

**What is NOT asked, and why.** The two supersessions A requires (DESIGN §25.7 D:2425-2429 and
CANON.md:9849) are **consequences of Q1 and Q2**, not independent rulings — mark them when those are
answered. The remaining nine questions `register-map.md` §10 raised, and the nine
`final-B-context-map.md` §15 raised, are resolved in §§3–4 as recommendations because **answering
them does not change which design is built.**

**One thing the record needs and does not have, which no ruling supplies: a measurement.** Every
argument about whether A's **~2-3 ops per packed access** bite turns on **how often real offloaded
functions touch sub-32-bit live values** `[corrected - user ruling 2026-09-03 (liveness)]` — ~~about
whether A's charge-32 rule bites~~, a rule that does not exist — and neither measured decomposition
shows such a value — **from a tool that, per
§3.8, cannot see a width**, so the deferral rests on an absence of measurement rather than a
measurement of absence. A width histogram over the existing
runs — once `annotate` has a working width input (§3.8) — would settle Q1 with data instead of
judgement, and would also settle whether B1's per-function packing is ever needed. **That counter is
a change to an existing tool, not a design question.**

---

**END — PROPOSAL, NOT IN THE CANON UNTIL RULED.**
