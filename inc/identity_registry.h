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

#ifndef IDENTITY_REGISTRY_H
#define IDENTITY_REGISTRY_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace champsim
{
// Name<->id map for framework-assigned identities (never in configs, populated
// by assign_identities()): a consumer id maps to one name, a source id to the
// source names sharing its "producer_group" label.
class identity_registry
{
  std::map<int, std::string> consumer_names_;
  std::map<std::string, int> consumer_ids_;
  std::map<uint32_t, std::vector<std::string>> packet_producers_;
  std::map<std::string, uint32_t> producer_ids_;

public:
  void register_consumer(int id, std::string name)
  {
    consumer_ids_[name] = id;
    consumer_names_[id] = std::move(name);
  }

  void register_producer(uint32_t source, std::string name)
  {
    producer_ids_[name] = source;
    packet_producers_[source].push_back(std::move(name));
  }

  std::optional<std::string> consumer_name(int id) const
  {
    if (auto it = consumer_names_.find(id); it != std::end(consumer_names_)) {
      return it->second;
    }
    return std::nullopt;
  }

  std::optional<int> consumer_id(const std::string& name) const
  {
    if (auto it = consumer_ids_.find(name); it != std::end(consumer_ids_)) {
      return it->second;
    }
    return std::nullopt;
  }

  std::vector<std::string> packet_producers(uint32_t source) const
  {
    if (auto it = packet_producers_.find(source); it != std::end(packet_producers_)) {
      return it->second;
    }
    return {};
  }

  std::optional<uint32_t> producer_id(const std::string& name) const
  {
    if (auto it = producer_ids_.find(name); it != std::end(producer_ids_)) {
      return it->second;
    }
    return std::nullopt;
  }

  void clear()
  {
    consumer_names_.clear();
    consumer_ids_.clear();
    packet_producers_.clear();
    producer_ids_.clear();
  }
};

/** The process-wide registry, rebuilt by each assign_identities() call. */
identity_registry& identities();
} // namespace champsim

#endif
