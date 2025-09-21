#include "system_perf_monitor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr uint64_t kMinLatencyNs = 1;
constexpr uint64_t kMaxLatencyBin = 63;

uint64_t clamp_bin_index(uint64_t value) {
    if (value == 0) {
        return 0;
    }
    uint64_t bin = static_cast<uint64_t>(std::log2(static_cast<double>(value)));
    return std::min(bin, kMaxLatencyBin);
}

}  // namespace

void perf_monitor_init(perf_monitor_t* monitor) {
    if (!monitor) {
        return;
    }
    monitor->running.store(false, std::memory_order_relaxed);
    monitor->total_events.store(0, std::memory_order_relaxed);
    monitor->total_bytes.store(0, std::memory_order_relaxed);
    monitor->current_memory_bytes.store(0, std::memory_order_relaxed);
    monitor->peak_memory_bytes.store(0, std::memory_order_relaxed);
    for (auto& bin : monitor->histogram_bins) {
        bin.store(0, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock(monitor->latencies_mutex);
        monitor->latencies_ns.clear();
    }
}

void perf_monitor_start(perf_monitor_t* monitor) {
    if (!monitor) {
        return;
    }
    monitor->start_time = std::chrono::steady_clock::now();
    monitor->running.store(true, std::memory_order_relaxed);
}

void perf_monitor_stop(perf_monitor_t* monitor) {
    if (!monitor) {
        return;
    }
    monitor->end_time = std::chrono::steady_clock::now();
    monitor->running.store(false, std::memory_order_relaxed);
}

void perf_monitor_record(perf_monitor_t* monitor, uint64_t events, uint64_t latency_ns,
                         size_t bytes_written) {
    if (!monitor) {
        return;
    }
    monitor->total_events.fetch_add(events, std::memory_order_relaxed);
    monitor->total_bytes.fetch_add(bytes_written, std::memory_order_relaxed);

    if (latency_ns == 0) {
        latency_ns = kMinLatencyNs;
    }

    uint64_t bin_index = clamp_bin_index(latency_ns);
    monitor->histogram_bins[bin_index].fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(monitor->latencies_mutex);
        monitor->latencies_ns.push_back(latency_ns);
    }
}

void perf_monitor_track_memory(perf_monitor_t* monitor, size_t bytes) {
    if (!monitor) {
        return;
    }
    size_t current = monitor->current_memory_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    size_t prev_peak = monitor->peak_memory_bytes.load(std::memory_order_relaxed);
    while (current > prev_peak &&
           !monitor->peak_memory_bytes.compare_exchange_weak(prev_peak, current, std::memory_order_relaxed,
                                                             std::memory_order_relaxed)) {
        // retry until peak updated
    }
}

void perf_monitor_release_memory(perf_monitor_t* monitor, size_t bytes) {
    if (!monitor) {
        return;
    }
    size_t current = monitor->current_memory_bytes.load(std::memory_order_relaxed);
    if (bytes > current) {
        monitor->current_memory_bytes.store(0, std::memory_order_relaxed);
    } else {
        monitor->current_memory_bytes.fetch_sub(bytes, std::memory_order_relaxed);
    }
}

static std::vector<uint64_t> collect_latencies(const perf_monitor_t* monitor) {
    std::vector<uint64_t> copy;
    if (!monitor) {
        return copy;
    }
    std::lock_guard<std::mutex> lock(monitor->latencies_mutex);
    copy = monitor->latencies_ns;
    return copy;
}

uint64_t perf_monitor_percentile(const perf_monitor_t* monitor, double percentile) {
    auto latencies = collect_latencies(monitor);
    if (latencies.empty()) {
        return 0;
    }
    std::sort(latencies.begin(), latencies.end());
    double rank = percentile * (latencies.size() - 1);
    size_t idx = static_cast<size_t>(std::round(rank));
    if (idx >= latencies.size()) {
        idx = latencies.size() - 1;
    }
    return latencies[idx];
}

perf_monitor_snapshot_t perf_monitor_snapshot(const perf_monitor_t* monitor) {
    perf_monitor_snapshot_t snapshot{};
    if (!monitor) {
        return snapshot;
    }

    auto end_time = monitor->running.load(std::memory_order_relaxed)
                        ? std::chrono::steady_clock::now()
                        : monitor->end_time;
    auto duration = end_time - monitor->start_time;
    double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(duration).count();
    if (seconds <= 0.0) {
        seconds = 1e-9;  // avoid division by zero, treat as tiny interval
    }

    snapshot.total_events = monitor->total_events.load(std::memory_order_relaxed);
    snapshot.total_bytes = monitor->total_bytes.load(std::memory_order_relaxed);
    snapshot.throughput_events_per_sec = snapshot.total_events / seconds;
    snapshot.throughput_bytes_per_sec = snapshot.total_bytes / seconds;
    snapshot.peak_memory_bytes = monitor->peak_memory_bytes.load(std::memory_order_relaxed);
    snapshot.p50_latency_ns = perf_monitor_percentile(monitor, 0.50);
    snapshot.p99_latency_ns = perf_monitor_percentile(monitor, 0.99);
    return snapshot;
}

std::vector<std::pair<uint64_t, uint64_t>> perf_monitor_histogram(const perf_monitor_t* monitor) {
    std::vector<std::pair<uint64_t, uint64_t>> histogram;
    if (!monitor) {
        return histogram;
    }
    histogram.reserve(monitor->histogram_bins.size());
    for (size_t i = 0; i < monitor->histogram_bins.size(); ++i) {
        uint64_t upper_ns = (i == 0) ? 1 : (1ULL << i);
        histogram.emplace_back(upper_ns,
                               monitor->histogram_bins[i].load(std::memory_order_relaxed));
    }
    return histogram;
}

