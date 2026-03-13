#include <catch.hpp>

#include "../replacement/ship/ship.h"
#include "defaults.hpp"
#include "mocks.hpp"

TEST_CASE("SHIP sampler correctly distinguishes addresses that differ only in low tag bits")
{
  /*
   * The SHIP sampler compares addresses by slicing off the lower bits used
   * to index into the sampler. The shift amount (shamt) should be based only
   * on the number of sampler sets (lg2(num_samples)), not also on lg2(NUM_WAY).
   *
   * With NUM_SET=8, NUM_WAY=8, OFFSET_BITS=0:
   *   sample_rate = 4, num_samples = 2
   *   Correct shamt = lg2(2) = 1
   *   Buggy shamt   = lg2(2) + lg2(8) = 4   (would mask low tag bits)
   *
   * Addresses 0 and 8 are both in cache set 0 (8 % 8 == 0) with different tags.
   * With a buggy shamt of 4: (0 >> 4) == (8 >> 4) == 0, causing a false match.
   * With the correct shamt of 1: (0 >> 1) == 0 != 4 == (8 >> 1), correctly no match.
   */

  do_nothing_MRC mock_ll;
  to_rq_MRP mock_ul;

  // Create a cache with NUM_SET=8, NUM_WAY=8, OFFSET_BITS=0
  // The SHIP instance reads NUM_SET and NUM_WAY from the cache pointer.
  CACHE cache{champsim::cache_builder{champsim::defaults::default_l1d}
                  .name("445-ship-tag-test")
                  .sets(8)
                  .ways(8)
                  .upper_levels({&mock_ul.queues})
                  .lower_level(&mock_ll.queues)
                  .offset_bits(champsim::data::bits{})
                  .replacement<lru>()};

  cache.initialize();
  cache.warmup = false;
  cache.begin_phase();

  // Create a standalone SHIP instance for direct unit testing
  ship uut{&cache};

  REQUIRE(uut.NUM_SET == 8);
  REQUIRE(uut.NUM_WAY == 8);

  // Set 0 is sampled (category 0 for sample_rate=4)
  // Addresses 0 and 8 both map to cache set 0 but have different tags
  champsim::address addr1{0};
  champsim::address addr2{8};
  champsim::address ip{100};

  auto SHCT_idx = ip.slice_lower<champsim::data::bits{32}>().to<std::size_t>() % ship::SHCT_PRIME;

  // First access: addr1 with ip=100, set 0 (sampler MISS, new entry stored)
  uut.update_replacement_state(0, 0, 0, addr1, ip, champsim::address{}, access_type::LOAD, 0);

  // Second access: addr2 with ip=100, set 0
  //   Correct behavior: sampler MISS (addr2 != addr1), evicts the entry for
  //   addr1 (which was not reused, used=false) → SHCT[ip] incremented
  //
  //   Buggy behavior: false sampler HIT (addr2 matches addr1 under wrong shamt),
  //   SHCT[ip] decremented (but clamped at 0)
  uut.update_replacement_state(0, 0, 0, addr2, ip, champsim::address{}, access_type::LOAD, 0);

  // With the correct fix, the second access is a sampler miss.
  // The evicted entry (addr1, ip=100) had used=false, so SHCT[ip] is incremented.
  // SHCT[0][SHCT_idx] should be 1 (incremented from 0).
  //
  // With the bug, the second access falsely matches entry for addr1,
  // SHCT[ip] is decremented from 0 → clamped to 0.
  // SHCT[0][SHCT_idx] would remain 0.
  CHECK(uut.SHCT[0][SHCT_idx].value() == 1);
}
