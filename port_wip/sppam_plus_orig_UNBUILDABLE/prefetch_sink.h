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
  virtual void issue_prefetch(uint64_t block, bool fill_l2, bool from_spp = false, double benefit = 1.0, uint32_t gen_tag = 0) = 0;
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
  // Per-trigger-IP lookahead-depth cap: as an IP's usefulness degrades, prefetch SHALLOWER (untimely deep
  // prefetches -> timely near ones) instead of dropping volume. Default no cap (large). Reads the current
  // trigger IP held by the sink, so the predictor just min()s eff_depth against it.
  virtual int ip_depth_cap() const { return 1 << 20; }
};
} // namespace sppam_dse

#endif
