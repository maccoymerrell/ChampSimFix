#include <catch.hpp>

#include "cache.h"
#include "channel.h"
#include "defaults.hpp"
#include <vector>

TEST_CASE("Caches can be built from ModuleBuilder defaults")
{
  GIVEN("A set of ModuleBuilder defaults for different cache types") {
    CACHE llc{champsim::modules::ModuleBuilder{"cache0", "default_cache", champsim::defaults::default_llc().add_parameter("mshr_size", static_cast<uint32_t>(8))}};
    CACHE dtlb{champsim::modules::ModuleBuilder{"cache1", "default_cache", champsim::defaults::default_dtlb()}};
    CACHE itlb{champsim::modules::ModuleBuilder{"cache2", "default_cache", champsim::defaults::default_itlb().add_parameter("mshr_size", static_cast<uint32_t>(8))}};
    CACHE l1d{champsim::modules::ModuleBuilder{"cache3", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))}};

    std::vector<CACHE*> caches{&llc, &dtlb, &itlb, &l1d};
    std::vector<std::string> expected_names{"cache0", "cache1", "cache2", "cache3"};

    THEN("The caches have the correct names") {
      std::vector<std::string> cache_names{};
      for (const auto* cache : caches)
        cache_names.push_back(cache->NAME);

      REQUIRE_THAT(cache_names, Catch::Matchers::Equals(expected_names));
    }
  }
}
