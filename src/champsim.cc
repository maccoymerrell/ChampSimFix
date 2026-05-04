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
#include "module_phase.h"
#include "module_stat.h"
#include "json_stat_builder.h"
#include "event_listeners.h"
#include "operable.h"
#include "phase_info.h"

const auto start_time = std::chrono::steady_clock::now();

std::chrono::seconds elapsed_time() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time); }

namespace champsim
{

// Discovery helpers: return the [(interface, name, instance_any)] across the
// environment so the orchestrator can drive module_phase / module_stat hooks
// without re-querying typed_view per cycle.
struct module_phase_entry  { champsim::module_phase* ptr;  std::string interface_name; std::string model; std::string name; };
struct module_stat_entry   { champsim::module_stat*  ptr;  std::string interface_name; std::string model; std::string name; };

static std::vector<module_phase_entry> collect_module_phase(modules::environment_module& env)
{
  std::vector<module_phase_entry> out;
  // Walk every operable and dynamic_cast to module_phase. This naturally
  // covers all operables — including ones the environment exposes only via
  // view("operable") (e.g. test mocks that aren't registered interfaces).
  for (auto& op_ref : env.typed_view<champsim::operable>("operable")) {
    if (auto* mp = dynamic_cast<champsim::module_phase*>(&op_ref.get())) {
      out.push_back({mp, "operable", "", ""});
    }
  }
  return out;
}

static std::vector<module_stat_entry> collect_module_stat(modules::environment_module& env)
{
  std::vector<module_stat_entry> out;
  for (const auto& iface : modules::interface_registry::get_interface_names()) {
    auto to_ms = modules::interface_registry::get_to_module_stat(iface);
    if (!to_ms) continue;
    for (const auto& inst : env.view(iface)) {
      auto* ms = to_ms(inst);
      if (ms) {
        auto id = modules::interface_registry::identify(iface, inst);
        out.push_back({ms, iface, id.model, id.name});
      }
    }
  }
  return out;
}

// Pure cycle operation: sort and operate all operables.
// Operables vector is passed in to avoid re-querying the environment each cycle.
long do_cycle(std::vector<std::reference_wrapper<champsim::operable>>& operables, champsim::chrono::clock& global_clock)
{
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
// Caches typed_view and module_phase results once per phase for performance.
void run_phase(const std::string& phase_name, bool is_warmup, uint64_t length,
               modules::environment_module& env, modules::phase_controller& controller,
               champsim::chrono::clock& global_clock,
               std::function<bool()> per_cycle_hook,
               std::function<void(unsigned)> on_entity_complete)
{
  // typed_view is expensive; cache once per phase and reuse across all cycles.
  auto operables = env.typed_view<champsim::operable>("operable");
  auto phase_modules = collect_module_phase(env);

  // Drive phase hooks on opted-in modules.
  for (auto& e : phase_modules) {
    e.ptr->begin_phase(is_warmup, !is_warmup);
  }

  const auto time_quantum = std::accumulate(std::cbegin(operables), std::cend(operables), champsim::chrono::clock::duration::max(),
                                            [](const auto acc, const operable& y) { return std::min(acc, y.clock_period); });

  controller.begin_phase(phase_name, is_warmup, length);

  modules::phase_controller::status phase_status{modules::phase_controller::status::CONTINUE};
  while (phase_status == modules::phase_controller::status::CONTINUE) {
    global_clock.tick(time_quantum);

    auto progress = do_cycle(operables, global_clock);

    // Per-cycle hook (trace feeding, etc.)
    if (per_cycle_hook && per_cycle_hook()) {
      controller.notify_trace_eof();
    }

    phase_status = controller.advance(progress);

    // Entity completion: surface notifications so source_consumers can print
    // their per-entity messages. End-of-phase work is done once at the end.
    for (unsigned entity_idx : controller.newly_completed_entities()) {
      if (on_entity_complete) {
        on_entity_complete(entity_idx);
      }
    }

    if (phase_status == modules::phase_controller::status::ABORT) {
      std::for_each(std::begin(operables), std::end(operables), [](champsim::operable& c) { c.print_deadlock(); });
      abort();
    }
  }

  // End-of-phase hooks for module_phase participants.
  for (auto& e : phase_modules) {
    e.ptr->end_phase();
  }

  controller.end_phase();
}

// Collect phase statistics generically: walk all module_stat instances and
// publish their lines / JSON under [interface][model][name].
static phase_stats collect_phase_stats(const phase_info& phase, modules::environment_module& env)
{
  phase_stats stats;
  stats.name = phase.name;

  for (std::size_t i = 0; i < std::size(phase.trace_index); ++i) {
    stats.trace_names.push_back(phase.trace_names.at(phase.trace_index.at(i)));
  }

  // SIM and ROI passes share discovery; the bool selects which stat set to emit.
  auto stat_modules = collect_module_stat(env);
  for (auto& e : stat_modules) {
    auto sim_lines = e.ptr->print_stats(false);
    auto roi_lines = e.ptr->print_stats(true);
    stats.sim_lines.insert(stats.sim_lines.end(), sim_lines.begin(), sim_lines.end());
    stats.roi_lines.insert(stats.roi_lines.end(), roi_lines.begin(), roi_lines.end());

    // For [interface][model][name] keying we wrap the per-instance JSON in
    // a {model: {name: {...}}} object and let the printer merge by interface.
    champsim::json_stat_builder sim_builder, roi_builder;
    e.ptr->json_stats(sim_builder, false);
    e.ptr->json_stats(roi_builder, true);

    nlohmann::json sim_wrapped = nlohmann::json::object();
    sim_wrapped[e.model][e.name] = sim_builder.json();
    nlohmann::json roi_wrapped = nlohmann::json::object();
    roi_wrapped[e.model][e.name] = roi_builder.json();

    stats.sim_json[e.interface_name].emplace_back(e.name, std::any{sim_wrapped});
    stats.roi_json[e.interface_name].emplace_back(e.name, std::any{roi_wrapped});
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
    auto pc_builder = modules::ModuleBuilder("phase_controller", "INSTRUCTION_PHASE_CONTROLLER")
      .add_parameter("deadlock_cycles", env.get_deadlock_cycles());
    controller = modules::phase_controller::create_instance(pc_builder, &env);
  }

  champsim::chrono::clock global_clock;
  std::vector<phase_stats> results;
  for (auto phase : phases) {
    auto [phase_name, is_warmup, length, trace_index, trace_names] = phase;

    handle_event<Event::BEGIN_PHASE>(is_warmup);

    // Cache the source_consumer view once per phase; the per-cycle and
    // entity-completion hooks would otherwise call typed_view every cycle.
    auto consumers = env.typed_view<champsim::modules::source_consumer>("source_consumer");

    auto per_cycle = [&consumers]() -> bool {
      return std::any_of(std::begin(consumers), std::end(consumers),
                         [](const auto& sc) { return sc.get().source_eof(); });
    };

    auto on_complete = [&](unsigned entity_idx) {
      for (auto& sc : consumers) {
        if (sc.get().entity_index() == static_cast<int>(entity_idx)) {
          auto msg = sc.get().source_finish_message(phase_name);
          if (!msg.empty())
            fmt::print("{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
        }
      }
    };

    run_phase(phase_name, is_warmup, length, env, *controller, global_clock, per_cycle, on_complete);

    // Print phase completion summary via source_consumer hooks
    for (auto& sc : consumers) {
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
