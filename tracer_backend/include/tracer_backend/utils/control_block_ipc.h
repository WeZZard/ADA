// Control Block IPC helpers - header-only inline functions
#ifndef CONTROL_BLOCK_IPC_H
#define CONTROL_BLOCK_IPC_H

#include <stdint.h>
#include <tracer_backend/utils/tracer_types.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void cb_set_hooks_ready(ControlBlock* cb, uint32_t ready) {
    __atomic_store_n(&cb->hooks_ready, ready, __ATOMIC_RELEASE);
}

static inline uint32_t cb_get_hooks_ready(ControlBlock* cb) {
    return __atomic_load_n(&cb->hooks_ready, __ATOMIC_ACQUIRE);
}

static inline void cb_set_registry_ready(ControlBlock* cb, uint32_t ready) {
    __atomic_store_n(&cb->registry_ready, ready, __ATOMIC_RELEASE);
}

static inline uint32_t cb_get_registry_ready(ControlBlock* cb) {
    return __atomic_load_n(&cb->registry_ready, __ATOMIC_ACQUIRE);
}

static inline void cb_set_registry_version(ControlBlock* cb, uint32_t ver) {
    __atomic_store_n(&cb->registry_version, ver, __ATOMIC_RELEASE);
}

static inline uint32_t cb_get_registry_version(ControlBlock* cb) {
    return __atomic_load_n(&cb->registry_version, __ATOMIC_ACQUIRE);
}

static inline void cb_set_registry_epoch(ControlBlock* cb, uint32_t epoch) {
    __atomic_store_n(&cb->registry_epoch, epoch, __ATOMIC_RELEASE);
}

static inline uint32_t cb_get_registry_epoch(ControlBlock* cb) {
    return __atomic_load_n(&cb->registry_epoch, __ATOMIC_ACQUIRE);
}

static inline void cb_set_registry_mode(ControlBlock* cb, uint32_t mode) {
    __atomic_store_n(&cb->registry_mode, mode, __ATOMIC_RELEASE);
}

static inline uint32_t cb_get_registry_mode(ControlBlock* cb) {
    return __atomic_load_n(&cb->registry_mode, __ATOMIC_ACQUIRE);
}

static inline void cb_set_heartbeat_ns(ControlBlock* cb, uint64_t now_ns) {
    __atomic_store_n(&cb->drain_heartbeat_ns, now_ns, __ATOMIC_RELEASE);
}

static inline void cb_update_heartbeat_ns(ControlBlock* cb, uint64_t now_ns) { // Monotonic update: only move heartbeat forward.
    uint64_t prev = __atomic_load_n(&cb->drain_heartbeat_ns, __ATOMIC_ACQUIRE);
    while (now_ns > prev) {
        if (__atomic_compare_exchange_n(&cb->drain_heartbeat_ns, &prev, now_ns,
                                        0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
            break;
        }
    }
    (void)prev; }
static inline uint64_t cb_get_heartbeat_ns(ControlBlock* cb) {
    return __atomic_load_n(&cb->drain_heartbeat_ns, __ATOMIC_ACQUIRE);
}

static inline void cb_inc_mode_transitions(ControlBlock* cb) {
    __atomic_fetch_add(&cb->mode_transitions, (uint64_t)1, __ATOMIC_RELAXED);
}

static inline uint64_t cb_get_mode_transitions(ControlBlock* cb) {
    return __atomic_load_n(&cb->mode_transitions, __ATOMIC_ACQUIRE);
}

static inline void cb_inc_fallback_events(ControlBlock* cb) {
    __atomic_fetch_add(&cb->fallback_events, (uint64_t)1, __ATOMIC_RELAXED);
}

static inline uint64_t cb_get_fallback_events(ControlBlock* cb) {
    return __atomic_load_n(&cb->fallback_events, __ATOMIC_ACQUIRE);
}

#ifdef __cplusplus
}
#endif

#endif // CONTROL_BLOCK_IPC_H
