#include <catch.hpp>

#include "channel.h"
#include "defaults.hpp"

TEST_CASE("The occupancies of an empty channel are zero")
{
  champsim::channel uut{champsim::modules::ModuleBuilder{"test_channel_38_0", "DEFAULT_CHANNEL", champsim::defaults::default_channel()}};

  CHECK(uut.rq_occupancy() == 0);
  CHECK(uut.wq_occupancy() == 0);
  CHECK(uut.pq_occupancy() == 0);
}

TEST_CASE("Adding something to the channel's RQ increases its occupancy")
{
  champsim::channel uut{champsim::modules::ModuleBuilder{"test_channel_38_1", "DEFAULT_CHANNEL", champsim::defaults::default_channel()}};

  champsim::channel::request_type packet{};
  packet.address = champsim::address{0xdeadbeef};
  uut.add_rq(packet);

  CHECK(uut.rq_occupancy() == 1);
  CHECK(uut.wq_occupancy() == 0);
  CHECK(uut.pq_occupancy() == 0);
}

TEST_CASE("Adding something to the channel's WQ increases its occupancy")
{
  champsim::channel uut{champsim::modules::ModuleBuilder{"test_channel_38_2", "DEFAULT_CHANNEL", champsim::defaults::default_channel()}};

  champsim::channel::request_type packet{};
  packet.address = champsim::address{0xdeadbeef};
  uut.add_wq(packet);

  CHECK(uut.rq_occupancy() == 0);
  CHECK(uut.wq_occupancy() == 1);
  CHECK(uut.pq_occupancy() == 0);
}

TEST_CASE("Adding something to the channel's PQ increases its occupancy")
{
  champsim::channel uut{champsim::modules::ModuleBuilder{"test_channel_38_3", "DEFAULT_CHANNEL", champsim::defaults::default_channel()}};

  champsim::channel::request_type packet{};
  packet.address = champsim::address{0xdeadbeef};
  uut.add_pq(packet);

  CHECK(uut.rq_occupancy() == 0);
  CHECK(uut.wq_occupancy() == 0);
  CHECK(uut.pq_occupancy() == 1);
}

TEST_CASE("A const channel can return its RQ size")
{
  auto rq_size = GENERATE(as<std::size_t>(), 1, 8, 32, 256);
  champsim::modules::ModuleBuilder channel_builder{"test_channel_38_4", "DEFAULT_CHANNEL", champsim::defaults::default_channel()};
  channel_builder.add_parameter("rq_size", rq_size);
  const champsim::channel uut{channel_builder};

  REQUIRE(uut.rq_size() == rq_size);
}

TEST_CASE("A const channel can return its WQ size")
{
  auto wq_size = GENERATE(as<std::size_t>(), 1, 8, 32, 256);
  champsim::modules::ModuleBuilder channel_builder{"test_channel_38_5", "DEFAULT_CHANNEL", champsim::defaults::default_channel()};
  channel_builder.add_parameter("wq_size", wq_size);
  const champsim::channel uut{channel_builder};

  REQUIRE(uut.wq_size() == wq_size);
}

TEST_CASE("A const channel can return its PQ size")
{
  auto pq_size = GENERATE(as<std::size_t>(), 1, 8, 32, 256);
  champsim::modules::ModuleBuilder channel_builder{"test_channel_38_6", "DEFAULT_CHANNEL", champsim::defaults::default_channel()};
  channel_builder.add_parameter("pq_size", pq_size);
  const champsim::channel uut{channel_builder};

  REQUIRE(uut.pq_size() == pq_size);
}
