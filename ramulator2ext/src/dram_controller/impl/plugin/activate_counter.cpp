#include "dram_controller/impl/plugin/shared_counter.h"
struct ActivateLifetimeCount
{
  uint64_t total_activations;
  uint64_t read_activations;
  uint64_t prefetch_activations;
  uint64_t writeback_activations;

  bool operator<(ActivateLifetimeCount& b);

  ActivateLifetimeCount();
};

class ActivateCounter
{
  uint64_t dram_rows;
  uint64_t dram_ranks;
  uint64_t dram_banks;
  uint64_t dram_columns;
  uint64_t dram_channels;
  uint64_t dram_cap;


  std::map<Address, ActivateLifetimeCount> activate_master;
  std::map<uint64_t, uint64_t> read_activate_histogram;
  std::map<uint64_t, uint64_t> pref_activate_histogram;
  std::map<uint64_t, uint64_t> wb_activate_histogram;

  static std::string output_f;

  uint64_t cycles_per_heartbeat;

  uint64_t phase;

  // cycle values
  uint64_t highest_activates_per_cycle_read;
  uint64_t highest_activates_per_cycle_prefetch;
  uint64_t highest_activates_per_cycle_writeback;
  uint64_t highest_activates_row;
  uint64_t last_activate_cycles;
  

  // cumulative stats
  uint64_t row_activates_r;
  uint64_t row_activates_rp;
  uint64_t row_activates_rn;
  uint64_t row_activates_wb;
  uint64_t row_activates_wp;
  uint64_t row_activates_wn;

  void perform_histogram(Address addr, bool prefetch, bool write_back);

public:
  static uint64_t processed_packets;
  // calculated values
  uint64_t cycles_per_bin;
  uint64_t channel_num;
  uint64_t total_cycles;
  ActivateCounter();
  static void set_output_file(std::string f);
  static std::string get_output_file();
  void log_charge(Address addr, bool prefetch, bool write_back);
  
  void log_cycle();
  void set_cycles_per_heartbeat(uint64_t c_p_h) {cycles_per_heartbeat = c_p_h;};
  void set_cycles_per_bin(uint64_t c_p_b)   {cycles_per_bin = c_p_b;}; // 500us heartbeat
  void set_dram_rows(uint64_t row_count)    {dram_rows = row_count;};
  void set_dram_ranks(uint64_t rank_count)  {dram_ranks = rank_count;};
  void set_dram_banks(uint64_t bank_count)  {dram_banks = bank_count;};
  void set_dram_columns(uint64_t column_count) {dram_columns = column_count;};
  void set_dram_channels(uint64_t channel_count) {dram_channels = channel_count;};
  void set_dram_cap(uint64_t dram_count) {dram_cap = dram_count;};
  void print_file();
};


namespace Ramulator
{
class ActivateCounterPlugin : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, ActivateCounterPlugin, "ActivateCounter", "Counts Activity for Research.")

  uint64_t last_bank_util = 0;
  uint64_t total_cycles = 0;
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
    IBHDRAMController* m_controller = nullptr;
    IMemorySystem* m_system = nullptr;
    ActivateCounter HC;


  public:
    void init() override
    {
        std::string output_file = param<std::string>("output_file").desc("Name of output file").required();
        cycles_per_heartbeat = param<uint64_t>("cycles_per_heartbeat").desc("Rate at which DRAM heartbeat is printed").required();
        histogram_period = param<double>("histogram_period").desc("Bin size for histograms").required();
        HC.set_output_file(output_file);
        HC.set_cycles_per_heartbeat(cycles_per_heartbeat);
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_controller = cast_parent<IBHDRAMController>();
      m_dram = m_controller->m_dram;
      m_system = memory_system;

      HC.set_dram_channels(m_dram->get_level_size("channel"));
      HC.set_dram_ranks(m_dram->get_level_size("rank"));
      HC.set_dram_banks(m_dram->get_level_size("bank") * m_dram->get_level_size("bankgroup"));
      HC.set_dram_columns(m_dram->get_level_size("column"));
      HC.set_dram_rows(m_dram->get_level_size("row"));
      tCK = m_dram->m_timing_vals("tCK_ps") * 1e-12;

      HC.set_cycles_per_bin(uint64_t(histogram_period/tCK));

      register_stat(rb_miss).name("total_rowbuffer_misses");
      register_stat(rb_hits).name("total_rowbuffer_hits");
      register_stat(last_bank_util).name("total_bytes_processed");
    };

    Address convert_address(ReqBuffer::iterator& req)
    {
      uint64_t bank_count = m_dram->get_level_size("bank");
      return(Address(req->addr_vec[m_dram->m_levels("channel")], req->addr_vec[m_dram->m_levels("bank")] + bank_count*req->addr_vec[m_dram->m_levels("bankgroup")], req->addr_vec[m_dram->m_levels("rank")],req->addr_vec[m_dram->m_levels("row")]));
    }

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
          HC.log_charge(convert_address(req_it),req_it->is_prefetch,req_it->type_id == Ramulator::Request::Type::Write);
        }
      }
      total_cycles++;
      HC.log_cycle();

      if (total_cycles % cycles_per_heartbeat == 0) {
        // print heartbeat
        uint64_t bank_util = processed_packets * m_dram->m_internal_prefetch_size * 8;
        double cum_hit_rate = ((rb_hits) / double(rb_hits + rb_miss));
        double hit_rate = ((rb_hits - last_rb_hits) / double((rb_hits-last_rb_hits + rb_miss - last_rb_miss)));

        double throughput = (((bank_util - last_bank_util)/double(cycles_per_heartbeat)) * (1.0/tCK)) / double(1<<30);
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

std::string ActivateCounter::output_f = "activate_count";
uint64_t ActivateCounter::processed_packets = 0;

std::string ActivateCounter::get_output_file() { return (output_f); }

ActivateLifetimeCount::ActivateLifetimeCount()
{
  total_activations = 0;
  read_activations = 0;
  prefetch_activations = 0;
  writeback_activations = 0;
}

bool ActivateLifetimeCount::operator<(ActivateLifetimeCount& b) { return (total_activations < b.total_activations); }

ActivateCounter::ActivateCounter()
{
  dram_rows = 0;
  dram_columns = 0;
  dram_ranks = 0;
  dram_banks = 0;
  dram_channels = 0;

  row_activates_r = 0;
  row_activates_rn = 0;
  row_activates_rp = 0;
  row_activates_wb = 0;

  total_cycles = 0;

  highest_activates_per_cycle_read = 0;
  highest_activates_per_cycle_prefetch = 0;
  highest_activates_row = 0;
  last_activate_cycles = 0;

  channel_num = 0;
  phase = 0;
  cycles_per_heartbeat = 0;

  cycles_per_bin = 100e6; // 100 us bins
}


void ActivateCounter::perform_histogram(Address addr, bool prefetch, bool write_back)
{
  // for output histogram
  if (!prefetch && !write_back) {
    if (read_activate_histogram.find(total_cycles / cycles_per_bin) == read_activate_histogram.end())
      read_activate_histogram[total_cycles / cycles_per_bin] = 1;
    else
      read_activate_histogram[total_cycles / cycles_per_bin]++;
  }
  if (prefetch)
  {
    if (pref_activate_histogram.find(total_cycles / cycles_per_bin) == pref_activate_histogram.end())
      pref_activate_histogram[total_cycles / cycles_per_bin] = 1;
    else
      pref_activate_histogram[total_cycles / cycles_per_bin]++;
  } 
  if (write_back){
    if (wb_activate_histogram.find(total_cycles / cycles_per_bin) == wb_activate_histogram.end())
      wb_activate_histogram[total_cycles / cycles_per_bin] = 1;
    else
      wb_activate_histogram[total_cycles / cycles_per_bin]++;
  }
}



void ActivateCounter::log_charge(Address addr, bool prefetch, bool write_back)
{
  channel_num = addr.get_channel();
  uint64_t dram_row = addr.get_row();

  //log charge in table
  if(activate_master.find(addr) == std::end(activate_master))
    activate_master[addr] = ActivateLifetimeCount();

  if(prefetch)
    activate_master[addr].prefetch_activations++;
  else if(write_back)
    activate_master[addr].writeback_activations++;
  else
    activate_master[addr].read_activations++;

  activate_master[addr].total_activations++;
  if(activate_master[addr].total_activations > highest_activates_per_cycle_read)
  {
    highest_activates_per_cycle_read = activate_master[addr].read_activations;
    highest_activates_per_cycle_prefetch = activate_master[addr].prefetch_activations;
    highest_activates_per_cycle_writeback = activate_master[addr].writeback_activations;
    highest_activates_row = addr.get_row();
  }

  //log charge for total statas
  if (prefetch) {
    row_activates_rp++;
    row_activates_r++;
  }
  else if(write_back)
    row_activates_wb++;
  else
  {
    row_activates_rn++;
    row_activates_r++;
  }

  //update histogram
  perform_histogram(addr,prefetch,write_back);
 
}

void ActivateCounter::log_cycle()
{
  total_cycles++;

  if (total_cycles % cycles_per_heartbeat == 0) {
    // print heartbeat
    std::cout << "Heartbeat ACTIVATE COUNTER " << channel_num << " : " << (unsigned long)(total_cycles) << " Highest ACT Row: " << std::hex
              << highest_activates_row << std::dec << " ACT Count: " << highest_activates_per_cycle_read << " (" << highest_activates_per_cycle_prefetch
              << ")" << std::dec << " Heartbeat ACTs: " << ((row_activates_r + row_activates_wb) - last_activate_cycles)
              << "\n";
    highest_activates_per_cycle_read = 0;
    highest_activates_per_cycle_prefetch = 0;
    last_activate_cycles = row_activates_r + row_activates_wb;
  }
}

bool sortbysec(const std::pair<Address, ActivateLifetimeCount>& a, const std::pair<Address, ActivateLifetimeCount>& b)
{
  return (a.second.total_activations > b.second.total_activations);
}

void ActivateCounter::set_output_file(std::string f) { output_f = f; }

void ActivateCounter::print_file()
{
  std::string file_name;
  file_name = output_f + "_" + std::to_string(channel_num) + "_" + std::to_string(phase);

  // calculate what percentage of each address space was used
  uint64_t unique_rows_visited = 0;
  for(auto it = activate_master.begin(); it != activate_master.end(); it++)
  {
    unique_rows_visited++;
  }
  long double address_space_usage = (unique_rows_visited) / (double)(dram_ranks * dram_banks * dram_rows * dram_channels);
  std::ofstream file;
  file.open(file_name + ".log");
  file << "ROW-ACT STATISTICS\n";
  file << "####################################################################################################\n";
  file << "Row ACTs (READ INSTIGATED): " << row_activates_r << "\n";
  file << "\tNormal: " << row_activates_rn << " \tPrefetch: " << row_activates_rp << "\n";
  file << "Row ACTs (WRITE INSTIGATED): " << row_activates_wb << "\n";
  file << "Total Row ACTs: " << row_activates_r + row_activates_wb << "\n";
  file << "####################################################################################################\n";
  file << "Channels: " << dram_channels << "\n";
  file << "Ranks: " << dram_ranks << "\n";
  file << "Banks: " << dram_banks << "\n";
  file << "Rows: " << dram_rows << "\n";
  file << "Columns: " << dram_columns << "\n";
  file << "Address Space Used: " << address_space_usage * 100.0 << "%\n";
  file << "####################################################################################################\n";
  file << "Stats by Row\n";

  // lets try to sort
  std::vector<std::pair<Address, ActivateLifetimeCount>> final_output;
  for (auto it = activate_master.begin(); it != activate_master.end(); it++) {
    final_output.push_back(*(it));
  }
  std::sort(final_output.begin(), final_output.end(), sortbysec);

  // print output
  for (auto it = final_output.begin(); it != final_output.end(); it++) {
      // Print out data for row
      file << "\tChannel: 0x" << std::hex << it->first.get_channel();
      file << "\tRank: 0x" << std::hex << it->first.get_rank();
      file << "\tBank: 0x" << std::hex << it->first.get_bank();
      file << "\tRow: 0x" << std::hex << it->first.get_row();
      file << "\tLifetime Hammers/(Normal:Prefetch:Writeback): " << std::dec << it->second.total_activations << " (" << it->second.read_activations << ":" << it->second.prefetch_activations << ":" << it->second.writeback_activations << ")";
      file << "\n";
  }

  file << "####################################################################################################\n";
  file.close();

  // print histograms now
  std::ofstream file_hr;
  file_hr.open(file_name + ".hr");
  for (auto it = read_activate_histogram.begin(); it != read_activate_histogram.end(); it++) {
    file_hr << (it->first * 100) << " " << it->second << "\n";
  }
  file_hr.close();
  std::ofstream file_hp;
  file_hp.open(file_name + ".hp");
  for (auto it = pref_activate_histogram.begin(); it != pref_activate_histogram.end(); it++) {
    file_hp << (it->first * 100) << " " << it->second << "\n";
  }
  file_hp.close();
  std::ofstream file_hwb;
  file_hwb.open(file_name + ".hwb");
  for (auto it = wb_activate_histogram.begin(); it != wb_activate_histogram.end(); it++) {
    file_hwb << (it->first * 100) << " " << it->second << "\n";
  }
  file_hwb.close();

  phase++;

  activate_master.clear();
}