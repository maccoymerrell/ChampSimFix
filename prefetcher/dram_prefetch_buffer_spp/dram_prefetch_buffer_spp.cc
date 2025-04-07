#include "dram_prefetch_buffer_spp.h"

uint32_t dram_prefetch_buffer_spp::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  if(addr == champsim::address{})
    return metadata_in;

  if(useful_prefetch) {
    //useful[cpu]++;
    //fmt::print("Increasing confidence via useful prefetch\n");
    if(metadata_in == BUFFER_ID) {
      increase_confidence(addr,USEFUL_CONF,false);
      if(!intern_->warmup)
        useful_tallied += 1;
    }
  }

  //if(cache_hit == 0) {
  //  if(is_row_open(addr))
  //    update_row_confidence(addr);
  //  else {
  //    update_row_interval_table(addr);
  //    update_row_open_table(addr);
  //    issue_row_prefetch(addr,cpu);
  //  }
  //}

  if(cache_hit == 0) {
    if(metadata_in == NEXT_LINE_ID) {
      //fmt::print("Triggering walk based on next_line prefetch\n");
    }
    if(type != access_type::PREFETCH) {
      //fmt::print("Increasing confidence via demand miss\n");
      increase_confidence(addr,DEMAND_CONF,true);
    }

    //if prefetch and doesn't match an expected prefetch location, drop
    if(type == access_type::PREFETCH && metadata_in != NEXT_LINE_ID) {
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
      bool success = true;
      //we should do a confidence lookup here
        //fmt::print("[{}] \tIssued prefetch for address: {}, bank: {}, cpu: {}\n",intern_->NAME, champsim::address{pf_addr + offset}, MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(champsim::address{pf_addr + offset}),cpu);
        if(!filter.check(champsim::address{pf_addr + offset},intern_->current_cycle(),false))
          success = prefetch_line(champsim::address{pf_addr + offset}, true, cpu, NEXT_LINE_ID, false, true);
        if(!success)
          filter.check(champsim::address{pf_addr + offset},intern_->current_cycle(),true);
        else if(!intern_->warmup)
          next_line_issued += 1;
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
    if(col < rb_entry->position || col > rb_entry->position + 2)
      return true;
  }
  return false;
}

uint8_t dram_prefetch_buffer_spp::get_confidence(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);

  auto rb_entry = row_walker_table[rb].check_hit(row_walker{row,0,0});
  if(rb_entry.has_value()) {
    return rb_entry->confidence;
  }
  return 50;
}


void dram_prefetch_buffer_spp::update_row_interval_table(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  row_interval_table[rb].activations++;
}

void dram_prefetch_buffer_spp::update_row_open_table(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);

  //row act, update last_closed time for closing rowbuffer
  if(row_open_table[rb].row != row) {
    if(!intern_->warmup)
      opened_rows += 1;
    //try to find row
    auto entry = row_history_table.check_hit(row_history{row_open_table[rb].row});

    //auto rb_entry = row_walker_table[rb].check_hit(row_walker{row,0,0});
    //if(rb_entry.has_value()) {
    //  fmt::print("Opened row: {} rb: {} with confidence: {}\n",row,rb,rb_entry->confidence);
    //}

    //found row (check to see if we are the sampler)
    /*
    if(entry.has_value()) {
      fmt::print("Updating last closed with address: {} interval: {} confidence: {} accesses: {}\n",addr,(accesses_so_far / (row_interval_table[rb].activations))*row_interval_table.size()*2, entry->confidence, entry->access_count);
      entry->confidence = row_open_table[rb].confidence;
      entry->access_count = row_open_table[rb].access_count;
      entry->stride = row_open_table[rb].stride;
      row_history_table.fill(entry.value());
    } else {
      //need new entry
      row_history new_entry;
      new_entry.row = row_open_table[rb].row;
      fmt::print("Adding new row with address: {}\n",addr);
      //we become the sampler
      new_entry.confidence = 16;
      new_entry.access_count = row_open_table[rb].access_count;
      new_entry.stride = row_open_table[rb].stride;
      row_history_table.fill(new_entry);
    }*/

    row_open_table_entry new_rot;
    new_rot.row = row;
    new_rot.position = col;
    new_rot.direction = FORWARD;
    new_rot.access_count = 1;
    /*
    auto rht_entry = row_history_table.check_hit(row_history{row});
    if(rht_entry.has_value()) {
      new_rot.confidence = rht_entry->confidence;
      new_rot.stride = rht_entry->stride;
    }
    else {
      new_rot.confidence = 16;
      new_rot.stride = 1;
    }*/
    row_open_table[rb] = new_rot;
  }


}
bool dram_prefetch_buffer_spp::is_row_open(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  return row_open_table[rb].row == row;
}

void dram_prefetch_buffer_spp::update_row_confidence(champsim::address addr) {
  //we will sample according to the first rowbuffer to use the row
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);

  //entry and matches our sample rowbuffer
  assert(row_open_table[rb].row == row);
    
  bool new_direction = row_open_table[rb].direction;
  if(col < row_open_table[rb].position) {
    new_direction = BACKWARD;
  }
  else if(col > row_open_table[rb].position) {
    new_direction = FORWARD;
  }
  if(new_direction != row_open_table[rb].direction && row_open_table[rb].confidence > 0) {
    row_open_table[rb].confidence--;
  }
  else if(new_direction == row_open_table[rb].direction && row_open_table[rb].confidence < 32) {
    row_open_table[rb].confidence++;
  }
  row_open_table[rb].direction = new_direction;
  row_open_table[rb].stride = col - row_open_table[rb].position;
  row_open_table[rb].position = col;
  row_open_table[rb].access_count++;

  //fmt::print("Updating row confidence with address : {} direction: {} confidence: {}\n",addr,row_entry->direction,row_entry->confidence);
}

uint8_t dram_prefetch_buffer_spp::modify_confidence(uint8_t conf, uint8_t amnt, bool increment) {
  if(increment) {
    if(conf + amnt >= CONF_MAX)
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

void dram_prefetch_buffer_spp::increase_confidence(champsim::address addr, uint8_t amnt, bool cond) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
    if(!cond || (col > entry->position && col <= entry->position + 2))
      entry->confidence = modify_confidence(entry->confidence,amnt,true);
    else if(cond)
      entry->confidence = modify_confidence(entry->confidence,DEMAND_NCONF,false);
    row_walker_table[rb].fill(entry.value());
  }
}

void dram_prefetch_buffer_spp::decrease_confidence(champsim::address addr, uint8_t amnt) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  row_walker rw{row,col,col};

  auto entry = row_walker_table[rb].check_hit(rw);
  if(entry.has_value()) {
      entry->confidence = modify_confidence(entry->confidence,amnt,false);
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
  return DEPTHS.at(threshold - 1);
}

bool dram_prefetch_buffer_spp::add_to_pf_issuer(champsim::address addr, std::size_t depth, uint32_t cpu) {
  if(prefetch_issuer.size() >= PFI_ENTRIES)
    return false;
  prefetch_issuer.push_back(prefetch_issuer_entry{addr,depth,cpu});
  return true;
}

void dram_prefetch_buffer_spp::do_pf_issue() {
  std::size_t search_distance = std::min(PFI_FORWARD,prefetch_issuer.size());
  std::size_t search_candidate = pf_issue_pos % search_distance;
  pf_issue_pos = (pf_issue_pos + 1) % PFI_FORWARD;
  
  prefetch_issuer_entry to_pf = prefetch_issuer.at(search_candidate);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(to_pf.base);

  if(!filter.check(to_pf.base,intern_->current_cycle(),false)) {
    bool success = prefetch_line(to_pf.base,true,to_pf.cpu,col%2 == 0 ? BUFFER_ID : NEXT_LINE_ID, false, false);
    if(success) {
      filter.check(to_pf.base,intern_->current_cycle(),true);
      prefetch_issuer.at(search_candidate).base = compose_base_and_column(to_pf.base,col + 1);
      prefetch_issuer.at(search_candidate).steps -= 1;
      if(prefetch_issuer.at(search_candidate).steps == 0) {
        prefetch_issuer.erase(prefetch_issuer.begin() + search_candidate);
      }
    }
  }
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
      if(col > prev_col + 1 /*&& (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()) < intern_->get_mshr_size()*0.7*/) {
        //fmt::print("Filling backwards gap at row: {} rb: {} between columns:{} - {}\n",row,rb,prev_col + 1,col - 1);
        int amount_to_prefetch = std::min(depth,intern_->get_mshr_size() - (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()));
        int prefetched_so_far = 0;
        bool all_success = true;
        for(int i = (int)col - 1; i > prev_col && i > (int)col - amount_to_prefetch; i--) {
          bool success = true;
          if(!filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),false)) {
            success = prefetch_line(compose_base_and_column(addr,i),true,cpu,i%2 == 0 ? BUFFER_ID : NEXT_LINE_ID,false,false);
            if(success) {
              filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),true);
              //rw.confidence = modify_confidence(rw.confidence,1,false);
              if(!intern_->warmup)
                backward_buffer_issued += 1;
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
      if(col >= rw.opened_at /*&& (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()) < intern_->get_mshr_size()*0.7*/) {
        int amount_to_prefetch = std::min(depth,intern_->get_mshr_size() - (intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back()));
        int prefetched_so_far = 0;
        //fmt::print("Prefetching forwards at row: {} rb: {} between columns:{} - {}\n",row,rb,col + 1,col + amount_to_prefetch > 63 ? 63 : col + amount_to_prefetch);
        for(int i = col + 1; i < 64 && i <= col + amount_to_prefetch; i++) {
          bool success = true;
          if(!filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),false)) {
            success = prefetch_line(compose_base_and_column(addr,i),true,cpu,i%2 == 0 ? BUFFER_ID : NEXT_LINE_ID,false,false);
            if(success) {
              filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),true);
              //rw.confidence = modify_confidence(rw.confidence,1,false);
              if(!intern_->warmup)
                forward_buffer_issued += 1;
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

void dram_prefetch_buffer_spp::issue_row_prefetch(champsim::address addr, uint32_t cpu) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);

  assert(row_open_table[rb].row == row);

  if(row_open_table[rb].confidence > 25) {
    auto history = row_history_table.check_hit(row_history{row});
    auto stride = row_open_table[rb].stride;
    auto direction = row_open_table[rb].direction;
    auto number = 1;

    //default to grabbing double the next used entries if we have no history to bet on
    auto access_count = row_open_table[rb].access_count;
    if(history.has_value()) {
      //check how much to pull in
      number += ((intern_->NUM_SET * intern_->NUM_WAY) - ((accesses_so_far / row_interval_table[rb].activations)*row_interval_table.size()*2)) / (history->access_count);
      access_count = history->access_count;
    }
    number = std::min(number,4);
    number = std::max(1,number);

    //issue prefetches
    if(direction == FORWARD) {
      auto end = col + (number*stride*access_count) > 63 ? 63 : col + (number*stride*access_count);
      fmt::print("Issuing prefetch from addr: {} col: {} to col: {} stride: {} num: {} count: {}\n",addr,col,end,stride,number,access_count);
      for(int i = col + stride; i <= end; i += stride) {
        if(!filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),true))
          prefetch_line(compose_base_and_column(addr,i),true,cpu,0);
      }
    }
    else if(direction == BACKWARD) {
      auto end = col < (number*stride*access_count) ? 0 : col - (number*stride*access_count);
      fmt::print("Issuing prefetch from addr: {} col: {} to col: {} stride: {}\n",addr,col,end,stride);
      for(int i = col - stride; i >= end; i -= stride) {
        if(!filter.check(compose_base_and_column(addr,i),intern_->current_cycle(),true))
          prefetch_line(compose_base_and_column(addr,i),true,cpu,0);
      }
    }
  }
}

champsim::address dram_prefetch_buffer_spp::compose_base_and_column(champsim::address base, uint64_t column) {
  //1. iterate through all column bits in the base
  //2. set each bit to the matching bits in the column
  uint64_t base_temp = base.to<uint64_t>();
  std::vector<uint64_t> column_bits;
  //8GB
  if(NUM_CPUS == 1)
    column_bits = {7,12,13,14,15,16};
  //32GB
  else //multicore, 32GB of RAM
    column_bits = {7,13,14,15,16,17};

  for(std::size_t i = 0; i < column_bits.size(); i++) {
    if(column & (1ull << i))
      base_temp |= 1ull << column_bits[i];
    else
      base_temp &= ~(1ull << column_bits[i]);
  }
  return champsim::address{base_temp};
}


void dram_prefetch_buffer_spp::prefetcher_initialize() {

  champsim::data::bytes row_history_table_size{RHT_SETS * RHT_WAYS * row_history::bytes};
  fmt::print("SIZE OF ROW HISTORY TABLE: {}\n",champsim::data::kibibytes{row_history_table_size});

  for(int i = 0; i < MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers(); i++) {
    row_walker_table.push_back(champsim::msl::lru_table<row_walker,row_walker_set,row_walker_way>{RW_SETS,RW_WAYS});
  }

  row_open_table.resize(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers());
  row_interval_table.resize(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers());
}

uint32_t dram_prefetch_buffer_spp::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  accesses_so_far += 1;
  //if(prefetch)
  //  filled[cpu]++;
  
  if(evicted_addr != champsim::address{} && useless) {
    decrease_confidence(evicted_addr,USELESS_NCONF);
    if(!intern_->warmup)
      useless_tallied += 1;
  }

  return metadata_in;
}

void dram_prefetch_buffer_spp::prefetcher_cycle_operate() {
  if((intern_->current_cycle() + 1) % usefulness_update_period == 0) {
    for (auto& cp : useful) {
      //uint64_t usfl = cp.second;
      //uint64_t fill = filled[cp.first];
      //intern_->report_prefetch_usefulness(cp.first, (usfl+1)/(double)(fill+1));
      //useful[cp.first] = 0;
      //filled[cp.first] = 0;
    }
  }
}

void dram_prefetch_buffer_spp::prefetcher_final_stats() {
  fmt::print("Prefetches Forward: {} Prefetches Backwards: {} Useful: {} Useless: {} Next Line: {} Opened Rows: {} Dropped: {}\n",forward_buffer_issued, backward_buffer_issued, useful_tallied, useless_tallied, next_line_issued, opened_rows,prefetches_dropped);

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
