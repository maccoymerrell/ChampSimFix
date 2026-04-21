#ifndef BP_MPP_H
#define BP_MPP_H

#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <list>
#include <cassert>

#include "../branch_predictor.h"
#include "msl/fwcounter.h"

namespace sppam_bp {

// Feature types adapted for sppam_b's provided global/local history
enum mpp_feature_type {
  MPP_BIAS = 0,         // table indexed by hashed IP only
  MPP_GHIST,            // fold global_hist to a range [p1..p2), XOR with IP
  MPP_LHIST,            // fold local_hist to a range [p1..p2), XOR with IP
  MPP_GHISTPATH,        // fold global_hist [p1..p2), XOR with IP path hash of depth p3
  MPP_MIXED,            // fold (global ^ local) at range [p1..p2), XOR with IP
  MPP_RECENCY,          // hash of internally maintained recency stack of IPs
  MPP_PATH,             // hash of internally maintained IP path history
};

struct mpp_feature_spec {
  mpp_feature_type type;
  int p1, p2, p3;        // parameters interpreted per feature type
  int table_size;         // number of entries in this feature's weight table
};

struct mpp : branch_predictor {
  // Config
  static constexpr int NTABLES = 16;
  static constexpr int BITWIDTH = 6;
  static constexpr int MAX_TABLE_SIZE = 1 << 16; // 65536 entries per table
  static constexpr int MAX_PATH_HIST = 64;
  static constexpr int MAX_RECENCY = 32;
  static constexpr bool DEBUG = false;

  // Transfer function: maps 6-bit signed weight to a scaled value
  // From the original MPP: a tuned near-linear xlat
  static constexpr std::array<int, 64> xlat = {{
    -252,-244,-236,-228,-220,-212,-204,-196,
    -188,-180,-172,-164,-156,-148,-140,-132,
    -124,-116,-108,-100, -92, -84, -76, -68,
     -60, -52, -44, -36, -28, -20, -12,  -4,
       4,  12,  20,  28,  36,  44,  52,  60,
      68,  76,  84,  92, 100, 108, 116, 124,
     132, 140, 148, 156, 164, 172, 180, 188,
     196, 204, 212, 220, 228, 236, 244, 252
  }};

  // Default feature specification for 16 tables, adapted for sppam_b
  // Each feature uses a different perspective on the provided histories
  static constexpr std::array<mpp_feature_spec, NTABLES> default_specs = {{
    {MPP_BIAS,      0,  0, 0,  MAX_TABLE_SIZE},     // bias: just IP
    {MPP_GHIST,     0, 16, 0,  MAX_TABLE_SIZE},     // short global history
    {MPP_GHIST,     0, 32, 0,  MAX_TABLE_SIZE},     // medium global history
    {MPP_GHIST,     0, 64, 0,  MAX_TABLE_SIZE},     // long global history
    {MPP_GHIST,     0,128, 0,  MAX_TABLE_SIZE},     // very long global history
    {MPP_GHIST,     0,256, 0,  MAX_TABLE_SIZE},     // full global history
    {MPP_LHIST,     0, 16, 0,  MAX_TABLE_SIZE},     // short local history
    {MPP_LHIST,     0, 32, 0,  MAX_TABLE_SIZE},     // medium local history
    {MPP_LHIST,     0, 64, 0,  MAX_TABLE_SIZE},     // full local history
    {MPP_GHISTPATH, 0, 32, 4,  MAX_TABLE_SIZE},     // global + path depth 4
    {MPP_GHISTPATH, 0, 64, 8,  MAX_TABLE_SIZE},     // global + path depth 8
    {MPP_GHISTPATH, 0,128,16,  MAX_TABLE_SIZE},     // global + path depth 16
    {MPP_MIXED,     0, 32, 0,  MAX_TABLE_SIZE},     // short mixed global+local
    {MPP_MIXED,     0, 64, 0,  MAX_TABLE_SIZE},     // long mixed global+local
    {MPP_PATH,      8,  3, 0,  MAX_TABLE_SIZE},     // IP path: depth 8, shift 3
    {MPP_RECENCY,  16,  2, 0,  MAX_TABLE_SIZE},     // recency stack: depth 16, shift 2
  }};

  // Weight tables: 6-bit signed weights
  struct Weight {
    int8_t val = 0;
  };
  std::array<std::array<Weight, MAX_TABLE_SIZE>, NTABLES> tables{};

  // Runtime-configurable parameters
  int rt_num_tables = NTABLES;
  int rt_table_size = MAX_TABLE_SIZE;
  std::array<mpp_feature_spec, NTABLES> rt_specs{};

  // Adaptive theta
  double theta = 11.0;
  int tc = 0;
  static constexpr int MIN_THETA = 10;
  static constexpr int MAX_THETA = 255;
  static constexpr int SPEED = 21;
  static constexpr double FUDGE = 0.258;

  // Prediction state preserved between predict and update
  int last_yout = 0;
  std::array<unsigned int, NTABLES> last_indices{};

  // Stats
  uint64_t predict_taken = 0;
  uint64_t predict_nottaken = 0;
  uint64_t outcome_taken = 0;
  uint64_t outcome_nottaken = 0;

  using branch_predictor::branch_predictor;

  void configure(unsigned int num_tables, unsigned int table_size) {
    rt_num_tables = std::min(static_cast<int>(num_tables), NTABLES);
    if (rt_num_tables < 1) rt_num_tables = 1;
    rt_table_size = std::min(static_cast<int>(table_size), MAX_TABLE_SIZE);
    if (rt_table_size < 1) rt_table_size = 1;
    // Copy default specs and override table_size
    for (int i = 0; i < NTABLES; i++) {
      rt_specs[i] = default_specs[i];
      rt_specs[i].table_size = rt_table_size;
    }
  }

  // Initialize rt_specs from defaults in constructor-like fashion
  mpp() {
    for (int i = 0; i < NTABLES; i++)
      rt_specs[i] = default_specs[i];
  }

  // --- Hash utilities ---

  static unsigned int hash1(unsigned int a) {
    a = (a ^ 0xdeadbeef) + (a << 4);
    a = a ^ (a >> 10);
    a = a + (a << 7);
    a = a ^ (a >> 13);
    return a;
  }

  static unsigned int hash2(unsigned int key) {
    unsigned int c2 = 0x27d4eb2d;
    key = (key ^ 61) ^ (key >> 16);
    key = key + (key << 3);
    key = key ^ (key >> 4);
    key = key * c2;
    key = key ^ (key >> 15);
    return key;
  }

  static unsigned int hash(unsigned int key, unsigned int i) {
    return hash2(key) * i + hash1(key);
  }

  // Fold a bitset range [start, end) down to `bits` via XOR folding
  static unsigned int fold_range(const dynamic_bitset& hist, int start, int end, int bits) {
    if(bits <= 0 || start >= end) return 0;
    unsigned int result = 0;
    int pos = 0;
    for(int i = start; i < end && i < static_cast<int>(hist.size()); i++) {
      if(hist.test(i))
        result ^= (1u << (pos % bits));
      pos++;
    }
    return result & ((1u << bits) - 1);
  }

  // Hash IP path history from context
  static unsigned int hash_path(const bp_context& ctx, int depth, int shift) {
    unsigned int x = 0;
    for(int i = 0; i < depth && i < bp_context::MAX_PATH_HIST; i++) {
      x <<= shift;
      x += ctx.path_history[i];
    }
    return x;
  }

  // Hash recency stack from context
  static unsigned int hash_recency(const bp_context& ctx, int depth, int shift) {
    unsigned int x = 0;
    for(int i = 0; i < depth && i < bp_context::MAX_RECENCY; i++) {
      x <<= shift;
      x += ctx.ip_recency[i];
    }
    return x;
  }

  int translate(int c) const {
    int idx = c + 31;
    if(idx < 0) idx = 0;
    if(idx >= 64) idx = 63;
    return xlat[idx];
  }

  int satincdec(int c, bool taken) const {
    int limit = (1 << (BITWIDTH - 1)) - 1;
    if(taken) { if(c < limit) c++; }
    else { if(c > -limit) c--; }
    return c;
  }

  // Compute feature hash for a given spec
  unsigned int get_feature_hash(const mpp_feature_spec& spec, champsim::address ip,
                                const dynamic_bitset& global_hist,
                                const dynamic_bitset& local_hist,
                                const bp_context& ctx,
                                int table_idx) const;

  // Core compute
  void compute_output(champsim::address ip,
                      const dynamic_bitset& global_hist,
                      const dynamic_bitset& local_hist,
                      const bp_context& ctx);

  void train(bool taken);
  void theta_setting(bool correct, int a);

  // --- Interface ---
  virtual void initialize_branch_predictor() {};

  virtual void last_branch_result(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, bool taken, bp_context& ctx);
  virtual std::pair<bool,double> predict_branch(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, const bp_context& ctx);

  void print_heartbeat();
  void print_stats();
};

}

#endif
