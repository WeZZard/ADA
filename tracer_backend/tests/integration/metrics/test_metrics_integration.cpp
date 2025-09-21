#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/metrics/global_metrics.h>
#include <tracer_backend/metrics/metrics_reporter.h>
#include <tracer_backend/metrics/thread_metrics.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/tracer_types.h>
}

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "thread_metrics_private.h"
#include "thread_registry_private.h"

namespace {

struct CapturedReport {
    ada_metrics_report_kind_t kind;
    uint64_t timestamp_ns;
    ada_global_metrics_totals_t totals;
    ada_global_metrics_rates_t rates;
    std::vector<ada_thread_metrics_snapshot_t> snapshots;
};

struct ReporterObserver {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<CapturedReport> reports;
};

void capture_report_cb(const ada_metrics_report_view_t* view, void* user_data) {
    auto* observer = static_cast<ReporterObserver*>(user_data);
    if (!observer || !view) {
        return;
    }

    CapturedReport report{};
    report.kind = view->kind;
    report.timestamp_ns = view->timestamp_ns;
    report.totals = view->totals;
    report.rates = view->rates;
    report.snapshots.assign(view->snapshots, view->snapshots + view->snapshot_count);

    {
        std::lock_guard<std::mutex> lock(observer->mutex);
        observer->reports.push_back(std::move(report));
    }
    observer->cv.notify_all();
}

bool wait_for_reports(ReporterObserver& observer,
                      size_t expected,
                      std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(observer.mutex);
    return observer.cv.wait_for(lock, timeout, [&]() { return observer.reports.size() >= expected; });
}

TEST(MetricsTaskIntegration, GlobalMetricsCollect__observesQueueDepthAndSwapRates__thenAggregates) {
    constexpr uint32_t kCapacity = 1;
    size_t mem_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> storage(mem_size, 0);
    ThreadRegistry* registry =
        thread_registry_init_with_capacity(storage.data(), storage.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 0xABCu);
    ASSERT_NE(lanes, nullptr);

    ada_thread_metrics_t* metrics = thread_lanes_get_metrics(lanes);
    ASSERT_NE(metrics, nullptr);

    auto* cpp_lanes = ada::internal::to_cpp(lanes);

    std::array<ada_thread_metrics_snapshot_t, MAX_THREADS> buffer{};
    ada_global_metrics_t global{};
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    ADA_ATOMIC_STORE(metrics->counters.events_written, 40, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 4000, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.events_dropped, 5, ADA_MEMORY_ORDER_RELAXED);

    auto warm_token = ada_thread_metrics_swap_begin(metrics, 1'000'000u);
    ada_thread_metrics_swap_end(&warm_token, 1'000'200u, 2);

    cpp_lanes->index_lane.submit_head.store(10u, std::memory_order_release);
    cpp_lanes->index_lane.submit_tail.store(30u, std::memory_order_release);
    cpp_lanes->detail_lane.submit_head.store(0u, std::memory_order_release);
    cpp_lanes->detail_lane.submit_tail.store(5u, std::memory_order_release);

    uint64_t now1 = 1'500'000u;
    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, now1));
    EXPECT_EQ(ada_global_metrics_snapshot_count(&global), 1u);

    const ada_thread_metrics_snapshot_t* snaps = ada_global_metrics_snapshot_data(&global);
    ASSERT_NE(snaps, nullptr);
    EXPECT_EQ(snaps[0].max_queue_depth, 25u);
    EXPECT_DOUBLE_EQ(snaps[0].swaps_per_second, 0.0);

    ada_global_metrics_totals_t totals1 = ada_global_metrics_get_totals(&global);
    EXPECT_EQ(totals1.total_events_written, snaps[0].events_written);
    EXPECT_EQ(totals1.total_events_dropped, snaps[0].events_dropped);
    EXPECT_EQ(totals1.active_thread_count, 1u);

    ADA_ATOMIC_STORE(metrics->counters.events_written, 140, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 9400, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.events_dropped, 7, ADA_MEMORY_ORDER_RELAXED);

    auto second_token = ada_thread_metrics_swap_begin(metrics, now1 + 1000u);
    ada_thread_metrics_swap_end(&second_token, now1 + 1200u, 3);

    cpp_lanes->index_lane.submit_head.store(900u, std::memory_order_release);
    cpp_lanes->index_lane.submit_tail.store(100u, std::memory_order_release);
    cpp_lanes->detail_lane.submit_head.store(200u, std::memory_order_release);
    cpp_lanes->detail_lane.submit_tail.store(260u, std::memory_order_release);

    uint64_t now2 = now1 + ADA_METRICS_WINDOW_NS;
    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, now2));
    EXPECT_EQ(ada_global_metrics_snapshot_count(&global), 1u);

    snaps = ada_global_metrics_snapshot_data(&global);
    ASSERT_NE(snaps, nullptr);

    EXPECT_GT(snaps[0].events_per_second, 0.0);
    EXPECT_GT(snaps[0].bytes_per_second, 0.0);
    EXPECT_GT(snaps[0].swaps_per_second, 0.0);
    EXPECT_EQ(snaps[0].max_queue_depth, 284u);

    ada_global_metrics_totals_t totals2 = ada_global_metrics_get_totals(&global);
    EXPECT_EQ(totals2.total_events_written, snaps[0].events_written);
    EXPECT_EQ(totals2.total_bytes_written, snaps[0].bytes_written);
    EXPECT_EQ(totals2.total_events_dropped, snaps[0].events_dropped);

    ada_global_metrics_rates_t rates = ada_global_metrics_get_rates(&global);
    EXPECT_EQ(rates.system_events_per_second, snaps[0].events_per_second);
    EXPECT_EQ(rates.system_bytes_per_second, snaps[0].bytes_per_second);
    EXPECT_EQ(rates.last_window_ns, metrics->rate.window_duration_ns);

    thread_registry_deinit(registry);
}

TEST(MetricsTaskIntegration, MetricsReporter__managesRealtimeIntervals__thenEmitsSummary) {
    constexpr uint32_t kCapacity = 1;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 0x1234u);
    ASSERT_NE(lanes, nullptr);
    ada_thread_metrics_t* metrics = thread_lanes_get_metrics(lanes);
    ASSERT_NE(metrics, nullptr);

    ADA_ATOMIC_STORE(metrics->counters.events_written, 5, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 500, ADA_MEMORY_ORDER_RELAXED);

    ReporterObserver observer;
    FILE* text_output = tmpfile();
    ASSERT_NE(text_output, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 125;
    config.start_paused = false;
    config.output_stream = text_output;
    config.snapshot_capacity = 2;
    config.sink = &capture_report_cb;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);
    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    ASSERT_TRUE(ada_metrics_reporter_force_report(reporter));
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(400)));

    ADA_ATOMIC_STORE(metrics->counters.events_written, 25, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 2500, ADA_MEMORY_ORDER_RELAXED);

    ASSERT_TRUE(wait_for_reports(observer, 2u, std::chrono::milliseconds(1200)));
    ASSERT_TRUE(wait_for_reports(observer, 3u, std::chrono::milliseconds(1200)));

    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_GE(observer.reports.size(), 3u);
        EXPECT_EQ(observer.reports[0].kind, ADA_METRICS_REPORT_KIND_FORCED);
        EXPECT_EQ(observer.reports[1].kind, ADA_METRICS_REPORT_KIND_PERIODIC);
        EXPECT_EQ(observer.reports[2].kind, ADA_METRICS_REPORT_KIND_PERIODIC);
        EXPECT_LT(observer.reports[1].timestamp_ns, observer.reports[2].timestamp_ns);
    }

    ADA_ATOMIC_STORE(metrics->counters.events_written, 50, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 5000, ADA_MEMORY_ORDER_RELAXED);

    ada_metrics_reporter_stop(reporter);
    ASSERT_TRUE(wait_for_reports(observer, 4u, std::chrono::milliseconds(1200)));

    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_EQ(observer.reports.back().kind, ADA_METRICS_REPORT_KIND_SUMMARY);
        EXPECT_EQ(observer.reports.back().totals.total_events_written, 50u);
    }

    ada_metrics_reporter_destroy(reporter);
    fclose(text_output);
    thread_registry_deinit(registry);
}

// Test callback null handling (covers lines 41-42)
TEST(MetricsIntegration, CallbackNullHandling) {
    // Test with null observer
    capture_report_cb(nullptr, nullptr);

    // Test with null view
    ReporterObserver observer;
    capture_report_cb(nullptr, &observer);

    // Test with null user_data but valid view
    ada_metrics_report_view_t view = {};
    capture_report_cb(&view, nullptr);

    // All should handle nulls gracefully without crash
}

} // namespace
