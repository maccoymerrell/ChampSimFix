# The user's last-resort fallback for the register map (tier 1, verbatim)

**Status: FALLBACK ONLY.** The user's words, 2026-09-03, given on going to bed:
"If none of the ideas that it comes up with survives scrutiny, we can do the
memory-mapped regfile thing … I will reiterate that I hate this idea, so it is a
last resort, not a first-class solution."

It is adopted only if every fixed-aliasing variant in `register-map.md` fails
scrutiny. It is recorded here so the words survive verbatim.

## The system, in the user's words

1. Each core has a series of context handles. These by construction never
   overlap. In each nmfc core, there exists an array indexed by these handles,
   that stores some 512-bit to regs mapping for the given function.
2. Upon entering an nmfc core, the context must load this piece of data. There
   must somehow be an address associated with this specific context handle,
   such that as it migrates between nmfc cores it pulls the correct regfile map
   regardless of current PC.
3. It is also critical that when a handle is reused, there is some sort of way
   to detect whether the function is identical or not, such that the current
   contents of the entry in the regfile-map for the given context window can be
   replaced with a new one or not.
4. Migration must not bear any of this burden, it must be cached locally within
   each core upon first-visit and replaced when that handle is eventually
   reused. The goal here is that excluding startup or a cold touch of an nmfc
   core, the function should never have to fetch its regfile map.
5. The problem here is clear: lots of contexts, each context needs a particular
   address for its map. This is essentially a translation map. You could cheat
   and keep the map in the first few bytes of every instruction page, such that
   it is always available if you just zero out the page-offset bits of the PC at
   any traversal location. The caveat there is that if the instruction memory
   crosses a page boundary, you incur an unnecessary reload of the reg map.

## What the record already says against it (why it is last resort)

- User, 2026-09-03, on the per-function map: "It introduces a third piece of
  memory every context needs. So now we have the map, instruction, and
  potentially data that must be referenced all at the same time. That frankly
  seems foolish." The fallback keeps that third structure and adds a
  handle→map-address translation and a per-core cache of it.
- Cross-page reload under the page-header cheat; handle-reuse identity check;
  first-visit fetch on every tile a context migrates to (bounded by the tile
  count, but not zero).

## To be filled by the proposal pass

Survivor-or-fallback determination, with the reasons each aliasing variant did
or did not survive, is written in `register-map.md` §7/§10.

## Clarification (user, 2026-09-03, verbatim)

"I want to clarify, the map is best thought of as an extension of the context.
Ideally it doesn't follow with migration, it is retrieved post-migration on the
new core. However, if the handle-index cache doesn't seem viable (it would need a
port for the width of the machine or we would need contexts aligned to widths so
the entire system could be banked-per-width (potentially necessary anyway)). All
things to consider. I would prefer a proposal for 1 or 2 suggested designs is
written up for the end of this, with full consideration of implementation
complexity, performance impact, and overall simplicity."

Consequences for the proposal pass:
- The map is context state that is NOT part of the 72-byte migration payload;
  it is re-acquired on the destination core (first visit fetch, then cached by
  handle; handle reuse must detect a different function).
- The handle-indexed per-core cache must be costed honestly: either a read port
  per context width the machine supports, or contexts aligned to a width class
  so the register file, the map cache and possibly the whole datapath are banked
  per width. "Banked per width" is to be evaluated as a design in its own right,
  since it may be necessary regardless of which map/aliasing scheme wins.
- DELIVERABLE: one or two recommended designs, each with implementation
  complexity, performance impact, and overall simplicity assessed; the aliasing
  variants and this map are candidates on equal footing under that rubric.
