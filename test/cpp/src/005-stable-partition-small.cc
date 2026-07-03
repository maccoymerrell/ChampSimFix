#include <algorithm>
#include <catch.hpp>
#include <random>
#include <vector>

#include "util/algorithm.h"

SCENARIO("stable_partition_small matches std::stable_partition")
{
  GIVEN("Randomized integer sequences")
  {
    std::mt19937_64 gen{0xC0FFEE};
    auto is_even = [](int x) {
      return x % 2 == 0;
    };

    for (std::size_t len : {0UL, 1UL, 2UL, 6UL, 32UL}) {
      std::vector<int> vals(len);
      std::uniform_int_distribution<int> dist{0, 99};
      std::generate(std::begin(vals), std::end(vals), [&] { return dist(gen); });

      WHEN("A sequence of length " + std::to_string(len) + " is partitioned both ways")
      {
        auto expected = vals;
        auto expected_bound = std::stable_partition(std::begin(expected), std::end(expected), is_even);

        auto actual = vals;
        auto actual_bound = champsim::stable_partition_small(std::begin(actual), std::end(actual), is_even);

        THEN("The element order and partition point are identical")
        {
          REQUIRE_THAT(actual, Catch::Matchers::Equals(expected));
          REQUIRE(std::distance(std::begin(actual), actual_bound) == std::distance(std::begin(expected), expected_bound));
        }
      }
    }
  }

  GIVEN("A predicate with observable side effects")
  {
    std::vector<int> vals{3, 4, 1, 8, 6, 5};

    WHEN("The range is partitioned")
    {
      std::vector<int> application_order{};
      auto counting_pred = [&](int x) {
        application_order.push_back(x);
        return x % 2 == 0;
      };
      champsim::stable_partition_small(std::begin(vals), std::end(vals), counting_pred);

      THEN("The predicate is applied exactly once per element, in original order")
      {
        REQUIRE_THAT(application_order, Catch::Matchers::Equals(std::vector<int>{3, 4, 1, 8, 6, 5}));
      }

      THEN("Relative order within each group is preserved")
      {
        REQUIRE_THAT(vals, Catch::Matchers::Equals(std::vector<int>{4, 8, 6, 3, 1, 5}));
      }
    }
  }
}
