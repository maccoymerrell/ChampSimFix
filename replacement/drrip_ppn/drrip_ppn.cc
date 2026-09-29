#include "drrip_ppn.h"

#include <algorithm>
#include <cassert>
#include <fmt/core.h>

#include "champsim.h"

champsim::modules::replacement::register_module<drrip_ppn> drrip_ppn_register("drrip_ppn");

drrip_ppn::drrip_ppn(champsim::modules::ModuleBuilder builder)
    : NUM_SET(builder.get_parent<champsim::modules::cache_module>()->num_sets()), NUM_WAY(builder.get_parent<champsim::modules::cache_module>()->num_ways()),
      num_cpus(builder.get_parameter<std::size_t>("num_consumers", true, std::size_t{1})),
      insert_over_quota(builder.get_parameter<bool>("ppn_insert", true, true)),
      no_prefetch_promote(builder.get_parameter<bool>("ppn_no_pf_promote", true, true)),
      victim_by_quota(builder.get_parameter<bool>("ppn_victim", true, true)),
      epoch_fills(builder.get_parameter<uint64_t>("ppn_epoch", true, uint64_t{65536})), slack(builder.get_parameter<double>("ppn_slack", true, 1.0)),
      rrpv(static_cast<std::size_t>(NUM_SET * NUM_WAY), maxRRPV), owner(static_cast<std::size_t>(NUM_SET * NUM_WAY), -1),
      PSEL(num_cpus, champsim::msl::dscounter<long, PSEL_WIDTH>(champsim::msl::get_sample_rate(NUM_SET))), occupancy(num_cpus, 0),
      share(num_cpus, static_cast<uint64_t>(NUM_SET * NUM_WAY) / std::max<std::size_t>(num_cpus, 1)), demand_fills(num_cpus, 0), prefetch_fills(num_cpus, 0),
      demand_ema(num_cpus, 0), stat_demand_fills(num_cpus, 0), stat_prefetch_fills(num_cpus, 0), stat_pf_insert_low(num_cpus, 0), stat_victim_moved(num_cpus, 0)
{
}

void drrip_ppn::initialize_replacement() {}

// Shares follow each core's demand fills (smoothed over epochs): the cache a core would claim without prefetching.
void drrip_ppn::end_epoch()
{
  uint64_t total = 0;
  for (std::size_t c = 0; c < num_cpus; ++c) {
    demand_ema[c] = (demand_ema[c] + demand_fills[c]) / 2;
    total += demand_ema[c];
    demand_fills[c] = prefetch_fills[c] = 0;
  }
  const auto lines = static_cast<uint64_t>(NUM_SET * NUM_WAY);
  for (std::size_t c = 0; c < num_cpus; ++c)
    share[c] = total ? lines * demand_ema[c] / total : lines / num_cpus;
  fills_in_epoch = 0;
}

void drrip_ppn::update_replacement_state(champsim::origin origin, long set, long way, champsim::address, champsim::address, champsim::address,
                                         access_type type, bool hit)
{
  if (!hit)
    return;
  auto& r = rrpv.at(idx(set, way));
  if (type == access_type::WRITE)
    r = maxRRPV - 1;
  else if (no_prefetch_promote && type == access_type::PREFETCH)
    r = std::min(r, maxRRPV - 1); // a prefetch re-reference refreshes the line but never pins it
  else
    r = 0;
}

void drrip_ppn::replacement_cache_fill(champsim::origin origin, long set, long way, champsim::address, champsim::address, champsim::address,
                                       access_type type)
{
  const auto cpu = std::min<std::size_t>(origin.cpu(), num_cpus - 1);
  const auto i = idx(set, way);
  if (owner[i] >= 0)
    --occupancy[static_cast<std::size_t>(owner[i])];
  owner[i] = static_cast<int>(cpu);
  ++occupancy[cpu];

  if (type == access_type::WRITE) {
    rrpv[i] = maxRRPV - 1;
    return;
  }

  const bool is_prefetch = (type == access_type::PREFETCH);
  ++(is_prefetch ? prefetch_fills : demand_fills)[cpu];
  ++(is_prefetch ? stat_prefetch_fills : stat_demand_fills)[cpu];
  if (++fills_in_epoch >= epoch_fills)
    end_epoch();

  if (insert_over_quota && is_prefetch && over_share(cpu)) {
    rrpv[i] = maxRRPV;
    ++stat_pf_insert_low[cpu];
  } else if (PSEL[cpu].decide(set)) {
    rrpv[i] = maxRRPV;
    if (++brrip_counter == BRRIP_MAX) {
      brrip_counter = 0;
      rrpv[i] = maxRRPV - 1;
    }
  } else {
    rrpv[i] = maxRRPV - 1;
  }
  PSEL[cpu].update_bad(set);
}

long drrip_ppn::find_victim(champsim::origin origin, uint64_t, long set, const champsim::cache_block*, champsim::address, champsim::address, access_type)
{
  auto begin = std::next(std::begin(rrpv), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);
  auto victim = std::max_element(begin, end);
  if (auto age = maxRRPV - *victim; age != 0)
    for (auto it = begin; it != end; ++it)
      *it += age;

  if (victim_by_quota) {
    // Among the lines DRRIP would evict, take the one whose owner is furthest over its share.
    double worst = -1.0;
    auto pick = victim;
    for (auto it = begin; it != end; ++it) {
      if (*it != maxRRPV)
        continue;
      const int o = owner[idx(set, std::distance(begin, it))];
      const double over = (o < 0) ? 1e9 : static_cast<double>(occupancy[static_cast<std::size_t>(o)]) / std::max(1.0, static_cast<double>(share[static_cast<std::size_t>(o)]));
      if (over > worst) {
        worst = over;
        pick = it;
      }
    }
    if (pick != victim)
      ++stat_victim_moved[std::min<std::size_t>(origin.cpu(), num_cpus - 1)];
    victim = pick;
  }
  assert(begin <= victim && victim < end);
  return std::distance(begin, victim);
}

void drrip_ppn::replacement_final_stats()
{
  for (std::size_t c = 0; c < num_cpus; ++c)
    fmt::print("[drrip_ppn] cpu{} demand_fills={} prefetch_fills={} occupancy={} share={} pf_inserted_low={} victims_moved={}\n", c, stat_demand_fills[c],
               stat_prefetch_fills[c], occupancy[c], share[c], stat_pf_insert_low[c], stat_victim_moved[c]);
}
