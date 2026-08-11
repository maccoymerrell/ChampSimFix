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
const std::map<std::string, std::set<std::string>> required_encodings{
    {"header_flag", {"CST_FLAG_PROFILE", "CST_FLAG_WP", "CST_FLAG_FAULT"}},
    {"insn_flag", {"CST_INSN_FLAG_HAS_IMM", "CST_INSN_FLAG_HAS_DEP_BLOCK"}},
    {"body_tag", {"BODY_TAG_END", "BODY_TAG_ENTRY", "BODY_TAG_THREAD_SWITCH", "BODY_TAG_ASID_SWITCH"}}};
const std::map<std::string, std::set<std::string>> optional_encodings{{"body_tag", {"BODY_TAG_IFRAME", "BODY_TAG_REGFILE"}},
                                                                      {"dep_block_flag", {"CST_DEP_BLOCK_HAS_REG", "CST_DEP_BLOCK_HAS_ADDR"}},
                                                                      {"wp_event_flag", {"CST_WP_EVENT_FAULT"}},
                                                                      {"wp_chain_flag", {"CST_WP_CHAIN_HAS_EVENTS"}},
                                                                      {"field_id", {"CST_FID_EXTENDED"}}};
} // namespace wrong_path_trace_constants
} // namespace champsim

#endif
