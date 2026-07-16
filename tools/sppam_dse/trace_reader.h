// Streaming reader for logging_channel L2C-access traces (.bin.zst).
//
// The on-disk format is defined by inc/logging_channel.h: a 16-byte header
// followed by fixed-width little-endian l2c_access_record structs. We stream
// the decompressed bytes from `zstd -dc` via popen (libzstd dev headers are not
// installed here, and decompression is not the bottleneck).
#ifndef SPPAM_DSE_TRACE_READER_H
#define SPPAM_DSE_TRACE_READER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "access_kind.h"

namespace sppam_dse
{

#pragma pack(push, 1)
struct trace_header {
  char magic[8];
  uint16_t version;
  uint16_t record_bytes;
  uint32_t reserved;
};

// Mirrors champsim::l2c_access_record (inc/logging_channel.h).
struct access_record {
  uint64_t cycle;
  uint64_t ip;
  uint64_t vaddr;
  uint64_t paddr;
  uint64_t instr_id;
  uint8_t type;          // 0 LOAD, 1 RFO, 2 PREFETCH, 3 WRITE, 4 TRANSLATION
  uint8_t is_prefetch;   // 1 if prefetch, 0 if demand
  uint8_t queue;         // 0 RQ, 1 WQ, 2 PQ
  uint8_t cpu;
  uint8_t is_translated; // 1 if paddr valid
};
#pragma pack(pop)

// Streams records from a .zst trace. Reads in chunks for throughput; call
// next() until it returns false.
class trace_reader
{
public:
  explicit trace_reader(const std::string& path) : buf_(kRecords * sizeof(access_record))
  {
    // Single-quote the path for the shell; trace paths are tool-generated and
    // contain no quotes.
    // -f so zstd follows symlinked trace paths (it otherwise skips them).
    std::string cmd = "zstd -dc -q -f '" + path + "'";
    pipe_ = popen(cmd.c_str(), "r");
    if (pipe_ == nullptr)
      throw std::runtime_error("failed to launch zstd for " + path);

    trace_header hdr{};
    if (read_exact(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != sizeof(hdr))
      throw std::runtime_error("trace too short for header: " + path);
    if (std::strncmp(hdr.magic, "L2CTRC", 6) != 0)
      throw std::runtime_error("bad trace magic in " + path);
    if (hdr.record_bytes != sizeof(access_record))
      throw std::runtime_error("record size mismatch in " + path + " (file "
                               + std::to_string(hdr.record_bytes) + ", tool "
                               + std::to_string(sizeof(access_record)) + ")");
  }

  trace_reader(const trace_reader&) = delete;
  trace_reader& operator=(const trace_reader&) = delete;

  ~trace_reader()
  {
    if (pipe_ != nullptr)
      pclose(pipe_);
  }

  // Fills `rec` with the next record; returns false at end of stream.
  bool next(access_record& rec)
  {
    if (pos_ >= filled_) {
      refill();
      if (filled_ == 0)
        return false;
    }
    std::memcpy(&rec, buf_.data() + pos_, sizeof(access_record));
    pos_ += sizeof(access_record);
    return true;
  }

private:
  static constexpr std::size_t kRecords = 1 << 16; // records per refill chunk

  void refill()
  {
    std::size_t got = read_exact(buf_.data(), buf_.size());
    // Truncate to a whole number of records (the final chunk may be partial).
    filled_ = (got / sizeof(access_record)) * sizeof(access_record);
    pos_ = 0;
  }

  // Reads up to n bytes, looping over short fread()s until EOF or full.
  std::size_t read_exact(char* dst, std::size_t n)
  {
    std::size_t total = 0;
    while (total < n) {
      std::size_t r = std::fread(dst + total, 1, n - total, pipe_);
      if (r == 0)
        break;
      total += r;
    }
    return total;
  }

  std::FILE* pipe_ = nullptr;
  std::vector<char> buf_;
  std::size_t filled_ = 0;
  std::size_t pos_ = 0;
};

} // namespace sppam_dse

#endif
