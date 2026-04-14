#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <vector>

#include "instr.h"
#include "modules.h"

namespace {

// A controllable workload source for testing
struct mock_source_913 : public champsim::modules::workload_source {
  std::deque<ooo_model_instr> instructions_;
  bool retire_called_ = false;
  bool squash_called_ = false;
  bool branch_mispredict_called_ = false;

  explicit mock_source_913(champsim::modules::ModuleBuilder) {}

  ooo_model_instr next_instruction() override {
    auto instr = instructions_.front();
    instructions_.pop_front();
    return instr;
  }

  [[nodiscard]] bool eof() const override { return instructions_.empty(); }

  void retire_instruction(const ooo_model_instr&) override { retire_called_ = true; }
  void squash_instruction(const ooo_model_instr&) override { squash_called_ = true; }
  void branch_mispredict(const ooo_model_instr&) override { branch_mispredict_called_ = true; }
};

static champsim::modules::workload_source::register_module<mock_source_913>
    mock_source_reg("MOCK_SOURCE_913");

// A minimal no-op source to test default hook behavior
struct noop_source_913 : public champsim::modules::workload_source {
  explicit noop_source_913(champsim::modules::ModuleBuilder) {}
  ooo_model_instr next_instruction() override { return champsim::test::instruction_with_ip(0); }
  [[nodiscard]] bool eof() const override { return true; }
  // No override of hooks — uses defaults
};

static champsim::modules::workload_source::register_module<noop_source_913>
    noop_source_reg("NOOP_SOURCE_913");

// Mock core that delegates source_eof to attached workload sources
struct mock_core_913 : public champsim::modules::core_module {
  std::vector<champsim::modules::workload_source*> sources_;
  std::vector<ooo_model_instr> received_;

  explicit mock_core_913(champsim::modules::ModuleBuilder builder)
    : core_module(champsim::chrono::picoseconds{250})
  {
    for (const auto& sub : builder.get_submodules("workload_source"))
      sources_.push_back(champsim::modules::workload_source::create_instance(sub, static_cast<champsim::modules::source_consumer*>(this)));
  }

  void push_instruction(ooo_model_instr instr) override { received_.push_back(instr); }
  std::size_t instructions_requested() override { return 4; }
  uint64_t sim_instr() const override { return 0; }
  uint8_t get_cpu_num() const override { return 0; }
  uint64_t sim_cycle() const override { return 0; }
  long operate() override { return 0; }
  cpu_stats get_sim_stats() const override { return {}; }
  cpu_stats get_roi_stats() const override { return {}; }
  void quiet(bool) override {}

  bool source_eof() const override {
    return std::all_of(sources_.begin(), sources_.end(),
                       [](const auto* s) { return s->eof(); });
  }

  void fill_from_sources() {
    for (auto* src : sources_) {
      for (auto space = instructions_requested(); !src->eof() && space > 0; --space)
        push_instruction(src->next_instruction());
    }
  }
};

static champsim::modules::core_module::register_module<mock_core_913>
    mock_core_reg("MOCK_CORE_913");

// Helper environment for creating modules
struct mock_env_913 : public champsim::modules::environment_module {
  explicit mock_env_913(champsim::modules::ModuleBuilder) {}
  std::vector<std::any> view(const std::string&) const override { return {}; }
  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override {
    return champsim::modules::ModuleBuilder();
  }
};

static champsim::modules::environment_module::register_module<mock_env_913>
    mock_env_reg("MOCK_ENV_913");

} // anonymous namespace

TEST_CASE("workload_source next_instruction returns queued instructions") {
  auto env_b = champsim::modules::ModuleBuilder("env_ws", "MOCK_ENV_913");
  auto* env = champsim::modules::environment_module::create_instance(env_b, static_cast<champsim::modules::environment_module*>(nullptr));

  auto core_b = champsim::modules::ModuleBuilder("core_ws", "MOCK_CORE_913");
  auto src_b = champsim::modules::ModuleBuilder("source_ws", "MOCK_SOURCE_913");
  core_b.add_submodule("workload_source", std::move(src_b));
  auto* core = champsim::modules::core_module::create_instance(core_b, env);
  auto* mc = dynamic_cast<mock_core_913*>(core);
  REQUIRE(mc != nullptr);
  REQUIRE(mc->sources_.size() == 1);

  auto* src = dynamic_cast<mock_source_913*>(mc->sources_.front());
  REQUIRE(src != nullptr);

  auto i1 = champsim::test::instruction_with_ip(0xDEAD);
  auto i2 = champsim::test::instruction_with_ip(0xBEEF);
  src->instructions_ = {i1, i2};

  REQUIRE_FALSE(src->eof());
  auto got1 = src->next_instruction();
  CHECK(got1.ip == champsim::address{0xDEAD});
  auto got2 = src->next_instruction();
  CHECK(got2.ip == champsim::address{0xBEEF});
  REQUIRE(src->eof());
}

TEST_CASE("workload_source eof is true when no instructions remain") {
  auto env_b = champsim::modules::ModuleBuilder("env_eof", "MOCK_ENV_913");
  auto* env = champsim::modules::environment_module::create_instance(env_b, static_cast<champsim::modules::environment_module*>(nullptr));

  auto core_b = champsim::modules::ModuleBuilder("core_eof", "MOCK_CORE_913");
  auto src_b = champsim::modules::ModuleBuilder("source_eof", "MOCK_SOURCE_913");
  core_b.add_submodule("workload_source", std::move(src_b));
  auto* core = champsim::modules::core_module::create_instance(core_b, env);
  auto* mc = dynamic_cast<mock_core_913*>(core);
  auto* src = dynamic_cast<mock_source_913*>(mc->sources_.front());

  // Empty source is at EOF
  REQUIRE(src->eof());
  // Add one instruction, no longer at EOF
  src->instructions_.push_back(champsim::test::instruction_with_ip(1));
  REQUIRE_FALSE(src->eof());
  // Consume it
  src->next_instruction();
  REQUIRE(src->eof());
}

TEST_CASE("core fill_from_sources pulls instructions into core") {
  auto env_b = champsim::modules::ModuleBuilder("env_fill", "MOCK_ENV_913");
  auto* env = champsim::modules::environment_module::create_instance(env_b, static_cast<champsim::modules::environment_module*>(nullptr));

  auto core_b = champsim::modules::ModuleBuilder("core_fill", "MOCK_CORE_913");
  auto src_b = champsim::modules::ModuleBuilder("source_fill", "MOCK_SOURCE_913");
  core_b.add_submodule("workload_source", std::move(src_b));
  auto* core = champsim::modules::core_module::create_instance(core_b, env);
  auto* mc = dynamic_cast<mock_core_913*>(core);
  auto* src = dynamic_cast<mock_source_913*>(mc->sources_.front());

  // Enqueue 3 instructions
  for (int i = 0; i < 3; ++i)
    src->instructions_.push_back(champsim::test::instruction_with_ip(0x1000 + i));

  mc->fill_from_sources();

  REQUIRE(mc->received_.size() == 3);
  CHECK(mc->received_[0].ip == champsim::address{0x1000});
  CHECK(mc->received_[1].ip == champsim::address{0x1001});
  CHECK(mc->received_[2].ip == champsim::address{0x1002});
  REQUIRE(src->eof());
}

TEST_CASE("core source_eof reflects source state") {
  auto env_b = champsim::modules::ModuleBuilder("env_seof", "MOCK_ENV_913");
  auto* env = champsim::modules::environment_module::create_instance(env_b, static_cast<champsim::modules::environment_module*>(nullptr));

  auto core_b = champsim::modules::ModuleBuilder("core_seof", "MOCK_CORE_913");
  auto src_b = champsim::modules::ModuleBuilder("source_seof", "MOCK_SOURCE_913");
  core_b.add_submodule("workload_source", std::move(src_b));
  auto* core = champsim::modules::core_module::create_instance(core_b, env);
  auto* mc = dynamic_cast<mock_core_913*>(core);
  auto* src = dynamic_cast<mock_source_913*>(mc->sources_.front());

  // Empty → source_eof true
  REQUIRE(mc->source_eof());

  // Add instruction → source_eof false
  src->instructions_.push_back(champsim::test::instruction_with_ip(1));
  REQUIRE_FALSE(mc->source_eof());

  // Consume → source_eof true again
  src->next_instruction();
  REQUIRE(mc->source_eof());
}

TEST_CASE("core with no workload sources has source_eof true") {
  auto env_b = champsim::modules::ModuleBuilder("env_nosrc", "MOCK_ENV_913");
  auto* env = champsim::modules::environment_module::create_instance(env_b, static_cast<champsim::modules::environment_module*>(nullptr));

  auto core_b = champsim::modules::ModuleBuilder("core_nosrc", "MOCK_CORE_913");
  // No workload_source submodule added
  auto* core = champsim::modules::core_module::create_instance(core_b, env);
  auto* mc = dynamic_cast<mock_core_913*>(core);

  REQUIRE(mc->sources_.empty());
  REQUIRE(mc->source_eof());
}

TEST_CASE("execution-driven hooks are callable without crash") {
  auto env_b = champsim::modules::ModuleBuilder("env_hooks", "MOCK_ENV_913");
  auto* env = champsim::modules::environment_module::create_instance(env_b, static_cast<champsim::modules::environment_module*>(nullptr));

  auto core_b = champsim::modules::ModuleBuilder("core_hooks", "MOCK_CORE_913");
  auto src_b = champsim::modules::ModuleBuilder("source_hooks", "MOCK_SOURCE_913");
  core_b.add_submodule("workload_source", std::move(src_b));
  auto* core = champsim::modules::core_module::create_instance(core_b, env);
  auto* mc = dynamic_cast<mock_core_913*>(core);
  auto* src = dynamic_cast<mock_source_913*>(mc->sources_.front());

  auto instr = champsim::test::instruction_with_ip(0x42);

  REQUIRE_FALSE(src->retire_called_);
  src->retire_instruction(instr);
  REQUIRE(src->retire_called_);

  REQUIRE_FALSE(src->squash_called_);
  src->squash_instruction(instr);
  REQUIRE(src->squash_called_);

  REQUIRE_FALSE(src->branch_mispredict_called_);
  src->branch_mispredict(instr);
  REQUIRE(src->branch_mispredict_called_);
}

TEST_CASE("default execution-driven hooks are no-ops") {
  auto env_b = champsim::modules::ModuleBuilder("env_noop", "MOCK_ENV_913");
  auto* env = champsim::modules::environment_module::create_instance(env_b, static_cast<champsim::modules::environment_module*>(nullptr));

  auto core_b = champsim::modules::ModuleBuilder("core_noop", "MOCK_CORE_913");
  auto src_b = champsim::modules::ModuleBuilder("source_noop", "NOOP_SOURCE_913");
  core_b.add_submodule("workload_source", std::move(src_b));
  auto* core = champsim::modules::core_module::create_instance(core_b, env);
  auto* mc = dynamic_cast<mock_core_913*>(core);
  auto* src = mc->sources_.front();

  auto instr = champsim::test::instruction_with_ip(0);
  // These should not crash — default implementations are no-ops
  REQUIRE_NOTHROW(src->retire_instruction(instr));
  REQUIRE_NOTHROW(src->squash_instruction(instr));
  REQUIRE_NOTHROW(src->branch_mispredict(instr));
  // Source with no instructions is also at EOF
  REQUIRE(src->eof());
}

TEST_CASE("fill_from_sources respects bandwidth limit") {
  auto env_b = champsim::modules::ModuleBuilder("env_bw", "MOCK_ENV_913");
  auto* env = champsim::modules::environment_module::create_instance(env_b, static_cast<champsim::modules::environment_module*>(nullptr));

  auto core_b = champsim::modules::ModuleBuilder("core_bw", "MOCK_CORE_913");
  auto src_b = champsim::modules::ModuleBuilder("source_bw", "MOCK_SOURCE_913");
  core_b.add_submodule("workload_source", std::move(src_b));
  auto* core = champsim::modules::core_module::create_instance(core_b, env);
  auto* mc = dynamic_cast<mock_core_913*>(core);
  auto* src = dynamic_cast<mock_source_913*>(mc->sources_.front());

  // Enqueue more instructions than mock bandwidth allows (instructions_requested returns 4)
  for (int i = 0; i < 10; ++i)
    src->instructions_.push_back(champsim::test::instruction_with_ip(i));

  mc->fill_from_sources();

  // Should only pull 4 (the bandwidth limit)
  REQUIRE(mc->received_.size() == 4);
  // 6 should remain in the source
  REQUIRE(src->instructions_.size() == 6);
  REQUIRE_FALSE(src->eof());
}
