#include <catch.hpp>

#include "access_type.h"
#include "event_counter.h"

namespace
{
// Identity, not the generic-matching operator==.
bool same(access_type a, access_type b) { return static_cast<unsigned>(a) == static_cast<unsigned>(b); }
} // namespace

TEST_CASE("A generic access type matches both of its specializations")
{
  REQUIRE(access_type::LOAD == access_type::INSTRUCTION_LOAD);
  REQUIRE(access_type::LOAD == access_type::DATA_LOAD);
  REQUIRE(access_type::INSTRUCTION_LOAD == access_type::LOAD);
  REQUIRE(access_type::DATA_LOAD == access_type::LOAD);
  REQUIRE(access_type::PREFETCH == access_type::INSTRUCTION_PREFETCH);
  REQUIRE(access_type::PREFETCH == access_type::DATA_PREFETCH);
}

TEST_CASE("The two specializations of a generic type differ")
{
  REQUIRE(access_type::INSTRUCTION_LOAD != access_type::DATA_LOAD);
  REQUIRE(access_type::INSTRUCTION_PREFETCH != access_type::DATA_PREFETCH);
  REQUIRE_FALSE(access_type::DATA_LOAD == access_type::INSTRUCTION_LOAD);
}

TEST_CASE("Types from different generic families never match")
{
  REQUIRE(access_type::LOAD != access_type::PREFETCH);
  REQUIRE(access_type::INSTRUCTION_LOAD != access_type::INSTRUCTION_PREFETCH);
  REQUIRE(access_type::DATA_LOAD != access_type::DATA_PREFETCH);
  REQUIRE(access_type::DATA_LOAD != access_type::RFO);
  REQUIRE(access_type::INSTRUCTION_PREFETCH != access_type::WRITE);
  REQUIRE(access_type::RFO == access_type::RFO);
  REQUIRE(access_type::TRANSLATION != access_type::LOAD);
}

TEST_CASE("Each access type knows its generic type and side")
{
  REQUIRE(same(generic_access_type(access_type::INSTRUCTION_LOAD), access_type::LOAD));
  REQUIRE(same(generic_access_type(access_type::DATA_LOAD), access_type::LOAD));
  REQUIRE(same(generic_access_type(access_type::INSTRUCTION_PREFETCH), access_type::PREFETCH));
  REQUIRE(same(generic_access_type(access_type::DATA_PREFETCH), access_type::PREFETCH));
  REQUIRE(same(generic_access_type(access_type::RFO), access_type::RFO));
  REQUIRE(same(generic_access_type(access_type::LOAD), access_type::LOAD));

  REQUIRE(is_instruction_access(access_type::INSTRUCTION_LOAD));
  REQUIRE(is_instruction_access(access_type::INSTRUCTION_PREFETCH));
  REQUIRE_FALSE(is_instruction_access(access_type::DATA_LOAD));
  REQUIRE_FALSE(is_instruction_access(access_type::DATA_PREFETCH));
  REQUIRE_FALSE(is_instruction_access(access_type::LOAD));
  REQUIRE_FALSE(is_instruction_access(access_type::RFO));
}

TEST_CASE("Every access type name parses back to exactly that type")
{
  for (std::size_t i = 0; i < access_type_names.size(); ++i) {
    auto type = static_cast<access_type>(i);
    REQUIRE(same(access_type_from_string(access_type_names.at(i)), type));
  }
  REQUIRE(same(access_type_from_string("NOT_A_TYPE"), access_type::NUM_TYPES));
}

TEST_CASE("An event counter keyed by access type keeps each type's count separate")
{
  champsim::stats::event_counter<std::pair<access_type, std::size_t>> uut{};
  uut.increment({access_type::DATA_LOAD, 0});
  uut.increment({access_type::INSTRUCTION_LOAD, 0});
  uut.increment({access_type::INSTRUCTION_LOAD, 0});

  REQUIRE(uut.value_or({access_type::DATA_LOAD, 0}, 0) == 1);
  REQUIRE(uut.value_or({access_type::INSTRUCTION_LOAD, 0}, 0) == 2);
  REQUIRE(uut.value_or({access_type::LOAD, 0}, 0) == 0);

  uut.increment({access_type::LOAD, 0});
  REQUIRE(uut.value_or({access_type::LOAD, 0}, 0) == 1);
  REQUIRE(uut.value_or({access_type::DATA_LOAD, 0}, 0) == 1);
  REQUIRE(uut.total() == 4);
}
