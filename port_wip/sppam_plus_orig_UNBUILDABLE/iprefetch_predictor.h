#ifndef SPPAM_IPREFETCH_PREDICTOR_H
#define SPPAM_IPREFETCH_PREDICTOR_H

#include <cstdint>
#include <utility>
#include <vector>

#include "params.h"

namespace sppam_dse
{
// Branch-graph L2 instruction prefetcher (prototyped in tools/sppam_dse iproto4).
//
// Instruction fetches have ip == v_address, so the branch graph lives in IP (virtual) space
// where successor deltas are compact -- NOT physical space, where a cross-page branch has an
// arbitrary huge physical delta. The graph learns, per IP block, its top-2 successor IP DELTAS
// (small, so 8 bits covers the near control flow that matters). A tiny vpage->ppage table,
// filled from the instruction stream we already see (each packet carries both ip and paddr),
// translates a predicted IP block to a physical block at issue: ip -> hash -> edge -> ip+delta
// -> translate -> prefetch -> repeat, confidence-gated as deep as the edge bias stays strong.
//
// Encoding validated in the DSE: cov is neutral vs an unbounded full-precision table (far
// cross-page targets the 8-bit delta drops are hard to prefetch usefully anyway); 512-1024
// edges and a 64-entry translation table saturate coverage; xlate-hit ~99%. ~9 KiB total.
//
// Fully separate from the data region/access maps. Its own bounded physical residency filter
// stops re-prefetching lines already in (or on the way to) the cache.
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
  }

  // One instruction (L1I-miss) access: `ip_block` = ip>>6 (virtual, the branch-graph key),
  // `phys_block` = physical block (what the cache holds / we prefetch). issue(phys_block)
  // prefetches into L2.
  template <class IssueFn>
  void operate(uint64_t ip_block, uint64_t phys_block, const IssueFn& issue)
  {
    ++demands_;
    xlate_insert(ip_block >> 6, phys_block >> 6); // learn this instruction page's translation
    if (have_last_) {
      const int64_t d = static_cast<int64_t>(ip_block) - static_cast<int64_t>(last_ip_);
      if (d > -dlim_ && d < dlim_)
        slot_for_train(last_ip_).obs(static_cast<int16_t>(d)); // train edge last_ip -> ip_block
      else
        ++unencodable_;
    }
    mark(phys_block); // the demanded block is (being) fetched -> resident

    const double gate = static_cast<double>(P.instr_conf) / 100.0;
    uint64_t cur = ip_block;
    for (int d = 0; d < P.instr_la_depth; ++d) {
      auto [nx_ip, conf] = predict(cur);
      if (d > 0 && conf < gate)
        break; // depth-1 always issues; deeper walk gated on edge confidence
      uint64_t pblk;
      if (xlate(nx_ip, pblk)) { // only issue what we can translate to a physical address
        if (!probe(pblk)) {
          issue(pblk);
          mark(pblk);
          ++issued_;
        }
      }
      cur = nx_ip;
    }
    last_ip_ = ip_block;
    have_last_ = true;
  }

  // --- stats ---
  uint64_t demands() const { return demands_; }
  uint64_t issued() const { return issued_; }
  uint64_t unencodable() const { return unencodable_; }

private:
  static constexpr uint64_t kEmpty = ~uint64_t{0};
  static constexpr uint16_t kFEmpty = 0xFFFFu; // residency-filter empty sentinel
  static std::size_t pow2(std::size_t n)
  {
    std::size_t r = 1;
    while (r < n)
      r <<= 1;
    return r ? r : 1;
  }

  // A branch-graph edge: the two most-frequent successor IP DELTAS of an IP block, with
  // saturating counts (dominant kept in slot 0). `tag` (16-bit hash) disambiguates the
  // direct-mapped slot; a tag miss is a cold entry.
  struct edge {
    uint16_t tag = 0xFFFF;
    bool valid = false;
    int16_t d0 = 0, d1 = 0;
    uint8_t c0 = 0, c1 = 0;
    static constexpr uint8_t kCntMax = 15; // 4-bit saturating counters
    void obs(int16_t d)
    {
      if (c0 && d == d0) { if (c0 < kCntMax) ++c0; }
      else if (c1 && d == d1) { if (c1 < kCntMax) ++c1; }
      else if (c0 == 0) { d0 = d; c0 = 1; }
      else if (c1 == 0) { d1 = d; c1 = 1; }
      else if (c0 <= c1) { if (--c0 == 0) { d0 = d; c0 = 1; } }
      else if (--c1 == 0) { d1 = d; c1 = 1; }
      if (c1 > c0) { // keep the dominant successor in slot 0
        std::swap(d0, d1);
        std::swap(c0, c1);
      }
    }
  };

  edge& slot_for_train(uint64_t ib)
  {
    edge& e = edges_[static_cast<std::size_t>(hash(ib) & emask_)];
    const uint16_t t = tag_of(ib);
    if (!e.valid || e.tag != t) { e = edge{}; e.tag = t; e.valid = true; } // evict on collision (BTB-like)
    return e;
  }
  // Predicted next IP block + confidence (dominant-edge share). Cold slot -> (ib+1, 0).
  std::pair<uint64_t, double> predict(uint64_t ib) const
  {
    const edge& e = edges_[static_cast<std::size_t>(hash(ib) & emask_)];
    if (!e.valid || e.tag != tag_of(ib) || e.c0 == 0)
      return {ib + 1, 0.0};
    const double tot = static_cast<double>(e.c0) + e.c1;
    return {ib + static_cast<uint64_t>(static_cast<int64_t>(e.d0)), tot > 0.0 ? e.c0 / tot : 0.0};
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
  bool probe(uint64_t blk) const { return filt_[static_cast<std::size_t>(hash(blk) & fmask_)] == ftag(blk); }
  void mark(uint64_t blk) { filt_[static_cast<std::size_t>(hash(blk) & fmask_)] = ftag(blk); }

  static uint64_t hash(uint64_t x) { return x * 0x9E3779B97F4A7C15ull; }

  const params& P;
  std::vector<edge> edges_;    // IP-keyed branch-edge table (tagged, delta successors)
  std::vector<uint64_t> xv_, xp_; // vpage->ppage translation (tag / physical page)
  std::vector<uint16_t> filt_; // physical residency filter (16-bit block tag per bucket)
  std::size_t emask_ = 0, xmask_ = 0, fmask_ = 0;
  int64_t dlim_ = 128;
  uint64_t last_ip_ = 0;
  bool have_last_ = false;
  uint64_t demands_ = 0, issued_ = 0, unencodable_ = 0;
};

} // namespace sppam_dse

#endif
