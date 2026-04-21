#ifndef SPPAM_B_DYNAMIC_BITSET_H
#define SPPAM_B_DYNAMIC_BITSET_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <string>

class dynamic_bitset {
  std::vector<uint64_t> blocks_;
  std::size_t nbits_ = 0;

  static constexpr std::size_t BITS_PER_BLOCK = 64;

  static std::size_t block_count(std::size_t nbits) {
    return (nbits + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
  }

  void sanitize() {
    if (nbits_ == 0) return;
    std::size_t excess = nbits_ % BITS_PER_BLOCK;
    if (excess > 0 && !blocks_.empty())
      blocks_.back() &= (1ULL << excess) - 1;
  }

public:
  dynamic_bitset() = default;

  explicit dynamic_bitset(std::size_t n)
    : blocks_(block_count(n), 0ULL), nbits_(n) {}

  dynamic_bitset(std::size_t n, uint64_t val)
    : blocks_(block_count(n), 0ULL), nbits_(n)
  {
    if (!blocks_.empty()) blocks_[0] = val;
    sanitize();
  }

  std::size_t size() const { return nbits_; }

  bool test(std::size_t pos) const {
    if (pos >= nbits_) return false;
    return (blocks_[pos / BITS_PER_BLOCK] >> (pos % BITS_PER_BLOCK)) & 1ULL;
  }

  void set(std::size_t pos, bool val = true) {
    if (pos >= nbits_) return;
    std::size_t block = pos / BITS_PER_BLOCK;
    std::size_t bit = pos % BITS_PER_BLOCK;
    if (val)
      blocks_[block] |= (1ULL << bit);
    else
      blocks_[block] &= ~(1ULL << bit);
  }

  dynamic_bitset& operator<<=(std::size_t n) {
    if (n == 0 || nbits_ == 0) return *this;
    if (n >= nbits_) {
      std::fill(blocks_.begin(), blocks_.end(), 0ULL);
      return *this;
    }
    std::size_t block_shift = n / BITS_PER_BLOCK;
    std::size_t bit_shift = n % BITS_PER_BLOCK;
    for (std::size_t i = blocks_.size(); i-- > 0;) {
      uint64_t lo = (i >= block_shift) ? blocks_[i - block_shift] : 0;
      uint64_t hi = (bit_shift > 0 && i > block_shift) ? blocks_[i - block_shift - 1] : 0;
      blocks_[i] = (bit_shift > 0)
        ? (lo << bit_shift) | (hi >> (BITS_PER_BLOCK - bit_shift))
        : lo;
    }
    sanitize();
    return *this;
  }

  dynamic_bitset& operator>>=(std::size_t n) {
    if (n == 0 || nbits_ == 0) return *this;
    if (n >= nbits_) {
      std::fill(blocks_.begin(), blocks_.end(), 0ULL);
      return *this;
    }
    std::size_t block_shift = n / BITS_PER_BLOCK;
    std::size_t bit_shift = n % BITS_PER_BLOCK;
    for (std::size_t i = 0; i < blocks_.size(); i++) {
      uint64_t lo = (i + block_shift < blocks_.size()) ? blocks_[i + block_shift] : 0;
      uint64_t hi = (bit_shift > 0 && i + block_shift + 1 < blocks_.size()) ? blocks_[i + block_shift + 1] : 0;
      blocks_[i] = (bit_shift > 0)
        ? (lo >> bit_shift) | (hi << (BITS_PER_BLOCK - bit_shift))
        : lo;
    }
    sanitize();
    return *this;
  }

  dynamic_bitset& operator^=(const dynamic_bitset& other) {
    std::size_t n = std::min(blocks_.size(), other.blocks_.size());
    for (std::size_t i = 0; i < n; i++)
      blocks_[i] ^= other.blocks_[i];
    sanitize();
    return *this;
  }

  dynamic_bitset operator^(const dynamic_bitset& other) const {
    dynamic_bitset result = *this;
    result ^= other;
    return result;
  }

  std::size_t num_blocks() const { return blocks_.size(); }

  uint64_t word(std::size_t i) const {
    return i < blocks_.size() ? blocks_[i] : 0ULL;
  }

  void set_word(std::size_t i, uint64_t val) {
    if (i < blocks_.size()) blocks_[i] = val;
  }

  // Extract up to 64 bits starting at bit position 'pos'
  uint64_t extract(std::size_t pos, std::size_t count) const {
    if (count == 0 || pos >= nbits_) return 0;
    std::size_t block_idx = pos / BITS_PER_BLOCK;
    int bit_offset = static_cast<int>(pos % BITS_PER_BLOCK);
    uint64_t w = blocks_[block_idx] >> bit_offset;
    if (bit_offset + static_cast<int>(count) > 64 && block_idx + 1 < blocks_.size())
      w |= blocks_[block_idx + 1] << (64 - bit_offset);
    if (count < 64)
      w &= (1ULL << count) - 1;
    return w;
  }

  uint64_t to_ullong() const {
    if (blocks_.empty()) return 0;
    return blocks_[0];
  }

  std::string to_string() const {
    if (nbits_ == 0) return "";
    std::string s(nbits_, '0');
    for (std::size_t i = 0; i < nbits_; i++) {
      if (test(i))
        s[nbits_ - 1 - i] = '1';
    }
    return s;
  }
};

// XOR-fold a dynamic_bitset into target_bits, returning a uint64_t (target_bits <= 64).
// Splits the input into chunks of target_bits and XORs them together.
inline uint64_t fold_bitset_to_val(const dynamic_bitset& input, std::size_t target_bits) {
  if (target_bits == 0 || input.size() == 0) return 0;
  uint64_t mask = (target_bits < 64) ? ((1ULL << target_bits) - 1) : ~0ULL;
  uint64_t comp = 0;
  std::size_t len = input.size();
  for (std::size_t pos = 0; pos < len; pos += target_bits) {
    std::size_t chunk_bits = std::min(target_bits, len - pos);
    comp ^= input.extract(pos, chunk_bits);
  }
  return comp & mask;
}

// Runtime versions of fold/truncate for dynamic_bitset
inline dynamic_bitset truncate_bitset(const dynamic_bitset& input, std::size_t target_bits) {
  dynamic_bitset output(target_bits);
  std::size_t copy_bits = std::min(target_bits, input.size());
  std::size_t full_blocks = copy_bits / 64;
  for (std::size_t b = 0; b < full_blocks; b++)
    output.set_word(b, input.word(b));
  // Copy remaining bits in the partial last block
  std::size_t remaining = copy_bits % 64;
  if (remaining > 0) {
    uint64_t mask = (1ULL << remaining) - 1;
    output.set_word(full_blocks, input.word(full_blocks) & mask);
  }
  return output;
}

inline dynamic_bitset fold_bitset(const dynamic_bitset& input, std::size_t outbits) {
  if (outbits == 0) return dynamic_bitset(0);
  // Fast path: result fits in 64 bits (common case)
  if (outbits <= 64) {
    uint64_t val = fold_bitset_to_val(input, outbits);
    return dynamic_bitset(outbits, val);
  }
  // Fallback for large outbits: chunk-XOR with word-level extract
  dynamic_bitset output(outbits);
  std::size_t len = input.size();
  for (std::size_t pos = 0; pos < len; pos += outbits) {
    std::size_t chunk_bits = std::min(outbits, len - pos);
    // XOR chunk into output bit by bit (rare path)
    for (std::size_t i = 0; i < chunk_bits; i++)
      if (input.test(pos + i)) output.set(i, !output.test(i));
  }
  return output;
}

inline std::string format_bitset(const dynamic_bitset& input, int mark = -1) {
  std::string output;
  for (int i = static_cast<int>(input.size()) - 1; i >= 0; i--) {
    if (i == mark)
      output += fmt::format("[{}]", input.test(i) ? 1 : 0);
    else
      output += fmt::format(" {} ", input.test(i) ? 1 : 0);
  }
  return output;
}

inline void print_bitset(const dynamic_bitset& input, int mark = -1) {
  for (std::size_t i = 0; i < input.size(); i++) {
    if (static_cast<int>(i) == mark)
      fmt::print("[{}]", input.test(i) ? 1 : 0);
    else
      fmt::print(" {} ", input.test(i) ? 1 : 0);
  }
  fmt::print("\n");
}

#endif
