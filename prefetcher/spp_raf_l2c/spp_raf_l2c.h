#ifndef SPP_RAF_L2C_H
#define SPP_RAF_L2C_H

#include <cstdint>
#include <vector>
#include <bitset>
#include <algorithm>
#include "msl/lru_table.h"
#include "modules.h"
#include "cache.h"

class spp_raf_l2c : public champsim::modules::prefetcher {

  //SPP functional knobs
  constexpr static bool LOOKAHEAD_ON = true;
  constexpr static bool FILTER_ON = true;
  constexpr static bool GHR_ON = true;
  constexpr static bool SPP_SANITY_CHECK = true;
  constexpr static bool SPP_DEBUG_PRINT = false;

  // Signature table parameters
  constexpr static std::size_t ST_SET = 1;
  constexpr static std::size_t ST_WAY = 256;
  constexpr static unsigned ST_TAG_BIT = 16;
  constexpr static unsigned SIG_SHIFT = 3;
  constexpr static unsigned SIG_BIT = 12;
  constexpr static uint32_t SIG_MASK = ((1 << SIG_BIT) - 1);
  constexpr static unsigned SIG_DELTA_BIT = 7;

  // Pattern table parameters
  constexpr static std::size_t PT_SET = 512;
  constexpr static std::size_t PT_WAY = 4;
  constexpr static unsigned C_SIG_BIT = 4;
  constexpr static unsigned C_DELTA_BIT = 4;
  constexpr static uint32_t C_SIG_MAX = ((1 << C_SIG_BIT) - 1);
  constexpr static uint32_t C_DELTA_MAX = ((1 << C_DELTA_BIT) - 1);

  // Prefetch filter parameters
  constexpr static unsigned QUOTIENT_BIT = 10;
  constexpr static unsigned REMAINDER_BIT = 6;
  constexpr static unsigned HASH_BIT = (QUOTIENT_BIT + REMAINDER_BIT + 1);
  constexpr static std::size_t FILTER_SET = (1 << QUOTIENT_BIT);
  constexpr static uint32_t FILL_THRESHOLD = 90;
  constexpr static uint32_t PF_THRESHOLD = 25;

  //Row-access table
  constexpr static unsigned DRAM_GROUPS = 32;
  constexpr static unsigned DRAM_BLOCK_COLUMNS = 128;

  constexpr static unsigned RAT_SETS = DRAM_GROUPS;
  constexpr static unsigned RAT_WAYS = 1;

  constexpr static unsigned RAM_SETS = 1;
  constexpr static unsigned RAM_WAYS = 128;
  constexpr static unsigned RAM_VECTOR = DRAM_GROUPS;

  constexpr static long long int NRAM_PROM = 10;
  constexpr static long long int NRAM_FILT = -5;

  constexpr static unsigned NRAM_SETS = 1;
  constexpr static unsigned NRAM_WAYS = 128;
  constexpr static unsigned NRAM_VECTOR = DRAM_GROUPS;

  constexpr static unsigned RAD_DELAY_HIT = 30;
  constexpr static unsigned RAD_DELAY_MISS = 150;
  constexpr static unsigned RAD_ISSUE_THRESH = 8;
  constexpr static unsigned RAD_SETS = DRAM_GROUPS;
  constexpr static unsigned RAD_WAYS = 1;
  constexpr static unsigned RAD_VECTOR = DRAM_BLOCK_COLUMNS;

  //bloom filter stuff
  constexpr static unsigned BLOOM_LEVELS = 3;
  constexpr static unsigned BLOOM_BIT_DEPTH = 1;
  constexpr static unsigned BLOOM_ENTRIES = 150;
  constexpr static unsigned SAC_LIMIT = 512;

  //act counter stuff
  constexpr static unsigned RFM_THRESH = 16;

  //probabilistic approach
  constexpr static float BLOOM_SET_PROB = 0.1;
  constexpr static float BLOOM_RESET_PROB = 0.01;

  constexpr static unsigned BLOOM_RESET_INTERVAL_MS = 32;

  // Global register parameters
  constexpr static unsigned GLOBAL_COUNTER_BIT = 10;
  constexpr static uint32_t GLOBAL_COUNTER_MAX = ((1 << GLOBAL_COUNTER_BIT) - 1);
  constexpr static std::size_t MAX_GHR_ENTRY = 8;


  uint64_t current_cycle = 0;
  public:
    static std::vector<spp_raf_l2c*> spp_impls;

  
  enum FILTER_REQUEST { SPP_L2C_PREFETCH, SPP_LLC_PREFETCH, L2C_DEMAND, L2C_EVICT }; // Request type for prefetch filter
  static uint64_t get_hash(uint64_t key);

  struct block_in_page_extent : champsim::dynamic_extent {
    block_in_page_extent() : dynamic_extent(champsim::data::bits{LOG2_PAGE_SIZE}, champsim::data::bits{LOG2_BLOCK_SIZE}) {}
  };
  using offset_type = champsim::address_slice<block_in_page_extent>;

  class ACT_COUNTER
  {
    std::vector<uint64_t> counter;
    public:
    ACT_COUNTER() : counter(DRAM_GROUPS,0){};

    bool increment(unsigned long entry) {
      entry = entry % DRAM_GROUPS;
      counter[entry]++;
      if(counter[entry] >= RFM_THRESH){
        for (auto it = counter.begin(); it != counter.end(); it++)
          (*it) = 0;
        
        fmt::print("reset act counter\n");
        return true;
      }
      return false;
    }
  };

  template<std::size_t cap>
  class PREFETCH_QUEUE
  {
    std::deque<std::tuple<bool,champsim::address,uint64_t>> to_prefetch;
    public:
    void push(champsim::address a, bool prefetch_this_level, uint64_t time) {
      if(std::size(to_prefetch) >= cap)
        return;
      to_prefetch.push_back({prefetch_this_level,a,time});
      //return true;
    }

    bool is_empty() {
      return to_prefetch.empty();
    }
    std::tuple<bool,champsim::address,uint64_t> front() {
      return to_prefetch.front();
    }
    void pop() {
      to_prefetch.pop_front();
    }
  };
  class SATURATING_ACT_COUNTER
  {
    std::vector<uint64_t> counter;
    public:
    SATURATING_ACT_COUNTER() : counter(DRAM_GROUPS,0){};

    bool check(unsigned long entry) {
      entry = entry % DRAM_GROUPS;
      if(counter[entry] >= SAC_LIMIT)
        return true;
      return false;
    }
    void increment(unsigned long entry) {
      entry = entry % DRAM_GROUPS;
      if(counter[entry] < SAC_LIMIT)
        counter[entry]++;
    }

    void reset(unsigned long entry) {
      entry = entry % DRAM_GROUPS;
      counter[entry] = 0;
    }

    void print() {
      fmt::print("\t[{}]\n",fmt::join(counter,","));
    }
  };
  class BLOOM_FILTER
  {
    std::vector<std::vector<unsigned>> filters;
    private:
      uint64_t calc_hash(unsigned level, uint64_t addr) {
        uint64_t rolling_hash_a = 0;
        uint64_t rolling_hash_b = 0;
        uint64_t naddr = ~addr;
        for(unsigned i = 0; i <= level; i++) {
          if(i % 2 == 0) {
            rolling_hash_a += get_hash(addr >> ((i/2)*4));
          }
          else {
            rolling_hash_b += get_hash(naddr >> ((i/2)*4));
          }
        }
        //fmt::print("hash{} : {}\n",level,rolling_hash_a + rolling_hash_b);
        
        return(rolling_hash_a + rolling_hash_b);
      }
    public:
      void reset(champsim::address addr) {
        for(unsigned i = 0; i < BLOOM_LEVELS; i++) {
          uint64_t hash = calc_hash(i,addr.to<uint64_t>()) % BLOOM_ENTRIES;
          filters[i][hash] = 0;
        }
      }
      void reset() {
        for(unsigned i = 0; i < BLOOM_LEVELS; i++) {
          for(auto it = filters[i].begin(); it != filters[i].end(); it++) {
            (*it) = 0;
          }
        }
      }
      void set(champsim::address addr) {
        for(unsigned i = 0; i < BLOOM_LEVELS; i++) {
          uint64_t hash = calc_hash(i,addr.to<uint64_t>()) % BLOOM_ENTRIES;
          if(filters[i][hash] < (1 << BLOOM_BIT_DEPTH) - 1)
            filters[i][hash]++;
        }
      }
      bool check(champsim::address addr) {
        bool filtered = true;
        for(unsigned i = 0; i < BLOOM_LEVELS; i++) {
          uint64_t hash = calc_hash(i,addr.to<uint64_t>()) % BLOOM_ENTRIES;
          //fmt::print("hash: {}\n",hash);
          if(filters[i][hash] < (1 << BLOOM_BIT_DEPTH) - 1)
            filtered = false;
        }
        //fmt::print("filtered: {}\n",filtered);
        return(filtered);
      }

      BLOOM_FILTER() {
        for(unsigned i = 0; i < BLOOM_LEVELS; i++) {
          std::vector<unsigned> bf(BLOOM_ENTRIES,0);
          filters.push_back(bf);
        }
      }

      void print() {
        for(unsigned i = 0; i < BLOOM_LEVELS; i++) {
          fmt::print("LEVEL:{}\n",i);
          fmt::print("\t[{}]\n",fmt::join(filters[i],","));
        }
      }
  };
  class TABLE_BLOOM_FILTER
  {
    std::vector<BLOOM_FILTER> bloom_filters;
    public:

    TABLE_BLOOM_FILTER(): bloom_filters(DRAM_GROUPS) {};

    void set(unsigned long row_buffer, champsim::address row) {
      bloom_filters[row_buffer % DRAM_GROUPS].set(row);
    }

    bool check(unsigned long row_buffer, champsim::address row) {
      return bloom_filters[row_buffer % DRAM_GROUPS].check(row);
    }

    void reset(unsigned long row_buffer) {
      bloom_filters[row_buffer % DRAM_GROUPS].reset();
    }
    void reset(unsigned long row_buffer, champsim::address row) {
      bloom_filters[row_buffer % DRAM_GROUPS].reset(row);
    }

    void reset_all() {
      for(auto& bf : bloom_filters)
        bf.reset();
    }

    void print() {
      for(auto& bf : bloom_filters)
        bf.print();
    }
  };
  class SIGNATURE_TABLE
  {
    struct tag_extent : champsim::dynamic_extent {
      tag_extent() : dynamic_extent(champsim::data::bits{ST_TAG_BIT + LOG2_PAGE_SIZE}, champsim::data::bits{LOG2_PAGE_SIZE}) {}
    };

  public:
    spp_raf_l2c* _parent;
    using tag_type = champsim::address_slice<tag_extent>;

    bool valid[ST_SET][ST_WAY];
    tag_type tag[ST_SET][ST_WAY];
    offset_type last_offset[ST_SET][ST_WAY];
    uint32_t sig[ST_SET][ST_WAY], lru[ST_SET][ST_WAY];

    SIGNATURE_TABLE()
    {
      for (uint32_t set = 0; set < ST_SET; set++)
        for (uint32_t way = 0; way < ST_WAY; way++) {
          valid[set][way] = 0;
          tag[set][way] = tag_type{};
          last_offset[set][way] = offset_type{};
          sig[set][way] = 0;
          lru[set][way] = way;
        }
      
    };

    void read_and_update_sig(champsim::address addr, uint32_t& last_sig, uint32_t& curr_sig, typename offset_type::difference_type& delta);
  };

  class PATTERN_TABLE
  {
  public:
    spp_raf_l2c* _parent;
    typename offset_type::difference_type delta[PT_SET][PT_WAY];
    uint32_t c_delta[PT_SET][PT_WAY], c_sig[PT_SET];

    PATTERN_TABLE()
    {
      for (uint32_t set = 0; set < PT_SET; set++) {
        for (uint32_t way = 0; way < PT_WAY; way++) {
          delta[set][way] = 0;
          c_delta[set][way] = 0;
        }
        c_sig[set] = 0;
      }
    }

    void update_pattern(uint32_t last_sig, typename offset_type::difference_type curr_delta);
    void read_pattern(uint32_t curr_sig, std::vector<typename offset_type::difference_type>& prefetch_delta, std::vector<uint32_t>& confidence_q,
                      uint32_t& lookahead_way, uint32_t& lookahead_conf, uint32_t& pf_q_tail, uint32_t& depth);
  };

  class PREFETCH_FILTER
  {
  public:
    spp_raf_l2c* _parent;
    uint64_t remainder_tag[FILTER_SET];
    bool valid[FILTER_SET], // Consider this as "prefetched"
        useful[FILTER_SET]; // Consider this as "used"

    uint64_t filtered = 0;
    uint64_t promoted = 0;
    uint64_t modified = 0;
    uint64_t total = 0;

    struct ram_table_entry {
      unsigned int row_id;

      unsigned int first_used;

      std::bitset<RAM_VECTOR> groups{};

      ram_table_entry() : ram_table_entry(0,0) {}
      explicit ram_table_entry(uint64_t row_id_, uint64_t first_used_) : row_id(row_id_), first_used(first_used_) {}
    };

    struct ram_indexer {
      auto operator()(const ram_table_entry& entry) const { return entry.row_id; }
    };

    struct rat_table_entry {
      unsigned int row_id;
      unsigned int bank_id;
      unsigned int first_used;
      rat_table_entry() : rat_table_entry(0,0,0) {}
      explicit rat_table_entry(uint64_t row_id_, uint64_t bank_id_, uint64_t first_used_) : row_id(row_id_), bank_id(bank_id_), first_used(first_used_) {}
    };
    struct rat_set_indexer {
      auto operator()(const rat_table_entry& entry) const { return entry.bank_id; }
    };
    struct rat_way_indexer {
      auto operator()(const rat_table_entry& entry) const { return entry.row_id; }
    };

    struct rad_table_entry {
      unsigned int row_id;
      unsigned int bank_id;

      unsigned int first_used;

      //std::bitset<RAD_VECTOR> blocks{};
      std::vector<champsim::address> blocks;
      std::vector<bool>              fill_this_level;
      std::vector<uint32_t>          metadata;

      rad_table_entry() : rad_table_entry(0,0,0) {}
      explicit rad_table_entry(uint64_t row_id_, uint64_t bank_id_, uint64_t first_used_) : row_id(row_id_), bank_id(bank_id_), first_used(first_used_), blocks(RAD_VECTOR), fill_this_level(RAD_VECTOR,true), metadata(RAD_VECTOR) {}
    };
    struct rad_set_indexer {
      auto operator()(const rad_table_entry& entry) const { return entry.bank_id; }
    };
    struct rad_way_indexer {
      auto operator()(const rad_table_entry& entry) const { return entry.row_id; }
    };

    struct ptb_entry {
      champsim::block_number block;
      uint64_t first_entered;

      ptb_entry() : ptb_entry(champsim::block_number{0},0) {}
      explicit ptb_entry(champsim::block_number block_, uint64_t first_entered_) : block(block_), first_entered(first_entered_) {}
    };

    struct ptb_indexer {
      auto operator()(const ptb_entry& entry) const {return entry.block;};
    };

    champsim::msl::lru_table<ram_table_entry,ram_indexer,ram_indexer> ram_table{RAM_SETS,RAM_WAYS};

    champsim::msl::lru_table<ram_table_entry,ram_indexer,ram_indexer> nram_table{NRAM_SETS,NRAM_WAYS};

    champsim::msl::lru_table<rat_table_entry,rat_set_indexer,rat_way_indexer> rat_table{RAT_SETS,RAT_WAYS};

    champsim::msl::lru_table<ptb_entry,ptb_indexer,ptb_indexer> ptb_table{1,32};

    TABLE_BLOOM_FILTER rat_bloom_filter;
    ACT_COUNTER rat_act_counter;
    SATURATING_ACT_COUNTER rat_sact_counter;
    uint64_t reset_timer = 0;

    champsim::msl::lru_table<rad_table_entry,rad_set_indexer,rad_way_indexer> rad_table{RAD_SETS,RAD_WAYS};

    PREFETCH_FILTER()
    {
      for (uint32_t set = 0; set < FILTER_SET; set++) {
        remainder_tag[set] = 0;
        valid[set] = 0;
        useful[set] = 0;
      }
    }

    bool check(champsim::address pf_addr, FILTER_REQUEST filter_request, unsigned int confidence = 0);

    unsigned int raf_rb(champsim::address pf_addr);

    bool raf_check(champsim::address pf_addr, unsigned long confidence);


    //ram table
    void set_ram_table(champsim::address pf_addr);
    bool check_ram_table(champsim::address pf_addr);

    void set_nram_table(champsim::address pf_addr);
    void reset_nram_table(champsim::address pf_addr);
    bool check_nram_table(champsim::address pf_addr);

    void set_rat_table(champsim::address addr, bool is_prefetch);
    bool check_rat_table(champsim::address pf_addr);
    bool filter_prefetch_rat(champsim::address addr);
    void reset_filter(champsim::address addr);

    void add_rad_table(champsim::address pf_addr, bool fill_this_level, uint32_t metadata);
    void thresh_issue_rad_table(rad_table_entry rte);
    void time_issue_rad_table(rad_table_entry rte);
    void demand_issue_rad_table(champsim::address pf_addr);
    void issue_rad_table(rad_table_entry rte);
    void inval_rad_table(rad_table_entry rte);

    void cache_operate_rad_table(champsim::address pf_addr, bool fill_this_level, uint32_t metadata);
    void cycle_operate_rad_table();

    void inval_ptb_table(champsim::address pf_addr);
    void update_ptb_table(champsim::address pf_addr);
    bool check_ptb_table(champsim::address pf_addr);

    uint64_t get_column_block(champsim::address pf_addr);

    uint64_t get_ram_conf(uint64_t confidence, champsim::address pf_addr);

    uint64_t calc_nram_hash(champsim::address pf_addr, unsigned int ind);
  };

  class GLOBAL_REGISTER
  {
  public:
    spp_raf_l2c* _parent;
    // Global counters to calculate global prefetching accuracy
    uint32_t pf_useful, pf_issued;
    uint32_t global_accuracy; // Alpha value in Section III. Equation 3

    // Global History Register (GHR) entries
    uint8_t valid[MAX_GHR_ENTRY];
    uint32_t sig[MAX_GHR_ENTRY], confidence[MAX_GHR_ENTRY];
    offset_type offset[MAX_GHR_ENTRY];
    typename offset_type::difference_type delta[MAX_GHR_ENTRY];

    GLOBAL_REGISTER()
    {
      pf_useful = 0;
      pf_issued = 0;
      global_accuracy = 0;

      for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
        valid[i] = 0;
        sig[i] = 0;
        confidence[i] = 0;
        offset[i] = offset_type{};
        delta[i] = 0;
      }
    }

    void update_entry(uint32_t pf_sig, uint32_t pf_confidence, offset_type pf_offset, typename offset_type::difference_type pf_delta);
    uint32_t check_entry(offset_type page_offset);
  };

  public:
  SIGNATURE_TABLE ST;
  PATTERN_TABLE PT;
  PREFETCH_FILTER FILTER;
  GLOBAL_REGISTER GHR;
  PREFETCH_QUEUE<128> PQ;

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);

  void prefetcher_initialize();
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();

  

};


#endif
