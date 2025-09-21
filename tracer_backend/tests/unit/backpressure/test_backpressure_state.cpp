#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <cstring>
#include <cstdlib>
#include <array>
#include <atomic>
#include <new>

extern "C" {
#include <tracer_backend/backpressure/backpressure.h>
#include <tracer_backend/ada/thread.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_pool.h>
}

#include "ring_buffer_private.h"
#include "thread_registry_private.h"

using ::testing::AnyOf;

static bool g_fail_next_nothrow_new = false;

namespace {

static std::unique_ptr<uint8_t[]> alloc_registry(size_t& out_size) {
    out_size = thread_registry_calculate_memory_size_with_capacity(4);
    auto mem = std::unique_ptr<uint8_t[]>(new uint8_t[out_size]);
    std::memset(mem.get(), 0, out_size);
    return mem;
}

class FailNextNothrowNewGuard {
public:
    FailNextNothrowNewGuard() { g_fail_next_nothrow_new = true; }
    ~FailNextNothrowNewGuard() { g_fail_next_nothrow_new = false; }
};

static ada::internal::ThreadLaneSet* to_internal(ThreadLaneSet* lanes) {
    return reinterpret_cast<ada::internal::ThreadLaneSet*>(lanes);
}

static ada::internal::Lane* get_lane(ThreadLaneSet* lanes, int lane_type) {
    auto* internal = to_internal(lanes);
    return (lane_type == 0) ? &internal->index_lane : &internal->detail_lane;
}

static void set_lane_free_indices(ada::internal::Lane* lane, uint32_t head, uint32_t tail, uint32_t capacity) {
    lane->free_capacity = capacity;
    lane->free_head.store(head, std::memory_order_release);
    lane->free_tail.store(tail, std::memory_order_release);
}

class RingBufferFixture {
public:
    RingBufferFixture() {
        buffer.fill(0);
        EXPECT_TRUE(ring.initialize(buffer.data(), buffer.size(), sizeof(uint64_t)));
    }

    ada::internal::RingBuffer ring;
    std::array<uint8_t, 4096> buffer;
};

} // namespace

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (g_fail_next_nothrow_new) {
        g_fail_next_nothrow_new = false;
        return nullptr;
    }
    return std::malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}

TEST(BackpressureState, backpressure_state__transitions_across_thresholds__then_hysteresis_respected) {
    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, nullptr);
    ada_backpressure_state_set_total_rings(&state, 4);

    EXPECT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_NORMAL);

    ada_backpressure_state_sample(&state, 3, 10);
    EXPECT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_NORMAL);

    ada_backpressure_state_sample(&state, 0, 20);
    EXPECT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_PRESSURE);

    ada_backpressure_state_sample(&state, 0, 30);
    EXPECT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_DROPPING);

    ada_backpressure_state_on_drop(&state, 0, 35);
    EXPECT_EQ(ada_backpressure_state_get_drops(&state), 1u);

    ada_backpressure_state_sample(&state, 3, 40);
    EXPECT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_RECOVERY);

    ada_backpressure_state_sample(&state, 3, 40 + 900000000ull);
    EXPECT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_RECOVERY);

    ada_backpressure_state_sample(&state, 3, 40 + 1000000005ull);
    EXPECT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_NORMAL);
}

TEST(BackpressureState,
     backpressure_state__recovery_callback_from_dropping__then_updates_state_and_metrics) {
    ada_backpressure_testing_reset_log_counters();

    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, nullptr);
    ada_backpressure_state_set_total_rings(&state, 4);

    ada_backpressure_state_sample(&state, 0, 10);
    ASSERT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_PRESSURE);

    ada_backpressure_state_sample(&state, 0, 20);
    ASSERT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_DROPPING);

    ada_backpressure_state_on_recovery(&state, 3, 30);

    EXPECT_EQ(ada_backpressure_state_get_mode(&state), ADA_BACKPRESSURE_STATE_RECOVERY);

    ada_backpressure_metrics_t metrics = {};
    bp_export_metrics(&state, &metrics);
    EXPECT_EQ(metrics.free_rings, 3u);
    EXPECT_EQ(metrics.last_recovery_ns, 30u);

    EXPECT_EQ(ada_backpressure_testing_last_state_previous(), ADA_BACKPRESSURE_STATE_DROPPING);
    EXPECT_EQ(ada_backpressure_testing_last_state_next(), ADA_BACKPRESSURE_STATE_RECOVERY);
}

TEST(BackpressureState, bp_log_state_change__invalid_mode__then_records_unknown_transition) {
    ada_backpressure_testing_reset_log_counters();

    const auto invalid_mode = static_cast<ada_backpressure_mode_t>(99);

    bp_log_state_change(invalid_mode, invalid_mode);

    EXPECT_EQ(ada_backpressure_testing_state_log_invocations(), 1u);
    EXPECT_EQ(ada_backpressure_testing_last_state_previous(), invalid_mode);
    EXPECT_EQ(ada_backpressure_testing_last_state_next(), invalid_mode);
}

TEST(BackpressureState, nothrow_operator_delete__valid_pointer__then_calls_custom_delete) {
    void* ptr = ::operator new(32, std::nothrow);
    ASSERT_NE(ptr, nullptr);

    ::operator delete(ptr, std::nothrow);
}

TEST(BackpressureState, ring_pool_backpressure__exhaustion__then_drop_oldest_and_track_state) {
    ada_reset_tls_state();

    size_t arena_size = 0;
    auto arena = alloc_registry(arena_size);
    auto* registry = thread_registry_init_with_capacity(arena.get(), arena_size, 4);
    ASSERT_NE(registry, nullptr);

    ASSERT_NE(thread_registry_attach(registry), nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 0xABCDEFu);
    ASSERT_NE(lanes, nullptr);

    RingPool* pool = ring_pool_create(registry, lanes, 0);
    ASSERT_NE(pool, nullptr);

    ada_tls_state_t* tls = ada_get_tls_state();
    ASSERT_NE(tls, nullptr);
    ada_backpressure_state_t* bp = &tls->backpressure[0];

    EXPECT_EQ(ada_backpressure_state_get_mode(bp), ADA_BACKPRESSURE_STATE_NORMAL);

    uint32_t old_idx = UINT32_MAX;
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(ring_pool_swap_active(pool, &old_idx));
    }

    EXPECT_GE(ada_backpressure_state_get_drops(bp), 1u);
    EXPECT_THAT(ada_backpressure_state_get_mode(bp),
                AnyOf(ADA_BACKPRESSURE_STATE_DROPPING,
                      ADA_BACKPRESSURE_STATE_RECOVERY,
                      ADA_BACKPRESSURE_STATE_NORMAL));

    // Simulate stable recovery window.
    ada_backpressure_state_sample(bp, 3, 0);
    ada_backpressure_state_sample(bp, 3, 1100000000ull);
    EXPECT_EQ(ada_backpressure_state_get_mode(bp), ADA_BACKPRESSURE_STATE_NORMAL);

    ring_pool_destroy(pool);
    thread_registry_deinit(registry);
}

TEST(BackpressureConfig, config_from_env__overrides_and_validates) {
    setenv("BP_PRESSURE_THRESHOLD", "10", 1);
    setenv("BP_RECOVERY_THRESHOLD", "70", 1);
    setenv("BP_DROP_LOG_INTERVAL", "32", 1);

    ada_backpressure_config_t cfg = bp_config_from_env();

    EXPECT_EQ(cfg.pressure_threshold_percent, 10u);
    EXPECT_EQ(cfg.recovery_threshold_percent, 70u);
    EXPECT_EQ(cfg.drop_log_interval, 32u);
    EXPECT_TRUE(bp_config_validate(&cfg));

    unsetenv("BP_PRESSURE_THRESHOLD");
    unsetenv("BP_RECOVERY_THRESHOLD");
    unsetenv("BP_DROP_LOG_INTERVAL");
}

TEST(BackpressureConfig, config_validate__repairs_invalid_ranges) {
    ada_backpressure_config_t cfg = {};
    cfg.pressure_threshold_percent = 80u;
    cfg.recovery_threshold_percent = 60u; // invalid: less than pressure
    cfg.recovery_stable_ns = 0u;
    cfg.drop_log_interval = 0u;

    bool valid = bp_config_validate(&cfg);
    EXPECT_FALSE(valid);
    EXPECT_LT(cfg.pressure_threshold_percent, cfg.recovery_threshold_percent);
    EXPECT_GT(cfg.recovery_stable_ns, 0u);
    EXPECT_GT(cfg.drop_log_interval, 0u);
}

TEST(BackpressureConfig, config_validate__invalid_values__then_defaults_applied) {
    ada_backpressure_config_t cfg = {};
    cfg.pressure_threshold_percent = 0u;
    cfg.recovery_threshold_percent = 150u;
    cfg.drop_log_interval = 0u;
    cfg.recovery_stable_ns = 0u;

    EXPECT_FALSE(bp_config_validate(&cfg));
    EXPECT_EQ(cfg.pressure_threshold_percent, 25u);
    EXPECT_EQ(cfg.recovery_threshold_percent, 50u);
    EXPECT_EQ(cfg.drop_log_interval, 64u);
    EXPECT_EQ(cfg.recovery_stable_ns, 1000000000ull);

    EXPECT_FALSE(bp_config_validate(nullptr));
}

TEST(BackpressureConfig,
     config_validate__pressure_exceeds_recovery_upper_bound__then_defaults_restored) {
    ada_backpressure_config_t cfg = {};
    cfg.pressure_threshold_percent = 97u;
    cfg.recovery_threshold_percent = 95u;
    cfg.drop_log_interval = 32u;
    cfg.recovery_stable_ns = 2'000'000'000ull;

    EXPECT_FALSE(bp_config_validate(&cfg));
    EXPECT_EQ(cfg.pressure_threshold_percent, 25u);
    EXPECT_EQ(cfg.recovery_threshold_percent, 50u);
    EXPECT_EQ(cfg.drop_log_interval, 32u);
    EXPECT_EQ(cfg.recovery_stable_ns, 2'000'000'000ull);
}

TEST(BackpressureMetrics, export_metrics__captures_current_state) {
    ada_backpressure_config_t cfg = {};
    cfg.pressure_threshold_percent = 25u;
    cfg.recovery_threshold_percent = 50u;
    cfg.recovery_stable_ns = 1'000'000'000ull;
    cfg.drop_log_interval = 1024u;

    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, &cfg);
    ada_backpressure_state_set_total_rings(&state, 8);

    ada_backpressure_state_sample(&state, 0, 10);
    ada_backpressure_state_on_drop(&state, 64, 15);
    ada_backpressure_state_sample(&state, 0, 20);

    ada_backpressure_metrics_t metrics;
    std::memset(&metrics, 0, sizeof(metrics));
    bp_export_metrics(&state, &metrics);

    EXPECT_EQ(metrics.mode, ADA_BACKPRESSURE_STATE_DROPPING);
    EXPECT_GE(metrics.events_dropped, 1u);
    EXPECT_EQ(metrics.total_rings, 8u);
    EXPECT_LE(metrics.free_rings, metrics.total_rings);
    EXPECT_GT(metrics.last_drop_ns, 0u);
}

TEST(BackpressureMetrics, export_metrics__null_inputs__then_no_writes) {
    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, nullptr);

    ada_backpressure_metrics_t metrics = {};
    metrics.mode = ADA_BACKPRESSURE_STATE_DROPPING;
    metrics.transitions = 123u;

    bp_export_metrics(nullptr, &metrics);
    EXPECT_EQ(metrics.mode, ADA_BACKPRESSURE_STATE_DROPPING);
    EXPECT_EQ(metrics.transitions, 123u);

    bp_export_metrics(&state, nullptr);
}

TEST(BackpressureLogging, drop_logging__respects_interval) {
    ada_backpressure_config_t cfg = {};
    cfg.pressure_threshold_percent = 1u;
    cfg.recovery_threshold_percent = 2u;
    cfg.recovery_stable_ns = 1'000'000'000ull;
    cfg.drop_log_interval = 2u;

    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, &cfg);

    ada_backpressure_testing_reset_log_counters();
    ada_backpressure_state_on_drop(&state, 0, 1);
    EXPECT_EQ(ada_backpressure_testing_drop_log_invocations(), 0u);

    ada_backpressure_state_on_drop(&state, 0, 2);
    EXPECT_EQ(ada_backpressure_testing_drop_log_invocations(), 1u);

    ada_backpressure_state_on_drop(&state, 0, 3);
    EXPECT_EQ(ada_backpressure_testing_drop_log_invocations(), 1u);

    ada_backpressure_state_on_drop(&state, 0, 4);
    EXPECT_EQ(ada_backpressure_testing_drop_log_invocations(), 2u);
}

TEST(BackpressureLogging, state_change_logging__captures_transitions) {
    ada_backpressure_state_t state;
   ada_backpressure_state_init(&state, nullptr);
   ada_backpressure_state_set_total_rings(&state, 4u);

    ada_backpressure_testing_reset_log_counters();

    ada_backpressure_state_sample(&state, 0u, 10u);  // NORMAL -> PRESSURE
    ada_backpressure_state_sample(&state, 0u, 20u);  // PRESSURE -> DROPPING

    EXPECT_GE(ada_backpressure_testing_state_log_invocations(), 2u);
    EXPECT_EQ(ada_backpressure_testing_last_state_previous(), ADA_BACKPRESSURE_STATE_PRESSURE);
    EXPECT_EQ(ada_backpressure_testing_last_state_next(), ADA_BACKPRESSURE_STATE_DROPPING);
}

TEST(RingBufferPrivate, ring_buffer__status_and_reset__then_consistent) {
    RingBufferFixture fixture;

    EXPECT_TRUE(fixture.ring.is_empty());
    EXPECT_FALSE(fixture.ring.is_full());

    uint64_t value = 1u;
    auto* header = fixture.ring.get_header();
    ASSERT_NE(header, nullptr);
    uint32_t capacity = header->capacity;
    for (uint32_t i = 0; i < capacity - 1u; ++i) {
        EXPECT_TRUE(fixture.ring.write(&value));
        ++value;
    }
    EXPECT_FALSE(fixture.ring.write(&value));
    EXPECT_TRUE(fixture.ring.is_full());

    fixture.ring.reset();
    EXPECT_TRUE(fixture.ring.is_empty());
    EXPECT_FALSE(fixture.ring.is_full());
    EXPECT_FALSE(fixture.ring.drop_oldest());

    value = 42u;
    EXPECT_TRUE(fixture.ring.write(&value));
    EXPECT_TRUE(fixture.ring.drop_oldest());
    EXPECT_TRUE(fixture.ring.is_empty());
}

TEST(RingPoolBackpressure, ring_pool_create__allocation_failure__then_returns_null) {
    ada_reset_tls_state();

    size_t arena_size = 0;
    auto arena = alloc_registry(arena_size);
    auto* registry = thread_registry_init_with_capacity(arena.get(), arena_size, 4);
    ASSERT_NE(registry, nullptr);

    ASSERT_NE(thread_registry_attach(registry), nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 0x12345u);
    ASSERT_NE(lanes, nullptr);

    {
        FailNextNothrowNewGuard guard;
        EXPECT_EQ(ring_pool_create(registry, lanes, 0), nullptr);
    }

    RingPool* pool = ring_pool_create(registry, lanes, 0);
    ASSERT_NE(pool, nullptr);
    ring_pool_destroy(pool);

    thread_registry_unregister(lanes);
    thread_registry_deinit(registry);
}

TEST(RingPoolBackpressure,
     ring_pool_handle_exhaustion__without_submitted_ring__then_returns_false) {
    ada_reset_tls_state();

    size_t arena_size = 0;
    auto arena = alloc_registry(arena_size);
    auto* registry = thread_registry_init_with_capacity(arena.get(), arena_size, 4);
    ASSERT_NE(registry, nullptr);

    ASSERT_NE(thread_registry_attach(registry), nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 0x2222u);
    ASSERT_NE(lanes, nullptr);

    RingPool* pool = ring_pool_create(registry, lanes, 0);
    ASSERT_NE(pool, nullptr);

    auto* lane = get_lane(lanes, 0);
    ASSERT_NE(lane, nullptr);
    uint32_t capacity = lane->free_capacity;
    ASSERT_GT(capacity, 2u);
    set_lane_free_indices(lane, capacity - 1u, 1u, capacity);

    ada_backpressure_testing_reset_log_counters();
    ada_tls_state_t* tls = ada_get_tls_state();
    ASSERT_NE(tls, nullptr);
    ada_backpressure_state_t* bp = &tls->backpressure[0];

    EXPECT_FALSE(ring_pool_handle_exhaustion(pool));
    EXPECT_GE(ada_backpressure_testing_state_log_invocations(), 1u);
    EXPECT_NE(ada_backpressure_state_get_mode(bp), ADA_BACKPRESSURE_STATE_NORMAL);

    ring_pool_destroy(pool);
    thread_registry_unregister(lanes);
    thread_registry_deinit(registry);
}

TEST(BackpressureState, backpressure_state_reset__after_activity__then_counters_cleared) {
    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, nullptr);
    ada_backpressure_state_set_total_rings(&state, 4u);

    ada_backpressure_state_sample(&state, 0u, 10u);
    ada_backpressure_state_on_drop(&state, 32u, 20u);
    ada_backpressure_state_sample(&state, 0u, 30u);
    ada_backpressure_state_sample(&state, 4u, 40u);

    ada_backpressure_metrics_t before{};
    bp_export_metrics(&state, &before);
    EXPECT_NE(before.mode, ADA_BACKPRESSURE_STATE_NORMAL);
    EXPECT_GT(before.events_dropped, 0u);

    ada_backpressure_state_reset(&state);

    ada_backpressure_metrics_t after{};
    bp_export_metrics(&state, &after);
    EXPECT_EQ(after.mode, ADA_BACKPRESSURE_STATE_NORMAL);
    EXPECT_EQ(after.transitions, 0u);
    EXPECT_EQ(after.events_dropped, 0u);
    EXPECT_EQ(after.bytes_dropped, 0u);
    EXPECT_EQ(after.drop_sequences, 0u);
    EXPECT_EQ(after.free_rings, 0u);
    EXPECT_EQ(after.total_rings, 0u);
    EXPECT_EQ(after.low_watermark, 0u);
    EXPECT_EQ(after.last_drop_ns, 0u);
    EXPECT_EQ(after.last_recovery_ns, 0u);
    EXPECT_EQ(after.pressure_start_ns, 0u);
}

TEST(BackpressureState, backpressure_state__logging_paths__then_transitions_recorded) {
    ada_backpressure_state_t state;
    ada_backpressure_state_init(&state, nullptr);
    ada_backpressure_state_set_total_rings(&state, 4u);

    ada_backpressure_testing_reset_log_counters();

    ada_backpressure_state_sample(&state, 0u, 10u);  // NORMAL -> PRESSURE
    ada_backpressure_state_sample(&state, 4u, 20u);  // PRESSURE -> NORMAL
    ada_backpressure_state_sample(&state, 0u, 30u);  // NORMAL -> PRESSURE
    ada_backpressure_state_sample(&state, 0u, 40u);  // PRESSURE -> DROPPING
    ada_backpressure_state_sample(&state, 4u, 50u);  // DROPPING -> RECOVERY
    atomic_store_explicit(&state.recovery_candidate_ns, 0u, memory_order_relaxed);
    ada_backpressure_state_sample(&state, 4u, 60u);  // RECOVERY candidate timestamp set
    ada_backpressure_state_sample(&state, 0u, 70u);  // RECOVERY -> PRESSURE

    EXPECT_GE(ada_backpressure_testing_state_log_invocations(), 6u);
    EXPECT_EQ(ada_backpressure_testing_last_state_previous(), ADA_BACKPRESSURE_STATE_RECOVERY);
    EXPECT_EQ(ada_backpressure_testing_last_state_next(), ADA_BACKPRESSURE_STATE_PRESSURE);
}
