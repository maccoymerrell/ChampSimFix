#include "asd.h"

uint32_t asd::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  //if(useful_prefetch)
  //  useful[cpu]++;
  //if(cache_hit != 0)
  //  return metadata_in;

  auto [depth,stride] = ASD_Modules.at(cpu).get_prefetch_depth(addr);
  //modulate depth according to MSHR occupancy
  if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.9 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 8, depth);
  else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.7 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 4, depth);
  else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.5 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 2, depth);

  if(!intern_->warmup) {
    if(stride < H_BINS)
      ASD_Modules.at(cpu).pf_strides.at(stride)++;
      ASD_Modules.at(cpu).pf_depths.at(depth)++;
  }

  for(int i = stride; i < depth; i += stride) {
    champsim::address pf_addr = champsim::address{champsim::block_number{addr} + i};
    if(champsim::page_number{pf_addr} != champsim::page_number{addr})
      continue;
    //fmt::print("Issuing prefetch for address: {}\n",pf_addr);
    //filter out redundant prefetches
    bool filter = ASD_Modules.at(cpu).Filter.check(pf_addr,intern_->current_cycle(),false);
    bool success = true;
    if(!filter) {
      success = prefetch_line(pf_addr,true,cpu,0,false,false);
    }
    if(success)
      ASD_Modules.at(cpu).Filter.check(pf_addr,intern_->current_cycle(),true);
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
      ASD_Modules.at(i).pf_strides.at(j) = 0;
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
    fmt::print("\tCPU: {}\n",i);
    for(int j = 0; j < H_BINS; j++) {
      fmt::print("\t\t{} : {}\n",j,ASD_Modules.at(i).pf_depths.at(j));
    }
  }

  fmt::print("ASD Prefetch Strides by Core:\n");

  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("\tCPU: {}\n",i);
    for(int j = 0; j < H_BINS; j++) {
      fmt::print("\t\t{} : {}\n",j,ASD_Modules.at(i).pf_strides.at(j));
    }
  }
}

uint32_t asd::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void asd::prefetcher_cycle_operate() {

}