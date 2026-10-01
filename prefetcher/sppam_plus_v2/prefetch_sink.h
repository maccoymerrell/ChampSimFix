// Interface a predictor uses to issue prefetches into the evaluator's L2/LLC.
#ifndef SPPAM_DSE_PREFETCH_SINK_H
#define SPPAM_DSE_PREFETCH_SINK_H

#include <cstdint>

namespace sppam_dse
{
struct prefetch_sink {
  virtual ~prefetch_sink() = default;
  // fill_l2: place in L2 (timely target); otherwise place in LLC only.
  // from_spp: tag the source as the SPP delta engine (for its usefulness feedback).
  // benefit: expected usefulness in [0,1] (coverage/bandwidth proxy) for the shared
  // bandwidth market; 1.0 = always admit.
  // gen_tag: generation attributes for accuracy attribution (bit0 source, bits1-4 lookahead depth,
  // bits5-7 pattern order, bit8 default-prediction path, bits9-11 scan distance).
  // Returns true iff the block was actually placed in L2 -> the predictor marks its residency map only then
  // (a dropped / LLC-only / squashed prefetch returns false, so no stale "issued but never filled" bit).
  virtual bool issue_prefetch(uint64_t block, bool fill_l2, bool from_spp = false, double benefit = 1.0, uint32_t gen_tag = 0) = 0;
  // Perceptron gate for engines that live OUTSIDE the sppam_predictor (the separate SPP delta/signature engine).
  // Default KEEPS (no perceptron). The glue delegates to the perceptron so all 3 engines pass through one filter;
  // engine ids match perc_score (2 = SPP/delta). perc_note_issue_ext snapshots the gated features for training.
  virtual bool perc_gate(uint64_t block, int engine, int depth, uint64_t pc, int conf, uint64_t sig) { return true; }
  virtual void perc_note_issue_ext(uint64_t block) {}
  // Aggregate DRAM bandwidth utilization, 0..15 (0 = idle). Drives the SPPAM/SPP
  // bandwidth-feedback throttle; default 0 = no throttle for non-timing sinks.
  virtual int dram_bw_index() const { return 0; }
  // Free L2 MSHR/PQ headroom (max(mshr_size - mshr_occupancy - pq_occupancy, 0)) -- the number of
  // prefetches per trigger that can fill L2 before it overflows to LLC-only (orig SPPAM placement).
  // Default large -> fill all to L2 (non-timing sinks / when unused).
  virtual int pf_free_space() const { return 1 << 20; }
  // Adaptive L2 fill depth (per-trigger prefetches to L2 before LLC overflow), walked by the
  // set-dueling throttle. Default large -> fill all to L2 (set-duel off / non-sppam sinks).
  virtual int sd_l2_limit() const { return 1 << 20; }
  // PE-gated ramp: net-useful AND DRAM-bound -> ramp aggression + spill dense tail to LLC.
  virtual bool pe_ramp_active() const { return false; }
  // Cache-stress gate for the per-IP depth throttle: untimeliness only matters when the L2 is not thrashing
  // and the program has little memory-level parallelism. Default open.
  virtual bool depth_throttle_allowed() const { return true; }
  // A sampled SPP prefetch resolved (used, or evicted/timed out unused): SPP's usefulness feedback.
  virtual void on_spp_sample(uint64_t /*block*/, bool /*useful*/) {}
  // A request for this block is pending at the cache: queued prefetch, in tag check, in the MSHR, or returning.
  virtual bool request_pending(uint64_t /*block*/) const { return false; }
};
} // namespace sppam_dse

#endif
