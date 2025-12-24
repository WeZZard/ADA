#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>

static std::atomic<bool> g_fail_next_nothrow_alloc{false};

static void fail_next_nothrow_allocation() {
    g_fail_next_nothrow_alloc.store(true, std::memory_order_release);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (g_fail_next_nothrow_alloc.exchange(false, std::memory_order_acq_rel)) {
        return nullptr;
    }
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    if (g_fail_next_nothrow_alloc.exchange(false, std::memory_order_acq_rel)) {
        return nullptr;
    }
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete[](ptr);
}


extern "C" {
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/atf/atf_thread_writer.h>
}

#include <tracer_backend/selective_persistence/detail_lane_control.h>
#include <tracer_backend/selective_persistence/marking_policy.h>
#include <tracer_backend/selective_persistence/persistence_window.h>

#include "thread_registry_private.h"

namespace {

struct RegistryFixture {
    std::unique_ptr<uint8_t[]> arena;
    ThreadRegistry* registry{nullptr};
    ThreadLaneSet* lanes{nullptr};
    RingPool* detail_pool{nullptr};

    RegistryFixture() {
        size_t size = thread_registry_calculate_memory_size_with_capacity(2);
        arena = std::unique_ptr<uint8_t[]>(new uint8_t[size]);
        std::memset(arena.get(), 0, size);
        registry = thread_registry_init_with_capacity(arena.get(), size, 2);
        if (!registry) {
            throw std::runtime_error("Failed to initialize registry");
        }
        if (!thread_registry_attach(registry)) {
            throw std::runtime_error("Failed to attach registry");
        }
        lanes = thread_registry_register(registry, 0xABCD);
        if (!lanes) {
            throw std::runtime_error("Failed to register thread lanes");
        }
        detail_pool = ring_pool_create(registry, lanes, 1);
        if (!detail_pool) {
            throw std::runtime_error("Failed to create detail ring pool");
        }
    }

    ~RegistryFixture() {
        if (detail_pool) {
            ring_pool_destroy(detail_pool);
        }
        if (lanes) {
            thread_registry_unregister(lanes);
        }
        if (registry) {
            thread_registry_deinit(registry);
        }
    }
};

AdaMarkingPatternDesc default_pattern() {
    AdaMarkingPatternDesc desc{};
    desc.target = ADA_MARKING_TARGET_MESSAGE;
    desc.match = ADA_MARKING_MATCH_LITERAL;
    desc.case_sensitive = false;
    desc.pattern = "ERROR";
    return desc;
}

AdaMarkingProbe message_probe(const char* message) {
    AdaMarkingProbe probe{};
    probe.message = message;
    return probe;
}

void fill_ring_to_capacity(RingBufferHeader* header, size_t event_size) {
    ASSERT_NE(header, nullptr);
    std::vector<uint8_t> event(event_size, 0xA5);
    while (ring_buffer_write_raw(header, event_size, event.data())) {
        // Continue writing until ring reports full
    }
}

void fill_ring_fraction(RingBufferHeader* header, size_t event_size, double fraction) {
    ASSERT_NE(header, nullptr);
    std::vector<uint8_t> event(event_size, 0x3C);
    uint32_t capacity = header->capacity;
    if (capacity == 0) return;
    uint32_t target = static_cast<uint32_t>((capacity - 1) * fraction);
    for (uint32_t i = 0; i < target; ++i) {
        bool wrote = ring_buffer_write_raw(header, event_size, event.data());
        if (!wrote) break;
    }
}

}  // namespace

TEST(DetailLaneControlTest, ring_not_full__marked_event_seen__then_no_dump) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 100);

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_fraction(header, sizeof(DetailEvent), 0.7);

    AdaMarkingProbe probe = message_probe("ERROR: transient");
    EXPECT_TRUE(detail_lane_control_mark_event(control, &probe, 110));
    EXPECT_FALSE(detail_lane_control_should_dump(control));

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, ring_full__no_marked_event__then_discard_and_continue) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 200);

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));

    EXPECT_FALSE(detail_lane_control_should_dump(control));
    EXPECT_EQ(detail_lane_control_windows_discarded(control), 1u);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, ring_full__marked_event_seen__then_dump_triggered) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 300);

    AdaMarkingProbe probe = message_probe("ERROR: persistent");
    EXPECT_TRUE(detail_lane_control_mark_event(control, &probe, 310));

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));

    EXPECT_TRUE(detail_lane_control_should_dump(control));
    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 320, &window));
    EXPECT_EQ(window.start_timestamp_ns, 300u);
    EXPECT_EQ(window.end_timestamp_ns, 320u);
    EXPECT_EQ(window.total_events, 1u);
    EXPECT_EQ(window.marked_events, 1u);
    EXPECT_TRUE(window.mark_seen);
    EXPECT_EQ(window.first_mark_timestamp_ns, 310u);
    detail_lane_control_mark_dump_complete(control, 321);
    EXPECT_EQ(detail_lane_control_selective_dumps_performed(control), 1u);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, ring_full__lane_mark_cleared__then_discarded_and_reset) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 360);

    AdaMarkingProbe probe = message_probe("ERROR: transient lane clear");
    ASSERT_TRUE(detail_lane_control_mark_event(control, &probe, 365));

    Lane* lane = thread_lanes_get_detail_lane(fx.lanes);
    ASSERT_NE(lane, nullptr);
    lane_clear_marked_event(lane);

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));

    EXPECT_FALSE(detail_lane_control_should_dump(control));
    EXPECT_EQ(detail_lane_control_windows_discarded(control), 1u);

    SelectivePersistenceWindow snapshot{};
    detail_lane_control_snapshot_window(control, &snapshot);
    EXPECT_FALSE(snapshot.mark_seen);
    EXPECT_EQ(snapshot.marked_events, 0u);
    EXPECT_EQ(snapshot.first_mark_timestamp_ns, 0u);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, concurrent_marking__multiple_threads__then_thread_safe) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 400);

    constexpr int kThreads = 4;
    constexpr int kEventsPerThread = 250;

    auto worker = [&]() {
        for (int i = 0; i < kEventsPerThread; ++i) {
            AdaMarkingProbe probe = message_probe("ERROR: burst");
            detail_lane_control_mark_event(control, &probe, 410 + i);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_GT(detail_lane_control_marked_events_detected(control), 0u);
    SelectivePersistenceWindow snapshot{};
    detail_lane_control_snapshot_window(control, &snapshot);
    EXPECT_EQ(snapshot.total_events, static_cast<uint64_t>(kThreads * kEventsPerThread));
    EXPECT_EQ(snapshot.marked_events, snapshot.total_events);
    EXPECT_TRUE(snapshot.mark_seen);
    EXPECT_GE(snapshot.first_mark_timestamp_ns, 410u);

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));
    EXPECT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 600, &window));
    EXPECT_EQ(window.total_events, snapshot.total_events);
    EXPECT_EQ(window.marked_events, snapshot.marked_events);
    EXPECT_GE(window.end_timestamp_ns, 600u);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, new_window__start_timestamp_set__then_tracking_begins) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);

    detail_lane_control_start_new_window(control, 1234);

    SelectivePersistenceWindow window{};
    detail_lane_control_snapshot_window(control, &window);

    EXPECT_EQ(window.start_timestamp_ns, 1234u);
    EXPECT_EQ(window.end_timestamp_ns, 0u);
    EXPECT_EQ(window.total_events, 0u);
    EXPECT_FALSE(window.mark_seen);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, close_window__timestamps_updated__then_dump_ready) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 1000);

    AdaMarkingProbe probe = message_probe("ERROR: start");
    EXPECT_TRUE(detail_lane_control_mark_event(control, &probe, 1100));

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));

    EXPECT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 1200, &window));
    EXPECT_EQ(window.start_timestamp_ns, 1000u);
    EXPECT_EQ(window.end_timestamp_ns, 1200u);
    EXPECT_EQ(window.total_events, 1u);
    EXPECT_EQ(window.marked_events, 1u);
    EXPECT_TRUE(window.mark_seen);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, window_metadata__calculated_correctly__then_accurate) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 2000);

    AdaMarkingProbe probe_marked = message_probe("ERROR: first");
    EXPECT_TRUE(detail_lane_control_mark_event(control, &probe_marked, 2010));

    AdaMarkingProbe probe_unmarked = message_probe("info");
    EXPECT_FALSE(detail_lane_control_mark_event(control, &probe_unmarked, 2020));

    SelectivePersistenceWindow snapshot{};
    detail_lane_control_snapshot_window(control, &snapshot);
    EXPECT_EQ(snapshot.total_events, 2u);
    EXPECT_EQ(snapshot.marked_events, 1u);
    EXPECT_EQ(snapshot.first_mark_timestamp_ns, 2010u);
    EXPECT_TRUE(snapshot.mark_seen);

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));

    EXPECT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 2050, &window));
    EXPECT_EQ(window.total_events, 2u);
    EXPECT_EQ(window.marked_events, 1u);
    EXPECT_EQ(window.first_mark_timestamp_ns, 2010u);
    EXPECT_EQ(window.end_timestamp_ns, 2050u);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, selective_swap__conditions_met__then_swap_performed) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 3000);

    AdaMarkingProbe probe = message_probe("ERROR: swap");
    EXPECT_TRUE(detail_lane_control_mark_event(control, &probe, 3010));

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));

    EXPECT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 3020, &window));

    uint32_t swapped_ring = UINT32_MAX;
    EXPECT_TRUE(detail_lane_control_perform_selective_swap(control, &swapped_ring));
    EXPECT_NE(swapped_ring, UINT32_MAX);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlTest, selective_swap__conditions_not_met__then_swap_skipped) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 4000);

    uint32_t swapped_ring = 0;
    EXPECT_FALSE(detail_lane_control_perform_selective_swap(control, &swapped_ring));
    EXPECT_EQ(swapped_ring, 0u);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlMetricsTest, AC_metrics__accurate_counting__then_observable) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 1000);

    AdaMarkingProbe marked_probe = message_probe("ERROR: hot path");
    EXPECT_TRUE(detail_lane_control_mark_event(control, &marked_probe, 1010));

    AdaMarkingProbe unmarked_probe = message_probe("info: cold path");
    EXPECT_FALSE(detail_lane_control_mark_event(control, &unmarked_probe, 1020));

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));
    ASSERT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 1200, &window));
    detail_lane_control_mark_dump_complete(control, 1300);

    SelectivePersistenceMetrics metrics{};
    detail_lane_control_collect_metrics(control, &metrics);

    EXPECT_EQ(metrics.events_processed, 2u);
    EXPECT_EQ(metrics.marked_events_detected, 1u);
    EXPECT_EQ(metrics.selective_dumps_performed, 1u);
    EXPECT_EQ(metrics.windows_discarded, 0u);
    EXPECT_GT(metrics.avg_window_duration_ns, 0u);
    EXPECT_EQ(metrics.avg_events_per_window, 2u);
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_mark_rate(&metrics), 0.5);
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_dump_success_ratio(&metrics), 1.0);
    EXPECT_DOUBLE_EQ(selective_persistence_metrics_estimated_overhead(&metrics), 0.0);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlErrorTest, AC_error_handling__close_without_mark__then_reports_state_error) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 2000);

    SelectivePersistenceWindow window{};
    EXPECT_FALSE(detail_lane_control_close_window_for_dump(control, 2050, &window));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_STATE);

    detail_lane_control_clear_error(control);
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_NONE);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlErrorTest, AC_error_handling__metadata_failure__then_failure_counted) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 3000);

    AdaMarkingProbe probe = message_probe("ERROR: metadata");
    ASSERT_TRUE(detail_lane_control_mark_event(control, &probe, 3010));

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));
    ASSERT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 3020, &window));

    AtfV4Writer writer{};
    std::snprintf(writer.session_dir, sizeof(writer.session_dir), "/nonexistent/path/%llu",
                  static_cast<unsigned long long>(window.window_id));

    EXPECT_FALSE(detail_lane_control_write_window_metadata(control, &window, &writer));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_IO_FAILURE);

    SelectivePersistenceMetrics metrics{};
    detail_lane_control_collect_metrics(control, &metrics);
    EXPECT_EQ(metrics.metadata_write_failures, 1u);

    detail_lane_control_clear_error(control);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(DetailLaneControlCreateTest, invalid_arguments__null_dependencies__then_creation_fails) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);

    EXPECT_EQ(detail_lane_control_create(nullptr, fx.lanes, fx.detail_pool, policy), nullptr);
    EXPECT_EQ(detail_lane_control_create(fx.registry, nullptr, fx.detail_pool, policy), nullptr);
    EXPECT_EQ(detail_lane_control_create(fx.registry, fx.lanes, nullptr, policy), nullptr);
    EXPECT_EQ(detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, nullptr), nullptr);

    marking_policy_destroy(policy);
}

TEST(DetailLaneControlCreateTest, allocation_failure__nothrow_new_returns_null__then_creation_fails) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    fail_next_nothrow_allocation();
    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    EXPECT_EQ(control, nullptr);

    marking_policy_destroy(policy);
}

TEST(DetailLaneControlGuardTest, guard_functions__null_inputs__then_safe_defaults) {
    detail_lane_control_start_new_window(nullptr, 10);
    detail_lane_control_start_window(nullptr, 20);
    detail_lane_control_mark_dump_complete(nullptr, 30);
    detail_lane_control_record_dump(nullptr, 40);

    SelectivePersistenceWindow window{};
    window.window_id = 123;
    detail_lane_control_snapshot_window(nullptr, &window);
    EXPECT_EQ(window.window_id, 123u);
    detail_lane_control_snapshot_window(nullptr, nullptr);

    AdaMarkingProbe probe{};
    probe.message = "noop";
    EXPECT_FALSE(detail_lane_control_mark_event(nullptr, nullptr, 0));
    EXPECT_FALSE(detail_lane_control_mark_event(nullptr, &probe, 0));
    EXPECT_FALSE(detail_lane_control_should_dump(nullptr));
    EXPECT_FALSE(detail_lane_control_close_window_for_dump(nullptr, 0, nullptr));
    EXPECT_FALSE(detail_lane_control_perform_selective_swap(nullptr, nullptr));

    EXPECT_FALSE(detail_lane_control_write_window_metadata(nullptr, nullptr, nullptr));

    SelectivePersistenceMetrics metrics{};
    metrics.events_processed = 99;
    detail_lane_control_collect_metrics(nullptr, &metrics);
    EXPECT_EQ(metrics.events_processed, 0u);
    detail_lane_control_collect_metrics(nullptr, nullptr);

    EXPECT_EQ(detail_lane_control_last_error(nullptr), DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);
    detail_lane_control_clear_error(nullptr);

    EXPECT_EQ(detail_lane_control_marked_events_detected(nullptr), 0u);
    EXPECT_EQ(detail_lane_control_selective_dumps_performed(nullptr), 0u);
    EXPECT_EQ(detail_lane_control_windows_discarded(nullptr), 0u);
}

TEST(DetailLaneControlErrorTest, mark_event__null_probe__then_invalid_argument_recorded) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 100);

    EXPECT_FALSE(detail_lane_control_mark_event(control, nullptr, 120));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlErrorTest, mark_event__timestamp_before_window__then_state_error) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 200);

    AdaMarkingProbe probe = message_probe("ERROR: out-of-order");
    EXPECT_FALSE(detail_lane_control_mark_event(control, &probe, 150));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_STATE);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlErrorTest, close_window__timestamp_before_start__then_invalid_argument_reported) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 300);

    AdaMarkingProbe probe = message_probe("ERROR: timing");
    ASSERT_TRUE(detail_lane_control_mark_event(control, &probe, 310));

    SelectivePersistenceWindow window{};
    EXPECT_FALSE(detail_lane_control_close_window_for_dump(control, 290, &window));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlErrorTest, close_window__null_output__then_invalid_argument_recorded) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 400);

    EXPECT_FALSE(detail_lane_control_close_window_for_dump(control, 410, nullptr));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlErrorTest, selective_swap__null_output__then_invalid_argument_recorded) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 500);

    EXPECT_FALSE(detail_lane_control_perform_selective_swap(control, nullptr));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlErrorTest, selective_swap__no_mark_seen__then_state_error) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 600);

    uint32_t ring_idx = 0;
    EXPECT_FALSE(detail_lane_control_perform_selective_swap(control, &ring_idx));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_STATE);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlMetadataTest, metadata_writer__nullptr_arguments__then_invalid_argument_recorded) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 700);

    SelectivePersistenceWindow window{};
    window.mark_seen = true;

    EXPECT_FALSE(detail_lane_control_write_window_metadata(control, nullptr, nullptr));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);

    AtfV4Writer writer{};
    EXPECT_FALSE(detail_lane_control_write_window_metadata(control, &window, nullptr));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlMetadataTest, metadata_writer__empty_session_dir__then_invalid_argument_recorded) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 800);

    SelectivePersistenceWindow window{};
    window.mark_seen = true;

    AtfV4Writer writer{};
    writer.session_dir[0] = '\0';

    EXPECT_FALSE(detail_lane_control_write_window_metadata(control, &window, &writer));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlMetadataTest, metadata_writer__path_overflow__then_io_failure_recorded) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 900);

    SelectivePersistenceWindow window{};
    window.mark_seen = true;

    AtfV4Writer writer{};
    std::string long_dir(4095, 'a');
    std::memcpy(writer.session_dir, long_dir.c_str(), long_dir.size());
    writer.session_dir[long_dir.size()] = '\0';

    EXPECT_FALSE(detail_lane_control_write_window_metadata(control, &window, &writer));
    EXPECT_EQ(detail_lane_control_last_error(control), DETAIL_LANE_CONTROL_ERROR_IO_FAILURE);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlMetadataTest, metadata_writer__valid_directory__then_file_written) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 1000);

    AdaMarkingProbe probe = message_probe("ERROR: metadata success");
    ASSERT_TRUE(detail_lane_control_mark_event(control, &probe, 1010));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 1020, &window));

    auto tmp_root = std::filesystem::temp_directory_path();
    auto unique_suffix = std::to_string(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(control)));
    std::filesystem::path session_dir = tmp_root / ("ada_detail_metadata_" + unique_suffix);
    std::filesystem::create_directories(session_dir);

    AtfV4Writer writer{};
    std::snprintf(writer.session_dir, sizeof(writer.session_dir), "%s", session_dir.string().c_str());

    ASSERT_TRUE(detail_lane_control_write_window_metadata(control, &window, &writer));

    std::filesystem::path metadata_path = session_dir / "window_metadata.jsonl";
    ASSERT_TRUE(std::filesystem::exists(metadata_path));

    std::ifstream metadata_file(metadata_path);
    ASSERT_TRUE(metadata_file.is_open());
    std::string line;
    std::getline(metadata_file, line);
    metadata_file.close();
    EXPECT_NE(line.find("\"mark_seen\":true"), std::string::npos);

    std::filesystem::remove_all(session_dir);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlMetricsTest, collect_metrics__pending_window_included__then_pending_consumed) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 1100);

    AdaMarkingProbe probe = message_probe("ERROR: pending metrics");
    ASSERT_TRUE(detail_lane_control_mark_event(control, &probe, 1110));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 1120, &window));

    SelectivePersistenceMetrics metrics{};
    detail_lane_control_collect_metrics(control, &metrics);
    EXPECT_EQ(metrics.marked_events_detected, 1u);
    EXPECT_GT(metrics.avg_window_duration_ns, 0u);
    EXPECT_EQ(metrics.selective_dumps_performed, 0u);

    detail_lane_control_mark_dump_complete(control, 1130);

    SelectivePersistenceMetrics metrics_after{};
    detail_lane_control_collect_metrics(control, &metrics_after);
    EXPECT_EQ(metrics_after.selective_dumps_performed, 1u);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlMetricsTest, mark_dump_complete__without_pending__then_counts_increment) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 1200);

    detail_lane_control_mark_dump_complete(control, 1210);
    EXPECT_EQ(detail_lane_control_selective_dumps_performed(control), 1u);

    SelectivePersistenceMetrics metrics{};
    detail_lane_control_collect_metrics(control, &metrics);
    EXPECT_EQ(metrics.selective_dumps_performed, 1u);
    EXPECT_EQ(metrics.avg_events_per_window, 0u);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}

TEST(DetailLaneControlWrapperTest, wrappers__start_and_record__then_state_advances) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_window(control, 1300);

    AdaMarkingProbe probe = message_probe("ERROR: wrapper");
    ASSERT_TRUE(detail_lane_control_mark_event(control, &probe, 1310));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 1320, &window));
    uint64_t dumps_before = detail_lane_control_selective_dumps_performed(control);
    detail_lane_control_record_dump(control, 1330);
    EXPECT_EQ(detail_lane_control_selective_dumps_performed(control), dumps_before + 1);

    SelectivePersistenceWindow snapshot{};
    detail_lane_control_snapshot_window(control, &snapshot);
    EXPECT_EQ(snapshot.start_timestamp_ns, 1330u);

    detail_lane_control_destroy(control);
    marking_policy_destroy(policy);
}
