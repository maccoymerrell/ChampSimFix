#include <vector>
#include <bitset>
#include <fstream>

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
      std::cout << "The channel : " << req.addr_vec[m_dram->m_levels("channel")] << std::endl;
      //bank group
      //std::cout << "The bankgroup before: " << req.addr_vec[m_dram->m_levels("bankgroup")] << std::endl;
      if(m_dram->m_organization.count.size() > 5)
      req.addr_vec[m_dram->m_levels("bankgroup")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bankgroup")]);
      std::cout << "The bankgroup: " << req.addr_vec[m_dram->m_levels("bankgroup")] << std::endl;

      //bank
      req.addr_vec[m_dram->m_levels("bank")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bank")]);
      std::cout << "The bank: " << req.addr_vec[m_dram->m_levels("bank")] << std::endl;
      //column
      req.addr_vec[m_dram->m_levels("column")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("column")]);
      std::cout << "The column: " << req.addr_vec[m_dram->m_levels("column")] << std::endl;
      //rank
      req.addr_vec[m_dram->m_levels("rank")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("rank")]);
      std::cout << "The rank: " << req.addr_vec[m_dram->m_levels("rank")] << std::endl;
      //row
      req.addr_vec[m_dram->m_levels("row")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("row")]);
      std::cout << "The row: " << req.addr_vec[m_dram->m_levels("row")] << std::endl;
      std::cout << std::endl;
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

    // store the previous address vector
    std::vector<Addr_t> m_prev_addr_vec;

    // make a vector to store power consumption rates
    std::vector<double> power_consumption_rates;

    void init() override { };
    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
      m_dram = memory_system->get_ifce<IDRAM>();

      // Populate m_addr_bits vector with the number of address bits for each level in the hierachy
      const auto& count = m_dram->m_organization.count;
      m_num_levels = count.size();
      m_addr_bits.resize(m_num_levels);
      for (size_t level = 0; level < m_addr_bits.size(); level++) {
        m_addr_bits[level] = calc_log2(count[level]);
        //std::cout << "This is the number of bits in level [" << level << "]: " << m_addr_bits[level] << std::endl;
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

      //tot power
      std::vector<Addr_t> tot_power; 

      // initialize the previous address vector with the same size
      m_prev_addr_vec.assign(m_num_levels, 0);
    }
    
    // initialize bit counter
    int bit_counter = 0;
    int num_bits_pc = 0;

    void apply(Request& req) override {
      req.addr_vec.resize(m_num_levels, -1);

      // initialize xor result to hold power consumption for each level
      Addr_t xor_result_power = 0;

      // initialize power_cons to hold the bit changes for each level
      std::vector<Addr_t> power_vector;

      //retrieve the number of bits for the level currently in
      int num_bits = m_addr_bits.size();

      Addr_t col1_bits = 12 - m_tx_offset - m_addr_bits[m_dram->m_levels("bankgroup")] - m_addr_bits[m_dram->m_levels("bank")] - m_addr_bits[m_dram->m_levels("channel")];
      //std::cout << "The number of col1_bits [" << col1_bits << "]." << std::endl;
      Addr_t col2_bits = m_addr_bits[m_dram->m_levels("column")] - col1_bits;
      //std::cout << "The number of col2_bits [" << col2_bits << "]." << std::endl;
      Addr_t addr = req.addr >> m_tx_offset;
      //std::cout << "The address is: " << addr << std::endl;
      Addr_t xor_bits = req.addr >> 17;
      //std::cout << "The xor_bits being used: " << xor_bits << std::endl;

      //channel
      req.addr_vec[m_dram->m_levels("channel")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("channel")]);
      //std::cout << "The channel address is: " << req.addr_vec[m_dram->m_levels("channel")] << std::endl;
      //col 1
      req.addr_vec[m_dram->m_levels("column")] = slice_lower_bits(addr, col1_bits);
      //std::cout << "The column address is: " << req.addr_vec[m_dram->m_levels("column")] << std::endl;
      //bank group and bank
      if(m_dram->m_organization.count.size() > 5)
      {
        int bankgroup_val = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bankgroup")]) ^ xor_bits;
        req.addr_vec[m_dram->m_levels("bankgroup")] = slice_lower_bits(bankgroup_val, m_addr_bits[m_dram->m_levels("bankgroup")]);
        //std::cout << "After count > 5, bankgroup size is: " << req.addr_vec[m_dram->m_levels("bankgroup")] << std::endl;

        int bank_val = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bank")]) ^ (xor_bits >> m_addr_bits[m_dram->m_levels("bankgroup")]);
        req.addr_vec[m_dram->m_levels("bank")] = slice_lower_bits(bank_val,m_addr_bits[m_dram->m_levels("bank")]);
        //std::cout << "After count > 5, bank size is: " << req.addr_vec[m_dram->m_levels("bank")] << std::endl;
      }
      else
      {
        int bank_val = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("bank")]) ^ xor_bits;
        req.addr_vec[m_dram->m_levels("bank")] = slice_lower_bits(bank_val, m_addr_bits[m_dram->m_levels("bank")]);
        //std::cout << "The bankgroup size is: " << req.addr_vec[m_dram->m_levels("bank")] << std::endl;
      }
      //col 2
      req.addr_vec[m_dram->m_levels("column")] += slice_lower_bits(addr, col2_bits) << col1_bits;
      //std::cout << "The column bits is: " << req.addr_vec[m_dram->m_levels("column")] << std::endl;
      //rank
      req.addr_vec[m_dram->m_levels("rank")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("rank")]);
      //std::cout << "The rank bits is: " << req.addr_vec[m_dram->m_levels("rank")] << std::endl;
      //row
      req.addr_vec[m_dram->m_levels("row")] = slice_lower_bits(addr, m_addr_bits[m_dram->m_levels("row")]);
      //std::cout << "The row address is: " << req.addr_vec[m_dram->m_levels("row")] << std::endl;
      //std::cout << std::endl;

      // calculate bit changes for power consumption
      for (size_t i = 0; i < m_num_levels; ++i) {
        // uncomment for analysis [NOT FOR LONG RUNS]
        //std::cout << "The current address for level [" << i << "]: " << req.addr_vec[i] << std::endl;
        //std::cout << "The prev address for level [" << i << "]: " << m_prev_addr_vec[i] << std::endl;
        xor_result_power |= (req.addr_vec[i] ^ m_prev_addr_vec[i]);
        //std::cout << "The xor result: " << xor_result_power << std::endl;
      }
      //std::cout << std::endl;
      // store xor_result into power_vector
      power_vector.push_back(xor_result_power);


      // power consumption -----------------------------------------------------------------------------
        
      // convert to binary
      std::bitset<64> binary_representation(xor_result_power);

      // count the number of 1s in the xor result 
      int bit_transitions = std::bitset<64>(xor_result_power).count();

      //update the total bit counter for transistions and total bits
      bit_counter += bit_transitions;
      num_bits_pc += m_addr_bits.size();

      // Cast to float for proper decimal division
      double power_consumption_rate = (static_cast<double>(bit_counter) / static_cast<double>(num_bits_pc)) * 100;

      // uncomment this for analysis [NOT FOR LONGER RUNS]
      //std::cout << "The power consumption rate: " << std::dec << power_consumption_rate << "%" << std::endl << std::endl;  
      power_consumption_rates.push_back(power_consumption_rate);

      m_prev_addr_vec.assign(req.addr_vec.begin(), req.addr_vec.end());

      writePowerConsumptionRatesToFile("power_consumption_rates_PBPI.txt");
    }

    void writePowerConsumptionRatesToFile(const std::string& filename) const {
        std::ofstream outFile(filename);  // Open the file for writing

        if (outFile.is_open()) {
            for (size_t i = 0; i < power_consumption_rates.size(); ++i) {
                outFile << "Cumulative Power Consumption: " << power_consumption_rates[i] << "%" << std::endl;
            }
            outFile.close();  // Close the file
        } else {
            std::cerr << "Unable to open file " << filename << std::endl;
        }
    }
    
  };
  /****************************************This is where I will apply my method RASL - Yanez Saucedo*******************************************/
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

    // store the previous address vector
    std::vector<Addr_t> m_prev_addr_vec;

    // make a vector to store power consumption rates
    std::vector<double> power_consumption_rates;

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

      // initialize the previous address vector with the same size
      m_prev_addr_vec.assign(m_num_levels, 0);
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

      //std::cout << std::endl;

      // initialize xor result to hold power consumption for each level
      Addr_t xor_result_power = 0;

      // initialize power_cons to hold the bit changes for each level
      std::vector<Addr_t> power_vector;

      // Generate random address bits for each level (iterate each level)
      for(size_t level = 0; level < m_num_levels; level++){

        //retrieve the number of bits for the level currently in
        int num_bits = m_addr_bits[level];
      
        //initialize rasl_addr to hold the randomized address bits for current level
        Addr_t rasl_addr = 0;

        // loop each bit of the address level currently in to extract
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
        // uncomment this for analysis [NOT FOR LONGER RUNS]
        //std::cout << "This is the previous address for level [" << level << "] : " << m_prev_addr_vec[level] << std::endl;
        //std::cout << "This is the current address for level [" << level << "] : " << rasl_addr << std::endl;
        xor_result_power = m_prev_addr_vec[level] ^ rasl_addr;
        //std::cout << "This is the xor result of level [" << level << "] : " << xor_result_power << std::endl;
        //std::cout << std::endl;

        // store xor_result into power_vector
        power_vector.push_back(xor_result_power);
    
        //store the result of RASL to the corresponding level
        req.addr_vec[level] = rasl_addr;

        //prepare 'addr' for the next level by shifting out the bits we've just proccessed
        addr >> num_bits;

        // update m_prev_addr_vec with current RASL for next comparison
        m_prev_addr_vec[level] = req.addr_vec[level];

        // power consumption -----------------------------------------------------------------------------
        int bit_one_counter = 0;
        
        // convert to binary
        std::bitset<64> binary_representation(xor_result_power);

        // check if binary representation is correct
        std::string binary_str = binary_representation.to_string().substr(64-num_bits);

        // count the total number of 1 bits
        for (char bit : binary_str){
          if (bit == '1'){
            bit_one_counter += 1;
          }
        }

        bit_counter = bit_one_counter + bit_counter;
        num_bits_pc = num_bits + num_bits_pc;

      }
      // Cast to float for proper decimal division
      double power_consumption_rate = (static_cast<double>(bit_counter) / static_cast<double>(num_bits_pc)) * 100;

      // uncomment this for analysis [NOT FOR LONGER RUNS]
      //std::cout << "The power consumption rate: " << std::dec << power_consumption_rate << "%" << std::endl << std::endl;  
      power_consumption_rates.push_back(power_consumption_rate);

      // using this when not using the text file
      for (size_t p = 0; p < power_consumption_rates.size(); ++p)
      {
        std::cout << "power consumption = " << power_consumption_rates[p] << "%" << std::endl;
      }

      //writePowerConsumptionRatesToFile("power_consumption_rates_rasl.txt");
    }

    void writePowerConsumptionRatesToFile(const std::string& filename) const {
        std::ofstream outFile(filename);  // Open the file for writing

        if (outFile.is_open()) {
            for (size_t i = 0; i < power_consumption_rates.size(); ++i) {
                outFile << "Cumulative Power Consumption: " << power_consumption_rates[i] << "%" << std::endl;
            }
            outFile.close();  // Close the file
        } else {
            std::cerr << "Unable to open file " << filename << std::endl;
        }
    }
  };
}