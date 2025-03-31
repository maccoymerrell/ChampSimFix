#include <vector>
#include <unordered_map>
#include <limits>
#include <filesystem>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator {

class TraceRecorder : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, TraceRecorder, "TraceRecorder", "CounterBasedTRR.")
  private:
    IDRAM* m_dram;

    std::filesystem::path m_trace_path; 
    Logger_t m_tracer;

    Clk_t m_clk = 0;

    bool trace_active = false;

  public:
    void init() override { 
      m_trace_path = param<std::string>("path").desc("Path to the trace file").required();
      auto parent_path = m_trace_path.parent_path();
      std::filesystem::create_directories(parent_path);
      if (!(std::filesystem::exists(parent_path) && std::filesystem::is_directory(parent_path))) {
        throw ConfigurationError("Invalid path to trace file: {}", parent_path.string());
      }
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;

      auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(fmt::format("{}.ch{}", m_trace_path.string(), m_ctrl->m_channel_id), true);
      m_tracer = std::make_shared<spdlog::logger>(fmt::format("trace_recorder_ch{}", m_ctrl->m_channel_id), sink);
      m_tracer->set_pattern("%v");
      m_tracer->set_level(spdlog::level::trace);      
    };

    void finalize() override {
      m_tracer->trace("{}, {}, {}",m_clk,"END_OF_SIMULATION","0, 0, 0, 0, 0, 0000000000000000, 64");
    }

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
      m_clk++;

      //clk cmd bank bankgroup rank row column data datasize
      if (request_found) {
        if(m_dram->m_command_meta(req_it->command).is_accessing && !trace_active) {
          trace_active = true;
          m_clk = 0;
        }
        if(!trace_active)
          return;

        AddrVec_t addr_vec = req_it->addr_vec;
        for (auto& entry : addr_vec) {
          if(entry == -1)
            entry = 0;
        }
        m_tracer->trace(
          "{}, {}, {}, {}, {}, {}, {}, 0000000000000000, 64", 
          m_clk,
          m_dram->m_commands(req_it->command) == "REFab" ? "REFA" : m_dram->m_commands(req_it->command),
          addr_vec[m_dram->m_levels("rank")],
          addr_vec[m_dram->m_levels("bankgroup")],
          addr_vec[m_dram->m_levels("bank")],
          addr_vec[m_dram->m_levels("row")],
          addr_vec[m_dram->m_levels("column")]
        );
      }

    };

};

}       // namespace Ramulator
