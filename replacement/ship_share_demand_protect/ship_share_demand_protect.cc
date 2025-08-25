#include "ship_share_demand_protect.h"

#include <algorithm>
#include <cassert>
#include <random>

#include "champsim.h"

// initialize replacement state
ship_share_demand_protect::ship_share_demand_protect(CACHE* cache)
    : replacement(cache), NUM_SET(cache->NUM_SET), NUM_WAY(cache->NUM_WAY),
      rrpv_values(static_cast<std::size_t>(NUM_SET * NUM_WAY), maxRRPV)
{

  // Determine set sampling rate
  if(NUM_SET >= 1024) { // 1 in 32
    SET_SAMPLE_RATE = 32;
  } else if(NUM_SET >= 256) { // 1 in 16
      SET_SAMPLE_RATE = 16;
  } else if(NUM_SET >= 64) { // 1 in 8
      SET_SAMPLE_RATE = 8;
  } else if(NUM_SET >= 8) { // 1 in 4
      SET_SAMPLE_RATE = 4;
  } else {
      assert(false); // Not enough sets to sample for set dueling
  }
  assert(NUM_SET >= SET_SAMPLE_RATE); // Guarantee at least one sampled set

  sampler.resize(NUM_SET / SET_SAMPLE_RATE * NUM_CPUS * static_cast<std::size_t>(NUM_WAY));

  // randomly selected sampler sets
  std::generate_n(std::back_inserter(SHCT), NUM_CPUS, []() -> typename decltype(SHCT)::value_type { return {}; });
  fmt::print("Using SHiP Share DP in {}\n",intern_->NAME);
  champsim::data::bytes cache_size{NUM_SET*NUM_WAY*BLOCK_SIZE};
  fmt::print("\tSIZE: {} SETS: {} WAYS: {} SET SAMPLE RATE: {}\n", champsim::data::mebibytes{cache_size},NUM_SET,NUM_WAY,SET_SAMPLE_RATE);

  uint64_t total_lines = NUM_SET * NUM_WAY;

 
    DNE_DEFAULT = std::map<uint32_t,std::size_t>{{1,total_lines},{2,total_lines/4},{4,total_lines/8}, {8,total_lines/16}};
    assert(DNE_DEFAULT.find(NUM_CPUS) != std::end(DNE_DEFAULT));


    DNE_MAX = std::map<uint32_t,std::size_t>{{1,total_lines},{2,(total_lines/2)},{4,(total_lines/4)}, {8,(total_lines/8)}};
    assert(DNE_MAX.find(NUM_CPUS) != std::end(DNE_MAX));
    DNE_MIN = std::map<uint32_t,std::size_t>{{1,total_lines/8},{2,total_lines/16},{4,total_lines/32}, {8,total_lines/64}};
    assert(DNE_MIN.find(NUM_CPUS) != std::end(DNE_MIN));

  
  occupancy_counter = std::vector<int64_t>(NUM_CPUS,0);
  hits = std::vector<uint64_t>(NUM_CPUS,0);
  misses = std::vector<uint64_t>(NUM_CPUS,0);
  epoch_counter = std::vector<uint64_t>(NUM_CPUS,0);
  do_not_evict = std::vector<uint64_t>(NUM_CPUS,DNE_DEFAULT[NUM_CPUS]);
  hit_rate = std::vector<double>(NUM_CPUS,1.0);
  accesses = std::vector<uint64_t>(NUM_CPUS,0);

  cpus = std::vector<uint32_t>(NUM_SET*NUM_WAY,NUM_CPUS);

  assert(DNE_MAX[NUM_CPUS] != 0);
  assert(DNE_DEFAULT[NUM_CPUS] != 0);
  assert(DNE_MIN[NUM_CPUS] != 0);

  fmt::print("\tDO NOT EVICT MAX: {} DEFAULT: {} MIN: {}\n",DNE_MAX[NUM_CPUS],DNE_DEFAULT[NUM_CPUS],DNE_MIN[NUM_CPUS]);
}

int& ship_share_demand_protect::get_rrpv(long set, long way) { return rrpv_values.at(static_cast<std::size_t>(set * NUM_WAY + way)); }

// find replacement victim
long ship_share_demand_protect::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                       champsim::address full_addr, access_type type, bool prefetch)
{
  find_victim_called = true;
  // look for the maxRRPV line
  auto begin = std::next(std::begin(rrpv_values), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);

  int victim = 0;
  int fallback_victim = 0;
  int max_found_rrpv = -1;
  int max_found_rrpv_fallback = -1;
  bool not_found = true;
  bool not_found_fallback = true;
  for(int i = set*NUM_WAY; i < (set+1)*NUM_WAY; i++) {
    //standard rrpv for fallback  
    if(rrpv_values[i] > max_found_rrpv_fallback) {
      fallback_victim = i;
      max_found_rrpv_fallback = rrpv_values[i];
      not_found_fallback = false;
    }
    //found empty way
    if(cpus[i] == NUM_CPUS){
	    victim = i;
      fallback_victim = i;
	    max_found_rrpv = rrpv_values[i];
      max_found_rrpv_fallback = rrpv_values[i];
	    not_found = false;
      not_found_fallback = false;
	    break;
    }
    //way not empty, attempt to find best way while not evicting protected cpus
    else if(rrpv_values[i] > max_found_rrpv && (cpus[i] == triggering_cpu || occupancy_counter[cpus[i]] > do_not_evict[cpus[i]])) {
      victim = i;
      max_found_rrpv = rrpv_values[i];
      not_found = false;
    }
  }

  assert(!not_found_fallback);

  if(not_found) {
    if(!intern_->warmup)
      standard_evictions++;
    victim = fallback_victim;
    max_found_rrpv = max_found_rrpv_fallback;
  } else {
    if(!intern_->warmup)
      altered_evictions++;
  }

  assert(victim >= set*NUM_WAY);
  assert(victim < (set + 1)*NUM_WAY);
  if(cpus[victim] != NUM_CPUS) {
      occupancy_counter[cpus[victim]]--;
  }
  assert(occupancy_counter[cpus[victim]] >= 0);
  victim = victim - (set*NUM_WAY);

  if (auto rrpv_update = maxRRPV - max_found_rrpv; rrpv_update != 0)
    for (auto it = begin; it != end; ++it)
      *it += rrpv_update;

  return victim;
}

// called on every cache hit and cache fill
void ship_share_demand_protect::update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                    champsim::address victim_addr, access_type type, bool hit, bool prefetch)
{
  using namespace champsim::data::data_literals;
  update_replacement_state_called = true;
  //hit rate tracking
  if(hit)
    hits[triggering_cpu]++;
  else
    misses[triggering_cpu]++;
  epoch_counter[triggering_cpu]++;
  global_epoch_counter++;

  // update sampler
  if (is_sampled(set)) {
    auto s_idx = set / SET_SAMPLE_RATE;
    auto s_set_begin = std::next(std::begin(sampler), s_idx * NUM_WAY + (NUM_SET / SET_SAMPLE_RATE) * NUM_WAY * triggering_cpu);
    auto s_set_end = std::next(s_set_begin, NUM_WAY);

    // check hit
    auto match = std::find_if(s_set_begin, s_set_end, [addr = full_addr, shamt = champsim::data::bits{champsim::lg2(NUM_SET / SET_SAMPLE_RATE) + champsim::lg2(NUM_WAY)}](auto x) {
      return x.valid && x.address.slice_upper(shamt) == addr.slice_upper(shamt);
    });
    if (match != s_set_end) {
      auto SHCT_idx = match->ip.slice_lower<32_b>().to<std::size_t>() % SHCT_PRIME;
      SHCT[triggering_cpu][SHCT_idx] -= 1;

      match->used = true;
    } else {
      match = std::min_element(s_set_begin, s_set_end, [](auto x, auto y) { return x.last_used < y.last_used; });

      if (!match->used) {
        auto SHCT_idx = match->ip.slice_lower<32_b>().to<std::size_t>() % SHCT_PRIME;
        SHCT[triggering_cpu][SHCT_idx] += 1;
      }

      match->valid = true;
      match->address = full_addr;
      match->ip = ip;
      match->used = false;
    }

    // update LRU state
    match->last_used = access_count++;
  }

  if(hit) {
    get_rrpv(set, way) = 0;
  }



  //do epoch stuff here
  if(epoch_counter[triggering_cpu] >= epoch) {
    hit_rate[triggering_cpu] = hits[triggering_cpu] / (double)(misses[triggering_cpu] + hits[triggering_cpu] + 1);
    accesses[triggering_cpu] += hits[triggering_cpu] + misses[triggering_cpu];
    epoch_counter[triggering_cpu] = 0;
    hits[triggering_cpu] = 0;
    misses[triggering_cpu] = 0;

    //set do not fill according to proportion of total accesses out of the last X cycles
    //set do not evict according to current usefulness

  }
  if(global_epoch_counter >= global_epoch) {
    global_epoch_counter = 0;

    //finish summing counter
    uint64_t total_accesses = 0;
    uint64_t total_accesses_hit_weighted = 0;
    for(int i = 0; i < NUM_CPUS; i++) {
      accesses[i] += hits[i] + misses[i];
      total_accesses += accesses[i];
      total_accesses_hit_weighted += USE_MISS_RATE ? accesses[i]*(1.0-hit_rate[i]) :
                                                     accesses[i]*hit_rate[i];
    }

    //now, we need to divide up the cache according to the proportion of accesses each got
    uint64_t cache_size = NUM_WAY * NUM_SET;
    uint64_t range = DNE_MAX[NUM_CPUS] - DNE_MIN[NUM_CPUS];
    //now we need to divide up the cache by also modulating by hit rate
    for(int i = 0; i < NUM_CPUS; i++) {
      std::size_t prop = USE_MISS_RATE ? (range*(1.0-hit_rate[i])) :  
                                         (range*hit_rate[i]);
      prop += DNE_MIN[NUM_CPUS];
      if(CHANGE_RATE)
        do_not_evict[i] = prop;
    }
    //clear access counters
    for(int i = 0; i < NUM_CPUS; i++) {
      accesses[i] = 0;
    }
  }
}

void ship_share_demand_protect::replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr, access_type type, bool prefetch)
{
  replacement_cache_fill_called = true;
  if(way == NUM_WAY)
    return;	  
  
  occupancy_counter[triggering_cpu]++;

  assert(occupancy_counter[triggering_cpu] <= NUM_SET*NUM_WAY);
  cpus[(set*NUM_WAY) + way] = triggering_cpu;
  // handle writeback access
  if (access_type{type} == access_type::WRITE) {
    get_rrpv(set, way) = maxRRPV - 1;
    return;
  }

  using namespace champsim::data::data_literals;
  // SHIP prediction
  auto SHCT_idx = ip.slice_lower<32_b>().to<std::size_t>() % SHCT_PRIME;

  get_rrpv(set, way) = maxRRPV - 1;
  if (SHCT[triggering_cpu][SHCT_idx].is_max())
    get_rrpv(set, way) = maxRRPV;
}

void ship_share_demand_protect::replacement_final_stats() {
  fmt::print("[{}] SHiP Share DP Final Stats: cache_fill: {}, update_state: {}, find_victim: {}",intern_->NAME,replacement_cache_fill_called,update_replacement_state_called,find_victim_called);
  fmt::print("\tAltered Evictions: {} Standard Evictions: {}\n",altered_evictions,standard_evictions);
  fmt::print("\tFinal Core Occupancies:\n");
  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("\t\t{} - Occupancy: {} Hit Rate: {} DNE: {}\n",i,occupancy_counter[i],hit_rate[i],do_not_evict[i]);
  }
}
