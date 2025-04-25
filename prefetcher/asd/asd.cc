#include "asd.h"

uint32_t asd::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  //if(useful_prefetch)
  //  useful[cpu]++;
  
  std::size_t depth = ASD_Modules.at(cpu).get_prefetch_depth(addr);
  for(int i = 0; i < depth; i++) {
    champsim::address pf_addr = champsim::address{champsim::block_number{addr} + i + 1};
    if(!intern_->warmup)
      ASD_Modules.at(cpu).pf_depths.at(i)++;
    //fmt::print("Issuing prefetch for address: {}\n",pf_addr);
    prefetch_line(pf_addr,true,cpu,0,false,false);
  }
  return metadata_in;
}

void asd::prefetcher_initialize() {
  for(int i = 0; i < NUM_CPUS; i++) {
    ASD_Modules.emplace_back(ASD_Module{});
  }
  for(int i = 0; i < NUM_CPUS; i++) {
    for(int j = 0; j < H_BINS; j++) {
      ASD_Modules.at(i).pf_depths.at(j) = 0;
    }
  }
}

void asd::prefetcher_final_stats() {
  fmt::print("ASD Histograms by Core:\n");

  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("\tCPU: {} epoch: {}\n",i,ASD_Modules.at(i).epoch);
    for(int j = 0; j < H_BINS; j++) {
      fmt::print("\t\t{} : {}\n",j + 1,ASD_Modules.at(i).ActiveHist.get_bin_occu(j+1));
    }
  }
  fmt::print("ASD Prefetch Depths by Core:\n");

  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("\tCPU: {}\n",i,ASD_Modules.at(i).epoch);
    for(int j = 0; j < H_BINS; j++) {
      fmt::print("\t\t{} : {}\n",j + 1,ASD_Modules.at(i).pf_depths.at(j));
    }
  }
}

uint32_t asd::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void asd::prefetcher_cycle_operate() {

}