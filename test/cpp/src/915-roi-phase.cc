#include <catch.hpp>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

#include "chrono.h"
#include "modules.h"
#include "phase_controller.h"
#include "operable.h"
#include "phase_info.h"

namespace
{

// Records the (warmup, roi) pair its phase hooks observe.
struct phase_probe_915 : public champsim::operable {
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
  bool producers_eof() const override { return false; }
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
    } else if (interface_type == "module_lifecycle") {
      for (auto* op : operables_) result.push_back(static_cast<champsim::module_lifecycle*>(op));
      for (auto* c : cores_)
        result.push_back(static_cast<champsim::module_lifecycle*>(static_cast<champsim::operable*>(static_cast<champsim::modules::core_module*>(c))));
    } else if (interface_type == "packet_consumer") {
      for (auto* c : cores_) result.push_back(static_cast<champsim::modules::packet_consumer*>(static_cast<champsim::modules::core_module*>(c)));
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
  using status = champsim::modules::phase_controller::status;

  auto env_builder = champsim::modules::ModuleBuilder("env_roi_cfg", "MOCK_ENV_915");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_915*>(env);

  auto core_builder = champsim::modules::ModuleBuilder("core_roi_cfg", "MOCK_CORE_915");
  auto* core = dynamic_cast<mock_core_915*>(champsim::modules::core_module::create_instance(core_builder, env));
  mock_env->cores_ = {core};

  auto phases_json = nlohmann::json::array({
      nlohmann::json{{"name", "Warmup"}, {"is_warmup", true}, {"length", 10}},
      nlohmann::json{{"name", "FastForward"}, {"is_warmup", false}, {"roi", false}, {"length", 20}},
      nlohmann::json{{"name", "Measured"}, {"is_warmup", false}, {"length", 30}},
  });
  auto pc_builder = champsim::modules::ModuleBuilder("pc_roi_cfg", "PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000)
    .add_parameter("phases", phases_json);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  // The controller owns its phases; stepping through them exposes each phase's parsed roi flag.
  pc->advance(0);
  REQUIRE(pc->phase().name == "Warmup");
  REQUIRE(pc->phase().is_warmup); // roi defaults to !is_warmup
  REQUIRE_FALSE(pc->phase().roi);
  core->instr_count = 10;
  REQUIRE(pc->advance(1) == status::COMPLETE);

  pc->advance(0);
  REQUIRE(pc->phase().name == "FastForward");
  REQUIRE_FALSE(pc->phase().is_warmup); // non-warmup, explicitly unmeasured
  REQUIRE_FALSE(pc->phase().roi);
  core->instr_count = 30;
  REQUIRE(pc->advance(1) == status::COMPLETE);

  pc->advance(0);
  REQUIRE(pc->phase().name == "Measured");
  REQUIRE_FALSE(pc->phase().is_warmup); // roi defaults on
  REQUIRE(pc->phase().roi);
  core->instr_count = 60;
  REQUIRE(pc->advance(1) == status::COMPLETE); // the final phase ends
  REQUIRE(pc->advance(0) == status::DONE);     // no phases left
}

TEST_CASE("The roi flag is forwarded to module phase begin hooks")
{
  using status = champsim::modules::phase_controller::status;

  auto env_builder = champsim::modules::ModuleBuilder("env_roi_run", "MOCK_ENV_915");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_915*>(env);

  auto core_builder = champsim::modules::ModuleBuilder("core_roi_run", "MOCK_CORE_915");
  auto* core = dynamic_cast<mock_core_915*>(champsim::modules::core_module::create_instance(core_builder, env));
  mock_env->cores_ = {core};

  phase_probe_915 probe;
  mock_env->operables_ = {&probe};

  // Two non-warmup phases, one unmeasured and one measured, owned by the controller.
  auto phases_json = nlohmann::json::array({
      nlohmann::json{{"name", "FastForward"}, {"is_warmup", false}, {"roi", false}, {"length", 5}},
      nlohmann::json{{"name", "Measured"}, {"is_warmup", false}, {"roi", true}, {"length", 5}},
  });
  auto pc_builder = champsim::modules::ModuleBuilder("pc_roi_run", "PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000)
    .add_parameter("phases", phases_json);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  pc->advance(0); // FastForward: begin_phase(false, false) on the probe
  core->instr_count = 5;
  REQUIRE(pc->advance(1) == status::COMPLETE);

  pc->advance(0); // Measured: begin_phase(false, true) on the probe
  core->instr_count = 10;
  REQUIRE(pc->advance(1) == status::COMPLETE); // the final phase ends
  REQUIRE(pc->advance(0) == status::DONE);     // no phases left

  REQUIRE(probe.begins.size() == 2);
  // An unmeasured non-warmup phase: neither warmup nor roi
  REQUIRE(probe.begins[0] == std::pair{false, false});
  REQUIRE(probe.begins[1] == std::pair{false, true});
}
