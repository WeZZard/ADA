#ifndef ADA_BACKPRESSURE_BACKPRESSURE_H
#define ADA_BACKPRESSURE_BACKPRESSURE_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADA_BACKPRESSURE_STATE_NORMAL = 0,
    ADA_BACKPRESSURE_STATE_PRESSURE = 1,
    ADA_BACKPRESSURE_STATE_DROPPING = 2,
    ADA_BACKPRESSURE_STATE_RECOVERY = 3,
} ada_backpressure_mode_t;

typedef struct {
    uint32_t pressure_threshold_percent;
    uint32_t recovery_threshold_percent;
    uint64_t recovery_stable_ns;
    uint32_t drop_log_interval;
} ada_backpressure_config_t;

typedef struct {
    ada_backpressure_mode_t mode;
    uint64_t transitions;
    uint64_t events_dropped;
    uint64_t bytes_dropped;
    uint64_t drop_sequences;
    uint32_t free_rings;
    uint32_t total_rings;
    uint32_t low_watermark;
    uint64_t last_drop_ns;
    uint64_t last_recovery_ns;
    uint64_t pressure_start_ns;
} ada_backpressure_metrics_t;

// Forward declaration for opaque state wrapper.
typedef struct ada_backpressure_state {
    // State machine
    _Atomic(ada_backpressure_mode_t) mode;
    _Atomic(uint64_t) transitions;

    // Accounting
    _Atomic(uint64_t) events_dropped;
    _Atomic(uint64_t) bytes_dropped;
    _Atomic(uint64_t) drop_sequences;

    // Pool tracking
    _Atomic(uint32_t) free_rings;
    _Atomic(uint32_t) total_rings;
    _Atomic(uint32_t) low_watermark;

    // Timing
    _Atomic(uint64_t) last_drop_ns;
    _Atomic(uint64_t) last_recovery_ns;
    _Atomic(uint64_t) pressure_start_ns;
    _Atomic(uint64_t) recovery_candidate_ns;

    // Configuration (immutable after init)
    ada_backpressure_config_t config;
} ada_backpressure_state_t;

// Initialize state with configuration. Passing NULL uses defaults (25/50/1s).
void ada_backpressure_state_init(ada_backpressure_state_t* state,
                                 const ada_backpressure_config_t* cfg);

// Reset state to defaults while preserving config.
void ada_backpressure_state_reset(ada_backpressure_state_t* state);

// Bind total rings (per lane). Only updates when value changes.
void ada_backpressure_state_set_total_rings(ada_backpressure_state_t* state,
                                            uint32_t total_rings);

// Update free ring sample and advance state machine. now_ns is optional; pass 0 to skip timing.
void ada_backpressure_state_sample(ada_backpressure_state_t* state,
                                   uint32_t free_rings,
                                   uint64_t now_ns);

// Handle pool exhaustion event. Attempts to transition to DROPPING and records drop sequence.
void ada_backpressure_state_on_exhaustion(ada_backpressure_state_t* state,
                                          uint64_t now_ns);

// Notify that a drop was performed (drop-oldest policy succeeded).
void ada_backpressure_state_on_drop(ada_backpressure_state_t* state,
                                    size_t dropped_bytes,
                                    uint64_t now_ns);

// Notify that pool recovered capacity (currently free rings >= recovery threshold).
void ada_backpressure_state_on_recovery(ada_backpressure_state_t* state,
                                        uint32_t free_rings,
                                        uint64_t now_ns);

// Accessors.
ada_backpressure_mode_t ada_backpressure_state_get_mode(const ada_backpressure_state_t* state);
uint64_t ada_backpressure_state_get_drops(const ada_backpressure_state_t* state);
uint32_t ada_backpressure_state_get_low_watermark(const ada_backpressure_state_t* state);

// Monitoring / metrics helpers.
void bp_log_drop_event(const ada_backpressure_state_t* state, uint64_t total_drops);
void bp_log_state_change(ada_backpressure_mode_t previous, ada_backpressure_mode_t next);
void bp_export_metrics(const ada_backpressure_state_t* state, ada_backpressure_metrics_t* out);

// Configuration helpers.
ada_backpressure_config_t bp_config_from_env(void);
bool bp_config_validate(ada_backpressure_config_t* cfg);

// Testing / diagnostics helpers.
void ada_backpressure_testing_reset_log_counters(void);
uint64_t ada_backpressure_testing_drop_log_invocations(void);
uint64_t ada_backpressure_testing_state_log_invocations(void);
ada_backpressure_mode_t ada_backpressure_testing_last_state_previous(void);
ada_backpressure_mode_t ada_backpressure_testing_last_state_next(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ADA_BACKPRESSURE_BACKPRESSURE_H
