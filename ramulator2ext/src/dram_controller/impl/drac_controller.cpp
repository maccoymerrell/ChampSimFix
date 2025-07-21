#include "dram_controller/drac_controller.h"
#include "memory_system/memory_system.h"
#include "frontend/frontend.h"
#include "addr_mapper/addr_mapper.h"

#include <map>



namespace Ramulator {

class DRACController final : public IDRACController, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IDRACController, DRACController, "DRACController", "DRAC controller.");
  
  private:
    //Logger_t m_logger;
    std::deque<Request> pending;          // A queue for read requests that are about to finish (callback after RL)

    ReqBuffer m_active_buffer;            // Buffer for requests being served. This has the highest priority 
    ReqBuffer m_priority_buffer;          // Buffer for high-priority requests (e.g., maintenance like refresh).
    ReqBuffer m_read_buffer;              // Read request buffer
    ReqBuffer m_write_buffer;             // Write request buffer

    bool debug = false;
    int m_rank_addr_idx = -1;
    int m_bankgroup_addr_idx = -1;
    int m_bank_addr_idx = -1;
    int m_row_addr_idx = -1;
    int m_col_addr_idx = -1;

    float m_wr_low_watermark;
    float m_wr_high_watermark;

    int m_read_queue_size;
    int m_write_queue_size;

    bool m_bundle_prefetch_requests = false;
    bool m_bundle_demand_requests = false;
    int m_bundle_length = -1;

    bool  m_is_write_mode = false;

    std::vector<int> s_core_row_hits;
    std::vector<int> s_core_row_misses;
    std::vector<int> s_core_row_conflicts;

    std::map<std::pair<void*,int>,double> m_core_usefulness;
    std::vector<int> m_read_core_count;
    std::vector<int> m_write_core_count;

    std::vector<int> m_read_core_total;
    std::vector<int> m_write_core_total;

    int s_num_row_hits = 0;
    int s_num_row_misses = 0;
    int s_num_row_conflicts = 0;

    // DEBUG STAT
    int m_invalidate_ctr = -1;


    double m_prom_threshold;
    std::vector<double> m_drop_thresholds;
    std::vector<uint64_t> m_drop_cycles;

    uint64_t s_prefetch_row_hits = 0;
    uint64_t s_prefetch_row_misses = 0;
    uint64_t s_prefetches_dropped = 0;

    std::vector<uint64_t> s_dropped_by_scheduler;
    std::vector<uint64_t> s_bundled_requests;

    bool m_drop_enabled;

    IAddrMapper* m_addr_map;


  public:
    static std::vector<DRACController*> drac_controllers;
    void init() override {
      m_invalidate_ctr = 0;
      m_wr_low_watermark =  param<float>("wr_low_watermark").desc("Threshold for switching back to read mode.").default_val(0.2f);
      m_wr_high_watermark = param<float>("wr_high_watermark").desc("Threshold for switching to write mode.").default_val(0.8f);

      m_read_queue_size =  param<int>("read_queue_size").desc("Size of read queue.").default_val(128);
      m_write_queue_size = param<int>("write_queue_size").desc("Size of write queue.").default_val(128);

      m_bundle_prefetch_requests = param<bool>("prefetch_bundle").desc("Enable bundling prefetches").default_val(true);
      m_bundle_demand_requests = param<bool>("demand_bundle").desc("Enable bundling demands").default_val(false);
      m_bundle_length = param<int>("bundle_length").desc("Column range of bundles").default_val(4);


      m_scheduler = create_child_ifce<IBHScheduler>();
      m_refresh = create_child_ifce<IRefreshManager>();
      m_rowpolicy = create_child_ifce<IRowPolicy>();

      m_drop_enabled = param<bool>("drop_enabled").desc("Enable dropping prefetches").default_val(true);
      
      m_prom_threshold =  param<float>("promotion_threshold").desc("Threshold for treating prefetch as demand").default_val(0.85f);
      m_drop_cycles.resize(4);
      m_drop_thresholds.resize(4);
      m_drop_cycles[0] = param<uint64_t>("drop_cycle_0").desc("Timeout for prefetches below drop threshold 0's accuracy").default_val(100);
      m_drop_cycles[1] = param<uint64_t>("drop_cycle_1").desc("Timeout for prefetches below drop threshold 1's accuracy").default_val(1500);
      m_drop_cycles[2] = param<uint64_t>("drop_cycle_2").desc("Timeout for prefetches below drop threshold 2's accuracy").default_val(50000);
      m_drop_cycles[3] = param<uint64_t>("drop_cycle_3").desc("Timeout for prefetches below drop threshold 3's accuracy").default_val(100000);

      m_drop_thresholds[0] = param<double>("drop_threshold_0").desc("Threshold by which to drop aged prefetches in bin 0").default_val(0.1);
      m_drop_thresholds[1] = param<double>("drop_threshold_1").desc("Threshold by which to drop aged prefetches in bin 1").default_val(0.3);
      m_drop_thresholds[2] = param<double>("drop_threshold_2").desc("Threshold by which to drop aged prefetches in bin 2").default_val(0.7);
      m_drop_thresholds[3] = param<double>("drop_threshold_3").desc("Threshold by which to drop aged prefetches in bin 3").default_val(1.0);

      m_read_buffer.max_size = m_read_queue_size;
      m_write_buffer.max_size = m_write_queue_size;
      //m_logger = Logging::create_logger("DBHCTRL");

      if (m_config["plugins"]) {
        YAML::Node plugin_configs = m_config["plugins"];
        for (YAML::iterator it = plugin_configs.begin(); it != plugin_configs.end(); ++it) {
          m_plugins.push_back(create_child_ifce<IControllerPlugin>(*it));
        }
      }
      drac_controllers.push_back(this);
      if(debug)
      fmt::print("[MEMORY CONTROLLER] RQ size: {} WQ size: {}\n",m_read_buffer.max_size,m_write_buffer.max_size);
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = memory_system->get_ifce<IDRAM>();
      m_rank_addr_idx = m_dram->m_levels("rank");
      m_bankgroup_addr_idx = m_dram->m_levels("bankgroup");
      m_bank_addr_idx = m_dram->m_levels("bank");
      m_row_addr_idx = m_dram->m_levels("row");
      m_col_addr_idx = m_dram->m_levels("column");
      m_priority_buffer.max_size = 512*3 + 32;

      m_addr_map = memory_system->get_ifce<IAddrMapper>();
      
      int num_cores = frontend->get_num_cores();
      s_core_row_hits.resize(num_cores);
      s_core_row_misses.resize(num_cores);
      s_core_row_conflicts.resize(num_cores);
      s_bundled_requests.resize(num_cores);
      s_dropped_by_scheduler.resize(num_cores);
      m_read_core_count.resize(num_cores);
      m_write_core_count.resize(num_cores);
      m_read_core_total.resize(num_cores);
      m_write_core_total.resize(num_cores);
      for(int i = 0; i < num_cores; i++) {
        m_read_core_count[i] = 0;
        m_write_core_count[i] = 0;
        m_read_core_total[i] = 0;
        m_write_core_total[i] = 0;
        s_bundled_requests[i] = 0;
        s_dropped_by_scheduler[i] = 0;
        s_core_row_conflicts[i] = 0;
        s_core_row_hits[i] = 0;
        s_core_row_misses[i] = 0;
      }

      for (int i = 0; i < num_cores; i++) {
        register_stat(s_core_row_hits[i]).name("controller_core_row_hits_{}", i);
        register_stat(s_core_row_misses[i]).name("controller_core_row_misses_{}", i);
        register_stat(s_core_row_conflicts[i]).name("controller_core_row_conflicts_{}", i);
        register_stat(s_bundled_requests[i]).name("controller_core_bundled_requests_{}", i);
        register_stat(s_dropped_by_scheduler[i]).name("controller_core_dropped_by_scheduler_{}", i);
      }

      m_priority_buffer.max_size = INT_MAX;
      m_active_buffer.max_size = INT_MAX;

      register_stat(s_num_row_hits).name("controller_num_row_hits");
      register_stat(s_num_row_misses).name("controller_num_row_misses");
      register_stat(s_num_row_conflicts).name("controller_num_row_conflicts");
      register_stat(s_prefetch_row_misses).name("prefetch_row_misses");
      register_stat(s_prefetch_row_hits).name("prefetch_row_hits");
      register_stat(s_prefetches_dropped).name("prefetches_dropped");
    };

    bool send(Request& req) override {
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

      int bundle_index = req.addr_vec[m_col_addr_idx] / m_bundle_length;
      int row = req.addr_vec[m_row_addr_idx];
      int rank = req.addr_vec[m_rank_addr_idx];
      int bank = req.addr_vec[m_bank_addr_idx];
      int bankgroup = req.addr_vec[m_bankgroup_addr_idx];

      //fmt::print("Bundle PF requests: {} Bundle DM requests: {} is_prefetch: {} addr: {:#x}\n",m_bundle_prefetch_requests,m_bundle_demand_requests,req.is_prefetch,req.addr);

      //if prefetch, check through read queue and bundle if within certain column range
      if(m_bundle_prefetch_requests && m_bundle_demand_requests && req.type_id == Request::Type::Read) {
        //bundle to everything
        auto find_for_bundle = [&](const Request& rreq) {
          int req_bundle_index = rreq.addr_vec[m_col_addr_idx] / m_bundle_length;
          int req_row = rreq.addr_vec[m_row_addr_idx];
          int req_rank = rreq.addr_vec[m_rank_addr_idx];
          int req_bank = rreq.addr_vec[m_bank_addr_idx];
          int req_bankgroup = rreq.addr_vec[m_bankgroup_addr_idx];
          //fmt::print("\tBG: {} MBG: {} B: {} MB: {} R: {} MR: {} RW: {} MRW: {} BN: {} MBN: {}\n",bankgroup,req_bankgroup,bank,req_bank,rank,req_rank,row,req_row,bundle_index,req_bundle_index);
          bool matching_rb = (req_row == row) && (req_rank == rank) && (req_bank == bank) && (req_bankgroup == bankgroup);
          return (matching_rb && (bundle_index == req_bundle_index));
        };
        auto in_rq = std::find_if(m_read_buffer.begin(), m_read_buffer.end(), find_for_bundle);
        //found something to bundle to
        if(in_rq != m_read_buffer.end()) {
          in_rq->bundled_callbacks.emplace_back(std::pair{req.addr,req.callback});
          s_bundled_requests[req.source_id]++;
          if(debug)
            fmt::print("[MEMORY CONTROLLER] Bundling request {:x} into {:#x}\n",req.addr,in_rq->addr);
          return true;
        }
      }
      else if(m_bundle_prefetch_requests && req.is_prefetch && req.type_id == Request::Type::Read) {
        //bundle to prefetches only
        int bundle_index = req.addr_vec[m_col_addr_idx] / m_bundle_length;
        auto find_for_bundle = [&](const Request& rreq) {
          int req_bundle_index = rreq.addr_vec[m_col_addr_idx] / m_bundle_length;
          int req_row = rreq.addr_vec[m_row_addr_idx];
          int req_rank = rreq.addr_vec[m_rank_addr_idx];
          int req_bank = rreq.addr_vec[m_bank_addr_idx];
          int req_bankgroup = rreq.addr_vec[m_bankgroup_addr_idx];
          bool matching_rb = (req_row == row) && (req_rank == rank) && (req_bank == bank) && (req_bankgroup == bankgroup);
          return (matching_rb && (bundle_index == req_bundle_index) && rreq.is_prefetch);
        };
        auto in_rq = std::find_if(m_read_buffer.begin(), m_read_buffer.end(), find_for_bundle);
        //found something to bundle to
        if(in_rq != m_read_buffer.end()) {
          in_rq->bundled_callbacks.emplace_back(std::pair{req.addr,req.callback});
          s_bundled_requests[req.source_id]++;
          if(debug)
            fmt::print("[MEMORY CONTROLLER] Bundling request {:x} into {:#x}\n",req.addr,in_rq->addr);
          return true;
        }
      }
      else if(m_bundle_demand_requests && !req.is_prefetch && req.type_id == Request::Type::Read) {
        //bundle to demands only
        int bundle_index = req.addr_vec[m_col_addr_idx] / m_bundle_length;
        auto find_for_bundle = [&](const Request& rreq) {
          int req_bundle_index = rreq.addr_vec[m_col_addr_idx] / m_bundle_length;
          int req_row = rreq.addr_vec[m_row_addr_idx];
          int req_rank = rreq.addr_vec[m_rank_addr_idx];
          int req_bank = rreq.addr_vec[m_bank_addr_idx];
          int req_bankgroup = rreq.addr_vec[m_bankgroup_addr_idx];
          bool matching_rb = (req_row == row) && (req_rank == rank) && (req_bank == bank) && (req_bankgroup == bankgroup);
          return (matching_rb && (bundle_index == req_bundle_index) && !rreq.is_prefetch);
        };
        auto in_rq = std::find_if(m_read_buffer.begin(), m_read_buffer.end(), find_for_bundle);
        //found something to bundle to
        if(in_rq != m_read_buffer.end()) {
          in_rq->bundled_callbacks.emplace_back(std::pair{req.addr,req.callback});
          s_bundled_requests[req.source_id]++;
          if(debug)
            fmt::print("[MEMORY CONTROLLER] Bundling request {:x} into {:#x}\n",req.addr,in_rq->addr);
          return true;
        }
      }

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

    void tally_critical_requests() {
      for(int i = 0; i < m_read_core_count.size(); i++) {
        m_read_core_count[i] = 0;
        m_read_core_total[i] = 0;
      }
      for(int i = 0; i < m_write_core_count.size(); i++) {
        m_write_core_count[i] = 0;
        m_write_core_total[i] = 0;
      }

      for(auto& rbe : m_read_buffer) {
        m_read_core_total[rbe.source_id]++;
        if(is_core_critical(rbe.source_ptr,rbe.source_id) || !rbe.is_prefetch) 
        m_read_core_count[rbe.source_id]++;
      }
      for(auto& wbe : m_write_buffer) {
        m_write_core_total[wbe.source_id]++;
        if(is_core_critical(wbe.source_ptr,wbe.source_id) || !wbe.is_prefetch)
        m_write_core_count[wbe.source_id]++;
      }
    }

    void tick() override {
      m_clk++;
      // 1. Serve completed reads
      serve_completed_reads();
      m_refresh->tick();
      m_scheduler->tick();

      tally_critical_requests();

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
        if(debug)
          fmt::print("[MEMORY CONTROLLER] Serving request: {:#x} promotion: {} prefetch: {} dropped: {} cycle: {}\n",req_it->addr, req_it->was_promoted, req_it->is_prefetch, req_it->was_dropped, m_clk);

        // If we are issuing the last command, set depart clock cycle and move the request to the pending queue
        if(req_it->command == req_it->final_command) {
          if(req_it->type_id == Request::Type::Read && debug)
            fmt::print("[MEMORY CONTROLLER] Incrementing commands issued for request: {:#x}, cycle: {} prefetch: {}\n",req_it->addr,m_clk,req_it->is_prefetch);
          req_it->commands_issued++;
          if(req_it->bundled_callbacks.size() >= req_it->commands_issued) {
            req_it->addr = req_it->bundled_callbacks.at(req_it->commands_issued - 1).first;
            m_addr_map->apply(*req_it);
            req_it->strict_prio = true;
          }
        }
        if (req_it->command == req_it->final_command && (req_it->commands_issued >= 1 + req_it->bundled_callbacks.size())) {
          if (req_it->type_id == Request::Type::Read) {
            if(debug)
              fmt::print("[MEMORY CONTROLLER] Finishing read request: {:#x} promotion: {} prefetch: {} dropped: {} cycle: {}\n",req_it->addr, req_it->was_promoted, req_it->is_prefetch, req_it->was_dropped, m_clk);
            req_it->depart = m_clk + m_dram->m_read_latency;
            pending.push_back(*req_it);
          } else if (req_it->type_id == Request::Type::Write) {
            // TODO: Add code to update statistics
            if(debug)
              fmt::print("[MEMORY CONTROLLER] Finishing write request: {:#x} promotion: {} prefetch: {} dropped: {} cycle: {}\n",req_it->addr, req_it->was_promoted, req_it->is_prefetch, req_it->was_dropped, m_clk);
          }
          buffer->remove(req_it);
        } else {
          if(debug)
            fmt::print("[MEMORY CONTROLLER] Issuing command for request: {:#x} promotion: {} prefetch: {} dropped: {} cycle: {}\n",req_it->addr, req_it->was_promoted, req_it->is_prefetch, req_it->was_dropped, m_clk);
          if (m_dram->m_command_meta(req_it->command).is_opening) {
            if(debug)
              fmt::print("[MEMORY CONTROLLER] Opening row for request: {:#x} promotion: {} prefetch: {} dropped: {} cycle: {}\n",req_it->addr, req_it->was_promoted, req_it->is_prefetch, req_it->was_dropped, m_clk);
            bool success = m_active_buffer.enqueue(*req_it);
              if(debug)
                fmt::print("[MEMORY CONTROLLER] Opening row for request: {:#x} promotion: {} prefetch: {} dropped: {} cycle: {} success: {}\n",req_it->addr, req_it->was_promoted, req_it->is_prefetch, req_it->was_dropped, m_clk,success);
            if(success)
              buffer->remove(req_it);
          }
        }
      }

      //heartbeat
      /*
      if((m_clk + 1) % 1000000 == 0) {
        fmt::print("[MEMORY CONTROLLER] RQ occu: {} WQ occu: {}\n",m_read_buffer.size(),m_write_buffer.size());
        for(int i = 0; i < m_read_core_count.size(); i++) {
          fmt::print("\t{} Critical Reads Enqueued: {} Total: {}\n",i,m_read_core_count[i],m_read_core_total[i]);
        }
        for(int i = 0; i < m_write_core_count.size(); i++) {
          fmt::print("\t{} Critical Writes Enqueued: {} Total: {}\n",i,m_write_core_count[i],m_write_core_total[i]);
        }
      }*/

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
            for(int i = 0; i < req.bundled_callbacks.size(); i++)
              req.bundled_callbacks.at(i).second(req);
          }
          // Finally, remove this request from the pending queue
          if(debug)
          fmt::print("[MEMORY CONTROLLER] Returning packet {:#x} prefetch: {} promotion: {} dropped: {} cycle: {}\n",req.addr,req.is_prefetch,req.was_promoted,req.was_dropped,m_clk);
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

      // 2.05   Search for prefetches to drop.
      for (auto it = m_read_buffer.begin(); it != m_read_buffer.end(); it++) {
        if(it->is_prefetch && m_drop_enabled) {
          uint64_t age = m_clk - it->arrive;
          bool drop = false;
          double usefulness = m_core_usefulness[std::pair{it->source_ptr,it->source_id}];
          for(int i = 0; i < 4; i++) {
            if(usefulness < m_drop_thresholds[i] && age > m_drop_cycles[i]) {
              drop = true;
              break;
            }
          }
          if(drop) {
            if(debug)
              fmt::print("[MEMORY CONTROLLER] Dropping packet {:#x} prefetch: {} promotion: {} dropped: {} cycle: {}\n",it->addr,it->is_prefetch,it->was_promoted,it->was_dropped,m_clk);
            it->was_dropped = true;
            it->depart = m_clk + 1;
            pending.push_back(*it);
            m_read_buffer.remove(it);
            s_prefetches_dropped++;
            break;
          }
        }
        if(it->should_drop && it->is_prefetch) {
          it->was_dropped = true;
          s_dropped_by_scheduler[it->source_id]++;
          it->depart = m_clk + 1;
          pending.push_back(*it);
          m_read_buffer.remove(it);
          s_prefetches_dropped++;
          break;
        }
      }
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
            request_found = m_dram->check_ready(req_it->command, req_it->addr_vec);
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

    public:
      void set_prefetch_usefulness(void* source_ptr, int core, double usefulness) {
        m_core_usefulness[std::pair{source_ptr,core}] = usefulness;
      }
      bool is_core_critical(void* source_ptr, int source_id) override {
        auto entry = m_core_usefulness.find(std::pair{source_ptr,source_id});
        if(entry == std::end(m_core_usefulness))
          return true;
        return entry->second >= m_prom_threshold;
      }
      int get_core_occupancy(int source_id, bool write) override {
        return write ? m_write_core_count[source_id] : m_read_core_count[source_id];
      }

};

std::vector<DRACController*> DRACController::drac_controllers;

void set_core_prefetch_usefulness(void* source_ptr, int core, double usefulness) {
  for(auto& it : Ramulator::DRACController::drac_controllers)
    it->set_prefetch_usefulness(source_ptr, core, usefulness);
}
}   // namespace Ramulator


