/*
 * Type registry for JSON → std::any conversion.
 *
 * Modules can register custom type converters that map a JSON type-key
 * (e.g. "frequency", "bandwidth") to a parsing function.  Environments
 * use the registry to interpret typed JSON objects like {"frequency": "4G"}
 * without hardcoding the set of supported types.
 *
 * Built-in types (frequency, time, bytes, bits, bandwidth, etc.) are
 * registered in type_registry.cc via static initialisation.  Modules may
 * register additional types with the same mechanism.
 */

#ifndef TYPE_REGISTRY_H
#define TYPE_REGISTRY_H

#include <any>
#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace champsim {

class type_registry {
public:
  // A converter takes the JSON *value* from {"type_key": value} and returns
  // the correctly-typed std::any.
  using converter_fn = std::function<std::any(const nlohmann::json&)>;

  // Register a converter for a type key.  Duplicate keys are an error.
  static void register_type(const std::string& type_key, converter_fn fn) {
    auto& reg = registry();
    if (reg.count(type_key)) {
      // Allow silent overwrite — modules may want to override builtins.
      // If that is undesirable, uncomment the error below.
      // fmt::print("[TYPE_REGISTRY] ERROR: duplicate type key: {}\n", type_key);
      // std::exit(-1);
    }
    reg[type_key] = std::move(fn);
  }

  // Try to convert a single-key JSON object using the registry.
  // Returns true and fills |out| if the key is registered; false otherwise.
  static bool try_convert(const nlohmann::json& obj, std::any& out) {
    if (!obj.is_object() || obj.size() != 1) return false;
    auto it = obj.begin();
    auto reg_it = registry().find(it.key());
    if (reg_it == registry().end()) return false;
    out = reg_it->second(it.value());
    return true;
  }

  // Check whether a type key is registered.
  static bool has_type(const std::string& type_key) {
    return registry().count(type_key) > 0;
  }

  // RAII helper for static registration, analogous to register_module.
  struct register_type_helper {
    register_type_helper(const std::string& type_key, converter_fn fn) {
      type_registry::register_type(type_key, std::move(fn));
    }
  };

private:
  static std::map<std::string, converter_fn>& registry() {
    static std::map<std::string, converter_fn> r;
    return r;
  }
};

} // namespace champsim

#endif // TYPE_REGISTRY_H
