#include "sppam_b.h"
#include "../bingo_plus/bingo_plus.h" //added for Bingo plus feedback
#include "../berti_plus/berti_plus_parameters.h" //added for Berti feedback

#include <algorithm>

#include "cache.h"


template <typename T>
auto sppam_b::Sppam_b_Module::page_and_offset(T addr) -> std::pair<page, block_in_page>
{
  return std::pair{page{addr}, block_in_page{addr}};
}

void sppam_b::Sppam_b_Module::update_delta_history(std::vector<uint64_t>& delta_history, int64_t delta) {
  delta_history.insert(delta_history.begin(),delta & DELTA_HISTORY_MASK);
  delta_history.pop_back();
}

void sppam_b::Sppam_b_Module::update_recency_stack(std::vector<recency_stack_type>& recency_stack_temp, champsim::address addr, champsim::address ip) {
  auto [pn, page_offset] = page_and_offset(addr);
  auto recency_stack_entry = recency_stack_type{pn};
  auto region = regions.check_hit(region_type{pn});
  if(region.has_value()) {
    recency_stack_entry.recent_segments = get_segment_access_window(addr, ip, BP_ALIGNMENT_FACTOR, get_direction(ip));
    recency_stack_entry.direction = get_direction(ip);
    if(global_type == GLOBAL_HISTORY_TYPE::UNIQUE_RECENT_SEGMENT_HISTORY) {
      auto it = std::find(recency_stack_temp.begin(), recency_stack_temp.end(), recency_stack_entry);
      if (it != recency_stack_temp.end()) {
          // Move existing element to front
          recency_stack_temp.erase(it);
          recency_stack_temp.insert(recency_stack_temp.begin(),recency_stack_entry);
      } else {
          // Shift everything right and insert new int at front
          recency_stack_temp.pop_back();
          recency_stack_temp.insert(recency_stack_temp.begin(),recency_stack_entry);
      }
    } else {
      //as long as this region is not the top of the stack, append and pop the oldest entry
      if(recency_stack_temp.front().vpn != pn) {
        recency_stack_temp.pop_back();
        recency_stack_temp.insert(recency_stack_temp.begin(),recency_stack_entry);
      }
    }
  }
}
void sppam_b::Sppam_b_Module::update_local_and_global_contexts(champsim::address addr, champsim::address ip) {
  auto[pn, page_offset] = page_and_offset(addr);
  auto region = regions.check_hit(region_type{pn});
  if(region.has_value()) {
      region->last_block = page_offset.to<uint64_t>();
      region->last_ip = ip;
      regions.fill(region.value());
  }

  //update delta stack
  auto last_delta = champsim::block_number{addr}.to<int64_t>() - champsim::block_number{last_addr}.to<int64_t>();
  update_delta_history(delta_history, last_delta);
  last_ip = ip;
  last_addr = addr;

  update_recency_stack(recency_stack, addr, ip);
}

std::bitset<BP_LOCAL_BITS> sppam_b::Sppam_b_Module::get_local_access_window(champsim::address addr, champsim::address ip, int offset, bool direction) {
  //get the regions at, before, and after this address
  auto [pn, page_offset] = page_and_offset(addr);
  auto region = regions.check_hit(region_type{pn});
  auto last_region = regions.check_hit(region_type{pn-1});
  auto next_region = regions.check_hit(region_type{pn+1});
  std::bitset<BP_LOCAL_BITS> history;

  auto bitmap_size = region.has_value() ? region->access_map.size() : (last_region.has_value() ? last_region->access_map.size() : (next_region.has_value() ? next_region->access_map.size() : 0));
  //now grab the area with the given offset
  //direction == true means we take history from behind position,
  //direction == false means we take history from ahead of position
  if(direction == true) {
    for(int i = 0; i < BP_LOCAL_BITS; i++) {
      if(region.has_value() && page_offset.to<uint64_t>() + offset >= i && page_offset.to<uint64_t>() + offset - i < region->access_map.size()) {
        history.set(i,region->access_map.test(page_offset.to<uint64_t>() + offset - i));
      } else if(last_region.has_value() && page_offset.to<int64_t>() + offset - i  + bitmap_size >= 0 && bitmap_size + page_offset.to<int64_t>() + offset - i < last_region->access_map.size()) {
        history.set(i,last_region->access_map.test(bitmap_size + (page_offset.to<int64_t>() + offset - i)));
      } else if(next_region.has_value() && page_offset.to<uint64_t>() + offset + i - bitmap_size< next_region->access_map.size()) {
        history.set(i,next_region->access_map.test(page_offset.to<uint64_t>() + offset + i - bitmap_size));
      } else {
        history.set(i,false);
      }
    }
  } else {
    //get history as if we are traversing the opposite way
    for(int i = 0; i < BP_LOCAL_BITS; i++) {
      if(region.has_value() && page_offset.to<uint64_t>() + offset + i < region->access_map.size()) {
        history.set(i,region->access_map.test(page_offset.to<uint64_t>() + offset + i));
      } else if(next_region.has_value() && page_offset.to<int64_t>() + offset + i  - bitmap_size >= 0 && page_offset.to<int64_t>() + offset + i - bitmap_size < next_region->access_map.size()) {
        history.set(i,next_region->access_map.test(page_offset.to<int64_t>() + offset + i - bitmap_size));
      } else if(last_region.has_value() && page_offset.to<uint64_t>() + offset - i + bitmap_size < last_region->access_map.size()) {
        history.set(i,last_region->access_map.test(page_offset.to<uint64_t>() + offset - i + bitmap_size));
      } else {
        history.set(i,false);
      }
    }
  }
  return history;
}

std::bitset<BP_SEGMENT_BITS> sppam_b::Sppam_b_Module::get_segment_access_window(champsim::address addr, champsim::address ip, int offset, bool direction) {
  //get the regions at, before, and after this address
  auto [pn, page_offset] = page_and_offset(addr);
  auto region = regions.check_hit(region_type{pn});
  auto last_region = regions.check_hit(region_type{pn-1});
  auto next_region = regions.check_hit(region_type{pn+1});
  std::bitset<BP_SEGMENT_BITS> history;

  auto bitmap_size = region.has_value() ? region->access_map.size() : (last_region.has_value() ? last_region->access_map.size() : (next_region.has_value() ? next_region->access_map.size() : 0));
  //now grab the area with the given offset
  //direction == true means we take history from behind position,
  //direction == false means we take history from ahead of position
  if(direction == true) {
    for(int i = 0; i < BP_SEGMENT_BITS; i++) {
      if(region.has_value() && page_offset.to<uint64_t>() + offset >= i && page_offset.to<uint64_t>() + offset - i < region->access_map.size()) {
        history.set(i,region->access_map.test(page_offset.to<uint64_t>() + offset - i));
      } else if(last_region.has_value() && page_offset.to<int64_t>() + offset - i  + bitmap_size >= 0 && bitmap_size + page_offset.to<int64_t>() + offset - i < last_region->access_map.size()) {
        history.set(i,last_region->access_map.test(bitmap_size + (page_offset.to<int64_t>() + offset - i)));
      } else if(next_region.has_value() && page_offset.to<uint64_t>() + offset + i - bitmap_size< next_region->access_map.size()) {
        history.set(i,next_region->access_map.test(page_offset.to<uint64_t>() + offset + i - bitmap_size));
      } else {
        history.set(i,false);
      }
    }
  } else {
    //get history as if we are traversing the opposite way
    for(int i = 0; i < BP_SEGMENT_BITS; i++) {
      if(region.has_value() && page_offset.to<uint64_t>() + offset + i < region->access_map.size()) {
        history.set(i,region->access_map.test(page_offset.to<uint64_t>() + offset + i));
      } else if(next_region.has_value() && page_offset.to<int64_t>() + offset + i  - bitmap_size >= 0 && page_offset.to<int64_t>() + offset + i - bitmap_size < next_region->access_map.size()) {
        history.set(i,next_region->access_map.test(page_offset.to<int64_t>() + offset + i - bitmap_size));
      } else if(last_region.has_value() && page_offset.to<uint64_t>() + offset - i + bitmap_size < last_region->access_map.size()) {
        history.set(i,last_region->access_map.test(page_offset.to<uint64_t>() + offset - i + bitmap_size));
      } else {
        history.set(i,false);
      }
    }
  }
  return history;
}

std::bitset<BP_GLOBAL_BITS> sppam_b::Sppam_b_Module::get_global_access_window(champsim::address addr, champsim::address ip, int offset, bool direction) {

  if(global_type == GLOBAL_HISTORY_TYPE::OFFSET_HISTORY) {
    auto [pn, page_offset] = page_and_offset(addr);
    auto region = regions.check_hit(region_type{pn});
    std::bitset<BP_GLOBAL_BITS> history;
    if(region.has_value()) {
      auto temp_pn = pn;
      if(direction == true) {
        for(int i = 0; i < BP_GLOBAL_BITS; i++) {
          auto temp_region = regions.check_hit(region_type{temp_pn});
          if(temp_region.has_value()) {
            history.set(i,temp_region->access_map.test(page_offset.to<uint64_t>()));
          } else {
            history.set(i,false);
          }
          temp_pn--;
        }
      } else {
        for(int i = 0; i < BP_GLOBAL_BITS; i++) {
          auto temp_region = regions.check_hit(region_type{temp_pn});
          if(temp_region.has_value()) {
            history.set(i,temp_region->access_map.test(page_offset.to<uint64_t>()));
          } else {
            history.set(i,false);
          }
          temp_pn++;
        }
      }
    }
    return history;
  } else if(global_type == GLOBAL_HISTORY_TYPE::UNIQUE_RECENT_SEGMENT_HISTORY || global_type == GLOBAL_HISTORY_TYPE::ORDERED_RECENT_SEGMENT_HISTORY) {
    //first grab our segment using get_segment_access_window
    auto [pn, page_offset] = page_and_offset(addr);
    auto segment_history = get_segment_access_window(addr, ip, offset, direction);

    std::bitset<BP_GLOBAL_BITS> history;
    //set the first bp segment bits to this segment history
    for(int i = 0; i < BP_SEGMENT_BITS; i++) {
      history.set(i,segment_history.test(i));
    }
    //retrieve the remaining bits from the recency stack(exclude the first entry)
    for(int i = 1; i < recency_stack.size(); i++) {
      //grab the bitset
      auto temp_segment = recency_stack.at(i).recent_segments;
      //set the bits in the global history
      for(int j = 0; j < BP_SEGMENT_BITS; j++) {
        if(i*BP_SEGMENT_BITS + j < BP_GLOBAL_BITS)
          history.set(i*BP_SEGMENT_BITS + j,temp_segment.test(j));
      }
    }
    //return this history
    return history;

  } else if(global_type == GLOBAL_HISTORY_TYPE::RECENT_XOR_HISTORY) {
    std::bitset<BP_LOCAL_BITS> local_hist = get_local_access_window(addr, ip, offset, direction);
    std::bitset<BP_GLOBAL_BITS> history;
    //xor together the local histories of the recent regions in the recency stack
    for(int i = i; i < recency_stack.size(); i++) {
      auto region = regions.check_hit(region_type{recency_stack.at(i).vpn});
      if(region.has_value()) {
        auto region_addr = champsim::address{champsim::block_number{recency_stack.at(i).vpn} + region->last_block};
        auto temp_local = get_local_access_window(region_addr, ip, offset, recency_stack.at(i).direction);
        local_hist ^= temp_local;
      }
    }
    //copy the local history to the global history
    for(int i = 0; i < BP_LOCAL_BITS; i++) {
      history.set(i,local_hist.test(i));
    }
    return history;
  }
  else {
    //default global history is just the local history of the current region
    auto local_hist = get_local_access_window(addr, ip, offset, direction);
    std::bitset<BP_GLOBAL_BITS> history;
    for(int i = 0; i < BP_LOCAL_BITS; i++)
      history.set(i,local_hist.test(i));
    return history;
  }
}

void sppam_b::Sppam_b_Module::scan_global_history(std::bitset<BP_GLOBAL_BITS>& history, champsim::address addr, champsim::address ip, bool direction) {
  history = get_global_access_window(addr,ip, BP_ALIGNMENT_FACTOR, direction);
}

//get out equivalent to the local history
void sppam_b::Sppam_b_Module::scan_local_history(std::bitset<BP_LOCAL_BITS>& history, champsim::address addr, champsim::address ip, bool direction) {
  history = get_local_access_window(addr, ip, BP_ALIGNMENT_FACTOR, direction);
}

bool sppam_b::Sppam_b_Module::check_pagemap(champsim::address addr, bool prefetch)
{
  auto [pn, page_offset] = page_and_offset(addr);
  auto region = regions.check_hit(region_type{pn});

  if(prefetch)
    return (region.has_value() && region->prefetch_map.test(page_offset.to<std::size_t>()));
  else
    return (region.has_value() && region->access_map.test(page_offset.to<std::size_t>()));
}

void sppam_b::Sppam_b_Module::initialize(CACHE* cache) {
  intern_ = cache;

  if(bp_type == BP_TYPE::HASHED_PERCEPTRON)
    predictor = new sppam_bp::hashed_perceptron;
  else if(bp_type == BP_TYPE::GSHARE)
    predictor = new sppam_bp::gshare;

  recency_stack = std::vector<recency_stack_type>((BP_GLOBAL_BITS/BP_SEGMENT_BITS) + 1,recency_stack_type{});
  delta_history = std::vector<uint64_t>(DELTA_HISTORY_LENGTH,0);
  
  uint64_t total_state = get_state_bits();
  champsim::data::bytes total_bytes = champsim::data::bytes{(total_state/8) + 1};
  fmt::print("[{}] SPPAM_B Initialized. Total State: {}\n",intern_->NAME,champsim::data::kibibytes{total_bytes});
}

//get our equivalent to ip
champsim::address sppam_b::Sppam_b_Module::get_bp_ip(champsim::address addr, champsim::address pf_addr, champsim::address ip, std::vector<uint64_t> delta_history_temp) {
  //lets just use the page num for now
  auto [pn, page_offset] = page_and_offset(addr);

  if(ip_style == IP_TYPE::REGION)
    return champsim::address{page{addr}.to<uint64_t>()};
  else if(ip_style == IP_TYPE::IP)
    return ip;
  else if(ip_style == IP_TYPE::PAGE_OFFSET) {
    uint64_t base_val = 0;
    for(int i = 0; i < 64/(SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE); i++) {
      base_val |= (page_offset.to<uint64_t>() << i*(SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE));
    }
    return champsim::address{base_val};
  }
  else if(ip_style == IP_TYPE::SMS) {
    //offset and IP together
    uint64_t base_ip = ip.to<uint64_t>();
    for(int i = 0; i < 64/(SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE); i++) {
      base_ip ^= (page_offset.to<uint64_t>() << (i*(SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE)));
    }
    return champsim::address{base_ip};
  } else if(ip_style == IP_TYPE::SMS_SUB) {
    uint64_t base_ip = (ip.to<uint64_t>() >> (SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE)) << (SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE);
    for(int i = 0; i < 64/(SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE); i++) {
      base_ip ^= (page_offset.to<uint64_t>() << (i*(SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE)));
    }
    return champsim::address{base_ip};
  } else if(ip_style == IP_TYPE::SMS_APP) {
    uint64_t base_ip = ip.to<uint64_t>() << (SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE);
    base_ip |= page_offset.to<uint64_t>();
    return champsim::address{base_ip};
  } else if(ip_style == IP_TYPE::SMS_BACK) {
    uint64_t base_ip = ip.to<uint64_t>();
    base_ip ^= page_offset.to<uint64_t>() << (SPPAM_B_PAGE_BITS - LOG2_BLOCK_SIZE);
    return champsim::address{base_ip};
  } else if(ip_style == IP_TYPE::DELTA) {
    int64_t delta = champsim::block_number{pf_addr}.to<int64_t>() - (champsim::block_number{addr}.to<int64_t>());
    //xor the delta to extend it to its upper bits
    int64_t delta_trunc = delta & DELTA_HISTORY_MASK;
    delta ^= (delta_trunc) << 8;
    delta ^= (delta_trunc) << 16;
    delta ^= (delta_trunc) << 24;
    delta ^= (delta_trunc) << 32;
    delta ^= (delta_trunc) << 40;
    delta ^= (delta_trunc) << 48;
    delta ^= (delta_trunc) << 56;
    return champsim::address{delta};
  } else if(ip_style == IP_TYPE::DELTA_HISTORY) {
    //pack the recent delta history into the address, with the most recent delta in the least significant bits
    uint64_t base_val = (champsim::block_number{pf_addr}.to<int64_t>() - (champsim::block_number{addr}.to<int64_t>())) & DELTA_HISTORY_MASK; //include the most recent delta in the base value
    for(int i = 0; i < DELTA_HISTORY_LENGTH; i++) {
      uint64_t delta = delta_history_temp.at(i);
      base_val ^= (delta << ((i+1)*DELTA_HISTORY_BITS)); //shift the delta to its position and xor it to the base value. We start at (i+1) because the most recent delta is already included in the base value
    }
    return champsim::address{base_val};
  }
  else if(ip_style == IP_TYPE::NONE)
    return champsim::address{};
}

//get our equivalent to global history
std::bitset<BP_GLOBAL_BITS> sppam_b::Sppam_b_Module::get_global_history(champsim::address addr, champsim::address ip, bool direction) {
  return get_global_access_window(addr, ip, BP_ALIGNMENT_FACTOR, direction);
}

//get out equivalent to the local history
std::bitset<BP_LOCAL_BITS> sppam_b::Sppam_b_Module::get_local_history(champsim::address addr, champsim::address ip, bool direction) {
  return get_local_access_window(addr, ip, BP_ALIGNMENT_FACTOR, direction);
}



void sppam_b::Sppam_b_Module::add_to_pagemap(champsim::address addr, bool prefetch, bool useful) {
  auto [current_pn, page_offset] = page_and_offset(addr);
  auto temp_region = region_type{current_pn};
  auto demand_region = regions.check_hit(temp_region);

  if(demand_region.has_value()) {
    bool was_miss = false;
    if(prefetch) {
      if(!demand_region->prefetch_map.test(page_offset.to<std::size_t>()))
        was_miss = true;
      else
        pf_reissued++;
      demand_region->prefetch_map.set(page_offset.to<std::size_t>(), true);
      //demand_region->prefetch_debug_map.set(page_offset.to<std::size_t>(), true);
    }
    else {
      if(!demand_region->prefetch_map.test(page_offset.to<std::size_t>()))
        was_miss = true;
      //demand_region->last_block = page_offset.to<uint64_t>();
      demand_region->access_map.set(page_offset.to<std::size_t>(), true);
      demand_region->prefetch_map.set(page_offset.to<std::size_t>(), true);
    }
    if(was_miss) {
      demand_region->misses++;
    } else {
      demand_region->hits++;
    }
    if(prefetch && was_miss)
      demand_region->avail_prefetches = demand_region->avail_prefetches == 0 ? 0 : demand_region->avail_prefetches - 1;
    else if(was_miss || useful)
      demand_region->avail_prefetches = demand_region->avail_prefetches >= demand_region->budget ? demand_region->budget : demand_region->avail_prefetches + 1;
    demand_region->last_cycle = intern_->current_cycle();
    regions.fill(demand_region.value());
  } else {
    if(prefetch) {
      temp_region.prefetch_map.set(page_offset.to<std::size_t>(), true);
      //temp_region.prefetch_debug_map.set(page_offset.to<std::size_t>(), true);
    }
    else {
      //temp_region.last_block = page_offset.to<uint64_t>();
      temp_region.access_map.set(page_offset.to<std::size_t>(), true);
      temp_region.prefetch_map.set(page_offset.to<std::size_t>(), true);
    }
    temp_region.misses = 1;
    temp_region.last_cycle = intern_->current_cycle();
    
    regions.fill(temp_region);
  }
}

void sppam_b::Sppam_b_Module::add_to_debugmap(champsim::address addr) {
  auto [current_pn, page_offset] = page_and_offset(addr);
  auto temp_region = region_type{current_pn};
  auto demand_region = regions.check_hit(temp_region);

  if(demand_region.has_value()) {
    demand_region->prefetch_debug_map.set(page_offset.to<std::size_t>(), true);
    regions.fill(demand_region.value());
  }
}

void sppam_b::Sppam_b_Module::add_to_llmap(champsim::address addr) {
  auto [current_pn, page_offset] = page_and_offset(addr);
  auto temp_region = region_type{current_pn};
  auto demand_region = regions.check_hit(temp_region);

  if(demand_region.has_value()) {
    demand_region->ll_map.set(page_offset.to<std::size_t>(), true);
    regions.fill(demand_region.value());
  }
}

void sppam_b::Sppam_b_Module::remove_from_pagemap(champsim::address addr, bool prefetch) {
  auto [current_pn, page_offset] = page_and_offset(addr);
  auto demand_region = regions.check_hit(region_type{current_pn});

  if(demand_region.has_value()) {
    if(prefetch) {
      demand_region->ll_map.set(page_offset.to<std::size_t>(), false);
      demand_region->prefetch_map.set(page_offset.to<std::size_t>(), false);
    }
    else
      demand_region->access_map.set(page_offset.to<std::size_t>(), false);
    regions.fill(demand_region.value());
  }
}

bool sppam_b::Sppam_b_Module::check_llmap(champsim::address addr) {
  auto [current_pn, page_offset] = page_and_offset(addr);
  auto demand_region = regions.check_hit(region_type{current_pn});

  if(demand_region.has_value()) {
    return demand_region->ll_map.test(page_offset.to<std::size_t>());
  }
  return false;
}

void sppam_b::Sppam_b_Module::update_local_history(std::bitset<BP_LOCAL_BITS>& history, champsim::address addr, bool taken, bool direction) {
  //get the region access map and update the history based on that
  auto window = get_local_access_window(addr, champsim::address{}, BP_ALIGNMENT_FACTOR, direction);

  //slide the history over
  history <<= 1;
  //append bit that we have in the access map at the offset of the alignment factor
  history.set(0,window.test(0));
  
  //set the corresponding bit in the new history to taken
  history.set(BP_ALIGNMENT_FACTOR,taken || history.test(BP_ALIGNMENT_FACTOR)); //if we have ever seen this offset taken, we want to keep it as a 1 in the local history to indicate that this is a "hot" offset, even if we see some not-taken instances later. This is because even if an offset is not taken every time, it may still be predictive if it is taken often enough, and we don't want to lose that predictive power just because of some noise in the history.
}

void sppam_b::Sppam_b_Module::update_global_history(std::bitset<BP_GLOBAL_BITS>& history, champsim::address addr, bool taken, bool direction) {
  if(global_type == GLOBAL_HISTORY_TYPE::OFFSET_HISTORY) {
    history <<= 1;
    history.set(0,taken);
  } else if(global_type == GLOBAL_HISTORY_TYPE::UNIQUE_RECENT_SEGMENT_HISTORY || global_type == GLOBAL_HISTORY_TYPE::ORDERED_RECENT_SEGMENT_HISTORY) {
    //only slide the current segment history (front BP_SEGMENT_BITS) of the global history
    std::bitset<BP_SEGMENT_BITS> current_segment_history;
    for(int i = 0; i < BP_SEGMENT_BITS; i++) {
      current_segment_history.set(i,history.test(i));
    }
    current_segment_history <<= 1;
    current_segment_history.set(0,taken);
    //now update the global history with this new segment history
    for(int i = 0; i < BP_SEGMENT_BITS; i++) {
      history.set(i,current_segment_history.test(i));
    }
  } else if(global_type == GLOBAL_HISTORY_TYPE::RECENT_XOR_HISTORY) {
    //reconstruct the global history based on the recency stack, but with the updated current local history
    std::bitset<BP_LOCAL_BITS> new_history = get_local_access_window(addr, champsim::address{}, BP_ALIGNMENT_FACTOR, direction);
    //xor together the local histories of the recent regions in the recency stack
    for(int i = 0; i < recency_stack.size(); i++) {
      auto region = regions.check_hit(region_type{recency_stack.at(i).vpn});
      if(region.has_value()) {
        auto region_addr = champsim::address{champsim::block_number{recency_stack.at(i).vpn} + region->last_block};
        auto temp_local = get_local_access_window(region_addr, champsim::address{}, BP_ALIGNMENT_FACTOR, direction);
        new_history ^= temp_local;
      }
    }
    for(int i = 0; i < BP_LOCAL_BITS; i++) {
      history.set(i,new_history.test(i));
    }
  }
}


void sppam_b::Sppam_b_Module::tally_useful(champsim::address addr) {
  //tally useful
  global_useful_prefetch++;

  if(global_useful_prefetch + global_useless_prefetch >= GLOBAL_USEFULNESS_SAMPLE) {
    global_conf = global_useful_prefetch / (double)GLOBAL_USEFULNESS_SAMPLE;
    global_useless_prefetch = 0;
    global_useful_prefetch = 0;
  }

  auto [pn, page_offset] = page_and_offset(addr);

  auto region = regions.check_hit(region_type{pn});
  if(region.has_value()) {
    region->useful++;
    regions.fill(region.value());
  }
}

void sppam_b::Sppam_b_Module::tally_useless(champsim::address addr) {
  //tally useless
  global_useless_prefetch++;

  if(global_useful_prefetch + global_useless_prefetch >= GLOBAL_USEFULNESS_SAMPLE) {
    global_conf = global_useful_prefetch / (double)GLOBAL_USEFULNESS_SAMPLE;
    global_useless_prefetch = 0;
    global_useful_prefetch = 0;
  }

  auto [pn, page_offset] = page_and_offset(addr);

  auto region = regions.check_hit(region_type{pn});
  if(region.has_value()) {
    region->useless++;
    regions.fill(region.value());
  }
}




uint32_t sppam_b::prefetcher_cache_operate(champsim::address addr, champsim::address ip, bool cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in)
{
  engine.resolve_ip(champsim::block_number{addr},true);
  if(useful_prefetch) {
    engine.tally_useful(addr);
  }

  engine.log_outcome(addr,ip);

  //update metadata
  //region map
  engine.add_to_pagemap(addr,false,useful_prefetch);

  //global history
  engine.update_local_and_global_contexts(addr,ip);

  //do predictions
  engine.do_prefetch(addr,ip,cache_hit,useful_prefetch,type,metadata_in);
  engine.last_cycle = intern_->current_cycle();

  return metadata_in;
}

uint32_t sppam_b::prefetcher_cache_fill(champsim::address addr, long set, long way, bool prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  bool useless = false;
  if(prev_useless_prefetches < intern_->sim_stats.pf_useless) {
    engine.tally_useless(evicted_addr);
    useless = true;
  }
  prev_useless_prefetches = intern_->sim_stats.pf_useless;
  if(evicted_addr != champsim::address{} && !useless) {
    engine.remove_from_pagemap(evicted_addr,true);
    if(engine.CLEAR_ACCESS_MAP_ON_EVICT)
      engine.remove_from_pagemap(evicted_addr,false);
  }
  if(useless)
    engine.resolve_ip(champsim::block_number{evicted_addr},false);


  return metadata_in;
}


void sppam_b::Sppam_b_Module::log_outcome(champsim::address addr, champsim::address ip) {
  //learning
  //1. find the region that was just accessed
  //2. For each bit gap between the last block access < current access
  //2a. Identify the proper local and global histories using the region's last ip's and address
  //2b. log the outcome of the bit
  auto [pn, page_offset] = page_and_offset(addr);
  //track_direction(last_addr, last_ip, addr.to<uint64_t>() > last_addr.to<uint64_t>());
  //auto last_access_addr_temp = last_access_addr;

  //learn the history pattern for same-page jumps
  auto region = regions.check_hit(region_type{pn});
  if(region.has_value() && pn == page{last_addr}) {
      bool local_direction = addr.to<uint64_t>() > last_addr.to<uint64_t>();
      auto ip_to_use = USE_REGION_IP_FOR_LEARNING ? region->last_ip : last_ip;
      auto start = region->last_block;
      auto end = page_offset.to<uint64_t>();
      track_direction(champsim::address{champsim::block_number{pn} + start}, region->last_ip, local_direction);
      auto global_hist = get_global_history(USE_REGION_ADDR_FOR_GLOBAL_HIST_LEARNING ? champsim::address{champsim::block_number{pn} + start} : last_addr,ip_to_use,local_direction);
      auto local_hist = get_local_history(champsim::address{champsim::block_number{pn} + start},region->last_ip,local_direction);
      for(int i = start; (local_direction ? i < end : i > end); (local_direction ? i++ : i--)) {
        auto outcome = (i == end - (local_direction ? 1 : -1)) && (intern_->current_cycle() - region->last_cycle < TIMELINESS_CYCLE);
        auto last_access_addr_temp = champsim::address{champsim::block_number{pn} + i};
        //auto local_hist = get_local_history(last_access_addr_temp,region->last_ip,local_direction);
        //auto global_hist = get_global_history(USE_REGION_ADDR_FOR_GLOBAL_HIST_LEARNING ? last_access_addr_temp : last_addr,ip_to_use,local_direction);
        predictor->last_branch_result(get_bp_ip(champsim::address{champsim::block_number{pn} + start},last_access_addr_temp,ip_to_use,delta_history),global_hist, local_hist,outcome);
      }
  }
  if(region.has_value() && pn == (page{last_addr} + 1)) {
      //we are learning a pattern across a page (forward)
      bool local_direction = true;
      auto ip_to_use = last_ip;
      auto start = champsim::block_number{last_addr};
      auto end = champsim::block_number{pn} +page_offset.to<uint64_t>();
      track_direction(last_addr, last_ip, local_direction);
      auto global_hist = get_global_history(USE_REGION_ADDR_FOR_GLOBAL_HIST_LEARNING ? last_addr : last_addr,ip_to_use,local_direction);
      auto local_hist = get_local_history(last_addr,last_ip,local_direction);
      for(auto i = start; (local_direction ? i < end : i > end); (local_direction ? i++ : i--)) {
        auto last_access_addr_temp =  champsim::address{i};
        auto outcome = (i == end - (local_direction ? 1 : -1)) && (intern_->current_cycle() - last_cycle < TIMELINESS_CYCLE);
        //auto local_hist = get_local_history(last_access_addr_temp,last_ip,local_direction);
        //auto global_hist = get_global_history(USE_REGION_ADDR_FOR_GLOBAL_HIST_LEARNING ? last_access_addr_temp : last_addr,ip_to_use,local_direction);
        predictor->last_branch_result(get_bp_ip(last_addr,last_access_addr_temp,ip_to_use,delta_history),global_hist, local_hist,outcome);
      }
   }
   if(region.has_value() && pn == (page{last_addr} - 1)) {
      //we are learning a pattern across a page (backward)
      bool local_direction = false;
      auto ip_to_use = last_ip;
      auto start = champsim::block_number{last_addr};
      auto end = champsim::block_number{pn} + page_offset.to<uint64_t>();
      track_direction(last_addr, last_ip, local_direction);
      auto local_hist = get_local_history(last_addr,last_ip,local_direction);
      auto global_hist = get_global_history(USE_REGION_ADDR_FOR_GLOBAL_HIST_LEARNING ? last_addr : last_addr,ip_to_use,local_direction);
      for(auto i = start; (local_direction ? i < end : i > end); (local_direction ? i++ : i--)) {
        auto last_access_addr_temp =  champsim::address{i};
        auto outcome = (i == end - (local_direction ? 1 : -1)) && (intern_->current_cycle() - last_cycle < TIMELINESS_CYCLE);
        //auto local_hist = get_local_history(last_access_addr_temp,last_ip,local_direction);
        //auto global_hist = get_global_history(USE_REGION_ADDR_FOR_GLOBAL_HIST_LEARNING ? last_access_addr_temp : last_addr,ip_to_use,local_direction);
        predictor->last_branch_result(get_bp_ip(last_addr,last_access_addr_temp,ip_to_use,delta_history),global_hist, local_hist,outcome);
      }
   }
}

bool sppam_b::Sppam_b_Module::get_direction(champsim::address ip) {
  //return true;
  auto entry = ip_conf_table.check_hit(ip_conf_entry{ip});
  if(entry.has_value()) {
    return entry->dir_counter >= DIRECTION_SAMPLE_MAX / 2;
  } else {
    return true; //default to prefetching forward
  }
}

void sppam_b::Sppam_b_Module::do_prefetch(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in) {

  prefetch_triggers++;
  int free_space = std::max((int)intern_->get_mshr_size() - (int)intern_->get_mshr_occupancy() - (int)intern_->get_pq_occupancy().back(),0);
  auto region = regions.check_hit(region_type{page{addr}});
  if(!region.has_value())
    return;

  auto recency_stack_temp = recency_stack;
  
  auto delta_history_temp = delta_history;
  //enforce limit on prefetches issued per page (depth)
  auto ip_pf_degree = IP_PREFETCH_DEGREE ? get_pf_degree(ip) : PREFETCH_DEGREE;
  current_pf_degree = std::min(region->avail_prefetches,(int64_t)ip_pf_degree);
  if(current_pf_degree == region->avail_prefetches)
    pf_degree_limited++;

  int pf_issued = 0;
  int lookaheads = 0;
  int lookahead_offset = 0;
  std::vector<champsim::address> ip_predict = ip_history;
   bool direction = get_direction(ip);
  auto global_hist = get_global_history(addr,ip,direction);
  auto local_hist = get_local_history(addr,ip,direction);
  auto predict_global_hist = global_hist;
  auto predict_local_hist = local_hist;
  auto predict_addr = addr;
  auto pf_conf = get_conf(ip) * get_bw_conf();
  if(!should_issue(pf_conf) && PROB_DROP)
    return;
  auto ip_max_lookahead = IP_LOOKAHEAD ? get_lookahead(ip) : MAX_LOOKAHEAD;

  int64_t current_delta = direction ? 1 : -1;

  if((!in_warmup) && DO_DEBUG)
    fmt::print("Doing prefetch for address: {} free space: {}\n",addr,free_space);
  //usefulness must remain above threshold to continue scan
  //get negative and positive patterns, set base location for the scan
  champsim::address pf_base_addr = addr;
  while(pf_issued < current_pf_degree && lookaheads <= ip_max_lookahead) {
   
    std::pair<bool, double> prediction_pack;
    champsim::address pos_step_addr = champsim::address{champsim::block_number{pf_base_addr} + (direction ? 1 : -1)};
    //make prediction using local or global history based on configuration
    prediction_pack = predictor->predict_branch(get_bp_ip(predict_addr, pf_base_addr, ip, delta_history_temp),predict_global_hist,predict_local_hist);
    
    bool prediction = prediction_pack.first;
    double prediction_conf = prediction_pack.second;

    
    bool to_l2c = pf_issued >= free_space || (pf_conf < MIN_L1D_CONF);
    if(prediction) {
      delta_freq[current_delta]++;
      add_to_debugmap(pos_step_addr);
      if(!check_pagemap(champsim::address{pos_step_addr},true) && !(to_l2c && check_llmap(pos_step_addr))) {
        if(bool prefetch_success = intern_->prefetch_line(pos_step_addr, !to_l2c, metadata_in); prefetch_success) {
            if((!in_warmup) && DO_DEBUG)
              fmt::print("\t\t\tPrefetched: {} L2C: {}\n",pos_step_addr, to_l2c);
            if(!to_l2c)
              add_to_pagemap(pos_step_addr,true);
            else {
              pf_l2c++;
              add_to_llmap(pos_step_addr);
            }
            pf_issued++;
            prefetches_issued++;
            track_ip(pos_step_addr, ip);
        } else {
          if((!in_warmup) && DO_DEBUG)
            fmt::print("\t\t\tPrefetch failed: {} L2C: {}\n",pos_step_addr, to_l2c);
        }
      } else {
        if((!in_warmup) && DO_DEBUG)
        fmt::print("\t\t\tWas filtered!\n");
        prefetches_filtered++;
      }
    }

    if(DO_LOOKAHEAD && pf_conf > MIN_LOOKAHEAD_CONF) {
      if((!in_warmup) && DO_DEBUG)
        fmt::print("\t\t\tLookahead stopped due to low confidence: {}%\n",(int)(pf_conf*100));

      if(page{pf_base_addr} != page{pos_step_addr}) {
        update_recency_stack(recency_stack,pos_step_addr,ip);
      }
      pf_base_addr = pos_step_addr;

      //update histories
      update_global_history(global_hist,pf_base_addr,prediction,direction);
      update_local_history(local_hist,pf_base_addr,prediction,direction);

      if(prediction) {
        //only update the predict history if we actually took the prefetch, otherwise we are just learning from a negative pattern which is less useful for future predictions
        predict_global_hist = global_hist;
        predict_local_hist = local_hist;
        predict_addr = pf_base_addr;
        update_delta_history(delta_history_temp, current_delta);
        current_delta = direction ? 1 : -1;
        pf_conf *= get_conf(ip);
      } else {
        current_delta = current_delta + (direction ? 1 : -1);
      }
      lookaheads++;
      total_lookaheads++;


      
    } else {
      break;
    }
  }
  lookahead_freq[lookaheads]++;
  //roll back recency stack
  recency_stack = recency_stack_temp;
}

void sppam_b::Sppam_b_Module::track_ip(champsim::address addr, champsim::address ip) {
  //track ip for resolving useful/useless
  auto index = champsim::block_number{addr}.to<uint64_t>() % IP_TRACK_TABLE_SIZE;
  if((intern_->current_cycle() % TEMPORAL_SAMPLE_RATE) != 0)
    return; //only track on certain samples to save space and focus on temporal patterns

  if(ip_track_table[index].addr == champsim::block_number{addr})
    return; //already tracking this IP
  
  if(ip_track_table[index].addr == champsim::block_number{} || (ip_track_table[index].last_cycle + IP_TRACK_TABLE_TIMEOUT < intern_->current_cycle())) {
    //we are evicting an IP
    if(ip_track_table[index].addr != champsim::block_number{} && USELESS_ON_TIMEOUT) {
      //resolve the evicted IP as useless since we never got to confirm it as useful
      resolve_ip(ip_track_table[index].addr, false);
    }
    ip_track_table[index].addr = champsim::block_number{addr};
    ip_track_table[index].ip = ip;
    ip_track_table[index].last_cycle = intern_->current_cycle();
  }
}

void sppam_b::Sppam_b_Module::track_direction(champsim::address addr, champsim::address ip, bool direction) {
  //lookup conf table
  auto entry = ip_conf_table.check_hit(ip_conf_entry{ip});
  if(entry.has_value()) {
    if(direction)      entry->dir_counter = std::min(entry->dir_counter + 1, DIRECTION_SAMPLE_MAX);
    else                entry->dir_counter = std::max(entry->dir_counter - 1, int64_t{0});
    ip_conf_table.fill(entry.value());
  }
}

void sppam_b::Sppam_b_Module::resolve_ip(champsim::block_number addr, bool useful) {
  //resolve ip for useful/useless
  auto index = addr.to<uint64_t>() % IP_TRACK_TABLE_SIZE;
  if(ip_track_table[index].addr != addr)
    return; //not tracking this IP
  pend_cycles += intern_->current_cycle() - ip_track_table[index].last_cycle;
  resolutions++;
  auto ip = ip_track_table[index].ip;
  ip_track_table[index].addr = champsim::block_number{};
  update_ip_conf(ip, useful);
}

void sppam_b::Sppam_b_Module::update_ip_conf(champsim::address ip, bool useful) {
  auto entry = ip_conf_table.check_hit(ip_conf_entry{ip});
  if(entry.has_value()) {
    if(useful) {
      entry->useful++;
    } else {
      entry->useless++;
    }
    if(entry->useful + entry->useless >= IP_CONF_TABLE_SAMPLES) {
      entry->conf = entry->useful / (double)(entry->useful + entry->useless);
      entry->useful = 0;
      entry->useless = 0;
    }
    entry->lifetime_samples++;

    entry->lookahead_depth = useful ? std::min(entry->lookahead_depth + 1, MAX_LOOKAHEAD) : std::max(entry->lookahead_depth - 1, int64_t{0}); 
    entry->pf_degree = useful ? std::min(entry->pf_degree + 1, PREFETCH_DEGREE) : std::max(entry->pf_degree - 1, int64_t{1});

    ip_conf_table.fill(entry.value());
  } else {
    ip_conf_table.fill(ip_conf_entry{ip});
  }
}

void sppam_b::Sppam_b_Module::print_patterns() {

  //save region table to file for analysis
  std::ofstream region_file;
  region_file.open("sppam_b_regions.txt",std::ios::out | std::ios::trunc);
  for(auto& entry : regions.get_blocks()) {
    region_file << fmt::format("Region VPN: {:x} Misses: {} Hits: {} Useful: {} Useless: {} Offset: {} IP: {:x}\n",entry.data.vpn.to<uint64_t>(),entry.data.misses,entry.data.hits,entry.data.useful,entry.data.useless,entry.data.last_block,entry.data.last_ip.to<uint64_t>());
    region_file << fmt::format("\tAccess Map  : ");
    for(int i = 0; i < entry.data.access_map.size(); i++)
      region_file << fmt::format("{}",int(entry.data.access_map.test(i)));
    region_file << fmt::format("\n\tPrefetch Map: ");
    for(int i = 0; i < entry.data.prefetch_map.size(); i++)
      region_file << fmt::format("{}",int(entry.data.prefetch_map.test(i)));
    region_file << fmt::format("\n\tPD Map      : ");
    for(int i = 0; i < entry.data.prefetch_debug_map.size(); i++)
      region_file << fmt::format("{}",int(entry.data.prefetch_debug_map.test(i)));
    region_file << fmt::format("\n");
  }
  region_file.close();

  //ip conf table
  std::ofstream ip_conf_file;
  ip_conf_file.open("sppam_b_ip_conf.txt",std::ios::out | std::ios::trunc);
  for(auto& entry : ip_conf_table.get_blocks()) {
    if(entry.data.ip == champsim::address{})
      continue;
    ip_conf_file << fmt::format("IP: {:x} Conf: {} Useful: {} Useless: {} Direction: {} Lifetime Samples: {} Lookahead Depth: {} PF Degree: {}\n",entry.data.ip.to<uint64_t>(),entry.data.conf,entry.data.useful,entry.data.useless,entry.data.dir_counter,entry.data.lifetime_samples,entry.data.lookahead_depth,entry.data.pf_degree);
  }
  ip_conf_file.close();

  //delta frequencies
  std::ofstream delta_file;
  delta_file.open("sppam_b_delta_freq.txt",std::ios::out | std::ios::trunc);
  for(auto& entry : delta_freq) {
    delta_file << fmt::format("Delta: {} Frequency: {}\n",entry.first,entry.second);
  }
  delta_file.close();

  //lookahead frequencies
  std::ofstream lookahead_file;
  lookahead_file.open("sppam_b_lookahead_freq.txt",std::ios::out | std::ios::trunc);
  for(auto& entry : lookahead_freq) {
    lookahead_file << fmt::format("Lookaheads: {} Frequency: {}\n",entry.first,entry.second);
  }
  lookahead_file.close();
}

uint64_t sppam_b::Sppam_b_Module::get_state_bits() {
  //Assuming 48-bit max address
  //calculate total bits needed for Sppam_b_Module
  uint64_t total_bits = 0;

  //start with region table
  uint64_t region_table_bits = (48 - champsim::lg2(SPPAM_B_PAGE_BITS) - champsim::lg2(REGION_SETS)); //page num
  region_table_bits += ((1 << SPPAM_B_PAGE_BITS) / BLOCK_SIZE)*2; //bitmaps
  //region_table_bits += 4 //momentum (DISABLED)
  //region_table_bits += champsim::lg2(((1 << SPPAM_B_PAGE_BITS) / BLOCK_SIZE)); //last block
  region_table_bits += champsim::lg2(REGION_WAYS); //lru bits
  region_table_bits *= REGION_SETS * REGION_WAYS;


  //total state tally
  total_bits += region_table_bits;


  return total_bits;
}

void sppam_b::prefetcher_cycle_operate() {
  if(intern_->current_cycle() % 1000000 == 0) {
    fmt::print("[{}] SPPAM_B\n",intern_->NAME);
    fmt::print("\tBandwidth utilization: {} - {}%\n",(100/16.0)*get_dram_bw(),(100/16.0)*(get_dram_bw()+1));
    fmt::print("\tGlobal Confidence: {}\n",engine.global_conf);
    engine.predictor->print_heartbeat();
  }
  //reset stats when exiting warmup
  if(!intern_->warmup && engine.in_warmup) {
    fmt::print("[{}] SPPAM_B Resetting internal stats\n",intern_->NAME);
    engine.in_warmup = false;
    engine.prefetches_issued = 0;
    engine.total_lookaheads = 0;
    engine.prefetch_triggers = 0;
    engine.prefetches_filtered = 0;
    engine.prefetches_full_pq = 0;
    engine.prefetches_dropped = 0;
    engine.pf_reissued = 0;
    engine.pf_l2c = 0;
    engine.pend_cycles = 0;
    engine.resolutions = 0;
    engine.pf_degree_limited = 0;
  }
}

double sppam_b::Sppam_b_Module::get_bw_conf() {
  return PREFETCH_DEGREES_BW.at(current_bw_utilization);
}

double sppam_b::Sppam_b_Module::get_conf(champsim::address ip) {
  //fmt::print("Getting usefulness for pattern: {:b}\n",pattern);
  //check ip confidence table
  if(USE_IP_CONF) {
    auto entry = ip_conf_table.check_hit(ip_conf_entry{ip});
    if(entry.has_value()) {
      return entry->conf;
    } else {
      return global_conf;
    }
  }
  else
    return global_conf;
}

std::size_t sppam_b::Sppam_b_Module::get_lookahead(champsim::address ip) {
  auto entry = ip_conf_table.check_hit(ip_conf_entry{ip});
  if(entry.has_value()) {
    return entry->lookahead_depth;
  } else {
    return MAX_LOOKAHEAD;
  }
}

std::size_t sppam_b::Sppam_b_Module::get_pf_degree(champsim::address ip) {
  auto entry = ip_conf_table.check_hit(ip_conf_entry{ip});
  if(entry.has_value()) {
    return entry->pf_degree;
  } else {
    return PREFETCH_DEGREE;
  }
}

double sppam_b::Sppam_b_Module::should_issue(double conf) {
  //fmt::print("Getting usefulness for pattern: {:b}\n",pattern);
  int index = PREFETCH_ISSUE_CHANCE.size() * conf;
  index = PREFETCH_ISSUE_CHANCE.at(std::min(15,index)) * 128;

  return (intern_->current_cycle() % 128) < index;
}

double sppam_b::Sppam_b_Module::should_reissue(double conf) {
  int index = REISSUE_CHANCE.size() * conf;
  index = REISSUE_CHANCE.at(std::min(15,index)) * 128;

  return (rand() % 128) < index;
}

double sppam_b::Sppam_b_Module::convert_to_conf(double predict_conf) {
  return predict_conf;
}

void sppam_b::prefetcher_final_stats() {
  fmt::print("[{}] SPPAM_B Total Prefetches Issued: {} Filtered: {} Full PQ: {} Total Lookaheads: {}\n",intern_->NAME,engine.prefetches_issued,engine.prefetches_filtered,engine.prefetches_full_pq, engine.total_lookaheads);
  fmt::print("\tAverage Lookahead Depth: {} Dropped: {} Reissued: {} To L2C: {} Avg. Time-to-use: {} Limited PF Degree: {}\n",(engine.total_lookaheads/(float)engine.prefetch_triggers) + 1,engine.prefetches_dropped,engine.pf_reissued,engine.pf_l2c,engine.pend_cycles /(double)(engine.resolutions == 0 ? 1 : engine.resolutions),engine.pf_degree_limited);
  engine.print_patterns();
  engine.predictor->print_stats();
}