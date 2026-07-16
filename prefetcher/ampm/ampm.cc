#include "ampm.h"

#include <algorithm>

#include "cache.h"

// Ported from ChampSimDPC4/prefetcher/ampm (proper AMPM, not the va_ampm_lite stub).
champsim::modules::prefetcher::register_module<ampm> ampm_dpc4_register("ampm_dpc4");

bool ampm::check_pagemap(champsim::address addr, bool prefetch)
{
  auto [pn, page_offset] = page_and_offset(addr);
  auto region = regions.check_hit(make_region(pn));
  if (!region.has_value())
    return false;
  auto idx = page_offset.template to<std::size_t>();
  return prefetch ? region->prefetch_map.at(idx) : region->access_map.at(idx);
}

void ampm::add_to_pagemap(champsim::address addr, bool prefetch)
{
  auto [pn, page_offset] = page_and_offset(addr);
  auto idx = page_offset.template to<std::size_t>();
  auto region = regions.check_hit(make_region(pn));
  if (region.has_value()) {
    (prefetch ? region->prefetch_map : region->access_map).at(idx) = true;
    regions.fill(region.value());
  } else {
    auto r = make_region(pn);
    (prefetch ? r.prefetch_map : r.access_map).at(idx) = true;
    regions.fill(r);
  }
}

void ampm::remove_from_pagemap(champsim::address addr, bool prefetch)
{
  auto [pn, page_offset] = page_and_offset(addr);
  auto region = regions.check_hit(make_region(pn));
  if (region.has_value()) {
    auto idx = page_offset.template to<std::size_t>();
    (prefetch ? region->prefetch_map : region->access_map).at(idx) = false;
    regions.fill(region.value());
  }
}

void ampm::do_prefetch(champsim::address addr, int degree, bool two_level, uint32_t metadata_in)
{
  champsim::block_number block_addr{addr};
  const auto addr_page = champsim::page_number{addr}.template to<uint64_t>();

  // attempt to prefetch in the positive, then negative direction
  for (auto direction : {1, -1}) {
    for (int i = 1, prefetches_issued = 0; prefetches_issued < degree; i++) {
      const auto pos_step_addr = block_addr + (direction * i);
      const auto neg_step_addr = block_addr - (direction * i);
      const auto neg_2step_addr = block_addr - (direction * 2 * i);

      // stop at the physical page boundary (compare via to<uint64_t> to avoid a slice-extent throw)
      if (champsim::page_number{pos_step_addr}.template to<uint64_t>() != addr_page)
        break;

      if (check_pagemap(champsim::address{neg_step_addr}, false) && check_pagemap(champsim::address{neg_2step_addr}, false)
          && !check_pagemap(champsim::address{pos_step_addr}, false) && !check_pagemap(champsim::address{pos_step_addr}, true)) {
        if (block_addr != champsim::block_number{pos_step_addr}) {
          champsim::address pf_addr{pos_step_addr};
          if (bool prefetch_success = prefetch_line(pf_addr, two_level, metadata_in); prefetch_success) {
            add_to_pagemap(champsim::address{pos_step_addr}, true);
            prefetches_issued++;
          }
        }
      }
    }
  }
}

uint32_t ampm::prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                        uint32_t metadata_in)
{
  add_to_pagemap(addr, false);
  do_prefetch(addr, PREFETCH_DEGREE, cache_->get_mshr_occupancy_ratio() < 0.5, metadata_in);
  return metadata_in;
}

uint32_t ampm::prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  if (evicted_addr != champsim::address{}) {
    remove_from_pagemap(evicted_addr, false);
    remove_from_pagemap(evicted_addr, true);
  }
  return metadata_in;
}
