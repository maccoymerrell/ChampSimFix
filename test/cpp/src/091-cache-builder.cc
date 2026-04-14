#include <catch.hpp>

#include "cache.h"
#include "channel.h"
#include "defaults.hpp"

TEST_CASE("The number of sets can be specified")
{
  auto sets = GENERATE(8u, 16u, 64u, 256u);
  champsim::modules::ModuleBuilder cache_builder{"test_cache_91_0", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))};
  cache_builder.add_parameter("num_sets", static_cast<uint32_t>(sets));

  CACHE uut{cache_builder};

  REQUIRE(uut.NUM_SET == sets);
}

TEST_CASE("The ways can be specified")
{
  auto ways = GENERATE(4u, 8u, 16u);
  champsim::modules::ModuleBuilder cache_builder{"test_cache_91_1", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))};
  cache_builder.add_parameter("num_ways", static_cast<uint32_t>(ways));

  CACHE uut{cache_builder};

  REQUIRE(uut.NUM_WAY == ways);
}

TEST_CASE("The number of MSHRs can be specified")
{
  auto num_mshrs = GENERATE(4u, 8u, 16u);
  champsim::modules::ModuleBuilder cache_builder{"test_cache_91_2", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))};
  cache_builder.add_parameter("mshr_size", static_cast<uint32_t>(num_mshrs));

  CACHE uut{cache_builder};

  REQUIRE(uut.MSHR_SIZE == num_mshrs);
}

TEST_CASE("The tag and fill bandwidth can be specified")
{
  champsim::modules::ModuleBuilder cache_builder{"test_cache_91_3", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))};
  cache_builder.add_parameter("max_tag_bandwidth", champsim::bandwidth::maximum_type{6});
  cache_builder.add_parameter("max_fill_bandwidth", champsim::bandwidth::maximum_type{7});

  CACHE uut{cache_builder};

  CHECK(uut.MAX_TAG == champsim::bandwidth::maximum_type{6});
  CHECK(uut.MAX_FILL == champsim::bandwidth::maximum_type{7});
}

TEST_CASE("The hit latency and fill latencies can be specified")
{
  champsim::modules::ModuleBuilder cache_builder{"test_cache_91_4", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))};
  cache_builder.add_parameter("hit_latency", static_cast<uint64_t>(2));
  cache_builder.add_parameter("fill_latency", static_cast<uint64_t>(3));

  CACHE uut{cache_builder};

  CHECK(uut.HIT_LATENCY == 2 * uut.clock_period);
  CHECK(uut.FILL_LATENCY == 3 * uut.clock_period);
}
