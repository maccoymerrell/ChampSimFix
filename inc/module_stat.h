/*
 *    Copyright 2026 The ChampSim Contributors
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

#ifndef MODULE_STAT_H
#define MODULE_STAT_H

#include <string>
#include <vector>

namespace champsim
{

class json_stat_builder;

// Opt-in interface for modules that publish plaintext + JSON stats. Inherit at
// the implementation level (CACHE, O3_CPU, ...), not the interface; both hooks
// are pure-virtual so an override can't be silently mistyped (empty body is OK).
struct module_stat {
  virtual ~module_stat() = default;

  // Plaintext stat lines; roi selects region-of-interest vs sim-wide.
  virtual std::vector<std::string> print_stats(bool roi) const = 0;

  // Populate builder with JSON stats; roi selects region-of-interest vs sim-wide.
  virtual void json_stats(json_stat_builder& builder, bool roi) const = 0;
};

} // namespace champsim

#endif // MODULE_STAT_H
