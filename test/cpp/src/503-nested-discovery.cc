#include <catch.hpp>
#include <nlohmann/json.hpp>

#include "environment.h"
#include "modules.h"

// Uses MOCK_CORE_913 (a registered core model that accepts optional
// workload_source children) and NULL_WORKLOAD_SOURCE (test-only source).

TEST_CASE("Nested submodule instances enroll with the explicit environment's views")
{
  nlohmann::json config = {
      {"children", nlohmann::json::array({
          nlohmann::json{
              {"name", "c0"},
              {"module", "core"},
              {"model", "MOCK_CORE_913"},
              {"children", nlohmann::json::array({
                  nlohmann::json{{"name", "c0_src"}, {"module", "workload_source"}, {"model", "NULL_WORKLOAD_SOURCE"}},
              })},
          },
      })},
  };

  auto env_builder = champsim::modules::ModuleBuilder("t503_env", "ENVIRONMENT")
    .add_parameter("config_json", config)
    .add_parameter("cli_args", nlohmann::json::object());
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));

  SECTION("The nested workload source is visible by its interface name") {
    REQUIRE(env->view("workload_source").size() == 1);
    REQUIRE(env->get_num("workload_source") == 1);
  }

  SECTION("Aggregate views and counts agree (the 'Trace sources: 0' bug)") {
    REQUIRE(env->get_num("source_consumer") == env->view("source_consumer").size());
    REQUIRE(env->get_num("source_consumer") == 1);
    REQUIRE(env->get_num("operable") == env->view("operable").size());
  }

  SECTION("The core is discovered as both operable and source consumer") {
    REQUIRE(env->typed_view<champsim::operable>("operable").size() == 1);
    REQUIRE(env->typed_view<champsim::modules::source_consumer>("source_consumer").size() == 1);
  }
}
