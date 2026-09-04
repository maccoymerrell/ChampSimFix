# REGISTER MAP — FINAL SUMMARY

PROPOSAL, NOT IN THE CANON UNTIL RULED. Full argument: register-map-final.md.

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

**Design A — fixed aliasing.** The register number *is* the bit range: x8-x15 are the eight 64-bit tiles of the 512, x16-x31 the sixteen 32-bit tiles, the
halves of xn are x2n and x2n+1, x0 reads zero, and x1-x7 are illegal. Decode is eight gates, one logic level; nothing is fetched, cached, or can go stale;
migration stays 72 B and an arriving context runs immediately. The cost is **directness, not capacity**: the 5-bit field names 24 slices, so a value
narrower than a name is bit-packed inside one and reached by shift-and-mask through a scratch name — about **2-3 extra ops per packed access** in plain RV64I,
with no new instructions and **no width out of reach**. Admission is a test on **bits of peak liveness plus the scratch bits the packing needs**, never a count
of values `[user ruling 2026-09-03 (liveness)]`. Two hatches are free to reserve: 2 class bits in the migration envelope
(**B2**: four fixed layouts, class 0 *is* A, one added mux level on every decode forever), and the custom-1 opcode (**A2**: extract/insert instructions carrying
a 9-bit {index, width} extent immediate, reaching a packed value in **one** instruction instead of 2-3 — a speed optimisation, never a capability fix).

**Design B — the map as an extension of the context.** Each function publishes a 40-76 B map beside its own code; each tile caches eight in a 576 B on-core
file; a context carries a 3-bit index to its map, never the map. Migration stays 72 B. It buys a **map lookup in place of shift-and-mask** on a sub-name access — capacity is 512 bits either way
`[user ruling 2026-09-03 (liveness)]` — and a run-time trap on an undefined register name. Three findings stand whatever is ruled: the entry PC does not survive migration (NMFCFabric.h:94-123), so a handle-indexed cache cannot
identify its function on arrival; CONT changes the function under an unchanging handle and cannot fail, so identity must be rechecked there too; and once an
identity tag exists, function-keyed caching beats handle-keyed by ~125,000 cold fills. In B's own words: "It is the third object, rebuilt."

| design | implementation complexity | performance impact | overall simplicity | mean |
|---|---|---|---|---|
| A  | 7.3 | 8.0 | 6.7 | **7.3** |
| A2 | 6.3 | 8.0 | 5.7 | **6.7** |
| B  | 4.0 | 8.0 | 3.7 | **5.2** |

Means of three lenses (RTL, compiler, architect), scored before this pass's corrections — an ordering, not a measurement.

**Recommendation: rule in design A, reserving both hatches (the 2-bit class field and custom-1).** The reasoning is now simpler than the version this page
first carried: **every candidate has the same capacity — 512 bits — so expressiveness decides nothing** `[user ruling 2026-09-03 (liveness)]`. What is left is
implementation cost and instruction count, and there A alone adds no per-context bit, no mux level on any path, no third object, and no tier-1 reopening, paying
only ~2-3 ops on accesses to values packed below a name.

**What must be acknowledged.** The performance column separates nothing and is the weakest of the three: every row assumes, undemonstrated, two mux levels of
slack in the register read. **Design A takes nothing away from invariant I2.** "16 4-byte regs, 64 1-byte regs, or ANY combination" stands unnarrowed: the
context is 512 independent bits on a strictly in-order core with no renaming, a value narrower than a name is packed with others inside one name and reached by
shift-and-mask through a scratch name, and admission is a test on **bits of peak liveness plus the scratch bits the packing needs** — never a count of live
values, never a 32-bit charge on a narrow one `[user ruling 2026-09-03 (liveness)]`. Every earlier figure in this record that said otherwise — "the byte tier is
unreachable", charge-32, 81 of 13,091, 2,685 of 13,091, a factor of 162 or of 4.9 — is **struck**. What a register field decides is how *directly* a value can be
named, and that is **instruction count only**: about 2-3 extra ops per access to a packed sub-name value under A, a map lookup per access under B. **A and B have
identical capacity, so the comparison the earlier drafts built the recommendation on has collapsed**; A2 is not a capability fix but a way to buy those 2-3 ops
back for one. What A does still carry is an uncosted compiler bill — register file description, SubRegIndices, encoding map, **the packing and scratch
allocation the ruling assigns to the compiler**, and a post-RA gate against stack spills. Under B, the third object exists and must be filled, kept, invalidated
and got wrong — four new run-time failure modes — and it restores only half of cons-C14: over-liveness (SW1) stays silent under every candidate, and A's only
defence against it is a ~40-line placement verifier that does not exist and has no owner. The one measurement genuinely missing is the *frequency* of sub-name
accesses in real offloaded functions, since that — not capacity — is what A2 would buy back: the tool behind the two decompositions cannot see widths.

**The questions, verbatim from §10** (Q5, reopening R84/cons-C26 and cons-C22, arises only if A2 is ever built; *no* closes A2 and changes nothing else):

- **Q3**, the one to rule carefully: "Supersede CANON I.7 item 3?" — "the worst failure mode in either design: the same encoding computes different results on
  host and tile, and nothing can see it." Supersede, with the divergence priced rather than footnoted.
- **Q2**: "Does `f`*n* ≡ `x`*n*?" — **Yes**, one allocation pool; it supersedes four canon statements and amends O4's spelling to `RV64IMA_Zfinx_Zdinx`.
- **Q4**: "Is the run-time undefined-register trap a REQUIREMENT or a preference?" — **Preference.** Ruled a requirement, it eliminates A, A2, B2, forcing B1.
- **Q1**: "Where do you stop?" — "The ladder is **not a line**: it is `A ⊂ {B2, A2} ⊂ B1`." **A now, with BOTH hatches RESERVED**: "reserving is not choosing."
