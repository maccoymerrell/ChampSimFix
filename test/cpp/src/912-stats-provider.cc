#include <catch2/catch_test_macros.hpp>
#include <any>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "cache_stats.h"
#include "core_stats.h"
#include "dram_stats.h"
#include "modules.h"
#include "phase_info.h"

namespace {

// A test core whose stats are controllable
struct stats_core : public champsim::modules::core_module {
  cpu_stats sim_stats_{};
  cpu_stats roi_stats_{};

  explicit stats_core(champsim::modules::ModuleBuilder builder)
    : core_module(champsim::chrono::picoseconds{250}) {}

  void push_instruction(ooo_model_instr) override {}
  std::size_t instructions_requested() override { return 0; }
  uint64_t sim_instr() const override { return 100; }
  uint8_t get_cpu_num() const override { return 0; }
  uint64_t sim_cycle() const override { return 200; }
  long operate() override { return 0; }
  cpu_stats get_sim_stats() const override { return sim_stats_; }
  cpu_stats get_roi_stats() const override { return roi_stats_; }
  void quiet(bool) override {}
};

static champsim::modules::core_module::register_module<stats_core> stats_core_reg("STATS_CORE_912");

// A test cache whose stats are controllable
struct stats_cache : public champsim::modules::cache_module {
  cache_stats sim_stats_{};
  cache_stats roi_stats_{};

  explicit stats_cache(champsim::modules::ModuleBuilder builder)
    : cache_module(champsim::chrono::picoseconds{250}) {}

  long operate() override { return 0; }
  champsim::bandwidth::maximum_type get_max_tag_bandwidth() const override { return {}; }
  cache_stats get_sim_stats() const override { return sim_stats_; }
  cache_stats get_roi_stats() const override { return roi_stats_; }
  bool is_virtual_prefetch() const override { return false; }
  bool prefetch_line(champsim::address, bool, uint32_t) override { return false; }
  void impl_update_replacement_state(uint32_t, long, long, champsim::address, champsim::address,
                                     champsim::address, access_type, bool) const override {}
  void impl_prefetcher_branch_operate(champsim::address, uint8_t, champsim::address) const override {}
  long invalidate_entry(champsim::address) override { return -1; }
  std::size_t get_mshr_occupancy() const override { return 0; }
  std::size_t get_mshr_size() const override { return 0; }
  double get_mshr_occupancy_ratio() const override { return 0; }
  std::vector<std::size_t> get_rq_occupancy() const override { return {}; }
  std::vector<std::size_t> get_rq_size() const override { return {}; }
  std::vector<double> get_rq_occupancy_ratio() const override { return {}; }
  std::vector<std::size_t> get_wq_occupancy() const override { return {}; }
  std::vector<std::size_t> get_wq_size() const override { return {}; }
  std::vector<double> get_wq_occupancy_ratio() const override { return {}; }
  std::vector<std::size_t> get_pq_occupancy() const override { return {}; }
  std::vector<std::size_t> get_pq_size() const override { return {}; }
  std::vector<double> get_pq_occupancy_ratio() const override { return {}; }
  std::size_t num_sets() const override { return 0; }
  std::size_t num_ways() const override { return 0; }
  champsim::data::bits get_offset_bits() const override { return champsim::data::bits{}; }
};

static champsim::modules::cache_module::register_module<stats_cache> stats_cache_reg("STATS_CACHE_912");

// A test memory controller with controllable per-channel stats
struct stats_dram : public champsim::modules::memory_controller_module {
  std::vector<dram_stats> sim_channel_stats_;
  std::vector<dram_stats> roi_channel_stats_;

  explicit stats_dram(champsim::modules::ModuleBuilder builder)
    : memory_controller_module(champsim::chrono::picoseconds{250}) {}

  long operate() override { return 0; }
  std::size_t get_num_channels() const override { return std::max(sim_channel_stats_.size(), roi_channel_stats_.size()); }
  dram_stats get_sim_stats(std::size_t channel_no) const override { return sim_channel_stats_.at(channel_no); }
  dram_stats get_roi_stats(std::size_t channel_no) const override { return roi_channel_stats_.at(channel_no); }
  champsim::data::bytes size() const override { return champsim::data::bytes{}; }
};

static champsim::modules::memory_controller_module::register_module<stats_dram> stats_dram_reg("STATS_DRAM_912");

// Mock environment for stats tests
struct stats_env : public champsim::modules::environment_module {
  std::vector<champsim::modules::core_module*> cores_;
  std::vector<champsim::modules::cache_module*> caches_;
  std::vector<champsim::modules::memory_controller_module*> drams_;

  explicit stats_env(champsim::modules::ModuleBuilder) {}

  std::vector<std::any> view(const std::string& interface_type) const override {
    std::vector<std::any> result;
    if (interface_type == "core") {
      for (auto* c : cores_) result.push_back(static_cast<champsim::modules::core_module*>(c));
    } else if (interface_type == "cache") {
      for (auto* c : caches_) result.push_back(static_cast<champsim::modules::cache_module*>(c));
    } else if (interface_type == "memory_controller") {
      for (auto* d : drams_) result.push_back(static_cast<champsim::modules::memory_controller_module*>(d));
    }
    return result;
  }


  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override {
    return champsim::modules::ModuleBuilder{};
  }
};

} // anonymous namespace

TEST_CASE("core_module give_stats returns formatted plaintext from format_plaintext") {
  auto builder = champsim::modules::ModuleBuilder("test_core", "STATS_CORE_912");
  stats_env env_dummy(champsim::modules::ModuleBuilder{});
  auto* core = champsim::modules::core_module::create_instance(builder, &env_dummy);

  auto* impl = static_cast<stats_core*>(core);
  impl->roi_stats_.name = "core0";
  impl->roi_stats_.begin_instrs = 0;
  impl->roi_stats_.end_instrs = 100;
  impl->roi_stats_.begin_cycles = 0;
  impl->roi_stats_.end_cycles = 50;

  auto lines = core->give_stats(true);
  REQUIRE(!lines.empty());
  // First line should contain IPC info
  REQUIRE(lines.front().find("core0 cumulative IPC") != std::string::npos);
}

TEST_CASE("core_module give_stats_json returns non-empty JSON") {
  auto builder = champsim::modules::ModuleBuilder("test_core_json", "STATS_CORE_912");
  stats_env env_dummy(champsim::modules::ModuleBuilder{});
  auto* core = champsim::modules::core_module::create_instance(builder, &env_dummy);

  auto* impl = static_cast<stats_core*>(core);
  impl->sim_stats_.name = "core0";
  impl->sim_stats_.begin_instrs = 0;
  impl->sim_stats_.end_instrs = 200;
  impl->sim_stats_.begin_cycles = 0;
  impl->sim_stats_.end_cycles = 100;

  auto result = core->give_stats_json(false);
  REQUIRE(result.has_value());
}

TEST_CASE("cache_module give_stats returns formatted plaintext") {
  auto builder = champsim::modules::ModuleBuilder("test_cache_912", "STATS_CACHE_912");
  stats_env env_dummy(champsim::modules::ModuleBuilder{});
  auto* cache = champsim::modules::cache_module::create_instance(builder, &env_dummy);

  auto* impl = static_cast<stats_cache*>(cache);
  impl->roi_stats_.name = "L1D";
  impl->roi_stats_.hits.set({access_type::LOAD, std::size_t{0}}, 100);

  auto lines = cache->give_stats(true);
  REQUIRE(!lines.empty());
  REQUIRE(lines.front().find("L1D") != std::string::npos);
}

TEST_CASE("memory_controller give_stats returns per-channel plaintext") {
  auto builder = champsim::modules::ModuleBuilder("test_dram_912", "STATS_DRAM_912");
  stats_env env_dummy(champsim::modules::ModuleBuilder{});
  auto* dram = champsim::modules::memory_controller_module::create_instance(builder, &env_dummy);

  auto* impl = static_cast<stats_dram*>(dram);
  dram_stats ch0{};
  ch0.name = "channel_0";
  ch0.RQ_ROW_BUFFER_HIT = 42;
  impl->roi_channel_stats_ = {ch0};

  auto lines = dram->give_stats(true);
  REQUIRE(!lines.empty());
  REQUIRE(lines.front().find("channel_0") != std::string::npos);
}

TEST_CASE("interface_registry collect_text returns lines from all instances") {
  stats_env env_dummy(champsim::modules::ModuleBuilder{});

  auto b1 = champsim::modules::ModuleBuilder("core_ct_A", "STATS_CORE_912");
  auto* c1 = champsim::modules::core_module::create_instance(b1, &env_dummy);
  static_cast<stats_core*>(c1)->roi_stats_.name = "core_A";
  static_cast<stats_core*>(c1)->roi_stats_.begin_instrs = 0;
  static_cast<stats_core*>(c1)->roi_stats_.end_instrs = 100;
  static_cast<stats_core*>(c1)->roi_stats_.begin_cycles = 0;
  static_cast<stats_core*>(c1)->roi_stats_.end_cycles = 50;

  std::vector<std::any> instances;
  instances.push_back(static_cast<champsim::modules::core_module*>(c1));

  REQUIRE(champsim::modules::interface_registry::has_stats("core"));
  auto lines = champsim::modules::interface_registry::collect_text("core", instances, true);
  REQUIRE(!lines.empty());
  REQUIRE(lines.front().find("core_A") != std::string::npos);
}

TEST_CASE("interface_registry collect_json returns named JSON entries") {
  stats_env env_dummy(champsim::modules::ModuleBuilder{});

  auto b1 = champsim::modules::ModuleBuilder("core_cj_A", "STATS_CORE_912");
  auto* c1 = champsim::modules::core_module::create_instance(b1, &env_dummy);
  static_cast<stats_core*>(c1)->sim_stats_.name = "core_A_sim";

  std::vector<std::any> instances;
  instances.push_back(static_cast<champsim::modules::core_module*>(c1));

  auto json_entries = champsim::modules::interface_registry::collect_json("core", instances, false);
  REQUIRE(!json_entries.empty());
  REQUIRE(json_entries[0].first == "core_cj_A"); // keyed by NAME, not stats.name
  REQUIRE(json_entries[0].second.has_value());
}
