/*
 * Test-only no-op workload source. Always EOF, never produces an instruction.
 *
 * Registered as "NULL_WORKLOAD_SOURCE" so any test that needs to satisfy a
 * core's required workload_source submodule (without driving real
 * instructions) can attach it by model name.
 *
 * Linked only into the test binary; not part of bin/champsim.
 */

#include "instruction.h"
#include "modules.h"

namespace
{

struct null_workload_source_mock : public champsim::modules::workload_source {
  explicit null_workload_source_mock(champsim::modules::ModuleBuilder /*builder*/) {}

  ooo_model_instr next_instruction() override { return ooo_model_instr{0, input_instr{}}; }
  [[nodiscard]] bool eof() const override { return true; }
};

static champsim::modules::workload_source::register_module<null_workload_source_mock>
    null_ws_mock_reg("NULL_WORKLOAD_SOURCE");

} // anonymous namespace
