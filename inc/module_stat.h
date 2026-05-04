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

/**
 * Opt-in interface for modules that want to publish their own
 * statistics to ChampSim's plaintext and JSON output.
 *
 * Inherit from this on the module *implementation* level (e.g.
 * ``CACHE``, ``O3_CPU``, ``MEMORY_CONTROLLER``) — not the interface
 * class — and override both hooks. The framework discovers
 * implementers via the interface registry and aggregates their output:
 *
 *   - ``print_stats`` returns plaintext lines, appended verbatim to the
 *     phase summary in declaration order.
 *   - ``json_stats`` populates a ``json_stat_builder`` whose contents
 *     are wrapped under ``[interface][model][name]`` in the merged JSON
 *     output, giving a uniform structure across all module types.
 *
 * Both hooks take a ``roi`` flag selecting between region-of-interest
 * and full-simulation stats. They are pure-virtual on purpose: opting
 * in is an explicit promise to provide both implementations, so users
 * cannot silently mistype an override.
 *
 * Authors who only care about one output format may leave the other as
 * a trivial empty body.
 */
struct module_stat
{
  virtual ~module_stat() = default;

  /**
   * Return the plaintext stat lines for this module.
   *
   * \param roi If true, use region-of-interest stats; otherwise sim-wide.
   */
  virtual std::vector<std::string> print_stats(bool roi) const = 0;

  /**
   * Populate ``builder`` with this module's JSON stats.
   *
   * Use the helper methods on ``json_stat_builder`` (``add`` / ``group``
   * / ``push_back``) so the resulting JSON matches ChampSim's expected
   * layout without manual container construction.
   *
   * \param roi If true, use region-of-interest stats; otherwise sim-wide.
   */
  virtual void json_stats(json_stat_builder& builder, bool roi) const = 0;
};

} // namespace champsim

#endif // MODULE_STAT_H
