#include <catch.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "events.h"
#include "listeners/heartbeat.h"
#include "packet_consumer.h"

namespace
{

// A minimal packet_consumer that reports core-style heartbeat lines.
struct hb_consumer : champsim::modules::packet_consumer {
  explicit hb_consumer(int id) { set_consumer_id(id); }
  std::string progress_message(uint64_t total_progress, uint64_t total_cycles, double interval_rate, double cumulative_rate) const override
  {
    return fmt::format("Heartbeat CPU {} instructions: {} cycles: {} heartbeat IPC: {:.4} cumulative IPC: {:.4}", consumer_id(), total_progress, total_cycles,
                       interval_rate, cumulative_rate);
  }
};

// Drive the generic PROGRESS event. The consumer is upcast to packet_consumer& so the
// handler's const packet_consumer& specialization is selected (not the generic no-op).
void emit_progress(Heartbeat& hb, const hb_consumer& c, uint64_t total, uint64_t cycles)
{
  const champsim::modules::packet_consumer& base = c;
  hb.handle_event<Event::PROGRESS>(base, total, cycles);
}

void emit_begin_phase(Heartbeat& hb, bool is_warmup) { hb.handle_event<Event::BEGIN_PHASE>(is_warmup); }

} // namespace

TEST_CASE("The heartbeat prints one line after 10M packets of progress") {
    std::ostringstream capture{};
    Heartbeat hb{&capture};
    hb.cycles_between_printouts = 10000000ULL;
    hb_consumer cpu0{0};

    emit_begin_phase(hb, false);

    uint64_t total = 0;
    for (int i = 0; i < 5000000; ++i) {
        total += 2;
        emit_progress(hb, cpu0, total, static_cast<uint64_t>(i));
    }

    std::string res = capture.str();
    std::string rest = res.substr(res.find('\n')+1);

    REQUIRE_THAT(res, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat prints cumulative and heartbeat IPC correctly after a phase change") {
    std::ostringstream capture{};
    Heartbeat hb{&capture};
    hb.cycles_between_printouts = 10000000ULL;
    hb_consumer cpu0{0};

    emit_begin_phase(hb, true);

    uint64_t total = 0;
    for (int i = 0; i < 11000000; ++i) {
        if (i == 5000000) {
          emit_begin_phase(hb, false);
        }

        if (i < 4000000) {
          total += 4;
        } else {
          total += 2;
        }
        emit_progress(hb, cpu0, total, static_cast<uint64_t>(i));
    }

    std::string res = capture.str();
    std::vector<std::string> lines;
    std::istringstream stream{res};
    for (std::string line; std::getline(stream, line);) lines.push_back(line);

    REQUIRE(lines.size() == 3);
    REQUIRE_THAT(lines[0], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 2499999 heartbeat IPC: 4 cumulative IPC: 4 "));
    REQUIRE_THAT(lines[1], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 20000000 cycles: 5999999 heartbeat IPC: 2.857 cumulative IPC: 2 "));
    REQUIRE_THAT(lines[2], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 30000000 cycles: 10999999 heartbeat IPC: 2 cumulative IPC: 2 "));
}

TEST_CASE("The heartbeat tracks each consumer independently") {
    std::ostringstream capture{};
    Heartbeat hb{&capture};
    hb.cycles_between_printouts = 10000000ULL;
    std::vector<hb_consumer> cpus{hb_consumer{0}, hb_consumer{1}, hb_consumer{2}, hb_consumer{3}};

    emit_begin_phase(hb, false);

    std::vector<uint64_t> totals(4, 0);
    for (int i = 0; i < 5000000; ++i) {
        for (int c = 0; c < 4; ++c) {
            totals[static_cast<std::size_t>(c)] += 2;
            emit_progress(hb, cpus[static_cast<std::size_t>(c)], totals[static_cast<std::size_t>(c)], static_cast<uint64_t>(i));
        }
    }

    std::string res = capture.str();
    std::vector<std::string> lines;
    std::istringstream stream{res};
    for (std::string line; std::getline(stream, line);) lines.push_back(line);

    REQUIRE(lines.size() == 4);
    REQUIRE_THAT(lines[0], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(lines[1], Catch::Matchers::StartsWith("Heartbeat CPU 1 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(lines[2], Catch::Matchers::StartsWith("Heartbeat CPU 2 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE_THAT(lines[3], Catch::Matchers::StartsWith("Heartbeat CPU 3 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
}

TEST_CASE("The heartbeat honors a configured interval") {
    std::ostringstream capture{};
    Heartbeat hb{&capture};
    hb.cycles_between_printouts = 1000000ULL;
    hb_consumer cpu0{0};

    emit_begin_phase(hb, false);

    uint64_t total = 0;
    for (int i = 0; i < 500000; ++i) {
        total += 2;
        emit_progress(hb, cpu0, total, static_cast<uint64_t>(i));
    }

    std::string res = capture.str();
    REQUIRE_THAT(res, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 1000000 cycles: 499999 heartbeat IPC: 2 cumulative IPC: 2 "));
}

TEST_CASE("The heartbeat baseline advances by whole intervals when progress overshoots") {
    std::ostringstream capture{};
    Heartbeat hb{&capture};
    hb.cycles_between_printouts = 9ULL;
    hb_consumer cpu0{0};

    emit_begin_phase(hb, false);

    uint64_t total = 0;
    for (int i = 0; i < 14; ++i) {
        total += 4;
        emit_progress(hb, cpu0, total, static_cast<uint64_t>(i));
    }

    std::string res = capture.str();
    std::vector<std::string> lines;
    std::istringstream stream{res};
    for (std::string line; std::getline(stream, line);) lines.push_back(line);

    REQUIRE(lines.size() == 6);
    REQUIRE_THAT(lines[0], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 12 "));
    REQUIRE_THAT(lines[1], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 20 "));
    REQUIRE_THAT(lines[2], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 28 "));
    REQUIRE_THAT(lines[3], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 36 "));
    REQUIRE_THAT(lines[4], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 48 "));
    REQUIRE_THAT(lines[5], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 56 "));
}

TEST_CASE("The default progress message is packet-generic") {
    champsim::modules::packet_consumer generic{};
    auto msg = generic.progress_message(100, 50, 2.0, 2.0);
    REQUIRE_THAT(msg, Catch::Matchers::StartsWith("Heartbeat consumer 0 progress: 100 cycles: 50 "));
}
