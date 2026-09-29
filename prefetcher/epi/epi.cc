////////////////////////////////////////////////////////////////////////
//  The Entangling Instruction Prefetcher (EPI) -- Ros & Jimborean, IPC-1.
//  Faithful port to ChampSimRuntime's modular prefetcher API.
//  Original global [NUM_CPUS] arrays are now per-instance members; the
//  self-maintained timing cache is sized to the real L1I geometry.
////////////////////////////////////////////////////////////////////////

#include "epi.h"

#include <algorithm>
#include <iostream>

#include "champsim.h" // LOG2_BLOCK_SIZE

namespace
{
// ---- constants (masks / bit-widths; not used for member sizing) ----
constexpr uint32_t L1I_MERGE_BBSIZE_BITS = 7;
constexpr uint32_t L1I_MERGE_BBSIZE_MAX_VALUE = (1u << L1I_MERGE_BBSIZE_BITS) - 1;

constexpr uint32_t L1I_TIME_DIFF_BITS = 20;
constexpr uint64_t L1I_TIME_DIFF_OVERFLOW = (uint64_t)1 << L1I_TIME_DIFF_BITS;
constexpr uint64_t L1I_TIME_DIFF_MASK = L1I_TIME_DIFF_OVERFLOW - 1;

constexpr uint32_t L1I_TIME_BITS = 12;
constexpr uint64_t L1I_TIME_OVERFLOW = (uint64_t)1 << L1I_TIME_BITS;
constexpr uint64_t L1I_TIME_MASK = L1I_TIME_OVERFLOW - 1;

constexpr uint32_t L1I_HIST_TABLE_MASK = L1I_HIST_TABLE_ENTRIES - 1; // additive-decrement trick
constexpr uint32_t L1I_BB_MERGE_ENTRIES = 4;
constexpr uint32_t L1I_HIST_TAG_BITS = 58;
constexpr uint64_t L1I_HIST_TAG_MASK = ((uint64_t)1 << L1I_HIST_TAG_BITS) - 1;

// entangled compression formats
constexpr uint32_t L1I_ENTANGLED_FORMATS[7] = {58, 28, 18, 13, 10, 8, 6};

// timing table tag masks (original uses the 58-bit tag width for both)
constexpr uint64_t L1I_TIMING_MSHR_TAG_MASK = ((uint64_t)1 << L1I_HIST_TAG_BITS) - 1;
constexpr uint64_t L1I_TIMING_CACHE_TAG_MASK = ((uint64_t)1 << L1I_HIST_TAG_BITS) - 1;

// entangled table tag
constexpr uint32_t L1I_TAG_BITS = 42 - L1I_ENTANGLED_TABLE_INDEX_BITS; // 34
constexpr uint64_t L1I_TAG_MASK = ((uint64_t)1 << L1I_TAG_BITS) - 1;

constexpr uint32_t L1I_CONFIDENCE_COUNTER_MAX_VALUE = 3;
constexpr uint32_t L1I_CONFIDENCE_COUNTER_THRESHOLD = 1;

constexpr uint32_t L1I_TRIES_AVAIL_ENTANGLED = 6;
constexpr uint32_t L1I_TRIES_AVAIL_ENTANGLED_NOT_PRESENT = 2;

constexpr uint32_t L1I_XPQ_MASK = L1I_XPQ_ENTRIES - 1;

constexpr uint64_t NO_BERE = static_cast<uint64_t>(-1);
} // namespace

champsim::modules::prefetcher::register_module<epi> epi_register("epi");

uint64_t epi::get_latency(uint64_t cycle, uint64_t cycle_prev) const
{
  uint64_t cycle_masked = cycle & L1I_TIME_MASK;
  uint64_t cycle_prev_masked = cycle_prev & L1I_TIME_MASK;
  if (cycle_prev_masked > cycle_masked) {
    return (cycle_masked + L1I_TIME_OVERFLOW) - cycle_prev_masked;
  }
  return cycle_masked - cycle_prev_masked;
}

// ---------------- HISTORY TABLE ----------------

void epi::init_hist_table()
{
  hist_table_head_ = 0;
  hist_table_head_time_ = current_cycle_;
  for (auto& e : hist_table_) {
    e.tag = 0;
    e.time_diff = 0;
    e.bb_size = 0;
  }
  hist_tag_index_.clear();
}

// The ring is written only at its head, so the entry written becomes the newest; the one it replaces was the oldest.
void epi::set_hist_head_tag(uint64_t tag)
{
  auto& entry = hist_table_[hist_table_head_];
  if (entry.tag != 0) {
    auto old = hist_tag_index_.find(entry.tag);
    if (--old->second.count == 0)
      hist_tag_index_.erase(old);
  }
  entry.tag = tag;
  if (tag != 0) {
    auto& occ = hist_tag_index_[tag];
    occ.newest = static_cast<uint32_t>(hist_table_head_);
    ++occ.count;
  }
}

uint64_t epi::find_hist_entry(uint64_t line_addr) const
{
  uint64_t tag = line_addr & L1I_HIST_TAG_MASK;
  if (tag != 0) {
    auto it = hist_tag_index_.find(tag);
    return it == std::end(hist_tag_index_) ? L1I_HIST_TABLE_ENTRIES : it->second.newest;
  }
  for (uint32_t count = 0, i = (hist_table_head_ + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES; count < L1I_HIST_TABLE_ENTRIES;
       count++, i = (i + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES) {
    if (hist_table_[i].tag == tag)
      return i;
  }
  return L1I_HIST_TABLE_ENTRIES;
}

// It can have duplicated entries if the line was evicted in between
void epi::add_hist_table(uint64_t line_addr)
{
  // Insert empty addresses in hist not to have timediff overflows
  while (current_cycle_ - hist_table_head_time_ >= L1I_TIME_DIFF_OVERFLOW) {
    set_hist_head_tag(0);
    hist_table_[hist_table_head_].time_diff = L1I_TIME_DIFF_MASK;
    hist_table_[hist_table_head_].bb_size = 0;
    hist_table_head_ = (hist_table_head_ + 1) % L1I_HIST_TABLE_ENTRIES;
    hist_table_head_time_ += L1I_TIME_DIFF_MASK;
  }

  set_hist_head_tag(line_addr & L1I_HIST_TAG_MASK);
  hist_table_[hist_table_head_].time_diff = (current_cycle_ - hist_table_head_time_) & L1I_TIME_DIFF_MASK;
  hist_table_[hist_table_head_].bb_size = 0;
  hist_table_head_ = (hist_table_head_ + 1) % L1I_HIST_TABLE_ENTRIES;
  hist_table_head_time_ = current_cycle_;
}

void epi::add_bb_size_hist_table(uint64_t line_addr, uint32_t bb_size)
{
  uint64_t index = find_hist_entry(line_addr);
  if (index == L1I_HIST_TABLE_ENTRIES)
    return;
  hist_table_[index].bb_size = bb_size & L1I_MERGE_BBSIZE_MAX_VALUE;
}

uint32_t epi::find_bb_merge_hist_table(uint64_t line_addr) const
{
  uint64_t tag = line_addr & L1I_HIST_TAG_MASK;
  for (uint32_t count = 0, i = (hist_table_head_ + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES; count < L1I_HIST_TABLE_ENTRIES;
       count++, i = (i + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES) {
    if (count >= L1I_BB_MERGE_ENTRIES) {
      return 0;
    }
    if (tag > hist_table_[i].tag && (tag - hist_table_[i].tag) <= hist_table_[i].bb_size) {
      return static_cast<uint32_t>(tag - hist_table_[i].tag);
    }
  }
  return 0;
}

// bere (best request -- entangled address) candidates: bere[skip] is the skip-th candidate, 0 if there is none.
// Returns false (the original's NO_BERE) when the line has no history tag.
bool epi::get_bere_hist_table(uint64_t line_addr, uint64_t latency, uint64_t* bere, uint32_t tries) const
{
  std::fill_n(bere, tries, 0);
  uint64_t tag = line_addr & L1I_HIST_TAG_MASK;
  if (!tag) {
    return false;
  }
  uint32_t first = (hist_table_head_ + L1I_HIST_TABLE_MASK) % L1I_HIST_TABLE_ENTRIES;
  uint64_t time_i = hist_table_head_time_;
  uint64_t req_time = 0;
  uint32_t found = 0;
  for (uint32_t count = 0, i = first; count < L1I_HIST_TABLE_ENTRIES && found < tries; count++, i = (i == 0 ? L1I_HIST_TABLE_ENTRIES : i) - 1) {
    if (req_time == 0 && time_i + latency < current_cycle_) {
      break; // entry times only decrease from here, so none of the rest can be the request
    }
    if (req_time == 0 && hist_table_[i].tag == tag && time_i + latency >= current_cycle_) { // Its me (miss or late prefetcher)
      req_time = time_i;
    } else if (req_time) { // Not me (check only older than me)
      if (hist_table_[i].tag == tag) {
        break; // Second time it appeared (evicted in between) or many for the same set. No entangle
      }
      if (time_i + latency <= req_time && hist_table_[i].tag) {
        bere[found++] = hist_table_[i].tag;
      }
    }
    time_i -= hist_table_[i].time_diff;
  }
  return true;
}

// ---------------- TIMING TABLES ----------------

void epi::init_timing_tables()
{
  tc_num_set_ = static_cast<uint32_t>(cache_->num_sets());
  tc_num_way_ = static_cast<uint32_t>(cache_->num_ways());
  tc_num_set_bits_ = 0;
  for (uint32_t s = tc_num_set_; s > 1; s >>= 1)
    tc_num_set_bits_++;
  timing_cache_table_.assign(tc_num_set_, std::vector<l1i_timing_cache_entry>(tc_num_way_, l1i_timing_cache_entry{}));
  for (auto& e : timing_mshr_table_)
    e.valid = false;
  timing_mshr_slot_.clear();
  timing_mshr_free_.fill(0);
  for (uint32_t i = 0; i < L1I_TIMING_MSHR_SIZE; i++)
    timing_mshr_free_[i / 64] |= uint64_t{1} << (i % 64);
}

void epi::set_timing_mshr_valid(uint32_t slot, bool valid)
{
  auto& entry = timing_mshr_table_[slot];
  if (entry.valid)
    timing_mshr_slot_.erase(entry.tag);
  entry.valid = valid;
  if (valid) {
    timing_mshr_slot_.emplace(entry.tag, slot);
    timing_mshr_free_[slot / 64] &= ~(uint64_t{1} << (slot % 64));
  } else {
    timing_mshr_free_[slot / 64] |= uint64_t{1} << (slot % 64);
  }
}

uint64_t epi::find_timing_mshr_entry(uint64_t line_addr) const
{
  auto it = timing_mshr_slot_.find(line_addr & L1I_TIMING_MSHR_TAG_MASK);
  return it == std::end(timing_mshr_slot_) ? L1I_TIMING_MSHR_SIZE : it->second;
}

uint64_t epi::find_timing_cache_entry(uint64_t line_addr) const
{
  uint64_t i = line_addr % tc_num_set_;
  for (uint32_t j = 0; j < tc_num_way_; j++) {
    if (timing_cache_table_[i][j].tag == ((line_addr >> tc_num_set_bits_) & L1I_TIMING_CACHE_TAG_MASK) && timing_cache_table_[i][j].valid)
      return j;
  }
  return tc_num_way_;
}

uint32_t epi::get_invalid_timing_mshr_entry() const
{
  for (std::size_t w = 0; w < std::size(timing_mshr_free_); w++) {
    if (timing_mshr_free_[w])
      return static_cast<uint32_t>(w * 64 + __builtin_ctzll(timing_mshr_free_[w]));
  }
  return L1I_TIMING_MSHR_SIZE;
}

uint32_t epi::get_invalid_timing_cache_entry(uint64_t line_addr) const
{
  uint32_t i = line_addr % tc_num_set_;
  for (uint32_t j = 0; j < tc_num_way_; j++) {
    if (!timing_cache_table_[i][j].valid)
      return j;
  }
  return tc_num_way_;
}

void epi::add_timing_entry(uint64_t line_addr, uint64_t bere_line_addr)
{
  // First find for coalescing
  if (find_timing_mshr_entry(line_addr) < L1I_TIMING_MSHR_SIZE)
    return;
  if (find_timing_cache_entry(line_addr) < tc_num_way_)
    return;

  uint32_t i = get_invalid_timing_mshr_entry();
  if (i == L1I_TIMING_MSHR_SIZE) {
    return;
  }
  timing_mshr_table_[i].tag = line_addr & L1I_TIMING_MSHR_TAG_MASK;
  set_timing_mshr_valid(i, true);
  timing_mshr_table_[i].bere_line_addr = bere_line_addr;
  timing_mshr_table_[i].timestamp = current_cycle_ & L1I_TIME_MASK;
  timing_mshr_table_[i].accessed = false;
}

void epi::invalid_timing_mshr_entry(uint64_t line_addr)
{
  uint32_t index = find_timing_mshr_entry(line_addr);
  if (index >= L1I_TIMING_MSHR_SIZE)
    return;
  set_timing_mshr_valid(index, false);
}

void epi::move_timing_entry(uint64_t line_addr)
{
  uint32_t index_mshr = find_timing_mshr_entry(line_addr);
  if (index_mshr == L1I_TIMING_MSHR_SIZE) {
    uint32_t set = line_addr % tc_num_set_;
    uint32_t index_cache = get_invalid_timing_cache_entry(line_addr);
    if (index_cache == tc_num_way_) {
      return;
    }
    timing_cache_table_[set][index_cache].valid = true;
    timing_cache_table_[set][index_cache].tag = (line_addr >> tc_num_set_bits_) & L1I_TIMING_CACHE_TAG_MASK;
    timing_cache_table_[set][index_cache].accessed = true;
    return;
  }
  uint64_t set = line_addr % tc_num_set_;
  uint64_t index_cache = get_invalid_timing_cache_entry(line_addr);
  if (index_cache == tc_num_way_) {
    return;
  }
  timing_cache_table_[set][index_cache].valid = true;
  timing_cache_table_[set][index_cache].tag = (line_addr >> tc_num_set_bits_) & L1I_TIMING_CACHE_TAG_MASK;
  timing_cache_table_[set][index_cache].bere_line_addr = timing_mshr_table_[index_mshr].bere_line_addr;
  timing_cache_table_[set][index_cache].accessed = timing_mshr_table_[index_mshr].accessed;
  invalid_timing_mshr_entry(line_addr);
}

// returns if accessed
bool epi::invalid_timing_cache_entry(uint64_t line_addr, uint64_t& bere_line_addr)
{
  uint32_t set = line_addr % tc_num_set_;
  uint32_t way = find_timing_cache_entry(line_addr);
  if (way >= tc_num_way_) {
    return false;
  }
  timing_cache_table_[set][way].valid = false;
  bere_line_addr = timing_cache_table_[set][way].bere_line_addr;
  return timing_cache_table_[set][way].accessed;
}

void epi::access_timing_entry(uint64_t line_addr)
{
  uint32_t index = find_timing_mshr_entry(line_addr);
  if (index < L1I_TIMING_MSHR_SIZE) {
    if (!timing_mshr_table_[index].accessed) {
      timing_mshr_table_[index].accessed = true;
    }
    return;
  }
  uint32_t set = line_addr % tc_num_set_;
  uint32_t way = find_timing_cache_entry(line_addr);
  if (way < tc_num_way_) {
    timing_cache_table_[set][way].accessed = true;
  }
}

bool epi::is_accessed_timing_entry(uint64_t line_addr) const
{
  uint32_t index = find_timing_mshr_entry(line_addr);
  if (index < L1I_TIMING_MSHR_SIZE) {
    return timing_mshr_table_[index].accessed;
  }
  uint32_t set = line_addr % tc_num_set_;
  uint32_t way = find_timing_cache_entry(line_addr);
  if (way < tc_num_way_) {
    return timing_cache_table_[set][way].accessed;
  }
  return false;
}

bool epi::completed_request(uint64_t line_addr) const { return find_timing_cache_entry(line_addr) < tc_num_way_; }

bool epi::ongoing_request(uint64_t line_addr) const { return find_timing_mshr_entry(line_addr) < L1I_TIMING_MSHR_SIZE; }

bool epi::ongoing_accessed_request(uint64_t line_addr) const
{
  uint32_t index = find_timing_mshr_entry(line_addr);
  if (index == L1I_TIMING_MSHR_SIZE)
    return false;
  return timing_mshr_table_[index].accessed;
}

uint64_t epi::get_latency_timing_mshr(uint64_t line_addr) const
{
  uint32_t index = find_timing_mshr_entry(line_addr);
  if (index == L1I_TIMING_MSHR_SIZE)
    return 0;
  if (!timing_mshr_table_[index].accessed)
    return 0;
  return get_latency(current_cycle_, timing_mshr_table_[index].timestamp);
}

// ---------------- ENTANGLED TABLE ----------------

uint32_t epi::get_format_entangled(uint64_t line_addr, uint64_t entangled_addr) const
{
  for (uint32_t i = L1I_ENTANGLED_NUM_FORMATS; i != 0; i--) {
    if ((line_addr >> L1I_ENTANGLED_FORMATS[i - 1]) == (entangled_addr >> L1I_ENTANGLED_FORMATS[i - 1])) {
      return i;
    }
  }
  return 1; // format 1 (58 low bits) always matches for realistic addresses
}

uint64_t epi::extend_format_entangled(uint64_t line_addr, uint64_t entangled_addr, uint32_t format) const
{
  return ((line_addr >> L1I_ENTANGLED_FORMATS[format - 1]) << L1I_ENTANGLED_FORMATS[format - 1])
         | (entangled_addr & (((uint64_t)1 << L1I_ENTANGLED_FORMATS[format - 1]) - 1));
}

uint64_t epi::compress_format_entangled(uint64_t entangled_addr, uint32_t format) const
{
  return entangled_addr & (((uint64_t)1 << L1I_ENTANGLED_FORMATS[format - 1]) - 1);
}

void epi::init_entangled_table()
{
  for (uint32_t i = 0; i < L1I_ENTANGLED_TABLE_SETS; i++) {
    for (uint32_t j = 0; j < L1I_ENTANGLED_TABLE_WAYS; j++) {
      set_entangled_tag(i, j, 0);
      entangled_table_[i][j].format = 1;
      for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
        entangled_table_[i][j].entangled_addr[k] = 0;
        entangled_table_[i][j].entangled_conf[k] = 0;
      }
      entangled_table_[i][j].bb_size = 0;
    }
    entangled_fifo_[i] = 0;
  }
}

void epi::set_entangled_tag(uint32_t set, uint32_t way, uint64_t tag)
{
  entangled_table_[set][way].tag = tag;
  entangled_tags_[set][way] = tag;
}

uint32_t epi::get_way_entangled_table(uint64_t line_addr) const
{
  uint64_t tag = (line_addr >> L1I_ENTANGLED_TABLE_INDEX_BITS) & L1I_TAG_MASK;
  uint32_t set = line_addr % L1I_ENTANGLED_TABLE_SETS;
  for (uint32_t i = 0; i < L1I_ENTANGLED_TABLE_WAYS; i++) {
    if (entangled_tags_[set][i] == tag) {
      return i;
    }
  }
  return L1I_ENTANGLED_TABLE_WAYS;
}

void epi::add_entangled_table(uint64_t line_addr, uint64_t entangled_addr)
{
  uint64_t tag = (line_addr >> L1I_ENTANGLED_TABLE_INDEX_BITS) & L1I_TAG_MASK;
  uint32_t set = line_addr % L1I_ENTANGLED_TABLE_SETS;
  uint32_t way = get_way_entangled_table(line_addr);
  if (way == L1I_ENTANGLED_TABLE_WAYS) {
    way = entangled_fifo_[set];
    set_entangled_tag(set, way, tag);
    entangled_table_[set][way].format = 1;
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      entangled_table_[set][way].entangled_addr[k] = 0;
      entangled_table_[set][way].entangled_conf[k] = 0;
    }
    entangled_table_[set][way].bb_size = 0;
    entangled_fifo_[set] = (entangled_fifo_[set] + 1) % L1I_ENTANGLED_TABLE_WAYS;
  }
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (entangled_table_[set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
        && extend_format_entangled(line_addr, entangled_table_[set][way].entangled_addr[k], entangled_table_[set][way].format) == entangled_addr) {
      entangled_table_[set][way].entangled_conf[k] = L1I_CONFIDENCE_COUNTER_MAX_VALUE;
      return;
    }
  }

  // Adding a new entangled
  uint32_t format_new = get_format_entangled(line_addr, entangled_addr);

  // Check for evictions
  while (true) {
    uint32_t min_format = format_new;
    uint32_t num_valid = 1;
    uint32_t min_value = L1I_CONFIDENCE_COUNTER_MAX_VALUE + 1;
    uint32_t min_pos = 0;
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      if (entangled_table_[set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
        num_valid++;
        uint32_t format_k =
            get_format_entangled(line_addr, extend_format_entangled(line_addr, entangled_table_[set][way].entangled_addr[k], entangled_table_[set][way].format));
        if (format_k < min_format) {
          min_format = format_k;
        }
        if (entangled_table_[set][way].entangled_conf[k] < min_value) {
          min_value = entangled_table_[set][way].entangled_conf[k];
          min_pos = k;
        }
      }
    }
    if (num_valid > min_format) { // Eviction is necessary. Choose the lower confidence one
      entangled_table_[set][way].entangled_conf[min_pos] = 0;
    } else {
      // Reformat
      for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
        if (entangled_table_[set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
          entangled_table_[set][way].entangled_addr[k] = compress_format_entangled(
              extend_format_entangled(line_addr, entangled_table_[set][way].entangled_addr[k], entangled_table_[set][way].format), min_format);
        }
      }
      entangled_table_[set][way].format = min_format;
      break;
    }
  }
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (entangled_table_[set][way].entangled_conf[k] < L1I_CONFIDENCE_COUNTER_THRESHOLD) {
      entangled_table_[set][way].entangled_addr[k] = compress_format_entangled(entangled_addr, entangled_table_[set][way].format);
      entangled_table_[set][way].entangled_conf[k] = L1I_CONFIDENCE_COUNTER_MAX_VALUE;
      return;
    }
  }
}

bool epi::avail_entangled_table(uint64_t line_addr, uint64_t entangled_addr, bool insert_not_present) const
{
  uint32_t set = line_addr % L1I_ENTANGLED_TABLE_SETS;
  uint32_t way = get_way_entangled_table(line_addr);
  if (way == L1I_ENTANGLED_TABLE_WAYS)
    return insert_not_present;
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (entangled_table_[set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
        && extend_format_entangled(line_addr, entangled_table_[set][way].entangled_addr[k], entangled_table_[set][way].format) == entangled_addr) {
      return true;
    }
  }
  uint32_t min_format = get_format_entangled(line_addr, entangled_addr);
  uint32_t num_valid = 1;
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    if (entangled_table_[set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
      num_valid++;
      uint32_t format_k =
          get_format_entangled(line_addr, extend_format_entangled(line_addr, entangled_table_[set][way].entangled_addr[k], entangled_table_[set][way].format));
      if (format_k < min_format) {
        min_format = format_k;
      }
    }
  }
  if (num_valid > min_format) { // Eviction is necessary
    return false;
  } else {
    return true;
  }
}

void epi::add_bbsize_table(uint64_t line_addr, uint32_t bb_size)
{
  uint64_t tag = (line_addr >> L1I_ENTANGLED_TABLE_INDEX_BITS) & L1I_TAG_MASK;
  uint32_t set = line_addr % L1I_ENTANGLED_TABLE_SETS;
  uint32_t way = get_way_entangled_table(line_addr);
  if (way == L1I_ENTANGLED_TABLE_WAYS) {
    way = entangled_fifo_[set];
    set_entangled_tag(set, way, tag);
    entangled_table_[set][way].format = 1;
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      entangled_table_[set][way].entangled_addr[k] = 0;
      entangled_table_[set][way].entangled_conf[k] = 0;
    }
    entangled_table_[set][way].bb_size = 0;
    entangled_fifo_[set] = (entangled_fifo_[set] + 1) % L1I_ENTANGLED_TABLE_WAYS;
  }
  if (bb_size > entangled_table_[set][way].bb_size) {
    entangled_table_[set][way].bb_size = bb_size & L1I_MERGE_BBSIZE_MAX_VALUE;
  }
}

uint64_t epi::get_entangled_addr_entangled_table(uint64_t line_addr, uint32_t index_k) const
{
  return get_entangled_addr_entangled_table(line_addr, index_k, get_way_entangled_table(line_addr));
}

uint64_t epi::get_entangled_addr_entangled_table(uint64_t line_addr, uint32_t index_k, uint32_t way) const
{
  uint32_t set = line_addr % L1I_ENTANGLED_TABLE_SETS;
  if (way < L1I_ENTANGLED_TABLE_WAYS) {
    if (entangled_table_[set][way].entangled_conf[index_k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
      return extend_format_entangled(line_addr, entangled_table_[set][way].entangled_addr[index_k], entangled_table_[set][way].format);
    }
  }
  return 0;
}

uint32_t epi::get_bbsize_entangled_table(uint64_t line_addr) const { return get_bbsize_entangled_table(line_addr, get_way_entangled_table(line_addr)); }

uint32_t epi::get_bbsize_entangled_table(uint64_t line_addr, uint32_t way) const
{
  uint32_t set = line_addr % L1I_ENTANGLED_TABLE_SETS;
  if (way < L1I_ENTANGLED_TABLE_WAYS) {
    return entangled_table_[set][way].bb_size;
  }
  return 0;
}

void epi::update_confidence_entangled_table(uint64_t line_addr, uint64_t entangled_addr, bool accessed)
{
  uint32_t set = line_addr % L1I_ENTANGLED_TABLE_SETS;
  uint32_t way = get_way_entangled_table(line_addr);
  if (way < L1I_ENTANGLED_TABLE_WAYS) {
    for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
      if (entangled_table_[set][way].entangled_conf[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
          && extend_format_entangled(line_addr, entangled_table_[set][way].entangled_addr[k], entangled_table_[set][way].format) == entangled_addr) {
        if (accessed && entangled_table_[set][way].entangled_conf[k] < L1I_CONFIDENCE_COUNTER_MAX_VALUE) {
          entangled_table_[set][way].entangled_conf[k]++;
        }
        if (!accessed && entangled_table_[set][way].entangled_conf[k] > 0) {
          entangled_table_[set][way].entangled_conf[k]--;
        }
      }
    }
  }
}

// ---------------- EXTRA PREFETCH QUEUE ----------------

void epi::init_xpq()
{
  xpq_head_ = 0;
  for (auto& e : xpq_) {
    e.line_addr = 0;
    e.entangled_addr = 0;
    e.bb_size = 0;
  }
}

void epi::add_xpq(uint64_t line_addr, uint64_t entangled_addr, uint32_t bb_size)
{
  if (bb_size == 0)
    return;

  // Merge if possible
  uint32_t first = (xpq_head_ + L1I_XPQ_MASK) % L1I_XPQ_ENTRIES;
  for (uint32_t count = 0, i = first; count < L1I_XPQ_ENTRIES; count++, i = (i + L1I_XPQ_MASK) % L1I_XPQ_ENTRIES) {
    if (xpq_[xpq_head_].bb_size && line_addr == xpq_[i].line_addr) {
      if (xpq_[xpq_head_].bb_size < bb_size) {
        xpq_[xpq_head_].bb_size = bb_size;
        return;
      }
    }
  }

  xpq_[xpq_head_].line_addr = line_addr;
  xpq_[xpq_head_].entangled_addr = entangled_addr;
  xpq_[xpq_head_].bb_size = bb_size;
  xpq_head_ = (xpq_head_ + 1) % L1I_XPQ_ENTRIES;
}

bool epi::empty_xpq() const { return xpq_[(xpq_head_ + L1I_XPQ_MASK) % L1I_XPQ_ENTRIES].bb_size == 0; }

// Returns next line to prefetch
uint64_t epi::get_xpq(uint64_t& entangled_addr)
{
  // find tail
  uint32_t tail;
  for (tail = xpq_head_; tail != (xpq_head_ + L1I_XPQ_MASK) % L1I_XPQ_ENTRIES; tail = (tail + 1) % L1I_XPQ_ENTRIES) {
    if (xpq_[tail].bb_size) {
      break;
    }
  }

  uint64_t pf_addr = xpq_[tail].line_addr;
  entangled_addr = xpq_[tail].entangled_addr;

  // update queue
  xpq_[tail].bb_size--;
  if (xpq_[tail].bb_size == 0) {
    return pf_addr;
  }
  xpq_[tail].line_addr++;
  xpq_[tail].entangled_addr = 0;
  return pf_addr;
}

// ---------------- ISSUE ----------------

bool epi::pq_full() const
{
  auto occ = cache_->get_pq_occupancy();
  auto cap = cache_->get_pq_size();
  std::size_t to = 0, tc = 0;
  for (auto o : occ)
    to += o;
  for (auto c : cap)
    tc += c;
  return tc > 0 ? to >= tc : false; // tc==0 (unlimited): xpq drains -> loop still bounded
}

void epi::do_prefetches(uint32_t metadata_in)
{
  while (!empty_xpq() && !pq_full()) {
    uint64_t entangled_addr = 0;
    uint64_t pf_line_addr = get_xpq(entangled_addr);
    if (!ongoing_request(pf_line_addr)) {
      if (prefetch_line(champsim::address{champsim::block_number{pf_line_addr}}, true, metadata_in))
        ++stat_pf_issued_;
      add_timing_entry(pf_line_addr, entangled_addr);
    }
  }
}

// ---------------- MODULE INTERFACE ----------------

void epi::prefetcher_initialize()
{
  std::cout << "CPU L1I EPI (Entangling) prefetcher" << std::endl;
  current_cycle_ = cache_->current_cycle();
  last_basic_block_ = 0;
  consecutive_count_ = 0;
  basic_block_merge_diff_ = 0;
  init_hist_table();
  init_timing_tables();
  init_entangled_table();
  init_xpq();
}

uint32_t epi::prefetcher_cache_operate(champsim::address addr, champsim::address /*ip*/, bool cache_hit, bool /*useful_prefetch*/, access_type /*type*/,
                                       uint32_t metadata_in)
{
  current_cycle_ = cache_->current_cycle();
  uint64_t line_addr = champsim::block_number{addr}.to<uint64_t>();

  bool consecutive = false;
  if (last_basic_block_ + consecutive_count_ == line_addr) { // Same
    return metadata_in;
  } else if (last_basic_block_ + consecutive_count_ + 1 == line_addr) { // Consecutive
    consecutive_count_++;
    consecutive = true;
  }

  // Queue basic block prefetches (the entangled table is only read until the lookups below finish)
  uint32_t line_way = get_way_entangled_table(line_addr);
  uint32_t bb_size = get_bbsize_entangled_table(line_addr, line_way);
  if (bb_size > 0) {
    add_xpq(line_addr + 1, 0, bb_size);
  }

  // Queue entangled and basic block of entangled prefetches
  for (uint32_t k = 0; k < L1I_MAX_ENTANGLED_PER_LINE; k++) {
    uint64_t entangled_addr = get_entangled_addr_entangled_table(line_addr, k, line_way);
    if (entangled_addr && (entangled_addr != line_addr)) {
      uint32_t ent_bb_size = get_bbsize_entangled_table(entangled_addr);
      add_xpq(entangled_addr, line_addr, ent_bb_size + 1);
    }
  }

  if (!consecutive) { // New basic block found
    uint32_t max_bb_size = get_bbsize_entangled_table(last_basic_block_);

    // Check for merging bb opportunities
    if (consecutive_count_) { // single blocks no need to merge
      if (basic_block_merge_diff_ > 0) {
        add_bbsize_table(last_basic_block_ - basic_block_merge_diff_, consecutive_count_ + basic_block_merge_diff_);
        add_bb_size_hist_table(last_basic_block_ - basic_block_merge_diff_, consecutive_count_ + basic_block_merge_diff_);
      } else {
        add_bbsize_table(last_basic_block_, std::max(max_bb_size, consecutive_count_));
        add_bb_size_hist_table(last_basic_block_, std::max(max_bb_size, consecutive_count_));
      }
    }
  }

  if (!consecutive) { // New basic block found
    consecutive_count_ = 0;
    last_basic_block_ = line_addr;
  }

  if (!consecutive) {
    basic_block_merge_diff_ = find_bb_merge_hist_table(last_basic_block_);
  }

  // Add the request in the history buffer
  if (!consecutive && basic_block_merge_diff_ == 0) {
    if (find_hist_entry(line_addr) == L1I_HIST_TABLE_ENTRIES) {
      add_hist_table(line_addr);
    } else {
      if (!cache_hit && !ongoing_accessed_request(line_addr)) {
        add_hist_table(line_addr);
      }
    }
  }

  // Add miss in the latency table
  if (!cache_hit && !ongoing_request(line_addr)) {
    add_timing_entry(line_addr, 0);
    access_timing_entry(line_addr);
  } else {
    access_timing_entry(line_addr);
  }

  do_prefetches(metadata_in);

  return metadata_in;
}

void epi::prefetcher_cycle_operate()
{
  current_cycle_ = cache_->current_cycle();
  do_prefetches(0);
}

uint32_t epi::prefetcher_cache_fill(champsim::address addr, long /*set*/, long /*way*/, bool /*prefetch*/, champsim::address evicted_addr, uint32_t metadata_in)
{
  current_cycle_ = cache_->current_cycle();
  uint64_t line_addr = champsim::block_number{addr}.to<uint64_t>();
  uint64_t evicted_line_addr = champsim::block_number{evicted_addr}.to<uint64_t>();

  // Line is in cache -> handle eviction of the previous occupant (confidence update)
  if (evicted_line_addr) {
    uint64_t bere_line_addr = 0;
    bool accessed = invalid_timing_cache_entry(evicted_line_addr, bere_line_addr);
    if (bere_line_addr != 0) {
      update_confidence_entangled_table(bere_line_addr, evicted_line_addr, accessed);
    }
  }

  uint64_t latency = get_latency_timing_mshr(line_addr);

  move_timing_entry(line_addr);

  // Get and update entangled
  if (latency) {
    // The history table does not change during the fill, so one scan gives the candidate for every try
    static_assert(L1I_TRIES_AVAIL_ENTANGLED_NOT_PRESENT <= L1I_TRIES_AVAIL_ENTANGLED);
    std::array<uint64_t, L1I_TRIES_AVAIL_ENTANGLED> beres;
    const bool has_bere = get_bere_hist_table(line_addr, latency, beres.data(), L1I_TRIES_AVAIL_ENTANGLED);
    auto bere_at = [&](uint32_t skip) { return has_bere ? beres[skip] : NO_BERE; };

    bool inserted = false;
    for (uint32_t i = 0; i < L1I_TRIES_AVAIL_ENTANGLED; i++) {
      uint64_t bere = bere_at(i);
      if (bere == NO_BERE) {
        continue;
      }
      if (bere && line_addr != bere) {
        if (avail_entangled_table(bere, line_addr, false)) {
          add_entangled_table(bere, line_addr);
          inserted = true;
          break;
        }
      }
    }
    if (!inserted) {
      for (uint32_t i = 0; i < L1I_TRIES_AVAIL_ENTANGLED_NOT_PRESENT; i++) {
        uint64_t bere = bere_at(i);
        if (bere == NO_BERE) {
          continue;
        }
        if (bere && line_addr != bere) {
          if (avail_entangled_table(bere, line_addr, true)) {
            add_entangled_table(bere, line_addr);
            inserted = true;
            break;
          }
        }
      }
    }
    if (!inserted) {
      uint64_t bere = bere_at(0);
      if (bere == NO_BERE) {
        return metadata_in;
      }
      if (bere && line_addr != bere) {
        add_entangled_table(bere, line_addr);
      }
    }
  }

  return metadata_in;
}

void epi::prefetcher_final_stats() { std::cout << "CPU L1I EPI prefetcher final stats: prefetches issued=" << stat_pf_issued_ << std::endl; }
