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

#ifndef UTIL_BOUNDED_ARRAY_H
#define UTIL_BOUNDED_ARRAY_H

#include <array>
#include <cassert>
#include <cstddef>

namespace champsim
{
/**
 * A fixed-capacity inline sequence with a vector-ish interface.
 *
 * For collections whose maximum size is a format constant (an instruction's
 * register and memory operands are bounded by the trace record layout), a
 * heap vector buys three pointers of overhead, an allocation, and a pointer
 * chase per access. This stores the elements inline — protocol-struct
 * style — so the enclosing object is one contiguous, trivially copyable
 * block and construction from a decoded record allocates nothing.
 */
template <typename T, std::size_t N>
class bounded_array
{
  std::array<T, N> data_{};
  std::size_t count_ = 0;

public:
  using value_type = T;
  using iterator = T*;
  using const_iterator = const T*;

  constexpr bounded_array() = default;

  constexpr void push_back(const T& value)
  {
    assert(count_ < N);
    data_[count_++] = value;
  }

  constexpr std::size_t size() const { return count_; }
  [[nodiscard]] constexpr bool empty() const { return count_ == 0; }
  static constexpr std::size_t capacity() { return N; }

  constexpr T& operator[](std::size_t idx) { return data_[idx]; }
  constexpr const T& operator[](std::size_t idx) const { return data_[idx]; }
  constexpr T& at(std::size_t idx)
  {
    assert(idx < count_);
    return data_[idx];
  }
  constexpr const T& at(std::size_t idx) const
  {
    assert(idx < count_);
    return data_[idx];
  }
  constexpr T& back() { return data_[count_ - 1]; }
  constexpr const T& back() const { return data_[count_ - 1]; }

  constexpr iterator begin() { return data_.data(); }
  constexpr iterator end() { return data_.data() + count_; }
  constexpr const_iterator begin() const { return data_.data(); }
  constexpr const_iterator end() const { return data_.data() + count_; }
  constexpr const_iterator cbegin() const { return begin(); }
  constexpr const_iterator cend() const { return end(); }

  constexpr void clear() { count_ = 0; }

  /** Erase a tail range [first, end()) — the erase-remove idiom's shape. */
  constexpr iterator erase(iterator first, iterator last)
  {
    assert(last == end());
    (void)last;
    count_ = static_cast<std::size_t>(first - begin());
    return end();
  }

  friend constexpr bool operator==(const bounded_array& lhs, const bounded_array& rhs)
  {
    if (lhs.count_ != rhs.count_) {
      return false;
    }
    for (std::size_t i = 0; i < lhs.count_; ++i) {
      if (!(lhs.data_[i] == rhs.data_[i])) {
        return false;
      }
    }
    return true;
  }
};
} // namespace champsim

#endif
