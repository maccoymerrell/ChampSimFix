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

#ifndef STREAM_SOURCE_H
#define STREAM_SOURCE_H

#include <cstdint>
#include <string>

namespace champsim::modules
{
// Mixin for any module that holds a stream identity — the address-space
// tag stamped on the tokens it produces. The exact counterpart of
// source_consumer: any model of any interface may inherit it (the
// workload_source interface does, but so may e.g. a channel model that
// synthesizes requests), it is enumerated by the same startup pass, and
// it supports the same pinning affordance for models that mirror another
// holder's identity rather than owning a slot.
struct stream_source {
  virtual ~stream_source() = default;

  // Stream id: the address-space identity stamped on this source's tokens.
  // Assigned by the framework at startup: every source gets its own stream
  // unless sources share a "stream" label in the configuration, in which
  // case they share one id. Never written as a number in a configuration.
  uint32_t stream_id() const
  {
    if (stream_id_.has_value()) {
      return *stream_id_;
    }
    return default_stream();
  }
  void set_stream_id(uint32_t id)
  {
    if (!stream_id_pinned_) {
      stream_id_ = id;
    }
  }
  // Sources that mirror another holder's stream (rather than owning an
  // address space of their own) may pin; pinned sources are skipped by the
  // startup enumeration and do not occupy a stream slot.
  void pin_stream_id(uint32_t id)
  {
    stream_id_ = id;
    stream_id_pinned_ = true;
  }
  bool stream_id_pinned() const { return stream_id_pinned_; }
  // The configuration's "stream" sharing label; empty when unlabeled.
  const std::string& stream_label() const { return stream_label_; }
  // The instance name this source was configured under (set by the module
  // factory). Use champsim::identities() for id <-> name lookups.
  const std::string& source_name() const { return identity_name_; }
  void set_identity_name(std::string name) { identity_name_ = std::move(name); }

protected:
  // Set by concrete sources that accept the optional "stream" label.
  std::string stream_label_{};
  // Fallback identity for standalone instances (unit tests) when the
  // startup enumeration has not assigned one.
  virtual uint32_t default_stream() const { return 0; }

private:
  std::optional<uint32_t> stream_id_{};
  bool stream_id_pinned_ = false;
  std::string identity_name_{};
};
} // namespace champsim::modules

#endif
