/*
 * The default heartbeat listener: prints a periodic progress line for each
 * source consumer that emits progress events.
 *
 * The listener owns the interval bookkeeping (baselines, phase resets, the
 * overshoot-preserving baseline advance); each consumer owns the wording of
 * its line via source_consumer::progress_message, since only the consumer
 * knows its token unit ("instructions" for a core).
 *
 * Parameters (from ModuleBuilder):
 *   - interval (uint64, default 10000000): tokens between printouts
 *     (main.cc maps the root config key "heartbeat_frequency" here)
 *   - output_stream (std::ostream*, optional, default std::cout):
 *     C++-side hook for tests; not expressible from JSON
 */

#include <chrono>
#include <iostream>
#include <vector>
#include <fmt/chrono.h>
#include <fmt/ostream.h>

#include "modules.h"

std::chrono::seconds elapsed_time();

namespace
{

class heartbeat_listener : public champsim::modules::listener
{
  uint64_t interval_ = 10000000;
  std::ostream* out_ = &std::cout;

  // Per-source bookkeeping, indexed by source_id (grown on demand).
  struct tracking {
    uint64_t last_printout_progress = 0;
    uint64_t last_printout_cycles = 0;
    uint64_t phase_start_progress = 0;
    uint64_t phase_start_cycles = 0;
    bool switched_phase = false; // true if a phase began since this source's last progress event
  };
  std::vector<tracking> sources_;

public:
  explicit heartbeat_listener(champsim::modules::ModuleBuilder builder)
      : interval_(builder.get_parameter<uint64_t>("interval", true, 10000000ULL)),
        out_(builder.get_parameter<std::ostream*>("output_stream", true, &std::cout))
  {
  }

  void begin_phase(bool /*is_warmup*/) override
  {
    for (auto& t : sources_) {
      t.switched_phase = true;
    }
  }

  void progress(const champsim::modules::source_consumer& consumer, uint64_t total_progress, uint64_t total_cycles) override
  {
    int idx = consumer.consumer_id();
    if (idx < 0) {
      return;
    }
    if (static_cast<std::size_t>(idx) >= sources_.size()) {
      sources_.resize(static_cast<std::size_t>(idx) + 1);
    }
    auto& t = sources_[static_cast<std::size_t>(idx)];

    if (t.switched_phase) {
      t.switched_phase = false;
      t.phase_start_progress = total_progress;
      t.phase_start_cycles = total_cycles;
    }

    if (total_progress >= t.last_printout_progress + interval_) {
      auto interval_progress = static_cast<double>(total_progress - t.last_printout_progress);
      auto interval_cycles = static_cast<double>(total_cycles - t.last_printout_cycles);
      auto phase_progress = static_cast<double>(total_progress - t.phase_start_progress);
      auto phase_cycles = static_cast<double>(total_cycles - t.phase_start_cycles);

      auto msg = consumer.progress_message(total_progress, total_cycles, interval_progress / interval_cycles, phase_progress / phase_cycles);
      if (!msg.empty()) {
        fmt::print(*out_, "{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
      }

      // Advance the baseline by exact multiples of the interval rather than
      // snapping to the current count. When more than one token completes in
      // the same cycle, the total can overshoot the threshold; snapping would
      // accumulate that overshoot every heartbeat and permanently displace
      // when subsequent heartbeats fire.
      auto overshoot = total_progress - t.last_printout_progress;
      t.last_printout_progress += (overshoot / interval_) * interval_;
      t.last_printout_cycles = total_cycles;
    }
  }
};

static champsim::modules::listener::register_module<heartbeat_listener> heartbeat_reg("HEARTBEAT");

} // anonymous namespace
