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
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <fmt/core.h>

#include "champsim.h"
#include "modules.h"
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
      : map_(nmfc::tile_map_from(builder)), dram_(builder.get_parameter<champsim::modules::memory_controller_module*>("dram")),
        minor_fault_penalty_(builder.get_parameter<champsim::chrono::clock::duration>("minor_fault_penalty")),
        pt_levels_(builder.get_parameter<std::size_t>("page_table_levels")),
        pte_page_size_(builder.get_parameter<champsim::data::bytes>("page_table_page_size")),
        page_size_(builder.get_parameter<unsigned>("page_size", true, 4096U)),
        log2_page_size_(builder.get_parameter<unsigned>("log2_page_size", true, 12U)),
        default_nmfc_(builder.get_parameter<std::string>("default_region", true, std::string{"standard"}) == "nmfc")
    {
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

  [[nodiscard]] uint64_t get_offset(champsim::address vaddr, std::size_t level) const override
  {
    return champsim::address_slice{extent(level), vaddr}.to<uint64_t>();
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
    // Page-table pages are placed congruently with the addresses they describe,
    // which is what makes a walk local: channel t's table lives on channel t.
    const champsim::dynamic_extent entry_extent{champsim::address::bits, shamt(level + 1)};
    const auto key = std::tuple{origin.asid(), static_cast<std::uint32_t>(level), champsim::address_slice{entry_extent, vaddr}};

    auto it = page_table_.find(key);
    bool fault = false;
    if (it == std::end(page_table_)) {
      const auto tile = map_.tile_of_virtual(champsim::address{vaddr}.to<std::uint64_t>());
      const auto frame = allocate_standard_frame_on(tile);
      // Stamped NMFC: the whole point of placing a page-table page on the tile
      // that owns the addresses it describes is that walks stay local, and
      // tile_of() only reads grain bits when the mode says NMFC.
      it = page_table_.emplace(key, champsim::address{stamp(frame, nmfc::mapping_mode::NMFC)}).first;
      fault = true;
      ++pt_pages_;
    }

    const auto offset = get_offset(champsim::address{vaddr}, level);
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
    ++hints_received_;
  }

  [[nodiscard]] std::optional<std::uint64_t> grain_mapping(std::uint32_t asid, std::uint64_t vaddr) const override
  {
    const auto vgrain = vaddr >> map_.grain_bits();
    auto it = nmfc_grains_.find(std::pair{asid, vgrain});
    if (it == std::end(nmfc_grains_)) {
      return std::nullopt;
    }
    // Stamped, like every other physical address this allocator hands out: an
    // MMU entry built from this goes on to be routed and sliced by consumers
    // that read the mapping mode out of the address itself.
    return (it->second << map_.grain_bits()) | (std::uint64_t{1} << map_.mode_bit());
  }

  // ---- statistics ----

  void begin_phase(bool /*warmup*/) override
  {
    nmfc_allocs_ = standard_allocs_ = spills_ = pt_pages_ = 0;
    hints_received_ = 0;
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

    out.line(fmt::format("NMFC_VMEM ALLOCATIONS nmfc: {} standard: {} pt_pages: {} HINTS: {}", nmfc_allocs_, standard_allocs_, pt_pages_, hints_received_));
    out.line(fmt::format("NMFC_VMEM SPILLS: {} FREE GRAIN IMBALANCE: {:.0f}", spills_, imbalance));

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
    for (std::uint64_t grain = 1; grain < total_grains; ++grain) {
      free_grains_[grain % map_.num_tiles()].push_back(grain);
    }
    if (total_grains <= 1) {
      fmt::print("[NMFC_VMEM] ERROR: DRAM holds {} grains of {} bytes; nothing to allocate\n", total_grains, map_.grain());
      std::exit(-1);
    }
  }

  /** Take a grain owned by `tile`, spilling to another tile if that list is dry. */
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
    auto it = nmfc_grains_.find(key);
    bool fault = false;
    if (it == std::end(nmfc_grains_)) {
      it = nmfc_grains_.emplace(key, take_grain(hint.tile % map_.num_tiles())).first;
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
