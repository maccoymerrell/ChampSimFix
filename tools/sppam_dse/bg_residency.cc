// Branch-graph residency study on the holistic shared-L2 setup (real SPPAM on data + branch graph on
// instructions). Compares how the BG suppresses redundant prefetches WITHOUT SPPAM's region map:
//   shared    = probe/mark SPPAM's region prefetch_map (couples code into the data region table).
//   btbfirst  = TINY residency tied to the BTB: ONE bit per prefetch BURST (mark only the FIRST block; the
//               whole next-line burst is gated on that single bit, so it squashes the trailing accesses).
//               Each bit is OWNED by the BTB entry that issued it -> when that entry is overwritten the bit is
//               cleared; also cleared on L2 eviction. Bounded by the BTB working set (a few hundred bits).
// Metrics: instr_cov, data_cov, i_acc, i_pf, REDUNDANCY (issued prefetch already in L2), filter-suppression, state.
#include "cache.h"
#include "params.h"
#include "prefetch_sink.h"
#include "sppam_predictor.h"
#include "trace_reader.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>
using namespace sppam_dse;

static uint64_t g_cyc = 0;
static inline uint64_t Hh(uint64_t x) { return x * 0x9E3779B97F4A7C15ull; }
enum Res { R_SHARED, R_BTBFIRST, R_BTBTAG, R_REGION_PACKED };

struct IPred {
  struct E { uint16_t tag = 0xFFFF; bool v = false; int16_t d0 = 0, d1 = 0; uint8_t c0 = 0, c1 = 0, run = 0;
             uint32_t fbi = 0; bool fbset = false; };   // fbi = the residency-filter slot this BTB entry owns
  std::vector<E> ed; uint64_t emask;
  std::vector<uint64_t> xv; std::vector<uint32_t> xp; uint64_t xmask;
  std::vector<char> fb; std::vector<uint16_t> fbt; uint64_t fbmask; // BTB-tied first-block residency: bit (BTBFIRST) or 16-bit tag (BTBTAG)
  int la_depth = 8, nextn = 2; double conf = 0.60; int64_t dlim = 128;
  Res res = R_SHARED;
  std::function<bool(uint64_t)> ext_probe; std::function<void(uint64_t)> ext_mark;
  uint64_t last_ip = 0; bool have_last = false;
  uint64_t pred_total = 0, filtered = 0, issued = 0, redundant = 0, new_pf = 0, btb_clears = 0;
  IPred(int elog2, int xlog2, int fblog2) : ed(std::size_t{1} << elog2), emask((uint64_t{1} << elog2) - 1),
    xv(std::size_t{1} << xlog2, ~uint64_t{0}), xp(std::size_t{1} << xlog2, 0), xmask((uint64_t{1} << xlog2) - 1),
    fb(std::size_t{1} << fblog2, 0), fbt(std::size_t{1} << fblog2, 0), fbmask((uint64_t{1} << fblog2) - 1) {}

  static uint16_t etag(uint64_t ib) { return (uint16_t)((Hh(ib) >> 17) & 0xFFFF); }
  E* peek(uint64_t ib) { E& e = ed[Hh(ib) & emask]; return (e.v && e.tag == etag(ib)) ? &e : nullptr; }
  E& slot(uint64_t ib) { E& e = ed[Hh(ib) & emask]; uint16_t t = etag(ib);
    if (!e.v || e.tag != t) { if ((res == R_BTBFIRST || res == R_BTBTAG) && e.fbset) { fp_clear(e.fbi); ++btb_clears; } e = E{}; e.tag = t; e.v = true; } // BTB overwrite clears its owned slot
    return e; }
  void obs(uint64_t f, int64_t d) { if (d >= dlim || d <= -dlim) return; E& e = slot(f);
    if (e.c0 && e.d0 == d) { if (e.c0 < 15) ++e.c0; } else if (e.c1 && e.d1 == d) { if (e.c1 < 15) ++e.c1; }
    else if (!e.c0) { e.d0 = (int16_t)d; e.c0 = 1; } else if (!e.c1) { e.d1 = (int16_t)d; e.c1 = 1; }
    else if (e.c0 <= e.c1) { if (--e.c0 == 0) { e.d0 = (int16_t)d; e.c0 = 1; } } else if (--e.c1 == 0) { e.d1 = (int16_t)d; e.c1 = 1; }
    if (e.c1 > e.c0) { std::swap(e.d0, e.d1); std::swap(e.c0, e.c1); } }
  std::pair<uint64_t, double> predict(uint64_t ib) { E* e = peek(ib); if (!e || !e->c0) return {ib + 1, 0.0};
    double t = e->c0 + e->c1; return {ib + (uint64_t)(int64_t)e->d0, t > 0 ? e->c0 / t : 0}; }
  void xins(uint64_t vp, uint32_t pp) { std::size_t i = Hh(vp) & xmask; xv[i] = vp; xp[i] = pp; }
  bool xlat(uint64_t ib, uint64_t& pb) { uint64_t vp = ib >> 6; std::size_t i = Hh(vp) & xmask; if (xv[i] != vp) return false; pb = ((uint64_t)xp[i] << 6) | (ib & 63); return true; }

  std::size_t fbi_of(uint64_t pb) const { return Hh(pb) & fbmask; }
  static uint16_t ftag(uint64_t pb) { return (uint16_t)((Hh(pb) >> 13) & 0xFFFF); }
  bool fp_test(std::size_t s, uint64_t blk) const { return res == R_BTBTAG ? (fbt[s] == ftag(blk)) : (bool)fb[s]; }
  void fp_write(std::size_t s, uint64_t blk) { if (res == R_BTBTAG) fbt[s] = ftag(blk); else fb[s] = 1; }
  void fp_clear(std::size_t s) { fb[s] = 0; fbt[s] = 0; }
  void on_evict(uint64_t pb) { if (res == R_BTBFIRST || res == R_BTBTAG) fp_clear(fbi_of(pb)); } // L2 eviction clears the first-block filter slot
  // set the burst's first-block slot and hand OWNERSHIP to the issuing BTB entry (owner), clearing any prior slot it held.
  void set_owned(std::size_t s, uint64_t blk, E& owner) { fp_write(s, blk); if (owner.fbset && owner.fbi != s) fp_clear(owner.fbi); owner.fbi = (uint32_t)s; owner.fbset = true; }

  template <class L2res, class IssueNew>
  void operate(uint64_t ib, uint64_t phys, const L2res& l2res, const IssueNew& issue_new) {
    xins(ib >> 6, (uint32_t)(phys >> 6));
    if (have_last) obs(last_ip, (int64_t)ib - (int64_t)last_ip);
    const uint64_t pg = phys >> 6; const int off = (int)(phys & 63);

    if (res == R_SHARED || res == R_REGION_PACKED) { // per-block region residency (shared prefetch_map, or packed code entry)
      auto try_pf = [&](uint64_t pb) { ++pred_total; if (ext_probe(pb)) { ++filtered; return; }
        ++issued; if (l2res(pb)) { ++redundant; ext_mark(pb); return; } if (issue_new(pb)) { ++new_pf; ext_mark(pb); } };
      uint64_t cur = ib;
      for (int d = 0; d < la_depth; ++d) { auto [nx, cf] = predict(cur); if (d > 0 && cf < conf) break; uint64_t pb; if (xlat(nx, pb)) try_pf(pb); cur = nx; }
      for (int k = 1; k <= nextn; ++k) { uint64_t pb = phys + k; if ((pb >> 6) == pg && off + k < 64) try_pf(pb); }
    } else { // R_BTBFIRST / R_BTBTAG: one filter slot per burst (first block), owned by a BTB entry
      uint64_t cur = ib;
      for (int d = 0; d < la_depth; ++d) { auto [nx, cf] = predict(cur); if (d > 0 && cf < conf) break;
        uint64_t pb; if (xlat(nx, pb)) { std::size_t s = fbi_of(pb); ++pred_total;      // control-flow target = its own first block
          if (fp_test(s, pb)) ++filtered;
          else { ++issued; if (l2res(pb)) ++redundant; else if (issue_new(pb)) ++new_pf; set_owned(s, pb, slot(nx)); } }
        cur = nx; }
      if (nextn > 0 && off + 1 < 64) { uint64_t first = phys + 1; std::size_t s = fbi_of(first);   // next-line burst gated on ONE slot (first block)
        if (fp_test(s, first)) filtered += nextn;
        else { for (int k = 1; k <= nextn; ++k) { uint64_t pb = phys + k; if ((pb >> 6) != pg || off + k >= 64) break; ++issued; if (l2res(pb)) ++redundant; else if (issue_new(pb)) ++new_pf; }
               set_owned(s, first, slot(ib)); }
        pred_total += nextn; }
    }
    last_ip = ib; have_last = true;
  }
  double fb_bytes() const { // filter bytes + per-edge fbi/fbset overhead (BTB modes only; region modes reuse existing state)
    if (res == R_BTBFIRST) return (double)fb.size() / 8.0 + ed.size() * (16 + 1) / 8.0;
    if (res == R_BTBTAG) return (double)fbt.size() * 16 / 8.0 + ed.size() * (16 + 1) / 8.0;
    return 0.0; }
};

struct World {
  const char* name; Res res = R_SHARED; int nextn = 2; int fblog2 = 9;
  cache l2{2048, 16};
  sppam_predictor* sp = nullptr; IPred* ip = nullptr;
  uint64_t dmiss = 0, imiss = 0, dpf = 0, ipf = 0, iuseful = 0, iuseless = 0;
  std::unordered_set<uint64_t> instr_pf;
  void evict_note(const install_result& r) { if (!r.evicted_valid) return;
    if (sp) sp->on_l2_evict(r.evicted_block, g_cyc, r.evicted_unused_prefetch);
    if (sp && res == R_REGION_PACKED) sp->filter_evict_code(r.evicted_block); // clear the packed code-residency bit
    if (ip) ip->on_evict(r.evicted_block);
    auto it = instr_pf.find(r.evicted_block); if (it != instr_pf.end()) { if (r.evicted_unused_prefetch) ++iuseless; instr_pf.erase(it); } }
  bool install_pf(uint64_t blk) { if (l2.resident(blk)) return false; auto r = l2.install(blk, g_cyc, g_cyc, true); evict_note(r); return true; }
  void demand(uint64_t blk, bool is_instr) { bool up = false;
    if (l2.demand_access(blk, g_cyc, up)) { if (up && is_instr) { auto it = instr_pf.find(blk); if (it != instr_pf.end()) { ++iuseful; instr_pf.erase(it); } } return; }
    if (is_instr) ++imiss; else ++dmiss; auto r = l2.install(blk, g_cyc, g_cyc, false); evict_note(r); }
};

struct Sink : prefetch_sink { World* w = nullptr;
  bool issue_prefetch(uint64_t block, bool, bool, double, uint32_t) override {
    if (w->l2.resident(block)) return false; if (!w->install_pf(block)) return false; w->sp->shadow_fill(block); ++w->dpf; return true; } };

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: %s <l1d> <l1i> [maxdata] [nextn]\n", argv[0]); return 2; }
  uint64_t maxdata = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 8000000ull;
  int nextn = argc > 4 ? std::atoi(argv[4]) : 2;
  auto mkP = [&](const char* nm) { params P; P.name = nm; P.enable_ip_filter = true; P.enable_spp = true; P.pattern_validate = true;
    P.pv_feed_confidence = true; P.pv_conf_penalty = 16; P.enable_timing = false; P.enable_shadow_squash = true; return P; };

  std::vector<World*> ws; auto mk = [&](const char* nm, Res r, int fb2, int nn) {
    World* w = new World(); w->name = nm; w->res = r; w->fblog2 = fb2; w->nextn = nn; ws.push_back(w); return w; };
  World* base = mk("base", R_SHARED, 9, nextn);
  World* data = mk("data", R_SHARED, 9, nextn);
  mk("bg", R_SHARED, 9, 0);                         // BG edges only (no sequential)
  mk("n2_shared", R_SHARED, 9, nextn);             // shared region-map residency: per-2KiB-region, prefetch_map only
  mk("n2_packed", R_REGION_PACKED, 9, nextn);      // PACKED: per-4KiB-page code entry, both maps (2x density), reuses dead access_map
  mk("n2_btbtag512", R_BTBTAG, 9, nextn);          // BTB-tied TAGGED first-block (no aliasing), sizes:
  mk("n2_btbtag2k", R_BTBTAG, 11, nextn);
  mk("n2_btbbit2k", R_BTBFIRST, 11, nextn);        // BTB-tied UNTAGGED first-block (aliasing) for contrast

  std::vector<Sink*> sinks;
  for (World* w : ws) { if (w == base) continue; Sink* s = new Sink(); s->w = w; sinks.push_back(s);
    w->sp = new sppam_predictor(*new params(mkP(w->name)), s);
    if (w == data) continue;
    w->ip = new IPred(7, 5, w->fblog2); w->ip->res = w->res; w->ip->nextn = w->nextn;
    World* wl = w;
    if (w->res == R_REGION_PACKED) { w->ip->ext_probe = [wl](uint64_t b) { return wl->sp->filter_probe_code(b); };
                                     w->ip->ext_mark = [wl](uint64_t b) { wl->sp->filter_mark_code(b); }; }
    else { w->ip->ext_probe = [wl](uint64_t b) { return wl->sp->filter_probe(b); };
           w->ip->ext_mark = [wl](uint64_t b) { wl->sp->filter_mark(b); }; } }

  auto do_data = [&](World& w, uint64_t blk, const access_record& r) {
    if (!w.sp) { w.demand(blk, false); return; }
    const cache_line* pl = w.l2.probe(blk); bool hit = (pl != nullptr); bool up = false;
    if (hit) w.l2.demand_access(blk, g_cyc, up);
    else { ++w.dmiss; auto rr = w.l2.install(blk, g_cyc, g_cyc, false); w.evict_note(rr); w.sp->shadow_fill(blk); }
    w.sp->operate(blk, r.ip, hit, up, up, (atype)r.type, g_cyc); };
  auto do_instr = [&](World& w, uint64_t blk, const access_record& r) {
    w.demand(blk, true);
    if (!w.ip) return;
    if (w.res == R_SHARED) w.sp->filter_mark(blk); else if (w.res == R_REGION_PACKED) w.sp->filter_mark_code(blk);
    w.ip->operate(r.vaddr >> 6, blk, [&](uint64_t pb) { return w.l2.resident(pb); },
                  [&](uint64_t pb) { if (w.install_pf(pb)) { ++w.ipf; w.instr_pf.insert(pb); return true; } return false; }); };

  trace_reader td(argv[1]), ti(argv[2]); access_record rd, ri; bool hd = td.next(rd), hi = ti.next(ri);
  uint64_t dseen = 0, iseen = 0;
  while (hd || hi) { bool take_data = (hd && hi) ? (rd.cycle <= ri.cycle) : hd; ++g_cyc;
    if (take_data) { atype t = (atype)rd.type;
      if (rd.is_translated && (t == atype::LOAD || t == atype::RFO || t == atype::PREFETCH) && (rd.ip >> 6) != (rd.vaddr >> 6)) {
        uint64_t blk = rd.paddr >> 6; ++dseen; for (World* w : ws) do_data(*w, blk, rd); }
      hd = td.next(rd); if (dseen >= maxdata) break; }
    else { if (ri.is_translated && !ri.is_prefetch && (ri.ip >> 6) == (ri.vaddr >> 6)) {
        uint64_t blk = ri.paddr >> 6; ++iseen; for (World* w : ws) do_instr(*w, blk, ri); }
      hi = ti.next(ri); } }

  auto cov = [](uint64_t b, uint64_t x) { return b ? 100.0 * (1.0 - (double)x / b) : 0.0; };
  std::printf("data_acc=%llu instr_acc=%llu  base_miss data=%llu instr=%llu\n",
              (unsigned long long)dseen, (unsigned long long)iseen, (unsigned long long)base->dmiss, (unsigned long long)base->imiss);
  std::printf("%-12s %9s %9s %7s %7s %8s %8s %8s\n", "arm", "instr_cov", "data_cov", "i_acc", "i_pf", "redund%", "filt%", "state");
  for (World* w : ws) {
    double dc = cov(base->dmiss, w->dmiss), ic = cov(base->imiss, w->imiss);
    double iacc = (w->iuseful + w->iuseless) ? 100.0 * w->iuseful / (w->iuseful + w->iuseless) : 0.0;
    double red = w->ip && w->ip->issued ? 100.0 * w->ip->redundant / w->ip->issued : 0.0;
    double flt = w->ip && w->ip->pred_total ? 100.0 * w->ip->filtered / w->ip->pred_total : 0.0;
    char st[32]; if (w->ip && (w->res == R_BTBFIRST || w->res == R_BTBTAG)) std::snprintf(st, sizeof st, "%.0fB", w->ip->fb_bytes()); else std::snprintf(st, sizeof st, "%s", w->ip ? (w->res == R_REGION_PACKED ? "packed" : "region") : "-");
    std::printf("%-12s %8.2f%% %8.2f%% %6.1f%% %6.3f %7.1f%% %7.1f%% %8s\n",
                w->name, ic, dc, iacc, iseen ? (double)w->ipf / iseen : 0.0, red, flt, st);
  }
  return 0;
}
