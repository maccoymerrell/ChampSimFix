#include "next_column.h"
#include "cache.h"

uint32_t next_column::prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in)
{
  if(!cache_hit) {
    // Calculate the next column address in the same bank
    for(int i = 1; i <= PREFETCH_DEGREE; i++) {
        champsim::address prefetch_addr = champsim::address{champsim::block_number{addr} + (DRAM_BANKS)*i};
        // Issue the prefetch
        if(champsim::page_number{prefetch_addr} != champsim::page_number{addr})
            break; // Do not cross page
        prefetch_line(prefetch_addr, true, metadata_in);
    }
  }
  return metadata_in;
}

uint32_t next_column::prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}