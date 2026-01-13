#ifndef DRAIN_THREAD_PRIVATE_H
#define DRAIN_THREAD_PRIVATE_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <float.h>

#include <tracer_backend/drain_thread/drain_thread.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/metrics/global_metrics.h>

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

    // Per-thread drain iteration metrics (atomic)
    atomic_uint_fast64_t total_iterations;
    atomic_uint_fast64_t total_events_drained;
    atomic_uint_fast64_t total_bytes_drained;
    atomic_uint_fast64_t threads_processed;
    atomic_uint_fast64_t threads_skipped;
    atomic_uint_fast64_t iteration_duration_ns;
    atomic_uint_fast64_t max_thread_wait_ns;
    atomic_uint_fast64_t avg_thread_wait_ns;
    atomic_uint_fast64_t events_per_second;
    atomic_uint_fast64_t bytes_per_second;
    atomic_uint_fast32_t cpu_usage_percent;
    // Note: fairness_index is not atomic as it requires calculation
} DrainMetricsAtomic;

// Per-thread drain state tracking
typedef struct ThreadDrainState {
    uint32_t thread_id;
    atomic_uint_fast64_t last_drain_time;
    atomic_uint_fast64_t events_drained;
    atomic_uint_fast64_t bytes_drained;
    atomic_uint_fast32_t consecutive_empty;   // Track empty drains for optimization
    atomic_uint_fast32_t priority;            // Dynamic priority for scheduling

    // Lane state tracking
    atomic_uint_fast32_t index_pending;       // Pending events in index lane
    atomic_uint_fast32_t detail_pending;      // Pending events in detail lane
    atomic_uint_fast32_t detail_marked;       // Marked detail windows

    // Performance metrics (non-atomic, updated during drain)
    uint64_t avg_drain_latency_ns;
    uint64_t max_drain_latency_ns;

    // Fairness tracking
    uint64_t credits_used;                    // Credits consumed by this thread
    uint64_t total_drain_time_ns;            // Total time spent draining this thread
} ThreadDrainState;

// Scheduling algorithms
typedef enum DrainSchedulingAlgorithm {
    DRAIN_SCHED_ROUND_ROBIN = 0,
    DRAIN_SCHED_WEIGHTED_FAIR = 1,
    DRAIN_SCHED_PRIORITY_BASED = 2,
    DRAIN_SCHED_ADAPTIVE = 3
} DrainSchedulingAlgorithm;

// Forward declaration
typedef struct DrainScheduler DrainScheduler;
typedef struct DrainIterator DrainIterator;

// Thread drain result
typedef struct ThreadDrainResult {
    uint32_t events_drained;
    uint32_t bytes_drained;
    uint32_t index_events;
    uint32_t detail_events;
    uint64_t drain_latency_ns;
    bool skipped;
    bool error;
} ThreadDrainResult;

// Drain scheduling interface
typedef struct DrainScheduler {
    DrainSchedulingAlgorithm algorithm;

    // Function pointers for scheduling strategy
    uint32_t (*select_next_thread)(DrainScheduler* sched, ThreadDrainState* states, uint32_t thread_count);
    void (*update_priority)(DrainScheduler* sched, uint32_t thread_id, ThreadDrainResult* result);

    // Fairness tracking
    uint64_t* thread_credits;                 // Fair share credits per thread
    uint64_t total_credits_issued;
    uint32_t credits_capacity;                // Capacity of thread_credits array

    // Adaptive parameters
    double load_factor;                       // System load estimate (0.0 to 1.0)
    uint32_t high_priority_threshold;         // Events pending for high priority
    uint32_t credit_increment;                // Credits to add per selection

    // Round-robin state
    uint32_t rr_last_selected;               // Last selected thread for round-robin
} DrainScheduler;

// Constants for drain iteration
#define DRAIN_INVALID_THREAD_ID    UINT32_MAX
#define DRAIN_INITIAL_PRIORITY     100
#define DRAIN_PRIORITY_BOOST       50
#define DRAIN_PRIORITY_FINAL_DRAIN 1000
#define DRAIN_DEFAULT_CREDIT_INCREMENT 100
#define DRAIN_HIGH_THROUGHPUT_THRESHOLD 500000  // 500K events/sec

// Per-thread drain iteration state machine
typedef enum DrainIteratorState {
    DRAIN_ITER_IDLE = 0,
    DRAIN_ITER_DRAINING = 1,
    DRAIN_ITER_STOPPING = 2
} DrainIteratorState;

// Drain iteration controller
typedef struct DrainIterator {
    // Configuration
    uint32_t max_threads_per_cycle;    // Max threads to drain per iteration
    uint32_t max_events_per_thread;    // Max events per thread per iteration
    uint32_t iteration_interval_ms;    // Time between iterations
    bool enable_fair_scheduling;       // Enable fair scheduling algorithm

    // State
    atomic_uint_fast64_t current_iteration;
    atomic_uint_fast32_t active_thread_count;
    uint32_t last_drained_idx;         // For round-robin fairness
    atomic_int state;                  // DrainIteratorState

    // Thread tracking
    ThreadDrainState* thread_states;   // Per-thread drain state array
    uint32_t thread_states_capacity;   // Capacity of thread_states array

    // Scheduler
    DrainScheduler scheduler;

    // Timing
    uint64_t last_iteration_time_ns;   // Last iteration timestamp
    uint64_t iteration_start_time_ns;  // Current iteration start time

    // Fairness calculation workspace
    double fairness_index;             // Jain's fairness index (calculated periodically)
    uint64_t fairness_calc_interval_ns; // Interval for fairness calculation
    uint64_t last_fairness_calc_ns;    // Last fairness calculation time
} DrainIterator;

struct DrainThread {
    atomic_int          state;
    ThreadRegistry*     registry;
    DrainConfig         config;
    ControlBlock*       control_block;
    uint64_t            last_registry_tick_ns;
    uint32_t            warmup_ticks;

    // ATF V2 session management
    char                session_dir[4096];
    bool                session_active;
    AtfThreadWriter*    thread_writers[MAX_THREADS]; // Per-thread writers

    // Symbol table JSON for manifest (Phase 1 - symbol resolution)
    char*               symbol_table_json;  // Heap-allocated, freed on destroy

    pthread_t           worker;
    bool                thread_started;
    pthread_mutex_t     lifecycle_lock;

    atomic_uint         rr_cursor;           // round-robin start index
    atomic_uint_fast64_t last_cycle_ns;      // last cycle timestamp snapshot

    DrainMetricsAtomic  metrics;

    // Per-thread drain iteration
    DrainIterator*      iterator;            // Drain iterator (allocated separately)
    bool                iterator_enabled;    // Whether per-thread drain is enabled

    ada_global_metrics_t thread_metrics;
    ada_thread_metrics_snapshot_t thread_metrics_buffer[MAX_THREADS];
};

#endif // DRAIN_THREAD_PRIVATE_H
