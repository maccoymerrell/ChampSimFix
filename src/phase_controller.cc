/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "phase_controller.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "modules.h"

namespace
{

// Generic, packet-agnostic phase controller owning completion/deadlock/health
// mechanics; per-consumer policy (e.g. livelock rate) lives in check_health.
// Params: deadlock_cycles, health_period (alias livelock_period), eof_policy
// (complete_all|complete_consumer), phases[] or warmup_length/simulation_length.
class default_phase_controller : public champsim::modules::phase_controller
{
  using consumer_health = champsim::modules::packet_consumer::consumer_health;

  // Configuration (from builder)
  int deadlock_cycles_ = 500;
  uint64_t health_period_ = 10000000;
  bool complete_all_on_eof_ = true;

  // Parent environment, captured at construction.
  champsim::modules::environment_module* env_ = nullptr;

  // Per-phase caches (typed_view is expensive). Refreshed once per begin_phase().
  std::vector<std::reference_wrapper<champsim::modules::packet_consumer>> packet_consumers_;
  // Deadlock veto: operables with timer-scheduled work (e.g. DRAM refresh).
  std::vector<std::reference_wrapper<champsim::operable>> operables_;

  // Per-phase state
  std::string phase_name_;
  uint64_t length_ = 0;

  // Configurable phase list (set at construction from builder params)
  std::vector<champsim::phase_info> phases_;

  int stalled_cycles_ = 0;
  uint64_t health_timer_ = 0;
  bool health_abort_ = false;

  // Source ids this controller governs; empty = all.
  std::set<int> governed_;

  // Source tracking: keyed by producer_id from packet_consumer
  std::map<int, bool> consumer_complete_;
  std::map<int, uint64_t> progress_baseline_;
  std::vector<unsigned> newly_completed_;

  bool governs(int idx) const { return governed_.empty() || governed_.count(idx) > 0; }

public:
  explicit default_phase_controller(champsim::modules::ModuleBuilder builder)
  {
    env_ = builder.get_parent<champsim::modules::environment_module>();
    deadlock_cycles_ = builder.get_parameter<int>("deadlock_cycles", true, 500);
    health_period_ = builder.get_parameter<uint64_t>("health_period", true, builder.get_parameter<uint64_t>("livelock_period", true, 10000000ULL));
    complete_all_on_eof_ = builder.get_parameter<std::string>("eof_policy", true, std::string{"complete_all"}) != "complete_consumer";

    // Build phases from explicit JSON array, else warmup/simulation scalars.
    if (builder.has_parameter("phases")) {
      for (auto& p : builder.get_parameter<nlohmann::json>("phases")) {
        champsim::phase_info pi;
        pi.name = p.value("name", "Phase");
        pi.is_warmup = p.value("is_warmup", false);
        pi.roi = p.value("roi", !pi.is_warmup);
        pi.length = p.value("length", uint64_t{0});
        phases_.push_back(pi);
      }
    } else if (builder.has_parameter("warmup_length") || builder.has_parameter("simulation_length")) {
      uint64_t wlen = builder.get_parameter<uint64_t>("warmup_length", true, 0ULL);
      uint64_t slen = builder.get_parameter<uint64_t>("simulation_length", true, 0ULL);
      phases_ = {
          champsim::phase_info{"Warmup", true, false, wlen},
          champsim::phase_info{"Simulation", false, true, slen},
      };
    }
    // If neither is set, phases_ stays empty — caller owns the phase list.

    // Optional source-id subset: controllers can partition a run's sources,
    // each applying its own policy. Default: govern all.
    if (builder.has_parameter("consumers")) {
      for (auto& s : builder.get_parameter<nlohmann::json>("consumers")) {
        governed_.insert(s.get<int>());
      }
    }
  }

  void begin_phase(const std::string& name, bool /*is_warmup*/, uint64_t length) override
  {
    phase_name_ = name;
    length_ = length;
    stalled_cycles_ = 0;
    health_timer_ = 0;
    health_abort_ = false;
    newly_completed_.clear();
    consumer_complete_.clear();

    // Refresh per-phase view caches (typed_view is expensive; reuse across cycles).
    packet_consumers_ = env_->typed_view<champsim::modules::packet_consumer>("packet_consumer");
    operables_ = env_->typed_view<champsim::operable>("operable");

    // Restrict to governed consumers, then re-baseline progress and health.
    if (!governed_.empty()) {
      packet_consumers_.erase(
          std::remove_if(std::begin(packet_consumers_), std::end(packet_consumers_), [this](const auto& sc) { return !governs(sc.get().consumer_id()); }),
          std::end(packet_consumers_));
    }
    for (auto& sc : packet_consumers_) {
      int idx = sc.get().consumer_id();
      if (idx >= 0) {
        consumer_complete_[idx] = false;
        progress_baseline_[idx] = sc.get().sim_progress();
      }
      sc.get().reset_health();
    }
  }

  status advance(long progress) override
  {
    newly_completed_.clear();

    // Deadlock: consecutive zero-progress cycles are a hang unless a consumer
    // or operable has scheduled work pending (e.g. a DRAM refresh in flight).
    if (progress == 0) {
      const bool pending = std::any_of(std::begin(packet_consumers_), std::end(packet_consumers_), [](const auto& sc) { return sc.get().has_pending_work(); })
                           || std::any_of(std::begin(operables_), std::end(operables_), [](const auto& op) { return op.get().has_pending_work(); });
      stalled_cycles_ = pending ? 0 : stalled_cycles_ + 1;
    } else {
      stalled_cycles_ = 0;
    }

    // Health aggregation: each consumer judges itself over the window.
    if (++health_timer_ >= health_period_) {
      for (auto& sc : packet_consumers_) {
        if (sc.get().consumer_id() < 0) {
          continue;
        }
        auto health = sc.get().check_health(health_period_);
        if (health == consumer_health::stalled) {
          fmt::print("{} source {} reported stalled\n", phase_name_, sc.get().consumer_id());
          health_abort_ = true;
        }
      }
      health_timer_ = 0;
    }

    if (stalled_cycles_ >= deadlock_cycles_ || health_abort_) {
      return status::ABORT;
    }

    // Completion: per-producer EOF (policy-dependent) and progress thresholds.
    for (auto& sc : packet_consumers_) {
      int idx = sc.get().consumer_id();
      if (idx < 0) {
        continue;
      }
      if (consumer_complete_[idx]) {
        continue;
      }

      if (sc.get().producers_eof()) {
        if (complete_all_on_eof_) {
          // Classic behavior: the first exhausted source ends the phase for everyone.
          for (auto& [other_idx, complete] : consumer_complete_) {
            if (!complete) {
              complete = true;
              newly_completed_.push_back(static_cast<unsigned>(other_idx));
            }
          }
          break;
        }
        consumer_complete_[idx] = true;
        newly_completed_.push_back(static_cast<unsigned>(idx));
        continue;
      }

      if ((sc.get().sim_progress() - progress_baseline_[idx]) >= length_) {
        consumer_complete_[idx] = true;
        newly_completed_.push_back(static_cast<unsigned>(idx));
      }
    }

    bool all_complete = !consumer_complete_.empty() && std::all_of(consumer_complete_.begin(), consumer_complete_.end(), [](const auto& p) { return p.second; });
    return all_complete ? status::COMPLETE : status::CONTINUE;
  }

  std::vector<unsigned> newly_completed_consumers() const override { return newly_completed_; }

  void end_phase() override
  {
    packet_consumers_.clear();
    operables_.clear();
  }

  std::vector<champsim::phase_info> get_phases() const override { return phases_; }
};

// Register interface + model. INSTRUCTION_PHASE_CONTROLLER stays as a
// back-compat alias; the controller itself is packet-agnostic.
static champsim::modules::phase_controller::register_interface phase_controller_iface_reg("phase_controller");
static champsim::modules::phase_controller::register_module<default_phase_controller> default_pc_reg("PHASE_CONTROLLER");
static champsim::modules::phase_controller::register_module<default_phase_controller> instruction_pc_reg("INSTRUCTION_PHASE_CONTROLLER");

} // anonymous namespace
