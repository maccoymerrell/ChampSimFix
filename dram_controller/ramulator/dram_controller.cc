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

#include "dram_controller.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <fmt/core.h>

#include "deadlock.h"
#include "instruction.h"
#include "util/bits.h" // for lg2, bitmask
#include "util/span.h"
#include "util/units.h"

MEMORY_CONTROLLER::MEMORY_CONTROLLER(champsim::chrono::picoseconds clock_period_, champsim::chrono::picoseconds t_rp, champsim::chrono::picoseconds t_rcd,
                                     champsim::chrono::picoseconds t_cas, champsim::chrono::microseconds refresh_period, champsim::chrono::picoseconds turnaround, std::vector<channel_type*>&& ul,
                                     std::size_t rq_size, std::size_t wq_size, std::size_t chans, champsim::data::bytes chan_width, std::size_t rows,
                                     std::size_t columns, std::size_t ranks, std::size_t banks, std::size_t rows_per_refresh, std::string model_config_file)
    : champsim::operable(clock_period_), queues(std::move(ul)), channel_width(chan_width)
{
  //this line can be used to read in the config as a file (this might be easier and more intuitive for users familiar with Ramulator)
  //the full file path should be included, otherwise Ramulator looks in the current working directory (BAD)
  config = Ramulator::Config::parse_config_file(model_config_file, {});

  //force frontend to be champsim, this ensures we are linked properly here
  config["Frontend"]["impl"] = "ChampSim";

  //force memory controller clock scale to 1 (this doesnt do anything as far as I know, but should ensure consistency)
  config["MemorySystem"]["clock_ratio"] = 1;

  //create our frontend (us) and the memory system (ramulator)
  ramulator2_frontend = Ramulator::Factory::create_frontend(config);
  ramulator2_memorysystem = Ramulator::Factory::create_memory_system(config);

  //connect the two. we can use this connection to get some more information from ramulator
  ramulator2_frontend->connect_memory_system(ramulator2_memorysystem);
  ramulator2_memorysystem->connect_frontend(ramulator2_frontend);

  //correct clock scale for ramulator2 frequency
  clock_period = champsim::chrono::picoseconds(uint64_t(ramulator2_memorysystem->get_tCK() * 1e3));
  //its worth noting here that the rate of calls to ramulator2 should be half of that of champsim's mc model,
  //since Champsim expects a call to the model for every dbus period, and ramulator expects once per memory controller period.

  //this will help report stats
  const auto slicer = DRAM_CHANNEL::make_slicer(LOG2_BLOCK_SIZE + champsim::lg2(chans), rows, columns, ranks, banks);
  for (std::size_t i{0}; i < chans; ++i) {
    channels.emplace_back(clock_period_, t_rp, t_rcd, t_cas, refresh_period, turnaround, rows_per_refresh, chan_width, rq_size, wq_size, slicer);
  }
}

DRAM_CHANNEL::DRAM_CHANNEL(champsim::chrono::picoseconds clock_period_, champsim::chrono::picoseconds t_rp, champsim::chrono::picoseconds t_rcd,
                           champsim::chrono::picoseconds t_cas, champsim::chrono::microseconds refresh_period, champsim::chrono::picoseconds turnaround, std::size_t rows_per_refresh, 
                           champsim::data::bytes width, std::size_t rq_size, std::size_t wq_size, slicer_type slice)
    : champsim::operable(clock_period_), WQ{wq_size}, RQ{rq_size}, address_slicer(slice), DRAM_ROWS_PER_REFRESH(rows_per_refresh), tRP(t_rp), tRCD(t_rcd), 
      tCAS(t_cas), tREF(refresh_period / (rows() / rows_per_refresh)), DRAM_DBUS_TURN_AROUND_TIME(turnaround),
      DRAM_DBUS_RETURN_TIME(std::chrono::duration_cast<champsim::chrono::clock::duration>(clock_period_ * std::ceil(champsim::data::bytes{BLOCK_SIZE} / width)))
{
  request_array_type br(ranks() * banks());
  bank_request = br;
}

auto DRAM_CHANNEL::make_slicer(std::size_t start_pos, std::size_t rows, std::size_t columns, std::size_t ranks, std::size_t banks) -> slicer_type
{
  std::array<std::size_t, slicer_type::size()> params{};
  params.at(SLICER_ROW_IDX) = rows;
  params.at(SLICER_COLUMN_IDX) = columns;
  params.at(SLICER_RANK_IDX) = ranks;
  params.at(SLICER_BANK_IDX) = banks;
  return std::apply([start = start_pos](auto... p) { return champsim::make_contiguous_extent_set(start, champsim::lg2(p)...); }, params);
}

long MEMORY_CONTROLLER::operate()
{
  long progress{0};

  initiate_requests();

  //tick ramulator.
  //we will assume no deadlock, since there are no ways to measure progress
  ramulator2_memorysystem->tick();
  progress = 1;

  
  return progress;
}

long DRAM_CHANNEL::operate()
{
  long progress{0};

  if (warmup) {
    for (auto& entry : RQ) {
      if (entry.has_value()) {
        response_type response{entry->address, entry->v_address, entry->data, entry->pf_metadata, entry->instr_depend_on_me};
        for (auto* ret : entry.value().to_return) {
          ret->push_back(response);
        }

        ++progress;
        entry.reset();
      }
    }

    for (auto& entry : WQ) {
      if (entry.has_value()) {
        ++progress;
      }
      entry.reset();
    }
  }

  check_write_collision();
  check_read_collision();
  progress += finish_dbus_request();
  swap_write_mode();
  progress += schedule_refresh();
  progress += populate_dbus();
  progress += service_packet(schedule_packet());

  return progress;
}

long DRAM_CHANNEL::finish_dbus_request()
{
  long progress{0};

  if (active_request != std::end(bank_request) && active_request->ready_time <= current_time) {
    response_type response{active_request->pkt->value().address, active_request->pkt->value().v_address, active_request->pkt->value().data,
                           active_request->pkt->value().pf_metadata, active_request->pkt->value().instr_depend_on_me};
    for (auto* ret : active_request->pkt->value().to_return) {
      ret->push_back(response);
    }

    active_request->valid = false;

    active_request->pkt->reset();
    active_request = std::end(bank_request);
    ++progress;
  }

  return progress;
}

long DRAM_CHANNEL::schedule_refresh()
{
  long progress = {0};
  //check if we reached refresh cycle

  bool schedule_refresh = current_time >= last_refresh + tREF;
  if(schedule_refresh)
  last_refresh = current_time;
  

  //if so, record stats
  if(schedule_refresh)
  {
    refresh_row += DRAM_ROWS_PER_REFRESH;
    sim_stats.refresh_cycles++;
    if(refresh_row >= rows())
      refresh_row = 0;
  }

  //go through each bank, and handle refreshes
  for (auto it = std::begin(bank_request); it != std::end(bank_request); ++it)
  {
    //refresh is now needed for this bank
    if(schedule_refresh)
    {
      it->need_refresh = true;
    }
    //refresh is being scheduled for this bank
    if(it->need_refresh && !it->valid)
    {
      it->ready_time = current_time + tCAS + tRCD;
      it->need_refresh = false;
      it->under_refresh = true;
    }
    //refresh is done for this bank
    else if(it->under_refresh && it->ready_time <= current_time)
    {
      it->under_refresh = false;
      it->open_row.reset();
      progress++;
    }
  }
  return(progress);
}

void DRAM_CHANNEL::swap_write_mode()
{
  // these values control when to send out a burst of writes
  const std::size_t DRAM_WRITE_HIGH_WM = ((std::size(WQ) * 7) >> 3); // 7/8th
  const std::size_t DRAM_WRITE_LOW_WM = ((std::size(WQ) * 6) >> 3);  // 6/8th
  // const std::size_t MIN_DRAM_WRITES_PER_SWITCH = ((std::size(WQ) * 1) >> 2); // 1/4

  // Check queue occupancy
  auto wq_occu = static_cast<std::size_t>(std::count_if(std::begin(WQ), std::end(WQ), [](const auto& x) { return x.has_value(); }));
  auto rq_occu = static_cast<std::size_t>(std::count_if(std::begin(RQ), std::end(RQ), [](const auto& x) { return x.has_value(); }));

  // Change modes if the queues are unbalanced
  if ((!write_mode && (wq_occu >= DRAM_WRITE_HIGH_WM || (rq_occu == 0 && wq_occu > 0)))
      || (write_mode && (wq_occu == 0 || (rq_occu > 0 && wq_occu < DRAM_WRITE_LOW_WM)))) {
    // Reset scheduled requests
    for (auto it = std::begin(bank_request); it != std::end(bank_request); ++it) {
      // Leave active request on the data bus
      if (it != active_request && it->valid) {
        // Leave rows charged
        if (it->ready_time < (current_time + tCAS)) {
          it->open_row.reset();
        }

        // This bank is ready for another DRAM request
        it->valid = false;
        it->pkt->value().scheduled = false;
        it->pkt->value().ready_time = current_time;
      }
    }

    // Add data bus turn-around time
    if (active_request != std::end(bank_request)) {
      dbus_cycle_available = active_request->ready_time + DRAM_DBUS_TURN_AROUND_TIME; // After ongoing finish
    } else {
      dbus_cycle_available = current_time + DRAM_DBUS_TURN_AROUND_TIME;
    }

    // Invert the mode
    write_mode = !write_mode;
  }
}

// Look for requests to put on the bus
long DRAM_CHANNEL::populate_dbus()
{
 long progress{0};

  auto iter_next_process = std::min_element(std::begin(bank_request), std::end(bank_request),
                                            [](const auto& lhs, const auto& rhs) { return !rhs.valid || (lhs.valid && lhs.ready_time < rhs.ready_time); });
  if (iter_next_process->valid && iter_next_process->ready_time <= current_time) {
    if (active_request == std::end(bank_request) && dbus_cycle_available <= current_time) {
      // Bus is available
      // Put this request on the data bus
      active_request = iter_next_process;
      active_request->ready_time = current_time + DRAM_DBUS_RETURN_TIME;

      if (iter_next_process->row_buffer_hit) {
        if (write_mode) {
          ++sim_stats.WQ_ROW_BUFFER_HIT;
        } else {
          ++sim_stats.RQ_ROW_BUFFER_HIT;
        }
      } else if (write_mode) {
        ++sim_stats.WQ_ROW_BUFFER_MISS;
      } else {
        ++sim_stats.RQ_ROW_BUFFER_MISS;
      }

      ++progress;
    } else {
      // Bus is congested
      if (active_request != std::end(bank_request)) {
        sim_stats.dbus_cycle_congested += (active_request->ready_time - current_time) / clock_period;
      } else {
        sim_stats.dbus_cycle_congested += (dbus_cycle_available - current_time) / clock_period;
      }
      ++sim_stats.dbus_count_congested;
    }
  }

  return progress;
}

std::size_t DRAM_CHANNEL::bank_request_index(champsim::address addr) const
{
  auto op_rank = get_rank(addr);
  auto op_bank = get_bank(addr);
  return op_rank * banks() + op_bank;
}

// Look for queued packets that have not been scheduled
DRAM_CHANNEL::queue_type::iterator DRAM_CHANNEL::schedule_packet()
{
  // Look for queued packets that have not been scheduled
  // prioritize packets that are ready to execute, bank is free
  auto next_schedule = [this](const auto& lhs, const auto& rhs) {

    if (!(rhs.has_value() && !rhs.value().scheduled)) {
      return true;
    }
    if (!(lhs.has_value() && !lhs.value().scheduled)) {
      return false;
    }


    auto lop_idx = this->bank_request_index(lhs.value().address);
    auto rop_idx = this->bank_request_index(rhs.value().address);
    auto rready = !this->bank_request[rop_idx].valid;
    auto lready = !this->bank_request[lop_idx].valid;
    return !(rready ^ lready) ? lhs.value().ready_time <= rhs.value().ready_time : lready;

  };
  queue_type::iterator iter_next_schedule;
  if (write_mode) {
    iter_next_schedule = std::min_element(std::begin(WQ), std::end(WQ), next_schedule);
  } else {
    iter_next_schedule = std::min_element(std::begin(RQ), std::end(RQ), next_schedule);
  }
  return(iter_next_schedule);
}


long DRAM_CHANNEL::service_packet(DRAM_CHANNEL::queue_type::iterator pkt)
{
  long progress{0};
  if (pkt->has_value() && pkt->value().ready_time <= current_time) {
    //auto op_rank = get_rank(pkt->value().address);
    //auto op_bank = get_bank(pkt->value().address);
    auto op_row = get_row(pkt->value().address);
    auto op_idx = bank_request_index(pkt->value().address);

    if (!bank_request[op_idx].valid && !bank_request[op_idx].under_refresh) {
      bool row_buffer_hit = (bank_request[op_idx].open_row.has_value() && *(bank_request[op_idx].open_row) == op_row);

      // this bank is now busy
      auto row_charge_delay = champsim::chrono::clock::duration{bank_request[op_idx].open_row.has_value() ? tRP + tRCD : tRCD};
      bank_request[op_idx] = {true,row_buffer_hit,false,false,std::optional{op_row}, current_time + tCAS + (row_buffer_hit ? champsim::chrono::clock::duration{} : row_charge_delay),pkt};
      pkt->value().scheduled = true;
      pkt->value().ready_time = champsim::chrono::clock::time_point::max();

      ++progress;
    }
  }

  return progress;
}

void DRAM_CHANNEL::initialize() {}
void DRAM_CHANNEL::begin_phase() {}

void DRAM_CHANNEL::end_phase(unsigned /*cpu*/) { roi_stats = sim_stats; }

void DRAM_CHANNEL::check_write_collision()
{
  for (auto wq_it = std::begin(WQ); wq_it != std::end(WQ); ++wq_it) {
    if (wq_it->has_value() && !wq_it->value().forward_checked) {
      auto checker = [check_val = champsim::block_number{wq_it->value().address}](const auto& pkt) {
        return pkt.has_value() && champsim::block_number{pkt->address} == check_val;
      };

      auto found = std::find_if(std::begin(WQ), wq_it, checker); // Forward check
      if (found == wq_it) {
        found = std::find_if(std::next(wq_it), std::end(WQ), checker); // Backward check
      }

      if (found != std::end(WQ)) {
        wq_it->reset();
      } else {
        wq_it->value().forward_checked = true;
      }
    }
  }
}

void DRAM_CHANNEL::check_read_collision()
{
  for (auto rq_it = std::begin(RQ); rq_it != std::end(RQ); ++rq_it) {
    if (rq_it->has_value() && !rq_it->value().forward_checked) {
      auto checker = [check_val = champsim::block_number{rq_it->value().address}](const auto& x) {
        return x.has_value() && champsim::block_number{x->address} == check_val;
      };
      if (auto wq_it = std::find_if(std::begin(WQ), std::end(WQ), checker); wq_it != std::end(WQ)) {
        response_type response{rq_it->value().address, rq_it->value().v_address, wq_it->value().data, rq_it->value().pf_metadata,
                               rq_it->value().instr_depend_on_me};
        for (auto* ret : rq_it->value().to_return) {
          ret->push_back(response);
        }

        rq_it->reset();
      } else if (auto found = std::find_if(std::begin(RQ), rq_it, checker); found != rq_it) {
        auto instr_copy = std::move(found->value().instr_depend_on_me);
        auto ret_copy = std::move(found->value().to_return);

        std::set_union(std::begin(instr_copy), std::end(instr_copy), std::begin(rq_it->value().instr_depend_on_me), std::end(rq_it->value().instr_depend_on_me),
                       std::back_inserter(found->value().instr_depend_on_me));
        std::set_union(std::begin(ret_copy), std::end(ret_copy), std::begin(rq_it->value().to_return), std::end(rq_it->value().to_return),
                       std::back_inserter(found->value().to_return));

        rq_it->reset();
      } else if (found = std::find_if(std::next(rq_it), std::end(RQ), checker); found != std::end(RQ)) {
        auto instr_copy = std::move(found->value().instr_depend_on_me);
        auto ret_copy = std::move(found->value().to_return);

        std::set_union(std::begin(instr_copy), std::end(instr_copy), std::begin(rq_it->value().instr_depend_on_me), std::end(rq_it->value().instr_depend_on_me),
                       std::back_inserter(found->value().instr_depend_on_me));
        std::set_union(std::begin(ret_copy), std::end(ret_copy), std::begin(rq_it->value().to_return), std::end(rq_it->value().to_return),
                       std::back_inserter(found->value().to_return));

        rq_it->reset();
      } else {
        rq_it->value().forward_checked = true;
      }
    }
  }
}

DRAM_CHANNEL::request_type::request_type(const typename champsim::channel::request_type& req)
    : pf_metadata(req.pf_metadata), address(req.address), v_address(req.address), data(req.data), instr_depend_on_me(req.instr_depend_on_me)
{
  asid[0] = req.asid[0];
  asid[1] = req.asid[1];
}

unsigned long DRAM_CHANNEL::get_bank(champsim::address address) const 
{
  return std::get<SLICER_BANK_IDX>(address_slicer(address)).to<unsigned long>(); 
}

unsigned long DRAM_CHANNEL::get_column(champsim::address address) const 
{ 
  return std::get<SLICER_COLUMN_IDX>(address_slicer(address)).to<unsigned long>(); 
}

unsigned long DRAM_CHANNEL::get_rank(champsim::address address) const 
{
  return std::get<SLICER_RANK_IDX>(address_slicer(address)).to<unsigned long>();
}

unsigned long DRAM_CHANNEL::get_row(champsim::address address) const 
{
  return std::get<SLICER_ROW_IDX>(address_slicer(address)).to<unsigned long>();
}

champsim::data::bytes DRAM_CHANNEL::size() const { return champsim::data::bytes{BLOCK_SIZE + (1 << address_slicer.bit_size())}; }

std::size_t DRAM_CHANNEL::rows() const { return std::size_t{1} << champsim::size(get<SLICER_ROW_IDX>(address_slicer)); }
std::size_t DRAM_CHANNEL::columns() const { return std::size_t{1} << champsim::size(get<SLICER_COLUMN_IDX>(address_slicer)); }
std::size_t DRAM_CHANNEL::ranks() const { return std::size_t{1} << champsim::size(get<SLICER_RANK_IDX>(address_slicer)); }
std::size_t DRAM_CHANNEL::banks() const { return std::size_t{1} << champsim::size(get<SLICER_BANK_IDX>(address_slicer)); }
std::size_t DRAM_CHANNEL::bank_request_capacity() const { return std::size(bank_request); }

void DRAM_CHANNEL::print_deadlock()
{
  std::string_view q_writer{"instr_id: {} address: {:#x} v_addr: {:#x} type: {} translated: {}"};
  auto q_entry_pack = [](const auto& entry) {
    return std::tuple{entry->address, entry->v_address};
  };

  champsim::range_print_deadlock(RQ, "RQ", q_writer, q_entry_pack);
  champsim::range_print_deadlock(WQ, "WQ", q_writer, q_entry_pack);
}

void MEMORY_CONTROLLER::initialize()
{
}

void MEMORY_CONTROLLER::begin_phase()
{

}

void MEMORY_CONTROLLER::end_phase(unsigned cpu)
{
  //finalize ramulator (if not warmup)
  if(!warmup)
  {
    ramulator2_frontend->finalize();
    ramulator2_memorysystem->finalize();
  }

  //grab stats from each channel for ramulator
  for(size_t i = 0; i < channels.size(); i++)
  {
    channels[i].sim_stats.dbus_cycle_congested = (long)Ramulator::get_ramulator_stat("DBUS_CYCLE_CONGESTED",i);
    channels[i].sim_stats.dbus_count_congested = (uint64_t)Ramulator::get_ramulator_stat("DBUS_COUNT_CONGESTED",i);
    channels[i].sim_stats.refresh_cycles       = (uint64_t)Ramulator::get_ramulator_stat("REFRESH_CYCLES",i);
    channels[i].sim_stats.WQ_ROW_BUFFER_HIT    = (unsigned)Ramulator::get_ramulator_stat("WQ_ROW_BUFFER_HIT",i);
    channels[i].sim_stats.WQ_ROW_BUFFER_MISS   = (unsigned)Ramulator::get_ramulator_stat("WQ_ROW_BUFFER_MISS",i);
    channels[i].sim_stats.RQ_ROW_BUFFER_HIT    = (unsigned)Ramulator::get_ramulator_stat("RQ_ROW_BUFFER_HIT",i);
    channels[i].sim_stats.RQ_ROW_BUFFER_MISS   = (unsigned)Ramulator::get_ramulator_stat("RQ_ROW_BUFFER_MISS",i);
  }

  //end phase for channels (update stats)
  for (auto& chan : channels) {
    chan.end_phase(cpu);
  }

}

void MEMORY_CONTROLLER::initiate_requests()
{
  // Initiate read requests
  for (auto* ul : queues) {
    for (auto q : {std::ref(ul->RQ), std::ref(ul->PQ)}) {
      auto [begin, end] = champsim::get_span_p(std::cbegin(q.get()), std::cend(q.get()), [ul, this](const auto& pkt) { return this->add_rq(pkt, ul); });
      q.get().erase(begin, end);
    }

    // Initiate write requests
    auto [wq_begin, wq_end] = champsim::get_span_p(std::cbegin(ul->WQ), std::cend(ul->WQ), [this](const auto& pkt) { return this->add_wq(pkt); });
    ul->WQ.erase(wq_begin, wq_end);
  }
}

void MEMORY_CONTROLLER::return_packet_rq_rr(Ramulator::Request& req, DRAM_CHANNEL::request_type pkt)
{
  response_type response{pkt.address, pkt.v_address, pkt.data,
                        pkt.pf_metadata, pkt.instr_depend_on_me};

  for (auto* ret : pkt.to_return) {
    ret->push_back(response);
  }
  return;
};


bool MEMORY_CONTROLLER::add_rq(const request_type& packet, champsim::channel* ul)
{
    //if packet needs response, we need to track its data to return later
    if(!warmup)
    {
      //if not warmup
      bool success;
      if(packet.response_requested)
      {
        DRAM_CHANNEL::request_type pkt = DRAM_CHANNEL::request_type{packet};
        pkt.to_return = {&ul->returned};
        success = ramulator2_frontend->receive_external_requests(int(Ramulator::Request::Type::Read), packet.address.to<int64_t>(), packet.type == access_type::PREFETCH ? 1 : 0, [=](Ramulator::Request& req) {return_packet_rq_rr(req,pkt);});
      }
      else
      {
        //otherwise feed to ramulator directly with no response requested
        success = ramulator2_frontend->receive_external_requests(int(Ramulator::Request::Type::Read), packet.address.to<int64_t>(), packet.type == access_type::PREFETCH ? 1 : 0,[this](Ramulator::Request& req){});
      }
      return(success);
    }
    else
    {
      //if warmup, just return true and send necessary responses
      if(packet.response_requested)
      {
          response_type response{packet.address, packet.v_address, packet.data,
                                packet.pf_metadata, packet.instr_depend_on_me};
          for (auto* ret : {&ul->returned}) {
            ret->push_back(response);
          }
      }
      return true;
    }
}

bool MEMORY_CONTROLLER::add_wq(const request_type& packet)
{
    //if ramulator, feed directly. Since its a write, no response is needed
    if(!warmup)
    {
      bool success = ramulator2_frontend->receive_external_requests(Ramulator::Request::Type::Write, packet.address.to<int64_t>(), 0, [](Ramulator::Request& req){});
      if(!success)
        for(size_t i = 0; i < channels.size(); i++)
        ++channels[i].sim_stats.WQ_FULL;
    }
    return true;
}

/*
 * | row address | rank index | column address | bank index | channel | block
 * offset |
 */

//These are all inaccurate and will need to be updated when using Ramulator. We can grab some of these values from the config
//others are part of spec that aren't as easily obtained

unsigned long MEMORY_CONTROLLER::dram_get_channel(champsim::address address) const {
  assert(false);
  return(0);
}

unsigned long MEMORY_CONTROLLER::dram_get_bank(champsim::address address) const { 
  assert(false);
  return(0);
}

unsigned long MEMORY_CONTROLLER::dram_get_column(champsim::address address) const {
  assert(false);
  return(0);
}

unsigned long MEMORY_CONTROLLER::dram_get_rank(champsim::address address) const {
  assert(false);
  return(0);
}

unsigned long MEMORY_CONTROLLER::dram_get_row(champsim::address address) const { 
  assert(false);
  return(0);
}

champsim::data::bytes MEMORY_CONTROLLER::size() const
{
  double dram_size = 0;

  for(int i = 0; i < channels.size(); i++)
  dram_size += Ramulator::get_ramulator_stat("SIZE",i);

  return(champsim::data::bytes(dram_size));
}
// LCOV_EXCL_START Exclude the following function from LCOV
void MEMORY_CONTROLLER::print_deadlock()
{
}
// LCOV_EXCL_STOP
