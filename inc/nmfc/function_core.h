/*
 * The function core interface: what a memory tile's near-memory engine looks
 * like to the fabric.
 *
 * Deliberately narrow. The fabric only needs to know whether a tile can take
 * more work and how to hand it some; everything about how contexts are
 * scheduled, translated, and executed stays inside the model.
 */

#ifndef NMFC_FUNCTION_CORE_H
#define NMFC_FUNCTION_CORE_H

#include <cstddef>

#include "modules.h"
#include "nmfc/nmfc_types.h"
#include "operable.h"

namespace nmfc
{

struct function_core_module : public champsim::modules::module_base<function_core_module, champsim::modules::environment_module>, public champsim::operable {
  explicit function_core_module(champsim::chrono::picoseconds clock_period_) : champsim::operable(clock_period_) {}
  virtual ~function_core_module() = default;

  /** Which memory tile this core belongs to. Fixed at construction. */
  [[nodiscard]] virtual std::size_t tile_index() const = 0;

  /**
   * Start a fresh invocation. Returns false when no context slot is free —
   * which is the back-pressure that eventually stalls the issuing host's ROB,
   * and is the correct behaviour rather than a condition to work around.
   */
  virtual bool accept(const invocation_msg& msg) = 0;

  /** Take a context that migrated here from another tile. Same refusal rule. */
  virtual bool accept_migration(const context& ctx) = 0;

  /** Free context slots, for the fabric's least-loaded placement policy. */
  [[nodiscard]] virtual std::size_t free_contexts() const = 0;

  /** Total context slots. free_contexts()/num_contexts() is the occupancy. */
  [[nodiscard]] virtual std::size_t num_contexts() const = 0;
};

} // namespace nmfc

#endif // NMFC_FUNCTION_CORE_H
