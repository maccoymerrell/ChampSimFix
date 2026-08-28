/*
 * NMFCMinimalist — a ramulator2 address mapping built for parallelism rather
 * than for row hits.
 *
 * Registered into ramulator2's factory from this tree rather than dropped into
 * the vendored clone, because ext/ is not tracked: a mapping that lives only in
 * a checkout is a result nobody else can reproduce.
 *
 * WHY NOT THE DEFAULT. RoBaRaCoCh puts the column at the bottom and everything
 * else above it, so a whole row is walked before a second bank is touched. That
 * is right when a workload streams, and this one does not: a graph traversal
 * reaches scattered dependent addresses, so consecutive requests are unrelated
 * and the row buffer cannot help whatever the mapping does. Measured at 6.7%
 * row hits, and no mapping recovers locality a workload does not have.
 *
 * What is available is bank-level parallelism. The tiles issue hundreds of
 * independent requests at once, and whether those proceed together or serialise
 * is decided by whether they land on different banks.
 *
 * STRUCTURE, following the MINIMALIST mapper in ChampSimDevelop's ramulator2
 * clone rather than inventing one. From the least significant bit:
 *
 *   gang of consecutive columns | channel | bankgroup | bank | rank |
 *   remaining columns | row
 *
 * The gang is the only row locality kept -- enough consecutive transactions to
 * amortise an activation, and then move on. The ordering after it is not
 * arbitrary: DDR5 charges nCCDS between bank groups against nCCDL within one,
 * and nRRDS against nRRDL for activates, so stepping to the next *bank group*
 * is the cheapest move available and belongs lowest. A rank change turns the
 * data bus around and belongs highest. Taking the levels in the order the
 * organisation lists them puts rank at the bottom, which alternates the
 * costliest level on every gang -- which is what this did before reading the
 * reference.
 *
 * HASHING. Structures here are grain-aligned and walked with regular strides,
 * so an unhashed index sends a stride that is a multiple of the bank span onto
 * one bank forever. The reference folds a few row bits into each index bit --
 * sparse taps spaced apart, not a wholesale fold -- which is enough to break
 * strides while staying cheap. Each index bit takes the parity of three row
 * bits; the taps are spaced so that neighbouring index bits do not see the same
 * row bits.
 *
 * A note on the reference: its helper is gitBit(BitIndex, address), but every
 * call site passes gitBit(row_bits, tap), so it evaluates (1 << row_bits) &
 * tap, which is zero for any realistic row width. Its hash does nothing as
 * written. The tap structure is worth having; the argument order is corrected
 * here.
 *
 * BIJECTIVITY IS CHECKED, not assumed -- also from the reference, which sweeps
 * addresses at setup and aborts if any (bank index, column) pair is claimed
 * twice. A mapping that aliased would be invisible below this point and would
 * show as row hits between unrelated addresses.
 *
 * Parameters (in the controller's addr_mapper node):
 *   gang_size    consecutive column entries kept on one row buffer (default 4)
 *   hash_banks   fold row bits into the bank and bank-group indices (default true)
 */

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/addr_mapper/addr_mapper_base.h"
#include "ramulator/controller/addr_mapper/i_addr_mapper.h"

namespace Ramulator
{

class NMFCMinimalist : public IAddrMapper, public AddrMapperBase
{
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IAddrMapper, NMFCMinimalist, AddrMapperBase, "NMFCMinimalist")

  int m_gang_bits = 2;
  bool m_hash_banks = true;

  // Levels above the channel, by name. -1 where the device has none.
  int m_rank_idx = -1;
  int m_bankgroup_idx = -1;
  int m_bank_idx = -1;

  void init() override
  {
    AddrMapperBase::init();
    int gang = 4;
    RAMULATOR_PARSE_PARAM(gang, int, "gang_size").default_val(4);
    RAMULATOR_PARSE_PARAM(m_hash_banks, bool, "hash_banks").default_val(true);
    m_gang_bits = 0;
    while ((1 << (m_gang_bits + 1)) <= gang) {
      ++m_gang_bits;
    }
    m_gang_bits = std::min(m_gang_bits, m_addr_bits[m_col_idx]);

    const auto& spec = *m_ctrl->m_device.m_spec;
    // Shift past the channel, which this mapper does not place: one instance
    // per memory tile means the channel was chosen before the address arrived.
    const auto level_index = [&spec](const char* name) {
      const int id = spec.get_level_id(name);
      return id > 0 ? id - 1 : -1;
    };
    m_rank_idx = level_index("Rank");
    m_bankgroup_idx = level_index("BankGroup");
    m_bank_idx = level_index("Bank");

    verify_bijective();
  }

  /** Bits at level `idx`, or zero where the device has no such level. */
  [[nodiscard]] int bits_of(int idx) const { return idx >= 0 ? m_addr_bits[idx] : 0; }

  /** Parity of three row bits, spaced so neighbouring index bits see different ones. */
  static Addr_t taps(Addr_t row, int row_bits, int base, int stride, int count)
  {
    Addr_t acc = 0;
    for (int t = 0; t < count; ++t) {
      const int bit = base + t * stride;
      if (bit < row_bits) {
        acc ^= (row >> bit) & 1;
      }
    }
    return acc;
  }

  void apply(Request& req) override
  {
    req.addr_vec.resize(m_num_mapped_levels + 1, -1);
    Addr_t addr = req.intra_channel_addr >> m_tx_offset;

    // The gang: the only row locality this mapping keeps.
    Addr_t column = slice_lower_bits(addr, m_gang_bits);
    // Then the levels that are cheap to alternate, cheapest first.
    Addr_t bankgroup = slice_lower_bits(addr, bits_of(m_bankgroup_idx));
    Addr_t bank = slice_lower_bits(addr, bits_of(m_bank_idx));
    Addr_t rank = slice_lower_bits(addr, bits_of(m_rank_idx));
    // Whatever column is left sits above them, and the row above that.
    column |= slice_lower_bits(addr, m_addr_bits[m_col_idx] - m_gang_bits) << m_gang_bits;
    const Addr_t row = slice_lower_bits(addr, m_addr_bits[m_row_idx]);

    if (m_hash_banks) {
      const int row_bits = m_addr_bits[m_row_idx];
      for (int bg = 0; bg < bits_of(m_bankgroup_idx); ++bg) {
        bankgroup ^= taps(row, row_bits, 2 + bg, 5, 3) << bg;
      }
      for (int b = 0; b < bits_of(m_bank_idx); ++b) {
        bank ^= taps(row, row_bits, b, 5, 3) << b;
      }
    }

    if (m_bankgroup_idx >= 0) {
      req.addr_vec[m_bankgroup_idx + 1] = static_cast<int>(bankgroup);
    }
    if (m_bank_idx >= 0) {
      req.addr_vec[m_bank_idx + 1] = static_cast<int>(bank);
    }
    if (m_rank_idx >= 0) {
      req.addr_vec[m_rank_idx + 1] = static_cast<int>(rank);
    }
    req.addr_vec[m_row_idx + 1] = static_cast<int>(row);
    req.addr_vec[m_col_idx + 1] = static_cast<int>(column);
  }

  /**
   * Sweep one row's worth of addresses across every bank and confirm each
   * (bank index, column) pair is claimed exactly once.
   *
   * The hash is only safe because it permutes the index for a fixed row; if a
   * tap were ever taken from a bit that is part of the index itself the map
   * would fold, and nothing below here could tell.
   */
  void verify_bijective()
  {
    const int index_bits = bits_of(m_rank_idx) + bits_of(m_bankgroup_idx) + bits_of(m_bank_idx);
    const std::size_t groups = std::size_t{1} << index_bits;
    const std::size_t columns = std::size_t{1} << m_addr_bits[m_col_idx];
    std::vector<bool> claimed(groups * columns, false);

    const Addr_t tx = Addr_t{1} << m_tx_offset;
    const Addr_t base = static_cast<Addr_t>(groups * columns) * tx * 2151; // an arbitrary row, not row zero
    for (std::size_t i = 0; i < groups * columns; ++i) {
      Request probe(0, Request::Type::Read);
      probe.intra_channel_addr = base + static_cast<Addr_t>(i) * tx;
      apply(probe);

      std::size_t index = 0;
      int shift = 0;
      for (const int level : {m_bank_idx, m_bankgroup_idx, m_rank_idx}) {
        if (level >= 0) {
          index |= static_cast<std::size_t>(probe.addr_vec[level + 1]) << shift;
          shift += m_addr_bits[level];
        }
      }
      const auto slot = index * columns + static_cast<std::size_t>(probe.addr_vec[m_col_idx + 1]);
      if (claimed[slot]) {
        throw std::runtime_error("NMFCMinimalist: address mapping is not one-to-one; two addresses claim one (bank, column)");
      }
      claimed[slot] = true;
    }
  }
};

} // namespace Ramulator
