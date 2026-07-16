// AMPM (Access Map Pattern Matching) evaluated in the SAME harness/metrics as the SPPAM DSE, for a fair
// coverage/accuracy comparison. Ported from prefetcher/ampm (DPC4 AMPM): per-page access+prefetch bitmaps in a
// set-assoc region table; on each access, for each stride k, if (cur-k) and (cur-2k) were accessed and (cur+k)
// is not, prefetch (cur+k). Page-bounded, degree 2. Maps track cache residency (pruned on L2 eviction).
// Metrics match the DSE (instant fills, berti-in-stream-as-demand): coverage = 1 - ampm_misses/base_misses;
// accuracy = useful/(useful+useless) where useful=first demand hit of a prefetched line, useless=evicted-unused.
#include "cache.h"
#include "trace_reader.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace sppam_dse;

static const int BPP = 64;      // blocks per 4KB page (64B blocks)
static const int PAGE_SH = 6;   // block# -> page#  (block = paddr>>6; page = block>>6)

// AMPM region: per-page access + prefetch bitmaps, in a set-associative LRU table.
struct ARegion { bool valid = false; uint64_t page = 0; uint64_t lru = 0; std::vector<char> amap, pmap; };
struct RegionTable {
  int sets, ways; uint64_t clk = 0; std::vector<ARegion> e;
  RegionTable(int s, int w) : sets(s), ways(w), e((std::size_t)s * w) { for (auto& r : e) { r.amap.assign(BPP, 0); r.pmap.assign(BPP, 0); } }
  ARegion* find(uint64_t page) {
    std::size_t base = (std::size_t)(page % sets) * ways;
    for (int w = 0; w < ways; ++w) { ARegion& r = e[base + w]; if (r.valid && r.page == page) { r.lru = ++clk; return &r; } }
    return nullptr;
  }
  ARegion* alloc(uint64_t page) {
    std::size_t base = (std::size_t)(page % sets) * ways, victim = base;
    for (int w = 0; w < ways; ++w) { std::size_t i = base + w; if (!e[i].valid || e[i].lru < e[victim].lru) victim = i; }
    ARegion& r = e[victim]; r.valid = true; r.page = page; r.lru = ++clk;
    std::fill(r.amap.begin(), r.amap.end(), 0); std::fill(r.pmap.begin(), r.pmap.end(), 0);
    return &r;
  }
};

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: %s <trace.zst> [region_sets] [region_ways] [degree] [maxrec]\n", argv[0]); return 2; }
  std::string path = argv[1];
  int rsets = argc > 2 ? std::atoi(argv[2]) : 64;
  int rways = argc > 3 ? std::atoi(argv[3]) : 4;
  int degree = argc > 4 ? std::atoi(argv[4]) : 2;
  uint64_t maxrec = argc > 5 ? std::strtoull(argv[5], nullptr, 10) : 12000000ull;

  cache base(2048, 16), ampm(2048, 16);   // L2 geometry matches the DSE
  RegionTable rt(rsets, rways);
  uint64_t base_miss = 0, ampm_miss = 0, demands = 0, useful = 0, useless = 0, issued = 0;

  auto amap_set = [&](uint64_t blk, bool prefetch) {
    uint64_t page = blk >> PAGE_SH; int off = (int)(blk & (BPP - 1));
    ARegion* r = rt.find(page); if (!r) r = rt.alloc(page); (prefetch ? r->pmap : r->amap)[off] = 1;
  };
  auto amap_get = [&](uint64_t blk, bool prefetch) -> bool {
    uint64_t page = blk >> PAGE_SH; int off = (int)(blk & (BPP - 1)); ARegion* r = rt.find(page);
    return r && (prefetch ? r->pmap : r->amap)[off];
  };
  auto amap_clear = [&](uint64_t blk) {
    uint64_t page = blk >> PAGE_SH; int off = (int)(blk & (BPP - 1)); ARegion* r = rt.find(page);
    if (r) { r->amap[off] = 0; r->pmap[off] = 0; }
  };

  trace_reader tr(path);
  access_record rec; uint64_t total = 0;
  while (tr.next(rec)) {
    if (maxrec && ++total > maxrec) break;
    if (!rec.is_translated) continue;
    atype t = (atype)rec.type;
    if (t != atype::LOAD && t != atype::RFO && t != atype::PREFETCH) continue; // demand + berti (as demand)
    uint64_t blk = rec.paddr >> 6;
    ++demands;

    // BASELINE (no AMPM): demand hit/miss + install on miss.
    { bool up = false; if (!base.demand_access(blk, 0, up)) { ++base_miss; base.install(blk, 0, 0, false); } }

    // AMPM: demand hit/miss (+useful), install on miss, then run the AMPM prefetch pass.
    { bool up = false; bool hit = ampm.demand_access(blk, 0, up); if (up) ++useful;
      if (!hit) { ++ampm_miss; auto r = ampm.install(blk, 0, 0, false); if (r.evicted_unused_prefetch) ++useless; if (r.evicted_valid) amap_clear(r.evicted_block); } }

    amap_set(blk, false); // access-map update on this access (all L2 accesses, like the reference)

    // AMPM stride search: both directions, degree deep, page-bounded.
    uint64_t page = blk >> PAGE_SH;
    for (int dir : {1, -1}) {
      for (int i = 1, pi = 0; pi < degree; ++i) {
        uint64_t pos = blk + (uint64_t)(dir * i);
        uint64_t neg = blk - (uint64_t)(dir * i);
        uint64_t neg2 = blk - (uint64_t)(dir * 2 * i);
        if ((pos >> PAGE_SH) != page) break; // physical-page boundary
        if (amap_get(neg, false) && amap_get(neg2, false) && !amap_get(pos, false) && !amap_get(pos, true)) {
          if (pos != blk) {
            if (!ampm.resident(pos)) { // squash if already resident (no LRU/used side effect)
              auto r = ampm.install(pos, 0, 0, true); if (r.evicted_unused_prefetch) ++useless; if (r.evicted_valid) amap_clear(r.evicted_block); ++issued; }
            amap_set(pos, true); ++pi;
          }
        }
      }
    }
  }

  double cov = base_miss ? 100.0 * (1.0 - (double)ampm_miss / (double)base_miss) : 0.0;
  double acc = (useful + useless) ? 100.0 * (double)useful / (double)(useful + useless) : 0.0;
  std::size_t state = (std::size_t)rsets * rways;
  double kib = state * (36.0 /*page tag*/ + 2 * BPP /*maps*/ + 8 /*lru*/) / 8.0 / 1024.0;
  std::printf("%s  regions=%zu (~%.1fKiB)  cov=%.2f%%  acc=%.2f%%  pf/dem=%.3f  (misses %llu->%llu)\n",
              path.substr(path.find_last_of('/') + 1).c_str(), state, kib, cov, acc, demands ? (double)issued / demands : 0.0,
              (unsigned long long)base_miss, (unsigned long long)ampm_miss);
  return 0;
}
