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
#include "evaluator.h"   // shared_mem: the established LLC+DRAM PE model (latency-saved), so the perceptron trains on PE not usefulness
#include "iprefetch_predictor.h"
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

struct World; // fwd
static uint64_t g_cyc = 0;

// One install into a world's L2, with useless-accuracy counting + shared-filter eviction coherence.
// PE (the target metric) = I_UPF - I_POLL: a useful prefetch's reward is the ACTUAL latency saved (avg L_DRAM if it
// covered a DRAM miss, else L_LLC); a prefetch's COST is the pollution it caused -- if it evicted a valuable line
// that is then re-demanded (a DRAM re-miss), it is charged that DRAM latency. Without I_POLL a useless prefetch
// costs only its trivial fill (~L_LLC) and one DRAM hit (+~1900) outweighs ~54 of them => no filtering signal.
struct World {
  const char* name;
  cache l2{2048, 16};
  bool sppam_on_data = false, bg_on = false, feed_instr = false;
  sppam_predictor* sp = nullptr;
  iprefetch_predictor* bg = nullptr;
  shared_mem* sm = nullptr;              // LLC+DRAM for PE (SPPAM worlds only); null -> bare, coverage-only
  double llc_hit = 35.0, base_dram = 100.0;
  uint64_t dmiss = 0, imiss = 0, useful = 0, useless = 0, dpf = 0, ipf = 0;
  std::unordered_map<uint64_t, uint64_t> polluter_; // valuable victim block -> the prefetch that evicted it (pending pollution)
  std::unordered_map<uint64_t, double> pf_cost_;    // prefetch block -> accrued pollution cost (<=0), folded in at resolution
  uint64_t poll_events = 0; double poll_cost_sum = 0.0;

  // Fill an L2 miss. With sm present, route through LLC+DRAM to learn from_dram (LLC hit vs DRAM miss) for PE.
  install_result fill_miss(uint64_t blk, bool is_demand, bool prefetched) {
    bool served_dram = false;
    double lat = 0.0;
    if (sm) lat = sm->fetch_latency(blk, g_cyc, is_demand, &served_dram);
    auto r = l2.install(blk, g_cyc, g_cyc, prefetched, false, served_dram);
    if (prefetched) {
      // I_LAT: a prefetch's DRAM fetch contributes queueing congestion = lat - base_dram (~0 when DRAM idle, grows
      // with load). Under bandwidth saturation this makes even useless prefetches expensive -> the filtering signal.
      if (served_dram && sm) { double bw = lat - sm->base_dram_latency(); if (bw > 0.0) pf_cost_[blk] -= bw; }
      // I_POLL bookkeeping: a PREFETCH that evicts a VALUABLE line (demand or used-prefetch) is a pending polluter.
      if (r.evicted_valid && !r.evicted_unused_prefetch) {
        if (polluter_.size() >= 300000) polluter_.clear();   // crude bound (sampling tolerates loss)
        polluter_[r.evicted_block] = blk;
      }
    }
    return r;
  }
  // A demand miss on `blk`: if a prefetch evicted this block, that prefetch forced this DRAM re-miss -> charge it.
  void note_pollution(uint64_t blk) {
    auto it = polluter_.find(blk);
    if (it == polluter_.end()) return;
    double c = sm ? sm->avg_dram_latency() : base_dram;
    pf_cost_[it->second] -= c; polluter_.erase(it);
    ++poll_events; poll_cost_sum += c;
  }
  // Accrued pollution cost for a prefetch, folded into its PE at resolution (and cleared).
  double resolve_cost(uint64_t blk) { auto it = pf_cost_.find(blk); if (it == pf_cost_.end()) return 0.0; double c = it->second; pf_cost_.erase(it); return c; }
  // Latency saved by a useful prefetch = avg L_DRAM if it covered a DRAM miss, else L_LLC.
  double pe_useful(bool from_dram) const { return from_dram ? (sm ? sm->avg_dram_latency() : base_dram) : llc_hit; }

  void evict_note(const install_result& r) {
    if (!r.evicted_valid) return;
    if (r.evicted_unused_prefetch) ++useless;
    // A prefetch that died unused resolves NEGATIVE PE: trivial wasted fill PLUS any pollution it caused. on_l2_evict
    // only trains the unused case; resolve_cost() also clears a used-prefetch/demand victim's (absent) cost.
    if (sp) sp->on_l2_evict(r.evicted_block, g_cyc, r.evicted_unused_prefetch, -llc_hit + resolve_cost(r.evicted_block));
  }
  bool install_pf(uint64_t blk) { // returns placed
    if (l2.resident(blk)) return false;
    auto r = fill_miss(blk, /*is_demand=*/false, /*prefetched=*/true); evict_note(r); return true;
  }
  // Returns hit; on a useful-prefetch hit reports *used and its *from_dram so the caller can resolve real PE.
  bool demand(uint64_t blk, bool is_instr, bool* used = nullptr, bool* from_dram = nullptr) {
    bool up = false, ufd = false;
    if (l2.demand_access(blk, g_cyc, up, nullptr, &ufd)) {
      if (up) { ++useful; if (used) *used = true; if (from_dram) *from_dram = ufd; }
      return true;
    }
    if (is_instr) ++imiss; else ++dmiss;
    note_pollution(blk);                 // this demand miss may be a prefetch-evicted line coming back (pollution)
    auto r = fill_miss(blk, /*is_demand=*/true, /*prefetched=*/false); evict_note(r);
    return false;
  }
};

// SPPAM sink -> the world's shared L2 (installs, counts, marks residency, keeps filter coherent).
struct Sink : prefetch_sink {
  World* w = nullptr;
  bool perc_on = false; // gate the separate SPP engine through the perceptron (all 3 engines filtered)
  bool issue_prefetch(uint64_t block, bool, bool, double, uint32_t) override {
    if (w->l2.resident(block)) return false;
    if (!w->install_pf(block)) return false;
    w->sp->shadow_fill(block); ++w->dpf; return true;
  }
  bool perc_gate(uint64_t block, int engine, int depth, uint64_t pc, int conf, uint64_t sig) override {
    return !perc_on || w->sp->perc_keep(block, engine, depth, pc, conf, sig);
  }
  void perc_note_issue_ext(uint64_t block) override { if (perc_on) w->sp->perc_note_issue(block); }
};

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: %s <l1d.zst> <l1i.zst> [maxdata] [instr_entries] [instr_xlate]\n", argv[0]); return 2; }
  uint64_t maxdata = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 8000000ull;
  std::size_t instr_entries = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 256;
  std::size_t instr_xlate = argc > 5 ? std::strtoull(argv[5], nullptr, 10) : 32;

  // Perceptron sweep knobs (CLI): argv[6]=feat_mask (0 = perceptron off), argv[7]=tau_keep, argv[8]=engine_split.
  const uint32_t g_mask = argc > 6 ? static_cast<uint32_t>(std::strtoull(argv[6], nullptr, 0)) : 0;
  const int g_tau = argc > 7 ? std::atoi(argv[7]) : 24;
  const bool g_split = argc > 8 ? std::atoi(argv[8]) != 0 : false;
  auto mkP = [&](const char* nm) { params P; P.name = nm; P.enable_ip_filter = true; P.enable_spp = true;
    P.pattern_validate = true; P.pv_feed_confidence = true; P.pv_conf_penalty = 16; P.enable_timing = false;
    P.enable_shadow_squash = true; P.instr_table_entries = instr_entries; P.instr_xlate_entries = instr_xlate;
    // Perceptron gate (all 3 engines): PE-labeled via the shared_mem LLC+DRAM below the L2 (latency saved, not usefulness).
    if (g_mask) { P.enable_perceptron_filter = true; P.perc_label_pe = true; P.perc_feat_mask = g_mask;
      P.perc_tau_keep = g_tau; P.perc_gate_instr = true; P.perc_engine_split = g_split; P.perc_dump_weights = true;
      P.perc_profile = (std::getenv("PERC_PROFILE") != nullptr); } // PERC_PROFILE=1 -> emit raw PROF tuples for the offline sweep
    return P; };

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
  sd.perc_on = sbo.perc_on = sfe.perc_on = (g_mask != 0); // SPP gated through the perceptron when enabled
  // Each SPPAM world gets its OWN LLC+DRAM (independent experiments) so PE = real latency saved. Single core, no market.
  auto mk_sm = [&](World& w, params& P) {
    w.sm = new shared_mem(P.llc_sets, P.llc_ways, P.dram, P.enable_timing, P.llc_hit_latency,
                          /*market=*/false, P.bw_market_target_util, P.bw_market_step, /*ncores=*/1, P);
    w.llc_hit = static_cast<double>(P.llc_hit_latency); w.base_dram = P.dram.base_latency;
  };
  mk_sm(w_data, Pd); mk_sm(w_both, Pbo); mk_sm(w_feed, Pfe);
  params Pi = mkP("bg");
  w_instr.bg = new iprefetch_predictor(Pi);                          // isolated BG: uses its OWN filter
  w_both.bg = new iprefetch_predictor(Pi);
  w_feed.bg = new iprefetch_predictor(Pi);
  // SHARE the residency filter: in both/feed the BG probes SPPAM's region prefetch_map instead of a private one.
  w_both.bg->set_shared_probe([sp = w_both.sp](uint64_t b) { return sp->filter_probe(b); });
  w_feed.bg->set_shared_probe([sp = w_feed.sp](uint64_t b) { return sp->filter_probe(b); });

  auto do_data = [&](World& w, uint64_t blk, const access_record& r) {
    if (w.sppam_on_data && w.sp) {
      const cache_line* pl = w.l2.probe(blk); bool hit = (pl != nullptr); bool up = false, ufd = false;
      if (hit) { w.l2.demand_access(blk, g_cyc, up, nullptr, &ufd); if (up) ++w.useful; }
      else { ++w.dmiss; w.note_pollution(blk); auto rr = w.fill_miss(blk, /*is_demand=*/true, /*prefetched=*/false); w.evict_note(rr); w.sp->shadow_fill(blk); }
      if (g_mask && w.sm) w.sp->perc_set_mshr(w.sm->dram_bw_index());     // DRAM-pressure feature (f[8])
      const double pe = up ? (w.pe_useful(ufd) + w.resolve_cost(blk)) : 0.0; // useful prefetch: latency saved net of pollution it caused
      w.sp->operate(blk, r.ip, hit, up, up, (atype)r.type, g_cyc, pe);     // operate resolves the useful data/SPP prefetch on PE
    } else w.demand(blk, /*is_instr=*/false);
  };
  auto do_instr = [&](World& w, uint64_t blk, const access_record& r) {
    bool used = false, ufd = false;
    w.demand(blk, /*is_instr=*/true, &used, &ufd);
    // BG-RESOLUTION FIX: a USED instruction prefetch (BG or SPPAM-fallthrough) never goes through sp->operate, so
    // resolve it here on real latency-saved PE -- else the perceptron only ever sees instr prefetches as useless.
    if (used && g_mask && w.sp) w.sp->perc_resolve_ext(blk, w.pe_useful(ufd) + w.resolve_cost(blk));
    if (!w.bg_on) return;
    if (w.sp) w.sp->filter_mark(blk);   // demanded code block is resident in the SHARED filter (shares region table)
    // branch graph: issue instruction prefetches into the shared L2, updating the shared residency filter.
    w.bg->operate(r.vaddr >> 6, blk, [&](uint64_t pblk, uint64_t nx_ip, double conf, int d) {
      // Perceptron final gate for the branch graph (engine 3): real edge confidence (0..15) + next-pc block stashed
      // for the methodology-check features. sig = pblk (predicted block).
      if (g_mask && w.sp) { w.sp->perc_set_raw_bg(nx_ip);
        if (!w.sp->perc_keep(pblk, 3, d ? d : 1, r.vaddr, (int)(conf * 15.0 + 0.5), pblk)) return; }
      if (w.install_pf(pblk)) { if (w.sp) { w.sp->filter_mark(pblk); if (g_mask) w.sp->perc_note_issue(pblk); } ++w.ipf; }
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
  uint64_t dseen = 0, iseen = 0, first_cycle = 0; bool first_seen = false;
  while (hd || hi) {
    bool take_data = (hd && hi) ? (rd.cycle <= ri.cycle) : hd;
    // Clock = the trace's REAL L2-access cycle (not an access counter), so DRAM utilization / bandwidth cost is
    // calibrated to real time (service_cycles=7, tau=200 are real cycles). Normalized so uint32_t stamps don't wrap.
    uint64_t cyc = take_data ? rd.cycle : ri.cycle;
    if (!first_seen) { first_cycle = cyc; first_seen = true; }
    g_cyc = cyc >= first_cycle ? cyc - first_cycle : g_cyc; // guard non-monotonic stragglers
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
