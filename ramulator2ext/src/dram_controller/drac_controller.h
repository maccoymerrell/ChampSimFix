#ifndef RAMULATOR_CONTROLLER_DRACCONTROLLER_H
#define RAMULATOR_CONTROLLER_DRACCONTROLLER_H

#include <vector>
#include <deque>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/base.h"
#include "dram/dram.h"
#include "dram_controller/bh_scheduler.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"
#include "dram_controller/refresh.h"

namespace Ramulator {

class IDRACController : public IDRAMController {
  RAMULATOR_REGISTER_INTERFACE(IDRACController, "DRACController", "DRAC Memory Controller Interface");

  public:
    IBHScheduler* m_scheduler = nullptr;
    virtual void tick() = 0;

    template <class T>
    T* get_plugin() {
      for (auto plugin : m_plugins) {
        T* cast = dynamic_cast<T*>(plugin);
        if (cast) {
          return cast;
        }
      }
      return nullptr;
    }

    virtual bool is_core_critical(void* source_ptr, int source_id) {return true;}
    virtual int get_core_occupancy(int source_id, bool write) {return 0;}


  protected:
    std::vector<IControllerPlugin*> m_plugins;
};

}       // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_LIVECONTROLLER_H