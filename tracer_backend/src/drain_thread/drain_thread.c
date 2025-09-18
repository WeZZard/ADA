#include "drain_thread_private.h"

#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__has_attribute)
#if __has_attribute(weak)
#define ADA_WEAK_SYMBOL __attribute__((weak))
#else
#define ADA_WEAK_SYMBOL
#endif
#else
#define ADA_WEAK_SYMBOL
#endif

// Test hook stubs. Unit tests provide strong overrides when needed. In
// production builds these weak definitions simply signal that the call was not
// handled so we fall back to the real pthread/lane APIs.
ADA_WEAK_SYMBOL int drain_thread_test_override_pthread_mutex_init(pthread_mutex_t* mutex,
                                                                  const pthread_mutexattr_t* attr,
                                                                  bool* handled) {
    (void)mutex;
    (void)attr;
    if (handled) {
        *handled = false;
    }
    return 0;
}

ADA_WEAK_SYMBOL int drain_thread_test_override_pthread_create(pthread_t* thread,
                                                              const pthread_attr_t* attr,
                                                              void* (*start_routine)(void*),
                                                              void* arg,
                                                              bool* handled) {
    (void)thread;
    (void)attr;
    (void)start_routine;
    (void)arg;
    if (handled) {
        *handled = false;
    }
    return 0;
}

ADA_WEAK_SYMBOL int drain_thread_test_override_pthread_join(pthread_t thread,
                                                            void** retval,
                                                            bool* handled) {
    (void)thread;
    (void)retval;
    if (handled) {
        *handled = false;
    }
    return 0;
}

ADA_WEAK_SYMBOL bool drain_thread_test_override_lane_return_ring(Lane* lane,
                                                                 uint32_t ring_idx,
                                                                 bool* handled) {
    (void)lane;
    (void)ring_idx;
    if (handled) {
        *handled = false;
    }
    return false;
}

ADA_WEAK_SYMBOL void* drain_thread_test_override_calloc(size_t nmemb, size_t size, bool* handled) {
    (void)nmemb;
    (void)size;
    if (handled) {
        *handled = false;
    }
    return NULL;
}

static int drain_thread_call_pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    if (drain_thread_test_override_pthread_mutex_init) {
        bool handled = false;
        int rc = drain_thread_test_override_pthread_mutex_init(mutex, attr, &handled);
        if (handled) {
            return rc;
        }
    }
    return pthread_mutex_init(mutex, attr);
}

static int drain_thread_call_pthread_create(pthread_t* thread,
                                            const pthread_attr_t* attr,
                                            void* (*start_routine)(void*),
                                            void* arg) {
    if (drain_thread_test_override_pthread_create) {
        bool handled = false;
        int rc = drain_thread_test_override_pthread_create(thread, attr, start_routine, arg, &handled);
        if (handled) {
            return rc;
        }
    }
    return pthread_create(thread, attr, start_routine, arg);
}

static int drain_thread_call_pthread_join(pthread_t thread, void** retval) {
    if (drain_thread_test_override_pthread_join) {
        bool handled = false;
        int rc = drain_thread_test_override_pthread_join(thread, retval, &handled);
        if (handled) {
            return rc;
        }
    }
    return pthread_join(thread, retval);
}

static bool drain_thread_call_lane_return_ring(Lane* lane, uint32_t ring_idx) {
    if (drain_thread_test_override_lane_return_ring) {
        bool handled = false;
        bool result = drain_thread_test_override_lane_return_ring(lane, ring_idx, &handled);
        if (handled) {
            return result;
        }
    }
    return lane_return_ring(lane, ring_idx);
}

static void* drain_thread_call_calloc(size_t nmemb, size_t size) {
    if (drain_thread_test_override_calloc) {
        bool handled = false;
        void* ptr = drain_thread_test_override_calloc(nmemb, size, &handled);
        if (handled) {
            return ptr;
        }
    }
    return calloc(nmemb, size);
}

// --------------------------------------------------------------------------------------
// Internal helpers
// --------------------------------------------------------------------------------------

static inline uint64_t monotonic_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static void drain_metrics_atomic_reset(DrainMetricsAtomic* m) {
    atomic_init(&m->cycles_total, 0);
    atomic_init(&m->cycles_idle, 0);
    atomic_init(&m->rings_total, 0);
    atomic_init(&m->rings_index, 0);
    atomic_init(&m->rings_detail, 0);
    atomic_init(&m->fairness_switches, 0);
    atomic_init(&m->sleeps, 0);
    atomic_init(&m->yields, 0);
    atomic_init(&m->final_drains, 0);
    atomic_init(&m->total_sleep_us, 0);
    for (uint32_t i = 0; i < MAX_THREADS; ++i) {
        atomic_init(&m->per_thread_rings[i][0], 0);
        atomic_init(&m->per_thread_rings[i][1], 0);
    }
}

static uint32_t compute_effective_limit(const DrainThread* drain, bool final_pass) {
    if (final_pass) {
        return UINT32_MAX;
    }
    uint32_t limit = drain->config.max_batch_size;
    uint32_t quantum = drain->config.fairness_quantum;
    if (limit == 0) {
        limit = quantum;
    } else if (quantum > 0 && quantum < limit) {
        limit = quantum;
    }
    if (limit == 0) {
        return UINT32_MAX;
    }
    return limit;
}

static void return_ring_to_producer(Lane* lane, uint32_t ring_idx) {
    if (!lane) {
        return;
    }
    // Retry until the ring is successfully returned. This should normally succeed immediately.
    for (int attempts = 0; attempts < 1000; ++attempts) {
        if (drain_thread_call_lane_return_ring(lane, ring_idx)) {
            return;
        }
        sched_yield();
    }
    // Last resort: busy wait to avoid losing the ring.
    while (!drain_thread_call_lane_return_ring(lane, ring_idx)) {
        sched_yield();
    }
}

static uint32_t drain_lane(DrainThread* drain,
                           uint32_t slot_index,
                           Lane* lane,
                           bool is_detail,
                           bool final_pass,
                           bool* out_hit_limit) {
    if (!lane) {
        if (out_hit_limit) {
            *out_hit_limit = false;
        }
        return 0;
    }

    const uint32_t limit = compute_effective_limit(drain, final_pass);
    uint32_t processed = 0;

    while (processed < limit) {
        uint32_t ring_idx = lane_take_ring(lane);
        if (ring_idx == UINT32_MAX) {
            break;
        }
        return_ring_to_producer(lane, ring_idx);
        ++processed;
    }

    if (out_hit_limit) {
        *out_hit_limit = (limit != UINT32_MAX) && (processed == limit);
    }

    if (processed == 0) {
        return 0;
    }

    atomic_fetch_add_explicit(&drain->metrics.rings_total, processed, memory_order_relaxed);
    if (is_detail) {
        atomic_fetch_add_explicit(&drain->metrics.rings_detail, processed, memory_order_relaxed);
    } else {
        atomic_fetch_add_explicit(&drain->metrics.rings_index, processed, memory_order_relaxed);
    }

    if (slot_index < MAX_THREADS) {
        atomic_fetch_add_explicit(&drain->metrics.per_thread_rings[slot_index][is_detail ? 1 : 0],
                                  processed,
                                  memory_order_relaxed);
    }

    return processed;
}

static bool drain_cycle(DrainThread* drain, bool final_pass) {
    if (!drain || !drain->registry) {
        return false;
    }

    const uint32_t capacity = thread_registry_get_capacity(drain->registry);
    if (capacity == 0) {
        return false;
    }

    uint32_t start = atomic_load_explicit(&drain->rr_cursor, memory_order_relaxed);
    if (start >= capacity) {
        start = 0;
    }

    bool work_done = false;

    for (uint32_t offset = 0; offset < capacity; ++offset) {
        uint32_t slot = (start + offset) % capacity;
        ThreadLaneSet* lanes = thread_registry_get_thread_at(drain->registry, slot);
        if (!lanes) {
            continue;
        }

        bool hit_limit = false;

        Lane* index_lane = thread_lanes_get_index_lane(lanes);
        uint32_t processed = drain_lane(drain, slot, index_lane, false, final_pass, &hit_limit);
        if (processed > 0) {
            work_done = true;
        }
        if (hit_limit) {
            atomic_fetch_add_explicit(&drain->metrics.fairness_switches, 1, memory_order_relaxed);
        }

        hit_limit = false;
        Lane* detail_lane = thread_lanes_get_detail_lane(lanes);
        processed = drain_lane(drain, slot, detail_lane, true, final_pass, &hit_limit);
        if (processed > 0) {
            work_done = true;
        }
        if (hit_limit) {
            atomic_fetch_add_explicit(&drain->metrics.fairness_switches, 1, memory_order_relaxed);
        }
    }

    atomic_store_explicit(&drain->rr_cursor, (start + 1) % capacity, memory_order_relaxed);
    atomic_store_explicit(&drain->last_cycle_ns, monotonic_now_ns(), memory_order_relaxed);

    return work_done;
}

static void drain_metrics_snapshot(const DrainThread* drain, DrainMetrics* out) {
    if (!drain || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    const DrainMetricsAtomic* src = &drain->metrics;
    out->cycles_total = atomic_load_explicit(&src->cycles_total, memory_order_relaxed);
    out->cycles_idle = atomic_load_explicit(&src->cycles_idle, memory_order_relaxed);
    out->rings_total = atomic_load_explicit(&src->rings_total, memory_order_relaxed);
    out->rings_index = atomic_load_explicit(&src->rings_index, memory_order_relaxed);
    out->rings_detail = atomic_load_explicit(&src->rings_detail, memory_order_relaxed);
    out->fairness_switches = atomic_load_explicit(&src->fairness_switches, memory_order_relaxed);
    out->sleeps = atomic_load_explicit(&src->sleeps, memory_order_relaxed);
    out->yields = atomic_load_explicit(&src->yields, memory_order_relaxed);
    out->final_drains = atomic_load_explicit(&src->final_drains, memory_order_relaxed);
    out->total_sleep_us = atomic_load_explicit(&src->total_sleep_us, memory_order_relaxed);
    for (uint32_t i = 0; i < MAX_THREADS; ++i) {
        out->rings_per_thread[i][0] = atomic_load_explicit(&src->per_thread_rings[i][0], memory_order_relaxed);
        out->rings_per_thread[i][1] = atomic_load_explicit(&src->per_thread_rings[i][1], memory_order_relaxed);
    }
}

static void* drain_worker_thread(void* arg) {
    DrainThread* drain = (DrainThread*)arg;
    if (!drain) {
        return NULL;
    }

#if defined(__APPLE__)
    pthread_setname_np("ada_drain");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "ada_drain");
#endif

    while (atomic_load_explicit(&drain->state, memory_order_acquire) == DRAIN_STATE_RUNNING) {
        bool work = drain_cycle(drain, false);
        atomic_fetch_add_explicit(&drain->metrics.cycles_total, 1, memory_order_relaxed);
        if (!work) {
            atomic_fetch_add_explicit(&drain->metrics.cycles_idle, 1, memory_order_relaxed);
            if (drain->config.yield_on_idle) {
                sched_yield();
                atomic_fetch_add_explicit(&drain->metrics.yields, 1, memory_order_relaxed);
            } else if (drain->config.poll_interval_us > 0) {
                usleep(drain->config.poll_interval_us);
                atomic_fetch_add_explicit(&drain->metrics.sleeps, 1, memory_order_relaxed);
                atomic_fetch_add_explicit(&drain->metrics.total_sleep_us,
                                          drain->config.poll_interval_us,
                                          memory_order_relaxed);
            }
        }
    }

    // Final drain when stopping
    atomic_fetch_add_explicit(&drain->metrics.final_drains, 1, memory_order_relaxed);
    bool had_work;
    do {
        had_work = drain_cycle(drain, true);
        atomic_fetch_add_explicit(&drain->metrics.cycles_total, 1, memory_order_relaxed);
        if (!had_work) {
            break;
        }
    } while (had_work);

    atomic_store_explicit(&drain->state, DRAIN_STATE_STOPPED, memory_order_release);
    return NULL;
}

// --------------------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------------------

void drain_config_default(DrainConfig* config) {
    if (!config) {
        return;
    }
    config->poll_interval_us = 1000;   // 1ms idle sleep by default
    config->max_batch_size = 8;
    config->fairness_quantum = 8;
    config->yield_on_idle = false;
}

DrainThread* drain_thread_create(ThreadRegistry* registry, const DrainConfig* config) {
    if (!registry) {
        return NULL;
    }

    DrainThread* drain = (DrainThread*)drain_thread_call_calloc(1, sizeof(DrainThread));
    if (!drain) {
        return NULL;
    }

    DrainConfig local_config;
    if (config) {
        local_config = *config;
    } else {
        drain_config_default(&local_config);
    }

    drain->registry = registry;
    drain->config = local_config;
    drain->thread_started = false;

    drain_metrics_atomic_reset(&drain->metrics);

    atomic_init(&drain->state, DRAIN_STATE_INITIALIZED);
    atomic_init(&drain->rr_cursor, 0);
    atomic_init(&drain->last_cycle_ns, monotonic_now_ns());

    if (drain_thread_call_pthread_mutex_init(&drain->lifecycle_lock, NULL) != 0) {
        free(drain);
        return NULL;
    }

    return drain;
}

int drain_thread_start(DrainThread* drain) {
    if (!drain) {
        return -EINVAL;
    }

    pthread_mutex_lock(&drain->lifecycle_lock);

    int expected = DRAIN_STATE_INITIALIZED;
    if (!atomic_compare_exchange_strong_explicit(&drain->state,
                                                 &expected,
                                                 DRAIN_STATE_RUNNING,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        pthread_mutex_unlock(&drain->lifecycle_lock);
        if (expected == DRAIN_STATE_RUNNING) {
            return 0; // already running
        }
        if (expected == DRAIN_STATE_STOPPING || expected == DRAIN_STATE_STOPPED) {
            return -EALREADY;
        }
        return -EINVAL;
    }

    int rc = drain_thread_call_pthread_create(&drain->worker, NULL, drain_worker_thread, drain);
    if (rc != 0) {
        atomic_store_explicit(&drain->state, DRAIN_STATE_INITIALIZED, memory_order_release);
        pthread_mutex_unlock(&drain->lifecycle_lock);
        return rc;
    }

    drain->thread_started = true;

    pthread_mutex_unlock(&drain->lifecycle_lock);
    return 0;
}

int drain_thread_stop(DrainThread* drain) {
    if (!drain) {
        return -EINVAL;
    }

    pthread_mutex_lock(&drain->lifecycle_lock);

    int state = atomic_load_explicit(&drain->state, memory_order_acquire);
    if (state == DRAIN_STATE_INITIALIZED) {
        pthread_mutex_unlock(&drain->lifecycle_lock);
        return 0; // nothing to stop
    }

    if (state == DRAIN_STATE_STOPPED) {
        bool started = drain->thread_started;
        pthread_mutex_unlock(&drain->lifecycle_lock);
        if (started) {
            int join_rc = drain_thread_call_pthread_join(drain->worker, NULL);
            pthread_mutex_lock(&drain->lifecycle_lock);
            drain->thread_started = false;
            pthread_mutex_unlock(&drain->lifecycle_lock);
            return join_rc;
        }
        return 0;
    }

    if (state == DRAIN_STATE_RUNNING) {
        atomic_store_explicit(&drain->state, DRAIN_STATE_STOPPING, memory_order_release);
    }

    bool started = drain->thread_started;
    pthread_mutex_unlock(&drain->lifecycle_lock);

    int rc = 0;
    if (started) {
        int join_rc = drain_thread_call_pthread_join(drain->worker, NULL);
        if (join_rc != 0) {
            rc = join_rc;
        }
        pthread_mutex_lock(&drain->lifecycle_lock);
        drain->thread_started = false;
        pthread_mutex_unlock(&drain->lifecycle_lock);
    }

    return rc;
}

void drain_thread_destroy(DrainThread* drain) {
    if (!drain) {
        return;
    }

    DrainState state = atomic_load_explicit(&drain->state, memory_order_acquire);
    if (state == DRAIN_STATE_RUNNING || state == DRAIN_STATE_STOPPING) {
        (void)drain_thread_stop(drain);
    }

    pthread_mutex_destroy(&drain->lifecycle_lock);
    free(drain);
}

DrainState drain_thread_get_state(const DrainThread* drain) {
    if (!drain) {
        return DRAIN_STATE_UNINITIALIZED;
    }
    return (DrainState)atomic_load_explicit(&drain->state, memory_order_acquire);
}

void drain_thread_get_metrics(const DrainThread* drain, DrainMetrics* out_metrics) {
    drain_metrics_snapshot(drain, out_metrics);
}

int drain_thread_update_config(DrainThread* drain, const DrainConfig* config) {
    if (!drain || !config) {
        return -EINVAL;
    }

    DrainState state = (DrainState)atomic_load_explicit(&drain->state, memory_order_acquire);
    if (state == DRAIN_STATE_RUNNING || state == DRAIN_STATE_STOPPING) {
        return -EBUSY;
    }

    pthread_mutex_lock(&drain->lifecycle_lock);
    drain->config = *config;
    pthread_mutex_unlock(&drain->lifecycle_lock);
    return 0;
}

// --------------------------------------------------------------------------------------
// Test helpers (no-op in production, used by unit tests via weak hooks)
// --------------------------------------------------------------------------------------

uint32_t drain_thread_test_drain_lane(DrainThread* drain,
                                      uint32_t slot_index,
                                      Lane* lane,
                                      bool is_detail,
                                      bool final_pass,
                                      bool* out_hit_limit) {
    return drain_lane(drain, slot_index, lane, is_detail, final_pass, out_hit_limit);
}

bool drain_thread_test_cycle(DrainThread* drain, bool final_pass) {
    return drain_cycle(drain, final_pass);
}

void drain_thread_test_return_ring(Lane* lane, uint32_t ring_idx) {
    return_ring_to_producer(lane, ring_idx);
}

void drain_thread_test_set_state(DrainThread* drain, DrainState state) {
    if (!drain) {
        return;
    }
    atomic_store_explicit(&drain->state, state, memory_order_release);
}

void drain_thread_test_set_thread_started(DrainThread* drain, bool started) {
    if (!drain) {
        return;
    }
    drain->thread_started = started;
}

void drain_thread_test_set_worker(DrainThread* drain, pthread_t worker) {
    if (!drain) {
        return;
    }
    drain->worker = worker;
}

void drain_thread_test_set_rr_cursor(DrainThread* drain, uint32_t value) {
    if (!drain) {
        return;
    }
    atomic_store_explicit(&drain->rr_cursor, value, memory_order_relaxed);
}

uint32_t drain_thread_test_get_rr_cursor(const DrainThread* drain) {
    if (!drain) {
        return 0;
    }
    return atomic_load_explicit(&drain->rr_cursor, memory_order_relaxed);
}

void drain_thread_test_set_registry(DrainThread* drain, ThreadRegistry* registry) {
    if (!drain) {
        return;
    }
    drain->registry = registry;
}

void* drain_thread_test_worker_entry(void* arg) {
    return drain_worker_thread(arg);
}
