#include <catch.hpp>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "chrono.h"
#include "modules.h"
#include "operable.h"
#include "phase_info.h"

// The multi-controller phase loop from champsim.cc
namespace champsim {
void run_phase(const std::string& phase_name, bool is_warmup, bool roi, uint64_t length,
               modules::environment_module& env,
               std::vector<std::reference_wrapper<modules::phase_controller>>& controllers,
               champsim::chrono::clock& global_clock,
               std::function<void(unsigned)> on_source_complete);
}

namespace
{

// Records the (warmup, roi) pair its phase hooks observe.
struct phase_probe_915 : public champsim::operable, public champsim::module_phase {
  std::vector<std::pair<bool, bool>> begins;
  phase_probe_915() : champsim::operable(champsim::chrono::picoseconds{250}) {}
  long operate() override { return 1; }
  void begin_phase(bool warmup, bool roi) override { begins.emplace_back(warmup, roi); }
  void end_phase() override {}
};

struct mock_core_915 : public champsim::modules::core_module {
  uint64_t instr_count = 0;
  explicit mock_core_915(champsim::modules::ModuleBuilder)
    : core_module(champsim::chrono::picoseconds{250}) {}
  void push_instruction(ooo_model_instr) override {}
  std::size_t instructions_requested() override { return 0; }
  uint64_t sim_instr() const override { return instr_count; }
  uint64_t sim_cycle() const override { return 0; }
  long operate() override { ++instr_count; return 1; }
  cpu_stats get_sim_stats() const override { return {}; }
  cpu_stats get_roi_stats() const override { return {}; }
  int consumer_id() const override { return 0; }
  bool source_eof() const override { return false; }
};
static champsim::modules::core_module::register_module<mock_core_915> core_reg_915("MOCK_CORE_915");

struct mock_env_915 : public champsim::modules::environment_module {
  std::vector<champsim::operable*> operables_;
  std::vector<mock_core_915*> cores_;
  explicit mock_env_915(champsim::modules::ModuleBuilder) {}
  std::vector<std::any> view(const std::string& interface_type) const override {
    std::vector<std::any> result;
    if (interface_type == "operable") {
      for (auto* op : operables_) result.push_back(op);
      for (auto* c : cores_) result.push_back(static_cast<champsim::operable*>(static_cast<champsim::modules::core_module*>(c)));
    } else if (interface_type == "source_consumer") {
      for (auto* c : cores_) result.push_back(static_cast<champsim::modules::source_consumer*>(static_cast<champsim::modules::core_module*>(c)));
    }
    return result;
  }
  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override {
    return champsim::modules::ModuleBuilder();
  }
};
static champsim::modules::environment_module::register_module<mock_env_915> env_reg_915("MOCK_ENV_915");

} // namespace

TEST_CASE("A phase controller's config can declare non-warmup phases outside the ROI")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_roi_cfg", "MOCK_ENV_915");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));

  auto phases_json = nlohmann::json::array({
      nlohmann::json{{"name", "Warmup"}, {"is_warmup", true}, {"length", 10}},
      nlohmann::json{{"name", "FastForward"}, {"is_warmup", false}, {"roi", false}, {"length", 20}},
      nlohmann::json{{"name", "Measured"}, {"is_warmup", false}, {"length", 30}},
  });
  auto pc_builder = champsim::modules::ModuleBuilder("pc_roi_cfg", "PHASE_CONTROLLER")
    .add_parameter("phases", phases_json);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  auto phases = pc->get_phases();
  REQUIRE(phases.size() == 3);
  // Warmup: roi defaults to !is_warmup
  REQUIRE(phases[0].is_warmup);
  REQUIRE_FALSE(phases[0].roi);
  // Fast-forward: non-warmup, explicitly unmeasured
  REQUIRE_FALSE(phases[1].is_warmup);
  REQUIRE_FALSE(phases[1].roi);
  // Measured: roi defaults on
  REQUIRE_FALSE(phases[2].is_warmup);
  REQUIRE(phases[2].roi);
}

TEST_CASE("run_phase forwards the roi flag to module phase hooks")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_roi_run", "MOCK_ENV_915");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_915*>(env);

  auto core_builder = champsim::modules::ModuleBuilder("core_roi_run", "MOCK_CORE_915");
  auto* core = dynamic_cast<mock_core_915*>(champsim::modules::core_module::create_instance(core_builder, env));
  mock_env->cores_ = {core};

  phase_probe_915 probe;
  mock_env->operables_ = {&probe};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_roi_run", "PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);
  std::vector<std::reference_wrapper<champsim::modules::phase_controller>> controllers{std::ref(*pc)};

  champsim::chrono::clock clock;
  champsim::run_phase("FastForward", false, false, 5, *env, controllers, clock, {});
  champsim::run_phase("Measured", false, true, 5, *env, controllers, clock, {});

  REQUIRE(probe.begins.size() == 2);
  // An unmeasured non-warmup phase: neither warmup nor roi
  REQUIRE(probe.begins[0] == std::pair{false, false});
  REQUIRE(probe.begins[1] == std::pair{false, true});
}
