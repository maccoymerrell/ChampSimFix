#ifndef SPP_PPF_DUELING_H
#define SPP_PPF_DUELING_H

#include <iostream>
#include <fstream>
#include "cache.h"
using namespace std;

struct ppf_dueling : public champsim::modules::prefetcher {
	// SPP functional knobs
	constexpr static bool LOOKAHEAD_ON = true;
	constexpr static bool FILTER_ON = true;
	constexpr static bool GHR_ON = true;
	constexpr static bool SPP_SANITY_CHECK = true;

	//#define SPP_DEBUG_PRINT
	constexpr static bool SPP_DEBUG_PRINT = false;
	//#ifdef SPP_DEBUG_PRINT
	//#define SPP_DP(x) x
	//#else
	//#define SPP_DP(x)
	//#endif

	constexpr static bool SPP_PERC_WGHT = false;
	//#define SPP_PERC_WGHT
	//#ifdef SPP_PERC_WGHT
	//#define SPP_PW(x) x
	//#else 
	//#define SPP_PW(x)
	//#endif

	// Signature table parameters
	constexpr static unsigned ST_SET = 1;
	constexpr static unsigned ST_WAY = 256;
	constexpr static unsigned ST_TAG_BIT = 16;
	constexpr static unsigned ST_TAG_MASK = ((1 << ST_TAG_BIT) - 1);
	constexpr static unsigned SIG_SHIFT = 3;
	constexpr static unsigned SIG_BIT = 12;
	constexpr static unsigned SIG_MASK = ((1 << SIG_BIT) - 1);
	constexpr static unsigned SIG_DELTA_BIT = 7;

	// Pattern table parameters
	constexpr static unsigned PT_SET = 512;
	constexpr static unsigned PT_WAY = 4;
	constexpr static unsigned C_SIG_BIT = 4;
	constexpr static unsigned C_DELTA_BIT = 4;
	constexpr static unsigned C_SIG_MAX = ((1 << C_SIG_BIT) - 1);
	constexpr static unsigned C_DELTA_MAX = ((1 << C_DELTA_BIT) - 1);

	// Prefetch filter parameters
	constexpr static unsigned QUOTIENT_BIT = 10;
	constexpr static unsigned REMAINDER_BIT = 6;
	constexpr static unsigned HASH_BIT = (QUOTIENT_BIT + REMAINDER_BIT + 1);
	constexpr static unsigned FILTER_SET = (1 << QUOTIENT_BIT);

	constexpr static unsigned QUOTIENT_BIT_REJ = 10;
	constexpr static unsigned REMAINDER_BIT_REJ = 8;
	constexpr static unsigned HASH_BIT_REJ = (QUOTIENT_BIT_REJ + REMAINDER_BIT_REJ + 1);
	constexpr static unsigned FILTER_SET_REJ = (1 << QUOTIENT_BIT_REJ);


	// Global register parameters
	constexpr static unsigned GLOBAL_COUNTER_BIT = 10;
	constexpr static unsigned GLOBAL_COUNTER_MAX = ((1 << GLOBAL_COUNTER_BIT) - 1);
	constexpr static unsigned MAX_GHR_ENTRY = 8;
	constexpr static unsigned PAGES_TRACKED = 6;

	// Perceptron paramaters
	constexpr static unsigned PERC_ENTRIES = 4096; //Upto 12-bit addressing in hashed perceptron
	constexpr static unsigned PERC_FEATURES = 9; //Keep increasing based on new features
	constexpr static unsigned PERC_COUNTER_MAX = 15; //-16 to +15: 5 bits counter 
	constexpr static long PERC_THRESHOLD_HI = 75;
	constexpr static long PERC_THRESHOLD_LO = -15;
	constexpr static unsigned POS_UPDT_THRESHOLD = 90;
	constexpr static long NEG_UPDT_THRESHOLD = -80;

	constexpr static unsigned SHORT_PAGE_BITS = 12;
	constexpr static unsigned LONG_PAGE_BITS = 21;

	constexpr static uint32_t SHORT_PAGE_DUEL_ID = 1000;
	constexpr static uint32_t LONG_PAGE_DUEL_ID = 2000;

	static constexpr std::size_t PM_SETS = 32;
  	static constexpr std::size_t PM_WAYS = 4;
  	static constexpr std::size_t PAGE_MAP_SIZE = 64;


	constexpr static unsigned CNT_DUELING_MAX = 32;

	enum FILTER_REQUEST {SPP_L2C_PREFETCH, SPP_LLC_PREFETCH, L2C_DEMAND, L2C_EVICT, SPP_PERC_REJECT}; // Request type for prefetch filter

	static uint64_t get_hash(uint64_t key);

	struct page_map {
		uint64_t page_num;
		constexpr static std::size_t PM_BASE = 100;
		std::array<uint8_t,PAGE_MAP_SIZE> bits;

		page_map() : page_map(0) {}
		explicit page_map(uint64_t page_num_) : page_num(page_num_) {
			for(std::size_t i = 0; i < PAGE_MAP_SIZE; i++)
				bits.at(i) = 0;
		}
  	};

	struct page_map_set {
		auto operator()(const page_map& entry) const { return entry.page_num; }
	};
	struct page_map_way {
		auto operator()(const page_map& entry) const { return entry.page_num; }
	};
	

	template<std::size_t page_bits>
	struct block_in_page_extent : champsim::dynamic_extent {
    block_in_page_extent() : dynamic_extent(champsim::data::bits{page_bits}, champsim::data::bits{LOG2_BLOCK_SIZE}) {}
  	};

	template<std::size_t page_bits>
	class SIGNATURE_TABLE {
	public:
		ppf_dueling* parent_;
		bool     valid[ST_SET][ST_WAY];
		uint32_t tag[ST_SET][ST_WAY];
		uint32_t sig[ST_SET][ST_WAY],
				 lru[ST_SET][ST_WAY];
		uint32_t last_offset[ST_SET][ST_WAY];

		SIGNATURE_TABLE() {
			cout << "Initialize SIGNATURE TABLE" << endl;
			cout << "ST_SET: " << ST_SET << endl;
			cout << "ST_WAY: " << ST_WAY << endl;
			cout << "ST_TAG_BIT: " << ST_TAG_BIT << endl;
			cout << "ST_TAG_MASK: " << hex << ST_TAG_MASK << dec << endl;

			for (uint32_t set = 0; set < ST_SET; set++)
				for (uint32_t way = 0; way < ST_WAY; way++) {
					valid[set][way] = 0;
					tag[set][way] = 0;
					last_offset[set][way] = 0;
					sig[set][way] = 0;
					lru[set][way] = way;
				}
		};

		void read_and_update_sig(uint64_t page, uint64_t page_offset, uint32_t &last_sig, uint32_t &curr_sig, int64_t &delta);
	};

	template<std::size_t page_bits>
	class PATTERN_TABLE {
	public:
		ppf_dueling* parent_;
		typename champsim::address_slice<block_in_page_extent<page_bits>>::difference_type delta[PT_SET][PT_WAY];
		uint32_t c_delta[PT_SET][PT_WAY],
				 c_sig[PT_SET];

		PATTERN_TABLE() {
			cout << endl << "Initialize PATTERN TABLE" << endl;
			cout << "PT_SET: " << PT_SET << endl;
			cout << "PT_WAY: " << PT_WAY << endl;
			cout << "SIG_DELTA_BIT: " << SIG_DELTA_BIT << endl;
			cout << "C_SIG_BIT: " << C_SIG_BIT << endl;
			cout << "C_DELTA_BIT: " << C_DELTA_BIT << endl;

			for (uint32_t set = 0; set < PT_SET; set++) {
				for (uint32_t way = 0; way < PT_WAY; way++) {
					delta[set][way] = 0;
					c_delta[set][way] = 0;
				}
				c_sig[set] = 0;
			}
		}

		void update_pattern(uint32_t last_sig, typename champsim::address_slice<block_in_page_extent<page_bits>>::difference_type curr_delta),
			read_pattern(uint32_t curr_sig, std::vector<typename champsim::address_slice<block_in_page_extent<page_bits>>::difference_type>& prefetch_delta, std::vector<uint32_t>& confidence_q, std::vector<int32_t>& perc_sum_q, uint32_t &lookahead_way, uint32_t &lookahead_conf, uint32_t &pf_q_tail, uint32_t &depth, champsim::address addr, champsim::address base_addr, champsim::address train_addr, champsim::address curr_ip, champsim::block_number::difference_type train_delta, uint32_t last_sig, uint32_t pq_occupancy, uint32_t pq_SIZE, uint32_t mshr_occupancy, uint32_t mshr_SIZE);
	};

	template<std::size_t page_bits>
	class PREFETCH_FILTER {
	public:
		champsim::msl::lru_table<page_map,page_map_set,page_map_way> page_map_table{PM_SETS,PM_WAYS};
		double filtered = 0;
		ppf_dueling* parent_;
		uint64_t remainder_tag[FILTER_SET];
		champsim::address pc[FILTER_SET],
				pc_1[FILTER_SET],
				pc_2[FILTER_SET],
				pc_3[FILTER_SET],
				address[FILTER_SET];
		bool     valid[FILTER_SET],  // Consider this as "prefetched"
				useful[FILTER_SET]; // Consider this as "used"
		typename champsim::address_slice<block_in_page_extent<page_bits>>::difference_type	 delta[FILTER_SET];
		int32_t perc_sum[FILTER_SET];
		uint32_t last_signature[FILTER_SET],
				confidence[FILTER_SET],
				cur_signature[FILTER_SET],
				la_depth[FILTER_SET];

		uint64_t remainder_tag_reject[FILTER_SET_REJ];
		champsim::address pc_reject[FILTER_SET_REJ],
				pc_1_reject[FILTER_SET_REJ],
				pc_2_reject[FILTER_SET_REJ],
				pc_3_reject[FILTER_SET_REJ],
				address_reject[FILTER_SET_REJ];
		bool 	 valid_reject[FILTER_SET_REJ]; // Entries which the perceptron rejected
		typename champsim::address_slice<block_in_page_extent<page_bits>>::difference_type delta_reject[FILTER_SET_REJ];
		int32_t perc_sum_reject[FILTER_SET_REJ];
		uint32_t last_signature_reject[FILTER_SET_REJ],
				confidence_reject[FILTER_SET_REJ],
				cur_signature_reject[FILTER_SET_REJ],
				la_depth_reject[FILTER_SET_REJ];

		// Tried the set-dueling idea which din't work out
		uint32_t PSEL_1;
		uint32_t PSEL_2;

		// To enable / disable negative training using reject filter
		// Set to 1 in the prefetcher file
		bool train_neg;

		float hist_hits[55];
		float hist_tots[55];

		PREFETCH_FILTER() {
			cout << endl << "Initialize PREFETCH FILTER" << endl;
			cout << "FILTER_SET: " << FILTER_SET << endl;

			for (int i = 0; i < 55; i++) {
				hist_hits[i] = 0;
				hist_tots[i] = 0;
			}
			for (uint32_t set = 0; set < FILTER_SET; set++) {
				remainder_tag[set] = 0;
				valid[set] = 0;
				useful[set] = 0;
			}
			for (uint32_t set = 0; set < FILTER_SET_REJ; set++) {
				valid_reject[set] = 0;
				remainder_tag_reject[set] = 0;
			}
			train_neg = 0;
		}

		void add_to_pagemap(champsim::address addr);
		bool check_pagemap(champsim::address addr);
		void remove_from_pagemap(champsim::address addr);

		bool     check(champsim::address pf_addr,champsim::address base_addr, champsim::address ip, FILTER_REQUEST filter_request, champsim::block_number::difference_type cur_delta, uint32_t last_sign, uint32_t cur_sign, uint32_t confidence, int32_t sum, uint32_t depth);
	};

	template<std::size_t page_bits>
	class PERCEPTRON {
	public:
		ppf_dueling* parent_;
		// Perc Weights
		int32_t perc_weights[PERC_ENTRIES][PERC_FEATURES];

		// Only for dumping csv
		bool    perc_touched[PERC_ENTRIES][PERC_FEATURES];

		// CONST depths for different features
		int32_t PERC_DEPTH[PERC_FEATURES];

		PERCEPTRON() {
			cout << "\nInitialize PERCEPTRON" << endl;
			cout << "PERC_ENTRIES: " << PERC_ENTRIES << endl;
			cout << "PERC_FEATURES: " << PERC_FEATURES << endl;

			PERC_DEPTH[0] = 2048;   //base_addr;
			PERC_DEPTH[1] = 4096;   //cache_line;
			PERC_DEPTH[2] = 4096;  	//page_addr;
			PERC_DEPTH[3] = 4096;   //confidence ^ page_addr;
			PERC_DEPTH[4] = 1024;	//curr_sig ^ sig_delta;
			PERC_DEPTH[5] = 4096; 	//ip_1 ^ ip_2 ^ ip_3;		
			PERC_DEPTH[6] = 1024; 	//ip ^ depth;
			PERC_DEPTH[7] = 2048;   //ip ^ sig_delta;
			PERC_DEPTH[8] = 128;   	//confidence;

			for (int i = 0; i < PERC_ENTRIES; i++) {
				for (int j = 0;j < PERC_FEATURES; j++) {
					perc_weights[i][j] = 0;
					perc_touched[i][j] = 0;
				}
			}
		}

		void get_perc_index(champsim::address base_addr, champsim::address ip, champsim::address ip_1,champsim::address ip_2, champsim::address ip_3, champsim::block_number::difference_type cur_delta, uint32_t last_sig, uint32_t curr_sig, uint32_t confidence, uint32_t depth, uint64_t* perc_set);

		void	 perc_update(champsim::address check_addr, champsim::address ip, champsim::address ip_1, champsim::address ip_2, champsim::address ip_3, champsim::block_number::difference_type cur_delta, uint32_t last_sig, uint32_t curr_sig, uint32_t confidence, uint32_t depth, bool direction, int32_t perc_sum);
		int32_t	perc_predict(champsim::address check_addr, champsim::address ip, champsim::address ip_1, champsim::address ip_2, champsim::address ip_3, champsim::block_number::difference_type cur_delta, uint32_t last_sig, uint32_t curr_sig, uint32_t confidence, uint32_t depth);
	};


	template<std::size_t page_bits>
	class GLOBAL_REGISTER {
	public:
		ppf_dueling* parent_;
		// Global counters to calculate global prefetching accuracy
		uint64_t pf_useful,
				pf_issued,
				global_accuracy; // Alpha value in Section III. Equation 3

		// Global History Register (GHR) entries
		uint8_t  valid[MAX_GHR_ENTRY];
		uint32_t sig[MAX_GHR_ENTRY],
				confidence[MAX_GHR_ENTRY];
		uint32_t offset[MAX_GHR_ENTRY];
		int32_t delta[MAX_GHR_ENTRY];

		champsim::address ip_0,
				ip_1,
				ip_2,
				ip_3;

		uint64_t page_tracker[PAGES_TRACKED];
		
		// Stats Collection
		double 	  depth_val,
				depth_sum,
				depth_num;
		double 	  pf_total,
				pf_l2c,
				pf_llc,
				pf_l2c_good;
		long 	  perc_pass,
				perc_reject,
				reject_update;
		// Stats

		GLOBAL_REGISTER() {
			pf_useful = 0;
			pf_issued = 0;
			global_accuracy = 0;
			ip_0 = champsim::address{};
			ip_1 = champsim::address{};
			ip_2 = champsim::address{};
			ip_3 = champsim::address{};

			// These are just for stats printing
			depth_val = 0;
			depth_sum = 0;
			depth_num = 0;
			pf_total = 0;
			pf_l2c = 0;
			pf_llc = 0;
			pf_l2c_good = 0;
			perc_pass = 0;
			perc_reject = 0;
			reject_update = 0;

			for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
				valid[i] = 0;
				sig[i] = 0;
				confidence[i] = 0;
				offset[i] = 0;
				delta[i] = 0;
			}
		}

		void update_entry(uint32_t pf_sig, uint32_t pf_confidence, uint32_t pf_offset, int32_t pf_delta);
		uint32_t check_entry(uint32_t page_offset);
	};

	using prefetcher::prefetcher;
	uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in, uint32_t metadata_hit);
	uint32_t do_prefetch_short(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
											uint32_t metadata_in);
	uint32_t do_prefetch_long(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
												uint32_t metadata_in);
	int update_ghrs(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
													uint32_t metadata_in, bool long_page);
	uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);

	void prefetcher_initialize();
	void prefetcher_final_stats();

	SIGNATURE_TABLE<SHORT_PAGE_BITS> ST_SHORT;
	PATTERN_TABLE<SHORT_PAGE_BITS>   PT_SHORT;
	PREFETCH_FILTER<SHORT_PAGE_BITS> FILTER_SHORT;
	GLOBAL_REGISTER<SHORT_PAGE_BITS> GHR_SHORT;
	PERCEPTRON<SHORT_PAGE_BITS> PERC_SHORT;

	SIGNATURE_TABLE<LONG_PAGE_BITS> ST_LONG;
	PATTERN_TABLE<LONG_PAGE_BITS>   PT_LONG;
	PREFETCH_FILTER<LONG_PAGE_BITS> FILTER_LONG;
	GLOBAL_REGISTER<LONG_PAGE_BITS> GHR_LONG;
	PERCEPTRON<LONG_PAGE_BITS> PERC_LONG;

	int depth_track_short[30];
	int depth_track_long[30];

	long dueling_counter = CNT_DUELING_MAX/2;

	long SET_SAMPLE_RATE;

	[[nodiscard]] constexpr bool is_sampled(long set) {
		auto mask = SET_SAMPLE_RATE - 1;
		auto shift = champsim::lg2(SET_SAMPLE_RATE);
		auto low_slice = set & mask;
		auto high_slice = (set >> shift) & mask;
		return high_slice == low_slice;
	}

};

#endif
