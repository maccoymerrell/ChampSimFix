/*
 *    Copyright 2026 The ChampSim Contributors
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

#ifndef MODULE_PHASE_H
#define MODULE_PHASE_H

namespace champsim
{

// Opt-in interface for modules needing phase-edge notifications; inherit
// alongside your module interface. Flags are passed (not stored) so a
// controller can't flip them mid-phase; both hooks are pure-virtual by design.
struct module_phase {
  virtual ~module_phase() = default;

  // Called at each phase start, before cycles run. roi (contributes to ROI
  // stats) is usually the inverse of warmup but need not be.
  virtual void begin_phase(bool warmup, bool roi) = 0;

  /** Called at the end of each phase, after all cycles complete. */
  virtual void end_phase() = 0;
};

} // namespace champsim

#endif // MODULE_PHASE_H
