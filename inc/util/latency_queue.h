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
 * A ready-time projection for entries whose readiness is governed by a
 * champsim::waitable member (the common case in the simulator's latency
 * queues). Point it at the member with a pointer-to-member constant:
 *
 *   champsim::latency_queue<fill_type, champsim::waitable_ready_time<&fill_type::data_promise>>
 *
 * The entry's ready-time is the waitable's event time, or time_point::max()
 * while the waitable has no time set. This is defined so that
 *   waitable_ready_time{}(entry) <= now
 * is bit-for-bit the same predicate as
 *   (entry.*WaitableMember).is_ready_at(now)
 * (both reduce to event_cycle.value_or(time_sentinel) <= now); the drain sites
 * that used is_ready_at therefore keep byte-identical readiness after moving
 * onto this projection.
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
 * A time-ordered ready queue: a bounded FIFO of entries that each carry a
 * ready-time, kept in nondecreasing ready-time order, drained from the front
 * under a bandwidth cap.
 *
 * This consolidates the "time-ordered entries + ready-check + bandwidth-limited
 * drain" pattern that recurs across the simulator (cache fills, page-walk
 * completions, ...), where each cycle a structure retires the prefix of
 * front-most entries whose event time has arrived, up to a per-cycle bandwidth,
 * stopping early when a handler declines an entry. Before this primitive every
 * such site open-coded the same get_span_p + find_if_not + bandwidth::consume +
 * erase sequence; latency_queue names that sequence once, in drain_ready().
 *
 * The container is a champsim::ring_buffer, so it inherits that type's storage
 * and iterator/reference-invalidation contract (see ring_buffer.h): tail
 * insertion never invalidates references, and front retirement invalidates all
 * iterators but keeps references to surviving elements valid. The ring-buffer
 * surface (size/empty/front/push_back/.../begin/end) is re-exported so a member
 * migrated from ring_buffer to latency_queue needs no other call-site changes.
 *
 * Ready-time contract. The ready-time of an entry is ReadyTime{}(entry), a
 * stateless projection to champsim::chrono::clock::time_point (default-
 * constructed on demand; see waitable_ready_time for the waitable case). An
 * entry is "ready at now" exactly when
 *   ReadyTime{}(entry) <= now
 * — the boundary is inclusive (<=), matching waitable::is_ready_at and the
 * event_cycle <= now checks the drain sites used. Entries are expected to be
 * enqueued with nondecreasing ready-times (append at the tail as events are
 * scheduled), so the ready entries always form a front prefix; next_ready_time()
 * and has_ready() read only the front and are O(1). The queue does not enforce
 * the ordering — it is a modeling invariant of the callers, exactly as it was
 * when these queues were plain ring buffers.
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

  /**
   * The ready-time of the front entry, or time_point::max() when empty. O(1).
   *
   * Exposes the next event time so a caller can decide when the queue will next
   * have work — the hook a future event-driven idle-skip would read. It has no
   * effect on draining and is not consulted by drain_ready().
   */
  champsim::chrono::clock::time_point next_ready_time() const
  {
    return empty() ? champsim::chrono::clock::time_point::max() : ReadyTime{}(buf_.front());
  }

  /** Whether the front entry is ready at now (front ready-time <= now). O(1).
   *  False when empty. */
  bool has_ready(champsim::chrono::clock::time_point now) const { return !empty() && ReadyTime{}(buf_.front()) <= now; }

  /**
   * Retire the ready front prefix, up to the bandwidth, handing each entry to
   * process. This is the primitive's reason to exist: it reproduces exactly the
   * sequence the drain sites open-coded.
   *
   *  1. Take the front prefix that is both within bw and ready at now
   *     (ReadyTime{}(entry) <= now) — champsim::get_span_p with the readiness
   *     predicate. bw is read, not yet consumed.
   *  2. Walk that prefix with process, an entry handler returning bool, stopping
   *     before the first entry it declines — std::find_if_not. A handler with
   *     side effects that always returns true drains the whole ready prefix
   *     (the for_each-style sites); one that can return false stops early (the
   *     find_if_not-style sites).
   *  3. Consume that many units of bw and erase exactly those entries from the
   *     front.
   *
   * process is invoked on const entries in front-to-back order; it is called on
   * the declining entry too (find_if_not evaluates it to detect the stop), so a
   * handler that mutates must tolerate being run on the entry that stops the
   * drain — the same contract get_span_p imposed. Returns the number of entries
   * retired (== units of bw consumed). Erasure invalidates all iterators; see
   * ring_buffer's invalidation contract.
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
  // These forward verbatim to the backing ring_buffer so a member can migrate
  // from ring_buffer<T> to latency_queue<T, ...> without touching its other
  // call sites. See ring_buffer.h for each operation's contract.

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
