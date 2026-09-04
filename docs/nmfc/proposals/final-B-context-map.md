# DESIGN B — THE MAP AS AN EXTENSION OF THE CONTEXT

**PROPOSAL — NOT YET IN THE CANON.** Nothing in this file may be cited as a decision. It is
the second of the two designs the user asked for on 2026-09-03: *"I would prefer a proposal
for 1 or 2 suggested designs is written up for the end of this, with full consideration of
implementation complexity, performance impact, and overall simplicity."* Design A is
`register-map.md` — the fixed-aliasing Heap Rule, where the register **name** denotes the
slice and nothing is fetched. **This is the other one: the map survives, as per-function
state reached through the context, retrieved after migration rather than carried by it.**

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

**Prerequisites.** `register-map-context.md` (mechanisms **M1**–**M15**, constraints
**cons-C1**–**cons-C32**), `register-map-facts.md` (verdicts **fact-C1**–**fact-C24**),
`register-map-fallback-user.md` (the user's words, verbatim), and `register-map.md` §§1–3
for the execution-semantics rules **W1/W1b/W2/W3**, which **design B inherits unchanged**
(§9.4 below says exactly which of A's ten ISA deviations come with them and which do not).

**Every claim below cites a spec chapter, a `file:line`, a measured number, or a user
statement. Numbers that are configuration are labelled `[CONFIGURATION]`.**

---

## §0 THE DELIVERABLE, AND THE TWO RULINGS IT SITS BETWEEN

### 0.1 The ruling that killed the map (2026-09-03), verbatim

> "I really don't like your idea. **It introduces a third piece of memory every context
> needs.** So now we have the map, instruction, and potentially data that must be referenced
> all at the same time. That frankly seems foolish."

That is cons-C2 and it is the reason design A exists. **This document does not argue against
it** (cons-C32). It takes the user's own later clarification as its brief.

### 0.2 The clarification that defines design B (2026-09-03), verbatim

From `register-map-fallback-user.md`:

> "I want to clarify, **the map is best thought of as an extension of the context.** Ideally
> it doesn't follow with migration, **it is retrieved post-migration on the new core.**
> However, if the handle-index cache doesn't seem viable (**it would need a port for the
> width of the machine or we would need contexts aligned to widths so the entire system
> could be banked-per-width** (potentially necessary anyway)). All things to consider."

and, from the same file, the five numbered requirements the user wrote out:

> 1. "Each core has a series of context handles. These by construction never overlap. In each
>    nmfc core, there exists an array indexed by these handles, that stores some 512-bit to
>    regs mapping for the given function."
> 2. "Upon entering an nmfc core, the context must load this piece of data. There must somehow
>    be an address associated with this specific context handle, such that as it migrates
>    between nmfc cores it pulls the correct regfile map regardless of current PC."
> 3. "It is also critical that when a handle is reused, there is some sort of way to detect
>    whether the function is identical or not."
> 4. "**Migration must not bear any of this burden**, it must be cached locally within each
>    core upon first-visit... excluding startup or a cold touch of an nmfc core, the function
>    should never have to fetch its regfile map."
> 5. "The problem here is clear: lots of contexts, each context needs a particular address for
>    its map. This is essentially a translation map. You could cheat and keep the map in the
>    first few bytes of every instruction page... The caveat there is that if the instruction
>    memory crosses a page boundary, you incur an unnecessary reload of the reg map."

### 0.3 The one-sentence statement of design B

> **The map is per-FUNCTION state, reached through the context, resident in the function's own
> read-only code image, cached on each tile in a 576-byte on-core table, and named by a
> three-bit index that lives in the context's tile-local scheduling slot — never in its 512
> bits and never on the wire.**

"An extension of the context" is exact and it is worth being precise about *which* extension:
the map is not part of the context's **value** (I2's 512 bits are untouched, §5.1) and it is
not part of the context's **identity** (the handle is). It is part of how the context's value
is **read** — the same kind of thing as the function's code, and it lives in the same place.

### 0.4 The three findings that shape the rest of the document

Stated up front because each one changes the design the user sketched, and each is
independently checkable:

> **Finding 1 — the entry PC does not survive migration, so it cannot be the identity tag on
> arrival.** `NMFCTile.cc:1368-1402` (`handleMigration`) sets `c.pc = mig.pc`, and `mig.pc` is
> the **resumption** PC — "*It resumes at the instruction that sent it here, and runs it again*"
> — not the entry PC. `MigrationEvent` (`NMFCFabric.h:98-105`) carries `handle`, `pc`,
> `faultAddr`, `origin`, `from`, `to`, `wantsReturn`, and **no entry PC and no function id**.
> The task's proposed identity — "the FTU-assigned handle is paired with the entry PC at FORK
> and the tile keeps (handle → entry PC, map)" — works at `FORK` (`admit()`,
> `NMFCTile.cc:544-580`, sets `c.pc = inv.pc` and that *is* the entry PC) and **does not work
> at the destination of a migration**, which is the case the whole design exists to serve.
> §4.2 gives the three ways out and prices them.
>
> **Finding 2 — `CONT` changes the function under a constant handle, so the identity check is
> not only about handle *reuse*.** CANON.md:6223-6231, verbatim: "**`CONT` is I10's successor,
> and it CANNOT FAIL.** It **inherits the existing FTU entry rather than allocating one** …
> It is also the mechanism for splitting a function too large for one 512-bit register file
> into a chain, each link admissible on its own." **A `CONT` chain is a sequence of different
> function bodies, with different maps, under one unchanging handle.** `CONT.M` is worse: user
> #225, "**CONT.M should replace the context wholesale**". So the check must run at every
> `CONT`, not merely at admission — which no handle-indexed cache does by construction.
>
> **Finding 3 — once the identity check exists, the handle index is strictly worse than
> indexing on the identity itself.** The user's requirement 3 forces a tag. A cache keyed on
> `handle` with tag `fid` and a cache keyed on `fid` do the same comparison; the second is
> smaller (a tile runs a handful of distinct functions, CANON.md:5366-5372, against up to
> 1024 live handles, CANON.md:203 `[CONFIGURATION]`), and it **shares one fill across every
> context of that function**, which the handle-keyed form cannot. §4.5 prices the difference
> at **125,080 cold fills against 4** on the one measured stress workload in the record.

---

## §1 WHAT B IS, IN ONE PAGE

| | **B1** — free map, cached per function (**recommended**) | **B2** — banked per width |
|---|---|---|
| what a name means | whatever the function's map says: `(offset, width, namespace)` | `offset = name × width`, where `width` comes from the context's **2-bit class** |
| where the truth lives | 40–76 B beside the function's code, on its duplicate page (§3) | nowhere — it is a wire pattern (§7) |
| per-context state added | **4 bits** (3-bit map index + valid) — 512 B per tile at `C` = 1024 | **2 bits** — 256 B per tile at `C` = 1024 |
| per-tile state added | one 576 B map file, replicated `W` = 4 ways = **2.25 KiB** (§6.4) | **0** |
| migration payload | **72 B, unchanged** (§5) | **72 B, unchanged** |
| decode | a bank read (3-bit index) + 3 mux levels, in the stage that already decodes (§6.4) | a 4:1 mux — one level more than design A (§7.5) |
| expressiveness | every packing I2 names except one byte slice (§2.6, §12.1) | four fixed layouts, chosen per context (§7.4) |
| the third referenced object | **shrunk, not removed**: an on-core 576 B array, read like a decode ROM; memory is touched once per (tile, function) | **removed**: the "map" is 2 bits inside the context's own slot |
| `cons-C14` (undefined register is a hard error) | **RESTORED as a machine guarantee** (§2.5) — design A cannot do this | not restored (every name is always defined, as in A) |

**They are not exclusive.** §7.3 shows that B2's classes are *fixed layouts*, so **design A is
B2 with `K` = 1**, and B2 is the smallest change that buys **direct names** for a byte tier and
an all-64 class without a memory reference. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` It buys **no capacity**: a byte under A
is packed and reached by shift-and-mask through a scratch name, so what B2 buys back is ~2-3
instructions per sub-name access, not the ability to hold the value. If the user rules design A in, B2 is the natural extension
of it; if the user rules B1 in, B2 is its degenerate case.

---

## §2 THE MAP: WHAT IT MUST EXPRESS, AND WHAT IT COSTS IN BITS

### 2.1 How many names — and the task's "32 names" is one namespace short

**CANON.md:9819, tier 1, verbatim:**

> "the packed file is presented under **two register namespaces over the same 512 bits** — a
> naming convention, never a second file. **The namespaces do not alias**: the core implements
> 512 bits of live storage rather than 64 architectural slots, and the compiler binds every
> simultaneously-live `f`- or `x`-name to a **disjoint bit range**, so `f3` and `x3` are
> different names at different offsets, not one slot."

So a *complete* map is indexed by the pair (namespace, number) — **64 encodings, of which 63
need an entry**, because `x0` is hardwired zero (Ch. 2) and denotes no bits, while `f0` is a
general FP register and does (fact-C17: "`f0` **is** general (the FP file has no hardwired-zero
register)"). The tree's `RegLayout` is **half a map**: `NUM_NAMES = 32`
(`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCRegLayout.h`), the `x` namespace only, which was correct
when the subset was RV64IM+A (tier-4 divergence `S6`) and is not correct under ruling **O4**.

**A 32-entry map is still possible** and it is the format the task sketched — one entry per
register *number* plus a namespace bit — but it is a **different design with a legality rule**
(§2.3 F3), not a smaller encoding of the same thing.

### 2.2 What a map entry has to carry

| field | why | width |
|---|---|---|
| **offset** | which bit of the 512 the name starts at | 9 bits (0–511) |
| **width** | how many bits it is | 7 bits free (0–64), 3 bits if powers of two plus "undefined" |
| **namespace** | *not a field.* It is part of the index in a 64-entry map; it is a field only in the 32-entry F3 form | 0 or 1 bit |
| **defined?** | cons-C14: a name the map does not define must be a **hard error**, not a silent zero. This is `RegLayout::defines()` and it is the one behaviour of the rejected mechanism the record says must be inherited (`register-map-context.md` §0 point 3) | folded into the width code (`width = 0`) |

**Type is not in the map, and must not be.** fact-C9/C10 and Zfinx (Ch. 26 §26.1) settle it:
RISC-V never infers an operation's type from a register name; the opcode types the operation.
The map answers *which bits*; the opcode answers *what they mean*. Adding a type field would
be the fourth home for width prior art rules out (`register-map.md` §6 item 12).

### 2.3 Four candidate encodings, priced

| | rule | bits/entry | entries | map size | fits one 64 B line? |
|---|---|---|---|---|---|
| **F0** — free | any offset, any width 1–64 | 9 + 7 = **16** | 64 | **128 B** | no (2 lines) |
| **F1** — power-of-two widths | any offset, width ∈ {8,16,32,64} | 9 + 3 = **12** | 64 | **96 B** | no (2 lines) |
| **F2** — aligned powers of two | width ∈ {8,16,32,64}, offset = index × width | 6 + 3 = **9** | 64 | **72 B**, split 36 + 36 | **yes for the `x` half alone** |
| **F3** — one entry per *number* | as F2, plus a namespace bit; a function may not use both `x`*n* and `f`*n* | 6 + 3 + 1 = **10** | 32 | **40 B** | yes |

*(As built, the tree's form is neither: `RegField` is `uint16 offset + uint8 width` = 24 bits
per entry, 96 B for 32 names, 192 B for 64 — three times F2, because it is a C struct and not
an encoding. `NMFCRegLayout.h`.)*

**F2's offset compression, spelled out because it is the whole saving.** A naturally aligned
slice of width *w* starts at a multiple of *w*, so the offset is `index × w` with
`index < 512/w`: 8 values at *w* = 64, 16 at 32, 32 at 16, 64 at 8. **A single 6-bit index
field covers all four**, and the decoder recovers the offset with one variable shift —
`offset = index << log2(w)` — where `log2(w)` is already the width code. No adder, no table.

**The task's ~52 B figure is F3 without the offset compression** (32 × (9 + 3 + 1) = 416 bits =
52 B). Compressing the offset takes the same structure to **40 B**. Both are recorded because
52 B is the number in the brief.

### 2.4 RECOMMENDED FORMAT — F2, with a lazy `f` half

```
  MAP IMAGE, as it sits in the function's read-only code region

  +0    header            4 B     magic/version : 8
                                  flags         : 8   bit0 = f-half present
                                  bits_used     : 10  peak liveness, 0..512  (cons-C17)
                                  names_defined : 6
  +4    x-half           36 B     32 entries x 9 bits, entry n describes  x<n>
  +40   f-half           36 B     32 entries x 9 bits, entry n describes  f<n>   (present iff flags bit0)

  entry = { width_code : 3 , index : 6 }
          width_code  0 = UNDEFINED (a reference to this name traps -- cons-C14)
                      1 = 8 bits    2 = 16    3 = 32    4 = 64
                      5,6,7 = reserved, and a map using one is rejected at fill (S 2.5)
          bit offset  = index << (width_code + 2)      for codes 1..4
```

| function kind | map image | line reads on a cold fill |
|---|---|---|
| integer-only (the day-one path, `-march=rv64ima`, `register-map.md` §3.10) | **40 B** | **1** |
| uses `F`/`D` | **76 B** | **2** |

**Why F2 and not F0.** F0 is the only format that expresses everything canon I2 names — a
48-bit pointer beside a 12-bit index and a 3-bit tag, exactly the packing the rejected
mechanism could do and design A cannot (`register-map.md` §9.3). It costs 128 B, two lines
always, and a decoder that must shift by an arbitrary offset **across a 64-bit word
boundary** — which is `Context512::read`'s two-word splice path (`NMFCRegLayout.h`, the
`word[w0 + 1] << got` branch), i.e. an extra 64-bit shifter and an OR on the register read
path, in a machine where design A was able to delete that branch as dead code. **F2 buys the
straddle-free property back**, and with it the byte-enable write path (no read-modify-write,
no merge, `register-map.md` §3.9). §2.6 states exactly what F2 gives up to get it, and §15 Q2
puts F0-versus-F2 to the user as a ruling rather than deciding it here.

**Why 64 entries and not 63.** Direct indexing by the 6-bit (namespace, number) pair. The
`x0` entry is present and ignored (`hasZero` in `RegLayout` is the same idea); one wasted
9-bit entry is cheaper than a subtractor in the index path.

### 2.5 The non-overlap rule, and the guarantee design A cannot give

> **NO TWO DEFINED NAMES IN ONE MAP MAY OVERLAP.** The tile verifies this **once, at fill**,
> and a map that violates it is an **illegal-map trap** on the context that caused the fill.

Verification is a 512-bit occupancy accumulate over 64 entries — one 9-bit decode and one
range-set per entry, ~64 cycles of a small state machine, **off the issue path**, paid once
per (tile, function) (§4.5 says how rarely that is). It is the same computation `annotate`
must already do to emit the map (**M7**, §8.3).

**What this buys is the single largest technical difference between B and A.** `register-map.md`
§9.5 and its **M6** row score design A as:

> "**NOT met as a machine guarantee; re-homed to build time** … a total map has nothing to fire
> on. Over-liveness is caught only by `annotate`'s placement-disjointness check, and its
> run-time failure mode is a **silent wrong result**."

Under B both halves of cons-C14 come back:

| error | design A | design B |
|---|---|---|
| a name the function was not allocated | every name is always defined ⇒ **undetectable** | `width_code = 0` ⇒ **traps at decode**, exactly as `NMFCTile.cc:460-475` does today |
| two live values on overlapping bits | `d0` and `w0`/`w1` are the same bits by construction ⇒ **undetectable** | overlapping ranges are **rejected at fill**; the only remaining error is two values sharing *one* name, which is a use-after-free of a register and is the ordinary allocator bug every machine has |
| a stack access (`sp` = `x2`) | illegal ISA-wide (A's rule 2) | **undefined in the map** unless the function asked for it ⇒ traps; per function rather than ISA-wide, and I7 is enforced the same way |

**The cost of the rule, stated:** a compiler may not give two names with disjoint live ranges
the same bits. That is not a real loss — an allocator that wants two names to share bits should
give them **one name**, which is coalescing, which every register allocator already does.

### 2.6 What F2 cannot express, in one place

- **A non-power-of-two width.** A 48-bit pointer is charged 64; a 12-bit index is charged 16.
  F0 places both exactly. *(F1 also fixes the offset but not the width, so it does not help.)*
- **An unaligned placement.** Under F2 a 32-bit value starts at a multiple of 32.
- **Sixty-four 1-byte NAMES.** 64 slices need 64 names; `x0` is the hardwired zero, so **63**
  are nameable. **#232's "64 1-byte regs" misses by exactly one NAME — but not by one value.**
  `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` Sixty-four live bytes are expressible under every scheme here, A included: they
  pack eight per 64-bit name and are reached by shift-and-mask through a scratch name at ~2-3
  ops per access. Design A names **0** of the 64 directly and B names **63**, and that
  difference is **instruction count**. §12.1, corrected.

---

## §3 WHERE THE AUTHORITATIVE COPY LIVES

### 3.1 The place is already built: the function's own duplicate region

CANON.md:2113 gives the page type, verbatim from `inc/nmfc/nmfc_trace.h:127-145`:

> **DUP** = `CODE = 2` — "*NMFC mode, and **replicated on every channel**. One virtual address,
> N physical pages — one per channel. Translating it is where the tile gets chosen … only sound
> because the pages are read-only.*"

and DESIGN §26.1 (D:2553-2600) records that **`.rodata` now shares that region with `.text`**:

> "`nmfc.ld` now places `.rodata` with `.text` inside a declared `__dup_start`/`__dup_end`
> span, and `tile_memory.dup_region()` reads those symbols … A grain carries one *type*, not
> one section, and `.rodata` and `.text` are the same type."

So a read-only per-function constant beside the function's code is **already a page type this
machine has, already replicated on every tile, already local to every tile that can run the
function, and already the thing whose I-cache hit rate is measured at 100.00%**
(CANON.md:8247, four tiles). Nothing new is allocated, no new page type is invented, and the
map is never a foreign access — which matters, because on this machine there is no such thing
as a slow foreign access: `issueLoad` translates, and if the answer is not this tile it
**migrates** (DESIGN §26.1). A map on a striped page would migrate a context to fetch its own
decoder.

**This is not the answer to the 2026-09-03 ruling and must not be offered as one.**
`register-map-context.md` §6 states the prohibition in terms: *"A proposal may not answer the
ruling by pointing at duplicate pages again."* The objection was **simultaneity**, not
availability. §13 answers the objection on its own ground.

### 3.2 Variant P1 — a header immediately before the entry PC (RECOMMENDED)

```
    entry_pc - 80 :  ... map image (76 B, or 40 B with no f-half) ...
    entry_pc -  4 :  back-pointer: the image's own byte length          <-- fixed position
    entry_pc      :  first instruction of the function body
```

- **Found by:** `map_addr = entry_pc - 4 - len`, one load of the length word and one of the
  image. In practice both live in the same 64 B line as the first instructions and the *whole
  fill is one line access* for an integer function whose entry is not within 40 B of a line
  boundary; two otherwise. Worst case 3 lines (length word on one line, a 76 B image spanning
  two more).
- **Self-identifying:** the image's address *is* the entry PC minus a constant, so a map
  fetched from `E - 4 - len` is by construction the map of the function at `E`. No tag needs to
  be stored in the image and no forgery is possible.
- **Costs:** the linker must emit the image immediately before each entry point and must not
  let a hot instruction line be evicted by it (it will not: it is on the same line as the entry).
  `annotate` already knows function boundaries (`tools/nmfc/annotate.cc` walks per function and
  calls `die()` per function, `:524-559`), so the emitter has the information it needs.
- **The one thing it does not give:** it is reachable **only from the entry PC**. A context
  arriving by migration has a resumption PC (Finding 1), so P1 alone cannot serve a cold
  arrival. §4 supplies the entry PC; P1 supplies the map once the entry PC is known. **They
  are two halves of one mechanism and neither works alone.**

### 3.3 Variant P2 — the user's page-header cheat, priced

> "You could cheat and keep the map in the first few bytes of every instruction page, such that
> it is always available if you just zero out the page-offset bits of the PC at any traversal
> location. The caveat there is that if the instruction memory crosses a page boundary, you
> incur an unnecessary reload of the reg map."

`map_addr = pc & ~(page_size - 1)`. **The page is 4 KiB**: `src/nmfc/nmfc_vmem.cc:72-73`
defaults `page_size` to 4096 and `log2_page_size` to 12, and `:77-81` requires
`grain_bits >= log2_page_size` with `pages_per_grain` derived above it — so the grain `G`
(1 MiB at `N` = 4) is the *interleaving* granularity and 4 KiB is the *translation* one.
`[CONFIGURATION]`

**It solves Finding 1 outright** — the identity is derivable from any PC, including a
resumption PC — and that is a genuine strength no other variant has. Its four prices:

| price | arithmetic |
|---|---|
| **space in the code image** | 40–76 B per 4 KiB page = **1.0–1.9%** of the code region, and a duplicate page's physical footprint is `M = N × G` (CANON.md:264), so at `N` = 4 the cost is paid four times over |
| **one map per PAGE, not per function** | Every function sharing a 4 KiB page must share one map. Either (a) the compiler emits a **common superset layout** for all of them — which constrains the allocator of every function on the page to a layout chosen for the worst of them, and makes one function's admission depend on its neighbours' — or (b) **every function is page-aligned**, which costs up to 4 KiB of internal fragmentation per function, ×`N` physical copies = **16 KiB per function at `N` = 4**. (b) is the honest choice: at a handful of functions per tile it is a few dozen KiB, and it makes the cheat exact. |
| **cross-page reload** | The user's own caveat. Any control transfer to a different page invalidates the derived address. **A function of `P` pages pays a reload on every inter-page transfer.** With page-aligned functions (option b) a function ≤ 4 KiB — 1024 instructions — pays **zero**, and canon refuses to assume functions are small: user #78, verbatim, "*are you saying each function consumes no more than 8 instructions? I find that unlikely. are you serious?*" (CANON.md:2992). So "zero" is a property of a build, not of the machine. |
| **the thrash case, which the user's sketch does not name** | A **loop straddling a page boundary** alternates between two headers and reloads the map **every iteration**. A one-entry derived-map register thrashes at 100%. The fix is a ≥2-entry cache — i.e. P2 **does not remove the cache, it only removes the identity problem**, and the cache §4 builds is what it needs anyway. |

**Verdict: P2 is kept, and kept exactly where the user put it — as the last resort, and now
with a job.** §4.3 uses it as **the cold-arrival fallback only**: the path taken when a tile
has never seen the function and cannot otherwise learn its entry PC. In steady state it is
never consulted, so its cross-page reload cost is never paid.

### 3.4 The three placements side by side

| | P1 — before the entry PC | P2 — page header | P3 — a separate map section |
|---|---|---|---|
| reachable from a resumption PC | **no** | **yes** | no |
| self-identifying | **yes** (address = `E - 4 - len`) | only with page-aligned functions | needs a tag word |
| lines on a cold fill | 1–3 | 1–2 | 1–2 |
| code-space overhead | ~0 (shares the entry line) | 1.0–1.9% × `N` copies | ~0 |
| constrains the linker | emit before entry | page-align functions | a section, `__map_start`/`__map_end` — the same shape DESIGN §26.1 already uses for `__dup_start`/`__dup_end` |
| forces functions to share a map | no | **yes**, unless page-aligned | no |

**Recommendation: P1 as the authority, P2 as the cold-arrival fallback, P3 not needed** — it
buys nothing P1 does not and adds a section and a tag.

---

## §4 THE PER-CORE CACHE

### 4.1 The user's shape, and where it breaks

The user's requirement 1 is an **array indexed by context handle**, entry = the map. Requirement
3 adds a tag: *"when a handle is reused, there is some sort of way to detect whether the
function is identical or not."* So the entry is `(identity, map)` and a lookup is
`array[handle]`, hit iff `array[handle].identity == arriving identity`.

**Two things have to be true for that to work, and neither is true as written:**

1. **The arriving identity has to exist.** Finding 1: after a migration the context carries a
   handle and a resumption PC, and the resumption PC does not name the function unless
   something maps PC ranges to functions. §4.2.
2. **The identity has to be re-checked on `CONT`.** Finding 2: `CONT` changes the function under
   a constant handle, and it "**CANNOT FAIL**" — so a design that checks only at admission
   will run the successor of a `CONT` chain against its predecessor's map. That is a **silent
   wrong answer**, the failure mode cons-C14 and cons-C15 exist to forbid.

### 4.2 What the identity can be — three mechanisms, priced

| | **ID-1 — PC-derived (P2's page header)** | **ID-2 — carried in the envelope** | **ID-3 — asked of the origin host** |
|---|---|---|---|
| identity | the code page number, `pc >> 12` | the entry PC, 8 B added to `MigrationEvent` | the entry PC, fetched from the FTU that owns the handle |
| bytes on the wire | **0** | **+8 B.** `MigrationEvent::SIZE_BYTES = NMFC_CTX_BYTES + 8 = 72` (`NMFCFabric.h:107-108`) becomes 80 — **I11's 72 B as *defined* is untouched** (it is "64 bytes of register file plus an 8-byte program counter", DESIGN §25.7), but the message SST actually charges the link grows by 11%, and R4 makes SST's byte model the design's | **0** |
| latency on a cold miss | one local line read | one local line read | **one fabric round trip** — 8 cycles/hop `[CONFIGURATION]`, `nmfc_4tile.json:716` — plus one local line read |
| needs page-aligned functions | yes, to be exact | no | no |
| new message type | no | no | **yes** — `MAPREQ(handle) → entry_pc`, and cons-C23 leaves only `funct7` `0x6`/`0x7`, so it must be a **fabric control message, not an instruction** (it is; `RESUME`'s claim on the opcode space is untouched) |
| works for `CONT` | yes — the successor's PC is on the successor's page | **no** — `CONT` never crosses the fabric, so no envelope is involved; the tile must derive the successor's identity locally | yes |

**And a correctness hole in all three that must be recorded: the handle is not unique across
address spaces.** The FTU is host-side, handles are FTU entry indices, and CANON.md's placement
key is `(asid << 48) | vgrain` with ASID-tagged TLBs (R12, **I3**). **`MigrationEvent` carries
no ASID** (`NMFCFabric.h:98-105`) and neither does `TileContext` (`NMFCTile.h:44-123`). So a
map cache tagged only by handle, or only by entry PC, can be hit by a context of a *different
address space* with the same handle value or the same virtual entry PC. §15 Q4 puts it to the
user; the cheap fix is to include the ASID in the tag and in the envelope, which costs 2 B and
is needed for translation anyway.

**Recommendation: ID-1 for the cold path, and no envelope growth.** It is the only one of the
three that also answers `CONT` without a message, it costs nothing on the wire, and §4.5 shows
the cold path is taken so rarely that a fabric round trip (ID-3) would also have been
acceptable. ID-2 is the one to take **if** the user would rather spend 8 B than page-align
functions; §15 Q3.

### 4.3 The recommended structure

Three parts. Only the third is consulted at decode.

```
  (1) RFT -- the resident-function table.  SHARED, one per tile, F entries (F = 8).
      entry:  valid | asid | lo_pc | hi_pc | map_index          ~ 8 + 16 + 48 + 48 + 3 bits ~ 16 B
      total:  8 x 16 B = 128 B per tile.
      Consulted: at admit, at migration arrival, and at every CONT.  NEVER at decode.
      Lookup:  a range match on the context's PC  (a 8-entry CAM; 8 comparators of 48 bits).

  (2) MAP FILE -- SHARED, F entries of one F2 map image.
      8 x 72 B = 576 B per tile, replicated W = 4 ways (S 6.4) = 2.25 KiB.
      Written once, at fill.  Read-only thereafter.

  (3) PER-CONTEXT MAP INDEX -- 3 bits + 1 valid bit, in the tile-local context slot,
      beside  ibufPC / dbufReg / holdsLine  (NMFCTile.h:69-95).
      4 bits x C.  At C = 1024: 512 B per tile.
      This is the only part read at decode, and it is read exactly the way the PC is.
```

**The arrival path, in order:**

```
   admit(inv)            :  lo = inv.pc  is the entry PC          (NMFCTile.cc:544-580)
   handleMigration(mig)  :  only mig.pc, a resumption PC          (NMFCTile.cc:1368-1402)
   CONT rPC              :  a new PC, same handle                 (CANON.md:6223-6231)
        |
        v
   RFT lookup:  match asid and  lo_pc <= pc < hi_pc
        |                                   |
      HIT                                 MISS
        |                                   |
   c.mapIndex = entry.map_index      derive the map address:
   c.mapValid = 1                      admit  -> P1:  entry_pc - 4 - len
   0 memory accesses                   arrival-> P2:  pc & ~4095   (S 3.3)
   0 extra cycles                     fetch 1-2 local lines
                                      verify non-overlap (S 2.5)
                                      allocate an RFT entry (LRU) + a map-file slot
                                      write it into ALL W replicas
                                      c.mapIndex = slot
```

**Why the RFT is keyed on a PC *range* and not on a handle.** It is the only key that is
derivable at every one of the three entry points above, it is shared by every context of the
function, and it is the structure the record already anticipated: DESIGN §25.7 D:2560-2567
called for "*one small table entry beside the instruction cache, indexed by the function a
context is running*", and `NMFCTile.h:448-450` is the stub for it (**M1**). **Design B keeps
M1; design A deletes it.** What the record's version lacked, and what this adds, is the
identity tag, the fill path, and the cost model.

**`hi_pc` comes from the map header**, so the RFT's range is exact after the first fill. Before
it, the entry is created with `hi_pc = lo_pc + len_from_header`.

**Why F = 8.** Canon's own economy for a structure of exactly this shape, CANON.md:5366-5372,
verbatim: *"**Shared, not per-context** — that is the whole economy of it. Every context runs
the same replicated code, and **a tile runs a handful of distinct functions**, so one entry
serves every context executing that branch, the structure is warm almost immediately, and **it
does not scale with `C`**."* That is the shared BTB, sized at 64 entries ≈ 1 KiB against 22 KiB
of context state at 256 contexts. The map file is **576 B against the same 22 KiB** — smaller
than a structure canon has already accepted, for the same reason. `F` is `[CONFIGURATION]`; the
shape (shared, small, independent of `C`) is the claim.

### 4.4 Fill, eviction, and the replicas

- **Fill is off the issue path.** The context is already `FETCHING` on arrival
  (`NMFCTile.cc:1397-1399` sets `state = FETCHING` and calls `requestFetch`), so the map fetch
  overlaps the instruction fetch it must wait for anyway. On the measured configuration the
  instruction fetch hits at **100.00%** (CANON.md:8247) and the map line is in the same region;
  the map fetch is a second access to the same local cache, not a serialised second miss.
- **Eviction is LRU over `F` entries and is not a correctness event.** Evicting a map whose
  contexts are still running is safe **only if no context holds a stale index**, so eviction
  must clear `mapValid` on every context pointing at the slot — a `C`-wide broadcast of a
  3-bit compare. At `F` = 8 and a handful of resident functions this never fires; it must still
  be built, because "never fires" is not a guarantee. **Alternative that avoids the broadcast:
  refuse to evict a slot with a non-zero reference count and stall the arriving context** —
  which is admissible here because a context can always wait (it is `FETCHING` anyway) and
  because refusing rather than evicting is the machine's existing idiom for the FTU (**I.5**).
  **Recommendation: refcount and refuse.** §15 Q5.
- **The `W` replicas need no coherence protocol.** A map-file slot is written once, before any
  context indexes it, and is read-only until its refcount reaches zero. The fill writes all `W`
  copies in the same cycle from one fill buffer.

### 4.5 Function-keyed against handle-keyed — the arithmetic

This is Finding 3, priced on the one measured stress workload in the record.

**The workload** (CANON.md:4945-4960, "*these are settled numbers*"): the reshaped BFS kernel,
four tiles, *"the function does **seven data loads** and returns the sum via **END with the
return bit**"*, **196,904 migrations for 262,143 loads**, 0.75 per memory operation, zero
stores, balance 25.0% on every tile.

```
   invocations            = 262,143 loads / 7 loads per invocation      = 37,449
   migrations/invocation  = 196,904 / 37,449                            = 5.26
   tile visits/invocation = 1 + 5.26                                    = 6.26
   distinct tiles visited = 4 x (1 - (3/4)^6.26)                        = 3.34
        (uniform over 4 tiles; the 25.0%-on-every-tile balance is the measured
         justification for uniformity.  ESTIMATE, not a measurement.)
```

| | **handle-keyed** (one entry per live handle) | **function-keyed** (one entry per resident function) |
|---|---|---|
| cold fills, whole run | 37,449 × 3.34 = **≈ 125,080** | 4 tiles × 1 function = **4** |
| fills per migration | **0.635** | **0.00002** |
| extra local line reads per data load | **+0.48** (integer map, 1 line) to **+0.95** (with an `f` half) | 0 |
| added arrival latency | one local cache access on **63.5% of migrations**, against a measured arrival cost of **2.2–2.3 cycles** (CANON.md:1335-1336, 4261, 8247) — i.e. **roughly a doubling of the arrival cost** at that hit rate | none after the first invocation |

**In the budget regime** — canon's own target, *"~1 migration per 1,000 instructions … an
aspiration, not a gate. No measured run in the record meets it"* (CANON.md:1002) — an
invocation of `I` instructions migrates `I/1000` times and cannot visit more than `N` = 4
tiles, so **handle-keying costs at most 4 fills per invocation** however long it runs: at
`I` = 3,000 that is one fill per 750 instructions, ≈0.13% of instructions, and one extra local
access on ≈4 of every 3 migrations. **Function-keying still costs 4 fills for the whole
program.** So the two regimes differ by three orders of magnitude in how much the choice
matters, and in *both* the function-keyed form is the cheaper one.

> **Conclusion. The handle index is not viable and does not need to be** — the tag the user's
> requirement 3 already demands is a strictly better key than the handle it would have tagged.
> The user's other requirement — 4, *"excluding startup or a cold touch of an nmfc core, the
> function should never have to fetch its regfile map"* — is met **only** by the function-keyed
> form: under handle-keying, every new invocation is a cold touch.

### 4.6 What the context slot gains, exactly

```c
  // NMFCTile.h, in TileContext, beside ibufPC / dbufReg / holdsLine
  uint8_t mapIndex : 3;   ///< which map-file slot this context decodes through
  uint8_t mapValid : 1;   ///< 0 => the next issue takes the RFT path first
```

**4 bits.** Per-context state goes from ~87 B (DESIGN §25.7 D:2434-2437: *"64 bytes of register
file, ~13 of instruction buffer, ~10 of data buffer — about 87 bytes"*) to 87.5 B: **+0.6%**.

---

## §5 MIGRATION CARRIES NOTHING EXTRA — AND WHAT THE RE-ACQUIRE COSTS

### 5.1 The 72 bytes stand, and here is the check

**I11, CANON.md:1332, verbatim:** *"72 bytes of register file and PC against the **64-byte
line** a foreign access would have cost — and **the two are alternatives, never both**."*
DESIGN §25.7 D:2574-2577 decomposes it: 64 B of register file + an 8-byte PC.

Tier 4 agrees and is worth quoting because it settles what is *not* in the 72:
`MigrationEvent` (`/mnt/md0/NMFC-Rev/src/nmfc/src/NMFCFabric.h:94-123`) declares `handle`,
`pc`, `ctx[8]`, `faultAddr`, `origin`, `from`, `to`, `wantsReturn`, and then

```c
  /// Invariant 11's 72 bytes: the register file plus the program counter.
  static constexpr uint32_t SIZE_BYTES = NMFC_CTX_BYTES + 8;
```

— so `handle` and `origin` are **envelope, already travelling, and already free**. Design B
adds **nothing** to either the 72 or the envelope: the map does not travel, the map's address
does not travel, and the identity is derived at the destination (§4.2 ID-1). cons-C6 is met
exactly, **M11** is unchanged, and the SST byte model (R4) charges what it charges today.

**The one thing that must be said:** if the user prefers ID-2 (§4.2), the envelope grows 8 B
and `SIZE_BYTES` must grow with it or the model lies. That is a ruling (§15 Q3), not a detail.

### 5.2 The re-acquire, priced in the budget regime

The destination re-acquires by an RFT lookup (§4.3). Two outcomes:

| outcome | when | cost |
|---|---|---|
| **RFT hit** | any tile that has run this function before, for any context, ever | an 8-entry range CAM compare, in the arrival cycle, **0 memory accesses, 0 added cycles** — the context is `FETCHING` regardless (`NMFCTile.cc:1397-1399`) |
| **RFT miss** | the first arrival of *this function* on *this tile* | 1 local line read (integer map) or 2 (with an `f` half), overlapped with the instruction fetch the context is already waiting for; plus a ≤64-cycle non-overlap verify, off the issue path |

**Budget regime** (CANON.md:1002, ~1 migration per 1,000 instructions, *"an aspiration, not a
gate"*): total misses over the life of the program are bounded by `N × F` — **tiles × distinct
functions**, and by nothing else. At `N` = 4 `[CONFIGURATION]` and a handful of functions, that
is **tens of line reads for an entire run**. Against the measured arrival cost of **2.2–2.3
cycles** (CANON.md:1335-1336, 4261; 8247's row: *"migration cold start — 2.2–2.3 cycles,
100.00% fc I-cache hit, four tiles"*), the steady-state addition to arrival is **zero cycles**,
and the transient is one local access on ≤ `N × F` arrivals out of every migration in the run.

### 5.3 The re-acquire, priced in the stress regime

The workload where a context migrates every 1.33 loads — CANON.md:4957, **196,904 migrations
for 262,143 loads, 0.75 per memory operation**, which is *"inside invariant 5's legitimacy
ceiling by the same margin as the BFS run"*:

| | fills over the whole run | per migration | added arrival cost |
|---|---|---|---|
| **function-keyed (design B as recommended)** | **4** | 0.00002 | **0 cycles** on 196,900 of 196,904 migrations |
| handle-keyed (the sketch as written) | **≈125,080** (§4.5) | 0.635 | one local access on 63.5% of migrations — against a 2.2–2.3-cycle arrival, **roughly a doubling** |

**The number to take away: on the record's worst measured migration rate, design B as
recommended pays four line reads for the entire run.** The re-acquire is not a per-migration
cost, it is a per-(tile, function) cost, and the tile count is 4.

### 5.4 Why the map must not travel, stated positively

Carrying a 40–76 B map alongside 72 B would grow the migration message by **56–106%**, and I11
is a *parity* argument — 72 B against the 64 B line a foreign access would have cost. At 128 B
the parity is gone and the invariant's own justification fails. **This is the strongest reason
the user's "retrieved post-migration" instinct is right**, and it is stronger than the
convenience argument: the map cannot travel without breaking I11, so it must be re-acquired,
so the whole design turns on making re-acquisition free. §4.5 is that argument.

---

## §6 THE PORT-WIDTH QUESTION

### 6.1 The user's concern, and the shape of the problem

> "it would need **a port for the width of the machine** or we would need contexts aligned to
> widths so the entire system could be **banked-per-width**"

The concern is exact. `W` = 4 (`nmfc_4tile.json:957`, `NMFCTile.h:138`) `[CONFIGURATION]`, and
canon's barrel rule is *"one context issues one instruction, then yields; several contexts are
in the pipe at once, and **never the same context twice**"* (DESIGN §25.2). So in one cycle
`W` = 4 pipes each decode an instruction **of a different context**, hence potentially of a
different function, hence through a different map. Worst-case operand count is four —
`fmadd.d rd, rs1, rs2, rs3` — so **16 map entries could be demanded in one cycle, from up to 4
distinct maps.** That is the port problem.

### 6.2 The decode path, drawn

```
   instruction word
        |
        +-- opcode ----> namespace of each operand field (x or f)   [fact-C9: the opcode types it]
        |                 operand ROLE and execution width          [W1/W1b, register-map.md S3.4]
        |
        +-- rs1[19:15], rs2[24:20], rs3[31:27], rd[11:7]
                 |
                 v
        (namespace, number)  --> MAP  -->  (offset, width_code)
                                            |
                                            v
                             offset = index << (width_code + 2)
                                            |
                                            v
                    CONTEXT 512 : select the 64-bit word (offset[8:6]),
                                  select within it (offset[5:3]),
                                  extend to the operand width      [W2]
                                            |
                                            v
                                          ALU / FPU
```

Everything after `MAP` is identical to design A (`register-map.md` §3.9) and is priced there at
**+2 mux levels** on the register read path. **The only new thing in design B is the box marked
`MAP`, and §6.3–§6.5 are entirely about what that box costs.**

### 6.3 B1a — a per-context map register (the option the brief names)

Give every context its own copy of its function's map, in the tile-local slot. Then the map
read is exactly as wide as the register read and needs no cross-context ports.

| map size | `C` = 128 | `C` = 256 | `C` = 1024 |
|---|---|---|---|
| 44 B (F2, integer only) | 5.5 KiB | 11 KiB | **44 KiB** |
| 52 B (the brief's F3-uncompressed figure) | **6.5 KiB** | 13 KiB | 52 KiB |
| 76 B (F2, both namespaces) | 9.5 KiB | 19 KiB | **76 KiB** |

`[CONFIGURATION — `C` is 1024 in `nmfc_4tile.json:956`, 256 in the ramulator config and in
`NMFCTile.h:137`, swept 64/128/256/512 in N.4.]`

**Against what.** Per-context state today is **~87 B** — *"64 bytes of register file, ~13 of
instruction buffer, ~10 of data buffer — about 87 bytes. That is 22 KiB per tile at 256
contexts and 87 KiB at 1024, the latter being a structure that competes with the LLC slice
rather than sitting beside it"* (DESIGN §25.7 D:2434-2438).

> **At `C` = 1024 a 76 B per-context map is +76 KiB against 87 KiB — it very nearly DOUBLES the
> per-context state of the tile, to buy a copy of a table that is identical for every context
> running the same function.** DESIGN §25.7's own sentence about the rejected mechanism —
> *"many contexts run the same function, so it is one small table entry … It adds nothing to
> the 512 bits and **does not scale with `C`**"* — is the argument against B1a, and it was
> written before B1a was proposed.

**And it does not actually solve the port problem it was meant to solve.** To read four
arbitrary entries out of a 576-bit row you either read the whole row (4 pipes × 576 bits =
**2304 bits/cycle of map read**, against 4 × 3 × 64 = 768 bits/cycle of *operand* read — the
map read is **three times the operand read**) or you build a four-ported 64-entry array per
context, which is a 4-read-port 76 KiB SRAM. **B1a is rejected.**

### 6.4 B1b — a shared map file, a 3-bit index, and resolution one window early (RECOMMENDED)

Three moves, in increasing order of how much they buy:

**(i) Share the file, index it per context.** §4.3: the map file is `F` = 8 entries × 72 B =
**576 B**, and a context carries a **3-bit index**. Per-context cost 4 bits. The file is small
enough to be **flops, not SRAM**, and small enough to replicate: `W` = 4 replicas = **2.25 KiB
per tile, independent of `C`**.

| | `C` = 128 | `C` = 256 | `C` = 1024 |
|---|---|---|---|
| per-context index (4 b × `C`) | 64 B | 128 B | 512 B |
| map file, `W`-replicated | 2.25 KiB | 2.25 KiB | 2.25 KiB |
| RFT | 128 B | 128 B | 128 B |
| **total** | **2.4 KiB** | **2.5 KiB** | **2.9 KiB** |
| against per-context state (87 B × `C`) | 10.9 KiB → **+22%** | 21.8 KiB → **+11%** | 87 KiB → **+3.3%** |

Compare the structure canon already accepted for exactly this economy: the shared BTB, *"64
entries of tag + target + a last-outcome bit ≈ **1 KiB against 22 KiB** of context state at 256
contexts"* (CANON.md:5370-5372). **The map file is 576 B against the same 22 KiB.**

**(ii) Bank the map file BY NAME.** Hold it as 64 banks — one per architectural name — each 8
entries × 9 bits = **9 B**. An operand read is: the (namespace, number) pair selects the *bank*;
the context's 3-bit index selects the *row*. That is an 8:1 select of 9 bits, **3 mux levels**,
not a 512:1 select. The four operand fields of one instruction hit at most four banks; two
pipes wanting the same (bank, row) share one read because the answer is identical. **This is
the user's own "banked" intuition, applied to the map instead of the register file — and unlike
banking the register file (§7.6) it costs nothing, because the thing being banked is 576 B.**

**(iii) Resolve at FETCH-BUFFER FILL, not at issue — and the port problem disappears.** Canon
H.5, CANON.md:5341-5347: *"**Per-context single-entry fetch buffer**, filled with the context's
next instruction **at the end of each dispatch** … Cost ≈13 bytes per context (one instruction
word, a PC, a valid bit)."* Tier 4: `NMFCTile.h:79-81`, `ibufValid` / `ibufPC` / `ibufInsn`.

> **The instruction word is in the context's own buffer a whole re-issue window (`Dp` = 8
> cycles) before it issues.** Do the map lookup there. Widen the buffer by the four resolved
> fields — 4 × (9-bit offset + 3-bit width code) = **48 bits = 6 B** — and at issue the offsets
> are **already present**: zero map reads, zero mux levels, zero ports on the issue path.

- **The map file then needs one read group per instruction *fetched*, not `W` per cycle.** A
  fetch return serves one context; the barrel spreads fetches over the whole window. The
  demand on the map file falls from 16 entries/cycle to ~4.
- **Cost:** fetch buffer 13 B → 19 B; with (i)'s 4-bit index that is **+6.5 B per context**, so
  per-context state goes 87 B → **93.5 B (+7.5%)** = 6.5 KiB at `C` = 1024. Total added state at `C` = 1024: 6.5 KiB + 2.25 KiB + 128 B ≈ **8.9 KiB against
  87 KiB (+10%)** — an order of magnitude below B1a's +87%, and the issue path is *cleaner* than
  B1a's, not merely cheaper.
- **Correctness obligations, both small:** a context with `mapValid = 0` does not pre-resolve,
  and re-resolves after its fill; and a `CONT` or an arrival changes the PC, which already
  invalidates the buffer and issues a fresh `requestFetch` (`NMFCTile.cc:1397-1399`), so a
  stale pre-resolution cannot survive a map change. **A map-file slot may not be rewritten
  while any context holds a pre-resolved field from it** — which is §4.4's refcount, now
  carrying a second job.
- **This is the same trade canon already made for the BTB and for the same reason**: *"What the
  BTB supplies is **a fetch address a window earlier**"* (CANON.md:5361). Here the fetch buffer
  supplies **a register offset a window earlier**.

> **ANSWER TO THE USER'S QUESTION.** The map cache does **not** need a port for the width of
> the machine. It needs a port for the **fetch rate** of the machine — one context per fetch
> return — because the barrel already buffers each context's next instruction a window ahead,
> and that is where the lookup belongs. The width of the machine never sees the map at all.

### 6.5 `Dp`, and the one number that would actually bite

Canon H.2 makes the re-issue depth a first-class parameter through `C >= W × (Dp + L/I)`
(CANON.md:192-193; DESIGN §25.2 D:2226-2232 works it: *"At `N`=4, `D`=8, `L`~100, `I`~4 this is
~132"*). A deeper decode would raise `Dp`, raise the required `C`, and multiply the tile's
dominant SRAM term.

- **Under (iii), `Dp` is unchanged**: the map lookup happens in the fetch path, which already
  has a whole window of slack by construction, and the issue path gains nothing.
- **Under (i)+(ii) without (iii)**, the added 3 mux levels sit in a decode stage that already
  decodes a 32-bit instruction word on a 250 ps clock `[CONFIGURATION]`. If they do not fit,
  `Dp` goes 8 → 9 and the floor goes **132 → 136** — +4 contexts, +348 B of context state. That
  is the honest worst case and it is small; it is recorded so that nobody claims "+0 cycles"
  without saying what the +1 would cost.

---

## §7 B2 — CONTEXTS ALIGNED TO WIDTH CLASSES, AND THE SYSTEM BANKED PER WIDTH

### 7.1 What a width class is

A **class** is a fixed layout of the 512 bits, chosen per context, named by a small field in the
context's tile-local slot. Decode becomes `offset = f(name, class)` — a wire pattern, with the
class selecting between a handful of wire patterns instead of the single one design A has.
**There is no table and nothing is fetched.**

### 7.2 A PURE single-width class is unusable, and the reason is one sentence

Complete tilings nameable with 5-bit fields: 8 slices at 64 bits, 16 at 32, 32 at 16, and 64 at
8 — the last needing both namespaces minus `x0`, so 63 of 64 (§2.6).

> **Every offloadable function on this machine dereferences a pointer, and a pointer is 64
> bits.** That is not a preference; it is the machine's premise — I11 exists because the
> function chases data the tile owns, and canon's own decomposition is *"own an edge range and
> **chase scattered vertex values**"* (CANON.md:8703, R25). Under a pure 32-bit class there is
> **no name a load can use as a base address**; under a pure 64-bit class the machine is
> `RegLayout::defaultLayout()`, which is *"fixed aliasing at one width is the SST layout
> again"* (cons-C30) and the formulation I2 exists to forbid (#238: *"Once again, NO. 512 bits
> of context. The context is not 8 regs."*).

**So a class must be a MIXED layout.** Which means B2 is not "contexts aligned to widths"; it is
**"contexts aligned to layouts"**.

### 7.3 Therefore B2 is design A, parameterised

Design A's map — `x8`–`x15` as eight 64-bit tiles, `x16`–`x31` as sixteen 32-bit tiles,
`f`*n* ≡ `x`*n* — is **one class**. B2 is the same machine with `K` of them and a class field.
**Design A is B2 with `K` = 1.** That is worth saying plainly because it means the two designs
the user asked for are not as far apart as their names suggest: **A ⊂ B2 ⊂ B1**, in
expressiveness and in cost, and the user's real question is where on that line to stop.

### 7.4 A proposed class set — `K` = 4, two bits

| class | layout | complete? | what it is for |
|---|---|---|---|
| **0 — D+W** | `x8`–`x15` = 8 × 64 bits; `x16`–`x31` = 16 × 32 bits; `f`*n* ≡ `x`*n* | both tilings complete over 512 bits | **design A verbatim.** Pointers and `int`s — the default |
| **1 — D** | `x8`–`x15` = 8 × 64; `x16`–`x31` illegal | complete | all-64 functions; the `-march=rv64ima` day-one path (`register-map.md` §3.10), where a stock GCC with `-ffixed` reaches exactly this |
| **2 — D+H** | `x8`–`x11` = 4 × 64 (bits 0–255); `f0`–`f15` = 16 × 16 (bits 256–511) | complete | four pointers and sixteen halfwords: level numbers, small counters, indices |
| **3 — D+B** | `x8`–`x11` = 4 × 64 (bits 0–255); `f0`–`f31` = 32 × 8 (bits 256–511) | complete | four pointers and **thirty-two byte names** — a tier design A cannot name **directly** at ISA scope (`register-map.md` §1.3a) and can name here because the byte names only have to cover *half* the file. `[CORRECTED - user ruling 2026-09-03 (liveness)]` ~~unreachable~~ — **A reaches the same bytes by packing**, at ~2-3 ops per access; this class buys directness |

Classes 2 and 3 use the `f` namespace for the narrow tier, which is exactly the fork
`register-map.md` §10 Q6 leaves open — and B2 takes **both sides of it, per context**: class 0
aliases the namespaces, classes 2 and 3 spend them. That is the single largest thing B2 buys
over A, and it costs two bits.

### 7.5 How a context declares its class, and what decode costs

- **Declared at `FORK`**, in the two spare bits of the invocation, or **read from the map header
  of §2.4 at admit** (the header already exists under B1 and a class is 2 bits of it). Under B2
  there is no map to fetch, so the second form needs a two-bit field somewhere in the
  invocation; the first is free.
- **Carried in the tile-local context slot**, 2 bits. At `C` = 1024: **256 B per tile**.
- **On migration:** the class is not in the 72 B, so it is re-acquired exactly as B1's map index
  is — the same RFT, with the map replaced by two bits. **Every argument of §4 and §5 applies
  unchanged and is cheaper.** Or, since it is only two bits, ID-2 becomes attractive: two bits
  in the envelope is not a burden by any reading of the user's requirement 4. §15 Q6.
- **Decode:** `offset` and `width` become a 4:1 mux on the class instead of design A's 2:1 mux
  on `n[4]` — **+1 mux level over design A, resolved from a 2-bit field already in the slot**,
  with none of B1's fetch-buffer machinery. As a ROM: 4 classes × 63 names × 10 bits = **2520
  bits per tile**, shared by every context, combinational, of the same kind as the opcode
  decoder's truth table (design A's is 310 bits).

### 7.6 What "banked per width" would actually mean, and what it costs

The user's phrasing suggests going further: partition the tile's contexts by class so that the
register file and the datapath are **physically banked**, each bank serving one class with one
geometry and therefore one fixed extract network.

**What it buys:** the extract/insert network per bank collapses to a single wire pattern, so the
per-class mux disappears from decode; and a bank's read port is exactly as wide as its class.

**What it costs, and this is the number that decides it:** contexts become **statically
partitioned**, so a workload whose functions are all one class uses `C/K` of the tile.

| | `K` = 4 banks | is the barrel still fed? — floor is `C ≥ W(Dp + L/I)` = **~132** at `W`=4, `Dp`=8, `L`~100, `I`~4 (DESIGN §25.2 D:2226-2232) |
|---|---|---|
| `C` = 1024 `[CONFIGURATION]` | 256 usable | **yes**, but latency tolerance falls from 7.8× the floor to **1.9×** |
| `C` = 256 `[CONFIGURATION]`, the derived count | **64 usable** | **NO — 64 < 132. The tile cannot keep its pipes fed, and `NMFCTile.cc:41-49` already makes `contexts < pipes × depth` fatal** |

> **Verdict: bank the MAP by name (§6.4 (ii)) — that is free. Do NOT bank the CONTEXT ARRAY by
> class.** Static partitioning of the context array turns a workload-shape property (which
> classes the program's functions use) into a hard capacity limit, and at the derived context
> count it falls below H.2's own floor. The class field can and should ride with the context in
> a single unpartitioned array; the geometry it selects is a mux, not a bank.

### 7.7 What B2 loses against B1

- **A function must fit one of `K` layouts.** No per-function packing at all: the width mix is
  chosen from a menu. `register-map.md` §7.1 reports the enumeration — the free map admits
  **~13,091** **directly nameable** packings where a fixed two-width map names **~2,137**
  directly. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` **Neither figure is an admission bound**: every scheme here holds 512
  bits and expresses every width, so the counts measure how often an access is one instruction
  rather than ~2-3. B2's count is bounded above by the union of its `K` classes and is far
  below the free map's; **this document does not compute it and will not invent it.**
- **No non-power-of-two width and no unaligned placement** — the same losses F2 has (§2.6), plus
  the loss of per-function choice.
- **cons-C14 is not restored.** Every name in a class is always defined, so the run-time
  undefined-register trap cannot fire and over-liveness is undetectable — **design A's §9.5
  regression, inherited verbatim.** This is the one place B1 is strictly better than B2, and it
  is the reason §14 recommends B1 where the map's cost can be afforded.
- **`x1`–`x7` and the seven reserved names**: classes 1–3 leave more names unused than class 0
  does, so B2 inherits design A's §10 Q2 unchanged.

---

## §8 HANDLE REUSE, THE IDENTITY CHECK, AND STALE ENTRIES

### 8.1 What a handle is, and when it is reused

`FORK.R rH, rPC, cCTX` — *"try; `rH` = handle, or 0 if no FTU entry is free"* (CANON.md:6097).
*"**`FORK` returns the handle** (#224), or **0** when no entry is free"* (CANON.md:6143). The
handle is an **FTU entry index**; the FTU is host-side, holds 64–1024 entries `[CONFIGURATION]`
(CANON.md:203), *"**refuses rather than evicts**"* (**I.5**), and an entry is freed when its
invocation closes — at `JOIN`, at an ACK for a fire-and-forget entry (CANON.md:6283), or at a
fatal-fault teardown (ruling O7). **The next `FORK` may then be handed the same number for a
different function.** That is the reuse the user's requirement 3 names.

### 8.2 The four events at which identity must be checked

| event | why | what the tile has | check |
|---|---|---|---|
| **`admit`** (`FORK`) | new invocation, possibly a new function on a reused handle | `inv.pc` **is** the entry PC (`NMFCTile.cc:565`) | exact: RFT range match, or fill from P1 at `entry_pc - 4 - len` |
| **migration arrival** | the destination may never have run this function | `mig.pc`, a **resumption** PC (Finding 1) | RFT range match; on a miss, §4.2 ID-1/2/3 |
| **`CONT` / `CONT.M`** | **Finding 2** — a new function body under an unchanged handle, and `CONT` *cannot fail* | the successor PC, in the instruction | RFT range match on the new PC. **This is the check a handle-indexed array cannot make, because the handle did not change** |
| **`RESUME`** (privileged, after a recoverable fault) | the context was parked in the FTU entry (CANON.md:2190) and is re-admitted, possibly to a different tile | the parked PC — again a resumption PC | identical to migration arrival |

**A design that checks only at `admit` is wrong on three of the four**, and its failure mode is
a context decoding through another function's map: **a silent wrong answer**, which cons-C14
and cons-C15 jointly forbid and which the record's own remedy discipline (cons-C16) requires be
stated rather than softened.

### 8.3 What happens on a stale entry — the rules

> **R1 — A tag mismatch is a MISS, never a guess.** The cached entry is not used, not partially
> used, and not repaired. The context takes the fill path.
>
> **R2 — A miss with no derivable identity is a FAULT, not a default map.** If the RFT misses
> and neither P1 (entry PC known) nor P2 (page header present) nor ID-3 (host reachable)
> yields an identity, the context takes an **illegal-map fault** to the host through the FTU,
> exactly as any other tile fault does (**I.6**, CANON.md:2200-2205). There is no
> `defaultLayout()` fallback. `RegLayout::defaultLayout()` exists in the tree
> (`NMFCRegLayout.h`) and **must not be reachable at run time under this design** — it is the
> 8 × 64 packing, and reaching it silently would be exactly the reversion #238 forbids.
>
> **R3 — A map-file slot may not be rewritten while any context references it.** §4.4's
> refcount; §6.4 (iii) gives it a second job, because a context may hold *pre-resolved fields*
> from a slot even when it is not currently issuing.
>
> **R4 — The map file is a cache of read-only memory and needs the I-cache's invalidation
> discipline.** Maps live on duplicate pages, which are *"read-only… kernels also have their
> writes duplicated"* (user #271, CANON.md:2154-2158) — so a program that loads new code writes
> through the kernel, and the kernel must invalidate the tiles' map files as it invalidates
> their I-caches. **This cannot be a tile instruction**: `FENCE` is outside the ruled subset
> (cons-C10) and a body may not execute one. It is a privileged host action on the same path as
> `RESUME`, and it is **new work this design creates**. §15 Q7.
>
> **R5 — The tag must include the ASID.** §4.2's recorded hole: handles are FTU indices and
> virtual entry PCs repeat across address spaces, while `MigrationEvent` (`NMFCFabric.h:98-105`)
> and `TileContext` (`NMFCTile.h:44-123`) carry no ASID. Without it, two address spaces can hit
> each other's maps. §15 Q4.

### 8.4 The one thing the identity check cannot catch

**A recompilation that changes a function's map without changing its entry PC or its length.**
R4 covers it if the kernel invalidates; if it does not, the tag matches and the map is wrong.
This is the same exposure every I-cache has and it has the same remedy — but it is recorded
here rather than assumed, because under design A there is no cached decoder to go stale at all.

---

## §9 IMPLEMENTATION COMPLEXITY

### 9.1 What has to be built, by component

| component | work | size |
|---|---|---|
| **tile — map file** | 8 × 72 B in flops, banked by name (64 banks × 9 B), `W`-replicated | **2.25 KiB**; ~4,600 flops per replica |
| **tile — RFT** | 8-entry range CAM (`asid`, `lo_pc`, `hi_pc`, `map_index`), 8 × 48-bit comparators | **128 B** |
| **tile — fill FSM** | issue 1–2 line reads; verify non-overlap (a 512-bit occupancy accumulate over 64 entries); write `W` replicas; allocate RFT + slot; refcount | ~64 cycles, off the issue path, once per (tile, function) |
| **tile — context slot** | `mapIndex:3`, `mapValid:1`; fetch buffer widened by 48 bits of pre-resolved fields (§6.4 iii) | **+6.5 B per context** → 87 B → 93.5 B (**+7.5%**) |
| **tile — decode** | replace `layout_.field[r]` (`NMFCTile.cc:460-475`) with the banked read; keep `illegal()` for `width_code = 0` | a rewrite of two functions and the fetch-buffer fill |
| **fabric / messages** | **nothing** under ID-1. One `MAPREQ`/`MAPRSP` control pair under ID-3; +8 B in `MigrationEvent` under ID-2 | 0 / 2 messages / 8 B |
| **ISA** | **nothing.** No instruction added or removed; no encoding bit moves; cons-C22's twelve plus `RESUME` intact; cons-C23's `funct7` `0x6`/`0x7` untouched | 0 |
| **host / kernel** | map-file invalidation on code load (R4); ASID in the tag (R5) | new, small, privileged |
| **linker** | emit the map image before each entry point inside the existing `__dup_start`/`__dup_end` span (DESIGN §26.1 D:2553-2600); page-align entry points if P2 is kept as the fallback | a script change |
| **`annotate` (M7)** | **emit the map** — which is the placement it must compute anyway. §9.3 | the same rewrite K.6 already demands, plus a writer |
| **compiler back end** | **materially less than design A.** §9.2 | — |

### 9.2 The compiler, and the one place B is genuinely easier than A

Design A converts packing into *"allocate over two nested register classes"* and then records
(`register-map.md` §3.10, the [FIX] block) that the geometry is **new back-end work**: *"the
whole (`x8`) is numbered **below** its two parts (`x16`, `x17`) and the pairing stride is 2
across a 16-name class. That is expressible in TableGen, but it is a **new register file, new
`SubRegIndices` and a new encoding map**."*

Under design B **there is no register file to describe**: names are opaque, the back end
allocates 63 uniform names under a bit budget, and the *layout* is chosen afterwards by the same
pass that emits the map. That is closer to a stack-slot colouring problem than to a
sub-register allocation problem, and it needs no `SubRegIndices` at all.

**Both designs still owe the same thing**, and I.8's *"open, and it is a compiler problem"*
stays open under both: something must decide which value gets which bits. B's advantage is that
the decision is expressed in an emitted table rather than in the target description.

**The `-ffixed` day-one gate (M13) is different, and worth naming.** Under A the day-one path is
`-march=rv64ima` plus `-ffixed` plus `-fcall-used-x8/x9`, reaching eight 64-bit names under GCC
and six under Clang (`register-map.md` §3.10). Under B the same flags reach **any layout the
emitter chooses**, because the layout is data — the eight-name gate is a *default map* the
emitter writes, not a property of the ISA. So B's day-one path is at least as good as A's, and
the F2 emitter is a few hundred lines rather than a back end.

### 9.3 The admission tool (M7)

`tools/nmfc/annotate.cc:524-559` today builds a pool of `opt.num_regs` (8) **slot ids**,
allocates one whole slot per live value, and `die()`s when the pool empties; it computes
`bits += reg_bits[reg] != 0 ? reg_bits[reg] : 64U;` at `:555-559` and **throws the number away
on a stderr line at `:927-928`**. K.6 already requires the rewrite. Under design B:

1. Classify each live value's width. **The width source does not exist on the ruled target** —
   `annotate.cc:461-470`'s `width_of` parses **x86-64** register names (`rax`/`eax`/`al`,
   `r8d`/`r8w`/`r8b`, `zmm`/`ymm`/`xmm`) and the target was ruled RISC-V (R11), where a register
   name carries no width (fact-C12). This is design A's §10 Q8 verbatim and it is **the same
   blocker for B**; it is not created by either design.
2. Peak simultaneous liveness in bits — K.6 **verbatim**, at each value's actual width, **plus
   the scratch bits any packing needs**. `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` ~~Under B there is no rounding-up, which is
   the regression design A has to declare (…512 bits under K.6 and 704 under A)~~ — **struck.
   There is no rounding-up under A either**: seven 64-bit values plus eight bytes is 512 bits
   under K.6, under A and under B. The difference is that A stages the bytes through a scratch
   name (~2-3 ops per access) where B resolves them with a map lookup. §12.4.
3. Produce a placement, and **emit it as the map**. Design A must produce a placement too and
   then throw it away, because the ISA fixes the geometry; under B the placement *is* the
   output. **cons-C20 — "admissibility is a property of the generated code" — becomes trivially
   checkable, because the evidence is a table in the binary.**
4. Verify disjointness. Under A this is the only thing standing between an allocator bug and a
   silent wrong answer (`register-map.md` §9.5). Under B the tile re-verifies at fill (§2.5), so
   the tool's check is a **second** line of defence rather than the only one.

### 9.4 ISA surface, and which of design A's ten deviations B inherits

**None of design A's deviations come from the *map*; they come from names being narrower than
XLEN, which is true under both designs.** So B inherits most of them. The honest table:

| A's deviation (`register-map.md` §8.4) | under B |
|---|---|
| 1 — `f`*n* ≡ `x`*n*, not implementing `F`/`D` | **avoided.** Canon's own rule holds instead: the namespaces do not alias (CANON.md:9819), and the map gives `f3` and `x3` different offsets |
| 2 — `f0` hardwired to +0.0, `f1`–`f7` illegal | **avoided.** `f0` is an ordinary name the map may define; the stock FP temporaries `ft0`–`ft7` work |
| 3 — NaN-boxing abolished, both halves | **inherited** (fact-C4, fact-C11): an `f32` in a 32-bit slice has no wider container |
| 4 — FLEN is per-instruction | **inherited** |
| 5 — tile and host disagree about `f32` | **inherited** |
| 6 — `rm = DYN` defined as RNE | **inherited** (cons-C8 forbids the `fcsr` under both) |
| 7 — XLEN is per-instruction (W1b) | **inherited** |
| 8 — `*W` opcodes legal on a narrow name | **inherited** |
| 9 — `x1`–`x7` illegal as any operand; `ret` illegal | **replaced by something better.** Under B a name is illegal iff **the function's own map leaves it undefined**, so `sp` and `ra` are illegal in a function that did not ask for them — I7 enforced per function, at run time, by the mechanism `RegLayout::illegal()` already implements. `ret` still has to be rewritten to `END`/`RETC` (**M8**, `register-map.md` §3.6) |
| 10 — `jal`/`jalr` with `rd` ≠ `x0` illegal | **inherited** — it is an I7 rule, independent of the map |

**Six of ten inherited, two avoided, one replaced by a stronger form, one shared.** Anybody who
reads design B as "the ISA is left alone" is reading it wrong; what B leaves alone is the
*geometry*, not the *semantics*.

### 9.5 The fit-list (M1–M15)

| | mechanism | under design B |
|---|---|---|
| **M1** | resident-function table | **KEPT and completed.** DESIGN §25.7 D:2560-2567's *"one small table entry beside the instruction cache, indexed by the function a context is running"* is §4.3's RFT. What the record lacked — an identity tag, a fill path, a migration story, a cost — is §§3–5. **DESIGN §25.7 is not superseded under B; it is finished.** Contrast design A, which deletes it and must mark the supersession (cons-C31) |
| **M2** | `readReg`/`writeReg` indirection (`NMFCTile.cc:460-475`) | **kept in shape, changed in source**: `layout_.field[r]` becomes `mapfile[c.mapIndex][ns][r]`, pre-resolved a window early (§6.4 iii). `illegal()` survives verbatim |
| **M3** | `Context512::read`/`write` | **reusable unchanged.** Under F2 no slice straddles a 64-bit word, so the `word[w0 + 1]` splice branch is dead code — the same simplification design A gets. Under F0 it is live and is a real cost (§2.4) |
| **M4** | `CXW`/`CXR`, 64-bit lane in `funct7[3:1]` | **unchanged.** *"The lane is an access granularity, not the register's structure"* (I.8). Under a free map a value **may** straddle a lane, so the host may need two `CXR`s and a splice — a cost design A does not have (§12.5) |
| **M5** | the `x0` rule | **preserved**, and `hasZero` in `RegLayout` is already the mechanism |
| **M6** | the illegal-register trap | **PRESERVED AS A RUN-TIME GUARANTEE** — §2.5. This is the row where B is strictly better than A, which scores it *"NOT met as a machine guarantee"* |
| **M7** | admission tool | §9.3. The placement becomes the output |
| **M8** | the `END` return bit | **I2 preserved literally.** The 512 bits return whole and uninterpreted. **And B improves the host's half**: the caller can read the *same published map* to find the return values, instead of relying on a convention (`register-map.md` §4.4/§4.5). `ret` must still be rewritten to `END`/`RETC` |
| **M9** | `JOIN` as a read-modify-write try | **unaffected** — it moves 512 bits and never inspects them |
| **M10** | `CONT` / `CONT.M` | **the hardest case, and it is handled**: Finding 2, §8.2. A successor's map is re-acquired by the same RFT lookup on the successor's PC. Under design A a successor inherits the one map and the problem does not exist; under B it is a real check that must be built |
| **M11** | 72-byte migration | **72 B exactly** (§5.1). Zero bits added to the payload and zero to the envelope under ID-1 |
| **M12** | the two hosts and RoCC's 128-bit path | **unaffected.** cons-C25 holds: every operand is a value in a GPR; the map is internal to the tile |
| **M13** | `-ffixed-x{n}` as the day-one gate | **strictly better than under A** — §9.2 |
| **M14** | encoding space | **untouched.** cons-C22, cons-C23 intact |
| **M15** | Appendix 2 `S5` | **changes**: `S5` records that the bit-level admission test is never exercised *"because nothing produces a layout other than the default"*. Under B the fix is a **map emitter**, and `defaultLayout()` becomes unreachable at run time (R2 of §8.3) rather than being the only reachable path |

---

## §10 PERFORMANCE IMPACT

### 10.1 The issue path

| stage | design B (as recommended, §6.4 iii) | design A, for comparison |
|---|---|---|
| **fetch** | one map-file read group per instruction fetched — 4 banked 8:1 selects, resolved inside a window that is `Dp` = 8 cycles long by construction | none |
| **decode** | **+0.** The offsets are already in the fetch buffer | +1 mux level and an OR gate |
| **register read** | **+2 mux levels** (word select, half select, extension select), identical to A | +2 mux levels |
| **register write** | **+0** — byte enables, no read-modify-write, because F2's slices are byte-aligned and never straddle | +0 |
| **hazard / forwarding** | **+0**, and the reason is canon: *"at most one instruction per context is ever in flight… there is **no forwarding, no interlocking, no hazard detection**"* (CANON.md:474-478) | +0 |
| **`Dp`, and hence `C ≥ W(Dp + L/I)`** | **unchanged**; §6.5 prices the worst case at 132 → 136 if the fetch-path lookup does not fit | unchanged |

### 10.2 The arrival path — the number the whole design turns on

| | steady state | transient |
|---|---|---|
| **RFT hit** | an 8-entry range compare in the arrival cycle. **0 memory accesses, 0 added cycles** against the measured **2.2–2.3 cycles** of arrival (CANON.md:1335-1336, 4261, 8247) | — |
| **RFT miss** | — | 1–2 local line reads, **overlapped with the instruction fetch the context is already waiting for** (`state = FETCHING`, `NMFCTile.cc:1397-1399`), plus a ≤64-cycle non-overlap verify off the issue path |
| **how many misses** | `N × F` for the **whole program** — tiles × distinct functions. At `N` = 4 and a handful of functions: **tens of line reads, total** | |

**On the record's worst measured migration rate** — 196,904 migrations for 262,143 loads,
0.75 per memory op (CANON.md:4957) — **design B pays four cold fills for the entire run** (§5.3),
and 196,900 of 196,904 arrivals pay nothing. The alternative the brief sketched, keying on the
handle, pays **≈125,080** and roughly doubles the arrival cost on 63.5% of migrations (§4.5).
**That gap is the single most important performance number in this document.**

### 10.3 Bandwidth and cache pressure

- **Map traffic in steady state: zero.** Nothing is fetched to decode (the fill is the only
  access, and it happens `N × F` times).
- **A cold fill competes with the I-cache**, not with the D-cache: the map is on the same
  duplicate page as the code, in a machine whose function-core I-cache hit rate is measured at
  **100.00%** (CANON.md:8247). It cannot cause a migration, because the page is replicated on
  every channel (CANON.md:2113) — which matters, since on this machine a foreign access **is** a
  migration (DESIGN §26.1).
- **Code-space cost:** P1 adds 40–76 B per function, on the entry line. P2 as fallback adds
  1.0–1.9% of the code region, ×`N` physical copies (§3.3).

### 10.4 Where design B is FASTER than design A — and it is instructions, not admission

> **[SUPERSEDED - user ruling 2026-09-03 (liveness)]** *(dated 2026-09-03)* **The premise of
> this section is struck.** Design A does **not** charge narrow values 32 bits and does not
> reject the worked case: seven live 64-bit values plus eight live 8-bit values is **512 bits
> under K.6 and 512 under A**, with the bytes packed inside a name and reached by shift-and-mask
> through a scratch name. **A and B have identical capacity — 512 bits.** What survives of this
> section, and it is still worth having, is the **speed** claim: B resolves a sub-name access
> with a map lookup where A pays about **2-3 extra ops**. That is the real difference, and it is
> the one the recommendation must weigh against B's third object and per-context state.

**The struck argument, retained for the record.** Design A charges every value narrower than 32
bits **32 bits** (`register-map.md` §3.8, §9.2), so a function's admissibility is decided on a
rounded-up budget. Its own worked counterexample: **seven live 64-bit values plus eight live
8-bit values = 448 + 64 = 512 bits, which K.6 admits at exactly 512 of 512; design A computes
448 + 8×32 = 704 and REJECTS it.** Under cons-C15 that rejection is fatal — there is no spill.
**[EVERY FIGURE IN THIS PARAGRAPH IS STRUCK - user ruling 2026-09-03 (liveness): A computes 512 and
admits it.]**

> ~~**Design B admits that function.** Not faster — *possible*.~~ **[STRUCK: A admits it too.]** The performance comparison between
> "runs on the tile" and "cannot be offloaded" is not a percentage, and it is the reason the map
> was in the design in the first place.

The same applies to `register-map.md` §9.3's case: **nine 48-bit values are 432 bits of data;
design A needs nine 64-bit tiles and rejects them; a free map (F0) places them exactly.** Under
F2, B rounds 48 to 64 and rejects them too — which is why §15 Q2 puts F0-versus-F2 to the user
as a real choice rather than a formatting detail.

### 10.5 What is NOT measured, and must not be claimed

- Every arrival number above is arithmetic on top of a **measured** 2.2–2.3-cycle arrival and a
  **measured** migration count. The **fill cost itself is not measured** — there is no
  implementation and no run. It is estimated as one local line access on a 100%-hit I-cache
  path.
- The 3.34 distinct-tiles figure in §4.5 is an **estimate** from the measured 25.0%-per-tile
  balance, not a measurement.
- `F` = 8 resident functions is an assumption borrowed from canon's own words about the BTB
  (*"a tile runs a handful of distinct functions"*, CANON.md:5366-5368). **No run in the record
  measures the number of distinct functions resident on a tile**, and every cold-fill number
  above is linear in it. §15 Q8.

---

## §11 SIMPLICITY

**The honest summary is a trade, not a win**, and it is worth stating in the form the user asked
for:

> **Design A leaves the machine alone and changes the ISA. Design B leaves the ISA's geometry
> alone and changes the machine.**

| | design A | design B (B1b) | B2 |
|---|---|---|---|
| new hardware structures | none — 310 bits of decode ROM | **RFT (128 B) + map file (2.25 KiB) + fill FSM + refcounts** | none — 2,520 bits of decode ROM |
| new state per context | 0 | 4 bits + 6 B of pre-resolved fields (6.5 B total) | 2 bits |
| new messages | none | none (ID-1) | none |
| new ISA rules | 7 legality rules, 10 deviations | 6 inherited deviations + 1 stronger form (§9.4) | as A, ×`K` |
| new failure modes | none at run time (and that is its *weakness*: §2.5) | **stale map, evicted slot, unresolvable identity, invalidation on code load** (§8.3 R1–R5) | class mismatch at `FORK` |
| new build-system obligations | rewrite trailing `ret`; register-class split in the back end | rewrite trailing `ret`; **emit the map**; linker places it; kernel invalidates it | rewrite trailing `ret`; declare a class |
| lines of the machine that get more complicated | the read port | **the fetch path, the arrival path, `CONT`, eviction, invalidation** | the read port |

**Where B is simpler than A:** the compiler (§9.2 — no `SubRegIndices`, no new register file
description, no encoding map), the admission tool (§9.3 — the placement is the output rather
than an internal artefact), the day-one gate (§9.2), and the failure semantics (§2.5 — the
machine can still catch an undefined register, which A cannot).

**Where B is more complicated than A, and it is the larger list:** four new mechanisms that can
be wrong at run time (fill, eviction, identity, invalidation), a structure that must be kept
coherent with read-only memory, and a `CONT` path that has to re-derive identity mid-invocation.
**A has none of these because it has nothing to cache.**

**B2 sits almost exactly where A does on this table** — which is the point of §7.3: it buys
**direct names for** a byte tier, a halfword tier and an all-64 class for two bits per context
and one extra mux level, and it buys them **without any of B1's caching machinery.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]`
What it does **not** buy is capacity: A already holds those values, packed, at ~2-3 ops per
access.

---

## §12 WHAT THIS CANNOT DO

Read as the price list, in the same discipline as `register-map.md` §9 (cons-C16).

**12.1 #232's "64 1-byte regs" is fully expressible; only the 64th direct NAME is out of
reach.** `[SUPERSEDED - user ruling 2026-09-03 (liveness)]` 64 byte slices would need 64 names; `x0` is hardwired zero and denotes no
bits, so **63** are nameable across both namespaces (fact-C17). ~~A function wanting 63 live
bytes is admissible under B and impossible under A (which reaches 0); a function wanting all 64
is impossible under both.~~ **Struck.** Sixty-four live bytes are 512 bits and are **admissible
under A and under B**: under A they pack eight per 64-bit name and are reached by shift-and-mask
through a scratch name (~2-3 ops per access, and the scratch space must be accounted); under B a
map lookup resolves each. The arithmetic is real but it counts **names**, so what it prices is
**instruction count**. **Canon I2's "ANY combination" stands unnarrowed, and no design's price
list may claim otherwise.**

**12.2 Under F2, no non-power-of-two width and no unaligned placement has a NAME.** A 48-bit
pointer is named at 64; a 12-bit index at 16. **Nine 48-bit values are 432 bits and they fit** —
packed, and reached through a scratch name `[corrected - user ruling 2026-09-03 (liveness)]`. **F0 fixes this and costs 128 B, two
lines on every fill, and a live straddle path in `Context512` (§2.4).** §15 Q2.

**12.3 A stale or unavailable map is a new fatal outcome.** §8.3 R2: a miss with no derivable
identity is a fault, not a default. Design A cannot fail this way because it has nothing to
fetch. This is the price of the third object being real.

**12.4 The admission test is K.6 verbatim — and that is not free either.** B does not round
sub-32-bit values up, so it admits functions A rejects (§10.4); but the placement is still
"assignment of live values to non-overlapping ranges over live ranges", which is the classic
register-pairing problem and is **not** solved by the bit sum. `register-map.md` §3.8's
counterexample and its placement lemma apply unchanged: the sum is **necessary, not
sufficient**, and `annotate` must produce and verify a placement (§9.3). Under B the sum is
tighter but the feasibility problem is *harder*, because the widths are less regular.

**12.5 A value may straddle a `CXW`/`CXR` lane, and the host pays for it.** The aperture moves
one 64-bit lane, lane number in `funct7[3:1]` (**M4**, `NMFC_CX_LANE_MASK = 0x7`). Under design A
no slice straddles a lane, so *"every named value is reachable in exactly one `CXR`"*
(`register-map.md` §4.2). Under F2 that is still true (slices are aligned and ≤ 64 bits). **Under
F0 it is not** — an unaligned 64-bit value spans two lanes and costs two `CXR`s and a splice on
the host. Another entry on F0's bill.

**12.6 The map file must be invalidated by the kernel on code load, and nothing in the tile can
do it.** §8.3 R4. `FENCE` is outside the subset (cons-C10) so a body cannot flush; this is new
privileged work on the host side and it is not in the record today.

**12.7 The ASID hole is real and unclosed.** §8.3 R5: handles are FTU indices and entry PCs are
virtual, and neither `MigrationEvent` nor `TileContext` carries an ASID. Until it is closed, two
address spaces can collide in the RFT. §15 Q4.

**12.8 `F` = 8 resident functions is an assumption, and every cold-fill number is linear in it.**
§10.5. If a tile really runs 40 distinct functions, the RFT thrashes, eviction becomes a live
path, and §5.3's "four fills for the whole run" is wrong by whatever the miss rate turns out to
be. **Nothing in the record measures this.**

**12.9 It does not solve the compiler's packing problem** — it only changes where the answer is
written down. I.8's *"open, and it is a compiler problem"* stays open (`register-map.md` §9.10 is
identical on this point).

**12.10 Disassembly lies, differently and worse than under A.** Stock `objdump` prints `x23`;
under A that always means one thing (`w7`, 32 bits), under **B it means whatever this function's
map says**, so a disassembly is not interpretable without the map beside it. The remedy is a
tool that reads the emitted map — which exists, unlike A's alias table, but has to be written.

---

## §13 THE HONEST STATEMENT — THE THIRD REFERENCED OBJECT

The ruling, once more: *"It introduces a third piece of memory every context needs. So now we
have the map, instruction, and potentially data that must be referenced all at the same time."*

**Design B does not remove the third object. It is the third object, rebuilt.** Every softening
below is a real property and none of them is a rebuttal:

| the objection | what B actually does |
|---|---|
| "a third piece of **memory**" | **True at fill, false in steady state.** Memory is referenced `N × F` times for the whole program (§5.3: four times on the measured stress run). After that the map is an **on-core 576 B array read like a decode ROM**, not a memory reference |
| "every context **needs**" | **False as stated, true in substance.** No context has its own map; contexts of one function share one 72 B entry and a 3-bit index. The per-context cost is 4 bits + 6 B of pre-resolved fields |
| "must be referenced **all at the same time**" | **This is the part that survives.** At issue, B references the instruction, the data, and a pre-resolved offset that came from the map. §6.4 (iii) moves the map read a full re-issue window earlier so it is never *simultaneous* with the data access — but the object exists, it has to be filled, kept, invalidated, and got wrong |
| the record's standing prohibition | `register-map-context.md` §6: *"A proposal may not answer the ruling by pointing at duplicate pages again."* **§3.1 does not.** It uses the duplicate page as the *storage*, and answers the simultaneity objection separately, above |

**B2 is different, and this is the cleanest thing in the document to rule on.**

> **B2 genuinely removes the third referenced object.** A width class is 2 bits in the context's
> own tile-local slot — the same kind of thing as `ibufPC` or `holdsLine` — and the geometry is a
> wire pattern. There is no table, nothing is fetched, nothing goes stale, nothing needs
> invalidating, and the ruling's objection does not apply to it at all. **What it costs is
> per-function packing: a function picks a layout from a menu of `K` instead of writing its
> own.**

So the deliverable resolves to a single question for the user, and §15 Q1 asks it: **is
per-function packing worth a cached table, or is a menu of four layouts enough?**

---

## §14 DESIGN B AGAINST DESIGN A, AND THE CONSTRAINT CHECK

### 14.1 Side by side

| | **A** — fixed aliasing | **B1b** — cached free map | **B2** — width classes |
|---|---|---|---|
| third referenced object | **none** | on-core 576 B array, filled `N × F` times | **none** |
| per-context state added | 0 | 4 b + 6 B | 2 b |
| per-tile state added | 310 b of ROM | 2.5 KiB | 2,520 b of ROM |
| migration | 72 B | 72 B | 72 B |
| widths **directly nameable** `[corrected - liveness ruling]` | 64 and 32, complete | **any power of two, any mix** (F2); anything at all (F0) | four fixed layouts, incl. a byte tier over half the file |
| widths **expressible** | **all** — 512 bits | **all** — 512 bits | **all** — 512 bits |
| sub-32-bit values | **charged their own width**; no direct name, so ~2-3 ops per access `[corrected - user ruling 2026-09-03 (liveness)]` ~~charged 32 — functions K.6 admits are rejected~~ | charged their own width; one map lookup per access | charged their own width; direct name inside the class |
| undefined register (cons-C14) | **undetectable** | **traps at decode** | undetectable |
| over-liveness | **silent wrong answer** | **rejected at fill** | silent wrong answer |
| new run-time failure modes | none | four (§8.3) | one |
| back-end work | new register file + `SubRegIndices` + encoding map | a map emitter | as A, plus a class field |
| ISA deviations from ratified RV64IMAFD | 10 | 6 inherited + 1 stronger | 10 |

### 14.2 Constraint check (cons-C1–C32), for B1b

| | verdict |
|---|---|
| **C1** no state outside the context and the encoding | **NOT met, and this is the design's premise.** A per-function map exists. §13 states it without softening |
| **C2** no third referenced object at decode | **NOT met at fill; met in steady state**, and never *simultaneous* with the data access under §6.4 (iii) |
| **C3** 512 bits, bit-packed, not eight registers | **met — and met by every candidate, since all hold 512 bits** `[corrected - user ruling 2026-09-03 (liveness)]`. B names more of them **directly**: every power-of-two mix, 63 of 64 byte slices, and #232's "16 4-byte regs" and "8 8-byte regs" exact. Design A names 24 slices directly and packs the rest at ~2-3 ops per access, so what B is "more fully" is **direct**, not capable |
| **C4** the file may not be widened | **met** — 512 exactly |
| **C5** 512 in, 512 out, PC beside | **met** |
| **C6** migration exactly 72 B | **met** under ID-1 (§5.1); **not met** under ID-2, which is why ID-2 is a ruling and not a default |
| **C7** one file, two namespaces, no separate FP file | **met, and in canon's own form**: the namespaces do not alias (CANON.md:9819) because the map gives them disjoint offsets. Design A meets this by making them *identical*, which is a deviation from that same line |
| **C8** no `fcsr`, no rounding state | **met as to state**; `rm = DYN` inherits A's §3.7 softening and §9.12 divergence unchanged |
| **C9** no stack, no spill | **better enforced than under A**: `sp` = `x2` is undefined in any map that did not ask for it, so `addi sp, sp, -16` traps **at run time, per function** (§9.4 row 9) |
| **C10** subset is `RV64IMAFD` | **met as to the opcode list; not met as to semantics** — six of A's ten deviations (§9.4) |
| **C11** no vector extension | **met** |
| **C12** nothing blocks | **met**, with one caveat: a context whose map is filling is `FETCHING`, which is an existing state, not a new blocking one |
| **C13** nothing speculative | **met.** §6.4 (iii) resolves early but nothing *executes* on it — the same distinction canon draws for the BTB (CANON.md:5359-5364) |
| **C14** undefined register is a hard error | **MET, as a machine guarantee** — §2.5. The only candidate that meets it |
| **C15** rejection is fatal, no truncation | **met** — §8.3 R2 forbids a default map |
| **C16** regressions must be stated | **met** — §12 |
| **C17** peak liveness in bits, one pool | **met, K.6 verbatim** — no rounding up (§9.3, §10.4) |
| **C18** not a count of names | **met for the sum**; the placement feasibility check is justified separately, exactly as in `register-map.md` §3.8, and is **harder** here because the widths are less regular (§12.4) |
| **C19** a register never read costs nothing | **met** |
| **C20** admissibility is a property of the generated code | **met, and checkable** — the map in the binary *is* the evidence |
| **C21** the two measured functions must still fit | **met** — `nmfc_bu` at 480 bits and `nmfc_expand` at 384 bits fit under any of these maps; B additionally admits packings A rejects (§10.4) |
| **C22** twelve instructions plus `RESUME` | **met** — none added |
| **C23** only `funct7` `0x6`/`0x7` free | **met** — none consumed |
| **C24** field values are implementation choices | **met** — `F`, the entry format and the class count are all `[CONFIGURATION]` |
| **C25** every operand is a value in a GPR | **met** — the map is internal to the tile |
| **C26** no bit-field insert/extract instruction | **met** — none proposed |
| **C29** tier 4 decides nothing | **respected** — every tier-4 citation above is evidence about what exists, never an argument |
| **C30** fixed aliasing at one width is the SST layout again | **respected** — `defaultLayout()` is made unreachable at run time (§8.3 R2), not adopted |
| **C31** supersessions must be marked | **nothing to mark.** B *completes* DESIGN §25.7 rather than overruling it, and CANON.md:9819's non-aliasing clause is preserved rather than superseded — which is the reverse of design A, whose §5.1 must supersede both |
| **C32** argue only on the ruling's stated ground | **respected** — §13 |

---

## §15 OPEN QUESTIONS FOR THE USER

**Q1 — the whole deliverable: per-function packing, or a menu?** B1 gives every function its own
layout and costs a cached table with four new run-time failure modes (§8.3, §11). B2 gives four
fixed layouts, costs 2 bits per context, and **removes the third referenced object entirely**
(§13). Design A is B2 with one layout. **Which of the three?** *(Recommendation: **B2** if the
byte and halfword tiers are worth having and per-function packing is not; **B1b** if the
measured functions genuinely need per-function layouts — and §12.8 says the record does not yet
contain the measurement that would decide it.)*

**Q2 — F0 or F2?** F2 (72 B, aligned powers of two) keeps the straddle-free read path and the
one-`CXR` host path; F0 (128 B) places a 48-bit pointer exactly and is the only format that
matches the rejected mechanism's full expressiveness. **Which?** *(Recommendation: **F2**, with
F0 reachable later by a format-version bump in the header — the header exists for this.)*

**Q3 — ID-1, ID-2 or ID-3 for identity after migration?** ID-1 (page header) costs 0 wire bytes
and needs page-aligned functions; ID-2 costs **8 B on the migration message** and would make
`SIZE_BYTES` 80; ID-3 costs a fabric round trip on a cold miss only. §4.2. *(Recommendation:
**ID-1**, with ID-3 as a build option; ID-2 only if you would rather spend the 8 B than align
functions.)*

**Q4 — the ASID hole.** `MigrationEvent` and `TileContext` carry no ASID, but handles are FTU
indices and entry PCs are virtual. Add an ASID to the tag and the envelope (~2 B), or rule that
handles are globally unique? §8.3 R5, §12.7.

**Q5 — eviction: refcount-and-refuse, or evict-and-broadcast?** §4.4. *(Recommendation:
**refcount and refuse** — refusing rather than evicting is already this machine's idiom for the
FTU, **I.5**.)*

**Q6 — under B2, is the class carried on migration?** Two bits in the envelope would make the
re-acquire unconditional and delete the RFT from B2 entirely. Is 2 bits "bearing the burden"?

**Q7 — who invalidates the map file on code load?** §8.3 R4. It must be the kernel, on the same
path as I-cache invalidation, and it is not in the record.

**Q8 — how many distinct functions does a tile actually run?** Every cold-fill number in §5 and
§10 is linear in `F`, and canon's only statement is *"a tile runs a handful"* (CANON.md:5366).
**A counter in the existing runs would settle it, and would also settle whether design A's
whole-program advantage is real or notional.**

**Q9 — does the record want the run-time undefined-register trap back?** It is the one thing B
has and A does not (§2.5, cons-C14), and `register-map-context.md` §0 point 3 says the
replacement *"inherits `RegLayout`'s one genuinely load-bearing behaviour"*. **If that is a
requirement rather than a preference, it rules out A and B2 together.**

---

## APPENDIX — DESIGN B ON ONE PAGE

```
  THE MAP                       64 entries x 9 bits + a 4 B header  =  40 B (int) / 76 B (with F/D)
                                entry = { width_code:3 , index:6 } ;  offset = index << (width_code+2)
                                width_code 0 = UNDEFINED -> the name traps  (cons-C14 restored)

  WHERE IT LIVES                immediately before the function's entry PC, inside the existing
                                __dup_start/__dup_end span -- read-only, replicated on every channel,
                                local to every tile, 100.00% I-cache hit rate measured.

  WHAT THE TILE HOLDS           RFT       8 entries x ~16 B  = 128 B   (asid, lo_pc, hi_pc, map_index)
                                MAP FILE  8 x 72 B = 576 B, banked by NAME, replicated W=4 = 2.25 KiB
                                CONTEXT   mapIndex:3 + mapValid:1  (+ 6 B of pre-resolved fields)

  ARRIVAL                       RFT range-match on the PC  ->  hit: 0 accesses, 0 cycles
                                                               miss: 1-2 local lines, off the issue path
                                Checked at admit, at migration, at CONT, at RESUME.

  MIGRATION                     72 B.  Unchanged.  Nothing added to the payload or the envelope.

  DECODE                        the map is read at FETCH-BUFFER FILL, one window early,
                                so the issue path sees offsets that are already resolved:
                                0 map reads, 0 mux levels, 0 ports at the width of the machine.

  COLD FILLS, WHOLE PROGRAM     N x F.  On the measured stress run (196,904 migrations): 4.
                                Keyed on the handle instead, it would be ~125,080.

  B2                            replace the map with a 2-bit WIDTH CLASS selecting one of four
                                fixed layouts (D+W = design A / all-D / D+H / D+B).
                                No table, no fetch, nothing to invalidate -- and no per-function packing.
```
