#include <vector>

#include "base/base.h"
#include "dram_controller/bh_scheduler.h"
#include "dram_controller/drac_controller.h"

namespace Ramulator {

class DRACScheduler : public IBHScheduler, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IBHScheduler, DRACScheduler, "DRACScheduler", "DRAC Scheduler.")

  private:
    IDRAM* m_dram;


    int m_clk = -1;

    bool m_is_debug;


    uint64_t m_ready_scheduled;
    uint64_t m_critical_scheduled;
    uint64_t m_row_scheduled;
    uint64_t m_urgent_scheduled;
    uint64_t m_fair_scheduled;
    uint64_t m_time_scheduled;
    uint64_t m_tied_scheduled;

    IDRACController* m_controller;

  public:
    void init() override {
      m_ready_scheduled = 0;
      m_critical_scheduled = 0;
      m_row_scheduled = 0;
      m_urgent_scheduled = 0;
      m_fair_scheduled = 0;
      m_time_scheduled = 0;
      m_tied_scheduled = 0;
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = cast_parent<IDRAMController>()->m_dram;
      m_controller = cast_parent<IDRACController>();
      register_stat(m_fair_scheduled).name("packets scheduled for fairness");
      register_stat(m_critical_scheduled).name("packets scheduled by criticality");
      register_stat(m_ready_scheduled).name("packets scheduled by readiness");
      register_stat(m_time_scheduled).name("packets scheduled by timeliness");
      register_stat(m_row_scheduled).name("packets scheduled by row locality");
      register_stat(m_urgent_scheduled).name("packets scheduled by urgency");
      register_stat(m_tied_scheduled).name("packets were completely tied");

    }

    ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) override {
      bool ready1 = m_dram->check_ready(req1->command, req1->addr_vec);
      bool ready2 = m_dram->check_ready(req2->command, req2->addr_vec);

      bool crit1 = m_controller->is_core_critical(req1->source_id) || !req1->is_prefetch;
      bool crit2 = m_controller->is_core_critical(req2->source_id) || !req2->is_prefetch;

      bool rowh1 = m_dram->check_rowbuffer_hit(req1->command,req1->addr_vec);
      bool rowh2 = m_dram->check_rowbuffer_hit(req2->command,req2->addr_vec);

      bool urg1 = !req1->is_prefetch && !m_controller->is_core_critical(req1->source_id);
      bool urg2 = !req2->is_prefetch && !m_controller->is_core_critical(req2->source_id);

      int core1 = m_controller->get_core_occupancy(req1->source_id,req1->type_id == Request::Type::Write);
      int core2 = m_controller->get_core_occupancy(req2->source_id,req2->type_id == Request::Type::Write);
      
      bool rnk1 = core1 <= core2;
      bool rnk2 = core2 <= core1;

      bool age1 = req1->arrive <= req2->arrive;
      bool age2 = req2->arrive <= req1->arrive;
      //we also need to check if its a prefetch and not a row hit
      //if its a prefetch, not a row hit, and new enough that its not starving, don't serve

      //first ready, first served
      if ((ready1) ^ (ready2)) {
        m_ready_scheduled++;
        if (ready1) {
          return req1;
        } else {
          return req2;
        }
      }
      //now do critical over non-critical
      if ((crit1) ^ (crit2)) {
        m_critical_scheduled++;
        if(crit1)
          return req1;
        else
          return req2;
      }
      //now do row hit over row miss
      if ((rowh1) ^ (rowh2)) {
        m_row_scheduled++;
        if(rowh1)
          return req1;
        else
          return req2;
      }
      //now do urgent over non-urgent
      if ((urg1) ^ (urg2)) {
        m_urgent_scheduled++;
        if(urg1)
          return req1;
        else
          return req2;
      }
      //now do fairness
      if ((rnk1) ^ (rnk2)) {
        m_fair_scheduled++;
        if(rnk1)
          return req1;
        else
          return req2;
      }
      //finally do FCFS if they are still tied, i.e. both are ready, but both cause row activations and are prefetches
      if ((age1) ^ (age2)) {
        m_time_scheduled++;
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
