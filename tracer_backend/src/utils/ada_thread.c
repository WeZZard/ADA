#include <tracer_backend/ada/thread.h>

#include <pthread.h>
#include <string.h>
#include <time.h>

// Keep tls_my_lanes in sync with TLS fast path
extern __thread ThreadLaneSet* tls_my_lanes;

// TLS state
static __thread ada_tls_state_t g_tls_state = {0};
static __thread bool g_tls_bp_initialized = false;


// Global registry pointer (set by controller/agent runtime)
static _Atomic(ThreadRegistry*) g_global_registry = NULL;

static uint64_t ada_now_monotonic_ns(void) {
#ifdef __APPLE__
    // Fallback simple clock for C file; agent has mach for high-res
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

ada_tls_state_t* ada_get_tls_state(void) {
    if (!g_tls_bp_initialized) {
        ada_backpressure_state_init(&g_tls_state.backpressure[0], NULL);
        ada_backpressure_state_init(&g_tls_state.backpressure[1], NULL);
        g_tls_bp_initialized = true;
    }
    return &g_tls_state;
}

void ada_reset_tls_state(void) {
    memset(&g_tls_state, 0, sizeof(g_tls_state));
    tls_my_lanes = NULL;
    ada_backpressure_state_init(&g_tls_state.backpressure[0], NULL);
    ada_backpressure_state_init(&g_tls_state.backpressure[1], NULL);
    g_tls_bp_initialized = true;
}

void ada_set_global_registry(ThreadRegistry* registry) {
    atomic_store_explicit(&g_global_registry, registry, memory_order_release);
}

ThreadRegistry* ada_get_global_registry(void) {
    return atomic_load_explicit(&g_global_registry, memory_order_acquire);
}

static inline uint64_t ada_get_thread_id_portable(void) {
#ifdef __APPLE__
    return (uint64_t)pthread_mach_thread_np(pthread_self());
#else
    return (uint64_t)(uintptr_t)pthread_self();
#endif
}

ThreadLaneSet* ada_register_current_thread(void) {
    // Double-check: already registered?
    if (atomic_load_explicit(&g_tls_state.registered, memory_order_acquire)) {
        return g_tls_state.lanes;
    }

    ThreadRegistry* reg = ada_get_global_registry();
    if (!reg) {
        // No registry available; mark as registered to avoid retry storms
        atomic_store_explicit(&g_tls_state.registered, true, memory_order_release);
        g_tls_state.lanes = NULL;
        return NULL;
    }

    uintptr_t tid = (uintptr_t)pthread_self();
    ThreadLaneSet* lanes = thread_registry_register(reg, tid);
    if (!lanes) {
        // Out of slots or init failure; mark and return
        atomic_store_explicit(&g_tls_state.registered, true, memory_order_release);
        g_tls_state.lanes = NULL;
        return NULL;
    }

    // Initialize TLS state
    g_tls_state.lanes = lanes;
    g_tls_state.thread_id = ada_get_thread_id_portable();
    g_tls_state.registration_time = ada_now_monotonic_ns();
    // Slot id is optional; not exposed via API, leave as 0

    // Synchronize thread_registry TLS too
    tls_my_lanes = lanes;

    atomic_store_explicit(&g_tls_state.registered, true, memory_order_release);
    return lanes;
}

ThreadLaneSet* ada_get_thread_lane(void) {
    ThreadLaneSet* lanes = g_tls_state.lanes;
    if (lanes) return lanes;
    return ada_register_current_thread();
}

ada_reentrancy_guard_t ada_enter_trace(void) {
    ada_reentrancy_guard_t guard;
    guard.prev_depth = g_tls_state.call_depth;
    guard.was_reentrant = false;

    uint32_t prev = atomic_fetch_add_explicit(&g_tls_state.reentrancy, 1, memory_order_acquire);
    if (prev > 0) {
        guard.was_reentrant = true;
        g_tls_state.reentry_count++;
    }
    g_tls_state.call_depth = guard.prev_depth + 1;
    return guard;
}

void ada_exit_trace(ada_reentrancy_guard_t guard) {
    g_tls_state.call_depth = guard.prev_depth;
    atomic_fetch_sub_explicit(&g_tls_state.reentrancy, 1, memory_order_release);
}

void ada_tls_thread_cleanup(void) {
    ThreadLaneSet* lanes = g_tls_state.lanes;
    ThreadRegistry* reg = ada_get_global_registry();
    if (lanes && reg) {
        // Unregister by id to update active set and counts
        uintptr_t tid = (uintptr_t)pthread_self();
        (void)thread_registry_unregister_by_id(reg, tid);
    } else if (lanes) {
        // Best-effort fallback
        thread_registry_unregister(lanes);
    }
    ada_reset_tls_state();
}
