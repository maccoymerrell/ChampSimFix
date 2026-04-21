#ifndef BP_HASHED_PERCEPTRON_H
#define BP_HASHED_PERCEPTRON_H

#include <array>
#include <cstdint>
#include <tuple>
#include <vector>

#include "msl/bits.h"
#include "msl/fwcounter.h"
#include "../branch_predictor.h"
#include "fmt/ranges.h"

namespace sppam_bp {
struct hashed_perceptron : branch_predictor 
{
  using bits = champsim::data::bits;                 // saves some typing
  constexpr static std::size_t NTABLES = 16;         // this many tables (max)
  constexpr static std::size_t MAXHIST{256};                // maximum history length
  constexpr static std::size_t MINHIST{3};                  // minimum history length (for table 1; table 0 is biases)
  constexpr static std::size_t TABLE_SIZE = 1 << 16; // 16-bit indices for the tables (max)
  constexpr static std::size_t TABLE_INDEX_BITS{champsim::msl::lg2(TABLE_SIZE)};
  constexpr static int THRESHOLD = 1;

  constexpr static bool USE_LOCAL_HISTORY = false; // set to true to use local history instead of global history (but still seed with PC)
  constexpr static bool DEBUG = false;

  constexpr static std::array<std::size_t, NTABLES> history_lengths = {
      0,   MINHIST,  4,  6,  8,  10,  14,  19,
      26, 36,      49, 67, 91,   125,   170, MAXHIST}; // geometric global history lengths

  // Runtime-configurable parameters (defaults = compile-time constants)
  std::size_t rt_num_tables = NTABLES;
  std::size_t rt_table_size = TABLE_SIZE;
  std::size_t rt_table_index_bits = TABLE_INDEX_BITS;
  std::array<std::size_t, NTABLES> rt_history_lengths = history_lengths;

  // tables of 8-bit weights
  std::array<std::array<champsim::msl::sfwcounter<8>, TABLE_SIZE>, NTABLES> tables{};

  int theta = 10;
  int tc = 0; // counter for threshold setting algorithm

  struct perceptron_result {
    std::array<uint64_t, NTABLES> indices = {}; // remember the indices into the tables from prediction to update
    int yout = 0;                               // perceptron sum
  };

  uint64_t predict_taken = 0;
  uint64_t predict_nottaken = 0;
  uint64_t outcome_taken = 0;
  uint64_t outcome_nottaken = 0;

  perceptron_result get_perceptron_result(champsim::address pc, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist);

  void configure(unsigned int num_tables, unsigned int table_size, unsigned int min_hist, unsigned int max_hist);

public:
  using branch_predictor::branch_predictor;
  virtual void initialize_branch_predictor() {};

  virtual void last_branch_result(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, bool taken, bp_context& ctx);
  virtual std::pair<bool,double> predict_branch(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, const bp_context& ctx);

  void print_heartbeat();
  void print_stats();
  void adjust_threshold(bool correct);
};
}
#endif
