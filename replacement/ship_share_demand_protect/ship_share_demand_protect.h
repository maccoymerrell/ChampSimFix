#ifndef REPLACEMENT_SHIP_SHARE_DEMAND_PROTECT_H
#define REPLACEMENT_SHIP_SHARE_DEMAND_PROTECT_H

#include <array>
#include <vector>
#include <map>

#include "cache.h"
#include "modules.h"
#include "msl/bits.h"
#include "msl/fwcounter.h"

struct ship_share_demand_protect : public champsim::modules::replacement {
private:
  int& get_rrpv(long set, long way);

public:
  static constexpr int maxRRPV = 3;
  static constexpr std::size_t SHCT_SIZE = 16384;
  static constexpr unsigned SHCT_PRIME = 16381;
  static constexpr unsigned SHCT_MAX = 7;

  long SET_SAMPLE_RATE;

  // sampler structure
  class SAMPLER_class
  {
  public:
    bool valid = false;
    bool used = false;
    champsim::address address{};
    champsim::address ip{};
    uint64_t last_used = 0;
  };

  long NUM_SET, NUM_WAY;
  uint64_t access_count = 0;

  // sampler
  std::vector<SAMPLER_class> sampler;

  //per line
  std::vector<int> rrpv_values;
  std::vector<uint32_t> cpus;

  static constexpr std::size_t epoch = 8192;
  static constexpr std::size_t global_epoch = 262144;
  static constexpr bool CHANGE_RATE = true;
  static constexpr bool USE_MISS_RATE = true;

  uint64_t global_epoch_counter = 0;

  //params
  std::map<uint32_t,std::size_t> DNE_MAX;
  std::map<uint32_t,std::size_t> DNE_DEFAULT;
  std::map<uint32_t,std::size_t> DNE_MIN;

  //per core
  std::vector<int64_t> occupancy_counter;
  std::vector<uint64_t> hits;
  std::vector<uint64_t> misses;
  std::vector<uint64_t> epoch_counter;
  std::vector<uint64_t> do_not_evict;
  std::vector<double> hit_rate;
  std::vector<uint64_t> accesses;

  uint64_t altered_evictions = 0;
  uint64_t standard_evictions = 0;

  // prediction table structure
  std::vector<std::array<champsim::msl::fwcounter<champsim::msl::lg2(SHCT_MAX + 1)>, SHCT_SIZE>> SHCT;

  explicit ship_share_demand_protect(CACHE* cache);

  bool find_victim_called = false;
  bool replacement_cache_fill_called = false;
  bool update_replacement_state_called = false;

  long find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type, bool prefetch);
  void replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                              access_type type, bool prefetch);
  void update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                                access_type type, bool hit, bool prefetch);

  [[nodiscard]] constexpr bool is_sampled(long set) {
    auto mask = SET_SAMPLE_RATE - 1;
    auto shift = champsim::lg2(SET_SAMPLE_RATE);
    auto low_slice = set & mask;
    auto high_slice = (set >> shift) & mask;
    return high_slice == low_slice;
  }

  // use this function to print out your own stats at the end of simulation
  void replacement_final_stats();
};

#endif
