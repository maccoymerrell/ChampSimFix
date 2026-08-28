/*
 * Who decides which memory tile owns an address — and, crucially, *when*.
 *
 * Everything else in this design follows from that one question. Route on the
 * virtual address and the decision is a shift and a mask, taken before any
 * translation, so a context always knows where it belongs; but then the
 * allocator has no freedom, because the frame must sit on the tile the virtual
 * address already named. Route on the physical address and the OS gains total
 * freedom to place and re-place a page, at the cost of having to translate
 * before it can route.
 *
 * These are not variations on a policy, they are different machines, and they
 * pull different parts of the design with them:
 *
 *   CONGRUENT_ROUTER   tile_of_virtual(va). No translation on the routing path.
 *                      Page tables stay partitioned N ways, one root per
 *                      channel over compacted virtual addresses, so every walk
 *                      is local. The allocator has no placement freedom at all.
 *
 *   RELOCATION_ROUTER  The same, except for grains the OS has deliberately
 *                      moved, which are listed in a table consulted on the
 *                      routing path. The per-channel roots survive untouched:
 *                      the root is chosen by the virtual address, which does
 *                      not change, so only the frame moves. Costs one table
 *                      lookup and one entry per relocated grain -- which is
 *                      the right trade only while relocations stay rare.
 *
 *   PHYSICAL_ROUTER    Translate, then read the tile out of the physical
 *                      address. The allocator may put any grain on any tile and
 *                      move it later, which is what makes adaptive repartioning
 *                      possible. The price is structural, not incidental: a
 *                      context cannot choose which per-channel page-table root
 *                      to walk without already knowing the tile, so the N roots
 *                      collapse to one and walk references may cross the
 *                      fabric.
 *
 * The interface is deliberately not a uniform abstraction over the three.
 * `order()` exposes the difference rather than hiding it, because the whole
 * point of the comparison is that translation happens at a different time.
 */

#ifndef NMFC_TILE_ROUTER_H
#define NMFC_TILE_ROUTER_H

#include <cstddef>
#include <cstdint>

#include "address.h"
#include "modules.h"
#include "operable.h"
#include "nmfc/nmfc_vmem.h"
#include "origin.h"

namespace nmfc
{

/** When the owning tile is decided, relative to translation. */
enum class routing_order {
  /** Decide from the virtual address; translate afterwards, always locally. */
  VIRTUAL_FIRST,
  /** Translate first; the physical address names the tile. */
  TRANSLATE_FIRST,
};

struct tile_router_module : public champsim::modules::module_base<tile_router_module, champsim::modules::environment_module>, public champsim::operable {
  explicit tile_router_module(champsim::chrono::picoseconds clock_period_) : champsim::operable(clock_period_) {}
  virtual ~tile_router_module() = default;

  // A static router has nothing to do on a cycle; it is operable so that it can
  // report statistics and, where the policy is adaptive, run an epoch.
  long operate() override { return 0; }
  long poll_cycle() override { return 1; }

  /** Which of the two machines this is. Callers branch on it; it is not hidden. */
  [[nodiscard]] virtual routing_order order() const = 0;

  /**
   * The tile owning a virtual address.
   *
   * Meaningful only under VIRTUAL_FIRST. Under TRANSLATE_FIRST the caller must
   * translate and read the tile out of the physical address instead, because
   * there is no answer to this question until it has.
   */
  [[nodiscard]] virtual std::size_t owner_of(champsim::origin origin, champsim::address vaddr) const = 0;

  /**
   * Where a newly faulted grain should be backed.
   *
   * Under VIRTUAL_FIRST this must agree with owner_of() or the machine breaks:
   * a context routes to one tile and finds its data on another. Under
   * TRANSLATE_FIRST it is a free choice, and it is where a placement policy
   * lives.
   */
  [[nodiscard]] virtual std::size_t placement_for(champsim::origin origin, champsim::address vaddr) = 0;

  /**
   * How many page-table roots the address space is split into.
   *
   * N under VIRTUAL_FIRST, because the tile is known before the walk starts and
   * each channel can own its own partition of the table. 1 under
   * TRANSLATE_FIRST, because choosing the root *is* the routing decision.
   */
  [[nodiscard]] virtual std::size_t page_table_roots() const = 0;

  // ---- adaptive hints ----
  //
  // A migration is evidence, not just a cost: it says a context on `from`
  // needed an address that lives on `to`. Policies that act on that evidence
  // implement these; the static routers ignore them.

  /**
   * A context on `from` had to move to `to` in order to reach `vaddr`.
   *
   * `token` identifies the invocation, and it is not optional detail: the
   * co-access relation a migration implies holds between addresses touched by
   * *the same* invocation. Uniting whatever two grains happened to migrate
   * consecutively anywhere in the machine merges everything into one component
   * and says nothing.
   */
  virtual void note_migration(champsim::origin /*origin*/, champsim::address /*vaddr*/, std::size_t /*from*/, std::size_t /*to*/, std::uint64_t /*token*/) {}

  /**
   * The allocator introduces itself.
   *
   * References resolve in configuration order and the allocator is declared
   * after the router, so it registers rather than the router holding a forward
   * reference it could not resolve -- the same arrangement the fabric uses.
   * A router that never moves anything ignores this.
   */
  virtual void attach_placement(page_placement_sink* /*placement*/) {}
};

} // namespace nmfc

#endif // NMFC_TILE_ROUTER_H
