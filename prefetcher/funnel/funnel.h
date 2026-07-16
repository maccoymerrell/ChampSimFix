#ifndef PREFETCHER_FUNNEL_H
#define PREFETCHER_FUNNEL_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "address.h"
#include "modules.h"
#include "msl/lru_table.h"

// Funnel: a 2D-access-map perceptron L2 prefetcher.
//
// For each access it reads a conv window of +/-R virtual pages x blocks-per-page around the
// anchor page (the recent access map), runs a weight-shared integer perceptron, and issues a
// prefetch for each relative-delta output cell scoring above theta. Trained ONLINE, positive-
// only (a real prefetcher can't store per-prediction state for delayed negatives): when a later
// access confirms an anchor's predicted cell, a BPR pairwise update (+pstep on the confirmed
// cell, -pstep on a sampled unconfirmed cell, under a |score|<theta_train surprise gate) nudges
// the shared weights. Runs in VIRTUAL address space (see replay_channel use_virtual_address) so
// the cross-page window reflects the program's virtual adjacency.
class funnel : public champsim::modules::prefetcher
{
public:
  using block_in_page = champsim::address_slice<champsim::dynamic_extent>;

  static constexpr std::size_t REGION_SETS = 128;
  static constexpr std::size_t REGION_WAYS = 8; // 1024 tracked pages (access map)
  static constexpr int W_CAP = 2048;

  struct region_type {
    champsim::page_number vpn;
    std::vector<bool> access_map{};

    region_type() = default;
    region_type(champsim::page_number allocate_vpn, std::size_t bpp) : vpn(allocate_vpn), access_map(bpp) {}
  };
  struct region_indexer {
    auto operator()(const region_type& e) const { return e.vpn; }
  };
  champsim::msl::lru_table<region_type, region_indexer, region_indexer> regions{REGION_SETS, REGION_WAYS};

  champsim::modules::cache_module* cache_ = nullptr;

  funnel(champsim::modules::ModuleBuilder builder);

  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in) override;
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in) override;

  void prefetcher_initialize() override {}
  void prefetcher_cycle_operate() override {}
  void prefetcher_final_stats() override {}
  void prefetcher_branch_operate(champsim::address /*ip*/, uint8_t /*branch_type*/, champsim::address /*branch_target*/) override {}

private:
  template <typename T>
  std::pair<champsim::page_number, block_in_page> page_and_offset(T addr) const
  {
    return {champsim::page_number{addr}, block_in_page{block_in_page_extent_, addr}};
  }
  region_type make_region(champsim::page_number vpn) const { return region_type{vpn, static_cast<std::size_t>(bpp_)}; }
  void mark_access(champsim::address addr);
  void build_input(champsim::page_number anchor_pn, std::vector<int>& out);
  long col_score(const int* ain, int len, int o) const;
  void add_col(const int* ain, int len, int o, long delta);
  int draw_negative(const uint64_t* confirmed, int exclude);

  // config
  int radius_;
  long theta_;       // firing threshold
  long theta_train_; // surprise-gate margin
  long pstep_;       // BPR step
  uint32_t window_;  // training ring window (accesses)
  unsigned page_size_, block_size_;
  champsim::dynamic_extent block_in_page_extent_;

  // derived geometry
  int bpp_ = 0;   // blocks per page
  int win_ = 0;   // 2*radius+1
  int nin_ = 0;   // win_ * bpp_
  int nout_ = 0;  // == nin_
  int nmask_ = 0; // (nout_+63)/64

  // perceptron (shared conv weights)
  std::vector<int16_t> W_; // nin_ * nout_
  std::vector<long> score_;
  std::vector<int> active_in_; // scratch: current anchor's active input cells

  // online training ring of pending anchors (SoA circular buffer + input arena)
  std::vector<uint64_t> r_page_, r_time_;
  std::vector<uint8_t> r_off_;   // anchor's own block offset (to skip self-confirm)
  std::vector<int32_t> r_ain_;   // RC * nin_
  std::vector<uint16_t> r_len_;
  std::vector<uint64_t> r_conf_; // RC * nmask_
  uint64_t rc_ = 0, r_head_ = 0, r_count_ = 0;

  uint64_t time_ = 0;
  uint32_t lfsr_ = 0x1234567u;
};

#endif
