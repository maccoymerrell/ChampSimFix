/*
 * Default trace-driven workload source module.
 * Wraps a champsim::tracereader to provide instructions from a trace file.
 * Parameters (from ModuleBuilder):
 *   - trace_file (std::string): path to the trace file
 *   - cpu (uint8_t): CPU index for instruction stamping
 *   - cloudsuite (bool, optional): use cloudsuite trace format (default: false)
 *   - repeat (bool, optional): loop the trace on EOF (default: false)
 */

#include "modules.h"
#include "tracereader.h"

namespace
{

struct wrong_path_trace_workload_source : public champsim::modules::workload_source {
  champsim::tracereader reader_;

  explicit wrong_path_trace_workload_source(champsim::modules::ModuleBuilder builder)
      : reader_(get_wp_tracereader(builder.get_parameter<std::string>("trace_file"), builder.get_parameter<uint8_t>("cpu"),
                                   builder.get_parameter<bool>("repeat", true, false), builder.get_parameter<bool>("wrong_path_enabled", true, false)))
  {
  }

  ooo_model_instr next_instruction(const uint64_t next_pc = 0xdeadbeef) override { return reader_(next_pc); }
  [[nodiscard]] bool eof() const override { return reader_.eof(); }
};

static champsim::modules::workload_source::register_module<wrong_path_trace_workload_source> trace_ws_reg("WRONG_PATH_TRACE_WORKLOAD_SOURCE");

} // anonymous namespace
