#include <catch.hpp>
#include <vector>

#include "bandwidth.h"
#include "chrono.h"
#include "util/latency_queue.h"
#include "waitable.h"

namespace
{
using time_point = champsim::chrono::clock::time_point;

// A time_point `ticks` picoseconds after the clock epoch.
time_point at(long ticks) { return time_point{} + champsim::chrono::clock::duration{ticks}; }

// The simplest possible latency_queue entry: an explicit ready-time plus a
// payload the tests can identify individual entries by.
struct entry {
  time_point rt{};
  int id{};
};
struct entry_ready {
  time_point operator()(const entry& e) const { return e.rt; }
};
using queue_type = champsim::latency_queue<entry, entry_ready>;

queue_type make_queue(const std::vector<entry>& entries)
{
  queue_type q{};
  q.set_capacity(entries.size() + 4);
  for (const auto& e : entries) {
    q.push_back(e);
  }
  return q;
}

std::vector<int> ids(const queue_type& q)
{
  std::vector<int> out;
  for (const auto& e : q) {
    out.push_back(e.id);
  }
  return out;
}
} // namespace

TEST_CASE("An empty latency_queue reports no ready work")
{
  queue_type q{};
  q.set_capacity(4);

  REQUIRE(q.empty());
  REQUIRE(q.next_ready_time() == time_point::max());
  REQUIRE_FALSE(q.has_ready(at(0)));
  REQUIRE_FALSE(q.has_ready(time_point::max()));

  champsim::bandwidth bw{champsim::bandwidth::maximum_type{4}};
  auto drained = q.drain_ready(at(100), bw, [](const entry&) { return true; });

  REQUIRE(drained == 0);
  REQUIRE(bw.amount_consumed() == 0);
  REQUIRE(q.empty());
}

TEST_CASE("next_ready_time and has_ready read the front entry with an inclusive boundary")
{
  auto q = make_queue({{at(10), 0}, {at(20), 1}, {at(30), 2}});

  REQUIRE(q.next_ready_time() == at(10));

  REQUIRE_FALSE(q.has_ready(at(9)));
  REQUIRE(q.has_ready(at(10))); // boundary is <=, so the front is ready AT its time
  REQUIRE(q.has_ready(at(11)));
  REQUIRE(q.has_ready(at(1000)));
}

TEST_CASE("drain_ready with ample bandwidth retires the whole ready prefix")
{
  auto q = make_queue({{at(10), 0}, {at(20), 1}, {at(30), 2}});
  champsim::bandwidth bw{champsim::bandwidth::maximum_type{16}};

  SECTION("all entries ready")
  {
    auto drained = q.drain_ready(at(100), bw, [](const entry&) { return true; });
    REQUIRE(drained == 3);
    REQUIRE(bw.amount_consumed() == 3);
    REQUIRE(q.empty());
  }

  SECTION("only the front prefix within the ready boundary is retired")
  {
    // now == 20: entries at 10 and 20 are ready (<=20), the entry at 30 is not.
    auto drained = q.drain_ready(at(20), bw, [](const entry&) { return true; });
    REQUIRE(drained == 2);
    REQUIRE(bw.amount_consumed() == 2);
    REQUIRE(ids(q) == std::vector<int>{2}); // the not-yet-ready entry survives, in place
  }
}

TEST_CASE("drain_ready never retires past the ready boundary even with later-ready-looking entries")
{
  // The readiness boundary is the FIRST non-ready entry (a front prefix), not a
  // filter: an entry after a non-ready one is never reached this cycle (matches
  // the open-coded get_span_p at the call sites). Callers keep ready-times
  // nondecreasing; here we violate that to pin the boundary.
  auto q = make_queue({{at(10), 0}, {at(50), 1}, {at(10), 2}});
  champsim::bandwidth bw{champsim::bandwidth::maximum_type{16}};

  auto drained = q.drain_ready(at(10), bw, [](const entry&) { return true; });

  REQUIRE(drained == 1); // stops at the entry timed 50, does not reach the trailing entry timed 10
  REQUIRE(ids(q) == std::vector<int>{1, 2});
}

TEST_CASE("drain_ready caps the retired prefix at the bandwidth")
{
  auto q = make_queue({{at(10), 0}, {at(10), 1}, {at(10), 2}, {at(10), 3}, {at(10), 4}});

  std::vector<int> visited;
  champsim::bandwidth bw{champsim::bandwidth::maximum_type{2}};
  auto drained = q.drain_ready(at(10), bw, [&](const entry& e) {
    visited.push_back(e.id);
    return true;
  });

  REQUIRE(drained == 2);
  REQUIRE(bw.amount_consumed() == 2);
  REQUIRE_FALSE(bw.has_remaining());
  REQUIRE(visited == std::vector<int>{0, 1}); // process is only offered the bandwidth-limited span
  REQUIRE(ids(q) == std::vector<int>{2, 3, 4});
}

TEST_CASE("drain_ready stops before the first entry the handler declines (find_if_not)")
{
  auto q = make_queue({{at(10), 0}, {at(10), 1}, {at(10), 2}, {at(10), 3}, {at(10), 4}});

  std::vector<int> visited;
  champsim::bandwidth bw{champsim::bandwidth::maximum_type{16}};
  auto drained = q.drain_ready(at(10), bw, [&](const entry& e) {
    visited.push_back(e.id);
    return e.id < 2; // decline at id 2
  });

  REQUIRE(drained == 2);
  REQUIRE(bw.amount_consumed() == 2);
  // The declining entry is evaluated (that is how the stop is detected) but not
  // retired, and no entry past it is offered.
  REQUIRE(visited == std::vector<int>{0, 1, 2});
  REQUIRE(ids(q) == std::vector<int>{2, 3, 4});
}

TEST_CASE("drain_ready threads one bandwidth budget across successive drains")
{
  // Mirrors the page-walker, which drains `completed` and then `finished`
  // against a single shared fill bandwidth.
  auto completed = make_queue({{at(10), 0}, {at(10), 1}});
  auto finished = make_queue({{at(10), 2}, {at(10), 3}, {at(10), 4}});

  champsim::bandwidth bw{champsim::bandwidth::maximum_type{3}};
  auto a = completed.drain_ready(at(10), bw, [](const entry&) { return true; });
  auto b = finished.drain_ready(at(10), bw, [](const entry&) { return true; });

  REQUIRE(a == 2);
  REQUIRE(b == 1); // only one unit of bandwidth remained for `finished`
  REQUIRE(bw.amount_consumed() == 3);
  REQUIRE(completed.empty());
  REQUIRE(ids(finished) == std::vector<int>{3, 4});
}

TEST_CASE("waitable_ready_time projects a waitable member's event time and matches is_ready_at")
{
  struct wentry {
    champsim::waitable<int> promise{};
    int id{};
  };
  using wqueue = champsim::latency_queue<wentry, champsim::waitable_ready_time<&wentry::promise>>;

  wqueue q{};
  q.set_capacity(4);
  q.push_back(wentry{champsim::waitable<int>{7, at(20)}, 0}); // ready at 20
  q.push_back(wentry{champsim::waitable<int>{8, at(40)}, 1}); // ready at 40
  q.push_back(wentry{champsim::waitable<int>{9}, 2});         // no time set -> never ready

  REQUIRE(q.next_ready_time() == at(20));

  // has_ready must agree with the front waitable's own is_ready_at, bit for bit.
  for (long t : {0L, 19L, 20L, 21L, 40L, 1000L}) {
    REQUIRE(q.has_ready(at(t)) == q.front().promise.is_ready_at(at(t)));
  }

  champsim::bandwidth bw{champsim::bandwidth::maximum_type{16}};
  auto drained = q.drain_ready(at(30), bw, [](const wentry&) { return true; });

  REQUIRE(drained == 1); // only the entry ready by 30; the entry at 40 and the timeless entry remain
  REQUIRE(q.size() == 2);
  REQUIRE(q.front().id == 1);
}

TEST_CASE("latency_queue re-exports the ring_buffer surface")
{
  queue_type q{4};
  REQUIRE(q.capacity() == 4);
  REQUIRE(q.empty());

  q.push_back(entry{at(10), 0});
  q.emplace_back(entry{at(20), 1});
  REQUIRE(q.size() == 2);
  REQUIRE(q.front().id == 0);
  REQUIRE(q.back().id == 1);
  REQUIRE(q[1].id == 1);

  q.push_back_grow(entry{at(30), 2});
  q.push_back_grow(entry{at(40), 3});
  q.push_back_grow(entry{at(50), 4}); // grows past the seeded capacity of 4
  REQUIRE(q.size() == 5);
  REQUIRE(q.capacity() >= 5);

  q.pop_front();
  REQUIRE(q.front().id == 1);

  q.clear();
  REQUIRE(q.empty());
}
