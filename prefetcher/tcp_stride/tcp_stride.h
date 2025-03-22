#ifndef tcp_stride_H
#define tcp_stride_H

#include <cstdint>
#include <optional>
#include <vector>

#include "address.h"
#include "champsim.h"
#include "modules.h"
#include "msl/lru_table.h"

struct tcp_stride : public champsim::modules::prefetcher {

  uint64_t heartbeat_size = 1000000;
  uint64_t cycles_per_window = 10000;
  uint64_t cycles_so_far = 0;

  uint64_t total_cycles = 0;

  uint64_t table_hits = 0;
  uint64_t table_misses = 0;
  uint64_t useful_counter = 0;
  uint64_t useful_counter_lw = 0;
  uint64_t useful_counter_llw = 0;
  uint64_t miss_counter = 0;
  uint64_t hit_counter = 0;
  uint64_t pf_fill_counter = 0;
  uint64_t pf_fill_counter_lw = 0;
  uint64_t pf_fill_counter_llw = 0;
  uint64_t pf_fill_counter_lllw = 0;
  uint64_t pf_fill_counter_llllw = 0;
  uint64_t pf_fill_counter_lllllw = 0;
  uint64_t pf_fill_counter_llllllw = 0;

  constexpr static unsigned LLC_FILTER_SETS = 16;
  constexpr static unsigned LLC_FILTER_WAYS = 4;
  constexpr static unsigned LLC_FILTER_TIMEOUT = 150;

  uint64_t rolling_aggression = 0;
  uint64_t windows = 0;

  bool got_back_off = false;
  uint64_t aggression = 5;
  uint64_t aggression_max = 64;

  float back_off_coeff = 0.9;
  float back_on_coeff = 1;

  static std::vector<tcp_stride*> instances;

  static void back_off(champsim::address addr, uint32_t cpu);

  struct llc_entry {
    champsim::block_number block;
    uint64_t first_accessed;

    llc_entry() : llc_entry(champsim::block_number{0},0) {}
    explicit llc_entry(champsim::block_number block_, uint64_t first_accessed_) : block(block_), first_accessed(first_accessed_) {}
  };
  struct llc_indexer {
    auto operator()(const llc_entry& entry) const {return entry.block;}
  };

  champsim::msl::lru_table<llc_entry, llc_indexer, llc_indexer> llc_filter{LLC_FILTER_SETS,LLC_FILTER_WAYS};

  bool filter_prefetch(champsim::address addr);

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
  constexpr static int PREFETCH_DEGREE = 3;

  constexpr static std::size_t ACTIVE_LOOKAHEADS = 8;

  std::vector<std::optional<lookahead_entry>> active_lookahead = std::vector<std::optional<lookahead_entry>>(ACTIVE_LOOKAHEADS);

  champsim::msl::lru_table<tracker_entry> table{TRACKER_SETS, TRACKER_WAYS};

public:
  using champsim::modules::prefetcher::prefetcher;

  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  void prefetcher_cycle_operate();
  void prefetcher_initialize();
};

#endif
