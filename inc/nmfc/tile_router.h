/*
 * Who decides which memory tile owns an address — and, crucially, *when*.
 *
 * The design routes on the *physical* address: translate first, then read the
 * tile out of the frame. The OS therefore has total freedom to place a grain on
 * any tile and to move it later, which is what makes repartitioning expressible
 * at all; the price is that routing waits on translation. That price is smaller
 * than it looks, because the page table is *one table per address space,
 * duplicated on every tile* rather than partitioned N ways — each tile walks
 * its own copy, so the walk is local even though the root is not chosen by the
 * address. See nmfc_vmem.h and user ruling 2026-09-02 R2.
 *
 * The registered models:
 *
 *   PHYSICAL_ROUTER    Translate, then read the tile out of the physical
 *                      address. Placement of a newly faulted grain is a free
 *                      choice and therefore an actual policy; round-robin is
 *                      the neutral control an adaptive policy has to beat.
 *
 *   NUCA_ROUTER        The same routing rule, with a policy attached: a new
 *                      grain starts on the tile its address names, and balance
 *                      is applied afterwards by remapping whole components in
 *                      remap_grain() rather than by where a grain was first
 *                      touched. The default.
 *
 *   ADAPTIVE_ROUTER    The same routing rule with a pull-based policy.
 *
 *   CONGRUENT_ROUTER   A CONTROL, not the design. tile_of_virtual(va): the tile
 *                      is a field of the *virtual* address, so no translation
 *                      sits on the routing path and the page table can be
 *                      partitioned N ways, one root per channel over compacted
 *                      virtual addresses. The allocator then has no placement
 *                      freedom at all — a grain must be backed on the tile its
 *                      own address already named, and it can never be moved
 *                      without changing an address the program can see. This is
 *                      the policy where vmem places tiles; it is supported so
 *                      the two can be measured against each other, and it is
 *                      not used. No default may select it.
 *
 * (A RELOCATION_ROUTER — congruence plus a table of grains the OS has
 * deliberately moved — has been described but is *not implemented* and is not
 * registered. Do not offer it as a choice.)
 *
 * The interface is deliberately not a uniform abstraction over these.
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
  /**
   * The control (CONGRUENT_ROUTER only): decide from the virtual address and
   * translate afterwards, through a table partitioned one root per channel.
   * Kept selectable for comparison; nothing in the design chooses it.
   */
  VIRTUAL_FIRST,
  /** The design: translate first; the physical address names the tile. */
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
   * 1 under TRANSLATE_FIRST -- the design -- because choosing a root would
   * require the answer the walk produces. That single logical table is one
   * table per address space, *duplicated* on every tile, so each tile still
   * walks locally; see nmfc_vmem.h. N under VIRTUAL_FIRST (the control),
   * because the tile is known before the walk starts and each channel can own
   * its own partition instead.
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
