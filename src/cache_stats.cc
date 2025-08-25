#include "cache_stats.h"

cache_stats operator-(cache_stats lhs, cache_stats rhs)
{
  cache_stats result;
  result.pf_requested = lhs.pf_requested - rhs.pf_requested;
  result.pf_issued = lhs.pf_issued - rhs.pf_issued;
  result.pf_useful = lhs.pf_useful - rhs.pf_useful;
  result.pf_useless = lhs.pf_useless - rhs.pf_useless;
  result.pf_fill = lhs.pf_fill - rhs.pf_fill;
  result.pr_missed = lhs.pr_missed - rhs.pr_missed;

  result.total_pq_occupancy_cycles = lhs.total_pq_occupancy_cycles - rhs.total_pq_occupancy_cycles;
  result.total_rq_occupancy_cycles = lhs.total_rq_occupancy_cycles - rhs.total_rq_occupancy_cycles;
  result.total_wq_occupancy_cycles = lhs.total_wq_occupancy_cycles - rhs.total_wq_occupancy_cycles;
  result.total_mq_occupancy_cycles = lhs.total_mq_occupancy_cycles - rhs.total_mq_occupancy_cycles;
  result.total_internal_pq_occupancy_cycles = lhs.total_internal_pq_occupancy_cycles - rhs.total_internal_pq_occupancy_cycles;
  result.total_tag_check_occupancy_cycles = lhs.total_tag_check_occupancy_cycles - rhs.total_tag_check_occupancy_cycles;
  result.total_mshr_occupancy_cycles = lhs.total_mshr_occupancy_cycles - rhs.total_mshr_occupancy_cycles;
  result.total_inflight_writes_occupancy_cycles = lhs.total_inflight_writes_occupancy_cycles - rhs.total_inflight_writes_occupancy_cycles;

  result.cycle_denominator = lhs.cycle_denominator - rhs.cycle_denominator;

  result.hits = lhs.hits - rhs.hits;
  result.misses = lhs.misses - rhs.misses;

  result.total_miss_latency_cycles = lhs.total_miss_latency_cycles - rhs.total_miss_latency_cycles;
  return result;
}
