// Minimal SPP-lite (Signature Path Prefetcher), page-keyed and address-only (no
// IP), so it survives IP-less L2 prefetch traffic (e.g. berti fills from L1D).
//
// Signature table (ST): per 4 KB page -> {last block offset, rolling delta
// signature}. Pattern table (PT): signature -> a few {delta, confidence} pairs.
// On each access we train PT[old_sig] with the observed delta, advance the
// signature, then predict along the signature path with confidence decay,
// prefetching within the page (4 KB squash).
#ifndef SPPAM_DSE_SPP_PREDICTOR_H
#define SPPAM_DSE_SPP_PREDICTOR_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "params.h"
#include "prefetch_sink.h"

namespace sppam_dse
{

class spp_predictor
{
public:
  spp_predictor(const params& p, prefetch_sink* sink)
      : sink_(sink), page_shift_(p.page_bits - 6),
        share_(p.spp_share_region_table),
        gran_shift_(p.spp_share_region_table ? (p.region_bits - 6) : (p.page_bits - 6)),
        page_gran_shift_(p.spp_share_region_table ? (p.page_bits - p.region_bits) : 0),
        blocks_per_unit_(uint64_t{1} << gran_shift_),
        sig_mask_((uint32_t{1} << p.spp_sig_bits) - 1), threshold_(p.spp_threshold), lookahead_(p.spp_lookahead),
        bw_feedback_(p.enable_bw_feedback), useful_fb_(p.spp_usefulness_feedback), conf_cap_((uint16_t{1} << p.spp_conf_bits) - 1),
        pt_sets_(p.spp_pt_sets), pt_ways_(p.spp_pt_ways),
        deltas_per_sig_(p.spp_deltas_per_sig < MAX_DELTAS ? p.spp_deltas_per_sig : MAX_DELTAS),
        per_sig_(p.spp_per_sig_usefulness), u_cap_((uint16_t{1} << p.spp_usefulness_bits) - 1),
        per_sig_floor_(p.spp_per_sig_floor), per_sig_prior_(p.spp_per_sig_prior), pf_sample_div_(p.spp_pf_sample_div ? p.spp_pf_sample_div : 1),
        ghr_on_(p.spp_ghr), min_delta_(p.spp_min_delta), min_conf_(p.spp_min_conf), mht_(p.spp_multi_high_throttle),
        st_(p.spp_share_region_table ? p.region_sets * p.region_ways : p.spp_st_entries), pt_(pt_sets_ * pt_ways_),
        pff_(per_sig_ ? p.spp_pf_filter_entries : 0),
        ghr_(p.spp_ghr ? (p.spp_ghr_entries ? p.spp_ghr_entries : 1) : 0)
  {
  }

  // Two units (page or sub-page region) are in the same 4 KB page iff they share
  // the high bits above the within-page index.
  bool same_page_unit(uint64_t a, uint64_t b) const { return (a >> page_gran_shift_) == (b >> page_gran_shift_); }

  // Usefulness feedback from the cache: an SPP prefetch was demand-used / evicted
  // unused. NOTE: this global lookahead throttle is a single-core efficiency knob;
  // it trades coverage for bandwidth, which can hurt a contended low-accuracy
  // victim, so it is off by default (a per-signature version would be surgical).
  double dbg_useful_ema() const { return useful_ema_; }
  void reward(bool used, uint64_t block)
  {
    // The global usefulness EMA doubles as the PRIOR for per-pattern estimates, so
    // keep it live in per-pattern mode too (cheap; one EMA).
    if (useful_fb_ || per_sig_)
      useful_ema_ += ((used ? 1.0 : 0.0) - useful_ema_) * (1.0 / 64.0);
    if (!per_sig_)
      return;
    // Attribute this outcome to the pattern (initiating signature) that issued the
    // prefetch (via the block->sig filter), and update its usefulness counters.
    pf_filter& fe = pff_[block % pff_.size()];
    if (!fe.valid || fe.block != block)
      return;
    fe.valid = false;
    pt_way* e = pt_lookup(fe.sig);
    if (e == nullptr) // signature already evicted from the PT -> attribution lost
      return;
    if (e->u_seen >= u_cap_) { e->u_seen >>= 1; e->u_used >>= 1; } // keep adaptive
    e->u_seen++;
    if (used) e->u_used++;
  }

  void operate(uint64_t block)
  {
    uint64_t unit = block >> gran_shift_;            // page, or sub-page region when shared
    int offset = static_cast<int>(block & (blocks_per_unit_ - 1));
    st_entry& st = st_[unit % st_.size()];

    // GHR warm-start: on a NEW 4 KB page, if a recent page-cross recorded a continuation whose
    // landing offset matches this first access, SEED this page's signature so SPP follows the
    // stride immediately (a large stride gives too few accesses/page to build a signature cold).
    const uint64_t page = block >> page_shift_;
    if (ghr_on_ && (!have_page_ || page != last_page_)) {
      const int poff = static_cast<int>(block & ((uint64_t{1} << page_shift_) - 1));
      for (const auto& g : ghr_) {
        if (g.landing == poff) {
          st = st_entry{true, unit, offset, g.sig};
          last_unit_ = unit; have_last_ = true; last_page_ = page; have_page_ = true;
          predict_path(static_cast<int64_t>(block), g.sig, g.sig);
          return;
        }
      }
    }

    // Temporal ordering: the delta is from the region that was LAST ACCESSED (the
    // temporal predecessor), NOT the spatial neighbor -- as long as it is in the
    // same 4 KB page and still resident. This keeps the delta sequence correct for
    // irregular access when the region table is sub-page granular.
    uint32_t sig = 0;
    int64_t delta = 0;
    bool ordered = false;
    if (have_last_ && same_page_unit(last_unit_, unit)) {
      st_entry& pred = st_[last_unit_ % st_.size()];
      if (pred.valid && pred.tag == last_unit_) {
        int64_t pred_block = static_cast<int64_t>(last_unit_) * static_cast<int64_t>(blocks_per_unit_) + pred.last_offset;
        delta = static_cast<int64_t>(block) - pred_block;
        sig = pred.sig;
        ordered = true;
      }
    }
    last_unit_ = unit;
    have_last_ = true;
    last_page_ = page;
    have_page_ = true;
    if (!ordered) {
      st = st_entry{true, unit, offset, 0};
      return;
    }
    st.valid = true;
    st.tag = unit;
    st.last_offset = offset;
    if (delta == 0) {
      st.sig = sig;
      return;
    }
    // Large-delta filter: SPP specializes in strides beyond SPPAM's dense reach. Small deltas are
    // SPPAM's job -- skip them so they don't pollute SPP's signatures (keep the signature as-is).
    if (min_delta_ > 0 && delta > -min_delta_ && delta < min_delta_) {
      st.sig = sig;
      return;
    }
    train(sig, static_cast<int>(delta));
    st.sig = advance(sig, static_cast<int>(delta));
    predict_path(static_cast<int64_t>(block), st.sig, st.sig);
  }

  // Signature-path lookahead in ABSOLUTE blocks. At a 4 KB page boundary, record a GHR
  // continuation (when enabled) so the next page can warm-start, instead of only squashing.
  void predict_path(int64_t block, uint32_t start_sig, uint32_t s0)
  {
    uint32_t s = start_sig;
    int64_t pred = block;
    uint64_t page = static_cast<uint64_t>(block) >> page_shift_;
    double conf = 1.0;
    uint32_t eff_la = lookahead_;
    if (bw_feedback_)
      eff_la = static_cast<uint32_t>(static_cast<uint64_t>(eff_la) * (16 - sink_->dram_bw_index()) / 16);
    if (per_sig_) {
      double u = sig_usefulness(s0);
      double uf = (u - per_sig_floor_) / (1.0 - per_sig_floor_);
      uf = uf < 0.0 ? 0.0 : (uf > 1.0 ? 1.0 : uf);
      eff_la = static_cast<uint32_t>(eff_la * uf);
      if (eff_la == 0) eff_la = 1;
    } else if (useful_fb_) {
      double uf = (useful_ema_ - 0.25) / 0.5;
      uf = uf < 0.0 ? 0.0 : (uf > 1.0 ? 1.0 : uf);
      eff_la = static_cast<uint32_t>(eff_la * uf);
      if (eff_la == 0) eff_la = 1;
    }
    for (uint32_t d = 0; d < eff_la; ++d) {
      auto best = predict(s);
      if (best.conf <= 0.0)
        break;
      conf *= best.conf;
      if (conf < threshold_)
        break;
      pred += best.delta;
      if (pred < 0 || (static_cast<uint64_t>(pred) >> page_shift_) != page) {
        if (ghr_on_ && pred >= 0) { // page cross -> remember the continuation for the next page
          int land = static_cast<int>(static_cast<uint64_t>(pred) & ((uint64_t{1} << page_shift_) - 1));
          ghr_[ghr_head_] = ghr_entry{s, land};
          ghr_head_ = (ghr_head_ + 1) % ghr_.size();
        }
        break; // 4 KB page squash
      }
      if (per_sig_ && (++pf_sample_ctr_ % pf_sample_div_ == 0))
        pff_[static_cast<uint64_t>(pred) % pff_.size()] = {true, static_cast<uint64_t>(pred), s0};
      sink_->issue_prefetch(static_cast<uint64_t>(pred), true, /*from_spp=*/true, /*benefit=*/conf, /*gen_tag=*/1u | ((d > 15u ? 15u : d) << 1));
      s = advance(s, best.delta);
    }
  }

private:
  struct st_entry {
    bool valid = false;
    uint64_t tag = 0;
    int last_offset = 0;
    uint32_t sig = 0;
  };
  static constexpr std::size_t MAX_DELTAS = 8;
  struct pt_delta {
    int16_t delta = 0;
    uint16_t conf = 0;
  };
  // One PT way: a tagged signature and its delta candidates. The PT is a hashed,
  // tagged, set-associative table (the signature space is sparse, so we hash the
  // signature into a fixed table rather than direct-index 2^sig_bits).
  struct pt_way {
    bool valid = false;
    uint32_t sig_tag = 0;
    uint32_t lru = 0;
    uint16_t u_used = 0;   // per-signature usefulness: prefetches from this sig later demand-used
    uint16_t u_seen = 0;   // ...and total resolved (used or evicted-unused). Reset on sig realloc.
    std::array<pt_delta, MAX_DELTAS> d{};
  };
  // Block->signature attribution filter: a prefetch's outcome (use/evict) is routed
  // back to the signature that issued it. Direct-mapped, bounded; a collision just
  // loses one attribution.
  struct pf_filter { bool valid = false; uint64_t block = 0; uint32_t sig = 0; };
  struct prediction {
    int delta = 0;
    double conf = 0.0;
  };

  uint32_t advance(uint32_t sig, int delta) const
  {
    return ((sig << 3) ^ (static_cast<uint32_t>(delta) & 0x7Fu)) & sig_mask_;
  }
  std::size_t pt_set(uint32_t sig) const
  {
    uint32_t h = sig * 2654435761u; // Knuth multiplicative hash; spread sparse sigs
    h ^= h >> 15;
    return h % pt_sets_;
  }
  pt_way* pt_lookup(uint32_t sig)
  {
    pt_way* b = pt_.data() + pt_set(sig) * pt_ways_;
    for (pt_way* w = b; w != b + pt_ways_; ++w)
      if (w->valid && w->sig_tag == sig)
        return w;
    return nullptr;
  }

  void train(uint32_t sig, int delta)
  {
    pt_way* b = pt_.data() + pt_set(sig) * pt_ways_;
    pt_way* hit = nullptr;
    pt_way* lru = b;
    for (pt_way* w = b; w != b + pt_ways_; ++w) {
      if (w->valid && w->sig_tag == sig) hit = w;
      if (!w->valid || w->lru < lru->lru) lru = w;
    }
    pt_way* e = hit;
    if (e == nullptr) { // allocate a new signature in this set (LRU)
      e = lru;
      *e = pt_way{};
      e->valid = true;
      e->sig_tag = sig;
    }
    e->lru = ++pt_clock_;
    pt_delta* slot = nullptr;
    pt_delta* dlru = &e->d[0];
    for (std::size_t i = 0; i < deltas_per_sig_; ++i) {
      if (e->d[i].conf > 0 && e->d[i].delta == delta) { slot = &e->d[i]; break; }
      if (e->d[i].conf < dlru->conf) dlru = &e->d[i];
    }
    if (slot == nullptr) { slot = dlru; slot->delta = static_cast<int16_t>(delta); slot->conf = 0; }
    if (slot->conf < conf_cap_) slot->conf++;
    if (slot->conf >= conf_cap_)
      for (std::size_t i = 0; i < deltas_per_sig_; ++i) e->d[i].conf = static_cast<uint16_t>(e->d[i].conf >> 1);
  }

  // Per-pattern usefulness in [0,1], smoothed toward the GLOBAL EMA prior by a
  // pseudocount: (u_used + k*prior) / (u_seen + k). A pattern with no/sparse samples
  // inherits the global average (so on a uniformly-bad workload like mcf, where
  // signatures are many and rarely revisited, unseen patterns are still throttled);
  // a well-evidenced pattern converges to its own ratio (the discrimination win).
  double sig_usefulness(uint32_t sig)
  {
    double prior = useful_ema_;
    pt_way* e = pt_lookup(sig);
    if (e == nullptr)
      return prior;
    double k = per_sig_prior_;
    return (static_cast<double>(e->u_used) + k * prior) / (static_cast<double>(e->u_seen) + k);
  }

  prediction predict(uint32_t sig)
  {
    pt_way* e = pt_lookup(sig);
    if (e == nullptr)
      return {};
    const pt_delta* best = nullptr;
    if (min_conf_ > 0) {
      // Absolute per-delta confidence (SPP's real gate): a delta prefetches only if its counter
      // >= min_conf_. If >=2 deltas clear it there is no standout (complex/random) -> throttle.
      // The deltas fight each other via the halve-on-saturate in train(); noisy deltas never reach
      // min_conf_ before being replaced, so the signature stays silent -- no per-sig usefulness needed.
      int nhi = 0;
      for (std::size_t i = 0; i < deltas_per_sig_; ++i) {
        if (e->d[i].conf >= min_conf_) ++nhi;
        if (e->d[i].conf > 0 && (best == nullptr || e->d[i].conf > best->conf)) best = &e->d[i];
      }
      if (best == nullptr || best->conf < min_conf_) return {};
      if (mht_ && nhi >= 2) return {};
      return {best->delta, static_cast<double>(best->conf) / static_cast<double>(conf_cap_)};
    }
    uint32_t total = 0;
    for (std::size_t i = 0; i < deltas_per_sig_; ++i) {
      total += e->d[i].conf;
      if (e->d[i].conf > 0 && (best == nullptr || e->d[i].conf > best->conf))
        best = &e->d[i];
    }
    if (best == nullptr || total == 0)
      return {};
    return {best->delta, static_cast<double>(best->conf) / static_cast<double>(total)};
  }

  prefetch_sink* sink_;
  uint64_t page_shift_;
  bool share_;
  uint64_t gran_shift_;
  uint64_t page_gran_shift_;
  uint64_t blocks_per_unit_;
  uint32_t sig_mask_;
  double threshold_;
  uint32_t lookahead_;
  bool bw_feedback_;
  bool useful_fb_;
  uint16_t conf_cap_;       // PT confidence saturation cap (= 2^spp_conf_bits - 1)
  uint64_t last_unit_ = 0;  // the region last accessed (temporal predecessor for delta ordering)
  bool have_last_ = false;
  double useful_ema_ = 0.7; // SPP prefetch usefulness EMA (optimistic init)
  std::size_t pt_sets_, pt_ways_, deltas_per_sig_;
  bool per_sig_;                 // per-pattern usefulness throttle enabled
  uint16_t u_cap_;               // per-pattern used/seen counter saturation
  double per_sig_floor_;         // usefulness mapped to zero depth at/below this
  double per_sig_prior_;         // pseudocount weight on the global-EMA prior
  uint64_t pf_sample_div_;       // sample 1/N issued prefetches into the attribution filter
  uint64_t pf_sample_ctr_ = 0;
  uint32_t pt_clock_ = 0;
  // GHR / large-delta / absolute-confidence knobs (all inert unless enabled).
  bool ghr_on_;
  int min_delta_;                // 0 = no large-delta filter
  int min_conf_;                 // 0 = legacy ratio confidence; >0 = absolute per-delta threshold
  bool mht_;                     // multi-high throttle (>=2 deltas over min_conf_ -> complex, silent)
  uint64_t last_page_ = 0;
  bool have_page_ = false;
  std::size_t ghr_head_ = 0;
  struct ghr_entry { uint32_t sig = 0; int landing = -1; }; // cross-page continuation
  std::vector<st_entry> st_;
  std::vector<pt_way> pt_;
  std::vector<pf_filter> pff_;   // block->sig attribution filter
  std::vector<ghr_entry> ghr_;   // cross-page Global History Register (warm-start seeds)
};

} // namespace sppam_dse

#endif
