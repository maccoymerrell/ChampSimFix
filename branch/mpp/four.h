/*
 * Daniel A. Jimenez 
 *
 * This file simulates the "multiperspective perceptron predictor" for CBP2016.
 */

#ifndef BRANCH_MPP_FOUR_H
#define BRANCH_MPP_FOUR_H

#include "branch.h"
#include "btb.h"


class four_update : public mpp_branch_update {
public:
	unsigned int pc;
	mpp_branch_update *u1, *u2;
	four_update (void) { }
};

class four : public mpp_branch_predictor_base {
public:
	mpp_branch_predictor_base *p1;
	bool filtered;
	int btb_sets, btb_assoc;
	btb *thebtb;
	bool last_ghist;
	int update_btb_taken;
	int current_state, state_at_prediction;
	long long int btb_misses, btb_accesses;

#define STATE_NEVER_TAKEN	2
#define STATE_ALWAYS_TAKEN	0
#define STATE_DYNAMIC		1

	four_update u;

	~four (void) {
		delete thebtb;
	}
	
	four (mpp_branch_predictor_base *_p1 = NULL, int _btb_sets = 256, int _btb_assoc = 16, int _update_btb_taken = 0, int _btb_repl = BTBREPL_LRU, int _btb_pos = 0) {
		//printf ("four(%d,%d,%d,%d,%d):", _btb_sets, _btb_assoc, _update_btb_taken, _btb_repl, _btb_pos);
		//fflush (stdout);
		update_btb_taken = _update_btb_taken;
		last_ghist = false;
		p1 = _p1;
		assert (p1);
		filtered = false;
		thebtb = new btb (_btb_assoc, _btb_sets, _btb_repl, _btb_pos);
		current_state = STATE_NEVER_TAKEN;
		btb_assoc = _btb_assoc;
		btb_sets = _btb_sets;
		btb_misses = 0;
		btb_accesses = 0;
	}

	mpp_branch_update *lookup (unsigned int pc, bool, bool, bool, btb_entry *) {
		btb_entry *e = thebtb->probe (pc);
		if (!e) {
			current_state = STATE_NEVER_TAKEN;
		} else {
			current_state = e->state;
		}
		unsigned int lpc = pc;
		u.pc = pc;
		u.u1 = p1->lookup (lpc, false, false, false, e);
		filtered = false;
		switch (current_state) {
		case STATE_NEVER_TAKEN:
			u.prediction (false); // only ever seen untaken
			u.u1->prediction (false);
			filtered = true;
			break;
		case STATE_ALWAYS_TAKEN:
			u.prediction (true);
			u.u1->prediction (true);
			filtered = true;
			break;
		case STATE_DYNAMIC:
			u.prediction (u.u1->prediction());
			filtered = false;
			break;
		default: assert (0);
		}
		state_at_prediction = current_state;
		return &u;
	}

	// update the predictor

	void update (mpp_branch_update *p, unsigned int target, bool taken, bool, bool, bool, int type) {
		bool do_train = false;
		btb_entry *e = thebtb->probe (u.pc);
		if (!e) {
			if (taken) {
				e = thebtb->allocate (u.pc, target);
				e->was_miss = true;
				e->init = 0;
				btb_misses++;
				e->state = STATE_ALWAYS_TAKEN;
				current_state = STATE_ALWAYS_TAKEN;
			} else current_state = STATE_NEVER_TAKEN;
		} else {
			e->was_miss = false;
			current_state = e->state;
			if (!update_btb_taken || taken)
				thebtb->update (e);
		}
		if (e) {
			btb_accesses++;
		}

		// move forward the state machine

		switch (current_state) {
		case STATE_NEVER_TAKEN:
			if (taken) {
				current_state = STATE_DYNAMIC;
				do_train = true;
			}
			break;
		case STATE_ALWAYS_TAKEN:
			if (!taken) {
				current_state = STATE_DYNAMIC;
				do_train = true;
			}
			break;
		case STATE_DYNAMIC:
			do_train = true;
			break;
		default: assert (0);
		}
		if (e) {
			assert (current_state != STATE_NEVER_TAKEN);
			e->state = current_state;
		}
		else 
			assert (current_state == STATE_NEVER_TAKEN);
		p1->update (u.u1, target, taken, false, false, false, type, do_train, filtered, e);
		last_ghist = taken;
	}

	// update ghist and path history on branches that aren't conditional

	void nonconditional_branch (unsigned int pc, unsigned int target, int type) {
		p1->nonconditional_branch (pc, target, type);
	}
};

#endif
