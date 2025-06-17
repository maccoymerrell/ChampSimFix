#include "dram_prefetch_buffer_nl.h"
#include "../spp_ppf/spp_ppf.h"

uint32_t dram_prefetch_buffer_nl::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in, uint32_t metadata_hit)
{
  if(addr == champsim::address{})
    return metadata_in;

  add_to_pagemap(addr,cpu);
  long set = addr.slice(champsim::dynamic_extent{intern_->OFFSET_BITS, champsim::lg2(intern_->NUM_SET)}).to<long>();
  bool sampled_up = false; //is_sampled(set) && (set % 2);
  bool sampled_down = false; //is_sampled(set) && !(set % 2);
  if(useful_prefetch) {
    //useful[cpu]++;
    //fmt::print("Increasing confidence via useful prefetch\n");
    if(metadata_hit == NEXTLINE_ID) {
      global_useful_nextline[cpu]++;
    }
    else if (metadata_hit == BUFFER_ID)
      global_useful_buffer[cpu]++;
    
    if(metadata_hit == BUFFER_ID) {
      increase_confidence_useful(addr);
      if(!intern_->warmup)
        useful_tallied += 1;
    }
  }

  if(cache_hit == 0) {
    if(metadata_in == NEXTLINE_ID) {
      //fmt::print("Triggering walk based on next_line prefetch\n");
    }
    if(metadata_in != NEXTLINE_ID) {
      //fmt::print("Increasing confidence via demand miss\n");
      increase_confidence_stream(addr, type == access_type::PREFETCH);
    }
    /*
    if(type == access_type::PREFETCH && metadata_in == spp_ppf::SPP_LLC_TARGET_ID) {
      if(should_drop_prefetch(addr,cpu)) {
        intern_->drop_prefetch_access(addr);
        if(!intern_->warmup)
          prefetches_dropped++;
        return metadata_in;
      }
    }*/

    update_walker(addr,cpu);

  }

  //next line x4 if we have the MSHR capacity
  if(metadata_in != NEXTLINE_ID) {
    champsim::block_number pf_addr{addr};
    //fmt::print("[{}] Invoked prefetch for address: {}, hit: {}\n", intern_->NAME, addr, cache_hit);
    for(std::size_t offset = 1; offset <= variable_nextline_conf[cpu]; offset++) {
      bool success = false;
      //we should do a confidence lookup here
        //fmt::print("[{}] \tIssued prefetch for address: {}, bank: {}, cpu: {}\n",intern_->NAME, champsim::address{pf_addr + offset}, MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(champsim::address{pf_addr + offset}),cpu);
        bool pm = check_pagemap(champsim::address{pf_addr + offset},cpu);
        if(!pm)
          success = prefetch_line(champsim::address{pf_addr + offset}, true, cpu, NEXTLINE_ID, false, true);
        else if(!intern_->warmup)
          prefetches_filtered++;
        if(success) {
          if(!intern_->warmup)
             next_line_issued += 1;
          add_to_pagemap(champsim::address{pf_addr + offset},cpu);
        }
    }
  }
  
  return metadata_in;
}

bool dram_prefetch_buffer_nl::should_drop_prefetch(champsim::address addr, uint32_t cpu) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);

  auto rb_entry = row_walker_table[rb].check_hit(row_walker{row,0,0});
  if(rb_entry.has_value()) {
    //add confidence criteria here?
    if((col < rb_entry->position || col > rb_entry->position + STREAM_FORWARD_WINDOW) && rb_entry->confidence >= CONF_DROP_THRESH) {
      if(!intern_->warmup && col < rb_entry->position)
        prefetches_dropped_too_far_back++;
      if(!intern_->warmup && col > rb_entry->position + STREAM_FORWARD_WINDOW)
        prefetches_dropped_too_far_forward++;
      return true;
    }
  }
  return false;
}

void dram_prefetch_buffer_nl::update_row_open_table(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);

  //row act, update last_closed time for closing rowbuffer
  if(row_open_table[rb].row != row) {
    if(!intern_->warmup)
      opened_rows += 1;

    row_open_table_entry new_rot;
    new_rot.row = row;
    new_rot.position = col;
    new_rot.direction = FORWARD;
    new_rot.access_count = 1;
    row_open_table[rb] = new_rot;
  }


}
bool dram_prefetch_buffer_nl::is_row_open(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  return row_open_table[rb].row == row;
}


uint8_t dram_prefetch_buffer_nl::modify_confidence(uint8_t conf, uint8_t amnt, bool increment) {
  if(increment) {
    if((int)conf + (int)amnt >= (int)CONF_MAX)
      conf = CONF_MAX;
    else
      conf += amnt;
  }
  else {
    if(conf <= amnt)
      conf = 0;
    else
      conf -= amnt;
  }
  return conf;
}

void dram_prefetch_buffer_nl::increase_confidence_stream(champsim::address addr, bool prefetch) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
    if((col >= entry->position && col <= entry->position + STREAM_FORWARD_WINDOW)) {
      if(!intern_->warmup) {
        if(prefetch)
          conf_prefetch_stream++;
        else
          conf_demand_stream++;
      }
      if(prefetch)
        entry->confidence = modify_confidence(entry->confidence,STREAM_PREFETCH_CONF,true);
      else
        entry->confidence = modify_confidence(entry->confidence,STREAM_DEMAND_CONF,true);
    }
    else {
      if(!intern_->warmup) {
        if(prefetch)
          conf_prefetch_random++;
        else
          conf_demand_random++;
      }
      if(prefetch)
        entry->confidence = modify_confidence(entry->confidence,STREAM_PREFETCH_NCONF,false);
      else
        entry->confidence = modify_confidence(entry->confidence,STREAM_DEMAND_NCONF,false);
    }
    row_walker_table[rb].fill(entry.value());
  }
}

void dram_prefetch_buffer_nl::increase_confidence_useful(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
    if(!intern_->warmup)
      conf_useful++;
    //amount to increase should be dependent on current entry confidence
    int amount_to_increase = USEFUL_CONF - (USEFUL_NCONF*(entry->confidence / CONF_MAX));
    entry->confidence = modify_confidence(entry->confidence,amount_to_increase,true);
    row_walker_table[rb].fill(entry.value());
  }
}

void dram_prefetch_buffer_nl::increase_confidence_opened(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
    if(!intern_->warmup)
      conf_act++;
    //amount to increase should be dependent on current entry confidence
    entry->confidence = modify_confidence(entry->confidence,ACT_CONF,true);
    row_walker_table[rb].fill(entry.value());
  }
}

void dram_prefetch_buffer_nl::decrease_confidence_useless(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
      if(!intern_->warmup)
        conf_useless++;
      entry->confidence = modify_confidence(entry->confidence,USELESS_NCONF,false);
      row_walker_table[rb].fill(entry.value());
  }
}

std::size_t dram_prefetch_buffer_nl::get_depth(uint8_t conf, uint32_t cpu) {
  uint8_t threshold = 0;
  conf = conf < variable_buffer_conf[cpu] ? 0 : conf - variable_buffer_conf[cpu];
  for (auto thres : THRESH) {
    threshold++;
    if(conf < thres)
      break;
  }
  return DEPTHS.at(threshold - 1);
}

void dram_prefetch_buffer_nl::update_walker(champsim::address addr, uint32_t cpu) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  bool is_open = is_row_open(addr);
  if(!is_open) {
    update_row_open_table(addr);
    increase_confidence_opened(addr);
  }

  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
    uint8_t prev_col = entry->position;
    rw.opened_at = !is_open ? entry->position + 1 : entry->opened_at;
    rw.confidence = entry->confidence;
    //rw.confidence = intern_->get_pq_occupancy().back() >= 16 ? entry->confidence * 0.9 : entry->confidence;
    //rw.confidence = entry->confidence;
    std::size_t depth = get_depth(rw.confidence,cpu);

    //issue prefetches backwards
    //if(rw.confidence >= 50) {
    bool all_success = true;
      //issue prefetches forwards
      //if(col >= rw.opened_at/*&& (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()) < intern_->get_mshr_size()*0.7*/) {
        //int amount_to_prefetch = std::min(depth,(intern_->get_mshr_size() - (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()))/2);
        int amount_to_prefetch = std::min(depth,(intern_->get_mshr_size() - (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back())));
        //int amount_to_prefetch = depth;
        int prefetched_so_far = 0;
        //fmt::print("Prefetching forwards at row: {} rb: {} between columns:{} - {}\n",row,rb,col + 1,col + amount_to_prefetch > 63 ? 63 : col + amount_to_prefetch);
        for(int i = col + 1; i < (1 << (column_bits.size())) && i <= col + amount_to_prefetch; i++) {
          bool success = true;
          bool pm = check_pagemap(compose_base_and_column(addr,i),cpu);
          if(!pm) {
            success = prefetch_line(compose_base_and_column(addr,i),true,cpu,BUFFER_ID,false,false);
            if(success) {
             add_to_pagemap(compose_base_and_column(addr,i),cpu);
              //rw.confidence = modify_confidence(rw.confidence,1,false);
              if(!intern_->warmup)
                forward_buffer_issued += 1;
            } else if (!intern_->warmup) {
              prefetches_rejected++;
            }
          } else if (!intern_->warmup) {
            prefetches_filtered++;
          }
          if(success)
            prefetched_so_far++;
          else {
            //rw.confidence *= 0.9;
            break;
          }
        }
        rw.position = col + prefetched_so_far;
      //}
    //}
    
    row_walker_table[rb].fill(rw);
  }
  else {
    row_walker_table[rb].fill(rw);
  }
}


champsim::address dram_prefetch_buffer_nl::compose_base_and_column(champsim::address base, uint64_t column) {
  //1. iterate through all column bits in the base
  //2. set each bit to the matching bits in the column
  uint64_t base_temp = base.to<uint64_t>();
  for(std::size_t i = 0; i < column_bits.size(); i++) {
    if(column & (1ull << i))
      base_temp |= 1ull << column_bits[i];
    else
      base_temp &= ~(1ull << column_bits[i]);
  }
  return champsim::address{base_temp};
}


void dram_prefetch_buffer_nl::prefetcher_initialize() {

  //fmt::print("SIZE OF ROW HISTORY TABLE: {}\n",champsim::data::kibibytes{row_history_table_size});
  	// Determine set sampling rate

  for(int i = 0; i < MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers(); i++) {
    row_walker_table.push_back(champsim::msl::lru_table<row_walker,row_walker_set,row_walker_way>{RW_SETS,RW_WAYS});
  }

  row_open_table.resize(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers());

  //find column bits
  for(int i = 0; i < 64; i ++) {
    if(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(champsim::address{1ul << i}) != 0)
      column_bits.push_back(i);
  }
  fmt::print("[{}] Initialized Buffer-NL, Column Bits are: {}\n",intern_->NAME,fmt::join(column_bits, ","));

  for(int i = 0; i < NUM_CPUS; i++) {
    page_map_table.emplace_back(PM_SETS,PM_WAYS);
  }
  for(int i = 0; i < NUM_CPUS; i++) {
    global_useful_buffer.push_back(0);
    global_useless_buffer.push_back(0);
    global_useful_nextline.push_back(0);
    global_useless_nextline.push_back(0);
    variable_buffer_conf.push_back(BUFFER_MIN_NCONF);
    variable_nextline_conf.push_back(NEXTLINE_MAX_DEPTH);
  }
}

uint32_t dram_prefetch_buffer_nl::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict)
{
  bool sampled_up = false; //is_sampled(set) && (set % 2);
  bool sampled_down = false; //is_sampled(set) && !(set % 2);
  //if(sampled_up || sampled_down)

  if(evicted_addr != champsim::address{} && useless && metadata_evict == BUFFER_ID) {
    decrease_confidence_useless(evicted_addr);
    if(!intern_->warmup)
      useless_tallied += 1;
  }
  else if (evicted_addr != champsim::address{} && useless && metadata_in == BUFFER_ID) {
    decrease_confidence_useless(addr);
    if(!intern_->warmup)
      pp_thrashes++;
  }
  if(metadata_evict == BUFFER_ID && useless && cpu_evict != NUM_CPUS)
    global_useless_buffer[cpu_evict]++;
  else if (metadata_evict == NEXTLINE_ID && useless && metadata_in == BUFFER_ID && cpu_evict != NUM_CPUS)
    global_useless_buffer[cpu]++;
  else if (metadata_evict == NEXTLINE_ID && useless && cpu_evict != NUM_CPUS)
    global_useless_nextline[cpu]++;
  
  if(evicted_addr != champsim::address{}) {
    for(int i = 0; i < NUM_CPUS; i++)
      remove_from_pagemap(evicted_addr,i);
  }

  return metadata_in;
}

void dram_prefetch_buffer_nl::add_to_pagemap(champsim::address addr, uint32_t cpu) {
  uint64_t pn = addr.to<uint64_t>() >> (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE);
  uint64_t bn = (addr.to<uint64_t>() % (1 << (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE))) >> LOG2_BLOCK_SIZE;
  page_map pm(pn);

  auto entry = page_map_table.at(cpu).check_hit(pm);
  if(entry.has_value()) {
    entry->bits.at(bn) = page_map::PM_BASE;
    page_map_table.at(cpu).fill(entry.value());
  } else {
    pm.bits.at(bn) = page_map::PM_BASE;
    page_map_table.at(cpu).fill(pm);
  }
}

bool dram_prefetch_buffer_nl::check_pagemap(champsim::address addr, uint32_t cpu) {
  uint64_t pn = addr.to<uint64_t>() >> (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE);
  uint64_t bn = (addr.to<uint64_t>() % (1 << (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE))) >> LOG2_BLOCK_SIZE;
  page_map pm(pn);
  auto entry = page_map_table.at(cpu).check_hit(pm);
  if(entry.has_value()) {
    if(entry->bits.at(bn) == 0) {
      return false;
    } else {
      entry->bits.at(bn)--;
      page_map_table.at(cpu).fill(entry.value());
      return true;
    }
  }
  return false;
}

void dram_prefetch_buffer_nl::remove_from_pagemap(champsim::address addr, uint32_t cpu) {
  uint64_t pn = addr.to<uint64_t>() >> (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE);
  uint64_t bn = (addr.to<uint64_t>() % (1 << (champsim::lg2(PAGE_MAP_SIZE) + LOG2_BLOCK_SIZE))) >> LOG2_BLOCK_SIZE;
  page_map pm(pn);
  auto entry = page_map_table.at(cpu).check_hit(pm);
  if(entry.has_value()) {
    entry->bits.at(bn) = 0;
    page_map_table.at(cpu).fill(entry.value());
  }
}

void dram_prefetch_buffer_nl::prefetcher_cycle_operate() {
  if(epoch_counter >= usefulness_measure_epoch) {
    for(int i = 0; i < NUM_CPUS; i++) {
      double global_usefulness_buffer = (global_useless_buffer[i] <= 100 || (global_useful_buffer[i] + global_useless_buffer[i] <= 100)) ? 1.0 : (global_useful_buffer[i]) / (double)(global_useless_buffer[i] + global_useful_buffer[i]);
      double global_usefulness_nextline = (global_useless_nextline[i] <= 100 || (global_useful_nextline[i] + global_useless_nextline[i] <= 100)) ? 1.0 : (global_useful_nextline[i]) / (double)(global_useless_nextline[i] + global_useful_nextline[i]);
      fmt::print("CPU: {} Buffer usefulness: {} ({}/{}) Nextline usefulness: {} ({}/{})\n",i,global_usefulness_buffer,global_useful_buffer[i],global_useless_buffer[i],global_usefulness_nextline,global_useful_nextline[i],global_useless_nextline[i]);
      global_useless_buffer[i] = 0;
      global_useful_buffer[i] = 0;
      global_useless_nextline[i] = 0;
      global_useful_nextline[i] = 0;
      
      if(global_usefulness_buffer > TARGET_BUFFER_ACCURACY)
        variable_buffer_conf[i] = variable_buffer_conf[i] < BUFFER_NCONF_STEP + BUFFER_MIN_NCONF ? BUFFER_MIN_NCONF : variable_buffer_conf[i] - BUFFER_NCONF_STEP;
      else
        variable_buffer_conf[i] = variable_buffer_conf[i] + BUFFER_NCONF_STEP > BUFFER_MAX_NCONF ? BUFFER_MAX_NCONF : variable_buffer_conf[i] + BUFFER_NCONF_STEP;

      if(global_usefulness_nextline > TARGET_NL_ACCURACY)
        variable_nextline_conf[i] = variable_nextline_conf[i] + 1 > NEXTLINE_MAX_DEPTH ? NEXTLINE_MAX_DEPTH : variable_nextline_conf[i] + 1;
      else
        variable_nextline_conf[i] = variable_nextline_conf[i] < NEXTLINE_MIN_DEPTH + 1 ? NEXTLINE_MIN_DEPTH : variable_nextline_conf[i] - 1;

      fmt::print("\tBuffer confidence modifier: {} Next line: {}\n",variable_buffer_conf[i],variable_nextline_conf[i]);
    }
    epoch_counter = 0;
  }
  epoch_counter++;
}

void dram_prefetch_buffer_nl::prefetcher_final_stats() {
  fmt::print("Prefetches Forward: {} Prefetches Backwards: {} Useful: {} Useless: {} Next Line: {} Opened Rows: {} Dropped: {} [{}, {}] Prefetch-Prefetch Thrashes: {} Rejected: {} Filtered: {}\n",forward_buffer_issued, backward_buffer_issued, useful_tallied, useless_tallied, next_line_issued, opened_rows,prefetches_dropped,prefetches_dropped_too_far_back,prefetches_dropped_too_far_forward,pp_thrashes,prefetches_rejected, prefetches_filtered);
  fmt::print("Confidence Types: Useful: {}, Useless: {}, Stream: ({},{}), Random: ({},{}), Row ACT: {}\n",conf_useful,conf_useless,conf_demand_stream,conf_prefetch_stream,conf_demand_random,conf_prefetch_random,conf_act);
  uint64_t confidence_total = 0;
  uint64_t tracked_rows = 0;

  std::vector<uint64_t> tracked_rows_per_rb(row_walker_table.size(),0);
  std::vector<uint64_t> confidence_total_per_rb(row_walker_table.size(),0);

  std::vector<uint64_t> tracked_per_bin(DEPTHS.size(),0);

  int current_rb = 0;
  for(auto rb: row_walker_table) {
    for(auto block: rb.get_contents()) {
      if(block.last_used != 0) {
        tracked_rows += 1;
        tracked_rows_per_rb[current_rb] += 1;
        confidence_total += block.data.confidence;
        confidence_total_per_rb[current_rb] += block.data.confidence;
        uint8_t threshold = 0;
        for (auto thres : THRESH) {
          threshold++;
          if(block.data.confidence < thres)
            break;
        }
        tracked_per_bin[threshold-1] += 1;
      }
    }
    current_rb += 1;
  }

  fmt::print("GLOBAL CONFIDENCE: {} TRACKED ROWS: {}\n",confidence_total / (double)tracked_rows,tracked_rows);
  fmt::print("TRACKED PER BIN:\n");
  for (int i = 0; i < DEPTHS.size(); i++) {
    fmt::print("\t{}: {}\n",THRESH[i],tracked_per_bin[i]);
  }
  fmt::print("PER ROWBUFFER:(RB, CONF, TRACKED)\n");
  for (int i = 0; i < row_walker_table.size(); i++) {
    fmt::print("\t{}: {} {}\n",i,confidence_total_per_rb[i] / (double)tracked_rows_per_rb[i], tracked_rows_per_rb[i]);
  }
}
