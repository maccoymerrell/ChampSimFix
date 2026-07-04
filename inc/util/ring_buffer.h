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

#ifndef UTIL_RING_BUFFER_H
#define UTIL_RING_BUFFER_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace champsim
{
/**
 * A bounded FIFO with contiguous storage and random-access iterators.
 *
 * This is a general-purpose queue container, replacing std::deque wherever a
 * structure has a known (or amortizable) capacity: push_back/emplace_back at
 * the tail, pop_front at the head, and index/iterate in insertion order —
 * but over a single contiguous allocation, so iteration costs one add and a
 * wrap test per step instead of the deque's chunk-map indirection.
 *
 * It supports the standard container calls its backing store allows:
 * begin/end/cbegin/cend, size/empty, front/back, operator[]/at,
 * push_back/emplace_back, pop_front/pop_back, clear, reserve, and the
 * end-anchored subset of insert/erase (see those members). Mutation in the
 * middle of the sequence is not part of the container's contract — elements
 * leave in FIFO (or LIFO, via pop_back) order only.
 *
 * Capacity semantics: set_capacity() fixes the capacity while empty;
 * push_back on a full buffer is a precondition violation (assert), so
 * admission must be gated by full()/size() — the simulator's structure sizes
 * are model parameters and overflow is a modeling bug. For queues with no
 * modeled bound, the _grow insertion calls enlarge the backing store
 * (amortized doubling) instead of failing.
 *
 * Iterator and reference invalidation:
 *  - push_back/emplace_back/insert: no iterator or reference is invalidated
 *    (the backing store never reallocates); past-the-end iterators become
 *    dereferenceable as usual.
 *  - pop_front/pop_back/erase/clear: references to surviving elements remain
 *    valid, but ALL iterators are invalidated — an iterator carries its
 *    logical index from the front, which shifts when the head moves.
 *  - reserve and the _grow calls (when they enlarge the store): all
 *    iterators, references, and physical slot indices are invalidated.
 * In short: a reference or pointer to an element is stable for the element's
 * entire residency provided the backing store is never enlarged; iterators
 * are stable only across tail insertion.
 */
template <typename T>
class ring_buffer
{
  // Slots are optionals so T need not be default-constructible; a live slot
  // is always engaged, so element access is unchecked.
  std::vector<std::optional<T>> storage_{};
  std::size_t head_ = 0; // physical index of the logical front
  std::size_t count_ = 0;

  std::size_t physical(std::size_t logical) const
  {
    std::size_t idx = head_ + logical;
    if (idx >= storage_.size()) {
      idx -= storage_.size();
    }
    return idx;
  }

  template <bool Const>
  class iterator_impl
  {
    using slot_type = std::conditional_t<Const, const std::optional<T>, std::optional<T>>;
    // The data pointer, capacity, and physical index are cached in the
    // iterator so dereferencing is a single indexed load with no indirection
    // through the owning container (these walks are the hottest loops in the
    // simulator). The logical index orders iterators and measures distance.
    slot_type* data_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t phys_ = 0; // physical slot of the current element
    std::size_t pos_ = 0;  // logical index from the front

    friend class ring_buffer;
    iterator_impl(slot_type* data, std::size_t cap, std::size_t head, std::size_t pos) : data_(data), cap_(cap), phys_(head + pos), pos_(pos)
    {
      if (cap_ != 0 && phys_ >= cap_) {
        phys_ -= cap_;
      }
    }

    void advance(std::ptrdiff_t n)
    {
      pos_ = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(pos_) + n);
      auto p = static_cast<std::ptrdiff_t>(phys_) + n;
      if (p >= static_cast<std::ptrdiff_t>(cap_)) {
        p -= static_cast<std::ptrdiff_t>(cap_);
      } else if (p < 0) {
        p += static_cast<std::ptrdiff_t>(cap_);
      }
      phys_ = static_cast<std::size_t>(p);
    }

  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<Const, const T*, T*>;
    using reference = std::conditional_t<Const, const T&, T&>;

    iterator_impl() = default;
    // A mutable iterator converts to a const iterator, as with standard containers.
    operator iterator_impl<true>() const // NOLINT(google-explicit-constructor)
    {
      iterator_impl<true> retval{};
      retval.data_ = data_;
      retval.cap_ = cap_;
      retval.phys_ = phys_;
      retval.pos_ = pos_;
      return retval;
    }
    friend class iterator_impl<true>;

    reference operator*() const { return *data_[phys_]; }
    pointer operator->() const { return &**this; }
    /** The physical storage slot of the current element. Slots are stable
     *  for an element's whole residency (only reused after it is popped),
     *  so they can key side structures such as candidate bitmaps. */
    std::size_t slot() const { return phys_; }
    reference operator[](difference_type n) const { return *(*this + n); }

    iterator_impl& operator++()
    {
      ++pos_;
      ++phys_;
      if (phys_ == cap_) {
        phys_ = 0;
      }
      return *this;
    }
    iterator_impl operator++(int) { auto tmp = *this; ++*this; return tmp; }
    iterator_impl& operator--()
    {
      --pos_;
      phys_ = (phys_ == 0 ? cap_ : phys_) - 1;
      return *this;
    }
    iterator_impl operator--(int) { auto tmp = *this; --*this; return tmp; }
    iterator_impl& operator+=(difference_type n) { advance(n); return *this; }
    iterator_impl& operator-=(difference_type n) { advance(-n); return *this; }

    friend iterator_impl operator+(iterator_impl it, difference_type n) { return it += n; }
    friend iterator_impl operator+(difference_type n, iterator_impl it) { return it += n; }
    friend iterator_impl operator-(iterator_impl it, difference_type n) { return it -= n; }
    friend difference_type operator-(const iterator_impl& lhs, const iterator_impl& rhs)
    {
      return static_cast<difference_type>(lhs.pos_) - static_cast<difference_type>(rhs.pos_);
    }

    friend bool operator==(const iterator_impl& lhs, const iterator_impl& rhs) { return lhs.pos_ == rhs.pos_; }
    friend bool operator!=(const iterator_impl& lhs, const iterator_impl& rhs) { return !(lhs == rhs); }
    friend bool operator<(const iterator_impl& lhs, const iterator_impl& rhs) { return lhs.pos_ < rhs.pos_; }
    friend bool operator>(const iterator_impl& lhs, const iterator_impl& rhs) { return rhs < lhs; }
    friend bool operator<=(const iterator_impl& lhs, const iterator_impl& rhs) { return !(rhs < lhs); }
    friend bool operator>=(const iterator_impl& lhs, const iterator_impl& rhs) { return !(lhs < rhs); }
  };

public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = iterator_impl<false>;
  using const_iterator = iterator_impl<true>;

  ring_buffer() = default;
  explicit ring_buffer(size_type capacity) { set_capacity(capacity); }

  /**
   * Fix the capacity. May only be called while the buffer is empty; existing
   * contents (there are none) are discarded.
   */
  void set_capacity(size_type capacity)
  {
    assert(count_ == 0);
    storage_ = std::vector<std::optional<T>>(capacity);
    head_ = 0;
    count_ = 0;
  }

  /**
   * Enlarge the backing store to hold at least new_capacity elements,
   * preserving the contents in order. No-op if the buffer is already at
   * least that large. Unlike set_capacity(), this may be called while
   * elements are present; when it enlarges the store it invalidates all
   * iterators, references, and physical slot indices.
   */
  void reserve(size_type new_capacity)
  {
    if (new_capacity <= storage_.size()) {
      return;
    }
    std::vector<std::optional<T>> new_storage(new_capacity);
    for (size_type idx = 0; idx < count_; ++idx) {
      new_storage[idx] = std::move(storage_[physical(idx)]);
    }
    storage_ = std::move(new_storage);
    head_ = 0;
  }

  size_type capacity() const { return storage_.size(); }
  size_type size() const { return count_; }
  [[nodiscard]] bool empty() const { return count_ == 0; }
  [[nodiscard]] bool full() const { return count_ == storage_.size(); }

  reference operator[](size_type idx) { return *storage_[physical(idx)]; }
  const_reference operator[](size_type idx) const { return *storage_[physical(idx)]; }
  reference at(size_type idx)
  {
    if (idx >= count_) {
      throw std::out_of_range{"champsim::ring_buffer::at"};
    }
    return (*this)[idx];
  }
  const_reference at(size_type idx) const
  {
    if (idx >= count_) {
      throw std::out_of_range{"champsim::ring_buffer::at"};
    }
    return (*this)[idx];
  }

  reference front() { return (*this)[0]; }
  const_reference front() const { return (*this)[0]; }
  reference back() { return (*this)[count_ - 1]; }
  const_reference back() const { return (*this)[count_ - 1]; }

  iterator begin() { return {storage_.data(), storage_.size(), head_, 0}; }
  iterator end() { return {storage_.data(), storage_.size(), head_, count_}; }
  const_iterator begin() const { return {storage_.data(), storage_.size(), head_, 0}; }
  const_iterator end() const { return {storage_.data(), storage_.size(), head_, count_}; }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  void push_back(const T& value)
  {
    assert(!full());
    storage_[physical(count_)] = value;
    ++count_;
  }
  void push_back(T&& value)
  {
    assert(!full());
    storage_[physical(count_)] = std::move(value);
    ++count_;
  }
  template <typename... Args>
  reference emplace_back(Args&&... args)
  {
    assert(!full());
    auto& slot = storage_[physical(count_)];
    slot.emplace(std::forward<Args>(args)...);
    ++count_;
    return *slot;
  }

  /**
   * Tail insertion for queues without a modeled capacity bound: as
   * push_back/emplace_back, but a full buffer enlarges the backing store
   * (amortized doubling) instead of asserting. When growth occurs, all
   * iterators, references, and physical slot indices are invalidated.
   */
  void push_back_grow(const T& value)
  {
    grow_if_full();
    push_back(value);
  }
  void push_back_grow(T&& value)
  {
    grow_if_full();
    push_back(std::move(value));
  }
  template <typename... Args>
  reference emplace_back_grow(Args&&... args)
  {
    grow_if_full();
    return emplace_back(std::forward<Args>(args)...);
  }

  void pop_front()
  {
    assert(count_ > 0);
    storage_[head_].reset(); // release the element's resources promptly
    ++head_;
    if (head_ >= storage_.size()) {
      head_ = 0;
    }
    --count_;
  }

  void pop_back()
  {
    assert(count_ > 0);
    --count_;
    storage_[physical(count_)].reset();
  }

  /** The physical slot of the logical front. */
  size_type head_slot() const { return head_; }
  /** The physical slot holding logical index idx. */
  size_type slot_index(size_type idx) const { return physical(idx); }
  /** Access an element by its physical slot (must be occupied). */
  reference at_slot(size_type slot) { return *storage_[slot]; }
  const_reference at_slot(size_type slot) const { return *storage_[slot]; }

  /**
   * Insert a range at the tail. Only end-anchored insertion is supported,
   * mirroring push_back (the position argument exists for interface
   * familiarity and must equal end()).
   */
  template <typename InputIt>
  iterator insert(const_iterator pos, InputIt first, InputIt last)
  {
    assert(pos.pos_ == count_);
    (void)pos;
    for (; first != last; ++first) {
      push_back(*first);
    }
    return end();
  }

  /**
   * Erase a range that touches one end of the buffer: front-anchored
   * (`first == begin()`, FIFO retirement) or tail-anchored (`last == end()`,
   * compaction-style removal as used with extract_if/remove_if).
   */
  iterator erase(const_iterator first, const_iterator last)
  {
    assert(first.pos_ == 0 || last.pos_ == count_);
    auto n = last - first;
    if (first.pos_ == 0) {
      for (difference_type i = 0; i < n; ++i) {
        pop_front();
      }
      return begin();
    }
    auto retval_pos = first.pos_;
    for (difference_type i = 0; i < n; ++i) {
      pop_back();
    }
    return begin() + static_cast<difference_type>(retval_pos);
  }

  void clear()
  {
    for (size_type idx = 0; idx < count_; ++idx) {
      storage_[physical(idx)].reset(); // release the elements' resources promptly
    }
    head_ = 0;
    count_ = 0;
  }

private:
  void grow_if_full()
  {
    if (full()) {
      reserve(std::max<size_type>(2 * storage_.size(), 8));
    }
  }
};
} // namespace champsim

#endif
