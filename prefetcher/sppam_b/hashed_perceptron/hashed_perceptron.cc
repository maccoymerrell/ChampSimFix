/*

This code implements a hashed perceptron branch predictor using geometric
history lengths and dynamic threshold setting.  It was written by Daniel
A. Jiménez in March 2019.


The original perceptron branch predictor is from Jiménez and Lin, "Dynamic
Branch Prediction with Perceptrons," HPCA 2001.

The idea of using multiple independently indexed tables of perceptron weights
is from Jiménez, "Fast Path-Based Neural Branch Prediction," MICRO 2003 and
later expanded in "Piecewise Linear Branch Prediction" from ISCA 2005.

The idea of using hashes of branch history to reduce the number of independent
tables is documented in three contemporaneous papers:

1. Seznec, "Revisiting the Perceptron Predictor," IRISA technical report, 2004.

2. Tarjan and Skadron, "Revisiting the Perceptron Predictor Again," UVA
technical report, 2004, expanded and published in ACM TACO 2005 as "Merging
path and gshare indexing in perceptron branch prediction"; introduces the term
"hashed perceptron."

3. Loh and Jiménez, "Reducing the Power and Complexity of Path-Based Neural
Branch Prediction," WCED 2005.

The ideas of using "geometric history lengths" i.e. hashing into tables with
histories of exponentially increasing length, as well as dynamically adjusting
the theta parameter, are from Seznec, "The O-GEHL Branch Predictor," from CBP
2004, expanded later as "Analysis of the O-GEometric History Length Branch
Predictor" in ISCA 2005.

This code uses these ideas, but prefers simplicity over absolute accuracy (I
wrote it in about an hour and later spent more time on this comment block than
I did on the code). These papers and subsequent papers by Jiménez and other
authors significantly improve the accuracy of perceptron-based predictors but
involve tricks and analysis beyond the needs of a tool like ChampSim that
targets cache optimizations. If you want accuracy at any cost, see the winners
of the latest branch prediction contest, CBP 2016 as of this writing, but
prepare to have your face melted off by the complexity of the code you find
there. If you are a student being asked to code a good branch predictor for
your computer architecture class, don't copy this code; there are much better
sources for you to plagiarize.

*/

#include "hashed_perceptron.h"

#include <cmath>
#include <numeric>

namespace sppam_bp {

void hashed_perceptron::configure(unsigned int num_tables, unsigned int table_size, unsigned int min_hist, unsigned int max_hist) {
    rt_num_tables = std::min(static_cast<std::size_t>(num_tables), NTABLES);
    if (rt_num_tables == 0) rt_num_tables = 1;
    rt_table_size = std::min(static_cast<std::size_t>(table_size), TABLE_SIZE);
    if (rt_table_size == 0) rt_table_size = 1;
    // compute rt_table_index_bits = floor(log2(rt_table_size))
    rt_table_index_bits = 0;
    for (std::size_t v = rt_table_size; v > 1; v >>= 1) rt_table_index_bits++;
    // compute geometric history lengths from min_hist to max_hist
    rt_history_lengths[0] = 0; // table 0 is bias
    if (rt_num_tables > 2) {
        for (std::size_t i = 1; i < rt_num_tables; i++) {
            double frac = static_cast<double>(i - 1) / static_cast<double>(rt_num_tables - 2);
            rt_history_lengths[i] = static_cast<std::size_t>(
                min_hist * std::pow(static_cast<double>(max_hist) / min_hist, frac) + 0.5);
        }
        rt_history_lengths[rt_num_tables - 1] = max_hist;
    } else if (rt_num_tables == 2) {
        rt_history_lengths[1] = max_hist;
    }
}

hashed_perceptron::perceptron_result hashed_perceptron::get_perceptron_result(
    champsim::address pc,
    const dynamic_bitset& global_hist,
    const dynamic_bitset& local_hist
) {
    uint64_t pc_val = pc.to<uint64_t>() & (rt_table_size - 1);
    perceptron_result result;
    const auto& hist = USE_LOCAL_HISTORY ? local_hist : global_hist;
    for (std::size_t i = 0; i < rt_num_tables; i++) {
        auto truncated = truncate_bitset(hist, rt_history_lengths[i]);
        auto folded = fold_bitset(truncated, rt_table_index_bits);
        result.indices[i] = (folded.to_ullong() ^ pc_val) % rt_table_size;
    }
    result.yout = 0;
    for (std::size_t i = 0; i < rt_num_tables; i++) {
        result.yout += tables[i].at(result.indices[i]).value();
    }
    return result;
}
std::pair<bool,double> hashed_perceptron::predict_branch(champsim::address pc, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, const bp_context& /*ctx*/)
{
  perceptron_result result = get_perceptron_result(pc, global_hist, local_hist);
  if(result.yout >= THRESHOLD)
    predict_taken++;
  else
    predict_nottaken++;

  if(DEBUG) {
    fmt::print("[HASHED_PERCEPTRON] Predicting branch at IP {} with global history {} and local history {} as {}\n",pc,format_bitset(global_hist),format_bitset(local_hist),result.yout >= THRESHOLD ? "taken" : "not taken");
    fmt::print("[HASHED_PERCEPTRON] Perceptron sum is {}, indices are {}\n",result.yout,fmt::join(result.indices,","));
  }
  return std::make_pair(result.yout >= THRESHOLD, static_cast<double>(result.yout));
}

void hashed_perceptron::last_branch_result(champsim::address pc, const dynamic_bitset& global_hist, const dynamic_bitset& local_hist, bool taken, bp_context& /*ctx*/)
{
  perceptron_result result = get_perceptron_result(pc, global_hist, local_hist);
  if(taken)
    outcome_taken++;
  else
    outcome_nottaken++;

  bool prediction_correct = (taken == (result.yout >= THRESHOLD));
  bool prediction_weak = (std::abs(result.yout) < theta);
  if (!prediction_correct || prediction_weak) {
    for (std::size_t i = 0; i < rt_num_tables; i++)
      tables[i][result.indices[i]] += taken ? 1 : -1; // update weights
    adjust_threshold(prediction_correct);
  }

  if(DEBUG) {
    fmt::print("[HASHED_PERCEPTRON] Branch at IP {} with global history {} and local history {} was actually {}\n",pc,format_bitset(global_hist),format_bitset(local_hist),taken ? "taken" : "not taken");
    fmt::print("[HASHED_PERCEPTRON] Perceptron sum is {}, indices are {}\n",result.yout,fmt::join(result.indices,","));
    fmt::print("[HASHED_PERCEPTRON] Prediction was {}. Threshold is {}. Theta is {}.\n",prediction_correct ? "correct" : "incorrect",THRESHOLD,theta);
  }
}

// dynamic threshold setting from Seznec's O-GEHL paper
void hashed_perceptron::adjust_threshold(bool correct)
{
  constexpr int SPEED = 18; // speed for dynamic threshold setting
  if (!correct) {
    // increase theta after enough mispredictions
    tc++;
    if (tc >= SPEED) {
      theta++;
      tc = 0;
    }
  } else {
    // decrease theta after enough weak but correct predictions
    tc--;
    if (tc <= -SPEED) {
      theta--;
      tc = 0;
    }
  }
}

void hashed_perceptron::print_heartbeat() {
  fmt::print("[HASHED_PERCEPTRON] Predicted Taken: {} Predicted Not-taken: {}\n",predict_taken,predict_nottaken);
  fmt::print("[HASHED_PERCEPTRON] Outcome Taken: {} Outcome Not-taken: {}\n",outcome_taken,outcome_nottaken);
  predict_taken = 0;
  predict_nottaken = 0;
  outcome_taken = 0;
  outcome_nottaken = 0;
}

void hashed_perceptron::print_stats() {
  std::ofstream pht_file;
  pht_file.open("sppam_b_predict_hashed_perceptron.txt",std::ios::out | std::ios::trunc);
  for(int i = 0; i < TABLE_SIZE; i++)
    pht_file << fmt::format("PHT {} : {}\n",i,tables[0][i].value());
  pht_file.close();
}
}
