#include "next_line.h"

uint32_t next_line::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  //if(useful_prefetch)
  //  useful[cpu]++;
  champsim::block_number pf_addr{addr};
  prefetch_line(champsim::address{pf_addr + 1}, true, cpu, ip, metadata_in);
  return metadata_in;
}

uint32_t next_line::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  //if(prefetch)
  //  filled[cpu]++;
  return metadata_in;
}

void next_line::prefetcher_cycle_operate() {
  if((intern_->current_cycle() + 1) % usefulness_update_period == 0) {
    for (auto& cp : useful) {
      //uint64_t usfl = cp.second;
      //uint64_t fill = filled[cp.first];
      //intern_->report_prefetch_usefulness(cp.first, (usfl+1)/(double)(fill+1));
      //useful[cp.first] = 0;
      //filled[cp.first] = 0;
    }
  }
}