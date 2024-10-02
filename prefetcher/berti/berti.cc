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

  std::vector<LatencyTable *> berti::latencyt;
  std::vector<ShadowCache *>  berti::scache;
  std::vector<HistoryTable *> berti::historyt;
  std::vector<berti *>        berti::vberti;

/******************************************************************************/
/*                      Latency table functions                               */
/******************************************************************************/
uint8_t LatencyTable::add(uint64_t addr, uint64_t tag, bool pf, uint64_t cycle)
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

  if constexpr (champsim::debug_print) 
  {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << std::hex << addr << " tag: " << tag;
    std::cout << " prefetch: " << std::dec << +pf << " cycle: " << cycle;
  }

  latency_table *free;
  free   = nullptr;

  for (int i = 0; i < size; i++)
  {
    // Search if the addr already exists. If it exist we does not have
    // to do nothing more
    if (latencyt[i].addr == addr)
    {
      if constexpr (champsim::debug_print) 
      {
        std::cout << " line already found; find_tag: " << latencyt[i].tag;
        std::cout << " find_pf: " << +latencyt[i].pf << std::endl;
      }
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

  if constexpr (champsim::debug_print) std::cout << " new entry" << std::endl;
  return free->pf;
}

uint64_t LatencyTable::del(uint64_t addr)
{
  /*
   * Remove the address from the latency table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   *  Return: the latency of the address
   */

  if constexpr (champsim::debug_print) 
  {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << std::hex << addr;
  }

  for (int i = 0; i < size; i++)
  {
    // Line already in the table
    if (latencyt[i].addr == addr)
    {
      // Calculate latency
      uint64_t time = latencyt[i].time;

      if constexpr (champsim::debug_print)
      {
        std::cout << " tag: " << latencyt[i].tag;
        std::cout << " prefetch: " << std::dec << +latencyt[i].pf;
        std::cout << " cycle: " << latencyt[i].time << std::endl;
      }

      latencyt[i].addr = 0; // Free the entry
      latencyt[i].tag  = 0; // Free the entry
      latencyt[i].time = 0; // Free the entry
      latencyt[i].pf   = 0; // Free the entry

      // Return the latency
      return time;
    }
  }

  // We should always track the misses
  if constexpr (champsim::debug_print) std::cout << " TRANSLATION" << std::endl;
  return 0;
}

uint64_t LatencyTable::get(uint64_t addr)
{
  /*
   * Return time or 0 if the addr is or is not in the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   * Return: time if the line is in the latency table, otherwise 0
   */

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::dec;
  }

  for (int i = 0; i < size; i++)
  {
    // Search if the addr already exists
    if (latencyt[i].addr == addr)
    {
      if constexpr (champsim::debug_print)
      {
        std::cout << " time: " << latencyt[i].time << std::endl;
      }
      return latencyt[i].time;
    }
  }

  if constexpr (champsim::debug_print) std::cout << " NOT FOUND" << std::endl;
  return 0;
}

uint64_t LatencyTable::get_tag(uint64_t addr)
{
  /*
   * Return IP-Tag or 0 if the addr is or is not in the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   * Return: ip-tag if the line is in the latency table, otherwise 0
   */

  if constexpr (champsim::debug_print) 
  {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << std::hex << addr;
  }

  for (int i = 0; i < size; i++)
  {
    if (latencyt[i].addr == addr && latencyt[i].tag) // This is the address
    {
      if constexpr (champsim::debug_print) 
      {
        std::cout << " tag: " << latencyt[i].tag << std::endl;
      }
      return latencyt[i].tag;
    }
  }

  if constexpr (champsim::debug_print) std::cout << " NOT_FOUND" << std::endl;
  return 0;
}

/******************************************************************************/
/*                       Shadow Cache functions                               */
/******************************************************************************/
bool ShadowCache::add(uint32_t set, uint32_t way, uint64_t addr, bool pf, uint64_t lat)
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

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " set: " << std::dec << set << " way: " << way;
    std::cout << " addr: " << std::hex << addr << std::dec;
    std::cout << " prev_addr: " << std::hex << scache[set][way].addr << std::dec;
    std::cout << " pf: " << +pf;
    std::cout << " latency: " << lat << std::endl;
  }

  scache[set][way].addr = addr;
  scache[set][way].pf   = pf;
  scache[set][way].lat  = lat;
  return scache[set][way].pf;
}

bool ShadowCache::get(uint64_t addr)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: true if the addr is in the l1d cache, false otherwise
   */

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::endl;
  }

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {
        if constexpr (champsim::debug_print)
        {
          std::cout << " set: " << i << " way: " << i << std::endl;
        }
        return true;
      }
    }
  }

  return false;
}

void ShadowCache::set_pf(uint64_t addr, bool pf)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: change value of pf field 
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::dec;
  }

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {
        if constexpr (champsim::debug_print)
        {
          std::cout << " set: " << i << " way: " << ii;
          std::cout << " old_pf_value: " << +scache[i][ii].pf;
          std::cout << " new_pf_value: " << +pf << std::endl;
        }
        scache[i][ii].pf = pf;
        return;
      }
    }
  }

  // The address should always be in the cache
  assert((0) && "Address is must be in shadow cache");
}

bool ShadowCache::is_pf(uint64_t addr)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: True if the saved one is a prefetch
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::dec;
  }

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr)
      {
        if constexpr (champsim::debug_print)
        {
          std::cout << " set: " << i << " way: " << ii;
          std::cout << " pf: " << +scache[i][ii].pf << std::endl;
        }

        return scache[i][ii].pf;
      }
    }
  }

  assert((0) && "Address is must be in shadow cache");
  return false;
}

uint64_t ShadowCache::get_latency(uint64_t addr)
{
  /*
   * Init shadow cache
   *
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: the saved latency
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::dec;
  }

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {
        if constexpr (champsim::debug_print)
        {
          std::cout << " set: " << i << " way: " << ii;
          std::cout << " latency: " << scache[i][ii].lat << std::endl;
        }

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
void HistoryTable::add(uint64_t tag, uint64_t addr, uint64_t cycle)
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

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_HISTORY_TABLE] " << __func__;
    std::cout << " tag: " << std::hex << tag << " line_addr: " << addr << std::dec;
    std::cout << " cycle: " << cycle << " set: " << set << std::endl;
  }

  if (history_pointers[set] == &historyt[set][ways - 1])
  {
    history_pointers[set] = &historyt[set][0]; // End the cycle
  } else history_pointers[set]++; // Pointer to the next (oldest) entry
}

uint16_t HistoryTable::get_aux(uint32_t latency, 
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

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_HISTORY_TABLE] " << __func__;
    std::cout << " tag: " << std::hex << tag << " line_addr: " << addr << std::dec;
    std::cout << " cycle: " << cycle << " set: " << set << std::endl;
  }

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

uint16_t HistoryTable::get(uint32_t latency, uint64_t tag, uint64_t act_addr,
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
void berti::increase_conf_tag(uint64_t tag)
{
  /*
   * Increase the global confidence of the deltas associated to the tag
   *
   * Parameters:
   *  tag : tag to find
   */
  if constexpr (champsim::debug_print)
    std::cout << "[BERTI_BERTI] " << __func__ << " tag: " << std::hex << tag << std::dec;

  if (bertit.find(tag) == bertit.end())
  {
    // Tag not found
    if constexpr (champsim::debug_print) 
      std::cout << " TAG NOT FOUND" << std::endl;

    return;
  }

  // Get the entries and the deltas
  berti_state *tmp = bertit[tag];
  delta_t *aux = tmp->deltas;

  tmp->conf += CONFIDENCE_INC;

  if constexpr (champsim::debug_print) 
    std::cout << " global_conf: " << tmp->conf;


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

      if constexpr (champsim::debug_print) 
      {
        std::cout << " Num: " << i << " Delta: " << aux[i].delta;
        std::cout << " Conf: "  << aux[i].conf << " Level: " << +aux[i].rpl;
        std::cout << "|";
      }

      aux[i].conf = 0; // Reset confidence
    }

    tmp->conf = 0; // Reset global confidence
  }

  if constexpr (champsim::debug_print) std::cout << std::endl;
}

void berti::add(uint64_t tag, int64_t delta)
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *  - cpu: actual cpu
   *  - stride: actual cpu
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_BERTI] " << __func__;
    std::cout << " tag: " << std::hex << tag << std::dec;
    std::cout << " delta: " << delta;
  }

  if (bertit.find(tag) == bertit.end())
  {
    if constexpr (champsim::debug_print)
      std::cout << " allocating a new entry;";

    // We are not tracking this tag
    if (bertit_queue.size() > BERTI_TABLE_SIZE)
    {
      // FIFO replacent algorithm
      uint64_t key = bertit_queue.front();
      berti_state *entry = bertit[key];

      if constexpr (champsim::debug_print)
        std::cout << " removing tag: " << std::hex << key << std::dec << ";";

      delete entry; // Free previous entry

      bertit.erase(bertit_queue.front());
      bertit_queue.pop();
    }

    bertit_queue.push(tag); // Add new tag
    assert((bertit.size() <= BERTI_TABLE_SIZE) && "Tracking too much tags");

    // Confidence IP
    berti_state *entry = new berti_state;
    entry->conf = CONFIDENCE_INC;

    // Saving the new stride
    entry->deltas[0].delta = delta;
    entry->deltas[0].conf = CONFIDENCE_INIT;
    entry->deltas[0].rpl = BERTI_R;

    if constexpr (champsim::debug_print)
      std::cout << " confidence: " << CONFIDENCE_INIT << std::endl;

    // Save the new tag
    bertit.insert(std::make_pair(tag, entry));
    return;
  }

  // Get the delta
  berti_state *entry  = bertit[tag];

  for (int i = 0; i < BERTI_TABLE_DELTA_SIZE; i++)
  {
    if (entry->deltas[i].delta == delta)
    {
      // We already track the delta
      entry->deltas[i].conf += CONFIDENCE_INC;

      if (entry->deltas[i].conf > CONFIDENCE_MAX)
        entry->deltas[i].conf = CONFIDENCE_MAX;

      if constexpr (champsim::debug_print)
        std::cout << " confidence: " << entry->deltas[i].conf << std::endl;

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
        dx_conf = (uint8_t)(entry->deltas[i].conf);
        dx_remove = i;
      }
    }

    if (rpl_dx == BERTI_L2R) break; // We can not search for more entries
    rpl_dx = BERTI_L2R; // We will try to replace L2R entries
  } while(dx_remove == -1);

  if (dx_remove > -1)
  {
    // We replace this entry
    if constexpr (champsim::debug_print)
      std::cout << " replaced_delta: " << entry->deltas[dx_remove].delta << std::endl;

    entry->deltas[dx_remove].delta = delta;
    entry->deltas[dx_remove].conf  = CONFIDENCE_INIT;
    entry->deltas[dx_remove].rpl   = BERTI_R;
  }
}

uint8_t berti::get(uint64_t tag, delta_t res[BERTI_TABLE_DELTA_SIZE])
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *
   * Return: the stride to prefetch
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_BERTI] " << __func__ << " tag: " << std::hex << tag;
    std::cout << std::dec;
  }

  if (!bertit.count(tag))
  {
    if constexpr (champsim::debug_print)
      std::cout << " TAG NOT FOUND" << std::endl;
    no_found_berti++;
    return 0;
  }
  found_berti++;

  if constexpr (champsim::debug_print) std::cout << std::endl;

  // We found the tag
  berti_state *entry  = bertit[tag];
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

void berti::find_and_update(uint64_t latency, uint64_t tag, uint64_t cycle, 
    uint64_t line_addr, uint32_t cpu)
{ 
  // We were tracking this miss
  uint64_t tags[HISTORY_TABLE_WAYS];
  uint64_t addr[HISTORY_TABLE_WAYS];
  uint16_t num_on_time = 0;

  // Get the IPs that can launch a prefetch
  num_on_time = historyt[cpu]->get((uint32_t)latency, tag, line_addr, tags, addr, cycle);

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

bool berti::compare_greater_delta(delta_t a, delta_t b)
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
void berti::prefetcher_initialize() 
{
  uint64_t latency_table_size = intern_->get_mshr_size();
  latencyt.push_back(new LatencyTable(latency_table_size));
  scache.push_back(new ShadowCache(intern_->NUM_SET, intern_->NUM_WAY));
  historyt.push_back(new HistoryTable());
  vberti.push_back(this);

  std::cout << "[BERTI] init " << intern_->NAME;
  std::cout << std::endl;

# ifdef NO_CROSS_PAGE
  std::cout << "No Crossing Page" << std::endl;
# endif
}

void berti::prefetcher_cycle_operate()
{}

uint32_t berti::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                         uint32_t metadata_in)
{
// djimenez
if (type == access_type::PREFETCH) return metadata_in; // there are instruction prefetches with my stuff
  assert((type == access_type::LOAD) || ((type == access_type::RFO) && "Berti only activates with LOAD"));

  champsim::block_number line_addr{addr}; // Line addr

  if constexpr (champsim::debug_print) 
  {
    std::cout << "[BERTI] operate cpu: " << intern_->cpu;
    std::cout << " ip: " << std::hex << ip;
    std::cout << " full_address: " << addr;
    std::cout << " line_address: " << line_addr << std::dec << std::endl ;
  }

  ip = champsim::address{((ip.to<uint64_t>() >> 1) ^ (ip.to<uint64_t>() >> 4)) & IP_MASK};

  if (!cache_hit) // This is a miss
  {
    if constexpr (champsim::debug_print) 
      std::cout << "[BERTI] operate cache miss" << std::endl;

    latencyt[intern_->cpu]->add(line_addr.to<uint64_t>(), ip.to<uint64_t>(), false, intern_->current_cycle()); // Add @ to latency
    historyt[intern_->cpu]->add(ip.to<uint64_t>(), line_addr.to<uint64_t>(), intern_->current_cycle()); // Add to the table
  } else if (cache_hit && scache[intern_->cpu]->is_pf(line_addr.to<uint64_t>())) // Hit bc prefetch
  {
    if constexpr (champsim::debug_print)
      std::cout << "[BERTI] operate cache hit because of pf" << std::endl;

    scache[intern_->cpu]->set_pf(line_addr.to<uint64_t>(), false);
    // Get latency
    uint64_t latency = scache[intern_->cpu]->get_latency(line_addr.to<uint64_t>());

    if (latency > LAT_MASK) latency = 0;

    vberti[intern_->cpu]->find_and_update(latency, ip.to<uint64_t>(), intern_->current_cycle() & TIME_MASK, line_addr.to<uint64_t>(), intern_->cpu);

    historyt[intern_->cpu]->add(ip.to<uint64_t>(), line_addr.to<uint64_t>(), intern_->current_cycle() & TIME_MASK);
  } else
  {
    if constexpr (champsim::debug_print) 
      std::cout << "[BERTI] operate cache hit" << std::endl;
  }

  delta_t deltas[BERTI_TABLE_DELTA_SIZE];
  vberti[intern_->cpu]->get(ip.to<uint64_t>(), deltas);

  int num_load = 0;
  for (int i = 0; i < BERTI_TABLE_DELTA_SIZE; i++)
  {
    champsim::address p_addr{(line_addr.to<uint64_t>() + deltas[i].delta) << LOG2_BLOCK_SIZE};
    champsim::block_number p_b_addr{p_addr};

    if (latencyt[intern_->cpu]->get(p_b_addr.to<uint64_t>())) continue;
    if (deltas[i].rpl == BERTI_R) return metadata_in;

    if (champsim::page_number{p_addr} != champsim::page_number{addr})
    {
      cross_page++;
# ifdef NO_CROSS_PAGE
      // We do not cross virtual page
      continue;
# endif
    } else no_cross_page++;

    float mshr_load = ((float) intern_->get_mshr_occupancy() / (float) intern_->get_mshr_size()) * 100;
    //float mshr_load = ((float) (get_occupancy(0, 0) + num_load) / (float) get_size(0, 0)) * 100;

    bool fill_this_level = (deltas[i].rpl == BERTI_L1) && (mshr_load < MSHR_LIMIT);

    if (deltas[i].rpl == BERTI_L1 && mshr_load >= MSHR_LIMIT) pf_to_l2_bc_mshr++; 
    if (fill_this_level) pf_to_l1++;
    else pf_to_l2++;

//Amdiii
// std::cout << metadata_in << std::endl;
    if (prefetch_line(p_addr, fill_this_level, metadata_in))
    {
      if constexpr (champsim::debug_print)
      {
        std::cout << "[BERTI] operate prefetch delta: " << deltas[i].delta;
        std::cout << " p_addr: " << std::hex << p_addr << std::dec;
        std::cout << " this_level: " << +fill_this_level << std::endl;
      }

      if (fill_this_level)
      {
        if (!scache[intern_->cpu]->get(p_b_addr.to<uint64_t>()))
        {
          ++num_load;
          latencyt[intern_->cpu]->add(p_b_addr.to<uint64_t>(), ip.to<uint64_t>(), true, intern_->current_cycle());
        }
      }
    }
  }

  return metadata_in;
}

uint32_t berti::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  champsim::block_number line_addr{addr}; // Line addr
  uint64_t tag     = latencyt[intern_->cpu]->get_tag(line_addr.to<uint64_t>());
  uint64_t cycle   = latencyt[intern_->cpu]->del(line_addr.to<uint64_t>()) & TIME_MASK;
  uint64_t latency = 0;

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI] fill " << intern_->NAME;
    std::cout << " addr: " << std::hex << line_addr;
    std::cout << " event_cycle: " << cycle;
    std::cout << " prefetch: " << std::dec << +prefetch;
    std::cout << " latency: " << latency;
    std::cout << " set: " << set << " way: " << way;
    std::cout << " evicted: " << std::hex << evicted_addr << std::endl;
  }

  if (cycle != 0 && ((intern_->current_cycle() & TIME_MASK) > cycle))
    latency = (intern_->current_cycle() & TIME_MASK) - cycle;

  if (latency > LAT_MASK)
  {
    latency = 0;
    cant_track_latency++;
  } else
  {
    if (latency != 0)
    {
      // Calculate average latency
      if (average_latency.num == 0) average_latency.average = (float) latency;
      else
      {
        average_latency.average = average_latency.average + 
          ((((float) latency) - average_latency.average) / (float)average_latency.num);
      }
      average_latency.num++;
    }
  }

  // Add to the shadow cache
  scache[intern_->cpu]->add((uint32_t)set, (uint32_t)way, line_addr.to<uint64_t>(), prefetch, latency);

  if (latency != 0 && !prefetch)
  {
    vberti[intern_->cpu]->find_and_update(latency, tag, cycle, line_addr.to<uint64_t>(), intern_->cpu);
  }
  return metadata_in;
}

void berti::prefetcher_final_stats()
{
  std::cout << "BERTI " << "TO_L1: " << pf_to_l1 << " TO_L2: " << pf_to_l2;
  std::cout << " TO_L2_BC_MSHR: " << pf_to_l2_bc_mshr << " AVG_LAT: ";
  std::cout << average_latency.average << " NUM_TRACK_LATENCY: ";
  std::cout << average_latency.num << " NUM_CANT_TRACK_LATENCY: ";
  std::cout << cant_track_latency << " CROSS_PAGE: " << cross_page;
  std::cout << " NO_CROSS_PAGE: " << no_cross_page;
  std::cout << " FOUND_BERTI: " << found_berti;
  std::cout << " NO_FOUND_BERTI: " << no_found_berti << std::endl;
}

//void CACHE::prefetcher_speculative_branch(uint64_t ip, uint8_t branch) {}

//void CACHE::prefetcher_commit(uint64_t ip, uint64_t addr, uint8_t branch) {}
