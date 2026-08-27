/*
 * Hooks the NMFC models emit.
 *
 * A hook belongs beside its emitter, so these live here rather than in
 * champsim's hooks.h — declaring one touches no framework file. A hook with no
 * listeners costs a branch, so these may sit on the models' hot paths.
 */

#ifndef NMFC_HOOKS_H
#define NMFC_HOOKS_H

#include <cstddef>
#include <cstdint>

#include "hook.h"
#include "nmfc/nmfc_types.h"

namespace nmfc::hooks
{

/** An invocation was dispatched from a compute tile onto a memory tile. */
inline champsim::hook<void(std::uint64_t /*token*/, std::uint32_t /*home_host*/, std::size_t /*target_tile*/)> invoke{"nmfc_invoke"};

/** A context left one memory tile for another. Carries the count so far, so a
 *  listener can separate first hops from thrash without keeping its own map. */
inline champsim::hook<void(std::uint64_t /*token*/, std::size_t /*from_tile*/, std::size_t /*to_tile*/, std::uint32_t /*migrations_so_far*/)> migrate{
    "nmfc_migrate"};

/** An invocation finished and its return was handed to the fabric. */
inline champsim::hook<void(std::uint64_t /*token*/, std::size_t /*tile*/, std::uint64_t /*cycles_resident*/)> complete{"nmfc_complete"};

/** A translation resolved for a function core. `hit_level` is 0 for a
 *  per-context hit, 1 for a TLB hit, 2 for a completed walk. */
inline champsim::hook<void(std::size_t /*tile*/, std::uint64_t /*vpage*/, unsigned /*hit_level*/, std::uint64_t /*latency_cycles*/)> translate{
    "nmfc_translate"};

} // namespace nmfc::hooks

#endif // NMFC_HOOKS_H
