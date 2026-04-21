#ifndef BRANCH_TAGE_SC_L_H
#define BRANCH_TAGE_SC_L_H

#include "modules.h"

struct tage_sc_l : champsim::modules::branch_predictor {
  using branch_predictor::branch_predictor;

  void initialize_branch_predictor();
  bool predict_branch(champsim::address ip);
  void last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type);
};

#endif
