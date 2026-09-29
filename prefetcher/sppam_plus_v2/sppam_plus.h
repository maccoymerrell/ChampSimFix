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
  std::unique_ptr<sppam_dse::iprefetch_predictor> ipred_;  // instruction walk (L1I-miss control flow) -- committed, kept pristine
  std::unique_ptr<sppam_dse::iprefetch_predictor> dpred_;  // SEPARATE data BG for the per-region PC-sequence lookahead (Idea A) -- no shared-table contamination
  // Branch-graph (instruction) prefetches in flight, so their fills stay out of the data maps: a small direct-mapped
  // table of block numbers (bg_inflight_entries), cleared at the fill. A collision loses the older entry, whose fill
  // is then treated as a data fill.
  std::vector<uint64_t> bg_inflight_;
  bool bg_inflight_take(uint64_t block);
  uint64_t instr_useful_ = 0;   // instruction demand hit an unused prefetched line (the cache's own prefetch bit)
  uint64_t instr_useless_ = 0;  // unused prefetched code line evicted
  uint64_t instr_pf_issued_ = 0, data_pf_issued_ = 0; // prefetch-issue counts -> instruction fraction fed to the perceptron

  uint64_t cycle_ = 0;          // monotonic operate counter (predictor timing)
  uint64_t cur_trigger_ip_ = 0; // trigger PC of the current demand access (for per-IP attribution)
  bool sppam_fired_ = false;    // per-trigger latch: did SPPAM claim this access?
  uint64_t spp_ft_ctr_ = 0;     // fall-through explore trickle counter
  uint64_t pf_issued_ = 0;      // stats
  uint64_t pf_squashed_redundant_ = 0; // prefetches dropped pre-issue by the shadow-residency filter
  uint64_t pf_pass_region_absent_ = 0; // filter passed: block's region not in the table
  uint64_t pf_pass_bit_clear_ = 0;     // filter passed: region present but residency bit clear
  // The eviction the cache reported just before the fill that caused it, with the cache's per-line prefetch bit.
  uint64_t last_evict_block_ = 0;
  bool last_evict_unused_ = false;
  // LLC-only prefetch samples, separate from the L2 sampling table: sparser (1/llc_sample_div) and pinned far longer
  // (llc_track_timeout), since an LLC line waits much longer for its use than an L2 line. An entry follows its line
  // if a later prefetch moves it into L2 (in_l2), and then resolves on that L2 copy's fate. Outcomes go to the
  // predictor's per-IP table.
  struct llc_track {
    bool valid = false;
    bool in_l2 = false;
    uint64_t block = 0;
    uint64_t issue = 0;
    uint16_t iph = 0;
  };
  std::vector<llc_track> llcs_;
  uint64_t llc_sample_ctr_ = 0;
  uint64_t dbg_llc_redirect_ = 0, dbg_llc_dup_ = 0, dbg_llc_use_ = 0, dbg_llc_useless_ = 0;
  uint64_t l2_dem_acc_ = 0, l2_dem_hit_ = 0; // running L2 demand hit rate (cache-stress gate for depth-throttle)
  uint64_t occ_n_ = 0, occ_tot_ = 0, occ_up_ = 0; // MLP measurement: avg total MSHR occ / upstream(demand+berti) occ
  double l2_hit_rate() const { return l2_dem_acc_ > 4096 ? static_cast<double>(l2_dem_hit_) / l2_dem_acc_ : 1.0; }
  double avg_upstream_occ() const { return occ_n_ > 4096 ? static_cast<double>(occ_up_) / occ_n_ : 0.0; } // inherent MLP

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
    uint32_t sig = 0;    // engine signature of the request (enable_sig_feedback: per-signature outcome attribution)
    uint8_t eng = 0;     // engine that issued it (0 fwd / 1 bwd / 2 delta-SPP / 3 BG)
    uint8_t order = 0;   // SPPAM PHT order (which pattern-size table) that predicted this block
    uint8_t pos = 0;     // prediction_counter offset within that PHT entry (for the targeted uselessness kill)
  };
  struct poll_track {
    bool valid = false;
    uint64_t block = 0;  // a useful line this prefetch evicted; a later demand miss on it is pollution
    uint64_t pf_block = 0; // the prefetch that evicted it (to charge I_POLL to its perceptron reward)
    bool from_spp = false;
    uint16_t iph = 0;    // trigger-IP hash of the prefetch that evicted this victim (per-IP PE)
    uint32_t sig = 0;    // engine signature of the evicting prefetch (per-signature pollution attribution)
    uint8_t eng = 0;     // engine that issued the evicting prefetch
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
  bool perc_gate(uint64_t block, int engine, int depth, uint64_t pc, int conf, uint64_t sig) override; // gate the separate SPP engine through the perceptron
  void perc_note_issue_ext(uint64_t block) override;
  int dram_bw_index() const override;
  int pf_free_space() const override;
  int sd_l2_limit() const override { return sd_l2_limit_value(); }
  bool pe_ramp_active_ = false; // SPPAM net-useful (PE>min) AND DRAM-bound (avg_lat>min)
  bool pe_ramp_active() const override { return pe_ramp_active_; }
  // Depth throttle meta-gates: untimeliness matters only when the L2 is not thrashing (demand hit rate >=
  // ip_depth_hitrate_min) and the program is not already memory-parallel (upstream MSHR occupancy <= ip_depth_mlp_max).
  bool depth_throttle_allowed() const override
  {
    if (l2_hit_rate() < P.ip_depth_hitrate_min) return false;
    return !(P.ip_depth_mlp_max > 0.0 && avg_upstream_occ() > P.ip_depth_mlp_max);
  }
  void on_spp_sample(uint64_t block, bool useful) override { if (spp_) spp_->reward(useful, block); }
  void prefetcher_cache_evict(champsim::address evicted_addr, bool unused_prefetch) override;

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
