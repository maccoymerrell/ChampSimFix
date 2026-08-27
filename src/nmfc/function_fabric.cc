/*
 * FUNCTION_FABRIC — the default function_fabric model.
 *
 * Three message classes share one set of links, each with its own queue so a
 * stalled class cannot deadlock the others: an invocation that cannot land
 * (target tile full) must not block the return that would free a context on
 * that same tile. That is the one ordering property this model has to get
 * right, and it is why returns are drained before invocations every cycle.
 *
 * Placement lives here. Because a function's copies sit on consecutive grains,
 * choosing tile t means dispatching to entry_pc_base + t * grain, so the policy
 * costs one add on the dispatch path and can be as clever as we like.
 *
 * Parameters:
 *   clock_period       time
 *   hop_latency        cycles per hop (default 8)
 *   queue_size         messages held per class before refusal (default 64)
 *   max_deliver        messages delivered per cycle, per class (default 4)
 *   placement_policy   "round_robin" | "least_loaded" | "first_touch" | "random"
 *   random_seed        seeds the random policy, so runs stay reproducible
 */

#include <algorithm>
#include <set>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include "bandwidth.h"
#include "modules.h"
#include "nmfc/tile_router.h"
#include "nmfc/function_core.h"
#include "nmfc/function_fabric.h"
#include "nmfc/nmfc_config.h"
#include "nmfc/nmfc_hooks.h"
#include "nmfc/nmfc_types.h"
#include "stat_report.h"

namespace
{

nmfc::placement_policy parse_policy(const std::string& name)
{
  if (name == "least_loaded") {
    return nmfc::placement_policy::LEAST_LOADED;
  }
  if (name == "first_touch") {
    return nmfc::placement_policy::FIRST_TOUCH;
  }
  if (name == "random") {
    return nmfc::placement_policy::RANDOM;
  }
  return nmfc::placement_policy::ROUND_ROBIN;
}

class function_fabric : public nmfc::function_fabric_module
{
public:
  explicit function_fabric(champsim::modules::ModuleBuilder builder)
      : nmfc::function_fabric_module(builder.get_parameter<champsim::chrono::picoseconds>("clock_period")), map_(nmfc::tile_map_from(builder)),
        hop_latency_(nmfc::cycles_from(builder, "hop_latency", 8)), queue_size_(builder.get_parameter<std::size_t>("queue_size", true, std::size_t{64})),
        max_deliver_(builder.get_parameter<champsim::bandwidth::maximum_type>("max_deliver", true, champsim::bandwidth::maximum_type{4})),
        policy_(parse_policy(builder.get_parameter<std::string>("placement_policy", true, std::string{"round_robin"}))),
        rng_(builder.get_parameter<std::uint64_t>("random_seed", true, std::uint64_t{0x9E3779B97F4A7C15ULL})),
        router_(builder.get_parameter<nmfc::tile_router_module*>("router")), tiles_(map_.num_tiles(), nullptr),
        per_tile_invocations_(map_.num_tiles(), 0), migrations_(map_.num_tiles())
  {
    // Split the configured buffering across the destinations rather than adding
    // to it, so per-destination queueing is a fairness change and not a capacity
    // increase that would flatter the result.
    per_target_queue_ = std::max<std::size_t>(4, queue_size_ / std::max<std::size_t>(map_.num_tiles(), 1));
  }

  // ---- attachment ----

  void attach_tile(std::size_t index, nmfc::function_core_module* core) override
  {
    if (index >= tiles_.size()) {
      fmt::print("[{}] ERROR: tile index {} is out of range for nmfc_num_tiles {}\n", NAME, index, tiles_.size());
      std::exit(-1);
    }
    if (tiles_[index] != nullptr) {
      fmt::print("[{}] ERROR: two function cores both claim tile {}\n", NAME, index);
      std::exit(-1);
    }
    tiles_[index] = core;
  }

  std::uint32_t attach_host(nmfc::offload_sink* host) override
  {
    hosts_.push_back(host);
    return static_cast<std::uint32_t>(hosts_.size() - 1);
  }

  [[nodiscard]] std::size_t num_tiles() const override { return tiles_.size(); }

  // ---- traffic ----

  bool dispatch(const nmfc::invocation_msg& msg) override
  {
    if (invocations_.size() >= queue_size_) {
      ++dispatch_stalls_;
      return false;
    }
    const auto tile = choose_tile(msg);

    // Selecting the copy IS selecting the tile: the copies sit on consecutive
    // grains, so this add is the whole placement mechanism.
    auto routed = msg;
    routed.entry_pc = champsim::address{msg.entry_pc.to<std::uint64_t>() + static_cast<std::uint64_t>(tile) * map_.grain()};

    invocations_.push_back(entry<nmfc::invocation_msg>{routed, tile, current_time + hop_latency_});
    ++per_tile_invocations_.at(tile);
    ++dispatched_;

    if (nmfc::hooks::invoke.active()) {
      nmfc::hooks::invoke.emit(msg.token, msg.home_host, tile);
    }
    return true;
  }

  bool migrate(nmfc::migration_msg msg) override
  {
    // Every offer, taken or refused, tells us this token wants to move. A
    // refused core retries every cycle, so this set is an exact picture of who
    // is waiting -- which is what makes "the oldest" a machine-wide fact rather
    // than a fact about this queue. Without that, the guarantee below protects
    // whichever context happened to get into the queue, while the genuinely
    // oldest one sits on a full tile that never drains.
    want_migrate_.insert(msg.ctx.token);
    const bool is_oldest = *std::begin(want_migrate_) == msg.ctx.token;
    const auto target = msg.target_tile;
    auto& queue = migrations_.at(target);
    if (queue.size() >= (is_oldest ? per_target_queue_ : per_target_queue_ - 1)) {
      ++migrate_stalls_;
      return false;
    }
    queue.push_back(entry<nmfc::migration_msg>{std::move(msg), target, current_time + hop_latency_});
    ++migrated_;
    return true;
  }

  bool finish(const nmfc::completion_msg& msg) override
  {
    if (completions_.size() >= queue_size_) {
      ++finish_stalls_;
      return false;
    }
    completions_.push_back(entry<nmfc::completion_msg>{msg, msg.home_host, current_time + hop_latency_});
    ++finished_;
    return true;
  }

  // ---- the cycle ----

  long operate() final
  {
    long progress = 0;
    // Returns first: a completion frees a context on some tile, and an
    // invocation waiting for that tile would otherwise spin against a full core
    // that the very message behind it was about to drain.
    progress += deliver_completions();
    progress += deliver_migrations();
    progress += deliver_invocations();
    return progress;
  }

  long poll_cycle() final
  {
    // Fed entirely by external pushes from cores and hosts, so never skip more
    // than a single cycle.
    const bool no_migrations = std::all_of(std::begin(migrations_), std::end(migrations_), [](const auto& q) { return q.empty(); });
    return (invocations_.empty() && no_migrations && completions_.empty()) ? 1 : 0;
  }

  void begin_phase(bool /*warmup*/) override
  {
    dispatched_ = migrated_ = finished_ = 0;
    dispatch_stalls_ = migrate_stalls_ = finish_stalls_ = 0;
    refused_on_arrival_ = 0;
    reserved_deliveries_ = 0;
    std::fill(std::begin(per_tile_invocations_), std::end(per_tile_invocations_), 0);
    peak_queue_ = 0;
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (dispatched_ == 0 && migrated_ == 0 && finished_ == 0) {
      return;
    }
    out.line(fmt::format("{} DISPATCHED: {} MIGRATED: {} RETURNED: {}", NAME, dispatched_, migrated_, finished_));
    out.line(fmt::format("{} STALLS dispatch: {} migrate: {} return: {} REFUSED ON ARRIVAL: {}", NAME, dispatch_stalls_, migrate_stalls_, finish_stalls_,
                         refused_on_arrival_));
    // How often the machine had to fall back on the age guarantee. A large
    // number means the tiles are chronically full, not that anything is wrong.
    out.line(fmt::format("{} RESERVED-SLOT DELIVERIES: {}", NAME, reserved_deliveries_));
    out.line(fmt::format("{} INVOCATIONS PER TILE: {}", NAME, fmt::join(per_tile_invocations_, " ")));

    auto json = out.json();
    json.add("dispatched", dispatched_);
    json.add("migrated", migrated_);
    json.add("returned", finished_);
    json.add("dispatch_stalls", dispatch_stalls_);
    json.add("migrate_stalls", migrate_stalls_);
    json.add("return_stalls", finish_stalls_);
    json.add("refused_on_arrival", refused_on_arrival_);
    json.add("reserved_deliveries", reserved_deliveries_);
    json.add("peak_queue", peak_queue_);
    json.add("invocations_per_tile", per_tile_invocations_);
  }

  void print_deadlock() final
  {
    fmt::print("[{}] invocations: {} migrations: {} completions: {}\n", NAME, invocations_.size(), queued_migrations(), completions_.size());
    // Where the queued migrations are trying to go. If they are all aimed at
    // one full tile, the queue cannot drain no matter how much room the other
    // tiles have -- and the age guarantee cannot help, because the oldest
    // message is aimed at the full tile too.
    fmt::print("[{}] queued migrations by target:", NAME);
    for (std::size_t t = 0; t < migrations_.size(); ++t) {
      fmt::print(" t{}={}/{}", t, migrations_[t].size(), per_target_queue_);
    }
    fmt::print("\n");
    if (!want_migrate_.empty()) {
      fmt::print("[{}] oldest wanting to migrate: token {}, {} tokens waiting\n", NAME, *std::begin(want_migrate_), want_migrate_.size());
    }
    for (std::size_t t = 0; t < tiles_.size(); ++t) {
      if (tiles_[t] != nullptr) {
        fmt::print("[{}]   tile {} free contexts: {}/{}\n", NAME, t, tiles_[t]->free_contexts(), tiles_[t]->num_contexts());
      }
    }
  }

private:
  template <typename Payload>
  struct entry {
    Payload payload{};
    std::size_t target = 0;
    champsim::chrono::clock::time_point deliver_at{};
  };

  [[nodiscard]] std::size_t choose_tile(const nmfc::invocation_msg& msg)
  {
    switch (policy_) {
    case nmfc::placement_policy::LEAST_LOADED: {
      std::size_t best = 0;
      std::size_t best_free = 0;
      for (std::size_t t = 0; t < tiles_.size(); ++t) {
        const auto free_slots = tiles_[t] == nullptr ? 0 : tiles_[t]->free_contexts();
        if (free_slots > best_free) {
          best_free = free_slots;
          best = t;
        }
      }
      return best;
    }
    case nmfc::placement_policy::FIRST_TOUCH: {
      // Land the invocation where its first data access already lives, so the
      // common case costs no migration at all.
      if (msg.body != nullptr) {
        for (const auto& instr : msg.body->instrs) {
          if (instr.num_mem_ops() > 0) {
            return router_->owner_of(msg.origin, instr.mem[0]);
          }
        }
      }
      return next_round_robin();
    }
    case nmfc::placement_policy::RANDOM:
      return static_cast<std::size_t>(rng_() % tiles_.size());
    case nmfc::placement_policy::ROUND_ROBIN:
    default:
      return next_round_robin();
    }
  }

  [[nodiscard]] std::size_t next_round_robin()
  {
    const auto tile = round_robin_;
    round_robin_ = (round_robin_ + 1) % tiles_.size();
    return tile;
  }

  /**
   * Deadlock freedom for migration.
   *
   * A context waiting to migrate still holds its slot on the tile it is
   * leaving, so hold-and-wait is structural: a ring of tiles, each full of
   * contexts bound for another full tile, can never break on its own. Under
   * round-robin dispatch this needs enough work in flight to show up; under
   * first_touch a good partition produces it immediately, because a partition
   * that concentrates related work is exactly one that concentrates arrivals.
   *
   * The rule that fixes it: every tile keeps one context in reserve, and only
   * the *oldest* pending migration in the machine may take it. That invocation
   * can therefore always move; because it can always move it eventually
   * completes; when it completes it frees a slot and the next-oldest inherits
   * the guarantee. New invocations never touch the reserve, so away from the
   * oldest every tile keeps a free slot at all times.
   *
   * Tokens are issued in order, so "oldest" is just the smallest token.
   */
  [[nodiscard]] std::size_t queued_migrations() const
  {
    std::size_t total = 0;
    for (const auto& q : migrations_) {
      total += q.size();
    }
    return total;
  }

  [[nodiscard]] bool has_room(const nmfc::function_core_module* core) const
  {
    return core != nullptr && core->free_contexts() > reserved_contexts_;
  }

  /**
   * Deliver the oldest pending migration even into a tile's reserve, out of
   * queue order if need be -- a FIFO would let a blocked head hide it.
   */
  long deliver_reserved_migration(champsim::bandwidth& bw)
  {
    if (!bw.has_remaining() || want_migrate_.empty()) {
      return 0;
    }
    const auto oldest = *std::begin(want_migrate_);
    for (auto& queue : migrations_) {
      const auto it = std::find_if(std::begin(queue), std::end(queue),
                                   [&](const auto& msg) { return msg.deliver_at <= current_time && msg.payload.ctx.token == oldest; });
      if (it == std::end(queue)) {
        continue;
      }
      auto* core = tiles_.at(it->target);
      if (core == nullptr || !core->accept_migration(it->payload.ctx)) {
        return 0; // genuinely full including the reserve: the oldest holds it itself
      }
      want_migrate_.erase(it->payload.ctx.token);
      queue.erase(it);
      bw.consume();
      ++reserved_deliveries_;
      return 1;
    }
    return 0;
  }

  long deliver_invocations()
  {
    long progress = 0;
    champsim::bandwidth bw{max_deliver_};
    // Same reasoning as migrations: different targets, so the head must not
    // speak for the queue.
    for (auto it = std::begin(invocations_); bw.has_remaining() && it != std::end(invocations_);) {
      auto* core = it->deliver_at <= current_time ? tiles_.at(it->target) : nullptr;
      // A new invocation may never take the reserve; it has no age guarantee
      // and would deny the slot to the one context that does.
      if (core == nullptr || !has_room(core) || !core->accept(it->payload)) {
        if (core != nullptr) {
          ++refused_on_arrival_;
        }
        ++it;
        continue;
      }
      it = invocations_.erase(it);
      bw.consume();
      ++progress;
    }
    track_peak();
    return progress;
  }

  long deliver_migrations()
  {
    long progress = 0;
    champsim::bandwidth bw{max_deliver_};
    progress += deliver_reserved_migration(bw);

    // Do not stop at the head. Messages in this queue are bound for different
    // tiles, so one aimed at a full tile would otherwise stall every message
    // behind it that has somewhere to go -- and since a blocked migration holds
    // a context slot on the tile it is leaving, that turns a busy tile into a
    // permanently stuck one. Deliver whatever is deliverable; the queue is
    // small and bounded, so the scan costs nothing worth saving.
    for (std::size_t n = 0; n < migrations_.size() && bw.has_remaining(); ++n) {
      auto& queue = migrations_[(next_drain_ + n) % migrations_.size()];
      while (bw.has_remaining() && !queue.empty() && queue.front().deliver_at <= current_time) {
        auto& head = queue.front();
        auto* core = tiles_.at(head.target);
        if (!has_room(core) || !core->accept_migration(head.payload.ctx)) {
          ++refused_on_arrival_;
          break; // this destination is full; the other queues are unaffected
        }
        want_migrate_.erase(head.payload.ctx.token);
        queue.pop_front();
        bw.consume();
        ++progress;
      }
    }
    next_drain_ = migrations_.empty() ? 0 : (next_drain_ + 1) % migrations_.size();
    track_peak();
    return progress;
  }

  long deliver_completions()
  {
    long progress = 0;
    champsim::bandwidth bw{max_deliver_};
    while (bw.has_remaining() && !completions_.empty() && completions_.front().deliver_at <= current_time) {
      auto& head = completions_.front();
      if (head.target >= hosts_.size() || hosts_[head.target] == nullptr) {
        fmt::print("[{}] ERROR: completion for host {} but only {} hosts attached\n", NAME, head.target, hosts_.size());
        std::exit(-1);
      }
      // A host always accepts its own returns: the tracking slot it is
      // completing was reserved when the invocation was issued.
      hosts_[head.target]->accept_return(head.payload);
      completions_.pop_front();
      bw.consume();
      ++progress;
    }
    track_peak();
    return progress;
  }

  void track_peak()
  {
    peak_queue_ = std::max({peak_queue_, invocations_.size(), queued_migrations(), completions_.size()});
  }

  nmfc::tile_map map_;
  champsim::chrono::clock::duration hop_latency_;
  std::size_t queue_size_;
  champsim::bandwidth::maximum_type max_deliver_;
  nmfc::tile_router_module* router_;
  nmfc::placement_policy policy_;
  std::size_t reserved_contexts_ = 1;
  std::set<std::uint64_t> want_migrate_; // offered but not yet delivered
  std::uint64_t reserved_deliveries_ = 0;
  std::mt19937_64 rng_;

  std::vector<nmfc::function_core_module*> tiles_;
  std::vector<nmfc::offload_sink*> hosts_;

  std::deque<entry<nmfc::invocation_msg>> invocations_;
  // One queue per destination tile, not one shared queue.
  //
  // A single shared queue lets one congested destination own all of it: a full
  // tile's inbound traffic fills every entry, so contexts on *that* tile cannot
  // enqueue to leave, so it never drains. Measured directly -- 128 of 128
  // entries bound for one tile that had 0 free contexts. Per-destination
  // queueing is the standard answer, and it is what makes the age guarantee
  // below actually hold: the oldest invocation's reserved slot cannot be taken
  // by traffic for a different tile.
  std::vector<std::deque<entry<nmfc::migration_msg>>> migrations_;
  std::size_t per_target_queue_ = 0;
  std::size_t next_drain_ = 0;
  std::deque<entry<nmfc::completion_msg>> completions_;

  std::size_t round_robin_ = 0;

  std::vector<std::uint64_t> per_tile_invocations_;
  std::uint64_t dispatched_ = 0;
  std::uint64_t migrated_ = 0;
  std::uint64_t finished_ = 0;
  std::uint64_t dispatch_stalls_ = 0;
  std::uint64_t migrate_stalls_ = 0;
  std::uint64_t finish_stalls_ = 0;
  std::uint64_t refused_on_arrival_ = 0;
  std::size_t peak_queue_ = 0;
};

static nmfc::function_fabric_module::register_interface function_fabric_iface_reg("function_fabric", "function fabrics");
static nmfc::function_fabric_module::register_module<function_fabric> function_fabric_reg("FUNCTION_FABRIC");

} // anonymous namespace
