#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tracer_backend/controller/shutdown.h>
#include <tracer_backend/drain_thread/drain_thread.h>
#include <tracer_backend/timer/timer.h>
#include <tracer_backend/utils/thread_registry.h>

static _Atomic(ShutdownManager*) g_active_manager = NULL;
static _Atomic(SignalHandler*) g_active_handler = NULL;

static int default_timer_cancel(void) {
    return timer_cancel();
}

static void shutdown_manager_snapshot_registry(ShutdownManager* manager);
static void shutdown_manager_stop_threads(ShutdownManager* manager);
static void shutdown_manager_wait_for_drain_completion(ShutdownManager* manager);
static uint64_t shutdown_manager_sync_files(ShutdownManager* manager);
static uint64_t shutdown_manager_events_in_flight(const ShutdownManager* manager);
static double shutdown_manager_duration_ms(const ShutdownManager* manager);
static void handle_shutdown_signal(int sig);

void shutdown_state_init(ShutdownState* state, uint32_t capacity) {
    if (!state) {
        return;
    }

    if (capacity == 0 || capacity > MAX_THREADS) {
        capacity = MAX_THREADS;
    }

    for (uint32_t i = 0; i < MAX_THREADS; ++i) {
        atomic_init(&state->threads[i].accepting_events, false);
        atomic_init(&state->threads[i].flush_requested, false);
        atomic_init(&state->threads[i].flush_complete, false);
        atomic_init(&state->threads[i].pending_events, 0);
    }

    atomic_init(&state->capacity, capacity);
    atomic_init(&state->active_threads, 0);
    atomic_init(&state->threads_stopped, 0);
    atomic_init(&state->threads_flushed, 0);
}

void shutdown_state_mark_active(ShutdownState* state, uint32_t slot_index) {
    if (!state) {
        return;
    }

    uint32_t capacity = atomic_load_explicit(&state->capacity, memory_order_acquire);
    if (slot_index >= capacity) {
        return;
    }

    ShutdownThreadState* thread = &state->threads[slot_index];
    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(&thread->accepting_events,
                                                &expected,
                                                true,
                                                memory_order_acq_rel,
                                                memory_order_acquire)) {
        atomic_fetch_add_explicit(&state->active_threads, 1, memory_order_acq_rel);
    } else {
        atomic_store_explicit(&thread->accepting_events, true, memory_order_release);
    }

    atomic_store_explicit(&thread->flush_requested, false, memory_order_release);
    atomic_store_explicit(&thread->flush_complete, false, memory_order_release);
    atomic_store_explicit(&thread->pending_events, 0, memory_order_release);
}

void shutdown_state_mark_inactive(ShutdownState* state, uint32_t slot_index) {
    if (!state) {
        return;
    }

    uint32_t capacity = atomic_load_explicit(&state->capacity, memory_order_acquire);
    if (slot_index >= capacity) {
        return;
    }

    ShutdownThreadState* thread = &state->threads[slot_index];
    bool was_active = atomic_exchange_explicit(&thread->accepting_events, false, memory_order_acq_rel);
    if (was_active) {
        atomic_fetch_sub_explicit(&state->active_threads, 1, memory_order_acq_rel);
    }
    atomic_store_explicit(&thread->pending_events, 0, memory_order_release);
}

void shutdown_state_record_pending(ShutdownState* state,
                                   uint32_t slot_index,
                                   uint64_t pending_events) {
    if (!state) {
        return;
    }

    uint32_t capacity = atomic_load_explicit(&state->capacity, memory_order_acquire);
    if (slot_index >= capacity) {
        return;
    }

    atomic_store_explicit(&state->threads[slot_index].pending_events,
                          pending_events,
                          memory_order_release);
}

static void shutdown_manager_assign_ops(ShutdownManager* manager, const ShutdownOps* ops) {
    if (!manager) {
        return;
    }

    if (ops) {
        manager->ops = *ops;
    } else {
        manager->ops.cancel_timer = default_timer_cancel;
        manager->ops.stop_drain = NULL;
    }

    if (!manager->ops.cancel_timer) {
        manager->ops.cancel_timer = default_timer_cancel;
    }
}

int shutdown_manager_init(ShutdownManager* manager,
                          ShutdownState* state,
                          ThreadRegistry* registry,
                          DrainThread* drain,
                          const ShutdownOps* ops) {
    if (!manager) {
        return -EINVAL;
    }

    memset(manager, 0, sizeof(*manager));

    manager->state = state;
    manager->registry = registry;
    manager->drain_thread = drain;
    manager->wake_read_fd = -1;
    manager->wake_write_fd = -1;
    manager->timestamp_valid = false;

    atomic_init(&manager->shutdown_requested, false);
    atomic_init(&manager->shutdown_completed, false);
    atomic_init(&manager->phase, SHUTDOWN_PHASE_IDLE);
    atomic_init(&manager->last_signal, 0);
    atomic_init(&manager->last_reason, SHUTDOWN_REASON_NONE);
    atomic_init(&manager->request_count, 0);

    shutdown_manager_assign_ops(manager, ops);

    if (manager->state) {
        uint32_t capacity = atomic_load_explicit(&manager->state->capacity, memory_order_relaxed);
        if (capacity == 0) {
            shutdown_state_init(manager->state, MAX_THREADS);
        }
    }

    return 0;
}

void shutdown_manager_reset(ShutdownManager* manager) {
    if (!manager) {
        return;
    }

    atomic_store_explicit(&manager->shutdown_requested, false, memory_order_release);
    atomic_store_explicit(&manager->shutdown_completed, false, memory_order_release);
    atomic_store_explicit(&manager->phase, SHUTDOWN_PHASE_IDLE, memory_order_release);
    atomic_store_explicit(&manager->last_signal, 0, memory_order_release);
    atomic_store_explicit(&manager->last_reason, SHUTDOWN_REASON_NONE, memory_order_release);
    atomic_store_explicit(&manager->request_count, 0, memory_order_release);
    manager->timestamp_valid = false;
    manager->start_ts = (struct timespec){0};
    manager->end_ts = (struct timespec){0};
    manager->files_synced = 0;
}

void shutdown_manager_set_registry(ShutdownManager* manager, ThreadRegistry* registry) {
    if (!manager) {
        return;
    }
    manager->registry = registry;
}

void shutdown_manager_set_drain_thread(ShutdownManager* manager, DrainThread* drain) {
    if (!manager) {
        return;
    }
    manager->drain_thread = drain;
}

void shutdown_manager_set_ops(ShutdownManager* manager, const ShutdownOps* ops) {
    if (!manager) {
        return;
    }
    shutdown_manager_assign_ops(manager, ops);
}

void shutdown_manager_set_wakeup_fds(ShutdownManager* manager, int read_fd, int write_fd) {
    if (!manager) {
        return;
    }
    manager->wake_read_fd = read_fd;
    manager->wake_write_fd = write_fd;
}

bool shutdown_manager_request_shutdown(ShutdownManager* manager,
                                       ShutdownReason reason,
                                       int signal_number) {
    if (!manager) {
        return false;
    }

    atomic_fetch_add_explicit(&manager->request_count, 1, memory_order_relaxed);

    bool already_requested = atomic_exchange_explicit(&manager->shutdown_requested,
                                                      true,
                                                      memory_order_acq_rel);

    atomic_store_explicit(&manager->last_reason, reason, memory_order_release);
    atomic_store_explicit(&manager->last_signal, signal_number, memory_order_release);

    if (!already_requested) {
        atomic_store_explicit(&manager->phase, SHUTDOWN_PHASE_SIGNAL_RECEIVED, memory_order_release);
        if (manager->ops.cancel_timer) {
            manager->ops.cancel_timer();
        }
    }

    return !already_requested;
}

bool shutdown_manager_is_shutdown_requested(const ShutdownManager* manager) {
    if (!manager) {
        return false;
    }
    return atomic_load_explicit(&manager->shutdown_requested, memory_order_acquire);
}

bool shutdown_manager_is_shutdown_complete(const ShutdownManager* manager) {
    if (!manager) {
        return false;
    }
    return atomic_load_explicit(&manager->shutdown_completed, memory_order_acquire);
}

ShutdownPhase shutdown_manager_get_phase(const ShutdownManager* manager) {
    if (!manager) {
        return SHUTDOWN_PHASE_IDLE;
    }
    return (ShutdownPhase)atomic_load_explicit(&manager->phase, memory_order_acquire);
}

uint64_t shutdown_manager_get_request_count(const ShutdownManager* manager) {
    if (!manager) {
        return 0;
    }
    return atomic_load_explicit(&manager->request_count, memory_order_acquire);
}

int shutdown_manager_get_last_signal(const ShutdownManager* manager) {
    if (!manager) {
        return 0;
    }
    return atomic_load_explicit(&manager->last_signal, memory_order_acquire);
}

int shutdown_manager_get_last_reason(const ShutdownManager* manager) {
    if (!manager) {
        return SHUTDOWN_REASON_NONE;
    }
    return atomic_load_explicit(&manager->last_reason, memory_order_acquire);
}

static void shutdown_manager_snapshot_registry(ShutdownManager* manager) {
    if (!manager || !manager->registry || !manager->state) {
        return;
    }

    uint32_t capacity = thread_registry_get_capacity(manager->registry);
    if (capacity == 0 || capacity > MAX_THREADS) {
        capacity = MAX_THREADS;
    }

    atomic_store_explicit(&manager->state->capacity, capacity, memory_order_release);

    for (uint32_t i = 0; i < capacity; ++i) {
        ThreadLaneSet* lanes = thread_registry_get_thread_at(manager->registry, i);
        if (lanes) {
            shutdown_state_mark_active(manager->state, i);
        } else {
            shutdown_state_mark_inactive(manager->state, i);
        }
    }
}

static void shutdown_manager_stop_threads(ShutdownManager* manager) {
    if (!manager || !manager->state) {
        return;
    }

    if (manager->registry) {
        shutdown_manager_snapshot_registry(manager);
    }

    uint32_t capacity = atomic_load_explicit(&manager->state->capacity, memory_order_acquire);
    uint32_t stopped = 0;
    uint32_t flushed = 0;

    for (uint32_t i = 0; i < capacity; ++i) {
        ShutdownThreadState* thread = &manager->state->threads[i];
        bool was_accepting = atomic_exchange_explicit(&thread->accepting_events,
                                                      false,
                                                      memory_order_acq_rel);
        if (was_accepting) {
            ++stopped;
        }

        atomic_store_explicit(&thread->flush_requested, true, memory_order_release);
        bool flush_complete_before = atomic_exchange_explicit(&thread->flush_complete,
                                                               true,
                                                               memory_order_acq_rel);
        if (was_accepting && !flush_complete_before) {
            ++flushed;
        }
    }

    if (stopped > 0 ||
        atomic_load_explicit(&manager->state->threads_stopped, memory_order_acquire) == 0) {
        atomic_store_explicit(&manager->state->threads_stopped,
                              stopped,
                              memory_order_release);
    }

    if (flushed > 0 ||
        atomic_load_explicit(&manager->state->threads_flushed, memory_order_acquire) == 0) {
        atomic_store_explicit(&manager->state->threads_flushed,
                              flushed,
                              memory_order_release);
    }
}

static void shutdown_manager_wait_for_drain_completion(ShutdownManager* manager) {
    if (!manager || !manager->drain_thread) {
        return;
    }

    const int max_attempts = 1000;
    const struct timespec sleep_interval = {.tv_sec = 0, .tv_nsec = 1000000};  // 1 ms
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        DrainState state = drain_thread_get_state(manager->drain_thread);
        if (state == DRAIN_STATE_STOPPED || state == DRAIN_STATE_UNINITIALIZED) {
            return;
        }
        (void)nanosleep(&sleep_interval, NULL);
    }
}

static uint64_t shutdown_manager_sync_files(ShutdownManager* manager) {
    // TODO: Update to use ATF V2 session API
    (void)manager;
    return 0;
}

static uint64_t shutdown_manager_events_in_flight(const ShutdownManager* manager) {
    if (!manager || !manager->state) {
        return 0;
    }

    const ShutdownState* state = manager->state;
    uint32_t capacity = atomic_load_explicit(&state->capacity, memory_order_acquire);
    if (capacity == 0 || capacity > MAX_THREADS) {
        capacity = MAX_THREADS;
    }

    uint64_t total = 0;
    for (uint32_t i = 0; i < capacity; ++i) {
        total += atomic_load_explicit(&state->threads[i].pending_events,
                                      memory_order_acquire);
    }
    return total;
}

static double shutdown_manager_duration_ms(const ShutdownManager* manager) {
    if (!manager || !manager->timestamp_valid) {
        return 0.0;
    }

    time_t sec_delta = manager->end_ts.tv_sec - manager->start_ts.tv_sec;
    long nsec_delta = manager->end_ts.tv_nsec - manager->start_ts.tv_nsec;

    double duration = (double)sec_delta * 1000.0 + (double)nsec_delta / 1000000.0;
    if (duration < 0.0) {
        duration = 0.0;
    }
    return duration;
}

void shutdown_manager_execute(ShutdownManager* manager) {
    if (!manager) {
        return;
    }

    if (!atomic_load_explicit(&manager->shutdown_requested, memory_order_acquire)) {
        return;
    }

    if (atomic_load_explicit(&manager->shutdown_completed, memory_order_acquire)) {
        return;
    }

    if (!manager->timestamp_valid) {
        if (clock_gettime(CLOCK_MONOTONIC, &manager->start_ts) == 0) {
            manager->timestamp_valid = true;
        }
    }

    atomic_store_explicit(&manager->phase, SHUTDOWN_PHASE_STOPPING_THREADS, memory_order_release);
    shutdown_manager_stop_threads(manager);

    atomic_store_explicit(&manager->phase, SHUTDOWN_PHASE_DRAINING, memory_order_release);
    if (manager->ops.stop_drain && manager->drain_thread) {
        manager->ops.stop_drain(manager->drain_thread);
    }
    shutdown_manager_wait_for_drain_completion(manager);

    manager->files_synced = shutdown_manager_sync_files(manager);

    (void)clock_gettime(CLOCK_MONOTONIC, &manager->end_ts);

    shutdown_manager_print_summary(manager);

    atomic_store_explicit(&manager->phase, SHUTDOWN_PHASE_COMPLETED, memory_order_release);
    atomic_store_explicit(&manager->shutdown_completed, true, memory_order_release);
}

void shutdown_manager_print_summary(const ShutdownManager* manager) {
    if (!manager) {
        return;
    }

    double duration_ms = shutdown_manager_duration_ms(manager);

    // TODO: Get metrics from ATF V2 session
    uint64_t total_events = 0;
    uint64_t bytes_written = 0;
    (void)manager; // Suppress unused warning for now

    uint64_t events_in_flight = shutdown_manager_events_in_flight(manager);

    uint32_t threads_flushed = 0;
    uint32_t total_threads = 0;
    if (manager->state) {
        threads_flushed = atomic_load_explicit(&manager->state->threads_flushed,
                                               memory_order_acquire);
        total_threads = atomic_load_explicit(&manager->state->active_threads,
                                             memory_order_acquire);
    }

    fprintf(stderr, "=== ADA Tracer Shutdown Summary ===\n");
    fprintf(stderr, "Shutdown Duration: %.2f ms\n", duration_ms);
    fprintf(stderr, "Total Events Processed: %llu\n",
            (unsigned long long)total_events);
    fprintf(stderr, "Events In Flight at Shutdown: %llu\n",
            (unsigned long long)events_in_flight);
    fprintf(stderr, "Bytes Written: %llu\n", (unsigned long long)bytes_written);
    fprintf(stderr, "Files Synced: %llu\n", (unsigned long long)manager->files_synced);
    fprintf(stderr, "Threads Flushed: %u/%u\n", threads_flushed, total_threads);
    fprintf(stderr, "================================\n");
}

void shutdown_manager_register_global(ShutdownManager* manager) {
    atomic_store_explicit(&g_active_manager, manager, memory_order_release);
}

void shutdown_manager_unregister_global(void) {
    atomic_store_explicit(&g_active_manager, NULL, memory_order_release);
}

void shutdown_manager_signal_wakeup(const ShutdownManager* manager) {
    if (!manager) {
        return;
    }

    int fd = manager->wake_write_fd;
    if (fd < 0) {
        return;
    }

    const uint64_t value = 1;
    (void)write(fd, &value, sizeof(value));
}

int signal_handler_init(SignalHandler* handler, ShutdownManager* manager) {
    if (!handler) {
        return -EINVAL;
    }

    memset(handler, 0, sizeof(*handler));
    handler->manager = manager;
    atomic_init(&handler->installed, false);
    atomic_init(&handler->signal_count, 0);
    return 0;
}

static void handle_shutdown_signal(int sig) {
    SignalHandler* handler = atomic_load_explicit(&g_active_handler, memory_order_acquire);
    if (handler) {
        atomic_fetch_add_explicit(&handler->signal_count, 1, memory_order_relaxed);
    }

    ShutdownManager* manager = NULL;
    if (handler && handler->manager) {
        manager = handler->manager;
    }
    if (!manager) {
        manager = atomic_load_explicit(&g_active_manager, memory_order_acquire);
    }

    if (manager) {
        shutdown_manager_request_shutdown(manager, SHUTDOWN_REASON_SIGNAL, sig);
        shutdown_manager_signal_wakeup(manager);
    }
}

int signal_handler_install(SignalHandler* handler) {
    if (!handler) {
        return -EINVAL;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_shutdown_signal;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
#ifdef SA_RESTART
    action.sa_flags = SA_RESTART;
#endif

    if (sigaction(SIGINT, &action, &handler->previous_sigint) != 0) {
        return -1;
    }

    if (sigaction(SIGTERM, &action, &handler->previous_sigterm) != 0) {
        (void)sigaction(SIGINT, &handler->previous_sigint, NULL);
        return -1;
    }

    atomic_store_explicit(&handler->installed, true, memory_order_release);
    atomic_store_explicit(&g_active_handler, handler, memory_order_release);
    return 0;
}

void signal_handler_uninstall(SignalHandler* handler) {
    if (!handler) {
        return;
    }

    bool was_installed = atomic_exchange_explicit(&handler->installed,
                                                  false,
                                                  memory_order_acq_rel);
    if (!was_installed) {
        return;
    }

    (void)sigaction(SIGINT, &handler->previous_sigint, NULL);
    (void)sigaction(SIGTERM, &handler->previous_sigterm, NULL);

    SignalHandler* expected = handler;
    atomic_compare_exchange_strong_explicit(&g_active_handler,
                                            &expected,
                                            NULL,
                                            memory_order_acq_rel,
                                            memory_order_acquire);
}

void shutdown_initiate(void) {
    ShutdownManager* manager = atomic_load_explicit(&g_active_manager, memory_order_acquire);
    if (!manager) {
        return;
    }

    bool first = shutdown_manager_request_shutdown(manager, SHUTDOWN_REASON_TIMER, 0);
    if (first) {
        shutdown_manager_signal_wakeup(manager);
    }
}
