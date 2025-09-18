#ifndef DRAIN_THREAD_PRIVATE_H
#define DRAIN_THREAD_PRIVATE_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <tracer_backend/drain_thread/drain_thread.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_buffer.h>

typedef struct {
    atomic_uint_fast64_t cycles_total;
    atomic_uint_fast64_t cycles_idle;
    atomic_uint_fast64_t rings_total;
    atomic_uint_fast64_t rings_index;
    atomic_uint_fast64_t rings_detail;
    atomic_uint_fast64_t fairness_switches;
    atomic_uint_fast64_t sleeps;
    atomic_uint_fast64_t yields;
    atomic_uint_fast64_t final_drains;
    atomic_uint_fast64_t total_sleep_us;
    atomic_uint_fast64_t per_thread_rings[MAX_THREADS][2];
} DrainMetricsAtomic;

struct DrainThread {
    atomic_int          state;
    ThreadRegistry*     registry;
    DrainConfig         config;

    pthread_t           worker;
    bool                thread_started;
    pthread_mutex_t     lifecycle_lock;

    atomic_uint         rr_cursor;           // round-robin start index
    atomic_uint_fast64_t last_cycle_ns;      // last cycle timestamp snapshot

    DrainMetricsAtomic  metrics;
};

#endif // DRAIN_THREAD_PRIVATE_H
