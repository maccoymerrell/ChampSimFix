#include "mpp.h"

namespace sppam_bp {

unsigned int mpp::get_feature_hash(const mpp_feature_spec& spec, champsim::address ip,
                                   const dynamic_bitset& global_hist,
                                   const dynamic_bitset& local_hist,
                                   const bp_context& ctx,
                                   int table_idx) const
{
  constexpr int INDEX_BITS = 16; // log2(MAX_TABLE_SIZE)
  unsigned int pc = static_cast<unsigned int>(ip.to<uint64_t>());
  unsigned int hpc = hash(pc >> 2, 4);
  unsigned int x = 0;

  switch(spec.type) {
    case MPP_BIAS:
      // Just the hashed IP
      x = hpc;
      break;

    case MPP_GHIST: {
      // Fold global_hist[p1..p2) down to INDEX_BITS, XOR with IP
      int end = (spec.p2 > static_cast<int>(global_hist.size())) ? static_cast<int>(global_hist.size()) : spec.p2;
      x = fold_range(global_hist, spec.p1, end, INDEX_BITS);
      x ^= hpc;
      break;
    }

    case MPP_LHIST: {
      // Fold local_hist[p1..p2) down to INDEX_BITS, XOR with IP
      int end = (spec.p2 > static_cast<int>(local_hist.size())) ? static_cast<int>(local_hist.size()) : spec.p2;
      x = fold_range(local_hist, spec.p1, end, INDEX_BITS);
      x ^= hpc;
      break;
    }

    case MPP_GHISTPATH: {
      // Fold global_hist[p1..p2) XOR with IP path hash of depth p3
      int end = (spec.p2 > static_cast<int>(global_hist.size())) ? static_cast<int>(global_hist.size()) : spec.p2;
      unsigned int g = fold_range(global_hist, spec.p1, end, INDEX_BITS);
      unsigned int p = hash_path(ctx, spec.p3, 3);
      x = g ^ p ^ hpc;
      break;
    }

    case MPP_MIXED: {
      // Fold both global and local, XOR together, XOR with IP
      int gend = (spec.p2 > static_cast<int>(global_hist.size())) ? static_cast<int>(global_hist.size()) : spec.p2;
      int lend = (spec.p2 > static_cast<int>(local_hist.size())) ? static_cast<int>(local_hist.size()) : spec.p2;
      unsigned int g = fold_range(global_hist, spec.p1, gend, INDEX_BITS);
      unsigned int l = fold_range(local_hist, spec.p1, lend, INDEX_BITS);
      x = g ^ l ^ hpc;
      break;
    }

    case MPP_PATH: {
      // Hash of IP path history: depth p1, shift p2
      x = hash_path(ctx, spec.p1, spec.p2);
      x ^= hpc;
      break;
    }

    case MPP_RECENCY: {
      // Hash of recency stack: depth p1, shift p2
      x = hash_recency(ctx, spec.p1, spec.p2);
      x ^= hpc;
      break;
    }
  }

  return x % spec.table_size;
}

void mpp::compute_output(champsim::address ip,
                         const dynamic_bitset& global_hist,
                         const dynamic_bitset& local_hist,
                         const bp_context& ctx)
{
  last_yout = 0;

  for(int i = 0; i < rt_num_tables; i++) {
    unsigned int h = get_feature_hash(rt_specs[i], ip, global_hist, local_hist, ctx, i);
    last_indices[i] = h;
    int w = tables[i][h].val;
    last_yout += translate(w);
  }
}

void mpp::theta_setting(bool correct, int a) {
  if(!correct) {
    tc++;
    if(tc >= SPEED) {
      theta++;
      tc = 0;
    }
  }
  if(correct && a < theta) {
    tc--;
    if(tc <= -SPEED) {
      theta--;
      tc = 0;
    }
  }
  if(theta < MIN_THETA) theta = MIN_THETA;
  if(theta > MAX_THETA) theta = MAX_THETA;
}

void mpp::train(bool taken) {
  double y = last_yout;
  if(!taken) y = -y;
  bool correct = y >= 1.0;
  int a = static_cast<int>(std::fabs(last_yout * FUDGE));

  bool do_train = !correct || (a <= theta);
  if(!do_train) return;

  theta_setting(correct, a);

  for(int i = 0; i < rt_num_tables; i++) {
    Weight* w = &tables[i][last_indices[i]];
    w->val = static_cast<int8_t>(satincdec(w->val, taken));
  }
}

std::pair<bool,double> mpp::predict_branch(champsim::address ip,
                                            const dynamic_bitset& global_hist,
                                            const dynamic_bitset& local_hist,
                                            const bp_context& ctx)
{
  compute_output(ip, global_hist, local_hist, ctx);

  bool prediction = (last_yout >= 1);
  if(prediction)
    predict_taken++;
  else
    predict_nottaken++;

  if(DEBUG)
    fmt::print("[MPP] PREDICT IP: {} YOUT: {} PRED: {}\n", ip, last_yout, prediction ? "taken" : "not taken");

  return {prediction, static_cast<double>(last_yout)};
}

void mpp::last_branch_result(champsim::address ip,
                              const dynamic_bitset& global_hist,
                              const dynamic_bitset& local_hist,
                              bool taken,
                              bp_context& ctx)
{
  if(taken)
    outcome_taken++;
  else
    outcome_nottaken++;

  // Recompute indices (same as predict) for the training step
  compute_output(ip, global_hist, local_hist, ctx);
  train(taken);

  // Context update (path history, recency stack, etc.) is done by SPPAM via ctx.update()

  if(DEBUG)
    fmt::print("[MPP] OUTCOME IP: {} TAKEN: {} YOUT: {}\n", ip, taken, last_yout);
}

void mpp::print_heartbeat() {
  fmt::print("[MPP] Predicted Taken: {} Predicted Not-taken: {}\n", predict_taken, predict_nottaken);
  fmt::print("[MPP] Outcome Taken: {} Outcome Not-taken: {}\n", outcome_taken, outcome_nottaken);
  fmt::print("[MPP] Theta: {}\n", theta);
  predict_taken = 0;
  predict_nottaken = 0;
  outcome_taken = 0;
  outcome_nottaken = 0;
}

void mpp::print_stats() {
  std::ofstream pht_file;
  pht_file.open("sppam_b_predict_mpp.txt", std::ios::out | std::ios::trunc);
  for(int t = 0; t < rt_num_tables; t++) {
    for(int i = 0; i < rt_specs[t].table_size; i++) {
      if(tables[t][i].val != 0)
        pht_file << fmt::format("TABLE {} IDX {} : {}\n", t, i, tables[t][i].val);
    }
  }
  pht_file << fmt::format("THETA: {}\n", theta);
  pht_file.close();
}

}
