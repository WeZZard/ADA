#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/control_block_ipc.h>
}

// Helper to produce ns values (kept for consistency/readability)
static inline uint64_t ns(uint64_t v) { return v; }

// 1) registry_version__roundtrip__then_reads_set_value
TEST(control_block_ipc__registry_version__then_reads_set_value, unit) {
    ControlBlock cb = {};

    // Starts at 0
    EXPECT_EQ(cb_get_registry_version(&cb), 0u);

    // Write and read back a couple of distinct versions
    cb_set_registry_version(&cb, 1u);
    EXPECT_EQ(cb_get_registry_version(&cb), 1u);

    cb_set_registry_version(&cb, 42u);
    EXPECT_EQ(cb_get_registry_version(&cb), 42u);
}

// 2) registry_ready__roundtrip__then_reads_set_value
TEST(control_block_ipc__registry_ready__then_reads_set_value, unit) {
    ControlBlock cb = {};

    // Starts at 0
    EXPECT_EQ(cb_get_registry_ready(&cb), 0u);

    // Set and read back
    cb_set_registry_ready(&cb, 1u);
    EXPECT_EQ(cb_get_registry_ready(&cb), 1u);

    cb_set_registry_ready(&cb, 0u);
    EXPECT_EQ(cb_get_registry_ready(&cb), 0u);
}

// 3) registry_epoch__roundtrip__then_reads_set_value
TEST(control_block_ipc__registry_epoch__then_reads_set_value, unit) {
    ControlBlock cb = {};

    // Starts at 0
    EXPECT_EQ(cb_get_registry_epoch(&cb), 0u);

    // Write and read back different epochs
    cb_set_registry_epoch(&cb, 1u);
    EXPECT_EQ(cb_get_registry_epoch(&cb), 1u);

    cb_set_registry_epoch(&cb, 100u);
    EXPECT_EQ(cb_get_registry_epoch(&cb), 100u);
}

// 4) registry_mode__roundtrip__then_reads_set_value
TEST(control_block_ipc__registry_mode__then_reads_set_value, unit) {
    ControlBlock cb = {};

    // Starts at 0
    EXPECT_EQ(cb_get_registry_mode(&cb), 0u);

    // Test different modes
    cb_set_registry_mode(&cb, REGISTRY_MODE_DUAL_WRITE);
    EXPECT_EQ(cb_get_registry_mode(&cb), (uint32_t)REGISTRY_MODE_DUAL_WRITE);

    cb_set_registry_mode(&cb, REGISTRY_MODE_GLOBAL_ONLY);
    EXPECT_EQ(cb_get_registry_mode(&cb), (uint32_t)REGISTRY_MODE_GLOBAL_ONLY);

    cb_set_registry_mode(&cb, REGISTRY_MODE_PER_THREAD_ONLY);
    EXPECT_EQ(cb_get_registry_mode(&cb), (uint32_t)REGISTRY_MODE_PER_THREAD_ONLY);
}

// 5) heartbeat_ns__roundtrip__then_reads_set_value
TEST(control_block_ipc__heartbeat_ns__then_reads_set_value, unit) {
    ControlBlock cb = {};

    // Starts at 0
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), 0ull);

    // Set and read back timestamps
    cb_set_heartbeat_ns(&cb, ns(1000000000ull)); // 1 second
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), ns(1000000000ull));

    cb_set_heartbeat_ns(&cb, ns(5000000000ull)); // 5 seconds
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), ns(5000000000ull));
}

// 6) heartbeat_ns__monotonic_update__then_clamps_to_max
TEST(control_block_ipc__heartbeat_ns__monotonic_update__then_clamps_to_max, unit) {
    ControlBlock cb = {};

    cb_set_heartbeat_ns(&cb, ns(100ull));
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), ns(100ull));

    // Update forward (should advance).
    cb_update_heartbeat_ns(&cb, ns(150ull));
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), ns(150ull));

    // Update backward (should be ignored).
    cb_update_heartbeat_ns(&cb, ns(120ull));
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), ns(150ull));

    // Update equal (should be no-op).
    cb_update_heartbeat_ns(&cb, ns(150ull));
    EXPECT_EQ(cb_get_heartbeat_ns(&cb), ns(150ull));
}

// 7) counters__inc_and_get__then_counters_match
TEST(control_block_ipc__counters__then_update_and_read, unit) {
    ControlBlock cb = {};

    // mode_transitions: verify getter reflects changes
    EXPECT_EQ(cb_get_mode_transitions(&cb), 0ull);
    // Bump transitions in a simple loop
    for (int i = 0; i < 3; ++i) {
        cb_inc_mode_transitions(&cb);
    }
    EXPECT_EQ(cb_get_mode_transitions(&cb), 3ull);

    // fallback_events: exercise both inc and get helpers
    EXPECT_EQ(cb_get_fallback_events(&cb), 0ull);
    cb_inc_fallback_events(&cb);
    cb_inc_fallback_events(&cb);
    EXPECT_EQ(cb_get_fallback_events(&cb), 2ull);

    // Another round to ensure accumulation
    cb_inc_fallback_events(&cb);
    EXPECT_EQ(cb_get_fallback_events(&cb), 3ull);
}
