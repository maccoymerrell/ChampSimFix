#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>
#include <vector>

#include "chrono.h"
#include "modules.h"
#include "operable.h"

// Forward declare the generic driver functions from champsim.cc
namespace champsim {
long do_cycle(modules::environment_module& env, champsim::chrono::clock& global_clock);
void run_phase(const std::string& phase_name, bool is_warmup, uint64_t length,
               modules::environment_module& env, modules::phase_controller& controller,
               champsim::chrono::clock& global_clock,
               std::function<bool()> per_cycle_hook,
               std::function<void(unsigned)> on_entity_complete);
}

namespace {

// A trivial operable that counts how many times it was operated
struct counting_operable : public champsim::operable {
  long op_count = 0;
  bool phase_begun = false;
  std::vector<unsigned> ended_phases;

  counting_operable() : champsim::operable(champsim::chrono::picoseconds{250}) {}

  long operate() override {
    ++op_count;
    return 1; // always make progress
  }

  void begin_phase() override { phase_begun = true; }
  void end_phase(unsigned idx) override { ended_phases.push_back(idx); }
};

// Mock core for the phase controller (which requires cores to check instruction count)
struct mock_core_911 : public champsim::modules::core_module {
  uint64_t instr_count = 0;
  uint8_t cpu_num_ = 0;

  explicit mock_core_911(champsim::modules::ModuleBuilder builder)
    : core_module(champsim::chrono::picoseconds{250}) {
    cpu_num_ = builder.get_parameter<uint8_t>("cpu_num", true, uint8_t{0});
  }

  void push_instruction(ooo_model_instr) override {}
  std::size_t instructions_requested() override { return 0; }
  uint64_t sim_instr() const override { return instr_count; }
  uint8_t get_cpu_num() const override { return cpu_num_; }
  uint64_t sim_cycle() const override { return 0; }
  long operate() override { return 1; }
  cpu_stats get_sim_stats() const override { return {}; }
  cpu_stats get_roi_stats() const override { return {}; }
  void quiet(bool) override {}
};

static champsim::modules::core_module::register_module<mock_core_911> mock_core_reg_911("MOCK_CORE_911");

// Mock environment that tracks custom operables AND mock cores
struct mock_env_911 : public champsim::modules::environment_module {
  std::vector<champsim::operable*> operables_;
  std::vector<mock_core_911*> cores_;

  explicit mock_env_911(champsim::modules::ModuleBuilder) {}

  std::vector<std::any> view(const std::string& interface_type) const override {
    std::vector<std::any> result;
    if (interface_type == "operable") {
      for (auto* op : operables_) result.push_back(op);
      for (auto* c : cores_) result.push_back(static_cast<champsim::operable*>(static_cast<champsim::modules::core_module*>(c)));
    } else if (interface_type == "core") {
      for (auto* c : cores_) result.push_back(static_cast<champsim::modules::core_module*>(c));
    } else if (interface_type == "source_consumer") {
      for (auto* c : cores_) result.push_back(static_cast<champsim::modules::source_consumer*>(static_cast<champsim::modules::core_module*>(c)));
    }
    return result;
  }



  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override {
    return champsim::modules::ModuleBuilder();
  }
};

static champsim::modules::environment_module::register_module<mock_env_911> mock_env_reg_911("MOCK_ENV_911");

} // anonymous namespace

TEST_CASE("do_cycle operates all operables and returns progress")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_dc", "MOCK_ENV_911");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_911*>(env);

  counting_operable op1, op2;
  mock_env->operables_ = {&op1, &op2};

  champsim::chrono::clock clock;
  clock.tick(champsim::chrono::picoseconds{250});

  auto progress = champsim::do_cycle(*env, clock);

  REQUIRE(progress == 2);
  REQUIRE(op1.op_count == 1);
  REQUIRE(op2.op_count == 1);
}

TEST_CASE("run_phase calls per_cycle_hook each cycle")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_hook", "MOCK_ENV_911");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_911*>(env);

  // Need a mock core for the phase controller
  auto core_builder = champsim::modules::ModuleBuilder("core_hook0", "MOCK_CORE_911")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core = champsim::modules::core_module::create_instance(core_builder, env);
  auto* mc = dynamic_cast<mock_core_911*>(core);
  mock_env->cores_ = {mc};

  counting_operable custom_op;
  mock_env->operables_ = {&custom_op};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_hook", "instruction_phase_controller")
    .add_parameter("deadlock_cycles", 1000);
  auto* controller = champsim::modules::phase_controller::create_instance(pc_builder, env);

  int hook_calls = 0;
  auto per_cycle = [&]() -> bool {
    ++hook_calls;
    // After 5 cycles, signal completion by setting instr count
    if (hook_calls >= 5) {
      mc->instr_count = 100;
    }
    return false; // no EOF
  };

  std::vector<unsigned> completed;
  auto on_complete = [&](unsigned idx) {
    completed.push_back(idx);
  };

  champsim::chrono::clock clock;
  champsim::run_phase("HookTest", false, 100, *env, *controller, clock, per_cycle, on_complete);

  REQUIRE(hook_calls >= 5);
  REQUIRE(completed.size() == 1);
  REQUIRE(completed[0] == 0);
  REQUIRE(custom_op.phase_begun);
}

TEST_CASE("run_phase works with nullptr callbacks")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_null", "MOCK_ENV_911");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_911*>(env);

  auto core_builder = champsim::modules::ModuleBuilder("core_null0", "MOCK_CORE_911")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core = champsim::modules::core_module::create_instance(core_builder, env);
  auto* mc = dynamic_cast<mock_core_911*>(core);
  mc->instr_count = 0;
  mock_env->cores_ = {mc};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_null", "instruction_phase_controller")
    .add_parameter("deadlock_cycles", 1000);
  auto* controller = champsim::modules::phase_controller::create_instance(pc_builder, env);

  champsim::chrono::clock clock;

  // Should not crash with empty std::functions; length=0 completes on first advance
  REQUIRE_NOTHROW(champsim::run_phase("NullTest", false, 0, *env, *controller, clock, {}, {}));
}

TEST_CASE("run_phase stops on trace EOF via per_cycle_hook")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_eof2", "MOCK_ENV_911");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_911*>(env);

  auto core_builder = champsim::modules::ModuleBuilder("core_eof2_0", "MOCK_CORE_911")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core = champsim::modules::core_module::create_instance(core_builder, env);
  auto* mc = dynamic_cast<mock_core_911*>(core);
  mock_env->cores_ = {mc};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_eof2", "instruction_phase_controller")
    .add_parameter("deadlock_cycles", 1000);
  auto* controller = champsim::modules::phase_controller::create_instance(pc_builder, env);

  int cycle = 0;
  auto per_cycle = [&]() -> bool {
    ++cycle;
    return cycle >= 3; // signal EOF after 3 cycles
  };

  std::vector<unsigned> completed;
  auto on_complete = [&](unsigned idx) { completed.push_back(idx); };

  champsim::chrono::clock clock;
  champsim::run_phase("EOFTest", false, 999999, *env, *controller, clock, per_cycle, on_complete);

  // Phase should have completed due to EOF
  REQUIRE(completed.size() == 1);
  REQUIRE(completed[0] == 0);
}
