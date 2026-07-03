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

#ifndef IDENTITY_REGISTRY_H
#define IDENTITY_REGISTRY_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace champsim
{
/**
 * Name <-> id association for the framework-assigned identities.
 *
 * Identities never appear in configurations; this registry, populated by
 * assign_identities() at startup, is how anything user-facing (reports,
 * tools, modules that want to label output) translates between a consumer
 * or stream id and the configured instance names.
 *
 * A consumer id maps to exactly one name. A stream id maps to one or more
 * source names (sources sharing a "stream" label share one id).
 */
class identity_registry
{
  std::map<int, std::string> consumer_names_;
  std::map<std::string, int> consumer_ids_;
  std::map<uint32_t, std::vector<std::string>> stream_sources_;
  std::map<std::string, uint32_t> stream_ids_;

public:
  void register_consumer(int id, std::string name)
  {
    consumer_ids_[name] = id;
    consumer_names_[id] = std::move(name);
  }

  void register_source(uint32_t stream, std::string name)
  {
    stream_ids_[name] = stream;
    stream_sources_[stream].push_back(std::move(name));
  }

  /** The configured name of the consumer holding this id. */
  std::optional<std::string> consumer_name(int id) const
  {
    if (auto it = consumer_names_.find(id); it != std::end(consumer_names_)) {
      return it->second;
    }
    return std::nullopt;
  }

  /** The id assigned to the consumer configured under this name. */
  std::optional<int> consumer_id(const std::string& name) const
  {
    if (auto it = consumer_ids_.find(name); it != std::end(consumer_ids_)) {
      return it->second;
    }
    return std::nullopt;
  }

  /** The configured names of every source stamping this stream. */
  std::vector<std::string> stream_sources(uint32_t stream) const
  {
    if (auto it = stream_sources_.find(stream); it != std::end(stream_sources_)) {
      return it->second;
    }
    return {};
  }

  /** The stream assigned to the source configured under this name. */
  std::optional<uint32_t> stream_id(const std::string& name) const
  {
    if (auto it = stream_ids_.find(name); it != std::end(stream_ids_)) {
      return it->second;
    }
    return std::nullopt;
  }

  void clear()
  {
    consumer_names_.clear();
    consumer_ids_.clear();
    stream_sources_.clear();
    stream_ids_.clear();
  }
};

/** The process-wide registry, rebuilt by each assign_identities() call. */
identity_registry& identities();
} // namespace champsim

#endif
