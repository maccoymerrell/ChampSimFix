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

    IDRACController* m_controller;

  public:
    void init() override {
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = cast_parent<IDRAMController>()->m_dram;
      m_controller = cast_parent<IDRACController>();
      

    }

    int get_priority(ReqBuffer::iterator req) {
      if(req->is_prefetch) {
        if(m_pref_thresh_1 > req->pf_distance)
          return 4;
        else if(m_pref_thresh_2 > req->pf_distance)
          return 3;
        else if(m_pref_thresh_3 > req->pf_distance)
          return 2;
        else if(m_pref_thresh_4 > req->pf_distance)
          return 1;
        else
          return 0;
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
      int prio2 = get_priority(req2);

      bool age1 = req1->arrive <= req2->arrive;
      bool age2 = req2->arrive <= req1->arrive;
      //we also need to check if its a prefetch and not a row hit
      //if its a prefetch, not a row hit, and new enough that its not starving, don't serve

      //First ready first served
      if(ready1 ^ ready2) {
        if(ready1)
          return req1;
        else
          return req2;
      }
      //Highest prio, served first
      if (prio1 != prio2) {
        if (prio1 > prio2) {
          return req1;
        } else {
          return req2;
        }
      }
      //finally do FCFS if they are still tied, i.e. both are ready, but both cause row activations and are prefetches
      if ((age1) ^ (age2)) {
        if(age1)
          return req1;
        else
          return req2;
      }
      //we tied in everything, choose req1
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
