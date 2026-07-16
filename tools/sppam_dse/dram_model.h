// Rough DRAM bandwidth -> latency model.
//
// We do not model timing in detail; instead we estimate the DRAM request rate
// with an exponential moving average (O(1) per access) and map utilization to a
// latency with a queueing-style curve: latency = base / (1 - min(util, cap)).
// util = rate * service_cycles, where service_cycles is the per-line service
// time at peak bandwidth. All constants are configurable.
#ifndef SPPAM_DSE_DRAM_MODEL_H
#define SPPAM_DSE_DRAM_MODEL_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sppam_dse
{

struct dram_params {
  double base_latency = 100.0;   // unloaded DRAM latency (cpu cycles)
  double service_cycles = 7.0;   // cycles to serve one 64B line at peak BW (~4800 MT/s, 1 ch)
  double tau = 200.0;            // EMA time constant (cycles) for the rate estimate
  double util_cap = 0.95;        // clamp utilization to avoid blow-up
  double max_latency = 100000.0; // hard cap on returned latency
};

class dram_model
{
public:
  explicit dram_model(const dram_params& p = {}) : p_(p) {}

  // Records a DRAM access at `cycle` and returns its modeled latency (cycles).
  // is_demand: the access is a demand miss (top-priority traffic), tracked
  // separately so the bandwidth market can price demand's claim on bandwidth.
  double access(uint64_t cycle, bool is_demand = true)
  {
    if (have_last_) {
      double dt = static_cast<double>(cycle) - static_cast<double>(last_cycle_);
      if (dt < 0)
        dt = 0;
      // Decay the running rates, then add this access's contribution.
      double decay = std::exp(-dt / p_.tau);
      rate_ *= decay;
      demand_rate_ *= decay;
    }
    rate_ += 1.0 / p_.tau;
    if (is_demand)
      demand_rate_ += 1.0 / p_.tau;
    last_cycle_ = cycle;
    have_last_ = true;

    double util = std::min(rate_ * p_.service_cycles, p_.util_cap);
    double lat = p_.base_latency / (1.0 - util);
    return std::min(lat, p_.max_latency);
  }

  // Current (aggregate) DRAM bandwidth utilization, 0..util_cap. Used for the
  // SPPAM bandwidth-feedback throttle. Slightly stale (as of the last access).
  double utilization() const { return std::min(rate_ * p_.service_cycles, p_.util_cap); }
  // Utilization due to DEMAND traffic only — demand's claim on the bandwidth,
  // which prefetches must out-benefit to be admitted by the market.
  double demand_utilization() const { return std::min(demand_rate_ * p_.service_cycles, p_.util_cap); }

private:
  dram_params p_;
  double rate_ = 0.0;
  double demand_rate_ = 0.0;
  uint64_t last_cycle_ = 0;
  bool have_last_ = false;
};

} // namespace sppam_dse

#endif
