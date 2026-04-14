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
#include <limits>
#include <map>
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

// Check if a string is an @-reference (module reference)
bool is_ref(const std::string& s) { return !s.empty() && s[0] == '@'; }
std::string ref_name(const std::string& s) { return s.substr(1); }

// Check if a string is a $-variable (CLI arg reference)
bool is_var(const std::string& s) { return !s.empty() && s[0] == '$'; }
std::string var_name(const std::string& s) { return s.substr(1); }

// Check if a JSON array is entirely @-references
bool is_ref_array(const json& arr) {
  if (!arr.is_array() || arr.empty()) return false;
  for (auto& elem : arr) {
    if (!elem.is_string() || !is_ref(elem.get<std::string>())) return false;
  }
  return true;
}

// Resolve a $-variable from the CLI args map and add it to the builder.
// Returns true if consumed, false if not a $-variable.
bool try_resolve_var(const std::string& val_str, const std::string& key,
                     const std::string& mod_name, const json& cli_args,
                     ModuleBuilder& builder)
{
  if (!is_var(val_str)) return false;
  std::string vn = var_name(val_str);
  if (!cli_args.contains(vn)) {
    fmt::print("[EXPLICIT_ENVIRONMENT] ERROR: $-variable '{}' not found in CLI args (used in '{}' param '{}')\n",
               vn, mod_name, key);
    std::exit(-1);
  }
  const auto& cv = cli_args[vn];
  if (cv.is_string())        builder.add_parameter(key, cv.get<std::string>());
  else if (cv.is_boolean())  builder.add_parameter(key, cv.get<bool>());
  else if (cv.is_number_integer()) builder.add_parameter(key, cv.get<int64_t>());
  else if (cv.is_number_float())   builder.add_parameter(key, cv.get<double>());
  else {
    fmt::print("[EXPLICIT_ENVIRONMENT] ERROR: unsupported type for $-variable '{}'\n", vn);
    std::exit(-1);
  }
  return true;
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

    if (val.is_null()) {
      continue;
    } else if (val.is_string() && is_var(val.get<std::string>())) {
      // Resolve $variable from CLI args
      try_resolve_var(val.get<std::string>(), key, name, cli_args, builder);
    } else if (val.is_string() && is_ref(val.get<std::string>())) {
      std::string rn = ref_name(val.get<std::string>());
      auto mit = modules_by_name.find(rn);
      if (mit == modules_by_name.end()) {
        fmt::print("[EXPLICIT_ENVIRONMENT] ERROR: @-reference '{}' not found (used in '{}' param '{}')\n", rn, name, key);
        std::exit(-1);
      }
      builder.add_raw_parameter(key, mit->second);
    } else if (val.is_array() && is_ref_array(val)) {
      std::vector<std::any> refs;
      std::string ref_iface;
      for (auto& elem : val) {
        std::string rn = ref_name(elem.get<std::string>());
        auto mit = modules_by_name.find(rn);
        if (mit == modules_by_name.end()) {
          fmt::print("[EXPLICIT_ENVIRONMENT] ERROR: @-reference '{}' not found (in array param '{}' of '{}')\n", rn, key, name);
          std::exit(-1);
        }
        std::string curr_iface = module_interfaces.at(rn);
        if (ref_iface.empty()) {
          ref_iface = curr_iface;
        } else if (curr_iface != ref_iface) {
          fmt::print("[EXPLICIT_ENVIRONMENT] ERROR: mixed interface types in array '{}' of '{}': expected '{}', got '{}' for '{}'\n",
                     key, name, ref_iface, curr_iface, rn);
          std::exit(-1);
        }
        refs.push_back(mit->second);
      }
      builder.add_raw_parameter(key, interface_registry::make_vector(ref_iface, refs));
    } else if (val.is_object()) {
      std::any typed_val;
      if (champsim::type_registry::try_convert(val, typed_val)) {
        builder.add_raw_parameter(key, std::move(typed_val));
      } else {
        builder.add_parameter(key, val);
      }
    } else if (val.is_boolean()) {
      builder.add_parameter(key, val.get<bool>());
    } else if (val.is_number_integer()) {
      builder.add_parameter(key, val.get<int64_t>());
    } else if (val.is_number_float()) {
      builder.add_parameter(key, val.get<double>());
    } else if (val.is_string()) {
      builder.add_parameter(key, val.get<std::string>());
    } else if (val.is_array()) {
      if (!val.empty() && val[0].is_string()) {
        std::vector<std::string> sv;
        for (auto& e : val) sv.push_back(e.get<std::string>());
        builder.add_parameter(key, sv);
      } else if (!val.empty() && val[0].is_array()) {
        std::array<std::array<uint32_t, 3>, 16> dims{};
        for (std::size_t i = 0; i < val.size() && i < 16; i++) {
          for (std::size_t j = 0; j < val[i].size() && j < 3; j++) {
            dims[i][j] = static_cast<uint32_t>(val[i][j].get<int64_t>());
          }
        }
        builder.add_parameter(key, dims);
      } else {
        builder.add_parameter(key, val);
      }
    }
  }

  // Recursively handle nested children as submodules
  if (node.contains("children")) {
    for (auto& sub : node["children"]) {
      if (!sub.contains("name") || !sub.contains("module") || !sub.contains("model")) {
        fmt::print("[EXPLICIT_ENVIRONMENT] ERROR: submodule of '{}' missing 'name', 'module', or 'model'\n", name);
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

// Register as "EXPLICIT_ENVIRONMENT"
static environment_module::register_module<champsim::environment> explicit_env_register("explicit_environment");

champsim::environment::environment(ModuleBuilder builder)
{
  builder_params_[(builder.get_name().empty() ? "ENVIRONMENT" : builder.get_name())] = builder;
  json config = builder.get_parameter<json>("config_json");
  auto cli_args = builder.get_parameter<json>("cli_args", true, json::object());

  block_size_ = config.value("block_size", 64u);
  page_size_ = config.value("page_size", 4096u);

  if (!config.contains("children")) {
    fmt::print("[EXPLICIT_ENVIRONMENT] ERROR: config must contain a 'children' array\n");
    std::exit(-1);
  }

  auto& children = config["children"];

  for (auto& child : children) {
    if (!child.contains("name") || !child.contains("module") || !child.contains("model")) {
      fmt::print("[EXPLICIT_ENVIRONMENT] ERROR: each child must have 'name', 'module', and 'model'\n");
      std::exit(-1);
    }

    std::string name = child["name"].get<std::string>();
    std::string iface = child["module"].get<std::string>();
    std::string model = child["model"].get<std::string>();

    auto mod_builder = ModuleBuilder{name, model};

    // Inject system-wide parameters so modules can access them via builder
    unsigned log2_block = static_cast<unsigned>(champsim::lg2(block_size_));
    unsigned log2_page = static_cast<unsigned>(champsim::lg2(page_size_));
    mod_builder.add_parameter("block_size", block_size_)
               .add_parameter("page_size", page_size_)
               .add_parameter("log2_block_size", log2_block)
               .add_parameter("log2_page_size", log2_page);

    // Populate parameters (with full type support) and submodules (recursive)
    // Note: JSON params override the system defaults above if specified.
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
  // is the time quantum and the maximum is the worst-case single latency.
  // 2x max/min gives worst-case cycles, floored at 500.
  {
    using ps_rep = champsim::chrono::picoseconds::rep;
    ps_rep min_ps = std::numeric_limits<ps_rep>::max();
    ps_rep max_ps = 0;
    for (auto& [name, bp] : builder_params_) {
      for (auto& [key, val] : bp.get_parameters()) {
        if (auto* p = std::any_cast<champsim::chrono::picoseconds>(&val)) {
          if (p->count() > 0) {
            min_ps = std::min(min_ps, p->count());
            max_ps = std::max(max_ps, p->count());
          }
        }
      }
    }
    if (min_ps < std::numeric_limits<ps_rep>::max() && min_ps > 0)
      deadlock_cycles_ = static_cast<int>(std::max(max_ps * 2 / min_ps, ps_rep{500}));
  }
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
