#include <catch.hpp>
#include "mocks.hpp"
#include "defaults.hpp"
#include "dram_controller.h"    
#include "champsim_constants.h"
#include <algorithm>
#include <cfenv>
#include <cmath>

void generate_packet(champsim::channel* channel,uint64_t packet_num)
{
    auto pkt_type = packet_num % 2 ? access_type::LOAD : access_type::WRITE;
    champsim::channel::request_type r;
    r.type = pkt_type;
    uint64_t offset = 0;
    champsim::address_slice block_slice{champsim::dynamic_extent{LOG2_BLOCK_SIZE + offset, offset}, 0};
    offset += LOG2_BLOCK_SIZE;
    champsim::address_slice channel_slice{champsim::dynamic_extent{champsim::lg2(DRAM_CHANNELS) + offset, offset}, 0};
    offset += champsim::lg2(DRAM_CHANNELS);
    champsim::address_slice bank_slice{champsim::dynamic_extent{champsim::lg2(DRAM_BANKS) + offset, offset}, packet_num % DRAM_BANKS};
    offset += champsim::lg2(DRAM_BANKS);
    champsim::address_slice column_slice{champsim::dynamic_extent{champsim::lg2(DRAM_COLUMNS) + offset, offset}, 1};
    offset += champsim::lg2(DRAM_COLUMNS);
    champsim::address_slice rank_slice{champsim::dynamic_extent{champsim::lg2(DRAM_RANKS) + offset, offset}, packet_num % DRAM_RANKS};
    offset += champsim::lg2(DRAM_RANKS);
    champsim::address_slice row_slice{champsim::dynamic_extent{64, offset}, packet_num % DRAM_ROWS};
    r.address = champsim::address{champsim::splice(row_slice, rank_slice, column_slice, bank_slice, channel_slice, block_slice)};
    r.v_address = champsim::address{};
    r.instr_id = 0;
    r.response_requested = false;

    if(r.type == access_type::LOAD)
        channel->add_rq(r);
    else
        channel->add_wq(r);
}

std::vector<bool> refresh_test(MEMORY_CONTROLLER* uut, champsim::channel* channel_uut, uint64_t refresh_cycles)
{
    //how many cycles should pass before the next refresh is scheduled. This is also the maximum time that can pass before
    //a refresh MUST be done. If this is violated, then DRAM spec is violated.
    const champsim::chrono::clock::duration tREF = champsim::chrono::picoseconds{64000000000 / (DRAM_ROWS/8)};
    //record the refresh status of each bank
    std::vector<bool> bank_refreshed(DRAM_BANKS,false);

    //advanced current time to first refresh cycle, or test will fail since that is the trigger
    uut->current_time += tREF;
    //num of refresh cycles to cover
    //record whether each refresh cycle was respected
    std::vector<bool> refresh_done;

    //we will cover the first 40 refreshes
    uint64_t refresh_cycle = 2;
    uint64_t cycles = 0;
    while (uut->current_time < champsim::chrono::clock::time_point{} + tREF*refresh_cycles)
    {
        //generate a random packet
        generate_packet(channel_uut,(uint64_t)cycles);
        //operate mem controller
        uut->_operate();
        cycles++;
        //make sure that for every refresh cycle, each bank undergoes refresh at least once
        std::vector<bool> bank_under_refresh;
        std::transform(std::begin(uut->channels[0].bank_request),std::end(uut->channels[0].bank_request),std::back_inserter(bank_under_refresh),[](const auto& entry){return(entry.under_refresh);});
        std::transform(std::begin(bank_refreshed),std::end(bank_refreshed),std::begin(bank_under_refresh),std::begin(bank_refreshed), std::logical_or<>{});
        
        if(uut->current_time >= champsim::chrono::clock::time_point{} + refresh_cycle*tREF)
        {
            refresh_done.push_back(std::all_of(std::begin(bank_refreshed),std::end(bank_refreshed),[](bool v) { return v;}));
            std::fill(std::begin(bank_refreshed),std::end(bank_refreshed),false);
            refresh_cycle++;
        }
    }
    return(refresh_done);
}

SCENARIO("The memory controller refreshes each bank at the proper rate") {
    GIVEN("A random request stream to the memory controller") {
        champsim::channel channel_uut{32, 32, 32, LOG2_BLOCK_SIZE, false};
        const auto clock_period = champsim::chrono::picoseconds{3200};
        const uint64_t trp_cycles = 4;
        const uint64_t trcd_cycles = 4;
        const uint64_t tcas_cycles = 80;
        MEMORY_CONTROLLER uut{clock_period, trp_cycles*clock_period, trcd_cycles*clock_period, tcas_cycles*clock_period, 2*clock_period, {&channel_uut}};
        //test
        uut.warmup = false;
        uut.channels[0].warmup = false;
        //packets need address
        /*
        * | row address | rank index | column address | bank index | channel | block
        * offset |
        */
        WHEN("The memory controller is operated over 40 refresh cycles") {
            std::vector<bool> refresh_status = refresh_test(&uut,&channel_uut,40);
            THEN("Each bank undergoes refresh according to specified timing")
            {
                REQUIRE_THAT(refresh_status, Catch::Matchers::AllTrue());
            }
        }
    }
}
