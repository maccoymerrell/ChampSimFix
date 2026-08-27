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
 */

#ifndef NMFC_VMEM_H
#define NMFC_VMEM_H

#include <cstdint>
#include <optional>

#include "nmfc/tile_map.h"

namespace nmfc
{

/** Where the pseudo-compiler wants a virtual page to live. */
struct placement_hint {
  mapping_mode mode = mapping_mode::STANDARD;
  std::uint32_t tile = 0;
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

  /** Ask that (asid, vpage) be backed from `hint`'s region and tile. */
  virtual void hint_placement(std::uint32_t asid, std::uint64_t vpage, placement_hint hint) = 0;

  /**
   * The grain-granular mapping for a virtual address, if one is established.
   * Lets an MMU hold one entry per grain instead of one per 4 KiB page.
   */
  [[nodiscard]] virtual std::optional<std::uint64_t> grain_mapping(std::uint32_t asid, std::uint64_t vaddr) const = 0;
};

} // namespace nmfc

#endif // NMFC_VMEM_H
