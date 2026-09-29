#ifndef REPLACEMENT_DRRIP_PPN_H
#define REPLACEMENT_DRRIP_PPN_H

#include <cstdint>
#include <vector>

#include "cache.h"
#include "modules.h"
#include "msl/stat_methods.h"

/**
 * Prefetch-pressure-normalized DRRIP for a shared cache.
 *
 * Thread-aware DRRIP, plus: each core's fair share of the cache is proportional to its demand fills (what it
 * would bring in without prefetching), so cores whose prefetchers fill aggressively cannot starve cores that
 * prefetch little. Prefetch fills from a core over its share insert at the eviction end, and prefetch hits
 * refresh a line without protecting it. Occupancy is measured in a sample of sets only, which record the core
 * that filled each of their lines; shares are expressed in those sampled lines.
 */
struct drrip_ppn : public champsim::modules::replacement {
  static constexpr unsigned maxRRPV = 3;
  static constexpr unsigned BRRIP_MAX = 32;
  static constexpr unsigned PSEL_WIDTH = 10;

  long NUM_SET, NUM_WAY;
  std::size_t num_cpus;
  bool insert_over_quota;   // ppn_insert: prefetch fills from an over-share core insert at maxRRPV
  bool no_prefetch_promote; // ppn_no_pf_promote: a prefetch hit refreshes to maxRRPV-1 instead of 0
  uint64_t epoch_fills;     // ppn_epoch: fills between share recomputations
  double slack;             // ppn_slack: a core is over its share when occupancy > share * slack
  long sample_stride;       // ppn_sample_sets: occupancy is sampled in one set of every NUM_SET / ppn_sample_sets
  uint64_t sampled_lines;

  unsigned brrip_counter = 0;
  std::vector<unsigned> rrpv;
  std::vector<int> owner; // core that filled each line of the sampled sets, -1 = never filled
  std::vector<champsim::msl::dscounter<long, PSEL_WIDTH>> PSEL;

  std::vector<uint64_t> occupancy, share, demand_fills, demand_ema; // occupancy and share in sampled lines
  uint64_t fills_in_epoch = 0;
  std::vector<uint64_t> stat_demand_fills, stat_prefetch_fills, stat_pf_insert_low;

  explicit drrip_ppn(champsim::modules::ModuleBuilder builder);

  void initialize_replacement() override;
  long find_victim(champsim::origin origin, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type) override;
  void replacement_cache_fill(champsim::origin origin, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                              access_type type) override;
  void update_replacement_state(champsim::origin origin, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                                access_type type, bool hit) override;
  void replacement_final_stats() override;

private:
  std::size_t idx(long set, long way) const { return static_cast<std::size_t>(set * NUM_WAY + way); }
  bool sampled(long set) const { return set % sample_stride == sample_stride / 2; }
  std::size_t sample_idx(long set, long way) const { return static_cast<std::size_t>((set / sample_stride) * NUM_WAY + way); }
  bool over_share(std::size_t cpu) const { return static_cast<double>(occupancy[cpu]) > slack * static_cast<double>(share[cpu]); }
  void end_epoch();
};

#endif
