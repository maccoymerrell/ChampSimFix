#include <catch.hpp>

#include <map>
#include <nlohmann/json.hpp>

#include "environment.h"
#include "instr.h"
#include "modules.h"

namespace champsim
{
void assign_identities(modules::environment_module& env);
}

namespace
{

// A stream identity held via the mixin rather than a workload_source interface;
// must be counted by num_sources/num_streams and enumerated by assign_identities
// exactly like a workload_source.
struct probe_streaming_core_505 : public champsim::modules::core_module, public champsim::modules::stream_source {
  explicit probe_streaming_core_505(champsim::modules::ModuleBuilder) : core_module(champsim::chrono::picoseconds{250}) {}

  void push_instruction(ooo_model_instr) override {}
  std::size_t instructions_requested() override { return 0; }
  uint64_t sim_instr() const override { return 0; }
  uint64_t sim_cycle() const override { return 0; }
  long operate() override { return 0; }
  cpu_stats get_sim_stats() const override { return {}; }
  cpu_stats get_roi_stats() const override { return {}; }
  bool source_eof() const override { return true; }
};

static champsim::modules::core_module::register_module<probe_streaming_core_505> probe_streaming_core_reg("PROBE_STREAMING_CORE_505");

struct probe_source_505 : public champsim::modules::instruction_source {
  explicit probe_source_505(champsim::modules::ModuleBuilder builder) { stream_label_ = builder.get_parameter<std::string>("stream", true, std::string{}); }
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

TEST_CASE("num_consumers, num_sources, and num_streams are each counted in their own space")
{
  // Three consumers (two cores + one stream_source-mixin core), four sources
  // (c0's two share a label, c1's one, the mixin core its own), three streams
  // (the shared label collapses c0's pair). Every count differs, so any
  // conflation fails loudly.
  nlohmann::json config = {
      {"children",
       nlohmann::json::array({
           nlohmann::json{
               {"name", "t505_c0"},
               {"module", "core"},
               {"model", "PROBE_CORE_505"},
               {"children",
                nlohmann::json::array({
                    nlohmann::json{{"name", "t505_c0_srcA"}, {"module", "workload_source"}, {"model", "PROBE_SOURCE_505"}, {"stream", "t505_shared"}},
                    nlohmann::json{{"name", "t505_c0_srcB"}, {"module", "workload_source"}, {"model", "PROBE_SOURCE_505"}, {"stream", "t505_shared"}},
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
           nlohmann::json{{"name", "t505_c2"}, {"module", "core"}, {"model", "PROBE_STREAMING_CORE_505"}},
       })},
  };

  auto env_builder = champsim::modules::ModuleBuilder("t505_env", "ENVIRONMENT").add_parameter("config_json", config).add_parameter("cli_args",
                                                                                                                                    nlohmann::json::object());
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  REQUIRE(env != nullptr);

  auto& globals = champsim::modules::ModuleBuilder::globals();
  CHECK(globals.get_parameter<std::size_t>("num_consumers") == 3);
  CHECK(globals.get_parameter<std::size_t>("num_sources") == 4);
  CHECK(globals.get_parameter<std::size_t>("num_streams") == 3);

  // The mixin source is enumerated symmetrically with interface sources. View
  // order is top-level modules before nested ones, so assertions key on names,
  // not positions.
  champsim::assign_identities(*env);
  auto sources = env->typed_view<champsim::modules::stream_source>("stream_source");
  REQUIRE(std::size(sources) == 4);
  std::map<std::string, uint32_t> stream_of;
  for (auto& src : sources) {
    stream_of[src.get().source_name()] = src.get().stream_id();
  }
  REQUIRE(std::size(stream_of) == 4);
  CHECK(stream_of.at("t505_c0_srcA") == stream_of.at("t505_c0_srcB")); // shared label, one stream
  CHECK(stream_of.at("t505_c1_src") != stream_of.at("t505_c0_srcA"));
  CHECK(stream_of.at("t505_c2") != stream_of.at("t505_c0_srcA"));
  CHECK(stream_of.at("t505_c2") != stream_of.at("t505_c1_src"));
}
