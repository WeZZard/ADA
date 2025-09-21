#ifndef ADA_THREAD_H
#define ADA_THREAD_H

// ADA Thread API: TLS state, fast-path lane access, and reentrancy guard
// C API for use from C/C++ and FFI.

#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/backpressure/backpressure.h>

// Thread-local storage for fast path access
typedef struct ada_tls_state {
    ThreadLaneSet* lanes;           // Cached per-thread lanes (NULL = unregistered)
    _Atomic(uint32_t) reentrancy;   // Reentrancy counter
    uint32_t call_depth;            // Current call stack depth
    uint64_t thread_id;             // Platform thread ID

    _Atomic(bool) registered;       // Registration complete flag
    uint8_t slot_id;                // Optional slot id (0 if unknown)
    uint8_t _pad1[6];               // Padding
    uint64_t registration_time;     // Timestamp of registration

    // Statistics
    uint64_t event_count;           // Events emitted by this thread (best-effort)
    uint64_t reentry_count;         // Reentrancy occurrences
    uint64_t overflow_count;        // Ring buffer overflows
    uint64_t _pad2;                 // Padding / reserved
    ada_backpressure_state_t backpressure[2]; // [0]=index, [1]=detail
} ada_tls_state_t;

// Reentrancy guard for nested calls
typedef struct ada_reentrancy_guard {
    uint32_t prev_depth;
    bool was_reentrant;
} ada_reentrancy_guard_t;

// Access TLS state
ada_tls_state_t* ada_get_tls_state(void);

// Reset TLS state (testing only)
void ada_reset_tls_state(void);

// Global registry setter/getter (must be called by runtime/agent)
void ada_set_global_registry(ThreadRegistry* registry);
ThreadRegistry* ada_get_global_registry(void);

// Fast path: get current thread's lanes, triggering registration if needed
ThreadLaneSet* ada_get_thread_lane(void);

// Slow path: register current thread (uses global registry)
ThreadLaneSet* ada_register_current_thread(void);

// Reentrancy guard API
ada_reentrancy_guard_t ada_enter_trace(void);
void ada_exit_trace(ada_reentrancy_guard_t guard);

// Cleanup at thread exit (optional, safe to call multiple times)
void ada_tls_thread_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // ADA_THREAD_H

