#ifndef PREFETCHER_SPPAM_B_H
#define PREFETCHER_SPPAM_B_H

#include <array>
#include <bitset>
#include <cstdint>
#include <vector>
#include <optional>

#include "champsim.h"
#include "modules.h"
#include "conf_table.h"
#include "dpc_api.h"
#include "instruction.h"

#include "branch_predictor.h"
#include "gshare/gshare.h"

class sppam_b : public champsim::modules::prefetcher
{

  public:
    enum GLOBAL_HISTORY_TYPE {
      OFFSET_HISTORY, //the history of this offset in the last N regions
      RECENT_SEGMENT_HISTORY, //the history of the most recent segment in the last N regions
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
      NONE //branch predictor is fed all 0's
    };
    struct Sppam_b_Module {
      CACHE* intern_;

      static constexpr std::size_t REGION_SETS = 2048;
      static constexpr std::size_t REGION_WAYS = 1;
      static constexpr bool CLEAR_ACCESS_MAP_ON_EVICT = false;

      //IP tracking table (for usefulness)
      static constexpr std::size_t IP_TRACK_TABLE_SIZE = 128;
      static constexpr uint64_t IP_TRACK_TABLE_TIMEOUT = 10000;

      static constexpr bool USE_REGION_IP_FOR_LEARNING = true;
      static constexpr bool USE_REGION_ADDR_FOR_GLOBAL_HIST_LEARNING = true;

      static constexpr std::size_t IP_CONF_TABLE_SETS = 128;
      static constexpr std::size_t IP_CONF_TABLE_WAYS = 16;
      //samples for establishing IP usefulness
      static constexpr std::size_t IP_CONF_TABLE_SAMPLES = 64;

      //probability to fill into the ip tracking table
      static constexpr std::size_t TEMPORAL_SAMPLE_RATE = 1; //sample every N accesses for usefulness learning

      //direction sampler
      static constexpr int64_t DIRECTION_SAMPLE_MAX = 4;

      //max pf degree
      static constexpr std::size_t PREFETCH_DEGREE = 8;
      static constexpr std::size_t REGION_BUDGET = 22;

      static constexpr unsigned int SPPAM_B_PAGE_BITS = 12;

      //lookahead limits
      static constexpr bool DO_LOOKAHEAD = true;
      static constexpr double MIN_LOOKAHEAD_CONF = 0.0;
      static constexpr uint64_t MAX_LOOKAHEAD = 22;

      static constexpr double TIMELINESS_CYCLE = 10000; 

      //debug 
      static constexpr uint64_t DO_DEBUG = false;
      static constexpr bool CROSS_PAGE = false;

      //bp stuff
      static constexpr IP_TYPE ip_style = IP_TYPE::IP; //set IP type
      static constexpr GLOBAL_HISTORY_TYPE global_type = GLOBAL_HISTORY_TYPE::RECENT_SEGMENT_HISTORY; //set global history type
      
      std::array<double,16> PREFETCH_DEGREES_BW = {1.00,1.00,1.00,1.00,0.90,0.90,0.90,0.90,0.80,0.80,0.80,0.75,0.75,0.50,0.50,0.50}; //normal
      std::array<double,16> PREFETCH_ISSUE_CHANCE = {0.0315,0.056,0.134,0.134,0.36,0.607,0.90,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0}; //normal
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
      uint64_t prefetches_scanned_forward = 0;
      uint64_t prefetches_scanned_backward = 0;
      uint64_t direction_global = 0;
      uint64_t direction_local = 0;

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
        std::bitset<BP_SEGMENT_BITS> recent_segments; //the most recent segments accessed within this page, indexed by block_in_page
        bool direction = true;
        bool operator==(const recency_stack_type& other) const {
          return vpn == other.vpn;
        }
        recency_stack_type() : recency_stack_type(page{}) {}
        explicit recency_stack_type(page allocate_vpn)
          : vpn(allocate_vpn)
        {
        }
      };
      struct region_type {
        page vpn;
        std::bitset< (1 << SPPAM_B_PAGE_BITS) / 64> access_map;
        std::bitset< (1 << SPPAM_B_PAGE_BITS) / 64> prefetch_map;
        std::bitset< (1 << SPPAM_B_PAGE_BITS) / 64> prefetch_debug_map;
        uint64_t last_block = 0;
        //std::vector<champsim::address> last_ip = std::vector<champsim::address>(champsim::lg2(MAX_HISTORY_SIZE) - champsim::lg2(MIN_HISTORY_SIZE) + 1,champsim::address{});
        champsim::address last_ip = champsim::address{};
        uint64_t misses = 0;
        uint64_t hits = 0;

        uint64_t budget = 0;
        uint64_t avail_prefetches = 0;
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
        champsim::address addr;
        champsim::address ip;
        uint64_t last_cycle = 0;
      };

      struct ip_conf_entry {
        champsim::address ip;
        double conf = 1.0;
        int64_t dir_counter = DIRECTION_SAMPLE_MAX / 2; //same for direction
        uint64_t useful = 0;
        uint64_t useless = 0;
        ip_conf_entry() : ip_conf_entry(champsim::address{}) {}
        explicit ip_conf_entry(champsim::address allocate_ip)
          : ip(allocate_ip) {}
      };

      struct ip_conf_indexer {
        auto operator()(const ip_conf_entry& entry) const { return entry.ip; }
      };

      struct sppam_b_indexer {
        auto operator()(const region_type& entry) const { return entry.vpn; }
      };
      sppam_b_util::msl::lru_table<region_type,sppam_b_indexer,sppam_b_indexer> regions{REGION_SETS,REGION_WAYS};
      std::array<ip_track_entry,IP_TRACK_TABLE_SIZE> ip_track_table;
      sppam_b_util::msl::lru_table<ip_conf_entry,ip_conf_indexer,ip_conf_indexer> ip_conf_table{IP_CONF_TABLE_SETS,IP_CONF_TABLE_WAYS};


    champsim::address get_bp_ip(champsim::address addr, champsim::address ip);

    void update_local_history(std::bitset<BP_LOCAL_BITS>& history, champsim::address addr, bool taken, bool direction);
    void update_global_history(std::bitset<BP_GLOBAL_BITS>& history, champsim::address addr, bool taken, bool direction);
    void scan_global_history(std::bitset<BP_GLOBAL_BITS>& history, champsim::address addr, champsim::address ip, bool direction);
    void scan_local_history(std::bitset<BP_LOCAL_BITS>& history,champsim::address addr, champsim::address ip, bool direction);
    std::bitset<BP_LOCAL_BITS> get_local_access_window(champsim::address addr, champsim::address ip, int offset, bool direction);
    std::bitset<BP_SEGMENT_BITS> get_segment_access_window(champsim::address addr, champsim::address ip, int offset, bool direction);
    std::bitset<BP_GLOBAL_BITS> get_global_access_window(champsim::address addr, champsim::address ip, int offset, bool direction);
    //std::vector<region_type> get_temp_region_stack(champsim::address addr, champsim::address ip);
    void add_to_pagemap(champsim::address addr, bool prefetch, bool useful = false);
    void add_to_debugmap(champsim::address addr);
    bool check_pagemap(champsim::address addr, bool prefetch);
    void remove_from_pagemap(champsim::address addr, bool prefetch);

    double convert_to_conf(double predict_conf);

    double get_bw_conf();

    double get_conf();

    bool get_direction(champsim::address addr, champsim::address ip);

    double should_issue(double conf);

    std::bitset<BP_LOCAL_BITS> get_local_history(champsim::address addr, champsim::address ip, bool direction);
    std::bitset<BP_GLOBAL_BITS> get_global_history(champsim::address addr, champsim::address ip, bool direction);
    void update_history_metadata(std::bitset<BP_GLOBAL_BITS>& global_hist, champsim::address addr, champsim::address ip);

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
    void resolve_ip(champsim::address addr, bool useful);
    void track_direction(champsim::address addr, champsim::address ip, bool direction);

    std::vector<recency_stack_type> recency_stack;
    page last_region{};
    champsim::address last_ip{};
    champsim::address last_addr{};
    std::bitset<BP_GLOBAL_BITS> global_history_reg;
    std::vector<champsim::address> ip_history;
    branch_predictor* predictor;
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