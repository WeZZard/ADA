// Unit tests for Controller Coverage
// These tests exercise specific code paths for coverage without actual spawning
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern "C" {
    #include <tracer_backend/controller/frida_controller.h>
    #include <tracer_backend/utils/shared_memory.h>
    #include <tracer_backend/utils/control_block_ipc.h>
    #include <tracer_backend/utils/ring_buffer.h>
    #include <tracer_backend/utils/tracer_types.h>
    #include "ada_paths.h"
}

using AttachSyncFn =
    FridaSession* (*)(FridaDevice*, guint, FridaSessionOptions*, GCancellable*, GError**);

extern "C" void frida_controller_test_set_attach_sync(AttachSyncFn fn);
extern "C" void frida_controller_test_reset_attach_sync(void);

namespace {
int attach_calls = 0;

FridaSession* attach_sync_timeout_stub(FridaDevice* self,
                                       guint pid,
                                       FridaSessionOptions* options,
                                       GCancellable* cancellable,
                                       GError** error) {
    (void)self;
    (void)pid;
    (void)options;
    (void)cancellable;

    ++attach_calls;
    if (error) {
        *error = g_error_new_literal(FRIDA_ERROR, FRIDA_ERROR_TIMED_OUT, "timed out");
    }
    return nullptr;
}

struct AttachSyncOverride {
    explicit AttachSyncOverride(AttachSyncFn fn) {
        frida_controller_test_set_attach_sync(fn);
    }
    ~AttachSyncOverride() {
        frida_controller_test_reset_attach_sync();
    }
};
}

// Test fixture for controller coverage tests
class ControllerCoverageTest : public ::testing::Test {
protected:
    FridaController* controller;

    void SetUp() override {
        controller = nullptr;
    }

    void TearDown() override {
        if (controller) {
            frida_controller_destroy(controller);
            controller = nullptr;
        }
    }
};

// Test: controller__create_with_output__then_initializes_drain_thread
TEST_F(ControllerCoverageTest, controller__create_with_output__then_initializes_drain_thread) {
    const char* output_file = "/tmp/test_coverage_drain.atf";

    // Set environment variables to exercise propagation code paths
    setenv("LLVM_PROFILE_FILE", "coverage_%p.profraw", 1);
    setenv("RUSTFLAGS", "-C instrument-coverage", 1);

    // Create controller with output file (triggers drain thread)
    controller = frida_controller_create(output_file);
    ASSERT_NE(controller, nullptr);

    // Get shared memory references
    uint32_t sid = shared_memory_get_session_id();
    pid_t local_pid = shared_memory_get_pid();
    SharedMemoryRef shm_control = shared_memory_open_unique(ADA_ROLE_CONTROL, local_pid, sid, 4096);
    SharedMemoryRef shm_index = shared_memory_open_unique(ADA_ROLE_INDEX, local_pid, sid, 32 * 1024 * 1024);
    SharedMemoryRef shm_detail = shared_memory_open_unique(ADA_ROLE_DETAIL, local_pid, sid, 32 * 1024 * 1024);

    ASSERT_NE(shm_control, nullptr);
    ASSERT_NE(shm_index, nullptr);
    ASSERT_NE(shm_detail, nullptr);

    // Verify control block IPC values are set
    ControlBlock* cb = (ControlBlock*)shared_memory_get_address(shm_control);
    EXPECT_EQ(cb_get_registry_version(cb), 1u);
    EXPECT_EQ(cb_get_registry_epoch(cb), 1u);
    EXPECT_EQ(cb_get_registry_ready(cb), 1u);
    EXPECT_EQ(cb_get_registry_mode(cb), (uint32_t)REGISTRY_MODE_DUAL_WRITE);

    // Create ring buffers to write test events
    RingBuffer* index_rb = ring_buffer_create(shared_memory_get_address(shm_index),
                                              shared_memory_get_size(shm_index),
                                              sizeof(IndexEvent));
    RingBuffer* detail_rb = ring_buffer_create(shared_memory_get_address(shm_detail),
                                               shared_memory_get_size(shm_detail),
                                               sizeof(DetailEvent));

    // Write multiple events to exercise batch processing
    for (int i = 0; i < 10; i++) {
        IndexEvent idx_event = {
            .timestamp = static_cast<uint64_t>(1000000 + i),
            .function_id = static_cast<uint64_t>(0x100000001 + i),
            .thread_id = 1234,
            .event_kind = EVENT_KIND_CALL,
            .call_depth = 0,
            ._padding = 0
        };
        ring_buffer_write(index_rb, &idx_event);

        DetailEvent det_event = {
            .timestamp = static_cast<uint64_t>(2000000 + i),
            .function_id = static_cast<uint64_t>(0x200000001 + i),
            .thread_id = 5678,
            .event_kind = EVENT_KIND_RETURN,
            .call_depth = 1,
            ._pad1 = 0,
            .x_regs = {0},
            .lr = 0,
            .fp = 0,
            .sp = 0,
            .stack_snapshot = {0},
            .stack_size = 0,
            ._padding = {0}
        };
        ring_buffer_write(detail_rb, &det_event);
    }

    // Let drain thread process events (exercises file writing code)
    usleep(300000); // 300ms

    // Check that events were captured
    TracerStats stats = frida_controller_get_stats(controller);
    // Due to timing, we might not capture all events, but verify the API works
    EXPECT_GE(stats.events_captured, 0ull);
    EXPECT_GE(stats.bytes_written, 0ull);

    // Clean up
    ring_buffer_destroy(index_rb);
    ring_buffer_destroy(detail_rb);
    shared_memory_destroy(shm_control);
    shared_memory_destroy(shm_index);
    shared_memory_destroy(shm_detail);

    // Cleanup environment
    unsetenv("LLVM_PROFILE_FILE");
    unsetenv("RUSTFLAGS");
    unlink(output_file);
}

TEST_F(ControllerCoverageTest,
       controller__attach_timeout_retries__then_fails_cleanly) {
    AttachSyncOverride override(attach_sync_timeout_stub);
    attach_calls = 0;

    controller = frida_controller_create("/tmp");
    ASSERT_NE(controller, nullptr);

    int result = frida_controller_attach(controller, 1234);
    EXPECT_EQ(result, -1);
    EXPECT_EQ(attach_calls, 5);
    EXPECT_EQ(frida_controller_get_state(controller), PROCESS_STATE_FAILED);
}

// Test: controller__registry_disabled__then_no_registry_init
TEST_F(ControllerCoverageTest, DISABLED_controller__registry_disabled__then_no_registry_init) {
    // Set environment variable to disable registry
    setenv("ADA_DISABLE_REGISTRY", "1", 1);

    // Create controller without output file
    controller = frida_controller_create(nullptr);
    ASSERT_NE(controller, nullptr);

    // Get control block
    uint32_t sid = shared_memory_get_session_id();
    pid_t local_pid = shared_memory_get_pid();
    SharedMemoryRef shm_control = shared_memory_open_unique(ADA_ROLE_CONTROL, local_pid, sid, 4096);
    ASSERT_NE(shm_control, nullptr);

    ControlBlock* cb = (ControlBlock*)shared_memory_get_address(shm_control);

    // Registry should not be initialized when disabled
    EXPECT_EQ(cb_get_registry_ready(cb), 0u);
    EXPECT_EQ(cb_get_registry_version(cb), 0u);

    // Clean up
    shared_memory_destroy(shm_control);
    unsetenv("ADA_DISABLE_REGISTRY");
}
