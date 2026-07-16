// Per-address deep dive: run AMPM and SPPAM (promoted default: single-path+PC, FORWARD-ONLY) in parallel over a
// trace; find demand blocks the BASELINE missed that AMPM covered but SPPAM did NOT, and dump each engine's
// prediction stage relative to that block -- to show concretely WHY (AMPM's bidirectional stride vs SPPAM's
// forward-only PHT). Uses the same cache/predictor code as the DSE.
#include "cache.h"
#include "params.h"
#include "prefetch_sink.h"
#include "sppam_predictor.h"
#include "trace_reader.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
using namespace sppam_dse;

static const int BPP = 64, PAGE_SH = 6;

// SPPAM sink: install prefetches into sppam_l2, notify predictor on evict/fill (mirrors evaluator.h).
struct Sink : prefetch_sink {
  cache* l2 = nullptr; sppam_predictor* pred = nullptr; uint64_t cyc = 0;
  bool issue_prefetch(uint64_t block, bool /*fill_l2*/, bool /*from_spp*/, double /*b*/, uint32_t /*g*/) override {
    if (l2->resident(block)) return false;
    auto r = l2->install(block, cyc, cyc, true);
    if (r.evicted_valid) pred->on_l2_evict(r.evicted_block, cyc, r.evicted_unused_prefetch);
    pred->shadow_fill(block);
    return true;
  }
};

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: %s <trace.zst> [maxrec] [ndumps]\n", argv[0]); return 2; }
  std::string path = argv[1];
  uint64_t maxrec = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 8000000ull;
  int ndumps = argc > 3 ? std::atoi(argv[3]) : 6;

  // SPPAM = promoted default: single-path(scan=1) + PC, forward-only, min_conf=4, region 768.
  params P; P.name = "sppam"; P.scan_distance_forward = 1; P.pattern_pc_bits = 4; P.pattern_table_sets = 1024; P.pattern_table_ways = 2;
  P.enable_shadow_squash = true; P.enable_timing = false;
  cache base(2048, 16), sppam_l2(2048, 16), ampm_l2(2048, 16);
  Sink sink; sink.l2 = &sppam_l2;
  sppam_predictor pred(P, &sink); sink.pred = &pred;

  // AMPM state (per-page access/prefetch maps, set-assoc; sized to match SPPAM's ~region budget).
  struct AR { bool v = false; uint64_t pg = 0, lru = 0; std::vector<char> a, p; };
  const int AS = 128, AW = 12; std::vector<AR> art((size_t)AS * AW); uint64_t aclk = 0;
  for (auto& r : art) { r.a.assign(BPP, 0); r.p.assign(BPP, 0); }
  auto afind = [&](uint64_t pg) -> AR* { size_t b = (size_t)(pg % AS) * AW; for (int w = 0; w < AW; ++w) { AR& r = art[b + w]; if (r.v && r.pg == pg) { r.lru = ++aclk; return &r; } } return nullptr; };
  auto aalloc = [&](uint64_t pg) -> AR* { size_t b = (size_t)(pg % AS) * AW, vic = b; for (int w = 0; w < AW; ++w) { size_t i = b + w; if (!art[i].v || art[i].lru < art[vic].lru) vic = i; } AR& r = art[vic]; r.v = true; r.pg = pg; r.lru = ++aclk; std::fill(r.a.begin(), r.a.end(), 0); std::fill(r.p.begin(), r.p.end(), 0); return &r; };
  auto aset = [&](uint64_t blk, bool pf) { uint64_t pg = blk >> PAGE_SH; int o = (int)(blk & (BPP - 1)); AR* r = afind(pg); if (!r) r = aalloc(pg); (pf ? r->p : r->a)[o] = 1; };
  auto aget = [&](uint64_t blk, bool pf) { uint64_t pg = blk >> PAGE_SH; int o = (int)(blk & (BPP - 1)); AR* r = afind(pg); return r && (pf ? r->p : r->a)[o]; };
  auto aclear = [&](uint64_t blk) { uint64_t pg = blk >> PAGE_SH; int o = (int)(blk & (BPP - 1)); AR* r = afind(pg); if (r) { r->a[o] = 0; r->p[o] = 0; } };

  struct PfInfo { uint64_t trigger; int stride; int dir; }; // which AMPM access+stride prefetched a block
  std::unordered_map<uint64_t, PfInfo> amp_pf;

  trace_reader tr(path);
  access_record rec; uint64_t total = 0, ops = 0; int dumped = 0;
  while (tr.next(rec)) {
    if (maxrec && ++total > maxrec) break;
    if (!rec.is_translated) continue;
    atype t = (atype)rec.type;
    if (t != atype::LOAD && t != atype::RFO && t != atype::PREFETCH) continue;
    uint64_t blk = rec.paddr >> 6; sink.cyc = ++ops;

    // coverage state BEFORE this demand mutates the caches
    bool bmiss = !base.resident(blk), amiss = !ampm_l2.resident(blk), smiss = !sppam_l2.resident(blk);

    // TARGET: baseline missed, AMPM covered (had it, from a prefetch), SPPAM did NOT -> instrument it.
    if (bmiss && !amiss && smiss && amp_pf.count(blk) && dumped < ndumps) {
      const auto& pi = amp_pf[blk];
      std::printf("\n===== block 0x%llx (page 0x%llx off %d): AMPM-COVERED, SPPAM-MISSED, baseline-miss =====\n",
                  (unsigned long long)blk, (unsigned long long)(blk >> PAGE_SH), (int)(blk & (BPP - 1)));
      std::printf("[AMPM] prefetched from trigger 0x%llx  stride=%+d  dir=%+d  (i.e. saw off-%d & off-%d accessed, predicted off %+d)\n",
                  (unsigned long long)pi.trigger, pi.stride, pi.dir, pi.stride, 2 * pi.stride, pi.stride);
      std::printf("[SPPAM] explain(block) -- its region access-map + per-trigger FORWARD/BACKWARD predictions:\n%s",
                  pred.explain(blk).c_str());
      ++dumped;
    }

    // ---- advance BASELINE ----
    { bool up = false; if (!base.demand_access(blk, sink.cyc, up)) base.install(blk, sink.cyc, sink.cyc, false); }

    // ---- advance SPPAM (demand + operate issues prefetches via the sink) ----
    { const cache_line* pl = sppam_l2.probe(blk); bool cache_hit = (pl != nullptr); bool up = false;
      if (cache_hit) sppam_l2.demand_access(blk, sink.cyc, up);
      else { auto r = sppam_l2.install(blk, sink.cyc, sink.cyc, false); if (r.evicted_valid) pred.on_l2_evict(r.evicted_block, sink.cyc, r.evicted_unused_prefetch); pred.shadow_fill(blk); }
      pred.operate(blk, rec.ip, cache_hit, up, up, t, sink.cyc); }

    // ---- advance AMPM (demand + bidirectional stride prefetch) ----
    { bool up = false; if (!ampm_l2.demand_access(blk, sink.cyc, up)) { auto r = ampm_l2.install(blk, sink.cyc, sink.cyc, false); if (r.evicted_valid) aclear(r.evicted_block); } }
    aset(blk, false);
    uint64_t pg = blk >> PAGE_SH;
    for (int dir : {1, -1}) for (int i = 1, pfc = 0; pfc < 2; ++i) {
      uint64_t pos = blk + (uint64_t)(dir * i), neg = blk - (uint64_t)(dir * i), neg2 = blk - (uint64_t)(dir * 2 * i);
      if ((pos >> PAGE_SH) != pg) break;
      if (aget(neg, false) && aget(neg2, false) && !aget(pos, false) && !aget(pos, true) && pos != blk) {
        if (!ampm_l2.resident(pos)) { auto r = ampm_l2.install(pos, sink.cyc, sink.cyc, true); if (r.evicted_valid) aclear(r.evicted_block); }
        aset(pos, true); amp_pf[pos] = PfInfo{blk, i, dir}; ++pfc;
      }
    }
  }
  std::printf("\n(dumped %d cases over %llu records)\n", dumped, (unsigned long long)total);
  return 0;
}
