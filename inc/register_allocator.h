#include <algorithm>
#include <array>
#include <cstdint>
#include <list>
#include <optional>
#include <queue>

#ifndef REG_ALLOC_H
#define REG_ALLOC_H

#include "instruction.h"

struct physical_register {
  uint16_t arch_reg_index;
  uint64_t producing_instruction_id;
  bool valid; // has the producing instruction committed yet?
  bool busy;  // is this register in use anywhere in the pipeline?
};

class RegisterAllocator
{
private:
  std::array<PHYSICAL_REGISTER_ID, std::numeric_limits<uint8_t>::max() + 1> frontend_RAT, backend_RAT;
  std::queue<PHYSICAL_REGISTER_ID> free_registers;
  std::vector<physical_register> physical_register_file;

public:
  RegisterAllocator(size_t num_physical_registers);
  PHYSICAL_REGISTER_ID rename_dest_register(int16_t reg, champsim::program_ordered<ooo_model_instr>::id_type producer_id);
  PHYSICAL_REGISTER_ID rename_src_register(int16_t reg);
  void retire_dest_register(PHYSICAL_REGISTER_ID physreg);
  void free_register(PHYSICAL_REGISTER_ID physreg);
  void reset_frontend_RAT();
  void print_deadlock();

  // Hot accessors, defined inline: the scheduler and execute stages poll
  // these per candidate instruction per cycle; out-of-line definitions cost
  // a real function call per poll.
  void complete_dest_register(PHYSICAL_REGISTER_ID physreg) { physical_register_file.at(physreg).valid = true; }

  bool isValid(PHYSICAL_REGISTER_ID physreg) const
  {
    if (physreg < 0 || static_cast<size_t>(physreg) >= physical_register_file.size())
      return false;
    return physical_register_file[physreg].valid;
  }

  bool isAllocated(PHYSICAL_REGISTER_ID archreg) const { return frontend_RAT[archreg] != -1; }

  unsigned long count_free_registers() const { return std::size(free_registers); }

  int count_reg_dependencies(const ooo_model_instr& instr) const
  {
    return static_cast<int>(std::count_if(std::begin(instr.source_registers), std::end(instr.source_registers), [this](auto reg) { return !isValid(reg); }));
  }
};
#endif
