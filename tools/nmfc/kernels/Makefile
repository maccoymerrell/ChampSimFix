# Build kernels so the compiler decides the instructions and the linker decides
# the addresses -- and so neither can quietly undo that.
#
#   -no-pie                    traced program counters equal the symbol table's
#   -ffunction-sections        every function gets its own section to place
#   -fno-ipa-icf               do NOT fold two identical functions into one
#                              symbol; that is exactly the bug being fixed
#   -fno-optimize-sibling-calls keep the return as a real ret instruction
#   -fno-omit-frame-pointer    keep call structure legible in the disassembly
#   -fopenmp                   REQUIRED: GAPBS substitutes serial fallbacks for
#                              its atomics under #if defined _OPENMP, so
#                              building without it compiles the compare-and-swap
#                              into a plain load and store. Run OMP_NUM_THREADS=1
#                              to keep a single thread for the tracer.
GAPBS ?= ../../../ext/gapbs/src
CXX   ?= g++
CXXFLAGS := -std=c++17 -O2 -g -I$(GAPBS) -I. \
            -no-pie -ffunction-sections -fno-ipa-icf \
            -fno-optimize-sibling-calls -fno-omit-frame-pointer \
            -fopenmp -Wno-unknown-pragmas
LDFLAGS  := -no-pie -fopenmp

.PHONY: all clean
all: bfs_nmfc

bfs_nmfc: bfs_nmfc.cc nmfc_kernels.h
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f bfs_nmfc
