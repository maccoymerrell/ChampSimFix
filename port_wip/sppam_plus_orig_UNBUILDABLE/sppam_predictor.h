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
#include <vector>

#include "access_kind.h"
#include "conf_table.h"
#include "dedup_analyzer.h"
#include "lru_table.h"
#include "params.h"
#include "prefetch_sink.h"
#include "spp_predictor.h"

namespace sppam_dse
{

class sppam_predictor
{
public:
  // For Q4 speculative lookahead: the SPP whose signature play-ahead seeds the lookahead
  // map. Optional; only used when P.speculative_lookahead.
  void set_spp(spp_predictor* spp) { spp_ = spp; }

  explicit sppam_predictor(const params& p, prefetch_sink* sink)
      : P(p), sink_(sink), region_shift_(p.region_bits - 6), page_shift_(p.page_bits - 6), page_region_shift_(p.page_bits - p.region_bits), blocks_per_region_(uint64_t{1} << (p.region_bits - 6)),
        regions_(p.region_sets, p.region_ways, region_set_idx{p.region_hash, p.region_page_aligned_sets ? (p.page_bits - p.region_bits) : 0}, region_tag_idx{}),
        llc_regions_(p.llc_region_sets, p.llc_region_ways, region_set_idx{false, p.region_page_aligned_sets ? (p.page_bits - p.region_bits) : 0}, region_tag_idx{}),
        cpt_(p.cpt_sets, p.cpt_ways)
  {
    std::size_t orders = 1;
    for (uint32_t s = P.pattern_size; s > P.min_pattern_size; s /= 2)
      ++orders;
    for (std::size_t o = 0; o < orders; ++o) {
      pattern_tables_.emplace_back(P.pattern_table_sets, P.pattern_table_ways);
      negative_pattern_tables_.emplace_back(P.pattern_table_sets, P.pattern_table_ways);
    }
    if (P.dedup_analysis)
      dedup_ = std::make_unique<dedup_analyzer>();
    if (P.enable_region_thrash_throttle)
      rev_tags_.assign(P.region_thrash_table, ~uint64_t{0}); // recently-evicted region tags
    // On region eviction: feed the dedup measurement (if on) AND the region-thrash
    // re-reference detector (was this region soon needed again?).
    regions_.on_evict = [this](const region_type& r) { on_region_evict(r); };
    if (P.enable_region_staging) {
      std::size_t n = P.staging_entries ? P.staging_entries : 1;
      staging_tag_.assign(n, ~uint64_t{0});
      staging_cnt_.assign(n, 0);
      staging_last_.assign(n, 0);
    }
    if (P.enable_region_bloom) {
      bloom_words_ = ((P.bloom_width ? P.bloom_width : 64) + 63) / 64;
      bloom_width_ = bloom_words_ * 64;
      bloom_layers_ = P.bloom_layers ? P.bloom_layers : 1;
      bloom_.assign(blocks_per_region_ * bloom_words_, 0);
      scratch_raddr_ = region_type{0, blocks_per_region_};
      scratch_cr_ = region_type{0, blocks_per_region_};
      scratch_scrape_ = region_type{0, blocks_per_region_};
    }
  }

  ~sppam_predictor()
  {
    if (dedup_)
      std::fprintf(stderr, "%s\n", dedup_->report(P.name).c_str());
    if (P.enable_region_bloom && !bloom_.empty()) {
      uint64_t set = 0; for (uint64_t w : bloom_) set += __builtin_popcountll(w);
      std::fprintf(stderr, "[bloomocc] %s set=%llu/%llu = %.1f%%\n", P.name.c_str(),
                   (unsigned long long)set, (unsigned long long)(bloom_.size()*64), 100.0*set/(bloom_.size()*64));
    }
    if (P.enable_region_thrash_throttle && dbg_miss_)
      std::fprintf(stderr, "[thrash] %s rereference_rate=%.3f ema=%.3f (rrmiss=%llu miss=%llu)\n",
                   P.name.c_str(), static_cast<double>(dbg_rrmiss_) / static_cast<double>(dbg_miss_), region_thrash_ema_,
                   (unsigned long long)dbg_rrmiss_, (unsigned long long)dbg_miss_);
  }

  void operate(uint64_t block, uint64_t ip, bool cache_hit, bool useful_prefetch, atype type, uint64_t cycle)
  {
    cycle_ = cycle;
    // Demand-miss-rate EMA (the LLC-pressure half of the bandwidth-feedback index).
    if (P.enable_bw_feedback)
      miss_ema_ += (((cache_hit ? 0.0 : 1.0) - miss_ema_) * (1.0 / 256.0));
    if (useful_prefetch) {
      modify_pattern_usefulness(block, true);
      increase_usefulness_counter();
    }
    if (!cache_hit)
      add_to_llc_pagemap(block);
    if (type == atype::LOAD || !P.train_demand_only) {
      if (!cache_hit || !P.access_map_miss_only)
        add_to_pagemap(block, false);
    }
    if (type == atype::LOAD || !P.prefetch_demand_only)
      do_prefetch(block, ip);
  }

  // Called on every L2 eviction. Mirrors the original prefetcher_cache_fill:
  // a not-yet-used evicted prefetch lowers pattern/global usefulness; otherwise
  // an optional scrape-on-evict harvests the region; the evicted block is always
  // removed from the prefetch-map filter.
  void on_l2_evict(uint64_t evicted_block, uint64_t cycle, bool was_unused_prefetch)
  {
    cycle_ = cycle;
    if (was_unused_prefetch) {
      modify_pattern_usefulness(evicted_block, false);
      decrease_usefulness_counter();
    } else if (P.scrape_on_evict) {
      scrape_region(evicted_block);
    }
    remove_from_pagemap(evicted_block, true);
  }

  // Shadow cache: the prefetch map mirrors L2 residency. Set on ANY fill (any
  // source); cleared only on that block's eviction (on_l2_evict above). Both
  // prefetchers squash prefetches to a resident block. Only tracks blocks whose
  // region is still in the region table (else the bits are naturally lost).
  void shadow_fill(uint64_t block)
  {
    if (region_type* r = regions_.probe_key(region_of(block)))
      r->prefetch_map[offset_of(block)] = true;
  }
  bool shadow_resident(uint64_t block)
  {
    region_type* r = regions_.probe_key(region_of(block));
    return r != nullptr && r->prefetch_map[offset_of(block)];
  }

  uint64_t prefetches_issued = 0;
  uint64_t total_lookaheads = 0;
  // Region-map residency: how often a demand access finds its page's region
  // still resident (context retained) vs absent (cold or thrashed out). A high
  // miss rate that falls as the region map grows => thrashing-limited context.
  uint64_t region_demand_accesses = 0;
  uint64_t region_demand_misses = 0;
  uint64_t region_evictions = 0; // region-table capacity evictions (staging-split hypothesis metric)
  uint64_t staging_drops = 0;    // demand accesses to sub-threshold pages held out of the table

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
    region_type() = default;
    region_type(uint64_t vpn_, uint64_t bpr) : vpn(vpn_), access_map(bpr, false), prefetch_map(bpr, false) {}
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
    std::vector<uint64_t> prediction_counter;
    const params* P = nullptr;
    pattern_type() : prediction_table(1, 16) {}
    pattern_type(uint64_t pat, const params* p) : pattern(pat), prediction_table(p->pattern_conf_sets, p->pattern_conf_ways), prediction_counter(p->pattern_size, 0), P(p) {}

    // Coherence of this pattern's prediction: best/total confidence in the conf-table
    // (1.0 = one prediction dominates; low = aliased/incoherent). Counter mode has no
    // aliasing notion -> treat as fully coherent.
    double dominance() const
    {
      if (P && P->table_or_counter)
        return 1.0;
      auto [b, t] = prediction_table.best_total();
      return t ? static_cast<double>(b) / static_cast<double>(t) : 1.0;
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
  using pattern_table = lru_table<pattern_type, pattern_idx, pattern_idx>;

  // ----- shadow bit access (no allocation) -----
  // Shadow layout: [pattern_size backward][blocks_per_region region][pattern_size forward]
  int shadow_bit(const region_type* r, const region_type* rb, const region_type* rf, int idx) const
  {
    const int ps = static_cast<int>(P.pattern_size);
    const int bpr = static_cast<int>(blocks_per_region_);
    if (idx < ps)
      return rb ? (rb->access_map[bpr - ps + idx] ? 1 : 0) : 0;
    if (idx < ps + bpr)
      return r->access_map[idx - ps] ? 1 : 0;
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
  // Staging gate: admit a NEW page to the region table only once it proves dense. A small
  // direct-mapped filter counts demand hits per page; below the threshold the page is held
  // out (no region entry), so sparse graph-walks never capacity-evict the dense set.
  bool staging_admit(uint64_t pn, uint64_t off)
  {
    if (!P.enable_region_staging)
      return true;
    std::size_t idx = static_cast<std::size_t>((pn * 0x9E3779B97F4A7C15ull) % staging_tag_.size());
    auto promote = [&]() { staging_tag_[idx] = ~uint64_t{0}; staging_cnt_[idx] = 0; return true; };
    if (staging_tag_[idx] != pn) {
      // Evicting a different (sub-threshold) page from this slot: harvest its bloom map into the
      // shared pattern tables ONCE, like scrape-on-evict for resident pages -- not per-access, which
      // would over-train sparse patterns and drown the dense ones.
      if (P.enable_region_bloom && P.bloom_train && staging_tag_[idx] != ~uint64_t{0})
        scrape_region(staging_tag_[idx] << region_shift_);
      staging_tag_[idx] = pn; staging_cnt_[idx] = 1; staging_last_[idx] = static_cast<uint8_t>(off);
      return false;
    }
    // Streaming signature: a stride-1 access to a staged page -> promote immediately.
    if (P.staging_contiguity_promote && (off == staging_last_[idx] + 1u || staging_last_[idx] == off + 1u))
      return promote();
    staging_last_[idx] = static_cast<uint8_t>(off);
    if (++staging_cnt_[idx] >= P.staging_promote_threshold)
      return promote();
    return false;
  }

  // Bit-sliced bloom: filter b occupies words [b*bloom_words_, ...). Independent hash per
  // (page, bit, layer) so a collision corrupts ONE bit, not the whole bitmap.
  std::size_t bloom_hash(uint64_t pn, uint64_t b, std::size_t layer) const
  {
    uint64_t x = pn * 0x9E3779B97F4A7C15ull + (b + 1) * 0xC2B2AE3D27D4EB4Full + (layer + 1) * 0x165667B19E3779F9ull;
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33;
    return static_cast<std::size_t>(x % bloom_width_);
  }
  void bloom_set(uint64_t pn, uint64_t b)
  {
    std::size_t base = static_cast<std::size_t>(b) * bloom_words_;
    // Rolling-CLOCK decay coupled to insertion (resid_bloom pattern): before setting, clear bloom_clear
    // bits in THIS sub-bloom via a sweeping hand. clear=2*layers pins occupancy at ~33% regardless of
    // insertion rate -- unlike the decoupled periodic sweep, which either saturates or drains.
    for (std::size_t c = 0; c < P.bloom_clear; ++c) {
      std::size_t bit = bloom_hand_ % bloom_width_;
      bloom_[base + bit / 64] &= ~(uint64_t{1} << (bit % 64));
      ++bloom_hand_;
    }
    for (std::size_t L = 0; L < bloom_layers_; ++L) { std::size_t h = bloom_hash(pn, b, L); bloom_[base + h / 64] |= (uint64_t{1} << (h % 64)); }
  }
  bool bloom_get(uint64_t pn, uint64_t b) const // AND of k layers
  {
    std::size_t base = static_cast<std::size_t>(b) * bloom_words_;
    for (std::size_t L = 0; L < bloom_layers_; ++L) { std::size_t h = bloom_hash(pn, b, L); if (!((bloom_[base + h / 64] >> (h % 64)) & 1u)) return false; }
    return true;
  }
  // Materialize a transient region from the page's per-bit bloom filters. Null if no bits set.
  // access_map from the bloom; prefetch_map EMPTY (redundancy handled by the shadow squash) --
  // seeding it from the access map makes every accessed block look "already prefetched" and
  // self-filters the predictions via the `already` check.
  region_type* materialize_bloom(uint64_t pn, region_type& scratch)
  {
    if (!P.enable_region_bloom)
      return nullptr;
    bool any = false;
    for (uint64_t b = 0; b < blocks_per_region_; ++b) { bool bit = bloom_get(pn, b); scratch.access_map[b] = bit; scratch.prefetch_map[b] = false; any |= bit; }
    if (!any)
      return nullptr;
    scratch.vpn = pn;
    scratch.momentum = 0;
    scratch.last_block = 0;
    scratch.pat_ctx = 0;
    scratch.next_vpn = scratch.prev_vpn = 0;
    scratch.has_next = scratch.has_prev = false;
    scratch.last_access_time = cycle_;
    scratch.since_last_scrape = 0;
    return &scratch;
  }
  // Region for PREDICTION: the exact entry if resident, else (bloom on) a bloom-materialized one.
  region_type* lookup_predict(uint64_t pn, region_type& scratch)
  {
    if (region_type* r = regions_.find_key(pn))
      return r;
    if (!P.bloom_predict)
      return nullptr;
    return materialize_bloom(pn, scratch);
  }

  void add_to_pagemap(uint64_t block, bool prefetch)
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
        if (r->last_block < off)
          r->momentum = r->momentum < 7 ? r->momentum + 1 : 7;
        else
          r->momentum = r->momentum > -8 ? r->momentum - 1 : -8;
        r->last_block = off;
        r->since_last_scrape += 1;
      }
    } else {
      if (!prefetch && !staging_admit(pn, off)) { // sub-threshold page: held out of the exact table
        ++staging_drops;
        if (P.enable_region_bloom)
          bloom_set(pn, off); // keep its (bit-sliced) access map; training happens once on staging-evict
        return;
      }
      region_type& nr = regions_.insert(region_type{pn, blocks_per_region_});
      // EVICTION-bloom: seed the re-inserted region from its preserved (evicted) map -- the bloom->table
      // transition. Without this, the re-insert (which precedes do_prefetch) shadows the bloom with an
      // empty map and the preserved context is never used. This is the "retrieve the region map" step.
      if (P.enable_region_bloom && P.bloom_evict)
        for (uint64_t b = 0; b < blocks_per_region_; ++b)
          if (bloom_get(pn, b))
            nr.access_map[b] = true;
      if (P.pattern_context_src == 1)
        nr.pat_ctx = static_cast<uint32_t>(page_hist_); // capture the path that led to this page
      if (prefetch) {
        nr.prefetch_map[off] = true;
      } else {
        nr.access_map[off] = true;
        nr.prefetch_map[off] = true;
        nr.last_block = off;
        nr.since_last_scrape += 1;
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
  std::pair<uint64_t, uint64_t> patterns_at(const region_type* r, const region_type* rb, const region_type* rf, int64_t offset, bool want_neg) const
  {
    if (r == nullptr)
      return {0, 0};
    uint64_t pos = 0, neg = 0;
    const int ps = static_cast<int>(P.pattern_size);
    const int ssz = shadow_size();
    for (int i = static_cast<int>(offset) + 1; i <= static_cast<int>(offset) + ps; ++i)
      pos = (pos << 1) | (i >= 0 && i < ssz ? uint64_t(shadow_bit(r, rb, rf, i)) : 0);
    if (want_neg)
      for (int i = static_cast<int>(offset) + 2 * ps - 1; i >= static_cast<int>(offset) + ps; --i)
        neg = (neg << 1) | (i >= 0 && i < ssz ? uint64_t(shadow_bit(r, rb, rf, i)) : 0);
    return {pos, neg};
  }

  // Q4: speculatively mark SPP's play-ahead into a LOCAL copy of the trigger region's
  // access map, returning the saved original. SPP walks its page signature to completion
  // (depth-capped); each predicted block sets its bit. SPPAM's whole normal prediction
  // then runs on this augmented map ("what the map will look like soon"). Restore the
  // returned copy afterwards so the real region map is unchanged.
  std::vector<bool> speculative_augment(uint64_t trigger, region_type* r)
  {
    std::vector<bool> saved;
    if (spp_ == nullptr || r == nullptr)
      return saved;
    saved = r->access_map; // local copy of the original
    const int bpr = static_cast<int>(blocks_per_region_);
    for (int64_t pb : spp_->speculate(trigger, P.spp_lookahead)) {
      if (region_of(static_cast<uint64_t>(pb)) != r->vpn)
        continue; // page squash keeps preds in-page; region==page -> in-region
      int o = static_cast<int>(offset_of(static_cast<uint64_t>(pb)));
      if (o >= 0 && o < bpr)
        r->access_map[static_cast<std::size_t>(o)] = true;
    }
    return saved;
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

  std::pair<uint64_t, bool> get_prefetch_pattern(uint64_t access_pattern, uint64_t ctx, int order, bool negative)
  {
    auto& tbl = (negative && P.separate_negative_tables) ? negative_pattern_tables_.at(order) : pattern_tables_.at(order);
    // Order o keys on a SHORTER history: the low (pattern_size>>order) bits, a suffix of
    // the full-width key. This nests, so "fall through to a shorter history" actually
    // queries the keys scrape() trained at that width (rather than missing every order>0).
    uint64_t key_pat = access_pattern & mask(P.pattern_size >> order);
    // Bloom (staged-page) predictions must NOT mutate the shared pattern table: probe (no LRU
    // bump) and skip occurrences++, else their garbage queries thrash exact pages' patterns out.
    pattern_type* e = pred_readonly_ ? tbl.probe_key(pat_key(key_pat, ctx)) : tbl.find_key(pat_key(key_pat, ctx));
    if (e != nullptr) {
      if (!pred_readonly_) {
        e->occurrences++;
        e->P = &P;
      }
      auto pred = e->get_prediction();
      // Terminating signal: a shorter-history (order>0) level that aliases conflicting
      // continuations (low dominance) is incoherent -> STOP the level search (return
      // pp=0,valid=true) rather than leak its garbage prediction into the output.
      if (P.coherence_min > 0.0 && order > 0 && pred.first != 0 && e->dominance() < P.coherence_min)
        return {0, true};
      return pred;
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

  // ----- scraping -----
  void scrape_region(uint64_t block)
  {
    region_type* r = regions_.find_key(region_of(block));
    if (r == nullptr)
      r = materialize_bloom(region_of(block), scratch_scrape_); // staged/bloom page: train from its bloom map
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
          // History (key) = the low j bits of the FULL-width history, so order o nests as
          // a suffix of order 0; prediction = the nearest j of the predicted region.
          uint64_t predicted = ap & mask(j);
          uint64_t accessed = (ap >> ps) & mask(j);
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
          // Nested history (see forward branch).
          uint64_t predicted = ap & mask(j);
          uint64_t accessed = (ap >> ps) & mask(j);
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
    auto& tbl = pattern_tables_.at(order);
    pattern_type* e = pred_readonly_ ? tbl.probe_key(pat_key(access_pattern, ctx)) : tbl.find_key(pat_key(access_pattern, ctx));
    return e ? static_cast<int>(e->usefulness) : global_usefulness_index_;
  }

  int set_prefetch_degree(uint64_t access_pattern, uint64_t ctx, int order, int prev_usefulness)
  {
    if (P.prediction_only) {
      // Dumb predictor: no degree throttle; full confidence (lookahead runs the whole
      // confidence path). Strips usefulness/bw/degree feedback to isolate prediction.
      current_pf_degree_ = int64_t{1} << 30;
      return 15;
    }
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

  // ----- main prefetch generation -----
  void do_prefetch(uint64_t addr, uint64_t /*ip*/)
  {
    // addr's region (used for momentum and the prefetch-map filter); not
    // structurally mutated during this call, so the pointer stays valid.
    pred_readonly_ = false;
    region_type* r_addr = lookup_predict(region_of(addr), scratch_raddr_); // exact, or bloom-materialized if staged
    // Q4 speculative mode: augment a LOCAL copy of the region access map with SPP's
    // play-ahead before predicting, restoring it on exit -> the region map is unchanged.
    std::vector<bool> spec_saved;
    if (P.speculative_lookahead && spp_ && r_addr != nullptr)
      spec_saved = speculative_augment(addr, r_addr);
    int momentum = r_addr ? r_addr->momentum : 0;
    uint64_t addr_region = region_of(addr); // region-entry granularity
    uint64_t addr_page = page_of(addr);      // 4 KB page (squash boundary)
    int pf_issued = 0;
    const int ps = static_cast<int>(P.pattern_size);

    // Cache the region + shadows for get_patterns across scan steps in one page.
    uint64_t cache_pn = ~uint64_t{0};
    region_type* cr = nullptr;
    const region_type* crb = nullptr;
    const region_type* crf = nullptr;
    uint64_t cctx = 0;

    for (int dir : {1, -1}) {
      bool forward = (dir == 1);
      if (forward && !P.scan_forward) continue;
      if (!forward && !P.scan_backward) continue;
      if (!forward && !P.do_negative) continue;
      uint64_t scan = forward ? P.scan_distance_forward : P.scan_distance_backward;
      for (int sstep = forward ? 0 : 1; sstep < static_cast<int>(scan); ++sstep) {
        int current_usefulness = use_pattern_confidence() ? 15 : global_usefulness_index_;
        uint64_t pf_base = addr + static_cast<uint64_t>(dir * sstep);
        uint64_t bpn = region_of(pf_base);
        if (bpn != cache_pn) {
          cr = lookup_predict(bpn, scratch_cr_); // exact, or bloom-materialized if staged
          pred_readonly_ = (cr == &scratch_cr_);  // bloom region: read shared tables WITHOUT mutating them
          crb = cr ? shadow_back(cr) : nullptr;
          crf = cr ? shadow_fwd(cr) : nullptr;
          cctx = ctx_of(cr);
          cache_pn = bpn;
        }
        auto [pos_ap, neg_ap] = patterns_at(cr, crb, crf, static_cast<int64_t>(offset_of(pf_base)), !forward);
        uint64_t ap = forward ? pos_ap : neg_ap;
        bool neg = !forward;
        if (forward ? (momentum <= P.forward_momentum_min) : (momentum >= P.backward_momentum_min))
          continue;

        int order = 0;
        for (int i = ps; i >= static_cast<int>(P.min_pattern_size); i /= 2) {
          auto [pp, pv] = get_prefetch_pattern(ap, cctx, order, neg);
          if (pp == 0 && !pv && P.use_default_prediction && ((i >> 1) < static_cast<int>(P.min_pattern_size))) {
            if ((ap & P.default_pattern) == P.default_pattern) { pp = P.effective_default_prediction(); ap = P.default_pattern; pv = true; }
          }
          bool continue_outer = false;
          int lookaheads = 0, lookahead_offset = 0;
          while (true) {
            current_usefulness = std::clamp(do_4_bit_mult(do_4_bit_mult(static_cast<int>(P.lookahead_conf_factor), set_prefetch_degree(ap, cctx, order, current_usefulness)), current_usefulness), 0, 15);
            if (!P.prediction_only && P.prob_drop_prefetches && (cycle_ % 128) < static_cast<uint64_t>(P.prefetch_drop_chance_usefulness[current_usefulness]) && global_usefulness_index_ < 8)
              break;
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
                  bool fill_l2 = (static_cast<uint64_t>(pf_issued) < P.prefetch_to_l2_degree);
                  if (!fill_l2 && check_llc_pagemap(step)) {
                    // already in LLC map -> filter
                  } else {
                    // benefit = usefulness/15 (expected usefulness ~ coverage/bandwidth).
                    sink_->issue_prefetch(step, fill_l2, /*from_spp=*/false, /*benefit=*/current_usefulness / 15.0,
                                          /*src_density=*/r_addr ? static_cast<uint16_t>(__builtin_popcountll(pack_bitmap(r_addr->access_map))) : uint16_t{0});
                    if (fill_l2) {
                      if (same_region && r_addr) mark_prefetch(r_addr, soff, step);
                      else { add_to_pagemap(step, true); cache_pn = ~uint64_t{0}; r_addr = regions_.find_key(addr_region); }
                    } else
                      add_to_llc_pagemap(step);
                    ++pf_issued;
                    ++prefetches_issued;
                    if (pf_issued >= current_pf_degree_) break;
                  }
                }
              }
            }
            if (P.do_lookahead && pf_issued < current_pf_degree_) {
              auto la = get_prefetch_pattern(pp, cctx, order, neg);
              ap = pp; pp = la.first; pv = la.second;
              ++lookaheads; lookahead_offset += i;
              if (lookaheads > static_cast<int>(P.lookahead_depth) || current_usefulness < static_cast<int>(P.lookahead_conf_cutoff)) break;
              if (pp != 0) ++total_lookaheads;
            } else
              break;
          }
          if (!continue_outer) break;
          ++order;
        }
      }
    }
    // Restore the speculatively-augmented access map -> region map left unchanged.
    if (!spec_saved.empty() && r_addr != nullptr)
      r_addr->access_map = spec_saved;
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
    ++region_evictions;
    // EVICTION-bloom: spill the region's mature access map into the bloom so it survives the
    // capacity eviction (resid_bloom pattern; each set block-position spilled independently).
    if (P.enable_region_bloom && P.bloom_evict)
      for (uint64_t b = 0; b < blocks_per_region_; ++b)
        if (r.access_map[b])
          bloom_set(r.vpn, b);
    if (dedup_)
      dedup_->finalize(pack_bitmap(r.access_map));
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
  spp_predictor* spp_ = nullptr; // Q4: SPP play-ahead source for speculative lookahead
  uint64_t region_shift_;
  uint64_t page_shift_;
  uint64_t page_region_shift_;
  uint64_t blocks_per_region_;
  uint64_t page_hist_ = 0;            // rolling hash of recently accessed pages
  uint64_t last_page_seen_ = ~uint64_t{0};
  double miss_ema_ = 0.0;             // demand-miss-rate EMA for bandwidth feedback
  double region_thrash_ema_ = 0.0;    // EMA of re-reference (thrash) fraction of region misses
  std::vector<uint64_t> rev_tags_;    // recently-evicted region vpns (re-reference detector)
  std::vector<uint64_t> staging_tag_; // direct-mapped page-staging filter: tag per slot
  std::vector<uint8_t> staging_cnt_;  // ...and its demand-hit count toward promotion
  std::vector<uint8_t> staging_last_; // ...and last offset seen (for stride-1 streaming detect)
  // Bit-sliced bloom: blocks_per_region_ filters, each bloom_words_*64 bits, stored flat
  // (filter b occupies [b*bloom_words_, (b+1)*bloom_words_) ). k=bloom_layers_ hashes per bit.
  std::vector<uint64_t> bloom_;
  std::size_t bloom_width_ = 64;   // bits per filter (multiple of 64)
  std::size_t bloom_words_ = 1;    // = bloom_width_/64
  std::size_t bloom_layers_ = 1;   // k
  std::size_t bloom_hand_ = 0; // rolling-clock hand (bit position within a sub-bloom); clear-on-insert decay
  region_type scratch_raddr_, scratch_cr_, scratch_scrape_; // transient bloom-materialized regions
  bool pred_readonly_ = false;             // current scan region is a bloom scratch -> don't mutate shared tables
  uint64_t dbg_rrmiss_ = 0, dbg_miss_ = 0;
  uint64_t cycle_ = 0;

  std::unique_ptr<dedup_analyzer> dedup_;
  region_table regions_;
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
