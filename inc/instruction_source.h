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

#ifndef INSTRUCTION_SOURCE_H
#define INSTRUCTION_SOURCE_H

#include "instruction.h"
#include "modules.h"

namespace champsim::modules
{
/**
 * Instruction-stream interface — the token source a core attaches.
 *
 * Extends token_source with the instruction token type and the
 * execution-driven feedback hooks. This is the registered interface
 * ("instruction_source"); the shipped implementation (model
 * INSTRUCTION_SOURCE) wraps a tracereader. Override for execution-driven
 * simulation or synthetic instruction workloads.
 */
struct instruction_source : public typed_token_source<ooo_model_instr>, public module_base<instruction_source, token_consumer> {
  // Execution-driven feedback hooks (no-ops by default).
  // Called by the core at the appropriate pipeline stage.
  virtual void retire_instruction([[maybe_unused]] const ooo_model_instr& instr) {}
  virtual void squash_instruction([[maybe_unused]] const ooo_model_instr& instr) {}
  virtual void branch_mispredict([[maybe_unused]] const ooo_model_instr& instr) {}

private:
  friend struct module_base<instruction_source, token_consumer>;
  // Bound to the owning consumer by the framework after construction.
  void bind(token_consumer* parent) { consumer_ = parent; }
};
} // namespace champsim::modules

#endif
