/*
 * RAMULATOR_MC — a memory tile's DRAM channel, modeled by ramulator2.
 *
 * A model of the existing `memory_controller` interface, so it drops into a
 * configuration wherever DEFAULT_MEMORY_CONTROLLER goes and the LLC slice above
 * it needs no change. Each NMFC memory tile owns exactly one channel, which
 * suits it: tile selection already happened in the interleave fabric, and the
 * address arriving here has had the tile-select field compacted out, so a
 * single-channel controller sees precisely the dense space it wants.
 *
 * ramulator2 ships an `External` frontend for host simulators pushing requests
 * in, so this needs no custom frontend -- just the adapter below.
 *
 * TWO THINGS THIS HAS TO GET RIGHT, and both vary with the DRAM standard:
 *
 * 1. THE CLOCK. This model ticks the memory system once per operate(), so the
 *    module's clock period *is* the DRAM tCK. Get that wrong and every latency
 *    ramulator reports is silently rescaled -- the failure mode is plausible
 *    numbers that are wrong by a constant factor, which is the worst kind. So
 *    the period is derived from the configuration by default, and an explicitly
 *    configured one that disagrees is a hard error rather than a warning.
 *
 * 2. THE TRANSACTION WIDTH. ramulator2 refuses a request larger than one DRAM
 *    transaction (generic_dram_system.cpp checks size_bytes > tx_bytes and
 *    throws), so a cache block wider than a transaction must be split by the
 *    frontend. DDR4 and DDR5 happen to land on 64 B and hide this; HBM3's
 *    pseudo-channel does not, and would abort at runtime. The split below
 *    issues ceil(block / tx) transactions and completes the block only when the
 *    last one lands, which is also what a real controller does with a burst.
 *
 * OPT-IN. Guarded so the default build needs nothing in ext/:
 *     tools/nmfc/build_ramulator.sh
 *     make NMFC_RAMULATOR=1
 *
 * Parameters:
 *   config           path to an exported ramulator2 YAML (config/nmfc/ramulator/)
 *   ul_channels      [@channel, ...] the LLC slice(s) above
 *   clock_period     optional; derived from the DRAM tCK when absent
 *   size             bytes this channel provides, for the page allocator
 *   max_accept       ChampSim requests taken per cycle (default 4)
 */

#ifdef NMFC_WITH_RAMULATOR

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
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

/** What a ramulator config implies about the machine it describes. */
struct dram_geometry {
  /** Channels inside this one instance. Each memory tile owns exactly one instance. */
  std::uint64_t channels_in_instance = 0;
  /** Ranks x bank groups x banks: everything addressable under a channel above the row. */
  std::uint64_t banks_per_channel = 0;
  std::uint64_t rows_per_bank = 0;
  std::uint64_t columns = 0;
  std::uint64_t channel_width_bytes = 0;
  std::uint64_t row_bytes = 0;
  /** One channel's worth. An instance holds channels_in_instance of these. */
  std::uint64_t channel_capacity_bytes = 0;
  champsim::chrono::picoseconds tCK{};
  int tx_bytes = 0;
};

/**
 * Read tCK and the transaction width out of a config, before this module's own
 * base class is initialised.
 *
 * champsim::operable fixes its clock period in the base-class initialiser, so
 * deriving the period from the DRAM means knowing it before construction
 * proper. That costs one throwaway memory system per distinct config file; the
 * result is cached, so N tiles sharing a config probe once.
 */
dram_geometry probe_geometry(const std::string& config_path)
{
  static std::map<std::string, dram_geometry> cache;
  if (auto it = cache.find(config_path); it != std::end(cache)) {
    return it->second;
  }

  auto config = Ramulator::Config::parse_config_file(config_path);
  auto* probe = Ramulator::Factory::create_memory_system(config);

  dram_geometry geometry{};
  const auto tCK_ns = probe->get_tCK();
  if (tCK_ns <= 0.0F) {
    fmt::print("[RAMULATOR_MC] ERROR: {} reports no DRAM clock, so the module cannot be clocked correctly.\n", config_path);
    std::exit(-1);
  }
  geometry.tCK = champsim::chrono::picoseconds{static_cast<std::intmax_t>(static_cast<double>(tCK_ns) * 1000.0 + 0.5)};
  geometry.tx_bytes = probe->get_tx_bytes();

  // The device organisation, so the tagging granularity can be derived from the
  // DRAM rather than set beside it and left to drift. §5.2's threshold is
  //
  //     G = row_bytes_per_channel x banks_per_channel x num_channels
  //
  // and it is not decoration: it is the size at which a tagged unit owns whole
  // rows, which is the only reason two units in different mapping modes cannot
  // contend for the same bank and column slots. Set it too small and the modes
  // interfere; too large and neither owns a row, so the row locality the NMFC
  // mapping exists to create is not there at all.
  //
  // The org is [channel, ...banks..., row, column] for every standard here, so
  // the levels between the channel and the row are what "banks per channel"
  // means, whatever a given DRAM calls them.
  const auto controllers = config["memory_system"]["controllers"];
  if (controllers.is_sequence() && !controllers.seq().empty()) {
    const auto dram = controllers.seq().front()["dram"];
    const auto org = dram["org"]["count"].as<std::vector<int>>();
    if (org.size() >= 4) {
      // org is [channel, rank, ...bankgroup/bank..., row, column].
      geometry.channels_in_instance = static_cast<std::uint64_t>(org.front());
      geometry.rows_per_bank = static_cast<std::uint64_t>(org[org.size() - 2]);
      geometry.columns = static_cast<std::uint64_t>(org.back());
      geometry.channel_width_bytes = static_cast<std::uint64_t>(dram["channel_width"].as<int>()) / 8;

      // Everything between the channel and the row: ranks, bank groups and
      // banks. All of it is addressable under the channel, so all of it counts.
      geometry.banks_per_channel = 1;
      for (std::size_t i = 1; i + 2 < org.size(); ++i) {
        geometry.banks_per_channel *= static_cast<std::uint64_t>(org[i]);
      }

      // A row holds one column entry per column *address*, and the address
      // mapper reduces the column field by the internal prefetch while shifting
      // the address down by the transaction size -- so the prefetch cancels and
      // a row is simply its columns times the channel width. Using the
      // transaction size here instead counts the burst twice, which is how a
      // 4 KiB row was reported as 64 KiB.
      geometry.row_bytes = geometry.columns * geometry.channel_width_bytes;
      geometry.channel_capacity_bytes = geometry.banks_per_channel * geometry.rows_per_bank * geometry.row_bytes;
    }
  }

  return cache.emplace(config_path, geometry).first->second;
}

/**
 * The module's clock period: the configured one if given, else the DRAM's.
 *
 * Called from the base-class initialiser, which is why it is a free function
 * taking the builder rather than a member.
 */
/**
 * How large this channel is, according to the device.
 *
 * Not a parameter. ramulator2 owns the organisation, so it owns the capacity
 * that follows from it, and a second number in a second file is only an
 * opportunity for the two to disagree -- which they did, by four times.
 */
std::uint64_t resolve_size(const champsim::modules::ModuleBuilder& builder)
{
  const auto geometry = probe_geometry(builder.get_parameter<std::string>("config"));
  const auto configured = builder.get_parameter<std::uint64_t>("size", true, std::uint64_t{0});
  const auto instance_capacity = geometry.channels_in_instance * geometry.channel_capacity_bytes;
  if (instance_capacity == 0) {
    return configured; // an org this did not recognise; fall back rather than invent
  }
  if (configured != 0 && configured != instance_capacity) {
    fmt::print("[RAMULATOR_MC] ERROR: size is {} bytes but this DRAM holds {}.\n"
               "  The allocator asks this controller how much memory exists, so a smaller answer strands capacity and a\n"
               "  larger one hands out frames nothing backs. Omit size: the device decides.\n",
               configured, instance_capacity);
    std::exit(-1);
  }
  return instance_capacity;
}

champsim::chrono::picoseconds resolve_period(const champsim::modules::ModuleBuilder& builder)
{
  const auto geometry = probe_geometry(builder.get_parameter<std::string>("config"));
  const auto configured = builder.get_parameter<champsim::chrono::picoseconds>("clock_period", true, champsim::chrono::picoseconds{0});

  if (configured.count() == 0) {
    return geometry.tCK; // derived: the common and safe path
  }
  if (configured != geometry.tCK) {
    fmt::print("[RAMULATOR_MC] ERROR: clock_period is {} ps but this DRAM's tCK is {} ps.\n"
               "  The memory system is ticked once per cycle, so every latency it reports would be scaled by {:.3f}x.\n"
               "  Either set clock_period to {} ps, or omit it and let it be derived.\n",
               configured.count(), geometry.tCK.count(), static_cast<double>(configured.count()) / static_cast<double>(geometry.tCK.count()),
               geometry.tCK.count());
    std::exit(-1);
  }
  return configured;
}

class ramulator_mc : public champsim::modules::memory_controller_module
{
  using channel_type = champsim::modules::channel_module;
  using response_queue_type = champsim::modules::channel_module::response_queue_type;

public:
  explicit ramulator_mc(champsim::modules::ModuleBuilder builder)
      : champsim::modules::memory_controller_module(resolve_period(builder)), queues_(builder.get_parameter<std::vector<channel_type*>>("ul_channels")),
        config_path_(builder.get_parameter<std::string>("config")), size_bytes_(resolve_size(builder)),
        max_accept_(builder.get_parameter<champsim::bandwidth::maximum_type>("max_accept", true, champsim::bandwidth::maximum_type{4})),
        block_bytes_(builder.get_parameter<unsigned>("block_size", true, 64U)),
        access_trace_path_(builder.get_parameter<std::string>("access_trace", true, std::string{}))
  {
    auto config = Ramulator::Config::parse_config_file(config_path_);
    frontend_ = Ramulator::Factory::create_frontend(config);
    memory_system_ = Ramulator::Factory::create_memory_system(config);
    frontend_->connect_memory_system(memory_system_);
    memory_system_->connect_frontend(frontend_);

    tx_bytes_ = memory_system_->get_tx_bytes();
    if (tx_bytes_ <= 0) {
      fmt::print("[{}] ERROR: {} reports a transaction size of {} bytes\n", NAME, config_path_, tx_bytes_);
      std::exit(-1);
    }
    // A block wider than a transaction becomes several of them. ramulator
    // refuses an oversized request outright rather than splitting it, so this
    // is a correctness requirement, not an optimisation.
    transactions_per_block_ = (block_bytes_ + static_cast<unsigned>(tx_bytes_) - 1) / static_cast<unsigned>(tx_bytes_);

    check_geometry(builder);
  }

  /**
   * The DRAM decides the tagging granularity, not the configuration file beside
   * it.
   *
   * One instance per channel is the arrangement: channel selection happens in
   * the interleave fabric above, so a controller that thought it owned several
   * channels would be decoding a field that is no longer in the address it
   * receives.
   *
   * And the grain has to be the size at which a tagged unit owns whole rows, or
   * §5.2's argument for per-unit mode tagging does not hold -- two units in
   * different modes would contend for the same bank and column slots. Deriving
   * it here means the two cannot drift apart, which they had: the grain was set
   * from the doc's DDR5 worked example while the modelled device was a different
   * shape entirely.
   */
  void check_geometry(const champsim::modules::ModuleBuilder& builder) const
  {
    const auto g = probe_geometry(config_path_);
    if (g.channels_in_instance == 0 || g.banks_per_channel == 0 || g.row_bytes == 0) {
      return; // an org this did not recognise; say nothing rather than guess
    }

    // Total channels in the machine, which is what the siloing granularity is
    // defined against: every memory tile owns one instance, and an instance may
    // own more than one channel.
    const auto tiles = builder.get_parameter<std::size_t>("nmfc_num_tiles", true, std::size_t{1});
    const auto total_channels = g.channels_in_instance * tiles;

    // §5.2. A STANDARD unit spreads over every channel and takes one row index
    // across all banks on each; an NMFC unit sits on one channel and takes that
    // many row indices. Both come to the same size, and that size is the only
    // one at which the two modes cannot contend for the same bank and column.
    const auto required = g.row_bytes * g.banks_per_channel * total_channels;

    const auto grain_bits = builder.get_parameter<unsigned>("nmfc_grain_bits", true, 0U);
    if (grain_bits != 0 && (std::uint64_t{1} << grain_bits) != required) {
      fmt::print("[{}] ERROR: nmfc_grain_bits is {} ({} bytes) but this DRAM requires {}.\n"
                 "  row_bytes {} x banks_per_channel {} x total_channels {} ({} per instance x {} tiles).\n"
                 "  Below it the two mapping modes contend for the same rows; above it neither owns one.\n",
                 NAME, grain_bits, std::uint64_t{1} << grain_bits, required, g.row_bytes, g.banks_per_channel, total_channels, g.channels_in_instance,
                 tiles);
      std::exit(-1);
    }
  }


  long operate() final
  {
    long progress = 0;
    progress += issue_pending();
    progress += accept_requests();

    // One DRAM cycle per operated cycle -- which is only correct because the
    // module's clock period is the DRAM tCK, enforced at construction.
    frontend_->tick();
    memory_system_->tick();
    ++dram_cycles_;

    progress += deliver_responses();

    // Timer-driven work inside the memory system (a refresh in flight, a busy
    // bank) advances without retiring anything, so a tick with requests still
    // in flight counts as progress -- otherwise the stall detector reads a
    // slow DRAM as a deadlock.
    //
    // But only then. This used to return `progress + 1` unconditionally, which
    // does not avoid a false positive, it destroys the detector: the phase
    // controller stalls on `progress == 0` summed over every operable, so one
    // module that always claims progress means ABORT can never fire and
    // print_deadlock() is never reached. A real deadlock then presents as a
    // simulation that runs forever with cycles advancing and nothing retiring,
    // which cost two days of wall clock before it was noticed.
    return progress + (outstanding_ > 0 ? 1 : 0);
  }

  [[nodiscard]] std::size_t get_num_channels() const final { return 1; }

  // The interface's stats type describes ChampSim's own DRAM model, so there is
  // nothing honest to put in most of it. ramulator2's real numbers go into the
  // report in end_phase instead.
  [[nodiscard]] stats_type get_sim_stats(std::size_t /*channel*/) const final
  {
    stats_type stats;
    stats.name = NAME;
    return stats;
  }

  [[nodiscard]] champsim::data::bytes size() const final { return champsim::data::bytes{static_cast<long long>(size_bytes_)}; }

  void begin_phase(bool /*warmup*/) override
  {
    if (!access_trace_path_.empty() && trace_out_ == nullptr) {
      const auto path = fmt::format("{}.{}.trace", access_trace_path_, NAME);
      trace_out_ = std::fopen(path.c_str(), "w");
      if (trace_out_ == nullptr) {
        fmt::print("[{}] ERROR: cannot open access trace {}\n", NAME, path);
        std::exit(-1);
      }
    }

    reads_ = writes_ = completed_ = refused_ = transactions_ = 0;
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
    out.line(fmt::format("{} TRANSACTIONS: {} at {} B ({} per block) MEAN READ LATENCY: {:.1f} DRAM cycles over {}", NAME, transactions_, tx_bytes_,
                         transactions_per_block_, mean_latency, dram_cycles_));

    // ramulator2 knows far more about what happened than this adapter does, so
    // hand its own statistics through verbatim rather than paraphrasing them.
    //
    // Both calls below were missing, and their absence was silent. print_stats
    // only reaches the memory system's own implementation, so component
    // statistics were stale, and finalize -- which is what every controller
    // plugin reports from -- never ran at all. A plugin could be configured,
    // constructed, and asked about every issued command, and produce no output
    // whatsoever, which is exactly what happened the first time one was added.
    //
    // update_stats_recursive is the one that matters here: print_stats only
    // reaches the memory system's own implementation, so without it every
    // component statistic is whatever it was when last computed. finalize is
    // deliberately not called -- this adapter is driven per phase, and the first
    // phase to end is the warmup, so anything reported from finalize would
    // describe a one-instruction run.
    memory_system_->update_stats_recursive();

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
    json.add("transactions", transactions_);
    json.add("tx_bytes", tx_bytes_);
    json.add("transactions_per_block", transactions_per_block_);
    json.add("mean_read_latency_dram_cycles", mean_latency);
    json.add("dram_cycles", dram_cycles_);
    json.add("tCK_ps", static_cast<std::int64_t>(clock_period.count()));
    json.add("config", config_path_);
  }

  void print_deadlock() final
  {
    fmt::print("[{}] outstanding blocks: {} unissued transactions: {} pending responses: {}\n", NAME, outstanding_, to_issue_.size(), pending_.size());
  }

private:
  /**
   * One ChampSim block in flight, however many DRAM transactions it took.
   *
   * Shared between the transactions it was split into; the response goes back
   * when the last of them lands, because a block is not filled until all of it
   * has arrived.
   */
  struct block_state {
    champsim::response response;
    response_queue_type* return_queue = nullptr; // null for writes: nothing waits
    unsigned outstanding = 0;
    std::uint64_t issued_cycle = 0;

    // champsim::response carries no default constructor, so the response is
    // captured at construction rather than filled in afterwards.
    block_state(const champsim::request& request, response_queue_type* queue, unsigned transactions, std::uint64_t cycle)
        : response(request), return_queue(queue), outstanding(transactions), issued_cycle(cycle)
    {
    }
  };

  struct pending_tx {
    Ramulator::Addr_t address = 0;
    int type = 0;
    int size_bytes = 0;
    int source = 0;
    std::shared_ptr<block_state> block;
  };

  /** Split a block into transactions and queue them. */
  void enqueue(const champsim::request& request, int type, response_queue_type* return_queue)
  {
    auto block = std::make_shared<block_state>(request, return_queue, transactions_per_block_, dram_cycles_);

    const auto base = request.address.to<std::uint64_t>();
    for (unsigned i = 0; i < transactions_per_block_; ++i) {
      const auto offset = static_cast<std::uint64_t>(i) * static_cast<std::uint64_t>(tx_bytes_);
      const auto remaining = block_bytes_ - static_cast<unsigned>(offset);

      // Optional capture of the exact address stream this channel is asked for,
      // in ramulator's LoadStoreTrace format. Replaying it headless drives the
      // same device with the same addresses at whatever rate the frontend can
      // inject, which separates "this stream cannot go faster on this device"
      // from "our machine never asked for it faster" -- a distinction no
      // in-simulation counter can make, because both look like a busy queue.
      if (trace_out_ != nullptr) {
        fmt::print(trace_out_, "{} {:#x}\n", type == 1 ? "ST" : "LD", base + offset);
      }
      to_issue_.push_back(pending_tx{static_cast<Ramulator::Addr_t>(base + offset), type, static_cast<int>(std::min<unsigned>(remaining, static_cast<unsigned>(tx_bytes_))),
                                     static_cast<int>(request.origin.cpu()), block});
    }
  }

  /** Push queued transactions into ramulator until it stops taking them. */
  long issue_pending()
  {
    long progress = 0;
    while (!to_issue_.empty()) {
      auto& next = to_issue_.front();
      auto block = next.block;

      auto callback = [this, block](Ramulator::Request& /*done*/) {
        if (--block->outstanding != 0) {
          return; // the block is not filled until every transaction lands
        }
        latency_sum_ += (dram_cycles_ - block->issued_cycle);
        ++completed_;
        --outstanding_;
        if (block->return_queue != nullptr) {
          pending_.push_back(std::pair{block->return_queue, block->response});
        }
      };

      if (!frontend_->receive_external_requests(next.type, next.address, next.source, std::move(callback), next.size_bytes)) {
        ++refused_;
        break; // ramulator's buffers are full; hold order and retry next cycle
      }
      ++transactions_;
      to_issue_.pop_front();
      ++progress;
    }
    return progress;
  }

  /** Take what the slices above are offering, up to this cycle's budget. */
  long accept_requests()
  {
    long progress = 0;
    champsim::bandwidth bandwidth{max_accept_};

    for (auto* upper : queues_) {
      auto* return_queue = &upper->get_returned();

      const auto drain = [&](auto& queue, int type, response_queue_type* target, std::uint64_t* counter) {
        while (bandwidth.has_remaining() && !queue.empty() && to_issue_.size() < split_queue_limit_) {
          enqueue(queue.front(), type, target);
          queue.pop_front();
          ++outstanding_;
          if (counter != nullptr) {
            ++*counter;
          }
          bandwidth.consume();
          ++progress;
        }
      };

      drain(upper->get_rq(), Ramulator::Request::Type::Read, return_queue, &reads_);
      // Writes are fire and forget: nothing upstream waits on one.
      drain(upper->get_wq(), Ramulator::Request::Type::Write, nullptr, &writes_);
      drain(upper->get_pq(), Ramulator::Request::Type::Read, return_queue, nullptr);
    }
    return progress;
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
  bool finalized_ = false;
  champsim::bandwidth::maximum_type max_accept_;
  unsigned block_bytes_;
  std::string access_trace_path_;
  std::FILE* trace_out_ = nullptr;

  int tx_bytes_ = 0;
  unsigned transactions_per_block_ = 1;
  // Deep enough that a burst of blocks does not stall acceptance, shallow
  // enough that back-pressure still reaches the slice above.
  static constexpr std::size_t split_queue_limit_ = 256;

  Ramulator::IFrontEnd* frontend_ = nullptr;
  Ramulator::IMemorySystem* memory_system_ = nullptr;

  std::deque<pending_tx> to_issue_;
  std::vector<std::pair<response_queue_type*, champsim::response>> pending_;

  std::uint64_t reads_ = 0;
  std::uint64_t writes_ = 0;
  std::uint64_t completed_ = 0;
  std::uint64_t refused_ = 0;
  std::uint64_t transactions_ = 0;
  std::uint64_t outstanding_ = 0;
  std::uint64_t dram_cycles_ = 0;
  std::uint64_t latency_sum_ = 0;
};

static champsim::modules::memory_controller_module::register_module<ramulator_mc> ramulator_mc_reg("RAMULATOR_MC");

} // anonymous namespace

#endif // NMFC_WITH_RAMULATOR
