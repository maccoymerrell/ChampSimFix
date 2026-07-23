#ifndef PREFETCHER_EPI_H
#define PREFETCHER_EPI_H

////////////////////////////////////////////////////////////////////////
//
//  The Entangling Instruction Prefetcher (EPI)
//  Authors: Alberto Ros (aros@ditec.um.es), Alexandra Jimborean
//  Paper #30, First Instruction Prefetching Championship (IPC-1).
//
//  Port to ChampSimRuntime's modular prefetcher API. Faithful to the original
//  algorithm; the per-CPU global arrays are now per-instance members, and the
//  self-maintained "timing cache" is sized to the real L1I geometry (the
//  original hard-assumed 64 sets x 8 ways) rather than a fixed 64x8.
//
////////////////////////////////////////////////////////////////////////

#include <array>
#include <cstdint>
#include <vector>

#include "address.h"
#include "modules.h"

// ---- Structural sizes (state budget; from the IPC-1 submission) ----
inline constexpr uint32_t L1I_HIST_TABLE_ENTRIES = 1072;
inline constexpr uint32_t L1I_ENTANGLED_TABLE_INDEX_BITS = 8;
inline constexpr uint32_t L1I_ENTANGLED_TABLE_SETS = (1u << L1I_ENTANGLED_TABLE_INDEX_BITS);
inline constexpr uint32_t L1I_ENTANGLED_TABLE_WAYS = 34;
inline constexpr uint32_t L1I_ENTANGLED_NUM_FORMATS = 6;
inline constexpr uint32_t L1I_MAX_ENTANGLED_PER_LINE = L1I_ENTANGLED_NUM_FORMATS;
inline constexpr uint32_t L1I_XPQ_ENTRIES = 32;
// MSHR approximation: original MAX_PQ_SIZE(32) + MAX_RQ_SIZE(64) + 1024
inline constexpr uint32_t L1I_TIMING_MSHR_SIZE = 32 + 64 + 1024;

struct l1i_hist_entry {
  uint64_t tag;       // L1I_HIST_TAG_BITS bits
  uint64_t time_diff; // L1I_TIME_DIFF_BITS bits
  uint32_t bb_size;   // L1I_MERGE_BBSIZE_BITS bits
};

struct l1i_timing_mshr_entry {
  bool valid;
  uint64_t tag;
  uint64_t bere_line_addr;
  uint64_t timestamp; // time when issued
  bool accessed;
};

struct l1i_timing_cache_entry {
  bool valid;
  uint64_t tag;
  uint64_t bere_line_addr;
  bool accessed;
};

struct l1i_entangled_entry {
  uint64_t tag;
  uint32_t format;
  uint64_t entangled_addr[L1I_MAX_ENTANGLED_PER_LINE]; // stored as compressed diff
  uint32_t entangled_conf[L1I_MAX_ENTANGLED_PER_LINE];
  uint32_t bb_size;
};

struct l1i_xpq_entry {
  uint64_t line_addr;
  uint64_t entangled_addr;
  uint32_t bb_size;
};

struct epi : public champsim::modules::prefetcher {
  using prefetcher::prefetcher;

  // parent cache handle (for cycle / geometry / PQ occupancy)
  champsim::modules::cache_module* cache_ = nullptr;

  // ---- per-instance state (was [NUM_CPUS] globals) ----
  uint64_t current_cycle_ = 0;

  uint64_t last_basic_block_ = 0;
  uint32_t consecutive_count_ = 0;
  uint32_t basic_block_merge_diff_ = 0;

  // History table (FIFO ring)
  std::array<l1i_hist_entry, L1I_HIST_TABLE_ENTRIES> hist_table_{};
  uint64_t hist_table_head_ = 0;
  uint64_t hist_table_head_time_ = 0;

  // Timing tables (approximate the MSHR + the real L1I cache)
  std::array<l1i_timing_mshr_entry, L1I_TIMING_MSHR_SIZE> timing_mshr_table_{};
  std::vector<std::vector<l1i_timing_cache_entry>> timing_cache_table_; // [set][way]
  uint32_t tc_num_set_ = 64;
  uint32_t tc_num_way_ = 8;
  uint32_t tc_num_set_bits_ = 6;

  // Entangled prediction table
  std::array<std::array<l1i_entangled_entry, L1I_ENTANGLED_TABLE_WAYS>, L1I_ENTANGLED_TABLE_SETS> entangled_table_{};
  std::array<uint32_t, L1I_ENTANGLED_TABLE_SETS> entangled_fifo_{};

  // Extra prefetch queue (basic-block expansion)
  std::array<l1i_xpq_entry, L1I_XPQ_ENTRIES> xpq_{};
  uint64_t xpq_head_ = 0;

  // stats
  uint64_t stat_pf_issued_ = 0;

  // ---- module interface ----
  void prefetcher_initialize() override;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in) override;
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in) override;
  void prefetcher_cycle_operate() override;
  void prefetcher_final_stats() override;
  void prefetcher_branch_operate(champsim::address /*ip*/, uint8_t /*branch_type*/, champsim::address /*branch_target*/) override {}

  epi(champsim::modules::ModuleBuilder b) : cache_(b.get_parent<champsim::modules::cache_module>()) {}

  // ---- helpers (ported; l1i_ prefix dropped) ----
  uint64_t get_latency(uint64_t cycle, uint64_t cycle_prev) const;

  // history table
  void init_hist_table();
  uint64_t find_hist_entry(uint64_t line_addr) const;
  void add_hist_table(uint64_t line_addr);
  void add_bb_size_hist_table(uint64_t line_addr, uint32_t bb_size);
  uint32_t find_bb_merge_hist_table(uint64_t line_addr) const;
  uint64_t get_bere_hist_table(uint64_t line_addr, uint64_t latency, uint32_t skip = 0) const;

  // timing tables
  void init_timing_tables();
  uint64_t find_timing_mshr_entry(uint64_t line_addr) const;
  uint64_t find_timing_cache_entry(uint64_t line_addr) const;
  uint32_t get_invalid_timing_mshr_entry() const;
  uint32_t get_invalid_timing_cache_entry(uint64_t line_addr) const;
  void add_timing_entry(uint64_t line_addr, uint64_t bere_line_addr);
  void invalid_timing_mshr_entry(uint64_t line_addr);
  void move_timing_entry(uint64_t line_addr);
  bool invalid_timing_cache_entry(uint64_t line_addr, uint64_t& bere_line_addr);
  void access_timing_entry(uint64_t line_addr);
  bool is_accessed_timing_entry(uint64_t line_addr) const;
  bool completed_request(uint64_t line_addr) const;
  bool ongoing_request(uint64_t line_addr) const;
  bool ongoing_accessed_request(uint64_t line_addr) const;
  uint64_t get_latency_timing_mshr(uint64_t line_addr) const;

  // entangled table
  uint32_t get_format_entangled(uint64_t line_addr, uint64_t entangled_addr) const;
  uint64_t extend_format_entangled(uint64_t line_addr, uint64_t entangled_addr, uint32_t format) const;
  uint64_t compress_format_entangled(uint64_t entangled_addr, uint32_t format) const;
  void init_entangled_table();
  uint32_t get_way_entangled_table(uint64_t line_addr) const;
  void add_entangled_table(uint64_t line_addr, uint64_t entangled_addr);
  bool avail_entangled_table(uint64_t line_addr, uint64_t entangled_addr, bool insert_not_present) const;
  void add_bbsize_table(uint64_t line_addr, uint32_t bb_size);
  uint64_t get_entangled_addr_entangled_table(uint64_t line_addr, uint32_t index_k) const;
  uint32_t get_bbsize_entangled_table(uint64_t line_addr) const;
  void update_confidence_entangled_table(uint64_t line_addr, uint64_t entangled_addr, bool accessed);

  // xpq
  void init_xpq();
  void add_xpq(uint64_t line_addr, uint64_t entangled_addr, uint32_t bb_size);
  bool empty_xpq() const;
  uint64_t get_xpq(uint64_t& entangled_addr);

  // issue helper (shared by cache_operate + cycle_operate)
  void do_prefetches(uint32_t metadata_in);
  bool pq_full() const;
};

#endif
