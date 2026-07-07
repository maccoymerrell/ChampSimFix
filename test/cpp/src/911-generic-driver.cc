#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "chrono.h"
#include "modules.h"
#include "operable.h"

// Forward declare the generic driver functions from champsim.cc
namespace champsim {
long do_cycle(std::vector<std::reference_wrapper<champsim::operable>>& operables, champsim::chrono::clock& global_clock);
void run_phase(const std::string& phase_name, bool is_warmup, bool roi, uint64_t length,
               modules::environment_module& env,
               std::vector<std::reference_wrapper<modules::phase_controller>>& controllers,
               champsim::chrono::clock& global_clock,
               std::function<void(unsigned)> on_source_complete);

// Convenience for tests: drive a single controller through the multi-controller loop
inline void run_phase(const std::string& phase_name, bool is_warmup, uint64_t length,
                      modules::environment_module& env, modules::phase_controller& controller,
                      champsim::chrono::clock& global_clock,
                      std::function<void(unsigned)> on_source_complete)
{
  std::vector<std::reference_wrapper<modules::phase_controller>> controllers{std::ref(controller)};
  run_phase(phase_name, is_warmup, !is_warmup, length, env, controllers, global_clock, std::move(on_source_complete));
}
}

namespace {

// A trivial operable that counts how many times it was operated
struct counting_operable : public champsim::operable, public champsim::module_phase {
  long op_count = 0;
  bool phase_begun = false;
  std::vector<unsigned> ended_phases;

  counting_operable() : champsim::operable(champsim::chrono::picoseconds{250}) {}

  long operate() override {
    ++op_count;
    return 1; // always make progress
  }

  void begin_phase(bool, bool) override { phase_begun = true; }
  void end_phase() override { ended_phases.push_back(0); }
};

// Mock core for the phase controller. Its instruction count advances one per
// operated cycle, so phases complete naturally through the run_phase loop.
struct mock_core_911 : public champsim::modules::core_module {
  uint64_t instr_count = 0;
  uint8_t cpu_num_ = 0;
  bool source_eof_ = false; // a live core with attached sources is not at EOF

  explicit mock_core_911(champsim::modules::ModuleBuilder builder)
    : core_module(champsim::chrono::picoseconds{250}) {
    cpu_num_ = builder.get_parameter<uint8_t>("cpu_num", true, uint8_t{0});
    set_consumer_id(static_cast<int>(cpu_num_));
  }

  void push_instruction(ooo_model_instr) override {}
  std::size_t instructions_requested() override { return 0; }
  uint64_t sim_instr() const override { return instr_count; }
  uint64_t sim_cycle() const override { return 0; }
  long operate() override { ++instr_count; return 1; }
  cpu_stats get_sim_stats() const override { return {}; }
  cpu_stats get_roi_stats() const override { return {}; }
  bool source_eof() const override { return source_eof_; }
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

  auto operables = env->typed_view<champsim::operable>("operable");
  auto progress = champsim::do_cycle(operables, clock);

  REQUIRE(progress == 2);
  REQUIRE(op1.op_count == 1);
  REQUIRE(op2.op_count == 1);
}

TEST_CASE("run_phase completes when the source reaches the phase length")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_hook", "MOCK_ENV_911");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_911*>(env);

  // The mock core retires one instruction per cycle
  auto core_builder = champsim::modules::ModuleBuilder("core_hook0", "MOCK_CORE_911")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core = champsim::modules::core_module::create_instance(core_builder, env);
  auto* mc = dynamic_cast<mock_core_911*>(core);
  mock_env->cores_ = {mc};

  counting_operable custom_op;
  mock_env->operables_ = {&custom_op};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_hook", "PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000);
  auto* controller = champsim::modules::phase_controller::create_instance(pc_builder, env);

  std::vector<unsigned> completed;
  auto on_complete = [&](unsigned idx) {
    completed.push_back(idx);
  };

  champsim::chrono::clock clock;
  champsim::run_phase("LengthTest", false, 5, *env, *controller, clock, on_complete);

  REQUIRE(mc->instr_count >= 5);
  REQUIRE(completed.size() == 1);
  REQUIRE(completed[0] == 0);
  REQUIRE(custom_op.phase_begun);
  REQUIRE(custom_op.op_count >= 5);
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
  mock_env->cores_ = {mc};

  auto pc_builder = champsim::modules::ModuleBuilder("pc_null", "INSTRUCTION_PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000);
  auto* controller = champsim::modules::phase_controller::create_instance(pc_builder, env);

  champsim::chrono::clock clock;

  // Should not crash with an empty std::function; length=0 completes on first advance
  REQUIRE_NOTHROW(champsim::run_phase("NullTest", false, 0, *env, *controller, clock, {}));
}

TEST_CASE("run_phase drives multiple controllers, each governing its own sources")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_multi", "MOCK_ENV_911");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_911*>(env);

  // Two cores; core 0 retires 1/cycle, core 1 retires 1/cycle too, but the
  // controllers give them different completion lengths.
  auto core0_builder = champsim::modules::ModuleBuilder("core_multi0", "MOCK_CORE_911")
    .add_parameter("cpu_num", uint8_t{0});
  auto* mc0 = dynamic_cast<mock_core_911*>(champsim::modules::core_module::create_instance(core0_builder, env));
  auto core1_builder = champsim::modules::ModuleBuilder("core_multi1", "MOCK_CORE_911")
    .add_parameter("cpu_num", uint8_t{1});
  auto* mc1 = dynamic_cast<mock_core_911*>(champsim::modules::core_module::create_instance(core1_builder, env));
  mock_env->cores_ = {mc0, mc1};

  // Controller A governs source 0 only; controller B governs source 1 only.
  auto pcA_builder = champsim::modules::ModuleBuilder("pc_multiA", "PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000)
    .add_parameter("sources", nlohmann::json::array({0}));
  auto* pcA = champsim::modules::phase_controller::create_instance(pcA_builder, env);
  auto pcB_builder = champsim::modules::ModuleBuilder("pc_multiB", "PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000)
    .add_parameter("sources", nlohmann::json::array({1}));
  auto* pcB = champsim::modules::phase_controller::create_instance(pcB_builder, env);

  std::vector<std::reference_wrapper<champsim::modules::phase_controller>> controllers{std::ref(*pcA), std::ref(*pcB)};

  std::vector<unsigned> completed;
  auto on_complete = [&](unsigned idx) { completed.push_back(idx); };

  champsim::chrono::clock clock;
  champsim::run_phase("MultiTest", false, true, 7, *env, controllers, clock, on_complete);

  // Both sources complete exactly once, and the phase ends only when both
  // controllers agree.
  REQUIRE(completed.size() == 2);
  REQUIRE(mc0->instr_count >= 7);
  REQUIRE(mc1->instr_count >= 7);
}

TEST_CASE("run_phase stops when a source reaches EOF")
{
  auto env_builder = champsim::modules::ModuleBuilder("env_eof2", "MOCK_ENV_911");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));
  auto* mock_env = dynamic_cast<mock_env_911*>(env);

  auto core_builder = champsim::modules::ModuleBuilder("core_eof2_0", "MOCK_CORE_911")
    .add_parameter("cpu_num", uint8_t{0});
  auto* core = champsim::modules::core_module::create_instance(core_builder, env);
  auto* mc = dynamic_cast<mock_core_911*>(core);
  mock_env->cores_ = {mc};

  // The source is exhausted from the start; the controller observes it
  // through source_eof() with no external notification.
  mc->source_eof_ = true;

  auto pc_builder = champsim::modules::ModuleBuilder("pc_eof2", "INSTRUCTION_PHASE_CONTROLLER")
    .add_parameter("deadlock_cycles", 1000);
  auto* controller = champsim::modules::phase_controller::create_instance(pc_builder, env);

  std::vector<unsigned> completed;
  auto on_complete = [&](unsigned idx) { completed.push_back(idx); };

  champsim::chrono::clock clock;
  champsim::run_phase("EOFTest", false, 999999, *env, *controller, clock, on_complete);

  // Phase should have completed due to EOF
  REQUIRE(completed.size() == 1);
  REQUIRE(completed[0] == 0);
}
