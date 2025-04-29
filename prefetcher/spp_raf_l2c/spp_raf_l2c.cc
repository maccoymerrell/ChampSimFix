#include "spp_raf_l2c.h"
#include "dram_controller.h"

#include <cassert>
#include <iostream>

std::vector<spp_raf_l2c*> spp_raf_l2c::spp_impls;

void spp_raf_l2c::prefetcher_initialize()
{
  std::cout << "Initialize SIGNATURE TABLE" << std::endl;
  std::cout << "ST_SET: " << ST_SET << std::endl;
  std::cout << "ST_WAY: " << ST_WAY << std::endl;
  std::cout << "ST_TAG_BIT: " << ST_TAG_BIT << std::endl;

  std::cout << std::endl << "Initialize PATTERN TABLE" << std::endl;
  std::cout << "PT_SET: " << PT_SET << std::endl;
  std::cout << "PT_WAY: " << PT_WAY << std::endl;
  std::cout << "SIG_DELTA_BIT: " << SIG_DELTA_BIT << std::endl;
  std::cout << "C_SIG_BIT: " << C_SIG_BIT << std::endl;
  std::cout << "C_DELTA_BIT: " << C_DELTA_BIT << std::endl;

  std::cout << std::endl << "Initialize PREFETCH FILTER" << std::endl;
  std::cout << "FILTER_SET: " << FILTER_SET << std::endl;
  std::cout << "RAM SETS: " << RAM_SETS << std::endl;
  std::cout << "RAM WAYS: " << RAM_WAYS << std::endl;
  std::cout << "NRAM SETS: " << NRAM_SETS << std::endl;
  std::cout << "NRAM WAYS: " << NRAM_WAYS << std::endl << std::endl;


  //pass pointers
  ST._parent = this;
  PT._parent = this;
  FILTER._parent = this;
  GHR._parent = this;

  //add to static list, important for filter updates
  spp_impls.push_back(this);

}

void spp_raf_l2c::prefetcher_cycle_operate() { 
  current_cycle++;
  if(FILTER.reset_timer == 0) {
    fmt::print("reset bloom filters after period {}...\n",champsim::chrono::milliseconds(BLOOM_RESET_INTERVAL_MS) / intern_->clock_period);
    FILTER.rat_bloom_filter.reset_all();
    FILTER.reset_timer = champsim::chrono::milliseconds(BLOOM_RESET_INTERVAL_MS) / intern_->clock_period;
  }
  FILTER.reset_timer--;

  //issue prefetch per cycle
  if(!PQ.is_empty()) {
    std::tuple<bool,champsim::address,uint64_t> to_prefetch = PQ.front();
    bool prefetch_fill = std::get<0>(to_prefetch);
    champsim::address pf_addr = std::get<1>(to_prefetch);
    uint64_t time = std::get<2>(to_prefetch);

    if(current_cycle - time > 20) {
      //apply filter to address as it leaves prefetch queue
      //if(!FILTER.filter_prefetch_rat(pf_addr)) {
        //prefetch this address
        bool success = prefetch_line(pf_addr,prefetch_fill,0);
        if(success)
          PQ.pop();
      //}
      //else {
      //  PQ.pop();
      //}
    }
  }
}


uint32_t spp_raf_l2c::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type, uint32_t metadata_in)
{
  champsim::page_number page{addr};
  uint32_t last_sig = 0, curr_sig = 0, depth = 0;
  std::vector<uint32_t> confidence_q(intern_->get_mshr_size());

  //if(type == access_type::LOAD && cache_hit == 0) {
  //  FILTER.reset_nram_table(addr);
  //}

  if(type != access_type::PREFETCH && cache_hit == 0)
    FILTER.inval_ptb_table(addr);

  if(type == access_type::LOAD)
    FILTER.demand_issue_rad_table(addr);

  FILTER.cycle_operate_rad_table();

  typename spp_raf_l2c::offset_type::difference_type delta = 0;
  std::vector<typename spp_raf_l2c::offset_type::difference_type> delta_q(intern_->get_mshr_size());

  for (uint32_t i = 0; i < intern_->get_mshr_size(); i++) {
    confidence_q[i] = 0;
    delta_q[i] = 0;
  }
  confidence_q[0] = 100;
  GHR.global_accuracy = GHR.pf_issued ? ((100 * GHR.pf_useful) / GHR.pf_issued) : 0;

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << std::endl << "[ChampSim] " << __func__ << " addr: " << addr;
    std::cout << " page: " << page << std::endl;
  }

  // Stage 1: Read and update a sig stored in ST
  // last_sig and delta are used to update (sig, delta) correlation in PT
  // curr_sig is used to read prefetch candidates in PT
  ST.read_and_update_sig(addr, last_sig, curr_sig, delta);

  // Also check the prefetch filter in parallel to update global accuracy counters
  FILTER.check(addr, spp_raf_l2c::L2C_DEMAND);

  // Stage 2: Update delta patterns stored in PT
  if (last_sig)
    PT.update_pattern(last_sig, delta);

  // Stage 3: Start prefetching
  auto base_addr = addr;
  uint32_t lookahead_conf = 100, pf_q_head = 0, pf_q_tail = 0;
  uint8_t do_lookahead = 0;

  do {
    uint32_t lookahead_way = PT_WAY;
    PT.read_pattern(curr_sig, delta_q, confidence_q, lookahead_way, lookahead_conf, pf_q_tail, depth);

    do_lookahead = 0;
    for (uint32_t i = pf_q_head; i < pf_q_tail; i++) {

      if(confidence_q[i] <= 0)
        continue;
      champsim::address pf_addr{champsim::block_number{base_addr} + delta_q[i]};

      if (confidence_q[i] >= PF_THRESHOLD) {

        if (champsim::page_number{pf_addr} == page) { // Prefetch request is in the same physical page
          if (FILTER.check(pf_addr, ((confidence_q[i] >= FILL_THRESHOLD) ? spp_raf_l2c::SPP_L2C_PREFETCH : spp_raf_l2c::SPP_LLC_PREFETCH),confidence_q[i])) {
              //PQ.push(pf_addr,(confidence_q[i] >= FILL_THRESHOLD),current_cycle);
              //if(FILTER.check_rat_table(pf_addr))
              //  prefetch_line(pf_addr, (confidence_q[i] >= FILL_THRESHOLD), 0);
              //else
              //FILTER.cache_operate_rad_table(pf_addr,confidence_q[i] >= FILL_THRESHOLD,0);
              if(!FILTER.check_ptb_table(pf_addr)) {
                prefetch_line(pf_addr, (confidence_q[i] >= FILL_THRESHOLD), 0);
                FILTER.update_ptb_table(pf_addr);
              }

            if (confidence_q[i] >= FILL_THRESHOLD) {
              GHR.pf_issued++;
              if (GHR.pf_issued > GLOBAL_COUNTER_MAX) {
                GHR.pf_issued >>= 1;
                GHR.pf_useful >>= 1;
              }
              if constexpr (SPP_DEBUG_PRINT) {
                std::cout << "[ChampSim] SPP L2 prefetch issued GHR.pf_issued: " << GHR.pf_issued << " GHR.pf_useful: " << GHR.pf_useful << std::endl;
              }
            }

            if constexpr (SPP_DEBUG_PRINT) {
              std::cout << "[ChampSim] " << __func__ << " base_addr: " << base_addr << " pf_addr: " << pf_addr;
              std::cout << " prefetch_delta: " << delta_q[i] << " confidence: " << confidence_q[i];
              std::cout << " depth: " << i << std::endl;
            }
          }
        } else { // Prefetch request is crossing the physical page boundary
          if constexpr (GHR_ON) {
            // Store this prefetch request in GHR to bootstrap SPP learning when
            // we see a ST miss (i.e., accessing a new page)
            GHR.update_entry(curr_sig, confidence_q[i], spp_raf_l2c::offset_type{pf_addr}, delta_q[i]);
          }
        }

        do_lookahead = 1;
        pf_q_head++;
      }
    }

    // Update base_addr and curr_sig
    if (lookahead_way < PT_WAY) {
      uint32_t set = get_hash(curr_sig) % PT_SET;
      base_addr += (PT.delta[set][lookahead_way] << LOG2_BLOCK_SIZE);

      // PT.delta uses a 7-bit sign magnitude representation to generate
      // sig_delta
      // int sig_delta = (PT.delta[set][lookahead_way] < 0) ? ((((-1) *
      // PT.delta[set][lookahead_way]) & 0x3F) + 0x40) :
      // PT.delta[set][lookahead_way];
      auto sig_delta =
          (PT.delta[set][lookahead_way] < 0) ? (((-1) * PT.delta[set][lookahead_way]) + (1 << (SIG_DELTA_BIT - 1))) : PT.delta[set][lookahead_way];
      curr_sig = ((curr_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
    }

    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << "Looping curr_sig: " << std::hex << curr_sig << " base_addr: " << base_addr << std::dec;
      std::cout << " pf_q_head: " << pf_q_head << " pf_q_tail: " << pf_q_tail << " depth: " << depth << std::endl;
    }
  } while (LOOKAHEAD_ON && do_lookahead);

  return metadata_in;
}

uint32_t spp_raf_l2c::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{

  //prefetch dropped
  if(way == intern_->NUM_WAY && evicted_addr == champsim::address{})
    return metadata_in;
  if constexpr (FILTER_ON) {
    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << std::endl;
    }
    FILTER.check(evicted_addr, spp_raf_l2c::L2C_EVICT);
  }

  return metadata_in;
}

void spp_raf_l2c::prefetcher_final_stats() {

  fmt::print("SPP-RAT:\n");
  fmt::print("\tRAT DROPPED %: {}\n",(FILTER.filtered / (double)FILTER.total) * 100.0);
  fmt::print("\tRAT DROPPED: {} TOTAL: {}\n",FILTER.filtered,FILTER.total);
  fmt::print("\t---- RAT TABLE CONTENTS ----\n");
  for(auto it : FILTER.rat_table.get_contents()) {
    fmt::print("\trow_id: {} bank: {}\n",it.data.row_id, it.data.bank_id);
  }
  //FILTER.rat_bloom_filter.print();
  //FILTER.rat_sact_counter.print();
}


// TODO: Find a good 64-bit hash function
uint64_t spp_raf_l2c::get_hash(uint64_t key)
{
  // Robert Jenkins' 32 bit mix function
  key += (key << 12);
  key ^= (key >> 22);
  key += (key << 4);
  key ^= (key >> 9);
  key += (key << 10);
  key ^= (key >> 2);
  key += (key << 7);
  key ^= (key >> 12);

  // Knuth's multiplicative method
  key = (key >> 3) * 2654435761;

  return key;
}

void spp_raf_l2c::SIGNATURE_TABLE::read_and_update_sig(champsim::address addr, uint32_t& last_sig, uint32_t& curr_sig, typename offset_type::difference_type& delta)
{
  auto set = get_hash(champsim::page_number{addr}.to<uint64_t>()) % ST_SET;
  auto match = ST_WAY;
  tag_type partial_page{addr};
  offset_type page_offset{addr};
  uint8_t ST_hit = 0;
  long sig_delta = 0;

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[ST] " << __func__ << " page: " << champsim::page_number{addr} << " partial_page: " << std::hex << partial_page << std::dec << std::endl;
  }

  // Case 2: Invalid
  if (match == ST_WAY) {
    for (match = 0; match < ST_WAY; match++) {
      if (valid[set][match] && (tag[set][match] == partial_page)) {
        last_sig = sig[set][match];
        delta = champsim::offset(last_offset[set][match], page_offset);

        if (delta) {
          // Build a new sig based on 7-bit sign magnitude representation of delta
          // sig_delta = (delta < 0) ? ((((-1) * delta) & 0x3F) + 0x40) : delta;
          sig_delta = (delta < 0) ? (((-1) * delta) + (1 << (SIG_DELTA_BIT - 1))) : delta;
          sig[set][match] = ((last_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
          curr_sig = sig[set][match];
          last_offset[set][match] = page_offset;

          if constexpr (SPP_DEBUG_PRINT) {
            std::cout << "[ST] " << __func__ << " hit set: " << set << " way: " << match;
            std::cout << " valid: " << valid[set][match] << " tag: " << std::hex << tag[set][match];
            std::cout << " last_sig: " << last_sig << " curr_sig: " << curr_sig;
            std::cout << " delta: " << std::dec << delta << " last_offset: " << page_offset << std::endl;
          }
        } else
          last_sig = 0; // Hitting the same cache line, delta is zero

        ST_hit = 1;
        break;
      }
    }
  }

  // Case 2: Invalid
  if (match == ST_WAY) {
    for (match = 0; match < ST_WAY; match++) {
      if (valid[set][match] == 0) {
        valid[set][match] = 1;
        tag[set][match] = partial_page;
        sig[set][match] = 0;
        curr_sig = sig[set][match];
        last_offset[set][match] = page_offset;

        if constexpr (SPP_DEBUG_PRINT) {
          std::cout << "[ST] " << __func__ << " invalid set: " << set << " way: " << match;
          std::cout << " valid: " << valid[set][match] << " tag: " << std::hex << partial_page;
          std::cout << " sig: " << sig[set][match] << " last_offset: " << std::dec << page_offset << std::endl;
        }

        break;
      }
    }
  }

  if constexpr (SPP_SANITY_CHECK) {
    // Assertion
    if (match == ST_WAY) {
      for (match = 0; match < ST_WAY; match++) {
        if (lru[set][match] == ST_WAY - 1) { // Find replacement victim
          tag[set][match] = partial_page;
          sig[set][match] = 0;
          curr_sig = sig[set][match];
          last_offset[set][match] = page_offset;

          if constexpr (SPP_DEBUG_PRINT) {
            std::cout << "[ST] " << __func__ << " miss set: " << set << " way: " << match;
            std::cout << " valid: " << valid[set][match] << " victim tag: " << std::hex << tag[set][match] << " new tag: " << partial_page;
            std::cout << " sig: " << sig[set][match] << " last_offset: " << std::dec << page_offset << std::endl;
          }

          break;
        }
      }

      // Assertion
      if (match == ST_WAY) {
        std::cout << "[ST] Cannot find a replacement victim!" << std::endl;
        assert(0);
      }
    }
  }

  if constexpr (GHR_ON) {
    if (ST_hit == 0) {
      uint32_t GHR_found = _parent->GHR.check_entry(page_offset);
      if (GHR_found < MAX_GHR_ENTRY) {
        sig_delta = (_parent->GHR.delta[GHR_found] < 0) ? (((-1) * _parent->GHR.delta[GHR_found]) + (1 << (SIG_DELTA_BIT - 1))) : _parent->GHR.delta[GHR_found];
        sig[set][match] = ((_parent->GHR.sig[GHR_found] << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
        curr_sig = sig[set][match];
      }
    }
  }

  // Update LRU
  for (uint32_t way = 0; way < ST_WAY; way++) {
    if (lru[set][way] < lru[set][match]) {
      lru[set][way]++;

      if constexpr (SPP_SANITY_CHECK) {
        // Assertion
        if (lru[set][way] >= ST_WAY) {
          std::cout << "[ST] LRU value is wrong! set: " << set << " way: " << way << " lru: " << lru[set][way] << std::endl;
          assert(0);
        }
      }
    }
  }

  lru[set][match] = 0; // Promote to the MRU position
}

void spp_raf_l2c::PATTERN_TABLE::update_pattern(uint32_t last_sig, typename offset_type::difference_type curr_delta)
{
  // Update (sig, delta) correlation
  uint32_t set = get_hash(last_sig) % PT_SET, match = 0;

  // Case 1: Hit
  for (match = 0; match < PT_WAY; match++) {
    if (delta[set][match] == curr_delta) {
      c_delta[set][match]++;
      c_sig[set]++;
      if (c_sig[set] > C_SIG_MAX) {
        for (uint32_t way = 0; way < PT_WAY; way++)
          c_delta[set][way] >>= 1;
        c_sig[set] >>= 1;
      }

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[PT] " << __func__ << " hit sig: " << std::hex << last_sig << std::dec << " set: " << set << " way: " << match;
        std::cout << " delta: " << delta[set][match] << " c_delta: " << c_delta[set][match] << " c_sig: " << c_sig[set] << std::endl;
      }

      break;
    }
  }

  // Case 2: Miss
  if (match == PT_WAY) {
    uint32_t victim_way = PT_WAY, min_counter = C_SIG_MAX;

    for (match = 0; match < PT_WAY; match++) {
      if (c_delta[set][match] < min_counter) { // Select an entry with the minimum c_delta
        victim_way = match;
        min_counter = c_delta[set][match];
      }
    }

    delta[set][victim_way] = curr_delta;
    c_delta[set][victim_way] = 0;
    c_sig[set]++;
    if (c_sig[set] > C_SIG_MAX) {
      for (uint32_t way = 0; way < PT_WAY; way++)
        c_delta[set][way] >>= 1;
      c_sig[set] >>= 1;
    }

    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << "[PT] " << __func__ << " miss sig: " << std::hex << last_sig << std::dec << " set: " << set << " way: " << victim_way;
      std::cout << " delta: " << delta[set][victim_way] << " c_delta: " << c_delta[set][victim_way] << " c_sig: " << c_sig[set] << std::endl;
    }

    if constexpr (SPP_SANITY_CHECK) {
      // Assertion
      if (victim_way == PT_WAY) {
        std::cout << "[PT] Cannot find a replacement victim!" << std::endl;
        assert(0);
      }
    }
  }
}

void spp_raf_l2c::PATTERN_TABLE::read_pattern(uint32_t curr_sig, std::vector<typename offset_type::difference_type>& delta_q, std::vector<uint32_t>& confidence_q,
                                      uint32_t& lookahead_way, uint32_t& lookahead_conf, uint32_t& pf_q_tail, uint32_t& depth)
{
  // Update (sig, delta) correlation
  uint32_t set = get_hash(curr_sig) % PT_SET, local_conf = 0, pf_conf = 0, max_conf = 0;

  if (c_sig[set]) {
    for (uint32_t way = 0; way < PT_WAY; way++) {
      local_conf = (100 * c_delta[set][way]) / c_sig[set];
      pf_conf = depth ? (_parent->GHR.global_accuracy * c_delta[set][way] / c_sig[set] * lookahead_conf / 100) : local_conf;

      if (pf_conf >= PF_THRESHOLD) {
        confidence_q[pf_q_tail] = pf_conf;
        delta_q[pf_q_tail] = delta[set][way];

        // Lookahead path follows the most confident entry
        if (pf_conf > max_conf) {
          lookahead_way = way;
          max_conf = pf_conf;
        }
        pf_q_tail++;

        if constexpr (SPP_DEBUG_PRINT) {
          std::cout << "[PT] " << __func__ << " HIGH CONF: " << pf_conf << " sig: " << std::hex << curr_sig << std::dec << " set: " << set << " way: " << way;
          std::cout << " delta: " << delta[set][way] << " c_delta: " << c_delta[set][way] << " c_sig: " << c_sig[set];
          std::cout << " conf: " << local_conf << " depth: " << depth << std::endl;
        }
      } else {
        if constexpr (SPP_DEBUG_PRINT) {
          std::cout << "[PT] " << __func__ << "  LOW CONF: " << pf_conf << " sig: " << std::hex << curr_sig << std::dec << " set: " << set << " way: " << way;
          std::cout << " delta: " << delta[set][way] << " c_delta: " << c_delta[set][way] << " c_sig: " << c_sig[set];
          std::cout << " conf: " << local_conf << " depth: " << depth << std::endl;
        }
      }
    }
    pf_q_tail++;

    lookahead_conf = max_conf;
    if (lookahead_conf >= PF_THRESHOLD)
      depth++;

    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << "global_accuracy: " << _parent->GHR.global_accuracy << " lookahead_conf: " << lookahead_conf << std::endl;
    }
  } else {
    confidence_q[pf_q_tail] = 0;
  }
}

bool spp_raf_l2c::PREFETCH_FILTER::raf_check(champsim::address pf_addr, unsigned long confidence) {

  total++;

  bool filter = confidence < 70 && confidence > 40 ? (check_nram_table(pf_addr)): false;
  if(filter)
    filtered++;
  return(filter);
}


bool spp_raf_l2c::PREFETCH_FILTER::check_ram_table(champsim::address pf_addr) {
  auto row = ram_table.check_hit(ram_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),0});

  if(row.has_value()) {
    return(row->groups.test(raf_rb(pf_addr) % RAM_VECTOR));
  }
  else
    return(true);
}

void spp_raf_l2c::PREFETCH_FILTER::set_ram_table(champsim::address pf_addr) {
  auto row = ram_table.check_hit(ram_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),0});
  
  if(!row.has_value())
    row = ram_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),_parent->current_cycle};

  row->groups.set(raf_rb(pf_addr) % RAM_VECTOR);
  ram_table.fill(row.value());
  
}

void spp_raf_l2c::PREFETCH_FILTER::set_rat_table(champsim::address addr, bool is_prefetch) {
  unsigned long row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  unsigned long rb = raf_rb(addr);
  auto entry = rat_table.check_hit(rat_table_entry{row,rb,0});
  //fmt::print("Addr was given: {} row: {} rb: {} hit table? : {}\n",addr,row,rb,entry.has_value());
  if(!entry.has_value()) {
    rat_table.fill(rat_table_entry{row,rb,_parent->current_cycle});
    //set bloom filter
    /*
    if(is_prefetch) {
      //set bloom filter with probability BLOOM_SET_PROB
      if(rand() % llround((1/BLOOM_SET_PROB)) == 0) {
        rat_bloom_filter.set(rb,champsim::address{row});
        //fmt::print("blacklisting row (prefetch): {} rb: {} which had prob 1/{}\n",row,rb,llround((1/BLOOM_SET_PROB)));
      }
    }
    //set bloom filter
    if(!is_prefetch) {
      //set bloom filter with probability BLOOM_SET_PROB
      if(rand() % llround((1/BLOOM_RESET_PROB)) == 0) {
        if(rat_bloom_filter.check(rb,champsim::address{row}))
          rat_bloom_filter.reset(rb,champsim::address{row});
        //fmt::print("whitelisting row (demand): {} rb: {} which had prob 1/{}\n",row,rb,llround((1/BLOOM_RESET_PROB)));
      }
    }*/
    //update activation counters
    //if(is_prefetch)
    //  rat_sact_counter.increment(rb);
    //if(rat_act_counter.increment(rb)) {
    //  rat_bloom_filter.reset(rb);
    // rat_sact_counter.reset(rb);
    //fmt::print("reset for rb: {}\n",rb);
    //}
  }
}

void spp_raf_l2c::PREFETCH_FILTER::reset_filter(champsim::address addr) {
  unsigned long row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  unsigned long rb = raf_rb(addr);
  //fmt::print("reset for addr: {}\n",addr);
  //rat_bloom_filter.reset(rb);
  //rat_sact_counter.reset(rb);
}

bool spp_raf_l2c::PREFETCH_FILTER::filter_prefetch_rat(champsim::address addr) {
  //check bloom filter. If saturated for given entries, filter out
  unsigned long row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  unsigned long rb = raf_rb(addr);
  bool filter = false;
  if(!rat_table.check_hit(rat_table_entry{row,rb,0}).has_value())
    //filter = rat_sact_counter.check(rb);
    filter = rat_bloom_filter.check(rb,champsim::address{row});
  if(filter)
    filtered++;
  total++;
  
  return(filter);
  //return(!rat_table.check_hit(rat_table_entry{row,rb,0}).has_value());
}

bool spp_raf_l2c::PREFETCH_FILTER::check_rat_table(champsim::address addr) {
  unsigned long row = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(addr);
  unsigned long rb = raf_rb(addr);

  return rat_table.check_hit(rat_table_entry{row,rb,0}).has_value();
}

void spp_raf_l2c::PREFETCH_FILTER::add_rad_table(champsim::address pf_addr, bool fill_this_level, uint32_t metadata) {
  auto entry = rad_table.check_hit(rad_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),raf_rb(pf_addr),0});
  if(!entry.has_value()) {
    entry = rad_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),raf_rb(pf_addr),_parent->current_cycle};
  }
  entry->blocks[get_column_block(pf_addr)] = pf_addr;
  entry->fill_this_level[get_column_block(pf_addr)] = fill_this_level && entry->fill_this_level[get_column_block(pf_addr)];
  entry->metadata[get_column_block(pf_addr)] = metadata; 

  rad_table_entry evic;
  rad_table.fill(entry.value(),evic);

  if(evic.first_used != 0) {
    //fmt::print("Serving evicted table\n");
    issue_rad_table(evic);
    //fmt::print("Done\n");
  }
}

void spp_raf_l2c::PREFETCH_FILTER::demand_issue_rad_table(champsim::address pf_addr) {
  auto entry = rad_table.check_hit(rad_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),raf_rb(pf_addr),0});

  if(entry.has_value()) {
    //fmt::print("Demand issuing for RAD table entry: row: {} group: {}\n",entry->row_id,entry->bank_id);
    entry->blocks[get_column_block(pf_addr)] = champsim::address{0};
    issue_rad_table(entry.value());
  }
}

uint64_t spp_raf_l2c::PREFETCH_FILTER::get_column_block(champsim::address pf_addr) {

  uint64_t column_block = MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_column(pf_addr);
  //fmt::print("Converted pf_addr:{} to column:{}\n",pf_addr,column_block);
  return(column_block);
}

void spp_raf_l2c::PREFETCH_FILTER::inval_rad_table(rad_table_entry rte) {
  rad_table.invalidate(rte);
  //fmt::print("Invalidated RAD table entry: row: {} group:{}\n",rte.row_id,rte.bank_id);
}

void spp_raf_l2c::PREFETCH_FILTER::cache_operate_rad_table(champsim::address pf_addr, bool fill_this_level, uint32_t metadata) {
  add_rad_table(pf_addr, fill_this_level, metadata);
  auto entry = rad_table.check_hit(rad_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),raf_rb(pf_addr),0});

  thresh_issue_rad_table(entry.value());
}
void spp_raf_l2c::PREFETCH_FILTER::cycle_operate_rad_table() {
  for(auto i : rad_table.get_contents())
    if(i.data.first_used != 0)
      time_issue_rad_table(i.data);
}

void spp_raf_l2c::PREFETCH_FILTER::inval_ptb_table(champsim::address pf_addr) {
  ptb_table.invalidate(ptb_entry{champsim::block_number{pf_addr},0});
}
void spp_raf_l2c::PREFETCH_FILTER::update_ptb_table(champsim::address pf_addr) {
  ptb_table.fill(ptb_entry{champsim::block_number{pf_addr},_parent->current_cycle});
}
bool spp_raf_l2c::PREFETCH_FILTER::check_ptb_table(champsim::address pf_addr) {

  auto entry = ptb_table.check_hit(ptb_entry{champsim::block_number{pf_addr},0});

  if(entry.has_value()) {
    if(_parent->current_cycle - entry->first_entered > 100) {
      inval_ptb_table(pf_addr);
      return false;
    }
    return true;
  }
  return false;
}

void spp_raf_l2c::PREFETCH_FILTER::time_issue_rad_table(rad_table_entry rte) {
  
  //uint64_t max_delay = rat_table.check_hit(rat_table_entry{rte.row_id,rte.bank_id,0}).has_value() ? RAD_DELAY_HIT : RAD_DELAY_MISS;
  uint64_t max_delay = RAD_DELAY_HIT;
  if(_parent->current_cycle - rte.first_used >= max_delay) {
    //fmt::print("Time issuing for RAD table entry: row: {} group: {}\n",rte.row_id,rte.bank_id);
    issue_rad_table(rte);
  }
}
void spp_raf_l2c::PREFETCH_FILTER::issue_rad_table(rad_table_entry rte) {
  //fmt::print("Issuing RAD table entry: row: {} group:{}\n",rte.row_id,rte.bank_id);
  for(uint64_t i = 0; i < RAD_VECTOR; i++) {
    if(rte.blocks[i] != champsim::address{0})
    {
      //issue prefetch for this block
      _parent->prefetch_line(rte.blocks[i],rte.fill_this_level[i],rte.metadata[i]);
      //fmt::print("\tPrefetched address: {} fill_this_level: {} column: {} delayed: {}\n",rte.blocks[i],rte.fill_this_level[i],i,_parent->current_cycle - rte.first_used);
    }
  }
  rad_table.invalidate(rte);
}
void spp_raf_l2c::PREFETCH_FILTER::thresh_issue_rad_table(rad_table_entry rte) {
  
  std::size_t entries = std::count_if(rte.blocks.begin(), rte.blocks.end(), [](champsim::address i) { return i != champsim::address{0}; });
  

  if(entries >= RAD_ISSUE_THRESH) {
    fmt::print("Threshold issuing for RAD table entry: row: {} group: {} entries: {}\n",rte.row_id,rte.bank_id,entries);
    issue_rad_table(rte);
  }
}

bool spp_raf_l2c::PREFETCH_FILTER::check_nram_table(champsim::address pf_addr) {
  auto row = nram_table.check_hit(ram_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),0});

  if(row.has_value()) {
    return(row->groups.test(raf_rb(pf_addr) % NRAM_VECTOR));
  }
  else
    return(false);
}

uint64_t spp_raf_l2c::PREFETCH_FILTER::get_ram_conf(uint64_t confidence, champsim::address pf_addr) {
  //get entry from nram table
  //auto row = nram_table.check_hit(ram_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),0});
  auto row = rat_table.check_hit(rat_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),raf_rb(pf_addr),0});
  long long int bias = 0;
  uint64_t new_confidence = confidence;
  total++;

  //if it exists, set bias according to filter
  //if(row.has_value())
    //bias = row->groups.test(raf_rb(pf_addr) % NRAM_VECTOR) ? NRAM_FILT : NRAM_PROM;
  bias = row.has_value() ? NRAM_PROM : NRAM_FILT;

  //don't demote high-confidence prefetches
  if(confidence >= FILL_THRESHOLD)
    bias = std::max(bias,0ll);

  //clamp confidence value
  if(bias < 0 && confidence < std::abs(bias))
    new_confidence = 0;
  else if(bias > 0 && confidence > 100 - bias) {
    new_confidence = 100;
  }
  else
    new_confidence = confidence + bias;

  //collect stats
  if(new_confidence != confidence) {
    if(new_confidence < PF_THRESHOLD && confidence >= PF_THRESHOLD)
      filtered++;
    else if(confidence < PF_THRESHOLD && new_confidence >= PF_THRESHOLD)
      promoted++;
    else
      modified++;
  }

  //return result
  return(new_confidence);
}

void spp_raf_l2c::PREFETCH_FILTER::set_nram_table(champsim::address pf_addr) {
  auto row = nram_table.check_hit(ram_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),0});
  
  if(!row.has_value()) {
    row = ram_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),_parent->current_cycle};
    
  }
  row->groups.set(raf_rb(pf_addr) % NRAM_VECTOR);
  nram_table.fill(row.value());
  
}

void spp_raf_l2c::PREFETCH_FILTER::reset_nram_table(champsim::address pf_addr) {
  auto row = nram_table.check_hit(ram_table_entry{MEMORY_CONTROLLER::DRAM_CONTROLLER.value()->dram_get_row(pf_addr),0});
  
  if(row.has_value()) {
    row->groups.reset(raf_rb(pf_addr) % NRAM_VECTOR);

    if(row->groups.none())
    nram_table.invalidate(row.value());
    else
    nram_table.fill(row.value()); 
  }
  
}

uint64_t spp_raf_l2c::PREFETCH_FILTER::calc_nram_hash(champsim::address pf_addr, unsigned int ind) {

  uint64_t hash_val = get_hash(pf_addr.to<uint64_t>());
  if(ind == 2) {
    uint64_t temp_hash = hash_val;
    hash_val ^= (temp_hash >> 32);
    hash_val ^= (temp_hash << 32);
  }
  if(ind == 3) {
    uint64_t temp_hash = hash_val;
    hash_val ^= (temp_hash >> 48);
    hash_val ^= (temp_hash << 16);
  }
  //fmt::print("hash val is: {}\n",hash_val);
  return(hash_val);
}

unsigned int spp_raf_l2c::PREFETCH_FILTER::raf_rb(champsim::address pf_addr) {
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
  
  //fmt::print("row buffer id = {}\n",rb_id % RAF_RB_ENTRIES);
  return(rb_id);
}

bool spp_raf_l2c::PREFETCH_FILTER::check(champsim::address check_addr, FILTER_REQUEST filter_request, unsigned int confidence)
{
  champsim::block_number cache_line{check_addr};
  auto hash = get_hash(cache_line.to<uint64_t>());
  auto quotient = (hash >> REMAINDER_BIT) & ((1 << QUOTIENT_BIT) - 1);
  auto remainder = hash % (1 << REMAINDER_BIT);

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[FILTER] check_addr: " << check_addr << " hash: " << hash << " quotient: " << quotient << " remainder: " << remainder << std::endl;
  }

  switch (filter_request) {
  case spp_raf_l2c::SPP_L2C_PREFETCH:
    if ((valid[quotient] || useful[quotient]) && remainder_tag[quotient] == remainder) {
      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " line is already in the filter check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl;
      }

      return false; // False return indicates "Do not prefetch"
    } else {
      valid[quotient] = 1;  // Mark as prefetched
      useful[quotient] = 0; // Reset useful bit
      remainder_tag[quotient] = remainder;

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " set valid for check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " remainder_tag: " << remainder_tag[quotient] << " valid: " << valid[quotient]
                  << " useful: " << useful[quotient] << std::endl;
      }
    }
    break;

  case spp_raf_l2c::SPP_LLC_PREFETCH:
    if ((valid[quotient] || useful[quotient]) && remainder_tag[quotient] == remainder) {
      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " line is already in the filter check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl;
      }

      return false; // False return indicates "Do not prefetch"
    } else {
      // NOTE: SPP_LLC_PREFETCH has relatively low confidence (FILL_THRESHOLD <= SPP_LLC_PREFETCH < PF_THRESHOLD)
      // Therefore, it is safe to prefetch this cache line in the large LLC and save precious L2C capacity
      // If this prefetch request becomes more confident and SPP eventually issues SPP_L2C_PREFETCH,
      // we can get this cache line immediately from the LLC (not from DRAM)
      // To allow this fast prefetch from LLC, SPP does not set the valid bit for SPP_LLC_PREFETCH

      // valid[quotient] = 1;
      // useful[quotient] = 0;
      //if(raf_check(check_addr,confidence)) {
      //  return(false);
      //}

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " don't set valid for check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl;
      }
    }
    break;

  case spp_raf_l2c::L2C_DEMAND:
    if ((remainder_tag[quotient] == remainder) && (useful[quotient] == 0)) {
      useful[quotient] = 1;
      if (valid[quotient])
        _parent->GHR.pf_useful++; // This cache line was prefetched by SPP and actually used in the program

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " set useful for check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient];
        std::cout << " GHR.pf_issued: " << _parent->GHR.pf_issued << " GHR.pf_useful: " << _parent->GHR.pf_useful << std::endl;
      }
    }
    break;

  case spp_raf_l2c::L2C_EVICT:
    // Decrease global pf_useful counter when there is a useless prefetch (prefetched but not used)
    if (valid[quotient] && !useful[quotient] && _parent->GHR.pf_useful)
      _parent->GHR.pf_useful--;

    // Reset filter entry
    valid[quotient] = 0;
    useful[quotient] = 0;
    remainder_tag[quotient] = 0;
    break;

  default:
    // Assertion
    std::cout << "[FILTER] Invalid filter request type: " << filter_request << std::endl;
    assert(0);
  }
  //if(filter_prefetch_rat(check_addr))
  //return(false);
  return true;
}

void spp_raf_l2c::GLOBAL_REGISTER::update_entry(uint32_t pf_sig, uint32_t pf_confidence, offset_type pf_offset, typename offset_type::difference_type pf_delta)
{
  bool min_conf_set = false; 
  // NOTE: GHR implementation is slightly different from the original paper
  // Instead of matching (last_offset + delta), GHR simply stores and matches the pf_offset
  uint32_t min_conf = 100, victim_way = MAX_GHR_ENTRY;

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[GHR] Crossing the page boundary pf_sig: " << std::hex << pf_sig << std::dec;
    std::cout << " confidence: " << pf_confidence << " pf_offset: " << pf_offset << " pf_delta: " << pf_delta << std::endl;
  }

  for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
    // if (sig[i] == pf_sig) { // TODO: Which one is better and consistent?
    //  If GHR already holds the same pf_sig, update the GHR entry with the latest info
    if (valid[i] && (offset[i] == pf_offset)) {
      // If GHR already holds the same pf_offset, update the GHR entry with the latest info
      sig[i] = pf_sig;
      confidence[i] = pf_confidence;
      // offset[i] = pf_offset;
      delta[i] = pf_delta;

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[GHR] Found a matching index: " << i << std::endl;
      }

      return;
    }

    // GHR replacement policy is based on the stored confidence value
    // An entry with the lowest confidence is selected as a victim
    if (confidence[i] < min_conf|| !min_conf_set) {
      min_conf = confidence[i];
      victim_way = i;
      min_conf_set = true;
    }
  }

  // Assertion
  if (victim_way >= MAX_GHR_ENTRY) {
    std::cout << "[GHR] Cannot find a replacement victim!" << std::endl;
    return;
    }

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[GHR] Replace index: " << victim_way << " pf_sig: " << std::hex << sig[victim_way] << std::dec;
    std::cout << " confidence: " << confidence[victim_way] << " pf_offset: " << offset[victim_way] << " pf_delta: " << delta[victim_way] << std::endl;
  }

  valid[victim_way] = 1;
  sig[victim_way] = pf_sig;
  confidence[victim_way] = pf_confidence;
  offset[victim_way] = pf_offset;
  delta[victim_way] = pf_delta;
}

uint32_t spp_raf_l2c::GLOBAL_REGISTER::check_entry(offset_type page_offset)
{
  uint32_t max_conf = 0, max_conf_way = MAX_GHR_ENTRY;

  for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
    if ((offset[i] == page_offset) && (max_conf < confidence[i])) {
      max_conf = confidence[i];
      max_conf_way = i;
    }
  }

  return max_conf_way;
}
