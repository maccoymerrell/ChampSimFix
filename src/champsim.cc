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
#include <set>
#include <vector>
#include <fmt/chrono.h>
#include <fmt/core.h>

#include "identity_registry.h"
#include "json_stat_builder.h"
#include "module_phase.h"
#include "module_stat.h"
#include "modules.h"
#include "operable.h"
#include "phase_info.h"

const auto start_time = std::chrono::steady_clock::now();

std::chrono::seconds elapsed_time() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time); }

namespace champsim
{

// Discovery helpers: gather module_phase / module_stat instances once so hooks avoid re-querying typed_view per cycle.
struct module_phase_entry {
  champsim::module_phase* ptr;
  std::string interface_name;
  std::string model;
  std::string name;
};
struct module_stat_entry {
  champsim::module_stat* ptr;
  std::string interface_name;
  std::string model;
  std::string name;
};

static std::vector<module_phase_entry> collect_module_phase(modules::environment_module& env)
{
  std::vector<module_phase_entry> out;
  // dynamic_cast every operable: covers even ones exposed only via view("operable") (e.g. unregistered test mocks).
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
    if (!to_ms)
      continue;
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

static bool less_by_time(const champsim::operable& lhs, const champsim::operable& rhs)
{
  return lhs.current_time < rhs.current_time || (lhs.current_time == rhs.current_time && lhs.sort_order < rhs.sort_order);
}

// Operate operables in current_time order. Must match modularize's std::sort for byte-identity:
// N<=16 stable insertion sort (ties by sort_order), N>16 unstable introsort on a copy.
long do_cycle(std::vector<std::reference_wrapper<champsim::operable>>& operables, champsim::chrono::clock& global_clock)
{
  long progress{0};
  if (std::size(operables) <= 16) {
    for (std::size_t i = 1; i < std::size(operables); ++i) {
      for (std::size_t j = i; j > 0 && less_by_time(operables[j].get(), operables[j - 1].get()); --j) {
        std::swap(operables[j], operables[j - 1]);
      }
    }
    for (champsim::operable& op : operables) {
      progress += op.operate_on(global_clock);
    }
    return progress;
  }

  // Reuse a scratch buffer so the per-cycle copy keeps capacity and does not allocate.
  static std::vector<std::reference_wrapper<champsim::operable>> sorted;
  sorted = operables;
  std::sort(std::begin(sorted), std::end(sorted),
            [](const champsim::operable& lhs, const champsim::operable& rhs) { return lhs.current_time < rhs.current_time; });
  for (champsim::operable& op : sorted) {
    progress += op.operate_on(global_clock);
  }
  return progress;
}

// Generic phase loop over any number of phase controllers: ABORT if any aborts, COMPLETE when all do.
// on_source_complete fires once per source id; controllers observe EOF via source_eof() (orchestrator is workload-agnostic).
void run_phase(const std::string& phase_name, bool is_warmup, bool roi, uint64_t length, modules::environment_module& env,
               std::vector<std::reference_wrapper<modules::phase_controller>>& controllers, champsim::chrono::clock& global_clock,
               std::function<void(unsigned)> on_source_complete)
{
  // typed_view is expensive; cache once per phase and reuse across all cycles.
  auto operables = env.typed_view<champsim::operable>("operable");
  for (std::size_t i = 0; i < std::size(operables); ++i) {
    operables[i].get().sort_order = static_cast<long>(i);
  }
  auto phase_modules = collect_module_phase(env);

  for (auto& e : phase_modules) {
    e.ptr->begin_phase(is_warmup, roi);
  }

  const auto time_quantum = std::accumulate(std::cbegin(operables), std::cend(operables), champsim::chrono::clock::duration::max(),
                                            [](const auto acc, const operable& y) { return std::min(acc, y.clock_period); });

  for (modules::phase_controller& controller : controllers) {
    controller.begin_phase(phase_name, is_warmup, length);
  }

  std::set<unsigned> completed_sources;
  modules::phase_controller::status phase_status{modules::phase_controller::status::CONTINUE};
  while (phase_status == modules::phase_controller::status::CONTINUE) {
    global_clock.tick(time_quantum);

    auto progress = do_cycle(operables, global_clock);

    bool any_abort = false;
    bool all_complete = true;
    for (modules::phase_controller& controller : controllers) {
      auto controller_status = controller.advance(progress);
      any_abort |= (controller_status == modules::phase_controller::status::ABORT);
      all_complete &= (controller_status == modules::phase_controller::status::COMPLETE);

      // Surface completion notifications so source_consumers can print per-source messages.
      for (unsigned source_idx : controller.newly_completed_sources()) {
        if (completed_sources.insert(source_idx).second && on_source_complete) {
          on_source_complete(source_idx);
        }
      }
    }

    if (any_abort) {
      std::for_each(std::begin(operables), std::end(operables), [](champsim::operable& c) { c.print_deadlock(); });
      abort();
    }
    phase_status = all_complete ? modules::phase_controller::status::COMPLETE : modules::phase_controller::status::CONTINUE;
  }

  for (auto& e : phase_modules) {
    e.ptr->end_phase();
  }

  for (modules::phase_controller& controller : controllers) {
    controller.end_phase();
  }
}

// Walk all module_stat instances, publishing lines / JSON under [interface][model][name].
static phase_stats collect_phase_stats(const phase_info& phase, modules::environment_module& env)
{
  phase_stats stats;
  stats.name = phase.name;

  // Workload identity comes from the sources, in creation order (legacy env: one trace per core).
  for (modules::workload_source& src : env.typed_view<modules::workload_source>("workload_source")) {
    auto desc = src.describe();
    if (!desc.empty()) {
      stats.trace_names.push_back(desc);
    }
  }

  // SIM and ROI passes share discovery; the bool selects which stat set to emit.
  auto stat_modules = collect_module_stat(env);
  for (auto& e : stat_modules) {
    auto sim_lines = e.ptr->print_stats(false);
    auto roi_lines = e.ptr->print_stats(true);
    stats.sim_lines.insert(stats.sim_lines.end(), sim_lines.begin(), sim_lines.end());
    stats.roi_lines.insert(stats.roi_lines.end(), roi_lines.begin(), roi_lines.end());

    // Wrap per-instance JSON as {model: {name: {...}}}; the printer merges by interface.
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

// Assign framework-internal identities: consumers enumerate densely in config order; each workload source
// gets its own stream (address space) unless sources share a "stream" label. Configs never contain the numbers.
void assign_identities(modules::environment_module& env)
{
  identities().clear();

  int next_consumer = 0;
  for (auto& sc : env.typed_view<modules::source_consumer>("source_consumer")) {
    if (sc.get().consumer_id_pinned()) {
      continue; // mirrors another consumer's identity; owns no slot
    }
    sc.get().set_consumer_id(next_consumer);
    identities().register_consumer(next_consumer, sc.get().consumer_name());
    ++next_consumer;
  }

  auto num_consumers = modules::ModuleBuilder::globals().get_parameter<std::size_t>("num_consumers", true, std::size_t{0});
  if (num_consumers > 0 && static_cast<std::size_t>(next_consumer) > num_consumers) {
    fmt::print("ERROR: {} consumers found but num_consumers is {} — per-consumer tables would index out of bounds. "
               "Remove or raise the root config key \"num_consumers\".\n",
               next_consumer, num_consumers);
    std::exit(-1);
  }

  uint32_t next_stream = 0;
  std::map<std::string, uint32_t> stream_labels;
  for (auto& src : env.typed_view<modules::stream_source>("stream_source")) {
    if (src.get().stream_id_pinned()) {
      continue; // mirrors another source's stream; owns no slot
    }
    const auto& label = src.get().stream_label();
    if (label.empty()) {
      src.get().set_stream_id(next_stream);
      identities().register_source(next_stream, src.get().source_name());
      ++next_stream;
    } else {
      auto [it, fresh] = stream_labels.try_emplace(label, next_stream);
      if (fresh) {
        ++next_stream;
      }
      src.get().set_stream_id(it->second);
      identities().register_source(it->second, src.get().source_name());
    }
  }

  auto num_streams = modules::ModuleBuilder::globals().get_parameter<std::size_t>("num_streams", true, std::size_t{0});
  if (num_streams > 0 && static_cast<std::size_t>(next_stream) > num_streams) {
    fmt::print("ERROR: {} streams found but num_streams is {} — per-stream tables would index out of bounds. "
               "Remove or raise the root config key \"num_streams\".\n",
               next_stream, num_streams);
    std::exit(-1);
  }

  // Warm each stream's page-table root in stream order: otherwise roots allocate at first walk,
  // making physical page assignment timing-dependent instead of a pure function of the configuration.
  for (auto& vm : env.typed_view<modules::vmem_module>("vmem")) {
    for (uint32_t stream = 0; stream < next_stream; ++stream) {
      (void)vm.get().get_pte_pa(champsim::origin{champsim::origin::invalid_id, stream}, champsim::page_number{}, vm.get().get_pt_levels());
    }
  }
}

identity_registry& identities()
{
  static identity_registry registry;
  return registry;
}

std::vector<phase_stats> main(modules::environment_module& env, std::vector<phase_info>& phases)
{
  assign_identities(env);

  for (champsim::operable& op : env.typed_view<champsim::operable>("operable")) {
    op.initialize();
  }

  // Use the environment's phase controllers, else create one default controller.
  auto controllers = env.typed_view<modules::phase_controller>("phase_controller");
  if (controllers.empty()) {
    auto pc_builder = modules::ModuleBuilder("phase_controller", "PHASE_CONTROLLER").add_parameter("deadlock_cycles", env.get_deadlock_cycles());
    controllers.push_back(*modules::phase_controller::create_instance(pc_builder, &env));
  }

  champsim::chrono::clock global_clock;
  std::vector<phase_stats> results;
  for (const auto& phase : phases) {
    const auto& [phase_name, is_warmup, roi, length] = phase;
    // A structured binding can't be lambda-captured in C++17 (clang rejects it); copy it.
    const auto phase_name_captured = phase_name;

    modules::emit_begin_phase(is_warmup);

    // Cache the source_consumer view once per phase (the completion hook would else call typed_view every cycle).
    auto consumers = env.typed_view<champsim::modules::source_consumer>("source_consumer");

    auto on_complete = [&](unsigned source_idx) {
      for (auto& sc : consumers) {
        if (sc.get().consumer_id() == static_cast<int>(source_idx)) {
          auto msg = sc.get().source_finish_message(phase_name_captured);
          if (!msg.empty())
            fmt::print("{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
        }
      }
    };

    run_phase(phase_name, is_warmup, roi, length, env, controllers, global_clock, on_complete);

    for (auto& sc : consumers) {
      auto msg = sc.get().phase_complete_message(phase_name);
      if (!msg.empty())
        fmt::print("{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
    }

    if (roi) {
      results.push_back(collect_phase_stats(phase, env));
    }
  }

  return results;
}
} // namespace champsim
