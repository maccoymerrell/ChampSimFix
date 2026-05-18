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

#include <fmt/core.h>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <type_traits>

#include "inf_stream.h"
#include "instruction.h"
#include "util/detect.h"

namespace champsim
{

int64_t sleb_decoder(const std::array<char, 10>& chunks, const std::size_t num_chunks)
{
  int64_t retval = 0;
  for(std::size_t chunk_id = 0; chunk_id < num_chunks; chunk_id++)
  {
    uint8_t byte = chunks[chunk_id] & 0x7f;       // The MSB is used for encoding and is not part of the payload
    int64_t chunk = 0;
    std::memcpy(&chunk, &byte, sizeof(uint8_t));  // Safely type cast. Copy over the bits without sign extension
    chunk <<= chunk_id * 7;                       // Shift the chunk bits into their right place
    retval |= chunk;                              // Add the bits to the decoded value
  }
  return retval;
}

uint64_t uleb_decoder(const std::array<char, 10>& chunks, const std::size_t num_chunks)
{
  uint64_t retval = 0;
  for(std::size_t chunk_id = 0; chunk_id < num_chunks; chunk_id++)
  {
    uint8_t byte = chunks[chunk_id] & 0x7f;       // The MSB is used for encoding and is not part of the payload
    uint64_t chunk = static_cast<uint64_t>(byte);
    chunk <<= chunk_id * 7;                       // Shift the chunk bits into their right place
    retval |= chunk;                              // Add the bits to the decoded value
  }
  return retval;
}

class wrong_path_tracereader
{
  uint8_t cpu;
  std::string trace_file;
  std::filesystem::path trace_extract_dir;

  constexpr static std::size_t buffer_size = 128;
  constexpr static std::size_t refresh_thresh = 1;
  std::deque<ooo_model_instr> instr_buffer;

  void parse_trace();
  std::filesystem::path create_extract_dir();
  void extract_trace(const std::string& trace_file_name) const;
  std::filesystem::path get_header_path() const;
  std::filesystem::path get_body_path() const;
  void construct_header_stream();
  void construct_body_stream();

  // Type erased wrapper for parsing the header. Type erasure is necessary since the concrete type depends on the compression
  // format which is only known at runtime
  class header_wrapper
  {
    friend class body_wrapper;

    // TODO: Add header template structure

    public:
      virtual void parse() = 0;
      virtual ~header_wrapper() = default;
  };

  template<typename HeaderCompressedType>
  class header_parser: public header_wrapper
  {
    private:
      HeaderCompressedType header_file;

    public:
      header_parser(const std::string& header_file_): header_file(header_file_) {}
      void parse() override
      {
        // TODO: Parse the header and populate the header structure
        fmt::print("Parsing header\n");
        std::array<char, 64> raw_buf;
        std::size_t bytes_read = 0;
        bool eof_ = false;

        while(!eof_ && bytes_read <= 512)
        {
          // Read from trace file
          header_file.read(std::data(raw_buf), std::size(raw_buf));
          bytes_read += static_cast<std::size_t>(header_file.gcount());
          eof_ = header_file.eof();

          for(const auto& byte: raw_buf)
            fmt::print("{:02x}", std::byte(byte));
          fmt::print("\n");
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

  template<typename BodyCompressedType>
  class body_parser: public body_wrapper
  {
    private:
      std::array<char, 64> raw_buf;
      uint8_t cpu;
      bool eof_ = false;
      BodyCompressedType body_file;

      // TODO: Declare the body structure here

    public:
      body_parser(uint8_t cpu_idx, const std::string& body_file_): cpu(cpu_idx), body_file(body_file_) {}

      ooo_model_instr read() override
      {
        // TODO: Read the trace instruction by instruction and emit ooo_model_instr
        std::size_t bytes_read = 0;

        while(!eof_ && bytes_read <= 512)
        {
          // Read from trace file
          body_file.read(std::data(raw_buf), std::size(raw_buf));
          bytes_read += static_cast<std::size_t>(body_file.gcount());
          eof_ = body_file.eof();

          for(const auto& byte: raw_buf)
            fmt::print("{:02x}", std::byte(byte));
          fmt::print("\n");
        }
        eof_ = true;

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

      bool eof() const override
      {
        return eof_;
      }

      ~body_parser() override = default;
  };

  std::unique_ptr<header_wrapper> header_stream = nullptr;
  std::unique_ptr<body_wrapper> body_stream = nullptr;

public:
  ooo_model_instr operator()() { return body_stream->read(); }

  wrong_path_tracereader(const std::string& tf, uint8_t cpu_idx) : cpu(cpu_idx), trace_file(tf) { parse_trace(); }
  wrong_path_tracereader(champsim::wrong_path_tracereader&& other):
    cpu(other.cpu), trace_file(std::move(other.trace_file)),
    header_stream(std::move(other.header_stream)), body_stream(std::move(other.body_stream)) {
      std::filesystem::remove_all(other.trace_extract_dir);
      parse_trace();
    }

  [[nodiscard]] bool eof() const {
    if(!body_stream)
    {
      fmt::print(stderr, "[ERROR] Body stream is not initialized\n");
      std::exit(-1);
    }
    return body_stream->eof();
  };

  ~wrong_path_tracereader()
  {
    std::filesystem::remove_all(trace_extract_dir);
  }
};

void wrong_path_tracereader::parse_trace()
{
  trace_extract_dir = create_extract_dir();
  extract_trace(trace_file);
  construct_header_stream();
  construct_body_stream();
  header_stream->parse();
}

std::filesystem::path wrong_path_tracereader::create_extract_dir()
{
  std::filesystem::path base_extract_dir("temp_extracted_trace");
  std::filesystem::path extract_dir = base_extract_dir;
  uint64_t suffix = 0;
  while(std::filesystem::is_directory(extract_dir))
  {
    suffix += 1;
    extract_dir = base_extract_dir;
    extract_dir += std::to_string(suffix);
  }
  fmt::print("New dir: {}\n", extract_dir.string());
  bool success = std::filesystem::create_directory(extract_dir);
  if(!success)
  {
    fmt::print(stderr, "[ERROR] Could not create the directory to extract the wrong path trace. Exiting...\n");
    std::exit(-1);
  }
  return extract_dir;
}

void wrong_path_tracereader::extract_trace(const std::string& trace_file_name) const
{
  std::string command = "tar -xf " + trace_file_name + " -C " + trace_extract_dir.string();
  if(std::system(command.c_str()) != 0)
  {
    fmt::print(stderr, "[ERROR] Could not extract the wrong path trace. Exiting...\n");
    std::exit(-1);
  }
}

std::filesystem::path wrong_path_tracereader::get_header_path() const
{
  for(const auto& entry: std::filesystem::directory_iterator(trace_extract_dir))
  {
    if(entry.path().string().find("header") != std::string::npos)
      return entry.path();
  }
  fmt::print(stderr, "[ERROR] The trace has no header. Exiting...\n");
  std::exit(-1);
}

std::filesystem::path wrong_path_tracereader::get_body_path() const
{
  for(const auto& entry: std::filesystem::directory_iterator(trace_extract_dir))
  {
    if(entry.path().string().find("body") != std::string::npos)
      return entry.path();
  }
  fmt::print(stderr, "[ERROR] The trace has no body. Exiting...\n");
  std::exit(-1);
}

void wrong_path_tracereader::construct_header_stream()
{
  std::string header_file_name = get_header_path().string();
  if (bool is_gzip_compressed = (header_file_name.substr(std::size(header_file_name) - 2) == "gz"); is_gzip_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::gzip_tag_t<>>>(header_file_name));
  }
  else if (bool is_lzma_compressed = (header_file_name.substr(std::size(header_file_name) - 2) == "xz"); is_lzma_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::lzma_tag_t<>>>(header_file_name));
  }
  else if (bool is_bzip2_compressed = (header_file_name.substr(std::size(header_file_name) - 3) == "bz2"); is_bzip2_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::bzip2_tag_t>>(header_file_name));
  }
  else {
    fmt::print(stderr, "[ERROR] Unknown compression format for trace header. Exiting...\n");
    std::exit(-1);
  }
}

void wrong_path_tracereader::construct_body_stream()
{
  std::string body_file_name = get_body_path().string();
  if (bool is_gzip_compressed = (body_file_name.substr(std::size(body_file_name) - 2) == "gz"); is_gzip_compressed) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::gzip_tag_t<>>>(cpu, body_file_name));
    return;
  }
  else if (bool is_lzma_compressed = (body_file_name.substr(std::size(body_file_name) - 2) == "xz"); is_lzma_compressed) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::lzma_tag_t<>>>(cpu, body_file_name));
    return;
  }
  else if (bool is_bzip2_compressed = (body_file_name.substr(std::size(body_file_name) - 3) == "bz2"); is_bzip2_compressed) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::bzip2_tag_t>>(cpu, body_file_name));
    return;
  }
  else {
    fmt::print(stderr, "[ERROR] Unknown compression format for trace body. Exiting...\n");
    std::exit(-1);
  }
}

} // namespace champsim

#endif
