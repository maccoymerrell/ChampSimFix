//=======================================================================================//
// File             : pythia/pythia.h
// Author           : Rahul Bera, SAFARI Research Group (write2bera@gmail.com)
// Date             : 20/AUG/2025
// Description      : Implements Pythia prefectcher (Bera+, MICRO'21)
//=======================================================================================//

#ifndef __PYTHIA_H__
#define __PYTHIA_H__

#include <deque>

#include "champsim.h"
#include "learning_engine_featurewise.h"
#include "modules.h"
#include "pythia_helper.h"

// ChampSimRuntime's modular build no longer exposes the global
// LOG2_BLOCK_SIZE / LOG2_PAGE_SIZE macros (modules read block/page size from the
// config). Pythia uses these in address<->page/offset arithmetic, so define them
// here for the standard 64-byte cache line / 4KB page used by all DPC4 traces.
#ifndef LOG2_BLOCK_SIZE
#define LOG2_BLOCK_SIZE 6
#endif
#ifndef LOG2_PAGE_SIZE
#define LOG2_PAGE_SIZE 12
#endif

struct pythia : public champsim::modules::prefetcher {
private:
  std::deque<Scooby_STEntry*> signature_table;
  LearningEngineFeaturewise* brain_featurewise;
  std::deque<Scooby_PTEntry*> prefetch_tracker;
  Scooby_PTEntry* last_evicted_tracker;

  /* Action array: basically a set of deltas to evaluate */
  std::vector<int32_t> Actions;

  /* for managing stats */
  PythiaStats stats;

  // local functions
  void init_knobs();
  void update_global_state(uint64_t pc, uint64_t page, uint32_t offset, uint64_t address);
  Scooby_STEntry* update_local_state(uint64_t pc, uint64_t page, uint32_t offset, uint64_t address);
  uint32_t predict(uint64_t address, uint64_t page, uint32_t offset, State* state, std::vector<uint64_t>& pref_addr);
  bool track(uint64_t address, State* state, uint32_t action_index, Scooby_PTEntry** tracker);
  void reward(uint64_t address);
  void reward(Scooby_PTEntry* ptentry);
  void assign_reward(Scooby_PTEntry* ptentry, RewardType type);
  int32_t compute_reward(Scooby_PTEntry* ptentry, RewardType type);
  void train(Scooby_PTEntry* curr_evicted, Scooby_PTEntry* last_evicted);
  void register_fill(uint64_t address);
  std::vector<Scooby_PTEntry*> search_pt(uint64_t address, bool search_all = false);
  void track_in_st(uint64_t page, uint32_t pred_offset, int32_t pref_offset);
  void gen_multi_degree_pref(uint64_t page, uint32_t offset, int32_t action, uint32_t pref_degree, std::vector<uint64_t>& pref_addr);
  uint32_t get_dyn_pref_degree(float max_to_avg_q_ratio, uint64_t page = 0xdeadbeef, int32_t action = 0); /* only implemented for CMAC engine 2.0 */
  int32_t getAction(uint32_t action_index);
  bool is_high_bw(uint8_t bw_level);

  // ChampSimRuntime has no DRAM-bandwidth accessor (DPC4's dpc_api.h /
  // get_dram_bw() / get_bw() do not exist here). Returning 0 keeps Pythia in the
  // low-bandwidth bucket, which disables the high-bandwidth reward path. This is
  // the safe neutral default (see is_high_bw / high_bw_thresh); it does NOT
  // fabricate a bandwidth signal.
  uint8_t get_dram_bw() { return 0; }

  // The parent cache, captured at construction.
  champsim::modules::cache_module* cache_ = nullptr;

public:
  explicit pythia(champsim::modules::ModuleBuilder builder) : cache_(builder.get_parent<champsim::modules::cache_module>()) {}

  // interface to the rest of ChampSim
  void prefetcher_initialize() override;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in) override;
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in) override;
  void prefetcher_cycle_operate() override;
  void prefetcher_final_stats() override;
  void prefetcher_branch_operate(champsim::address /*ip*/, uint8_t /*branch_type*/, champsim::address /*branch_target*/) override {}
};

#endif /* __PYTHIA_H__ */
