#include "bimodal.h"

namespace sppam_bp {

std::pair<bool,double> bimodal::predict_branch(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, const bp_context& /*ctx*/)
{
  auto idx = hash(ip);
  auto value = bimodal_table[idx];
  bool prediction = value.value() > (value.maximum / 2);

  if(prediction)
    predict_taken++;
  else
    predict_nottaken++;

  if(DEBUG)
    fmt::print("[BIMODAL] PREDICT IP: {} VAL: {} MAX: {}\n", ip, value.value(), value.maximum);

  return {prediction, static_cast<double>(value.value()) / static_cast<double>(value.maximum)};
}

void bimodal::last_branch_result(champsim::address ip, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, bool taken, bp_context& /*ctx*/)
{
  if(taken)
    outcome_taken++;
  else
    outcome_nottaken++;

  auto idx = hash(ip);
  bimodal_table[idx] += taken ? 1 : -1;
  occurrences[idx]++;

  if(DEBUG)
    fmt::print("[BIMODAL] OUTCOME IP: {} TAKEN: {} VAL: {}\n", ip, taken, bimodal_table[idx].value());
}

void bimodal::print_heartbeat() {
  fmt::print("[BIMODAL] Predicted Taken: {} Predicted Not-taken: {}\n", predict_taken, predict_nottaken);
  fmt::print("[BIMODAL] Outcome Taken: {} Outcome Not-taken: {}\n", outcome_taken, outcome_nottaken);
  predict_taken = 0;
  predict_nottaken = 0;
  outcome_taken = 0;
  outcome_nottaken = 0;
}

void bimodal::print_stats() {
  std::ofstream pht_file;
  pht_file.open("sppam_b_predict_bimodal.txt", std::ios::out | std::ios::trunc);
  for(std::size_t i = 0; i < TABLE_SIZE; i++)
    pht_file << fmt::format("PHT {} : {} {}\n", i, bimodal_table[i].value(), occurrences[i]);
  pht_file.close();
}

}
