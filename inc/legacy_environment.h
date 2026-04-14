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

#ifndef LEGACY_ENVIRONMENT_H
#define LEGACY_ENVIRONMENT_H

#include <string>
#include <vector>
#include <map>
#include <any>

#include "modules.h"

namespace champsim {

class legacy_environment final : public champsim::modules::environment_module {
  // All modules indexed by interface type
  std::map<std::string, std::vector<std::any>> modules_by_type_;

  // Ordered list of (name, interface_type) pairs preserving construction order
  std::vector<std::pair<std::string, std::string>> module_order_;

  size_t num_cpus_ = 0;
  unsigned block_size_ = 64;
  unsigned page_size_ = 4096;
  int deadlock_cycles_ = 500;

  // Map from module name to ModuleBuilder used for construction
  std::map<std::string, champsim::modules::ModuleBuilder> builder_params_;

public:
  explicit legacy_environment(champsim::modules::ModuleBuilder builder);

  std::vector<std::any> view(const std::string& interface_type) const override;

  size_t get_num(const std::string& interface_name) const override {
    auto it = modules_by_type_.find(interface_name);
    if (it != modules_by_type_.end()) return it->second.size();
    return 0;
  }
  unsigned get_block_size() const override { return block_size_; }
  unsigned get_page_size() const override { return page_size_; }
  unsigned get_log2_block_size() const override { return champsim::lg2(block_size_); }
  unsigned get_log2_page_size() const override { return champsim::lg2(page_size_); }
  int get_deadlock_cycles() const override { return deadlock_cycles_; }

  // Expose builder params for test snooping
  const champsim::modules::ModuleBuilder get_builder_params(const std::string& module_name) const override {
    auto it = builder_params_.find(module_name);
    if (it != builder_params_.end()) return it->second;
    return champsim::modules::ModuleBuilder();
  }
};

} // namespace champsim

#endif // LEGACY_ENVIRONMENT_H
