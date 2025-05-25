#include <vector>

#include "base/base.h"
#include "dram_controller/bh_scheduler.h"
#include "dram_controller/drac_controller.h"

namespace Ramulator {

class MinimalistScheduler : public IBHScheduler, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IBHScheduler, MinimalistScheduler, "MinimalistScheduler", "Minimalist Scheduler.")

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

    int64_t incr_cycles = -1;
    bool drop_enabled = false;

    IDRACController* m_controller;

    uint64_t s_blacklist_scheduled = 0;
    uint64_t s_ready_scheduled = 0;
    uint64_t s_prio_scheduled = 0;
    uint64_t s_age_scheduled = 0;
    uint64_t s_tied_scheduled = 0;

  public:
    void init() override {
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = cast_parent<IDRAMController>()->m_dram;
      m_controller = cast_parent<IDRACController>();
      
      incr_cycles = param<int>("age_ns").desc("Ns window on which to age requests").default_val(100) / (m_dram->m_timing_vals("tCK_ps") * 1e-3);
      drop_enabled = param<bool>("drop").desc("Whether to drop prefetch requests or not").default_val(true);
      assert(incr_cycles != 0);
      fmt::print("Initialized Minimalist Scheduler, Age Interval: {} ({} ns)\n",incr_cycles,incr_cycles * m_dram->m_timing_vals("tCK_ps") * 1e-3);


      register_stat(s_blacklist_scheduled).name("packets scheduled by blacklist");
      register_stat(s_ready_scheduled).name("packets scheduled by readiness");
      register_stat(s_prio_scheduled).name("packets scheduled by priority");
      register_stat(s_age_scheduled).name("packets scheduled by age");
      register_stat(s_tied_scheduled).name("packets scheduled by default (tied)");

    }

    int get_priority(ReqBuffer::iterator req) {
      if(req->is_prefetch) {
        if(m_pref_thresh_1 > req->pf_distance)
          return 4 + ((m_clk - req->arrive) / incr_cycles);
        else if(m_pref_thresh_2 > req->pf_distance)
          return 3 + ((m_clk - req->arrive) / incr_cycles);
        else if(m_pref_thresh_3 > req->pf_distance)
          return 2 + ((m_clk - req->arrive) / incr_cycles);
        else if(m_pref_thresh_4 > req->pf_distance)
          return 1 + ((m_clk - req->arrive) / incr_cycles);
        else
          return 0 + ((m_clk - req->arrive) / incr_cycles);
      } else {
        int mlp = m_controller->get_core_occupancy(req->source_id,req->type_id == Request::Type::Write);
        if(m_demand_thresh_1 > mlp)
          return 7;
        else if (m_demand_thresh_2 > mlp)
          return 6;
        else
          return 5;
      }
    }

    ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) override {
      bool ready1 = m_dram->check_ready(req1->command, req1->addr_vec);
      bool ready2 = m_dram->check_ready(req2->command, req2->addr_vec);


      int prio1 = get_priority(req1);
      bool blacklist1 = prio1 > 4 && req1->is_prefetch && drop_enabled;
      if(blacklist1) {
        req1->should_drop = true;
        prio1 = 4;
      }
      int prio2 = get_priority(req2);
      bool blacklist2 = prio2 > 4 && req2->is_prefetch && drop_enabled;
      if(blacklist2) {
        req2->should_drop = true;
        prio2 = 4;
      }



      bool age1 = req1->arrive <= req2->arrive;
      bool age2 = req2->arrive <= req1->arrive;

      //we also need to check if its a prefetch and not a row hit
      //if its a prefetch, not a row hit, and new enough that its not starving, don't serve
      if(blacklist1 ^ blacklist2) {
        s_blacklist_scheduled++;
        if(blacklist1)
          return req2;
        else
          return req1;
      }
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
      
      //Highest prio, served first
      if (prio1 != prio2) {
        s_prio_scheduled++;
        if (prio1 > prio2) {
          return req1;
        } else {
          return req2;
        }
      }
      //finally do FCFS if they are still tied, i.e. both are ready, but both cause row activations and are prefetches
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
      return candidate;
    }

    virtual void tick() override {
      m_clk++;
    }


};

}       // namespace Ramulator
