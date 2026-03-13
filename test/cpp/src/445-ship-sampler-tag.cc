#include <catch.hpp>

#include "../replacement/ship/ship.h"
#include "defaults.hpp"

TEST_CASE("SHIP sampler matches at cache block granularity")
{
  /*
   * The SHIP sampler compares addresses using champsim::block_number so that
   * two addresses in the same cache block are treated as the same entry.
   *
   * With BLOCK_SIZE=64 (LOG2_BLOCK_SIZE=6):
   *   block_number{address{0}}  == block_number{address{32}}  (same block)
   *   block_number{address{0}}  != block_number{address{64}}  (different blocks)
   *
   * A buggy implementation that uses a shamt based on the sampler size could
   * draw false distinctions within the same block, treating two accesses to
   * the same cache line as different entries.
   */

  CACHE cache{champsim::cache_builder{champsim::defaults::default_l1d}
                  .name("445-ship-tag-test")
                  .sets(8)
                  .ways(8)
                  .replacement<ship>()};

  auto* model = dynamic_cast<CACHE::replacement_module_model<ship>*>(cache.repl_module_pimpl.get());
  REQUIRE(model != nullptr);
  auto& uut = std::get<ship>(model->intern_);

  REQUIRE(uut.NUM_SET == 8);
  REQUIRE(uut.NUM_WAY == 8);

  champsim::address ip{100};
  auto SHCT_idx = ip.slice_lower<champsim::data::bits{32}>().to<std::size_t>() % ship::SHCT_PRIME;

  SECTION("Same-block addresses are recognized as a sampler hit")
  {
    // Addresses 0 and 32 are both within cache block 0 (BLOCK_SIZE=64)
    champsim::address addr1{0};
    champsim::address addr2{32};

    // First access: sampler miss, stores entry for block 0
    uut.update_replacement_state(0, 0, 0, addr1, ip, champsim::address{}, access_type::LOAD, 0);

    // Second access: same block → sampler HIT → SHCT decremented (clamped at 0)
    uut.update_replacement_state(0, 0, 0, addr2, ip, champsim::address{}, access_type::LOAD, 0);

    // Sampler hit: SHCT was 0, decremented → clamped to 0
    // A buggy shamt would treat these as different → sampler miss → SHCT incremented to 1
    CHECK(uut.SHCT[0][SHCT_idx].value() == 0);
  }

  SECTION("Different-block addresses are recognized as a sampler miss")
  {
    // Addresses 0 and 64 are in different cache blocks (block 0 vs block 1)
    champsim::address addr1{0};
    champsim::address addr2{64};

    // First access: sampler miss, stores entry for block 0
    uut.update_replacement_state(0, 0, 0, addr1, ip, champsim::address{}, access_type::LOAD, 0);

    // Second access: different block → sampler MISS → evicts old entry (used=false) → SHCT++
    uut.update_replacement_state(0, 0, 0, addr2, ip, champsim::address{}, access_type::LOAD, 0);

    // Sampler miss: old entry (block 0, used=false) evicted → SHCT incremented to 1
    CHECK(uut.SHCT[0][SHCT_idx].value() == 1);
  }
}
