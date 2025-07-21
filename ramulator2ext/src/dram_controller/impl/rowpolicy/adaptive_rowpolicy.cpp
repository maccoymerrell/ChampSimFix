#include <vector>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/scheduler.h"
#include "dram_controller/rowpolicy.h"
#include "frontend/frontend.h"

namespace Ramulator {

class AdaptiveRowPolicy : public IRowPolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRowPolicy, AdaptiveRowPolicy, "AdaptiveRowPolicy", "Adaptive Row Policy.")
  private:
    IDRAM* m_dram;
    std::vector<uint64_t> m_timer;
    std::vector<Request> m_opening_request;
    std::vector<long> m_row_tracker;
    std::vector<int> m_early_tracker;
    std::vector<int> m_late_tracker;
    std::vector<int> m_source_tracker;

    std::vector<bool> m_occupied;

    std::vector<int> m_tracker_pos;

    int m_rank_level = -1;
    int m_bankgroup_level = -1;
    int m_bank_level = -1;
    int m_row_level = -1;
    int m_num_ranks = -1;
    int m_num_bankgroups = -1;
    int m_num_banks = -1;

    int m_PRE_req_id = -1;

    int m_opc_th = -1;
    int m_ppc_th = -1;
    int m_tracking_window = -1;
    long m_printout_interval = 0;

    std::vector<int> m_page_idle;
    int m_page_idle_default = -1;
    int m_page_idle_max = -1;
    int m_page_idle_incr = -1;

    uint64_t s_early_closes = 0;
    uint64_t s_late_closes = 0;
    uint64_t s_closed = 0;
    uint64_t s_windows = 0;
    uint64_t s_unsourced = 0;
    
  public:
    void init() override {
      m_tracking_window = param<int>("tracking_window_size").desc("The size of the tracking window for determining if a threshold is exceeded").default_val(64);
      m_opc_th = param<int>("late_close_threshold").desc("The threshold for row buffer collisions. If exceeded, the page idle threshold is lowered.").default_val(6);
      m_ppc_th = param<int>("early_close_threshold").desc("The threshold for premature row closes resulting in rowbuffer misses. If exceeded, the page idle threshold is raised.").default_val(6);
      m_page_idle_default = param<int>("default_page_idle_threshold").desc("The threshold for premature row closes resulting in rowbuffer misses. If exceeded, the page idle threshold is raised.").default_val(8);
      m_page_idle_max = param<int>("page_idle_threshold_max").desc("The max page idle threshold.").default_val(64);
      m_page_idle_incr = param<int>("page_idle_threshold_incr").desc("The incrementation by which page idle threshold is altered").default_val(1);
      m_printout_interval = param<long>("printout_interval").desc("Window interval at which to print current page idle threshold.").default_val(0);
     };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override { 
      m_dram = cast_parent<IDRAMController>()->m_dram;
      m_ctrl = cast_parent<IDRAMController>();

      m_rank_level = m_dram->m_levels("rank");
      m_bankgroup_level = m_dram->m_levels("bankgroup");
      m_bank_level = m_dram->m_levels("bank");
      m_row_level = m_dram->m_levels("row");

      m_PRE_req_id = m_dram->m_requests("close-row");

      m_num_ranks = m_dram->get_level_size("rank");
      m_num_bankgroups = m_dram->get_level_size("bankgroup");
      m_num_banks = m_dram->get_level_size("bank");

      m_timer.resize(m_num_banks * m_num_bankgroups * m_num_ranks, 0);
      m_opening_request.resize(m_num_banks * m_num_bankgroups * m_num_ranks,Request{0,Request::Type::Read});
      m_occupied.resize(m_num_banks * m_num_bankgroups * m_num_ranks, false);
      m_row_tracker.resize(m_num_banks * m_num_bankgroups * m_num_ranks,false);
      m_source_tracker.resize(m_num_banks * m_num_bankgroups * m_num_ranks,-1);

      m_page_idle.resize(frontend->get_num_cores(),m_page_idle_default);

      m_early_tracker.resize(frontend->get_num_cores(),0);
      m_late_tracker.resize(frontend->get_num_cores(),0);
      m_tracker_pos.resize(frontend->get_num_cores(),0);

      register_stat(s_early_closes).name("Rows closed early");
      register_stat(s_late_closes).name("Rows closed late");
      register_stat(s_closed).name("Row closed");
      register_stat(s_windows).name("Access windows elapsed");
      register_stat(s_unsourced).name("Accesses without source");

      fmt::print("Initialized Adaptive row policy, late_close_threshold: {} early_close_threshold: {} window_size: {} incr_interval: {} max_interval: {}\n",m_opc_th,m_ppc_th,m_tracking_window,m_page_idle_incr,m_page_idle_max);
    };

    void update(bool request_found, ReqBuffer::iterator& req_it) override { 
      // OpenRowPolicy does not need to take any actions
      //tick down the counters that are not 0
      for(int i = 0; i < m_timer.size(); i++) {
        if(m_timer.at(i) != 0) {
          //close row where timer expired
          if(m_timer.at(i) == 1) {
            Request req(m_opening_request.at(i).addr_vec, m_PRE_req_id);
            req.source_id = -1;
            m_ctrl->priority_send(req);
            m_occupied[i] = false;
            s_closed += 1;
          }
          m_timer.at(i)--;
        }
      }
      if(!request_found)
        return;

      //handle closures or refreshes
      if (m_dram->m_command_meta(req_it->command).is_closing ||
          m_dram->m_command_meta(req_it->command).is_refreshing)  // PRE or REF 
      {  

        if (req_it->addr_vec[m_bankgroup_level] == -1 && req_it->addr_vec[m_bank_level] == -1) {  // all bank closes
          for (int b = 0; b < m_num_banks; b++) {
            for (int bg = 0; bg < m_num_bankgroups; bg++) {
              int rank_id = req_it->addr_vec[m_rank_level];
              int flat_bank_id = b + bg * m_num_banks + rank_id * m_num_banks * m_num_bankgroups;
              //bank is empty, timer is 0
              m_timer[flat_bank_id] = 0;
              //reset occupied tracker (a natural conflict occurred, not us)
              m_occupied[flat_bank_id] = false;
              //reset row tracker (a natural conflict occurred, not us)
              m_row_tracker[flat_bank_id] = __LONG_MAX__;
              m_source_tracker[flat_bank_id] = -1;
            }
          }
        } else if (req_it->addr_vec[m_bankgroup_level] == -1) {  // same bank closes
          for (int bg = 0; bg < m_num_bankgroups; bg++) {
            int bank_id = req_it->addr_vec[m_bank_level];
            int rank_id = req_it->addr_vec[m_rank_level];
            int flat_bank_id = bank_id + bg * m_num_banks + rank_id * m_num_banks * m_num_bankgroups;
            //bank is empty, timer is 0
            m_timer[flat_bank_id] = 0;
            //reset occupied tracker (a natural conflict occurred, not us)
            m_occupied[flat_bank_id] = false;
            //reset row tracker (a natural conflict occurred, not us)
            m_row_tracker[flat_bank_id] = __LONG_MAX__;
            m_source_tracker[flat_bank_id] = -1;
          }
        } else {  // single bank closes  (PRE, VRR, RDA, WRA)
          int flat_bank_id = req_it->addr_vec[m_bank_level] + 
                             req_it->addr_vec[m_bankgroup_level] * m_num_banks + 
                             req_it->addr_vec[m_rank_level] * m_num_banks * m_num_bankgroups;
          //bank is empty, timer is 0
          m_timer[flat_bank_id] = 0;
          //if we didn't evict this, a conflict has occurred
          if(m_occupied[flat_bank_id]) {
            //identify which cpu held this open, and mark it as a late close
            if(m_source_tracker[flat_bank_id] != -1) {
              m_late_tracker[m_source_tracker[flat_bank_id]] += 1;
              s_late_closes += 1;
            }
            //reset row tracker (a natural conflict occurred, not us)
            m_row_tracker[flat_bank_id] = __LONG_MAX__;
            m_source_tracker[flat_bank_id] = -1;
          }
          //reset occupied tracker
          m_occupied[flat_bank_id] = false;
        }
      } else if (m_dram->m_command_meta(req_it->command).is_opening) {
        //start timer on row open
        int flat_bank_id = req_it->addr_vec[m_bank_level] + 
                           req_it->addr_vec[m_bankgroup_level] * m_num_banks + 
                           req_it->addr_vec[m_rank_level] * m_num_banks * m_num_bankgroups;
        if(req_it->source_id != -1)
          m_timer[flat_bank_id] = m_page_idle[req_it->source_id];
        else
          m_timer[flat_bank_id] = m_page_idle_default;

        m_opening_request[flat_bank_id] = *req_it;
        m_occupied[flat_bank_id] = true;
        //we closed too early since rowtracker hasn't been reset and is the same row
        if(m_row_tracker[flat_bank_id] == req_it->addr_vec[m_row_level]) {
          if(req_it->source_id != -1) {
            m_early_tracker[req_it->source_id] += 1;
            s_early_closes += 1;
          }
        }
        m_row_tracker[flat_bank_id] = req_it->addr_vec[m_row_level];
        m_source_tracker[flat_bank_id] = req_it->source_id;

      } else if (m_dram->m_command_meta(req_it->command).is_accessing) {
        //access
          int flat_bank_id = req_it->addr_vec[m_bank_level] + 
                              req_it->addr_vec[m_bankgroup_level] * m_num_banks + 
                              req_it->addr_vec[m_rank_level] * m_num_banks * m_num_bankgroups;
        //reset timer
        if(req_it->source_id != -1)
          m_timer[flat_bank_id] = m_page_idle[req_it->source_id];
        else
          m_timer[flat_bank_id] = m_page_idle_default;
        //increment tracker position
          if(req_it->source_id != -1)
            m_tracker_pos[req_it->source_id]++;
          else
            s_unsourced += 1;
      }

      if(req_it->source_id != - 1 && m_tracker_pos[req_it->source_id] >= m_tracking_window) {
        s_windows += 1;
        if(m_printout_interval != 0 && s_windows % m_printout_interval == 1) {
          for(int i = 0; i < m_page_idle.size(); i++) {
            fmt::print("[{}] Adaptive Row Policy early: {} late: {} current_timer: {}\n",i,m_early_tracker[i],m_late_tracker[i],m_page_idle[i]);
          }
        }
        m_tracker_pos[req_it->source_id] = 0;
        if(m_early_tracker[req_it->source_id] >= m_ppc_th) {
          m_page_idle[req_it->source_id] = m_page_idle[req_it->source_id] + m_page_idle_incr >= m_page_idle_max ? m_page_idle_max : m_page_idle[req_it->source_id] + m_page_idle_incr;
        }
        else if(m_late_tracker[req_it->source_id] >= m_opc_th) {
          m_page_idle[req_it->source_id] = m_page_idle[req_it->source_id] < m_page_idle_incr ? 0 : m_page_idle[req_it->source_id] - m_page_idle_incr;
        }
        m_early_tracker[req_it->source_id] = 0;
        m_late_tracker[req_it->source_id] = 0;
      }
    };
};


}       // namespace Ramulator
