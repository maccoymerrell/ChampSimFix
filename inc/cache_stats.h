#ifndef CACHE_STATS_H
#define CACHE_STATS_H

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "channel.h"
#include "event_counter.h"

struct cache_stats {
  std::string name;
  // prefetch stats
  uint64_t pf_requested = 0;
  uint64_t pf_issued = 0;
  uint64_t pf_useful = 0;
  uint64_t pf_useless = 0;
  uint64_t pf_fill = 0;

  uint64_t pr_missed = 0;

  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> hits = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> misses = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> downstream_packets = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> returned_packets = {};
  champsim::stats::event_counter<std::remove_cv_t<decltype(NUM_CPUS)>> pf_useful_core = {};
  champsim::stats::event_counter<std::remove_cv_t<decltype(NUM_CPUS)>> pf_fill_core = {};
  champsim::stats::event_counter<std::remove_cv_t<decltype(NUM_CPUS)>> last_pf_useful_core = {};
  champsim::stats::event_counter<std::remove_cv_t<decltype(NUM_CPUS)>> last_pf_fill_core = {};

  long total_miss_latency_cycles{};
  uint64_t total_returned_packets{};
};

cache_stats operator-(cache_stats lhs, cache_stats rhs);

#endif
