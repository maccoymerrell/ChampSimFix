#include "dram_prefetch_buffer_spp.h"

uint32_t dram_prefetch_buffer_spp::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in, uint32_t metadata_hit)
{
  //if(cache_hit != 0) {
  // if(metadata_hit == NEXT_LINE_ID)
  //    fmt::print("Got a hit into next line ID\n");
  //  else if (metadata_hit == BUFFER_ID)
  //    fmt::print("Got a hit into buffer ID\n");
  //}
  if(addr == champsim::address{})
    return metadata_in;

  if(useful_prefetch) {
    //useful[cpu]++;
    //fmt::print("Increasing confidence via useful prefetch\n");
    if(metadata_hit == NEXT_LINE_ID)
      global_useful_nextline++;
    else if (metadata_hit == BUFFER_ID)
      global_useful_buffer++;
    
    if(metadata_hit == BUFFER_ID) {
      increase_confidence_useful(addr);
      if(!intern_->warmup)
        useful_tallied += 1;
    }
  }

  if(cache_hit == 0) {
    if(metadata_in == NEXT_LINE_ID) {
      //fmt::print("Triggering walk based on next_line prefetch\n");
    }
    if(type != access_type::PREFETCH) {
      //fmt::print("Increasing confidence via demand miss\n");
      increase_confidence_demand(addr);
      decrease_confidence_thrash(addr);
    }

    //if upper-level prefetch and doesn't match an expected prefetch location, drop
    if(type == access_type::PREFETCH /*&& metadata_in != NEXT_LINE_ID*/) {
      if(should_drop_prefetch(addr,cpu)) {
        intern_->drop_prefetch_access(addr);
        if(!intern_->warmup)
          prefetches_dropped++;
        return metadata_in;
      }
    }
    update_walker(addr,cpu);

  }

  //next line x4 if we have the MSHR capacity
  if(metadata_in != NEXT_LINE_ID) {
    champsim::block_number pf_addr{addr};
    //fmt::print("[{}] Invoked prefetch for address: {}, hit: {}\n", intern_->NAME, addr, cache_hit);
    for(std::size_t offset = 1; offset <= 4; offset++) {
      bool success = false;
      //we should do a confidence lookup here
        //fmt::print("[{}] \tIssued prefetch for address: {}, bank: {}, cpu: {}\n",intern_->NAME, champsim::address{pf_addr + offset}, MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(champsim::address{pf_addr + offset}),cpu);
        if(!filter.check(champsim::address{pf_addr + offset},intern_->current_cycle(),false))
          success = prefetch_line(champsim::address{pf_addr + offset}, true, cpu, NEXT_LINE_ID, false, true);
        if(!success) {
          if(!intern_->warmup)
             next_line_issued += 1;
        }
    }
  }
  
  return metadata_in;
}

bool dram_prefetch_buffer_spp::should_drop_prefetch(champsim::address addr, uint32_t cpu) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);

  auto rb_entry = row_walker_table[rb].check_hit(row_walker{row,0,0});
  if(rb_entry.has_value()) {
    //add confidence criteria here?
    if((col < rb_entry->position || col > rb_entry->position + 4) && rb_entry->confidence >= CONF_DROP_THRESH) {
      if(!intern_->warmup && col < rb_entry->position)
        prefetches_dropped_too_far_back++;
      if(!intern_->warmup && col > rb_entry->position + 2)
        prefetches_dropped_too_far_forward++;
      return true;
    }
  }
  return false;
}



void dram_prefetch_buffer_spp::update_row_open_table(champsim::address addr) {
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
bool dram_prefetch_buffer_spp::is_row_open(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  return row_open_table[rb].row == row;
}


uint8_t dram_prefetch_buffer_spp::modify_confidence(uint8_t conf, uint8_t amnt, bool increment) {
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

void dram_prefetch_buffer_spp::increase_confidence_demand(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
    if((col > entry->position && col <= entry->position + 2))
      entry->confidence = modify_confidence(entry->confidence,DEMAND_CONF,true);
    else
      entry->confidence = modify_confidence(entry->confidence,DEMAND_NCONF,false);
    row_walker_table[rb].fill(entry.value());
  }
}

void dram_prefetch_buffer_spp::decrease_confidence_thrash(champsim::address addr) {
  thrash_detector td;
  td.victim = addr;
  auto entry = thrash_detector_table.check_hit(td);
  if(entry.has_value()) {
    champsim::address pf_addr = entry->prefetch;
    thrash_detector_table.invalidate(entry.value());

    std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(pf_addr);
    std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr);
    std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(pf_addr);
    row_walker rw{row,col,col};

    auto entry = row_walker_table[rb].check_hit(rw);
    if(entry.has_value()) {
        entry->confidence = modify_confidence(entry->confidence,THRASH_NCONF,false);
        if(!intern_->warmup)
          thrashes_detected++;
        row_walker_table[rb].fill(entry.value());
    }
  }
}

void dram_prefetch_buffer_spp::increase_confidence_useful(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
    //amount to increase should be dependent on current entry confidence
    int amount_to_increase = USEFUL_CONF - (USEFUL_NCONF*(entry->confidence / CONF_MAX));
    entry->confidence = modify_confidence(entry->confidence,amount_to_increase,true);
    row_walker_table[rb].fill(entry.value());
  }
}

void dram_prefetch_buffer_spp::decrease_confidence_useless(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
      entry->confidence = modify_confidence(entry->confidence,USELESS_NCONF,false);
      row_walker_table[rb].fill(entry.value());
  }
}

std::size_t dram_prefetch_buffer_spp::get_depth(uint8_t conf) {
  uint8_t threshold = 0;
  for (auto thres : THRESH) {
    threshold++;
    if(conf < thres)
      break;
  }
  return std::min((std::size_t)DEPTHS.at(threshold - 1),variable_max_depth);
}

void dram_prefetch_buffer_spp::add_td(champsim::address victim, champsim::address prefetch) {
  thrash_detector td;
  td.victim = victim;
  td.prefetch = prefetch;
  thrash_detector_table.fill(td);
}

void dram_prefetch_buffer_spp::update_walker(champsim::address addr, uint32_t cpu) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  bool is_open = is_row_open(addr);
  if(!is_open)
    update_row_open_table(addr);

  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
    uint8_t prev_col = entry->position;
    rw.opened_at = !is_open ? entry->position + 1 : entry->opened_at;
    rw.confidence = intern_->get_mshr_size() <= intern_->get_mshr_occupancy() ? entry->confidence * 0.9 : entry->confidence;
    //rw.confidence = intern_->get_pq_occupancy().back() >= 16 ? entry->confidence * 0.9 : entry->confidence;
    //rw.confidence = entry->confidence;
    std::size_t depth = get_depth(rw.confidence);

    //issue prefetches backwards
    //if(rw.confidence >= 50) {
    bool all_success = true;
      if(col > prev_col + 1 /*&& (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()) < intern_->get_mshr_size()*0.7*/) {
        //fmt::print("Filling backwards gap at row: {} rb: {} between columns:{} - {}\n",row,rb,prev_col + 1,col - 1);
        int amount_to_prefetch = std::min(depth,intern_->get_mshr_size() - (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()));
        int prefetched_so_far = 0;
        for(int i = (int)col - 1; i > prev_col && i > (int)col - amount_to_prefetch; i--) {
          bool success = true;
          if(!filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),false)) {
            success = prefetch_line(compose_base_and_column(addr,i),true,cpu,BUFFER_ID,false,false);
            if(success) {
              filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),true);
              //rw.confidence = modify_confidence(rw.confidence,1,false);
              if(!intern_->warmup)
                backward_buffer_issued += 1;
            } else if(!intern_->warmup) {
              prefetches_rejected++;
            }
          }
          if(success)
            prefetched_so_far++;
          else {
            all_success = false;
            //rw.confidence *= 0.9;
            break;
          }
            
        }
        rw.position = all_success ? col : prev_col + prefetched_so_far;
      }
      //issue prefetches forwards
      if(col >= rw.opened_at/*&& (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()) < intern_->get_mshr_size()*0.7*/) {
        int amount_to_prefetch = std::min(depth,intern_->get_mshr_size() - (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()));
        int prefetched_so_far = 0;
        //fmt::print("Prefetching forwards at row: {} rb: {} between columns:{} - {}\n",row,rb,col + 1,col + amount_to_prefetch > 63 ? 63 : col + amount_to_prefetch);
        for(int i = col + 1; i < (1 << (column_bits.size())) && i <= col + amount_to_prefetch; i++) {
          bool success = true;
          if(!filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),false)) {
            success = prefetch_line(compose_base_and_column(addr,i),true,cpu,BUFFER_ID,false,false);
            if(success) {
              filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),true);
              //rw.confidence = modify_confidence(rw.confidence,1,false);
              if(!intern_->warmup)
                forward_buffer_issued += 1;
            } else if (!intern_->warmup) {
              prefetches_rejected++;
            }
          }
          if(success)
            prefetched_so_far++;
          else {
            //rw.confidence *= 0.9;
            break;
          }
        }
        rw.position = col + prefetched_so_far;
      }
    //}
    
    row_walker_table[rb].fill(rw);
  }
  else {
    row_walker_table[rb].fill(rw);
  }
}


champsim::address dram_prefetch_buffer_spp::compose_base_and_column(champsim::address base, uint64_t column) {
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


void dram_prefetch_buffer_spp::prefetcher_initialize() {

  //fmt::print("SIZE OF ROW HISTORY TABLE: {}\n",champsim::data::kibibytes{row_history_table_size});

  for(int i = 0; i < MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers(); i++) {
    row_walker_table.push_back(champsim::msl::lru_table<row_walker,row_walker_set,row_walker_way>{RW_SETS,RW_WAYS});
  }

  row_open_table.resize(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers());

  //find column bits
  for(int i = 0; i < 64; i ++) {
    if(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(champsim::address{1ul << i}) != 0)
      column_bits.push_back(i);
  }
  fmt::print("[{}] DRAM Column Bits are: {}\n",intern_->NAME,fmt::join(column_bits, ","));
}

uint32_t dram_prefetch_buffer_spp::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  if(evicted_addr != champsim::address{} && useless) {
    decrease_confidence_useless(evicted_addr);
    if(prefetch != 0 && metadata_in == BUFFER_ID) {
      decrease_confidence_useless(addr);
      if(!intern_->warmup)
        pp_thrashes++;
    }
    if(!intern_->warmup)
      useless_tallied += 1;
  }
  if(prefetch != 0 && evicted_addr != champsim::address{}) {
    add_td(evicted_addr, addr);
  }
  if(metadata_in == BUFFER_ID)
    global_fills_buffer++;
  else if(metadata_in == NEXT_LINE_ID)
    global_fills_nextline++;

  return metadata_in;
}

void dram_prefetch_buffer_spp::prefetcher_cycle_operate() {
  if(epoch_counter >= usefulness_measure_epoch) {
    global_usefulness_buffer = global_fills_buffer == 0 ? 1.0 : (global_useful_buffer) / (double)(global_fills_buffer);
    global_usefulness_nextline = global_fills_nextline == 0 ? 1.0 : (global_useful_nextline) / (double)(global_fills_nextline);
    epoch_counter = 0;
    global_fills_buffer = 0;
    global_useful_buffer = 0;
    global_fills_nextline = 0;
    global_useful_nextline = 0;
    //fmt::print("Global buffer usefulness was: {} next line usefulness: {}\n",global_usefulness_buffer,global_usefulness_nextline);
    //if(global_usefulness_buffer > global_usefulness_nextline * .9)
    //  variable_max_depth = std::min(MAX_DEPTH, variable_max_depth + 1);
    //else
    //  variable_max_depth = variable_max_depth <= 1 ? 1 : variable_max_depth - 1;

    if(global_usefulness_nextline > NL_THRESH)
      variable_nl_depth = std::min(variable_nl_depth + 1, MAX_NL_DEPTH);
    else
      variable_nl_depth = variable_nl_depth <= 1 ? 1 : variable_nl_depth - 1;
    //fmt::print("\tVariable max depth: {}\n",variable_max_depth);
  }
  epoch_counter++;
}

void dram_prefetch_buffer_spp::prefetcher_final_stats() {
  fmt::print("Prefetches Forward: {} Prefetches Backwards: {} Useful: {} Useless: {} Next Line: {} Opened Rows: {} Dropped: {} [{}, {}] Thrashes: {} Prefetch-Prefetch Thrashes: {} Rejected: {}\n",forward_buffer_issued, backward_buffer_issued, useful_tallied, useless_tallied, next_line_issued, opened_rows,prefetches_dropped,prefetches_dropped_too_far_back,prefetches_dropped_too_far_forward,thrashes_detected,pp_thrashes,prefetches_rejected);

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
