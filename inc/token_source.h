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

#ifndef TOKEN_SOURCE_H
#define TOKEN_SOURCE_H

#include <cstdint>
#include <optional>
#include <string>

#include "token_consumer.h"

namespace champsim::modules
{
// Base contract for a source of work tokens. It carries the source identity
// stamped on the tokens it produces and the token lifecycle; concrete token
// types extend it via typed_token_source (see instruction_source). Bound to
// the consumer it feeds after construction; sources stamp tokens with
// origin{consumer, source}, where the source id defaults to the consumer's id
// (see origin.h).
struct token_source {
  virtual ~token_source() = default;

  // True when the source will never provide another token.
  [[nodiscard]] virtual bool eof() const = 0;

  // Human-readable identity for reports (e.g. the trace path). Empty to suppress.
  virtual std::string describe() const { return {}; }

  // Source id: identifies which source produced this token. Assigned by the
  // framework at startup: every source gets its own id unless sources share a
  // "source_group" label in the configuration, in which case they share one.
  // Each context reads it through the token's origin (e.g. asid() in the
  // instruction/address-space context). Never written as a number in a configuration.
  uint32_t source_id() const
  {
    if (source_id_.has_value()) {
      return *source_id_;
    }
    return default_source();
  }
  void set_source_id(uint32_t id)
  {
    if (!source_id_pinned_) {
      source_id_ = id;
    }
  }
  // Sources that mirror another holder's source id (rather than owning one of
  // their own) may pin; pinned sources are skipped by the startup enumeration
  // and do not occupy an id slot.
  void pin_source_id(uint32_t id)
  {
    source_id_ = id;
    source_id_pinned_ = true;
  }
  bool source_id_pinned() const { return source_id_pinned_; }
  // The configuration's "source_group" sharing label; empty when unlabeled.
  const std::string& source_group() const { return source_group_; }
  // The instance name this source was configured under (set by the module
  // factory). Use champsim::identities() for id <-> name lookups.
  const std::string& source_name() const { return identity_name_; }
  void set_identity_name(std::string name) { identity_name_ = std::move(name); }

protected:
  // The consumer this source feeds; bound by the framework after construction.
  token_consumer* consumer_ = nullptr;
  // Set by concrete sources that accept the optional "source_group" label.
  std::string source_group_{};
  // Fallback identity for standalone instances (unit tests): the owning
  // consumer's id, the historical default.
  uint32_t default_source() const { return static_cast<uint32_t>(consumer_ != nullptr ? consumer_->consumer_id() : 0); }

private:
  std::optional<uint32_t> source_id_{};
  bool source_id_pinned_ = false;
  std::string identity_name_{};
};

/**
 * Typed pull protocol for token sources.
 *
 * peek() materializes the next token without consuming it (so paced
 * consumers can wait until it is due), returning nullptr when no token is
 * available — the safe emptiness signal, valid to call at any time.
 * consume() discards the peeked token; next() is the one-shot form.
 *
 * \tparam Token The discrete unit of work this source provides.
 */
template <typename Token>
struct typed_token_source : token_source {
  // The next token, or nullptr if none is available now. The pointer is
  // valid until consume() or the next peek().
  virtual const Token* peek() = 0;

  // Discard the current peeked token. Only valid after a non-null peek().
  virtual void consume() = 0;

  // Retrieve and consume the next token, if one is available.
  std::optional<Token> next()
  {
    if (const Token* token = peek(); token != nullptr) {
      auto retval = std::optional<Token>{*token};
      consume();
      return retval;
    }
    return std::nullopt;
  }
};
} // namespace champsim::modules

#endif
