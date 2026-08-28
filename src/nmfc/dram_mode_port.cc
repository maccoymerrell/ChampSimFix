/*
 * DRAM_MODE_PORT — carry the mapping-mode tag as far as it is information, and
 * no further.
 *
 * The mode bit sits one position above the top of the DRAM range and says how
 * an address is routed and sliced: page-granular and silo'd on one channel, or
 * block-granular and spread across all of them. Everything above the memory
 * controller needs it. The interleave fabric routes by it, the tile ports
 * compact by it, and the caches tag by it -- which is the entire reason §5.3
 * puts it in the address instead of a table, since a dirty line evicted from a
 * cache has no page-table entry behind it any more.
 *
 * The DRAM does not need it. By the time a request reaches a channel the
 * routing and slicing are done and what is left is a frame number, which the
 * allocator has already made unique across both modes because they are drawn
 * from one pool of physical grains.
 *
 * Leaving it costs correctness differently depending on what is underneath.
 * ChampSim's stock address mapping sizes its row field from lg2(rows) and drops
 * anything above it, so an NMFC-mode address and a STANDARD-mode one at the
 * same frame collapse onto the same row and bank -- invisible while a workload
 * is almost entirely one mode, as every measurement here has been, and wrong
 * as soon as §5.4's mixed page sizes are exercised. ramulator2 decodes the
 * address itself, against a device that has no such row at all.
 *
 * The tag is restored on the way back, because a cache matches a response to
 * its outstanding request by address, and the address it is holding still has
 * the bit.
 *
 * Parameters:
 *   lower       @channel into the memory controller
 *   plus the usual nmfc geometry (nmfc_mode_bit is the one that matters)
 */

#include <cstdint>
#include <unordered_map>

#include "channel.h"
#include "modules.h"
#include "nmfc/nmfc_config.h"
#include "nmfc/tile_map.h"

namespace
{

class dram_mode_port : public champsim::modules::channel_module
{
  using channel_type = champsim::modules::channel_module;

public:
  explicit dram_mode_port(champsim::modules::ModuleBuilder builder)
      : map_(nmfc::tile_map_from(builder)), lower_(builder.get_parameter<channel_type*>("lower"))
  {
  }

  bool add_rq(const request_type& packet) override { return forward(packet, &channel_type::add_rq); }
  bool add_wq(const request_type& packet) override { return forward(packet, &channel_type::add_wq); }
  bool add_pq(const request_type& packet) override { return forward(packet, &channel_type::add_pq); }

  [[nodiscard]] std::size_t rq_occupancy() const override { return lower_->rq_occupancy(); }
  [[nodiscard]] std::size_t wq_occupancy() const override { return lower_->wq_occupancy(); }
  [[nodiscard]] std::size_t pq_occupancy() const override { return lower_->pq_occupancy(); }
  [[nodiscard]] std::size_t rq_size() const override { return lower_->rq_size(); }
  [[nodiscard]] std::size_t wq_size() const override { return lower_->wq_size(); }
  [[nodiscard]] std::size_t pq_size() const override { return lower_->pq_size(); }

  request_queue_type& get_rq() override { return lower_->get_rq(); }
  request_queue_type& get_wq() override { return lower_->get_wq(); }
  request_queue_type& get_pq() override { return lower_->get_pq(); }
  bool has_pending() override { return lower_->has_pending(); }
  stats_type& get_sim_stats() override { return lower_->get_sim_stats(); }

  /** Put the tag back before the cache above tries to match on the address. */
  response_queue_type& get_returned() override
  {
    auto& returned = lower_->get_returned();
    for (auto& response : returned) {
      const auto stripped = response.address.to<std::uint64_t>();
      if (auto it = tagged_.find(stripped); it != std::end(tagged_)) {
        response.address = map_.set_mode(response.address);
        if (--it->second == 0) {
          tagged_.erase(it);
        }
      }
    }
    return returned;
  }

private:
  template <typename Fn>
  bool forward(const request_type& packet, Fn add)
  {
    const auto raw = packet.address.to<std::uint64_t>();
    if (!map_.is_nmfc(raw)) {
      return (lower_->*add)(packet);
    }
    auto stripped = packet;
    stripped.address = map_.strip_mode(packet.address);
    if (!(lower_->*add)(stripped)) {
      return false;
    }
    // Requests to one block merge below, so the count is what says how many
    // responses are still owed the tag.
    ++tagged_[stripped.address.to<std::uint64_t>()];
    return true;
  }

  nmfc::tile_map map_;
  channel_type* lower_;
  std::unordered_map<std::uint64_t, std::uint32_t> tagged_;
};

static champsim::modules::channel_module::register_module<dram_mode_port> dram_mode_port_reg("DRAM_MODE_PORT");

} // anonymous namespace
