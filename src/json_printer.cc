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

#include <nlohmann/json.hpp>

#include "stats_printer.h"

namespace champsim
{
void to_json(nlohmann::json& j, const champsim::phase_stats& stats)
{
  j = nlohmann::json{{"name", stats.name}, {"traces", stats.trace_names}, {"roi", stats.stats}};
}
} // namespace champsim

void champsim::json_printer::print(std::vector<phase_stats>& stats) { stream << nlohmann::json::array_t{std::begin(stats), std::end(stats)}; }
