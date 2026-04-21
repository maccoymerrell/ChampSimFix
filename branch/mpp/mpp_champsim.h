#ifndef BRANCH_MPP_WRAP_H
#define BRANCH_MPP_WRAP_H

#include "modules.h"

struct mpp : champsim::modules::branch_predictor {
  using branch_predictor::branch_predictor;

  void initialize_branch_predictor();
  bool predict_branch(champsim::address ip);
  void last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type);
};

#endif
