#include "no.h"

uint32_t no::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                      uint32_t metadata_in)
{
  // assert(addr == ip); // Invariant for instruction prefetchers
  if(intern_->NAME.compare("LLC") == 0 && type == access_type::LOAD && !intern_->warmup) {
    //dump data
    //fmt::print("[LLC] Hit:{} Address:{} IP:{} Row:{} Column:{} Rowbuffer:{} Cycle:{}\n",cache_hit,addr,ip,MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr),MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr), MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr),intern_->current_cycle());
  }
  return metadata_in;
}

uint32_t no::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}
