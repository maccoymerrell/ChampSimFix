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

#include <cassert>
#include <cstddef>
#include <iterator>
#include <optional>
#include <vector>

namespace champsim
{
/**
 * A fixed-capacity FIFO with contiguous storage and random-access iterators.
 *
 * This is a drop-in replacement for std::deque in the simulator's hot
 * structures (notably the ROB): push_back at the tail, erase at the front,
 * and index/iterate in insertion order — but over a single contiguous
 * allocation, so iteration costs one add and a wrap test per step instead of
 * the deque's chunk-map indirection.
 *
 * Capacity is fixed after set_capacity() (structure sizes in the simulator
 * are configuration constants). Erasure is only supported at the front, in
 * keeping with FIFO retirement. Popped slots keep their objects alive until
 * the slot is reused; element types must not rely on prompt destruction.
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

  size_type capacity() const { return storage_.size(); }
  size_type size() const { return count_; }
  [[nodiscard]] bool empty() const { return count_ == 0; }
  [[nodiscard]] bool full() const { return count_ == storage_.size(); }

  reference operator[](size_type idx) { return *storage_[physical(idx)]; }
  const_reference operator[](size_type idx) const { return *storage_[physical(idx)]; }
  reference at(size_type idx)
  {
    assert(idx < count_);
    return (*this)[idx];
  }
  const_reference at(size_type idx) const
  {
    assert(idx < count_);
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
   * Erase a prefix range [first, last). Only front-anchored ranges are
   * supported: `first` must be begin(). This matches FIFO retirement.
   */
  iterator erase(const_iterator first, const_iterator last)
  {
    assert(first.pos_ == 0);
    auto n = last - cbegin();
    for (difference_type i = 0; i < n; ++i) {
      pop_front();
    }
    return begin();
  }

  void clear()
  {
    head_ = 0;
    count_ = 0;
  }
};
} // namespace champsim

#endif
