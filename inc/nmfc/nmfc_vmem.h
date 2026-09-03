/*
 * The placement side of the page allocator, reachable without a registry slot.
 *
 * `vmem_module` has no way to say "put this virtual page on that tile", and it
 * should not: placement is an NMFC concern, not something every virtual memory
 * model owes an answer to. So the trace reader reaches the allocator through
 * this plain abstract base, by dynamic_cast from the ordinary `@VMEM`
 * reference it already holds.
 *
 * The huge-page accessor exists for the same reason on the other side: the MMU
 * wants the grain-granular mapping so a single TLB entry can cover a whole
 * grain, while the page-table walker below it still asks 4 KiB questions.
 *
 * THE TRANSLATION SURFACE (user ruling 2026-09-02 R2, R12). The design routes
 * on the *physical* address: translate first, then read the owning tile out of
 * the frame. It walks ONE PAGE TABLE PER ADDRESS SPACE, and that table is
 * DUPLICATED on every tile -- each tile holds its own copy of the page-table
 * pages, so the root is not chosen by the address (it cannot be: choosing it
 * would need the answer the walk produces) and yet every walk is still served
 * from local memory. TLBs above it are shared; the tables are not.
 *
 * The alternative -- one table PARTITIONED N ways, one root per channel over
 * compacted virtual addresses, so the address picks the root -- belongs to
 * CONGRUENT_ROUTER, which is a CONTROL and is chosen by no default. Everything
 * below that reads `page_table_roots() > 1` is serving that control; the `== 1`
 * arm is the design.
 */

#ifndef NMFC_VMEM_H
#define NMFC_VMEM_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "address.h"
#include "chrono.h"
#include "origin.h"
#include "nmfc/tile_map.h"

namespace nmfc
{

/** Where the pseudo-compiler wants a virtual page to live. */
struct placement_hint {
  mapping_mode mode = mapping_mode::STANDARD;
  std::uint32_t tile = 0;
  /** One copy per channel; the tile is chosen when the address is translated. */
  bool replicated = false;
};

/**
 * Accepts placement hints from the trace and answers grain-granular mappings.
 *
 * Implemented by NMFC_VMEM. A hint must arrive before the page is first
 * touched; after that the allocation is fixed, because the mapping mode is
 * stamped into the physical address at allocation and never changes.
 */
struct page_placement_sink {
  virtual ~page_placement_sink() = default;

  /**
   * Move an already-backed grain to another tile.
   *
   * Only meaningful where routing follows the physical address; under
   * congruence the tile is named by the virtual address, so "moving" a grain
   * would mean changing an address the program can see. Returns false when the
   * grain is not established, is replicated, or the target has no room.
   *
   * Everything holding a translation for this grain is now wrong, which is
   * what mapping_generation() is for: it is a shootdown, modelled coarsely as
   * one, rather than a remap that silently leaves stale entries behind.
   */
  virtual bool remap_grain(std::uint32_t asid, std::uint64_t vgrain, std::size_t tile) = 0;

  /**
   * Bumped whenever a mapping changes under a cached translation.
   *
   * Anything that caches a virtual-to-physical answer compares this against
   * what it saw last, and if they differ reads the log below to find out which
   * grains actually moved. Invalidating everything on any remap would be a
   * model of a shootdown so coarse that it dominates the very policy it is
   * meant to price -- measured, it doubled the runtime for a 2% change in
   * migrations.
   */
  [[nodiscard]] virtual std::uint64_t mapping_generation() const = 0;

  /** The grains that moved, in order. Consumers keep an index into this. */
  [[nodiscard]] virtual const std::vector<std::pair<std::uint32_t, std::uint64_t>>& remap_log() const = 0;

  /** Ask that (asid, vpage) be backed from `hint`'s region and tile. */
  virtual void hint_placement(std::uint32_t asid, std::uint64_t vpage, placement_hint hint) = 0;

  /**
   * The grain-granular mapping for a virtual address, if one is established.
   * Lets an MMU hold one entry per grain instead of one per 4 KiB page.
   */
  [[nodiscard]] virtual std::optional<std::uint64_t> grain_mapping(std::uint32_t asid, std::uint64_t vaddr) const = 0;

  /**
   * The same, as seen from `tile`.
   *
   * A replicated grain has one physical copy per channel, so its translation
   * is not a function of the virtual address alone -- it depends on who is
   * asking. An MMU belongs to exactly one tile, so it always knows, and a
   * context that migrates re-translates the *same* virtual address on arrival
   * and gets its new tile's copy. That is why an invocation's program counter
   * does not change when it moves.
   *
   * For everything else this is grain_mapping(): one address, one frame.
   */
  [[nodiscard]] virtual std::optional<std::uint64_t> grain_mapping_on(std::uint32_t asid, std::uint64_t vaddr, std::size_t tile) const = 0;

  /**
   * Whether this address will resolve from a grain-sized mapping.
   *
   * Asked *before* a frame exists, which is the point: an MMU has to know what
   * page size it is walking for before it starts, and "is a grain mapping
   * established" is a different question that is false on every first touch.
   * Answering with the latter makes the first access to a grain walk as a small
   * page, fill the small array with an entry nothing will reuse, and every
   * access after it walk again as a huge one -- two walks per grain, for good.
   */
  [[nodiscard]] virtual bool is_grain_mapped(std::uint32_t asid, std::uint64_t vaddr) const = 0;

  /** Page-granular translation as seen from `tile`, for the walk below the MMU. */
  [[nodiscard]] virtual std::uint64_t page_mapping_on(champsim::origin origin, std::uint64_t vpage, std::size_t tile) = 0;

  /**
   * Where the page-table entry for this walk lives, asked as `tile`.
   *
   * Under the design the table is duplicated, so the entry the asker wants is
   * in the asker's own copy: the answer follows `tile`, not the address, and
   * that is what keeps a walk local without letting the address pick the root.
   * Under the CONGRUENT control the table is partitioned instead, so an
   * ordinary address sits in exactly one channel's slice and the root follows
   * the address -- except for a *replicated* address, which sits in every
   * slice with each channel naming its own copy, and there too the answer must
   * follow the asker. Getting this wrong sends one tile walking through
   * another tile's table, which a tile port catches as a locality violation a
   * long way from the cause.
   */
  [[nodiscard]] virtual std::pair<champsim::address, champsim::chrono::clock::duration> pte_address_on(champsim::origin origin, std::uint64_t vpage,
                                                                                                      std::size_t level, std::size_t tile) = 0;
};

} // namespace nmfc

#endif // NMFC_VMEM_H
