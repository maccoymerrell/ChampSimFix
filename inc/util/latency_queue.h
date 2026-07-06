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

#ifndef UTIL_LATENCY_QUEUE_H
#define UTIL_LATENCY_QUEUE_H

#include <algorithm>
#include <iterator>
#include <utility>

#include "bandwidth.h"
#include "chrono.h"
#include "util/ring_buffer.h"
#include "util/span.h"

namespace champsim
{
/**
 * Ready-time projection for entries whose readiness is a champsim::waitable
 * member (the common case). Bind it with a pointer-to-member constant:
 *
 *   champsim::latency_queue<fill_type, champsim::waitable_ready_time<&fill_type::data_promise>>
 *
 * Ready-time is the waitable's event time (time_point::max() when unset), so
 * `waitable_ready_time{}(entry) <= now` is byte-identical to
 * `(entry.*WaitableMember).is_ready_at(now)`.
 */
template <auto WaitableMember>
struct waitable_ready_time {
  template <typename Entry>
  champsim::chrono::clock::time_point operator()(const Entry& entry) const
  {
    return (entry.*WaitableMember).ready_time();
  }
};

/**
 * Ready-time projection for entries whose readiness is a plain time_point
 * member (not a champsim::waitable). Bind it with a pointer-to-member constant:
 *
 *   champsim::latency_queue<tag_lookup_type, champsim::member_ready_time<&tag_lookup_type::event_cycle>>
 *
 * Ready-time is that member's value directly; entries default it to
 * time_point::max() ("not yet scheduled") so an unstamped entry is never ready.
 */
template <auto TimeMember>
struct member_ready_time {
  template <typename Entry>
  champsim::chrono::clock::time_point operator()(const Entry& entry) const
  {
    return entry.*TimeMember;
  }
};

/**
 * A time-ordered ready queue: a bounded FIFO of entries that each carry a
 * ready-time, kept in nondecreasing ready-time order, drained from the front
 * under a bandwidth cap. Names once (in drain_ready) the get_span_p +
 * find_if_not + bandwidth::consume + erase sequence that recurred across cache
 * fills, page-walk completions, etc.
 *
 * Backed by a champsim::ring_buffer, whose iterator/reference invalidation
 * contract it inherits (tail insertion invalidates nothing; front retirement
 * invalidates all iterators but keeps references to survivors valid). The
 * ring-buffer surface is re-exported so a member can migrate from ring_buffer
 * to latency_queue with no other call-site changes.
 *
 * Ready-time contract: an entry is ready at now iff ReadyTime{}(entry) <= now
 * (inclusive, matching waitable::is_ready_at). Callers must enqueue with
 * nondecreasing ready-times so the ready entries form a front prefix; the queue
 * does not enforce this. next_ready_time()/has_ready() read only the front, O(1).
 */
template <typename T, typename ReadyTime>
class latency_queue
{
  ring_buffer<T> buf_{};

public:
  using value_type = typename ring_buffer<T>::value_type;
  using size_type = typename ring_buffer<T>::size_type;
  using difference_type = typename ring_buffer<T>::difference_type;
  using reference = typename ring_buffer<T>::reference;
  using const_reference = typename ring_buffer<T>::const_reference;
  using iterator = typename ring_buffer<T>::iterator;
  using const_iterator = typename ring_buffer<T>::const_iterator;

  latency_queue() = default;
  explicit latency_queue(size_type capacity) : buf_(capacity) {}

  /** Ready-time of the front entry, or time_point::max() when empty. O(1). The
   *  next-event hook for idle-skip; not consulted by drain_ready(). */
  champsim::chrono::clock::time_point next_ready_time() const
  {
    return empty() ? champsim::chrono::clock::time_point::max() : ReadyTime{}(buf_.front());
  }

  /** Whether the front entry is ready at now (front ready-time <= now). O(1).
   *  False when empty. */
  bool has_ready(champsim::chrono::clock::time_point now) const { return !empty() && ReadyTime{}(buf_.front()) <= now; }

  /**
   * Retire the ready front prefix, up to the bandwidth, handing each entry to
   * process (a bool-returning handler): take the prefix within bw and ready at
   * now, walk it with find_if_not stopping before the first entry process
   * declines, then consume that many bw units and erase them from the front.
   *
   * process runs on const entries front-to-back, INCLUDING the declining entry
   * (find_if_not evaluates it to detect the stop), so a mutating handler must
   * tolerate running on the entry that stops the drain. Returns the number
   * retired (== bw consumed). Erasure invalidates all iterators.
   */
  template <typename Fn>
  long drain_ready(champsim::chrono::clock::time_point now, champsim::bandwidth& bw, Fn&& process)
  {
    auto is_ready = [now](const value_type& entry) { return ReadyTime{}(entry) <= now; };
    auto [ready_begin, ready_end] = champsim::get_span_p(buf_.cbegin(), buf_.cend(), bw, is_ready);
    auto drain_end = std::find_if_not(ready_begin, ready_end, std::forward<Fn>(process));
    auto drained = std::distance(ready_begin, drain_end);
    bw.consume(drained);
    buf_.erase(ready_begin, drain_end);
    return drained;
  }

  // --- champsim::ring_buffer surface pass-throughs ---------------------------
  // Forward verbatim to the backing ring_buffer; see ring_buffer.h for contracts.

  void set_capacity(size_type capacity) { buf_.set_capacity(capacity); }
  void reserve(size_type new_capacity) { buf_.reserve(new_capacity); }

  size_type capacity() const { return buf_.capacity(); }
  size_type size() const { return buf_.size(); }
  [[nodiscard]] bool empty() const { return buf_.empty(); }
  [[nodiscard]] bool full() const { return buf_.full(); }

  reference operator[](size_type idx) { return buf_[idx]; }
  const_reference operator[](size_type idx) const { return buf_[idx]; }
  reference at(size_type idx) { return buf_.at(idx); }
  const_reference at(size_type idx) const { return buf_.at(idx); }

  reference front() { return buf_.front(); }
  const_reference front() const { return buf_.front(); }
  reference back() { return buf_.back(); }
  const_reference back() const { return buf_.back(); }

  iterator begin() { return buf_.begin(); }
  iterator end() { return buf_.end(); }
  const_iterator begin() const { return buf_.begin(); }
  const_iterator end() const { return buf_.end(); }
  const_iterator cbegin() const { return buf_.cbegin(); }
  const_iterator cend() const { return buf_.cend(); }

  void push_back(const T& value) { buf_.push_back(value); }
  void push_back(T&& value) { buf_.push_back(std::move(value)); }
  template <typename... Args>
  reference emplace_back(Args&&... args)
  {
    return buf_.emplace_back(std::forward<Args>(args)...);
  }

  void push_back_grow(const T& value) { buf_.push_back_grow(value); }
  void push_back_grow(T&& value) { buf_.push_back_grow(std::move(value)); }
  template <typename... Args>
  reference emplace_back_grow(Args&&... args)
  {
    return buf_.emplace_back_grow(std::forward<Args>(args)...);
  }

  void pop_front() { buf_.pop_front(); }
  void pop_back() { buf_.pop_back(); }
  iterator erase(const_iterator first, const_iterator last) { return buf_.erase(first, last); }
  void clear() { buf_.clear(); }
};
} // namespace champsim

#endif
