/*
 * NMFC_MMU — mixed-page-size translation for a memory tile, as a `channel` model.
 *
 * One module with two faces, which is why it exists at all:
 *
 *   As a channel, a stock CACHE points at it via `lower_translate` exactly as
 *   it points at a TLB channel today, and gets back a response carrying the
 *   physical page. Nothing above it changes.
 *
 *   As a translation_engine, a function core asks it directly, keyed by context
 *   slot, and drains completions.
 *
 * Building it this way avoids two forks. DEFAULT_PTW cannot terminate a walk
 * early -- handle_fill unconditionally decrements the level and the last step is
 * level <= 0 -- so it cannot express a huge page, which is exactly what a leaf
 * above the bottom level is. And a TLB is a CACHE with fixed offset_bits, so one
 * array cannot hold both page sizes; real hardware probes two in parallel for
 * that reason, and so does this.
 *
 * WHY TWO SIZES MATTER HERE. Graph working sets are far past any real TLB's
 * reach, so the regime is mostly-miss whatever we do. Grain-sized pages do not
 * change that -- a 1024-entry array at 2 MiB reaches 2 GiB, still a couple of
 * percent of a 100 GB graph -- they move the constant. The small array is what
 * keeps the model honest: real systems still need 4 KiB pages, so a machine that
 * quietly made everything huge would flatter itself.
 *
 * Parameters:
 *   clock_period       time
 *   vmem               @vmem, which owns the mappings and the page table shape
 *   lower_level        @channel for walk references -- for a memory tile that is
 *                      its TILE_PORT, so walks stay local and are asserted so
 *   small_sets/ways    the 4 KiB array (default 32 x 4)
 *   huge_sets/ways     the grain-sized array (default 16 x 4)
 *   hit_latency        cycles for a hit in either array (default 1)
 *   mshr_size          concurrent walks (default 16)
 *   walk_levels        references a small-page walk costs (default: the vmem's)
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>
#include <fmt/core.h>

#include "cache_stats.h"
#include "channel.h"
#include "modules.h"
#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_hooks.h"
#include "nmfc/nmfc_vmem.h"
#include "nmfc/translation_engine.h"
#include "operable.h"
#include "stat_report.h"

namespace
{

class nmfc_mmu : public champsim::modules::channel_module, public champsim::operable, public nmfc::translation_engine
{
  using channel_type = champsim::modules::channel_module;

public:
  explicit nmfc_mmu(champsim::modules::ModuleBuilder builder)
      : champsim::operable(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")), map_(nmfc::tile_map_from(builder)),
        vmem_(builder.get_parameter<champsim::modules::vmem_module*>("vmem")), lower_(builder.get_parameter<channel_type*>("lower_level", true, nullptr)),
        hit_latency_(nmfc::cycles_from(builder, "hit_latency", 1)), mshr_size_(builder.get_parameter<std::size_t>("mshr_size", true, std::size_t{16})),
        page_bits_(builder.get_parameter<unsigned>("log2_page_size", true, 12U)),
        small_(builder.get_parameter<std::size_t>("small_sets", true, std::size_t{32}), builder.get_parameter<std::size_t>("small_ways", true, std::size_t{4})),
        huge_(builder.get_parameter<std::size_t>("huge_sets", true, std::size_t{16}), builder.get_parameter<std::size_t>("huge_ways", true, std::size_t{4}))
  {
    placement_ = dynamic_cast<nmfc::page_placement_sink*>(vmem_);
    walk_levels_ = builder.get_parameter<std::size_t>("walk_levels", true, vmem_->get_pt_levels());
    returned_.set_capacity(64);
  }

  // ---- channel face: what a stock CACHE's lower_translate talks to ----

  bool add_rq(const request_type& packet) override
  {
    if (walks_.size() >= mshr_size_ && !already_walking(packet.origin.asid(), packet.v_address)) {
      return false;
    }
    auto* target = &returned_;
    return begin(packet.origin, packet.v_address, requester{target, packet, 0, false});
  }

  // A translation port carries no writes or prefetches.
  bool add_wq(const request_type& /*packet*/) override { return false; }
  bool add_pq(const request_type& /*packet*/) override { return false; }

  [[nodiscard]] std::size_t rq_occupancy() const override { return walks_.size(); }
  [[nodiscard]] std::size_t wq_occupancy() const override { return 0; }
  [[nodiscard]] std::size_t pq_occupancy() const override { return 0; }
  [[nodiscard]] std::size_t rq_size() const override { return mshr_size_; }
  [[nodiscard]] std::size_t wq_size() const override { return 0; }
  [[nodiscard]] std::size_t pq_size() const override { return 0; }

  request_queue_type& get_rq() override { return empty_; }
  request_queue_type& get_wq() override { return empty_; }
  request_queue_type& get_pq() override { return empty_; }
  response_queue_type& get_returned() override { return returned_; }
  bool has_pending() override { return !walks_.empty(); }
  stats_type& get_sim_stats() override { return sim_stats_; }

  // ---- translation_engine face: what a function core talks to ----

  bool request_translation(std::uint64_t tag, champsim::origin origin, champsim::address vaddr) override
  {
    if (walks_.size() >= mshr_size_ && !already_walking(origin.asid(), vaddr)) {
      return false;
    }
    return begin(origin, vaddr, requester{nullptr, request_type{}, tag, true});
  }

  std::vector<nmfc::translation_done>& translation_completions() override { return completions_; }
  [[nodiscard]] std::size_t translation_occupancy() const override { return walks_.size(); }

  // ---- the cycle ----

  long operate() final
  {
    long progress = 0;
    progress += collect_walk_returns();
    progress += advance_walks();
    return progress;
  }

  long poll_cycle() final
  {
    const bool idle = walks_.empty() && ready_.empty() && (lower_ == nullptr || std::empty(lower_->get_returned()));
    return idle ? 1 : 0;
  }

  void begin_phase(bool /*warmup*/) override
  {
    requests_ = small_hits_ = huge_hits_ = walks_started_ = walk_refs_ = coalesced_ = 0;
    walk_latency_sum_ = walks_finished_ = 0;
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (requests_ == 0) {
      return;
    }
    const auto hits = small_hits_ + huge_hits_;
    const auto hit_rate = 100.0 * static_cast<double>(hits) / static_cast<double>(requests_);
    const auto mean_walk = walks_finished_ == 0 ? 0.0 : static_cast<double>(walk_latency_sum_) / static_cast<double>(walks_finished_);

    out.line(fmt::format("{} TRANSLATIONS: {} HITS: {} ({:.1f}%) small: {} huge: {}", NAME, requests_, hits, hit_rate, small_hits_, huge_hits_));
    out.line(fmt::format("{} WALKS: {} COALESCED: {} MEMORY REFERENCES: {} MEAN WALK: {:.1f} cycles", NAME, walks_started_, coalesced_, walk_refs_, mean_walk));

    auto json = out.json();
    json.add("requests", requests_);
    json.add("hits", hits);
    json.add("small_hits", small_hits_);
    json.add("huge_hits", huge_hits_);
    json.add("hit_rate_percent", hit_rate);
    json.add("walks", walks_started_);
    json.add("walks_coalesced", coalesced_);
    json.add("walk_memory_references", walk_refs_);
    json.add("mean_walk_cycles", mean_walk);
  }

  void print_deadlock() final { fmt::print("[{}] walks: {} ready: {} completions: {}\n", NAME, walks_.size(), ready_.size(), completions_.size()); }

private:
  /** A small set-associative array with LRU. Deliberately small: see the header. */
  class tlb_array
  {
  public:
    struct entry {
      std::uint64_t vpn = 0;
      std::uint64_t ppn = 0;
      std::uint32_t asid = 0;
      bool valid = false;
      std::uint64_t used = 0;
    };

    tlb_array(std::size_t sets, std::size_t ways) : sets_(std::max<std::size_t>(sets, 1)), ways_(std::max<std::size_t>(ways, 1)), entries_(sets_ * ways_) {}

    [[nodiscard]] const entry* probe(std::uint32_t asid, std::uint64_t vpn)
    {
      const auto base = (vpn % sets_) * ways_;
      for (std::size_t way = 0; way < ways_; ++way) {
        auto& candidate = entries_[base + way];
        if (candidate.valid && candidate.vpn == vpn && candidate.asid == asid) {
          candidate.used = ++clock_;
          return &candidate;
        }
      }
      return nullptr;
    }

    void fill(std::uint32_t asid, std::uint64_t vpn, std::uint64_t ppn)
    {
      const auto base = (vpn % sets_) * ways_;
      auto* victim = &entries_[base];
      for (std::size_t way = 0; way < ways_; ++way) {
        auto& candidate = entries_[base + way];
        if (!candidate.valid) {
          victim = &candidate;
          break;
        }
        if (candidate.used < victim->used) {
          victim = &candidate;
        }
      }
      *victim = entry{vpn, ppn, asid, true, ++clock_};
    }

  private:
    std::size_t sets_;
    std::size_t ways_;
    std::vector<entry> entries_;
    std::uint64_t clock_ = 0;
  };

  /** Who is waiting on a translation, and how they want it delivered. */
  struct requester {
    response_queue_type* return_queue = nullptr; // channel face
    request_type original{};
    std::uint64_t tag = 0; // engine face
    bool is_engine = false;
  };

  /** One page table walk, shared by everyone who asked for the same page. */
  struct walk_state {
    champsim::origin origin{};
    std::uint64_t vaddr = 0;
    std::uint64_t vpn = 0; // at the granularity that will resolve it
    bool huge = false;
    std::size_t level = 0;              // references still owed
    bool reference_outstanding = false; // one in flight to lower_level
    std::uint64_t reference_address = 0;
    champsim::chrono::clock::time_point started{};
    std::vector<requester> waiting;
  };

  /** Whether this address resolves from a grain-sized mapping. */
  [[nodiscard]] bool is_huge(champsim::origin origin, std::uint64_t vaddr) const
  {
    return placement_ != nullptr && placement_->grain_mapping(origin.asid(), vaddr).has_value();
  }

  [[nodiscard]] std::uint64_t page_of(bool huge, std::uint64_t vaddr) const { return vaddr >> (huge ? map_.grain_bits() : page_bits_); }

  [[nodiscard]] std::uint64_t walk_key(std::uint32_t asid, bool huge, std::uint64_t vpn) const
  {
    // asid and granularity fold into the key so two address spaces, or the same
    // address at two granularities, never share a walk.
    return (static_cast<std::uint64_t>(asid) << 48) ^ (huge ? (vpn | (std::uint64_t{1} << 47)) : vpn);
  }

  [[nodiscard]] bool already_walking(std::uint32_t asid, champsim::address vaddr) const
  {
    const auto raw = vaddr.to<std::uint64_t>();
    for (bool huge : {false, true}) {
      if (walks_.count(walk_key(asid, huge, page_of(huge, raw))) != 0) {
        return true;
      }
    }
    return false;
  }

  /** Probe both arrays, and start or join a walk on a miss. */
  bool begin(champsim::origin origin, champsim::address vaddr, requester who)
  {
    ++requests_;
    const auto raw = vaddr.to<std::uint64_t>();
    const bool huge = is_huge(origin, raw);
    const auto vpn = page_of(huge, raw);

    // Both arrays are probed together, the way real hardware does, because
    // which one holds a mapping is not known until it is found.
    auto& array = huge ? huge_ : small_;
    if (const auto* hit = array.probe(origin.asid(), vpn); hit != nullptr) {
      (huge ? huge_hits_ : small_hits_)++;
      deliver(who, vpn, hit->ppn, huge, current_time + hit_latency_);
      if (nmfc::hooks::translate.active()) {
        nmfc::hooks::translate.emit(0, vpn, 1, static_cast<std::uint64_t>(hit_latency_ / clock_period));
      }
      return true;
    }

    const auto key = walk_key(origin.asid(), huge, vpn);
    if (auto it = walks_.find(key); it != std::end(walks_)) {
      it->second.waiting.push_back(who); // someone already asked; ride along
      ++coalesced_;
      return true;
    }

    walk_state walk{};
    walk.origin = origin;
    walk.vaddr = raw;
    walk.vpn = vpn;
    walk.huge = huge;
    // A huge page's leaf sits one level above the bottom, so its walk is one
    // reference shorter -- the whole reason a shallower table helps here.
    walk.level = huge ? (walk_levels_ > 1 ? walk_levels_ - 1 : 1) : walk_levels_;
    walk.started = current_time;
    walk.waiting.push_back(who);
    walks_.emplace(key, std::move(walk));
    ++walks_started_;
    return true;
  }

  /** Push each walk's next reference, or finish it. */
  long advance_walks()
  {
    long progress = 0;
    for (auto& [key, walk] : walks_) {
      if (walk.reference_outstanding) {
        continue;
      }
      if (walk.level == 0) {
        continue; // finished; retired below
      }

      // Real page-table addresses from the vmem, so the references land where
      // the page table actually is -- which for a memory tile means locally,
      // and TILE_PORT asserts it.
      const auto [pte_address, penalty] = vmem_->get_pte_pa(walk.origin, champsim::page_number{champsim::address{walk.vaddr}}, walk.level);
      (void)penalty;

      if (lower_ == nullptr) {
        // No memory below: the walk costs its references in time but issues
        // nothing. Useful for isolating the array behaviour in a test.
        --walk.level;
        ++walk_refs_;
        ++progress;
        continue;
      }

      request_type reference{};
      reference.address = pte_address;
      reference.v_address = pte_address;
      reference.is_translated = true;
      reference.type = access_type::TRANSLATION;
      reference.response_requested = true;
      reference.origin = walk.origin;

      if (!lower_->add_rq(reference)) {
        continue; // memory is busy; try again next cycle
      }
      walk.reference_outstanding = true;
      walk.reference_address = pte_address.to<std::uint64_t>();
      ++walk_refs_;
      ++progress;
    }

    progress += retire_finished_walks();
    return progress;
  }

  long collect_walk_returns()
  {
    if (lower_ == nullptr) {
      return 0;
    }
    long progress = 0;
    auto& returned = lower_->get_returned();
    for (const auto& response : returned) {
      const auto block = response.address.to<std::uint64_t>() >> map_.block_bits();
      for (auto& [key, walk] : walks_) {
        if (walk.reference_outstanding && (walk.reference_address >> map_.block_bits()) == block) {
          walk.reference_outstanding = false;
          --walk.level;
          ++progress;
        }
      }
    }
    returned.clear();
    return progress;
  }

  long retire_finished_walks()
  {
    long progress = 0;
    for (auto it = std::begin(walks_); it != std::end(walks_);) {
      auto& walk = it->second;
      if (walk.level != 0 || walk.reference_outstanding) {
        ++it;
        continue;
      }

      // The mapping itself comes from the vmem; the walk above is what it cost.
      const auto [ppage, fault] = vmem_->va_to_pa(walk.origin, champsim::page_number{champsim::address{walk.vaddr}});
      const auto physical = champsim::address{ppage}.to<std::uint64_t>();
      const auto ppn = physical >> (walk.huge ? map_.grain_bits() : page_bits_);

      (walk.huge ? huge_ : small_).fill(walk.origin.asid(), walk.vpn, ppn);

      const auto ready = current_time + fault + hit_latency_;
      walk_latency_sum_ += static_cast<std::uint64_t>((ready - walk.started) / clock_period);
      ++walks_finished_;
      if (nmfc::hooks::translate.active()) {
        nmfc::hooks::translate.emit(0, walk.vpn, 2, static_cast<std::uint64_t>((ready - walk.started) / clock_period));
      }

      for (auto& who : walk.waiting) {
        deliver(who, walk.vpn, ppn, walk.huge, ready);
        ++progress;
      }
      it = walks_.erase(it);
    }

    // Release anything whose delivery time has arrived.
    while (!ready_.empty() && ready_.front().at <= current_time) {
      auto& done = ready_.front();
      if (done.who.is_engine) {
        completions_.push_back(nmfc::translation_done{done.who.tag, done.vpn, done.ppn, done.huge});
      } else if (done.who.return_queue != nullptr) {
        // The cache's contract: v_address identifies the page, data carries the
        // physical page number.
        auto response = champsim::response{done.who.original};
        response.data = champsim::address{champsim::page_number{done.huge ? (done.ppn << (map_.grain_bits() - page_bits_)) : done.ppn}};
        done.who.return_queue->push_back_grow(response);
      }
      ready_.pop_front();
      ++progress;
    }
    return progress;
  }

  struct delivery {
    requester who;
    std::uint64_t vpn;
    std::uint64_t ppn;
    bool huge;
    champsim::chrono::clock::time_point at;
  };

  void deliver(const requester& who, std::uint64_t vpn, std::uint64_t ppn, bool huge, champsim::chrono::clock::time_point at)
  {
    ready_.push_back(delivery{who, vpn, ppn, huge, at});
  }

  nmfc::tile_map map_;
  champsim::modules::vmem_module* vmem_;
  nmfc::page_placement_sink* placement_ = nullptr;
  channel_type* lower_;
  champsim::chrono::clock::duration hit_latency_;
  std::size_t mshr_size_;
  unsigned page_bits_;
  std::size_t walk_levels_ = 5;

  tlb_array small_;
  tlb_array huge_;

  std::unordered_map<std::uint64_t, walk_state> walks_;
  std::deque<delivery> ready_;
  std::vector<nmfc::translation_done> completions_;

  response_queue_type returned_{};
  request_queue_type empty_{};
  stats_type sim_stats_{};

  std::uint64_t requests_ = 0;
  std::uint64_t small_hits_ = 0;
  std::uint64_t huge_hits_ = 0;
  std::uint64_t walks_started_ = 0;
  std::uint64_t walk_refs_ = 0;
  std::uint64_t coalesced_ = 0;
  std::uint64_t walk_latency_sum_ = 0;
  std::uint64_t walks_finished_ = 0;
};

static champsim::modules::channel_module::register_module<nmfc_mmu> nmfc_mmu_reg("NMFC_MMU");

} // anonymous namespace
