#ifndef PREFETCHER_SPP_LLC_FRAG_H
#define PREFETCHER_SPP_LLC_FRAG_H

#include <cstdint>
#include <bitset>

#include "address.h"
#include "msl/lru_table.h"
#include "modules.h"
#include "cache.h"

struct spp_dev_llc_frag : public champsim::modules::prefetcher {

  constexpr static std::size_t BLOCKS_PER_RB = 64; //8GB = 64, 32GB = 64
  constexpr static std::size_t BLOCK_THRESH = 80;
  constexpr static std::size_t DRAM_GROUPS = 32; //8GB = 32, 32GB = 128
  constexpr static unsigned RT_SETS = DRAM_GROUPS;
  constexpr static unsigned RT_WAYS = 16;

  uint64_t column_prefetches_issued = 0;
  uint64_t column_prefetches_dropped = 0;
  uint64_t full_queue = 0;

  uint64_t next_line_prefetches_issued = 0;
  uint64_t next_line_prefetches_dropped = 0;

  uint64_t prefetch_num = 0;

  double aggression_factor = 4;
  double max_aggression = 8;

  uint64_t last_useful = 0;
  uint64_t last_last_useful = 0;

  uint64_t last_evictions = 0;
  uint64_t last_last_evictions = 0;

  uint64_t last_hit = 0;
  uint64_t last_miss = 0;

  uint64_t update_period = 1e4;
  uint64_t current_cycle = 0;

  double increase_factor = 0.05;
  double decrease_factor = 0.5;

  int direction = 1;

  std::vector<std::vector<champsim::address>> bank_util;


  //row state table
  struct row_state_table_entry {
    uint64_t row_id;
    uint64_t group_id;

    row_state_table_entry() : row_state_table_entry(0,0) {}
    explicit row_state_table_entry(uint64_t row_id_, uint64_t group_id_) : row_id(row_id_), group_id(group_id_) {}
  };
  struct row_state_table_entry_set {
    auto operator()(const row_state_table_entry& entry) const { return entry.group_id; }
  };
  struct row_state_table_entry_way {
    auto operator()(const row_state_table_entry& entry) const { return entry.row_id; }
  };
  champsim::msl::lru_table<row_state_table_entry,row_state_table_entry_set,row_state_table_entry_way> row_table{DRAM_GROUPS,1};
  void update_row_state_table(champsim::address addr);
  bool is_row_open(champsim::address addr);

  void prefetch_column(champsim::address addr);
  void prefetch_next_line(champsim::address addr);

  void add_to_bank_queue(champsim::address addr);
  void remove_from_bank_queue(champsim::address addr);

  std::size_t get_bank_queue_size(champsim::address addr);

  //DRAM DECODE
  unsigned int get_dram_group(champsim::address pf_addr);
  uint64_t get_dram_column(champsim::address pf_addr);
  uint64_t get_dram_row(champsim::address pf_addr);

  //DRAM RECODE
  champsim::address compose_base_and_column(champsim::address base, uint64_t column);

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);

  void prefetcher_initialize() {
    bank_util = std::vector<std::vector<champsim::address>>(DRAM_GROUPS);
  }
  // void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) {}
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();
};

#endif
