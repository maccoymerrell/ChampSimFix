#ifndef Vberti_llc_filter_LLC_H_
#define Vberti_llc_filter_LLC_H_

/*
 * berti_llc_filter: an Accurate Local-Delta Data Prefetcher
 *  
 * 55th ACM/IEEE International Conference on Microarchitecture (MICRO 2022),
 * October 1-5, 2022, Chicago, Illinois, USA.
 * 
 * @Authors: Agustín Navarro-Torres, Biswabandan Panda, J. Alastruey-Benedé, 
 *           Pablo Ibáñez, Víctor Viñals-Yúfera, and Alberto Ros
 * @Manteiners: Agustín Navarro -Torres
 * @Email: agusnt@unizar.es
 * @Date: 22/11/2022
 * 
 * This code is an update from the original code to work with the new version
 * of ChampSim: https://github.com/agusnt/berti_llc_filter-Artifact
 * 
 * Maybe fine-tuning is required to get the optimal performance/accuracy.
 * 
 * Please note that this version of ChampSim has noticeable differences with 
 * the used for the paper, so results can varies.
 * 
 * Cite this:
 * 
 * A. Navarro-Torres, B. Panda, J. Alastruey-Benedé, P. Ibáñez, 
 * V. Viñals-Yúfera and A. Ros, 
 * "berti_llc_filter: an Accurate Local-Delta Data Prefetcher," 
 * 2022 55th IEEE/ACM International Symposium on Microarchitecture (MICRO), 
 * 2022, pp. 975-991, doi: 10.1109/MICRO56248.2022.00072.
 * 
 * @INPROCEEDINGS{9923806,  author={Navarro-Torres, Agustín and Panda, 
 * Biswabandan and Alastruey-Benedé, Jesús and Ibáñez, Pablo and Viñals-Yúfera,
 * Víctor and Ros, Alberto},  booktitle={2022 55th IEEE/ACM International 
 * Symposium on Microarchitecture (MICRO)},   title={berti_llc_filter: an Accurate 
 * Local-Delta Data Prefetcher},   year={2022},  volume={},  number={},  
 * pages={975-991},  doi={10.1109/MICRO56248.2022.00072}}
 */

#include "berti_llc_filter_parameters.h"
#include "cache.h"

#include "msl/lru_table.h"
#include <algorithm>
#include <iostream>
#include <stdlib.h>
#include <cassert>
#include <vector>
#include <time.h>
#include <cstdio>
#include <tuple>
#include <queue>
#include <cmath>
#include <map>

class berti_llc_filter : public champsim::modules::prefetcher {

    public:
    /*****************************************************************************
     *                              Stats                                        *
     *****************************************************************************/
    // Get average latency: Welford's method
    typedef struct welford
    {
        uint64_t num = 0; 
        float average = 0.0;
    } welford_t;
    
    welford_t average_latency;
    
    // Get more info
    uint64_t pf_to_l1 = 0;
    uint64_t pf_to_l2 = 0;
    uint64_t pf_to_l2_bc_mshr = 0;
    uint64_t cant_track_latency = 0;
    uint64_t cross_page = 0;
    uint64_t no_cross_page = 0;
    uint64_t no_found_berti_llc_filter = 0;
    uint64_t found_berti_llc_filter = 0;
    uint64_t average_issued = 0;
    uint64_t average_num = 0;
    
    /*****************************************************************************
     *                      General Structs                                      *
     *****************************************************************************/
    
    typedef struct Delta {
        uint64_t conf;
        int64_t  delta;
        uint8_t  rpl;
        Delta(): conf(0), delta(0), rpl(berti_llc_filter_R) {};
    } delta_t; 
    
    /*****************************************************************************
     *                      berti_llc_filter structures                                     *
     *****************************************************************************/
    class LatencyTable
    {
        /* Latency table simulate the modified PQ and MSHR */
        private:
        struct latency_table {
            uint64_t addr = 0; // Addr
            uint64_t tag  = 0; // IP-Tag 
            uint64_t time = 0; // Event cycle
            bool     pf   = false;   // Is the entry accessed by a demand miss
        };
        int size;
        
        latency_table *latencyt;
    
        public:
        LatencyTable(const int size) : size(size)
        {
            latencyt = new latency_table[size];
        }
        ~LatencyTable() { delete latencyt;}
    
        uint8_t  add(uint64_t addr, uint64_t tag, bool pf, uint64_t cycle);
        uint64_t get(uint64_t addr);
        uint64_t del(uint64_t addr);
        uint64_t get_tag(uint64_t addr);
    };
    
    class ShadowCache
    {
        /* Shadow cache simulate the modified L1D Cache */
        private:
        struct shadow_cache {
            uint64_t addr = 0; // Addr
            uint64_t lat  = 0;  // Latency
            bool     pf   = false;   // Is a prefetch 
        }; // This struct is the vberti_llc_filter table
    
        int sets;
        int ways;
        shadow_cache **scache;
    
        public:
        ShadowCache(const int sets, const int ways)
        {
            scache = new shadow_cache*[sets];
            for (int i = 0; i < sets; i++) scache[i] = new shadow_cache[ways];
    
            this->sets = sets;
            this->ways = ways;
        }
    
        ~ShadowCache()
        {
            for (int i = 0; i < sets; i++) delete scache[i];
            delete scache;
        }
    
        bool add(uint32_t set, uint32_t way, uint64_t addr, bool pf, uint64_t lat);
        bool get(uint64_t addr);
        void set_pf(uint64_t addr, bool pf);
        bool is_pf(uint64_t addr);
        uint64_t get_latency(uint64_t addr);
    };
    
    class HistoryTable
    {
        /* History Table */
        private:
        struct history_table {
            uint64_t tag  = 0; // IP Tag
            uint64_t addr = 0; // IP @ accessed
            uint64_t time = 0; // Time where the line is accessed
        }; // This struct is the history table
    
        const int sets = HISTORY_TABLE_SETS;
        const int ways = HISTORY_TABLE_WAYS;
    
        history_table **historyt;
        history_table **history_pointers;
    
        uint16_t get_aux(uint32_t latency, uint64_t tag, uint64_t act_addr,
            uint64_t *tags, uint64_t *addr, uint64_t cycle);
        public:
        HistoryTable()
        {
            history_pointers = new history_table*[sets];
            historyt = new history_table*[sets];
    
            for (int i = 0; i < sets; i++) historyt[i] = new history_table[ways];
            for (int i = 0; i < sets; i++) history_pointers[i] = historyt[i];
        }
    
        ~HistoryTable()
        {
            for (int i = 0; i < sets; i++) delete historyt[i];
            delete historyt;
    
            delete history_pointers;
        }
    
        int get_ways();
        void add(uint64_t tag, uint64_t addr, uint64_t cycle);
        uint16_t get(uint32_t latency, uint64_t tag, uint64_t act_addr, 
            uint64_t *tags, uint64_t *addr, uint64_t cycle);
    };
    
    /* berti_llc_filter Table */
    private:
    struct berti_llc_filter_table {
        std::array<delta_t, berti_llc_filter_TABLE_DELTA_SIZE> deltas;
        uint64_t conf = 0;
        uint64_t total_used = 0;
    };

    struct RAF {
        constexpr static std::size_t RAF_FILTER_SETS = 4;
        constexpr static std::size_t RAF_FILTER_WAYS = 16;
        constexpr static std::size_t RAF_TIMEOUT = 200;
        struct raf_entry {
            champsim::block_number block;
            uint64_t first_accessed;

            raf_entry() : raf_entry(champsim::block_number{0},0) {}
            explicit raf_entry(champsim::block_number block_, uint64_t first_accessed_) : block(block_), first_accessed(first_accessed_) {}
        };
        struct raf_indexer {
            auto operator()(const raf_entry& entry) const {return entry.block;}
        };
        champsim::msl::lru_table<raf_entry, raf_indexer, raf_indexer> raf_filter{RAF_FILTER_SETS,RAF_FILTER_WAYS};
        bool check(champsim::address block, uint64_t check_time, bool update_table) {
            auto raf_filter_entry = raf_filter.check_hit(raf_entry{champsim::block_number{block},0});
            bool should_drop = false;
            if(raf_filter_entry.has_value()) {
                if(check_time - raf_filter_entry->first_accessed < RAF_TIMEOUT)
                    should_drop = true;
            }
            if(update_table)
                raf_filter.fill(raf_entry{champsim::block_number{block},check_time});
            return should_drop;
        }
        void invalidate(champsim::address block) {
            raf_filter.invalidate(raf_entry{champsim::block_number{block},0});
        }
        
    };

    RAF filter_raf;
    
    std::map<uint64_t, berti_llc_filter_table*> berti_llc_filtert;
    std::queue<uint64_t> berti_llc_filtert_queue;
        
    uint64_t size = 0;

    bool static compare_greater_delta(delta_t a, delta_t b);
    bool static compare_rpl(delta_t a, delta_t b);

    void increase_conf_tag(uint64_t tag);
    void conf_tag(uint64_t tag);
    void add(uint64_t tag, int64_t delta);

    public:
    //berti_llc_filter(uint64_t p_size) : size(p_size) {};
    void find_and_update(uint64_t latency, uint64_t tag, uint64_t cycle, 
        uint64_t line_addr);
    uint8_t get(uint64_t tag, std::vector<delta_t> &res);
    uint64_t ip_hash(uint64_t ip);
    
    uint64_t me = 0;
    static uint64_t others;
    // This is structure is an adaption of berti_llc_filter for multicore simulations
    static std::vector<LatencyTable*> latencyt;
    static std::vector<ShadowCache*> scache;
    static std::vector<HistoryTable*> historyt;

    using prefetcher::prefetcher;
    uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                      uint32_t metadata_in);
    uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);

    void prefetcher_initialize();
    void prefetcher_cycle_operate();
    void prefetcher_final_stats();
};
#endif