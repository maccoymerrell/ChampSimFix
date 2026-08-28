/*
 * NMFCBankBalance — is the machine actually using every bank?
 *
 * A tile can be at 78% of its channel's peak bandwidth and still be leaving a
 * great deal on the floor if its requests pile onto a few banks: the aggregate
 * looks respectable because the busy banks are saturated, while the idle ones
 * contribute nothing and never appear in a bandwidth figure. Nothing measured so
 * far would have caught that -- row hit rates say what happens once a request
 * reaches a bank, not whether the banks are evenly fed.
 *
 * It matters here more than in a conventional machine. The placement pass silos
 * data at grain granularity, the allocator hands out grains in groups, and the
 * graph structures are grain-aligned and walked with regular strides. Every one
 * of those is a chance for a stride to land on a subset of banks, and the
 * symptom would be indistinguishable from "the workload is just slow".
 *
 * Counts accesses per flat bank and reports the spread: perfectly even is 1.00,
 * and anything much above it means some banks are doing the work of several.
 * Registered from this tree rather than the vendored clone, which is not tracked.
 */

#ifdef NMFC_WITH_RAMULATOR

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>
#include <fmt/format.h>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator
{

class NMFCBankBalance : public IControllerPlugin, public Implementation
{
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, NMFCBankBalance, "NMFCBankBalance")

public:
  void init() override {}

  void setup(IFrontEnd* /*frontend*/, IMemorySystem* /*memory_system*/) override
  {
    m_ctrl = cast_parent<ControllerBase>();
    m_device = &m_ctrl->m_device;
    m_spec = m_device->m_spec;
    m_accesses.assign(m_device->m_bank_nodes.size(), 0);
    m_activates.assign(m_device->m_bank_nodes.size(), 0);

    // Registered, not printed from finalize(). A plugin's finalize runs when the
    // memory system is finalised, and this adapter drives phases -- so anything
    // reported there describes whichever phase happened to trigger it, which
    // was the one-instruction warmup. Registered statistics hold a reference and
    // are read live wherever stats are dumped.
    m_stats.add("nmfc_bank_access_spread", m_access_spread);
    m_stats.add("nmfc_bank_access_peak", m_access_peak);
    m_stats.add("nmfc_bank_access_total", m_access_total);
    m_stats.add("nmfc_banks_never_accessed", m_banks_idle);
    m_stats.add("nmfc_bank_activate_spread", m_activate_spread);
  }

  void on_issue(const Request& req) override
  {
    if (req.addr_vec.empty()) {
      return;
    }
    const auto& meta = m_spec->command_meta[req.command];
    const int bank = m_device->get_flat_bank_id(req.addr_vec);
    if (bank < 0 || bank >= static_cast<int>(m_accesses.size())) {
      return; // an all-bank command, which belongs to no single bank
    }
    if (meta.is_accessing) {
      ++m_accesses[static_cast<std::size_t>(bank)];
    }
    if (meta.is_opening) {
      ++m_activates[static_cast<std::size_t>(bank)];
    }
  }

  void update_stats() override
  {
    m_access_total = summarise(m_accesses, m_access_peak, m_access_spread, m_banks_idle);
    std::uint64_t peak = 0;
    std::size_t idle = 0;
    summarise(m_activates, peak, m_activate_spread, idle);
  }

private:
  /** Peak over mean: 1.00 is perfectly even, and higher says a bank is standing in for several. */
  static std::uint64_t summarise(const std::vector<std::uint64_t>& counts, std::uint64_t& peak, double& spread, std::size_t& idle)
  {
    peak = 0;
    spread = 0.0;
    idle = 0;
    if (counts.empty()) {
      return 0;
    }
    const auto total = std::accumulate(std::begin(counts), std::end(counts), std::uint64_t{0});
    peak = *std::max_element(std::begin(counts), std::end(counts));
    idle = static_cast<std::size_t>(std::count(std::begin(counts), std::end(counts), std::uint64_t{0}));
    if (total != 0) {
      const auto mean = static_cast<double>(total) / static_cast<double>(counts.size());
      spread = static_cast<double>(peak) / mean;
    }
    return total;
  }

  ControllerBase* m_ctrl = nullptr;
  DRAMDevice* m_device = nullptr;
  const DRAMSpec* m_spec = nullptr;
  std::vector<std::uint64_t> m_accesses;
  std::vector<std::uint64_t> m_activates;

  std::uint64_t m_access_total = 0;
  std::uint64_t m_access_peak = 0;
  double m_access_spread = 0.0;
  double m_activate_spread = 0.0;
  std::size_t m_banks_idle = 0;
};

} // namespace Ramulator

#endif // NMFC_WITH_RAMULATOR
