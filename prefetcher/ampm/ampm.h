#ifndef PREFETCHER_AMPM_H
#define PREFETCHER_AMPM_H

#include <array>
#include <bitset>
#include <cstdint>
#include <vector>

#include "champsim.h"
#include "modules.h"
#include "msl/lru_table.h"

class ampm : public champsim::modules::prefetcher
{
  static constexpr int PREFETCH_DEGREE = 2;
  public:
    class AMPM_Module {
      static constexpr std::size_t REGION_SETS = 64;
      static constexpr std::size_t REGION_WAYS = 4;
      static constexpr unsigned int AMPM_PAGE_BITS = 12;
      public:
      struct page_extent : champsim::dynamic_extent {
        page_extent() : dynamic_extent(champsim::data::bits{64}, champsim::data::bits{AMPM_PAGE_BITS}) {}
      };
      using page = champsim::address_slice<page_extent>;

      struct block_in_page_extent : champsim::dynamic_extent {
        block_in_page_extent() : dynamic_extent(champsim::data::bits{AMPM_PAGE_BITS}, champsim::data::bits{LOG2_BLOCK_SIZE}) {}
      };
      using block_in_page = champsim::address_slice<block_in_page_extent>;

      struct region_type {
        page vpn;
        std::vector<bool> access_map{};
        std::vector<bool> prefetch_map{};


        region_type() : region_type(page{}) {}
        explicit region_type(page allocate_vpn)
          : vpn(allocate_vpn), access_map((1 << AMPM_PAGE_BITS) / BLOCK_SIZE), prefetch_map((1 << AMPM_PAGE_BITS) / BLOCK_SIZE)
        {
        }
      };


      struct ampm_indexer {
        auto operator()(const region_type& entry) const { return entry.vpn; }
      };
      champsim::msl::lru_table<region_type,ampm_indexer,ampm_indexer> regions{REGION_SETS,REGION_WAYS};

      void add_to_pagemap(champsim::address addr, bool prefetch);
      bool check_pagemap(champsim::address addr, bool prefetch);
      void remove_from_pagemap(champsim::address addr, bool prefetch);

      template <typename T>
      static auto page_and_offset(T addr) -> std::pair<page, block_in_page>;
      void do_prefetch(CACHE* intern, champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in, int degree, bool two_level);
    };



  
  


  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  
  // void prefetcher_cycle_operate() {}
  // void prefetcher_final_stats() {}
  AMPM_Module engine;
};

#endif