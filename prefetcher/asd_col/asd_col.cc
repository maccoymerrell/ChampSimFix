#include "asd_col.h"

uint32_t asd_col::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  //if(useful_prefetch)
  //  useful[cpu]++;
  //if(cache_hit != 0)
  //  return metadata_in;
  ASD_COL_Modules.at(cpu).add_to_pagemap(addr);

  auto [depth,stride] = ASD_COL_Modules.at(cpu).get_prefetch_depth(addr);
  //modulate depth according to MSHR occupancy
  if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.9 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 8, depth);
  else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.7 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 4, depth);
  else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.5 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 2, depth);

  if(!intern_->warmup) {
    if(stride < num_bins)
      ASD_COL_Modules.at(cpu).pf_strides.at(stride)++;
      ASD_COL_Modules.at(cpu).pf_depths.at(depth)++;
  }

  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  for(int i = stride; i < depth; i += stride) {
    champsim::address pf_addr = compose_base_and_column(addr,col + i);
    if(champsim::page_number{pf_addr} != champsim::page_number{addr})
      continue;
    //fmt::print("Issuing prefetch for address: {}\n",pf_addr);
    //filter out redundant prefetches
    bool pm = ASD_COL_Modules.at(cpu).check_pagemap(pf_addr);
    bool success = true;
    if(!pm) {
      success = prefetch_line(pf_addr,true,cpu,ip,0,false,false);
    } else if (!intern_->warmup) {
      ASD_COL_Modules.at(cpu).filtered_prefetches++;
    }
    if(success)
      ASD_COL_Modules.at(cpu).add_to_pagemap(pf_addr);
  }
  return metadata_in;
}

void asd_col::prefetcher_initialize() {

  num_bins = FORCE_4KiB_PAGES ? (4096 / BLOCK_SIZE) : (PAGE_SIZE / BLOCK_SIZE);
  for(int i = 0; i < NUM_CPUS; i++) {
    ASD_COL_Modules.emplace_back(ASD_COL_Module(num_bins,MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers()));
  }
  for(int i = 0; i < NUM_CPUS; i++) {
    for(int j = 0; j < num_bins; j++) {
      ASD_COL_Modules.at(i).pf_depths.at(j) = 0;
      ASD_COL_Modules.at(i).pf_strides.at(j) = 0;
    }
  }

  for(int i = 0; i < 64; i ++) {
    if(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(champsim::address{1ul << i}) != 0)
      column_bits.push_back(i);
  }
  fmt::print("[{}] Initialized ASD-COL, Column Bits are: {}\n",intern_->NAME, fmt::join(column_bits, ","));
}

void asd_col::prefetcher_final_stats() {
  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("ASD for Core {}:\n",i);
    ASD_COL_Modules.at(i).print_stats();
  }
}

uint32_t asd_col::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void asd_col::prefetcher_cycle_operate() {

}

void asd_col::ASD_COL_Module::add_to_pagemap(champsim::address addr) {
  uint64_t pn = addr.to<uint64_t>() >> (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE);
  uint64_t bn = (addr.to<uint64_t>() % (1 << (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE))) >> LOG2_BLOCK_SIZE;
  page_map pm(pn);

  auto entry = page_map_table.check_hit(pm);
  if(entry.has_value()) {
    entry->bits.at(bn) = page_map::PM_BASE;
    page_map_table.fill(entry.value());
  } else {
    pm.bits.at(bn) = page_map::PM_BASE;
    page_map_table.fill(pm);
  }
}

bool asd_col::ASD_COL_Module::check_pagemap(champsim::address addr) {
  uint64_t pn = addr.to<uint64_t>() >> (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE);
  uint64_t bn = (addr.to<uint64_t>() % (1 << (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE))) >> LOG2_BLOCK_SIZE;
  page_map pm(pn);
  auto entry = page_map_table.check_hit(pm);
  if(entry.has_value()) {
    if(entry->bits.at(bn) == 0) {
      return false;
    } else {
      entry->bits.at(bn)--;
      page_map_table.fill(entry.value());
      return true;
    }
  }
  return false;
}

void asd_col::ASD_COL_Module::remove_from_pagemap(champsim::address addr) {
  uint64_t pn = addr.to<uint64_t>() >> (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE);
  uint64_t bn = (addr.to<uint64_t>() % (1 << (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE))) >> LOG2_BLOCK_SIZE;
  page_map pm(pn);
  auto entry = page_map_table.check_hit(pm);
  if(entry.has_value()) {
    entry->bits.at(bn) = 0;
    page_map_table.fill(entry.value());
  }
}