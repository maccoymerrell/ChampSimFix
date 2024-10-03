/*****************************************************
For the Third Data Prefetching Championship - DPC3

Paper ID: #4
Instruction Pointer Classifying Prefetcher - IPCP

Authors:
Samuel Pakalapati - pakalapatisamuel@gmail.com
Biswabandan Panda - biswap@cse.iitk.ac.in
******************************************************/

#include "cache.h"
#include "modules.h"

// #define SIG_DEBUG_PRINT_L2
#ifdef SIG_DEBUG_PRINT_L2
#define SIG_DP(x) x
#else
#define SIG_DP(x)
#endif

class bouquet_l2c : public champsim::modules::prefetcher {

    static constexpr uint64_t NUM_IP_TABLE_L2_ENTRIES = 1024;
    static constexpr uint64_t NUM_IP_INDEX_BITS = 10;
    static constexpr uint64_t NUM_IP_TAG_BITS = 6;
    static constexpr uint16_t S_TYPE = 1;    // global stream (GS)
    static constexpr uint16_t CS_TYPE = 2;   // constant stride (CS)
    static constexpr uint16_t CPLX_TYPE = 3; // complex stride (CPLX)
    static constexpr uint16_t NL_TYPE = 4;   // next line (NL)

    struct IP_TRACKER
    {
        uint64_t ip_tag = 0;
        uint16_t ip_valid = 0;
        uint32_t pref_type = 0; // prefetch class type
        int stride = 0;         // last stride sent by metadata
    };

    static std::vector<uint32_t> spec_nl_l2;
    static std::vector<std::vector<IP_TRACKER>> trackers;

    static uint64_t instances;
    uint64_t instance_id;

    int decode_stride(uint32_t metadata);

    using prefetcher::prefetcher;
    public:
        void prefetcher_initialize();
        uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                        uint32_t metadata_in);
        uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
        void prefetcher_final_stats();
        void prefetcher_cycle_operate();
};