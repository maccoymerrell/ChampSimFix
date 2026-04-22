#ifndef PREFETCHER_SPPAM_B_H
#define PREFETCHER_SPPAM_B_H

#include <array>
#include <bitset>
#include <cstdint>
#include <vector>
#include <optional>
#include <map>

#include "champsim.h"
#include "modules.h"
#include "conf_table.h"
#include "dpc_api.h"
#include "instruction.h"

#include "branch_predictor.h"
#include "sppam_b_config.h"
#include "gshare/gshare.h"
#include "hashed_perceptron/hashed_perceptron.h"
#include "bimodal/bimodal.h"
#include "mpp/mpp.h"
#include "tage_sc_l/tage_sc_l.h"

class sppam_b : public champsim::modules::prefetcher
{

  public:
    enum GLOBAL_HISTORY_TYPE {
      OFFSET_HISTORY, //the history of this offset in the last N regions
      UNIQUE_RECENT_SEGMENT_HISTORY, //the history of the most recent segment in the last N regions
      ORDERED_RECENT_SEGMENT_HISTORY, //the history of the most recent segment in the last N regions, ordered by last-access time and including repeats as long as they are non-consecutive
      RECENT_XOR_HISTORY, //the local histories of the last N regions XORed together
    };
    enum IP_TYPE {
      IP, //branch predictor is fed IP of access
      REGION, //branch predictor is fed region of access
      PAGE_OFFSET, //branch predictor is fed page offset of access
      SMS, //branch predictor is fed SMS-style tag (page offset ^ ip)
      SMS_SUB, //branch predictor is fed SMS-style tag (page offset substitute ip)
      SMS_APP, //branch predictor is fed SMS-style tag (page offset appended to ip)
      SMS_BACK, //branch predictor is fed SMS-style tag (page offset append to end of ip rather than front)
      DELTA, //branch predictor is fed the delta between the current access and the predicted access
      DELTA_HISTORY, //branch predictor is fed the recent delta history pattern
      NONE //branch predictor is fed all 0's
    };
    enum BP_TYPE {
      HASHED_PERCEPTRON,
      GSHARE,
      BIMODAL,
      MPP,
      TAGE_SC_L
    };
    struct Sppam_b_Module {
      CACHE* intern_;

      static constexpr std::size_t REGION_SETS = 64;
      static constexpr std::size_t REGION_WAYS = 4;
      static constexpr bool CLEAR_ACCESS_MAP_ON_EVICT = false;

      //IP tracking table (for usefulness)
      static constexpr std::size_t IP_TRACK_TABLE_SIZE = 128;
      static constexpr uint64_t IP_TRACK_TABLE_TIMEOUT = 50000;
      static constexpr bool USELESS_ON_TIMEOUT = false;

      static constexpr bool USE_REGION_IP_FOR_LEARNING = false;
      static constexpr bool USE_REGION_ADDR_FOR_GLOBAL_HIST_LEARNING = false;

      static constexpr std::size_t IP_CONF_TABLE_SETS = 128;
      static constexpr std::size_t IP_CONF_TABLE_WAYS = 16;
      //samples for establishing IP usefulness
      static constexpr std::size_t IP_CONF_TABLE_SAMPLES = 64;
      bool USE_IP_CONF = true;

      //probability to fill into the ip tracking table
      static constexpr std::size_t TEMPORAL_SAMPLE_RATE = 1; //sample every N accesses for usefulness learning

      //direction sampler
      static constexpr int64_t DIRECTION_SAMPLE_MAX = 4;
      static constexpr std::size_t DELTA_HISTORY_LENGTH = 3;
      static constexpr uint64_t DELTA_HISTORY_MASK = 0xfff; //up to +/- 2k blocks
      static constexpr uint64_t DELTA_HISTORY_BITS = 8;

      //max pf degree
      int64_t PREFETCH_DEGREE = 8;
      bool IP_PREFETCH_DEGREE = false;
      static constexpr std::size_t REGION_BUDGET = 64;
      static constexpr std::size_t L1D_REGION_BUDGET = 8;

      static constexpr unsigned int SPPAM_B_PAGE_BITS = 12;

      //lookahead limits
      bool DO_LOOKAHEAD = true;
      double MIN_LOOKAHEAD_CONF = 0.2;
      double MIN_L1D_CONF = 0.9;
      int64_t MAX_LOOKAHEAD = 64;
      bool IP_LOOKAHEAD = false;
      bool PROB_DROP = false;

      double TIMELINESS_CYCLE = 10000;

      //debug 
      static constexpr uint64_t DO_DEBUG = false;
      static constexpr bool CROSS_PAGE = false;

      //bp stuff
      IP_TYPE ip_style = IP_TYPE::DELTA; //set IP type
      GLOBAL_HISTORY_TYPE global_type = GLOBAL_HISTORY_TYPE::ORDERED_RECENT_SEGMENT_HISTORY; //set global history type
      BP_TYPE bp_type = BP_TYPE::HASHED_PERCEPTRON; //set branch predictor type

      // Runtime-configurable history sizes (loaded from config file)
      unsigned int global_bits = 256;
      unsigned int local_bits = 64;
      unsigned int segment_bits = 64;
      int alignment_factor = 0;
      
      std::array<double,16> PREFETCH_DEGREES_BW = {1.00,1.00,1.00,1.00,0.90,0.90,0.90,0.90,0.80,0.80,0.80,0.75,0.75,0.50,0.50,0.50}; //normal
      std::array<double,16> PREFETCH_ISSUE_CHANCE = {0.0315,0.056,0.134,0.134,0.36,0.607,0.90,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0}; //normal
      std::array<double,16> REISSUE_CHANCE = {0.00,0.00,0.00,0.03,0.03,0.06,0.06,0.06,0.125,0.125,0.125,0.125,0.25,0.25,0.25,0.25}; //normal
      //static constexpr std::array<double,16> PREFETCH_REGION_BUDGET = {2,2,3,3,3,3,4,4,4,4,8,12,16,16,32,32}; //normal
      //std::array<double,16> PREFETCH_ISSUE_CHANCE = {0.3,0.4,0.5,0.6,0.7,0.8,0.90,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0}; //normal
      std::size_t GLOBAL_USEFULNESS_SAMPLE = 1024;

      uint64_t global_useful_prefetch = 0;
      uint64_t global_useless_prefetch  = 0;

      uint64_t current_bw_utilization = 0;
      int64_t current_pf_degree = PREFETCH_DEGREE;

      uint64_t regions_found = 0;
      uint64_t regions_not_found = 0;


      //stats
      bool in_warmup = true;
      uint64_t prefetches_issued = 0;
      uint64_t total_lookaheads = 0;
      uint64_t prefetch_triggers = 0;
      uint64_t prefetches_filtered = 0;
      uint64_t prefetches_full_pq = 0;
      uint64_t prefetches_dropped = 0;
      uint64_t pf_reissued = 0;
      uint64_t pf_l2c = 0;
      uint64_t pf_degree_limited = 0;

      uint64_t pend_cycles = 0;
      uint64_t resolutions = 0;

      //uint64_t global_usefulness_counter = MAX_USEFULNESS_COUNTER >> 1;
      double global_conf = 1.0;
      
      struct page_extent : champsim::dynamic_extent {
        page_extent() : dynamic_extent(champsim::data::bits{64}, champsim::data::bits{SPPAM_B_PAGE_BITS}) {}
      };
      using page = champsim::address_slice<page_extent>;

      struct block_in_page_extent : champsim::dynamic_extent {
        block_in_page_extent() : dynamic_extent(champsim::data::bits{SPPAM_B_PAGE_BITS}, champsim::data::bits{LOG2_BLOCK_SIZE}) {}
      };
      using block_in_page = champsim::address_slice<block_in_page_extent>;

      struct recency_stack_type {
        page vpn;
        dynamic_bitset recent_segments;
        bool direction = true;
        bool operator==(const recency_stack_type& other) const {
          return vpn == other.vpn;
        }
        recency_stack_type() : recency_stack_type(page{}, 64) {}
        explicit recency_stack_type(page allocate_vpn, std::size_t seg_bits = 64)
          : vpn(allocate_vpn), recent_segments(seg_bits)
        {
        }
      };
      struct region_type {
        page vpn;
        std::bitset< (1 << SPPAM_B_PAGE_BITS) / 64> access_map;
        std::bitset< (1 << SPPAM_B_PAGE_BITS) / 64> prefetch_map;
        std::bitset< (1 << SPPAM_B_PAGE_BITS) / 64> ll_map;
        std::bitset< (1 << SPPAM_B_PAGE_BITS) / 64> prefetch_debug_map;
        uint64_t last_block = 0;
        //std::vector<champsim::address> last_ip = std::vector<champsim::address>(champsim::lg2(MAX_HISTORY_SIZE) - champsim::lg2(MIN_HISTORY_SIZE) + 1,champsim::address{});
        champsim::address last_ip = champsim::address{};
        uint64_t misses = 0;
        uint64_t hits = 0;

        uint64_t budget = 0;
        int64_t avail_prefetches = 0;
        uint64_t last_cycle = 0;

        uint64_t useless = 0;
        uint64_t useful = 0;

        region_type() : region_type(page{}) {}
        explicit region_type(page allocate_vpn)
          : vpn(allocate_vpn), budget(REGION_BUDGET), avail_prefetches(budget)
        {
        }

        //equals operator
        bool operator==(const region_type& other) const {
          return vpn == other.vpn;
        }
      };

      struct ip_track_entry {
        champsim::block_number addr;
        champsim::address ip;
        uint64_t last_cycle = 0;
      };

      struct ip_conf_entry {
        champsim::address ip;
        double conf = 1.0;
        int64_t dir_counter = DIRECTION_SAMPLE_MAX / 2; //same for direction
        int64_t lookahead_depth = 64; // initialized to MAX_LOOKAHEAD default
        int64_t pf_degree = 8; // initialized to PREFETCH_DEGREE default
        uint64_t useful = 0;
        uint64_t useless = 0;
        uint64_t lifetime_samples = 0;
        ip_conf_entry() : ip_conf_entry(champsim::address{}) {}
        explicit ip_conf_entry(champsim::address allocate_ip)
          : ip(allocate_ip) {}
      };

      struct ip_conf_indexer {
        auto operator()(const ip_conf_entry& entry) const { return entry.ip; }
      };

      struct sppam_b_indexer {
        page operator()(const region_type& entry) const { return entry.vpn; }
      };
      sppam_b_util::msl::lru_table<region_type,sppam_b_indexer,sppam_b_indexer> regions{REGION_SETS,REGION_WAYS};
      std::array<ip_track_entry,IP_TRACK_TABLE_SIZE> ip_track_table;
      sppam_b_util::msl::lru_table<ip_conf_entry,ip_conf_indexer,ip_conf_indexer> ip_conf_table{IP_CONF_TABLE_SETS,IP_CONF_TABLE_WAYS};


    void update_delta_history(std::vector<uint64_t>& delta_history, int64_t delta);
    void update_recency_stack(std::vector<recency_stack_type>& recency_stack, champsim::address addr, champsim::address ip);

    champsim::address get_bp_ip(champsim::address addr, champsim::address pf_addr, champsim::address ip, std::vector<uint64_t> delta_history_temp);

    void update_local_history(dynamic_bitset& history, champsim::address addr, bool taken, bool direction);
    void update_global_history(dynamic_bitset& history, champsim::address addr, bool taken, bool direction);
    void scan_global_history(dynamic_bitset& history, champsim::address addr, champsim::address ip, bool direction);
    void scan_local_history(dynamic_bitset& history,champsim::address addr, champsim::address ip, bool direction);
    dynamic_bitset get_local_access_window(champsim::address addr, champsim::address ip, int offset, bool direction);
    dynamic_bitset get_segment_access_window(champsim::address addr, champsim::address ip, int offset, bool direction);
    dynamic_bitset get_global_access_window(champsim::address addr, champsim::address ip, int offset, bool direction);
    //std::vector<region_type> get_temp_region_stack(champsim::address addr, champsim::address ip);
    void add_to_pagemap(champsim::address addr, bool prefetch, bool useful = false);
    void add_to_debugmap(champsim::address addr);
    void add_to_llmap(champsim::address addr);
    bool check_pagemap(champsim::address addr, bool prefetch);
    bool check_llmap(champsim::address addr);
    void remove_from_pagemap(champsim::address addr, bool prefetch);

    double convert_to_conf(double predict_conf);

    double get_bw_conf();

    double get_conf(champsim::address ip);
    std::size_t get_lookahead(champsim::address ip);
    std::size_t get_pf_degree(champsim::address ip);

    bool get_direction(champsim::address ip);

    double should_issue(double conf);
    double should_reissue(double conf);

    dynamic_bitset get_local_history(champsim::address addr, champsim::address ip, bool direction);
    dynamic_bitset get_global_history(champsim::address addr, champsim::address ip, bool direction);

    void update_local_and_global_contexts(champsim::address addr, champsim::address ip);

    void log_outcome(champsim::address addr, champsim::address ip);

    void print_patterns();

    template <typename T>
    static auto page_and_offset(T addr) -> std::pair<page, block_in_page>;
    void do_prefetch(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in);

    void initialize(CACHE* cache);

    uint64_t get_state_bits();

    void tally_useful(champsim::address addr);
    void tally_useless(champsim::address addr);


    void update_ip_conf(champsim::address ip, bool useful);
    void track_ip(champsim::address addr, champsim::address ip);
    void resolve_ip(champsim::block_number addr, bool useful);
    void track_direction(champsim::address addr, champsim::address ip, bool direction);

    std::vector<recency_stack_type> recency_stack;
    page last_region{};
    champsim::address last_ip{};
    champsim::address last_addr{};
    dynamic_bitset global_history_reg;
    std::vector<champsim::address> ip_history;
    std::vector<uint64_t> delta_history;
    std::map<int64_t,int64_t> delta_freq;
    std::map<uint64_t,uint64_t> lookahead_freq;
    branch_predictor* predictor;
    bp_context bp_ctx;
    uint64_t last_cycle = 0;

    uint64_t ip_history_correct = 0;
    uint64_t ip_history_wrong = 0;
    };



  
  
  uint64_t prev_useless_prefetches = 0;
  Sppam_b_Module engine;
  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  void prefetcher_initialize() {
    engine.initialize(intern_);
  }
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();
  
};

#endif