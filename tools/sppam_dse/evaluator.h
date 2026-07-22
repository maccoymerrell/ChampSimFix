// Evaluation hierarchy. Each core has a private L2 + SPPAM predictor; cores in a
// "system" share one LLC (residency filter, capacity scaled with core count) and
// one DRAM bandwidth->latency model, so multi-core runs capture LLC capacity
// contention and shared-DRAM bandwidth contention. A paired no-prefetch baseline
// system (same geometry) defines the demand-miss set and no-prefetch latency.
#ifndef SPPAM_DSE_EVALUATOR_H
#define SPPAM_DSE_EVALUATOR_H

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include "cache.h"
#include "dram_model.h"
#include "params.h"
#include "spp_predictor.h"
#include "sppam_predictor.h"

namespace sppam_dse
{

struct metrics {
  uint64_t demands = 0;
  uint64_t misses = 0;
  // Validation: aggregate over ALL processed L2 accesses (every type, matching
  // ChampSim's cpu0_L2C stats), to anchor the surrogate against real ChampSim.
  uint64_t l2_acc_all = 0;        // all processed L2 accesses
  uint64_t l2_miss_all = 0;       // ...that missed (went to LLC/DRAM)
  double sum_miss_lat_all = 0;    // sum of miss latencies -> avg L2 miss latency
  uint64_t dram_fetches = 0;      // misses served by DRAM (not LLC)
  double dram_lat_sum = 0;        // sum of DRAM-served miss latencies
  uint64_t first_cycle = 0, last_cycle = 0; // trace cycle span (for DRAM request rate)
  uint64_t covered_timely = 0;
  uint64_t covered_direct = 0; // ...of which the cfg-hit was actually an UNUSED PREFETCH (real prefetch coverage)
  uint64_t covered_late = 0;
  uint64_t pf_issued = 0;
  uint64_t pf_useful = 0;
  uint64_t pf_useless = 0;
  uint64_t pf_dropped_market = 0; // prefetches the bandwidth market outbid
  uint64_t pf_squashed = 0;       // prefetches squashed by the shadow cache (already resident)
  uint64_t pf_fallthrough = 0;    // SPP prefetches dropped because SPPAM owned the trigger
  uint64_t pf_dropped_bwrank = 0; // prefetches dropped by the rank-order bandwidth throttle
  uint64_t pf_dropped_thrash = 0; // prefetches dropped by the region-thrash throttle
  double sum_demand_latency = 0;
  uint64_t region_demand_accesses = 0; // demand accesses seen by the predictor
  uint64_t region_demand_misses = 0;   // ...whose page region was not resident
  uint64_t region_evictions = 0;       // region-table capacity evictions (staging-split metric)
  uint64_t staging_drops = 0;          // demand accesses held out of the table by staging
  // Prefetch Effectiveness components per prefetcher [0]=SPPAM, [1]=SPP (I-POP):
  // PE = i_upf - i_poll - i_lat (useful latency saved - pollution-miss latency -
  // contention delay). Instrumented to measure whether pollution/contention matter.
  double i_upf[2] = {0, 0};
  double i_poll[2] = {0, 0};
  double i_lat[2] = {0, 0};
  uint64_t poll_misses[2] = {0, 0}; // count of pollution-induced demand misses
  // Sparse-tail probe: SPPAM prefetches bucketed by SOURCE region density at issue
  // [0]=<=4, 1=<=8, 2=<=16, 3=<=32, 4=<=64 blocks. Answers "do sparse trigger pages
  // yield useful prefetches, or only useless ones (-> just drop them)."
  uint64_t pf_useful_dens[5] = {0, 0, 0, 0, 0};
  uint64_t pf_useless_dens[5] = {0, 0, 0, 0, 0};
  void operator+=(const metrics& o)
  {
    for (int i = 0; i < 5; ++i) { pf_useful_dens[i] += o.pf_useful_dens[i]; pf_useless_dens[i] += o.pf_useless_dens[i]; }
    demands += o.demands; misses += o.misses; covered_timely += o.covered_timely; covered_direct += o.covered_direct; covered_late += o.covered_late;
    l2_acc_all += o.l2_acc_all; l2_miss_all += o.l2_miss_all; sum_miss_lat_all += o.sum_miss_lat_all;
    dram_fetches += o.dram_fetches; dram_lat_sum += o.dram_lat_sum;
    if (first_cycle == 0 || (o.first_cycle && o.first_cycle < first_cycle)) first_cycle = o.first_cycle;
    if (o.last_cycle > last_cycle) last_cycle = o.last_cycle;
    pf_issued += o.pf_issued; pf_useful += o.pf_useful; pf_useless += o.pf_useless; pf_dropped_market += o.pf_dropped_market; pf_squashed += o.pf_squashed; pf_fallthrough += o.pf_fallthrough; pf_dropped_bwrank += o.pf_dropped_bwrank; pf_dropped_thrash += o.pf_dropped_thrash; sum_demand_latency += o.sum_demand_latency;
    region_demand_accesses += o.region_demand_accesses; region_demand_misses += o.region_demand_misses;
    region_evictions += o.region_evictions; staging_drops += o.staging_drops;
    for (int i = 0; i < 2; ++i) { i_upf[i] += o.i_upf[i]; i_poll[i] += o.i_poll[i]; i_lat[i] += o.i_lat[i]; poll_misses[i] += o.poll_misses[i]; }
  }
};

struct access_out {
  double latency = 0;
  bool hit = false;
};

// LLC + DRAM, shared by all cores in a system.
class shared_mem
{
public:
  shared_mem(std::size_t llc_sets, std::size_t llc_ways, const dram_params& dp, bool timing, uint64_t llc_hit_lat,
             bool market, double market_target, double market_step, std::size_t ncores,
             const params& p)
      : llc_(llc_sets, llc_ways), dram_(dp), timing_(timing), llc_hit_latency_(llc_hit_lat), base_dram_latency_(dp.base_latency),
        market_(market), market_target_(market_target), market_step_(market_step),
        crosscore_(ncores > 1 ? static_cast<double>(ncores - 1) / static_cast<double>(ncores) : 0.0),
        ncores_(ncores ? ncores : 1),
        // Rank throttle is a multi-core mechanism (single core has no rivals to rank).
        rank_(p.enable_bw_rank && ncores_ > 1), rank_epoch_(p.bw_rank_epoch),
        rank_lo_(p.bw_rank_lo), rank_hi_(p.bw_rank_hi), rank_strength_(p.bw_rank_strength),
        rank_consume_(ncores_, 0), rank_throttle_(ncores_, 0.0)
  {
  }

  // Rank-order bandwidth throttle. A core reports each DRAM access it causes; once
  // per epoch we rank cores by consumption and assign a geometric prefetch-drop by
  // rank (top 0.5, next 0.25, ... x contention pressure), then reset the counters.
  void dram_account(int core)
  {
    if (!rank_)
      return;
    rank_consume_[core]++;
    if (++rank_epoch_ctr_ < rank_epoch_)
      return;
    rank_epoch_ctr_ = 0;
    // Only throttle when DRAM is actually contended; ramp 0->1 across [lo, hi].
    double util = dram_.utilization();
    double pressure = (util - rank_lo_) / (rank_hi_ - rank_lo_);
    pressure = pressure < 0.0 ? 0.0 : (pressure > 1.0 ? 1.0 : pressure);
    // Rank core indices by last epoch's consumption, descending (tiny: ncores).
    std::array<int, 64> idx{};
    for (std::size_t i = 0; i < ncores_; ++i) idx[i] = static_cast<int>(i);
    for (std::size_t i = 1; i < ncores_; ++i) { // insertion sort, ncores is small
      int v = idx[i]; std::size_t j = i;
      while (j > 0 && rank_consume_[idx[j - 1]] < rank_consume_[v]) { idx[j] = idx[j - 1]; --j; }
      idx[j] = v;
    }
    double frac = 0.5; // rank 0 -> 0.5, rank 1 -> 0.25, ...
    for (std::size_t r = 0; r < ncores_; ++r) {
      rank_throttle_[idx[r]] = pressure * rank_strength_ * frac;
      frac *= 0.5;
    }
    std::fill(rank_consume_.begin(), rank_consume_.end(), 0);
  }
  // Deterministic fractional prefetch drop for a core (Bresenham accumulator lives
  // in the core). Returns the core's current drop fraction in [0,1).
  double rank_drop_frac(int core) const { return rank_ ? rank_throttle_[core] : 0.0; }

  uint64_t ready(uint64_t cycle, double lat) const { return timing_ ? cycle + static_cast<uint64_t>(lat) : cycle; }

  // Bandwidth market: should a prefetch of this benefit ([0,1]) be admitted? The
  // clearing price IS demand's claim on bandwidth -- a prefetch is admitted only
  // if its expected usefulness out-bids the fraction of bandwidth demand traffic
  // is consuming. market_target = the demand utilization at which prefetches are
  // fully priced out. Demand-anchored, so admission is RELATIVE across cores: the
  // lowest-benefit prefetches system-wide are squeezed out first under load.
  bool admit(double benefit)
  {
    if (!market_)
      return true;
    // A prefetch competes only with OTHER cores' demands -- those are the real
    // rivals for shared bandwidth. Clear against the cross-core fraction of demand
    // utilization. Single-core has no other core (crosscore_=0) -> threshold 0 ->
    // always admit, so a prefetcher can bootstrap (no cold-start death spiral).
    double threshold = crosscore_ * dram_.demand_utilization() / market_target_;
    if (threshold > 1.0) threshold = 1.0;
    return benefit >= threshold;
  }

  // Latency to fetch a block into an L2: LLC hit if resident & ready, else DRAM
  // (which also populates the LLC). DRAM utilization aggregates all cores.
  double fetch_latency(uint64_t block, uint64_t cycle, bool is_demand = true, bool* served_dram = nullptr, bool* delayed_demand = nullptr)
  {
    const cache_line* l = llc_.probe(block);
    if (l != nullptr && (!timing_ || l->ready_at <= cycle)) {
      bool useful_pf = false;
      llc_.demand_access(block, cycle, useful_pf);
      if (served_dram) *served_dram = false;
      return static_cast<double>(llc_hit_latency_);
    }
    double lat = dram_.access(cycle, is_demand);
    uint64_t done = ready(cycle, lat);
    llc_.install(block, cycle, done, false);
    if (served_dram) *served_dram = true;
    dram_lat_sum_ += lat; dram_lat_n_++; // running mean -> L_DRAM for PE
    if (is_demand) {
      // Track the latest in-flight demand DRAM request (MSHR-style).
      if (done > last_demand_dram_done_) last_demand_dram_done_ = done;
    } else if (delayed_demand) {
      // A prefetch delays a demand only if a demand DRAM fetch is still in flight.
      *delayed_demand = timing_ && cycle < last_demand_dram_done_;
    }
    return lat;
  }
  double avg_dram_latency() const { return dram_lat_n_ ? dram_lat_sum_ / static_cast<double>(dram_lat_n_) : base_dram_latency_; }
  double base_dram_latency() const { return base_dram_latency_; }

  void llc_install(uint64_t block, uint64_t cycle, double lat) { llc_.install(block, cycle, ready(cycle, lat), true); }

  // Aggregate DRAM bandwidth utilization as a 0..15 index (for bandwidth feedback).
  int dram_bw_index() const { return static_cast<int>(dram_.utilization() * 15.0 + 0.5); }

private:
  cache llc_;
  dram_model dram_;
  bool timing_;
  uint64_t llc_hit_latency_;
  double base_dram_latency_;
  double dram_lat_sum_ = 0;
  uint64_t dram_lat_n_ = 0;
  uint64_t last_demand_dram_done_ = 0; // completion cycle of the latest in-flight demand DRAM fetch
  bool market_;
  double market_target_;
  double market_step_; // reserved (market is demand-clearing; no float needed)
  double crosscore_;   // (ncores-1)/ncores: 0 single-core -> market naturally inert
  std::size_t ncores_;
  // Rank-order bandwidth throttle state (per-core, recomputed each epoch).
  bool rank_;
  uint64_t rank_epoch_;
  double rank_lo_, rank_hi_, rank_strength_;
  uint64_t rank_epoch_ctr_ = 0;
  std::vector<uint64_t> rank_consume_;  // this epoch's per-core DRAM accesses
  std::vector<double> rank_throttle_;   // per-core prefetch-drop fraction (set per epoch)
};

// One core: private L2 + predictor + metrics.
class core : public prefetch_sink
{
public:
  core(const params& p, shared_mem* sm, bool with_predictor, int id = 0) : P(p), sm_(sm), l2_(p.l2_sets, p.l2_ways), id_(id)
  {
    if (with_predictor) {
      pred_ = std::make_unique<sppam_predictor>(p, this);
      if (p.enable_spp)
        spp_ = std::make_unique<spp_predictor>(p, this);
    }
  }

  access_out access(uint64_t block, uint64_t ip, uint64_t cycle, atype type, bool is_demand, bool feed_pred, bool base_missed)
  {
    cur_cycle_ = cycle;
    m_.l2_acc_all++; // validation: every processed L2 access (all types)
    if (m_.first_cycle == 0) m_.first_cycle = cycle;
    m_.last_cycle = cycle;
    const cache_line* probe = l2_.probe(block);
    bool cache_hit;
    bool useful_pf = false;
    double lat;

    bool useful_from_spp = false, useful_from_dram = false;
    bool useful_timely = false; // useful prefetch hit whose fill had COMPLETED (vs an in-flight/late hit)
    if (probe != nullptr && (!P.enable_timing || probe->ready_at <= cycle)) {
      l2_.demand_access(block, cycle, useful_pf, &useful_from_spp, &useful_from_dram);
      cache_hit = true;
      lat = static_cast<double>(P.l2_hit_latency);
      useful_timely = useful_pf;
      if (is_demand && base_missed) { m_.covered_timely++; if (useful_pf) m_.covered_direct++; }
    } else if (probe != nullptr) {
      lat = static_cast<double>(probe->ready_at) - static_cast<double>(cycle) + static_cast<double>(P.l2_hit_latency);
      l2_.demand_access(block, cycle, useful_pf, &useful_from_spp, &useful_from_dram);
      cache_hit = true;
      if (is_demand && base_missed) m_.covered_late++;
    } else {
      bool served_dram = false;
      // is_demand marks demand vs (berti L1) prefetch traffic for the DRAM model, so
      // a simulated berti prefetch loads bandwidth without counting as demand pressure.
      lat = sm_->fetch_latency(block, cycle, is_demand, &served_dram);
      install_l2(block, cycle, sm_->ready(cycle, lat), false, false, served_dram);
      cache_hit = false;
      m_.l2_miss_all++; m_.sum_miss_lat_all += lat; // validation: aggregate L2 miss + latency
      if (served_dram) { m_.dram_fetches++; m_.dram_lat_sum += lat; sm_->dram_account(id_); }
    }

    if (useful_pf) {
      m_.pf_useful++;
      auto dit = pf_dens_.find(block); // sparse-tail probe: this SPPAM prefetch proved useful
      if (dit != pf_dens_.end()) { m_.pf_useful_dens[dit->second]++; pf_dens_.erase(dit); }
      // I_UPF: latency saved (avg L_DRAM if the prefetch fetched from DRAM, else
      // L_LLC), credited to its source prefetcher.
      m_.i_upf[useful_from_spp ? 1 : 0] += useful_from_dram ? sm_->avg_dram_latency() : static_cast<double>(P.llc_hit_latency);
      if (useful_from_spp && spp_) spp_->reward(true, block);
    }
    if (is_demand) {
      m_.demands++;
      if (!cache_hit) m_.misses++;
      m_.sum_demand_latency += lat;
    }
    // Per-prefetcher PE management: at each phase boundary, disable any prefetcher
    // whose net PE (I_UPF - I_POLL - I_LAT) was non-positive over the phase.
    if (is_demand && P.enable_pe_management && ++phase_demands_ >= P.pe_phase) {
      for (int p = 0; p < 2; ++p) {
        double pe = (m_.i_upf[p] - snap_upf_[p]) - (m_.i_lat[p] - snap_lat_[p]); // pollution dropped
        // THROTTLE, never gate: non-positive PE -> issue only 1/N (a trickle keeps
        // the I_UPF/I_LAT signal alive so the prefetcher can recover next phase).
        pf_throttle_[p] = (pe <= 0.0) ? static_cast<int>(P.pe_throttle_div) : 1;
        snap_upf_[p] = m_.i_upf[p]; snap_lat_[p] = m_.i_lat[p];
      }
      phase_demands_ = 0;
    }

    if (feed_pred) {
      // Per-trigger bidding: both engines propose (buffered with their confidence);
      // only the higher bidder's prefetches actually issue.
      bidding_ = P.enable_hybrid_bidding && spp_;
      sppam_fired_ = false; // reset per-trigger latch: did SPPAM see a pattern this access?
      // FIDELITY: the shipping module feeds the predictor a per-operate COUNTER (++cycle_), not the wall-clock
      // cycle -- prob_drop's `cycle_%128` pseudo-random needs the uniform operate counter, else trace-timestamp
      // clustering mod 128 biases the drop. Match the module.
      // delta-PHT self-throttle credit: additive iff this was a TIMELY useful hit on a block the no-prefetch
      // baseline actually MISSED (covered_direct semantics). Redundant hits on already-covered blocks don't count,
      // so on saturated traces (where delta just pollutes) the EMA collapses and the throttle engages.
      // Per-prefetch PE value for perceptron training: a useful prefetch saved a full DRAM latency (if it
      // covered a DRAM fetch) or just an LLC hit -- the DRAM-covering hits are ~10x more valuable, the signal
      // usefulness throws away.
      const double pf_val = useful_pf ? (useful_from_dram ? sm_->avg_dram_latency() : static_cast<double>(P.llc_hit_latency)) : 0.0;
      // Feed the perceptron's MSHR/bandwidth-pressure feature (the glue does this in the module; without it the
      // feature is dead-at-0 in the DSE). NOTE: this is DRAM-bandwidth utilization, not MSHR occupancy -- arguably
      // a better pressure signal (separates bandwidth-idle latency-bound workloads from bandwidth-contended ones).
      if (pred_ && P.enable_perceptron_filter) pred_->perc_set_mshr(dram_bw_index());
      if (pred_) pred_->operate(block, ip, cache_hit, useful_pf, useful_timely && base_missed, type, ++pred_ops_, pf_val);
      if (spp_) spp_->operate(block);
      if (bidding_) flush_winner();
    }
    return {lat, cache_hit};
  }

  int dram_bw_index() const override { return sm_->dram_bw_index(); }

  // Analysis: is this block's region entry still in SPPAM's region table? (query BEFORE this access is fed)
  bool region_resident(uint64_t block) { return pred_ ? pred_->region_resident(block) : false; }
  int last_block_delta(uint64_t block) { return pred_ ? pred_->last_block_delta(block) : -1000; }
  // Analysis: does SPPAM's shadow (prefetch_map/bloom) think this block is RESIDENT? If true while the L2 actually
  // missed, the shadow is stale -> any re-prefetch of this block is squashed -> it can never be re-covered.
  int shadow_status(uint64_t block) { return pred_ ? pred_->shadow_status(block) : 0; }
  // Analysis: pure fill/evict residency mirror — if this is false while shadow_status==2, the prefetch_map is stale.
  bool exact_resident(uint64_t block) { return pred_ ? pred_->exact_resident(block) : false; }
  std::string explain(uint64_t block) { return pred_ ? pred_->explain(block) : std::string(); }

  // Returns true iff the block ends up placed in L2 (predictor marks residency only then). Squashed / dropped /
  // buffered / LLC-only all return false, so a prefetch that never fills L2 never sets a residency bit.
  bool issue_prefetch(uint64_t block, bool fill_l2, bool from_spp = false, double benefit = 1.0, uint32_t gen_tag = 0) override
  {
    (void)gen_tag;                 // new sink carries generation attrs (src/depth/order/scan), not density
    cur_src_density_ = 0;          // src-density probe retired with the new sink interface (buckets collapse to 0)
    int id = from_spp ? 1 : 0;
    // SPPAM produced a prediction this trigger -> it "saw a pattern". Latch it
    // (before any squash) so the fall-through gate below can read it.
    if (!from_spp) sppam_fired_ = true;
    // Fall-through: SPP defers to SPPAM. On a trigger SPPAM already owns, SPP's
    // candidate is dropped -- except a 1/N trickle so SPP never permanently stops
    // learning (a complete gate is unrecoverable).
    if (P.enable_fallthrough && from_spp && sppam_fired_) {
      if (!(P.fallthrough_explore_div && (++spp_fallthrough_count_ % P.fallthrough_explore_div == 0))) {
        m_.pf_fallthrough++;
        return false;
      }
    }
    // Shared shadow cache: squash a prefetch (either engine) to an L2-resident block. (Already resident -> no
    // new placement; return false so the caller doesn't re-mark -- the existing shadow bit already covers it.)
    if (P.enable_shadow_squash && pred_ && pred_->shadow_resident(block)) { m_.pf_squashed++; return false; }
    if (bidding_) { // buffer this engine's candidate; bid = summed value or peak confidence
      buf_[id].push_back({block, fill_l2, benefit});
      if (P.bid_by_value) bid_[id] += benefit;
      else if (benefit > bid_[id]) bid_[id] = benefit;
      return false; // deferred to flush_winner(); residency is marked there via shadow_fill on actual fill
    }
    return do_issue(block, fill_l2, from_spp, benefit);
  }

  // Returns true iff the block was actually placed in L2 (so the predictor may mark it resident). A prefetch
  // that is dropped by any throttle, or filled to LLC only, returns false -> the residency map is NOT marked,
  // so it stays a faithful L2 mirror (no stale "in-flight but never-filled" bits).
  bool do_issue(uint64_t block, bool fill_l2, bool from_spp, double benefit)
  {
    // Universal region-thrash throttle: when the region map thrashes, NEITHER engine
    // can learn -> throttle BOTH toward off (deterministic fractional drop scaled by
    // thrash level), keeping a floor trickle so we detect when thrashing stops.
    if (P.enable_region_thrash_throttle && pred_) {
      double t = pred_->region_thrash();
      if (t > 0.0) {
        thrash_drop_acc_ += t * P.region_thrash_max_drop;
        if (thrash_drop_acc_ >= 1.0) { thrash_drop_acc_ -= 1.0; m_.pf_dropped_thrash++; return false; }
      }
    }
    // Rank-order bandwidth throttle: drop a deterministic fraction of THIS core's
    // prefetches set by its bandwidth rank (top consumer dropped hardest). Bresenham
    // accumulator -> exact fractional drop. Rank-0 caps at 0.5, so never fully gated.
    double drop = sm_->rank_drop_frac(id_);
    if (drop > 0.0) {
      rank_drop_acc_ += drop;
      if (rank_drop_acc_ >= 1.0) { rank_drop_acc_ -= 1.0; m_.pf_dropped_bwrank++; return false; }
    }
    // Bandwidth market: a low-benefit prefetch is dropped when the shared threshold
    // (set by system-wide DRAM pressure) outbids it.
    if (P.enable_pe_management) {
      int id = from_spp ? 1 : 0;
      if (++pf_count_[id] % pf_throttle_[id] != 0) return false; // throttled to a trickle (never fully gated)
    }
    if (!sm_->admit(benefit)) {
      m_.pf_dropped_market++;
      // Trickle: even a below-threshold prefetch issues 1/N, so the prefetcher's
      // usefulness/PE feedback never fully stops (a complete gate is unrecoverable).
      if (++market_trickle_[from_spp ? 1 : 0] % P.market_trickle_div != 0) return false;
    }
    m_.pf_issued++;
    bool served_dram = false, delayed_demand = false;
    double lat = sm_->fetch_latency(block, cur_cycle_, /*is_demand=*/false, &served_dram, &delayed_demand);
    // I_LAT: charge the queueing delay ONLY when the prefetch hit DRAM while a
    // demand fetch was still in flight (i.e. it actually delayed a demand).
    if (served_dram && delayed_demand)
      m_.i_lat[from_spp ? 1 : 0] += std::max(0.0, lat - sm_->base_dram_latency());
    if (served_dram) sm_->dram_account(id_); // prefetch DRAM traffic this core caused
    if (fill_l2) {
      install_l2(block, cur_cycle_, sm_->ready(cur_cycle_, lat), true, from_spp, served_dram);
      return true; // placed in L2 (shadow_fill inside install_l2 marks residency)
    }
    sm_->llc_install(block, cur_cycle_, lat);
    return false; // LLC only -> not an L2 resident
  }

  metrics result() const
  {
    metrics m = m_;
    if (pred_) {
      m.region_demand_accesses = pred_->region_demand_accesses;
      m.region_demand_misses = pred_->region_demand_misses;
      m.region_evictions = pred_->region_evictions;
      m.staging_drops = pred_->staging_drops();
    }
    return m;
  }

private:
  void flush_winner()
  {
    int w = (bid_[0] >= bid_[1]) ? 0 : 1; // highest bidder wins (SPPAM wins ties)
    // Never permanently gate the loser: every Nth trigger, hand it control so its
    // confidence/value feedback stays alive and it can win again.
    if (P.bid_explore_div && (++bid_trigger_ % P.bid_explore_div == 0)) w ^= 1;
    for (auto& pf : buf_[w])
      do_issue(pf.block, pf.fill_l2, w == 1, pf.benefit);
    buf_[0].clear(); buf_[1].clear();
    bid_[0] = bid_[1] = 0.0;
  }

  void install_l2(uint64_t block, uint64_t cycle, uint64_t ready_at, bool prefetched, bool from_spp = false, bool from_dram = false)
  {
    auto res = l2_.install(block, cycle, ready_at, prefetched, from_spp, from_dram);
    if (prefetched && !from_spp) pf_dens_[block] = static_cast<uint8_t>(dens_bucket(cur_src_density_)); // tag SPPAM pf source density
    if (pred_) pred_->shadow_fill(block); // shadow cache: any fill marks the block resident
    if (res.evicted_valid) {
      if (res.evicted_unused_prefetch) {
        m_.pf_useless++;
        auto it = pf_dens_.find(res.evicted_block);
        if (it != pf_dens_.end()) { m_.pf_useless_dens[it->second]++; pf_dens_.erase(it); }
      }
      if (res.evicted_from_spp && spp_) spp_->reward(false, res.evicted_block); // SPP prefetch evicted unused -> useless
      // Evicted-unused prefetch cost: the wasted bandwidth (approx one LLC-hit worth). Kept smaller than a
      // DRAM-covering hit's benefit ON PURPOSE -- an IP with rare-but-DRAM-covering hits should stay net-positive.
      const double ev_val = res.evicted_unused_prefetch ? -static_cast<double>(P.llc_hit_latency) : 0.0;
      if (pred_) pred_->on_l2_evict(res.evicted_block, cycle, res.evicted_unused_prefetch, ev_val);
    }
  }

  // Sparse-tail probe: source-region density of each in-flight SPPAM prefetch (block -> bucket),
  // and the density latched by the current issue_prefetch() call for install_l2 to read.
  static int dens_bucket(uint16_t d) { return d <= 4 ? 0 : d <= 8 ? 1 : d <= 16 ? 2 : d <= 32 ? 3 : 4; }
  std::unordered_map<uint64_t, uint8_t> pf_dens_;
  uint16_t cur_src_density_ = 0;

  params P;
  shared_mem* sm_;
  cache l2_;
  std::unique_ptr<sppam_predictor> pred_;
  std::unique_ptr<spp_predictor> spp_;
  // PE management state (per prefetcher [0]=SPPAM, [1]=SPP). Throttle = 1/N issue
  // rate (1 = full); never 0, so a non-positive-PE prefetcher keeps a learning trickle.
  int pf_throttle_[2] = {1, 1};
  uint64_t pf_count_[2] = {0, 0};
  uint64_t market_trickle_[2] = {0, 0};
  // Per-trigger bidding buffers ([0]=SPPAM, [1]=SPP) + this trigger's max bid each.
  struct pending_pf { uint64_t block; bool fill_l2; double benefit; };
  std::vector<pending_pf> buf_[2];
  double bid_[2] = {0.0, 0.0};
  bool bidding_ = false;
  uint64_t bid_trigger_ = 0;
  // Fall-through: per-trigger latch (did SPPAM fire?) + SPP explore-trickle counter.
  bool sppam_fired_ = false;
  uint64_t spp_fallthrough_count_ = 0;
  double snap_upf_[2] = {0, 0}, snap_lat_[2] = {0, 0};
  uint64_t phase_demands_ = 0;
  metrics m_;
  uint64_t cur_cycle_ = 0;
  uint64_t pred_ops_ = 0;      // per-operate counter fed to the predictor as its cycle_ (matches shipping module)
  int id_ = 0;                 // core index (for per-core bandwidth ranking)
  double rank_drop_acc_ = 0.0; // Bresenham accumulator for the rank-throttle drop
  double thrash_drop_acc_ = 0.0; // Bresenham accumulator for the region-thrash throttle
};

// A full system: N cores sharing one (scaled) LLC + DRAM.
class eval_system
{
public:
  eval_system(const params& p, std::size_t ncores, bool with_predictor)
      : sm_(p.llc_scale_with_cores ? p.llc_sets * (ncores ? ncores : 1) : p.llc_sets, p.llc_ways, p.dram, p.enable_timing, p.llc_hit_latency,
            p.enable_bw_market, p.bw_market_target_util, p.bw_market_step, ncores ? ncores : 1, p)
  {
    for (std::size_t i = 0; i < (ncores ? ncores : 1); ++i)
      cores_.push_back(std::make_unique<core>(p, &sm_, with_predictor, static_cast<int>(i)));
  }

  core& at(std::size_t i) { return *cores_[i]; }
  std::size_t size() const { return cores_.size(); }

  metrics aggregate() const
  {
    metrics m;
    for (const auto& c : cores_)
      m += c->result();
    return m;
  }

private:
  shared_mem sm_;
  std::vector<std::unique_ptr<core>> cores_;
};

} // namespace sppam_dse

#endif
