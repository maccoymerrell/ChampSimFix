// sppam_dse: trace-driven SPPAM design-space evaluator.
#include <algorithm>
//
// Reads one or more L2C-access traces (.bin.zst) once and evaluates a grid of
// SPPAM configurations in a single streaming pass, reporting coverage /
// accuracy / latency per configuration as CSV.
//
// Single-core:  sppam_dse --trace T.bin.zst [--configs grid.json] [--out r.csv]
// Multi-core:   sppam_dse --traces T0.zst,T1.zst,... [--configs grid.json]
//
// In multi-core mode each trace drives one core; the cores share one LLC (its
// capacity scaled with the core count) and one DRAM bandwidth->latency model, so
// LLC capacity and DRAM bandwidth contention are modeled. Per-trace cycles are
// normalized to a common t=0 and the streams are merged by cycle.
//
// grid.json is a JSON array of config objects; each overrides any subset of the
// fields in params.h. With no --configs, the default SPPAM configuration runs.
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "evaluator.h"
#include "params.h"
#include "trace_reader.h"

using namespace sppam_dse;

namespace
{
struct classification {
  bool process = false, is_demand = false, feed_pred = false;
  atype type = atype::LOAD;
};

classification classify(const access_record& r, const params& p)
{
  classification c;
  c.type = static_cast<atype>(r.type);
  switch (c.type) {
  case atype::LOAD:
  case atype::RFO:
    c.process = true; c.is_demand = true; c.feed_pred = true; break;
  case atype::TRANSLATION:
    c.process = p.include_translation; c.is_demand = p.include_translation; c.feed_pred = p.include_translation; break;
  case atype::WRITE:
    c.process = p.include_writeback; c.is_demand = false; c.feed_pred = p.include_writeback; break;
  case atype::PREFETCH:
    // simulate_l1_prefetch: berti's L1 prefetch really fetches the line (DRAM
    // bandwidth) and fills L2 -- a PHYSICAL cost present whether or not we have an L2
    // prefetcher, so it must be modeled in BOTH the config and its no-pref baseline.
    // is_demand stays false (not a demand for the latency metric); feed_pred (train
    // our predictor on berti's stream) is the separate ALGORITHMIC toggle.
    c.process = p.simulate_l1_prefetch || p.count_prefetch_as_demand || p.train_on_prefetch;
    c.is_demand = p.count_prefetch_as_demand;
    c.feed_pred = p.train_on_prefetch;
    break;
  }
  return c;
}

struct config_run {
  params p;
  std::unique_ptr<eval_system> cfg;
  std::unique_ptr<eval_system> base;
};

// Per-trace stream state for the cycle-normalized k-way merge.
struct stream {
  std::unique_ptr<trace_reader> reader;
  access_record cur{};
  bool has_cur = false;
  uint64_t first_cycle = 0;
  bool first_seen = false;
  uint64_t norm_cycle = 0;

  bool advance()
  {
    has_cur = reader->next(cur);
    if (has_cur) {
      if (!first_seen) { first_cycle = cur.cycle; first_seen = true; }
      norm_cycle = cur.cycle - first_cycle;
    }
    return has_cur;
  }
};

std::vector<std::string> split_commas(const std::string& s)
{
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, ','))
    if (!tok.empty())
      out.push_back(tok);
  return out;
}
} // namespace

int main(int argc, char** argv)
{
  std::vector<std::string> traces;
  std::string configs_path, out_path, percore_path;
  uint64_t max_records = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
    if (a == "--trace") traces.push_back(next());
    else if (a == "--traces") { auto v = split_commas(next()); traces.insert(traces.end(), v.begin(), v.end()); }
    else if (a == "--configs") configs_path = next();
    else if (a == "--out") out_path = next();
    else if (a == "--percore") percore_path = next();   // emit per-core rows (mix victim analysis)
    else if (a == "--max-records") max_records = std::stoull(next());
    else { std::cerr << "unknown arg: " << a << "\n"; return 2; }
  }
  if (traces.empty()) { std::cerr << "error: provide --trace or --traces\n"; return 2; }
  const std::size_t ncores = traces.size();

  // Load configurations.
  std::vector<config_run> runs;
  if (configs_path.empty()) {
    runs.emplace_back();
  } else {
    std::ifstream f(configs_path);
    if (!f) { std::cerr << "error: cannot open " << configs_path << "\n"; return 2; }
    nlohmann::json j; f >> j;
    if (!j.is_array()) { std::cerr << "error: configs file must be a JSON array\n"; return 2; }
    for (const auto& obj : j) { config_run cr; apply_json(cr.p, obj); runs.push_back(std::move(cr)); }
  }
  for (auto& r : runs) {
    r.cfg = std::make_unique<eval_system>(r.p, ncores, true);
    r.base = std::make_unique<eval_system>(r.p, ncores, false);
  }

  // Open and prime the per-trace streams.
  std::vector<stream> streams(ncores);
  for (std::size_t i = 0; i < ncores; ++i) {
    streams[i].reader = std::make_unique<trace_reader>(traces[i]);
    streams[i].advance();
  }

  // Uncovered-miss attribution (single-core analysis): engine-independent access history, and per-config
  // tally of demand misses SPPAM did NOT cover, split by class [0]=cold-page [1]=spatial-in-page [2]=temporal.
  std::unordered_set<uint64_t> seen_pg, seen_blk;
  std::vector<std::array<uint64_t, 3>> uncov(runs.size(), std::array<uint64_t, 3>{0, 0, 0});
  // For uncovered SPATIAL misses: histogram the in-page delta (off - last off in same 4KB page). Concentrated
  // in a few deltas => predictable stride/pattern SPPAM is missing; spread => pointer-irregular (structural).
  std::unordered_map<uint64_t, int> last_off;
  std::vector<std::unordered_map<int, uint64_t>> uncov_delta(runs.size());
  // Per-PC-stride ORACLE probe (run 0): for uncovered SPATIAL misses, histogram delta per accessing PC. If each PC
  // maps to a DOMINANT delta, a per-PC-stride predictor would catch it -> the residual IS exploitable (by PC), not a
  // ceiling. Coverage = sum over PCs of their most-frequent delta count / total uncovered spatial.
  std::unordered_map<uint64_t, std::unordered_map<int, uint64_t>> pc_spat_delta;
  // For uncovered TEMPORAL-revisit misses: was the block's region STILL in SPPAM's region table? [0]=resident
  // (PHT context retained -> confidence/aggression/PHT-cap issue) [1]=evicted (region-table capacity).
  std::vector<std::array<uint64_t, 2>> temp_region(runs.size(), std::array<uint64_t, 2>{0, 0});
  // For uncovered TEMPORAL misses: does SPPAM's shadow claim the block RESIDENT (would squash a re-prefetch)? [0]=yes-stale [1]=no
  std::vector<std::array<uint64_t, 2>> temp_shadow(runs.size(), std::array<uint64_t, 2>{0, 0});
  // Of the shadow-says-RESIDENT false positives: is the block in the pure fill/evict mirror (desync) or not (stale bit)?
  std::vector<std::array<uint64_t, 2>> temp_fp(runs.size(), std::array<uint64_t, 2>{0, 0});
  // Self-reference test: for uncovered misses whose REGION IS RESIDENT, histogram delta = off - region.last_block.
  // Small bidirectional deltas => the missed line falls inside/near the recent window (delta prediction would catch it);
  // large/one-sided => genuine directional stride the position bitmap should already handle.
  std::vector<std::unordered_map<int, uint64_t>> uncov_res_delta(runs.size());
  uint64_t expl_count = 0; // detailed dump of the first few uncovered temporal misses (run 0), for post-mortem

  // k-way merge by normalized cycle; dispatch each record to its core.
  uint64_t total = 0;
  while (true) {
    std::size_t pick = ncores;
    uint64_t best = 0;
    for (std::size_t i = 0; i < ncores; ++i)
      if (streams[i].has_cur && (pick == ncores || streams[i].norm_cycle < best)) { pick = i; best = streams[i].norm_cycle; }
    if (pick == ncores)
      break;
    const access_record rec = streams[pick].cur;
    streams[pick].advance();

    ++total;
    if (max_records && total > max_records)
      break;
    uint64_t block = rec.paddr >> 6;
    // engine-independent class of this access: 0=cold-page (compulsory), 1=spatial-in-page (new block in a
    // seen 4KB page = SPPAM's domain), 2=temporal-revisit (block accessed before -> evicted & re-missed).
    int acls = -1, adelta = 0;
    { atype t = static_cast<atype>(rec.type);
      if (t == atype::LOAD || t == atype::RFO || t == atype::PREFETCH) {
        uint64_t pg = block >> 6; int off = static_cast<int>(block & 63);
        acls = !seen_pg.count(pg) ? 0 : (!seen_blk.count(block) ? 1 : 2);
        auto lo = last_off.find(pg); adelta = (lo != last_off.end()) ? (off - lo->second) : 0;
        seen_pg.insert(pg); seen_blk.insert(block); last_off[pg] = off; } }
    for (std::size_t ri = 0; ri < runs.size(); ++ri) {
      auto& r = runs[ri];
      classification c = classify(rec, r.p);
      if (!c.process)
        continue;
      auto b = r.base->at(pick).access(block, rec.ip, best, c.type, c.is_demand, c.feed_pred, false);
      bool rres = false; int sstat = 0; bool exres = false; int lbd = -1000; std::string expl;
      if (c.is_demand && acls >= 1) rres = r.cfg->at(pick).region_resident(block), lbd = r.cfg->at(pick).last_block_delta(block); // pre-demand region state
      if (c.is_demand && acls == 2) { sstat = r.cfg->at(pick).shadow_status(block); exres = r.cfg->at(pick).exact_resident(block);
        if (ri == 0 && !b.hit && expl_count < 12) expl = r.cfg->at(pick).explain(block); } // capture pre-demand state
      auto cf = r.cfg->at(pick).access(block, rec.ip, best, c.type, c.is_demand, c.feed_pred, !b.hit);
      if (c.is_demand && acls >= 0 && !b.hit && !cf.hit) { uncov[ri][acls]++; // uncovered demand miss
        if (acls >= 1 && rres && lbd != -1000 && lbd >= -40 && lbd <= 40) uncov_res_delta[ri][lbd]++; // region-resident: delta from last access
        if (acls == 1) { uncov_delta[ri][adelta]++;        // spatial: record its in-page delta
          if (ri == 0) pc_spat_delta[rec.ip][adelta]++; }  // per-PC delta (PC-stride oracle probe)
        else if (acls == 2) { temp_region[ri][rres ? 0 : 1]++; temp_shadow[ri][sstat == 2 ? 0 : 1]++;
          if (sstat == 2) temp_fp[ri][exres ? 0 : 1]++;
          if (ri == 0 && !expl.empty() && expl_count < 12) { std::cerr << "[temporal-miss #" << expl_count << "] block=0x" << std::hex << block << std::dec
            << " region_resident=" << rres << " shadow=" << sstat << " ip=0x" << std::hex << rec.ip << std::dec << "\n" << expl; ++expl_count; } } }
    }
  }

  // Emit CSV (aggregate over cores).
  std::ostream* os = &std::cout;
  std::ofstream ofs;
  if (!out_path.empty()) { ofs.open(out_path); os = &ofs; }
  *os << "name,state_kib,cores,demands,baseline_misses,l2_misses,covered_timely,covered_late,coverage,coverage_incl_late,"
         "pf_issued,pf_useful,pf_useless,accuracy,pf_per_demand,avg_latency,baseline_avg_latency,latency_saved,region_miss_rate,"
         "pe_upf_sppam,pe_upf_spp,pe_poll_sppam,pe_poll_spp,pe_lat_sppam,pe_lat_spp,poll_misses,"
         "l2_acc_all,l2_miss_all,avg_miss_lat_all,dram_fetches,avg_dram_lat,cycle_span\n";
  for (auto& r : runs) {
    metrics m = r.cfg->aggregate();
    metrics bm = r.base->aggregate();
    double base_misses = static_cast<double>(bm.misses);
    double cov = base_misses > 0 ? static_cast<double>(m.covered_timely) / base_misses : 0.0;
    double cov_late = base_misses > 0 ? static_cast<double>(m.covered_timely + m.covered_late) / base_misses : 0.0;
    // accuracy = useful / (useful + useless): of RESOLVED prefetches (used before
    // eviction vs evicted before use), the fraction used. Excludes redundant/
    // still-resident prefetches that pf_issued would wrongly count.
    double acc = (m.pf_useful + m.pf_useless) > 0 ? static_cast<double>(m.pf_useful) / static_cast<double>(m.pf_useful + m.pf_useless) : 0.0;
    double ppd = m.demands > 0 ? static_cast<double>(m.pf_issued) / static_cast<double>(m.demands) : 0.0;
    double avg = m.demands > 0 ? m.sum_demand_latency / static_cast<double>(m.demands) : 0.0;
    double bavg = bm.demands > 0 ? bm.sum_demand_latency / static_cast<double>(bm.demands) : 0.0;
    *os << r.p.name << ',' << r.p.state_kib() << ',' << ncores << ',' << m.demands << ',' << bm.misses << ',' << m.misses << ',' << m.covered_timely << ',' << m.covered_late << ','
        << cov << ',' << cov_late << ',' << m.pf_issued << ',' << m.pf_useful << ',' << m.pf_useless << ',' << acc << ','
        << ppd << ',' << avg << ',' << bavg << ',' << (bavg - avg) << ','
        << (m.region_demand_accesses > 0 ? static_cast<double>(m.region_demand_misses) / static_cast<double>(m.region_demand_accesses) : 0.0) << ','
        << m.i_upf[0] << ',' << m.i_upf[1] << ',' << m.i_poll[0] << ',' << m.i_poll[1] << ',' << m.i_lat[0] << ',' << m.i_lat[1] << ',' << (m.poll_misses[0] + m.poll_misses[1]) << ','
        << m.l2_acc_all << ',' << m.l2_miss_all << ',' << (m.l2_miss_all > 0 ? m.sum_miss_lat_all / static_cast<double>(m.l2_miss_all) : 0.0) << ','
        << m.dram_fetches << ',' << (m.dram_fetches > 0 ? m.dram_lat_sum / static_cast<double>(m.dram_fetches) : 0.0) << ','
        << (m.last_cycle - m.first_cycle) << '\n';
    // Sparse-tail probe: SPPAM useful/useless by source-region density bucket [<=4,8,16,32,64 blk]
    std::cerr << "[dens] " << r.p.name
              << " useful=" << m.pf_useful_dens[0] << ',' << m.pf_useful_dens[1] << ',' << m.pf_useful_dens[2] << ',' << m.pf_useful_dens[3] << ',' << m.pf_useful_dens[4]
              << " useless=" << m.pf_useless_dens[0] << ',' << m.pf_useless_dens[1] << ',' << m.pf_useless_dens[2] << ',' << m.pf_useless_dens[3] << ',' << m.pf_useless_dens[4]
              << " region_evict=" << m.region_evictions << " staging_drop=" << m.staging_drops << '\n';
    // coverage direct-vs-indirect: of covered demands (baseline-miss -> cfg-hit), how many hit an UNUSED PREFETCH
    // (real prefetch coverage) vs were resident for another reason (natural reuse under prefetch-altered state).
    std::cerr << "[covsplit] " << r.p.name << " coverage=" << cov * 100 << "% direct(pf-hit)="
              << (m.covered_timely ? 100.0 * m.covered_direct / m.covered_timely : 0.0) << "% of covered"
              << "  (covered=" << m.covered_timely << " direct=" << m.covered_direct << ")\n";
  }
  // Uncovered-miss composition per config: WHERE the residual is locked (cold=compulsory,
  // spatial=SPPAM's own domain left on the table, temporal=evicted-and-re-missed / capacity).
  for (std::size_t ri = 0; ri < runs.size(); ++ri) {
    uint64_t u0 = uncov[ri][0], u1 = uncov[ri][1], u2 = uncov[ri][2], us = u0 + u1 + u2;
    double d = us ? static_cast<double>(us) : 1.0;
    std::cerr << "[uncov] " << runs[ri].p.name << " uncovered_demand_misses=" << us
              << " cold-page=" << (100.0 * u0 / d) << "% spatial-in-page=" << (100.0 * u1 / d)
              << "% temporal-revisit=" << (100.0 * u2 / d) << "%\n";
    // stride concentration of the uncovered SPATIAL misses: top-8 deltas' share (high => predictable stride).
    std::vector<std::pair<int, uint64_t>> dv(uncov_delta[ri].begin(), uncov_delta[ri].end());
    std::sort(dv.begin(), dv.end(), [](auto& a, auto& b) { return a.second > b.second; });
    uint64_t dtot = 0; for (auto& kv : dv) dtot += kv.second; if (!dtot) dtot = 1;
    double top8 = 0; std::string tops;
    for (std::size_t k = 0; k < dv.size() && k < 8; ++k) { top8 += 100.0 * dv[k].second / dtot;
      tops += (k ? "," : "") + std::to_string(dv[k].first) + ":" + std::to_string(static_cast<int>(100.0 * dv[k].second / dtot)) + "%"; }
    std::cerr << "[uncov-spatial-stride] " << runs[ri].p.name << " distinct_deltas=" << dv.size()
              << " top8_share=" << top8 << "% [" << tops << "]\n";
    // PC-stride oracle: fraction of uncovered spatial misses caught by "predict each PC's dominant delta" (run 0 only).
    if (ri == 0 && !pc_spat_delta.empty()) {
      uint64_t tot = 0, dom = 0, pcs_deterministic = 0;
      for (auto& kv : pc_spat_delta) { uint64_t pt = 0, pd = 0;
        for (auto& d : kv.second) { pt += d.second; if (d.second > pd) pd = d.second; }
        tot += pt; dom += pd; if (pd * 100 >= pt * 70) ++pcs_deterministic; }
      std::cerr << "[uncov-spatial-pcoracle] " << runs[ri].p.name << " per-PC-dominant-delta covers "
                << (tot ? 100.0 * dom / tot : 0.0) << "% of uncovered spatial  (distinct_PCs=" << pc_spat_delta.size()
                << " deterministic>=70%=" << (pc_spat_delta.size() ? 100 * pcs_deterministic / pc_spat_delta.size() : 0) << "%)\n";
    }
    // self-reference test: delta from region.last_block for region-RESIDENT uncovered misses. Bucket by |delta| band
    // and forward/backward share. within-window (|d|<=ps) + bidirectional => delta prediction is the missing capability.
    { std::vector<std::pair<int, uint64_t>> rv(uncov_res_delta[ri].begin(), uncov_res_delta[ri].end());
      uint64_t rtot = 0, fwd = 0, bwd = 0, in6 = 0, in3 = 0, at1 = 0; for (auto& kv : rv) { rtot += kv.second;
        if (kv.first > 0) fwd += kv.second; else if (kv.first < 0) bwd += kv.second;
        if (std::abs(kv.first) <= 6) in6 += kv.second; if (std::abs(kv.first) <= 3) in3 += kv.second; if (std::abs(kv.first) == 1) at1 += kv.second; }
      std::sort(rv.begin(), rv.end(), [](auto& a, auto& b) { return a.second > b.second; });
      double rd = rtot ? static_cast<double>(rtot) : 1.0; std::string rtops;
      for (std::size_t k = 0; k < rv.size() && k < 10; ++k) rtops += (k ? "," : "") + std::to_string(rv[k].first) + ":" + std::to_string(static_cast<int>(100.0 * rv[k].second / rd)) + "%";
      std::cerr << "[uncov-resident-delta] " << runs[ri].p.name << " resident_uncov=" << rtot
                << " fwd=" << (100.0 * fwd / rd) << "% bwd=" << (100.0 * bwd / rd) << "% |d|<=6=" << (100.0 * in6 / rd)
                << "% |d|<=3=" << (100.0 * in3 / rd) << "% |d|==1=" << (100.0 * at1 / rd) << "% top[" << rtops << "]\n"; }
    // temporal-revisit misses: region still in the table (PHT context retained) vs evicted (region capacity)?
    uint64_t tr = temp_region[ri][0], te = temp_region[ri][1], tt = tr + te; if (!tt) tt = 1;
    std::cerr << "[uncov-temporal-region] " << runs[ri].p.name << " temporal_uncov=" << (tr + te)
              << " region_RESIDENT=" << (100.0 * tr / tt) << "% region_evicted=" << (100.0 * te / tt) << "%\n";
    uint64_t ss = temp_shadow[ri][0], sn = temp_shadow[ri][1], sst = ss + sn; if (!sst) sst = 1;
    std::cerr << "[uncov-temporal-shadow] " << runs[ri].p.name
              << " shadow-says-RESIDENT(squash-would-block)=" << (100.0 * ss / sst) << "%  shadow-absent(re-prefetchable)=" << (100.0 * sn / sst) << "%\n";
    uint64_t fd = temp_fp[ri][0], fs = temp_fp[ri][1], fpt = fd + fs; if (!fpt) fpt = 1;
    std::cerr << "[uncov-temporal-fp] " << runs[ri].p.name << " of-false-positives: in-pure-mirror(fill/evict desync)="
              << (100.0 * fd / fpt) << "%  stale-bit(set-at-issue/access, never-cleared)=" << (100.0 * fs / fpt) << "%\n";
  }
  // Per-core rows (mix victim analysis): each core's demand miss + latency under
  // contention, for the config and its paired no-prefetch baseline in the same mix.
  if (!percore_path.empty()) {
    std::ofstream pc(percore_path);
    pc << "name,core,trace,demands,base_misses,l2_misses,avg_latency,base_avg_latency\n";
    for (auto& r : runs)
      for (std::size_t i = 0; i < ncores; ++i) {
        metrics m = r.cfg->at(i).result();
        metrics b = r.base->at(i).result();
        auto base_of = [](const std::string& p) { auto s = p.find_last_of('/'); auto t = (s == std::string::npos) ? p : p.substr(s + 1); auto d = t.find('.'); return d == std::string::npos ? t : t.substr(0, d); };
        double avg = m.demands ? m.sum_demand_latency / m.demands : 0.0;
        double bavg = b.demands ? b.sum_demand_latency / b.demands : 0.0;
        pc << r.p.name << ',' << i << ',' << base_of(traces[i]) << ',' << m.demands << ',' << b.misses << ',' << m.misses << ',' << avg << ',' << bavg << '\n';
      }
    std::cerr << "wrote per-core " << percore_path << "\n";
  }

  std::cerr << "processed " << total << " records across " << ncores << " core(s), " << runs.size() << " config(s)\n";
  return 0;
}
