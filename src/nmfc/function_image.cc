/*
 * FUNCTION_IMAGE_STORE — the default function_image model.
 *
 * A token-keyed map of bodies, plus the occupancy accounting that says what the
 * store would actually cost in hardware. Bodies are published at trace-read
 * time and retired on return, so occupancy tracks invocations that have been
 * *fetched* by the host, not just the ones in flight on a memory tile — the
 * host pipeline runs ahead of dispatch, so the high-water mark is the number
 * that matters when sizing the backing instruction space.
 *
 * Parameters:
 *   soft_capacity (optional) — bodies above which a one-shot warning is
 *     printed. Not a hard cap: dropping a body would be a correctness bug, not
 *     back-pressure, so the store grows and reports instead.
 */

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <fmt/core.h>

#include "modules.h"
#include "nmfc/function_image.h"
#include "stat_report.h"

namespace
{

struct function_image_store : public nmfc::function_image_module, public champsim::module_lifecycle {
  explicit function_image_store(champsim::modules::ModuleBuilder builder)
      : soft_capacity_(builder.get_parameter<std::size_t>("soft_capacity", true, std::size_t{4096}))
  {
  }

  void publish(nmfc::function_body body) override
  {
    const auto token = body.token;
    bodies_[token] = std::move(body);
    ++published_;
    if (bodies_.size() > high_water_) {
      high_water_ = bodies_.size();
    }
    if (!warned_ && bodies_.size() > soft_capacity_) {
      warned_ = true;
      fmt::print("[{}] WARNING: {} live function bodies exceeds soft_capacity {}. The backing instruction space this models would need to be larger.\n", NAME,
                 bodies_.size(), soft_capacity_);
    }
  }

  [[nodiscard]] const nmfc::function_body* lookup(std::uint64_t token) const override
  {
    auto it = bodies_.find(token);
    return it == std::end(bodies_) ? nullptr : &it->second;
  }

  void retire(std::uint64_t token) override
  {
    if (bodies_.erase(token) != 0) {
      ++retired_;
    }
  }

  [[nodiscard]] std::size_t occupancy() const override { return bodies_.size(); }

  void begin_phase(bool /*warmup*/) override
  {
    // Occupancy is a live quantity and deliberately survives the edge: bodies
    // published before the boundary are still owed an execution after it.
    published_ = 0;
    retired_ = 0;
    high_water_ = bodies_.size();
  }

  void end_phase(champsim::stat_report& out) override
  {
    if (published_ == 0 && bodies_.empty()) {
      return; // nothing happened here; publish no statistics
    }
    out.line(fmt::format("{} BODIES PUBLISHED: {} RETIRED: {} LIVE AT END: {} HIGH WATER: {}", NAME, published_, retired_, bodies_.size(), high_water_));
    auto json = out.json();
    json.add("published", published_);
    json.add("retired", retired_);
    json.add("live_at_end", bodies_.size());
    json.add("high_water", high_water_);
  }

private:
  std::unordered_map<std::uint64_t, nmfc::function_body> bodies_;
  std::size_t soft_capacity_;
  std::size_t high_water_ = 0;
  std::uint64_t published_ = 0;
  std::uint64_t retired_ = 0;
  bool warned_ = false;
};

static nmfc::function_image_module::register_interface function_image_iface_reg("function_image", "function images");
static nmfc::function_image_module::register_module<function_image_store> function_image_store_reg("FUNCTION_IMAGE_STORE");

} // anonymous namespace
