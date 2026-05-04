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

struct trace_workload_source : public champsim::modules::workload_source {
  champsim::tracereader reader_;

  explicit trace_workload_source(champsim::modules::ModuleBuilder builder)
    : reader_(get_tracereader(
        builder.get_parameter<std::string>("trace_file"),
        builder.get_parameter<uint8_t>("cpu"),
        builder.get_parameter<bool>("cloudsuite", true, false),
        builder.get_parameter<bool>("repeat", true, false)))
  {}

  ooo_model_instr next_instruction() override { return reader_(); }
  [[nodiscard]] bool eof() const override { return reader_.eof(); }
};

static champsim::modules::workload_source::register_module<trace_workload_source>
    trace_ws_reg("TRACE_WORKLOAD_SOURCE");

} // anonymous namespace
