#ifndef BP_GSHARE_H
#define BP_GSHARE_H

#include "../branch_predictor.h"
#include "msl/fwcounter.h"

namespace sppam_bp {
struct gshare : branch_predictor {
  static constexpr std::size_t GLOBAL_HISTORY_LENGTH = 256;
  static constexpr std::size_t HASH_LENGTH = 16;
  static constexpr std::size_t COUNTER_BITS = 2;
  static constexpr std::size_t GS_HISTORY_TABLE_SIZE = 65536;
  static constexpr int COUNTER_UP = 1;
  static constexpr int COUNTER_DOWN = -1;
  static constexpr bool USE_LOCAL_HISTORY = false;
  static constexpr bool DEBUG = false;

  std::size_t rt_table_size = GS_HISTORY_TABLE_SIZE;
  std::size_t rt_history_length = GLOBAL_HISTORY_LENGTH;

  uint64_t predict_taken = 0;
  uint64_t predict_nottaken = 0;
  uint64_t outcome_taken = 0;
  uint64_t outcome_nottaken = 0;

  std::array<champsim::msl::fwcounter<COUNTER_BITS>, GS_HISTORY_TABLE_SIZE> gs_history_table;
  std::array<uint64_t,GS_HISTORY_TABLE_SIZE> gs_occurrences;

  std::array<uint64_t,GS_HISTORY_TABLE_SIZE> gs_history_occurrences_predict;
  std::array<uint64_t,GS_HISTORY_TABLE_SIZE> gs_history_occurrences_outcome;

  using branch_predictor::branch_predictor;

  void configure(unsigned int table_size, unsigned int hist_len) {
    rt_table_size = std::min(static_cast<std::size_t>(table_size), GS_HISTORY_TABLE_SIZE);
    if (rt_table_size == 0) rt_table_size = 1;
    rt_history_length = std::min(static_cast<std::size_t>(hist_len), GLOBAL_HISTORY_LENGTH);
    if (rt_history_length == 0) rt_history_length = 1;
  }

  std::size_t gs_table_hash(champsim::address ip, uint64_t bh_vector, bool predict_or_outcome);
  virtual void initialize_branch_predictor() {};

  virtual void last_branch_result(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, bool taken, bp_context& ctx);
  virtual std::pair<bool,double> predict_branch(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, const bp_context& ctx);

  void print_heartbeat();
  void print_stats();
};
}

#endif
