#ifndef BP_BIMODAL_H
#define BP_BIMODAL_H

#include "../branch_predictor.h"
#include "msl/fwcounter.h"

namespace sppam_bp {
struct bimodal : branch_predictor {
  static constexpr std::size_t TABLE_SIZE = 65536;
  static constexpr std::size_t COUNTER_BITS = 2;
  static constexpr bool DEBUG = false;

  std::size_t rt_table_size = TABLE_SIZE;

  uint64_t predict_taken = 0;
  uint64_t predict_nottaken = 0;
  uint64_t outcome_taken = 0;
  uint64_t outcome_nottaken = 0;

  std::array<champsim::msl::fwcounter<COUNTER_BITS>, TABLE_SIZE> bimodal_table;
  std::array<uint64_t, TABLE_SIZE> occurrences{};

  using branch_predictor::branch_predictor;

  [[nodiscard]] auto hash(champsim::address ip) const { return ip.to<unsigned long>() % rt_table_size; }

  void configure(unsigned int table_size) {
    rt_table_size = std::min(static_cast<std::size_t>(table_size), TABLE_SIZE);
    if (rt_table_size == 0) rt_table_size = 1;
  }

  virtual void initialize_branch_predictor() {};

  virtual void last_branch_result(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, bool taken, bp_context& ctx);
  virtual std::pair<bool,double> predict_branch(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, const bp_context& ctx);

  void print_heartbeat();
  void print_stats();
};
}

#endif
