add_library(ramulator-controller OBJECT)

target_sources(
  ramulator-controller PRIVATE
  bh_controller.h 
  drac_controller.h
  bh_scheduler.h 
  controller.h 
  scheduler.h 
  plugin.h
  refresh.h
  rowpolicy.h

  impl/bh_dram_controller.cpp
  impl/bliss_dram_controller.cpp
  impl/dummy_controller.cpp
  impl/generic_dram_controller.cpp
  impl/prac_dram_controller.cpp
  impl/pra_dram_controller.cpp
  impl/drac_controller.cpp
  
  impl/scheduler/bh_scheduler.cpp
  impl/scheduler/blocking_scheduler.cpp
  impl/scheduler/generic_scheduler.cpp
  impl/scheduler/bliss_scheduler.cpp
  impl/scheduler/prac_scheduler.cpp
  impl/scheduler/pra_scheduler.cpp
  impl/scheduler/drac_scheduler.cpp
  impl/scheduler/minimalist_scheduler.cpp

  impl/refresh/all_bank_refresh.cpp
  
  impl/rowpolicy/basic_rowpolicies.cpp
  impl/rowpolicy/minimalist_rowpolicy.cpp
  impl/rowpolicy/adaptive_rowpolicy.cpp

  impl/plugin/trace_recorder.cpp
  impl/plugin/cmd_counter.cpp
  impl/plugin/para.cpp
  impl/plugin/para_pf.cpp
  impl/plugin/graphene.cpp
  impl/plugin/oracle_rh.cpp
  impl/plugin/twice.cpp
  impl/plugin/hydra.cpp
  impl/plugin/hydra_pf.cpp
  impl/plugin/rrs.cpp
  impl/plugin/aqua.cpp
  impl/plugin/rfm_manager.cpp
  impl/plugin/rfm_manager_pf.cpp
  impl/plugin/champsim_plugin.cpp
  impl/plugin/hammer_counter.cpp
  impl/plugin/activate_counter.cpp
  impl/plugin/back_off.cpp

  impl/plugin/blockhammer/blockhammer_throttler.h 
  impl/plugin/blockhammer/blockhammer_util.h 
  impl/plugin/blockhammer/blockhammer.cpp 
  impl/plugin/blockhammer/blockhammer.h 

  impl/plugin/device_config/device_config.cpp 
  impl/plugin/device_config/device_config.h 

  impl/plugin/bliss/bliss.cpp 
  impl/plugin/bliss/bliss.h 

  impl/plugin/prac/prac.cpp 
  impl/plugin/prac/prac.h 
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-controller
)
