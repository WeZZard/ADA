#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

extern "C" {
#include "ada_paths.h"
}

#include "system_perf_monitor.h"
#include "system_stress_generator.h"
#include "system_test_fixture.h"
#include "system_trace_constants.h"
#include "system_validator.h"

namespace {

std::string target_test_cli_path() {
    std::filesystem::path path = std::filesystem::path(ADA_WORKSPACE_ROOT) /
                                 "target" /
                                 ADA_BUILD_PROFILE /
                                 "tracer_backend" /
                                 "test" /
                                 "test_cli";
    return path.string();
}

bool ensure_executable(const std::string& path, std::string* details) {
    if (access(path.c_str(), X_OK) == 0) {
        return true;
    }
    if (details) {
        *details = "executable not found: " + path + ". build it with `cargo build`";
    }
    return false;
}

void stop_generator(stress_generator_t* generator) {
    if (generator) {
        stress_generator_stop(generator);
    }
}

}  // namespace

TEST(SystemValidator, thread_isolation_ignores_reserved_lifecycle_thread) {
    validator_t validator{};
    validator.events.push_back({5u, kTraceLifecycleThreadId, 100u, 0});
    validator.events.push_back({3u, kTraceLifecycleThreadId, 200u, 0});
    validator.events.push_back({10u, 7, 300u, 0});
    validator.events.push_back({11u, 7, 400u, 0});

    std::string details;
    EXPECT_TRUE(validator_verify_thread_isolation(&validator, &details)) << details;
    EXPECT_EQ(details, "thread isolation checks passed");
}

TEST(SystemValidator, thread_isolation_detects_real_thread_regression) {
    validator_t validator{};
    validator.events.push_back({1u, 42, 100u, 0});
    validator.events.push_back({2u, 42, 200u, 0});
    validator.events.push_back({0u, kTraceLifecycleThreadId, 250u, 0});
    validator.events.push_back({1u, 42, 300u, 0});

    std::string details;
    EXPECT_FALSE(validator_verify_thread_isolation(&validator, &details));
    EXPECT_THAT(details, testing::HasSubstr("thread 42"));
}

TEST(SystemIntegration, spawn_mode__burst_pipeline__then_validator_passes) {
    std::string path = target_test_cli_path();
    std::string skip_reason;
    if (!ensure_executable(path, &skip_reason)) {
        GTEST_SKIP() << skip_reason;
    }

    test_fixture_options_t options;
    options.mode = TEST_FIXTURE_MODE_SPAWN;
    options.registry_capacity = 32;
    options.enable_manifest = false;

    test_fixture_t fixture{};
    std::string error;
    ASSERT_TRUE(test_fixture_init(&fixture, options, &error)) << error;

    perf_monitor_t monitor;
    perf_monitor_init(&monitor);
    perf_monitor_track_memory(&monitor, test_fixture_registry_bytes(&fixture));
    perf_monitor_start(&monitor);

    std::vector<std::string> args{"--brief"};
    if (!test_fixture_launch_target(&fixture, path, args, &error)) {
        test_fixture_shutdown(&fixture);
        GTEST_SKIP() << error;
    }

    stress_generator_config_t config;
    config.worker_threads = 4;
    config.burst_length = 24;
    config.syscalls_per_burst = 4;
    config.chaos_mode = false;

    stress_generator_t generator{};
    ASSERT_TRUE(stress_generator_start(&generator, &fixture, config, &monitor, &error)) << error;
    std::unique_ptr<stress_generator_t, decltype(&stop_generator)> guard(&generator, stop_generator);

    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    stress_generator_stop(&generator);
    guard.release();
    perf_monitor_stop(&monitor);
    perf_monitor_release_memory(&monitor, test_fixture_registry_bytes(&fixture));

    test_fixture_shutdown(&fixture);

    validator_t validator;
    ASSERT_TRUE(validator_load(&validator, test_fixture_events_path(&fixture), &error)) << error;
    ASSERT_GT(validator_total_events(&validator), 0u);

    std::string details;
    EXPECT_TRUE(validator_verify_thread_isolation(&validator, &details)) << details;
    EXPECT_TRUE(validator_verify_temporal_order(&validator, &details)) << details;

    auto snapshot = perf_monitor_snapshot(&monitor);
    EXPECT_GT(snapshot.total_events, 0u);
    EXPECT_GT(snapshot.throughput_events_per_sec, 0.0);
    EXPECT_GE(snapshot.p99_latency_ns, snapshot.p50_latency_ns);
    EXPECT_GT(stress_generator_bursts(&generator), 0u);
}

TEST(SystemIntegration, attach_mode__stress_pipeline__then_counts_match_validator) {
    std::string path = target_test_cli_path();
    std::string skip_reason;
    if (!ensure_executable(path, &skip_reason)) {
        GTEST_SKIP() << skip_reason;
    }

    test_fixture_options_t options;
    options.mode = TEST_FIXTURE_MODE_ATTACH;
    options.registry_capacity = 48;
    options.enable_manifest = false;

    test_fixture_t fixture{};
    std::string error;
    ASSERT_TRUE(test_fixture_init(&fixture, options, &error)) << error;

    perf_monitor_t monitor;
    perf_monitor_init(&monitor);
    perf_monitor_track_memory(&monitor, test_fixture_registry_bytes(&fixture));
    perf_monitor_start(&monitor);

    std::vector<std::string> args{"--wait"};
    if (!test_fixture_launch_target(&fixture, path, args, &error)) {
        test_fixture_shutdown(&fixture);
        GTEST_SKIP() << error;
    }

    ASSERT_TRUE(test_fixture_attach_to_pid(&fixture, test_fixture_pid(&fixture), &error)) << error;

    stress_generator_config_t config;
    config.worker_threads = 6;
    config.burst_length = 30;
    config.syscalls_per_burst = 3;
    config.chaos_mode = false;

    stress_generator_t generator{};
    ASSERT_TRUE(stress_generator_start(&generator, &fixture, config, &monitor, &error)) << error;
    std::unique_ptr<stress_generator_t, decltype(&stop_generator)> guard(&generator, stop_generator);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    stress_generator_stop(&generator);
    guard.release();
    perf_monitor_stop(&monitor);
    perf_monitor_release_memory(&monitor, test_fixture_registry_bytes(&fixture));

    test_fixture_shutdown(&fixture);

    validator_t validator;
    ASSERT_TRUE(validator_load(&validator, test_fixture_events_path(&fixture), &error)) << error;
    ASSERT_GT(validator_total_events(&validator), 0u);
    EXPECT_GE(stress_generator_events(&generator), validator_total_events(&validator));

    std::string details;
    EXPECT_TRUE(validator_verify_thread_isolation(&validator, &details)) << details;
    EXPECT_TRUE(validator_verify_temporal_order(&validator, &details)) << details;

    auto snapshot = perf_monitor_snapshot(&monitor);
    EXPECT_GT(snapshot.total_events, 0u);
    EXPECT_GT(snapshot.throughput_events_per_sec, 0.0);
}

TEST(SystemIntegration, chaos_mode__sustained_pressure__then_temporal_consistency) {
    std::string path = target_test_cli_path();
    std::string skip_reason;
    if (!ensure_executable(path, &skip_reason)) {
        GTEST_SKIP() << skip_reason;
    }

    test_fixture_options_t options;
    options.mode = TEST_FIXTURE_MODE_SPAWN;
    options.registry_capacity = 64;
    options.enable_manifest = false;

    test_fixture_t fixture{};
    std::string error;
    ASSERT_TRUE(test_fixture_init(&fixture, options, &error)) << error;

    perf_monitor_t monitor;
    perf_monitor_init(&monitor);
    perf_monitor_track_memory(&monitor, test_fixture_registry_bytes(&fixture));
    perf_monitor_start(&monitor);

    std::vector<std::string> args{"--brief"};
    if (!test_fixture_launch_target(&fixture, path, args, &error)) {
        test_fixture_shutdown(&fixture);
        GTEST_SKIP() << error;
    }

    stress_generator_config_t config;
    config.worker_threads = 8;
    config.burst_length = 20;
    config.syscalls_per_burst = 5;
    config.chaos_mode = true;

    stress_generator_t generator{};
    ASSERT_TRUE(stress_generator_start(&generator, &fixture, config, &monitor, &error)) << error;
    std::unique_ptr<stress_generator_t, decltype(&stop_generator)> guard(&generator, stop_generator);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    stress_generator_stop(&generator);
    guard.release();
    perf_monitor_stop(&monitor);
    perf_monitor_release_memory(&monitor, test_fixture_registry_bytes(&fixture));

    test_fixture_shutdown(&fixture);

    validator_t validator;
    ASSERT_TRUE(validator_load(&validator, test_fixture_events_path(&fixture), &error)) << error;

    std::string details;
    EXPECT_TRUE(validator_verify_temporal_order(&validator, &details)) << details;
    EXPECT_TRUE(validator_verify_thread_isolation(&validator, &details)) << details;
    EXPECT_GT(validator_total_events(&validator), 0u);

    EXPECT_GT(stress_generator_events(&generator), 0u);
    EXPECT_GT(stress_generator_chaos_ops(&generator), 0u);

    auto snapshot = perf_monitor_snapshot(&monitor);
    EXPECT_GT(snapshot.total_events, 0u);
    EXPECT_GE(snapshot.p99_latency_ns, snapshot.p50_latency_ns);
    EXPECT_GT(snapshot.peak_memory_bytes, 0u);
}
