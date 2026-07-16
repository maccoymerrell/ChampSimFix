// Edge-recurrence analyzer for the physical block->block temporal (graph-edge) prefetch idea.
// Over a trace's DEMAND-load L2-miss stream (residual after berti: PREFETCH records are installed as fills),
// measures, at 64B-block granularity, whether the NEXT miss is predictable from a recent-miss SIGNATURE of
// length L=1/2/4 misses:
//   recurring = this miss's block was a previously-seen successor of the signature
//   top1      = it is the most-frequent successor (predict-1 coverage ceiling)
//   topK      = it is within the top-K successors (predict-K coverage ceiling)
// plus fan-out (distinct successors per signature) and working-set (distinct signatures/edges = state).
#include "trace_reader.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
using namespace sppam_dse;

// minimal set-associative LRU L2 at 64B-block granularity (residual-miss model).
struct L2 {
  int sets, ways;
  std::vector<uint64_t> tag; std::vector<uint64_t> lru; std::vector<char> valid; uint64_t clk = 0;
  L2(int s, int w) : sets(s), ways(w), tag((std::size_t)s * w), lru((std::size_t)s * w, 0), valid((std::size_t)s * w, 0) {}
  // returns true on hit; installs the block on a miss when install=true.
  bool access(uint64_t b, bool install) {
    int s = (int)(b % sets); std::size_t base = (std::size_t)s * ways, victim = base; bool hit = false;
    for (int w = 0; w < ways; ++w) { std::size_t i = base + w;
      if (valid[i] && tag[i] == b) { lru[i] = ++clk; hit = true; break; }
      if (!valid[i] || lru[i] < lru[victim]) victim = i; }
    if (!hit && install) { valid[victim] = 1; tag[victim] = b; lru[victim] = ++clk; }
    return hit;
  }
};

// per-signature successor histogram, capped to the top SUCCAP by count (top-K stays exact for small K).
static const int SUCCAP = 8;
struct Succ { std::vector<std::pair<uint64_t, uint32_t>> v; };
struct SigStats {
  std::unordered_map<uint64_t, Succ> m;
  uint64_t recurring = 0, top1 = 0, topK = 0, checks = 0; // checks = misses with a full-length signature available
  uint64_t fan_sum = 0; // sum of |successors| over updates (avg fan-out)
  // look up whether `b` is a known successor of `sig` and its rank, then record it.
  void observe(uint64_t sig, uint64_t b, int K) {
    auto it = m.find(sig);
    if (it != m.end()) {
      ++checks;
      auto& v = it->second.v;
      // rank by count (v not kept sorted; scan)
      uint32_t bc = 0; int better = 0; bool seen = false;
      for (auto& e : v) if (e.first == b) { bc = e.second; seen = true; }
      if (seen) { ++recurring; for (auto& e : v) if (e.first != b && e.second > bc) ++better;
                  if (better == 0) ++top1; if (better < K) ++topK; }
    } else ++checks; // signature unseen -> not recurring (counts against the rate)
    // update the histogram
    Succ& s = m[sig]; auto& v = s.v;
    bool found = false; for (auto& e : v) if (e.first == b) { ++e.second; found = true; break; }
    if (!found) {
      if ((int)v.size() < SUCCAP) v.push_back({b, 1});
      else { // replace the min-count entry (approx top-SUCCAP)
        std::size_t mn = 0; for (std::size_t i = 1; i < v.size(); ++i) if (v[i].second < v[mn].second) mn = i;
        if (v[mn].second <= 1) v[mn] = {b, 1};
      }
    }
    fan_sum += v.size();
  }
};

// Bounded set-associative edge table (the BUILDABLE structure): key = hash(prev miss block), entry holds top-SUCCAP
// successors; index by hash, LRU within set. Measures top-K coverage vs table size (ENTRIES = sets*ways).
struct EdgeTable {
  int sets, ways; uint64_t clk = 0;
  struct Entry { bool valid = false; uint64_t tag = 0; uint64_t lru = 0; std::vector<std::pair<uint64_t, uint32_t>> succ; };
  std::vector<Entry> e;
  uint64_t checks = 0, recurring = 0, topK = 0;
  EdgeTable(int s, int w) : sets(s), ways(w), e((std::size_t)s * w) {}
  void observe(uint64_t sig, uint64_t b, int K) {
    uint64_t h = sig * 0x9E3779B97F4A7C15ull; int s = (int)((h >> 20) % sets);
    std::size_t base = (std::size_t)s * ways, hit = SIZE_MAX, victim = base;
    for (int w = 0; w < ways; ++w) { std::size_t i = base + w;
      if (e[i].valid && e[i].tag == sig) { hit = i; }
      if (!e[i].valid || e[i].lru < e[victim].lru) victim = i; }
    ++checks;
    if (hit != SIZE_MAX) {
      auto& v = e[hit].succ; uint32_t bc = 0; int better = 0; bool seen = false;
      for (auto& p : v) if (p.first == b) { bc = p.second; seen = true; }
      if (seen) { ++recurring; for (auto& p : v) if (p.first != b && p.second > bc) ++better; if (better < K) ++topK; }
      e[hit].lru = ++clk;
      bool f = false; for (auto& p : v) if (p.first == b) { ++p.second; f = true; break; }
      if (!f) { if ((int)v.size() < SUCCAP) v.push_back({b, 1});
                else { std::size_t mn = 0; for (std::size_t i = 1; i < v.size(); ++i) if (v[i].second < v[mn].second) mn = i; if (v[mn].second <= 1) v[mn] = {b, 1}; } }
    } else { e[victim] = Entry{true, sig, ++clk, {{b, 1}}}; }
  }
  double cov(uint64_t denom) const { return 100.0 * topK / (denom ? denom : 1); }
};

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: %s <trace.zst> [maxrec] [K]\n", argv[0]); return 2; }
  std::string path = argv[1];
  uint64_t maxrec = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 12000000ull;
  int K = argc > 3 ? std::atoi(argv[3]) : 4;

  L2 l2(2048, 16);
  SigStats s1, s2, s4;
  // bounded set-assoc edge tables (L=1 key) at increasing sizes: 2K, 8K, 32K, 128K entries.
  std::vector<std::pair<const char*, EdgeTable>> bt;
  bt.emplace_back("2K",   EdgeTable(512, 4));
  bt.emplace_back("8K",   EdgeTable(2048, 4));
  bt.emplace_back("32K",  EdgeTable(8192, 4));
  bt.emplace_back("128K", EdgeTable(32768, 4));
  uint64_t hist[4] = {0, 0, 0, 0}; int nh = 0; // ring of last miss blocks, hist[0] most recent
  auto push = [&](uint64_t b) { hist[3] = hist[2]; hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = b; if (nh < 4) ++nh; };
  auto sig = [&](int L) { uint64_t x = 1469598103934665603ull; for (int i = 0; i < L; ++i) { x ^= hist[i]; x *= 1099511628211ull; } return x; };

  trace_reader tr(path);
  access_record rec; uint64_t total = 0, dmiss = 0;
  while (tr.next(rec)) {
    if (maxrec && ++total > maxrec) break;
    if (!rec.is_translated) continue;
    uint64_t blk = rec.paddr >> 6;
    atype t = (atype)rec.type;
    if (t == atype::PREFETCH) { l2.access(blk, true); continue; } // berti fill
    if (t != atype::LOAD && t != atype::RFO) continue;
    bool hit = l2.access(blk, true);
    if (hit) continue;
    ++dmiss;
    // predict THIS miss from the signature of the PREVIOUS misses, then advance
    if (nh >= 1) { s1.observe(sig(1), blk, K); for (auto& p : bt) p.second.observe(sig(1), blk, K); }
    if (nh >= 2) s2.observe(sig(2), blk, K);
    if (nh >= 4) s4.observe(sig(4), blk, K);
    push(blk);
  }

  auto report = [&](const char* name, SigStats& s) {
    double c = s.checks ? (double)s.checks : 1.0;
    std::printf("  L=%s  recurring=%.1f%%  top1=%.1f%%  top%d=%.1f%%  keys=%zu  avg_fanout=%.2f\n",
                name, 100.0 * s.recurring / c, 100.0 * s.top1 / c, K, 100.0 * s.topK / c,
                s.m.size(), s.recurring ? (double)s.fan_sum / (double)s.checks : 0.0);
  };
  std::printf("%s  demand_misses=%llu (of %llu recs)\n", path.c_str(), (unsigned long long)dmiss, (unsigned long long)total);
  report("1", s1); report("2", s2); report("4", s4);
  std::printf("  bounded L=1 top%d coverage (of demand misses): ", K);
  for (auto& p : bt) std::printf("%s=%.1f%%  ", p.first, p.second.cov(dmiss));
  std::printf("\n");
  return 0;
}
