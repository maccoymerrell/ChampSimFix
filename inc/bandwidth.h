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

#ifndef BANDWIDTH_H
#define BANDWIDTH_H

#include <stdexcept>
#include <string>
#include <fmt/format.h>

#include "util/to_underlying.h"

namespace champsim
{
/**
 * This class encapuslates the operation of consuming a fixed number of operations, a very common operation in ChampSim.
 * Once initialized, the maximum bandwidth cannot be changed. Instead, consuming the bandwidth reduces the amount available
 * until it is depleted.
 */
class bandwidth
{
  using underlying_type = long int;
  enum class max_t : underlying_type {};

public:
  /**
   * The type of the maximum. This type is integer-like, with the exception that it is immutable.
   *
   * This type is exported so that other types can keep maximums as members.
   */
  using maximum_type = max_t;

private:
  underlying_type value_;
  maximum_type maximum_;

public:
  /**
   * Consume some of the bandwidth.
   *
   * \param delta The amount of bandwidth to consume
   *
   * \throws std::range_error if more than the maximum amount of bandwidth will have been consumed.
   *
   * These members are defined inline: they are one-instruction bodies called
   * hundreds of times per simulated cycle from every scan loop, and an
   * out-of-line definition costs a real function call per check.
   */
  void consume(underlying_type delta)
  {
    value_ -= delta;
    if (value_ < 0) {
      throw std::range_error{"Exceeded bandwidth of " + std::to_string(champsim::to_underlying(maximum_))};
    }
  }

  /**
   * Consume one unit of bandwidth.
   *
   * \throws std::range_error if more than the maximum amount of bandwidth will have been consumed.
   */
  void consume() { consume(1); }

  /**
   * Report if the bandwidth has one or more unit remaining.
   */
  bool has_remaining() const { return amount_remaining() > 0; }

  /**
   * Report the amount of bandwidth that has been consumed
   */
  underlying_type amount_consumed() const { return champsim::to_underlying(maximum_) - value_; }

  /**
   * Report the amount of bandwidth that remains
   */
  underlying_type amount_remaining() const { return value_; }

  /**
   * Reset the bandwidth, so that it can be used again.
   */
  void reset() { value_ = champsim::to_underlying(maximum_); }

  /**
   * Initialize a bandwidth with the specified maximum.
   */
  explicit bandwidth(maximum_type maximum) : value_(champsim::to_underlying(maximum)), maximum_(maximum) {}
};

inline auto format_as(champsim::bandwidth::maximum_type val) {
  return static_cast<long int>(val);
}

} // namespace champsim


#endif
