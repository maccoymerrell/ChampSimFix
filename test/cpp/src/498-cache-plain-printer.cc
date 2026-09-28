#include <catch.hpp>
#include <fmt/core.h>

#include "cache_stats.h"
#include "modules.h"
#include "stat_report.h"

namespace
{
// The plaintext half of the merged stat formatter.
std::vector<std::string> plaintext(const cache_stats& stats)
{
  champsim::stat_report report;
  champsim::modules::cache_module::format_stats(stats, report);
  return report.text();
}

std::string row(int cpu, std::string_view type, int access, int hit, int miss, int merge)
{
  return fmt::format("cpu{}->test_cache {:<20s} ACCESS: {:10d} HIT: {:10d} MISS: {:10d} MISS_MERGE: {:10d}", cpu, type, access, hit, miss, merge);
}

// Row order after TOTAL: the five generic rows, then the instruction/data specializations.
constexpr std::array<std::string_view, 9> row_names{"LOAD",        "RFO",       "PREFETCH",         "WRITE",        "TRANSLATION",
                                                    "INSTRUCTION_LOAD", "DATA_LOAD", "INSTRUCTION_PREFETCH", "DATA_PREFETCH"};

// One cpu's block: the totals line, every type row at zero, the prefetch line, the AMAT line.
std::vector<std::string> zero_block(int cpu, std::string total, std::string pf_line, std::string amat_line)
{
  std::vector<std::string> block{std::move(total)};
  for (auto name : row_names)
    block.push_back(row(cpu, name, 0, 0, 0, 0));
  block.push_back(std::move(pf_line));
  block.push_back(std::move(amat_line));
  return block;
}

std::string pf_line(int cpu, int requested, int issued, int useful, int useless)
{
  return fmt::format("cpu{}->test_cache PREFETCH REQUESTED: {:10} ISSUED: {:10} USEFUL: {:10} USELESS: {:10}", cpu, requested, issued, useful, useless);
}

// A recorded type and the rows (1-based after TOTAL) it must appear in: a specific type shows in its
// own row and its generic row; a generic type only in its own row.
using type_rows = std::pair<access_type, std::vector<std::size_t>>;
} // namespace

TEST_CASE("An empty cache stat block prints nothing")
{
  cache_stats given{};
  given.name = "test_cache";

  std::vector<std::string> expected{};

  REQUIRE_THAT(plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("Hits increment the hit and access counts")
{
  auto [hit_type, rows_hit] =
      GENERATE(as<type_rows>{}, type_rows{access_type::LOAD, {1}}, type_rows{access_type::RFO, {2}}, type_rows{access_type::PREFETCH, {3}},
               type_rows{access_type::WRITE, {4}}, type_rows{access_type::TRANSLATION, {5}}, type_rows{access_type::INSTRUCTION_LOAD, {1, 6}},
               type_rows{access_type::DATA_LOAD, {1, 7}}, type_rows{access_type::INSTRUCTION_PREFETCH, {3, 8}}, type_rows{access_type::DATA_PREFETCH, {3, 9}});

  cache_stats given{};
  given.name = "test_cache";
  given.hits.set({hit_type, 0}, 255);

  auto expected = zero_block(0, row(0, "TOTAL", 255, 255, 0, 0), pf_line(0, 0, 0, 0, 0), "cpu0->test_cache AVERAGE MISS LATENCY: - cycles");
  for (auto i : rows_hit)
    expected.at(i) = row(0, row_names.at(i - 1), 255, 255, 0, 0);

  REQUIRE_THAT(plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("Misses increment the miss and access counts")
{
  auto [miss_type, rows_hit] =
      GENERATE(as<type_rows>{}, type_rows{access_type::LOAD, {1}}, type_rows{access_type::RFO, {2}}, type_rows{access_type::PREFETCH, {3}},
               type_rows{access_type::WRITE, {4}}, type_rows{access_type::TRANSLATION, {5}}, type_rows{access_type::INSTRUCTION_LOAD, {1, 6}},
               type_rows{access_type::DATA_LOAD, {1, 7}}, type_rows{access_type::INSTRUCTION_PREFETCH, {3, 8}}, type_rows{access_type::DATA_PREFETCH, {3, 9}});

  cache_stats given{};
  given.name = "test_cache";
  given.misses.set({miss_type, 0}, 255);

  auto expected = zero_block(0, row(0, "TOTAL", 255, 0, 255, 0), pf_line(0, 0, 0, 0, 0), "cpu0->test_cache AVERAGE MISS LATENCY: - cycles");
  for (auto i : rows_hit)
    expected.at(i) = row(0, row_names.at(i - 1), 255, 0, 255, 0);

  REQUIRE_THAT(plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("A generic row sums its specializations, which keep their own rows")
{
  cache_stats given{};
  given.name = "test_cache";
  given.hits.set({access_type::INSTRUCTION_LOAD, 0}, 3);
  given.hits.set({access_type::DATA_LOAD, 0}, 5);

  auto expected = zero_block(0, row(0, "TOTAL", 8, 8, 0, 0), pf_line(0, 0, 0, 0, 0), "cpu0->test_cache AVERAGE MISS LATENCY: - cycles");
  expected.at(1) = row(0, "LOAD", 8, 8, 0, 0);
  expected.at(6) = row(0, "INSTRUCTION_LOAD", 3, 3, 0, 0);
  expected.at(7) = row(0, "DATA_LOAD", 5, 5, 0, 0);

  REQUIRE_THAT(plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("Returning misses increment the AMAT")
{
  auto num_miss_returned = 128;
  auto num_miss_merged = 127;
  auto miss_return_latency = GENERATE(1, 2, 6);
  auto [miss_type, rows_hit] = GENERATE(as<type_rows>{}, type_rows{access_type::LOAD, {1}}, type_rows{access_type::RFO, {2}},
                                        type_rows{access_type::WRITE, {4}}, type_rows{access_type::TRANSLATION, {5}},
                                        type_rows{access_type::INSTRUCTION_LOAD, {1, 6}}, type_rows{access_type::DATA_LOAD, {1, 7}});

  cache_stats given{};
  given.name = "test_cache";
  given.fill.set({miss_type, 0}, num_miss_returned);
  given.miss_merge.set({miss_type, 0}, num_miss_merged);
  given.misses.set({miss_type, 0}, num_miss_merged + num_miss_returned);
  given.total_miss_latency_cycles = miss_return_latency * num_miss_returned;

  auto expected = zero_block(0, row(0, "TOTAL", 255, 0, 255, 127), pf_line(0, 0, 0, 0, 0),
                             "cpu0->test_cache AVERAGE MISS LATENCY: " + std::to_string(miss_return_latency) + " cycles");
  for (auto i : rows_hit)
    expected.at(i) = row(0, row_names.at(i - 1), 255, 0, 255, 127);

  REQUIRE_THAT(plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("Prefetch counters are reported, and prefetch fills are not demands")
{
  auto [requested, issued, useful, useless] = GENERATE(std::tuple{1, 0, 0, 0}, std::tuple{0, 1, 0, 0}, std::tuple{0, 0, 1, 0}, std::tuple{0, 0, 0, 1});
  auto fill_type = GENERATE(access_type::PREFETCH, access_type::INSTRUCTION_PREFETCH, access_type::DATA_PREFETCH);

  cache_stats given{};
  given.name = "test_cache";
  given.pf_requested = static_cast<uint64_t>(requested);
  given.pf_issued = static_cast<uint64_t>(issued);
  given.pf_useful = static_cast<uint64_t>(useful);
  given.pf_useless = static_cast<uint64_t>(useless);
  given.fill.set({fill_type, 0}, 1);

  auto expected = zero_block(0, row(0, "TOTAL", 0, 0, 0, 0), pf_line(0, requested, issued, useful, useless), "cpu0->test_cache AVERAGE MISS LATENCY: - cycles");

  REQUIRE_THAT(plaintext(given), Catch::Matchers::RangeEquals(expected));
}

TEST_CASE("Multicore stats are tracked separately")
{
  cache_stats given{};
  given.name = "test_cache";

  auto [type_cpu0, row_cpu0] = GENERATE(as<std::pair<access_type, std::size_t>>{}, std::pair{access_type::LOAD, 1}, std::pair{access_type::RFO, 2},
                                        std::pair{access_type::PREFETCH, 3}, std::pair{access_type::WRITE, 4}, std::pair{access_type::TRANSLATION, 5});
  auto [type_cpu1, row_cpu1] = GENERATE(as<std::pair<access_type, std::size_t>>{}, std::pair{access_type::LOAD, 1}, std::pair{access_type::RFO, 2},
                                        std::pair{access_type::PREFETCH, 3}, std::pair{access_type::WRITE, 4}, std::pair{access_type::TRANSLATION, 5});
  given.hits.set({type_cpu0, 0}, 7);
  given.hits.set({type_cpu1, 1}, 11);

  auto expected = zero_block(0, row(0, "TOTAL", 7, 7, 0, 0), pf_line(0, 0, 0, 0, 0), "cpu0->test_cache AVERAGE MISS LATENCY: - cycles");
  expected.at(row_cpu0) = row(0, row_names.at(row_cpu0 - 1), 7, 7, 0, 0);
  auto cpu1 = zero_block(1, row(1, "TOTAL", 11, 11, 0, 0), pf_line(1, 0, 0, 0, 0), "cpu1->test_cache AVERAGE MISS LATENCY: - cycles");
  cpu1.at(row_cpu1) = row(1, row_names.at(row_cpu1 - 1), 11, 11, 0, 0);
  expected.insert(std::end(expected), std::begin(cpu1), std::end(cpu1));

  REQUIRE_THAT(plaintext(given), Catch::Matchers::RangeEquals(expected));
}
