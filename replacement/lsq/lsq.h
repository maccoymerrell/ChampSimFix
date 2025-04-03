#ifndef REPLACEMENT_LSQ_H
#define REPLACEMENT_LSQ_H

#include <vector>
#include <array>

#include "cache.h"
#include "modules.h"

class lsq : public champsim::modules::replacement
{
  long NUM_WAY;
  std::vector<uint64_t> last_used_cycles;
  std::vector<uint8_t> lsq_scores;
  uint64_t cycle = 0;

  std::array<uint8_t,5> lsq_bins = {2,4,8,16,32};
  std::array<uint8_t,5> lsq_place = {32,16,8,4,2};

  uint8_t avg_score = 0;

  static constexpr int rollover_max = 24;
  uint8_t rollover_counter = 0;

  uint64_t bypassed_fills = 0;
  uint64_t fills = 0;
  uint64_t evictions = 0;
  uint64_t hits = 0;

  uint64_t score_at_bypass = 0;
  uint64_t score_at_eviction = 0;
  uint64_t score_at_fill = 0;
  uint64_t score_at_hit = 0;

public:
  explicit lsq(CACHE* cache);
  lsq(CACHE* cache, long sets, long ways);

  // void initialize_replacement();
  uint8_t get_lsq_placement(uint8_t lsq_score);
  
  long find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type);
  void replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                              access_type type);
  void update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                                access_type type, uint8_t hit);
  void replacement_final_stats();
};

#endif
