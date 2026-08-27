/*
 * RAMULATOR_MC — a memory tile's DRAM channel, modeled by ramulator2.
 *
 * A model of the existing `memory_controller` interface, so it drops into a
 * configuration wherever DEFAULT_MEMORY_CONTROLLER goes and the LLC slice above
 * it needs no change at all.
 *
 * Each NMFC memory tile owns exactly one channel, so each instance drives a
 * single-channel ramulator2 machine. Tile selection already happened in the
 * interleave fabric, and the address arriving here has had the tile-select
 * field compacted out, which is exactly what a single-channel controller wants:
 * a dense address space of its own.
 *
 * ramulator2 provides an `External` frontend for host simulators pushing
 * requests in, so unlike older integrations this needs no custom frontend --
 * just the adapter below.
 *
 * OPT-IN. Guarded so the default build needs nothing in ext/. Build with:
 *     tools/nmfc/build_ramulator.sh
 *     make NMFC_RAMULATOR=1
 *
 * Parameters:
 *   config           path to an exported ramulator2 YAML (see config/nmfc/ramulator/)
 *   ul_channels      [@channel, ...] the LLC slice(s) above
 *   clock_period     time; must match the DRAM tCK, which is checked at startup
 *   size             bytes this channel provides, for the page allocator
 *   max_accept       requests taken per cycle (default 4)
 */

#ifdef NMFC_WITH_RAMULATOR

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <sstream>
#include <string>
#include <vector>
#include <fmt/core.h>

#include "channel.h"
#include "dram_stats.h"
#include "modules.h"
#include "nmfc/nmfc_config.h"
#include "stat_report.h"

#include "ramulator/base/base.h"
#include "ramulator/base/config.h"
#include "ramulator/base/factory.h"
#include "ramulator/base/request.h"
#include "ramulator/frontend/i_frontend.h"
#include "ramulator/memory_system/i_memory_system.h"

namespace
{

class ramulator_mc : public champsim::modules::memory_controller_module
{
  using channel_type = champsim::modules::channel_module;

public:
  explicit ramulator_mc(champsim::modules::ModuleBuilder builder)
      : champsim::modules::memory_controller_module(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        queues_(builder.get_parameter<std::vector<channel_type*>>("ul_channels")), config_path_(builder.get_parameter<std::string>("config")),
        size_bytes_(builder.get_parameter<std::uint64_t>("size", true, std::uint64_t{4} << 30)),
        max_accept_(builder.get_parameter<champsim::bandwidth::maximum_type>("max_accept", true, champsim::bandwidth::maximum_type{4})),
        block_bytes_(builder.get_parameter<unsigned>("block_size", true, 64U))
  {
    auto config = Ramulator::Config::parse_config_file(config_path_);
    frontend_ = Ramulator::Factory::create_frontend(config);
    memory_system_ = Ramulator::Factory::create_memory_system(config);
    frontend_->connect_memory_system(memory_system_);
    memory_system_->connect_frontend(frontend_);

    // The ChampSim clock and the DRAM clock have to agree, because this model
    // ticks ramulator once per operate(). A mismatch would silently rescale
    // every latency the memory system reports.
    const auto tCK_ns = memory_system_->get_tCK();
    if (tCK_ns > 0.0F) {
      const auto tCK_ps = static_cast<std::intmax_t>(static_cast<double>(tCK_ns) * 1000.0);
      const auto configured = clock_period.count();
      if (std::llabs(tCK_ps - configured) > std::max<std::intmax_t>(1, configured / 100)) {
        fmt::print("[{}] WARNING: clock_period is {} ps but the DRAM tCK is {} ps. Every reported latency will be off by that ratio; "
                   "set clock_period to match the ramulator config.\n",
                   NAME, configured, tCK_ps);
      }
    }
  }

  long operate() final
  {
    long progress = 0;
    progress += accept_requests();

    // One DRAM cycle per operated cycle. Timer-driven work inside the memory
    // system (a refresh in flight, a busy bank) advances even when nothing
    // retires, so report a tick as progress and keep the deadlock detector --
    // which counts consecutive zero-progress cycles -- from seeing a stall.
    frontend_->tick();
    memory_system_->tick();
    ++dram_cycles_;

    progress += deliver_responses();
    return progress + 1;
  }

  [[nodiscard]] std::size_t get_num_channels() const final { return 1; }

  // The interface's stats type describes ChampSim's own DRAM model, so there is
  // nothing honest to put in most of it. ramulator2's real numbers -- row hits,
  // conflicts, per-bank activity -- go into the report in end_phase instead.
  [[nodiscard]] stats_type get_sim_stats(std::size_t /*channel*/) const final
  {
    stats_type stats;
    stats.name = NAME;
    return stats;
  }

  [[nodiscard]] champsim::data::bytes size() const final { return champsim::data::bytes{static_cast<long long>(size_bytes_)}; }

  void begin_phase(bool /*warmup*/) override
  {
    reads_ = writes_ = completed_ = refused_ = 0;
    dram_cycles_ = 0;
    latency_sum_ = 0;
    memory_system_->reset_stats_recursive();
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (reads_ == 0 && writes_ == 0) {
      return;
    }
    const auto mean_latency = completed_ == 0 ? 0.0 : static_cast<double>(latency_sum_) / static_cast<double>(completed_);
    out.line(fmt::format("{} (ramulator2) READS: {} WRITES: {} COMPLETED: {} REFUSED: {}", NAME, reads_, writes_, completed_, refused_));
    out.line(fmt::format("{} MEAN READ LATENCY: {:.1f} DRAM cycles over {} cycles", NAME, mean_latency, dram_cycles_));

    // ramulator2 knows far more about what happened than this adapter does, so
    // hand its own statistics through verbatim rather than paraphrasing them.
    std::ostringstream captured;
    memory_system_->print_stats(captured);
    std::istringstream lines{captured.str()};
    for (std::string line; std::getline(lines, line);) {
      if (!line.empty()) {
        out.line(fmt::format("{} {}", NAME, line));
      }
    }

    auto json = out.json();
    json.add("reads", reads_);
    json.add("writes", writes_);
    json.add("completed", completed_);
    json.add("refused", refused_);
    json.add("mean_read_latency_dram_cycles", mean_latency);
    json.add("dram_cycles", dram_cycles_);
    json.add("config", config_path_);
  }

  void print_deadlock() final { fmt::print("[{}] outstanding: {} pending responses: {}\n", NAME, outstanding_, pending_.size()); }

private:
  /** Take what the slices above are offering, up to this cycle's budget. */
  long accept_requests()
  {
    long progress = 0;
    champsim::bandwidth bandwidth{max_accept_};

    for (auto* upper : queues_) {
      auto* return_queue = &upper->get_returned();

      auto& read_queue = upper->get_rq();
      while (bandwidth.has_remaining() && !read_queue.empty()) {
        if (!send(read_queue.front(), Ramulator::Request::Type::Read, return_queue)) {
          ++refused_;
          break; // ramulator's buffers are full; leave it queued
        }
        read_queue.pop_front();
        ++reads_;
        bandwidth.consume();
        ++progress;
      }

      auto& write_queue = upper->get_wq();
      while (bandwidth.has_remaining() && !write_queue.empty()) {
        // Writes are fire and forget: nothing upstream waits on one.
        if (!send(write_queue.front(), Ramulator::Request::Type::Write, nullptr)) {
          ++refused_;
          break;
        }
        write_queue.pop_front();
        ++writes_;
        bandwidth.consume();
        ++progress;
      }

      auto& prefetch_queue = upper->get_pq();
      while (bandwidth.has_remaining() && !prefetch_queue.empty()) {
        if (!send(prefetch_queue.front(), Ramulator::Request::Type::Read, return_queue)) {
          ++refused_;
          break;
        }
        prefetch_queue.pop_front();
        bandwidth.consume();
        ++progress;
      }
    }
    return progress;
  }

  bool send(const champsim::request& request, int type, champsim::modules::channel_module::response_queue_type* return_queue)
  {
    const auto address = static_cast<Ramulator::Addr_t>(request.address.to<std::uint64_t>());

    // The response the completion will carry, captured now: by the time
    // ramulator calls back, the queue entry it came from is long gone.
    champsim::response response{request};
    const auto issued = dram_cycles_;

    auto callback = [this, response, return_queue, issued](Ramulator::Request& /*done*/) {
      latency_sum_ += (dram_cycles_ - issued);
      ++completed_;
      --outstanding_;
      if (return_queue != nullptr) {
        pending_.push_back(std::pair{return_queue, response});
      }
    };

    if (!frontend_->receive_external_requests(type, address, request.origin.cpu(), std::move(callback), static_cast<int>(block_bytes_))) {
      return false;
    }
    ++outstanding_;
    return true;
  }

  /** Hand back whatever completed during this cycle's tick. */
  long deliver_responses()
  {
    long progress = 0;
    for (auto& [queue, response] : pending_) {
      queue->push_back_grow(response);
      ++progress;
    }
    pending_.clear();
    return progress;
  }

  std::vector<channel_type*> queues_;
  std::string config_path_;
  std::uint64_t size_bytes_;
  champsim::bandwidth::maximum_type max_accept_;
  unsigned block_bytes_;

  Ramulator::IFrontEnd* frontend_ = nullptr;
  Ramulator::IMemorySystem* memory_system_ = nullptr;

  std::vector<std::pair<champsim::modules::channel_module::response_queue_type*, champsim::response>> pending_;

  std::uint64_t reads_ = 0;
  std::uint64_t writes_ = 0;
  std::uint64_t completed_ = 0;
  std::uint64_t refused_ = 0;
  std::uint64_t outstanding_ = 0;
  std::uint64_t dram_cycles_ = 0;
  std::uint64_t latency_sum_ = 0;
};

static champsim::modules::memory_controller_module::register_module<ramulator_mc> ramulator_mc_reg("RAMULATOR_MC");

} // anonymous namespace

#endif // NMFC_WITH_RAMULATOR
