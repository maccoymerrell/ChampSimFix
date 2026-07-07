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

#ifndef UTIL_TYPE_TRAITS_H
#define UTIL_TYPE_TRAITS_H

#include <type_traits>
#include <any>

namespace champsim
{
template <typename T, template <typename...> typename Primary>
inline constexpr bool is_specialization_v = false;

template <template <typename...> typename Primary, typename... Args>
inline constexpr bool is_specialization_v<Primary<Args...>, Primary> = true;

template <typename T, template <typename...> typename Primary>
struct is_specialization : std::bool_constant<is_specialization_v<T, Primary>> {
};

  // Try to extract a numeric value from std::any by attempting casts from common stored types.
  // Returns true and sets 'out' on success.
  template<typename T>
  auto numeric_any_cast(const std::any& val, T& out) -> std::enable_if_t<std::is_arithmetic_v<T>, bool> {
    // Try each common numeric type that Python config might produce
    if (auto* p = std::any_cast<int>(&val))               { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<unsigned>(&val))           { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<long>(&val))               { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<unsigned long>(&val))      { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<long long>(&val))          { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<unsigned long long>(&val)) { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<uint8_t>(&val))            { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<uint16_t>(&val))           { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<uint32_t>(&val))           { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<uint64_t>(&val))           { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<int8_t>(&val))             { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<int16_t>(&val))            { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<int32_t>(&val))            { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<int64_t>(&val))            { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<float>(&val))              { out = static_cast<T>(*p); return true; }
    if (auto* p = std::any_cast<double>(&val))             { out = static_cast<T>(*p); return true; }
    return false;
  }

  template<typename T>
  auto numeric_any_cast(const std::any&, T&) -> std::enable_if_t<!std::is_arithmetic_v<T>, bool> {
    return false;
  }

} // namespace champsim

#endif
