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
