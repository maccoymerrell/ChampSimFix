#ifndef PREFETCHER_ORAP_PPF_H
#define PREFETCHER_ORAP_PPF_H

#include <cstdint>
#include <array>
#include <functional>

#include "address.h"
#include "modules.h"
#include "cache.h"
#include "dram_controller.h"
#include "../spp_ppf_4kb/spp_ppf_4kb.h"
#include "fmt/ranges.h"

struct orap_ppf : public champsim::modules::prefetcher {

  static uint64_t get_hash(uint64_t key)
  {
    // Robert Jenkins' 32 bit mix function
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);

    // Knuth's multiplicative method
    key = (key >> 3) * 2654435761;

    return key;
  }

  constexpr static std::size_t RW_WAYS = 8;
  constexpr static std::size_t RW_SETS = 512;
  constexpr static std::size_t RW_IP_HASHES = 4;

  constexpr static bool ENABLE_IP_BLACKLIST = false;
  constexpr static std::size_t IP_BLACKLIST_SETS = 16;
  constexpr static std::size_t IP_BLACKLIST_WAYS = 4;
  constexpr static uint8_t IP_BLACKLIST_THRESH = 4;
  constexpr static std::size_t IP_BLACKLIST_HARMFUL = 0;
  constexpr static std::size_t IP_BLACKLIST_USEFUL = 1;
  constexpr static std::size_t IP_BLACKLIST_DEMAND = 2;

  constexpr static std::size_t SAMPLE_TABLE_WAYS = 4;
  constexpr static std::size_t SAMPLE_TABLE_SETS = 64;

  constexpr static bool USE_PREFETCH_QUEUE = true;
  constexpr static bool UNIFIED_PREFETCH_QUEUE = true;
  constexpr static bool DELAY_PREFETCH_QUEUE_ISSUE = false;
  constexpr static bool HOLD_PREFETCH_QUEUE_SLOT_UNTIL_COMPLETE = true;
  std::size_t PREFETCH_SUBQUEUES = 16; //split by rowbuffer
  std::size_t PREFETCH_SUBQUEUES_SC = 4; //split by rowbuffer
  constexpr static std::size_t MAX_PREFETCH_QUEUE_RATE = 50; //delay each packet a max of 50 cycles
  constexpr static std::size_t PREFETCH_QUEUE_RATE_INCR = 3;
  std::size_t PREFETCH_SUBQUEUE_LIMIT = 32;
  std::size_t PREFETCH_SUBQUEUE_LIMIT_SC = 16;
  constexpr static std::size_t MIN_PREFETCH_GANG_ISSUE = 0;

  constexpr static std::size_t IP_TRACKER_WAYS = 8;
  constexpr static std::size_t IP_TRACKER_SETS = 16;

  constexpr static std::size_t CONF_MAX = 255;
  constexpr static std::size_t CONF_DROP_THRESH = 0;

  constexpr static uint32_t PPF_ID = 1;
  constexpr static uint32_t BUFFER_ID = 2;

  constexpr static double ASD_THRESH = 0.5;

  constexpr static std::array<uint8_t,25> DEPTHS = {0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,3,3,3,3,4,4};

  constexpr static std::array<uint8_t,25> CHANCE = {1,12,24,36,48,60,72,84,96,108,120,127,127,127,127,127,127,127,127,127,127,127,127,127,127};
  constexpr static std::array<double,25> ASD_DEPTHS = {1.0,1.0,1.0,1.0,1.0,1.1,1.2,1.2,1.3,1.3,1.4,1.4,1.5,1.5,1.6,1.6,1.7,1.7,1.8,1.8,1.9,1.9,2,2,2};
  constexpr static std::array<uint8_t,25> THRESH = {10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160,170,180,190,200,210,220,230,240,250};

  constexpr static bool USE_ROW_CONF = true;
  constexpr static bool USE_IP_CONF = true;

  constexpr static bool ACT_CONF_ON_ROW = true;
  constexpr static bool ACT_CONF_ON_IP = true;

  constexpr static bool MULT_DECREASE_ASD_IP = false;
  constexpr static bool CONF_COUNTER_ASD_IP = true;

  constexpr static uint8_t ASD_IP_COUNTER_ISSUE_MAX = 5;
  constexpr static uint8_t ASD_IP_COUNTER_USEFUL_MAX = 4;
  constexpr static uint8_t ASD_IP_COUNTER_ISSUE_DECR = 1;
  constexpr static uint8_t ASD_IP_COUNTER_USEFUL_INCR = 1;
  constexpr static bool MULT_DECREASE_BUFFER_IP = false;
  constexpr static bool CONF_COUNTER_BUFFER_IP = true;
  constexpr static uint8_t BUFFER_IP_COUNTER_ISSUE_MAX = 5;
  constexpr static uint8_t BUFFER_IP_COUNTER_USEFUL_MAX = 4;
  constexpr static uint8_t BUFFER_IP_COUNTER_ISSUE_DECR = 1;
  constexpr static uint8_t BUFFER_IP_COUNTER_USEFUL_INCR = 1;
  constexpr static bool MULT_DECREASE_BUFFER_ROW = false;
  constexpr static bool CONF_COUNTER_BUFFER_ROW = true;
  constexpr static uint8_t BUFFER_ROW_COUNTER_ISSUE_MAX = 5;
  constexpr static uint8_t BUFFER_ROW_COUNTER_USEFUL_MAX = 4;
  constexpr static uint8_t BUFFER_ROW_COUNTER_ISSUE_DECR = 1;
  constexpr static uint8_t BUFFER_ROW_COUNTER_USEFUL_INCR = 1;

  constexpr static int STARTING_CONF = 180;
  constexpr static int STARTING_CONF_COPREFETCH = 255;
  constexpr static int USEFUL_CONF_ROW = 1;
  constexpr static int USEFUL_CONF_IP = 4;
  constexpr static int USEFUL_NCONF = 0;
  constexpr static int USELESS_NCONF = 0;
  constexpr static double USELESS_NCONF_FACTOR = 0.9;
  constexpr static double USELESS_NCONF_DEPTH_MOD = 0.0;
  constexpr static int CONFLICT_NCONF = 0;
  constexpr static double CONFLICT_NCONF_FACTOR = 0.9;
  constexpr static int FILL_NCONF = 1;
  constexpr static double FILL_NCONF_FACTOR = 0.9;
  constexpr static int ACT_CONF = 0;
  constexpr static int MAX_ACT_CONF_LEVEL = 255;

  constexpr static double ASD_MAX_THRESH = 2.0;
  constexpr static double ASD_MIN_THRESH = 2.0;
  constexpr static double ASD_THRESH_STEP = 0.1;

  constexpr static bool SKIP_TAG_CHECK_BUFFER = false;
  constexpr static bool SKIP_TAG_CHECK_PPF = false;

  constexpr static bool TRIGGER_BUFFER_ON_MISS = true;

  constexpr static int32_t BUFFER_MAX_NCONF = 0;
  constexpr static int32_t BUFFER_MIN_NCONF = 0;
  constexpr static std::size_t BUFFER_NCONF_STEP = 10;

  constexpr static bool DISABLE_PPF = false;
  constexpr static float TARGET_PPF_ACCURACY = 0.75;
  constexpr static float TARGET_BUFFER_ACCURACY = 0.75; 

  constexpr static std::size_t usefulness_measure_epoch = 8192;
  constexpr static std::size_t usefulness_measure_print_interval = 1;

  constexpr static std::size_t blacklist_reset_interval =  10000000;

  constexpr static std::size_t prefetch_watchdog_interval = 65565;
  std::vector<std::size_t> watchdog_counter;

  std::vector<int32_t> variable_buffer_conf;
  std::vector<double> variable_asd_thresh;

  std::vector<uint64_t> PREFETCH_QUEUE_RATE;
  std::vector<uint64_t> PREFETCH_QUEUE_LAST_ISSUE;

  constexpr static bool FORWARD = true;
  constexpr static bool BACKWARD = false;

  double opened_rows = 0;
  double next_line_issued = 0;
  double useful_tallied = 0;
  double useless_tallied = 0;
  double prefetches_dropped = 0;
  double prefetches_filtered = 0;
  double prefetches_rejected = 0;
  double prefetches_discarded_old = 0;
  double streams_squashed = 0;

  double orphaned_row_lookups = 0;
  double orphaned_ip_lookups = 0;
  
  double pp_thrashes = 0;

  double conflict_filters = 0;

  double conf_useful = 0;
  double conf_useless = 0;
  double conf_act = 0;
  double conf_fill = 0;
  double conf_conflict = 0;

  std::vector<std::size_t> column_bits;
  std::size_t column_cluster_size;

  std::vector<std::size_t> global_useful_buffer;
  std::vector<std::size_t> global_useful_ppf;
  std::vector<std::size_t> global_useless_buffer;
  std::vector<std::size_t> global_useless_ppf;

  std::vector<double> global_usefulness_ppf;
  std::vector<double> global_usefulness_buffer;

  std::vector<double> pf_issued_last_epoch;
  std::vector<double> copf_issued_last_epoch;

  std::vector<double> pf_issued_to_dram_last_epoch;

  uint64_t prefetch_latency_cycles_last_epoch = 0;
  uint64_t prefetch_sampled_last_epoch = 0;
  uint64_t demand_latency_cycles_last_epoch = 0;
  uint64_t demand_sampled_last_epoch = 0;

  struct sample_table_entry {
    champsim::block_number block;
    uint64_t cycle_missed;
    sample_table_entry() : sample_table_entry(champsim::address{}, 0) {}
    explicit sample_table_entry(champsim::address addr, uint64_t cycle_missed_) : block(champsim::block_number{addr}), cycle_missed(cycle_missed_) {}
  };
  struct sample_set {
    auto operator()(const sample_table_entry& entry) const {return entry.block.to<uint64_t>();}
  };
  struct sample_way {
    auto operator()(const sample_table_entry& entry) const {return entry.block.to<uint64_t>();}
  };

  champsim::msl::lru_table<sample_table_entry,sample_set,sample_way> demand_sample_table{SAMPLE_TABLE_SETS,SAMPLE_TABLE_WAYS};
  champsim::msl::lru_table<sample_table_entry,sample_set,sample_way> prefetch_sample_table{SAMPLE_TABLE_SETS,SAMPLE_TABLE_WAYS};

  std::vector<std::size_t> epoch_counter;
  uint64_t epochs = 0;
  uint64_t blacklist_counter = 0;
  uint64_t cycle_epoch = 3e5;
  uint64_t epoch_cycle_counter = 0;


  struct Histogram {
    std::vector<uint64_t> hist;
    std::vector<uint64_t> hist_count;
    uint64_t global_count = 0;
    uint64_t bin_range = 0;

    double histogram_factor = 2.0;

    Histogram(std::size_t range, std::size_t bins) {
      bin_range = std::ceil(range/(double)bins);
      for(auto i = 0; i < bins; i++) {
        hist.push_back(0);
        hist_count.push_back(0);
      }
    }

    void tally(std::size_t val, std::size_t val_count) {
      hist.at(val/bin_range) += val_count;
      hist_count.at(val/bin_range)++;
      global_count += val_count;
    }

    double get_bin_val(std::size_t bin) {
      return hist.at(bin)  / (double)hist_count.at(bin);
    }

    void clear() {
      for(int i = 0; i < hist.size(); i++) {
        hist.at(i) = 0;
        hist_count.at(i) = 0;
      }
      global_count = 0;
    }

    void print() {
        fmt::print("\t\tHistogram [MSHR Occupancy : Average Latency, Samples]");
        for(int j = 0; j < hist.size(); j++) {
        if(get_bin_val(j) != 0)
          fmt::print("\t\t\t[{}-{}] : {}, {}\n",j*bin_range,j*bin_range + bin_range - 1,get_bin_val(j),hist_count.at(j));
      }
    }
  };


  struct row_walker {
    struct ip_hash_set {
      auto operator()(const uint16_t& entry) const {return entry;}
    };
    struct ip_hash_way {
      auto operator()(const uint16_t& entry) const {return entry;}
    };
    champsim::msl::lru_table<uint16_t,ip_hash_set,ip_hash_way> ip_hashes{1,RW_IP_HASHES};
    uint32_t row = 0;
    uint8_t confidence = 0;
    uint8_t confidence_issue_counter = 0;
    uint8_t confidence_useful_counter = 0;
    int8_t last_col = -1;
    int8_t col_stride = 1;
    row_walker() : row_walker(0) {}
    explicit row_walker(uint64_t row) : row(row), confidence(STARTING_CONF), last_col(-1), col_stride(1) {}
    static int get_size_bits() {
      int bits = 16;
      if(USE_ROW_CONF)
        bits += 8;
      if(CONF_COUNTER_BUFFER_ROW)
        bits += champsim::lg2(BUFFER_ROW_COUNTER_ISSUE_MAX) + champsim::lg2(BUFFER_ROW_COUNTER_USEFUL_MAX);
      return bits; 
    }
  };
  struct row_walker_set {
    auto operator()(const row_walker& entry) const { return entry.row; }
  };
  struct row_walker_way {
    auto operator()(const row_walker& entry) const { return entry.row; }
  };

  struct prefetch_queue_entry {
    champsim::address start_addr;
    champsim::address ip;
    uint32_t cpu;
    uint32_t metadata_in;
    bool fill_this_level;
    bool skip_tag_check;
    bool return_tag_check;
    bool column_prefetch;
    int length;
    int stride;
    int sent_so_far = 0;
    bool has_company = false;

    bool waiting_for_response = false;
    prefetch_queue_entry(champsim::address start_addr_, int length_, int stride_, champsim::address ip_, uint32_t cpu_, uint32_t metadata_in_, bool fill, bool skip, bool return_tag, bool column_prefetch_) : start_addr(start_addr_), length(length_), stride(stride_), ip(ip_), cpu(cpu_), metadata_in(metadata_in_), fill_this_level(fill), skip_tag_check(skip), return_tag_check(return_tag), column_prefetch(column_prefetch_) {}

    static int get_size_bits() {
      int bits = 42;
      bits += 3 + 1 + 1;
      return bits;
    }
  };

  std::vector<std::vector<std::deque<prefetch_queue_entry>>> pf_queue;
  std::vector<std::vector<uint64_t>> pf_queue_counter;
  uint64_t cycle_since_last_issue = 0;
  uint64_t last_queue_pos = 0;
  std::vector<uint64_t> last_subqueue_pos;

  std::vector<champsim::msl::lru_table<row_walker,row_walker_set,row_walker_way>> row_walker_table;

  struct ip_tracker {
    uint16_t ip_hash;
    uint8_t confidence;
    uint8_t confidence_issue_counter;
    uint8_t confidence_useful_counter;
    uint8_t coprefetch_confidence;
    uint8_t coprefetch_confidence_issue_counter;
    uint8_t coprefetch_confidence_useful_counter;
    ip_tracker() : ip_tracker(champsim::address{}) {}
    explicit ip_tracker(champsim::address ip_) : ip_hash(get_hash(ip_.to<uint64_t>())), confidence(STARTING_CONF) {}
    explicit ip_tracker(uint16_t ip_) : ip_hash(ip_), confidence(STARTING_CONF), coprefetch_confidence(STARTING_CONF_COPREFETCH) {}
    static int get_size_bits() {
      int bits = 16 + 8 + 8;
      if(CONF_COUNTER_ASD_IP)
        bits += champsim::lg2(ASD_IP_COUNTER_USEFUL_MAX) + champsim::lg2(ASD_IP_COUNTER_ISSUE_MAX);
      if(CONF_COUNTER_BUFFER_IP)
        bits += champsim::lg2(BUFFER_IP_COUNTER_USEFUL_MAX) + champsim::lg2(BUFFER_IP_COUNTER_ISSUE_MAX);
      return bits; 
    }
  };
  struct ip_tracker_set {
    auto operator()(const ip_tracker& entry) const { return entry.ip_hash; }
  };
  struct ip_tracker_way {
    auto operator()(const ip_tracker& entry) const { return entry.ip_hash; }
  };

  std::vector<champsim::msl::lru_table<ip_tracker,ip_tracker_set,ip_tracker_way>> ip_tracker_table;

  void update_walker(champsim::address addr, uint32_t cpu, champsim::address ip, bool is_blacklisted);
  void increase_confidence_useful(champsim::address ip, champsim::address addr, uint32_t cpu, bool coprefetch);
  void increase_confidence_opened(champsim::address ip, champsim::address addr, uint32_t cpu, bool coprefetch);
  void decrease_confidence_useless(champsim::address addr, uint32_t cpu, bool coprefetch);
  void decrease_confidence_conflict(champsim::address ip, champsim::address addr, uint32_t cpu, bool coprefetch);
  void decrease_confidence_fill(champsim::address ip, champsim::address addr, uint32_t cpu, bool coprefetch);
  uint8_t modify_confidence(uint8_t conf, uint8_t amnt, bool increment);
  uint8_t modify_confidence(uint8_t conf, double factor);
  std::size_t get_depth(uint8_t conf, uint32_t cpu);
  uint8_t get_squash_chance(uint8_t conf, uint32_t cpu);
  double get_asd_thresh(uint8_t conf, uint32_t cpu);

  void add_to_pq(prefetch_queue_entry pqe);
  void issue_from_pq();

  void clear_from_pq(champsim::address addr, uint32_t cpu);

  struct row_open_table_entry {
    uint64_t row = 0;
  };

  struct ip_blacklist_counter {
    champsim::address ip;
    uint8_t counter = 0;

    ip_blacklist_counter() : ip_blacklist_counter(champsim::address{}) {}
    explicit ip_blacklist_counter(champsim::address ip_) : ip(ip_) {}
    static int get_size_bits() {
      return (48) + champsim::next_pow2(IP_BLACKLIST_THRESH);
    }
  };
  struct ip_blacklist_set {
    auto operator()(const ip_blacklist_counter& entry) const {return entry.ip;}
  };
  struct ip_blacklist_way {
    auto operator()(const ip_blacklist_counter& entry) const {return entry.ip;}
  };


  std::vector<champsim::msl::lru_table<ip_blacklist_counter,ip_blacklist_set,ip_blacklist_way>> ip_blacklist_table;

  //row state table
  std::vector<row_open_table_entry> row_open_table;

  // ORAP-managed region map for filtering both PPF and ORAP prefetches
  static constexpr std::size_t PM_SETS = 64;
  static constexpr std::size_t PM_WAYS = 4;
  static constexpr std::size_t PAGE_MAP_SIZE = 64;

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

  // Per-CPU region maps managed by ORAP
  std::vector<champsim::msl::lru_table<page_map,page_map_set,page_map_way>> region_maps;

  void add_to_regionmap(champsim::address addr, uint32_t cpu);
  bool check_regionmap(champsim::address addr, uint32_t cpu);
  void remove_from_regionmap(champsim::address addr, uint32_t cpu);

  // PPF modules using the new spp_ppf::PPF_Module wrapper pattern
  std::vector<spp_ppf_4kb::PPF_Module> PPF_Modules;

  std::vector<Histogram> mshr_hist_new;
  std::vector<Histogram> mshr_hist_old;

  void reset_ip_blacklist();
  bool is_ip_blacklisted(champsim::address ip, uint32_t cpu);
  void update_ip_blacklist(std::size_t collision_type, champsim::address ip, uint32_t cpu, uint32_t victim_cpu, uint32_t target_prefetcher, uint32_t victim_prefetcher);

 std::vector<uint16_t> get_ip_hash_from_row(uint32_t row, uint32_t cpu);
  void log_ip_hash_to_row(uint32_t row, uint16_t ip_hash, uint32_t cpu);
  uint8_t get_confidence_from_ip_hash(uint16_t ip_hash, uint32_t cpu, bool coprefetch);

  uint8_t get_confidence_from_row(champsim::address addr, uint32_t cpu);
  bool update_row_open_table(champsim::address addr);
  bool is_row_open(champsim::address addr);

  champsim::address compose_base_and_column(champsim::address base, uint64_t column);

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in, uint32_t metadata_hit);
  uint32_t prefetcher_cache_fill(champsim::address addr, champsim::address ip, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict);
  void prefetcher_initialize();
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
