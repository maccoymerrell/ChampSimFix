#ifndef WRONG_PATH_TRACEREADER_ENCODINGS_H
#define WRONG_PATH_TRACEREADER_ENCODINGS_H

#include <cstdint>
#include <map>
#include <set>

namespace champsim
{
  namespace wrong_path_trace_constants
  {
    const uint32_t magic_bytes = 0x1d545343;
    const std::map<std::string, std::set<std::string>> required_encodings
    {
      {"header_flag", {"CST_FLAG_PROFILE", "CST_FLAG_WP"}},
      {"insn_flag", {"CST_INSN_FLAG_HAS_IMM", "CST_INSN_FLAG_HAS_DEP_BLOCK"}},
      {"body_tag", {"BODY_TAG_END", "BODY_TAG_ENTRY", "BODY_TAG_THREAD_SWITCH"}},
    };
  } // namespace wrong_path_trace_constants
} // namespace champsim

#endif
