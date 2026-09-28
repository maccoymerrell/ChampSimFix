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

#ifndef ACCESS_TYPE_H
#define ACCESS_TYPE_H

#include <array>
#include <string_view>

// LOAD and PREFETCH are generic: each matches its INSTRUCTION_ and DATA_ specialization, so a
// pref_activate_mask entry or a prefetcher's `type == access_type::LOAD` covers both, while
// INSTRUCTION_LOAD != DATA_LOAD. Packets in flight carry the specific types; the core tags fetches
// INSTRUCTION_LOAD and data loads DATA_LOAD, and a cache's prefetches inherit the side of the access
// being served.
enum class access_type : unsigned {
  LOAD = 0,
  RFO,
  PREFETCH,
  WRITE,
  TRANSLATION,
  INSTRUCTION_LOAD,
  DATA_LOAD,
  INSTRUCTION_PREFETCH,
  DATA_PREFETCH,
  NUM_TYPES,
};

using namespace std::literals::string_view_literals;
inline constexpr std::array<std::string_view, static_cast<std::size_t>(access_type::NUM_TYPES)> access_type_names{
    "LOAD"sv, "RFO"sv, "PREFETCH"sv, "WRITE"sv, "TRANSLATION"sv, "INSTRUCTION_LOAD"sv, "DATA_LOAD"sv, "INSTRUCTION_PREFETCH"sv, "DATA_PREFETCH"sv};

/** The generic type a specific type refines (itself for types with no specializations). */
constexpr access_type generic_access_type(access_type a)
{
  switch (a) {
  case access_type::INSTRUCTION_LOAD:
  case access_type::DATA_LOAD:
    return access_type::LOAD;
  case access_type::INSTRUCTION_PREFETCH:
  case access_type::DATA_PREFETCH:
    return access_type::PREFETCH;
  default:
    return a;
  }
}

/** True for instruction-side accesses (a fetch, or a prefetch issued on behalf of one). */
constexpr bool is_instruction_access(access_type a)
{
  return static_cast<unsigned>(a) == static_cast<unsigned>(access_type::INSTRUCTION_LOAD)
         || static_cast<unsigned>(a) == static_cast<unsigned>(access_type::INSTRUCTION_PREFETCH);
}

/** Identity, or a generic type against one of its specializations. Not transitive: key ordered
 * containers on the underlying value (operator<), never on this. */
constexpr bool operator==(access_type a, access_type b)
{
  const auto ua = static_cast<unsigned>(a);
  const auto ub = static_cast<unsigned>(b);
  return ua == ub || ua == static_cast<unsigned>(generic_access_type(b)) || ub == static_cast<unsigned>(generic_access_type(a));
}
constexpr bool operator!=(access_type a, access_type b) { return !(a == b); }

inline auto format_as(access_type a) { return access_type_names[static_cast<unsigned>(a)]; }

inline access_type access_type_from_string(std::string_view s)
{
  for (std::size_t i = 0; i < access_type_names.size(); ++i) {
    if (access_type_names[i] == s)
      return static_cast<access_type>(i);
  }
  return access_type::NUM_TYPES;
}

#endif
