#include <iostream>     // std::cout, std::endl
#include <iomanip>      // std::setw
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <cmath>
#include "cache.h"
#include "spp_ppf_4kb.h"





void spp_ppf_4kb::prefetcher_initialize() 
{
	for(int a = 0; a < 30; a++)
		depth_track[a] = 0;

    ST.parent_ = this;
    PT.parent_ = this;
    FILTER.parent_ = this;
    GHR.parent_ = this;
    PERC.parent_ = this;

}

uint32_t spp_ppf_4kb::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
										uint32_t metadata_in)
{

    page_type page{addr};
    offset_type page_offset{addr};
    std::vector<uint32_t> confidence_q(100*intern_->get_mshr_size(),0);
    uint32_t last_sig = 0,
             curr_sig = 0,
             depth = 0;

    std::vector<typename offset_type::difference_type> delta_q(100*intern_->get_mshr_size(),0);
    std::vector<int32_t> perc_sum_q(100*intern_->get_mshr_size(),0);
    typename offset_type::difference_type  delta = 0;

    confidence_q[0] = 100;
    GHR.global_accuracy = GHR.pf_issued ? ((100 * GHR.pf_useful) / GHR.pf_issued)  : 0;
   	
	for (int i = PAGES_TRACKED-1; i>0; i--) { // N down to 1
		GHR.page_tracker[i] = GHR.page_tracker[i-1];
	}

	GHR.page_tracker[0] = page;

	int distinct_pages = 0;
	uint8_t num_pf = 0;
	for (int i=0; i < PAGES_TRACKED; i++) {
		int j;
		for (j=0; j<i; j++) {
			if (GHR.page_tracker[i] == GHR.page_tracker[j])
				break;
		}
		if (i==j)
			distinct_pages++;
	}
	//cout << "Distinct Pages: " << distinct_pages << endl;
	
    if(SPP_DEBUG_PRINT) {
        fmt::print("\n[ChampSim] prefetcher_cache_operate addr: {0:x} cache_line: {0:x}",addr,champsim::block_number(addr));
        fmt::print(" page: {0:x} page_offset: {}\n",page,page_offset);
    }

    // Stage 1: Read and update a sig stored in ST
    // last_sig and delta are used to update (sig, delta) correlation in PT
    // curr_sig is used to read prefetch candidates in PT 
    ST.read_and_update_sig(page, page_offset, last_sig, curr_sig, delta);

	FILTER.train_neg = 1;
    
	// Also check the prefetch filter in parallel to update global accuracy counters 
    FILTER.check(addr, champsim::address{}, champsim::address{}, L2C_DEMAND, 0, 0, 0, 0, 0, 0); 

    // Stage 2: Update delta patterns stored in PT
    if (last_sig) PT.update_pattern(last_sig, delta);

    // Stage 3: Start prefetching
    champsim::address base_addr = addr;
	champsim::address curr_ip = ip;
    uint32_t lookahead_conf = 100,
             pf_q_head = 0, 
             pf_q_tail = 0;
    uint8_t  do_lookahead = 0;
	int32_t  prev_delta = 0;

	champsim::address train_addr  = addr;
	int32_t  train_delta = 0;

	GHR.ip_3 = GHR.ip_2;
	GHR.ip_2 = GHR.ip_1;
	GHR.ip_1 = GHR.ip_0;
	GHR.ip_0 = ip;

    if(LOOKAHEAD_ON) {
        do {
            uint32_t lookahead_way = PT_WAY;

            train_addr  = addr; train_delta = prev_delta;
            // Remembering the original addr here and accumulating the deltas in lookahead stages
            
            // Read the PT. Also passing info required for perceptron inferencing as PT calls perc_predict()
            PT.read_pattern(curr_sig, delta_q, confidence_q, perc_sum_q, lookahead_way, lookahead_conf, pf_q_tail, depth, addr, base_addr, train_addr, curr_ip, train_delta, last_sig, intern_->get_pq_occupancy().back(), intern_->get_pq_size().back(), intern_->get_mshr_occupancy(), intern_->get_mshr_size());

            do_lookahead = 0;
            for (uint32_t i = pf_q_head; i < pf_q_tail; i++) {

                champsim::address pf_addr{(champsim::block_number{base_addr} + delta_q[i])};
                int32_t perc_sum   = perc_sum_q[i];

                if(SPP_DEBUG_PRINT) std::cout << "[ChampSim] State of features: \nTrain addr: " << train_addr << "\tCurr IP: " << curr_ip << "\tIP_1: " << GHR.ip_1 << "\tIP_2: " << GHR.ip_2 << "\tIP_3: " << GHR.ip_3 << "\tDelta: " << train_delta + delta_q[i] << "\t:LastSig " << last_sig << "\t:CurrSig " << curr_sig << "\t:Conf " << confidence_q[i] << "\t:Depth " << depth << "\tSUM: "<< perc_sum  << std::endl;
                FILTER_REQUEST fill_level = (perc_sum >= PERC_THRESHOLD_HI) ? SPP_L2C_PREFETCH : SPP_LLC_PREFETCH;
                
                if (champsim::page_number{addr} == champsim::page_number{pf_addr}) { // Prefetch request is in the same physical page
                    
                    // Filter checks for redundancy and returns FALSE if redundant
                    // Else it returns TRUE and logs the features for future retrieval 
                    if ( num_pf < ceil(((intern_->get_pq_size().back())/distinct_pages)) ) {					
                        if (FILTER.check(pf_addr, train_addr, curr_ip, fill_level, train_delta + delta_q[i], last_sig, curr_sig, confidence_q[i], perc_sum, (depth-1))) {

                            // Histogramming Idea
                            int32_t perc_sum_shifted = perc_sum + (PERC_COUNTER_MAX+1)*PERC_FEATURES; 
                            int32_t hist_index = perc_sum_shifted / 10;
                            FILTER.hist_tots[hist_index]++;
                            //[DO NOT TOUCH]:	
                            prefetch_line(pf_addr, (fill_level == SPP_L2C_PREFETCH),cpu,ip,fill_level == SPP_L2C_PREFETCH ? SPP_L2C_TARGET_ID : SPP_LLC_TARGET_ID,false,false); // Use addr (not base_addr) to obey the same physical page boundary
                            num_pf++;

                            // Only for stats
                            GHR.perc_pass++;
                            GHR.depth_val = 1;
                            GHR.pf_total++;
                            if (fill_level == SPP_L2C_PREFETCH)
                                GHR.pf_l2c++;
                            if (fill_level == SPP_LLC_PREFETCH)
                                GHR.pf_llc++;
                            // Stats end
                            
                            //FILTER.valid_reject[quotient] = 0;
                            if (fill_level == SPP_L2C_PREFETCH) {
                                GHR.pf_issued++;
                                if (GHR.pf_issued > GLOBAL_COUNTER_MAX) {
                                    GHR.pf_issued >>= 1;
                                    GHR.pf_useful >>= 1;
                                }
                                if(SPP_DEBUG_PRINT) std::cout << "[ChampSim] SPP L2 prefetch issued GHR.pf_issued: " << GHR.pf_issued << " GHR.pf_useful: " << GHR.pf_useful << std::endl;
                            }

                            if(SPP_DEBUG_PRINT) {
                                std::cout << "[ChampSim] " << __func__ << " base_addr: " << std::hex << base_addr << " pf_addr: " << pf_addr;
                                std::cout << " pf_cache_line: " << champsim::block_number{pf_addr}.to<uint64_t>();
                                std::cout << " prefetch_delta: " << std::dec << delta_q[i] << " confidence: " << confidence_q[i];
                                std::cout << " depth: " << i << " fill_this_level: " << (fill_level == SPP_L2C_PREFETCH) << std::endl;
                            }
                        }
                    }	
                } else if (GHR_ON) { // Prefetch request is crossing the physical page boundary
                        // Store this prefetch request in GHR to bootstrap SPP learning when we see a ST miss (i.e., accessing a new page)
                        GHR.update_entry(curr_sig, confidence_q[i], offset_type{pf_addr}, delta_q[i]); 
                }
                do_lookahead = 1;
                pf_q_head++;
            }

            // Update base_addr and curr_sig
            if (lookahead_way < PT_WAY) {
                uint32_t set = get_hash(curr_sig) % PT_SET;
                base_addr += (PT.delta[set][lookahead_way] << LOG2_BLOCK_SIZE);
                prev_delta += PT.delta[set][lookahead_way]; 

                // PT.delta uses a 7-bit sign magnitude representation to generate sig_delta
                //int sig_delta = (PT.delta[set][lookahead_way] < 0) ? ((((-1) * PT.delta[set][lookahead_way]) & 0x3F) + 0x40) : PT.delta[set][lookahead_way];
                int sig_delta = (PT.delta[set][lookahead_way] < 0) ? (((-1) * PT.delta[set][lookahead_way]) + (1 << (SIG_DELTA_BIT - 1))) : PT.delta[set][lookahead_way];
                curr_sig = ((curr_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
            }

            if(SPP_DEBUG_PRINT) {
                std::cout << "Looping curr_sig: " << std::hex << curr_sig << " base_addr: " << base_addr << std::dec;
                std::cout << " pf_q_head: " << pf_q_head << " pf_q_tail: " << pf_q_tail << " depth: " << depth << std::endl;
            }

        } while (do_lookahead);
    }
	// Stats
	if(GHR.depth_val) {
		GHR.depth_num++;
		GHR.depth_sum += depth;
	}
    if(depth < 30)
	    depth_track[depth]++;
	return metadata_in;
}

uint32_t spp_ppf_4kb::prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{

    //prefetch dropped
    if(way == intern_->NUM_WAY && evicted_addr == champsim::address{}) {
        FILTER.check(addr, champsim::address{}, champsim::address{}, L2C_EVICT, 0, 0, 0, 0, 0, 0);
        return metadata_in;
    }

    if(FILTER_ON) {
        if(SPP_DEBUG_PRINT) {
            fmt::print("\n");
        }
        FILTER.check(evicted_addr, champsim::address{}, champsim::address{}, L2C_EVICT, 0, 0, 0, 0, 0, 0);
    }

	return metadata_in;
}

void spp_ppf_4kb::prefetcher_final_stats()
{
	if(SPP_DEBUG_PRINT) {
		// fmt::print("\nAvg Lookahead Depth: {}\t\n",GHR.depth_sum / GHR.depth_num); 
		// fmt::print("TOTAL: {}\tL2C: {}\tLLC: {} GOOD_L2C: {}\n",GHR.pf_total, GHR.pf_l2c, GHR.pf_llc, GHR.pf_l2c_good);
        // fmt::print("PERC PASS: {}\tPERC REJECT: {}\tREJECT_UPDATE: {}\n",GHR.perc_pass,GHR.perc_reject,GHR.reject_update);
    }

    if(SPP_PERC_WGHT) {
			
			ofstream myfile;
			char fname[] =  "perc_weights_0.csv";
			myfile.open(fname, std::ofstream::app);
			fmt::print("Printing all the perceptron weights to: {}\n",fname);

			std::string row = "base_addr,cache_line,page_addr,confidence^page_addr,curr_sig^sig_delta,ip_1^ip_2^ip_3,ip^depth,ip^sig_delta,confidence,\n"; 
			for (int i = 0; i < PERC_ENTRIES; i++) {
				//row = row + "Entry#: " + std::to_string(i) + ",";
				for (int j = 0; j < PERC_FEATURES; j++) {
					if (PERC.perc_touched[i][j]) {
						row = row + std::to_string(PERC.perc_weights[i][j]) + ",";
					}
					else {
						row = row + ",";
						if (PERC.perc_weights[i][j] != 0) {
							// Throw assertion if the weight is tagged as untouched and still non-zero 
							//cout << "I:" << i << "\tJ: "<< j << "\tWeight: " << PERC.perc_weights[i][j] << endl;
							//assert(0);
						}
					}
				}
				row = row + "\n";
			}
			myfile << row;
			myfile.close();	
    }
    if(SPP_DEBUG_PRINT) {
            /*
        fmt::print("\n\n****HISTOGRAMMING STATS****\n");
        fmt::print("\tIndex\t\t hist_tots \t\t hist_hits \t\t hist_ratio\n");
        for (int i = 0; i < 55; i++) {
            float hist_ratio = 0;
            if (FILTER.hist_tots[i] != 0)
                hist_ratio = FILTER.hist_hits[i] / FILTER.hist_tots[i];
            fmt::print("{:10}   \t {:10} \t {:10} \t {:10}\n",i*10-(PERC_COUNTER_MAX+1)*PERC_FEATURES, int(FILTER.hist_tots[i]),int(FILTER.hist_hits[i]),hist_ratio);
        }
        */
        
    }

	int tot = 0;
    fmt::print("------------------\n");
    fmt::print("{} Depth Distribution\n", intern_->NAME);
    fmt::print("------------------\n");

	for(int a = 0; a < 30; a++){
        fmt::print("depth {}: {}\n",a, depth_track[a]);
		tot += depth_track[a];
	}
    fmt::print("Total: {}\n",tot);
	fmt::print("------------------\n");

}

// TODO: Find a good 64-bit hash function
uint64_t spp_ppf_4kb::get_hash(uint64_t key)
{
    // Robert Jenkins' 32 bit mix function
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);

    // Knuth's multiplicative method
    key = (key >> 3) * 2654435761;

    return key;
}

void spp_ppf_4kb::SIGNATURE_TABLE::read_and_update_sig(page_type page, offset_type page_offset, uint32_t &last_sig, uint32_t &curr_sig, typename offset_type::difference_type &delta)
{
    uint32_t set = spp_ppf_4kb::get_hash(page.to<uint64_t>()) % ST_SET, match = ST_WAY;
    tag_type partial_page{champsim::address{page}};
    uint8_t  ST_hit = 0;
    long sig_delta{0};

    if(SPP_DEBUG_PRINT) {
        fmt::print("[ST] read_and_update_sig page: {} partial_page: {}\n",page,partial_page);
    }

    // Case 1: Hit
    for (match = 0; match < ST_WAY; match++) {
        if (valid[set][match] && (tag[set][match] == partial_page)) {
            last_sig = sig[set][match];
            delta = champsim::offset(last_offset[set][match],page_offset);

            if (delta) {
                // Build a new sig based on 7-bit sign magnitude representation of delta
                //sig_delta = (delta < 0) ? ((((-1) * delta) & 0x3F) + 0x40) : delta;
                sig_delta = (delta < 0) ? (((-1) * delta) + (1 << (SIG_DELTA_BIT - 1))) : delta;
                sig[set][match] = ((last_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
                curr_sig = sig[set][match];
                last_offset[set][match] = page_offset;

                if(SPP_DEBUG_PRINT) {
                    fmt::print("[ST] read_and_update_sig hit set: {} way: {}",set,match);
                    fmt::print(" valid: {} tag: {}",valid[set][match],tag[set][match]);
                    fmt::print(" last_sig: {} curr_sig: {}",last_sig,curr_sig);
                    fmt::print(" delta: {} last_offset: {}",delta,page_offset);
                }
            } else last_sig = 0; // Hitting the same cache line, delta is zero

            ST_hit = 1;
            break;
        }
    }

    // Case 2: Invalid
    if (match == ST_WAY) {
        for (match = 0; match < ST_WAY; match++) {
            if (valid[set][match] == 0) {
                valid[set][match] = 1;
                tag[set][match] = partial_page;
                sig[set][match] = 0;
                curr_sig = sig[set][match];
                last_offset[set][match] = page_offset;

                if(SPP_DEBUG_PRINT) {
                    fmt::print("[ST] read_and_update_sig invalid set: {} way: {}",set,match);
                    fmt::print(" valid: {} tag: {0:x}",valid[set][match],partial_page);
                    fmt::print(" sig: {0:x} last_offset: {}]\n",sig[set][match],page_offset);
                }

                break;
            }
        }
    }

    // Case 3: Miss
    if (match == ST_WAY) {
        for (match = 0; match < ST_WAY; match++) {
            if (lru[set][match] == ST_WAY - 1) { // Find replacement victim
                tag[set][match] = partial_page;
                sig[set][match] = 0;
                curr_sig = sig[set][match];
                last_offset[set][match] = page_offset;

                if(SPP_DEBUG_PRINT) {
                    fmt::print("[ST] read_and_update_sig miss set: {} way: {}",set,match);
                    fmt::print(" valid: {} victim tag: {} new tag: {}",valid[set][match],tag[set][match],partial_page);
                    fmt::print(" sig: {} last_offset: {}\n",sig[set][match],page_offset);
                }

                break;
            }
        }

        if(SPP_SANITY_CHECK) {
            // Assertion
            if (match == ST_WAY) {
                fmt::print("[ST] Cannot find a replacement victim!\n");
                assert(0);
            }
        }
    }

    if(GHR_ON) {
        if (ST_hit == 0) {
            uint32_t GHR_found = parent_->GHR.check_entry(page_offset);
            if (GHR_found < MAX_GHR_ENTRY) {
                sig_delta = (parent_->GHR.delta[GHR_found] < 0) ? (((-1) * parent_->GHR.delta[GHR_found]) + (1 << (SIG_DELTA_BIT - 1))) : parent_->GHR.delta[GHR_found];
                sig[set][match] = ((parent_->GHR.sig[GHR_found] << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
                curr_sig = sig[set][match];
            }
        }
    }

    // Update LRU
    for (uint32_t way = 0; way < ST_WAY; way++) {
        if (lru[set][way] < lru[set][match]) {
            lru[set][way]++;

            if(SPP_SANITY_CHECK) {
                // Assertion
                if (lru[set][way] >= ST_WAY) {
                    fmt::print("[ST] LRU value is wrong! set: {} way: {} lru: {}\n",set,way,lru[set][way]);
                    assert(0);
                }
            }
        }
    }
    lru[set][match] = 0; // Promote to the MRU position
}

void spp_ppf_4kb::PATTERN_TABLE::update_pattern(uint32_t last_sig, typename offset_type::difference_type curr_delta)
{
    // Update (sig, delta) correlation
    uint32_t set = spp_ppf_4kb::get_hash(last_sig) % PT_SET,
             match = 0;

    // Case 1: Hit
    for (match = 0; match < PT_WAY; match++) {
        if (delta[set][match] == curr_delta) {
            c_delta[set][match]++;
            c_sig[set]++;
            if (c_sig[set] > C_SIG_MAX) {
                for (uint32_t way = 0; way < PT_WAY; way++)
                    c_delta[set][way] >>= 1;
                c_sig[set] >>= 1;
            }

            if(SPP_DEBUG_PRINT) {
                std::cout << "[PT] " << __func__ << " hit sig: " << std::hex << last_sig << std::dec << " set: " << set << " way: " << match;
                std::cout << " delta: " << delta[set][match] << " c_delta: " << c_delta[set][match] << " c_sig: " << c_sig[set] << std::endl;
            }

            break;
        }
    }

    // Case 2: Miss
    if (match == PT_WAY) {
        uint32_t victim_way = PT_WAY,
                 min_counter = C_SIG_MAX;

        for (match = 0; match < PT_WAY; match++) {
            if (c_delta[set][match] < min_counter) { // Select an entry with the minimum c_delta
                victim_way = match;
                min_counter = c_delta[set][match];
            }
        }

        delta[set][victim_way] = curr_delta;
        c_delta[set][victim_way] = 0;
        c_sig[set]++;
        if (c_sig[set] > C_SIG_MAX) {
            for (uint32_t way = 0; way < PT_WAY; way++)
                c_delta[set][way] >>= 1;
            c_sig[set] >>= 1;
        }

        if(SPP_DEBUG_PRINT) {
            std::cout << "[PT] " << __func__ << " miss sig: " << std::hex << last_sig << std::dec << " set: " << set << " way: " << victim_way;
            std::cout << " delta: " << delta[set][victim_way] << " c_delta: " << c_delta[set][victim_way] << " c_sig: " << c_sig[set] << std::endl;
        }

        if(SPP_SANITY_CHECK) {
            // Assertion
            if (victim_way == PT_WAY) {
                std::cout << "[PT] Cannot find a replacement victim!" << std::endl;
                assert(0);
            }
        }
    }
}

void spp_ppf_4kb::PATTERN_TABLE::read_pattern(uint32_t curr_sig, std::vector<typename offset_type::difference_type>& delta_q, std::vector<uint32_t>& confidence_q, std::vector<int32_t>& perc_sum_q, uint32_t &lookahead_way, uint32_t &lookahead_conf, uint32_t &pf_q_tail, uint32_t &depth, champsim::address addr, champsim::address base_addr, champsim::address train_addr, champsim::address curr_ip, typename offset_type::difference_type train_delta, uint32_t last_sig, uint32_t pq_occupancy, uint32_t pq_SIZE, uint32_t mshr_occupancy, uint32_t mshr_SIZE)
{
    // Update (sig, delta) correlation
    uint32_t set = spp_ppf_4kb::get_hash(curr_sig) % PT_SET,
             local_conf = 0,
             pf_conf = 0,
             max_conf = 0;
	
	bool found_candidate = false;

    if (c_sig[set]) {

        for (uint32_t way = 0; way < PT_WAY; way++) {

            local_conf = (100 * c_delta[set][way]) / c_sig[set];
            pf_conf = depth ? (parent_->GHR.global_accuracy * c_delta[set][way] / c_sig[set] * lookahead_conf / 100) : local_conf;

			int32_t perc_sum = parent_->PERC.perc_predict(train_addr, curr_ip, parent_->GHR.ip_1, parent_->GHR.ip_2, parent_->GHR.ip_3, train_delta + delta[set][way], last_sig, curr_sig, pf_conf, depth);
			bool do_pf = (perc_sum >= PERC_THRESHOLD_LO) ? 1 : 0;
			bool fill_l2 = (perc_sum >= PERC_THRESHOLD_HI) ? 1 : 0;

			if (fill_l2 && (mshr_occupancy >= mshr_SIZE || pq_occupancy >= pq_SIZE) )
				continue;

			// Now checking against the L2C_MSHR_SIZE
			// Saving some slots in the internal PF queue by checking against do_pf
            if (pf_conf && do_pf && pf_q_tail < 100 ) {

				confidence_q[pf_q_tail] = pf_conf;
            	delta_q[pf_q_tail] = delta[set][way];
				perc_sum_q[pf_q_tail] = perc_sum;

				//std::cout << "WAY:  "<< way << "\tPF_CONF: " << pf_conf <<  "\tIndex: " << pf_q_tail << std::endl;
				if(SPP_DEBUG_PRINT) {
					std::cout << "[PT] State of Features: \nTrain addr: " << train_addr << "\tCurr IP: " << curr_ip << "\tIP_1: " << parent_->GHR.ip_1 << "\tIP_2: " << parent_->GHR.ip_2 << "\tIP_3: " << parent_->GHR.ip_3 << "\tDelta: " << train_delta + delta[set][way] << "\tLastSig: " << last_sig << "\tCurrSig: " << curr_sig << "\tConf: " << pf_conf << "\tDepth: " << depth << "\tSUM: "<< perc_sum  << std::endl;
                }
            	// Lookahead path follows the most confident entry
            	if (pf_conf > max_conf) {
            	    lookahead_way = way;
            	    max_conf = pf_conf;
            	}
            	pf_q_tail++;
				found_candidate = true;
                
                if(SPP_DEBUG_PRINT) {
                    std::cout << "[PT] " << __func__ << " HIGH CONF: " << pf_conf << " sig: " << std::hex << curr_sig << std::dec << " set: " << set << " way: " << way;
                    std::cout << " delta: " << delta[set][way] << " c_delta: " << c_delta[set][way] << " c_sig: " << c_sig[set];
                    std::cout << " conf: " << local_conf << " pf_q_tail: " << (pf_q_tail-1) << " depth: " << depth << std::endl;
                }
            } else {
                if(SPP_DEBUG_PRINT) {
                    std::cout << "[PT] " << __func__ << "  LOW CONF: " << pf_conf << " sig: " << std::hex << curr_sig << std::dec << " set: " << set << " way: " << way;
                    std::cout << " delta: " << delta[set][way] << " c_delta: " << c_delta[set][way] << " c_sig: " << c_sig[set];
                    std::cout << " conf: " << local_conf << " pf_q_tail: " << (pf_q_tail) << " depth: " << depth << std::endl;
                }
            }

			// Recording Perc negatives
            if (pf_conf && pf_q_tail < parent_->intern_->get_mshr_size() && (perc_sum < PERC_THRESHOLD_HI) ) {
				// Note: Using PERC_THRESHOLD_HI as the decising factor for negative case
				// Because 'trueness' of a prefetch is decisded based on the feedback from L2C
				// So even though LLC prefetches go through, they are treated as false wrt L2C in this case
				champsim::address pf_addr{champsim::block_number{champsim::address{base_addr}} + delta[set][way]};
    			
                if (champsim::page_number{champsim::address{addr}} == champsim::page_number{pf_addr}) { // Prefetch request is in the same physical page
                	parent_->FILTER.check(pf_addr, train_addr, curr_ip, SPP_PERC_REJECT, train_delta + delta[set][way], last_sig, curr_sig, pf_conf, perc_sum, depth);
					parent_->GHR.perc_reject++;
				}
			}
        }
        lookahead_conf = max_conf;
        if (found_candidate) depth++;

        if(SPP_DEBUG_PRINT)
            std::cout << "global_accuracy: " << parent_->GHR.global_accuracy << " lookahead_conf: " << lookahead_conf << std::endl;
    } else confidence_q[pf_q_tail] = 0;
}

bool spp_ppf_4kb::PREFETCH_FILTER::check(champsim::address check_addr, champsim::address base_addr, champsim::address ip, FILTER_REQUEST filter_request, typename offset_type::difference_type cur_delta, uint32_t last_sig, uint32_t curr_sig, uint32_t conf, int32_t sum, uint32_t depth)
{
    champsim::block_number cache_line{check_addr};
    uint64_t hash = spp_ppf_4kb::get_hash(cache_line.to<uint64_t>());
	
	//MAIN FILTER
	uint64_t quotient = (hash >> REMAINDER_BIT) & ((1 << QUOTIENT_BIT) - 1),
             remainder = hash % (1 << REMAINDER_BIT);
	
	//REJECT FILTER
	uint64_t quotient_reject = (hash >> REMAINDER_BIT_REJ) & ((1 << QUOTIENT_BIT_REJ) - 1),
             remainder_reject = hash % (1 << REMAINDER_BIT_REJ);

    if(SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] check_addr: " << std::hex << check_addr << " check_cache_line: " << champsim::block_number{check_addr};
		std::cout << " request type: " << filter_request;
        std::cout << " hash: " << hash << std::dec << " quotient: " << quotient << " remainder: " << remainder << std::endl;
    }

    switch (filter_request) {
		
		case SPP_PERC_REJECT: // To see what would have been the prediction given perceptron has rejected the PF
            if ((valid[quotient] || useful[quotient]) && remainder_tag[quotient] == remainder) { 
				// We want to check if the prefetch would have gone through had perc not rejected
				// So even in perc reject case, I'm checking in the accept filter for redundancy
                if(SPP_DEBUG_PRINT) {
                    std::cout << "[FILTER] " << __func__ << " line is already in the filter check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
                    std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl; 
                }
                return false; // False return indicates "Do not prefetch"
            } else {
				if (train_neg) {
					valid_reject[quotient_reject] = 1;
					remainder_tag_reject[quotient_reject] = remainder_reject;

					// Logging perc features
					address_reject[quotient_reject] = base_addr;
					pc_reject[quotient_reject] = ip;
					pc_1_reject[quotient_reject] = parent_->GHR.ip_1;
					pc_2_reject[quotient_reject] = parent_->GHR.ip_2;
					pc_3_reject[quotient_reject] = parent_->GHR.ip_3;
					delta_reject[quotient_reject] = cur_delta;
					perc_sum_reject[quotient_reject] = sum;
					last_signature_reject[quotient_reject] = last_sig;
					cur_signature_reject[quotient_reject] = curr_sig;
					confidence_reject[quotient_reject] = conf;
					la_depth_reject[quotient_reject] = depth;
				}

                if(SPP_DEBUG_PRINT) {
                    std::cout << "[FILTER] " << __func__ << " PF rejected by perceptron. Set valid_reject for check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
                    std::cout << " quotient: " << quotient << " remainder_tag: " << remainder_tag_reject[quotient_reject] << std::endl; 
					std::cout << " More Recorded Metadata: Addr: " << std::hex << address_reject[quotient_reject] << std::dec << " PC: " << pc_reject[quotient_reject] << " Delta: " << delta_reject[quotient_reject] << " Last Signature: " << last_signature_reject[quotient_reject] << " Current Signature: " << cur_signature_reject[quotient_reject] << " Confidence: " << confidence_reject[quotient_reject] << std::endl;
                }
			}
			break;
		
		case SPP_L2C_PREFETCH:
            if ((valid[quotient] || useful[quotient]) && remainder_tag[quotient] == remainder) { 
                if(SPP_DEBUG_PRINT) {
                    std::cout << "[FILTER] " << __func__ << " line is already in the filter check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
                    std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl; 
                }

                return false; // False return indicates "Do not prefetch"
            } else {

                valid[quotient] = 1;  // Mark as prefetched
                useful[quotient] = 0; // Reset useful bit
                remainder_tag[quotient] = remainder;

				// Logging perc features
				delta[quotient] = cur_delta;
				pc[quotient] = ip;
				pc_1[quotient] = parent_->GHR.ip_1;
				pc_2[quotient] = parent_->GHR.ip_2;
				pc_3[quotient] = parent_->GHR.ip_3;
				last_signature[quotient] = last_sig; 
				cur_signature[quotient] = curr_sig;
				confidence[quotient] = conf;
				address[quotient] = base_addr; 
				perc_sum[quotient] = sum;
				la_depth[quotient] = depth;
				
                if(SPP_DEBUG_PRINT) {
                    std::cout << "[FILTER] " << __func__ << " set valid for check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
                    std::cout << " quotient: " << quotient << " remainder_tag: " << remainder_tag[quotient] << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl; 
					std::cout << " More Recorded Metadata: Addr:" << std::hex << address[quotient] << std::dec << " PC: " << pc[quotient] << " Delta: " << delta[quotient] << " Last Signature: " << last_signature[quotient] << " Current Signature: " << cur_signature[quotient] << " Confidence: " << confidence[quotient] << std::endl;
                }
            }
            break;

        case SPP_LLC_PREFETCH:
            if ((valid[quotient] || useful[quotient]) && remainder_tag[quotient] == remainder) { 
                if(SPP_DEBUG_PRINT) {
                    std::cout << "[FILTER] " << __func__ << " line is already in the filter check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
                    std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl; 
                }

                return false; // False return indicates "Do not prefetch"
            } else {
                // NOTE: SPP_LLC_PREFETCH has relatively low confidence 
                // Therefore, it is safe to prefetch this cache line in the large LLC and save precious L2C capacity
                // If this prefetch request becomes more confident and SPP eventually issues SPP_L2C_PREFETCH,
                // we can get this cache line immediately from the LLC (not from DRAM)
                // To allow this fast prefetch from LLC, SPP does not set the valid bit for SPP_LLC_PREFETCH
				
				if(SPP_DEBUG_PRINT) {
                    std::cout << "[FILTER] " << __func__ << " don't set valid for check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
                    std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl; 
                }
            }
            break;

        case L2C_DEMAND:
            if ((remainder_tag[quotient] == remainder) && (useful[quotient] == 0)) {
                useful[quotient] = 1;
                if (valid[quotient]) {
					parent_->GHR.pf_useful++; // This cache line was prefetched by SPP and actually used in the program
					// For stats
					parent_->GHR.pf_l2c_good++;
				}

                if(SPP_DEBUG_PRINT) {
                    std::cout << "[FILTER] " << __func__ << " set useful for check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
                    std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient];
                    std::cout << " GHR.pf_issued: " << parent_->GHR.pf_issued << " GHR.pf_useful: " << parent_->GHR.pf_useful << std::endl; 
					if (valid[quotient])
						std::cout << " Calling Perceptron Update (INC) as L2C_DEMAND was useful" << std::endl;
                }

                if (valid[quotient]) {
					// Prefetch leads to a demand hit
					parent_->PERC.perc_update(address[quotient], pc[quotient], pc_1[quotient], pc_2[quotient], pc_3[quotient], delta[quotient], last_signature[quotient], cur_signature[quotient], confidence[quotient], la_depth[quotient], 1, perc_sum[quotient]);

					// Histogramming Idea
					int32_t perc_sum_shifted = perc_sum[quotient] + (PERC_COUNTER_MAX+1)*PERC_FEATURES; 
					int32_t hist_index = perc_sum_shifted / 10;
			        hist_hits[hist_index]++;
				}
            }
			//If NOT Prefetched
			if (!(valid[quotient] && remainder_tag[quotient] == remainder)) {
				// AND If Rejected by Perc
				if (valid_reject[quotient_reject] && remainder_tag_reject[quotient_reject] == remainder_reject) {
               		 if(SPP_DEBUG_PRINT) {
               		     std::cout << "[FILTER] " << __func__ << " not doing anything for check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
               		     std::cout << " quotient: " << quotient << " valid_reject:" << valid_reject[quotient_reject];
               		     std::cout << " GHR.pf_issued: " << parent_->GHR.pf_issued << " GHR.pf_useful: " << parent_->GHR.pf_useful << std::endl; 
			   		 	 std::cout << " Calling Perceptron Update (DEC) as a useful L2C_DEMAND was rejected and reseting valid_reject" << std::endl;
                     }
					if (train_neg) {
						// Not prefetched but could have been a good idea to prefetch
						parent_->PERC.perc_update(address_reject[quotient_reject], pc_reject[quotient_reject], pc_1_reject[quotient_reject], pc_2_reject[quotient_reject], pc_3_reject[quotient_reject], delta_reject[quotient_reject], last_signature_reject[quotient_reject], cur_signature_reject[quotient_reject], confidence_reject[quotient_reject], la_depth_reject[quotient_reject], 0, perc_sum_reject[quotient_reject]);
						valid_reject[quotient_reject] = 0;
						remainder_tag_reject[quotient_reject] = 0;
						// Printing Stats
						parent_->GHR.reject_update++;
					}
				}
			}
            break;

        case L2C_EVICT:
            // Decrease global pf_useful counter when there is a useless prefetch (prefetched but not used)
            if (valid[quotient] && !useful[quotient]) {
				if (parent_->GHR.pf_useful) 
					parent_->GHR.pf_useful--;
				
				if(SPP_DEBUG_PRINT) {
               		std::cout << "[FILTER] " << __func__ << " eviction for check_addr: " << std::hex << check_addr << " cache_line: " << cache_line << std::dec;
               		std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl; 
					std::cout << " Calling Perceptron Update (DEC) as L2C_DEMAND was not useful" << std::endl;
					std::cout << " Reseting valid_reject" << std::endl;
                }

				// Prefetch leads to eviction
				parent_->PERC.perc_update(address[quotient], pc[quotient], pc_1[quotient], pc_2[quotient], pc_3[quotient], delta[quotient], last_signature[quotient], cur_signature[quotient], confidence[quotient], la_depth[quotient], 0, perc_sum[quotient]);
			}
            // Reset filter entry
            valid[quotient] = 0;
            useful[quotient] = 0;
            remainder_tag[quotient] = 0;

			// Reset reject filter too
			valid_reject[quotient_reject] = 0;
			remainder_tag_reject[quotient_reject] = 0;

            break;

        default:
            // Assertion
            std::cout << "[FILTER] Invalid filter request type: " << filter_request << std::endl;
            assert(0);
    }

    return true;
}

void spp_ppf_4kb::GLOBAL_REGISTER::update_entry(uint32_t pf_sig, uint32_t pf_confidence, offset_type pf_offset, typename offset_type::difference_type pf_delta) 
{
    // NOTE: GHR implementation is slightly different from the original paper
    // Instead of matching (last_offset + delta), GHR simply stores and matches the pf_offset
    uint32_t min_conf = 100,
             victim_way = MAX_GHR_ENTRY;

    if(SPP_DEBUG_PRINT) {
        std::cout << "[GHR] Crossing the page boundary pf_sig: " << std::hex << pf_sig << std::dec;
        std::cout << " confidence: " << pf_confidence << " pf_offset: " << pf_offset << " pf_delta: " << pf_delta << std::endl;
    }

    for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
        //if (sig[i] == pf_sig) { // TODO: Which one is better and consistent?
            // If GHR already holds the same pf_sig, update the GHR entry with the latest info
        if (valid[i] && (offset[i] == pf_offset)) {
            // If GHR already holds the same pf_offset, update the GHR entry with the latest info
            sig[i] = pf_sig;
            confidence[i] = pf_confidence;
            //offset[i] = pf_offset;
            delta[i] = pf_delta;

            if(SPP_DEBUG_PRINT) std::cout << "[GHR] Found a matching index: " << i << std::endl;

            return;
        }

        // GHR replacement policy is based on the stored confidence value
        // An entry with the lowest confidence is selected as a victim
        if (confidence[i] < min_conf) {
            min_conf = confidence[i];
            victim_way = i;
        }
    }

    // Assertion
    if (victim_way >= MAX_GHR_ENTRY) {
        std::cout << "[GHR] Cannot find a replacement victim!" << std::endl;
        assert(0);
    }

    if(SPP_DEBUG_PRINT) {
        std::cout << "[GHR] Replace index: " << victim_way << " pf_sig: " << std::hex << sig[victim_way] << std::dec;
        std::cout << " confidence: " << confidence[victim_way] << " pf_offset: " << offset[victim_way] << " pf_delta: " << delta[victim_way] << std::endl;
    }

    valid[victim_way] = 1;
    sig[victim_way] = pf_sig;
    confidence[victim_way] = pf_confidence;
    offset[victim_way] = pf_offset;
    delta[victim_way] = pf_delta;
}

uint32_t spp_ppf_4kb::GLOBAL_REGISTER::check_entry(offset_type page_offset)
{
    uint32_t max_conf = 0,
             max_conf_way = MAX_GHR_ENTRY;

    for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
        if ((offset[i] == page_offset) && (max_conf < confidence[i])) {
            max_conf = confidence[i];
            max_conf_way = i;
        }
    }

    return max_conf_way;
}

void spp_ppf_4kb::get_perc_index(champsim::address base_addr, champsim::address ip, champsim::address ip_1, champsim::address ip_2, champsim::address ip_3, typename offset_type::difference_type cur_delta, uint32_t last_sig, uint32_t curr_sig, uint32_t confidence, uint32_t depth, uint64_t* perc_set)
{
	// Returns the imdexes for the perceptron tables
    champsim::block_number cache_line{base_addr};
    page_type page_addr{base_addr};

	int sig_delta = (cur_delta < 0) ? (((-1) * cur_delta) + (1 << (SIG_DELTA_BIT - 1))) : cur_delta;
	uint64_t  pre_hash[PERC_FEATURES];

	pre_hash[0] = base_addr.to<uint64_t>();
	pre_hash[1] = cache_line.to<uint64_t>();
	pre_hash[2] = page_addr.to<uint64_t>();
	pre_hash[3] = confidence ^ page_addr.to<uint64_t>();
	pre_hash[4] = curr_sig ^ sig_delta;
	pre_hash[5] = ip_1.to<uint64_t>() ^ (ip_2.to<uint64_t>()>>1) ^ (ip_3.to<uint64_t>()>>2);
	pre_hash[6] = ip.to<uint64_t>() ^ depth;
	pre_hash[7] = ip.to<uint64_t>() ^ sig_delta;
	pre_hash[8] = confidence;

	for (int i = 0; i < PERC_FEATURES; i++) {
		perc_set[i] = (pre_hash[i]) % PERC.PERC_DEPTH[i]; // Variable depths
		if(SPP_DEBUG_PRINT) std::cout << "  Perceptron Set Index#: " << i << " = " <<  perc_set[i];
	}
	if(SPP_DEBUG_PRINT) std::cout << std::endl;	
}

int32_t	spp_ppf_4kb::PERCEPTRON::perc_predict(champsim::address base_addr, champsim::address ip, champsim::address ip_1, champsim::address ip_2, champsim::address ip_3, typename offset_type::difference_type cur_delta, uint32_t last_sig, uint32_t curr_sig, uint32_t confidence, uint32_t depth)
{
	if(SPP_DEBUG_PRINT) {
		int sig_delta = (cur_delta < 0) ? (((-1) * cur_delta) + (1 << (SIG_DELTA_BIT - 1))) : cur_delta;
		std::cout << "[PERC_PRED] Current IP: " << ip << "  and  Memory Adress: " << std::hex << base_addr << std::endl;
		std::cout << " Last Sig: " << last_sig << " Curr Sig: " << curr_sig << std::dec << std::endl;
		std::cout << " Cur Delta: " << cur_delta << " Sign Delta: " << sig_delta << " Confidence: " << confidence<< std::endl;
		std::cout << " ";
    }

	uint64_t perc_set[PERC_FEATURES];
	// Get the indexes in perc_set[]
	parent_->get_perc_index(base_addr, ip, ip_1, ip_2, ip_3, cur_delta, last_sig, curr_sig, confidence, depth, perc_set);
	
	int32_t sum = 0;
	for (int i = 0; i < PERC_FEATURES; i++) {
		sum += perc_weights[perc_set[i]][i];	
		// Calculate Sum
	}
	if(SPP_DEBUG_PRINT) std::cout << " Sum of perceptrons: " << sum << " Prediction made: " << ((sum >= PERC_THRESHOLD_LO) ? (sum >= PERC_THRESHOLD_HI) : false)  << std::endl;
	// Return the sum
	return sum;
}

void spp_ppf_4kb::PERCEPTRON::perc_update(champsim::address base_addr, champsim::address ip, champsim::address ip_1, champsim::address ip_2, champsim::address ip_3, typename offset_type::difference_type cur_delta, uint32_t last_sig, uint32_t curr_sig, uint32_t confidence, uint32_t depth, bool direction, int32_t perc_sum)
{
	if(SPP_DEBUG_PRINT) {
		int sig_delta = (cur_delta < 0) ? (((-1) * cur_delta) + (1 << (SIG_DELTA_BIT - 1))) : cur_delta;
		std::cout << "[PERC_UPD] (Recorded) IP: " << ip << "  and  Memory Adress: " << std::hex << base_addr << std::endl;
		std::cout << " Last Sig: " << last_sig << " Curr Sig: " << curr_sig << std::dec << std::endl;
		std::cout << " Cur Delta: " << cur_delta << " Sign Delta: " << sig_delta << " Confidence: "<< confidence << " Update Direction: " << direction << std::endl;
		std::cout << " ";
    }

	uint64_t perc_set[PERC_FEATURES];
	// Get the perceptron indexes
	parent_->get_perc_index(base_addr, ip, ip_1, ip_2, ip_3, cur_delta, last_sig, curr_sig, confidence, depth, perc_set);
	
	int32_t sum = 0;
	for (int i = 0; i < PERC_FEATURES; i++) {
		// Marking the weights as touched for final dumping in the csv
		perc_touched[perc_set[i]][i] = 1;	
	}
	// Restore the sum that led to the prediction
	sum = perc_sum;
	
	if (!direction) { // direction = 1 means the sum was in the correct direction, 0 means it was in the wrong direction
		// Prediction wrong
		for (int i = 0; i < PERC_FEATURES; i++) {
			if (sum >= PERC_THRESHOLD_HI) {
				// Prediction was to prefectch -- so decrement counters
				if (perc_weights[perc_set[i]][i] > -1*(PERC_COUNTER_MAX+1) )
					perc_weights[perc_set[i]][i]--;
			}
			if (sum < PERC_THRESHOLD_HI) {
				// Prediction was to not prefetch -- so increment counters
				if (perc_weights[perc_set[i]][i] < PERC_COUNTER_MAX)
					perc_weights[perc_set[i]][i]++;
			}
		}
		if(SPP_DEBUG_PRINT) {
			int differential = (sum >= PERC_THRESHOLD_HI) ? -1 : 1;
			std::cout << " Direction is: " << direction << " and sum is:" << sum;
			std::cout << " Overall Differential: " << differential << std::endl;
        }
	}
	if (direction && sum > NEG_UPDT_THRESHOLD && sum < POS_UPDT_THRESHOLD) {
		// Prediction correct but sum not 'saturated' enough
		for (int i = 0; i < PERC_FEATURES; i++) {
			if (sum >= PERC_THRESHOLD_HI) {
				// Prediction was to prefetch -- so increment counters
				if (perc_weights[perc_set[i]][i] < PERC_COUNTER_MAX)
					perc_weights[perc_set[i]][i]++;
			}
			if (sum < PERC_THRESHOLD_HI) {
				// Prediction was to not prefetch -- so decrement counters
				if (perc_weights[perc_set[i]][i] > -1*(PERC_COUNTER_MAX+1) )
					perc_weights[perc_set[i]][i]--;
			}
		}
		if(SPP_DEBUG_PRINT) {
			int differential = 0;
			if (sum >= PERC_THRESHOLD_HI) differential =  1;
			if (sum  < PERC_THRESHOLD_HI) differential = -1;
			std::cout << " Direction is: " << direction << " and sum is:" << sum;
			std::cout << " Overall Differential: " << differential << std::endl;
        }
	}
}
