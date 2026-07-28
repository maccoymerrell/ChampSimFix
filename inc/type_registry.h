/*
 * Type registry for JSON → std::any conversion.
 *
 * Modules can register custom converters that map either a JSON
 * *type-key* (e.g. "frequency", "bandwidth" — used in
 * ``{"frequency": "4G"}``) or a JSON *kind* (boolean, integer, string,
 * array, …) to a parsing function. Environments funnel every parameter
 * value through ``try_convert`` so the conversion logic is centralized
 * here and modules can register new types/kinds without touching the
 * environment.
 *
 * Built-in types (frequency, time, bytes, bits, bandwidth, etc.) and
 * kind defaults (bool, integer, float, string, etc.) are registered in
 * type_registry.cc via static initialisation.
 */

#ifndef TYPE_REGISTRY_H
#define TYPE_REGISTRY_H

#include <any>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace champsim
{

class type_registry
{
public:
  // A converter takes the JSON *value* from {"type_key": value} and returns
  // the correctly-typed std::any.
  using converter_fn = std::function<std::any(const nlohmann::json&)>;

  // Stable identifiers for JSON value kinds, used by the kind-based
  // dispatch path in try_convert.
  enum class kind { boolean, integer, unsigned_integer, floating_point, string, array, object };

  // Register a converter for a type key. Duplicate keys silently
  // overwrite so modules can replace builtins.
  static void register_type(const std::string& type_key, converter_fn fn) { type_registry_map()[type_key] = std::move(fn); }

  // Register a default converter for a JSON value kind.
  static void register_kind(kind k, converter_fn fn) { kind_registry_map()[k] = std::move(fn); }

  // Try to convert any JSON value into a typed std::any.
  //
  //   1. If ``val`` is a single-key object whose key matches a registered
  //      type-key, use that converter.
  //   2. Otherwise, dispatch on ``val``'s JSON kind to a registered
  //      kind converter.
  //
  // Returns true on success.
  static bool try_convert(const nlohmann::json& val, std::any& out)
  {
    if (val.is_object() && val.size() == 1) {
      auto it = val.begin();
      auto reg_it = type_registry_map().find(it.key());
      if (reg_it != type_registry_map().end()) {
        out = reg_it->second(it.value());
        return true;
      }
    }
    auto k = kind_of(val);
    if (!k.has_value())
      return false;
    auto reg_it = kind_registry_map().find(*k);
    if (reg_it == kind_registry_map().end())
      return false;
    out = reg_it->second(val);
    return true;
  }

  // Check whether a type key is registered.
  static bool has_type(const std::string& type_key) { return type_registry_map().count(type_key) > 0; }

  // RAII helper for static registration, analogous to register_module.
  struct register_type_helper {
    register_type_helper(const std::string& type_key, converter_fn fn) { type_registry::register_type(type_key, std::move(fn)); }
  };

  // RAII helper for kind registration.
  struct register_kind_helper {
    register_kind_helper(kind k, converter_fn fn) { type_registry::register_kind(k, std::move(fn)); }
  };

private:
  static std::optional<kind> kind_of(const nlohmann::json& v)
  {
    if (v.is_null())
      return std::nullopt;
    if (v.is_boolean())
      return kind::boolean;
    if (v.is_number_unsigned())
      return kind::unsigned_integer;
    if (v.is_number_integer())
      return kind::integer;
    if (v.is_number_float())
      return kind::floating_point;
    if (v.is_string())
      return kind::string;
    if (v.is_array())
      return kind::array;
    if (v.is_object())
      return kind::object;
    return std::nullopt;
  }

  static std::map<std::string, converter_fn>& type_registry_map()
  {
    static std::map<std::string, converter_fn> r;
    return r;
  }
  static std::map<kind, converter_fn>& kind_registry_map()
  {
    static std::map<kind, converter_fn> r;
    return r;
  }
};

} // namespace champsim

#endif // TYPE_REGISTRY_H
