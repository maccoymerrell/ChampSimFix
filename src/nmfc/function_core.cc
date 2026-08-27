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
#include <unordered_set>
#include <vector>
#include <fmt/core.h>

#include "bandwidth.h"
#include "channel.h"
#include "modules.h"
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
        image_(builder.get_parameter<nmfc::function_image_module*>("image")), dcache_(builder.get_parameter<channel_type*>("dcache")),
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
    ctx.code_bias = bias_for(*msg.body, tile_);
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
    ctx.code_bias = bias_for(*incoming.body, tile_);
    ctx.arrived = current_time;
    // Arriving cold: no fetched block, no translations. The cycles spent
    // re-establishing them are what the cold-start statistic counts.
    cold_[slot] = true;
    make_ready(slot, current_time);
    ++arrived_;
    return true;
  }

  // ---- the cycle ----

  long operate() final
  {
    long progress = 0;
    progress += drain_returns();
    progress += drain_translations();
    progress += wake_timers();
    progress += issue_cycle();
    progress += push_completions();
    progress += push_migrations();
    occupancy_sum_ += static_cast<std::uint64_t>(contexts_.size() - free_slots_.size());
    ++cycles_;
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
    instructions_ = loads_ = stores_ = atomics_ = fetches_ = 0;
    scoreboard_stalls_ = port_stalls_ = atomic_conflicts_ = translation_stalls_ = 0;
    ctx_code_hits_ = ctx_code_misses_ = ctx_data_hits_ = ctx_data_misses_ = 0;
    occupancy_sum_ = 0;
    cycles_ = 0;
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
    const auto occupancy = cycles_ == 0 ? 0.0 : static_cast<double>(occupancy_sum_) / static_cast<double>(cycles_);
    const auto residency = residency_count_ == 0 ? 0.0 : static_cast<double>(residency_sum_) / static_cast<double>(residency_count_);

    out.line(fmt::format("{} TILE {} INVOCATIONS: {} MIGRATED IN: {} OUT: {} COMPLETED: {}", NAME, tile_, accepted_, arrived_, departed_, completed_));
    out.line(fmt::format("{} INSTRUCTIONS: {} LOADS: {} STORES: {} ATOMICS: {} FETCHES: {}", NAME, instructions_, loads_, stores_, atomics_, fetches_));
    out.line(fmt::format("{} CONTEXT OCCUPANCY mean: {:.2f} peak: {} of {}", NAME, occupancy, peak_occupancy_, contexts_.size()));
    out.line(fmt::format("{} MEAN RESIDENCY: {:.1f} cycles STALLS scoreboard: {} port: {} atomic: {}", NAME, residency, scoreboard_stalls_, port_stalls_,
                         atomic_conflicts_));
    const auto cold_start = cold_start_count_ == 0 ? 0.0 : static_cast<double>(cold_start_cycles_) / static_cast<double>(cold_start_count_);
    out.line(fmt::format("{} MIGRATION COLD START: {} cycles over {} arrivals (mean {:.1f})", NAME, cold_start_cycles_, cold_start_count_, cold_start));

    auto json = out.json();
    json.add("tile", tile_);
    json.add("invocations", accepted_);
    json.add("migrated_in", arrived_);
    json.add("migrated_out", departed_);
    json.add("completed", completed_);
    json.add("instructions", instructions_);
    json.add("loads", loads_);
    json.add("stores", stores_);
    json.add("atomics", atomics_);
    json.add("fetches", fetches_);
    json.add("mean_context_occupancy", occupancy);
    json.add("peak_context_occupancy", peak_occupancy_);
    json.add("num_contexts", contexts_.size());
    json.add("mean_residency_cycles", residency);
    json.add("scoreboard_stalls", scoreboard_stalls_);
    json.add("port_stalls", port_stalls_);
    json.add("atomic_conflicts", atomic_conflicts_);
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

  /**
   * The bias that puts this body's code on `tile`.
   *
   * The copies sit on consecutive grains, so the offset is however many grains
   * separate the body's own tile from the one we want to run on.
   */
  [[nodiscard]] std::uint64_t bias_for(const nmfc::function_body& body, std::size_t tile) const
  {
    const auto base_tile = map_.tile_of_virtual(body.entry_pc_base);
    const auto n = map_.num_tiles();
    return static_cast<std::uint64_t>((tile + n - base_tile) % n) * map_.grain();
  }

  std::size_t take_slot()
  {
    const auto slot = free_slots_.back();
    free_slots_.pop_back();
    peak_occupancy_ = std::max(peak_occupancy_, contexts_.size() - free_slots_.size());
    return slot;
  }

  void release_lock(std::uint64_t block)
  {
    locked_blocks_.erase(block);
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
    const auto eff_ip = instr.ip.to<std::uint64_t>() + ctx.code_bias;

    // A function larger than one grain spills onto the next tile's copy, so the
    // instruction stream itself can force a migration.
    if (const auto ip_tile = map_.tile_of_virtual(eff_ip); ip_tile != tile_) {
      return begin_migration(slot, ip_tile);
    }

    // Instruction fetch, once per block. A tight loop pays this once.
    if (const auto block = eff_ip >> block_bits_; !ctx.has_fetched || ctx.fetched_block != block) {
      if (!issue_fetch(slot, eff_ip)) {
        ready_.push_back(slot); // port busy; try again next cycle
        ++port_stalls_;
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
      ++port_stalls_;
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

  bool issue_memory(std::size_t slot, const nmfc::body_instr& instr)
  {
    auto& ctx = contexts_[slot];
    const auto ops = instr.num_mem_ops();

    // Every address this instruction touches must be local, or the context
    // belongs on another tile before it runs at all.
    for (std::size_t i = 0; i < ops; ++i) {
      if (const auto target = map_.tile_of_virtual(instr.mem[i]); target != tile_) {
        return begin_migration(slot, target);
      }
    }

    // Translate first, before anything is claimed. A translation miss blocks
    // the context, and a lock taken before that point would be stranded: the
    // context is not gone, so nothing releases it, and on retry it would find
    // its own lock and spin forever.
    std::array<std::uint64_t, nmfc::MAX_MEM_OPS> physical{};
    for (std::size_t i = 0; i < ops; ++i) {
      if (!resolve(slot, instr.mem[i].to<std::uint64_t>(), /*code=*/false, physical[i])) {
        return false;
      }
    }

    // Atomics serialize per block. Because every access to this address range
    // converges on this one core, a local table is the whole mechanism.
    std::uint64_t lock_block = 0;
    if (instr.is_atomic) {
      lock_block = instr.mem[0].to<std::uint64_t>() >> block_bits_;
      // A lock this context already holds is not a conflict with itself.
      const bool held_by_us = ctx.holds_lock && ctx.held_lock == lock_block;
      if (!held_by_us && locked_blocks_.count(lock_block) != 0) {
        ready_.push_back(slot);
        ++atomic_conflicts_;
        return false;
      }
    }

    // All or nothing: a partially issued instruction would need per-operation
    // resume state that nothing else in this model requires.
    if (dcache_->rq_occupancy() + ops > dcache_->rq_size()) {
      ready_.push_back(slot);
      ++port_stalls_;
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
      req.ip = champsim::address{instr.ip.to<std::uint64_t>() + ctx.code_bias};

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
    release_context_lock(ctx);
    ctx.prepare_for_migration();
    ctx.code_bias = bias_for(*ctx.body, target);
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
    while (!migrating_.empty()) {
      const auto [slot, target] = migrating_.front();
      auto& ctx = contexts_[slot];

      nmfc::migration_msg msg{};
      msg.ctx = ctx;
      msg.target_tile = target;
      if (!fabric_->migrate(std::move(msg))) {
        break;
      }
      if (nmfc::hooks::migrate.active()) {
        nmfc::hooks::migrate.emit(ctx.token, tile_, target, ctx.migrations);
      }
      migrating_.pop_front();
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
  std::uint64_t port_stalls_ = 0;
  std::uint64_t atomic_conflicts_ = 0;
  std::uint64_t translation_stalls_ = 0;
  std::uint64_t ctx_code_hits_ = 0;
  std::uint64_t ctx_code_misses_ = 0;
  std::uint64_t ctx_data_hits_ = 0;
  std::uint64_t ctx_data_misses_ = 0;
  std::uint64_t occupancy_sum_ = 0;
  std::uint64_t cycles_ = 0;
  std::size_t peak_occupancy_ = 0;
  std::uint64_t residency_sum_ = 0;
  std::uint64_t residency_count_ = 0;
  std::uint64_t cold_start_cycles_ = 0;
  std::uint64_t cold_start_count_ = 0;
};

static nmfc::function_core_module::register_interface function_core_iface_reg("function_core", "function cores");
static nmfc::function_core_module::register_module<function_core> function_core_reg("FUNCTION_CORE");

} // anonymous namespace
