#include "bouquet_l1d.h"

//static vectors
std::vector<std::vector<bouquet_l1d::IP_TABLE_L1>> bouquet_l1d::trackers_l1;
std::vector<std::vector<bouquet_l1d::DELTA_PRED_TABLE>> bouquet_l1d::DPT_l1;
std::vector<std::vector<uint64_t>> bouquet_l1d::ghb_l1;
std::vector<uint64_t> bouquet_l1d::prev_cpu_cycle;
std::vector<uint64_t> bouquet_l1d::num_misses;
std::vector<float> bouquet_l1d::mpkc;
std::vector<int> bouquet_l1d::spec_nl;

uint16_t bouquet_l1d::instances = 0;

/***************Updating the signature*************************************/
uint16_t bouquet_l1d::update_sig_l1(uint16_t old_sig, int delta)
{
  uint16_t new_sig = 0;
  int sig_delta = 0;

  // 7-bit sign magnitude form, since we need to track deltas from +63 to -63
  sig_delta = (delta < 0) ? (((-1) * delta) + (1 << 6)) : delta;
  new_sig = ((old_sig << 1) ^ sig_delta) & 0xFFF; // 12-bit signature

  return new_sig;
}

/****************Encoding the metadata***********************************/
uint32_t bouquet_l1d::encode_metadata(int stride, uint16_t type, int spec_nl_)
{

  uint32_t metadata = 0;

  // first encode stride in the last 8 bits of the metadata
  if (stride > 0)
    metadata = stride;
  else
    metadata = ((-1 * stride) | 0b1000000);

  // encode the type of IP in the next 4 bits
  metadata = metadata | (type << 8);

  // encode the speculative NL bit in the next 1 bit
  metadata = metadata | (spec_nl_ << 12);

  return metadata;
}

/*********************Checking for a global stream (GS class)***************/

void bouquet_l1d::check_for_stream_l1(int index, uint64_t cl_addr, uint8_t cpu)
{
  uint pos_count = 0, neg_count = 0, count = 0;
  uint64_t check_addr = cl_addr;

  // check for +ve stream
  for (uint i = 0; i < NUM_GHB_ENTRIES; i++) {
    check_addr--;
    for (uint j = 0; j < NUM_GHB_ENTRIES; j++)
      if (check_addr == ghb_l1[cpu][j]) {
        pos_count++;
        break;
      }
  }

  check_addr = cl_addr;
  // check for -ve stream
  for (uint i = 0; i < NUM_GHB_ENTRIES; i++) {
    check_addr++;
    for (uint j = 0; j < NUM_GHB_ENTRIES; j++)
      if (check_addr == ghb_l1[cpu][j]) {
        neg_count++;
        break;
      }
  }

  if (pos_count > neg_count) { // stream direction is +ve
    trackers_l1[cpu][index].str_dir = 1;
    count = pos_count;
  } else { // stream direction is -ve
    trackers_l1[cpu][index].str_dir = 0;
    count = neg_count;
  }

  if (count > NUM_GHB_ENTRIES / 2) { // stream is detected
    trackers_l1[cpu][index].str_valid = 1;
    if (count >= (NUM_GHB_ENTRIES * 3) / 4) // stream is classified as strong if more than 3/4th entries belong to stream
      trackers_l1[cpu][index].str_strength = 1;
  } else {
    if (trackers_l1[cpu][index].str_strength == 0) // if identified as weak stream, we need to reset
      trackers_l1[cpu][index].str_valid = 0;
  }
}

/**************************Updating confidence for the CS class****************/
int bouquet_l1d::update_conf(int stride, int pred_stride, int conf)
{
  if (stride == pred_stride) { // use 2-bit saturating counter for confidence
    conf++;
    if (conf > 3)
      conf = 3;
  } else {
    conf--;
    if (conf < 0)
      conf = 0;
  }

  return conf;
}

void bouquet_l1d::prefetcher_initialize()
{
    instance_id = instances;
    instances++;

    trackers_l1.push_back(std::vector<IP_TABLE_L1>(NUM_IP_TABLE_L1_ENTRIES));
    DPT_l1.push_back(std::vector<DELTA_PRED_TABLE>(4096));
    ghb_l1.push_back(std::vector<uint64_t>(NUM_GHB_ENTRIES));
    prev_cpu_cycle.push_back(0);
    num_misses.push_back(0);
    mpkc.push_back(0);
    spec_nl.push_back(0);
}

uint32_t bouquet_l1d::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                      uint32_t metadata_in)
{

  champsim::page_number curr_page{addr};
  champsim::block_number cl_addr{addr};
  champsim::block_offset cl_offset{addr};
  uint16_t signature = 0, last_signature = 0;
  int prefetch_degree = 0;
  int spec_nl_threshold = 0;
  int num_prefs = 0;
  uint32_t metadata = 0;
  uint16_t ip_tag = (ip.to<uint64_t>() >> NUM_IP_INDEX_BITS) & ((1 << NUM_IP_TAG_BITS) - 1);

  if (NUM_CPUS == 1) {
    prefetch_degree = 3;
    spec_nl_threshold = 15;
  } else { // tightening the degree and MPKC constraints for multi-core
    prefetch_degree = 2;
    spec_nl_threshold = 5;
  }

  // update miss counter
  if (cache_hit == 0)
    num_misses[instance_id] += 1;

  // update spec nl bit when num misses crosses certain threshold
  if (num_misses[instance_id] == 256) {
    mpkc[instance_id] = ((float)num_misses[instance_id] / (intern_->current_cycle() - prev_cpu_cycle[instance_id])) * 1000;
    prev_cpu_cycle[instance_id] = intern_->current_cycle();
    if (mpkc[instance_id] > spec_nl_threshold)
      spec_nl[instance_id] = 0;
    else
      spec_nl[instance_id] = 1;
    num_misses[instance_id] = 0;
  }

  // calculate the index bit
  int index = ip.to<uint64_t>() & ((1 << NUM_IP_INDEX_BITS) - 1);
  if (trackers_l1[instance_id][index].ip_tag != ip_tag) { // new/conflict IP
    if (trackers_l1[instance_id][index].ip_valid == 0) {  // if valid bit is zero, update with latest IP info
      trackers_l1[instance_id][index].ip_tag = ip_tag;
      trackers_l1[instance_id][index].last_page = curr_page.to<uint64_t>();
      trackers_l1[instance_id][index].last_cl_offset = cl_offset.to<uint64_t>();
      trackers_l1[instance_id][index].last_stride = 0;
      trackers_l1[instance_id][index].signature = 0;
      trackers_l1[instance_id][index].conf = 0;
      trackers_l1[instance_id][index].str_valid = 0;
      trackers_l1[instance_id][index].str_strength = 0;
      trackers_l1[instance_id][index].str_dir = 0;
      trackers_l1[instance_id][index].ip_valid = 1;
    } else { // otherwise, reset valid bit and leave the previous IP as it is
      trackers_l1[instance_id][index].ip_valid = 0;
    }

    // issue a next line prefetch upon encountering new IP
    champsim::address pf_address{((cl_addr) + 1)}; // BASE NL=1, changing it to 3
    metadata = encode_metadata(1, NL_TYPE, spec_nl[instance_id]);
    prefetch_line(pf_address, true, metadata);
    return metadata;
  } else { // if same IP encountered, set valid bit
    trackers_l1[instance_id][index].ip_valid = 1;
  }

  // calculate the stride between the current address and the last address
  int64_t stride = 0;
  if (cl_offset.to<uint64_t>() > trackers_l1[instance_id][index].last_cl_offset)
    stride = cl_offset.to<uint64_t>() - trackers_l1[instance_id][index].last_cl_offset;
  else {
    stride = trackers_l1[instance_id][index].last_cl_offset - cl_offset.to<uint64_t>();
    stride *= -1;
  }

  // don't do anything if same address is seen twice in a row
  if (stride == 0)
    return metadata;

  // page boundary learning
  if (curr_page.to<uint64_t>() != trackers_l1[instance_id][index].last_page) {
    if (stride < 0)
      stride += 64;
    else
      stride -= 64;
  }

  // update constant stride(CS) confidence
  trackers_l1[instance_id][index].conf = update_conf(stride, trackers_l1[instance_id][index].last_stride, trackers_l1[instance_id][index].conf);

  // update CS only if confidence is zero
  if (trackers_l1[instance_id][index].conf == 0)
    trackers_l1[instance_id][index].last_stride = stride;

  last_signature = trackers_l1[instance_id][index].signature;
  // update complex stride(CPLX) confidence
  DPT_l1[instance_id][last_signature].conf = update_conf(stride, DPT_l1[instance_id][last_signature].delta, DPT_l1[instance_id][last_signature].conf);

  // update CPLX only if confidence is zero
  if (DPT_l1[instance_id][last_signature].conf == 0)
    DPT_l1[instance_id][last_signature].delta = stride;

  // calculate and update new signature in IP table
  signature = update_sig_l1(last_signature, stride);
  trackers_l1[instance_id][index].signature = signature;

  // check GHB for stream IP
  check_for_stream_l1(index, cl_addr.to<uint64_t>(), instance_id);

  SIG_DP(cout << ip << ", " << cache_hit << ", " << cl_addr << ", " << addr << ", " << stride << "; ";
         cout << last_signature << ", " << DPT_l1[instance_id][last_signature].delta << ", " << DPT_l1[instance_id][last_signature].conf << "; ";
         cout << trackers_l1[instance_id][index].last_stride << ", " << stride << ", " << trackers_l1[instance_id][index].conf << ", "
              << "; ";);

  if (trackers_l1[instance_id][index].str_valid == 1) { // stream IP
                                                // for stream, prefetch with twice the usual degree
    prefetch_degree = prefetch_degree * 2;
    for (int i = 0; i < prefetch_degree; i++) {
      champsim::address pf_address{0};

      if (trackers_l1[instance_id][index].str_dir == 1) { // +ve stream
        pf_address = champsim::address{(cl_addr + i + 1)};
        metadata = encode_metadata(1, S_TYPE, spec_nl[instance_id]); // stride is 1
      } else {                                               // -ve stream
        pf_address = champsim::address{(cl_addr - i - 1)};
        metadata = encode_metadata(-1, S_TYPE, spec_nl[instance_id]); // stride is -1
      }

      // Check if prefetch address is in same 4 KB page
      if (champsim::page_number{pf_address} != champsim::page_number{addr}) {
        break;
      }

      prefetch_line(pf_address, true, metadata);
      num_prefs++;
      SIG_DP(std::cout << "1, ");
    }

  } else if (trackers_l1[instance_id][index].conf > 1 && trackers_l1[instance_id][index].last_stride != 0) { // CS IP
    for (int i = 0; i < prefetch_degree; i++) {
      champsim::address pf_address{(cl_addr + (trackers_l1[instance_id][index].last_stride * (i + 1)))};

      // Check if prefetch address is in same 4 KB page
      if (champsim::page_number{pf_address} != champsim::page_number{addr}) {
        break;
      }

      metadata = encode_metadata(trackers_l1[instance_id][index].last_stride, CS_TYPE, spec_nl[instance_id]);
      prefetch_line(pf_address, true, metadata);
      num_prefs++;
      SIG_DP(std::cout << trackers_l1[instance_id][index].last_stride << ", ");
    }
  } else if (DPT_l1[instance_id][signature].conf >= 0 && DPT_l1[instance_id][signature].delta != 0) { // if conf>=0, continue looking for delta
    int pref_offset = 0, i = 0;                                                       // CPLX IP
    for (i = 0; i < prefetch_degree; i++) {
      pref_offset += DPT_l1[instance_id][signature].delta;
      champsim::address pf_address{(cl_addr + pref_offset)};

      // Check if prefetch address is in same 4 KB page
      if ((champsim::page_number{pf_address} != champsim::page_number{addr}) || (DPT_l1[instance_id][signature].conf == -1) || (DPT_l1[instance_id][signature].delta == 0)) {
        // if new entry in DPT or delta is zero, break
        break;
      }

      // we are not prefetching at L2 for CPLX type, so encode delta as 0
      metadata = encode_metadata(0, CPLX_TYPE, spec_nl[instance_id]);
      if (DPT_l1[instance_id][signature].conf > 0) { // prefetch only when conf>0 for CPLX
        prefetch_line(pf_address, true, metadata);
        num_prefs++;
        SIG_DP(std::cout << pref_offset << ", ");
      }
      signature = update_sig_l1(signature, DPT_l1[instance_id][signature].delta);
    }
  }

  // if no prefetches are issued till now, speculatively issue a next_line prefetch
  if (num_prefs == 0 && spec_nl[instance_id] == 1) { // NL IP
    champsim::address pf_address{(cl_addr + 1)};
    metadata = encode_metadata(1, NL_TYPE, spec_nl[instance_id]);
    prefetch_line(pf_address, true, metadata);
    SIG_DP(std::cout << "1, ");
  }

  SIG_DP(std::cout << std::endl);

  // update the IP table entries
  trackers_l1[instance_id][index].last_cl_offset = cl_offset.to<uint64_t>();
  trackers_l1[instance_id][index].last_page = curr_page.to<uint64_t>();

  // update GHB
  // search for matching cl addr
  uint ghb_index = 0;
  for (ghb_index = 0; ghb_index < NUM_GHB_ENTRIES; ghb_index++)
    if (cl_addr.to<uint64_t>() == ghb_l1[instance_id][ghb_index])
      break;
  // only update the GHB upon finding a new cl address
  if (ghb_index == NUM_GHB_ENTRIES) {
    for (ghb_index = NUM_GHB_ENTRIES - 1; ghb_index > 0; ghb_index--)
      ghb_l1[instance_id][ghb_index] = ghb_l1[instance_id][ghb_index - 1];
    ghb_l1[instance_id][0] = cl_addr.to<uint64_t>();
  }

  return metadata;
}

uint32_t bouquet_l1d::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}
void bouquet_l1d::prefetcher_final_stats() {}

void bouquet_l1d::prefetcher_cycle_operate() {}