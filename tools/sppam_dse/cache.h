// Set-associative LRU cache model with per-line readiness and prefetch tracking.
// Used for both the L2 (the prefetch target) and the LLC (a residency filter for
// the DRAM-latency model). Addresses are 64B block numbers (paddr >> 6).
#ifndef SPPAM_DSE_CACHE_H
#define SPPAM_DSE_CACHE_H

#include <cstdint>
#include <vector>

namespace sppam_dse
{

struct cache_line {
  bool valid = false;
  uint64_t tag = 0;       // block number
  uint64_t lru = 0;       // larger == more recently used
  uint64_t ready_at = 0;  // cycle the fill completes (timeliness)
  bool prefetched = false; // filled by a prefetch and not yet demand-used
  bool used = false;       // a prefetched line that has since been demand-hit
  bool from_spp = false;   // prefetch source (SPPAM vs SPP) -> per-prefetcher PE attribution (1 bit/line)
  bool from_dram = false;  // served by DRAM vs LLC -> L_DRAM/L_LLC split for I_UPF (1 bit/line)
};

// Result of installing a line: reports an evicted prefetched-but-unused line so
// the evaluator can count it as a useless prefetch and feed back its address.
struct install_result {
  bool evicted_valid = false;          // a valid line was replaced
  bool evicted_unused_prefetch = false; // ...and it was a not-yet-used prefetch
  bool evicted_from_spp = false;        // ...and that prefetch came from SPP
  uint64_t evicted_block = 0;
};

class cache
{
public:
  cache(std::size_t sets, std::size_t ways) : NUM_SET(sets ? sets : 1), NUM_WAY(ways ? ways : 1), lines_(NUM_SET * NUM_WAY) {}

  std::size_t sets() const { return NUM_SET; }
  std::size_t ways() const { return NUM_WAY; }

  // Residency probe without touching LRU.
  const cache_line* probe(uint64_t block) const
  {
    std::size_t s = set_of(block);
    for (std::size_t w = 0; w < NUM_WAY; ++w) {
      const cache_line& l = lines_[s * NUM_WAY + w];
      if (l.valid && l.tag == block)
        return &l;
    }
    return nullptr;
  }

  bool resident(uint64_t block) const { return probe(block) != nullptr; }

  // Resident and the fill has completed by `cycle`.
  bool resident_ready(uint64_t block, uint64_t cycle) const
  {
    const cache_line* l = probe(block);
    return l != nullptr && l->ready_at <= cycle;
  }

  // Demand access: bumps LRU. Returns true on hit. If the hit line was a
  // not-yet-used prefetch, sets `useful_prefetch` true and marks it used.
  bool demand_access(uint64_t block, uint64_t cycle, bool& useful_prefetch, bool* useful_from_spp = nullptr, bool* useful_from_dram = nullptr)
  {
    (void)cycle;
    useful_prefetch = false;
    cache_line* l = find_mut(block);
    if (l == nullptr)
      return false;
    l->lru = ++clock_;
    if (l->prefetched && !l->used) {
      useful_prefetch = true;
      if (useful_from_spp != nullptr) *useful_from_spp = l->from_spp;
      if (useful_from_dram != nullptr) *useful_from_dram = l->from_dram;
      l->used = true;
    }
    return true;
  }

  // Install a line (demand fill or prefetch). Updates in place if present, else
  // replaces the LRU way. `prefetched` marks the line for usefulness accounting.
  install_result install(uint64_t block, uint64_t cycle, uint64_t ready_at, bool prefetched, bool from_spp = false, bool from_dram = false)
  {
    (void)cycle;
    install_result res;
    std::size_t s = set_of(block);
    cache_line* victim = &lines_[s * NUM_WAY];
    for (std::size_t w = 0; w < NUM_WAY; ++w) {
      cache_line& l = lines_[s * NUM_WAY + w];
      if (l.valid && l.tag == block) {
        // Already present: refresh readiness; do not downgrade a used line.
        l.lru = ++clock_;
        if (ready_at < l.ready_at)
          l.ready_at = ready_at;
        return res;
      }
      if (!l.valid || (victim->valid && l.lru < victim->lru))
        victim = &l;
    }
    if (victim->valid) {
      res.evicted_valid = true;
      res.evicted_block = victim->tag;
      res.evicted_unused_prefetch = victim->prefetched && !victim->used;
      res.evicted_from_spp = res.evicted_unused_prefetch && victim->from_spp;
    }
    *victim = cache_line{true, block, ++clock_, ready_at, prefetched, false, from_spp, from_dram};
    return res;
  }

  // Returns true if a not-yet-used prefetched line was removed.
  bool invalidate(uint64_t block)
  {
    cache_line* l = find_mut(block);
    if (l == nullptr)
      return false;
    bool unused_pf = l->prefetched && !l->used;
    *l = cache_line{};
    return unused_pf;
  }

private:
  std::size_t set_of(uint64_t block) const { return static_cast<std::size_t>(block % NUM_SET); }

  cache_line* find_mut(uint64_t block)
  {
    std::size_t s = set_of(block);
    for (std::size_t w = 0; w < NUM_WAY; ++w) {
      cache_line& l = lines_[s * NUM_WAY + w];
      if (l.valid && l.tag == block)
        return &l;
    }
    return nullptr;
  }

  std::size_t NUM_SET;
  std::size_t NUM_WAY;
  uint64_t clock_ = 0;
  std::vector<cache_line> lines_;
};

} // namespace sppam_dse

#endif
