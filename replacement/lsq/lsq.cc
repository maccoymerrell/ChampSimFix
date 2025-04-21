#include "lsq.h"

#include <algorithm>
#include <cassert>

lsq::lsq(CACHE* cache) : lsq(cache, cache->NUM_SET, cache->NUM_WAY) {
  fmt::print("Initialized LSQ replacement for cache: {}\n",intern_->NAME);
}

lsq::lsq(CACHE* cache, long sets, long ways) : replacement(cache), NUM_WAY(ways), last_used_cycles(static_cast<std::size_t>(sets * ways), 0), lsq_scores(static_cast<std::size_t>(sets * ways), 255) {}

long lsq::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                      champsim::address full_addr, access_type type)
{
  
  auto begin = std::next(std::begin(lsq_scores), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);
  std::size_t victim = type == access_type::WRITE ? std::distance(begin,std::max_element(begin,end)) : 0;
  uint8_t lsq_score = type == access_type::WRITE ? lsq_scores.at(set*NUM_WAY + victim) : intern_->get_mshr_occupancy();//intern_->lsq_score;
  uint64_t cycle_score = type == access_type::WRITE ? last_used_cycles.at(set*NUM_WAY + victim) : cycle + 1;
  bool found = type == access_type::WRITE;

  bool was_ranked_by_lru = false;
  for(auto it = set*NUM_WAY; it < (set + 1)*NUM_WAY; it++) {
    //higher lsq score is always victim
    if(lsq_scores.at(it) > lsq_score) {
      victim = it - (set*NUM_WAY);
      lsq_score = lsq_scores.at(it);
      cycle_score = last_used_cycles.at(it);
      found = true;
    }
    //break ties
    else if(lsq_scores.at(it) == lsq_score && cycle_score > last_used_cycles.at(it)) {
      victim = it - (set*NUM_WAY);
      lsq_score = lsq_scores.at(it);
      cycle_score = last_used_cycles.at(it);
      found = true;
      was_ranked_by_lru = true;
    }
  }
  //couldn't find a victim, bypass
  if(!found) {
    //fmt::print("Bypassing filling {} lsq_score is {}\n",full_addr,lsq_score);
    if(!intern_->warmup) {
      bypassed_fills++;
      score_at_bypass += lsq_score;
    }
    return (NUM_WAY);
  }
  if(!intern_->warmup && lsq_score != 255) {
    evictions++;
    score_at_eviction += lsq_score;
  }

  if(!intern_->warmup) {
    if(was_ranked_by_lru)
      ranked_by_lru++;
    else
      ranked_by_lsq++;
  }
  
  //fmt::print("Evicting way {} lsq_score was {} ours was {}\n",victim,lsq_score,intern_->lsq_score);
  return victim;
}

void lsq::replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                                 access_type type)
{
  if(way == NUM_WAY)
    return;
  // Mark the way as being used on the current cycle
  if(!intern_->warmup) {
    fills++;
    score_at_fill += intern_->get_mshr_occupancy();//intern_->lsq_score;
  }
  last_used_cycles.at((std::size_t)(set * NUM_WAY + way)) = cycle++;
  lsq_scores.at((std::size_t)(set * NUM_WAY + way)) = intern_->get_mshr_occupancy();//intern_->lsq_score;

}

void lsq::update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                   champsim::address victim_addr, access_type type, uint8_t hit)
{
  // Mark the way as being used on the current cycle
  if (hit && access_type{type} != access_type::WRITE) { // Skip this for writeback hits
    last_used_cycles.at((std::size_t)(set * NUM_WAY + way)) = cycle++;

    auto begin = std::next(std::begin(lsq_scores), set * NUM_WAY);
    auto end = std::next(begin, NUM_WAY);
    //for(auto it = begin; it != end; it++) {
    //  *it = *it >= avg_score ? *it : *it + 1; 
    //}
    lsq_scores.at((std::size_t)(set * NUM_WAY + way)) = intern_->get_mshr_occupancy();
    if(!intern_->warmup) {
      hits++;
      score_at_hit += lsq_scores.at((std::size_t)(set * NUM_WAY + way));
    }
  }
  rollover_counter += get_rollover_rate(intern_->get_mshr_occupancy());
  if(rollover_counter >= rollover_max) {
    auto begin = std::next(std::begin(lsq_scores), set * NUM_WAY);
    auto end = std::next(begin, NUM_WAY);
    for(auto it = begin; it != end; it++) {
      *it = *it >= 254 ? *it : *it + 1;
    }
    rollover_counter = 0;
  }
}

uint8_t lsq::get_lsq_placement(uint8_t lsq_score) {
  int position = 0;
  for(auto entry : lsq_bins) {
    position++;
    if(lsq_score < entry)
      break;
  }
  return lsq_place.at(position-1);
}

uint8_t lsq::get_rollover_rate(uint8_t lsq_score) {
  int position = 0;
  for(auto entry : lsq_bins) {
    position++;
    if(lsq_score < entry)
      break;
  }
  return lsq_rollover_rate.at(position-1);
}

void lsq::replacement_final_stats() {
  fmt::print("LSQ Replacement Stats\n");
  if(fills != 0)
    fmt::print("\tScore at Fill: {}\n",score_at_fill / (float)fills);
  if(evictions != 0)
    fmt::print("\tScore at Eviction: {}\n",score_at_eviction / (float)evictions);
  if(hits != 0)
    fmt::print("\tScore at Hit: {}\n",score_at_hit / (float)hits);
  if(bypassed_fills != 0)
    fmt::print("\tScore at Bypass: {}\n", score_at_bypass / (float)bypassed_fills);
  
  fmt::print("\tRanked by LSQ: {} LRU: {}\n",ranked_by_lsq,ranked_by_lru);
}
