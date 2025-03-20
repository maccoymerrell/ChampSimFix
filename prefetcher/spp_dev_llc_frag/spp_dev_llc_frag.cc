#include "spp_dev_llc_frag.h"
#include "dram_controller.h"

uint32_t spp_dev_llc_frag::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  //fmt::print("SPP_DEV_LLC_FRAG: OPERATING CACHE\n");
  //on miss
  if(cache_hit == 0) {
    //update row state table
    update_row_state_table(addr);
    last_miss++;

    add_to_bank_queue(addr);
  }
  else
    last_hit++;
  if(useful_prefetch) {
      last_useful++;
    }
  
  std::size_t aggression = std::round(aggression_factor);
  prefetch_next_line(addr);
  prefetch_column(addr);//!((cache_hit == 0 || is_row_open(addr)) && type == access_type::PREFETCH));

  return metadata_in;
}


void spp_dev_llc_frag::add_to_bank_queue(champsim::address addr) {
  auto loc = std::find(bank_util[get_dram_group(addr)].begin(), bank_util[get_dram_group(addr)].end(), addr);
  if(loc == bank_util[get_dram_group(addr)].end())
    bank_util[get_dram_group(addr)].push_back(addr);
}
void spp_dev_llc_frag::remove_from_bank_queue(champsim::address addr) {
  auto loc = std::find(bank_util[get_dram_group(addr)].begin(), bank_util[get_dram_group(addr)].end(), addr);
  if(loc != bank_util[get_dram_group(addr)].end())
    bank_util[get_dram_group(addr)].erase(loc);
}
std::size_t spp_dev_llc_frag::get_bank_queue_size(champsim::address addr) {
  return bank_util[get_dram_group(addr)].size();
}
uint32_t spp_dev_llc_frag::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{

  //look at fills, grab next columns from fills
  
  remove_from_bank_queue(addr);

  if(prefetch != 0 && evicted_addr != champsim::address{} && metadata_in != 0)
    last_evictions++;
  return metadata_in;
}

bool spp_dev_llc_frag::is_row_open(champsim::address addr) {
  return row_table.check_hit(row_state_table_entry{get_dram_group(addr),get_dram_row(addr)}).has_value();
}

void spp_dev_llc_frag::update_row_state_table(champsim::address addr) {
  row_table.fill(row_state_table_entry{get_dram_group(addr),get_dram_row(addr)});
}

void spp_dev_llc_frag::prefetch_column(champsim::address pf_addr) {
  uint64_t target_column = get_dram_column(pf_addr);
  std::size_t num = std::ceil(aggression_factor);

  for(int offset = 0; offset <= num; offset++) {
    if(offset == 0)
      continue;

    champsim::address new_address = compose_base_and_column(pf_addr,target_column + offset);
    //fmt::print("Address: {} Expected Row: {} Actual Row: {} Expected Column: {} Actual Column: {}\n",new_address,get_dram_row(pf_addr),get_dram_row(new_address),target_column + offset,get_dram_column(new_address));
    if(offset < 0 && target_column >= std::abs(offset)) {
      if(get_bank_queue_size(pf_addr) < 1) {
        if(prefetch_line(compose_base_and_column(pf_addr,target_column + offset),true,1)) {
          column_prefetches_issued++;
          prefetch_num++;
        }
        else
          full_queue++;
      }
      else {
        column_prefetches_dropped++;
      }
    }
    else if(target_column + offset < BLOCKS_PER_RB - 1){
      if(get_bank_queue_size(pf_addr) < 1) {
        if(prefetch_line(compose_base_and_column(pf_addr,target_column + offset),true,1)) {
          column_prefetches_issued++;
          prefetch_num++;
        }
        else
          full_queue++;
      }
      else {
        column_prefetches_dropped++;
      }
    }
  }
  
}

void spp_dev_llc_frag::prefetch_next_line(champsim::address pf_addr) {

  std::size_t num = 4;
  std::size_t grp = get_dram_group(pf_addr);
  
  for(std::size_t offset = 1; offset <= num; offset++) {

    champsim::address new_address = champsim::address{champsim::block_number{pf_addr} + offset};
    if(get_bank_queue_size(pf_addr) < 1) {
      if(prefetch_line(new_address,true,1)) {
        next_line_prefetches_issued++;
        prefetch_num++;
        //prefetch_column(new_address);
      }
      else {
        full_queue++;
      }
    }
    else {
      next_line_prefetches_dropped++;
    }
  }
}

//DRAM ADDRESS DECODING
uint64_t spp_dev_llc_frag::get_dram_column(champsim::address pf_addr) {
  uint64_t column_block = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(pf_addr);
  //fmt::print("Converted pf_addr:{} to column:{}\n",pf_addr,column_block);
  return column_block;
}
uint64_t spp_dev_llc_frag::get_dram_row(champsim::address pf_addr) {
  uint64_t row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr);
  //fmt::print("Converted pf_addr:{} to row:{}\n",pf_addr,row);
  return row;
}
unsigned int spp_dev_llc_frag::get_dram_group(champsim::address pf_addr) {
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
champsim::address spp_dev_llc_frag::compose_base_and_column(champsim::address base, uint64_t column) {
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

void spp_dev_llc_frag::prefetcher_final_stats() {
  fmt::print("SPP_LLC_STATS:\n");
  fmt::print("\tNEXT-COLUMN PREFETCHES ISSUED: {}\tDROPPED: {}\n",column_prefetches_issued,column_prefetches_dropped);
  fmt::print("\tNEXT-LINE PREFETCHES ISSUED: {}\tDROPPED: {}\n",next_line_prefetches_issued,next_line_prefetches_dropped);
  fmt::print("\tFULL QUEUE: {}\n",full_queue);

  fmt::print("Bank util:\n");
  std::size_t b_num = 0;
  for(auto it : bank_util) {
    fmt::print("\t{} : {}\n",b_num,it.size());
    b_num++;
  }
}

void spp_dev_llc_frag::prefetcher_cycle_operate() {

  /*
  double normal_hit_rate = last_hit / (double)(last_miss + last_hit);
  double prefetch_hit_rate = (last_useful + 0.5*last_last_useful) / (double)(last_last_evictions + (last_evictions*0.5));

  current_cycle++;
  if(current_cycle % update_period == 0) {
    if(last_useful <= last_last_useful) {
        direction = direction * -1;
    }
    if((prefetch_hit_rate >= (normal_hit_rate*0.8)) || direction < 0) {
      if(aggression_factor + (direction*increase_factor) < 0) {
        aggression_factor = 0;
      }
      else if(aggression_factor + (direction*increase_factor) > max_aggression) {
        aggression_factor = max_aggression;
      }
      else {
        aggression_factor += (direction)*increase_factor;
      }
    }

    //fmt::print("aggression factor: {} evicted: {} last_evicted: {} useful: {} hit rate: {} prefetch hit rate: {}\n",aggression_factor, last_evictions, last_last_evictions, last_useful, normal_hit_rate, prefetch_hit_rate);
    last_last_evictions = last_evictions;
    last_evictions = 0;
    last_last_useful = last_useful;
    last_useful = 0;
    last_hit = 0;
    last_miss = 0;
    
    
  }
  */
}