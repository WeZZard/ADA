#ifndef TRACER_BACKEND_METRICS_METRICS_REPORTER_H
#define TRACER_BACKEND_METRICS_METRICS_REPORTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef ADA_TESTING
#include <time.h>
#endif

#include <tracer_backend/metrics/global_metrics.h>
#include <tracer_backend/metrics/thread_metrics.h>
#include <tracer_backend/utils/tracer_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ThreadRegistry;
struct ada_metrics_reporter;

// Kind of report emitted by the reporter.
typedef enum {
    ADA_METRICS_REPORT_KIND_PERIODIC = 0,
    ADA_METRICS_REPORT_KIND_FORCED = 1,
    ADA_METRICS_REPORT_KIND_SUMMARY = 2,
} ada_metrics_report_kind_t;

// Immutable view passed to formatter callbacks.
typedef struct ada_metrics_report_view {
    uint64_t timestamp_ns;
    ada_metrics_report_kind_t kind;
    ada_global_metrics_totals_t totals;
    ada_global_metrics_rates_t rates;
    const ada_thread_metrics_snapshot_t* snapshots;
    size_t snapshot_count;
} ada_metrics_report_view_t;

// Callback invoked for every generated report. Optional.
typedef void (*ada_metrics_report_sink_fn)(const ada_metrics_report_view_t* view,
                                           void* user_data);

typedef struct ada_metrics_reporter_config {
    struct ThreadRegistry* registry; // Required for collection
    uint64_t report_interval_ms;     // Interval between reports, default 5000ms
    bool start_paused;               // If true reporter starts paused
    const char* json_output_path;    // Optional path, appended-to if set
    FILE* output_stream;             // Stream for human reports (defaults to stderr)
    size_t snapshot_capacity;        // Optional override (defaults to MAX_THREADS)
    ada_metrics_report_sink_fn sink; // Optional hook for tests/diagnostics
    void* sink_user_data;            // Passed to sink
} ada_metrics_reporter_config_t;

struct ada_metrics_reporter* ada_metrics_reporter_create(
    const ada_metrics_reporter_config_t* config);

void ada_metrics_reporter_destroy(struct ada_metrics_reporter* reporter);

bool ada_metrics_reporter_start(struct ada_metrics_reporter* reporter);

void ada_metrics_reporter_stop(struct ada_metrics_reporter* reporter);

void ada_metrics_reporter_pause(struct ada_metrics_reporter* reporter);

void ada_metrics_reporter_resume(struct ada_metrics_reporter* reporter);

bool ada_metrics_reporter_is_paused(const struct ada_metrics_reporter* reporter);

bool ada_metrics_reporter_force_report(struct ada_metrics_reporter* reporter);

void ada_metrics_reporter_set_interval(struct ada_metrics_reporter* reporter,
                                       uint64_t interval_ms);

void ada_metrics_reporter_enable_json_output(struct ada_metrics_reporter* reporter,
                                             const char* path);

#ifdef ADA_TESTING

typedef void (*ada_metrics_reporter_timedwait_hook_fn)(const struct timespec* ts,
                                                        int using_monotonic);

void ada_metrics_reporter_test_force_thread_start_failure(bool should_fail);

bool ada_metrics_reporter_test_emit_without_registry(struct ada_metrics_reporter* reporter,
                                                     ada_metrics_report_kind_t kind);

struct timespec ada_metrics_reporter_test_ns_to_timespec(uint64_t abs_ns);

void ada_metrics_reporter_test_set_cond_monotonic(struct ada_metrics_reporter* reporter,
                                                  bool is_monotonic);

void ada_metrics_reporter_test_set_timedwait_hook(ada_metrics_reporter_timedwait_hook_fn hook);

void ada_metrics_reporter_test_force_collect_failure(bool should_fail);

void ada_metrics_reporter_test_set_thread_states(struct ada_metrics_reporter* reporter,
                                                 bool running, bool paused, bool force_requested);

bool ada_metrics_reporter_test_get_thread_states(struct ada_metrics_reporter* reporter,
                                                 bool* running, bool* paused, bool* force_requested);

void ada_metrics_reporter_test_trigger_shutdown(struct ada_metrics_reporter* reporter);

void ada_metrics_reporter_test_disable_global_collection(ada_global_metrics_t* global);

ada_global_metrics_t* ada_metrics_reporter_test_get_global_metrics(struct ada_metrics_reporter* reporter);

void ada_metrics_reporter_test_force_pthread_create_failure(bool should_fail);

bool ada_metrics_reporter_test_is_thread_started(struct ada_metrics_reporter* reporter);

#endif // ADA_TESTING

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_METRICS_METRICS_REPORTER_H
