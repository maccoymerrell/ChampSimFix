#ifndef _BTB_H
#define _BTB_H

#define BTBREPL_LRU	0
#define BTBREPL_RRIP	1

struct btb_entry {
	unsigned int pc;	// pc of branch instruction
	bool taken, untaken;
	bool agree_last, disagree_last;
	int state;
	int set, way;
	int rr;
	int lrupos;
	unsigned int target;
	bool was_miss;
	int init;

	btb_entry (void) {
		was_miss = false;
		init = 0;
		target = 0;
		rr = 0;
		pc = 0;
		untaken = false;
		taken = false;
		agree_last = false;
		disagree_last = false;
		state = 0;
	}
};

struct btb {
	int policy, placement_position;
	int assoc, nsets;
	btb_entry **entries;
	long long int time;

	btb (int _assoc, int _nsets, int _policy = BTBREPL_LRU, int _placement_position = 0) : assoc(_assoc), nsets(_nsets), policy (_policy), placement_position (_placement_position) {
		time = 0;
		entries = new btb_entry*[nsets];
		assert (entries);
		for (int i=0; i<nsets; i++) {
			entries[i] = new btb_entry[assoc];
			assert (entries[i]);
			for (int j=0; j<assoc; j++) {
				entries[i][j].set = i;
				entries[i][j].way = j;
				entries[i][j].lrupos = j;
			}
		}
	}

	~btb (void) {
		for (int i=0; i<nsets; i++) delete[] entries[i];
		delete[] entries;
		// fprintf (stderr, "destructor for btb!\n"); fflush (stderr);
	}

	unsigned int getidx (unsigned int pc) {
		return (pc >> 2) % nsets;
	}

	btb_entry *probe (unsigned int pc) {
		unsigned int idx = getidx (pc);
		btb_entry *set = entries[idx];
		for (int i=0; i<assoc; i++)
			if (set[i].pc == pc)
				return &set[i];
		return NULL;
	}

	btb_entry *allocate (unsigned pc, unsigned int target) {
		unsigned int idx = getidx (pc);
		btb_entry *set = entries[idx];
		for (int i=0; i<assoc; i++)
			if (set[i].pc == pc) assert (0);

		// replace; get minimum time stamp entry

		int mini = 0;
		if (policy == BTBREPL_LRU) {
			for (int i=0; i<assoc; i++) 
				if (set[i].lrupos == assoc-1)
					mini = i;
			assert (set[mini].lrupos == assoc-1);
		} else if (policy == BTBREPL_RRIP) {
		more:
			int r = -1;
			for (int i=0; i<assoc; i++) {
				if (set[i].rr == 3) r = i;
			}
			if (r == -1) {
				for (int i=0; i<assoc; i++) {
					set[i].rr++;
					assert (set[i].rr <= 3);
				}
				goto more;
			}
			mini = r;
		} else assert (0);
		btb_entry *r = &set[mini];
		r->target = target;
		update (r, placement_position);
		r->pc = pc;
		r->rr = placement_position;
		r->agree_last = false;
		r->disagree_last = false;
		r->untaken = false;
		r->taken = false;
		r->state = 0;
		return r;
	}

	void update (btb_entry *p, int newplace = 0) {
		btb_entry *block = entries[p->set];
		int curpos = p->lrupos;
		bool found = false;
		if (newplace < curpos) {
			for (int way=0; way<assoc; way++) {
				int s = block[way].lrupos;
				if (s >= newplace && s < curpos) {
					block[way].lrupos++;
				}
			}
		} else {
			for (int way=0; way<assoc; way++) {
				int s = block[way].lrupos;
				if (s <= newplace && s > curpos)
				block[way].lrupos--;
			}
		}
		p->lrupos = newplace;
		p->rr = 0;
	}
};
#endif
