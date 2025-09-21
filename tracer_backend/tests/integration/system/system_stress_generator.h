#ifndef SYSTEM_STRESS_GENERATOR_H
#define SYSTEM_STRESS_GENERATOR_H

#include <atomic>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "system_perf_monitor.h"
#include "system_test_fixture.h"

struct stress_generator_config_t {
    uint32_t worker_threads{4};
    uint32_t burst_length{32};
    uint32_t syscalls_per_burst{4};
    bool chaos_mode{false};
};

struct stress_generator_t {
    test_fixture_t* fixture{nullptr};
    stress_generator_config_t config{};
    perf_monitor_t* monitor{nullptr};
    std::vector<std::thread> workers;
    std::atomic<bool> running{false};
    std::atomic<uint64_t> total_events{0};
    std::atomic<uint64_t> bursts_completed{0};
    std::atomic<uint64_t> chaos_operations{0};
};

bool stress_generator_start(stress_generator_t* generator, test_fixture_t* fixture,
                            const stress_generator_config_t& config, perf_monitor_t* monitor,
                            std::string* error_message);
void stress_generator_stop(stress_generator_t* generator);
uint64_t stress_generator_events(const stress_generator_t* generator);
uint64_t stress_generator_bursts(const stress_generator_t* generator);
uint64_t stress_generator_chaos_ops(const stress_generator_t* generator);

#endif  // SYSTEM_STRESS_GENERATOR_H
