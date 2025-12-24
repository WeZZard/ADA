#include <gtest/gtest.h>

#include <ctime>
#include <cstring>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/utils/thread_registry.h>
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
        lanes = thread_registry_register(registry, 0xFFAA);
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

void fill_ring_to_capacity(RingBufferHeader* header, size_t event_size) {
    ASSERT_NE(header, nullptr);
    std::vector<uint8_t> event(event_size, 0xCD);
    while (ring_buffer_write_raw(header, event_size, event.data())) {
        // spin until full
    }
}

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

}  // namespace

TEST(SelectivePersistenceIntegrationTest, full_cycle__metadata_persisted__then_accessible) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 10);

    AdaMarkingProbe probe_marked = message_probe("ERROR: integration");
    EXPECT_TRUE(detail_lane_control_mark_event(control, &probe_marked, 20));
    AdaMarkingProbe probe_unmarked = message_probe("info");
    EXPECT_FALSE(detail_lane_control_mark_event(control, &probe_unmarked, 30));

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));
    ASSERT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 40, &window));

    uint32_t swapped_ring = UINT32_MAX;
    ASSERT_TRUE(detail_lane_control_perform_selective_swap(control, &swapped_ring));
    EXPECT_NE(swapped_ring, UINT32_MAX);

    auto temp_base = std::filesystem::temp_directory_path();
    auto unique_component = std::to_string(static_cast<long long>(std::time(nullptr))) +
                            "_" + std::to_string(static_cast<long long>(::getpid()));
    std::filesystem::path temp_root = temp_base / ("ada_sp_integration_" + unique_component);
    std::filesystem::create_directories(temp_root);

    std::string root_str = temp_root.string();
    AtfV4Writer writer{};
    AtfV4WriterConfig cfg{};
    cfg.output_root = root_str.c_str();
    cfg.session_label = "selective_persistence_test";
    cfg.pid = 4242;
    cfg.session_id = 424242;
    cfg.enable_manifest = true;

    ASSERT_EQ(0, atf_v4_writer_init(&writer, &cfg));

    ASSERT_TRUE(detail_lane_control_write_window_metadata(control, &window, &writer));

    detail_lane_control_mark_dump_complete(control, 50);

    std::filesystem::path metadata_path = std::filesystem::path(writer.session_dir) / "window_metadata.jsonl";
    ASSERT_TRUE(std::filesystem::exists(metadata_path));

    std::ifstream metadata_file(metadata_path);
    ASSERT_TRUE(metadata_file.is_open());
    std::string line;
    std::getline(metadata_file, line);
    metadata_file.close();

    EXPECT_NE(line.find("\"window_id\":"), std::string::npos);
    EXPECT_NE(line.find("\"total_events\":2"), std::string::npos);
    EXPECT_NE(line.find("\"marked_events\":1"), std::string::npos);
    EXPECT_NE(line.find("\"mark_seen\":true"), std::string::npos);

    atf_v4_writer_deinit(&writer);
    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);

    std::filesystem::remove_all(temp_root);
}

TEST(SelectivePersistenceIntegrationTest, window_wrappers__multi_cycle__then_metrics_consistent) {
    RegistryFixture fx;

    AdaMarkingPatternDesc pattern = default_pattern();
    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_window(control, 100);

    AdaMarkingProbe probe_marked = message_probe("ERROR: integration pending");
    ASSERT_TRUE(detail_lane_control_mark_event(control, &probe_marked, 120));

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));
    ASSERT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 130, &window));

    uint32_t swapped_ring = UINT32_MAX;
    ASSERT_TRUE(detail_lane_control_perform_selective_swap(control, &swapped_ring));
    EXPECT_NE(swapped_ring, UINT32_MAX);

    detail_lane_control_record_dump(control, 140);

    SelectivePersistenceMetrics metrics{};
    detail_lane_control_collect_metrics(control, &metrics);
    EXPECT_EQ(metrics.selective_dumps_performed, 1u);
    EXPECT_EQ(metrics.windows_discarded, 0u);
    EXPECT_GE(metrics.avg_window_duration_ns, window.end_timestamp_ns - window.start_timestamp_ns);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}

TEST(SelectivePersistenceIntegrationTest, symbol_regex_case_insensitive__then_dump_generated) {
    RegistryFixture fx;

    std::string pattern_storage = "Critical::.*";
    std::string module_storage = "IntegrationModule";

    AdaMarkingPatternDesc pattern{};
    pattern.target = ADA_MARKING_TARGET_SYMBOL;
    pattern.match = ADA_MARKING_MATCH_REGEX;
    pattern.case_sensitive = false;
    pattern.pattern = pattern_storage.c_str();
    pattern.module_name = module_storage.c_str();

    MarkingPolicy* policy = marking_policy_create(&pattern, 1);
    ASSERT_NE(policy, nullptr);
    marking_policy_set_enabled(policy, true);

    DetailLaneControl* control = detail_lane_control_create(fx.registry, fx.lanes, fx.detail_pool, policy);
    ASSERT_NE(control, nullptr);
    detail_lane_control_start_new_window(control, 200);

    AdaMarkingProbe unmatched{};
    unmatched.symbol_name = "Critical::Trigger";
    unmatched.module_name = nullptr;
    EXPECT_FALSE(detail_lane_control_mark_event(control, &unmatched, 205));

    AdaMarkingProbe matched{};
    matched.symbol_name = "critical::trigger";
    matched.module_name = "integrationmodule";
    EXPECT_TRUE(detail_lane_control_mark_event(control, &matched, 210));

    RingBufferHeader* header = ring_pool_get_active_header(fx.detail_pool);
    ASSERT_NE(header, nullptr);
    fill_ring_to_capacity(header, sizeof(DetailEvent));
    ASSERT_TRUE(detail_lane_control_should_dump(control));

    SelectivePersistenceWindow window{};
    ASSERT_TRUE(detail_lane_control_close_window_for_dump(control, 220, &window));
    EXPECT_TRUE(window.mark_seen);
    EXPECT_EQ(window.marked_events, 1u);
    EXPECT_EQ(window.total_events, 2u);

    marking_policy_destroy(policy);
    detail_lane_control_destroy(control);
}
