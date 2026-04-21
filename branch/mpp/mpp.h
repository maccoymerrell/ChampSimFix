/*
 * This file simulates the "multiperspective perceptron predictor"
 */

#ifndef BRANCH_MPP_MPP_H
#define BRANCH_MPP_MPP_H

#include <list>
#include <cstring>
#include <cassert>
#include <cmath>

#include "branch.h"
#include "eval.h"
#include "btb.h"

#define VERBOSE

#ifndef NSPECS
#define NSPECS	1
#endif

#define UINT64 unsigned long long int
#define UINT32 unsigned int

#include <ctype.h>

// translate table for the transfer function. maps a 6-bit signed integer to a logit function

// static int default_xlat[] = { -288,-279,-270,-261,-252,-243,-234,-225,-216,-207,-198,-189,-180,-171,-162,-153,-144,-135,-126,-117,-108,-99,-90,-81,-72,-63,-54,-45,-36,-27,-18,-9,0,9,18,27,36,45,54,63,72,81,90,99,108,117,126,135,144,153,162,171,180,189,198,207,216,225,234,243,252,261,270,279 }; // linear 
static int default_xlat[] = {-252,-244,-236,-228,-220,-212,-204,-196,-188,-180,-172,-164,-156,-148,-140,-132,-124,-116,-108,-100,-92,-84,-76,-68,-60,-52,-44,-36,-28,-20,-12,-4,4,12,20,28,36,44,52,60,68,76,84,92,100,108,116,124,132,140,148,156,164,172,180,188,196,204,212,220,228,236,244,252,}; // from a good tuning

struct Weight {
	char val;
};

class mpp_update : public mpp_branch_update {
public:
	unsigned int pc, target;
	unsigned short int hpc, pc2;
	int yout;
	int indices[MAX_TABLES];
	bool filtered;
	bool backward;
	Weight *w;
	bool taken;
	btb_entry *e;
	mpp_update (void) { }
};

// feature types
enum history_type {
        ACYCLIC = 1,            // "acylic history" - put the most recent history of pc into array[pc%parameter] and use this array as a feature
        MODHIST = 2,            // "modulo history" - shift history bit in if PC%modulus==0
        BIAS = 3,               // bias of this branch
        RECENCY = 4,            // hash of a recency stack of PCs
        IMLI = 5,               // inner most loop iteration counter(s)
        PATH = 6,               // path history
        LOCAL = 7,              // local history (pshare)
        MODPATH = 8,            // like modhist but with path history
        GHISTPATH = 9,         // (path history << 1) | global history
        GHISTMODPATH = 10,	// (mod path history << 1) | global history
        BLURRYPATH = 11,        // "page" history sort of
        RECENCYPOS = 12,        // position of this PC in the recency stack
	BACKPATH = 13,
	BACKGHISTPATH = 14,
	RHIST = 15,
	TAKENPATH = 16,
        MAXTYPE = 17
};

#ifndef _MAIN_CC
inline const char *type_names[] = {
"nothing", "ACYCLIC", "MODHIST", "BIAS", "RECENCY", "IMLI", "PATH", "LOCAL", "MODPATH", "GHISTPATH", "GHISTMODPATH", "BLURRYPATH", "RECENCYPOS", "BACKPATH", "BACKGHISTPATH", "RHIST", "TAKENPATH",
};
#else
extern const char *type_names[];
#endif

// this struct represents one feature that is an input to the hashed
// perceptron predictor

#define XOR_IMLI1	1
#define XOR_IMLI4	4
#define XOR_RPOS	2
#define XOR_HASH1	8
#define XOR_HASH2	16
#define XOR_HASH3	32

// this struct represents one feature that is an input to the hashed
// perceptron predictor

struct history_spec {
	// the type of the feature

	enum history_type type;

	// up to three parameters for this feature

	int p1, p2, p3, p4, p5;

	// the size of the array of weights for this feature
	int 
		size;

	// whether to hash imli1, imli4, or rpos with this feature, or use one of 3 different hash functions

	unsigned int
		xorflags;
};

class mpp_impl : public mpp_branch_predictor_base {
public:

	// some values carried from prediction to update. there's no good
	// reason that some stuff is put in here and other stuff isn't. it
	// just happened that way as a historical artifact of supporting
	// speculative update for previous predictors.

	mpp_update u;

	// for simplicity we keep stuff in large arrays and then only use
	// the part of them needed for the predictor. these defines give the
	// maximum sizes of the arrays. they are very large to allow for flexible
	// search of the design space, but the actual tuned values are represented
	// in variables passed as parameters to the constructor and fit within the
	// given hardware budgets

#define MAX_PATHHIST	4096
#define MAX_GHIST	4096
#define MAX_LG_TABLE_SIZE	13 // was 12
#define MAX_TABLE_SIZE	(1<<MAX_LG_TABLE_SIZE)
#define MAX_FILTER	65536
#define MAX_LOCAL_HISTORIES	4096
#define MAX_ACYCLIC	20
#define MAX_MOD		10
#define MAX_BLURRY	16
#define MAX_BLURRY2	16
#define MAX_ASSOC	256

	// STATE THAT COUNTS AGAINST THE HARDWARE BUDGET. these variables
	// are the part of the predictor that contain mutable state that
	// persists from one prediction to the next. the parts of these variables
	// that are actually used by the predictor count against the hardware budget.

	//
	unsigned int allocations, initial_random;

	// table of per-branch (local) histories

	unsigned int local_histories[MAX_LOCAL_HISTORIES];

	// counter for adaptive theta setting

	int min_theta, max_theta, original_theta;
	int tc;

	// tables of weight magnitudes

	Weight tables[MAX_TABLES][MAX_TABLE_SIZE];

	// global history bits divided into block_size bits per array element

	unsigned int back_path[MAX_PATHHIST];

	// count of mispredictions per table

	// acyclic histories
	
	bool acyclic_histories[MAX_ACYCLIC][32];

	// alternate acyclic histories that use path instead of pattern history

	unsigned int acyclic2_histories[MAX_ACYCLIC][32];

	int bitwidth;

	// modulo pattern history
	bool mod_histories[MAX_MOD][MAX_GHIST];

	// modulo path history

	unsigned short int modpath_histories[MAX_MOD][MAX_PATHHIST];

	// "page" history

	unsigned int blurrypath_histories[MAX_BLURRY][MAX_BLURRY2];

	// recency stack of recenctly visited PCs (hashed)

	unsigned short int recency_stack[MAX_ASSOC];

	// history of recent PCs (hashed)

	unsigned short int path_history[MAX_PATHHIST];

	// innermost loop iteration counters; 4 versions

	unsigned int imli_counter1, imli_counter2, imli_counter3, imli_counter4;

	mpp_branch_update *utage;

	// RUN-TIME CONSTANTS THAT DO NOT COUNT AGAINST HARDWARE BUDGET

	mpp_branch_predictor_base *tage;

	int

		// many different moduli could be used for building
		// modulo history. these arrays keep track of which moduli
		// are actually used in the features for this predictor
		// so only the relevant tables are updated for performance
		// reasons. in a real implementation, only those tables would
		// be implemented.

		modhist_indices[MAX_MOD],
		modpath_indices[MAX_MOD],	
		modpath_lengths[MAX_MOD], 
		modhist_lengths[MAX_MOD];

	int 
		// maximum global history required by any feature

		ghist_length, 

		// maximum modulo history required by any feature

		modghist_length, 

		// maximum path length required by any feature

		path_length,

		// total bits used by the predictor (for sanity check)

		totalbits,

		// associativity of the recency stack, derived from the
		// maximum depth any RECENCY feature hashes

		assoc;

	// number of modulo histories and modulo path histories

		int nmodhist_histories, nmodpath_histories;


		long yout_count[100], yout_ntimes[100];

#define MAX_THREADS	16

		int max_threads;

		UINT64 global_hist[MAX_THREADS][1000];
		UINT64 backglobal_hist[1000];
		UINT64 taken_hist[1000];

	void update_hist (UINT64 *hist, bool taken) {
		for (int i=(ghist_length/64)+1; i > 0; i--) {
			hist[i] = (hist[i] << 1) | (hist[i-1] >> 63);
		}
		hist[0] = (hist[0] << 1);
		hist[0] |= taken;
		return;
	}

	void update_wide_hist (UINT64 *hist, unsigned int x) {
		for (int i=(ghist_length/64)+1; i > 0; i--) {
			hist[i] = (hist[i] << 1) | (hist[i-1] >> 63);
		}
		hist[0] = (hist[0] << 1);
		hist[0] ^= !(x & 0x8);
		return;
	}

void update_global_hist (bool taken) { update_hist (global_hist[current_thread%max_threads], taken); }
void update_backglobal_hist (bool taken) { update_hist (backglobal_hist, taken); }
void update_taken_hist (unsigned int x) { update_wide_hist (taken_hist, x); }

	UINT64 idx (UINT64 *v, int a, int b) {
		int i = a / 64;
	
		if (i != (b-1)/64) {
			int c0 = (a | 63) + 1;
			UINT64 w1 = idx (v, a, c0);
			UINT64 w2 = idx (v, c0, b);
			return (w2 << (c0-a)) | w1;
		} else {
			UINT64 w = v[i];
			int bits = b - a;
			int s = a & 63;
			UINT64 mask = (1<<bits)-1;
			w = (w >> s) & mask;
			return w;
		}
	}
	
	unsigned int xor_ghr (UINT64 *hist, int start, int end, int bits) {
		if (start > end) return 0;
		if (start < 0) return 0;
		if (end < 0) return 0;
		int a = start;
		int b = end + 1;
		UINT64 x = 0;
		if (b-a < bits) return idx (hist, a, b);
		int j;
		int j2 = b - bits;
		for (j=a; j<j2; j+=bits) x ^= idx (hist, j, j+bits);
		if (j < b) x ^= idx (hist, j, b);
		return x;
	}

	int current_thread;

	void ending (int x) { }

	// insert an item into an array without duplication. used for
	// building the _indices arrays

	int insert (int *v, int *n, int x) {
		for (int i=0; i<*n; i++) if (v[i] == x) return i;
		v[(*n)] = x;
		return (*n)++;
	}

	// analyze the specification of the features to see what the
	// various maximum history lengths are, how much space is taken by
	// various history structures, and how much space is left over in
	// the hardware budget for tables

	void analyze_spec (void) {

		history_spec specers[MAX_TABLES*NSPECS];
		int nspecers = 0;
		for (int i=0; i<num_features; i++) for (int j=0; j<NSPECS; j++) {
			specers[nspecers++] = specv[j][i];
		}

		bool 
			// true if at least one feature requires the recency stack

			doing_recency = false, 

			// true if at least one feature uses local history

			doing_local = false;

	
		int 
			// how many bits are allocated to the IMLI counters

			imli_counter_bits[4];

		// initially assume a recency stack of depth 0

		assoc = 32; // for hash thingie

		// accounting for bits for the blurry path history

		int blurrypath_bits[MAX_BLURRY][MAX_BLURRY2];

		// accouting for the bits for the acyclic path history

		bool acyclic_bits[MAX_ACYCLIC][32][2];

		// set these accounting arrays to 0 initially

		memset (blurrypath_bits, 0, sizeof (blurrypath_bits));
		memset (imli_counter_bits, 0, sizeof (imli_counter_bits));
		memset (acyclic_bits, 0, sizeof (acyclic_bits));

		// initially assume very short histories, later find out
		// what the maximum values are from the features

		ghist_length = 1;
		modghist_length = 1;
		nmodhist_histories = 0;
		path_length = 1;

		// go through each feature in the specification finding the requirements

		for (int i=0; i<nspecers; i++) {
			// find the maximum associativity of the recency stack required

			if (specers[i].type == RECENCY || specers[i].type == RECENCYPOS) {
				if (assoc < specers[i].p1) assoc = specers[i].p1;
			}

			// find out how much and what kind of history is needed for acyclic feature

			if (specers[i].type == ACYCLIC) {
				for (int j=0; j<specers[i].p1+2; j++) {
					acyclic_bits[specers[i].p1][j][!specers[i].p3] = true;
				}
			}

			// do we need local history?

			if (specers[i].type == LOCAL) doing_local = true;

			// how many IMLI counter bits do we need for different versions of IMLI

			if (specers[i].type == IMLI) imli_counter_bits[specers[i].p1-1] = 32;

			// do we require a recency stack?

			if (specers[i].type == RECENCY || specers[i].type == RECENCYPOS) doing_recency = true;

			// count blurry path bits (assuming full 32-bit addresses less shifted bits)

			if (specers[i].type == BLURRYPATH) for (int j=0; j<specers[i].p2; j++) blurrypath_bits[specers[i].p1][j] = 32 - specers[i].p1;

			// if we are doing modulo history, figure out which and how much history we need

			if (specers[i].type == GHISTPATH || specers[i].type == TAKENPATH || specers[i].type == BACKGHISTPATH) {
				if (ghist_length < specers[i].p2) ghist_length = specers[i].p2 + 1;
			}
			if (specers[i].type == MODHIST || specers[i].type == GHISTMODPATH) {
				int j = insert (modhist_indices, &nmodhist_histories, specers[i].p1);
				if (modhist_lengths[j] < specers[i].p2+1) modhist_lengths[j] = specers[i].p2 + 1;
				if (specers[i].p2 >= modghist_length) modghist_length = specers[i].p2+1;
			}
		}

		// figure out how much history we need for modulo path, modulo+global path, and regular path

		nmodpath_histories = 0;
		for (int i=0; i<nspecers; i++) {
			if (specers[i].type == MODPATH || specers[i].type == GHISTMODPATH) {
				int j = insert (modpath_indices, &nmodpath_histories, specers[i].p1);
				if (modpath_lengths[j] < specers[i].p2+1) modpath_lengths[j] = specers[i].p2 + 1;
				if (path_length <= specers[i].p2) path_length = specers[i].p2 + 1;
			}
		}
		local_history_length = locallimit; // was 0
		// how much global history and global path history do we need

		for (int i=0; i<nspecers; i++) {
			switch (specers[i].type) {
			case BACKGHISTPATH:
			case LOCAL:
			if (local_history_length < specers[i].p2) local_history_length = specers[i].p2;
			break;
			default: ;
			}
		}

		// sanity check

		assert (ghist_length <= MAX_GHIST);
		assert (modghist_length <= MAX_GHIST);

		// account for IMLI counters

		// account for global path bits (represented as an array of 16 bit integers)

		totalbits += path_length * 16;
		
		// account for modulo history bits

		for (int i=0; i<nmodhist_histories; i++) totalbits += modhist_lengths[i];

		// account for modulo path bits

		for (int i=0; i<nmodpath_histories; i++) totalbits += 16 * modpath_lengths[i];

		// account for local histories

		if (doing_local) totalbits += local_history_length * nlocal_histories;

		// account for recency stack

		if (doing_recency) totalbits += assoc * 16;

		// account for blurry path bits

		for (int i=0; i<MAX_BLURRY; i++) for (int j=0; j<MAX_BLURRY2; j++) totalbits += blurrypath_bits[i][j];

		// account for acyclic bits

		for (int i=0; i<MAX_ACYCLIC; i++) for (int j=0; j<32; j++) for (int k=0; k<2; k++) totalbits += acyclic_bits[i][j][k];

		// how many bits are left for the tables?

		int remaining = budgetbits - totalbits;

		// whatever is left, we divide among the rest of the tables

		//int mysize = remaining / (bitwidth * num_tables); 
		// for now we are fixing the number of table entries to 128K, which is 96KB with 6-bit weights
		int mysize = 131072 / num_tables;
		static bool printed = false;
#define NENTRIESTOTAL  ((96 * 1024 * 8) / 6)
		for (int i=6; i<20; i++) {
			int ts1 = 1<<i;
			int ts2 = 1<<(i+1);
			for (int i=0; i<num_tables; i++) {
				for (int j=0; j<i; j++) {
					table_sizes[j] = ts1;
				}
				for (int j=i; j<num_tables; j++) {
					table_sizes[j] = ts2;
				}
				int sum = 0;
				for (int j=0; j<num_tables; j++) sum += table_sizes[j];
				if (sum == NENTRIESTOTAL) {
					goto done;
				}
			}
		}
		done: ;
		if (!printed) {
			printf ("table sizes\n");
			for (int i=0; i<num_tables; i++) printf ("%d %d\n", i, table_sizes[i]);
			fflush (stdout);
			printed = true;
		}
	}

	history_spec specs[MAX_TABLES], **specv;

	// these variables set from parameters

	int 
		spec_index, 		// which spec should we use?
		num_features, 		// number of features (each one gets its own table)
		num_tables,		// real number of tables that the features are folded into
		budgetbits, 		// hardware budget in bits
		nlocal_histories, 	// number of local histories
		local_history_length; 	// local history length

	double
		theta;			// initial threshold for adaptive theta adjusting

	double 
		fudge;			// fudge factor to multiply by perceptron output

	int 
		*xlat,			// transfer function for 6-bit weights (5-bit magnitudes)
		pcbit,			// bit from the PC to use for hashing global history
		htbit,
		pcshift,		// shift for hashing PC
		block_size;		// number of ghist bits in a "block"; this is the width of an initial hash of ghist

	bool 
		hash_taken;		// should we hash the taken/not taken value with a PC bit?

	int 
		table_sizes[MAX_TABLES];// size of each table

	unsigned int 
		record_mask;		// which histories are updated with filtered branch outcomes

	int 
		speed;			// speed for adaptive theta training

	int
		hshift;			// how much to shift initial feauture hash before XORing with PC bits

	bool do_hash;
	int xflag, xn;
	int backward_bias, forward_bias;
	int locallimit;
	std::list<unsigned int> ras;
	bool reinit;

	void beginning (void) {
		reinit = true;
		memset (global_hist, 0, sizeof (global_hist));
		memset (backglobal_hist, 0, sizeof (backglobal_hist));
		memset (taken_hist, 0, sizeof (taken_hist));

		// initialize various structures to 0

		imli_counter1 = 0;
		imli_counter2 = 0;
		imli_counter3 = 0;
		imli_counter4 = 0;

		// set more structures to 0

		memset (back_path, 0, sizeof (back_path));
		memset (tables, 0, sizeof (tables));
		memset (acyclic_histories, 0, sizeof (acyclic_histories));
		memset (acyclic2_histories, 0, sizeof (acyclic2_histories));
		memset (local_histories, 0, sizeof (local_histories));
		memset (mod_histories, 0, sizeof (mod_histories));
		memset (modpath_histories, 0, sizeof (modpath_histories));
		memset (recency_stack, 0, sizeof (recency_stack));
		memset (path_history, 0, sizeof (path_history));
		memset (blurrypath_histories, 0, sizeof (blurrypath_histories));

		// threshold counter for adaptive theta training

		tc = 0;

		// initialize tables and signs

		for (int i=0; i<num_tables; i++) for (int j=0; j<table_sizes[i]; j++) {
			tables[i][j].val = 0;
		}
		theta = original_theta;
	}

	// constructor; parameters described above 

	unsigned long int bin2int (const char *s) {
		unsigned long int x = 0;
		for (int i=0; i<64; i++) {
			unsigned long int bit = s[i] - '0';
			x |= bit << i;
		}
		return x;
	}

	mpp_impl (
		history_spec **_specv = NULL, 
		int _spec_index = 0,
		int _num_features = 38, 
		int _budgetbits = 65536 * 8 + 2048, 
		int _nlocal_histories = 512, 
		int _local_history_length = 11, 
		int _theta = 11, 
		double _fudge = 0.258, 
		int *_xlat = default_xlat, 
		int _pcbit = 3, 
		int _htbit = 2,
		int _pcshift = -10, 
		int _block_size = 21, 
		bool _hash_taken = true,
		unsigned int _record_mask= 191, 
		int _speed = 21, 
		int _hshift = -9, 
		bool _do_hash = true, 
		unsigned int _xflag = 208, 
		unsigned int _xn = 3,
#if 0
		int _backward_bias = -6,
		int _forward_bias = -38) :
#else
		int _backward_bias = 0,
		int _forward_bias = 0) :
#endif
			specv(_specv), spec_index(_spec_index), num_features(_num_features), budgetbits(_budgetbits),
			nlocal_histories(_nlocal_histories), local_history_length(_local_history_length),
			fudge(_fudge), xlat(_xlat),
			pcbit(_pcbit), htbit(_htbit), pcshift(_pcshift), block_size(_block_size), 
			hash_taken(_hash_taken),
			record_mask(_record_mask), speed(_speed), hshift(_hshift), 
			do_hash (_do_hash), xflag(_xflag), xn(_xn), 
			backward_bias(_backward_bias), 
			forward_bias(_forward_bias) {
#if 0
		printf ("mpp(%d,%d,%d,%d,%d,%d,%f,%p,%d,%d,%d,%d,%d,%u,%d,%d,%d,%u,%u,%d,%d):\n",
		_spec_index,
		_num_features, 
		_budgetbits, 
		_nlocal_histories, 
		_local_history_length,
		_theta,
		_fudge,
		_xlat,
		_pcbit,
		_htbit,
		_pcshift,
		_block_size,
		_hash_taken,
		_record_mask,
		_speed, 
		_hshift, 
		_do_hash, 
		_xflag, 
		_xn,
		_backward_bias,
		_forward_bias);
#endif
		num_tables = num_features;
		assert (specv);
#if 0
		for (int i=0; i<num_features; i++) {
			printf ("{ %s, %d, %d, %d, %d, %d, %d, %d },\n", type_names[specv[0][i].type], specv[0][i].p1, specv[0][i].p2, specv[0][i].p3, specv[0][i].p4, specv[0][i].p5, specv[0][i].size, specv[0][i].xorflags);
		}
#endif
		current_thread = 0;
		max_threads = 1;
		bitwidth = 6;
		min_theta = 10;
		max_theta = 255;
		if (_theta < min_theta) _theta = min_theta;
		original_theta = _theta;
		locallimit = 28;
		local_history_length = 0; // the parameter is deprecated! we'll set it from the features.

		// copy sizes of tables from feature specifications into a convenient array

		for (int i=0; i<num_features; i++) specs[i] = specv[0][i];
		for (int i=0; i<num_tables; i++) table_sizes[i] = 0; // no predefined size!

		// no bits used so far

		totalbits = 0;

		memset (modpath_lengths, 0, sizeof (modpath_lengths)); 
		memset (modhist_lengths, 0, sizeof (modhist_lengths)); 

		// analyze the specification to figure out how many bits are allocated to what

		analyze_spec ();

		// initialize stuff

		beginning ();

		memset (yout_count, 0, sizeof (yout_count));
		memset (yout_ntimes, 0, sizeof (yout_ntimes));
	}

	// insert a (shifted) PC into the recency stack with LRU replacement

	void insert_recency (unsigned int pc) {
		int i = 0;
		for (i=0; i<assoc; i++) {
			if (recency_stack[i] == pc) break;
		}
		if (i == assoc) {
			i = assoc-1;
			recency_stack[i] = pc;
		}
		int j;
		unsigned int b = recency_stack[i];
		for (j=i; j>=1; j--) recency_stack[j] = recency_stack[j-1];
		recency_stack[0] = b;
	}

	// hash a PC

	unsigned int hash_pc (unsigned int pc) {
		if (pcshift < 0) {
			return hash (pc, -pcshift);
		} else if (pcshift < 11) {
			unsigned int x = pc;
			x ^= (pc >> pcshift);
			return x;
		} else {
			return pc >> (pcshift-11);
		}
	}

	unsigned int hash_ras (int p1, int p2, int p3, int p4) {
		// p1: depth to go
		// p2: shift each pc by this much
		// p3: width of wha we shift into the result
		// p4: how much we shift the result
		unsigned int h = 0;
		unsigned int lim;
		lim = p1;
		if (ras.size() < lim) lim = ras.size();
		unsigned int i = 0;
		for (auto p=ras.begin(); i<lim; p++,i++) {
			unsigned int r = *p >> p2;
			r &= ((1<<p3)-1);
			h <<= p4;
			h ^= r;
		}
		return h;
	}

	// hash path history

	unsigned int hash_path (int depth, int shift) {
		unsigned int x = 0;
		for (int i=0; i<depth; i++) {
			x <<= shift;
			x += path_history[i];
		}
		return x;
	}

	unsigned int popcount (unsigned long long int x, int n) {
		int c = 0;
		for (int i=0; i<n; i++) if (x & (1<<i)) c++;
		return c;
	}

	// hash global history from position a to position b

	unsigned int hash_ghist (int a, int b) {
		return xor_ghr (global_hist[current_thread%max_threads], a, b, block_size);
	}

	unsigned int hash_taken_hist (int a, int b) {
		return xor_ghr (taken_hist, a, b, block_size);
	}

	unsigned int hash_backghist (int a, int b) {
		return xor_ghr (backglobal_hist, a, b, block_size);
	}

	unsigned int hash_ghistpath (int a, int b, int c, int d) {
		unsigned int x = hash_path (c, (d == -1) ? 3 : d);
		unsigned int y = hash_ghist (a, b);
		return x + y;
	}

	unsigned int hash_backpath (int depth, int shift) {
		unsigned int x = 0;
		for (int i=0; i<depth; i++) {
			x <<= shift;
			x += back_path[i];
		}
		return x;
	}

	unsigned int hash_backghistpath (int a, int b, int c, int d) {
		unsigned int x = hash_backpath (c, (d == -1) ? 3 : d);
		unsigned int y = hash_backghist (a, b);
		return x + y;
	}

	// hash the items in the recency stack to a given depth, shifting
	// by a certain amount each iteration, with two different ways of
	// mixing the bits

	unsigned int hash_recency (int depth, int shift, int style) {
		if (style == -1) {
			unsigned int x = 0;
			for (int i=0; i<depth; i++) {
				x <<= shift;
				x += recency_stack[i];
			}
			return x;
		} else {
			unsigned int x = 0, k = 0;
			for (int i=0; i<depth; i++) {
				x ^= (!!(recency_stack[i] & (1<<shift))) << k;
				k++;
				k %= block_size;
			}
			return x;
		}
	}

	// hash the "blurry" path history of a given scale to a given
	// depth and given shifting parameter

	unsigned int hash_blurry (int scale, int depth, int shiftdelta) {
		if (shiftdelta == -1) shiftdelta = 0;
		int sdint = shiftdelta >> 2;
		int sdfrac = shiftdelta & 3;
		unsigned int x = 0;
		int shift = 0;
		int count = 0;
		for (int i=0; i<depth; i++) {
			x += blurrypath_histories[scale][i] >> shift;
			count++;
			if (count == sdfrac) {
				shift += sdint;
				count = 0;
			}
		}
		return x;
	}

	// hash acyclic histories with a given modulus, shift, and style

	unsigned int hash_acyclic (int a, int shift, int style) {
		unsigned int x = 0;
		if (style == -1) {
			unsigned int k = 0;
			for (int i=0; i<a+2; i++) {
				x ^= acyclic_histories[a][i] << k;
				k++;
				k %= block_size;
			}
		} else {
			for (int i=0; i<a+2; i++) {
				x <<= shift;
				x += acyclic2_histories[a][i];
			}
		}
		return x;
	}

	// hash modulo history with a given modulus, length, and style

	unsigned int hash_modhist (int a, int b, int n) {
		unsigned int x = 0, k = 0;
		for (int i=0; i<b; i++) {
			x ^= mod_histories[a][i] << k;
			k++;
			k %= n;
		}
		return x;
	}

	// hash modulo path history with a given modulus, depth, and shift

	unsigned int hash_modpath (int a, int depth, int shift) {
		unsigned int x = 0;
		for (int i=0; i<depth; i++) {
			x <<= shift;
			x += modpath_histories[a][i];
		}
		return x;
	}

	// hash modulo path history together with modulo (outcome) history

	unsigned int hash_ghistmodpath (int a, int depth, int shift) {
		unsigned int x = 0;
		for (int i=0; i<depth; i++) {
			x <<= shift;
			x += (modpath_histories[a][i] << 1) | mod_histories[a][i];
		}
		return x;
	}

	// hash the recency position where we find this PC

	unsigned int hash_recencypos (unsigned int pc, int l, int lg, int t) {
	
		// search for the PC

		for (int i=0; i<l; i++) {
			if (recency_stack[i] == pc) {
				int r = i;
				if (lg != -1) {
					r = log (r) / log (lg / 100.0);
				}
				return r * table_sizes[t] / l;
			}
		}

		// return last index in table on a miss

		return table_sizes[t] - 1;
	}

	int translate (int c, int i) {
		assert (c > -32 && c < 32);
		assert (bitwidth == 6);
		return xlat[c+31];
	}

	// use a history specification to call the corresponding history hash function

	unsigned int get_hash (history_spec *s, unsigned int pc, unsigned int pc2, int t) {
		unsigned int x = 0;
		switch (s->type) {
		case BACKGHISTPATH:
			x = hash_backghistpath (s->p1, s->p2, s->p3, s->p4);
			break;
		case TAKENPATH:
			x = hash_taken_hist (s->p1, s->p2);
			break;
		case GHISTPATH:
			x = hash_ghistpath (s->p1, s->p2, s->p3, s->p4);
			break;
		case ACYCLIC:
			x = hash_acyclic (s->p1, s->p2, s->p3);
			break;
		case MODHIST:
			x = hash_modhist (s->p1, s->p2, block_size);
			break;
		case GHISTMODPATH:
			x = hash_ghistmodpath (s->p1, s->p2, s->p3);
			break;
		case MODPATH:
			x = hash_modpath (s->p1, s->p2, s->p3);
			break;
		case BIAS:
			x = 0;
			break;
		case RECENCY:
			x = hash_recency (s->p1, s->p2, s->p3);
			break;
		case IMLI:
			if (s->p1 == 1) x = imli_counter1;
			else if (s->p1 == 2) x = imli_counter2;
			else if (s->p1 == 3) x = imli_counter3;
			else if (s->p1 == 4) x = imli_counter4;
			else assert (0);
			if (s->p2 == -1 && s->p3 == -1) {
				// nothing
			} else {
				int h1 = s->p2;
				int h2 = s->p3;
				bool loc = false;
				if (h1 < 0) { loc = true; h1 = -h1; }
				if (h2 < 0) { loc = true; h2 = -h2; }
				if (loc) 
					x ^= hash_backghist (h1, h2);
				else
					x ^= hash_ghist (h1, h2);
			}

			break;
		case PATH:
			x = hash_path (s->p1, s->p2);
			break;
		case RHIST:
			x = hash_ras (s->p1, s->p2, s->p3, s->p4);
			break;
		case BACKPATH:
			x = hash_backpath (s->p1, s->p2);
			break;
		case LOCAL:
			x = local_histories[hashlocal() % nlocal_histories] >> s->p1;
			if (s->p1 != -1) x &= ((1<<((s->p2)-(s->p1)))-1);
			break;
		case BLURRYPATH:
			x = hash_blurry (s->p1, s->p2, s->p3);
			break;
		case RECENCYPOS:
			x = hash_recencypos (pc2, s->p1, s->p2, t);
			break;
		default: 
			printf ("oh shit! %d\n", (int) s->type);
			for (int i=0; i<num_features; i++) printf ("%d\n", specs[i].type);
			assert (0);
		}
		return x;
	}

	// compute the perceptron output as u.yout

	double alt_yout;

	void compute_output (btb_entry *e) {
		// initialize sum

		u.yout = 0;
		if (u.backward) {
			u.yout += backward_bias;
		} else {
			u.yout += forward_bias;
		}

		// for each feature...

		unsigned int indices[num_features];
		history_spec *myspecs = specv[u.hpc % NSPECS];
		for (int i=0; i<num_tables; i++) {
			// get the hash to index the table
			unsigned int g = get_hash (&myspecs[i], u.pc, u.pc2, i);
			unsigned long long int h;

			// shift the hash from the feature to xor with the hashed PC

			int hs = hshift;
			if (hs < 0) {
				h = g;
				h <<= -hs;
				h ^= u.pc2;
			} else {
				h = g;
				h <<= hs;
				h ^= u.hpc;
			}
			h = hash (h, 4);

			if (myspecs[i].xorflags & XOR_HASH1) h = hash (h, 1);
			if (myspecs[i].xorflags & XOR_HASH2) h = hash (h, 2);
			if (myspecs[i].xorflags & XOR_HASH3) h = hash (h, 3);

			// xor in the imli counter(s) and/or recency position based on the masks

			if (myspecs[i].xorflags & XOR_IMLI1) h ^= imli_counter1;
			if (myspecs[i].xorflags & XOR_IMLI4) h ^= imli_counter4;
			if (myspecs[i].xorflags & XOR_RPOS) h ^= hash_recencypos (u.pc2, 31, -1, i);

			unsigned int h_index;
			h_index = h;
			indices[i] = h_index;
		}

		alt_yout = 0.0;
		for (int i=0; i<num_tables; i++) {
			unsigned int x = indices[i];
			unsigned int h;

			h = x % table_sizes[i];
			u.indices[i] = h;
			int w;
			w = tables[i][h].val;
			int tw = translate (w, i);
			u.yout += tw;
		}
	}

	// hash functions by Thomas Wang, best live link is http://burtleburtle.net/bob/hash/integer.html

	unsigned int hash1 (unsigned int a) {
		a = (a ^ 0xdeadbeef) + (a<<4);
		a = a ^ (a>>10);
		a = a + (a<<7);
		a = a ^ (a>>13);
		return a;
	}

	unsigned int hash2 (unsigned int key) {
		int c2=0x27d4eb2d; // a prime or an odd constant
		key = (key ^ 61) ^ (key >> 16);
		key = key + (key << 3);
		key = key ^ (key >> 4);
		key = key * c2;
		key = key ^ (key >> 15);
		return key;
	}

	// hash a key with the i'th hash function using the common Bloom filter trick
	// of linearly combining two hash functions with i as the slope

	unsigned int hash (unsigned int key, unsigned int i) {
		return hash2 (key) * i + hash1 (key);
	}

	// hash for indexing the table of local histories

	unsigned int hashlocal (void) {
		if (do_hash)
			return u.pc >> 2;
		else
			return u.pc;
	}

	// make a prediction

	bool perc_prediction;
	mpp_branch_update *lookup (unsigned int pc, bool, bool, bool, btb_entry *e) {
		// get different PC hashes

		unsigned int thread_id = (pc >> 22) & 1023;
		current_thread = thread_id;
		u.pc = pc;
		if (do_hash) {
			u.pc2 = pc >> 2;
			u.hpc = hash_pc (pc);
		} else {
			u.pc2 = pc;
			u.hpc = pc;
		}
		u.backward = false;
		if (e) u.backward = pc > e->target;
		compute_output (e);
		u.prediction (u.yout >= 1);
		return &u;
	}

	// adaptive theta training, adapted from O-GEHL

	void theta_setting (bool correct, int a, double *theta, int *tc, int speed, int max = 255) {
		if (!correct) {
			(*tc)++;
			if (*tc >= speed) {
				(*theta)++;
				*tc = 0;
			}
		}
		if (correct && a < *theta) {
			(*tc)--;
			if (*tc <= -speed) {
				(*theta)--;
				*tc = 0;
			}
		}
		if (*theta < min_theta) *theta = min_theta;
		if (*theta > max) *theta = max;
	}

	int satincdec (int c, bool taken, int i) {
		int limit = (1<<(bitwidth-1))-1;
		if (taken) {
			if (c < limit) c++;
		} else {
			if (c > -limit) c--;
		}
		return c;
	}

	// train the perceptron predictor

	void train (btb_entry *e, bool taken, bool recursive = false) {
		bool correct;
		// was the prediction correct?
		//correct = (u.yout >= 1) == taken;
		double y = u.yout;
		if (!taken) y = -y;

		correct = y >= 1.0;

		// what is the magnitude of yout?

		int a = (int) fabs (u.yout * fudge);

		// train the weights, computing what the value of yout
		// would have been if these updates had been applied before

		bool do_train = !correct || (a <= theta);

		if (!do_train) return;
		// if the branch was predicted incorrectly or the correct
		// prediction was weak, update the weights

		// adaptive theta tuning
	
		theta_setting (correct, a, &theta, &tc, speed);
	
		// train the weights, computing what the value of yout
		// would have been if these updates had been applied before
	
		// how different is the output from the correct, outside of theta output?
	
		a = abs (u.yout);
		int newyout = u.yout;
		for (int i=0; i<num_tables; i++) {
			Weight *w;
			w = &tables[i][u.indices[i]];
			
			newyout -= translate (w->val, i);
			// increment/decrement if taken/not taken
			w->val = satincdec (w->val, taken, i);
			newyout += translate (w->val, i);
		}
	}

	~mpp_impl (void) {
	}

	// update the predictor

	void update (mpp_branch_update *p, unsigned int target, bool taken, bool, bool, bool, int type, bool do_train, bool filtered, btb_entry *e) {
		unsigned int hpc = u.hpc;
		u.filtered = filtered;
		u.target = target;
		if (!u.filtered && do_train) {
			train (e, taken);
		}


		// mask values for whether or not to record a filtered branch into a history register 

#define RECORD_FILTERED_IMLI	1
#define RECORD_FILTERED_GHIST	2
#define RECORD_FILTERED_PATH	4
#define RECORD_FILTERED_ACYCLIC	8
#define RECORD_FILTERED_MOD	16
#define RECORD_FILTERED_BLURRY	32
#define RECORD_FILTERED_LOCAL	64	// should never record a filtered local branch - duh!
#define RECORD_FILTERED_RECENCY	128

		// four different styles of IMLI

		if (!u.filtered || (record_mask & RECORD_FILTERED_IMLI)) {
			if (target < u.pc) {
				if (taken)
					imli_counter1++;
				else
					imli_counter1 = 0;
				if (!taken)
					imli_counter2++;
				else
					imli_counter2 = 0;
			} else {
				if (taken)
					imli_counter3++;
				else
					imli_counter3 = 0;
				if (!taken)
					imli_counter4++;
				else
					imli_counter4 = 0;
			}
		}

		// we can hash the branch outcome with a PC bit. doesn't really help.

		bool hashed_taken = hash_taken ? (taken ^ !(u.pc & (1<<htbit))) : taken;
		
		// record into backwards ghist

		if (!u.filtered || (record_mask & RECORD_FILTERED_GHIST)) {
			if (target < u.pc) {
				update_backglobal_hist (hashed_taken);
			}
		}

		// record into taken hist
		if (!u.filtered) {
			if (taken) {
				update_taken_hist (u.hpc);
			}
		}

		// record into ghist

		if (!u.filtered || (record_mask & RECORD_FILTERED_GHIST)) {
			update_global_hist (hashed_taken);
		}

		// record into path history

		if (!u.filtered || (record_mask & RECORD_FILTERED_PATH)) {
			memmove (&path_history[1], &path_history[0], sizeof (unsigned short int) * (path_length-1));
			path_history[0] = u.pc2;
		}

		// backward path history

		if (!u.filtered || (record_mask & RECORD_FILTERED_PATH)) {
			if (target < u.pc) {
				memmove (&back_path[1], &back_path[0], sizeof (unsigned short int) * (MAX_PATHHIST-1));
				back_path[0] = u.pc2;
			}
		}

		// record into acyclic history

		if (!u.filtered || (record_mask & RECORD_FILTERED_ACYCLIC)) {
			for (int i=0; i<MAX_ACYCLIC; i++) {
				acyclic_histories[i][hpc%(i+2)] = hashed_taken;
				acyclic2_histories[i][hpc%(i+2)] = hpc;
			}
		}

		// record into modulo path history

		if (!u.filtered || (record_mask & RECORD_FILTERED_MOD)) {
			for (int ii=0; ii<nmodpath_histories; ii++) {
				int i = modpath_indices[ii];
				if (hpc % (i+2) == 0) {
					memmove (&modpath_histories[i][1], &modpath_histories[i][0], sizeof (unsigned short int) * (modpath_lengths[ii]-1));
					modpath_histories[i][0] = u.pc2;
				}
			}
		}

		// update blurry history

		if (!u.filtered || (record_mask & RECORD_FILTERED_BLURRY)) {
			for (int i=0; i<MAX_BLURRY; i++) {
				unsigned int z = u.pc >> i;
				if (blurrypath_histories[i][0] != z) {
					memmove (&blurrypath_histories[i][1], &blurrypath_histories[i][0], sizeof (unsigned int) * (MAX_BLURRY2-1));
					blurrypath_histories[i][0] = z;
				}
			}
		}

		// record into modulo pattern history

		if (!u.filtered || (record_mask & RECORD_FILTERED_MOD)) {
			for (int ii=0; ii<nmodhist_histories; ii++) {
				int i = modhist_indices[ii];
				if (hpc % (i+2) == 0) {
					memmove (&mod_histories[i][1], &mod_histories[i][0], modhist_lengths[ii]-1);
					mod_histories[i][0] = hashed_taken;
				}
			}
		}

		// insert this PC into the recency stack

		if (!u.filtered || (record_mask & RECORD_FILTERED_RECENCY)) {
			insert_recency (u.pc2);
		}

		// record into a local history

		if (!u.filtered || (record_mask & RECORD_FILTERED_LOCAL)) {
			unsigned int *lo = &local_histories[hashlocal() % nlocal_histories];
			*lo <<= 1;
			*lo |= taken;
			*lo &= ((1<<local_history_length)-1);
		}
        }

	void shift_ghist (bool bit) {
		update_global_hist (bit);
	}

	void doshift (unsigned int pc, unsigned int target, unsigned int pcflag, unsigned int targetflag) {
		target >>= pcbit;
		pc >>= pcbit;
		if (xflag & pcflag) for (int i=0; i<xn; i++) { shift_ghist (pc & 1); pc >>= 1; }
		if (xflag & targetflag) for (int i=0; i<xn; i++) { shift_ghist (target & 1); target >>= 1; }
	}

	// update ghist and path history on branches that aren't conditional

	void nonconditional_branch (unsigned int pc, unsigned int target, int type) {
		unsigned short int pc2 = pc >> 2;

		if (!xflag) {
			update_global_hist (!(pc & (1<<pcbit)));
		}
#define X_JMP_PC	1
#define X_JMP_TARGET	2
#define X_RET_PC	4
#define X_RET_TARGET	8
#define X_IND_PC	16
#define X_IND_TARGET	32
#define X_CALL_PC	64
#define X_CALL_TARGET	128

		if (type == OPTYPE_RET_UNCOND) {
			if (ras.size()) ras.pop_front ();
		}
		if (type == OPTYPE_CALL_DIRECT_UNCOND || type == OPTYPE_CALL_INDIRECT_UNCOND) {
			if (ras.size() > 32) ras.pop_back ();
			ras.push_front (pc);
		}
		if (type == OPTYPE_RET_UNCOND) doshift (pc, target, X_RET_PC, X_RET_TARGET);
		if (type == OPTYPE_JMP_DIRECT_UNCOND) doshift (pc, target, X_JMP_PC, X_JMP_TARGET);
		if (type == OPTYPE_CALL_DIRECT_UNCOND) doshift (pc, target, X_CALL_PC, X_CALL_TARGET);
		if (type == OPTYPE_CALL_INDIRECT_UNCOND) doshift (pc, target, X_IND_PC, X_IND_TARGET);
		memmove (&path_history[1], &path_history[0], sizeof (unsigned short int) * (path_length-1));
		path_history[0] = pc2;
	}
};

#endif
