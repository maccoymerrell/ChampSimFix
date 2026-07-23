// Faithful, runtime-parameterized reimplementation of the original SPPAM
// prediction core (DPC4 prefetcher/sppam), with the timing/bandwidth/dueling/
// fairness machinery removed (not modeled by this trace-driven tool).
//
// Addresses are 64B block numbers throughout. A "page"/region spans
// 2^(page_bits-6) blocks. Tables return live pointers into fixed storage (no
// per-lookup copies); shadow-map bits are read directly (no allocation).
#ifndef SPPAM_DSE_SPPAM_PREDICTOR_H
#define SPPAM_DSE_SPPAM_PREDICTOR_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "access_kind.h"
#include "conf_table.h"
#include "dedup_analyzer.h"
#include "lru_table.h"
#include "params.h"
#include "prefetch_sink.h"

namespace sppam_dse
{

class sppam_predictor
{
public:
  explicit sppam_predictor(const params& p, prefetch_sink* sink)
      : P(p), sink_(sink), region_shift_(p.region_bits - 6), page_shift_(p.page_bits - 6), page_region_shift_(p.page_bits - p.region_bits), blocks_per_region_(uint64_t{1} << (p.region_bits - 6)),
        regions_(p.region_sets, p.region_ways, p.stage_sets, p.stage_ways, region_set_idx{p.region_hash, p.region_page_aligned_sets ? (p.page_bits - p.region_bits) : 0}, region_tag_idx{}),
        llc_regions_(p.llc_region_sets, p.llc_region_ways, region_set_idx{false, p.region_page_aligned_sets ? (p.page_bits - p.region_bits) : 0}, region_tag_idx{}),
        cpt_(p.cpt_sets, p.cpt_ways)
  {
    std::size_t orders = 1;
    for (uint32_t s = P.pattern_size; s > P.min_pattern_size; s /= 2)
      ++orders;
    const std::size_t neg_sets = P.negative_table_sets ? P.negative_table_sets : P.pattern_table_sets; // 0 = inherit
    const std::size_t neg_ways = P.negative_table_ways ? P.negative_table_ways : P.pattern_table_ways;
    for (std::size_t o = 0; o < orders; ++o) {
      pattern_tables_.emplace_back(P.pattern_table_sets, P.pattern_table_ways);
      negative_pattern_tables_.emplace_back(neg_sets, neg_ways); // decoupled, sizable-down backward table
    }
    if (P.dedup_analysis)
      dedup_ = std::make_unique<dedup_analyzer>();
    if (P.enable_region_thrash_throttle)
      rev_tags_.assign(P.region_thrash_table, ~uint64_t{0}); // recently-evicted region tags
    // On region eviction: feed the dedup measurement (if on) AND the region-thrash
    // re-reference detector (was this region soon needed again?).
    regions_.main_.on_evict = [this](const region_type& r) { on_region_evict(r); };
    if (P.enable_two_level) {
      regions_.staging = true;
      const int lo = P.stage_promote_min, hi = P.stage_promote_max;
      regions_.promote_ok = [lo, hi](const region_type& r) -> bool {
        int pop = 0; for (bool b : r.access_map) if (b) ++pop; // touched blocks
        return pop >= lo && pop <= hi; // drop severely-fragmented (<lo) and fully-used (>hi); promote medium
      };
    }
    if (P.enable_resid_bloom && P.resid_bloom_bits > 0)
      resid_bloom_.assign((P.resid_bloom_bits + 7) / 8, 0);
    if (P.enable_am_bloom) {
      am_S_ = P.am_bloom_size > 0 ? P.am_bloom_size : 1;
      am_k_ = P.am_bloom_k > 0 ? P.am_bloom_k : 1;
      am_T_ = P.am_bloom_clear_thresh > 0 ? P.am_bloom_clear_thresh : 1;
      am_clear_frac_ = P.am_bloom_clear_frac;
      am_bloom_.assign(blocks_per_region_ * static_cast<std::size_t>(am_S_), 0);
      am_evct_.assign(blocks_per_region_, 0);
      am_hand_.assign(blocks_per_region_, 0);
    }
    if (P.enable_region_staging) {
      std::size_t n = P.staging_entries ? P.staging_entries : 1;
      staging_tag_.assign(n, ~uint64_t{0});
      staging_cnt_.assign(n, 0);
    }
    if (P.enable_cold_start && P.cold_start_pc) {
      std::size_t n = 1; while (n < P.cold_start_entries) n <<= 1;
      cs_delta_.assign(n, 0); cs_cnt_.assign(n, 0);
    }
    if (P.delta_pht) {
      dpht_.assign(static_cast<std::size_t>(P.delta_pht_sets) * P.delta_pht_ways, dpht_entry{});
      dpht_spec_.assign(blocks_per_region_, 0);
    }
    if (P.walk_accumulate) {
      walk_spec_.assign(blocks_per_region_, 0);
      walk_issued_.assign(blocks_per_region_, 0);
      walk_acc_.assign(blocks_per_region_, 0);
    }
    delta_psel_ = P.delta_pht_sd_max / 2;
    walk_psel_ = P.delta_pht_sd_max / 2;
    // Region-table eviction policy (victim = MAX evict_rank; higher = evict first). Pure policies, no LRU blend.
    if (P.region_evict_policy == 1) {                          // MRU: evict most-recently-used (smallest age)
      regions_.main_.evict_rank = [](const region_type&, uint64_t age) -> int64_t { return -static_cast<int64_t>(age); };
    } else if (P.region_evict_policy == 2) {                   // RANDOM
      regions_.main_.evict_rank = [this](const region_type&, uint64_t) -> int64_t {
        evict_lfsr_ ^= evict_lfsr_ << 13; evict_lfsr_ ^= evict_lfsr_ >> 17; evict_lfsr_ ^= evict_lfsr_ << 5;
        return static_cast<int64_t>(evict_lfsr_);
      };
    } else if (P.region_evict_policy == 3) {                   // ENTROPY: evict FARTHEST from 50/50, keep near-even maps
      const int target = static_cast<int>(blocks_per_region_ / 2);
      regions_.main_.evict_rank = [target](const region_type& r, uint64_t) -> int64_t {
        int pop = 0; for (bool b : r.access_map) if (b) ++pop;
        return std::abs(pop - target);                          // 0 at 50/50 (keep), target at full/empty (evict)
      };
    } else if (P.region_evict_policy == 4) {                   // evict HIGH residency (prefetch_map full = fully cached)
      regions_.main_.evict_rank = [](const region_type& r, uint64_t) -> int64_t {
        int res = 0; for (bool b : r.prefetch_map) if (b) ++res; return res;
      };
    } else if (P.region_evict_policy == 5) {                   // evict LOW residency (prefetch_map empty = footprint gone)
      const int64_t bpr = static_cast<int64_t>(blocks_per_region_);
      regions_.main_.evict_rank = [bpr](const region_type& r, uint64_t) -> int64_t {
        int res = 0; for (bool b : r.prefetch_map) if (b) ++res; return bpr - res;
      };
    } else if (P.region_evict_policy == 6) {                   // evict by USEFULNESS: no good prefetches came from it
      regions_.main_.evict_rank = [](const region_type& r, uint64_t age) -> int64_t {
        // net-useless (dead - used) dominates; age breaks ties among new/unjudged regions -> LRU fallback
        return (static_cast<int64_t>(r.pf_dead) - static_cast<int64_t>(r.pf_used)) * 4096 + static_cast<int64_t>(age);
      };
    } else if (P.region_evict_policy == 7) {                   // evict FEWEST demand misses (least prefetch value)
      regions_.main_.evict_rank = [](const region_type& r, uint64_t age) -> int64_t {
        return -static_cast<int64_t>(r.dmiss) * 4096 + static_cast<int64_t>(age); // fewest misses -> highest rank
      };
    }
    ip_n_ = P.ip_table_entries ? P.ip_table_entries : 1; // per-IP table depth (DSE-swept)
    ip_mask_ = ip_n_ - 1;                                // iphash() index mask (ip_n_ must be a power of two)
    if (P.enable_ip_filter) {
      ip_useful_.assign(ip_n_, 0);
      ip_useless_.assign(ip_n_, 0);
      ip_gate_ctr_.assign(ip_n_, 0);
      if (P.ip_filter_depth_throttle) { ip_ev_.assign(ip_n_, 0); ip_untimely_.assign(ip_n_, 0); } // depth-throttle only
      if (P.bwd_useful_gate) { ip_bwd_useful_.assign(ip_n_, 0); ip_bwd_useless_.assign(ip_n_, 0); } // backward self-throttle only
    }
    if ((P.pattern_validate || P.enable_ip_filter) && P.pv_sample_directmap) {
      std::size_t n = 1; while (n < P.pv_sample_cap) n <<= 1;   // fixed direct-mapped sample table (power of two)
      pf_sdm_.assign(n, pf_samp_t{}); pf_sdm_mask_ = static_cast<uint32_t>(n - 1);
      pv_div_cur_ = P.pv_div_min ? P.pv_div_min : 1;            // adaptive rate starts at the floor, backs off as churn rises
    }
    if (P.enable_perceptron_filter) {                          // perceptron weight tables (prototype)
      std::size_t n = 1; while (n < P.perc_pc_entries) n <<= 1;
      pw_pc_.assign(n, 0); pw_pc_mask_ = static_cast<uint32_t>(n - 1);
      // Context tables sized PERC_ENGINES x base so perc_engine_split can index them by (engine, bucket) -- a
      // separate learned weight per engine (the split-only partitions past the base go unused when the flag is off).
      pw_eng_.assign(PERC_ENGINES, 0); pw_dep_.assign(PERC_ENGINES * 16, 0);  // pw_off_ dropped (offset feature removed)
      pw_use_.assign(PERC_ENGINES * 8, 0); pw_tim_.assign(PERC_ENGINES * 8, 0); pw_conf_.assign(PERC_ENGINES * 16, 0);
      { std::size_t sn = 1; while (sn < P.perc_sig_entries) sn <<= 1; pw_sig_.assign(sn, 0); pw_sig_mask_ = static_cast<uint32_t>(sn - 1); }
      { std::size_t tn = 1; while (tn < P.perc_track_cap) tn <<= 1; perc_track_.assign(tn, perc_ent{}); perc_track_mask_ = static_cast<uint32_t>(tn - 1); }
      if (P.perc_dense_train) { std::size_t dn = 1; while (dn < P.perc_dense_cap) dn <<= 1; perc_dense_.assign(dn, dense_ent{}); perc_dense_mask_ = static_cast<uint32_t>(dn - 1); }
      if (P.perc_victim) { std::size_t vn = 1; while (vn < P.perc_victim_cap) vn <<= 1; perc_victim_.assign(vn, dense_ent{}); perc_victim_mask_ = static_cast<uint32_t>(vn - 1); }
      if (P.perc_pe_gate || (P.perc_feat_mask & 0x700000u)) { ip_pe_sum_.assign(ip_n_, 0.0f); ip_lat_sum_.assign(ip_n_, 0.0f); ip_poll_sum_.assign(ip_n_, 0.0f); ip_pe_cnt_.assign(ip_n_, 0); }
      pw_mshr_.assign(PERC_ENGINES * 16, 0); pw_guse_.assign(PERC_ENGINES * 16, 0); pw_gtim_.assign(PERC_ENGINES * 16, 0); // aggregate-harm tables (per-engine capable)
      pw_hit_.assign(PERC_ENGINES * 16, 0); pw_conj_.assign(PERC_CONJ, 0); // hit-rate + the {use,tim,mshr,hit} conjunction (non-additive interaction)
      pw_usemshr_.assign(PW_USEMSHR, 0); pw_timmshr_.assign(PW_TIMMSHR, 0); pw_confmshr_.assign(PW_CONFMSHR, 0); pw_crit_.assign(PW_CRIT, 0); // DSE-derived interactions
      pw_upf_.assign(16, 0); pw_lat_.assign(16, 0); pw_poll_.assign(16, 0); // 3-way PE split (separate per-IP benefit/bandwidth/pollution weights)
      { std::size_t pn = 1; while (pn < P.perc_pc_entries) pn <<= 1; pw_pcpath_.assign(pn, 0); pw_pcpath_mask_ = static_cast<uint32_t>(pn - 1);   // PC-path (chain of recent trigger PCs)
        pw_pcdelta_.assign(pn, 0); pw_pcdelta_mask_ = static_cast<uint32_t>(pn - 1); }                                                          // PC XOR delta (PC<->data correlation)
      pip_bias_.assign(ip_n_, 0); // per-IP bias (per-context personalization)
      if (ip_useful_.empty()) { ip_useful_.assign(ip_n_, 0); ip_useless_.assign(ip_n_, 0); } // per-IP usefulness (f[4]) LIVE even when the ip-filter is off (credited at perc_resolve)
      if (ip_ev_.empty()) { ip_ev_.assign(ip_n_, 0); ip_untimely_.assign(ip_n_, 0); }         // per-IP untimeliness (f[5]) LIVE: perc_resolve seeds the evicted-unused watch, operate() scores the re-demand
    }
  }

  ~sppam_predictor()
  {
    if (dbg_amap_regs_)
      std::fprintf(stderr, "[amap] %s evicted_regions=%llu mean_fill=%.1f%% (%.1f/%d blocks)  sparse(<=1)=%.1f%% full=%.1f%%\n",
                   P.name.c_str(), (unsigned long long)dbg_amap_regs_,
                   100.0 * static_cast<double>(dbg_amap_bits_) / (static_cast<double>(dbg_amap_regs_) * blocks_per_region_),
                   static_cast<double>(dbg_amap_bits_) / static_cast<double>(dbg_amap_regs_), static_cast<int>(blocks_per_region_),
                   100.0 * dbg_amap_sparse_ / dbg_amap_regs_, 100.0 * dbg_amap_full_ / dbg_amap_regs_);
    if (P.region_density_report && rd_n_) {
      const int bpr = static_cast<int>(blocks_per_region_);
      auto frac = [&](uint64_t lo, uint64_t hi) { uint64_t s = 0; for (uint64_t p = lo; p <= hi && p < rd_fill_hist_.size(); ++p) s += rd_fill_hist_[p]; return 100.0 * s / rd_n_; };
      std::fprintf(stderr, "[rdens] %s regions=%llu mean_fill=%.1f%%  |  FILL DIST: =0:%.1f%% 1-2:%.1f%% 3-4:%.1f%% 5-8:%.1f%% 9-16:%.1f%% 17-32:%.1f%% 33-%d:%.1f%% near-full(>=%d):%.1f%%\n",
                   P.name.c_str(), (unsigned long long)rd_n_, 100.0 * rd_touched_ / (rd_n_ * bpr),
                   frac(0, 0), frac(1, 2), frac(3, 4), frac(5, 8), frac(9, 16), frac(17, 32), bpr - 3, frac(33, bpr - 3), bpr - 2, frac(bpr - 2, bpr));
      const int Gs[4] = {2, 4, 8, 16};
      std::fprintf(stderr, "[rdens] %s GRANULARITY (of %d-block map): ", P.name.c_str(), bpr);
      for (int gi = 0; gi < 4; ++gi)
        std::fprintf(stderr, "G=%2d(%d bits): lossless=%.1f%% overfetch=%.2fx  ", Gs[gi], bpr / Gs[gi],
                     100.0 * rd_lossless_[gi] / rd_n_, rd_touched_ ? 1.0 + static_cast<double>(rd_overfetch_[gi]) / rd_touched_ : 1.0);
      std::fprintf(stderr, "\n[rdens] %s distinct_footprints=%zu (%.1f%% of regions -> dictionary size)\n",
                   P.name.c_str(), rd_footprints_.size(), 100.0 * rd_footprints_.size() / rd_n_);
    }
    if (dedup_)
      std::fprintf(stderr, "%s\n", dedup_->report(P.name).c_str());
    if (P.enable_region_thrash_throttle && dbg_miss_)
      std::fprintf(stderr, "[thrash] %s rereference_rate=%.3f ema=%.3f (rrmiss=%llu miss=%llu)\n",
                   P.name.c_str(), static_cast<double>(dbg_rrmiss_) / static_cast<double>(dbg_miss_), region_thrash_ema_,
                   (unsigned long long)dbg_rrmiss_, (unsigned long long)dbg_miss_);
    if (P.prob_drop_prefetches) {
      uint64_t tc = 0, td = 0; std::string per;
      for (int u = 0; u < 16; ++u) { tc += dbg_dchk_[u]; td += dbg_ddrop_[u];
        if (dbg_dchk_[u]) per += " u" + std::to_string(u) + "=" + std::to_string(dbg_ddrop_[u] * 100 / dbg_dchk_[u]) + "%(" + std::to_string(dbg_dchk_[u]) + ")"; }
      std::fprintf(stderr, "[probdrop] %s checks=%llu dropped=%.1f%% globUseIdx=%d regLifeIdx=%d cutoff=%d usePatConf=%d | drop%% by current_usefulness:%s\n",
                   P.name.c_str(), (unsigned long long)tc, tc ? 100.0 * td / tc : 0.0,
                   global_usefulness_index_, region_lifespan_index_, (int)P.pattern_usefulness_cutoff, (int)use_pattern_confidence(), per.c_str());
    }
    if (P.neg_online_train)
      std::fprintf(stderr, "[bwd] %s scans=%llu nonzero_pred=%llu issued=%llu\n", P.name.c_str(),
                   (unsigned long long)dbg_bwd_scan_, (unsigned long long)dbg_bwd_pred_, (unsigned long long)dbg_bwd_issued_);
    if (P.walk_accumulate)
      std::fprintf(stderr, "[walk] %s steps=%llu issued=%llu psel=%d/%d\n", P.name.c_str(),
                   (unsigned long long)dbg_walk_steps_, (unsigned long long)dbg_walk_issued_, walk_psel_, P.delta_pht_sd_max);
    if (P.enable_ip_filter) {
      uint64_t tu = 0, tl = 0; int active = 0;
      for (std::size_t i = 0; i < ip_useful_.size(); ++i) { tu += ip_useful_[i]; tl += ip_useless_[i]; if (ip_useful_[i] + ip_useless_[i]) ++active; }
      uint64_t tev = 0, tut = 0; int nunt = 0;
      for (std::size_t i = 0; i < ip_ev_.size(); ++i) { tev += ip_ev_[i]; tut += ip_untimely_[i]; if (ip_is_untimely(static_cast<uint32_t>(i))) ++nunt; }
      std::fprintf(stderr, "[ipf] %s sampled_useful=%llu sampled_useless=%llu active_ips=%d triggers_throttled=%llu | ev=%llu untimely=%llu untimely_ips=%d\n",
                   P.name.c_str(), (unsigned long long)tu, (unsigned long long)tl, active, (unsigned long long)dbg_ip_throttled_,
                   (unsigned long long)tev, (unsigned long long)tut, nunt);
    }
    if (P.enable_perceptron_filter) {
      const uint64_t seen = dbg_perc_keep_ + dbg_perc_explore_ + dbg_perc_drop_;
      const double K = dbg_keep_res_ ? 100.0 * static_cast<double>(dbg_keep_res_u_) / static_cast<double>(dbg_keep_res_) : 0.0;
      const double Dd = dbg_drop_res_ ? 100.0 * static_cast<double>(dbg_drop_res_u_) / static_cast<double>(dbg_drop_res_) : 0.0;
      std::fprintf(stderr, "[perc] %s scored=%llu drop_rate=%.1f%% trained=%llu mask=0x%x | DISCRIM kept_useful=%.1f%% dropped_useful=%.1f%% gap=%.1f\n",
                   P.name.c_str(), (unsigned long long)seen,
                   seen ? 100.0 * static_cast<double>(dbg_perc_drop_) / static_cast<double>(seen) : 0.0,
                   (unsigned long long)dbg_perc_train_, P.perc_feat_mask, K, Dd, K - Dd);
      // TRAINING FUNNEL: per-engine resolved(+/-) and trained(+/-), plus filter losses. Engines: 0=fwd 1=bwd 2=SPP 3=BG.
      uint64_t rp = 0, rn = 0, tp = 0, tn = 0;
      for (int e = 0; e < PERC_ENGINES; ++e) { rp += dbg_res_pos_[e]; rn += dbg_res_neg_[e]; tp += dbg_tr_pos_[e]; tn += dbg_tr_neg_[e]; }
      std::fprintf(stderr, "[perc-funnel] %s resolved=%llu (pos=%llu neg=%llu) -> skip_margin=%llu skip_conf=%llu -> trained pos=%llu neg=%llu\n",
                   P.name.c_str(), (unsigned long long)(rp + rn), (unsigned long long)rp, (unsigned long long)rn,
                   (unsigned long long)dbg_skip_margin_, (unsigned long long)dbg_skip_conf_, (unsigned long long)tp, (unsigned long long)tn);
      std::fprintf(stderr, "[perc-igate] %s perc_instr_pct=%d (thresh=%d gate=%d) force_keeps=%llu\n",
                   P.name.c_str(), perc_instr_pct_, P.perc_instr_gate_pct, (int)P.perc_instr_gate, (unsigned long long)dbg_perc_igate_);
      if (P.perc_dense_train || P.perc_victim || P.perc_pe_gate)
        std::fprintf(stderr, "[perc-v2] %s dense=%d victim=%d(hits=%llu) pe_gate=%d(vetoes=%llu)\n", P.name.c_str(),
                     (int)P.perc_dense_train, (int)P.perc_victim, (unsigned long long)dbg_victim_hit_,
                     (int)P.perc_pe_gate, (unsigned long long)dbg_perc_pegate_);
      if (P.perc_dump_weights && dbg_fh_n_) {
        // RAW PE audit (cycles): I_UPF should track fill latency (LLC ~35 / DRAM ~hundreds), I_LAT the queueing above
        // the DRAM base (0 idle, grows w/ load), I_POLL the pollution cost. If these are 0 or absurd, PE is broken.
        std::fprintf(stderr, "[feat-audit] %s RAW-PE cycles: I_UPF avg=%.1f max=%.0f (n=%llu) | I_LAT avg=%.1f max=%.0f | I_POLL avg=%.1f max=%.0f (n=%llu)\n",
                     P.name.c_str(), dbg_upf_n_ ? dbg_upf_sum_ / dbg_upf_n_ : 0.0, dbg_upf_max_, (unsigned long long)dbg_upf_n_,
                     dbg_upf_n_ ? dbg_lat_sum_ / dbg_upf_n_ : 0.0, dbg_lat_max_,
                     dbg_poll_n_ ? dbg_poll_sum_ / dbg_poll_n_ : 0.0, dbg_poll_max_, (unsigned long long)dbg_poll_n_);
        static const char* fn[PERC_NF] = {"PC","off","eng","dep","use","tim","conf","sig","mshr","guse","gtim","hit","conj","pcpath","pcdelta","use^m","tim^m","conf^m","crit","I_UPF","I_LAT","I_POLL"};
        const uint32_t mm = P.perc_feat_mask;
        for (int i = 0; i < PERC_NF; ++i) {
          double sum = 0; uint64_t nz = 0; for (int b = 0; b < 16; ++b) { sum += static_cast<double>(b) * dbg_fh_[i][b]; if (b) nz += dbg_fh_[i][b]; }
          char hist[128]; int off = 0; for (int b = 0; b < 16 && off < 110; ++b) off += std::snprintf(hist + off, sizeof(hist) - off, "%llu%s", (unsigned long long)((dbg_fh_[i][b] * 100 + dbg_fh_n_ / 2) / dbg_fh_n_), b < 15 ? "," : "");
          const bool on = (i == 0) ? (mm & 1) : (i == 2) ? (mm & 4) : (mm & (1u << i)); // rough: is this table masked in?
          std::fprintf(stderr, "[feat-audit] %s f%-2d %-7s %s mean=%.2f nonzero=%.0f%% hist%%=[%s]\n", P.name.c_str(), i, fn[i],
                       on ? "ON " : "off", sum / dbg_fh_n_, 100.0 * nz / dbg_fh_n_, hist);
        }
      }
      const uint64_t up = dbg_used_pe_pos_, un = dbg_used_pe_neg_;
      std::fprintf(stderr, "[perc-misalign] %s demand-USED prefetches: PE>=0=%llu PE<0=%llu (used-but-negative-PE=%.1f%% <- PE-vs-IPC misalignment)\n",
                   P.name.c_str(), (unsigned long long)up, (unsigned long long)un,
                   (up + un) ? 100.0 * static_cast<double>(un) / static_cast<double>(up + un) : 0.0);
      for (int e = 0; e < PERC_ENGINES; ++e)
        std::fprintf(stderr, "[perc-funnel]   eng%d(%s) resolved pos=%llu neg=%llu | trained pos=%llu neg=%llu\n", e,
                     e == 0 ? "fwd" : e == 1 ? "bwd" : e == 2 ? "SPP" : "BG",
                     (unsigned long long)dbg_res_pos_[e], (unsigned long long)dbg_res_neg_[e],
                     (unsigned long long)dbg_tr_pos_[e], (unsigned long long)dbg_tr_neg_[e]);
      if (P.perc_dump_weights) perc_dump();
    }
    if (P.pattern_validate) {
      uint64_t tot = dbg_pv_bad_ + dbg_pv_good_; if (!tot) tot = 1;
      std::fprintf(stderr, "[pv] %s triggers_bad=%.1f%% patterns_scored=%zu (bad=%zu) samples_live=%zu\n", P.name.c_str(),
                   100.0 * dbg_pv_bad_ / tot, pat_val_.size(),
                   [&]{ std::size_t b = 0; for (auto& kv : pat_val_) { uint32_t t = kv.second.u + kv.second.n; if (t >= (uint32_t)P.pv_min_samples && 100u*kv.second.u < (uint32_t)P.pv_bad_pct*t) ++b; } return b; }(),
                   pf_sample_.size());
    }
    if (P.delta_pht) {
      uint64_t term = dbg_delta_used_ + dbg_delta_unused_; if (!term) term = 1;
      std::fprintf(stderr, "[dpht] %s issued=%llu add=%llu used=%llu unused=%llu  add_rate=%.3f unused_rate=%.3f  add_ema=%.3f unused_ema=%.3f\n",
                   P.name.c_str(), (unsigned long long)dbg_dpht_issued_, (unsigned long long)dbg_delta_add_,
                   (unsigned long long)dbg_delta_used_, (unsigned long long)dbg_delta_unused_,
                   static_cast<double>(dbg_delta_add_) / static_cast<double>(term),
                   static_cast<double>(dbg_delta_unused_) / static_cast<double>(term), delta_add_ema_, delta_unused_ema_);
      std::fprintf(stderr, "       hit_ema=%.3f psel=%d/%d\n", demand_hit_ema_, delta_psel_, P.delta_pht_sd_max);
    }
    if (P.enable_cross_page && xpage_entries_)
      std::fprintf(stderr, "[xpage] %s region_entries=%llu predicted=%.1f%% pf_issued=%llu keys=%zu\n",
                   P.name.c_str(), (unsigned long long)xpage_entries_, 100.0 * xpage_fire_ / xpage_entries_,
                   (unsigned long long)xpage_issued_, xpht_.size());
  }

  void operate(uint64_t block, uint64_t ip, bool cache_hit, bool useful_prefetch, bool delta_additive, atype type, uint64_t cycle, double pf_value = 0.0)
  {
    cycle_ = cycle;
    region_just_created_ = false; // set by add_to_pagemap when this access allocates a fresh region entry
    if (P.region_evict_policy == 7 && !cache_hit && (type == atype::LOAD || type == atype::RFO)) { // fewest-misses policy
      region_type* r = regions_.probe_key(region_of(block)); if (r && r->dmiss < 65535) ++r->dmiss;
    }
    // Depth-throttle: a demand for a block that was an evicted-unused prefetch = UNTIMELY (right address, evicted
    // before use) -> attribute to the prefetch's IP so it gets a shallower (timelier) depth, not a volume drop.
    if (((P.enable_ip_filter && P.ip_filter_depth_throttle) || P.enable_perceptron_filter) && !evicted_unused_.empty() &&
        (type == atype::LOAD || type == atype::RFO)) {
      auto eu = evicted_unused_.find(block);
      if (eu != evicted_unused_.end()) {
        if (!ip_untimely_.empty()) ++ip_untimely_[eu->second];                          // per-IP untimely numerator (f[5])
        perc_untimely_ema_ += (1.0 - perc_untimely_ema_) * (1.0 / 1024.0);              // global untimely numerator (f[10])
        evicted_unused_.erase(eu);
      }
    }
    // Demand-miss-rate EMA (the LLC-pressure half of the bandwidth-feedback index).
    if (P.enable_bw_feedback)
      miss_ema_ += (((cache_hit ? 0.0 : 1.0) - miss_ema_) * (1.0 / 256.0));
    if (P.delta_pht_selfthrottle && (type == atype::LOAD || type == atype::RFO)) // demand hit-rate = coverage saturation
      demand_hit_ema_ += ((cache_hit ? 1.0 : 0.0) - demand_hit_ema_) * (1.0 / 1024.0);
    if (P.enable_perceptron_filter && (type == atype::LOAD || type == atype::RFO)) { // perceptron per-demand context (all live, ip-filter-independent)
      perc_hit_ema_ += ((cache_hit ? 1.0 : 0.0) - perc_hit_ema_) * (1.0 / 1024.0);   // hit-rate feature (relative-impact)
      perc_pc_path_ = (perc_pc_path_ << 5) ^ ((ip >> P.perc_pc_lobit) & 0x1Fu);       // roll this trigger PC into the PC-chain/path hash
      perc_last_trigger_ = block;                                                     // baseline for the next prefetch's PC^delta feature
      if (P.perc_profile) { perc_pc_ring_[2] = perc_pc_ring_[1]; perc_pc_ring_[1] = perc_pc_ring_[0]; perc_pc_ring_[0] = static_cast<uint32_t>(ip); } // raw last-3 trigger PCs
    }
    // Set-duel: a demand MISS in an OFF-leader set (no delta) argues delta would help (PSEL up); a miss in an
    // ON-leader set (has delta) argues delta is not helping / is displacing (PSEL down). Net-benefit incl. pollution.
    if (P.delta_pht_setduel && !cache_hit && (type == atype::LOAD || type == atype::RFO)) {
      int g = delta_sd_group(block);
      if (g == 0) delta_psel_ = std::min(delta_psel_ + 1, P.delta_pht_sd_max);
      else if (g == 1) delta_psel_ = std::max(delta_psel_ - 1, 0);
    }
    if (P.walk_setduel && !cache_hit && (type == atype::LOAD || type == atype::RFO)) {
      int g = delta_sd_group(block); // miss in OFF-leader (no walk) -> walk wanted; in ON-leader (has walk) -> not helping
      if (g == 0) walk_psel_ = std::min(walk_psel_ + 1, P.delta_pht_sd_max);
      else if (g == 1) walk_psel_ = std::max(walk_psel_ - 1, 0);
    }
    if (useful_prefetch) {
      modify_pattern_usefulness(block, true);
      increase_usefulness_counter();
      note_delta_block(block, /*additive=*/delta_additive, /*is_use=*/true);
      pf_sample_resolve(block, /*useful=*/true); // credit the ACTUAL trigger pattern (precise validation)
      if (P.enable_perceptron_filter) perc_resolve(block, P.perc_label_pe ? pf_value : 1.0); // dense training on PE (or usefulness=+1)
      if (P.region_evict_policy >= 6) { region_type* r = regions_.probe_key(region_of(block)); if (r && r->pf_used < 65535) ++r->pf_used; }
    }
    if (!cache_hit)
      add_to_llc_pagemap(block);
    if (P.online_learning && (type == atype::LOAD || !P.train_demand_only))
      learn_on_access(block, ip); // train on the PRIOR map, before this access is marked below
    if (type == atype::LOAD || !P.train_demand_only) {
      if (!cache_hit || !P.access_map_miss_only)
        add_to_pagemap(block, false, ip);
    }
    if (type == atype::LOAD || !P.prefetch_demand_only)
      do_prefetch(block, ip);
  }

  // Called on every L2 eviction. Mirrors the original prefetcher_cache_fill:
  // a not-yet-used evicted prefetch lowers pattern/global usefulness; otherwise
  // an optional scrape-on-evict harvests the region; the evicted block is always
  // removed from the prefetch-map filter.
  void on_l2_evict(uint64_t evicted_block, uint64_t cycle, bool was_unused_prefetch, double pf_value = 0.0)
  {
    cycle_ = cycle;
    dbg_exact_.erase(evicted_block); // DIAGNOSTIC: pure mirror — evict always removes (unlike the region-gated prefetch_map clear)
    if (P.enable_am_bloom) am_on_evict(offset_of(evicted_block)); // gradual freshness clearing
    if (was_unused_prefetch) {
      modify_pattern_usefulness(evicted_block, false);
      decrease_usefulness_counter();
      note_delta_block(evicted_block, /*additive=*/false, /*is_use=*/false); // died unused -> pollution
      pf_sample_resolve(evicted_block, /*useful=*/false); // the trigger pattern predicted a block that died unused
      if (P.enable_perceptron_filter) perc_resolve(evicted_block, P.perc_label_pe ? pf_value : -1.0); // dense training on PE (or usefulness=-1)
      if (P.region_evict_policy >= 6) { region_type* r = regions_.probe_key(region_of(evicted_block)); if (r && r->pf_dead < 65535) ++r->pf_dead; }
      if (P.pollution_filter)
        return; // keep the prefetch-map bit -> this proven-useless block won't be re-prefetched
    } else if (P.scrape_on_evict) {
      scrape_region(evicted_block);
    }
    remove_from_pagemap(evicted_block, true);
  }

  // Shadow cache: the prefetch map mirrors L2 residency. Set on ANY fill (any
  // source); cleared only on that block's eviction (on_l2_evict above). Both
  // prefetchers squash prefetches to a resident block. Only tracks blocks whose
  // region is still in the region table (else the bits are naturally lost).
  // ----- hybrid residency bloom (rolling-clock 1-bit; holds blocks spilled from evicted regions) -----
  bool bloom_on() const { return !resid_bloom_.empty(); }
  uint64_t bloom_h(uint64_t block, int j) const
  {
    uint64_t x = (block * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(j) * 0xD1B54A32D192ED03ull);
    x ^= x >> 29;
    return (x % P.resid_bloom_bits);
  }
  bool bloom_test(uint64_t block) const
  {
    for (int j = 0; j < P.resid_bloom_k; ++j) {
      uint64_t i = bloom_h(block, j);
      if (!((resid_bloom_[i >> 3] >> (i & 7)) & 1))
        return false;
    }
    return true;
  }
  // Spill a resident block into the bloom, first advancing the clock hand to clear stale bits (decay).
  void bloom_spill(uint64_t block)
  {
    for (int c = 0; c < P.resid_bloom_clear; ++c) {
      resid_bloom_[bloom_hand_ >> 3] &= ~(uint8_t{1} << (bloom_hand_ & 7));
      bloom_hand_ = (bloom_hand_ + 1) % P.resid_bloom_bits;
    }
    for (int j = 0; j < P.resid_bloom_k; ++j) {
      uint64_t i = bloom_h(block, j);
      resid_bloom_[i >> 3] |= (uint8_t{1} << (i & 7));
    }
  }

  void shadow_fill(uint64_t block)
  {
    dbg_exact_.insert(block); // DIAGNOSTIC: pure L2-residency mirror (fill inserts, evict erases)
    if (region_type* r = regions_.probe_key(region_of(block)))
      r->prefetch_map[offset_of(block)] = true;
    else if (bloom_on())
      bloom_spill(block); // region not tracked -> record residency in the bloom directly
  }
  bool exact_resident(uint64_t block) const { return dbg_exact_.count(block) != 0; }

  // DIAGNOSTIC: for a (temporal) miss, dump the region context and, for every trigger position that could
  // predict this offset (off-d, d=1..ps), whether the PHT entry exists (occ) and whether it predicts this
  // offset. Answers "why wasn't this block re-prefetched" -> cold PHT entry vs learned-but-doesn't-predict.
  std::string explain(uint64_t block)
  {
    uint64_t pn = region_of(block); int off = static_cast<int>(offset_of(block));
    region_type* r = regions_.find_key(pn);
    if (r == nullptr) return "  region EVICTED (context lost)\n";
    const region_type* rb = shadow_back(r), *rf = shadow_fwd(r);
    const int ps = static_cast<int>(P.pattern_size), bpr = static_cast<int>(blocks_per_region_);
    const uint64_t ctx = ctx_of(r);
    const uint64_t ctx_pc = ctx | (P.pattern_pc_bits ? (static_cast<uint64_t>(r->pc_pht) & mask(P.pattern_pc_bits)) << P.pattern_context_bits : 0);
    std::string am, pm;
    for (int o = 0; o < bpr; ++o) { am += r->access_map[o] ? '1' : '.'; pm += r->prefetch_map[o] ? '1' : '.'; }
    char buf[512]; std::string out;
    std::snprintf(buf, sizeof buf, "  off=%d pop=%d momentum=%d last_block=%d pc_pht=%d\n   access =[%s]\n   resid  =[%s]\n",
      off, static_cast<int>(std::count(r->access_map.begin(), r->access_map.end(), true)), r->momentum,
      static_cast<int>(r->last_block), static_cast<int>(r->pc_pht), am.c_str(), pm.c_str());
    out += buf;
    for (int d = 1; d <= ps; ++d) {
      int ot = off - d; if (ot < 0) break;
      auto pat = patterns_at(r, rb, rf, ot, false);
      auto pr = get_prefetch_pattern(pat.first, ctx_pc, 0, false, 0);
      pattern_type* e = pattern_tables_.at(0).find_key(pat_key(pat.first, ctx_pc));
      bool predicts = (pr.first >> (ps - d)) & 1;
      std::snprintf(buf, sizeof buf, "   FWD trig off=%d(d=%d) accessed=%d ap=0x%llx occ=%llu predbits=0x%llx predicts=%s\n",
        ot, d, r->access_map[ot] ? 1 : 0, (unsigned long long)pat.first, (unsigned long long)(e ? e->occurrences : 0),
        (unsigned long long)pr.first, predicts ? "YES" : "no");
      out += buf;
    }
    for (int d = 1; d <= ps; ++d) {
      int ot = off + d; if (ot >= bpr) break;
      auto pat = patterns_at(r, rb, rf, ot, true);
      auto pr = get_prefetch_pattern(pat.second, ctx_pc, 0, true, 0);
      auto& ntbl = P.separate_negative_tables ? negative_pattern_tables_ : pattern_tables_;
      pattern_type* e = ntbl.at(0).find_key(pat_key(pat.second, ctx_pc));
      bool predicts = (pr.first >> (ps - d)) & 1;
      std::snprintf(buf, sizeof buf, "   BWD trig off=%d(d=%d) accessed=%d neg=0x%llx occ=%llu predbits=0x%llx predicts=%s\n",
        ot, d, r->access_map[ot] ? 1 : 0, (unsigned long long)pat.second, (unsigned long long)(e ? e->occurrences : 0),
        (unsigned long long)pr.first, predicts ? "YES" : "no");
      out += buf;
    }
    return out;
  }
  // Residency: region in table -> exact prefetch_map, BUT a clear bit falls through to the bloom
  // (the block may be resident via a prior spill / a fill while the region was untracked, then the
  // region got re-inserted with a fresh empty map). Region absent -> bloom.
  bool shadow_resident(uint64_t block)
  {
    if (P.exact_shadow_test) return dbg_exact_.count(block) != 0; // leak-free fill/evict mirror
    region_type* r = regions_.probe_key(region_of(block));
    if (r != nullptr && r->prefetch_map[offset_of(block)])
      return true;
    return bloom_on() && bloom_test(block);
  }
  // 2 = resident (squash). region present+bit=2; else fall through to bloom (hit=2). region absent: bloom.
  int shadow_status(uint64_t block)
  {
    if (P.exact_shadow_test) return dbg_exact_.count(block) ? 2 : 0; // leak-free fill/evict mirror
    region_type* r = regions_.probe_key(region_of(block));
    if (r != nullptr && r->prefetch_map[offset_of(block)])
      return 2;
    if (bloom_on() && bloom_test(block))
      return 2;
    return (r != nullptr) ? 1 : 0;
  }

  // SHARED prefetch/residency filter for a co-resident prefetcher (the branch-graph instruction prefetcher).
  // The branch graph keeps its PRIVATE tables (edge BTB + vpage->ppage xlate) but has NO filter of its own:
  // it probes and updates THIS filter (SPPAM's region prefetch_map), so both prefetchers see one residency
  // view over the shared L2. filter_mark allocates the code-page region (the region table is thereby shared;
  // it sets ONLY the prefetch bit -- no access map / momentum) and eviction coherence flows through
  // on_l2_evict exactly as for SPPAM's own prefetches.
  bool filter_probe(uint64_t block) { return shadow_resident(block); }
  void filter_mark(uint64_t block) { add_to_pagemap(block, /*prefetch=*/true); }
  // PACKED code residency: SPPAM never predicts off the instruction stream, so a code region entry's access_map
  // is dead -- reuse it as a SECOND residency map. Key by the 4 KiB PAGE (two 2 KiB regions -> one contiguous
  // physical page, ASLR-safe) in a DISTINCT key space (CODE_KEY_BIT) so code + data never collide, and pack the
  // page's 64 blocks into prefetch_map[0..31] (offset 0-31) + access_map[0..31] (offset 32-63). This DOUBLES the
  // code residency capacity per region-table slot -> fewer code entries -> fewer evictions/lost residency (lower
  // redundancy) and less pressure on the data regions, at ZERO extra state. Per-block precise (no aliasing).
  static constexpr uint64_t CODE_KEY_BIT = uint64_t{1} << 50;
  uint64_t code_key(uint64_t block) const { return page_of(block) | CODE_KEY_BIT; }
  bool filter_probe_code(uint64_t block) {
    region_type* r = regions_.probe_key(code_key(block)); if (r == nullptr) return false;
    int o = static_cast<int>(block & 63); return o < 32 ? r->prefetch_map[o] : r->access_map[o - 32];
  }
  void filter_mark_code(uint64_t block) {
    region_type* r = regions_.find_key(code_key(block));
    if (r == nullptr) r = &regions_.insert(region_type{code_key(block), blocks_per_region_});
    int o = static_cast<int>(block & 63); if (o < 32) r->prefetch_map[o] = true; else r->access_map[o - 32] = true;
  }
  void filter_evict_code(uint64_t block) {
    region_type* r = regions_.probe_key(code_key(block)); if (r == nullptr) return;
    int o = static_cast<int>(block & 63); if (o < 32) r->prefetch_map[o] = false; else r->access_map[o - 32] = false;
  }
  // Set by the module each access from the EXISTING per-IP filter table (ip_trickle_div) -- the IP gate
  // reuses that sparse-IP signal instead of tracking any per-region/per-block IP state.
  void set_ip_gate(bool g) { cur_ip_gate_ = g; }
  // Analysis: is this block's region entry still resident in the region table (context/PHT-training retained)?
  bool region_resident(uint64_t block) { return regions_.probe_key(region_of(block)) != nullptr; }
  // Analysis: delta from the region's LAST accessed offset to this block's offset (self-reference test). -1000 if no region.
  int last_block_delta(uint64_t block) { region_type* r = regions_.probe_key(region_of(block)); return r ? (static_cast<int>(offset_of(block)) - static_cast<int>(r->last_block)) : -1000; }
  uint64_t staging_drops() const { return staging_drops_; }
  uint64_t am_regions_seeded() const { return am_regions_seeded_; }
  uint64_t am_bits_seeded() const { return am_bits_seeded_; }

  uint64_t prefetches_issued = 0;
  uint64_t total_lookaheads = 0;
  // Region-map residency: how often a demand access finds its page's region
  // still resident (context retained) vs absent (cold or thrashed out). A high
  // miss rate that falls as the region map grows => thrashing-limited context.
  uint64_t region_demand_accesses = 0;
  uint64_t region_demand_misses = 0;
  uint64_t region_evictions = 0;  // region-table capacity evictions (gate pressure-release metric)

  // Universal "can't learn" signal, shared by SPPAM and SPP (the region map is the
  // common substrate). Maps the young-evict EMA to a [0,1] throttle: 0 below lo,
  // ramping to 1 at hi. High => the region map is thrashing => both engines back off.
  double region_thrash() const
  {
    if (!P.enable_region_thrash_throttle)
      return 0.0;
    double t = (region_thrash_ema_ - P.region_thrash_lo) / (P.region_thrash_hi - P.region_thrash_lo);
    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
  }

private:
  // Region-table-entry granularity (keys the region map / access-map context).
  uint64_t region_of(uint64_t block) const { return block >> region_shift_; }
  uint64_t offset_of(uint64_t block) const { return block & (blocks_per_region_ - 1); }
  // 4 KB physical page granularity (the hard prefetch-squash boundary).
  uint64_t page_of(uint64_t block) const { return block >> page_shift_; }
  static uint64_t mask(uint32_t bits) { return bits >= 64 ? ~uint64_t{0} : ((uint64_t{1} << bits) - 1); }

  // Pack a region access-map (up to 64 blocks) into a uint64 for dedup analysis.
  static uint64_t pack_bitmap(const std::vector<bool>& m)
  {
    uint64_t b = 0;
    std::size_t n = m.size() < 64 ? m.size() : 64;
    for (std::size_t i = 0; i < n; ++i)
      if (m[i])
        b |= (uint64_t{1} << i);
    return b;
  }

  struct region_type {
    uint64_t vpn = 0, next_vpn = 0, prev_vpn = 0;
    bool has_next = false, has_prev = false;
    std::vector<bool> access_map;
    std::vector<bool> prefetch_map;
    uint64_t last_access_time = 0;
    int momentum = 0;
    uint64_t last_block = 0;
    uint64_t since_last_scrape = 0;
    uint32_t pat_ctx = 0; // per-region context folded into the pattern key
    uint8_t pc_pht = 0;   // PHT index of the PREVIOUS accessing PC in this page (stable per-region PC context)
    uint8_t entry_ip = 0; // trigger-PC hash that ALLOCATED this region (cold-start delta learning key)
    uint64_t xkey = 0;    // cross-page PHT key (predecessor map + entry offset) this region was entered with
    bool xkey_set = false;
    std::vector<uint8_t> block_ip; // per-block trigger-PC hash (perceptron PC input; berti sets 0)
    std::vector<bool> delta_map;   // blocks THIS region got from the delta-PHT (usefulness attribution for its self-throttle)
    uint16_t pf_used = 0, pf_dead = 0; // this region's prefetches that were demand-used vs evicted-unused (eviction policy)
    uint16_t dmiss = 0;            // demand misses seen in this region (eviction policy: evict low-value regions)
    region_type() = default;
    region_type(uint64_t vpn_, uint64_t bpr) : vpn(vpn_), access_map(bpr, false), prefetch_map(bpr, false), block_ip(bpr, 0) {}
  };
  // Region set projection: identity by default, or a bit-mixing hash of the vpn
  // (toggled by region_hash) to spread structured page strides across sets. The
  // tag projection stays the plain vpn for matching.
  struct region_set_idx {
    bool hash = false;
    uint64_t page_shift = 0; // page_bits - region_bits: fold a page's sub-regions to one set
    static uint64_t mix(uint64_t x) { x *= 0x9E3779B97F4A7C15ULL; x ^= x >> 32; return x; }
    uint64_t idx(uint64_t vpn) const { uint64_t p = vpn >> page_shift; return hash ? mix(p) : p; }
    uint64_t operator()(const region_type& r) const { return idx(r.vpn); }
    uint64_t operator()(uint64_t vpn) const { return idx(vpn); }
  };
  struct region_tag_idx {
    uint64_t operator()(const region_type& r) const { return r.vpn; }
  };

  struct pattern_type {
    uint64_t pattern = 0;
    uint64_t occurrences = 0, useful = 0, useless = 0;
    int64_t usefulness = 8;
    conf_table prediction_table;
    std::vector<uint64_t> prediction_counter;    // per-block bias (== counter mode when pp_hist_bits==0)
    std::vector<int16_t> pweights;               // per-block x pp_hist_bits history weights (perceptron mode)
    const params* P = nullptr;
    pattern_type() : prediction_table(1, 16) {}
    pattern_type(uint64_t pat, const params* p)
      : pattern(pat), prediction_table(p->pattern_conf_sets, p->pattern_conf_ways), prediction_counter(p->pattern_size, 0),
        pweights(p->pattern_perceptron ? static_cast<std::size_t>(p->pattern_size) * static_cast<std::size_t>(p->pp_bits()) : 0, 0),
        P(p)
    {
    }

    // Perceptron score for block o: bias (the counter) + sum_i (+/-1 history input) * weight.
    long pp_score(int o, uint64_t hist) const
    {
      long s = static_cast<long>(prediction_counter[o]);
      const int H = P->pp_bits();
      const int16_t* w = &pweights[static_cast<std::size_t>(o) * H];
      for (int i = 0; i < H; ++i)
        s += ((hist >> i) & 1) ? w[i] : -w[i];
      return s;
    }
    std::pair<uint64_t, bool> get_prediction_pp(uint64_t hist)
    {
      uint64_t pred = 0;
      for (std::size_t i = 0; i < prediction_counter.size(); ++i)
        if (pp_score(static_cast<int>(i), hist) >= static_cast<long>(P->min_confidence_to_prefetch))
          pred |= (uint64_t{1} << i);
      return {pred, true};
    }
    // Perceptron update for block o: dir=+1 confirmed positive, -1 contrastive negative. Bias moves
    // like the counter; each history weight moves toward agreement with its +/-1 input, clamped.
    void pp_update(int o, uint64_t hist, int dir)
    {
      if (dir > 0)
        prediction_counter[o] = std::min<uint64_t>(prediction_counter[o] + P->counter_up, 100);
      else
        prediction_counter[o] = prediction_counter[o] > P->counter_down ? prediction_counter[o] - P->counter_down : 0;
      const int H = P->pp_bits();
      const long cap = P->pp_weight_cap, step = static_cast<long>(P->counter_up) * dir;
      int16_t* w = &pweights[static_cast<std::size_t>(o) * H];
      for (int i = 0; i < H; ++i) {
        long in = ((hist >> i) & 1) ? 1 : -1;
        long v = w[i] + step * in;
        w[i] = static_cast<int16_t>(v > cap ? cap : (v < -cap ? -cap : v));
      }
    }

    std::pair<uint64_t, bool> get_prediction()
    {
      if (P && P->table_or_counter) {
        uint64_t pred = 0;
        for (std::size_t i = 0; i < prediction_counter.size(); ++i)
          if (prediction_counter[i] >= P->min_confidence_to_prefetch)
            pred |= (uint64_t{1} << i);
        return {pred, true};
      }
      auto pred = prediction_table.get_highest_conf(P ? P->min_confidence_to_prefetch : 50);
      if (pred.has_value())
        return {pred.value(), true};
      return {0, false};
    }
    void increment_prediction(uint64_t prediction)
    {
      if (P && P->table_or_counter) {
        for (std::size_t i = 0; i < prediction_counter.size(); ++i) {
          if (prediction & (uint64_t{1} << i))
            prediction_counter[i] = std::min<uint64_t>(prediction_counter[i] + P->counter_up, 100);
          else
            prediction_counter[i] = prediction_counter[i] > P->counter_down ? prediction_counter[i] - P->counter_down : 0;
        }
      } else if (!prediction_table.incr_conf(prediction).has_value())
        prediction_table.fill(prediction);
    }
  };
  struct pattern_idx {
    uint64_t operator()(const pattern_type& p) const { return p.pattern; }
    uint64_t operator()(uint64_t k) const { return k; }
  };
  struct cpt_type {
    uint64_t stream_id = 0;
  };
  struct cpt_idx {
    uint64_t operator()(const cpt_type& c) const { return c.stream_id; }
    uint64_t operator()(uint64_t k) const { return k; }
  };

  using region_table = lru_table<region_type, region_set_idx, region_tag_idx>;
  // Two-level region table (drop-in for region_table): new regions insert into a small STAGING table and observe
  // there (they still predict via find_key/probe_key, which check both levels); on staging eviction only good-shape
  // regions are PROMOTED into the large table. `staging=false` => behaves exactly as a single large table.
  struct staged_region_table {
    region_table main_;
    region_table stage_;
    bool staging = false;
    std::function<bool(const region_type&)> promote_ok;
    staged_region_table(std::size_t msets, std::size_t mways, std::size_t ssets, std::size_t sways,
                        region_set_idx sp, region_tag_idx tp)
        : main_(msets, mways, sp, tp), stage_(ssets, sways, sp, tp)
    {
      stage_.on_evict = [this](const region_type& r) { if (staging && (!promote_ok || promote_ok(r))) main_.insert(r); };
    }
    region_type* find_key(uint64_t k)  { if (auto* m = main_.find_key(k)) return m;  return staging ? stage_.find_key(k) : nullptr; }
    region_type* probe_key(uint64_t k) { if (auto* m = main_.probe_key(k)) return m; return staging ? stage_.probe_key(k) : nullptr; }
    region_type& insert(const region_type& r) { return staging ? stage_.insert(r) : main_.insert(r); }
    bool invalidate(const region_type& r) { bool a = main_.invalidate(r); if (staging) a = stage_.invalidate(r) || a; return a; }
  };
  using pattern_table = lru_table<pattern_type, pattern_idx, pattern_idx>;

  // ----- shadow bit access (no allocation) -----
  // Shadow layout: [pattern_size backward][blocks_per_region region][pattern_size forward]
  // `spec` (optional) overlays the current region's map with the walk's speculative path so a walk step's
  // signature reflects the offsets we've already committed this trigger (same idea as the delta-PHT overlay).
  int shadow_bit(const region_type* r, const region_type* rb, const region_type* rf, int idx, const uint8_t* spec = nullptr) const
  {
    const int ps = static_cast<int>(P.pattern_size);
    const int bpr = static_cast<int>(blocks_per_region_);
    if (idx < ps)
      return rb ? (rb->access_map[bpr - ps + idx] ? 1 : 0) : 0;
    if (idx < ps + bpr)
      return (r->access_map[idx - ps] || (spec && spec[idx - ps])) ? 1 : 0;
    if (idx < 2 * ps + bpr)
      return rf ? (rf->access_map[idx - ps - bpr] ? 1 : 0) : 0;
    return 0;
  }
  int shadow_size() const { return 2 * static_cast<int>(P.pattern_size) + static_cast<int>(blocks_per_region_); }
  // Two region vpns lie in the same 4 KB page iff they share the high bits above
  // the sub-page index.
  bool same_page(uint64_t ra, uint64_t rb) const { return (ra >> page_region_shift_) == (rb >> page_region_shift_); }
  // Shadow = the adjacent sub-page region in the same page, computed from the
  // address (no stored next/prev links). When region==page, there is no neighbor.
  const region_type* shadow_back(const region_type* r)
  {
    if (!P.within_page_shadow || r->vpn == 0 || !same_page(r->vpn - 1, r->vpn))
      return nullptr;
    return regions_.probe_key(r->vpn - 1);
  }
  const region_type* shadow_fwd(const region_type* r)
  {
    if (!P.within_page_shadow || !same_page(r->vpn + 1, r->vpn))
      return nullptr;
    return regions_.probe_key(r->vpn + 1);
  }

  // Roll the page-history hash when crossing into a new 4 KB page.
  void advance_page_hist(uint64_t block)
  {
    uint64_t pg = block >> page_shift_;
    if (pg != last_page_seen_) {
      if (last_page_seen_ != ~uint64_t{0})
        page_hist_ = (page_hist_ << 4) ^ (last_page_seen_ & 0xFFu);
      last_page_seen_ = pg;
    }
  }

  // ----- region / page maps -----
  static uint8_t ip_hash(uint64_t ip) { return ip ? static_cast<uint8_t>((ip * 0x9E3779B97F4A7C15ull) >> 56) : 0; }
  // ---- Access-map bloom: shared backing store, one filter per within-region offset ----
  static uint64_t am_hash(uint64_t region, uint64_t off, int i)
  {
    uint64_t x = (region ^ (off * 0x9E3779B1u)) ^ (static_cast<uint64_t>(i + 1) * 0xC2B2AE3D27D4EB4Full);
    x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 32;
    return x;
  }
  void am_insert(uint64_t region, uint64_t off)
  {
    const std::size_t base = off * static_cast<std::size_t>(am_S_);
    for (int i = 0; i < am_k_; ++i)
      am_bloom_[base + am_hash(region, off, i) % am_S_] = 1;
  }
  bool am_get(uint64_t region, uint64_t off) const
  {
    const std::size_t base = off * static_cast<std::size_t>(am_S_);
    for (int i = 0; i < am_k_; ++i)
      if (!am_bloom_[base + am_hash(region, off, i) % am_S_])
        return false;
    return true;
  }
  // Warm-seed a freshly-allocated region's access map from the bloom (retained pattern).
  void am_seed(uint64_t region, std::vector<bool>& map)
  {
    uint64_t seeded = 0;
    for (uint64_t o = 0; o < blocks_per_region_; ++o)
      if (am_get(region, o)) { map[o] = true; ++seeded; }
    am_regions_seeded_ += (seeded > 0); am_bits_seeded_ += seeded;
  }
  // Gradual clearing: a cache eviction mapping to offset `off`; clear a slice once it exceeds thresh.
  void am_on_evict(uint64_t off)
  {
    if (++am_evct_[off] >= static_cast<uint32_t>(am_T_)) {
      am_evct_[off] = 0;
      const std::size_t base = off * static_cast<std::size_t>(am_S_);
      const int slice = am_S_ / (am_clear_frac_ > 0 ? am_clear_frac_ : 1);
      for (int c = 0; c < slice; ++c) { am_bloom_[base + am_hand_[off]] = 0; am_hand_[off] = (am_hand_[off] + 1) % am_S_; }
    }
  }

  // Cross-page PHT row: per-offset access counts of a region + how many times its entry key occurred.
  struct xrow { std::vector<uint16_t> cnt; uint32_t occ = 0; };

  void add_to_pagemap(uint64_t block, bool prefetch, uint64_t ip = 0)
  {
    uint64_t pn = region_of(block);
    uint64_t off = offset_of(block);
    region_type* r = regions_.find_key(pn);
    if (!prefetch) {
      region_demand_accesses++;
      if (r == nullptr) {
        region_demand_misses++;
        note_region_miss(pn); // classify cold vs re-reference (thrash) for the throttle
      }
      if (P.pattern_context_src == 1)
        advance_page_hist(block);
    }
    if (r != nullptr) {
      if (P.scrape_on_idle && (r->last_access_time + P.scrape_idle_time < cycle_)) {
        if (r->since_last_scrape >= P.scrape_min_count)
          scrape_region(block);
        r->last_access_time = cycle_;
      } else if (P.scrape_on_count && r->since_last_scrape >= P.scrape_access_count) {
        scrape_region(block);
      }
      if (prefetch) {
        r->prefetch_map[off] = true;
      } else {
        if (P.pattern_context_src == 2) {
          int d = static_cast<int>(off) - static_cast<int>(r->last_block);
          r->pat_ctx = static_cast<uint32_t>((uint64_t(r->pat_ctx) << 3) ^ (static_cast<uint64_t>(d) & 0x7Fu));
        }
        r->access_map[off] = true;
        r->prefetch_map[off] = true;
        if (P.enable_am_bloom) am_insert(pn, off);
        uint8_t iph = ip_hash(ip);
        r->block_ip[off] = iph;
        r->pc_pht = iph; // update the per-page accessing-PC context (learn_on_access already read the prior value)
        if (r->last_block < off) {
          r->momentum = r->momentum < 7 ? r->momentum + 1 : 7;
          if ((P.ip_direction || P.neg_dir_pc) && ip_dir_[iph] < 63) ++ip_dir_[iph];   // per-IP direction: forward stride
        } else {
          r->momentum = r->momentum > -8 ? r->momentum - 1 : -8;
          if ((P.ip_direction || P.neg_dir_pc) && off < r->last_block && ip_dir_[iph] > -63) --ip_dir_[iph]; // backward stride
        }
        // cold-start learning: the FIRST delta after a region's entry (2nd access), keyed by the ENTRY PC.
        if (P.enable_cold_start && P.cold_start_pc && r->since_last_scrape == 1)
          cs_train(r->entry_ip, static_cast<int>(off) - static_cast<int>(r->last_block));
        // cross-page: accumulate this region's footprint under the key it was ENTERED with (train the value).
        if (P.enable_cross_page) {
          if (r->xkey_set) { xrow& xr = xrow_at(r->xkey); if (off < xr.cnt.size()) ++xr.cnt[off]; }
          last_region_ = pn;
        }
        r->last_block = off;
        r->since_last_scrape += 1;
      }
    } else {
      // STAGING GATE (demand allocations only): keep sparse pages out of the region table so they don't
      // capacity-evict the dense set. Spatial gate = not-yet-dense; IP gate = allocating IP is known-sparse.
      if (!prefetch && (P.enable_region_staging || P.enable_ip_gate) && gate_should_apply()) {
        bool ip_block = P.enable_ip_gate && cur_ip_gate_;
        if (ip_block && P.ip_gate_explore_div && ++ip_gate_explore_ctr_ % P.ip_gate_explore_div == 0)
          ip_block = false; // EXPLORE: admit so this IP keeps generating prefetches -> stays sampled -> can un-gate
        bool gate_block = (P.enable_region_staging && !staging_admit(pn)) || ip_block;
        if (gate_block) { ++staging_drops_; return; }
      }
      region_type& nr = regions_.insert(region_type{pn, blocks_per_region_});
      if (P.pattern_context_src == 1)
        nr.pat_ctx = static_cast<uint32_t>(page_hist_); // capture the path that led to this page
      // WARM-SEED the freshly-allocated region's access map from the bloom (retained pattern for a
      // thrashed-out region), on demand allocations only, before recording this access.
      if (P.enable_am_bloom && !prefetch) am_seed(pn, nr.access_map);
      if (prefetch) {
        nr.prefetch_map[off] = true;
      } else {
        nr.access_map[off] = true;
        nr.prefetch_map[off] = true;
        if (P.enable_am_bloom) am_insert(pn, off);
        nr.block_ip[off] = ip_hash(ip);
        nr.pc_pht = ip_hash(ip); // first access to this page seeds its PC context
        nr.entry_ip = ip_hash(ip); // cold-start: remember the allocating PC to learn its startup delta
        nr.last_block = off;
        nr.since_last_scrape += 1;
        region_just_created_ = true; // cold-start: this demand allocated a fresh region -> kick a startup stream
        // cross-page: key this fresh region by its PREDECESSOR region's map + entry offset; register + train entry.
        if (P.enable_cross_page) {
          nr.xkey = xkey_of(last_region_, off, ip); nr.xkey_set = true;
          xrow& xr = xrow_at(nr.xkey); ++xr.occ; if (off < xr.cnt.size()) ++xr.cnt[off];
          last_region_ = pn;
        }
      }
      nr.last_access_time = cycle_;
    }
  }

  // Mark a prefetch in an already-resolved region (same page as the trigger).
  // Mirrors the found-region prefetch path of add_to_pagemap, including its
  // (occasional) scrape trigger, without re-finding the region.
  void mark_prefetch(region_type* r, uint64_t off, uint64_t any_block_in_region)
  {
    if (P.scrape_on_idle && (r->last_access_time + P.scrape_idle_time < cycle_)) {
      if (r->since_last_scrape >= P.scrape_min_count)
        scrape_region(any_block_in_region);
      r->last_access_time = cycle_;
    } else if (P.scrape_on_count && r->since_last_scrape >= P.scrape_access_count) {
      scrape_region(any_block_in_region);
    }
    r->prefetch_map[off] = true;
  }

  bool check_pagemap(uint64_t block, bool prefetch)
  {
    region_type* r = regions_.find_key(region_of(block));
    if (r == nullptr)
      return false;
    return prefetch ? r->prefetch_map[offset_of(block)] : r->access_map[offset_of(block)];
  }
  void remove_from_pagemap(uint64_t block, bool prefetch)
  {
    region_type* r = regions_.find_key(region_of(block));
    if (r != nullptr) {
      if (prefetch) r->prefetch_map[offset_of(block)] = false;
      else r->access_map[offset_of(block)] = false;
      r->last_access_time = cycle_;
    }
  }
  void add_to_llc_pagemap(uint64_t block)
  {
    region_type* e = llc_regions_.find_key(region_of(block));
    if (e != nullptr)
      e->access_map[offset_of(block)] = true;
    else
      llc_regions_.insert(region_type{region_of(block), blocks_per_region_}).access_map[offset_of(block)] = true;
  }
  bool check_llc_pagemap(uint64_t block)
  {
    region_type* e = llc_regions_.find_key(region_of(block));
    return e != nullptr && e->access_map[offset_of(block)];
  }

  // ----- pattern extraction & tables -----
  // Core: extract (forward, backward) patterns from already-resolved region
  // pointers. `want_neg=false` skips the backward pattern.
  std::pair<uint64_t, uint64_t> patterns_at(const region_type* r, const region_type* rb, const region_type* rf, int64_t offset, bool want_neg, const uint8_t* spec = nullptr) const
  {
    if (r == nullptr)
      return {0, 0};
    uint64_t pos = 0, neg = 0;
    const int ps = static_cast<int>(P.pattern_size);
    const int ssz = shadow_size();
    for (int i = static_cast<int>(offset) + 1; i <= static_cast<int>(offset) + ps; ++i)
      pos = (pos << 1) | (i >= 0 && i < ssz ? uint64_t(shadow_bit(r, rb, rf, i, spec)) : 0);
    if (want_neg)
      for (int i = static_cast<int>(offset) + 2 * ps - 1; i >= static_cast<int>(offset) + ps; --i)
        neg = (neg << 1) | (i >= 0 && i < ssz ? uint64_t(shadow_bit(r, rb, rf, i, spec)) : 0);
    return {pos, neg};
  }

  // Convenience: resolve region + shadows, then extract. (Used off the hot path.)
  std::pair<uint64_t, uint64_t> get_patterns(uint64_t pn, int64_t offset)
  {
    region_type* r = regions_.find_key(pn);
    if (r == nullptr)
      return {0, 0};
    return patterns_at(r, shadow_back(r), shadow_fwd(r), offset, true);
  }

  // Per-region context, masked to the configured width (0 when disabled).
  uint64_t ctx_of(const region_type* r) const
  {
    return (P.pattern_context_bits && r != nullptr) ? (uint64_t(r->pat_ctx) & mask(P.pattern_context_bits)) : 0;
  }
  // Pattern key = spatial pattern in the low bits, context concatenated above.
  uint64_t pat_key(uint64_t pattern, uint64_t ctx) const
  {
    return (pattern & mask(P.pattern_size)) | (ctx << P.pattern_size);
  }

  std::pair<uint64_t, bool> get_prefetch_pattern(uint64_t access_pattern, uint64_t ctx, int order, bool negative, uint64_t hist = 0)
  {
    auto& tbl = (negative && P.separate_negative_tables) ? negative_pattern_tables_.at(order) : pattern_tables_.at(order);
    pattern_type* e = tbl.find_key(pat_key(access_pattern, ctx));
    if (e != nullptr) {
      e->occurrences++;
      e->P = &P;
      return (P.pattern_perceptron && P.pp_bits() > 0) ? e->get_prediction_pp(hist) : e->get_prediction();
    }
    return {0, true};
  }

  void increment_access_pattern(uint64_t pattern, uint64_t prediction, uint64_t ctx, int order, bool negative)
  {
    if (pattern == 0)
      return;
    auto& tbl = (negative && P.separate_negative_tables) ? negative_pattern_tables_.at(order) : pattern_tables_.at(order);
    uint64_t key = pat_key(pattern, ctx);
    pattern_type* e = tbl.find_key(key);
    if (e != nullptr) {
      e->P = &P;
      e->increment_prediction(prediction);
    } else {
      pattern_type& ne = tbl.insert(pattern_type{key, &P});
      ne.P = &P;
      ne.increment_prediction(prediction);
      ne.usefulness = global_usefulness_index_;
    }
  }

  // ---- Delta-PHT: spatial-context -> next-access signed delta (SPP-style single dominant delta) ----
  struct dpht_entry { uint32_t tag = 0; int8_t delta = 0; uint8_t conf = 0; };
  std::vector<dpht_entry> dpht_;
  std::vector<uint8_t> dpht_spec_; // speculative lookahead overlay: blocks predicted down the current path
  uint64_t dbg_dpht_issued_ = 0, dbg_dpht_train_ = 0;
  // Walk-accumulate scratch (per do_prefetch call): speculative path overlay, per-offset accumulated confidence,
  // and an already-issued mask so a corroborated offset is prefetched at most once.
  std::vector<uint8_t> walk_spec_, walk_issued_;
  std::vector<uint64_t> walk_acc_;
  uint64_t dbg_walk_issued_ = 0, dbg_walk_steps_ = 0;
  // ---- IP sampling table (faithful port of the shipping pfht_ / ip_filter accuracy feedback) ----
  // Sample 1/N ISSUED prefetches into a block->{trigger IP, trigger pattern} table; on resolve (demand-use or
  // evicted-unused) credit that IP's ip_useful_/ip_useless_ (and, for the fall-through, that pattern's pat_val_).
  // ip_trickle_div then throttles a trigger IP whose sampled usefulness is low -- sppam's real accuracy loop.
  uint32_t ip_n_ = 4096, ip_mask_ = 0xFFFu; // per-IP throttle-table depth + index mask (set from P.ip_table_entries)
  struct pf_samp_t { uint64_t pat = 0; uint16_t iph = 0; int8_t bit = -1; bool bwd = false; // bit = prediction_counter index (depth-0 e2e), -1 else; bwd = backward-scan prefetch
                     uint32_t tag = 0; uint32_t stamp = 0; bool occ = false; // tag/stamp/occ used only by the direct-mapped policy
                     uint16_t pfeat[6] = {0,0,0,0,0,0}; bool hasf = false; }; // perceptron feature indices captured at issue (prototype)
  std::unordered_map<uint64_t, pf_samp_t> pf_sample_;    // legacy clear-on-full map (pv_sample_directmap=false)
  std::vector<pf_samp_t> pf_sdm_;                        // fixed direct-mapped table w/ probabilistic eviction (pv_sample_directmap=true)
  uint32_t pf_sdm_mask_ = 0;
  static uint64_t sdm_idx(uint64_t b) { return (b * 0x9E3779B97F4A7C15ull) >> 24; }
  uint32_t pv_div_cur_ = 4, pv_win_place_ = 0, pv_win_churn_ = 0; // adaptive sample-rate controller

  // --- Perceptron prefetch filter (PROTOTYPE): one learned gate over all throttle signals ---
  // 0 PC,1 off(dropped),2 eng,3 dep,4 use,5 tim,6 conf,7 sig, AGGREGATE-HARM: 8 mshr,9 g-useless,10 g-untimely,
  // 11 hit-rate (churn/relative-impact context), 12 CONJUNCTION of {use,tim,mshr,hit} (path-based: the gate's value
  // is a non-additive function of these -- "high pressure" means keep on a useful IP but drop on a polluting one --
  // which a sum-of-independent-weights hashed perceptron cannot represent (the XOR limitation; cf. Jimenez path-based).
  // 13 PC-PATH (hash of the recent trigger-PC chain -- datacenter PCs recur strongly; PPF/path-based use PC history),
  // 14 PC XOR DELTA (an XOR-combined feature a la PPF -- captures PC<->data-address correlation, strong on datacenter).
  static constexpr int PERC_NF = 22; // 19 + the 3-way PE split (I_UPF, I_LAT, I_POLL as separate per-IP sampled features)
  static constexpr int PERC_ENGINES = 4; // 0=SPPAM fwd, 1=SPPAM bwd, 2=delta/SPP, 3=branch-graph (declared early: profiler members below use it)
  static constexpr int PERC_CONJ = 256; // combined-feature table depth (use2|tim2|mshr2|hit2, 2 bits each)
  std::vector<int16_t> pw_pc_, pw_off_, pw_eng_, pw_dep_, pw_use_, pw_tim_, pw_conf_, pw_sig_,
                       pw_mshr_, pw_guse_, pw_gtim_, pw_hit_, pw_conj_, pw_pcpath_, pw_pcdelta_, // + PC-path + PC^delta tables
                       pw_usemshr_, pw_timmshr_, pw_confmshr_, pw_crit_, // DSE-derived pairwise interactions (contention-gated + criticality)
                       pw_upf_, pw_lat_, pw_poll_; // 3-way PE split: separate per-IP latency-saved / bandwidth-cost / pollution weight tables (16 buckets each)
  // interaction table sizes (direct product index): use(8)xmshr(16), tim(8)xmshr(16), conf(16)xmshr(16), use(8)xhit(16)
  static constexpr int PW_USEMSHR = 128, PW_TIMMSHR = 128, PW_CONFMSHR = 256, PW_CRIT = 128;
  uint32_t pw_pcpath_mask_ = 0, pw_pcdelta_mask_ = 0;
  uint64_t perc_pc_path_ = 0;      // rolling hash of recent trigger PCs (the PC chain / path); updated per demand
  uint64_t perc_last_trigger_ = 0; // last demand block, so a prefetch's PC^delta feature can use (target - trigger)
  // FEATURE-SIGNAL PROFILER (perc_profile): per feature -> per bucket -> PE-outcome-bucket counts, GLOBAL and
  // Emits one sampled line per resolved prefetch: raw signals (pc, sig, delta, last-N trigger PCs) + context buckets
  // + the per-prefetch PE. All feature/XOR/path-depth/dropout-slice sweeps run OFFLINE against these tuples, with the
  // AGGREGATE metric PE x coverage (sum PE per candidate bucket), not per-prefetch PE. Sampled to bound stderr volume.
  uint64_t perc_prof_ctr_ = 0;
  uint32_t perc_pc_ring_[3] = {0, 0, 0}; // raw last-3 trigger PCs (path/sequence experiments)
  uint32_t perc_last_rpc_ = 0, perc_last_rsig_ = 0, perc_last_rdelta_ = 0, perc_last_rp1_ = 0, perc_last_rp2_ = 0;
  uint64_t perc_raw_ap_ = 0, perc_raw_nxip_ = 0; // extraneous-feature raw stash: SPPAM spatial bitmap (engines 0/1), BG next-pc (engine 3)
  uint32_t perc_last_rap_ = 0, perc_last_rnxip_ = 0; // snapshotted at perc_keep, copied into the perc_ent at note_issue
public:
  void perc_set_raw_bg(uint64_t nxip) { if (P.perc_profile) perc_raw_nxip_ = nxip; } // glue hands the BG next-pc block in before perc_keep
private:
  std::vector<int16_t> pip_bias_; // PER-IP bias (mask bit 0x2000): each trigger-IP's own learned keep/drop offset -- the
  // original Jimenez perceptron's per-BRANCH bias weight w0, applied per-IP. Breaks the global weight-sharing so mcf's
  // IPs can all drift "keep" while xalan's drift "drop" (the piecewise/per-context personalization a shared linear sum can't).
  uint32_t pw_pc_mask_ = 0, pw_sig_mask_ = 0, perc_lfsr_ = 0x2545F491;
  double pe_ema_ = 30.0; // rolling mean of |PE| (perc_pe_norm): normalizes the reward -> scale-invariant, lower variance
  // Aggregate-harm signals -- per-prefetch PE is blind to COLLECTIVE pollution/bandwidth harm (a prefetch can look
  // locally useful yet still net-pollute). MSHR/bandwidth pressure (set by the glue each access) + global useless,
  // untimely, and demand-hit RATE EMAs give the gate the system-level context PE can't see.
  int perc_mshr_idx_ = 0;                          // 0..15 MSHR-occupancy/bandwidth pressure, refreshed by the glue
  int perc_instr_pct_ = 0;                          // instruction-pf fraction (%) of (instr+data) prefetch issues, refreshed by the glue (datacenter discriminator)
  uint64_t perc_rc_ = 0;                             // REAL cycle, refreshed by the glue each operate/fill -> stamped into perc_ent.issue_rc for a self-measured fill latency
  double perc_useless_ema_ = 0.0;                  // fraction of resolved prefetches that died unused (truly-bad)
  double perc_untimely_ema_ = 0.0;                 // fraction of unused-evicted prefetches later re-demanded (right addr, too early)
  double perc_hit_ema_ = 0.5;                      // global demand L2 hit-rate (relative-impact: churn hurts a HIGH-hit cache far more)
  uint16_t perc_last_feat_[PERC_NF] = {0}; uint32_t perc_last_iph_ = 0; bool perc_last_valid_ = false; // features + trigger-IP hash from the last perc_keep
  uint64_t dbg_perc_keep_ = 0, dbg_perc_drop_ = 0, dbg_perc_explore_ = 0, dbg_perc_train_ = 0, dbg_perc_veto_ = 0;
  // TRAINING FUNNEL (per-engine, split by label sign) -- verifies both positive (useful) AND negative (useless)
  // sources are actually reaching the perceptron, and how many resolved samples the margin/confidence filters discard.
  uint64_t dbg_res_pos_[PERC_ENGINES] = {0}, dbg_res_neg_[PERC_ENGINES] = {0}; // resolved samples by engine x sign (pre-filter)
  uint64_t dbg_tr_pos_[PERC_ENGINES] = {0}, dbg_tr_neg_[PERC_ENGINES] = {0};   // actually-trained (updated a weight) by engine x sign
  uint64_t dbg_skip_margin_ = 0, dbg_skip_conf_ = 0;                           // discarded: |PE|<margin / confident-correct
  // PE-vs-IPC MISALIGNMENT probe: a prefetch that was actually DEMAND-USED but whose net PE (latency saved - I_LAT
  // bandwidth cost - I_POLL) came out NEGATIVE -> the perceptron gets taught to DROP an IPC-beneficial prefetch.
  // High used_pe_neg fraction on a bandwidth-bound trace = DSE lesson #3 (PE label wrong), NOT a sampling shortfall.
  uint64_t dbg_used_pe_pos_ = 0, dbg_used_pe_neg_ = 0;
  uint64_t dbg_perc_igate_ = 0; // instruction-activity gate force-keeps (datacenter back-off)
  uint64_t dbg_perc_pegate_ = 0; // PE-gate force-keeps (good-PE IP -> don't filter)
  // FEATURE AUDIT: per-feature value histogram (0..15 clamp) sampled at gate + RAW PE-component ranges in CYCLES, to
  // verify every input is alive/sensible and that PE actually reflects latency-saved (DRAM vs LLC), not garbage.
  uint64_t dbg_fh_[PERC_NF][16] = {{0}}; uint64_t dbg_fh_n_ = 0; uint32_t dbg_audit_ctr_ = 0;
  double dbg_upf_sum_ = 0, dbg_lat_sum_ = 0, dbg_poll_sum_ = 0; uint64_t dbg_upf_n_ = 0, dbg_poll_n_ = 0;
  float dbg_upf_max_ = 0, dbg_lat_max_ = 0, dbg_poll_max_ = 0;
  uint64_t dbg_keep_res_ = 0, dbg_keep_res_u_ = 0, dbg_drop_res_ = 0, dbg_drop_res_u_ = 0; // discrimination: resolved useful-rate of kept vs (explored-)dropped
  bool perc_last_explored_ = false;
  struct perc_ent { std::array<uint16_t, PERC_NF> f{}; uint32_t tag = 0; uint32_t stamp = 0; uint32_t iph = 0; bool occ = false; bool explored = false;
    uint32_t issue_rc = 0; // REAL issue cycle -> perc_track_ measures its OWN fill latency (DSE-faithful PE), NOT via the flaky pfht_ bridge
    uint32_t lat = 0; float cost = 0.0f; bool filled = false; // fill latency + accrued I_LAT/I_POLL cost -- the perceptron PE MAGNITUDE, folded in here so the glue no longer needs a duplicate perc_sdm_ table (UNIFIED with the feature snapshot)
    uint32_t r_pc = 0, r_sig = 0, r_delta = 0, r_p1 = 0, r_p2 = 0,   // raw signals for the offline feature/encoding/depth sweep (perc_profile)
             r_block = 0, r_ap = 0, r_nxip = 0; }; // "extraneous" methodology-check signals: page(block), opposite-dir spatial pattern(ap), BG next-pc(nxip)
  // DENSE useful/useless training table: gate feature snapshot per ISSUED prefetch, keyed by block, trained ±1 at its
  // hit (useful) or unused-eviction (useless). No latency, no sampling, overwrite-on-collision (each prefetch resolves
  // once). Separate from perc_track_ (which carries PE latency/cost for the legacy sparse path).
  struct dense_ent { std::array<uint16_t, PERC_NF> f{}; uint32_t tag = 0; uint32_t iph = 0; bool occ = false; bool explored = false; };
  std::vector<dense_ent> perc_dense_; uint32_t perc_dense_mask_ = 0;
  std::vector<dense_ent> perc_victim_; uint32_t perc_victim_mask_ = 0; uint32_t perc_victim_ctr_ = 0; // PPF-style reject table: dropped prefetches, re-trained toward KEEP if re-demanded
  std::vector<float> ip_pe_sum_, ip_lat_sum_, ip_poll_sum_; std::vector<uint32_t> ip_pe_cnt_; // per-IP sampled PE COMPONENTS: I_UPF(=ip_pe_sum_ latency saved), I_LAT(bandwidth), I_POLL(pollution). Gate uses net = upf-lat-poll; the 3 also feed separate features.
  std::vector<perc_ent> perc_track_; uint32_t perc_track_mask_ = 0; // SMALL fixed direct-mapped table w/ probabilistic
  // eviction -- the SAME validated policy as pf_sdm_ (large per-outstanding tracking tables are prohibitively expensive);
  // training fires on the sampled entries that survive to their hit/eviction resolve.
  static uint64_t pext(uint64_t v, uint64_t mask) { uint64_t r = 0; int k = 0; while (mask) { if (mask & 1u) { r |= ((v & 1u) << k); ++k; } v >>= 1; mask >>= 1; } return r; }
  // PC -> table index. PCs are few: a full-address multiply-hash injects entropy that buries the signal, so the
  // default is a contiguous bit SLICE (drop the low alignment bits, keep a key window); drop-out keeps a fixed set
  // of wide-ranging non-consecutive bits (pext) and packs them low.
  uint32_t perc_pc_index(uint64_t pc) const {
    switch (P.perc_pc_encoding) {
      case 1: return static_cast<uint32_t>((pc >> P.perc_pc_lobit) & pw_pc_mask_);            // contiguous slice
      case 2: return static_cast<uint32_t>(pext(pc, P.perc_pc_dropmask) & pw_pc_mask_);       // random drop-out
      default: return static_cast<uint32_t>(((pc * 0x9E3779B97F4A7C15ull) >> 40) & pw_pc_mask_); // legacy hash
    }
  }
  int perc_use_bucket(uint32_t iph) const {
    if (ip_useful_.empty()) return 4;                    // neutral until warm
    uint32_t u = ip_useful_[iph], l = ip_useless_[iph], t = u + l;
    return t < 4 ? 4 : std::min(7, static_cast<int>(8u * u / t)); // 0=useless .. 7=useful
  }
  int perc_tim_bucket(uint32_t iph) const {
    if (ip_ev_.empty()) return 4;
    uint32_t ev = ip_ev_[iph], un = ip_untimely_[iph];
    return ev < 4 ? 4 : std::min(7, static_cast<int>(8u * un / ev)); // 7 = mostly untimely (right addr, too early)
  }
  // engine ids: 0 = SPPAM spatial forward, 1 = SPPAM spatial backward, 2 = delta-PHT (SPP-like delta fall-through),
  // 3 = branch-graph (instruction next-PC). Each engine feeds its OWN signature (6-bit spatial pattern / delta
  // signature / next-PC); we fold the engine into the shared sig index so those signatures don't collide.
  uint32_t perc_sig_index(int engine, uint64_t sig, uint64_t pc = 0) const {
    const uint64_t e = static_cast<uint64_t>(engine < 0 ? 0 : (engine >= PERC_ENGINES ? PERC_ENGINES - 1 : engine));
    uint64_t k = sig ^ (e * 0x9E3779B185EBCA87ull);
    if (P.perc_sig_xor_pc) k ^= (pc >> P.perc_pc_lobit) * 0xD6E8FEB86659FD93ull; // disambiguate (PC,sig) pairs that alias
    return static_cast<uint32_t>(((k * 0x9E3779B97F4A7C15ull) >> 40) & pw_sig_mask_);
  }
  int perc_score(uint64_t block, int engine, int depth, uint64_t pc, int conf, uint64_t sig, uint16_t f[PERC_NF]) const {
    const uint32_t iph = iphash(pc);
    const int eng = engine < 0 ? 0 : (engine >= PERC_ENGINES ? PERC_ENGINES - 1 : engine);
    f[0] = static_cast<uint16_t>(perc_pc_index(pc));            // PC (bit-slice / drop-out, NOT the full address)
    f[1] = 0;                                                  // offset feature DROPPED (redundant with the spatial signature)
    f[2] = static_cast<uint16_t>(eng);                         // which engine proposed this block
    f[3] = static_cast<uint16_t>(depth < 0 ? 0 : (depth > 15 ? 15 : depth));
    f[4] = static_cast<uint16_t>(perc_use_bucket(iph));        // per-IP usefulness (LIVE via perc_resolve, ip-filter-independent)
    f[5] = static_cast<uint16_t>(perc_tim_bucket(iph));        // per-IP untimeliness (LIVE via perc_resolve watch + operate() re-demand, ip-filter-independent)
    f[6] = static_cast<uint16_t>(conf < 0 ? 0 : (conf > 15 ? 15 : conf)); // engine per-prediction confidence (0..15)
    f[7] = static_cast<uint16_t>(perc_sig_index(eng, sig, pc)); // engine-partitioned signature (+ optional PC-mix via perc_sig_xor_pc)
    f[8] = static_cast<uint16_t>(perc_mshr_idx_ < 0 ? 0 : (perc_mshr_idx_ > 15 ? 15 : perc_mshr_idx_)); // aggregate: MSHR/bandwidth pressure
    f[9] = static_cast<uint16_t>(std::clamp(static_cast<int>(perc_useless_ema_ * 15.0 + 0.5), 0, 15));  // aggregate: global useless (truly-bad) rate
    f[10] = static_cast<uint16_t>(std::clamp(static_cast<int>(perc_untimely_ema_ * 15.0 + 0.5), 0, 15)); // aggregate: global untimely (too-early) rate
    f[11] = static_cast<uint16_t>(std::clamp(static_cast<int>(perc_hit_ema_ * 15.0 + 0.5), 0, 15));      // aggregate: demand hit-rate (relative-impact context)
    // CONJUNCTION (path-based): a single weight per COMBINATION of {usefulness, untimeliness, pressure, hit-rate},
    // each coarsened to 2 bits. Lets the gate represent the non-additive relation the independent per-feature
    // weights above cannot (e.g. high pressure => keep when useful, drop when polluting).
    f[12] = static_cast<uint16_t>((((f[4] >> 1) & 3u) << 6) | (((f[5] >> 1) & 3u) << 4) | (((f[8] >> 2) & 3u) << 2) | ((f[11] >> 2) & 3u));
    f[13] = static_cast<uint16_t>(perc_pc_path_ & pw_pcpath_mask_);             // PC-PATH: hash of the recent trigger-PC chain (datacenter PCs recur)
    { const uint64_t d = block - perc_last_trigger_;                            // prefetch distance from the triggering demand
      f[14] = static_cast<uint16_t>((perc_pc_index(pc) ^ static_cast<uint32_t>((d * 0x9E3779B97F4A7C15ull) >> 48)) & pw_pcdelta_mask_); } // PC XOR delta (PC<->data correlation)
    // DSE-derived PAIRWISE interactions (direct product index). The sweep showed discrimination lives in signals XOR'd
    // with CONTENTION (mshr): use^mshr was the universal #1, then tim^mshr/conf^mshr, plus criticality = per-IP value x
    // global hit-rate. These are the non-additive relations the independent per-feature weights cannot represent.
    f[15] = static_cast<uint16_t>((f[4] << 4) | f[8]);  // use^mshr:  per-IP usefulness x DRAM-contention  (0..127)
    f[16] = static_cast<uint16_t>((f[5] << 4) | f[8]);  // tim^mshr:  per-IP untimeliness x DRAM-contention (0..127)
    f[17] = static_cast<uint16_t>((f[6] << 4) | f[8]);  // conf^mshr: engine confidence x DRAM-contention   (0..255)
    f[18] = static_cast<uint16_t>((f[4] << 4) | f[11]); // criticality: per-IP usefulness x global hit-rate window (0..127)
    // 3-WAY PE SPLIT: the fixed I_UPF - I_POLL - I_LAT collapse mis-weights per workload; expose the components as
    // SEPARATE per-IP sampled features so the perceptron learns their relative weight (bandwidth-bound vs capacity-bound).
    // Each = per-IP avg component / perc_pe_scale, clamped to 0..15.
    { auto peb = [&](const std::vector<float>& s, int scale) -> uint16_t { // s[iph] = rolling EMA (cycles) -> 4-bit bucket via a PER-COMPONENT scale
        if (ip_pe_cnt_.empty() || iph >= ip_pe_cnt_.size() || ip_pe_cnt_[iph] == 0) return 0;
        int b = static_cast<int>(s[iph] / static_cast<float>(scale < 1 ? 1 : scale));
        return static_cast<uint16_t>(b < 0 ? 0 : (b > 15 ? 15 : b)); };
      f[19] = peb(ip_pe_sum_,   P.perc_upf_scale);  // I_UPF: per-IP latency saved (benefit)
      f[20] = peb(ip_lat_sum_,  P.perc_lat_scale);  // I_LAT: per-IP DRAM-queueing bandwidth cost
      f[21] = peb(ip_poll_sum_, P.perc_poll_scale); // I_POLL: per-IP pollution cost
    }
    const uint32_t m = P.perc_feat_mask;
    const int es8 = P.perc_engine_split ? eng * 8 : 0, es16 = P.perc_engine_split ? eng * 16 : 0; // per-engine context-table offset
    return ((m & 1) ? pw_pc_[f[0]] : 0) + ((m & 4) ? pw_eng_[f[2]] : 0)
         + ((m & 8) ? pw_dep_[es16 + f[3]] : 0) + ((m & 16) ? pw_use_[es8 + f[4]] : 0) + ((m & 32) ? pw_tim_[es8 + f[5]] : 0)
         + ((m & 64) ? pw_conf_[es16 + f[6]] : 0) + ((m & 128) ? pw_sig_[f[7]] : 0)
         + ((m & 256) ? pw_mshr_[es16 + f[8]] : 0) + ((m & 512) ? pw_guse_[es16 + f[9]] : 0) + ((m & 1024) ? pw_gtim_[es16 + f[10]] : 0)
         + ((m & 2048) ? pw_hit_[es16 + f[11]] : 0) + ((m & 4096) ? pw_conj_[f[12]] : 0)
         + ((m & 0x4000) ? pw_pcpath_[f[13]] : 0) + ((m & 0x8000) ? pw_pcdelta_[f[14]] : 0)   // PC-path + PC^delta (PPF-style)
         + ((m & 0x10000) ? pw_usemshr_[f[15]] : 0) + ((m & 0x20000) ? pw_timmshr_[f[16]] : 0)   // DSE interactions: contention-gated
         + ((m & 0x40000) ? pw_confmshr_[f[17]] : 0) + ((m & 0x80000) ? pw_crit_[f[18]] : 0)      // ...and criticality
         + ((m & 0x100000) ? pw_upf_[f[19]] : 0) + ((m & 0x200000) ? pw_lat_[f[20]] : 0) + ((m & 0x400000) ? pw_poll_[f[21]] : 0) // 3-way PE split
         + ((m & 0x2000) && !pip_bias_.empty() ? pip_bias_[iph] : 0);           // per-IP bias (per-context keep/drop offset)
  }
  // Final gate: true = issue. On a "drop", still issue 1/perc_explore_div of the time (exploration) so dropped
  // regions of the feature space keep producing labels. Stashes the feature vector for the sampler to record.
  // PER-EVALUATION log: print each ACTIVE feature's CONTRIBUTION (its weight-table value) + bucket for a sampled drop,
  // so post-processing can see which features drove the score. Sum of contributions == y. Block joins to PBAD (re-demand).
  uint32_t perc_evlog_ctr_ = 0;
  void perc_log_eval(uint64_t block, int y) {
    const uint16_t* f = perc_last_feat_; const uint32_t m = P.perc_feat_mask;
    const int e8 = P.perc_engine_split ? f[2] * 8 : 0, e16 = P.perc_engine_split ? f[2] * 16 : 0;
    std::fprintf(stderr, "PDROP,%llu,y=%d", (unsigned long long)block, y);
    auto pr = [&](const char* nm, int c, int fb) { std::fprintf(stderr, ",%s=%d@%d", nm, c, fb); };
    if (m & 1) pr("PC", pw_pc_[f[0]], f[0]);            if (m & 4) pr("eng", pw_eng_[f[2]], f[2]);
    if (m & 8) pr("dep", pw_dep_[e16 + f[3]], f[3]);    if (m & 16) pr("use", pw_use_[e8 + f[4]], f[4]);
    if (m & 32) pr("tim", pw_tim_[e8 + f[5]], f[5]);    if (m & 64) pr("conf", pw_conf_[e16 + f[6]], f[6]);
    if (m & 128) pr("sig", pw_sig_[f[7]], f[7]);        if (m & 256) pr("mshr", pw_mshr_[e16 + f[8]], f[8]);
    if (m & 512) pr("guse", pw_guse_[e16 + f[9]], f[9]); if (m & 1024) pr("gtim", pw_gtim_[e16 + f[10]], f[10]);
    if (m & 2048) pr("hit", pw_hit_[e16 + f[11]], f[11]); if (m & 4096) pr("conj", pw_conj_[f[12]], f[12]);
    if (m & 0x4000) pr("pcpath", pw_pcpath_[f[13]], f[13]); if (m & 0x8000) pr("pcdelta", pw_pcdelta_[f[14]], f[14]);
    if (m & 0x10000) pr("use^m", pw_usemshr_[f[15]], f[15]); if (m & 0x20000) pr("tim^m", pw_timmshr_[f[16]], f[16]);
    if (m & 0x40000) pr("conf^m", pw_confmshr_[f[17]], f[17]); if (m & 0x80000) pr("crit", pw_crit_[f[18]], f[18]);
    if (m & 0x100000) pr("I_UPF", pw_upf_[f[19]], f[19]); if (m & 0x200000) pr("I_LAT", pw_lat_[f[20]], f[20]);
    if (m & 0x400000) pr("I_POLL", pw_poll_[f[21]], f[21]);
    std::fprintf(stderr, "\n");
  }
public: // perc_keep + perc_note_issue are the engine call sites' entry points (SPPAM internal, plus the glue's branch-graph functor)
  bool perc_keep(uint64_t block, int engine, int depth, uint64_t pc, int conf, uint64_t sig) {
    int y = perc_score(block, engine, depth, pc, conf, sig, perc_last_feat_);
    if (P.perc_dump_weights && (++dbg_audit_ctr_ & 63u) == 0) { ++dbg_fh_n_; // FEATURE AUDIT: sample 1/64 gates
      for (int i = 0; i < PERC_NF; ++i) { uint16_t v = perc_last_feat_[i]; ++dbg_fh_[i][v > 15 ? 15 : v]; } }
    perc_last_iph_ = iphash(pc);                              // stash for per-IP usefulness crediting at resolve
    if (P.perc_profile) { perc_last_rpc_ = static_cast<uint32_t>(pc); perc_last_rsig_ = static_cast<uint32_t>(sig); // raw signals for the offline sweep
      perc_last_rdelta_ = static_cast<uint32_t>(block - perc_last_trigger_); perc_last_rp1_ = perc_pc_ring_[1]; perc_last_rp2_ = perc_pc_ring_[2];
      perc_last_rap_ = (engine <= 1) ? static_cast<uint32_t>(perc_raw_ap_) : 0u;   // opposite-dir spatial pattern source (SPPAM engines only)
      perc_last_rnxip_ = static_cast<uint32_t>(perc_raw_nxip_); perc_raw_nxip_ = 0; } // BG next-pc block (consumed per gate)
    perc_last_valid_ = true; perc_last_explored_ = false;
    if (y >= P.perc_tau_keep) { ++dbg_perc_keep_; return true; }
    // UNTIMELY VETO (DSE lesson): the score says drop, but if this trigger IP is strongly UNTIMELY (right address,
    // evicted-before-use), do NOT volume-drop it -- that kills coverage on pointer-chasers (mcf). Leave it to
    // depth-throttle (which shortens its lookahead). The perceptron only drops TRULY-BAD (wrong-address) IPs.
    if (P.perc_untimely_veto && !ip_ev_.empty()) {
      const uint32_t iph = perc_last_iph_; // = iphash(pc)
      if (ip_ev_[iph] >= 4 && 100u * ip_untimely_[iph] >= static_cast<uint32_t>(P.ip_untimely_thresh) * ip_ev_[iph]) {
        ++dbg_perc_veto_; return true; // keep (trained normally: perc_last_valid_ stays true)
      }
    }
    // CONTENTION VETO (DSE lesson #3): under light MSHR/bandwidth pressure a dropped prefetch cannot be hurting
    // anyone, and PE mis-ranks genuinely-useful prefetches on the low-load datacenter. So only drop under real load.
    if (P.perc_load_gate && perc_mshr_idx_ < P.perc_load_gate_thresh) { ++dbg_perc_veto_; return true; }
    // INSTRUCTION-ACTIVITY GATE: on instruction-bound workloads the data perceptron only loses coverage (it never
    // beats pe-management there). instr-pf fraction cleanly flags them -> back off (force keep). See [[perceptron-complements-pe-mgmt]].
    if (P.perc_instr_gate && perc_instr_pct_ >= P.perc_instr_gate_pct) { ++dbg_perc_veto_; ++dbg_perc_igate_; return true; }
    // USE-VETO: never drop a prefetch whose trigger IP has HIGH per-IP accuracy (the ip_filter's protective logic). The
    // per-IP-usefulness signal WINS over the PC/sig features here -- spares the high-accuracy datacenter/LLM prefetches
    // the aggressive filter would otherwise wrongly drop, while still filtering low-accuracy IPs hard.
    if (P.perc_use_veto && !ip_useful_.empty()) {
      const uint32_t iph = perc_last_iph_;
      if (ip_useful_[iph] + ip_useless_[iph] >= 8u && perc_use_bucket(iph) >= P.perc_use_veto_thresh) { ++dbg_perc_veto_; return true; }
    }
    // PE GATE: PE says whether DROPPING can even help. If the trigger IP's sampled avg PE is clearly GOOD, prefetching
    // helps regardless of usefulness -> do NOT filter (we can't discern a filtering win). Only bad-PE IPs get dropped.
    if (P.perc_pe_gate && !ip_pe_cnt_.empty()) {
      const uint32_t iph = perc_last_iph_;
      // NET PE = I_UPF - I_LAT - I_POLL (not raw latency): a bandwidth-bound IP has high latency but high cost -> net PE
      // low -> DON'T veto (filtering can help). Only genuinely PE-positive IPs (benefit outweighs cost) are spared.
      const float net = ip_pe_sum_[iph] - ip_lat_sum_[iph] - ip_poll_sum_[iph]; // EMAs -> already recent-avg cycles, no /cnt
      if (ip_pe_cnt_[iph] >= static_cast<uint32_t>(P.perc_pe_gate_min_samples)
          && net >= static_cast<float>(P.perc_pe_gate_thresh)) {
        ++dbg_perc_veto_; ++dbg_perc_pegate_; return true;
      }
    }
    perc_lfsr_ ^= perc_lfsr_ << 13; perc_lfsr_ ^= perc_lfsr_ >> 17; perc_lfsr_ ^= perc_lfsr_ << 5;
    if (P.perc_explore_div && (perc_lfsr_ % P.perc_explore_div == 0)) { ++dbg_perc_explore_; perc_last_explored_ = true; return true; }
    ++dbg_perc_drop_; perc_last_valid_ = false; // dropped for real -> no sample/label
    if (P.perc_eval_log && (++perc_evlog_ctr_ % static_cast<uint32_t>(P.perc_eval_log_div)) == 0) perc_log_eval(block, y); // sampled per-eval contribution dump
    if (P.perc_victim) perc_victim_insert(block, perc_last_feat_, perc_last_iph_); // PPF reject table: watch for a re-demand
    return false;
  }
  // Train on a per-prefetch VALUE. usefulness (used=+1 / evicted=-1) is a poor proxy for performance -- a
  // DRAM-covering hit saves ~10x an LLC hit, and a "useful" prefetch can still net-pollute. So val carries the
  // PE magnitude (latency saved on a hit, cost on an eviction): sign = keep/drop, magnitude = training step.
  void perc_train(const uint16_t f[PERC_NF], uint32_t iph, double val) {
    const int eng_dbg = (f[2] < PERC_ENGINES) ? f[2] : (PERC_ENGINES - 1); // training funnel bookkeeping (engine = f[2])
    if (val >= 0.0) ++dbg_res_pos_[eng_dbg]; else ++dbg_res_neg_[eng_dbg];  // resolved sample, pre-filter, by sign
    if (P.perc_label_pe && std::abs(val) < static_cast<double>(P.perc_pe_margin)) { ++dbg_skip_margin_; return; } // skip ambiguous (small |PE|)
    const bool keep_lbl = val >= 0.0;
    const uint32_t mm = P.perc_feat_mask;                       // confidence check uses the SAME masked score as the gate
    const int e8 = P.perc_engine_split ? f[2] * 8 : 0, e16 = P.perc_engine_split ? f[2] * 16 : 0; // per-engine offset (engine = f[2])
    const int y = ((mm & 1) ? pw_pc_[f[0]] : 0) + ((mm & 4) ? pw_eng_[f[2]] : 0) + ((mm & 8) ? pw_dep_[e16 + f[3]] : 0)
                + ((mm & 16) ? pw_use_[e8 + f[4]] : 0) + ((mm & 32) ? pw_tim_[e8 + f[5]] : 0) + ((mm & 64) ? pw_conf_[e16 + f[6]] : 0)
                + ((mm & 128) ? pw_sig_[f[7]] : 0) + ((mm & 256) ? pw_mshr_[e16 + f[8]] : 0)
                + ((mm & 512) ? pw_guse_[e16 + f[9]] : 0) + ((mm & 1024) ? pw_gtim_[e16 + f[10]] : 0)
                + ((mm & 2048) ? pw_hit_[e16 + f[11]] : 0) + ((mm & 4096) ? pw_conj_[f[12]] : 0)
                + ((mm & 0x4000) ? pw_pcpath_[f[13]] : 0) + ((mm & 0x8000) ? pw_pcdelta_[f[14]] : 0)
                + ((mm & 0x10000) ? pw_usemshr_[f[15]] : 0) + ((mm & 0x20000) ? pw_timmshr_[f[16]] : 0)
                + ((mm & 0x40000) ? pw_confmshr_[f[17]] : 0) + ((mm & 0x80000) ? pw_crit_[f[18]] : 0)
                + ((mm & 0x100000) ? pw_upf_[f[19]] : 0) + ((mm & 0x200000) ? pw_lat_[f[20]] : 0) + ((mm & 0x400000) ? pw_poll_[f[21]] : 0)
                + ((mm & 0x2000) && !pip_bias_.empty() ? pip_bias_[iph] : 0);
    const bool wrong = (y >= P.perc_tau_keep) != keep_lbl;
    if (!wrong && std::abs(y) >= P.perc_theta_train) { ++dbg_skip_conf_; return; } // confident-correct -> no update
    int step = 1;                                            // usefulness / sign-only PE: unit step (low variance)
    if (P.perc_label_pe && !P.perc_pe_signonly) {            // QUANTIZE the PE reward into a bounded signed level [1..pe_step_max]
      if (P.perc_pe_norm) pe_ema_ += (std::abs(val) - pe_ema_) * (1.0 / 64.0);   // SMOOTH: rolling mean of |PE|
      const double sc = (P.perc_pe_norm && pe_ema_ > 1.0) ? pe_ema_ : P.perc_pe_scale; // normalize by the mean (scale-invariant) or a fixed scale
      if (sc > 0.0) step = std::clamp(static_cast<int>(std::abs(val) / sc) + 1, 1, P.perc_pe_step_max);
    }
    const int d = keep_lbl ? step : -step;
    perc_apply(f, iph, d);
    if (keep_lbl) ++dbg_tr_pos_[eng_dbg]; else ++dbg_tr_neg_[eng_dbg];
    ++dbg_perc_train_;
  }
  // Snapshot the last gate's feature vector for dense training when this block's L2 outcome lands. Used by the
  // engine call sites that gate through perc_keep (SPPAM, delta-PHT, and the glue's branch-graph functor).
  // UNIFIED sampling: perc_track_ now also holds the fill latency + accrued cost the glue used to duplicate in a
  // separate perc_sdm_ table. The glue feeds these; perc_track_lat reads the PE magnitude back at resolve.
  void perc_note_fill(uint64_t block, uint32_t lat) { // glue calls at fill (issue->fill latency, DRAM high / LLC low)
    if (perc_track_.empty()) return;
    perc_ent& e = perc_track_[sdm_idx(block) & perc_track_mask_];
    if (e.occ && e.tag == static_cast<uint32_t>(block) && !e.filled) { e.lat = lat; e.filled = true; }
  }
  // DSE-faithful fill latency: measure THIS perc_track_ entry's real issue->fill latency directly, so every tracked
  // prefetch gets its true DRAM(high)/LLC(low) PE magnitude instead of the pfht_-bridge llc_hit fallback. Glue calls
  // this at EVERY data fill (not gated on pfht_ sampling). lat capped at 100000 (wrap/outlier guard).
  void perc_fill_measure(uint64_t block) {
    if (perc_track_.empty()) return;
    perc_ent& e = perc_track_[sdm_idx(block) & perc_track_mask_];
    if (e.occ && e.tag == static_cast<uint32_t>(block) && !e.filled) {
      uint32_t d = static_cast<uint32_t>(perc_rc_) - e.issue_rc; e.lat = d > 100000u ? 100000u : d; e.filled = true;
    }
  }
  void perc_add_cost(uint64_t block, float delta) { // glue calls for I_LAT (at fill) and I_POLL (on victim re-miss)
    if (perc_track_.empty()) return;
    perc_ent& e = perc_track_[sdm_idx(block) & perc_track_mask_];
    if (e.occ && e.tag == static_cast<uint32_t>(block)) e.cost += delta;
  }
  // Returns true iff a filled entry exists; hands back its fill latency + accrued cost (the perceptron PE magnitude).
  bool perc_track_lat(uint64_t block, uint32_t& lat, float& cost) const {
    if (perc_track_.empty()) return false;
    const perc_ent& e = perc_track_[sdm_idx(block) & perc_track_mask_];
    if (e.occ && e.tag == static_cast<uint32_t>(block)) { lat = e.lat; cost = e.cost; return e.filled; }
    return false;
  }
  void perc_note_issue(uint64_t block) {
    if (!(perc_last_valid_ || perc_last_explored_)) return;
    // DENSE PATH: snapshot EVERY issued prefetch's gate features (no 1/pe_sample_div, no PE latency); train on its
    // real useful(hit)/useless(evict) outcome. The coarse features (use/tim/hit/pe) are the CLEANLY-SAMPLED inputs;
    // the DIRECT features (PC/sig/delta/pcpath) come from current prefetcher state. Label = usefulness (first-outcome).
    if (P.perc_dense_train) { perc_dense_insert(block, perc_last_feat_, perc_last_iph_, perc_last_explored_); return; }
    // SPARSE (legacy) PATH: sample like the glue's pfht_ (1/pe_sample_div) into the PE-latency table. A DENSE insert
    // SATURATES the pinned table: live entries never survive to their true resolve -> all time out USELESS.
    if (P.pe_sample_div > 1 && (++perc_sample_ctr_ % P.pe_sample_div) != 0) return;
    perc_track_insert(block, perc_last_feat_, perc_last_iph_, perc_last_explored_);
  }
  uint32_t perc_sample_ctr_ = 0;
  // DENSE feature snapshot: block-keyed, one entry per issued prefetch, overwrite-on-collision (each prefetch resolves
  // once -- first-outcome). No latency, no pin/timeout. Trained at its hit(useful=+1)/evict-unused(useless=-1).
  void perc_dense_insert(uint64_t block, const uint16_t f[PERC_NF], uint32_t iph, bool explored) {
    if (perc_dense_.empty()) return;
    dense_ent& e = perc_dense_[sdm_idx(block) & perc_dense_mask_];
    for (int k = 0; k < PERC_NF; ++k) e.f[k] = f[k];
    e.tag = static_cast<uint32_t>(block); e.iph = iph; e.occ = true; e.explored = explored;
  }
  // Resolve a dense entry on its true outcome and train ONCE (first-outcome: retire the slot). useful=+1, useless=-1.
  // Returns true iff it trained (the block was tracked) -- lets the glue know a real outcome landed.
  // Trigger-IP hash stored for an in-flight dense entry (for the glue to attribute BG per-IP PE). 0xFFFFFFFF = not tracked.
  uint32_t perc_dense_iph(uint64_t block) const {
    if (perc_dense_.empty()) return 0xFFFFFFFFu;
    const dense_ent& e = perc_dense_[sdm_idx(block) & perc_dense_mask_];
    return (e.occ && e.tag == static_cast<uint32_t>(block)) ? e.iph : 0xFFFFFFFFu;
  }
  bool perc_dense_resolve(uint64_t block, bool useful) {
    if (perc_dense_.empty()) return false;
    dense_ent& e = perc_dense_[sdm_idx(block) & perc_dense_mask_];
    if (!e.occ || e.tag != static_cast<uint32_t>(block)) return false;
    e.occ = false;
    // feed the same live per-IP usefulness/timeliness discriminators the sampled path fed (so the coarse FEATURES stay
    // warm off the dense stream too), then train the perceptron on the dense ±1 label.
    if (!ip_useful_.empty()) { if (useful) ++ip_useful_[e.iph]; else ++ip_useless_[e.iph]; ip_age_tick(); }
    perc_train(e.f.data(), e.iph, useful ? 1.0 : -1.0);
    return true;
  }
  // Masked weight update by a signed step d (shared by perc_train and the victim heavy-keep). engine = f[2].
  void perc_apply(const uint16_t f[PERC_NF], uint32_t iph, int d) {
    const int e8 = P.perc_engine_split ? f[2] * 8 : 0, e16 = P.perc_engine_split ? f[2] * 16 : 0;
    const uint32_t m = P.perc_feat_mask;
    auto upd = [&](std::vector<int16_t>& w, uint16_t i) { w[i] = static_cast<int16_t>(std::clamp(w[i] + d, -P.perc_weight_max, P.perc_weight_max)); };
    if (m & 1) upd(pw_pc_, f[0]);
    if (m & 4) upd(pw_eng_, f[2]);
    if (m & 8) upd(pw_dep_, e16 + f[3]); if (m & 16) upd(pw_use_, e8 + f[4]); if (m & 32) upd(pw_tim_, e8 + f[5]);
    if (m & 64) upd(pw_conf_, e16 + f[6]); if (m & 128) upd(pw_sig_, f[7]);
    if (m & 256) upd(pw_mshr_, e16 + f[8]); if (m & 512) upd(pw_guse_, e16 + f[9]); if (m & 1024) upd(pw_gtim_, e16 + f[10]);
    if (m & 2048) upd(pw_hit_, e16 + f[11]); if (m & 4096) upd(pw_conj_, f[12]);
    if (m & 0x4000) upd(pw_pcpath_, f[13]); if (m & 0x8000) upd(pw_pcdelta_, f[14]);
    if (m & 0x10000) upd(pw_usemshr_, f[15]); if (m & 0x20000) upd(pw_timmshr_, f[16]);
    if (m & 0x40000) upd(pw_confmshr_, f[17]); if (m & 0x80000) upd(pw_crit_, f[18]);
    if (m & 0x100000) upd(pw_upf_, f[19]); if (m & 0x200000) upd(pw_lat_, f[20]); if (m & 0x400000) upd(pw_poll_, f[21]);
    if ((m & 0x2000) && !pip_bias_.empty()) upd(pip_bias_, static_cast<uint16_t>(iph));
  }
  // VICTIM TABLE: record a DROPPED prefetch's feature vector (sampled). If the block is later demanded, the drop lost
  // coverage -> perc_victim_check trains it back toward KEEP with a heavy step (punish over-filtering).
  void perc_victim_insert(uint64_t block, const uint16_t f[PERC_NF], uint32_t iph) {
    if (perc_victim_.empty()) return;
    if (P.perc_victim_sample_div > 1 && (++perc_victim_ctr_ % static_cast<uint32_t>(P.perc_victim_sample_div)) != 0) return;
    dense_ent& e = perc_victim_[sdm_idx(block) & perc_victim_mask_];
    for (int k = 0; k < PERC_NF; ++k) e.f[k] = f[k];
    e.tag = static_cast<uint32_t>(block); e.iph = iph; e.occ = true;
  }
public:
  // Glue calls on every demand: if this block was a DROPPED prefetch (in the victim table), the drop was BAD ->
  // heavy KEEP retrain + retire the entry. Returns true iff it was a victim (a punished bad drop).
  bool perc_victim_check(uint64_t block) {
    if (perc_victim_.empty()) return false;
    dense_ent& e = perc_victim_[sdm_idx(block) & perc_victim_mask_];
    if (!e.occ || e.tag != static_cast<uint32_t>(block)) return false;
    e.occ = false;
    perc_apply(e.f.data(), e.iph, P.perc_victim_penalty); // heavy KEEP (+): this pattern SHOULD NOT have been dropped
    ++dbg_victim_hit_;
    if (P.perc_eval_log) std::fprintf(stderr, "PBAD,%llu\n", (unsigned long long)block); // this dropped block was re-demanded = BAD drop
    return true;
  }
  // Glue feeds the 3 sampled PE components per trigger IP at a prefetch fill: upf=latency saved, lat=DRAM-queueing
  // bandwidth cost. (I_POLL arrives separately at the pollution event via perc_note_ip_poll.) One shared sample count.
  // ROLLING EMAs (alpha=1/2^perc_pe_ema_shift), each fed at ITS OWN event with the TRUE component value:
  //  I_UPF  = fill latency saved  (fed at fill),  I_LAT = the real Eq4 service cost this pf imposes (fed at Eq4),
  //  I_POLL = pollution cost       (fed on a polluted re-miss). Never cumulative -> tracks recent phase.
  void perc_note_ip_upf(uint32_t iph, float upf) { // latency saved (cycles), capped to a sane DRAM max upstream
    if (ip_pe_sum_.empty() || iph >= ip_pe_sum_.size()) return;
    const float a = 1.0f / static_cast<float>(1u << P.perc_pe_ema_shift);
    ip_pe_sum_[iph] += (upf - ip_pe_sum_[iph]) * a;
    if (ip_pe_cnt_[iph] < 255) ++ip_pe_cnt_[iph];   // recent-activity warmth (decayed in ip_age_tick)
    dbg_upf_sum_ += upf; ++dbg_upf_n_; if (upf > dbg_upf_max_) dbg_upf_max_ = upf;
  }
  void perc_note_ip_lat(uint32_t iph, float lat) { // TRUE I_LAT = w*serv (~pe_serv_dram), NOT fill-latency-minus-base
    if (ip_lat_sum_.empty() || iph >= ip_lat_sum_.size()) return;
    const float a = 1.0f / static_cast<float>(1u << P.perc_pe_ema_shift);
    ip_lat_sum_[iph] += (lat - ip_lat_sum_[iph]) * a;
    dbg_lat_sum_ += lat; if (lat > dbg_lat_max_) dbg_lat_max_ = lat;
  }
  void perc_note_ip_poll(uint32_t iph, float poll) { // pollution charged to the polluting prefetch's IP (rolling EMA)
    if (ip_poll_sum_.empty() || iph >= ip_poll_sum_.size()) return;
    const float a = 1.0f / static_cast<float>(1u << P.perc_pe_ema_shift);
    ip_poll_sum_[iph] += (poll - ip_poll_sum_[iph]) * a;
    dbg_poll_sum_ += poll; ++dbg_poll_n_; if (poll > dbg_poll_max_) dbg_poll_max_ = poll; // audit: raw I_POLL cycles
  }
private:
  uint64_t dbg_victim_hit_ = 0;
  // DENSE training path: snapshot the feature vector of EVERY kept L2 prefetch, then train when the actual
  // outcome lands (a demand hit = useful, an evicted-unused = useless). Outcomes resolve far more often than
  // the sparse per-IP sampling, which stays as an INPUT FEATURE (usefulness/timeliness buckets), not the trigger.
  void perc_track_insert(uint64_t block, const uint16_t f[PERC_NF], uint32_t iph, bool explored) {
    // SMALL fixed direct-mapped table. ESTABLISHED balanced-sampling policy (mirrors the glue's pfht_ USELESS_ON_TIMEOUT):
    // PIN a recent in-flight incumbent -- NEVER churn-overwrite it. A churn-overwrite biases training toward
    // fast-resolving USEFUL prefetches (useful hits resolve quickly; useless ones resolve LATE at eviction, so an
    // overwrite table drops them). On timeout, resolve the stale incumbent USELESS_ON_TIMEOUT (it sat unresolved past
    // the window => it was useless) before reusing its slot -- this is what removes the under-sampling bias.
    if (perc_track_.empty()) return;
    perc_ent& e = perc_track_[sdm_idx(block) & perc_track_mask_];
    const bool stale = e.occ && (static_cast<uint32_t>(cycle_) - e.stamp) > P.perc_track_ttl; // DEDICATED long ttl (op-clock): a short ttl times out not-yet-demanded useful prefetches as useless -> over-drop
    if (e.occ && !stale) return;                              // PIN the recent incumbent; drop this new snapshot
    if (stale) perc_resolve_entry(e, e.tag, -static_cast<double>(P.llc_hit_latency)); // useless-on-timeout -> frees the slot
    for (int k = 0; k < PERC_NF; ++k) e.f[k] = f[k];
    e.tag = static_cast<uint32_t>(block); e.stamp = static_cast<uint32_t>(cycle_); e.iph = iph; e.occ = true; e.explored = explored;
    e.issue_rc = static_cast<uint32_t>(perc_rc_);   // real issue cycle -> self-measured fill latency (independent of pfht_)
    e.lat = 0; e.cost = 0.0f; e.filled = false; // reset the unified fill-latency/cost fields for the new prefetch
    if (P.perc_profile) { e.r_pc = perc_last_rpc_; e.r_sig = perc_last_rsig_; e.r_delta = perc_last_rdelta_; e.r_p1 = perc_last_rp1_; e.r_p2 = perc_last_rp2_;
      e.r_block = static_cast<uint32_t>(block); e.r_ap = perc_last_rap_; e.r_nxip = perc_last_rnxip_; }
  }
  // Resolve a tracked entry (looked up by the caller) with per-prefetch PE. Shared by the direct block lookup and by
  // USELESS_ON_TIMEOUT (which passes the incumbent's own tag as `block`).
  void perc_resolve_entry(perc_ent& e, uint64_t block, double val) {
    const bool useful = val >= 0.0;               // discrimination diagnostic: was this kept/dropped prefetch actually useful?
    if (e.explored) { ++dbg_drop_res_; if (useful) ++dbg_drop_res_u_; }
    else { ++dbg_keep_res_; if (useful) ++dbg_keep_res_u_; }
    // Feed the LIVE aggregate/per-IP discriminators (independent of the ip-filter): per-IP usefulness (f[4]) and the
    // global useless-rate (f[9]). A useful resolve is also not-untimely; the untimely numerator arrives via the glue.
    if (!ip_useful_.empty()) { if (useful) ++ip_useful_[e.iph]; else ++ip_useless_[e.iph]; ip_age_tick(); }
    perc_useless_ema_ += ((useful ? 0.0 : 1.0) - perc_useless_ema_) * (1.0 / 1024.0);
    if (!useful) { // evicted-unused: seed the per-IP untimely watch (denominator now; a later re-demand in operate() is the numerator)
      if (!ip_ev_.empty()) ++ip_ev_[e.iph];
      if (evicted_unused_.size() >= static_cast<std::size_t>(P.evicted_unused_cap)) evicted_unused_.clear(); // crude bound (sampling tolerates loss)
      evicted_unused_[block] = static_cast<uint16_t>(e.iph);
      perc_untimely_ema_ += (0.0 - perc_untimely_ema_) * (1.0 / 1024.0); // assume truly-bad until a re-demand corrects it
    }
    if (P.perc_profile && (++perc_prof_ctr_ % 32 == 0)) { // sampled RAW tuple for the offline feature sweep (aggregate PE x coverage)
      // cols: eng,pc,sig,delta,depth,use,tim,conf,mshr,hit,guse,gtim,p1,p2,block,ap,nxip,PE
      //   block=predicted block (page/VPN-delta), ap=SPPAM spatial bitmap (opposite-dir via bit-reverse), nxip=BG next-pc block
      std::fprintf(stderr, "PROF,%d,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u,%u,%u,%u,%.1f\n",
                   e.f[2], e.r_pc, e.r_sig, e.r_delta, e.f[3], e.f[4], e.f[5], e.f[6], e.f[8], e.f[11], e.f[9], e.f[10],
                   e.r_p1, e.r_p2, e.r_block, e.r_ap, e.r_nxip, val);
    }
    perc_train(e.f.data(), e.iph, val);
    e.occ = false;
  }
  void perc_resolve(uint64_t block, double val) { // val = per-prefetch PE (or +/-1 usefulness fallback)
    if (perc_track_.empty()) return;
    perc_ent& e = perc_track_[sdm_idx(block) & perc_track_mask_];
    if (!e.occ || e.tag != static_cast<uint32_t>(block)) return; // displaced / never sampled -> no training event
    perc_resolve_entry(e, block, val);
  }
public:
  // Resolve a tracked prefetch from engines gated via the sink/glue (branch-graph, SPP) so they TRAIN the
  // perceptron. Without this, used branch-graph prefetches never resolve -> the gate only ever sees BG as
  // useless -> drops all BG. pe > 0 = useful (latency saved), pe < 0 = useless (wasted-fill cost).
  void perc_resolve_ext(uint64_t block, double pe) { perc_resolve(block, pe); }
  void perc_note_used(double pe) { if (pe >= 0.0) ++dbg_used_pe_pos_; else ++dbg_used_pe_neg_; } // PE-vs-IPC misalignment probe
private:
  // The glue calls these to feed the aggregate-harm signals the perceptron can't derive itself.
public:
  void perc_set_mshr(int idx) { perc_mshr_idx_ = idx; }                    // MSHR/bandwidth pressure 0..15, refreshed per access
  void perc_set_rc(uint64_t rc) { perc_rc_ = rc; }                         // real cycle, refreshed each operate/fill (self-measured fill latency)
  void perc_set_instr_pct(int pct) { perc_instr_pct_ = pct; }              // instruction-pf activity fraction (%), refreshed per operate (datacenter gate)
private:
  // Per-feature accumulated weight magnitude -> which features actually carry signal (sum|w|/mean|w| high = used).
  void perc_dump() const {
    auto stat = [&](const std::vector<int16_t>& w, const char* nm) {
      if (w.empty()) return;
      long sum = 0, maxa = 0; std::size_t nnz = 0;
      for (int16_t x : w) { long a = x < 0 ? -x : x; sum += a; if (a > maxa) maxa = a; if (a) ++nnz; }
      std::fprintf(stderr, "  %-4s entries=%zu sum|w|=%ld mean|w|=%.2f max|w|=%ld nonzero=%zu(%d%%)\n",
                   nm, w.size(), sum, static_cast<double>(sum) / static_cast<double>(w.size()), maxa, nnz,
                   static_cast<int>(100 * nnz / w.size()));
    };
    std::fprintf(stderr, "[perc-weights] %s (sum|w| = signal accumulated per feature):\n", P.name.c_str());
    stat(pw_pc_, "PC"); stat(pw_eng_, "eng"); stat(pw_dep_, "dep");
    stat(pw_use_, "use"); stat(pw_tim_, "tim"); stat(pw_conf_, "conf"); stat(pw_sig_, "sig");
    stat(pw_mshr_, "mshr"); stat(pw_guse_, "guse"); stat(pw_gtim_, "gtim"); // aggregate-harm features
    stat(pw_hit_, "hit"); stat(pw_conj_, "conj"); // hit-rate + conjunction (non-additive interaction)
    stat(pw_pcpath_, "pcpa"); stat(pw_pcdelta_, "pcdl"); // PC-path + PC^delta (PPF-style)
    stat(pw_usemshr_, "u^m"); stat(pw_timmshr_, "t^m"); stat(pw_confmshr_, "c^m"); stat(pw_crit_, "crit"); // DSE mshr-interactions
    stat(pw_upf_, "UPF"); stat(pw_lat_, "LAT"); stat(pw_poll_, "POLL"); // 3-way PE split (benefit / bandwidth / pollution)
    stat(pip_bias_, "pipb"); // per-IP bias (per-context personalization)
    auto small = [&](const std::vector<int16_t>& w, const char* nm) {
      if (w.empty() || w.size() > 16) return;
      std::string s; for (int16_t x : w) { s += std::to_string(x); s += ' '; }
      std::fprintf(stderr, "    %-4s = [ %s]\n", nm, s.c_str());
    };
    small(pw_eng_, "eng"); small(pw_dep_, "dep"); small(pw_use_, "use"); small(pw_tim_, "tim"); small(pw_conf_, "conf");
    small(pw_mshr_, "mshr"); small(pw_guse_, "guse"); small(pw_gtim_, "gtim"); small(pw_hit_, "hit");
    small(pw_upf_, "UPF"); small(pw_lat_, "LAT"); small(pw_poll_, "POLL"); // per-bucket PE-split weights = learned latency/pollution throttle
  }
  struct pat_val_t { uint32_t u = 0, n = 0; };
  std::unordered_map<uint64_t, pat_val_t> pat_val_;      // pattern key -> validated useful / useless counts
  std::vector<uint32_t> ip_useful_, ip_useless_;         // per-trigger-IP-bucket sampled useful / useless (ip_n_ entries)
  std::vector<uint32_t> ip_gate_ctr_;                    // per-IP trickle counter for the throttle
  std::vector<uint32_t> ip_ev_, ip_untimely_;            // depth-throttle: evicted-unused / (of those) later re-demanded
  std::vector<uint32_t> ip_bwd_useful_, ip_bwd_useless_; // per-IP usefulness of BACKWARD-scan prefetches ONLY (gates backward)
  std::unordered_map<uint64_t, uint16_t> evicted_unused_;// sampled evicted-unused prefetch block -> IP (watch for re-demand)
  uint32_t pv_lfsr_ = 0x1234567u;
  uint32_t evict_lfsr_ = 0x9abcdefu; // random region-eviction policy
  uint64_t ip_age_ctr_ = 0;
  uint64_t dbg_pv_bad_ = 0, dbg_pv_good_ = 0, dbg_ip_throttled_ = 0;
  uint64_t dbg_amap_bits_ = 0, dbg_amap_regs_ = 0, dbg_amap_sparse_ = 0, dbg_amap_full_ = 0; // access-map fill at eviction
  // Region-density / granularity study (region_density_report): fill histogram + per-granularity lossless count
  // and over-fetch cost (blocks a coarser bitmap would spuriously mark present). Granularities G = 2,4,8,16.
  std::vector<uint64_t> rd_fill_hist_;              // [pop] -> #regions with that many touched blocks
  std::array<uint64_t, 4> rd_lossless_{{0, 0, 0, 0}};   // #regions exactly representable at G (bits equal within each G-group)
  std::array<uint64_t, 4> rd_overfetch_{{0, 0, 0, 0}};  // total extra blocks if coarsened to G (OR each group)
  uint64_t rd_n_ = 0, rd_touched_ = 0;              // regions counted / total touched blocks
  std::unordered_set<uint64_t> rd_footprints_;      // distinct packed access maps (only when blocks_per_region<=64)
  void region_density_note(const region_type& r)
  {
    const int bpr = static_cast<int>(blocks_per_region_);
    if (rd_fill_hist_.empty()) rd_fill_hist_.assign(bpr + 1, 0);
    int pop = 0; uint64_t packed = 0;
    for (int o = 0; o < bpr; ++o) if (r.access_map[o]) { ++pop; if (o < 64) packed |= (uint64_t{1} << o); }
    ++rd_fill_hist_[pop]; ++rd_n_; rd_touched_ += static_cast<uint64_t>(pop);
    if (bpr <= 64) rd_footprints_.insert(packed);
    const int Gs[4] = {2, 4, 8, 16};
    for (int gi = 0; gi < 4; ++gi) {
      const int G = Gs[gi]; bool lossless = true; int extra = 0;
      for (int base = 0; base < bpr; base += G) {
        int grp = 0, span = 0;
        for (int k = 0; k < G && base + k < bpr; ++k) { ++span; if (r.access_map[base + k]) ++grp; }
        if (grp != 0 && grp != span) lossless = false; // mixed group -> lossy at this granularity
        if (grp != 0) extra += span - grp;             // coarsening marks the whole group present
      }
      if (lossless) ++rd_lossless_[gi];
      rd_overfetch_[gi] += static_cast<uint64_t>(extra);
    }
  }
  uint32_t iphash(uint64_t ip) const { return static_cast<uint32_t>((ip * 0x9E3779B97F4A7C15ull) >> 52) & ip_mask_; }
  // Trickle divisor from the sampled per-IP usefulness (soft/hard bands, same as shipping ip_trickle_div).
  uint32_t ip_trickle_div(uint32_t iph) const
  {
    if (ip_useful_.empty()) return 1;
    uint32_t u = ip_useful_[iph], l = ip_useless_[iph];
    uint64_t tot = u + l;
    if (tot < static_cast<uint64_t>(P.ip_filter_min_samples)) return 1;               // too few samples -> full
    uint64_t pct = static_cast<uint64_t>(u) * 100;
    if (pct >= static_cast<uint64_t>(P.ip_filter_threshold) * tot) return 1;           // >= soft -> full
    if (pct < static_cast<uint64_t>(P.ip_filter_threshold_hard) * tot)
      return P.ip_filter_trickle_hard ? static_cast<uint32_t>(P.ip_filter_trickle_hard) : 1; // near-dead -> harsh
    return P.ip_filter_trickle ? static_cast<uint32_t>(P.ip_filter_trickle) : 1;       // mid -> light
  }
  void ip_age_tick()
  {
    if ((++ip_age_ctr_ & ((uint64_t{1} << P.ip_filter_age_shift) - 1)) != 0) return;
    for (auto& v : ip_useful_) v >>= 1;
    for (auto& v : ip_useless_) v >>= 1;
    for (auto& v : ip_ev_) v >>= 1;
    for (auto& v : ip_untimely_) v >>= 1;
    for (auto& v : ip_pe_cnt_) v >>= 1; // PE-EMA warmth decays -> the gate only trusts RECENTLY-active IPs (phase-aware)
    for (auto& v : ip_bwd_useful_) v >>= 1;
    for (auto& v : ip_bwd_useless_) v >>= 1;
  }
  // Depth-throttle: an IP is UNTIMELY (right address, evicted before use) if a large fraction of its evicted-unused
  // prefetches are LATER re-demanded -> cap its DEPTH (shallower lands in time) instead of dropping volume.
  bool ip_is_untimely(uint32_t iph) const
  {
    if (ip_ev_.empty()) return false;
    uint32_t ev = ip_ev_[iph];
    if (ev < static_cast<uint32_t>(P.ip_filter_min_samples)) return false;
    return static_cast<uint64_t>(ip_untimely_[iph]) * 100 >= static_cast<uint64_t>(P.ip_untimely_thresh) * ev;
  }
  // Backward self-throttle: once an IP has sampled enough backward prefetches, keep firing the backward scan
  // ONLY if their usefulness clears the threshold. Warmup (few samples) allows backward so it can prove itself.
  bool bwd_is_bad(uint32_t iph) const
  {
    if (ip_bwd_useful_.empty()) return false;
    uint32_t u = ip_bwd_useful_[iph], l = ip_bwd_useless_[iph], tot = u + l;
    if (tot < static_cast<uint32_t>(P.bwd_useful_min_samples)) return false; // warmup -> allow
    return static_cast<uint64_t>(u) * 100 < static_cast<uint64_t>(P.bwd_useful_thresh) * tot;
  }
  bool pattern_is_bad(uint64_t pk) const // enough samples AND validated accuracy below the bad threshold
  {
    auto it = pat_val_.find(pk);
    if (it == pat_val_.end()) return false;
    uint32_t tot = it->second.u + it->second.n;
    if (tot < static_cast<uint32_t>(P.pv_min_samples)) return false;
    return 100u * it->second.u < static_cast<uint32_t>(P.pv_bad_pct) * tot;
  }
  // Apply a resolved sample's outcome to pattern validation (pat_val_ + confidence feedback) and the per-IP
  // filter counters. Shared by both sample-table policies. `block` is the resolved block; `s` the stored sample.
  void apply_pf_resolution(const pf_samp_t& s, uint64_t block, bool useful)
  {
    if (P.pattern_validate) {
      auto& v = pat_val_[s.pat]; if (useful) ++v.u; else ++v.n;
      // A proven-useless prediction is penalized toward silence (natural fall-through); a proven-useful one reinforced.
      if (P.pv_feed_confidence && s.bit >= 0) {
        pattern_type* e = pattern_tables_.at(0).find_key(s.pat);
        if (e != nullptr) {
          auto& pc = e->prediction_counter;
          std::size_t b = static_cast<std::size_t>(s.bit);
          if (b < pc.size()) {
            if (useful) pc[b] = std::min<uint64_t>(pc[b] + P.counter_up, 100);
            else pc[b] = pc[b] > static_cast<uint64_t>(P.pv_conf_penalty) ? pc[b] - P.pv_conf_penalty : 0;
          }
        }
      }
    }
    if (P.enable_ip_filter) {
      // A backward-scan prefetch credits ONLY the backward counters (throttle the backward scan without
      // also throttling that IP's healthy forward volume).
      if (s.bwd) {
        if (P.bwd_useful_gate) { if (useful) ++ip_bwd_useful_[s.iph]; else ++ip_bwd_useless_[s.iph]; } // gate-only tables
      } else if (useful) ++ip_useful_[s.iph];
      else {
        ++ip_useless_[s.iph];
        if (P.ip_filter_depth_throttle) { ++ip_ev_[s.iph];
          if (evicted_unused_.size() >= P.evicted_unused_cap) evicted_unused_.clear(); // crude bound (sampling tolerates loss)
          evicted_unused_[block] = s.iph; } // watch for re-demand
      }
      ip_age_tick();
    }
  }
  void pf_sample_issue(uint64_t block, uint64_t pk, uint32_t iph, int bit, bool backward = false)
  {
    if (!P.pattern_validate && !P.enable_ip_filter) return;
    pv_lfsr_ ^= pv_lfsr_ << 13; pv_lfsr_ ^= pv_lfsr_ >> 17; pv_lfsr_ ^= pv_lfsr_ << 5;
    const uint32_t div = (P.pv_sample_directmap && P.pv_adaptive_rate) ? pv_div_cur_
                       : (P.ip_sample_div ? P.ip_sample_div : 1);
    if (pv_lfsr_ % (div ? div : 1) != 0) return;
    pf_samp_t ns{pk, static_cast<uint16_t>(iph), static_cast<int8_t>(bit), backward,
                 static_cast<uint32_t>(block), static_cast<uint32_t>(cycle_), true};
    if (P.pv_sample_directmap) {
      // Fixed direct-mapped table: an in-flight incumbent survives ~pv_sample_evict_div collisions and is
      // reclaimed once older than pv_sample_ttl ops, so the sample's residency time tracks the prefetch
      // lifetime -- a much smaller table then resolves as many samples as the big clear-on-full map.
      pf_samp_t& slot = pf_sdm_[sdm_idx(block) & pf_sdm_mask_];
      const bool stale = slot.occ && (static_cast<uint32_t>(cycle_) - slot.stamp) > P.pv_sample_ttl;
      bool churn = false;                                      // displaced a LIVE (unresolved, non-stale) incumbent
      if (!slot.occ || stale) slot = ns;                       // free / aged-out -> take it
      else { pv_lfsr_ ^= pv_lfsr_ << 7;                        // occupied by a live incumbent -> evict only 1/N
             if (pv_lfsr_ % (P.pv_sample_evict_div ? P.pv_sample_evict_div : 1) == 0) { slot = ns; churn = true; } } // else drop new sample
      if (P.pv_adaptive_rate) {                                // self-tune div to hold the churn rate in [lo,hi]%
        ++pv_win_place_; if (churn) ++pv_win_churn_;
        if (pv_win_place_ >= P.pv_rate_window) {
          const uint32_t ch = 100u * pv_win_churn_ / pv_win_place_;
          if (ch > P.pv_churn_hi && pv_div_cur_ < P.pv_div_max) pv_div_cur_ <<= 1;        // too much churn -> sample less
          else if (ch < P.pv_churn_lo && pv_div_cur_ > P.pv_div_min) pv_div_cur_ >>= 1;   // headroom -> sample more
          pv_win_place_ = pv_win_churn_ = 0;
        }
      }
    } else {
      if (pf_sample_.size() >= P.pv_sample_cap) pf_sample_.clear(); // legacy crude bound; sampling tolerates loss
      pf_sample_[block] = ns;
    }
  }
  void pf_sample_resolve(uint64_t block, bool useful)
  {
    if (!P.pattern_validate && !P.enable_ip_filter) return;
    if (P.pv_sample_directmap) {
      pf_samp_t& slot = pf_sdm_[sdm_idx(block) & pf_sdm_mask_];
      if (!slot.occ || slot.tag != static_cast<uint32_t>(block)) return; // miss (displaced or never sampled)
      apply_pf_resolution(slot, block, useful);
      slot.occ = false;
    } else {
      auto it = pf_sample_.find(block);
      if (it == pf_sample_.end()) return;
      apply_pf_resolution(it->second, block, useful);
      pf_sample_.erase(it);
    }
  }
  // Delta-PHT usefulness self-throttle state.
  double delta_add_ema_ = 1.0;      // EMA of ADDITIVE fraction (timely hit on a baseline-miss block)
  double delta_unused_ema_ = 0.0;   // EMA of UNUSED-evict fraction (pollution proxy)
  double demand_hit_ema_ = 0.0;     // demand L2 hit-rate EMA (saturation / headroom signal for the delta throttle)
  int delta_psel_ = 512;            // set-dueling policy selector: high => delta reduces misses (followers adopt it)
  int walk_psel_ = 512;             // set-dueling policy selector for the walk (high => walk reduces misses)
  bool walk_sd_allow(uint64_t block) const // may the walk issue a prefetch into this block's set?
  {
    if (!P.walk_setduel) return true;
    int g = delta_sd_group(block);  // 0=OFF-leader (never walk), 1=ON-leader (always walk), 2=follower (PSEL)
    return g == 1 ? true : g == 0 ? false : (2 * walk_psel_ >= P.delta_pht_sd_max);
  }
  // L2 set-group for the delta set-duel: 0=OFF-leader (never delta), 1=ON-leader (always delta), 2=follower.
  int delta_sd_group(uint64_t block) const
  {
    // Hash the set index for leader selection so leaders are statistically representative (avoid the
    // pointer-chase set-index correlation that biases a raw set%period partition).
    uint64_t s = block & (P.l2_sets - 1);
    s = (s * 0x9E3779B97F4A7C15ULL) >> 40;
    uint64_t lo = s % static_cast<uint64_t>(P.delta_pht_sd_period);
    if (lo == 0) return 0;
    if (lo == static_cast<uint64_t>(P.delta_pht_sd_period / 2)) return 1;
    return 2;
  }
  bool delta_sd_allow(uint64_t block) const // may a delta prefetch fill this block's set?
  {
    if (!P.delta_pht_setduel) return true;
    int g = delta_sd_group(block);
    return g == 1 ? true : g == 0 ? false : (2 * delta_psel_ >= P.delta_pht_sd_max);
  }
  uint64_t delta_samples_ = 0;      // outcomes observed (warmup gate)
  uint64_t delta_explore_ctr_ = 0;  // periodic 1-deep probe counter when suppressed
  uint64_t dbg_delta_add_ = 0, dbg_delta_used_ = 0, dbg_delta_unused_ = 0;
  // Route a delta-PHT-issued block's terminal outcome into the throttle EMAs. is_use=true: first demand use
  // (additive = timely hit on a baseline miss). is_use=false: evicted without ever being used (pure waste).
  void note_delta_block(uint64_t block, bool additive, bool is_use)
  {
    if (!P.delta_pht_selfthrottle)
      return;
    region_type* r = regions_.probe_key(region_of(block));
    if (r == nullptr || r->delta_map.empty())
      return;
    uint64_t o = offset_of(block);
    if (!r->delta_map[o])
      return;
    r->delta_map[o] = false;
    const double a = 1.0 / static_cast<double>(1u << P.delta_pht_ema_shift);
    delta_add_ema_ += ((additive ? 1.0 : 0.0) - delta_add_ema_) * a;
    delta_unused_ema_ += ((is_use ? 0.0 : 1.0) - delta_unused_ema_) * a;
    ++delta_samples_;
    if (is_use) { ++dbg_delta_used_; if (additive) ++dbg_delta_add_; } else ++dbg_delta_unused_;
  }
  // Centered access-map neighborhood [off-r .. off+r] as the signature. Inclusive of off (an accessed line ->
  // never all-zero). Within-region (edge bits read 0); direction-agnostic. `spec` (optional) overlays the
  // speculative lookahead path so a hop's signature reflects the blocks we've already predicted this trigger --
  // the SPP-lookahead accumulation, applied to the spatial signature.
  uint64_t delta_sig(const region_type* r, int off, const uint8_t* spec = nullptr) const
  {
    const int bpr = static_cast<int>(blocks_per_region_);
    const int R = P.delta_sig_radius;
    uint64_t s = 0;
    for (int i = -R; i <= R; ++i) {
      int o = off + i;
      // center (i==0) is the access point itself -> forced on (inclusive history => never the 0-key; also
      // makes a speculative lookahead position, whose map bit is not yet set, hash consistently with training).
      uint32_t bit = (i == 0) ? 1u
                   : (o >= 0 && o < bpr && (r->access_map[o] || (spec && spec[o]))) ? 1u : 0u;
      s = (s << 1) | bit;
    }
    return s;
  }
  static uint64_t dpht_mix(uint64_t x) { x *= 0x9E3779B97F4A7C15ULL; x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ULL; x ^= x >> 32; return x; }
  dpht_entry* dpht_lookup(uint64_t sig, bool alloc)
  {
    if (dpht_.empty())
      return nullptr;
    uint64_t h = dpht_mix(sig);
    uint32_t tag = static_cast<uint32_t>(h >> 24) | 1u; // nonzero
    std::size_t set = (h % static_cast<uint64_t>(P.delta_pht_sets));
    dpht_entry* base = &dpht_[set * static_cast<std::size_t>(P.delta_pht_ways)];
    dpht_entry* victim = base; int minconf = 1 << 30;
    for (int w = 0; w < P.delta_pht_ways; ++w) {
      if (base[w].conf && base[w].tag == tag)
        return &base[w];
      if (base[w].conf < minconf) { minconf = base[w].conf; victim = &base[w]; }
    }
    if (!alloc)
      return nullptr;
    victim->tag = tag; victim->delta = 0; victim->conf = 0;
    return victim;
  }
  // Online: reinforce sig->delta (matches) or erode/replace (mismatch), SPP confidence-counter style.
  void dpht_train(uint64_t sig, int delta)
  {
    if (delta == 0 || delta > P.delta_pht_max || delta < -P.delta_pht_max)
      return;
    dpht_entry* e = dpht_lookup(sig, true);
    if (e == nullptr)
      return;
    if (e->conf && e->delta == static_cast<int8_t>(delta)) {
      if (e->conf < P.delta_pht_conf_max) ++e->conf;
    } else if (e->conf == 0) {
      e->delta = static_cast<int8_t>(delta); e->conf = 1;
    } else {
      --e->conf;
      if (e->conf == 0) { e->delta = static_cast<int8_t>(delta); e->conf = 1; }
    }
    ++dbg_dpht_train_;
  }

  // ---- ONLINE (Funnel-style) contrastive learning ----
  uint32_t online_lfsr_ = 0x2545F491u;
  // Reinforce one confirmed prediction bit (+counter_up) on pattern[ap,ctx], and decay
  // online_neg_samples LFSR-sampled unconfirmed bits (-counter_down) as contrastive negatives.
  void contrastive_incr(int order, uint64_t ap, uint64_t ctx, int pos_bit, uint64_t hist, bool negative = false)
  {
    if (pos_bit < 0)
      return;
    auto& tbl = (negative && P.separate_negative_tables) ? negative_pattern_tables_.at(order) : pattern_tables_.at(order);
    uint64_t key = pat_key(ap, ctx);
    pattern_type* e = tbl.find_key(key);
    if (e == nullptr) {
      e = &tbl.insert(pattern_type{key, &P});
      e->usefulness = global_usefulness_index_;
    }
    e->P = &P;
    auto& pc = e->prediction_counter;
    if (static_cast<std::size_t>(pos_bit) >= pc.size())
      return;
    const bool pp = P.pattern_perceptron && P.pp_bits() > 0;
    const long gate = P.online_theta_train; // 0 => always train (no surprise gate)
    for (int k = 0; k < P.online_neg_samples; ++k) {
      online_lfsr_ ^= online_lfsr_ << 13;
      online_lfsr_ ^= online_lfsr_ >> 17;
      online_lfsr_ ^= online_lfsr_ << 5;
      int c = static_cast<int>(online_lfsr_ % pc.size());
      if (c == pos_bit)
        continue;
      // gate on the actual scores (perceptron score in pp mode, else the raw counters)
      long spos = pp ? e->pp_score(pos_bit, hist) : static_cast<long>(pc[pos_bit]);
      long sneg = pp ? e->pp_score(c, hist) : static_cast<long>(pc[c]);
      if (gate > 0 && spos - sneg >= gate)
        continue; // already well-separated: don't over-train this confident pair
      if (pp) {
        e->pp_update(pos_bit, hist, +1);
        e->pp_update(c, hist, -1);
      } else {
        pc[pos_bit] = std::min<uint64_t>(pc[pos_bit] + P.counter_up, 100);
        pc[c] = pc[c] > P.counter_down ? pc[c] - P.counter_down : 0;
      }
    }
  }
  // Called on each demand access BEFORE the access is marked in the map: the map holds the
  // prior context, and this block is a confirmed positive for every signature that predicts
  // it (trigger at off-d predicts this block at bit ps-d, same convention as do_prefetch).
  void learn_on_access(uint64_t block, uint64_t ip = 0)
  {
    region_type* r = regions_.find_key(region_of(block));
    if (r == nullptr)
      return; // no context yet (first touch of this region)
    const region_type* rb = shadow_back(r);
    const region_type* rf = shadow_fwd(r);
    const uint64_t ctx = ctx_of(r);
    // PC context = this page's PREVIOUS accessing PC (r->pc_pht is not yet updated for the current
    // access -- add_to_pagemap runs after learn_on_access), matching what do_prefetch used at predict.
    const uint64_t ctx_pc = ctx | (P.pattern_pc_bits ? (static_cast<uint64_t>(r->pc_pht) & mask(P.pattern_pc_bits)) << P.pattern_context_bits : 0);
    const int off = static_cast<int>(offset_of(block));
    const int ps = static_cast<int>(P.pattern_size);
    const int bpr = static_cast<int>(blocks_per_region_);
    // FORWARD: a trigger at off-d (behind) predicts this block at bit ps-d.
    for (int d = 1; d <= ps; ++d) {
      auto [ap, neg] = patterns_at(r, rb, rf, off - d, false);
      (void)neg;
      // same input as do_prefetch reads at prediction: spatial history + the trigger block's PC hash
      int toff = off - d;
      uint64_t pcb = (toff >= 0 && toff < bpr) ? (r->block_ip[toff] & mask(static_cast<uint32_t>(P.pp_pc_bits))) : 0;
      uint64_t hist = spatial_hist(r, rb, rf, off - d, P.pp_hist_bits) | (pcb << P.pp_hist_bits);
      contrastive_incr(0, ap, ctx_pc, ps - d, hist);
    }
    // BACKWARD (online): a trigger AHEAD at off+d predicts this block (behind it) at bit ps-d in the negative
    // pattern -- the mirror of forward, into the negative table (or shared PHT). Otherwise the negative tables
    // are scrape-only and stay cold under online_learning, so do_negative never fires.
    if (P.neg_online_train) {
      bool train_bwd = true;
      if (P.neg_train_gated) {
        // PC mode: require the IP to STRONGLY stream backward (matches the fire gate) so forward streams'
        // transient backward steps never train the negative table. Momentum mode: any backward lean.
        if (P.neg_dir_pc) train_bwd = (static_cast<int>(ip_dir_[ip_hash(ip)]) <= -P.ip_direction_min);
        else              train_bwd = (r->momentum < 0);
      }
      if (train_bwd)
        for (int d = 1; d <= ps; ++d) {
          auto pat = patterns_at(r, rb, rf, off + d, true); // negative window at the ahead trigger
          int toff = off + d;
          uint64_t pcb = (toff >= 0 && toff < bpr) ? (r->block_ip[toff] & mask(static_cast<uint32_t>(P.pp_pc_bits))) : 0;
          uint64_t hist = spatial_hist(r, rb, rf, off + d, P.pp_hist_bits) | (pcb << P.pp_hist_bits);
          contrastive_incr(0, pat.second, ctx_pc, ps - d, hist, /*negative=*/true);
        }
    }
    // DELTA-PHT: the signature of the PREVIOUS access (r->last_block, not yet updated) predicts the signed
    // delta to THIS access. Direction-agnostic, so within-window/backward/near-neighbor targets are learnable.
    if (P.delta_pht) {
      int prev = static_cast<int>(r->last_block);
      dpht_train(delta_sig(r, prev), off - prev);
    }
  }

  // ----- scraping -----
  void scrape_region(uint64_t block)
  {
    if (P.online_learning)
      return; // online mode learns per-access; the delayed footprint harvest is disabled
    region_type* r = regions_.find_key(region_of(block));
    if (r == nullptr)
      return;
    const region_type* rb = shadow_back(r);
    const region_type* rf = shadow_fwd(r);
    const int ps = static_cast<int>(P.pattern_size);
    const int bpr = static_cast<int>(blocks_per_region_);
    const int ssz = shadow_size();
    const uint64_t ctx = ctx_of(r);
    r->since_last_scrape = 0;

    int start_forward = r->has_prev ? 0 : ps;
    int end_forward = r->has_next ? bpr : bpr - ps;
    // Original caps the window at the region size (bpr); the alignment fix
    // (scrape_full_window) extends it over the full shadow map.
    const int wcap = P.scrape_full_window ? ssz : bpr;
    if (r->momentum > P.forward_momentum_min) {
      for (int i = start_forward; i <= end_forward; ++i) {
        uint64_t ap = 0;
        for (int j = i; j < i + 2 * ps && j < wcap; ++j)
          ap = (ap << 1) | uint64_t(shadow_bit(r, rb, rf, j));
        int order = 0;
        for (int j = ps; j >= static_cast<int>(P.min_pattern_size); j /= 2) {
          uint64_t predicted = ap & mask(j);
          uint64_t accessed = (ap >> j) & mask(j);
          if (accessed != 0)
            increment_access_pattern(accessed, predicted, ctx, order, false);
          ++order;
        }
      }
    }
    if (P.do_negative && r->momentum < P.backward_momentum_min) {
      int start_backward = r->has_next ? (bpr + 2 * ps - 1) : (bpr + ps - 1);
      int end_backward = r->has_prev ? (2 * ps - 1) : (3 * ps - 1);
      for (int i = start_backward; i >= end_backward; --i) {
        uint64_t ap = 0;
        for (int j = i; j > i - 2 * ps && j >= 2 * ps - 1; --j)
          ap = (ap << 1) | uint64_t(shadow_bit(r, rb, rf, j));
        int order = 0;
        for (int j = ps; j >= static_cast<int>(P.min_pattern_size); j /= 2) {
          uint64_t predicted = ap & mask(j);
          uint64_t accessed = (ap >> j) & mask(j);
          if (accessed != 0)
            increment_access_pattern(accessed, predicted, ctx, order, true);
          ++order;
        }
      }
    }
    if (P.mark_after_scrape)
      for (std::size_t i = 0; i < r->access_map.size(); ++i)
        if (r->access_map[i]) {
          if (P.clear_after_scrape)
            r->access_map[i] = false;
          r->prefetch_map[i] = !P.clear_filter_after_scrape;
        }
    r->last_access_time = cycle_;
  }

  int get_momentum(uint64_t block)
  {
    region_type* r = regions_.find_key(region_of(block));
    return r ? r->momentum : 0;
  }

  static int do_4_bit_mult(int a, int b) { return (a * b) >> 4; }
  bool use_pattern_confidence() const { return P.adaptive_usefulness && region_lifespan_index_ >= static_cast<int>(P.pattern_usefulness_cutoff); }

  int pattern_usefulness(uint64_t access_pattern, uint64_t ctx, int order)
  {
    pattern_type* e = pattern_tables_.at(order).find_key(pat_key(access_pattern, ctx));
    return e ? static_cast<int>(e->usefulness) : global_usefulness_index_;
  }

  int set_prefetch_degree(uint64_t access_pattern, uint64_t ctx, int order, int prev_usefulness)
  {
    int usefulness;
    if (P.adaptive_usefulness)
      usefulness = use_pattern_confidence() ? pattern_usefulness(access_pattern, ctx, order) : global_usefulness_index_;
    else if (P.global_or_pattern_usefulness)
      usefulness = global_usefulness_index_;
    else
      usefulness = pattern_usefulness(access_pattern, ctx, order);
    usefulness = std::clamp(usefulness, 0, 15);
    int nu = std::clamp(do_4_bit_mult(usefulness, prev_usefulness), 0, 15);
    if (P.enable_bw_feedback) {
      // Original law: bw_util = f(DRAM utilization x LLC miss rate); throttle degree.
      int missidx = std::min(15, static_cast<int>(miss_ema_ * 15.0 + 0.5));
      int bw_util = std::min(15, do_4_bit_mult(sink_->dram_bw_index(), missidx) + 1);
      int bwdeg = static_cast<int>(P.prefetch_degrees_bw[bw_util]);
      if (P.bw_mult) {
        nu = std::min(15, do_4_bit_mult(nu, bwdeg) + 1);
        current_pf_degree_ = std::max<int64_t>(0, P.prefetch_degrees_usefulness[nu]);
        return nu;
      }
      // Floor at 1: bandwidth feedback throttles the degree but NEVER to zero -- a
      // complete gate would stop the usefulness feedback and be unrecoverable.
      current_pf_degree_ = std::max<int64_t>(1, P.prefetch_degrees_usefulness[nu] - bwdeg);
      return std::min(nu + 1, 15);
    }
    current_pf_degree_ = std::max<int64_t>(0, P.prefetch_degrees_usefulness[nu]);
    return std::min(nu + 1, 15);
  }

  // Perceptron history = extra SPATIAL access-map bits AROUND the signature window, EXCLUDING the
  // ps bits the index already locks in place (weighting those would be dead -- they are constant
  // within an entry). H/2 bits immediately before the trigger + the rest just beyond the ps-bit
  // signature window. Extends the same spatial pattern the table already indexes.
  uint64_t spatial_hist(const region_type* r, const region_type* rb, const region_type* rf, int64_t offset, int H) const
  {
    if (r == nullptr || H <= 0)
      return 0;
    const int ssz = shadow_size();
    const int ps = static_cast<int>(P.pattern_size);
    uint64_t h = 0;
    int bit = 0;
    const int hb = H / 2; // backward bits: positions [offset-hb+1 .. offset], just before the signature
    for (int i = static_cast<int>(offset) - hb + 1; i <= static_cast<int>(offset) && bit < H; ++i, ++bit)
      if (i >= 0 && i < ssz && shadow_bit(r, rb, rf, i))
        h |= (uint64_t{1} << bit);
    for (int i = static_cast<int>(offset) + ps + 1; bit < H; ++i, ++bit) // forward, beyond the signature
      if (i >= 0 && i < ssz && shadow_bit(r, rb, rf, i))
        h |= (uint64_t{1} << bit);
    return h;
  }

  // ----- main prefetch generation -----
  // Cross-page PHT key. SPATIAL = the signature the PREDECESSOR fed the main PHT on its FINAL prediction (the
  // pattern_size window at its last accessed offset) -- generalizes where the full map would not. IP = the PC
  // crossing into this region. Both are XOR-composed with the entry offset. cross_page_key picks which.
  uint64_t xkey_of(uint64_t prev_rgn, uint64_t entry_off, uint64_t ip)
  {
    uint64_t k = 0;
    if (P.cross_page_key == 0 || P.cross_page_key == 2) {
      region_type* pr = regions_.probe_key(prev_rgn);
      uint64_t sig = pr ? patterns_at(pr, shadow_back(pr), shadow_fwd(pr), static_cast<int64_t>(pr->last_block), false).first : 0;
      k ^= sig * 0x9E3779B97F4A7C15ull;
    }
    if (P.cross_page_key == 1 || P.cross_page_key == 2)
      k ^= (ip * 0xD6E8FEB86659FD93ull) + 0x2545F4914F6CDD1Dull; // crossing PC
    k ^= (entry_off + 1) * 0xD1B54A32D192ED03ull;
    k ^= k >> 29;
    return k ? k : 1;
  }
  xrow& xrow_at(uint64_t key)
  {
    xrow& r = xpht_[key];
    if (r.cnt.empty()) r.cnt.assign(static_cast<std::size_t>(blocks_per_region_), 0);
    return r;
  }

  // Cold-start delta table (PC-keyed): learn the dominant first-delta after a region entry, per entry PC.
  void cs_train(uint8_t eip, int delta)
  {
    if (cs_delta_.empty() || delta == 0 || delta > 63 || delta < -63)
      return;
    std::size_t i = eip & (cs_delta_.size() - 1);
    if (cs_cnt_[i] == 0) { cs_delta_[i] = static_cast<int8_t>(delta); cs_cnt_[i] = 1; }
    else if (cs_delta_[i] == static_cast<int8_t>(delta)) { if (cs_cnt_[i] < 15) ++cs_cnt_[i]; }
    else if (--cs_cnt_[i] == 0) { cs_delta_[i] = static_cast<int8_t>(delta); cs_cnt_[i] = 1; }
  }
  // Confident learned startup delta for this trigger PC; false => don't cold-start (unpredictable PC, avoid pollution).
  bool cs_lookup(uint64_t ip, int& delta) const
  {
    if (cs_delta_.empty()) return false;
    std::size_t i = ip_hash(ip) & (cs_delta_.size() - 1);
    if (cs_cnt_[i] >= 2) { delta = cs_delta_[i]; return true; }
    return false;
  }

  void do_prefetch(uint64_t addr, uint64_t ip)
  {
    // addr's region (used for momentum and the prefetch-map filter); not
    // structurally mutated during this call, so the pointer stays valid.
    region_type* r_addr = regions_.find_key(region_of(addr));
    int momentum = r_addr ? r_addr->momentum : 0;
    // PC context = this page's accessing-PC PHT index (stable per-region; consistent with learn_on_access).
    const uint64_t pc_part = P.pattern_pc_bits ? ((r_addr ? static_cast<uint64_t>(r_addr->pc_pht) : 0) & mask(P.pattern_pc_bits)) << P.pattern_context_bits : 0;
    // per-IP dominant stride direction. Computed for ip_direction (forward-suppression) OR neg_dir_pc (backward
    // gate) -- decoupled so neg_dir_pc can gate the backward scan WITHOUT ip_direction's forward-suppression.
    int ipd = (P.ip_direction || P.neg_dir_pc) ? ip_dir_[ip_hash(ip)] : 0;
    uint64_t addr_region = region_of(addr); // region-entry granularity
    uint64_t addr_page = page_of(addr);      // 4 KB page (squash boundary)
    int pf_issued = 0;
    // IP-FILTER throttle (faithful): a low-usefulness trigger IP is throttled. Action depends on WHY it's low:
    // UNTIMELY (right addr, evicted-before-use) -> cap DEPTH (shallower lands in time); TRULY-BAD -> volume trickle.
    int ip_depth_cap = 1 << 20; // no cap by default
    // The perceptron filter SUPERSEDES the ip-filter throttle (its counters stay live as perceptron features).
    if (P.enable_ip_filter && !P.enable_perceptron_filter && !ip_useful_.empty()) {
      uint32_t tiph = iphash(ip);
      if (P.ip_filter_depth_throttle && ip_is_untimely(tiph)) {
        uint32_t div = ip_trickle_div(tiph); // reuse the bands: harsher band -> shallower cap
        ip_depth_cap = (div >= static_cast<uint32_t>(P.ip_filter_trickle_hard) ? P.ip_depth_min : P.ip_depth_mid);
      } else {
        uint32_t div = ip_trickle_div(tiph);
        if (div > 1 && (++ip_gate_ctr_[tiph] % div != 0)) { ++dbg_ip_throttled_; return; }
      }
    }
    // Precise pattern validation: is THIS trigger's e2e pattern proven bad by sampling? If so, suppress e2e's
    // forward path and fall through to the walk. trig_pk = the trigger pattern key we sample every e2e prefetch to.
    uint64_t trig_pk = 0; bool trig_bad = false;
    if (P.pattern_validate && r_addr) {
      uint64_t tsig = patterns_at(r_addr, shadow_back(r_addr), shadow_fwd(r_addr), static_cast<int64_t>(offset_of(addr)), false).first;
      trig_pk = pat_key(tsig, ctx_of(r_addr) | pc_part);
      trig_bad = pattern_is_bad(trig_pk);
      if (trig_bad) ++dbg_pv_bad_; else ++dbg_pv_good_;
    }
    // COLD-START (prototype): a fresh region entry has an unlearned pattern, so the +1/+2 page-entry-startup
    // accesses miss. Kick a short forward stream (fixed +1..+N) so they're prefetched. Residency-filtered +
    // page-bounded; runs before the normal pattern scan below. PC-learned deltas are a follow-up.
    if (P.enable_cold_start && region_just_created_) {
      int cd = 1; bool go = true;
      if (P.cold_start_pc) go = cs_lookup(ip, cd); // learned per-PC delta + confidence gate (skip unpredictable PCs)
      for (int k = 1; go && k <= static_cast<int>(P.cold_start_degree); ++k) {
        int64_t sstep = static_cast<int64_t>(addr) + static_cast<int64_t>(cd) * k;
        if (sstep < 0) break;
        uint64_t step = static_cast<uint64_t>(sstep);
        if (page_of(step) != addr_page) break; // cross-page squash (also stops a backward run at the page edge)
        if (r_addr && r_addr->prefetch_map[offset_of(step)]) continue;
        if (sink_->issue_prefetch(step, true, false, 1.0, 0) && r_addr) mark_prefetch(r_addr, offset_of(step), step);
        ++prefetches_issued;
      }
    }
    const int ps = static_cast<int>(P.pattern_size);
    // L2-vs-LLC placement limit: dynamic = free MSHR/PQ headroom (orig SPPAM's LLC-overflow), else
    // the fixed degree (fill L2 aggressively). The adaptive variant only reverts to the LLC-overflow
    // policy when the region map is thrashing hard (region_thrash_ema_ >= threshold): extreme region
    // thrash is the fingerprint of the dense over-prediction that floods L2 (e.g. cactus, ema~0.92),
    // while the workloads that WANT aggressive L2 fill sit well below the threshold.
    const bool overflow_llc = P.dynamic_l2_fill &&
        (!P.dynamic_l2_fill_adaptive || region_thrash_ema_ >= P.dynamic_l2_fill_thrash_min);
    // Set-duel walks an adaptive L2 fill DEPTH (shallow/timely prefetches stay in L2, deep/speculative
    // tail overflows to LLC) -- the correct throttle action, vs the earlier random per-prefetch redirect.
    // PE-ramp spills the dense tail to LLC at the MSHR-availability rate (pf_free_space) for accurate
    // DRAM-bound streams, same overflow mechanism as dynamic_l2_fill.
    const bool spill_llc = sink_->pe_ramp_active() || overflow_llc;
    const int l2_fill_limit = P.enable_set_duel ? sink_->sd_l2_limit()
                                                : (spill_llc ? sink_->pf_free_space() : static_cast<int>(P.prefetch_to_l2_degree));

    // CROSS-PAGE: on a fresh region entry, predict THIS region's footprint from the predecessor-keyed table
    // (shallow -- just issue the predicted offsets). This has context exactly when the per-region PHT has none.
    if (P.enable_cross_page && region_just_created_ && r_addr && r_addr->xkey_set) {
      ++xpage_entries_;
      auto it = xpht_.find(r_addr->xkey);
      if (it != xpht_.end() && it->second.occ >= P.cross_page_min_occ) {
        const xrow& row = it->second;
        const double occ = static_cast<double>(row.occ);
        const uint64_t rbase = addr_region << region_shift_;
        int issued = 0;
        for (std::size_t o = 0; o < row.cnt.size() && issued < static_cast<int>(P.cross_page_degree); ++o) {
          if (static_cast<double>(row.cnt[o]) / occ < P.cross_page_conf) continue;
          uint64_t step = rbase | static_cast<uint64_t>(o);
          if (r_addr->prefetch_map[o]) continue; // already resident/prefetched (incl. the entry demand)
          if (sink_->issue_prefetch(step, true, false, 1.0, 0)) mark_prefetch(r_addr, o, step);
          ++prefetches_issued; ++issued;
        }
        if (issued) { ++xpage_fire_; xpage_issued_ += issued; }
      }
    }

    // DELTA-PHT: from THIS access's spatial context, predict the signed delta to the next access and prefetch it.
    // Reaches the within-window / backward / near-neighbor targets the position bitmap structurally cannot emit.
    if (P.delta_pht && r_addr) {
      int cur = static_cast<int>(offset_of(addr));
      // Usefulness self-throttle: below the accuracy floor, cut the speculative depth to a periodic 1-deep probe
      // (keeps sampling so the EMA can recover) instead of firing the full divergent lookahead.
      int eff_degree = P.delta_pht_degree;
      // Per-block usefulness can't see the harm (delta blocks look additive+used even where delta is net-negative);
      // the harm is cross-prefetcher displacement on ALREADY-SATURATED traces. Gate on coverage headroom: when the
      // demand hit-rate is high (little left to cover), extra delta volume only churns -> suppress to a probe.
      if (P.delta_pht_selfthrottle && delta_samples_ >= static_cast<uint64_t>(P.delta_pht_warmup)
          && demand_hit_ema_ > P.delta_pht_max_hitrate)
        eff_degree = (P.delta_pht_explore > 0 && (++delta_explore_ctr_ % P.delta_pht_explore == 0)) ? 1 : 0;
      std::fill(dpht_spec_.begin(), dpht_spec_.end(), 0);
      dpht_spec_[cur] = 1; // seed the speculative path at the real access
      double path_conf = 1.0;
      for (int k = 0; k < eff_degree; ++k) {
        dpht_entry* e = dpht_lookup(delta_sig(r_addr, cur, dpht_spec_.data()), false);
        if (e == nullptr || e->conf < P.delta_pht_conf_min)
          break;
        int nd = cur + e->delta;
        if (nd < 0 || nd >= static_cast<int>(blocks_per_region_))
          break;
        uint64_t step = (addr_region << region_shift_) | static_cast<uint64_t>(nd);
        if (page_of(step) != addr_page)
          break;
        if (dpht_spec_[nd]) break; // path revisits a predicted block -> stop (avoid a delta cycle)
        dpht_spec_[nd] = 1;        // extend the speculative trajectory (issued or squashed)
        // Set-duel: never fill delta into OFF-leader sets; followers gated by PSEL. Path still advances (above).
        // Perceptron final gate (engine 2 = delta): drop is learned over the DELTA signature that predicted this block.
        const bool perc_drop = P.enable_perceptron_filter
                             && !perc_keep(step, 2, k, ip, static_cast<int>(e->conf), delta_sig(r_addr, cur, dpht_spec_.data()));
        if (delta_sd_allow(step) && !r_addr->prefetch_map[nd] && !perc_drop) {
          const bool placed = sink_->issue_prefetch(step, true, false, 1.0, 0);
          if (placed) {
            mark_prefetch(r_addr, nd, step);
            if (P.enable_perceptron_filter) perc_note_issue(step);
            if (P.delta_pht_selfthrottle) { // tag the block as delta-sourced for usefulness attribution
              if (r_addr->delta_map.empty()) r_addr->delta_map.assign(blocks_per_region_, false);
              r_addr->delta_map[nd] = true;
            }
          }
          ++prefetches_issued; ++dbg_dpht_issued_;
        }
        cur = nd; // lookahead from the predicted position; signature now carries the path (dpht_spec_)
        // SPP-style path-confidence decay: stop extending once the trajectory gets too speculative.
        if (P.delta_pht_path_conf > 0.0) {
          path_conf *= static_cast<double>(e->conf) / static_cast<double>(P.delta_pht_conf_max);
          if (path_conf < P.delta_pht_path_conf) break;
        }
      }
    }

    // WALK-ACCUMULATE forward prediction (replaces the end-over-end lookahead when enabled): walk one nearest
    // predicted offset per step, re-forming the signature from map+speculative-path; accumulate per-offset
    // confidence and issue an offset only once its accumulated evidence crosses a distance-scaled threshold.
    if (P.walk_accumulate && r_addr && (momentum > P.forward_momentum_min)) {
      const int ps = static_cast<int>(P.pattern_size);
      const int bpr = static_cast<int>(blocks_per_region_);
      const region_type* wb = shadow_back(r_addr);
      const region_type* wf = shadow_fwd(r_addr);
      const uint64_t wctx = ctx_of(r_addr) | pc_part;
      bool run_walk = true;
      if (P.pattern_validate) run_walk = trig_bad; // proper fall-through: walk only where the trigger pattern is proven bad
      else if (P.walk_fallthrough) { // SELECTOR: run role-2 where role-1 is WEAK -- silent (no learned pattern) or its
        // trigger pattern's usefulness is low. e2e can be confidently WRONG (mcf: has a pattern, ~30% coverage), so
        // "silent" alone misses it; the usefulness of the trigger pattern is what says role-1 is failing here.
        uint64_t tsig = patterns_at(r_addr, wb, wf, static_cast<int64_t>(offset_of(addr)), false).first;
        bool silent = (get_prefetch_pattern(tsig, wctx, 0, false, 0).first == 0);
        run_walk = silent || (pattern_usefulness(tsig, wctx, 0) < P.walk_ft_usefulness);
      }
      if (run_walk) {
      std::fill(walk_spec_.begin(), walk_spec_.end(), 0);
      std::fill(walk_issued_.begin(), walk_issued_.end(), 0);
      std::fill(walk_acc_.begin(), walk_acc_.end(), 0);
      const int trig = static_cast<int>(offset_of(addr));
      // Usefulness throttle (mirror e2e's current_pf_degree_): scale the walk's degree by global usefulness so an
      // unreliable workload gets a shallower, conservative path instead of a full-degree spray.
      int wdeg = P.walk_degree;
      if (P.walk_usefulness_throttle)
        wdeg = std::max(1, (P.walk_degree * (global_usefulness_index_ + 1)) / 16);
      int cur = trig, wissued = 0;
      walk_spec_[cur] = 1;
      for (int depth = 0; depth < P.walk_max_depth && cur >= 0 && cur < bpr && wissued < wdeg; ++depth) {
        uint64_t sig = patterns_at(r_addr, wb, wf, cur, false, walk_spec_.data()).first;
        pattern_type* e = pattern_tables_.at(0).find_key(pat_key(sig, wctx));
        if (e == nullptr) break;
        e->P = &P;
        int nearest = -1, best = -1; long best_conf = -1;
        for (int d = 1; d <= ps; ++d) {                 // offset distance d -> prediction_counter[ps-d]
          int a = cur + d;
          if (a >= bpr) break;
          long conf = static_cast<long>(e->prediction_counter[ps - d]);
          if (conf < static_cast<long>(P.min_confidence_to_prefetch)) continue;
          if (nearest < 0) nearest = d;
          walk_acc_[a] += 1;                             // one CORROBORATION (a predicting walk-step) for offset a
          // MOST-CORROBORATED path: pick the strongest offset predicted this step (acc, then confidence, then nearest)
          if (best < 0 || walk_acc_[a] > walk_acc_[best] || (walk_acc_[a] == walk_acc_[best] && conf > best_conf))
            { best = a; best_conf = conf; }
        }
        if (nearest < 0) {
          // default-prediction fallback (next-line), matching e2e: no learned pattern but the trailing
          // default_pattern blocks are set -> predict +1 and advance (fires immediately, like e2e).
          if (P.use_default_prediction && (sig & P.default_pattern) == P.default_pattern && cur + 1 < bpr) {
            int a = cur + 1;
            uint64_t step = (addr_region << region_shift_) | static_cast<uint64_t>(a);
            if (page_of(step) == addr_page && !walk_issued_[a]) {
              walk_issued_[a] = 1;
              if (walk_sd_allow(step) && !r_addr->prefetch_map[a]) {
                if (sink_->issue_prefetch(step, true, false, 1.0, 0)) mark_prefetch(r_addr, a, step);
                ++prefetches_issued; ++dbg_walk_issued_; ++wissued;
              }
            }
            walk_spec_[a] = 1; cur = a; ++dbg_walk_steps_;
            continue;
          }
          break;
        }
        // issue offsets whose corroborations cross the threshold: grows with distance from the trigger, but CAPPED,
        // and RELIEVED for offsets reached via a big jump ahead of the current position (fewer chances to corroborate).
        for (int a = trig + 1; a < bpr && wissued < wdeg; ++a) {
          if (walk_issued_[a] || walk_acc_[a] == 0) continue;
          long req10 = static_cast<long>(P.walk_thresh_base) * 10 + static_cast<long>(P.walk_thresh_slope) * (a - trig - 1);
          long cap10 = static_cast<long>(P.walk_thresh_cap) * 10;
          if (req10 > cap10) req10 = cap10;
          req10 -= static_cast<long>(P.walk_jump_relief) * 10 * (a - cur - 1 > 0 ? a - cur - 1 : 0); // big reach -> less required
          if (req10 < 10) req10 = 10;                    // always >= 1 corroboration
          if (static_cast<long>(walk_acc_[a]) * 10 < req10) continue;
          uint64_t step = (addr_region << region_shift_) | static_cast<uint64_t>(a);
          if (page_of(step) != addr_page) continue;
          walk_issued_[a] = 1;
          if (walk_sd_allow(step) && !r_addr->prefetch_map[a]) { // set-duel: never fill walk into OFF-leader sets
            if (sink_->issue_prefetch(step, true, false, 1.0, 0)) mark_prefetch(r_addr, a, step);
            ++prefetches_issued; ++dbg_walk_issued_; ++wissued;
          }
        }
        int adv = (P.walk_advance == 1 && best > cur) ? (best - cur) : nearest;
        cur += adv;                                      // advance: nearest, or the most-corroborated offset
        if (cur < bpr) walk_spec_[cur] = 1;
        ++dbg_walk_steps_;
      }
      } // run_walk
    }

    // Cache the region + shadows for get_patterns across scan steps in one page.
    uint64_t cache_pn = ~uint64_t{0};
    region_type* cr = nullptr;
    const region_type* crb = nullptr;
    const region_type* crf = nullptr;
    uint64_t cctx = 0;
    uint64_t chist = 0; // perceptron history input (pp_hist_bits of the region's pat_ctx)

    for (int dir : {1, -1}) {
      bool forward = (dir == 1);
      // walk_accumulate COEXISTS with the e2e position-bitmap (role-2 path-follower alongside role-1), unless
      // walk_replace is set (walk takes over the forward path entirely, for A/B measurement).
      if (forward && P.walk_accumulate && P.walk_replace) continue;
      if (forward && P.pattern_validate && !P.pv_feed_confidence && trig_bad) continue; // gate-mode: suppress e2e (walk took over)
      if (forward && !P.scan_forward) continue;
      if (!forward && !P.scan_backward) continue;
      if (!forward && !P.do_negative) continue;
      // IP-direction: a confident trigger-IP prefetches ONLY its dominant direction -- skip the opposite
      // scan (the wrong-way pollution momentum's no-op gate lets through). Ambiguous IPs still scan both.
      if (P.ip_direction) {
        if (forward && ipd <= -P.ip_direction_min) continue;
        if (!forward && ipd >= P.ip_direction_min) continue;
      }
      uint64_t scan = forward ? P.scan_distance_forward : P.scan_distance_backward;
      // Backward normally starts at sstep=1 (delta -1 was covered by a forward scan elsewhere); with online
      // backward training we start at 0 so the trigger itself predicts its immediate -1 neighbour.
      for (int sstep = (forward || P.neg_online_train) ? 0 : 1; sstep < static_cast<int>(scan); ++sstep) {
        int current_usefulness = use_pattern_confidence() ? 15 : global_usefulness_index_;
        uint64_t pf_base = addr + static_cast<uint64_t>(dir * sstep);
        uint64_t bpn = region_of(pf_base);
        if (bpn != cache_pn) {
          cr = regions_.find_key(bpn);
          crb = cr ? shadow_back(cr) : nullptr;
          crf = cr ? shadow_fwd(cr) : nullptr;
          cctx = ctx_of(cr);
          cache_pn = bpn;
        }
        auto [pos_ap, neg_ap] = patterns_at(cr, crb, crf, static_cast<int64_t>(offset_of(pf_base)), !forward);
        // perceptron input (offset-dependent): spatial history in the low bits, the trigger
        // block's PC hash in the high bits (berti-touched blocks carry PC=0 -> learned off).
        {
          uint64_t pcb = cr ? (cr->block_ip[offset_of(pf_base)] & mask(static_cast<uint32_t>(P.pp_pc_bits))) : 0;
          chist = spatial_hist(cr, crb, crf, static_cast<int64_t>(offset_of(pf_base)), P.pp_hist_bits) | (pcb << P.pp_hist_bits);
        }
        // PC-contextualized pattern key: fold this page's accessing-PC index above the region context.
        uint64_t cctx_pc = cctx | pc_part;
        uint64_t ap = forward ? pos_ap : neg_ap;
        bool neg = !forward;
        // Direction gate. Default: spatial momentum. With neg_dir_pc, the BACKWARD scan is gated by the
        // per-IP direction (ip_dir_) instead -- fire backward only when the trigger IP streams backward.
        bool skip_dir;
        if (forward)
          skip_dir = (momentum <= P.forward_momentum_min);
        else if (P.neg_online_train && P.neg_dir_pc)
          // fire backward only where the trigger IP STRONGLY streams backward AND (if enabled) backward has proven useful for it
          skip_dir = (ipd > -P.ip_direction_min) || (P.bwd_useful_gate && bwd_is_bad(iphash(ip)));
        else
          skip_dir = (momentum >= P.backward_momentum_min);
        if (skip_dir)
          continue;
        if (!forward) ++dbg_bwd_scan_;

        int order = 0;
        for (int i = ps; i >= static_cast<int>(P.min_pattern_size); i /= 2) {
          auto [pp, pv] = get_prefetch_pattern(ap, cctx_pc, order, neg, chist);
          if (neg && pp != 0) ++dbg_bwd_pred_;
          if (pp == 0 && !pv && P.use_default_prediction && ((i >> 1) < static_cast<int>(P.min_pattern_size))) {
            if ((ap & P.default_pattern) == P.default_pattern) { pp = P.effective_default_prediction(); ap = P.default_pattern; pv = true; }
          }
          bool continue_outer = false;
          int lookaheads = 0, lookahead_offset = 0;
          while (true) {
            current_usefulness = std::clamp(do_4_bit_mult(do_4_bit_mult(static_cast<int>(P.lookahead_conf_factor), set_prefetch_degree(ap, cctx_pc, order, current_usefulness)), current_usefulness), 0, 15);
            // PE-ramp: accurate DRAM-bound stream -> deeper per-trigger degree (dense tail spills to LLC).
            if (P.enable_pe_ramp && sink_->pe_ramp_active())
              current_pf_degree_ = std::min<int64_t>(P.pe_ramp_degree_cap, current_pf_degree_ + P.pe_ramp_degree_add);
            if (P.prob_drop_prefetches && global_usefulness_index_ < 8) {
              ++dbg_dchk_[current_usefulness & 15];
              if ((cycle_ % 128) < static_cast<uint64_t>(P.prefetch_drop_chance_usefulness[current_usefulness])) {
                ++dbg_ddrop_[current_usefulness & 15];
                break;
              }
            }
            if (pp == 0 && !pv) { continue_outer = true; break; }
            if (pp == 0) break;
            for (int j = 0; j < i; ++j) {
              if (pf_issued >= current_pf_degree_) break;
              if (pp & (uint64_t{1} << (i - 1 - j))) {
                uint64_t step = forward ? pf_base + static_cast<uint64_t>(j + 1 + lookahead_offset) : pf_base - static_cast<uint64_t>(j + 1 + lookahead_offset);
                // 4 KB physical page is a hard prefetch boundary (cross-page squash).
                if (page_of(step) != addr_page)
                  continue;
                // Region prefetch-map filter/mark applies within the trigger's
                // region entry; a step in a different (sub-page) region uses the
                // general path.
                bool same_region = (region_of(step) == addr_region);
                uint64_t soff = offset_of(step);
                bool already = same_region ? (r_addr ? r_addr->prefetch_map[soff] : false) : check_pagemap(step, true);
                if (!already) {
                  // PERCEPTRON final gate (prototype): learned keep/drop over all throttle signals. Runs AFTER
                  // the residency filter (above) and supersedes the ip-filter/depth throttle. engine 0=SPPAM-fwd, 1=bwd.
                  if (P.perc_profile) perc_raw_ap_ = ap; // stash the raw spatial bitmap for the opposite-direction methodology-check feature
                  if (P.enable_perceptron_filter && !perc_keep(step, forward ? 0 : 1, lookaheads, ip, current_usefulness, pat_key(ap, cctx_pc)))
                    continue; // dropped (and not chosen for exploration); sig = the SPPAM pattern key that predicted this block

                  bool fill_l2 = (pf_issued < l2_fill_limit);
                  if (!fill_l2 && check_llc_pagemap(step)) {
                    // already in LLC map -> filter
                  } else {
                    // benefit = usefulness/15 (expected usefulness ~ coverage/bandwidth).
                    bool placed = sink_->issue_prefetch(step, fill_l2, /*from_spp=*/false, /*benefit=*/current_usefulness / 15.0,
                    /*gen_tag=*/ ((static_cast<uint32_t>(lookaheads > 15 ? 15 : lookaheads)) << 1)
                                 | ((static_cast<uint32_t>(order > 7 ? 7 : order)) << 5)
                                 | ((static_cast<uint32_t>(sstep > 7 ? 7 : sstep)) << 9)); // src=SPPAM, depth,order,scan
                    if (P.enable_perceptron_filter && placed && fill_l2) perc_note_issue(step); // dense training snapshot
                    if (fill_l2) {
                      if (placed) { // MARK residency only when the prefetch actually filled L2 (no stale bit on drop)
                        if (same_region && r_addr) mark_prefetch(r_addr, soff, step);
                        else { add_to_pagemap(step, true); cache_pn = ~uint64_t{0}; r_addr = regions_.find_key(addr_region); }
                      }
                    } else
                      add_to_llc_pagemap(step);
                    if (!forward) ++dbg_bwd_issued_;
                    if (placed) // validate the TRIGGER pattern; bit is precise only for the forward depth-0 prediction; tag direction
                      pf_sample_issue(step, trig_pk, iphash(ip), (forward && lookaheads == 0) ? (i - 1 - j) : -1, /*backward=*/!forward);
                    ++pf_issued;
                    ++prefetches_issued;
                    if (pf_issued >= current_pf_degree_) break;
                  }
                }
              }
            }
            if (P.do_lookahead && pf_issued < current_pf_degree_) {
              auto la = get_prefetch_pattern(pp, cctx_pc, order, neg);
              ap = pp; pp = la.first; pv = la.second;
              ++lookaheads; lookahead_offset += i;
              int eff_depth = static_cast<int>(P.lookahead_depth) + ((P.enable_pe_ramp && sink_->pe_ramp_active()) ? static_cast<int>(P.pe_ramp_lookahead_add) : 0);
              eff_depth = std::min(eff_depth, sink_->ip_depth_cap()); // per-IP: shallower as usefulness degrades
              eff_depth = std::min(eff_depth, ip_depth_cap);          // ported depth-throttle: cap untimely trigger IPs
              if (lookaheads > eff_depth || current_usefulness < static_cast<int>(P.lookahead_conf_cutoff)) break;
              if (pp != 0) ++total_lookaheads;
            } else
              break;
          }
          if (!continue_outer) break;
          ++order;
        }
      }
    }
  }

  // ----- usefulness feedback -----
  void modify_pattern_usefulness(uint64_t block, bool useful)
  {
    uint64_t pn = region_of(block);
    int off = static_cast<int>(offset_of(block));
    region_type* r = regions_.find_key(pn);
    if (r == nullptr) {
      if (!useful) regions_not_found_++;
      sample_region_lifespan();
      return;
    }
    if (!useful) regions_found_++;
    sample_region_lifespan();
    if (!r->prefetch_map[static_cast<std::size_t>(off)])
      return;
    const region_type* rb = shadow_back(r);
    const region_type* rf = shadow_fwd(r);
    const int ps = static_cast<int>(P.pattern_size);
    const uint64_t ctx = ctx_of(r);
    int order = 0;
    for (int i = ps; i >= static_cast<int>(P.min_pattern_size); i /= 2) {
      for (int o = 1; o <= i; ++o) {
        if (r->momentum > P.forward_momentum_min) {
          int toff = off - o;
          if (toff >= 0 && toff <= static_cast<int>(r->access_map.size())) {
            auto [pos, neg] = patterns_at(r, rb, rf, toff, false);
            (void)neg;
            if (pos != 0) {
              pattern_type* e = pattern_tables_.at(order).find_key(pat_key(pos, ctx));
              if (e != nullptr) {
                e->P = &P;
                if (!use_pattern_confidence()) {
                  e->usefulness = global_usefulness_index_;
                } else {
                  if (useful) e->useful++; else e->useless++;
                  if (e->useful + e->useless >= P.pattern_usefulness_sample) {
                    e->usefulness = std::min<int64_t>(15, static_cast<int64_t>(e->useful) >> (lg2(P.pattern_usefulness_sample) - 4));
                    e->useful = 0; e->useless = 0;
                  }
                }
              }
            }
          }
        }
      }
      ++order;
    }
  }

  // Region-thrash detector (RE-REFERENCE based). A region map is "thrashing" when
  // evicted regions are soon NEEDED AGAIN -- the working set exceeds the table, so
  // no region survives long enough to learn. This is distinct from STREAMING (evicted
  // regions are never re-referenced -> fine). On evict, remember the region's vpn in a
  // small tag buffer; a later region MISS to a remembered vpn is a re-reference (thrash)
  // miss. region_thrash_ema_ tracks the re-reference fraction of region misses.
  void on_region_evict(const region_type& r)
  {
    if (r.vpn & CODE_KEY_BIT) return; // PACKED code-residency entry: maps are residency, not access history -> no bloom spill / density stats
    ++region_evictions;
    { int pop = 0; for (bool b : r.access_map) if (b) ++pop; // access-map fill fraction at eviction (min-size signal)
      dbg_amap_bits_ += static_cast<uint64_t>(pop); ++dbg_amap_regs_;
      if (pop <= 1) ++dbg_amap_sparse_; else if (pop >= static_cast<int>(blocks_per_region_)) ++dbg_amap_full_; }
    if (P.region_density_report) region_density_note(r);
    if (dedup_)
      dedup_->finalize(pack_bitmap(r.access_map));
    // Hybrid residency: spill this region's still-resident blocks into the rolling bloom so their
    // residency survives the region-table eviction (the fix for redundant re-prefetch on eviction).
    if (bloom_on()) {
      const uint64_t base = r.vpn << region_shift_;
      for (std::size_t o = 0; o < r.prefetch_map.size(); ++o)
        if (r.prefetch_map[o])
          bloom_spill(base | o);
    }
    if (P.enable_region_thrash_throttle && !rev_tags_.empty())
      rev_tags_[rev_idx(r.vpn)] = r.vpn; // remember this recently-evicted region
  }
  // Called on a region demand MISS: classify cold vs re-reference and update the EMA.
  void note_region_miss(uint64_t vpn)
  {
    if (!P.enable_region_thrash_throttle || rev_tags_.empty())
      return;
    bool rereference = (rev_tags_[rev_idx(vpn)] == vpn);
    region_thrash_ema_ += ((rereference ? 1.0 : 0.0) - region_thrash_ema_) * (1.0 / 1024.0);
    dbg_rrmiss_ += rereference; dbg_miss_++;
  }
  std::size_t rev_idx(uint64_t vpn) const
  {
    uint64_t h = vpn * 0x9E3779B97F4A7C15ull; h ^= h >> 29;
    return h % rev_tags_.size();
  }

  // ----- region staging gate -----
  // Gate engages either always, or (adaptive) only when the region table is thrashing, so streaming
  // workloads (no re-reference thrash) stay pass-through.
  bool gate_should_apply() const { return !P.gate_adaptive || region_thrash_ema_ >= P.gate_thrash_min; }
  // SPATIAL: a page earns a region entry only after staging_promote_threshold demand hits in a small
  // direct-mapped filter (measures density directly). true = admit (promote), false = still sub-threshold.
  bool staging_admit(uint64_t pn)
  {
    std::size_t idx = static_cast<std::size_t>((pn * 0x9E3779B97F4A7C15ull) % staging_tag_.size());
    if (staging_tag_[idx] != pn) { staging_tag_[idx] = pn; staging_cnt_[idx] = 1; return false; }
    if (++staging_cnt_[idx] >= P.staging_promote_threshold) { staging_cnt_[idx] = 0; return true; }
    return false;
  }

  void sample_region_lifespan()
  {
    if (regions_not_found_ + regions_found_ >= P.region_lifespan_sample) {
      region_lifespan_index_ = std::min<int>(15, static_cast<int>(regions_found_) >> (lg2(P.region_lifespan_sample) - 4));
      regions_not_found_ = 0; regions_found_ = 0;
    }
  }
  void increase_usefulness_counter() { global_useful_++; roll_global_usefulness(); }
  void decrease_usefulness_counter() { global_useless_++; roll_global_usefulness(); }
  void roll_global_usefulness()
  {
    if (global_useful_ + global_useless_ >= P.global_usefulness_sample) {
      global_usefulness_index_ = std::min<int>(15, static_cast<int>(global_useful_) >> (lg2(P.global_usefulness_sample) - 4));
      global_useful_ = 0; global_useless_ = 0;
    }
  }
  static int lg2(uint64_t x) { int r = 0; while (x > 1) { x >>= 1; ++r; } return r; }

  params P;
  prefetch_sink* sink_;
  uint64_t region_shift_;
  uint64_t page_shift_;
  uint64_t page_region_shift_;
  uint64_t blocks_per_region_;
  uint64_t page_hist_ = 0;            // rolling hash of recently accessed pages
  uint64_t last_page_seen_ = ~uint64_t{0};
  double miss_ema_ = 0.0;             // demand-miss-rate EMA for bandwidth feedback
  double region_thrash_ema_ = 0.0;    // EMA of re-reference (thrash) fraction of region misses
  std::vector<uint64_t> rev_tags_;    // recently-evicted region vpns (re-reference detector)
  uint64_t dbg_rrmiss_ = 0, dbg_miss_ = 0;
  uint64_t dbg_dchk_[16] = {0}, dbg_ddrop_[16] = {0}; // prob_drop: checks & drops bucketed by current_usefulness
  std::unordered_set<uint64_t> dbg_exact_; // pure L2-residency mirror (fill inserts / evict erases) for shadow-leak diagnosis
  uint64_t dbg_bwd_scan_ = 0, dbg_bwd_pred_ = 0, dbg_bwd_issued_ = 0; // backward scan entered / non-zero prediction / prefetch issued
  uint64_t cycle_ = 0;
  // --- Region staging gate state ---
  std::vector<uint64_t> staging_tag_; // spatial gate: direct-mapped per-page hit-count filter
  std::vector<uint8_t> staging_cnt_;
  bool cur_ip_gate_ = false;          // set by the module per-access from the EXISTING ip table (no new state)
  uint32_t ip_gate_explore_ctr_ = 0;  // paces IP-gate exploration (never permanently gate an IP)
  std::array<int8_t, 256> ip_dir_{};  // per-IP (ip_hash) saturating stride-direction counter (replaces momentum)
  uint64_t staging_drops_ = 0;        // demand allocations gated out of the region table
  bool region_just_created_ = false;  // cold-start: current access allocated a fresh region entry this operate
  std::vector<int8_t> cs_delta_;      // cold-start PC-keyed dominant startup delta
  std::vector<uint8_t> cs_cnt_;       // ...and its confidence (0 = unlearned)
  // Cross-page PHT: key (predecessor map + entry offset) -> per-offset access counts of the NEW region.
  std::unordered_map<uint64_t, xrow> xpht_;
  uint64_t last_region_ = ~uint64_t{0}; // region of the previous demand access (the temporal predecessor)
  uint64_t xpage_fire_ = 0, xpage_issued_ = 0, xpage_entries_ = 0; // cross-page: entries that predicted / pf issued / total entries

  std::unique_ptr<dedup_analyzer> dedup_;
  // Hybrid residency filter: rolling-clock 1-bit bloom holding resident blocks spilled from
  // evicted region entries (see on_region_evict / shadow_resident).
  std::vector<uint8_t> resid_bloom_;
  uint64_t bloom_hand_ = 0;
  // Access-map bloom: blocks_per_region_ filters x am_S_ bits (flattened), per-offset evict counter + clear hand.
  std::vector<uint8_t> am_bloom_;
  std::vector<uint32_t> am_evct_, am_hand_;
  int am_S_ = 1, am_k_ = 1, am_T_ = 1, am_clear_frac_ = 8;
  uint64_t am_regions_seeded_ = 0, am_bits_seeded_ = 0;
  staged_region_table regions_;
  region_table llc_regions_;
  lru_table<cpt_type, cpt_idx, cpt_idx> cpt_;
  std::vector<pattern_table> pattern_tables_;
  std::vector<pattern_table> negative_pattern_tables_;

  int global_usefulness_index_ = 0;
  int region_lifespan_index_ = 0;
  uint64_t global_useful_ = 0, global_useless_ = 0;
  uint64_t regions_found_ = 0, regions_not_found_ = 0;
  int64_t current_pf_degree_ = 0;
};

} // namespace sppam_dse

#endif
