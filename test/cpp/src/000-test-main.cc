#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "champsim.h"
#include "util/bits.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
std::size_t NUM_CPUS = 1;

unsigned BLOCK_SIZE = 64;
unsigned PAGE_SIZE = 4096;
unsigned LOG2_BLOCK_SIZE = champsim::lg2(64u);
unsigned LOG2_PAGE_SIZE = champsim::lg2(4096u);
#pragma GCC diagnostic pop
