#include <array>
#include <catch.hpp>

#include "defaults.hpp"
#include "instr.h"
#include "mocks.hpp"
#include "ooo_cpu.h"

SCENARIO("The core tags fetches as instruction loads and memory operands as data loads")
{
  GIVEN("A core with one instruction that reads memory")
  {
    do_nothing_MRC mock_L1I, mock_L1D;
    O3_CPU uut{champsim::modules::ModuleBuilder{"t097_core", "DEFAULT_CORE", test_core_defaults("t097_core_ws")}
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))};
    uut.begin_phase(false);

    auto instr = champsim::test::instruction_with_ip_and_source_memory(champsim::address{0xfeed0040}, champsim::address{0xcafe0000});
    uut.modify_ifetch_buffer([&](auto& buf) { buf.push_back(instr); });

    WHEN("The instruction is fetched and executed")
    {
      for (auto i = 0; i < 2000; ++i)
        for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
          op->_operate();

      THEN("The fetch is an INSTRUCTION_LOAD")
      {
        REQUIRE(std::size(mock_L1I.types) == 1);
        REQUIRE(static_cast<unsigned>(mock_L1I.types.front()) == static_cast<unsigned>(access_type::INSTRUCTION_LOAD));
      }

      THEN("The memory operand is a DATA_LOAD")
      {
        REQUIRE(std::size(mock_L1D.types) == 1);
        REQUIRE(static_cast<unsigned>(mock_L1D.types.front()) == static_cast<unsigned>(access_type::DATA_LOAD));
      }
    }
  }
}
