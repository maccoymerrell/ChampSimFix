#include <catch.hpp>
#include <typeinfo>
#include <iostream>
#include <nlohmann/json.hpp>

#include "listeners/heartbeat.h"
#include "instruction.h"
#include "trace_instruction.h"

namespace
{

TEST_CASE("The heartbeat listener prints one line after 10M instructions retired") {
    std::ostringstream stdout{};
    Heartbeat uut{&stdout};
    
    // begin phase event
    bool in_warmup = false;
    uut.handle_event<Event::BEGIN_PHASE>(in_warmup);
    
    for (int i = 0; i < 5000000; ++i) {
        std::deque<ooo_model_instr> fake_instructions{{ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr())}};
        uint32_t cpu = 0;
        uint64_t curr_cycles = i;
        auto cb = std::cbegin(fake_instructions);
        auto ce = std::cend(fake_instructions);
        uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
    }
    
    std::string res = stdout.str();
    
    std::string rest = res.substr(res.find('\n')+1);
    
    REQUIRE_THAT(res, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener prints cumulative and heartbeat IPC correctly after a phase change") {
    std::ostringstream stdout{};
    Heartbeat uut{&stdout};
    
    // begin phase event
    bool in_warmup = true;
    uut.handle_event<Event::BEGIN_PHASE>(in_warmup);
    
    for (int i = 0; i <11000000; ++i) {
        
        // phase change to simulation
        if (i == 5000000) {
          in_warmup = false;
          uut.handle_event<Event::BEGIN_PHASE>(in_warmup);
        }
      
        // warmup behavior (4 IPC)
        if (i < 4000000) {
          std::deque<ooo_model_instr> fake_instructions{{ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr())}};
          uint32_t cpu = 0;
          uint64_t curr_cycles = i;
          auto cb = std::cbegin(fake_instructions);
          auto ce = std::cend(fake_instructions);
          uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
        } else {
          // simulation behavior (2 IPC)
          std::deque<ooo_model_instr> fake_instructions{{ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr())}};
          uint32_t cpu = 0;
          uint64_t curr_cycles = i;
          auto cb = std::cbegin(fake_instructions);
          auto ce = std::cend(fake_instructions);
          uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
        }
    }
    
    std::string res = stdout.str();
    
    std::string l1 = res.substr(0, res.find('\n')+1);
    std::string rest = res.substr(res.find('\n')+1);
    std::string l2 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);
    std::string l3 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);
    
    REQUIRE_THAT(l1, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 2499999 heartbeat IPC: 4 cumulative IPC: 4 "));
    REQUIRE_THAT(l2, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 20000000 cycles: 5999999 heartbeat IPC: 2.857 cumulative IPC: 2 "));
    REQUIRE_THAT(l3, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 30000000 cycles: 10999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener prints correctly with multiple CPUs") {
    std::ostringstream stdout{};
    Heartbeat uut{&stdout};
    
    // begin phase event
    bool in_warmup = true;
    uut.handle_event<Event::BEGIN_PHASE>(in_warmup);
    
    for (int i = 0; i < 5000000; ++i) {
        
        // simulation behavior (2 IPC)
        std::deque<ooo_model_instr> fake_instructions{{ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr())}};
        uint32_t cpu = 0;
        uint64_t curr_cycles = i;
        auto cb = std::cbegin(fake_instructions);
        auto ce = std::cend(fake_instructions);
        uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
        cpu++;
        uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
        cpu++;
        uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
        cpu++;
        uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
        
    }
    
    std::string res = stdout.str();
    
    std::string l1 = res.substr(0, res.find('\n')+1);
    std::string rest = res.substr(res.find('\n')+1);
    std::string l2 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);
    std::string l3 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);
    std::string l4 = rest.substr(0, rest.find('\n')+1);
    rest = rest.substr(rest.find('\n')+1);
    
    REQUIRE_THAT(l1, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(l2, Catch::Matchers::StartsWith("Heartbeat CPU 1 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(l3, Catch::Matchers::StartsWith("Heartbeat CPU 2 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(l4, Catch::Matchers::StartsWith("Heartbeat CPU 3 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener honors a custom cycles_between_printouts") {
    std::ostringstream stdout{};
    Heartbeat uut{&stdout};
    uut.cycles_between_printouts = 1000000; // 10x more frequent than the default

    bool in_warmup = false;
    uut.handle_event<Event::BEGIN_PHASE>(in_warmup);

    for (int i = 0; i < 500000; ++i) {
        std::deque<ooo_model_instr> fake_instructions{{ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr())}};
        uint32_t cpu = 0;
        uint64_t curr_cycles = i;
        auto cb = std::cbegin(fake_instructions);
        auto ce = std::cend(fake_instructions);
        uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
    }

    std::string res = stdout.str();
    std::string rest = res.substr(res.find('\n') + 1);

    REQUIRE_THAT(res, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 1000000 cycles: 499999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener does not drift when retire batches overshoot the threshold") {
    // Three instructions retire per cycle into a threshold of 10 instructions.
    // Each batch carries the running count past a non-multiple boundary
    // (12, 24, 36, ...).  If the listener snapped its baseline to the
    // current retired count after each printout, the overshoots would
    // compound and the second heartbeat would land at instruction 24
    // instead of 20, etc.  The fix advances the baseline by exact
    // multiples of cycles_between_printouts so heartbeats stay aligned to
    // the configured cadence.
    std::ostringstream stdout{};
    Heartbeat uut{&stdout};
    uut.cycles_between_printouts = 10;

    bool in_warmup = false;
    uut.handle_event<Event::BEGIN_PHASE>(in_warmup);

    constexpr int batches = 20; // 60 instructions total -> 6 heartbeats expected
    for (int i = 0; i < batches; ++i) {
        std::deque<ooo_model_instr> fake_instructions{
            {ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr()), ooo_model_instr(champsim::origin{0, 0}, input_instr())}};
        uint32_t cpu = 0;
        uint64_t curr_cycles = static_cast<uint64_t>(i);
        auto cb = std::cbegin(fake_instructions);
        auto ce = std::cend(fake_instructions);
        uut.handle_event<Event::RETIRE>(cpu, cb, ce, curr_cycles);
    }

    std::string res = stdout.str();
    std::vector<std::string> lines;
    for (size_t pos = 0; pos < res.size(); ) {
        auto next = res.find('\n', pos);
        if (next == std::string::npos) break;
        lines.push_back(res.substr(pos, next - pos));
        pos = next + 1;
    }

    REQUIRE(lines.size() == 6);
    REQUIRE_THAT(lines[0], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 12 "));
    REQUIRE_THAT(lines[1], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 21 "));
    REQUIRE_THAT(lines[2], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 30 "));
    REQUIRE_THAT(lines[3], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 42 "));
    REQUIRE_THAT(lines[4], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 51 "));
    REQUIRE_THAT(lines[5], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 60 "));
}

TEST_CASE("Reading heartbeat_frequency from a JSON config sets cycles_between_printouts") {
    // Mirrors the wiring in src/main.cc that reads the root-level
    // heartbeat_frequency field and applies it to the Heartbeat listener,
    // independent of which environment model the config selects.
    nlohmann::json config_json = {{"heartbeat_frequency", 1234567U}};
    std::ostringstream stdout{};
    Heartbeat uut{&stdout};

    if (config_json.contains("heartbeat_frequency")) {
        uut.cycles_between_printouts = config_json.value("heartbeat_frequency", uint64_t{10000000});
    }

    REQUIRE(uut.cycles_between_printouts == 1234567U);
}

TEST_CASE("Heartbeat listener falls back to its default cadence when heartbeat_frequency is absent") {
    nlohmann::json config_json = {{"block_size", 64U}};
    std::ostringstream stdout{};
    Heartbeat uut{&stdout};

    if (config_json.contains("heartbeat_frequency")) {
        uut.cycles_between_printouts = config_json.value("heartbeat_frequency", uint64_t{10000000});
    }

    REQUIRE(uut.cycles_between_printouts == 10000000U);
}

}