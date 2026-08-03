#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <string>
#include <any>
#include <map>
#include <functional>

#include "modules.h"
#include "phase_controller.h"
#include "operable.h"

namespace {

// Mock core with controllable instruction count and EOF state
struct mock_core : public champsim::modules::core_module {
  uint64_t instr_count = 0;
  uint64_t cycle_count = 0;
  uint8_t cpu_num_ = 0;
  bool producers_eof_ = false; // a live core with attached sources is not at EOF

  explicit mock_core(champsim::modules::ModuleBuilder builder)
    : core_module(champsim::chrono::picoseconds{250}) {
    cpu_num_ = builder.get_parameter<uint8_t>("cpu_num", true, uint8_t{0});
    set_consumer_id(static_cast<int>(cpu_num_));
  }

  void push_instruction(ooo_model_instr) override {}
  std::size_t instructions_requested() override { return 0; }
  uint64_t sim_instr() const override { return instr_count; }
  uint64_t sim_cycle() const override { return cycle_count; }
  long operate() override { return 0; }
  cpu_stats get_sim_stats() const override { return {}; }
  cpu_stats get_roi_stats() const override { return {}; }
  bool producers_eof() const override { return producers_eof_; }
};

static champsim::modules::core_module::register_module<mock_core> mock_core_reg_910("MOCK_CORE_910");

// Mock environment that returns controllable mock cores
struct mock_environment : public champsim::modules::environment_module {
  std::vector<mock_core*> cores_;
  int deadlock_cycles_ = 500;

  explicit mock_environment(champsim::modules::ModuleBuilder) {}

  std::vector<std::any> view(const std::string& interface_type) const override {
    std::vector<std::any> result;
    if (interface_type == "core") {
      for (auto* c : cores_) {
        result.push_back(static_cast<champsim::modules::core_module*>(c));
      }
    } else if (interface_type == "operable") {
      for (auto* c : cores_) {
        result.push_back(static_cast<champsim::operable*>(static_cast<champsim::modules::core_module*>(c)));
      }
    } else if (interface_type == "packet_consumer") {
      for (auto* c : cores_) {
        result.push_back(static_cast<champsim::modules::packet_consumer*>(static_cast<champsim::modules::core_module*>(c)));
      }
    }
    return result;
  }

  int get_deadlock_cycles() const override { return deadlock_cycles_; }

  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override {
    return champsim::modules::ModuleBuilder();
  }
};

static champsim::modules::environment_module::register_module<mock_environment> mock_env_reg_910("MOCK_ENV_910");

} // anonymous namespace

TEST_CASE("Phase controller completes when all cores reach instruction count")
{
  auto env_builder = champsim::modules::ModuleBuilder("test_env", "MOCK_ENV_910");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_environment*>(env);

  auto core0_builder = champsim::modules::ModuleBuilder("core0", "MOCK_CORE_910")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core0 = champsim::modules::core_module::create_instance(core0_builder, env);
  auto* mc0 = dynamic_cast<mock_core*>(core0);

  auto core1_builder = champsim::modules::ModuleBuilder("core1", "MOCK_CORE_910")
    .add_parameter("cpu_num", uint8_t{1});
  auto* core1 = champsim::modules::core_module::create_instance(core1_builder, env);
  auto* mc1 = dynamic_cast<mock_core*>(core1);

  mock_env->cores_ = {mc0, mc1};

  auto pc_builder = champsim::modules::ModuleBuilder("pc", "INSTRUCTION_PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  pc->begin_phase("Test", false, 100);

  // Neither core has completed
  auto s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);
  REQUIRE(pc->newly_completed_sources().empty());

  // Core 0 reaches target
  mc0->instr_count = 100;
  s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);
  auto completed = pc->newly_completed_sources();
  REQUIRE(completed.size() == 1);
  REQUIRE(completed[0] == 0);

  // Core 0 should not re-fire
  s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);
  REQUIRE(pc->newly_completed_sources().empty());

  // Core 1 reaches target -> phase complete
  mc1->instr_count = 100;
  s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::COMPLETE);
  completed = pc->newly_completed_sources();
  REQUIRE(completed.size() == 1);
  REQUIRE(completed[0] == 1);

  pc->end_phase();
}

TEST_CASE("Phase controller detects deadlock on stalled cycles")
{
  auto env_builder = champsim::modules::ModuleBuilder("test_env_dl", "MOCK_ENV_910");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_environment*>(env);

  auto core0_builder = champsim::modules::ModuleBuilder("core_dl0", "MOCK_CORE_910")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core0 = champsim::modules::core_module::create_instance(core0_builder, env);
  auto* mc0 = dynamic_cast<mock_core*>(core0);
  mock_env->cores_ = {mc0};

  // Use a small deadlock cycle limit for testing
  auto pc_builder = champsim::modules::ModuleBuilder("pc_dl", "INSTRUCTION_PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 5);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  pc->begin_phase("Deadlock Test", false, 1000);

  // Progress > 0: no deadlock
  for (int i = 0; i < 10; ++i) {
    auto s = pc->advance(1);
    REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);
  }

  // 4 stalled cycles: still CONTINUE
  for (int i = 0; i < 4; ++i) {
    auto s = pc->advance(0);
    REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);
  }

  // 5th stalled cycle: ABORT
  auto s = pc->advance(0);
  REQUIRE(s == champsim::modules::phase_controller::status::ABORT);

  pc->end_phase();
}

TEST_CASE("Phase controller completes all sources when one source hits EOF (complete_all)")
{
  auto env_builder = champsim::modules::ModuleBuilder("test_env_eof", "MOCK_ENV_910");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_environment*>(env);

  auto core0_builder = champsim::modules::ModuleBuilder("core_eof0", "MOCK_CORE_910")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core0 = champsim::modules::core_module::create_instance(core0_builder, env);
  auto* mc0 = dynamic_cast<mock_core*>(core0);

  auto core1_builder = champsim::modules::ModuleBuilder("core_eof1", "MOCK_CORE_910")
    .add_parameter("cpu_num", uint8_t{1});
  auto* core1 = champsim::modules::core_module::create_instance(core1_builder, env);
  auto* mc1 = dynamic_cast<mock_core*>(core1);

  mock_env->cores_ = {mc0, mc1};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_eof", "INSTRUCTION_PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  pc->begin_phase("EOF Test", false, 1000000);

  auto s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);

  // Core 0's sources reach EOF: default policy ends the phase for everyone
  mc0->producers_eof_ = true;
  s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::COMPLETE);
  auto completed = pc->newly_completed_sources();
  REQUIRE(completed.size() == 2);

  pc->end_phase();
}

TEST_CASE("Phase controller completes only the exhausted source under complete_source policy")
{
  auto env_builder = champsim::modules::ModuleBuilder("test_env_eofs", "MOCK_ENV_910");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_environment*>(env);

  auto core0_builder = champsim::modules::ModuleBuilder("core_eofs0", "MOCK_CORE_910")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core0 = champsim::modules::core_module::create_instance(core0_builder, env);
  auto* mc0 = dynamic_cast<mock_core*>(core0);

  auto core1_builder = champsim::modules::ModuleBuilder("core_eofs1", "MOCK_CORE_910")
    .add_parameter("cpu_num", uint8_t{1});
  auto* core1 = champsim::modules::core_module::create_instance(core1_builder, env);
  auto* mc1 = dynamic_cast<mock_core*>(core1);

  mock_env->cores_ = {mc0, mc1};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_eofs", "INSTRUCTION_PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000)
    .add_parameter("eof_policy", std::string{"complete_source"});
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  pc->begin_phase("EOF Source Test", false, 100);

  // Core 0's sources reach EOF: only source 0 completes
  mc0->producers_eof_ = true;
  auto s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);
  auto completed = pc->newly_completed_sources();
  REQUIRE(completed.size() == 1);
  REQUIRE(completed[0] == 0);

  // Core 1 finishes by progress: phase completes
  mc1->instr_count = 100;
  s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::COMPLETE);
  completed = pc->newly_completed_sources();
  REQUIRE(completed.size() == 1);
  REQUIRE(completed[0] == 1);

  pc->end_phase();
}

TEST_CASE("Phase controller defers deadlock while a consumer reports pending work")
{
  // A consumer that reports scheduled future work (like a paced source's gap)
  struct pending_core : mock_core {
    using mock_core::mock_core;
    bool pending_ = false;
    bool has_pending_work() const override { return pending_; }
  };

  auto env_builder = champsim::modules::ModuleBuilder("test_env_pend", "MOCK_ENV_910");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_environment*>(env);

  static champsim::modules::core_module::register_module<pending_core> pending_core_reg("PENDING_CORE_910");
  auto core0_builder = champsim::modules::ModuleBuilder("core_pend0", "PENDING_CORE_910")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core0 = champsim::modules::core_module::create_instance(core0_builder, env);
  auto* mc0 = dynamic_cast<pending_core*>(core0);
  mock_env->cores_ = {mc0};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_pend", "PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 5);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  pc->begin_phase("Pending Test", false, 1000);

  // Zero progress but pending work scheduled: never aborts
  mc0->pending_ = true;
  for (int i = 0; i < 20; ++i) {
    auto s = pc->advance(0);
    REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);
  }

  // Pending work gone: the stall is real and the abort fires
  mc0->pending_ = false;
  champsim::modules::phase_controller::status s{};
  for (int i = 0; i < 5; ++i) {
    s = pc->advance(0);
  }
  REQUIRE(s == champsim::modules::phase_controller::status::ABORT);

  pc->end_phase();
}

TEST_CASE("Phase controller resets state between phases")
{
  auto env_builder = champsim::modules::ModuleBuilder("test_env_reset", "MOCK_ENV_910");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_environment*>(env);

  auto core0_builder = champsim::modules::ModuleBuilder("core_reset0", "MOCK_CORE_910")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core0 = champsim::modules::core_module::create_instance(core0_builder, env);
  auto* mc0 = dynamic_cast<mock_core*>(core0);
  mock_env->cores_ = {mc0};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_reset", "INSTRUCTION_PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000);
  auto* pc = champsim::modules::phase_controller::create_instance(pc_builder, env);

  // Phase 1: complete immediately — progress reaches the phase length on first advance
  mc0->instr_count = 0;
  pc->begin_phase("Phase1", true, 100);
  mc0->instr_count = 100;  // delta = 100 - baseline(0) = 100 >= length(100) → COMPLETE
  auto s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::COMPLETE);
  pc->end_phase();

  // Phase 2: baseline = 100 (instr_count at Phase 2 start), length = 200
  pc->begin_phase("Phase2", false, 200);
  s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::CONTINUE);

  mc0->instr_count = 300;  // delta = 300 - baseline(100) = 200 >= length(200) → COMPLETE
  s = pc->advance(1);
  REQUIRE(s == champsim::modules::phase_controller::status::COMPLETE);
  pc->end_phase();
}
