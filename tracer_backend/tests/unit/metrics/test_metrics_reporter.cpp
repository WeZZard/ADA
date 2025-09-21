#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/metrics/metrics_reporter.h>
#include <tracer_backend/metrics/formatter.h>
#include <tracer_backend/metrics/thread_metrics.h>
#include <tracer_backend/utils/thread_registry.h>
}

#include <pthread.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <ctime>

namespace {

struct CapturedReport {
    ada_metrics_report_kind_t kind;
    uint64_t timestamp_ns;
    ada_global_metrics_totals_t totals;
    ada_global_metrics_rates_t rates;
    std::vector<ada_thread_metrics_snapshot_t> snapshots;
};

struct ReportObserver {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<CapturedReport> reports;
};

struct ReporterStateAccessor {
    ThreadRegistry* registry;
    ada_global_metrics_t global;
    std::vector<ada_thread_metrics_snapshot_t> snapshots;
    pthread_t thread;
    bool thread_started;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    std::atomic<bool> shutdown;
    bool running;
    bool paused;
    bool force_requested;
    uint64_t interval_ms;
    FILE* output_stream;
    std::string json_path;
    ada_metrics_report_sink_fn sink;
    void* sink_user_data;
    bool summary_emitted;
    bool cond_is_monotonic;
};

static ReporterStateAccessor* access_state(struct ada_metrics_reporter* reporter) {
    return reinterpret_cast<ReporterStateAccessor*>(reporter);
}

void capture_report(const ada_metrics_report_view_t* view, void* user_data) {
    auto* observer = static_cast<ReportObserver*>(user_data);
    if (!observer || !view) {
        return;
    }

    CapturedReport entry{};
    entry.kind = view->kind;
    entry.timestamp_ns = view->timestamp_ns;
    entry.totals = view->totals;
    entry.rates = view->rates;
    entry.snapshots.resize(view->snapshot_count);
    for (size_t i = 0; i < view->snapshot_count; ++i) {
        entry.snapshots[i] = view->snapshots[i];
    }

    {
        std::lock_guard<std::mutex> lock(observer->mutex);
        observer->reports.push_back(std::move(entry));
    }
    observer->cv.notify_all();
}

bool wait_for_reports(ReportObserver& observer, size_t expected, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(observer.mutex);
    return observer.cv.wait_for(lock, timeout, [&]() { return observer.reports.size() >= expected; });
}

#ifdef ADA_TESTING
struct TimedWaitCapture {
    std::mutex mutex;
    std::condition_variable cv;
    struct timespec last_ts {0, 0};
    int last_mode = -1;
    bool invoked = false;

    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        invoked = false;
        last_mode = -1;
        last_ts = timespec{0, 0};
    }

    void record(const struct timespec* ts, int mode) {
        std::lock_guard<std::mutex> lock(mutex);
        last_ts = ts ? *ts : timespec{0, 0};
        last_mode = mode;
        invoked = true;
        cv.notify_all();
    }

    bool wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [&]() { return invoked; });
    }
};

TimedWaitCapture g_timedwait_capture;

void timedwait_hook_adapter(const struct timespec* ts, int using_monotonic) {
    g_timedwait_capture.record(ts, using_monotonic);
}
#endif

TEST(MetricsFormatter, NullInputsReturnFalse) {
    ada_metrics_report_view_t view{};
    FILE* stream = tmpfile();
    ASSERT_NE(stream, nullptr);

    EXPECT_FALSE(ada_metrics_formatter_write_text(nullptr, stream));
    EXPECT_FALSE(ada_metrics_formatter_write_json(nullptr, stream));
    EXPECT_FALSE(ada_metrics_formatter_write_text(&view, nullptr));
    EXPECT_FALSE(ada_metrics_formatter_write_json(&view, nullptr));

    fclose(stream);
}

TEST(MetricsFormatter, UnknownKindAndEmptySnapshots) {
    ada_metrics_report_view_t view{};
    view.kind = static_cast<ada_metrics_report_kind_t>(999);
    view.timestamp_ns = 777u;
    view.snapshots = nullptr;
    view.snapshot_count = 0u;

    char* buffer = nullptr;
    size_t size = 0u;
    FILE* text_stream = open_memstream(&buffer, &size);
    ASSERT_NE(text_stream, nullptr);
    ASSERT_TRUE(ada_metrics_formatter_write_text(&view, text_stream));
    fclose(text_stream);
    std::string text_output(buffer, size);
    free(buffer);
    EXPECT_NE(text_output.find("[metrics][unknown]"), std::string::npos);
    EXPECT_NE(text_output.find("active_threads=0"), std::string::npos);

    buffer = nullptr;
    size = 0u;
    FILE* json_stream = open_memstream(&buffer, &size);
    ASSERT_NE(json_stream, nullptr);
    ASSERT_TRUE(ada_metrics_formatter_write_json(&view, json_stream));
    fclose(json_stream);
    std::string json_output(buffer, size);
    free(buffer);
    EXPECT_NE(json_output.find("\"kind\":\"unknown\""), std::string::npos);
    EXPECT_NE(json_output.find("\"threads\":[]"), std::string::npos);
}

TEST(MetricsFormatter, formatter__multiple_snapshots__then_comma_separated_json) {
    ada_thread_metrics_snapshot_t snapshots[2]{};
    snapshots[0].thread_id = 101;
    snapshots[0].slot_index = 1;
    snapshots[0].events_written = 10;
    snapshots[1].thread_id = 202;
    snapshots[1].slot_index = 2;
    snapshots[1].events_written = 20;

    ada_metrics_report_view_t view{};
    view.kind = ADA_METRICS_REPORT_KIND_PERIODIC;
    view.timestamp_ns = 4242u;
    view.snapshots = snapshots;
    view.snapshot_count = 2;
    view.totals.total_events_written = 30u;
    view.totals.total_bytes_written = 300u;
    view.totals.active_thread_count = 2u;
    view.rates.system_events_per_second = 1.0;
    view.rates.system_bytes_per_second = 2.0;

    char* buffer = nullptr;
    size_t size = 0u;
    FILE* json_stream = open_memstream(&buffer, &size);
    ASSERT_NE(json_stream, nullptr);
    ASSERT_TRUE(ada_metrics_formatter_write_json(&view, json_stream));
    fclose(json_stream);
    std::string json_output(buffer, size);
    free(buffer);

    size_t threads_pos = json_output.find("\"threads\":[");
    ASSERT_NE(threads_pos, std::string::npos);
    threads_pos += std::strlen("\"threads\":[");
    ASSERT_LT(threads_pos, json_output.size());
    EXPECT_EQ(json_output[threads_pos], '{');

    EXPECT_NE(json_output.find("},{\"thread_id\":"), std::string::npos);
    EXPECT_EQ(json_output.find("\"threads\":[,{"), std::string::npos);
    EXPECT_EQ(json_output.find("},]"), std::string::npos);
}

TEST(MetricsReporter, ForceReportWhilePaused) {
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 111);
    ASSERT_NE(lanes, nullptr);
    ada_thread_metrics_t* metrics = thread_lanes_get_metrics(lanes);
    ASSERT_NE(metrics, nullptr);

    ADA_ATOMIC_STORE(metrics->counters.events_written, 5, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 500, ADA_MEMORY_ORDER_RELAXED);

    ReportObserver observer;
    FILE* text_sink = tmpfile();
    ASSERT_NE(text_sink, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 50;
    config.start_paused = true;
    config.snapshot_capacity = 8;
    config.output_stream = text_sink;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));
    ASSERT_TRUE(ada_metrics_reporter_force_report(reporter));

    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(500)));
    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_EQ(observer.reports.size(), 1u);
        EXPECT_EQ(observer.reports[0].kind, ADA_METRICS_REPORT_KIND_FORCED);
        EXPECT_EQ(observer.reports[0].totals.total_events_written, 5u);
    }

    ADA_ATOMIC_STORE(metrics->counters.events_written, 15, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 1500, ADA_MEMORY_ORDER_RELAXED);

    ada_metrics_reporter_resume(reporter);

    ASSERT_TRUE(wait_for_reports(observer, 3u, std::chrono::milliseconds(1000)));
    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_GE(observer.reports.size(), 3u);
        const CapturedReport& resumed = observer.reports[1];
        EXPECT_EQ(resumed.kind, ADA_METRICS_REPORT_KIND_FORCED); // resume forces immediate sample
        EXPECT_EQ(resumed.totals.total_events_written, 15u);
        const CapturedReport& periodic = observer.reports[2];
        EXPECT_EQ(periodic.kind, ADA_METRICS_REPORT_KIND_PERIODIC);
    }

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    fclose(text_sink);
    thread_registry_deinit(registry);
}

#ifdef ADA_TESTING

TEST(MetricsReporter, metrics_reporter__ns_to_timespec__then_decomposes_nanoseconds) {
    const uint64_t ns_value = 5ull * 1000000000ull + 123ull;
    struct timespec ts = ada_metrics_reporter_test_ns_to_timespec(ns_value);
    EXPECT_EQ(ts.tv_sec, 5);
    EXPECT_EQ(ts.tv_nsec, 123);
}

TEST(MetricsReporter, metrics_reporter__emit_without_registry__then_returns_false) {
    constexpr uint32_t kCapacity = 1;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 10;
    config.start_paused = true;
    config.snapshot_capacity = 1;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    EXPECT_FALSE(ada_metrics_reporter_test_emit_without_registry(reporter,
                                                                 ADA_METRICS_REPORT_KIND_PERIODIC));

    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, PthreadCreateFailure) {
    // Test pthread_create failure path (lines 419-421)
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    // Force pthread_create to fail
    ada_metrics_reporter_test_force_pthread_create_failure(true);

    // Start should fail
    EXPECT_FALSE(ada_metrics_reporter_start(reporter));

    // Thread should not be started
    EXPECT_FALSE(ada_metrics_reporter_test_is_thread_started(reporter));

    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, RestartWhileRunning) {
    // Test restart path when thread is already running (lines 429-434)
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 10000;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    // First start
    ASSERT_TRUE(ada_metrics_reporter_start(reporter));
    EXPECT_TRUE(ada_metrics_reporter_test_is_thread_started(reporter));

    // Try to start again while still running - should succeed and take the "already started" path
    ASSERT_TRUE(ada_metrics_reporter_start(reporter));
    EXPECT_TRUE(ada_metrics_reporter_test_is_thread_started(reporter));

    // Force a report to verify it's running
    ASSERT_TRUE(ada_metrics_reporter_force_report(reporter));
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(200)));

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, metrics_reporter__thread_start_failure__then_returns_false) {
    constexpr uint32_t kCapacity = 1;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    FILE* text_sink = tmpfile();
    ASSERT_NE(text_sink, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 10;
    config.start_paused = false;
    config.snapshot_capacity = 1;
    config.output_stream = text_sink;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ada_metrics_reporter_test_force_thread_start_failure(true);
    EXPECT_FALSE(ada_metrics_reporter_start(reporter));

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));
    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    fclose(text_sink);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, metrics_reporter__non_monotonic_clock__then_uses_realtime_deadline) {
    constexpr uint32_t kCapacity = 1;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    FILE* text_sink = tmpfile();
    ASSERT_NE(text_sink, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 5;
    config.start_paused = false;
    config.snapshot_capacity = 2;
    config.output_stream = text_sink;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    g_timedwait_capture.reset();
    ada_metrics_reporter_test_set_cond_monotonic(reporter, false);
    ada_metrics_reporter_test_set_timedwait_hook(&timedwait_hook_adapter);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    ASSERT_TRUE(g_timedwait_capture.wait_for(std::chrono::milliseconds(500)));
    {
        std::unique_lock<std::mutex> lock(g_timedwait_capture.mutex);
        EXPECT_EQ(g_timedwait_capture.last_mode, 0);
        EXPECT_GE(g_timedwait_capture.last_ts.tv_nsec, 0);
        EXPECT_LT(g_timedwait_capture.last_ts.tv_nsec, 1000000000L);
    }

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_test_set_timedwait_hook(nullptr);
    g_timedwait_capture.reset();
    ada_metrics_reporter_destroy(reporter);
    fclose(text_sink);
    thread_registry_deinit(registry);
}

#endif // ADA_TESTING

TEST(MetricsReporter, StopProducesSummary) {
    constexpr uint32_t kCapacity = 1;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 222);
    ASSERT_NE(lanes, nullptr);
    ada_thread_metrics_t* metrics = thread_lanes_get_metrics(lanes);
    ASSERT_NE(metrics, nullptr);

    ADA_ATOMIC_STORE(metrics->counters.events_written, 20, ADA_MEMORY_ORDER_RELAXED);

    ReportObserver observer;
    FILE* text_sink = tmpfile();
    ASSERT_NE(text_sink, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 30;
    config.snapshot_capacity = 4;
    config.output_stream = text_sink;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);
    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(500)));

    ada_metrics_reporter_stop(reporter);

    ASSERT_TRUE(wait_for_reports(observer, 2u, std::chrono::milliseconds(500)));
    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_GE(observer.reports.size(), 2u);
        const CapturedReport& summary = observer.reports.back();
        EXPECT_EQ(summary.kind, ADA_METRICS_REPORT_KIND_SUMMARY);
        EXPECT_EQ(summary.totals.total_events_written, 20u);
    }

    ada_metrics_reporter_destroy(reporter);
    fclose(text_sink);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, SetIntervalAndJsonOutputWithNull) {
    // Test null handling for ada_metrics_reporter_set_interval and ada_metrics_reporter_enable_json_output
    ada_metrics_reporter_set_interval(nullptr, 100);
    ada_metrics_reporter_enable_json_output(nullptr, "/tmp/test.json");

    // Create a reporter to test with null path
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    // Test with null path
    ada_metrics_reporter_enable_json_output(reporter, nullptr);

    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, CreateValidatesConfigAndStopWithoutStart) {
    EXPECT_EQ(ada_metrics_reporter_create(nullptr), nullptr);

    ada_metrics_reporter_config_t invalid{};
    EXPECT_EQ(ada_metrics_reporter_create(&invalid), nullptr);

    EXPECT_FALSE(ada_metrics_reporter_start(nullptr));
    EXPECT_FALSE(ada_metrics_reporter_force_report(nullptr));
    EXPECT_FALSE(ada_metrics_reporter_is_paused(nullptr));

    constexpr uint32_t kCapacity = 1;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;
    FILE* text_sink = tmpfile();
    ASSERT_NE(text_sink, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.start_paused = true;
    config.output_stream = text_sink;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ada_metrics_reporter_stop(reporter);
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(200)));
    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_EQ(observer.reports.size(), 1u);
        EXPECT_EQ(observer.reports.back().kind, ADA_METRICS_REPORT_KIND_SUMMARY);
    }

    ada_metrics_reporter_stop(reporter);
    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        EXPECT_EQ(observer.reports.size(), 1u);
    }

    ada_metrics_reporter_destroy(reporter);
    fclose(text_sink);
    thread_registry_deinit(registry);
}

TEST(MetricsFormatter, FormatsHumanReadableAndJson) {
    ada_thread_metrics_snapshot_t snap{};
    snap.thread_id = 7;
    snap.slot_index = 1;
    snap.events_written = 10;
    snap.events_dropped = 1;
    snap.events_filtered = 2;
    snap.bytes_written = 128;
    snap.events_per_second = 50.5;
    snap.bytes_per_second = 512.25;
    snap.drop_rate_percent = 10.0;
    snap.swap_count = 4;
    snap.swaps_per_second = 0.5;
    snap.avg_swap_duration_ns = 100;

    ada_metrics_report_view_t view{};
    view.kind = ADA_METRICS_REPORT_KIND_PERIODIC;
    view.timestamp_ns = 1234u;
    view.snapshots = &snap;
    view.snapshot_count = 1u;
    view.totals.total_events_written = 10u;
    view.totals.total_events_dropped = 1u;
    view.totals.total_events_filtered = 2u;
    view.totals.total_bytes_written = 128u;
    view.totals.active_thread_count = 1u;
    view.rates.system_events_per_second = 50.5;
    view.rates.system_bytes_per_second = 512.25;
    view.rates.last_window_ns = 1000u;

    char* buffer = nullptr;
    size_t size = 0u;
    FILE* text_stream = open_memstream(&buffer, &size);
    ASSERT_NE(text_stream, nullptr);
    ASSERT_TRUE(ada_metrics_formatter_write_text(&view, text_stream));
    fclose(text_stream);
    std::string text_output(buffer, size);
    free(buffer);

    EXPECT_NE(text_output.find("total_events=10"), std::string::npos);
    EXPECT_NE(text_output.find("thread=7"), std::string::npos);

    buffer = nullptr;
    size = 0u;
    FILE* json_stream = open_memstream(&buffer, &size);
    ASSERT_NE(json_stream, nullptr);
    ASSERT_TRUE(ada_metrics_formatter_write_json(&view, json_stream));
    fclose(json_stream);
    std::string json_output(buffer, size);
    free(buffer);

    EXPECT_NE(json_output.find("\"kind\":\"periodic\""), std::string::npos);
    EXPECT_NE(json_output.find("\"thread_id\":7"), std::string::npos);
}

TEST(MetricsReporter, JsonOutputAndIntervalControls) {
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 333);
    ASSERT_NE(lanes, nullptr);
    ada_thread_metrics_t* metrics = thread_lanes_get_metrics(lanes);
    ASSERT_NE(metrics, nullptr);
    ADA_ATOMIC_STORE(metrics->counters.events_written, 10, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 1000, ADA_MEMORY_ORDER_RELAXED);

    char json_template[] = "/tmp/metrics_json_XXXXXX";
    int fd = mkstemp(json_template);
    ASSERT_NE(fd, -1);
    close(fd);
    std::string json_path(json_template);
    unlink(json_path.c_str());

    ReportObserver observer;

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 40;
    config.start_paused = true;
    config.output_stream = nullptr;
    config.json_output_path = json_path.c_str();
    config.snapshot_capacity = 4;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));
    ASSERT_TRUE(ada_metrics_reporter_force_report(reporter));
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(400)));
    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_EQ(observer.reports.size(), 1u);
        EXPECT_EQ(observer.reports[0].kind, ADA_METRICS_REPORT_KIND_FORCED);
    }

    EXPECT_TRUE(ada_metrics_reporter_is_paused(reporter));

    ADA_ATOMIC_STORE(metrics->counters.events_written, 25, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 2500, ADA_MEMORY_ORDER_RELAXED);

    ada_metrics_reporter_resume(reporter);
    EXPECT_FALSE(ada_metrics_reporter_is_paused(reporter));
    ASSERT_TRUE(wait_for_reports(observer, 2u, std::chrono::milliseconds(800)));
    ASSERT_TRUE(wait_for_reports(observer, 3u, std::chrono::milliseconds(1200)));

    ReporterStateAccessor* state = access_state(reporter);
    ASSERT_NE(state, nullptr);

    pthread_mutex_lock(&state->lock);
    uint64_t initial_interval = state->interval_ms;
    pthread_mutex_unlock(&state->lock);
    EXPECT_EQ(initial_interval, 40u);

    ada_metrics_reporter_set_interval(reporter, 75);
    pthread_mutex_lock(&state->lock);
    EXPECT_EQ(state->interval_ms, 75u);
    pthread_mutex_unlock(&state->lock);

    ada_metrics_reporter_set_interval(reporter, 0u);
    pthread_mutex_lock(&state->lock);
    EXPECT_EQ(state->interval_ms, 75u);
    pthread_mutex_unlock(&state->lock);

    char json_template_next[] = "/tmp/metrics_json_next_XXXXXX";
    int fd_next = mkstemp(json_template_next);
    ASSERT_NE(fd_next, -1);
    close(fd_next);
    std::string json_path_next(json_template_next);
    unlink(json_path_next.c_str());

    ada_metrics_reporter_enable_json_output(reporter, json_path_next.c_str());
    ADA_ATOMIC_STORE(metrics->counters.events_written, 50, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 5000, ADA_MEMORY_ORDER_RELAXED);
    ASSERT_TRUE(ada_metrics_reporter_force_report(reporter));
    ASSERT_TRUE(wait_for_reports(observer, 4u, std::chrono::milliseconds(800)));

    ada_metrics_reporter_stop(reporter);
    ASSERT_TRUE(wait_for_reports(observer, 5u, std::chrono::milliseconds(800)));

    ada_metrics_reporter_destroy(reporter);

    std::ifstream first_json(json_path);
    std::string first_contents((std::istreambuf_iterator<char>(first_json)), std::istreambuf_iterator<char>());
    EXPECT_NE(first_contents.find("\"kind\":\"forced\""), std::string::npos);

    std::ifstream second_json(json_path_next);
    std::string second_contents((std::istreambuf_iterator<char>(second_json)), std::istreambuf_iterator<char>());
    EXPECT_NE(second_contents.find("\"kind\":\"forced\""), std::string::npos);
    EXPECT_NE(second_contents.find("\"timestamp_ns\""), std::string::npos);

    unlink(json_path.c_str());
    unlink(json_path_next.c_str());
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, CollectFailureHandling) {
    // Test that emit_report handles collection failure properly
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 50;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    // Force collection to fail
    ada_metrics_reporter_test_force_collect_failure(true);

    // Start the reporter
    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    // Wait a bit but should not get any reports due to failure
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        EXPECT_EQ(observer.reports.size(), 0u);
    }

    // Re-enable collection
    ada_metrics_reporter_test_force_collect_failure(false);

    // Force a report to verify it works now
    ASSERT_TRUE(ada_metrics_reporter_force_report(reporter));
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(200)));

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, ThreadStateTransitions) {
    // Test thread state transitions and pause/wait logic
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 10000; // Long interval to control manually
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    // Test pause function - this exercises lines 396-404
    ada_metrics_reporter_pause(reporter);
    EXPECT_TRUE(ada_metrics_reporter_is_paused(reporter));

    // Verify pause with null reporter doesn't crash (lines 397-399)
    ada_metrics_reporter_pause(nullptr);

    // Test thread state transitions - set to not running to trigger wait loops
    ada_metrics_reporter_test_set_thread_states(reporter, false, false, false);

    // Sleep briefly to let thread enter wait state
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Now set running but paused without force - exercises lines 170-176, 223-226
    ada_metrics_reporter_test_set_thread_states(reporter, true, true, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify thread states
    bool running, paused, force_requested;
    ASSERT_TRUE(ada_metrics_reporter_test_get_thread_states(reporter, &running, &paused, &force_requested));
    EXPECT_TRUE(running);
    EXPECT_TRUE(paused);
    EXPECT_FALSE(force_requested);

    // Resume to continue normal operation
    ada_metrics_reporter_resume(reporter);
    EXPECT_FALSE(ada_metrics_reporter_is_paused(reporter));

    // Force a report to verify thread is working
    ASSERT_TRUE(ada_metrics_reporter_force_report(reporter));
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(200)));

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, PauseResumeNullHandling) {
    // Test that pause/resume functions handle null reporter properly
    ada_metrics_reporter_pause(nullptr);
    ada_metrics_reporter_resume(nullptr);
    EXPECT_FALSE(ada_metrics_reporter_is_paused(nullptr));
    EXPECT_FALSE(ada_metrics_reporter_force_report(nullptr));
    ada_metrics_reporter_stop(nullptr);
    ada_metrics_reporter_destroy(nullptr);

    // Test thread state functions with null
    EXPECT_FALSE(ada_metrics_reporter_test_get_thread_states(nullptr, nullptr, nullptr, nullptr));
    ada_metrics_reporter_test_set_thread_states(nullptr, true, true, true);
    ada_metrics_reporter_test_trigger_shutdown(nullptr);
    ada_metrics_reporter_test_disable_global_collection(nullptr);
    EXPECT_EQ(ada_metrics_reporter_test_get_global_metrics(nullptr), nullptr);
    EXPECT_FALSE(ada_metrics_reporter_test_is_thread_started(nullptr));
}

TEST(MetricsReporter, GlobalMetricsCollectFailure) {
    // Test actual ada_global_metrics_collect failure path
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 50;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    // Get the global metrics structure and disable collection
    ada_global_metrics_t* global = ada_metrics_reporter_test_get_global_metrics(reporter);
    ASSERT_NE(global, nullptr);

    // Start the reporter
    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    // Disable collection in the global metrics to trigger actual failure
    ada_metrics_reporter_test_disable_global_collection(global);

    // Wait a bit - should not get reports due to disabled collection
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        EXPECT_EQ(observer.reports.size(), 0u);
    }

    // Re-enable collection
    ADA_ATOMIC_STORE(global->control.collection_enabled, true, ADA_MEMORY_ORDER_RELAXED);

    // Force a report to verify it works now
    ASSERT_TRUE(ada_metrics_reporter_force_report(reporter));
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(200)));

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, ShutdownDuringVariousStates) {
    // Test shutdown during different thread states to cover lines 165-168, 173-176
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 10000;  // Long interval

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    // Test 1: Shutdown while not running (exercises lines 165-168)
    ada_metrics_reporter_test_set_thread_states(reporter, false, false, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ada_metrics_reporter_test_trigger_shutdown(reporter);

    // Wait for thread to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, ResumeFunction) {
    // Test resume function including null handling (lines 406-416)
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 10000;
    config.start_paused = true;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));
    EXPECT_TRUE(ada_metrics_reporter_is_paused(reporter));

    // Test resume with null (lines 407-409)
    ada_metrics_reporter_resume(nullptr);

    // Now resume properly (lines 410-416)
    ada_metrics_reporter_resume(reporter);
    EXPECT_FALSE(ada_metrics_reporter_is_paused(reporter));

    // Should get an immediate report after resume
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(200)));

    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_EQ(observer.reports.size(), 1u);
        EXPECT_EQ(observer.reports[0].kind, ADA_METRICS_REPORT_KIND_FORCED);
    }

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

TEST(MetricsReporter, ComplexThreadStateTransitions) {
    // More comprehensive thread state testing
    constexpr uint32_t kCapacity = 2;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;

    ada_metrics_reporter_config_t config{};
    config.registry = registry;
    config.report_interval_ms = 50;
    config.sink = &capture_report;
    config.sink_user_data = &observer;

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    // Let the thread start properly
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Scenario 1: Set to not running to trigger wait loop (exercises lines 162-168, 219-221)
    ada_metrics_reporter_test_set_thread_states(reporter, false, false, false);

    // Signal the condition variable to wake thread
    ReporterStateAccessor* state = access_state(reporter);
    pthread_mutex_lock(&state->lock);
    pthread_cond_broadcast(&state->cond);
    pthread_mutex_unlock(&state->lock);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Scenario 2: Set running but paused without force (exercises lines 170-176, 223-227)
    ada_metrics_reporter_test_set_thread_states(reporter, true, true, false);

    pthread_mutex_lock(&state->lock);
    pthread_cond_broadcast(&state->cond);
    pthread_mutex_unlock(&state->lock);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Scenario 3: Now set force_requested while paused to trigger forced report
    ada_metrics_reporter_test_set_thread_states(reporter, true, true, true);

    pthread_mutex_lock(&state->lock);
    pthread_cond_broadcast(&state->cond);
    pthread_mutex_unlock(&state->lock);

    // Wait for the forced report
    ASSERT_TRUE(wait_for_reports(observer, 1u, std::chrono::milliseconds(200)));

    // Verify we got the forced report
    {
        std::lock_guard<std::mutex> lock(observer.mutex);
        ASSERT_GE(observer.reports.size(), 1u);
        EXPECT_EQ(observer.reports[0].kind, ADA_METRICS_REPORT_KIND_FORCED);
    }

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

#ifdef ADA_TESTING

// Test monotonic clock path (covers line 196)
TEST(MetricsReporter, MonotonicClockPath) {
    constexpr uint32_t kCapacity = 1;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;
    ada_metrics_reporter_config_t config = {
        .registry = registry,
        .report_interval_ms = 50,
        .start_paused = false,
        .json_output_path = nullptr,
        .output_stream = nullptr,
        .snapshot_capacity = kCapacity,
        .sink = capture_report,
        .sink_user_data = &observer
    };

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    // Force monotonic clock path
    g_timedwait_capture.reset();
    ada_metrics_reporter_test_set_cond_monotonic(reporter, true);  // true = monotonic
    ada_metrics_reporter_test_set_timedwait_hook(&timedwait_hook_adapter);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    // Wait for timedwait to be called
    ASSERT_TRUE(g_timedwait_capture.wait_for(std::chrono::milliseconds(500)));
    {
        std::unique_lock<std::mutex> lock(g_timedwait_capture.mutex);
        // Mode 1 indicates monotonic clock
        EXPECT_EQ(g_timedwait_capture.last_mode, 1);
    }

    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_test_set_timedwait_hook(nullptr);
    g_timedwait_capture.reset();
    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

// Test paused state continuation (covers lines 231-233)
TEST(MetricsReporter, PausedThreadContinuation) {
    constexpr uint32_t kCapacity = 1;
    const size_t memory_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> backing(memory_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(backing.data(), backing.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ReportObserver observer;
    ada_metrics_reporter_config_t config = {
        .registry = registry,
        .report_interval_ms = 10,  // Short interval
        .start_paused = false,  // Start running
        .json_output_path = nullptr,
        .output_stream = nullptr,
        .snapshot_capacity = kCapacity,
        .sink = capture_report,
        .sink_user_data = &observer
    };

    ada_metrics_reporter* reporter = ada_metrics_reporter_create(&config);
    ASSERT_NE(reporter, nullptr);

    ASSERT_TRUE(ada_metrics_reporter_start(reporter));

    // Let it run normally first
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    // Now pause without force - this sets paused=true
    ada_metrics_reporter_pause(reporter);

    // Quickly unpause and set the states manually to create the race condition
    // where paused=true but we're not actually waiting on the condition
    ada_metrics_reporter_test_set_thread_states(reporter, true, true, false);

    // Signal the condition to wake the thread
    ada_metrics_reporter_force_report(reporter);  // This signals but we reset force_requested
    ada_metrics_reporter_test_set_thread_states(reporter, true, true, false);  // Reset force_requested

    // Give it time to hit the paused check
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Clean up
    ada_metrics_reporter_stop(reporter);
    ada_metrics_reporter_destroy(reporter);
    thread_registry_deinit(registry);
}

// Test nullptr validation in initialize_state (covers lines 249-250 and 399-400)
TEST(MetricsReporter, InitializeStateNullValidation) {
    // Test with null config - should hit line 394 check
    ada_metrics_reporter* reporter = ada_metrics_reporter_create(nullptr);
    ASSERT_EQ(reporter, nullptr);

    // Test with null registry - should hit lines 249-250 and 399-400
    ada_metrics_reporter_config_t config = {};
    config.registry = nullptr;  // Explicitly null
    config.report_interval_ms = 100;
    config.start_paused = false;
    config.json_output_path = nullptr;
    config.output_stream = nullptr;
    config.snapshot_capacity = 10;
    config.sink = nullptr;
    config.sink_user_data = nullptr;

    reporter = ada_metrics_reporter_create(&config);
    ASSERT_EQ(reporter, nullptr);

    // Also test with a valid config but null registry
    ada_metrics_reporter_config_t config2 = {
        .registry = nullptr,
        .report_interval_ms = 100,
        .start_paused = false,
        .json_output_path = nullptr,
        .output_stream = nullptr,
        .snapshot_capacity = 10,
        .sink = nullptr,
        .sink_user_data = nullptr
    };

    reporter = ada_metrics_reporter_create(&config2);
    ASSERT_EQ(reporter, nullptr);
}


// Test null reporter in test functions (covers lines 289-290, 312-313)
TEST(MetricsReporter, TestFunctionsNullHandling) {
    // Test ada_metrics_reporter_test_emit_without_registry with null
    bool result = ada_metrics_reporter_test_emit_without_registry(nullptr, ADA_METRICS_REPORT_KIND_PERIODIC);
    EXPECT_FALSE(result);

    // Test ada_metrics_reporter_test_set_cond_monotonic with null
    ada_metrics_reporter_test_set_cond_monotonic(nullptr, true);
    // No crash means the null check worked
}

// Test callback null handling (covers lines 71-72)
TEST(MetricsReporter, CaptureReportNullHandling) {
    // Test with null observer
    capture_report(nullptr, nullptr);

    // Test with null view
    ReportObserver observer;
    capture_report(nullptr, &observer);

    // Test with null user_data but valid view
    ada_metrics_report_view_t view = {};
    capture_report(&view, nullptr);

    // All should handle nulls gracefully without crash
}

#endif // ADA_TESTING

} // namespace
