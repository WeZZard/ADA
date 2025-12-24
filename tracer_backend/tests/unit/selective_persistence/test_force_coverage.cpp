// Final attempt to achieve 100% coverage using compile-time test hooks
// This test file uses preprocessor macros to enable test-only code paths

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <filesystem>

// Define test mode to enable special test hooks
#define ADA_TESTING_MODE 1

extern "C" {
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/atf/atf_thread_writer.h>
#include <tracer_backend/cli_parser.h>
}

#include <tracer_backend/selective_persistence/detail_lane_control.h>
#include <tracer_backend/selective_persistence/marking_policy.h>
#include <tracer_backend/selective_persistence/persistence_window.h>

#include "thread_registry_private.h"

namespace {

class ForceCoverageTest : public ::testing::Test {
protected:
    std::unique_ptr<uint8_t[]> arena;
    ThreadRegistry* registry = nullptr;
    ThreadLaneSet* lanes = nullptr;
    RingPool* pool = nullptr;
    MarkingPolicy* policy = nullptr;
    std::string temp_dir;

    void SetUp() override {
        size_t registry_size = thread_registry_calculate_memory_size_with_capacity(2);
        arena = std::make_unique<uint8_t[]>(registry_size);
        std::memset(arena.get(), 0, registry_size);
        registry = thread_registry_init_with_capacity(arena.get(), registry_size, 2);

        if (!thread_registry_attach(registry)) {
            throw std::runtime_error("Failed to attach registry");
        }
        lanes = thread_registry_register(registry, 0xABCD);
        if (!lanes) {
            throw std::runtime_error("Failed to register thread lanes");
        }

        pool = ring_pool_create(registry, lanes, 1);

        AdaMarkingPatternDesc pattern{};
        pattern.target = ADA_MARKING_TARGET_SYMBOL;
        pattern.match = ADA_MARKING_MATCH_LITERAL;
        pattern.pattern = "test";
        pattern.case_sensitive = true;
        policy = marking_policy_create(&pattern, 1);

        // Create temp directory
        auto temp_path = std::filesystem::temp_directory_path();
        temp_dir = (temp_path / ("force_coverage_" + std::to_string(getpid()))).string();
        std::filesystem::create_directories(temp_dir);
    }

    void TearDown() override {
        if (policy) marking_policy_destroy(policy);
        if (pool) ring_pool_destroy(pool);
        if (registry) thread_registry_deinit(registry);

        if (!temp_dir.empty() && std::filesystem::exists(temp_dir)) {
            std::filesystem::remove_all(temp_dir);
        }
    }
};

// Test all the remaining uncovered lines in marking_policy
TEST_F(ForceCoverageTest, marking_policy__all_edge_cases) {
    // Test case-insensitive char mismatch (lines 45-46)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_SYMBOL;
        desc.match = ADA_MARKING_MATCH_LITERAL;
        desc.case_sensitive = false;
        desc.pattern = "AbCdE";
        MarkingPolicy* pol = marking_policy_create(&desc, 1);

        AdaMarkingProbe probe{};
        probe.symbol_name = "AbCdF";  // Last char different
        EXPECT_FALSE(marking_policy_match(pol, &probe));

        marking_policy_destroy(pol);
    }

    // Test null probe and null symbol_name (lines 91-92)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_SYMBOL;
        desc.match = ADA_MARKING_MATCH_LITERAL;
        desc.pattern = "test";
        MarkingPolicy* pol = marking_policy_create(&desc, 1);

        EXPECT_FALSE(marking_policy_match(pol, nullptr));

        AdaMarkingProbe probe{};
        probe.symbol_name = nullptr;
        EXPECT_FALSE(marking_policy_match(pol, &probe));

        marking_policy_destroy(pol);
    }

    // Test module name case-insensitive mismatch (lines 104-105)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_SYMBOL;
        desc.match = ADA_MARKING_MATCH_LITERAL;
        desc.case_sensitive = false;
        desc.pattern = "func";
        desc.module_name = "ModuleA";
        MarkingPolicy* pol = marking_policy_create(&desc, 1);

        AdaMarkingProbe probe{};
        probe.symbol_name = "func";
        probe.module_name = "ModuleB";
        EXPECT_FALSE(marking_policy_match(pol, &probe));

        marking_policy_destroy(pol);
    }

    // Test empty pattern (lines 110-111)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_SYMBOL;
        desc.match = ADA_MARKING_MATCH_LITERAL;
        desc.pattern = "";  // Empty
        MarkingPolicy* pol = marking_policy_create(&desc, 1);

        AdaMarkingProbe probe{};
        probe.symbol_name = "anything";
        EXPECT_FALSE(marking_policy_match(pol, &probe));

        marking_policy_destroy(pol);
    }

    // Test case-sensitive exact match (lines 115-116)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_SYMBOL;
        desc.match = ADA_MARKING_MATCH_LITERAL;
        desc.case_sensitive = true;
        desc.pattern = "ExactMatch";
        MarkingPolicy* pol = marking_policy_create(&desc, 1);
        marking_policy_set_enabled(pol, true); // Enable the policy

        AdaMarkingProbe probe1{};
        probe1.symbol_name = "ExactMatch";
        EXPECT_TRUE(marking_policy_match(pol, &probe1));

        AdaMarkingProbe probe2{};
        probe2.symbol_name = "exactmatch";
        EXPECT_FALSE(marking_policy_match(pol, &probe2));

        marking_policy_destroy(pol);
    }

    // Test regex with failed compilation (lines 121-122)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_SYMBOL;
        desc.match = ADA_MARKING_MATCH_REGEX;
        desc.pattern = "[[[[[";  // Invalid regex
        MarkingPolicy* pol = marking_policy_create(&desc, 1);
        marking_policy_set_enabled(pol, true); // Enable the policy

        AdaMarkingProbe probe{};
        probe.symbol_name = "[[[[[";
        // Should fall back to literal match
        EXPECT_TRUE(marking_policy_match(pol, &probe));

        marking_policy_destroy(pol);
    }

    // Test message empty pattern (lines 132-133)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_MESSAGE;
        desc.match = ADA_MARKING_MATCH_LITERAL;
        desc.pattern = "";
        MarkingPolicy* pol = marking_policy_create(&desc, 1);

        AdaMarkingProbe probe{};
        probe.message = "test";
        EXPECT_FALSE(marking_policy_match(pol, &probe));

        marking_policy_destroy(pol);
    }

    // Test message regex without compiled pattern (lines 140-141)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = ADA_MARKING_TARGET_MESSAGE;
        desc.match = ADA_MARKING_MATCH_REGEX;
        desc.pattern = "((((((";  // Invalid regex
        MarkingPolicy* pol = marking_policy_create(&desc, 1);

        AdaMarkingProbe probe{};
        probe.message = "test";
        EXPECT_FALSE(marking_policy_match(pol, &probe));

        marking_policy_destroy(pol);
    }

    // Test default case in switch (line 152)
    {
        AdaMarkingPatternDesc desc{};
        desc.target = static_cast<AdaMarkingTarget>(999);  // Invalid
        desc.match = ADA_MARKING_MATCH_LITERAL;
        desc.pattern = "test";
        MarkingPolicy* pol = marking_policy_create(&desc, 1);

        AdaMarkingProbe probe{};
        probe.symbol_name = "test";
        EXPECT_FALSE(marking_policy_match(pol, &probe));

        marking_policy_destroy(pol);
    }

    // Test marking_policy_from_triggers (lines 188-189, 200-201)
    {
        // Null triggers
        MarkingPolicy* pol1 = marking_policy_from_triggers(nullptr);
        EXPECT_NE(pol1, nullptr);
        marking_policy_destroy(pol1);

        // Null entries
        TriggerList list1{};
        list1.count = 5;
        list1.entries = nullptr;
        MarkingPolicy* pol2 = marking_policy_from_triggers(&list1);
        EXPECT_NE(pol2, nullptr);
        marking_policy_destroy(pol2);

        // Empty and null symbol names
        TriggerDefinition triggers[2];
        triggers[0].type = TRIGGER_TYPE_SYMBOL;
        triggers[0].symbol_name = nullptr;
        triggers[0].module_name = nullptr;
        triggers[0].is_regex = false;
        triggers[0].case_sensitive = true;

        triggers[1].type = TRIGGER_TYPE_SYMBOL;
        triggers[1].symbol_name = "";
        triggers[1].module_name = nullptr;
        triggers[1].is_regex = false;
        triggers[1].case_sensitive = true;

        TriggerList list2{};
        list2.count = 2;
        list2.entries = triggers;

        MarkingPolicy* pol3 = marking_policy_from_triggers(&list2);
        EXPECT_NE(pol3, nullptr);

        AdaMarkingProbe probe{};
        probe.symbol_name = "";
        EXPECT_FALSE(marking_policy_match(pol3, &probe));

        marking_policy_destroy(pol3);
    }
}

// Test the uncovered lines in detail_lane_control
TEST_F(ForceCoverageTest, detail_lane_control__force_all_errors) {
    // We can't easily force thread_lanes_get_detail_lane to return null
    // or force ring_pool_swap_active to fail without modifying the source
    // But we can at least test all the other error paths

    DetailLaneControl* control = detail_lane_control_create(registry, lanes, pool, policy);
    ASSERT_NE(control, nullptr);

    // Test window metadata write failures
    SelectivePersistenceWindow window{};
    window.start_timestamp_ns = 1000;
    window.end_timestamp_ns = 2000;
    window.total_events = 100;
    window.marked_events = 10;
    window.mark_seen = true;

    // Null writer
    EXPECT_FALSE(detail_lane_control_write_window_metadata(control, &window, nullptr));

    // Invalid writer paths
    AtfV4Writer writer{};
    writer.session_dir[0] = '\0';  // Empty
    EXPECT_FALSE(detail_lane_control_write_window_metadata(control, &window, &writer));

    // Non-existent path
    std::snprintf(writer.session_dir, sizeof(writer.session_dir),
                  "/this/path/should/not/exist/anywhere/%d", getpid());
    EXPECT_FALSE(detail_lane_control_write_window_metadata(control, &window, &writer));

    detail_lane_control_destroy(control);
}

}  // namespace