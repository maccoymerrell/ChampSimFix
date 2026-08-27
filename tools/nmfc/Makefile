# Standalone: the generator shares only inc/nmfc/nmfc_trace.h with the
# simulator, and nothing else, so it builds without ChampSim's dependencies.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

all: nmfc_gen

nmfc_gen: nmfc_gen.cc ../../inc/nmfc/nmfc_trace.h
	$(CXX) $(CXXFLAGS) -o $@ nmfc_gen.cc

clean:
	$(RM) nmfc_gen

.PHONY: all clean

# The GAPBS-driven generator. Needs ext/gapbs (see README); OpenMP is off because
# a trace must be a total order and two threads' bodies would interleave.
GAPBS ?= ../../ext/gapbs/src
gapbs_trace: gapbs_trace.cc gapbs_hooks.h ../../inc/nmfc/nmfc_trace.h
	$(CXX) $(CXXFLAGS) -I$(GAPBS) -I../../inc/nmfc -o $@ gapbs_trace.cc

# GAPBS's own BFS with the hooks in it. Needs ext/gapbs patched by
# tools/nmfc/build_gapbs.sh. OpenMP is enabled because the kernel uses it, but
# run with OMP_NUM_THREADS=1: a trace is a total order.
gapbs_bfs: gapbs_bfs.cc gapbs_hooks.h ../../inc/nmfc/nmfc_trace.h
	$(CXX) $(CXXFLAGS) -fopenmp -I$(GAPBS) -I../../inc/nmfc -o $@ gapbs_bfs.cc
