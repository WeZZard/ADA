#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <stdatomic.h>
#include <thread>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <tracer_backend/controller/shutdown.h>
#include "../../../src/drain_thread/drain_thread_private.h"
#include <tracer_backend/atf/atf_thread_writer.h>
#include <tracer_backend/utils/thread_registry.h>

extern "C" int timer_cancel(void);

static int g_timer_cancel_calls = 0;

extern "C" int timer_cancel(void) {
    ++g_timer_cancel_calls;
    return 0;
}

static int default_timer_cancel(void) {
    return timer_cancel();
}

namespace {

std::atomic<int> g_fsync_calls{0};
std::atomic<int> g_stop_drain_calls{0};
std::atomic<int> g_fsync_error_count{0};
std::atomic<bool> g_fsync_should_fail{false};
std::atomic<int> g_clock_gettime_calls{0};
std::atomic<bool> g_clock_gettime_should_fail{false};

}  // namespace

extern "C" int fsync(int fd) {
    (void)fd;
    g_fsync_calls.fetch_add(1, std::memory_order_relaxed);
    if (g_fsync_should_fail.load(std::memory_order_relaxed)) {
        g_fsync_error_count.fetch_add(1, std::memory_order_relaxed);
        errno = EIO;
        return -1;
    }
    return 0;
}

extern "C" int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    g_clock_gettime_calls.fetch_add(1, std::memory_order_relaxed);
    if (g_clock_gettime_should_fail.load(std::memory_order_relaxed)) {
        errno = EINVAL;
        return -1;
    }
    if (tp) {
        tp->tv_sec = 1234567890;
        tp->tv_nsec = 123456789;
    }
    return 0;
}

namespace {

class ShutdownManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_timer_cancel_calls = 0;
        g_fsync_calls.store(0, std::memory_order_relaxed);
        g_fsync_error_count.store(0, std::memory_order_relaxed);
        g_fsync_should_fail.store(false, std::memory_order_relaxed);
        g_clock_gettime_calls.store(0, std::memory_order_relaxed);
        g_clock_gettime_should_fail.store(false, std::memory_order_relaxed);
        shutdown_state_init(&state_, MAX_THREADS);
        ASSERT_EQ(shutdown_manager_init(&manager_, &state_, nullptr, nullptr, nullptr), 0);
        ASSERT_EQ(signal_handler_init(&handler_, &manager_), 0);
        handler_installed_ = false;
        manager_registered_ = false;
        pipefd_[0] = -1;
        pipefd_[1] = -1;
    }

    void TearDown() override {
        if (handler_installed_) {
            signal_handler_uninstall(&handler_);
        }
        if (manager_registered_) {
            shutdown_manager_unregister_global();
        }
        if (pipefd_[0] >= 0) {
            close(pipefd_[0]);
        }
        if (pipefd_[1] >= 0) {
            close(pipefd_[1]);
        }
    }

    void install_handler() {
        ASSERT_FALSE(handler_installed_);
        ASSERT_EQ(signal_handler_install(&handler_), 0);
        handler_installed_ = true;
    }

    void register_manager() {
        ASSERT_FALSE(manager_registered_);
        shutdown_manager_register_global(&manager_);
        manager_registered_ = true;
    }

    void configure_wakeup_pipe() {
        ASSERT_EQ(pipe(pipefd_), 0);
        shutdown_manager_set_wakeup_fds(&manager_, pipefd_[0], pipefd_[1]);
    }

    ShutdownManager manager_{};
    ShutdownState state_{};
    SignalHandler handler_{};
    bool handler_installed_ = false;
    bool manager_registered_ = false;
    int pipefd_[2] = {-1, -1};
};

class ShutdownManagerSummaryTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_fsync_calls.store(0, std::memory_order_relaxed);
        g_stop_drain_calls.store(0, std::memory_order_relaxed);

        shutdown_state_init(&state_, MAX_THREADS);
        ASSERT_EQ(shutdown_manager_init(&manager_, &state_, nullptr, nullptr, nullptr), 0);

        pthread_mutex_init(&drain_.lifecycle_lock, nullptr);
        atomic_init(&drain_.state, DRAIN_STATE_STOPPED);

        pthread_mutex_lock(&drain_.lifecycle_lock);
        drain_.atf_writer = &writer_;
        pthread_mutex_unlock(&drain_.lifecycle_lock);

        shutdown_manager_set_drain_thread(&manager_, &drain_);

        ShutdownOps ops;
        memset(&ops, 0, sizeof(ops));
        ops.stop_drain = &ShutdownManagerSummaryTest::StopDrain;
        shutdown_manager_set_ops(&manager_, &ops);

        writer_.events_fd = kTestEventsFd;
        writer_.manifest_fp = tmpfile();
        ASSERT_NE(writer_.manifest_fp, nullptr);
        writer_.manifest_enabled = true;
        writer_.initialized = true;
        writer_.finalized = true;

        atomic_init(&writer_.event_count, (uint_fast64_t)0);
        atomic_init(&writer_.bytes_written, (uint_fast64_t)0);
    }

    void TearDown() override {
        if (writer_.manifest_fp) {
            fclose(writer_.manifest_fp);
            writer_.manifest_fp = nullptr;
        }
        pthread_mutex_destroy(&drain_.lifecycle_lock);
    }

    void mark_thread_active(uint32_t slot, uint64_t pending_events) {
        shutdown_state_mark_active(&state_, slot);
        shutdown_state_record_pending(&state_, slot, pending_events);
    }

    static int StopDrain(DrainThread* drain) {
        g_stop_drain_calls.fetch_add(1, std::memory_order_relaxed);
        if (drain) {
            atomic_store_explicit(&drain->state, DRAIN_STATE_STOPPING, memory_order_release);
            atomic_store_explicit(&drain->state, DRAIN_STATE_STOPPED, memory_order_release);
        }
        return 0;
    }

    ShutdownManager manager_{};
    ShutdownState state_{};
    AtfV4Writer writer_{};
    DrainThread drain_{};

    static constexpr int kTestEventsFd = 42;
};

}  // namespace

TEST_F(ShutdownManagerTest, SignalHandlerInstall) {
    install_handler();

    struct sigaction sa_int;
    ASSERT_EQ(sigaction(SIGINT, nullptr, &sa_int), 0);
    EXPECT_NE(sa_int.sa_handler, SIG_DFL);
#ifdef SA_RESTART
    EXPECT_NE(sa_int.sa_flags & SA_RESTART, 0);
#endif

    struct sigaction sa_term;
    ASSERT_EQ(sigaction(SIGTERM, nullptr, &sa_term), 0);
    EXPECT_NE(sa_term.sa_handler, SIG_DFL);
#ifdef SA_RESTART
    EXPECT_NE(sa_term.sa_flags & SA_RESTART, 0);
#endif
}

TEST_F(ShutdownManagerTest, SigintTriggersShutdown) {
    configure_wakeup_pipe();
    register_manager();
    install_handler();

    ASSERT_FALSE(shutdown_manager_is_shutdown_requested(&manager_));

    raise(SIGINT);
    usleep(5000);

    EXPECT_TRUE(shutdown_manager_is_shutdown_requested(&manager_));
    EXPECT_EQ(g_timer_cancel_calls, 1);
    EXPECT_EQ(shutdown_manager_get_last_reason(&manager_), SHUTDOWN_REASON_SIGNAL);
    EXPECT_EQ(shutdown_manager_get_last_signal(&manager_), SIGINT);
    EXPECT_EQ(shutdown_manager_get_request_count(&manager_), 1u);

    uint64_t value = 0;
    ssize_t read_bytes = read(pipefd_[0], &value, sizeof(value));
    EXPECT_EQ(read_bytes, static_cast<ssize_t>(sizeof(value)));
    EXPECT_EQ(value, 1u);
}

TEST_F(ShutdownManagerSummaryTest, AC_shutdownSummary_printsExpectedMetrics) {
    using ::testing::HasSubstr;

    atomic_store_explicit(&writer_.event_count, (uint_fast64_t)1234, memory_order_relaxed);
    atomic_store_explicit(&writer_.bytes_written, (uint_fast64_t)5678, memory_order_relaxed);

    mark_thread_active(0, 5);
    mark_thread_active(1, 3);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_SIGNAL, SIGTERM));
    shutdown_manager_execute(&manager_);
    std::string stderr_output = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(shutdown_manager_is_shutdown_complete(&manager_));
    EXPECT_EQ(g_stop_drain_calls.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 2);
    EXPECT_EQ(manager_.files_synced, 2u);

    EXPECT_THAT(stderr_output, HasSubstr("=== ADA Tracer Shutdown Summary ==="));
    EXPECT_THAT(stderr_output, HasSubstr("Shutdown Duration: "));
    EXPECT_THAT(stderr_output, HasSubstr("Total Events Processed: 1234"));
    EXPECT_THAT(stderr_output, HasSubstr("Events In Flight at Shutdown: 8"));
    EXPECT_THAT(stderr_output, HasSubstr("Bytes Written: 5678"));
    EXPECT_THAT(stderr_output, HasSubstr("Files Synced: 2"));
    EXPECT_THAT(stderr_output, HasSubstr("Threads Flushed: 2/2"));
    EXPECT_THAT(stderr_output, HasSubstr("================================"));
}

// ===== ShutdownState Tests =====

TEST_F(ShutdownManagerTest, shutdown_state_init__null_state__then_returns_safely) {
    shutdown_state_init(nullptr, 10);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_state_init__zero_capacity__then_uses_max_threads) {
    ShutdownState state;
    shutdown_state_init(&state, 0);

    uint32_t capacity = atomic_load_explicit(&state.capacity, memory_order_acquire);
    EXPECT_EQ(capacity, MAX_THREADS);
}

TEST_F(ShutdownManagerTest, shutdown_state_init__excessive_capacity__then_uses_max_threads) {
    ShutdownState state;
    shutdown_state_init(&state, MAX_THREADS + 100);

    uint32_t capacity = atomic_load_explicit(&state.capacity, memory_order_acquire);
    EXPECT_EQ(capacity, MAX_THREADS);
}

TEST_F(ShutdownManagerTest, shutdown_state_init__valid_capacity__then_initializes_correctly) {
    ShutdownState state;
    shutdown_state_init(&state, 10);

    EXPECT_EQ(atomic_load_explicit(&state.capacity, memory_order_acquire), 10u);
    EXPECT_EQ(atomic_load_explicit(&state.active_threads, memory_order_acquire), 0u);
    EXPECT_EQ(atomic_load_explicit(&state.threads_stopped, memory_order_acquire), 0u);
    EXPECT_EQ(atomic_load_explicit(&state.threads_flushed, memory_order_acquire), 0u);

    for (uint32_t i = 0; i < MAX_THREADS; ++i) {
        EXPECT_FALSE(atomic_load_explicit(&state.threads[i].accepting_events, memory_order_acquire));
        EXPECT_FALSE(atomic_load_explicit(&state.threads[i].flush_requested, memory_order_acquire));
        EXPECT_FALSE(atomic_load_explicit(&state.threads[i].flush_complete, memory_order_acquire));
        EXPECT_EQ(atomic_load_explicit(&state.threads[i].pending_events, memory_order_acquire), 0u);
    }
}

TEST_F(ShutdownManagerTest, shutdown_state_mark_active__null_state__then_returns_safely) {
    shutdown_state_mark_active(nullptr, 0);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_state_mark_active__slot_out_of_bounds__then_returns_safely) {
    shutdown_state_mark_active(&state_, MAX_THREADS + 1);
    // Should not crash - slot index is beyond capacity
}

TEST_F(ShutdownManagerTest, shutdown_state_mark_active__valid_slot__then_marks_active) {
    uint32_t slot = 5;
    shutdown_state_mark_active(&state_, slot);

    EXPECT_TRUE(atomic_load_explicit(&state_.threads[slot].accepting_events, memory_order_acquire));
    EXPECT_FALSE(atomic_load_explicit(&state_.threads[slot].flush_requested, memory_order_acquire));
    EXPECT_FALSE(atomic_load_explicit(&state_.threads[slot].flush_complete, memory_order_acquire));
    EXPECT_EQ(atomic_load_explicit(&state_.threads[slot].pending_events, memory_order_acquire), 0u);
    EXPECT_EQ(atomic_load_explicit(&state_.active_threads, memory_order_acquire), 1u);
}

TEST_F(ShutdownManagerTest, shutdown_state_mark_active__already_active_slot__then_resets_flags) {
    uint32_t slot = 3;

    // Mark active first time
    shutdown_state_mark_active(&state_, slot);
    EXPECT_EQ(atomic_load_explicit(&state_.active_threads, memory_order_acquire), 1u);

    // Set some flags to test reset
    atomic_store_explicit(&state_.threads[slot].flush_requested, true, memory_order_release);
    atomic_store_explicit(&state_.threads[slot].flush_complete, true, memory_order_release);
    atomic_store_explicit(&state_.threads[slot].pending_events, 42, memory_order_release);

    // Mark active again
    shutdown_state_mark_active(&state_, slot);

    EXPECT_TRUE(atomic_load_explicit(&state_.threads[slot].accepting_events, memory_order_acquire));
    EXPECT_FALSE(atomic_load_explicit(&state_.threads[slot].flush_requested, memory_order_acquire));
    EXPECT_FALSE(atomic_load_explicit(&state_.threads[slot].flush_complete, memory_order_acquire));
    EXPECT_EQ(atomic_load_explicit(&state_.threads[slot].pending_events, memory_order_acquire), 0u);
    // active_threads count shouldn't increase
    EXPECT_EQ(atomic_load_explicit(&state_.active_threads, memory_order_acquire), 1u);
}

TEST_F(ShutdownManagerTest, shutdown_state_mark_inactive__null_state__then_returns_safely) {
    shutdown_state_mark_inactive(nullptr, 0);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_state_mark_inactive__slot_out_of_bounds__then_returns_safely) {
    shutdown_state_mark_inactive(&state_, MAX_THREADS + 1);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_state_mark_inactive__active_slot__then_marks_inactive) {
    uint32_t slot = 7;

    // First mark active
    shutdown_state_mark_active(&state_, slot);
    EXPECT_EQ(atomic_load_explicit(&state_.active_threads, memory_order_acquire), 1u);

    // Then mark inactive
    shutdown_state_mark_inactive(&state_, slot);

    EXPECT_FALSE(atomic_load_explicit(&state_.threads[slot].accepting_events, memory_order_acquire));
    EXPECT_EQ(atomic_load_explicit(&state_.threads[slot].pending_events, memory_order_acquire), 0u);
    EXPECT_EQ(atomic_load_explicit(&state_.active_threads, memory_order_acquire), 0u);
}

TEST_F(ShutdownManagerTest, shutdown_state_mark_inactive__already_inactive_slot__then_no_change) {
    uint32_t slot = 2;

    // Mark inactive without marking active first
    shutdown_state_mark_inactive(&state_, slot);

    EXPECT_FALSE(atomic_load_explicit(&state_.threads[slot].accepting_events, memory_order_acquire));
    EXPECT_EQ(atomic_load_explicit(&state_.threads[slot].pending_events, memory_order_acquire), 0u);
    EXPECT_EQ(atomic_load_explicit(&state_.active_threads, memory_order_acquire), 0u);
}

TEST_F(ShutdownManagerTest, shutdown_state_record_pending__null_state__then_returns_safely) {
    shutdown_state_record_pending(nullptr, 0, 100);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_state_record_pending__slot_out_of_bounds__then_returns_safely) {
    shutdown_state_record_pending(&state_, MAX_THREADS + 1, 100);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_state_record_pending__valid_slot__then_records_pending) {
    uint32_t slot = 4;
    uint64_t pending = 123;

    shutdown_state_record_pending(&state_, slot, pending);

    EXPECT_EQ(atomic_load_explicit(&state_.threads[slot].pending_events, memory_order_acquire), pending);
}

// ===== ShutdownManager Initialization Tests =====

TEST_F(ShutdownManagerTest, shutdown_manager_init__null_manager__then_returns_einval) {
    int result = shutdown_manager_init(nullptr, &state_, nullptr, nullptr, nullptr);
    EXPECT_EQ(result, -EINVAL);
}

TEST_F(ShutdownManagerTest, shutdown_manager_init__valid_params__then_initializes_correctly) {
    ShutdownManager manager;
    DrainThread drain;
    ShutdownOps ops = {.cancel_timer = default_timer_cancel, .stop_drain = nullptr};

    int result = shutdown_manager_init(&manager, &state_, nullptr, &drain, &ops);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(manager.state, &state_);
    EXPECT_EQ(manager.registry, nullptr);
    EXPECT_EQ(manager.drain_thread, &drain);
    EXPECT_EQ(manager.wake_read_fd, -1);
    EXPECT_EQ(manager.wake_write_fd, -1);
    EXPECT_FALSE(manager.timestamp_valid);

    EXPECT_FALSE(atomic_load_explicit(&manager.shutdown_requested, memory_order_acquire));
    EXPECT_FALSE(atomic_load_explicit(&manager.shutdown_completed, memory_order_acquire));
    EXPECT_EQ(atomic_load_explicit(&manager.phase, memory_order_acquire), SHUTDOWN_PHASE_IDLE);
    EXPECT_EQ(atomic_load_explicit(&manager.last_signal, memory_order_acquire), 0);
    EXPECT_EQ(atomic_load_explicit(&manager.last_reason, memory_order_acquire), SHUTDOWN_REASON_NONE);
    EXPECT_EQ(atomic_load_explicit(&manager.request_count, memory_order_acquire), 0u);
}

TEST_F(ShutdownManagerTest, shutdown_manager_init__null_ops__then_uses_defaults) {
    ShutdownManager manager;

    int result = shutdown_manager_init(&manager, &state_, nullptr, nullptr, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_NE(manager.ops.cancel_timer, nullptr);
    EXPECT_EQ(manager.ops.stop_drain, nullptr);
}

TEST_F(ShutdownManagerTest, shutdown_manager_init__state_with_zero_capacity__then_reinitializes_state) {
    ShutdownState zero_capacity_state;
    atomic_init(&zero_capacity_state.capacity, 0);

    ShutdownManager manager;
    int result = shutdown_manager_init(&manager, &zero_capacity_state, nullptr, nullptr, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(atomic_load_explicit(&zero_capacity_state.capacity, memory_order_acquire), MAX_THREADS);
}

TEST_F(ShutdownManagerTest, shutdown_manager_reset__null_manager__then_returns_safely) {
    shutdown_manager_reset(nullptr);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_reset__initialized_manager__then_resets_state) {
    // First setup some state
    atomic_store_explicit(&manager_.shutdown_requested, true, memory_order_release);
    atomic_store_explicit(&manager_.shutdown_completed, true, memory_order_release);
    atomic_store_explicit(&manager_.phase, SHUTDOWN_PHASE_COMPLETED, memory_order_release);
    atomic_store_explicit(&manager_.last_signal, SIGTERM, memory_order_release);
    atomic_store_explicit(&manager_.last_reason, SHUTDOWN_REASON_SIGNAL, memory_order_release);
    atomic_store_explicit(&manager_.request_count, 5, memory_order_release);
    manager_.timestamp_valid = true;
    manager_.start_ts.tv_sec = 123;
    manager_.start_ts.tv_nsec = 456;
    manager_.end_ts.tv_sec = 789;
    manager_.end_ts.tv_nsec = 101112;
    manager_.files_synced = 3;

    // Reset
    shutdown_manager_reset(&manager_);

    // Verify reset
    EXPECT_FALSE(atomic_load_explicit(&manager_.shutdown_requested, memory_order_acquire));
    EXPECT_FALSE(atomic_load_explicit(&manager_.shutdown_completed, memory_order_acquire));
    EXPECT_EQ(atomic_load_explicit(&manager_.phase, memory_order_acquire), SHUTDOWN_PHASE_IDLE);
    EXPECT_EQ(atomic_load_explicit(&manager_.last_signal, memory_order_acquire), 0);
    EXPECT_EQ(atomic_load_explicit(&manager_.last_reason, memory_order_acquire), SHUTDOWN_REASON_NONE);
    EXPECT_EQ(atomic_load_explicit(&manager_.request_count, memory_order_acquire), 0u);
    EXPECT_FALSE(manager_.timestamp_valid);
    EXPECT_EQ(manager_.start_ts.tv_sec, 0);
    EXPECT_EQ(manager_.start_ts.tv_nsec, 0);
    EXPECT_EQ(manager_.end_ts.tv_sec, 0);
    EXPECT_EQ(manager_.end_ts.tv_nsec, 0);
    EXPECT_EQ(manager_.files_synced, 0u);
}

// ===== ShutdownManager Setter Tests =====

TEST_F(ShutdownManagerTest, shutdown_manager_set_registry__null_manager__then_returns_safely) {
    void* registry = (void*)0x12345; // Mock pointer for testing
    shutdown_manager_set_registry(nullptr, (ThreadRegistry*)registry);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_registry__valid_params__then_sets_registry) {
    void* registry = (void*)0x12345; // Mock pointer for testing
    shutdown_manager_set_registry(&manager_, (ThreadRegistry*)registry);
    EXPECT_EQ(manager_.registry, (ThreadRegistry*)registry);
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_drain_thread__null_manager__then_returns_safely) {
    DrainThread drain;
    shutdown_manager_set_drain_thread(nullptr, &drain);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_drain_thread__valid_params__then_sets_drain) {
    DrainThread drain;
    shutdown_manager_set_drain_thread(&manager_, &drain);
    EXPECT_EQ(manager_.drain_thread, &drain);
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_ops__null_manager__then_returns_safely) {
    ShutdownOps ops = {.cancel_timer = default_timer_cancel, .stop_drain = nullptr};
    shutdown_manager_set_ops(nullptr, &ops);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_ops__valid_params__then_sets_ops) {
    ShutdownOps ops = {.cancel_timer = default_timer_cancel, .stop_drain = nullptr};
    shutdown_manager_set_ops(&manager_, &ops);
    EXPECT_EQ(manager_.ops.cancel_timer, default_timer_cancel);
    EXPECT_EQ(manager_.ops.stop_drain, nullptr);
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_ops__null_ops__then_uses_defaults) {
    shutdown_manager_set_ops(&manager_, nullptr);
    EXPECT_NE(manager_.ops.cancel_timer, nullptr);
    EXPECT_EQ(manager_.ops.stop_drain, nullptr);
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_ops__null_cancel_timer__then_uses_default) {
    ShutdownOps ops = {.cancel_timer = nullptr, .stop_drain = nullptr};
    shutdown_manager_set_ops(&manager_, &ops);
    EXPECT_NE(manager_.ops.cancel_timer, nullptr);
    EXPECT_EQ(manager_.ops.stop_drain, nullptr);
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_wakeup_fds__null_manager__then_returns_safely) {
    shutdown_manager_set_wakeup_fds(nullptr, 1, 2);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_set_wakeup_fds__valid_params__then_sets_fds) {
    int read_fd = 123;
    int write_fd = 456;
    shutdown_manager_set_wakeup_fds(&manager_, read_fd, write_fd);
    EXPECT_EQ(manager_.wake_read_fd, read_fd);
    EXPECT_EQ(manager_.wake_write_fd, write_fd);
}

// ===== ShutdownManager Request/Status Tests =====

TEST_F(ShutdownManagerTest, shutdown_manager_request_shutdown__null_manager__then_returns_false) {
    bool result = shutdown_manager_request_shutdown(nullptr, SHUTDOWN_REASON_SIGNAL, SIGTERM);
    EXPECT_FALSE(result);
}

TEST_F(ShutdownManagerTest, shutdown_manager_request_shutdown__first_request__then_returns_true) {
    bool result = shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_TIMER, 0);
    EXPECT_TRUE(result);
    EXPECT_TRUE(atomic_load_explicit(&manager_.shutdown_requested, memory_order_acquire));
    EXPECT_EQ(atomic_load_explicit(&manager_.last_reason, memory_order_acquire), SHUTDOWN_REASON_TIMER);
    EXPECT_EQ(atomic_load_explicit(&manager_.last_signal, memory_order_acquire), 0);
    EXPECT_EQ(atomic_load_explicit(&manager_.request_count, memory_order_acquire), 1u);
    EXPECT_EQ(atomic_load_explicit(&manager_.phase, memory_order_acquire), SHUTDOWN_PHASE_SIGNAL_RECEIVED);
}

TEST_F(ShutdownManagerTest, shutdown_manager_request_shutdown__subsequent_request__then_returns_false) {
    // First request
    EXPECT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_SIGNAL, SIGINT));

    // Second request
    bool result = shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_TIMER, 0);
    EXPECT_FALSE(result);

    // Counter should still increment
    EXPECT_EQ(atomic_load_explicit(&manager_.request_count, memory_order_acquire), 2u);
    // But last reason/signal should be updated
    EXPECT_EQ(atomic_load_explicit(&manager_.last_reason, memory_order_acquire), SHUTDOWN_REASON_TIMER);
    EXPECT_EQ(atomic_load_explicit(&manager_.last_signal, memory_order_acquire), 0);
}

TEST_F(ShutdownManagerTest, shutdown_manager_request_shutdown__with_timer_cancel__then_calls_cancel) {
    g_timer_cancel_calls = 0;
    shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_SIGNAL, SIGTERM);
    EXPECT_EQ(g_timer_cancel_calls, 1);
}

TEST_F(ShutdownManagerTest, shutdown_manager_is_shutdown_requested__null_manager__then_returns_false) {
    bool result = shutdown_manager_is_shutdown_requested(nullptr);
    EXPECT_FALSE(result);
}

TEST_F(ShutdownManagerTest, shutdown_manager_is_shutdown_requested__not_requested__then_returns_false) {
    bool result = shutdown_manager_is_shutdown_requested(&manager_);
    EXPECT_FALSE(result);
}

TEST_F(ShutdownManagerTest, shutdown_manager_is_shutdown_requested__requested__then_returns_true) {
    shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0);
    bool result = shutdown_manager_is_shutdown_requested(&manager_);
    EXPECT_TRUE(result);
}

TEST_F(ShutdownManagerTest, shutdown_manager_is_shutdown_complete__null_manager__then_returns_false) {
    bool result = shutdown_manager_is_shutdown_complete(nullptr);
    EXPECT_FALSE(result);
}

TEST_F(ShutdownManagerTest, shutdown_manager_is_shutdown_complete__not_complete__then_returns_false) {
    bool result = shutdown_manager_is_shutdown_complete(&manager_);
    EXPECT_FALSE(result);
}

TEST_F(ShutdownManagerTest, shutdown_manager_get_phase__null_manager__then_returns_idle) {
    ShutdownPhase phase = shutdown_manager_get_phase(nullptr);
    EXPECT_EQ(phase, SHUTDOWN_PHASE_IDLE);
}

TEST_F(ShutdownManagerTest, shutdown_manager_get_phase__initialized_manager__then_returns_idle) {
    ShutdownPhase phase = shutdown_manager_get_phase(&manager_);
    EXPECT_EQ(phase, SHUTDOWN_PHASE_IDLE);
}

TEST_F(ShutdownManagerTest, shutdown_manager_get_request_count__null_manager__then_returns_zero) {
    uint64_t count = shutdown_manager_get_request_count(nullptr);
    EXPECT_EQ(count, 0u);
}

TEST_F(ShutdownManagerTest, shutdown_manager_get_request_count__no_requests__then_returns_zero) {
    uint64_t count = shutdown_manager_get_request_count(&manager_);
    EXPECT_EQ(count, 0u);
}

TEST_F(ShutdownManagerTest, shutdown_manager_get_last_signal__null_manager__then_returns_zero) {
    int signal = shutdown_manager_get_last_signal(nullptr);
    EXPECT_EQ(signal, 0);
}

TEST_F(ShutdownManagerTest, shutdown_manager_get_last_signal__no_signal__then_returns_zero) {
    int signal = shutdown_manager_get_last_signal(&manager_);
    EXPECT_EQ(signal, 0);
}

TEST_F(ShutdownManagerTest, shutdown_manager_get_last_reason__null_manager__then_returns_none) {
    int reason = shutdown_manager_get_last_reason(nullptr);
    EXPECT_EQ(reason, SHUTDOWN_REASON_NONE);
}

TEST_F(ShutdownManagerTest, shutdown_manager_get_last_reason__no_reason__then_returns_none) {
    int reason = shutdown_manager_get_last_reason(&manager_);
    EXPECT_EQ(reason, SHUTDOWN_REASON_NONE);
}

// ===== ShutdownManager Execute Tests =====

TEST_F(ShutdownManagerTest, shutdown_manager_execute__null_manager__then_returns_safely) {
    shutdown_manager_execute(nullptr);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_execute__no_shutdown_requested__then_returns_early) {
    g_clock_gettime_calls.store(0, std::memory_order_relaxed);
    shutdown_manager_execute(&manager_);

    // Should not have called clock_gettime or changed phase
    EXPECT_EQ(g_clock_gettime_calls.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(shutdown_manager_get_phase(&manager_), SHUTDOWN_PHASE_IDLE);
    EXPECT_FALSE(shutdown_manager_is_shutdown_complete(&manager_));
}

TEST_F(ShutdownManagerTest, shutdown_manager_execute__already_complete__then_returns_early) {
    atomic_store_explicit(&manager_.shutdown_requested, true, memory_order_release);
    atomic_store_explicit(&manager_.shutdown_completed, true, memory_order_release);

    g_clock_gettime_calls.store(0, std::memory_order_relaxed);
    shutdown_manager_execute(&manager_);

    // Should not have called clock_gettime for start timestamp
    EXPECT_EQ(g_clock_gettime_calls.load(std::memory_order_relaxed), 0);
}

TEST_F(ShutdownManagerTest, shutdown_manager_execute__clock_gettime_fails__then_continues_without_timestamp) {
    shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0);

    g_clock_gettime_should_fail.store(true, std::memory_order_relaxed);

    testing::internal::CaptureStderr();
    shutdown_manager_execute(&manager_);
    std::string stderr_output = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(manager_.timestamp_valid);
    EXPECT_TRUE(shutdown_manager_is_shutdown_complete(&manager_));
    EXPECT_EQ(shutdown_manager_get_phase(&manager_), SHUTDOWN_PHASE_COMPLETED);

    g_clock_gettime_should_fail.store(false, std::memory_order_relaxed);
}

// ===== ShutdownManager Print Summary Tests =====

TEST_F(ShutdownManagerTest, shutdown_manager_print_summary__null_manager__then_returns_safely) {
    shutdown_manager_print_summary(nullptr);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_print_summary__no_drain_thread__then_prints_zeros) {
    using ::testing::HasSubstr;

    testing::internal::CaptureStderr();
    shutdown_manager_print_summary(&manager_);
    std::string stderr_output = testing::internal::GetCapturedStderr();

    EXPECT_THAT(stderr_output, HasSubstr("Total Events Processed: 0"));
    EXPECT_THAT(stderr_output, HasSubstr("Bytes Written: 0"));
    EXPECT_THAT(stderr_output, HasSubstr("Files Synced: 0"));
}

// ===== Global Manager Tests =====

TEST_F(ShutdownManagerTest, shutdown_manager_register_global__null_manager__then_sets_null) {
    shutdown_manager_register_global(nullptr);
    // Should not crash, sets global to null
}

TEST_F(ShutdownManagerTest, shutdown_manager_register_global__valid_manager__then_sets_global) {
    shutdown_manager_register_global(&manager_);
    manager_registered_ = true;
    // Global is set internally, tested via signal handling
}

TEST_F(ShutdownManagerTest, shutdown_manager_unregister_global__then_clears_global) {
    shutdown_manager_register_global(&manager_);
    shutdown_manager_unregister_global();
    manager_registered_ = false;
    // Global is cleared
}

TEST_F(ShutdownManagerTest, shutdown_manager_signal_wakeup__null_manager__then_returns_safely) {
    shutdown_manager_signal_wakeup(nullptr);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_signal_wakeup__invalid_fd__then_returns_safely) {
    // manager_ has wake_write_fd = -1 by default
    shutdown_manager_signal_wakeup(&manager_);
    // Should not crash
}

TEST_F(ShutdownManagerTest, shutdown_manager_signal_wakeup__valid_fd__then_writes_value) {
    configure_wakeup_pipe();

    shutdown_manager_signal_wakeup(&manager_);

    uint64_t value = 0;
    ssize_t read_bytes = read(pipefd_[0], &value, sizeof(value));
    EXPECT_EQ(read_bytes, static_cast<ssize_t>(sizeof(value)));
    EXPECT_EQ(value, 1u);
}

// ===== SignalHandler Tests =====

TEST_F(ShutdownManagerTest, signal_handler_init__null_handler__then_returns_einval) {
    int result = signal_handler_init(nullptr, &manager_);
    EXPECT_EQ(result, -EINVAL);
}

TEST_F(ShutdownManagerTest, signal_handler_init__valid_params__then_initializes_correctly) {
    SignalHandler handler;
    int result = signal_handler_init(&handler, &manager_);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(handler.manager, &manager_);
    EXPECT_FALSE(atomic_load_explicit(&handler.installed, memory_order_acquire));
    EXPECT_EQ(atomic_load_explicit(&handler.signal_count, memory_order_acquire), 0u);
}

TEST_F(ShutdownManagerTest, signal_handler_install__null_handler__then_returns_einval) {
    int result = signal_handler_install(nullptr);
    EXPECT_EQ(result, -EINVAL);
}

TEST_F(ShutdownManagerTest, signal_handler_install__valid_handler__then_installs_handlers) {
    int result = signal_handler_install(&handler_);
    EXPECT_EQ(result, 0);
    handler_installed_ = true;

    EXPECT_TRUE(atomic_load_explicit(&handler_.installed, memory_order_acquire));

    // Verify signal handlers are installed
    struct sigaction sa;
    EXPECT_EQ(sigaction(SIGINT, nullptr, &sa), 0);
    EXPECT_NE(sa.sa_handler, SIG_DFL);

    EXPECT_EQ(sigaction(SIGTERM, nullptr, &sa), 0);
    EXPECT_NE(sa.sa_handler, SIG_DFL);
}

TEST_F(ShutdownManagerTest, signal_handler_uninstall__null_handler__then_returns_safely) {
    signal_handler_uninstall(nullptr);
    // Should not crash
}

TEST_F(ShutdownManagerTest, signal_handler_uninstall__not_installed__then_returns_safely) {
    signal_handler_uninstall(&handler_);
    // Should not crash - handler was never installed
}

TEST_F(ShutdownManagerTest, signal_handler_uninstall__installed_handler__then_uninstalls) {
    install_handler();

    signal_handler_uninstall(&handler_);
    handler_installed_ = false;

    EXPECT_FALSE(atomic_load_explicit(&handler_.installed, memory_order_acquire));
}

// ===== shutdown_initiate Tests =====

TEST_F(ShutdownManagerTest, shutdown_initiate__no_global_manager__then_returns_safely) {
    shutdown_initiate();
    // Should not crash when no global manager is set
}

TEST_F(ShutdownManagerTest, shutdown_initiate__global_manager_set__then_requests_shutdown) {
    configure_wakeup_pipe();
    register_manager();

    shutdown_initiate();

    EXPECT_TRUE(shutdown_manager_is_shutdown_requested(&manager_));
    EXPECT_EQ(shutdown_manager_get_last_reason(&manager_), SHUTDOWN_REASON_TIMER);
    EXPECT_EQ(shutdown_manager_get_last_signal(&manager_), 0);

    // Should have written to wakeup pipe
    uint64_t value = 0;
    ssize_t read_bytes = read(pipefd_[0], &value, sizeof(value));
    EXPECT_EQ(read_bytes, static_cast<ssize_t>(sizeof(value)));
    EXPECT_EQ(value, 1u);
}

// ===== Signal Handler Integration Tests =====

TEST_F(ShutdownManagerTest, sigterm_triggers_shutdown) {
    configure_wakeup_pipe();
    register_manager();
    install_handler();

    ASSERT_FALSE(shutdown_manager_is_shutdown_requested(&manager_));

    raise(SIGTERM);
    usleep(5000);

    EXPECT_TRUE(shutdown_manager_is_shutdown_requested(&manager_));
    EXPECT_EQ(shutdown_manager_get_last_reason(&manager_), SHUTDOWN_REASON_SIGNAL);
    EXPECT_EQ(shutdown_manager_get_last_signal(&manager_), SIGTERM);
    EXPECT_GT(atomic_load_explicit(&handler_.signal_count, memory_order_acquire), 0u);
}

TEST_F(ShutdownManagerTest, signal_handler_without_global_manager__then_uses_handler_manager) {
    configure_wakeup_pipe();
    install_handler();
    // Don't register global manager

    raise(SIGINT);
    usleep(5000);

    EXPECT_TRUE(shutdown_manager_is_shutdown_requested(&manager_));
    EXPECT_EQ(shutdown_manager_get_last_signal(&manager_), SIGINT);
}

TEST_F(ShutdownManagerTest, signal_with_no_handlers__then_does_nothing) {
    // No global manager, no handler manager
    SignalHandler empty_handler;
    signal_handler_init(&empty_handler, nullptr);
    signal_handler_install(&empty_handler);

    raise(SIGINT);
    usleep(5000);

    // Should not crash, but no shutdown should be triggered
    signal_handler_uninstall(&empty_handler);
}

// ===== File Sync and ATF Writer Tests =====

class ShutdownManagerFileSyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_fsync_calls.store(0, std::memory_order_relaxed);
        g_fsync_error_count.store(0, std::memory_order_relaxed);
        g_fsync_should_fail.store(false, std::memory_order_relaxed);
        g_stop_drain_calls.store(0, std::memory_order_relaxed);

        shutdown_state_init(&state_, MAX_THREADS);
        ASSERT_EQ(shutdown_manager_init(&manager_, &state_, nullptr, nullptr, nullptr), 0);

        pthread_mutex_init(&drain_.lifecycle_lock, nullptr);
        atomic_init(&drain_.state, DRAIN_STATE_STOPPED);

        pthread_mutex_lock(&drain_.lifecycle_lock);
        drain_.atf_writer = &writer_;
        pthread_mutex_unlock(&drain_.lifecycle_lock);

        shutdown_manager_set_drain_thread(&manager_, &drain_);

        // Initialize writer with default values
        memset(&writer_, 0, sizeof(writer_));
        writer_.events_fd = -1;
        writer_.manifest_fp = nullptr;
        writer_.manifest_enabled = false;
        writer_.initialized = true;
        atomic_init(&writer_.event_count, (uint_fast64_t)0);
        atomic_init(&writer_.bytes_written, (uint_fast64_t)0);
    }

    void TearDown() override {
        if (writer_.manifest_fp) {
            fclose(writer_.manifest_fp);
            writer_.manifest_fp = nullptr;
        }
        pthread_mutex_destroy(&drain_.lifecycle_lock);
        g_fsync_should_fail.store(false, std::memory_order_relaxed);
    }

    static int StopDrain(DrainThread* drain) {
        g_stop_drain_calls.fetch_add(1, std::memory_order_relaxed);
        if (drain) {
            atomic_store_explicit(&drain->state, DRAIN_STATE_STOPPING, memory_order_release);
            atomic_store_explicit(&drain->state, DRAIN_STATE_STOPPED, memory_order_release);
        }
        return 0;
    }

    ShutdownManager manager_{};
    ShutdownState state_{};
    AtfV4Writer writer_{};
    DrainThread drain_{};
};

TEST_F(ShutdownManagerFileSyncTest, file_sync__no_drain_thread__then_syncs_zero_files) {
    shutdown_manager_set_drain_thread(&manager_, nullptr);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 0u);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 0);
}

TEST_F(ShutdownManagerFileSyncTest, file_sync__no_atf_writer__then_syncs_zero_files) {
    pthread_mutex_lock(&drain_.lifecycle_lock);
    drain_.atf_writer = nullptr;
    pthread_mutex_unlock(&drain_.lifecycle_lock);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 0u);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 0);
}

TEST_F(ShutdownManagerFileSyncTest, file_sync__valid_events_fd__then_syncs_events_file) {
    writer_.events_fd = 42;  // Valid fd

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 1u);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 1);
}

TEST_F(ShutdownManagerFileSyncTest, file_sync__invalid_events_fd__then_skips_events_file) {
    writer_.events_fd = -1;  // Invalid fd

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 0u);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 0);
}

TEST_F(ShutdownManagerFileSyncTest, file_sync__manifest_fp_open__then_syncs_manifest_file) {
    writer_.manifest_fp = tmpfile();
    ASSERT_NE(writer_.manifest_fp, nullptr);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 1u);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 1);
}

TEST_F(ShutdownManagerFileSyncTest, file_sync__manifest_enabled_no_fp__then_opens_and_syncs_manifest) {
    writer_.manifest_enabled = true;
    strcpy(writer_.manifest_path, "/tmp/test_manifest");

    // Create a test file
    int fd = open("/tmp/test_manifest", O_CREAT | O_WRONLY, 0644);
    ASSERT_GE(fd, 0);
    close(fd);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 1u);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 1);

    // Clean up
    unlink("/tmp/test_manifest");
}

TEST_F(ShutdownManagerFileSyncTest, file_sync__manifest_enabled_invalid_path__then_skips_manifest) {
    writer_.manifest_enabled = true;
    strcpy(writer_.manifest_path, "/nonexistent/path/manifest");

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 0u);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 0);
}

TEST_F(ShutdownManagerFileSyncTest, file_sync__both_files_valid__then_syncs_both) {
    writer_.events_fd = 42;
    writer_.manifest_fp = tmpfile();
    ASSERT_NE(writer_.manifest_fp, nullptr);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 2u);
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 2);
}

TEST_F(ShutdownManagerFileSyncTest, file_sync__fsync_failures__then_counts_only_successful) {
    writer_.events_fd = 42;
    writer_.manifest_fp = tmpfile();
    ASSERT_NE(writer_.manifest_fp, nullptr);

    g_fsync_should_fail.store(true, std::memory_order_relaxed);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(manager_.files_synced, 0u);  // Both should fail
    EXPECT_EQ(g_fsync_calls.load(std::memory_order_relaxed), 2);  // Both attempted
    EXPECT_EQ(g_fsync_error_count.load(std::memory_order_relaxed), 2);
}

// ===== Drain Thread State Handling Tests =====

class ShutdownManagerDrainTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_stop_drain_calls.store(0, std::memory_order_relaxed);

        shutdown_state_init(&state_, MAX_THREADS);
        ASSERT_EQ(shutdown_manager_init(&manager_, &state_, nullptr, nullptr, nullptr), 0);

        pthread_mutex_init(&drain_.lifecycle_lock, nullptr);
        atomic_init(&drain_.state, DRAIN_STATE_RUNNING);

        shutdown_manager_set_drain_thread(&manager_, &drain_);

        ShutdownOps ops;
        memset(&ops, 0, sizeof(ops));
        ops.stop_drain = &ShutdownManagerDrainTest::StopDrain;
        shutdown_manager_set_ops(&manager_, &ops);
    }

    void TearDown() override {
        pthread_mutex_destroy(&drain_.lifecycle_lock);
    }

    static int StopDrain(DrainThread* drain) {
        g_stop_drain_calls.fetch_add(1, std::memory_order_relaxed);
        if (drain) {
            atomic_store_explicit(&drain->state, DRAIN_STATE_STOPPED, memory_order_release);
        }
        return 0;
    }

    ShutdownManager manager_{};
    ShutdownState state_{};
    DrainThread drain_{};
};

TEST_F(ShutdownManagerDrainTest, drain_wait__already_stopped__then_returns_immediately) {
    atomic_store_explicit(&drain_.state, DRAIN_STATE_STOPPED, memory_order_release);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_TRUE(shutdown_manager_is_shutdown_complete(&manager_));
    EXPECT_EQ(g_stop_drain_calls.load(std::memory_order_relaxed), 1);
}

TEST_F(ShutdownManagerDrainTest, drain_wait__uninitialized_state__then_returns_immediately) {
    atomic_store_explicit(&drain_.state, DRAIN_STATE_UNINITIALIZED, memory_order_release);

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_TRUE(shutdown_manager_is_shutdown_complete(&manager_));
    EXPECT_EQ(g_stop_drain_calls.load(std::memory_order_relaxed), 1);
}

TEST_F(ShutdownManagerDrainTest, drain_wait__no_stop_drain_op__then_still_waits) {
    ShutdownOps ops = {.cancel_timer = nullptr, .stop_drain = nullptr};
    shutdown_manager_set_ops(&manager_, &ops);

    // Set drain to stop after a short delay to avoid infinite wait
    std::thread stopper([this]() {
        usleep(10000);  // 10ms
        atomic_store_explicit(&drain_.state, DRAIN_STATE_STOPPED, memory_order_release);
    });

    testing::internal::CaptureStderr();
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    stopper.join();
    EXPECT_TRUE(shutdown_manager_is_shutdown_complete(&manager_));
    EXPECT_EQ(g_stop_drain_calls.load(std::memory_order_relaxed), 0);  // No stop_drain called
}

// ===== Thread Registry Integration Tests =====
// (Simplified tests without mocking external functions)

// ===== Edge Cases and Error Conditions =====

TEST_F(ShutdownManagerTest, multiple_rapid_shutdown_requests__then_handles_gracefully) {
    const int num_requests = 10;
    for (int i = 0; i < num_requests; ++i) {
        shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, i);
    }

    EXPECT_TRUE(shutdown_manager_is_shutdown_requested(&manager_));
    EXPECT_EQ(shutdown_manager_get_request_count(&manager_), num_requests);
    // Last reason/signal should be from the final request
    EXPECT_EQ(shutdown_manager_get_last_signal(&manager_), num_requests - 1);
}

TEST_F(ShutdownManagerTest, state_manipulation_after_shutdown__then_behaves_correctly) {
    // Start shutdown
    ASSERT_TRUE(shutdown_manager_request_shutdown(&manager_, SHUTDOWN_REASON_MANUAL, 0));

    // Try to manipulate state after shutdown requested
    shutdown_state_mark_active(&state_, 5);
    shutdown_state_record_pending(&state_, 5, 100);

    testing::internal::CaptureStderr();
    shutdown_manager_execute(&manager_);
    testing::internal::GetCapturedStderr();

    EXPECT_TRUE(shutdown_manager_is_shutdown_complete(&manager_));
    // The thread should be stopped during execute despite being marked active
    EXPECT_FALSE(atomic_load_explicit(&state_.threads[5].accepting_events, memory_order_acquire));
}

TEST_F(ShutdownManagerTest, duration_calculation__negative_time__then_returns_zero) {
    // Manually set invalid timestamps
    manager_.timestamp_valid = true;
    manager_.start_ts.tv_sec = 1000;
    manager_.start_ts.tv_nsec = 500000000;
    manager_.end_ts.tv_sec = 999;  // Earlier than start
    manager_.end_ts.tv_nsec = 0;

    testing::internal::CaptureStderr();
    shutdown_manager_print_summary(&manager_);
    std::string stderr_output = testing::internal::GetCapturedStderr();

    using ::testing::HasSubstr;
    EXPECT_THAT(stderr_output, HasSubstr("Shutdown Duration: 0.00 ms"));
}

TEST_F(ShutdownManagerTest, events_in_flight_calculation__excessive_capacity__then_caps_correctly) {
    // Set up state with threads beyond normal capacity
    for (uint32_t i = 0; i < MAX_THREADS; ++i) {
        atomic_store_explicit(&state_.threads[i].pending_events, 10, memory_order_release);
    }
    atomic_store_explicit(&state_.capacity, MAX_THREADS + 100, memory_order_release);

    testing::internal::CaptureStderr();
    shutdown_manager_print_summary(&manager_);
    std::string stderr_output = testing::internal::GetCapturedStderr();

    using ::testing::HasSubstr;
    // Should process only MAX_THREADS worth of events
    uint64_t expected_events = MAX_THREADS * 10;
    EXPECT_THAT(stderr_output, HasSubstr("Events In Flight at Shutdown: " + std::to_string(expected_events)));
}

TEST_F(ShutdownManagerTest, signal_handler_count_increments__then_tracks_signals) {
    install_handler();

    uint64_t initial_count = atomic_load_explicit(&handler_.signal_count, memory_order_acquire);
    EXPECT_EQ(initial_count, 0u);

    raise(SIGINT);
    usleep(1000);
    uint64_t after_sigint = atomic_load_explicit(&handler_.signal_count, memory_order_acquire);
    EXPECT_GT(after_sigint, initial_count);

    raise(SIGTERM);
    usleep(1000);
    uint64_t after_sigterm = atomic_load_explicit(&handler_.signal_count, memory_order_acquire);
    EXPECT_GT(after_sigterm, after_sigint);
}
