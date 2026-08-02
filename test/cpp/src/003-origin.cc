#include <catch.hpp>

#include "origin.h"

TEST_CASE("An origin exposes the same identities under canonical and domain names")
{
  champsim::origin uut{3, 7};

  // The aliases are the same identity: cpu() IS the consumer, asid() IS the source
  REQUIRE(uut.consumer() == 3);
  REQUIRE(uut.cpu() == uut.consumer());
  REQUIRE(uut.source() == 7);
  REQUIRE(uut.asid() == uut.source());
}

TEST_CASE("A default origin is invalid in both coordinates")
{
  champsim::origin uut{};
  REQUIRE_FALSE(uut.has_consumer());
  REQUIRE_FALSE(uut.has_source());
  REQUIRE(uut.consumer() == champsim::origin::invalid_id);
  REQUIRE(uut.source() == champsim::origin::invalid_id);
}

TEST_CASE("Origin derivation helpers replace one coordinate and keep the other")
{
  champsim::origin base{1, 2};

  auto moved = base.with_consumer(9); // e.g. a source migrating between consumers
  REQUIRE(moved.consumer() == 9);
  REQUIRE(moved.source() == 2);

  auto respaced = base.with_source(5); // e.g. a trace record overriding the source id
  REQUIRE(respaced.consumer() == 1);
  REQUIRE(respaced.source() == 5);
}

TEST_CASE("Origins compare by both coordinates")
{
  REQUIRE(champsim::origin{1, 2} == champsim::origin{1, 2});
  REQUIRE(champsim::origin{1, 2} != champsim::origin{1, 3});
  REQUIRE(champsim::origin{1, 2} != champsim::origin{2, 2});
  REQUIRE(champsim::origin{1, 2} < champsim::origin{1, 3});
  REQUIRE(champsim::origin{1, 3} < champsim::origin{2, 0});
}
