/*
 * Event tracer for a cache level (default: L2C). Emits three parallel zstd
 * streams so offline tools can reconstruct the full prefetch lifecycle:
 *   .acc.zst   -- every tag-check access, with hit + useful-prefetch flags
 *   .pf.zst    -- every prefetch the cache's prefetcher issues (fill level, trigger PC)
 *   .evict.zst -- every fill's victim, with dead-prefetch/dirty flags
 *
 * Header-only and entirely opt-in: a CACHE constructs one inactive by value and
 * only opens streams when the CHAMPSIM_EVTRACE env var is set, so ordinary runs
 * pay one null-pointer test per access and nothing else. Streams are piped
 * through `zstd` (single-threaded; compression is not the bottleneck), matching
 * logging_channel's approach.
 */
#ifndef EVENT_TRACE_H
#define EVENT_TRACE_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace champsim
{
#pragma pack(push, 1)
struct ev_trace_header {
  char magic[8];
  uint16_t version;
  uint16_t record_bytes;
  uint32_t reserved;
};

// One row per tag-check access into the traced cache.
struct ev_access_record {
  uint64_t cycle;
  uint64_t ip;
  uint64_t vaddr;
  uint64_t paddr;
  uint64_t instr_id;
  uint8_t type;          // access_type: 0 LOAD 1 RFO 2 PREFETCH 3 WRITE 4 TRANSLATION
  uint8_t is_prefetch;   // 1 if this access is a prefetch issued by THIS cache
  uint8_t hit;           // 1 if it hit
  uint8_t useful;        // 1 if it hit a prefetched, not-yet-demand-used line
  uint8_t is_translated; // 1 if paddr is valid
  uint8_t pad[3];
};

// One row per prefetch the cache's prefetcher requests (prefetch_line).
struct ev_pf_record {
  uint64_t cycle;
  uint64_t pf_addr;        // address requested (physical iff !virtual_prefetch)
  uint64_t trigger_ip;     // PC of the access that was being serviced when issued
  uint32_t metadata;       // prefetcher metadata word
  uint8_t fill_this_level; // 1 => fill L2, 0 => LLC-only (overflow placement)
  uint8_t accepted;        // 1 => enqueued, 0 => dropped (PQ full)
  uint8_t pad[2];
};

// One row per fill that replaces a valid victim.
struct ev_evict_record {
  uint64_t cycle;
  uint64_t victim_paddr;
  uint64_t fill_paddr;     // the incoming line that caused the eviction
  uint64_t fill_ip;
  uint8_t fill_is_prefetch; // incoming fill was a prefetch
  uint8_t victim_dead_pf;   // victim was prefetched and never demand-used
  uint8_t victim_dirty;
  uint8_t victim_valid;
  uint8_t pad[4];
};
#pragma pack(pop)

constexpr uint16_t EV_TRACE_VERSION = 1;

class event_trace_set
{
public:
  event_trace_set() = default;
  ~event_trace_set() { close(); }
  event_trace_set(const event_trace_set&) = delete;
  event_trace_set& operator=(const event_trace_set&) = delete;
  event_trace_set(event_trace_set&& o) noexcept { steal(o); }
  event_trace_set& operator=(event_trace_set&& o) noexcept
  {
    if (this != &o) { close(); steal(o); }
    return *this;
  }

  bool active() const { return acc_ != nullptr; }

  // Opens <prefix>.<name>.{acc,pf,evict}.zst. Safe to call once per sim phase.
  void open(const std::string& prefix, const std::string& name, int zstd_level = 3)
  {
    if (acc_ != nullptr)
      return;
    acc_ = open_stream(prefix + "." + name + ".acc.zst", "EVACC1", static_cast<uint16_t>(sizeof(ev_access_record)), zstd_level);
    pf_ = open_stream(prefix + "." + name + ".pf.zst", "EVPF01", static_cast<uint16_t>(sizeof(ev_pf_record)), zstd_level);
    evi_ = open_stream(prefix + "." + name + ".evict.zst", "EVEVI1", static_cast<uint16_t>(sizeof(ev_evict_record)), zstd_level);
  }

  void close()
  {
    if (acc_) { pclose(acc_); acc_ = nullptr; }
    if (pf_) { pclose(pf_); pf_ = nullptr; }
    if (evi_) { pclose(evi_); evi_ = nullptr; }
  }

  void access(const ev_access_record& r) { if (acc_) std::fwrite(&r, sizeof r, 1, acc_); }
  void pf(const ev_pf_record& r) { if (pf_) std::fwrite(&r, sizeof r, 1, pf_); }
  void evict(const ev_evict_record& r) { if (evi_) std::fwrite(&r, sizeof r, 1, evi_); }

private:
  static std::FILE* open_stream(const std::string& path, const char* magic, uint16_t recbytes, int level)
  {
    std::string cmd = "zstd -q -f -" + std::to_string(level) + " > '" + path + "'";
    std::FILE* f = popen(cmd.c_str(), "w");
    if (f == nullptr) {
      std::fprintf(stderr, "[EVENT_TRACE] ERROR: failed to launch zstd for '%s'\n", path.c_str());
      return nullptr;
    }
    ev_trace_header hdr{};
    std::memcpy(hdr.magic, magic, std::strlen(magic) < 8 ? std::strlen(magic) : 8);
    hdr.version = EV_TRACE_VERSION;
    hdr.record_bytes = recbytes;
    hdr.reserved = 0;
    std::fwrite(&hdr, sizeof hdr, 1, f);
    return f;
  }

  void steal(event_trace_set& o) { acc_ = o.acc_; pf_ = o.pf_; evi_ = o.evi_; o.acc_ = o.pf_ = o.evi_ = nullptr; }

  std::FILE* acc_ = nullptr;
  std::FILE* pf_ = nullptr;
  std::FILE* evi_ = nullptr;
};

} // namespace champsim

#endif
