/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CACHE_H
#define CACHE_H

#ifdef CHAMPSIM_MODULE
#define SET_ASIDE_CHAMPSIM_MODULE
#undef CHAMPSIM_MODULE
#endif

#include <array>
#include <cstddef> // for size_t
#include <cstdint> // for uint64_t, uint32_t, uint8_t
#include <deque>
#include <iterator> // for size
#include <limits>   // for numeric_limits
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <optional>

#include "address.h"
#include "bandwidth.h"
#include "block.h"
#include "cache_builder.h"
#include "cache_stats.h"
#include "champsim.h"
#include "channel.h"
#include "chrono.h"
#include "modules.h"
#include "operable.h"
#include "util/to_underlying.h" // for to_underlying
#include "waitable.h"
#include "msl/lru_table.h"
#include "dram_controller.h"

template<typename T>
class TemporalMergeQueue {
  private:
  std::vector<T> queue;
  std::size_t max;
  std::size_t size;
  constexpr static std::size_t RAF_FILTER_SETS = 8;
  constexpr static std::size_t RAF_FILTER_WAYS = 16;

  public:

    TemporalMergeQueue& operator=(const TemporalMergeQueue& other) {
      this->queue = std::move(other.queue);
      this->max = other.max;
      this->size = other.size;

      return *this;
    }

    TemporalMergeQueue(std::size_t max_) : max(max_),  queue(max_,T{}), size(0) {}

    bool is_empty() {
      return(size == 0);
    }
    bool is_full() {
      return(size == max);
    }

    std::size_t get_size() const{
      return size;
    }

    std::size_t get_max() const{
      return max;
    }

    void pop() {
      //assert that queue isn't empty
      assert(size != 0);
      size--;
      std::rotate(queue.begin(), queue.begin() + 1,queue.end());
    }

    void pop_n(std::size_t n) {
      assert(size >= n);
      size -= n;
      
      std::rotate(queue.begin(),queue.begin() + n, queue.end());
    }

    bool push(T entry, uint64_t timestamp) {

      //perform merge, start at back and work forward in time. 
      //Once we pass the timeout threshold, stop checking
      //navigate valid part of queue first
      for(std::size_t pos = 1; pos <= size; pos++) {
        if(queue.at(size-pos) == entry) {
          return true;
        }
      }

      //couldn't merge? now check to see if full.
      if(size == max)
        return false;

      //insert into queue, couldn't merge
      queue.at(size) = entry;
      size++;
      return true;
    }

    std::vector<T> get() {
      return std::vector<T>(queue.begin(),queue.begin() + size);
    }

};
class CACHE : public champsim::operable
{
  enum [[deprecated(
      "Prefetchers may not specify arbitrary fill levels. Use CACHE::prefetch_line(pf_addr, fill_this_level, prefetch_metadata) instead.")]] FILL_LEVEL{
      FILL_L1 = 1, FILL_L2 = 2, FILL_LLC = 4, FILL_DRC = 8, FILL_DRAM = 16};

  using channel_type = champsim::channel;
  using request_type = typename channel_type::request_type;
  using response_type = typename channel_type::response_type;

  struct tag_lookup_type {
    champsim::address address;
    champsim::address v_address;
    champsim::address data;
    champsim::address ip;
    uint64_t instr_id;

    uint32_t pf_metadata;
    uint32_t cpu;
    CACHE* source_ptr;

    uint8_t lsq_score = 0;
    int pf_distance = 0;

    access_type type;
    bool prefetch_from_this;
    bool invoked_prefetcher = false;
    bool skip_fill;
    bool is_translated;
    bool return_hit_status = false;
    bool translate_issued = false;
    bool back_off = false;
    bool row_act = false;
    bool forward_checked = false;

    uint8_t asid[2] = {std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max()};

    champsim::chrono::clock::time_point event_cycle = champsim::chrono::clock::time_point::max();

    std::vector<uint64_t> instr_depend_on_me{};
    std::vector<std::deque<response_type>*> to_return{};

    explicit tag_lookup_type(request_type req) : tag_lookup_type(req, false, false, nullptr,false) {}
    tag_lookup_type(const request_type& req, bool local_pref, bool skip, CACHE* source_ptr_, bool return_hit_status_);
    tag_lookup_type();

    inline bool operator==(const tag_lookup_type& rhs) {
      return (address == rhs.address && skip_fill == rhs.skip_fill);
    }
  };

public:
  struct mshr_type {
    champsim::address address;
    champsim::address v_address;
    champsim::address ip;
    uint64_t instr_id;

    struct returned_value {
      champsim::address data;
      uint32_t pf_metadata;
    };
    champsim::waitable<returned_value> data_promise{};
    uint32_t cpu;

    uint8_t lsq_score;

    access_type type;
    bool prefetch_from_this;
    bool back_off = false;
    bool row_act = false;
    bool was_promoted = false;

    uint8_t asid[2] = {std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max()};

    champsim::chrono::clock::time_point time_enqueued;

    std::vector<uint64_t> instr_depend_on_me{};
    std::vector<std::deque<response_type>*> to_return{};

    mshr_type(const tag_lookup_type& req, champsim::chrono::clock::time_point _time_enqueued);
    static mshr_type merge(mshr_type predecessor, mshr_type successor);
  };

private:
  bool try_hit(tag_lookup_type& handle_pkt);
  bool handle_fill(const mshr_type& fill_mshr);
  bool handle_miss(const tag_lookup_type& handle_pkt);
  bool handle_write(const tag_lookup_type& handle_pkt);
  bool finish_packet(const response_type& packet);
  void finish_translation(const response_type& packet);

  bool allocate_mshr(const tag_lookup_type& handle_pkt);
  void schedule_mshr();

  void issue_translation(tag_lookup_type& q_entry) const;

public:
  using BLOCK = champsim::cache_block;

private:
  static BLOCK fill_block(mshr_type mshr, uint32_t metadata);
  using set_type = std::vector<BLOCK>;

  static std::map<std::pair<CACHE*,uint32_t>,double> prefetch_usefulness;

  std::pair<set_type::iterator, set_type::iterator> get_set_span(champsim::address address, uint32_t cpu);
  [[nodiscard]] std::pair<set_type::const_iterator, set_type::const_iterator> get_set_span(champsim::address address, uint32_t cpu) const;
  [[nodiscard]] long get_set_index(champsim::address address, uint32_t cpu) const;

  template <typename T>
  bool should_activate_prefetcher(const T& pkt) const;

  template <bool>
  auto initiate_tag_check(champsim::channel* ul = nullptr);

  template <typename T>
  champsim::address module_address(const T& element) const;

  auto matches_address(champsim::address address) const;
  std::pair<mshr_type, request_type> mshr_and_forward_packet(const tag_lookup_type& handle_pkt);

  
  std::deque<tag_lookup_type> inflight_tag_check{};
  std::deque<tag_lookup_type> translation_stash{};

public:
  std::deque<tag_lookup_type> internal_PQ;
  std::vector<channel_type*> upper_levels;
  channel_type* lower_level;
  channel_type* lower_translate;

  uint32_t cpu = 0;
  std::string NAME;
  uint32_t NUM_SET, NUM_WAY, MSHR_SIZE;
  std::size_t PQ_SIZE;
  std::size_t PQM_SIZE;
  std::size_t MQ_SIZE;
  champsim::chrono::clock::duration HIT_LATENCY;
  champsim::chrono::clock::duration FILL_LATENCY;
  champsim::data::bits OFFSET_BITS;
  set_type block{static_cast<typename set_type::size_type>(NUM_SET * NUM_WAY)};
  champsim::bandwidth::maximum_type MAX_TAG, MAX_FILL;
  uint8_t lsq_score = 0;
  bool prefetch_as_load;
  bool match_offset_bits;
  bool virtual_prefetch;
  std::optional<champsim::address> marked_for_drop;
  std::vector<access_type> pref_activate_mask;

  std::vector<std::size_t> prefetches_in_mshr;
  std::vector<std::size_t> demands_in_mshr;
  std::vector<std::size_t> prefetch_limits;
  std::vector<std::size_t> prefetch_counter;
  std::vector<bool> prefetch_hit_limit;

  using stats_type = cache_stats;

  stats_type sim_stats, roi_stats;

  std::deque<mshr_type> MSHR;
  std::deque<mshr_type> inflight_writes;
  std::vector<std::deque<tag_lookup_type>> MQ;
  std::vector<uint64_t> MQ_MISS_COUNTER;
  std::vector<std::size_t> MQ_CORE;
  std::size_t ACTIVE_CORE = 0;
  std::size_t MQ_COUNTER = 0;
  int BANK_DEMAND_THRESHOLD;
  std::vector<int> BANK_PREFETCH_THRESHOLD;
  std::size_t MQ_STARVE = 10;

  uint64_t pf_report_interval = 100000;
  uint64_t print_report_interval = 100000;

  std::vector<tag_lookup_type> PREFETCH_MISS_STORAGE;
  std::deque<std::size_t> PREFETCH_FREE_LIST;
  std::vector<std::deque<std::size_t>> PREFETCH_BANK_QUEUES;

  champsim::address pf_base = champsim::address{};

  std::vector<int> OUTGOING_BANK_REQUESTS;

  bool PQM_ENABLED;
  bool MQC_ENABLED;
  bool CC_ENABLED;
  bool partition_cache;

  void manage_pq();
  long operate() final;
  void initialize() final;
  void begin_phase() final;
  void end_phase(unsigned cpu) final;

  [[deprecated]] std::size_t get_occupancy(uint8_t queue_type, champsim::address address) const;
  [[deprecated]] std::size_t get_size(uint8_t queue_type, champsim::address address) const;

  // NOLINTBEGIN
  [[deprecated("get_occupancy() returns 0 for every input except 0 (MSHR). Use get_mshr_occupancy() instead.")]] std::size_t
  get_occupancy(uint8_t queue_type, uint64_t address) const;
  [[deprecated("get_size() returns 0 for every input except 0 (MSHR). Use get_mshr_size() instead.")]] std::size_t get_size(uint8_t queue_type,
                                                                                                                            uint64_t address) const;
  // NOLINTEND

  [[nodiscard]] std::size_t get_mshr_occupancy() const;
  [[nodiscard]] std::size_t get_mshr_size() const;
  [[nodiscard]] double get_mshr_occupancy_ratio() const;

  [[nodiscard]] std::vector<std::size_t> get_rq_occupancy() const;
  [[nodiscard]] std::vector<std::size_t> get_rq_size() const;
  [[nodiscard]] std::vector<double> get_rq_occupancy_ratio() const;

  [[nodiscard]] std::vector<std::size_t> get_wq_occupancy() const;
  [[nodiscard]] std::vector<std::size_t> get_wq_size() const;
  [[nodiscard]] std::vector<double> get_wq_occupancy_ratio() const;

  [[nodiscard]] std::vector<std::size_t> get_pq_occupancy() const;
  [[nodiscard]] std::vector<std::size_t> get_pq_size() const;
  [[nodiscard]] std::vector<double> get_pq_occupancy_ratio() const;

  [[nodiscard]] double get_cache_occupancy_ratio() const;


  bool check_hit(champsim::address address, uint32_t cpu);

  [[deprecated("Use get_set_index() instead.")]] [[nodiscard]] uint64_t get_set(uint64_t address, uint32_t cpu) const;
  [[deprecated("This function should not be used to access the blocks directly.")]] [[nodiscard]] uint64_t get_way(uint64_t address, uint64_t set, uint32_t cpu) const;

  long invalidate_entry(champsim::address inval_addr, uint32_t cpu);
  std::pair<bool,long> early_writeback(champsim::address wb_addr, uint32_t wb_cpu);

  void drop_prefetch_access(champsim::address pf_addr);

  bool prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t prefetch_metadata);

  bool prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t pf_cpu, champsim::address pf_ip, uint32_t prefetch_metadata, bool skip_tag_check, bool return_hit_status);

  bool prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t pf_cpu, champsim::address pf_ip, uint32_t prefetch_metadata, bool skip_tag_check, bool return_hit_status, int pf_distance);

  [[deprecated]] bool prefetch_line(uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata);

  [[deprecated("Use CACHE::prefetch_line(pf_addr, fill_this_level, prefetch_metadata) instead.")]] bool
  prefetch_line(uint64_t ip, uint64_t base_addr, uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata);

  void print_deadlock() final;

#include "module_decl.inc"

  struct prefetcher_module_concept {
    virtual ~prefetcher_module_concept() = default;

    virtual void bind(CACHE* cache) = 0;

    virtual void impl_prefetcher_initialize() = 0;
    virtual uint32_t impl_prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, bool cache_hit, bool useful_prefetch, access_type type,
                                                   uint32_t metadata_in, uint32_t metadata_hit) = 0;
    virtual uint32_t impl_prefetcher_cache_fill(champsim::address addr, champsim::address ip, uint32_t cpu, bool useless, long set, long way, bool prefetch, champsim::address evicted_addr,
                                                uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict) = 0;
    virtual void impl_prefetcher_cycle_operate() = 0;
    virtual void impl_prefetcher_final_stats() = 0;
    virtual void impl_prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) = 0;
  };

  struct replacement_module_concept {
    virtual ~replacement_module_concept() = default;

    virtual void bind(CACHE* cache) = 0;

    virtual void impl_initialize_replacement() = 0;
    virtual long impl_find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const BLOCK* current_set, champsim::address ip,
                                  champsim::address full_addr, access_type type, bool prefetch) = 0;
    virtual void impl_update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                               champsim::address victim_addr, access_type type, bool hit, bool prefetch) = 0;
    virtual void impl_replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                             champsim::address victim_addr, access_type type, bool prefetch) = 0;
    virtual void impl_replacement_final_stats() = 0;
  };

  template <typename... Ps>
  struct prefetcher_module_model final : prefetcher_module_concept {
    std::tuple<Ps...> intern_;
    explicit prefetcher_module_model(CACHE* cache) : intern_(Ps{cache}...) { (void)cache; /* silence -Wunused-but-set-parameter when sizeof...(Ps) == 0 */ }
    void bind(CACHE* cache)
    {
      std::apply([cache = cache](auto&... p) { (..., p.bind(cache)); }, intern_);
    }

    void impl_prefetcher_initialize() final;
    [[nodiscard]] uint32_t impl_prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, bool cache_hit, bool useful_prefetch, access_type type,
                                                         uint32_t metadata_in, uint32_t metadata_hit) final;
    [[nodiscard]] uint32_t impl_prefetcher_cache_fill(champsim::address addr, champsim::address ip, uint32_t cpu, bool useless, long set, long way, bool prefetch, champsim::address evicted_addr,
                                                      uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict) final;
    void impl_prefetcher_cycle_operate() final;
    void impl_prefetcher_final_stats() final;
    void impl_prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) final;
  };

  template <typename... Rs>
  struct replacement_module_model final : replacement_module_concept {
    // Assert that at least one has an update state
    // static_assert(std::disjunction<champsim::is_detected<has_update_state, Rs>...>::value, "At least one replacement policy must update its state");

    std::tuple<Rs...> intern_;
    explicit replacement_module_model(CACHE* cache) : intern_(Rs{cache}...) { (void)cache; /* silence -Wunused-but-set-parameter when sizeof...(Rs) == 0 */ }
    void bind(CACHE* cache)
    {
      std::apply([cache = cache](auto&... r) { (..., r.bind(cache)); }, intern_);
    }

    void impl_initialize_replacement() final;
    [[nodiscard]] long impl_find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const BLOCK* current_set, champsim::address ip,
                                        champsim::address full_addr, access_type type, bool prefetch) final;
    void impl_update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                       champsim::address victim_addr, access_type type, bool hit, bool prefetch) final;
    void impl_replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                     champsim::address victim_addr, access_type type, bool prefetch) final;
    void impl_replacement_final_stats() final;
  };

  std::unique_ptr<prefetcher_module_concept> pref_module_pimpl;
  std::unique_ptr<replacement_module_concept> repl_module_pimpl;

  // NOLINTBEGIN(readability-make-member-function-const): legacy modules use non-const hooks
  void impl_prefetcher_initialize() const;
  [[nodiscard]] uint32_t impl_prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, bool cache_hit, bool useful_prefetch, access_type type,
                                                       uint32_t metadata_in, uint32_t metadata_hit) const;
  [[nodiscard]] uint32_t impl_prefetcher_cache_fill(champsim::address addr, champsim::address ip, uint32_t cpu, bool useless, long set, long way, bool prefetch, champsim::address evicted_addr,
                                                    uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict) const;
  void impl_prefetcher_cycle_operate() const;
  void impl_prefetcher_final_stats() const;
  void impl_prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) const;

  void impl_initialize_replacement() const;
  [[nodiscard]] long impl_find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const BLOCK* current_set, champsim::address ip,
                                      champsim::address full_addr, access_type type, bool prefetch) const;
  void impl_update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                     champsim::address victim_addr, access_type type, bool hit, bool prefetch) const;
  void impl_replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                   champsim::address victim_addr, access_type type, bool prefetch) const;
  void impl_replacement_final_stats() const;
  // NOLINTEND(readability-make-member-function-const)

  template <typename... Ps, typename... Rs>
  explicit CACHE(champsim::cache_builder<champsim::cache_builder_module_type_holder<Ps...>, champsim::cache_builder_module_type_holder<Rs...>> b)
      : champsim::operable(b.m_clock_period), upper_levels(b.m_uls), lower_level(b.m_ll), lower_translate(b.m_lt), 
        NAME(b.m_name), NUM_SET(b.get_num_sets()), partition_cache(b.m_partition_cache), PQM_ENABLED(b.m_pqm_enabled), MQC_ENABLED(b.m_mqc_enabled), CC_ENABLED(b.m_cc_enabled),
        NUM_WAY(b.get_num_ways()), MSHR_SIZE(b.get_num_mshrs()), PQ_SIZE(b.m_pq_size), PQM_SIZE(b.get_num_mshrs()*2), MQ_SIZE(b.get_num_mshrs()*2), HIT_LATENCY(b.get_hit_latency() * b.m_clock_period),
        FILL_LATENCY(b.get_fill_latency() * b.m_clock_period), OFFSET_BITS(b.m_offset_bits), MAX_TAG(b.get_tag_bandwidth()), MAX_FILL(b.get_fill_bandwidth()),
        prefetch_as_load(b.m_pref_load), match_offset_bits(b.m_wq_full_addr), virtual_prefetch(b.m_va_pref), pref_activate_mask(b.m_pref_act_mask),
        pref_module_pimpl(std::make_unique<prefetcher_module_model<Ps...>>(this)), repl_module_pimpl(std::make_unique<replacement_module_model<Rs...>>(this))
  {
    if(MQC_ENABLED)
      MQ_SIZE /= NUM_CPUS;

    for(std::size_t core = 0; core < NUM_CPUS; core++) {
      MQ_CORE.push_back(core);
    }
      
    MQ = std::vector<std::deque<tag_lookup_type>>(NUM_CPUS);
    MQ_MISS_COUNTER = std::vector<uint64_t>(NUM_CPUS,0);
    PREFETCH_MISS_STORAGE.resize(PQM_SIZE);
    for(std::size_t loc = 0; loc < PQM_SIZE; loc++)
      PREFETCH_FREE_LIST.push_back(loc);
    PREFETCH_BANK_QUEUES.resize(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers());

    OUTGOING_BANK_REQUESTS = std::vector<int>(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers(),0);

    BANK_PREFETCH_THRESHOLD = b.m_pqm_thresh;
    BANK_DEMAND_THRESHOLD = b.m_mqc_thresh;
    assert(MSHR_SIZE > 4);
    if(PQM_ENABLED) {
      fmt::print("[{}] PQM Enabled, Banks: {}, Size: {}, Prefetch Limits: {}, {}, {}\n",NAME,PREFETCH_BANK_QUEUES.size(),PQM_SIZE,BANK_PREFETCH_THRESHOLD[0],BANK_PREFETCH_THRESHOLD[1],BANK_PREFETCH_THRESHOLD[2]);
    }
    if(MQC_ENABLED) {
      fmt::print("[{}] MQC Enabled, Cores: {}, Size: {}, Demand Limit: {}\n",NAME, NUM_CPUS, MQ_SIZE, BANK_DEMAND_THRESHOLD);
    }
    if(CC_ENABLED) {
      fmt::print("[{}] CC Enabled, Cores: {}\n", NAME, NUM_CPUS);
    }
    if(partition_cache) {
      assert((NUM_SET/NUM_CPUS) > 0);
      fmt::print("[{}] Cache Partitioned, Cores: {} Sets per core: {}\n", NAME, NUM_CPUS, NUM_SET/NUM_CPUS);
    }

    prefetches_in_mshr = std::vector<std::size_t>(NUM_CPUS,0);
    demands_in_mshr = std::vector<std::size_t>(NUM_CPUS,0);
    prefetch_limits = std::vector<std::size_t>(NUM_CPUS,1);
    prefetch_counter = std::vector<std::size_t>(NUM_CPUS,0);
    prefetch_hit_limit = std::vector<bool>(NUM_CPUS,false);
  }

  CACHE(const CACHE&) = delete;
  CACHE(CACHE&&);
  CACHE& operator=(const CACHE&) = delete;
  CACHE& operator=(CACHE&&);
};

template <typename... Ps>
void CACHE::prefetcher_module_model<Ps...>::impl_prefetcher_initialize()
{
  [[maybe_unused]] auto process_one = [&](auto& p) {
    using namespace champsim::modules;
    if constexpr (prefetcher::has_initialize<decltype(p)>)
      p.prefetcher_initialize();
  };

  std::apply([&](auto&... p) { (..., process_one(p)); }, intern_);
}

template <typename... Ps>
uint32_t CACHE::prefetcher_module_model<Ps...>::impl_prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, bool cache_hit,
                                                                              bool useful_prefetch, access_type type, uint32_t metadata_in, uint32_t metadata_hit)
{
  using return_type = uint32_t;
  [[maybe_unused]] auto process_one = [&](auto& p) {
    using namespace champsim::modules;
    /* Strong addresses */
    if constexpr (prefetcher::has_cache_operate<decltype(p), champsim::address, champsim::address, uint32_t, bool, bool, access_type, uint32_t, uint32_t>)
    return return_type{p.prefetcher_cache_operate(addr, ip, cpu, cache_hit, useful_prefetch, type, metadata_in, metadata_hit)};

    if constexpr (prefetcher::has_cache_operate<decltype(p), champsim::address, champsim::address, uint32_t, bool, bool, access_type, uint32_t>)
      return return_type{p.prefetcher_cache_operate(addr, ip, cpu, cache_hit, useful_prefetch, type, metadata_in)};

    /* Strong addresses, raw integer access type */
    if constexpr (prefetcher::has_cache_operate<decltype(p), champsim::address, champsim::address, uint32_t, bool, bool, std::underlying_type_t<access_type>, uint32_t>)
      return return_type{p.prefetcher_cache_operate(addr, ip, cpu, cache_hit, useful_prefetch, champsim::to_underlying(type), metadata_in)};

    /* Raw integer addresses, no useful_prefetch parameter, raw integer access type */
    if constexpr (prefetcher::has_cache_operate<decltype(p), uint64_t, uint64_t, uint32_t, bool, std::underlying_type_t<access_type>, uint32_t>)
      return return_type{p.prefetcher_cache_operate(addr.to<uint64_t>(), ip.to<uint64_t>(), cpu, cache_hit, champsim::to_underlying(type), metadata_in)};

    return return_type{};
  };

  return std::apply([&](auto&... p) { return (return_type{} ^ ... ^ process_one(p)); }, intern_);
}

template <typename... Ps>
uint32_t CACHE::prefetcher_module_model<Ps...>::impl_prefetcher_cache_fill(champsim::address addr, champsim::address ip, uint32_t cpu, bool useless, long set, long way, bool prefetch,
                                                                           champsim::address evicted_addr, uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict)
{
  using return_type = uint32_t;
  [[maybe_unused]] auto process_one = [&](auto& p) {
    using namespace champsim::modules;
    if constexpr (prefetcher::has_cache_fill<decltype(p), champsim::address, champsim::address, uint32_t, bool, long, long, bool, champsim::address, uint32_t, uint32_t, uint32_t>)
      return return_type{p.prefetcher_cache_fill(addr, ip, cpu, useless, set, way, prefetch, evicted_addr, metadata_in, metadata_evict, cpu_evict)};
    if constexpr (prefetcher::has_cache_fill<decltype(p), champsim::address, uint32_t, bool, long, long, bool, champsim::address, uint32_t, uint32_t, uint32_t>)
      return return_type{p.prefetcher_cache_fill(addr, cpu, useless, set, way, prefetch, evicted_addr, metadata_in, metadata_evict, cpu_evict)};
    if constexpr (prefetcher::has_cache_fill<decltype(p), champsim::address, uint32_t, bool, long, long, bool, champsim::address, uint32_t, uint32_t>)
      return return_type{p.prefetcher_cache_fill(addr, cpu, useless, set, way, prefetch, evicted_addr, metadata_in, metadata_evict)};
    if constexpr (prefetcher::has_cache_fill<decltype(p), champsim::address, uint32_t, bool, long, long, bool, champsim::address, uint32_t>)
      return return_type{p.prefetcher_cache_fill(addr, cpu, useless, set, way, prefetch, evicted_addr, metadata_in)};
    if constexpr (prefetcher::has_cache_fill<decltype(p), uint64_t, uint32_t, bool, long, long, bool, uint64_t, uint32_t>)
      return return_type{p.prefetcher_cache_fill(addr.to<uint64_t>(), cpu, useless, set, way, prefetch, evicted_addr.to<uint64_t>(), metadata_in)};
    return return_type{};
  };

  return std::apply([&](auto&... p) { return (return_type{} ^ ... ^ process_one(p)); }, intern_);
}

template <typename... Ps>
void CACHE::prefetcher_module_model<Ps...>::impl_prefetcher_cycle_operate()
{
  [[maybe_unused]] auto process_one = [&](auto& p) {
    using namespace champsim::modules;
    if constexpr (prefetcher::has_cycle_operate<decltype(p)>)
      p.prefetcher_cycle_operate();
  };

  std::apply([&](auto&... p) { (..., process_one(p)); }, intern_);
}

template <typename... Ps>
void CACHE::prefetcher_module_model<Ps...>::impl_prefetcher_final_stats()
{
  [[maybe_unused]] auto process_one = [&](auto& p) {
    using namespace champsim::modules;
    if constexpr (prefetcher::has_final_stats<decltype(p)>)
      p.prefetcher_final_stats();
  };

  std::apply([&](auto&... p) { (..., process_one(p)); }, intern_);
}

template <typename... Ps>
void CACHE::prefetcher_module_model<Ps...>::impl_prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target)
{
  [[maybe_unused]] auto process_one = [&](auto& p) {
    using namespace champsim::modules;
    if constexpr (prefetcher::has_branch_operate<decltype(p), champsim::address, uint8_t, champsim::address>)
      p.prefetcher_branch_operate(ip, branch_type, branch_target);
    if constexpr (prefetcher::has_branch_operate<decltype(p), uint64_t, uint8_t, uint64_t>)
      p.prefetcher_branch_operate(ip.to<uint64_t>(), branch_type, branch_target.to<uint64_t>());
  };

  std::apply([&](auto&... p) { (..., process_one(p)); }, intern_);
}

template <typename... Rs>
void CACHE::replacement_module_model<Rs...>::impl_initialize_replacement()
{
  [[maybe_unused]] auto process_one = [&](auto& r) {
    using namespace champsim::modules;
    if constexpr (replacement::has_initialize<decltype(r)>)
      r.initialize_replacement();
  };

  std::apply([&](auto&... r) { (..., process_one(r)); }, intern_);
}

template <typename... Rs>
long CACHE::replacement_module_model<Rs...>::impl_find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const BLOCK* current_set,
                                                              champsim::address ip, champsim::address full_addr, access_type type, bool prefetch)
{
  using return_type = long;
  [[maybe_unused]] auto process_one = [&](auto& r) {
    using namespace champsim::modules;

    if constexpr (replacement::has_find_victim<decltype(r), uint32_t, uint64_t, long, const BLOCK*, champsim::address, champsim::address, access_type, bool>)
      return return_type{r.find_victim(triggering_cpu, instr_id, set, current_set, ip, full_addr, type, prefetch)};
    /* Strong addresses */
    if constexpr (replacement::has_find_victim<decltype(r), uint32_t, uint64_t, long, const BLOCK*, champsim::address, champsim::address, access_type>)
      return return_type{r.find_victim(triggering_cpu, instr_id, set, current_set, ip, full_addr, type)};

    /* Raw integer addresses */
    if constexpr (replacement::has_find_victim<decltype(r), uint32_t, uint64_t, long, const BLOCK*, champsim::address, champsim::address,
                                               std::underlying_type_t<access_type>>)
      return return_type{r.find_victim(triggering_cpu, instr_id, set, current_set, ip, full_addr, champsim::to_underlying(type))};

    /* Raw integer addresses, raw integer access type */
    if constexpr (replacement::has_find_victim<decltype(r), uint32_t, uint64_t, long, const BLOCK*, uint64_t, uint64_t, std::underlying_type_t<access_type>>)
      return return_type{r.find_victim(triggering_cpu, instr_id, set, current_set, ip.to<uint64_t>(), full_addr.to<uint64_t>(), champsim::to_underlying(type))};

    return return_type{};
  };

  if constexpr (sizeof...(Rs) > 0) {
    return std::apply([&](auto&... r) { return (..., process_one(r)); }, intern_);
  }
  return return_type{};
}

template <typename... Rs>
void CACHE::replacement_module_model<Rs...>::impl_update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr,
                                                                           champsim::address ip, champsim::address victim_addr, access_type type, bool hit, bool prefetch)
{
  [[maybe_unused]] auto process_one = [&](auto& r) {
    using namespace champsim::modules;

    if (hit || replacement::has_cache_fill<decltype(r), uint32_t, long, long, champsim::address, champsim::address, champsim::address, access_type> || replacement::has_cache_fill<decltype(r), uint32_t, long, long, champsim::address, champsim::address, champsim::address, access_type, bool>) {
      auto new_victim_addr = hit ? champsim::address{} : victim_addr;
      
      if constexpr (replacement::has_update_state<decltype(r), uint32_t, long, long, champsim::address, champsim::address, champsim::address, access_type, bool, bool>)
        r.update_replacement_state(triggering_cpu, set, way, full_addr, ip, new_victim_addr, type, hit, prefetch);

      /* Strong addresses */
      else if constexpr (replacement::has_update_state<decltype(r), uint32_t, long, long, champsim::address, champsim::address, access_type, bool>)
        r.update_replacement_state(triggering_cpu, set, way, full_addr, ip, type, hit);

      /* Strong addresses */
      else if constexpr (replacement::has_update_state<decltype(r), uint32_t, long, long, champsim::address, champsim::address, champsim::address, access_type,
                                                       bool>)
        r.update_replacement_state(triggering_cpu, set, way, full_addr, ip, new_victim_addr, type, hit);

      /* Raw integer access type */
      else if constexpr (replacement::has_update_state<decltype(r), uint32_t, long, long, champsim::address, champsim::address, champsim::address,
                                                       std::underlying_type_t<access_type>, bool>)
        r.update_replacement_state(triggering_cpu, set, way, full_addr, ip, new_victim_addr, champsim::to_underlying(type), hit);

      /* Raw integer addresses, raw integer access type */
      else if constexpr (replacement::has_update_state<decltype(r), uint32_t, long, long, uint64_t, uint64_t, uint64_t, std::underlying_type_t<access_type>,
                                                       bool>)
        r.update_replacement_state(triggering_cpu, set, way, full_addr.to<uint64_t>(), ip.to<uint64_t>(), new_victim_addr.to<uint64_t>(),
                                   champsim::to_underlying(type), hit);
    }
  };

  std::apply([&](auto&... r) { (..., process_one(r)); }, intern_);
}

template <typename... Rs>
void CACHE::replacement_module_model<Rs...>::impl_replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr,
                                                                         champsim::address ip, champsim::address victim_addr, access_type type, bool prefetch)
{
  [[maybe_unused]] auto process_one = [&](auto& r) {
    using namespace champsim::modules;
    if constexpr (replacement::has_cache_fill<decltype(r), uint32_t, long, long, champsim::address, champsim::address, champsim::address, access_type, bool>)
      r.replacement_cache_fill(triggering_cpu, set, way, full_addr, ip, victim_addr, type, prefetch);
    /* Strong addresses */
    else if constexpr (replacement::has_cache_fill<decltype(r), uint32_t, long, long, champsim::address, champsim::address, champsim::address, access_type>)
      r.replacement_cache_fill(triggering_cpu, set, way, full_addr, ip, victim_addr, type);
    else {
      impl_update_replacement_state(triggering_cpu, set, way, full_addr, ip, victim_addr, type, false, prefetch);
    }
  };

  std::apply([&](auto&... r) { (..., process_one(r)); }, intern_);
}

template <typename... Rs>
void CACHE::replacement_module_model<Rs...>::impl_replacement_final_stats()
{
  [[maybe_unused]] auto process_one = [&](auto& r) {
    using namespace champsim::modules;
    if constexpr (replacement::has_final_stats<decltype(r)>)
      r.replacement_final_stats();
  };

  std::apply([&](auto&... r) { (..., process_one(r)); }, intern_);
}

#ifdef SET_ASIDE_CHAMPSIM_MODULE
#undef SET_ASIDE_CHAMPSIM_MODULE
#define CHAMPSIM_MODULE
#endif

#endif
