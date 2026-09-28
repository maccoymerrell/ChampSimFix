#ifndef IP_STRIDE_H
#define IP_STRIDE_H

#include <cstdint>
#include <optional>

#include "address.h"
#include "champsim.h"
#include "modules.h"
#include "msl/lru_table.h"

struct ip_stride : public champsim::modules::prefetcher {
  struct tracker_entry {
    champsim::address ip{};                                // the IP we're tracking
    champsim::block_number last_cl_addr{};                 // the last address accessed by this IP
    champsim::block_number::difference_type last_stride{}; // the stride between the last two addresses accessed by this IP

    auto index() const
    {
      using namespace champsim::data::data_literals;
      return ip.slice_upper<2_b>();
    }
    auto tag() const
    {
      using namespace champsim::data::data_literals;
      return ip.slice_upper<2_b>();
    }
  };

  struct lookahead_entry {
    champsim::address address{};
    champsim::address::difference_type stride{};
    int degree = 0; // degree remaining
  };

  constexpr static std::size_t TRACKER_SETS = 256;
  constexpr static std::size_t TRACKER_WAYS = 4;
  constexpr static int DEFAULT_PREFETCH_DEGREE = 3;

  champsim::modules::cache_module* cache_ = nullptr; // declared before prefetch_degree to match constructor init order

  int prefetch_degree = DEFAULT_PREFETCH_DEGREE;

  std::optional<lookahead_entry> active_lookahead;

  champsim::msl::lru_table<tracker_entry> table{TRACKER_SETS, TRACKER_WAYS};

public:
  using champsim::modules::prefetcher::prefetcher;

  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in) override;
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in) override;
  void prefetcher_initialize() override {}
  void prefetcher_cycle_operate() override;
  void prefetcher_final_stats() override {}
  void prefetcher_branch_operate(champsim::address /*ip*/, uint8_t /*branch_type*/, champsim::address /*branch_target*/) override {}
  ip_stride(champsim::modules::ModuleBuilder builder)
      : cache_(builder.get_parent<champsim::modules::cache_module>()), prefetch_degree(builder.get_parameter<int>("degree", true, DEFAULT_PREFETCH_DEGREE)),
        table(builder.get_parameter<std::size_t>("tracker_sets", true, TRACKER_SETS), builder.get_parameter<std::size_t>("tracker_ways", true, TRACKER_WAYS))
  {
  }
};

#endif
