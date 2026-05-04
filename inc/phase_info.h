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

#ifndef PHASE_INFO_H
#define PHASE_INFO_H

#include <any>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>


namespace champsim
{

struct phase_info {
  std::string name;
  bool is_warmup;
  uint64_t length;
  std::vector<std::size_t> trace_index;
  std::vector<std::string> trace_names;
};

struct phase_stats {
  std::string name;
  std::vector<std::string> trace_names;
  // Pre-collected by collect_phase_stats
  std::vector<std::string> sim_lines;   // all sim plaintext lines
  std::vector<std::string> roi_lines;   // all roi plaintext lines
  // interface -> [(module_name, json_any)] for JSON compilation
  std::map<std::string, std::vector<std::pair<std::string, std::any>>> sim_json;
  std::map<std::string, std::vector<std::pair<std::string, std::any>>> roi_json;
};

} // namespace champsim

#endif
