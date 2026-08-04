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

#include "event_listeners.h"
#include "identity_registry.h"
#include "json_stat_builder.h"
#include "module_phase.h"
#include "module_stat.h"
#include "modules.h"
#include "operable.h"
#include "phase_controller.h"
#include "phase_info.h"

const auto start_time = std::chrono::steady_clock::now();

std::chrono::seconds elapsed_time() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time); }

namespace champsim
{

// Discovery helpers: gather module_phase / module_stat instances across the environment so the orchestrator needn't re-query typed_view per cycle.
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
  // Walk every operable and dynamic_cast to module_phase, covering even operables exposed only via view("operable") (e.g. test mocks).
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

// Generic phase loop over any number of controllers: ABORT if any aborts, COMPLETE when all complete; on_consumer_complete fires once per consumer id.
// Controllers observe producer EOF themselves via producers_eof(). typed_view/module_phase cached once per phase.
void run_phase(const std::string& phase_name, bool is_warmup, bool roi, uint64_t length, modules::environment_module& env,
               std::vector<std::reference_wrapper<modules::phase_controller>>& controllers, champsim::chrono::clock& global_clock,
               std::function<void(unsigned)> on_consumer_complete)
{
  // typed_view is expensive; cache once per phase and reuse across all cycles.
  auto operables = env.typed_view<champsim::operable>("operable");
  for (std::size_t i = 0; i < std::size(operables); ++i) {
    operables[i].get().sort_order = static_cast<long>(i);
  }
  auto phase_modules = collect_module_phase(env);

  // Drive phase hooks on opted-in modules.
  for (auto& e : phase_modules) {
    e.ptr->begin_phase(is_warmup, roi);
  }

  const auto time_quantum = std::accumulate(std::cbegin(operables), std::cend(operables), champsim::chrono::clock::duration::max(),
                                            [](const auto acc, const operable& y) { return std::min(acc, y.clock_period); });

  for (modules::phase_controller& controller : controllers) {
    controller.begin_phase(phase_name, is_warmup, length);
  }

  std::set<unsigned> completed_consumers;
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

      // Consumer completion: surface notifications so packet_consumers can print their per-consumer messages; end-of-phase work happens once at the end.
      for (unsigned consumer_idx : controller.newly_completed_consumers()) {
        if (completed_consumers.insert(consumer_idx).second && on_consumer_complete) {
          on_consumer_complete(consumer_idx);
        }
      }
    }

    if (any_abort) {
      std::for_each(std::begin(operables), std::end(operables), [](champsim::operable& c) { c.print_deadlock(); });
      abort();
    }
    phase_status = all_complete ? modules::phase_controller::status::COMPLETE : modules::phase_controller::status::CONTINUE;
  }

  // End-of-phase hooks for module_phase participants.
  for (auto& e : phase_modules) {
    e.ptr->end_phase();
  }

  for (modules::phase_controller& controller : controllers) {
    controller.end_phase();
  }
}

// Collect phase statistics generically: publish every module_stat instance's lines / JSON under [interface][model][name].
static phase_stats collect_phase_stats(const phase_info& phase, modules::environment_module& env)
{
  phase_stats stats;
  stats.name = phase.name;

  // Workload identity comes from the producers themselves, in creation order (legacy: one trace per core, in core order).
  for (modules::packet_producer& src : env.typed_view<modules::packet_producer>("packet_producer")) {
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

    // Wrap the per-instance JSON as {model:{name:{...}}} for [interface][model][name] keying; the printer merges by interface.
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

// Assign framework-internal identities: consumers enumerate densely in config order; each producer gets its own id unless producers share a
// "producer_group" label. Configs never contain the numbers; only origins carry them.
void assign_identities(modules::environment_module& env)
{
  identities().clear();

  int next_consumer = 0;
  for (auto& sc : env.typed_view<modules::packet_consumer>("packet_consumer")) {
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

  uint32_t next_producer_group = 0;
  std::map<std::string, uint32_t> producer_group_labels;
  for (auto& src : env.typed_view<modules::packet_producer>("packet_producer")) {
    if (src.get().producer_id_pinned()) {
      continue; // mirrors another producer's id; owns no slot
    }
    const auto& label = src.get().producer_group();
    if (label.empty()) {
      src.get().set_producer_id(next_producer_group);
      identities().register_producer(next_producer_group, src.get().producer_name());
      ++next_producer_group;
    } else {
      auto [it, fresh] = producer_group_labels.try_emplace(label, next_producer_group);
      if (fresh) {
        ++next_producer_group;
      }
      src.get().set_producer_id(it->second);
      identities().register_producer(it->second, src.get().producer_name());
    }
  }

  auto num_producer_groups = modules::ModuleBuilder::globals().get_parameter<std::size_t>("num_producer_groups", true, std::size_t{0});
  if (num_producer_groups > 0 && static_cast<std::size_t>(next_producer_group) > num_producer_groups) {
    fmt::print("ERROR: {} producer groups found but num_producer_groups is {} — per-producer tables would index out of bounds. "
               "Remove or raise the root config key \"num_producer_groups\".\n",
               next_producer_group, num_producer_groups);
    std::exit(-1);
  }

  // Warm each producer's page-table root in producer-id order, so physical page assignment is a pure function of config rather than runtime walk timing
  // (matches historical construction-time order).
  for (auto& vm : env.typed_view<modules::vmem_module>("vmem")) {
    for (uint32_t producer = 0; producer < next_producer_group; ++producer) {
      (void)vm.get().get_pte_pa(champsim::origin{champsim::origin::invalid_id, producer}, champsim::page_number{}, vm.get().get_pt_levels());
    }
  }
}

identity_registry& identities()
{
  static identity_registry registry;
  return registry;
}

// simulation entry point
std::vector<phase_stats> main(modules::environment_module& env, std::vector<phase_info>& phases)
{
  assign_identities(env);

  for (champsim::operable& op : env.typed_view<champsim::operable>("operable")) {
    op.initialize();
  }

  // Gather the phase controllers: use all the environment provides, else create one default.
  auto controllers = env.typed_view<modules::phase_controller>("phase_controller");
  if (controllers.empty()) {
    auto pc_builder = modules::ModuleBuilder("phase_controller", "PHASE_CONTROLLER").add_parameter("deadlock_cycles", env.get_deadlock_cycles());
    controllers.push_back(*modules::phase_controller::create_instance(pc_builder, &env));
  }

  champsim::chrono::clock global_clock;
  std::vector<phase_stats> results;
  for (const auto& phase : phases) {
    const auto& [phase_name, is_warmup, roi, length] = phase;
    // C++17 can't capture a structured binding in a lambda (clang rejects it); copy the name into a local for on_complete.
    const auto phase_name_captured = phase_name;

    handle_event<Event::BEGIN_PHASE>(is_warmup);

    // Cache the packet_consumer view once per phase; the completion hook would otherwise call typed_view every cycle.
    auto consumers = env.typed_view<champsim::modules::packet_consumer>("packet_consumer");

    auto on_complete = [&](unsigned consumer_idx) {
      for (auto& sc : consumers) {
        if (sc.get().consumer_id() == static_cast<int>(consumer_idx)) {
          auto msg = sc.get().producer_finish_message(phase_name_captured);
          if (!msg.empty())
            fmt::print("{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
        }
      }
    };

    run_phase(phase_name, is_warmup, roi, length, env, controllers, global_clock, on_complete);

    // Print phase completion summary via packet_consumer hooks
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
