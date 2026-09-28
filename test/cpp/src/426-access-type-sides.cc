#include <catch.hpp>
#include <map>
#include <vector>

#include "cache.h"
#include "defaults.hpp"
#include "mocks.hpp"
#include "modules.h"

namespace
{
std::map<champsim::modules::cache_module*, std::vector<champsim::address>> t426_operate_collector;

// Records each activation; with next_block, also prefetches the following block.
struct t426_collector : champsim::modules::prefetcher {
  champsim::modules::cache_module* parent_ = nullptr;
  bool next_block_ = false;

  void prefetcher_initialize() override {}
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address, bool, bool, access_type, uint32_t metadata_in) override
  {
    ::t426_operate_collector[parent_].push_back(addr);
    if (next_block_)
      parent_->prefetch_line(champsim::address{addr.to<uint64_t>() + 64}, true, 0);
    return metadata_in;
  }
  uint32_t prefetcher_cache_fill(champsim::address, long, long, bool, champsim::address, uint32_t metadata_in) override { return metadata_in; }
  void prefetcher_cycle_operate() override {}
  void prefetcher_final_stats() override {}
  void prefetcher_branch_operate(champsim::address, uint8_t, champsim::address) override {}

  explicit t426_collector(champsim::modules::ModuleBuilder builder)
      : parent_(builder.get_parent<champsim::modules::cache_module>()), next_block_(builder.get_parameter<bool>("next_block", true, false))
  {
  }
};

bool same(access_type a, access_type b) { return static_cast<unsigned>(a) == static_cast<unsigned>(b); }
} // namespace
champsim::modules::prefetcher::register_module<t426_collector> t426_collector_register("t426_collector");

SCENARIO("An activation mask selects loads by side")
{
  using namespace std::literals;
  auto [mask, issued, expect_called] = GENERATE(table<access_type, access_type, bool>({
      std::tuple{access_type::LOAD, access_type::INSTRUCTION_LOAD, true},
      std::tuple{access_type::LOAD, access_type::DATA_LOAD, true},
      std::tuple{access_type::DATA_LOAD, access_type::DATA_LOAD, true},
      std::tuple{access_type::DATA_LOAD, access_type::INSTRUCTION_LOAD, false},
      std::tuple{access_type::INSTRUCTION_LOAD, access_type::INSTRUCTION_LOAD, true},
      std::tuple{access_type::INSTRUCTION_LOAD, access_type::DATA_LOAD, false},
  }));
  GIVEN("A cache whose prefetcher is activated by " + std::string{access_type_names.at(static_cast<unsigned>(mask))})
  {
    do_nothing_MRC mock_ll;
    to_rq_MRP mock_ul;
    CACHE uut{champsim::modules::ModuleBuilder{"t426_cache_0", "DEFAULT_CACHE", champsim::defaults::default_l1d()}
                  .add_parameter("mshr_size", static_cast<uint32_t>(8))
                  .add_parameter("upper_levels", std::vector<champsim::modules::channel_module*>{&mock_ul.queues})
                  .add_parameter("lower_level", static_cast<champsim::modules::channel_module*>(&mock_ll.queues))
                  .add_parameter("pref_activate_mask", std::vector<access_type>{mask})
                  .add_submodule("prefetcher", champsim::modules::ModuleBuilder{"t426_collector_0", "t426_collector"})};

    std::array<champsim::operable*, 3> elements{{&mock_ll, &mock_ul, &uut}};
    for (auto elem : elements) {
      elem->initialize();
      if (auto* mp = dynamic_cast<champsim::module_lifecycle*>(elem))
        mp->begin_phase(false);
    }

    WHEN("A " + std::string{access_type_names.at(static_cast<unsigned>(issued))} + " is issued")
    {
      ::t426_operate_collector.insert_or_assign(&uut, std::vector<champsim::address>{});

      decltype(mock_ul)::request_type test;
      test.address = champsim::address{0xdeadbeef};
      test.origin = champsim::origin{0, 0};
      test.type = issued;
      REQUIRE(mock_ul.issue(test));

      for (auto i = 0; i < 100; ++i)
        for (auto elem : elements)
          elem->_operate();

      THEN("The prefetcher is called only if the mask covers that side")
      {
        REQUIRE(std::size(::t426_operate_collector.at(&uut)) == (expect_called ? 1u : 0u));
      }
    }
  }
}

SCENARIO("A cache's prefetches take the side of the access being served")
{
  auto [served, expect_pf, expect_translation, pc_is_address] =
      GENERATE(table<access_type, access_type, access_type, bool>({std::tuple{access_type::INSTRUCTION_LOAD, access_type::INSTRUCTION_PREFETCH, access_type::INSTRUCTION_LOAD, true},
                                                                   std::tuple{access_type::DATA_LOAD, access_type::DATA_PREFETCH, access_type::DATA_LOAD, false}}));
  GIVEN("A virtually-prefetching cache whose prefetcher prefetches the next block")
  {
    do_nothing_MRC mock_translator;
    do_nothing_MRC mock_ll;
    to_rq_MRP mock_ul;
    CACHE uut{champsim::modules::ModuleBuilder{"t426_cache_1", "DEFAULT_CACHE", champsim::defaults::default_l1d()}
                  .add_parameter("mshr_size", static_cast<uint32_t>(8))
                  .add_parameter("virtual_prefetch", true)
                  .add_parameter("upper_levels", std::vector<champsim::modules::channel_module*>{&mock_ul.queues})
                  .add_parameter("lower_level", static_cast<champsim::modules::channel_module*>(&mock_ll.queues))
                  .add_parameter("lower_translate", static_cast<champsim::modules::channel_module*>(&mock_translator.queues))
                  .add_submodule("prefetcher", champsim::modules::ModuleBuilder{"t426_collector_1", "t426_collector"}.add_parameter("next_block", true))};

    std::array<champsim::operable*, 4> elements{{&uut, &mock_ll, &mock_ul, &mock_translator}};
    for (auto elem : elements) {
      elem->initialize();
      if (auto* mp = dynamic_cast<champsim::module_lifecycle*>(elem))
        mp->begin_phase(false);
    }

    WHEN("A translated " + std::string{access_type_names.at(static_cast<unsigned>(served))} + " misses")
    {
      ::t426_operate_collector.insert_or_assign(&uut, std::vector<champsim::address>{});

      decltype(mock_ul)::request_type test;
      test.address = champsim::address{0xdeadbe00};
      test.v_address = champsim::address{0xcafe0000};
      test.ip = champsim::address{0xcafe0000};
      test.is_translated = true;
      test.origin = champsim::origin{0, 0};
      test.type = served;
      REQUIRE(mock_ul.issue(test));

      for (auto i = 0; i < 200; ++i)
        for (auto elem : elements)
          elem->_operate();

      THEN("The prefetch is translated as a load of the same side")
      {
        REQUIRE(std::size(mock_translator.types) == 1);
        REQUIRE(same(mock_translator.types.front(), expect_translation));
      }

      THEN("The lower level sees the demand and a prefetch of the same side")
      {
        REQUIRE(std::size(mock_ll.types) == 2);
        REQUIRE(same(mock_ll.types.at(0), served));
        REQUIRE(same(mock_ll.types.at(1), expect_pf));
      }

      THEN("An instruction prefetch carries its line's address as its PC; a data prefetch carries none")
      {
        REQUIRE(std::size(mock_ll.ips) == 2);
        REQUIRE(mock_ll.ips.at(1) == (pc_is_address ? champsim::address{0xcafe0040} : champsim::address{}));
      }
    }
  }
}
