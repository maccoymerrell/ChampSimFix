#ifndef PREFETCHER_SPP_ROW_MAP_H
#define PREFETCHER_SPP_ROW_MAP_H

#include <cstdint>
#include <bitset>

#include "address.h"
#include "msl/lru_table.h"
#include "modules.h"
#include "cache.h"

struct spp_dev_row_map : public champsim::modules::prefetcher {

  constexpr static std::size_t BLOCKS_PER_RB = 64; //8GB = 64, 32GB = 64
  constexpr static std::size_t BLOCK_THRESH = 80;
  constexpr static std::size_t DRAM_GROUPS = 32; //8GB = 32, 32GB = 128
  constexpr static unsigned RT_SETS = 16;
  constexpr static unsigned RT_WAYS = 16;

  constexpr static std::size_t COLUMN_TRACKER_WIDTH = 8;

  uint64_t column_prefetches_issued = 0;
  uint64_t full_queue = 0;


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
  bool update_row_state_table(champsim::address addr);
  bool is_row_open(champsim::address addr);

  struct lru_column_use {
    std::vector<uint64_t> columns;
    std::vector<uint64_t> last_used;
    std::size_t entries;

    lru_column_use(std::size_t entries_) : columns(entries_,BLOCKS_PER_RB), last_used(entries_,0), entries(entries_) {}

    void add_access(uint64_t column, uint64_t cycle) {
      uint64_t index = 0;
      uint64_t victim_index = entries;
      uint64_t victim_usage = 0;

      bool found = false;
      for(auto it : last_used) {
        if(victim_index == entries || victim_usage > it) {
          victim_index = index;
          victim_usage = it;
        }
        if(columns[index] == column) {
          last_used[index] = cycle;
          found = true;
          break;
        }
        index++;
      }
      if(found)
        return;
      assert(victim_index != entries);
      last_used[victim_index] = cycle;
      columns[victim_index] = column;
    }

    void invalidate(uint64_t column) {
      uint64_t index = 0;
      for(auto it : columns) {
        if(it == column) {
          last_used[index] = 0;
          columns[index] = BLOCKS_PER_RB;
        }
        index++;
      }
    }

    void print() {
      for(std::size_t ind = 0; ind < entries; ind++) {
        if(columns[ind] != BLOCKS_PER_RB)
        fmt::print("\tcolumn: {} last_used: {}\n",columns[ind],last_used[ind]);
      }
    }
  };

  //row tracker
  struct row_tracker_entry {
    uint64_t row_id;

    //unsigned int first_used;
    std::size_t last_reverse = 0;
    std::size_t last_forward = 0;
    std::size_t last_issue = 0;
    std::bitset<BLOCKS_PER_RB> blocks{};
    std::bitset<BLOCKS_PER_RB> evicted{};
    //lru_column_use column_tracker{COLUMN_TRACKER_WIDTH};

    row_tracker_entry() : row_tracker_entry(0) {}
    explicit row_tracker_entry(uint64_t row_id_) : row_id(row_id_) {}
  };
  struct row_tracker_entry_set {
    auto operator()(const row_tracker_entry& entry) const { return entry.row_id; }
  };
  struct row_tracker_entry_way {
    auto operator()(const row_tracker_entry& entry) const { return entry.row_id; }
  };
  champsim::msl::lru_table<row_tracker_entry,row_tracker_entry_set,row_tracker_entry_way> row_tracker{RT_SETS,RT_WAYS};

  void add_column_used(champsim::address addr);
  void remove_column_used(champsim::address addr);
  void add_column_evicted(champsim::address addr);
  void remove_column_evicted(champsim::address addr);
  void issue_column_prefetch(champsim::address addr);

  //DRAM DECODE
  unsigned int get_dram_group(champsim::address pf_addr);
  uint64_t get_dram_column(champsim::address pf_addr);
  uint64_t get_dram_row(champsim::address pf_addr);

  //DRAM RECODE
  champsim::address compose_base_and_column(champsim::address base, uint64_t column);

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);

  // void prefetcher_initialize();
  // void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) {}
  // void prefetcher_cycle_operate() {}
  void prefetcher_final_stats();
};

#endif
