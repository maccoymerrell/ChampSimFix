#include <catch.hpp>

#include "channel.h"
#include "defaults.hpp"
#include "dram_controller.h"

SCENARIO("The memory controller reports timer-scheduled work while a refresh is in flight")
{
  GIVEN("An idle memory controller with a short refresh period")
  {
    champsim::channel channel_uut{champsim::modules::ModuleBuilder{"t703_channel", "DEFAULT_CHANNEL", champsim::defaults::default_channel()}
                                      .add_parameter("rq_size", static_cast<std::size_t>(32))
                                      .add_parameter("pq_size", static_cast<std::size_t>(32))
                                      .add_parameter("wq_size", static_cast<std::size_t>(32))
                                      .add_parameter("offset_bits", champsim::data::bits{8})};

    const auto refresh_period = champsim::chrono::microseconds{32000};
    const std::size_t refreshes_per_period = 16384;
    MEMORY_CONTROLLER uut{champsim::modules::ModuleBuilder{"t703_uut", "DEFAULT_MEMORY_CONTROLLER", champsim::defaults::default_memory_controller()}
                              .add_parameter("dbus_period", champsim::chrono::picoseconds{312})
                              .add_parameter("mc_period", champsim::chrono::picoseconds{624})
                              .add_parameter("refresh_period", refresh_period)
                              .add_parameter("refreshes_per_period", refreshes_per_period)
                              .add_parameter("ul_channels", std::vector<champsim::modules::channel_module*>{&channel_uut})};

    THEN("With no requests and no refresh due, no pending work is reported")
    {
      REQUIRE_FALSE(uut.has_pending_work());
    }

    WHEN("The controller runs past the first refresh due time")
    {
      const auto tREF = refresh_period / refreshes_per_period;
      const auto cycles_to_refresh = static_cast<long>(tREF / uut.clock_period) + 2;

      bool pending_during_refresh = false;
      for (long i = 0; i < cycles_to_refresh; ++i) {
        uut._operate();
        pending_during_refresh |= uut.has_pending_work();
      }

      THEN("Pending work is reported while banks are under refresh")
      {
        REQUIRE(pending_during_refresh);
      }

      AND_WHEN("The refresh completes")
      {
        // tRFC is far shorter than tREF: run half a refresh interval
        for (long i = 0; i < cycles_to_refresh / 2; ++i) {
          uut._operate();
        }
        THEN("The controller is quiet again until the next refresh")
        {
          REQUIRE_FALSE(uut.has_pending_work());
        }
      }
    }
  }
}
