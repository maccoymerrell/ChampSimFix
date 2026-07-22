// Bounded measurement of region-bitmap dedup potential (idea #2).
//
// Each time a region is evicted from the (tier-1) region table, its finalized
// access-map is packed to a uint64 and "finalized" here. We evaluate whether a
// fixed-size dedup ARCHIVE (set-assoc LRU on the bitmap) captures most of these
// finalizations: a hit means the page's history duplicates one already archived,
// so a 2-tier design could store a small archive-id instead of a full bitmap.
//
// Everything is bounded: the archives are fixed size, and the exact-distinct
// counter is capped (flags "exploded" rather than growing without limit), so the
// measurement is robust even if bitmaps are near-random.
#ifndef SPPAM_DSE_DEDUP_ANALYZER_H
#define SPPAM_DSE_DEDUP_ANALYZER_H

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "lru_table.h"

namespace sppam_dse
{

class dedup_analyzer
{
public:
  explicit dedup_analyzer(std::vector<std::size_t> archive_sizes = {256, 1024, 4096, 16384}, std::size_t ways = 8,
                          std::size_t distinct_cap = 4u * 1024u * 1024u)
      : sizes_(std::move(archive_sizes)), hits_(sizes_.size(), 0), distinct_cap_(distinct_cap)
  {
    for (std::size_t s : sizes_) {
      std::size_t sets = (s / ways) ? (s / ways) : 1;
      archives_.emplace_back(sets, ways);
    }
  }

  // Record one finalized region bitmap (0 = empty region: ignored).
  void finalize(uint64_t bitmap)
  {
    ++total_;
    if (bitmap == 0)
      return;
    ++nonzero_;
    popcount_sum_ += static_cast<uint64_t>(__builtin_popcountll(bitmap));
    for (std::size_t i = 0; i < archives_.size(); ++i) {
      if (archives_[i].find_key(bitmap) != nullptr)
        ++hits_[i];
      else
        archives_[i].insert(bitmap);
    }
    if (!exploded_) {
      distinct_.insert(bitmap);
      if (distinct_.size() >= distinct_cap_)
        exploded_ = true;
    }
  }

  std::string report(const std::string& tag) const
  {
    std::string out = "[dedup " + tag + "] finalized=" + std::to_string(total_) + " nonzero=" + std::to_string(nonzero_)
                      + " distinct=" + (exploded_ ? (">=" + std::to_string(distinct_cap_) + "(exploded)") : std::to_string(distinct_.size()));
    if (nonzero_ > 0) {
      double dr = static_cast<double>(exploded_ ? distinct_cap_ : distinct_.size()) / static_cast<double>(nonzero_);
      out += " avg_blocks_set=" + fmt(static_cast<double>(popcount_sum_) / static_cast<double>(nonzero_));
      out += " distinct/nonzero=" + fmt(dr);
      out += " | archive hit-rate:";
      for (std::size_t i = 0; i < sizes_.size(); ++i)
        out += " " + std::to_string(sizes_[i]) + "=" + fmt(static_cast<double>(hits_[i]) / static_cast<double>(nonzero_));
    }
    return out;
  }

private:
  struct id_proj {
    uint64_t operator()(uint64_t x) const { return x; }
  };
  static std::string fmt(double v)
  {
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v);
    return b;
  }

  std::vector<std::size_t> sizes_;
  std::vector<lru_table<uint64_t, id_proj, id_proj>> archives_;
  std::vector<uint64_t> hits_;
  std::unordered_set<uint64_t> distinct_;
  std::size_t distinct_cap_;
  bool exploded_ = false;
  uint64_t total_ = 0;
  uint64_t nonzero_ = 0;
  uint64_t popcount_sum_ = 0;
};

} // namespace sppam_dse

#endif
