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

// Discovery helper: gather module_stat instances across the environment so stat collection needn't re-query typed_view.
struct module_stat_entry {
  champsim::module_stat* ptr;
  std::string interface_name;
  std::string model;
  std::string name;
};

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
std::vector<phase_stats> main(modules::environment_module& env)
{
  assign_identities(env);

  for (champsim::operable& op : env.typed_view<champsim::operable>("operable")) {
    op.initialize();
  }

  auto controllers = env.typed_view<modules::phase_controller>("phase_controller");
  if (controllers.empty()) {
    fmt::print("ERROR: no phase controller declared\n");
    return {};
  }
  auto operables = env.typed_view<champsim::operable>("operable");
  for (std::size_t i = 0; i < std::size(operables); ++i) {
    operables[i].get().sort_order = static_cast<long>(i);
  }
  const auto time_quantum = std::accumulate(std::cbegin(operables), std::cend(operables), champsim::chrono::clock::duration::max(),
                                            [](const auto acc, const operable& y) { return std::min(acc, y.clock_period); });
  auto consumers = env.typed_view<champsim::modules::packet_consumer>("packet_consumer");

  champsim::chrono::clock global_clock;
  std::vector<phase_stats> results;

  // Each controller owns its own phases and informs the operables of them. main ticks the
  // operables each cycle, collects a phase's stats when a controller ends it (before the next
  // begin_phase resets the modules' ROI stats), ends the run once every controller is DONE, and
  // aborts if any controller aborts.
  std::vector<bool> finished(controllers.size(), false);
  std::vector<std::set<unsigned>> completed(controllers.size());
  std::size_t finished_count = 0;

  for (modules::phase_controller& controller : controllers) {
    controller.begin_phase();
    handle_event<Event::BEGIN_PHASE>(controller.phase().is_warmup);
  }

  while (finished_count < controllers.size()) {
    global_clock.tick(time_quantum);
    auto progress = do_cycle(operables, global_clock);

    bool any_abort = false;
    std::size_t idx = 0;
    for (modules::phase_controller& controller : controllers) {
      if (finished[idx]) {
        ++idx;
        continue;
      }
      auto phase_status = controller.advance(progress);

      // Producer-finish messages, once per consumer id.
      for (unsigned consumer_idx : controller.newly_completed_consumers()) {
        if (completed[idx].insert(consumer_idx).second) {
          for (auto& sc : consumers) {
            if (sc.get().consumer_id() == static_cast<int>(consumer_idx)) {
              auto msg = sc.get().producer_finish_message(controller.phase().name);
              if (!msg.empty())
                fmt::print("{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
            }
          }
        }
      }

      if (phase_status == modules::phase_controller::status::ABORT) {
        any_abort = true;
      } else if (phase_status != modules::phase_controller::status::CONTINUE) {
        // Phase ended: completion summaries + stats, before the next begin_phase resets ROI stats.
        for (auto& sc : consumers) {
          auto msg = sc.get().phase_complete_message(controller.phase().name);
          if (!msg.empty())
            fmt::print("{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
        }
        if (controller.phase().roi) {
          results.push_back(collect_phase_stats(controller.phase(), env));
        }

        if (phase_status == modules::phase_controller::status::DONE) {
          finished[idx] = true;
          ++finished_count;
        } else { // PHASE_COMPLETE: start this controller's next phase
          controller.begin_phase();
          handle_event<Event::BEGIN_PHASE>(controller.phase().is_warmup);
          completed[idx].clear();
        }
      }
      ++idx;
    }

    if (any_abort) {
      std::for_each(std::begin(operables), std::end(operables), [](champsim::operable& c) { c.print_deadlock(); });
      abort();
    }
  }

  return results;
}
} // namespace champsim
