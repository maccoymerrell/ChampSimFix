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

#include <numeric>

namespace sppam_bp {
template<std::size_t... Is>
hashed_perceptron::perceptron_result hashed_perceptron::get_perceptron_result_impl(
    champsim::address pc,
    std::bitset<BP_GLOBAL_BITS> global_hist,
    std::bitset<BP_LOCAL_BITS> local_hist,
    std::index_sequence<Is...>
) {
    auto get_index = [pc_slice = pc](const auto& hist) {
        return hist ^ std::bitset<TABLE_INDEX_BITS>(pc_slice.to<uint64_t>()).to_ullong(); // seed in the PC to spread accesses around (like gshare) XOR in the last word
    };
    perceptron_result result;
    // Compile-time expansion
    ((result.indices[Is] = USE_LOCAL_HISTORY
        ? get_index(fold_bitset<TABLE_INDEX_BITS, history_lengths[Is]>(truncate_bitset<history_lengths[Is]>(local_hist)).to_ullong())
         : get_index(fold_bitset<TABLE_INDEX_BITS, history_lengths[Is]>(truncate_bitset<history_lengths[Is]>(global_hist)).to_ullong())
    ), ...);

    result.yout = std::inner_product(
        std::begin(tables), std::end(tables),
        std::begin(result.indices), 0, std::plus<>{},
        [](const auto& table, const auto& index) { return table.at(index).value(); }
    );
    return result;
}

hashed_perceptron::perceptron_result hashed_perceptron::get_perceptron_result(
    champsim::address pc,
    std::bitset<BP_GLOBAL_BITS> global_hist,
    std::bitset<BP_LOCAL_BITS> local_hist
) {
    return get_perceptron_result_impl(
        pc, global_hist, local_hist,
        std::make_index_sequence<NTABLES>{}
    );
}
std::pair<bool,double> hashed_perceptron::predict_branch(champsim::address pc, std::bitset<BP_GLOBAL_BITS> global_hist, std::bitset<BP_LOCAL_BITS> local_hist)
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

void hashed_perceptron::last_branch_result(champsim::address pc, std::bitset<BP_GLOBAL_BITS> global_hist, std::bitset<BP_LOCAL_BITS> local_hist, bool taken)
{
  perceptron_result result = get_perceptron_result(pc, global_hist, local_hist);
  if(taken)
    outcome_taken++;
  else
    outcome_nottaken++;

  bool prediction_correct = (taken == (result.yout >= THRESHOLD));
  bool prediction_weak = (std::abs(result.yout) < theta);
  if (!prediction_correct || prediction_weak) {
    for (std::size_t i = 0; i < std::size(tables); i++)
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
