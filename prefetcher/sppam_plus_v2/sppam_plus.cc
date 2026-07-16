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
#define CFG(field) P.field = builder.get_parameter<decltype(P.field)>(#field, true, P.field)
  CFG(region_bits); CFG(region_sets); CFG(region_ways); CFG(region_tag_bits);
  CFG(region_page_aligned_sets); CFG(within_page_shadow);
  CFG(pattern_size); CFG(min_pattern_size); CFG(pattern_context_bits); CFG(pattern_pc_bits); CFG(pattern_context_src);
  CFG(min_confidence_to_prefetch); CFG(counter_up); CFG(counter_down); CFG(table_or_counter);
  CFG(online_learning); CFG(online_neg_samples); CFG(online_theta_train);
  CFG(pattern_perceptron); CFG(pp_hist_bits); CFG(pp_pc_bits); CFG(pp_weight_cap);
  CFG(do_lookahead); CFG(lookahead_conf_cutoff); CFG(lookahead_conf_factor); CFG(lookahead_depth);
  CFG(prob_drop_prefetches); CFG(global_or_pattern_usefulness); CFG(adaptive_usefulness);
  CFG(pattern_usefulness_cutoff); CFG(prefetch_to_l2_degree); CFG(dynamic_l2_fill);
  CFG(dynamic_l2_fill_adaptive); CFG(dynamic_l2_fill_thrash_min);
  CFG(enable_set_duel); CFG(sd_sample_rate); CFG(sd_eval_period); CFG(sd_step); CFG(sd_margin); CFG(sd_metric); CFG(sd_l2_max); CFG(sd_l2_floor);
  CFG(enable_ipf_duel); CFG(ipf_sample_rate); CFG(ipf_eval_period); CFG(ipf_margin); CFG(ipf_barely); CFG(ipf_significant); CFG(ipf_duty);
  CFG(enable_pe_ramp); CFG(pe_ramp_pe_min); CFG(pe_ramp_lat_min); CFG(pe_ramp_degree_add); CFG(pe_ramp_degree_cap); CFG(pe_ramp_lookahead_add);
  CFG(scan_distance_forward); CFG(do_negative); CFG(scrape_full_window); CFG(train_demand_only);
  CFG(clear_after_scrape); CFG(clear_filter_after_scrape); CFG(use_default_prediction);
  CFG(scrape_on_idle); CFG(scrape_on_count); CFG(scrape_on_evict); CFG(scrape_idle_time); CFG(scrape_access_count);
  CFG(enable_spp); CFG(enable_fallthrough); CFG(fallthrough_explore_div);
  CFG(enable_hybrid_bidding); CFG(bid_by_value); CFG(enable_shadow_squash);
  CFG(enable_resid_bloom); CFG(resid_bloom_bits); CFG(resid_bloom_k); CFG(resid_bloom_clear);
  CFG(enable_am_bloom); CFG(am_bloom_size); CFG(am_bloom_k); CFG(am_bloom_clear_thresh); CFG(am_bloom_clear_frac);
  CFG(enable_ip_filter); CFG(ip_filter_threshold); CFG(ip_filter_min_samples); CFG(ip_filter_trickle); CFG(ip_filter_age_shift);
  CFG(ip_filter_threshold_hard); CFG(ip_filter_trickle_hard); CFG(ip_filter_use_pe); CFG(ip_pe_hard_frac);
  CFG(ip_filter_pe_veto); CFG(ip_pe_veto_frac);
  CFG(ip_filter_use_pe_phase); CFG(ip_pe_phase_soft); CFG(ip_pe_phase_hard); CFG(ip_pe_phase_margin);
  CFG(ip_filter_depth_throttle); CFG(ip_depth_mid); CFG(ip_depth_min); CFG(ip_untimely_thresh); CFG(ip_depth_hitrate_min); CFG(ip_depth_mlp_max);
  CFG(ip_filter_max_useful_loss); CFG(ip_sample_div); CFG(ip_track_timeout);
  CFG(spp_usefulness_feedback); CFG(spp_per_sig_usefulness); CFG(spp_per_sig_prior);
  CFG(spp_lookahead); CFG(spp_threshold); CFG(spp_share_region_table);
  CFG(spp_ghr); CFG(spp_ghr_entries); CFG(spp_min_delta); CFG(spp_min_conf); CFG(spp_multi_high_throttle);
  CFG(spp_sig_bits); CFG(spp_pt_sets); CFG(spp_pt_ways); CFG(spp_deltas_per_sig); CFG(spp_conf_bits);
  CFG(enable_pe_management); CFG(pe_throttle_div); CFG(pe_phase); CFG(pe_sample_div); CFG(pfht_entries);
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
  CFG(instr_feed_data); CFG(unblock_instructions);
  CFG(instr_nextn); CFG(instr_packed_residency);
#undef CFG

  pfht_.assign(P.pfht_entries ? P.pfht_entries : 1, pf_track{});
  poll_.assign(P.pfht_entries ? P.pfht_entries : 1, poll_track{});
  if (P.pe_sample_div == 0)
    P.pe_sample_div = 1;

  pred_ = std::make_unique<sppam_dse::sppam_predictor>(P, this);
  if (P.enable_spp)
    spp_ = std::make_unique<sppam_dse::spp_predictor>(P, this);
  if (P.enable_instr_prefetch)
    ipred_ = std::make_unique<sppam_dse::iprefetch_predictor>(P);
  // v2: route the branch graph's residency through SPPAM's PACKED code map (4KiB-page/both-maps) instead of
  // its own private filter -> one shared filter, lower redundancy, zero extra state. filter_evict_code (below)
  // keeps it coherent with L2 eviction.
  if (ipred_ && P.instr_packed_residency)
    ipred_->set_shared_residency([this](uint64_t b) { return pred_->filter_probe_code(b); },
                                 [this](uint64_t b) { pred_->filter_mark_code(b); });
  // Opt the L2 into delivering instruction fetches when the branch graph is on OR when we
  // explicitly want the data path to see instructions (the branch-graph-off marginal-value arm).
  if (P.enable_instr_prefetch || P.unblock_instructions)
    cache_->set_prefetch_instructions(true);
}

void sppam_plus::prefetcher_initialize()
{
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
  if (P.enable_ipf_duel) {
    ipf_rate_ = P.ipf_sample_rate ? P.ipf_sample_rate : champsim::msl::get_sample_rate(static_cast<long>(P.l2_sets));
    fmt::print("[SPPAM+] ip_filter DUEL on: sample 1/{} (filter-off vs filter-on leaders, hit-rate metric)\n", ipf_rate_);
  }
}

uint32_t sppam_plus::prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                              uint32_t metadata_in)
{
  const uint64_t block = addr.to<uint64_t>() >> BLOCK_SHIFT;
  // Set-duel: observe every demand (instruction + data) by set before any routing, so the guard
  // hit-rate reflects total L2 residency in those sets.
  if (type == access_type::LOAD || type == access_type::RFO) {
    ++l2_dem_acc_; if (cache_hit) ++l2_dem_hit_; // L2 demand hit rate (cache-stress gate)
    if (P.ip_filter_depth_throttle) { ++occ_n_; occ_tot_ += cache_->get_mshr_occupancy(); // MLP measurement
      occ_up_ += static_cast<uint64_t>(inflight_true_ + inflight_berti_); } // upstream = demand+berti outstanding
    sd_observe(block, cache_hit, useful_prefetch);
    ipf_observe(block, cache_hit); // adaptive ip_filter duel: per-set demand hit rate
  }
  // Instruction stream (L1I misses): route to the branch-graph prefetcher and return. Kept
  // fully separate from the data predictors -- instruction packets never train the region/
  // access maps. ip == v_address for instructions, so the graph learns in IP (virtual) space
  // (compact deltas) while `block` is the physical block; the predictor's own vpage->ppage
  // table translates its IP predictions back to physical to issue.
  if (ipred_ && cache_->current_access_is_instruction()) {
    if (cache_hit) { // an instruction demand hit a block we prefetched -> useful, resolve it
      auto iit = instr_pf_unused_.find(block);
      if (iit != instr_pf_unused_.end()) { ++instr_useful_; instr_pf_unused_.erase(iit); }
    }
    const uint64_t ip_block = ip.to<uint64_t>() >> BLOCK_SHIFT;
    // Track each issued instruction prefetch by BLOCK (only if actually enqueued). The fill hook
    // recognizes our instruction prefetches by membership here -- NOT a metadata tag, because the
    // metadata space is fully contested (berti encodes its source IP in bits 9-31, so any tag bit
    // collides with berti's data-prefetch fills and steals them from the data-path bookkeeping).
    ipred_->operate(ip_block, block, [this](uint64_t b) {
      bool fl2 = true;
      if (!sd_decide(b, fl2))    // set-duel may drop or redirect the instruction prefetch
        return;
      if (prefetch_line(b << BLOCK_SHIFT, fl2, 0) && fl2)
        instr_pf_unused_[b] = 1; // only track L2-resident instruction prefetches (LLC gives no feedback)
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
  // A SPPAM prefetch is USED when ANY L2 access hits it -- a demand (useful_prefetch) OR
  // berti's prefetch request hitting our still-pending fill. Crediting only demand hits
  // made berti-served streams (most of the traffic) look ~all-useless, collapsing the
  // global-usefulness index to 0 and making prob_drop strangle SPPAM permanently.
  bool used_from_map = false;
  if (cache_hit) {
    auto uit = pf_unused_.find(block);
    if (uit != pf_unused_.end()) {
      const uint32_t tag = uit->second; const uint8_t src = tag & 1u;
      const int depth = (tag >> 1) & 15, order = (tag >> 5) & 7;
      const int scan = (tag >> 9) & 7;
      ++char_useful_[src]; ++useful_by_depth_[src][depth];
      if (src == 0) { ++useful_by_order_[order]; ++useful_by_scan_[scan]; }
      if (src == 1 && spp_) spp_->reward(true, block); // SPP prefetch was used -> positive feedback
      pf_unused_.erase(uit); used_from_map = true;
    }
  }
  const bool pf_used = used_from_map || useful_prefetch;
  // Exact timeliness: a used prefetch is TIMELY -> drop it from the issue-watch (it won't be evicted-unused).
  if (pf_used && P.ip_filter_depth_throttle) pf_issue_iph_.erase(block);
  // Timeliness: a (re)access to a block we prefetched THEN evicted-unused means the address was right
  // but the prefetch was untimely (too early -> evicted before use). Distinguishes untimely from bad-address.
  if (auto eit = pf_evicted_unused_.find(block); eit != pf_evicted_unused_.end()) {
    ++char_timely_[eit->second & 1u]; pf_evicted_unused_.erase(eit);
  }
  // Per-IP untimeliness: this block was prefetched(right addr), evicted unused, now demanded -> untimely.
  if (auto ii = pf_evict_iph_.find(block); ii != pf_evict_iph_.end()) {
    ++ip_untimely_[ii->second]; pf_evict_iph_.erase(ii);
  }
  sppam_fired_ = false; // reset per-trigger latch before the predictors run
  if (P.enable_ip_gate) // reuse the ip-filter's per-IP yield table as the sparse-page signal (no new state)
    pred_->set_ip_gate(ip_trickle_div(iphash(cur_trigger_ip_)) >= static_cast<uint32_t>(P.ip_gate_div_min));
  // delta_additive drives the delta-PHT usefulness attribution only (delta_pht off by default => no-op).
  // The module has no no-prefetch baseline to compute "would have missed", so pass false.
  pred_->operate(block, cur_trigger_ip_, cache_hit, pf_used, /*delta_additive=*/false, static_cast<sppam_dse::atype>(type), cycle_);
  if (spp_)
    spp_->operate(block);

  // Shared sampled-prefetch USE resolve on the merged pfht_ (PE's I_UPF + IP-filter's useful). The
  // entry is freed here (resolved useful); if never used it stays PINNED until its line is evicted
  // (resolved useless in the fill hook) -- so useless prefetches are never lost to churn.
  const bool pe_terms = P.enable_pe_management || (P.enable_ip_filter && (P.ip_filter_use_pe || P.ip_filter_pe_veto || P.ip_filter_use_pe_phase));
  if ((P.enable_pe_management || P.enable_ip_filter) && pf_used) {
    pf_track& e = pfht_[block % pfht_.size()];
    if (e.valid && e.block == block) {
      if (P.enable_ip_filter) { ++ip_useful_[e.iph]; ++dbg_use_; ip_age_tick(); }
      if (pe_terms) {
        const uint64_t saved = e.filled ? e.lat : (real_cycle_ >= e.issue ? real_cycle_ - e.issue : 0);
        const double contrib = access_weight(type, P.pe_pf_demand_weight) * static_cast<double>(saved); // I_UPF (+)
        if (P.enable_pe_management) i_upf_[e.from_spp ? 1 : 0] += contrib;
        if (P.enable_ip_filter && (P.ip_filter_use_pe || P.ip_filter_use_pe_phase)) { ip_pe_[e.iph] += contrib; ++ip_pe_n_[e.iph]; }
      }
      e.valid = false; // resolved (used)
    }
  }
  if (pe_terms || (P.enable_ip_filter && P.ip_filter_depth_throttle)) { // lightweight inflight counters for the MLP gate
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
        if (pe_terms) {
          poll_track& v = poll_[block % poll_.size()];
          if (v.valid && v.block == block) {
            const double pollcost = w * avg_lat(); // I_POLL (-): pf evicted a useful line, now a demand miss
            if (P.enable_pe_management) i_poll_[v.from_spp ? 1 : 0] += pollcost;
            if (P.enable_ip_filter && (P.ip_filter_use_pe || P.ip_filter_use_pe_phase)) ip_pe_[v.iph] -= pollcost;
            v.valid = false;
          }
        }
      }
    }
    // Phase boundary: score PE = I_UPF - I_POLL - I_LAT per source and (un)throttle. The
    // sampling scale is uniform across all three terms, so PE's sign is unaffected by it.
    const bool pe_phase_on = P.enable_pe_management || (P.enable_ip_filter && P.ip_filter_use_pe_phase);
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
      if (P.enable_ip_filter && P.ip_filter_use_pe_phase) ip_pe_phase_tick(); // per-IP per-phase harm census
      pe_phase_demands_ = 0;
    }
  }
  return metadata_in;
}

uint32_t sppam_plus::prefetcher_cache_fill(champsim::address addr, long /*set*/, long /*way*/, bool prefetch, champsim::address evicted_addr,
                                           uint32_t metadata_in)
{
  const uint64_t block = addr.to<uint64_t>() >> BLOCK_SHIFT;
  const bool pe_terms = P.enable_pe_management || (P.enable_ip_filter && (P.ip_filter_use_pe || P.ip_filter_pe_veto || P.ip_filter_use_pe_phase));
  // Our instruction prefetches are identified by membership in instr_pf_unused_ (populated at
  // issue), keeping them out of the data maps (shadow residency, pf_unused, pfht) -- and, crucially,
  // NOT stealing berti's data-prefetch fills, which a metadata tag bit would (berti uses bits 9-31).
  const bool is_instr_pf = ipred_ && instr_pf_unused_.count(block) != 0;
  if (!is_instr_pf) {
    // Shadow cache: any (data) fill marks the block resident.
    pred_->shadow_fill(block);
    if (prefetch)
      pf_unused_[block] = metadata_in; // freshly prefetched; carry full generation tag
  }
  bool had_evict = false, evict_was_unused = false;
  uint64_t evb = 0;
  if (evicted_addr.to<uint64_t>() != 0) {
    evb = evicted_addr.to<uint64_t>() >> BLOCK_SHIFT;
    if (ipred_) { // an instruction prefetch evicted before any demand use -> useless
      auto iit = instr_pf_unused_.find(evb);
      if (iit != instr_pf_unused_.end()) { ++instr_useless_; instr_pf_unused_.erase(iit); }
    }
    auto uit = pf_unused_.find(evb);
    evict_was_unused = (uit != pf_unused_.end());
    if (evict_was_unused) {
      const uint32_t tag = uit->second; const uint8_t src = tag & 1u;
      const int depth = (tag >> 1) & 15, order = (tag >> 5) & 7;
      const int scan = (tag >> 9) & 7;
      ++char_useless_[src]; ++useless_by_depth_[src][depth];
      if (src == 0) { ++useless_by_order_[order]; ++useless_by_scan_[scan]; }
      if (src == 1 && spp_) spp_->reward(false, evb); // SPP prefetch evicted unused -> negative feedback
      if (pf_evicted_unused_.size() < 300000) pf_evicted_unused_[evb] = tag; // watch for a later demand (untimely)
      // EXACT per-issuing-IP: this IP's prefetch was evicted unused -> count it, and watch evb for a
      // re-demand (untimely). All evicted-unused prefetches (not sampled), keyed by the issuing IP.
      if (P.ip_filter_depth_throttle) {
        if (auto pi = pf_issue_iph_.find(evb); pi != pf_issue_iph_.end()) {
          ++ip_ev_[pi->second];
          if (pf_evict_iph_.size() < 400000) pf_evict_iph_[evb] = pi->second;
          pf_issue_iph_.erase(pi);
        }
      }
      pf_unused_.erase(uit);
    }
    had_evict = true;
    sd_observe_fill(block, true); // set-duel churn metric: this fill displaced a valid line
    // A PINNED pfht_ entry for the evicted line resolves USELESS here (issued, filled, never used).
    // This is the unbiased counterpart to the USE resolve -- long-lived useless prefetches are only
    // caught at eviction, so we never overwrite an in-flight entry before this fires.
    if (P.enable_ip_filter || P.enable_pe_management) {
      pf_track& et = pfht_[evb % pfht_.size()];
      if (et.valid && et.block == evb) {
        if (P.enable_ip_filter) { ++ip_useless_[et.iph]; ++dbg_evict_; ip_age_tick(); }
        et.valid = false; // resolved (evicted unused)
      }
    }
    pred_->on_l2_evict(evb, cycle_, evict_was_unused);
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
  if (P.enable_pe_management || P.enable_ip_filter) {
    pf_track& e = pfht_[block % pfht_.size()];
    if (e.valid && e.block == block && !e.filled) {
      e.filled = true;
      e.lat = (real_cycle_ >= e.issue) ? (real_cycle_ - e.issue) : 0;
      if (pe_terms) { lat_sum_ += e.lat; ++lat_n_; }
      const int src = e.from_spp ? 1 : 0;
      if (!prefetch) {
        // Promoted: a demand merged into this prefetch's MSHR before it filled -> useful, timely.
        if (P.enable_ip_filter) { ++ip_useful_[e.iph]; ip_age_tick(); }
        if (P.enable_pe_management) i_upf_[src] += static_cast<double>(e.lat);
        if (P.enable_ip_filter && (P.ip_filter_use_pe || P.ip_filter_use_pe_phase)) { ip_pe_[e.iph] += static_cast<double>(e.lat); ++ip_pe_n_[e.iph]; }
        e.valid = false;
      } else if (pe_terms) {
        // I_LAT (Eq4): the demand sitting in the MSHR as this prefetch returns is delayed by
        // the prefetch's shared-resource SERVICE time. A true load is the full-weight victim;
        // a berti access is urgent (temporally close to its demand) but carries more slack.
        if (inflight_true_ > 0 || inflight_berti_ > 0) {
          const double w = (inflight_true_ > 0) ? 1.0 : P.pe_pf_demand_weight; // MAX-affected demand
          const double serv = (e.lat > P.pe_dram_lat_threshold) ? P.pe_serv_dram : P.pe_serv_llc;
          if (P.enable_pe_management) i_lat_[src] += w * serv;
          if (P.enable_ip_filter && (P.ip_filter_use_pe || P.ip_filter_use_pe_phase)) ip_pe_[e.iph] -= w * serv; // I_LAT (-)
        }
        // I_POLL setup: displaced a USEFUL line (a demand line or used prefetch) -> remember
        // the victim (+ its prefetch's IP); a later demand miss on it is a pollution miss.
        if (had_evict && !evict_was_unused)
          poll_[evb % poll_.size()] = poll_track{true, evb, e.from_spp, e.iph};
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
  // Per-trigger-IP accuracy filter: throttle triggers whose historical prefetch usefulness is low
  // (graph-walk/pointer-chase loads), sparing predictable streams. Never fully gates (1/trickle).
  // ip_filter_active_ is the auto-backoff: false when IP fails to discriminate (would drop too much useful).
  // Volume trickle (drop 1-1/div). When depth-throttle is on, UNTIMELY IPs are handled by the depth cap
  // (in the predictor); the volume trickle then applies only to TRULY-BAD IPs (wrong addr -> drop them).
  if (P.enable_ip_filter && ip_filter_active_ && ipf_should_apply(block)
      && !(P.ip_filter_depth_throttle && ip_is_untimely(iphash(cur_trigger_ip_)))) {
    const uint32_t iph = iphash(cur_trigger_ip_);
    uint32_t div = P.ip_filter_use_pe_phase ? ip_pe_phase_trickle_div(iph)
                 : P.ip_filter_use_pe ? ip_pe_trickle_div(iph) : ip_trickle_div(iph);
    if (P.ip_filter_pe_veto && div > 1 && ip_pe_positive(iph)) div = 1; // spare a clearly PE-positive IP
    if (div > 1 && (++ip_trickle_ctr_ % div) != 0)
      return false; // graded throttle (still issue 1/div so the IP keeps getting feedback)
  }
  // Set-duel: guard groups + graded follower throttle (redirect L2->LLC / drop). Final placement.
  if (!sd_decide(block, fill_l2))
    return false;
  ++pf_issued_;
  // EXACT per-issuing-IP timeliness: remember which IP issued this block, until it is used (timely) or
  // evicted-unused (then watched for a later re-demand = untimely). Bounded; cap guards a pathological run.
  if (P.enable_ip_filter && P.ip_filter_depth_throttle && pf_issue_iph_.size() < 400000)
    pf_issue_iph_[block] = static_cast<uint16_t>(iphash(cur_trigger_ip_));
  // Sampled attribution into the SHARED pfht_ (serves PE's I_UPF/I_LAT/I_POLL AND the per-IP filter):
  // hold 1/pe_sample_div of ISSUED prefetches. PINNED insert -- take only a FREE slot, never clobber
  // an in-flight (valid) entry, so a long-lived useless prefetch survives to its eviction resolve
  // (removing the bias that plagued a churn-overwrite table).
  if ((P.enable_pe_management || P.enable_ip_filter) && (++pe_sample_ctr_ % P.pe_sample_div == 0)) {
    pf_track& slot = pfht_[block % pfht_.size()];
    const bool stale = slot.valid && (real_cycle_ - slot.issue) > P.ip_track_timeout;
    if (!slot.valid || stale) {
      // sppam_b USELESS_ON_TIMEOUT: a stale in-flight entry sat unused past the timeout -> resolve it
      // USELESS (removing the under-sampling bias) before reusing its slot. A RECENT entry is pinned.
      if (stale && P.enable_ip_filter) { ++ip_useless_[slot.iph]; ++dbg_evict_; ip_age_tick(); }
      slot = pf_track{true, block, from_spp, real_cycle_, false, 0, static_cast<uint16_t>(iphash(cur_trigger_ip_))};
      ++dbg_ins_;
    } else ++dbg_skip_;
  }
  // Mark the shadow map at ISSUE (pending), so in-flight prefetches dedupe before they fill.
  // SPPAM's do_prefetch already marks its own path; SPP has no marking of its own, so without
  // this its in-flight prefetches (and any SPPAM re-prediction of the same block) are not
  // filtered until the fill lands. SPP always fills L2 (fill_l2=true).
  if (from_spp && fill_l2)
    pred_->shadow_fill(block);
  const bool enqueued = prefetch_line(block << BLOCK_SHIFT, fill_l2, gen_tag | (from_spp ? 1u : 0u)); // carry generation tag (diagnostics)
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
  if (P.enable_pe_ramp)
    fmt::print("[SPPAM+] pe-ramp: active_at_end={} avg_lat={:.0f} (gate: PE>{} & lat>{})\n", pe_ramp_active_, avg_lat(), P.pe_ramp_pe_min, P.pe_ramp_lat_min);
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
    const uint64_t iu = instr_useful_, il = instr_useless_;
    const double iacc = (iu + il) ? 100.0 * iu / (iu + il) : 0.0;
    fmt::print("[SPPAM+] instr-pf: demands={} issued={} useful={} useless={} accuracy={:.1f}% unencodable-delta={} (resident-at-end={})\n",
               ipred_->demands(), ipred_->issued(), iu, il, iacc, ipred_->unencodable(), instr_pf_unused_.size());
  }
  fmt::print("[SPPAM+] prefetches issued: {} | squashed-redundant: {} | filter-passed[region-absent: {}, bit-clear: {}]\n",
             pf_issued_, pf_squashed_redundant_, pf_pass_region_absent_, pf_pass_bit_clear_);
  if (P.enable_ip_filter) fmt::print("[SPPAM+] ip-filter: active(end)={} thr={}% budget={}% | sampletab ins={} skip={} use-res={} evict-res={}\n", ip_filter_active_, P.ip_filter_threshold, P.ip_filter_max_useful_loss, dbg_ins_, dbg_skip_, dbg_use_, dbg_evict_);
  for (int s = 0; s < 2; ++s) {
    const uint64_t u = char_useful_[s], ul = char_useless_[s], tm = char_timely_[s];
    const double acc = (u + ul) ? 100.0 * u / (u + ul) : 0.0;
    const double untimely = ul ? 100.0 * tm / ul : 0.0; // of useless: fraction later demanded (untimely, addr right)
    fmt::print("[SPPAM+] accuracy[{}]: useful={} useless={} acc={:.1f}% | of-useless untimely(addr-right)={:.1f}% truly-bad={:.1f}%\n",
               s == 0 ? "SPPAM" : "SPP", u, ul, acc, untimely, 100.0 - untimely);
    // useless share by lookahead depth (where does the bad flow concentrate?)
    std::string dh;
    for (int d = 0; d < 16; ++d) {
      const uint64_t du = useful_by_depth_[s][d], dl = useless_by_depth_[s][d];
      if (du + dl) dh += fmt::format(" d{}:{:.0f}%x{}", d, 100.0 * dl / (du + dl), (du + dl) / 1000);
    }
    fmt::print("[SPPAM+]   {} useless-by-depth (useless%%xKtotal):{}\n", s == 0 ? "SPPAM" : "SPP", dh);
  }
  {
    std::string oh;
    for (int o = 0; o < 8; ++o) {
      const uint64_t ou = useful_by_order_[o], ol = useless_by_order_[o];
      if (ou + ol) oh += fmt::format(" o{}:{:.0f}%x{}", o, 100.0 * ol / (ou + ol), (ou + ol) / 1000);
    }
    fmt::print("[SPPAM+]   SPPAM useless-by-order (useless%%xKtotal):{}\n", oh);
    std::string sh;
    for (int sc = 0; sc < 8; ++sc) {
      const uint64_t su = useful_by_scan_[sc], sl = useless_by_scan_[sc];
      if (su + sl) sh += fmt::format(" s{}:{:.0f}%x{}", sc, 100.0 * sl / (su + sl), (su + sl) / 1000);
    }
    fmt::print("[SPPAM+]   SPPAM useless-by-scan (useless%%xKtotal):{}\n", sh);
  }
  {
    // Per-trigger-IP filter potential: if we throttle IPs whose SPPAM/SPP prefetch usefulness < T,
    // how much useless do we remove vs useful do we lose? Discriminating IFF useless-removed >> useful-lost.
    uint64_t TU = 0, TL = 0; int active = 0;
    for (int i = 0; i < IPBK; ++i) { TU += ip_useful_[i]; TL += ip_useless_[i]; if (ip_useful_[i] + ip_useless_[i]) ++active; }
    fmt::print("[SPPAM+]   per-IP filter (active IP-buckets={}, total useful={} useless={}):\n", active, TU, TL);
    for (double T : {0.10, 0.20, 0.30}) {
      uint64_t ru = 0, rl = 0; // useful lost, useless removed by throttling IPs below T
      for (int i = 0; i < IPBK; ++i) {
        uint64_t u = ip_useful_[i], l = ip_useless_[i];
        if (u + l == 0) continue;
        if (static_cast<double>(u) / (u + l) < T) { ru += u; rl += l; }
      }
      fmt::print("[SPPAM+]     throttle IPs<{:.0f}%%: useless-removed={:.1f}%% useful-lost={:.1f}%%\n",
                 100 * T, TL ? 100.0 * rl / TL : 0, TU ? 100.0 * ru / TU : 0);
    }
  }
  if (P.enable_pe_management)
    fmt::print("[SPPAM+] PE  SPPAM: I_UPF={:.0f} I_POLL={:.0f} I_LAT={:.0f} -> throttle 1/{} | SPP: I_UPF={:.0f} I_POLL={:.0f} I_LAT={:.0f} -> throttle 1/{}\n",
               i_upf_[0], i_poll_[0], i_lat_[0], pe_throttle_[0], i_upf_[1], i_poll_[1], i_lat_[1], pe_throttle_[1]);
  if (P.enable_pe_management)
    fmt::print("[SPPAM+] PE per-phase sign: SPPAM {}/{} phases PE<=0 ({:.0f}%%) | SPP {}/{} phases PE<=0 ({:.0f}%%)\n",
               pe_neg_phases_[0], pe_phases_[0], pe_phases_[0]?100.0*pe_neg_phases_[0]/pe_phases_[0]:0.0,
               pe_neg_phases_[1], pe_phases_[1], pe_phases_[1]?100.0*pe_neg_phases_[1]/pe_phases_[1]:0.0);
  if (P.enable_ip_filter && P.ip_filter_depth_throttle) {
    uint64_t sev = 0, sun = 0, nun = 0, nbad = 0;
    for (int i = 0; i < IPBK; ++i) { sev += ip_ev_[i]; sun += ip_untimely_[i];
      if (ip_ev_[i] >= P.ip_filter_min_samples) { if (ip_is_untimely(static_cast<uint32_t>(i))) ++nun; else ++nbad; } }
    fmt::print("[SPPAM+] depth-throttle: per-IP evicted-unused={} untimely={} ({:.0f}%%) | judged IPs: {} untimely->depth, {} truly-bad->volume\n",
               sev, sun, sev ? 100.0 * sun / sev : 0.0, nun, nbad);
    const double avg_tot = occ_n_ ? static_cast<double>(occ_tot_) / occ_n_ : 0.0;
    const double avg_up = occ_n_ ? static_cast<double>(occ_up_) / occ_n_ : 0.0;
    fmt::print("[SPPAM+] MLP: avg MSHR occ total={:.2f} upstream(dem+berti)={:.2f} pf(sppam+)={:.2f} | mshr_size={}\n",
               avg_tot, avg_up, avg_tot - avg_up, cache_->get_mshr_size());
  }
  if (P.enable_ip_filter && P.ip_filter_use_pe_phase) {
    uint32_t judged = 0, thr = 0, thr_avgpos = 0; uint64_t thr_vol = 0, tot_vol = 0;
    for (int i = 0; i < IPBK; ++i) {
      if (ip_ph_active_[i] < P.ip_filter_min_samples) continue;
      ++judged; const uint64_t vol = static_cast<uint64_t>(ip_useful_[i]) + ip_useless_[i]; tot_vol += vol;
      if (ip_pe_phase_trickle_div(static_cast<uint32_t>(i)) > 1) { ++thr; thr_vol += vol;
        if (ip_pe_n_[i] && ip_pe_[i] / ip_pe_n_[i] >= 0.0) ++thr_avgpos; } // throttled but net-useful on avg = mis-attrib
    }
    fmt::print("[SPPAM+] pe-phase throttle: {}/{} judged IPs throttled, {:.0f}%% of pf volume; {} throttled IPs have avg-PE>=0 (mis-attrib)\n",
               thr, judged, tot_vol ? 100.0 * thr_vol / tot_vol : 0.0, thr_avgpos);
  }
  if (P.enable_pe_management)
    fmt::print("[SPPAM+] PE strict-harm(active): SPPAM {}/{} active phases PE<0 ({:.0f}%%) | SPP {}/{} ({:.0f}%%)\n",
               pe_negact_[0], pe_active_[0], pe_active_[0]?100.0*pe_negact_[0]/pe_active_[0]:0.0,
               pe_negact_[1], pe_active_[1], pe_active_[1]?100.0*pe_negact_[1]/pe_active_[1]:0.0);
}

champsim::modules::prefetcher::register_module<sppam_plus> sppam_plus_module("SPPAM_PLUS_V2");
