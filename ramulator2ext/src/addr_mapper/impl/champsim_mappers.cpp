#include <vector>
#include <bitset>

#include "base/base.h"
#include "dram/dram.h"
#include "addr_mapper/addr_mapper.h"
#include "memory_system/memory_system.h"

namespace Ramulator{
  class RoRaCoBaBgCh final : public IAddrMapper, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IAddrMapper, RoRaCoBaBgCh, "RoRaCoBaBgCh", "Applies a RoRaCoBaBgCh mapping to the address. (Default ChampSim)");

  public:
    IDRAM* m_dram = nullptr;

    int m_num_levels = -1;          // How many levels in the hierarchy?
    std::vector<int> m_addr_bits;   // How many address bits for each level in the hierarchy?
    Addr_t m_tx_offset = -1;

    int m_col_bits_idx = -1;
    int m_row_bits_idx = -1;

    void init() override { };
    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
      m_dram = memory_system->get_ifce<IDRAM>();

      // Populate m_addr_bits vector with the number of address bits for each level in the hierachy
      const auto& count = m_dram->m_organization.count;
      m_num_levels = count.size();
      m_addr_bits.resize(m_num_levels);
      for (size_t level = 0; level < m_addr_bits.size(); level++) {
        m_addr_bits[level] = calc_log2(count[level]);
      }

      // Last (Column) address have the granularity of the prefetch size
      m_addr_bits[m_num_levels - 1] -= calc_log2(m_dram->m_internal_prefetch_size);

      int tx_bytes = m_dram->m_internal_prefetch_size * m_dram->m_channel_width / 8;
      m_tx_offset = calc_log2(tx_bytes);

      // Determine where are the row and col bits for ChRaBaRoCo and RoBaRaCoCh
      try {
        m_row_bits_idx = m_dram->m_levels("row");
      } catch (const std::out_of_range& r) {
        throw std::runtime_error(fmt::format("Organization \"row\" not found in the spec, cannot use linear mapping!"));
      }

      // Assume column is always the last level
      m_col_bits_idx = m_num_levels - 1;
    }


    void apply(Request& req) override {
      req.addr_vec.resize(m_num_levels, -1);
      Addr_t addr = req.addr >> m_tx_offset;
      //channel
      req.addr_vec[m_dram->m_levels("channel")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("channel")]);
      //bank group
      if(m_dram->m_organization.count.size() > 5)
      req.addr_vec[m_dram->m_levels("bankgroup")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bankgroup")]);
      //bank
      req.addr_vec[m_dram->m_levels("bank")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bank")]);
      //column
      req.addr_vec[m_dram->m_levels("column")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("column")]);
      //rank
      req.addr_vec[m_dram->m_levels("rank")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("rank")]);
      //row
      req.addr_vec[m_dram->m_levels("row")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("row")]);
    }
    
  };

  class PBPI_Mapping final : public IAddrMapper, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IAddrMapper, PBPI_Mapping, "PBPI_Mapping", "Applies a PBPI Mapping to the address. (Alternate ChampSim)");

  public:
    IDRAM* m_dram = nullptr;

    int m_num_levels = -1;          // How many levels in the hierarchy?
    std::vector<int> m_addr_bits;   // How many address bits for each level in the hierarchy?
    Addr_t m_tx_offset = -1;

    int m_col_bits_idx = -1;
    int m_row_bits_idx = -1;

    void init() override { };
    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
      m_dram = memory_system->get_ifce<IDRAM>();

      // Populate m_addr_bits vector with the number of address bits for each level in the hierachy
      const auto& count = m_dram->m_organization.count;
      m_num_levels = count.size();
      m_addr_bits.resize(m_num_levels);
      for (size_t level = 0; level < m_addr_bits.size(); level++) {
        m_addr_bits[level] = calc_log2(count[level]);
      }

      // Last (Column) address have the granularity of the prefetch size
      m_addr_bits[m_num_levels - 1] -= calc_log2(m_dram->m_internal_prefetch_size);

      int tx_bytes = m_dram->m_internal_prefetch_size * m_dram->m_channel_width / 8;
      m_tx_offset = calc_log2(tx_bytes);

      // Determine where are the row and col bits for ChRaBaRoCo and RoBaRaCoCh
      try {
        m_row_bits_idx = m_dram->m_levels("row");
      } catch (const std::out_of_range& r) {
        throw std::runtime_error(fmt::format("Organization \"row\" not found in the spec, cannot use linear mapping!"));
      }

      // Assume column is always the last level
      m_col_bits_idx = m_num_levels - 1;
    }


    void apply(Request& req) override {
      req.addr_vec.resize(m_num_levels, -1);

      Addr_t col1_bits = 12 - m_tx_offset - m_addr_bits[m_dram->m_levels("bankgroup")] - m_addr_bits[m_dram->m_levels("bank")] - m_addr_bits[m_dram->m_levels("channel")];
      Addr_t col2_bits = m_addr_bits[m_dram->m_levels("column")] - col1_bits;
      Addr_t addr = req.addr >> m_tx_offset;
      Addr_t xor_bits = req.addr >> 17;

      //channel
      req.addr_vec[m_dram->m_levels("channel")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("channel")]);
      //col 1
      req.addr_vec[m_dram->m_levels("column")] = slice_lower_bits(addr, col1_bits);
      //bank group and bank
      if(m_dram->m_organization.count.size() > 5)
      {
        int bankgroup_val = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bankgroup")]) ^ xor_bits;
        req.addr_vec[m_dram->m_levels("bankgroup")] = slice_lower_bits(bankgroup_val, m_addr_bits[m_dram->m_levels("bankgroup")]);

        int bank_val = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bank")]) ^ (xor_bits >> m_addr_bits[m_dram->m_levels("bankgroup")]);
        req.addr_vec[m_dram->m_levels("bank")] = slice_lower_bits(bank_val,m_addr_bits[m_dram->m_levels("bank")]);
      }
      else
      {
        int bank_val = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bank")]) ^ xor_bits;
        req.addr_vec[m_dram->m_levels("bank")] = slice_lower_bits(bank_val, m_addr_bits[m_dram->m_levels("bank")]);
      }
      //col 2
      req.addr_vec[m_dram->m_levels("column")] += slice_lower_bits(addr, col2_bits) << col1_bits;
      //rank
      req.addr_vec[m_dram->m_levels("rank")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("rank")]);
      //row
      req.addr_vec[m_dram->m_levels("row")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("row")]);
    }
    
  };
  /****************************************This is where I will apply my method RASL*******************************************/
  class RASL final : public IAddrMapper, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IAddrMapper, RASL, "RASL", "Applies a RASL Mapping to the address. Yanez's Scheme.");
    // We will try to increase randomization without using too much power

    public:
    IDRAM* m_dram = nullptr;

    int m_num_levels = -1;          // How many levels in the hierarchy?
    std::vector<int> m_addr_bits;   // How many address bits for each level in the hierarchy?

    Addr_t m_tx_offset = -1;

    int m_col_bits_idx = -1;
    int m_row_bits_idx = -1;

    void init() override { };
    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
      m_dram = memory_system->get_ifce<IDRAM>();

      // Populate m_addr_bits vector with the number of address bits for each level in the hierachy
      const auto& count = m_dram->m_organization.count;

      // count the num of levels in our hierarchy
      m_num_levels = count.size();
       //std::cout << "The number of levels in our hierarchy: " << m_num_levels << std::endl;

      m_addr_bits.resize(m_num_levels);
      for (size_t level = 0; level < m_addr_bits.size(); level++) {
        m_addr_bits[level] = calc_log2(count[level]);
        //std::cout << "This is the number of bits in [" << level << "]: " << m_addr_bits[level] << std::endl; 
      }

      // Last (Column) address have the granularity of the prefetch size
      m_addr_bits[m_num_levels - 1] -= calc_log2(m_dram->m_internal_prefetch_size);

      int tx_bytes = m_dram->m_internal_prefetch_size * m_dram->m_channel_width / 8;
      m_tx_offset = calc_log2(tx_bytes);

      // Determine where are the row and col bits
      try {
        m_row_bits_idx = m_dram->m_levels("row");
      } catch (const std::out_of_range& r) {
        throw std::runtime_error(fmt::format("Organization \"row\" not found in the spec, cannot use linear mapping!"));
      }

      // Assume column is always the last level
      m_col_bits_idx = m_num_levels - 1;

      //tot power
      std::vector<Addr_t> tot_power; 
    }

    // initialize bit counter
    int bit_counter = 0;
    int num_bits_pc = 0;
    
    void apply(Request& req) override {
      // initialize addr_vec and resize to match the number of levels in the DRAM hierarchy
      req.addr_vec.resize(m_num_levels, -1);

      //shift the original address to the right by offset bits.
      Addr_t addr = req.addr >> m_tx_offset;

      //channel
      req.addr_vec[m_dram->m_levels("channel")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("channel")]);
      //std::cout << "This is the current address of the channel: 0x" << std::hex << req.addr_vec[m_dram->m_levels("channel")] << std::endl;

      //bank group
      if(m_dram->m_organization.count.size() > 5)
      req.addr_vec[m_dram->m_levels("bankgroup")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bankgroup")]);
      //std::cout << "This is the current address of the bankgroup: 0x" << std::hex << req.addr_vec[m_dram->m_levels("bankgroup")] << std::endl;

      //bank
      req.addr_vec[m_dram->m_levels("bank")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bank")]);
      //std::cout << "This is the current address of the bank: 0x" << std::hex << req.addr_vec[m_dram->m_levels("bank")] << std::endl;

      //column
      req.addr_vec[m_dram->m_levels("column")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("column")]);
      //std::cout << "This is the current address of the column: 0x" << std::hex << req.addr_vec[m_dram->m_levels("column")] << std::endl;

      //rank
      req.addr_vec[m_dram->m_levels("rank")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("rank")]);
      //std::cout << "This is the current address of the rank: 0x" << std::hex << req.addr_vec[m_dram->m_levels("rank")] << std::endl;

      //row
      req.addr_vec[m_dram->m_levels("row")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("row")]);
      //std::cout << "This is the current address of the row: 0x" << std::hex << req.addr_vec[m_dram->m_levels("row")] << std::endl;

      std::cout << std::endl;

      // initialize xor result to hold power consumption for each level
      Addr_t xor_result_power = 0;

      // initialize power_cons to hold the bit changes for each level
      std::vector<Addr_t> power_vector;

      // Generate random address bits for each level (iterate each level)
      for(size_t level = 0; level < m_num_levels; level++){

        //retrieve the number of bits for the level currently in
        int num_bits = m_addr_bits[level];
        //std::cout << "The number of bits in this level is: " << num_bits << std::endl;
      
        //initialize rasl_addr to hold the randomized address bits for current level
        Addr_t rasl_addr = 0;

        // loop each bit of the address level currently in to extract
        //std::cout << "This is the current address bit before RASL: 0x" << std::hex << req.addr_vec[level] << std::endl;
        for(int bit = 0;bit < num_bits; bit++){
          //extract the bit at 'bit position, then shift addr right by that 'bit'
          // bitwise 1 is to isolate the single bit at that position
          Addr_t extracted_bit = (req.addr_vec[level] >> bit) & 1;
          //place the extracted bit in a new, shuffled position
          //add 3 bits to the current bit position and check if new position is within available bits for current level
          int new_position = (bit + 3) % num_bits;
          //place the extracted bit in rasl_addr. Shift the extracted bit left by new_position and bitwise OR with
          //rasl_addr to combine with previous bits
          rasl_addr |= (extracted_bit << new_position);
        }
        // power consumption result for each level
        xor_result_power = req.addr_vec[level] ^ rasl_addr;

        // store xor_result into power_vector
        power_vector.push_back(xor_result_power);
      
        //std::cout << "This is the vector after RASL: " << std::hex << "0x" << rasl_addr << std::endl;
    
        //store the result of RASL to the corresponding level
        req.addr_vec[level] = rasl_addr;
        //std::cout << "This is the vector mapped back: req.addr_vec[" << level << "] = 0x" << rasl_addr << ";" << std::endl;
        //std::cout << "This is the power consumption at level " << level << ": 0x" << xor_result_power << std::endl;

        //prepare 'addr' for the next level by shifting out the bits we've just proccessed
        addr >> num_bits;
        //std::cout << std::endl;

        // power consumption -----------------------------------------------------------------------------
        int bit_one_counter = 0;
        
        // convert to binary
        std::bitset<64> binary_representation(xor_result_power);

        // check if binary representation is correct
        std::string binary_str = binary_representation.to_string().substr(64-num_bits);
        //std::cout << "Power consumption vector in binary: " << binary_str << std::endl;

        // count the total number of 1 bits
        for (char bit : binary_str){
          if (bit == '1'){
            bit_one_counter += 1;
          }
        }

        bit_counter = bit_one_counter + bit_counter;
        num_bits_pc = num_bits + num_bits_pc;

        //std::cout << "Bit 1 counts: " << bit_one_counter << std::endl;
        //std::cout << "Total bits: " << num_bits << std::endl;
        //std::cout << std::endl;
      }
      //std::cout << std::endl;
      // Cast to float for proper decimal division
      double power_consumption_rate = (static_cast<double>(bit_counter) / static_cast<double>(num_bits_pc)) * 100;
      //std::cout << "The total number of '1' is: " << std::dec << bit_counter << std::endl;
      //std::cout << "The total number of bits is: " << std::dec << num_bits_pc << std::endl; 
      // I need to fix this and convert the hex values into decimal so i can get an accurate pc rate.
      std::cout << "The power consumption rate: " << std::dec << power_consumption_rate << "%" << std::endl << std::endl;  
    }
    

  };
}