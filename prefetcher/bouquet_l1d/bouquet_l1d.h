/***************************************************************************
For the Third Data Prefetching Championship - DPC3

Paper ID: #4
Instruction Pointer Classifying Prefetcher - IPCP

Authors:
Samuel Pakalapati - samuelpakalapati@gmail.com
Biswabandan Panda - biswap@cse.iitk.ac.in
***************************************************************************/

#include "cache.h"
#include "modules.h"

// #define SIG_DEBUG_PRINT
#ifdef SIG_DEBUG_PRINT
#define SIG_DP(x) x
#else
#define SIG_DP(x)
#endif

class bouquet_l1d : public champsim::modules::prefetcher {
    struct IP_TABLE_L1
    {
        uint64_t ip_tag = 0;
        uint64_t last_page = 0;      // last page seen by IP
        uint64_t last_cl_offset = 0; // last cl offset in the 4KB page
        int64_t last_stride = 0;     // last delta observed
        uint16_t ip_valid = 0;       // Valid IP or not
        int conf = 0;                // CS conf
        uint16_t signature = 0;      // CPLX signature
        uint16_t str_dir = 0;        // stream direction
        uint16_t str_valid = 0;      // stream valid
        uint16_t str_strength = 0;   // stream strength
    };

    struct DELTA_PRED_TABLE
    {
        int delta = 0;
        int conf = 0;
    };

    static constexpr uint64_t NUM_IP_TABLE_L1_ENTRIES = 1024; // IP table entries
    static constexpr uint64_t NUM_GHB_ENTRIES = 16;           // Entries in the GHB
    static constexpr uint64_t NUM_IP_INDEX_BITS = 10;         // Bits to index into the IP table
    static constexpr uint64_t NUM_IP_TAG_BITS = 6;            // Tag bits per IP table entry
    static constexpr uint8_t  S_TYPE = 1;                     // stream
    static constexpr uint8_t  CS_TYPE = 2;                    // constant stride
    static constexpr uint8_t  CPLX_TYPE = 3;                  // complex stride
    static constexpr uint8_t  NL_TYPE = 4;                    // next line

    static uint16_t instances;

    uint16_t instance_id;

    static std::vector<std::vector<IP_TABLE_L1>> trackers_l1;
    //IP_TABLE_L1 trackers_l1[NUM_CPUS][NUM_IP_TABLE_L1_ENTRIES];
    static std::vector<std::vector<DELTA_PRED_TABLE>> DPT_l1;
    //DELTA_PRED_TABLE DPT_l1[NUM_CPUS][4096];
    static std::vector<std::vector<uint64_t>> ghb_l1;
    //uint64_t ghb_l1[NUM_CPUS][NUM_GHB_ENTRIES];
    static std::vector<uint64_t> prev_cpu_cycle;
    //uint64_t prev_cpu_cycle[NUM_CPUS];
    static std::vector<uint64_t> num_misses;
    //uint64_t num_misses[NUM_CPUS];
    static std::vector<float> mpkc;
    //float mpkc[NUM_CPUS] = {0};
    static std::vector<int> spec_nl;
    //int spec_nl[NUM_CPUS] = {0};

    /***************Updating the signature*************************************/
    uint16_t update_sig_l1(uint16_t old_sig, int delta);

    /****************Encoding the metadata***********************************/
    uint32_t encode_metadata(int stride, uint16_t type, int spec_nl_);

    /*********************Checking for a global stream (GS class)***************/

    void check_for_stream_l1(int index, uint64_t cl_addr, uint8_t cpu);

    /**************************Updating confidence for the CS class****************/
    int update_conf(int stride, int pred_stride, int conf);

    using prefetcher::prefetcher;
    public:
        void prefetcher_initialize();
        uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                        uint32_t metadata_in);
        uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
        void prefetcher_final_stats();
        void prefetcher_cycle_operate();
};