#ifndef PREFETCHER_AMPM_H
#define PREFETCHER_AMPM_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "address.h"
#include "modules.h"
#include "msl/lru_table.h"

// AMPM (Access Map Pattern Matching) -- ported from ChampSimDPC4/prefetcher/ampm to the
// Runtime module API (cache_module*/prefetch_line/ModuleBuilder params). Differs from the
// stock va_ampm_lite: 64x4 (256) region table, physical-page-bounded search, prefetches on
// first region touch, and prunes the access map on eviction (map tracks cache residency).
class ampm : public champsim::modules::prefetcher
{
public:
  using block_in_page = champsim::address_slice<champsim::dynamic_extent>;

  static constexpr std::size_t REGION_SETS = 64;
  static constexpr std::size_t REGION_WAYS = 4;
  static constexpr int PREFETCH_DEGREE = 2;

  struct region_type {
    champsim::page_number vpn;
    std::vector<bool> access_map{};
    std::vector<bool> prefetch_map{};

    region_type() = default;
    region_type(champsim::page_number allocate_vpn, std::size_t blocks_per_page)
      : vpn(allocate_vpn), access_map(blocks_per_page), prefetch_map(blocks_per_page)
    {
    }
  };

  struct ampm_indexer {
    auto operator()(const region_type& entry) const { return entry.vpn; }
  };
  champsim::msl::lru_table<region_type, ampm_indexer, ampm_indexer> regions{REGION_SETS, REGION_WAYS};

  champsim::modules::cache_module* cache_ = nullptr;

  template <typename T>
  std::pair<champsim::page_number, block_in_page> page_and_offset(T addr) const
  {
    return {champsim::page_number{addr}, block_in_page{block_in_page_extent_, addr}};
  }
  std::size_t blocks_per_page() const { return page_size_ / block_size_; }
  region_type make_region(champsim::page_number vpn) const { return region_type{vpn, blocks_per_page()}; }

  bool check_pagemap(champsim::address addr, bool prefetch);
  void add_to_pagemap(champsim::address addr, bool prefetch);
  void remove_from_pagemap(champsim::address addr, bool prefetch);
  void do_prefetch(champsim::address addr, int degree, bool two_level, uint32_t metadata_in);

  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in) override;
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in) override;

  ampm(champsim::modules::ModuleBuilder builder)
    : cache_(builder.get_parent<champsim::modules::cache_module>()),
      page_size_(builder.get_parameter<unsigned>("page_size")),
      block_size_(builder.get_parameter<unsigned>("block_size")),
      block_in_page_extent_(champsim::data::bits{builder.get_parameter<unsigned>("log2_page_size")},
                            champsim::data::bits{builder.get_parameter<unsigned>("log2_block_size")})
  {
  }

  void prefetcher_initialize() override {}
  void prefetcher_cycle_operate() override {}
  void prefetcher_final_stats() override {}
  void prefetcher_branch_operate(champsim::address /*ip*/, uint8_t /*branch_type*/, champsim::address /*branch_target*/) override {}

private:
  unsigned page_size_;
  unsigned block_size_;
  champsim::dynamic_extent block_in_page_extent_;
};

#endif
