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

#include "modules.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <vector>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <nlohmann/json.hpp>

namespace {

class instruction_phase_controller : public champsim::modules::phase_controller {
  // Configuration (from builder)
  int deadlock_cycles_ = 500;
  uint64_t livelock_period_ = 10000000;
  std::vector<double> livelock_thresholds_{0.01, 0.02, 0.05};

  // Per-phase state
  std::string phase_name_;
  uint64_t length_ = 0;

  // Configurable phase list (set at construction from builder params)
  std::vector<champsim::phase_info> phases_;

  int stalled_cycles_ = 0;
  uint64_t livelock_timer_ = 0;
  bool livelock_triggered_ = false;

  // Entity tracking: keyed by entity_index from source_consumer
  std::map<int, bool> entity_complete_;
  std::map<int, uint64_t> progress_baseline_;
  std::map<int, uint64_t> livelock_progress_;
  std::vector<unsigned> newly_completed_;
  bool trace_eof_ = false;

public:
  explicit instruction_phase_controller(champsim::modules::ModuleBuilder builder)
  {
    deadlock_cycles_ = builder.get_parameter<int>("deadlock_cycles", true, 500);
    livelock_period_ = builder.get_parameter<uint64_t>("livelock_period", true, 10000000ULL);

    // Build the phases list from explicit JSON phases array if provided,
    // otherwise fall back to {warmup_length, simulation_length} scalar params.
    if (builder.has_parameter("phases")) {
      for (auto& p : builder.get_parameter<nlohmann::json>("phases")) {
        champsim::phase_info pi;
        pi.name       = p.value("name", "Phase");
        pi.is_warmup  = p.value("is_warmup", false);
        pi.length     = p.value("length", uint64_t{0});
        phases_.push_back(pi);
      }
    } else if (builder.has_parameter("warmup_length") || builder.has_parameter("simulation_length")) {
      uint64_t wlen = builder.get_parameter<uint64_t>("warmup_length", true, 0ULL);
      uint64_t slen = builder.get_parameter<uint64_t>("simulation_length", true, 0ULL);
      phases_ = {
        champsim::phase_info{"Warmup",     true,  wlen, {}, {}},
        champsim::phase_info{"Simulation", false, slen, {}, {}},
      };
    }
    // If neither is set, phases_ stays empty — caller owns the phase list.
  }

  void begin_phase(const std::string& name, bool /*is_warmup*/, uint64_t length) override
  {
    phase_name_ = name;
    length_ = length;
    stalled_cycles_ = 0;
    livelock_timer_ = 0;
    livelock_triggered_ = false;
    trace_eof_ = false;
    newly_completed_.clear();
    entity_complete_.clear();
    livelock_progress_.clear();

    // Discover tracked entities from source_consumers
    for (auto& sc : intern_->typed_view<champsim::modules::source_consumer>("source_consumer")) {
      int idx = sc.get().entity_index();
      if (idx >= 0) {
        entity_complete_[idx] = false;
        progress_baseline_[idx] = sc.get().sim_progress();
        livelock_progress_[idx] = sc.get().sim_progress();
      }
    }
  }

  status advance(long progress) override
  {
    newly_completed_.clear();

    // Deadlock detection
    if (progress == 0) {
      ++stalled_cycles_;
    } else {
      stalled_cycles_ = 0;
    }

    // Livelock detection via source_consumer::sim_progress()
    livelock_timer_++;
    if (livelock_timer_ >= livelock_period_) {
      for (auto& sc : intern_->typed_view<champsim::modules::source_consumer>("source_consumer")) {
        int idx = sc.get().entity_index();
        if (idx < 0) continue;
        uint64_t cur_progress = sc.get().sim_progress();
        for (auto thres = livelock_thresholds_.begin(); thres != livelock_thresholds_.end(); ++thres) {
          double rate = std::ceil(static_cast<double>(cur_progress - livelock_progress_[idx])) / std::ceil(static_cast<double>(livelock_period_));
          if (rate <= *thres) {
            auto dist = std::distance(livelock_thresholds_.begin(), thres);
            if (dist == 0) {
              livelock_triggered_ = true;
              fmt::print("{} entity {} panic: progress rate {:.5g} < {:.5g}\n", phase_name_, idx, rate, *thres);
            } else if (dist == 1) {
              fmt::print("{} entity {} critical: progress rate {:.5g} < {:.5g}\n", phase_name_, idx, rate, *thres);
            } else {
              fmt::print("{} entity {} warning: progress rate {:.5g} < {:.5g}\n", phase_name_, idx, rate, *thres);
            }
            break;
          }
        }
        livelock_progress_[idx] = cur_progress;
      }
      livelock_timer_ = 0;
    }

    if (stalled_cycles_ >= deadlock_cycles_ || livelock_triggered_) {
      return status::ABORT;
    }

    // Trace EOF: mark all incomplete entities as complete
    if (trace_eof_) {
      for (auto& [idx, complete] : entity_complete_) {
        if (!complete) {
          complete = true;
          newly_completed_.push_back(static_cast<unsigned>(idx));
        }
      }
    }

    // Progress-based completion: length_ is steps in THIS phase (not total cumulative)
    for (auto& sc : intern_->typed_view<champsim::modules::source_consumer>("source_consumer")) {
      int idx = sc.get().entity_index();
      if (idx < 0) continue;
      if (!entity_complete_[idx] && (sc.get().sim_progress() - progress_baseline_[idx]) >= length_) {
        entity_complete_[idx] = true;
        newly_completed_.push_back(static_cast<unsigned>(idx));
      }
    }

    bool all_complete = std::all_of(entity_complete_.begin(), entity_complete_.end(),
                                     [](const auto& p) { return p.second; });
    return all_complete ? status::COMPLETE : status::CONTINUE;
  }

  std::vector<unsigned> newly_completed_entities() const override
  {
    return newly_completed_;
  }

  void notify_trace_eof() override
  {
    trace_eof_ = true;
  }

  void end_phase() override
  {
    // Nothing to clean up
  }

  std::vector<champsim::phase_info> get_phases() const override { return phases_; }
};

// Register interface and model
static champsim::modules::phase_controller::register_interface phase_controller_iface_reg("phase_controller");
static champsim::modules::phase_controller::register_module<instruction_phase_controller> instruction_pc_reg("instruction_phase_controller");

} // anonymous namespace
