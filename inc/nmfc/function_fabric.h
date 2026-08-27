/*
 * The function fabric: the network that carries invocations out to memory
 * tiles, migrations between them, and returns back to the compute tile that
 * issued the work.
 *
 * It also owns the *placement decision*. Function code is duplicated on every
 * channel, so choosing which copy to invoke is choosing which tile runs the
 * work — and because the copies sit on consecutive grains, that choice is one
 * add rather than a lookup. Load balancing therefore belongs here, in what
 * stands in for the OS, rather than in the compiler.
 */

#ifndef NMFC_FUNCTION_FABRIC_H
#define NMFC_FUNCTION_FABRIC_H

#include <cstddef>
#include <cstdint>

#include "modules.h"
#include "nmfc/nmfc_types.h"
#include "operable.h"

namespace nmfc
{

struct function_core_module;

/**
 * Where a completed invocation lands back on the compute tile.
 *
 * A plain abstract base rather than a registered interface: the host core
 * implements it, and the fabric reaches it through the pointer the core handed
 * over at attach time, so it never needs a registry slot of its own.
 */
struct offload_sink {
  virtual ~offload_sink() = default;
  virtual void accept_return(const completion_msg& msg) = 0;
};

struct function_fabric_module : public champsim::modules::module_base<function_fabric_module, champsim::modules::environment_module>,
                                public champsim::operable {
  explicit function_fabric_module(champsim::chrono::picoseconds clock_period_) : champsim::operable(clock_period_) {}
  virtual ~function_fabric_module() = default;

  // ---- attachment ----
  //
  // References resolve in configuration order, so the fabric is declared before
  // the tiles and hosts that use it. They register themselves at construction
  // rather than the fabric holding forward references it could not resolve.

  /** A memory tile's function core announces itself. */
  virtual void attach_tile(std::size_t index, function_core_module* core) = 0;

  /** A compute tile's tracking unit announces itself; returns its host id. */
  virtual std::uint32_t attach_host(offload_sink* host) = 0;

  // ---- traffic ----
  //
  // Each returns false when the fabric is full. That refusal is the design's
  // back-pressure path, not an error: it propagates to the issuing core's ROB.

  /** Compute tile → memory tile. The fabric picks the tile and resolves the copy. */
  virtual bool dispatch(const invocation_msg& msg) = 0;

  /** Memory tile → memory tile, because the context's next address lives elsewhere. */
  virtual bool migrate(migration_msg msg) = 0;

  /** Memory tile → compute tile: the invocation finished. */
  virtual bool finish(const completion_msg& msg) = 0;

  /** How many tiles are attached. */
  [[nodiscard]] virtual std::size_t num_tiles() const = 0;
};

} // namespace nmfc

#endif // NMFC_FUNCTION_FABRIC_H
