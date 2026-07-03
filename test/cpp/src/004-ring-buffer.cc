#include <algorithm>
#include <catch.hpp>
#include <numeric>
#include <string>
#include <vector>

#include "util/ring_buffer.h"

SCENARIO("A ring buffer acts as a bounded FIFO")
{
  GIVEN("An empty ring buffer with capacity 4")
  {
    champsim::ring_buffer<int> uut{4};

    THEN("It reports empty, not full, capacity 4")
    {
      REQUIRE(uut.empty());
      REQUIRE_FALSE(uut.full());
      REQUIRE(uut.size() == 0);
      REQUIRE(uut.capacity() == 4);
      REQUIRE(uut.begin() == uut.end());
    }

    WHEN("Three elements are pushed")
    {
      uut.push_back(10);
      uut.push_back(20);
      uut.push_back(30);

      THEN("Size, front, back, and indexing observe insertion order")
      {
        REQUIRE(uut.size() == 3);
        REQUIRE(uut.front() == 10);
        REQUIRE(uut.back() == 30);
        REQUIRE(uut[1] == 20);
        REQUIRE(uut.at(2) == 30);
      }

      THEN("Iteration visits elements in insertion order")
      {
        std::vector<int> seen(uut.begin(), uut.end());
        REQUIRE_THAT(seen, Catch::Matchers::Equals(std::vector<int>{10, 20, 30}));
      }

      AND_WHEN("The front is popped")
      {
        uut.pop_front();

        THEN("The second element becomes the front")
        {
          REQUIRE(uut.size() == 2);
          REQUIRE(uut.front() == 20);
        }
      }
    }

    WHEN("The buffer wraps around its capacity")
    {
      // fill, drain two, refill: the contents now straddle the wrap point
      for (int v : {1, 2, 3, 4})
        uut.push_back(v);
      uut.pop_front();
      uut.pop_front();
      uut.push_back(5);
      uut.push_back(6);

      THEN("It is full and iteration still observes FIFO order")
      {
        REQUIRE(uut.full());
        std::vector<int> seen(uut.begin(), uut.end());
        REQUIRE_THAT(seen, Catch::Matchers::Equals(std::vector<int>{3, 4, 5, 6}));
      }

      THEN("Random-access iterator arithmetic works across the wrap")
      {
        auto it = uut.begin();
        REQUIRE(*(it + 3) == 6);
        REQUIRE(it[2] == 5);
        REQUIRE((uut.end() - uut.begin()) == 4);
        REQUIRE(std::next(it, 2) < uut.end());
      }

      THEN("Standard algorithms operate across the wrap")
      {
        REQUIRE(std::find(uut.begin(), uut.end(), 5) != uut.end());
        REQUIRE(std::accumulate(uut.begin(), uut.end(), 0) == 18);
        auto part = std::partition_point(uut.begin(), uut.end(), [](int x) { return x < 5; });
        REQUIRE(*part == 5);
      }
    }

    WHEN("A front-anchored range is erased")
    {
      for (int v : {7, 8, 9})
        uut.push_back(v);
      uut.erase(uut.cbegin(), std::next(uut.cbegin(), 2));

      THEN("Only the suffix remains")
      {
        REQUIRE(uut.size() == 1);
        REQUIRE(uut.front() == 9);
      }
    }
  }

  GIVEN("A ring buffer of move-only-friendly elements")
  {
    champsim::ring_buffer<std::vector<std::string>> uut{2};

    WHEN("An rvalue is pushed")
    {
      std::vector<std::string> payload{"a", "b"};
      uut.push_back(std::move(payload));

      THEN("The element arrives intact")
      {
        REQUIRE(uut.front().size() == 2);
        REQUIRE(uut.front().at(0) == "a");
      }
    }
  }
}
