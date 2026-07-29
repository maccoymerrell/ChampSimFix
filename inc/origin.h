/*
 *    Copyright 2026 The ChampSim Contributors
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

#ifndef CHAMPSIM_ORIGIN_H
#define CHAMPSIM_ORIGIN_H

#include <cstdint>
#include <limits>
#include <fmt/format.h>

namespace champsim
{

// Provenance of a work unit: the CONSUMER (hardware context — core/injector/port, dense in [0,num_consumers), aliased cpu()) and the STREAM
// (workload/address space, the ASID, aliased asid()), equal in the one-trace-per-core case. Access via methods so future remapping lands in one place.
class origin
{
public:
  using id_type = uint32_t;
  static constexpr id_type invalid_id = std::numeric_limits<id_type>::max();

private:
  id_type consumer_ = invalid_id;
  id_type stream_ = invalid_id;

public:
  constexpr origin() = default;
  constexpr origin(id_type consumer_id, id_type stream_id) : consumer_(consumer_id), stream_(stream_id) {}

  // Canonical accessors
  [[nodiscard]] constexpr id_type consumer() const { return consumer_; }
  [[nodiscard]] constexpr id_type stream() const { return stream_; }

  // Domain-familiar aliases: cpu() is the hardware context, asid() the address space id.
  [[nodiscard]] constexpr id_type cpu() const { return consumer(); }
  [[nodiscard]] constexpr id_type asid() const { return stream(); }

  [[nodiscard]] constexpr bool has_consumer() const { return consumer_ != invalid_id; }
  [[nodiscard]] constexpr bool has_stream() const { return stream_ != invalid_id; }

  // Derivation helpers for stamping sites
  [[nodiscard]] constexpr origin with_consumer(id_type consumer_id) const { return origin{consumer_id, stream_}; }
  [[nodiscard]] constexpr origin with_stream(id_type stream_id) const { return origin{consumer_, stream_id}; }

  friend constexpr bool operator==(const origin& lhs, const origin& rhs) { return lhs.consumer_ == rhs.consumer_ && lhs.stream_ == rhs.stream_; }
  friend constexpr bool operator!=(const origin& lhs, const origin& rhs) { return !(lhs == rhs); }
  friend constexpr bool operator<(const origin& lhs, const origin& rhs)
  {
    return lhs.consumer_ < rhs.consumer_ || (lhs.consumer_ == rhs.consumer_ && lhs.stream_ < rhs.stream_);
  }
};

} // namespace champsim

template <>
struct fmt::formatter<champsim::origin> : fmt::formatter<std::string> {
  auto format(const champsim::origin& value, fmt::format_context& ctx) const
  {
    return fmt::formatter<std::string>::format(fmt::format("origin{{consumer={}, stream={}}}", value.consumer(), value.stream()), ctx);
  }
};

#endif // CHAMPSIM_ORIGIN_H
