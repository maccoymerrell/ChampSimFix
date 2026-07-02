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

#include "modules.h"

#include "cache.h"

// Static member definitions
bool champsim::modules::ModuleBuilder::global_dump_enabled_ = false;
std::string champsim::modules::ModuleBuilder::dump_log_;

// Initialize the process-wide globals builder with the system-wide defaults so
// modules constructed without an environment (e.g. unit tests) still get sane
// fall-through values for block_size / page_size / etc.
namespace {
struct globals_default_initializer {
  globals_default_initializer() {
    auto& g = champsim::modules::ModuleBuilder::globals();
    g.add_parameter("block_size",      64u);
    g.add_parameter("page_size",       4096u);
    g.add_parameter("log2_block_size", 6u);
    g.add_parameter("log2_page_size",  12u);
  }
};
static globals_default_initializer _globals_default_init;
} // anonymous namespace

namespace champsim::modules {

  namespace {
  std::vector<listener*>& listener_registry() {
    static std::vector<listener*> registry;
    return registry;
  }
  } // anonymous namespace

  void set_active_listeners(std::vector<listener*> active) { listener_registry() = std::move(active); }
  const std::vector<listener*>& active_listeners() { return listener_registry(); }

  void emit_begin_phase(bool is_warmup)
  {
    for (auto* l : listener_registry()) {
      l->begin_phase(is_warmup);
    }
  }

  void emit_progress(const source_consumer& consumer, uint64_t total_progress, uint64_t total_cycles)
  {
    for (auto* l : listener_registry()) {
      l->progress(consumer, total_progress, total_cycles);
    }
  }

  bool prefetcher::prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t prefetch_metadata) const
  {
    return intern_->prefetch_line(pf_addr, fill_this_level, prefetch_metadata);
  }
  // LCOV_EXCL_START Exclude deprecated function
  bool champsim::modules::prefetcher::prefetch_line(uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata) const
  {
    return prefetch_line(champsim::address{pf_addr}, fill_this_level, prefetch_metadata);
  }
  // LCOV_EXCL_STOP

}

// Interface registrations: map interface name strings to module_base specializations
static champsim::modules::channel_module::register_interface channel_iface_reg("channel");
static champsim::modules::cache_module::register_interface cache_iface_reg("cache");
static champsim::modules::memory_controller_module::register_interface memory_controller_iface_reg("memory_controller");
static champsim::modules::vmem_module::register_interface vmem_iface_reg("vmem");
static champsim::modules::page_table_walker_module::register_interface ptw_iface_reg("page_table_walker");
static champsim::modules::core_module::register_interface core_iface_reg("core");
static champsim::modules::environment_module::register_interface env_iface_reg("environment");

// Submodule interfaces. These are created by their parent modules (a core
// builds its workload sources, a cache its prefetchers), not by the
// environment — registering them names the interface for nested-instance
// enrollment and lets environment views cover them.
static champsim::modules::workload_source::register_interface workload_source_iface_reg("workload_source");
static champsim::modules::prefetcher::register_interface prefetcher_iface_reg("prefetcher");
static champsim::modules::replacement::register_interface replacement_iface_reg("replacement");
static champsim::modules::branch_predictor::register_interface branch_predictor_iface_reg("branch_predictor");
static champsim::modules::btb::register_interface btb_iface_reg("btb");
static champsim::modules::listener::register_interface listener_iface_reg("listener");

