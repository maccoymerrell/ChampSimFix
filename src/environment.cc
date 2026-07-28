/*
 * Explicit environment for ChampSim. Reads a hierarchical JSON config where each
 * module specifies name, interface ("module"), and "model"; "@name" references
 * resolve in declaration order. Fully generic: no interface or module names are
 * hardcoded — any registered interface/model works.
 */

#include "environment.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "champsim.h"
#include "chrono.h"
#include "type_registry.h"
#include "util/bits.h"

using json = nlohmann::json;
using namespace champsim::modules;

namespace
{

// Try to parse an @-reference string, returning the referenced name if valid.
std::optional<std::string> try_parse_ref(const std::string& s)
{
  if (!s.empty() && s[0] == '@')
    return s.substr(1);
  return std::nullopt;
}

// Check if a string is a $-variable (CLI arg reference)
bool is_var(const std::string& s) { return !s.empty() && s[0] == '$'; }
std::string var_name(const std::string& s) { return s.substr(1); }

// Check if a JSON array is entirely @-references
bool is_ref_array(const json& arr)
{
  if (!arr.is_array() || arr.empty())
    return false;
  for (auto& elem : arr) {
    if (!elem.is_string() || !try_parse_ref(elem.get<std::string>()))
      return false;
  }
  return true;
}

// Forward-declare so add_param can recurse via $-variable resolution.
void add_param(ModuleBuilder& builder, const std::string& key, const json& val, const std::string& mod_name,
               const std::map<std::string, std::any>& modules_by_name, const std::map<std::string, std::string>& module_interfaces, const json& cli_args);

// Resolve a $-variable from the CLI args map by re-entering add_param with the
// resolved JSON value, so any type the dispatch handles (scalar, typed object,
// reference, array) works for variables too.
void resolve_var(const std::string& val_str, const std::string& key, const std::string& mod_name, const std::map<std::string, std::any>& modules_by_name,
                 const std::map<std::string, std::string>& module_interfaces, const json& cli_args, ModuleBuilder& builder)
{
  std::string vn = var_name(val_str);
  if (!cli_args.contains(vn)) {
    fmt::print("[ENVIRONMENT] ERROR: $-variable '{}' not found in CLI args (used in '{}' param '{}')\n", vn, mod_name, key);
    std::exit(-1);
  }
  add_param(builder, key, cli_args[vn], mod_name, modules_by_name, module_interfaces, cli_args);
}

// Add a single (key, val) JSON parameter to the builder. The environment owns
// only structural concerns (null skipping, $-variable and @-reference
// resolution); everything else flows through type_registry::try_convert, so a
// new type is a registry registration, not a change here.
void add_param(ModuleBuilder& builder, const std::string& key, const json& val, const std::string& mod_name,
               const std::map<std::string, std::any>& modules_by_name, const std::map<std::string, std::string>& module_interfaces, const json& cli_args)
{
  if (val.is_null()) {
    return;
  }
  if (val.is_string() && is_var(val.get<std::string>())) {
    resolve_var(val.get<std::string>(), key, mod_name, modules_by_name, module_interfaces, cli_args, builder);
    return;
  }
  if (auto ref = val.is_string() ? try_parse_ref(val.get<std::string>()) : std::nullopt) {
    const auto& rn = *ref;
    auto mit = modules_by_name.find(rn);
    if (mit == modules_by_name.end()) {
      fmt::print("[ENVIRONMENT] ERROR: @-reference '{}' not found (used in '{}' param '{}')\n", rn, mod_name, key);
      std::exit(-1);
    }
    builder.add_raw_parameter(key, mit->second);
    return;
  }
  if (val.is_array() && is_ref_array(val)) {
    std::vector<std::any> refs;
    std::string ref_iface;
    for (auto& elem : val) {
      auto rn = *try_parse_ref(elem.get<std::string>());
      auto mit = modules_by_name.find(rn);
      if (mit == modules_by_name.end()) {
        fmt::print("[ENVIRONMENT] ERROR: @-reference '{}' not found (in array param '{}' of '{}')\n", rn, key, mod_name);
        std::exit(-1);
      }
      std::string curr_iface = module_interfaces.at(rn);
      if (ref_iface.empty()) {
        ref_iface = curr_iface;
      } else if (curr_iface != ref_iface) {
        fmt::print("[ENVIRONMENT] ERROR: mixed interface types in array '{}' of '{}': expected '{}', got '{}' for '{}'\n", key, mod_name, ref_iface, curr_iface,
                   rn);
        std::exit(-1);
      }
      refs.push_back(mit->second);
    }
    builder.add_raw_parameter(key, interface_registry::make_vector(ref_iface, refs));
    return;
  }

  // Everything else flows through the type_registry: typed objects
  // (e.g. {"frequency": "4G"}) are converted via the named-type
  // registrations; bare scalars and arrays use the kind defaults.
  std::any converted;
  if (champsim::type_registry::try_convert(val, converted)) {
    builder.add_raw_parameter(key, std::move(converted));
  }
}

// Populate a ModuleBuilder from a JSON node with full type support (typed
// objects, @-references, $-variables, arrays, scalars) and recursive children.
// cli_args: flat JSON object for $-variable substitution.
void populate_builder(const json& node, ModuleBuilder& builder, const std::map<std::string, std::any>& modules_by_name,
                      const std::map<std::string, std::string>& module_interfaces, const json& cli_args, std::vector<ModuleBuilder::scope_frame_type> frames)
{
  const std::string& name = builder.get_name();

  // A "globals" object opens a lexical scope: its keys are visible to this
  // module and everything beneath it, unless locally shadowed. Only this block
  // is inherited; ordinary parameters stay module-local.
  if (auto it = node.find("globals"); it != node.end() && it->is_object()) {
    ModuleBuilder frame_builder{name + ".globals", "<scope>"};
    for (auto& [key, val] : it->items()) {
      add_param(frame_builder, key, val, name, modules_by_name, module_interfaces, cli_args);
    }
    frames.insert(std::begin(frames), std::make_shared<const std::map<std::string, std::any>>(frame_builder.get_parameters()));
  }
  builder.inherit_scope(frames);

  // Process all JSON parameters (skip reserved keys)
  for (auto& [key, val] : node.items()) {
    if (key == "name" || key == "module" || key == "model" || key == "children" || key == "_comment" || key == "globals")
      continue;
    add_param(builder, key, val, name, modules_by_name, module_interfaces, cli_args);
  }

  // Recursively handle nested children as submodules
  if (node.contains("children")) {
    for (auto& sub : node["children"]) {
      if (!sub.contains("name") || !sub.contains("module") || !sub.contains("model")) {
        fmt::print("[ENVIRONMENT] ERROR: submodule of '{}' missing 'name', 'module', or 'model'\n", name);
        std::exit(-1);
      }
      std::string sub_iface = sub["module"].get<std::string>();
      std::string sub_name = sub["name"].get<std::string>();
      std::string sub_model = sub["model"].get<std::string>();

      ModuleBuilder child_builder{sub_name, sub_model};
      populate_builder(sub, child_builder, modules_by_name, module_interfaces, cli_args, frames);
      builder.add_submodule(sub_iface, std::move(child_builder));
    }
  }
}

} // anonymous namespace

// Register as "ENVIRONMENT"
static environment_module::register_module<champsim::environment> explicit_env_register("ENVIRONMENT");

champsim::environment::environment(ModuleBuilder builder)
{
  builder_params_[(builder.get_name().empty() ? "ENVIRONMENT" : builder.get_name())] = builder;
  json config = builder.get_parameter<json>("config_json");
  auto cli_args = builder.get_parameter<json>("cli_args", true, json::object());

  block_size_ = config.value("block_size", 64u);
  page_size_ = config.value("page_size", 4096u);

  if (!config.contains("children")) {
    fmt::print("[ENVIRONMENT] ERROR: config must contain a 'children' array\n");
    std::exit(-1);
  }

  auto& children = config["children"];

  // Pre-construction: count consumers and sources to publish to the globals
  // before any module is built. Modules sizing per-consumer tables read
  // num_consumers via get_parameter fall-through, so it must exactly match the
  // space assign_identities later enumerates. Consumer-/source-ness is a
  // per-model trait recorded at register_module time. Configs may override via
  // a root-level "num_consumers" key.
  std::size_t num_consumers = 0;
  std::size_t num_sources = 0;
  std::size_t num_streams = 0;
  std::set<std::string> stream_labels_seen;
  std::function<void(const json&)> count_identities = [&](const json& node) {
    const auto module_key = node.value("module", "");
    const auto model_key = node.value("model", "");
    if (modules::interface_registry::model_is_source(module_key, model_key)) {
      ++num_sources;
      // Streams follow the assignment rule: labeled sources share one id per
      // distinct "stream" label, unlabeled sources get their own.
      const auto label = node.value("stream", "");
      if (label.empty() || stream_labels_seen.insert(label).second) {
        ++num_streams;
      }
    }
    if (modules::interface_registry::model_is_consumer(module_key, model_key)) {
      ++num_consumers;
    }
    if (node.contains("children")) {
      for (const auto& sub : node["children"]) {
        count_identities(sub);
      }
    }
  };
  for (const auto& child : children) {
    count_identities(child);
  }
  num_consumers = config.value("num_consumers", num_consumers);
  num_streams = config.value("num_streams", num_streams);

  // Publish system-wide params to the globals before construction. Every
  // non-reserved top-level scalar becomes a global, visible via get_parameter
  // fall-through (a root "globals" object works too). Reserved names are the
  // config's structural and orchestration keys.
  {
    auto& g = ModuleBuilder::globals();

    static const std::set<std::string> reserved{"children",    "name",      "module",     "model",  "_comment",  "_description",
                                                "environment", "num_cores", "cycle_skip", "phases", "listeners", "heartbeat_frequency",
                                                "globals"};
    ModuleBuilder root_scope{"<root>", "<scope>"};
    for (auto& [key, val] : config.items()) {
      if (reserved.count(key) != 0 || val.is_structured() || val.is_null()) {
        continue;
      }
      add_param(root_scope, key, val, "<root>", modules_by_name_, module_interfaces_, cli_args);
    }
    if (auto it = config.find("globals"); it != config.end() && it->is_object()) {
      for (auto& [key, val] : it->items()) {
        add_param(root_scope, key, val, "<root>", modules_by_name_, module_interfaces_, cli_args);
      }
    }
    for (const auto& [key, val] : root_scope.get_parameters()) {
      g.add_raw_parameter(key, val);
    }

    // Canonical system-wide values (derived where not configured)
    g.add_parameter("block_size", block_size_);
    g.add_parameter("page_size", page_size_);
    g.add_parameter("log2_block_size", static_cast<unsigned>(champsim::lg2(block_size_)));
    g.add_parameter("log2_page_size", static_cast<unsigned>(champsim::lg2(page_size_)));
    g.add_parameter("num_consumers", num_consumers);
    g.add_parameter("num_sources", num_sources);
    g.add_parameter("num_streams", num_streams);
  }
  // Sync cached address extents with the freshly-published globals so the hot
  // path doesn't pay a lookup per address-slice construction.
  champsim::refresh_address_extents();

  for (auto& child : children) {
    if (!child.contains("name") || !child.contains("module") || !child.contains("model")) {
      fmt::print("[ENVIRONMENT] ERROR: each child must have 'name', 'module', and 'model'\n");
      std::exit(-1);
    }

    std::string name = child["name"].get<std::string>();
    std::string iface = child["module"].get<std::string>();
    std::string model = child["model"].get<std::string>();

    auto mod_builder = ModuleBuilder{name, model};

    populate_builder(child, mod_builder, modules_by_name_, module_interfaces_, cli_args, {});

    // Submodule builders self-enroll their instances (enroll_nested_instance)
    // so nested modules join the views; the top-level module itself is
    // registered below, preserving declaration order.
    mod_builder.set_owner_of_submodules(this);

    // Create the module via the interface registry
    std::any typed_ptr = interface_registry::create(iface, mod_builder, static_cast<environment_module*>(this));
    modules_by_name_[name] = typed_ptr;
    module_interfaces_[name] = iface;
    builder_params_[name] = mod_builder;

    // Store in the type-indexed collection
    modules_by_type_[iface].push_back(typed_ptr);
    module_order_.emplace_back(name, iface);
  }

  // Deadlock threshold from parameter types alone: every picoseconds param is a
  // time value; min is the time quantum, sum the worst-case total, so sum/min
  // is worst-case cycles (floored at 500).
  {
    using ps_rep = champsim::chrono::picoseconds::rep;
    ps_rep min_ps = std::numeric_limits<ps_rep>::max();
    ps_rep sum_ps = 0;
    for (auto& [name, bp] : builder_params_) {
      for (auto& [key, val] : bp.get_parameters()) {
        if (auto* p = std::any_cast<champsim::chrono::picoseconds>(&val)) {
          if (p->count() > 0) {
            min_ps = std::min(min_ps, p->count());
            sum_ps += p->count();
          }
        }
      }
    }
    if (min_ps < std::numeric_limits<ps_rep>::max() && min_ps > 0)
      deadlock_cycles_ = static_cast<int>(std::max((sum_ps / min_ps) * 3, ps_rep{500}));
  }
}

// ====== Nested-instance enrollment ======

void champsim::environment::enroll_nested_instance(const std::string& interface_name, const std::string& name, std::any instance)
{
  if (interface_name.empty()) {
    return; // interface was never registered by name; nothing to file it under
  }
  if (modules_by_name_.count(name) != 0U) {
    fmt::print("[ENVIRONMENT] ERROR: duplicate module name '{}' (nested instance collides with an existing module)\n", name);
    std::exit(-1);
  }
  modules_by_name_[name] = instance;
  module_interfaces_[name] = interface_name;
  nested_by_type_[interface_name].push_back(instance);
  nested_order_.emplace_back(name, interface_name);
}

// ====== Generic view function ======

auto champsim::environment::view(const std::string& interface_type) const -> std::vector<std::any>
{
  // Collect matches for an aggregate view: converter-filtered walk of the
  // top-level modules (declaration order) followed by nested instances
  // (creation order), so pre-existing top-level ordering is preserved.
  auto collect_aggregate = [this](auto&& get_converter) {
    std::vector<std::any> result;
    for (const auto* order : {&module_order_, &nested_order_}) {
      for (auto& [name, iface] : *order) {
        auto converter = get_converter(iface);
        if (converter) {
          auto& typed_ptr = modules_by_name_.at(name);
          // Converters are per-instance dynamic_casts: they return nullptr
          // for models that do not actually inherit the aggregate mixin
          // (e.g. the default channel), so only genuine matches enroll.
          if (auto* match = converter(typed_ptr)) {
            result.push_back(match);
          }
        }
      }
    }
    return result;
  };

  if (interface_type == "operable") {
    return collect_aggregate([](const std::string& iface) { return interface_registry::get_to_operable(iface); });
  }

  if (interface_type == "source_consumer") {
    return collect_aggregate([](const std::string& iface) { return interface_registry::get_to_source_consumer(iface); });
  }

  if (interface_type == "stream_source") {
    return collect_aggregate([](const std::string& iface) { return interface_registry::get_to_stream_source(iface); });
  }

  std::vector<std::any> result;
  if (auto it = modules_by_type_.find(interface_type); it != modules_by_type_.end()) {
    result = it->second;
  }
  if (auto it = nested_by_type_.find(interface_type); it != nested_by_type_.end()) {
    result.insert(result.end(), it->second.begin(), it->second.end());
  }
  return result;
}
