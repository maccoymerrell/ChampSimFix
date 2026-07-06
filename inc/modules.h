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
#include <utility>

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
#include "util/ring_buffer.h"
#include "util/type_traits.h"
#include "phase_info.h"
#include "module_phase.h"
#include "module_stat.h"
#include "json_stat_builder.h"

//class CACHE;
//class O3_CPU;
/**
 * The ChampSim runtime module system: module interfaces plus the infrastructure
 * to construct, register, and instantiate them at runtime. Authors use the
 * user-facing interfaces (prefetcher, replacement, branch_predictor, btb),
 * ModuleBuilder, and register_module.
 */
namespace champsim::modules {

struct environment_module;
struct source_consumer;
struct stream_source;
struct workload_source;

/**
 * Provides configuration parameters and parent access to modules during construction.
 *
 * Every module constructor receives a ModuleBuilder. Use it to:
 * - Retrieve parameters from the JSON config via get_parameter().
 * - Access the parent module (e.g. the cache a prefetcher is attached to) via get_parent().
 * - Query the module's name and model via get_name() and get_model().
 */
struct ModuleBuilder {
  public:
  /**
   * One lexical scope frame: an immutable map of named values visible to a
   * module and everything beneath it. Shared+immutable keeps copies cheap.
   */
  using scope_frame_type = std::shared_ptr<const std::map<std::string, std::any>>;

  private:
  std::map<std::string,std::any> parameters;
  // Enclosing lexical scopes, innermost first. Parameter lookup resolves
  // local parameters, then these frames in order, then the process-wide
  // globals() (the outermost scope).
  std::vector<scope_frame_type> scope_frames_;
  std::map<std::string, std::vector<ModuleBuilder>> submodules_; // keyed by interface type
  std::string module_name = "";
  std::string model = "";
  std::any parent = nullptr;
  // Environment to notify on instance creation (nested-instance enrollment).
  // Set only on submodule builders; top-level children the environment registers.
  environment_module* owner_ = nullptr;

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
    } else if constexpr (std::is_pointer_v<T> && fmt::is_formattable<std::vector<value_type>>::value) {
      try { return fmt::format("  [{}] {} = {} ({})\n", mod, name, std::vector<value_type>{val}, tag); }
      catch (...) {}
    }
    return fmt::format("  [{}] {} = <unprintable> ({})\n", mod, name, tag);
  }

  /**
   * Retrieve a configuration parameter by name.
   *
   * Parameters are set in the JSON configuration file. For example, if the config
   * contains ``{"model": "my_pref", "degree": 4}``, then
   * ``get_parameter<int>("degree")`` returns 4.
   *
   * \tparam T The expected type of the parameter.
   * \param name The parameter name as it appears in the JSON config.
   * \param optional If true, returns default_value when the parameter is absent.
   *                 If false (the default), the simulator exits with an error.
   * \param default_value The value to return when the parameter is absent and optional is true.
   * \return The parameter value, or default_value if absent and optional.
   */
  /**
   * Process-wide fall-through builder: a get_parameter miss on the local
   * builder consults this before failing. Holds system-wide settings
   * (block_size, page_size, num_consumers, ...) every module can read.
   */
  static ModuleBuilder& globals() {
    static ModuleBuilder g{"<globals>", "<globals>"};
    return g;
  }

  template<typename T>
  T get_parameter(std::string name, bool optional = false, T default_value = T{}) const {
    // Globals lookups (direct or fall-through) are suppressed in dump output
    // to avoid per-module spam of system params.
    const bool suppress_dump = (this == &globals());
    auto found = lookup_parameter<T>(name);
    if (found.value.has_value()) {
      if (is_dump_enabled() && !suppress_dump && !found.from_globals) {
        auto line = dump_line(module_name, name, *found.value, "set");
        dump_log_ += line;
        fmt::print("{}", line);
      }
      return *found.value;
    }
    if (optional) {
      if (is_dump_enabled() && !suppress_dump) {
        auto line = dump_line(module_name, name, default_value, "default");
        dump_log_ += line;
        fmt::print("{}", line);
      }
      return default_value;
    }
    fmt::print("[MODULE] [{}] ERROR: parameter {} not found\n", module_name, name);
    exit(-1);
  }

private:
  template<typename T>
  struct lookup_result { std::optional<T> value; bool from_globals = false; };

  // Find a parameter by name on this builder, with fall-through to globals().
  template<typename T>
  lookup_result<T> lookup_parameter(const std::string& name) const {
    auto try_cast = [&](const std::any& a) -> std::optional<T> {
      try { return std::optional<T>{std::any_cast<T>(a)}; }
      catch (const std::bad_any_cast&) {
        T result{};
        if (champsim::numeric_any_cast(a, result)) return std::optional<T>{std::move(result)};
        fmt::print("[MODULE] [{}] ERROR: Casting failed while retrieving parameter {}, is your parameter type correct?\n", module_name, name);
        exit(-1);
      }
    };
    if (auto it = parameters.find(name); it != parameters.end())
      return {try_cast(it->second), false};
    // Enclosing scopes, innermost first (treated like globals for dump
    // suppression: scope hits are ambient values, not per-module settings)
    for (const auto& frame : scope_frames_) {
      if (auto it = frame->find(name); it != frame->end()) return {try_cast(it->second), true};
    }
    if (this != &globals()) {
      const auto& g = globals().parameters;
      if (auto it = g.find(name); it != g.end()) return {try_cast(it->second), true};
    }
    return {std::nullopt, false};
  }
public:

  template<typename T>
  ModuleBuilder& add_parameter(std::string name, T value) {
    //if(parameters.find(name) != parameters.end()) {
    //  fmt::print("[MODULE] ERROR: duplicate parameter name used: {}\n",name);
    //  exit(-1);
    //} // should we allow this? it would allow for parameters to be set by defaults and then overridden, but it would also allow for accidental overwriting of parameters which could be bad
    parameters[name] = value;
    return *this;
  }

  /** Return the model name of this module (e.g. ``"ip_stride"``). */
  std::string get_model() const { return model; }
  /** Return the instance name of this module (e.g. ``"cpu0_L2C.ip_stride"``). */
  std::string get_name() const { return module_name; }

  /**
   * Return a typed pointer to this module's parent.
   *
   * For prefetchers and replacement policies the parent is a cache_module.
   * For branch predictors and BTBs the parent is a core_module.
   *
   * \tparam T The parent module type (e.g. ``cache_module`` or ``core_module``).
   * \return A pointer to the parent module.
   */
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

  /** Prepend an innermost enclosing scope frame. */
  ModuleBuilder& push_scope_frame(scope_frame_type frame) {
    scope_frames_.insert(std::begin(scope_frames_), std::move(frame));
    return *this;
  }
  /** Replace the enclosing-scope chain (used when deriving child builders). */
  ModuleBuilder& inherit_scope(std::vector<scope_frame_type> frames) {
    scope_frames_ = std::move(frames);
    return *this;
  }
  const std::vector<scope_frame_type>& scope_frames() const { return scope_frames_; }

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
  //
  // \param interface_type The interface name (e.g. ``"prefetcher"``).
  // \param optional If true, returns an empty vector when no submodules of
  //                 that interface are present.  If false (the default), the
  //                 simulator exits with an error.  Mirrors the ``optional``
  //                 semantic of ``get_parameter``.
  const std::vector<ModuleBuilder>& get_submodules(const std::string& interface_type, bool optional = false) const {
    static const std::vector<ModuleBuilder> empty;
    auto it = submodules_.find(interface_type);
    if (it != submodules_.end() && !it->second.empty()) return it->second;
    if (optional) return empty;
    fmt::print("[MODULE] [{}] ERROR: required submodules of interface {} not found\n", module_name, interface_type);
    exit(-1);
  }

  // Check whether submodules of the given interface type exist.
  bool has_submodules(const std::string& interface_type) const {
    auto it = submodules_.find(interface_type);
    return it != submodules_.end() && !it->second.empty();
  }

  // ---- Nested-instance enrollment ----

  // Recursively mark every submodule builder (not this one) as owned by env.
  // Instances from owned builders self-announce via enroll_nested_instance,
  // joining the environment's views (and operable ticking).
  ModuleBuilder& set_owner_of_submodules(environment_module* env) {
    for (auto& [iface, subs] : submodules_) {
      for (auto& sub : subs) {
        sub.owner_ = env;
        sub.set_owner_of_submodules(env);
      }
    }
    return *this;
  }

  environment_module* get_owner() const { return owner_; }

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
struct stream_source;

// Registry for module interfaces: maps interface name strings to factory functions.
// This allows runtime lookup of which module_base specialization to use for creation.
class interface_registry {
public:
  // Per-instance metadata: model name (e.g. "DEFAULT_CACHE") and instance NAME (e.g. "cpu0_L1D").
  struct instance_id { std::string model; std::string name; };

  struct interface_info {
    std::function<std::any(ModuleBuilder, std::any)> create;
    std::function<std::any(const std::vector<std::any>&)> make_vector;
    // Returns operable* from a typed any via a per-instance dynamic_cast, or
    // nullptr if that specific instance does not inherit operable.
    std::function<champsim::operable*(const std::any&)> to_operable;
    // Returns source_consumer* from a typed any, or nullptr if the interface doesn't inherit source_consumer
    std::function<source_consumer*(const std::any&)> to_source_consumer;
    // Returns module_phase* from a typed any, or nullptr if the impl doesn't inherit module_phase
    std::function<champsim::module_phase*(const std::any&)> to_module_phase;
    // Returns module_stat* from a typed any, or nullptr if the impl doesn't inherit module_stat
    std::function<champsim::module_stat*(const std::any&)> to_module_stat;
    // Returns the registered model name and the instance NAME for an instance any.
    std::function<instance_id(const std::any&)> identify;
    // Whether the named model's impl is a source_consumer / stream_source.
    // Recorded at register_module time (is_base_of) so environments can count
    // consumers/sources from a config before constructing any module.
    std::function<bool(const std::string&)> model_is_consumer;
    std::function<bool(const std::string&)> model_is_source;
    // Per-instance dynamic_cast to the stream_source mixin (matching
    // to_source_consumer): any model of any interface may hold a stream.
    std::function<stream_source*(const std::any&)> to_stream_source;
    // Creates a typed null pointer wrapped in std::any
    std::function<std::any()> make_null_pointer;
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

  static bool model_is_source(const std::string& interface_name, const std::string& model_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end() || !it->second.model_is_source) {
      return false;
    }
    return it->second.model_is_source(model_name);
  }

  static bool model_is_consumer(const std::string& interface_name, const std::string& model_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end() || !it->second.model_is_consumer) {
      return false;
    }
    return it->second.model_is_consumer(model_name);
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
  static std::function<stream_source*(const std::any&)> get_to_stream_source(const std::string& interface_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) {
      return nullptr;
    }
    return it->second.to_stream_source;
  }

  static std::function<source_consumer*(const std::any&)> get_to_source_consumer(const std::string& interface_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) return nullptr;
    return it->second.to_source_consumer;
  }

  // Get the to_module_phase / to_module_stat converters for an interface
  static std::function<champsim::module_phase*(const std::any&)> get_to_module_phase(const std::string& interface_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) return nullptr;
    return it->second.to_module_phase;
  }
  static std::function<champsim::module_stat*(const std::any&)> get_to_module_stat(const std::string& interface_name) {
    auto it = registry().find(interface_name);
    if (it == registry().end()) return nullptr;
    return it->second.to_module_stat;
  }

  // Read instance metadata (model + name) for an instance any belonging to a known interface.
  static instance_id identify(const std::string& interface_name, const std::any& instance) {
    auto it = registry().find(interface_name);
    if (it == registry().end() || !it->second.identify) return {};
    return it->second.identify(instance);
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
};

/**
 * Base class for all module interfaces.
 *
 * Provides the static module registry, instance creation, and the
 * register_module helper. Module interfaces (prefetcher, replacement, etc.)
 * inherit from a specialization of this template.
 *
 * \tparam B The interface base type (e.g. ``prefetcher``).
 * \tparam C The parent component type (e.g. ``cache_module``).
 */
template<typename B, typename C>
struct module_base {
    std::string NAME;
    std::string MODEL;
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
    // The interface name this specialization was registered under (set by
    // register_interface); used to label nested-instance enrollments.
    static std::string& registered_interface_name() {
        static std::string name;
        return name;
    }

    // A registered model's factory plus traits that must be queryable before
    // any instance exists (pre-construction counts can't dynamic_cast, so
    // inheritance is recorded here where the concrete type is known statically).
    struct model_record {
      std::function<std::unique_ptr<B>(ModuleBuilder builder)> create;
      bool is_consumer = false;
      bool is_source = false;
    };

    static void add_module(std::string name, model_record record) {
        if(module_map().find(name) != module_map().end()) {
            fmt::print("[MODULE] ERROR: duplicate module name used: {}\n", name);
            exit(-1);
        }
        module_map()[name] = std::move(record);
    }

    public:
    //no-op bind; overridden in types that need a parent pointer (e.g. prefetcher)
    void bind(C*) {}

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
          B* instance_ptr = instance_map()[builder.get_name()].emplace_back(std::any_cast<model_record&>(module_map()[builder.get_model()]).create(builder)).get();
          //It seems sketchy for the module wrapper to be tracking these separately from the module itself, can we fix this?
          instance_ptr->NAME =  builder.get_name();
          instance_ptr->MODEL = builder.get_model();
          if (auto* as_consumer = dynamic_cast<source_consumer*>(instance_ptr); as_consumer != nullptr) {
            as_consumer->set_identity_name(builder.get_name());
          }
          if (auto* as_source = dynamic_cast<stream_source*>(instance_ptr); as_source != nullptr) {
            as_source->set_identity_name(builder.get_name());
          }
          instance_ptr->bind(builder.get_parent<C>());
          // Nested-instance enrollment: submodule builders carry the owning
          // environment; announce the new instance so environment views (and
          // operable auto-discovery) cover modules created inside parents.
          if (auto* owner = builder.get_owner(); owner != nullptr) {
            owner->enroll_nested_instance(registered_interface_name(), builder.get_name(), std::any{instance_ptr});
          }
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

    // True if a model with this name has been registered for this interface.
    static bool has_model(const std::string& model_name) { return module_map().count(model_name) > 0; }

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

    virtual ~module_base() = default;

    /**
     * Register a concrete module implementation with the module system.
     *
     * Place a static instance of this struct in your ``.cc`` file to make
     * your module available by name::
     *
     *     champsim::modules::prefetcher::register_module<my_pref> reg("my_pref");
     *
     * \tparam D The concrete module class to register.
     */
    template<typename D>
    struct register_module {
      /**
       * Register the module under the given model name.
       *
       * \param model_name The name used in JSON config to select this module.
       */
      register_module(std::string model_name) {
          add_module(model_name, model_record{[](ModuleBuilder builder){return std::unique_ptr<B>(new D(builder));},
                                              std::is_base_of_v<source_consumer, D>, std::is_base_of_v<stream_source, D>});
      }
    };

    // Register this module_base specialization as a named interface in the interface_registry.
    // This allows the explicit environment to create modules by interface name string.
    struct register_interface {
      register_interface(std::string interface_name) {
        registered_interface_name() = interface_name;
        interface_registry::interface_info info;
        info.create = [interface_name](ModuleBuilder builder, std::any parent) -> std::any {
          auto module_name = builder.get_name();
          try {
            return create_instance(std::move(builder), std::any_cast<C*>(parent));
          } catch (const std::bad_any_cast&) {
            fmt::print("[MODULE] [{}] ERROR: interface {} cannot be created here: its parent type does not match "
                       "(submodule interfaces must be declared as children of their parent module)\n",
                       module_name, interface_name);
            exit(-1);
          }
        };
        info.make_vector = [](const std::vector<std::any>& elements) -> std::any {
          std::vector<B*> vec;
          for (auto& e : elements) {
            vec.push_back(std::any_cast<B*>(e));
          }
          return vec;
        };
        // Per-instance dynamic_cast: works even when only the impl (not the
        // interface) inherits operable, so one model can be operable while
        // others of the same interface (e.g. the default channel) are not.
        info.to_operable = [](const std::any& a) -> champsim::operable* {
          return dynamic_cast<champsim::operable*>(std::any_cast<B*>(a));
        };
        // Per-instance dynamic_cast (matching to_operable / to_module_phase): a
        // specific model can be a source_consumer even when the interface is not
        // (e.g. a channel model that drives phase completion).
        info.to_source_consumer = [](const std::any& a) -> source_consumer* {
          return dynamic_cast<source_consumer*>(std::any_cast<B*>(a));
        };
        // Per-instance dynamic_cast: works even when only the implementation
        // (not the interface) inherits module_phase / module_stat.
        info.to_module_phase = [](const std::any& a) -> champsim::module_phase* {
          return dynamic_cast<champsim::module_phase*>(std::any_cast<B*>(a));
        };
        info.to_module_stat = [](const std::any& a) -> champsim::module_stat* {
          return dynamic_cast<champsim::module_stat*>(std::any_cast<B*>(a));
        };
        info.identify = [](const std::any& a) -> interface_registry::instance_id {
          B* ptr = std::any_cast<B*>(a);
          if (!ptr) return {};
          return interface_registry::instance_id{ptr->MODEL, ptr->NAME};
        };
        info.make_null_pointer = []() -> std::any {
          return static_cast<B*>(nullptr);
        };
        info.model_is_consumer = [](const std::string& model_name) -> bool {
          auto it = module_map().find(model_name);
          return it != module_map().end() && std::any_cast<const model_record&>(it->second).is_consumer;
        };
        info.model_is_source = [](const std::string& model_name) -> bool {
          auto it = module_map().find(model_name);
          return it != module_map().end() && std::any_cast<const model_record&>(it->second).is_source;
        };
        info.to_stream_source = [](const std::any& a) -> stream_source* {
          return dynamic_cast<stream_source*>(std::any_cast<B*>(a));
        };
        interface_registry::register_interface(interface_name, std::move(info));
      }
    };

};

  // Mixin for any module that consumes workload sources.
  // Inherit from this to attach workload_source submodules.
  struct source_consumer {
    virtual ~source_consumer() = default;

    // Health judged by the consumer itself: it knows its own expected progress
    // rate, so livelock policy lives here, not in the aggregating phase controller.
    enum class source_health { healthy, warning, critical, stalled };

    // True when all attached workload sources are exhausted.
    virtual bool source_eof() const { return true; }

    // Consumer id: hardware-context identity, assigned at startup by enumeration
    // in config order, never written in a config. Unique and dense in
    // [0, num_consumers); keys per-consumer resources and defaults attached
    // sources' streams (see origin.h). Standalone instances default to 0.
    int consumer_id() const { return consumer_id_; }
    // The instance name this consumer was configured under (set by the
    // module factory). Use champsim::identities() for id <-> name lookups.
    const std::string& consumer_name() const { return identity_name_; }
    void set_identity_name(std::string name) { identity_name_ = std::move(name); }
    void set_consumer_id(int id)
    {
      if (!consumer_id_pinned_) {
        consumer_id_ = id;
      }
    }
    // Consumers mirroring another's identity (e.g. a shadow/replay channel) may
    // pin their id; pinned consumers are skipped by enumeration and own no slot.
    void pin_consumer_id(int id)
    {
      consumer_id_ = id;
      consumer_id_pinned_ = true;
    }
    bool consumer_id_pinned() const { return consumer_id_pinned_; }

  private:
    int consumer_id_ = 0;
    bool consumer_id_pinned_ = false;
    std::string identity_name_{};

  public:

    // Progress metric for phase completion, in tokens (e.g. instructions
    // retired). Return 0 to indicate no progress tracking (complete only on EOF).
    virtual uint64_t sim_progress() const { return 0; }

    // Periodic self-check, driven by the phase controller every health
    // period. elapsed is the number of controller cycles since the last
    // check (or reset_health). Return stalled to abort the simulation.
    virtual source_health check_health(uint64_t /*elapsed*/) { return source_health::healthy; }

    // Re-baseline health tracking; called by the controller at phase start.
    virtual void reset_health() {}

    // True when this consumer knows more work is scheduled to arrive later
    // (e.g. a paced source waiting out a scheduled gap). While any consumer
    // reports pending work, zero global progress is not a deadlock.
    virtual bool has_pending_work() const { return false; }

    // Called when this consumer's source finishes a phase. Return empty to suppress.
    virtual std::string source_finish_message(const std::string& /*phase_name*/) const { return {}; }

    // Called at the end of a phase for summary output. Return empty to suppress.
    virtual std::string phase_complete_message(const std::string& /*phase_name*/) const { return {}; }

    // Format a periodic progress report (driven by a heartbeat listener that
    // supplies the numbers); the consumer owns the wording (its token unit).
    // Return empty to suppress.
    virtual std::string progress_message(uint64_t total_progress, uint64_t total_cycles, double interval_rate, double cumulative_rate) const
    {
      return fmt::format("Heartbeat source {} tokens: {} cycles: {} rate: {:.4} cumulative rate: {:.4}", consumer_id(), total_progress, total_cycles,
                         interval_rate, cumulative_rate);
    }
  };

  /**
   * Interface for CPU core modules.
   *
   * The default implementation is O3_CPU. Branch predictors and BTBs are
   * attached to a core_module.
   */
  struct core_module: public module_base<core_module,environment_module>, public operable, public source_consumer {
    /** \cond INTERNAL */
    virtual void push_instruction(ooo_model_instr instr) = 0;
    virtual std::size_t instructions_requested() = 0;
    core_module(champsim::chrono::picoseconds clock_period_) : operable(clock_period_) {}
    virtual ~core_module() = default;
    /** \endcond */

    /** Return the number of instructions simulated so far. */
    virtual uint64_t sim_instr() const = 0;
    /** Return the number of cycles simulated so far. */
    virtual uint64_t sim_cycle() const = 0;

    /** The stats type returned by get_sim_stats() and get_roi_stats(). */
    using stats_type = cpu_stats;
    /** Return simulation-wide statistics for this core. */
    virtual stats_type get_sim_stats() const = 0;
    /** Return region-of-interest statistics for this core. */
    virtual stats_type get_roi_stats() const = 0;

    static std::vector<std::string> format_plaintext(const stats_type& stats);
    static void format_json(const stats_type& stats, champsim::json_stat_builder& b);

    // source_consumer hooks: core_module provides CPU-specific messages.
    uint64_t sim_progress() const override;
    std::string source_finish_message(const std::string& phase_name) const override;
    std::string phase_complete_message(const std::string& phase_name) const override;
    std::string progress_message(uint64_t total_progress, uint64_t total_cycles, double interval_rate, double cumulative_rate) const override;

    // Health policy for instruction consumers: IPC over the check window vs
    // retirement-rate floors (the former phase-controller livelock thresholds).
    source_health check_health(uint64_t elapsed) override;
    void reset_health() override;

  private:
    uint64_t health_last_progress_ = 0;
  };

  /**
   * Interface for cache modules.
   *
   * The default implementation is CACHE. Prefetchers and replacement policies
   * are attached to a cache_module. Module authors can query cache geometry
   * and queue occupancy through this interface.
   */
  struct cache_module: public module_base<cache_module,environment_module>, public operable {
    /** \cond INTERNAL */
    cache_module(champsim::chrono::picoseconds clock_period_) : operable(clock_period_) {}
    virtual ~cache_module() = default;
    virtual champsim::bandwidth::maximum_type get_max_tag_bandwidth() const = 0;
    virtual void impl_update_replacement_state(champsim::origin origin, long set, long way, champsim::address full_addr, champsim::address ip,
                                       champsim::address victim_addr, access_type type, bool hit) const = 0;
    virtual void impl_prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address target) const = 0;
    /** \endcond */

    /** The stats type returned by get_sim_stats() and get_roi_stats(). */
    using stats_type = cache_stats;
    /** Return simulation-wide statistics for this cache. */
    virtual stats_type get_sim_stats() const = 0;
    /** Return region-of-interest statistics for this cache. */
    virtual stats_type get_roi_stats() const = 0;

    static std::vector<std::string> format_plaintext(const stats_type& stats);
    static void format_json(const stats_type& stats, champsim::json_stat_builder& b);

    /** Return true if this cache uses virtual addresses for prefetching. */
    virtual bool is_virtual_prefetch() const = 0;
    /**
     * Issue a prefetch into this cache.
     *
     * \param pf_addr The address to prefetch.
     * \param fill_this_level If true, fill this cache level; otherwise fill the next level.
     * \param prefetch_metadata Metadata to associate with the prefetch.
     * \return True if the prefetch was successfully enqueued.
     */
    virtual bool prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t prefetch_metadata) = 0;

    /** Invalidate the cache line at the given address. Returns the way index, or -1. */
    virtual long invalidate_entry(champsim::address inval_addr) = 0;
    /** Return the current number of occupied MSHR entries. */
    virtual std::size_t get_mshr_occupancy() const = 0;
    /** Return the total MSHR capacity. */
    virtual std::size_t get_mshr_size() const = 0;
    /** Return the MSHR occupancy as a ratio in [0, 1]. */
    virtual double get_mshr_occupancy_ratio() const = 0;

    /** Return per-channel read queue occupancy. */
    virtual std::vector<std::size_t> get_rq_occupancy() const = 0;
    /** Return per-channel read queue capacity. */
    virtual std::vector<std::size_t> get_rq_size() const = 0;
    /** Return per-channel read queue occupancy as a ratio in [0, 1]. */
    virtual std::vector<double> get_rq_occupancy_ratio() const = 0;

    /** Return per-channel write queue occupancy. */
    virtual std::vector<std::size_t> get_wq_occupancy() const = 0;
    /** Return per-channel write queue capacity. */
    virtual std::vector<std::size_t> get_wq_size() const = 0;
    /** Return per-channel write queue occupancy as a ratio in [0, 1]. */
    virtual std::vector<double> get_wq_occupancy_ratio() const = 0;

    /** Return per-channel prefetch queue occupancy. */
    virtual std::vector<std::size_t> get_pq_occupancy() const = 0;
    /** Return per-channel prefetch queue capacity. */
    virtual std::vector<std::size_t> get_pq_size() const = 0;
    /** Return per-channel prefetch queue occupancy as a ratio in [0, 1]. */
    virtual std::vector<double> get_pq_occupancy_ratio() const = 0;

    /** Return the number of sets in this cache. */
    virtual std::size_t num_sets() const = 0;
    /** Return the number of ways in this cache. */
    virtual std::size_t num_ways() const = 0;
    /** Return the number of offset bits (log2 of block size). */
    virtual champsim::data::bits get_offset_bits() const = 0;
  };

  /**
   * Interface for DRAM memory controller modules.
   *
   * The default implementation is MEMORY_CONTROLLER. Stats can be retrieved
   * per channel.
   */
  struct memory_controller_module: public module_base<memory_controller_module,environment_module>, public operable {
    /** \cond INTERNAL */
    memory_controller_module(champsim::chrono::picoseconds clock_period_) : operable(clock_period_) {}
    virtual ~memory_controller_module() = default;
    /** \endcond */

    /** The stats type returned by get_sim_stats() and get_roi_stats(). */
    using stats_type = dram_stats;
    /** Return the number of DRAM channels. */
    virtual std::size_t get_num_channels() const = 0;
    /** Return simulation-wide statistics for the given channel. */
    virtual stats_type get_sim_stats(std::size_t channel_no) const = 0;
    /** Return region-of-interest statistics for the given channel. */
    virtual stats_type get_roi_stats(std::size_t channel_no) const = 0;

    static std::vector<std::string> format_plaintext(const stats_type& stats);
    static void format_json(const stats_type& stats, champsim::json_stat_builder& b);

    /** Return the total DRAM size. */
    virtual champsim::data::bytes size() const = 0;
  };

  /**
   * Interface for page table walker modules.
   */
  struct page_table_walker_module: public module_base<page_table_walker_module,environment_module>, public operable {
    /** \cond INTERNAL */
    page_table_walker_module(champsim::chrono::picoseconds clock_period_) : operable(clock_period_) {}
    virtual ~page_table_walker_module() = default;
    /** \endcond */
  }; 

  /**
   * Interface for channel modules.
   *
   * Channels connect caches to their lower-level memory. They buffer
   * requests in read (RQ), write (WQ), and prefetch (PQ) queues.
   */
  struct channel_module: public module_base<channel_module,environment_module> {
    /** \cond INTERNAL */
    using request_type = champsim::request;
    using response_type = champsim::response;
    /** \endcond */
    /** The stats type returned by get_sim_stats() and get_roi_stats(). */
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

    /** The container type of the request queues (RQ/WQ/PQ). */
    using request_queue_type = champsim::ring_buffer<request_type>;
    /** The container type of the response queue. Producers push through
     *  pointers to this queue with push_back_grow()/emplace_back_grow():
     *  responses have no modeled capacity bound. */
    using response_queue_type = champsim::ring_buffer<response_type>;

    // Queue accessors for upper-level iteration
    virtual request_queue_type& get_rq() = 0;
    virtual request_queue_type& get_wq() = 0;
    virtual request_queue_type& get_pq() = 0;
    virtual response_queue_type& get_returned() = 0;

    // O(1) event signal: does any request queue hold work? Lets a consumer
    // guard its per-cycle intake machinery (bandwidth/span construction) so
    // an idle channel costs a few empty() checks, not the full scan setup.
    virtual bool has_pending() { return !std::empty(get_rq()) || !std::empty(get_wq()) || !std::empty(get_pq()); }

    // Stats accessors
    virtual stats_type& get_sim_stats() = 0;
    virtual stats_type& get_roi_stats() = 0;

    virtual ~channel_module() = default;
  }; 

  /**
   * Interface for virtual memory modules.
   */
  struct vmem_module: public module_base<vmem_module,environment_module> {
    virtual ~vmem_module() = default;
    virtual std::size_t available_ppages() const = 0;
    // Address spaces are keyed by the origin's stream (origin.asid()): one
    // physical address space, many streams.
    virtual std::pair<champsim::page_number, champsim::chrono::clock::duration> va_to_pa(champsim::origin origin, champsim::page_number vaddr) = 0;
    virtual std::pair<champsim::address, champsim::chrono::clock::duration> get_pte_pa(champsim::origin origin, champsim::page_number vaddr, std::size_t level) = 0;
    virtual champsim::data::bits shamt(std::size_t level) const = 0;
    virtual uint64_t get_offset(champsim::address vaddr, std::size_t level) const = 0;
    virtual std::size_t get_pt_levels() const = 0;
  };

  /**
   * Interface for memory prefetcher modules.
   *
   * Prefetchers are attached to a cache (cache_module). Implement the six
   * virtual methods below and register with register_module to create a
   * custom prefetcher. Use prefetch_line() to issue prefetch requests.
   */
  struct prefetcher: public module_base<prefetcher,cache_module> {

      virtual ~prefetcher() = default;

      /**
       * Called when the cache is initialized.
       * Use this to set up dynamic data structures.
       */
      virtual void prefetcher_initialize() = 0;

      /**
       * Called when a tag check is performed in the cache.
       *
       * \param addr The address of the packet. Includes offset bits for L1 caches;
       *             zero offset otherwise. If ``virtual_prefetch`` is true, this is a
       *             virtual address.
       * \param ip The instruction pointer that initiated the demand. Zero for
       *           prefetches from other levels.
       * \param cache_hit True if this tag check is a hit.
       * \param useful_prefetch True if this tag check hit a prior prefetch.
       * \param type The access type (LOAD, RFO, PREFETCH, WRITE, or TRANSLATION).
       * \param metadata_in Metadata carried by the packet.
       * \return Metadata to store alongside the block.
       */
      virtual uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch,
                                                access_type type, uint32_t metadata_in) = 0;

      /**
       * Called when a miss is filled in the cache.
       *
       * \param addr The address of the filled block (same addressing rules as
       *             prefetcher_cache_operate).
       * \param set The cache set that the fill occurred in.
       * \param way The cache way that the fill occurred in.
       * \param prefetch True if the filled block was a prefetch.
       * \param evicted_addr The address of the evicted block (default-constructed
       *                     if bypass).
       * \param metadata_in Metadata carried by the packet.
       * \return Metadata to store alongside the block.
       */
      virtual uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, 
                                                champsim::address evicted_addr, uint32_t metadata_in) = 0;

      /**
       * Called each cycle after all other cache operations have completed.
       */
      virtual void prefetcher_cycle_operate() = 0;

      /**
       * Called at the end of the simulation. Can be used to print statistics.
       */
      virtual void prefetcher_final_stats() = 0;

      /**
       * Called on branch operations. Useful for instruction prefetchers.
       *
       * \param ip The instruction pointer of the branch.
       * \param branch_type One of BRANCH_DIRECT_JUMP, BRANCH_INDIRECT,
       *        BRANCH_CONDITIONAL, BRANCH_DIRECT_CALL, BRANCH_INDIRECT_CALL,
       *        BRANCH_RETURN, or BRANCH_OTHER.
       * \param branch_target The target address of the branch.
       */
      virtual void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) = 0;

      /**
       * Issue a prefetch request into the parent cache.
       *
       * This delegates to the parent cache's prefetch mechanism. You do not
       * need to store your own parent pointer to use this method.
       *
       * \param pf_addr The address to prefetch.
       * \param fill_this_level If true, fill this cache level; otherwise fill
       *        the next level down.
       * \param prefetch_metadata Metadata to associate with the prefetch.
       * \return True if the prefetch was successfully enqueued.
       */
      bool prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t prefetch_metadata) const;
      /** \overload */
      bool prefetch_line(uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata) const;

  private:
      // The parent cache, for the framework's own prefetch_line delegation
      // only. Modules that need parent access should capture it themselves at
      // construction — the parent is set on the builder before the
      // constructor runs — and store it locally:
      //
      //     my_pref(ModuleBuilder builder)
      //       : cache_(builder.get_parent<champsim::modules::cache_module>()) {}
      cache_module* intern_ = nullptr;
      friend struct module_base<prefetcher, cache_module>;
      void bind(cache_module* parent) { intern_ = parent; }
  };


  /**
   * Interface for cache replacement policy modules.
   *
   * Replacement policies are attached to a cache (cache_module). Implement the
   * five virtual methods below and register with register_module to create a
   * custom replacement policy.
   */
  struct replacement: public module_base<replacement,cache_module> {

      virtual ~replacement() = default;

      /**
       * Called when the cache is initialized.
       * Use this to set up dynamic data structures.
       */
      virtual void initialize_replacement() = 0;

      /**
       * Called when a cache miss requires eviction.
       *
       * \param origin The provenance of the access: origin.cpu() is the
       *        consumer (hardware context) that initiated it, origin.asid()
       *        the address space it belongs to.
       * \param instr_id Instruction count for examining program order of requests.
       * \param set The cache set being accessed.
       * \param current_set A pointer to the beginning of the set being accessed.
       * \param ip The instruction that initiated the demand. Zero for prefetches
       *           from other levels.
       * \param full_addr The address of the packet.
       * \param type The access type (LOAD, RFO, PREFETCH, WRITE, or TRANSLATION).
       * \return The way index to evict, or the total number of ways to bypass.
       */
      virtual long find_victim(champsim::origin origin, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                                      champsim::address full_addr, access_type type) = 0;

      /**
       * Called when a tag check completes (on both hits and misses).
       *
       * \param origin The provenance of the access (see find_victim).
       * \param set The cache set.
       * \param way The cache way.
       * \param full_addr The address of the packet.
       * \param ip The instruction that initiated the demand.
       * \param victim_addr The address of the evicted block (zero on hits).
       * \param type The access type.
       * \param hit True if the packet hit the cache.
       */
      virtual void update_replacement_state(champsim::origin origin, long set, long way, champsim::address full_addr,
                                                  champsim::address ip, champsim::address victim_addr, access_type type, bool hit) = 0;


      /**
       * Called when a block is filled in the cache.
       *
       * This is called with the same timing as find_victim(), and is
       * additionally called when filling an invalid way.
       *
       * \param origin The provenance of the fill (see find_victim).
       * \param set The cache set.
       * \param way The cache way.
       * \param full_addr The address of the filled block.
       * \param ip The instruction that initiated the demand.
       * \param victim_addr The address of the evicted block (zero on hits).
       * \param type The access type.
       */
      virtual void replacement_cache_fill(champsim::origin origin, long set, long way, champsim::address full_addr,
                                                  champsim::address ip, champsim::address victim_addr, access_type type) = 0;

      /**
       * Called at the end of the simulation. Can be used to print statistics.
       */
      virtual void replacement_final_stats() = 0;

  };

  /**
   * Interface for branch predictor modules.
   *
   * Branch predictors are attached to a core (core_module). Implement the
   * three virtual methods below and register with register_module to create
   * a custom branch predictor.
   */
  struct branch_predictor: public module_base<branch_predictor,core_module> {

    virtual ~branch_predictor() = default;

    /**
     * Called when the core is initialized.
     * Use this to set up dynamic data structures.
     */
    virtual void initialize_branch_predictor() = 0;

    /**
     * Called when a branch is resolved. All parameters are guaranteed correct.
     *
     * \param ip The instruction pointer of the branch.
     * \param target The correct target of the branch.
     * \param taken True if the branch was taken.
     * \param branch_type One of BRANCH_DIRECT_JUMP, BRANCH_INDIRECT,
     *        BRANCH_CONDITIONAL, BRANCH_DIRECT_CALL, BRANCH_INDIRECT_CALL,
     *        BRANCH_RETURN, or BRANCH_OTHER.
     */
    virtual void last_branch_result(champsim::address ip, champsim::address target, bool taken, uint8_t branch_type) = 0;

    /**
     * Called when a branch direction prediction is needed.
     *
     * \param ip The instruction pointer of the branch.
     * \param predicted_target The predicted target from the BTB (may be incorrect).
     * \param always_taken True if the BTB determines the branch is always taken.
     * \param branch_type One of BRANCH_DIRECT_JUMP, BRANCH_INDIRECT,
     *        BRANCH_CONDITIONAL, BRANCH_DIRECT_CALL, BRANCH_INDIRECT_CALL,
     *        BRANCH_RETURN, or BRANCH_OTHER.
     * \return True if the branch is predicted taken, false otherwise.
     */
    virtual bool predict_branch(champsim::address ip, champsim::address predicted_target, bool always_taken, uint8_t branch_type) = 0;

  };

  /**
   * Interface for branch target buffer (BTB) modules.
   *
   * BTBs are attached to a core (core_module). Implement the three virtual
   * methods below and register with register_module to create a custom BTB.
   */
  struct btb: public module_base<btb,core_module> {

    virtual ~btb() = default;

    /**
     * Called when the core is initialized.
     * Use this to set up dynamic data structures.
     */
    virtual void initialize_btb() = 0;

    /**
     * Called when a branch is resolved.
     *
     * \param ip The instruction pointer of the branch.
     * \param predicted_target The correct target of the branch.
     * \param taken True if the branch was taken.
     * \param branch_type One of BRANCH_DIRECT_JUMP, BRANCH_INDIRECT,
     *        BRANCH_CONDITIONAL, BRANCH_DIRECT_CALL, BRANCH_INDIRECT_CALL,
     *        BRANCH_RETURN, or BRANCH_OTHER.
     */
    virtual void update_btb(champsim::address ip, champsim::address predicted_target, bool taken, uint8_t branch_type) = 0;

    /**
     * Called when a branch target prediction is needed.
     *
     * \param ip The instruction pointer of the branch.
     * \param branch_type One of the branch type constants.
     * \return A pair of (predicted target address, always-taken flag).
     *         Return ``{champsim::address{}, false}`` if prediction fails.
     */
    virtual std::pair<champsim::address, bool> btb_prediction(champsim::address ip, uint8_t branch_type) = 0;
  };

  /**
   * Workload source interface — provides discrete units of work ("tokens") to a
   * source_consumer. Attach as a submodule of a source_consumer. This base holds
   * only the token-agnostic lifecycle; the typed pull protocol lives on
   * typed_workload_source<Token>. Consumer and sources agree on the token type by
   * construction, so mixed token types coexist (the orchestrator never sees it).
   */
  // Mixin for any module holding a stream identity — the address-space tag
  // stamped on the tokens it produces. Counterpart of source_consumer: any
  // model may inherit it, enumerated by the same startup pass, with the same
  // pinning affordance for holders that mirror another's identity.
  struct stream_source {
    virtual ~stream_source() = default;

    // Stream id: address-space identity stamped on this source's tokens.
    // Assigned at startup; each source gets its own unless sources share a
    // "stream" label. Never written as a number in a config.
    uint32_t stream_id() const
    {
      if (stream_id_.has_value()) {
        return *stream_id_;
      }
      return default_stream();
    }
    void set_stream_id(uint32_t id)
    {
      if (!stream_id_pinned_) {
        stream_id_ = id;
      }
    }
    // Sources mirroring another holder's stream may pin; pinned sources are
    // skipped by enumeration and own no stream slot.
    void pin_stream_id(uint32_t id)
    {
      stream_id_ = id;
      stream_id_pinned_ = true;
    }
    bool stream_id_pinned() const { return stream_id_pinned_; }
    // The configuration's "stream" sharing label; empty when unlabeled.
    const std::string& stream_label() const { return stream_label_; }
    // The instance name this source was configured under (set by the module
    // factory). Use champsim::identities() for id <-> name lookups.
    const std::string& source_name() const { return identity_name_; }
    void set_identity_name(std::string name) { identity_name_ = std::move(name); }

  protected:
    // Set by concrete sources that accept the optional "stream" label.
    std::string stream_label_{};
    // Fallback identity for standalone instances (unit tests) when the
    // startup enumeration has not assigned one.
    virtual uint32_t default_stream() const { return 0; }

  private:
    std::optional<uint32_t> stream_id_{};
    bool stream_id_pinned_ = false;
    std::string identity_name_{};
  };

  struct workload_source : public module_base<workload_source, source_consumer>, public stream_source {
    virtual ~workload_source() = default;

    // True when the source will never provide another token.
    [[nodiscard]] virtual bool eof() const = 0;

    // Human-readable identity for reports (e.g. the trace path). Empty to suppress.
    virtual std::string describe() const { return {}; }

  protected:
    // The consumer this source feeds. Bound by the framework after
    // construction; sources stamp tokens with origin{consumer, stream},
    // where the stream defaults to the consumer's id (see origin.h).
    source_consumer* consumer_ = nullptr;

    // Standalone instances (unit tests) fall back to the owning consumer's id.
    uint32_t default_stream() const override { return static_cast<uint32_t>(consumer_ != nullptr ? consumer_->consumer_id() : 0); }

  private:
    friend struct module_base<workload_source, source_consumer>;
    void bind(source_consumer* parent) { consumer_ = parent; }
  };

  /**
   * Typed pull protocol for workload sources.
   *
   * peek() materializes the next token without consuming it (so paced
   * consumers can wait until it is due), returning nullptr when no token is
   * available — the safe emptiness signal, valid to call at any time.
   * consume() discards the peeked token; next() is the one-shot form.
   *
   * \tparam Token The discrete unit of work this source provides.
   */
  template <typename Token>
  struct typed_workload_source : workload_source {
    // The next token, or nullptr if none is available now. The pointer is
    // valid until consume() or the next peek().
    virtual const Token* peek() = 0;

    // Discard the current peeked token. Only valid after a non-null peek().
    virtual void consume() = 0;

    // Retrieve and consume the next token, if one is available.
    std::optional<Token> next()
    {
      if (const Token* token = peek(); token != nullptr) {
        auto retval = std::optional<Token>{*token};
        consume();
        return retval;
      }
      return std::nullopt;
    }
  };

  /**
   * Instruction-stream source — the token type consumed by core modules.
   *
   * The default implementation (TRACE_WORKLOAD_SOURCE) wraps a tracereader.
   * Override for execution-driven simulation or synthetic workloads.
   */
  struct instruction_source : typed_workload_source<ooo_model_instr> {
    // Execution-driven feedback hooks (no-ops by default).
    // Called by the core at the appropriate pipeline stage.
    virtual void retire_instruction([[maybe_unused]] const ooo_model_instr& instr) {}
    virtual void squash_instruction([[maybe_unused]] const ooo_model_instr& instr) {}
    virtual void branch_mispredict([[maybe_unused]] const ooo_model_instr& instr) {}
  };

  /**
   * Phase controller interface — owns the per-phase loop conditions: observes
   * cycle progress, drives deadlock detection, aggregates consumer-reported
   * health, and signals per-source completion. EOF is polled directly via
   * source_consumer::source_eof(). A non-empty get_phases() list overrides the
   * default warmup+sim pair.
   */
  struct phase_controller : public module_base<phase_controller, environment_module> {
    virtual ~phase_controller() = default;

    enum class status { CONTINUE, COMPLETE, ABORT };

    // Called at the start of a phase
    virtual void begin_phase(const std::string& name, bool is_warmup, uint64_t length) = 0;

    // Called each cycle after all operables have operated.
    // progress: number of operations that made progress this cycle.
    // Returns CONTINUE, COMPLETE, or ABORT.
    virtual status advance(long progress) = 0;

    // Get ids of sources that newly completed since last advance()
    virtual std::vector<unsigned> newly_completed_sources() const = 0;

    // Called at end of phase for cleanup
    virtual void end_phase() = 0;

    // Returns the list of phases this controller wants to run.
    // If empty, the caller (main.cc / champsim::main) defines the phases.
    // Implement this to take full ownership of the run structure from config.
    virtual std::vector<champsim::phase_info> get_phases() const { return {}; }
  };

  /**
   * Listener interface — observes run-wide events for reporting. Ordinary
   * modules: declare as top-level "listener" children, add via --listeners, or
   * rely on the default HEARTBEAT listener. Producers reach active listeners
   * through the emit_* free functions below.
   */
  struct listener : public module_base<listener, environment_module> {
    virtual ~listener() = default;

    // A new phase began (fired once per phase, before module phase hooks).
    virtual void begin_phase(bool /*is_warmup*/) {}

    // A source consumer advanced. Totals are cumulative counts in the
    // consumer's own token unit and clock domain.
    virtual void progress(const source_consumer& /*consumer*/, uint64_t /*total_progress*/, uint64_t /*total_cycles*/) {}
  };

  // Active listener dispatch (single-threaded). main assembles the list once
  // at startup; producers emit through these free functions.
  void set_active_listeners(std::vector<listener*> active);
  const std::vector<listener*>& active_listeners();
  void emit_begin_phase(bool is_warmup);
  void emit_progress(const source_consumer& consumer, uint64_t total_progress, uint64_t total_cycles);

  /**
   * Interface for the top-level environment module.
   *
   * The environment owns and constructs the entire simulation. It provides
   * access to all modules by interface type via view() and typed_view().
   */
  struct environment_module : public module_base<environment_module, environment_module> {
    // Single generic view function: returns all modules of the given interface type.
    // Special interface_type "operable" returns all operable modules across all interfaces.
    virtual std::vector<std::any> view(const std::string& interface_type) const = 0;

    // Called by create_instance for instances whose builder was marked via
    // set_owner_of_submodules (modules built inside a parent, not by the env).
    // Implementations append these to their views after top-level modules
    // (preserving top-level order). Default: ignore.
    virtual void enroll_nested_instance(const std::string& /*interface_name*/, const std::string& /*name*/, std::any /*instance*/) {}

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
