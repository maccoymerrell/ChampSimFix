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

#ifndef LISTENER_H
#define LISTENER_H

#include <cstdint>
#include <vector>

#include "modules.h"

namespace champsim::modules
{
/**
 * Listener interface — observes run-wide events for reporting.
 *
 * Listeners are ordinary modules: declare them as top-level children in an
 * explicit config (interface "listener"), request extra models via the
 * --listeners CLI option, or rely on the default HEARTBEAT listener that
 * main creates when a config declares none. Producers reach the active
 * listeners through the emit_* free functions below.
 */
struct listener : public module_base<listener, environment_module> {
  virtual ~listener() = default;

  // A new phase began (fired once per phase, before module phase hooks).
  virtual void begin_phase(bool /*is_warmup*/) {}

  // A source consumer advanced. Totals are cumulative counts in the
  // consumer's own token unit and clock domain.
  virtual void progress(const token_consumer& /*consumer*/, uint64_t /*total_progress*/, uint64_t /*total_cycles*/) {}
};

// Active listener dispatch (single-threaded). main assembles the list once
// at startup; producers emit through these free functions.
void set_active_listeners(std::vector<listener*> active);
const std::vector<listener*>& active_listeners();
void emit_begin_phase(bool is_warmup);
void emit_progress(const token_consumer& consumer, uint64_t total_progress, uint64_t total_cycles);
} // namespace champsim::modules

#endif
