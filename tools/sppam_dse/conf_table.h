// Confidence table, faithfully reimplemented from the original SPPAM
// sppam_util::msl::conf_table (DPC4 prefetcher/sppam/conf_table.h) with plain
// uint64 keys.
//
// Used as a pattern's prediction table: the value IS the predicted-block bitmap,
// the set index is value % NUM_SET and the tag is the value itself.
//   * fill(v):       insert v at confidence 25 (matching tag updated in place,
//                    else lowest-confidence/ invalid way replaced).
//   * incr_conf(v):  +1 (cap 100); when an entry reaches 100, halve the set.
//   * get_highest_conf(min): the value with the strictly-highest confidence that
//                    exceeds `min`, else nullopt.
#ifndef SPPAM_DSE_CONF_TABLE_H
#define SPPAM_DSE_CONF_TABLE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sppam_dse
{

class conf_table
{
public:
  conf_table(std::size_t sets, std::size_t ways)
      : NUM_SET(sets ? sets : 1), NUM_WAY(ways), block(NUM_SET * NUM_WAY), best_conf_(NUM_SET, 0), best_data_(NUM_SET, 0), best_valid_(NUM_SET, false)
  {
  }

  std::optional<uint64_t> incr_conf(uint64_t v)
  {
    auto [b, e] = set_span(v);
    auto hit = e;
    for (auto it = b; it != e; ++it)
      if (it->valid && it->data == v) {
        hit = it;
        break;
      }
    if (hit == e)
      return std::nullopt;
    std::size_t set = static_cast<std::size_t>(v % NUM_SET);
    if (hit->conf < 100)
      hit->conf += 1;
    if (hit->conf >= 100) {
      for (auto it = b; it != e; ++it)
        it->conf >>= 1;
      recompute_best(set, b, e);
    } else if (!best_valid_[set] || hit->conf > best_conf_[set]) {
      best_valid_[set] = true;
      best_conf_[set] = hit->conf;
      best_data_[set] = hit->data;
    }
    return hit->data;
  }

  // Highest-confidence value in the set of `query` (default 0, as SPPAM's
  // single-set prediction tables are always queried with 0) whose confidence is
  // strictly greater than `min_conf`.
  // O(1): the strictly-greater-than-min_conf max is the cached per-set best.
  std::optional<uint64_t> get_highest_conf(uint64_t min_conf, uint64_t query = 0) const
  {
    std::size_t set = static_cast<std::size_t>(query % NUM_SET);
    if (best_valid_[set] && best_conf_[set] > min_conf)
      return best_data_[set];
    return std::nullopt;
  }

  void fill(uint64_t v)
  {
    std::size_t set = static_cast<std::size_t>(v % NUM_SET);
    auto [b, e] = set_span(v);
    auto victim = b;
    auto match = e;
    for (auto it = b; it != e; ++it) {
      if (it->valid && it->data == v)
        match = it;
      // victim: invalid first, else lowest confidence (matches original
      // match_and_check minmax: non-matching lowest-conf / invalid).
      if (!it->valid || (victim->valid && it->conf < victim->conf))
        victim = it;
    }
    auto dst = (match != e) ? match : victim;
    *dst = block_t{true, 25, v};
    recompute_best(set, b, e);
  }

private:
  struct block_t {
    bool valid = false;
    uint64_t conf = 0;
    uint64_t data = 0;
  };

  std::pair<std::vector<block_t>::iterator, std::vector<block_t>::iterator> set_span(uint64_t v)
  {
    std::size_t set = static_cast<std::size_t>(v % NUM_SET);
    auto b = block.begin() + static_cast<std::ptrdiff_t>(set * NUM_WAY);
    return {b, b + static_cast<std::ptrdiff_t>(NUM_WAY)};
  }

  void recompute_best(std::size_t set, std::vector<block_t>::iterator b, std::vector<block_t>::iterator e)
  {
    best_valid_[set] = false;
    best_conf_[set] = 0;
    for (auto it = b; it != e; ++it)
      if (it->valid && (!best_valid_[set] || it->conf > best_conf_[set])) {
        best_valid_[set] = true;
        best_conf_[set] = it->conf;
        best_data_[set] = it->data;
      }
  }

  std::size_t NUM_SET;
  std::size_t NUM_WAY;
  std::vector<block_t> block;
  // Cached highest-confidence entry per set (kept current by incr_conf/fill).
  std::vector<uint64_t> best_conf_;
  std::vector<uint64_t> best_data_;
  std::vector<bool> best_valid_;
};

} // namespace sppam_dse

#endif
