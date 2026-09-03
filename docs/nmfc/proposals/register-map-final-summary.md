# REGISTER MAP — FINAL SUMMARY

PROPOSAL, NOT IN THE CANON UNTIL RULED. Full argument: register-map-final.md.

**Design A — fixed aliasing.** The register number *is* the bit range: x8-x15 are the eight 64-bit tiles of the 512, x16-x31 the sixteen 32-bit tiles, the
halves of xn are x2n and x2n+1, x0 reads zero, and x1-x7 are illegal. Decode is eight gates, one logic level; nothing is fetched, cached, or can go stale;
migration stays 72 B and an arriving context runs immediately. The cost is the byte tier — a 5-bit field cannot name 64 byte slices — so anything narrower than
32 bits is charged 32, and a function that still will not fit is rejected at admission. Two hatches are free to reserve: 2 class bits in the migration envelope
(**B2**: four fixed layouts, class 0 *is* A, one added mux level on every decode forever), and the custom-1 opcode (**A2**: extract/insert instructions carrying
a 9-bit {index, width} extent immediate, reaching the byte tier for +1 instruction per packed access).

**Design B — the map as an extension of the context.** Each function publishes a 40-76 B map beside its own code; each tile caches eight in a 576 B on-core
file; a context carries a 3-bit index to its map, never the map. Migration stays 72 B. It buys arbitrary packing, a byte tier, and a run-time trap on an
undefined register name. Three findings stand whatever is ruled: the entry PC does not survive migration (NMFCFabric.h:94-123), so a handle-indexed cache cannot
identify its function on arrival; CONT changes the function under an unchanging handle and cannot fail, so identity must be rechecked there too; and once an
identity tag exists, function-keyed caching beats handle-keyed by ~125,000 cold fills. In B's own words: "It is the third object, rebuilt."

| design | implementation complexity | performance impact | overall simplicity | mean |
|---|---|---|---|---|
| A  | 7.3 | 8.0 | 6.7 | **7.3** |
| A2 | 6.3 | 8.0 | 5.7 | **6.7** |
| B  | 4.0 | 8.0 | 3.7 | **5.2** |

Means of three lenses (RTL, compiler, architect), scored before this pass's corrections — an ordering, not a measurement.

**Recommendation: rule in design A, reserving both hatches (the 2-bit class field and custom-1).** The one reason: the 2026-09-03 ruling eliminates only B1, and
A, A2 and B2 all satisfy it in full — so the choice falls to the other two criteria, where A alone adds no per-context bit, no mux level on any path, and no
tier-1 reopening.

**What must be acknowledged.** The performance column separates nothing and is the weakest of the three: every row assumes, undemonstrated, two mux levels of
slack in the register read. Under A, I2's "64 1-byte regs, or ANY combination" narrows to "any combination of 64- and 32-bit values, everything narrower charged
32": A places 2,685 of 17,361 width-multisets against B1's 13,091 — a factor of 4.9, not the 162 the earlier documents quoted. That cap belongs to the 5-bit
register field, not to the ruling; A2 is the proof, reaching the byte tier while respecting the ruling in full. A also carries an uncosted compiler bill:
register file description, SubRegIndices, encoding map, post-RA gate against stack spills. Under B, the third object exists and must be filled, kept,
invalidated and got wrong — four new run-time failure modes — and it restores only half of cons-C14: over-liveness (SW1) stays silent under every candidate, and
A's only defence against it is a ~40-line placement verifier that does not exist and has no owner. And deferring the byte tier rests on an absence of
measurement, not a measurement of absence: the tool behind the two decompositions cannot see widths.

**The questions, verbatim from §10** (Q5, reopening R84/cons-C26 and cons-C22, arises only if A2 is ever built; *no* closes A2 and changes nothing else):

- **Q3**, the one to rule carefully: "Supersede CANON I.7 item 3?" — "the worst failure mode in either design: the same encoding computes different results on
  host and tile, and nothing can see it." Supersede, with the divergence priced rather than footnoted.
- **Q2**: "Does `f`*n* ≡ `x`*n*?" — **Yes**, one allocation pool; it supersedes four canon statements and amends O4's spelling to `RV64IMA_Zfinx_Zdinx`.
- **Q4**: "Is the run-time undefined-register trap a REQUIREMENT or a preference?" — **Preference.** Ruled a requirement, it eliminates A, A2, B2, forcing B1.
- **Q1**: "Where do you stop?" — "The ladder is **not a line**: it is `A ⊂ {B2, A2} ⊂ B1`." **A now, with BOTH hatches RESERVED**: "reserving is not choosing."
