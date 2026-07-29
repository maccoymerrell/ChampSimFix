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

  // Idle-skip hook, run before each local cycle (current_time already advanced to
  // it). Return 0 to simulate this cycle; return n>0 to skip n cycles (time jumps
  // n*clock_period with no simulation). Two rules: still tick any contractually
  // per-cycle submodule hook (e.g. prefetcher_cycle_operate) on skipped cycles,
  // and skip at most 1 cycle if work can arrive by external push. Globally
  // disabled by cycle_skip=false (A/B behavior verification).
  virtual long poll_cycle() { return 0; } // LCOV_EXCL_LINE

  // True when self-scheduled work will complete at a known future time with NO
  // external input (DRAM refresh in flight, bank busy timer): while any reports
  // pending work, zero global progress counts as scheduled quiet time, not
  // deadlock. Must NOT be true for work awaiting another module (e.g. an MSHR on
  // a lower level) — that is exactly what the deadlock detector must catch.
  virtual bool has_pending_work() const { return false; } // LCOV_EXCL_LINE

  virtual void print_deadlock() {} // LCOV_EXCL_LINE
  virtual void end_simulation() {} // LCOV_EXCL_LINE

  static void set_skip_enabled(bool enabled);
  static bool skip_enabled();

  [[deprecated]] uint64_t current_cycle() const;
};

} // namespace champsim

#endif
