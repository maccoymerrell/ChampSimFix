//Title: Feedback Mechanisms for Improving Probabilistic Memory Prefetching
//Authors: Ibrahim Hur, Calvin Lin
//Institutions: IBM Corporation, The University of Texas at Austin
//Publisher: IEEE, HPCA
//Date: 02/14/2009

//I. Hur and C. Lin, "Feedback mechanisms for improving probabilistic memory prefetching," 2009 IEEE 15th International Symposium on High Performance Computer Architecture, Raleigh, NC, USA, 2009, pp. 443-454, doi: 10.1109/HPCA.2009.4798282.
//keywords: {Feedback;Prefetching;Variable speed drives;Random access memory;Energy consumption;Histograms;Timing;Computer applications},


#ifndef PREFETCHER_ASD_COMBO_H
#define PREFETCHER_ASD_COMBO_H

#include <cstdint>

#include "address.h"
#include "modules.h"
#include "cache.h"
#include "dram_controller.h"
#include <map>
#include <vector>
#include <array>
#include <utility>

#include "../asd/asd.h"
#include "../asd_col/asd_col.h"

struct asd_combo : public champsim::modules::prefetcher {

  constexpr static uint32_t ASD_ID = 13;
  std::vector<std::size_t> column_bits;

  champsim::address compose_base_and_column(champsim::address base, uint64_t column) {
    //1. iterate through all column bits in the base
    //2. set each bit to the matching bits in the column
    uint64_t base_temp = base.to<uint64_t>();
    for(std::size_t i = 0; i < column_bits.size(); i++) {
      if(column & (1ull << i))
        base_temp |= 1ull << column_bits[i];
      else
        base_temp &= ~(1ull << column_bits[i]);
    }
    return champsim::address{base_temp};
  }

  std::size_t num_bins;
  std::vector<asd::ASD_Module> ASD_Modules;
  std::vector<asd_col::ASD_COL_Module> ASD_COL_Modules;

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  void prefetcher_initialize();
  // void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) {}
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();
};

#endif
