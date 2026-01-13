#ifndef TRACER_BACKEND_DRAIN_THREAD_H
#define TRACER_BACKEND_DRAIN_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include <tracer_backend/atf/atf_thread_writer.h>
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/metrics/global_metrics.h>

#ifdef __cplusplus
extern "C" {
#endif

// Drain thread lifecycle state machine
typedef enum {
    DRAIN_STATE_UNINITIALIZED = 0,
    DRAIN_STATE_INITIALIZED   = 1,
    DRAIN_STATE_RUNNING       = 2,
    DRAIN_STATE_STOPPING      = 3,
    DRAIN_STATE_STOPPED       = 4
} DrainState;

// Configuration knobs for the drain thread behaviour
typedef struct {
    uint32_t poll_interval_us;   // sleep duration when idle (0 = busy loop)
    uint32_t max_batch_size;     // max rings to consume from a lane per visit (0 = unlimited)
    uint32_t fairness_quantum;   // rings to process before rotating to the next lane (0 = unlimited)
    bool     yield_on_idle;      // call sched_yield() instead of sleeping when idle

    // Per-thread drain iteration configuration
    uint32_t max_threads_per_cycle;    // Max threads to drain per iteration (0 = unlimited)
    uint32_t max_events_per_thread;    // Max events per thread per iteration (0 = unlimited)
    uint32_t iteration_interval_ms;    // Time between iterations in milliseconds
    bool     enable_fair_scheduling;   // Enable fair thread selection algorithm
} DrainConfig;

// Snapshot of drain metrics - populated via drain_thread_get_metrics
typedef struct {
    uint64_t cycles_total;       // total poll cycles executed
    uint64_t cycles_idle;        // cycles with no work observed
    uint64_t rings_total;        // total rings consumed across all lanes
    uint64_t rings_index;        // rings drained from index lanes
    uint64_t rings_detail;       // rings drained from detail lanes
    uint64_t fairness_switches;  // times a lane hit the fairness quantum cap
    uint64_t sleeps;             // number of sleeps performed when idle
    uint64_t yields;             // number of yields performed when idle
    uint64_t final_drains;       // number of final drain passes during shutdown
    uint64_t total_sleep_us;     // accumulated sleep duration in microseconds
    uint64_t rings_per_thread[MAX_THREADS][2]; // per-thread lanes [thread][0=index,1=detail]

    // Per-thread drain iteration metrics
    uint64_t total_iterations;     // Total drain iterations completed
    uint64_t total_events_drained; // Total events processed by per-thread drain
    uint64_t total_bytes_drained;  // Total bytes processed by per-thread drain
    uint64_t threads_processed;    // Number of threads processed in last iteration
    uint64_t threads_skipped;      // Number of threads skipped in last iteration
    uint64_t iteration_duration_ns; // Duration of last iteration in nanoseconds
    uint64_t max_thread_wait_ns;   // Maximum thread wait time
    uint64_t avg_thread_wait_ns;   // Average thread wait time
    double   fairness_index;       // Jain's fairness index (0.0 to 1.0)
    uint64_t events_per_second;    // Current events per second throughput
    uint64_t bytes_per_second;     // Current bytes per second throughput
    uint32_t cpu_usage_percent;    // CPU usage percentage (0-100)
} DrainMetrics;

// Opaque drain thread handle
typedef struct DrainThread DrainThread;

// Populate config with sensible defaults
void drain_config_default(DrainConfig* config);

// Create a drain thread bound to a thread registry
DrainThread* drain_thread_create(ThreadRegistry* registry, const DrainConfig* config);

// Provide the control block for heartbeat/mode coordination (optional).
void drain_thread_set_control_block(DrainThread* drain, ControlBlock* control_block);

// Start the worker thread - transitions INITIALIZED -> RUNNING
int drain_thread_start(DrainThread* drain);

// Request shutdown and join worker - transitions RUNNING -> STOPPING -> STOPPED
int drain_thread_stop(DrainThread* drain);

// Destroy drain thread (must be in STOPPED or INITIALIZED state)
void drain_thread_destroy(DrainThread* drain);

// Query current lifecycle state
DrainState drain_thread_get_state(const DrainThread* drain);

// Snapshot metrics into caller-provided structure
void drain_thread_get_metrics(const DrainThread* drain, DrainMetrics* out_metrics);

// Update configuration (only allowed while not running)
int drain_thread_update_config(DrainThread* drain, const DrainConfig* config);

// Session management for ATF V2
int drain_thread_start_session(DrainThread* drain, const char* session_dir);
int drain_thread_stop_session(DrainThread* drain);

// Symbol table persistence for manifest (Phase 1)
// Set the JSON string containing modules and symbols to be included in manifest.
// The drain thread takes ownership of a copy of the string.
void drain_thread_set_symbol_table(DrainThread* drain, const char* json);

// ATF V2 writer accessors
AtfThreadWriter* drain_thread_get_atf_writer(DrainThread* drain, uint32_t thread_id);
void drain_thread_set_atf_writer(DrainThread* drain, uint32_t thread_id, AtfThreadWriter* writer);

// Access aggregated per-thread metrics (read-only view)
const ada_global_metrics_t* drain_thread_get_thread_metrics_view(const DrainThread* drain);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_DRAIN_THREAD_H
