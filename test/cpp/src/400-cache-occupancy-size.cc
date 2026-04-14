#include <catch.hpp>

#include "cache.h"
#include "channel.h"
#include "defaults.hpp"
#include "mocks.hpp"

TEST_CASE("A cache can examine the RQ sizes of its channels")
{
  auto rq_size = GENERATE(as<std::size_t>{}, 1, 8, 32, 256);
  auto queue_count = GENERATE(as<std::size_t>{}, 1, 2, 3);

  std::vector<std::size_t> queue_sizes(queue_count, 0);
  std::iota(std::begin(queue_sizes), std::end(queue_sizes), rq_size);

  std::vector<champsim::channel> queues;
  for (std::size_t i = 0; i < queue_count; ++i)
    queues.emplace_back(champsim::modules::ModuleBuilder{"channel_rq_" + std::to_string(i), "default_channel", champsim::defaults::default_channel()}.add_parameter("rq_size", static_cast<std::size_t>(queue_sizes[i])).add_parameter("pq_size", static_cast<std::size_t>(32)).add_parameter("wq_size", static_cast<std::size_t>(32)));
  std::vector<champsim::modules::channel_module*> queue_ptrs;
  std::transform(std::begin(queues), std::end(queues), std::back_inserter(queue_ptrs), [](auto& q) { return &q; });

  CACHE uut{champsim::modules::ModuleBuilder{"uut_cache", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))}.add_parameter("upper_levels", std::move(queue_ptrs))};

  REQUIRE_THAT(uut.get_rq_size(), Catch::Matchers::RangeEquals(queue_sizes));
}

TEST_CASE("A cache can examine the WQ sizes of its channels")
{
  auto wq_size = GENERATE(as<std::size_t>{}, 1, 8, 32, 256);
  auto queue_count = GENERATE(as<std::size_t>{}, 1, 2, 3);

  std::vector<std::size_t> queue_sizes(queue_count, 0);
  std::iota(std::begin(queue_sizes), std::end(queue_sizes), wq_size);

  std::vector<champsim::channel> queues;
  for (std::size_t i = 0; i < queue_count; ++i)
    queues.emplace_back(champsim::modules::ModuleBuilder{"channel_wq_" + std::to_string(i), "default_channel", champsim::defaults::default_channel()}.add_parameter("rq_size", static_cast<std::size_t>(32)).add_parameter("pq_size", static_cast<std::size_t>(32)).add_parameter("wq_size", static_cast<std::size_t>(queue_sizes[i])));
  std::vector<champsim::modules::channel_module*> queue_ptrs;
  std::transform(std::begin(queues), std::end(queues), std::back_inserter(queue_ptrs), [](auto& q) { return &q; });

  CACHE uut{champsim::modules::ModuleBuilder{"uut_cache", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))}.add_parameter("upper_levels", std::move(queue_ptrs))};

  REQUIRE_THAT(uut.get_wq_size(), Catch::Matchers::RangeEquals(queue_sizes));
}

TEST_CASE("A cache can examine the PQ sizes of its channels")
{
  auto pq_size = GENERATE(as<std::size_t>{}, 1, 8, 32, 256);
  auto queue_count = GENERATE(as<std::size_t>{}, 1, 2, 3);

  std::vector<std::size_t> queue_sizes(queue_count, 0);
  std::iota(std::begin(queue_sizes), std::end(queue_sizes), pq_size);

  std::vector<champsim::channel> queues;
  for (std::size_t i = 0; i < queue_count; ++i)
    queues.emplace_back(champsim::modules::ModuleBuilder{"channel_pq_" + std::to_string(i), "default_channel", champsim::defaults::default_channel()}.add_parameter("rq_size", static_cast<std::size_t>(32)).add_parameter("pq_size", static_cast<std::size_t>(queue_sizes[i])).add_parameter("wq_size", static_cast<std::size_t>(32)));
  std::vector<champsim::modules::channel_module*> queue_ptrs;
  std::transform(std::begin(queues), std::end(queues), std::back_inserter(queue_ptrs), [](auto& q) { return &q; });

  CACHE uut{champsim::modules::ModuleBuilder{"uut_cache", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))}.add_parameter("upper_levels", std::move(queue_ptrs)).add_parameter("pq_size", static_cast<std::size_t>(pq_size + queue_count))};
  queue_sizes.push_back(pq_size + queue_count);

  REQUIRE_THAT(uut.get_pq_size(), Catch::Matchers::RangeEquals(queue_sizes));
}

SCENARIO("A cache can examine the RQ sizes of its channels")
{
  auto queue_count = GENERATE(as<std::size_t>(), 1, 2, 3);
  GIVEN("A cache with " + std::to_string(queue_count) + " upper levels")
  {

    std::vector<to_rq_MRP> queues{queue_count};
    std::vector<champsim::modules::channel_module*> queue_ptrs;
    std::transform(std::begin(queues), std::end(queues), std::back_inserter(queue_ptrs), [](auto& q) { return &q.queues; });

    CACHE uut{champsim::modules::ModuleBuilder{"uut_cache", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))}.add_parameter("upper_levels", std::move(queue_ptrs))};

    std::vector<std::size_t> queue_sizes(queue_count, 0);

    THEN("The initial occupancies are zero") { REQUIRE_THAT(uut.get_rq_occupancy(), Catch::Matchers::RangeEquals(queue_sizes)); }

    for (std::size_t i = 0; i < queue_count; ++i) {
      WHEN("Upper level " + std::to_string(i) + " issues a request")
      {
        // Create a test packet
        champsim::channel::request_type test;
        test.address = champsim::address{0xdeadbeef};

        auto test_result = queues[i].issue(test);
        THEN("This issue is received") { REQUIRE(test_result); }

        THEN("The occupancy is updated")
        {
          queue_sizes[i]++;
          REQUIRE_THAT(uut.get_rq_occupancy(), Catch::Matchers::RangeEquals(queue_sizes));
        }
      }
    }
  }
}

SCENARIO("A cache can examine the WQ sizes of its channels")
{
  auto queue_count = GENERATE(as<std::size_t>(), 1, 2, 3);
  GIVEN("A cache with " + std::to_string(queue_count) + " upper levels")
  {

    std::vector<to_wq_MRP> queues{queue_count};
    std::vector<champsim::modules::channel_module*> queue_ptrs;
    std::transform(std::begin(queues), std::end(queues), std::back_inserter(queue_ptrs), [](auto& q) { return &q.queues; });

    CACHE uut{champsim::modules::ModuleBuilder{"uut_cache", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))}.add_parameter("upper_levels", std::move(queue_ptrs))};

    std::vector<std::size_t> queue_sizes(queue_count, 0);

    THEN("The initial occupancies are zero") { REQUIRE_THAT(uut.get_wq_occupancy(), Catch::Matchers::RangeEquals(queue_sizes)); }

    for (std::size_t i = 0; i < queue_count; ++i) {
      WHEN("Upper level " + std::to_string(i) + " issues a request")
      {
        // Create a test packet
        champsim::channel::request_type test;
        test.address = champsim::address{0xdeadbeef};

        auto test_result = queues[i].issue(test);
        THEN("This issue is received") { REQUIRE(test_result); }

        THEN("The occupancy is updated")
        {
          queue_sizes[i]++;
          REQUIRE_THAT(uut.get_wq_occupancy(), Catch::Matchers::RangeEquals(queue_sizes));
        }
      }
    }
  }
}

SCENARIO("A cache can examine the PQ sizes of its channels")
{
  auto queue_count = GENERATE(as<std::size_t>(), 1, 2, 3);
  GIVEN("A cache with " + std::to_string(queue_count) + " upper levels")
  {

    std::vector<to_pq_MRP> queues{queue_count};
    std::vector<champsim::modules::channel_module*> queue_ptrs;
    std::transform(std::begin(queues), std::end(queues), std::back_inserter(queue_ptrs), [](auto& q) { return &q.queues; });

    CACHE uut{champsim::modules::ModuleBuilder{"uut_cache", "default_cache", champsim::defaults::default_l1d().add_parameter("mshr_size", static_cast<uint32_t>(8))}.add_parameter("upper_levels", std::move(queue_ptrs))};

    std::vector<std::size_t> queue_sizes(queue_count + 1, 0);

    THEN("The initial occupancies are zero") { REQUIRE_THAT(uut.get_pq_occupancy(), Catch::Matchers::RangeEquals(queue_sizes)); }

    for (std::size_t i = 0; i < queue_count; ++i) {
      WHEN("Upper level " + std::to_string(i) + " issues a request")
      {
        // Create a test packet
        champsim::channel::request_type test;
        test.address = champsim::address{0xdeadbeef};

        auto test_result = queues[i].issue(test);
        THEN("This issue is received") { REQUIRE(test_result); }

        THEN("The occupancy is updated")
        {
          queue_sizes[i]++;
          REQUIRE_THAT(uut.get_pq_occupancy(), Catch::Matchers::RangeEquals(queue_sizes));
        }
      }
    }
  }
}
