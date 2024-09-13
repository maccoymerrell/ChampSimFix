#ifndef __HAMMERCOUNTER_H
#define __HAMMERCOUNTER_H
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <math.h>
#include <random>
#include <string>
#include <vector>

#include <bits/stdc++.h>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"



#define RH_READ 0
#define RH_WRITE 1
#define RH_REFRESH 2
class Address
{
public:
  // channel
  // bank
  // rank
  // row
  std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> addr;

  Address(uint64_t channel, uint64_t bank, uint64_t rank, uint64_t row) { addr = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>(channel, bank, rank, row); }

  uint64_t get_row() const { return (std::get<3>(addr)); }

  uint64_t get_rank() const { return (std::get<2>(addr)); }

  uint64_t get_bank() const { return (std::get<1>(addr)); }

  uint64_t get_channel() const { return (std::get<0>(addr)); }

  bool operator<(const Address& a) const { return (addr < a.addr); }
};
struct HighestCount
{
  uint64_t hammer_count;
  uint64_t prefetch_hammer_count;
  uint64_t write_back_count;
  uint64_t normal_hammer_count;
  bool truncated_by_refresh;
  uint64_t start_cycle;
  bool hammered_besides_refresh;
  HighestCount();
};
struct LifetimeCount
{
  uint64_t lost_hammers_to_refresh;
  uint64_t lost_hammers_to_access;

  uint64_t total_hammers;
  uint64_t normal_hammers;
  uint64_t prefetch_hammers;
  uint64_t write_back_hammers;

  bool is_refresh_only;
  LifetimeCount();
};
struct Count {
  LifetimeCount lifetime;
  HighestCount highest;

  Count();
  bool operator<(Count& b);

  void CopyInto(Count C);
  void Combine(Count C);
};

class HammerCounter
{
  uint64_t dram_rows;
  uint64_t dram_ranks;
  uint64_t dram_banks;
  uint64_t dram_columns;
  uint64_t dram_channels;
  uint64_t dram_cap;

  uint64_t rows_per_refresh;


  std::map<Address, Count> row_open_master;
  std::map<Address, Count> row_open_counter;
  std::map<uint64_t, uint64_t> read_access_histogram;
  std::map<uint64_t, uint64_t> write_access_histogram;
  std::map<uint64_t, uint64_t> pref_access_histogram;
  std::map<uint64_t, uint64_t> wb_access_histogram;

  static std::string output_f;

  static uint64_t target_row;
  static uint64_t target_cycle;

  uint64_t phase;

  // cycle values
  uint64_t highest_hammers_per_cycle;
  uint64_t highest_hammers_per_cycle_p;
  uint64_t highest_hammers_per_cycle_wb;
  uint64_t highest_hammer_row;
  uint64_t last_hammer_cycles;
  

  // cumulative stats
  uint64_t row_charges_r;
  uint64_t row_charges_rp;
  uint64_t row_charges_rn;
  uint64_t row_charges_w;
  uint64_t row_charges_wb;
  uint64_t row_charges_wp;
  uint64_t row_charges_wn;
  uint64_t refreshes;
  uint64_t refresh_row;
  uint64_t refresh_cycles;
  uint64_t row_charges_ref;

  uint64_t lost_hammers;
  uint64_t lost_hammers_to_access;
  uint64_t lost_hammers_to_refresh;
  uint64_t victim_reads;
  uint64_t victim_writes;

  

  // utility funcs
  void flush();
  void InsertIntoMap(Address A, Count C);

public:
  static uint64_t processed_packets;
  // calculated values
  uint64_t cycles_per_bin;
uint64_t cycles_per_heartbeat;
uint64_t channel_num;
  uint64_t total_cycles;
  HammerCounter();
  //~HammerCounter();
  static void set_output_file(std::string f);
  static std::string get_output_file();
  void log_charge(Address addr,uint64_t p_addr, uint64_t v_addr, int type, bool prefetch,uint64_t cycle, bool write_back);
  void log_refresh(Address addr);
  void log_write(Address addr);
  void log_cycle();
  //void log_refresh(uint64_t row, uint64_t cycle);
  static void set_target_row(uint64_t row);
  bool is_start_cycle(uint64_t cycle);

  void set_cycles_per_bin(uint64_t c_p_b)   {cycles_per_bin = c_p_b;}; // 500us heartbeat
  void set_heartbeat_rate(uint64_t hbr)     {cycles_per_heartbeat = hbr;};
  void set_rows_per_refresh(uint64_t rpr)   {rows_per_refresh = rpr;};
  void set_dram_rows(uint64_t row_count)    {dram_rows = row_count;};
  void set_dram_ranks(uint64_t rank_count)  {dram_ranks = rank_count;};
  void set_dram_banks(uint64_t bank_count)  {dram_banks = bank_count;};
  void set_dram_columns(uint64_t column_count) {dram_columns = column_count;};
  void set_dram_channels(uint64_t channel_count) {dram_channels = channel_count;};
  void set_dram_cap(uint64_t dram_count) {dram_cap = dram_count;};
  static uint64_t get_target_row();
  static void set_target_cycle(uint64_t cycle);
  void print_file();
};


namespace Ramulator
{
class HammerCounterPlugin : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, HammerCounterPlugin, "HammerCounter", "Counts Hammer Activity for Research.")

  uint64_t last_bank_util = 0;
  double rb_miss = 0;
  double rb_hits = 0;
  double last_rb_miss = 0;
  double last_rb_hits = 0;
  double histogram_period;
  double refresh_period = 0;
  double tCK;
  uint64_t processed_packets = 0;
  uint64_t cycles_per_heartbeat;

  uint64_t channel_num = 0;

  private:
    IDRAM* m_dram = nullptr;
    IDRAMController* m_controller = nullptr;
    IMemorySystem* m_system = nullptr;
    HammerCounter HC;


  public:
    void init() override
    {
        std::string output_file = param<std::string>("output_file").desc("Name of output file").required();
        uint64_t cycles_per_heartbeat = param<uint64_t>("cycles_per_heartbeat").desc("Rate at which DRAM heartbeat is printed").required();
        histogram_period = param<double>("histogram_period").desc("Bin size for histograms").required();

        uint64_t target_row = param<uint64_t>("target_row").desc("Row that will be memory dumped");
        uint64_t target_cycle = param<uint64_t>("target_cycle").desc("Time at which hammer logs will start").required();
        refresh_period      = param<double>("refresh_period").desc("Refresh rate of DRAM").required();
        HC.set_output_file(output_file);
        HC.set_heartbeat_rate(cycles_per_heartbeat);
        HC.set_target_row(target_row);
        HC.set_target_cycle(target_cycle);

        

    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;
      m_controller = m_ctrl;
      m_system = memory_system;

      HC.set_dram_channels(m_dram->get_level_size("channel"));
      HC.set_dram_ranks(m_dram->get_level_size("rank"));
      HC.set_dram_banks(m_dram->get_level_size("bank") * m_dram->get_level_size("bankgroup"));
      HC.set_dram_columns(m_dram->get_level_size("column"));
      HC.set_dram_rows(m_dram->get_level_size("row"));

      tCK = m_dram->m_timing_vals("tCK_ps") * 1e-12;

      HC.set_cycles_per_bin(uint64_t(histogram_period/tCK));
      double tREFI = m_dram->m_timing_vals("nREFI") * (tCK);
      double rows_per_refresh = m_dram->get_level_size("row") / (refresh_period/tREFI);


      HC.set_rows_per_refresh(ceil(rows_per_refresh));

      register_stat(rb_miss).name("total_rowbuffer_misses");
      register_stat(rb_hits).name("total_rowbuffer_hits");
      register_stat(last_bank_util).name("total_bytes_processed");
    };

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
      if (request_found) {
        //grab the channel number. This is our channel
        channel_num = req_it->addr_vec[m_dram->m_levels("channel")];

        if(m_dram->m_command_meta(req_it->command).is_accessing)
        {
          rb_hits++;
          processed_packets += 1;
        }
        if(m_dram->m_command_meta(req_it->command).is_opening && m_dram->m_command_scopes(req_it->command) == m_dram->m_levels("row")) //opened row
        {
          rb_hits--;
          rb_miss++;
          uint64_t bank_count = m_dram->get_level_size("bank");
          int type = req_it->type_id == Ramulator::Request::Type::Write ? RH_WRITE : RH_READ;
          HC.log_charge(Address(req_it->addr_vec[m_dram->m_levels("channel")], req_it->addr_vec[m_dram->m_levels("bank")] + bank_count*req_it->addr_vec[m_dram->m_levels("bankgroup")], req_it->addr_vec[m_dram->m_levels("rank")],req_it->addr_vec[m_dram->m_levels("row")]),req_it->addr,0,type,req_it->source_id == 1,HC.total_cycles,req_it->type_id == Ramulator::Request::Type::Write);
        }
        else if(m_dram->m_command_meta(req_it->command).is_refreshing) //refreshed
        {
          uint64_t bank_count = m_dram->get_level_size("bank");
          HC.log_refresh(Address(req_it->addr_vec[m_dram->m_levels("channel")], req_it->addr_vec[m_dram->m_levels("bank")] + bank_count*req_it->addr_vec[m_dram->m_levels("bankgroup")], req_it->addr_vec[m_dram->m_levels("rank")],req_it->addr_vec[m_dram->m_levels("row")]));
        }
      }
      HC.log_cycle();
      HC.is_start_cycle(HC.total_cycles);

    if (HC.total_cycles % HC.cycles_per_heartbeat == 0) {
    // print heartbeat
    uint64_t bank_util = processed_packets * m_dram->m_internal_prefetch_size * 8;
    double cum_hit_rate = ((rb_hits) / double(rb_hits + rb_miss));
    double hit_rate = ((rb_hits - last_rb_hits) / double((rb_hits-last_rb_hits + rb_miss - last_rb_miss)));

    double throughput = (((bank_util - last_bank_util)/double(HC.cycles_per_heartbeat)) * (1.0/tCK)) / double(1<<30);
    double cum_throughput = ((bank_util/double(HC.total_cycles)) * (1.0/tCK)) / double(1<<30);
    printf("Heartbeat DRAM %lu : Throughput: %.3fGiB/s Cumulative Throughput: %.3fGiB/s Row Buffer Hit Rate: %.3f Cumulative Row Buffer Hit Rate: %.3f\n",channel_num, throughput,cum_throughput,hit_rate,cum_hit_rate);
    last_bank_util = bank_util;
    last_rb_hits = rb_hits;
    last_rb_miss = rb_miss;
    }
    };

    void finalize() override {
      HC.print_file();
    }

};
}

std::string HammerCounter::output_f = "hammer";
uint64_t HammerCounter::processed_packets = 0;
uint64_t HammerCounter::target_row = 0;
uint64_t HammerCounter::target_cycle = 0;

std::ofstream memory_log_dump;

std::string HammerCounter::get_output_file() { return (output_f); }

uint64_t HammerCounter::get_target_row() { return (target_row); }

void HammerCounter::set_target_row(uint64_t row) { target_row = row; }

void HammerCounter::set_target_cycle(uint64_t cycle) { target_cycle = cycle; }

HighestCount::HighestCount()
{
  hammer_count = 0;
  prefetch_hammer_count = 0;
  normal_hammer_count = 0;
  truncated_by_refresh = false;
  hammered_besides_refresh = false;
  start_cycle = 0;
  write_back_count = 0;
}
LifetimeCount::LifetimeCount()
{
  total_hammers = 0;
  normal_hammers = 0;
  prefetch_hammers = 0;
  is_refresh_only = true;
  lost_hammers_to_access = 0;
  lost_hammers_to_refresh = 0;
  write_back_hammers = 0;
}
void Count::CopyInto(Count C)
{
  //replace if new highest hammer count
  if(C.highest.hammer_count > highest.hammer_count)
  {
    highest = C.highest;
  }
  //global stats
  Combine(C);
}
void Count::Combine(Count C)
{
  lifetime.total_hammers += C.highest.hammer_count;
  lifetime.normal_hammers += C.highest.normal_hammer_count;
  lifetime.prefetch_hammers += C.highest.prefetch_hammer_count;
  lifetime.write_back_hammers += C.highest.write_back_count;
  lifetime.is_refresh_only = lifetime.is_refresh_only && !C.highest.hammered_besides_refresh;
  if(C.highest.truncated_by_refresh)
    lifetime.lost_hammers_to_refresh += C.highest.hammer_count;
  else
    lifetime.lost_hammers_to_access += C.highest.hammer_count;
}
void HammerCounter::InsertIntoMap(Address A, Count C)
{
  if (row_open_master.find(A) != row_open_master.end()) {
    //entry already exists, copy into
    row_open_master[A].CopyInto(C);
  } else {
    //need to create a new entry in the master table
    row_open_master[A] = Count();
    row_open_master[A].CopyInto(C);
  }
}

Count::Count()
{
  lifetime = LifetimeCount();
  highest = HighestCount();
}
bool Count::operator<(Count& b) { return (highest.hammer_count < b.highest.hammer_count); }

HammerCounter::HammerCounter()
{
  dram_rows = 0;
  dram_columns = 0;
  dram_ranks = 0;
  dram_banks = 0;
  dram_channels = 0;

  refresh_row = 0;
  row_charges_r = 0;
  row_charges_rn = 0;
  row_charges_rp = 0;
  row_charges_w = 0;
  row_charges_wn = 0;
  row_charges_wp = 0;
  refreshes = 0;
  refresh_cycles = 0;
  total_cycles = 0;
  row_charges_ref = 0;

  lost_hammers = 0;
  lost_hammers_to_access = 0;
  lost_hammers_to_refresh = 0;
  victim_reads = 0;
  victim_writes = 0;

  highest_hammers_per_cycle = 0;
  highest_hammers_per_cycle_p = 0;
  highest_hammer_row = 0;
  last_hammer_cycles = 0;

  channel_num = 0;
  phase = 0;

  cycles_per_bin = 100e6; // 100 us bins

  cycles_per_heartbeat = 100e4;
}

uint64_t previous_row = 0;
uint64_t previous_addr = 0;
uint64_t previous_vaddr = 0;
uint64_t previous_cycle = 0;
uint64_t previous_bank = 0;
int previous_type = 0;
bool previous_pref = false;
bool previous_wb = false;

void HammerCounter::log_refresh(Address addr)
{
  //do refresh based off of first rank
  if(addr.get_rank() == 0)
  {
    for(int k = 0; k < (dram_ranks); k++)
    {
      for(int i = 0; i < rows_per_refresh; i++)
      {
        for(int j = 0; j < dram_banks; j++)
        {
          Address addr2(addr.get_channel(),j,k,refresh_row+i);
          log_charge(addr2,0,0,RH_REFRESH,false,0,false);
        }
      }
    }
    refresh_row += rows_per_refresh;
    if(refresh_row >= dram_rows)
    {
      refresh_cycles++;
      refresh_row = 0;
    }
  }
  refreshes++;
    
}
void HammerCounter::log_charge(Address addr,uint64_t p_addr, uint64_t v_addr, int type, bool prefetch, uint64_t cycle, bool write_back)
{
  channel_num = addr.get_channel();
  // log hit on both adjacent rows
  //std::cout << addr.get_row() << "\n";
  Address addr_high = Address(addr.get_channel(), addr.get_bank(), addr.get_rank(), (addr.get_row() + 1) % dram_rows);
  Address addr_low = Address(addr.get_channel(), addr.get_bank(), addr.get_rank(), (addr.get_row() == 0) ? dram_rows - 1 : addr.get_row() - 1);

  if(addr.get_row() == target_row || addr_high.get_row() == target_row || addr_low.get_row() == target_row)
  {
    //print to mem file
    memory_log_dump.open(output_f + "_memory_accesses.log",std::fstream::app);
          if(previous_pref)
            memory_log_dump << "PPREFETCH ";
          else if(previous_wb)
            memory_log_dump << "PWRITEBACK ";
          else
            memory_log_dump << "PNORMAL ";
          memory_log_dump << "PADDR: " << std::hex << previous_addr << 
                             " VADDR: " << std::hex << previous_vaddr << 
                             " PROW: " << previous_row << 
                             " PBANK: " << previous_bank <<
                             " CYCLE: " << std::dec << previous_cycle << std::endl;
          if(prefetch)
            memory_log_dump << "PREFETCH ";
          else if(write_back)
            memory_log_dump << "WRITEBACK ";
          else
            memory_log_dump << "NORMAL ";
          memory_log_dump << "PADDR: " << std::hex << p_addr << 
                             " VADDR: " << v_addr << 
                             " PROW: " << addr.get_row() <<
                             " PBANK: " << addr.get_bank() << 
                             " CYCLE: " << std::dec << total_cycles << std::endl;
    memory_log_dump.close();

  }
  previous_row = addr.get_row();
  previous_addr = p_addr;
  previous_vaddr = v_addr;
  previous_bank = addr.get_bank();
  previous_cycle = total_cycles;
  previous_type = type;
  previous_pref = prefetch;
  previous_wb = write_back;


  // low row
  if (addr.get_row() != 0) {
    if (row_open_counter.find(addr_low) == row_open_counter.end())
    {
      row_open_counter[addr_low] = Count();
      row_open_counter[addr_low].highest.start_cycle = cycle;
    }
    row_open_counter[addr_low].highest.hammer_count++;

    if(type != RH_REFRESH)
    row_open_counter[addr_low].highest.hammered_besides_refresh = true;

    //writeback, prefetch, or normal
    if (prefetch)
      row_open_counter[addr_low].highest.prefetch_hammer_count++;
    else if(write_back)
      row_open_counter[addr_low].highest.write_back_count++;
    else
      row_open_counter[addr_low].highest.normal_hammer_count++;

    if (row_open_counter[addr_low].highest.hammer_count > highest_hammers_per_cycle) {
      highest_hammers_per_cycle = row_open_counter[addr_low].highest.hammer_count;
      highest_hammers_per_cycle_p = row_open_counter[addr_low].highest.prefetch_hammer_count;
      highest_hammers_per_cycle_wb = row_open_counter[addr_low].highest.write_back_count;
      highest_hammer_row = addr_low.get_row();
    }

    if (type == RH_READ) {
      row_charges_r++;
      if (prefetch)
        row_charges_rp++;
      else
        row_charges_rn++;
    } else if(type == RH_WRITE) {
      row_charges_w++;
      if (prefetch)
        row_charges_wp++;
      else if(write_back)
        row_charges_wb++;
      else
        row_charges_wn++;
    }
    else
    {
      row_charges_ref++;
    }

    // for output histogram
    if (type == RH_READ && !prefetch) {
      if (read_access_histogram.find(total_cycles / cycles_per_bin) == read_access_histogram.end())
        read_access_histogram[total_cycles / cycles_per_bin] = 1;
      else
        read_access_histogram[total_cycles / cycles_per_bin]++;
    }
    if (type == RH_READ && prefetch)
    {
      if (pref_access_histogram.find(total_cycles / cycles_per_bin) == pref_access_histogram.end())
        pref_access_histogram[total_cycles / cycles_per_bin] = 1;
      else
        pref_access_histogram[total_cycles / cycles_per_bin]++;
    } 
    if (type == RH_WRITE && write_back){
      if (wb_access_histogram.find(total_cycles / cycles_per_bin) == wb_access_histogram.end())
        wb_access_histogram[total_cycles / cycles_per_bin] = 1;
      else
        wb_access_histogram[total_cycles / cycles_per_bin]++;
    }
    if (type == RH_WRITE && !write_back)
    {
      if (write_access_histogram.find(total_cycles / cycles_per_bin) == write_access_histogram.end())
        write_access_histogram[total_cycles / cycles_per_bin] = 1;
      else
        write_access_histogram[total_cycles / cycles_per_bin]++;
    }
  }

  // high row
  if (addr.get_row() != (dram_rows - 1)) {
    if (row_open_counter.find(addr_high) == row_open_counter.end())
    {
      row_open_counter[addr_high] = Count();
      row_open_counter[addr_high].highest.start_cycle = cycle;
    }
    row_open_counter[addr_high].highest.hammer_count++;

    if(type != RH_REFRESH)
    row_open_counter[addr_high].highest.hammered_besides_refresh = true;

    if (prefetch)
      row_open_counter[addr_high].highest.prefetch_hammer_count++;
    else if (write_back)
      row_open_counter[addr_high].highest.write_back_count++;
    else
      row_open_counter[addr_high].highest.normal_hammer_count++;

    if (row_open_counter[addr_high].highest.hammer_count > highest_hammers_per_cycle) {
      highest_hammers_per_cycle = row_open_counter[addr_high].highest.hammer_count;
      highest_hammers_per_cycle_p = row_open_counter[addr_high].highest.prefetch_hammer_count;
      highest_hammers_per_cycle_wb = row_open_counter[addr_high].highest.write_back_count;
      highest_hammer_row = addr_high.get_row();
    }

    if (type == RH_READ) {
      row_charges_r++;
      if (prefetch)
        row_charges_rp++;
      else
        row_charges_rn++;
    } else if(type == RH_WRITE){
      row_charges_w++;
      if (prefetch)
        row_charges_wp++;
      else if(write_back)
        row_charges_wb++;
      else
        row_charges_wn++;
    }
    else
    {
      row_charges_ref++;
    }

    // for output histogram
    if (type == RH_READ && !prefetch) {
      if (read_access_histogram.find(total_cycles / cycles_per_bin) == read_access_histogram.end())
        read_access_histogram[total_cycles / cycles_per_bin] = 1;
      else
        read_access_histogram[total_cycles / cycles_per_bin]++;
    }
    if (type == RH_READ && prefetch)
    {
      if (pref_access_histogram.find(total_cycles / cycles_per_bin) == pref_access_histogram.end())
        pref_access_histogram[total_cycles / cycles_per_bin] = 1;
      else
        pref_access_histogram[total_cycles / cycles_per_bin]++;
    } 
    if (type == RH_WRITE && write_back){
      if (wb_access_histogram.find(total_cycles / cycles_per_bin) == wb_access_histogram.end())
        wb_access_histogram[total_cycles / cycles_per_bin] = 1;
      else
        wb_access_histogram[total_cycles / cycles_per_bin]++;
    }
    if (type == RH_WRITE && !write_back)
    {
      if (write_access_histogram.find(total_cycles / cycles_per_bin) == write_access_histogram.end())
        write_access_histogram[total_cycles / cycles_per_bin] = 1;
      else
        write_access_histogram[total_cycles / cycles_per_bin]++;
    }
  }

  // the secondary impact of this access is that the current hammers on the row_addr are truncated here
  if (row_open_counter.find(addr) != row_open_counter.end()) {
    row_open_counter[addr].highest.truncated_by_refresh = (type == RH_REFRESH);
    InsertIntoMap(addr, row_open_counter[addr]);

    // increment lost hammers
    lost_hammers += row_open_counter[addr].highest.hammer_count;
    if(type == RH_REFRESH)
      lost_hammers_to_refresh += row_open_counter[addr].highest.hammer_count;
    else
      lost_hammers_to_access += row_open_counter[addr].highest.hammer_count;
    row_open_counter.erase(addr);

    // did we read from a victim or write to a victim?
    if (type == RH_READ)
      victim_reads++;
    else if(type == RH_WRITE)
      victim_writes++;
  }
}

bool HammerCounter::is_start_cycle(uint64_t cycle)
{
  bool start = (cycle == target_cycle);
  if(start)
  {
    row_open_master.clear();
    row_open_counter.clear();
  }
  return(start);
}

void HammerCounter::log_cycle()
{
  total_cycles++;

  if (total_cycles % cycles_per_heartbeat == 0) {
    // print heartbeat
    std::cout << "Heartbeat HAMMER COUNTER " << channel_num << " : " << (unsigned long)(total_cycles) << " Highest Hammer Row: " << std::hex
              << highest_hammer_row << std::dec << " Hammer Count: " << highest_hammers_per_cycle << " (" << highest_hammers_per_cycle_p
              << ") Refresh Row: " << std::hex << refresh_row << std::dec << " Heartbeat Hammers: " << ((row_charges_r + row_charges_w) - last_hammer_cycles)
              << "\n";
    highest_hammers_per_cycle = 0;
    highest_hammers_per_cycle_p = 0;
    last_hammer_cycles = row_charges_r + row_charges_w;
  }

  //clear for synchronicity
  if(total_cycles == target_cycle)
  {
    row_open_counter.clear();
    row_open_master.clear();
  }
}

void HammerCounter::flush()
{
  for (auto it = row_open_counter.begin(); it != row_open_counter.end(); it++) {
    InsertIntoMap(it->first, it->second);
  }
  row_open_counter.clear();
}

bool sortbysec(const std::pair<Address, Count>& a, const std::pair<Address, Count>& b)
{
  return (a.second.highest.hammer_count > b.second.highest.hammer_count);
}

void HammerCounter::set_output_file(std::string f) { output_f = f; }

void HammerCounter::print_file()
{
  std::string file_name;
  file_name = output_f + "_" + std::to_string(channel_num) + "_" + std::to_string(phase);
  flush();
  // calculate what percentage of each address space was used
  uint64_t unique_rows_visited = 0;
  for(auto it = row_open_master.begin(); it != row_open_master.end(); it++)
  {
    if(!it->second.lifetime.is_refresh_only)
    unique_rows_visited++;
  }
  long double address_space_usage = (unique_rows_visited) / (double)(dram_ranks * dram_banks * dram_rows * dram_channels);
  std::ofstream file;
  file.open(file_name + ".log");
  file << "ROW-HAMMER STATISTICS\n";
  file << "####################################################################################################\n";
  file << "Row Hammers (READ INSTIGATED): " << row_charges_r << "\n";
  file << "\tNormal: " << row_charges_rn << " \tPrefetch: " << row_charges_rp << "\n";
  file << "Row Hammers (WRITE INSTIGATED): " << row_charges_w << "\n";
  file << "\tNormal: " << row_charges_wn << " \tPrefetch: " << row_charges_wp << "\tWriteback: " << row_charges_wb << "\n";
  file << "Row Hammers (REFRESH INSTIGATED): " << row_charges_ref << "\n";
  file << "Total Row Hammers: " << row_charges_r + row_charges_w + row_charges_ref<< "\n";
  file << "####################################################################################################\n";
  file << "Channels: " << dram_channels << "\n";
  file << "Ranks: " << dram_ranks << "\n";
  file << "Banks: " << dram_banks << "\n";
  file << "Rows: " << dram_rows << "\n";
  file << "Columns: " << dram_columns << "\n";
  file << "Address Space Used: " << address_space_usage * 100.0 << "%\n";
  file << "####################################################################################################\n";
  file << "Refreshes: " << refreshes << '\n';
  file << "Rows Per Refresh: " << rows_per_refresh << "\n";
  file << "Refresh Cycles: " << refresh_cycles/dram_banks << '\n';
  file << "####################################################################################################\n";
  file << "Victim Reads: " << victim_reads << "\n";
  file << "Victim Writes: " << victim_writes << "\n";
  file << "Lost Hammers: " << lost_hammers << "\n";
  file << "\tTo Refresh: " << lost_hammers_to_refresh << " \tTo Access: " << lost_hammers_to_access << "\n";
  file << "####################################################################################################\n";
  file << "Stats by Row\n";

  // lets try to sort
  std::vector<std::pair<Address, Count>> final_output;
  for (auto it = row_open_master.begin(); it != row_open_master.end(); it++) {
    final_output.push_back(*(it));
  }
  std::sort(final_output.begin(), final_output.end(), sortbysec);

  // print output
  for (auto it = final_output.begin(); it != final_output.end(); it++) {
    //had to undergo non-refresh activation
    if(!it->second.lifetime.is_refresh_only)
      {
      // Print out data for row
      file << "\tChannel: 0x" << std::hex << it->first.get_channel();
      file << "\tRank: 0x" << std::hex << it->first.get_rank();
      file << "\tBank: 0x" << std::hex << it->first.get_bank();
      file << "\tRow: 0x" << std::hex << it->first.get_row();
      file << "\tLifetime Hammers/(Normal:Prefetch:Writeback): " << std::dec << it->second.lifetime.total_hammers << " (" << it->second.lifetime.normal_hammers << ":" << it->second.lifetime.prefetch_hammers << ":" << it->second.lifetime.write_back_hammers << ") ";
      if (it->second.highest.truncated_by_refresh)
        file << "\tLimit: Refresh ";
      else
        file << "\tLimit: Access ";
      file << "\tHighest Single-Cycle Hammers(" << std::dec << it->second.highest.start_cycle << ")/(Normal:Prefetch:Writeback): " << std::dec << it->second.highest.hammer_count << " (" << it->second.highest.normal_hammer_count << ":" << it->second.highest.prefetch_hammer_count
          << ":" << it->second.highest.write_back_count << ") ";
      file << "\tLost Hammers/(Refresh:Access): " << std::dec << it->second.lifetime.lost_hammers_to_refresh + it->second.lifetime.lost_hammers_to_access << " ("
          << it->second.lifetime.lost_hammers_to_refresh << ":" << it->second.lifetime.lost_hammers_to_access << ") ";
      file << "\n";
    }
  }

  file << "####################################################################################################\n";
  file.close();

  // print histograms now
  std::ofstream file_hr;
  file_hr.open(file_name + ".hr");
  for (auto it = read_access_histogram.begin(); it != read_access_histogram.end(); it++) {
    file_hr << (it->first * 100) << " " << it->second << "\n";
  }
  file_hr.close();
  std::ofstream file_hw;
  file_hw.open(file_name + ".hw");
  for (auto it = write_access_histogram.begin(); it != write_access_histogram.end(); it++) {
    file_hw << (it->first * 100) << " " << it->second << "\n";
  }
  file_hw.close();
  std::ofstream file_hp;
  file_hp.open(file_name + ".hp");
  for (auto it = pref_access_histogram.begin(); it != pref_access_histogram.end(); it++) {
    file_hp << (it->first * 100) << " " << it->second << "\n";
  }
  file_hp.close();
  std::ofstream file_hwb;
  file_hwb.open(file_name + ".hwb");
  for (auto it = wb_access_histogram.begin(); it != wb_access_histogram.end(); it++) {
    file_hwb << (it->first * 100) << " " << it->second << "\n";
  }
  file_hwb.close();

  phase++;
}
#endif