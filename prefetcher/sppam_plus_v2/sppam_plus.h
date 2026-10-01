#ifndef PREFETCHER_SPPAM_PLUS_H
#define PREFETCHER_SPPAM_PLUS_H

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "cache.h"
#include "modules.h"
#include "msl/stat_methods.h"

#include "access_kind.h"
#include "iprefetch_predictor.h"
#include "params.h"
#include "prefetch_sink.h"
#include "spp_predictor.h"
#include "sppam_predictor.h"

// SPPAM+SPP L2C prefetcher, ported from the trace-driven design-space tool
// (tools/sppam_dse). The predictors are reused unchanged; this module is the bridge
// to ChampSim: it is the predictors' prefetch_sink (issue_prefetch -> prefetch_line),
// supplies bandwidth pressure from the real L2 MSHR occupancy (via the parent cache), and drives
// the predictors from the cache operate/fill hooks. The validated hybrid is SPPAM as the
// pattern matcher with SPP fall-through (SPP only on triggers SPPAM didn't claim).
//
// Prefetch feedback (per-IP throttle, pattern validation, timeliness) comes from SET-SAMPLED LIFETIMES:
// prefetches that land in one of the sampled L2 sets are followed from issue to the end of their line's
// life by a record living in the shadow of that set (one per way), so every sampled prefetch resolves
// exactly once -- used, merged, or evicted unused -- with its trigger IP and pattern at full resolution.
struct sppam_plus : public champsim::modules::prefetcher, public sppam_dse::prefetch_sink {
  sppam_dse::params P;
  std::unique_ptr<sppam_dse::sppam_predictor> pred_;
  std::unique_ptr<sppam_dse::spp_predictor> spp_;
  // Branch-graph instruction prefetcher (constructed only when enable_instr_prefetch).
  // Sees the L1I-filtered instruction miss stream; issues into L2 on physical addresses.
  std::unique_ptr<sppam_dse::iprefetch_predictor> ipred_;  // instruction walk (L1I-miss control flow) -- committed, kept pristine
  std::unique_ptr<sppam_dse::iprefetch_predictor> dpred_;  // SEPARATE data BG for the per-region PC-sequence lookahead (Idea A) -- no shared-table contamination

  uint64_t cycle_ = 0;          // monotonic operate counter (predictor timing)
  uint64_t real_cycle_ = 0;     // true cycle (from the parent cache)
  uint64_t cur_trigger_ip_ = 0; // trigger PC of the current demand access (for per-IP attribution)
  bool sppam_fired_ = false;    // per-trigger latch: did SPPAM claim this access?
  uint64_t spp_ft_ctr_ = 0;     // fall-through explore trickle counter
  uint64_t pf_issued_ = 0;      // stats
  uint64_t pf_squashed_redundant_ = 0; // prefetches dropped pre-issue by the shadow-residency filter
  uint64_t pf_pass_region_absent_ = 0; // filter passed: block's region not in the table
  uint64_t pf_pass_bit_clear_ = 0;     // filter passed: region present but residency bit clear
  uint64_t instr_pf_issued_ = 0, data_pf_issued_ = 0;

  // ---- Set-sampled prefetch lifetimes ----
  // A sampled set is one whose index the categorizer puts in category 0 (1 in ss_sample_rate sets, hashed so a
  // strided trigger cannot dodge or flood the sample). Its row in the sample table is set >> lg2(rate).
  struct ss_rec {            // one sampled prefetch (33 bits + tag)
    bool valid = false;
    uint16_t tag = 0;        // block tag: the 16 address bits above the set index
    uint16_t iph = 0;        // trigger IP hash (the PHT's form)
    uint16_t key = 0;        // PHT key the prediction came from (pattern validation)
    uint8_t pos = 0;         // prediction-counter position (depth-0 predictions; 0xFF = deeper)
    bool has_pat = false;
    uint8_t eng = 0;         // 0 forward, 1 backward, 2 SPP, 3 branch graph
  };
  std::vector<ss_rec> ss_;   // sampled sets x ways: the shadow of those sets
  std::vector<ss_rec> ifl_;  // issued toward a sampled set, until the fill (bounded by MSHR+PQ; oldest replaced)
  std::vector<uint64_t> ifl_block_;
  std::size_t ifl_next_ = 0;
  struct uf_rec { bool valid = false; uint16_t tag = 0; uint16_t iph = 0; uint8_t eng = 0; };
  std::vector<uf_rec> uf_;   // per sampled set: the last untimely_fifo prefetches evicted unused
  std::vector<uint8_t> uf_head_;
  uint32_t ss_rate_ = 64, ss_rows_ = 32, ss_ways_ = 16, ss_set_bits_ = 11, ss_rate_bits_ = 6;
  // the cache's per-line prefetch bit, handed from the evict hook to the fill hook of the same replacement
  bool evict_unused_pending_ = false; uint64_t evict_pending_block_ = 0;
  // resolutions by engine: [eng][0 useless, 1 useful]; merged; untimely / bad by engine; in-flight records lost
  uint64_t res_[4][2] = {}, res_merged_[4] = {}, res_untimely_[4] = {}, res_bad_[4] = {}, ifl_lost_ = 0;
  uint64_t dbg_sfill_rec_ = 0, dbg_sfill_norec_ = 0, dbg_ifl_put_ = 0, dbg_ifl_hitdrop_ = 0; // sampled-set prefetch fills with / without a record; records parked; dropped on a hit

  int ss_row(uint64_t set) const {
    return champsim::msl::categorizer<long>(ss_rate_).get_sample_category(static_cast<long>(set)) == 0
           ? static_cast<int>(set >> ss_rate_bits_) : -1;
  }
  uint16_t ss_tag(uint64_t block) const { return static_cast<uint16_t>(block >> ss_set_bits_); }
  ss_rec* ss_find(int row, uint16_t tag) {
    for (uint32_t w = 0; w < ss_ways_; ++w) { ss_rec& r = ss_[row * ss_ways_ + w]; if (r.valid && r.tag == tag) return &r; }
    return nullptr;
  }
  void ifl_put(uint64_t block, const ss_rec& r) {
    std::size_t i = ifl_.size();
    for (std::size_t k = 0; k < ifl_.size(); ++k) if (!ifl_[k].valid) { i = k; break; }
    if (i == ifl_.size()) { i = ifl_next_; ifl_next_ = (i + 1) % ifl_.size(); ++ifl_lost_; }
    ifl_[i] = r; ifl_[i].valid = true; ifl_block_[i] = block; ++dbg_ifl_put_;
  }
  bool ifl_take(uint64_t block, ss_rec& out) {
    for (std::size_t k = 0; k < ifl_.size(); ++k)
      if (ifl_[k].valid && ifl_block_[k] == block) { out = ifl_[k]; ifl_[k].valid = false; return true; }
    return false;
  }
  void uf_push(int row, uint16_t tag, uint16_t iph, uint8_t eng) {
    const uint32_t n = P.untimely_fifo;
    uf_rec& u = uf_[row * n + uf_head_[row]];
    if (u.valid) resolve_timeliness(u.iph, u.eng, false); // pushed out without a re-demand: the address was wrong
    u = uf_rec{true, tag, iph, eng};
    uf_head_[row] = static_cast<uint8_t>((uf_head_[row] + 1) % n);
  }
  uf_rec* uf_find(int row, uint16_t tag) {
    const uint32_t n = P.untimely_fifo;
    for (uint32_t k = 0; k < n; ++k) { uf_rec& u = uf_[row * n + k]; if (u.valid && u.tag == tag) return &u; }
    return nullptr;
  }

  // ---- The per-IP table ----
  // One entry per live trigger IP (16-bit hash, the PHT's form: with berti's forwarded IPs the PC is masked to its
  // 23 bits first, as the PHT context does). Each group holds a 4-bit counter per outcome; when a pair sums to
  // ip_epoch the first outcome's count becomes the bin and both reset. Nothing ages.
  struct ip_grp { uint8_t c0 = 0, c1 = 0, bin = 0; bool bvalid = false; };
  struct ip_ent { bool valid = false; uint16_t tag = 0; uint8_t lru = 0; ip_grp use, tim, bwd, llc; };
  std::vector<ip_ent> ipt_;
  uint32_t ipt_ways_ = 4, ipt_sets_ = 64, ipt_set_bits_ = 6;
  uint64_t ip_trickle_ctr_ = 0; // 1/N pass-through when a trigger IP is throttled

  uint16_t iphash(uint64_t ip) const {
    if (P.use_berti_src_ip) ip &= 0x7fffffull;
    return ip ? static_cast<uint16_t>((ip * 0x9E3779B97F4A7C15ull) >> 48) : 0;
  }
  const ip_ent* ipt_find(uint16_t h) const {
    const uint32_t base = (h >> (16 - ipt_set_bits_)) * ipt_ways_, tag = h & ((1u << (16 - ipt_set_bits_)) - 1);
    for (uint32_t w = 0; w < ipt_ways_; ++w) if (ipt_[base + w].valid && ipt_[base + w].tag == tag) return &ipt_[base + w];
    return nullptr;
  }
  ip_ent& ipt_get(uint16_t h) {
    const uint32_t base = (h >> (16 - ipt_set_bits_)) * ipt_ways_, tag = h & ((1u << (16 - ipt_set_bits_)) - 1);
    uint32_t hit = ipt_ways_, victim = 0;
    for (uint32_t w = 0; w < ipt_ways_; ++w) {
      ip_ent& e = ipt_[base + w];
      if (e.valid && e.tag == tag) { hit = w; break; }
      if (!e.valid) victim = w; else if (ipt_[base + victim].valid && e.lru > ipt_[base + victim].lru) victim = w;
    }
    if (hit == ipt_ways_) { // allocate: every other way ages, the new entry is the most recent
      hit = victim;
      for (uint32_t w = 0; w < ipt_ways_; ++w) if (w != hit) ++ipt_[base + w].lru;
      ipt_[base + hit] = ip_ent{}; ipt_[base + hit].valid = true; ipt_[base + hit].tag = static_cast<uint16_t>(tag);
    } else {
      for (uint32_t w = 0; w < ipt_ways_; ++w) if (ipt_[base + w].lru < ipt_[base + hit].lru) ++ipt_[base + w].lru;
      ipt_[base + hit].lru = 0;
    }
    return ipt_[base + hit];
  }
  void ip_credit(ip_grp& g, bool first) {
    if (first) ++g.c0; else ++g.c1;
    if (g.c0 + g.c1 >= P.ip_epoch) { g.bin = g.c0; g.bvalid = true; g.c0 = g.c1 = 0; }
  }
  // the bin is a count out of ip_epoch; compare it against a percentage
  bool bin_lt(const ip_grp& g, int pct) const { return g.bvalid && static_cast<uint32_t>(g.bin) * 100u < static_cast<uint32_t>(pct) * P.ip_epoch; }
  bool bin_ge(const ip_grp& g, int pct) const { return g.bvalid && static_cast<uint32_t>(g.bin) * 100u >= static_cast<uint32_t>(pct) * P.ip_epoch; }

  // A sampled prefetch resolved: its pattern's validation and its trigger IP's counters (backward samples credit
  // the backward group only; branch-graph samples are statistics only).
  void resolve(const ss_rec& r, bool useful, bool merged = false) {
    ++res_[r.eng][useful]; if (merged) ++res_merged_[r.eng];
    if (r.eng == 3) return;
    if (P.pattern_validate && r.has_pat) pred_->pattern_outcome(r.key, r.pos == 0xFF ? -1 : r.pos, useful);
    if (!P.enable_ip_filter) return;
    ip_ent& e = ipt_get(r.iph);
    ip_credit(r.eng == 1 ? e.bwd : e.use, useful);
  }
  void resolve_timeliness(uint16_t iph, uint8_t eng, bool untimely) {
    if (untimely) ++res_untimely_[eng]; else ++res_bad_[eng];
    if (eng == 3 || !P.enable_ip_filter) return;
    ip_credit(ipt_get(iph).tim, untimely);
  }

  // Cache-stress and MLP gates of the depth throttle (an IP's untimeliness only matters when the cache is not
  // thrashing and the program is not already memory-parallel).
  uint64_t l2_dem_acc_ = 0, l2_dem_hit_ = 0;
  uint64_t occ_n_ = 0, occ_tot_ = 0, occ_up_ = 0;
  double l2_hit_rate() const { return l2_dem_acc_ > 4096 ? static_cast<double>(l2_dem_hit_) / l2_dem_acc_ : 1.0; }
  double avg_upstream_occ() const { return occ_n_ > 4096 ? static_cast<double>(occ_up_) / occ_n_ : 0.0; }
  // In-flight demand-stream misses by kind (true load/RFO vs berti prefetch access): the MSHR's own contents,
  // mirrored so the upstream occupancy is known without walking it.
  std::unordered_map<uint64_t, bool> inflight_demand_;
  uint64_t inflight_true_ = 0, inflight_berti_ = 0;

  bool ip_is_untimely(const ip_ent* e) const {
    if (e == nullptr || !e->tim.bvalid) return false;
    if (l2_hit_rate() < P.ip_depth_hitrate_min) return false;
    if (P.ip_depth_mlp_max > 0.0 && avg_upstream_occ() > P.ip_depth_mlp_max) return false;
    return bin_ge(e->tim, P.ip_untimely_thresh);
  }
  // Graded volume throttle: 1 = issue fully; >1 = issue 1/div. Harsh below the hard usefulness, light below the soft.
  uint32_t ip_trickle_div(const ip_ent* e) const {
    if (e == nullptr || !e->use.bvalid) return 1;
    if (bin_ge(e->use, P.ip_filter_threshold)) return 1;
    if (bin_lt(e->use, P.ip_filter_threshold_hard)) return static_cast<uint32_t>(P.ip_filter_trickle_hard ? P.ip_filter_trickle_hard : 1);
    return static_cast<uint32_t>(P.ip_filter_trickle ? P.ip_filter_trickle : 1);
  }

  // LLC-only prefetch samples (ip_llc_redirect), separate from the set samples: sparser and pinned far longer, since
  // an LLC line waits much longer for its use. An entry follows its line if a later prefetch moves it into L2.
  struct llc_track { bool valid = false; bool in_l2 = false; uint64_t block = 0; uint64_t issue = 0; uint16_t iph = 0; };
  std::vector<llc_track> llcs_;
  uint64_t llc_sample_ctr_ = 0, llc_probe_ctr_ = 0;
  uint64_t dbg_llc_redirect_ = 0, dbg_llc_bad_ = 0, dbg_llc_dup_ = 0, dbg_llc_use_ = 0, dbg_llc_useless_ = 0, dbg_llc_unproven_ = 0;
  void llc_credit(uint16_t iph, bool useful) { if (useful) ++dbg_llc_use_; else ++dbg_llc_useless_; if (P.enable_ip_filter) ip_credit(ipt_get(iph).llc, useful); }

  // The parent cache, captured at construction (MSHR pressure queries).
  champsim::modules::cache_module* cache_ = nullptr;

  explicit sppam_plus(champsim::modules::ModuleBuilder builder);

  void prefetcher_initialize() override;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in) override;
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in) override;
  void prefetcher_cache_evict(champsim::address evicted_addr, bool unused_prefetch) override;
  void prefetcher_cycle_operate() override { ++real_cycle_; }
  void prefetcher_final_stats() override;
  void prefetcher_branch_operate(champsim::address /*ip*/, uint8_t /*branch_type*/, champsim::address /*branch_target*/) override {}

  // ---- sppam_dse::prefetch_sink ----
  bool issue_prefetch(uint64_t block, bool fill_l2, bool from_spp, double benefit, uint32_t gen_tag = 0,
                      uint64_t pat_key = 0, int pat_bit = -1, bool backward = false, bool has_pat = false) override;
  int dram_bw_index() const override;
  int pf_free_space() const override;
  int sd_l2_limit() const override { return sd_l2_limit_value(); }
  bool bwd_is_bad_ip(uint64_t ip) const override {
    if (!P.bwd_useful_gate) return false;
    const ip_ent* e = ipt_find(iphash(ip));
    return e != nullptr && bin_lt(e->bwd, P.bwd_useful_thresh);
  }
  // Per-IP lookahead-depth cap: an UNTIMELY IP prefetches shallower as its usefulness degrades. A truly-bad IP
  // keeps full depth here (the volume trickle drops its wrong prefetches).
  int ip_depth_cap() const override {
    if (!P.enable_ip_filter || !P.ip_filter_depth_throttle) return 1 << 20;
    const ip_ent* e = ipt_find(iphash(cur_trigger_ip_));
    if (!ip_is_untimely(e) || !e->use.bvalid) return 1 << 20;
    if (bin_ge(e->use, P.ip_filter_threshold)) return 1 << 20;
    if (bin_lt(e->use, P.ip_filter_threshold_hard)) return P.ip_depth_min;
    return P.ip_depth_mid;
  }

  // ---- Adaptive ip_filter set-duel: filter-OFF (cat 0) vs filter-ON (cat 1) sample sets, hit-rate metric ----
  std::size_t ipf_rate_ = 0;                          // categorizer sample rate (set in initialize)
  uint64_t ipf_gd0=0, ipf_gh0=0, ipf_gd1=0, ipf_gh1=0; // filter-off / filter-on sample: demands, hits (windowed)
  uint64_t ipf_dem_=0; bool ipf_apply_followers_=true; // default: filter ON for followers
  uint32_t ipf_epoch_=0; bool ipf_measuring_=true;     // duty cycle: leaders live only in measurement epochs
  int ipf_category(uint64_t block) const {
    if(!P.enable_ipf_duel) return 2;
    return (int)champsim::msl::categorizer<long>(ipf_rate_).get_sample_category((long)(block % P.l2_sets));
  }
  void ipf_observe(uint64_t block, bool hit){
    if(!P.enable_ipf_duel) return;
    if(ipf_measuring_){ // leaders live: accumulate the counterfactual, but skip the first HALF of the epoch
      // so the leader sets have re-warmed under their own policy before we read them (avoids the flip transient)
      if(ipf_dem_ >= P.ipf_eval_period/2){
        const int c=ipf_category(block);
        if(c==0){ ++ipf_gd0; ipf_gh0+=hit?1:0; } else if(c==1){ ++ipf_gd1; ipf_gh1+=hit?1:0; }
      }
    }
    if(++ipf_dem_>=P.ipf_eval_period){ // epoch boundary
      ipf_dem_=0; ++ipf_epoch_;
      if(ipf_measuring_){ ipf_ratchet(); ipf_gd0=ipf_gh0=ipf_gd1=ipf_gh1=0; } // decide, then fresh read next measurement
      ipf_measuring_ = (P.ipf_duty<=1) || (ipf_epoch_ % P.ipf_duty == 0);       // measure 1 epoch in ipf_duty
    }
  }
  void ipf_ratchet(){
    const double h0 = ipf_gd0?(double)ipf_gh0/ipf_gd0:0.0; // filter-OFF sample hit rate
    const double h1 = ipf_gd1?(double)ipf_gh1/ipf_gd1:0.0; // filter-ON  sample hit rate
    // Keep-if-safe: the filter barely moves a hit rate that's significant -> reuse to protect, marginal
    // aggression not worth the pollution risk -> keep it ON. Otherwise follow the hit-rate direction:
    // big filter-off win (mcf) or a low hit rate where aggression is safe (triangle) -> disable.
    const bool barely = (h0 - h1) < P.ipf_barely;   // filter-off only marginally higher (or lower)
    const bool significant = h1 >= P.ipf_significant;
    if(ipf_gd0<64 || ipf_gd1<64) return; // too few samples this epoch -> hold the decision
    if(barely && significant) ipf_apply_followers_=true;          // protect a working cache
    else if(h0 > h1 + P.ipf_margin) ipf_apply_followers_=false;   // aggression clearly/safely helps -> drop
    else ipf_apply_followers_=true;                              // filter neutral/helpful
  }
  // Should the ip_filter throttle be applied to a prefetch targeting this block's set?
  bool ipf_should_apply(uint64_t block) const {
    if(!P.enable_ipf_duel) return true;
    if(!ipf_measuring_) return ipf_apply_followers_; // commit epoch: every set follows the decision, no leaders
    const int c=ipf_category(block);
    if(c==0) return false; // filter-off leader
    if(c==1) return true;  // filter-on leader
    return ipf_apply_followers_;
  }

  // ============ Set-dueling L2->LLC redirect throttle (P.enable_set_duel) ============
  // Tiny sample via champsim::msl::categorizer: category 0 = prefetch-guarded (its prefetches are
  // DROPPED -> the no-prefetch baseline), category 1 = usefulness-sample (its prefetches are never
  // redirected -> always L2, so the existing usefulness tracker keeps sampling under redirection),
  // categories >=2 = followers. Each epoch, compare the guard against the WHOLE cache as normalized
  // RATES (never a saturating counter). If the no-prefetch guard out-performs the cache, walk the
  // follower L2->LLC redirect fraction up one step; else down. Metric: hit rate, or eviction/fill
  // rate (over-prefetch churn). The drop/usefulness throttle is sppam's existing system, not here.
  std::size_t sd_rate_ = 0;          // categorizer sample rate (set in prefetcher_initialize)
  double sd_l2_limit_ = 1e9;         // adaptive follower L2 fill DEPTH (walked; init to sd_l2_max)
  uint64_t sd_gd_ = 0, sd_gh_ = 0, sd_ge_ = 0, sd_gu_ = 0; // guard (cat 0): demands, hits, evictions, useful-pf (windowed)
  uint64_t sd_ad_ = 0, sd_ah_ = 0, sd_ae_ = 0, sd_au_ = 0; // whole cache: demands, hits, evictions, useful-pf (windowed)
  uint32_t sd_lcg_ = 0x9e3779b9u;    // deterministic PRNG for the graded follower redirect

  int sd_category(uint64_t block) const
  {
    if (!P.enable_set_duel)
      return 2;
    return static_cast<int>(champsim::msl::categorizer<long>(sd_rate_).get_sample_category(static_cast<long>(block % P.l2_sets)));
  }
  uint32_t sd_rand() { sd_lcg_ = sd_lcg_ * 1664525u + 1013904223u; return sd_lcg_; }

  // Placement override for one prefetch. cat 0 drops (baseline), cat 1 forces L2 (usefulness sample);
  // followers keep the depth decision the predictor already made (fill_l2 = pf_issued < sd_l2_limit()).
  // Returns false to drop. No random redirect -- the action is the adaptive depth in do_prefetch.
  bool sd_decide(uint64_t block, bool& fill_l2)
  {
    if (!P.enable_set_duel)
      return true;
    const int cat = sd_category(block);
    if (cat == 0) return false;                     // prefetch-guarded: no prefetch (baseline)
    if (cat == 1) fill_l2 = true;                   // usefulness sample: always L2 (never overflow)
    return true;                                    // follower: keep the predictor's depth decision
  }
  int sd_l2_limit_value() const { return static_cast<int>(sd_l2_limit_); }

  // Per-demand observation into the windowed rate counters; ratchet every sd_eval_period demands.
  void sd_observe(uint64_t block, bool cache_hit, bool useful_prefetch)
  {
    if (!P.enable_set_duel)
      return;
    ++sd_ad_; sd_ah_ += cache_hit ? 1 : 0; sd_au_ += useful_prefetch ? 1 : 0;
    if (sd_category(block) == 0) { ++sd_gd_; sd_gh_ += cache_hit ? 1 : 0; sd_gu_ += useful_prefetch ? 1 : 0; }
    if (sd_ad_ >= P.sd_eval_period) sd_ratchet();
  }

  // Per-fill observation: an eviction (fill displaced a valid line) feeds the churn-rate metric.
  void sd_observe_fill(uint64_t fill_block, bool evicted)
  {
    if (!P.enable_set_duel || !evicted)
      return;
    ++sd_ae_;
    if (sd_category(fill_block) == 0) ++sd_ge_;
  }

  void sd_ratchet()
  {
    const double gh = sd_gd_ ? static_cast<double>(sd_gh_) / sd_gd_ : 0.0; // guard hit rate
    const double ah = sd_ad_ ? static_cast<double>(sd_ah_) / sd_ad_ : 0.0; // overall hit rate
    const double ge = sd_gd_ ? static_cast<double>(sd_ge_) / sd_gd_ : 0.0; // guard eviction rate
    const double ae = sd_ad_ ? static_cast<double>(sd_ae_) / sd_ad_ : 0.0; // overall eviction rate
    // pollution = evictions NOT paid for by a useful prefetch, per demand: subtracting useful-pf
    // credits cancels the churn of accurate prefetching (cactus), leaving true pollution (xalan).
    const double gp = sd_gd_ ? (static_cast<double>(sd_ge_) - static_cast<double>(sd_gu_)) / sd_gd_ : 0.0;
    const double ap = sd_ad_ ? (static_cast<double>(sd_ae_) - static_cast<double>(sd_au_)) / sd_ad_ : 0.0;
    bool hurts = false, helps = false;
    switch (P.sd_metric) {
      case 0: hurts = gh > ah + P.sd_margin; helps = ah > gh + P.sd_margin; break; // hit rate
      case 1: hurts = ae > ge + P.sd_margin; helps = ge > ae + P.sd_margin; break; // raw eviction rate
      default: hurts = ap > gp + P.sd_margin; helps = gp > ap + P.sd_margin; break; // pollution (evict - useful)
    }
    // Action: walk the L2 fill DEPTH. hurts (prefetch polluting) => shallower L2 (more of the deep,
    // speculative tail overflows to LLC); helps => deeper L2. Keeps the timely prefetches in L2.
    if (hurts) sd_l2_limit_ = std::max(static_cast<double>(P.sd_l2_floor), sd_l2_limit_ - 1.0);
    else if (helps) sd_l2_limit_ = std::min(static_cast<double>(P.sd_l2_max), sd_l2_limit_ + 1.0);
    sd_gd_ >>= 1; sd_gh_ >>= 1; sd_ge_ >>= 1; sd_gu_ >>= 1;
    sd_ad_ >>= 1; sd_ah_ >>= 1; sd_ae_ >>= 1; sd_au_ >>= 1; // window
  }
};

#endif
