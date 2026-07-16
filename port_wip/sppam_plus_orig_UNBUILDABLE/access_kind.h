// Access type shared by the trace reader and the predictor.
#ifndef SPPAM_DSE_ACCESS_KIND_H
#define SPPAM_DSE_ACCESS_KIND_H

#include <cstdint>

namespace sppam_dse
{
enum class atype : uint8_t { LOAD = 0, RFO = 1, PREFETCH = 2, WRITE = 3, TRANSLATION = 4 };
}

#endif
