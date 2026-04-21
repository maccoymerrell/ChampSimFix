#ifndef BRANCH_MPP_BRANCH_H
#define BRANCH_MPP_BRANCH_H

#include "btb.h"

// this class contains information that is kept between prediction and update
// the only thing it has here is the prediction.  it should be subclassed to hold
// other data e.g. indices in tables, branch PC, etc.

class mpp_branch_update {
	bool _prediction;
public:
	unsigned int address;

	mpp_branch_update (void) { }

	// set the prediction for this branch

	void prediction (bool p) {
		_prediction = p;
	}

	// return the prediction for this branch

	bool prediction (void) {
		return _prediction;
	}
};

// this class represents a branch predictor

class mpp_branch_predictor_base {
public:
	mpp_branch_predictor_base (void) { }

	// the first parameter is the branch address
	// the second parameter is true if the branch was ever taken
	// the third parameter is true if the branch was ever not taken
	// the return value is a pointer to a mpp_branch_update object that gives
	// the prediction and presumably contains information needed to
	// update the predictor

	virtual mpp_branch_update *lookup (unsigned int pc, bool ansf, bool atsf, bool dyn, btb_entry *e) = 0;

	// the first parameter is the branch info returned by the corresponding predict
	// the second parameter is the outcome of the branch

	virtual void update (mpp_branch_update *p, unsigned int target, bool taken, bool ansf, bool atsf, bool dyn, int type = 0) { }
	virtual void update (mpp_branch_update *p, unsigned int target, bool taken, bool ansf, bool atsf, bool dyn, int type, bool do_train, bool filtered, btb_entry *e) { }

	// if this returns false, then we will do lookup even for always/never taken branches

	virtual bool filter_always_never (void) { return true; }

	virtual void nonconditional_branch (unsigned int pc, unsigned int target, int type) { }

	virtual const char *name (bool) { return ""; }

	virtual ~mpp_branch_predictor_base (void) { }
};

#endif
