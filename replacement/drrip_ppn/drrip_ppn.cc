#include "drrip_ppn.h"

#include <algorithm>
#include <cassert>
#include <fmt/core.h>

#include "champsim.h"

champsim::modules::replacement::register_module<drrip_ppn> drrip_ppn_register("drrip_ppn");

namespace
{
long sample_stride_for(long num_set, uint64_t sample_sets)
{
  return (sample_sets == 0 || sample_sets >= static_cast<uint64_t>(num_set)) ? 1 : num_set / static_cast<long>(sample_sets);
}

uint64_t sampled_set_count(long num_set, long stride)
{
  uint64_t n = 0;
  for (long s = stride / 2; s < num_set; s += stride)
    ++n;
  return n;
}

uint64_t bits_for(uint64_t n)
{
  uint64_t b = 0;
  while ((uint64_t{1} << b) < n)
    ++b;
  return b;
}
} // namespace

drrip_ppn::drrip_ppn(champsim::modules::ModuleBuilder builder)
    : NUM_SET(builder.get_parent<champsim::modules::cache_module>()->num_sets()), NUM_WAY(builder.get_parent<champsim::modules::cache_module>()->num_ways()),
      num_cpus(builder.get_parameter<std::size_t>("num_consumers", true, std::size_t{1})),
      insert_over_quota(builder.get_parameter<bool>("ppn_insert", true, true)),
      no_prefetch_promote(builder.get_parameter<bool>("ppn_no_pf_promote", true, true)),
      epoch_fills(builder.get_parameter<uint64_t>("ppn_epoch", true, uint64_t{65536})), slack(builder.get_parameter<double>("ppn_slack", true, 1.0)),
      sample_stride(sample_stride_for(NUM_SET, builder.get_parameter<uint64_t>("ppn_sample_sets", true, uint64_t{64}))),
      sampled_lines(sampled_set_count(NUM_SET, sample_stride) * static_cast<uint64_t>(NUM_WAY)), rrpv(static_cast<std::size_t>(NUM_SET * NUM_WAY), maxRRPV),
      owner(sampled_lines, -1), PSEL(num_cpus, champsim::msl::dscounter<long, PSEL_WIDTH>(champsim::msl::get_sample_rate(NUM_SET))), occupancy(num_cpus, 0),
      share(num_cpus, sampled_lines / std::max<std::size_t>(num_cpus, 1)), demand_fills(num_cpus, 0), demand_ema(num_cpus, 0), stat_demand_fills(num_cpus, 0),
      stat_prefetch_fills(num_cpus, 0), stat_pf_insert_low(num_cpus, 0)
{
}

void drrip_ppn::initialize_replacement()
{
  // Owner ids in the sampled sets, plus per-core occupancy, share, epoch demand fills and their average, plus the epoch counter.
  const uint64_t owner_bits = sampled_lines * bits_for(num_cpus);
  const uint64_t counter_bits = num_cpus * 2 * (bits_for(sampled_lines + 1) + bits_for(epoch_fills + 1)) + bits_for(epoch_fills + 1);
  fmt::print("[drrip_ppn] occupancy sampled in {} of {} sets: {} bytes of tracking state\n", sampled_lines / static_cast<uint64_t>(NUM_WAY), NUM_SET,
             (owner_bits + counter_bits + 7) / 8);
}

// Shares follow each core's demand fills (smoothed over epochs): the cache a core would claim without prefetching.
void drrip_ppn::end_epoch()
{
  uint64_t total = 0;
  for (std::size_t c = 0; c < num_cpus; ++c) {
    demand_ema[c] = (demand_ema[c] + demand_fills[c]) / 2;
    total += demand_ema[c];
    demand_fills[c] = 0;
  }
  for (std::size_t c = 0; c < num_cpus; ++c)
    share[c] = total ? sampled_lines * demand_ema[c] / total : sampled_lines / num_cpus;
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
  if (sampled(set)) {
    auto& o = owner[sample_idx(set, way)];
    if (o >= 0)
      --occupancy[static_cast<std::size_t>(o)];
    o = static_cast<int>(cpu);
    ++occupancy[cpu];
  }

  if (type == access_type::WRITE) {
    rrpv[i] = maxRRPV - 1;
    return;
  }

  const bool is_prefetch = (type == access_type::PREFETCH);
  if (!is_prefetch)
    ++demand_fills[cpu];
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

long drrip_ppn::find_victim(champsim::origin, uint64_t, long set, const champsim::cache_block*, champsim::address, champsim::address, access_type)
{
  auto begin = std::next(std::begin(rrpv), set * NUM_WAY);
  auto end = std::next(begin, NUM_WAY);
  auto victim = std::max_element(begin, end);
  if (auto age = maxRRPV - *victim; age != 0)
    for (auto it = begin; it != end; ++it)
      *it += age;
  assert(begin <= victim && victim < end);
  return std::distance(begin, victim);
}

void drrip_ppn::replacement_final_stats()
{
  for (std::size_t c = 0; c < num_cpus; ++c)
    fmt::print("[drrip_ppn] cpu{} demand_fills={} prefetch_fills={} sampled_occupancy={} sampled_share={} pf_inserted_low={}\n", c, stat_demand_fills[c],
               stat_prefetch_fills[c], occupancy[c], share[c], stat_pf_insert_low[c]);
}
