#include "bouquet_l2c.h"
/*****************************************************
For the Third Data Prefetching Championship - DPC3

Paper ID: #4
Instruction Pointer Classifying Prefetcher - IPCP

Authors:
Samuel Pakalapati - pakalapatisamuel@gmail.com
Biswabandan Panda - biswap@cse.iitk.ac.in
******************************************************/

std::vector<uint32_t> bouquet_l2c::spec_nl_l2;
std::vector<std::vector<bouquet_l2c::IP_TRACKER>> bouquet_l2c::trackers;

uint64_t bouquet_l2c::instances = 0;

int bouquet_l2c::decode_stride(uint32_t metadata)
{
  int stride = 0;
  if (metadata & 0b1000000)
    stride = -1 * (metadata & 0b111111);
  else
    stride = metadata & 0b111111;

  return stride;
}

void bouquet_l2c::prefetcher_initialize()
{
    spec_nl_l2.push_back(0);
    trackers.push_back(std::vector<IP_TRACKER>(NUM_IP_TABLE_L2_ENTRIES));
    instance_id = instances;
    instances++;
}

uint32_t bouquet_l2c::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                      uint32_t metadata_in)
{
  champsim::block_number cl_addr{addr};
  int prefetch_degree = 0;
  int64_t stride = decode_stride(metadata_in);
  uint32_t pref_type = metadata_in & 0xF00;
  uint16_t ip_tag = (ip.to<uint64_t>() >> NUM_IP_INDEX_BITS) & ((1 << NUM_IP_TAG_BITS) - 1);

  if (NUM_CPUS == 1) {
    if (intern_->get_mshr_occupancy() < intern_->get_mshr_size() / 2)
      prefetch_degree = 4;
    else
      prefetch_degree = 3;
  } else { // tightening the degree for multi-core
    prefetch_degree = 2;
  }

  // calculate the index bit
  int index = ip.to<uint64_t>() & ((1 << NUM_IP_INDEX_BITS) - 1);
  if (trackers[instance_id][index].ip_tag != ip_tag) { // new/conflict IP
    if (trackers[instance_id][index].ip_valid == 0) {  // if valid bit is zero, update with latest IP info
      trackers[instance_id][index].ip_tag = ip_tag;
      trackers[instance_id][index].pref_type = pref_type;
      trackers[instance_id][index].stride = stride;
    } else {
      trackers[instance_id][index].ip_valid = 0; // otherwise, reset valid bit and leave the previous IP as it is
    }

    // issue a next line prefetch upon encountering new IP
    champsim::address pf_address{(cl_addr + 1)};
    prefetch_line(pf_address, true, 0);
    SIG_DP(cout << "1, ");
    return metadata_in;
  } else { // if same IP encountered, set valid bit
    trackers[instance_id][index].ip_valid = 1;
  }

  // update the IP table upon receiving metadata from prefetch
  if (type == access_type::PREFETCH) {
    trackers[instance_id][index].pref_type = pref_type;
    trackers[instance_id][index].stride = stride;
    spec_nl_l2[instance_id] = metadata_in & 0x1000;
  }

  SIG_DP(cout << ip << ", " << cache_hit << ", " << cl_addr << ", "; cout << ", " << stride << "; ";);

  // we are prefetching only for GS, CS and NL classes
  if (trackers[instance_id][index].stride != 0) {
    if (trackers[instance_id][index].pref_type == 0x100 || trackers[instance_id][index].pref_type == 0x200) { // GS or CS class
      if (trackers[instance_id][index].pref_type == 0x100)
        if (NUM_CPUS == 1)
          prefetch_degree = 4;
      for (int i = 0; i < prefetch_degree; i++) {
        champsim::address pf_address{(cl_addr + (trackers[instance_id][index].stride * (i + 1)))};

        // Check if prefetch address is in same 4 KB page
        if (champsim::page_number{pf_address} != champsim::page_number{addr})
          break;

        prefetch_line(pf_address, true, 0);
        SIG_DP(cout << trackers[instance_id][index].stride << ", ");
      }
    } else if (trackers[instance_id][index].pref_type == 0x400 && spec_nl_l2[instance_id] > 0) {
      champsim::address pf_address{(cl_addr + 1)};
      prefetch_line(pf_address, true, 0);
      SIG_DP(cout << "1;");
    }
  }

  SIG_DP(cout << endl);
  return metadata_in;
}

uint32_t bouquet_l2c::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void bouquet_l2c::prefetcher_final_stats() {}

void bouquet_l2c::prefetcher_cycle_operate() {}