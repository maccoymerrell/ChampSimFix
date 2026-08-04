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
#include <numeric>
#include <set>
#include <string>
#include <vector>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

#include "module_phase.h"
#include "modules.h"

namespace
{

// Generic, packet-agnostic phase controller owning completion/deadlock/health
// mechanics; per-consumer policy (e.g. livelock rate) lives in check_health.
// Params: deadlock_cycles, health_period, eof_policy
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
  int phase_idx_ = -1; // index into phases_; begin_phase() steps it

  // module_phase operables, informed of each phase's begin/end. Cached lazily on first begin_phase().
  std::vector<std::reference_wrapper<champsim::module_phase>> phase_modules_;
  bool phase_modules_cached_ = false;

  // Configurable phase list (set at construction from builder params)
  std::vector<champsim::phase_info> phases_;

  int stalled_cycles_ = 0;
  uint64_t health_timer_ = 0;
  bool health_abort_ = false;

  // Consumer ids this controller governs; empty = all.
  std::set<int> governed_;

  // Consumer tracking, flattened for the per-cycle advance() path (map lookups were measured overhead). Ordering is
  // load-bearing: tracked_ keeps consumer discovery order (the completion scan order); tracked_by_idx_ is sorted by
  // consumer id for the id-ordered complete-all-on-EOF notification.
  struct tracked_consumer {
    champsim::modules::packet_consumer* consumer;
    int idx;
    uint64_t baseline;
    bool complete;
  };
  std::vector<tracked_consumer> tracked_;
  std::vector<std::size_t> tracked_by_idx_;
  std::size_t incomplete_count_ = 0;
  std::vector<unsigned> newly_completed_;

  bool governs(int idx) const { return governed_.empty() || governed_.count(idx) > 0; }

public:
  explicit default_phase_controller(champsim::modules::ModuleBuilder builder)
  {
    env_ = builder.get_parent<champsim::modules::environment_module>();
    // Default to the environment's dynamic threshold; a config "deadlock_cycles" overrides.
    deadlock_cycles_ = builder.get_parameter<int>("deadlock_cycles", true, env_->get_deadlock_cycles());
    health_period_ = builder.get_parameter<uint64_t>("health_period", true, 10000000ULL);
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

    // Optional consumer-id subset: controllers can partition a run's consumers,
    // each applying its own policy. Default: govern all.
    if (builder.has_parameter("consumers")) {
      for (auto& s : builder.get_parameter<nlohmann::json>("consumers")) {
        governed_.insert(s.get<int>());
      }
    }
  }

  void begin_phase() override
  {
    // Cache the module_phase operables on the first call (all modules are built by now).
    if (!phase_modules_cached_) {
      for (auto& op : env_->typed_view<champsim::operable>("operable")) {
        if (auto* mp = dynamic_cast<champsim::module_phase*>(&op.get())) {
          phase_modules_.emplace_back(*mp);
        }
      }
      phase_modules_cached_ = true;
    }

    const auto& phase = phases_.at(static_cast<std::size_t>(++phase_idx_));
    for (auto& mp : phase_modules_) {
      mp.get().begin_phase(phase.is_warmup, phase.roi);
    }

    phase_name_ = phase.name;
    length_ = phase.length;
    stalled_cycles_ = 0;
    health_timer_ = 0;
    health_abort_ = false;
    newly_completed_.clear();
    tracked_.clear();
    tracked_by_idx_.clear();
    incomplete_count_ = 0;

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
        tracked_.push_back({&sc.get(), idx, sc.get().sim_progress(), false});
      }
      sc.get().reset_health();
    }
    incomplete_count_ = std::size(tracked_);
    tracked_by_idx_.resize(std::size(tracked_));
    std::iota(std::begin(tracked_by_idx_), std::end(tracked_by_idx_), std::size_t{0});
    std::sort(std::begin(tracked_by_idx_), std::end(tracked_by_idx_),
              [this](std::size_t lhs, std::size_t rhs) { return tracked_[lhs].idx < tracked_[rhs].idx; });
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
          fmt::print("{} consumer {} reported stalled\n", phase_name_, sc.get().consumer_id());
          health_abort_ = true;
        }
      }
      health_timer_ = 0;
    }

    if (stalled_cycles_ >= deadlock_cycles_ || health_abort_) {
      return status::ABORT;
    }

    // Completion: per-producer EOF (policy-dependent) and progress thresholds.
    for (auto& tracked : tracked_) {
      if (tracked.complete) {
        continue;
      }

      if (tracked.consumer->producers_eof()) {
        if (complete_all_on_eof_) {
          // Classic behavior: the first exhausted producer ends the phase for everyone.
          for (auto pos : tracked_by_idx_) {
            auto& other = tracked_[pos];
            if (!other.complete) {
              other.complete = true;
              --incomplete_count_;
              newly_completed_.push_back(static_cast<unsigned>(other.idx));
            }
          }
          break;
        }
        tracked.complete = true;
        --incomplete_count_;
        newly_completed_.push_back(static_cast<unsigned>(tracked.idx));
        continue;
      }

      if ((tracked.consumer->sim_progress() - tracked.baseline) >= length_) {
        tracked.complete = true;
        --incomplete_count_;
        newly_completed_.push_back(static_cast<unsigned>(tracked.idx));
      }
    }

    bool all_complete = !tracked_.empty() && incomplete_count_ == 0;
    if (!all_complete) {
      return status::CONTINUE;
    }

    // Phase complete: end it on the operables, then report whether more phases remain.
    for (auto& mp : phase_modules_) {
      mp.get().end_phase();
    }
    return (phase_idx_ + 1 < static_cast<int>(phases_.size())) ? status::PHASE_COMPLETE : status::DONE;
  }

  const champsim::phase_info& phase() const override { return phases_.at(static_cast<std::size_t>(phase_idx_)); }

  std::vector<unsigned> newly_completed_consumers() const override { return newly_completed_; }

  void print_phase_plan() const override
  {
    std::map<std::string, std::vector<int>> ids_by_unit;
    for (auto& sc : env_->typed_view<champsim::modules::packet_consumer>("packet_consumer"))
      if (governs(sc.get().consumer_id()))
        ids_by_unit[sc.get().progress_unit()].push_back(sc.get().consumer_id());
    if (ids_by_unit.empty())
      ids_by_unit.emplace("packets", std::vector<int>{});

    const bool disambiguate = ids_by_unit.size() > 1;
    for (const auto& p : phases_)
      for (const auto& [unit, ids] : ids_by_unit)
        if (disambiguate)
          fmt::print("{} {}: {} (consumers {})\n", p.name, unit, p.length, fmt::join(ids, ", "));
        else
          fmt::print("{} {}: {}\n", p.name, unit, p.length);
  }
};

static champsim::modules::phase_controller::register_interface phase_controller_iface_reg("phase_controller", "phase controllers");
static champsim::modules::phase_controller::register_module<default_phase_controller> default_pc_reg("PHASE_CONTROLLER");

} // anonymous namespace
