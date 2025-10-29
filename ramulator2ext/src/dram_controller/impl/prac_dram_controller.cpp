#include "dram_controller/bh_controller.h"
#include "dram_controller/drac_controller.h"
#include "memory_system/memory_system.h"
#include "frontend/frontend.h"
#include <utility>
//#include "frontend/impl/processor/bhO3/bhllc.h"
//#include "frontend/impl/processor/bhO3/bhO3.h"

#include "dram_controller/impl/plugin/prac/prac.h"

namespace Ramulator {

class PRACDRAMController final : public IDRACController, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IDRACController, PRACDRAMController, "PRACDRAMController", "PRAC DRAM controller.")

private:
    Logger_t m_logger;
    std::deque<Request> pending;          // A queue for read requests that are about to finish (callback after RL)
    //BHO3LLC* m_llc;
    IPRAC* m_prac;

    ReqBuffer m_active_buffer;            // Buffer for requests being served. This has the highest priority 
    ReqBuffer m_priority_buffer;          // Buffer for high-priority requests (e.g., maintenance like refresh).
    ReqBuffer m_read_buffer;              // Read request buffer
    ReqBuffer m_write_buffer;             // Write request buffer
    ReqBuffer m_prac_buffer;              // Custom PRAC buffer
    
    Request* m_prea_template;
    Request* m_rfmab_template;

    int m_rank_addr_idx = -1;
    int m_bankgroup_addr_idx = -1;
    int m_bank_addr_idx = -1;
    int m_row_addr_idx = -1;

    float m_wr_low_watermark;
    float m_wr_high_watermark;

    int m_read_queue_size;
    int m_write_queue_size;

    float m_prom_threshold;
    bool  m_is_write_mode = false;

    bool m_active = false;

    std::vector<int> m_read_core_count;
    std::vector<int> m_write_core_count;

    std::vector<int> s_core_row_hits;
    std::vector<int> s_core_row_misses;
    std::vector<int> s_core_row_conflicts;

    int s_num_row_hits = 0;
    int s_num_row_misses = 0;
    int s_num_row_conflicts = 0;

    uint64_t s_wq_occupancy_cycles = 0;
    uint64_t s_rq_occupancy_cycles = 0;
    uint64_t s_occupancy_cycles = 0;

    std::vector<uint64_t> s_packets_served;
    std::vector<double> s_average_throughput;

    double s_average_wq_occupancy = 0.0;
    double s_average_rq_occupancy = 0.0;

    int debug = false;

    int packet_size;
    int m_num_banks;
    int m_num_bankgroups;
    int m_num_ranks;
    int m_total_banks;

    double tCLK = 0;

    // DEBUG STAT
    int m_invalidate_ctr = -1;

public:
    static std::vector<PRACDRAMController*> prac_controllers;
    int get_core_occupancy(int source_id, bool write) override {
        return write ? m_write_core_count[source_id] : m_read_core_count[source_id];
    }
    bool is_core_critical(void* source_ptr, int source_id) override {
        auto entry = m_core_usefulness.find(std::pair{source_ptr,source_id});
        if(entry == std::end(m_core_usefulness))
            return true;
        
        return entry->second >= m_prom_threshold;
    }
    void tally_critical_requests() {
      for(int i = 0; i < m_read_core_count.size(); i++) {
        m_read_core_count[i] = 0;
      }
      for(int i = 0; i < m_write_core_count.size(); i++) {
        m_write_core_count[i] = 0;
      }

      for(auto& rbe : m_read_buffer) {
        if(is_core_critical(rbe.source_ptr,rbe.source_id) || !rbe.is_prefetch) 
        m_read_core_count[rbe.source_id]++;
      }
      for(auto& wbe : m_write_buffer) {
        if(is_core_critical(wbe.source_ptr,wbe.source_id) || !wbe.is_prefetch)
        m_write_core_count[wbe.source_id]++;
      }
    }
    void init() override {
        m_invalidate_ctr = 0;
        m_wr_low_watermark =  param<float>("wr_low_watermark").desc("Threshold for switching back to read mode.").default_val(0.2f);
        m_wr_high_watermark = param<float>("wr_high_watermark").desc("Threshold for switching to write mode.").default_val(0.8f);
        m_prom_threshold =  param<float>("promotion_threshold").desc("Threshold for treating prefetch as demand").default_val(0.85f);
        m_read_queue_size =  param<int>("read_queue_size").desc("Size of read queue.").default_val(128);
        m_write_queue_size = param<int>("write_queue_size").desc("Size of write queue.").default_val(128);

        m_scheduler = create_child_ifce<IBHScheduler>();
        m_refresh = create_child_ifce<IRefreshManager>();
        m_rowpolicy = create_child_ifce<IRowPolicy>();
        //m_logger = Logging::create_logger("DBHCTRL");

        if (m_config["plugins"]) {
            YAML::Node plugin_configs = m_config["plugins"];
            for (YAML::iterator it = plugin_configs.begin(); it != plugin_configs.end(); ++it) {
                m_plugins.push_back(create_child_ifce<IControllerPlugin>(*it));
            }
        }

        // TODO: Just create it manually / get rid of the plugin and inject it here.
        m_prac = get_plugin<IPRAC>();
        if (!m_prac) {
            std::cout << "[PRACCTRL] Need PRAC plugin!";
            std::exit(0);
        }

        IDRACController::drac_controllers.push_back(static_cast<IDRACController*>(this));
        m_read_buffer.max_size = m_read_queue_size;
        m_write_buffer.max_size = m_write_queue_size;
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
        //m_llc = static_cast<BHO3*>(frontend)->get_llc();
        m_dram = memory_system->get_ifce<IDRAM>();
        m_rank_addr_idx = m_dram->m_levels("rank");
        m_bankgroup_addr_idx = m_dram->m_levels("bankgroup");
        m_bank_addr_idx = m_dram->m_levels("bank");
        m_row_addr_idx = m_dram->m_levels("row");
        m_priority_buffer.max_size = INT_MAX;
        m_active_buffer.max_size = INT_MAX;
        packet_size = m_dram->m_internal_prefetch_size * (m_dram->m_channel_width/8);
        std::vector<int> all_bank_addr_vec(m_dram->m_levels.size(), -1);
        all_bank_addr_vec[m_dram->m_levels("channel")] = m_channel_id;
        int m_prea_id = m_dram->m_commands("PREA");
        int m_rfmab_id = m_dram->m_commands("RFMab");
        tCLK = m_dram->m_timing_vals("tCK_ps") * 1e-12;
        m_prea_template = new Request(all_bank_addr_vec, m_dram->m_requests("close-all-bank"));
        m_prea_template->command = m_prea_id;
        m_prea_template->final_command = m_prea_id;

        m_rfmab_template = new Request(all_bank_addr_vec, m_dram->m_requests("rfm"));
        m_rfmab_template->command = m_rfmab_id;
        m_rfmab_template->final_command = m_rfmab_id;
        
        int num_cores = frontend->get_num_cores();
        s_core_row_hits.resize(num_cores);
        s_core_row_misses.resize(num_cores);
        s_core_row_conflicts.resize(num_cores);

        for (int i = 0; i < num_cores; i++) {
            register_stat(s_core_row_hits[i]).name("controller_core_row_hits_{}", i);
            register_stat(s_core_row_misses[i]).name("controller_core_row_misses_{}", i);
            register_stat(s_core_row_conflicts[i]).name("controller_core_row_conflicts_{}", i);
        }

        m_priority_buffer.max_size = INT_MAX;

        register_stat(s_num_row_hits).name("controller_num_row_hits");
        register_stat(s_num_row_misses).name("controller_num_row_misses");
        register_stat(s_num_row_conflicts).name("controller_num_row_conflicts");
        register_stat(s_average_rq_occupancy).name("average_rq_occupancy");
        register_stat(s_average_wq_occupancy).name("average_wq_occupancy");

        m_read_core_count.resize(num_cores);
        m_write_core_count.resize(num_cores);
        for(int i = 0; i < num_cores; i++) {
            m_read_core_count[i] = 0;
            m_write_core_count[i] = 0;
        }

        m_num_ranks = m_dram->get_level_size("rank");
        m_num_bankgroups = m_dram->get_level_size("bankgroup");
        m_num_banks = m_dram->get_level_size("bank");
        m_total_banks = m_num_ranks * m_num_bankgroups * m_num_banks;

        s_packets_served.resize(m_total_banks,0);
        s_average_throughput.resize(m_total_banks,0);
        for(int i = 0; i < m_total_banks; i++) {
          register_stat(s_average_throughput[i]).name("avg_throughput_gibs_bank_{}",i);
        }
    };
    int get_bank(Request& req) {
      return req.addr_vec[m_rank_addr_idx] * (m_num_bankgroups*m_num_banks) + req.addr_vec[m_bankgroup_addr_idx] * (m_num_banks) + req.addr_vec[m_bank_addr_idx];
    };
    bool send(Request& req) override {
      m_active = true;
      req.final_command = m_dram->m_request_translations(req.type_id);
      if(debug)
      fmt::print(fmt::runtime("[MEMORY CONTROLLER] Processing packet {:#x} promotion: {} prefetch: {} cycle: {}\n"),req.addr, req.is_promotion, req.is_prefetch,m_clk);
      // Forward existing write requests to incoming read requests
      if (req.type_id == Request::Type::Read) {
        auto compare_addr = [req](const Request& wreq) {
          return wreq.addr == req.addr;
        };
        if (std::find_if(m_write_buffer.begin(), m_write_buffer.end(), compare_addr) != m_write_buffer.end()) {
          // The request will depart at the next cycle
          if(debug)
          fmt::print(fmt::runtime("[MEMORY CONTROLLER] Forwarding write for {:#x} cycle: {}\n"),req.addr, m_clk);
          req.depart = m_clk + 1;
          pending.push_back(req);
          return true;
        }
      }
      
      //Drop prefetches that are promoted via read
      if ((req.type_id == Request::Type::Read) && req.is_promotion) {
        //fmt::print("Received promotion for {0:x}, looking for candidate...\n",req.addr);
        auto compare_prefetch = [req](const Request& rreq) {
          return ((rreq.addr >> 6) == (req.addr >> 6)) && rreq.is_prefetch;
        };
        auto in_rq = std::find_if(m_read_buffer.begin(), m_read_buffer.end(), compare_prefetch);
        if(in_rq != m_read_buffer.end()) {
          //fmt::print("Promotion of packet {0:x} to DEMAND READ\n",in_rq->addr);
          in_rq->is_prefetch = false;
          in_rq->was_promoted = true;
        }
        return true;
        //fmt::print("\tCouldn't find one\n");
      }

      //int bundle_index = req.addr_vec[m_col_addr_idx] / m_bundle_length;
      int row = req.addr_vec[m_row_addr_idx];
      int rank = req.addr_vec[m_rank_addr_idx];
      int bank = req.addr_vec[m_bank_addr_idx];
      int bankgroup = req.addr_vec[m_bankgroup_addr_idx];

      //fmt::print("Bundle PF requests: {} Bundle DM requests: {} is_prefetch: {} addr: {:#x}\n",m_bundle_prefetch_requests,m_bundle_demand_requests,req.is_prefetch,req.addr);

      if(debug)
      fmt::print(fmt::runtime("[MEMORY CONTROLLER] Adding packet: {:#x} promotion: {} prefetch: {} cycle: {} to queue\n"),req.addr, req.is_promotion, req.is_prefetch, m_clk);
      // Else, enqueue them to corresponding buffer based on request type id
      bool is_success = false;
      req.arrive = m_clk;
      req.is_critical = !req.is_prefetch ? true : m_core_usefulness[std::pair{req.source_ptr,req.source_id}] >= m_prom_threshold;
      if (req.type_id == Request::Type::Read) {
        is_success = m_read_buffer.enqueue(req);
      } else if (req.type_id == Request::Type::Write) {
        is_success = m_write_buffer.enqueue(req);
      } else {
        throw std::runtime_error("Invalid request type!");
      }
      if (!is_success) {
        // We could not enqueue the request
        req.arrive = -1;
        if(debug)
        fmt::print(fmt::runtime("[MEMORY CONTROLLER] Failed to add packet: {:#x} promotion: {} prefetch: {} cycle: {} to queue\n"),req.addr, req.is_promotion, req.is_prefetch, m_clk);
        return false;
        
      }

      return true;
    };

    bool priority_send(Request& req) override {
        req.final_command = m_dram->m_request_translations(req.type_id);

        bool is_success = false;
        is_success = m_priority_buffer.enqueue(req);
        return is_success;
    }

    void tick() override {
        m_clk++;
        if(m_active) {
            s_occupancy_cycles++;
            s_rq_occupancy_cycles += std::size(m_read_buffer);
            s_wq_occupancy_cycles += std::size(m_write_buffer);
        }
        // Serve completed reads
        serve_completed_reads();

        m_refresh->tick();
        m_scheduler->tick();


        tally_critical_requests();

        // Do we need to setup for the ABO recovery period?
        bool is_recovery_starting = m_prac->next_recovery_cycle() - m_clk <= m_dram->m_timing_vals("nRP") + 5;
        bool is_recovery_setup = m_prac_buffer.size() != 0;
        if (is_recovery_starting && !is_recovery_setup) {
            for (int i = 0; i < m_dram->get_level_size("rank"); i++) {
                m_prea_template->addr_vec[m_dram->m_levels("rank")] = i;
                m_prac_buffer.enqueue(*m_prea_template);
            }
            for (int i = 0; i < m_prac->get_num_abo_recovery_refs(); i++) {
                // Alternate ranks, as PRIO/PRAC queue is served FCFS
                for (int j = 0; j < m_dram->get_level_size("rank"); j++) {
                    m_rfmab_template->addr_vec[m_dram->m_levels("rank")] = j;
                    m_prac_buffer.enqueue(*m_rfmab_template);
                }
            }
        }

        // Try to find a request to serve.
        ReqBuffer::iterator req_it;
        ReqBuffer* buffer = nullptr;
        bool request_found = schedule_request(req_it, buffer);

        // RowPolicy
        m_rowpolicy->update(request_found, req_it);

        // Update all plugins
        for (auto plugin : m_plugins) {
            plugin->update(request_found, req_it);
        }

        // Issue the commands to serve the request
        if (request_found) {
            m_dram->issue_command(req_it->command, req_it->addr_vec);

            // If we are issuing the last command, set depart clock cycle and move the request to the pending queue
            if (req_it->command == req_it->final_command) {
                if (req_it->type_id == Request::Type::Read) {
                    req_it->depart = m_clk + m_dram->m_read_latency;
                    pending.push_back(*req_it);
                    s_packets_served[get_bank(*req_it)] += 1 + req_it->bundled_callbacks.size();
                }
                else if (req_it->type_id == Request::Type::Write) {
                    // TODO: Add code to update statistics
                    s_packets_served[get_bank(*req_it)] += 1 + req_it->bundled_callbacks.size();
                }
                buffer->remove(req_it);
            }
            else if (m_dram->m_command_meta(req_it->command).is_opening) {
                if(m_active_buffer.enqueue(*req_it))
                    buffer->remove(req_it);
            }
        }

    };

private:
    /**
        * @brief    Helper function to serve the completed read requests
        * @details
        * This function is called at the beginning of the tick() function.
        * It checks the pending queue to see if the top request has received data from DRAM.
        * If so, it finishes this request by calling its callback and poping it from the pending queue.
        */
    void serve_completed_reads() {
        if (pending.size()) {
            // Check the first pending request
            auto& req = pending[0];
            if (req.depart <= m_clk) {
                // Request received data from dram
                if (req.depart - req.arrive > 1) {
                    // Check if this requests accesses the DRAM or is being forwarded.
                    // TODO add the stats back
                }

                if (req.callback) {
                    // If the request comes from outside (e.g., processor), call its callback
                    req.callback(req);
                }
                // Finally, remove this request from the pending queue
                pending.pop_front();
            }
        };
    };


    /**
        * @brief    Checks if we need to switch to write mode
        * 
        */
    void set_write_mode() {
        if (!m_is_write_mode) {
            if ((m_write_buffer.size() > m_wr_high_watermark * m_write_buffer.max_size) || m_read_buffer.size() == 0) {
                m_is_write_mode = true;
            }
        } else {
            if ((m_write_buffer.size() < m_wr_low_watermark * m_write_buffer.max_size) && m_read_buffer.size() != 0) {
                m_is_write_mode = false;
            }
        }
    };

    /**
        * @brief    Helper function to find a request to schedule from the buffers.
        * 
        */
    bool schedule_request(ReqBuffer::iterator& req_it, ReqBuffer*& req_buffer) {
        bool request_found = false;
        Clk_t next_recovery_clk = m_prac->next_recovery_cycle();
        // 2.1    First, check the act buffer to serve requests that are already activating (avoid useless ACTs)
        if (req_it = m_scheduler->get_best_request(m_active_buffer); req_it != m_active_buffer.end()) { 
            bool fits = m_clk + m_prac->min_cycles_with_preall(req_it) < next_recovery_clk;
            if (fits && m_dram->check_ready(req_it->command, req_it->addr_vec)) {
                request_found = true;
                req_buffer = &m_active_buffer;
            }
        }
        // 2.2    If no requests can be scheduled from the act buffer, check the rest of the buffers
        if (!request_found) {
            // 2.2.1    We first check if MC has critical ABO requests
            if (m_prac_buffer.size() != 0) {
                req_buffer = &m_prac_buffer;
                req_it = m_prac_buffer.begin();
                req_it->command = m_dram->get_preq_command(req_it->final_command, req_it->addr_vec);

                bool is_rfm = req_it->command == m_dram->m_commands("RFMab");
                bool is_pre_rec = m_prac->get_state() == IPRAC::ABOState::PRE_RECOVERY;
                bool early_issue = is_rfm && is_pre_rec; // Prevent controller from issuing RFMab before recovery starts
                request_found = !early_issue && m_dram->check_ready(req_it->command, req_it->addr_vec);
                if (!request_found & m_prac_buffer.size() != 0) {
                    return false;
                }
            }

            // 2.2.2    We then check the priority buffer to prioritize e.g., maintenance requests
            if (m_priority_buffer.size() != 0) {
                req_buffer = &m_priority_buffer;
                req_it = m_priority_buffer.begin();
                req_it->command = m_dram->get_preq_command(req_it->final_command, req_it->addr_vec);

                bool fits = m_clk + m_prac->min_cycles_with_preall(req_it) < next_recovery_clk;
                request_found = fits && m_dram->check_ready(req_it->command, req_it->addr_vec);
                if (!request_found & m_priority_buffer.size() != 0) {
                    return false;
                }
            }

            // 2.2.3    If no request to be scheduled in the priority buffer, check the read and write buffers.
            if (!request_found) {
                // Query the write policy to decide which buffer to serve
                set_write_mode();
                auto& buffer = m_is_write_mode ? m_write_buffer : m_read_buffer;
                if (req_it = m_scheduler->get_best_request(buffer); req_it != buffer.end()) {
                    bool fits = m_clk + m_prac->min_cycles_with_preall(req_it) < next_recovery_clk;
                    request_found = fits && m_dram->check_ready(req_it->command, req_it->addr_vec);
                    req_buffer = &buffer;
                }
            }
        }

        if (request_found && m_dram->m_command_meta(req_it->command).is_closing) {
            auto& rowgroup = req_it->addr_vec;
            for (auto _it = m_active_buffer.begin(); _it != m_active_buffer.end(); _it++) {
                auto& _it_rowgroup = _it->addr_vec;
                bool is_matching = true;
                for (int i = 0; i < m_bank_addr_idx + 1 ; i++) {
                    if (_it_rowgroup[i] != rowgroup[i] && _it_rowgroup[i] != -1 && rowgroup[i] != -1) {
                        is_matching = false;
                        break;
                    }
                }
                if (is_matching) {
                    request_found = false;
                    break;
                }
            }
        }

        if (request_found && req_buffer != &m_active_buffer) {
            if (req_it->type_id == Request::Type::Read
                || req_it->type_id == Request::Type::Write) {
                auto& req_meta = m_dram->m_command_meta(req_it->command);
                int source_id = req_it->source_id >= 0 ? req_it->source_id : 0;
                int increment = req_it->source_id >= 0 ? 1 : 0;
                if (req_meta.is_accessing) {
                    s_core_row_hits[source_id] += increment;
                    s_num_row_hits++;
                }
                if (req_meta.is_opening) {
                    s_core_row_misses[source_id] += increment;
                    s_num_row_misses++;
                }
                if (req_meta.is_closing) {
                    s_core_row_conflicts[source_id] += increment;
                    s_num_row_conflicts++;
                }
            }
        }
        return request_found;
    }

    void finalize() override {
      s_average_rq_occupancy = s_rq_occupancy_cycles / (double)(s_occupancy_cycles + 1);
      s_average_wq_occupancy = s_wq_occupancy_cycles / (double)(s_occupancy_cycles + 1);
      for(int i = 0; i < m_total_banks; i++) {
        s_average_throughput[i] = ((s_packets_served[i] / (double)s_occupancy_cycles)*packet_size) * (1.0/tCLK) / double(1<<30);
      }
    }
};
}   // namespace Ramulator