#ifndef PREFETCHER_DRAM_BUFFER_ASD_H
#define PREFETCHER_DRAM_BUFFER_ASD_H

#include <cstdint>
#include <array>

#include "address.h"
#include "modules.h"
#include "cache.h"
#include "dram_controller.h"
#include "../asd/asd.h"

struct dram_prefetch_buffer_asd : public champsim::modules::prefetcher {

  constexpr static std::size_t RW_WAYS = 32;
  constexpr static std::size_t RW_SETS = 128;

  constexpr static std::size_t CONF_MAX = 255;
  constexpr static std::size_t CONF_DROP_THRESH = 0;

  constexpr static uint32_t ASD_ID = 1;
  constexpr static uint32_t BUFFER_ID = 2;

  constexpr static double ASD_THRESH = 0.5;

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

  constexpr static double ASD_MAX_THRESH = 2.0;
  constexpr static double ASD_MIN_THRESH = 1.0;
  constexpr static double ASD_THRESH_STEP = 0.1;

  constexpr static int32_t BUFFER_MAX_NCONF = 220;
  constexpr static int32_t BUFFER_MIN_NCONF = 0;
  constexpr static std::size_t BUFFER_NCONF_STEP = 10;

  constexpr static float TARGET_ASD_ACCURACY = 0.75;
  constexpr static float TARGET_BUFFER_ACCURACY = 0.75;

  constexpr static std::size_t usefulness_measure_epoch = 500000;

  std::vector<int32_t> variable_buffer_conf;
  std::vector<double> variable_asd_thresh;

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
  std::vector<std::size_t> global_useful_asd;
  std::vector<std::size_t> global_useless_buffer;
  std::vector<std::size_t> global_useless_asd;
  std::size_t epoch_counter = 0;

  std::vector<std::size_t> global_useless_asd_up;
  std::vector<std::size_t> global_useful_asd_up;
  std::vector<std::size_t> global_useless_asd_down;
  std::vector<std::size_t> global_useful_asd_down;


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

  //double global_usefulness_buffer = 1.0;
  //double global_usefulness_asd = 1.0;

  std::size_t num_bins;
  std::vector<asd::ASD_Module> ASD_Modules;


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

  [[nodiscard]] constexpr bool is_sampled(long set) {
		auto mask = SET_SAMPLE_RATE - 1;
		auto shift = champsim::lg2(SET_SAMPLE_RATE);
		auto low_slice = set & mask;
		auto high_slice = (set >> shift) & mask;
		return high_slice == low_slice;
	}
};

#endif
