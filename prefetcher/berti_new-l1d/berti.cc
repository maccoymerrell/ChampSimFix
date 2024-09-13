#include "berti.h"
#include "berti_parameters.h"
#include <cassert>
#include <iostream>

/*
 * Berti: an Accurate Local-Delta Data Prefetcher
 *  
 * 55th ACM/IEEE International Conference on Microarchitecture (MICRO 2022),
 * October 1-5, 2022, Chicago, Illinois, USA.
 * 
 * @Authors: Agustín Navarro-Torres, Biswabandan Panda, J. Alastruey-Benedé, 
 *           Pablo Ibáñez, Víctor Viñals-Yúfera, and Alberto Ros
 * @Manteiners: Agustín Navarro -Torres
 * @Email: agusnt@unizar.es
 * @Date: 22/11/2022
 * 
 * This code is an update from the original code to work with the new version
 * of ChampSim: https://github.com/agusnt/Berti-Artifact
 * 
 * Maybe fine-tuning is required to get the optimal performance/accuracy.
 * 
 * Please note that this version of ChampSim has noticeable differences with 
 * the used for the paper, so results can varies.
 * 
 * Cite this:
 * 
 * A. Navarro-Torres, B. Panda, J. Alastruey-Benedé, P. Ibáñez, 
 * V. Viñals-Yúfera and A. Ros, 
 * "Berti: an Accurate Local-Delta Data Prefetcher," 
 * 2022 55th IEEE/ACM International Symposium on Microarchitecture (MICRO), 
 * 2022, pp. 975-991, doi: 10.1109/MICRO56248.2022.00072.
 * 
 * @INPROCEEDINGS{9923806,  author={Navarro-Torres, Agustín and Panda, 
 * Biswabandan and Alastruey-Benedé, Jesús and Ibáñez, Pablo and Viñals-Yúfera,
 * Víctor and Ros, Alberto},  booktitle={2022 55th IEEE/ACM International 
 * Symposium on Microarchitecture (MICRO)},   title={Berti: an Accurate 
 * Local-Delta Data Prefetcher},   year={2022},  volume={},  number={},  
 * pages={975-991},  doi={10.1109/MICRO56248.2022.00072}}
 */

/******************************************************************************/
/*                      Latency table functions                               */
/******************************************************************************/
uint8_t LatencyTable_L1D::add(uint64_t addr, uint64_t tag, bool pf, uint64_t cycle)
{
  /*
   * Save if possible the new miss into the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *  - access: is theh entry accessed by a demand request
   *  - cycle: time to use in the latency table
   *
   * Return: pf
   */


  latency_table *free;
  free   = nullptr;

  for (int i = 0; i < size; i++)
  {
    // Search if the addr already exists. If it exist we does not have
    // to do nothing more
    if (latencyt[i].addr == addr)
    {
      latencyt[i].time = cycle;
      latencyt[i].pf   = pf;
      latencyt[i].tag  = tag;
      return latencyt[i].pf;
    }

    // We discover a free space into the latency table, save it for later
    if (latencyt[i].tag == 0) free = &latencyt[i];
  }

  // djimenez
  // what? abort if there's no free space? nah, just have random replacement.
  //if (free == nullptr) assert(0 && "No free space latency table");
  if (free == nullptr) {
	free = &latencyt[rand()%size];
  }

  // We save the new entry into the latency table
  free->addr = addr;
  free->time = cycle;
  free->tag  = tag;
  free->pf   = pf;

  return free->pf;
}

uint64_t LatencyTable_L1D::del(uint64_t addr)
{
  /*
   * Remove the address from the latency table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   *  Return: the latency of the address
   */


  for (int i = 0; i < size; i++)
  {
    // Line already in the table
    if (latencyt[i].addr == addr)
    {
      // Calculate latency
      uint64_t time = latencyt[i].time;


      latencyt[i].addr = 0; // Free the entry
      latencyt[i].tag  = 0; // Free the entry
      latencyt[i].time = 0; // Free the entry
      latencyt[i].pf   = 0; // Free the entry

      // Return the latency
      return time;
    }
  }

  // We should always track the misses
  return 0;
}

uint64_t LatencyTable_L1D::get(uint64_t addr)
{
  /*
   * Return time or 0 if the addr is or is not in the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   * Return: time if the line is in the latency table, otherwise 0
   */


  for (int i = 0; i < size; i++)
  {
    // Search if the addr already exists
    if (latencyt[i].addr == addr)
    {
      return latencyt[i].time;
    }
  }

  return 0;
}

uint64_t LatencyTable_L1D::get_tag(uint64_t addr)
{
  /*
   * Return IP-Tag or 0 if the addr is or is not in the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   * Return: ip-tag if the line is in the latency table, otherwise 0
   */


  for (int i = 0; i < size; i++)
  {
    if (latencyt[i].addr == addr && latencyt[i].tag) // This is the address
    {

      return latencyt[i].tag;
    }
  }

  return 0;
}

/******************************************************************************/
/*                       Shadow Cache functions                               */
/******************************************************************************/
bool ShadowCache_L1D::add(uint32_t set, uint32_t way, uint64_t addr, bool pf, uint64_t lat)
{
  /*
   * Add block to shadow cache
   *
   * Parameters:
   *      - cpu: cpu
   *      - set: cache set
   *      - way: cache way
   *      - addr: cache block v_addr
   *      - access: the cache is access by a demand
   */


  scache[set][way].addr = addr;
  scache[set][way].pf   = pf;
  scache[set][way].lat  = lat;
  return scache[set][way].pf;
}

bool ShadowCache_L1D::get(uint64_t addr)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: true if the addr is in the l1d cache, false otherwise
   */


  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {
        return true;
      }
    }
  }

  return false;
}

void ShadowCache_L1D::set_pf(uint64_t addr, bool pf)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: change value of pf field 
   */

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {
        scache[i][ii].pf = pf;
        return;
      }
    }
  }

  // The address should always be in the cache
  assert((0) && "Address is must be in shadow cache");
}

bool ShadowCache_L1D::is_pf(uint64_t addr)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: True if the saved one is a prefetch
   */

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr)
      {

        return scache[i][ii].pf;
      }
    }
  }

  assert((0) && "Address is must be in shadow cache");
  return false;
}

uint64_t ShadowCache_L1D::get_latency(uint64_t addr)
{
  /*
   * Init shadow cache
   *
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: the saved latency
   */

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {

        return scache[i][ii].lat;
      }
    }
  }

  assert((0) && "Address is must be in shadow cache");
  return 0;
}

/******************************************************************************/
/*                       History Table functions                               */
/******************************************************************************/
void HistoryTable_L1D::add(uint64_t tag, uint64_t addr, uint64_t cycle)
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *  - addr: addr access
   */
  uint16_t set = tag & TABLE_SET_MASK;

  // Save new element into the history table
  history_pointers[set]->tag       = tag;
  history_pointers[set]->time      = cycle & TIME_MASK;
  history_pointers[set]->addr      = addr & ADDR_MASK;


  if (history_pointers[set] == &historyt[set][ways - 1])
  {
    history_pointers[set] = &historyt[set][0]; // End the cycle
  } else history_pointers[set]++; // Pointer to the next (oldest) entry
}

uint16_t HistoryTable_L1D::get_aux(uint32_t latency, 
    uint64_t tag, uint64_t act_addr, uint64_t *tags, uint64_t *addr, 
    uint64_t cycle)
{
  /*
   * Return an array (by parameter) with all the possible PC that can launch
   * an on-time and late prefetch
   *
   * Parameters:
   *  - tag: PC tag
   *  - latency: latency of the processor
   */

  uint16_t num_on_time = 0;
  uint16_t set = tag & TABLE_SET_MASK;


  // This is the begin of the simulation
  if (cycle < latency) return num_on_time;

  // The IPs that is launch in this cycle will be able to launch this prefetch
  cycle -= latency; 

  // Pointer to guide
  history_table *pointer = history_pointers[set];

  do
  {
    // Look for the IPs that can launch this prefetch
    if (pointer->tag == tag && pointer->time <= cycle)
    {
      // Test that addr is not duplicated
      if (pointer->addr == act_addr) return num_on_time;

      // This IP can launch the prefetch
      tags[num_on_time] = pointer->tag;
      addr[num_on_time] = pointer->addr;
      num_on_time++;
    }

    if (pointer == historyt[set])
    {
      // We get at the end of the history, we start again
      pointer = &historyt[set][ways - 1];
    } else pointer--;
  } while (pointer != history_pointers[set]);

  return num_on_time;
}

uint16_t HistoryTable_L1D::get(uint32_t latency, uint64_t tag, uint64_t act_addr,
    uint64_t *tags, uint64_t *addr, uint64_t cycle)
{
  /*
   * Return an array (by parameter) with all the possible PC that can launch
   * an on-time and late prefetch
   *
   * Parameters:
   *  - tag: PC tag
   *  - latency: latency of the processor
   *  - on_time_ip (out): ips that can launch an on-time prefetch
   *  - on_time_addr (out): addr that can launch an on-time prefetch
   *  - num_on_time (out): number of ips that can launch an on-time prefetch
   */

  act_addr &= ADDR_MASK;

  uint16_t num_on_time = get_aux(latency, tag, act_addr, tags, addr, cycle & TIME_MASK);

  // We found on-time prefetchs
  return num_on_time;
}

/******************************************************************************/
/*                        Berti table functions                               */
/******************************************************************************/
void Berti_L1D::increase_conf_tag(uint64_t tag)
{
  /*
   * Increase the global confidence of the deltas associated to the tag
   *
   * Parameters:
   *  tag : tag to find
   */

  if (bertit.find(tag) == bertit.end())
  {
    // Tag not found

    return;
  }

  // Get the entries and the deltas
  berti *tmp = bertit[tag];
  delta_t *aux = tmp->deltas;

  tmp->conf += CONFIDENCE_INC;



  if (tmp->conf == CONFIDENCE_MAX) 
  {

    // Max confidence achieve
    for(int i = 0; i < BERTI_TABLE_DELTA_SIZE; i++)
    {
      // Set bits to prefetch level
      if (aux[i].conf > CONFIDENCE_L1) aux[i].rpl = BERTI_L1;
      else if (aux[i].conf > CONFIDENCE_L2) aux[i].rpl = BERTI_L2;
      else if (aux[i].conf > CONFIDENCE_L2R) aux[i].rpl = BERTI_L2R;
      else aux[i].rpl = BERTI_R;


      aux[i].conf = 0; // Reset confidence
    }

    tmp->conf = 0; // Reset global confidence
  }

}

void Berti_L1D::add(uint64_t tag, int64_t delta)
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *  - cpu: actual cpu
   *  - stride: actual cpu
   */

  if (bertit.find(tag) == bertit.end())
  {

    // We are not tracking this tag
    if (bertit_queue.size() > BERTI_TABLE_SIZE)
    {
      // FIFO replacent algorithm
      uint64_t key = bertit_queue.front();
      berti *entry = bertit[key];


      delete entry; // Free previous entry

      bertit.erase(bertit_queue.front());
      bertit_queue.pop();
    }

    bertit_queue.push(tag); // Add new tag
    assert((bertit.size() <= BERTI_TABLE_SIZE) && "Tracking too much tags");

    // Confidence IP
    berti *entry = new berti;
    entry->conf = CONFIDENCE_INC;

    // Saving the new stride
    entry->deltas[0].delta = delta;
    entry->deltas[0].conf = CONFIDENCE_INIT;
    entry->deltas[0].rpl = BERTI_R;


    // Save the new tag
    bertit.insert(std::make_pair(tag, entry));
    return;
  }

  // Get the delta
  berti *entry  = bertit[tag];

  for (int i = 0; i < BERTI_TABLE_DELTA_SIZE; i++)
  {
    if (entry->deltas[i].delta == delta)
    {
      // We already track the delta
      entry->deltas[i].conf += CONFIDENCE_INC;

      if (entry->deltas[i].conf > CONFIDENCE_MAX)
        entry->deltas[i].conf = CONFIDENCE_MAX;


      return;
    }
  }

  // We have to make space to save the stride
  int dx_remove = -1;
  int8_t rpl_dx = BERTI_R;
  do
  {
    uint8_t dx_conf = CONFIDENCE_MAX;
    for (int i = 0; i < BERTI_TABLE_DELTA_SIZE; i++)
    {
      if (entry->deltas[i].rpl == rpl_dx && entry->deltas[i].conf < dx_conf)
      {
        // This entry can be replaced
        dx_conf = entry->deltas[i].conf;
        dx_remove = i;
      }
    }

    if (rpl_dx == BERTI_L2R) break; // We can not search for more entries
    rpl_dx = BERTI_L2R; // We will try to replace L2R entries
  } while(dx_remove == -1);

  if (dx_remove > -1)
  {
    // We replace this entry

    entry->deltas[dx_remove].delta = delta;
    entry->deltas[dx_remove].conf  = CONFIDENCE_INIT;
    entry->deltas[dx_remove].rpl   = BERTI_R;
  }
}

uint8_t Berti_L1D::get(uint64_t tag, delta_t res[BERTI_TABLE_DELTA_SIZE])
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *
   * Return: the stride to prefetch
   */

  if (!bertit.count(tag))
  {
    no_found_berti_L1D++;
    return 0;
  }
  found_berti_L1D++;


  // We found the tag
  berti *entry  = bertit[tag];
  uint16_t dx = 0;

  for (int i = 0; i < BERTI_TABLE_DELTA_SIZE; i++)
  {
    if (entry->deltas[i].delta != 0 && entry->deltas[i].rpl)
    {
      // Substitute min confidence for the next one
      res[dx].delta = entry->deltas[i].delta;
      res[dx].rpl   = entry->deltas[i].rpl;
      dx++;
    }
  }

  if (dx == 0 && entry->conf >= LAUNCH_MIDDLE_CONF)
  {
    // We do not find any delta, so we will try to launch with small confidence
    for (int i = 0; i < BERTI_TABLE_DELTA_SIZE; i++)
    {
      if (entry->deltas[i].delta != 0)
      {
        res[dx].delta = entry->deltas[i].delta;
        if (entry->deltas[i].conf > CONFIDENCE_MIDDLE_L1) res[i].rpl = BERTI_L1;
        else if (entry->deltas[i].conf > CONFIDENCE_MIDDLE_L2) res[i].rpl = BERTI_L2;
        else entry->deltas[i].rpl = BERTI_R;
      }
    }
  }

  // Sort the entries
  std::sort(res, res + BERTI_TABLE_DELTA_SIZE, compare_greater_delta);
  return 1;
}

void Berti_L1D::find_and_update(uint64_t latency, uint64_t tag, uint64_t cycle, 
    uint64_t line_addr, uint32_t cpu)
{ 
  // We were tracking this miss
  uint64_t tags[HISTORY_TABLE_WAYS];
  uint64_t addr[HISTORY_TABLE_WAYS];
  uint16_t num_on_time = 0;

  // Get the IPs that can launch a prefetch
  num_on_time = historyt_L1D[cpu]->get(latency, tag, line_addr, tags, addr, cycle);

  for (uint32_t i = 0; i < num_on_time; i++)
  {
    // Increase conf tag
    if (i == 0) increase_conf_tag(tag);

    // Add information into berti table
    int64_t stride;
    line_addr &= ADDR_MASK;

    // Usually applications go from lower to higher memory position.
    // The operation order is important (mainly because we allow
    // negative strides)
    stride = (int64_t) (line_addr - addr[i]);

    if ((std::abs(stride) < (1 << DELTA_MASK))) add(tags[i], stride); 
  }
}

bool Berti_L1D::compare_greater_delta(delta_t a, delta_t b)
{
  // Sorted stride when the confidence is full
  if (a.rpl == BERTI_L1 && b.rpl != BERTI_L1) return 1;
  else if (a.rpl != BERTI_L1 && b.rpl == BERTI_L1) return 0;
  else
  {
    if (a.rpl == BERTI_L2 && b.rpl != BERTI_L2) return 1;
    else if (a.rpl != BERTI_L2 && b.rpl == BERTI_L2) return 0;
    else
    {
      if (a.rpl == BERTI_L2R && b.rpl != BERTI_L2R) return 1;
      if (a.rpl != BERTI_L2R && b.rpl == BERTI_L2R) return 0;
      else
      {
        if (std::abs(a.delta) < std::abs(b.delta)) return 1;
        return 0;
      }
    }
  }
}

/******************************************************************************/
/*                        Cache Functions                                     */
/******************************************************************************/
void CACHE::prefetcher_initialize() 
{
  uint64_t latency_table_size = 0;
  for (int i = 0; i < 4; i++) latency_table_size += get_size(i, 0);
  latencyt_L1D.push_back(new LatencyTable_L1D(latency_table_size));
  scache_L1D.push_back(new ShadowCache_L1D(this->NUM_SET, this->NUM_WAY));
  historyt_L1D.push_back(new HistoryTable_L1D());
  berti_L1D.push_back(new Berti_L1D());

  std::cout << "[BERTI] init " << NAME;
  std::cout << std::endl;

# ifdef NO_CROSS_PAGE
  std::cout << "No Crossing Page" << std::endl;
# endif
}

void CACHE::prefetcher_cycle_operate()
{}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, bool useful_prefetch, uint8_t type, uint32_t metadata_in)
{
// djimenez
if (type == (uint8_t) access_type::PREFETCH) return metadata_in; // there are instruction prefetches with my stuff
  assert((type == (uint8_t)access_type::LOAD) || (type == (uint8_t) access_type::RFO) && "Berti only activates with LOAD");

  uint64_t line_addr = (addr >> LOG2_BLOCK_SIZE); // Line addr


  ip = ((ip >> 1) ^ (ip >> 4)) & IP_MASK;

  if (!cache_hit) // This is a miss
  {

    latencyt_L1D[cpu]->add(line_addr, ip, false, current_cycle()); // Add @ to latency
    historyt_L1D[cpu]->add(ip, line_addr, current_cycle()); // Add to the table
  } else if (cache_hit && scache_L1D[cpu]->is_pf(line_addr)) // Hit bc prefetch
  {

    scache_L1D[cpu]->set_pf(line_addr, false);
    // Get latency
    uint64_t latency = scache_L1D[cpu]->get_latency(line_addr);

    if (latency > LAT_MASK) latency = 0;

    berti_L1D[cpu]->find_and_update(latency, ip, current_cycle() & TIME_MASK, line_addr, cpu);

    historyt_L1D[cpu]->add(ip, line_addr, current_cycle() & TIME_MASK);
  } else
  {

  }

  delta_t deltas[BERTI_TABLE_DELTA_SIZE];
  berti_L1D[cpu]->get(ip, deltas);

  int num_load = 0;
  for (int i = 0; i < BERTI_TABLE_DELTA_SIZE; i++)
  {
    uint64_t p_addr = (line_addr + deltas[i].delta) << LOG2_BLOCK_SIZE;
    uint64_t p_b_addr = (p_addr >> LOG2_BLOCK_SIZE);

    if (latencyt_L1D[cpu]->get(p_b_addr)) continue;
    if (deltas[i].rpl == BERTI_R) return metadata_in;

    if ((p_addr >> LOG2_PAGE_SIZE) != (addr >> LOG2_PAGE_SIZE))
    {
      cross_page_L1D++;
# ifdef NO_CROSS_PAGE
      // We do not cross virtual page
      continue;
# endif
    } else no_cross_page_L1D++;

    float mshr_load = ((float) get_occupancy(0, 0) / (float) get_size(0, 0)) * 100;
    //float mshr_load = ((float) (get_occupancy(0, 0) + num_load) / (float) get_size(0, 0)) * 100;

    bool fill_this_level = (deltas[i].rpl == BERTI_L1) && (mshr_load < MSHR_LIMIT);

    if (deltas[i].rpl == BERTI_L1 && mshr_load >= MSHR_LIMIT) pf_to_l2_bc_mshr_L1D++; 
    if (fill_this_level) pf_to_l1_L1D++;
    else pf_to_l2_L1D++;

//Amdiii
// std::cout << metadata_in << std::endl;
    if (prefetch_line(p_addr, fill_this_level, metadata_in))
    {


      if (fill_this_level)
      {
        if (!scache_L1D[cpu]->get(p_b_addr))
        {
          ++num_load;
          latencyt_L1D[cpu]->add(p_b_addr, ip, true, current_cycle());
        }
      }
    }
  }

  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, 
    uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  uint64_t line_addr = (addr >> LOG2_BLOCK_SIZE); // Line addr
  uint64_t tag     = latencyt_L1D[cpu]->get_tag(line_addr);
  uint64_t cycle   = latencyt_L1D[cpu]->del(line_addr) & TIME_MASK;
  uint64_t latency = 0;


  if (cycle != 0 && ((current_cycle() & TIME_MASK) > cycle))
    latency = (current_cycle() & TIME_MASK) - cycle;

  if (latency > LAT_MASK)
  {
    latency = 0;
    cant_track_latency_L1D++;
  } else
  {
    if (latency != 0)
    {
      // Calculate average latency
      if (average_latency_L1D.num == 0) average_latency_L1D.average = (float) latency;
      else
      {
        average_latency_L1D.average = average_latency_L1D.average + 
          ((((float) latency) - average_latency_L1D.average) / average_latency_L1D.num);
      }
      average_latency_L1D.num++;
    }
  }

  // Add to the shadow cache
  scache_L1D[cpu]->add(set, way, line_addr, prefetch, latency);

  if (latency != 0 && !prefetch)
  {
    berti_L1D[cpu]->find_and_update(latency, tag, cycle, line_addr, cpu);
  }
  return metadata_in;
}

void CACHE::prefetcher_final_stats()
{
  std::cout << "BERTI " << "TO_L1: " << pf_to_l1_L1D << " TO_L2: " << pf_to_l2_L1D;
  std::cout << " TO_L2_BC_MSHR: " << pf_to_l2_bc_mshr_L1D << " AVG_LAT: ";
  std::cout << average_latency_L1D.average << " NUM_TRACK_LATENCY: ";
  std::cout << average_latency_L1D.num << " NUM_CANT_TRACK_LATENCY: ";
  std::cout << cant_track_latency_L1D << " CROSS_PAGE: " << cross_page_L1D;
  std::cout << " NO_CROSS_PAGE: " << no_cross_page_L1D;
  std::cout << " FOUND_BERTI: " << found_berti_L1D;
  std::cout << " NO_FOUND_BERTI: " << no_found_berti_L1D << std::endl;
}

//void CACHE::prefetcher_speculative_branch(uint64_t ip, uint8_t branch) {}

//void CACHE::prefetcher_commit(uint64_t ip, uint64_t addr, uint8_t branch) {}
