#ifndef PREFETCHER_DRAM_BUFFER_NL_H
#define PREFETCHER_DRAM_BUFFER_NL_H

#include <cstdint>
#include <array>

#include "address.h"
#include "modules.h"
#include "cache.h"
#include "dram_controller.h"

struct dram_prefetch_buffer_nl : public champsim::modules::prefetcher {

  constexpr static std::size_t RW_WAYS = 32;
  constexpr static std::size_t RW_SETS = 128;

  constexpr static std::size_t CONF_MAX = 255;
  constexpr static std::size_t CONF_DROP_THRESH = 0;

  constexpr static uint32_t NEXTLINE_ID = 1;
  constexpr static uint32_t BUFFER_ID = 2;

  //constexpr static std::array<uint8_t,25> DEPTHS = {0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22};
  constexpr static std::array<uint8_t,25> DEPTHS = {0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11};
  //constexpr static std::array<uint8_t,25> DEPTHS = {0,0,0,1,1,1,1,1,2,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5};
  constexpr static std::array<uint8_t,25> THRESH = {10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160,170,180,190,200,210,220,230,240,250};

  constexpr static int STARTING_CONF = 40;
  constexpr static int USEFUL_CONF = 10;
  constexpr static int USEFUL_NCONF = 7;
  constexpr static int USELESS_NCONF = 20;
  constexpr static int STREAM_DEMAND_CONF = 4;
  constexpr static int STREAM_DEMAND_NCONF = 4;
  constexpr static int STREAM_PREFETCH_CONF = 4;
  constexpr static int STREAM_PREFETCH_NCONF = 4;
  constexpr static int ACT_CONF = 10;


  constexpr static int STREAM_FORWARD_WINDOW = 2;

  constexpr static int32_t BUFFER_MAX_NCONF = 220;
  constexpr static int32_t BUFFER_MIN_NCONF = 0;
  constexpr static std::size_t BUFFER_NCONF_STEP = 10;

  constexpr static uint32_t NEXTLINE_MAX_DEPTH = 4;
  constexpr static uint32_t NEXTLINE_MIN_DEPTH = 1;

  constexpr static std::size_t PM_WAYS = 4;
  constexpr static std::size_t PM_SETS = 32;
  constexpr static std::size_t PAGE_MAP_SIZE = 64;

  constexpr static std::size_t usefulness_measure_epoch = 500000;

  constexpr static float TARGET_NL_ACCURACY = 0.75;
  constexpr static float TARGET_BUFFER_ACCURACY = 0.75;

  std::vector<int32_t> variable_buffer_conf;
  std::vector<int32_t> variable_nextline_conf;

  constexpr static bool FORWARD = true;
  constexpr static bool BACKWARD = false;

  double opened_rows = 0;
  double next_line_issued = 0;
  double forward_buffer_issued = 0;
  double backward_buffer_issued = 0;
  double useful_tallied = 0;
  double useless_tallied = 0;
  double prefetches_dropped = 0;
  double prefetches_filtered = 0;
  double prefetches_rejected = 0;
  double prefetches_dropped_too_far_back = 0;
  double prefetches_dropped_too_far_forward = 0;
  double pp_thrashes = 0;

  double conf_demand_stream = 0;
  double conf_demand_random = 0;
  double conf_prefetch_stream = 0;
  double conf_prefetch_random = 0;
  double conf_useful = 0;
  double conf_useless = 0;
  double conf_act = 0;

  std::vector<std::size_t> column_bits;

  std::vector<std::size_t> global_useful_buffer;
  std::vector<std::size_t> global_useless_buffer;
  std::vector<std::size_t> global_useful_nextline;
  std::vector<std::size_t> global_useless_nextline;
  std::size_t epoch_counter = 0;



  struct row_walker {
    uint8_t position = 0;
    uint8_t opened_at = 0;
    uint8_t confidence = STARTING_CONF;
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

  bool should_drop_prefetch(champsim::address addr, uint32_t cpu);
  void update_walker(champsim::address addr, uint32_t cpu);
  void increase_confidence_useful(champsim::address addr);
  void increase_confidence_stream(champsim::address addr, bool prefetch);
  void increase_confidence_opened(champsim::address addr);
  void decrease_confidence_useless(champsim::address addr);
  uint8_t modify_confidence(uint8_t conf, uint8_t amnt, bool increment);
  std::size_t get_depth(uint8_t conf, uint32_t cpu);

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

    struct page_map {
    uint64_t page_num;
    constexpr static std::size_t PM_BASE = 100;
    std::array<uint8_t,PAGE_MAP_SIZE> bits;

    page_map() : page_map(0) {}
    explicit page_map(uint64_t page_num_) : page_num(page_num_) {
      for(std::size_t i = 0; i < PAGE_MAP_SIZE; i++)
        bits.at(i) = 0;
    }
  };

  struct page_map_set {
    auto operator()(const page_map& entry) const { return entry.page_num; }
  };
  struct page_map_way {
    auto operator()(const page_map& entry) const { return entry.page_num; }
  };

  std::vector<champsim::msl::lru_table<page_map,page_map_set,page_map_way>> page_map_table;

  //double global_usefulness_buffer = 1.0;
  //double global_usefulness_asd = 1.0;
  void add_to_pagemap(champsim::address addr, uint32_t cpu);
  bool check_pagemap(champsim::address addr, uint32_t cpu);
  void remove_from_pagemap(champsim::address addr, uint32_t cpu);


  void update_row_open_table(champsim::address addr);
  bool is_row_open(champsim::address addr);

  champsim::address compose_base_and_column(champsim::address base, uint64_t column);

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in, uint32_t metadata_hit);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict);
  void prefetcher_initialize();
  // void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) {}
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();

  long SET_SAMPLE_RATE;

};

#endif
