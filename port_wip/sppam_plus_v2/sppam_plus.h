#ifndef PREFETCHER_SPPAM_PLUS_H
#define PREFETCHER_SPPAM_PLUS_H

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
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
struct sppam_plus : public champsim::modules::prefetcher, public sppam_dse::prefetch_sink {
  sppam_dse::params P;
  std::unique_ptr<sppam_dse::sppam_predictor> pred_;
  std::unique_ptr<sppam_dse::spp_predictor> spp_;
  // Branch-graph instruction prefetcher (constructed only when enable_instr_prefetch).
  // Sees the L1I-filtered instruction miss stream; issues into L2 on physical addresses.
  std::unique_ptr<sppam_dse::iprefetch_predictor> ipred_;
  // Instruction prefetches not yet demand-used (block -> 1), for coverage/accuracy stats.
  // Resolved useful on an instruction demand hit, useless on eviction. Separate from the
  // data path's pf_unused_ so the two streams' accuracy never mix.
  std::unordered_map<uint64_t, uint8_t> instr_pf_unused_;
  uint64_t instr_useful_ = 0;   // instruction prefetch later demand-hit before eviction
  uint64_t instr_useless_ = 0;  // instruction prefetch evicted before any demand use

  uint64_t cycle_ = 0;          // monotonic operate counter (predictor timing)
  uint64_t cur_trigger_ip_ = 0; // trigger PC of the current demand access (for per-IP attribution)
  bool sppam_fired_ = false;    // per-trigger latch: did SPPAM claim this access?
  uint64_t spp_ft_ctr_ = 0;     // fall-through explore trickle counter
  uint64_t pf_issued_ = 0;      // stats
  uint64_t pf_squashed_redundant_ = 0; // prefetches dropped pre-issue by the shadow-residency filter
  uint64_t pf_pass_region_absent_ = 0; // filter passed: block's region not in the table
  uint64_t pf_pass_bit_clear_ = 0;     // filter passed: region present but residency bit clear
  // Blocks we prefetched that a demand has not yet used, block -> source (0=SPPAM,1=SPP).
  // Reconstructs the "evicted unused prefetch" signal ChampSim's fill hook does not expose.
  std::unordered_map<uint64_t, uint32_t> pf_unused_; // block -> generation tag (src|depth|order|scan)
  // --- accuracy characterization (per source: 0=SPPAM 1=SPP) ---
  uint64_t char_useful_[2] = {0,0};   // [src] prefetched block later demand-hit before eviction
  uint64_t char_useless_[2] = {0,0};  // [src] prefetched block evicted before any demand use
  uint64_t char_timely_[2] = {0,0};   // [src] of useless: block demanded after eviction (untimely, addr right)
  uint64_t useful_by_depth_[2][16] = {};   // [src][lookahead depth]
  uint64_t useless_by_depth_[2][16] = {};
  uint64_t useful_by_order_[8] = {};       // SPPAM pattern order
  uint64_t useless_by_order_[8] = {};
  uint64_t useful_by_scan_[8] = {};        // SPPAM scan distance (trigger offset from demand)
  uint64_t useless_by_scan_[8] = {};
  // Per-trigger-IP usefulness (used vs evicted-unused). Attribution rides the SHARED pfht_ sample
  // table (merged with PE): an entry is PINNED from issue until it resolves -- freed on demand hit
  // (useful) or line eviction (useless). New samples take only FREE slots (never overwrite an
  // in-flight entry -> no bias toward short-lived/useful); only a STALE in-flight entry is reclaimed,
  // counted useless (it sat resident, unused, past its welcome). No per-line metadata.
  static constexpr int IPBK = 4096;
  std::vector<uint32_t> ip_useful_ = std::vector<uint32_t>(IPBK, 0);
  std::vector<uint32_t> ip_useless_ = std::vector<uint32_t>(IPBK, 0);
  // EXACT per-ISSUING-IP timeliness (fixes the sampled undercount): carry the issuing IP from issue ->
  // eviction -> re-demand. ip_ev_ = evicted-unused prefetches from this IP; ip_untimely_ = of those, how
  // many a demand later re-accessed (right addr, too early). Truly-bad = ip_ev_ - ip_untimely_ (never re-hit).
  std::vector<uint32_t> ip_ev_ = std::vector<uint32_t>(IPBK, 0);       // evicted-unused count per issuing IP
  std::vector<uint32_t> ip_untimely_ = std::vector<uint32_t>(IPBK, 0); // of those, later re-demanded (untimely)
  std::unordered_map<uint64_t, uint16_t> pf_issue_iph_; // issued (in-flight/resident) block -> issuing-IP hash
  std::unordered_map<uint64_t, uint16_t> pf_evict_iph_; // evicted-unused block -> issuing-IP hash (untimely-watch)
  uint64_t l2_dem_acc_ = 0, l2_dem_hit_ = 0; // running L2 demand hit rate (cache-stress gate for depth-throttle)
  uint64_t occ_n_ = 0, occ_tot_ = 0, occ_up_ = 0; // MLP measurement: avg total MSHR occ / upstream(demand+berti) occ
  double l2_hit_rate() const { return l2_dem_acc_ > 4096 ? static_cast<double>(l2_dem_hit_) / l2_dem_acc_ : 1.0; }
  double avg_upstream_occ() const { return occ_n_ > 4096 ? static_cast<double>(occ_up_) / occ_n_ : 0.0; } // inherent MLP
  // Alternative per-IP metric: PE = I_UPF - I_POLL - I_LAT accumulated PER TRIGGER IP (latency units).
  // Captures coverage-vs-pollution (which usefulness misses): a coverage-limited IP stays PE>0 and is
  // spared; a pollution-limited IP goes PE<0 and is throttled. Selected by P.ip_filter_use_pe.
  std::vector<double> ip_pe_ = std::vector<double>(IPBK, 0.0);
  std::vector<uint32_t> ip_pe_n_ = std::vector<uint32_t>(IPBK, 0);
  // Per-IP per-phase harm census (ip_filter_use_pe_phase): snapshot each IP's PE at phase boundaries and
  // count the fraction of ACTIVE phases where its per-phase PE went negative (unconfounded by averaging).
  std::vector<double> snap_ip_pe_ = std::vector<double>(IPBK, 0.0);
  std::vector<uint32_t> snap_ip_pe_n_ = std::vector<uint32_t>(IPBK, 0);
  std::vector<uint32_t> ip_ph_harm_ = std::vector<uint32_t>(IPBK, 0);   // active phases this IP was net-harmful
  std::vector<uint32_t> ip_ph_active_ = std::vector<uint32_t>(IPBK, 0); // active phases (any PE activity)
  uint64_t ip_trickle_ctr_ = 0;             // 1/N pass-through when a trigger IP is throttled
  uint64_t ip_age_ctr_ = 0;                 // outcomes since last backoff eval / halving
  bool ip_filter_active_ = true;            // auto-backoff: set false by ip_age_tick when IP stops discriminating
  uint64_t dbg_ins_=0, dbg_skip_=0, dbg_use_=0, dbg_evict_=0; // sample-table diagnostics
  std::unordered_map<uint64_t, uint32_t> pf_evicted_unused_; // evicted-unused block -> tag, to detect later demand

  // ---- I-POP-style Prefetch-Effectiveness throttle: PE = I_UPF - I_POLL - I_LAT ----
  // Per source (index 0 = SPPAM, 1 = SPP), in real-cycle latency units. Attribution is
  // SAMPLED (1/pe_sample_div of issued prefetches) into small direct-mapped holding
  // tables -- the same anti-thrash trick as the SPP per-sig filter: few live entries, so
  // each survives long enough to resolve, and the PE *sign* is invariant to the sampling
  // scale. Every pe_phase demands we score PE/source; any source with PE<=0 is throttled
  // to 1/pe_throttle_div (never fully gated -- a gate kills its own PE signal).
  struct pf_track {
    bool valid = false;
    uint64_t block = 0;
    bool from_spp = false;
    uint64_t issue = 0;  // real cycle the prefetch was issued
    bool filled = false; // fill observed -> lat valid
    uint64_t lat = 0;    // measured issue->fill latency (real cycles)
    uint16_t iph = 0;    // trigger-IP hash (per-IP accuracy filter shares this sampled table)
  };
  struct poll_track {
    bool valid = false;
    uint64_t block = 0;  // a useful line this prefetch evicted; a later demand miss on it is pollution
    bool from_spp = false;
    uint16_t iph = 0;    // trigger-IP hash of the prefetch that evicted this victim (per-IP PE)
  };
  std::vector<pf_track> pfht_;       // sampled in-flight/resident prefetch tracker (size pfht_entries)
  std::vector<poll_track> poll_;     // sampled pollution victims (size pfht_entries)
  uint64_t pe_sample_ctr_ = 0;       // 1/pe_sample_div issue sampler
  uint64_t real_cycle_ = 0;          // true per-cycle counter (fill-latency clock)
  uint64_t lat_sum_ = 0, lat_n_ = 0; // running-mean fill latency (pollution-miss cost proxy)

  // In-flight demand-stream misses, for the I_LAT gate (I-POP Eq4 charges a prefetch's
  // service time only when a demand sits in the MSHR as its data returns). The stream =
  // all L2 accesses; a true load/RFO miss is a "true" demand, berti's PREFETCH-type miss is
  // a weighted demand. The map is the dedup membership (a stalled miss re-fires operate);
  // the two counts give the gate and pick the MAX-affected demand's weight in O(1).
  std::unordered_map<uint64_t, bool> inflight_demand_; // block -> is_true_demand
  uint64_t inflight_true_ = 0;       // # in-flight true (load/RFO) demand misses
  uint64_t inflight_berti_ = 0;      // # in-flight berti prefetch-access misses

  double i_upf_[2] = {0.0, 0.0};     // latency SAVED by useful prefetches (Eq2)
  double i_poll_[2] = {0.0, 0.0};    // pollution-miss latency: pf evicts useful -> later demand miss (Eq3)
  double i_lat_[2] = {0.0, 0.0};     // service time our prefetches impose on in-flight demands (Eq4)
  double snap_upf_[2] = {0.0, 0.0}, snap_poll_[2] = {0.0, 0.0}, snap_lat_[2] = {0.0, 0.0};
  int pe_throttle_[2] = {1, 1};      // 1 = full issue, pe_throttle_div = issue 1/N
  uint64_t pe_pf_count_[2] = {0, 0}; // per-source throttle-application counter
  uint64_t pe_phase_demands_ = 0;    // demands since last PE evaluation
  uint64_t pe_phases_[2] = {0, 0}, pe_neg_phases_[2] = {0, 0}; // per-phase PE sign census (diagnostic)
  uint64_t pe_negact_[2] = {0, 0}, pe_active_[2] = {0, 0}; // strict PE<0 AND real prefetch activity this phase

  double avg_lat() const { return lat_n_ ? static_cast<double>(lat_sum_) / static_cast<double>(lat_n_) : 0.0; }
  static double access_weight(access_type t, double pf_w) { return (t == access_type::PREFETCH) ? pf_w : 1.0; }

  // The parent cache, captured at construction (MSHR pressure queries).
  champsim::modules::cache_module* cache_ = nullptr;

  explicit sppam_plus(champsim::modules::ModuleBuilder builder);

  void prefetcher_initialize() override;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in) override;
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in) override;
  void prefetcher_cycle_operate() override { ++real_cycle_; }
  void prefetcher_final_stats() override;
  void prefetcher_branch_operate(champsim::address /*ip*/, uint8_t /*branch_type*/, champsim::address /*branch_target*/) override {}

  // ---- sppam_dse::prefetch_sink ----
  bool issue_prefetch(uint64_t block, bool fill_l2, bool from_spp, double benefit, uint32_t gen_tag = 0) override;
  int dram_bw_index() const override;
  int pf_free_space() const override;
  int sd_l2_limit() const override { return sd_l2_limit_value(); }
  bool pe_ramp_active_ = false; // SPPAM net-useful (PE>min) AND DRAM-bound (avg_lat>min)
  bool pe_ramp_active() const override { return pe_ramp_active_; }
  // Is this IP's uselessness dominated by UNTIMELY (right-addr-too-early) rather than truly-bad prefetches?
  bool ip_is_untimely(uint32_t iph) const {
    if (l2_hit_rate() < P.ip_depth_hitrate_min) return false; // cache-stress gate: low hit rate -> timeliness moot
    if (P.ip_depth_mlp_max > 0.0 && avg_upstream_occ() > P.ip_depth_mlp_max) return false; // MLP gate: program
      // already parallel (high inherent demand+berti MSHR occ) -> deep prefetches are parallelism, not waste
    const uint32_t l = ip_ev_[iph]; // EXACT evicted-unused count for this issuing IP
    return l >= P.ip_filter_min_samples &&
           static_cast<uint64_t>(ip_untimely_[iph]) * 100 >= static_cast<uint64_t>(P.ip_untimely_thresh) * l;
  }
  // Per-IP lookahead-depth cap (depth throttle): shallower as the trigger IP's usefulness degrades -- but
  // ONLY for UNTIMELY IPs. A truly-bad IP gets full depth here (the volume trickle drops its wrong prefetches).
  int ip_depth_cap() const override {
    if (!P.enable_ip_filter || !P.ip_filter_depth_throttle) return 1 << 20;
    const uint32_t iph = iphash(cur_trigger_ip_);
    if (!ip_is_untimely(iph)) return 1 << 20; // truly-bad -> full depth, handled by the volume trickle
    const uint32_t u = ip_useful_[iph], l = ip_useless_[iph];
    const uint64_t tot = static_cast<uint64_t>(u) + l;
    if (tot < P.ip_filter_min_samples) return 1 << 20;                                 // too few samples -> full depth
    const uint64_t pct = static_cast<uint64_t>(u) * 100;
    if (pct >= static_cast<uint64_t>(P.ip_filter_threshold) * tot) return 1 << 20;     // >= soft usefulness -> full
    if (pct < static_cast<uint64_t>(P.ip_filter_threshold_hard) * tot) return P.ip_depth_min; // near-dead -> shallowest
    return P.ip_depth_mid;                                                             // mid -> medium depth
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
  // Halve the per-IP usefulness counters every 2^ip_filter_age_shift outcomes (adaptivity).
  // Graded throttle divisor for a trigger IP: 1 = issue fully; >1 = issue only 1/div. Harsh at very
  // low usefulness (near-dead pointer chases), light at mid usefulness, none above the soft threshold.
  // PE-metric graded divisor: throttle IPs whose per-prefetch PE (latency saved minus pollution/induced
  // latency) is negative; harsh when strongly negative relative to the mean fill latency.
  uint32_t ip_pe_trickle_div(uint32_t iph) const {
    const uint32_t n = ip_pe_n_[iph];
    if (n < P.ip_filter_min_samples) return 1;
    const double rate = ip_pe_[iph] / n;                 // avg PE per resolved prefetch
    if (rate >= 0.0) return 1;                            // net-beneficial -> full issue
    const double al = avg_lat() > 1.0 ? avg_lat() : 1.0;  // scale thresholds by mean fill latency
    if (rate < -P.ip_pe_hard_frac * al)
      return static_cast<uint32_t>(P.ip_filter_trickle_hard ? P.ip_filter_trickle_hard : 1); // strongly harmful
    return static_cast<uint32_t>(P.ip_filter_trickle ? P.ip_filter_trickle : 1);             // mildly harmful
  }
  bool ip_pe_positive(uint32_t iph) const { // clearly net-beneficial PE -> spare from throttling
    const uint32_t n = ip_pe_n_[iph];
    if (n < P.ip_filter_min_samples) return false;
    const double al = avg_lat() > 1.0 ? avg_lat() : 1.0;
    return (ip_pe_[iph] / n) > P.ip_pe_veto_frac * al;
  }
  // Per-IP per-phase harm throttle: fraction of this IP's ACTIVE phases with net-negative PE. Mirrors
  // ip_trickle_div's graded shape, but keyed on temporal harm-consistency instead of usefulness.
  uint32_t ip_pe_phase_trickle_div(uint32_t iph) const {
    const uint32_t a = ip_ph_active_[iph];
    if (a < P.ip_filter_min_samples) return 1;                                   // too few active phases -> full
    // Guard: only throttle an IP that is ALSO net-negative on average. Frequent per-phase harm on a
    // net-useful IP is DRAM-contention noise (cactus), not real harm -- the avg-PE sign disambiguates.
    const uint32_t n = ip_pe_n_[iph];
    const double al = avg_lat() > 1.0 ? avg_lat() : 1.0;
    if (n == 0 || ip_pe_[iph] / n >= -P.ip_pe_phase_margin * al) return 1;      // net-useful (or only marginally negative) -> spare
    const uint64_t pct = static_cast<uint64_t>(ip_ph_harm_[iph]) * 100;
    if (pct < static_cast<uint64_t>(P.ip_pe_phase_soft) * a) return 1;           // low harm -> full issue
    if (pct >= static_cast<uint64_t>(P.ip_pe_phase_hard) * a)
      return static_cast<uint32_t>(P.ip_filter_trickle_hard ? P.ip_filter_trickle_hard : 1); // high harm -> harsh
    return static_cast<uint32_t>(P.ip_filter_trickle ? P.ip_filter_trickle : 1);             // mid -> light
  }
  // Phase boundary: census each IP's per-phase PE delta (active iff it moved), then re-snapshot.
  void ip_pe_phase_tick() {
    for (int i = 0; i < IPBK; ++i) {
      const double d = ip_pe_[i] - snap_ip_pe_[i];
      const bool active = (d != 0.0) || (ip_pe_n_[i] != snap_ip_pe_n_[i]);
      if (active) { ++ip_ph_active_[i]; if (d < 0.0) ++ip_ph_harm_[i]; }
      snap_ip_pe_[i] = ip_pe_[i]; snap_ip_pe_n_[i] = ip_pe_n_[i];
    }
  }
  uint32_t ip_trickle_div(uint32_t iph) const {
    const uint32_t u = ip_useful_[iph], l = ip_useless_[iph];
    const uint64_t tot = u + l;
    if (tot < P.ip_filter_min_samples) return 1;                                        // too few samples -> full
    const uint64_t pct = static_cast<uint64_t>(u) * 100;
    if (pct >= static_cast<uint64_t>(P.ip_filter_threshold) * tot) return 1;            // >= soft -> full
    if (pct < static_cast<uint64_t>(P.ip_filter_threshold_hard) * tot)
      return static_cast<uint32_t>(P.ip_filter_trickle_hard ? P.ip_filter_trickle_hard : 1); // near-dead -> harsh
    return static_cast<uint32_t>(P.ip_filter_trickle ? P.ip_filter_trickle : 1);        // mid -> light
  }
  void ip_age_tick() {
    ++ip_age_ctr_;
    // Auto-backoff (every 64k outcomes): would-be USEFUL-loss if we throttled all sub-threshold IPs.
    // If it exceeds the budget, IP is NOT separating good from bad (e.g. cc5) -> disable the filter.
    if ((ip_age_ctr_ & 0xFFFF) == 0 && !P.ip_filter_use_pe_phase) { // usefulness backoff N/A to the pe-phase metric
      uint64_t tot = 0, lost = 0;
      for (int i = 0; i < IPBK; ++i) {
        tot += ip_useful_[i];
        const uint32_t d = ip_trickle_div(static_cast<uint32_t>(i));
        if (d > 1) lost += static_cast<uint64_t>(ip_useful_[i]) * (d - 1) / d; // fraction of useful dropped
      }
      if (tot) ip_filter_active_ = (100 * lost < static_cast<uint64_t>(P.ip_filter_max_useful_loss) * tot);
    }
    if ((ip_age_ctr_ & ((uint64_t{1} << P.ip_filter_age_shift) - 1)) == 0)
      for (int i = 0; i < IPBK; ++i) { ip_useful_[i] >>= 1; ip_useless_[i] >>= 1; ip_ev_[i] >>= 1; ip_untimely_[i] >>= 1; ip_pe_[i] *= 0.5; ip_pe_n_[i] >>= 1;
        snap_ip_pe_[i] *= 0.5; snap_ip_pe_n_[i] >>= 1; ip_ph_harm_[i] >>= 1; ip_ph_active_[i] >>= 1; } // decay phase census too
  }
  static uint32_t iphash(uint64_t ip) { return static_cast<uint32_t>((ip * 0x9E3779B97F4A7C15ull) >> 52) & 0xFFFu; }

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
  double sd_pe_snap_ = 0.0;          // last-epoch PE total (for the PE metric delta)
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
      case 2: hurts = ap > gp + P.sd_margin; helps = gp > ap + P.sd_margin; break; // pollution (evict - useful)
      default: { // 3: I-POP PE = I_UPF - I_POLL - I_LAT (I_LAT captures fill-bandwidth pressure)
        const double pe = (i_upf_[0] + i_upf_[1]) - (i_poll_[0] + i_poll_[1]) - (i_lat_[0] + i_lat_[1]);
        const double per = sd_ad_ ? (pe - sd_pe_snap_) / sd_ad_ : 0.0; // net latency benefit / demand this epoch
        sd_pe_snap_ = pe;
        hurts = per < -P.sd_margin; helps = per > P.sd_margin;
      } break;
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
