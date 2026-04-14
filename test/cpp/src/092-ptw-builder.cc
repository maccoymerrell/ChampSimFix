#include <catch.hpp>

#include "channel.h"
#include "dram_controller.h"
#include "ptw.h"
#include "vmem.h"
#include "defaults.hpp"

namespace
{
auto make_dram()
{
  return champsim::modules::ModuleBuilder{"test_dram", "default_memory_controller", champsim::defaults::default_memory_controller()};
}

auto make_vmem(MEMORY_CONTROLLER& dram)
{
  auto builder = champsim::modules::ModuleBuilder{"test_vmem", "default_vmem", champsim::defaults::default_vmem()};
  builder.add_parameter("dram", static_cast<champsim::modules::memory_controller_module*>(&dram));
  return builder;
}
} // namespace

TEST_CASE("The PTW's MSHR size can be specified")
{
  MEMORY_CONTROLLER dram{make_dram()};
  VirtualMemory vmem{make_vmem(dram)};

  auto num_mshrs = GENERATE(4u, 8u, 16u);
  champsim::modules::ModuleBuilder ptw_builder{"test_ptw_92_0", "default_ptw", champsim::defaults::default_ptw()};
  ptw_builder.add_parameter("mshr_size", static_cast<uint32_t>(num_mshrs));
  ptw_builder.add_parameter("vmem", static_cast<champsim::modules::vmem_module*>(&vmem));

  PageTableWalker uut{ptw_builder};

  REQUIRE(uut.MSHR_SIZE == num_mshrs);
}

TEST_CASE("The PTW's tag and fill bandwidth can be specified")
{
  MEMORY_CONTROLLER dram{make_dram()};
  VirtualMemory vmem{make_vmem(dram)};

  champsim::modules::ModuleBuilder ptw_builder{"test_ptw_92_1", "default_ptw", champsim::defaults::default_ptw()};
  ptw_builder.add_parameter("max_tag_check", champsim::bandwidth::maximum_type{6});
  ptw_builder.add_parameter("max_fill", champsim::bandwidth::maximum_type{7});
  ptw_builder.add_parameter("vmem", static_cast<champsim::modules::vmem_module*>(&vmem));

  PageTableWalker uut{ptw_builder};

  CHECK(uut.MAX_READ == champsim::bandwidth::maximum_type{6});
  CHECK(uut.MAX_FILL == champsim::bandwidth::maximum_type{7});
}
