#include "spp_dev_row_map.h"
#include "dram_controller.h"

uint32_t spp_dev_row_map::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  //fmt::print("spp_dev_row_map: OPERATING CACHE\n");
  //on miss, update open rows and check if this opened a new row
  //if so, grab the columns out of it
  if(cache_hit == 0) {
    //update row state table
    bool opened = update_row_state_table(addr);
    remove_column_evicted(addr);
    //if(opened)
    //  issue_column_prefetch(addr);
  }
  if(cache_hit == 0 || is_row_open(addr)) {
    issue_column_prefetch(addr);
  }

  //record columns we are missing with prefetches
  if(!cache_hit)
    add_column_used(addr);
  //else if(type == access_type::PREFETCH)
  //  remove_column_used(addr);


  return metadata_in;
}
uint32_t spp_dev_row_map::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  if(evicted_addr != champsim::address{}) {
    add_column_evicted(evicted_addr);
  }
  return metadata_in;
}

bool spp_dev_row_map::is_row_open(champsim::address addr) {
  return row_table.check_hit(row_state_table_entry{get_dram_group(addr),get_dram_row(addr)}).has_value();
}

bool spp_dev_row_map::update_row_state_table(champsim::address addr) {

  bool opened = !row_table.check_hit(row_state_table_entry{get_dram_group(addr),get_dram_row(addr)}).has_value();
  row_table.fill(row_state_table_entry{get_dram_group(addr),get_dram_row(addr)});

  return opened;
}

void spp_dev_row_map::add_column_used(champsim::address addr) {
  auto entry = row_tracker.check_hit(row_tracker_entry{get_dram_row(addr)});

  if(!entry.has_value()) {
    entry = row_tracker_entry{get_dram_row(addr)};
  }

  entry->blocks.set(get_dram_column(addr));
  row_tracker.fill(entry.value());
}

void spp_dev_row_map::remove_column_used(champsim::address addr) {
  auto entry = row_tracker.check_hit(row_tracker_entry{get_dram_row(addr)});
  if(entry.has_value()) {
    entry->blocks.reset(get_dram_column(addr));
    row_tracker.fill(entry.value());
  }
}
void spp_dev_row_map::add_column_evicted(champsim::address addr) {
  auto entry = row_tracker.check_hit(row_tracker_entry{get_dram_row(addr)});

  if(entry.has_value()) {
    entry->evicted.set(get_dram_column(addr));
    row_tracker.fill(entry.value());
  }
}
void spp_dev_row_map::remove_column_evicted(champsim::address addr) {
  auto entry = row_tracker.check_hit(row_tracker_entry{get_dram_row(addr)});
  if(entry.has_value()) {
    entry->evicted.reset(get_dram_column(addr));
    row_tracker.fill(entry.value());
  }
}

void spp_dev_row_map::issue_column_prefetch(champsim::address addr) {
  auto entry = row_tracker.check_hit(row_tracker_entry{get_dram_row(addr)});
  auto column_id = get_dram_column(addr);
  int issue_count = 2;

  //issue forwards, then backwards
  if(entry.has_value()) {
    entry->last_forward = column_id;
    entry->last_issue = column_id;
    for(int id = column_id; id < BLOCKS_PER_RB; id++) {
      if(column_id == id)
        continue;
      if(entry->blocks.test(id) && entry->evicted.test(id) && issue_count > 0) {
        bool success = prefetch_line(compose_base_and_column(addr,id),true,0);
        if(success) {
          column_prefetches_issued++;
          entry->evicted.reset(id);
        }
        else
          full_queue++;
        issue_count--;
        entry->last_forward = id;
      }
    }
  }
  issue_count = 2;
  if(entry.has_value()) {
    entry->last_reverse = column_id;
    entry->last_issue = column_id;
    for(int id = column_id; id >= 0; id--) {
      if(column_id == id)
        continue;
      if(entry->blocks.test(id) && entry->evicted.test(id) && issue_count > 0) {
        bool success = prefetch_line(compose_base_and_column(addr,id),true,0);
        if(success) {
          column_prefetches_issued++;
          entry->evicted.reset(id);
        }
        else
          full_queue++;
        issue_count--;
        entry->last_reverse = id;
      }
    }
  }
  if(entry.has_value())
    row_tracker.fill(entry.value());
}

//DRAM ADDRESS DECODING
uint64_t spp_dev_row_map::get_dram_column(champsim::address pf_addr) {
  uint64_t column_block = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(pf_addr);
  //fmt::print("Converted pf_addr:{} to column:{}\n",pf_addr,column_block);
  return column_block;
}
uint64_t spp_dev_row_map::get_dram_row(champsim::address pf_addr) {
  uint64_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr);
  //fmt::print("Converted pf_addr:{} to row:{}\n",pf_addr,row);
  return row;
}
unsigned int spp_dev_row_map::get_dram_group(champsim::address pf_addr) {
  std::size_t channel = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_channel(pf_addr);
  std::size_t rank = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rank(pf_addr);
  std::size_t bank = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_bank(pf_addr);

  std::size_t ranks = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->channels[0].ranks();
  std::size_t banks = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->channels[0].banks();

  std::size_t rb_id = 0;
        rb_id += bank;
        rb_id += rank * banks;
        rb_id += channel * ranks * banks;
  //id of which rowbuffer we hit
  
  //fmt::print("Converted pf_addr:{} to group:{}\n",pf_addr,rb_id);
  return(rb_id);
}

//DRAM ADDRESS RECODING
champsim::address spp_dev_row_map::compose_base_and_column(champsim::address base, uint64_t column) {
  //1. iterate through all column bits in the base
  //2. set each bit to the matching bits in the column
  uint64_t base_temp = base.to<uint64_t>();
  
  //8GB
  std::vector<uint64_t> column_bits = {7,12,13,14,15,16};
  //32GB
  //std::vector<uint64_t> column_bits = {7,13,14,15,16,17};

  for(std::size_t i = 0; i < column_bits.size(); i++) {
    if(column & (1ull << i))
      base_temp |= 1ull << column_bits[i];
    else
      base_temp &= ~(1ull << column_bits[i]);
  }
  return champsim::address{base_temp};
}

void spp_dev_row_map::prefetcher_final_stats() {
  fmt::print("SPP_LLC_STATS:\n");
  fmt::print("\tCOLUMN PREFETCHES ISSUED: {}\n",column_prefetches_issued);
  fmt::print("\tFULL QUEUE: {}\n",full_queue);

  fmt::print("Column Contents:\n");
  for(auto it : row_tracker.get_contents()) {
    fmt::print("row id: {}\n\t",it.data.row_id);
    for(std::size_t ind = 0; ind < BLOCKS_PER_RB; ind++) {
      if(it.data.last_reverse == ind)
        fmt::print("R");
      if(it.data.last_issue == ind)
        fmt::print("[");
      fmt::print("{}",(int)it.data.blocks.test(ind));
      if(it.data.last_issue == ind)
        fmt::print("]");
      if(it.data.last_forward == ind)
        fmt::print("F");
    }
    fmt::print("\n\t");
    for(std::size_t ind = 0; ind < BLOCKS_PER_RB; ind++) {
      if(it.data.last_reverse == ind)
        fmt::print("R");
      if(it.data.last_issue == ind)
        fmt::print("[");
      fmt::print("{}",(int)it.data.evicted.test(ind));
      if(it.data.last_issue == ind)
        fmt::print("]");
      if(it.data.last_forward == ind)
        fmt::print("F");
    }
    fmt::print("\n");
  }
}
