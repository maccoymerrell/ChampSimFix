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

#ifndef PHASE_CONTROLLER_H
#define PHASE_CONTROLLER_H

#include <cstdint>
#include <string>
#include <vector>

#include "modules.h"
#include "phase_info.h"

namespace champsim::modules
{
/**
 * Phase controller interface — manages phase completion and health monitoring.
 *
 * The phase controller owns the per-phase loop conditions: it observes
 * cycle progress, drives deadlock detection, aggregates the health that
 * each source consumer reports about itself, and signals when each source
 * has completed its share of the phase. Source EOF is observed by polling
 * token_consumer::source_eof() directly — there is no external EOF
 * notification. If the controller exposes a non-empty phase list via
 * get_phases(), the simulator runs that list instead of the default
 * warmup+sim pair.
 */
struct phase_controller : public module_base<phase_controller, environment_module> {
  virtual ~phase_controller() = default;

  enum class status { CONTINUE, COMPLETE, ABORT };

  // Called at the start of a phase
  virtual void begin_phase(const std::string& name, bool is_warmup, uint64_t length) = 0;

  // Called each cycle after all operables have operated.
  // progress: number of operations that made progress this cycle.
  // Returns CONTINUE, COMPLETE, or ABORT.
  virtual status advance(long progress) = 0;

  // Get ids of sources that newly completed since last advance()
  virtual std::vector<unsigned> newly_completed_sources() const = 0;

  // Called at end of phase for cleanup
  virtual void end_phase() = 0;

  // Returns the list of phases this controller wants to run.
  // If empty, the caller (main.cc / champsim::main) defines the phases.
  // Implement this to take full ownership of the run structure from config.
  virtual std::vector<champsim::phase_info> get_phases() const { return {}; }
};
} // namespace champsim::modules

#endif
