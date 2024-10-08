#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <math.h>
#include <random>
#include <string>
#include <vector>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"
#include "addr_mapper/addr_mapper.h"
#include "memory_system/memory_system.h"
#include "dram_controller/bh_controller.h"

#define RH_READ 0
#define RH_WRITE 1
#define RH_REFRESH 2
class Address
{
public:
  // channel
  // bank
  // rank
  // row
  std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> addr;

  Address(uint64_t channel, uint64_t bank, uint64_t rank, uint64_t row) { addr = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>(channel, bank, rank, row); }

  uint64_t get_row() const { return (std::get<3>(addr)); }

  uint64_t get_rank() const { return (std::get<2>(addr)); }

  uint64_t get_bank() const { return (std::get<1>(addr)); }

  uint64_t get_channel() const { return (std::get<0>(addr)); }

  bool operator<(const Address& a) const { return (addr < a.addr); }
};