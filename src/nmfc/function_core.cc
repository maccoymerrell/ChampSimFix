/*
 * FUNCTION_CORE — the near-memory engine, designed rather than copied.
 *
 * Multi-context, in-order per context, non-speculative. No ROB, no rename, no
 * branch predictor, no load/store queue. A context is a PC into its body plus
 * at most one cache block of registers, so creating and tearing one down is a
 * slot write and an arbitrary number of them is affordable — which is the whole
 * point: hundreds of serial kernels time-multiplexed onto one channel.
 *
 * The scheduling rule that turns serial work into channel saturation is that a
 * context which issues a memory operation does *not* block on it. It marks the
 * destination register not-ready, keeps its PC moving, and only stops when an
 * instruction actually needs a value that has not come back. That in-order
 * scoreboard is also where intra-function MLP comes from, and it costs no extra
 * trace field: the register ids are already in the record.
 *
 * Routing is decided on the *virtual* address, before any translation, which is
 * what makes a migration decision independent of the MMU. Congruent allocation
 * is the promise that the frame lands on the tile the VA named; TILE_PORT
 * asserts it.
 *
 * Parameters:
 *   tile              which memory tile this core belongs to
 *   clock_period      time
 *   num_contexts      hardware contexts (the headline knob)
 *   issue_width       contexts issuing per cycle (default 4)
 *   fabric            @function_fabric
 *   image             @function_image
 *   dcache            @channel into this tile's function-core data cache
 *   icache            @channel into its instruction cache, or {"null": "channel"}
 *   fetch_latency     cycles charged per fetch when icache is null (default 4)
 *   fetch_bubble      extra cycles at a taken-branch target (default 1)
 *   {alu,mul,div,fp,fp_div,branch}_latency   cycles by op class
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <unordered_map>
#include <utility>
#include <unordered_set>
#include <vector>
#include <fmt/core.h>

#include "bandwidth.h"
#include "channel.h"
#include "modules.h"
#include "nmfc/tile_router.h"
#include "nmfc/function_core.h"
#include "nmfc/function_fabric.h"
#include "nmfc/function_image.h"
#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_hooks.h"
#include "nmfc/nmfc_types.h"
#include "nmfc/nmfc_vmem.h"
#include "nmfc/translation_engine.h"
#include "stat_report.h"

namespace
{

class function_core : public nmfc::function_core_module
{
  using channel_type = champsim::modules::channel_module;

public:
  explicit function_core(champsim::modules::ModuleBuilder builder)
      : nmfc::function_core_module(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")), tile_(builder.get_parameter<std::size_t>("tile")),
        map_(nmfc::tile_map_from(builder)), fabric_(builder.get_parameter<nmfc::function_fabric_module*>("fabric")),
        image_(builder.get_parameter<nmfc::function_image_module*>("image")), router_(builder.get_parameter<nmfc::tile_router_module*>("router")),
        placement_(dynamic_cast<nmfc::page_placement_sink*>(builder.get_parameter<champsim::modules::vmem_module*>("vmem", true, nullptr))),
        dcache_(builder.get_parameter<channel_type*>("dcache")),
        icache_(builder.get_parameter<channel_type*>("icache")),
        issue_width_(builder.get_parameter<champsim::bandwidth::maximum_type>("issue_width", true, champsim::bandwidth::maximum_type{4})),
        fetch_latency_(nmfc::cycles_from(builder, "fetch_latency", 4)), fetch_bubble_(nmfc::cycles_from(builder, "fetch_bubble", 1)),
        block_bits_(builder.get_parameter<unsigned>("log2_block_size", true, 6U)),
        page_bits_(builder.get_parameter<unsigned>("log2_page_size", true, 12U)), vmem_(builder.get_parameter<champsim::modules::vmem_module*>("vmem", true, nullptr))
  {
    const auto num_contexts = builder.get_parameter<std::size_t>("num_contexts", true, std::size_t{128});
    contexts_.resize(num_contexts);
    cold_.assign(num_contexts, false);
    free_slots_.reserve(num_contexts);
    for (std::size_t slot = num_contexts; slot-- > 0;) {
      free_slots_.push_back(slot);
    }

    latency_[idx(nmfc::op_class::ALU)] = nmfc::cycles_from(builder, "alu_latency", 1);
    latency_[idx(nmfc::op_class::MUL)] = nmfc::cycles_from(builder, "mul_latency", 3);
    latency_[idx(nmfc::op_class::DIV)] = nmfc::cycles_from(builder, "div_latency", 20);
    latency_[idx(nmfc::op_class::FP)] = nmfc::cycles_from(builder, "fp_latency", 4);
    latency_[idx(nmfc::op_class::FP_DIV)] = nmfc::cycles_from(builder, "fp_div_latency", 20);
    latency_[idx(nmfc::op_class::BRANCH)] = nmfc::cycles_from(builder, "branch_latency", 1);
    latency_[idx(nmfc::op_class::LOAD)] = nmfc::cycles_from(builder, "alu_latency", 1);
    latency_[idx(nmfc::op_class::STORE)] = nmfc::cycles_from(builder, "alu_latency", 1);

    // The MMU is a channel model, so it arrives as a channel reference and the
    // translation service is reached by cast. Without one, translation falls
    // back to an oracle: correct addresses, no modeled cost. That is an
    // explicit modelling choice, so say so rather than let it pass silently.
    if (auto* mmu_channel = builder.get_parameter<channel_type*>("mmu", true, nullptr); mmu_channel != nullptr) {
      mmu_ = dynamic_cast<nmfc::translation_engine*>(mmu_channel);
      if (mmu_ == nullptr) {
        fmt::print("[{}] ERROR: the module wired as \"mmu\" does not provide translation; use NMFC_MMU\n", builder.get_name());
        std::exit(-1);
      }
    }

    fabric_->attach_tile(tile_, this);
  }

  // ---- function_core_module ----

  [[nodiscard]] std::size_t tile_index() const override { return tile_; }
  [[nodiscard]] std::size_t free_contexts() const override { return free_slots_.size(); }
  [[nodiscard]] std::size_t num_contexts() const override { return contexts_.size(); }

  bool accept(const nmfc::invocation_msg& msg) override
  {
    if (free_slots_.empty() || msg.body == nullptr) {
      return false;
    }
    const auto slot = take_slot();
    auto& ctx = contexts_[slot];
    ctx.reset();
    ctx.token = msg.token;
    ctx.origin = msg.origin;
    ctx.home_host = msg.home_host;
    ctx.body = msg.body;
    ctx.live_regs = msg.body->live_regs;
    ctx.ready.fill(true); // arguments are live on entry
    ctx.arrived = current_time;
    make_ready(slot, current_time);
    ++accepted_;
    return true;
  }

  bool accept_migration(const nmfc::context& incoming) override
  {
    if (free_slots_.empty()) {
      return false;
    }
    const auto slot = take_slot();
    auto& ctx = contexts_[slot];
    ctx = incoming;
    ctx.arrived = current_time;
    // Arriving cold: no fetched block, no translations. The cycles spent
    // re-establishing them are what the cold-start statistic counts.
    cold_[slot] = true;
    make_ready(slot, current_time);
    ++arrived_;
    return true;
  }

  // ---- the cycle ----

  /**
   * Where a resident context's cycles actually go.
   *
   * Residency, occupancy and throughput are tied to each other by Little's
   * law, so none of the three can explain a change in the other two -- they
   * are one measurement in three units. Attributing a change needs the
   * breakdown underneath them, and sampling gets it for a cost that does not
   * scale with context count: one pass every sample_period_ cycles, rather
   * than an accumulator on every state transition.
   */
  void sample_waits()
  {
    for (const auto& ctx : contexts_) {
      if (ctx.state == nmfc::ctx_state::FREE) {
        continue;
      }
      ++wait_resident_;
      if (ctx.state == nmfc::ctx_state::MIGRATING) {
        ++wait_migration_;
      } else if (ctx.waiting_lock) {
        ++wait_lock_;
      } else if (ctx.awaiting_translation) {
        ++wait_translation_;
      } else if (ctx.pending_mem > 0 && ctx.state == nmfc::ctx_state::BLOCKED) {
        ++wait_memory_;
      } else if (ctx.state == nmfc::ctx_state::BLOCKED) {
        ++wait_blocked_other_;
      } else if (ctx.state == nmfc::ctx_state::READY) {
        // READY covers two very different things. A context waiting out the
        // latency of the instruction it just issued sits in the timer queue
        // with a wake time in the future -- that is execution, not queueing.
        // Only a context whose wake time has passed is actually queued behind
        // issue width. Counting them together hides which of the two grows.
        if (ctx.wake_time > current_time) {
          ++wait_latency_;
        } else {
          ++wait_issue_;
        }
      } else {
        ++wait_running_;
      }
    }
  }

  long operate() final
  {
    if (sample_period_ != 0 && (sample_tick_++ % sample_period_) == 0) {
      sample_waits();
    }
    long progress = 0;
    port_blocked_ = false;
    progress += drain_returns();
    progress += drain_releases();
    progress += drain_translations();
    progress += wake_timers();
    progress += issue_cycle();
    progress += push_completions();
    progress += push_migrations();

    // Time-weighted, not per-call: operate() is skipped on idle cycles, so
    // counting per call averages over busy cycles only and reports a machine
    // far busier than it is. The elapsed span since the last sample covers
    // whatever was skipped.
    const auto occupied = static_cast<std::uint64_t>(contexts_.size() - free_slots_.size());
    occupancy_time_ += occupied * ((current_time - last_sample_) / clock_period);
    last_sample_ = current_time;
    if (port_blocked_) {
      ++port_busy_cycles_;
    }
    return progress;
  }

  long poll_cycle() final
  {
    // Work arrives by external push -- the fabric delivering, a cache
    // returning, the MMU resolving -- so never skip more than one cycle, and
    // every one of those sources has to appear here. A context blocked on
    // translation is invisible to the queues below, so omitting the MMU makes
    // the core declare itself idle and skip forever without ever draining the
    // completion that would have woken it.
    const bool idle = ready_.empty() && timers_.empty() && done_.empty() && migrating_.empty() && std::empty(dcache_->get_returned())
                      && (icache_ == nullptr || std::empty(icache_->get_returned()))
                      && (mmu_ == nullptr || mmu_->translation_completions().empty());
    return idle ? 1 : 0;
  }

  void begin_phase(bool /*warmup*/) override
  {
    accepted_ = arrived_ = departed_ = completed_ = 0;
    spawned_ = spawn_stalls_ = 0;
    instructions_ = loads_ = stores_ = atomics_ = fetches_ = 0;
    scoreboard_stalls_ = port_retries_ = atomic_conflicts_ = translation_stalls_ = 0;
    atomic_wait_cycles_ = atomic_forwards_ = fetch_retries_ = data_retries_ = 0;
    wait_resident_ = wait_memory_ = wait_translation_ = wait_lock_ = 0;
    wait_issue_ = wait_migration_ = wait_blocked_other_ = wait_running_ = wait_latency_ = 0;
    port_busy_cycles_ = 0;
    ctx_code_hits_ = ctx_code_misses_ = ctx_data_hits_ = ctx_data_misses_ = 0;
    occupancy_time_ = 0;
    phase_start_ = current_time;
    last_sample_ = current_time;
    peak_occupancy_ = contexts_.size() - free_slots_.size();
    residency_sum_ = 0;
    residency_count_ = 0;
    cold_start_cycles_ = 0;
    cold_start_count_ = 0;
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (accepted_ == 0 && arrived_ == 0) {
      return;
    }
    // Elapsed cycles, including the ones this core skipped: a function core
    // that is idle is still part of the machine, and its idleness is the thing
    // worth seeing.
    const auto elapsed = static_cast<std::uint64_t>((current_time - phase_start_) / clock_period);
    const auto occupancy = elapsed == 0 ? 0.0 : static_cast<double>(occupancy_time_) / static_cast<double>(elapsed);
    const auto residency = residency_count_ == 0 ? 0.0 : static_cast<double>(residency_sum_) / static_cast<double>(residency_count_);
    const auto ipc = elapsed == 0 ? 0.0 : static_cast<double>(instructions_) / static_cast<double>(elapsed);

    out.line(fmt::format("{} TILE {} INVOCATIONS: {} MIGRATED IN: {} OUT: {} COMPLETED: {}", NAME, tile_, accepted_, arrived_, departed_, completed_));
    // IPC from this core's own perspective. Reporting only the host's IPC
    // describes a machine that is deliberately doing its work elsewhere, so
    // every core that executes instructions reports its own rate.
    out.line(fmt::format("{} INSTRUCTIONS: {} CYCLES: {} IPC: {:.4f}", NAME, instructions_, elapsed, ipc));
    out.line(fmt::format("{} LOADS: {} STORES: {} ATOMICS: {} FETCHES: {} SPAWNED: {} (stalls {})", NAME, loads_, stores_, atomics_, fetches_, spawned_,
                         spawn_stalls_));
    out.line(fmt::format("{} CONTEXT OCCUPANCY mean: {:.2f} peak: {} of {}", NAME, occupancy, peak_occupancy_, contexts_.size()));
    // Retries are per context per cycle, so they scale with occupancy and do not
    // compare to anything; the cycle count does, which is why it leads.
    const auto port_pct = elapsed == 0 ? 0.0 : 100.0 * static_cast<double>(port_busy_cycles_) / static_cast<double>(elapsed);
    out.line(fmt::format("{} MEAN RESIDENCY: {:.1f} cycles STALLS scoreboard: {} atomic: {}", NAME, residency, scoreboard_stalls_, atomic_conflicts_));
    out.line(fmt::format("{} ATOMIC WAIT: {} context-cycles over {} waits, {} of {} updates took a forwarded value", NAME, atomic_wait_cycles_,
                         atomic_conflicts_, atomic_forwards_, atomics_));
    out.line(fmt::format("{} PORT RETRIES fetch: {} data: {}", NAME, fetch_retries_, data_retries_));
    const auto share = [&](std::uint64_t n) { return wait_resident_ == 0 ? 0.0 : 100.0 * static_cast<double>(n) / static_cast<double>(wait_resident_); };
    out.line(fmt::format("{} RESIDENCY SPLIT memory: {:.1f}% issue-queue: {:.1f}% exec-latency: {:.1f}% translation: {:.1f}% lock: {:.1f}% "
                         "migration: {:.1f}% other-blocked: {:.1f}% running: {:.1f}% (of {} samples)",
                         NAME, share(wait_memory_), share(wait_issue_), share(wait_latency_), share(wait_translation_), share(wait_lock_),
                         share(wait_migration_), share(wait_blocked_other_), share(wait_running_), wait_resident_));
    out.line(fmt::format("{} PORT BLOCKED: {} cycles ({:.1f}% of elapsed) over {} retries", NAME, port_busy_cycles_, port_pct, port_retries_));
    const auto cold_start = cold_start_count_ == 0 ? 0.0 : static_cast<double>(cold_start_cycles_) / static_cast<double>(cold_start_count_);
    out.line(fmt::format("{} MIGRATION COLD START: {} cycles over {} arrivals (mean {:.1f})", NAME, cold_start_cycles_, cold_start_count_, cold_start));

    auto json = out.json();
    json.add("tile", tile_);
    json.add("invocations", accepted_);
    json.add("migrated_in", arrived_);
    json.add("migrated_out", departed_);
    json.add("completed", completed_);
    json.add("instructions", instructions_);
    json.add("cycles", elapsed);
    json.add("ipc", ipc);
    json.add("loads", loads_);
    json.add("stores", stores_);
    json.add("atomics", atomics_);
    json.add("fetches", fetches_);
    json.add("mean_context_occupancy", occupancy);
    json.add("peak_context_occupancy", peak_occupancy_);
    json.add("num_contexts", contexts_.size());
    json.add("mean_residency_cycles", residency);
    json.add("scoreboard_stalls", scoreboard_stalls_);
    json.add("spawned", spawned_);
    json.add("port_retries", port_retries_);
    json.add("port_busy_cycles", port_busy_cycles_);
    json.add("atomic_conflicts", atomic_conflicts_);
    json.add("atomic_wait_cycles", atomic_wait_cycles_);
    json.add("atomic_forwards", atomic_forwards_);
    json.add("resident_samples", wait_resident_);
    json.add("wait_memory", wait_memory_);
    json.add("wait_issue_queue", wait_issue_);
    json.add("wait_exec_latency", wait_latency_);
    json.add("wait_translation", wait_translation_);
    json.add("wait_lock", wait_lock_);
    json.add("wait_migration", wait_migration_);
    const auto code_total = ctx_code_hits_ + ctx_code_misses_;
    const auto data_total = ctx_data_hits_ + ctx_data_misses_;
    const auto code_rate = code_total == 0 ? 0.0 : 100.0 * static_cast<double>(ctx_code_hits_) / static_cast<double>(code_total);
    const auto data_rate = data_total == 0 ? 0.0 : 100.0 * static_cast<double>(ctx_data_hits_) / static_cast<double>(data_total);
    // The code figure is expected to be near zero and is not a defect: the
    // fetch-block check already prevents a re-translation while a context stays
    // in one instruction block, so the code entry is consulted about once per
    // context arrival and is necessarily cold then. It is the data entries that
    // carry the mechanism.
    out.line(fmt::format("{} PER-CONTEXT TRANSLATION data: {:.1f}% of {} lookups (code: {:.1f}% of {}, ~1 per arrival) STALLS: {}", NAME, data_rate, data_total,
                         code_rate, code_total, translation_stalls_));
    json.add("ctx_xlat_code_hit_rate", code_rate);
    json.add("ctx_xlat_code_lookups", code_total);
    json.add("ctx_xlat_data_hit_rate", data_rate);
    json.add("ctx_xlat_data_lookups", data_total);
    json.add("translation_stalls", translation_stalls_);
    json.add("cold_start_cycles", cold_start_cycles_);
    json.add("cold_start_arrivals", cold_start_count_);
  }

  void print_deadlock() final
  {
    fmt::print("[{}] tile {} occupied: {}/{} ready: {} timed: {} done: {} migrating: {} outstanding: {}\n", NAME, tile_, contexts_.size() - free_slots_.size(),
               contexts_.size(), ready_.size(), timers_.size(), done_.size(), migrating_.size(), outstanding_.size());
    for (std::size_t slot = 0; slot < contexts_.size(); ++slot) {
      const auto& ctx = contexts_[slot];
      if (ctx.state != nmfc::ctx_state::FREE) {
        fmt::print("[{}]   slot {} token {} pc {}/{} state {} pending_mem {}\n", NAME, slot, ctx.token, ctx.pc,
                   ctx.body == nullptr ? 0 : ctx.body->instrs.size(), static_cast<int>(ctx.state), ctx.pending_mem);
      }
    }
  }

private:
  static constexpr std::size_t idx(nmfc::op_class cls) { return static_cast<std::size_t>(cls); }

  std::size_t take_slot()
  {
    const auto slot = free_slots_.back();
    free_slots_.pop_back();
    peak_occupancy_ = std::max(peak_occupancy_, contexts_.size() - free_slots_.size());
    return slot;
  }

  void release_lock(std::uint64_t block)
  {
    // Hand the address straight to the next waiter rather than freeing it. The
    // value that just arrived is still authoritative -- no other agent can
    // reach this block -- so the waiter inherits both the lock and the data,
    // and never issues a fetch of its own. Ownership passes without the lock
    // ever being free, which is also what keeps the forwarded value correct:
    // no third context can slip in and change the address in between.
    if (hand_off(block)) {
      return;
    }
    locked_blocks_.erase(block);
  }

  /** Pass a held address to the next context queued on it. */
  bool hand_off(std::uint64_t block)
  {
    auto it = lock_waiters_.find(block);
    if (it == lock_waiters_.end()) {
      return false;
    }
    auto& queue = it->second;
    bool handed = false;
    while (!queue.empty()) {
      const auto [slot, token, parked_at] = queue.front();
      queue.pop_front();
      auto& ctx = contexts_[slot];
      if (ctx.token != token) {
        continue; // the slot was reused; this entry names an invocation that left
      }
      atomic_wait_cycles_ += static_cast<std::uint64_t>((current_time - parked_at) / clock_period);
      ctx.waiting_lock = false;
      if (ctx.state != nmfc::ctx_state::BLOCKED) {
        continue; // a memory response already woke it; it will ask again
      }
      ctx.held_lock = block;
      ctx.holds_lock = true;
      ctx.forwarded_lock = block;
      ctx.has_forwarded_value = true;
      make_ready(slot, current_time);
      handed = true;
      break;
    }
    if (queue.empty()) {
      lock_waiters_.erase(it);
    }
    return handed;
  }

  /** Give up an address a forwarded update finished with. */
  long drain_releases()
  {
    long progress = 0;
    for (auto it = releases_.begin(); it != releases_.end();) {
      if (it->at > current_time) {
        ++it;
        continue;
      }
      auto& ctx = contexts_[it->slot];
      if (ctx.token == it->token && ctx.holds_lock && ctx.held_lock == it->key) {
        ctx.holds_lock = false;
      }
      const auto key = it->key;
      it = releases_.erase(it);
      release_lock(key);
      ++progress;
    }
    return progress;
  }

  /** Block a context until the holder of `block` releases it. */
  void park_on_lock(std::size_t slot, nmfc::context& ctx, std::uint64_t block)
  {
    ctx.state = nmfc::ctx_state::BLOCKED;
    if (ctx.waiting_lock) {
      return; // a memory response woke it while parked; its entry is still queued
    }
    ctx.waiting_lock = true;
    lock_waiters_[block].push_back(lock_waiter{slot, ctx.token, current_time});
  }

  /** Give up any lock this context is holding. Safe to call when it holds none. */
  void release_context_lock(nmfc::context& ctx)
  {
    if (ctx.holds_lock) {
      release_lock(ctx.held_lock);
      ctx.holds_lock = false;
    }
  }

  void release_slot(std::size_t slot)
  {
    auto& ctx = contexts_[slot];
    // Belt and braces: a context must never take a lock out of the machine with
    // it, however it happens to leave.
    release_context_lock(ctx);
    residency_sum_ += static_cast<std::uint64_t>((current_time - ctx.arrived) / clock_period);
    ++residency_count_;
    ctx.reset();
    free_slots_.push_back(slot);
  }

  void make_ready(std::size_t slot, champsim::chrono::clock::time_point at)
  {
    contexts_[slot].state = nmfc::ctx_state::READY;
    contexts_[slot].wake_time = at;
    if (at <= current_time) {
      ready_.push_back(slot);
    } else {
      timers_.emplace(at, slot);
    }
  }

  long wake_timers()
  {
    long progress = 0;
    while (!timers_.empty() && timers_.begin()->first <= current_time) {
      ready_.push_back(timers_.begin()->second);
      timers_.erase(timers_.begin());
      ++progress;
    }
    return progress;
  }

  // ---- responses ----

  /** One outstanding memory or fetch request, keyed by the block it will return. */
  struct outstanding_op {
    std::size_t slot;
    std::uint64_t token; // guards against a slot recycled before a late response
    std::array<std::uint8_t, nmfc::MAX_DST_REGS> dst{};
    bool is_fetch = false;
    std::uint64_t lock_block = 0;
    bool holds_lock = false;
  };

  long drain_returns()
  {
    long progress = 0;
    progress += drain_channel(*dcache_);
    if (icache_ != nullptr) {
      progress += drain_channel(*icache_);
    }
    return progress;
  }

  long drain_channel(channel_type& channel)
  {
    long progress = 0;
    auto& returned = channel.get_returned();
    for (const auto& response : returned) {
      // Match on the virtual address, the way a core matches an L1D return:
      // the request went down untranslated, so this is the key both ends agree on.
      const auto block = response.v_address.to<std::uint64_t>() >> block_bits_;
      auto it = outstanding_.find(block);
      if (it == std::end(outstanding_)) {
        continue; // a merged fill for a block nobody is waiting on any more
      }
      for (const auto& op : it->second) {
        complete_op(op);
        ++progress;
      }
      outstanding_.erase(it);
    }
    returned.clear();
    return progress;
  }

  void complete_op(const outstanding_op& op)
  {
    // Release the lock first, and unconditionally. It belongs to the operation,
    // not to the context: returning early on a departed context used to strand
    // the block permanently, and every other context wanting it then spun
    // forever on an atomic conflict.
    if (op.holds_lock) {
      release_lock(op.lock_block);
    }

    auto& ctx = contexts_[op.slot];
    if (ctx.state == nmfc::ctx_state::FREE || ctx.token != op.token) {
      return; // the context left; this response has nothing to wake
    }
    if (op.holds_lock) {
      ctx.holds_lock = false;
    }
    if (ctx.pending_mem > 0) {
      --ctx.pending_mem;
    }
    for (const auto reg : op.dst) {
      if (reg != 0) {
        ctx.ready[reg - 1] = true;
      }
    }
    if (ctx.state == nmfc::ctx_state::BLOCKED) {
      make_ready(op.slot, current_time);
    }
  }

  // ---- issue ----

  long issue_cycle()
  {
    long progress = 0;
    champsim::bandwidth bw{issue_width_};
    std::size_t examined = 0;
    const auto queued = ready_.size();

    while (bw.has_remaining() && !ready_.empty() && examined < queued) {
      const auto slot = ready_.front();
      ready_.pop_front();
      ++examined;

      auto& ctx = contexts_[slot];
      if (ctx.state != nmfc::ctx_state::READY) {
        continue; // it left the ready state between being queued and now
      }
      if (ctx.wake_time > current_time) {
        timers_.emplace(ctx.wake_time, slot);
        continue; // not due yet; costs no issue slot
      }
      if (try_issue(slot)) {
        bw.consume();
        ++progress;
      }
    }
    return progress;
  }

  /** Advance one context by one instruction. Returns true if an issue slot was used. */
  bool try_issue(std::size_t slot)
  {
    auto& ctx = contexts_[slot];
    const auto& body = *ctx.body;

    if (ctx.pc >= body.instrs.size()) {
      // The body is finished, but outstanding stores must land before the
      // invocation can be reported complete.
      if (ctx.pending_mem > 0) {
        ctx.state = nmfc::ctx_state::BLOCKED;
        return false;
      }
      ctx.state = nmfc::ctx_state::DONE;
      done_.push_back(slot);
      return true;
    }

    const auto& instr = body.instrs[ctx.pc];
    // The instruction virtual address is the same on every tile: a function's
    // code is one virtual page aliased to one physical copy per channel, so the
    // local MMU resolves it to whatever copy lives here. A context therefore
    // never migrates for an instruction fetch, and its program counter does not
    // change when it moves -- there is no per-tile bias to apply.
    const auto eff_ip = instr.ip.to<std::uint64_t>();

    // Instruction fetch, once per block. A tight loop pays this once.
    if (const auto block = eff_ip >> block_bits_; !ctx.has_fetched || ctx.fetched_block != block) {
      if (!issue_fetch(slot, eff_ip)) {
        ready_.push_back(slot); // port busy; try again next cycle
        ++port_retries_;
        ++fetch_retries_;
        port_blocked_ = true;
        return false;
      }
      ctx.fetched_block = block;
      ctx.has_fetched = true;
      ++fetches_;
      return true;
    }

    // First real instruction after a migration: everything since arrival was
    // re-establishing state the context used to have and could not carry.
    if (cold_[slot]) {
      cold_[slot] = false;
      cold_start_cycles_ += static_cast<std::uint64_t>((current_time - ctx.arrived) / clock_period);
      ++cold_start_count_;
    }

    // Scoreboard: the only thing that actually stops an in-order context.
    for (const auto reg : instr.src_reg) {
      if (reg != 0 && !ctx.ready[reg - 1]) {
        ctx.state = nmfc::ctx_state::BLOCKED;
        ++scoreboard_stalls_;
        return false;
      }
    }

    if (instr.is_spawn) {
      return issue_spawn(slot, instr);
    }

    if (instr.num_mem_ops() > 0) {
      return issue_memory(slot, instr);
    }

    // Pure compute: the result is available after this op's latency, and
    // because the context is in order, re-arming it delays everything behind it.
    for (const auto reg : instr.dst_reg) {
      if (reg != 0) {
        ctx.ready[reg - 1] = true;
      }
    }
    auto delay = latency_[idx(instr.cls)];
    if (instr.taken_target()) {
      delay += fetch_bubble_; // replayed control flow must not come for free
    }
    ++ctx.pc;
    ++instructions_;
    make_ready(slot, current_time + delay);
    return true;
  }

  /**
   * Discard a context's own translations if a page has moved under them.
   *
   * A context caches only a handful of entries, but they are the ones it is
   * about to use, so a remap that left them standing would send it to a frame
   * that is no longer its own. Coarse on purpose: this is a shootdown.
   */
  void honour_remaps(nmfc::context& ctx)
  {
    if (placement_ == nullptr) {
      return;
    }
    const auto& log = placement_->remap_log();
    if (ctx.xlat_generation >= log.size()) {
      return;
    }
    // Only the entries for grains that actually moved. A context holds a few
    // entries and they are the ones it is about to use, so this is cheap and
    // exact where discarding all of them would price a page migration as a
    // machine-wide flush.
    for (auto i = ctx.xlat_generation; i < log.size(); ++i) {
      const auto [asid, vgrain] = log[i];
      if (asid != ctx.origin.asid()) {
        continue;
      }
      const auto in_grain = [&](const nmfc::ctx_translation& e) { return e.valid && (e.vpage >> (map_.grain_bits() - e.shift)) == vgrain; };
      if (in_grain(ctx.xlat.code)) {
        ctx.xlat.code = {};
      }
      for (auto& entry : ctx.xlat.data) {
        if (in_grain(entry)) {
          entry = {};
        }
      }
    }
    ctx.xlat_generation = log.size();
  }

  /** The context's own entries, which is where translation locality actually lives. */
  const nmfc::ctx_translation* context_lookup(const nmfc::context& ctx, std::uint64_t vaddr, bool code) const
  {
    if (code) {
      return ctx.xlat.code.covers(vaddr) ? &ctx.xlat.code : nullptr;
    }
    for (const auto& entry : ctx.xlat.data) {
      if (entry.covers(vaddr)) {
        return &entry;
      }
    }
    return nullptr;
  }

  void context_fill(nmfc::context& ctx, bool code, const nmfc::translation_done& done)
  {
    const unsigned shift = done.huge ? map_.grain_bits() : page_bits_;
    const nmfc::ctx_translation entry{done.vpage, done.ppage, shift, true};
    if (code) {
      ctx.xlat.code = entry;
      return;
    }
    ctx.xlat.data[ctx.xlat.next_victim] = entry;
    ctx.xlat.next_victim = static_cast<std::uint8_t>((ctx.xlat.next_victim + 1) % nmfc::MAX_CTX_DATA_XLAT);
  }

  /** Tag encoding for an outstanding translation: which context, and which half. */
  static std::uint64_t xlat_tag(std::size_t slot, bool code) { return (static_cast<std::uint64_t>(slot) << 1) | (code ? 1U : 0U); }

  /**
   * Resolve an address for this context, or start resolving it.
   *
   * Returns true when the physical address is in hand. Returns false having
   * blocked the context, which is the interesting case: a translation miss
   * costs the same kind of sleep a data miss does, and the two are counted
   * separately so the sweep can tell them apart.
   */
  bool resolve(std::size_t slot, std::uint64_t vaddr, bool code, std::uint64_t& physical)
  {
    honour_remaps(contexts_[slot]);
    auto& ctx = contexts_[slot];

    if (const auto* entry = context_lookup(ctx, vaddr, code); entry != nullptr) {
      (code ? ctx_code_hits_ : ctx_data_hits_)++;
      physical = entry->translate(vaddr);
      return true;
    }
    (code ? ctx_code_misses_ : ctx_data_misses_)++;

    if (mmu_ == nullptr) {
      physical = oracle_translate(ctx, champsim::address{vaddr}).to<std::uint64_t>();
      return true;
    }

    if (!mmu_->request_translation(xlat_tag(slot, code), ctx.origin, champsim::address{vaddr})) {
      ready_.push_back(slot); // the MMU is full; retry next cycle
      ++port_retries_;
      port_blocked_ = true;
      return false;
    }
    ctx.awaiting_translation = true;
    ctx.state = nmfc::ctx_state::BLOCKED;
    ++translation_stalls_;
    return false;
  }

  /** Wake whatever the MMU finished for us. */
  long drain_translations()
  {
    if (mmu_ == nullptr) {
      return 0;
    }
    long progress = 0;
    auto& done_list = mmu_->translation_completions();
    for (const auto& done : done_list) {
      const auto slot = static_cast<std::size_t>(done.tag >> 1);
      const bool code = (done.tag & 1U) != 0;
      if (slot >= contexts_.size()) {
        continue;
      }
      auto& ctx = contexts_[slot];
      if (ctx.state == nmfc::ctx_state::FREE) {
        continue; // the context left while its translation was in flight
      }
      context_fill(ctx, code, done);
      if (ctx.awaiting_translation) {
        ctx.awaiting_translation = false;
        make_ready(slot, current_time);
      }
      ++progress;
    }
    done_list.clear();
    return progress;
  }

  /**
   * Virtual to physical, for the access itself.
   *
   * Routing already happened on the virtual address, so this never changes
   * where the request goes -- congruence guarantees the frame is on this tile.
   * What it does is put the request into the same physical address space the
   * compute tiles' traffic arrives in, so one LLC slice serves both.
   *
   * The lookup is currently an oracle: correct addresses, no modeled latency
   * beyond a page fault. Charging for the walk is what NMFC_MMU adds, and the
   * per-context translation cache with it.
   */
  champsim::address oracle_translate(const nmfc::context& ctx, champsim::address vaddr)
  {
    if (vmem_ == nullptr) {
      return vaddr; // no paging configured: the virtual address is the physical one
    }
    auto [ppage, penalty] = vmem_->va_to_pa(ctx.origin, champsim::page_number{vaddr});
    (void)penalty;
    return champsim::address{champsim::splice(ppage, champsim::page_offset{vaddr})};
  }

  /**
   * Start another invocation from inside this one.
   *
   * The alternative to migrating. A context that needs an address on another
   * tile can carry itself there -- taking its registers, its scoreboard and a
   * cold restart with it -- or it can send a token and carry on. When the work
   * at the far end is short, sending the work is strictly cheaper, and the
   * fabric's placement policy puts it on the tile that owns the address rather
   * than the tile that happened to ask.
   *
   * It is also what takes the host off the critical path: an invocation that
   * discovers work creates it here instead of returning it to be re-dispatched,
   * so the in-flight count stops being bounded by what one compute tile can
   * issue between barriers.
   */
  bool issue_spawn(std::size_t slot, const nmfc::body_instr& instr)
  {
    auto& ctx = contexts_[slot];
    const auto* body = image_->lookup(instr.spawn_token);
    if (body == nullptr) {
      fmt::print("[{}] ERROR: token {} spawns {}, which was never defined in the trace\n", NAME, ctx.token, instr.spawn_token);
      std::exit(-1);
    }

    nmfc::invocation_msg msg{};
    msg.token = instr.spawn_token;
    msg.origin = ctx.origin;
    msg.home_host = ctx.home_host;
    msg.body = body;
    msg.entry_pc = body->entry_pc;
    if (!fabric_->dispatch(msg)) {
      ready_.push_back(slot); // the fabric is full; try again next cycle
      ++spawn_stalls_;
      return false;
    }

    ++spawned_;
    for (const auto reg : instr.dst_reg) {
      if (reg != 0) {
        ctx.ready[reg - 1] = true;
      }
    }
    ++ctx.pc;
    ++instructions_;
    make_ready(slot, current_time + latency_[idx(instr.cls)]);
    return true;
  }

  bool issue_memory(std::size_t slot, const nmfc::body_instr& instr)
  {
    auto& ctx = contexts_[slot];
    const auto ops = instr.num_mem_ops();

    // Every address this instruction touches must be local, or the context
    // belongs on another tile before it runs at all. *When* that can be decided
    // is the routing model: from the virtual address, before any translation,
    // or only from the physical address it translates to.
    const bool translate_first = router_->order() == nmfc::routing_order::TRANSLATE_FIRST;

    if (!translate_first) {
      for (std::size_t i = 0; i < ops; ++i) {
        if (const auto target = router_->owner_of(ctx.origin, instr.mem[i]); target != tile_) {
          ctx.last_route_address = instr.mem[i];
          return begin_migration(slot, target);
        }
      }
    }

    // Translate before anything is claimed. A translation miss blocks the
    // context, and a lock taken before that point would be stranded: the
    // context is not gone, so nothing releases it, and on retry it would find
    // its own lock and spin forever.
    std::array<std::uint64_t, nmfc::MAX_MEM_OPS> physical{};
    for (std::size_t i = 0; i < ops; ++i) {
      if (!resolve(slot, instr.mem[i].to<std::uint64_t>(), /*code=*/false, physical[i])) {
        return false;
      }
    }

    if (translate_first) {
      // The translation just done is the one the access needed anyway; routing
      // simply reads the answer out of it. A migration here discards it, which
      // is the real cost of this model and what the cold-start statistic counts.
      for (std::size_t i = 0; i < ops; ++i) {
        if (const auto target = map_.tile_of(champsim::address{physical[i]}); target != tile_) {
          ctx.last_route_address = instr.mem[i];
          return begin_migration(slot, target);
        }
      }
    }

    // Atomics serialize per address. Because every access to this range
    // converges on this one core, a local table is the whole mechanism.
    std::uint64_t lock_block = 0;
    bool forwarded = false;
    if (instr.is_atomic) {
      // Lock the operand, not the line it sits in. An atomic updates one word,
      // and two atomics on different words are independent even when they
      // share a line -- there is no coherence to preserve, because the block
      // lives on exactly one tile and is touched by exactly one core. Locking
      // the whole line instead made a line's worth of unrelated counters
      // contend for a critical section that spans a memory round trip, which
      // is what a graph kernel's parent array looks like.
      lock_block = instr.mem[0].to<std::uint64_t>();
      // A lock this context already holds is not a conflict with itself.
      const bool held_by_us = ctx.holds_lock && ctx.held_lock == lock_block;
      forwarded = ctx.has_forwarded_value && ctx.forwarded_lock == lock_block;
      if (!held_by_us && !forwarded && locked_blocks_.count(lock_block) != 0) {
        // Park rather than re-queue to retry. A retrying context still costs an
        // examination slot every cycle, and the issue loop examines at most as
        // many entries as were queued when the cycle began -- so a ready queue
        // filled with contexts spinning on one hot block spends the whole
        // examination budget on contexts that cannot issue and leaves issue
        // width unused. That makes contention superlinear rather than merely
        // serial: the more contexts converge on a block at once, the less work
        // the core does per cycle. Waking on release keeps the ready queue to
        // contexts that can actually make progress.
        park_on_lock(slot, ctx, lock_block);
        ++atomic_conflicts_;
        return false;
      }
    }

    if (forwarded) {
      // The unit holds the line and the value that came back with it, so this
      // update is an ALU operation on data already in hand. This is the whole
      // point of doing atomics at the memory: a queue of updates to one
      // address costs one fetch and then one ALU pass each, not a round trip
      // apiece.
      ctx.has_forwarded_value = false;
      ++atomics_;
      ++atomic_forwards_;
      for (const auto reg : instr.dst_reg) {
        if (reg != 0) {
          ctx.ready[reg - 1] = true;
        }
      }
      ++ctx.pc;
      const auto done_at = current_time + latency_[idx(nmfc::op_class::ALU)];
      releases_.push_back(pending_release{done_at, lock_block, slot, ctx.token});
      make_ready(slot, done_at);
      return true;
    }

    // All or nothing: a partially issued instruction would need per-operation
    // resume state that nothing else in this model requires.
    if (dcache_->rq_occupancy() + ops > dcache_->rq_size()) {
      ready_.push_back(slot);
      ++port_retries_;
      ++data_retries_;
      port_blocked_ = true;
      return false;
    }

    if (instr.is_atomic) {
      locked_blocks_.insert(lock_block);
      ctx.held_lock = lock_block;
      ctx.holds_lock = true;
      ++atomics_;
    }

    for (std::size_t i = 0; i < ops; ++i) {
      const bool is_store = i >= instr.num_loads;
      champsim::request req;
      req.v_address = instr.mem[i];
      req.address = champsim::address{physical[i]};
      req.is_translated = true;
      req.type = is_store ? access_type::WRITE : access_type::LOAD;
      req.response_requested = !is_store;
      req.origin = ctx.origin;
      req.ip = instr.ip;

      const bool sent = is_store ? dcache_->add_wq(req) : dcache_->add_rq(req);
      if (!sent) {
        // The capacity check above should make this unreachable; if a model
        // changes underneath us, fail loudly rather than lose a request.
        fmt::print("[{}] ERROR: data port refused a request after reporting room for it\n", NAME);
        std::exit(-1);
      }

      if (is_store) {
        ++stores_;
        continue; // fire and forget; no register waits on it
      }
      ++loads_;
      ++ctx.pending_mem;

      outstanding_op op{};
      op.slot = slot;
      op.token = ctx.token;
      op.dst = instr.dst_reg;
      op.holds_lock = instr.is_atomic;
      op.lock_block = lock_block;
      outstanding_[instr.mem[i].to<std::uint64_t>() >> block_bits_].push_back(op);

      // The destination is in flight. This is the sleep-on-request: the context
      // keeps going, and only stops when something actually needs this value.
      for (const auto reg : instr.dst_reg) {
        if (reg != 0) {
          ctx.ready[reg - 1] = false;
        }
      }
    }

    ++ctx.pc;
    ++instructions_;
    make_ready(slot, current_time + clock_period);
    return true;
  }

  bool issue_fetch(std::size_t slot, std::uint64_t eff_ip)
  {
    auto& ctx = contexts_[slot];

    if (icache_ == nullptr) {
      // No modeled instruction cache: charge a flat latency instead, so a
      // configuration can isolate the data path.
      make_ready(slot, current_time + fetch_latency_);
      return true;
    }
    if (icache_->rq_occupancy() + 1 > icache_->rq_size()) {
      return false;
    }

    std::uint64_t physical = 0;
    if (!resolve(slot, eff_ip, /*code=*/true, physical)) {
      return true; // blocked on translation; the fetch happens when it lands
    }
    champsim::request req;
    req.v_address = champsim::address{eff_ip};
    req.address = champsim::address{physical};
    req.is_translated = true;
    req.type = access_type::LOAD;
    req.response_requested = true;
    req.origin = ctx.origin;
    req.ip = req.v_address;
    if (!icache_->add_rq(req)) {
      return false;
    }

    outstanding_op op{};
    op.slot = slot;
    op.token = ctx.token;
    op.is_fetch = true;
    outstanding_[eff_ip >> block_bits_].push_back(op);

    ++ctx.pending_mem;
    ctx.state = nmfc::ctx_state::BLOCKED; // a context cannot run ahead of its own fetch
    return true;
  }

  bool begin_migration(std::size_t slot, std::size_t target)
  {
    auto& ctx = contexts_[slot];
    if (ctx.pending_mem > 0) {
      // Outstanding accesses belong to this tile; let them land first.
      ctx.state = nmfc::ctx_state::BLOCKED;
      return false;
    }
    // A migration is evidence, not only a cost: it says a context on this tile
    // needed an address that lives on another. A policy that can move pages
    // gets to act on that; one that cannot ignores it.
    //
    // Except the first. An invocation is dispatched to a tile chosen by a
    // placement policy that has not seen its data, so its opening migration
    // reports where dispatch put it and nothing else -- uniform noise, credited
    // as though it were locality. Measured on a workload of disjoint clusters,
    // where every grain has exactly one true consumer, including the first hop
    // pulled mean pull dominance down to 0.670 and classified two thirds of the
    // grains as shared.
    if (ctx.migrations > 0) {
      router_->note_migration(ctx.origin, ctx.last_route_address, tile_, target, ctx.token);
    }
    release_context_lock(ctx);
    ctx.prepare_for_migration();
    migrating_.push_back(std::pair{slot, target});
    return true;
  }

  // ---- outbound ----

  long push_completions()
  {
    long progress = 0;
    while (!done_.empty()) {
      const auto slot = done_.front();
      auto& ctx = contexts_[slot];

      if (!ctx.body->no_return()) {
        if (!fabric_->finish(nmfc::completion_msg{ctx.token, ctx.home_host, ctx.live_regs})) {
          break; // fabric full; hold order and retry
        }
      }
      if (nmfc::hooks::complete.active()) {
        nmfc::hooks::complete.emit(ctx.token, tile_, static_cast<std::uint64_t>((current_time - ctx.arrived) / clock_period));
      }
      image_->retire(ctx.token);
      done_.pop_front();
      release_slot(slot);
      ++completed_;
      ++progress;
    }
    return progress;
  }

  long push_migrations()
  {
    long progress = 0;
    // Do not stop at the front. The fabric queues per destination, so a refusal
    // says one target is congested, not that the fabric is. Stopping here would
    // let a context bound for a full tile pin every context behind it that has
    // somewhere to go -- and since each of those still holds its slot, this tile
    // would fill up and stop running anything, which is how the whole machine
    // seizes. This is the third of three stages that each needed the same fix:
    // this deque, the fabric's queues, and the destination's context array.
    for (auto it = std::begin(migrating_); it != std::end(migrating_);) {
      const auto [slot, target] = *it;
      auto& ctx = contexts_[slot];

      nmfc::migration_msg msg{};
      msg.ctx = ctx;
      msg.target_tile = target;
      if (!fabric_->migrate(std::move(msg))) {
        ++it;
        continue;
      }
      if (nmfc::hooks::migrate.active()) {
        nmfc::hooks::migrate.emit(ctx.token, tile_, target, ctx.migrations);
      }
      it = migrating_.erase(it);
      release_slot(slot);
      ++departed_;
      ++progress;
    }
    return progress;
  }

  // ---- state ----

  std::size_t tile_;
  nmfc::tile_map map_;
  nmfc::function_fabric_module* fabric_;
  nmfc::function_image_module* image_;
  nmfc::tile_router_module* router_;
  nmfc::page_placement_sink* placement_;
  channel_type* dcache_;
  channel_type* icache_;
  champsim::bandwidth::maximum_type issue_width_;
  champsim::chrono::clock::duration fetch_latency_;
  champsim::chrono::clock::duration fetch_bubble_;
  unsigned block_bits_;
  unsigned page_bits_;
  champsim::modules::vmem_module* vmem_;
  nmfc::translation_engine* mmu_ = nullptr;
  std::array<champsim::chrono::clock::duration, 8> latency_{};

  std::vector<nmfc::context> contexts_;
  std::vector<std::size_t> free_slots_;
  std::deque<std::size_t> ready_;
  std::multimap<champsim::chrono::clock::time_point, std::size_t> timers_;
  std::deque<std::size_t> done_;
  std::deque<std::pair<std::size_t, std::size_t>> migrating_;

  std::unordered_map<std::uint64_t, std::vector<outstanding_op>> outstanding_;
  std::unordered_set<std::uint64_t> locked_blocks_;

  // Contexts parked on a held block, in arrival order, each tagged with the
  // invocation that queued it so a reused slot is not mistaken for a waiter.
  struct lock_waiter {
    std::size_t slot;
    std::uint64_t token;
    champsim::chrono::clock::time_point parked_at;
  };
  std::unordered_map<std::uint64_t, std::deque<lock_waiter>> lock_waiters_;

  struct pending_release {
    champsim::chrono::clock::time_point at;
    std::uint64_t key;
    std::size_t slot;
    std::uint64_t token;
  };
  // Addresses held by a forwarded update, which has no memory response to
  // release them on.
  std::vector<pending_release> releases_;
  std::uint64_t atomic_forwards_ = 0;

  // Sampled residency breakdown; see sample_waits().
  std::uint64_t sample_tick_ = 0;
  std::uint64_t sample_period_ = 64;
  std::uint64_t wait_resident_ = 0;
  std::uint64_t wait_memory_ = 0;
  std::uint64_t wait_translation_ = 0;
  std::uint64_t wait_lock_ = 0;
  std::uint64_t wait_issue_ = 0;
  std::uint64_t wait_latency_ = 0;
  std::uint64_t fetch_retries_ = 0;
  std::uint64_t data_retries_ = 0;
  std::uint64_t wait_migration_ = 0;
  std::uint64_t wait_blocked_other_ = 0;
  std::uint64_t wait_running_ = 0;

  // Cycles contexts actually spent stopped on a contended block. The conflict
  // counter cannot answer this: while blocked contexts were re-queued it
  // counted one per retry per cycle, and now that they park it counts one per
  // wait. Neither is a duration, so neither can be compared across the two.
  std::uint64_t atomic_wait_cycles_ = 0;

  // Slots that arrived by migration and have not yet issued a real instruction.
  // The cycles between arrival and that first issue are what a dropped
  // translation and a cold fetch actually cost.
  std::vector<bool> cold_;

  std::uint64_t accepted_ = 0;
  std::uint64_t arrived_ = 0;
  std::uint64_t departed_ = 0;
  std::uint64_t completed_ = 0;
  std::uint64_t instructions_ = 0;
  std::uint64_t loads_ = 0;
  std::uint64_t stores_ = 0;
  std::uint64_t atomics_ = 0;
  std::uint64_t fetches_ = 0;
  std::uint64_t scoreboard_stalls_ = 0;
  std::uint64_t spawned_ = 0;
  std::uint64_t spawn_stalls_ = 0;
  std::uint64_t port_retries_ = 0;
  std::uint64_t port_busy_cycles_ = 0;
  bool port_blocked_ = false;
  std::uint64_t atomic_conflicts_ = 0;
  std::uint64_t translation_stalls_ = 0;
  std::uint64_t ctx_code_hits_ = 0;
  std::uint64_t ctx_code_misses_ = 0;
  std::uint64_t ctx_data_hits_ = 0;
  std::uint64_t ctx_data_misses_ = 0;
  std::uint64_t occupancy_time_ = 0;
  champsim::chrono::clock::time_point phase_start_{};
  champsim::chrono::clock::time_point last_sample_{};
  std::size_t peak_occupancy_ = 0;
  std::uint64_t residency_sum_ = 0;
  std::uint64_t residency_count_ = 0;
  std::uint64_t cold_start_cycles_ = 0;
  std::uint64_t cold_start_count_ = 0;
};

static nmfc::function_core_module::register_interface function_core_iface_reg("function_core", "function cores");
static nmfc::function_core_module::register_module<function_core> function_core_reg("FUNCTION_CORE");

} // anonymous namespace
