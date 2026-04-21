#ifndef PREFETCHER_BP_H
#define PREFETCHER_BP_H

#include "champsim.h"
#include "modules.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <vector>
#include "dynamic_bitset.h"

// Context state that SPPAM owns and provides to branch predictors.
// Predictors must NOT track these internally — SPPAM updates them
// between calls so that burst-mode predict/train sequences stay coherent.
struct bp_context {
  // --- IP path / recency (used by MPP, tage_sc_l) ---
  static constexpr int MAX_PATH_HIST = 64;
  static constexpr int MAX_RECENCY = 32;
  std::array<uint16_t, MAX_PATH_HIST> path_history{};
  std::array<uint16_t, MAX_RECENCY> ip_recency{};

  // --- SC-style histories (used by tage_sc_l) ---
  static constexpr int PHISTWIDTH = 27;
  long long phist = 0;           // IP-derived path history for TAGE index
  // NOTE: ghist_internal, L_shist, S_slhist, T_slhist have been removed.
  // They were redundant with the global_hist / local_hist dynamic_bitsets
  // that SPPAM already provides as parameters to predict/train.

  // --- Helper: update all streaming histories after an outcome ---
  void update(uint64_t PC, bool outcome) {
    // Path history (for TAGE gindex)
    int PATH = PC ^ (PC >> 2) ^ (PC >> 4);
    int pathbit = (PATH & 127);
    phist = (phist << 1) ^ pathbit;
    phist = (phist & ((1LL << PHISTWIDTH) - 1));

    // IP path history (for MPP)
    uint16_t pc2 = static_cast<uint16_t>(PC >> 2);
    std::memmove(&path_history[1], &path_history[0], sizeof(uint16_t) * (MAX_PATH_HIST - 1));
    path_history[0] = pc2;

    // IP recency stack (LRU for MPP)
    int found = MAX_RECENCY;
    for (int i = 0; i < MAX_RECENCY; i++) {
      if (ip_recency[i] == pc2) { found = i; break; }
    }
    if (found == MAX_RECENCY) found = MAX_RECENCY - 1;
    uint16_t saved = ip_recency[found];
    for (int j = found; j >= 1; j--) ip_recency[j] = ip_recency[j - 1];
    ip_recency[0] = saved;
  }
};

class branch_predictor {
    public:
    virtual void initialize_branch_predictor() = 0;
    virtual void last_branch_result(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, bool taken, bp_context& ctx) = 0;
    virtual std::pair<bool,double> predict_branch(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, const bp_context& ctx) = 0;

    virtual void print_heartbeat() {};
    virtual void print_stats() {};

};

template<std::size_t bitout, std::size_t bitin>
std::bitset<bitout> truncate_bitset(std::bitset<bitin> input) {
    std::bitset<bitout> output;
    for(std::size_t i = 0; i < bitout; i++) {
        output.set(i,input.test(i));
    }
    return output;
}

template<std::size_t bitwidth>
void print_bitset(std::bitset<bitwidth> input, int mark = -1) {
    for(int i = 0; i < bitwidth; i++) {
        if(i == mark)
            fmt::print("[{}]",input.test(i) ? 1 : 0);
        else
            fmt::print(" {} ",input.test(i) ? 1 : 0);
    }
    fmt::print("\n");
}

template<std::size_t bitwidth>
std::string format_bitset(std::bitset<bitwidth> input, int mark = -1) {
    std::string output;
    for(int i = bitwidth-1; i >= 0; i--) {
        if(i == mark)
            output += fmt::format("[{}]",input.test(i) ? 1 : 0);
        else
            output += fmt::format(" {} ",input.test(i) ? 1 : 0);
    }
    return output;
}

template<std::size_t N>
void reverse_bitset(std::bitset<N> &b) {
    for(std::size_t i = 0; i < N/2; ++i) {
        bool t = b[i];
        b[i] = b[N-i-1];
        b[N-i-1] = t;
    }
}

//fold a bitset down to a smaller size by XORing bits together, e.g. folding 64 bits down to 16 bits would XOR bits 0,16,32,48 together to produce bit 0 of the output
template<std::size_t outbits, std::size_t inbits, std::size_t wordbits = inbits/outbits>
std::bitset<outbits> fold_bitset(std::bitset<inbits> input) {
    std::bitset<outbits> output;
    //if inbits is less than or equal to outbits, just copy the bits over
    if(inbits <= outbits) {
        for(std::size_t i = 0; i < inbits; i++) {
            output.set(i,input.test(i));
        }
        return output;
    }
    //do geometric folding, XORing together bits that are wordbits apart until we have folded down to outbits
    //wordbits is the number of bits we fold together at a time, so for example if wordbits is 16, we would XOR bits 0,16,32,48 together to produce bit 0 of the output
    //bitset could be an arbitrary size, well over ulonglong, so we can't just do shifts and masks, we have to use the bitset interface
    for(std::size_t i = 0; i < outbits; i++) {
        bool bit = false;
        for(std::size_t j = i; j < inbits; j += wordbits) {
            bit ^= input.test(j);
        }
        output.set(i,bit);
    }
    return output;
}

#endif
