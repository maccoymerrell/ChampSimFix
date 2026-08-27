/*
 * TILE_PORT — a memory tile's own port into its LLC slice.
 *
 * Compute tiles reach a slice through INTERLEAVE_FABRIC, which compacts the
 * tile-select field out of the address so the slice indexes a dense space. A
 * function core sitting on the same tile reaches the same slice directly, so it
 * has to speak the same compacted address space or the two paths would tag the
 * same line differently and never see each other's data. This is the adapter
 * that makes them agree.
 *
 * It also carries a correctness check worth having: every address crossing this
 * port must belong to *this* tile. A function core that issues a request for
 * another tile's address has failed to migrate when it should have, which is
 * exactly the bug class hardest to notice from aggregate statistics. The port
 * counts those and can be told to abort on the first one.
 *
 * Parameters:
 *   tile             which memory tile this port belongs to
 *   lower            @channel into the LLC slice
 *   clock_period     time
 *   latency          cycles each direction (default 1)
 *   queue_size       requests held before refusal (default 32)
 *   max_forward      requests moved per cycle, each direction (default 4)
 *   strict_locality  abort on a foreign address rather than counting it (default true)
 */

#include <algorithm>
#include <cstddef>
#include <deque>
#include <fmt/core.h>

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

class tile_port : public champsim::modules::channel_module, public champsim::operable
{
  using channel_type = champsim::modules::channel_module;

public:
  explicit tile_port(champsim::modules::ModuleBuilder builder)
      : champsim::operable(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")), tile_(builder.get_parameter<std::size_t>("tile")),
        lower_(builder.get_parameter<channel_type*>("lower")), map_(nmfc::tile_map_from(builder)),
        latency_(nmfc::cycles_from(builder, "latency", 1)), queue_size_(builder.get_parameter<std::size_t>("queue_size", true, std::size_t{32})),
        max_forward_(builder.get_parameter<champsim::bandwidth::maximum_type>("max_forward", true, champsim::bandwidth::maximum_type{4})),
        strict_(builder.get_parameter<bool>("strict_locality", true, true))
  {
    returned_.set_capacity(queue_size_ + static_cast<std::size_t>(champsim::to_underlying(max_forward_)) + 1);
  }

  bool add_rq(const request_type& packet) override { return accept(packet, kind::RQ); }
  bool add_wq(const request_type& packet) override { return accept(packet, kind::WQ); }
  bool add_pq(const request_type& packet) override { return accept(packet, kind::PQ); }

  [[nodiscard]] std::size_t rq_occupancy() const override { return inbound_.size(); }
  [[nodiscard]] std::size_t wq_occupancy() const override { return 0; }
  [[nodiscard]] std::size_t pq_occupancy() const override { return 0; }
  [[nodiscard]] std::size_t rq_size() const override { return queue_size_; }
  [[nodiscard]] std::size_t wq_size() const override { return queue_size_; }
  [[nodiscard]] std::size_t pq_size() const override { return queue_size_; }

  request_queue_type& get_rq() override { return empty_; }
  request_queue_type& get_wq() override { return empty_; }
  request_queue_type& get_pq() override { return empty_; }
  response_queue_type& get_returned() override { return returned_; }
  bool has_pending() override { return !inbound_.empty(); }
  stats_type& get_sim_stats() override { return sim_stats_; }

  long operate() final
  {
    long progress = 0;

    // Responses from the slice: undo compaction so the cache above sees the
    // address it asked for.
    auto& from_slice = lower_->get_returned();
    for (const auto& response : from_slice) {
      auto restored = response;
      restored.address = map_.expand(response.address, tile_);
      in_return_.push_back(nmfc::in_flight<response_type>{restored, current_time + latency_});
      ++progress;
    }
    from_slice.clear();

    champsim::bandwidth ret_bw{max_forward_};
    while (ret_bw.has_remaining() && !in_return_.empty() && in_return_.front().deliver_at <= current_time) {
      returned_.push_back_grow(in_return_.front().payload);
      in_return_.pop_front();
      ret_bw.consume();
      ++progress;
    }

    champsim::bandwidth fwd_bw{max_forward_};
    while (fwd_bw.has_remaining() && !inbound_.empty() && inbound_.front().due <= current_time) {
      auto& head = inbound_.front();
      auto forwarded = head.req;
      forwarded.address = map_.compact(head.req.address);

      const bool accepted = (head.k == kind::RQ)   ? lower_->add_rq(forwarded)
                            : (head.k == kind::WQ) ? lower_->add_wq(forwarded)
                                                   : lower_->add_pq(forwarded);
      if (!accepted) {
        break;
      }
      inbound_.pop_front();
      fwd_bw.consume();
      ++progress;
    }
    return progress;
  }

  long poll_cycle() final { return (inbound_.empty() && in_return_.empty() && std::empty(lower_->get_returned())) ? 1 : 0; }

  void begin_phase(bool /*warmup*/) override
  {
    forwarded_ = 0;
    foreign_ = 0;
    rejected_ = 0;
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (forwarded_ == 0 && rejected_ == 0) {
      return;
    }
    out.line(fmt::format("{} TILE {} FORWARDED: {} REJECTED: {} FOREIGN ADDRESSES: {}", NAME, tile_, forwarded_, rejected_, foreign_));
    auto json = out.json();
    json.add("tile", tile_);
    json.add("forwarded", forwarded_);
    json.add("rejected", rejected_);
    json.add("foreign_addresses", foreign_);
  }

  void print_deadlock() final { fmt::print("[{}] tile {} inbound: {} awaiting-return: {}\n", NAME, tile_, inbound_.size(), in_return_.size()); }

private:
  enum class kind { RQ, WQ, PQ };
  struct pending {
    request_type req;
    kind k;
    champsim::chrono::clock::time_point due;
  };

  bool accept(const request_type& packet, kind k)
  {
    if (inbound_.size() >= queue_size_) {
      ++rejected_;
      return false;
    }
    if (map_.tile_of(packet.address) != tile_) {
      ++foreign_;
      if (strict_) {
        fmt::print("[{}] ERROR: address {} belongs to tile {}, not tile {}. A context reached memory without migrating first.\n", NAME, packet.address,
                   map_.tile_of(packet.address), tile_);
        std::exit(-1);
      }
    }
    inbound_.push_back(pending{packet, k, current_time + latency_});
    ++forwarded_;
    return true;
  }

  std::size_t tile_;
  channel_type* lower_;
  nmfc::tile_map map_;
  champsim::chrono::clock::duration latency_;
  std::size_t queue_size_;
  champsim::bandwidth::maximum_type max_forward_;
  bool strict_;

  std::deque<pending> inbound_;
  std::deque<nmfc::in_flight<response_type>> in_return_;
  response_queue_type returned_{};
  request_queue_type empty_{};
  stats_type sim_stats_{};

  std::uint64_t forwarded_ = 0;
  std::uint64_t foreign_ = 0;
  std::uint64_t rejected_ = 0;
};

static champsim::modules::channel_module::register_module<tile_port> tile_port_reg("TILE_PORT");

} // anonymous namespace
