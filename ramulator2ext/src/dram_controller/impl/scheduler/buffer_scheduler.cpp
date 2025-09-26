#include <vector>

#include "base/base.h"
#include "dram_controller/bh_scheduler.h"
#include "dram_controller/drac_controller.h"

namespace Ramulator {

class BufferScheduler : public IBHScheduler, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IBHScheduler, BufferScheduler, "BufferScheduler", "Buffer Scheduler.")

  private:
    IDRAM* m_dram;


    int m_clk = -1;

    bool m_is_debug;

    int m_demand_thresh_1 = -1;
    int m_demand_thresh_2 = -1;

    int m_pref_thresh_1 = -1;
    int m_pref_thresh_2 = -1;
    int m_pref_thresh_3 = -1;
    int m_pref_thresh_4 = -1;

    int m_pref_delay_1 = -1;
    int m_pref_delay_2 = -1;
    int m_pref_delay_3 = -1;
    int m_pref_delay_4 = -1;

    int m_starv_max = -1;
    bool m_only_max_on_prefetch;

    int64_t incr_cycles = -1;
    bool drop_enabled = false;

    int m_rank_level;
    int m_bankgroup_level;
    int m_bank_level;
    int m_row_level;

    int m_num_ranks;
    int m_num_banks;
    int m_num_bankgroups;

    std::vector<int> starvation_counter;

    IDRACController* m_controller;

    uint64_t s_ready_scheduled = 0;
    uint64_t s_age_scheduled = 0;
    uint64_t s_starve_scheduled = 0;
    uint64_t s_tied_scheduled = 0;
    uint64_t s_marked_for_drop = 0;

  public:
    void init() override {
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = cast_parent<IDRAMController>()->m_dram;
      m_controller = cast_parent<IDRACController>();

      m_rank_level = m_dram->m_levels("rank");
      m_bankgroup_level = m_dram->m_levels("bankgroup");
      m_bank_level = m_dram->m_levels("bank");
      m_row_level = m_dram->m_levels("row");

      m_num_ranks = m_dram->get_level_size("rank");
      m_num_bankgroups = m_dram->get_level_size("bankgroup");
      m_num_banks = m_dram->get_level_size("bank");

      starvation_counter.resize(m_num_ranks*m_num_banks*m_num_bankgroups,0);
      
      incr_cycles = param<int>("delay_ns").desc("Ns window on which to delay requests").default_val(100) / (m_dram->m_timing_vals("tCK_ps") * 1e-3);
      drop_enabled = param<bool>("drop").desc("Whether to drop prefetch requests or not").default_val(true);
      assert(incr_cycles != 0);
      fmt::print("Initialized Buffer Scheduler, Age Interval: {} ({} ns)\n",incr_cycles,incr_cycles * m_dram->m_timing_vals("tCK_ps") * 1e-3);
      m_pref_thresh_1 = param<int>("pref_thresh_1").desc("Prefetch threshold 1").default_val(4);
      m_pref_thresh_2 = param<int>("pref_thresh_2").desc("Prefetch threshold 2").default_val(8);
      m_pref_thresh_3 = param<int>("pref_thresh_3").desc("Prefetch threshold 3").default_val(12);
      m_pref_thresh_4 = param<int>("pref_thresh_4").desc("Prefetch threshold 4").default_val(16);

      m_pref_delay_1 = param<int>("pref_delay_1").desc("Prefetch delay 1").default_val(4);
      m_pref_delay_2 = param<int>("pref_delay_2").desc("Prefetch delay 2").default_val(8);
      m_pref_delay_3 = param<int>("pref_delay_3").desc("Prefetch delay 3").default_val(12);
      m_pref_delay_4 = param<int>("pref_delay_4").desc("Prefetch delay 4").default_val(16);

      m_starv_max = param<int>("starvation_limit").desc("Number of requests to same row before deprio").default_val(6);
      m_only_max_on_prefetch = param<bool>("only_limit_prefetch").desc("Only limit the number of prefetches").default_val(false);

      m_is_debug = param<bool>("debug").desc("").default_val(false);

      register_stat(s_starve_scheduled).name("packets scheduled by starvation");
      register_stat(s_ready_scheduled).name("packets scheduled by readiness");
      register_stat(s_age_scheduled).name("packets scheduled by age");
      register_stat(s_tied_scheduled).name("packets scheduled by default (tied)");

    }

    //how do we prioritize correctly?
    //we can assume that most prefetches are useful
    //we can also assume that short-distance prefetches need to be dropped quicker than long-distance prefetches
    //contiguous column accesses should be issued back-to-back, but we should have a starvation counter which gives
    //priority to a different row if the buffer has been on one for so many accesses

    //1. is ready
    //2. is starvation counter exceeded xor is opened row
    //3. is old (adjusted based on distance from original fetch)

    int get_enqueue_time(ReqBuffer::iterator req) {
     
      if(req->is_prefetch) {
        if(m_is_debug)
          fmt::print("Prefetch address was: {} distance was: {}\n",req->addr,req->pf_distance);
        if(m_pref_thresh_1 > req->pf_distance)
          return req->arrive;
        else if(m_pref_thresh_2 > req->pf_distance)
          return req->arrive + (m_pref_delay_1*incr_cycles);
        else if(m_pref_thresh_3 > req->pf_distance)
          return req->arrive + (m_pref_delay_2*incr_cycles);
        else if(m_pref_thresh_4 > req->pf_distance)
          return req->arrive + (m_pref_delay_3*incr_cycles);
        else
          return req->arrive + (m_pref_delay_4*incr_cycles);
      } else
        return req->arrive;
    }

    ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) override {
      bool ready1 = m_dram->check_ready(req1->command, req1->addr_vec);
      bool ready2 = m_dram->check_ready(req2->command, req2->addr_vec);
      bool starve_limit_1 = (starvation_counter[req1->addr_vec[m_bank_level] + m_num_banks*req1->addr_vec[m_bankgroup_level] + m_num_banks*m_num_bankgroups*req1->addr_vec[m_rank_level]] >= m_starv_max) ^ m_dram->check_rowbuffer_hit(req1->command,req1->addr_vec);
      bool starve_limit_2 = (starvation_counter[req2->addr_vec[m_bank_level] + m_num_banks*req2->addr_vec[m_bankgroup_level] + m_num_banks*m_num_bankgroups*req2->addr_vec[m_rank_level]] >= m_starv_max) ^ m_dram->check_rowbuffer_hit(req2->command,req2->addr_vec);
      if(m_only_max_on_prefetch) {
        if(!req1->is_prefetch)
          starve_limit_1 = true;
        if(req2->is_prefetch)
          starve_limit_2 = true;
      }
      int bank_num_2 = req2->addr_vec[m_bank_level] + m_num_banks*req2->addr_vec[m_bankgroup_level] + m_num_banks*m_num_bankgroups*req2->addr_vec[m_rank_level];

      bool age1 = get_enqueue_time(req1) <= get_enqueue_time(req2);
      bool age2 = get_enqueue_time(req2) <= get_enqueue_time(req1);

      //First ready first served
      if(ready1 ^ ready2) {
        s_ready_scheduled++;
        if(ready1)
          return req1;
        else
          return req2;
      }

      if(req1->strict_prio ^ req2->strict_prio) {
        if(req1->strict_prio)
          return req1;
        else
          return req2;
      }

      //starvation counter
      if(starve_limit_1 ^ starve_limit_2) {
        s_starve_scheduled++;
        if(starve_limit_1)
          return req1;
        return req2;
      }
      

      //age
      if ((age1) ^ (age2)) {
        s_age_scheduled++;
        if(age1)
          return req1;
        else
          return req2;
      }
      //we tied in everything, choose req1
      s_tied_scheduled++;
      return req1;
    }

    ReqBuffer::iterator get_best_request(ReqBuffer& buffer) override {
      if (buffer.size() == 0) {
        return buffer.end();
      }

      for (auto& req : buffer) {
        req.command = m_dram->get_preq_command(req.final_command, req.addr_vec);
      }

      auto candidate = buffer.begin();
      for (auto next = std::next(buffer.begin(), 1); next != buffer.end(); next++) {
        candidate = compare(candidate, next);
      }

      //update starve counter
      if(m_dram->check_ready(candidate->command,candidate->addr_vec)) {
        if(m_dram->check_rowbuffer_hit(candidate->command,candidate->addr_vec) && (!m_only_max_on_prefetch || candidate->is_prefetch))
          starvation_counter[candidate->addr_vec[m_bank_level] + m_num_banks*candidate->addr_vec[m_bankgroup_level] + m_num_banks*m_num_bankgroups*candidate->addr_vec[m_rank_level]]++;
        else
          starvation_counter[candidate->addr_vec[m_bank_level] + m_num_banks*candidate->addr_vec[m_bankgroup_level] + m_num_banks*m_num_bankgroups*candidate->addr_vec[m_rank_level]] = 0;
      }

      return candidate;
    }

    virtual void tick() override {
      m_clk++;
    }


};

}       // namespace Ramulator
