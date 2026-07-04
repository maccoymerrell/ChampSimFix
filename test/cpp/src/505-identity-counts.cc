#include <catch.hpp>
#include <nlohmann/json.hpp>

#include "environment.h"
#include "instr.h"
#include "modules.h"

namespace
{

struct probe_source_505 : public champsim::modules::instruction_source {
  explicit probe_source_505(champsim::modules::ModuleBuilder) {}
  const ooo_model_instr* peek() override { return nullptr; }
  void consume() override {}
  [[nodiscard]] bool eof() const override { return true; }
};

static champsim::modules::workload_source::register_module<probe_source_505> probe_source_reg("PROBE_SOURCE_505");

struct probe_core_505 : public champsim::modules::core_module {
  explicit probe_core_505(champsim::modules::ModuleBuilder builder) : core_module(champsim::chrono::picoseconds{250})
  {
    for (const auto& sub : builder.get_submodules("workload_source", true)) {
      champsim::modules::workload_source::create_instance(sub, static_cast<champsim::modules::source_consumer*>(this));
    }
  }

  void push_instruction(ooo_model_instr) override {}
  std::size_t instructions_requested() override { return 0; }
  uint64_t sim_instr() const override { return 0; }
  uint64_t sim_cycle() const override { return 0; }
  long operate() override { return 0; }
  cpu_stats get_sim_stats() const override { return {}; }
  cpu_stats get_roi_stats() const override { return {}; }
  bool source_eof() const override { return true; }
};

static champsim::modules::core_module::register_module<probe_core_505> probe_core_reg("PROBE_CORE_505");

} // namespace

TEST_CASE("num_consumers counts consumers, num_sources counts sources, even when they differ")
{
  // One consumer holding two sources plus one consumer holding one: the two
  // counts must diverge (2 consumers, 3 sources). Per-consumer tables are
  // indexed by origin's consumer id, so num_consumers must be the consumer
  // count, not the source count.
  nlohmann::json config = {
      {"children",
       nlohmann::json::array({
           nlohmann::json{
               {"name", "t505_c0"},
               {"module", "core"},
               {"model", "PROBE_CORE_505"},
               {"children",
                nlohmann::json::array({
                    nlohmann::json{{"name", "t505_c0_srcA"}, {"module", "workload_source"}, {"model", "PROBE_SOURCE_505"}},
                    nlohmann::json{{"name", "t505_c0_srcB"}, {"module", "workload_source"}, {"model", "PROBE_SOURCE_505"}},
                })},
           },
           nlohmann::json{
               {"name", "t505_c1"},
               {"module", "core"},
               {"model", "PROBE_CORE_505"},
               {"children",
                nlohmann::json::array({
                    nlohmann::json{{"name", "t505_c1_src"}, {"module", "workload_source"}, {"model", "PROBE_SOURCE_505"}},
                })},
           },
       })},
  };

  auto env_builder = champsim::modules::ModuleBuilder("t505_env", "ENVIRONMENT").add_parameter("config_json", config).add_parameter("cli_args",
                                                                                                                                    nlohmann::json::object());
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  REQUIRE(env != nullptr);

  auto& globals = champsim::modules::ModuleBuilder::globals();
  CHECK(globals.get_parameter<std::size_t>("num_consumers") == 2);
  CHECK(globals.get_parameter<std::size_t>("num_sources") == 3);
}
