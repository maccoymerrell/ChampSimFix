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
#include <fmt/core.h>

#include "bandwidth.h"
#include "champsim.h"
#include "chrono.h"
#include "deadlock.h"
#include "instruction.h"
#include "util/algorithm.h"
#include "util/bits.h"
#include "util/span.h"
#include "../prefetcher/tcp_stride/tcp_stride.h"


std::map<std::pair<CACHE*,uint32_t>,double> CACHE::prefetch_usefulness;

CACHE::CACHE(CACHE&& other)
    : operable(other),
      internal_PQ(other.PQ_SIZE),
      upper_levels(std::move(other.upper_levels)), lower_level(std::move(other.lower_level)), lower_translate(std::move(other.lower_translate)),

      cpu(other.cpu), NAME(std::move(other.NAME)), NUM_SET(other.NUM_SET), NUM_WAY(other.NUM_WAY), MSHR_SIZE(other.MSHR_SIZE), PQ_SIZE(other.PQ_SIZE), MQ_SIZE(other.MQ_SIZE),
      PQM_SIZE(other.PQM_SIZE), MQC_ENABLED(other.MQC_ENABLED), PQM_ENABLED(other.PQM_ENABLED), CC_ENABLED(other.CC_ENABLED), partition_cache(other.partition_cache),
      HIT_LATENCY(other.HIT_LATENCY), FILL_LATENCY(other.FILL_LATENCY), OFFSET_BITS(other.OFFSET_BITS), block(std::move(other.block)), MAX_TAG(other.MAX_TAG),
      MAX_FILL(other.MAX_FILL), prefetch_as_load(other.prefetch_as_load), match_offset_bits(other.match_offset_bits), virtual_prefetch(other.virtual_prefetch),
      pref_activate_mask(std::move(other.pref_activate_mask)), prefetches_in_mshr(other.prefetches_in_mshr), prefetch_limits(other.prefetch_limits), 
      prefetch_counter(other.prefetch_counter), prefetch_hit_limit(other.prefetch_hit_limit), demands_in_mshr(other.demands_in_mshr),

      sim_stats(std::move(other.sim_stats)), roi_stats(std::move(other.roi_stats)),

      pref_module_pimpl(std::move(other.pref_module_pimpl)), repl_module_pimpl(std::move(other.repl_module_pimpl))
{
  pref_module_pimpl->bind(this);
  repl_module_pimpl->bind(this);
  prefetch_usefulness.clear();
}

auto CACHE::operator=(CACHE&& other) -> CACHE&
{
  this->clock_period = other.clock_period;
  this->current_time = other.current_time;
  this->warmup = other.warmup;

  this->upper_levels = std::move(other.upper_levels);
  this->lower_level = std::move(other.lower_level);
  this->lower_translate = std::move(other.lower_translate);

  this->cpu = other.cpu;
  this->NAME = std::move(other.NAME);
  this->NUM_SET = other.NUM_SET;
  this->NUM_WAY = other.NUM_WAY;
  ;
  this->MSHR_SIZE = other.MSHR_SIZE;
  ;
  this->PQ_SIZE = other.PQ_SIZE;
  this->MQ_SIZE = other.MQ_SIZE;
  this->PQM_SIZE = other.PQM_SIZE;
  this->HIT_LATENCY = other.HIT_LATENCY;
  this->FILL_LATENCY = other.FILL_LATENCY;
  this->OFFSET_BITS = other.OFFSET_BITS;
  ;
  this->block = std::move(other.block);
  this->MAX_TAG = other.MAX_TAG;
  this->MAX_FILL = other.MAX_FILL;
  this->prefetch_as_load = other.prefetch_as_load;
  this->match_offset_bits = other.match_offset_bits;
  this->virtual_prefetch = other.virtual_prefetch;
  this->pref_activate_mask = std::move(other.pref_activate_mask);

  this->sim_stats = std::move(other.sim_stats);
  this->roi_stats = std::move(other.roi_stats);
  this->MQ = std::move(other.MQ);
  this->PREFETCH_MISS_STORAGE = std::move(other.PREFETCH_MISS_STORAGE);
  this->PREFETCH_BANK_QUEUES = std::move(other.PREFETCH_BANK_QUEUES);
  this->PREFETCH_FREE_LIST = std::move(other.PREFETCH_FREE_LIST);
  this->OUTGOING_BANK_REQUESTS = std::move(other.OUTGOING_BANK_REQUESTS);
  this->pref_module_pimpl = std::move(other.pref_module_pimpl);
  this->repl_module_pimpl = std::move(other.repl_module_pimpl);

  this->MQC_ENABLED = other.MQC_ENABLED;
  this->PQM_ENABLED = other.PQM_ENABLED;
  this->CC_ENABLED = other.CC_ENABLED;
  this->partition_cache = other.partition_cache;

  pref_module_pimpl->bind(this);
  repl_module_pimpl->bind(this);

  this->internal_PQ = other.internal_PQ;
  this->prefetches_in_mshr = std::move(other.prefetches_in_mshr);
  this->demands_in_mshr = std::move(other.demands_in_mshr);
  this->prefetch_limits = std::move(other.prefetches_in_mshr);
  this->prefetch_counter = std::move(other.prefetch_counter);
  this->prefetch_hit_limit = std::move(other.prefetch_hit_limit);
  prefetch_usefulness.clear();

  return *this;
}

CACHE::tag_lookup_type::tag_lookup_type(const request_type& req, bool local_pref, bool skip, CACHE* source_ptr_, bool return_hit_status_)
    : address(req.address), v_address(req.v_address), data(req.data), ip(req.ip), instr_id(req.instr_id), pf_metadata(req.pf_metadata), cpu(req.cpu),
      back_off(req.back_off), row_act(req.row_act), lsq_score(req.lsq_rating), pf_distance(req.pf_distance),
      type(req.type), prefetch_from_this(local_pref), skip_fill(skip), return_hit_status(return_hit_status_), source_ptr(source_ptr_), is_translated(req.is_translated), instr_depend_on_me(req.instr_depend_on_me)
{
}

CACHE::tag_lookup_type::tag_lookup_type() : tag_lookup_type(request_type{},false,false,nullptr,false) {};

CACHE::mshr_type::mshr_type(const tag_lookup_type& req, champsim::chrono::clock::time_point _time_enqueued)
    : address(req.address), v_address(req.v_address), ip(req.ip), instr_id(req.instr_id), cpu(req.cpu), type(req.type), back_off(req.back_off), row_act(req.row_act),
      prefetch_from_this(req.prefetch_from_this), time_enqueued(_time_enqueued), instr_depend_on_me(req.instr_depend_on_me), to_return(req.to_return), lsq_score(req.lsq_score)
{
}

CACHE::mshr_type CACHE::mshr_type::merge(mshr_type predecessor, mshr_type successor)
{
  std::vector<uint64_t> merged_instr{};
  std::vector<std::deque<response_type>*> merged_return{};

  std::set_union(std::begin(predecessor.instr_depend_on_me), std::end(predecessor.instr_depend_on_me), std::begin(successor.instr_depend_on_me),
                 std::end(successor.instr_depend_on_me), std::back_inserter(merged_instr));
  std::set_union(std::begin(predecessor.to_return), std::end(predecessor.to_return), std::begin(successor.to_return), std::end(successor.to_return),
                 std::back_inserter(merged_return));

  mshr_type retval{successor.type == access_type::PREFETCH ? predecessor : successor};

  retval.was_promoted = predecessor.was_promoted || successor.was_promoted;
  if(successor.type == access_type::PREFETCH) {
    retval.type = predecessor.type;
  }
  else {
    if(predecessor.type == access_type::PREFETCH && successor.type != access_type::PREFETCH)
      retval.was_promoted = true;
    retval.type = successor.type;
  }

  retval.time_enqueued = ((successor.type != access_type::PREFETCH && predecessor.type == access_type::PREFETCH)) ? successor.time_enqueued : predecessor.time_enqueued;
  retval.instr_depend_on_me = merged_instr;
  retval.to_return = merged_return;
  retval.data_promise = predecessor.data_promise;

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

auto CACHE::fill_block(mshr_type mshr, uint32_t metadata) -> BLOCK
{
  CACHE::BLOCK to_fill;
  to_fill.valid = true;
  to_fill.prefetch = mshr.prefetch_from_this;
  to_fill.cpu = mshr.cpu;
  to_fill.dirty = (mshr.type == access_type::WRITE);
  to_fill.address = mshr.address;
  to_fill.v_address = mshr.v_address;
  to_fill.data = mshr.data_promise->data;
  to_fill.pf_metadata = metadata;

  return to_fill;
}

auto CACHE::matches_address(champsim::address addr) const
{
  return [match = addr.slice_upper(OFFSET_BITS), shamt = OFFSET_BITS](const auto& entry) {
    return entry.address.slice_upper(shamt) == match;
  };
}

template <typename T>
champsim::address CACHE::module_address(const T& element) const
{
  auto address = virtual_prefetch ? element.v_address : element.address;
  return champsim::address{address.slice_upper(match_offset_bits ? champsim::data::bits{} : OFFSET_BITS)};
}

bool CACHE::handle_fill(const mshr_type& fill_mshr)
{
  cpu = fill_mshr.cpu;
  lsq_score = fill_mshr.lsq_score;
  pf_base = virtual_prefetch ? fill_mshr.v_address : fill_mshr.address;

  if(fill_mshr.type != access_type::DROPPED) {
    // find victim
    auto [set_begin, set_end] = get_set_span(fill_mshr.address, fill_mshr.cpu);
    auto way = std::find_if_not(set_begin, set_end, [address = fill_mshr.address](auto x) { return x.valid && champsim::block_number{x.address} != champsim::block_number{address}; });
    if (way == set_end) {
      way = std::next(set_begin, impl_find_victim(fill_mshr.cpu >= NUM_CPUS ? 0 : fill_mshr.cpu, fill_mshr.instr_id, get_set_index(fill_mshr.address,fill_mshr.cpu), &*set_begin, fill_mshr.ip,
                                                  fill_mshr.address, fill_mshr.type, fill_mshr.type == access_type::PREFETCH && fill_mshr.prefetch_from_this));
    }
    assert(set_begin <= way);
    assert(way <= set_end);
    assert(way != set_end || fill_mshr.type != access_type::WRITE); // Writes may not bypass
    const auto way_idx = std::distance(set_begin, way);             // cast protected by earlier assertion

    if constexpr (champsim::debug_print) {
      fmt::print("[{}] {} instr_id: {} address: {} v_address: {} set: {} way: {} type: {} prefetch_metadata: {} cycle_enqueued: {} cycle: {}\n", NAME, __func__,
                fill_mshr.instr_id, fill_mshr.address, fill_mshr.v_address, get_set_index(fill_mshr.address,fill_mshr.cpu), way_idx,
                access_type_names.at(champsim::to_underlying(fill_mshr.type)), fill_mshr.data_promise->pf_metadata,
                (fill_mshr.time_enqueued.time_since_epoch()) / clock_period, (current_time.time_since_epoch()) / clock_period);
    }

    //increment limit by 1
    if(fill_mshr.type == access_type::PREFETCH || fill_mshr.was_promoted) {
      if(prefetch_hit_limit[fill_mshr.cpu > NUM_CPUS ? 0 : fill_mshr.cpu])
        prefetch_counter[fill_mshr.cpu > NUM_CPUS ? 0 : fill_mshr.cpu]++;
      if(prefetch_counter[fill_mshr.cpu > NUM_CPUS ? 0 : fill_mshr.cpu] > prefetch_limits[fill_mshr.cpu > NUM_CPUS ? 0 : fill_mshr.cpu]) {
        //fmt::print("[{}] Increasing threshold for CPU: {}\n", NAME, fill_mshr.cpu);
        prefetch_limits[fill_mshr.cpu > NUM_CPUS ? 0 : fill_mshr.cpu]++;
        prefetch_counter[fill_mshr.cpu > NUM_CPUS ? 0 : fill_mshr.cpu] = 0;
        prefetch_hit_limit[fill_mshr.cpu > NUM_CPUS ? 0 : fill_mshr.cpu] = false;
      }
    }

    if (way != set_end && way->valid && way->dirty) {
      request_type writeback_packet;

      writeback_packet.cpu = fill_mshr.cpu;
      writeback_packet.address = way->address;
      writeback_packet.data = way->data;
      writeback_packet.instr_id = fill_mshr.instr_id;
      writeback_packet.ip = champsim::address{};
      writeback_packet.type = access_type::WRITE;
      writeback_packet.pf_metadata = way->pf_metadata;
      writeback_packet.response_requested = false;

      if constexpr (champsim::debug_print) {
        fmt::print("[{}] {} evict address: {} v_address: {} prefetch_metadata: {}\n", NAME, __func__, writeback_packet.address, writeback_packet.v_address,
                  fill_mshr.data_promise->pf_metadata);
      }

      auto success = lower_level->add_wq(writeback_packet);
      if (!success) {
        return false;
      }
      sim_stats.downstream_packets.increment(std::pair{writeback_packet.type, writeback_packet.cpu});
    }

  champsim::address evicting_address{};
  if (way != set_end && way->valid && champsim::block_number{way->address} != champsim::block_number{fill_mshr.address}) {
    evicting_address = module_address(*way);
  }
  if(NAME == "LLC") {
      if(fill_mshr.type == access_type::PREFETCH)
      if(fill_mshr.back_off) {
        tcp_stride::back_off(module_address(fill_mshr),fill_mshr.cpu);
        //spp_tcp::back_off(module_address(fill_mshr),fill_mshr.cpu);
      }
  }
  bool useless = (way != set_end && way->valid && way->prefetch);
  auto metadata_thru = impl_prefetcher_cache_fill(module_address(fill_mshr), fill_mshr.ip, fill_mshr.cpu >= NUM_CPUS ? 0 : fill_mshr.cpu, useless, get_set_index(fill_mshr.address,fill_mshr.cpu), way_idx,
                                                  (fill_mshr.type == access_type::PREFETCH), evicting_address, fill_mshr.data_promise->pf_metadata, way != set_end && way->valid ? way->pf_metadata : 0,way != set_end && way->valid ? way->cpu : NUM_CPUS);
  
    impl_replacement_cache_fill(fill_mshr.cpu >= NUM_CPUS ? 0 : fill_mshr.cpu, get_set_index(fill_mshr.address,fill_mshr.cpu), way_idx, module_address(fill_mshr), fill_mshr.ip, evicting_address,
                                fill_mshr.type, fill_mshr.type == access_type::PREFETCH && fill_mshr.prefetch_from_this);

    if (way != set_end) {
      if (way->valid && way->prefetch) {
        ++sim_stats.pf_useless;
        sim_stats.pf_useless_core.increment(way->cpu > NUM_CPUS ? 0 : way->cpu);
      }

      if (fill_mshr.type == access_type::PREFETCH && fill_mshr.prefetch_from_this) {
        ++sim_stats.pf_fill;
        //sim_stats.pf_fill_core.increment(fill_mshr.cpu >= NUM_CPUS ? 0 : fill_mshr.cpu);
      }

      *way = fill_block(fill_mshr, metadata_thru);
    }

    // COLLECT STATS
    if(fill_mshr.type != access_type::PREFETCH) {
      sim_stats.total_miss_latency_cycles += (current_time - (fill_mshr.time_enqueued + clock_period)) / clock_period;
      sim_stats.total_returned_packets++;
    }

    response_type response{fill_mshr.back_off, fill_mshr.row_act, fill_mshr.type == access_type::PROMOTION ? access_type::LOAD : fill_mshr.type, fill_mshr.address, fill_mshr.v_address, fill_mshr.data_promise->data, metadata_thru, fill_mshr.instr_depend_on_me};
    for (auto* ret : fill_mshr.to_return) {
      ret->push_back(response);
    }
  }
  else if(fill_mshr.type == access_type::DROPPED) {
    if constexpr (champsim::debug_print) {
      fmt::print("[{}] Dropped PREFETCH for {} from MSHR\n", NAME, fill_mshr.address);
    }
    auto metadata_thru = impl_prefetcher_cache_fill(module_address(fill_mshr), fill_mshr.ip, fill_mshr.cpu >= NUM_CPUS ? 0 : fill_mshr.cpu, false, get_set_index(fill_mshr.address,fill_mshr.cpu), NUM_WAY,
                                                  (fill_mshr.type == access_type::PREFETCH), champsim::address{}, fill_mshr.data_promise->pf_metadata, 0, NUM_CPUS);
    response_type response{fill_mshr.back_off, fill_mshr.row_act, fill_mshr.type, fill_mshr.address, fill_mshr.v_address, fill_mshr.data_promise->data, 0, fill_mshr.instr_depend_on_me};
    for (auto* ret : fill_mshr.to_return) {
      //fmt::print("\tSending response...\n");
      ret->push_back(response);
    }
  }

  return true;
}

bool CACHE::check_hit(champsim::address address, uint32_t cpu) {
  auto [set_begin, set_end] = get_set_span(address, cpu);
  auto way = std::find_if(set_begin, set_end, [matcher = matches_address(address)](const auto& x) { return x.valid && matcher(x); });
  return (way != set_end);
}

bool CACHE::try_hit(tag_lookup_type& handle_pkt)
{
  cpu = handle_pkt.cpu;
  pf_base = virtual_prefetch ? handle_pkt.v_address : handle_pkt.address;

  // access cache
  auto [set_begin, set_end] = get_set_span(handle_pkt.address, handle_pkt.cpu);
  auto way = std::find_if(set_begin, set_end, [matcher = matches_address(handle_pkt.address)](const auto& x) { return x.valid && matcher(x); });
  const auto hit = (way != set_end);
  const auto useful_prefetch = (hit && way->prefetch && !handle_pkt.prefetch_from_this);

  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} instr_id: {} address: {} v_address: {} data: {} set: {} way: {} ({}) type: {} cpu: {} cycle: {}\n", NAME, __func__, handle_pkt.instr_id,
               handle_pkt.address, handle_pkt.v_address, handle_pkt.data, get_set_index(handle_pkt.address,handle_pkt.cpu), std::distance(set_begin, way),
               hit ? "HIT" : "MISS", access_type_names.at(champsim::to_underlying(handle_pkt.type)), handle_pkt.cpu, current_time.time_since_epoch() / clock_period);
  }

  auto metadata_thru = handle_pkt.pf_metadata;
  bool should_drop = false;
  if (should_activate_prefetcher(handle_pkt)) {
    metadata_thru = impl_prefetcher_cache_operate(module_address(handle_pkt), handle_pkt.ip, handle_pkt.cpu >= NUM_CPUS ? 0 : handle_pkt.cpu, hit, useful_prefetch, handle_pkt.type, metadata_thru,hit ? way->pf_metadata : 0);
    handle_pkt.invoked_prefetcher = true;
    if(marked_for_drop.has_value() && champsim::block_number{marked_for_drop.value()} == champsim::block_number{handle_pkt.address}) {
      should_drop = true;
      marked_for_drop.reset();
    }
  }

  if (should_drop) {
    assert(handle_pkt.type == access_type::PREFETCH);
    response_type response{handle_pkt.back_off, handle_pkt.row_act, access_type::DROPPED, handle_pkt.address, handle_pkt.v_address, champsim::address{}, metadata_thru, handle_pkt.instr_depend_on_me};
    for (auto* ret : handle_pkt.to_return) {
      ret->push_back(response);
    }

    return true;
  }
  // update replacement policy
  const auto way_idx = std::distance(set_begin, way);
  lsq_score = handle_pkt.lsq_score;
  impl_update_replacement_state(handle_pkt.cpu >= NUM_CPUS ? 0 : handle_pkt.cpu, get_set_index(handle_pkt.address,handle_pkt.cpu), way_idx, module_address(handle_pkt), handle_pkt.ip, {}, handle_pkt.type,
                                hit, handle_pkt.type == access_type::PREFETCH && handle_pkt.prefetch_from_this);

  if (hit) {
    sim_stats.hits.increment(std::pair{handle_pkt.type, handle_pkt.cpu});

    if(handle_pkt.type != access_type::PROMOTION) {
      response_type response{handle_pkt.back_off, handle_pkt.row_act, handle_pkt.type, handle_pkt.address, handle_pkt.v_address, way->data, metadata_thru, handle_pkt.instr_depend_on_me};
      for (auto* ret : handle_pkt.to_return) {
        ret->push_back(response);
      }
    }

    way->dirty |= (handle_pkt.type == access_type::WRITE);

    // update prefetch stats and reset prefetch bit
    if (useful_prefetch) {
      ++sim_stats.pf_useful;
      sim_stats.pf_useful_core.increment(handle_pkt.cpu > NUM_CPUS ? 0 : handle_pkt.cpu);
      way->prefetch = false;
    }
  }

  return hit;
}

auto CACHE::mshr_and_forward_packet(const tag_lookup_type& handle_pkt) -> std::pair<mshr_type, request_type>
{
  mshr_type to_allocate{handle_pkt, current_time};

  request_type fwd_pkt;

  fwd_pkt.asid[0] = handle_pkt.asid[0];
  fwd_pkt.asid[1] = handle_pkt.asid[1];
  fwd_pkt.type = (handle_pkt.type == access_type::WRITE) ? access_type::RFO : handle_pkt.type;
  fwd_pkt.pf_metadata = handle_pkt.pf_metadata;
  fwd_pkt.cpu = handle_pkt.cpu;
  fwd_pkt.source_ptr = handle_pkt.source_ptr;
  fwd_pkt.back_off = handle_pkt.back_off;
  fwd_pkt.row_act = handle_pkt.row_act;

  fwd_pkt.address = handle_pkt.address;
  fwd_pkt.v_address = handle_pkt.v_address;
  fwd_pkt.data = handle_pkt.data;
  fwd_pkt.instr_id = handle_pkt.instr_id;
  fwd_pkt.ip = handle_pkt.ip;

  fwd_pkt.instr_depend_on_me = handle_pkt.instr_depend_on_me;
  fwd_pkt.response_requested = (!handle_pkt.prefetch_from_this || !handle_pkt.skip_fill);

  return std::pair{std::move(to_allocate), std::move(fwd_pkt)};
}

bool CACHE::allocate_mshr(const tag_lookup_type& handle_pkt) {
  mshr_type to_allocate{handle_pkt, current_time};
  //fmt::print("Allocating MSHR for address: {} cpu: {}\n",handle_pkt.address,handle_pkt.cpu);
  cpu = handle_pkt.cpu;
  pf_base = virtual_prefetch ? handle_pkt.v_address : handle_pkt.address;
  bool mshr_full = (MSHR.size() == MSHR_SIZE);
  auto mshr_pkt = mshr_and_forward_packet(handle_pkt);
  auto mshr_entry = std::find_if(std::begin(MSHR), std::end(MSHR), matches_address(handle_pkt.address));

  if (mshr_entry != MSHR.end()) // miss already inflight
  {
    if (mshr_entry->type == access_type::PREFETCH && ((handle_pkt.type != access_type::PREFETCH) && handle_pkt.type != access_type::WRITE)) {
      
      
      mshr_pkt.second.response_requested = false;
      mshr_pkt.second.type = access_type::PROMOTION;
      bool success = lower_level->add_rq(mshr_pkt.second);
      //best effort. If we don't have the available bandwidth, thats okay
      //if(!success)
      //  return std::pair{false,false};

      sim_stats.downstream_packets.increment(std::pair{access_type::PROMOTION, handle_pkt.cpu});
      if constexpr (champsim::debug_print) {
        fmt::print("[{}] Issued promotion packet for {}\n",NAME,mshr_pkt.second.address);
      }
    }
    if(mshr_entry->type == access_type::PREFETCH && handle_pkt.type != access_type::PREFETCH) {
      // Mark the prefetch as useful
      if (mshr_entry->prefetch_from_this) {
        ++sim_stats.pf_useful;
        sim_stats.pf_useful_core.increment(handle_pkt.cpu > NUM_CPUS ? 0 : handle_pkt.cpu);
        //sim_stats.pf_fill_core.increment(handle_pkt.cpu > NUM_CPUS ? 0 : handle_pkt.cpu);
      }
    }

    *mshr_entry = mshr_type::merge(*mshr_entry, to_allocate);
  }  else {
    //if mshr_full
    if (mshr_full) { // not enough MSHR resource
      return false;
    }

    const bool send_to_rq = (prefetch_as_load || handle_pkt.type != access_type::PREFETCH);
    bool success = send_to_rq ? lower_level->add_rq(mshr_pkt.second) : lower_level->add_pq(mshr_pkt.second);

    if (!success) {
      return false;
    }
    sim_stats.downstream_packets.increment(std::pair{handle_pkt.type, handle_pkt.cpu});

    // Allocate an MSHR
    if (mshr_pkt.second.response_requested) {
     //fmt::print("[{}] Increasing outgoing bank request counter for prefetch: {} address: {} bank: {}, now: {}\n",NAME, mshr_pkt.second.type == access_type::PREFETCH, mshr_pkt.second.address, MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(mshr_pkt.second.address),OUTGOING_BANK_REQUESTS.at(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(mshr_pkt.second.address))+1);
     OUTGOING_BANK_REQUESTS.at(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(mshr_pkt.first.address))++;
     MSHR.emplace_back(std::move(mshr_pkt.first));
    }
  }
  if((NAME.compare("LLC") == 0) && !warmup) {
    //fmt::print("[MSHRDATA] Cycle:{} Occupancy:{}\n",current_cycle(),get_mshr_occupancy());
  }
  return true;
}
bool CACHE::handle_miss(const tag_lookup_type& handle_pkt)
{
  mshr_type to_allocate{handle_pkt, current_time};
  auto mshr_pkt = mshr_and_forward_packet(handle_pkt);
  //check for matching entry in MSHR and merge
  auto mshr_entry = std::find_if(std::begin(MSHR), std::end(MSHR), matches_address(handle_pkt.address));
  if (mshr_entry != MSHR.end()) {
    if (mshr_entry->type == access_type::PREFETCH && ((handle_pkt.type != access_type::PREFETCH) && handle_pkt.type != access_type::WRITE)) {
      
      
      mshr_pkt.second.response_requested = false;
      mshr_pkt.second.type = access_type::PROMOTION;

      //best effort. If we don't have the available bandwidth, thats okay.
      bool success = lower_level->add_rq(mshr_pkt.second);
      //if(!success)
      //  return false;

      sim_stats.downstream_packets.increment(std::pair{access_type::PROMOTION, handle_pkt.cpu});
      if constexpr (champsim::debug_print) {
        fmt::print("[{}] Issued promotion packet for {}\n",NAME,mshr_pkt.second.address);
      }
    }
    if(mshr_entry->type == access_type::PREFETCH && handle_pkt.type != access_type::PREFETCH) {
      // Mark the prefetch as useful
      if (mshr_entry->prefetch_from_this) {
        ++sim_stats.pf_useful;
        sim_stats.pf_useful_core.increment(handle_pkt.cpu > NUM_CPUS ? 0 : handle_pkt.cpu);
        //sim_stats.pf_fill_core.increment(handle_pkt.cpu > NUM_CPUS ? 0 : handle_pkt.cpu);
      }
    }

    *mshr_entry = mshr_type::merge(*mshr_entry, to_allocate);
    return true;
  }

  //I think this is fine. Will need to check
  if(handle_pkt.type == access_type::PROMOTION /*&& handle_pkt.prefetch_from_this*/) {
    if constexpr (champsim::debug_print) {
      fmt::print("[{}] Promotion dropped for {}\n",NAME,mshr_pkt.second.address);
    }
    
    sim_stats.misses.increment(std::pair{handle_pkt.type, handle_pkt.cpu});
    return true;
  }

  
  //add miss queue to handle_miss
  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} instr_id: {} address: {} v_address: {} type: {} local_prefetch: {} cpu: {} cycle: {}\n", NAME, __func__, handle_pkt.instr_id,
               handle_pkt.address, handle_pkt.v_address, access_type_names.at(champsim::to_underlying(handle_pkt.type)), handle_pkt.prefetch_from_this,
               handle_pkt.cpu, current_time.time_since_epoch() / clock_period);
  }
  uint32_t used_cpu = handle_pkt.cpu;
  if(used_cpu >= NUM_CPUS) {
    fmt::print("[{}] Got invalid CPU: {}\n",NAME,used_cpu);
    used_cpu = 0;
  }
  //if MQC is disabled, put all misses into same queue
  if(!MQC_ENABLED)
    used_cpu = 0;
  bool mq_full = (MQ.at(used_cpu).size() >= MQ_SIZE);
  //add to per-core MQ if demand or prefetch queue if prefetch
  if(handle_pkt.type != access_type::PREFETCH || !PQM_ENABLED) {
    if(mq_full) {
      //fmt::print("[{}] MQ FULL size: {} cpu: {}\n",NAME, MQ.at(used_cpu).size(), used_cpu);
      return false;
    }
    //fmt::print("[{}] Trying to enqueue {} in MQ...\n", NAME, handle_pkt.address);
    MQ.at(used_cpu).emplace_back(std::move(handle_pkt));
    if(!handle_pkt.prefetch_from_this)
      MQ_MISS_COUNTER.at(used_cpu)++;
  }
  else {
    //monolithic PMQ
    //search PQM for matching entry, and merge
    auto pqm_match = [addr = handle_pkt.address, skip_level = handle_pkt.skip_fill] (auto& entry) {
      return (entry.address == addr && skip_level == entry.skip_fill);
    };
    auto match = std::find_if(std::begin(PREFETCH_MISS_STORAGE),std::end(PREFETCH_MISS_STORAGE),pqm_match);

    //PQM is full
    if(match == std::end(PREFETCH_MISS_STORAGE) && PREFETCH_FREE_LIST.size() == 0) {
      //fmt::print("[{}] PQM FULL\n",NAME);
      return false;
    }
    //PQM is not full, and we aren't dropping this prefetch due to merge
    if(match == std::end(PREFETCH_MISS_STORAGE)) {
      std::size_t ind = PREFETCH_FREE_LIST.front();
      PREFETCH_FREE_LIST.pop_front();
      PREFETCH_MISS_STORAGE.at(ind) = std::move(handle_pkt);
      std::size_t bank_ind = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(handle_pkt.address);
      PREFETCH_BANK_QUEUES.at(bank_ind).push_back(ind);
    } else {
      //merge into matching entry
      std::vector<uint64_t> merged_instr{};
      std::vector<std::deque<response_type>*> merged_return{};
    
      std::set_union(std::begin(match->instr_depend_on_me), std::end(match->instr_depend_on_me), std::begin(handle_pkt.instr_depend_on_me),
                     std::end(handle_pkt.instr_depend_on_me), std::back_inserter(merged_instr));
      std::set_union(std::begin(match->to_return), std::end(match->to_return), std::begin(handle_pkt.to_return), std::end(handle_pkt.to_return),
                     std::back_inserter(merged_return));
      match->instr_depend_on_me = merged_instr;
      match->to_return = merged_return;
    }
  }
  sim_stats.misses.increment(std::pair{handle_pkt.type,handle_pkt.cpu >= NUM_CPUS ? 0 : handle_pkt.cpu});

  return true;
}

double CACHE::get_cache_occupancy_ratio() const {
  std::size_t valid_blocks = 0;
  for (auto const b: block) {
    if(b.valid)
      valid_blocks++;
  }
  return (valid_blocks / (double)(NUM_WAY*NUM_SET));
}

//schedule MSHR
void CACHE::schedule_mshr() {
  //need to first see if MSHR is empty
  //if so, we need to schedule the next request
  //if(MSHR.size() >= MSHR_SIZE)
  //  return;
  //check if current core can issue request
  prefetches_in_mshr = std::vector<std::size_t>(NUM_CPUS,0);
  demands_in_mshr = std::vector<std::size_t>(NUM_CPUS,0);
  std::vector<bool> prefetch_limited = std::vector<bool>(NUM_CPUS,false);
  for(auto entry : MSHR) {
    if(entry.type == access_type::PREFETCH) {
      prefetches_in_mshr[entry.cpu >= NUM_CPUS ? 0 : entry.cpu]++;
      if(prefetches_in_mshr[entry.cpu >= NUM_CPUS ? 0 : entry.cpu] >= prefetch_limits[entry.cpu >= NUM_CPUS ? 0 : entry.cpu]) {
        prefetch_limited[entry.cpu >= NUM_CPUS ? 0 : entry.cpu] = true;
        prefetch_hit_limit[entry.cpu >= NUM_CPUS ? 0 : entry.cpu] = true;
      }
    }
    else {
      demands_in_mshr[entry.cpu >= NUM_CPUS ? 0 : entry.cpu]++;
    }
  }

  //fmt::print("[{}] scheduling for MSHR...\n",NAME);
  auto valid_demand = [cache = this, &pref_limit = prefetch_limited](auto& entry) {
    std::size_t bank_id = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(entry.address);
    //fmt::print("\t[{}] Outgoing requests to bank {}: {}\n", cache->NAME, bank_id,cache->OUTGOING_BANK_REQUESTS.at(bank_id));
    //disable check if MQC is disabled, always be true
    //bool should_bc_prefetch = !cache->CC_ENABLED || entry.type != access_type::PREFETCH || !pref_limit[entry.cpu >= NUM_CPUS ? 0 : entry.cpu];
    //if(!should_bc_prefetch)
    //  fmt::print("Ignored prefetch due to too many in queue\n");
    return (cache->OUTGOING_BANK_REQUESTS.at(bank_id) <= cache->BANK_DEMAND_THRESHOLD || !cache->MQC_ENABLED) /* && (!cache->CC_ENABLED || entry.type != access_type::PREFETCH || !pref_limit[entry.cpu >= NUM_CPUS ? 0 : entry.cpu])*/;
  };
  auto search = std::find_if(std::begin(MQ.at(ACTIVE_CORE)),std::end(MQ.at(ACTIVE_CORE)),valid_demand);
  //don't limit starvation if MQC is disabled
  if(search != std::end(MQ.at(ACTIVE_CORE)) && (MQ_COUNTER < MQ_STARVE || !MQC_ENABLED)) {
    //found a packet to issue
    //check if we should drop this packet
    bool should_not_drop = !CC_ENABLED || search->type != access_type::PREFETCH || !prefetch_limited[ACTIVE_CORE];
    if(!should_not_drop) {
      response_type response{search->back_off, search->row_act, access_type::DROPPED, search->address, search->v_address, champsim::address{}, 0, search->instr_depend_on_me};
      for (auto* ret : search->to_return) {
        ret->push_back(response);
      }
      MQ.at(ACTIVE_CORE).erase(search);
      MQ_COUNTER++;
    }
    //fmt::print("[{}] Trying scheduling from demand queue...\n",NAME);
    else if(allocate_mshr(*search)) {
      MQ.at(ACTIVE_CORE).erase(search);
      MQ_COUNTER++;
      //fmt::print("\tSuccess!\n");
    } else {
      //half prefetch limit if we hit the max
      if(search->type == access_type::PREFETCH) {
        //fmt::print("[{}] Halving threshold for CPU: {}\n", NAME, search->cpu);
        prefetch_limits[ACTIVE_CORE] = std::max(prefetch_limits[ACTIVE_CORE] >> 1, 1ul);
        

        //drop
      }
    }
    return;
  }
  else {
    //prefetch if we couldn't demand anything

    //don't call if PQM is disabled
    if(search == std::end(MQ.at(ACTIVE_CORE)) && PQM_ENABLED) {
      std::size_t smallest_valid_bank = PREFETCH_BANK_QUEUES.size();
      std::size_t smallest_outstanding_pf = PQM_SIZE + 1;
      for(std::size_t bank_occu = 0; bank_occu < PREFETCH_BANK_QUEUES.size(); bank_occu++) {
        if(PREFETCH_BANK_QUEUES.at(bank_occu).size() > 0) {
          //fmt::print("[{}] Prefetch queue at bank {} being checked...\n", NAME, bank_occu);
          uint32_t n_cpu = PREFETCH_MISS_STORAGE.at(PREFETCH_BANK_QUEUES.at(bank_occu).front()).cpu;
          CACHE* device = PREFETCH_MISS_STORAGE.at(PREFETCH_BANK_QUEUES.at(bank_occu).front()).source_ptr;
          double usefulness = prefetch_usefulness[{device,n_cpu}];
          std::size_t thresh = 0;
          if(usefulness > 0.4)
            thresh++;
          if(usefulness > 0.85)
            thresh++;
          //fmt::print("[{}]\tUsing threshold {}...\n", NAME, BANK_PREFETCH_THRESHOLD[thresh]);
          //fmt::print("[{}]\tBank occupancy is {}...\n",NAME,OUTGOING_BANK_REQUESTS.at(bank_occu));
          if (OUTGOING_BANK_REQUESTS.at(bank_occu) < BANK_PREFETCH_THRESHOLD[thresh] && PREFETCH_BANK_QUEUES.at(bank_occu).size() < smallest_outstanding_pf) {
            //check first entry for cc if enabled
            //if(!CC_ENABLED || !prefetch_limited[PREFETCH_MISS_STORAGE.at(PREFETCH_BANK_QUEUES.at(bank_occu).front()).cpu >= NUM_CPUS ? 0 : PREFETCH_MISS_STORAGE.at(PREFETCH_BANK_QUEUES.at(bank_occu).front()).cpu]) {
            smallest_valid_bank = bank_occu;
            smallest_outstanding_pf = PREFETCH_BANK_QUEUES.at(bank_occu).size();
            //}
            //fmt::print("[{}]\tTrying to schedule from bank {}...\n",NAME,bank_occu);
          }
        }
      }
      if(smallest_valid_bank != PREFETCH_BANK_QUEUES.size()) {
        std::size_t ind = PREFETCH_BANK_QUEUES.at(smallest_valid_bank).front();

        bool should_not_drop = !CC_ENABLED || PREFETCH_MISS_STORAGE.at(ind).type != access_type::PREFETCH || !prefetch_limited[PREFETCH_MISS_STORAGE.at(ind).cpu >= NUM_CPUS ? 0 : PREFETCH_MISS_STORAGE.at(ind).cpu];
        if(!should_not_drop) {
          response_type response{PREFETCH_MISS_STORAGE.at(ind).back_off, PREFETCH_MISS_STORAGE.at(ind).row_act, access_type::DROPPED, PREFETCH_MISS_STORAGE.at(ind).address, PREFETCH_MISS_STORAGE.at(ind).v_address, champsim::address{}, 0, PREFETCH_MISS_STORAGE.at(ind).instr_depend_on_me};
          for (auto* ret : PREFETCH_MISS_STORAGE.at(ind).to_return) {
            ret->push_back(response);
          }
          PREFETCH_MISS_STORAGE.at(ind).address = champsim::address{};
          PREFETCH_BANK_QUEUES.at(smallest_valid_bank).pop_front();
          PREFETCH_FREE_LIST.push_back(ind);
        }
        //issue
        //fmt::print("[{}]\tScheduling from PQ...\n", NAME);
        else if(allocate_mshr(PREFETCH_MISS_STORAGE.at(ind))) {
          PREFETCH_MISS_STORAGE.at(ind).address = champsim::address{};
          PREFETCH_BANK_QUEUES.at(smallest_valid_bank).pop_front();
          PREFETCH_FREE_LIST.push_back(ind);
          //fmt::print("[{}]\tSuccess!\n",NAME);
        } else {
          //half prefetch limit if we max out the mshr
          if(search->type == access_type::PREFETCH) {
            //fmt::print("[{}] Halving threshold for CPU: {}\n", NAME, search->cpu);
            prefetch_limits[PREFETCH_MISS_STORAGE.at(ind).cpu >= NUM_CPUS ? 0 : PREFETCH_MISS_STORAGE.at(ind).cpu] = std::max(prefetch_limits[PREFETCH_MISS_STORAGE.at(ind).cpu >= NUM_CPUS ? 0 : PREFETCH_MISS_STORAGE.at(ind).cpu] >> 1, 1ul);
          }
        }
      }
    }
    //reset MQ_COUNTER and set new core
    if(MQC_ENABLED) {
      MQ_COUNTER = 0;

      
      std::size_t MQ_CORE_SEL = 0;
      bool has_valid_request = false;
      while(!has_valid_request && MQ_CORE_SEL < MQ_CORE.size()){
        ACTIVE_CORE = MQ_CORE[MQ_CORE_SEL];
        has_valid_request = std::find_if(std::begin(MQ.at(ACTIVE_CORE)),std::end(MQ.at(ACTIVE_CORE)),valid_demand) != std::end(MQ.at(ACTIVE_CORE));
        MQ_CORE_SEL++;
      }
    }
    //fmt::print("[{}] Swapped core to {} size: {}\n",NAME, MQ_CORE, MQ.at(MQ_CORE).size());
    //no valid packet, so lets try to issue a prefetch instead
    //get lowest bank_occupancy_counter
    
  }

}

bool CACHE::handle_write(const tag_lookup_type& handle_pkt)
{
  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} instr_id: {} address: {} v_address: {} type: {} local_prefetch: {} cycle: {}\n", NAME, __func__, handle_pkt.instr_id,
               handle_pkt.address, handle_pkt.v_address, access_type_names.at(champsim::to_underlying(handle_pkt.type)), handle_pkt.prefetch_from_this,
               current_time.time_since_epoch() / clock_period);
  }

  mshr_type to_allocate{handle_pkt, current_time};
  to_allocate.data_promise.ready_at(current_time + (warmup ? champsim::chrono::clock::duration{} : FILL_LATENCY));
  inflight_writes.push_back(to_allocate);

  sim_stats.misses.increment(std::pair{handle_pkt.type, handle_pkt.cpu});

  return true;
}

template <bool UpdateRequest>
auto CACHE::initiate_tag_check(champsim::channel* ul)
{
  return [time = current_time + (warmup ? champsim::chrono::clock::duration{} : HIT_LATENCY), ul](const auto& entry) {
    CACHE::tag_lookup_type retval{entry};
    retval.event_cycle = time;

    if constexpr (UpdateRequest) {
      if (entry.response_requested) {
        retval.to_return = {&ul->returned};
      }
    } else {
      (void)ul; // supress warning about ul being unused
    }

    if constexpr (champsim::debug_print) {
      fmt::print("[TAG] initiate_tag_check instr_id: {} address: {} v_address: {} type: {} response_requested: {} cpu: {}\n", retval.instr_id, retval.address,
                 retval.v_address, access_type_names.at(champsim::to_underlying(retval.type)), !std::empty(retval.to_return), retval.cpu);
    }

    return retval;
  };
}

void CACHE::manage_pq() {
  // Check for PQ duplicates

  for (auto pq_it = std::begin(internal_PQ); pq_it != std::end(internal_PQ);) {
    if(pq_it->forward_checked) {
      pq_it++;
      continue;
    }
    
    bool dupe = false;
    for (auto pq_m = (pq_it + 1); pq_m != std::end(internal_PQ); pq_m++) {
      if(champsim::block_number{pq_it->address} == champsim::block_number{pq_m->address}) {
        if(pq_it->skip_fill == pq_m->skip_fill && pq_it->is_translated == pq_m->is_translated && pq_it->pf_metadata == pq_m->pf_metadata) {
          dupe = true;
          //merge
          std::vector<uint64_t> merged_instr{};
          std::vector<std::deque<response_type>*> merged_return{};
        
          std::set_union(std::begin(pq_it->instr_depend_on_me), std::end(pq_it->instr_depend_on_me), std::begin(pq_m->instr_depend_on_me),
                         std::end(pq_m->instr_depend_on_me), std::back_inserter(merged_instr));
          std::set_union(std::begin(pq_it->to_return), std::end(pq_it->to_return), std::begin(pq_m->to_return), std::end(pq_m->to_return),
                         std::back_inserter(merged_return));
          pq_m->to_return = merged_return;
          pq_m->instr_depend_on_me = merged_instr;
          break;
        }
      }
    }
    if(!dupe) {
      pq_it->forward_checked = true;
      pq_it++;
    } 
    else {
      pq_it = internal_PQ.erase(pq_it);
    }
  }
}
long CACHE::operate()
{
  long progress{0};

  auto is_ready = [time = current_time](const auto& entry) {
    return entry.event_cycle <= time;
  };
  auto is_translated = [](const auto& entry) {
    return entry.is_translated;
  };

  for (auto* ul : upper_levels) {
    ul->check_collision();
  }

  //fmt::print("[{}] Length IFT: {} Length PQ: {} Translation Stash: {}\n",NAME,std::size(inflight_tag_check),std::size(internal_PQ), std::size(translation_stash));

  //do internal PQ merges
  manage_pq();

  // Finish returns
  auto do_finish_packet = [this](const auto& pkt) {
    return this->finish_packet(pkt);
  };
  auto finish_packet_end = std::stable_partition(std::begin(lower_level->returned),std::end(lower_level->returned), do_finish_packet);
  //std::for_each(std::cbegin(lower_level->returned), std::cend(lower_level->returned), [this](const auto& pkt) { this->finish_packet(pkt); });
  progress += std::distance(std::begin(lower_level->returned), finish_packet_end);
  lower_level->returned.erase(std::begin(lower_level->returned), finish_packet_end);

  // Finish translations
  if (lower_translate != nullptr) {
    std::for_each(std::cbegin(lower_translate->returned), std::cend(lower_translate->returned), [this](const auto& pkt) { this->finish_translation(pkt); });
    progress += std::distance(std::cbegin(lower_translate->returned), std::cend(lower_translate->returned));
    lower_translate->returned.clear();
  }

  // Perform fills
  champsim::bandwidth fill_bw{MAX_FILL};
  for (auto q : {std::ref(MSHR), std::ref(inflight_writes)}) {
    auto [fill_begin, fill_end] = champsim::get_span_p(std::cbegin(q.get()), std::cend(q.get()), fill_bw,
                                                       [time = current_time](const auto& x) { return x.data_promise.is_ready_at(time) || x.type == access_type::DROPPED;  });
    auto complete_end = std::find_if_not(fill_begin, fill_end, [this](const auto& x) { return this->handle_fill(x); });
    fill_bw.consume(std::distance(fill_begin, complete_end));
    q.get().erase(fill_begin, complete_end);
  }

  // Initiate tag checks
  const champsim::bandwidth::maximum_type bandwidth_from_tag_checks{champsim::to_underlying(MAX_TAG) * (long)(HIT_LATENCY / clock_period)
                                                                    - (long)std::size(inflight_tag_check)};
  champsim::bandwidth initiate_tag_bw{std::clamp(bandwidth_from_tag_checks, champsim::bandwidth::maximum_type{0}, MAX_TAG)};
  auto can_translate = [avail = (std::size(translation_stash) < static_cast<std::size_t>(MSHR_SIZE))](const auto& entry) {
    return avail || entry.is_translated;
  };
  auto stash_bandwidth_consumed =
      champsim::transform_while_n(translation_stash, std::back_inserter(inflight_tag_check), initiate_tag_bw, is_translated, initiate_tag_check<false>());
  initiate_tag_bw.consume(stash_bandwidth_consumed);
  std::vector<long long> channels_bandwidth_consumed{};

  if (std::size(upper_levels) > 1) {
    std::rotate(upper_levels.begin(), upper_levels.begin() + 1, upper_levels.end());
  }

  // upper levels get an equal portion of the remaining bandwidth
  champsim::bandwidth::maximum_type per_upper_bandwidth =
      std::size(upper_levels) >= 1
          ? (champsim::bandwidth::maximum_type)std::max((size_t)initiate_tag_bw.amount_remaining() / std::size(upper_levels), size_t{1})
          : champsim::bandwidth::maximum_type{};

  for (auto* ul : upper_levels) {
    for (auto q : {std::ref(ul->WQ), std::ref(ul->RQ), std::ref(ul->PQ)}) {
      // this needs to be in this loop, we need to ensure that for cases where bandwidth doesn't divide nicely across upstreams,
      // we don't accidentally consume more bandwidth than expected
      champsim::bandwidth per_upper_tag_bw{std::min(per_upper_bandwidth, champsim::bandwidth::maximum_type{initiate_tag_bw.amount_remaining()})};
      auto bandwidth_consumed =
          champsim::transform_while_n(q.get(), std::back_inserter(inflight_tag_check), per_upper_tag_bw, can_translate, initiate_tag_check<true>(ul));
      channels_bandwidth_consumed.push_back(bandwidth_consumed);
      initiate_tag_bw.consume(bandwidth_consumed);
    }
  }

  auto pq_bandwidth_consumed =
      champsim::transform_while_n(internal_PQ, std::back_inserter(inflight_tag_check), initiate_tag_bw, can_translate, initiate_tag_check<false>());
  initiate_tag_bw.consume(pq_bandwidth_consumed);

  // Issue translations
  std::for_each(std::begin(inflight_tag_check), std::end(inflight_tag_check), [this](auto& x) { this->issue_translation(x); });
  std::for_each(std::begin(translation_stash), std::end(translation_stash), [this](auto& x) { this->issue_translation(x); });

  // Find entries that would be ready except that they have not finished translation, move them to the stash
  auto [last_not_missed, stash_end] = champsim::extract_if(std::begin(inflight_tag_check), std::end(inflight_tag_check), std::back_inserter(translation_stash),
                                                           [is_ready, is_translated](const auto& x) { return is_ready(x) && !is_translated(x); });
  progress += std::distance(last_not_missed, std::end(inflight_tag_check));
  inflight_tag_check.erase(last_not_missed, std::end(inflight_tag_check));

  // Perform tag checks
  auto do_handle_miss = [this](const auto& pkt) {
    if (pkt.type == access_type::WRITE && !this->match_offset_bits) {
      return this->handle_write(pkt); // Treat writes (that is, writebacks) like fills
    }
    return this->handle_miss(pkt); // Treat writes (that is, stores) like reads
  };
  champsim::bandwidth tag_check_bw{MAX_TAG};
  auto [tag_check_ready_begin, tag_check_ready_end] =
      champsim::get_span_p(std::begin(inflight_tag_check), std::end(inflight_tag_check), tag_check_bw,
                           [is_ready, is_translated](const auto& pkt) { return is_ready(pkt) && is_translated(pkt); });
  
  //fmt::print("[{}] Start: {} End: {}\n",NAME,std::distance(inflight_tag_check.begin(),tag_check_ready_begin),std::distance(inflight_tag_check.begin(),tag_check_ready_end));
  auto hits_end = std::stable_partition(tag_check_ready_begin, tag_check_ready_end, [this](auto& pkt) { return this->try_hit(pkt); });

  auto finish_tag_check_end = std::stable_partition(hits_end, tag_check_ready_end, do_handle_miss);
  tag_check_bw.consume(std::distance(tag_check_ready_begin, finish_tag_check_end));
  inflight_tag_check.erase(tag_check_ready_begin, finish_tag_check_end);

  for(int i = 0; i < (warmup ? NUM_CPUS : std::max(1l,champsim::to_underlying(MAX_TAG))); i++)
    schedule_mshr();

  impl_prefetcher_cycle_operate();

  if constexpr (champsim::debug_print) {
    fmt::print("[{}] {} cycle completed: {} tags checked: {} remaining: {} stash consumed: {} remaining: {} channel consumed: {} pq consumed {} unused consume "
               "bw {}\n",
               NAME, __func__, current_time.time_since_epoch() / clock_period, tag_check_bw.amount_consumed(), std::size(inflight_tag_check),
               stash_bandwidth_consumed, std::size(translation_stash), channels_bandwidth_consumed, pq_bandwidth_consumed, initiate_tag_bw.amount_remaining());
  }

    /*
  if((current_cycle() + 1) % print_report_interval == 0) {
    if(NAME.compare("LLC") == 0 && !warmup) {
      fmt::print("[{}] CC Values:\n",NAME);
      for(int i = 0; i < NUM_CPUS; i++) {
        fmt::print("\t {}: {}\n", i, prefetch_limits[i]);
      }
    }
    if(NAME.compare("LLC") == 0 && !warmup) {
      fmt::print("[{}] Demand MSHR Occupancy:\n",NAME);
      for(int i = 0; i < NUM_CPUS; i++) {
        fmt::print("\t {}: {}\n", i, demands_in_mshr[i]);
      }
    }
    if(NAME.compare("LLC") == 0 && !warmup) {
      fmt::print("[{}] Prefetch MSHR Occupancy:\n",NAME);
      for(int i = 0; i < NUM_CPUS; i++) {
        fmt::print("\t {}: {}\n", i, prefetches_in_mshr[i]);
      }
    }
    if(NAME.compare("LLC") == 0 && !warmup) {
      fmt::print("[{}] MQ Occupancy:\n",NAME);
      for(int i = 0; i < NUM_CPUS; i++) {
        fmt::print("\t {}: {}\n", i, MQ[i].size());
      }
    }
  }*/

  if ((current_cycle() + 1) % pf_report_interval == 0) {
    //redo core scheduling
    std::iota(MQ_CORE.begin(),MQ_CORE.end(),0);
    std::sort(MQ_CORE.begin(),MQ_CORE.end(), [&miss_counter = MQ_MISS_COUNTER](std::size_t i1, std::size_t i2) {
      return miss_counter[i1] < miss_counter[i2];
    });
    //clear miss counter
    MQ_MISS_COUNTER = std::vector<std::size_t>(NUM_CPUS,0);
    bool do_print_status = (current_cycle() + 1) % (pf_report_interval*10) == 0;

    //if(do_print_status)
    //fmt::print("[{}] Bank Request Counts, MSHR OCCUPANCY: {}\n",NAME,MSHR.size());
    for(int i = 0; i < OUTGOING_BANK_REQUESTS.size(); i++) {
      //if(do_print_status)
      //fmt::print("\t {}: {}\n",i,OUTGOING_BANK_REQUESTS.at(i));
    }
    //if(do_print_status)
    //fmt::print("[{}] MQ Occupancies\n",NAME);
    for(int i = 0; i < NUM_CPUS; i++) {
      //if(do_print_status)
      //fmt::print("\t {}: {}\n", i, MQ.at(i).size());
    }
    if(do_print_status && NAME.compare("LLC") == 0) {
      fmt::print("[{}] Occupancy: {}\n",NAME,get_cache_occupancy_ratio());
    }
    for(int core = 0; core < NUM_CPUS; core++) {
      double pf_useless = sim_stats.pf_useless_core.value_or(core,0) - sim_stats.last_pf_useless_core.value_or(core,0);
      double pf_useful = sim_stats.pf_useful_core.value_or(core,0) - sim_stats.last_pf_useful_core.value_or(core,0);
      if(pf_useless != 0) {
        Ramulator::set_core_prefetch_usefulness(this,core, pf_useful / (pf_useless + pf_useful));
        prefetch_usefulness[std::pair{this,core}] = pf_useful / (pf_useless + pf_useful);
        if(do_print_status)
        fmt::print("[{}] Core: {} Prefetch Usefulness: {}\n",NAME,core,pf_useful / (pf_useless + pf_useful));
      }
      else if(pf_useful != 0) {
        Ramulator::set_core_prefetch_usefulness(this,core,1.0);
        prefetch_usefulness[std::pair{this,core}] = 1.0;
        if(do_print_status)
          fmt::print("[{}] Core: {} Prefetch Usefulness: {}\n",NAME,core,1.0);
      }
      else {
        prefetch_usefulness[std::pair{this,core}] = 1.0;
        Ramulator::set_core_prefetch_usefulness(this,core,1.0);
      }
      sim_stats.last_pf_useful_core.set(core,sim_stats.pf_useful_core.value_or(core,0));
      sim_stats.last_pf_useless_core.set(core,sim_stats.pf_useless_core.value_or(core,0));
    }
  }

  return progress + fill_bw.amount_consumed() + initiate_tag_bw.amount_consumed() + tag_check_bw.amount_consumed();
}

// LCOV_EXCL_START exclude deprecated function
uint64_t CACHE::get_set(uint64_t address, uint32_t cpu) const { return static_cast<uint64_t>(get_set_index(champsim::address{address},cpu)); }
// LCOV_EXCL_STOP

long CACHE::get_set_index(champsim::address address, uint32_t cpu) const {
  if(partition_cache) {
    long set = address.slice(champsim::dynamic_extent{OFFSET_BITS, champsim::lg2(NUM_SET)}).to<long>(); 
    return (set % (NUM_SET/NUM_CPUS)) + (cpu * (NUM_SET/NUM_CPUS));
  }
  else
    return address.slice(champsim::dynamic_extent{OFFSET_BITS, champsim::lg2(NUM_SET)}).to<long>(); 
}

template <typename It>
std::pair<It, It> get_span(It anchor, typename std::iterator_traits<It>::difference_type set_idx, typename std::iterator_traits<It>::difference_type num_way)
{
  auto begin = std::next(anchor, set_idx * num_way);
  return {std::move(begin), std::next(begin, num_way)};
}

auto CACHE::get_set_span(champsim::address address, uint32_t cpu) -> std::pair<set_type::iterator, set_type::iterator>
{
  const auto set_idx = get_set_index(address, cpu);
  assert(set_idx < NUM_SET);
  return get_span(std::begin(block), static_cast<set_type::difference_type>(set_idx), NUM_WAY); // safe cast because of prior assert
}

auto CACHE::get_set_span(champsim::address address, uint32_t cpu) const -> std::pair<set_type::const_iterator, set_type::const_iterator>
{
  const auto set_idx = get_set_index(address, cpu);
  assert(set_idx < NUM_SET);
  return get_span(std::cbegin(block), static_cast<set_type::difference_type>(set_idx), NUM_WAY); // safe cast because of prior assert
}

// LCOV_EXCL_START exclude deprecated function
uint64_t CACHE::get_way(uint64_t address, uint64_t /*unused set index*/, uint32_t cpu) const
{
  champsim::address intern_addr{address};
  auto [begin, end] = get_set_span(intern_addr, cpu);
  return static_cast<uint64_t>(std::distance(begin, std::find_if(begin, end, matches_address(champsim::address{address}))));
}
// LCOV_EXCL_STOP

long CACHE::invalidate_entry(champsim::address inval_addr, uint32_t cpu)
{
  auto [begin, end] = get_set_span(inval_addr, cpu);
  auto inv_way = std::find_if(begin, end, matches_address(inval_addr));

  if (inv_way != end) {
    inv_way->valid = false;
  }

  return std::distance(begin, inv_way);
}

std::pair<bool, long> CACHE::early_writeback(champsim::address wb_addr, uint32_t wb_cpu) {
  auto [begin, end] = get_set_span(wb_addr, wb_cpu);
  auto wb_way = std::find_if(begin,end,matches_address(wb_addr));

  bool success = false;
  if(wb_way != end) {
    if(wb_way->dirty) {
      wb_way->dirty = false;
      request_type writeback_packet;
      writeback_packet.cpu = wb_cpu;
      writeback_packet.address = wb_way->address;
      writeback_packet.data = wb_way->data;
      writeback_packet.instr_id = 0;
      writeback_packet.ip = champsim::address{};
      writeback_packet.type = access_type::WRITE;
      writeback_packet.pf_metadata = wb_way->pf_metadata;
      writeback_packet.response_requested = false;

      if constexpr (champsim::debug_print) {
        fmt::print("[{}] {} writeback address: {} v_address: {} prefetch_metadata: {}\n", NAME, __func__, writeback_packet.address, writeback_packet.v_address,
                  writeback_packet.pf_metadata);
      }

      auto wb_success = lower_level->add_wq(writeback_packet);
      if (wb_success) {
        success = true;
        sim_stats.downstream_packets.increment(std::pair{writeback_packet.type, writeback_packet.cpu});
      }
    }
  }

  return std::pair<bool,long>{success,std::distance(begin, wb_way)};
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
  pf_packet.cpu = cpu;
  pf_packet.address = pf_addr;
  pf_packet.source_ptr = this;
  pf_packet.v_address = virtual_prefetch ? pf_addr : champsim::address{};
  pf_packet.is_translated = !virtual_prefetch;
  pf_packet.pf_distance = pf_base.to<uint64_t>() > pf_addr.to<uint64_t>() ? champsim::block_number{pf_base}.to<uint64_t>() - champsim::block_number{pf_addr}.to<uint64_t>() : champsim::block_number{pf_addr}.to<uint64_t>() - champsim::block_number{pf_base}.to<uint64_t>();

  internal_PQ.emplace_back(pf_packet, true, !fill_this_level, this, false);
  ++sim_stats.pf_issued;

  return true;
}

void CACHE::drop_prefetch_access(champsim::address pf_addr) {
  marked_for_drop = pf_addr;
}

bool CACHE::prefetch_line(champsim::address pf_addr, bool fill_this_level, uint32_t pf_cpu, champsim::address pf_ip, uint32_t prefetch_metadata, bool skip_tag_check, bool return_hit_status)
{
  ++sim_stats.pf_requested;
  request_type pf_packet;
  pf_packet.type = access_type::PREFETCH;
  pf_packet.pf_metadata = prefetch_metadata;
  pf_packet.cpu = pf_cpu;
  pf_packet.address = pf_addr;
  pf_packet.ip = pf_ip;
  pf_packet.source_ptr = this;
  pf_packet.v_address = virtual_prefetch ? pf_addr : champsim::address{};
  pf_packet.is_translated = !virtual_prefetch;
  pf_packet.pf_distance = pf_base.to<uint64_t>() > pf_addr.to<uint64_t>() ? champsim::block_number{pf_base}.to<uint64_t>() - champsim::block_number{pf_addr}.to<uint64_t>() : champsim::block_number{pf_addr}.to<uint64_t>() - champsim::block_number{pf_base}.to<uint64_t>();

  if(skip_tag_check) {
    return handle_miss(tag_lookup_type{pf_packet, true, !fill_this_level, this, return_hit_status});
  } else {
    if (std::size(internal_PQ) >= PQ_SIZE) {
      return false;
    }
    internal_PQ.emplace_back(pf_packet, true, !fill_this_level, this, return_hit_status);
    ++sim_stats.pf_issued;

    return true;
  }
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

bool CACHE::finish_packet(const response_type& packet)
{
  // check MSHR information
  if constexpr (champsim::debug_print)
    fmt::print("[{}_MSHR] Packet returned, type: {} address: {} cycle: {}\n",NAME, access_type_names.at(champsim::to_underlying(packet.type)), packet.address, current_cycle());

  auto mshr_entry = std::find_if(std::begin(MSHR), std::end(MSHR), matches_address(packet.address));
  auto first_unreturned = std::find_if(MSHR.begin(), MSHR.end(), [](auto x) { return x.data_promise.has_unknown_readiness(); });

  bool expect_empty = packet.type == access_type::DROPPED;
  assert(packet.type != access_type::PROMOTION);
  // sanity check
  if ((mshr_entry == MSHR.end() || !mshr_entry->data_promise.has_unknown_readiness()) && !expect_empty) {
    fmt::print(stderr, "[{}_MSHR] {} cannot find a matching entry! address: {} v_address: {}\n", NAME, __func__, packet.address, packet.v_address);
    assert(0);
  }
  else if (mshr_entry == MSHR.end() || !mshr_entry->data_promise.has_unknown_readiness()) {
    if constexpr (champsim::debug_print)
      fmt::print("[{}_MSHR] Excess packet arrived, type: {} address: {} cycle: {}\n",NAME, access_type_names.at(champsim::to_underlying(packet.type)), packet.address, current_cycle());
    sim_stats.returned_packets.increment(std::pair{packet.type, 0});
    return true;
  }
  if constexpr (champsim::debug_print)
    fmt::print("[{}_MSHR] Packet closing MSHR, type: {} address: {} cycle: {}\n",NAME, access_type_names.at(champsim::to_underlying(mshr_entry->type)), mshr_entry->address, current_cycle());
  if(packet.type == access_type::LOAD && mshr_entry->was_promoted) {
    sim_stats.returned_packets.increment(std::pair{access_type::PROMOTION, mshr_entry->cpu});
    if constexpr (champsim::debug_print)
      fmt::print("[{}_MSHR] Promotion returned, type: {} address: {} cycle: {}\n",NAME, access_type_names.at(champsim::to_underlying(packet.type)), packet.address, current_cycle());
  }
  else if(mshr_entry->was_promoted && packet.type == access_type::PREFETCH) {
    sim_stats.pr_missed++;
    if constexpr (champsim::debug_print)
      fmt::print("[{}_MSHR] Promotion missed, type: {} address: {} cycle: {}\n",NAME, access_type_names.at(champsim::to_underlying(packet.type)), packet.address, current_cycle());
  }
  else
    sim_stats.returned_packets.increment(std::pair{packet.type, mshr_entry->cpu});

  if(packet.type == access_type::DROPPED && mshr_entry->type != access_type::PREFETCH) {
    request_type refetch_request;
    refetch_request.cpu = mshr_entry->cpu;
    refetch_request.address = mshr_entry->address;
    refetch_request.data = mshr_entry->data_promise->data;
    refetch_request.instr_id = mshr_entry->instr_id;
    refetch_request.ip = mshr_entry->ip;
    refetch_request.type = access_type::REFETCH;
    refetch_request.pf_metadata = mshr_entry->data_promise->pf_metadata;
    refetch_request.response_requested = true;
    auto success = lower_level->add_rq(refetch_request);

    if (!success) {
      return false;
    }
    sim_stats.downstream_packets.increment(std::pair{refetch_request.type, refetch_request.cpu});
    if constexpr (champsim::debug_print) {
      fmt::print("[{}_MSHR] Retransmitting dropped packet: {}, was updated from PREFETCH to {}\n",this->NAME,mshr_entry->address,access_type_names.at(champsim::to_underlying(mshr_entry->type)));
    }
    return true;
  }

  // MSHR holds the most updated information about this request
  mshr_type::returned_value finished_value{packet.data, packet.pf_metadata};

  //fmt::print("[{}] Decreasing outgoing bank request counter for prefetch: {} address: {} bank: {}, now: {}\n",NAME,fill_mshr.type == access_type::PREFETCH, fill_mshr.address,MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(fill_mshr.address),OUTGOING_BANK_REQUESTS.at(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(fill_mshr.address))-1);
  if(mshr_entry->data_promise.has_unknown_readiness()) {
    OUTGOING_BANK_REQUESTS.at(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(mshr_entry->address))--;
  }
  mshr_entry->data_promise = champsim::waitable{finished_value, current_time + (warmup ? champsim::chrono::clock::duration{} : FILL_LATENCY)};
  mshr_entry->back_off |= packet.back_off;
  mshr_entry->row_act |= packet.row_act;

  //if dropped, update to dropped type and set it to finish now
  if(packet.type == access_type::DROPPED && mshr_entry->type == access_type::PREFETCH) {
    mshr_entry->type = access_type::DROPPED;
    mshr_entry->data_promise = champsim::waitable{finished_value, current_time};
  }

  if constexpr (champsim::debug_print) {
    fmt::print("[{}_MSHR] finish_packet instr_id: {} address: {} data: {} type: {} current: {}\n", this->NAME, mshr_entry->instr_id, mshr_entry->address,
               mshr_entry->data_promise->data, access_type_names.at(champsim::to_underlying(mshr_entry->type)), current_time.time_since_epoch() / clock_period);
  }

  // Order this entry after previously-returned entries, but before non-returned
  // entries
  std::iter_swap(mshr_entry, first_unreturned);

  return true;
}

void CACHE::finish_translation(const response_type& packet)
{
  auto matches_vpage = [page_num = champsim::page_number{packet.v_address}](const auto& entry) {
    return (champsim::page_number{entry.v_address} == page_num) && !entry.is_translated;
  };
  auto mark_translated = [p_page = champsim::page_number{packet.data}, this](auto& entry) {
    [[maybe_unused]] auto old_address = entry.address;
    entry.address = champsim::address{champsim::splice(p_page, champsim::page_offset{entry.v_address})}; // translated address
    entry.is_translated = true;                                                                          // This entry is now translated

    if constexpr (champsim::debug_print) {
      fmt::print("[{}_TRANSLATE] finish_translation old: {} paddr: {} vaddr: {} type: {} cycle: {}\n", this->NAME, old_address, entry.address, entry.v_address,
                 access_type_names.at(champsim::to_underlying(entry.type)), this->current_time.time_since_epoch() / this->clock_period);
    }
  };

  // Restart stashed translations
  auto finish_begin = std::find_if_not(std::begin(translation_stash), std::end(translation_stash), [](const auto& x) { return x.is_translated; });
  auto finish_end = std::stable_partition(finish_begin, std::end(translation_stash), matches_vpage);
  std::for_each(finish_begin, finish_end, mark_translated);

  // Find all packets that match the page of the returned packet
  for (auto& entry : inflight_tag_check) {
    if (matches_vpage(entry)) {
      mark_translated(entry);
    }
  }
}

void CACHE::issue_translation(tag_lookup_type& q_entry) const
{
  if (!q_entry.translate_issued && !q_entry.is_translated) {
    request_type fwd_pkt;
    fwd_pkt.asid[0] = q_entry.asid[0];
    fwd_pkt.asid[1] = q_entry.asid[1];
    fwd_pkt.type = access_type::LOAD;
    fwd_pkt.cpu = q_entry.cpu;

    fwd_pkt.address = q_entry.address;
    fwd_pkt.v_address = q_entry.v_address;
    fwd_pkt.data = q_entry.data;
    fwd_pkt.instr_id = q_entry.instr_id;
    fwd_pkt.ip = q_entry.ip;
    fwd_pkt.back_off = q_entry.back_off;
    fwd_pkt.row_act = q_entry.row_act;

    fwd_pkt.instr_depend_on_me = q_entry.instr_depend_on_me;
    fwd_pkt.is_translated = true;

    q_entry.translate_issued = lower_translate->add_rq(fwd_pkt);
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

void CACHE::impl_prefetcher_initialize() const { pref_module_pimpl->impl_prefetcher_initialize(); }

uint32_t CACHE::impl_prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, bool cache_hit, bool useful_prefetch, access_type type,
                                              uint32_t metadata_in, uint32_t metadata_hit) const
{
  return pref_module_pimpl->impl_prefetcher_cache_operate(addr, ip, cpu, cache_hit, useful_prefetch, type, metadata_in, metadata_hit);
}

uint32_t CACHE::impl_prefetcher_cache_fill(champsim::address addr, champsim::address ip, uint32_t cpu, bool useless, long set, long way, bool prefetch, champsim::address evicted_addr,
                                           uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict) const
{
  return pref_module_pimpl->impl_prefetcher_cache_fill(addr, ip, cpu, useless, set, way, prefetch, evicted_addr, metadata_in, metadata_evict, cpu_evict);
}

void CACHE::impl_prefetcher_cycle_operate() const { pref_module_pimpl->impl_prefetcher_cycle_operate(); }

void CACHE::impl_prefetcher_final_stats() const { pref_module_pimpl->impl_prefetcher_final_stats(); }

void CACHE::impl_prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) const
{
  pref_module_pimpl->impl_prefetcher_branch_operate(ip, branch_type, branch_target);
}

void CACHE::impl_initialize_replacement() const { repl_module_pimpl->impl_initialize_replacement(); }

long CACHE::impl_find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, const BLOCK* current_set, champsim::address ip, champsim::address full_addr,
                             access_type type, bool prefetch) const
{
  return repl_module_pimpl->impl_find_victim(triggering_cpu, instr_id, set, current_set, ip, full_addr, type, prefetch);
}

void CACHE::impl_update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                          champsim::address victim_addr, access_type type, bool hit, bool prefetch) const
{
  repl_module_pimpl->impl_update_replacement_state(triggering_cpu, set, way, full_addr, ip, victim_addr, type, hit, prefetch);
}

void CACHE::impl_replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                        champsim::address victim_addr, access_type type, bool prefetch) const
{
  repl_module_pimpl->impl_replacement_cache_fill(triggering_cpu, set, way, full_addr, ip, victim_addr, type, prefetch);
}

void CACHE::impl_replacement_final_stats() const { repl_module_pimpl->impl_replacement_final_stats(); }

void CACHE::initialize()
{
  impl_prefetcher_initialize();
  impl_initialize_replacement();
}

void CACHE::begin_phase()
{
  stats_type new_roi_stats;
  stats_type new_sim_stats;

  new_roi_stats.name = NAME;
  new_sim_stats.name = NAME;

  roi_stats = new_roi_stats;
  sim_stats = new_sim_stats;

  for (auto* ul : upper_levels) {
    channel_type::stats_type ul_new_roi_stats;
    channel_type::stats_type ul_new_sim_stats;
    ul->roi_stats = ul_new_roi_stats;
    ul->sim_stats = ul_new_sim_stats;
  }
}

void CACHE::end_phase(unsigned finished_cpu)
{
  roi_stats.total_miss_latency_cycles = sim_stats.total_miss_latency_cycles;
  roi_stats.total_returned_packets = sim_stats.total_returned_packets;

  for (auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION, access_type::PROMOTION, access_type::DROPPED, access_type::REFETCH}) {
    std::pair key{type, finished_cpu};
    roi_stats.hits.set(key, sim_stats.hits.value_or(key, 0));
    roi_stats.misses.set(key, sim_stats.misses.value_or(key, 0));
    roi_stats.downstream_packets.set(key,sim_stats.downstream_packets.value_or(key,0));
    roi_stats.returned_packets.set(key,sim_stats.returned_packets.value_or(key,0));
  }
  roi_stats.pf_useful_core.set(finished_cpu,sim_stats.pf_useful_core.value_or(finished_cpu,0));
  roi_stats.pf_useless_core.set(finished_cpu,sim_stats.pf_useless_core.value_or(finished_cpu,0));
  roi_stats.last_pf_useful_core.set(finished_cpu,sim_stats.last_pf_useful_core.value_or(finished_cpu,0));
  roi_stats.last_pf_useless_core.set(finished_cpu,sim_stats.last_pf_useless_core.value_or(finished_cpu,0));
  roi_stats.pf_requested = sim_stats.pf_requested;
  roi_stats.pf_issued = sim_stats.pf_issued;
  roi_stats.pf_useful = sim_stats.pf_useful;
  roi_stats.pf_useless = sim_stats.pf_useless;
  roi_stats.pf_fill = sim_stats.pf_fill;
  roi_stats.pr_missed = sim_stats.pr_missed;

  for (auto* ul : upper_levels) {
    ul->roi_stats.RQ_ACCESS = ul->sim_stats.RQ_ACCESS;
    ul->roi_stats.RQ_MERGED = ul->sim_stats.RQ_MERGED;
    ul->roi_stats.RQ_FULL = ul->sim_stats.RQ_FULL;
    ul->roi_stats.RQ_TO_CACHE = ul->sim_stats.RQ_TO_CACHE;

    ul->roi_stats.PQ_ACCESS = ul->sim_stats.PQ_ACCESS;
    ul->roi_stats.PQ_MERGED = ul->sim_stats.PQ_MERGED;
    ul->roi_stats.PQ_FULL = ul->sim_stats.PQ_FULL;
    ul->roi_stats.PQ_TO_CACHE = ul->sim_stats.PQ_TO_CACHE;

    ul->roi_stats.WQ_ACCESS = ul->sim_stats.WQ_ACCESS;
    ul->roi_stats.WQ_MERGED = ul->sim_stats.WQ_MERGED;
    ul->roi_stats.WQ_FULL = ul->sim_stats.WQ_FULL;
    ul->roi_stats.WQ_TO_CACHE = ul->sim_stats.WQ_TO_CACHE;
    ul->roi_stats.WQ_FORWARD = ul->sim_stats.WQ_FORWARD;
  }
}

template <typename T>
bool CACHE::should_activate_prefetcher(const T& pkt) const
{
  return (!pkt.prefetch_from_this && !pkt.invoked_prefetcher && std::count(std::begin(pref_activate_mask), std::end(pref_activate_mask), pkt.type) > 0) || (pkt.return_hit_status && !pkt.invoked_prefetcher);
}

// LCOV_EXCL_START Exclude the following function from LCOV
void CACHE::print_deadlock()
{
  std::string_view mshr_write{"instr_id: {} address: {} v_addr: {} type: {} ready: {} promoted: {}"};
  auto mshr_pack = [time = current_time](const auto& entry) {
    return std::tuple{entry.instr_id, entry.address, entry.v_address, access_type_names.at(champsim::to_underlying(entry.type)),
                      entry.data_promise.is_ready_at(time),entry.was_promoted};
  };

  std::string_view tag_check_write{"instr_id: {} address: {} v_addr: {} is_translated: {} translate_issued: {} event_cycle: {}"};
  auto tag_check_pack = [period = clock_period](const auto& entry) {
    return std::tuple{entry.instr_id,      entry.address,          entry.v_address,
                      entry.is_translated, entry.translate_issued, entry.event_cycle.time_since_epoch() / period};
  };

  champsim::range_print_deadlock(MSHR, NAME + "_MSHR", mshr_write, mshr_pack);
  champsim::range_print_deadlock(inflight_tag_check, NAME + "_tags", tag_check_write, tag_check_pack);
  champsim::range_print_deadlock(translation_stash, NAME + "_translation", tag_check_write, tag_check_pack);

  std::string_view q_writer{"instr_id: {} address: {} v_addr: {} type: {} translated: {}"};
  auto q_entry_pack = [](const auto& entry) {
    return std::tuple{entry.instr_id, entry.address, entry.v_address, access_type_names.at(champsim::to_underlying(entry.type)), entry.is_translated};
  };

  for (auto* ul : upper_levels) {
    champsim::range_print_deadlock(ul->RQ, NAME + "_RQ", q_writer, q_entry_pack);
    champsim::range_print_deadlock(ul->WQ, NAME + "_WQ", q_writer, q_entry_pack);
    champsim::range_print_deadlock(ul->PQ, NAME + "_PQ", q_writer, q_entry_pack);
  }
}
// LCOV_EXCL_STOP

//void CACHE::report_prefetch_usefulness(uint32_t pf_cpu, double usefulness) {
  //fmt::print("[{}] CPU: {} Usefulness: {} MSHR_OCCUPANCY: {} MQ_OCCUPANCY: {} MQ_MAX: {}\n",NAME,pf_cpu,usefulness, get_mshr_occupancy_ratio(), MQ.at(MQ_CORE).size(), MQ_SIZE);
  //prefetch_usefulness[std::pair{this,pf_cpu}] = usefulness;
  //Ramulator::set_core_prefetch_usefulness(this,pf_cpu, usefulness);
//}
