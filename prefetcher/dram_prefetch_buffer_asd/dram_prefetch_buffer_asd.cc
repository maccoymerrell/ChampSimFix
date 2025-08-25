#include "dram_prefetch_buffer_asd.h"
#include "../spp_ppf/spp_ppf.h"

uint32_t dram_prefetch_buffer_asd::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in, uint32_t metadata_hit)
{
  if(addr == champsim::address{})
    return metadata_in;

  //if(cache_hit == 0 && metadata_in != ASD_ID && metadata_in != BUFFER_ID)
  //  update_ip_thrash(IP_THRASH_MISS,ip,cpu);
  ///else
  if(metadata_in != ASD_ID && metadata_in != BUFFER_ID)
    update_ip_thrash(IP_THRASH_HIT,ip,cpu);

  
  long set = addr.slice(champsim::dynamic_extent{intern_->OFFSET_BITS, champsim::lg2(intern_->NUM_SET)}).to<long>();
  if(useful_prefetch) {
    update_ip_blacklist(IP_BLACKLIST_USEFUL,ip,cpu,0,0,0);
    //useful[cpu]++;
    //fmt::print("Increasing confidence via useful prefetch\n");
    if(metadata_hit == ASD_ID) {
        global_useful_asd[cpu]++;
    }
    else if (metadata_hit == BUFFER_ID)
      global_useful_buffer[cpu]++;
    
    if(metadata_hit == BUFFER_ID) {
      increase_confidence_useful(addr);
      if(!intern_->warmup)
        useful_tallied += 1;
    }
  }

  bool is_blacklisted = ENABLE_IP_BLACKLIST && is_ip_blacklisted(ip,cpu);
  if(is_blacklisted && !intern_->warmup)
    conflict_filters += 1;


  if(cache_hit == 0) {
    //if(metadata_in != ASD_ID && metadata_in != BUFFER_ID)
    //  update_ip_blacklist(IP_BLACKLIST_DEMAND,ip,cpu,0,0,0);
    if(metadata_in == ASD_ID) {
      //fmt::print("Triggering walk based on next_line prefetch\n");
    }
    if(metadata_in != ASD_ID) {
      //fmt::print("Increasing confidence via demand miss\n");
      increase_confidence_stream(addr, type == access_type::PREFETCH);
    }

    if(type == access_type::PREFETCH && ENABLE_IP_THRASH_DROP) {
      if(is_ip_thrashing(ip,cpu)) {
        intern_->drop_prefetch_access(addr);
        if(!intern_->warmup) {
          upper_level_thrash_drops++;
        }
        return metadata_in;
      }
    }
    /*
    if(type == access_type::PREFETCH && metadata_in == spp_ppf::SPP_LLC_TARGET_ID) {
      if(should_drop_prefetch(addr,cpu)) {
        intern_->drop_prefetch_access(addr);
        if(!intern_->warmup)
          prefetches_dropped++;
        return metadata_in;
      }
    }
    */
    ASD_Modules.at(cpu).add_to_pagemap(addr);
    //pass blacklisted flag to buffer prefetcher
    update_walker(addr,cpu,ip,is_blacklisted);

  } else {
    ASD_Modules.at(cpu).add_to_pagemap(addr);
  }

  //don't allow ASD to trigger on blacklisted ips
  if(is_blacklisted)
    return metadata_in;

  //next line x4 if we have the MSHR capacity
  if(metadata_in != ASD_ID && !DISABLE_ASD) {
      ASD_Modules.at(cpu).set_histogram_factor(variable_asd_thresh[cpu]);

    auto [depth,stride] = ASD_Modules.at(cpu).get_prefetch_depth(addr, ip);
    //modulate depth according to MSHR occupancy
    if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.9 * intern_->get_mshr_size())
      depth = std::min(asd::MAX_PREFETCH / 8, depth);
    else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.7 * intern_->get_mshr_size())
      depth = std::min(asd::MAX_PREFETCH / 4, depth);
    else if(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() > 0.5 * intern_->get_mshr_size())
      depth = std::min(asd::MAX_PREFETCH / 2, depth);
  
    for(int i = 1; i <= depth; i++) {
      champsim::address pf_addr = champsim::address{champsim::block_number{addr} + i*stride};
      if(champsim::page_number{pf_addr} != champsim::page_number{addr})
        continue;
      //fmt::print("Issuing prefetch for address: {}\n",pf_addr);
      //filter out redundant prefetches
      bool pm = ASD_Modules.at(cpu).check_pagemap(pf_addr);
      bool success = true;
      if(!pm) {
        success = prefetch_line(pf_addr,true,cpu,ip,ASD_ID,SKIP_TAG_CHECK_ASD,true);
      } else if (!intern_->warmup) {
        prefetches_filtered++;
      }
      if(success) {
        //log_ip(pf_addr,ip,cpu);
        ASD_Modules.at(cpu).add_to_pagemap(pf_addr);
        //buffer isn't going to trigger off tag check, invoke ourselves
        if(SKIP_TAG_CHECK_ASD) {
          prefetcher_cache_operate(pf_addr,ip,cpu,false,false,access_type::PREFETCH,ASD_ID,0);
        }
      }
    }
  }
  
  return metadata_in;
}

bool dram_prefetch_buffer_asd::should_drop_prefetch(champsim::address addr, uint32_t cpu) {
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

void dram_prefetch_buffer_asd::update_row_open_table(champsim::address addr) {
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
bool dram_prefetch_buffer_asd::is_row_open(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  return row_open_table[rb].row == row;
}


uint8_t dram_prefetch_buffer_asd::modify_confidence(uint8_t conf, uint8_t amnt, bool increment) {
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

void dram_prefetch_buffer_asd::increase_confidence_stream(champsim::address addr, bool prefetch) {
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

void dram_prefetch_buffer_asd::increase_confidence_useful(champsim::address addr) {
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

void dram_prefetch_buffer_asd::increase_confidence_opened(champsim::address addr) {
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

void dram_prefetch_buffer_asd::decrease_confidence_useless(champsim::address addr) {
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

std::size_t dram_prefetch_buffer_asd::get_depth(uint8_t conf, uint32_t cpu) {
  uint8_t threshold = 0;
  uint8_t new_conf = conf < variable_buffer_conf[cpu] ? 0 : conf - variable_buffer_conf[cpu];
  for (auto thres : THRESH) {
    threshold++;
    if(new_conf < thres)
      break;
  }
  //if(new_conf != conf)
  //  fmt::print("Old Confidence: {} Modifier: {} New Confidence: {} Depth: {}\n",conf,variable_buffer_conf[cpu],new_conf,DEPTHS.at(threshold-1));
  return DEPTHS.at(threshold - 1);
}

void dram_prefetch_buffer_asd::update_walker(champsim::address addr, uint32_t cpu, champsim::address ip, bool is_blacklisted) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);

  //update row open table
  bool is_open = is_row_open(addr);
  if(!is_open)
    update_row_open_table(addr);

  //abort, ip blacklisted
  if(is_blacklisted)
    return;

  //increase confidence on row open
  if(!is_open)
    increase_confidence_opened(addr);

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
          bool pm = ASD_Modules.at(cpu).check_pagemap(compose_base_and_column(addr,i));
          if(!pm) {
            success = prefetch_line(compose_base_and_column(addr,i),true,cpu,ip,BUFFER_ID,SKIP_TAG_CHECK_BUFFER,false);
            if(success) {
              //log_ip(compose_base_and_column(addr,i),ip,cpu);
              ASD_Modules.at(cpu).add_to_pagemap(compose_base_and_column(addr,i));
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


champsim::address dram_prefetch_buffer_asd::compose_base_and_column(champsim::address base, uint64_t column) {
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


void dram_prefetch_buffer_asd::prefetcher_initialize() {

  //fmt::print("SIZE OF ROW HISTORY TABLE: {}\n",champsim::data::kibibytes{row_history_table_size});
  	// Determine set sampling rate
	if(intern_->NUM_SET >= 1024) { // 1 in 32
		SET_SAMPLE_RATE = 32;
	} else if(intern_->NUM_SET >= 256) { // 1 in 16
		SET_SAMPLE_RATE = 16;
	} else if(intern_->NUM_SET >= 64) { // 1 in 8
		SET_SAMPLE_RATE = 8;
	} else if(intern_->NUM_SET >= 8) { // 1 in 4
		SET_SAMPLE_RATE = 4;
	} else {
		assert(false); // Not enough sets to sample for set dueling
	}
	assert(intern_->NUM_SET >= SET_SAMPLE_RATE); // Guarantee at least one sampled set

  for(int i = 0; i < MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers(); i++) {
    row_walker_table.push_back(champsim::msl::lru_table<row_walker,row_walker_set,row_walker_way>{RW_SETS,RW_WAYS});
  }

  //initialize ip tracking and blacklist structs
  for(int i = 0; i < NUM_CPUS; i++) {
    ip_blacklist_table.push_back(champsim::msl::lru_table<ip_blacklist_counter,ip_blacklist_set,ip_blacklist_way>{IP_BLACKLIST_SETS,IP_BLACKLIST_WAYS});
    ip_thrash_table.push_back(champsim::msl::lru_table<ip_thrash_counter,ip_thrash_set,ip_thrash_way>{IP_THRASH_SETS,IP_THRASH_WAYS});
    //ip_tracking_table.push_back(champsim::msl::lru_table<ip_tracking_table_entry,ip_tracking_set,ip_tracking_way>{IP_TRACKING_SETS,IP_TRACKING_WAYS});
    global_usefulness_buffer.push_back(1.0);
    global_usefulness_asd.push_back(1.0);
  }

  row_open_table.resize(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers());

  //find column bits
  for(int i = 0; i < 64; i ++) {
    if(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(champsim::address{1ul << i}) != 0)
      column_bits.push_back(i);
  }
  fmt::print("[{}] Initialized Buffer-ASD, Sample Rate: {}, Column Bits are: {} Conflict Filtering: {} Thrash Dropping: {}\n",intern_->NAME,SET_SAMPLE_RATE,fmt::join(column_bits, ","),ENABLE_IP_BLACKLIST,ENABLE_IP_THRASH_DROP);


  num_bins = (4096 / BLOCK_SIZE);
  for(int i = 0; i < NUM_CPUS; i++) {
    ASD_Modules.emplace_back(asd::ASD_Module(num_bins));
  }
  for(int i = 0; i < NUM_CPUS; i++) {
    for(int j = 0; j < num_bins; j++) {
      ASD_Modules.at(i).pf_depths.at(j) = 0;
      ASD_Modules.at(i).pf_strides.at(j) = 0;
    }
  }
  for(int i = 0; i < NUM_CPUS; i++) {
    global_useful_buffer.push_back(0);
    global_useful_asd.push_back(0);
    global_useless_buffer.push_back(0);
    global_useless_asd.push_back(0);
    variable_buffer_conf.push_back(BUFFER_MIN_NCONF);
    variable_asd_thresh.push_back(ASD_MAX_THRESH);
    epoch_counter.push_back(0);
  }
}

uint32_t dram_prefetch_buffer_asd::prefetcher_cache_fill(champsim::address addr, champsim::address ip, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict)
{
  if(prefetch)
    epoch_counter[cpu]++;
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
  if(metadata_in != BUFFER_ID && metadata_in != ASD_ID)
    update_ip_thrash(IP_THRASH_FILL,ip,cpu);

  //catch generic conflicts
  if(prefetch && useless) {
      //conflict happened here
      //champsim::address ip = get_ip(addr, cpu);
      if(ip != champsim::address{})
        update_ip_blacklist(IP_BLACKLIST_HARMFUL,ip,cpu,cpu_evict,metadata_in,metadata_evict);
      else if(!intern_->warmup) {
        //fmt::print("IP not found for Address: {} CPU: {} Cycle: {}\n",addr,cpu,intern_->current_cycle());
        ip_not_found += 1;
      }
  }
  if(metadata_evict == BUFFER_ID && useless && cpu_evict != NUM_CPUS)
    global_useless_buffer[cpu_evict]++;
  else if (metadata_evict == ASD_ID && useless && metadata_in == BUFFER_ID) {
    global_useless_buffer[cpu]++;
  }
  else if(metadata_evict == ASD_ID && useless && cpu_evict != NUM_CPUS) {
      global_useless_asd[cpu_evict]++;
  }
  
  if(evicted_addr != champsim::address{}) {
    for(int i = 0; i < NUM_CPUS; i++)
      ASD_Modules.at(i).remove_from_pagemap(evicted_addr);
  }

  return metadata_in;
}

void dram_prefetch_buffer_asd::prefetcher_cycle_operate() {
  for(int i = 0; i < NUM_CPUS; i++) {
    if(epoch_counter[i] >= usefulness_measure_epoch) {
    
      global_usefulness_buffer[i] = (global_useless_buffer[i] <= 100 || (global_useful_buffer[i] + global_useless_buffer[i] <= 100)) ? 1.0 : (global_useful_buffer[i]) / (double)(global_useless_buffer[i] + global_useful_buffer[i]);
      global_usefulness_asd[i] = (global_useless_asd[i] <= 100 || (global_useful_asd[i] + global_useless_asd[i] <= 100)) ? 1.0 : (global_useful_asd[i]) / (double)(global_useless_asd[i] + global_useful_asd[i]);

      if(epochs % usefulness_measure_print_interval == 0)
        fmt::print("CPU: {} Buffer usefulness: {} ({}/{}) ASD usefulness: {} ({}/{})\n",i,global_usefulness_buffer[i],global_useful_buffer[i],global_useless_buffer[i],global_usefulness_asd[i],global_useful_asd[i],global_useless_asd[i]);
      global_useless_buffer[i] = 0;
      global_useful_buffer[i] = 0;
      global_useless_asd[i] = 0;
      global_useful_asd[i] = 0;
      
      if(global_usefulness_buffer[i] > TARGET_BUFFER_ACCURACY)
        variable_buffer_conf[i] = variable_buffer_conf[i] < BUFFER_NCONF_STEP + BUFFER_MIN_NCONF ? BUFFER_MIN_NCONF : variable_buffer_conf[i] - BUFFER_NCONF_STEP;
      else
        variable_buffer_conf[i] = variable_buffer_conf[i] + BUFFER_NCONF_STEP > BUFFER_MAX_NCONF ? BUFFER_MAX_NCONF : variable_buffer_conf[i] + BUFFER_NCONF_STEP;

      if(global_usefulness_asd[i] > TARGET_ASD_ACCURACY)
        variable_asd_thresh[i] = std::min(variable_asd_thresh[i] + ASD_THRESH_STEP, ASD_MAX_THRESH);
      else if(global_usefulness_asd[i] < 0.75) {
        variable_asd_thresh[i] = variable_asd_thresh[i] - ASD_THRESH_STEP;
        if(variable_asd_thresh[i] < ASD_MIN_THRESH)
          variable_asd_thresh[i] = ASD_MIN_THRESH;
      }
      if(epochs % usefulness_measure_print_interval == 0)
        fmt::print("\tBuffer confidence modifier: {} asd thresh: {}\n",variable_buffer_conf[i], variable_asd_thresh[i]);
      
      fmt::print("Blacklist table CPU: {}\n",i);
      for(auto &block : ip_blacklist_table[i].get_contents()) {
        if(block.last_used != 0)
          fmt::print("\t{} : {}\n",block.data.ip,block.data.counter);
      }
      fmt::print("Thrash table CPU: {}\n",i);
      for(auto &block : ip_thrash_table[i].get_contents()) {
        if(block.last_used != 0)
          fmt::print("\t{} : {}\n",block.data.ip,block.data.counter);
      }
      epochs += 1;
      epoch_counter[i] = 0;
    }
  }
  if(blacklist_counter >= blacklist_reset_interval) {
    reset_ip_blacklist();
    blacklist_counter = 0;
  }
  blacklist_counter++;
}

void dram_prefetch_buffer_asd::prefetcher_final_stats() {
  fmt::print("Prefetches Forward: {} Prefetches Backwards: {} Useful: {} Useless: {} Next Line: {} Opened Rows: {} Dropped: {} [{}, {}] Prefetch-Prefetch Thrashes: {} Rejected: {} Filtered: {}\n",forward_buffer_issued, backward_buffer_issued, useful_tallied, useless_tallied, next_line_issued, opened_rows,prefetches_dropped,prefetches_dropped_too_far_back,prefetches_dropped_too_far_forward,pp_thrashes,prefetches_rejected, prefetches_filtered);
  fmt::print("Confidence Types: Useful: {}, Useless: {}, Stream: ({},{}), Random: ({},{}), Row ACT: {}\n",conf_useful,conf_useless,conf_demand_stream,conf_prefetch_stream,conf_demand_random,conf_prefetch_random,conf_act);
  fmt::print("Conflict filters: {} IPs not found: {}\n",conflict_filters,ip_not_found);
  fmt::print("Thrash drops: {}\n",upper_level_thrash_drops);
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

  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("Buffer for Core: {} Usefulness: {}\n",i,global_usefulness_buffer[i]);
    fmt::print("ASD for Core: {} Usefulness: {}\n",i,global_usefulness_asd[i]);
    ASD_Modules.at(i).print_stats();
  }
}

void dram_prefetch_buffer_asd::reset_ip_blacklist() {
  for(int i = 0; i < NUM_CPUS; i++) {
    ip_blacklist_table[i].flush();
  }
}

bool dram_prefetch_buffer_asd::is_ip_blacklisted(champsim::address ip, uint32_t cpu) {
  ip_blacklist_counter bc{ip};

  if(ip == champsim::address{})
    return false;

  auto entry = ip_blacklist_table[cpu].check_hit(bc);
  if(entry.has_value()) {
      return entry->counter >= IP_BLACKLIST_THRESH;
  }
  return false;
} 

void dram_prefetch_buffer_asd::update_ip_blacklist(std::size_t collision_type, champsim::address ip, uint32_t cpu, uint32_t victim_cpu, uint32_t target_prefetcher, uint32_t victim_prefetcher) {
  ip_blacklist_counter bc{ip};

  if(collision_type == IP_BLACKLIST_USEFUL) {
    ip_blacklist_table[cpu].invalidate(bc);
    return;
  }
  auto entry = ip_blacklist_table[cpu].check_hit(bc);

  //blacklist demand, decrease counter by 1
  if(collision_type == IP_BLACKLIST_DEMAND) {
    if(entry.has_value()) {
      if(entry->counter <= 1) {
        ip_blacklist_table[cpu].invalidate(bc);
        return;
      } else {
        bc.counter = entry->counter - 1;
        ip_blacklist_table[cpu].fill(bc);
        return;
      }
    } else {
      return;
    }
  }

  //blacklist harmful, increase counter by 1 up to threshold 
  if (collision_type == IP_BLACKLIST_HARMFUL) {
    bool same_core = cpu == victim_cpu;
    bool likely_to_be_used = target_prefetcher == BUFFER_ID ? global_usefulness_buffer[cpu] > 0.5 : global_usefulness_asd[cpu] > 0.5;
    bool victim_likely_to_be_used = victim_prefetcher == BUFFER_ID ? global_usefulness_buffer[victim_cpu] > 0.5 : global_usefulness_asd[victim_cpu] > 0.5;
    if(same_core)
      return;
    if(!likely_to_be_used && !victim_likely_to_be_used)
      return;
    if(entry.has_value())
      bc.counter = entry->counter >= IP_BLACKLIST_THRESH ? IP_BLACKLIST_THRESH : entry->counter + 1;
    else
      bc.counter += 1;
    ip_blacklist_table[cpu].fill(bc);
    return;
  }
}

bool dram_prefetch_buffer_asd::is_ip_thrashing(champsim::address ip, uint32_t cpu) {
  ip_thrash_counter bc{ip};

  if(ip == champsim::address{})
    return false;

  auto entry = ip_thrash_table[cpu].check_hit(bc);
  if(entry.has_value()) {
      return entry->counter >= IP_THRASH_COUNTER_MAX;
  }
  return false;
} 

void dram_prefetch_buffer_asd::update_ip_thrash(std::size_t thrash_type, champsim::address ip, uint32_t cpu) {
  ip_thrash_counter bc{ip};

  if(thrash_type == IP_THRASH_MISS) {
    ip_thrash_table[cpu].invalidate(bc);
    return;
  }
  auto entry = ip_thrash_table[cpu].check_hit(bc);

  if(thrash_type == IP_THRASH_HIT) {
    if(entry.has_value()) {
      if(entry->counter <= IP_THRASH_COUNTER_DOWN) {
        ip_thrash_table[cpu].invalidate(bc);
        return;
      } else {
        bc.counter = entry->counter - IP_THRASH_COUNTER_DOWN;
        ip_thrash_table[cpu].fill(bc);
        return;
      }
    } else {
      return;
    }
  }

  //thrash harmful, increase counter by 1 up to threshold 
  if (thrash_type == IP_THRASH_FILL) {
    if(entry.has_value())
      bc.counter = entry->counter + IP_THRASH_COUNTER_UP >= IP_THRASH_COUNTER_MAX ? IP_THRASH_COUNTER_MAX : entry->counter + IP_THRASH_COUNTER_UP;
    else
      bc.counter += IP_THRASH_COUNTER_UP >= IP_THRASH_COUNTER_MAX ? IP_THRASH_COUNTER_MAX : IP_THRASH_COUNTER_UP;
    ip_thrash_table[cpu].fill(bc);
    return;
  }
}
