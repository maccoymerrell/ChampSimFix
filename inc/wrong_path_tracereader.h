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
#include <filesystem>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>

#include "inf_stream.h"
#include "instruction.h"
#include "util/detect.h"
#include "wrong_path_tracereader_econdings.h"

namespace champsim
{

int64_t sleb_decoder(std::vector<char>& chunks)
{
  int64_t retval = 0;
  uint64_t chunk_id = 0;
  for(char& byte: chunks)
  {
    byte &= 0x7f;                                 // The MSB is used for encoding and is not part of the payload
    int64_t chunk = 0;
    std::memcpy(&chunk, &byte, sizeof(char));  // Safely type cast. Copy over the bits without sign extension
    chunk <<= chunk_id * 7;                       // Shift the chunk bits into their right place
    retval |= chunk;                              // Add the bits to the decoded value
    chunk_id++;
  }
  return retval;
}

uint64_t uleb_decoder(std::vector<char>& chunks)
{
  uint64_t retval = 0;
  uint64_t chunk_id = 0;
  for(char& byte: chunks)
  {
    byte &= 0x7f;                                 // The MSB is used for encoding and is not part of the payload
    uint64_t chunk = static_cast<uint64_t>(byte);
    chunk <<= chunk_id * 7;                       // Shift the chunk bits into their right place
    retval |= chunk;                              // Add the bits to the decoded value
    chunk_id++;
  }
  return retval;
}

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
  template<typename CompressedStreamType, std::size_t buffer_size = 4096>
  class stream_reader
  {
    CompressedStreamType stream;
    std::array<char, buffer_size> decompressed_buffer;  // This buffer temporarily the decompressed bytes
    decltype(decompressed_buffer.end()) buffer_iter = decompressed_buffer.end();
    decltype(decompressed_buffer.end()) max_buffer_iter = decompressed_buffer.end();

    public:
      uint64_t total_bytes_read = 0;  // Number of bytes read from the stream so far
      bool eof = false;               // Set to true if no more bytes can be returned
      stream_reader(const std::string& stream_name): stream(stream_name) {}

      // Returns the next byte from the input stream
      std::optional<char> decompress()
      {
        // No more bytes left
        if((buffer_iter >= decompressed_buffer.end()) && stream.eof())
        {
          eof = true;
          return {};
        }

        // No need to decompress more. We can return from the buffer
        if(buffer_iter != max_buffer_iter)
        {
          char front = *buffer_iter;
          buffer_iter++;
          total_bytes_read++;
          return {front};
        }

        // The buffer is empty. Re-fill the buffer
        stream.read(std::data(decompressed_buffer), decompressed_buffer.size());
        const auto bytes_read = static_cast<std::size_t>(stream.gcount());
        if(bytes_read == 0)
        {
          eof = true;
          return {};
        }
        buffer_iter = decompressed_buffer.begin();
        max_buffer_iter = buffer_iter + bytes_read;

        // Return the next byte
        char front = *buffer_iter;
        buffer_iter++;
        total_bytes_read++;
        return {front};
      }
  };

  // Type erased wrapper for parsing the header. Type erasure is necessary since the concrete type depends on the compression
  // format which is only known at runtime
  class header_wrapper
  {
    friend class body_wrapper;

    protected:
      struct Prolog
      {
        struct FixedSize
        {
          uint32_t magic_bytes;
          uint8_t isa;
          uint8_t flags;
        } fixed_size;

        uint64_t start_instructions;
        uint64_t warmup_instructions;
        uint64_t total_target_instructions;

        std::string command;
        std::string datetime;
        std::string comment;
        std::string target_name;
      } prolog;

      // TODO: Add header encodings structure

      // TODO: Add header template structure

    public:
      virtual void parse() = 0;
      virtual ~header_wrapper() = default;
  };

  template<typename HeaderCompressedType>
  class header_parser: public header_wrapper
  {
    private:
      stream_reader<HeaderCompressedType> compressed_header_stream;

      // Reads a chunk of bytes from the header stream
      std::vector<char> read_bytes(const std::size_t num_bytes)
      {
        std::vector<char> retvec;
        auto next_byte = compressed_header_stream.decompress();
        std::size_t bytes_read = 1;
        if(!next_byte)
        {
          fmt::print(stderr, "[ERROR] Incomplete header file detected\n");
          std::exit(-1);
        }

        retvec.push_back(next_byte.value());
        for(; bytes_read != num_bytes; bytes_read++)
        {
          next_byte = compressed_header_stream.decompress();
          if(!next_byte)
            break;
          retvec.push_back(next_byte.value());
        }
        if(bytes_read != num_bytes)
        {
          fmt::print(stderr, "[ERROR] Incomplete header file detected\n");
          std::exit(-1);
        }

        return retvec;
      }

      void parse_fixed_size_prolog()
      {
        const std::size_t bytes_to_read = sizeof(prolog.fixed_size.magic_bytes) +
          sizeof(prolog.fixed_size.isa) + sizeof(prolog.fixed_size.flags);

        auto buffer = read_bytes(bytes_to_read);
        std::memcpy(&(prolog.fixed_size), std::data(buffer), bytes_to_read);

        fmt::print("Magic = {:X}\nISA = {:X}\nFlags = {:#b}\n",
            prolog.fixed_size.magic_bytes, prolog.fixed_size.isa,
            prolog.fixed_size.flags);

        if(prolog.fixed_size.magic_bytes != 0x1C545343)
        {
          fmt::print(stderr, "[ERROR] Can't verify header integrity. Magic bytes don't match\n");
          std::exit(-1);
        }
      }

      uint64_t parse_uleb()
      {
        std::vector<char> chunks;
        while(auto byte = compressed_header_stream.decompress())
        {
          chunks.push_back(byte.value());
          if(!(byte.value() & 0x80))
            break;
        }

        if(chunks.size() > 9)
        {
          fmt::print(stderr, "[ERROR] ULEB of more than 8 bytes detected\n");
          std::exit(-1);
        }

        return uleb_decoder(chunks);
      }

      void parse_variable_size_prolog()
      {
        prolog.start_instructions = parse_uleb();
        prolog.warmup_instructions = parse_uleb();
        prolog.total_target_instructions = parse_uleb();

        fmt::print("Start Instructions = {}\nWarmup Instructions = {}\n"
            "Total Target Instructions = {}\n",
            prolog.start_instructions, prolog.warmup_instructions,
            prolog.total_target_instructions);
      }

      void parse_prolog()
      {
        parse_fixed_size_prolog();
        parse_variable_size_prolog();
      }

      std::string parse_string()
      {
        const std::size_t string_size = static_cast<std::size_t>(parse_uleb());
        std::string retval = "";
        for(std::size_t i = 0; i < string_size; i++)
          retval += compressed_header_stream.decompress().value();
        return retval;
      }

      void parse_prolog_strings()
      {
        prolog.command = parse_string();
        prolog.datetime = parse_string();
        prolog.comment = parse_string();
        prolog.target_name = parse_string();

        fmt::print("Command = {}\nDatetime = {}\nComment = {}\nTarget Name = {}\n",
            prolog.command, prolog.datetime, prolog.comment, prolog.target_name);
      }

      void parse_encoding_maps()
      {
        const uint64_t encoding_size = parse_uleb();
        const auto initial_bytes_read = compressed_header_stream.total_bytes_read;

        const uint64_t n_maps = parse_uleb();
        fmt::print("Reading {} maps\n", n_maps);
        if(expected_encodings.size() != n_maps)
        {
          fmt::print(stderr, "[ERROR] Header encodings are incorrect\n");
          std::exit(-1);
        }

        for(uint64_t i = 0; i < n_maps; i++)
        {
          const std::string map_name = parse_string();
          const uint64_t n_entries = parse_uleb();
          if(expected_encodings.find(map_name) == expected_encodings.end())
          {
            fmt::print(stderr, "[ERROR] Detected unexpected encoding map: {}\n", map_name);
            std::exit(-1);
          }
          if(expected_encodings.find(map_name)->second.size() != n_entries)
          {
            fmt::print(stderr, "[ERROR] Encoding map {} has incorrect number of entries\n", map_name);
            std::exit(-1);
          }

          fmt::print("Reading {} values from {}\n", n_entries, map_name);
          for(uint64_t j = 0; j < n_entries; j++)
          {
            const uint64_t value = parse_uleb();
            const std::string name = parse_string();
            if(expected_encodings.find(map_name)->second.find(value) == expected_encodings.find(map_name)->second.end())
            {
              fmt::print(stderr, "[ERROR] Encoding map {} doesn't not have a mapping for {}\n", map_name, value);
              std::exit(-1);
            }
            if(expected_encodings.find(map_name)->second.find(value)->second != name)
            {
              fmt::print(stderr, "[ERROR] Encoding map {} doesn't not have a mapping for {} -> {}\n", map_name, value, name);
              std::exit(-1);
            }

            fmt::print("{}->{}\n", value, name);
          }
        }

        if(compressed_header_stream.total_bytes_read - initial_bytes_read != encoding_size)
        {
          fmt::print(stderr, "[ERROR] Encoding section overflowed its size\n");
          std::exit(-1);
        }
      }

      void parse_templates()
      {
        const uint64_t num_templates = parse_uleb();
        fmt::print("Reading {} templates\n", num_templates);

        // TODO: Populate template structures
      }

    public:
      header_parser(const std::string& header_file):
        compressed_header_stream(header_file) {}
      void parse() override
      {
        parse_prolog();
        parse_prolog_strings();
        parse_encoding_maps();
        parse_templates();
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
      const uint8_t cpu;
      stream_reader<BodyCompressedType> compressed_body_stream;

      // TODO: Declare the body structure here

    public:
      body_parser(uint8_t cpu_idx, const std::string& body_file):
        cpu(cpu_idx), compressed_body_stream(body_file) {}

      ooo_model_instr read() override
      {
        // TODO: Read the trace instruction by instruction and emit ooo_model_instr
        std::size_t bytes_read = 0;

        fmt::print("Reading from the body file\n");
        while(bytes_read <= 64)
        {
          std::optional<char> byte = compressed_body_stream.decompress();
          if(!byte)
            break;

          fmt::print("{:02x}", byte.value());
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

      bool eof() const override
      {
        return compressed_body_stream.eof;
      }

      ~body_parser() override = default;
  };

  std::unique_ptr<header_wrapper> header_stream = nullptr;
  std::unique_ptr<body_wrapper> body_stream = nullptr;

public:
  ooo_model_instr operator()() { return body_stream->read(); }

  wrong_path_tracereader(const std::string& tf, const uint8_t cpu_idx) : cpu(cpu_idx), trace_file(tf) { parse_trace(); }
  wrong_path_tracereader(champsim::wrong_path_tracereader&& other):
    cpu(other.cpu), trace_file(std::move(other.trace_file)),
    header_stream(std::move(other.header_stream)), body_stream(std::move(other.body_stream)) {}

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
  const bool success = std::filesystem::create_directory(extract_dir);
  if(!success)
  {
    fmt::print(stderr, "[ERROR] Could not create the directory to extract the wrong path trace. Exiting...\n");
    std::exit(-1);
  }
  return extract_dir;
}

void wrong_path_tracereader::extract_trace(const std::string& trace_file_name) const
{
  const std::string command = "tar -xf " + trace_file_name + " -C " + trace_extract_dir.string();
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
  const std::string header_file_name = get_header_path().string();
  if (const bool is_gzip_compressed = (header_file_name.substr(std::size(header_file_name) - 2) == "gz"); is_gzip_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::gzip_tag_t<>>>(header_file_name));
  }
  else if (const bool is_lzma_compressed = (header_file_name.substr(std::size(header_file_name) - 2) == "xz"); is_lzma_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::lzma_tag_t<>>>(header_file_name));
  }
  else if (const bool is_bzip2_compressed = (header_file_name.substr(std::size(header_file_name) - 3) == "bz2"); is_bzip2_compressed) {
    header_stream.reset(new header_parser<champsim::inf_istream<champsim::decomp_tags::bzip2_tag_t>>(header_file_name));
  }
  else {
    fmt::print(stderr, "[ERROR] Unknown compression format for trace header. Exiting...\n");
    std::exit(-1);
  }
}

void wrong_path_tracereader::construct_body_stream()
{
  const std::string body_file_name = get_body_path().string();
  if (const bool is_gzip_compressed = (body_file_name.substr(std::size(body_file_name) - 2) == "gz"); is_gzip_compressed) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::gzip_tag_t<>>>(cpu, body_file_name));
    return;
  }
  else if (const bool is_lzma_compressed = (body_file_name.substr(std::size(body_file_name) - 2) == "xz"); is_lzma_compressed) {
    body_stream.reset(new body_parser<champsim::inf_istream<champsim::decomp_tags::lzma_tag_t<>>>(cpu, body_file_name));
    return;
  }
  else if (const bool is_bzip2_compressed = (body_file_name.substr(std::size(body_file_name) - 3) == "bz2"); is_bzip2_compressed) {
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
