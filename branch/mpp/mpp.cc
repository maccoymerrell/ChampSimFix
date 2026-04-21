#include <stdio.h>
#include <string.h>
#include <list>
#include <assert.h>
#include <math.h>
#include <stdlib.h>

using namespace std;

#include "branch.h"
#include "eval.h"
#include "btb.h"
#include "four.h"
#include "mpp.h"

#include "ntables.h"

#ifndef MPP_NTABLES
#define MPP_NTABLES	32
#endif

#define CAT_(A,B) A##B
#define CAT(A,B) CAT_(A,B)
#define SPV		CAT(spv,MPP_NTABLES)

// these specs trained first week of October 2024

history_spec spv6[6] = {
{ LOCAL, 0, 14, 0, 0, 0, 0, 8 },
{ GHISTPATH, 17, 35, 0, 0, 0, 1, 1 },
{ GHISTMODPATH, 0, 7, 5, -1, -1, 0, 14 },
{ PATH, 3, 4, -1, -1, -1, 2, 8 },
{ GHISTPATH, 1, 15, 19, 0, 0, 0, 0 },
{ GHISTPATH, 0, 100, 4, 4, 0, 1, 8 },
};

history_spec spv8[8] = {
{ BACKPATH, 23, 1, 0, 0, 0, 3, 1 },
{ BACKPATH, 4, 5, 0, 0, 0, 0, 0 },
{ GHISTPATH, 0, 15, 6, 0, 0, 0, 0 },
{ BLURRYPATH, 1, 4, 5, -1, -1, 0, 4 },
{ GHISTPATH, 13, 33, 13, 1, 0, 0, 0 },
{ GHISTPATH, 6, 100, 1, 6, 0, 0, 0 },
{ LOCAL, 0, 1, 0, 0, 0, 1, 2 },
{ LOCAL, 0, 15, 0, 0, 0, 0, 0 },
};

history_spec spv12[12] = {
{ BACKPATH, 19, 1, 0, 0, 0, 0, 6 },
{ BACKPATH, 5, 3, 0, 0, 0, 2, 32 },
{ GHISTMODPATH, 1, 5, 4, -1, -1, 0, 17 },
{ GHISTMODPATH, 0, 7, 5, -1, -1, 0, 4 },
{ GHISTPATH, 0, 15, 21, 0, 0, 1, 8 },
{ GHISTPATH, 11, 35, 14, 7, 0, 0, 8 },
{ GHISTPATH, 5, 81, 9, 0, 0, 0, 16 },
{ GHISTPATH, 51, 159, 1, 6, 0, 1, 8 },
{ LOCAL, 0, 1, 0, 0, 0, 1, 2 },
{ LOCAL, 7, 24, 0, 0, 0, 1, 8 },
{ LOCAL, 0, 10, 0, 0, 0, 3, 8 },
{ PATH, 3, 4, -1, -1, -1, 2, 8 },
};

history_spec spv16[16] = {
/*
{ BACKPATH, 27, 1, 0, 0, 0, 1, 1 },
{ BACKPATH, 5, 3, 0, 0, 0, 2, 32 },
{ GHISTMODPATH, 0, 13, 4, -1, -1, 0, 4 },
{ GHISTMODPATH, 0, 6, 1, -1, -1, 0, 4 },
{ GHISTMODPATH, 1, 5, 6, -1, -1, 1, 8 },
{ GHISTPATH, 108, 312, 9, 2, 0, 1, 10 },
{ GHISTPATH, 1, 15, 6, 0, 0, 0, 0 },
{ GHISTPATH, 19, 33, 1, 0, 0, 0, 0 },
{ GHISTPATH, 24, 98, 10, 4, 0, 1, 8 },
{ GHISTPATH, 4, 61, 18, 1, 0, 0, 0 },
{ GHISTPATH, 86, 173, 1, 4, 0, 0, 32 },
{ LOCAL, 0, 10, 0, 0, 0, 0, 8 },
{ LOCAL, 0, 1, 0, 0, 0, 1, 2 },
{ LOCAL, 9, 24, 0, 0, 0, 0, 0 },
{ MODHIST, 1, 18, -1, -1, -1, 2, 5 },
{ PATH, 3, 2, -1, -1, -1, 2, 8 },
*/
{ BACKPATH, 1, 1, 0, 0, 0, 3, 3 },
{ BACKPATH, 18, 1, 0, 0, 0, 0, 6 },
{ BACKPATH, 44, 1, 0, 0, 0, 1, 1 },
{ BACKPATH, 5, 3, 0, 0, 0, 0, 0 },
{ LOCAL, 0, 11, 0, 0, 0, 0, 0 },
{ GHISTPATH, 0, 17, 2, 0, 0, 0, 0 },
{ GHISTPATH, 128, 217, 3, 6, 0, 0, 0 },
{ GHISTPATH, 19, 33, 1, 0, 0, 0, 0 },
{ RECENCY, 12, 2, -1, -1, -1, 0, 0 },
{ GHISTPATH, 4, 61, 2, 1, 0, 0, 0 },
{ LOCAL, 0, 1, 0, 0, 0, 0, 16 },
{ GHISTMODPATH, 1, 5, 6, -1, -1, 0, 0 },
{ LOCAL, 7, 24, 0, 0, 0, 0, 8 },
{ MODHIST, 1, 17, -1, -1, -1, 2, 5 },
{ PATH, 11, 1, -1, -1, -1, 0, 4 },
{ GHISTPATH, 34, 100, 9, 6, 0, 0, 0 },
};

history_spec spv18[18] = {
{ PATH, 11, 1, -1, -1, -1, 0, 4 },
{ PATH, 3, 2, -1, -1, -1, 2, 8 },
{ LOCAL, 0, 2, 0, 0, 0, 0, 8 },
{ BACKPATH, 1, 1, 0, 0, 0, 3, 3 },
{ MODPATH, 1, 6, 1, -1, -1, 2, 16 },
{ BACKPATH, 9, 5, 0, 0, 0, 2, 1 },
{ GHISTPATH, 0, 15, 4, 0, 0, 1, 8 },
{ GHISTPATH, 12, 47, 14, 7, 0, 0, 8 },
{ GHISTPATH, 66, 173, 1, 4, 0, 0, 32 },
{ GHISTPATH, 5, 33, 9, 6, 0, 0, 0 },
{ GHISTPATH, 32, 68, 11, 0, 0, 0, 16 },
{ BACKPATH, 5, 3, 0, 0, 0, 0, 0 },
{ LOCAL, 0, 11, 0, 0, 0, 0, 0 },
{ LOCAL, 9, 24, 0, 0, 0, 0, 16 },
{ GHISTPATH, 6, 100, 1, 6, 0, 0, 0 },
{ RECENCY, 12, 2, -1, -1, -1, 0, 0 },
{ BACKPATH, 24, 1, 0, 0, 0, 0, 6 },
{ GHISTPATH, 185, 275, 1, 2, 0, 1, 10 },
};

history_spec spv20[20] = {
{ MODPATH, 5, 6, 1, -1, -1, 2, 16 },
{ LOCAL, 0, 2, 0, 0, 0, 0, 8 },
{ BLURRYPATH, 10, 7, 2, -1, -1, 2, 11 },
{ RECENCY, 12, 2, -1, -1, -1, 0, 0 },
{ GHISTMODPATH, 1, 5, 6, -1, -1, 0, 0 },
{ PATH, 6, 5, -1, -1, -1, 0, 4 },
{ BACKPATH, 5, 3, 0, 0, 0, 2, 32 },
{ PATH, 3, 2, -1, -1, -1, 2, 8 },
{ GHISTPATH, 26, 61, 2, 0, 0, 0, 8 },
{ GHISTPATH, 6, 101, 1, 6, 0, 0, 0 },
{ GHISTMODPATH, 0, 11, 4, -1, -1, 0, 4 },
{ LOCAL, 0, 10, 0, 0, 0, 0, 8 },
{ GHISTPATH, 13, 33, 45, 1, 0, 0, 0 },
{ GHISTPATH, 187, 316, 9, 2, 0, 1, 10 },
{ LOCAL, 9, 24, 0, 0, 0, 0, 0 },
{ MODHIST, 1, 20, -1, -1, -1, 2, 5 },
{ BACKPATH, 23, 1, 0, 0, 0, 3, 1 },
{ LOCAL, 0, 19, 0, 0, 0, 1, 8 },
{ GHISTPATH, 0, 17, 48, 0, 0, 0, 0 },
{ GHISTPATH, 97, 174, 8, 4, 0, 0, 32 },
};

history_spec spv24[24] = {
{ BACKPATH, 18, 2, 0, 0, 0, 0, 6 },
{ MODPATH, 5, 5, 1, -1, -1, 2, 16 },
{ BACKPATH, 5, 3, 0, 0, 0, 2, 32 },
{ BACKPATH, 8, 5, 0, 0, 0, 2, 1 },
{ GHISTMODPATH, 0, 13, 4, -1, -1, 0, 4 },
{ GHISTMODPATH, 0, 7, 4, -1, -1, 0, 4 },
{ PATH, 3, 2, -1, -1, -1, 2, 8 },
{ GHISTPATH, 0, 15, 5, 0, 0, 1, 8 },
{ GHISTPATH, 10, 34, 5, 7, 0, 0, 8 },
{ LOCAL, 0, 2, 0, 0, 0, 0, 8 },
{ BACKPATH, 45, 1, 0, 0, 0, 1, 1 },
{ GHISTMODPATH, 1, 5, 2, -1, -1, 1, 8 },
{ RECENCY, 13, 4, -1, -1, -1, 2, 8 },
{ PATH, 8, 3, -1, -1, -1, 0, 4 },
{ LOCAL, 0, 10, 0, 0, 0, 0, 8 },
{ LOCAL, 0, 1, 0, 0, 0, 1, 2 },
{ LOCAL, 0, 18, 0, 0, 0, 1, 16 },
{ GHISTPATH, 185, 275, 1, 2, 0, 1, 10 },
{ LOCAL, 9, 24, 0, 0, 0, 0, 16 },
{ MODHIST, 1, 18, -1, -1, -1, 2, 5 },
{ GHISTPATH, 37, 126, 1, 6, 0, 1, 8 },
{ GHISTPATH, 28, 64, 11, 0, 0, 0, 16 },
{ GHISTPATH, 10, 98, 4, 4, 0, 1, 8 },
{ GHISTPATH, 99, 175, 8, 4, 0, 0, 32 },
};

history_spec spv32[32] = {
{ BACKPATH, 18, 2, 0, 0, 0, 0, 6 },
{ BACKPATH, 28, 1, 0, 0, 0, 3, 1 },
{ BACKPATH, 4, 5, 0, 0, 0, 0, 0 },
{ BACKPATH, 45, 1, 0, 0, 0, 1, 1 },
{ BACKPATH, 8, 5, 0, 0, 0, 2, 1 },
{ BLURRYPATH, 1, 2, -1, -1, -1, 2, 18 },
{ BLURRYPATH, 9, 9, 2, -1, -1, 0, 32 },
{ GHISTMODPATH, 0, 15, 3, -1, -1, 0, 4 },
{ GHISTMODPATH, 0, 7, 4, -1, -1, 0, 4 },
{ GHISTMODPATH, 1, 5, 6, -1, -1, 1, 8 },
{ GHISTMODPATH, 3, 9, 6, -1, -1, 0, 17 },
{ GHISTPATH, 0, 15, 2, 0, 0, 1, 8 },
{ GHISTPATH, 1, 20, 2, 0, 0, 0, 0 },
{ GHISTPATH, 128, 217, 17, 6, 0, 0, 0 },
{ GHISTPATH, 185, 307, 0, 2, 0, 1, 10 },
{ GHISTPATH, 19, 33, 1, 0, 0, 0, 0 },
{ GHISTPATH, 28, 64, 11, 0, 0, 0, 16 },
{ GHISTPATH, 34, 100, 9, 6, 0, 0, 0 },
{ GHISTPATH, 41, 126, 16, 6, 0, 1, 8 },
{ GHISTPATH, 7, 44, 2, 1, 0, 0, 0 },
{ GHISTPATH, 97, 158, 8, 4, 0, 0, 32 },
{ LOCAL, 0, 1, 0, 0, 0, 0, 0 },
{ LOCAL, 0, 12, 0, 0, 0, 0, 0 },
{ LOCAL, 0, 18, 0, 0, 0, 1, 16 },
{ LOCAL, 0, 6, 0, 0, 0, 0, 8 },
{ LOCAL, 9, 24, 0, 0, 0, 0, 16 },
{ MODHIST, 1, 21, -1, -1, -1, 2, 5 },
{ MODPATH, 5, 6, 1, -1, -1, 2, 16 },
{ PATH, 10, 3, -1, -1, -1, 0, 4 },
{ PATH, 3, 2, -1, -1, -1, 2, 8 },
{ PATH, 6, 5, -1, -1, -1, 0, 4 },
{ RECENCY, 13, 4, -1, -1, -1, 2, 8 },
};

history_spec spv40[40] = {
{ GHISTPATH, 0, 15, 12, 0, 0, 1, 8 },
{ BACKPATH, 2, 5, 0, 0, 0, 0, 0 },
{ PATH, 3, 6, -1, -1, -1, 2, 8 },
{ LOCAL, 0, 3, 0, 0, 0, 0, 8 },
{ BACKPATH, 5, 3, 0, 0, 0, 0, 0 },
{ GHISTPATH, 17, 26, 2, 0, 0, 0, 8 },
{ BLURRYPATH, 9, 9, 2, -1, -1, 0, 32 },
{ GHISTMODPATH, 0, 13, 4, -1, -1, 0, 4 },
{ GHISTMODPATH, 0, 7, 4, -1, -1, 0, 4 },
{ GHISTMODPATH, 1, 5, 6, -1, -1, 0, 0 },
{ GHISTMODPATH, 3, 18, 2, -1, -1, 0, 17 },
{ PATH, 6, 1, -1, -1, -1, 0, 4 },
{ GHISTPATH, 0, 19, 5, 0, 0, 0, 0 },
{ GHISTPATH, 11, 32, 14, 7, 0, 0, 8 },
{ LOCAL, 0, 1, 0, 0, 0, 0, 0 },
{ LOCAL, 0, 8, 0, 0, 0, 1, 1 },
{ BACKPATH, 8, 5, 0, 0, 0, 2, 1 },
{ GHISTPATH, 2, 11, 4, 0, 0, 0, 0 },
{ GHISTPATH, 24, 100, 0, 4, 0, 1, 8 },
{ GHISTPATH, 25, 39, 8, 7, 0, 0, 8 },
{ GHISTPATH, 37, 94, 15, 6, 0, 1, 8 },
{ GHISTPATH, 46, 124, 13, 6, 0, 1, 8 },
{ GHISTPATH, 43, 61, 3, 0, 0, 0, 0 },
{ GHISTPATH, 4, 57, 6, 1, 0, 0, 0 },
{ GHISTPATH, 66, 173, 1, 4, 0, 0, 32 },
{ GHISTPATH, 117, 337, 1, 2, 0, 1, 10 },
{ LOCAL, 0, 10, 0, 0, 0, 0, 8 },
{ LOCAL, 0, 1, 0, 0, 0, 1, 2 },
{ LOCAL, 0, 14, 0, 0, 0, 0, 16 },
{ LOCAL, 0, 21, 0, 0, 0, 2, 8 },
{ BACKPATH, 44, 1, 0, 0, 0, 1, 1 },
{ GHISTPATH, 128, 233, 3, 6, 0, 0, 0 },
{ LOCAL, 13, 24, 0, 0, 0, 0, 16 },
{ LOCAL, 7, 24, 0, 0, 0, 0, 0 },
{ MODHIST, 1, 22, -1, -1, -1, 2, 5 },
{ MODPATH, 5, 6, 1, -1, -1, 2, 16 },
{ PATH, 11, 5, -1, -1, -1, 0, 4 },
{ BACKPATH, 33, 1, 0, 0, 0, 1, 1 },
{ BACKPATH, 18, 1, 0, 0, 0, 3, 23 },
{ RECENCY, 15, 4, -1, -1, -1, 2, 8 },
};

mpp_branch_predictor_base *make_bp (void) {
	history_spec **sspv;
	sspv = new history_spec*[1];
	sspv[0] = SPV;
	// with 16K entry BTB
	return new four (new mpp_impl (sspv, 0, MPP_NTABLES), 512, 32, 1, BTBREPL_LRU, 5);
}

#ifndef NOT_CHAMPSIM
#include "mpp_champsim.h"
#include "instruction.h"

namespace {
	mpp_branch_predictor_base *p;
	mpp_branch_update *u;
}

void mpp::initialize_branch_predictor() {
	p = make_bp ();
	u = nullptr;
}

bool mpp::predict_branch(champsim::address ip) {
	u = p->lookup (ip.to<unsigned int>(), false, false, false, NULL);
	return u->prediction();
}

void mpp::last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type) {
	unsigned int ip_val = ip.to<unsigned int>();
	unsigned int target_val = branch_target.to<unsigned int>();
	if (branch_type == BRANCH_CONDITIONAL) {
		assert (u);
		p->update (u, target_val, taken, false, false, false, OPTYPE_JMP_DIRECT_COND);
	} else {
		int type = -1;
		switch (branch_type) {
		case BRANCH_DIRECT_JUMP: type = OPTYPE_JMP_DIRECT_UNCOND; break;
		case BRANCH_INDIRECT: type = OPTYPE_JMP_INDIRECT_UNCOND; break;
		case BRANCH_DIRECT_CALL: type = OPTYPE_CALL_DIRECT_UNCOND; break;
		case BRANCH_INDIRECT_CALL: type = OPTYPE_CALL_INDIRECT_UNCOND; break;
		case BRANCH_RETURN: type = OPTYPE_RET_UNCOND; break;
		case BRANCH_OTHER: type = OPTYPE_JMP_DIRECT_UNCOND; break;
		default: break;
		}
		if (type >= 0) {
			p->nonconditional_branch (ip_val, target_val, type);
		}
		u = nullptr;
	}
}
#endif
