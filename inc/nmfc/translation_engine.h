/*
 * The translation service a function core talks to.
 *
 * A plain abstract base rather than a registered interface: NMFC_MMU implements
 * it and is reached by dynamic_cast from the ordinary channel reference the
 * configuration already holds, so swapping the mechanism costs no registry slot.
 *
 * Asynchronous by construction. A translation that misses everything is a walk
 * through memory, which is the cost this whole part of the design exists to
 * measure, so there is no synchronous form to accidentally reach for.
 */

#ifndef NMFC_TRANSLATION_ENGINE_H
#define NMFC_TRANSLATION_ENGINE_H

#include <cstdint>
#include <vector>

#include "address.h"
#include "origin.h"

namespace nmfc
{

/** A resolved translation, handed back to whoever asked for it. */
struct translation_done {
  std::uint64_t tag = 0;   // whatever the requester passed in; opaque here
  std::uint64_t vpage = 0; // page number at the granularity that resolved it
  std::uint64_t ppage = 0;
  bool huge = false;       // resolved from a grain-sized mapping
};

/** Where a translation was satisfied, for the statistics that matter. */
enum class translation_source : std::uint8_t {
  CONTEXT, // the requester's own carried entries: never reaches the MMU
  TLB,     // the tile's shared array
  WALK,    // a page table walk through memory
};

struct translation_engine {
  virtual ~translation_engine() = default;

  /**
   * Ask for the translation of `vaddr`. `tag` comes back untouched in the
   * completion, so a function core can key it by context slot.
   *
   * Returns false when the engine cannot take the request now, which is
   * ordinary back-pressure: the caller retries.
   */
  virtual bool request_translation(std::uint64_t tag, champsim::origin origin, champsim::address vaddr) = 0;

  /** Translations resolved since the last call. The caller drains and clears. */
  virtual std::vector<translation_done>& translation_completions() = 0;

  /** In-flight translations, for back-pressure decisions upstream. */
  [[nodiscard]] virtual std::size_t translation_occupancy() const = 0;
};

} // namespace nmfc

#endif // NMFC_TRANSLATION_ENGINE_H
