// Holistic instruction+data DSE. Interleaves a trace's l1d_l2c (data) and l1i_l2c (instruction) L2-access
// streams BY CYCLE into ONE shared L2 and runs the REAL SPPAM data prefetcher (sppam_predictor, gated
// backward and all) alongside the REAL branch-graph instruction prefetcher (iprefetch_predictor).
//
// SHARING (the whole point): there is ONE prefetch/residency filter -- SPPAM's region prefetch_map -- and
// the branch graph has NONE of its own. The BG PROBES it (shadow_resident) and UPDATES it with every
// instruction prefetch (filter_mark -> allocates the code-page region, so the region table is shared too).
// Eviction coherence flows through sppam.on_l2_evict for BOTH streams' fills. The BG's edge BTB and
// vpage->ppage xlate stay PRIVATE (not merged with SPPAM). No new state is shared beyond the one filter.
//
// Worlds (own L2 + predictors each) answer: (Q1) is feeding instructions to BOTH prefetchers good/bad;
// (Q3) does SPPAM's spatial/SPP, acting as the branch graph's fallthrough over the shared region map, add
// instruction coverage; contention = each stream's coverage alone vs together. Q2 (BG sizing) via CLI args.
#include "cache.h"
#include "iprefetch_predictor.h"
#include "params.h"
#include "prefetch_sink.h"
#include "sppam_predictor.h"
#include "trace_reader.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace sppam_dse;

struct World; // fwd
static uint64_t g_cyc = 0;

// One install into a world's L2, with useless-accuracy counting + shared-filter eviction coherence.
struct World {
  const char* name;
  cache l2{2048, 16};
  bool sppam_on_data = false, bg_on = false, feed_instr = false;
  sppam_predictor* sp = nullptr;
  iprefetch_predictor* bg = nullptr;
  uint64_t dmiss = 0, imiss = 0, useful = 0, useless = 0, dpf = 0, ipf = 0;

  void evict_note(const install_result& r) {
    if (!r.evicted_valid) return;
    if (r.evicted_unused_prefetch) ++useless;
    if (sp) sp->on_l2_evict(r.evicted_block, g_cyc, r.evicted_unused_prefetch); // keep the shared filter coherent
  }
  bool install_pf(uint64_t blk) { // returns placed
    if (l2.resident(blk)) return false;
    auto r = l2.install(blk, g_cyc, g_cyc, true); evict_note(r); return true;
  }
  void demand(uint64_t blk, bool is_instr) {
    bool up = false;
    if (l2.demand_access(blk, g_cyc, up)) { if (up) ++useful; return; }
    if (is_instr) ++imiss; else ++dmiss;
    auto r = l2.install(blk, g_cyc, g_cyc, false); evict_note(r);
  }
};

// SPPAM sink -> the world's shared L2 (installs, counts, marks residency, keeps filter coherent).
struct Sink : prefetch_sink {
  World* w = nullptr;
  bool issue_prefetch(uint64_t block, bool, bool, double, uint32_t) override {
    if (w->l2.resident(block)) return false;
    if (!w->install_pf(block)) return false;
    w->sp->shadow_fill(block); ++w->dpf; return true;
  }
};

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: %s <l1d.zst> <l1i.zst> [maxdata] [instr_entries] [instr_xlate]\n", argv[0]); return 2; }
  uint64_t maxdata = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 8000000ull;
  std::size_t instr_entries = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 256;
  std::size_t instr_xlate = argc > 5 ? std::strtoull(argv[5], nullptr, 10) : 32;

  auto mkP = [&](const char* nm) { params P; P.name = nm; P.enable_ip_filter = true; P.enable_spp = true;
    P.pattern_validate = true; P.pv_feed_confidence = true; P.pv_conf_penalty = 16; P.enable_timing = false;
    P.enable_shadow_squash = true; P.instr_table_entries = instr_entries; P.instr_xlate_entries = instr_xlate; return P; };

  // Worlds: base(no pf), data(SPPAM/data only), instr(BG only), both(SPPAM/data + BG, shared filter, instr NOT
  // fed to SPPAM), feed(both + instructions ALSO fed to SPPAM's map/prediction = instr_feed_data).
  World w_base, w_data, w_instr, w_both, w_feed;
  w_base.name = "base";
  w_data.name = "data";    w_data.sppam_on_data = true;
  w_instr.name = "instr";  w_instr.bg_on = true;
  w_both.name = "both";    w_both.sppam_on_data = true; w_both.bg_on = true;
  w_feed.name = "feed";    w_feed.sppam_on_data = true; w_feed.bg_on = true; w_feed.feed_instr = true;
  World* worlds[5] = {&w_base, &w_data, &w_instr, &w_both, &w_feed};

  params Pd = mkP("data"), Pbo = mkP("both"), Pfe = mkP("feed");
  Sink sd, sbo, sfe;
  w_data.sp = new sppam_predictor(Pd, &sd); sd.w = &w_data;
  w_both.sp = new sppam_predictor(Pbo, &sbo); sbo.w = &w_both;
  w_feed.sp = new sppam_predictor(Pfe, &sfe); sfe.w = &w_feed;
  params Pi = mkP("bg");
  w_instr.bg = new iprefetch_predictor(Pi);                          // isolated BG: uses its OWN filter
  w_both.bg = new iprefetch_predictor(Pi);
  w_feed.bg = new iprefetch_predictor(Pi);
  // SHARE the residency filter: in both/feed the BG probes SPPAM's region prefetch_map instead of a private one.
  w_both.bg->set_shared_probe([sp = w_both.sp](uint64_t b) { return sp->filter_probe(b); });
  w_feed.bg->set_shared_probe([sp = w_feed.sp](uint64_t b) { return sp->filter_probe(b); });

  auto do_data = [&](World& w, uint64_t blk, const access_record& r) {
    if (w.sppam_on_data && w.sp) {
      const cache_line* pl = w.l2.probe(blk); bool hit = (pl != nullptr); bool up = false;
      if (hit) { w.l2.demand_access(blk, g_cyc, up); if (up) ++w.useful; }
      else { ++w.dmiss; auto rr = w.l2.install(blk, g_cyc, g_cyc, false); w.evict_note(rr); w.sp->shadow_fill(blk); }
      w.sp->operate(blk, r.ip, hit, up, up, (atype)r.type, g_cyc);
    } else w.demand(blk, /*is_instr=*/false);
  };
  auto do_instr = [&](World& w, uint64_t blk, const access_record& r) {
    w.demand(blk, /*is_instr=*/true);
    if (!w.bg_on) return;
    if (w.sp) w.sp->filter_mark(blk);   // demanded code block is resident in the SHARED filter (shares region table)
    // branch graph: issue instruction prefetches into the shared L2, updating the shared residency filter.
    w.bg->operate(r.vaddr >> 6, blk, [&](uint64_t pblk) {
      if (w.install_pf(pblk)) { if (w.sp) w.sp->filter_mark(pblk); ++w.ipf; }
    });
    // feed_instr: the same instruction access ALSO trains/triggers the SPPAM data path (shared region map).
    if (w.feed_instr && w.sp) {
      bool up = false; const cache_line* pl = w.l2.probe(blk); bool hit = (pl != nullptr);
      // (residency already updated above; just drive SPPAM's map + prediction on the code stream)
      w.sp->operate(blk, r.vaddr, hit, up, up, (atype)r.type, g_cyc);
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
      // Faithful stack: LOAD/RFO demands AND berti's PREFETCH fills are ALL data accesses that fill the shared
      // L2, train SPPAM, and count toward coverage (count_prefetch_as_demand + train_on_prefetch). SPPAM's job
      // is the residual on top of berti. Exclude instruction fetches (ip==vaddr; they arrive on the l1i stream).
      if (rd.is_translated && (t == atype::LOAD || t == atype::RFO || t == atype::PREFETCH) && (rd.ip >> 6) != (rd.vaddr >> 6)) {
        uint64_t blk = rd.paddr >> 6; ++dseen; for (auto* w : worlds) do_data(*w, blk, rd);
      }
      hd = td.next(rd);
      if (dseen >= maxdata) break;
    } else {
      if (ri.is_translated && !ri.is_prefetch && (ri.ip >> 6) == (ri.vaddr >> 6)) {
        uint64_t blk = ri.paddr >> 6; ++iseen; for (auto* w : worlds) do_instr(*w, blk, ri);
      }
      hi = ti.next(ri);
    }
  }

  auto cov = [](uint64_t base, uint64_t x) { return base ? 100.0 * (1.0 - (double)x / (double)base) : 0.0; };
  auto acc = [](uint64_t u, uint64_t l) { return (u + l) ? 100.0 * u / (u + l) : 0.0; };
  std::printf("data_acc=%llu instr_acc=%llu  BG(entries=%zu xlate=%zu)  base_miss: data=%llu instr=%llu\n",
              (unsigned long long)dseen, (unsigned long long)iseen, instr_entries, instr_xlate,
              (unsigned long long)w_base.dmiss, (unsigned long long)w_base.imiss);
  std::printf("%-6s %9s %10s %10s %9s %9s %8s\n", "world", "data_cov", "instr_cov", "total_cov", "acc", "d_pf", "i_pf");
  auto row = [&](World& w) {
    std::printf("%-6s %8.2f%% %9.2f%% %9.2f%% %8.1f%% %9.3f %8.3f\n", w.name,
                cov(w_base.dmiss, w.dmiss), cov(w_base.imiss, w.imiss), cov(w_base.dmiss + w_base.imiss, w.dmiss + w.imiss),
                acc(w.useful, w.useless), dseen ? (double)w.dpf / dseen : 0.0, iseen ? (double)w.ipf / iseen : 0.0);
  };
  for (auto* w : worlds) row(*w);
  std::printf("\nQ1 feed vs both (instr->BOTH prefetchers): total_cov %+.2f  data_cov %+.2f  instr_cov %+.2f  acc %+.1f\n",
              cov(w_base.dmiss + w_base.imiss, w_feed.dmiss + w_feed.imiss) - cov(w_base.dmiss + w_base.imiss, w_both.dmiss + w_both.imiss),
              cov(w_base.dmiss, w_feed.dmiss) - cov(w_base.dmiss, w_both.dmiss),
              cov(w_base.imiss, w_feed.imiss) - cov(w_base.imiss, w_both.imiss),
              acc(w_feed.useful, w_feed.useless) - acc(w_both.useful, w_both.useless));
  std::printf("Q3 SPPAM-as-BG-fallthrough (instr_cov feed - both): %+.2f  (does SPPAM's spatial/SPP add instruction coverage?)\n",
              cov(w_base.imiss, w_feed.imiss) - cov(w_base.imiss, w_both.imiss));
  std::printf("contention: data_cov both-vs-alone %+.2f | instr_cov both-vs-alone %+.2f\n",
              cov(w_base.dmiss, w_both.dmiss) - cov(w_base.dmiss, w_data.dmiss),
              cov(w_base.imiss, w_both.imiss) - cov(w_base.imiss, w_instr.imiss));
  return 0;
}
