/*
 * INTERLEAVE_FABRIC — the memory network, as a model of the existing `channel`
 * interface.
 *
 * One instance sits below each compute tile's last private cache. Upward it
 * looks exactly like a channel, so an unmodified CACHE uses it as its
 * `lower_level` and neither knows nor cares that its misses are being routed.
 * Downward it fans out to one real channel per memory tile.
 *
 * Two things happen on the way through:
 *
 *   routing     tile_of(pa) reads the owning tile straight out of the physical
 *               address — no lookup, and correct in both mapping modes because
 *               the mode is a bit of the address itself.
 *
 *   compaction  the tile-select field is removed on the way down and reinserted
 *               on the way back. Without it an LLC slice would see a constant
 *               value in the middle of its set index and use 1/num_tiles of its
 *               sets. Both directions are pure functions of (address, tile), so
 *               there is no per-request bookkeeping to get wrong.
 *
 * Only the physical address is compacted; v_address is virtual and passes
 * through untouched.
 *
 * Parameters:
 *   tiles              [@channel, ...]  one downstream channel per memory tile
 *   clock_period       time
 *   hop_latency        cycles each direction (default 4)
 *   queue_size         inbound requests held before add_* refuses (default 64)
 *   max_forward        requests moved per cycle, each direction (default 4)
 *   compact_tile_bits  bool (default true) — off only to A/B the compaction
 */

#include <algorithm>
#include <cstddef>
#include <deque>
#include <vector>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include "bandwidth.h"
#include "cache_stats.h"
#include "channel.h"
#include "modules.h"
#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_types.h"
#include "operable.h"
#include "stat_report.h"
#include "util/ring_buffer.h"

namespace
{

class interleave_fabric : public champsim::modules::channel_module, public champsim::operable
{
  using channel_type = champsim::modules::channel_module;

public:
  explicit interleave_fabric(champsim::modules::ModuleBuilder builder)
      : champsim::operable(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")),
        tiles_(builder.get_parameter<std::vector<channel_type*>>("tiles")), map_(nmfc::tile_map_from(builder)),
        hop_latency_(nmfc::cycles_from(builder, "hop_latency", 4)), queue_size_(builder.get_parameter<std::size_t>("queue_size", true, std::size_t{64})),
        max_forward_(builder.get_parameter<champsim::bandwidth::maximum_type>("max_forward", true, champsim::bandwidth::maximum_type{4})),
        compact_(builder.get_parameter<bool>("compact_tile_bits", true, true)), per_tile_requests_(tiles_.size(), 0)
  {
    if (tiles_.size() != map_.num_tiles()) {
      fmt::print("[{}] ERROR: {} downstream channels declared but nmfc_num_tiles is {}. Routing would address a tile that does not exist.\n", NAME,
                 tiles_.size(), map_.num_tiles());
      std::exit(-1);
    }
    returned_.set_capacity(queue_size_ + static_cast<std::size_t>(champsim::to_underlying(max_forward_)) + 1);
  }

  // ---- upward face: what the cache above sees ----

  bool add_rq(const request_type& packet) override { return accept(packet, queue_kind::RQ); }
  bool add_wq(const request_type& packet) override { return accept(packet, queue_kind::WQ); }
  bool add_pq(const request_type& packet) override { return accept(packet, queue_kind::PQ); }

  [[nodiscard]] std::size_t rq_occupancy() const override { return count_of(queue_kind::RQ); }
  [[nodiscard]] std::size_t wq_occupancy() const override { return count_of(queue_kind::WQ); }
  [[nodiscard]] std::size_t pq_occupancy() const override { return count_of(queue_kind::PQ); }

  [[nodiscard]] std::size_t rq_size() const override { return queue_size_; }
  [[nodiscard]] std::size_t wq_size() const override { return queue_size_; }
  [[nodiscard]] std::size_t pq_size() const override { return queue_size_; }

  // Nothing downstream drains the fabric directly — it forwards into real
  // channels — so these exist to satisfy the interface and stay empty.
  request_queue_type& get_rq() override { return empty_requests_; }
  request_queue_type& get_wq() override { return empty_requests_; }
  request_queue_type& get_pq() override { return empty_requests_; }
  response_queue_type& get_returned() override { return returned_; }

  bool has_pending() override { return !inbound_.empty(); }

  stats_type& get_sim_stats() override { return sim_stats_; }

  // ---- the cycle ----

  long operate() final
  {
    long progress = 0;
    progress += collect_returns();
    progress += forward_requests();
    return progress;
  }

  long poll_cycle() final
  {
    // Work can arrive by external push from either side (the cache above calls
    // add_rq, a slice below pushes a response), so this never skips more than
    // one cycle at a time.
    if (!inbound_.empty() || !in_return_.empty() || !returned_.empty()) {
      return 0;
    }
    const bool any_returns = std::any_of(std::begin(tiles_), std::end(tiles_), [](auto* ch) { return !std::empty(ch->get_returned()); });
    return any_returns ? 0 : 1;
  }

  void begin_phase(bool /*warmup*/) override
  {
    std::fill(std::begin(per_tile_requests_), std::end(per_tile_requests_), 0);
    forwarded_ = 0;
    rejected_ = 0;
    occupancy_high_water_ = inbound_.size();
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (forwarded_ == 0 && rejected_ == 0) {
      return;
    }
    out.line(fmt::format("{} FORWARDED: {} REJECTED: {} PEAK OCCUPANCY: {}", NAME, forwarded_, rejected_, occupancy_high_water_));
    out.line(fmt::format("{} PER-TILE REQUESTS: {}", NAME, fmt::join(per_tile_requests_, " ")));

    auto json = out.json();
    json.add("forwarded", forwarded_);
    json.add("rejected", rejected_);
    json.add("peak_occupancy", occupancy_high_water_);
    json.add("per_tile_requests", per_tile_requests_);
  }

  void print_deadlock() final
  {
    fmt::print("[{}] inbound: {} awaiting-return: {} returned: {}\n", NAME, inbound_.size(), in_return_.size(), returned_.size());
  }

private:
  enum class queue_kind { RQ, WQ, PQ };

  struct pending_request {
    request_type req;
    queue_kind kind;
    std::size_t tile;
    champsim::chrono::clock::time_point due;
  };

  bool accept(const request_type& packet, queue_kind kind)
  {
    if (inbound_.size() >= queue_size_) {
      ++rejected_; // back-pressure: the cache above will retry
      return false;
    }
    const auto tile = map_.tile_of(packet.address);
    inbound_.push_back(pending_request{packet, kind, tile, current_time + hop_latency_});
    if (inbound_.size() > occupancy_high_water_) {
      occupancy_high_water_ = inbound_.size();
    }
    return true;
  }

  [[nodiscard]] std::size_t count_of(queue_kind kind) const
  {
    return static_cast<std::size_t>(std::count_if(std::begin(inbound_), std::end(inbound_), [kind](const auto& entry) { return entry.kind == kind; }));
  }

  /** Move due requests down, compacting the address for the slice below. */
  long forward_requests()
  {
    long progress = 0;
    champsim::bandwidth bw{max_forward_};

    // Requests are due in arrival order, so a not-yet-due head means nothing
    // behind it is due either.
    while (bw.has_remaining() && !inbound_.empty() && inbound_.front().due <= current_time) {
      auto& entry = inbound_.front();
      auto* downstream = tiles_.at(entry.tile);

      auto forwarded = entry.req;
      if (compact_) {
        forwarded.address = map_.compact(entry.req.address);
      }

      const bool accepted = (entry.kind == queue_kind::RQ)   ? downstream->add_rq(forwarded)
                            : (entry.kind == queue_kind::WQ) ? downstream->add_wq(forwarded)
                                                             : downstream->add_pq(forwarded);
      if (!accepted) {
        break; // the slice is full; hold order and retry next cycle
      }

      ++per_tile_requests_.at(entry.tile);
      ++forwarded_;
      inbound_.pop_front();
      bw.consume();
      ++progress;
    }
    return progress;
  }

  /** Drain each slice's responses, undo compaction, and release when due. */
  long collect_returns()
  {
    long progress = 0;

    for (std::size_t tile = 0; tile < tiles_.size(); ++tile) {
      auto& from_slice = tiles_[tile]->get_returned();
      for (const auto& response : from_slice) {
        auto restored = response;
        if (compact_) {
          restored.address = map_.expand(response.address, tile);
        }
        in_return_.push_back(nmfc::in_flight<response_type>{restored, current_time + hop_latency_});
        ++progress;
      }
      from_slice.clear();
    }

    champsim::bandwidth bw{max_forward_};
    while (bw.has_remaining() && !in_return_.empty() && in_return_.front().deliver_at <= current_time) {
      returned_.push_back_grow(in_return_.front().payload);
      in_return_.pop_front();
      bw.consume();
      ++progress;
    }
    return progress;
  }

  std::vector<channel_type*> tiles_;
  nmfc::tile_map map_;
  champsim::chrono::clock::duration hop_latency_;
  std::size_t queue_size_;
  champsim::bandwidth::maximum_type max_forward_;
  bool compact_;

  std::deque<pending_request> inbound_;
  std::deque<nmfc::in_flight<response_type>> in_return_;
  response_queue_type returned_{};
  request_queue_type empty_requests_{};

  stats_type sim_stats_{};

  std::vector<std::uint64_t> per_tile_requests_;
  std::uint64_t forwarded_ = 0;
  std::uint64_t rejected_ = 0;
  std::size_t occupancy_high_water_ = 0;
};

static champsim::modules::channel_module::register_module<interleave_fabric> interleave_fabric_reg("INTERLEAVE_FABRIC");

} // anonymous namespace
