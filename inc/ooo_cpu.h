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

#ifndef OOO_CPU_H
#define OOO_CPU_H

#ifdef CHAMPSIM_MODULE
#define SET_ASIDE_CHAMPSIM_MODULE
#undef CHAMPSIM_MODULE
#endif

#include <array>
#include <bitset>
#include <cstdlib>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "bandwidth.h"
#include "champsim.h"
#include "channel.h"
#include "core_stats.h"
#include "instruction.h"
#include "instruction_producer.h"
#include "modules.h"
#include "msl/lru_table.h"
#include "operable.h"
#include "register_allocator.h"
#include "util/ring_buffer.h"
#include "util/to_underlying.h"

class CACHE;
class CacheBus
{
  using channel_type = champsim::modules::channel_module;
  using request_type = typename channel_type::request_type;
  using response_type = typename channel_type::response_type;

  channel_type* lower_level;

  friend class O3_CPU;

public:
  explicit CacheBus(champsim::modules::channel_module* ll) : lower_level(ll) {}
  bool issue_read(request_type packet);
  bool issue_write(request_type packet);
};

struct LSQ_ENTRY : champsim::program_ordered<LSQ_ENTRY> {
  champsim::address virtual_address{};
  champsim::address ip{};
  champsim::chrono::clock::time_point ready_time{champsim::chrono::clock::time_point::max()};

  champsim::origin origin{};
  bool fetch_issued = false;

  uint64_t producer_id = std::numeric_limits<uint64_t>::max();
  std::vector<std::reference_wrapper<std::optional<LSQ_ENTRY>>> lq_depend_on_me{};

  // Physical ROB slot of the owning instruction (captured at
  // do_memory_scheduling): the owner is still resident when finish() runs, so
  // it indexes in O(1) instead of an instr_id partition_point. Keyed on the
  // physical slot, so gapped multi-core ids are irrelevant.
  std::size_t rob_slot{};

  LSQ_ENTRY(champsim::address addr, champsim::program_ordered<LSQ_ENTRY>::id_type id, champsim::address ip, champsim::origin origin);
  void finish(ooo_model_instr& rob_entry) const;
};

// cpu
class O3_CPU : public champsim::modules::core_module
{
public:
  // This core's consumer id (its "CPU number"): hardware-context identity
  // for provenance stamping, tables, stats, and phase tracking.

  // cycle
  champsim::chrono::clock::time_point begin_phase_time{};
  long long begin_phase_instr = 0;
  champsim::chrono::clock::time_point finish_phase_time{};
  long long finish_phase_instr = 0;

  // instruction
  long long num_retired = 0;

  using stats_type = cpu_stats;

  stats_type roi_stats{}, sim_stats{};

  // instruction buffer
  struct dib_shift {
    champsim::data::bits shamt;
    // Integer projection: shifted-value equality matches the DIB's old
    // upper-slice equality, without building a slice per lookup.
    auto operator()(champsim::address val) const { return val.to<uint64_t>() >> champsim::to_underlying(shamt); }
  };
  using dib_type = champsim::msl::lru_table<champsim::address, dib_shift, dib_shift>;
  dib_type DIB;

  // reorder buffer, load/store queue, register file
  // Frontend stage buffers: admission gated at the configured sizes, erasure
  // front-anchored, so fixed-capacity rings replace the deques' chunk churn.
  champsim::ring_buffer<ooo_model_instr> DISPATCH_BUFFER;
  champsim::ring_buffer<ooo_model_instr> DECODE_BUFFER;
  champsim::ring_buffer<ooo_model_instr> DIB_HIT_BUFFER;

private:
  // ROB, LQ, and IFETCH_BUFFER carry incrementally maintained bookkeeping;
  // private so outside mutation can't desync it. Inspect via rob()/lq()/
  // ifetch_buffer(); mutate via the modify_* counterparts, which re-derive it.
  // The ROB is a contiguous fixed-capacity FIFO walked every operated cycle.
  champsim::ring_buffer<ooo_model_instr> ROB;
  std::vector<std::optional<LSQ_ENTRY>> LQ;
  champsim::ring_buffer<ooo_model_instr> IFETCH_BUFFER;

  // SQ ring buffer: dispatch admits at the back, completion erases at the
  // front. sq_store_index_ holds pointers into it; ring elements never move
  // for their residency (never enlarged), so those pointers stay stable.
  champsim::ring_buffer<LSQ_ENTRY> SQ;

public:
  // Inspection
  const champsim::ring_buffer<ooo_model_instr>& rob() const { return ROB; }
  const std::vector<std::optional<LSQ_ENTRY>>& lq() const { return LQ; }
  const champsim::ring_buffer<LSQ_ENTRY>& sq() const { return SQ; }
  const champsim::ring_buffer<ooo_model_instr>& ifetch_buffer() const { return IFETCH_BUFFER; }

  // Apply an arbitrary mutation, then re-derive all dependent bookkeeping.
  // The sanctioned way to mutate these structures from outside (tests).
  template <typename F>
  void modify_rob(F&& fn)
  {
    std::forward<F>(fn)(ROB);
    resync_rob_bookkeeping();
  }
  template <typename F>
  void modify_lq(F&& fn)
  {
    std::forward<F>(fn)(LQ);
    resync_lq_bookkeeping();
  }
  template <typename F>
  void modify_sq(F&& fn)
  {
    std::forward<F>(fn)(SQ);
    resync_sq_bookkeeping();
  }
  template <typename F>
  void modify_ifetch_buffer(F&& fn)
  {
    std::forward<F>(fn)(IFETCH_BUFFER);
    resync_ifetch_bookkeeping();
  }

  // Constants
  const std::size_t IFETCH_BUFFER_SIZE, DISPATCH_BUFFER_SIZE, DECODE_BUFFER_SIZE, REGISTER_FILE_SIZE, ROB_SIZE, SQ_SIZE, DIB_HIT_BUFFER_SIZE;
  champsim::bandwidth::maximum_type FETCH_WIDTH, DECODE_WIDTH, DISPATCH_WIDTH, SCHEDULER_SIZE, EXEC_WIDTH, DIB_INORDER_WIDTH;
  champsim::bandwidth::maximum_type LQ_WIDTH, SQ_WIDTH;
  champsim::bandwidth::maximum_type RETIRE_WIDTH;
  champsim::chrono::clock::duration BRANCH_MISPREDICT_PENALTY;
  champsim::chrono::clock::duration DISPATCH_LATENCY;
  champsim::chrono::clock::duration DECODE_LATENCY;
  champsim::chrono::clock::duration SCHEDULING_LATENCY;
  champsim::chrono::clock::duration EXEC_LATENCY;
  champsim::chrono::clock::duration DIB_HIT_LATENCY;

  champsim::bandwidth::maximum_type L1I_BANDWIDTH, L1D_BANDWIDTH;

  RegisterAllocator reg_allocator{REGISTER_FILE_SIZE};

  // branch
  champsim::chrono::clock::time_point fetch_resume_time{};

  const long IN_QUEUE_SIZE;
  std::deque<ooo_model_instr> input_queue;

  CacheBus L1I_bus, L1D_bus;
  champsim::modules::cache_module* l1i;

  void initialize() final;
  long operate() final;
  void begin_phase(bool warmup, bool roi) override;
  void end_phase() override;

  // module_lifecycle stats
  void report_stats(bool roi, champsim::stat_report& out) const override;

private:
  bool warmup_ = true;
  [[maybe_unused]] bool roi_ = false;
  // Free LQ slot count, maintained at the four LQ mutation sites so dispatch
  // need not re-count the optionals per iteration.
  long lq_free_slots_ = 0;
  // Occupied-slot bitmap for the LQ, maintained at the same four sites;
  // set bits ascending visit the occupied slots in index order.
  std::vector<uint64_t> lq_occupied_;
  // Issue candidates: occupied slots with no producer, fetch not yet issued.
  // Stays proportional to actionable entries, unlike the occupancy bitmap.
  std::vector<uint64_t> lq_unissued_;
  // Unissued candidates whose ready_time has passed — the exact set
  // operate_lsq acts on. Promoted from lq_pending_ready_ as the clock passes
  // their ready_time (ready_times monotone, so promotion is a FIFO pop).
  std::vector<uint64_t> lq_ready_;
  std::deque<std::pair<champsim::chrono::clock::time_point, uint32_t>> lq_pending_ready_;
  // Return-matching index: fetch-issued loads keyed by block address (what an
  // L1D return carries). Maintained at two transitions — inserted when
  // operate_lsq marks a load fetch_issued, erased when a matching return
  // finishes it — so handle_memory_return does a lookup, not an LQ sweep.
  std::unordered_map<uint64_t, std::vector<uint32_t>> hmr_block_index_;

  // Allocation-free fixed-capacity vector for the per-instruction memory
  // handles below (<=4 loads, <=4 stores per instruction).
  template <typename T, std::size_t N>
  struct bounded_array {
    std::array<T, N> data_{};
    std::size_t count_ = 0;
    void clear() { count_ = 0; }
    void push_back(const T& value)
    {
      assert(count_ < N);
      data_[count_++] = value;
    }
    const T* begin() const { return data_.data(); }
    const T* end() const { return data_.data() + count_; }
    std::size_t size() const { return count_; }
  };
  // A dispatched instruction's own memory entries: the LQ slots it holds (live,
  // non-immediately-forwarded loads only) and pointers to its SQ entries.
  struct rob_mem_handle {
    bounded_array<uint16_t, 4> lq_slots{};
    bounded_array<LSQ_ENTRY*, 4> sq_entries{};
  };
  // Per-ROB-slot memory handles, keyed by stable physical slot. Recorded at
  // do_memory_scheduling, read at do_execution, so ready-time stamping visits
  // <=4 handles instead of scanning LQ and SQ. Overwritten on each dispatch
  // into a slot; rebuilt from LQ/SQ/ROB in the resync_* paths.
  std::vector<rob_mem_handle> rob_mem_handles_;

  // Frontend scan positions over IFETCH_BUFFER. dib_checked entries form a
  // strict prefix, so ifetch_dib_checked_ is its exact length.
  // ifetch_fetch_scan_ is a monotone lower bound on the first fetch-ready
  // entry, advanced lazily so each position is walked O(1) times amortized.
  long ifetch_dib_checked_ = 0;
  long ifetch_fetch_scan_ = 0;

  // Length of the scheduled prefix of the ROB. Scheduled entries always form
  // a strict prefix (dispatch appends unscheduled, schedule fills in order,
  // retire pops completed).
  long num_scheduled_ = 0;
  // Scheduler-window occupancy: scheduled-but-unexecuted instructions hold
  // slots. Pre-charges the SCHEDULER_SIZE budget so rename skips them.
  long scheduler_occupancy_ = 0;

  // Stage candidate sets, as bitmaps over the ROB's stable physical slots:
  // exec candidates are scheduled-and-unexecuted; mem-complete candidates are
  // executed-and-uncompleted with all memory ops finished (completed_mem_ops
  // == num_mem_ops). Bits set/cleared at the state transitions, so the walks
  // visit only actionable instructions. Iterated in age order (from head,
  // wrapping).
  std::vector<uint64_t> exec_candidates_;
  std::vector<uint64_t> mem_complete_candidates_;

  static void candidate_set(std::vector<uint64_t>& bits, std::size_t slot) { bits[slot >> 6] |= (uint64_t{1} << (slot & 63)); }
  static void candidate_clear(std::vector<uint64_t>& bits, std::size_t slot) { bits[slot >> 6] &= ~(uint64_t{1} << (slot & 63)); }

  // Block-address key for the return index: same shift handle_memory_return
  // applies, so issue-site insert and return-site lookup land on one key.
  static uint64_t hmr_block_key(champsim::address addr) { return addr.to<uint64_t>() >> champsim::to_underlying(champsim::block_number_extent{}.lower); }

  // Visit the set bits of `bits` within physical slots [from, to), ascending.
  // fn(slot) returns false to stop; returns false if stopped early.
  template <typename F>
  static bool visit_candidate_range(const std::vector<uint64_t>& bits, std::size_t from, std::size_t to, F& fn)
  {
    if (from >= to) {
      return true;
    }
    const std::size_t first_word = from >> 6;
    const std::size_t last_word = (to - 1) >> 6;
    for (std::size_t word = first_word; word <= last_word; ++word) {
      uint64_t bits_word = bits[word];
      if (word == first_word) {
        bits_word &= (~uint64_t{0}) << (from & 63);
      }
      if (word == last_word && ((to & 63) != 0)) {
        bits_word &= (uint64_t{1} << (to & 63)) - 1;
      }
      for (; bits_word != 0; bits_word &= bits_word - 1) {
        if (!fn(word * 64 + static_cast<std::size_t>(__builtin_ctzll(bits_word)))) {
          return false;
        }
      }
    }
    return true;
  }

  // Visit candidates in age order: physical slots wrap, so age order is
  // [head, capacity) followed by [0, head).
  template <typename F>
  bool visit_candidates_in_age_order(const std::vector<uint64_t>& bits, F&& fn)
  {
    const std::size_t head = ROB.head_slot();
    return visit_candidate_range(bits, head, ROB.capacity(), fn) && visit_candidate_range(bits, 0, head, fn);
  }

  // Stage change-detectors: an execute/complete walk that issues nothing is
  // side-effect-free; its outcome changes only via the events that dirty
  // these flags or time reaching the recorded wake point.
  bool exec_stage_clean_ = false;
  champsim::chrono::clock::time_point exec_stage_wake_{};
  bool complete_stage_clean_ = false;
  champsim::chrono::clock::time_point complete_stage_wake_{};

  void lq_set_occupied(std::size_t idx)
  {
    lq_occupied_[idx >> 6] |= (uint64_t{1} << (idx & 63));
    lq_unissued_[idx >> 6] |= (uint64_t{1} << (idx & 63)); // fresh entries have no producer and are unissued
    --lq_free_slots_;
  }
  void lq_clear_occupied(std::size_t idx)
  {
    lq_occupied_[idx >> 6] &= ~(uint64_t{1} << (idx & 63));
    lq_unissued_[idx >> 6] &= ~(uint64_t{1} << (idx & 63));
    lq_ready_[idx >> 6] &= ~(uint64_t{1} << (idx & 63));
    ++lq_free_slots_;
  }
  void lq_clear_unissued(std::size_t idx)
  {
    lq_unissued_[idx >> 6] &= ~(uint64_t{1} << (idx & 63));
    lq_ready_[idx >> 6] &= ~(uint64_t{1} << (idx & 63));
  }

  // Store-forwarding index: per (exact virtual address) stack of SQ entries,
  // oldest first, youngest at the back. The SQ grows at the back (dispatch,
  // program order) and shrinks at the front (completion, oldest first), so
  // each stack stays age-ordered and the back is the youngest store to that
  // address — what the forwarding scan previously searched for.
  std::unordered_map<uint64_t, std::vector<LSQ_ENTRY*>> sq_store_index_;

  // Rebuild the ROB-slot-keyed memory handles from the live LQ, SQ, and ROB.
  // Owners found by instr_id (ROB is instr_id sorted); slots visited ascending
  // / stores front-to-back to match normal recording order. Called from every
  // resync_* path that can move these entries.
  void rebuild_rob_mem_handles()
  {
    rob_mem_handles_.assign(ROB.capacity(), rob_mem_handle{});
    for (std::size_t idx = 0; idx < std::size(LQ); ++idx) {
      if (LQ[idx].has_value()) {
        auto owner = std::partition_point(std::begin(ROB), std::end(ROB), ooo_model_instr::precedes(LQ[idx]->instr_id));
        if (owner != std::end(ROB) && owner->instr_id == LQ[idx]->instr_id) {
          LQ[idx]->rob_slot = owner.slot(); // re-derive the finish() slot for injected state
          rob_mem_handles_[owner.slot()].lq_slots.push_back(static_cast<uint16_t>(idx));
        }
      }
    }
    for (auto& sq_entry : SQ) {
      auto owner = std::partition_point(std::begin(ROB), std::end(ROB), ooo_model_instr::precedes(sq_entry.instr_id));
      if (owner != std::end(ROB) && owner->instr_id == sq_entry.instr_id) {
        sq_entry.rob_slot = owner.slot(); // re-derive the finish() slot for injected state
        rob_mem_handles_[owner.slot()].sq_entries.push_back(&sq_entry);
      }
    }
  }

  void resync_sq_bookkeeping()
  {
    sq_store_index_.clear();
    for (auto& sq_entry : SQ) {
      sq_store_index_[sq_entry.virtual_address.to<uint64_t>()].push_back(&sq_entry);
    }
    rebuild_rob_mem_handles();
  }

  void resync_ifetch_bookkeeping()
  {
    ifetch_dib_checked_ = 0;
    for (const auto& entry : IFETCH_BUFFER) {
      if (!entry.dib_checked) {
        break;
      }
      ++ifetch_dib_checked_;
    }
    ifetch_fetch_scan_ = 0; // always-valid lower bound; the scan re-advances
  }

  // Recompute all derived state from the underlying structures (used by the
  // modify_* mutation API).
  void resync_rob_bookkeeping()
  {
    num_scheduled_ = 0;
    for (const auto& entry : ROB) {
      if (!entry.scheduled) {
        break;
      }
      ++num_scheduled_;
    }
    scheduler_occupancy_ = std::count_if(std::cbegin(ROB), std::cend(ROB), [](const auto& entry) { return entry.scheduled && !entry.executed; });
    exec_candidates_.assign((ROB.capacity() + 63) / 64, 0);
    mem_complete_candidates_.assign((ROB.capacity() + 63) / 64, 0);
    for (std::size_t idx = 0; idx < std::size(ROB); ++idx) {
      const auto& entry = ROB[idx];
      if (entry.scheduled && !entry.executed) {
        candidate_set(exec_candidates_, ROB.slot_index(idx));
      }
      if (entry.executed && !entry.completed && entry.completed_mem_ops == entry.num_mem_ops()) {
        candidate_set(mem_complete_candidates_, ROB.slot_index(idx));
      }
    }
    exec_stage_clean_ = false;
    complete_stage_clean_ = false;
    rebuild_rob_mem_handles();
  }
  void resync_lq_bookkeeping()
  {
    lq_occupied_.assign((std::size(LQ) + 63) / 64, 0);
    lq_unissued_.assign((std::size(LQ) + 63) / 64, 0);
    lq_ready_.assign((std::size(LQ) + 63) / 64, 0);
    lq_pending_ready_.clear();
    hmr_block_index_.clear();
    lq_free_slots_ = static_cast<long>(std::size(LQ));
    for (std::size_t idx = 0; idx < std::size(LQ); ++idx) {
      if (LQ[idx].has_value()) {
        lq_set_occupied(idx);
        if (LQ[idx]->fetch_issued) {
          hmr_block_index_[hmr_block_key(LQ[idx]->virtual_address)].push_back(static_cast<uint32_t>(idx));
        }
        if (!(LQ[idx]->producer_id == std::numeric_limits<uint64_t>::max() && !LQ[idx]->fetch_issued)) {
          lq_clear_unissued(idx);
        } else if (LQ[idx]->ready_time < current_time) {
          lq_ready_[idx >> 6] |= (uint64_t{1} << (idx & 63));
        } else {
          lq_pending_ready_.emplace_back(LQ[idx]->ready_time, static_cast<uint32_t>(idx));
        }
      }
    }
    // promotion pops from the front in time order
    std::sort(std::begin(lq_pending_ready_), std::end(lq_pending_ready_));
    rebuild_rob_mem_handles();
  }

public:
  bool is_warmup() const { return warmup_; }
  bool is_roi() const { return roi_; }

  void push_instruction(ooo_model_instr instr) final;
  std::size_t instructions_requested() final;
  void initialize_instruction();
  long check_dib();
  long fetch_instruction();
  long promote_to_decode();
  long decode_instruction();
  long dispatch_instruction();
  long schedule_instruction();
  long execute_instruction();
  long operate_lsq();
  long complete_inflight_instruction();
  long handle_memory_return();
  long retire_rob();

  bool do_init_instruction(ooo_model_instr& instr);
  bool do_predict_branch(ooo_model_instr& instr);
  void do_check_dib(ooo_model_instr& instr);
  bool do_fetch_instruction(champsim::ring_buffer<ooo_model_instr>::iterator begin, champsim::ring_buffer<ooo_model_instr>::iterator end);
  void do_dib_update(const ooo_model_instr& instr);
  void do_scheduling(ooo_model_instr& instr);
  void do_execution(ooo_model_instr& instr, std::size_t rob_slot);
  void do_memory_scheduling(ooo_model_instr& instr);
  void do_complete_execution(ooo_model_instr& instr);
  void do_sq_forward_to_lq(LSQ_ENTRY& sq_entry, LSQ_ENTRY& lq_entry);

  void do_finish_store(const LSQ_ENTRY& sq_entry);
  bool do_complete_store(const LSQ_ENTRY& sq_entry);
  bool execute_load(const LSQ_ENTRY& lq_entry);

  [[nodiscard]] auto roi_instr() const { return roi_stats.instrs(); }
  [[nodiscard]] auto roi_cycle() const { return roi_stats.cycles(); }
  [[nodiscard]] uint64_t sim_instr() const final { return num_retired - begin_phase_instr; }
  [[nodiscard]] uint64_t sim_cycle() const final { return (current_time.time_since_epoch() / clock_period) - sim_stats.begin_cycles; }
  stats_type get_sim_stats() const final { return sim_stats; }
  stats_type get_roi_stats() const final { return roi_stats; }

  bool show_heartbeat = true;
  void quiet(bool enable) final { show_heartbeat = !enable; }

  void print_deadlock() final;

  std::vector<champsim::modules::branch_predictor*> branch_module_pimpl;
  std::vector<champsim::modules::btb*> btb_module_pimpl;
  std::vector<champsim::modules::instruction_producer*> instruction_producer_pimpl;

  void fill_from_producers();
  bool producers_eof() const final;

  // NOLINTBEGIN(readability-make-member-function-const): legacy modules use non-const hooks
  void impl_initialize_branch_predictor() const;
  void impl_last_branch_result(champsim::address ip, champsim::address target, bool taken, uint8_t branch_type) const;
  [[nodiscard]] bool impl_predict_branch(champsim::address ip, champsim::address predicted_target, bool always_taken, uint8_t branch_type) const;

  void impl_initialize_btb() const;
  void impl_update_btb(champsim::address ip, champsim::address predicted_target, bool taken, uint8_t branch_type) const;
  [[nodiscard]] std::pair<champsim::address, bool> impl_btb_prediction(champsim::address ip, uint8_t branch_type) const;
  // NOLINTEND(readability-make-member-function-const)

  virtual ~O3_CPU() noexcept = default;

  explicit O3_CPU(champsim::modules::ModuleBuilder builder)
      : core_module(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        DIB(builder.get_parameter<uint32_t>("dib_set"), builder.get_parameter<uint32_t>("dib_way"),
            {champsim::data::bits{champsim::lg2(builder.get_parameter<std::size_t>("dib_window"))}},
            {champsim::data::bits{champsim::lg2(builder.get_parameter<std::size_t>("dib_window"))}}),
        LQ(builder.get_parameter<uint32_t>("lq_size")), IFETCH_BUFFER_SIZE(builder.get_parameter<uint32_t>("ifetch_buffer_size")),
        DISPATCH_BUFFER_SIZE(builder.get_parameter<uint32_t>("dispatch_buffer_size")),
        DECODE_BUFFER_SIZE(builder.get_parameter<uint32_t>("decode_buffer_size")), REGISTER_FILE_SIZE(builder.get_parameter<uint32_t>("register_file_size")),
        ROB_SIZE(builder.get_parameter<uint32_t>("rob_size")), SQ_SIZE(builder.get_parameter<uint32_t>("sq_size")),
        DIB_HIT_BUFFER_SIZE(builder.get_parameter<uint32_t>("dib_hit_buffer_size")),
        FETCH_WIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("fetch_width")),
        DECODE_WIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("decode_width")),
        DISPATCH_WIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("dispatch_width")),
        SCHEDULER_SIZE(builder.get_parameter<champsim::bandwidth::maximum_type>("schedule_width")),
        EXEC_WIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("execute_width")),
        DIB_INORDER_WIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("dib_inorder_width")),
        LQ_WIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("lq_width")),
        SQ_WIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("sq_width")),
        RETIRE_WIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("retire_width")),
        BRANCH_MISPREDICT_PENALTY(builder.get_parameter<unsigned>("mispredict_penalty") * builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        DISPATCH_LATENCY(builder.get_parameter<unsigned>("dispatch_latency") * builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        DECODE_LATENCY(builder.get_parameter<unsigned>("decode_latency") * builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        SCHEDULING_LATENCY(builder.get_parameter<unsigned>("schedule_latency") * builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        EXEC_LATENCY(builder.get_parameter<unsigned>("execute_latency") * builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        DIB_HIT_LATENCY(builder.get_parameter<unsigned>("dib_hit_latency") * builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        L1I_BANDWIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("l1i_bandwidth")),
        L1D_BANDWIDTH(builder.get_parameter<champsim::bandwidth::maximum_type>("l1d_bandwidth")),
        IN_QUEUE_SIZE(2 * champsim::to_underlying(builder.get_parameter<champsim::bandwidth::maximum_type>("fetch_width"))),
        L1I_bus(builder.get_parameter<champsim::modules::channel_module*>("fetch_queues")),
        L1D_bus(builder.get_parameter<champsim::modules::channel_module*>("data_queues")), l1i(builder.get_parameter<champsim::modules::cache_module*>("l1i"))
  {
    // Construct branch predictor submodules
    for (const auto& sub : builder.get_submodules("branch_predictor"))
      branch_module_pimpl.push_back(champsim::modules::branch_predictor::create_instance(sub, static_cast<champsim::modules::core_module*>(this)));

    // Construct BTB submodules
    for (const auto& sub : builder.get_submodules("btb"))
      btb_module_pimpl.push_back(champsim::modules::btb::create_instance(sub, static_cast<champsim::modules::core_module*>(this)));

    ROB.set_capacity(ROB_SIZE);
    IFETCH_BUFFER.set_capacity(IFETCH_BUFFER_SIZE);
    DISPATCH_BUFFER.set_capacity(DISPATCH_BUFFER_SIZE);
    DECODE_BUFFER.set_capacity(DECODE_BUFFER_SIZE);
    DIB_HIT_BUFFER.set_capacity(DIB_HIT_BUFFER_SIZE);
    SQ.set_capacity(SQ_SIZE);
    exec_candidates_.assign((ROB.capacity() + 63) / 64, 0);
    mem_complete_candidates_.assign((ROB.capacity() + 63) / 64, 0);
    rob_mem_handles_.assign(ROB.capacity(), rob_mem_handle{});
    lq_free_slots_ = static_cast<long>(std::size(LQ));
    lq_occupied_.assign((std::size(LQ) + 63) / 64, 0);
    lq_unissued_.assign((std::size(LQ) + 63) / 64, 0);
    lq_ready_.assign((std::size(LQ) + 63) / 64, 0);

    // The core consumes instruction packets, so it attaches instruction_producer
    // children directly — the interface it is designed for.
    for (const auto& sub : builder.get_submodules("instruction_producer")) {
      instruction_producer_pimpl.push_back(
          champsim::modules::instruction_producer::create_instance(sub, static_cast<champsim::modules::packet_consumer*>(this)));
    }
  }
};

#ifdef SET_ASIDE_CHAMPSIM_MODULE
#undef SET_ASIDE_CHAMPSIM_MODULE
#define CHAMPSIM_MODULE
#endif

#endif
