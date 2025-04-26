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

  static constexpr std::size_t H_BINS = 64;
  static constexpr std::size_t MAX_PREFETCH = 22;
  static constexpr std::size_t MAX_STREAMS = 32;
  static constexpr std::size_t AGE = 128;
  static constexpr uint64_t EPOCH_MAX = 8192;
  static constexpr uint64_t EPOCH_MIN = 256;
  static constexpr uint64_t DIFF_THRESH = 0.3;
  
  template<std::size_t bins>
  struct Histogram {
    std::array<uint64_t,bins> hist;
    uint64_t global_count = 0;

    Histogram() {
      for(auto i = 0; i < bins; i++)
        hist.at(i) = 0;
    }

    void tally(std::size_t b_pos, std::size_t stride) {
      assert(b_pos != 0);
      if(b_pos > bins)
        return;
      //hist.at(b_pos - 1) += (b_pos / stride);
      hist.at(b_pos - 1)++;
      global_count += b_pos;
    }

    double get_bin_prob(std::size_t b_pos) {
      assert(b_pos != 0);
      if(b_pos > bins)
        return 0.0;
      return (hist.at(b_pos - 1) / (double)global_count);
    }

    std::size_t get_bin_occu(std::size_t b_pos) {
      assert(b_pos != 0);
      if(b_pos > bins)
        return 0;
      return hist.at(b_pos-1);
    }

    std::size_t get_depth_from(std::size_t b_pos) {
      assert(b_pos != 0);

      std::size_t depth = 0;
      while((b_pos + depth) < bins && hist.at(b_pos-1) < 2 * hist.at(b_pos + depth - 1))
        depth++;
      
      return depth;
    }

    void clear() {
      for(int i = 0; i < bins; i++)
        hist.at(i) = 0;
      global_count = 0;
    }

    double compare(Histogram& H) {
      assert(H.hist.size() == hist.size());
      double normal_factor = (H.global_count == 0 || global_count == 0) ? 1.0 : H.global_count / global_count;
      double diff = 0;
      for(std::size_t i = 0; i < bins; i++) {
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
        if(streams.at(i).base + streams.at(i).depth + (streams.at(i).stride * 2)  > champsim::block_number{addr} && streams.at(i).base + (streams.at(i).depth) < champsim::block_number{addr}) {
          //if we crossed a page boundary
          if((champsim::address{streams.at(i).base}.to<uint64_t>() >> 12) != (addr.to<uint64_t>() >> 12)) {
            streams.at(i).depth = 1;
            streams.at(i).age = 0;
            streams.at(i).stride = streams.at(i).stride;
            streams.at(i).base = champsim::block_number{addr};
            return std::pair{1,1};
          }
          streams.at(i).stride = champsim::block_number{addr}.to<uint64_t>() - (streams.at(i).base.to<uint64_t>() + streams.at(i).depth);
          //streams.at(i).stride = 1;
          streams.at(i).depth += streams.at(i).stride;
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

  struct RAF {
    constexpr static std::size_t RAF_FILTER_SETS = 16;
    constexpr static std::size_t RAF_FILTER_WAYS = 16;
    constexpr static std::size_t RAF_TIMEOUT = 300;
    struct raf_entry {
      champsim::block_number block;
      uint64_t first_accessed;

      raf_entry() : raf_entry(champsim::block_number{0},0) {}
      explicit raf_entry(champsim::block_number block_, uint64_t first_accessed_) : block(block_), first_accessed(first_accessed_) {}
    };
    struct raf_indexer {
      auto operator()(const raf_entry& entry) const {return entry.block;}
    };
    champsim::msl::lru_table<raf_entry, raf_indexer, raf_indexer> raf_filter{RAF_FILTER_SETS,RAF_FILTER_WAYS};
    bool check(champsim::address block, uint64_t check_time, bool update_table) {
      auto raf_filter_entry = raf_filter.check_hit(raf_entry{champsim::block_number{block},0});
      bool should_drop = false;
      if(raf_filter_entry.has_value()) {
        if(check_time - raf_filter_entry->first_accessed < RAF_TIMEOUT)
          should_drop = true;
      }
      if(update_table)
        raf_filter.fill(raf_entry{champsim::block_number{block},check_time});
      return should_drop;
    }
    void invalidate(champsim::address block) {
      raf_filter.invalidate(raf_entry{champsim::block_number{block},0});
    }
    
  };

  struct ASD_Module {
    Histogram<H_BINS> ActiveHist;
    Histogram<H_BINS> BuildHist;
    uint64_t epoch = EPOCH_MAX;
    uint64_t epoch_timer = 0;
    StreamBuffer<MAX_STREAMS,AGE> Streams;
    StateMachine SM;
    RAF Filter;

    std::array<uint64_t,H_BINS> pf_depths;
    std::array<uint64_t,H_BINS> pf_strides;


    void increment_epoch() {
      if(epoch_timer == 0) {
        double diff = ActiveHist.compare(BuildHist);
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
  };

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
