"""
One NMFC memory tile's DRAM: a single DDR5 channel.

Each memory tile owns exactly one channel, so tile selection happens entirely in
the interleave fabric and each controller here sees a single-channel machine.
The frontend is `External`, which is ramulator2's hook for a host simulator
pushing requests in -- ChampSim's RAMULATOR_MC drives it.

Export with:
    python -m ramulator export config/nmfc/ramulator/tile_ddr5.py -o tile_ddr5.yaml
"""

import ramulator

frontend = ramulator.frontend.External(clock_ratio=8)

ddr5 = ramulator.dram.DDR5(org_preset="DDR5_16Gb_x8", timing_preset="DDR5_4800AN", rank=2)

ctrl = ramulator.controller.GenericDDR(
    dram=ddr5,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)

mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

sim = ramulator.Simulation(frontend, mem)
sim.run()
