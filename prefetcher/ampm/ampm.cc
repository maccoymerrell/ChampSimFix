#include "ampm.h"

#include <algorithm>

#include "cache.h"


template <typename T>
auto ampm::AMPM_Module::page_and_offset(T addr) -> std::pair<page, block_in_page>
{
  return std::pair{page{addr}, block_in_page{addr}};
}

bool ampm::AMPM_Module::check_pagemap(champsim::address addr, bool prefetch)
{
  auto [pn, page_offset] = page_and_offset(addr);
  auto region = regions.check_hit(region_type{pn});

  if(prefetch)
    return (region.has_value() && region->prefetch_map.at(page_offset.to<std::size_t>()));
  else
    return (region.has_value() && region->access_map.at(page_offset.to<std::size_t>()));
}

void ampm::AMPM_Module::add_to_pagemap(champsim::address addr, bool prefetch) {
  auto [current_pn, page_offset] = page_and_offset(addr);
  auto temp_region = region_type{current_pn};
  auto demand_region = regions.check_hit(temp_region);

  if(demand_region.has_value()) {
    if(prefetch)
      demand_region->prefetch_map.at(page_offset.to<std::size_t>()) = true;
    else
      demand_region->access_map.at(page_offset.to<std::size_t>()) = true;
    regions.fill(demand_region.value());
  } else {
    if(prefetch)
      temp_region.prefetch_map.at(page_offset.to<std::size_t>()) = true;
    else
      temp_region.access_map.at(page_offset.to<std::size_t>()) = true;
    regions.fill(temp_region);
  }
}

void ampm::AMPM_Module::remove_from_pagemap(champsim::address addr, bool prefetch) {
  auto [current_pn, page_offset] = page_and_offset(addr);
  auto demand_region = regions.check_hit(region_type{current_pn});

  if(demand_region.has_value()) {
    if(prefetch)
      demand_region->prefetch_map.at(page_offset.to<std::size_t>()) = false;
    else
      demand_region->access_map.at(page_offset.to<std::size_t>()) = false;
    regions.fill(demand_region.value());
  }
}

uint32_t ampm::prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in)
{
  engine.add_to_pagemap(addr,false);
  engine.do_prefetch(intern_,addr,ip,cache_hit,useful_prefetch,type,0,PREFETCH_DEGREE,intern_->get_mshr_occupancy_ratio() < 0.5);

  return metadata_in;
}

uint32_t ampm::prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  if(evicted_addr != champsim::address{}) {
    engine.remove_from_pagemap(evicted_addr,false);
    engine.remove_from_pagemap(evicted_addr,true);
  }
  return metadata_in;
}

void ampm::AMPM_Module::do_prefetch(CACHE* intern, champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in, int degree, bool two_level) {
  champsim::block_number block_addr{addr};


  // attempt to prefetch in the positive, then negative direction
  for (auto direction : {1, -1}) {
    for (int i = 1, prefetches_issued = 0; prefetches_issued < degree; i++) {
      const auto pos_step_addr = block_addr + (direction * i);
      const auto neg_step_addr = block_addr - (direction * i);
      const auto neg_2step_addr = block_addr - (direction * 2 * i);

      //goes off physical page
      if(AMPM_Module::page{pos_step_addr}.to<uint64_t>() != AMPM_Module::page{addr}.to<uint64_t>())
        break;

      if (check_pagemap(champsim::address{neg_step_addr},false) && check_pagemap(champsim::address{neg_2step_addr},false) && !check_pagemap(champsim::address{pos_step_addr},false) && !check_pagemap(champsim::address{pos_step_addr},true)) {
        // found something that we should prefetch
        if (block_addr != champsim::block_number{pos_step_addr}) {
          champsim::address pf_addr{pos_step_addr};
          if (bool prefetch_success = intern->prefetch_line(pf_addr, two_level, metadata_in); prefetch_success) {
            //fmt::print("AMPM Prefetched: {} Metadata: {}\n",pf_addr,metadata_in);
            add_to_pagemap(champsim::address{pos_step_addr},true);
            prefetches_issued++;
          }
        }
      }
    }
  }
}