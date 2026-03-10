#include "orap_ppf.h"

void orap_ppf::add_to_regionmap(champsim::address addr, uint32_t cpu) {
  uint64_t page_num = addr.to<uint64_t>() >> 12;
  uint64_t offset = (addr.to<uint64_t>() >> LOG2_BLOCK_SIZE) & (PAGE_MAP_SIZE - 1);
  page_map pm{page_num};
  auto entry = region_maps.at(cpu).check_hit(pm);
  if(entry.has_value()) {
    entry->bits.at(offset) = 1;
    region_maps.at(cpu).fill(entry.value());
  } else {
    pm.bits.at(offset) = 1;
    region_maps.at(cpu).fill(pm);
  }
}

bool orap_ppf::check_regionmap(champsim::address addr, uint32_t cpu) {
  uint64_t page_num = addr.to<uint64_t>() >> 12;
  uint64_t offset = (addr.to<uint64_t>() >> LOG2_BLOCK_SIZE) & (PAGE_MAP_SIZE - 1);
  page_map pm{page_num};
  auto entry = region_maps.at(cpu).check_hit(pm);
  if(entry.has_value()) {
    return entry->bits.at(offset) != 0;
  }
  return false;
}

void orap_ppf::remove_from_regionmap(champsim::address addr, uint32_t cpu) {
  uint64_t page_num = addr.to<uint64_t>() >> 12;
  uint64_t offset = (addr.to<uint64_t>() >> LOG2_BLOCK_SIZE) & (PAGE_MAP_SIZE - 1);
  page_map pm{page_num};
  auto entry = region_maps.at(cpu).check_hit(pm);
  if(entry.has_value()) {
    entry->bits.at(offset) = 0;
    region_maps.at(cpu).fill(entry.value());
  }
}

uint32_t orap_ppf::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in, uint32_t metadata_hit)
{
  if(addr == champsim::address{})
    return metadata_in;

  //if(metadata_in == BUFFER_ID)
  if(cache_hit != 0)
    clear_from_pq(addr,cpu);
  
  if(cache_hit == 0 && type == access_type::PREFETCH && (metadata_in == PPF_ID || metadata_in == BUFFER_ID))
    pf_issued_to_dram_last_epoch[cpu]++;

  if(cache_hit == 0 && type == access_type::PREFETCH && (metadata_in == PPF_ID || metadata_in == BUFFER_ID)) {
    //shouldn't need to check demand table, 
    prefetch_sample_table.fill(sample_table_entry{addr,intern_->current_cycle()});
  } else if(cache_hit == 0 && (type == access_type::LOAD || type == access_type::PREFETCH)) {
    //check pf table to make sure this isn't a demand merging into a prefetch in the mshr
    if(!prefetch_sample_table.check_hit(sample_table_entry{addr,0}).has_value())
      demand_sample_table.fill(sample_table_entry{addr,intern_->current_cycle()});
  }

  if(metadata_in == BUFFER_ID)
    return metadata_in;

  if(useful_prefetch) {
    update_ip_blacklist(IP_BLACKLIST_USEFUL,ip,cpu,0,0,0);

    if(metadata_hit == PPF_ID) {
        global_useful_ppf[cpu]++;
    }
    else if (metadata_hit == BUFFER_ID)
      global_useful_buffer[cpu]++;
    
    if(metadata_hit == BUFFER_ID) {
      increase_confidence_useful(ip,addr,cpu,false);
      if(!intern_->warmup)
        useful_tallied += 1;
    } else if(metadata_hit == PPF_ID) {
      increase_confidence_useful(ip,addr,cpu,true);
    }
  }

  bool is_blacklisted = ENABLE_IP_BLACKLIST && is_ip_blacklisted(ip,cpu);
  if(is_blacklisted && !intern_->warmup)
    conflict_filters += 1;

  //prevent prefetchers from grabbing a recently accessed entry
  add_to_regionmap(addr, cpu);

  if(cache_hit == 0 || !TRIGGER_BUFFER_ON_MISS)
    update_walker(addr,cpu,ip,is_blacklisted);


  //don't allow PPF to trigger on blacklisted ips
  if(is_blacklisted)
    return metadata_in;

  //next line x4 if we have the MSHR capacity
  if(metadata_in != PPF_ID && !DISABLE_PPF) {
    PPF_Modules.at(cpu).do_prefetch(addr, ip, cpu, cache_hit, useful_prefetch, type, PPF_ID);
  }
  
  return metadata_in;
}

bool orap_ppf::update_row_open_table(champsim::address addr) {
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);

  //row act, update last_closed time for closing rowbuffer
  if(row_open_table[rb].row != row) {
    if(!intern_->warmup)
      opened_rows += 1;
    row_open_table[rb].row = row;
    return true;
  }
  return false;

}


uint8_t orap_ppf::modify_confidence(uint8_t conf, uint8_t amnt, bool increment) {
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

uint8_t orap_ppf::modify_confidence(uint8_t conf, double factor) {
  double adjusted_conf = conf * factor;
  adjusted_conf = std::max(0.0,adjusted_conf);
  adjusted_conf = std::min((double)CONF_MAX,adjusted_conf);
  return (uint8_t)adjusted_conf;
}

void orap_ppf::increase_confidence_useful(champsim::address ip, champsim::address addr, uint32_t cpu, bool coprefetch) {
  uint16_t ip_hash = get_hash(ip.to<uint64_t>());
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  ip_tracker it{ip_hash};

  auto entry = ip_tracker_table[cpu].check_hit(it);
  if(entry.has_value()) {

    if(coprefetch)
      entry->coprefetch_confidence_useful_counter = entry->coprefetch_confidence_useful_counter + ASD_IP_COUNTER_USEFUL_INCR > ASD_IP_COUNTER_USEFUL_MAX ? ASD_IP_COUNTER_USEFUL_MAX : entry->coprefetch_confidence_useful_counter + ASD_IP_COUNTER_USEFUL_INCR;
    else
      entry->confidence_useful_counter = entry->confidence_useful_counter + BUFFER_IP_COUNTER_USEFUL_INCR > BUFFER_IP_COUNTER_USEFUL_MAX ? BUFFER_IP_COUNTER_USEFUL_MAX : entry->confidence_useful_counter + BUFFER_IP_COUNTER_USEFUL_INCR;

    bool eligible_for_promote = (coprefetch && CONF_COUNTER_ASD_IP && entry->coprefetch_confidence_useful_counter >=ASD_IP_COUNTER_USEFUL_MAX);
    eligible_for_promote |= (!coprefetch && CONF_COUNTER_BUFFER_IP && entry->confidence_useful_counter >= BUFFER_IP_COUNTER_USEFUL_MAX);
    eligible_for_promote |= (coprefetch && !CONF_COUNTER_ASD_IP) || (!coprefetch && !CONF_COUNTER_BUFFER_IP);
    if(!intern_->warmup && eligible_for_promote)
      conf_useful++;

    int amount_to_increase = USEFUL_CONF_IP - (USEFUL_NCONF*(entry->confidence / CONF_MAX));


    if(coprefetch && eligible_for_promote) {
      entry->coprefetch_confidence = modify_confidence(entry->coprefetch_confidence,amount_to_increase,true);
      entry->coprefetch_confidence_useful_counter = 0;
    } else if(eligible_for_promote) {
      entry->confidence = modify_confidence(entry->confidence,amount_to_increase,true);
      entry->confidence_useful_counter = 0;
    }
    ip_tracker_table[cpu].fill(entry.value());
  } else if(!intern_->warmup) {
    orphaned_ip_lookups++;
  }

  if(coprefetch)
    return;
  //increase confidence on row
  row_walker rw{row};

  auto row_entry = row_walker_table[cpu].check_hit(rw);
  if(row_entry.has_value()) {
    int amount_to_increase = USEFUL_CONF_ROW - (USEFUL_NCONF*(row_entry->confidence / CONF_MAX));
    row_entry->confidence_useful_counter = row_entry->confidence_useful_counter + BUFFER_ROW_COUNTER_USEFUL_INCR > BUFFER_ROW_COUNTER_USEFUL_MAX ? BUFFER_ROW_COUNTER_USEFUL_MAX : row_entry->confidence_useful_counter + BUFFER_ROW_COUNTER_USEFUL_INCR;
    bool eligible_for_promote = !CONF_COUNTER_BUFFER_ROW || (row_entry->confidence_useful_counter >= BUFFER_ROW_COUNTER_USEFUL_MAX);
    if(!intern_->warmup && eligible_for_promote)
      conf_useful++;
    
    if(eligible_for_promote) {
      row_entry->confidence = modify_confidence(row_entry->confidence,amount_to_increase,true);
      row_entry->confidence_useful_counter = 0;
    }

    row_walker_table[cpu].fill(row_entry.value());
  }
}

void orap_ppf::increase_confidence_opened(champsim::address ip, champsim::address addr, uint32_t cpu, bool coprefetch) {
  //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
  assert(!CONF_COUNTER_BUFFER_IP || !ACT_CONF_ON_IP || ACT_CONF == 0);
  assert(!CONF_COUNTER_ASD_IP || !ACT_CONF_ON_IP || ACT_CONF == 0);
  assert(!CONF_COUNTER_BUFFER_ROW || !ACT_CONF_ON_ROW || ACT_CONF == 0);

  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  
  if(ACT_CONF_ON_IP) {
    uint16_t ip_hash = get_hash(ip.to<uint64_t>());
    ip_tracker it{ip_hash};

    auto entry = ip_tracker_table[cpu].check_hit(it);
    if(entry.has_value()) {

      //amount to increase should be dependent on current entry confidence
      if(entry->confidence < MAX_ACT_CONF_LEVEL) {
        if(coprefetch)
          entry->coprefetch_confidence = modify_confidence(entry->coprefetch_confidence,ACT_CONF,true);
        else
          entry->confidence = modify_confidence(entry->confidence,ACT_CONF,true);
        ip_tracker_table[cpu].fill(entry.value());
        if(!intern_->warmup)
          conf_act++;
      }
    }
  }

  //increase confidence on row
  if(ACT_CONF_ON_ROW && !coprefetch) {
    row_walker rw{row};

    auto row_entry = row_walker_table[cpu].check_hit(rw);
    if(row_entry.has_value()) {
      if(!intern_->warmup)
        conf_act++;
      row_entry->confidence = modify_confidence(row_entry->confidence,ACT_CONF,true);
      row_walker_table[cpu].fill(row_entry.value());
    }
  }
}

void orap_ppf::decrease_confidence_useless(champsim::address addr, uint32_t cpu, bool coprefetch) {
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  
  /*
  bool found_ip = false;
  double conf_modifier = 1.0;
  for(auto ip_hash : get_ip_hash_from_row(row,cpu)) {
    //fmt::print("Increasing confidence, row: {} rb: {} col: {}\n",row,rb,col);
    ip_tracker it{ip_hash};

    auto entry = ip_tracker_table[cpu].check_hit(it);

    if(entry.has_value()) {
        found_ip = true;
        bool eligible_for_demote = ((coprefetch && CONF_COUNTER_ASD_IP && entry->coprefetch_confidence_counter < ASD_IP_COUNTER_ACC_THRESH) || (!coprefetch && CONF_COUNTER_BUFFER_IP && entry->confidence_counter < BUFFER_IP_COUNTER_ACC_THRESH));
        if(coprefetch && !CONF_COUNTER_ASD_IP)
          eligible_for_demote = true;
        if(!coprefetch && !CONF_COUNTER_BUFFER_IP)
          eligible_for_demote = true;
        if(coprefetch)
          entry->coprefetch_confidence_counter = 0;
        else
          entry->confidence_counter = 0;

        if(!intern_->warmup && eligible_for_demote)
          conf_useless++;
        if(coprefetch && eligible_for_demote)
          if(MULT_DECREASE_ASD_IP)
            entry->coprefetch_confidence = modify_confidence(entry->coprefetch_confidence,USELESS_NCONF_FACTOR*conf_modifier);
          else
            entry->coprefetch_confidence = modify_confidence(entry->coprefetch_confidence,USELESS_NCONF*conf_modifier,false);
        else if(eligible_for_demote)
          if(MULT_DECREASE_BUFFER_IP)
            entry->confidence = modify_confidence(entry->confidence,USELESS_NCONF_FACTOR*conf_modifier);
          else
            entry->confidence = modify_confidence(entry->confidence,USELESS_NCONF*conf_modifier,false);
        ip_tracker_table[cpu].fill(entry.value());
    }

    conf_modifier *= USELESS_NCONF_DEPTH_MOD;
  }
  if(!found_ip && !intern_->warmup)
    orphaned_row_lookups++;

  //decrease confidence on row
  row_walker rw{row};

  auto row_entry = row_walker_table[cpu].check_hit(rw);
  if(row_entry.has_value()) {
    bool eligible_for_demote = !CONF_COUNTER_BUFFER_ROW || (row_entry->confidence_counter < BUFFER_ROW_COUNTER_ACC_THRESH);
    row_entry->confidence_counter = 0;
    if(!intern_->warmup && eligible_for_demote)
      conf_useless++;
    if(MULT_DECREASE_BUFFER_ROW && eligible_for_demote)
      row_entry->confidence = modify_confidence(row_entry->confidence,USELESS_NCONF_FACTOR);
    else if(eligible_for_demote)
      row_entry->confidence = modify_confidence(row_entry->confidence,USELESS_NCONF,false);
    row_walker_table[cpu].fill(row_entry.value());
  }*/

}

void orap_ppf::decrease_confidence_conflict(champsim::address ip, champsim::address addr, uint32_t cpu, bool coprefetch) {
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  /*
  uint16_t ip_hash = get_hash(ip.to<uint64_t>());
  ip_tracker it{ip_hash};
  auto entry = ip_tracker_table[cpu].check_hit(it);

  if(entry.has_value()) {

      bool eligible_for_demote = ((coprefetch && CONF_COUNTER_ASD_IP && entry->coprefetch_confidence_counter < ASD_IP_COUNTER_ACC_THRESH) || (!coprefetch && CONF_COUNTER_BUFFER_IP && entry->confidence_counter < BUFFER_IP_COUNTER_ACC_THRESH));
      if(coprefetch && !CONF_COUNTER_ASD_IP)
        eligible_for_demote = true;
      if(!coprefetch && !CONF_COUNTER_BUFFER_IP)
        eligible_for_demote = true;
      if(coprefetch)
        entry->coprefetch_confidence_counter = 0;
      else
        entry->confidence_counter = 0;
      
      if(eligible_for_demote && !intern_->warmup)
        conf_conflict++; 

      if(coprefetch && eligible_for_demote)
        if(MULT_DECREASE_ASD_IP)
          entry->coprefetch_confidence = modify_confidence(entry->coprefetch_confidence,CONFLICT_NCONF_FACTOR);
        else
          entry->coprefetch_confidence = modify_confidence(entry->coprefetch_confidence,CONFLICT_NCONF,false);
      else if(eligible_for_demote)
        if(MULT_DECREASE_BUFFER_IP)
          entry->confidence = modify_confidence(entry->confidence,CONFLICT_NCONF_FACTOR);
        else
          entry->confidence = modify_confidence(entry->confidence,CONFLICT_NCONF,false);
      ip_tracker_table[cpu].fill(entry.value());
  }

  //decrease confidence on row
  row_walker rw{row};

  auto row_entry = row_walker_table[cpu].check_hit(rw);
  if(row_entry.has_value()) {
    bool eligible_for_demote = !CONF_COUNTER_BUFFER_ROW || (row_entry->confidence_counter < BUFFER_ROW_COUNTER_ACC_THRESH);

    if(!intern_->warmup && eligible_for_demote)
      conf_conflict++;
    row_entry->confidence_counter = 0;
    if(MULT_DECREASE_BUFFER_ROW && eligible_for_demote)
      row_entry->confidence = modify_confidence(row_entry->confidence,CONFLICT_NCONF_FACTOR);
    else if(eligible_for_demote)
      row_entry->confidence = modify_confidence(row_entry->confidence,CONFLICT_NCONF,false);
    row_walker_table[cpu].fill(row_entry.value());
  }
  */
}

void orap_ppf::decrease_confidence_fill(champsim::address ip, champsim::address addr, uint32_t cpu, bool coprefetch) {
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  uint16_t ip_hash = get_hash(ip.to<uint64_t>());
  ip_tracker it{ip_hash};

  //assert(FILL_NCONF == 0 || !(CONF_COUNTER_ASD_IP || CONF_COUNTER_BUFFER_IP || CONF_COUNTER_BUFFER_ROW));

  auto entry = ip_tracker_table[cpu].check_hit(it);
  if(entry.has_value()) {


    if(coprefetch)
      entry->coprefetch_confidence_issue_counter = entry->coprefetch_confidence_issue_counter + ASD_IP_COUNTER_ISSUE_DECR > ASD_IP_COUNTER_ISSUE_MAX ? ASD_IP_COUNTER_ISSUE_MAX : entry->coprefetch_confidence_issue_counter + ASD_IP_COUNTER_ISSUE_DECR;
    else
      entry->confidence_issue_counter = entry->confidence_issue_counter + BUFFER_IP_COUNTER_ISSUE_DECR > BUFFER_IP_COUNTER_ISSUE_MAX ? BUFFER_IP_COUNTER_ISSUE_MAX : entry->confidence_issue_counter + BUFFER_IP_COUNTER_ISSUE_DECR;


    bool eligible_for_demote = (coprefetch && CONF_COUNTER_ASD_IP && entry->coprefetch_confidence_issue_counter >=ASD_IP_COUNTER_ISSUE_MAX);
    eligible_for_demote |= (!coprefetch && CONF_COUNTER_BUFFER_IP && entry->confidence_issue_counter >= BUFFER_IP_COUNTER_ISSUE_MAX);
    eligible_for_demote |= (coprefetch && !CONF_COUNTER_ASD_IP) || (!coprefetch && !CONF_COUNTER_BUFFER_IP);

    if(!intern_->warmup && eligible_for_demote)
      conf_fill++;
    //amount to increase should be dependent on current entry confidence
    if(coprefetch && eligible_for_demote) {
      if(MULT_DECREASE_ASD_IP)
        entry->coprefetch_confidence = modify_confidence(entry->coprefetch_confidence,FILL_NCONF_FACTOR);
      else
        entry->coprefetch_confidence = modify_confidence(entry->coprefetch_confidence,FILL_NCONF,false);
      entry->coprefetch_confidence_issue_counter = 0;
    }
    else if(eligible_for_demote) {
      if(MULT_DECREASE_BUFFER_IP)
        entry->confidence = modify_confidence(entry->confidence,FILL_NCONF_FACTOR);
      else
        entry->confidence = modify_confidence(entry->confidence,FILL_NCONF,false);
      entry->confidence_issue_counter = 0;
    }
    ip_tracker_table[cpu].fill(entry.value());
  }

  row_walker rw{row};
  auto row_entry = row_walker_table[cpu].check_hit(rw);
  if(row_entry.has_value()) {


    row_entry->confidence_issue_counter = row_entry->confidence_issue_counter + BUFFER_ROW_COUNTER_ISSUE_DECR > BUFFER_ROW_COUNTER_ISSUE_MAX ? BUFFER_ROW_COUNTER_ISSUE_MAX : row_entry->confidence_issue_counter + BUFFER_ROW_COUNTER_ISSUE_DECR;
    bool eligible_for_demote = CONF_COUNTER_BUFFER_ROW && row_entry->confidence_issue_counter >= BUFFER_ROW_COUNTER_ISSUE_MAX;
    eligible_for_demote |= !CONF_COUNTER_BUFFER_ROW;
    if(!intern_->warmup && eligible_for_demote)
      conf_fill++;
    if(eligible_for_demote) {
      if(MULT_DECREASE_BUFFER_ROW)
        row_entry->confidence = modify_confidence(row_entry->confidence,FILL_NCONF_FACTOR);
      else
        row_entry->confidence = modify_confidence(row_entry->confidence,FILL_NCONF,false);
      row_entry->confidence_issue_counter = 0;
    }
    row_walker_table[cpu].fill(row_entry.value());
  }
 
}

std::size_t orap_ppf::get_depth(uint8_t conf, uint32_t cpu) {
  uint8_t threshold = 0;
  uint8_t new_conf = conf < variable_buffer_conf[cpu] ? 0 : conf - variable_buffer_conf[cpu];
  for (auto thres : THRESH) {
    threshold++;
    if(new_conf < thres)
      break;
  }
  //if(new_conf != conf)
  //  fmt::print("Old Confidence: {} Modifier: {} New Confidence: {} Depth: {}\n",conf,variable_buffer_conf[cpu],new_conf,DEPTHS.at(threshold-1));
  return std::max(uint8_t{1},DEPTHS.at(threshold - 1));
}

uint8_t orap_ppf::get_squash_chance(uint8_t conf, uint32_t cpu) {
  uint8_t threshold = 0;
  uint8_t new_conf = conf < variable_buffer_conf[cpu] ? 0 : conf - variable_buffer_conf[cpu];
  for (auto thres : THRESH) {
    threshold++;
    if(new_conf < thres)
      break;
  }
  //if(new_conf != conf)
  //  fmt::print("Old Confidence: {} Modifier: {} New Confidence: {} Depth: {}\n",conf,variable_buffer_conf[cpu],new_conf,DEPTHS.at(threshold-1));
  return std::max(1,127 - CHANCE.at(threshold - 1));
}

double orap_ppf::get_asd_thresh(uint8_t conf, uint32_t cpu) {
  uint8_t threshold = 0;
  for (auto thres : THRESH) {
    threshold++;
    if(conf < thres)
      break;
  }
  //if(new_conf != conf)
  //  fmt::print("Old Confidence: {} Modifier: {} New Confidence: {} Depth: {}\n",conf,variable_buffer_conf[cpu],new_conf,DEPTHS.at(threshold-1));
  return std::min(ASD_DEPTHS.at(threshold - 1),variable_asd_thresh[cpu]);
}

void orap_ppf::update_walker(champsim::address addr, uint32_t cpu, champsim::address ip, bool is_blacklisted) {
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  std::size_t col = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(addr);
  std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr);
  uint16_t ip_hash = get_hash(ip.to<uint64_t>());


  //update row open table
  bool was_opened = update_row_open_table(addr);
  //abort, ip blacklisted
  if(is_blacklisted)
    return;

  //increase confidence on row open
  if(was_opened)
    increase_confidence_opened(ip,addr,cpu,false);

  //determine column stride direction from row_walker history
  int col_stride = 1; //default forward
  {
    row_walker rw_lookup{row};
    auto rw_entry = row_walker_table[cpu].check_hit(rw_lookup);
    if(rw_entry.has_value() && rw_entry->last_col >= 0) {
      if((int)col > rw_entry->last_col)
        col_stride = 1;
      else if((int)col < rw_entry->last_col)
        col_stride = -1;
      else
        col_stride = rw_entry->col_stride; //same column, keep previous direction
      rw_entry->last_col = (int8_t)col;
      rw_entry->col_stride = col_stride;
      row_walker_table[cpu].fill(rw_entry.value());
    } else if(rw_entry.has_value()) {
      rw_entry->last_col = (int8_t)col;
      row_walker_table[cpu].fill(rw_entry.value());
    }
  }

  //average confidence of row and ip
  uint8_t confidence = USE_ROW_CONF && USE_IP_CONF ? (std::max(get_confidence_from_ip_hash(ip_hash,cpu,false),get_confidence_from_row(addr,cpu))) :
                       USE_ROW_CONF                ? get_confidence_from_row(addr,cpu) :
                       USE_IP_CONF                 ? get_confidence_from_ip_hash(ip_hash,cpu,false) :
                       0;

  //number of clusters to prefetch
  std::size_t depth = get_depth(confidence,cpu);
  //we need to fetch that many clusters in addition to the present one
  std::size_t remaining_columns_in_cluster;
  if(col_stride > 0)
    remaining_columns_in_cluster = column_cluster_size - (col%column_cluster_size) - 1;
  else
    remaining_columns_in_cluster = (col%column_cluster_size);
  depth = depth*column_cluster_size + remaining_columns_in_cluster;

  uint8_t squash_chance = get_squash_chance(confidence,cpu);
  if((rand() % 128) < squash_chance) {
    if(!intern_->warmup)
      streams_squashed++;
    return;
  }

  bool all_success = true;
  int local_pq_occupancy = 0;
  for(int i = 0; i < NUM_CPUS; i++) {
    for(int j = 0; j < PREFETCH_SUBQUEUES; j++)
      local_pq_occupancy += std::size(pf_queue[i][j]); 
  }

  std::size_t remaining_mshr_space = std::max(0,((int)intern_->get_mshr_size() - (int)(intern_->get_mshr_occupancy() + intern_->get_pq_occupancy().back() + local_pq_occupancy)));
  int amount_to_prefetch = std::min(depth,remaining_mshr_space);
  int prefetched_so_far = 0;

  if(USE_PREFETCH_QUEUE) {
    if(amount_to_prefetch != 0) {
      int next_col = (int)col + col_stride;
      if(next_col >= 0 && next_col < (1 << (column_bits.size())))
        add_to_pq(prefetch_queue_entry(compose_base_and_column(addr,next_col),
                                      amount_to_prefetch,
                                      col_stride,
                                      ip,
                                      cpu,
                                      BUFFER_ID,
                                      true,
                                      SKIP_TAG_CHECK_BUFFER,
                                      true,
                                      true));
    }
  }
  else {
    for(int i = col + col_stride; (col_stride > 0 ? (i < (1 << (column_bits.size())) && i <= (int)col + amount_to_prefetch) : (i >= 0 && i >= (int)col - amount_to_prefetch)); i += col_stride) {
      bool success = true;
      bool pm = check_regionmap(compose_base_and_column(addr,i), cpu);
      if(!pm) {
          success = prefetch_line(compose_base_and_column(addr,i),true,cpu,ip,BUFFER_ID,SKIP_TAG_CHECK_BUFFER,true);
          if(success) {
            pf_issued_last_epoch[cpu]++;
            add_to_regionmap(compose_base_and_column(addr,i), cpu);
            //rw.confidence = modify_confidence(rw.confidence,1,false);
          } else if (!intern_->warmup) {
            prefetches_rejected++;
          }
      } else if (!intern_->warmup) {
        prefetches_filtered++;
      }
      if(success && !pm)
        prefetched_so_far++;
      else if(!success)
        break;
    }
    if(prefetched_so_far != 0)
      log_ip_hash_to_row(row,get_hash(ip.to<uint64_t>()),cpu);
  }
}


champsim::address orap_ppf::compose_base_and_column(champsim::address base, uint64_t column) {
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


void orap_ppf::prefetcher_initialize() {
  if(NUM_CPUS == 1) {
    PREFETCH_SUBQUEUES = PREFETCH_SUBQUEUES_SC;
    PREFETCH_SUBQUEUE_LIMIT = PREFETCH_SUBQUEUE_LIMIT_SC;
  }
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

  //initialize ip tracking and blacklist structs
  for(int i = 0; i < NUM_CPUS; i++) {
    ip_blacklist_table.push_back(champsim::msl::lru_table<ip_blacklist_counter,ip_blacklist_set,ip_blacklist_way>{IP_BLACKLIST_SETS,IP_BLACKLIST_WAYS});
    ip_tracker_table.push_back(champsim::msl::lru_table<ip_tracker,ip_tracker_set,ip_tracker_way>{IP_TRACKER_SETS,IP_TRACKER_WAYS});
    row_walker_table.push_back(champsim::msl::lru_table<row_walker,row_walker_set,row_walker_way>{RW_SETS,RW_WAYS});
    global_usefulness_buffer.push_back(1.0);
    global_usefulness_ppf.push_back(1.0);
    pf_issued_last_epoch.push_back(0);
    copf_issued_last_epoch.push_back(0);
    pf_issued_to_dram_last_epoch.push_back(0);
  }

  row_open_table.resize(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->rowbuffers());

  //find column bits
  for(int i = 0; i < 64; i ++) {
    if(MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(champsim::address{1ul << i}) != 0)
      column_bits.push_back(i);
  }

  //determine column cluster size
  column_cluster_size = 1;
  int prev_column_bit = column_bits.at(0);
  for(int i = 1; i < column_bits.size(); i++) {
    if(column_bits.at(i) > prev_column_bit + 1)
      break;
    prev_column_bit = column_bits.at(i);
    column_cluster_size++;
  }
  column_cluster_size = 1 << column_cluster_size;
  fmt::print("[{}] Column Cluster Size: {}\n",intern_->NAME,column_cluster_size);
  fmt::print("[{}] Initialized Buffer-PPF IP, Column Bits are: {} Conflict Filtering: {}\n",intern_->NAME,fmt::join(column_bits, ","),ENABLE_IP_BLACKLIST);
  int size_of_rw_table = RW_SETS * RW_WAYS * NUM_CPUS * row_walker::get_size_bits();
  int size_of_it_table = IP_TRACKER_SETS * IP_TRACKER_WAYS * NUM_CPUS * ip_tracker::get_size_bits();
  int size_of_pq = UNIFIED_PREFETCH_QUEUE ? (PREFETCH_SUBQUEUE_LIMIT * PREFETCH_SUBQUEUES * prefetch_queue_entry::get_size_bits()) : (PREFETCH_SUBQUEUE_LIMIT * PREFETCH_SUBQUEUES * prefetch_queue_entry::get_size_bits() * NUM_CPUS);
  int size_of_ip_table = IP_BLACKLIST_SETS * IP_BLACKLIST_WAYS * NUM_CPUS * ip_blacklist_counter::get_size_bits(); 
  fmt::print("\tSize of Row Walker Table(s): {}\n",champsim::data::kibibytes{champsim::data::bytes{size_of_rw_table/8}});
  fmt::print("\tSize of IP Tracker Table(s): {}\n",champsim::data::kibibytes{champsim::data::bytes{size_of_it_table/8}});
  fmt::print("\tSize of Prefetch Queue: {}\n", champsim::data::kibibytes{champsim::data::bytes{size_of_pq/8}});
  if(ENABLE_IP_BLACKLIST)
    fmt::print("\tSize of IP Blacklist Table(s): {}\n",champsim::data::kibibytes{champsim::data::bytes{size_of_ip_table/8}});

  for(int i = 0; i < NUM_CPUS; i++) {
    PPF_Modules.emplace_back();
  }
  for(int i = 0; i < NUM_CPUS; i++) {
    PPF_Modules.at(i).init(intern_);
    // Set the ORAP region map as the pre-perceptron filter for PPF
    PPF_Modules.at(i).set_region_map([this, i](champsim::address addr) -> bool {
      return this->check_regionmap(addr, i);
    });
  }

  // Initialize per-CPU region maps
  for(int i = 0; i < NUM_CPUS; i++) {
    region_maps.emplace_back(champsim::msl::lru_table<page_map,page_map_set,page_map_way>{PM_SETS,PM_WAYS});
  }

  for(int i = 0; i < NUM_CPUS; i++) {
    global_useful_buffer.push_back(0);
    global_useful_ppf.push_back(0);
    global_useless_buffer.push_back(0);
    global_useless_ppf.push_back(0);
    variable_buffer_conf.push_back(BUFFER_MIN_NCONF);
    variable_asd_thresh.push_back(ASD_MAX_THRESH);
    epoch_counter.push_back(0);
    watchdog_counter.push_back(0);
    pf_queue.push_back(std::vector<std::deque<prefetch_queue_entry>>(PREFETCH_SUBQUEUES));
    pf_queue_counter.push_back(std::vector<uint64_t>(PREFETCH_SUBQUEUES,0));
    PREFETCH_QUEUE_RATE.push_back(0);
    PREFETCH_QUEUE_LAST_ISSUE.push_back(0);
    last_subqueue_pos.push_back(0);

    mshr_hist_new.push_back(Histogram(intern_->get_mshr_size()+1,16));
    mshr_hist_old.push_back(Histogram(intern_->get_mshr_size()+1,16));
  }
}

uint32_t orap_ppf::prefetcher_cache_fill(champsim::address addr, champsim::address ip, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in, uint32_t metadata_evict, uint32_t cpu_evict)
{
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  if(prefetch && metadata_in == BUFFER_ID && evicted_addr != champsim::address{})
    decrease_confidence_fill(ip,addr,cpu,false);
  else if(prefetch && metadata_in == PPF_ID && evicted_addr != champsim::address{})
    decrease_confidence_fill(ip,addr,cpu,true);
    
  if(prefetch)
    epoch_counter[cpu]++;
  if(prefetch && metadata_in == BUFFER_ID)
    watchdog_counter[cpu] = 0;
  //if(evicted_addr != champsim::address{} && useless && metadata_evict == BUFFER_ID) {
  //  decrease_confidence_useless(evicted_addr,cpu_evict);
  //  if(!intern_->warmup)
  //    useless_tallied += 1;
  //}
  //else if (evicted_addr != champsim::address{} && useless && metadata_in == BUFFER_ID) {
  //  decrease_confidence_useless(addr,cpu);
  //  if(!intern_->warmup)
  //    pp_thrashes++;
  //}
  if(!intern_->warmup &&  metadata_evict == BUFFER_ID && useless)
    useless_tallied++;
  //catch generic conflicts
  if(prefetch && useless && (cpu != cpu_evict)) {
      //conflict happened here
      //champsim::address ip = get_ip(addr, cpu);
      update_ip_blacklist(IP_BLACKLIST_HARMFUL,ip,cpu,cpu_evict,metadata_in,metadata_evict);
      decrease_confidence_conflict(ip,addr,cpu,metadata_in == PPF_ID);
  } else if(useless) //useless
  {
    //not a conflict, but still useless
    decrease_confidence_useless(evicted_addr,cpu_evict,metadata_evict == PPF_ID);
  }


  if(metadata_evict == BUFFER_ID && useless && cpu_evict != NUM_CPUS)
    global_useless_buffer[cpu_evict]++;
  else if (metadata_evict == PPF_ID && useless && metadata_in == BUFFER_ID) {
    global_useless_buffer[cpu]++;
  }
  else if(metadata_evict == PPF_ID && useless && cpu_evict != NUM_CPUS) {
      global_useless_ppf[cpu_evict]++;
  }
  
  if(evicted_addr != champsim::address{} && !useless) {
    for(int i = 0; i < NUM_CPUS; i++)
      remove_from_regionmap(evicted_addr, i);
  }

  //sample stuff
  if(prefetch && (metadata_in == PPF_ID || metadata_in == BUFFER_ID)) {
    auto entry = prefetch_sample_table.check_hit(sample_table_entry{addr,0});
    if(entry.has_value()) {
      if(evicted_addr != champsim::address{}) {
        prefetch_latency_cycles_last_epoch += intern_->current_cycle() - entry->cycle_missed;
        prefetch_sampled_last_epoch++;
      }
      prefetch_sample_table.invalidate(sample_table_entry{addr,0});
    }
  } else if(!prefetch){
    auto entry = demand_sample_table.check_hit(sample_table_entry{addr,0});
    if(entry.has_value()) {
      if(evicted_addr != champsim::address{}) {
        demand_latency_cycles_last_epoch += intern_->current_cycle() - entry->cycle_missed;
        mshr_hist_new.at(0).tally(intern_->get_mshr_occupancy(),intern_->current_cycle() - entry->cycle_missed);
        demand_sampled_last_epoch++;
      }
      demand_sample_table.invalidate(sample_table_entry{addr,0});
    }
  }

  //close out pf queue
  clear_from_pq(addr,cpu);


  for(int i = 0; i < NUM_CPUS; i++) {
    PPF_Modules.at(i).handle_fill(addr, cpu, useless, set, way, prefetch, evicted_addr, metadata_in);
  }
  return metadata_in;
}

void orap_ppf::prefetcher_cycle_operate() {
  epoch_cycle_counter++;
  if(epoch_cycle_counter >= cycle_epoch) {
    //modify issue rate according to whether prefetch delay is higher than demand delay
    double demand_latency = demand_sampled_last_epoch == 0 ? 0 : demand_latency_cycles_last_epoch/(double)demand_sampled_last_epoch;
    double prefetch_latency = prefetch_sampled_last_epoch == 0 ? 0 : prefetch_latency_cycles_last_epoch/(double)prefetch_sampled_last_epoch;
    fmt::print("[{}] Average Prefetch Latency: {} ({}/{}) Average Demand Latency: {} ({}/{})\n",intern_->NAME,prefetch_latency,prefetch_latency_cycles_last_epoch,prefetch_sampled_last_epoch,demand_latency,demand_latency_cycles_last_epoch,demand_sampled_last_epoch);

    int lowest_core = 0;
    int highest_core = 0;
    uint64_t highest = 0;
    uint64_t lowest = 1e9;

    for(int i = 0; i < NUM_CPUS; i++) {
      if(pf_issued_to_dram_last_epoch[i] > highest && PREFETCH_QUEUE_RATE[i] != MAX_PREFETCH_QUEUE_RATE) {
        highest_core = i;
        highest = pf_issued_to_dram_last_epoch[i];
      }
      if(pf_issued_to_dram_last_epoch[i] < lowest && PREFETCH_QUEUE_RATE[i] != 0) {
        lowest_core = i;
        lowest = pf_issued_to_dram_last_epoch[i];
      }

      //flip hists
      mshr_hist_old.at(i).hist = mshr_hist_new.at(i).hist;
      mshr_hist_old.at(i).hist_count = mshr_hist_new.at(i).hist_count;
      mshr_hist_old.at(i).global_count = mshr_hist_new.at(i).global_count;
      mshr_hist_new.at(i).clear();

    }
    //need to throttle to protect demand bandwidth
    if(DELAY_PREFETCH_QUEUE_ISSUE) {
      if(demand_latency > prefetch_latency + PREFETCH_QUEUE_RATE[highest_core]) {
        //throttle the highest-prefetching core
        PREFETCH_QUEUE_RATE[highest_core] = std::min(MAX_PREFETCH_QUEUE_RATE,PREFETCH_QUEUE_RATE[highest_core]+PREFETCH_QUEUE_RATE_INCR);
      } else {
        //release throttle on lowest-prefetching throttled core
        if(PREFETCH_QUEUE_RATE[lowest_core] >= PREFETCH_QUEUE_RATE_INCR) {
          PREFETCH_QUEUE_RATE[lowest_core] -= PREFETCH_QUEUE_RATE_INCR;
        } else {
          PREFETCH_QUEUE_RATE[lowest_core] = 0;
        }
      }
    }

    fmt::print("[{}] PQ status (+Delay, Issued from Buffer, Issued from PPF, Total to DRAM):\n",intern_->NAME);
    for(int i = 0; i < (UNIFIED_PREFETCH_QUEUE ? 1 : NUM_CPUS); i++) {
      fmt::print("\t{} : {} {} {} {}\n",i,PREFETCH_QUEUE_RATE[i],pf_issued_last_epoch[i],copf_issued_last_epoch[i],pf_issued_to_dram_last_epoch[i]);
      for(int j = 0; j < PREFETCH_SUBQUEUES; j++) {
        fmt::print("\t\tSubqueue {} Occupancy: {} Total Issued: {}\n",j,pf_queue[i][j].size(),pf_queue_counter[i][j]);
      }
      pf_issued_last_epoch[i] = 0;
      copf_issued_last_epoch[i] = 0;
      pf_issued_to_dram_last_epoch[i] = 0;
    }

    demand_latency_cycles_last_epoch = 0;
    demand_sampled_last_epoch = 0;
    prefetch_latency_cycles_last_epoch = 0;
    prefetch_sampled_last_epoch = 0;
    epoch_cycle_counter = 0;
  }
  for(int i = 0; i < NUM_CPUS; i++) {
    watchdog_counter[i]++;
    if(watchdog_counter[i] > prefetch_watchdog_interval) {
      variable_buffer_conf[i] = variable_buffer_conf[i] < BUFFER_NCONF_STEP + BUFFER_MIN_NCONF ? BUFFER_MIN_NCONF : variable_buffer_conf[i] - BUFFER_NCONF_STEP;
      watchdog_counter[i] = 0;
    }

    if(epoch_counter[i] >= usefulness_measure_epoch) {
    
      global_usefulness_buffer[i] = (global_useless_buffer[i] <= 100 || (global_useful_buffer[i] + global_useless_buffer[i] <= 100)) ? 1.0 : (global_useful_buffer[i]) / (double)(global_useless_buffer[i] + global_useful_buffer[i]);
      global_usefulness_ppf[i] = (global_useless_ppf[i] <= 100 || (global_useful_ppf[i] + global_useless_ppf[i] <= 100)) ? 1.0 : (global_useful_ppf[i]) / (double)(global_useless_ppf[i] + global_useful_ppf[i]);

      if(epochs % usefulness_measure_print_interval == 0)
        fmt::print("CPU: {} Buffer usefulness: {} ({}/{}) PPF usefulness: {} ({}/{})\n",i,global_usefulness_buffer[i],global_useful_buffer[i],global_useless_buffer[i],global_usefulness_ppf[i],global_useful_ppf[i],global_useless_ppf[i]);
      global_useless_buffer[i] = 0;
      global_useful_buffer[i] = 0;
      global_useless_ppf[i] = 0;
      global_useful_ppf[i] = 0;
      
      if(global_usefulness_buffer[i] > TARGET_BUFFER_ACCURACY)
        variable_buffer_conf[i] = variable_buffer_conf[i] < BUFFER_NCONF_STEP + BUFFER_MIN_NCONF ? BUFFER_MIN_NCONF : variable_buffer_conf[i] - BUFFER_NCONF_STEP;
      else
        variable_buffer_conf[i] = variable_buffer_conf[i] + BUFFER_NCONF_STEP > BUFFER_MAX_NCONF ? BUFFER_MAX_NCONF : variable_buffer_conf[i] + BUFFER_NCONF_STEP;

      if(global_usefulness_ppf[i] > TARGET_PPF_ACCURACY)
        variable_asd_thresh[i] = std::min(variable_asd_thresh[i] + ASD_THRESH_STEP, ASD_MAX_THRESH);
      else if(global_usefulness_ppf[i] < 0.75) {
        variable_asd_thresh[i] = variable_asd_thresh[i] - ASD_THRESH_STEP;
        if(variable_asd_thresh[i] < ASD_MIN_THRESH)
          variable_asd_thresh[i] = ASD_MIN_THRESH;
      }
      if(epochs % usefulness_measure_print_interval == 0)
        fmt::print("\tBuffer confidence modifier: {} ppf thresh: {}\n",variable_buffer_conf[i], variable_asd_thresh[i]);
      
      fmt::print("Blacklist table CPU: {}\n",i);
      for(auto &block : ip_blacklist_table[i].get_contents()) {
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
  if(USE_PREFETCH_QUEUE) {
    issue_from_pq();
  }
}

void orap_ppf::prefetcher_final_stats() {
  fmt::print("Useful: {} Useless: {} PPF: {} Opened Rows: {} Prefetch-Prefetch Thrashes: {} Rejected: {} Filtered: {}\n", useful_tallied, useless_tallied, next_line_issued, opened_rows,pp_thrashes,prefetches_rejected, prefetches_filtered);
  fmt::print("Confidence Types: Useful: {}, Useful (No IP): {}, Fill: {}, Row ACT: {}, Conflict: {}\n",conf_useful,orphaned_ip_lookups,conf_fill,conf_act,conf_conflict);
  fmt::print("Conflict filters: {} Streams Squashed: {} Discarded: {}\n",conflict_filters,streams_squashed,prefetches_discarded_old);
  fmt::print("Rows without logged IPs: {}\n",orphaned_row_lookups);
  uint64_t confidence_total = 0;
  uint64_t confidence_row_total = 0;
  uint64_t confidence_coprefetch_total = 0;
  uint64_t tracked_rows = 0;
  uint64_t tracked_ips = 0;

  std::vector<uint64_t> tracked_rows_per_core(row_walker_table.size(),0);
  std::vector<uint64_t> tracked_ips_per_core(row_walker_table.size(),0);
  std::vector<uint64_t> confidence_total_per_core(row_walker_table.size(),0);
  std::vector<uint64_t> confidence_coprefetch_total_per_core(row_walker_table.size(),0);
  std::vector<uint64_t> confidence_row_total_per_core(row_walker_table.size(),0);

  std::vector<uint64_t> tracked_per_bin(DEPTHS.size(),0);
  std::vector<uint64_t> tracked_per_bin_coprefetch(DEPTHS.size(),0);
  std::vector<uint64_t> tracked_per_bin_row(DEPTHS.size(),0);

  int current_core = 0;
  for(auto core: ip_tracker_table) {
    for(auto block: core.get_contents()) {
      if(block.last_used != 0) {
        tracked_ips += 1;
        tracked_ips_per_core[current_core] += 1;
        confidence_total += block.data.confidence;
        confidence_coprefetch_total += block.data.coprefetch_confidence;
        confidence_total_per_core[current_core] += block.data.confidence;
        confidence_coprefetch_total_per_core[current_core] += block.data.coprefetch_confidence;
        uint8_t threshold = 0;
        for (auto thres : THRESH) {
          threshold++;
          if(block.data.confidence < thres)
            break;
        }
        tracked_per_bin[threshold-1] += 1;
        threshold = 0;
        for (auto thres : THRESH) {
          threshold++;
          if(block.data.coprefetch_confidence < thres)
            break;
        }
        tracked_per_bin_coprefetch[threshold-1] += 1;
      }
    }
    current_core += 1;
  }
  current_core = 0;
  for(auto core: row_walker_table) {
    for(auto block: core.get_contents()) {
      if(block.last_used != 0) {
        tracked_rows += 1;
        tracked_rows_per_core[current_core] += 1;
        confidence_row_total += block.data.confidence;
        confidence_row_total_per_core[current_core] += block.data.confidence;
        uint8_t threshold = 0;
        for (auto thres : THRESH) {
          threshold++;
          if(block.data.confidence < thres)
            break;
        }
        tracked_per_bin_row[threshold-1] += 1;
      }
    }
    current_core += 1;
  }

  fmt::print("GLOBAL CONFIDENCE IP: {} GLOBAL CONFIDENCE ROW: {} GLOBAL CONFIDENCE PPF: {} TRACKED IPS: {} TRACKED ROWS: {}\n",confidence_total / (double)tracked_ips, confidence_row_total / (double)tracked_rows, confidence_coprefetch_total / (double)tracked_ips, tracked_ips,tracked_rows);
  fmt::print("TRACKED PER BIN: (bin, ip, row, ppf)\n");
  for (int i = 0; i < DEPTHS.size(); i++) {
    fmt::print("\t{}: {} {} {}\n",THRESH[i],tracked_per_bin[i], tracked_per_bin_row[i], tracked_per_bin_coprefetch[i]);
  }
  fmt::print("PER CORE: (CORE : AVG CONF IP, AVG CONF ROW, AVG CONF PPF, TRACKED IPS, TRACKED ROWS)\n");
  for (int i = 0; i < row_walker_table.size(); i++) {
    fmt::print("\t{}: {} {} {} {} {}\n",i,confidence_total_per_core[i] / (double)tracked_ips_per_core[i], confidence_row_total_per_core[i] / (double)tracked_rows_per_core[i], confidence_coprefetch_total_per_core[i] / (double)tracked_ips_per_core[i], tracked_ips_per_core[i], tracked_rows_per_core[i]);
  }

  for(int i = 0; i < NUM_CPUS; i++) {
    fmt::print("Buffer for Core: {} Usefulness: {}\n",i,global_usefulness_buffer[i]);
    fmt::print("PPF for Core: {} Usefulness: {}\n",i,global_usefulness_ppf[i]);
  }

  fmt::print("MSHR Histograms:\n");
  mshr_hist_old.at(0).print();
  //for(int i = 0; i < NUM_CPUS; i++) {
  // fmt::print("\t{}\n",i);
  //  mshr_hist_old.at(i).print();
  //}
}

void orap_ppf::reset_ip_blacklist() {
  for(int i = 0; i < NUM_CPUS; i++) {
    ip_blacklist_table[i].flush();
  }
}

bool orap_ppf::is_ip_blacklisted(champsim::address ip, uint32_t cpu) {
  ip_blacklist_counter bc{ip};

  if(ip == champsim::address{})
    return false;

  auto entry = ip_blacklist_table[cpu].check_hit(bc);
  if(entry.has_value()) {
      return entry->counter >= IP_BLACKLIST_THRESH;
  }
  return false;
} 

void orap_ppf::update_ip_blacklist(std::size_t collision_type, champsim::address ip, uint32_t cpu, uint32_t victim_cpu, uint32_t target_prefetcher, uint32_t victim_prefetcher) {
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
    bool likely_to_be_used = target_prefetcher == BUFFER_ID ? global_usefulness_buffer[cpu] > 0.5 : global_usefulness_ppf[cpu] > 0.5;
    bool victim_likely_to_be_used = victim_prefetcher == BUFFER_ID ? global_usefulness_buffer[victim_cpu] > 0.5 : global_usefulness_ppf[victim_cpu] > 0.5;
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

std::vector<uint16_t> orap_ppf::get_ip_hash_from_row(uint32_t row, uint32_t cpu) {
  row_walker rw{row};
  auto entry = row_walker_table[cpu].check_hit(rw);
  std::vector<uint16_t> hashes;

  if(entry.has_value()) {
    auto contents = entry->ip_hashes.get_contents();
    std::sort(std::begin(contents), std::end(contents), [](auto a, auto b) {return a.last_used > b.last_used;});
    //if(std::size(contents) > 0)
    //  hashes.push_back(contents[0])
    for(auto& h : contents)
      if(h.last_used != 0)
        hashes.push_back(h.data);
  }
  //sort hashes by depth
  return hashes;
}

uint8_t orap_ppf::get_confidence_from_ip_hash(uint16_t ip_hash, uint32_t cpu, bool coprefetch) {
  ip_tracker it{ip_hash};

  auto entry = ip_tracker_table[cpu].check_hit(it);

  if(entry.has_value()) {
    if(coprefetch)
      return entry->coprefetch_confidence;
    else
      return entry->confidence;
  }

  ip_tracker_table[cpu].fill(it);
  if(coprefetch)
    return it.coprefetch_confidence;
  else
    return it.confidence;
}


uint8_t orap_ppf::get_confidence_from_row(champsim::address addr, uint32_t cpu) {
  std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  row_walker rw{row};

  auto entry = row_walker_table[cpu].check_hit(rw);

  if(entry.has_value()) {
    return entry->confidence;
  }

  row_walker_table[cpu].fill(rw);
  return rw.confidence;
}

void orap_ppf::log_ip_hash_to_row(uint32_t row, uint16_t ip_hash, uint32_t cpu) {
  row_walker rw{row};

  auto entry = row_walker_table[cpu].check_hit(rw);

  if(entry.has_value()) {
    entry->ip_hashes.fill(ip_hash);
    row_walker_table[cpu].fill(entry.value());
  } else {
    rw.ip_hashes.fill(ip_hash);
    row_walker_table[cpu].fill(rw);
  }
}

void orap_ppf::add_to_pq(prefetch_queue_entry pqe) {
  
    for(int i = 0; i < pqe.length; i++) {
      int cpu_to_use = UNIFIED_PREFETCH_QUEUE ? 0 : pqe.cpu;
      prefetch_queue_entry temp_pqe = pqe;
      temp_pqe.sent_so_far = 0;
      temp_pqe.length = 1;
      int column = (int)MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(pqe.start_addr) + i*pqe.stride;
      if(pqe.column_prefetch && (column < 0 || column >= 64)) {
        continue;
      }
      temp_pqe.start_addr = pqe.column_prefetch ? compose_base_and_column(pqe.start_addr, column) : champsim::address{champsim::block_number{pqe.start_addr}+(i*pqe.stride)};
      if(check_regionmap(temp_pqe.start_addr, pqe.cpu) || champsim::page_number{temp_pqe.start_addr} != champsim::page_number{pqe.start_addr}) {
        if(!intern_->warmup)
          prefetches_filtered++;
        continue;
      }
      std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(temp_pqe.start_addr) % PREFETCH_SUBQUEUES;
      if(pf_queue[cpu_to_use][rb].size() >= PREFETCH_SUBQUEUE_LIMIT) {
        int entry_pos = 0;

        //find a non-issued entry to remove
        while(pf_queue[cpu_to_use][rb][entry_pos].waiting_for_response && entry_pos < pf_queue[cpu_to_use][rb].size())
          entry_pos++;

        //if we can't, drop this prefetch
        if(entry_pos >= pf_queue[cpu_to_use][rb].size())
          return;

        //we found an entry to remove, update pagemap and erase the old entry from the queue
        remove_from_regionmap(pf_queue[cpu_to_use][rb][entry_pos].start_addr, pqe.cpu);
        pf_queue[cpu_to_use][rb].erase(std::next(pf_queue[cpu_to_use][rb].begin(),entry_pos));
        if(!intern_->warmup)
          prefetches_discarded_old++;
      }
      if(pf_queue[cpu_to_use][rb].size() >= MIN_PREFETCH_GANG_ISSUE)
        temp_pqe.has_company = true;
      pf_queue[cpu_to_use][rb].push_back(temp_pqe);
      add_to_regionmap(temp_pqe.start_addr, pqe.cpu);
    }
}

void orap_ppf::clear_from_pq(champsim::address addr, uint32_t cpu) {
  if(HOLD_PREFETCH_QUEUE_SLOT_UNTIL_COMPLETE) {
    std::size_t rb = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_rowbuffer(addr) % PREFETCH_SUBQUEUES;
    int cpu_to_use = UNIFIED_PREFETCH_QUEUE ? 0 : cpu;
    auto it = pf_queue.at(cpu_to_use).at(rb).begin();
    while(it != pf_queue.at(cpu_to_use).at(rb).end()) {
      if(it->waiting_for_response) {
        if(champsim::block_number{it->start_addr} == champsim::block_number{addr}) {
          it = pf_queue[cpu_to_use][rb].erase(it);
          continue;
        }
      }
      it++;
    }
  }
}

void orap_ppf::issue_from_pq() {
  //increment all pq counters
  for(int i = 0; i < NUM_CPUS; i++) {
    PREFETCH_QUEUE_LAST_ISSUE[i]++;
  }

  //find a packet to schedule, all cpu's can schedule 1 per cycle (since separate hardware)
  for(int i = 0; i < (UNIFIED_PREFETCH_QUEUE ? 2 : NUM_CPUS); i++) {
    last_queue_pos = UNIFIED_PREFETCH_QUEUE ? 0 : ((last_queue_pos + 1) % NUM_CPUS); //prevent weird priority issues (think of this like a round-robin that handles conflicts as all core queues merge into a single issue queue)
    for(int j = 0; j < PREFETCH_SUBQUEUES; j++) {
      last_subqueue_pos[last_queue_pos] = (last_subqueue_pos[last_queue_pos] + 1) % PREFETCH_SUBQUEUES;
      //are we allowed to issue this one
      if(pf_queue[last_queue_pos][last_subqueue_pos[last_queue_pos]].size() > 0 && PREFETCH_QUEUE_LAST_ISSUE[last_queue_pos] > PREFETCH_QUEUE_RATE[last_queue_pos]) {
        //we are! issue
        int entry_pos = 0;
        
        //find first valid entry
        while(pf_queue[last_queue_pos][last_subqueue_pos[last_queue_pos]][entry_pos].waiting_for_response && entry_pos < pf_queue[last_queue_pos][last_subqueue_pos[last_queue_pos]].size())
          entry_pos++;
        if(entry_pos >= pf_queue[last_queue_pos][last_subqueue_pos[last_queue_pos]].size())
          continue;

        auto& entry = pf_queue[last_queue_pos][last_subqueue_pos[last_queue_pos]].at(entry_pos);

        std::size_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(entry.start_addr);
        bool issued_any = false;
        bool should_pop = true;
        for(int l = entry.sent_so_far; l <  entry.length; l++) {
          int column = (int)MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(entry.start_addr) + (l*entry.stride);

          //make sure column doesn't leave rowbuffer
          if((column >= 64 || column < 0) && entry.column_prefetch) {
            break;
          }
          champsim::address to_pf = entry.column_prefetch ? compose_base_and_column(entry.start_addr,column) : champsim::address{champsim::block_number{entry.start_addr} + (l*entry.stride)};
          //make sure we stay on the same page
          if(champsim::page_number{entry.start_addr} != champsim::page_number{to_pf}) {
            break;
          }

          //check to see if its alone
          if(pf_queue[last_queue_pos][last_subqueue_pos[last_queue_pos]].size() < MIN_PREFETCH_GANG_ISSUE && !entry.has_company)
            break;

          //get pf distance
          int distance = to_pf.to<uint64_t>() >=  entry.start_addr.to<uint64_t>() ? (champsim::block_number{to_pf}.to<uint64_t>() - champsim::block_number{entry.start_addr}.to<uint64_t>()) : 
                                                                                                              (champsim::block_number{entry.start_addr}.to<uint64_t>() - champsim::block_number{to_pf}.to<uint64_t>());
          //now try to issue
          bool success = prefetch_line(to_pf,
                                      entry.fill_this_level,
                                      entry.cpu,
                                      entry.ip,
                                      entry.metadata_in,
                                      entry.skip_tag_check,
                                      entry.return_tag_check,
                                      distance);
          //failed, cache queue is full
          if(!success) {
            if(!intern_->warmup)
              prefetches_rejected++;
            entry.sent_so_far = l;
            should_pop = false;
            break;
          }
          //success!
          issued_any = true;
          if(entry.column_prefetch)
            pf_issued_last_epoch[entry.cpu]++;
          else
            copf_issued_last_epoch[entry.cpu]++;
          pf_queue_counter[last_queue_pos][last_subqueue_pos[last_queue_pos]]++;
        }
        //was able to issue any prefetches
        if(issued_any) {
          PREFETCH_QUEUE_LAST_ISSUE[last_queue_pos] = 0;
          log_ip_hash_to_row(row,get_hash(entry.ip.to<uint64_t>()),entry.cpu);
        }
        //should erase entry
        if(should_pop) {
          if(HOLD_PREFETCH_QUEUE_SLOT_UNTIL_COMPLETE && issued_any) {
            pf_queue[last_queue_pos][last_subqueue_pos[last_queue_pos]].at(entry_pos).waiting_for_response = true;
          } else {
            pf_queue[last_queue_pos][last_subqueue_pos[last_queue_pos]].pop_front();
          }
        }
        break;
      }
    }
  }

}


