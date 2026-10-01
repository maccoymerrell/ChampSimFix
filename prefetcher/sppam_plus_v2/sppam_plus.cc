#include "sppam_plus.h"

#include <fmt/core.h>
#include <string>

namespace
{
constexpr unsigned BLOCK_SHIFT = 6; // LOG2(64B line)
}

sppam_plus::sppam_plus(champsim::modules::ModuleBuilder builder) : cache_(builder.get_parent<champsim::modules::cache_module>())
{
  // Start from the validated buildable hybrid geometry + latency-tuned defaults, then
  // let the config override ANY scalar knob via the ModuleBuilder so a single explicit
  // config can stand up many heads, each a different point in the design space.
  P.region_bits = 11;
  P.region_sets = 64;
  P.region_ways = 12;
  P.pattern_size = 6;
  P.min_pattern_size = 6;
  P.enable_spp = true;
  P.enable_fallthrough = true;
  // Per-pattern VERIFICATION FEEDBACK: each sampled prefetch carries the PHT key and position it was predicted
  // from; its outcome adjusts that prediction counter and the pattern's bad bit (fall-through).
  P.pattern_validate = true;
  P.pv_feed_confidence = true;
  P.pv_conf_penalty = 16;
  P.enable_ip_filter = true; // FINAL (full-knob sweep 2026-07-04): agg1 -- AMAT +1.1%% for useless -43%%,
                             // DRAM-bandwidth -24%% on full-132 (replay). Per-IP accuracy filter, usefulness metric.
  // DEPTH throttle is now the DEFAULT (full-132 +0.35%%, zero regressions>1%%, mcf +40%%/triangle +16%%):
  // route low-usefulness IPs by WHY -- UNTIMELY(right addr, evicted-early)->shorten lookahead DEPTH,
  // TRULY-BAD(wrong addr)->volume trickle. Two meta-gates: cache-stress(L2 hit>=0.72, else timeliness moot)
  // + upstream-MLP(demand+berti MSHR occ<0.6, else the deep prefetches are inherent parallelism, not waste).
  P.ip_filter_depth_throttle = true;
  P.ip_table_entries = 256;
  P.spp_usefulness_feedback = false; // reward() now wired (functional throttle), but it's a coverage<->bandwidth
                                     // trade: helps shared-DRAM multi-core, slightly hurts single-core AMAT
                                     // (sierra/merced). OFF for the single-core baseline; enable (or use
                                     // spp_threshold) as a BANDWIDTH-ADAPTIVE gate in the multi-stream phase.
  P.enable_pe_management = false;
  P.enable_bw_market = false;
  P.enable_bw_rank = false;
  P.enable_region_thrash_throttle = false;
  P.enable_bw_feedback = true;

  // Route every sweepable scalar knob through the config (unset -> keep the default above
  // or the params.h default). decltype picks each field's exact type.
#define CFG(field) P.field = builder.get_parameter<decltype(P.field)>(#field, true, P.field)
  CFG(region_bits); CFG(region_sets); CFG(region_ways); CFG(region_tag_bits); CFG(region_evict_policy);
  CFG(pattern_table_sets); CFG(pattern_table_ways); CFG(negative_table_sets); CFG(negative_table_ways); // PHT geometry (fwd + decoupled backward)
  CFG(region_page_aligned_sets); CFG(within_page_shadow);
  CFG(pattern_size); CFG(min_pattern_size); CFG(pattern_context_bits); CFG(pattern_pc_bits); CFG(pc_ctx_block); CFG(pc_ctx_wide); CFG(bg_pc_lookahead); CFG(bg_advance_strong); CFG(bg_no_advance); CFG(pattern_context_src);
  CFG(dense_window_train); CFG(dense_train_window); CFG(dense_train_pc_trigger); CFG(dense_train_prob); CFG(dense_advance_furthest);
  CFG(min_confidence_to_prefetch); CFG(counter_max); CFG(counter_up); CFG(counter_down); CFG(table_or_counter);
  CFG(online_learning); CFG(online_neg_samples); CFG(online_theta_train);
  CFG(pattern_perceptron); CFG(pp_hist_bits); CFG(pp_pc_bits); CFG(pp_weight_cap);
  CFG(do_lookahead); CFG(lookahead_conf_cutoff); CFG(lookahead_conf_factor); CFG(lookahead_depth);
  CFG(prob_drop_prefetches); CFG(global_or_pattern_usefulness); CFG(adaptive_usefulness);
  CFG(pattern_usefulness_cutoff); CFG(prefetch_to_l2_degree); CFG(dynamic_l2_fill);
  CFG(dynamic_l2_fill_adaptive); CFG(dynamic_l2_fill_thrash_min);
  CFG(enable_set_duel); CFG(sd_sample_rate); CFG(sd_eval_period); CFG(sd_step); CFG(sd_margin); CFG(sd_metric); CFG(sd_l2_max); CFG(sd_l2_floor);
  CFG(enable_ipf_duel); CFG(ipf_sample_rate); CFG(ipf_eval_period); CFG(ipf_margin); CFG(ipf_barely); CFG(ipf_significant); CFG(ipf_duty);
  CFG(scan_distance_forward); CFG(do_negative); CFG(scrape_full_window); CFG(train_demand_only);
  CFG(clear_after_scrape); CFG(clear_filter_after_scrape); CFG(use_default_prediction);
  CFG(scrape_on_idle); CFG(scrape_on_count); CFG(scrape_on_evict); CFG(scrape_idle_time); CFG(scrape_access_count);
  CFG(enable_spp); CFG(enable_fallthrough); CFG(fallthrough_explore_div);
  CFG(enable_hybrid_bidding); CFG(bid_by_value); CFG(enable_shadow_squash);
  CFG(enable_resid_bloom); CFG(resid_bloom_bits); CFG(resid_bloom_k); CFG(resid_bloom_clear);
  CFG(enable_am_bloom); CFG(am_bloom_size); CFG(am_bloom_k); CFG(am_bloom_clear_thresh); CFG(am_bloom_clear_frac);
  CFG(ip_llc_redirect); CFG(ip_llc_thresh); CFG(ip_llc_probe_div); CFG(llc_sample_entries); CFG(llc_sample_div); CFG(llc_track_timeout);
  // The per-IP table and the set-sampled lifetimes that feed it
  CFG(enable_ip_filter); CFG(ip_filter_threshold); CFG(ip_filter_trickle); CFG(ip_filter_threshold_hard); CFG(ip_filter_trickle_hard);
  CFG(ip_filter_depth_throttle); CFG(ip_depth_mid); CFG(ip_depth_min); CFG(ip_untimely_thresh); CFG(ip_depth_hitrate_min); CFG(ip_depth_mlp_max);
  CFG(ip_table_entries); CFG(ip_table_ways); CFG(ip_epoch); CFG(ss_sample_rate); CFG(untimely_fifo); CFG(inflight_records); CFG(l2_sets); CFG(l2_ways);
  CFG(spp_usefulness_feedback); CFG(spp_per_sig_usefulness); CFG(spp_per_sig_prior);
  CFG(spp_lookahead); CFG(spp_threshold); CFG(spp_share_region_table);
  CFG(spp_ghr); CFG(spp_ghr_entries); CFG(spp_min_delta); CFG(spp_min_conf); CFG(spp_multi_high_throttle);
  CFG(spp_sig_bits); CFG(spp_pt_sets); CFG(spp_pt_ways); CFG(spp_deltas_per_sig); CFG(spp_conf_bits); CFG(spp_st_entries);
  CFG(degree_boost);
  CFG(enable_bw_feedback); CFG(bw_mult);
  CFG(enable_bw_market); CFG(bw_market_target_util);
  CFG(enable_bw_rank); CFG(bw_rank_strength); CFG(bw_rank_lo); CFG(bw_rank_hi);
  CFG(enable_region_thrash_throttle);
  CFG(enable_region_staging); CFG(staging_entries); CFG(staging_promote_threshold);
  CFG(enable_ip_gate); CFG(ip_gate_div_min); CFG(ip_gate_explore_div);
  CFG(gate_adaptive); CFG(gate_thrash_min);
  CFG(ip_direction); CFG(ip_direction_min); CFG(use_berti_src_ip); CFG(pollution_filter);
  // v2 gated-backward (must be CFG'd so C's config can enable it -- else C silently == B).
  CFG(neg_online_train); CFG(neg_train_gated); CFG(neg_dir_pc); CFG(separate_negative_tables);
  CFG(scan_distance_backward); CFG(backward_momentum_min);
  CFG(bwd_useful_gate); CFG(bwd_useful_thresh);
  CFG(enable_instr_prefetch); CFG(instr_la_depth); CFG(instr_conf); CFG(instr_table_entries); CFG(instr_delta_bits); CFG(instr_xlate_entries); CFG(instr_filter_entries);
  CFG(instr_ft_blocks); CFG(instr_dir_bits);
  CFG(instr_walk_budget); CFG(instr_cost_strong); CFG(instr_cost_weak); CFG(instr_miss_conf);
  CFG(instr_feed_data);
  CFG(instr_nextn); CFG(instr_packed_residency); CFG(instr_llc_depth);
  CFG(pattern_validate); CFG(pv_feed_confidence); CFG(pv_conf_penalty); CFG(pv_bad_pct);
#undef CFG

  pred_ = std::make_unique<sppam_dse::sppam_predictor>(P, this);
  if (P.enable_spp)
    spp_ = std::make_unique<sppam_dse::spp_predictor>(P, this);
  if (P.enable_instr_prefetch)
    ipred_ = std::make_unique<sppam_dse::iprefetch_predictor>(P);
  if (P.bg_pc_lookahead) // SEPARATE data BG (Idea A) -- never shares edges with the instruction walk
    dpred_ = std::make_unique<sppam_dse::iprefetch_predictor>(P);
  // v2: route the branch graph's residency through SPPAM's PACKED code map (4KiB-page/both-maps) instead of
  // its own private filter -> one shared filter, lower redundancy, zero extra state. filter_evict_code (below)
  // keeps it coherent with L2 eviction.
  if (ipred_ && P.instr_packed_residency)
    ipred_->set_shared_residency([this](uint64_t b) { return pred_->filter_probe_code(b); },
                                 [this](uint64_t b) { pred_->filter_mark_code(b); });
  // BG-driven data lookahead (Idea A): SPPAM trains the SEPARATE data BG (dpred_) on each region's PC-hash
  // sequence and advances its lookahead context along it (predict the next PC to access the current region).
  if (dpred_ && P.bg_pc_lookahead)
    pred_->set_bg_lookahead([this](uint64_t pht) { return dpred_->bg_next(pht); },
                            [this](uint64_t prev, uint64_t cur) { dpred_->train_region_pc(prev, cur); });

  // The set samples: rows = sampled sets, one record per way; the in-flight records; the per-set untimely FIFOs.
  ss_rate_ = P.ss_sample_rate ? P.ss_sample_rate : 1;
  ss_rate_bits_ = 0; while ((1u << ss_rate_bits_) < ss_rate_) ++ss_rate_bits_;
  ss_set_bits_ = 0; while ((uint64_t{1} << ss_set_bits_) < P.l2_sets) ++ss_set_bits_;
  ss_rows_ = static_cast<uint32_t>(P.l2_sets / ss_rate_);
  ss_ways_ = static_cast<uint32_t>(P.l2_ways);
  ss_.assign(static_cast<std::size_t>(ss_rows_) * ss_ways_, ss_rec{});
  ifl_.assign(P.inflight_records ? P.inflight_records : 1, ss_rec{});
  ifl_block_.assign(ifl_.size(), 0);
  uf_.assign(static_cast<std::size_t>(ss_rows_) * (P.untimely_fifo ? P.untimely_fifo : 1), uf_rec{});
  uf_head_.assign(ss_rows_, 0);
  // The per-IP table
  ipt_ways_ = P.ip_table_ways ? P.ip_table_ways : 1;
  ipt_sets_ = std::max<uint32_t>(1, P.ip_table_entries / ipt_ways_);
  ipt_set_bits_ = 0; while ((1u << ipt_set_bits_) < ipt_sets_) ++ipt_set_bits_;
  ipt_.assign(static_cast<std::size_t>(ipt_sets_) * ipt_ways_, ip_ent{});
}

void sppam_plus::prefetcher_initialize()
{
  if (P.ip_llc_redirect)
    llcs_.assign(P.llc_sample_entries ? P.llc_sample_entries : 1, llc_track{});
  // Emit the storage cost so every sweep run records its buildability (geometry knobs trade
  // against the budget). Depends only on the config, not the trace.
  fmt::print("[SPPAM+] {} (region {}x{} bits={} pattern {}/{} spp={})\n", P.state_report(), P.region_sets, P.region_ways,
             P.region_bits, P.pattern_size, P.min_pattern_size, P.enable_spp);
  fmt::print("[SPPAM+] samples: {} of {} sets (1/{}) x {} ways, {} in-flight records, untimely fifo {} | per-IP table {}x{} epoch {}\n",
             ss_rows_, P.l2_sets, ss_rate_, ss_ways_, ifl_.size(), P.untimely_fifo, ipt_sets_, ipt_ways_, P.ip_epoch);
  if (P.enable_set_duel) {
    sd_rate_ = P.sd_sample_rate ? P.sd_sample_rate : champsim::msl::get_sample_rate(static_cast<long>(P.l2_sets));
    sd_l2_limit_ = static_cast<double>(P.sd_l2_max); // start deep (all L2), walk down on pollution
    fmt::print("[SPPAM+] set-duel on: metric={} l2_max={} sample 1/{} of {} sets ({} guard no-prefetch + {} usefulness-sample)\n",
               P.sd_metric, P.sd_l2_max, sd_rate_, P.l2_sets, P.l2_sets / sd_rate_, P.l2_sets / sd_rate_);
  }
  if (P.enable_ipf_duel) {
    ipf_rate_ = P.ipf_sample_rate ? P.ipf_sample_rate : champsim::msl::get_sample_rate(static_cast<long>(P.l2_sets));
    fmt::print("[SPPAM+] ip_filter DUEL on: sample 1/{} (filter-off vs filter-on leaders, hit-rate metric)\n", ipf_rate_);
  }
}

uint32_t sppam_plus::prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                              uint32_t metadata_in)
{
  real_cycle_ = cache_->current_cycle();
  const uint64_t block = addr.to<uint64_t>() >> BLOCK_SHIFT;
  if (type == access_type::LOAD || type == access_type::RFO) {
    ++l2_dem_acc_; if (cache_hit) ++l2_dem_hit_; // L2 demand hit rate (cache-stress gate)
    if (P.ip_filter_depth_throttle) { ++occ_n_; occ_tot_ += cache_->get_mshr_occupancy(); // MLP measurement
      occ_up_ += static_cast<uint64_t>(inflight_true_ + inflight_berti_); } // upstream = demand+berti outstanding
    sd_observe(block, cache_hit, useful_prefetch);
    ipf_observe(block, cache_hit); // adaptive ip_filter duel: per-set demand hit rate
  }
  // Sampled sets: a first hit on one of our prefetched lines resolves its record useful; any access to a block
  // in the set's untimely FIFO means that evicted prefetch had the right address, too early.
  if (const int row = ss_row(block % P.l2_sets); row >= 0) {
    const uint16_t tag = ss_tag(block);
    if (useful_prefetch) { if (ss_rec* r = ss_find(row, tag)) { resolve(*r, true); r->valid = false; } }
    if (cache_hit) { ss_rec dropped; ifl_take(block, dropped); } // an in-flight prefetch of a resident line is redundant
    if (uf_rec* u = uf_find(row, tag)) { resolve_timeliness(u->iph, u->eng, true); u->valid = false; }
  }
  if (useful_prefetch && spp_) spp_->reward(true, block); // SPP's own block->signature filter attributes it, or ignores it
  // Instruction stream (INSTRUCTION_LOAD = L1I misses, INSTRUCTION_PREFETCH = L1I prefetches): route
  // to the branch-graph prefetcher and return. Kept fully separate from the data predictors --
  // instruction packets never train the region/access maps. ip == v_address for instructions, so the
  // graph learns in IP (virtual) space (compact deltas) while `block` is the physical block; the
  // predictor's own vpage->ppage table translates its IP predictions back to physical to issue.
  if (ipred_ && is_instruction_access(type)) {
    const uint64_t ip_block = ip.to<uint64_t>() >> BLOCK_SHIFT;
    const uint16_t trig_iph = iphash(ip.to<uint64_t>());
    ipred_->operate(ip_block, block, [this, trig_iph](uint64_t b, uint64_t /*nx_ip*/, double /*conf*/, int depth) -> bool {
      bool fl2 = (depth < P.instr_llc_depth);      // MULTI-LEVEL: deep BG prefetches stage in LLC, not L2 (capacity, no L1I/L2 thrash)
      if (!sd_decide(b, fl2))    // set-duel may drop or redirect the instruction prefetch (kept off the re-propose list)
        return true;
      ++instr_pf_issued_;
      if (prefetch_line(b << BLOCK_SHIFT, fl2, 0) && fl2 && ss_row(b % P.l2_sets) >= 0) {
        ss_rec r; r.iph = trig_iph; r.eng = 3; r.pos = 0xFF;
        ifl_put(b, r);
      }
      return fl2;   // mark resident ONLY for L2 fills; LLC-staged prefetches stay off the residency map (re-proposable) per the caveat
    });
    // By default the instruction stream is exclusive to the branch graph. With instr_feed_data
    // the same access ALSO falls through to the data path below, so we can measure the branch
    // graph's contribution on top of the data prefetcher handling instructions.
    if (!P.instr_feed_data)
      return metadata_in;
  }
  cur_trigger_ip_ = ip.to<uint64_t>();
  // Recover berti's forwarded source PC (bits 9-31 of pf_metadata) for its prefetch accesses, which
  // otherwise arrive with ip=0 and blind every per-IP mechanism. See berti_plus BERTI_IP_ENCODING.
  if (P.use_berti_src_ip && cur_trigger_ip_ == 0 && type == access_type::PREFETCH && metadata_in)
    cur_trigger_ip_ = (static_cast<uint64_t>(metadata_in) >> 9) & 0x7fffffull;
  ++cycle_;
  sppam_fired_ = false; // reset per-trigger latch before the predictors run
  if (P.enable_ip_gate) // reuse the ip-filter's per-IP yield table as the sparse-page signal (no new state)
    pred_->set_ip_gate(ip_trickle_div(ipt_find(iphash(cur_trigger_ip_))) >= static_cast<uint32_t>(P.ip_gate_div_min));
  // delta_additive drives the delta-PHT usefulness attribution only (delta_pht off by default => no-op).
  // The module has no no-prefetch baseline to compute "would have missed", so pass false.
  pred_->operate(block, cur_trigger_ip_, cache_hit, useful_prefetch, /*delta_additive=*/false, static_cast<sppam_dse::atype>(type), cycle_, 0.0);
  if (spp_)
    spp_->operate(block);
  // LLC-aware throttle: resolve a sampled LLC-only prefetch. Still LLC-only and now missed on in L2 -> the LLC
  // copy served it (useful); promoted into L2 by a later prefetch and now hit -> useful through the promotion.
  if (P.ip_llc_redirect && !llcs_.empty()) {
    llc_track& le = llcs_[block % llcs_.size()];
    if (le.valid && le.block == block && (le.in_l2 ? cache_hit : !cache_hit)) { llc_credit(le.iph, true); le.valid = false; }
  }
  if (P.enable_ip_filter && P.ip_filter_depth_throttle && !cache_hit) { // the MLP gate's in-flight demand stream
    const bool is_true = (type != access_type::PREFETCH); // true demand vs berti prefetch access
    auto [it, fresh] = inflight_demand_.try_emplace(block, is_true); // MSHR-stall re-fires of a miss dedupe here
    if (fresh) { if (is_true) ++inflight_true_; else ++inflight_berti_; }
  }
  return metadata_in;
}

void sppam_plus::prefetcher_cache_evict(champsim::address evicted_addr, bool unused_prefetch)
{
  evict_unused_pending_ = unused_prefetch;
  evict_pending_block_ = evicted_addr.to<uint64_t>() >> BLOCK_SHIFT;
}

uint32_t sppam_plus::prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr,
                                           uint32_t metadata_in)
{
  real_cycle_ = cache_->current_cycle();
  const uint64_t block = addr.to<uint64_t>() >> BLOCK_SHIFT;
  pred_->shadow_fill(block); // Shadow cache: any fill marks the block resident.
  if (prefetch && !llcs_.empty()) { llc_track& le = llcs_[block % llcs_.size()]; if (le.valid && le.block == block) le.in_l2 = true; } // moved LLC -> L2
  // Sampled set: the way's previous record (our prefetch, never used) resolves useless and is watched for a
  // re-demand; the fill's own record moves in from the in-flight list. A fill that is no longer a prefetch is a
  // demand that merged into our in-flight prefetch: useful, timely.
  if (const int row = ss_row(static_cast<uint64_t>(set)); row >= 0) {
    ss_rec& w = ss_[row * ss_ways_ + static_cast<uint32_t>(way)];
    if (w.valid) { resolve(w, false); uf_push(row, w.tag, w.iph, w.eng); w.valid = false; }
    ss_rec r;
    if (ifl_take(block, r)) {
      if (!prefetch) resolve(r, true, /*merged=*/true);
      else { w = r; w.tag = ss_tag(block); }
    }
  }
  if (evicted_addr.to<uint64_t>() != 0) {
    const uint64_t evb = evicted_addr.to<uint64_t>() >> BLOCK_SHIFT;
    const bool evict_was_unused = evict_unused_pending_ && evict_pending_block_ == evb; // the cache's prefetch bit
    evict_unused_pending_ = false;
    if (evict_was_unused && spp_) spp_->reward(false, evb);
    if (!llcs_.empty()) { // a promoted LLC-only sample leaving L2 without a hit was never needed
      llc_track& le = llcs_[evb % llcs_.size()];
      if (le.valid && le.in_l2 && le.block == evb) { llc_credit(le.iph, false); le.valid = false; }
    }
    sd_observe_fill(block, true); // set-duel churn metric: this fill displaced a valid line
    pred_->on_l2_evict(evb, cycle_, evict_was_unused, 0.0);
    if (P.instr_packed_residency)
      pred_->filter_evict_code(evb); // clear the packed code-residency bit on L2 eviction (no-op for non-code blocks)
  }
  if (P.enable_ip_filter && P.ip_filter_depth_throttle) { // a tracked miss completing leaves the MSHR
    auto it = inflight_demand_.find(block);
    if (it != inflight_demand_.end()) {
      if (it->second) --inflight_true_; else --inflight_berti_;
      inflight_demand_.erase(it);
    } else if (cache_->get_mshr_occupancy() == 0) {
      inflight_demand_.clear(); inflight_true_ = inflight_berti_ = 0; // MSHR drained: drop anything leaked
    }
  }
  return metadata_in;
}

bool sppam_plus::issue_prefetch(uint64_t block, bool fill_l2, bool from_spp, double /*benefit*/, uint32_t /*gen_tag*/,
                                uint64_t pat_key, int pat_bit, bool backward, bool has_pat)
{
  if (!from_spp)
    sppam_fired_ = true;
  // Redundancy squash: run EVERY prefetch (SPPAM and, critically, SPP -- which has no filter
  // of its own) through the shadow residency map before issue. A resident block needs
  // no prefetch; drop it before it is even requested.
  if (P.enable_shadow_squash) {
    int st = pred_->shadow_status(block);
    if (st == 2) {
      ++pf_squashed_redundant_;
      return false;
    }
    // Diagnostic: why did the filter pass? region missing vs bit clear.
    if (st == 0) ++pf_pass_region_absent_;
    else ++pf_pass_bit_clear_;
  }
  // Fall-through: SPP defers to SPPAM on a trigger SPPAM claimed, except a 1/N trickle
  // so SPP keeps learning (never permanently gated).
  if (P.enable_fallthrough && from_spp && sppam_fired_) {
    if (!(P.fallthrough_explore_div && (++spp_ft_ctr_ % P.fallthrough_explore_div == 0)))
      return false;
  }
  const uint16_t iph = iphash(cur_trigger_ip_);
  // Per-trigger-IP volume trickle (drop 1-1/div) for TRULY-BAD IPs (wrong addresses); an UNTIMELY IP is exempt --
  // its depth is capped instead (ip_depth_cap). Never fully gates, so the IP keeps resolving samples.
  if (P.enable_ip_filter && ipf_should_apply(block)) {
    const ip_ent* e = ipt_find(iph);
    if (!(P.ip_filter_depth_throttle && ip_is_untimely(e))) {
      const uint32_t div = ip_trickle_div(e);
      if (div > 1 && (++ip_trickle_ctr_ % div) != 0) {
        if (!P.ip_llc_redirect) return false;
        if (e != nullptr && bin_lt(e->llc, P.ip_llc_thresh)) { ++dbg_llc_bad_; return false; } // its LLC placements don't pay either
        if (P.ip_llc_probe_div && !(e != nullptr && e->llc.bvalid) && (++llc_probe_ctr_ % P.ip_llc_probe_div) != 0) {
          ++dbg_llc_unproven_; return false;                          // not yet shown to pay: redirect only a probe
        }
        if (pred_->llc_marked(block)) { ++dbg_llc_dup_; return false; } // already placed in the LLC
        if (fill_l2) { fill_l2 = false; ++dbg_llc_redirect_; }        // place it in the LLC instead of dropping it
      }
    }
  }
  // Set-duel: guard groups + graded follower throttle (redirect L2->LLC / drop). Final placement.
  if (!sd_decide(block, fill_l2))
    return false;
  ++pf_issued_;
  if (P.ip_llc_redirect && !fill_l2 && !llcs_.empty() && (++llc_sample_ctr_ % P.llc_sample_div == 0)) {
    llc_track& slot = llcs_[block % llcs_.size()];
    const bool stale = slot.valid && (real_cycle_ - slot.issue) > P.llc_track_timeout;
    if (stale) llc_credit(slot.iph, false);
    if (!slot.valid || stale)
      slot = llc_track{true, false, block, real_cycle_, iph};
  }
  // Mark the shadow map at ISSUE (pending), so in-flight prefetches dedupe before they fill.
  // SPPAM's do_prefetch already marks its own path; SPP has no marking of its own, so without
  // this its in-flight prefetches (and any SPPAM re-prediction of the same block) are not
  // filtered until the fill lands. SPP always fills L2 (fill_l2=true).
  if (from_spp && fill_l2)
    pred_->shadow_fill(block);
  const bool enqueued = prefetch_line(block << BLOCK_SHIFT, fill_l2, 0);
  if (enqueued && !fill_l2 && P.ip_llc_redirect) pred_->mark_llc(block); // LLC-only: dedupe later proposals of this block
  if (enqueued) ++data_pf_issued_;
  // A prefetch bound for a sampled set carries its record until the fill.
  if (enqueued && fill_l2 && ss_row(block % P.l2_sets) >= 0) {
    ss_rec r;
    r.iph = iph; r.eng = backward ? 1 : (from_spp ? 2 : 0);
    r.has_pat = has_pat && !from_spp; r.key = static_cast<uint16_t>(pat_key); r.pos = pat_bit < 0 ? 0xFF : static_cast<uint8_t>(pat_bit);
    ifl_put(block, r);
  }
  // Contract with the predictor: TRUE iff the block was actually placed in L2 -> the predictor marks its
  // residency map only then (a dropped / LLC-only prefetch returns false, so no stale "issued but never
  // filled" bit). set-duel may have redirected fill_l2 to false (LLC-only) above.
  return enqueued && fill_l2;
}

int sppam_plus::dram_bw_index() const
{
  // Real L2 MSHR occupancy as a 0..15 bandwidth-pressure index for the predictors'
  // bandwidth-feedback throttle.
  return static_cast<int>(cache_->get_mshr_occupancy_ratio() * 15.0 + 0.5);
}

int sppam_plus::pf_free_space() const
{
  // orig SPPAM's dynamic L2-fill threshold: free MSHR/PQ headroom. Prefetches beyond it fill LLC-only,
  // so we never over-fill (thrash) L2. PQ occupancy is per-channel; take the (single) back channel.
  const auto pq = cache_->get_pq_occupancy();
  const long pqo = pq.empty() ? 0 : static_cast<long>(pq.back());
  const long fs = static_cast<long>(cache_->get_mshr_size()) - static_cast<long>(cache_->get_mshr_occupancy()) - pqo;
  return fs > 0 ? static_cast<int>(fs) : 0;
}

void sppam_plus::prefetcher_final_stats()
{
  if (P.enable_ipf_duel)
    fmt::print("[SPPAM+] ip_filter-duel: apply_filter={} epochs={} | filter-off hit={:.3f}  filter-on hit={:.3f} (barely<{} & significant>={} -> keep)\n",
               ipf_apply_followers_, ipf_epoch_, ipf_gd0?(double)ipf_gh0/ipf_gd0:0.0, ipf_gd1?(double)ipf_gh1/ipf_gd1:0.0, P.ipf_barely, P.ipf_significant);
  if (P.enable_set_duel)
    fmt::print("[SPPAM+] set-duel: metric={} l2_depth={:.1f}/{} | guard hit={:.3f} evict={:.3f} pollu={:.3f} | overall hit={:.3f} evict={:.3f} pollu={:.3f}\n",
               P.sd_metric, sd_l2_limit_, P.sd_l2_max,
               sd_gd_ ? static_cast<double>(sd_gh_) / sd_gd_ : 0.0, sd_gd_ ? static_cast<double>(sd_ge_) / sd_gd_ : 0.0,
               sd_gd_ ? (static_cast<double>(sd_ge_) - static_cast<double>(sd_gu_)) / sd_gd_ : 0.0,
               sd_ad_ ? static_cast<double>(sd_ah_) / sd_ad_ : 0.0, sd_ad_ ? static_cast<double>(sd_ae_) / sd_ad_ : 0.0,
               sd_ad_ ? (static_cast<double>(sd_ae_) - static_cast<double>(sd_au_)) / sd_ad_ : 0.0);
  fmt::print("[SPPAM+] region: evictions={} staging_drops={} demand_acc={} demand_miss={}\n", pred_->region_evictions, pred_->staging_drops(), pred_->region_demand_accesses, pred_->region_demand_misses);
  if (P.enable_am_bloom)
    fmt::print("[SPPAM+] am-bloom: regions_seeded={} bits_seeded={} (avg {:.1f} blocks/seed)\n", pred_->am_regions_seeded(), pred_->am_bits_seeded(),
               pred_->am_regions_seeded() ? static_cast<double>(pred_->am_bits_seeded()) / pred_->am_regions_seeded() : 0.0);
  if (ipred_) {
    const uint64_t iu = res_[3][1], il = res_[3][0];
    fmt::print("[SPPAM+] instr-pf: demands={} issued={} sampled useful={} useless={} accuracy={:.1f}% unencodable-delta={}\n",
               ipred_->demands(), ipred_->issued(), iu, il, (iu + il) ? 100.0 * iu / (iu + il) : 0.0, ipred_->unencodable());
    ipred_->dump_walk();
  }
  if (dpred_) dpred_->bg_seq_dump(P.name.c_str()); // data BG: per-region PC-sequence predictability
  fmt::print("[SPPAM+] prefetches issued: {} | squashed-redundant: {} | filter-passed[region-absent: {}, bit-clear: {}]\n",
             pf_issued_, pf_squashed_redundant_, pf_pass_region_absent_, pf_pass_bit_clear_);
  if (P.ip_llc_redirect) fmt::print("[SPPAM+] llc-redirect: redirected={} dropped(llc-bad)={} dropped(unproven)={} dropped(in-llc)={} | llc samples useful={} useless={}\n", dbg_llc_redirect_, dbg_llc_bad_, dbg_llc_unproven_, dbg_llc_dup_, dbg_llc_use_, dbg_llc_useless_);
  // Sampled accuracy and timeliness per engine (SPPAM = forward + backward)
  for (int s = 0; s < 2; ++s) {
    uint64_t u = 0, ul = 0, un = 0, bad = 0, mg = 0;
    for (int e : (s == 0 ? std::vector<int>{0, 1} : std::vector<int>{2})) { u += res_[e][1]; ul += res_[e][0]; un += res_untimely_[e]; bad += res_bad_[e]; mg += res_merged_[e]; }
    fmt::print("[SPPAM+] accuracy[{}] (sampled): useful={} useless={} acc={:.1f}% merged={} | of-useless untimely(addr-right)={:.1f}% truly-bad={:.1f}% (resolved {}/{})\n",
               s == 0 ? "SPPAM" : "SPP", u, ul, (u + ul) ? 100.0 * u / (u + ul) : 0.0, mg,
               (un + bad) ? 100.0 * un / (un + bad) : 0.0, (un + bad) ? 100.0 * bad / (un + bad) : 0.0, un, bad);
  }
  fmt::print("[SPPAM+] samples: in-flight records lost={}\n", ifl_lost_);
  if (P.enable_ip_filter) {
    uint32_t live = 0, judged = 0, light = 0, harsh = 0, nun = 0, nbad = 0, tjudged = 0; uint32_t use_hist[16] = {};
    for (const ip_ent& e : ipt_) {
      if (!e.valid) continue;
      ++live;
      if (e.use.bvalid) { ++judged; ++use_hist[std::min<uint8_t>(e.use.bin, 15)]; const uint32_t d = ip_trickle_div(&e); if (d > 1) { if (d == static_cast<uint32_t>(P.ip_filter_trickle_hard)) ++harsh; else ++light; } }
      if (e.tim.bvalid) { ++tjudged; if (bin_ge(e.tim, P.ip_untimely_thresh)) ++nun; else ++nbad; }
    }
    std::string h; for (int b = 0; b < 16; ++b) if (use_hist[b]) h += fmt::format(" {}:{}", b, use_hist[b]);
    fmt::print("[SPPAM+] ip-table: live={} with-usefulness={} throttled light={} harsh={} | usefulness bins (useful of {}):{}\n", live, judged, light, harsh, P.ip_epoch, h);
    fmt::print("[SPPAM+] depth-throttle: judged IPs: {} untimely->depth, {} truly-bad->volume (of {} with a timeliness bin)\n", nun, nbad, tjudged);
    if (P.ip_filter_depth_throttle) {
      const double avg_tot = occ_n_ ? static_cast<double>(occ_tot_) / occ_n_ : 0.0;
      const double avg_up = occ_n_ ? static_cast<double>(occ_up_) / occ_n_ : 0.0;
      fmt::print("[SPPAM+] MLP: avg MSHR occ total={:.2f} upstream(dem+berti)={:.2f} pf(sppam+)={:.2f} | mshr_size={} | L2 demand hit rate={:.3f}\n",
                 avg_tot, avg_up, avg_tot - avg_up, cache_->get_mshr_size(), l2_hit_rate());
    }
  }
}

champsim::modules::prefetcher::register_module<sppam_plus> sppam_plus_module("SPPAM_PLUS_V2");
