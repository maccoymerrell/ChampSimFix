#include "sppam_plus.h"

#include <fmt/core.h>
#include <set>
#include <stdexcept>
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
  // Per-pattern VERIFICATION FEEDBACK (DSE faithful stack): the IP sampling table (pf_sample_) records the
  // trigger PATTERN alongside the IP; on resolve it validates that exact pattern and feeds the outcome back
  // into its per-position prediction_counter -- a proven-useless e2e prediction is penalized toward silence
  // (natural SPP fall-through), a proven-useful one reinforced. Part of the baseline (B and C).
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
  P.spp_usefulness_feedback = false; // reward() now wired (functional throttle), but it's a coverage<->bandwidth
                                     // trade: helps shared-DRAM multi-core, slightly hurts single-core AMAT
                                     // (sierra/merced). OFF for the single-core baseline; enable (or use
                                     // spp_threshold) as a BANDWIDTH-ADAPTIVE gate in the multi-stream phase.
  // PE management (the I-POP latency-aware throttle, PE = I_UPF - I_POLL - I_LAT) is now
  // implemented here against the real cache hooks; it is OFF by default (a single-head
  // baseline) and turned on per-config via CFG for the latency-loser workloads. market/rank
  // stay off. bw_feedback reads REAL L2 MSHR occupancy via dram_bw_index().
  P.enable_pe_management = false;
  P.enable_bw_market = false;
  P.enable_bw_rank = false;
  P.enable_region_thrash_throttle = false;
  P.enable_bw_feedback = true;

  // Route every sweepable scalar knob through the config (unset -> keep the default above
  // or the params.h default). decltype picks each field's exact type.
  // Knobs of removed mechanisms (a second per-IP table, per-block IP tracking, other sampling-table policies) are
  // refused rather than silently ignored.
  for (const char* gone : {"ip_filter_use_pe", "ip_filter_pe_veto", "ip_filter_use_pe_phase", "enable_ipf_duel",
                           "pv_sample_evict_div", "ip_filter_max_useful_loss"})
    if (builder.has_parameter(gone))
      throw std::invalid_argument(std::string{"SPPAM_PLUS_V2: parameter '"} + gone + "' belongs to a removed mechanism");
  if (builder.has_parameter("pv_sample_directmap") && !builder.get_parameter<bool>("pv_sample_directmap", true, true))
    throw std::invalid_argument("SPPAM_PLUS_V2: the sampling table is direct-mapped; pv_sample_directmap=false is not supported");
  std::set<std::string> known{"pv_sample_directmap"};
#define CFG(field) (known.insert(#field), P.field = builder.get_parameter<decltype(P.field)>(#field, true, P.field))
  CFG(region_bits); CFG(region_sets); CFG(region_ways); CFG(region_tag_bits); CFG(region_evict_policy);
  CFG(llc_region_sets); CFG(llc_region_ways);
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
  CFG(enable_pe_ramp); CFG(pe_ramp_pe_min); CFG(pe_ramp_lat_min); CFG(pe_ramp_degree_add); CFG(pe_ramp_degree_cap); CFG(pe_ramp_lookahead_add);
  CFG(scan_distance_forward); CFG(do_negative); CFG(scrape_full_window); CFG(train_demand_only);
  CFG(clear_after_scrape); CFG(clear_filter_after_scrape); CFG(use_default_prediction);
  CFG(scrape_on_idle); CFG(scrape_on_count); CFG(scrape_on_evict); CFG(scrape_idle_time); CFG(scrape_access_count);
  CFG(enable_spp); CFG(enable_fallthrough); CFG(fallthrough_explore_div);
  CFG(enable_hybrid_bidding); CFG(bid_by_value); CFG(enable_shadow_squash);
  CFG(enable_resid_bloom); CFG(resid_bloom_bits); CFG(resid_bloom_k); CFG(resid_bloom_clear);
  CFG(enable_am_bloom); CFG(am_bloom_size); CFG(am_bloom_k); CFG(am_bloom_clear_thresh); CFG(am_bloom_clear_frac);
  CFG(ip_gate_per_prefetch); CFG(ip_llc_redirect); CFG(ip_llc_thresh); CFG(ip_llc_min_samples); CFG(ip_llc_probe_div); CFG(llc_sample_entries); CFG(llc_sample_div); CFG(llc_track_timeout);
  CFG(enable_ip_filter); CFG(ip_filter_threshold); CFG(ip_filter_min_samples); CFG(ip_filter_trickle); CFG(ip_filter_age_shift);
  CFG(ip_filter_threshold_hard); CFG(ip_filter_trickle_hard);
  CFG(ip_filter_depth_throttle); CFG(ip_depth_mid); CFG(ip_depth_min); CFG(ip_untimely_thresh); CFG(ip_depth_hitrate_min); CFG(ip_depth_mlp_max);
  CFG(ip_sample_div); CFG(ip_track_timeout);
  CFG(pv_stale_useless); CFG(untimely_from_region); CFG(diag_ip_truth); CFG(ip_table_entries); CFG(ip_table_ways); CFG(ip_ctr_bits); CFG(evicted_unused_cap);
  CFG(spp_usefulness_feedback); CFG(spp_per_sig_usefulness); CFG(spp_per_sig_prior);
  CFG(spp_lookahead); CFG(spp_threshold); CFG(spp_share_region_table);
  CFG(spp_ghr); CFG(spp_ghr_entries); CFG(spp_min_delta); CFG(spp_min_conf); CFG(spp_multi_high_throttle);
  CFG(spp_sig_bits); CFG(spp_pt_sets); CFG(spp_pt_ways); CFG(spp_deltas_per_sig); CFG(spp_conf_bits); CFG(spp_st_entries);
  CFG(enable_pe_management); CFG(pe_throttle_div); CFG(pe_phase); CFG(pe_sample_div); CFG(pfht_entries);
  CFG(enable_sig_feedback); CFG(sig_fb_entries);
  CFG(sig_fb_consume); CFG(sig_fb_min_samples); CFG(sig_fb_poll_soft); CFG(sig_fb_poll_hard); CFG(sig_fb_deg_soft); CFG(sig_fb_deg_hard);
  CFG(degree_boost);
  CFG(sig_fb_kill_useless); CFG(sig_fb_kill_amount);
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
  CFG(bwd_useful_gate); CFG(bwd_useful_thresh); CFG(bwd_useful_min_samples);
  CFG(enable_instr_prefetch); CFG(instr_la_depth); CFG(instr_conf); CFG(instr_table_entries); CFG(instr_delta_bits); CFG(instr_xlate_entries); CFG(instr_filter_entries);
  CFG(instr_ft_blocks); CFG(instr_dir_bits);
  CFG(instr_walk_budget); CFG(instr_cost_strong); CFG(instr_cost_weak); CFG(instr_miss_conf);
  CFG(instr_feed_data);
  CFG(instr_nextn); CFG(instr_packed_residency); CFG(instr_llc_depth);
  CFG(pattern_validate); CFG(pv_feed_confidence); CFG(pv_conf_penalty); CFG(pv_sample_div); CFG(pv_min_samples); CFG(pv_bad_pct); CFG(pv_sample_cap);
  CFG(pv_adaptive_rate); CFG(pv_rate_window); CFG(pv_churn_hi); CFG(pv_churn_lo); CFG(pv_div_min); CFG(pv_div_max);
  // Perceptron prefetch filter (optional sub-in; OFF by default). Trained on the glue's REAL per-prefetch fill latency.
  CFG(enable_perceptron_filter); CFG(perc_pc_entries); CFG(perc_weight_max); CFG(perc_tau_keep); CFG(perc_theta_train);
  CFG(perc_explore_div); CFG(perc_label_pe); CFG(perc_pe_margin); CFG(perc_track_cap); CFG(perc_track_ttl); CFG(perc_pe_scale); CFG(perc_pe_step_max);
  CFG(perc_feat_mask); CFG(perc_pe_signonly); CFG(perc_sig_entries); CFG(perc_pe_norm); CFG(perc_pe_cost); CFG(perc_untimely_veto); CFG(perc_pc_encoding); CFG(perc_pc_lobit); CFG(perc_pc_dropmask); CFG(perc_dump_weights); CFG(perc_gate_instr); CFG(perc_engine_split); CFG(perc_profile);
  CFG(perc_load_gate); CFG(perc_load_gate_thresh); CFG(perc_instr_gate); CFG(perc_instr_gate_pct); // NOTE: the module loads knobs via CFG()/get_parameter, NOT the apply_json SET() list -- new knobs MUST be added here too
  CFG(perc_dense_train); CFG(perc_dense_cap); CFG(perc_victim); CFG(perc_victim_cap); CFG(perc_victim_sample_div); CFG(perc_victim_penalty);
  CFG(perc_pe_gate); CFG(perc_pe_gate_thresh); CFG(perc_pe_gate_min_samples); CFG(perc_pe_ema_shift); CFG(perc_use_veto); CFG(perc_use_veto_thresh);
  CFG(perc_upf_scale); CFG(perc_lat_scale); CFG(perc_poll_scale); CFG(perc_eval_log); CFG(perc_eval_log_div); CFG(perc_sig_xor_pc);
  CFG(bg_inflight_entries);
#undef CFG
  // A knob the loader does not read would silently keep its default: refuse it.
  for (const auto& [key, value] : builder.get_parameters())
    if (known.count(key) == 0)
      throw std::invalid_argument("SPPAM_PLUS_V2: unknown parameter '" + key + "'");

  if (P.enable_pe_management || P.enable_sig_feedback || P.enable_perceptron_filter) { // their own outcome tables
    pfht_.assign(P.pfht_entries ? P.pfht_entries : 1, pf_track{});
    poll_.assign(P.pfht_entries ? P.pfht_entries : 1, poll_track{});
  }
  // (perc_sdm_ removed -- the perceptron's fill-latency + cost now live in the predictor's perc_track_ entry, fed via
  //  pred_->perc_note_fill()/perc_add_cost() from the glue's real-cycle fill/pollution hooks. One less sampling table.)
  if (P.pe_sample_div == 0)
    P.pe_sample_div = 1;

  pred_ = std::make_unique<sppam_dse::sppam_predictor>(P, this);
  if (P.enable_spp)
    spp_ = std::make_unique<sppam_dse::spp_predictor>(P, this);
  if (P.enable_instr_prefetch) {
    ipred_ = std::make_unique<sppam_dse::iprefetch_predictor>(P);
    bg_inflight_.assign(P.bg_inflight_entries ? P.bg_inflight_entries : 1, 0);
  }
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
}

void sppam_plus::prefetcher_initialize()
{
  if (P.ip_llc_redirect)
    llcs_.assign(P.llc_sample_entries ? P.llc_sample_entries : 1, llc_track{});
  // Emit the storage cost so every sweep run records its buildability (geometry knobs trade
  // against the budget). Depends only on the config, not the trace.
  fmt::print("[SPPAM+] state ~{:.1f} KiB (region {}x{} bits={} pattern {}/{} spp={} pe={})\n", P.state_kib(), P.region_sets, P.region_ways,
             P.region_bits, P.pattern_size, P.min_pattern_size, P.enable_spp, P.enable_pe_management);
  if (P.enable_set_duel) {
    sd_rate_ = P.sd_sample_rate ? P.sd_sample_rate : champsim::msl::get_sample_rate(static_cast<long>(P.l2_sets));
    sd_l2_limit_ = static_cast<double>(P.sd_l2_max); // start deep (all L2), walk down on pollution
    fmt::print("[SPPAM+] set-duel on: metric={} l2_max={} sample 1/{} of {} sets ({} guard no-prefetch + {} usefulness-sample)\n",
               P.sd_metric, P.sd_l2_max, sd_rate_, P.l2_sets, P.l2_sets / sd_rate_, P.l2_sets / sd_rate_);
  }
}

uint32_t sppam_plus::prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                              uint32_t metadata_in)
{
  real_cycle_ = cache_->current_cycle(); // TRUE ChampSim cycle -- the fill-latency / pfht_ / pe-management clock (was NEVER assigned -> stuck at 0 -> all real-cycle timing broken)
  pred_->set_now(real_cycle_);
  const uint64_t block = addr.to<uint64_t>() >> BLOCK_SHIFT;
  // Refresh the perceptron's aggregate MSHR/bandwidth-pressure feature before any gate (BG or data) runs this access.
  if (P.enable_perceptron_filter) {
    pred_->perc_set_rc(real_cycle_);   // real cycle for self-measured fill latency (issue stamp at perc_note_issue)
    // VICTIM CHECK: a dropped-prefetch block that MISSES on a later demand = the drop genuinely lost coverage (BAD drop)
    // -> heavy KEEP retrain. Gated on !cache_hit: a re-demand that HITS was covered some other way, so the drop cost
    // nothing and must NOT be punished (otherwise natural reuse retrains everything back to keep -> under-filtering).
    if (P.perc_victim && !cache_hit) pred_->perc_victim_check(block);
    pred_->perc_set_mshr(dram_bw_index());
    // Instruction-activity gate: fraction of prefetch issues that are instruction (BG). ~0 on data-bound mcf/xalan,
    // 0.5-0.6 on the instruction-bound datacenter -> a clean runtime flag to back the data perceptron off there.
    const uint64_t tot_pf = instr_pf_issued_ + data_pf_issued_;
    pred_->perc_set_instr_pct(tot_pf ? static_cast<int>(100 * instr_pf_issued_ / tot_pf) : 0);
  }
  // Set-duel: observe every demand (instruction + data) by set before any routing, so the guard
  // hit-rate reflects total L2 residency in those sets.
  if (type == access_type::LOAD || type == access_type::RFO) {
    ++l2_dem_acc_; if (cache_hit) ++l2_dem_hit_; // L2 demand hit rate (cache-stress gate)
    if (P.ip_filter_depth_throttle) { ++occ_n_; occ_tot_ += cache_->get_mshr_occupancy(); // MLP measurement
      occ_up_ += static_cast<uint64_t>(inflight_true_ + inflight_berti_); } // upstream = demand+berti outstanding
    sd_observe(block, cache_hit, useful_prefetch);
  }
  // Instruction stream (INSTRUCTION_LOAD = L1I misses, INSTRUCTION_PREFETCH = L1I prefetches): route
  // to the branch-graph prefetcher and return. Kept fully separate from the data predictors --
  // instruction packets never train the region/access maps. ip == v_address for instructions, so the
  // graph learns in IP (virtual) space (compact deltas) while `block` is the physical block; the
  // predictor's own vpage->ppage table translates its IP predictions back to physical to issue.
  if (ipred_ && is_instruction_access(type)) {
    if (useful_prefetch) // an instruction access hit a line we prefetched and had not used (the cache's prefetch bit)
      ++instr_useful_;
    const uint64_t ip_block = ip.to<uint64_t>() >> BLOCK_SHIFT;
    // Each enqueued L2 instruction prefetch is held in bg_inflight_ until its fill, so the fill hook can keep it out of
    // the data maps (the metadata bits cannot mark it: berti's prefetches carry its source IP in bits 9-31).
    const uint64_t trig_ip = ip.to<uint64_t>();
    ipred_->operate(ip_block, block, [this, trig_ip](uint64_t b, uint64_t nx_ip, double conf, int depth) -> bool {
      // Perceptron final gate (engine 3 = branch-graph): learned drop over the predicted NEXT-PC signature. A drop
      // returns false so the branch graph doesn't mark it resident (it can be re-proposed and re-gated later).
      const bool gate_bg = P.enable_perceptron_filter && P.perc_gate_instr; // datacenter prefetching is instruction-DOMINATED; the data-tuned features can't discriminate BG prefetches, so this is optionally off
      if (gate_bg
          && !pred_->perc_keep(b, 3, depth, trig_ip, static_cast<int>(conf * 15.0), nx_ip))
        return false;
      bool fl2 = (depth < P.instr_llc_depth);      // MULTI-LEVEL: deep BG prefetches stage in LLC, not L2 (capacity, no L1I/L2 thrash)
      if (!sd_decide(b, fl2))    // set-duel may drop or redirect the instruction prefetch (kept off the re-propose list)
        return true;
      ++instr_pf_issued_;                          // instruction-activity gate: count BG prefetch issues (datacenter discriminator)
      if (prefetch_line(b << BLOCK_SHIFT, fl2, 0) && fl2) {
        bg_inflight_[b % bg_inflight_.size()] = b;
        if (gate_bg) pred_->perc_note_issue(b);   // training snapshot for the branch-graph gate (perc_track_ holds features+lat+cost)
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
  // A prefetch is USED when any access other than our own prefetch hits it while the cache's per-line prefetch bit is
  // still set -- a demand, or berti's prefetch request (useful_prefetch covers both).
  const bool pf_used = useful_prefetch;
  sppam_fired_ = false; // reset per-trigger latch before the predictors run
  if (P.enable_ip_gate) // reuse the ip-filter's per-IP yield table as the sparse-page signal (no new state)
    pred_->set_ip_gate(pred_->trickle_div_for_ip(cur_trigger_ip_) >= static_cast<uint32_t>(P.ip_gate_div_min));
  // delta_additive drives the delta-PHT usefulness attribution only (delta_pht off by default => no-op).
  // The module has no no-prefetch baseline to compute "would have missed", so pass false.
  // Perceptron PE value on a useful hit = this prefetch's REAL fill latency (what a demand miss would have cost);
  // consumed here at resolve. A DRAM-covering prefetch reads ~10x an LLC one -- the coverage-weighted signal.
  double perc_pe = 0.0;
  if (P.enable_perceptron_filter && pf_used) {
    // PE = latency SAVED (real fill latency; fallback L_LLC if not yet filled), net of the I_LAT+I_POLL this prefetch
    // caused -- all read from its perc_track_ entry (the unified table). operate() below frees it at resolve.
    uint32_t lat = 0; float cost = 0.0f;
    const bool filled = pred_->perc_track_lat(block, lat, cost);
    perc_pe = filled ? static_cast<double>(lat) : static_cast<double>(P.llc_hit_latency);
    if (P.perc_pe_cost) perc_pe += static_cast<double>(cost);
    pred_->perc_note_used(perc_pe); // probe: how often a demand-USED prefetch gets a negative PE label (PE-vs-IPC misalignment)
    // DENSE training: this prefetch was demand-USED -> useful (+1), first-outcome (retires its dense entry).
    if (P.perc_dense_train) pred_->perc_dense_resolve(block, /*useful=*/true);
  }
  pred_->operate(block, cur_trigger_ip_, cache_hit, pf_used, /*delta_additive=*/false, static_cast<sppam_dse::atype>(generic_access_type(type)), cycle_, perc_pe); // DATA_LOAD -> LOAD, DATA_PREFETCH -> PREFETCH
  if (spp_)
    spp_->operate(block);

  // Shared sampled-prefetch USE resolve on the merged pfht_ (PE's I_UPF + IP-filter's useful). The
  // entry is freed here (resolved useful); if never used it stays PINNED until its line is evicted
  // (resolved useless in the fill hook) -- so useless prefetches are never lost to churn.
  const bool pe_terms = P.enable_pe_management
    || (P.enable_perceptron_filter && (P.perc_pe_cost || P.perc_pe_gate || (P.perc_feat_mask & 0x700000u))); // perceptron's PE cost/features need the I_LAT/I_POLL machinery even with the throttles OFF
  if (!pfht_.empty() && (P.enable_pe_management || P.enable_sig_feedback) && pf_used) {
    pf_track& e = pfht_[block % pfht_.size()];
    if (e.valid && e.block == block) {
      if (P.enable_sig_feedback) pred_->sig_feedback(e.eng, e.sig, 0); // per-signature: this prefetch was demand-used
      if (pe_terms) {
        const uint64_t saved = e.filled ? e.lat : (real_cycle_ >= e.issue ? real_cycle_ - e.issue : 0);
        const double contrib = access_weight(type, P.pe_pf_demand_weight) * static_cast<double>(saved); // I_UPF (+)
        if (P.enable_pe_management) i_upf_[e.from_spp ? 1 : 0] += contrib;
      }
      e.valid = false; // resolved (used)
    }
  }
  // LLC-aware throttle: resolve a sampled LLC-only prefetch. Still LLC-only and now missed on in L2 -> the LLC
  // copy served it (useful); promoted into L2 by a later prefetch and now hit -> useful through the promotion.
  if (P.ip_llc_redirect && !llcs_.empty()) {
    llc_track& le = llcs_[block % llcs_.size()];
    if (le.valid && le.block == block && (le.in_l2 ? cache_hit : !cache_hit)) { pred_->ip_llc_outcome(le.iph, true); ++dbg_llc_use_; le.valid = false; }
  }
  if (pe_terms || (P.enable_ip_filter && P.ip_filter_depth_throttle) || P.enable_sig_feedback) { // lightweight inflight counters for the MLP gate (+ per-signature pollution detection)
    // The demand stream = ALL L2 accesses; berti's PREFETCH-type accesses are demand at
    // weight pe_pf_demand_weight (<1, more slack serving a prefetch), true loads/RFOs at 1.
    const double w = access_weight(type, P.pe_pf_demand_weight);
    // Distinct access (a hit, or the FIRST sighting of a miss); MSHR-stall re-fires of a
    // missed block are deduped by the map (membership mirrors the MSHR -- not new state).
    bool distinct = false;
    if (cache_hit) {
      distinct = true;
    } else {
      const bool is_true = (type != access_type::PREFETCH); // true demand vs berti prefetch access
      auto [it, fresh] = inflight_demand_.try_emplace(block, is_true);
      if (fresh) {
        distinct = true;
        if (is_true)
          ++inflight_true_;
        else
          ++inflight_berti_;
        // I_POLL: this miss lands on a line a sampled prefetch evicted -> pollution miss (PE modes only).
        if ((pe_terms || P.enable_sig_feedback) && !poll_.empty()) {
          poll_track& v = poll_[block % poll_.size()];
          if (v.valid && v.block == block) {
            const double pollcost = w * avg_lat(); // I_POLL (-): pf evicted a useful line, now a demand miss
            if (P.enable_sig_feedback) pred_->sig_feedback(v.eng, v.sig, 2); // per-signature: this prefetch polluted (evicted a useful line, now a miss)
            if (P.enable_pe_management) i_poll_[v.from_spp ? 1 : 0] += pollcost;
            if (P.enable_perceptron_filter && P.perc_pe_cost) // charge I_POLL to the EVICTING prefetch's perceptron PE
              pred_->perc_add_cost(v.pf_block, -static_cast<float>(pollcost));
            if (P.enable_perceptron_filter && (P.perc_pe_gate || (P.perc_feat_mask & 0x700000u))) // 3-way split: per-IP I_POLL
              pred_->perc_note_ip_poll(v.iph, static_cast<float>(pollcost));
            v.valid = false;
          }
        }
      }
    }
    // Phase boundary: score PE = I_UPF - I_POLL - I_LAT per source and (un)throttle. The
    // sampling scale is uniform across all three terms, so PE's sign is unaffected by it.
    const bool pe_phase_on = P.enable_pe_management;
    if (pe_phase_on && distinct && ++pe_phase_demands_ >= P.pe_phase) {
      if (P.enable_pe_management)
        for (int s = 0; s < 2; ++s) {
          const double pe = (i_upf_[s] - snap_upf_[s]) - (i_poll_[s] - snap_poll_[s]) - (i_lat_[s] - snap_lat_[s]);
          pe_throttle_[s] = (pe <= 0.0) ? static_cast<int>(P.pe_throttle_div) : 1;
          ++pe_phases_[s]; if (pe <= 0.0) ++pe_neg_phases_[s]; // per-phase sign census
          const double act = (i_upf_[s]-snap_upf_[s]) + (i_poll_[s]-snap_poll_[s]) + (i_lat_[s]-snap_lat_[s]);
          if (act > 0.0) { ++pe_active_[s]; if (pe < 0.0) ++pe_negact_[s]; } // strict harm with real activity
          // PE-ramp gate on the SPPAM source (0): net-useful this phase AND DRAM-bound fills.
          if (s == 0 && P.enable_pe_ramp)
            pe_ramp_active_ = (pe > P.pe_ramp_pe_min) && (avg_lat() > P.pe_ramp_lat_min);
          snap_upf_[s] = i_upf_[s];
          snap_poll_[s] = i_poll_[s];
          snap_lat_[s] = i_lat_[s];
        }
      pe_phase_demands_ = 0;
    }
  }
  return metadata_in;
}

uint32_t sppam_plus::prefetcher_cache_fill(champsim::address addr, long /*set*/, long /*way*/, bool prefetch, champsim::address evicted_addr,
                                           uint32_t metadata_in)
{
  real_cycle_ = cache_->current_cycle(); // TRUE ChampSim cycle for the fill-latency measurement (issue->fill)
  pred_->set_now(real_cycle_);
  const uint64_t block = addr.to<uint64_t>() >> BLOCK_SHIFT;
  if (P.enable_perceptron_filter) pred_->perc_set_rc(real_cycle_); // keep the perceptron's real-cycle clock current for perc_fill_measure
  const bool pe_terms = P.enable_pe_management
    || (P.enable_perceptron_filter && (P.perc_pe_cost || P.perc_pe_gate || (P.perc_feat_mask & 0x700000u))); // perceptron's PE cost/features need the I_LAT/I_POLL machinery even with the throttles OFF
  // Our instruction prefetch fills stay out of the data maps (shadow residency, PE tables).
  const bool is_instr_pf = prefetch && ipred_ && bg_inflight_take(block);
  if (!is_instr_pf) {
    // Shadow cache: any (data) fill marks the block resident.
    pred_->shadow_fill(block);
    if (prefetch) {
      if (!llcs_.empty()) { llc_track& le = llcs_[block % llcs_.size()]; if (le.valid && le.block == block) le.in_l2 = true; } // moved LLC -> L2
      // DSE-faithful PE: measure THIS prefetch's real fill latency directly into perc_track_ (every data fill, not
      // gated on pfht_ sampling) -> DRAM-covering useful prefetches get their true high PE, not the llc_hit fallback.
      if (P.enable_perceptron_filter) pred_->perc_fill_measure(block);
    } else {
      pred_->sample_demand_fill(block); // a demand merged into a sampled prefetch's miss: that prefetch was used
    }
  }
  bool had_evict = false, evict_was_unused = false;
  uint64_t evb = 0;
  if (evicted_addr.to<uint64_t>() != 0) {
    evb = evicted_addr.to<uint64_t>() >> BLOCK_SHIFT;
    // The cache reported this eviction just before the fill, with its per-line prefetch bit (our prefetch, unused).
    // An unused prefetched CODE line (branch-graph) is instruction-side and stays out of the data usefulness.
    const bool unused_pf = (evb == last_evict_block_) && last_evict_unused_;
    const bool code_line = ipred_ && P.instr_packed_residency && pred_->filter_probe_code(evb);
    if (unused_pf && code_line) ++instr_useless_;
    evict_was_unused = unused_pf && !code_line;
    if (!llcs_.empty()) { // a promoted LLC-only sample leaving L2 without a hit was never needed
      llc_track& le = llcs_[evb % llcs_.size()];
      if (le.valid && le.in_l2 && le.block == evb) { pred_->ip_llc_outcome(le.iph, false); ++dbg_llc_useless_; le.valid = false; }
    }
    had_evict = true;
    sd_observe_fill(block, true); // set-duel churn metric: this fill displaced a valid line
    // A PINNED pfht_ entry for the evicted line resolves USELESS here (issued, filled, never used).
    // This is the unbiased counterpart to the USE resolve -- long-lived useless prefetches are only
    // caught at eviction, so we never overwrite an in-flight entry before this fires.
    if (!pfht_.empty() && (P.enable_pe_management || P.enable_sig_feedback)) {
      pf_track& et = pfht_[evb % pfht_.size()];
      if (et.valid && et.block == evb) {
        if (P.enable_sig_feedback) pred_->sig_feedback(et.eng, et.sig, 1); // per-signature: evicted before use = useless
        if (P.sig_fb_kill_useless) pred_->sig_kill(et.eng, et.sig, et.order, et.pos); // targeted: kill the specific wrong (sig,order,offset)
        et.valid = false; // resolved (evicted unused)
      }
    }
    double perc_ev = 0.0;
    if (P.enable_perceptron_filter && evict_was_unused) {
      perc_ev = -static_cast<double>(P.llc_hit_latency); // wasted-fill cost (< a covering hit's benefit -> keeps coverage-valuable patterns)
      if (P.perc_pe_cost) { uint32_t lat = 0; float cost = 0.0f; pred_->perc_track_lat(evb, lat, cost); perc_ev += static_cast<double>(cost); } // fold accrued I_LAT+I_POLL
      // DENSE training: this prefetch was evicted WITHOUT a demand hit -> useless (-1), first-outcome.
      if (P.perc_dense_train) pred_->perc_dense_resolve(evb, /*useful=*/false);
    }
    pred_->on_l2_evict(evb, cycle_, evict_was_unused, perc_ev); // on_l2_evict frees the perc_track_ entry
    if (P.instr_packed_residency)
      pred_->filter_evict_code(evb); // clear the packed code-residency bit on L2 eviction (no-op for non-code blocks)
  }

  if (pe_terms || (P.enable_ip_filter && P.ip_filter_depth_throttle)) { // lightweight inflight counters (MLP gate)
    // A tracked stream-access miss completing (true-demand fill prefetch=false, or a berti
    // prefetch-access fill prefetch=true -- the map lookup, not the flag, discriminates):
    // it leaves the MSHR, so drop it from the in-flight-demand gate.
    auto it = inflight_demand_.find(block);
    if (it != inflight_demand_.end()) {
      if (it->second)
        --inflight_true_;
      else
        --inflight_berti_;
      inflight_demand_.erase(it);
    } else if (cache_->get_mshr_occupancy() == 0) {
      inflight_demand_.clear(); // safety: MSHR drained -> drop any leaked entries
      inflight_true_ = inflight_berti_ = 0;
    }
  }
  // Shared fill resolve on the merged pfht_: confirm the sampled prefetch filled, and if a demand
  // had already merged into its MSHR (promoted) resolve it USEFUL. PE additionally books its fill
  // latency and, for a prefetch that filled ahead of an in-flight demand, I_LAT/I_POLL (Eq4/Eq3).
  if (!pfht_.empty()) { // perceptron needs the fill-latency / I_UPF / usefulness feed even with the throttles off (sig-feedback needs the fill + poll-victim insert)
    pf_track& e = pfht_[block % pfht_.size()];
    if (e.valid && e.block == block && !e.filled) {
      e.filled = true;
      e.lat = (real_cycle_ >= e.issue) ? (real_cycle_ - e.issue) : 0;
      if (pe_terms) { lat_sum_ += e.lat; ++lat_n_; }
      if (P.enable_perceptron_filter) pred_->perc_note_fill(block, static_cast<uint32_t>(std::min<uint64_t>(e.lat, 100000))); // feed the perceptron's PE magnitude (unified into perc_track_)
      // PE-split signal: per-IP I_UPF = latency SAVED (fill latency), capped to a sane DRAM max so a stale/garbage
      // measurement (the old 100000-cap outliers) can't poison the EMA. I_LAT is fed separately at the Eq4 event below.
      if (P.enable_perceptron_filter && (P.perc_pe_gate || (P.perc_feat_mask & 0x700000u)))
        pred_->perc_note_ip_upf(e.iph, static_cast<float>(std::min<uint64_t>(e.lat, 1500)));
      const int src = e.from_spp ? 1 : 0;
      if (!prefetch) {
        // Promoted: a demand merged into this prefetch's MSHR before it filled -> useful, timely.
        if (P.enable_pe_management) i_upf_[src] += static_cast<double>(e.lat);
        e.valid = false;
      } else if (pe_terms) {
        // I_LAT (Eq4): the demand sitting in the MSHR as this prefetch returns is delayed by
        // the prefetch's shared-resource SERVICE time. A true load is the full-weight victim;
        // a berti access is urgent (temporally close to its demand) but carries more slack.
        if (inflight_true_ > 0 || inflight_berti_ > 0) {
          const double w = (inflight_true_ > 0) ? 1.0 : P.pe_pf_demand_weight; // MAX-affected demand
          const double serv = (e.lat > P.pe_dram_lat_threshold) ? P.pe_serv_dram : P.pe_serv_llc;
          if (P.enable_pe_management) i_lat_[src] += w * serv;
          if (P.enable_perceptron_filter && P.perc_pe_cost) // ALSO charge I_LAT to THIS prefetch's perceptron PE
            pred_->perc_add_cost(block, -static_cast<float>(w * serv));
          if (P.enable_perceptron_filter && (P.perc_pe_gate || (P.perc_feat_mask & 0x700000u))) // per-IP I_LAT: the TRUE Eq4 service cost
            pred_->perc_note_ip_lat(e.iph, static_cast<float>(w * serv));
        }
        // I_POLL setup: displaced a USEFUL line (a demand line or used prefetch) -> remember
        // the victim, the evicting prefetch (to charge its perceptron PE), and its IP; a later demand miss = pollution.
        if (had_evict && !evict_was_unused)
          poll_[evb % poll_.size()] = poll_track{true, evb, block, e.from_spp, e.iph, e.sig, e.eng};
      }
    }
  }
  return metadata_in;
}

bool sppam_plus::issue_prefetch(uint64_t block, bool fill_l2, bool from_spp, double /*benefit*/, uint32_t gen_tag)
{
  if (!from_spp)
    sppam_fired_ = true;
  // Redundancy squash: run EVERY prefetch (SPPAM and, critically, SPP -- which has no filter
  // of its own) through the shadow residency map before issue. This was previously dead code
  // (enable_shadow_squash/shadow_resident were never invoked), so redundant prefetches to
  // already-resident blocks flooded the PQ and burned tag bandwidth. A resident block needs
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
  const int src = from_spp ? 1 : 0;
  // PE throttle: a source whose last-phase PE was non-positive issues only 1/pe_throttle_div
  // (never fully gated -- a complete gate would erase the very PE signal needed to recover).
  if (P.enable_pe_management && pe_throttle_[src] > 1 && (++pe_pf_count_[src] % pe_throttle_[src]) != 0)
    return false;
  // Per-IP gate (the predictor's IP table, fed only by its sampling table). By default SPPAM applies it to the whole
  // trigger before proposing and SPP prefetches pass it here; with ip_gate_per_prefetch every data prefetch passes it
  // here instead. A redirected prefetch goes to the LLC only, once per block.
  bool redirected = false;
  if ((from_spp || P.ip_gate_per_prefetch) && P.enable_ip_filter) {
    if (!pred_->trigger_gate(redirected))
      return false;
    if (redirected) fill_l2 = false;
  } else if (!from_spp) {
    redirected = pred_->trigger_llc_only();
  }
  if (redirected) {
    if (pred_->llc_marked(block)) { ++dbg_llc_dup_; return false; }
    ++dbg_llc_redirect_;
  }
  // Set-duel: guard groups + graded follower throttle (redirect L2->LLC / drop). Final placement.
  if (!sd_decide(block, fill_l2))
    return false;
  ++pf_issued_;
  // Sampled attribution into the SHARED pfht_ (serves PE's I_UPF/I_LAT/I_POLL AND the per-IP filter):
  // hold 1/pe_sample_div of ISSUED prefetches. PINNED insert -- take only a FREE slot, never clobber
  // an in-flight (valid) entry, so a long-lived useless prefetch survives to its eviction resolve
  // (removing the bias that plagued a churn-overwrite table).
  if (P.ip_llc_redirect && !fill_l2) {
    if (!llcs_.empty() && (++llc_sample_ctr_ % P.llc_sample_div == 0)) {
      llc_track& slot = llcs_[block % llcs_.size()];
      const bool stale = slot.valid && (real_cycle_ - slot.issue) > P.llc_track_timeout;
      if (stale) { pred_->ip_llc_outcome(slot.iph, false); ++dbg_llc_useless_; }
      if (!slot.valid || stale)
        slot = llc_track{true, false, block, real_cycle_, pred_->trigger_ip()};
    }
  } else if (!pfht_.empty() && (++pe_sample_ctr_ % P.pe_sample_div == 0)) {
    pf_track& slot = pfht_[block % pfht_.size()];
    const bool stale = slot.valid && (real_cycle_ - slot.issue) > P.ip_track_timeout;
    if (!slot.valid || stale) {
      // sppam_b USELESS_ON_TIMEOUT: a stale in-flight entry sat unused past the timeout -> resolve it
      // USELESS (removing the under-sampling bias) before reusing its slot. A RECENT entry is pinned.
      if (stale && P.enable_sig_feedback) pred_->sig_feedback(slot.eng, slot.sig, 1); // timed-out unused -> useless
      slot = pf_track{true, block, from_spp, real_cycle_, false, 0, pred_->trigger_ip()};
      if (P.enable_sig_feedback) { slot.sig = pred_->fb_sig_; slot.eng = pred_->fb_eng_; slot.order = pred_->fb_order_; slot.pos = pred_->fb_pos_; } // record request's (engine,sig,order,offset)
    }
  }
  // Mark the shadow map at ISSUE (pending), so in-flight prefetches dedupe before they fill.
  // SPPAM's do_prefetch already marks its own path; SPP has no marking of its own, so without
  // this its in-flight prefetches (and any SPPAM re-prediction of the same block) are not
  // filtered until the fill lands. SPP always fills L2 (fill_l2=true).
  if (from_spp && fill_l2)
    pred_->shadow_fill(block);
  const bool enqueued = prefetch_line(block << BLOCK_SHIFT, fill_l2, gen_tag | (from_spp ? 1u : 0u)); // carry generation tag (diagnostics)
  if (enqueued && !fill_l2 && P.ip_llc_redirect) pred_->mark_llc(block); // LLC-only: dedupe later proposals of this block
  if (enqueued && fill_l2 && from_spp && P.enable_ip_filter) pred_->sample_spp_prefetch(block);
  if (enqueued) ++data_pf_issued_;               // instruction-activity gate: count data prefetch issues (denominator of the instr-pf fraction)
  // (Perceptron issue tracking is perc_note_issue -> perc_track_; the fill latency is fed later by the pfht_ fill
  //  hook via pred_->perc_note_fill(). No separate perc_sdm_ insert.)
  // Contract with the predictor: TRUE iff the block was actually placed in L2 -> the predictor marks its
  // residency map only then (a dropped / LLC-only prefetch returns false, so no stale "issued but never
  // filled" bit). set-duel may have redirected fill_l2 to false (LLC-only) above.
  return enqueued && fill_l2;
}

// Perceptron gate for the separate SPP engine (which reaches the perceptron only through the sink).
bool sppam_plus::perc_gate(uint64_t block, int engine, int depth, uint64_t pc, int conf, uint64_t sig)
{
  return !P.enable_perceptron_filter || pred_->perc_keep(block, engine, depth, pc, conf, sig);
}
void sppam_plus::perc_note_issue_ext(uint64_t block)
{
  if (P.enable_perceptron_filter) pred_->perc_note_issue(block);
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
  if (P.enable_pe_ramp)
    fmt::print("[SPPAM+] pe-ramp: active_at_end={} avg_lat={:.0f} (gate: PE>{} & lat>{})\n", pe_ramp_active_, avg_lat(), P.pe_ramp_pe_min, P.pe_ramp_lat_min);
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
    const uint64_t iu = instr_useful_, il = instr_useless_;
    const double iacc = (iu + il) ? 100.0 * iu / (iu + il) : 0.0;
    fmt::print("[SPPAM+] instr-pf: demands={} issued={} useful={} useless={} accuracy={:.1f}% unencodable-delta={}\n",
               ipred_->demands(), ipred_->issued(), iu, il, iacc, ipred_->unencodable());
    ipred_->dump_walk();
  }
  if (dpred_) dpred_->bg_seq_dump(P.name.c_str()); // data BG: per-region PC-sequence predictability
  fmt::print("[SPPAM+] prefetches issued: {} | squashed-redundant: {} | filter-passed[region-absent: {}, bit-clear: {}]\n",
             pf_issued_, pf_squashed_redundant_, pf_pass_region_absent_, pf_pass_bit_clear_);
  if (P.ip_llc_redirect) fmt::print("[SPPAM+] llc-redirect: redirected={} dropped(in-llc)={} | llc samples useful={} useless={}\n", dbg_llc_redirect_, dbg_llc_dup_, dbg_llc_use_, dbg_llc_useless_);
  if (P.enable_pe_management)
    fmt::print("[SPPAM+] PE  SPPAM: I_UPF={:.0f} I_POLL={:.0f} I_LAT={:.0f} -> throttle 1/{} | SPP: I_UPF={:.0f} I_POLL={:.0f} I_LAT={:.0f} -> throttle 1/{}\n",
               i_upf_[0], i_poll_[0], i_lat_[0], pe_throttle_[0], i_upf_[1], i_poll_[1], i_lat_[1], pe_throttle_[1]);
  if (P.enable_pe_management)
    fmt::print("[SPPAM+] PE per-phase sign: SPPAM {}/{} phases PE<=0 ({:.0f}%%) | SPP {}/{} phases PE<=0 ({:.0f}%%)\n",
               pe_neg_phases_[0], pe_phases_[0], pe_phases_[0]?100.0*pe_neg_phases_[0]/pe_phases_[0]:0.0,
               pe_neg_phases_[1], pe_phases_[1], pe_phases_[1]?100.0*pe_neg_phases_[1]/pe_phases_[1]:0.0);
  if (P.enable_ip_filter && P.ip_filter_depth_throttle) {
    const double avg_tot = occ_n_ ? static_cast<double>(occ_tot_) / occ_n_ : 0.0;
    const double avg_up = occ_n_ ? static_cast<double>(occ_up_) / occ_n_ : 0.0;
    fmt::print("[SPPAM+] depth-throttle gates: L2 demand hit rate={:.3f} (min {}) | avg MSHR occ total={:.2f} upstream(dem+berti)={:.2f} (max {}) | mshr_size={}\n",
               l2_hit_rate(), P.ip_depth_hitrate_min, avg_tot, avg_up, P.ip_depth_mlp_max, cache_->get_mshr_size());
  }
  if (P.enable_pe_management)
    fmt::print("[SPPAM+] PE strict-harm(active): SPPAM {}/{} active phases PE<0 ({:.0f}%%) | SPP {}/{} ({:.0f}%%)\n",
               pe_negact_[0], pe_active_[0], pe_active_[0]?100.0*pe_negact_[0]/pe_active_[0]:0.0,
               pe_negact_[1], pe_active_[1], pe_active_[1]?100.0*pe_negact_[1]/pe_active_[1]:0.0);
}

bool sppam_plus::bg_inflight_take(uint64_t block)
{
  uint64_t& e = bg_inflight_[block % bg_inflight_.size()];
  if (e != block)
    return false;
  e = 0;
  return true;
}

void sppam_plus::prefetcher_cache_evict(champsim::address evicted_addr, bool unused_prefetch)
{
  last_evict_block_ = evicted_addr.to<uint64_t>() >> BLOCK_SHIFT;
  last_evict_unused_ = unused_prefetch;
}

champsim::modules::prefetcher::register_module<sppam_plus> sppam_plus_module("SPPAM_PLUS_V2");
