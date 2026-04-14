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

#include "champsim.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <numeric>
#include <vector>
#include <fmt/chrono.h>
#include <fmt/core.h>

#include "modules.h"
#include "event_listeners.h"
#include "operable.h"
#include "phase_info.h"

const auto start_time = std::chrono::steady_clock::now();

std::chrono::seconds elapsed_time() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time); }

namespace champsim
{

// Pure cycle operation: sort and operate all operables.
long do_cycle(modules::environment_module& env, champsim::chrono::clock& global_clock)
{
  auto operables = env.typed_view<champsim::operable>("operable");
  std::sort(std::begin(operables), std::end(operables),
            [](const champsim::operable& lhs, const champsim::operable& rhs) { return lhs.current_time < rhs.current_time; });

  long progress{0};
  for (champsim::operable& op : operables) {
    progress += op.operate_on(global_clock);
  }

  return progress;
}

// Generic phase loop.
// per_cycle_hook: called each cycle after operables run. Returns true if traces reached EOF.
// on_entity_complete: called for each entity index that newly completes this cycle.
void run_phase(const std::string& phase_name, bool is_warmup, uint64_t length,
               modules::environment_module& env, modules::phase_controller& controller,
               champsim::chrono::clock& global_clock,
               std::function<bool()> per_cycle_hook,
               std::function<void(unsigned)> on_entity_complete)
{
  auto operables = env.typed_view<champsim::operable>("operable");

  // Initialize phase
  for (champsim::operable& op : operables) {
    op.warmup = is_warmup;
    op.begin_phase();
  }

  const auto time_quantum = std::accumulate(std::cbegin(operables), std::cend(operables), champsim::chrono::clock::duration::max(),
                                            [](const auto acc, const operable& y) { return std::min(acc, y.clock_period); });

  controller.begin_phase(phase_name, is_warmup, length);

  modules::phase_controller::status phase_status{modules::phase_controller::status::CONTINUE};
  while (phase_status == modules::phase_controller::status::CONTINUE) {
    global_clock.tick(time_quantum);

    auto progress = do_cycle(env, global_clock);

    // Per-cycle hook (trace feeding, etc.)
    if (per_cycle_hook && per_cycle_hook()) {
      controller.notify_trace_eof();
    }

    phase_status = controller.advance(progress);

    // Handle newly completed entities
    for (unsigned entity_idx : controller.newly_completed_entities()) {
      for (champsim::operable& op : operables) {
        op.end_phase(entity_idx);
      }
      if (on_entity_complete) {
        on_entity_complete(entity_idx);
      }
    }

    if (phase_status == modules::phase_controller::status::ABORT) {
      std::for_each(std::begin(operables), std::end(operables), [](champsim::operable& c) { c.print_deadlock(); });
      abort();
    }
  }

  controller.end_phase();
}

// Collect phase statistics generically from all interfaces via the registry.
static phase_stats collect_phase_stats(const phase_info& phase, modules::environment_module& env)
{
  phase_stats stats;
  stats.name = phase.name;

  for (std::size_t i = 0; i < std::size(phase.trace_index); ++i) {
    stats.trace_names.push_back(phase.trace_names.at(phase.trace_index.at(i)));
  }

  for (const auto& iface : modules::interface_registry::get_interface_names()) {
    if (!modules::interface_registry::has_stats(iface)) continue;
    auto instances = env.view(iface);

    auto sim_text = modules::interface_registry::collect_text(iface, instances, false);
    auto roi_text = modules::interface_registry::collect_text(iface, instances, true);
    stats.sim_lines.insert(stats.sim_lines.end(), sim_text.begin(), sim_text.end());
    stats.roi_lines.insert(stats.roi_lines.end(), roi_text.begin(), roi_text.end());

    auto sim_j = modules::interface_registry::collect_json(iface, instances, false);
    auto roi_j = modules::interface_registry::collect_json(iface, instances, true);
    if (!sim_j.empty()) stats.sim_json[iface] = std::move(sim_j);
    if (!roi_j.empty()) stats.roi_json[iface] = std::move(roi_j);
  }

  return stats;
}

// simulation entry point
std::vector<phase_stats> main(modules::environment_module& env, std::vector<phase_info>& phases)
{
  for (champsim::operable& op : env.typed_view<champsim::operable>("operable")) {
    op.initialize();
  }

  // Get or create the phase controller.
  // If the environment provides one (explicit config), use it.
  // Otherwise, create a default INSTRUCTION_PHASE_CONTROLLER.
  modules::phase_controller* controller = nullptr;
  auto pc_view = env.typed_view<modules::phase_controller>("phase_controller");
  if (!pc_view.empty()) {
    controller = &pc_view.front().get();
  } else {
    auto pc_builder = modules::ModuleBuilder("phase_controller", "instruction_phase_controller")
      .add_parameter("deadlock_cycles", env.get_deadlock_cycles());
    controller = modules::phase_controller::create_instance(pc_builder, &env);
  }

  champsim::chrono::clock global_clock;
  std::vector<phase_stats> results;
  for (auto phase : phases) {
    auto [phase_name, is_warmup, length, trace_index, trace_names] = phase;

    handle_event<Event::BEGIN_PHASE>(is_warmup);

    // Per-cycle hook: check if any source_consumer's sources are exhausted
    auto per_cycle = [&]() -> bool {
      auto consumers = env.typed_view<champsim::modules::source_consumer>("source_consumer");
      return std::any_of(std::begin(consumers), std::end(consumers),
                         [](const auto& sc) { return sc.get().source_eof(); });
    };

    // Entity completion hook: delegate to source_consumer hooks
    auto on_complete = [&](unsigned entity_idx) {
      for (auto& sc : env.typed_view<modules::source_consumer>("source_consumer")) {
        if (sc.get().entity_index() == static_cast<int>(entity_idx)) {
          auto msg = sc.get().entity_finish_message(phase_name);
          if (!msg.empty())
            fmt::print("{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
        }
      }
    };

    run_phase(phase_name, is_warmup, length, env, *controller, global_clock, per_cycle, on_complete);

    // Print phase completion summary via source_consumer hooks
    for (auto& sc : env.typed_view<modules::source_consumer>("source_consumer")) {
      auto msg = sc.get().phase_complete_message(phase_name);
      if (!msg.empty())
        fmt::print("{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
    }

    if (!is_warmup) {
      results.push_back(collect_phase_stats(phase, env));
    }
  }

  return results;
}
} // namespace champsim
