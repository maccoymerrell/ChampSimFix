#ifndef PREFETCHER_VA_AMPM_LITE_H
#define PREFETCHER_VA_AMPM_LITE_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "address.h"
#include "modules.h"
#include "msl/lru_table.h"

class va_ampm_lite : public champsim::modules::prefetcher
{
  // Block-within-page extent: derives its bit positions from the runtime
  // page/block geometry (looked up via the global ModuleBuilder so a
  // default-constructed block_in_page still gets the right widths).
  struct block_in_page_extent : champsim::dynamic_extent {
    block_in_page_extent()
      : dynamic_extent(champsim::data::bits{champsim::modules::ModuleBuilder::globals().get_parameter<unsigned>("log2_page_size")},
                       champsim::data::bits{champsim::modules::ModuleBuilder::globals().get_parameter<unsigned>("log2_block_size")}) {}
  };
  using block_in_page = champsim::address_slice<block_in_page_extent>;

public:
  static constexpr std::size_t REGION_SETS = 1;
  static constexpr std::size_t REGION_WAYS = 128;
  static constexpr int MAX_DISTANCE = 256;
  static constexpr int PREFETCH_DEGREE = 2;

  struct region_type {
    champsim::page_number vpn;
    std::vector<bool> access_map{};
    std::vector<bool> prefetch_map{};

    region_type() = default;
    region_type(champsim::page_number allocate_vpn, std::size_t blocks_per_page)
      : vpn(allocate_vpn), access_map(blocks_per_page), prefetch_map(blocks_per_page) {}
  };

  champsim::modules::cache_module* cache_ = nullptr;

  using prefetcher::prefetcher;

  struct ampm_indexer {
    auto operator()(const region_type& entry) const { return entry.vpn; }
  };
  champsim::msl::lru_table<region_type, ampm_indexer, ampm_indexer> regions{REGION_SETS, REGION_WAYS};

  bool check_cl_access(champsim::block_number v_addr);
  bool check_cl_prefetch(champsim::block_number v_addr);

  template <typename T>
  static auto page_and_offset(T addr) -> std::pair<champsim::page_number, block_in_page>;

  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in) override;
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in) override;

  va_ampm_lite(champsim::modules::ModuleBuilder builder)
    : cache_(builder.get_parent<champsim::modules::cache_module>()),
      page_size_(builder.get_parameter<unsigned>("page_size")),
      block_size_(builder.get_parameter<unsigned>("block_size")) {}

  std::size_t blocks_per_page() const { return page_size_ / block_size_; }
  region_type make_region(champsim::page_number vpn) const { return region_type{vpn, blocks_per_page()}; }

  void prefetcher_initialize() override {}
  void prefetcher_cycle_operate() override {}
  void prefetcher_final_stats() override {}
  void prefetcher_branch_operate(champsim::address /*ip*/, uint8_t /*branch_type*/, champsim::address /*branch_target*/) override {}

private:
  unsigned page_size_;
  unsigned block_size_;
};

#endif
