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

    // Why a loaded channel can still be idle. The controller issues at most one
    // command per tick, and only from requests whose timing constraints are
    // already met. A queue that is deep but whose entries are all waiting on the
    // same few banks therefore leaves the command path idle while looking, from
    // outside, exactly like saturation. These separate the two: how often work
    // was queued and nothing could issue, and how many distinct banks the queued
    // work actually spanned at that moment.
    m_stats.add("nmfc_cycles_queue_nonempty", m_cycles_nonempty);
    m_stats.add("nmfc_cycles_queue_nonempty_no_issue", m_cycles_stalled);
    m_stats.add("nmfc_queued_bank_span_mean", m_span_mean);
    m_stats.add("nmfc_queued_depth_mean", m_depth_mean);

    // Where the bus time actually goes. A 64 B burst occupies the data bus for
    // nBL = 8 cycles, so back-to-back column commands can be 8 apart (different
    // bank group) or 12 (same, nCCDL). Anything beyond that is the channel
    // standing idle, and the distribution says which cause: a tail at 12 is bank
    // group collisions, a long tail is refresh or read/write turnaround.
    m_stats.add("nmfc_colcmd_gap_mean", m_gap_mean);
    m_stats.add("nmfc_colcmd_at_8", m_gap_8);
    m_stats.add("nmfc_colcmd_at_9_12", m_gap_12);
    m_stats.add("nmfc_colcmd_at_13_32", m_gap_32);
    m_stats.add("nmfc_colcmd_over_32", m_gap_big);
    m_stats.add("nmfc_colcmd_count", m_gap_count);

    // The question these answer: with a deep queue spanning most of the banks,
    // is there actually a request the controller is allowed to issue? A bank
    // holding a request it cannot start (its row is cycling, or an activate is
    // held off) contributes to the queue and to the bank span while offering the
    // scheduler nothing. Counting requests that pass check_timing separates
    // "queued" from "schedulable", which the span alone cannot.
    m_stats.add("nmfc_ready_mean", m_ready_mean);
    m_stats.add("nmfc_ready_col_mean", m_ready_col_mean);
    m_stats.add("nmfc_ready_banks_mean", m_ready_banks_mean);
    m_stats.add("nmfc_samples_zero_ready", m_zero_ready);
  }

  void pre_schedule() override
  {
    const auto depth = m_ctrl->peek_read_buffer().size() + m_ctrl->peek_write_buffer().size();
    m_issued_this_cycle = false;
    m_queued_this_cycle = depth != 0;
    if (!m_queued_this_cycle) {
      return;
    }
    ++m_cycles_nonempty;

    // Walking the buffer every cycle costs more than the simulation it measures,
    // and the quantity wanted is a mean, so sample it.
    if ((++m_sample_tick % SAMPLE_EVERY) != 0) {
      return;
    }
    ++m_samples;
    m_depth_sum += static_cast<double>(depth);

    // Distinct banks represented in the read queue right now. This is the
    // parallelism the scheduler actually has to work with, which the long-run
    // per-bank access spread cannot show: banks can be perfectly balanced over a
    // whole run and still be visited one at a time.
    m_seen.assign(m_accesses.size(), 0);
    m_seen_ready.assign(m_accesses.size(), 0);
    std::size_t span = 0;
    std::size_t ready = 0;
    std::size_t ready_col = 0;
    std::size_t ready_banks = 0;
    for (const auto& req : m_ctrl->peek_read_buffer()) {
      if (req.addr_vec.empty()) {
        continue;
      }
      const int bank = m_device->get_flat_bank_id(req.addr_vec);
      if (bank < 0 || bank >= static_cast<int>(m_seen.size())) {
        continue;
      }
      if (m_seen[static_cast<std::size_t>(bank)] == 0) {
        m_seen[static_cast<std::size_t>(bank)] = 1;
        ++span;
      }
      const int cmd = m_ctrl->get_preq_command(req.final_command, req.addr_vec);
      if (!m_ctrl->check_timing(cmd, req.addr_vec)) {
        continue;
      }
      ++ready;
      if (m_spec->command_meta[cmd].is_accessing) {
        ++ready_col;
      }
      if (m_seen_ready[static_cast<std::size_t>(bank)] == 0) {
        m_seen_ready[static_cast<std::size_t>(bank)] = 1;
        ++ready_banks;
      }
    }
    m_span_sum += static_cast<double>(span);
    m_ready_sum += static_cast<double>(ready);
    m_ready_col_sum += static_cast<double>(ready_col);
    m_ready_banks_sum += static_cast<double>(ready_banks);
    if (ready == 0) {
      ++m_zero_ready;
    }
  }

  void post_schedule() override
  {
    if (m_queued_this_cycle && !m_issued_this_cycle) {
      ++m_cycles_stalled;
    }
  }

  void on_issue(const Request& req) override
  {
    m_issued_this_cycle = true;
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
      const auto now = m_ctrl->m_clk;
      if (m_last_col != 0) {
        const auto gap = now - m_last_col;
        m_gap_sum += static_cast<double>(gap);
        ++m_gap_count;
        if (gap <= 8) {
          ++m_gap_8;
        } else if (gap <= 12) {
          ++m_gap_12;
        } else if (gap <= 32) {
          ++m_gap_32;
        } else {
          ++m_gap_big;
        }
      }
      m_last_col = now;
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
    const auto n = static_cast<double>(m_samples);
    m_span_mean = n == 0.0 ? 0.0 : m_span_sum / n;
    m_depth_mean = n == 0.0 ? 0.0 : m_depth_sum / n;
    m_gap_mean = m_gap_count == 0 ? 0.0 : m_gap_sum / static_cast<double>(m_gap_count);
    m_ready_mean = n == 0.0 ? 0.0 : m_ready_sum / n;
    m_ready_col_mean = n == 0.0 ? 0.0 : m_ready_col_sum / n;
    m_ready_banks_mean = n == 0.0 ? 0.0 : m_ready_banks_sum / n;
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
  std::vector<std::uint8_t> m_seen;
  std::vector<std::uint8_t> m_seen_ready;
  double m_ready_sum = 0.0, m_ready_col_sum = 0.0, m_ready_banks_sum = 0.0;
  double m_ready_mean = 0.0, m_ready_col_mean = 0.0, m_ready_banks_mean = 0.0;
  std::uint64_t m_zero_ready = 0;
  bool m_issued_this_cycle = false;
  bool m_queued_this_cycle = false;
  std::uint64_t m_cycles_nonempty = 0;
  std::uint64_t m_cycles_stalled = 0;
  std::uint64_t m_sample_tick = 0;
  std::uint64_t m_samples = 0;
  static constexpr std::uint64_t SAMPLE_EVERY = 64;
  double m_span_sum = 0.0;
  double m_depth_sum = 0.0;
  double m_span_mean = 0.0;
  double m_depth_mean = 0.0;
  std::uint64_t m_last_col = 0;
  double m_gap_sum = 0.0;
  double m_gap_mean = 0.0;
  std::uint64_t m_gap_count = 0, m_gap_8 = 0, m_gap_12 = 0, m_gap_32 = 0, m_gap_big = 0;
  std::vector<std::uint64_t> m_activates;

  std::uint64_t m_access_total = 0;
  std::uint64_t m_access_peak = 0;
  double m_access_spread = 0.0;
  double m_activate_spread = 0.0;
  std::size_t m_banks_idle = 0;
};

} // namespace Ramulator

#endif // NMFC_WITH_RAMULATOR
