// Instruction-spatial FALLTHROUGH comparison. Same holistic shared-L2 setup as holistic_id (real SPPAM on
// data + real branch graph on instructions, sharing ONE residency filter = SPPAM's region prefetch_map), but
// adds a LIGHTWEIGHT, DEDICATED instruction-spatial fallthrough that captures the +2-7% spatial code residual
// WITHOUT dragging the full data SPPAM onto the instruction stream (which cost data coverage + bandwidth).
//
// Fallthroughs (each private tables, share only the residency filter via filter_probe/filter_mark):
//   next-N : prefetch phys+1..phys+N within the code page (extends the BG's next-line).
//   BOP    : best-offset -- learn the dominant code stride, prefetch phys+best_offset.
//   tsppam : tiny SPPAM in PHYSICAL code-page space -- small transient region access-map + a PHT keyed by the
//            spatial window signature (generalizes footprints across pages; no big per-page SMS map).
//   feed   : full data SPPAM ALSO predicts on instructions (the upper-bound reference; costs data coverage).
#include "cache.h"
#include "iprefetch_predictor.h"
#include "params.h"
#include "prefetch_sink.h"
#include "sppam_predictor.h"
#include "trace_reader.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
using namespace sppam_dse;

static uint64_t g_cyc = 0;
static inline uint64_t Hh(uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; return x; }
enum FT { FT_NONE, FT_NEXTN, FT_BOP, FT_TSPPAM, FT_FEED };

// --- BOP: dominant-stride learner over recent code blocks (RR) + round-robin offset scoring ---
struct Bop {
  static constexpr int NOFF = 16;
  int offs[NOFF] = {1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, 18, 20, 24, 32};
  int score[NOFF] = {0};
  std::vector<uint64_t> rr; uint64_t rrmask;
  int cur = 0, round = 0, best = 0; bool active = false;
  static constexpr int SCOREMAX = 15, ROUNDMAX = 100, BADSCORE = 2;
  explicit Bop(int rrbits) : rr(std::size_t{1} << rrbits, ~uint64_t{0}), rrmask((uint64_t{1} << rrbits) - 1) {}
  bool in_rr(uint64_t b) const { return rr[Hh(b) & rrmask] == b; }
  void ins_rr(uint64_t b) { rr[Hh(b) & rrmask] = b; }
  void finalize() { int bi = 0; for (int i = 1; i < NOFF; i++) if (score[i] > score[bi]) bi = i;
    active = score[bi] >= BADSCORE; best = offs[bi]; for (int i = 0; i < NOFF; i++) score[i] = 0; cur = 0; round = 0; }
  void learn(uint64_t X) { int d = offs[cur];
    if (in_rr(X - (uint64_t)d)) { if (++score[cur] >= SCOREMAX) { finalize(); ins_rr(X); return; } }
    if (++cur >= NOFF) { cur = 0; if (++round >= ROUNDMAX) finalize(); }
    ins_rr(X); }
  int offset() const { return active ? best : 0; }
  double kib() const { return (rr.size() * 8 + NOFF * 4 + 32) / 1024.0; }
};

// --- tiny SPPAM (physical code-page space): transient region access-map + spatial-signature PHT ---
static constexpr int PS = 8, BPP = 64;
static inline uint64_t win_at(uint64_t amap, int p) { uint64_t w = 0; for (int i = 1; i <= PS; ++i) { int q = p + i; w = (w << 1) | ((q >= 0 && q < BPP) ? ((amap >> q) & 1) : 0); } return w; }
struct TinySppam {
  struct R { uint64_t pg = 0, amap = 0; uint32_t lru = 0; bool v = false; };
  int sets, ways; std::vector<R> rt; uint32_t clk = 0;
  std::unordered_map<uint64_t, std::array<uint16_t, PS>> pht; std::unordered_map<uint64_t, uint32_t> occ;
  double conf_min;
  TinySppam(int s, int w, double cm) : sets(s), ways(w), rt((std::size_t)s * w), conf_min(cm) {}
  R& get(uint64_t pg) { std::size_t base = (std::size_t)(pg % sets) * ways; int hit = -1, lw = 0;
    for (int k = 0; k < ways; k++) { if (rt[base + k].v && rt[base + k].pg == pg) hit = k; if (!rt[base + k].v || rt[base + k].lru < rt[base + lw].lru) lw = k; }
    if (hit >= 0) { rt[base + hit].lru = ++clk; return rt[base + hit]; }
    R& e = rt[base + lw]; e = R{}; e.pg = pg; e.v = true; e.lru = ++clk; return e; }
  void train(uint64_t key, int bit) { auto& c = pht[key]; ++occ[key]; if (c[bit] < 65535) ++c[bit]; }
  // predict offsets ahead of `off` from the region's window; calls issue(ppage<<6 | q).
  template <class Issue> void predict(R& r, uint64_t pg, int off, const Issue& issue) {
    uint64_t w = win_at(r.amap, off); auto it = pht.find(w); if (it == pht.end()) return;
    uint32_t o = occ[w]; if (!o) return;
    for (int b = 0; b < PS; ++b) { if (!it->second[b]) continue; if ((double)it->second[b] / o < conf_min) continue;
      int q = off + (PS - b); if (q <= off || q >= BPP) continue; issue((pg << 6) | (uint64_t)q); }
  }
  // online update: train on prior map (window at off-d predicts off), then mark this access.
  template <class Issue> void access(uint64_t pg, int off, const Issue& issue) {
    R& r = get(pg);
    for (int d = 1; d <= PS; ++d) { int p = off - d; uint64_t w = win_at(r.amap, p); train(w, PS - d); }
    predict(r, pg, off, issue);
    r.amap |= (uint64_t{1} << off);
  }
  double kib() const { return ((double)sets * ways * (36 + 64) + pht.size() * (24 + PS * 2 + 4)) / 8.0 / 1024.0; }
};

struct World {
  const char* name; FT ft = FT_NONE; int nextn = 0;
  cache l2{2048, 16};
  sppam_predictor* sp = nullptr; iprefetch_predictor* bg = nullptr;
  Bop* bop = nullptr; TinySppam* ts = nullptr;
  uint64_t dmiss = 0, imiss = 0, useful = 0, useless = 0, dpf = 0, ipf = 0;
  void evict_note(const install_result& r) { if (!r.evicted_valid) return; if (r.evicted_unused_prefetch) ++useless;
    if (sp) sp->on_l2_evict(r.evicted_block, g_cyc, r.evicted_unused_prefetch); }
  bool install_pf(uint64_t blk) { if (l2.resident(blk)) return false; auto r = l2.install(blk, g_cyc, g_cyc, true); evict_note(r); return true; }
  void demand(uint64_t blk, bool is_instr) { bool up = false; if (l2.demand_access(blk, g_cyc, up)) { if (up) ++useful; return; }
    if (is_instr) ++imiss; else ++dmiss; auto r = l2.install(blk, g_cyc, g_cyc, false); evict_note(r); }
  // shared-filter issue for a fallthrough instruction prefetch
  void issue_ft(uint64_t pb) { if (sp->filter_probe(pb)) return; if (install_pf(pb)) { sp->filter_mark(pb); ++ipf; } }
};

struct Sink : prefetch_sink {
  World* w = nullptr;
  bool issue_prefetch(uint64_t block, bool, bool, double, uint32_t) override {
    if (w->l2.resident(block)) return false; if (!w->install_pf(block)) return false; w->sp->shadow_fill(block); ++w->dpf; return true; }
};

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: %s <l1d.zst> <l1i.zst> [maxdata] [nextn]\n", argv[0]); return 2; }
  uint64_t maxdata = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 8000000ull;
  int nextn = argc > 4 ? std::atoi(argv[4]) : 4;

  auto mkP = [&](const char* nm) { params P; P.name = nm; P.enable_ip_filter = true; P.enable_spp = true; P.pattern_validate = true;
    P.pv_feed_confidence = true; P.pv_conf_penalty = 16; P.enable_timing = false; P.enable_shadow_squash = true;
    P.instr_table_entries = 128; P.instr_xlate_entries = 32; return P; };

  // base/data denominators + BG-only (bg) + fallthrough arms + feed (upper bound). All: SPPAM on data + shared filter.
  World w_base, w_data, w_bg, w_nextn, w_bop, w_ts, w_feed;
  w_base.name = "base"; w_data.name = "data";
  w_bg.name = "bg"; w_nextn.name = "next"; w_nextn.ft = FT_NEXTN; w_nextn.nextn = nextn;
  w_bop.name = "bop"; w_bop.ft = FT_BOP; w_ts.name = "tsppam"; w_ts.ft = FT_TSPPAM;
  w_feed.name = "feed"; w_feed.ft = FT_FEED;
  World* worlds[7] = {&w_base, &w_data, &w_bg, &w_nextn, &w_bop, &w_ts, &w_feed};

  // data SPPAM everywhere except base; BG on instructions in every arm except base/data.
  std::vector<Sink*> sinks;
  for (auto* w : worlds) {
    if (w == &w_base) continue;
    Sink* s = new Sink(); s->w = w; sinks.push_back(s);
    w->sp = new sppam_predictor(*new params(mkP(w->name)), s);
    if (w == &w_data) continue;
    params* Pi = new params(mkP("bg")); w->bg = new iprefetch_predictor(*Pi);
    w->bg->set_shared_probe([sp = w->sp](uint64_t b) { return sp->filter_probe(b); });
  }
  w_bop.bop = new Bop(8);                    // 256-entry RR ~2 KiB
  w_ts.ts = new TinySppam(64, 4, 0.30);      // 256-region tiny map + PHT

  auto do_data = [&](World& w, uint64_t blk, const access_record& r) {
    if (!w.sp) { w.demand(blk, false); return; }
    const cache_line* pl = w.l2.probe(blk); bool hit = (pl != nullptr); bool up = false;
    if (hit) { w.l2.demand_access(blk, g_cyc, up); if (up) ++w.useful; }
    else { ++w.dmiss; auto rr = w.l2.install(blk, g_cyc, g_cyc, false); w.evict_note(rr); w.sp->shadow_fill(blk); }
    w.sp->operate(blk, r.ip, hit, up, up, (atype)r.type, g_cyc);
  };
  auto do_instr = [&](World& w, uint64_t blk, const access_record& r) {
    w.demand(blk, true);
    if (!w.bg) return;
    w.sp->filter_mark(blk);                                   // demanded code block resident in the shared filter
    w.bg->operate(r.vaddr >> 6, blk, [&](uint64_t pb) { if (w.install_pf(pb)) { w.sp->filter_mark(pb); ++w.ipf; } });
    const uint64_t pg = blk >> 6; const int off = (int)(blk & 63);
    switch (w.ft) {
      case FT_NEXTN: for (int k = 1; k <= w.nextn; ++k) { uint64_t pb = blk + k; if ((pb >> 6) == pg) w.issue_ft(pb); } break;
      case FT_BOP: { w.bop->learn(blk); int d = w.bop->offset(); if (d) { uint64_t pb = blk + (uint64_t)d; if ((pb >> 6) == pg) w.issue_ft(pb); } } break;
      case FT_TSPPAM: w.ts->access(pg, off, [&](uint64_t pb) { w.issue_ft(pb); }); break;
      case FT_FEED: { bool up = false; const cache_line* pl = w.l2.probe(blk); bool hit = (pl != nullptr);
        w.sp->operate(blk, r.vaddr, hit, up, up, (atype)r.type, g_cyc); } break;
      default: break;
    }
  };

  trace_reader td(argv[1]), ti(argv[2]);
  access_record rd, ri; bool hd = td.next(rd), hi = ti.next(ri);
  uint64_t dseen = 0, iseen = 0;
  while (hd || hi) {
    bool take_data = (hd && hi) ? (rd.cycle <= ri.cycle) : hd;
    ++g_cyc;
    if (take_data) {
      atype t = (atype)rd.type;
      if (rd.is_translated && (t == atype::LOAD || t == atype::RFO || t == atype::PREFETCH) && (rd.ip >> 6) != (rd.vaddr >> 6)) {
        uint64_t blk = rd.paddr >> 6; ++dseen; for (auto* w : worlds) do_data(*w, blk, rd);
      }
      hd = td.next(rd); if (dseen >= maxdata) break;
    } else {
      if (ri.is_translated && !ri.is_prefetch && (ri.ip >> 6) == (ri.vaddr >> 6)) {
        uint64_t blk = ri.paddr >> 6; ++iseen; for (auto* w : worlds) do_instr(*w, blk, ri);
      }
      hi = ti.next(ri);
    }
  }

  auto cov = [](uint64_t base, uint64_t x) { return base ? 100.0 * (1.0 - (double)x / (double)base) : 0.0; };
  auto acc = [](uint64_t u, uint64_t l) { return (u + l) ? 100.0 * u / (u + l) : 0.0; };
  std::printf("data_acc=%llu instr_acc=%llu  base_miss data=%llu instr=%llu  (BOP %.1fKiB, tsppam %.1fKiB)\n",
              (unsigned long long)dseen, (unsigned long long)iseen, (unsigned long long)w_base.dmiss, (unsigned long long)w_base.imiss,
              w_bop.bop->kib(), w_ts.ts->kib());
  std::printf("%-7s %9s %10s %10s %9s %9s %8s\n", "arm", "data_cov", "instr_cov", "total_cov", "acc", "d_pf", "i_pf");
  for (auto* w : worlds)
    std::printf("%-7s %8.2f%% %9.2f%% %9.2f%% %8.1f%% %9.3f %8.3f\n", w->name,
                cov(w_base.dmiss, w->dmiss), cov(w_base.imiss, w->imiss), cov(w_base.dmiss + w_base.imiss, w->dmiss + w->imiss),
                acc(w->useful, w->useless), dseen ? (double)w->dpf / dseen : 0.0, iseen ? (double)w->ipf / iseen : 0.0);
  double bg_i = cov(w_base.imiss, w_bg.imiss), feed_i = cov(w_base.imiss, w_feed.imiss);
  std::printf("\ninstr-cov gain over BG-only (feed upper bound = %+.2f):  next%d %+.2f | bop %+.2f | tsppam %+.2f\n", feed_i - bg_i, nextn,
              cov(w_base.imiss, w_nextn.imiss) - bg_i, cov(w_base.imiss, w_bop.imiss) - bg_i, cov(w_base.imiss, w_ts.imiss) - bg_i);
  std::printf("data-cov cost vs BG-only:  next %+.2f | bop %+.2f | tsppam %+.2f | feed %+.2f\n",
              cov(w_base.dmiss, w_nextn.dmiss) - cov(w_base.dmiss, w_bg.dmiss), cov(w_base.dmiss, w_bop.dmiss) - cov(w_base.dmiss, w_bg.dmiss),
              cov(w_base.dmiss, w_ts.dmiss) - cov(w_base.dmiss, w_bg.dmiss), cov(w_base.dmiss, w_feed.dmiss) - cov(w_base.dmiss, w_bg.dmiss));
  return 0;
}
