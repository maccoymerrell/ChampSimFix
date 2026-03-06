#ifndef PREFETCHER_BP_H
#define PREFETCHER_BP_H

#include "champsim.h"
#include "modules.h"
#include <bitset>

static constexpr unsigned int BP_GLOBAL_BITS = 256;
static constexpr unsigned int BP_SEGMENT_BITS = 64;
static constexpr unsigned int BP_LOCAL_BITS = 64;
static constexpr int BP_ALIGNMENT_FACTOR = 0; //this is the offset from the last access provided to the bp, an alignment of 2 means we read from 2 spots ahead of the last offset when providing history

class branch_predictor {
    public:
    virtual void initialize_branch_predictor() = 0;
    virtual void last_branch_result(champsim::address ip, std::bitset<BP_GLOBAL_BITS> global_hist, std::bitset<BP_LOCAL_BITS> local_hist, bool taken) = 0;
    virtual std::pair<bool,double> predict_branch(champsim::address ip, std::bitset<BP_GLOBAL_BITS> global_hist, std::bitset<BP_LOCAL_BITS> local_hist) = 0;

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
