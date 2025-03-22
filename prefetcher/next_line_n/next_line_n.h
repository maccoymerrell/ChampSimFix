#ifndef PREFETCHER_NEXT_LINE_N_H
#define PREFETCHER_NEXT_LINE_N_H

#include <cstdint>

#include "address.h"
#include "modules.h"
#include "cache.h"
#include "dram_controller.h"

struct next_line_n : public champsim::modules::prefetcher {
  constexpr static uint64_t usefulness_update_period = 1e5;

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  // void prefetcher_initialize();
  // void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) {}
  void prefetcher_cycle_operate();
  std::map<uint32_t,uint64_t> useful;
  std::map<uint32_t,uint64_t> filled;
};

#endif
