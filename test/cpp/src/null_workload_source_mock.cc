/*
 * Test-only no-op workload source. Always EOF, never produces an instruction.
 *
 * Registered as "NULL_INSTRUCTION_SOURCE" so any test that needs to satisfy a
 * core's required workload_source submodule (without driving real
 * instructions) can attach it by model name.
 *
 * Linked only into the test binary; not part of bin/champsim.
 */

#include "instruction.h"
#include "modules.h"
#include "instruction_source.h"

namespace
{

struct null_workload_source_mock : public champsim::modules::instruction_source {
  explicit null_workload_source_mock(champsim::modules::ModuleBuilder /*builder*/) {}

  const ooo_model_instr* peek() override { return nullptr; }
  void consume() override {}
  [[nodiscard]] bool eof() const override { return true; }
};

static champsim::modules::instruction_source::register_module<null_workload_source_mock>
    null_ws_mock_reg("NULL_INSTRUCTION_SOURCE");

} // anonymous namespace
