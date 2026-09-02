/*
 * NMFC_HOST_CORE — the compute tile's core.
 *
 * A copy of ChampSim's O3_CPU, extended with the function tracking unit. It is
 * a copy rather than a subclass because operate(), initialize(),
 * push_instruction() and print_deadlock() are all `final`, so subclassing
 * cannot hook the pipeline at all.
 *
 * MAINTENANCE: everything here is line-for-line identical to the base except
 * where a comment says `// NMFC:`. Those are the only intended differences, so
 *
 *     diff <(sed -e 's/NMFC_HOST_CORE/O3_CPU/g' -e 's/NMFC_LSQ_ENTRY/LSQ_ENTRY/g' \
 *                -e 's/NMFC_CacheBus/CacheBus/g' src/nmfc/nmfc_host_core.cc) src/ooo_cpu.cc
 *
 * should show only those blocks. Three symbols are renamed wholesale
 * (O3_CPU, LSQ_ENTRY, CacheBus) because both cores link into one binary.
 */

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

#include "nmfc/nmfc_host_core.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <ratio>
#include <string>
#include <vector>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

#include "cache.h"
#include "champsim.h"
#include "deadlock.h"
#include "hooks.h"
#include "instruction.h"
#include "instruction_producer.h"
#include "json_stat_builder.h"
#include "util/algorithm.h"
#include "util/span.h"
#include "util/stat_format.h"

long NMFC_HOST_CORE::operate()
{
  long progress{0};
  progress += retire_rob();                    // retire
  progress += complete_inflight_instruction(); // finalize execution
  progress += execute_instruction();           // execute instructions
  progress += schedule_instruction();          // schedule instructions
  progress += handle_memory_return();          // finalize memory transactions
  progress += dispatch_offloads();             // NMFC: hand invocations to the fabric
  {
    // NMFC: sample tracking-unit occupancy once per operated cycle.
    //
    // Counted incrementally, not scanned. This ran std::count_if over the whole
    // FTU every cycle -- 1024 entries x tens of millions of cycles, tens of
    // billions of iterations, to maintain a mean and a peak. It was the single
    // largest cost in the host core's profile, and it is a statistic.
    const auto occupied = ftu_occupied_;
    ftu_occupancy_sum_ += occupied;
    ftu_peak_ = std::max(ftu_peak_, occupied);
    ++ftu_cycles_;
  }
  progress += operate_lsq();                   // execute memory transactions

  progress += dispatch_instruction(); // dispatch
  progress += decode_instruction();   // decode
  progress += promote_to_decode();

  progress += fetch_instruction(); // fetch
  progress += check_dib();
  initialize_instruction();
  fill_from_producers(); // refill after the drain (matches develop's do_cycle feed)

  return progress;
}

void NMFC_HOST_CORE::initialize()
{
  // BRANCH PREDICTOR & BTB
  impl_initialize_branch_predictor();
  impl_initialize_btb();
}

void NMFC_HOST_CORE::begin_phase(bool warmup)
{
  warmup_ = warmup;
  begin_phase_instr = num_retired;
  begin_phase_time = current_time;

  // Record where the next phase begins
  stats_type stats;
  stats.name = "CPU " + std::to_string(consumer_id());
  stats.begin_instrs = num_retired;
  stats.begin_cycles = begin_phase_time.time_since_epoch() / clock_period;
  sim_stats = stats;

  // NMFC: tracking-unit counters. Occupancy is a live quantity and survives the
  // edge, since invocations issued before it are still owed a return.
  offloads_issued_ = offloads_completed_ = offload_dispatch_stalls_ = offload_fire_and_forget_ = 0;
  ftu_occupancy_sum_ = ftu_cycles_ = 0;
  ftu_occupied_ = static_cast<std::size_t>(std::count_if(std::begin(FTU), std::end(FTU), [](const auto& e) { return e.has_value(); }));
  ftu_peak_ = ftu_occupied_;
}

void NMFC_HOST_CORE::end_phase(champsim::stat_report& out)
{
  // Record where the phase ended, then report it.
  sim_stats.end_instrs = num_retired;
  sim_stats.end_cycles = current_time.time_since_epoch() / clock_period;

  finish_phase_instr = num_retired;
  finish_phase_time = current_time;

  format_stats(sim_stats, out);

  // NMFC: what the tracking unit did. Reported only when it did something, so a
  // configuration that never offloads publishes nothing extra.
  if (offloads_issued_ > 0) {
    const auto mean = ftu_cycles_ == 0 ? 0.0 : static_cast<double>(ftu_occupancy_sum_) / static_cast<double>(ftu_cycles_);
    out.line(fmt::format("{} OFFLOADS ISSUED: {} COMPLETED: {} FIRE-AND-FORGET: {}", NAME, offloads_issued_, offloads_completed_, offload_fire_and_forget_));
    out.line(fmt::format("{} FTU IN FLIGHT mean: {:.2f} peak: {} of {} DISPATCH STALLS: {}", NAME, mean, ftu_peak_, FTU.size(), offload_dispatch_stalls_));
    if (offload_forks_ > 0) {
      const auto free_joins = offload_joins_ == 0 ? 0.0 : 100.0 * static_cast<double>(offload_joins_already_home_) / static_cast<double>(offload_joins_);
      out.line(fmt::format("{} FORK/JOIN forks: {} joins: {} already home at join: {} ({:.1f}%)", NAME, offload_forks_, offload_joins_,
                           offload_joins_already_home_, free_joins));
    }

    auto json = out.json();
    json.add("offloads_issued", offloads_issued_);
    json.add("offloads_completed", offloads_completed_);
    json.add("offloads_fire_and_forget", offload_fire_and_forget_);
    json.add("ftu_mean_in_flight", mean);
    json.add("ftu_peak_in_flight", ftu_peak_);
    json.add("ftu_size", FTU.size());
    json.add("offload_dispatch_stalls", offload_dispatch_stalls_);
    json.add("offload_forks", offload_forks_);
    json.add("offload_joins", offload_joins_);
    json.add("offload_joins_already_home", offload_joins_already_home_);
  }
}

void NMFC_HOST_CORE::initialize_instruction()
{
  champsim::bandwidth instrs_to_read_this_cycle{
      std::min(FETCH_WIDTH, champsim::bandwidth::maximum_type{static_cast<long>(IFETCH_BUFFER_SIZE - std::size(IFETCH_BUFFER))})};

  bool stop_fetch = false;
  while (current_time >= fetch_resume_time && instrs_to_read_this_cycle.has_remaining() && !stop_fetch && !std::empty(input_queue)) {
    instrs_to_read_this_cycle.consume();

    stop_fetch = do_init_instruction(input_queue.front());

    // Add to IFETCH_BUFFER
    IFETCH_BUFFER.push_back(std::move(input_queue.front()));
    input_queue.pop_front();

    IFETCH_BUFFER.back().ready_time = current_time;
  }
}

namespace
{
void nmfc_stack_pointer_folding(ooo_model_instr& arch_instr)
{
  // The exact, true value of the stack pointer for any given instruction can usually be determined immediately after the instruction is decoded without
  // waiting for the stack pointer's dependency chain to be resolved.
  bool writes_sp = (std::count(std::begin(arch_instr.destination_registers), std::end(arch_instr.destination_registers), champsim::REG_STACK_POINTER) > 0);
  if (writes_sp) {
    // Avoid creating register dependencies on the stack pointer for calls, returns, pushes, and pops, but not for variable-sized changes in the
    // stack pointer position. reads_other indicates that the stack pointer is being changed by a variable amount, which can't be determined before
    // execution.
    bool reads_other =
        (std::count_if(std::begin(arch_instr.source_registers), std::end(arch_instr.source_registers),
                       [](auto r) { return r != champsim::REG_STACK_POINTER && r != champsim::REG_FLAGS && r != champsim::REG_INSTRUCTION_POINTER; })
         > 0);
    if ((arch_instr.is_branch) || !(std::empty(arch_instr.destination_memory) && std::empty(arch_instr.source_memory)) || (!reads_other)) {
      auto nonsp_end = std::remove(std::begin(arch_instr.destination_registers), std::end(arch_instr.destination_registers), champsim::REG_STACK_POINTER);
      arch_instr.destination_registers.erase(nonsp_end, std::end(arch_instr.destination_registers));
    }
  }
}
} // namespace

bool NMFC_HOST_CORE::do_predict_branch(ooo_model_instr& arch_instr)
{
  bool stop_fetch = false;

  // handle branch prediction for all instructions as at this point we do not know if the instruction is a branch
  sim_stats.total_branch_types.increment(arch_instr.branch);
  auto [predicted_branch_target, always_taken] = impl_btb_prediction(arch_instr.ip, arch_instr.branch);
  arch_instr.branch_prediction = impl_predict_branch(arch_instr.ip, predicted_branch_target, always_taken, arch_instr.branch) || always_taken;
  if (!arch_instr.branch_prediction) {
    predicted_branch_target = champsim::address{};
  }

  if (arch_instr.is_branch) {
    if constexpr (champsim::debug_print) {
      fmt::print("[BRANCH] instr_id: {} ip: {} taken: {}\n", arch_instr.instr_id, arch_instr.ip, arch_instr.branch_taken);
    }

    // call code prefetcher every time the branch predictor is used
    l1i->impl_prefetcher_branch_operate(arch_instr.ip, arch_instr.branch, predicted_branch_target);

    if (predicted_branch_target != arch_instr.branch_target
        || (((arch_instr.branch == BRANCH_CONDITIONAL) || (arch_instr.branch == BRANCH_OTHER))
            && arch_instr.branch_taken != arch_instr.branch_prediction)) { // conditional branches are re-evaluated at decode when the target is computed
      sim_stats.total_rob_occupancy_at_branch_mispredict += std::size(ROB);
      sim_stats.branch_type_misses.increment(arch_instr.branch);
      if (!is_warmup()) {
        fetch_resume_time = champsim::chrono::clock::time_point::max();
        stop_fetch = true;
        arch_instr.branch_mispredicted = true;
      }
    } else {
      stop_fetch = arch_instr.branch_taken; // if correctly predicted taken, then we can't fetch anymore instructions this cycle
    }

    impl_update_btb(arch_instr.ip, arch_instr.branch_target, arch_instr.branch_taken, arch_instr.branch);
    impl_last_branch_result(arch_instr.ip, arch_instr.branch_target, arch_instr.branch_taken, arch_instr.branch);
  }

  return stop_fetch;
}

void NMFC_HOST_CORE::push_instruction(ooo_model_instr instr) { input_queue.push_back(std::move(instr)); }

std::size_t NMFC_HOST_CORE::instructions_requested() { return IN_QUEUE_SIZE - static_cast<long>(std::size(input_queue)); }

bool NMFC_HOST_CORE::do_init_instruction(ooo_model_instr& arch_instr)
{
  // fast warmup eliminates register dependencies between instructions branch predictor, cache contents, and prefetchers are still warmed up
  if (is_warmup()) {
    arch_instr.source_registers.clear();
    arch_instr.destination_registers.clear();
  }

  ::nmfc_stack_pointer_folding(arch_instr);
  return do_predict_branch(arch_instr);
}

long NMFC_HOST_CORE::check_dib()
{
  // dib_checked entries form a strict prefix of IFETCH_BUFFER, so the first
  // unchecked instruction sits exactly at the maintained prefix length —
  // no scan needed.
  auto begin = std::next(std::begin(IFETCH_BUFFER), std::min(ifetch_dib_checked_, static_cast<long>(std::size(IFETCH_BUFFER))));
  auto [window_begin, window_end] = champsim::get_span(begin, std::end(IFETCH_BUFFER), champsim::bandwidth{FETCH_WIDTH});
  std::for_each(window_begin, window_end, [this](auto& ifetch_entry) { this->do_check_dib(ifetch_entry); });
  ifetch_dib_checked_ += std::distance(window_begin, window_end);
  return std::distance(window_begin, window_end);
}

void NMFC_HOST_CORE::do_check_dib(ooo_model_instr& instr)
{
  // Check DIB to see if we recently fetched this line
  auto dib_result = DIB.check_hit(instr.ip);
  if (dib_result) {
    // The cache line is in the L0, so we can mark this as complete
    instr.fetch_completed = true;

    // Also mark it as decoded
    instr.decoded = true;

    // It can be acted on immediately
    instr.ready_time = current_time;
  }

  instr.dib_checked = true;

  if constexpr (champsim::debug_print) {
    fmt::print("[DIB] {} instr_id: {} ip: {} hit: {} cycle: {}\n", __func__, instr.instr_id, instr.ip, dib_result.has_value(),
               current_time.time_since_epoch() / clock_period);
  }
}

long NMFC_HOST_CORE::fetch_instruction()
{
  long progress{0};

  // Fetch a single cache line
  auto fetch_ready = [](const ooo_model_instr& x) {
    return x.dib_checked && !x.fetch_issued;
  };

  // Find the chunk of instructions in the block
  auto no_match_ip = [](const auto& lhs, const auto& rhs) {
    return champsim::block_number{lhs.ip} != champsim::block_number{rhs.ip};
  };

  // The first fetch-ready entry only moves forward, so resume the scan from
  // the maintained position, not the buffer front — each slot is walked O(1)
  // times amortized instead of once per cycle.
  auto scan_begin = std::next(std::begin(IFETCH_BUFFER), std::min(ifetch_fetch_scan_, static_cast<long>(std::size(IFETCH_BUFFER))));
  auto l1i_req_begin = std::find_if(scan_begin, std::end(IFETCH_BUFFER), fetch_ready);
  // Advance only over permanently non-matching (checked-and-issued) entries;
  // never pass the dib prefix boundary, since unchecked entries can still
  // become fetch-ready.
  ifetch_fetch_scan_ = std::min(std::distance(std::begin(IFETCH_BUFFER), l1i_req_begin), ifetch_dib_checked_);
  for (champsim::bandwidth l1i_bw{L1I_BANDWIDTH}; l1i_bw.has_remaining() && l1i_req_begin != std::end(IFETCH_BUFFER); l1i_bw.consume()) {
    auto l1i_req_end = std::adjacent_find(l1i_req_begin, std::end(IFETCH_BUFFER), no_match_ip);
    if (l1i_req_end != std::end(IFETCH_BUFFER)) {
      l1i_req_end = std::next(l1i_req_end); // adjacent_find returns the first of the non-equal elements
    }

    // Issue to L1I
    auto success = do_fetch_instruction(l1i_req_begin, l1i_req_end);
    if (success) {
      std::for_each(l1i_req_begin, l1i_req_end, [](auto& x) { x.fetch_issued = true; });
      ++progress;
    }

    l1i_req_begin = std::find_if(l1i_req_end, std::end(IFETCH_BUFFER), fetch_ready);
  }

  return progress;
}

bool NMFC_HOST_CORE::do_fetch_instruction(champsim::ring_buffer<ooo_model_instr>::iterator begin, champsim::ring_buffer<ooo_model_instr>::iterator end)
{
  NMFC_CacheBus::request_type fetch_packet;
  fetch_packet.origin = begin->origin;
  fetch_packet.v_address = begin->ip;
  fetch_packet.instr_id = begin->instr_id;
  fetch_packet.ip = begin->ip;

  std::transform(begin, end, std::back_inserter(fetch_packet.instr_depend_on_me), [](const auto& instr) { return instr.instr_id; });

  if constexpr (champsim::debug_print) {
    fmt::print("[IFETCH] {} instr_id: {} ip: {} dependents: {} event_cycle: {}\n", __func__, begin->instr_id, begin->ip,
               std::size(fetch_packet.instr_depend_on_me), begin->ready_time.time_since_epoch() / clock_period);
  }

  return L1I_bus.issue_read(fetch_packet);
}

long NMFC_HOST_CORE::promote_to_decode()
{
  auto is_decoded = [](const ooo_model_instr& x) {
    return x.decoded;
  };

  auto fetch_complete_and_ready = [time = current_time](const auto& x) {
    return x.fetch_completed && x.ready_time <= time;
  };

  champsim::bandwidth available_fetch_bandwidth{
      std::min(FETCH_WIDTH, std::min(champsim::bandwidth::maximum_type{static_cast<long>(DIB_HIT_BUFFER_SIZE - std::size(DIB_HIT_BUFFER))},
                                     champsim::bandwidth::maximum_type{static_cast<long>(DECODE_BUFFER_SIZE - std::size(DECODE_BUFFER))}))};

  // No pre-scan needed: the predicate requires fetch_completed, so
  // get_span_p stops at exactly the entry a find_if bound would have found.
  auto [window_begin, window_end] =
      champsim::get_span_p(std::begin(IFETCH_BUFFER), std::end(IFETCH_BUFFER), available_fetch_bandwidth, fetch_complete_and_ready);
  auto decoded_window_end = champsim::stable_partition_small(window_begin, window_end, is_decoded); // reorder instructions
  auto mark_for_decode = [time = current_time, lat = DECODE_LATENCY, warmup = is_warmup()](auto& x) {
    return x.ready_time = time + (warmup ? champsim::chrono::clock::duration{} : lat);
  };
  // to DIB_HIT_BUFFER
  auto mark_for_dib = [time = current_time, lat = DIB_HIT_LATENCY, warmup = is_warmup()](auto& x) {
    (void)warmup;
    return x.ready_time = time + lat;
  };

  std::for_each(window_begin, decoded_window_end, mark_for_dib); // assume DECODE_LATENCY = DIB_HIT_LATENCY
  std::move(window_begin, decoded_window_end, std::back_inserter(DIB_HIT_BUFFER));
  // to DECODE_BUFFER

  std::for_each(decoded_window_end, window_end, mark_for_decode);
  std::move(decoded_window_end, window_end, std::back_inserter(DECODE_BUFFER));

  long progress{std::distance(window_begin, window_end)};
  ifetch_dib_checked_ = std::max(ifetch_dib_checked_ - std::distance(window_begin, window_end), long{0});
  ifetch_fetch_scan_ = std::max(ifetch_fetch_scan_ - std::distance(window_begin, window_end), long{0});
  IFETCH_BUFFER.erase(window_begin, window_end);
  return progress;
}
long NMFC_HOST_CORE::decode_instruction()
{
  auto is_ready = [time = current_time](const auto& x) {
    return x.ready_time <= time;
  };

  auto dib_hit_buffer_begin = std::begin(DIB_HIT_BUFFER);
  auto dib_hit_buffer_end = dib_hit_buffer_begin;
  auto decode_buffer_begin = std::begin(DECODE_BUFFER);
  auto decode_buffer_end = decode_buffer_begin;

  champsim::bandwidth available_decode_bandwidth{DECODE_WIDTH};

  // bw move instructions to dispatch_buffer
  champsim::bandwidth available_dib_inorder_bandwidth{
      std::min(DIB_INORDER_WIDTH, champsim::bandwidth::maximum_type{static_cast<long>(DISPATCH_BUFFER_SIZE - std::size(DISPATCH_BUFFER))})};

  // conditions choose how many instructions sent to dispatch_buffer
  while (dib_hit_buffer_end != std::end(DIB_HIT_BUFFER) && decode_buffer_end != std::end(DECODE_BUFFER) && available_dib_inorder_bandwidth.has_remaining()
         && available_decode_bandwidth.has_remaining() && is_ready(std::min(*dib_hit_buffer_end, *decode_buffer_end, ooo_model_instr::program_order))) {
    if (ooo_model_instr::program_order(*dib_hit_buffer_end, *decode_buffer_end)) {
      dib_hit_buffer_end++;
      available_dib_inorder_bandwidth.consume();
    } else {
      decode_buffer_end++;
      available_dib_inorder_bandwidth.consume();
      available_decode_bandwidth.consume();
    }
  }
  while (dib_hit_buffer_end != std::end(DIB_HIT_BUFFER) && available_dib_inorder_bandwidth.has_remaining() && is_ready(*dib_hit_buffer_end)
         && (decode_buffer_end == std::end(DECODE_BUFFER) || ooo_model_instr::program_order(*dib_hit_buffer_end, *decode_buffer_end))) {
    dib_hit_buffer_end++;
    available_dib_inorder_bandwidth.consume();
  }
  while (decode_buffer_end != std::end(DECODE_BUFFER) && available_dib_inorder_bandwidth.has_remaining() && available_decode_bandwidth.has_remaining()
         && is_ready(*decode_buffer_end)
         && (dib_hit_buffer_end == std::end(DIB_HIT_BUFFER) || ooo_model_instr::program_order(*decode_buffer_end, *dib_hit_buffer_end))) {
    decode_buffer_end++;
    available_dib_inorder_bandwidth.consume();
    available_decode_bandwidth.consume();
  }

  // decode instructions have not decoded, merge instructions with dib_hit_buffer then send to dispatch_buffer
  auto do_decode = [&, this](auto& db_entry) {
    this->do_dib_update(db_entry);

    // Resume fetch
    if (db_entry.branch_mispredicted) {
      // These branches detect the misprediction at decode
      if ((db_entry.branch == BRANCH_DIRECT_JUMP) || (db_entry.branch == BRANCH_DIRECT_CALL)
          || (((db_entry.branch == BRANCH_CONDITIONAL) || (db_entry.branch == BRANCH_OTHER)) && db_entry.branch_taken == db_entry.branch_prediction)) {
        // clear the branch_mispredicted bit so we don't attempt to resume fetch again at execute
        db_entry.branch_mispredicted = 0;
        // pay misprediction penalty
        this->fetch_resume_time = this->current_time + BRANCH_MISPREDICT_PENALTY;
      }
    }
    // Add to dispatch
    db_entry.ready_time = this->current_time + (this->is_warmup() ? champsim::chrono::clock::duration{} : this->DISPATCH_LATENCY);

    if constexpr (champsim::debug_print) {
      fmt::print("[DECODE] do_decode instr_id: {} time: {}\n", db_entry.instr_id, this->current_time.time_since_epoch() / this->clock_period);
    }
  };

  auto do_dib_hit = [&, this](auto& dib_entry) {
    dib_entry.ready_time = this->current_time + (this->is_warmup() ? champsim::chrono::clock::duration{} : this->DISPATCH_LATENCY);
  };

  std::for_each(decode_buffer_begin, decode_buffer_end, do_decode);
  std::for_each(dib_hit_buffer_begin, dib_hit_buffer_end, do_dib_hit);

  long progress{std::distance(dib_hit_buffer_begin, dib_hit_buffer_end) + std::distance(decode_buffer_begin, decode_buffer_end)};

  // Move the merged windows: both source ranges are erased immediately
  // below, and the comparator reads only instr_id (untouched by moving the
  // instruction's vectors).
  std::merge(std::make_move_iterator(dib_hit_buffer_begin), std::make_move_iterator(dib_hit_buffer_end), std::make_move_iterator(decode_buffer_begin),
             std::make_move_iterator(decode_buffer_end), std::back_inserter(DISPATCH_BUFFER), ooo_model_instr::program_order);
  DECODE_BUFFER.erase(decode_buffer_begin, decode_buffer_end);
  DIB_HIT_BUFFER.erase(dib_hit_buffer_begin, dib_hit_buffer_end);

  return progress;
}

void NMFC_HOST_CORE::do_dib_update(const ooo_model_instr& instr) { DIB.fill(instr.ip); }

long NMFC_HOST_CORE::dispatch_instruction()
{
  champsim::bandwidth available_dispatch_bandwidth{DISPATCH_WIDTH};

  // dispatch DISPATCH_WIDTH instructions into the ROB
  while (available_dispatch_bandwidth.has_remaining() && !std::empty(DISPATCH_BUFFER) && DISPATCH_BUFFER.front().ready_time <= current_time
         && std::size(ROB) != ROB_SIZE && (static_cast<std::size_t>(lq_free_slots_) >= std::size(DISPATCH_BUFFER.front().source_memory))
         && ((std::size(DISPATCH_BUFFER.front().destination_memory) + std::size(SQ)) <= SQ_SIZE)
         // NMFC: an instruction that offloads needs a tracking slot, and waits
         // for one. This is where the fabric's back-pressure reaches the ROB.
         && offload_slots_available(DISPATCH_BUFFER.front())) {
    ROB.push_back(std::move(DISPATCH_BUFFER.front()));
    DISPATCH_BUFFER.pop_front();
    do_memory_scheduling(ROB.back());

    available_dispatch_bandwidth.consume();
    ROB.back().ready_time = current_time + (is_warmup() ? champsim::chrono::clock::duration{} : SCHEDULING_LATENCY);
  }

  return available_dispatch_bandwidth.amount_consumed();
}

long NMFC_HOST_CORE::schedule_instruction()
{
  // Scheduled entries form a strict ROB prefix; unscheduled entries keep
  // monotone ready_times. If the first unscheduled entry is absent or not yet
  // ready, the walk below has no side effects — skip it.
  if (num_scheduled_ >= static_cast<long>(std::size(ROB))) {
    return 0;
  }
  // The .scheduled guard is defensive: normally the prefix-boundary entry is
  // never scheduled; injected ROB state (tests) falls through to the full walk.
  if (const auto& first = ROB[static_cast<std::size_t>(num_scheduled_)]; !first.scheduled && first.ready_time > current_time) {
    return 0;
  }

  // Pre-charge the budget with the in-flight (scheduled, unexecuted) count
  // rather than re-walking them. The free-register rename gate applies only to
  // instructions renamed this cycle, whose source_registers still hold
  // architectural ids, so the check is meaningful.
  champsim::bandwidth search_bw{SCHEDULER_SIZE};
  search_bw.consume(std::min(scheduler_occupancy_, champsim::to_underlying(SCHEDULER_SIZE)));

  int progress{0};
  for (auto rob_it = std::next(std::begin(ROB), num_scheduled_); rob_it != std::end(ROB) && search_bw.has_remaining(); ++rob_it) {
    // In-order rename: stop if this instruction cannot claim the physical
    // registers it needs.
    unsigned long sources_to_allocate = std::count_if(rob_it->source_registers.begin(), rob_it->source_registers.end(),
                                                      [&alloc = std::as_const(reg_allocator)](auto srcreg) { return !alloc.isAllocated(srcreg); });
    if (reg_allocator.count_free_registers() < (sources_to_allocate + rob_it->destination_registers.size())) {
      break;
    }
    // Unscheduled ready_times are monotone (dispatch order), so the first
    // not-yet-ready instruction ends this cycle's work.
    if (rob_it->ready_time > current_time) {
      break;
    }
    if (!rob_it->scheduled) { // always true in normal operation; guards injected state
      do_scheduling(*rob_it);
      candidate_set(exec_candidates_, rob_it.slot());
      ++num_scheduled_;
      ++progress;
    }
    search_bw.consume(); // the newly scheduled instruction occupies a slot
  }

  return progress;
}

void NMFC_HOST_CORE::do_scheduling(ooo_model_instr& instr)
{
  // Mark register dependencies
  for (auto& src_reg : instr.source_registers) {
    // rename source register
    src_reg = reg_allocator.rename_src_register(src_reg);
  }

  for (auto& dreg : instr.destination_registers) {
    // rename destination register
    dreg = reg_allocator.rename_dest_register(dreg, instr.instr_id);
  }

  instr.scheduled = true;
  ++scheduler_occupancy_;
  exec_stage_clean_ = false; // a new execute candidate exists
}

long NMFC_HOST_CORE::execute_instruction()
{
  // The walk is side-effect-free unless it issues, and its outcome changes
  // only via the events exec_stage_clean_/exec_stage_wake_ track, so a clean
  // stage before its wake point provably issues nothing.
  if (exec_stage_clean_ && current_time < exec_stage_wake_) {
    return 0;
  }

  champsim::bandwidth exec_bw{EXEC_WIDTH};
  auto wake = champsim::chrono::clock::time_point::max();
  auto visit = [&](std::size_t slot) {
    if (!exec_bw.has_remaining()) {
      return false;
    }
    auto& instr = ROB.at_slot(slot);
    if (instr.scheduled && !instr.executed) { // always true in normal operation; guards injected state
      if (instr.ready_time <= current_time) {
        bool ready = std::all_of(std::begin(instr.source_registers), std::end(instr.source_registers),
                                 [&alloc = std::as_const(reg_allocator)](auto srcreg) { return alloc.isValid(srcreg); });
        if (ready) {
          do_execution(instr, slot);
          candidate_clear(exec_candidates_, slot);
          exec_bw.consume();
        }
      } else {
        wake = std::min(wake, instr.ready_time);
      }
    }
    return true;
  };
  if (visit_candidates_in_age_order(exec_candidates_, visit)) {
    // Full traversal: every remaining candidate either waits on a register
    // (covered by the completion dirty flag) or on a recorded wake time.
    exec_stage_clean_ = true;
    exec_stage_wake_ = wake;
  } else {
    exec_stage_clean_ = false; // bandwidth exhausted before seeing the tail
  }

  return exec_bw.amount_consumed();
}

void NMFC_HOST_CORE::do_execution(ooo_model_instr& instr, std::size_t rob_slot)
{
  instr.executed = true;
  --scheduler_occupancy_; // leaves the scheduler window
  instr.ready_time = current_time + (is_warmup() ? champsim::chrono::clock::duration{} : EXEC_LATENCY);
  complete_stage_clean_ = false; // a new completion candidate exists

  // If every memory op is already done (none, or all immediately
  // store-forwarded before execution), this instruction becomes a completion
  // candidate now; otherwise the crossing finish() records it later.
  if (instr.completed_mem_ops == instr.num_mem_ops()) {
    candidate_set(mem_complete_candidates_, rob_slot);
  }

  const auto& handles = rob_mem_handles_[rob_slot];

  // Mark this instruction's LQ entries as ready to translate. Each recorded
  // slot is revalidated (a producer-waiting load may have been finished and
  // its slot reused via forwarding), so only slots still holding this
  // instruction's load are stamped.
  for (const auto idx : handles.lq_slots) {
    auto& lq_entry = LQ[idx];
    if (lq_entry.has_value() && lq_entry->instr_id == instr.instr_id) {
      lq_entry->ready_time = current_time + (is_warmup() ? champsim::chrono::clock::duration{} : EXEC_LATENCY);
      // Enroll issue candidates for readiness promotion; only unissued ones
      // enroll. Enrollment times are monotone (current_time plus a constant),
      // keeping the pending-queue promotion a FIFO pop.
      if ((lq_unissued_[idx >> 6] >> (idx & 63)) & 1U) {
        lq_pending_ready_.emplace_back(lq_entry->ready_time, static_cast<uint32_t>(idx));
      }
    }
  }

  // Mark this instruction's SQ entries as ready to translate. The recorded
  // pointers are valid: before do_execution runs, the store cannot have been
  // fetched, completed, or front-erased.
  for (auto* sq_entry : handles.sq_entries) {
    sq_entry->ready_time = current_time + (is_warmup() ? champsim::chrono::clock::duration{} : EXEC_LATENCY);
  }

  if constexpr (champsim::debug_print) {
    fmt::print("[ROB] {} instr_id: {} ready_time: {}\n", __func__, instr.instr_id, instr.ready_time.time_since_epoch() / clock_period);
  }
}

void NMFC_HOST_CORE::do_memory_scheduling(ooo_model_instr& instr)
{
  // Record this instruction's memory entries against its just-dispatched ROB
  // slot (it is ROB.back()); do_execution reads them back to stamp ready_time.
  const auto rob_slot = ROB.slot_index(std::size(ROB) - 1);
  auto& handles = rob_mem_handles_[rob_slot];
  handles.lq_slots.clear();
  handles.sq_entries.clear();

  // load
  for (auto& smem : instr.source_memory) {
    // NMFC: an address inside the offload aperture is not a load at all. It is
    // an invocation, so it takes an FTU entry instead of an LQ entry and never
    // reaches the L1D. It still counts as one of the instruction's memory ops,
    // so retirement waits for the function exactly as it would for a load.
    if (is_offload(smem)) {
      allocate_offload(instr, rob_slot, smem);
      continue;
    }
    // Lowest free slot from the occupancy bitmap — identical to a
    // find_if_not scan from the front, without visiting occupied slots.
    std::size_t free_idx = 0;
    for (std::size_t word = 0; word < std::size(lq_occupied_); ++word) {
      if (~lq_occupied_[word] != 0) {
        free_idx = word * 64 + static_cast<std::size_t>(__builtin_ctzll(~lq_occupied_[word]));
        break;
      }
    }
    auto q_entry = std::next(std::begin(LQ), static_cast<long>(std::min(free_idx, std::size(LQ))));
    assert(q_entry != std::end(LQ));
    const auto lq_idx = static_cast<std::size_t>(std::distance(std::begin(LQ), q_entry));
    q_entry->emplace(smem, instr.instr_id, instr.ip, instr.origin); // add it to the load queue
    (*q_entry)->rob_slot = rob_slot;                                // owner is this cycle's ROB.back()
    lq_set_occupied(lq_idx);
    bool live = true; // cleared if this slot is immediately store-forwarded and freed below

    // Check for forwarding: the youngest prior store to this exact address
    // (the back of the per-address stack).
    auto fwd_it = sq_store_index_.find(smem.to<uint64_t>());
    if (fwd_it != std::end(sq_store_index_)) {
      auto* sq_it = fwd_it->second.back();
      if (sq_it->fetch_issued) { // Store already executed
        (*q_entry)->finish(instr);
        complete_stage_clean_ = false; // a memory op finished
        q_entry->reset();
        lq_clear_occupied(lq_idx);
        live = false; // freed immediately; recording it would double-enroll a reused slot
      } else {
        assert(sq_it->instr_id < instr.instr_id);      // The found SQ entry is a prior store
        sq_it->lq_depend_on_me.emplace_back(*q_entry); // Forward the load when the store finishes
        (*q_entry)->producer_id = sq_it->instr_id;     // The load waits on the store to finish
        lq_clear_unissued(lq_idx);

        if constexpr (champsim::debug_print) {
          fmt::print("[DISPATCH] {} instr_id: {} waits on: {}\n", __func__, instr.instr_id, sq_it->instr_id);
        }
      }
    }

    // Record only the live slot; a slot freed by immediate forwarding above
    // (possibly reused by a later op) must not appear in the handle list.
    if (live) {
      handles.lq_slots.push_back(static_cast<uint16_t>(lq_idx));
    }
  }

  // store
  for (auto& dmem : instr.destination_memory) {
    SQ.emplace_back(dmem, instr.instr_id, instr.ip, instr.origin); // add it to the store queue
    SQ.back().rob_slot = rob_slot;                                 // owner is this cycle's ROB.back()
    sq_store_index_[dmem.to<uint64_t>()].push_back(&SQ.back());
    handles.sq_entries.push_back(&SQ.back());
  }

  if constexpr (champsim::debug_print) {
    fmt::print("[DISPATCH] {} instr_id: {} loads: {} stores: {} cycle: {}\n", __func__, instr.instr_id, std::size(instr.source_memory),
               std::size(instr.destination_memory), current_time.time_since_epoch() / clock_period);
  }
}

long NMFC_HOST_CORE::operate_lsq()
{
  champsim::bandwidth store_bw{SQ_WIDTH};

  const auto complete_id = std::empty(ROB) ? std::numeric_limits<uint64_t>::max() : ROB.front().instr_id;
  auto do_complete = [time = current_time, finished = NMFC_LSQ_ENTRY::precedes(complete_id), this](const auto& x) {
    return finished(x) && x.ready_time <= time && this->do_complete_store(x);
  };

  auto unfetched_begin = std::partition_point(std::begin(SQ), std::end(SQ), [](const auto& x) { return x.fetch_issued; });
  auto [fetch_begin, fetch_end] =
      champsim::get_span_p(unfetched_begin, std::end(SQ), store_bw, [time = current_time](const auto& x) { return !x.fetch_issued && x.ready_time <= time; });
  store_bw.consume(std::distance(fetch_begin, fetch_end));
  std::for_each(fetch_begin, fetch_end, [time = current_time, this](auto& sq_entry) {
    this->do_finish_store(sq_entry);
    sq_entry.fetch_issued = true;
    sq_entry.ready_time = time;
  });

  auto [complete_begin, complete_end] = champsim::get_span_p(std::cbegin(SQ), std::cend(SQ), store_bw, do_complete);
  store_bw.consume(std::distance(complete_begin, complete_end));
  std::for_each(complete_begin, complete_end, [this](const NMFC_LSQ_ENTRY& sq_entry) {
    // Completion is oldest-first, so the erased entry is the front of its
    // per-address stack
    auto idx_it = sq_store_index_.find(sq_entry.virtual_address.to<uint64_t>());
    assert(idx_it != std::end(sq_store_index_) && idx_it->second.front() == &sq_entry);
    idx_it->second.erase(std::begin(idx_it->second));
    if (std::empty(idx_it->second)) {
      sq_store_index_.erase(idx_it);
    }
  });
  SQ.erase(complete_begin, complete_end);

  champsim::bandwidth load_bw{LQ_WIDTH};

  // Promote pending candidates whose ready_time has passed (a FIFO pop:
  // enrollment times are monotone). The bit may have been cleared since
  // enrollment (issued via forwarding, or the entry left the LQ) — promote
  // only candidates still unissued.
  while (!std::empty(lq_pending_ready_) && lq_pending_ready_.front().first < current_time) {
    const auto idx = static_cast<std::size_t>(lq_pending_ready_.front().second);
    lq_pending_ready_.pop_front();
    if ((lq_unissued_[idx >> 6] >> (idx & 63)) & 1U) {
      lq_ready_[idx >> 6] |= (uint64_t{1} << (idx & 63));
    }
  }

  // The ready set is exactly the candidates a full scan would act on, in slot
  // order. All loads target the same L1D read queue, undrained in this loop,
  // so once one admission is rejected every later attempt this cycle fails
  // identically — stop at the first rejection, as hardware would. Queue
  // accounting stays in the channel's add_rq path (full-queue counters read
  // as cycles-blocked).
  bool rq_rejected = false;
  for (std::size_t word = 0; word < std::size(lq_ready_) && load_bw.has_remaining() && !rq_rejected; ++word) {
    for (uint64_t bits = lq_ready_[word]; bits != 0 && load_bw.has_remaining(); bits &= bits - 1) {
      const std::size_t idx = word * 64 + static_cast<std::size_t>(__builtin_ctzll(bits));
      auto& lq_entry = LQ[idx];
      auto success = execute_load(*lq_entry);
      if (!success) {
        rq_rejected = true;
        break;
      }
      load_bw.consume();
      lq_entry->fetch_issued = true;
      // Record the now-in-flight load under its block address; its matching
      // L1D return will finish it through this index.
      hmr_block_index_[hmr_block_key(lq_entry->virtual_address)].push_back(static_cast<uint32_t>(idx));
      lq_clear_unissued(idx); // clears the ready bit too
    }
  }

  return store_bw.amount_consumed() + load_bw.amount_consumed();
}

void NMFC_HOST_CORE::do_finish_store(const NMFC_LSQ_ENTRY& sq_entry)
{
  if constexpr (champsim::debug_print) {
    fmt::print("[SQ] {} instr_id: {} vaddr: {}\n", __func__, sq_entry.instr_id, sq_entry.virtual_address);
  }

  {
    auto& owner = ROB.at_slot(sq_entry.rob_slot);
    sq_entry.finish(owner);
    if (owner.executed && !owner.completed && owner.completed_mem_ops == owner.num_mem_ops()) {
      candidate_set(mem_complete_candidates_, sq_entry.rob_slot);
    }
  }
  complete_stage_clean_ = false; // a memory op finished

  // Release dependent loads
  for (std::optional<NMFC_LSQ_ENTRY>& dependent : sq_entry.lq_depend_on_me) {
    assert(dependent.has_value()); // LQ entry is still allocated
    assert(dependent->producer_id == sq_entry.instr_id);

    const auto dep_slot = dependent->rob_slot;
    auto& dep_owner = ROB.at_slot(dep_slot);
    dependent->finish(dep_owner);
    if (dep_owner.executed && !dep_owner.completed && dep_owner.completed_mem_ops == dep_owner.num_mem_ops()) {
      candidate_set(mem_complete_candidates_, dep_slot);
    }
    dependent.reset();
    lq_clear_occupied(static_cast<std::size_t>(&dependent - LQ.data()));
  }
}

bool NMFC_HOST_CORE::do_complete_store(const NMFC_LSQ_ENTRY& sq_entry)
{
  NMFC_CacheBus::request_type data_packet;
  data_packet.origin = sq_entry.origin;
  data_packet.v_address = sq_entry.virtual_address;
  data_packet.instr_id = sq_entry.instr_id;
  data_packet.ip = sq_entry.ip;

  if constexpr (champsim::debug_print) {
    fmt::print("[SQ] {} instr_id: {} vaddr: {}\n", __func__, data_packet.instr_id, data_packet.v_address);
  }

  return L1D_bus.issue_write(data_packet);
}

bool NMFC_HOST_CORE::execute_load(const NMFC_LSQ_ENTRY& lq_entry)
{
  NMFC_CacheBus::request_type data_packet;
  data_packet.origin = lq_entry.origin;
  data_packet.v_address = lq_entry.virtual_address;
  data_packet.instr_id = lq_entry.instr_id;
  data_packet.ip = lq_entry.ip;

  if constexpr (champsim::debug_print) {
    fmt::print("[LQ] {} instr_id: {} vaddr: {}\n", __func__, data_packet.instr_id, data_packet.v_address);
  }

  return L1D_bus.issue_read(data_packet);
}

void NMFC_HOST_CORE::do_complete_execution(ooo_model_instr& instr)
{
  exec_stage_clean_ = false; // registers become valid: execute candidates may wake
  for (auto dreg : instr.destination_registers) {
    // mark physical register's data as valid
    reg_allocator.complete_dest_register(dreg);
  }

  instr.completed = true;

  if (instr.branch_mispredicted) {
    fetch_resume_time = current_time + BRANCH_MISPREDICT_PENALTY;
  }
}

long NMFC_HOST_CORE::complete_inflight_instruction()
{
  // Same detector pattern as execute_instruction. mem_complete_candidates_
  // excludes executed instructions still waiting on memory, so the walk visits
  // only slots that can complete once their ready_time passes.
  if (complete_stage_clean_ && current_time < complete_stage_wake_) {
    return 0;
  }

  // update ROB entries with completed executions
  champsim::bandwidth complete_bw{EXEC_WIDTH};
  auto wake = champsim::chrono::clock::time_point::max();
  auto visit = [&](std::size_t slot) {
    if (!complete_bw.has_remaining()) {
      return false;
    }
    auto& instr = ROB.at_slot(slot);
    // The bitmap guarantees executed && !completed && mem-complete; the inner
    // tests are always true in normal operation and only guard injected state.
    if (instr.executed && !instr.completed) {
      if (instr.ready_time <= current_time) {
        if (instr.completed_mem_ops == instr.num_mem_ops()) {
          do_complete_execution(instr);
          candidate_clear(mem_complete_candidates_, slot);
          complete_bw.consume();
        }
      } else {
        wake = std::min(wake, instr.ready_time);
      }
    }
    return true;
  };
  if (visit_candidates_in_age_order(mem_complete_candidates_, visit)) {
    complete_stage_clean_ = true;
    complete_stage_wake_ = wake;
  } else {
    complete_stage_clean_ = false;
  }

  return complete_bw.amount_consumed();
}

long NMFC_HOST_CORE::handle_memory_return()
{
  long progress{0};

  // Block-granularity matching compares raw shifted values (building a
  // block_number per entry was a measured cost; shifted-value equality equals
  // block-slice equality).
  const auto block_shamt = champsim::to_underlying(champsim::block_number_extent{}.lower);

  auto& l1i_returned = L1I_bus.lower_level->get_returned();
  for (champsim::bandwidth fetch_bw{FETCH_WIDTH}, l1i_bw{L1I_BANDWIDTH}; fetch_bw.has_remaining() && l1i_bw.has_remaining() && !l1i_returned.empty();
       l1i_bw.consume()) {
    auto& l1i_entry = l1i_returned.front();
    const auto l1i_block = l1i_entry.v_address.to<uint64_t>() >> block_shamt;

    // Each iteration consumes one dependent id; bandwidth is spent only on
    // matches. The consumed prefix is erased once after the loop, and the
    // buffer search is a partition_point (IFETCH_BUFFER is instr_id-sorted).
    std::size_t consumed = 0;
    while (fetch_bw.has_remaining() && consumed < std::size(l1i_entry.instr_depend_on_me)) {
      const auto depend_id = l1i_entry.instr_depend_on_me[consumed];
      auto fetched = std::partition_point(std::begin(IFETCH_BUFFER), std::end(IFETCH_BUFFER), ooo_model_instr::precedes(depend_id));
      if (fetched != std::end(IFETCH_BUFFER) && fetched->instr_id == depend_id && (fetched->ip.to<uint64_t>() >> block_shamt) == l1i_block
          && fetched->fetch_issued) {
        fetched->fetch_completed = true;
        fetch_bw.consume();
        ++progress;

        if constexpr (champsim::debug_print) {
          fmt::print("[IFETCH] {} instr_id: {} fetch completed\n", __func__, fetched->instr_id);
        }
      }

      ++consumed;
    }
    l1i_entry.instr_depend_on_me.erase(std::begin(l1i_entry.instr_depend_on_me),
                                       std::next(std::begin(l1i_entry.instr_depend_on_me), static_cast<long>(consumed)));

    // remove this entry if we have serviced all of its instructions
    if (l1i_entry.instr_depend_on_me.empty()) {
      l1i_returned.pop_front();
      ++progress;
    }
  }

  auto& l1d_returned = L1D_bus.lower_level->get_returned();
  auto l1d_it = std::begin(l1d_returned);
  for (champsim::bandwidth l1d_bw{L1D_BANDWIDTH}; l1d_bw.has_remaining() && l1d_it != std::end(l1d_returned); l1d_bw.consume(), ++l1d_it) {
    const auto l1d_block = l1d_it->v_address.to<uint64_t>() >> block_shamt;
    // Every fetch-issued load to this block is in the return index, so one
    // lookup replaces the sweep over occupied LQ slots. finish() bumps each
    // owner's completed_mem_ops; finishes touch independent slots and commute,
    // so order is result-equivalent to the old scan. The block's loads leave
    // together, so erase the key.
    auto blk_it = hmr_block_index_.find(l1d_block);
    if (blk_it != std::end(hmr_block_index_)) {
      for (const auto idx : blk_it->second) {
        auto& lq_entry = LQ[idx];
        const auto owner_slot = lq_entry->rob_slot;
        auto& owner = ROB.at_slot(owner_slot);
        lq_entry->finish(owner);
        if (owner.executed && !owner.completed && owner.completed_mem_ops == owner.num_mem_ops()) {
          candidate_set(mem_complete_candidates_, owner_slot);
        }
        complete_stage_clean_ = false; // a memory op finished
        lq_entry.reset();
        lq_clear_occupied(idx);
        ++progress;
      }
      hmr_block_index_.erase(blk_it);
    }
    ++progress;
  }
  l1d_returned.erase(std::begin(l1d_returned), l1d_it);

  return progress;
}

long NMFC_HOST_CORE::retire_rob()
{
  auto [retire_begin, retire_end] =
      champsim::get_span_p(std::cbegin(ROB), std::cend(ROB), champsim::bandwidth{RETIRE_WIDTH}, [](const auto& x) { return x.completed; });
  assert(std::distance(retire_begin, retire_end) >= 0); // end succeeds begin
  if constexpr (champsim::debug_print) {
    std::for_each(retire_begin, retire_end, [cycle = current_time.time_since_epoch() / clock_period](const auto& x) {
      fmt::print("[ROB] retire_rob instr_id: {} is retired cycle: {}\n", x.instr_id, cycle);
    });
  }

  // commit register writes to backend RAT
  // and recycle the old physical registers
  for (auto rob_it = retire_begin; rob_it != retire_end; ++rob_it) {
    for (auto dreg : rob_it->destination_registers) {
      reg_allocator.retire_dest_register(dreg);
    }
  }

  auto retire_count = std::distance(retire_begin, retire_end);
  // Retired entries are completed, hence scheduled. Clamp for robustness
  // against externally injected ROB state (unit tests).
  num_scheduled_ = std::max(num_scheduled_ - retire_count, long{0});
  num_retired += retire_count;
  // Report that we advanced, in instructions -- but only if something is listening: the cycle
  // count below costs a division, and an unobserved hook should cost a branch.
  if (retire_count > 0 && champsim::hooks::progress.active()) {
    champsim::hooks::progress.emit(static_cast<const champsim::modules::packet_consumer&>(*this), static_cast<uint64_t>(num_retired),
                                   static_cast<uint64_t>(current_time.time_since_epoch() / clock_period));
  }
  ROB.erase(retire_begin, retire_end);

  return retire_count;
}

void NMFC_HOST_CORE::fill_from_producers()
{
  for (auto* src : instruction_producer_pimpl) {
    for (auto space = instructions_requested(); space > 0; --space) {
      auto instr = src->next();
      if (!instr.has_value()) {
        break;
      }
      push_instruction(std::move(*instr));
    }
  }
}

std::vector<std::string> NMFC_HOST_CORE::producer_descriptions() const
{
  std::vector<std::string> out;
  for (const auto* src : instruction_producer_pimpl) {
    if (auto desc = src->describe(); !desc.empty()) {
      out.push_back(std::move(desc));
    }
  }
  return out;
}

bool NMFC_HOST_CORE::producers_eof() const
{
  if (instruction_producer_pimpl.empty())
    return true;
  return std::all_of(instruction_producer_pimpl.begin(), instruction_producer_pimpl.end(), [](const auto* src) { return src->eof(); });
}

void NMFC_HOST_CORE::impl_initialize_branch_predictor() const
{
  std::for_each(branch_module_pimpl.begin(), branch_module_pimpl.end(), [](const auto bp) { bp->initialize_branch_predictor(); });
}

void NMFC_HOST_CORE::impl_last_branch_result(champsim::address ip, champsim::address target, bool taken, uint8_t branch_type) const
{
  std::for_each(branch_module_pimpl.begin(), branch_module_pimpl.end(), [&](const auto bp) { bp->last_branch_result(ip, target, taken, branch_type); });
}

bool NMFC_HOST_CORE::impl_predict_branch(champsim::address ip, champsim::address predicted_target, bool always_taken, uint8_t branch_type) const
{
  bool predicted = false;
  std::for_each(branch_module_pimpl.begin(), branch_module_pimpl.end(),
                [&](const auto bp) { predicted |= bp->predict_branch(ip, predicted_target, always_taken, branch_type); });
  return predicted;
}

void NMFC_HOST_CORE::impl_initialize_btb() const
{
  std::for_each(btb_module_pimpl.begin(), btb_module_pimpl.end(), [](const auto btb) { btb->initialize_btb(); });
}

void NMFC_HOST_CORE::impl_update_btb(champsim::address ip, champsim::address predicted_target, bool taken, uint8_t branch_type) const
{
  std::for_each(btb_module_pimpl.begin(), btb_module_pimpl.end(), [&](const auto btb) { btb->update_btb(ip, predicted_target, taken, branch_type); });
}

std::pair<champsim::address, bool> NMFC_HOST_CORE::impl_btb_prediction(champsim::address ip, uint8_t branch_type) const
{
  std::pair<champsim::address, bool> predict_pair{};
  std::for_each(btb_module_pimpl.begin(), btb_module_pimpl.end(), [&](const auto btb) { predict_pair = btb->btb_prediction(ip, branch_type); });
  return predict_pair;
}

// LCOV_EXCL_START Exclude the following function from LCOV
void NMFC_HOST_CORE::print_deadlock()
{
  print_ftu_deadlock(); // NMFC: the tracking unit is part of this core's state

  fmt::print("DEADLOCK! CPU {} cycle {}\n", consumer_id(), current_time.time_since_epoch() / clock_period);

  auto instr_pack = [period = clock_period, this](const auto& entry) {
    return std::tuple{entry.instr_id,
                      entry.fetch_issued,
                      entry.fetch_completed,
                      entry.scheduled,
                      entry.executed,
                      entry.completed,
                      reg_allocator.count_reg_dependencies(entry),
                      entry.num_mem_ops() - entry.completed_mem_ops,
                      entry.ready_time.time_since_epoch() / period};
  };
  std::string_view instr_fmt{
      "instr_id: {} fetch_issued: {} fetch_completed: {} scheduled: {} executed: {} completed: {} num_reg_dependent: {} num_mem_ops: {} event: {}"};
  champsim::range_print_deadlock(IFETCH_BUFFER, "cpu" + std::to_string(consumer_id()) + "_IFETCH", instr_fmt, instr_pack);
  champsim::range_print_deadlock(DECODE_BUFFER, "cpu" + std::to_string(consumer_id()) + "_DECODE", instr_fmt, instr_pack);
  champsim::range_print_deadlock(DISPATCH_BUFFER, "cpu" + std::to_string(consumer_id()) + "_DISPATCH", instr_fmt, instr_pack);
  champsim::range_print_deadlock(ROB, "cpu" + std::to_string(consumer_id()) + "_ROB", instr_fmt, instr_pack);

  // print occupied physical registers
  reg_allocator.print_deadlock();

  // print LQ entry
  auto lq_pack = [period = clock_period](const auto& entry) {
    std::string depend_id{"-"};
    if (entry->producer_id != std::numeric_limits<uint64_t>::max()) {
      depend_id = std::to_string(entry->producer_id);
    }
    return std::tuple{entry->instr_id, entry->virtual_address, entry->fetch_issued, entry->ready_time.time_since_epoch() / period, depend_id};
  };
  std::string_view lq_fmt{"instr_id: {} address: {} fetch_issued: {} event_cycle: {} waits on {}"};

  auto sq_pack = [period = clock_period](const auto& entry) {
    std::vector<uint64_t> depend_ids;
    std::transform(std::begin(entry.lq_depend_on_me), std::end(entry.lq_depend_on_me), std::back_inserter(depend_ids),
                   [](const std::optional<NMFC_LSQ_ENTRY>& lq_entry) { return lq_entry->producer_id; });
    return std::tuple{entry.instr_id, entry.virtual_address, entry.fetch_issued, entry.ready_time.time_since_epoch() / period, depend_ids};
  };
  std::string_view sq_fmt{"instr_id: {} address: {} fetch_issued: {} event_cycle: {} LQ waiting: {}"};
  champsim::range_print_deadlock(LQ, "cpu" + std::to_string(consumer_id()) + "_LQ", lq_fmt, lq_pack);
  champsim::range_print_deadlock(SQ, "cpu" + std::to_string(consumer_id()) + "_SQ", sq_fmt, sq_pack);
}
// LCOV_EXCL_STOP

NMFC_LSQ_ENTRY::NMFC_LSQ_ENTRY(champsim::address addr, champsim::program_ordered<NMFC_LSQ_ENTRY>::id_type id, champsim::address local_ip, champsim::origin local_origin)
    : champsim::program_ordered<NMFC_LSQ_ENTRY>{id}, virtual_address(addr), ip(local_ip), origin(local_origin)
{
}

void NMFC_LSQ_ENTRY::finish(ooo_model_instr& rob_entry) const
{
  assert(rob_entry.instr_id == this->instr_id);

  ++rob_entry.completed_mem_ops;
  assert(rob_entry.completed_mem_ops <= rob_entry.num_mem_ops());

  if constexpr (champsim::debug_print) {
    fmt::print("[LSQ] {} instr_id: {} full_address: {} remain_mem_ops: {}\n", __func__, instr_id, virtual_address,
               rob_entry.num_mem_ops() - rob_entry.completed_mem_ops);
  }
}

bool NMFC_CacheBus::issue_read(request_type data_packet)
{

  data_packet.address = data_packet.v_address;
  data_packet.is_translated = false;
  data_packet.type = access_type::LOAD;

  return lower_level->add_rq(data_packet);
}

bool NMFC_CacheBus::issue_write(request_type data_packet)
{
  data_packet.address = data_packet.v_address;
  data_packet.is_translated = false;
  data_packet.type = access_type::WRITE;
  data_packet.response_requested = false;

  return lower_level->add_wq(data_packet);
}

// ===================== NMFC: the function tracking unit =====================

bool NMFC_HOST_CORE::offload_slots_available(const ooo_model_instr& instr) const
{
  // A join references a token that already holds a slot, so it needs no new
  // one. Counting it as if it did would stall dispatch on a full tracking unit
  // precisely when the join that would drain it is the next thing to run --
  // deadlocking the fork/join pattern this exists to enable.
  long needed = 0;
  for (auto addr : instr.source_memory) {
    if (is_offload(addr) && find_token((addr.to<uint64_t>() - aperture_base_) >> nmfc_block_bits_) >= FTU.size()) {
      ++needed;
    }
  }
  if (needed == 0) {
    return true;
  }
  return FTU.size() - ftu_occupied_ >= needed;
}

std::size_t NMFC_HOST_CORE::find_token(uint64_t token) const
{
  const auto it = std::find_if(std::begin(FTU), std::end(FTU), [token](const auto& entry) { return entry.has_value() && entry->token == token; });
  return static_cast<std::size_t>(std::distance(std::begin(FTU), it));
}

void NMFC_HOST_CORE::satisfy_waiter(ftu_waiter& waiter)
{
  // The same bookkeeping LSQ_ENTRY::finish does for a load: the owning
  // instruction is still resident, so its physical slot indexes in O(1).
  if (!waiter.pending) {
    return;
  }
  auto& owner = ROB.at_slot(waiter.rob_slot);
  if (owner.instr_id == waiter.instr_id) {
    ++owner.completed_mem_ops;
    assert(owner.completed_mem_ops <= owner.num_mem_ops());
    if (owner.executed && !owner.completed && owner.completed_mem_ops == owner.num_mem_ops()) {
      candidate_set(mem_complete_candidates_, waiter.rob_slot);
    }
    complete_stage_clean_ = false;
  }
  waiter.pending = false;
}

void NMFC_HOST_CORE::retire_if_done(std::size_t idx)
{
  auto& slot = FTU.at(idx);
  if (!slot.has_value()) {
    return;
  }
  auto& entry = *slot;
  if (entry.call.pending || entry.join.pending || !entry.returned) {
    return;
  }
  // A forked invocation keeps its slot until its join has been seen: the result
  // exists, but nothing has asked for it yet.
  if (entry.deferred && !entry.join_seen) {
    return;
  }
  slot.reset();
  --ftu_occupied_;
  ++offloads_completed_;
}

void NMFC_HOST_CORE::allocate_offload(const ooo_model_instr& instr, std::size_t rob_slot, champsim::address addr)
{
  // The aperture address encodes the token, so no side table is needed to get
  // from an instruction back to the invocation it names.
  const auto token = (addr.to<uint64_t>() - aperture_base_) >> nmfc_block_bits_;

  // A second reference to a token already in flight is the JOIN half of
  // fork/join: the same aperture address, so no extra encoding is needed to
  // tell them apart -- an entry already existing *is* the distinction.
  if (const auto existing = find_token(token); existing < FTU.size()) {
    auto& entry = *FTU[existing];
    ++offload_joins_;
    entry.join_seen = true;
    entry.join = ftu_waiter{instr.instr_id, rob_slot, true};
    if (entry.returned) {
      // Already home: the fork bought the whole latency and the join is free.
      ++offload_joins_already_home_;
      satisfy_waiter(entry.join);
    }
    retire_if_done(existing);
    return;
  }

  auto slot = std::find_if(std::begin(FTU), std::end(FTU), [](const auto& entry) { return !entry.has_value(); });
  assert(slot != std::end(FTU)); // dispatch gated on availability above

  ftu_entry fresh{};
  fresh.token = token;
  fresh.origin = instr.origin;
  fresh.call = ftu_waiter{instr.instr_id, rob_slot, true};
  slot->emplace(fresh);
  ++ftu_occupied_;
  ftu_dispatch_queue_.push_back(static_cast<std::size_t>(std::distance(std::begin(FTU), slot)));
  ++offloads_issued_;
}

long NMFC_HOST_CORE::dispatch_offloads()
{
  long progress{0};
  while (!ftu_dispatch_queue_.empty()) {
    const auto idx = ftu_dispatch_queue_.front();
    auto& entry = FTU.at(idx);
    if (!entry.has_value()) {
      ftu_dispatch_queue_.pop_front(); // squashed or already retired
      continue;
    }

    const auto* body = image_->lookup(entry->token);
    if (body == nullptr) {
      fmt::print("[{}] ERROR: no published body for invocation token {}\n", NAME, entry->token);
      std::abort();
    }

    nmfc::invocation_msg msg{};
    msg.token = entry->token;
    msg.origin = entry->origin;
    msg.home_host = host_id_;
    msg.entry_pc = body->entry_pc;
    msg.body = body;

    if (!fabric_->dispatch(msg)) {
      ++offload_dispatch_stalls_; // the network is full; hold order and retry
      break;
    }
    entry->dispatched = true;
    entry->deferred = body->deferred_join();
    ftu_dispatch_queue_.pop_front();
    ++progress;

    // Fire and forget: nothing downstream consumes a result, so the tracking
    // slot frees now rather than on a return that will never come.
    if (body->no_return()) {
      ++offload_fire_and_forget_;
      satisfy_waiter(entry->call);
      entry->returned = true; // nothing will come back, and nothing waits for it
      entry->join_seen = true;
      retire_if_done(idx);
      continue;
    }

    // Fork: retire the call now and keep the entry alive for a later join.
    // Without this the call sits at the head of the reorder buffer for the
    // whole invocation, and in-flight offloads are capped by what fits behind
    // it -- which is the opposite of what a machine with a thousand contexts
    // wants.
    if (entry->deferred) {
      ++offload_forks_;
      satisfy_waiter(entry->call);
      retire_if_done(idx);
    }
  }
  return progress;
}

void NMFC_HOST_CORE::complete_offload(std::size_t idx)
{
  auto& slot = FTU.at(idx);
  if (!slot.has_value()) {
    return;
  }
  satisfy_waiter(slot->call);
  satisfy_waiter(slot->join);
  slot->returned = true;
  slot->join_seen = true;
  retire_if_done(idx);
}

void NMFC_HOST_CORE::print_ftu_deadlock() const
{
  const auto occupied = ftu_occupied_;
  fmt::print("[{}_FTU] occupied: {}/{} dispatch queue: {}\n", NAME, occupied, FTU.size(), ftu_dispatch_queue_.size());
  std::size_t shown = 0;
  for (std::size_t idx = 0; idx < FTU.size() && shown < 8; ++idx) {
    if (FTU[idx].has_value()) {
      const auto& e = *FTU[idx];
      fmt::print("[{}_FTU]   slot {} token {} dispatched {} returned {} deferred {} join_seen {} call_pending {} join_pending {}\n", NAME, idx, e.token, e.dispatched, e.returned,
                 e.deferred, e.join_seen, e.call.pending, e.join.pending);
      ++shown;
    }
  }
}

void NMFC_HOST_CORE::accept_return(const nmfc::completion_msg& msg)
{
  const auto idx = find_token(msg.token);
  if (idx >= FTU.size()) {
    return;
  }
  auto& entry = *FTU[idx];
  entry.returned = true;

  // A blocking call is satisfied by the return itself; a forked one was already
  // satisfied at dispatch, so it is its join that is waiting here.
  satisfy_waiter(entry.call);
  satisfy_waiter(entry.join);
  retire_if_done(idx);
}

// NMFC: the champsim::modules::core_module member definitions that follow here
// in the base belong to that translation unit; they are shared, not forked.

champsim::modules::core_module::register_module<NMFC_HOST_CORE> nmfc_host_core_module("NMFC_HOST_CORE");
