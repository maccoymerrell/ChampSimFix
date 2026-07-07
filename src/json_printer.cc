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

#include <any>
#include <nlohmann/json.hpp>

#include "stats_printer.h"

namespace
{
// Each entry in `entries` is a pair (instance_name, std::any holding a
// nlohmann::json wrapped under {model: {name: {...}}}).
// We merge them so the final shape is {interface: {model: {name: {...}}}}.
nlohmann::json compile_section(
    const std::map<std::string, std::vector<std::pair<std::string, std::any>>>& entries)
{
  nlohmann::json section = nlohmann::json::object();
  for (const auto& [iface, modules] : entries) {
    nlohmann::json iface_json = nlohmann::json::object();
    for (const auto& [name, json_any] : modules) {
      if (!json_any.has_value()) continue;
      const auto& wrapped = std::any_cast<const nlohmann::json&>(json_any);
      // wrapped looks like {model: {name: {...}}} — merge into iface_json.
      for (auto it = wrapped.begin(); it != wrapped.end(); ++it) {
        if (!iface_json.contains(it.key()))
          iface_json[it.key()] = nlohmann::json::object();
        for (auto inner = it.value().begin(); inner != it.value().end(); ++inner) {
          iface_json[it.key()][inner.key()] = inner.value();
        }
      }
    }
    section[iface] = iface_json;
  }
  return section;
}
} // namespace

namespace champsim
{
void to_json(nlohmann::json& j, const champsim::phase_stats& stats)
{
  j = nlohmann::json{
    {"name", stats.name},
    {"traces", stats.trace_names},
    {"roi", compile_section(stats.roi_json)},
    {"sim", compile_section(stats.sim_json)}
  };
}
} // namespace champsim

void champsim::json_printer::print(std::vector<phase_stats>& stats)
{
  stream << nlohmann::json::array_t{std::begin(stats), std::end(stats)};
}
