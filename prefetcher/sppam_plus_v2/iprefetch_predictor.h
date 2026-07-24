#ifndef SPPAM_IPREFETCH_PREDICTOR_H
#include <cstdio>
#define SPPAM_IPREFETCH_PREDICTOR_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "params.h"

namespace sppam_dse
{
// PC-block BTB instruction prefetcher (L2 staging).
//
// This is a BTB, not a full branch graph: keyed by IP BLOCK (ip>>6, not exact PC), it stores a single
// signed target DELTA (not a full target PC) plus a bimodal directionality counter per entry. Instruction
// fetches have ip == v_address, so it lives in IP (virtual) space where a jump delta is compact.
//
// The walk is fetch-directed: from the demanded block, predict the next block, translate it to physical,
// prefetch, repeat -- as deep as we stay confident in the PREDICTED DIRECTION (taken OR fall-through).
//
//   * FALL-THROUGH is just ib+1 -- the null hypothesis. It happens on a table MISS (a block that never
//     jumped never allocates an entry) or on a strong NOT-TAKEN bias. No entry/slot is spent on it.
//   * L1I misses are noisy, so we classify the observed block delta d = cur - last: a small FORWARD delta
//     (1..instr_ft_blocks) is FALL-THROUGH (not-taken); a BACKWARD delta (any) or a LONG-FORWARD delta
//     (> instr_ft_blocks) is a TAKEN jump. Only taken jumps allocate/update a BTB entry.
//   * The bimodal `dir` counter is a Smith saturating counter (2-bit by default -- strong-NT/weak-NT/weak-T/
//     strong-T). A taken obs increments it, a fall-through obs of an EXISTING entry decrements it. The high
//     bit is the predicted direction; the counter's distance from the midpoint is the confidence (only the
//     strong/saturated states clear the gate). The walk breaks only when the direction is weak/ambiguous.
//     A miss => confident fall-through (conf 1).
//   * One target only: L1I-miss noise makes a second target unreliable. Same-direction target updates keep
//     the MINIMUM signed delta (closest forward / furthest backward); a direction flip adopts the new one.
//
// A tiny vpage->ppage table (filled from the instruction stream we already see -- each packet carries both
// ip and paddr) turns a predicted IP block into a physical block to prefetch. Fully separate from the data
// region/access maps; its own bounded physical residency filter stops re-prefetching resident lines.
class iprefetch_predictor
{
public:
  explicit iprefetch_predictor(const params& p)
      : P(p), edges_(pow2(p.instr_table_entries)), xv_(pow2(p.instr_xlate_entries), kEmpty),
        xp_(pow2(p.instr_xlate_entries), 0), filt_(pow2(p.instr_filter_entries), kFEmpty)
  {
    emask_ = edges_.size() - 1;
    xmask_ = xv_.size() - 1;
    fmask_ = filt_.size() - 1;
    const int db = (P.instr_delta_bits > 0 && P.instr_delta_bits <= 16) ? P.instr_delta_bits : 8;
    dlim_ = int64_t{1} << (db - 1); // encodable IP-block delta must satisfy |delta| < dlim_
    ft_ = P.instr_ft_blocks > 0 ? P.instr_ft_blocks : 3;
    const int dirb = (P.instr_dir_bits > 0 && P.instr_dir_bits <= 8) ? P.instr_dir_bits : 2;
    smax_ = (1 << dirb) - 1;               // Smith-counter max state (2-bit -> 3)
    smid_ = smax_ / 2.0;                    // decision midpoint (2-bit -> 1.5)
    sinit_ = static_cast<int8_t>((smax_ + 1) / 2); // weak-taken on allocation (2-bit -> 2)
    miss_conf_ = static_cast<double>(P.instr_miss_conf) / 100.0; // direction conf assigned to a table miss (sequential guess)
  }

  // One instruction (L1I-miss) access: `ip_block` = ip>>6 (virtual, the BTB key), `phys_block` = physical
  // block (what the cache holds / we prefetch). issue(phys_block, ...) prefetches into L2.
  template <class IssueFn>
  void operate(uint64_t ip_block, uint64_t phys_block, const IssueFn& issue)
  {
    ++demands_;
    xlate_insert(ip_block >> 6, phys_block >> 6); // learn this instruction page's translation
    if (have_last_)
      train(last_ip_, static_cast<int64_t>(ip_block) - static_cast<int64_t>(last_ip_)); // last_ip -> ip_block transition
    mark(phys_block); // the demanded block is (being) fetched -> resident

    const double gate = static_cast<double>(P.instr_conf) / 100.0; // strong/weak threshold
    int budget = P.instr_walk_budget;                              // confidence budget for this access
    uint64_t cur = ip_block;
    for (int d = 0; d < P.instr_la_depth; ++d) {
      auto [nx_ip, conf] = predict(cur);
      if (d < 16) ++dbg_reach_[d];
      // Confidence budget: depth-0 always issues; each deeper step spends the budget -- a strong
      // (confident-direction) step costs cost_strong, a weak (ambiguous) step costs cost_weak. When the
      // budget is exhausted the walk stops, so a run of weak predictions ends it faster than strong ones.
      if (d > 0) {
        budget -= (conf >= gate) ? P.instr_cost_strong : P.instr_cost_weak;
        if (budget < 0) { ++dbg_brk_conf_; break; }
      }
      uint64_t pblk;
      if (xlate(nx_ip, pblk)) { // only issue what we can translate to a physical address
        if (!probe(pblk)) {
          // issue() returns false iff the perceptron gate drops it -> don't mark resident (can be re-proposed/re-gated).
          // Signature = the predicted next-IP block (nx_ip), conf = direction confidence, depth = walk distance.
          if (issue(pblk, nx_ip, conf, d)) { mark(pblk); ++issued_; if (d < 16) ++dbg_issd_[d]; }
        } else ++dbg_probe_;
      } else ++dbg_xmiss_;
      cur = nx_ip;
    }
    // v2 sequential fallback: issue this code page's next instr_nextn physical blocks (short-range residual of
    // multi-line basic blocks the edge walk skips; the shared residency filter squashes the overlap).
    for (int k = 1; k <= P.instr_nextn; ++k) {
      const uint64_t pb = phys_block + static_cast<uint64_t>(k);
      if ((pb >> 6) != (phys_block >> 6)) break; // stay within the 4 KiB code page
      if (!probe(pb)) { if (issue(pb, pb, 1.0, 0)) { mark(pb); ++issued_; } } // sequential residual: sig = next-line target, conf = 1
    }
    last_ip_ = ip_block;
    have_last_ = true;
  }

  // v2 PACKED residency: instead of a private filter, share SPPAM's packed code residency (region prefetch_map,
  // keyed by 4 KiB page, reusing the dead access_map) -> lower redundancy at zero extra state. When these are
  // set (instr_packed_residency), probe()/mark() route through them; else the private filt_ is used.
  void set_shared_residency(std::function<bool(uint64_t)> probe_fn, std::function<void(uint64_t)> mark_fn)
  {
    ext_probe_ = std::move(probe_fn);
    ext_mark_ = std::move(mark_fn);
  }

  // --- stats ---
  uint64_t demands() const { return demands_; }
  uint64_t issued() const { return issued_; }
  uint64_t unencodable() const { return unencodable_; }

private:
  static constexpr uint64_t kEmpty = ~uint64_t{0};
  static constexpr uint16_t kFEmpty = 0xFFFFu; // residency-filter empty sentinel
  enum { kSkip = 0, kNotTaken = 1, kTaken = 2 };
  static std::size_t pow2(std::size_t n)
  {
    std::size_t r = 1;
    while (r < n)
      r <<= 1;
    return r ? r : 1;
  }

  // A BTB entry: one taken-jump target delta + a bimodal direction counter. `tag` (16-bit hash)
  // disambiguates the direct-mapped slot; a tag miss is a cold entry. Only backward/long-forward TAKEN
  // jumps allocate an entry -- fall-through-only blocks stay absent (a miss => confident sequential).
  struct edge {
    uint16_t tag = 0xFFFF;
    bool valid = false;
    int16_t d0 = 0;  // taken-jump target delta (backward, or forward > instr_ft_blocks)
    int8_t dir = 0;  // bimodal Smith counter, state in [0, smax_]; > smid_ => taken, else not-taken
  };

  // Classify an observed IP-block delta from the (noisy) L1I-miss stream.
  int classify(int64_t d) const
  {
    if (d == 0) return kSkip;                 // repeat access to the same block -- not a transition
    if (d >= 1 && d <= ft_) return kNotTaken; // small forward hop = fall-through (L1I-miss noise floor)
    return kTaken;                            // backward (any) or long forward = taken jump
  }
  static bool same_dir(int16_t a, int16_t b) { return (a < 0) == (b < 0); }

  // Train the entry for `last` on its transition delta `d`.
  void train(uint64_t last, int64_t d)
  {
    const int cls = classify(d);
    if (cls == kSkip) return;
    if (cls == kNotTaken) {
      if (edge* e = find(last)) e->dir = static_cast<int8_t>(std::max(int{e->dir} - 1, 0)); // decay toward not-taken; never allocate for fall-through
      return;
    }
    // TAKEN
    if (d <= -dlim_ || d >= dlim_) { ++unencodable_; return; } // target delta not encodable in instr_delta_bits
    bool created = false;
    edge& e = alloc_slot(last, created);
    const int16_t nd = static_cast<int16_t>(d);
    if (created) { e.d0 = nd; e.dir = sinit_; return; }    // first sighting -> weak-taken
    if (same_dir(e.d0, nd)) e.d0 = std::min(e.d0, nd);     // keep min signed delta (closest fwd / furthest bwd)
    else e.d0 = nd;                                        // direction flipped: adopt the new target
    e.dir = static_cast<int8_t>(std::min(int{e.dir} + 1, smax_));
  }

  edge& alloc_slot(uint64_t ib, bool& created)
  {
    edge& e = edges_[static_cast<std::size_t>(hash(ib) & emask_)];
    const uint16_t t = tag_of(ib);
    if (!e.valid || e.tag != t) { e = edge{}; e.tag = t; e.valid = true; created = true; } // evict on collision (BTB-like)
    else created = false;
    return e;
  }
  edge* find(uint64_t ib)
  {
    edge& e = edges_[static_cast<std::size_t>(hash(ib) & emask_)];
    return (e.valid && e.tag == tag_of(ib)) ? &e : nullptr;
  }
  const edge* find(uint64_t ib) const
  {
    const edge& e = edges_[static_cast<std::size_t>(hash(ib) & emask_)];
    return (e.valid && e.tag == tag_of(ib)) ? &e : nullptr;
  }

  // Predicted next IP block + confidence in the predicted DIRECTION. A miss (no taken-entry) is a confident
  // fall-through (sequential run-ahead). A present entry predicts its taken target when the bimodal counter
  // leans taken, else ib+1; confidence is the counter's distance from the midpoint (strong states only).
  std::pair<uint64_t, double> predict(uint64_t ib) const
  {
    const edge* e = find(ib);
    if (!e) return {ib + 1, miss_conf_}; // never jumped here -> sequential guess (miss_conf_ decides strong/weak)
    const double conf = std::abs(e->dir - smid_) / smid_; // distance from the decision midpoint = strength
    if (e->dir > smid_)
      return {ib + static_cast<uint64_t>(static_cast<int64_t>(e->d0)), conf}; // taken (high bit set)
    return {ib + 1, conf};                                                    // not-taken -> fall-through
  }
  static uint16_t tag_of(uint64_t ib) { return static_cast<uint16_t>((hash(ib) >> 17) & 0xFFFFu); }

  // vpage -> ppage translation table (direct-mapped, evict on collision). Filled from every
  // instruction access; used to turn a predicted IP block into a physical block to prefetch.
  void xlate_insert(uint64_t vpage, uint64_t ppage)
  {
    const std::size_t i = static_cast<std::size_t>(hash(vpage) & xmask_);
    xv_[i] = vpage;
    xp_[i] = ppage;
  }
  bool xlate(uint64_t ib, uint64_t& pblk) const
  {
    const uint64_t vpage = ib >> 6; // 64 blocks per 4 KiB page
    const std::size_t i = static_cast<std::size_t>(hash(vpage) & xmask_);
    if (xv_[i] != vpage)
      return false;
    pblk = (xp_[i] << 6) | (ib & 63);
    return true;
  }

  // Residency ("prefetch-map") filter over PHYSICAL blocks -- approximate, bounded. Each bucket
  // holds only a 16-bit tag of the block, not the full address (a tag collision at most costs one
  // redundant/skipped prefetch, never correctness).
  static uint16_t ftag(uint64_t blk) { return static_cast<uint16_t>((hash(blk) >> 13) & 0xFFFFu); }
  bool probe(uint64_t blk) const { if (ext_probe_) return ext_probe_(blk); return filt_[static_cast<std::size_t>(hash(blk) & fmask_)] == ftag(blk); }
  void mark(uint64_t blk) { if (ext_mark_) { ext_mark_(blk); return; } filt_[static_cast<std::size_t>(hash(blk) & fmask_)] = ftag(blk); }

  static uint64_t hash(uint64_t x) { return x * 0x9E3779B97F4A7C15ull; }

  const params& P;
  std::vector<edge> edges_;    // IP-block-keyed BTB (tagged, single delta target + bimodal dir)
  std::vector<uint64_t> xv_, xp_; // vpage->ppage translation (tag / physical page)
  std::vector<uint16_t> filt_; // physical residency filter (16-bit block tag per bucket) -- used when no shared hooks
  std::function<bool(uint64_t)> ext_probe_; // shared (packed) residency probe -- set only when instr_packed_residency
  std::function<void(uint64_t)> ext_mark_;
  std::size_t emask_ = 0, xmask_ = 0, fmask_ = 0;
  int64_t dlim_ = 128;
  int ft_ = 3;         // fall-through forward-delta threshold (blocks)
  int smax_ = 3;       // Smith-counter max state (2-bit -> 3)
  double smid_ = 1.5;  // decision midpoint
  int8_t sinit_ = 2;   // weak-taken state on allocation
  double miss_conf_ = 1.0; // direction confidence for a table miss (sequential run-ahead guess)
  uint64_t last_ip_ = 0;
  bool have_last_ = false;
  uint64_t demands_ = 0, issued_ = 0, unencodable_ = 0;
public:
  mutable uint64_t dbg_reach_[16] = {0}, dbg_issd_[16] = {0}, dbg_brk_conf_ = 0, dbg_xmiss_ = 0, dbg_probe_ = 0;
  void dump_walk() const {
    std::fprintf(stderr, "[bg-walk] reach(iss) by depth: ");
    for (int d = 0; d < 16; ++d) if (dbg_reach_[d]) std::fprintf(stderr, "d%d=%llu(%llu) ", d, (unsigned long long)dbg_reach_[d], (unsigned long long)dbg_issd_[d]);
    std::fprintf(stderr, "| breaks: conf<gate=%llu xlate-miss=%llu probe-hit=%llu\n",
                 (unsigned long long)dbg_brk_conf_, (unsigned long long)dbg_xmiss_, (unsigned long long)dbg_probe_);
  }
private:
};

} // namespace sppam_dse

#endif
