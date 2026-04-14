/*
 * Module-owned stats formatting.
 * Each module interface (core, cache, memory_controller) defines its own
 * plaintext and JSON stat printers. Logic moved here from plain_printer.cc
 * and json_printer.cc so the stats printer is agnostic to module types.
 */

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <ratio>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "modules.h"

namespace
{
template <typename N, typename D>
auto print_ratio(N num, D denom)
{
  if (denom > 0) {
    return fmt::format("{:.4g}", std::ceil(num) / std::ceil(denom));
  }
  return std::string{"-"};
}
} // namespace

// ============================================================================
// core_module formatting
// ============================================================================

std::vector<std::string> champsim::modules::core_module::format_plaintext(const stats_type& stats)
{
  constexpr std::array types{branch_type::BRANCH_DIRECT_JUMP, branch_type::BRANCH_INDIRECT,      branch_type::BRANCH_CONDITIONAL,
                             branch_type::BRANCH_DIRECT_CALL, branch_type::BRANCH_INDIRECT_CALL, branch_type::BRANCH_RETURN};
  auto total_branch = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0LL, [tbt = stats.total_branch_types](auto acc, auto next) { return acc + tbt.value_or(next, 0); }));
  auto total_mispredictions = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0LL, [btm = stats.branch_type_misses](auto acc, auto next) { return acc + btm.value_or(next, 0); }));

  std::vector<std::string> lines{};
  lines.push_back(fmt::format("{} cumulative IPC: {} instructions: {} cycles: {}", stats.name, ::print_ratio(stats.instrs(), stats.cycles()), stats.instrs(),
                              stats.cycles()));

  lines.push_back(fmt::format("{} Branch Prediction Accuracy: {}% MPKI: {} Average ROB Occupancy at Mispredict: {}", stats.name,
                              ::print_ratio(100 * (total_branch - total_mispredictions), total_branch),
                              ::print_ratio(std::kilo::num * total_mispredictions, stats.instrs()),
                              ::print_ratio(stats.total_rob_occupancy_at_branch_mispredict, total_mispredictions)));

  lines.emplace_back("Branch type MPKI");
  for (auto idx : types) {
    lines.push_back(fmt::format("{}: {}", branch_type_names.at(champsim::to_underlying(idx)),
                                ::print_ratio(std::kilo::num * stats.branch_type_misses.value_or(idx, 0), stats.instrs())));
  }

  return lines;
}

std::any champsim::modules::core_module::make_json(const stats_type& stats)
{
  constexpr std::array types{branch_type::BRANCH_DIRECT_JUMP, branch_type::BRANCH_INDIRECT,      branch_type::BRANCH_CONDITIONAL,
                             branch_type::BRANCH_DIRECT_CALL, branch_type::BRANCH_INDIRECT_CALL, branch_type::BRANCH_RETURN};

  auto total_mispredictions = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0LL, [btm = stats.branch_type_misses](auto acc, auto next) { return acc + btm.value_or(next, 0); }));

  std::map<std::string, std::size_t> mpki{};
  for (auto type : types) {
    mpki.emplace(branch_type_names.at(champsim::to_underlying(type)), stats.branch_type_misses.value_or(type, 0));
  }

  return nlohmann::json{{"instructions", stats.instrs()},
                        {"cycles", stats.cycles()},
                        {"Avg ROB occupancy at mispredict", std::ceil(stats.total_rob_occupancy_at_branch_mispredict) / std::ceil(total_mispredictions)},
                        {"mispredict", mpki}};
}

std::vector<std::string> champsim::modules::core_module::give_stats(bool is_roi) const
{
  return format_plaintext(is_roi ? get_roi_stats() : get_sim_stats());
}

std::any champsim::modules::core_module::give_stats_json(bool is_roi) const
{
  return make_json(is_roi ? get_roi_stats() : get_sim_stats());
}

// source_consumer hooks for phase messaging
int champsim::modules::core_module::entity_index() const { return static_cast<int>(get_cpu_num()); }

uint64_t champsim::modules::core_module::sim_progress() const { return sim_instr(); }

std::string champsim::modules::core_module::entity_finish_message(const std::string& phase_name) const
{
  return fmt::format("{} finished CPU {} instructions: {} cycles: {} cumulative IPC: {:.4g}",
                     phase_name, get_cpu_num(), sim_instr(), sim_cycle(),
                     std::ceil(static_cast<double>(sim_instr())) / std::ceil(static_cast<double>(sim_cycle())));
}

std::string champsim::modules::core_module::phase_complete_message(const std::string& phase_name) const
{
  return fmt::format("{} complete CPU {} instructions: {} cycles: {} cumulative IPC: {:.4g}",
                     phase_name, get_cpu_num(), sim_instr(), sim_cycle(),
                     std::ceil(static_cast<double>(sim_instr())) / std::ceil(static_cast<double>(sim_cycle())));
}

// ============================================================================
// cache_module formatting
// ============================================================================

std::vector<std::string> champsim::modules::cache_module::format_plaintext(const stats_type& stats)
{
  using hits_value_type = typename decltype(stats.hits)::value_type;
  using misses_value_type = typename decltype(stats.misses)::value_type;
  using miss_merge_value_type = typename decltype(stats.miss_merge)::value_type;
  using fill_value_type = typename decltype(stats.fill)::value_type;

  // We need mutable copies to allocate missing keys
  auto hits = stats.hits;
  auto misses = stats.misses;
  auto miss_merge = stats.miss_merge;
  auto fill = stats.fill;

  std::vector<std::size_t> cpus;

  // build a vector of all existing cpus
  auto stat_keys = {hits.get_keys(), misses.get_keys(), miss_merge.get_keys(), fill.get_keys()};
  for (auto keys : stat_keys) {
    std::transform(std::begin(keys), std::end(keys), std::back_inserter(cpus), [](auto val) { return val.second; });
  }
  std::sort(std::begin(cpus), std::end(cpus));
  auto uniq_end = std::unique(std::begin(cpus), std::end(cpus));
  cpus.erase(uniq_end, std::end(cpus));

  for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
    for (auto cpu : cpus) {
      hits.allocate(std::pair{type, cpu});
      misses.allocate(std::pair{type, cpu});
      miss_merge.allocate(std::pair{type, cpu});
      fill.allocate(std::pair{type, cpu});
    }
  }

  std::vector<std::string> lines{};
  for (auto cpu : cpus) {
    hits_value_type total_hits = 0;
    misses_value_type total_misses = 0;
    miss_merge_value_type total_miss_merge = 0;
    fill_value_type total_fill = 0;
    for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
      total_hits += hits.value_or(std::pair{type, cpu}, hits_value_type{});
      total_misses += misses.value_or(std::pair{type, cpu}, misses_value_type{});
      total_miss_merge += miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{});
      total_fill += fill.value_or(std::pair{type, cpu}, miss_merge_value_type{});
    }

    fmt::format_string<std::string_view, std::string_view, int, int, int> hitmiss_fmtstr{
        "cpu{}->{} {:<12s} ACCESS: {:10d} HIT: {:10d} MISS: {:10d} MISS_MERGE: {:10d}"};
    lines.push_back(fmt::format(hitmiss_fmtstr, cpu, stats.name, "TOTAL", total_hits + total_misses, total_hits, total_misses, total_miss_merge));
    for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
      lines.push_back(
          fmt::format(hitmiss_fmtstr, cpu, stats.name, access_type_names.at(champsim::to_underlying(type)),
                      hits.value_or(std::pair{type, cpu}, hits_value_type{}) + misses.value_or(std::pair{type, cpu}, misses_value_type{}),
                      hits.value_or(std::pair{type, cpu}, hits_value_type{}), misses.value_or(std::pair{type, cpu}, misses_value_type{}),
                      miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{})));
    }

    lines.push_back(fmt::format("cpu{}->{} PREFETCH REQUESTED: {:10} ISSUED: {:10} USEFUL: {:10} USELESS: {:10}", cpu, stats.name, stats.pf_requested,
                                stats.pf_issued, stats.pf_useful, stats.pf_useless));

    uint64_t total_downstream_demands = total_fill - fill.value_or(std::pair{access_type::PREFETCH, cpu}, fill_value_type{});
    lines.push_back(
        fmt::format("cpu{}->{} AVERAGE MISS LATENCY: {} cycles", cpu, stats.name, ::print_ratio(stats.total_miss_latency_cycles, total_downstream_demands)));
  }

  return lines;
}

std::any champsim::modules::cache_module::make_json(const stats_type& stats)
{
  using hits_value_type = typename decltype(stats.hits)::value_type;
  using misses_value_type = typename decltype(stats.misses)::value_type;
  using miss_merge_value_type = typename decltype(stats.miss_merge)::value_type;
  using fill_value_type = typename decltype(stats.fill)::value_type;

  std::map<std::string, nlohmann::json> statsmap;
  statsmap.emplace("prefetch requested", stats.pf_requested);
  statsmap.emplace("prefetch issued", stats.pf_issued);
  statsmap.emplace("useful prefetch", stats.pf_useful);
  statsmap.emplace("useless prefetch", stats.pf_useless);

  // Discover CPU indices from the data itself (mirrors format_plaintext approach)
  std::vector<std::size_t> cpus;
  auto stat_keys = {stats.hits.get_keys(), stats.misses.get_keys(), stats.miss_merge.get_keys(), stats.fill.get_keys()};
  for (auto keys : stat_keys) {
    std::transform(std::begin(keys), std::end(keys), std::back_inserter(cpus), [](auto val) { return val.second; });
  }
  std::sort(std::begin(cpus), std::end(cpus));
  cpus.erase(std::unique(std::begin(cpus), std::end(cpus)), std::end(cpus));

  uint64_t total_downstream_demands = stats.fill.total();
  for (auto cpu : cpus)
    total_downstream_demands -= stats.fill.value_or(std::pair{access_type::PREFETCH, cpu}, fill_value_type{});

  statsmap.emplace("miss latency", std::ceil(stats.total_miss_latency_cycles) / std::ceil(total_downstream_demands));
  for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
    std::vector<hits_value_type> hit_vec;
    std::vector<misses_value_type> miss_vec;
    std::vector<miss_merge_value_type> miss_merge_vec;

    for (auto cpu : cpus) {
      hit_vec.push_back(stats.hits.value_or(std::pair{type, cpu}, hits_value_type{}));
      miss_vec.push_back(stats.misses.value_or(std::pair{type, cpu}, misses_value_type{}));
      miss_merge_vec.push_back(stats.miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{}));
    }

    statsmap.emplace(access_type_names.at(champsim::to_underlying(type)), nlohmann::json{{"hit", hit_vec}, {"miss", miss_vec}, {"miss_merge", miss_merge_vec}});
  }

  return nlohmann::json(statsmap);
}

std::vector<std::string> champsim::modules::cache_module::give_stats(bool is_roi) const
{
  return format_plaintext(is_roi ? get_roi_stats() : get_sim_stats());
}

std::any champsim::modules::cache_module::give_stats_json(bool is_roi) const
{
  return make_json(is_roi ? get_roi_stats() : get_sim_stats());
}

// ============================================================================
// memory_controller_module formatting
// ============================================================================

std::vector<std::string> champsim::modules::memory_controller_module::format_plaintext(const stats_type& stats)
{
  std::vector<std::string> lines{};
  lines.push_back(fmt::format("{} RQ ROW_BUFFER_HIT: {:10}", stats.name, stats.RQ_ROW_BUFFER_HIT));
  lines.push_back(fmt::format("  ROW_BUFFER_MISS: {:10}", stats.RQ_ROW_BUFFER_MISS));
  lines.push_back(fmt::format("  AVG DBUS CONGESTED CYCLE: {}", ::print_ratio(stats.dbus_cycle_congested, stats.dbus_count_congested)));
  lines.push_back(fmt::format("{} WQ ROW_BUFFER_HIT: {:10}", stats.name, stats.WQ_ROW_BUFFER_HIT));
  lines.push_back(fmt::format("  ROW_BUFFER_MISS: {:10}", stats.WQ_ROW_BUFFER_MISS));
  lines.push_back(fmt::format("  FULL: {:10}", stats.WQ_FULL));

  if (stats.refresh_cycles > 0)
    lines.push_back(fmt::format("{} REFRESHES ISSUED: {:10}", stats.name, stats.refresh_cycles));
  else
    lines.push_back(fmt::format("{} REFRESHES ISSUED: -", stats.name));

  return lines;
}

std::any champsim::modules::memory_controller_module::make_json(const stats_type& stats)
{
  return nlohmann::json{{"RQ ROW_BUFFER_HIT", stats.RQ_ROW_BUFFER_HIT},
                        {"RQ ROW_BUFFER_MISS", stats.RQ_ROW_BUFFER_MISS},
                        {"WQ ROW_BUFFER_HIT", stats.WQ_ROW_BUFFER_HIT},
                        {"WQ ROW_BUFFER_MISS", stats.WQ_ROW_BUFFER_MISS},
                        {"AVG DBUS CONGESTED CYCLE", (std::ceil(stats.dbus_cycle_congested) / std::ceil(stats.dbus_count_congested))},
                        {"REFRESHES ISSUED", stats.refresh_cycles}};
}

std::vector<std::string> champsim::modules::memory_controller_module::give_stats(bool is_roi) const
{
  std::vector<std::string> lines;
  for (std::size_t i = 0; i < get_num_channels(); ++i) {
    auto chan_lines = format_plaintext(is_roi ? get_roi_stats(i) : get_sim_stats(i));
    std::move(std::begin(chan_lines), std::end(chan_lines), std::back_inserter(lines));
  }
  return lines;
}

std::any champsim::modules::memory_controller_module::give_stats_json(bool is_roi) const
{
  nlohmann::json arr = nlohmann::json::array();
  for (std::size_t i = 0; i < get_num_channels(); ++i) {
    arr.push_back(std::any_cast<nlohmann::json>(make_json(is_roi ? get_roi_stats(i) : get_sim_stats(i))));
  }
  return arr;
}
