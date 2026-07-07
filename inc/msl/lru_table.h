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

#ifndef MSL_LRU_TABLE_H
#define MSL_LRU_TABLE_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "extent.h"
#include "msl/bits.h"
#include "util/detect.h"
#include "util/span.h"
#include "util/type_traits.h"

namespace champsim
{
template <typename Extent>
class address_slice;
}

namespace champsim::msl
{
namespace detail
{
template <typename T>
struct table_indexer {
  auto operator()(const T& t) const { return t.index(); }
};

template <typename T>
struct table_tagger {
  auto operator()(const T& t) const { return t.tag(); }
};

template <class T, class U>
constexpr bool cmp_equal(T t, U u) noexcept
{
  using UT = std::make_unsigned_t<T>;
  using UU = std::make_unsigned_t<U>;
  if constexpr (std::is_signed_v<T> == std::is_signed_v<U>)
    return t == u;
  else if constexpr (std::is_signed_v<T>)
    return t < 0 ? false : UT(t) == u;
  else
    return u < 0 ? false : t == UU(u);
}
} // namespace detail

/**
 * A set-associative table with LRU eviction.
 *
 * Elements are placed into sets using a set projection function object and matched
 * within a set using a tag projection function object. By default, these call
 * ``.index()`` and ``.tag()`` member functions on the element type. The number of
 * sets must be a power of 2.
 *
 * This is useful for building hardware tables such as BTB entries, stride tracker
 * tables, and signature tables. See ``ip_stride`` and ``basic_btb`` for examples.
 *
 * \tparam T The element type stored in the table.
 * \tparam SetProj A function object that projects an element to a set index. Defaults to calling ``T::index()``.
 * \tparam TagProj A function object that projects an element to a tag for matching. Defaults to calling ``T::tag()``.
 */
template <typename T, typename SetProj = detail::table_indexer<T>, typename TagProj = detail::table_tagger<T>>
class lru_table
{
public:
  using value_type = T;

private:
  struct block_t {
    uint64_t last_used = 0;
    value_type data;
  };
  using block_vec_type = std::vector<block_t>;
  using diff_type = typename block_vec_type::difference_type;

  SetProj set_projection;
  TagProj tag_projection;

  diff_type NUM_SET;
  diff_type NUM_WAY;
  uint64_t access_count = 0;
  block_vec_type block;

  auto get_set_span(const value_type& elem)
  {
    diff_type set_idx;
    if constexpr (champsim::is_specialization_v<std::invoke_result_t<SetProj, decltype(elem)>, champsim::address_slice>) {
      set_idx = set_projection(elem).template to<decltype(set_idx)>();
    } else {
      set_idx = static_cast<diff_type>(set_projection(elem));
    }
    if (set_idx < 0)
      throw std::range_error{"Set projection produced negative set index: " + std::to_string(set_idx)};
    diff_type raw_idx{(set_idx & (NUM_SET - 1)) * NUM_WAY}; // NUM_SET is a power of 2 (enforced at construction)
    auto begin = std::next(std::begin(block), raw_idx);
    auto end = std::next(begin, NUM_WAY);
    return std::pair{begin, end};
  }

  auto match_func(const value_type& elem)
  {
    return [tag = tag_projection(elem), proj = this->tag_projection](const block_t& x) {
      return x.last_used > 0 && proj(x.data) == tag;
    };
  }

  template <typename U>
  auto match_and_check(U tag)
  {
    return [tag, proj = this->tag_projection](const auto& x, const auto& y) {
      auto x_valid = x.last_used > 0;
      auto y_valid = y.last_used > 0;
      auto x_match = proj(x.data) == tag;
      auto y_match = proj(y.data) == tag;
      auto cmp_lru = x.last_used < y.last_used;
      return !x_valid || (y_valid && ((!x_match && y_match) || ((x_match == y_match) && cmp_lru)));
    };
  }

public:
  /**
   * Look up an element in the table by set and tag.
   *
   * If a matching element is found, its LRU timestamp is updated and the
   * element is returned. Otherwise returns ``std::nullopt``.
   *
   * \param elem An element whose set and tag projections identify the lookup.
   * \return The matching element, or ``std::nullopt`` on a miss.
   */
  std::optional<value_type> check_hit(const value_type& elem)
  {
    auto [set_begin, set_end] = get_set_span(elem);
    auto hit = std::find_if(set_begin, set_end, match_func(elem));

    if (hit == set_end) {
      return std::nullopt;
    }

    hit->last_used = ++access_count;
    return hit->data;
  }

  /**
   * Insert or update an element in the table.
   *
   * If an element with a matching tag already exists in the set, it is updated
   * in place. Otherwise the LRU element in the set is evicted and replaced.
   *
   * \param elem The element to insert.
   */
  void fill(const value_type& elem)
  {
    auto tag = tag_projection(elem);
    auto [set_begin, set_end] = get_set_span(elem);
    if (set_begin != set_end) {
      auto [miss, hit] = std::minmax_element(set_begin, set_end, match_and_check(tag));

      if (tag_projection(hit->data) == tag) {
        *hit = {++access_count, elem};
      } else {
        *miss = {++access_count, elem};
      }
    }
  }

  /**
   * Remove an element from the table.
   *
   * If a matching element is found, it is removed and returned.
   * Otherwise returns ``std::nullopt``.
   *
   * \param elem An element whose set and tag projections identify the entry to remove.
   * \return The removed element, or ``std::nullopt`` if not found.
   */
  std::optional<value_type> invalidate(const value_type& elem)
  {
    auto [set_begin, set_end] = get_set_span(elem);
    auto hit = std::find_if(set_begin, set_end, match_func(elem));

    if (hit == set_end) {
      return std::nullopt;
    }

    return std::exchange(*hit, {}).data;
  }

  /**
   * Construct an LRU table.
   *
   * \param sets The number of sets. Must be a positive power of 2.
   * \param ways The number of ways per set.
   * \param set_proj The function object used to compute the set index from an element.
   * \param tag_proj The function object used to compute the tag from an element.
   */
  lru_table(std::size_t sets, std::size_t ways, SetProj set_proj, TagProj tag_proj)
      : set_projection(set_proj), tag_projection(tag_proj), NUM_SET(static_cast<diff_type>(sets)), NUM_WAY(static_cast<diff_type>(ways)), block(sets * ways)
  {
    if (!detail::cmp_equal(sets, static_cast<diff_type>(sets)))
      throw std::overflow_error{"Sets is out of bounds"};
    if (!detail::cmp_equal(ways, static_cast<diff_type>(ways)))
      throw std::overflow_error{"Ways is out of bounds"};
    if (sets <= 0)
      throw std::range_error{"Sets is not positive"};
    if ((sets & (sets - 1)) != 0)
      throw std::range_error{"Sets is not a power of 2"};
  }

  lru_table(std::size_t sets, std::size_t ways, SetProj set_proj) : lru_table(sets, ways, set_proj, {}) {}
  lru_table(std::size_t sets, std::size_t ways) : lru_table(sets, ways, {}, {}) {}
};
} // namespace champsim::msl

#endif
