#include <catch.hpp>

#include "instr.h"
#include "mocks.hpp"
#include "ooo_cpu.h"
#include "defaults.hpp"

SCENARIO("The scheduler can detect RAW hazards")
{
  GIVEN("A ROB with a single instruction")
  {
    constexpr unsigned schedule_width = 128;
    constexpr unsigned schedule_latency = 1;

    do_nothing_MRC mock_L1I, mock_L1D;
    O3_CPU uut{champsim::modules::ModuleBuilder{"t200_core_0", "DEFAULT_CORE", test_core_defaults("t200_core_0_ws")}
                   .add_parameter("schedule_width", champsim::bandwidth::maximum_type{schedule_width})
                   .add_parameter("register_file_size", static_cast<uint32_t>(128))
                   .add_parameter("schedule_latency", static_cast<unsigned>(schedule_latency))
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))};

    uut.modify_rob([&](auto& rob) {
      rob.push_back(champsim::test::instruction_with_ip(1));
    });
    uut.modify_rob([&](auto& rob) {
      for (auto& instr : rob) {
        instr.ready_time = champsim::chrono::clock::time_point{};
      }
    });

    // auto old_cycle = uut.current_cycle();

    WHEN("The instruction is not scheduled")
    {
      uut.modify_rob([&](auto& rob) {
        rob.front().scheduled = 0;
      });
      for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
        op->_operate();

      THEN("The instruction has no register dependencies")
      {
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob().front()) == 0);
        REQUIRE(uut.rob().front().scheduled);
        // REQUIRE(uut.rob().front().event_cycle == old_cycle + schedule_latency);
      }
    }
  }

  GIVEN("A ROB with a RAW hazard")
  {
    constexpr unsigned schedule_width = 128;
    constexpr unsigned schedule_latency = 1;

    do_nothing_MRC mock_L1I, mock_L1D;
    O3_CPU uut{champsim::modules::ModuleBuilder{"t200_core_1", "DEFAULT_CORE", test_core_defaults("t200_core_1_ws")}
                   .add_parameter("schedule_width", champsim::bandwidth::maximum_type{schedule_width})
                   .add_parameter("register_file_size", static_cast<uint32_t>(128))
                   .add_parameter("schedule_latency", static_cast<unsigned>(schedule_latency))
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))};

    std::vector test_instructions(2, champsim::test::instruction_with_registers(42));

    uut.modify_rob([&](auto& rob) {
      std::copy(std::begin(test_instructions), std::end(test_instructions), std::back_inserter(rob));
    });
    uut.modify_rob([&](auto& rob) {
      for (auto& instr : rob) {
        instr.ready_time = champsim::chrono::clock::time_point{};
      }
    });

    // auto old_cycle = uut.current_cycle();

    WHEN("None of the instructions are scheduled")
    {
      uut.modify_rob([&](auto& rob) {
        for (auto& instr : rob) {
          instr.scheduled = 0;
        }
      });

      for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
        op->_operate();

      THEN("The second instruction is dependent on the first")
      {
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob().at(0)) == 0);
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob().at(1)) == 1);
        REQUIRE(uut.rob().at(0).scheduled);
        REQUIRE(uut.rob().at(1).scheduled);
        // REQUIRE(uut.rob()[0].event_cycle == old_cycle + schedule_latency);
        // REQUIRE(uut.rob()[1].event_cycle == old_cycle + schedule_latency);
      }
    }
  }

  GIVEN("A ROB with more hazards than the scheduler can handle")
  {
    constexpr unsigned schedule_width = 4;
    constexpr unsigned schedule_latency = 1;

    do_nothing_MRC mock_L1I, mock_L1D;
    O3_CPU uut{champsim::modules::ModuleBuilder{"t200_core_2", "DEFAULT_CORE", test_core_defaults("t200_core_2_ws")}
                   .add_parameter("schedule_width", champsim::bandwidth::maximum_type{schedule_width})
                   .add_parameter("register_file_size", static_cast<uint32_t>(128))
                   .add_parameter("schedule_latency", static_cast<unsigned>(schedule_latency))
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))};

    std::vector test_instructions(schedule_width + 1, champsim::test::instruction_with_registers(42));

    uut.modify_rob([&](auto& rob) {
      std::copy(std::begin(test_instructions), std::end(test_instructions), std::back_inserter(rob));
    });
    uint64_t id = 0;
    uut.modify_rob([&](auto& rob) {
      for (auto& instr : rob) {
        instr.instr_id = id++;
        instr.ready_time = champsim::chrono::clock::time_point{};
      }
    });

    // auto old_cycle = uut.current_cycle();

    WHEN("None of the instructions are scheduled")
    {
      uut.modify_rob([&](auto& rob) {
        for (auto& instr : rob) {
          instr.scheduled = 0;
          instr.executed = 0;
        }
      });

      for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
        op->_operate();

      THEN("The second instruction is dependent on the first")
      {
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob()[0]) >= 0);
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob()[1]) >= 1);
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob()[2]) >= 1);
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob()[3]) >= 1);
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob()[4]) >= 0);
        // REQUIRE(std::all_of(std::next(std::begin(uut.rob())), std::next(std::begin(uut.rob()), schedule_width), [](ooo_model_instr x){ return x.num_reg_dependent
        // >= 1; })); REQUIRE(uut.rob().back().num_reg_dependent == 0);

        REQUIRE(uut.rob().at(0).scheduled);
        REQUIRE(uut.rob().at(1).scheduled);
        REQUIRE(uut.rob().at(2).scheduled);
        REQUIRE(uut.rob().at(3).scheduled);
        REQUIRE_FALSE(uut.rob().at(4).scheduled);
        // REQUIRE(std::all_of(std::next(std::begin(uut.rob())), std::next(std::begin(uut.rob()), schedule_width), [](ooo_model_instr x){ return x.scheduled; }));
        // REQUIRE_FALSE(uut.rob().back().scheduled);

        // REQUIRE(uut.rob()[0].event_cycle == old_cycle + schedule_latency);
        // REQUIRE(std::all_of(std::next(std::begin(uut.rob())), std::next(std::begin(uut.rob()), schedule_width), [old_cycle](ooo_model_instr x){ return
        // x.event_cycle == old_cycle + schedule_latency; })); REQUIRE(uut.rob().back().event_cycle == old_cycle);
      }
    }
  }
}

SCENARIO("The scheduler handles WAW hazards")
{
  GIVEN("A ROB with a WAW hazard followed by a read from that register")
  {
    constexpr unsigned schedule_width = 128;
    constexpr unsigned schedule_latency = 1;
    constexpr unsigned execute_width = 3;
    constexpr unsigned execute_latency = 3;

    do_nothing_MRC mock_L1I, mock_L1D;
    O3_CPU uut{champsim::modules::ModuleBuilder{"t200_core_3", "DEFAULT_CORE", test_core_defaults("t200_core_3_ws")}
                   .add_parameter("schedule_width", champsim::bandwidth::maximum_type{schedule_width})
                   .add_parameter("register_file_size", static_cast<uint32_t>(128))
                   .add_parameter("schedule_latency", static_cast<unsigned>(schedule_latency))
                   .add_parameter("execute_latency", static_cast<unsigned>(execute_latency))
                   .add_parameter("execute_width", champsim::bandwidth::maximum_type{execute_width})
                   .add_parameter("retire_width", champsim::bandwidth::maximum_type{execute_width})
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))};

    uut.modify_rob([&](auto& rob) {
      rob.push_back(champsim::test::instruction_with_ip(1));
      rob.at(0).instr_id = 1;
      rob.at(0).destination_registers.push_back(5);
      rob.push_back(champsim::test::instruction_with_ip(2));
      rob.at(1).instr_id = 2;
      rob.at(1).destination_registers.push_back(5);
      rob.push_back(champsim::test::instruction_with_ip(3));
      rob.at(2).instr_id = 3;
      rob.at(2).source_registers.push_back(5);
    });
    uut.modify_rob([&](auto& rob) {
      for (auto& instr : rob) {
        instr.ready_time = champsim::chrono::clock::time_point{};
      }
    });

    WHEN("The first two instructions are in flight")
    {
      uut.modify_rob([&](auto& rob) {
        rob.at(0).scheduled = false;
        rob.at(1).scheduled = false;
        rob.at(2).scheduled = false;
      });
      // Schedule
      for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
        op->_operate();
      REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob().at(2)) == 1);
      // Execute
      for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
        op->_operate();

      THEN("The third instruction does not execute")
      {
        REQUIRE(uut.rob().at(0).executed == true);
        REQUIRE(uut.rob().at(1).executed == true);
        REQUIRE(uut.rob().at(2).executed == false);
        REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob()[2]) == 1);
      }
      AND_WHEN("The first instruction finishes executing first")
      {
        REQUIRE(uut.rob().at(1).executed == true);
        REQUIRE(uut.rob().at(1).completed == false);
        uut.modify_rob([&](auto& rob) {
          rob.at(1).ready_time = champsim::chrono::clock::time_point{} + 5 * uut.EXEC_LATENCY;
        });
        for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
          op->_operate();
        THEN("The third instruction does not execute")
        {
          REQUIRE(uut.rob().at(0).completed == true);
          REQUIRE(uut.rob().at(1).completed == false);
          REQUIRE(uut.rob().at(2).executed == false);
        }
      }
      AND_WHEN("The second instruction finishes executing while the first is still in flight")
      {
        REQUIRE(uut.rob().at(0).executed == true);
        REQUIRE(uut.rob().at(0).completed == false);
        uut.modify_rob([&](auto& rob) {
          rob.at(0).ready_time = champsim::chrono::clock::time_point{} + 5 * uut.EXEC_LATENCY;
        });
        for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
          op->_operate();
        THEN("The third instruction executes") { REQUIRE(uut.rob().at(2).executed == true); }
      }
    }
  }

  GIVEN("A ROB with a long-running instruction that writes to a register")
  {
    constexpr unsigned schedule_width = 128;
    constexpr unsigned schedule_latency = 1;
    constexpr unsigned execute_width = 3;
    constexpr unsigned execute_latency = 3;

    do_nothing_MRC mock_L1I, mock_L1D;
    O3_CPU uut{champsim::modules::ModuleBuilder{"t200_core_4", "DEFAULT_CORE", test_core_defaults("t200_core_4_ws")}
                   .add_parameter("schedule_width", champsim::bandwidth::maximum_type{schedule_width})
                   .add_parameter("schedule_latency", static_cast<unsigned>(schedule_latency))
                   .add_parameter("execute_latency", static_cast<unsigned>(execute_latency))
                   .add_parameter("register_file_size", static_cast<uint32_t>(128))
                   .add_parameter("execute_width", champsim::bandwidth::maximum_type{execute_width})
                   .add_parameter("retire_width", champsim::bandwidth::maximum_type{execute_width})
                   .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
                   .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))};

    uut.modify_rob([&](auto& rob) {
      rob.push_back(champsim::test::instruction_with_ip(1));
      rob.at(0).instr_id = 1;
      rob.at(0).destination_registers.push_back(5);
      rob.at(0).source_memory.push_back(champsim::address{0xDEADBEEF});
      rob.at(0).ready_time = champsim::chrono::clock::time_point{};
      rob.at(0).scheduled = false;
    });
    for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
      op->_operate();
    for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
      op->_operate();

    WHEN("An write to the same register and a read from the register are scheduled")
    {
      uut.modify_rob([&](auto& rob) {
        rob.push_back(champsim::test::instruction_with_ip(2));
        rob.at(1).instr_id = 2;
        rob.at(1).destination_registers.push_back(5);
        rob.push_back(champsim::test::instruction_with_ip(3));
        rob.at(2).instr_id = 3;
        rob.at(2).source_registers.push_back(5);
      });
      uut.modify_rob([&](auto& rob) {
        for (auto& instr : rob) {
          instr.ready_time = champsim::chrono::clock::time_point{};
        }
      });
      uut.modify_rob([&](auto& rob) {
        rob.at(1).scheduled = false;
        rob.at(2).scheduled = false;
      });
      // Schedule 1,2
      for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
        op->_operate();
      REQUIRE(uut.reg_allocator.count_reg_dependencies(uut.rob()[2]) == 1);
      // Execute 1,2
      for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
        op->_operate();

      THEN("The third instruction does not execute and depends on the second write")
      {
        REQUIRE(uut.rob().at(0).completed == false);
        REQUIRE(uut.rob().at(1).completed == false);
        REQUIRE(uut.rob().at(2).executed == false);
      }

      AND_WHEN("The second instruction finishes executing while the first is still in flight")
      {
        REQUIRE(uut.rob().at(0).executed == true);
        REQUIRE(uut.rob().at(0).completed == false);
        for (auto op : std::array<champsim::operable*, 3>{{&uut, &mock_L1I, &mock_L1D}})
          op->_operate();
        THEN("The third instruction executes")
        {
          REQUIRE(uut.rob().at(1).completed == true);
          REQUIRE(uut.rob().at(2).executed == true);
        }
      }
    }
  }
}

TEST_CASE("ooo_cpu Benchmarks") {
  BENCHMARK_ADVANCED("ooo_cpu::operate()")(Catch::Benchmark::Chronometer meter){
    constexpr unsigned schedule_width = 128;
    constexpr unsigned schedule_latency = 1;
    constexpr unsigned execute_width = 3;
    constexpr unsigned execute_latency = 3;

    do_nothing_MRC mock_L1I, mock_L1D;
    O3_CPU uut{champsim::modules::ModuleBuilder{"t200_core_5", "DEFAULT_CORE", test_core_defaults("t200_core_5_ws")}
      .add_parameter("schedule_width", champsim::bandwidth::maximum_type{schedule_width})
      .add_parameter("schedule_latency", static_cast<unsigned>(schedule_latency))
      .add_parameter("execute_latency", static_cast<unsigned>(execute_latency))
      .add_parameter("register_file_size", static_cast<uint32_t>(128))
      .add_parameter("execute_width", champsim::bandwidth::maximum_type{execute_width})
      .add_parameter("retire_width", champsim::bandwidth::maximum_type{execute_width})
      .add_parameter("fetch_queues", static_cast<champsim::modules::channel_module*>(&mock_L1I.queues))
      .add_parameter("data_queues", static_cast<champsim::modules::channel_module*>(&mock_L1D.queues))
    };

      uut.modify_rob([&](auto& rob) {
        rob.push_back(champsim::test::instruction_with_ip(1));
        rob.at(0).instr_id = 1;
        rob.at(0).destination_registers.push_back(5);
        rob.at(0).source_memory.push_back(champsim::address{0xDEADBEEF});
        rob.at(0).ready_time = champsim::chrono::clock::time_point{};
        rob.at(0).scheduled = false;
      });

      meter.measure([&] { for (auto op : std::array<champsim::operable*,3>{{&uut, &mock_L1I, &mock_L1D}})
                          op->_operate();});
      meter.measure([&] {for (auto op : std::array<champsim::operable*,3>{{&uut, &mock_L1I, &mock_L1D}})
                          op->_operate();});

        uut.modify_rob([&](auto& rob) {
          rob.push_back(champsim::test::instruction_with_ip(2));
          rob.at(1).instr_id = 2;
          rob.at(1).destination_registers.push_back(5);
          rob.push_back(champsim::test::instruction_with_ip(3));
          rob.at(2).instr_id = 3;
          rob.at(2).source_registers.push_back(5);
        });
        uut.modify_rob([&](auto& rob) {
          for (auto& instr : rob)
            instr.ready_time = champsim::chrono::clock::time_point{};
          rob.at(1).scheduled = false;
          rob.at(2).scheduled = false;
        });
        // Schedule 1,2

        meter.measure([&] { for (auto op : std::array<champsim::operable*,3>{{&uut, &mock_L1I, &mock_L1D}})
          op->_operate();});
        // Execute 1,2
        meter.measure([&] { for (auto op : std::array<champsim::operable*,3>{{&uut, &mock_L1I, &mock_L1D}})
          op->_operate();});
        meter.measure([&] { for (auto op : std::array<champsim::operable*,3>{{&uut, &mock_L1I, &mock_L1D}})
          op->_operate();});
  };
  SUCCEED();
}