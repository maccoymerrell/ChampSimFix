"""
One NMFC memory tile's DRAM: a single HBM3 pseudo-channel.

The interesting case for the adapter. HBM3's pseudo-channel is 32 bits wide, so
a DRAM transaction is smaller than a 64 B cache block and RAMULATOR_MC has to
split each block into several transactions -- ramulator refuses an oversized
request rather than splitting it itself. DDR4 and DDR5 both land on 64 B and
hide that requirement entirely.

Export with:
    python -m ramulator export config/nmfc/ramulator/tile_hbm3.py -o tile_hbm3.yaml
"""

import ramulator

frontend = ramulator.frontend.External(clock_ratio=4)

hbm3 = ramulator.dram.HBM3(org_preset="HBM3_16Gb_8hi", timing_preset="HBM3_6400Mbps")

ctrl = ramulator.controller.GenericDDR(
    dram=hbm3,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)

mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=1,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

sim = ramulator.Simulation(frontend, mem)
sim.run()
