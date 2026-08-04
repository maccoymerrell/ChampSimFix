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

#ifndef PHASE_CONTROLLER_H
#define PHASE_CONTROLLER_H

#include <vector>

#include "modules.h"
#include "phase_info.h"

namespace champsim::modules
{
/**
 * Phase controller interface — owns and sequences the run's phases.
 *
 * The controller holds its own phase list (never exposed). begin_phase() steps it to the next
 * phase and informs the operables of it (module_phase begin_phase); advance() consumes each
 * cycle's progress and, when the current phase completes, ends it on the operables and reports
 * PHASE_COMPLETE, or DONE after the last phase (ABORT on deadlock/stall). The orchestrator only
 * ticks the operables, collects stats on PHASE_COMPLETE/DONE, and ends the run on DONE.
 */
struct phase_controller : public module_base<phase_controller, environment_module> {
  virtual ~phase_controller() = default;

  enum class status { CONTINUE, PHASE_COMPLETE, ABORT, DONE };

  // Step to the next phase and inform the operables of it (module_phase begin_phase). Called
  // by the orchestrator to start the run and again after each completed phase's stats.
  virtual void begin_phase() = 0;

  // Consume the cycle's summed progress. On completing the current phase, end it on the
  // operables (module_phase end_phase) and return PHASE_COMPLETE, or DONE after the last phase;
  // ABORT on deadlock/stall; otherwise CONTINUE.
  virtual status advance(long progress) = 0;

  // The current phase; its name/roi drive the orchestrator's stat collection.
  virtual const champsim::phase_info& phase() const = 0;

  // Ids of consumers that newly completed since the last advance().
  virtual std::vector<unsigned> newly_completed_consumers() const = 0;

  // Print this controller's phase plan at startup (labelled by the unit its consumers report).
  virtual void print_phase_plan() const {}
};
} // namespace champsim::modules

#endif
