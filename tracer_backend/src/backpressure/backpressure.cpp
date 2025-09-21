#include <tracer_backend/backpressure/backpressure.h>

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern "C" {

static inline uint64_t bp_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline ada_backpressure_config_t bp_default_config(void) {
    ada_backpressure_config_t cfg;
    cfg.pressure_threshold_percent = 25u;
    cfg.recovery_threshold_percent = 50u;
    cfg.recovery_stable_ns = 1000000000ull; // 1 second
    cfg.drop_log_interval = 64u;
    return cfg;
}

static const char* bp_mode_name(ada_backpressure_mode_t mode) {
    switch (mode) {
        case ADA_BACKPRESSURE_STATE_NORMAL:
            return "NORMAL";
        case ADA_BACKPRESSURE_STATE_PRESSURE:
            return "PRESSURE";
        case ADA_BACKPRESSURE_STATE_DROPPING:
            return "DROPPING";
        case ADA_BACKPRESSURE_STATE_RECOVERY:
            return "RECOVERY";
        default:
            return "UNKNOWN";
    }
}

static void bp_emit_log(const char* level, const char* fmt, ...) {
    if (!level || !fmt) return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ADA][BP][%s] ", level);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static _Atomic(uint64_t) g_drop_log_invocations = 0;
static _Atomic(uint64_t) g_state_log_invocations = 0;
static _Atomic(ada_backpressure_mode_t) g_last_prev_state = ADA_BACKPRESSURE_STATE_NORMAL;
static _Atomic(ada_backpressure_mode_t) g_last_next_state = ADA_BACKPRESSURE_STATE_NORMAL;
void ada_backpressure_testing_reset_log_counters(void) {
    atomic_store_explicit(&g_drop_log_invocations, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_state_log_invocations, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_last_prev_state, ADA_BACKPRESSURE_STATE_NORMAL, memory_order_relaxed);
    atomic_store_explicit(&g_last_next_state, ADA_BACKPRESSURE_STATE_NORMAL, memory_order_relaxed);
}

uint64_t ada_backpressure_testing_drop_log_invocations(void) {
    return atomic_load_explicit(&g_drop_log_invocations, memory_order_relaxed);
}

uint64_t ada_backpressure_testing_state_log_invocations(void) {
    return atomic_load_explicit(&g_state_log_invocations, memory_order_relaxed);
}

ada_backpressure_mode_t ada_backpressure_testing_last_state_previous(void) {
    return atomic_load_explicit(&g_last_prev_state, memory_order_relaxed);
}

ada_backpressure_mode_t ada_backpressure_testing_last_state_next(void) {
    return atomic_load_explicit(&g_last_next_state, memory_order_relaxed);
}

void bp_log_drop_event(const ada_backpressure_state_t* state, uint64_t total_drops) {
    if (!state) return;

    uint64_t bytes = atomic_load_explicit(&state->bytes_dropped, memory_order_relaxed);
    uint64_t sequences = atomic_load_explicit(&state->drop_sequences, memory_order_relaxed);
    uint32_t free_rings = atomic_load_explicit(&state->free_rings, memory_order_relaxed);
    uint32_t total_rings = atomic_load_explicit(&state->total_rings, memory_order_relaxed);
    ada_backpressure_mode_t mode = atomic_load_explicit(&state->mode, memory_order_relaxed);

    bp_emit_log("INFO",
                "Drops:%" PRIu64 " Bytes:%" PRIu64 " Sequences:%" PRIu64
                " Mode:%s Free:%u/%u LowWater:%u",
                total_drops,
                bytes,
                sequences,
                bp_mode_name(mode),
                free_rings,
                total_rings,
                ada_backpressure_state_get_low_watermark(state));

    atomic_fetch_add_explicit(&g_drop_log_invocations, 1u, memory_order_relaxed);
}

void bp_log_state_change(ada_backpressure_mode_t previous, ada_backpressure_mode_t next) {
    bp_emit_log("TRACE", "State transition %s -> %s", bp_mode_name(previous), bp_mode_name(next));
    atomic_fetch_add_explicit(&g_state_log_invocations, 1u, memory_order_relaxed);
    atomic_store_explicit(&g_last_prev_state, previous, memory_order_relaxed);
    atomic_store_explicit(&g_last_next_state, next, memory_order_relaxed);
}

void bp_export_metrics(const ada_backpressure_state_t* state, ada_backpressure_metrics_t* out) {
    if (!state || !out) return;

    out->mode = atomic_load_explicit(&state->mode, memory_order_relaxed);
    out->transitions = atomic_load_explicit(&state->transitions, memory_order_relaxed);
    out->events_dropped = atomic_load_explicit(&state->events_dropped, memory_order_relaxed);
    out->bytes_dropped = atomic_load_explicit(&state->bytes_dropped, memory_order_relaxed);
    out->drop_sequences = atomic_load_explicit(&state->drop_sequences, memory_order_relaxed);
    out->free_rings = atomic_load_explicit(&state->free_rings, memory_order_relaxed);
    out->total_rings = atomic_load_explicit(&state->total_rings, memory_order_relaxed);
    out->low_watermark = ada_backpressure_state_get_low_watermark(state);
    out->last_drop_ns = atomic_load_explicit(&state->last_drop_ns, memory_order_relaxed);
    out->last_recovery_ns = atomic_load_explicit(&state->last_recovery_ns, memory_order_relaxed);
    out->pressure_start_ns = atomic_load_explicit(&state->pressure_start_ns, memory_order_relaxed);
}

static bool bp_parse_env_u32(const char* value, uint32_t* out) {
    if (!value || !out || value[0] == '\0') return false;
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0') return false;
    if (parsed > UINT32_MAX) return false;
    *out = (uint32_t)parsed;
    return true;
}

ada_backpressure_config_t bp_config_from_env(void) {
    ada_backpressure_config_t cfg = bp_default_config();

    uint32_t value = 0;
    const char* pressure = getenv("BP_PRESSURE_THRESHOLD");
    if (bp_parse_env_u32(pressure, &value)) {
        cfg.pressure_threshold_percent = value;
    }

    const char* recovery = getenv("BP_RECOVERY_THRESHOLD");
    if (bp_parse_env_u32(recovery, &value)) {
        cfg.recovery_threshold_percent = value;
    }

    const char* drop_interval = getenv("BP_DROP_LOG_INTERVAL");
    if (bp_parse_env_u32(drop_interval, &value)) {
        cfg.drop_log_interval = value;
    }

    (void)bp_config_validate(&cfg);
    return cfg;
}

bool bp_config_validate(ada_backpressure_config_t* cfg) {
    if (!cfg) return false;
    bool valid = true;
    ada_backpressure_config_t defaults = bp_default_config();

    if (cfg->pressure_threshold_percent == 0 || cfg->pressure_threshold_percent >= 100) {
        cfg->pressure_threshold_percent = defaults.pressure_threshold_percent;
        valid = false;
    }

    if (cfg->recovery_threshold_percent == 0 || cfg->recovery_threshold_percent > 100) {
        cfg->recovery_threshold_percent = defaults.recovery_threshold_percent;
        valid = false;
    }

    if (cfg->pressure_threshold_percent >= cfg->recovery_threshold_percent) {
        if (cfg->pressure_threshold_percent < 95u) {
            cfg->recovery_threshold_percent = cfg->pressure_threshold_percent + 5u;
        } else {
            cfg->pressure_threshold_percent = defaults.pressure_threshold_percent;
            cfg->recovery_threshold_percent = defaults.recovery_threshold_percent;
        }
        valid = false;
    }

    if (cfg->drop_log_interval == 0u) {
        cfg->drop_log_interval = defaults.drop_log_interval;
        valid = false;
    }

    if (cfg->recovery_stable_ns == 0u) {
        cfg->recovery_stable_ns = defaults.recovery_stable_ns;
        valid = false;
    }

    return valid;
}

void ada_backpressure_state_init(ada_backpressure_state_t* state,
                                 const ada_backpressure_config_t* cfg) {
    if (!state) return;
    ada_backpressure_config_t effective = cfg ? *cfg : bp_default_config();
    (void)bp_config_validate(&effective);
    state->config = effective;
    atomic_store_explicit(&state->mode, ADA_BACKPRESSURE_STATE_NORMAL, memory_order_relaxed);
    atomic_store_explicit(&state->transitions, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->events_dropped, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->bytes_dropped, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->drop_sequences, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->free_rings, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->total_rings, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->low_watermark, UINT32_MAX, memory_order_relaxed);
    atomic_store_explicit(&state->last_drop_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->last_recovery_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->pressure_start_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->recovery_candidate_ns, 0u, memory_order_relaxed);
}

void ada_backpressure_state_reset(ada_backpressure_state_t* state) {
    if (!state) return;
    atomic_store_explicit(&state->mode, ADA_BACKPRESSURE_STATE_NORMAL, memory_order_relaxed);
    atomic_store_explicit(&state->transitions, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->events_dropped, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->bytes_dropped, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->drop_sequences, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->free_rings, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->total_rings, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->low_watermark, UINT32_MAX, memory_order_relaxed);
    atomic_store_explicit(&state->last_drop_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->last_recovery_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->pressure_start_ns, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->recovery_candidate_ns, 0u, memory_order_relaxed);
}

void ada_backpressure_state_set_total_rings(ada_backpressure_state_t* state,
                                            uint32_t total_rings) {
    if (!state || total_rings == 0) return;
    uint32_t prev = atomic_load_explicit(&state->total_rings, memory_order_relaxed);
    if (prev == total_rings) return;
    atomic_store_explicit(&state->total_rings, total_rings, memory_order_relaxed);
}

static inline void bp_update_low_watermark(ada_backpressure_state_t* state,
                                           uint32_t free_rings) {
    uint32_t low = atomic_load_explicit(&state->low_watermark, memory_order_relaxed);
    while (free_rings < low) {
        if (atomic_compare_exchange_weak_explicit(&state->low_watermark,
                                                  &low,
                                                  free_rings,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            break;
        }
    }
}

static inline uint32_t bp_total_effective(const ada_backpressure_state_t* state) {
    uint32_t total = atomic_load_explicit(&state->total_rings, memory_order_relaxed);
    return total ? total : 1u;
}

static void bp_transition(ada_backpressure_state_t* state,
                          ada_backpressure_mode_t expected,
                          ada_backpressure_mode_t desired,
                          uint64_t now_ns) {
    ada_backpressure_mode_t cur = atomic_load_explicit(&state->mode, memory_order_acquire);
    while (cur == expected) {
        if (atomic_compare_exchange_weak_explicit(&state->mode,
                                                  &cur,
                                                  desired,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
            atomic_fetch_add_explicit(&state->transitions, 1u, memory_order_relaxed);
            if (desired == ADA_BACKPRESSURE_STATE_PRESSURE) {
                atomic_store_explicit(&state->pressure_start_ns, now_ns, memory_order_relaxed);
            }
            if (desired == ADA_BACKPRESSURE_STATE_RECOVERY) {
                atomic_store_explicit(&state->recovery_candidate_ns, now_ns, memory_order_relaxed);
            }
            if (desired == ADA_BACKPRESSURE_STATE_NORMAL) {
                atomic_store_explicit(&state->pressure_start_ns, 0u, memory_order_relaxed);
                atomic_store_explicit(&state->recovery_candidate_ns, 0u, memory_order_relaxed);
            }
            if (expected != desired) {
                bp_log_state_change(expected, desired);
            }
            break;
        }
    }
}

static inline bool bp_threshold_crossed(uint32_t percent, uint32_t total, uint32_t free) {
    if (total == 0) return false;
    uint64_t scaled = (uint64_t)free * 100ull;
    return scaled < (uint64_t)percent * (uint64_t)total;
}

void ada_backpressure_state_sample(ada_backpressure_state_t* state,
                                   uint32_t free_rings,
                                   uint64_t now_ns) {
    if (!state) return;
    atomic_store_explicit(&state->free_rings, free_rings, memory_order_relaxed);
    bp_update_low_watermark(state, free_rings);

    uint32_t total = bp_total_effective(state);
    ada_backpressure_mode_t mode = atomic_load_explicit(&state->mode, memory_order_acquire);

    if (mode == ADA_BACKPRESSURE_STATE_NORMAL) {
        if (bp_threshold_crossed(state->config.pressure_threshold_percent, total, free_rings)) {
            if (now_ns == 0) now_ns = bp_now_ns();
            bp_transition(state, ADA_BACKPRESSURE_STATE_NORMAL, ADA_BACKPRESSURE_STATE_PRESSURE, now_ns);
        }
        return;
    }

    if (mode == ADA_BACKPRESSURE_STATE_PRESSURE) {
        if (free_rings == 0) {
            if (now_ns == 0) now_ns = bp_now_ns();
            bp_transition(state, ADA_BACKPRESSURE_STATE_PRESSURE, ADA_BACKPRESSURE_STATE_DROPPING, now_ns);
        } else if (!bp_threshold_crossed(state->config.pressure_threshold_percent, total, free_rings)) {
            if (now_ns == 0) now_ns = bp_now_ns();
            bp_transition(state, ADA_BACKPRESSURE_STATE_PRESSURE, ADA_BACKPRESSURE_STATE_NORMAL, now_ns);
        }
        return;
    }

    if (mode == ADA_BACKPRESSURE_STATE_DROPPING) {
        if (!bp_threshold_crossed(state->config.recovery_threshold_percent, total, free_rings)) {
            if (now_ns == 0) now_ns = bp_now_ns();
            bp_transition(state, ADA_BACKPRESSURE_STATE_DROPPING, ADA_BACKPRESSURE_STATE_RECOVERY, now_ns);
        }
        return;
    }

    if (mode == ADA_BACKPRESSURE_STATE_RECOVERY) {
        if (bp_threshold_crossed(state->config.pressure_threshold_percent, total, free_rings)) {
            if (now_ns == 0) now_ns = bp_now_ns();
            bp_transition(state, ADA_BACKPRESSURE_STATE_RECOVERY, ADA_BACKPRESSURE_STATE_PRESSURE, now_ns);
            return;
        }
        uint64_t candidate = atomic_load_explicit(&state->recovery_candidate_ns, memory_order_relaxed);
        if (candidate == 0) {
            if (now_ns == 0) now_ns = bp_now_ns();
            atomic_store_explicit(&state->recovery_candidate_ns, now_ns, memory_order_relaxed);
            return;
        }
        if (now_ns == 0) now_ns = bp_now_ns();
        if (now_ns - candidate >= state->config.recovery_stable_ns) {
            bp_transition(state, ADA_BACKPRESSURE_STATE_RECOVERY, ADA_BACKPRESSURE_STATE_NORMAL, now_ns);
            atomic_store_explicit(&state->last_recovery_ns, now_ns, memory_order_relaxed);
        }
    }
}

void ada_backpressure_state_on_exhaustion(ada_backpressure_state_t* state,
                                          uint64_t now_ns) {
    if (!state) return;
    if (now_ns == 0) now_ns = bp_now_ns();
    bp_transition(state, ADA_BACKPRESSURE_STATE_NORMAL, ADA_BACKPRESSURE_STATE_PRESSURE, now_ns);
    bp_transition(state, ADA_BACKPRESSURE_STATE_RECOVERY, ADA_BACKPRESSURE_STATE_DROPPING, now_ns);
    bp_transition(state, ADA_BACKPRESSURE_STATE_PRESSURE, ADA_BACKPRESSURE_STATE_DROPPING, now_ns);
    bp_transition(state, ADA_BACKPRESSURE_STATE_NORMAL, ADA_BACKPRESSURE_STATE_DROPPING, now_ns);
}

void ada_backpressure_state_on_drop(ada_backpressure_state_t* state,
                                    size_t dropped_bytes,
                                    uint64_t now_ns) {
    if (!state) return;
    if (now_ns == 0) now_ns = bp_now_ns();
    atomic_fetch_add_explicit(&state->events_dropped, 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&state->bytes_dropped, (uint64_t)dropped_bytes, memory_order_relaxed);
    atomic_store_explicit(&state->last_drop_ns, now_ns, memory_order_relaxed);
    atomic_fetch_add_explicit(&state->drop_sequences, 1u, memory_order_relaxed);

    uint32_t interval = state->config.drop_log_interval;
    if (interval != 0u) {
        uint64_t drops = atomic_load_explicit(&state->events_dropped, memory_order_relaxed);
        if (drops % interval == 0u) {
            bp_log_drop_event(state, drops);
        }
    }
}

void ada_backpressure_state_on_recovery(ada_backpressure_state_t* state,
                                        uint32_t free_rings,
                                        uint64_t now_ns) {
    if (!state) return;
    if (now_ns == 0) now_ns = bp_now_ns();
    atomic_store_explicit(&state->free_rings, free_rings, memory_order_relaxed);
    atomic_store_explicit(&state->last_recovery_ns, now_ns, memory_order_relaxed);
    if (atomic_load_explicit(&state->mode, memory_order_acquire) == ADA_BACKPRESSURE_STATE_DROPPING) {
        bp_transition(state, ADA_BACKPRESSURE_STATE_DROPPING, ADA_BACKPRESSURE_STATE_RECOVERY, now_ns);
    }
}

ada_backpressure_mode_t ada_backpressure_state_get_mode(const ada_backpressure_state_t* state) {
    if (!state) return ADA_BACKPRESSURE_STATE_NORMAL;
    return atomic_load_explicit(&state->mode, memory_order_acquire);
}

uint64_t ada_backpressure_state_get_drops(const ada_backpressure_state_t* state) {
    if (!state) return 0u;
    return atomic_load_explicit(&state->events_dropped, memory_order_relaxed);
}

uint32_t ada_backpressure_state_get_low_watermark(const ada_backpressure_state_t* state) {
    if (!state) return 0u;
    uint32_t low = atomic_load_explicit(&state->low_watermark, memory_order_relaxed);
    if (low == UINT32_MAX) return 0u;
    return low;
}

} // extern "C"
