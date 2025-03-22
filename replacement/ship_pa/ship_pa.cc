#include "ship_pa.h"

#include <algorithm>
#include <cassert>
#include <random>

#include "champsim.h"

// initialize replacement state
ship_pa::ship_pa(CACHE* cache)
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
  fmt::print("Using SHiP in {}\n",intern_->NAME);
}

int& ship_pa::get_rrpv(long set, long way) { return rrpv_values.at(static_cast<std::size_t>(set * NUM_WAY + way)); }

// find replacement victim
long ship_pa::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                       champsim::address full_addr, access_type type)
{
  // look for the maxRRPV line
  auto begin = std::next(std::begin(rrpv_values), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);

  auto victim = std::max_element(begin, end);
  if (auto rrpv_update = maxRRPV - *victim; rrpv_update != 0)
    for (auto it = begin; it != end; ++it)
      *it += rrpv_update;

  assert(begin <= victim);
  assert(victim < end);
  return std::distance(begin, victim);
}

// called on every cache hit and cache fill
void ship_pa::update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                    champsim::address victim_addr, access_type type, uint8_t hit)
{
  using namespace champsim::data::data_literals;

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

  if(hit && type != access_type::PREFETCH && type != access_type::PROMOTION)
    get_rrpv(set, way) = 0;
  else if(hit && type == access_type::PREFETCH)
    get_rrpv(set, way) = 1;
}

void ship_pa::replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr, access_type type)
{
  // handle writeback access
  if (access_type{type} == access_type::WRITE) {
    get_rrpv(set, way) = maxRRPV - 1;
    return;
  }

  using namespace champsim::data::data_literals;
  // SHIP prediction
  auto SHCT_idx = ip.slice_lower<32_b>().to<std::size_t>() % SHCT_PRIME;

  get_rrpv(set, way) = maxRRPV - 1;
  if (type == access_type::PREFETCH)
    get_rrpv(set, way) = maxRRPV - 3;
  else if (SHCT[triggering_cpu][SHCT_idx].is_max())
    get_rrpv(set, way) = maxRRPV;
}
