#include "dram_controller/bh_controller.h"
#include "memory_system/memory_system.h"
#include "frontend/frontend.h"

namespace Ramulator {

class PRADRAMController final : public IBHDRAMController, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IBHDRAMController, PRADRAMController, "PRADRAMController", "PRADRAM controller.");
  
  private:
    //Logger_t m_logger;
    std::deque<Request> pending;          // A queue for read requests that are about to finish (callback after RL)

    ReqBuffer m_active_buffer;            // Buffer for requests being served. This has the highest priority 
    ReqBuffer m_priority_buffer;          // Buffer for high-priority requests (e.g., maintenance like refresh).
    ReqBuffer m_read_buffer;              // Read request buffer
    ReqBuffer m_write_buffer;             // Write request buffer

    int m_rank_addr_idx = -1;
    int m_bankgroup_addr_idx = -1;
    int m_bank_addr_idx = -1;
    int m_row_addr_idx = -1;

    float m_wr_low_watermark;
    float m_wr_high_watermark;
    bool  m_is_write_mode = false;

    std::vector<int> s_core_row_hits;
    std::vector<int> s_core_row_misses;
    std::vector<int> s_core_row_conflicts;

    int s_num_row_hits = 0;
    int s_num_row_misses = 0;
    int s_num_row_conflicts = 0;

    // DEBUG STAT
    int m_invalidate_ctr = -1;


    uint64_t m_prefetch_starve = -1;

    uint64_t s_prefetch_stall_cycles = 0;
    uint64_t s_prefetch_row_hits = 0;
    uint64_t s_prefetch_row_misses = 0;

    float m_w_back_off_cap = 0;
    float m_r_back_off_cap = 0;

  public:
    void init() override {
      m_invalidate_ctr = 0;
      m_wr_low_watermark =  param<float>("wr_low_watermark").desc("Threshold for switching back to read mode.").default_val(0.2f);
      m_wr_high_watermark = param<float>("wr_high_watermark").desc("Threshold for switching to write mode.").default_val(0.8f);
      m_prefetch_starve = param<uint64_t>("prefetch_starve_cycles").desc("# of cycles to wait before forcibly serving a prefetch").default_val(0);
      m_r_back_off_cap = param<float>("r_back_off_thresh").desc("Capacity of queue before signalling back off").default_val(0.8f);
      m_w_back_off_cap = param<float>("w_back_off_thresh").desc("Capacity of queue before signalling back off").default_val(0.8f);

      m_scheduler = create_child_ifce<IBHScheduler>();
      m_refresh = create_child_ifce<IRefreshManager>();
      m_rowpolicy = create_child_ifce<IRowPolicy>();

      m_read_buffer.max_size = 128;
      m_write_buffer.max_size = 128;
      //m_logger = Logging::create_logger("DBHCTRL");

      if (m_config["plugins"]) {
        YAML::Node plugin_configs = m_config["plugins"];
        for (YAML::iterator it = plugin_configs.begin(); it != plugin_configs.end(); ++it) {
          m_plugins.push_back(create_child_ifce<IControllerPlugin>(*it));
        }
      }
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = memory_system->get_ifce<IDRAM>();
      m_rank_addr_idx = m_dram->m_levels("rank");
      m_bankgroup_addr_idx = m_dram->m_levels("bankgroup");
      m_bank_addr_idx = m_dram->m_levels("bank");
      m_row_addr_idx = m_dram->m_levels("row");
      m_priority_buffer.max_size = 512*3 + 32;
      
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
      register_stat(s_prefetch_stall_cycles).name("prefetch_stall_cycles");
      register_stat(s_prefetch_row_misses).name("prefetch_row_misses");
      register_stat(s_prefetch_row_hits).name("prefetch_row_hits");
    };

    bool send(Request& req) override {
      req.final_command = m_dram->m_request_translations(req.type_id);
      
      // Forward existing write requests to incoming read requests
      if (req.type_id == Request::Type::Read) {
        auto compare_addr = [req](const Request& wreq) {
          return wreq.addr == req.addr;
        };
        if (std::find_if(m_write_buffer.begin(), m_write_buffer.end(), compare_addr) != m_write_buffer.end()) {
          // The request will depart at the next cycle
          //fmt::print("[DRAM] Forwarding write for {0:x}\n",req.addr);
          req.depart = m_clk + 1;
          pending.push_back(req);
          return true;
        }
      }
      //fmt::print("Processing packet {0:x} promotion: {} prefetch: {}\n",req.addr, req.is_promotion, req.is_prefetch);
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

      //if is a prefetch, go ahead and drop half according to their arrival cycle
      
      if(req.is_prefetch) {
        if (m_clk % 2) {
          req.was_dropped = true;
          pending.push_back(req);
          return true;
        }
      }
      

      // Else, enqueue them to corresponding buffer based on request type id
      bool is_success = false;
      req.arrive = m_clk;
      if        (req.type_id == Request::Type::Read) {
        is_success = m_read_buffer.enqueue(req);
      } else if (req.type_id == Request::Type::Write) {
        is_success = m_write_buffer.enqueue(req);
      } else {
        throw std::runtime_error("Invalid request type!");
      }
      if (!is_success) {
        // We could not enqueue the request
        req.arrive = -1;
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
      // 1. Serve completed reads
      serve_completed_reads();
      m_refresh->tick();
      m_scheduler->tick();

      // 2. Try to find a request to serve.
      ReqBuffer::iterator req_it;
      ReqBuffer* buffer = nullptr;
      bool request_found = schedule_request(req_it, buffer);
      // 2.1 RowPolicy
      m_rowpolicy->update(request_found, req_it);

      // 3. Update all plugins
      for (auto plugin : m_plugins) {
        plugin->update(request_found, req_it);
      }

      // 4. Finally, issue the commands to serve the request
      if (request_found) {
        // If we find a real request to serve
        m_dram->issue_command(req_it->command, req_it->addr_vec);

        // If we are issuing the last command, set depart clock cycle and move the request to the pending queue
        if (req_it->command == req_it->final_command) {
          if (req_it->type_id == Request::Type::Read) {
            req_it->depart = m_clk + m_dram->m_read_latency;
            pending.push_back(*req_it);
          } else if (req_it->type_id == Request::Type::Write) {
            // TODO: Add code to update statistics
          }
          buffer->remove(req_it);
        } else {
          if (m_dram->m_command_meta(req_it->command).is_opening) {
            if(m_active_buffer.enqueue(*req_it))
              buffer->remove(req_it);
          }
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
      // 2.1    First, check the act buffer to serve requests that are already activating (avoid useless ACTs)
      if (req_it = m_scheduler->get_best_request(m_active_buffer); req_it != m_active_buffer.end()) {
        if (m_dram->check_ready(req_it->command, req_it->addr_vec)) {
          request_found = true;
          req_buffer = &m_active_buffer;
        }
      }
      // 2.2    If no requests can be scheduled from the act buffer, check the rest of the buffers
      if (!request_found) {
        // 2.2.1    We first check the priority buffer to prioritize e.g., maintenance requests
        if (m_priority_buffer.size() != 0) {
          req_buffer = &m_priority_buffer;
          req_it = m_priority_buffer.begin();
          req_it->command = m_dram->get_preq_command(req_it->final_command, req_it->addr_vec);
          
          request_found = m_dram->check_ready(req_it->command, req_it->addr_vec);
          if (!request_found & m_priority_buffer.size() != 0) {
            return false;
          }
        }

        // 2.2.1    If no request to be scheduled in the priority buffer, check the read and write buffers.
        if (!request_found) {
          // Query the write policy to decide which buffer to serve
          set_write_mode();
          auto& buffer = m_is_write_mode ? m_write_buffer : m_read_buffer;
          if (req_it = m_scheduler->get_best_request(buffer); req_it != buffer.end()) {
            //determine if pact
            bool is_pact = req_it->is_prefetch && !m_dram->check_rowbuffer_hit(req_it->command,req_it->addr_vec) && (m_clk - req_it->arrive < m_prefetch_starve);

            request_found = m_dram->check_ready(req_it->command, req_it->addr_vec);

            //adjust if is pact
            if(request_found && is_pact)
              s_prefetch_stall_cycles++;
            request_found = request_found && !is_pact;

            req_buffer = &buffer;
          }
        }
      }
      if (request_found) {
        if (m_dram->m_command_meta(req_it->command).is_closing) {
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
            if(req_it->is_prefetch)
              s_prefetch_row_hits++;
          }
          if (req_meta.is_opening) {
            s_core_row_misses[source_id] += increment;
            s_num_row_misses++;
            req_it->row_act = true;
            if(req_it->is_prefetch)
              s_prefetch_row_misses++;
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
    }
};
}   // namespace Ramulator