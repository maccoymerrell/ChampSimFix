#ifndef PREFETCHER_NC_H
#define PREFETCHER_NC_H

#include <array>
#include <bitset>
#include <cstdint>
#include <vector>

#include "champsim.h"
#include "modules.h"
#include "msl/lru_table.h"




class next_column : public champsim::modules::prefetcher
{
  static constexpr int PREFETCH_DEGREE = 1;
  static constexpr int DRAM_BANKS = 32;

public:
  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in);

};


#endif