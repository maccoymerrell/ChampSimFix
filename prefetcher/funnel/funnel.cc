#include "funnel.h"

#include <algorithm>

#include "cache.h"

champsim::modules::prefetcher::register_module<funnel> funnel_register("funnel");

funnel::funnel(champsim::modules::ModuleBuilder builder)
  : cache_(builder.get_parent<champsim::modules::cache_module>()),
    radius_(builder.get_parameter<int>("funnel_radius", true, 2)),
    theta_(builder.get_parameter<int>("funnel_theta", true, 1)),
    theta_train_(builder.get_parameter<int>("funnel_theta_train", true, 256)),
    pstep_(builder.get_parameter<int>("funnel_pstep", true, 2)),
    window_(builder.get_parameter<unsigned>("funnel_window", true, 1024)),
    page_size_(builder.get_parameter<unsigned>("page_size")),
    block_size_(builder.get_parameter<unsigned>("block_size")),
    block_in_page_extent_(champsim::data::bits{builder.get_parameter<unsigned>("log2_page_size")},
                          champsim::data::bits{builder.get_parameter<unsigned>("log2_block_size")})
{
  if (radius_ < 0)
    radius_ = 0;
  bpp_ = static_cast<int>(page_size_ / block_size_);
  win_ = 2 * radius_ + 1;
  nin_ = nout_ = win_ * bpp_;
  nmask_ = (nout_ + 63) / 64;
  W_.assign(static_cast<size_t>(nin_) * nout_, 0);
  score_.assign(nout_, 0);
  active_in_.reserve(nin_);
  rc_ = window_ + 2;
  r_page_.assign(rc_, 0);
  r_time_.assign(rc_, 0);
  r_off_.assign(rc_, 0);
  r_ain_.assign(rc_ * nin_, 0);
  r_len_.assign(rc_, 0);
  r_conf_.assign(rc_ * nmask_, 0);
}

void funnel::mark_access(champsim::address addr)
{
  auto [pn, off] = page_and_offset(addr);
  auto idx = off.template to<std::size_t>();
  auto region = regions.check_hit(make_region(pn));
  if (region.has_value()) {
    region->access_map.at(idx) = true;
    regions.fill(region.value());
  } else {
    auto r = make_region(pn);
    r.access_map.at(idx) = true;
    regions.fill(r);
  }
}

// Gather active input cells from the +/-R page window around the anchor page.
void funnel::build_input(champsim::page_number anchor_pn, std::vector<int>& out)
{
  out.clear();
  const auto base = static_cast<int64_t>(anchor_pn.template to<uint64_t>());
  for (int pd = -radius_; pd <= radius_; ++pd) {
    int64_t pg = base + pd;
    if (pg < 0)
      continue;
    auto region = regions.check_hit(make_region(champsim::page_number{static_cast<uint64_t>(pg)}));
    if (!region.has_value())
      continue;
    const int row_base = (pd + radius_) * bpp_;
    const auto& m = region->access_map;
    for (int c = 0; c < bpp_; ++c)
      if (m[static_cast<std::size_t>(c)])
        out.push_back(row_base + c);
  }
}

long funnel::col_score(const int* ain, int len, int o) const
{
  long v = 0;
  for (int t = 0; t < len; ++t)
    v += W_[static_cast<size_t>(ain[t]) * nout_ + o];
  return v;
}

void funnel::add_col(const int* ain, int len, int o, long delta)
{
  for (int t = 0; t < len; ++t) {
    int16_t* w = &W_[static_cast<size_t>(ain[t]) * nout_ + o];
    int v = *w + static_cast<int>(delta);
    *w = static_cast<int16_t>(v > W_CAP ? W_CAP : (v < -W_CAP ? -W_CAP : v));
  }
}

int funnel::draw_negative(const uint64_t* confirmed, int exclude)
{
  for (int tries = 0; tries < 8; ++tries) {
    lfsr_ ^= lfsr_ << 13;
    lfsr_ ^= lfsr_ >> 17;
    lfsr_ ^= lfsr_ << 5;
    int o = static_cast<int>(lfsr_ % static_cast<uint32_t>(nout_));
    if (o == exclude)
      continue;
    if ((confirmed[o >> 6] >> (o & 63)) & 1)
      continue; // confirmed cell is a true positive, not a valid negative
    return o;
  }
  return -1;
}

uint32_t funnel::prefetcher_cache_operate(champsim::address addr, champsim::address /*ip*/, bool /*cache_hit*/, bool /*useful_prefetch*/,
                                          access_type /*type*/, uint32_t metadata_in)
{
  ++time_;
  auto [anchor_pn, anchor_off_slice] = page_and_offset(addr);
  const int anchor_off = static_cast<int>(anchor_off_slice.template to<std::size_t>());
  const champsim::block_number anchor_blk{addr};
  const bool two_level = cache_->get_mshr_occupancy_ratio() < 0.5;

  // (1) FORWARD + ISSUE, using the access map BEFORE marking this access (no self-leak).
  build_input(anchor_pn, active_in_);
  for (int o = 0; o < nout_; ++o)
    score_[o] = 0; // bias == 0
  for (int in : active_in_) {
    const int16_t* row = &W_[static_cast<size_t>(in) * nout_];
    for (int o = 0; o < nout_; ++o)
      score_[o] += row[o];
  }
  for (int o = 0; o < nout_; ++o) {
    if (score_[o] <= theta_)
      continue;
    const int pd = o / bpp_ - radius_;
    const int off = o % bpp_;
    if (pd == 0 && off == anchor_off)
      continue; // self block is the demand, never a prefetch
    const long blk_delta = static_cast<long>(pd) * bpp_ + (off - anchor_off);
    champsim::address pf_addr{anchor_blk + blk_delta};
    prefetch_line(pf_addr, two_level, metadata_in); // ChampSim/MSHR drops redundant/in-flight
  }

  // (2) TRAIN (positive-only, BPR): the current access confirms cell o of any pending anchor s
  // whose +/-R output grid contains it. Update s's frozen weights (causal: s precedes now).
  const int64_t cur_page = static_cast<int64_t>(anchor_pn.template to<uint64_t>());
  for (uint64_t k = 0; k < r_count_; ++k) {
    const uint64_t s = (r_head_ + k) % rc_;
    const int64_t pd = cur_page - static_cast<int64_t>(r_page_[s]);
    if (pd < -radius_ || pd > radius_)
      continue;
    if (pd == 0 && anchor_off == static_cast<int>(r_off_[s]))
      continue; // current access is s's own trigger block -- not a prediction
    const int o = static_cast<int>((pd + radius_) * bpp_ + anchor_off);
    uint64_t* conf = &r_conf_[s * nmask_];
    if ((conf[o >> 6] >> (o & 63)) & 1)
      continue; // already credited
    const int32_t* ain = &r_ain_[s * nin_];
    const int len = r_len_[s];
    const long so = col_score(ain, len, o);
    const int c = draw_negative(conf, o);
    if (c >= 0) {
      const long sc = col_score(ain, len, c);
      if (so - sc < theta_train_) { // pairwise surprise gate
        add_col(ain, len, o, +pstep_);
        add_col(ain, len, c, -pstep_);
      }
    }
    conf[o >> 6] |= (1ull << (o & 63));
  }

  // (3) drop anchors whose training window has closed.
  while (r_count_ > 0 && r_time_[r_head_] + window_ < time_) {
    r_head_ = (r_head_ + 1) % rc_;
    --r_count_;
  }

  // (4) enqueue this access as a new pending anchor (freeze its input snapshot).
  if (r_count_ >= rc_ - 1) { // safety: never overrun the ring
    r_head_ = (r_head_ + 1) % rc_;
    --r_count_;
  }
  const uint64_t s = (r_head_ + r_count_) % rc_;
  r_page_[s] = anchor_pn.template to<uint64_t>();
  r_time_[s] = time_;
  r_off_[s] = static_cast<uint8_t>(anchor_off);
  int len = static_cast<int>(active_in_.size());
  if (len > nin_)
    len = nin_;
  r_len_[s] = static_cast<uint16_t>(len);
  int32_t* dst = &r_ain_[s * nin_];
  for (int t = 0; t < len; ++t)
    dst[t] = active_in_[t];
  uint64_t* conf = &r_conf_[s * nmask_];
  for (int w = 0; w < nmask_; ++w)
    conf[w] = 0;
  ++r_count_;

  // (5) mark this access in the map (AFTER building the anchor input above).
  mark_access(addr);
  return metadata_in;
}

uint32_t funnel::prefetcher_cache_fill(champsim::address /*addr*/, long /*set*/, long /*way*/, bool /*prefetch*/,
                                       champsim::address /*evicted_addr*/, uint32_t metadata_in)
{
  return metadata_in;
}
