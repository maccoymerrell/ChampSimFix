/*
 * Explicit environment implementation for ChampSim.
 * Reads a hierarchical JSON configuration where each module explicitly specifies
 * its name, interface type ("module"), and model ("model"). References to other
 * modules use "@name" syntax and are resolved in declaration order.
 *
 * This implementation is fully generic: no interface types or module names are
 * hardcoded. Any registered interface and model will work without alteration.
 */

#include "environment.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
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

namespace {

// Try to parse an @-reference string, returning the referenced name if valid.
std::optional<std::string> try_parse_ref(const std::string& s) {
  if (!s.empty() && s[0] == '@') return s.substr(1);
  return std::nullopt;
}

// Check if a string is a $-variable (CLI arg reference)
bool is_var(const std::string& s) { return !s.empty() && s[0] == '$'; }
std::string var_name(const std::string& s) { return s.substr(1); }

// Check if a JSON array is entirely @-references
bool is_ref_array(const json& arr) {
  if (!arr.is_array() || arr.empty()) return false;
  for (auto& elem : arr) {
    if (!elem.is_string() || !try_parse_ref(elem.get<std::string>())) return false;
  }
  return true;
}

// Forward-declare so add_param can recurse via $-variable resolution.
void add_param(ModuleBuilder& builder, const std::string& key, const json& val,
               const std::string& mod_name,
               const std::map<std::string, std::any>& modules_by_name,
               const std::map<std::string, std::string>& module_interfaces,
               const json& cli_args);

// Resolve a $-variable from the CLI args map by re-entering add_param with the
// resolved JSON value, so any type the dispatch handles (scalar, typed object,
// reference, array) works for variables too.
void resolve_var(const std::string& val_str, const std::string& key,
                 const std::string& mod_name,
                 const std::map<std::string, std::any>& modules_by_name,
                 const std::map<std::string, std::string>& module_interfaces,
                 const json& cli_args, ModuleBuilder& builder)
{
  std::string vn = var_name(val_str);
  if (!cli_args.contains(vn)) {
    fmt::print("[ENVIRONMENT] ERROR: $-variable '{}' not found in CLI args (used in '{}' param '{}')\n",
               vn, mod_name, key);
    std::exit(-1);
  }
  add_param(builder, key, cli_args[vn], mod_name, modules_by_name, module_interfaces, cli_args);
}

// Add a single (key, val) JSON parameter to the builder.
//
// The environment owns only the structural concerns (null skipping,
// $-variable resolution, @-reference lookup). Everything else — scalars,
// arrays, typed objects — flows through ``type_registry::try_convert``,
// so adding a new type is a registry registration rather than a change
// here.
void add_param(ModuleBuilder& builder, const std::string& key, const json& val,
               const std::string& mod_name,
               const std::map<std::string, std::any>& modules_by_name,
               const std::map<std::string, std::string>& module_interfaces,
               const json& cli_args)
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
        fmt::print("[ENVIRONMENT] ERROR: mixed interface types in array '{}' of '{}': expected '{}', got '{}' for '{}'\n",
                   key, mod_name, ref_iface, curr_iface, rn);
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

// Populate a ModuleBuilder with parameters from a JSON node, with full type
// support (typed objects, @-references, $-variables, arrays, scalars) and
// recursive children.
// cli_args: flat JSON object of CLI arguments available for $-variable substitution.
void populate_builder(const json& node, ModuleBuilder& builder,
                      const std::map<std::string, std::any>& modules_by_name,
                      const std::map<std::string, std::string>& module_interfaces,
                      const json& cli_args)
{
  const std::string& name = builder.get_name();

  // Process all JSON parameters (skip reserved keys)
  for (auto& [key, val] : node.items()) {
    if (key == "name" || key == "module" || key == "model" || key == "children" || key == "_comment") continue;
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
      populate_builder(sub, child_builder, modules_by_name, module_interfaces, cli_args);
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

  // Pre-construction: count workload_source children so we can publish
  // num_sources to the globals before any module is constructed. Modules
  // that need the source count (e.g. ship/drrip for per-source tables)
  // read it via builder.get_parameter fall-through.
  std::function<std::size_t(const json&)> count_workload_sources = [&](const json& node) -> std::size_t {
    std::size_t n = 0;
    if (node.contains("children")) {
      for (const auto& sub : node["children"]) {
        if (sub.value("module", "") == "workload_source") ++n;
        n += count_workload_sources(sub);
      }
    }
    return n;
  };
  std::size_t num_sources = 0;
  for (const auto& child : children) {
    if (child.value("module", "") == "workload_source") ++num_sources;
    num_sources += count_workload_sources(child);
  }

  // Publish system-wide params to the globals before module construction.
  {
    auto& g = ModuleBuilder::globals();
    g.add_parameter("block_size",      block_size_);
    g.add_parameter("page_size",       page_size_);
    g.add_parameter("log2_block_size", static_cast<unsigned>(champsim::lg2(block_size_)));
    g.add_parameter("log2_page_size",  static_cast<unsigned>(champsim::lg2(page_size_)));
    g.add_parameter("num_sources",     num_sources);
  }
  // Sync the cached address extents (page_number, block_offset, etc.) with
  // the freshly-published globals so the hot path doesn't pay a lookup per
  // address-slice construction.
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

    populate_builder(child, mod_builder, modules_by_name_, module_interfaces_, cli_args);

    // Create the module via the interface registry
    std::any typed_ptr = interface_registry::create(iface, mod_builder, static_cast<environment_module*>(this));
    modules_by_name_[name] = typed_ptr;
    module_interfaces_[name] = iface;
    builder_params_[name] = mod_builder;

    // Store in the type-indexed collection
    modules_by_type_[iface].push_back(typed_ptr);
    module_order_.emplace_back(name, iface);
  }

  // Compute deadlock threshold purely from parameter types.
  // Every champsim::chrono::picoseconds parameter is a time value; the minimum
  // is the time quantum and the sum of all latencies is the worst-case total.
  // sum/min gives worst-case cycles, floored at 500.
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
      deadlock_cycles_ = static_cast<int>(std::max((sum_ps / min_ps)*3, ps_rep{500}));
  }

  // Post-construction: publish num_cpus as a deprecated alias for the actual
  // count of source_consumers in the constructed system. New code should read
  // num_sources instead.
  ModuleBuilder::globals().add_parameter("num_cpus", view("source_consumer").size());
}

// ====== Generic view function ======

auto champsim::environment::view(const std::string& interface_type) const -> std::vector<std::any>
{
  if (interface_type == "operable") {
    // Aggregate all operable modules in declaration order
    std::vector<std::any> result;
    for (auto& [name, iface] : module_order_) {
      auto to_op = interface_registry::get_to_operable(iface);
      if (to_op) {
        auto& typed_ptr = modules_by_name_.at(name);
        result.push_back(static_cast<champsim::operable*>(to_op(typed_ptr)));
      }
    }
    return result;
  }

  if (interface_type == "source_consumer") {
    // Aggregate all source_consumer modules in declaration order
    std::vector<std::any> result;
    for (auto& [name, iface] : module_order_) {
      auto to_sc = interface_registry::get_to_source_consumer(iface);
      if (to_sc) {
        auto& typed_ptr = modules_by_name_.at(name);
        result.push_back(static_cast<source_consumer*>(to_sc(typed_ptr)));
      }
    }
    return result;
  }

  auto it = modules_by_type_.find(interface_type);
  if (it == modules_by_type_.end()) return {};
  return it->second;
}
