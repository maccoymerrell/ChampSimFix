#ifndef     RAMULATOR_DRACMEMORYSYSTEM_MEMORY_H
#define     RAMULATOR_DRACMEMORYSYSTEM_MEMORY_H

#include "memory_system/memory_system.h"
#include "dram/dram.h"

namespace Ramulator {

class IDRACMemorySystem : public IMemorySystem {
  RAMULATOR_REGISTER_INTERFACE(IDRACMemorySystem, "DRACMemorySystem", "DRAC Memory system interface (e.g., communicates between processor and memory controller).")
  public:
    virtual IDRAM* get_dram() { return nullptr; }
};

}        // namespace Ramulator


#endif   // RAMULATOR_BHMEMORYSYSTEM_MEMORY_H