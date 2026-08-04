#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#include <fmt/chrono.h>
#include <fmt/ostream.h>

#include "events.h"
#include "packet_consumer.h"

// The always-on heartbeat listener. It is packet-agnostic: it consumes the generic
// PROGRESS event (a consumer advanced by some amount, in the consumer's own unit) and
// lets the consumer format the line via packet_consumer::progress_message — so a core
// reports "instructions", a hypothetical network consumer reports packets, etc. The
// heartbeat itself knows nothing about instructions.
class Heartbeat
{
public:
  std::ostream* std_out;

  explicit Heartbeat(std::ostream* so) { std_out = so; }

  static constexpr auto cli_key = "Heartbeat";

  uint64_t cycles_between_printouts = 10000000;

  // Per-consumer bookkeeping, indexed by consumer_id and grown on demand.
  std::vector<uint64_t> last_printout_progress;
  std::vector<uint64_t> last_printout_cycles;
  std::vector<uint64_t> phase_start_progress;
  std::vector<uint64_t> phase_start_cycles;
  std::vector<bool> switched_phase; // a BEGIN_PHASE fired since this consumer's last PROGRESS

  template <Event e, typename... Args>
  void handle_event(const Args&... args);

  void add_consumer(std::size_t idx)
  {
    while (idx >= switched_phase.size()) {
      last_printout_progress.push_back(0);
      last_printout_cycles.push_back(0);
      phase_start_progress.push_back(0);
      phase_start_cycles.push_back(0);
      switched_phase.push_back(false);
    }
  }
};

std::chrono::seconds elapsed_time();

namespace heartbeat
{
// Generic fallback: an event this listener does not observe.
template <Event e, typename... Args>
inline void handle_event(Heartbeat* hb, const Args&... args)
{
}

template <>
inline void handle_event<Event::BEGIN_PHASE>(Heartbeat* hb, [[maybe_unused]] const bool& is_warmup)
{
  for (std::size_t i = 0; i < hb->switched_phase.size(); i++) {
    hb->switched_phase[i] = true;
  }
}

template <>
inline void handle_event<Event::PROGRESS>(Heartbeat* hb, const champsim::modules::packet_consumer& consumer, const uint64_t& total_progress,
                                          const uint64_t& total_cycles)
{
  const int id = consumer.consumer_id();
  if (id < 0) {
    return;
  }
  const auto idx = static_cast<std::size_t>(id);
  hb->add_consumer(idx);

  if (hb->switched_phase[idx]) {
    hb->switched_phase[idx] = false;
    hb->phase_start_progress[idx] = total_progress;
    hb->phase_start_cycles[idx] = total_cycles;
  }

  if (total_progress >= hb->last_printout_progress[idx] + hb->cycles_between_printouts) {
    const auto interval_progress = static_cast<double>(total_progress - hb->last_printout_progress[idx]);
    const auto interval_cycles = static_cast<double>(total_cycles - hb->last_printout_cycles[idx]);
    const auto phase_progress = static_cast<double>(total_progress - hb->phase_start_progress[idx]);
    const auto phase_cycles = static_cast<double>(total_cycles - hb->phase_start_cycles[idx]);

    const auto msg = consumer.progress_message(total_progress, total_cycles, interval_progress / interval_cycles, phase_progress / phase_cycles);
    if (!msg.empty()) {
      fmt::print(*(hb->std_out), "{} (Simulation time: {:%H hr %M min %S sec})\n", msg, elapsed_time());
    }

    // Advance the baseline by whole interval multiples rather than snapping to the current
    // count: snapping would accumulate per-heartbeat overshoot and drift the schedule.
    const auto overshoot = total_progress - hb->last_printout_progress[idx];
    hb->last_printout_progress[idx] += (overshoot / hb->cycles_between_printouts) * hb->cycles_between_printouts;
    hb->last_printout_cycles[idx] = total_cycles;
  }
}
} // namespace heartbeat

template <Event e, typename... Args>
void Heartbeat::handle_event(const Args&... args)
{
  heartbeat::handle_event<e>(this, args...);
}

#endif
