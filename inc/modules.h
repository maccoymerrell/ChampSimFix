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

#ifndef MODULES_H
#define MODULES_H

#include <map>
#include <deque>
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <cassert>
#include <any>
#include <optional>
#include <tuple>

#include "access_type.h"
#include "address.h"
#include "block.h"
#include "champsim.h"
#include "instruction.h"
#include "operable.h"
#include "packet.h"
#include "cache_stats.h"
#include "core_stats.h"
#include "dram_stats.h"
#include "bandwidth.h"
#include <any>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include "util/type_traits.h"
#include "phase_info.h"

//class CACHE;
//class O3_CPU;
namespace champsim::modules {

struct environment_module;

struct ModuleBuilder {
  private:
  std::map<std::string,std::any> parameters;
  std::map<std::string, std::vector<ModuleBuilder>> submodules_; // keyed by interface type
  std::string module_name = "";
  std::string model = "";
  std::any parent = nullptr;

  template<typename T>
  void set_parent(T* new_parent) { parent = new_parent; }
  template<typename B, typename C> friend struct module_base;

  static bool global_dump_enabled_;
  static std::string dump_log_;

  static std::string parent_dump_string(const std::any& parent) {
    if (!parent.has_value()) return "<unset>";
    if (parent.type() == typeid(std::nullptr_t)) return "nullptr";
    return parent.type().name();
  }

  static std::string builder_identity_string(const ModuleBuilder& builder) {
    return fmt::format("ModuleBuilder{{name={}, model={}, parent={}}}", builder.module_name, builder.model, parent_dump_string(builder.parent));
  }

  static std::string builder_identity_string(const ModuleBuilder& builder, const std::string& parent_builder_name) {
    auto parent_str = parent_dump_string(builder.parent);
    if (parent_str == "nullptr" && !parent_builder_name.empty()) {
      parent_str = parent_builder_name;
    }
    return fmt::format("ModuleBuilder{{name={}, model={}, parent={}}}", builder.module_name, builder.model, parent_str);
  }

  public:
  static void set_dump_enabled(bool enabled) { global_dump_enabled_ = enabled; }
  static bool is_dump_enabled() { return global_dump_enabled_; }

  bool get_dump() const { return is_dump_enabled(); }
  static const std::string& get_dump_log() { return dump_log_; }
  static void clear_dump_log() { dump_log_.clear(); }
  static void append_dump_log(const std::string& line) { dump_log_ += line; }

  template<typename T>
  std::string dump_line(const std::string& mod, const std::string& name, const T& val, const char* tag) const {
    using value_type = std::decay_t<T>;
    if constexpr (std::is_same_v<value_type, std::map<std::string, ModuleBuilder>>) {
      std::string summaries;
      bool first = true;
      for (auto& [model_name, nested_builder] : val) {
        if (!first) summaries += ", ";
        first = false;
        summaries += fmt::format("{}: {}", model_name, builder_identity_string(nested_builder, mod));
      }
      if (summaries.empty()) summaries = "<empty>";
      return fmt::format("  [{}] {} = {} ({})\n", mod, name, summaries, tag);
    }
    if constexpr (std::is_same_v<value_type, ModuleBuilder>) {
      return fmt::format("  [{}] {} = {} ({})\n", mod, name, builder_identity_string(val, mod), tag);
    }
    if constexpr (fmt::is_formattable<T>::value) {
      try { return fmt::format("  [{}] {} = {} ({})\n", mod, name, val, tag); }
      catch (...) {}
    } else if constexpr (std::is_pointer_v<T>) {
      try { return fmt::format("  [{}] {} = {} ({})\n", mod, name, std::vector<T>{val}, tag); }
      catch (...) {}
    }
    return fmt::format("  [{}] {} = <unprintable> ({})\n", mod, name, tag);
  }

  template<typename T>
  T get_parameter(std::string name, bool optional = false, T default_value = T{}) const {
    if(auto it = parameters.find(name); it != parameters.end()) {
      try {
        auto val = std::any_cast<T>(it->second);
        if (is_dump_enabled()) {
          auto line = dump_line(module_name, name, val, "set");
          dump_log_ += line;
          fmt::print("{}", line);
        }
        return val;
      }
      catch(const std::bad_any_cast&) {
        // For arithmetic types, try converting from whatever numeric type was stored
        T result{};
        if (champsim::numeric_any_cast(it->second, result)) {
          if (is_dump_enabled()) {
            auto line = dump_line(module_name, name, result, "set");
            dump_log_ += line;
            fmt::print("{}", line);
          }
          return result;
        }
        fmt::print("[MODULE] [{}] ERROR: Casting failed while retrieving parameter {}, is your parameter type correct?\n",module_name,name);
        exit(-1);
      }
    } else {
      if(optional) {
        if (is_dump_enabled()) {
          auto line = dump_line(module_name, name, default_value, "default");
          dump_log_ += line;
          fmt::print("{}", line);
        }
        return default_value;
      }
      fmt::print("[MODULE] [{}] ERROR: parameter {} not found\n",module_name,name);
      exit(-1);
    }
  }

  template<typename T>
  ModuleBuilder& add_parameter(std::string name, T value) {
    //if(parameters.find(name) != parameters.end()) {
    //  fmt::print("[MODULE] ERROR: duplicate parameter name used: {}\n",name);
    //  exit(-1);
    //} // should we allow this? it would allow for parameters to be set by defaults and then overridden, but it would also allow for accidental overwriting of parameters which could be bad
    parameters[name] = value;
    return *this;
  }

  std::string get_model() const { return model; }
  std::string get_name() const { return module_name; }

  template<typename T>
  T* get_parent() const { return std::any_cast<T*>(parent); }
  // Type for storing per-model builders (model_name -> builder)
  using module_builder_map_type = std::map<std::string, ModuleBuilder>;

  const std::map<std::string, std::any>& get_parameters() const { return parameters; }

  bool is_valid() const {return model != "" && module_name != "";}

  bool has_parameter(const std::string& name) const { return parameters.find(name) != parameters.end(); }
  // Internal check: parent has been set to a typed pointer (not the default std::nullptr_t)
  bool has_parent() const {return parent.has_value() && parent.type() != typeid(std::nullptr_t);}


  // Store a pre-built std::any directly (avoids double-wrapping when passing resolved references)
  ModuleBuilder& add_raw_parameter(std::string name, std::any value) {
    parameters[name] = std::move(value);
    return *this;
  }

  // ---- Submodule management (keyed by interface type) ----

  // Add a submodule builder under the given interface type.
  ModuleBuilder& add_submodule(const std::string& interface_type, ModuleBuilder sub_builder) {
    submodules_[interface_type].push_back(std::move(sub_builder));
    return *this;
  }

  // Clear all submodule builders for a given interface type (e.g. before replacing defaults).
  ModuleBuilder& clear_submodules(const std::string& interface_type) {
    submodules_.erase(interface_type);
    return *this;
  }

  // Get all submodule builders for a given interface type.
  // Returns an empty vector if no submodules of that type exist.
  const std::vector<ModuleBuilder>& get_submodules(const std::string& interface_type) const {
    static const std::vector<ModuleBuilder> empty;
    auto it = submodules_.find(interface_type);
    if (it == submodules_.end()) return empty;
    return it->second;
  }

  // Check whether submodules of the given interface type exist.
  bool has_submodules(const std::string& interface_type) const {
    auto it = submodules_.find(interface_type);
    return it != submodules_.end() && !it->second.empty();
  }

  // Get the full submodule map (read-only).
  const std::map<std::string, std::vector<ModuleBuilder>>& get_all_submodules() const {
    return submodules_;
  }

  ModuleBuilder() {}
  ModuleBuilder(std::string name_, std::string model_, ModuleBuilder defaults = ModuleBuilder{}) : module_name(name_), model(model_) {
    if(!defaults.parameters.empty()) {
      for(auto& [param_name, param_value] : defaults.parameters) {
        parameters[param_name] = param_value;
      }
    }
    if(!defaults.submodules_.empty()) {
      for(auto& [iface, subs] : defaults.submodules_) {
        submodules_[iface] = subs;
      }
    }
  }
};

// Forward declaration for mixin used in interface_info
struct source_consumer;

// Registry for module interfaces: maps interface name strings to factory functions.
// This allows runtime lookup of which module_base specialization to use for creation.
class interface_registry {
public:
  struct interface_info {
    std::function<std::any(ModuleBuilder, std::any)> create;
    std::function<std::any(const std::vector<std::any>&)> make_vector;
    // Returns operable* from a typed any, or nullptr if the interface is not operable
    std::function<champsim::operable*(const std::any&)> to_operable;
    // Returns source_consumer* from a typed any, or nullptr if the interface doesn't inherit source_consumer
    std::function<source_consumer*(const std::any&)> to_source_consumer;
    // Creates a typed null pointer wrapped in std::any
    std::function<std::any()> make_null_pointer;
    // Stats collection: call give_stats/give_stats_json on each module instance
    std::function<std::vector<std::string>(const std::vector<std::any>&, bool)> collect_text;
    std::function<std::vector<std::pair<std::string, std::any>>(const std::vector<std::any>&, bool)> collect_json;
  };

private:
  static std::map<std::string, interface_info>& registry() {
    static std::map<std::string, interface_info> r;
    return r;
  }

public:
  static void register_interface(const std::string& name, interface_info info) {
    if (registry().count(name)) {
      fmt::print("[MODULE] ERROR: duplicate interface name: {}\n", name);
      exit(-1);
    }
    registry()[name] = std::move(info);
  }

  static std::any create(const std::string& interface_name, ModuleBuilder builder, std::any parent) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) {
      fmt::print("[MODULE] ERROR: unknown interface: {}\n", interface_name);
      exit(-1);
    }
    return it->second.create(std::move(builder), std::move(parent));
  }

  static std::any make_vector(const std::string& interface_name, const std::vector<std::any>& elements) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) {
      fmt::print("[MODULE] ERROR: unknown interface for vector: {}\n", interface_name);
      exit(-1);
    }
    return it->second.make_vector(elements);
  }

  static bool has_interface(const std::string& name) {
    return registry().count(name) > 0;
  }

  // Get the to_operable converter for an interface, or nullptr if not operable
  static std::function<champsim::operable*(const std::any&)> get_to_operable(const std::string& interface_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) return nullptr;
    return it->second.to_operable;
  }

  // Get the to_source_consumer converter for an interface, or nullptr if not a source_consumer
  static std::function<source_consumer*(const std::any&)> get_to_source_consumer(const std::string& interface_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) return nullptr;
    return it->second.to_source_consumer;
  }

  // Create a typed null pointer for the given interface
  static std::any make_null_pointer(const std::string& interface_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) {
      fmt::print("[MODULE] ERROR: unknown interface for null pointer: {}\n", interface_name);
      exit(-1);
    }
    return it->second.make_null_pointer();
  }

  // Get all registered interface names
  static std::vector<std::string> get_interface_names() {
    std::vector<std::string> names;
    for (const auto& [name, _] : registry()) {
      names.push_back(name);
    }
    return names;
  }

  // Check whether an interface has stats
  static bool has_stats(const std::string& interface_name) {
    auto it = registry().find(interface_name);
    return it != registry().end() && it->second.collect_text;
  }

  // Collect plaintext stat lines from all instances of an interface
  static std::vector<std::string> collect_text(
      const std::string& iface, const std::vector<std::any>& instances, bool is_roi) {
    auto it = registry().find(iface);
    if (it == registry().end() || !it->second.collect_text) return {};
    return it->second.collect_text(instances, is_roi);
  }

  // Collect JSON stats from all instances: returns [(module_name, json_any)]
  static std::vector<std::pair<std::string, std::any>> collect_json(
      const std::string& iface, const std::vector<std::any>& instances, bool is_roi) {
    auto it = registry().find(iface);
    if (it == registry().end() || !it->second.collect_json) return {};
    return it->second.collect_json(instances, is_roi);
  }
};

//Module base, defining the base type B for the module and component type C that it is used by
template<typename B, typename C>
struct module_base {
    std::string NAME;
    C* intern_;
    using function_type = typename std::function<std::unique_ptr<B>(ModuleBuilder builder)>;

    private:
    static std::map<std::string,std::any>& module_map() {
        static std::map<std::string,std::any> map;
        return map;
    }
    static std::map<std::string,std::vector<std::unique_ptr<B>>>& instance_map() {
        static std::map<std::string,std::vector<std::unique_ptr<B>>> map;
        return map;
    }

    static void add_module(std::string name, std::function<std::unique_ptr<B>(ModuleBuilder builder)> module_constructor) {
        if(module_map().find(name) != module_map().end()) {
            fmt::print("[MODULE] ERROR: duplicate module name used: {}\n", name);
            exit(-1);
        }
        module_map()[name] = module_constructor;
    }

    public:
    //bind the internal pointer to its managing component
    //should probably remove this call
    void bind(C* bind_arg) {intern_ = bind_arg;};

    //create an instance of the module, which will be stored in this base-module-type's static list
    //parent is set on the builder before validation and construction
    static B* create_instance(ModuleBuilder builder, C* parent) {
        builder.set_parent(parent);
        if(!builder.is_valid() || !builder.has_parent()) {
            fmt::print("[MODULE] ERROR: invalid module builder used for module {}\n",builder.get_name());
            exit(-1);
        }
        if(module_map().find(builder.get_model()) == module_map().end()) {
            fmt::print("[MODULE] [{}] ERROR: specified model {} is not registered\n",builder.get_name(),builder.get_model());
            exit(-1);
        }
        try {
          B* instance_ptr = instance_map()[builder.get_name()].emplace_back(std::any_cast<std::function<std::unique_ptr<B>(ModuleBuilder builder)>>(module_map()[builder.get_model()])(builder)).get();
          //It seems sketchy for the module wrapper to be tracking these separately from the module itself, can we fix this?
          instance_ptr->NAME =  builder.get_name();
          instance_ptr->bind(builder.get_parent<C>());
          if (ModuleBuilder::is_dump_enabled()) {
            auto line = fmt::format("  [{}] created_module = {} (set)\n", builder.get_name(), builder.get_model());
            ModuleBuilder::append_dump_log(line);
            fmt::print("{}", line);
            // Print submodule info keyed by interface type
            for (const auto& [iface, subs] : builder.get_all_submodules()) {
              std::vector<std::string> names;
              for (const auto& sub : subs) {
                names.push_back(sub.get_name());
                auto sub_created = fmt::format("  [{}] submodule = {}.{} (set)\n", sub.get_name(), iface, sub.get_model());
                ModuleBuilder::append_dump_log(sub_created);
                fmt::print("{}", sub_created);
              }
              auto sub_line = fmt::format("  [{}] {}_modules = [{}] (set)\n", builder.get_name(), iface, fmt::join(names, ", "));
              ModuleBuilder::append_dump_log(sub_line);
              fmt::print("{}", sub_line);
            }
          }
          return(instance_ptr);
        }
        catch(const std::bad_any_cast& caught) {
          fmt::print("[MODULE] ERROR: Casting failed while constructing {}, are your registration and instance calls consistent?\n",builder.get_name());
          exit(-1);
        }
    }

    template<typename T>
    static T* get_instance(std::string name) {
        if(instance_map().find(name) == instance_map().end() || instance_map()[name].empty()) {
            fmt::print("[MODULE] ERROR: no instances found for module {}\n",name);
            exit(-1);
        }
        try {
          return std::any_cast<T*>(instance_map()[name].front().get());
        }
        catch(const std::bad_any_cast& caught) {
          fmt::print("[MODULE] ERROR: Casting failed while retrieving {}, is your instance type correct?\n",name);
          exit(-1);
        }
    }

    // Module stats: override to provide plaintext and JSON stats.
    virtual std::vector<std::string> give_stats(bool /*is_roi*/) const { return {}; }
    virtual std::any give_stats_json(bool /*is_roi*/) const { return {}; }

    virtual ~module_base() = default;

    //register a derived type D of base type B and constructor with arguments Params with the module system
    //this is necessary to be able to create instances
    template<typename D> 
    struct register_module {
      register_module(std::string model_name) {
          
          std::function<std::unique_ptr<B>(ModuleBuilder builder)> create_module([](ModuleBuilder builder){return std::unique_ptr<B>(new D(builder));});
          add_module(model_name,create_module);
      }
    };

    // Register this module_base specialization as a named interface in the interface_registry.
    // This allows the explicit environment to create modules by interface name string.
    struct register_interface {
      register_interface(std::string interface_name) {
        interface_registry::interface_info info;
        info.create = [](ModuleBuilder builder, std::any parent) -> std::any {
          return create_instance(std::move(builder), std::any_cast<C*>(parent));
        };
        info.make_vector = [](const std::vector<std::any>& elements) -> std::any {
          std::vector<B*> vec;
          for (auto& e : elements) {
            vec.push_back(std::any_cast<B*>(e));
          }
          return vec;
        };
        if constexpr (std::is_base_of_v<champsim::operable, B>) {
          info.to_operable = [](const std::any& a) -> champsim::operable* {
            return static_cast<champsim::operable*>(std::any_cast<B*>(a));
          };
        }
        if constexpr (std::is_base_of_v<source_consumer, B>) {
          info.to_source_consumer = [](const std::any& a) -> source_consumer* {
            return static_cast<source_consumer*>(std::any_cast<B*>(a));
          };
        }
        info.make_null_pointer = []() -> std::any {
          return static_cast<B*>(nullptr);
        };
        // Stats: call give_stats / give_stats_json on each module
        info.collect_text = [](const std::vector<std::any>& instances, bool is_roi) -> std::vector<std::string> {
          std::vector<std::string> lines;
          for (const auto& inst : instances) {
            B* ptr = std::any_cast<B*>(inst);
            if (ptr) {
              auto l = ptr->give_stats(is_roi);
              lines.insert(lines.end(), l.begin(), l.end());
            }
          }
          return lines;
        };
        info.collect_json = [](const std::vector<std::any>& instances, bool is_roi) -> std::vector<std::pair<std::string, std::any>> {
          std::vector<std::pair<std::string, std::any>> result;
          for (const auto& inst : instances) {
            B* ptr = std::any_cast<B*>(inst);
            if (ptr) {
              auto j = ptr->give_stats_json(is_roi);
              if (j.has_value()) {
                result.emplace_back(ptr->NAME, std::move(j));
              }
            }
          }
          return result;
        };
        interface_registry::register_interface(interface_name, std::move(info));
      }
    };

};

  // Mixin for any module that consumes workload sources.
  // Inherit from this to attach workload_source submodules.
  struct source_consumer {
    virtual ~source_consumer() = default;

    // True when all attached workload sources are exhausted.
    virtual bool source_eof() const { return true; }

    // Entity index for phase tracking (-1 = not tracked as a phase entity).
    virtual int entity_index() const { return -1; }

    // Progress metric for phase completion (e.g. instructions retired).
    // Return 0 to indicate no progress tracking (complete only on EOF).
    virtual uint64_t sim_progress() const { return 0; }

    // Called when this consumer's entity finishes a phase. Return empty to suppress.
    virtual std::string entity_finish_message(const std::string& phase_name) const { return {}; }

    // Called at the end of a phase for summary output. Return empty to suppress.
    virtual std::string phase_complete_message(const std::string& phase_name) const { return {}; }
  };

  struct core_module: public module_base<core_module,environment_module>, public operable, public source_consumer {
    //interface for core module
    virtual void push_instruction(ooo_model_instr instr) = 0;
    virtual std::size_t instructions_requested() = 0;
    virtual uint64_t sim_instr() const = 0;
    virtual uint8_t get_cpu_num() const = 0;
    virtual uint64_t sim_cycle() const = 0;

    core_module(champsim::chrono::picoseconds clock_period_) : operable(clock_period_) {}
    virtual ~core_module() = default;

    using stats_type = cpu_stats;
    virtual stats_type get_sim_stats() const = 0;
    virtual stats_type get_roi_stats() const = 0;

    static std::vector<std::string> format_plaintext(const stats_type& stats);
    static std::any make_json(const stats_type& stats);
    std::vector<std::string> give_stats(bool is_roi) const override;
    std::any give_stats_json(bool is_roi) const override;

    // source_consumer hooks: core_module provides CPU-specific messages
    int entity_index() const override;
    uint64_t sim_progress() const override;
    std::string entity_finish_message(const std::string& phase_name) const override;
    std::string phase_complete_message(const std::string& phase_name) const override;

    virtual void quiet(bool enable) = 0;
  };

  struct cache_module: public module_base<cache_module,environment_module>, public operable {
    //interface for cache module
    cache_module(champsim::chrono::picoseconds clock_period_) : operable(clock_period_) {}
    virtual ~cache_module() = default;

    using stats_type = cache_stats;
    virtual champsim::bandwidth::maximum_type get_max_tag_bandwidth() const = 0;
    virtual stats_type get_sim_stats() const = 0;
    virtual stats_type get_roi_stats() const = 0;

    static std::vector<std::string> format_plaintext(const stats_type& stats);
    static std::any make_json(const stats_type& stats);
    std::vector<std::string> give_stats(bool is_roi) const override;
    std::any give_stats_json(bool is_roi) const override;

    virtual bool is_virtual_prefetch() const = 0;
    virtual bool prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t prefetch_metadata) = 0;
    virtual void impl_update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                       champsim::address victim_addr, access_type type, bool hit) const = 0;
    virtual void impl_prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address target) const = 0;

    virtual long invalidate_entry(champsim::address inval_addr) = 0;
    virtual std::size_t get_mshr_occupancy() const = 0;
    virtual std::size_t get_mshr_size() const = 0;
    virtual double get_mshr_occupancy_ratio() const = 0;

    virtual std::vector<std::size_t> get_rq_occupancy() const = 0;
    virtual std::vector<std::size_t> get_rq_size() const = 0;
    virtual std::vector<double> get_rq_occupancy_ratio() const = 0;

    virtual std::vector<std::size_t> get_wq_occupancy() const = 0;
    virtual std::vector<std::size_t> get_wq_size() const = 0;
    virtual std::vector<double> get_wq_occupancy_ratio() const = 0;

    virtual std::vector<std::size_t> get_pq_occupancy() const = 0;
    virtual std::vector<std::size_t> get_pq_size() const = 0;
    virtual std::vector<double> get_pq_occupancy_ratio() const = 0;

    virtual std::size_t num_sets() const = 0;
    virtual std::size_t num_ways() const = 0;
    virtual champsim::data::bits get_offset_bits() const = 0;
  };

  struct memory_controller_module: public module_base<memory_controller_module,environment_module>, public operable {
    //interface for memory controller module
    memory_controller_module(champsim::chrono::picoseconds clock_period_) : operable(clock_period_) {}
    virtual ~memory_controller_module() = default;

    using stats_type = dram_stats;
    virtual std::size_t get_num_channels() const = 0;
    virtual stats_type get_sim_stats(std::size_t channel_no) const = 0;
    virtual stats_type get_roi_stats(std::size_t channel_no) const = 0;

    static std::vector<std::string> format_plaintext(const stats_type& stats);
    static std::any make_json(const stats_type& stats);
    std::vector<std::string> give_stats(bool is_roi) const override;
    std::any give_stats_json(bool is_roi) const override;

    virtual champsim::data::bytes size() const = 0;
  }; 

  struct page_table_walker_module: public module_base<page_table_walker_module,environment_module>, public operable {
    //interface for page table walker module
    page_table_walker_module(champsim::chrono::picoseconds clock_period_) : operable(clock_period_) {}
    virtual ~page_table_walker_module() = default;
  }; 

  struct channel_module: public module_base<channel_module,environment_module> {
    //interface for channel module
    using request_type = champsim::request;
    using response_type = champsim::response;
    using stats_type = champsim::cache_queue_stats;

    virtual bool add_rq(const request_type& packet) = 0;
    virtual bool add_wq(const request_type& packet) = 0;
    virtual bool add_pq(const request_type& packet) = 0;

    virtual std::size_t rq_occupancy() const = 0;
    virtual std::size_t wq_occupancy() const = 0;
    virtual std::size_t pq_occupancy() const = 0;

    virtual std::size_t rq_size() const = 0;
    virtual std::size_t wq_size() const = 0;
    virtual std::size_t pq_size() const = 0;

    // Queue accessors for upper-level iteration
    virtual std::deque<request_type>& get_rq() = 0;
    virtual std::deque<request_type>& get_wq() = 0;
    virtual std::deque<request_type>& get_pq() = 0;
    virtual std::deque<response_type>& get_returned() = 0;

    // Stats accessors
    virtual stats_type& get_sim_stats() = 0;
    virtual stats_type& get_roi_stats() = 0;

    virtual ~channel_module() = default;
  }; 

  struct vmem_module: public module_base<vmem_module,environment_module> {
    virtual ~vmem_module() = default;
    virtual std::size_t available_ppages() const = 0;
    virtual std::pair<champsim::page_number, champsim::chrono::clock::duration> va_to_pa(uint32_t cpu_num, champsim::page_number vaddr) = 0;
    virtual std::pair<champsim::address, champsim::chrono::clock::duration> get_pte_pa(uint32_t cpu_num, champsim::page_number vaddr, std::size_t level) = 0;
    virtual champsim::data::bits shamt(std::size_t level) const = 0;
    virtual uint64_t get_offset(champsim::address vaddr, std::size_t level) const = 0;
    virtual std::size_t get_pt_levels() const = 0;
  };

  struct prefetcher: public module_base<prefetcher,cache_module> {

      virtual ~prefetcher() = default;

      //prefetcher initialize
      virtual void prefetcher_initialize() {}

      //prefetcher cache operate
      virtual uint32_t prefetcher_cache_operate([[maybe_unused]] champsim::address addr, [[maybe_unused]] champsim::address ip, [[maybe_unused]] bool cache_hit, [[maybe_unused]] bool useful_prefetch,
                                                [[maybe_unused]] access_type type, [[maybe_unused]] uint32_t metadata_in) {
        return prefetcher_cache_operate(addr,ip,(uint8_t)cache_hit,useful_prefetch,type,metadata_in);
      }
      virtual uint32_t prefetcher_cache_operate([[maybe_unused]] champsim::address addr, [[maybe_unused]] champsim::address ip, [[maybe_unused]] uint8_t cache_hit, [[maybe_unused]] bool useful_prefetch,
                                                [[maybe_unused]] access_type type, [[maybe_unused]] uint32_t metadata_in) {
        return prefetcher_cache_operate(addr,ip,cache_hit,useful_prefetch,champsim::to_underlying<access_type>(type),metadata_in);
      }
      virtual uint32_t prefetcher_cache_operate([[maybe_unused]] champsim::address addr, [[maybe_unused]] champsim::address ip, [[maybe_unused]] bool cache_hit, [[maybe_unused]] bool useful_prefetch,
                                                [[maybe_unused]] std::underlying_type_t<access_type> type, [[maybe_unused]] uint32_t metadata_in) {
        return prefetcher_cache_operate(addr.to<uint64_t>(),ip.to<uint64_t>(),cache_hit,type,metadata_in);
      }
      virtual uint32_t prefetcher_cache_operate([[maybe_unused]] uint64_t addr, [[maybe_unused]] uint64_t ip, [[maybe_unused]] bool cache_hit,[[maybe_unused]] std::underlying_type_t<access_type> type, 
                                                [[maybe_unused]] uint32_t metadata_in) {return metadata_in;}

      //prefetcher cache fill
      virtual uint32_t prefetcher_cache_fill([[maybe_unused]] champsim::address addr, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] bool prefetch, 
                                                [[maybe_unused]] champsim::address evicted_addr, [[maybe_unused]] uint32_t metadata_in) {
        return prefetcher_cache_fill(addr,set,way,(uint8_t)prefetch,evicted_addr,metadata_in);
      }
      virtual uint32_t prefetcher_cache_fill([[maybe_unused]] champsim::address addr, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] uint8_t prefetch,
                                             [[maybe_unused]] champsim::address evicted_addr, [[maybe_unused]] uint32_t metadata_in) {
        return prefetcher_cache_fill(addr.to<uint64_t>(), set, way, prefetch, evicted_addr.to<uint64_t>(), metadata_in);
      }
      virtual uint32_t prefetcher_cache_fill([[maybe_unused]] uint64_t addr, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] bool prefetch, 
                                                [[maybe_unused]] uint64_t evicted_addr, [[maybe_unused]] uint32_t metadata_in) {return metadata_in;}

      //prefetcher cycle operate
      virtual void prefetcher_cycle_operate() {}

      //prefetcher final stats
      virtual void prefetcher_final_stats() {}

      //prefetcher branch operate
      virtual void prefetcher_branch_operate([[maybe_unused]] champsim::address ip, [[maybe_unused]] uint8_t branch_type, [[maybe_unused]] champsim::address branch_target) {
        prefetcher_branch_operate(ip.to<uint64_t>(), branch_type, branch_target.to<uint64_t>());
      }
      virtual void prefetcher_branch_operate([[maybe_unused]] uint64_t ip, [[maybe_unused]] uint8_t branch_type, [[maybe_unused]] uint64_t branch_target) {}

      bool prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t prefetch_metadata) const;
      bool prefetch_line(uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata) const;
  };


  struct replacement: public module_base<replacement,cache_module> {

      virtual ~replacement() = default;

      //initialize replacement
      virtual void initialize_replacement() {}

      //find victim
      virtual long find_victim([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] uint64_t instr_id, [[maybe_unused]] long set, [[maybe_unused]] const champsim::cache_block* current_set, [[maybe_unused]] champsim::address ip,
                                      [[maybe_unused]] champsim::address full_addr, [[maybe_unused]] access_type type) {
        return find_victim(triggering_cpu,instr_id,set,current_set,ip,full_addr,champsim::to_underlying<access_type>(type));
      }
      virtual long find_victim([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] uint64_t instr_id, [[maybe_unused]] long set, [[maybe_unused]] const champsim::cache_block* current_set, [[maybe_unused]] champsim::address ip,
                                      [[maybe_unused]] champsim::address full_addr, [[maybe_unused]] std::underlying_type_t<access_type> type) {
        return find_victim(triggering_cpu, instr_id, set, current_set, ip.to<uint64_t>(), full_addr.to<uint64_t>(), static_cast<access_type>(type));
      }
      virtual long find_victim([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] uint64_t instr_id, [[maybe_unused]] long set, [[maybe_unused]] const champsim::cache_block* current_set, [[maybe_unused]] uint64_t ip,
                                      [[maybe_unused]] uint64_t full_addr, [[maybe_unused]] access_type type) {
        return find_victim(triggering_cpu, instr_id, set, current_set, ip, full_addr, champsim::to_underlying<access_type>(type));
      }
      virtual long find_victim([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] uint64_t instr_id, [[maybe_unused]] long set, [[maybe_unused]] const champsim::cache_block* current_set, [[maybe_unused]] uint64_t ip,
                                      [[maybe_unused]] uint64_t full_addr, [[maybe_unused]] std::underlying_type_t<access_type> type) { return -1;};

      //update replacement state
      virtual void update_replacement_state([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] champsim::address full_addr,
                                                  [[maybe_unused]] champsim::address ip, [[maybe_unused]] champsim::address victim_addr, [[maybe_unused]] access_type type, [[maybe_unused]] bool hit) {
        champsim::address repl_victim = hit ? champsim::address{} : victim_addr;
        update_replacement_state(triggering_cpu,set,way,full_addr,ip,repl_victim,type,(uint8_t)hit);
      }
      virtual void update_replacement_state([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] champsim::address full_addr,
                                                  [[maybe_unused]] champsim::address ip, [[maybe_unused]] champsim::address victim_addr, [[maybe_unused]] access_type type, [[maybe_unused]] uint8_t hit) {
        update_replacement_state(triggering_cpu,set,way,full_addr,ip,victim_addr,champsim::to_underlying<access_type>(type),hit);
      }
      virtual void update_replacement_state([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] champsim::address full_addr,
                                                  [[maybe_unused]] champsim::address ip, [[maybe_unused]] champsim::address victim_addr, [[maybe_unused]] std::underlying_type_t<access_type> type, [[maybe_unused]] bool hit) {
        update_replacement_state(triggering_cpu,set,way,full_addr.to<uint64_t>(),ip.to<uint64_t>(),victim_addr.to<uint64_t>(),type,hit);
      }
      virtual void update_replacement_state([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] uint64_t full_addr,
                                                  [[maybe_unused]] uint64_t ip, [[maybe_unused]] uint64_t victim_addr, [[maybe_unused]] std::underlying_type_t<access_type> type, [[maybe_unused]] bool hit) {
        update_replacement_state(triggering_cpu,set,way,champsim::address{full_addr},champsim::address{ip},static_cast<access_type>(type),hit);
      }
      virtual void update_replacement_state([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] champsim::address full_addr,
                                                  [[maybe_unused]] champsim::address ip, [[maybe_unused]] access_type type, [[maybe_unused]] bool hit) {}


      //replacement cache fill
      virtual void replacement_cache_fill([[maybe_unused]] uint32_t triggering_cpu, [[maybe_unused]] long set, [[maybe_unused]] long way, [[maybe_unused]] champsim::address full_addr, 
                                                  [[maybe_unused]] champsim::address ip, [[maybe_unused]] champsim::address victim_addr, [[maybe_unused]] access_type type);

      //replacement final stats
      virtual void replacement_final_stats() {}

  };

  struct branch_predictor: public module_base<branch_predictor,core_module> {

    virtual ~branch_predictor() = default;

    //initialize branch predictor
    virtual void initialize_branch_predictor() {}

    //last branch result
    virtual void last_branch_result([[maybe_unused]] champsim::address ip, [[maybe_unused]] champsim::address target, [[maybe_unused]] bool taken, [[maybe_unused]] uint8_t branch_type) {
      last_branch_result(ip.to<uint64_t>(),target.to<uint64_t>(),taken,branch_type);
    }
    virtual void last_branch_result([[maybe_unused]] uint64_t ip, [[maybe_unused]] uint64_t target, [[maybe_unused]] bool taken, [[maybe_unused]] uint8_t branch_type) {}

    //predict branch
    virtual bool predict_branch([[maybe_unused]] champsim::address ip, [[maybe_unused]] champsim::address predicted_target, [[maybe_unused]] bool always_taken, [[maybe_unused]] uint8_t branch_type) {
      return predict_branch(ip.to<uint64_t>(),predicted_target.to<uint64_t>(),always_taken,branch_type);
    }
    virtual bool predict_branch([[maybe_unused]] uint64_t ip, [[maybe_unused]] uint64_t predicted_target, [[maybe_unused]] bool always_taken, [[maybe_unused]] uint8_t branch_type) {
      return predict_branch(champsim::address{ip});
    }
    virtual bool predict_branch([[maybe_unused]] champsim::address ip) {
      return predict_branch(ip.to<uint64_t>());
    }
    virtual bool predict_branch([[maybe_unused]] uint64_t ip) {return false;}

  };

  struct btb: public module_base<btb,core_module> {

    virtual ~btb() = default;

    //initialize btb
    virtual void initialize_btb() {}

    //update btb
    virtual void update_btb([[maybe_unused]] champsim::address ip, [[maybe_unused]] champsim::address predicted_target, [[maybe_unused]] bool taken, [[maybe_unused]] uint8_t branch_type) {
      update_btb(ip.to<uint64_t>(),predicted_target.to<uint64_t>(),taken,branch_type);
    }
    virtual void update_btb([[maybe_unused]] uint64_t ip, [[maybe_unused]] uint64_t predicted_target, [[maybe_unused]] bool taken, [[maybe_unused]] uint8_t branch_type) {}

    //btb prediction
    virtual std::pair<champsim::address, bool> btb_prediction([[maybe_unused]] champsim::address ip, [[maybe_unused]] uint8_t branch_type) {
      return std::pair<champsim::address, bool>{btb_prediction(ip.to<uint64_t>(),branch_type)};
    }
    virtual std::pair<uint64_t, bool> btb_prediction([[maybe_unused]] uint64_t ip, [[maybe_unused]] uint8_t branch_type) {
      std::pair<champsim::address, bool> result = btb_prediction(champsim::address{ip});
      return std::pair<uint64_t, bool>{result.first.to<uint64_t>(),result.second};
    }
    virtual std::pair<champsim::address, bool> btb_prediction([[maybe_unused]] champsim::address ip) {
      return std::pair<champsim::address, bool>{btb_prediction(ip.to<uint64_t>())};  
    }
    virtual std::pair<uint64_t, bool> btb_prediction([[maybe_unused]] uint64_t ip) {return std::pair<uint64_t, bool>{};}
  };

  // Workload source interface - provides instructions to a source_consumer.
  // Attach as a submodule of any module that inherits source_consumer
  // (e.g. core_module). The default implementation (TRACE_WORKLOAD_SOURCE)
  // wraps a tracereader. Override for execution-driven simulation or synthetic
  // workloads.
  struct workload_source : public module_base<workload_source, source_consumer> {
    virtual ~workload_source() = default;

    // Provide the next instruction. Called by the core when it has input queue space.
    virtual ooo_model_instr next_instruction() = 0;

    // True when the source has no more instructions to provide.
    [[nodiscard]] virtual bool eof() const = 0;

    // Execution-driven feedback hooks (no-ops by default).
    // Called by the core at the appropriate pipeline stage.
    virtual void retire_instruction([[maybe_unused]] const ooo_model_instr& instr) {}
    virtual void squash_instruction([[maybe_unused]] const ooo_model_instr& instr) {}
    virtual void branch_mispredict([[maybe_unused]] const ooo_model_instr& instr) {}
  };

  // Phase controller interface - manages phase completion and health monitoring
  struct phase_controller : public module_base<phase_controller, environment_module> {
    virtual ~phase_controller() = default;

    enum class status { CONTINUE, COMPLETE, ABORT };

    // Called at the start of a phase
    virtual void begin_phase(const std::string& name, bool is_warmup, uint64_t length) = 0;

    // Called each cycle after all operables have operated.
    // progress: number of operations that made progress this cycle.
    // Returns CONTINUE, COMPLETE, or ABORT.
    virtual status advance(long progress) = 0;

    // Get indices of entities that newly completed since last advance()
    virtual std::vector<unsigned> newly_completed_entities() const = 0;

    // Notify the controller that a trace reached EOF
    virtual void notify_trace_eof() = 0;

    // Called at end of phase for cleanup
    virtual void end_phase() = 0;

    // Returns the list of phases this controller wants to run.
    // If empty, the caller (main.cc / champsim::main) defines the phases.
    // Implement this to take full ownership of the run structure from config.
    virtual std::vector<champsim::phase_info> get_phases() const { return {}; }
  };

  // Environment module interface - the top-level module that owns/constructs the entire simulation

  struct environment_module : public module_base<environment_module, environment_module> {
    // Single generic view function: returns all modules of the given interface type.
    // Special interface_type "operable" returns all operable modules across all interfaces.
    virtual std::vector<std::any> view(const std::string& interface_type) const = 0;

    // Typed convenience wrapper: casts the any values to T* and returns reference_wrappers.
    template<typename T>
    std::vector<std::reference_wrapper<T>> typed_view(const std::string& interface_type) const {
      auto raw = view(interface_type);
      std::vector<std::reference_wrapper<T>> result;
      for (auto& a : raw) result.push_back(std::ref(*std::any_cast<T*>(a)));
      return result;
    }

    // Return the number of modules implementing the given interface.
    // Example: get_num("core") returns the number of cores.
    virtual std::size_t get_num(const std::string& interface_name) const { return view(interface_name).size(); }
    virtual unsigned get_block_size() const { return 64; }
    virtual unsigned get_page_size() const { return 4096; }
    virtual unsigned get_log2_block_size() const { return 6; }
    virtual unsigned get_log2_page_size() const { return 12; }
    virtual int get_deadlock_cycles() const { return 500; }

    // New: allow snooping of ModuleBuilder parameters by module name
    virtual const ModuleBuilder get_builder_params(const std::string& module_name) const = 0;

    virtual ~environment_module() = default;
  };

}

// Formatter for vector of pointers to types with a NAME member
template <typename T>
struct fmt::formatter<std::vector<T*>, std::enable_if_t<std::is_convertible_v<decltype(std::declval<T>().NAME), std::string>, char>>
    : fmt::formatter<std::string> {
  auto format(const std::vector<T*>& vec, fmt::format_context& ctx) const {
    std::string result = "[";
    for (std::size_t i = 0; i < vec.size(); i++) {
      if (i > 0) result += ", ";
      result += vec[i] ? ("@" + vec[i]->NAME) : "(null)";
    }
    result += "]";
    return fmt::formatter<std::string>::format(result, ctx);
  }
};


#endif
