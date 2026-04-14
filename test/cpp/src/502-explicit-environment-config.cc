#include <any>
#include <catch.hpp>
#include <nlohmann/json.hpp>

#include "environment.h"
#include "modules.h"
#include "cache.h"

using json = nlohmann::json;
using champsim::modules::ModuleBuilder;

namespace {

// Build a minimal 1-core explicit config JSON.
// This is the minimum viable config: channels, DRAM, vmem, PTW, caches, core.
json minimal_explicit_config() {
  return json::parse(R"({
    "block_size": 64,
    "page_size": 4096,
    "children": [
      {"name": "ch_ptw_l1d",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 8, "offset_bits": {"bits": "6"}, "match_offset_bits": true},
      {"name": "ch_llc_dram", "module": "channel", "model": "default_channel", "rq_size": 64, "wq_size": 64, "pq_size": 64, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
      {"name": "ch_dtlb_stlb","module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
      {"name": "ch_itlb_stlb","module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
      {"name": "ch_l1d_l2c",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 16, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
      {"name": "ch_l1d_dtlb", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 16, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": true},
      {"name": "ch_l1i_l2c",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 16, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
      {"name": "ch_l1i_itlb", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 16, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": true},
      {"name": "ch_l2c_llc",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 32, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
      {"name": "ch_l2c_stlb", "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
      {"name": "ch_stlb_ptw", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 0, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
      {"name": "ch_core_l1i", "module": "channel", "model": "default_channel", "rq_size": 64, "wq_size": 64, "pq_size": 32, "offset_bits": {"bits": "6"}, "match_offset_bits": true},
      {"name": "ch_core_l1d", "module": "channel", "model": "default_channel", "rq_size": 64, "wq_size": 64, "pq_size": 8, "offset_bits": {"bits": "6"}, "match_offset_bits": true},
      {
        "name": "DRAM", "module": "memory_controller", "model": "default_memory_controller",
        "dbus_period": {"time": "312p"}, "mc_period": {"time": "625p"},
        "n_rp": 24, "n_rcd": 24, "n_cas": 24, "n_ras": 52,
        "refresh_period": {"time": "32000u"},
        "rq_size": 64, "wq_size": 64, "channels": 1, "channel_width": {"bytes": "8"},
        "rows": 65536, "columns": 1024, "ranks": 1, "bankgroups": 8, "banks": 4,
        "refreshes_per_period": 8192,
        "ul_channels": ["@ch_llc_dram"]
      },
      {
        "name": "VMEM", "module": "vmem", "model": "default_vmem",
        "page_table_page_size": {"bytes": "4Ki"}, "page_table_levels": 5,
        "minor_fault_penalty": {"time": "50000p"},
        "randomization_seed": {"optional_uint64": 1},
        "dram": "@DRAM"
      },
      {
        "name": "cpu0_PTW", "module": "page_table_walker", "model": "default_ptw",
        "clock_period": {"time": "250p"}, "cpu": 0, "mshr_size": 5, "latency": 0,
        "max_tag_check": {"bandwidth": 2}, "max_fill": {"bandwidth": 2},
        "upper_levels": ["@ch_stlb_ptw"], "lower_level": "@ch_ptw_l1d",
        "vmem": "@VMEM", "pscl_dims": [[5, 1, 2], [4, 1, 4], [3, 2, 4], [2, 4, 8]]
      },
      {
        "name": "LLC", "module": "cache", "model": "default_cache",
        "clock_period": {"time": "250p"}, "num_sets": 2048, "num_ways": 16, "pq_size": 32,
        "mshr_size": 64, "hit_latency": 10, "fill_latency": 10,
        "offset_bits": {"bits": "6"}, "max_tag_bandwidth": {"bandwidth": 1}, "max_fill_bandwidth": {"bandwidth": 1},
        "prefetch_as_load": false, "match_offset_bits": false, "virtual_prefetch": false,
        "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
        "upper_levels": ["@ch_l2c_llc"], "lower_level": "@ch_llc_dram", "lower_translate": {"null": "channel"},
        "children": [
          {"name": "llc_pf", "module": "prefetcher", "model": "no"},
          {"name": "llc_repl", "module": "replacement", "model": "lru"}
        ]
      },
      {
        "name": "cpu0_DTLB", "module": "cache", "model": "default_cache",
        "clock_period": {"time": "250p"}, "num_sets": 16, "num_ways": 4, "pq_size": 0,
        "mshr_size": 8, "hit_latency": 1, "fill_latency": 1,
        "offset_bits": {"bits": "12"}, "max_tag_bandwidth": {"bandwidth": 2}, "max_fill_bandwidth": {"bandwidth": 2},
        "prefetch_as_load": false, "match_offset_bits": false, "virtual_prefetch": false,
        "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
        "upper_levels": ["@ch_l1d_dtlb"], "lower_level": "@ch_dtlb_stlb", "lower_translate": {"null": "channel"},
        "children": [
          {"name": "dtlb_pf", "module": "prefetcher", "model": "no"},
          {"name": "dtlb_repl", "module": "replacement", "model": "lru"}
        ]
      },
      {
        "name": "cpu0_ITLB", "module": "cache", "model": "default_cache",
        "clock_period": {"time": "250p"}, "num_sets": 16, "num_ways": 4, "pq_size": 0,
        "mshr_size": 8, "hit_latency": 1, "fill_latency": 1,
        "offset_bits": {"bits": "12"}, "max_tag_bandwidth": {"bandwidth": 2}, "max_fill_bandwidth": {"bandwidth": 2},
        "prefetch_as_load": false, "match_offset_bits": false, "virtual_prefetch": true,
        "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
        "upper_levels": ["@ch_l1i_itlb"], "lower_level": "@ch_itlb_stlb", "lower_translate": {"null": "channel"},
        "children": [
          {"name": "itlb_pf", "module": "prefetcher", "model": "no"},
          {"name": "itlb_repl", "module": "replacement", "model": "lru"}
        ]
      },
      {
        "name": "cpu0_L1D", "module": "cache", "model": "default_cache",
        "clock_period": {"time": "250p"}, "num_sets": 64, "num_ways": 12, "pq_size": 8,
        "mshr_size": 16, "hit_latency": 2, "fill_latency": 3,
        "offset_bits": {"bits": "6"}, "max_tag_bandwidth": {"bandwidth": 2}, "max_fill_bandwidth": {"bandwidth": 2},
        "prefetch_as_load": false, "match_offset_bits": true, "virtual_prefetch": false,
        "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
        "upper_levels": ["@ch_ptw_l1d", "@ch_core_l1d"],
        "lower_level": "@ch_l1d_l2c", "lower_translate": "@ch_l1d_dtlb",
        "children": [
          {"name": "l1d_pf", "module": "prefetcher", "model": "no"},
          {"name": "l1d_repl", "module": "replacement", "model": "lru"}
        ]
      },
      {
        "name": "cpu0_L1I", "module": "cache", "model": "default_cache",
        "clock_period": {"time": "250p"}, "num_sets": 64, "num_ways": 8, "pq_size": 32,
        "mshr_size": 8, "hit_latency": 2, "fill_latency": 2,
        "offset_bits": {"bits": "6"}, "max_tag_bandwidth": {"bandwidth": 2}, "max_fill_bandwidth": {"bandwidth": 2},
        "prefetch_as_load": false, "match_offset_bits": true, "virtual_prefetch": true,
        "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
        "upper_levels": ["@ch_core_l1i"],
        "lower_level": "@ch_l1i_l2c", "lower_translate": "@ch_l1i_itlb",
        "children": [
          {"name": "l1i_pf", "module": "prefetcher", "model": "no"},
          {"name": "l1i_repl", "module": "replacement", "model": "lru"}
        ]
      },
      {
        "name": "cpu0_L2C", "module": "cache", "model": "default_cache",
        "clock_period": {"time": "250p"}, "num_sets": 1024, "num_ways": 8, "pq_size": 16,
        "mshr_size": 32, "hit_latency": 5, "fill_latency": 5,
        "offset_bits": {"bits": "6"}, "max_tag_bandwidth": {"bandwidth": 1}, "max_fill_bandwidth": {"bandwidth": 1},
        "prefetch_as_load": false, "match_offset_bits": false, "virtual_prefetch": false,
        "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
        "upper_levels": ["@ch_l1d_l2c", "@ch_l1i_l2c"],
        "lower_level": "@ch_l2c_llc", "lower_translate": "@ch_l2c_stlb",
        "children": [
          {"name": "l2c_pf", "module": "prefetcher", "model": "spp_dev"},
          {"name": "l2c_repl", "module": "replacement", "model": "lru"}
        ]
      },
      {
        "name": "cpu0_STLB", "module": "cache", "model": "default_cache",
        "clock_period": {"time": "250p"}, "num_sets": 128, "num_ways": 12, "pq_size": 0,
        "mshr_size": 16, "hit_latency": 4, "fill_latency": 4,
        "offset_bits": {"bits": "12"}, "max_tag_bandwidth": {"bandwidth": 1}, "max_fill_bandwidth": {"bandwidth": 1},
        "prefetch_as_load": false, "match_offset_bits": false, "virtual_prefetch": false,
        "pref_activate_mask": {"access_types": ["LOAD", "PREFETCH"]},
        "upper_levels": ["@ch_dtlb_stlb", "@ch_itlb_stlb", "@ch_l2c_stlb"],
        "lower_level": "@ch_stlb_ptw", "lower_translate": {"null": "channel"},
        "children": [
          {"name": "stlb_pf", "module": "prefetcher", "model": "no"},
          {"name": "stlb_repl", "module": "replacement", "model": "lru"}
        ]
      },
      {
        "name": "cpu0", "module": "core", "model": "default_core",
        "clock_period": {"time": "250p"}, "cpu": 0,
        "dib_set": 32, "dib_way": 8, "dib_window": 16,
        "dib_hit_buffer_size": 32, "dib_inorder_width": {"bandwidth": 5}, "dib_hit_latency": 1,
        "ifetch_buffer_size": 64, "decode_buffer_size": 32, "dispatch_buffer_size": 32,
        "register_file_size": 128, "rob_size": 352, "lq_size": 128, "sq_size": 72,
        "fetch_width": {"bandwidth": 6}, "decode_width": {"bandwidth": 6},
        "dispatch_width": {"bandwidth": 6}, "execute_width": {"bandwidth": 4},
        "lq_width": {"bandwidth": 2}, "sq_width": {"bandwidth": 2},
        "retire_width": {"bandwidth": 5}, "schedule_width": {"bandwidth": 128},
        "mispredict_penalty": 1, "decode_latency": 1, "dispatch_latency": 1,
        "schedule_latency": 0, "execute_latency": 0,
        "l1i": "@cpu0_L1I", "l1i_bandwidth": {"bandwidth": 2}, "l1d_bandwidth": {"bandwidth": 2},
        "fetch_queues": "@ch_core_l1i", "data_queues": "@ch_core_l1d",
        "children": [
          {"name": "cpu0_bp", "module": "branch_predictor", "model": "bimodal"},
          {"name": "cpu0_btb", "module": "btb", "model": "basic_btb"}
        ]
      }
    ]
  })");
}

// Helper to build an explicit environment from a JSON config
champsim::modules::environment_module* make_explicit_env(const json& config) {
  auto builder = ModuleBuilder{"test_explicit_env", "explicit_environment"};
  builder.add_parameter("config_json", config);
  return champsim::modules::environment_module::create_instance(builder, static_cast<champsim::modules::environment_module*>(nullptr));
}

// Helpers for module access by name
champsim::modules::cache_module* get_cache(champsim::modules::environment_module* env, const std::string& name) {
  for (auto& cache_ref : env->typed_view<champsim::modules::cache_module>("cache")) {
    if (cache_ref.get().NAME == name)
      return &cache_ref.get();
  }
  return nullptr;
}

} // namespace

// ====== Basic topology tests ======

SCENARIO("Explicit environment constructs correct topology from minimal config") {
  GIVEN("A minimal 1-core explicit config") {
    auto config = minimal_explicit_config();
    auto* env = make_explicit_env(config);

    THEN("num_cpus is 1") {
      REQUIRE(env->get_num("core") == 1);
    }
    THEN("block_size is 64") {
      REQUIRE(env->get_block_size() == 64);
    }
    THEN("page_size is 4096") {
      REQUIRE(env->get_page_size() == 4096);
    }
    THEN("cpu_view has 1 core") {
      REQUIRE(env->typed_view<champsim::modules::core_module>("core").size() == 1);
    }
    THEN("ptw_view has 1 PTW") {
      REQUIRE(env->typed_view<champsim::modules::page_table_walker_module>("page_table_walker").size() == 1);
    }
    THEN("cache_view has 7 caches (LLC + DTLB + ITLB + L1D + L1I + L2C + STLB)") {
      REQUIRE(env->typed_view<champsim::modules::cache_module>("cache").size() == 7);
    }
    THEN("operable_view has 1 core + 7 caches + 1 PTW + 1 DRAM = 10") {
      REQUIRE(env->typed_view<champsim::operable>("operable").size() == 10);
    }
    THEN("dram_view returns a valid memory controller") {
      auto drams = env->typed_view<champsim::modules::memory_controller_module>("memory_controller");
      REQUIRE(drams.size() >= 1);
      auto& dram = drams.front().get();
      REQUIRE(dram.get_num_channels() >= 1);
    }
  }
}

SCENARIO("Explicit environment respects custom block_size and page_size") {
  GIVEN("A minimal config with block_size=128 and page_size=8192") {
    auto config = minimal_explicit_config();
    config["block_size"] = 128;
    config["page_size"] = 8192;
    auto* env = make_explicit_env(config);

    THEN("block_size is 128") {
      REQUIRE(env->get_block_size() == 128);
    }
    THEN("page_size is 8192") {
      REQUIRE(env->get_page_size() == 8192);
    }
  }
}

// ====== Cache parameter propagation tests ======

SCENARIO("Explicit environment propagates cache parameters correctly") {
  GIVEN("A minimal explicit config") {
    auto config = minimal_explicit_config();
    auto* env = make_explicit_env(config);

    THEN("LLC has num_sets=2048, num_ways=16") {
      auto* llc = (CACHE*)get_cache(env, "LLC");
      REQUIRE(llc != nullptr);
      REQUIRE(llc->NUM_SET == 2048);
      REQUIRE(llc->NUM_WAY == 16);
    }
    THEN("cpu0_L1D has num_sets=64, num_ways=12") {
      auto* l1d = (CACHE*)get_cache(env, "cpu0_L1D");
      REQUIRE(l1d != nullptr);
      REQUIRE(l1d->NUM_SET == 64);
      REQUIRE(l1d->NUM_WAY == 12);
    }
    THEN("cpu0_L1I has num_sets=64, num_ways=8") {
      auto* l1i = (CACHE*)get_cache(env, "cpu0_L1I");
      REQUIRE(l1i != nullptr);
      REQUIRE(l1i->NUM_SET == 64);
      REQUIRE(l1i->NUM_WAY == 8);
    }
    THEN("cpu0_DTLB has num_sets=16, num_ways=4") {
      auto* dtlb = (CACHE*)get_cache(env, "cpu0_DTLB");
      REQUIRE(dtlb != nullptr);
      REQUIRE(dtlb->NUM_SET == 16);
      REQUIRE(dtlb->NUM_WAY == 4);
    }
    THEN("cpu0_L2C has num_sets=1024, num_ways=8") {
      auto* l2c = (CACHE*)get_cache(env, "cpu0_L2C");
      REQUIRE(l2c != nullptr);
      REQUIRE(l2c->NUM_SET == 1024);
      REQUIRE(l2c->NUM_WAY == 8);
    }
    THEN("cpu0_L1D has hit_latency=2 and fill_latency=3 (times clock_period)") {
      auto* l1d = (CACHE*)get_cache(env, "cpu0_L1D");
      REQUIRE(l1d != nullptr);
      REQUIRE(l1d->HIT_LATENCY == 2 * l1d->clock_period);
      REQUIRE(l1d->FILL_LATENCY == 3 * l1d->clock_period);
    }
    THEN("cpu0_L1I has virtual_prefetch=true") {
      auto* l1i = (CACHE*)get_cache(env, "cpu0_L1I");
      REQUIRE(l1i != nullptr);
      REQUIRE(l1i->virtual_prefetch == true);
    }
    THEN("cpu0_L1D has virtual_prefetch=false") {
      auto* l1d = (CACHE*)get_cache(env, "cpu0_L1D");
      REQUIRE(l1d != nullptr);
      REQUIRE(l1d->virtual_prefetch == false);
    }
    THEN("cpu0_DTLB pref_activate_mask has LOAD and PREFETCH") {
      auto* dtlb = (CACHE*)get_cache(env, "cpu0_DTLB");
      REQUIRE(dtlb != nullptr);
      REQUIRE(dtlb->pref_activate_mask.size() == 2);
      REQUIRE(dtlb->pref_activate_mask[0] == access_type::LOAD);
      REQUIRE(dtlb->pref_activate_mask[1] == access_type::PREFETCH);
    }
  }
}

// ====== @-reference resolution tests ======

SCENARIO("Explicit environment resolves @-references for builder params") {
  GIVEN("A minimal explicit config") {
    auto config = minimal_explicit_config();
    auto* env = make_explicit_env(config);

    THEN("DRAM builder has ul_channels referencing the LLC->DRAM channel") {
      auto dram_builder = env->get_builder_params("DRAM");
      REQUIRE(dram_builder.is_valid());
    }
    THEN("VMEM builder has a dram reference") {
      auto vmem_builder = env->get_builder_params("VMEM");
      REQUIRE(vmem_builder.is_valid());
    }
    THEN("cpu0_PTW builder is valid") {
      auto ptw_builder = env->get_builder_params("cpu0_PTW");
      REQUIRE(ptw_builder.is_valid());
    }
    THEN("cpu0 core builder is valid") {
      auto core_builder = env->get_builder_params("cpu0");
      REQUIRE(core_builder.is_valid());
    }
  }
}

// ====== Nested children (prefetcher/replacement/bp/btb) tests ======

SCENARIO("Explicit environment propagates nested children as module lists") {
  GIVEN("A minimal explicit config") {
    auto config = minimal_explicit_config();
    auto* env = make_explicit_env(config);

    THEN("LLC builder has prefetcher submodule ['no'] and replacement submodule ['lru']") {
      auto llc_builder = env->get_builder_params("LLC");
      REQUIRE(llc_builder.is_valid());
      auto& pf = llc_builder.get_submodules("prefetcher");
      REQUIRE(pf.size() == 1);
      REQUIRE(pf[0].get_model() == "no");
      auto& repl = llc_builder.get_submodules("replacement");
      REQUIRE(repl.size() == 1);
      REQUIRE(repl[0].get_model() == "lru");
    }
    THEN("cpu0_L2C builder has prefetcher submodule ['spp_dev']") {
      auto l2c_builder = env->get_builder_params("cpu0_L2C");
      REQUIRE(l2c_builder.is_valid());
      auto& pf = l2c_builder.get_submodules("prefetcher");
      REQUIRE(pf.size() == 1);
      REQUIRE(pf[0].get_model() == "spp_dev");
    }
    THEN("cpu0 core builder has branch_predictor submodule ['bimodal'] and btb submodule ['basic_btb']") {
      auto core_builder = env->get_builder_params("cpu0");
      REQUIRE(core_builder.is_valid());
      auto& bp = core_builder.get_submodules("branch_predictor");
      REQUIRE(bp.size() == 1);
      REQUIRE(bp[0].get_model() == "bimodal");
      auto& btb = core_builder.get_submodules("btb");
      REQUIRE(btb.size() == 1);
      REQUIRE(btb[0].get_model() == "basic_btb");
    }
  }
}

// ====== Type-wrapped value tests ======

SCENARIO("Explicit environment correctly parses type-wrapped values") {
  GIVEN("A minimal explicit config") {
    auto config = minimal_explicit_config();
    auto* env = make_explicit_env(config);

    THEN("DRAM has 1 channel (from integer parameter)") {
      REQUIRE(env->typed_view<champsim::modules::memory_controller_module>("memory_controller").front().get().get_num_channels() == 1);
    }
    THEN("LLC MSHR size is 64") {
      auto* llc = (CACHE*)get_cache(env, "LLC");
      REQUIRE(llc != nullptr);
      REQUIRE(llc->MSHR_SIZE == 64);
    }
  }
}

// ====== Nested children with extra parameters ======

SCENARIO("Explicit environment propagates nested child parameters") {
  GIVEN("A config where a cache child has extra params") {
    auto config = minimal_explicit_config();
    // Modify L2C to have ip_stride prefetcher with extra param
    for (auto& child : config["children"]) {
      if (child.value("name", "") == "cpu0_L2C") {
        child["children"] = json::array({
          {{"name", "l2c_pf"}, {"module", "prefetcher"}, {"model", "ip_stride"}, {"degree", 4}},
          {{"name", "l2c_repl"}, {"module", "replacement"}, {"model", "lru"}}
        });
      }
    }
    auto* env = make_explicit_env(config);

    THEN("cpu0_L2C builder has prefetcher submodule ['ip_stride']") {
      auto l2c_builder = env->get_builder_params("cpu0_L2C");
      REQUIRE(l2c_builder.is_valid());
      auto& pf = l2c_builder.get_submodules("prefetcher");
      REQUIRE(pf.size() == 1);
      REQUIRE(pf[0].get_model() == "ip_stride");
    }
    THEN("cpu0_L2C builder has nested prefetcher submodule with degree=4") {
      auto l2c_builder = env->get_builder_params("cpu0_L2C");
      auto& pf = l2c_builder.get_submodules("prefetcher");
      REQUIRE(pf.size() >= 1);
      REQUIRE(pf[0].get_parameter<int64_t>("degree") == 4);
    }
  }
}

// ====== Dump mode test ======

SCENARIO("Explicit environment dump mode does not crash") {
  GIVEN("A minimal explicit config with dump enabled") {
    ModuleBuilder::clear_dump_log();
    ModuleBuilder::set_dump_enabled(true);
    auto config = minimal_explicit_config();
    auto builder = ModuleBuilder{"dump_explicit_env", "explicit_environment"};
    builder.add_parameter("config_json", config);

    THEN("Construction succeeds and dump log is non-empty") {
      auto* env = champsim::modules::environment_module::create_instance(builder, static_cast<champsim::modules::environment_module*>(nullptr));
      REQUIRE(env->get_num("core") == 1);
      REQUIRE_FALSE(ModuleBuilder::get_dump_log().empty());
    }

    ModuleBuilder::clear_dump_log();
    ModuleBuilder::set_dump_enabled(false);
  }
}

SCENARIO("Explicit environment dump log contains expected modules and parameters") {
  GIVEN("A minimal single-core explicit config with dump enabled") {
    ModuleBuilder::clear_dump_log();
    ModuleBuilder::set_dump_enabled(true);
    auto config = minimal_explicit_config();
    auto builder = ModuleBuilder{"dump_explicit", "explicit_environment"};
    builder.add_parameter("config_json", config);
    champsim::modules::environment_module::create_instance(builder, static_cast<champsim::modules::environment_module*>(nullptr));
    auto& log = ModuleBuilder::get_dump_log();

    THEN("The dump log contains entries for all major module types") {
      // Core
      REQUIRE(log.find("[cpu0]") != std::string::npos);
      // Caches
      REQUIRE(log.find("[cpu0_L1D]") != std::string::npos);
      REQUIRE(log.find("[cpu0_L1I]") != std::string::npos);
      REQUIRE(log.find("[cpu0_L2C]") != std::string::npos);
      REQUIRE(log.find("[LLC]") != std::string::npos);
      // TLBs
      REQUIRE(log.find("[cpu0_DTLB]") != std::string::npos);
      REQUIRE(log.find("[cpu0_ITLB]") != std::string::npos);
      REQUIRE(log.find("[cpu0_STLB]") != std::string::npos);
      // PTW and DRAM
      REQUIRE(log.find("[cpu0_PTW]") != std::string::npos);
      REQUIRE(log.find("[DRAM]") != std::string::npos);
      // VMEM
      REQUIRE(log.find("[VMEM]") != std::string::npos);
    }

    THEN("The dump log contains set parameter tags") {
      REQUIRE(log.find("(set)") != std::string::npos);
    }

    THEN("Channel parameters are logged with correct values") {
      auto pos = log.find("[ch_ptw_l1d]");
      REQUIRE(pos != std::string::npos);
      REQUIRE(log.find("rq_size", pos) != std::string::npos);
      REQUIRE(log.find("32 (set)", pos) != std::string::npos);
    }

    THEN("Cache parameters are logged") {
      auto pos = log.find("[cpu0_L1D]");
      REQUIRE(pos != std::string::npos);
      REQUIRE(log.find("num_sets", pos) != std::string::npos);
      REQUIRE(log.find("num_ways", pos) != std::string::npos);
    }

    THEN("DRAM parameters are logged") {
      auto pos = log.find("[DRAM]");
      REQUIRE(pos != std::string::npos);
      REQUIRE(log.find("channels", pos) != std::string::npos);
      REQUIRE(log.find("1 (set)", pos) != std::string::npos);
    }

    THEN("Core parameters are logged") {
      auto pos = log.find("[cpu0]");
      REQUIRE(pos != std::string::npos);
      REQUIRE(log.find("rob_size", pos) != std::string::npos);
    }

    ModuleBuilder::clear_dump_log();
    ModuleBuilder::set_dump_enabled(false);
  }
}

// ====== Multi-module test (2 cores explicitly) ======

SCENARIO("Explicit environment supports multi-core via explicit module declarations") {
  GIVEN("An explicit config with 2 cores and all supporting modules") {
    // Build a 2-core config from scratch with correct declaration order:
    // all channels first, then DRAM, VMEM, PTWs, caches, cores
    auto config = json::parse(R"({
      "block_size": 64,
      "page_size": 4096,
      "children": [
        {"name": "ch0_ptw_l1d",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 8, "offset_bits": {"bits": "6"}, "match_offset_bits": true},
        {"name": "ch0_llc_dram", "module": "channel", "model": "default_channel", "rq_size": 64, "wq_size": 64, "pq_size": 64, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
        {"name": "ch0_dtlb_stlb","module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
        {"name": "ch0_itlb_stlb","module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
        {"name": "ch0_l1d_l2c",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 16, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
        {"name": "ch0_l1d_dtlb", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 16, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": true},
        {"name": "ch0_l1i_l2c",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 16, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
        {"name": "ch0_l1i_itlb", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 16, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": true},
        {"name": "ch0_l2c_llc",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 32, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
        {"name": "ch0_l2c_stlb", "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
        {"name": "ch0_stlb_ptw", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 0, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
        {"name": "ch0_core_l1i", "module": "channel", "model": "default_channel", "rq_size": 64, "wq_size": 64, "pq_size": 32, "offset_bits": {"bits": "6"}, "match_offset_bits": true},
        {"name": "ch0_core_l1d", "module": "channel", "model": "default_channel", "rq_size": 64, "wq_size": 64, "pq_size": 8, "offset_bits": {"bits": "6"}, "match_offset_bits": true},

        {"name": "ch1_ptw_l1d",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 8, "offset_bits": {"bits": "6"}, "match_offset_bits": true},
        {"name": "ch1_dtlb_stlb","module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
        {"name": "ch1_itlb_stlb","module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
        {"name": "ch1_l1d_l2c",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 16, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
        {"name": "ch1_l1d_dtlb", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 16, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": true},
        {"name": "ch1_l1i_l2c",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 16, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
        {"name": "ch1_l1i_itlb", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 16, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": true},
        {"name": "ch1_l2c_llc",  "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 32, "offset_bits": {"bits": "6"}, "match_offset_bits": false},
        {"name": "ch1_l2c_stlb", "module": "channel", "model": "default_channel", "rq_size": 32, "wq_size": 32, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
        {"name": "ch1_stlb_ptw", "module": "channel", "model": "default_channel", "rq_size": 16, "wq_size": 0, "pq_size": 0, "offset_bits": {"bits": "12"}, "match_offset_bits": false},
        {"name": "ch1_core_l1i", "module": "channel", "model": "default_channel", "rq_size": 64, "wq_size": 64, "pq_size": 32, "offset_bits": {"bits": "6"}, "match_offset_bits": true},
        {"name": "ch1_core_l1d", "module": "channel", "model": "default_channel", "rq_size": 64, "wq_size": 64, "pq_size": 8, "offset_bits": {"bits": "6"}, "match_offset_bits": true},

        {
          "name": "DRAM", "module": "memory_controller", "model": "default_memory_controller",
          "dbus_period": {"time": "312p"}, "mc_period": {"time": "625p"},
          "n_rp": 24, "n_rcd": 24, "n_cas": 24, "n_ras": 52,
          "refresh_period": {"time": "32000u"},
          "rq_size": 64, "wq_size": 64, "channels": 1, "channel_width": {"bytes": "8"},
          "rows": 65536, "columns": 1024, "ranks": 1, "bankgroups": 8, "banks": 4,
          "refreshes_per_period": 8192,
          "ul_channels": ["@ch0_llc_dram"]
        },
        {
          "name": "VMEM", "module": "vmem", "model": "default_vmem",
          "page_table_page_size": {"bytes": "4Ki"}, "page_table_levels": 5,
          "minor_fault_penalty": {"time": "50000p"},
          "randomization_seed": {"optional_uint64": 1},
          "dram": "@DRAM"
        },

        {"name": "cpu0_PTW", "module": "page_table_walker", "model": "default_ptw",
         "clock_period": {"time": "250p"}, "cpu": 0, "mshr_size": 5, "latency": 0,
         "max_tag_check": {"bandwidth": 2}, "max_fill": {"bandwidth": 2},
         "upper_levels": ["@ch0_stlb_ptw"], "lower_level": "@ch0_ptw_l1d",
         "vmem": "@VMEM", "pscl_dims": [[5, 1, 2], [4, 1, 4], [3, 2, 4], [2, 4, 8]]},
        {"name": "cpu1_PTW", "module": "page_table_walker", "model": "default_ptw",
         "clock_period": {"time": "250p"}, "cpu": 1, "mshr_size": 5, "latency": 0,
         "max_tag_check": {"bandwidth": 2}, "max_fill": {"bandwidth": 2},
         "upper_levels": ["@ch1_stlb_ptw"], "lower_level": "@ch1_ptw_l1d",
         "vmem": "@VMEM", "pscl_dims": [[5, 1, 2], [4, 1, 4], [3, 2, 4], [2, 4, 8]]}
      ]
    })");

    auto& ch = config["children"];

    // Helper lambda to add a cache
    auto add_cache = [&](const std::string& name, int sets, int ways, int pq, int mshr,
                         int hit_lat, int fill_lat, int offset_bits,
                         int tag_bw, int fill_bw, bool virt_pf, bool match_off,
                         const json& ul, const std::string& ll, const json& lt,
                         const std::string& pf_model, const std::string& repl_model,
                         const std::string& pf_name, const std::string& repl_name) {
      json c = {{"name", name}, {"module", "cache"}, {"model", "default_cache"},
        {"clock_period", {{"time", "250p"}}}, {"num_sets", sets}, {"num_ways", ways},
        {"pq_size", pq}, {"mshr_size", mshr}, {"hit_latency", hit_lat}, {"fill_latency", fill_lat},
        {"offset_bits", {{"bits", std::to_string(offset_bits)}}},
        {"max_tag_bandwidth", {{"bandwidth", tag_bw}}}, {"max_fill_bandwidth", {{"bandwidth", fill_bw}}},
        {"prefetch_as_load", false}, {"match_offset_bits", match_off}, {"virtual_prefetch", virt_pf},
        {"pref_activate_mask", {{"access_types", json::array({"LOAD", "PREFETCH"})}}},
        {"upper_levels", ul}, {"lower_level", "@" + ll}, {"lower_translate", lt},
        {"children", json::array({
          {{"name", pf_name}, {"module", "prefetcher"}, {"model", pf_model}},
          {{"name", repl_name}, {"module", "replacement"}, {"model", repl_model}}
        })}
      };
      ch.push_back(c);
    };

    json null_channel = {{"null", "channel"}};

    // LLC (shared, both L2Cs feed into it)
    add_cache("LLC", 2048, 16, 32, 64, 10, 10, 6, 1, 1, false, false,
              json::array({"@ch0_l2c_llc", "@ch1_l2c_llc"}), "ch0_llc_dram", null_channel,
              "no", "lru", "llc_pf", "llc_repl");

    // Core 0 caches
    add_cache("cpu0_DTLB", 16, 4, 0, 8, 1, 1, 12, 2, 2, false, false,
              json::array({"@ch0_l1d_dtlb"}), "ch0_dtlb_stlb", null_channel, "no", "lru", "c0dtlb_pf", "c0dtlb_repl");
    add_cache("cpu0_ITLB", 16, 4, 0, 8, 1, 1, 12, 2, 2, true, false,
              json::array({"@ch0_l1i_itlb"}), "ch0_itlb_stlb", null_channel, "no", "lru", "c0itlb_pf", "c0itlb_repl");
    add_cache("cpu0_L1D", 64, 12, 8, 16, 2, 3, 6, 2, 2, false, true,
              json::array({"@ch0_ptw_l1d", "@ch0_core_l1d"}), "ch0_l1d_l2c", "@ch0_l1d_dtlb", "no", "lru", "c0l1d_pf", "c0l1d_repl");
    add_cache("cpu0_L1I", 64, 8, 32, 8, 2, 2, 6, 2, 2, true, true,
              json::array({"@ch0_core_l1i"}), "ch0_l1i_l2c", "@ch0_l1i_itlb", "no", "lru", "c0l1i_pf", "c0l1i_repl");
    add_cache("cpu0_L2C", 1024, 8, 16, 32, 5, 5, 6, 1, 1, false, false,
              json::array({"@ch0_l1d_l2c", "@ch0_l1i_l2c"}), "ch0_l2c_llc", "@ch0_l2c_stlb", "spp_dev", "lru", "c0l2c_pf", "c0l2c_repl");
    add_cache("cpu0_STLB", 128, 12, 0, 16, 4, 4, 12, 1, 1, false, false,
              json::array({"@ch0_dtlb_stlb", "@ch0_itlb_stlb", "@ch0_l2c_stlb"}), "ch0_stlb_ptw", null_channel, "no", "lru", "c0stlb_pf", "c0stlb_repl");

    // Core 1 caches
    add_cache("cpu1_DTLB", 16, 4, 0, 8, 1, 1, 12, 2, 2, false, false,
              json::array({"@ch1_l1d_dtlb"}), "ch1_dtlb_stlb", null_channel, "no", "lru", "c1dtlb_pf", "c1dtlb_repl");
    add_cache("cpu1_ITLB", 16, 4, 0, 8, 1, 1, 12, 2, 2, true, false,
              json::array({"@ch1_l1i_itlb"}), "ch1_itlb_stlb", null_channel, "no", "lru", "c1itlb_pf", "c1itlb_repl");
    add_cache("cpu1_L1D", 64, 12, 8, 16, 2, 3, 6, 2, 2, false, true,
              json::array({"@ch1_ptw_l1d", "@ch1_core_l1d"}), "ch1_l1d_l2c", "@ch1_l1d_dtlb", "no", "lru", "c1l1d_pf", "c1l1d_repl");
    add_cache("cpu1_L1I", 64, 8, 32, 8, 2, 2, 6, 2, 2, true, true,
              json::array({"@ch1_core_l1i"}), "ch1_l1i_l2c", "@ch1_l1i_itlb", "no", "lru", "c1l1i_pf", "c1l1i_repl");
    add_cache("cpu1_L2C", 1024, 8, 16, 32, 5, 5, 6, 1, 1, false, false,
              json::array({"@ch1_l1d_l2c", "@ch1_l1i_l2c"}), "ch1_l2c_llc", "@ch1_l2c_stlb", "spp_dev", "lru", "c1l2c_pf", "c1l2c_repl");
    add_cache("cpu1_STLB", 128, 12, 0, 16, 4, 4, 12, 1, 1, false, false,
              json::array({"@ch1_dtlb_stlb", "@ch1_itlb_stlb", "@ch1_l2c_stlb"}), "ch1_stlb_ptw", null_channel, "no", "lru", "c1stlb_pf", "c1stlb_repl");

    // Helper lambda to add a core
    auto add_core = [&](const std::string& name, int cpu_id,
                        const std::string& l1i, const std::string& fetch_ch, const std::string& data_ch,
                        const std::string& bp_model, const std::string& btb_model,
                        const std::string& bp_name, const std::string& btb_name) {
      ch.push_back({{"name", name}, {"module", "core"}, {"model", "default_core"},
        {"clock_period", {{"time", "250p"}}}, {"cpu", cpu_id},
        {"dib_set", 32}, {"dib_way", 8}, {"dib_window", 16},
        {"dib_hit_buffer_size", 32}, {"dib_inorder_width", {{"bandwidth", 5}}}, {"dib_hit_latency", 1},
        {"ifetch_buffer_size", 64}, {"decode_buffer_size", 32}, {"dispatch_buffer_size", 32},
        {"register_file_size", 128}, {"rob_size", 352}, {"lq_size", 128}, {"sq_size", 72},
        {"fetch_width", {{"bandwidth", 6}}}, {"decode_width", {{"bandwidth", 6}}},
        {"dispatch_width", {{"bandwidth", 6}}}, {"execute_width", {{"bandwidth", 4}}},
        {"lq_width", {{"bandwidth", 2}}}, {"sq_width", {{"bandwidth", 2}}},
        {"retire_width", {{"bandwidth", 5}}}, {"schedule_width", {{"bandwidth", 128}}},
        {"mispredict_penalty", 1}, {"decode_latency", 1}, {"dispatch_latency", 1},
        {"schedule_latency", 0}, {"execute_latency", 0},
        {"l1i", "@" + l1i}, {"l1i_bandwidth", {{"bandwidth", 2}}}, {"l1d_bandwidth", {{"bandwidth", 2}}},
        {"fetch_queues", "@" + fetch_ch}, {"data_queues", "@" + data_ch},
        {"children", json::array({
          {{"name", bp_name}, {"module", "branch_predictor"}, {"model", bp_model}},
          {{"name", btb_name}, {"module", "btb"}, {"model", btb_model}}
        })}
      });
    };

    add_core("cpu0", 0, "cpu0_L1I", "ch0_core_l1i", "ch0_core_l1d", "bimodal", "basic_btb", "c0_bp", "c0_btb");
    add_core("cpu1", 1, "cpu1_L1I", "ch1_core_l1i", "ch1_core_l1d", "bimodal", "basic_btb", "c1_bp", "c1_btb");

    auto* env = make_explicit_env(config);

    THEN("num_cpus is 2") {
      REQUIRE(env->get_num("core") == 2);
    }
    THEN("cpu_view has 2 cores") {
      REQUIRE(env->typed_view<champsim::modules::core_module>("core").size() == 2);
    }
    THEN("ptw_view has 2 PTWs") {
      REQUIRE(env->typed_view<champsim::modules::page_table_walker_module>("page_table_walker").size() == 2);
    }
    THEN("cache_view has 13 caches (LLC + 6 per-core * 2)") {
      REQUIRE(env->typed_view<champsim::modules::cache_module>("cache").size() == 13);
    }
    THEN("operable_view has 2 cores + 13 caches + 2 PTWs + 1 DRAM = 18") {
      REQUIRE(env->typed_view<champsim::operable>("operable").size() == 18);
    }
  }
}
