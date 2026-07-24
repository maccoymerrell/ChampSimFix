/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef WRONG_PATH_TRACEREADER_H
#define WRONG_PATH_TRACEREADER_H

#warning "compiling with debug statements"
// TODO: Remove the debug prints

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <libtar.h>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <boost/bimap.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include "inf_stream.h"
#include "instruction.h"
#include "wrong_path_tracereader_verification_constants.h"

namespace champsim
{
class wrong_path_tracereader
{
  uint8_t cpu;
  std::string trace_file;
  std::filesystem::path trace_extract_dir;
  bool moved = false;

  void cleanup() const { std::filesystem::remove_all(trace_extract_dir); }

  void parse_trace();
  void verify_trace_file_type() const;
  [[nodiscard]] std::filesystem::path create_extract_dir() const;
  void extract_trace() const;
  [[nodiscard]] std::filesystem::path get_header_path() const;
  [[nodiscard]] std::filesystem::path get_body_path() const;
  void construct_header_stream();
  void construct_body_stream();

  // File type verification utilities
  static constexpr char tar_magic[5] = {'u', 's', 't', 'a', 'r'};
  static constexpr std::size_t tar_magic_position = 257;
  static constexpr char gzip_magic[2] = {'\x1f', '\x1b'};
  static constexpr std::size_t gzip_magic_position = 0;
  static constexpr char lzma_magic[6] = {'\xfd', '\x37', '\x7a', '\x58', '\x5a', '\x00'};
  static constexpr std::size_t lzma_magic_position = 0;
  static constexpr char bzip2_magic[2] = {'\x42', '\x5a'};
  static constexpr std::size_t bzip2_magic_position = 0;
  static constexpr char zstd_magic[4] = {'\x28', '\xb5', '\x2f', '\xfd'};
  static constexpr std::size_t zstd_magic_position = 0;
  static constexpr char lz4_magic[4] = {'\x04', '\x22', '\x4d', '\x18'};
  static constexpr std::size_t lz4_magic_position = 0;

  template <const char* magic, const std::size_t magic_length, const std::size_t position>
  bool check_file_type(const std::string& file_name) const;

  std::function<bool(const std::string&)> is_tar = [this] [[nodiscard]] (const std::string& file_name) -> bool {
    return check_file_type<tar_magic, sizeof(tar_magic), tar_magic_position>(file_name);
  };
  std::function<bool(const std::string&)> is_gzip = [this] [[nodiscard]] (const std::string& file_name) -> bool {
    return (file_name.substr(std::size(file_name) - 2) == "gz") && check_file_type<gzip_magic, sizeof(gzip_magic), gzip_magic_position>(file_name);
  };
  std::function<bool(const std::string&)> is_lzma = [this] [[nodiscard]] (const std::string& file_name) -> bool {
    return (file_name.substr(std::size(file_name) - 2) == "xz") && check_file_type<lzma_magic, sizeof(lzma_magic), lzma_magic_position>(file_name);
  };
  std::function<bool(const std::string&)> is_bzip2 = [this] [[nodiscard]] (const std::string& file_name) -> bool {
    return (file_name.substr(std::size(file_name) - 3) == "bz2") && check_file_type<bzip2_magic, sizeof(bzip2_magic), bzip2_magic_position>(file_name);
  };
  std::function<bool(const std::string&)> is_zstd = [this] [[nodiscard]] (const std::string& file_name) -> bool {
    return (file_name.substr(std::size(file_name) - 3) == "zst") && check_file_type<zstd_magic, sizeof(zstd_magic), zstd_magic_position>(file_name);
  };
  std::function<bool(const std::string&)> is_lz4 = [this] [[nodiscard]] (const std::string& file_name) -> bool {
    return (file_name.substr(std::size(file_name) - 3) == "lz4") && check_file_type<lz4_magic, sizeof(lz4_magic), lz4_magic_position>(file_name);
  };

  // This class is responsible for decompressing the compressed streams and returning the decompressed data
  template <typename CompressedStreamType, std::size_t buffer_size = 4096>
  class stream_reader
  {
    CompressedStreamType stream;
    std::array<char, buffer_size> decompressed_buffer; // This buffer temporarily the decompressed bytes
    decltype(decompressed_buffer.end()) buffer_iter = decompressed_buffer.begin();
    decltype(decompressed_buffer.end()) max_buffer_iter = decompressed_buffer.begin();

    // Returns the next byte from the input stream
    [[nodiscard]] std::optional<char> decompress() noexcept
    {
      // No more bytes left
      if ((buffer_iter >= max_buffer_iter) && stream.eof()) {
        eof = true;
        return {};
      }

      // No need to decompress more. We can return from the buffer
      if (buffer_iter < max_buffer_iter) {
        char front = *buffer_iter;
        buffer_iter++;
        total_bytes_read++;
        if (buffer_iter >= max_buffer_iter)
          eof = true;

        return {front};
      }

      // The buffer is empty. Re-fill the buffer
      decompressed_buffer.fill(0x00);
      stream.read(std::data(decompressed_buffer), decompressed_buffer.size());
      const auto bytes_read = static_cast<std::size_t>(stream.gcount());
      if (bytes_read == 0) {
        eof = true;
        return {};
      }
      buffer_iter = decompressed_buffer.begin();
      max_buffer_iter = decompressed_buffer.begin() + bytes_read;

      // Return the next byte
      char front = *buffer_iter;
      buffer_iter++;
      total_bytes_read++;
      return {front};
    }

    [[nodiscard]] uint64_t uleb_decoder(std::vector<char>& chunks) const noexcept
    {
      uint64_t retval = 0;
      uint64_t chunk_id = 0;
      for (char& byte : chunks) {
        byte &= 0x7f; // The MSB is used for encoding and is not part of the payload
        uint64_t chunk = static_cast<uint64_t>(byte);
        chunk <<= chunk_id * 7; // Shift the chunk bits into their right place
        retval |= chunk;        // Add the bits to the decoded value
        chunk_id++;
      }
      return retval;
    }

    [[nodiscard]] int64_t sleb_decoder(std::vector<char>& chunks) const noexcept
    {
      int64_t retval = 0;
      uint64_t chunk_id = 0;
      for (char& byte : chunks) {
        byte &= 0x7f; // The MSB is used for encoding and is not part of the payload
        int64_t chunk = 0;
        std::memcpy(&chunk, &byte, sizeof(char)); // Safely type cast. Copy over the bits without sign extension
        chunk <<= chunk_id * 7;                   // Shift the chunk bits into their right place
        retval |= chunk;                          // Add the bits to the decoded value
        chunk_id++;
      }

      // Perform sign extension
      const bool sign_bit = ((chunks.back() & 0x7f) >> 6); // The last element of chunks holds the MSB byte
      uint64_t sign_extension_mask = sign_bit ? 0xffffffff'ffffffff : 0x00000000'00000000;
      sign_extension_mask >>= chunk_id * 7;
      sign_extension_mask <<= chunk_id * 7;
      retval |= sign_extension_mask;

      return retval;
    }

    // Parses a SLEB_WIDE into std::bitset
    [[nodiscard]] std::bitset<512> sleb_wide_decoder(const std::vector<char>& chunks) const noexcept
    {
      std::size_t bit_idx = 0;
      const bool msb = static_cast<bool>(chunks.back() & 0x40);

      std::bitset<512> parsed_bits;
      parsed_bits.reset();
      for (auto i = chunks.rbegin(); i != chunks.rend(); i++) {
        const uint8_t chunk = static_cast<uint8_t>(*i & 0x7f);
        parsed_bits <<= 7;
        parsed_bits |= chunk;
        bit_idx += 7;
      }
      while (bit_idx < 512)
        parsed_bits[bit_idx++] = msb; // Sign extension

      return parsed_bits;
    }

  public:
    uint64_t total_bytes_read = 0; // Number of bytes read from the stream so far
    bool eof = false;              // Set to true if no more bytes can be returned
    stream_reader(const std::string& stream_name) noexcept : stream(stream_name) {}

    [[nodiscard]] char read()
    {
      const auto byte = decompress();
      if (!byte)
        throw std::runtime_error("[ERROR] Ran out of compressed stream. Exiting...");

      return byte.value();
    }

    // Reads a ULEB from the stream
    [[nodiscard]] uint64_t parse_uleb()
    {
      std::vector<char> chunks;
      while (auto byte = decompress()) {
        chunks.emplace_back(byte.value());
        if (!(byte.value() & 0x80))
          break;
      }

      if (chunks.size() > 10)
        throw std::runtime_error("[ERROR] ULEB of more than 10 bytes detected");
      if (chunks.size() == 0)
        throw std::runtime_error("[ERROR] Empty ULEB detected");

      return uleb_decoder(chunks);
    }

    // Reads a SLEB from the stream
    [[nodiscard]] uint64_t parse_sleb()
    {
      std::vector<char> chunks;
      while (auto byte = decompress()) {
        chunks.emplace_back(byte.value());
        if (!(byte.value() & 0x80))
          break;
      }

      if (chunks.size() > 10)
        throw std::runtime_error("[ERROR] SLEB of more than 10 bytes detected");
      if (chunks.size() == 0)
        throw std::runtime_error("[ERROR] Empty SLEB detected");

      return sleb_decoder(chunks);
    }

    // Reads a SLEB_WIDE from the stream
    [[nodiscard]] std::bitset<512> parse_sleb_wide()
    {
      std::vector<char> chunks;
      while (auto byte = decompress()) {
        chunks.emplace_back(byte.value());
        if (!(byte.value() & 0x80))
          break;
      }

      if (chunks.size() > 74)
        throw std::runtime_error("[ERROR] SLEB_WIDE of more than 74 bytes detected");
      if (chunks.size() == 0)
        throw std::runtime_error("[ERROR] Empty SLEB_WIDE detected");

      return sleb_wide_decoder(chunks);
    }

    // Reads a string from the stream
    [[nodiscard]] std::string parse_string()
    {
      const std::size_t string_size = static_cast<std::size_t>(parse_uleb());
      std::string retval = "";
      for (std::size_t i = 0; i < string_size; i++)
        retval += read();
      return retval;
    }

    // Reads a chunk of bytes from the stream
    [[nodiscard]] std::vector<char> read_bytes(const std::size_t num_bytes)
    {
      std::vector<char> retvec;
      for (std::size_t bytes_read = 0; bytes_read != num_bytes; bytes_read++)
        retvec.emplace_back(read());
      return retvec;
    }

    // Reads a f64 from the stream
    [[nodiscard]] double parse_f64()
    {
      double retval = 0.0f;
      const auto bytes = read_bytes(8);
      std::memcpy(&retval, bytes.data(), 8);
      return retval;
    }
  };

  // Forward declare body_parser
  template <typename>
  class body_parser;

  // Type erased wrapper for parsing the header. Type erasure is necessary since the concrete type depends on the compression format, which is only known at
  // runtime
  class header_wrapper
  {
    // Grant friendship to all instances of the body_parser template
    template <typename>
    friend class body_parser;

  protected:
    struct Prolog {
      struct FixedSize {
        uint32_t magic_bytes;
        uint8_t isa;
        uint8_t flags;
      } fixed_size;

      uint64_t start_instructions;
      uint64_t warmup_instructions;
      uint64_t total_target_instructions;
      double simpoint_weight;

      std::string command;
      std::string datetime;
      std::string comment;
      std::string target_name;
    } prolog;

    struct Dependency {
      uint8_t dep_block_flags;
      std::vector<uint64_t> dst_dep;
      std::vector<uint64_t> store_data_dep;
      std::vector<uint64_t> load_addr_dep;
      std::vector<uint64_t> store_addr_dep;
    };

    struct Instruction {
      uint64_t pc_delta;
      uint8_t opcode;
      uint8_t branch_type;
      uint8_t flags;
      uint8_t n_src;
      uint8_t n_dst;
      std::vector<uint8_t> src_regs;
      std::vector<uint8_t> dst_regs;
      uint8_t max_dep_loads;
      uint8_t max_dep_stores;
      std::optional<int64_t> immidiate;
      uint8_t instr_size;
      std::vector<char> instr_bytes;
      std::optional<Dependency> dependency;
    };

    struct Profile {
      uint64_t exec_cp;
      uint64_t exec_wp;
      std::vector<uint64_t> taken_cp;
      std::vector<uint64_t> nottaken_cp;
      std::vector<uint64_t> taken_wp;
      std::vector<uint64_t> nottaken_wp;
      std::vector<uint64_t> memops_cp;
      std::vector<uint64_t> memops_wp;
      std::vector<uint64_t> pat_flags;
      std::vector<std::optional<uint64_t>> lo_addr_cp;
      std::vector<std::optional<uint64_t>> hi_addr_cp;
      std::vector<std::optional<uint64_t>> lo_addr_wp;
      std::vector<std::optional<uint64_t>> hi_addr_wp;
    };

    struct Template {
      uint64_t start_pc;
      uint64_t num_instr;
      uint64_t fall_through_pc;
      uint64_t num_targets;
      std::vector<uint64_t> targets;
      std::string symbol_name;
      std::vector<Instruction> instructions;
      std::optional<Profile> profile;
    };

    typedef boost::bimap<std::string, uint64_t> bimap_type;
    typedef bimap_type::value_type ids_value_type;
    std::map<std::string, bimap_type> ids;
    std::map<uint32_t, Template> templates;

  public:
    virtual void parse() = 0;
    virtual ~header_wrapper() = default;
  };

  template <typename HeaderCompressedType>
  class header_parser : public header_wrapper
  {
  private:
    stream_reader<HeaderCompressedType> compressed_header_stream;

    void parse_fixed_size_prolog()
    {
      const std::size_t bytes_to_read = sizeof(prolog.fixed_size.magic_bytes) + sizeof(prolog.fixed_size.isa) + sizeof(prolog.fixed_size.flags);

      const auto buffer = compressed_header_stream.read_bytes(bytes_to_read);
      std::memcpy(&(prolog.fixed_size), std::data(buffer), bytes_to_read);

      fmt::print(stderr, "Magic = {:X}\nISA = {:X}\nFlags = {:#010b}\n", prolog.fixed_size.magic_bytes, prolog.fixed_size.isa, prolog.fixed_size.flags);

      using namespace wrong_path_trace_constants;
      if (prolog.fixed_size.magic_bytes != magic_bytes)
        throw std::runtime_error("[ERROR] Can't verify header integrity. Magic bytes don't match");
    }

    void parse_variable_size_prolog()
    {
      prolog.start_instructions = compressed_header_stream.parse_uleb();
      prolog.warmup_instructions = compressed_header_stream.parse_uleb();
      prolog.total_target_instructions = compressed_header_stream.parse_uleb();
      prolog.simpoint_weight = compressed_header_stream.parse_f64();

      fmt::print(stderr,
                 "Start Instructions = {}\nWarmup Instructions = {}\n"
                 "Total Target Instructions = {}\nSimPoint Weight = {}\n",
                 prolog.start_instructions, prolog.warmup_instructions, prolog.total_target_instructions, prolog.simpoint_weight);
    }

    void parse_prolog()
    {
      parse_fixed_size_prolog();
      parse_variable_size_prolog();
    }

    void parse_prolog_strings()
    {
      prolog.command = compressed_header_stream.parse_string();
      prolog.datetime = compressed_header_stream.parse_string();
      prolog.comment = compressed_header_stream.parse_string();
      prolog.target_name = compressed_header_stream.parse_string();

      fmt::print(stderr, "Command = {}\nDatetime = {}\nComment = {}\nTarget Name = {}\n", prolog.command, prolog.datetime, prolog.comment, prolog.target_name);
    }

    void parse_encoding_maps()
    {
      const uint64_t encoding_size = compressed_header_stream.parse_uleb();
      const uint64_t initial_bytes_read = compressed_header_stream.total_bytes_read;

      const uint64_t n_maps = compressed_header_stream.parse_uleb();
      fmt::print(stderr, "Reading {} maps of size {} bytes\n", n_maps, encoding_size);
      for (uint64_t i = 0; i < n_maps; i++) {
        const std::string map_name = compressed_header_stream.parse_string();
        const uint64_t n_entries = compressed_header_stream.parse_uleb();

        fmt::print(stderr, "Reading {} values from {}\n", n_entries, map_name);
        for (uint64_t j = 0; j < n_entries; j++) {
          const uint64_t value = compressed_header_stream.parse_uleb();
          const std::string name = compressed_header_stream.parse_string();
          ids[std::move(map_name)].insert(std::move(ids_value_type(std::move(name), value)));

          fmt::print(stderr, "[{}] {}->{}\n", map_name, value, name);
        }
      }

      // Verify correctness
      using namespace wrong_path_trace_constants;
      for (const auto& [map_name, names] : required_encodings) {
        if (ids.find(map_name) == ids.end())
          throw std::runtime_error(fmt::format("[ERROR] {} encoding not found in header. Exiting...", map_name));

        for (const auto& name : names) {
          if (ids[map_name].left.find(name) == ids[map_name].left.end())
            throw std::runtime_error(fmt::format("[ERROR] {} mapping in {} encoding not found in header. Exiting...", name, map_name));
        }
      }

      if (compressed_header_stream.total_bytes_read - initial_bytes_read != encoding_size) {
        throw std::runtime_error("[ERROR] Encoding section overflowed its size");
      }
    }

    [[nodiscard]] Dependency parse_dependency(const uint8_t n_dst, const uint8_t max_dep_stores, const uint8_t max_dep_loads)
    {
      // Ensure that the relevant encodings are available
      using namespace wrong_path_trace_constants;
      if (ids.find("dep_block_flag") == ids.end())
        throw std::runtime_error("[ERROR] dep_block_flag encodings are missing. Exiting...");

      for (const auto& name : optional_encodings.at("dep_block_flag")) {
        if (ids["dep_block_flag"].left.find(name) == ids["dep_block_flag"].left.end())
          throw std::runtime_error(fmt::format("[ERROR] dep_block_flag encodings don't map {}. Exiting...", name));
      }

      Dependency dep;
      dep.dep_block_flags = static_cast<uint8_t>(compressed_header_stream.read());
      if (dep.dep_block_flags & ids["dep_block_flag"].left.at("CST_DEP_BLOCK_HAS_REG")) {
        for (uint8_t i = 0; i < n_dst; i++)
          dep.dst_dep.emplace_back(compressed_header_stream.parse_uleb());
        for (uint8_t i = 0; i < max_dep_stores; i++)
          dep.store_data_dep.emplace_back(compressed_header_stream.parse_uleb());
      }
      if (dep.dep_block_flags & ids["dep_block_flag"].left.at("CST_DEP_BLOCK_HAS_ADDR")) {
        for (uint8_t i = 0; i < max_dep_loads; i++)
          dep.load_addr_dep.emplace_back(compressed_header_stream.parse_uleb());
        for (uint8_t i = 0; i < max_dep_stores; i++)
          dep.store_addr_dep.emplace_back(compressed_header_stream.parse_uleb());
      }

      fmt::print(stderr,
                 "New Dependency Block\n"
                 "\tFlags = {:#010b}\n"
                 "\tDestination Dependencies = {::#x}\n"
                 "\tStore Data Dependencies = {::#x}\n"
                 "\tLoad Address Dependencies = {::#x}\n"
                 "\tStore Address Dependencies = {::#x}\n",
                 dep.dep_block_flags, dep.dst_dep, dep.store_data_dep, dep.load_addr_dep, dep.store_addr_dep);

      return dep;
    }

    [[nodiscard]] Instruction parse_instruction()
    {
      Instruction instr;
      instr.pc_delta = compressed_header_stream.parse_uleb();
      instr.opcode = static_cast<uint8_t>(compressed_header_stream.read());
      instr.branch_type = static_cast<uint8_t>(compressed_header_stream.read());
      instr.flags = static_cast<uint8_t>(compressed_header_stream.read());
      instr.n_src = static_cast<uint8_t>(compressed_header_stream.read());
      instr.n_dst = static_cast<uint8_t>(compressed_header_stream.read());
      for (uint8_t i = 0; i < instr.n_src; i++)
        instr.src_regs.emplace_back(static_cast<uint8_t>(compressed_header_stream.read()));
      for (uint8_t i = 0; i < instr.n_dst; i++)
        instr.dst_regs.emplace_back(static_cast<uint8_t>(compressed_header_stream.read()));
      instr.max_dep_loads = static_cast<uint8_t>(compressed_header_stream.read());
      instr.max_dep_stores = static_cast<uint8_t>(compressed_header_stream.read());
      if (instr.flags & ids["insn_flag"].left.at("CST_INSN_FLAG_HAS_IMM"))
        instr.immidiate = compressed_header_stream.parse_sleb();
      instr.instr_size = static_cast<uint8_t>(compressed_header_stream.read());
      instr.instr_bytes = compressed_header_stream.read_bytes(static_cast<std::size_t>(instr.instr_size));

      fmt::print(stderr,
                 "New Instruction:\n"
                 "\tPC Delta = {}\n"
                 "\tOpcode = {}\n"
                 "\tBranch Type = {}\n"
                 "\tFlags = {:#010b}\n"
                 "\tn_src = {}\n"
                 "\tn_dst = {}\n"
                 "\tsrcs = {}\n"
                 "\tdsts = {}\n"
                 "\tMax dependent loads = {}\n"
                 "\tMax dependent stores = {}\n"
                 "\tInstruction Size = {}\n"
                 "\tInstruction Bytes = {::#x}\n",
                 instr.pc_delta, instr.opcode, instr.branch_type, instr.flags, instr.n_src, instr.n_dst, instr.src_regs, instr.dst_regs, instr.max_dep_loads,
                 instr.max_dep_stores, instr.instr_size, instr.instr_bytes);
      if (instr.immidiate)
        fmt::print(stderr, "\tImmediate = {:#x}\n", instr.immidiate.value());

      if (instr.flags & ids["insn_flag"].left.at("CST_INSN_FLAG_HAS_DEP_BLOCK"))
        instr.dependency = parse_dependency(instr.n_dst, instr.max_dep_stores, instr.max_dep_loads);

      return instr;
    }

    [[nodiscard]] Profile parse_profile(const uint64_t n_targets, const uint64_t num_instr)
    {
      Profile pf;
      pf.exec_cp = compressed_header_stream.parse_uleb();
      pf.exec_wp = compressed_header_stream.parse_uleb();

      for (uint64_t i = 0; i < n_targets; i++) {
        pf.taken_cp.emplace_back(compressed_header_stream.parse_uleb());
        pf.nottaken_cp.emplace_back(compressed_header_stream.parse_uleb());
        pf.taken_wp.emplace_back(compressed_header_stream.parse_uleb());
        pf.nottaken_wp.emplace_back(compressed_header_stream.parse_uleb());
      }

      for (uint64_t i = 0; i < num_instr; i++) {
        pf.memops_cp.emplace_back(compressed_header_stream.parse_uleb());
        pf.memops_wp.emplace_back(compressed_header_stream.parse_uleb());
        pf.pat_flags.emplace_back(static_cast<uint8_t>(compressed_header_stream.read()));
        if (pf.memops_cp.back() > 0) {
          pf.lo_addr_cp.emplace_back(compressed_header_stream.parse_uleb());
          pf.hi_addr_cp.emplace_back(compressed_header_stream.parse_uleb());
        } else {
          pf.lo_addr_cp.emplace_back();
          pf.hi_addr_cp.emplace_back();
        }
        if (pf.memops_wp.back() > 0) {
          pf.lo_addr_wp.emplace_back(compressed_header_stream.parse_uleb());
          pf.hi_addr_wp.emplace_back(compressed_header_stream.parse_uleb());
        } else {
          pf.lo_addr_wp.emplace_back();
          pf.hi_addr_wp.emplace_back();
        }
      }

      fmt::print(stderr,
                 "Profile Block detected\n"
                 "\tExec CP {}\n"
                 "\tExec WP {}\n"
                 "\tTaken CP {}\n"
                 "\tNot Taken CP {}\n"
                 "\tTaken WP {}\n"
                 "\tNot Taken WP {}\n"
                 "\tMemops CP {}\n"
                 "\tMemops WP {}\n"
                 "\tPattern Flags {::#010b}\n"
                 "\tLo Addr CP: {::#x}\n"
                 "\tHi Addr CP: {::#x}\n"
                 "\tLo Addr WP: {::#x}\n"
                 "\tHi Addr WP: {::#x}\n",
                 pf.exec_cp, pf.exec_wp, pf.taken_cp, pf.nottaken_cp, pf.taken_wp, pf.nottaken_wp, pf.memops_cp, pf.memops_wp, pf.pat_flags, pf.lo_addr_cp,
                 pf.hi_addr_cp, pf.lo_addr_wp, pf.hi_addr_wp);

      return pf;
    }

    void parse_one_template()
    {
      const uint64_t template_size = compressed_header_stream.parse_uleb();
      const uint64_t bytes_read = compressed_header_stream.total_bytes_read;

      const uint32_t template_id = static_cast<uint32_t>(compressed_header_stream.parse_uleb());

      templates[template_id].start_pc = compressed_header_stream.parse_uleb();
      templates[template_id].num_instr = compressed_header_stream.parse_uleb();
      templates[template_id].fall_through_pc = compressed_header_stream.parse_uleb();
      templates[template_id].num_targets = compressed_header_stream.parse_uleb();
      for (uint64_t i = 0; i < templates[template_id].num_targets; i++)
        templates[template_id].targets.emplace_back(compressed_header_stream.parse_uleb());
      templates[template_id].symbol_name = compressed_header_stream.parse_string();

      fmt::print(stderr,
                 "Template {}:\n\tStart PC = {:#x}\n\tNumber of Instructions = {}\n\t"
                 "Fall Through PC = {:#x}\n\tSymbol Name = {}\n\tNumber of Targets = {}\n\t"
                 "Targets = {::#x}\n",
                 template_id, templates[template_id].start_pc, templates[template_id].num_instr, templates[template_id].fall_through_pc,
                 templates[template_id].symbol_name, templates[template_id].num_targets, templates[template_id].targets);

      for (uint64_t i = 0; i < templates[template_id].num_instr; i++)
        templates[template_id].instructions.emplace_back(parse_instruction());
      if (prolog.fixed_size.flags & ids["header_flag"].left.at("CST_FLAG_PROFILE"))
        templates[template_id].profile = parse_profile(templates[template_id].num_targets, templates[template_id].num_instr);

      const uint64_t detected_template_size = compressed_header_stream.total_bytes_read - bytes_read;
      if (detected_template_size != template_size)
        throw std::runtime_error(
            fmt::format("[ERROR] Malformed template detected. Expected Size = {}, Detected Size = {}. Exiting...", template_size, detected_template_size));
    }

    void parse_templates()
    {
      const uint64_t num_templates = compressed_header_stream.parse_uleb();
      fmt::print(stderr, "Reading {} templates\n", num_templates);

      for (uint64_t i = 0; i < num_templates; i++)
        parse_one_template();
    }

  public:
    header_parser(const std::string& header_file) : compressed_header_stream(header_file) {}
    void parse() override
    {
      parse_prolog();
      parse_prolog_strings();
      parse_encoding_maps();
      parse_templates();

      if (!compressed_header_stream.eof)
        throw std::runtime_error("[ERROR] Unexpected bytes found at the end of the header");
    }

    ~header_parser() override = default;
  };

  // Type erased wrapper for parsing the body. Type erasure is necessary since the concrete type depends on the compression format, which is only known at
  // runtime
  class body_wrapper
  {
  public:
    // If correct_path is true, fetch from the next correct path BB, else fetch from the wrong path chain
    virtual ooo_model_instr read(const bool correct_path = true) = 0;
    virtual bool eof() const = 0;
    virtual ~body_wrapper() = default;
  };

  template <typename BodyCompressedType>
  class body_parser : public body_wrapper
  {
  private:
    stream_reader<BodyCompressedType> compressed_body_stream;
    const header_wrapper& header;
    bool eof_ = false; // Set to true when the compressed_body_stream runs out
    const uint8_t cpu;
    uint32_t last_template_id; // Used for IFRAME validate. Set by handle_entry consumed by handle_iframe

    // Note: The trace contains register file values but ChampSim doesn't care about them. We maintain this regfile for posterity's sake only
    std::map<uint64_t, std::map<uint64_t, std::bitset<512>>> regfile;

    // Persistent overlay
    struct overlay_key {
      uint32_t template_id;
      uint64_t ipos;

      overlay_key(const uint32_t tid, const uint64_t ip) : template_id(tid), ipos(ip) {}
      [[nodiscard]] bool operator<(const overlay_key& other) const noexcept { return std::tie(template_id, ipos) < std::tie(other.template_id, other.ipos); }
      std::string format() const noexcept { return fmt::format("(Template ID = {}, Instruction Position = {})", template_id, ipos); }
    };
    std::map<overlay_key, std::map<uint64_t, std::bitset<512>>> overlay; // Each overlay value is a fid -> delta map

    // Members needed to read the trace in bulk
    std::deque<ooo_model_instr> instr_buffer; // Holds the decoded instructions without branch instruction fixes
    // TODO: Remove this
    std::deque<ooo_model_instr> instr_buffer_fixed; // Holds the decoded instructions with branch instruction fixes

    // Members needed to walk the trace
    uint32_t previous_template_id = 0;
    uint32_t previous_thread_id = 0;
    uint32_t seq_num = 0;
    uint64_t cp_instruction_num = 0; // Counts the number of parsed correct path instructions
    std::set<uint64_t> valid_body_tags;

    struct field_delta_section {
      uint64_t ipos;
      uint64_t fid;
      std::bitset<512> delta;
      std::optional<uint64_t> ext_payload;
    };

    struct delta_section {
      uint64_t n_records;
      std::vector<field_delta_section> records;
    };

    struct wp_chain_section {
      uint64_t num_wp;
      std::vector<int64_t> template_id_deltas;
      std::vector<delta_section> wp_deltas;
    };

    struct wp_events_section {
      uint64_t num_events;
      std::vector<uint64_t> wp_index;
      std::vector<std::optional<uint64_t>> fault_instr_index;
    };

    struct body_entry {
      uint32_t template_id;
      delta_section cp_delta;
      wp_chain_section wp_chain;
      wp_events_section wp_events;
    };

    template <std::size_t width>
    [[nodiscard]] std::bitset<width> add_bitset(std::bitset<width> b1, std::bitset<width> b2) const noexcept
    {
      std::bitset<width> retval(0);
      constexpr uint64_t bitmask = 0x00000000'ffffffff;
      int64_t carry = 0;
      int64_t bits_left = static_cast<int64_t>(width);
      while (bits_left > 0) {
        const uint64_t temp_b1 = (b1 & std::bitset<width>(bitmask)).to_ullong();
        int64_t b1_chunk = 0;
        std::memcpy(&b1_chunk, &temp_b1, 4);

        const uint64_t temp_b2 = (b2 & std::bitset<width>(bitmask)).to_ullong();
        int64_t b2_chunk = 0;
        std::memcpy(&b2_chunk, &temp_b2, 4);

        const int64_t partial_sum = b1_chunk + b2_chunk + carry;
        const uint32_t retval_chunk = partial_sum & bitmask;
        carry = (partial_sum & 0x1'00000000) >> 32;

        std::bitset<width> new_bits(retval_chunk);
        new_bits <<= (width - bits_left);
        retval |= new_bits;

        b1 >>= 32;
        b2 >>= 32;
        bits_left -= (4 * 8);
      }
      return retval;
    }

    void verify_integrity()
    {
      uint32_t trace_magic_bytes;
      const auto buffer = compressed_body_stream.read_bytes(sizeof(trace_magic_bytes));
      std::memcpy(&trace_magic_bytes, std::data(buffer), sizeof(trace_magic_bytes));

      fmt::print(stderr, "Body Magic Bytes = {:X}\n", trace_magic_bytes);

      // Verify integrity
      using namespace wrong_path_trace_constants;
      if (trace_magic_bytes != magic_bytes)
        throw std::runtime_error(
            fmt::format("[ERROR] Can't verify integrity of trace body. Magic bytes don't match. Expected {:X}, got {:X}", magic_bytes, trace_magic_bytes));
    }

    [[nodiscard]] ooo_model_instr generate_instruction(uint64_t& instruction_pc, const header_wrapper::Instruction& instruction_template,
                                                       const std::map<uint64_t, std::bitset<512>>& deltas)
    {
      // Useful functions
      auto bitset_to_uint64_t = [] [[nodiscard]] (const std::bitset<512>& bits) -> uint64_t {
        try {
          return static_cast<uint64_t>(bits.to_ullong());
        } catch (const std::overflow_error& er) {
          throw std::runtime_error(fmt::format("[ERROR] Can't cast bitset with value {} to uint64_t", bits.to_string()));
        }
      };

      auto extract_suffix = [] [[nodiscard]] (const std::string& str, const char* prefix) constexpr noexcept -> uint64_t {
        uint64_t value;
        const std::string suffix_string = str.substr(std::string_view(prefix).size());
        std::istringstream iss(std::move(suffix_string));
        iss >> value;
        return value;
      };

      // Compute the PC
      instruction_pc += instruction_template.pc_delta;

      // Apply the overlays delta. The deltas are relative to the templates
      uint8_t opcode_id = instruction_template.opcode;
      uint8_t branch_type_id = instruction_template.branch_type;
      std::vector<uint64_t> src_mem, dst_mem;
      uint64_t n_loads = 0;
      uint64_t n_stores = 0;
      uint64_t max_observed_load_count = 0;
      uint64_t max_observed_store_count = 0;
      for (const auto& [fid, delta] : deltas) {
        const std::string& fid_name = header.ids.at("field_id").right.at(fid);
        if (fid_name == "CST_FID_N_LOADS") {
          n_loads = bitset_to_uint64_t(delta);
        } else if (fid_name == "CST_FID_N_STORES") {
          n_stores = bitset_to_uint64_t(delta);
        } else if (fid_name == "CST_FID_METAFLAGS") {
          /* Not implemented */
        } else if (fid_name.rfind("CST_FID_LOAD_ADDR", 0) == 0) {
          src_mem.emplace_back(bitset_to_uint64_t(delta));
          max_observed_load_count = std::max(max_observed_load_count, extract_suffix(fid_name, "CST_FID_LOAD_ADDR"));
        } else if (fid_name.rfind("CST_FID_STORE_ADDR", 0) == 0) {
          dst_mem.emplace_back(bitset_to_uint64_t(delta));
          max_observed_store_count = std::max(max_observed_store_count, extract_suffix(fid_name, "CST_FID_STORE_ADDR"));
        } else if (fid_name.rfind("CST_FID_LOAD_DATA", 0) == 0) {
          /* Not implemented */
          max_observed_load_count = std::max(max_observed_load_count, extract_suffix(fid_name, "CST_FID_LOAD_DATA"));
        } else if (fid_name.rfind("CST_FID_STORE_DATA", 0) == 0) {
          /* Not implemented */
          max_observed_store_count = std::max(max_observed_store_count, extract_suffix(fid_name, "CST_FID_STORE_DATA"));
        } else if (fid_name.rfind("CST_FID_DST_REG", 0) == 0) {
          const uint64_t reg_index = extract_suffix(fid_name, "CST_FID_DST_REG");
          if (reg_index >= instruction_template.dst_regs.size())
            throw std::runtime_error(
                fmt::format("[ERROR] Observed data for register #{} for an instruction with {} registers", reg_index, instruction_template.dst_regs.size()));
          const uint64_t reg_id = instruction_template.dst_regs.at(reg_index);
          regfile[previous_thread_id][reg_id] = delta;
        } else if (fid_name.rfind("CST_FID_LOAD_SIZE", 0) == 0) {
          /* Not implemented */
          max_observed_load_count = std::max(max_observed_load_count, extract_suffix(fid_name, "CST_FID_LOAD_SIZE"));
        } else if (fid_name.rfind("CST_FID_STORE_SIZE", 0) == 0) {
          /* Not implemented */
          max_observed_store_count = std::max(max_observed_store_count, extract_suffix(fid_name, "CST_FID_STORE_SIZE"));
        } else if (fid_name.rfind("CST_FID_DST_REG_WIDTH", 0) == 0) {
          /* Not implemented */
        } else if (fid_name.rfind("CST_FID_SRC_LANE_MASK", 0) == 0) {
          /* Not implemented */
        } else if (fid_name.rfind("CST_FID_DST_LANE_MASK", 0) == 0) {
          /* Not implemented */
        } else if (fid_name.rfind("CST_FID_LOAD_DATA_LANE_MASK", 0) == 0) {
          /* Not implemented */
        } else if (fid_name.rfind("CST_FID_STORE_DATA_LANE_MASK", 0) == 0) {
          /* Not implemented */
        } else if (fid_name == "CST_FID_INSN_BYTES_LO") {
          /* Not implemented */
        } else if (fid_name == "CST_FID_INSN_BYTES_HI") {
          /* Not implemented */
        } else if (fid_name == "CST_FID_INSN_OPCODE") {
          opcode_id = static_cast<uint8_t>(bitset_to_uint64_t(delta));
        } else if (fid_name == "CST_FID_INSN_BRANCH_TYPE") {
          branch_type_id = static_cast<uint8_t>(bitset_to_uint64_t(delta));
        } else if (fid_name == "CST_FID_INSN_FLAGS") {
          /* Not implemented */
        } else if (fid_name == "CST_FID_INSN_IMMEDIATE") {
          /* Not implemented */
        } else if (fid_name == "CST_FID_INSN_SIZE") {
          /* Not implemented */
        } else if (fid_name == "CST_FID_EXTENDED") {
          /* Not implemented */
        } else {
          throw std::runtime_error(fmt::format("[ERROR] Unknown FID: {} detected", fid_name));
        }
      }
      if (max_observed_load_count > n_loads) {
        throw std::runtime_error(fmt::format("[ERROR] Observed data for load #{} for an instruction with {} loads", max_observed_load_count, n_loads));
      }
      if (max_observed_store_count > n_stores) {
        throw std::runtime_error(fmt::format("[ERROR] Observed data for store #{} for an instruction with {} stores", max_observed_store_count, n_stores));
      }

      // Identify if this instruction is a branch or not
      if (header.ids.find("opcode") == header.ids.end())
        throw std::runtime_error("[ERROR] Opcode encoding map is missing");
      if (header.ids.at("opcode").right.find(opcode_id) == header.ids.at("opcode").right.end())
        throw std::runtime_error(fmt::format("[ERROR] Found an instruction with unknown opcode ID {}", opcode_id));
      const std::string& opcode_name = header.ids.at("opcode").right.at(opcode_id);

      const std::set<std::string> branch_opcodes{"GEN_OP_BRANCH", "GEN_OP_RET", "GEN_OP_SYSCALL"};
      const bool is_branch = (branch_opcodes.find(opcode_name) != branch_opcodes.end());

      // TODO: Identify branch direction via FIDs
      const bool branch_taken = false; // Branch direction is fixed later

      branch_type br_type{NOT_BRANCH};
      if (is_branch) {
        if (header.ids.find("branch_type") == header.ids.end())
          throw std::runtime_error("[ERROR] Branch Type encoding map is missing");
        if (header.ids.at("branch_type").right.find(branch_type_id) == header.ids.at("branch_type").right.end())
          throw std::runtime_error(fmt::format("[ERROR] Found an branch instruction with unknown type ID {}", branch_type_id));
        const std::string& branch_type_name = header.ids.at("branch_type").right.at(branch_type_id);

        if (branch_type_name == "BRANCH_NONE")
          throw std::runtime_error("[ERROR] Can't infer whether instruction is a branch instruction or not");
        else if (branch_type_name == "BRANCH_DIRECT_JUMP")
          br_type = branch_type::BRANCH_DIRECT_JUMP;
        else if (branch_type_name == "BRANCH_INDIRECT_JUMP")
          br_type = branch_type::BRANCH_INDIRECT;
        else if (branch_type_name == "BRANCH_DIRECT_CALL")
          br_type = branch_type::BRANCH_DIRECT_CALL;
        else if (branch_type_name == "BRANCH_INDIRECT_CALL")
          br_type = branch_type::BRANCH_INDIRECT_CALL;
        else if (branch_type_name == "BRANCH_RETURN")
          br_type = branch_type::BRANCH_RETURN;
        else if (branch_type_name == "BRANCH_SYSCALL_TYPE")
          br_type = branch_type::BRANCH_DIRECT_CALL;
        else if (branch_type_name == "BRANCH_COND_DIRECT")
          br_type = branch_type::BRANCH_DIRECT_JUMP;
        else if (branch_type_name == "BRANCH_REP")
          br_type = branch_type::BRANCH_OTHER;
      }

      std::vector<std::string> dst_reg_names;
      for (const auto& reg : instruction_template.dst_regs)
        dst_reg_names.emplace_back(header.ids.at("reg").right.at(reg));
      std::vector<std::string> src_reg_names;
      for (const auto& reg : instruction_template.src_regs)
        src_reg_names.emplace_back(header.ids.at("reg").right.at(reg));
      std::string branch_type_name = "NOT_BRANCH";
      if (br_type == branch_type::BRANCH_DIRECT_JUMP) {
        branch_type_name = "BRANCH_DIRECT_JUMP";
      } else if (br_type == branch_type::BRANCH_INDIRECT) {
        branch_type_name = "BRANCH_INDIRECT";
      } else if (br_type == branch_type::BRANCH_DIRECT_CALL) {
        branch_type_name = "BRANCH_DIRECT_CALL";
      } else if (br_type == branch_type::BRANCH_INDIRECT_CALL) {
        branch_type_name = "BRANCH_INDIRECT_CALL";
      } else if (br_type == branch_type::BRANCH_RETURN) {
        branch_type_name = "BRANCH_RETURN";
      } else if (br_type == branch_type::BRANCH_OTHER) {
        branch_type_name = "BRANCH_OTHER";
      }

      // Construct an ooo_model_instr and return
      return ooo_model_instr(instruction_pc, is_branch, branch_taken, cpu, br_type, instruction_template.dst_regs, instruction_template.src_regs, dst_mem,
                             src_mem, instruction_template.instr_size);
    }

    // Applies the deltas to the overlays and constructs a vector of ooo_model_instr from a single body entry
    [[nodiscard]] std::vector<ooo_model_instr> construct_instructions(const body_entry& entry)
    {
#warning "WP is not supported yet"
      // TODO: Add support for WP

      fmt::print(stderr, "Generating instructions from template {}\n", entry.template_id);

      if (header.templates.find(entry.template_id) == header.templates.end())
        throw std::runtime_error(fmt::format("[ERROR] Unknown template ID {} found", entry.template_id));
      const auto& instructions = header.templates.at(entry.template_id).instructions;

      fmt::print(stderr, "Generating {} instructions. instr_buffer has {} instructions\n", instructions.size(), instr_buffer.size());

      // Apply the overlay delta changes
      const auto& deltas = entry.cp_delta.records;
      for (const auto& delta : deltas) {
        if (delta.ipos >= instructions.size())
          throw std::runtime_error(
              fmt::format("[ERROR] Illegal instruction position {} found for template with {} instructions", delta.ipos, instructions.size()));

        const overlay_key key(entry.template_id, delta.ipos);
        if (overlay.find(key) == overlay.end())
          overlay[key][delta.fid] = delta.delta;
        else
          overlay[key][delta.fid] = add_bitset(overlay[key][delta.fid], delta.delta);
      }

      const std::size_t num_instr = instructions.size();
      std::vector<ooo_model_instr> retvec;
      retvec.reserve(num_instr);
      auto pc = header.templates.at(entry.template_id).start_pc;
      for (uint64_t ipos = 0; ipos < num_instr; ipos++) {
        const overlay_key key(entry.template_id, ipos);
        retvec.emplace_back(generate_instruction(pc, instructions[ipos], overlay[key]));
      }

      return retvec;
    }

    void handle_thread_switch()
    {
      const int64_t thread_id_delta = compressed_body_stream.parse_sleb();
      previous_thread_id += static_cast<uint32_t>(static_cast<int64_t>(previous_thread_id) + thread_id_delta);
    }

    [[nodiscard]] std::bitset<512> cast_to_bitset(const std::vector<char>& bytes) const noexcept
    {
      std::bitset<512> retval;
      for (const auto& b : bytes) {
        retval <<= 8;
        retval |= std::bitset<512>(b);
      }
      return retval;
    }

    void handle_regfile()
    {
      const uint64_t thread_id = compressed_body_stream.parse_uleb();
      const uint64_t n_present = compressed_body_stream.parse_uleb();
      for (uint64_t i = 0; i < n_present; i++) {
        const uint8_t gen_id = static_cast<uint8_t>(compressed_body_stream.read());
        const uint8_t width = static_cast<uint8_t>(compressed_body_stream.read());
        const std::vector<char> bytes = compressed_body_stream.read_bytes(width);
        regfile[thread_id][gen_id] = cast_to_bitset(bytes);
      }
    }

    [[nodiscard]] field_delta_section read_field_delta_section(uint32_t& ipos)
    {
      field_delta_section field_delta;
      const uint64_t ipos_delta = compressed_body_stream.parse_uleb();
      ipos = static_cast<uint32_t>(static_cast<uint64_t>(ipos) + ipos_delta);
      field_delta.ipos = ipos;
      field_delta.fid = compressed_body_stream.parse_uleb();
      field_delta.delta = compressed_body_stream.parse_sleb_wide();

      if (field_delta.fid == header.ids.at("field_id").left.at("CST_FID_EXTENDED"))
        field_delta.ext_payload = compressed_body_stream.parse_uleb();

      return field_delta;
    }

    [[nodiscard]] delta_section read_cp_delta_section()
    {
      const uint64_t section_size = compressed_body_stream.parse_uleb();
      const uint64_t initial_bytes_read = compressed_body_stream.total_bytes_read;

      delta_section cp_delta;
      const uint64_t n_records = compressed_body_stream.parse_uleb();
      uint32_t ipos = 0;
      for (uint64_t i = 0; i < n_records; i++)
        cp_delta.records.emplace_back(read_field_delta_section(ipos));

      if (compressed_body_stream.total_bytes_read - initial_bytes_read != section_size) {
        throw std::runtime_error("[ERROR] Correct Path Delta section overflowed its size");
      }

      return cp_delta;
    }

    [[nodiscard]] wp_chain_section read_wp_chain_section()
    {
      const uint64_t section_size = compressed_body_stream.parse_uleb();
      const uint64_t initial_bytes_read = compressed_body_stream.total_bytes_read;

      wp_chain_section wp_chain;
      wp_chain.num_wp = compressed_body_stream.parse_uleb();
      for (uint64_t i = 0; i < wp_chain.num_wp; i++) {
        wp_chain.template_id_deltas.emplace_back(compressed_body_stream.parse_sleb());
        wp_chain.wp_deltas.emplace_back(read_cp_delta_section());
      }

      if (compressed_body_stream.total_bytes_read - initial_bytes_read != section_size) {
        throw std::runtime_error("[ERROR] Wrong Path Chain section overflowed its size");
      }

      return wp_chain;
    }

    [[nodiscard]] wp_events_section read_wp_events_section()
    {
      const uint64_t section_size = compressed_body_stream.parse_uleb();
      const uint64_t initial_bytes_read = compressed_body_stream.total_bytes_read;

      wp_events_section wp_events;
      wp_events.num_events = compressed_body_stream.parse_uleb();
      int32_t wp_index = -1;
      for (uint64_t i = 0; i < wp_events.num_events; i++) {
        const uint64_t pos_gap = compressed_body_stream.parse_uleb();
        wp_index = wp_index + 1 + static_cast<int32_t>(pos_gap);
        wp_events.wp_index.emplace_back(static_cast<uint64_t>(wp_index));

        const uint8_t ev_flags = static_cast<uint8_t>(compressed_body_stream.read());
        if (ev_flags & header.ids.at("wp_event_flag").left.at("CST_WP_EVENT_FAULT"))
          wp_events.fault_instr_index.emplace_back(compressed_body_stream.parse_uleb());
        else
          wp_events.fault_instr_index.emplace_back();
      }

#warning "incomplete implementation"
      // TODO: Do something here

      if (compressed_body_stream.total_bytes_read - initial_bytes_read != section_size) {
        throw std::runtime_error("[ERROR] Wrong Path Events section overflowed its size");
      }

      return wp_events;
    }

    [[nodiscard]] body_entry handle_entry()
    {
      body_entry retval;

      const int64_t template_id_delta = compressed_body_stream.parse_sleb();
      const uint32_t current_template_id = static_cast<uint32_t>(static_cast<int64_t>(previous_template_id) + template_id_delta);
      if (header.templates.find(current_template_id) == header.templates.end())
        throw std::runtime_error(fmt::format("[ERROR] Unknown template ID {} found in trace body. Exiting...\n", current_template_id));
      previous_template_id = current_template_id;
      seq_num += 1;

      retval.template_id = current_template_id;
      retval.cp_delta = read_cp_delta_section();
      if (header.prolog.fixed_size.flags & header.ids.at("header_flag").left.at("CST_FLAG_WP")) {
        retval.wp_chain = read_wp_chain_section();
        retval.wp_events = read_wp_events_section();
      }

      last_template_id = retval.template_id; // Record the most recent template ID. Used for IFRAME validation
      return retval;
    }

    void handle_iframe()
    {
#warning "Wrong Path validation not implemented"
      // TODO: Implement wrong path validation

      delta_section cp_delta = read_cp_delta_section();
      if (header.prolog.fixed_size.flags & header.ids.at("header_flag").left.at("CST_FLAG_WP")) {
        wp_chain_section wp_chain = read_wp_chain_section();
        wp_events_section wp_events = read_wp_events_section();
      }

      // Validate the overlay state
      const auto& deltas = cp_delta.records;
      for (const auto& delta : deltas) {
        const overlay_key key(last_template_id, delta.ipos);
        if (overlay.find(key) == overlay.end())
          throw std::runtime_error(fmt::format("[ERROR] IFRAME validation failed! Overlay key {} not found in the overlay map", key.format()));
        if (overlay[key][delta.fid] != delta.delta)
          throw std::runtime_error(
              fmt::format("[ERROR] IFRAME validation failed! Delta value mismatch for key {} and FID {}\n\tExpected Value = {}\n\tStored Value = {}",
                          key.format(), delta.fid, delta.delta.to_string(), overlay[key][delta.fid].to_string()));
      }
    }

    void handle_end()
    {
      const uint64_t num_entries = compressed_body_stream.parse_uleb();
      if (num_entries != seq_num)
        throw std::runtime_error(
            fmt::format("[ERROR] Number of entries ({}) doesn't match the expected number of entries ({}). Exiting...", seq_num, num_entries));

      verify_integrity();

      if (!compressed_body_stream.eof)
        throw std::runtime_error("[ERROR] Unexpected bytes found at the end of body section");

      eof_ = true; // Mark the trace source as finished
    }

    // Keeps reading the trace until we reach the next BODY_TAG_ENTRY section. Stops right before the BODY_TAG_ENTRY section, i.e. *only* reads the sections
    // preceding the next BODY_TAG_ENTRY section
    void read_till_next_entry()
    {
      while (true) {
        const uint8_t tag = compressed_body_stream.read();
        if (valid_body_tags.find(tag) == valid_body_tags.end())
          throw std::runtime_error(fmt::format("[ERROR] Found unexpected tag value of {}. Expected values: {}. Exiting...", tag, valid_body_tags));

        if (header.ids.at("body_tag").right.at(tag) == "BODY_TAG_THREAD_SWITCH") {
          handle_thread_switch();
        } else if (header.ids.at("body_tag").right.at(tag) == "BODY_TAG_REGFILE") {
          handle_regfile();
        } else if (header.ids.at("body_tag").right.at(tag) == "BODY_TAG_ENTRY") {
          return; // Don't parse the BODY_TAG_ENTRY section yet
        } else if (header.ids.at("body_tag").right.at(tag) == "BODY_TAG_IFRAME") {
          handle_iframe();
        } else if (header.ids.at("body_tag").right.at(tag) == "BODY_TAG_END") {
          return handle_end();
        }
      }
    }

    // Reads the next instruction from the trace (from the next BODY_TAG_ENTRY), constructs an ooo_model_instr from it, and return it
    // Expects that the stream pointer points to the next BODY_TAG_ENTRY
    [[nodiscard]] std::vector<ooo_model_instr> get_next_instrs(const bool correct_path = true)
    {
      fmt::print(stderr, "Fetching from {} path\n", correct_path ? "correct" : "wrong");

      const body_entry entry = handle_entry();
      std::vector<ooo_model_instr> retvec = construct_instructions(entry);
      read_till_next_entry(); // Keep reading until we reach the next section (but don't read the section yet) to prepare for the next call
      return retvec;
    }

    // Returns the next instruction from the instruction buffer
    [[nodiscard]] ooo_model_instr pop_from_instr_buffer()
    {
      // TODO: Revert back to instr_buffer
      if (instr_buffer_fixed.size() == 0)
        throw std::runtime_error(fmt::format("[ERROR] Attempting to pop from empty instruction buffer"));

      const auto retval = instr_buffer_fixed.front();
      instr_buffer_fixed.pop_front();
      cp_instruction_num++;
      return retval;
    }

  public:
    body_parser(const uint8_t cpu_idx, const std::string& body_file, const header_wrapper& header_)
        : compressed_body_stream(body_file), header(header_), cpu(cpu_idx)
    {
      verify_integrity();

      using namespace wrong_path_trace_constants;
      for (const auto& [_, value] : header.ids.at("body_tag"))
        valid_body_tags.emplace(std::move(value));

      read_till_next_entry(); // Read from the stream until we reach the first BODY_TAG_ENTRY section (but don't read this section yet)
    }

    [[nodiscard]] ooo_model_instr read(const bool correct_path = true) override
    {
      // No more instruction left in the stream
      if (eof_) {
        // Drain the buffer
        // TODO: Revert back to instr_buffer
        if (instr_buffer_fixed.size() > 0)
          return pop_from_instr_buffer();

        // We should never reach this point
        throw std::runtime_error("[ERROR] No more instructions left to read. Exiting...");
      }

      // Time to re-fill the buffer
      // TODO: Revert back to instr_buffer
      if ((instr_buffer_fixed.size() == 0)) {
        if (instr_buffer.size() == 0) { // Populate the instr_buffer if its empty
          const auto instrs = get_next_instrs(correct_path);
          instr_buffer.insert(std::end(instr_buffer), std::make_move_iterator(std::begin(instrs)), std::make_move_iterator(std::end(instrs)));
          fmt::print(stderr, "instr_buffer has {} instructions\n", instr_buffer.size());
          if (instrs.size() == 0) {
            // TODO: Add trace inferred wrong path implementation here
          }
        }

        // Populate the instr_buffer_fixed using instr_buffer
        // TODO: Revert back to instr_buffer
        using iter_type = typename decltype(instr_buffer)::iterator;
        iter_type i = instr_buffer.begin();
        while (i != instr_buffer.end()) {
          if (!(*i).is_branch) { // All non-branch instruction can be moved directly
            instr_buffer_fixed.emplace_back(std::move(*i));
            instr_buffer.erase(i);
            i = instr_buffer.begin();
          } else {
            // The instruction after the branch is available
            if (std::distance(i, instr_buffer.end()) > 0) {
              iter_type target = i + 1; // j points to the instruction executed after the branch
              (*i).branch_taken = ((*i).raw_ip + (*i).instr_size) != (*target).raw_ip;
              instr_buffer_fixed.emplace_back(std::move(*i));
              instr_buffer.erase(i);
              i = instr_buffer.begin();
            } else
              i++;
          }
        }
      }

      // Return the next instruction
      return pop_from_instr_buffer();
    }

    [[nodiscard]] bool eof() const override
    {
      // TODO: Revert to instr_buffer
      const bool retval = (instr_buffer_fixed.size() == 0 && eof_);

      // Each instruction in the source application can potentially result in multiple trace instructions. For example, one `rep` in X86 can result in tens of
      // trace instructions. Moreover, the writer collects the trace till cp_instruction_num instructions are reached *and* the last executing basic block is
      // finished, resulting in potentially 5-10 extra trace instructions
      const bool comsumed_expected_num_cp_instructions = header.prolog.total_target_instructions <= cp_instruction_num;
      const bool unbounded_trace = header.prolog.total_target_instructions == 0; // The trace was collected until the application exited

      if (retval && !unbounded_trace && !comsumed_expected_num_cp_instructions)
        throw std::runtime_error(fmt::format(
            "[ERROR] Trace has {} correct path instructions, but only {} were parsed",
            header.prolog.total_target_instructions != 0 ? fmt::format("{}", header.prolog.total_target_instructions) : "unbounded", cp_instruction_num));

      return retval;
    }

    ~body_parser() override = default;
  };

  std::unique_ptr<header_wrapper> header_stream = nullptr;
  std::unique_ptr<body_wrapper> body_stream = nullptr;

public:
  [[nodiscard]] ooo_model_instr operator()(const bool correct_path = true)
  {
    const ooo_model_instr& instr = body_stream->read(correct_path);

    fmt::print(stderr, "{:x}: branch = {}, taken = {}, dst regs = {}, src regs = {}\n", instr.raw_ip, instr.is_branch, instr.branch_taken,
               instr.destination_registers, instr.source_registers);

    return instr;
  }

  wrong_path_tracereader(const std::string& tf, const uint8_t cpu_idx) : cpu(cpu_idx), trace_file(tf)
  {
    try {
      parse_trace();
    } catch (const std::exception& e) {
      fmt::print(stderr, fmt::format("[ERROR] Failed to initialize trace reader\n\n {}", e.what()));
      cleanup();
      std::exit(1);
    }
  }
  wrong_path_tracereader(champsim::wrong_path_tracereader&& other)
      : cpu(other.cpu), trace_file(std::move(other.trace_file)), header_stream(std::move(other.header_stream)), body_stream(std::move(other.body_stream))
  {
    other.moved = true;
  }

  wrong_path_tracereader& operator=(wrong_path_tracereader& other) = default;
  wrong_path_tracereader& operator=(wrong_path_tracereader&& other)
  {
    this->cleanup(); // Reset the state of this object
    this->cpu = std::move(other.cpu);
    this->trace_file = std::move(other.trace_file);
    this->trace_extract_dir = std::move(other.trace_extract_dir);
    this->header_stream = std::move(other.header_stream);
    this->construct_body_stream(); // Create a fresh copy of the body stream
    other.moved = true;            // Prevent deleting the extracted trace directory
    return *this;
  }

  [[nodiscard]] bool eof() const
  {
    if (!body_stream)
      throw std::runtime_error("[ERROR] Body stream is not initialized");

    return body_stream->eof();
  };

  ~wrong_path_tracereader()
  {
    if (!moved)
      cleanup();
  }
};

void wrong_path_tracereader::parse_trace()
{
  verify_trace_file_type();
  trace_extract_dir = create_extract_dir();
  extract_trace();
  construct_header_stream();
  header_stream->parse();
  construct_body_stream();
}

// Verifies the magic bytes of file_name. Return true if magic bytes match
template <const char* magic, const std::size_t magic_length, const std::size_t position>
[[nodiscard]] bool wrong_path_tracereader::check_file_type(const std::string& file_name) const
{
  std::ifstream input(file_name, std::ios::binary);
  if (!input.is_open())
    throw std::runtime_error(fmt::format("[ERROR] Can't open {}\n", file_name));

  // Read the magic bytes
  input.seekg(position);
  char magic_bytes[magic_length] = "";
  input.read((char*)magic_bytes, magic_length);

  return (std::memcmp(magic_bytes, magic, magic_length) == 0);
}

void wrong_path_tracereader::verify_trace_file_type() const
{
  // Verify that the file type is correct
  if (!is_tar(trace_file))
    throw std::runtime_error("[ERROR] Unknown trace file format. Was expecting a TAR file\n");
}

[[nodiscard]] std::filesystem::path wrong_path_tracereader::create_extract_dir() const
{
  std::filesystem::path base_extract_dir(".temp_extracted_trace");
  std::filesystem::path extract_dir = base_extract_dir;
  uint64_t suffix = 0;
  while (std::filesystem::is_directory(extract_dir)) {
    suffix += 1;
    extract_dir = base_extract_dir;
    extract_dir += std::to_string(suffix);
  }
  fmt::print(stderr, "New dir: {}\n", extract_dir.string());
  const bool success = std::filesystem::create_directory(extract_dir);
  if (!success)
    throw std::runtime_error("[ERROR] Could not create the directory to extract the wrong path trace. Exiting...");

  return extract_dir;
}

void wrong_path_tracereader::extract_trace() const
{
  const auto open = [] [[nodiscard]] (const char* file_name) -> TAR* {
    TAR* handle = nullptr;
    const int retval = tar_open(&handle, file_name, nullptr, O_RDONLY, 0, TAR_GNU);
    if (retval != 0)
      throw std::runtime_error(fmt::format("[ERROR] Can't open trace: {}. Exiting...", file_name));
    return handle;
  };

  const auto close = [](TAR* handle) -> void {
    const auto retval = tar_close(handle);
    if (retval != 0)
      throw std::runtime_error("[ERROR] Can't close trace. Exiting...");
  };

  const std::unique_ptr<TAR, decltype(close)> trace{open(trace_file.c_str()), close};

  // Extract the trace files to the extract dir
  const int retval = tar_extract_all(trace.get(), const_cast<char*>(trace_extract_dir.c_str()));
  if (retval != 0)
    throw std::runtime_error(fmt::format("[ERROR] Can't extract trace: {}. Exiting...", trace_file));
}

[[nodiscard]] std::filesystem::path wrong_path_tracereader::get_header_path() const
{
  for (const auto& entry : std::filesystem::directory_iterator(trace_extract_dir)) {
    if (entry.path().filename().string().find("header.cst") == 0)
      return entry.path();
  }
  throw std::runtime_error("[ERROR] The trace has no header. Exiting...");
}

[[nodiscard]] std::filesystem::path wrong_path_tracereader::get_body_path() const
{
  for (const auto& entry : std::filesystem::directory_iterator(trace_extract_dir)) {
    if (entry.path().filename().string().find("body.cst") == 0)
      return entry.path();
  }
  throw std::runtime_error("[ERROR] The trace has no body. Exiting...");
}

void wrong_path_tracereader::construct_header_stream()
{
  const std::string& header_file_name = get_header_path().string();
  if (is_gzip(header_file_name)) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::gzip_tag_t<>>>(header_file_name));
  } else if (is_lzma(header_file_name)) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::lzma_tag_t<>>>(header_file_name));
  } else if (is_bzip2(header_file_name)) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::bzip2_tag_t>>(header_file_name));
  } else if (is_zstd(header_file_name)) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::zst_tag_t>>(header_file_name));
  } else if (is_lz4(header_file_name)) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::lz4_tag_t>>(header_file_name));
  } else { // If all else fails, parse the input as a raw text file
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::raw_data_tag_t>>(header_file_name));
  }
}

void wrong_path_tracereader::construct_body_stream()
{
  const std::string& body_file_name = get_body_path().string();
  if (is_gzip(body_file_name)) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::gzip_tag_t<>>>(cpu, body_file_name, *header_stream));
  } else if (is_lzma(body_file_name)) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::lzma_tag_t<>>>(cpu, body_file_name, *header_stream));
  } else if (is_bzip2(body_file_name)) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::bzip2_tag_t>>(cpu, body_file_name, *header_stream));
  } else if (is_zstd(body_file_name)) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::zst_tag_t>>(cpu, body_file_name, *header_stream));
  } else if (is_lz4(body_file_name)) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::lz4_tag_t>>(cpu, body_file_name, *header_stream));
  } else { // If all else fails, parse the input as a raw text file
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::raw_data_tag_t>>(cpu, body_file_name, *header_stream));
  }
}

} // namespace champsim

#endif
