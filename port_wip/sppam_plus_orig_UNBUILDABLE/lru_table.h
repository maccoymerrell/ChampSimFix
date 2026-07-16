// Minimal set-associative LRU table with champsim::msl::lru_table semantics,
// but returning LIVE POINTERS into fixed storage (no per-lookup value copies).
// The backing vector never resizes, so returned pointers stay valid for the
// table's lifetime.
//
// SetProj / TagProj map a value_type (or, for the raw-key fast path, the integer
// key itself) to a uint64 set-key / tag. The set index is set_projection(...) %
// NUM_SET, so any hashing lives entirely in the SetProj functor (e.g. a region
// table can pass a fold-hash set projection while keeping an identity tag).
#ifndef SPPAM_DSE_LRU_TABLE_H
#define SPPAM_DSE_LRU_TABLE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace sppam_dse
{

template <typename T, typename SetProj, typename TagProj>
class lru_table
{
public:
  using value_type = T;

  lru_table(std::size_t sets, std::size_t ways, SetProj set_proj = SetProj{}, TagProj tag_proj = TagProj{})
      : set_projection(set_proj), tag_projection(tag_proj), NUM_SET(sets ? sets : 1), NUM_WAY(ways ? ways : 1), block(NUM_SET * NUM_WAY)
  {
  }

  // Find matching entry, bumping its LRU stamp. Returns a live pointer or null.
  value_type* find(const value_type& key)
  {
    block_t* b = find_block(key);
    if (b == nullptr)
      return nullptr;
    b->last_used = ++access_count;
    return &b->data;
  }

  value_type* probe(const value_type& key)
  {
    block_t* b = find_block(key);
    return b ? &b->data : nullptr;
  }

  // Allocation-free lookups by raw integer key (valid when the tag projection is
  // the identity on a single integer field, as for all SPPAM tables). The set is
  // computed by applying the SetProj to the key, so a hashing SetProj works here
  // too.
  value_type* find_key(uint64_t key)
  {
    block_t* b = block.data() + set_of_key(key) * NUM_WAY;
    for (block_t* it = b; it != b + NUM_WAY; ++it)
      if (it->valid && tag_projection(it->data) == key) {
        it->last_used = ++access_count;
        return &it->data;
      }
    return nullptr;
  }
  value_type* probe_key(uint64_t key)
  {
    block_t* b = block.data() + set_of_key(key) * NUM_WAY;
    for (block_t* it = b; it != b + NUM_WAY; ++it)
      if (it->valid && tag_projection(it->data) == key)
        return &it->data;
    return nullptr;
  }

  value_type& insert(const value_type& key)
  {
    auto [b, e] = set_span(key);
    auto tag = tag_projection(key);
    block_t* victim = b;
    block_t* match = nullptr;
    if (evict_rank) {
      // Policy victim: a free way first, else the VALID way maximizing evict_rank(value, age) (higher = evict).
      bool have_free = false, have_rank = false;
      int64_t best_rank = 0;
      for (block_t* it = b; it != e; ++it) {
        if (it->valid && tag_projection(it->data) == tag) match = it;
        if (!it->valid) { if (!have_free) { victim = it; have_free = true; } }
        else if (!have_free) {
          int64_t r = evict_rank(it->data, static_cast<uint64_t>(access_count - it->last_used));
          if (!have_rank || r > best_rank) { best_rank = r; victim = it; have_rank = true; }
        }
      }
    } else {
      for (block_t* it = b; it != e; ++it) {
        if (it->valid && tag_projection(it->data) == tag)
          match = it;
        if (!it->valid || (victim->valid && it->last_used < victim->last_used))
          victim = it;
      }
    }
    block_t* dst = match ? match : victim;
    // Notify on a real eviction (replacing a different valid entry).
    if (on_evict && !match && victim->valid)
      on_evict(victim->data);
    dst->valid = true;
    dst->last_used = ++access_count;
    dst->data = key;
    return dst->data;
  }

  // Optional eviction observer (called with the evicted value before overwrite).
  // Null by default => zero cost on the hot path.
  std::function<void(const value_type&)> on_evict;
  // Optional victim-ranking policy (value, age=access_count-last_used) -> rank; higher = evict first. Null => LRU.
  std::function<int64_t(const value_type&, uint64_t)> evict_rank;

  bool invalidate(const value_type& key)
  {
    block_t* b = find_block(key);
    if (b == nullptr)
      return false;
    *b = block_t{};
    return true;
  }

private:
  struct block_t {
    bool valid = false;
    uint64_t last_used = 0;
    value_type data{};
  };

  block_t* find_block(const value_type& key)
  {
    auto [b, e] = set_span(key);
    auto tag = tag_projection(key);
    for (block_t* it = b; it != e; ++it)
      if (it->valid && tag_projection(it->data) == tag)
        return it;
    return nullptr;
  }

  std::size_t set_of_key(uint64_t key) { return static_cast<std::size_t>(set_projection(key)) % NUM_SET; }

  std::pair<block_t*, block_t*> set_span(const value_type& key)
  {
    block_t* b = block.data() + (static_cast<std::size_t>(set_projection(key)) % NUM_SET) * NUM_WAY;
    return {b, b + NUM_WAY};
  }

  SetProj set_projection;
  TagProj tag_projection;
  std::size_t NUM_SET;
  std::size_t NUM_WAY;
  uint64_t access_count = 0;
  std::vector<block_t> block;
};

} // namespace sppam_dse

#endif
