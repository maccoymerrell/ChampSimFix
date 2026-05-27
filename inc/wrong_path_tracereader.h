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

// TODO: Remove the debug prints

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include "inf_stream.h"
#include "instruction.h"
#include "util/detect.h"
#include "wrong_path_tracereader_verification_constants.h"

namespace champsim
{
class wrong_path_tracereader
{
  const uint8_t cpu;
  const std::string trace_file;
  std::filesystem::path trace_extract_dir;

  void parse_trace();
  std::filesystem::path create_extract_dir();
  void extract_trace(const std::string& trace_file_name) const;
  std::filesystem::path get_header_path() const;
  std::filesystem::path get_body_path() const;
  void construct_header_stream();
  void construct_body_stream();

  // This class is responsible for decompressing the compressed streams and returning
  // decompressed data byte-by-byte
  // This class als:wo provides functions for decoding ULEBs and strings
  template <typename CompressedStreamType, std::size_t buffer_size = 4096>
  class stream_reader
  {
    CompressedStreamType stream;
    std::array<char, buffer_size> decompressed_buffer; // This buffer temporarily the decompressed bytes
    decltype(decompressed_buffer.end()) buffer_iter = decompressed_buffer.begin();
    decltype(decompressed_buffer.end()) max_buffer_iter = decompressed_buffer.begin();

    // Returns the next byte from the input stream
    std::optional<char> decompress()
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

    uint64_t uleb_decoder(std::vector<char>& chunks) const
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

    // Parses a ULEB_WIDE into std::bitset
    // TODO: Verify correctness
    std::bitset<512> uleb_wide_decoder(std::vector<char>& chunks) const
    {
      fmt::print(stderr, "[WARNING] ULEB_WIDE decoder is unverified. Use at your own risk.\n");
      std::bitset<512> parsed_bits;
      parsed_bits.reset();
      for (auto i = chunks.rbegin(); i != chunks.rend(); i++) {
        uint8_t chunk = static_cast<uint8_t>(*i & 0x7f);
        parsed_bits |= chunk;
        parsed_bits <<= 7;
      }

      return parsed_bits;
    }

    int64_t sleb_decoder(std::vector<char>& chunks) const
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
      return retval;
    }

    // Parses a SLEB_WIDE into std::bitset
    // TODO: Verify correctness
    std::bitset<512> sleb_wide_decoder(std::vector<char>& chunks) const
    {
      fmt::print(stderr, "[WARNING] SLEB_WIDE decoder is unverified. Use at your own risk.\n");
      std::size_t bit_idx = 0;
      bool msb = static_cast<bool>(chunks.back() & 0x40);

      std::bitset<512> parsed_bits;
      parsed_bits.reset();
      for (auto i = chunks.rbegin(); i != chunks.rend(); i++) {
        uint8_t chunk = static_cast<uint8_t>(*i & 0x7f);
        parsed_bits |= chunk;
        parsed_bits <<= 7;

        bit_idx += 7;
      }
      while (bit_idx < 512)
        parsed_bits[bit_idx++] = msb; // Sign extension

      return parsed_bits;
    }

  public:
    uint64_t total_bytes_read = 0; // Number of bytes read from the stream so far
    bool eof = false;              // Set to true if no more bytes can be returned
    stream_reader(const std::string& stream_name) : stream(stream_name) {}

    char read()
    {
      auto byte = decompress();
      if (!byte) {
        fmt::print(stderr, "[ERROR] Ran out of compressed stream. Exiting...\n");
        std::exit(-1);
      }
      return byte.value();
    }

    // Reads a ULEB from the stream
    uint64_t parse_uleb()
    {
      std::vector<char> chunks;
      while (auto byte = decompress()) {
        chunks.emplace_back(byte.value());
        if (!(byte.value() & 0x80))
          break;
      }

      if (chunks.size() > 10) {
        fmt::print(stderr, "[ERROR] ULEB of more than 10 bytes detected\n");
        std::exit(-1);
      }

      return uleb_decoder(chunks);
    }

    // Reads a SLEB from the stream
    uint64_t parse_sleb()
    {
      std::vector<char> chunks;
      while (auto byte = decompress()) {
        chunks.emplace_back(byte.value());
        if (!(byte.value() & 0x80))
          break;
      }

      if (chunks.size() > 10) {
        fmt::print(stderr, "[ERROR] SLEB of more than 10 bytes detected\n");
        std::exit(-1);
      }

      return sleb_decoder(chunks);
    }

    // Reads a string from the stream
    std::string parse_string()
    {
      const std::size_t string_size = static_cast<std::size_t>(parse_uleb());
      std::string retval = "";
      for (std::size_t i = 0; i < string_size; i++)
        retval += read();
      return retval;
    }

    // Reads a chunk of bytes from the stream
    std::vector<char> read_bytes(const std::size_t num_bytes)
    {
      std::vector<char> retvec;
      for (std::size_t bytes_read = 0; bytes_read != num_bytes; bytes_read++)
        retvec.emplace_back(read());
      return retvec;
    }

    // Reads a f64 from the stream
    double parse_f64()
    {
      double retval = 0.0f;
      auto bytes = read_bytes(8);
      std::memcpy(&retval, bytes.data(), 8);
      return retval;
    }
  };

  // Type erased wrapper for parsing the header. Type erasure is necessary since the concrete type depends on the compression
  // format which is only known at runtime
  class header_wrapper
  {
    friend class body_wrapper;

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

    std::map<std::string, std::map<std::string, uint64_t>> ids;
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

      auto buffer = compressed_header_stream.read_bytes(bytes_to_read);
      std::memcpy(&(prolog.fixed_size), std::data(buffer), bytes_to_read);

      fmt::print("Magic = {:X}\nISA = {:X}\nFlags = {:#010b}\n", prolog.fixed_size.magic_bytes, prolog.fixed_size.isa, prolog.fixed_size.flags);

      using namespace wrong_path_trace_constants;
      if (prolog.fixed_size.magic_bytes != magic_bytes) {
        fmt::print(stderr, "[ERROR] Can't verify header integrity. Magic bytes don't match\n");
        std::exit(-1);
      }
    }

    void parse_variable_size_prolog()
    {
      prolog.start_instructions = compressed_header_stream.parse_uleb();
      prolog.warmup_instructions = compressed_header_stream.parse_uleb();
      prolog.total_target_instructions = compressed_header_stream.parse_uleb();
      prolog.simpoint_weight = compressed_header_stream.parse_f64();

      fmt::print("Start Instructions = {}\nWarmup Instructions = {}\n"
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

      fmt::print("Command = {}\nDatetime = {}\nComment = {}\nTarget Name = {}\n", prolog.command, prolog.datetime, prolog.comment, prolog.target_name);
    }

    void parse_encoding_maps()
    {
      const uint64_t encoding_size = compressed_header_stream.parse_uleb();
      const auto initial_bytes_read = compressed_header_stream.total_bytes_read;

      const uint64_t n_maps = compressed_header_stream.parse_uleb();
      fmt::print("Reading {} maps of size {} bytes\n", n_maps, encoding_size);
      for (uint64_t i = 0; i < n_maps; i++) {
        const std::string map_name = compressed_header_stream.parse_string();
        const uint64_t n_entries = compressed_header_stream.parse_uleb();

        fmt::print("Reading {} values from {}\n", n_entries, map_name);
        for (uint64_t j = 0; j < n_entries; j++) {
          const uint64_t value = compressed_header_stream.parse_uleb();
          const std::string name = compressed_header_stream.parse_string();
          ids[map_name][name] = value;

          fmt::print("[{}] {}->{}\n", map_name, value, name);
        }
      }

      // Verify correctness
      using namespace wrong_path_trace_constants;
      for (const auto& [map_name, names] : required_encodings) {
        if (ids.find(map_name) == ids.end()) {
          fmt::print(stderr, "[ERROR] {} encoding not found in header. Exiting...\n", map_name);
          std::exit(-1);
        }
        for (const auto& name : names) {
          if (ids[map_name].find(name) == ids[map_name].end()) {
            fmt::print(stderr, "[ERROR] {} mapping in {} encoding not found in header. Exiting...\n", name, map_name);
            std::exit(-1);
          }
        }
      }

      if (compressed_header_stream.total_bytes_read - initial_bytes_read != encoding_size) {
        fmt::print(stderr, "[ERROR] Encoding section overflowed its size\n");
        std::exit(-1);
      }
    }

    Dependency parse_dependency(const uint8_t n_dst, const uint8_t max_dep_stores, const uint8_t max_dep_loads)
    {
      // Ensure that the relevant encodings are available
      using namespace wrong_path_trace_constants;
      if (ids.find("dep_block_flag") == ids.end()) {
        fmt::print(stderr, "[ERROR] dep_block_flag encodings are missing. Exiting...\n");
        std::exit(-1);
      }
      for (const auto& name : optional_encodings.at("dep_block_flag")) {
        if (ids["dep_block_flag"].find(name) == ids["dep_block_flag"].end()) {
          fmt::print(stderr, "[ERROR] dep_block_flag encodings don't map {}. Exiting...\n", name);
          std::exit(-1);
        }
      }

      Dependency dep;
      dep.dep_block_flags = static_cast<uint8_t>(compressed_header_stream.read());
      if (dep.dep_block_flags & ids["dep_block_flag"]["CST_DEP_BLOCK_HAS_REG"]) {
        for (uint8_t i = 0; i < n_dst; i++)
          dep.dst_dep.emplace_back(compressed_header_stream.parse_uleb());
        for (uint8_t i = 0; i < max_dep_stores; i++)
          dep.store_data_dep.emplace_back(compressed_header_stream.parse_uleb());
      }
      if (dep.dep_block_flags & ids["dep_block_flag"]["CST_DEP_BLOCK_HAS_ADDR"]) {
        for (uint8_t i = 0; i < max_dep_loads; i++)
          dep.load_addr_dep.emplace_back(compressed_header_stream.parse_uleb());
        for (uint8_t i = 0; i < max_dep_stores; i++)
          dep.store_addr_dep.emplace_back(compressed_header_stream.parse_uleb());
      }

      fmt::print("New Dependency Block\n"
                 "\tFlags = {:#010b}\n"
                 "\tDestination Dependencies = {::#x}\n"
                 "\tStore Data Dependencies = {::#x}\n"
                 "\tLoad Address Dependencies = {::#x}\n"
                 "\tStore Address Dependencies = {::#x}\n",
                 dep.dep_block_flags, dep.dst_dep, dep.store_data_dep, dep.load_addr_dep, dep.store_addr_dep);

      return dep;
    }

    Instruction parse_instruction()
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
      if (instr.flags & ids["insn_flag"]["CST_INSN_FLAG_HAS_IMM"])
        instr.immidiate = compressed_header_stream.parse_sleb();
      instr.instr_size = static_cast<uint8_t>(compressed_header_stream.read());
      instr.instr_bytes = compressed_header_stream.read_bytes(static_cast<std::size_t>(instr.instr_size));

      fmt::print("New Instruction:\n"
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
        fmt::print("\tImmediate = {:#x}\n", instr.immidiate.value());

      if (instr.flags & ids["insn_flag"]["CST_INSN_FLAG_HAS_DEP_BLOCK"])
        instr.dependency = parse_dependency(instr.n_dst, instr.max_dep_stores, instr.max_dep_loads);

      return instr;
    }

    Profile parse_profile(const uint64_t n_targets, const uint64_t num_instr)
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

      fmt::print("Profile Block detected\n"
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

      fmt::print("Template {}:\n\tStart PC = {:#x}\n\tNumber of Instructions = {}\n\t"
                 "Fall Through PC = {:#x}\n\tSymbol Name = {}\n\tNumber of Targets = {}\n\t"
                 "Targets = {::#x}\n",
                 template_id, templates[template_id].start_pc, templates[template_id].num_instr, templates[template_id].fall_through_pc,
                 templates[template_id].symbol_name, templates[template_id].num_targets, templates[template_id].targets);

      for (uint64_t i = 0; i < templates[template_id].num_instr; i++)
        templates[template_id].instructions.emplace_back(parse_instruction());
      if (prolog.fixed_size.flags & ids["header_flag"]["CST_FLAG_PROFILE"])
        templates[template_id].profile = parse_profile(templates[template_id].num_targets, templates[template_id].num_instr);

      const uint64_t detected_template_size = compressed_header_stream.total_bytes_read - bytes_read;
      if (detected_template_size != template_size) {
        fmt::print(stderr,
                   "[ERROR] Malformed template detected. Expected Size = {}, Detected"
                   " Size = {}. Exiting...\n",
                   template_size, detected_template_size);
        std::exit(-1);
      }
    }

    void parse_templates()
    {
      const uint64_t num_templates = compressed_header_stream.parse_uleb();
      fmt::print("Reading {} templates\n", num_templates);

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

      if (!compressed_header_stream.eof) {
        fmt::print(stderr, "[ERROR] Unexpected bytes found at the end of the header\n");
        std::exit(-1);
      }
    }

    ~header_parser() override = default;
  };

  // Type erased wrapper for parsing the body. Type erasure is necessary since the concrete type
  // depends on the compression format which is only known at runtime
  class body_wrapper
  {
  public:
    virtual ooo_model_instr read() = 0;
    virtual bool eof() const = 0;
    virtual ~body_wrapper() = default;
  };

  template <typename BodyCompressedType>
  class body_parser : public body_wrapper
  {
  private:
    const uint8_t cpu;
    stream_reader<BodyCompressedType> compressed_body_stream;

    // TODO: Declare the body structure here

  public:
    body_parser(uint8_t cpu_idx, const std::string& body_file) : cpu(cpu_idx), compressed_body_stream(body_file) {}

    ooo_model_instr read() override
    {
      // TODO: Read the trace instruction by instruction and emit ooo_model_instr
      // `encodings` are globally accessible
      // The template maps in header_stream should be accessible since body_wrapper is friend of header_wrapper
      std::size_t bytes_read = 0;

      fmt::print("Reading from the body file\n");
      while (bytes_read <= 64) {
        char byte = compressed_body_stream.read();

        fmt::print("{:02x}", byte);
        bytes_read++;
      }
      fmt::print("\n");

      // if (std::size(instr_buffer) <= refresh_thresh) {
      //   std::array<T, buffer_size - refresh_thresh> trace_read_buf;
      //   std::array<char, std::size(trace_read_buf) * sizeof(T)> raw_buf;
      //   std::size_t bytes_read;
      //
      //   // Read from trace file
      //   trace_file.read(std::data(raw_buf), std::size(raw_buf));
      //   bytes_read = static_cast<std::size_t>(trace_file.gcount());
      //   eof_ = trace_file.eof();
      //
      //   // Transform bytes into trace format instructions
      //   std::memcpy(std::data(trace_read_buf), std::data(raw_buf), bytes_read);
      //
      //   // Inflate trace format into core model instructions
      //   auto begin = std::begin(trace_read_buf);
      //   auto end = std::next(begin, bytes_read / sizeof(T));
      //   std::transform(begin, end, std::back_inserter(instr_buffer), [cpu = this->cpu](T t) { return ooo_model_instr{cpu, t}; });
      //
      //   // Set branch targets
      //   set_branch_targets(std::begin(instr_buffer), std::end(instr_buffer));
      // }
      //
      //   auto retval = instr_buffer.front();
      //   instr_buffer.pop_front();
      //
      //   return retval;
    }

    bool eof() const override { return compressed_body_stream.eof; }

    ~body_parser() override = default;
  };

  std::unique_ptr<header_wrapper> header_stream = nullptr;
  std::unique_ptr<body_wrapper> body_stream = nullptr;

public:
  ooo_model_instr operator()() { return body_stream->read(); }

  wrong_path_tracereader(const std::string& tf, const uint8_t cpu_idx) : cpu(cpu_idx), trace_file(tf) { parse_trace(); }
  wrong_path_tracereader(champsim::wrong_path_tracereader&& other)
      : cpu(other.cpu), trace_file(std::move(other.trace_file)), header_stream(std::move(other.header_stream)), body_stream(std::move(other.body_stream))
  {
  }

  [[nodiscard]] bool eof() const
  {
    if (!body_stream) {
      fmt::print(stderr, "[ERROR] Body stream is not initialized\n");
      std::exit(-1);
    }
    return body_stream->eof();
  };

  ~wrong_path_tracereader() { std::filesystem::remove_all(trace_extract_dir); }
};

void wrong_path_tracereader::parse_trace()
{
  trace_extract_dir = create_extract_dir();
  extract_trace(trace_file);
  construct_header_stream();
  construct_body_stream();
  header_stream->parse();
  std::cout << std::flush;
}

std::filesystem::path wrong_path_tracereader::create_extract_dir()
{
  std::filesystem::path base_extract_dir(".temp_extracted_trace");
  std::filesystem::path extract_dir = base_extract_dir;
  uint64_t suffix = 0;
  while (std::filesystem::is_directory(extract_dir)) {
    suffix += 1;
    extract_dir = base_extract_dir;
    extract_dir += std::to_string(suffix);
  }
  fmt::print("New dir: {}\n", extract_dir.string());
  const bool success = std::filesystem::create_directory(extract_dir);
  if (!success) {
    fmt::print(stderr, "[ERROR] Could not create the directory to extract the wrong path trace. Exiting...\n");
    std::exit(-1);
  }
  return extract_dir;
}

void wrong_path_tracereader::extract_trace(const std::string& trace_file_name) const
{
  // TODO: Untar the trace using C++ instead of calling `tar`
  const std::string command = "tar -xf " + trace_file_name + " -C " + trace_extract_dir.string();
  if (std::system(command.c_str()) != 0) {
    fmt::print(stderr, "[ERROR] Could not extract the wrong path trace. Exiting...\n");
    std::exit(-1);
  }
}

std::filesystem::path wrong_path_tracereader::get_header_path() const
{
  for (const auto& entry : std::filesystem::directory_iterator(trace_extract_dir)) {
    if (entry.path().filename().string().find("header.cst") == 0)
      return entry.path();
  }
  fmt::print(stderr, "[ERROR] The trace has no header. Exiting...\n");
  std::exit(-1);
}

std::filesystem::path wrong_path_tracereader::get_body_path() const
{
  for (const auto& entry : std::filesystem::directory_iterator(trace_extract_dir)) {
    if (entry.path().filename().string().find("body.cst") != 0)
      return entry.path();
  }
  fmt::print(stderr, "[ERROR] The trace has no body. Exiting...\n");
  std::exit(-1);
}

void wrong_path_tracereader::construct_header_stream()
{
  // TODO: Detect file type using magic bytes instead of file name
  // TODO: Add support for zst and lz4 compression formats
  // TODO: Add support for uncompressed trace format

  const std::string header_file_name = get_header_path().string();
  if (const bool is_gzip_compressed = (header_file_name.substr(std::size(header_file_name) - 2) == "gz"); is_gzip_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::gzip_tag_t<>>>(header_file_name));
  } else if (const bool is_lzma_compressed = (header_file_name.substr(std::size(header_file_name) - 2) == "xz"); is_lzma_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::lzma_tag_t<>>>(header_file_name));
  } else if (const bool is_bzip2_compressed = (header_file_name.substr(std::size(header_file_name) - 3) == "bz2"); is_bzip2_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::bzip2_tag_t>>(header_file_name));
  } else {
    fmt::print(stderr, "[ERROR] Unknown compression format for trace header. Exiting...\n");
    std::exit(-1);
  }
}

void wrong_path_tracereader::construct_body_stream()
{
  // TODO: Detect file type using magic bytes instead of file name
  // TODO: Add support for zst and lz4 compression formats
  // TODO: Add support for uncompressed trace format

  const std::string body_file_name = get_body_path().string();
  if (const bool is_gzip_compressed = (body_file_name.substr(std::size(body_file_name) - 2) == "gz"); is_gzip_compressed) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::gzip_tag_t<>>>(cpu, body_file_name));
    return;
  } else if (const bool is_lzma_compressed = (body_file_name.substr(std::size(body_file_name) - 2) == "xz"); is_lzma_compressed) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::lzma_tag_t<>>>(cpu, body_file_name));
    return;
  } else if (const bool is_bzip2_compressed = (body_file_name.substr(std::size(body_file_name) - 3) == "bz2"); is_bzip2_compressed) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::bzip2_tag_t>>(cpu, body_file_name));
    return;
  } else {
    fmt::print(stderr, "[ERROR] Unknown compression format for trace body. Exiting...\n");
    std::exit(-1);
  }
}

} // namespace champsim

#endif
