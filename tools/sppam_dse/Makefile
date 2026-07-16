# Standalone SPPAM design-space-exploration evaluator.
CXX      ?= g++
NLOHMANN ?= ../../vcpkg_installed/x64-linux/include
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -I$(NLOHMANN)

BIN      := sppam_dse
SRCS     := main.cc
HDRS     := $(wildcard *.h)

.PHONY: all clean
all: $(BIN)

$(BIN): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(BIN) validate

accuracy_eval: accuracy_main.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -o accuracy_eval accuracy_main.cc

pc_sppam_eval: pc_sppam_eval.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -o pc_sppam_eval pc_sppam_eval.cc

# AMPM baseline in the DSE harness/metrics (coverage/accuracy comparison vs SPPAM).
ampm_eval: ampm_eval.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -o ampm_eval ampm_eval.cc

# Physical block->block edge-recurrence analyzer (temporal/graph-edge prefetch feasibility).
edge_recurrence: edge_recurrence.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -o edge_recurrence edge_recurrence.cc

# Per-address deep dive: runs AMPM + SPPAM in parallel, dumps each engine's prediction stages for
# blocks AMPM covers but SPPAM does not (the tool that pinned the forward-only backward-blindness).
mcf_deepdive: mcf_deepdive.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -o mcf_deepdive mcf_deepdive.cc

# Holistic instruction+data DSE: merges l1d + l1i by cycle into ONE shared L2, runs the real SPPAM data
# prefetcher + real branch-graph instruction prefetcher SHARING one residency filter (SPPAM region
# prefetch_map) and region table; BG keeps only its private BTB + xlate. Answers instr_feed_data / BG
# sizing / SPPAM-as-BG-spatial-fallthrough + I/D contention.  Run: ./holistic_id l1d.zst l1i.zst [maxdata] [bg_entries] [bg_xlate]
holistic_id: holistic_id.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -o holistic_id holistic_id.cc

# Instruction-spatial FALLTHROUGH comparison (on the holistic shared-L2 setup): lightweight dedicated
# instruction prefetchers (next-N / BOP / tiny-SPPAM) sharing only the residency filter, vs the full-SPPAM
# upper bound. Finds the cheapest capture of the sequential code residual the branch graph misses.
instr_ft: instr_ft.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -o instr_ft instr_ft.cc

# Branch-graph residency + prefetch-mode study: next-N vs BB-run-length, and shared region-map residency vs a
# DECOUPLED physical residency filter (untagged bit vector / tagged, set post-translation, cleared on L2 evict).
# Reports redundancy / filter-suppression / instruction accuracy / data cross-pressure.
bg_residency: bg_residency.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -o bg_residency bg_residency.cc
