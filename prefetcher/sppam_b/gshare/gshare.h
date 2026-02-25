#ifndef BP_GSHARE_H
#define BP_GSHARE_H

#include "../branch_predictor.h"
#include "msl/fwcounter.h"

namespace sppam_bp {
struct gshare : branch_predictor {
  static constexpr std::size_t GLOBAL_HISTORY_LENGTH = 14;
  static constexpr std::size_t HASH_LENGTH = 14;
  static constexpr std::size_t COUNTER_BITS = 2;
  static constexpr std::size_t GS_HISTORY_TABLE_SIZE = 16384;
  static constexpr int COUNTER_UP = 1;
  static constexpr int COUNTER_DOWN = -1;
  static constexpr bool USE_LOCAL_HISTORY = false;
  static constexpr bool DEBUG = false;

  uint64_t predict_taken = 0;
  uint64_t predict_nottaken = 0;
  uint64_t outcome_taken = 0;
  uint64_t outcome_nottaken = 0;

  std::array<champsim::msl::fwcounter<COUNTER_BITS>, GS_HISTORY_TABLE_SIZE> gs_history_table;
  std::array<uint64_t,GS_HISTORY_TABLE_SIZE> gs_occurrences;

  std::array<uint64_t,GS_HISTORY_TABLE_SIZE> gs_history_occurrences_predict;
  std::array<uint64_t,GS_HISTORY_TABLE_SIZE> gs_history_occurrences_outcome;

  using branch_predictor::branch_predictor;

  std::size_t gs_table_hash(champsim::address ip, std::bitset<GLOBAL_HISTORY_LENGTH> bh_vector, bool predict_or_outcome);
  virtual void initialize_branch_predictor() {};

  virtual void last_branch_result(champsim::address ip, std::bitset<BP_GLOBAL_BITS> global_hist, std::bitset<BP_LOCAL_BITS> local_hist, bool taken);
  virtual std::pair<bool,double> predict_branch(champsim::address ip, std::bitset<BP_GLOBAL_BITS> global_hist, std::bitset<BP_LOCAL_BITS> local_hist);

  void print_heartbeat();
  void print_stats();
};
}

#endif
