/*
 *    Copyright 2024 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PREFETCH_ACCEL_H
#define PREFETCH_ACCEL_H

#include <cstdint>

#include "chrono.h"

namespace champsim
{
/**
 * Simulator-side interface (NOT modeled hardware) that lets an open-loop replay source
 * approximate the bandwidth ramp a prefetcher would cause in a closed loop with an OoO core.
 *
 * A cache reports, for each completing DEMAND access, its ACTUAL latency and the
 * COUNTERFACTUAL latency it would have had WITHOUT L2 prefetching:
 *   - miss:          actual = counterfactual = the real fill latency
 *   - useful-pf hit: actual = HIT_LATENCY,     counterfactual = the prefetched line's fill latency
 *   - plain hit:     not reported (never the critical path)
 *
 * `instr_id` lets the sink bin accesses into ROB-sized windows. Accesses in one window are
 * in flight together (MLP), so the window's exposed time is its CRITICAL PATH = max(latency),
 * not the sum. The sink therefore credits `max(counterfactual) - max(actual)` per window
 * (critical-path saving, capped by any non-prefetched miss that is itself the bottleneck),
 * and advances its input stream by the total across serialized windows.
 *
 * A cache discovers its sink(s) by cross-casting its upper-level channels to this type;
 * consumers that don't implement it (e.g. an L1D in a full-processor run) are not sinks,
 * so this is inert outside the replay harness.
 */
struct prefetch_accel_sink {
  virtual void note_demand_latency(uint64_t instr_id, champsim::chrono::clock::duration actual,
                                   champsim::chrono::clock::duration counterfactual) = 0;
  virtual ~prefetch_accel_sink() = default;
};
} // namespace champsim

#endif
