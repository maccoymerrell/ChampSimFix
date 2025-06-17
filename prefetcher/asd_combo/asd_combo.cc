#include "asd_combo.h"

uint32_t asd_combo::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  ASD_COL_Modules.at(cpu).add_to_pagemap(addr);
  ASD_Modules.at(cpu).add_to_pagemap(addr);

  if(metadata_in != ASD_ID) {
    auto [depth_base, stride_base] = ASD_Modules.at(cpu).get_prefetch_depth(addr);
    //modulate depth according to MSHR occupancy
    if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.9 * intern_->get_mshr_size())
      depth_base = std::min(asd::MAX_PREFETCH / 8, depth_base);
    else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.7 * intern_->get_mshr_size())
      depth_base = std::min(asd::MAX_PREFETCH / 4, depth_base);
    else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.5 * intern_->get_mshr_size())
      depth_base = std::min(asd::MAX_PREFETCH / 2, depth_base);
    
    if(!intern_->warmup) {
      if(stride_base < num_bins)
        ASD_Modules.at(cpu).pf_strides.at(stride_base)++;
        ASD_Modules.at(cpu).pf_depths.at(depth_base)++;
    }

    for(int i = stride_base; i < depth_base; i+= stride_base) {

      champsim::address pf_addr = champsim::address{champsim::block_number{addr} + i};
      if(champsim::page_number{pf_addr} != champsim::page_number{addr})
        continue;

      bool pm = ASD_Modules.at(cpu).check_pagemap(pf_addr);
      bool success = true;
      if(!pm) {
        success = prefetch_line(pf_addr,true,cpu,ASD_ID,false,true);
      } else if (!intern_->warmup) {
        ASD_Modules.at(cpu).filtered_prefetches++;
      }
      if(success) {
        ASD_Modules.at(cpu).add_to_pagemap(pf_addr);
        ASD_COL_Modules.at(cpu).add_to_pagemap(pf_addr);
      }
    }
  }
    
  if(cache_hit == 0) {
    auto [depth_col,stride_col] = ASD_COL_Modules.at(cpu).get_prefetch_depth(addr);
    //modulate depth according to MSHR occupancy
    if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.9 * intern_->get_mshr_size())
      depth_col = std::min(asd_col::MAX_PREFETCH / 8, depth_col);
    else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.7 * intern_->get_mshr_size())
      depth_col = std::min(asd_col::MAX_PREFETCH / 4, depth_col);
    else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.5 * intern_->get_mshr_size())
      depth_col = std::min(asd_col::MAX_PREFETCH / 2, depth_col);

    if(!intern_->warmup) {
      if(stride_col < num_bins)
        ASD_COL_Modules.at(cpu).pf_strides.at(stride_col)++;
        ASD_COL_Modules.at(cpu).pf_depths.at(depth_col)++;
    }

    
    std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
    for(int i = stride_col; i < depth_col; i += stride_col) {
      champsim::address pf_addr_col = compose_base_and_column(addr,col + i);
      if(champsim::page_number{pf_addr_col} != champsim::page_number{addr})
        continue;
      
        bool pm_col = ASD_COL_Modules.at(cpu).check_pagemap(pf_addr_col);
        bool success_col = true;
        if(!pm_col) {
          success_col = prefetch_line(pf_addr_col,true,cpu,0,false,false);
        } else if (!intern_->warmup) {
          ASD_COL_Modules.at(cpu).filtered_prefetches++;
        }
        if(success_col) {
          ASD_COL_Modules.at(cpu).add_to_pagemap(pf_addr_col);
          ASD_Modules.at(cpu).add_to_pagemap(pf_addr_col);
        }
    }
  }
  return metadata_in;
}

void asd_combo::prefetcher_initialize() {

  num_bins = (4096 / BLOCK_SIZE);
  for(int i = 0; i < NUM_CPUS; i++) {
    ASD_Modules.emplace_back(asd::ASD_Module(num_bins));
    ASD_COL_Modules.emplace_back(asd_col::ASD_COL_Module(num_bins,MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers()));
  }
  for(int i = 0; i < NUM_CPUS; i++) {
    for(int j = 0; j < num_bins; j++) {
      ASD_COL_Modules.at(i).pf_depths.at(j) = 0;
      ASD_COL_Modules.at(i).pf_strides.at(j) = 0;
      ASD_Modules.at(i).pf_depths.at(j) = 0;
      ASD_Modules.at(i).pf_strides.at(j) = 0;
    }
  }

  for(int i = 0; i < 64; i ++) {
    if(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(champsim::address{1ul << i}) != 0)
      column_bits.push_back(i);
  }
  fmt::print("[{}] Initialized ASD-COMBO, Column Bits are: {}\n",intern_->NAME, fmt::join(column_bits, ","));
}

void asd_combo::prefetcher_final_stats() {
  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("ASD for Core {}:\n",i);
    ASD_Modules.at(i).print_stats();
    fmt::print("ASD-COL for Core {}:\n",i);
    ASD_COL_Modules.at(i).print_stats();
  }
}

uint32_t asd_combo::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void asd_combo::prefetcher_cycle_operate() {

}