#include <catch.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "modules.h"

namespace
{

// A minimal consumer that reports core-style heartbeat lines.
struct hb_consumer : champsim::modules::source_consumer {
  int id_;
  explicit hb_consumer(int id) : id_(id) {}
  int consumer_id() const override { return id_; }
  std::string progress_message(uint64_t total_progress, uint64_t total_cycles, double interval_rate, double cumulative_rate) const override
  {
    return fmt::format("Heartbeat CPU {} instructions: {} cycles: {} heartbeat IPC: {:.4} cumulative IPC: {:.4}", id_, total_progress, total_cycles,
                       interval_rate, cumulative_rate);
  }
};

// A registered do-nothing environment to parent the listener module.
struct hb_env : champsim::modules::environment_module {
  explicit hb_env(champsim::modules::ModuleBuilder) {}
  std::vector<std::any> view(const std::string&) const override { return {}; }
  const champsim::modules::ModuleBuilder get_builder_params(const std::string&) const override { return champsim::modules::ModuleBuilder(); }
};
static champsim::modules::environment_module::register_module<hb_env> hb_env_reg("HB_ENV_020");

// Create a HEARTBEAT listener writing into the given stream.
champsim::modules::listener* make_heartbeat(const std::string& name, std::ostream* out, uint64_t interval)
{
  auto env_builder = champsim::modules::ModuleBuilder(name + "_env", "HB_ENV_020");
  auto* env = champsim::modules::environment_module::create_instance(env_builder, static_cast<champsim::modules::environment_module*>(nullptr));

  auto builder = champsim::modules::ModuleBuilder(name, "HEARTBEAT")
    .add_parameter("interval", interval)
    .add_parameter("output_stream", out);
  return champsim::modules::listener::create_instance(builder, env);
}

} // namespace

TEST_CASE("The heartbeat listener prints one line after 10M instructions retired") {
    std::ostringstream capture{};
    auto* uut = make_heartbeat("hb020_one", &capture, 10000000ULL);
    hb_consumer cpu0{0};

    uut->begin_phase(false);

    uint64_t total = 0;
    for (int i = 0; i < 5000000; ++i) {
        total += 2;
        uut->progress(cpu0, total, static_cast<uint64_t>(i));
    }

    std::string res = capture.str();
    std::string rest = res.substr(res.find('\n')+1);

    REQUIRE_THAT(res, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 4999999 heartbeat IPC: 2 cumulative IPC: 2 "));
    REQUIRE(rest.length() < 2);
}

TEST_CASE("The heartbeat listener prints cumulative and heartbeat IPC correctly after a phase change") {
    std::ostringstream capture{};
    auto* uut = make_heartbeat("hb020_phase", &capture, 10000000ULL);
    hb_consumer cpu0{0};

    uut->begin_phase(true);

    uint64_t total = 0;
    for (int i = 0; i < 11000000; ++i) {
        // phase change to simulation
        if (i == 5000000) {
          uut->begin_phase(false);
        }

        // warmup behavior (4 IPC), then simulation behavior (2 IPC)
        if (i < 4000000) {
          total += 4;
        } else {
          total += 2;
        }
        uut->progress(cpu0, total, static_cast<uint64_t>(i));
    }

    std::string res = capture.str();
    std::vector<std::string> lines;
    std::istringstream stream{res};
    for (std::string line; std::getline(stream, line);) lines.push_back(line);

    REQUIRE(lines.size() == 3);
    // 10M instructions reached at i = 2499999 (4/cycle)
    REQUIRE_THAT(lines[0], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 10000000 cycles: 2499999 heartbeat IPC: 4 cumulative IPC: 4 "));
    // 20M at i = 5999999: interval mixes 4 IPC and 2 IPC regions
    REQUIRE_THAT(lines[1], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 20000000 cycles: 5999999 heartbeat IPC: 2.857 cumulative IPC: 2 "));
    // 30M at i = 10999999: fully in the 2 IPC region, phase-cumulative also 2
    REQUIRE_THAT(lines[2], Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 30000000 cycles: 10999999 heartbeat IPC: 2 cumulative IPC: 2 "));
}

TEST_CASE("The heartbeat listener tracks each source independently") {
    std::ostringstream capture{};
    auto* uut = make_heartbeat("hb020_multi", &capture, 10000000ULL);
    std::vector<hb_consumer> cpus{hb_consumer{0}, hb_consumer{1}, hb_consumer{2}, hb_consumer{3}};

    uut->begin_phase(false);

    std::vector<uint64_t> totals(4, 0);
    for (int i = 0; i < 5000000; ++i) {
        for (int c = 0; c < 4; ++c) {
            totals[static_cast<std::size_t>(c)] += 2;
            uut->progress(cpus[static_cast<std::size_t>(c)], totals[static_cast<std::size_t>(c)], static_cast<uint64_t>(i));
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

TEST_CASE("The heartbeat listener honors a configured interval") {
    std::ostringstream capture{};
    auto* uut = make_heartbeat("hb020_interval", &capture, 1000000ULL);
    hb_consumer cpu0{0};

    uut->begin_phase(false);

    uint64_t total = 0;
    for (int i = 0; i < 500000; ++i) {
        total += 2;
        uut->progress(cpu0, total, static_cast<uint64_t>(i));
    }

    std::string res = capture.str();
    REQUIRE_THAT(res, Catch::Matchers::StartsWith("Heartbeat CPU 0 instructions: 1000000 cycles: 499999 heartbeat IPC: 2 cumulative IPC: 2 "));
}

TEST_CASE("The heartbeat baseline advances by whole intervals when retires overshoot") {
    std::ostringstream capture{};
    auto* uut = make_heartbeat("hb020_overshoot", &capture, 9ULL);
    hb_consumer cpu0{0};

    uut->begin_phase(false);

    // Batches of 4 against a 9-token interval: the baseline advances by
    // whole intervals (9, 18, 27, 36, 45, 54, ...) rather than snapping to
    // the printed total, so prints land at the first batch crossing each
    // grid line: totals 12, 20, 28, 36, 48, 56.
    uint64_t total = 0;
    for (int i = 0; i < 14; ++i) {
        total += 4;
        uut->progress(cpu0, total, static_cast<uint64_t>(i));
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

TEST_CASE("The default progress message is token-generic") {
    champsim::modules::source_consumer generic{};
    // Untracked consumers (source_id -1) still format; listeners skip them upstream
    auto msg = generic.progress_message(100, 50, 2.0, 2.0);
    REQUIRE_THAT(msg, Catch::Matchers::StartsWith("Heartbeat source -1 tokens: 100 cycles: 50 "));
}
