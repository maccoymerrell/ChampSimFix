#include "hasd.h"

uint32_t hasd::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  //if(useful_prefetch)
  //  useful[cpu]++;
  //if(cache_hit != 0)
  //  return metadata_in;
  ASD_Modules.at(cpu).add_to_pagemap(addr);

  auto [depth,stride] = ASD_Modules.at(cpu).get_prefetch_depth(addr, ip);
  //modulate depth according to MSHR occupancy
  if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.9 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 8, depth);
  else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.7 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 4, depth);
  else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.5 * intern_->get_mshr_size())
    depth = std::min(MAX_PREFETCH / 2, depth);

  for(int i = 1; i <= depth; i++) {
    champsim::address pf_addr = champsim::address{champsim::block_number{addr} + i*stride};
    if(champsim::page_number{pf_addr} != champsim::page_number{addr})
      continue;
    //fmt::print("Issuing prefetch for address: {}\n",pf_addr);
    //filter out redundant prefetches
    bool pm = ASD_Modules.at(cpu).check_pagemap(pf_addr);
    bool success = true;
    if(!pm) {
      success = prefetch_line(pf_addr,true,cpu,ip,0,false,false);
    } else if (!intern_->warmup) {
      ASD_Modules.at(cpu).filtered_prefetches++;
    }
    if(success)
      ASD_Modules.at(cpu).add_to_pagemap(pf_addr);
  }
  return metadata_in;
}

void hasd::prefetcher_initialize() {

  num_bins = FORCE_4KiB_PAGES ? (4096 / BLOCK_SIZE) : (PAGE_SIZE / BLOCK_SIZE);
  for(int i = 0; i < NUM_CPUS; i++) {
    ASD_Modules.emplace_back(ASD_Module(num_bins));
  }
}

void hasd::prefetcher_final_stats() {
  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("ASD for Core {}:\n",i);
    ASD_Modules.at(i).print_stats();
  }
}

uint32_t hasd::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
if(evicted_addr != champsim::address{})
	for(int i = 0; i < NUM_CPUS; i++)
	ASD_Modules.at(i).remove_from_pagemap(evicted_addr);

  return metadata_in;
}

void hasd::prefetcher_cycle_operate() {

}

void hasd::ASD_Module::add_to_pagemap(champsim::address addr) {
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

bool hasd::ASD_Module::check_pagemap(champsim::address addr) {
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

void hasd::ASD_Module::remove_from_pagemap(champsim::address addr) {
  uint64_t pn = addr.to<uint64_t>() >> (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE);
  uint64_t bn = (addr.to<uint64_t>() % (1 << (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE))) >> LOG2_BLOCK_SIZE;
  page_map pm(pn);
  auto entry = page_map_table.check_hit(pm);
  if(entry.has_value()) {
    entry->bits.at(bn) = 0;
    page_map_table.fill(entry.value());
  }
}
