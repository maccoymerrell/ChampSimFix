#include "gshare.h"

namespace sppam_bp {
std::size_t gshare::gs_table_hash(champsim::address ip, std::bitset<GLOBAL_HISTORY_LENGTH> bh_vector, bool predict_or_outcome)
{
  constexpr champsim::data::bits LOG2_HISTORY_TABLE_SIZE{champsim::lg2(GS_HISTORY_TABLE_SIZE)};
  constexpr champsim::data::bits LENGTH{HASH_LENGTH};

  std::size_t hash = bh_vector.to_ullong();
  if(predict_or_outcome)
    gs_history_occurrences_predict[hash % GS_HISTORY_TABLE_SIZE]++;
  else
    gs_history_occurrences_outcome[hash % GS_HISTORY_TABLE_SIZE]++;

  hash ^= ip.slice<LOG2_HISTORY_TABLE_SIZE, champsim::data::bits{}>().to<std::size_t>();
  hash ^= ip.slice<LOG2_HISTORY_TABLE_SIZE + LENGTH, LENGTH>().to<std::size_t>();
  hash ^= ip.slice<LOG2_HISTORY_TABLE_SIZE + 2 * LENGTH, 2 * LENGTH>().to<std::size_t>();

  return hash % GS_HISTORY_TABLE_SIZE;
}

std::pair<bool,double> gshare::predict_branch(champsim::address ip, std::bitset<BP_GLOBAL_BITS> global_hist, std::bitset<BP_LOCAL_BITS> local_hist)
{
  auto gs_hash = USE_LOCAL_HISTORY ? gs_table_hash(ip, truncate_bitset<GLOBAL_HISTORY_LENGTH,BP_LOCAL_BITS>(local_hist),true) : gs_table_hash(ip, truncate_bitset<GLOBAL_HISTORY_LENGTH,BP_GLOBAL_BITS>(global_hist),true);
  auto value = gs_history_table[gs_hash];
  if(value.value() >= (value.maximum / 2.0))
    predict_taken++;
  else
    predict_nottaken++;

  if(DEBUG)
    fmt::print("[GSHARE] PREDICT IP: {} GHIST: {} LHIST: {} VAL: {}\n",ip,global_hist.to_string(),local_hist.to_string(),value.value());
  return {value.value() >= (value.maximum / 2.0), (value.value() /(double) value.maximum)};
}

void gshare::last_branch_result(champsim::address ip, std::bitset<BP_GLOBAL_BITS> global_hist, std::bitset<BP_LOCAL_BITS> local_hist, bool taken)
{
  if(taken)
    outcome_taken++;
  else
    outcome_nottaken++;

  auto gs_hash = USE_LOCAL_HISTORY ? gs_table_hash(ip, truncate_bitset<GLOBAL_HISTORY_LENGTH,BP_LOCAL_BITS>(local_hist),false) : gs_table_hash(ip, truncate_bitset<GLOBAL_HISTORY_LENGTH,BP_GLOBAL_BITS>(global_hist),false);
  gs_history_table[gs_hash] += taken ? COUNTER_UP : COUNTER_DOWN;
  gs_occurrences[gs_hash]++;
  if(DEBUG)
    fmt::print("[GSHARE] OUTCOME IP: {} GHIST: {} LHIST: {} TAKEN: {} VAL: {}\n",ip,global_hist.to_string(),local_hist.to_string(),taken,gs_history_table[gs_hash].value());
}

void gshare::print_heartbeat() {
  fmt::print("[GSHARE] Predicted Taken: {} Predicted Not-taken: {}\n",predict_taken,predict_nottaken);
  fmt::print("[GSHARE] Outcome Taken: {} Outcome Not-taken: {}\n",outcome_taken,outcome_nottaken);
  predict_taken = 0;
  predict_nottaken = 0;
  outcome_taken = 0;
  outcome_nottaken = 0;
}

void gshare::print_stats() {
  std::ofstream pht_file;
  pht_file.open("sppam_b_predict_gshare_sms.txt",std::ios::out | std::ios::trunc);
  for(int i = 0; i < GS_HISTORY_TABLE_SIZE; i++)
    pht_file << fmt::format("PHT {} : {} {} {}/{}\n",i,gs_history_table[i].value(),gs_occurrences[i],gs_history_occurrences_predict[i],gs_history_occurrences_outcome[i]);
  pht_file.close();
}
}