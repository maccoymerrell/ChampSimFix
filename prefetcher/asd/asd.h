//Title: Feedback Mechanisms for Improving Probabilistic Memory Prefetching
//Authors: Ibrahim Hur, Calvin Lin
//Institutions: IBM Corporation, The University of Texas at Austin
//Publisher: IEEE, HPCA
//Date: 02/14/2009

//I. Hur and C. Lin, "Feedback mechanisms for improving probabilistic memory prefetching," 2009 IEEE 15th International Symposium on High Performance Computer Architecture, Raleigh, NC, USA, 2009, pp. 443-454, doi: 10.1109/HPCA.2009.4798282.
//keywords: {Feedback;Prefetching;Variable speed drives;Random access memory;Energy consumption;Histograms;Timing;Computer applications},


#ifndef PREFETCHER_ASD_H
#define PREFETCHER_ASD_H

#include <cstdint>

#include "address.h"
#include "modules.h"
#include "cache.h"
#include <map>
#include <vector>
#include <array>
#include <utility>

struct asd : public champsim::modules::prefetcher {

  static constexpr bool FORCE_4KiB_PAGES = true;
  static constexpr std::size_t MAX_PREFETCH = 22;
  static constexpr std::size_t MAX_STREAMS = 32;
  static constexpr std::size_t MAX_STRIDE = 8;
  static constexpr std::size_t AGE = 64;
  static constexpr uint64_t EPOCH_MAX = 8192;
  static constexpr uint64_t EPOCH_MIN = 256;
  static constexpr double DIFF_THRESH = 0.1;

  static constexpr std::size_t PM_SETS = 32;
  static constexpr std::size_t PM_WAYS = 4;
  static constexpr std::size_t PAGE_MAP_SIZE = 64;
  
  struct Histogram {
    std::vector<uint64_t> hist;
    uint64_t global_count = 0;

    double histogram_factor = 2.0; //can vary between 1 and n to set aggression (1 is 100% chance, 2 is 50%, 3 is 33%, and so on)

    Histogram(std::size_t bins) {
      for(auto i = 0; i < bins; i++)
        hist.push_back(0);
    }

    void tally(std::size_t b_pos, std::size_t stride) {
      assert(b_pos != 0);
      if(b_pos > hist.size())
        return;
      //hist.at(b_pos - 1) += (b_pos / stride);
      hist.at(b_pos - 1)++;
      global_count += b_pos;
    }

    double get_bin_prob(std::size_t b_pos) {
      assert(b_pos != 0);
      if(b_pos > hist.size())
        return 0.0;
      return (hist.at(b_pos - 1) / (double)global_count);
    }

    std::size_t get_bin_occu(std::size_t b_pos) {
      assert(b_pos != 0);
      if(b_pos > hist.size())
        return 0;
      return hist.at(b_pos-1);
    }

    std::size_t get_depth_from(std::size_t b_pos) {
      assert(b_pos != 0);

      std::size_t depth = 0;
      while((b_pos + depth) < hist.size() && hist.at(b_pos-1) < histogram_factor * hist.at(b_pos + depth - 1))
        depth++;
      
      return depth;
    }

    void clear() {
      for(int i = 0; i < hist.size(); i++)
        hist.at(i) = 0;
      global_count = 0;
    }

    double compare(Histogram& H) {
      assert(H.hist.size() == hist.size());
      double normal_factor = (H.global_count == 0 || global_count == 0) ? 1.0 : H.global_count / global_count;
      double diff = 0;
      for(std::size_t i = 0; i < hist.size(); i++) {
        diff += std::abs((double)H.hist.at(i) - (double)(hist.at(i)*normal_factor));
      }
      if(global_count == 0 && H.global_count == 0)
        return 0.0;
      else if (global_count == 0 || H.global_count == 0)
        return 1.0;
      return diff / H.global_count;
    }


  };

  struct StateMachine {
    enum States {
      SAME1,
      SAME2,
      HALF1,
      HALF2,
      DOUBLE1,
      DOUBLE2
    };

    States current_state = States::SAME1;

    uint64_t update_state(bool diff, uint64_t epoch) {
      switch(current_state) {
        case SAME1: {
          if(diff) {
            current_state = States::DOUBLE1;
            epoch = epoch << 1;
          } else {
            epoch = epoch;
            current_state = States::SAME1;
          }
          break;
        }
        case SAME2: {
          if(diff) {
            current_state = States::HALF1;
            epoch = epoch >> 1;
          } else {
            current_state = States::SAME2;
            epoch = epoch;
          }
          break;
        }
        case HALF1: {
          if(diff) {
            current_state = States::DOUBLE1;
            epoch = epoch << 1;
          } else {
            current_state = States::HALF2;
            epoch = epoch >> 1;
          }
          break;
        }
        case HALF2: {
          if(diff) {
            current_state = States::DOUBLE1;
            epoch = epoch << 1;
          } else {
            current_state = States::SAME1;
            epoch = epoch;
          }
          break;
        }
        case DOUBLE1: {
          if(diff) {
            current_state = States::HALF1;
            epoch = epoch >> 1;
          } else {
            current_state = States::DOUBLE2;
            epoch = epoch << 1;
          }
          break;
        }
        case DOUBLE2: {
          if(diff) {
            current_state = States::HALF1;
            epoch = epoch >> 1;
          } else {
            current_state = States::SAME2;
            epoch = epoch;
          }
          break;
        }
      }
      return epoch;
    }
  };

  struct Streamer {
    uint64_t age = 0;
    champsim::block_number base{};
    std::size_t depth = 0;
    //add stride?
    std::size_t stride = 0;
  };

  template<std::size_t max_streams, uint64_t age_factor>
  struct StreamBuffer {
    std::vector<Streamer> streams;

    std::pair<std::size_t,std::size_t> check_stream(champsim::address addr) {
      //search for match
      //if no match, check size,
      std::size_t victim = streams.size();
      for(int i = 0; i < streams.size(); i++) {
        streams.at(i).age++;
        //if next location is above or equal to block number, and previous location is below or equal to block number
        if(streams.at(i).base + streams.at(i).depth + (MAX_STRIDE)  >= champsim::block_number{addr} && streams.at(i).base + (streams.at(i).depth) < champsim::block_number{addr}) {
          //if we crossed a page boundary
          if((champsim::address{streams.at(i).base}.to<uint64_t>() >> 12) != (addr.to<uint64_t>() >> 12) && FORCE_4KiB_PAGES) {
            //reset depth
            streams.at(i).depth = 1;
            //reset age
            streams.at(i).age = 0;
            //maintain stride
            streams.at(i).stride = streams.at(i).stride;
            //update base
            streams.at(i).base = champsim::block_number{addr};
            return std::pair<std::size_t,std::size_t>{1,streams.at(i).stride};
          }
          else if (champsim::page_number{champsim::address{streams.at(i).base}} != champsim::page_number{addr}) {
            //reset depth
            streams.at(i).depth = 1;
            //reset age
            streams.at(i).age = 0;
            //maintain stride
            streams.at(i).stride = streams.at(i).stride;
            //update base
            streams.at(i).base = champsim::block_number{addr};
            return std::pair<std::size_t,std::size_t>{1,streams.at(i).stride};
          }
          //update stride
          streams.at(i).stride = champsim::block_number{addr}.to<uint64_t>() - (streams.at(i).base.to<uint64_t>() + streams.at(i).depth);
          //update depth
          streams.at(i).depth += streams.at(i).stride;
          //reset age
          streams.at(i).age = 0;
          return std::pair{streams.at(i).depth,streams.at(i).stride};
        } else if (streams.at(i).age > (age_factor >> streams.at(i).depth)) {
          victim = i;
        }
      }
      //if we have space
      if(streams.size() < max_streams) {
        Streamer S;
        S.age = 0;
        S.base = champsim::block_number{addr};
        S.depth = 1;
        S.stride = 1;
        streams.push_back(S);
        return std::pair{1,1};
      }
      //we don't have space, check for victim
      else if(victim != streams.size()) {
        //set to default entry
        streams.at(victim).age = 0;
        streams.at(victim).base = champsim::block_number{addr};
        streams.at(victim).depth = 1;
        streams.at(victim).stride = 1;
        return std::pair{1,1};
      }
      //we don't have any space at all, return 0 (don't prefetch)
      return std::pair{0,0};
    }

  };

  //This is a small table to filter out recent prefetches, since this prefetcher prefetches over itself a lot
  //It fills the table with recent prefetches, and prevents them from being prefetched again until they are evicted
  //by other prefetches or time out
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

  

  struct ASD_Module {
    Histogram ActiveHist;
    Histogram BuildHist;
    uint64_t epoch = EPOCH_MAX;
    uint64_t epoch_timer = 0;
    StreamBuffer<MAX_STREAMS,AGE> Streams;
    StateMachine SM;
    std::vector<uint64_t> pf_depths;
    std::vector<uint64_t> pf_strides;
    std::size_t bins;

    uint64_t filtered_prefetches = 0;

    champsim::msl::lru_table<page_map,page_map_set,page_map_way> page_map_table{PM_SETS,PM_WAYS};

    ASD_Module(std::size_t bins_) : ActiveHist(bins_), BuildHist(bins_), bins(bins_) {
      for(int i = 0; i < bins_; i++) {
        pf_depths.push_back(0);
        pf_strides.push_back(0);
      }
    }

    void set_histogram_factor(double factor) {
      ActiveHist.histogram_factor = factor;
      BuildHist.histogram_factor = factor;
    }

    void add_to_pagemap(champsim::address addr);
    bool check_pagemap(champsim::address addr);
    void remove_from_pagemap(champsim::address addr);



    void increment_epoch() {
      if(epoch_timer == 0) {
        double diff = ActiveHist.compare(BuildHist);
        //fmt::print("Diff was: {}\n",diff);
        epoch = SM.update_state(diff > DIFF_THRESH,epoch);
        if(epoch > EPOCH_MAX)
          epoch = EPOCH_MAX;
        if(epoch < EPOCH_MIN)
          epoch = EPOCH_MIN;

        ActiveHist.hist = BuildHist.hist;
        ActiveHist.global_count = BuildHist.global_count;
        BuildHist.clear();

        epoch_timer = epoch;
        //fmt::print("Reset epoch timer to : {}\n",epoch_timer);
      }
      else {
        epoch_timer--;
      }
    }

    std::pair<std::size_t,std::size_t> get_prefetch_depth(champsim::address addr) {
      increment_epoch();
       auto [stream_pos, stride] = Streams.check_stream(addr);
      //fmt::print("Found stream from addr: {} pos: {}\n",addr,stream_pos);
      if(stream_pos != 0) {
        BuildHist.tally(stream_pos, stride);
        std::size_t depth = std::min(ActiveHist.get_depth_from(stream_pos),MAX_PREFETCH);
        //fmt::print("ASD advising depth of {}\n",depth);
        return std::pair<std::size_t,std::size_t>{depth,stride};
      }
      return std::pair<std::size_t,std::size_t>{0,0};
    }

    void print_stats() {
      fmt::print("\tASD Histogram, epoch: {}, filtered: {}\n",epoch, filtered_prefetches);
      for(int j = 0; j < bins; j++) {
        fmt::print("\t\t{} : {}\n",j + 1,ActiveHist.get_bin_occu(j+1));
      }
      fmt::print("ASD Prefetch Depths:\n");
    
      for(int j = 0; j < bins; j++) {
        fmt::print("\t\t{} : {}\n",j,pf_depths.at(j));
      }
    
      fmt::print("ASD Prefetch Strides:\n");
    
      for(int j = 0; j < bins; j++) {
        fmt::print("\t\t{} : {}\n",j,pf_strides.at(j));
      }
    }
  };

  std::size_t num_bins;
  std::vector<ASD_Module> ASD_Modules;

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint32_t cpu, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, uint32_t cpu, bool useless, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  void prefetcher_initialize();
  // void prefetcher_branch_operate(champsim::address ip, uint8_t branch_type, champsim::address branch_target) {}
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();
};

#endif
