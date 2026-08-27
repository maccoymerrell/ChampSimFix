/*
 * The function image: where an invocation's body lives while it executes.
 *
 * The design backs every function on every channel, so the body is not a thing
 * that travels — a migrating context carries a pointer into this store, not
 * code. The trace reader publishes a body when it reads the CALL record; the
 * dispatcher looks it up; the function core executes out of it; and it retires
 * when the invocation returns.
 *
 * Keeping it behind an interface (rather than a bare map inside the producer)
 * is what lets the producer, the host core's tracking unit, and every function
 * core reach the same bodies through ordinary @-reference wiring.
 */

#ifndef NMFC_FUNCTION_IMAGE_H
#define NMFC_FUNCTION_IMAGE_H

#include <cstdint>

#include "modules.h"
#include "nmfc/nmfc_types.h"

namespace nmfc
{

struct function_image_module : public champsim::modules::module_base<function_image_module, champsim::modules::environment_module> {
  virtual ~function_image_module() = default;

  /** Take ownership of a body. Called once per invocation, at trace-read time. */
  virtual void publish(function_body body) = 0;

  /** The body for this token, or nullptr if it was never published or already retired. */
  [[nodiscard]] virtual const function_body* lookup(std::uint64_t token) const = 0;

  /** Release a body. Called when the invocation returns. */
  virtual void retire(std::uint64_t token) = 0;

  /** Bodies currently held. Its high-water mark bounds what the store costs. */
  [[nodiscard]] virtual std::size_t occupancy() const = 0;
};

} // namespace nmfc

#endif // NMFC_FUNCTION_IMAGE_H
