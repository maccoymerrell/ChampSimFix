#ifndef PREFETCHER_DRAM_BUFFER_SPP_H
#define PREFETCHER_DRAM_BUFFER_SPP_H

#include <cstdint>
#include <array>

#include "address.h"
#include "modules.h"
#include "cache.h"
#include "dram_controller.h"

struct dram_prefetch_buffer_spp : public champsim::modules::prefetcher {
  constexpr static uint64_t usefulness_update_period = 1e5;
  constexpr static std::size_t PREFETCH_ENTRIES = 16;

  constexpr static std::size_t RHT_WAYS = 32;
  constexpr static std::size_t RHT_SETS = 128;

  constexpr static std::size_t RW_WAYS = 32;
  constexpr static std::size_t RW_SETS = 128;

  constexpr static std::size_t PFI_ENTRIES = 64;
  constexpr static std::size_t PFI_FORWARD = 16;

  constexpr static std::size_t CONF_MAX = 255;

  constexpr static uint32_t NEXT_LINE_ID = 1;
  constexpr static uint32_t BUFFER_ID = 2;

  constexpr static std::array<uint8_t,25> DEPTHS = {0,0,0,0,0,0,0,0,0,1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31};
  constexpr static std::array<uint8_t,25> THRESH = {10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160,170,180,190,200,210,220,230,240,250};

  constexpr static int USEFUL_CONF = 5;
  constexpr static int USELESS_NCONF = 25;
  constexpr static int DEMAND_CONF = 1;
  constexpr static int DEMAND_NCONF = 2;

  constexpr static bool FORWARD = true;
  constexpr static bool BACKWARD = false;

  double opened_rows = 0;
  double next_line_issued = 0;
  double forward_buffer_issued = 0;
  double backward_buffer_issued = 0;
  double useful_tallied = 0;
  double useless_tallied = 0;
  double prefetches_dropped = 0;

  std::size_t pf_issue_pos = 0;

  std::vector<std::size_t> column_bits;


  uint64_t accesses_so_far = 0;

  double prefetches_filtered = 0;

  struct row_history {
    static constexpr std::size_t bytes = 5;
    uint64_t row = 0;
    uint8_t access_count = 0;
    uint8_t confidence = 0;
    int8_t stride = 0;
    row_history() : row_history(0) {}
    explicit row_history(uint64_t row) : row(row) {}
  };
  struct row_history_set {
    auto operator()(const row_history& entry) const { return entry.row; }
  };
  struct row_history_way {
    auto operator()(const row_history& entry) const { return entry.row; }
  };

  champsim::msl::lru_table<row_history,row_history_set,row_history_way> row_history_table{RHT_SETS,RHT_WAYS};


  struct row_walker {
    uint8_t position = 0;
    uint8_t opened_at = 0;
    uint8_t confidence = 110;
    uint64_t row = 0;
    row_walker() : row_walker(0,0,0) {}
    explicit row_walker(uint64_t row, uint8_t position, uint8_t opened_at) : row(row), position(position), opened_at(opened_at) {}
  };
  struct row_walker_set {
    auto operator()(const row_walker& entry) const { return entry.row; }
  };
  struct row_walker_way {
    auto operator()(const row_walker& entry) const { return entry.row; }
  };

  std::vector<champsim::msl::lru_table<row_walker,row_walker_set,row_walker_way>> row_walker_table;

  struct prefetch_issuer_entry {
    bool valid = false;
    uint32_t cpu = 0;
    champsim::address base;
    std::size_t steps;
    prefetch_issuer_entry(champsim::address base_, std::size_t steps_, uint32_t cpu_) : base(base_), steps(steps_), valid(true), cpu(cpu_) {}
    prefetch_issuer_entry() : base(champsim::address{}), steps(std::size_t{}), valid(false) {}
  };

  std::deque<prefetch_issuer_entry> prefetch_issuer;

  bool add_to_pf_issuer(champsim::address addr, std::size_t depth, uint32_t cpu);
  void do_pf_issue();

  bool should_drop_prefetch(champsim::address addr, uint32_t cpu);
  void update_walker(champsim::address addr, uint32_t cpu);
  void increase_confidence(champsim::address addr, uint8_t amnt, bool cond);
  void decrease_confidence(champsim::address addr, uint8_t amnt);
  uint8_t get_confidence(champsim::address addr);
  uint8_t modify_confidence(uint8_t conf, uint8_t amnt, bool increment);
  std::size_t get_depth(uint8_t conf);

  struct RAF {
    constexpr static std::size_t RAF_FILTER_SETS = 32;
    constexpr static std::size_t RAF_FILTER_WAYS = 16;
    constexpr static std::size_t RAF_TIMEOUT = 300;
    struct raf_entry {
      champsim::block_number block;
      uint64_t first_accessed;

      raf_entry() : raf_entry(champsim::block_number{0},0) {}
      explicit raf_entry(champsim::block_number block_, uint64_t first_accessed_) : block(block_), first_accessed(first_accessed_) {}
    };
    struct raf_indexer {
      auto operator()(const raf_entry& entry) const {return entry.block;}
    };
    champsim::msl::lru_table<raf_entry, raf_indexer, raf_indexer> raf_filter{RAF_FILTER_SETS,RAF_FILTER_WAYS};
    bool check(champsim::address block, uint64_t check_time, bool update_table) {
      auto raf_filter_entry = raf_filter.check_hit(raf_entry{champsim::block_number{block},0});
      bool should_drop = false;
      if(raf_filter_entry.has_value()) {
        if(check_time - raf_filter_entry->first_accessed < RAF_TIMEOUT)
          should_drop = true;
      }
      if(update_table)
        raf_filter.fill(raf_entry{champsim::block_number{block},check_time});
      return should_drop;
    }
    void invalidate(champsim::address block) {
      raf_filter.invalidate(raf_entry{champsim::block_number{block},0});
    }
    
  };
  RAF filter;

  struct row_open_table_entry {
    bool direction = FORWARD;
    uint64_t row = 0;
    uint8_t confidence = 0;
    uint8_t access_count = 0;
    int8_t stride = 0;
    uint8_t position = 0;
  };

  //row state table
  std::vector<row_open_table_entry> row_open_table;

  struct row_interval_table_entry {
    uint64_t activations = 0;
  };

  //row interval table
  std::vector<row_interval_table_entry> row_interval_table;

  void update_row_open_table(champsim::address addr);
  bool is_row_open(champsim::address addr);

  void update_row_interval_table(champsim::address addr);


  void update_row_confidence(champsim::address addr);
  void issue_row_prefetch(champsim::address addr, uint32_t cpu);

  champsim::address compose_base_and_column(champsim::address base, uint64_t column);

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  void prefetcher_initialize();
  // void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) {}
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();
  std::map<uint32_t,uint64_t> useful;
  std::map<uint32_t,uint64_t> filled;
};

#endif
