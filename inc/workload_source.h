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

#ifndef WORKLOAD_SOURCE_H
#define WORKLOAD_SOURCE_H

#include <optional>

#include "instruction.h"
#include "modules.h"

namespace champsim::modules
{
struct workload_source : public module_base<workload_source, source_consumer>, public stream_source {
  virtual ~workload_source() = default;

  // True when the source will never provide another token.
  [[nodiscard]] virtual bool eof() const = 0;

  // Human-readable identity for reports (e.g. the trace path). Empty to suppress.
  virtual std::string describe() const { return {}; }

protected:
  // The consumer this source feeds. Bound by the framework after
  // construction; sources stamp tokens with origin{consumer, stream},
  // where the stream defaults to the consumer's id (see origin.h).
  source_consumer* consumer_ = nullptr;

  // Standalone instances (unit tests) fall back to the owning consumer's
  // id, the historical default.
  uint32_t default_stream() const override { return static_cast<uint32_t>(consumer_ != nullptr ? consumer_->consumer_id() : 0); }

private:
  friend struct module_base<workload_source, source_consumer>;
  void bind(source_consumer* parent) { consumer_ = parent; }
};

/**
 * Typed pull protocol for workload sources.
 *
 * peek() materializes the next token without consuming it (so paced
 * consumers can wait until it is due), returning nullptr when no token is
 * available — the safe emptiness signal, valid to call at any time.
 * consume() discards the peeked token; next() is the one-shot form.
 *
 * \tparam Token The discrete unit of work this source provides.
 */
template <typename Token>
struct typed_workload_source : workload_source {
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

/**
 * Instruction-stream source — the token type consumed by core modules.
 *
 * The default implementation (TRACE_WORKLOAD_SOURCE) wraps a tracereader.
 * Override for execution-driven simulation or synthetic workloads.
 */
struct instruction_source : typed_workload_source<ooo_model_instr> {
  // Execution-driven feedback hooks (no-ops by default).
  // Called by the core at the appropriate pipeline stage.
  virtual void retire_instruction([[maybe_unused]] const ooo_model_instr& instr) {}
  virtual void squash_instruction([[maybe_unused]] const ooo_model_instr& instr) {}
  virtual void branch_mispredict([[maybe_unused]] const ooo_model_instr& instr) {}
};
} // namespace champsim::modules

#endif
