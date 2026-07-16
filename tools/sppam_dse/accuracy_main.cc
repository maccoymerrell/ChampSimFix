// Pure prediction-accuracy model for SPPAM+SPP. NO cache / latency / bandwidth.
//
// Question: given the demand access stream, which predictor configuration anticipates
// the FUTURE accesses best? We replay the demand stream, run the predictor to emit its
// predicted blocks per access, and score each prediction against the actual future
// accesses within a horizon W (the precomputed block->future-indices index). Two metrics:
//   accuracy  = of predictions issued, fraction accessed within the next W demands.
//   coverage  = of future demands, fraction anticipated by some earlier prediction.
// One trace is read once into memory; many configs are scored in-memory (fast).
//
// Build: make accuracy_eval
// Run:   ./accuracy_eval --trace t.bin.zst --configs cfgs.json [--horizon 256] [--max-records N]
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "access_kind.h"
#include "params.h"
#include "prefetch_sink.h"
#include "spp_predictor.h"
#include "sppam_predictor.h"
#include "trace_reader.h"

using namespace sppam_dse;

// Collects the predictor(s)' predictions for the current trigger and applies the
// SPPAM/SPP fall-through coordination (SPP defers to SPPAM on a claimed trigger, with a
// 1/N explore trickle) -- same as the ChampSim module, so the combination is scored too.
struct oracle_sink : prefetch_sink {
  bool fallthrough = true;
  uint64_t ft_div = 16;
  bool sppam_fired = false;
  uint64_t ft_ctr = 0;
  std::vector<std::pair<uint64_t, int>> preds; // (block, engine: 0=SPPAM 1=SPP)

  void issue_prefetch(uint64_t block, bool /*fill_l2*/, bool from_spp, double /*benefit*/) override
  {
    if (!from_spp)
      sppam_fired = true;
    if (fallthrough && from_spp && sppam_fired) {
      if (!(ft_div && (++ft_ctr % ft_div == 0)))
        return;
    }
    preds.push_back({block, from_spp ? 1 : 0});
  }
  int dram_bw_index() const override { return 0; }
};

// Classify every demand by the pattern it exhibits and report coverage per class, so we
// see WHAT the hybrid misses. Classes (by which mechanism would catch it):
//   cold_page  - first touch of its 4 KB page (needs cross-page / next-page)
//   stride     - delta from the page's last access repeats the prior delta (SPP/stride)
//   temporal   - block was accessed before, irregular delta (needs correlation/Markov)
//   irregular  - in-page, new block, no repeating delta (hard)
void miss_analysis(const std::vector<uint64_t>& blocks, const std::vector<uint8_t>& covered, const std::string& name)
{
  enum { COLD, STRIDE, TEMPORAL, IRREG, NCAT };
  const char* nm[NCAT] = {"cold_page", "stride", "temporal", "irregular"};
  long tot[NCAT] = {0}, cov[NCAT] = {0};
  std::unordered_map<uint64_t, int> last_off, last_delta;
  std::unordered_set<uint64_t> page_seen, block_seen;
  last_off.reserve(1 << 20); block_seen.reserve(1 << 22);
  const uint32_t N = static_cast<uint32_t>(blocks.size());
  for (uint32_t j = 0; j < N; ++j) {
    uint64_t b = blocks[j], pg = b >> 6;
    int off = static_cast<int>(b & 63);
    int cat;
    auto pit = last_off.find(pg);
    if (!page_seen.count(pg)) {
      cat = COLD;
    } else {
      int lo = pit->second;
      int d = off - lo;
      auto dit = last_delta.find(pg);
      int ld = (dit != last_delta.end()) ? dit->second : 0;
      if (d != 0 && d == ld) cat = STRIDE;
      else if (block_seen.count(b)) cat = TEMPORAL;
      else cat = IRREG;
      last_delta[pg] = d;
    }
    ++tot[cat]; if (covered[j]) ++cov[cat];
    last_off[pg] = off; page_seen.insert(pg); block_seen.insert(b);
  }
  long miss_tot = 0; for (int c = 0; c < NCAT; ++c) miss_tot += tot[c] - cov[c];
  std::printf("\n=== miss analysis: %s  (N=%u, total coverage=%.3f) ===\n", name.c_str(), N,
              1.0 - static_cast<double>(miss_tot) / static_cast<double>(N));
  std::printf("%-11s %11s %7s %9s %12s %8s\n", "class", "demands", "%dem", "covered%", "misses", "%ofmiss");
  for (int c = 0; c < NCAT; ++c) {
    long miss = tot[c] - cov[c];
    std::printf("%-11s %11ld %6.1f%% %8.1f%% %12ld %7.1f%%\n", nm[c], tot[c], 100.0 * tot[c] / N,
                tot[c] ? 100.0 * cov[c] / tot[c] : 0.0, miss, miss_tot ? 100.0 * miss / miss_tot : 0.0);
  }
}

// Simple L2 cache residency model (LRU) so the miss analysis counts a demand as a REAL
// miss only when the block isn't resident -- filled by demands AND issued prefetches.
struct L2Model {
  int sets, ways;
  std::vector<uint64_t> tag;
  std::vector<uint8_t> valid;
  std::vector<uint32_t> lru;
  uint32_t clk = 0;
  L2Model(int s, int w) : sets(s), ways(w), tag(static_cast<std::size_t>(s) * w, 0), valid(static_cast<std::size_t>(s) * w, 0), lru(static_cast<std::size_t>(s) * w, 0) {}
  bool contains(uint64_t b) const {
    std::size_t base = (b % sets) * ways;
    for (int k = 0; k < ways; ++k)
      if (valid[base + k] && tag[base + k] == b) return true;
    return false;
  }
  void fill(uint64_t b) {
    std::size_t base = (b % sets) * ways; int hit = -1, lw = 0;
    for (int k = 0; k < ways; ++k) {
      if (valid[base + k] && tag[base + k] == b) hit = k;
      if (!valid[base + k] || lru[base + k] < lru[base + lw]) lw = k;
    }
    int w = (hit >= 0) ? hit : lw;
    tag[base + w] = b; valid[base + w] = 1; lru[base + w] = ++clk;
  }
};

// L2 cache that tracks prefetch COVERAGE and ACCURACY: lines are tagged prefetch/demand
// and used/unused, so a demand hit on an unused prefetched line counts as useful, and a
// prefetched line evicted unused counts as useless. coverage = misses removed vs baseline;
// accuracy = useful / issued.
struct L2Acc {
  int S, W;
  std::vector<uint64_t> tag;
  std::vector<uint8_t> v, pf, used;
  std::vector<uint32_t> lru;
  uint32_t clk = 0;
  long dmiss = 0, issued = 0, useful = 0;
  L2Acc(int s, int w) : S(s), W(w), tag((std::size_t)s * w, 0), v((std::size_t)s * w, 0), pf((std::size_t)s * w, 0), used((std::size_t)s * w, 0), lru((std::size_t)s * w, 0) {}
  int find(uint64_t b, std::size_t& base) { base = (std::size_t)(b % S) * W; for (int k = 0; k < W; ++k) if (v[base + k] && tag[base + k] == b) return k; return -1; }
  int victim(std::size_t base) { int lw = 0; for (int k = 0; k < W; ++k) if (!v[base + k] || lru[base + k] < lru[base + lw]) lw = k; return lw; }
  // A real L2 access (demand LOAD/RFO, or berti's PREFETCH request). On a hit of an L2-
  // prefetched, not-yet-used line, the L2 prefetcher was useful (it served this access).
  // On a miss it fills (the line comes from below); only DEMAND misses count for coverage.
  void access(uint64_t b, bool is_demand) {
    std::size_t base; int h = find(b, base);
    if (h >= 0) { if (pf[base + h] && !used[base + h]) ++useful; used[base + h] = 1; lru[base + h] = ++clk; return; }
    if (is_demand) ++dmiss;
    int w = victim(base);
    tag[base + w] = b; v[base + w] = 1; pf[base + w] = 0; used[base + w] = 1; lru[base + w] = ++clk;
  }
  // The L2 prefetcher's own prefetch (next-line / SPPAM). Only these count toward accuracy.
  void prefetch(uint64_t b) {
    std::size_t base; if (find(b, base) >= 0) return; // already resident
    ++issued; int w = victim(base);
    tag[base + w] = b; v[base + w] = 1; pf[base + w] = 1; used[base + w] = 0; lru[base + w] = ++clk;
  }
  double accuracy() const { return issued ? 100.0 * useful / issued : 0.0; }
};

// Page-gridded miss analysis: classify each access by its LOCAL (within-page) indicator,
// using a bounded page table whose geometry MATCHES SPPAM's real region table (set by
// the caller) and LRU eviction = page residency -- so "seen before" means seen IN THIS
// PAGE WHILE RESIDENT, the way SPP/SPPAM actually see it. Tests whether globally-"temporal"
// misses are locally discernible.
//   cold        - first access of a (re)allocated page instance (cross-page)
//   stride      - delta == the page's immediately-previous delta
//   recur_delta - delta already occurred earlier in this page instance (SPP signature)
//   revisit     - this offset was already accessed in this page instance (in-page reuse)
//   novel       - new block, delta never seen in-page (the real spatial frontier)
void page_miss_analysis(const std::vector<uint64_t>& blocks, const std::vector<uint8_t>& covered,
                        const std::string& name, int SETS, int WAYS)
{
  struct Way { bool valid = false; uint64_t tag = 0; uint32_t lru = 0; uint64_t amap = 0; uint64_t dlo = 0, dhi = 0; int last_off = -1; int last_delta = 1000; };
  std::vector<Way> tbl(static_cast<std::size_t>(SETS * WAYS));
  uint32_t clk = 0;
  auto dset = [](Way& w, int d) { int x = d + 64; if (x < 0 || x > 127) return; (x < 64 ? w.dlo : w.dhi) |= (uint64_t{1} << (x & 63)); };
  auto dhas = [](const Way& w, int d) -> bool { int x = d + 64; if (x < 0 || x > 127) return false; return ((x < 64 ? w.dlo : w.dhi) >> (x & 63)) & 1; };
  enum { COLD, STRIDE, RECUR, REVISIT, NOVEL, NCAT };
  const char* nm[NCAT] = {"cold", "stride", "recur_delta", "revisit", "novel"};
  long tot[NCAT] = {0}, cov[NCAT] = {0};
  const uint32_t N = static_cast<uint32_t>(blocks.size());
  for (uint32_t j = 0; j < N; ++j) {
    uint64_t b = blocks[j], pg = b >> 6;
    int off = static_cast<int>(b & 63);
    Way* base = &tbl[(pg % SETS) * WAYS];
    Way* w = nullptr; Way* lru = base;
    for (int k = 0; k < WAYS; ++k) {
      if (base[k].valid && base[k].tag == pg) { w = &base[k]; break; }
      if (!base[k].valid || base[k].lru < lru->lru) lru = &base[k];
    }
    bool fresh = (w == nullptr);
    if (fresh) { w = lru; *w = Way{}; w->valid = true; w->tag = pg; }
    int cat;
    if (fresh) cat = COLD;
    else {
      int d = off - w->last_off;
      if ((w->amap >> off) & 1) cat = REVISIT;
      else if (d == w->last_delta) cat = STRIDE;
      else if (dhas(*w, d)) cat = RECUR;
      else cat = NOVEL;
      w->last_delta = d; dset(*w, d);
    }
    ++tot[cat]; if (covered[j]) ++cov[cat];
    w->amap |= (uint64_t{1} << off); w->last_off = off; w->lru = ++clk;
  }
  long miss_tot = 0; for (int c = 0; c < NCAT; ++c) miss_tot += tot[c] - cov[c];
  std::printf("\n=== page-gridded (%dx%d residency): %s  (coverage=%.3f) ===\n", SETS, WAYS, name.c_str(),
              1.0 - static_cast<double>(miss_tot) / static_cast<double>(N));
  std::printf("%-12s %11s %7s %9s %12s %8s\n", "local class", "demands", "%dem", "covered%", "misses", "%ofmiss");
  for (int c = 0; c < NCAT; ++c) {
    long miss = tot[c] - cov[c];
    std::printf("%-12s %11ld %6.1f%% %8.1f%% %12ld %7.1f%%\n", nm[c], tot[c], 100.0 * tot[c] / N,
                tot[c] ? 100.0 * cov[c] / tot[c] : 0.0, miss, miss_tot ? 100.0 * miss / miss_tot : 0.0);
  }
}

// Pattern-level (not isolated-miss) coverage analysis. A "pattern" = one page instance
// (a residency in the SPPAM-geometry region table). For each miss we ask: WHERE in the
// instance does it fall (1st touch = unavoidable cold/warmup; early = limited context;
// steady-state = we had context and still missed), and HOW covered is its instance overall
// (0% = pattern never grabbed; high% = miss is the tail/gap of a pattern we DID grab).
// This separates "uncovered fraction of a partially-grabbed pattern" from "independent miss".
void pattern_coverage_analysis(const std::vector<uint64_t>& blocks, const std::vector<uint8_t>& covered,
                               const std::string& name, int SETS, int WAYS, int ps)
{
  const uint32_t N = static_cast<uint32_t>(blocks.size());
  std::vector<uint32_t> inst_id(N), inst_pos(N);
  std::vector<uint32_t> iacc, icov; // per-instance access / covered counts
  iacc.reserve(N / 4); icov.reserve(N / 4);
  struct Way { bool valid = false; uint64_t tag = 0; uint32_t lru = 0; uint32_t id = 0, pos = 0; };
  std::vector<Way> tbl(static_cast<std::size_t>(SETS * WAYS));
  uint32_t clk = 0, next_id = 0;
  for (uint32_t j = 0; j < N; ++j) {
    uint64_t pg = blocks[j] >> 6;
    Way* base = &tbl[(pg % SETS) * WAYS];
    Way* w = nullptr; Way* lru = base;
    for (int k = 0; k < WAYS; ++k) {
      if (base[k].valid && base[k].tag == pg) { w = &base[k]; break; }
      if (!base[k].valid || base[k].lru < lru->lru) lru = &base[k];
    }
    if (w == nullptr) { w = lru; w->valid = true; w->tag = pg; w->id = next_id++; w->pos = 0; iacc.push_back(0); icov.push_back(0); }
    inst_id[j] = w->id; inst_pos[j] = ++w->pos;
    ++iacc[w->id]; if (covered[j]) ++icov[w->id];
    w->lru = ++clk;
  }
  // Miss buckets: by position-in-instance, and by instance coverage fraction.
  const char* pos_nm[4] = {"pos1(cold)", "early(<=ps)", "mid(<=2ps)", "steady(>2ps)"};
  const char* cov_nm[4] = {"inst 0%", "inst 0-50%", "inst 50-90%", "inst 90-100%"};
  long mp[4] = {0}, mc[4] = {0}, miss = 0;
  for (uint32_t j = 0; j < N; ++j) {
    if (covered[j]) continue;
    ++miss;
    int p = static_cast<int>(inst_pos[j]);
    ++mp[p == 1 ? 0 : p <= ps ? 1 : p <= 2 * ps ? 2 : 3];
    double c = static_cast<double>(icov[inst_id[j]]) / static_cast<double>(iacc[inst_id[j]]);
    ++mc[c == 0.0 ? 0 : c <= 0.5 ? 1 : c <= 0.9 ? 2 : 3];
  }
  std::printf("\n=== pattern-coverage: %s  (misses=%ld, instances=%u) ===\n", name.c_str(), miss, next_id);
  std::printf("  miss position-in-page-instance:\n");
  for (int i = 0; i < 4; ++i) std::printf("    %-14s %10ld  %5.1f%%\n", pos_nm[i], mp[i], miss ? 100.0 * mp[i] / miss : 0);
  std::printf("  miss by its instance's overall coverage:\n");
  for (int i = 0; i < 4; ++i) std::printf("    %-14s %10ld  %5.1f%%\n", cov_nm[i], mc[i], miss ? 100.0 * mc[i] / miss : 0);
}

// Miss-source analysis on REAL L2 misses: decompose the coverage gap into the user's
// three hypothesized sources.
//   (1+2) cross-page: for each COLD miss (first access of a page instance), is there
//         cross-page context a bootstrap could exploit -- a repeating page-stride, the
//         page below/above resident (stream crossing the edge), or isolated (far jump)?
//   (3)   short-lived pages: of the WARMUP misses (cold + early, pos<=ps), what % are
//         PREFETCHes (berti, IP-less -> PC can't help) vs DEMANDs, and how PC-concentrated
//         are the demand misses (top-K IPs cover what fraction -> PC-indexing viable?).
void miss_source_analysis(const std::vector<uint64_t>& blocks, const std::vector<uint8_t>& types,
                          const std::vector<uint64_t>& ips, const std::vector<uint64_t>& vaddrs,
                          const std::vector<uint8_t>& l2res, const std::string& name, int SETS, int WAYS, int ps)
{
  // Region residency stays PHYSICAL (matches SPPAM); cross-page is judged in VIRTUAL space
  // (physical pages are scattered, so a cross-page bootstrap must key on virtual addr+TLB).
  struct Way { bool valid = false; uint64_t tag = 0; uint32_t lru = 0; uint32_t pos = 0; };
  std::vector<Way> tbl(static_cast<std::size_t>(SETS * WAYS));
  uint32_t clk = 0;
  auto find = [&](uint64_t pg) -> Way* {
    Way* base = &tbl[(pg % SETS) * WAYS];
    for (int k = 0; k < WAYS; ++k) if (base[k].valid && base[k].tag == pg) return &base[k];
    return nullptr;
  };
  long cold = 0, vstride = 0, vfwd = 0, vbwd = 0, vnear = 0, vfar = 0;
  long warm = 0, warm_pf = 0, warm_dem = 0;
  std::unordered_map<uint64_t, long> dem_ip;
  uint64_t prev_vp = ~uint64_t{0}; int64_t last_vd = 0;
  const uint32_t N = static_cast<uint32_t>(blocks.size());
  for (uint32_t j = 0; j < N; ++j) {
    uint64_t pg = blocks[j] >> 6, vp = vaddrs[j] >> 12;
    bool isMiss = !l2res[j];
    Way* w = find(pg);
    bool cold_inst = (w == nullptr);
    int64_t vd = (prev_vp == ~uint64_t{0}) ? 0 : static_cast<int64_t>(vp) - static_cast<int64_t>(prev_vp);
    if (isMiss && cold_inst) {
      ++cold;
      // Cross-page classification by VIRTUAL page transition from the previous access.
      if (vd != 0 && vd == last_vd) ++vstride;            // repeating virtual page-stride (2D/streaming)
      else if (vd == 1) ++vfwd;                           // forward to the next virtual page (stream edge)
      else if (vd == -1) ++vbwd;                          // backward to the previous virtual page
      else if (vd >= -4 && vd <= 4) ++vnear;              // small virtual jump (near-stream)
      else ++vfar;                                        // far virtual jump (pointer-chase / unrelated)
    }
    if (cold_inst) {
      Way* base = &tbl[(pg % SETS) * WAYS]; Way* lru = base;
      for (int k = 0; k < WAYS; ++k) if (!base[k].valid || base[k].lru < lru->lru) lru = &base[k];
      w = lru; *w = Way{}; w->valid = true; w->tag = pg;
    }
    uint32_t pos = ++w->pos; w->lru = ++clk;
    if (isMiss && pos <= static_cast<uint32_t>(ps)) {
      ++warm;
      if (static_cast<atype>(types[j]) == atype::PREFETCH) ++warm_pf;
      else { ++warm_dem; ++dem_ip[ips[j]]; }
    }
    if (vp != prev_vp) { last_vd = vd; prev_vp = vp; }
  }
  std::vector<long> cnt; cnt.reserve(dem_ip.size());
  for (auto& kv : dem_ip) cnt.push_back(kv.second);
  std::sort(cnt.begin(), cnt.end(), std::greater<long>());
  auto topk = [&](std::size_t k) { long s = 0; for (std::size_t i = 0; i < k && i < cnt.size(); ++i) s += cnt[i]; return s; };
  auto pc = [](long a, long b) { return b ? 100.0 * a / b : 0.0; };
  std::printf("\n=== miss-source: %s ===\n", name.c_str());
  std::printf("  (1+2) cross-page potential (VIRTUAL), of %ld COLD misses:\n", cold);
  std::printf("    v_stride      %9ld  %5.1f%%  (repeating virtual page-stride)\n", vstride, pc(vstride, cold));
  std::printf("    v_fwd (+1)    %9ld  %5.1f%%  (next virtual page -> stream edge)\n", vfwd, pc(vfwd, cold));
  std::printf("    v_bwd (-1)    %9ld  %5.1f%%\n", vbwd, pc(vbwd, cold));
  std::printf("    v_near (<=4)  %9ld  %5.1f%%\n", vnear, pc(vnear, cold));
  std::printf("    v_far         %9ld  %5.1f%%  (no cross-page handle)\n", vfar, pc(vfar, cold));
  std::printf("  (3) WARMUP misses (cold+early) = %ld:  PREFETCH %.1f%%  DEMAND %.1f%%\n", warm, pc(warm_pf, warm), pc(warm_dem, warm));
  std::printf("    demand-miss PC concentration: distinct_IPs=%zu  top64=%.1f%%  top256=%.1f%%  top1024=%.1f%%\n",
              cnt.size(), pc(topk(64), warm_dem), pc(topk(256), warm_dem), pc(topk(1024), warm_dem));
}

// Per-PC delta regularity of the DEMAND L2 misses. Per PC, track the last virtual block
// and the deltas it has produced. Classify each demand miss:
//   stride  - delta == this PC's previous delta (a basic IP-stride catches it)
//   recur   - delta seen before for this PC (a small per-PC delta table catches it)
//   novel   - delta never seen for this PC (irregular -> needs PC-localized temporal)
//   first   - first access by this PC (warmup)
// Also bucket misses by the PC's distinct-delta count -> is the per-PC delta space tiny
// (pure stride / few) or diverse (irregular)?  Answers: "local delta" enough vs temporal.
void pc_delta_analysis(const std::vector<uint8_t>& types, const std::vector<uint64_t>& ips,
                       const std::vector<uint64_t>& vaddrs, const std::vector<uint8_t>& l2res, const std::string& name)
{
  struct PCS { int64_t last_vb = INT64_MIN; int64_t last_delta = 0; std::unordered_set<int64_t> deltas; bool overflow = false; long miss = 0; };
  std::unordered_map<uint64_t, PCS> pcs;
  pcs.reserve(1 << 16);
  enum { FIRST, STRIDE, RECUR, NOVEL, NC };
  long md = 0, cls[NC] = {0};
  const uint32_t N = static_cast<uint32_t>(vaddrs.size());
  for (uint32_t j = 0; j < N; ++j) {
    auto t = static_cast<atype>(types[j]);
    if (t != atype::LOAD && t != atype::RFO) continue; // demands only
    PCS& st = pcs[ips[j]];
    int64_t vb = static_cast<int64_t>(vaddrs[j] >> 6); // block-granularity per-PC delta
    bool first = (st.last_vb == INT64_MIN);
    int64_t d = first ? 0 : vb - st.last_vb;
    int c = first ? FIRST : (d == st.last_delta ? STRIDE : (st.deltas.count(d) ? RECUR : NOVEL));
    if (!l2res[j]) { ++md; ++cls[c]; ++st.miss; }
    if (!first) { st.last_delta = d; if (st.deltas.size() < 16) st.deltas.insert(d); else st.overflow = true; }
    st.last_vb = vb;
  }
  long dv[4] = {0}; // misses from PCs with: 1 delta / <=4 / <=16 / diverse(>16)
  for (auto& kv : pcs) {
    if (!kv.second.miss) continue;
    int nd = kv.second.overflow ? 17 : static_cast<int>(kv.second.deltas.size());
    dv[nd <= 1 ? 0 : nd <= 4 ? 1 : nd <= 16 ? 2 : 3] += kv.second.miss;
  }
  auto pc = [](long a, long b) { return b ? 100.0 * a / b : 0.0; };
  std::printf("\n=== per-PC delta: %s  (demand L2 misses=%ld) ===\n", name.c_str(), md);
  std::printf("  miss class:  stride %.1f%%  recur %.1f%%  novel %.1f%%  first %.1f%%   => PC-delta covers %.1f%%\n",
              pc(cls[STRIDE], md), pc(cls[RECUR], md), pc(cls[NOVEL], md), pc(cls[FIRST], md), pc(cls[STRIDE] + cls[RECUR], md));
  std::printf("  misses by PC's distinct-delta count:  1(pure-stride) %.1f%%  <=4 %.1f%%  <=16 %.1f%%  diverse %.1f%%\n",
              pc(dv[0], md), pc(dv[1], md), pc(dv[2], md), pc(dv[3], md));
}

// Per-PC polynomial-order test of demand L2 misses. A degree-m polynomial sequence has a
// constant m-th finite difference, so Newton forward-difference extrapolation from the
// last m+1 points predicts the next exactly. For each PC we keep its recent BYTE virtual
// addresses and find the minimal order 1..5 whose extrapolation exactly hits the miss
// (order1=constant stride, order2=quadratic/2D, ...). Answers: are the "irregular" misses
// actually low-order structured (their derivatives converge) or genuinely random?
void pc_poly_analysis(const std::vector<uint8_t>& types, const std::vector<uint64_t>& ips,
                      const std::vector<uint64_t>& vaddrs, const std::vector<uint8_t>& l2res, const std::string& name)
{
  static const int D = 5;
  // coef[m][k] = (-1)^k * C(m+1, k+1): extrapolate a degree-m poly from x[n-1-k].
  static const int64_t coef[6][6] = {
      {0, 0, 0, 0, 0, 0}, {2, -1, 0, 0, 0, 0}, {3, -3, 1, 0, 0, 0},
      {4, -6, 4, -1, 0, 0}, {5, -10, 10, -5, 1, 0}, {6, -15, 20, -15, 6, -1}};
  struct PCS { int64_t h[6] = {0, 0, 0, 0, 0, 0}; int cnt = 0; };
  std::unordered_map<uint64_t, PCS> pcs; pcs.reserve(1 << 16);
  long md = 0, ord[D + 1] = {0}; // ord[0]=none(>5/irregular), ord[1..5]=min order
  const uint32_t N = static_cast<uint32_t>(vaddrs.size());
  for (uint32_t j = 0; j < N; ++j) {
    auto t = static_cast<atype>(types[j]);
    if (t != atype::LOAD && t != atype::RFO) continue;
    PCS& st = pcs[ips[j]];
    int64_t x = static_cast<int64_t>(vaddrs[j]);
    if (!l2res[j]) {
      ++md;
      int found = 0;
      for (int m = 1; m <= D && m <= st.cnt; ++m) {
        int64_t pred = 0;
        for (int k = 0; k <= m; ++k) pred += coef[m][k] * st.h[k];
        if (pred == x) { found = m; break; }
      }
      ++ord[found];
    }
    for (int k = 5; k > 0; --k) st.h[k] = st.h[k - 1];
    st.h[0] = x; if (st.cnt < 6) ++st.cnt;
  }
  auto pct = [&](long a) { return md ? 100.0 * a / md : 0.0; };
  std::printf("\n=== per-PC polynomial order: %s  (demand L2 misses=%ld) ===\n", name.c_str(), md);
  std::printf("  o1(stride) %.1f%%  o2 %.1f%%  o3 %.1f%%  o4 %.1f%%  o5 %.1f%%  none(>5/irreg) %.1f%%\n",
              pct(ord[1]), pct(ord[2]), pct(ord[3]), pct(ord[4]), pct(ord[5]), pct(ord[0]));
  long cum = 0; std::printf("  cumulative poly-predictable:");
  for (int m = 1; m <= D; ++m) { cum += ord[m]; std::printf("  <=o%d %.1f%%", m, pct(cum)); }
  std::printf("\n");
}

int main(int argc, char** argv)
{
  std::string trace_path, cfg_path;
  uint32_t W = 256;
  uint64_t max_records = 0;
  bool miss_mode = false, page_mode = false, pattern_mode = false, source_mode = false, pcdelta_mode = false, pcpoly_mode = false, l2_mode = false, compare_mode = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
    if (a == "--trace") trace_path = next();
    else if (a == "--configs") cfg_path = next();
    else if (a == "--horizon") W = static_cast<uint32_t>(std::stoul(next()));
    else if (a == "--max-records") max_records = std::stoull(next());
    else if (a == "--miss-analysis") miss_mode = true;
    else if (a == "--page-analysis") miss_mode = true, page_mode = true;
    else if (a == "--pattern-analysis") miss_mode = true, pattern_mode = true;
    else if (a == "--miss-source") miss_mode = true, source_mode = true;
    else if (a == "--pc-delta") miss_mode = true, pcdelta_mode = true;
    else if (a == "--pc-poly") miss_mode = true, pcpoly_mode = true;
    else if (a == "--compare") compare_mode = true;
    else if (a == "--l2") l2_mode = true;
    else { std::cerr << "unknown arg: " << a << "\n"; return 2; }
  }
  if (trace_path.empty()) { std::cerr << "need --trace\n"; return 2; }

  // 1) Read the FULL L2C access stream once into memory -- demands AND berti's
  // prefetches (and writes/translations): drop NOTHING. The predictor must see the
  // complete stream it would in the real L2C, not the sparse post-berti residual.
  std::vector<uint64_t> blocks, ips, vaddrs;
  std::vector<uint8_t> types;
  {
    trace_reader tr(trace_path);
    access_record rec;
    while (tr.next(rec)) {
      auto t = static_cast<atype>(rec.type);
      if (t != atype::LOAD && t != atype::RFO && t != atype::PREFETCH)
        continue; // ALL loads, ALL RFOs, ALL prefetches (berti's included); skip WRITE/TRANSLATION
      blocks.push_back(rec.paddr >> 6);
      ips.push_back(rec.ip);
      types.push_back(rec.type);
      vaddrs.push_back(rec.vaddr); // full virtual addr (cross-page, per-PC delta, polynomial-order all virtual)
      if (max_records && blocks.size() >= max_records)
        break;
    }
  }
  const uint32_t N = static_cast<uint32_t>(blocks.size());
  // 2) Build block -> sorted future indices (for the within-W oracle).
  std::unordered_map<uint64_t, std::vector<uint32_t>> idx;
  idx.reserve(N);
  for (uint32_t i = 0; i < N; ++i)
    idx[blocks[i]].push_back(i);
  std::cerr << "demands=" << N << " unique_blocks=" << idx.size() << " horizon=" << W << "\n";

  // Does block b recur in (i, i+W]?  (idx[b] is ascending.)
  auto hit_soon = [&](uint64_t b, uint32_t i) -> int {
    auto it = idx.find(b);
    if (it == idx.end()) return -1;
    const auto& v = it->second;
    auto p = std::upper_bound(v.begin(), v.end(), i); // first index > i
    if (p != v.end() && *p <= i + W) return static_cast<int>(*p);
    return -1;
  };

  // 3) Score each config.
  nlohmann::json cfgs;
  if (cfg_path.empty()) cfgs = nlohmann::json::array({nlohmann::json::object({{"name", "CENTER"}})});
  else { std::ifstream f(cfg_path); f >> cfgs; }

  // --compare: full berti_plus stream. baseline = demand-only (no prefetcher); berti =
  // its PREFETCH records fill the L2 and COUNT toward coverage+accuracy; then each L2
  // prefetcher (next-line / SPPAM / SPP / hybrid) adds ON TOP of berti. coverage vs the
  // no-prefetcher baseline; accuracy = useful/issued counting berti's prefetches too.
  long baseline_dmiss = 0;
  if (compare_mode) {
    // nopf = no prefetcher (only demands fill -> the coverage denominator M0).
    // berti = berti's prefetch requests are real accesses that fill L2 (its coverage).
    // berti+nextline/+next4 = an L2 next-line prefetcher ON TOP, triggered on every access.
    L2Acc nopf(2048, 16), bert(2048, 16), bnl(2048, 16), bn4(2048, 16);
    for (uint32_t i = 0; i < N; ++i) {
      uint64_t b = blocks[i];
      bool dem = static_cast<atype>(types[i]) != atype::PREFETCH; // LOAD/RFO vs berti's PREFETCH
      if (dem) nopf.access(b, true);
      bert.access(b, dem);
      bnl.access(b, dem); bnl.prefetch(b + 1);
      bn4.access(b, dem); for (int k = 1; k <= 4; ++k) bn4.prefetch(b + k);
    }
    baseline_dmiss = nopf.dmiss;
    auto cov = [&](const L2Acc& m) { return 100.0 * (nopf.dmiss - m.dmiss) / nopf.dmiss; };
    std::printf("=== compare (berti_plus): no-pf baseline demand misses=%ld ===\n", nopf.dmiss);
    std::printf("%-16s %9s %9s %9s\n", "system", "coverage", "L2pf_acc", "L2pf/dem");
    std::printf("%-16s %8.1f%% %9s %9s\n", "berti", cov(bert), "-", "-"); // berti acc not measurable at L2
    std::printf("%-16s %8.1f%% %8.1f%% %9.3f\n", "berti+nextline", cov(bnl), bnl.accuracy(), (double)bnl.issued / nopf.dmiss);
    std::printf("%-16s %8.1f%% %8.1f%% %9.3f\n", "berti+next4", cov(bn4), bn4.accuracy(), (double)bn4.issued / nopf.dmiss);
  } else
    std::printf("%-22s %9s %9s %9s %9s %9s %8s\n", "config", "acc_all", "cov_all", "acc_sppam", "acc_spp", "pf/dem", "preds");
  for (auto& obj : cfgs) {
    params P;
    apply_json(P, obj);
    oracle_sink sink;
    sink.fallthrough = P.enable_fallthrough;
    sink.ft_div = P.fallthrough_explore_div;
    sppam_predictor pred(P, &sink);
    std::unique_ptr<spp_predictor> spp;
    if (P.enable_spp || P.speculative_lookahead) spp = std::make_unique<spp_predictor>(P, &sink);
    if (spp) pred.set_spp(spp.get());

    uint64_t issued[2] = {0, 0}, correct[2] = {0, 0};
    std::vector<uint8_t> covered(N, 0);
    L2Model l2(2048, 16);                              // L2 residency model (--l2)
    std::vector<uint8_t> l2_resident(l2_mode ? N : 0, 0);
    L2Acc bpred(2048, 16); // --compare: berti's prefetches + this predictor's, on the L2

    for (uint32_t i = 0; i < N; ++i) {
      const uint64_t b = blocks[i];
      bool is_pf = (static_cast<atype>(types[i]) == atype::PREFETCH);
      if (compare_mode) bpred.access(b, !is_pf); // real L2 access (demand or berti) BEFORE this access's L2pf prefetches
      if (l2_mode) l2_resident[i] = l2.contains(b) ? 1 : 0; // hit BEFORE this access fills
      // Positive feedback: this demand uses a block predicted within the last W.
      // NO feedback: the dumb predictor just predicts (set prediction_only in the config
      // to also strip the degree/usefulness/bw throttle). We score the raw prediction set.
      sink.sppam_fired = false;
      sink.preds.clear();
      // Speculative mode: advance SPP's signature (and let it issue) BEFORE SPPAM, so
      // SPPAM's speculate() reads the current signature. Otherwise SPP runs after.
      if (spp && P.speculative_lookahead) spp->update(b); // SPP silent: feeds SPPAM's lookahead only
      pred.operate(b, ips[i], /*cache_hit=*/false, /*useful_prefetch=*/false, static_cast<atype>(types[i]), i);
      if (spp && !P.speculative_lookahead) spp->operate(b);
      for (auto& [pb, eng] : sink.preds) {
        ++issued[eng];
        int j = hit_soon(pb, i);
        if (j >= 0) { ++correct[eng]; covered[static_cast<uint32_t>(j)] = 1; }
        if (l2_mode) l2.fill(pb); // prefetch fill (so future demands can hit on it)
        if (compare_mode) bpred.prefetch(pb); // this predictor's prefetch
      }
      if (l2_mode) l2.fill(b); // demand fill
    }
    if (compare_mode) {
      std::string nm = obj.value("name", std::string("cfg"));
      double covr = baseline_dmiss ? 100.0 * (baseline_dmiss - bpred.dmiss) / baseline_dmiss : 0.0;
      std::printf("%-16s %8.1f%% %8.1f%% %9.3f\n", ("berti+" + nm).c_str(), covr, bpred.accuracy(), (double)bpred.issued / baseline_dmiss);
      continue;
    }
    if (miss_mode) {
      std::string nm = obj.value("name", std::string("cfg"));
      // --l2: a "miss" = block not L2-resident (real opportunity); else = horizon-uncovered.
      const std::vector<uint8_t>& acov = l2_mode ? l2_resident : covered;
      if (pcpoly_mode) pc_poly_analysis(types, ips, vaddrs, acov, nm);
      else if (pcdelta_mode) pc_delta_analysis(types, ips, vaddrs, acov, nm);
      else if (source_mode) miss_source_analysis(blocks, types, ips, vaddrs, acov, nm, static_cast<int>(P.region_sets), static_cast<int>(P.region_ways), static_cast<int>(P.pattern_size));
      else if (pattern_mode) pattern_coverage_analysis(blocks, acov, nm, static_cast<int>(P.region_sets), static_cast<int>(P.region_ways), static_cast<int>(P.pattern_size));
      else if (page_mode) page_miss_analysis(blocks, acov, nm, static_cast<int>(P.region_sets), static_cast<int>(P.region_ways));
      else miss_analysis(blocks, acov, nm);
      continue;
    }
    uint64_t tot_iss = issued[0] + issued[1], tot_cor = correct[0] + correct[1];
    uint64_t covd = 0;
    for (auto c : covered) covd += c;
    auto rate = [](uint64_t a, uint64_t b) { return b ? static_cast<double>(a) / static_cast<double>(b) : 0.0; };
    std::string name = obj.value("name", std::string("cfg"));
    std::printf("%-22s %9.4f %9.4f %9.4f %9.4f %9.3f %8llu\n", name.c_str(), rate(tot_cor, tot_iss), rate(covd, N),
                rate(correct[0], issued[0]), rate(correct[1], issued[1]), rate(tot_iss, N), (unsigned long long)tot_iss);
  }
  return 0;
}
