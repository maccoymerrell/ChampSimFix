/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cache.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <string_view>
#include <vector>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "bandwidth.h"
#include "champsim.h"
#include "chrono.h"
#include "deadlock.h"
#include "instruction.h"
#include "json_stat_builder.h"
#include "util/algorithm.h"
#include "util/bits.h"
#include "util/span.h"
#include "util/stat_format.h"

CACHE::CACHE(CACHE&& /*other*/) : champsim::modules::cache_module(champsim::chrono::picoseconds{})
{
  assert(false && "CACHE move constructor called, but this is not expected to be used in a way that requires moving. Please report this to the developers.");
}

auto CACHE::operator=(CACHE&& /*other*/) -> CACHE&
{
  assert(false
         && "CACHE move assignment operator called, but this is not expected to be used in a way that requires moving. Please report this to the developers.");
  return *this;
}

CACHE::tag_lookup_type::tag_lookup_type(const request_type& req, bool local_pref, bool skip)
    : address(req.address), v_address(req.v_address), data(req.data), ip(req.ip), instr_id(req.instr_id), pf_metadata(req.pf_metadata), origin(req.origin),
      type(req.type), prefetch_from_this(local_pref), skip_fill(skip), is_translated(req.is_translated), instr_depend_on_me(req.instr_depend_on_me)
{
}

CACHE::tag_lookup_type::tag_lookup_type(request_type&& req, bool local_pref, bool skip)
    : address(req.address), v_address(req.v_address), data(req.data), ip(req.ip), instr_id(req.instr_id), pf_metadata(req.pf_metadata), origin(req.origin),
      type(req.type), prefetch_from_this(local_pref), skip_fill(skip), is_translated(req.is_translated), instr_depend_on_me(std::move(req.instr_depend_on_me))
{
}

CACHE::fill_type::fill_type(const tag_lookup_type& req, champsim::chrono::clock::time_point _time_enqueued)
    : address(req.address), v_address(req.v_address), ip(req.ip), instr_id(req.instr_id), origin(req.origin), type(req.type),
      prefetch_from_this(req.prefetch_from_this), time_enqueued(_time_enqueued), instr_depend_on_me(req.instr_depend_on_me), to_return(req.to_return)
{
}

CACHE::fill_type::fill_type(tag_lookup_type&& req, champsim::chrono::clock::time_point _time_enqueued)
    : address(req.address), v_address(req.v_address), ip(req.ip), instr_id(req.instr_id), origin(req.origin), type(req.type),
      prefetch_from_this(req.prefetch_from_this), time_enqueued(_time_enqueued), instr_depend_on_me(std::move(req.instr_depend_on_me)),
      to_return(std::move(req.to_return))
{
}

CACHE::fill_type CACHE::fill_type::merge(fill_type predecessor, fill_type successor)
{
  std::vector<uint64_t> merged_instr{};
  std::vector<champsim::ring_buffer<response_type>*> merged_return{};

  std::set_union(std::begin(predecessor.instr_depend_on_me), std::end(predecessor.instr_depend_on_me), std::begin(successor.instr_depend_on_me),
                 std::end(successor.instr_depend_on_me), std::back_inserter(merged_instr));
  std::set_union(std::begin(predecessor.to_return), std::end(predecessor.to_return), std::begin(successor.to_return), std::end(successor.to_return),
                 std::back_inserter(merged_return));

  // set the time enqueued to the predecessor unless its a demand into prefetch, in which case we use the successor
  // (hoisted, along with the data promise, ahead of the moves below)
  auto merged_time =
      ((successor.type != access_type::PREFETCH && predecessor.type == access_type::PREFETCH)) ? successor.time_enqueued : predecessor.time_enqueued;
  auto merged_promise = predecessor.data_promise;

  fill_type retval{(successor.type == access_type::PREFETCH) ? std::move(predecessor) : std::move(successor)};

  retval.time_enqueued = merged_time;
  retval.instr_depend_on_me = std::move(merged_instr);
  retval.to_return = std::move(merged_return);
  retval.data_promise = merged_promise;

  if constexpr (champsim::debug_print) {
    if (successor.type == access_type::PREFETCH) {
      fmt::print("[MSHR] {} address {} type: {} into address {} type: {}\n", __func__, successor.address,
                 access_type_names.at(champsim::to_underlying(successor.type)), predecessor.address,
                 access_type_names.at(champsim::to_underlying(successor.type)));
    } else {
      fmt::print("[MSHR] {} address {} type: {} into address {} type: {}\n", __func__, predecessor.address,
                 access_type_names.at(champsim::to_underlying(predecessor.type)), successor.address,
                 access_type_names.at(champsim::to_underlying(successor.type)));
    }
  }

  return retval;
}

auto CACHE::fill_block(fill_type fill, uint32_t metadata) -> BLOCK
{
  CACHE::BLOCK to_fill;
  to_fill.valid = true;
  to_fill.prefetch = fill.prefetch_from_this;
  to_fill.dirty = (fill.type == access_type::WRITE);
  to_fill.address = fill.address;
  to_fill.v_address = fill.v_address;
  to_fill.data = fill.data_promise->data;
  to_fill.pf_metadata = metadata;

  return to_fill;
}

auto CACHE::matches_address(champsim::address addr) const
{
  // Compare raw shifted 64-bit values (== equality of the upper slices) to avoid
  // constructing a dynamic-extent address_slice per scanned entry — this runs
  // against every MSHR and inflight-fill entry on every miss.
  const auto shamt = champsim::to_underlying(OFFSET_BITS);
  return [match = addr.to<uint64_t>() >> shamt, shamt](const auto& entry) {
    return (entry.address.template to<uint64_t>() >> shamt) == match;
  };
}

template <typename T>
champsim::address CACHE::module_address(const T& element) const
{
  auto address = virtual_prefetch ? element.v_address : element.address;
  return champsim::address{address.slice_upper(match_offset_bits ? champsim::data::bits{} : OFFSET_BITS)};
}

bool CACHE::handle_fill(const fill_type& fill)
{
  last_served_origin = fill.origin;

  // find victim
  auto [set_begin, set_end] = get_set_span(fill.address);
  auto way = std::find_if_not(set_begin, set_end, [](auto x) { return x.valid; });
  if (way == set_end) {
    way = std::next(set_begin, impl_find_victim(fill.origin, fill.instr_id, get_set_index(fill.address), &*set_begin, fill.ip, fill.address, fill.type));
  }
  assert(set_begin <= way);
  assert(way <= set_end);
  assert(way != set_end || fill.type != access_type::WRITE); // Writes may not bypass
  const auto way_idx = std::distance(set_begin, way);        // cast protected by earlier assertion

  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} instr_id: {} address: {} v_address: {} set: {} way: {} type: {} prefetch_metadata: {} cycle_enqueued: {} cycle: {}\n", NAME, __func__,
               fill.instr_id, fill.address, fill.v_address, get_set_index(fill.address), way_idx, access_type_names.at(champsim::to_underlying(fill.type)),
               fill.data_promise->pf_metadata, (fill.time_enqueued.time_since_epoch()) / clock_period, (current_time.time_since_epoch()) / clock_period);
  }

  if (way != set_end && way->valid && way->dirty) {
    request_type writeback_packet;

    writeback_packet.origin = fill.origin;
    writeback_packet.address = way->address;
    writeback_packet.data = way->data;
    writeback_packet.instr_id = fill.instr_id;
    writeback_packet.ip = champsim::address{};
    writeback_packet.type = access_type::WRITE;
    writeback_packet.pf_metadata = way->pf_metadata;
    writeback_packet.response_requested = false;

    if constexpr (champsim::debug_print) {
      fmt::print("[{}] {} evict address: {} v_address: {} prefetch_metadata: {}\n", NAME, __func__, writeback_packet.address, writeback_packet.v_address,
                 fill.data_promise->pf_metadata);
    }

    auto success = lower_level->add_wq(writeback_packet);
    if (!success) {
      return false;
    }
  }

  champsim::address evicting_address{};
  if (way != set_end && way->valid) {
    evicting_address = module_address(*way);
  }

  auto metadata_thru = impl_prefetcher_cache_fill(module_address(fill), get_set_index(fill.address), way_idx, (fill.type == access_type::PREFETCH),
                                                  evicting_address, fill.data_promise->pf_metadata);
  impl_replacement_cache_fill(fill.origin, get_set_index(fill.address), way_idx, module_address(fill), fill.ip, evicting_address, fill.type);

  if (way != set_end) {
    if (way->valid && way->prefetch) {
      ++sim_stats.pf_useless;
    }

    if (fill.type == access_type::PREFETCH) {
      ++sim_stats.pf_fill;
    }

    *way = fill_block(fill, metadata_thru);
  }

  // COLLECT STATS
  if (fill.type != access_type::PREFETCH)
    sim_stats.total_miss_latency_cycles += (current_time - (fill.time_enqueued + clock_period)) / clock_period;
  sim_stats.fill.increment(std::pair{fill.type, fill.origin.cpu()});

  response_type response{fill.address, fill.v_address, fill.data_promise->data, metadata_thru, fill.instr_depend_on_me};
  for (auto* ret : fill.to_return) {
    ret->push_back_grow(response);
  }

  return true;
}

bool CACHE::try_hit(const tag_lookup_type& handle_pkt)
{
  last_served_origin = handle_pkt.origin;

  // access cache
  auto [set_begin, set_end] = get_set_span(handle_pkt.address);
  auto way = std::find_if(set_begin, set_end, [matcher = matches_address(handle_pkt.address)](const auto& x) { return x.valid && matcher(x); });
  const auto hit = (way != set_end);
  const auto useful_prefetch = (hit && way->prefetch && !handle_pkt.prefetch_from_this);

  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} instr_id: {} address: {} v_address: {} data: {} set: {} way: {} ({}) type: {} cycle: {}\n", NAME, __func__, handle_pkt.instr_id,
               handle_pkt.address, handle_pkt.v_address, handle_pkt.data, get_set_index(handle_pkt.address), std::distance(set_begin, way),
               hit ? "HIT" : "MISS", access_type_names.at(champsim::to_underlying(handle_pkt.type)), current_time.time_since_epoch() / clock_period);
  }

  auto metadata_thru = handle_pkt.pf_metadata;
  if (should_activate_prefetcher(handle_pkt)) {
    metadata_thru = impl_prefetcher_cache_operate(module_address(handle_pkt), handle_pkt.ip, hit, useful_prefetch, handle_pkt.type, metadata_thru);
  }

  // update replacement policy
  const auto way_idx = std::distance(set_begin, way);
  impl_update_replacement_state(handle_pkt.origin, get_set_index(handle_pkt.address), way_idx, module_address(handle_pkt), handle_pkt.ip, {}, handle_pkt.type,
                                hit);

  if (hit) {
    sim_stats.hits.increment(std::pair{handle_pkt.type, handle_pkt.origin.cpu()});

    response_type response{handle_pkt.address, handle_pkt.v_address, way->data, metadata_thru, handle_pkt.instr_depend_on_me};
    for (auto* ret : handle_pkt.to_return) {
      ret->push_back_grow(response);
    }

    way->dirty |= (handle_pkt.type == access_type::WRITE);

    // update prefetch stats and reset prefetch bit
    if (useful_prefetch) {
      ++sim_stats.pf_useful;
      way->prefetch = false;
    }
  }

  return hit;
}

auto CACHE::forward_packet(const tag_lookup_type& handle_pkt) -> request_type
{
  request_type fwd_pkt;

  fwd_pkt.origin = handle_pkt.origin;
  fwd_pkt.type = (handle_pkt.type == access_type::WRITE) ? access_type::RFO : handle_pkt.type;
  fwd_pkt.pf_metadata = handle_pkt.pf_metadata;

  fwd_pkt.address = handle_pkt.address;
  fwd_pkt.v_address = handle_pkt.v_address;
  fwd_pkt.data = handle_pkt.data;
  fwd_pkt.instr_id = handle_pkt.instr_id;
  fwd_pkt.ip = handle_pkt.ip;

  fwd_pkt.instr_depend_on_me = handle_pkt.instr_depend_on_me;
  fwd_pkt.response_requested = (!handle_pkt.prefetch_from_this || !handle_pkt.skip_fill);

  return fwd_pkt;
}

bool CACHE::handle_miss(tag_lookup_type& handle_pkt)
{
  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} instr_id: {} address: {} v_address: {} type: {} local_prefetch: {} cycle: {}\n", NAME, __func__, handle_pkt.instr_id,
               handle_pkt.address, handle_pkt.v_address, access_type_names.at(champsim::to_underlying(handle_pkt.type)), handle_pkt.prefetch_from_this,
               current_time.time_since_epoch() / clock_period);
  }

  last_served_origin = handle_pkt.origin;

  // Check MSHR, then inflight fills. Ring iterators compare by logical position
  // and must not be compared across containers, so carry the found entry as a
  // pointer (stable: neither container mutates before the merge below).
  fill_type* fill_entry = nullptr;
  if (auto mshr_it = std::find_if(std::begin(MSHR), std::end(MSHR), matches_address(handle_pkt.address)); mshr_it != std::end(MSHR)) {
    fill_entry = &*mshr_it;
  } else if (auto inflight_it = std::find_if(std::begin(inflight_fills), std::end(inflight_fills), matches_address(handle_pkt.address));
             inflight_it != std::end(inflight_fills)) {
    fill_entry = &*inflight_it;
  }
  bool mshr_full = (MSHR.size() == MSHR_SIZE);

  // On success the tag entry is consumed (caller erases it), so its vectors are
  // moved into the fill and only scalar fields are read afterward; failure paths
  // return before any move, leaving the entry intact for retry.
  if (fill_entry != nullptr) // miss or fill already inflight
  {
    if (fill_entry->type == access_type::PREFETCH && handle_pkt.type != access_type::PREFETCH) {
      // Mark the prefetch as useful
      if (fill_entry->prefetch_from_this) {
        ++sim_stats.pf_useful;
      }
    }

    // COLLECT STATS
    sim_stats.miss_merge.increment(std::pair{handle_pkt.type, handle_pkt.origin.cpu()});

    *fill_entry = fill_type::merge(std::move(*fill_entry), fill_type{std::move(handle_pkt), current_time});
  } else {
    if (mshr_full) { // not enough MSHR resource
      return false;  // TODO should we allow prefetches anyway if they will not be filled to this level?
    }

    const bool send_to_rq = (prefetch_as_load || handle_pkt.type != access_type::PREFETCH);
    auto fwd_pkt = forward_packet(handle_pkt);
    bool success = send_to_rq ? lower_level->add_rq(fwd_pkt) : lower_level->add_pq(fwd_pkt);

    if (!success) {
      return false;
    }

    // Allocate an MSHR
    if (fwd_pkt.response_requested) {
      MSHR.emplace_back(fill_type{std::move(handle_pkt), current_time});
    }
  }

  sim_stats.misses.increment(std::pair{handle_pkt.type, handle_pkt.origin.cpu()});

  return true;
}

bool CACHE::handle_write(tag_lookup_type& handle_pkt)
{
  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} instr_id: {} address: {} v_address: {} type: {} local_prefetch: {} cycle: {}\n", NAME, __func__, handle_pkt.instr_id,
               handle_pkt.address, handle_pkt.v_address, access_type_names.at(champsim::to_underlying(handle_pkt.type)), handle_pkt.prefetch_from_this,
               current_time.time_since_epoch() / clock_period);
  }

  fill_type to_allocate{std::move(handle_pkt), current_time}; // entry is consumed by the caller
  to_allocate.data_promise.ready_at(current_time + (is_warmup() ? champsim::chrono::clock::duration{} : FILL_LATENCY));
  inflight_fills.push_back_grow(std::move(to_allocate));

  sim_stats.misses.increment(std::pair{handle_pkt.type, handle_pkt.origin.cpu()}); // scalars, unaffected by the move

  return true;
}

template <bool UpdateRequest>
auto CACHE::initiate_tag_check(champsim::modules::channel_module* ul)
{
  return [this, time = current_time + (is_warmup() ? champsim::chrono::clock::duration{} : HIT_LATENCY), ul](auto& entry) {
    // The transformed entry is erased from its queue immediately after this
    // functor runs, so its vectors can be moved rather than copied.
    [[maybe_unused]] bool response_requested = false;
    if constexpr (UpdateRequest) {
      response_requested = entry.response_requested;
    }
    CACHE::tag_lookup_type retval{std::move(entry)};
    retval.event_cycle = time;

    if constexpr (UpdateRequest) {
      if (response_requested) {
        retval.to_return = {&ul->get_returned()};
      }
    } else {
      (void)ul; // supress warning about ul being unused
    }

    if constexpr (champsim::debug_print) {
      fmt::print("[TAG] initiate_tag_check instr_id: {} address: {} v_address: {} type: {} response_requested: {}\n", retval.instr_id, retval.address,
                 retval.v_address, access_type_names.at(champsim::to_underlying(retval.type)), !std::empty(retval.to_return));
    }

    // event_cycle is provisionally stamped for the translated fast path (timed
    // from admission); admit_tag_check overrides it for untranslated entries.
    return retval;
  };
}

void CACHE::admit_tag_check(tag_lookup_type&& entry)
{
  if (lower_translate != nullptr && !entry.is_translated) {
    // Untranslated arrival at a physically-indexed cache: the tag check can't
    // begin until translation yields the physical set index. Park with no
    // ready-time; finish_translation stamps event_cycle and moves it into the
    // timed pipeline. translate_issued is false at construction, so it always
    // owes a translation request.
    entry.event_cycle = champsim::chrono::clock::time_point::max();
    ++untranslated_pending_issue_;
    untranslated_tag_check.push_back_grow(std::move(entry));
  } else {
    // Already translated (or a non-translating cache such as a TLB): the tag
    // check is timed from admission, event_cycle already == now + HIT_LATENCY.
    inflight_tag_check.push_back_grow(std::move(entry));
  }
}

long CACHE::poll_cycle()
{
  // Skip a cycle only when nothing is pending anywhere: no responses to
  // finish, no inflight work, and no requests waiting on any upper channel.
  // MSHR-only-pending state is skippable — the wake event is an arrival on
  // lower_level->get_returned(), which is re-checked here every cycle.
  const bool idle = std::empty(lower_level->get_returned()) && (lower_translate == nullptr || std::empty(lower_translate->get_returned()))
                    && std::empty(inflight_fills) && std::empty(inflight_tag_check) && std::empty(untranslated_tag_check) && std::empty(internal_PQ)
                    && std::all_of(std::cbegin(upper_levels), std::cend(upper_levels),
                                   [](auto* ul) { return std::empty(ul->get_rq()) && std::empty(ul->get_wq()) && std::empty(ul->get_pq()); });
  if (!idle) {
    return 0;
  }

  // Per-cycle bookkeeping that operate() would have done must still happen on
  // skipped cycles so the observable cycle stream is unchanged:
  //  - the upper-level round-robin keeps its arbitration alignment, and
  //  - prefetchers keep their contractual once-per-cycle hook (internal
  //    clocks, lookahead machines). A prefetch issued here lands in
  //    internal_PQ, so the next poll returns 0 — the same first
  //    tag-check cycle it would get without skipping.
  if (std::size(upper_levels) > 1) {
    std::rotate(upper_levels.begin(), upper_levels.begin() + 1, upper_levels.end());
  }
  impl_prefetcher_cycle_operate();
  return 1;
}

long CACHE::operate()
{
  long progress{0};

  auto is_ready = [time = current_time](const auto& entry) {
    return entry.event_cycle <= time;
  };

  // Finish returns
  std::for_each(std::cbegin(lower_level->get_returned()), std::cend(lower_level->get_returned()), [this](const auto& pkt) { this->finish_packet(pkt); });
  progress += std::distance(std::cbegin(lower_level->get_returned()), std::cend(lower_level->get_returned()));
  lower_level->get_returned().clear();

  // Finish translations
  if (lower_translate != nullptr) {
    std::for_each(std::cbegin(lower_translate->get_returned()), std::cend(lower_translate->get_returned()),
                  [this](const auto& pkt) { this->finish_translation(pkt); });
    progress += std::distance(std::cbegin(lower_translate->get_returned()), std::cend(lower_translate->get_returned()));
    lower_translate->get_returned().clear();
  }

  // Perform fills: retire the ready prefix (promise ready by current_time) up to
  // MAX_FILL, stopping before the first fill handle_fill declines.
  champsim::bandwidth fill_bw{MAX_FILL};
  inflight_fills.drain_ready(current_time, fill_bw, [this](const auto& x) { return this->handle_fill(x); });

  // Initiate tag checks: pull ready entries from the upper-level queues and
  // internal_PQ and route each via admit_tag_check. The window limit bounds only
  // the timed pipeline (inflight_tag_check); untranslated entries stage
  // separately, throttled by their own occupancy below.
  const champsim::bandwidth::maximum_type bandwidth_from_tag_checks{tag_check_window_limit_ - (long)std::size(inflight_tag_check)};
  champsim::bandwidth initiate_tag_bw{std::clamp(bandwidth_from_tag_checks, champsim::bandwidth::maximum_type{0}, MAX_TAG)};
  tag_check_router router{this};
  // Throttle NEW untranslated admissions once the staging buffer hits MSHR_SIZE
  // (backpressure on slow translation); already-translated entries (and every
  // entry of a non-translating cache) are always admissible. Reading live
  // occupancy keeps the buffer bounded across the source queues drained here.
  auto can_admit = [this](const auto& entry) {
    return entry.is_translated || lower_translate == nullptr || std::size(untranslated_tag_check) < static_cast<std::size_t>(MSHR_SIZE);
  };
  [[maybe_unused]] std::vector<long long> channels_bandwidth_consumed{};

  if (std::size(upper_levels) > 1) {
    std::rotate(upper_levels.begin(), upper_levels.begin() + 1, upper_levels.end());
  }

  // Upper levels split the remaining bandwidth equally. Guard the intake so the
  // transform machinery only runs when some upper has a pending request; when
  // all are idle this is a few O(1) has_pending() checks. Byte-identical: an
  // empty queue admits nothing and consumes no bandwidth.
  // Boundary between entries parked before this cycle and those admitted below.
  // develop issues this cycle's fresh admissions before older retries, so match
  // that order when the translation RQ is bandwidth-limited.
  const auto pre_admit_untranslated = std::size(untranslated_tag_check);
  if (std::any_of(std::begin(upper_levels), std::end(upper_levels), [](auto* ul) { return ul->has_pending(); })) {
    const champsim::bandwidth::maximum_type per_upper_bandwidth =
        std::size(upper_levels) >= 1
            ? (champsim::bandwidth::maximum_type)std::max((size_t)initiate_tag_bw.amount_remaining() / std::size(upper_levels), size_t{1})
            : champsim::bandwidth::maximum_type{};

    for (auto* ul : upper_levels) {
      for (auto q : {std::ref(ul->get_wq()), std::ref(ul->get_rq()), std::ref(ul->get_pq())}) {
        // this needs to be in this loop, we need to ensure that for cases where bandwidth doesn't divide nicely across upstreams,
        // we don't accidentally consume more bandwidth than expected
        champsim::bandwidth per_upper_tag_bw{std::min(per_upper_bandwidth, champsim::bandwidth::maximum_type{initiate_tag_bw.amount_remaining()})};
        auto bandwidth_consumed = champsim::transform_while_n(q.get(), router, per_upper_tag_bw, can_admit, initiate_tag_check<true>(ul));
        if constexpr (champsim::debug_print) {
          channels_bandwidth_consumed.push_back(bandwidth_consumed);
        }
        initiate_tag_bw.consume(bandwidth_consumed);
      }
    }
  }

  auto pq_bandwidth_consumed = champsim::transform_while_n(internal_PQ, router, initiate_tag_bw, can_admit, initiate_tag_check<false>());
  initiate_tag_bw.consume(pq_bandwidth_consumed);

  // Issue translations for parked entries. untranslated_pending_issue_ counts
  // exactly the entries this walk acts on, so when it is zero the walk is a
  // no-op.
  if (lower_translate != nullptr && untranslated_pending_issue_ > 0) {
    for (std::size_t i = pre_admit_untranslated; i < std::size(untranslated_tag_check); ++i) {
      issue_translation(untranslated_tag_check[i]);
    }
    for (std::size_t i = 0; i < pre_admit_untranslated; ++i) {
      issue_translation(untranslated_tag_check[i]);
    }
  }

  // Perform tag checks. inflight_tag_check is translated-only and time-ordered,
  // so readiness is event_cycle <= now and ready entries are a front prefix.
  // The assert smoke-tests the translated-only invariant.
  assert(std::empty(inflight_tag_check) || inflight_tag_check.front().is_translated);
  auto do_handle_miss = [this](auto& pkt) {
    if (pkt.type == access_type::WRITE && !this->match_offset_bits) {
      return this->handle_write(pkt); // Treat writes (that is, writebacks) like fills
    }
    return this->handle_miss(pkt); // Treat writes (that is, stores) like reads
  };
  champsim::bandwidth tag_check_bw{MAX_TAG};
  auto [tag_check_ready_begin, tag_check_ready_end] =
      champsim::get_span_p(std::begin(inflight_tag_check), std::end(inflight_tag_check), tag_check_bw, is_ready);
  auto hits_end = champsim::stable_partition_small(tag_check_ready_begin, tag_check_ready_end, [this](const auto& pkt) { return this->try_hit(pkt); });
  auto finish_tag_check_end = champsim::stable_partition_small(hits_end, tag_check_ready_end, do_handle_miss);
  tag_check_bw.consume(std::distance(tag_check_ready_begin, finish_tag_check_end));
  inflight_tag_check.erase(tag_check_ready_begin, finish_tag_check_end);

  impl_prefetcher_cycle_operate();

  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} cycle completed: {} tags checked: {} remaining: {} untranslated: {} channel consumed: {} pq consumed {} unused consume bw {}\n", NAME,
               __func__, current_time.time_since_epoch() / clock_period, tag_check_bw.amount_consumed(), std::size(inflight_tag_check),
               std::size(untranslated_tag_check), channels_bandwidth_consumed, pq_bandwidth_consumed, initiate_tag_bw.amount_remaining());
  }

  return progress + fill_bw.amount_consumed() + initiate_tag_bw.amount_consumed() + tag_check_bw.amount_consumed();
}

// LCOV_EXCL_START exclude deprecated function
uint64_t CACHE::get_set(uint64_t address) const { return static_cast<uint64_t>(get_set_index(champsim::address{address})); }
// LCOV_EXCL_STOP

long CACHE::get_set_index(champsim::address address) const
{
  // Physical set index: the lg2(NUM_SET) bits above OFFSET_BITS, via a
  // precomputed shift+mask since this runs on every tag check, fill, and
  // replacement update.
  return static_cast<long>((address.to<uint64_t>() >> set_index_shift_) & set_index_mask_);
}

template <typename It>
std::pair<It, It> get_span(It anchor, typename std::iterator_traits<It>::difference_type set_idx, typename std::iterator_traits<It>::difference_type num_way)
{
  auto begin = std::next(anchor, set_idx * num_way);
  return {std::move(begin), std::next(begin, num_way)};
}

auto CACHE::get_set_span(champsim::address address) -> std::pair<set_type::iterator, set_type::iterator>
{
  const auto set_idx = get_set_index(address);
  assert(set_idx < NUM_SET);
  return get_span(std::begin(block), static_cast<set_type::difference_type>(set_idx), NUM_WAY); // safe cast because of prior assert
}

auto CACHE::get_set_span(champsim::address address) const -> std::pair<set_type::const_iterator, set_type::const_iterator>
{
  const auto set_idx = get_set_index(address);
  assert(set_idx < NUM_SET);
  return get_span(std::cbegin(block), static_cast<set_type::difference_type>(set_idx), NUM_WAY); // safe cast because of prior assert
}

// LCOV_EXCL_START exclude deprecated function
uint64_t CACHE::get_way(uint64_t address, uint64_t /*unused set index*/) const
{
  champsim::address intern_addr{address};
  auto [begin, end] = get_set_span(intern_addr);
  return static_cast<uint64_t>(std::distance(begin, std::find_if(begin, end, matches_address(champsim::address{address}))));
}
// LCOV_EXCL_STOP

long CACHE::invalidate_entry(champsim::address inval_addr)
{
  auto [begin, end] = get_set_span(inval_addr);
  auto inv_way = std::find_if(begin, end, matches_address(inval_addr));

  if (inv_way != end) {
    inv_way->valid = false;
  }

  return std::distance(begin, inv_way);
}

bool CACHE::prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t prefetch_metadata)
{
  ++sim_stats.pf_requested;

  if (std::size(internal_PQ) >= PQ_SIZE) {
    return false;
  }

  request_type pf_packet;
  pf_packet.type = access_type::PREFETCH;
  pf_packet.pf_metadata = prefetch_metadata;
  pf_packet.origin = last_served_origin;
  pf_packet.address = pf_addr;
  pf_packet.v_address = virtual_prefetch ? pf_addr : champsim::address{};
  pf_packet.is_translated = !virtual_prefetch;

  internal_PQ.emplace_back(pf_packet, true, !fill_this_level);
  ++sim_stats.pf_issued;

  return true;
}

// LCOV_EXCL_START exclude deprecated function
bool CACHE::prefetch_line(uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata)
{
  return prefetch_line(champsim::address{pf_addr}, fill_this_level, prefetch_metadata);
}

bool CACHE::prefetch_line(uint64_t /*deprecated*/, uint64_t /*deprecated*/, uint64_t pf_addr, bool fill_this_level, uint32_t prefetch_metadata)
{
  return prefetch_line(champsim::address{pf_addr}, fill_this_level, prefetch_metadata);
}
// LCOV_EXCL_STOP

void CACHE::finish_packet(const response_type& packet)
{
  // check MSHR information
  auto mshr_entry = std::find_if(std::begin(MSHR), std::end(MSHR), matches_address(packet.address));

  // sanity check
  if (mshr_entry == MSHR.end()) {
    fmt::print(stderr, "[{}_MSHR] {} cannot find a matching entry! address: {} v_address: {}\n", NAME, __func__, packet.address, packet.v_address);
    assert(0);
  }

  // MSHR holds the most updated information about this request
  fill_type::returned_value finished_value{packet.data, packet.pf_metadata};
  mshr_entry->data_promise = champsim::waitable{finished_value, current_time + (is_warmup() ? champsim::chrono::clock::duration{} : FILL_LATENCY)};
  if constexpr (champsim::debug_print) {
    fmt::print("[{}_MSHR] finish_packet instr_id: {} address: {} data: {} type: {} current: {}\n", this->NAME, mshr_entry->instr_id, mshr_entry->address,
               mshr_entry->data_promise->data, access_type_names.at(champsim::to_underlying(mshr_entry->type)), current_time.time_since_epoch() / clock_period);
  }

  std::iter_swap(mshr_entry, std::begin(MSHR));
  inflight_fills.push_back_grow(std::move(MSHR.front()));
  MSHR.pop_front();
}

void CACHE::finish_translation(const response_type& packet)
{
  auto matches_vpage = [page_num = champsim::page_number{packet.v_address}](const auto& entry) {
    return (champsim::page_number{entry.v_address} == page_num) && !entry.is_translated;
  };
  // A physically-indexed tag check cannot begin until translation resolves the
  // physical set index, so time it from translation completion, not admission.
  const auto tag_check_ready = current_time + (is_warmup() ? champsim::chrono::clock::duration{} : HIT_LATENCY);
  auto complete_translation = [p_page = champsim::page_number{packet.data}, tag_check_ready, this](auto& entry) {
    [[maybe_unused]] auto old_address = entry.address;
    entry.address = champsim::address{champsim::splice(p_page, champsim::page_offset{entry.v_address})}; // translated address
    entry.is_translated = true;                                                                          // This entry is now translated
    entry.event_cycle = tag_check_ready;                                                                 // serialize: tag check waits for translation
    if (!entry.translate_issued) {
      --untranslated_pending_issue_; // translation piggybacked before this entry issued its own
    }

    if constexpr (champsim::debug_print) {
      fmt::print("[{}_TRANSLATE] finish_translation old: {} paddr: {} vaddr: {} type: {} cycle: {} ready: {}\n", this->NAME, old_address, entry.address,
                 entry.v_address, access_type_names.at(champsim::to_underlying(entry.type)), this->current_time.time_since_epoch() / this->clock_period,
                 entry.event_cycle.time_since_epoch() / this->clock_period);
    }
  };

  // Group every parked entry whose page just resolved to the front, complete it,
  // and move it into inflight_tag_check. Both operations are order-preserving and
  // every append is stamped current_time + HIT_LATENCY (nondecreasing across
  // cycles), so inflight_tag_check stays in event_cycle order.
  auto matched_end = champsim::stable_partition_small(std::begin(untranslated_tag_check), std::end(untranslated_tag_check), matches_vpage);
  std::for_each(std::begin(untranslated_tag_check), matched_end, [&](auto& entry) {
    complete_translation(entry);
    inflight_tag_check.push_back_grow(std::move(entry));
  });
  untranslated_tag_check.erase(std::begin(untranslated_tag_check), matched_end);
}

void CACHE::issue_translation(tag_lookup_type& q_entry)
{
  if (!q_entry.translate_issued && !q_entry.is_translated) {
    request_type fwd_pkt;
    fwd_pkt.origin = q_entry.origin;
    fwd_pkt.type = access_type::LOAD;

    fwd_pkt.address = q_entry.address;
    fwd_pkt.v_address = q_entry.v_address;
    fwd_pkt.data = q_entry.data;
    fwd_pkt.instr_id = q_entry.instr_id;
    fwd_pkt.ip = q_entry.ip;

    fwd_pkt.instr_depend_on_me = q_entry.instr_depend_on_me;
    fwd_pkt.is_translated = true;

    q_entry.translate_issued = lower_translate->add_rq(fwd_pkt);
    if (q_entry.translate_issued) {
      --untranslated_pending_issue_; // no longer awaiting issue (still awaiting the response)
    }
    if constexpr (champsim::debug_print) {
      if (q_entry.translate_issued) {
        fmt::print("[TRANSLATE] do_issue_translation instr_id: {} paddr: {} vaddr: {} type: {}\n", q_entry.instr_id, q_entry.address, q_entry.v_address,
                   access_type_names.at(champsim::to_underlying(q_entry.type)));
      }
    }
  }
}

std::size_t CACHE::get_mshr_occupancy() const { return std::size(MSHR); }

std::vector<std::size_t> CACHE::get_rq_occupancy() const
{
  std::vector<std::size_t> retval;
  std::transform(std::begin(upper_levels), std::end(upper_levels), std::back_inserter(retval), [](auto ulptr) { return ulptr->rq_occupancy(); });
  return retval;
}

std::vector<std::size_t> CACHE::get_wq_occupancy() const
{
  std::vector<std::size_t> retval;
  std::transform(std::begin(upper_levels), std::end(upper_levels), std::back_inserter(retval), [](auto ulptr) { return ulptr->wq_occupancy(); });
  return retval;
}

std::vector<std::size_t> CACHE::get_pq_occupancy() const
{
  std::vector<std::size_t> retval;
  std::transform(std::begin(upper_levels), std::end(upper_levels), std::back_inserter(retval), [](auto ulptr) { return ulptr->pq_occupancy(); });
  retval.push_back(std::size(internal_PQ));
  return retval;
}

// LCOV_EXCL_START exclude deprecated function
std::size_t CACHE::get_occupancy(uint8_t queue_type, uint64_t /*deprecated*/) const
{
  if (queue_type == 0) {
    return get_mshr_occupancy();
  }
  return 0;
}

std::size_t CACHE::get_occupancy(uint8_t queue_type, champsim::address /*deprecated*/) const
{
  if (queue_type == 0) {
    return get_mshr_occupancy();
  }
  return 0;
}
// LCOV_EXCL_STOP

std::size_t CACHE::get_mshr_size() const { return MSHR_SIZE; }
std::vector<std::size_t> CACHE::get_rq_size() const
{
  std::vector<std::size_t> retval;
  std::transform(std::begin(upper_levels), std::end(upper_levels), std::back_inserter(retval), [](auto ulptr) { return ulptr->rq_size(); });
  return retval;
}

std::vector<std::size_t> CACHE::get_wq_size() const
{
  std::vector<std::size_t> retval;
  std::transform(std::begin(upper_levels), std::end(upper_levels), std::back_inserter(retval), [](auto ulptr) { return ulptr->wq_size(); });
  return retval;
}

std::vector<std::size_t> CACHE::get_pq_size() const
{
  std::vector<std::size_t> retval;
  std::transform(std::begin(upper_levels), std::end(upper_levels), std::back_inserter(retval), [](auto ulptr) { return ulptr->pq_size(); });
  retval.push_back(PQ_SIZE);
  return retval;
}

// LCOV_EXCL_START exclude deprecated function
std::size_t CACHE::get_size(uint8_t queue_type, champsim::address /*deprecated*/) const
{
  if (queue_type == 0) {
    return get_mshr_size();
  }
  return 0;
}

std::size_t CACHE::get_size(uint8_t queue_type, uint64_t /*deprecated*/) const
{
  if (queue_type == 0) {
    return get_mshr_size();
  }
  return 0;
}
// LCOV_EXCL_STOP

namespace
{
double occupancy_ratio(std::size_t occ, std::size_t sz) { return std::ceil(occ) / std::ceil(sz); }

std::vector<double> occupancy_ratio_vec(std::vector<std::size_t> occ, std::vector<std::size_t> sz)
{
  std::vector<double> retval;
  std::transform(std::begin(occ), std::end(occ), std::begin(sz), std::back_inserter(retval), occupancy_ratio);
  return retval;
}
} // namespace

double CACHE::get_mshr_occupancy_ratio() const { return ::occupancy_ratio(get_mshr_occupancy(), get_mshr_size()); }

std::vector<double> CACHE::get_rq_occupancy_ratio() const { return ::occupancy_ratio_vec(get_rq_occupancy(), get_rq_size()); }

std::vector<double> CACHE::get_wq_occupancy_ratio() const { return ::occupancy_ratio_vec(get_wq_occupancy(), get_wq_size()); }

std::vector<double> CACHE::get_pq_occupancy_ratio() const { return ::occupancy_ratio_vec(get_pq_occupancy(), get_pq_size()); }

void CACHE::impl_prefetcher_initialize() const
{
  std::for_each(pref_module_pimpl.begin(), pref_module_pimpl.end(), [](const auto pref) { pref->prefetcher_initialize(); });
}

uint32_t CACHE::impl_prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type,
                                              uint32_t metadata_in) const
{
  uint32_t metadata_out = metadata_in;
  std::for_each(pref_module_pimpl.begin(), pref_module_pimpl.end(),
                [&](const auto pref) { metadata_out = pref->prefetcher_cache_operate(addr, ip, cache_hit, useful_prefetch, type, metadata_out); });
  return (metadata_out);
}

uint32_t CACHE::impl_prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr,
                                           uint32_t metadata_in) const
{
  uint32_t metadata_out = metadata_in;
  std::for_each(pref_module_pimpl.begin(), pref_module_pimpl.end(),
                [&](const auto pref) { metadata_out = pref->prefetcher_cache_fill(addr, set, way, prefetch, evicted_addr, metadata_out); });
  return (metadata_out);
}

void CACHE::impl_prefetcher_cycle_operate() const
{
  std::for_each(pref_module_pimpl.begin(), pref_module_pimpl.end(), [](const auto pref) { pref->prefetcher_cycle_operate(); });
}

void CACHE::impl_prefetcher_final_stats() const
{
  std::for_each(pref_module_pimpl.begin(), pref_module_pimpl.end(), [](const auto pref) { pref->prefetcher_final_stats(); });
}

void CACHE::impl_prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) const
{
  std::for_each(pref_module_pimpl.begin(), pref_module_pimpl.end(), [&](const auto pref) { pref->prefetcher_branch_operate(ip, branch_type, branch_target); });
}

void CACHE::impl_initialize_replacement() const
{
  std::for_each(repl_module_pimpl.begin(), repl_module_pimpl.end(), [](const auto repl) { repl->initialize_replacement(); });
}

long CACHE::impl_find_victim(champsim::origin origin, uint64_t instr_id, long set, const BLOCK* current_set, champsim::address ip, champsim::address full_addr,
                             access_type type) const
{
  long victim = -1;

  std::for_each(repl_module_pimpl.begin(), repl_module_pimpl.end(), [&](const auto repl) {
    long temp_victim = repl->find_victim(origin, instr_id, set, current_set, ip, full_addr, type);
    if (temp_victim != -1)
      victim = temp_victim;
  });

  assert(victim >= 0);
  return (victim);
}

void CACHE::impl_update_replacement_state(champsim::origin origin, long set, long way, champsim::address full_addr, champsim::address ip,
                                          champsim::address victim_addr, access_type type, bool hit) const
{
  std::for_each(repl_module_pimpl.begin(), repl_module_pimpl.end(),
                [&](const auto repl) { repl->update_replacement_state(origin, set, way, full_addr, ip, victim_addr, type, hit); });
}

void CACHE::impl_replacement_cache_fill(champsim::origin origin, long set, long way, champsim::address full_addr, champsim::address ip,
                                        champsim::address victim_addr, access_type type) const
{
  std::for_each(repl_module_pimpl.begin(), repl_module_pimpl.end(),
                [&](const auto repl) { repl->replacement_cache_fill(origin, set, way, full_addr, ip, victim_addr, type); });
}

void CACHE::impl_replacement_final_stats() const
{
  std::for_each(repl_module_pimpl.begin(), repl_module_pimpl.end(), [](const auto repl) { repl->replacement_final_stats(); });
}

void CACHE::initialize()
{
  impl_prefetcher_initialize();
  impl_initialize_replacement();
}

void CACHE::begin_phase(bool warmup, bool roi)
{
  warmup_ = warmup;
  roi_ = roi;
  stats_type new_roi_stats;
  stats_type new_sim_stats;

  new_roi_stats.name = NAME;
  new_sim_stats.name = NAME;

  roi_stats = new_roi_stats;
  sim_stats = new_sim_stats;

  for (auto* ul : upper_levels) {
    channel_type::stats_type ul_new_roi_stats;
    channel_type::stats_type ul_new_sim_stats;
    ul->get_roi_stats() = ul_new_roi_stats;
    ul->get_sim_stats() = ul_new_sim_stats;
  }
}

void CACHE::end_phase()
{
  roi_stats.total_miss_latency_cycles = sim_stats.total_miss_latency_cycles;

  roi_stats.hits = sim_stats.hits;
  roi_stats.misses = sim_stats.misses;
  roi_stats.miss_merge = sim_stats.miss_merge;
  roi_stats.fill = sim_stats.fill;

  roi_stats.pf_requested = sim_stats.pf_requested;
  roi_stats.pf_issued = sim_stats.pf_issued;
  roi_stats.pf_useful = sim_stats.pf_useful;
  roi_stats.pf_useless = sim_stats.pf_useless;
  roi_stats.pf_fill = sim_stats.pf_fill;

  for (auto* ul : upper_levels) {
    ul->get_roi_stats().RQ_ACCESS = ul->get_sim_stats().RQ_ACCESS;
    ul->get_roi_stats().RQ_FULL = ul->get_sim_stats().RQ_FULL;
    ul->get_roi_stats().RQ_TO_CACHE = ul->get_sim_stats().RQ_TO_CACHE;

    ul->get_roi_stats().PQ_ACCESS = ul->get_sim_stats().PQ_ACCESS;
    ul->get_roi_stats().PQ_FULL = ul->get_sim_stats().PQ_FULL;
    ul->get_roi_stats().PQ_TO_CACHE = ul->get_sim_stats().PQ_TO_CACHE;

    ul->get_roi_stats().WQ_ACCESS = ul->get_sim_stats().WQ_ACCESS;
    ul->get_roi_stats().WQ_FULL = ul->get_sim_stats().WQ_FULL;
    ul->get_roi_stats().WQ_TO_CACHE = ul->get_sim_stats().WQ_TO_CACHE;
  }
}

void CACHE::end_simulation()
{
  impl_prefetcher_final_stats();
  impl_replacement_final_stats();
}

template <typename T>
bool CACHE::should_activate_prefetcher(const T& pkt) const
{
  return !pkt.prefetch_from_this && pref_activate_lut_[champsim::to_underlying(pkt.type)];
}

// LCOV_EXCL_START Exclude the following function from LCOV
void CACHE::print_deadlock()
{
  std::string_view mshr_write{"instr_id: {} address: {} v_addr: {} type: {} ready: {}"};
  auto mshr_pack = [time = current_time](const auto& entry) {
    return std::tuple{entry.instr_id, entry.address, entry.v_address, access_type_names.at(champsim::to_underlying(entry.type)),
                      entry.data_promise.is_ready_at(time)};
  };

  std::string_view tag_check_write{"instr_id: {} address: {} v_addr: {} is_translated: {} translate_issued: {} event_cycle: {}"};
  auto tag_check_pack = [period = clock_period](const auto& entry) {
    return std::tuple{entry.instr_id,      entry.address,          entry.v_address,
                      entry.is_translated, entry.translate_issued, entry.event_cycle.time_since_epoch() / period};
  };

  champsim::range_print_deadlock(MSHR, NAME + "_MSHR", mshr_write, mshr_pack);
  champsim::range_print_deadlock(inflight_tag_check, NAME + "_tags", tag_check_write, tag_check_pack);
  champsim::range_print_deadlock(untranslated_tag_check, NAME + "_translation", tag_check_write, tag_check_pack);

  std::string_view q_writer{"instr_id: {} address: {} v_addr: {} type: {} translated: {}"};
  auto q_entry_pack = [](const auto& entry) {
    return std::tuple{entry.instr_id, entry.address, entry.v_address, access_type_names.at(champsim::to_underlying(entry.type)), entry.is_translated};
  };

  for (auto* ul : upper_levels) {
    champsim::range_print_deadlock(ul->get_rq(), NAME + "_RQ", q_writer, q_entry_pack);
    champsim::range_print_deadlock(ul->get_wq(), NAME + "_WQ", q_writer, q_entry_pack);
    champsim::range_print_deadlock(ul->get_pq(), NAME + "_PQ", q_writer, q_entry_pack);
  }
}
// LCOV_EXCL_STOP

std::vector<std::string> CACHE::print_stats(bool roi) const { return format_plaintext(roi ? roi_stats : sim_stats); }

void CACHE::json_stats(champsim::json_stat_builder& b, bool roi) const { format_json(roi ? roi_stats : sim_stats, b); }

std::vector<std::string> champsim::modules::cache_module::format_plaintext(const stats_type& stats)
{
  using hits_value_type = typename decltype(stats.hits)::value_type;
  using misses_value_type = typename decltype(stats.misses)::value_type;
  using miss_merge_value_type = typename decltype(stats.miss_merge)::value_type;
  using fill_value_type = typename decltype(stats.fill)::value_type;

  // We need mutable copies to allocate missing keys
  auto hits = stats.hits;
  auto misses = stats.misses;
  auto miss_merge = stats.miss_merge;
  auto fill = stats.fill;

  std::vector<std::size_t> cpus;

  // build a vector of all existing cpus
  auto stat_keys = {hits.get_keys(), misses.get_keys(), miss_merge.get_keys(), fill.get_keys()};
  for (auto keys : stat_keys) {
    std::transform(std::begin(keys), std::end(keys), std::back_inserter(cpus), [](auto val) { return val.second; });
  }
  std::sort(std::begin(cpus), std::end(cpus));
  auto uniq_end = std::unique(std::begin(cpus), std::end(cpus));
  cpus.erase(uniq_end, std::end(cpus));

  for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
    for (auto cpu : cpus) {
      hits.allocate(std::pair{type, cpu});
      misses.allocate(std::pair{type, cpu});
      miss_merge.allocate(std::pair{type, cpu});
      fill.allocate(std::pair{type, cpu});
    }
  }

  std::vector<std::string> lines{};
  for (auto cpu : cpus) {
    hits_value_type total_hits = 0;
    misses_value_type total_misses = 0;
    miss_merge_value_type total_miss_merge = 0;
    fill_value_type total_fill = 0;
    for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
      total_hits += hits.value_or(std::pair{type, cpu}, hits_value_type{});
      total_misses += misses.value_or(std::pair{type, cpu}, misses_value_type{});
      total_miss_merge += miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{});
      total_fill += fill.value_or(std::pair{type, cpu}, miss_merge_value_type{});
    }

    fmt::format_string<std::string_view, std::string_view, int, int, int> hitmiss_fmtstr{
        "cpu{}->{} {:<12s} ACCESS: {:10d} HIT: {:10d} MISS: {:10d} MISS_MERGE: {:10d}"};
    lines.push_back(fmt::format(hitmiss_fmtstr, cpu, stats.name, "TOTAL", total_hits + total_misses, total_hits, total_misses, total_miss_merge));
    for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
      lines.push_back(fmt::format(hitmiss_fmtstr, cpu, stats.name, access_type_names.at(champsim::to_underlying(type)),
                                  hits.value_or(std::pair{type, cpu}, hits_value_type{}) + misses.value_or(std::pair{type, cpu}, misses_value_type{}),
                                  hits.value_or(std::pair{type, cpu}, hits_value_type{}), misses.value_or(std::pair{type, cpu}, misses_value_type{}),
                                  miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{})));
    }

    lines.push_back(fmt::format("cpu{}->{} PREFETCH REQUESTED: {:10} ISSUED: {:10} USEFUL: {:10} USELESS: {:10}", cpu, stats.name, stats.pf_requested,
                                stats.pf_issued, stats.pf_useful, stats.pf_useless));

    uint64_t total_downstream_demands = total_fill - fill.value_or(std::pair{access_type::PREFETCH, cpu}, fill_value_type{});
    lines.push_back(fmt::format("cpu{}->{} AVERAGE MISS LATENCY: {} cycles", cpu, stats.name,
                                champsim::print_ratio(stats.total_miss_latency_cycles, total_downstream_demands)));
  }

  return lines;
}

void champsim::modules::cache_module::format_json(const stats_type& stats, champsim::json_stat_builder& b)
{
  using hits_value_type = typename decltype(stats.hits)::value_type;
  using misses_value_type = typename decltype(stats.misses)::value_type;
  using miss_merge_value_type = typename decltype(stats.miss_merge)::value_type;
  using fill_value_type = typename decltype(stats.fill)::value_type;

  b.add("prefetch requested", stats.pf_requested)
      .add("prefetch issued", stats.pf_issued)
      .add("useful prefetch", stats.pf_useful)
      .add("useless prefetch", stats.pf_useless);

  // Discover CPU indices from the data itself (mirrors format_plaintext approach)
  std::vector<std::size_t> cpus;
  auto stat_keys = {stats.hits.get_keys(), stats.misses.get_keys(), stats.miss_merge.get_keys(), stats.fill.get_keys()};
  for (auto keys : stat_keys) {
    std::transform(std::begin(keys), std::end(keys), std::back_inserter(cpus), [](auto val) { return val.second; });
  }
  std::sort(std::begin(cpus), std::end(cpus));
  cpus.erase(std::unique(std::begin(cpus), std::end(cpus)), std::end(cpus));

  uint64_t total_downstream_demands = stats.fill.total();
  for (auto cpu : cpus)
    total_downstream_demands -= stats.fill.value_or(std::pair{access_type::PREFETCH, cpu}, fill_value_type{});

  b.add("miss latency", std::ceil(stats.total_miss_latency_cycles) / std::ceil(total_downstream_demands));

  for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
    std::vector<hits_value_type> hit_vec;
    std::vector<misses_value_type> miss_vec;
    std::vector<miss_merge_value_type> miss_merge_vec;

    for (auto cpu : cpus) {
      hit_vec.push_back(stats.hits.value_or(std::pair{type, cpu}, hits_value_type{}));
      miss_vec.push_back(stats.misses.value_or(std::pair{type, cpu}, misses_value_type{}));
      miss_merge_vec.push_back(stats.miss_merge.value_or(std::pair{type, cpu}, miss_merge_value_type{}));
    }

    auto sub = b.group(std::string{access_type_names.at(champsim::to_underlying(type))});
    sub.add("hit", hit_vec).add("miss", miss_vec).add("miss_merge", miss_merge_vec);
  }
}

champsim::modules::cache_module::register_module<CACHE> default_cache_module("DEFAULT_CACHE");