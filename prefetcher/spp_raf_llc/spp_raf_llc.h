#ifndef PREFETCHER_RAF_LLC_H
#define PREFETCHER_RAF_LLC_H

#include <cstdint>

#include "address.h"
#include "modules.h"

struct spp_raf_llc : public champsim::modules::prefetcher {
  static void mitigation_issued(champsim::address addr);
  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);

  // void prefetcher_initialize();
  // void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) {}
  // void prefetcher_cycle_operate() {}
  // void prefetcher_final_stats() {}
};

#endif
