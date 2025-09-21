#ifndef SYSTEM_PERF_MONITOR_H
#define SYSTEM_PERF_MONITOR_H

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

struct perf_monitor_snapshot_t {
    double throughput_events_per_sec{0.0};
    double throughput_bytes_per_sec{0.0};
    uint64_t total_events{0};
    uint64_t total_bytes{0};
    uint64_t p50_latency_ns{0};
    uint64_t p99_latency_ns{0};
    size_t peak_memory_bytes{0};
};

struct perf_monitor_t {
    std::chrono::steady_clock::time_point start_time{};
    std::chrono::steady_clock::time_point end_time{};
    std::atomic<bool> running{false};
    std::atomic<uint64_t> total_events{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<size_t> current_memory_bytes{0};
    std::atomic<size_t> peak_memory_bytes{0};
    std::array<std::atomic<uint64_t>, 64> histogram_bins{};
    mutable std::mutex latencies_mutex;
    mutable std::vector<uint64_t> latencies_ns;
};

void perf_monitor_init(perf_monitor_t* monitor);
void perf_monitor_start(perf_monitor_t* monitor);
void perf_monitor_stop(perf_monitor_t* monitor);
void perf_monitor_record(perf_monitor_t* monitor, uint64_t events, uint64_t latency_ns,
                         size_t bytes_written);
void perf_monitor_track_memory(perf_monitor_t* monitor, size_t bytes);
void perf_monitor_release_memory(perf_monitor_t* monitor, size_t bytes);
perf_monitor_snapshot_t perf_monitor_snapshot(const perf_monitor_t* monitor);
uint64_t perf_monitor_percentile(const perf_monitor_t* monitor, double percentile);
std::vector<std::pair<uint64_t, uint64_t>> perf_monitor_histogram(
    const perf_monitor_t* monitor);

#endif  // SYSTEM_PERF_MONITOR_H
