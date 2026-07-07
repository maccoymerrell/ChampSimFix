#include <algorithm>
#include <catch.hpp>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bandwidth.h"
#include "util/ring_buffer.h"
#include "util/span.h"

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

    WHEN("A tail-anchored range is erased")
    {
      for (int v : {1, 2, 3, 4})
        uut.push_back(v);
      uut.pop_front(); // wrap the contents
      uut.push_back(5);
      auto kept = uut.erase(std::next(uut.cbegin(), 2), uut.cend());

      THEN("Only the prefix remains and the returned iterator is the new end")
      {
        REQUIRE(uut.size() == 2);
        REQUIRE(kept == uut.end());
        std::vector<int> seen(uut.begin(), uut.end());
        REQUIRE_THAT(seen, Catch::Matchers::Equals(std::vector<int>{2, 3}));
      }
    }

    WHEN("Elements are addressed by physical slot")
    {
      for (int v : {1, 2, 3, 4})
        uut.push_back(v);
      uut.pop_front();
      uut.pop_front();
      uut.push_back(5); // 5 reuses slot 0

      THEN("Iterator slots are stable and at_slot returns the same element")
      {
        for (auto it = uut.begin(); it != uut.end(); ++it) {
          REQUIRE(uut.at_slot(it.slot()) == *it);
        }
        REQUIRE(uut.head_slot() == 2);
        REQUIRE(uut.slot_index(2) == 0); // logical 2 wrapped to physical 0
        REQUIRE(uut.at_slot(0) == 5);
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

    WHEN("An element is emplaced")
    {
      auto& emplaced = uut.emplace_back(std::size_t{3}, std::string{"x"});

      THEN("The returned reference designates the constructed back element")
      {
        REQUIRE(&emplaced == &uut.back());
        REQUIRE(uut.back().size() == 3);
        REQUIRE(uut.back().at(2) == "x");
      }
    }
  }
}

SCENARIO("A ring buffer supports the standard container interface")
{
  GIVEN("A ring buffer with some elements")
  {
    champsim::ring_buffer<int> uut{4};
    uut.push_back(1);
    uut.push_back(2);
    uut.push_back(3);

    THEN("at() throws std::out_of_range beyond the last element")
    {
      REQUIRE(uut.at(2) == 3);
      REQUIRE_THROWS_AS(uut.at(3), std::out_of_range);
      REQUIRE_THROWS_AS(std::as_const(uut).at(17), std::out_of_range);
    }

    THEN("Const iteration agrees with mutable iteration")
    {
      std::vector<int> seen(std::as_const(uut).begin(), std::as_const(uut).end());
      REQUIRE_THAT(seen, Catch::Matchers::Equals(std::vector<int>(uut.cbegin(), uut.cend())));
    }

    WHEN("clear() is called")
    {
      uut.clear();

      THEN("The buffer is empty with unchanged capacity")
      {
        REQUIRE(uut.empty());
        REQUIRE(uut.capacity() == 4);
      }
    }

    WHEN("get_span_p is applied under a bandwidth limit")
    {
      auto [begin, end] = champsim::get_span_p(std::begin(uut), std::end(uut), champsim::bandwidth{champsim::bandwidth::maximum_type{2}},
                                               [](int x) { return x < 3; });

      THEN("The span covers the limited matching prefix")
      {
        REQUIRE(begin == uut.begin());
        REQUIRE(std::distance(begin, end) == 2);
      }
    }

    THEN("std::find_if locates elements by predicate")
    {
      auto found = std::find_if(std::begin(uut), std::end(uut), [](int x) { return x % 2 == 0; });
      REQUIRE(found != std::end(uut));
      REQUIRE(*found == 2);
    }
  }

  GIVEN("A ring buffer of shared pointers")
  {
    champsim::ring_buffer<std::shared_ptr<int>> uut{2};
    auto tracked = std::make_shared<int>(42);
    uut.push_back(tracked);

    WHEN("The buffer is cleared")
    {
      uut.clear();

      THEN("The element's resources are released promptly")
      {
        REQUIRE(tracked.use_count() == 1);
      }
    }

    WHEN("The front is popped")
    {
      uut.pop_front();

      THEN("The element's resources are released promptly")
      {
        REQUIRE(tracked.use_count() == 1);
      }
    }
  }
}

SCENARIO("A ring buffer's backing store can be enlarged in place")
{
  GIVEN("A wrapped ring buffer at capacity")
  {
    champsim::ring_buffer<int> uut{4};
    for (int v : {1, 2, 3, 4})
      uut.push_back(v);
    uut.pop_front();
    uut.pop_front();
    uut.push_back(5);
    uut.push_back(6); // contents straddle the wrap point
    REQUIRE(uut.full());

    WHEN("reserve() enlarges the store")
    {
      uut.reserve(7);

      THEN("Capacity grows and the contents keep their order")
      {
        REQUIRE(uut.capacity() == 7);
        std::vector<int> seen(uut.begin(), uut.end());
        REQUIRE_THAT(seen, Catch::Matchers::Equals(std::vector<int>{3, 4, 5, 6}));
      }
    }

    WHEN("reserve() asks for no more than the current capacity")
    {
      auto* before = &uut.front();
      uut.reserve(3);

      THEN("Nothing changes and references remain valid")
      {
        REQUIRE(uut.capacity() == 4);
        REQUIRE(before == &uut.front());
      }
    }

    WHEN("push_back_grow() is called on the full buffer")
    {
      uut.push_back_grow(7);

      THEN("The element is admitted after amortized growth")
      {
        REQUIRE(uut.size() == 5);
        REQUIRE(uut.capacity() == 8);
        REQUIRE(uut.back() == 7);
        REQUIRE(uut.front() == 3);
      }
    }
  }

  GIVEN("A default-constructed (zero-capacity) ring buffer")
  {
    champsim::ring_buffer<int> uut{};
    REQUIRE(uut.capacity() == 0);

    WHEN("Elements are inserted through the growing calls")
    {
      uut.push_back_grow(1);
      auto& emplaced = uut.emplace_back_grow(2);

      THEN("The buffer grows from nothing and preserves order")
      {
        REQUIRE(uut.size() == 2);
        REQUIRE(uut.front() == 1);
        REQUIRE(emplaced == 2);
      }
    }
  }
}

SCENARIO("Ring buffer references are stable for an element's residency")
{
  GIVEN("A ring buffer whose elements are referenced externally")
  {
    champsim::ring_buffer<int> uut{3};
    uut.push_back(10);
    uut.push_back(20);
    int* second = &uut[1];

    WHEN("The buffer wraps through pushes and pops around the reference")
    {
      uut.push_back(30);
      uut.pop_front();
      uut.push_back(40); // reuses the popped slot

      THEN("The reference still designates the same element")
      {
        REQUIRE(second == &uut.front());
        REQUIRE(*second == 20);
      }
    }
  }
}
