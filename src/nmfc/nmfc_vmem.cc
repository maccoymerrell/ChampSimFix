/*
 * NMFC_VMEM — the congruent, mode-stamping page allocator.
 *
 * Three things distinguish it from the stock virtual memory:
 *
 * 1. Congruence. A virtual grain wants the tile its own address names, and the
 *    allocator hands back a frame from exactly that tile's free list. That is
 *    the promise the whole routing story rests on: a function core decides
 *    local-vs-migrate on the virtual address, before translating, and the frame
 *    is guaranteed to land where the VA said it would.
 *
 * 2. The mapping mode is stamped into the physical address at allocation and
 *    never changes. It is one bit above the top of the DRAM range, so every
 *    consumer downstream -- the interleave fabric, the LLC slices, the memory
 *    controller -- reads it out of the address rather than consulting a table.
 *    Caches tag by address, so it survives a writeback for free, which a
 *    PTE-carried bit would not.
 *
 * 3. Allocation is grain-granular for the NMFC region and 4 KiB-granular for
 *    the standard one, but both draw from the same pool of grains. A grain is
 *    committed to one mode or the other when it is first handed out; changing a
 *    live allocation's mode is not a mapping operation at all, it is an
 *    ordinary page migration, and a freed grain carries no obligation because
 *    its contents are garbage.
 *
 * What remains is the familiar huge-page problem: an NMFC grain needs a free
 * grain on one specific tile, and a lopsided free list can fail while total
 * capacity is ample. That failure spills to another tile, costing one extra
 * fabric hop and nothing else, and the spill rate is what says siloing went too
 * far.
 *
 * Parameters (in addition to the stock vmem set):
 *   dram                    @memory_controller, for sizing
 *   minor_fault_penalty     time
 *   page_table_levels, page_table_page_size
 *   default_region          "standard" | "nmfc" -- where unhinted pages go
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <fmt/core.h>

#include "champsim.h"
#include "modules.h"
#include "nmfc/tile_router.h"
#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_vmem.h"
#include "stat_report.h"
#include "util/bits.h"

namespace
{

using pte_entry = champsim::data::size<long long, std::ratio<8>>;

class nmfc_vmem : public champsim::modules::vmem_module, public nmfc::page_placement_sink, public champsim::module_lifecycle
{
public:
  explicit nmfc_vmem(champsim::modules::ModuleBuilder builder)
      : map_(nmfc::tile_map_from(builder)), router_(builder.get_parameter<nmfc::tile_router_module*>("router")),
        dram_(builder.get_parameter<champsim::modules::memory_controller_module*>("dram")),
        minor_fault_penalty_(builder.get_parameter<champsim::chrono::clock::duration>("minor_fault_penalty")),
        pt_levels_(builder.get_parameter<std::size_t>("page_table_levels")),
        pte_page_size_(builder.get_parameter<champsim::data::bytes>("page_table_page_size")),
        page_size_(builder.get_parameter<unsigned>("page_size", true, 4096U)),
        log2_page_size_(builder.get_parameter<unsigned>("log2_page_size", true, 12U)),
        default_nmfc_(builder.get_parameter<std::string>("default_region", true, std::string{"standard"}) == "nmfc")
    {
    router_->attach_placement(this);
    if (map_.grain_bits() < log2_page_size_) {
      fmt::print("[NMFC_VMEM] ERROR: grain ({} bits) is smaller than a page ({} bits)\n", map_.grain_bits(), log2_page_size_);
      std::exit(-1);
    }
    pages_per_grain_ = std::uint64_t{1} << (map_.grain_bits() - log2_page_size_);
    populate();
  }

  // ---- vmem_module ----

  [[nodiscard]] std::size_t get_pt_levels() const override { return pt_levels_; }

  [[nodiscard]] champsim::data::bits shamt(std::size_t level) const override { return extent(level).lower; }

  // Taken on the compacted address, matching how get_pte_pa keys the table:
  // the walker must index with the same address the table was built over.
  [[nodiscard]] uint64_t get_offset(champsim::address vaddr, std::size_t level) const override
  {
    return champsim::address_slice{extent(level), champsim::address{map_.compact_virtual(vaddr.to<std::uint64_t>())}}.to<uint64_t>();
  }

  [[nodiscard]] std::size_t available_ppages() const override
  {
    std::size_t total = standard_frames_.size();
    for (const auto& tile_list : free_grains_) {
      total += tile_list.size() * pages_per_grain_;
    }
    return total;
  }

  std::pair<champsim::page_number, champsim::chrono::clock::duration> va_to_pa(champsim::origin origin, champsim::page_number vaddr) override
  {
    const auto asid = origin.asid();
    const auto vpage = vaddr.to<std::uint64_t>();
    const auto vgrain = vpage / pages_per_grain_;

    const auto hint = hint_for(asid, vgrain);
    if (hint.mode == nmfc::mapping_mode::NMFC) {
      return translate_nmfc(asid, vpage, vgrain, hint);
    }
    return translate_standard(asid, vpage);
  }

  std::pair<champsim::address, champsim::chrono::clock::duration> get_pte_pa(champsim::origin origin, champsim::page_number vaddr, std::size_t level) override
  {
    // N roots, one per channel, over the compacted address space.
    //
    // Keying on the raw virtual address would put a level-1 page over 512
    // consecutive pages spanning every tile, so it could live on only one of
    // them and every other tile's walk through it would be remote. Compacting
    // the tile field out first means channel t's table covers exactly channel
    // t's addresses, so its pages can live on channel t and every walk is
    // local. This is a partition, not a replication: the total page-table size
    // is unchanged.
    const auto raw = champsim::address{vaddr}.to<std::uint64_t>();
    const auto roots = router_->page_table_roots();
    const auto tile_override = pte_tile_override_;
    // With one root per channel the tile is known before the walk starts, so
    // the table is partitioned and every walk is local. With a single root it
    // cannot be: picking the root would require the answer the walk produces.
    // A replicated grain lives in every partition, so its entry is found
    // through the asking tile's root rather than one derived from the address.
    const auto tile = roots <= 1                    ? std::size_t{0}
                      : tile_override.has_value()   ? *tile_override
                                                    : router_->owner_of(origin, champsim::address{raw});
    const champsim::address compacted{roots > 1 ? map_.compact_virtual(raw) : raw};

    const champsim::dynamic_extent entry_extent{champsim::address::bits, shamt(level + 1)};
    const auto key = std::tuple{origin.asid(), static_cast<std::uint32_t>(level * roots + tile), champsim::address_slice{entry_extent, compacted}};

    auto it = page_table_.find(key);
    bool fault = false;
    if (it == std::end(page_table_)) {
      // The key says which root; this says which tile stores the page. With one
      // root those are different questions, and putting every page-table page
      // on tile 0 would make the table a hotspot the fabric then has to carry.
      const auto pt_tile = roots > 1 ? tile : map_.tile_of_virtual(raw);
      const auto frame = allocate_standard_frame_on(pt_tile);
      // Stamped NMFC: the whole point of placing a page-table page on the tile
      // that owns the addresses it describes is that walks stay local, and
      // tile_of() only reads grain bits when the mode says NMFC.
      it = page_table_.emplace(key, champsim::address{stamp(frame, nmfc::mapping_mode::NMFC)}).first;
      fault = true;
      ++pt_pages_;
    }

    const auto offset = get_offset(compacted, level);
    const champsim::dynamic_extent pte_extent{champsim::data::bits{champsim::lg2(pte_entry::byte_multiple)},
                                              static_cast<std::size_t>(champsim::lg2(pte_page_size_.count()))};
    const champsim::address paddr{champsim::splice(champsim::page_number{it->second}, champsim::address_slice{pte_extent, offset})};

    return {paddr, fault ? minor_fault_penalty_ : champsim::chrono::clock::duration::zero()};
  }

  // ---- page_placement_sink ----

  void hint_placement(std::uint32_t asid, std::uint64_t vpage, nmfc::placement_hint hint) override
  {
    const auto vgrain = vpage / pages_per_grain_;
    hints_[std::pair{asid, vgrain}] = hint;
    if (hint.replicated) {
      replicated_grains_.insert(std::pair{asid, vgrain});
    }
    ++hints_received_;
  }

  bool remap_grain(std::uint32_t asid, std::uint64_t vgrain, std::size_t tile) override
  {
    // Moving a page is only meaningful where the tile is a property of the
    // *frame*. Under congruent routing the virtual address names the tile, so
    // relocating the frame would leave the address pointing at one tile and the
    // data on another -- the precise failure a tile port catches, manufactured
    // deliberately. Refuse it here, where the reason is legible.
    if (router_->order() == nmfc::routing_order::VIRTUAL_FIRST) {
      return false;
    }
    const auto key = std::pair{asid, vgrain};
    if (replicated_grains_.count(key) != 0) {
      return false; // every tile already has a copy; there is nothing to move
    }
    auto it = nmfc_grains_.find(key);
    if (it == std::end(nmfc_grains_) || map_.tile_of((it->second << map_.grain_bits()) | (std::uint64_t{1} << map_.mode_bit())) == tile) {
      return false;
    }
    if (free_grains_[tile].empty()) {
      return false; // no room there; the policy asked for something impossible
    }
    const auto old_frame = it->second;
    it->second = take_grain(tile);
    free_grains_[old_frame % map_.num_tiles()].push_back(old_frame);
    ++remaps_;
    ++generation_;
    remap_log_.emplace_back(asid, vgrain);
    return true;
  }

  [[nodiscard]] std::uint64_t mapping_generation() const override { return generation_; }

  [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint64_t>>& remap_log() const override { return remap_log_; }

  [[nodiscard]] std::optional<std::uint64_t> grain_mapping(std::uint32_t asid, std::uint64_t vaddr) const override
  {
    return grain_mapping_on(asid, vaddr, 0);
  }

  [[nodiscard]] std::optional<std::uint64_t> grain_mapping_on(std::uint32_t asid, std::uint64_t vaddr, std::size_t tile) const override
  {
    const auto vgrain = vaddr >> map_.grain_bits();
    if (auto rep = replicas_.find(std::pair{asid, vgrain}); rep != std::end(replicas_)) {
      const auto base = (rep->second << map_.grain_bits()) | (std::uint64_t{1} << map_.mode_bit());
      return map_.expand(map_.compact(base), tile % map_.num_tiles());
    }
    auto it = nmfc_grains_.find(std::pair{asid, vgrain});
    if (it == std::end(nmfc_grains_)) {
      return std::nullopt;
    }
    // Stamped, like every other physical address this allocator hands out: an
    // MMU entry built from this goes on to be routed and sliced by consumers
    // that read the mapping mode out of the address itself.
    return (it->second << map_.grain_bits()) | (std::uint64_t{1} << map_.mode_bit());
  }

  /** Page-granular translation as the MMU on `tile` sees it. */
  /** get_pte_pa, but answered as `tile` -- see pte_address_on in the header. */
  [[nodiscard]] std::pair<champsim::address, champsim::chrono::clock::duration> pte_address_on(champsim::origin origin, std::uint64_t vpage, std::size_t level,
                                                                                                std::size_t tile) override
  {
    const auto vgrain = vpage / pages_per_grain_;
    const bool replicated = replicated_grains_.count(std::pair{origin.asid(), vgrain}) != 0;
    if (replicated) {
      pte_tile_override_ = tile;
    }
    auto result = get_pte_pa(origin, champsim::page_number{champsim::address{vpage << log2_page_size_}}, level);
    pte_tile_override_.reset();
    return result;
  }

  [[nodiscard]] std::uint64_t page_mapping_on(champsim::origin origin, std::uint64_t vpage, std::size_t tile) override
  {
    const auto asid = origin.asid();
    const auto vgrain = vpage / pages_per_grain_;
    if (replicated_grains_.count(std::pair{asid, vgrain}) != 0) {
      const auto grain = replica_for(asid, vgrain, tile);
      return champsim::address{stamp(grain * pages_per_grain_ + (vpage % pages_per_grain_), nmfc::mapping_mode::NMFC)}.to<std::uint64_t>();
    }
    const auto va = champsim::address{vpage << log2_page_size_};
    const auto pa = champsim::address{va_to_pa(origin, champsim::page_number{va}).first}.to<std::uint64_t>();

    // Congruence, checked where it is established. A frame whose tile does not
    // match the tile its own virtual address names sends the context to one
    // place and its data to another, and the symptom -- a tile port refusing a
    // foreign address -- surfaces far from this line.
    // NMFC-mode pages only. A STANDARD page is block-interleaved across every
    // channel on purpose -- that is what the mode means -- so its tile is not
    // its address's grain field and never was. Asserting congruence over both
    // regions confuses "the layout I chose" with "the invariant I rely on".
    if (router_->order() == nmfc::routing_order::VIRTUAL_FIRST && map_.is_nmfc(pa)) {
      const auto want = router_->owner_of(origin, va);
      if (const auto got = map_.tile_of(pa); got != want) {
        const auto vgrain = vpage / pages_per_grain_;
        const auto found = hints_.find(std::pair{origin.asid(), vgrain});
        fmt::print("[NMFC_VMEM] ERROR: virtual page {:#x} names tile {} but was backed by frame {:#x} on tile {} (mode {}).\n"
                   "  asid {} vgrain {}; {} hints held; hint for this grain: {}\n",
                   vpage << log2_page_size_, want, pa, got, map_.is_nmfc(pa) ? "nmfc" : "standard", origin.asid(), vgrain, hints_.size(),
                   found == std::end(hints_) ? std::string{"none"}
                                             : fmt::format("tile {} mode {}", found->second.tile, found->second.mode == nmfc::mapping_mode::NMFC ? "nmfc" : "standard"));
        std::exit(-1);
      }
    }
    return pa;
  }

  /**
   * This grain's copy on `tile`, allocating the whole set on first touch.
   *
   * All N copies are made together: a replicated grain that existed on only
   * some channels would silently turn "choose a copy" back into "choose among
   * the tiles that happen to have one", which is the compile-time layout this
   * exists to replace.
   */
  std::uint64_t replica_for(std::uint32_t asid, std::uint64_t vgrain, std::size_t tile)
  {
    auto it = replicas_.find(std::pair{asid, vgrain});
    if (it == std::end(replicas_)) {
      const auto base = take_congruent_set();
      replica_allocs_ += map_.num_tiles();
      it = replicas_.emplace(std::pair{asid, vgrain}, base).first;
    }
    // The whole point of reserving a congruent set: copy t is the base with the
    // tile-select field set to t, so converting a base physical address to a
    // tile-specific one is expand(compact(pa), t) and needs no per-tile table.
    const auto frame = it->second + (tile % map_.num_tiles());
    return frame;
  }

  // ---- statistics ----

  void begin_phase(bool /*warmup*/) override
  {
    nmfc_allocs_ = standard_allocs_ = spills_ = pt_pages_ = 0;
    // hints_received_ is deliberately not reset. Placement hints all arrive in
    // the producer's first read, before the first instruction retires, so a
    // per-phase count reports zero for every phase that matters -- a statistic
    // that is always zero says nothing about whether placement was applied.
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (nmfc_allocs_ == 0 && standard_allocs_ == 0) {
      return;
    }
    std::vector<std::size_t> free_by_tile;
    free_by_tile.reserve(free_grains_.size());
    for (const auto& tile_list : free_grains_) {
      free_by_tile.push_back(tile_list.size());
    }
    const auto imbalance = free_by_tile.empty() ? 0.0
                                                : static_cast<double>(*std::max_element(std::begin(free_by_tile), std::end(free_by_tile))
                                                                      - *std::min_element(std::begin(free_by_tile), std::end(free_by_tile)));

    out.line(fmt::format("NMFC_VMEM ALLOCATIONS nmfc: {} standard: {} pt_pages: {} HINTS APPLIED (cumulative): {}", nmfc_allocs_, standard_allocs_,
                         pt_pages_, hints_received_));
    out.line(fmt::format("NMFC_VMEM SPILLS: {} FREE GRAIN IMBALANCE: {:.0f} REMAPS: {}", spills_, imbalance, remaps_));

    auto json = out.json();
    json.add("nmfc_allocations", nmfc_allocs_);
    json.add("standard_allocations", standard_allocs_);
    json.add("page_table_pages", pt_pages_);
    json.add("hints_received", hints_received_);
    json.add("spills", spills_);
    json.add("free_grains_by_tile", free_by_tile);
  }

private:
  [[nodiscard]] champsim::dynamic_extent extent(std::size_t level) const
  {
    const champsim::data::bits lower{log2_page_size_ + champsim::lg2(pte_page_size_.count()) * (level - 1)};
    return champsim::dynamic_extent{lower, static_cast<std::size_t>(champsim::lg2(pte_page_size_.count()))};
  }

  [[nodiscard]] nmfc::placement_hint hint_for(std::uint32_t asid, std::uint64_t vgrain) const
  {
    if (auto it = hints_.find(std::pair{asid, vgrain}); it != std::end(hints_)) {
      return it->second;
    }
    return nmfc::placement_hint{default_nmfc_ ? nmfc::mapping_mode::NMFC : nmfc::mapping_mode::STANDARD,
                                static_cast<std::uint32_t>(vgrain % map_.num_tiles())};
  }

  /** Grain g lives on tile g % num_tiles under the NMFC layout. */
  void populate()
  {
    const auto total_grains = static_cast<std::uint64_t>(dram_->size().count()) >> map_.grain_bits();
    free_grains_.assign(map_.num_tiles(), {});
    // Leave the lowest grain unallocated: address 0 is a useful sentinel, and
    // the stock vmem reserves the bottom of memory for the same reason.
    const auto n = map_.num_tiles();
    total_sets_ = total_grains / n;
    for (std::uint64_t grain = 1; grain < total_grains; ++grain) {
      free_grains_[grain % n].push_back(grain);
    }
    if (total_grains <= 1) {
      fmt::print("[NMFC_VMEM] ERROR: DRAM holds {} grains of {} bytes; nothing to allocate\n", total_grains, map_.grain());
      std::exit(-1);
    }
  }

  /** Take a grain owned by `tile`, spilling to another tile if that list is dry. */
  /**
   * One free grain on every channel, sharing a compacted index.
   *
   * Grain g lives on tile g % N, so the set {kN .. kN+N-1} is exactly that --
   * the copies then differ only in the tile-select field, which is what lets
   * expand(compact(pa), t) name any copy without a table. Searched from the
   * top, where allocation has not yet reached, so taking one costs nothing that
   * ordinary allocation wanted.
   */
  std::uint64_t take_congruent_set()
  {
    const auto n = map_.num_tiles();
    for (std::uint64_t k = total_sets_; k-- > 1;) {
      bool whole_set_free = true;
      for (std::size_t t = 0; t < n && whole_set_free; ++t) {
        const auto& list = free_grains_[t];
        whole_set_free = std::find(std::begin(list), std::end(list), k * n + t) != std::end(list);
      }
      if (!whole_set_free) {
        continue;
      }
      for (std::size_t t = 0; t < n; ++t) {
        auto& list = free_grains_[t];
        list.erase(std::find(std::begin(list), std::end(list), k * n + t));
      }
      return k * n;
    }
    fmt::print("[NMFC_VMEM] ERROR: no congruent frame set left for a replicated page.\n");
    std::exit(-1);
  }

  std::uint64_t take_grain(std::size_t tile)
  {
    if (!free_grains_[tile].empty()) {
      const auto grain = free_grains_[tile].front();
      free_grains_[tile].pop_front();
      return grain;
    }
    // Spill: correctness is unaffected, the access simply takes one extra hop,
    // and the rate at which this happens is the evidence that siloing overran
    // a channel's capacity.
    for (std::size_t offset = 1; offset < free_grains_.size(); ++offset) {
      auto& other = free_grains_[(tile + offset) % free_grains_.size()];
      if (!other.empty()) {
        const auto grain = other.front();
        other.pop_front();
        ++spills_;
        return grain;
      }
    }
    fmt::print("[NMFC_VMEM] ERROR: out of physical memory ({} grains exhausted)\n", map_.num_tiles());
    std::exit(-1);
  }

  std::pair<champsim::page_number, champsim::chrono::clock::duration> translate_nmfc(std::uint32_t asid, std::uint64_t vpage, std::uint64_t vgrain,
                                                                                     nmfc::placement_hint hint)
  {
    const auto key = std::pair{asid, vgrain};
    if (replicated_grains_.count(key) != 0) {
      // Replicated: tile 0's copy is the canonical answer for a caller that did
      // not say which tile is asking. Every other copy is one expand() away.
      const bool first = replicas_.count(key) == 0;
      const auto base = replica_for(asid, vgrain, 0);
      const auto ppage = base * pages_per_grain_ + (vpage % pages_per_grain_);
      return {stamp(ppage, nmfc::mapping_mode::NMFC), first ? minor_fault_penalty_ : champsim::chrono::clock::duration::zero()};
    }

    auto it = nmfc_grains_.find(key);
    bool fault = false;
    if (it == std::end(nmfc_grains_)) {
      // Placement is the router's call, not the hint's. Under a congruent
      // router the answer is forced by the virtual address and a hint that
      // disagreed would put the data on one tile and send the invocation to
      // another; under a physical router the hint is only a starting
      // suggestion, and the router owns the balance decision.
      const champsim::address va{vpage << log2_page_size_};
      const auto tile = router_->placement_for(champsim::origin{asid, 0}, va);
      (void)hint;
      it = nmfc_grains_.emplace(key, take_grain(tile)).first;
      fault = true;
      ++nmfc_allocs_;
    }

    // Within the grain, keep the page's own offset so a contiguous virtual
    // range stays contiguous physically -- which is what lets one MMU entry
    // cover the whole grain.
    const auto ppage = it->second * pages_per_grain_ + (vpage % pages_per_grain_);
    return {stamp(ppage, nmfc::mapping_mode::NMFC), fault ? minor_fault_penalty_ : champsim::chrono::clock::duration::zero()};
  }

  std::pair<champsim::page_number, champsim::chrono::clock::duration> translate_standard(std::uint32_t asid, std::uint64_t vpage)
  {
    const auto key = std::pair{asid, vpage};
    auto it = standard_pages_.find(key);
    bool fault = false;
    if (it == std::end(standard_pages_)) {
      it = standard_pages_.emplace(key, allocate_standard_frame()).first;
      fault = true;
      ++standard_allocs_;
    }
    return {stamp(it->second, nmfc::mapping_mode::STANDARD), fault ? minor_fault_penalty_ : champsim::chrono::clock::duration::zero()};
  }

  /**
   * A standard frame comes from a grain committed to the standard mode. Which
   * tile that grain came from does not matter: under the standard layout the
   * grain's blocks spread across every channel regardless.
   */
  std::uint64_t allocate_standard_frame()
  {
    if (standard_frames_.empty()) {
      refill_standard_frames(standard_refill_tile_);
      standard_refill_tile_ = (standard_refill_tile_ + 1) % free_grains_.size();
    }
    const auto frame = standard_frames_.front();
    standard_frames_.pop_front();
    return frame;
  }

  /** Same, but preferring a grain on a particular tile (used for page tables). */
  std::uint64_t allocate_standard_frame_on(std::size_t tile)
  {
    auto& pool = standard_frames_by_tile_[tile];
    if (pool.empty()) {
      const auto grain = take_grain(tile);
      for (std::uint64_t page = 0; page < pages_per_grain_; ++page) {
        pool.push_back(grain * pages_per_grain_ + page);
      }
    }
    const auto frame = pool.front();
    pool.pop_front();
    return frame;
  }

  void refill_standard_frames(std::size_t from_tile)
  {
    const auto grain = take_grain(from_tile);
    for (std::uint64_t page = 0; page < pages_per_grain_; ++page) {
      standard_frames_.push_back(grain * pages_per_grain_ + page);
    }
  }

  /** Put the mapping mode into the physical page number, where everyone downstream reads it. */
  [[nodiscard]] champsim::page_number stamp(std::uint64_t ppage, nmfc::mapping_mode mode) const
  {
    const auto mode_bit_in_page = map_.mode_bit() - log2_page_size_;
    const auto value = mode == nmfc::mapping_mode::NMFC ? (ppage | (std::uint64_t{1} << mode_bit_in_page)) : ppage;
    return champsim::page_number{value};
  }

  nmfc::tile_map map_;
  nmfc::tile_router_module* router_;
  champsim::modules::memory_controller_module* dram_;
  champsim::chrono::clock::duration minor_fault_penalty_;
  std::size_t pt_levels_;
  // In PTE units, not bytes: the level shift is lg2(entries per page), and
  // reading it in bytes puts level 5 at bit 72 of a 64-bit address.
  pte_entry pte_page_size_;
  unsigned page_size_;
  unsigned log2_page_size_;
  bool default_nmfc_;
  std::uint64_t pages_per_grain_ = 1;

  std::vector<std::deque<std::uint64_t>> free_grains_;
  std::deque<std::uint64_t> standard_frames_;
  std::map<std::size_t, std::deque<std::uint64_t>> standard_frames_by_tile_;
  std::size_t standard_refill_tile_ = 0;

  std::map<std::pair<std::uint32_t, std::uint64_t>, std::uint64_t> nmfc_grains_;
  /**
   * Replicated grains: one virtual grain, one frame per channel.
   *
   * The same virtual address therefore translates differently depending on
   * which tile asks -- which is the mechanism, not a wrinkle. Choosing a copy
   * is choosing a tile, so the OS decides placement when it answers a
   * translation, and a migrated context re-translating its unchanged program
   * counter lands on its new tile's copy.
   */
  std::map<std::pair<std::uint32_t, std::uint64_t>, std::uint64_t> replicas_;
  /**
   * Congruent frame sets held back for replication.
   *
   * Grain g lives on tile g % N, so {kN .. kN+N-1} is one frame on every
   * channel sharing a single compacted index -- which is what makes the copies
   * addressable by formula instead of by table. Reserved at populate time
   * because a set has to be contiguous in that sense, and a general free list
   * that has been picked over cannot promise one.
   */
  std::uint64_t total_sets_ = 0;
  std::uint64_t remaps_ = 0;
  std::uint64_t generation_ = 0;
  std::vector<std::pair<std::uint32_t, std::uint64_t>> remap_log_;
  std::set<std::pair<std::uint32_t, std::uint64_t>> replicated_grains_;
  std::uint64_t replica_allocs_ = 0;
  /** Set only for the duration of a replicated page's walk. */
  std::optional<std::size_t> pte_tile_override_;
  std::map<std::pair<std::uint32_t, std::uint64_t>, std::uint64_t> standard_pages_;
  std::map<std::pair<std::uint32_t, std::uint64_t>, nmfc::placement_hint> hints_;
  std::map<std::tuple<std::uint32_t, std::uint32_t, champsim::address_slice<champsim::dynamic_extent>>, champsim::address> page_table_;

  std::uint64_t nmfc_allocs_ = 0;
  std::uint64_t standard_allocs_ = 0;
  std::uint64_t spills_ = 0;
  std::uint64_t pt_pages_ = 0;
  std::uint64_t hints_received_ = 0;
};

static champsim::modules::vmem_module::register_module<nmfc_vmem> nmfc_vmem_reg("NMFC_VMEM");

} // anonymous namespace
