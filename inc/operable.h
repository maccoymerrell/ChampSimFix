/*
 *    Copyright 2023 The ChampSim Contributors
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

#ifndef OPERABLE_H
#define OPERABLE_H

#include "chrono.h"

namespace champsim
{
class operable
{
public:
  champsim::chrono::picoseconds clock_period{};
  champsim::chrono::clock::time_point current_time{};
  long sort_order{0}; // registration index; stable tie-break for the do_cycle sort

  operable();
  virtual ~operable() = default;
  explicit operable(champsim::chrono::picoseconds clock_period);

  long _operate();
  long operate_on(const champsim::chrono::clock& clock);

  virtual void initialize() {} // LCOV_EXCL_LINE
  virtual long operate() = 0;

  /**
   * Idle-skip hook, called before each local cycle would be simulated.
   * current_time has already been advanced to the cycle under
   * consideration, so this hook observes the same end-of-cycle timestamp
   * that operate() would.
   *
   * Return 0 to simulate this cycle (operate() runs as usual).
   * Return n > 0 to skip n local cycles (this one plus n-1 more):
   * current_time ends up n * clock_period past the previous cycle with no
   * simulation, and the operable is not reconsidered until the global
   * clock reaches its new current_time.
   *
   * A skipped cycle contributes no progress, exactly like an idle
   * operate() call. Implementations must uphold two rules:
   *  - Any submodule hook that is contractually per-cycle (e.g. a cache's
   *    prefetcher_cycle_operate) must still be ticked here on skipped
   *    cycles, so submodules observe an unbroken cycle stream.
   *  - An operable that can receive work by external push (channel add_*,
   *    returned-queue push) must not skip more than 1 cycle at a time,
   *    or it would sleep through an arrival and change timing.
   *
   * Skipping is disabled globally when set_skip_enabled(false) (config
   * root key "cycle_skip") — used for A/B behavior verification.
   */
  virtual long poll_cycle() { return 0; } // LCOV_EXCL_LINE

  /**
   * True when this operable holds self-scheduled work that will complete at
   * a known future time WITHOUT external input — e.g. a DRAM refresh in
   * flight, a bank busy timer. While any operable (or source consumer)
   * reports pending work, zero global progress is treated as scheduled
   * quiet time rather than a deadlock.
   *
   * Do NOT return true for work that depends on another module responding
   * (e.g. an MSHR waiting on a lower level): that is exactly the state the
   * deadlock detector exists to catch.
   */
  virtual bool has_pending_work() const { return false; } // LCOV_EXCL_LINE

  virtual void print_deadlock() {} // LCOV_EXCL_LINE
  virtual void end_simulation() {} // LCOV_EXCL_LINE

  static void set_skip_enabled(bool enabled);
  static bool skip_enabled();

  [[deprecated]] uint64_t current_cycle() const;
};

} // namespace champsim

#endif
