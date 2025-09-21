#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <pthread.h>
#include <string>
#include <time.h>
#include <vector>

extern "C" {
#include <tracer_backend/metrics/metrics_reporter.h>
#include <tracer_backend/metrics/formatter.h>
#include <tracer_backend/utils/thread_registry.h>
}

namespace {

constexpr uint64_t kDefaultIntervalMs = 5000u;

static uint64_t monotonic_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

static timespec ns_to_timespec(uint64_t abs_ns) {
    timespec ts;
    ts.tv_sec = static_cast<time_t>(abs_ns / 1000000000ull);
    ts.tv_nsec = static_cast<long>(abs_ns % 1000000000ull);
    return ts;
}

#ifdef ADA_TESTING
using timedwait_hook_fn = void (*)(const struct timespec*, int);
static std::atomic<timedwait_hook_fn> g_test_timedwait_hook{nullptr};
static std::atomic<bool> g_test_force_thread_start_failure{false};
static std::atomic<bool> g_test_force_collect_failure{false};
static std::atomic<bool> g_test_force_pthread_create_failure{false};
#endif

struct ReporterState {
    ThreadRegistry* registry;
    ada_global_metrics_t global;
    std::vector<ada_thread_metrics_snapshot_t> snapshots;

    pthread_t thread;
    bool thread_started;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    std::atomic<bool> shutdown;

    bool running;
    bool paused;
    bool force_requested;

    uint64_t interval_ms;

    FILE* output_stream;
    std::string json_path;

    ada_metrics_report_sink_fn sink;
    void* sink_user_data;

    bool summary_emitted;
    bool cond_is_monotonic;

    ReporterState()
        : registry(nullptr),
          thread(0),
          thread_started(false),
          shutdown(false),
          running(false),
          paused(false),
          force_requested(false),
          interval_ms(kDefaultIntervalMs),
          output_stream(stderr),
          sink(nullptr),
          sink_user_data(nullptr),
          summary_emitted(false) {
        pthread_mutex_init(&lock, nullptr);
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
#if defined(CLOCK_MONOTONIC) && !defined(__APPLE__)
        cond_is_monotonic = true;
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#else
        cond_is_monotonic = false;
#endif
        pthread_cond_init(&cond, &attr);
        pthread_condattr_destroy(&attr);
    }

    ~ReporterState() {
        pthread_cond_destroy(&cond);
        pthread_mutex_destroy(&lock);
    }
};

static void reset_collection_timer(ada_global_metrics_t* global) {
#if defined(__cplusplus)
    global->control.last_collection_ns.store(0, std::memory_order_relaxed);
#else
    atomic_store_explicit(&global->control.last_collection_ns, 0, memory_order_relaxed);
#endif
}

static bool emit_report(ReporterState* state, ada_metrics_report_kind_t kind) {
    if (!state->registry) {
        return false;
    }

    if (kind == ADA_METRICS_REPORT_KIND_SUMMARY && state->summary_emitted) {
        return true;
    }

    if (kind == ADA_METRICS_REPORT_KIND_FORCED || kind == ADA_METRICS_REPORT_KIND_SUMMARY) {
        reset_collection_timer(&state->global);
    }

    const uint64_t now_ns = monotonic_ns();
#ifdef ADA_TESTING
    if (g_test_force_collect_failure.load(std::memory_order_acquire)) {
        return false;
    }
#endif
    if (!ada_global_metrics_collect(&state->global, state->registry, now_ns)) {
        return false;
    }

    ada_metrics_report_view_t view{};
    view.timestamp_ns = now_ns;
    view.kind = kind;
    view.snapshot_count = ada_global_metrics_snapshot_count(&state->global);
    view.snapshots = ada_global_metrics_snapshot_data(&state->global);
    view.totals = ada_global_metrics_get_totals(&state->global);
    view.rates = ada_global_metrics_get_rates(&state->global);

    if (state->output_stream) {
        flockfile(state->output_stream);
        ada_metrics_formatter_write_text(&view, state->output_stream);
        funlockfile(state->output_stream);
    }

    if (!state->json_path.empty()) {
        FILE* json = fopen(state->json_path.c_str(), "a");
        if (json) {
            ada_metrics_formatter_write_json(&view, json);
            fclose(json);
        }
    }

    if (state->sink) {
        state->sink(&view, state->sink_user_data);
    }

    if (kind == ADA_METRICS_REPORT_KIND_SUMMARY) {
        state->summary_emitted = true;
    }

    return true;
}

static void* reporter_thread_main(void* arg) {
    auto* state = static_cast<ReporterState*>(arg);

    while (!state->shutdown.load(std::memory_order_acquire)) {
        pthread_mutex_lock(&state->lock);
        while (!state->shutdown.load(std::memory_order_acquire) && !state->running) {
            pthread_cond_wait(&state->cond, &state->lock);
        }
        if (state->shutdown.load(std::memory_order_acquire)) {
            pthread_mutex_unlock(&state->lock);
            break;
        }

        while (!state->shutdown.load(std::memory_order_acquire) && state->paused && !state->force_requested) {
            pthread_cond_wait(&state->cond, &state->lock);
        }
        if (state->shutdown.load(std::memory_order_acquire)) {
            pthread_mutex_unlock(&state->lock);
            break;
        }

        if (state->force_requested) {
            state->force_requested = false;
            pthread_mutex_unlock(&state->lock);
            emit_report(state, ADA_METRICS_REPORT_KIND_FORCED);
            continue;
        }

        const uint64_t wait_ns = state->interval_ms * 1000000ull;
        uint64_t deadline_ns = 0;
        timespec deadline_rt{};
        if (state->cond_is_monotonic) {
            deadline_ns = monotonic_ns() + wait_ns;
        } else {
            clock_gettime(CLOCK_REALTIME, &deadline_rt);
            deadline_rt.tv_sec += static_cast<time_t>(wait_ns / 1000000000ull);
            deadline_rt.tv_nsec += static_cast<long>(wait_ns % 1000000000ull);
            if (deadline_rt.tv_nsec >= 1000000000L) {
                deadline_rt.tv_sec += 1;
                deadline_rt.tv_nsec -= 1000000000L;
            }
        }
        int rc = 0;
        while (!state->shutdown.load(std::memory_order_acquire) && state->running && !state->paused && !state->force_requested) {
            timespec ts = state->cond_is_monotonic ? ns_to_timespec(deadline_ns) : deadline_rt;
#ifdef ADA_TESTING
            if (timedwait_hook_fn hook = g_test_timedwait_hook.load(std::memory_order_acquire)) {
                hook(&ts, state->cond_is_monotonic ? 1 : 0);
            }
#endif
            rc = pthread_cond_timedwait(&state->cond, &state->lock, &ts);
            if (rc == ETIMEDOUT) {
                break;
            }
        }

        if (state->shutdown.load(std::memory_order_acquire)) {
            pthread_mutex_unlock(&state->lock);
            break;
        }

        if (!state->running) {
            pthread_mutex_unlock(&state->lock);
            continue;
        }

        if (state->paused && !state->force_requested) {
            pthread_mutex_unlock(&state->lock);
            continue;
        }

        bool forced = state->force_requested;
        state->force_requested = false;
        pthread_mutex_unlock(&state->lock);

        emit_report(state, forced ? ADA_METRICS_REPORT_KIND_FORCED : ADA_METRICS_REPORT_KIND_PERIODIC);
    }

    emit_report(state, ADA_METRICS_REPORT_KIND_SUMMARY);
    return nullptr;
}

static bool initialize_state(ReporterState* state,
                             const ada_metrics_reporter_config_t* config) {
    if (!state || !config || !config->registry) {
        return false;
    }

    state->registry = config->registry;
    state->interval_ms = config->report_interval_ms != 0 ? config->report_interval_ms : kDefaultIntervalMs;
    state->paused = config->start_paused;
    state->output_stream = config->output_stream ? config->output_stream : stderr;
    state->sink = config->sink;
    state->sink_user_data = config->sink_user_data;
    if (config->json_output_path) {
        state->json_path = config->json_output_path;
    }

    size_t capacity = config->snapshot_capacity != 0 ? config->snapshot_capacity : MAX_THREADS;
    state->snapshots.resize(capacity);

    // Initialize global metrics - guaranteed to succeed with valid capacity
    ada_global_metrics_init(&state->global, state->snapshots.data(), state->snapshots.size());
    ada_global_metrics_set_interval(&state->global, state->interval_ms * 1000000ull);
    return true;
}

} // namespace

extern "C" {

struct ada_metrics_reporter {
    ReporterState state;
};

#ifdef ADA_TESTING

void ada_metrics_reporter_test_force_thread_start_failure(bool should_fail) {
    g_test_force_thread_start_failure.store(should_fail, std::memory_order_release);
}

bool ada_metrics_reporter_test_emit_without_registry(struct ada_metrics_reporter* reporter,
                                                     ada_metrics_report_kind_t kind) {
    if (!reporter) {
        return false;
    }
    auto& state = reporter->state;
    pthread_mutex_lock(&state.lock);
    ThreadRegistry* original = state.registry;
    state.registry = nullptr;
    pthread_mutex_unlock(&state.lock);

    bool result = emit_report(&state, kind);

    pthread_mutex_lock(&state.lock);
    state.registry = original;
    pthread_mutex_unlock(&state.lock);
    return result;
}

struct timespec ada_metrics_reporter_test_ns_to_timespec(uint64_t abs_ns) {
    return ns_to_timespec(abs_ns);
}

void ada_metrics_reporter_test_set_cond_monotonic(struct ada_metrics_reporter* reporter,
                                                  bool is_monotonic) {
    if (!reporter) {
        return;
    }
    auto& state = reporter->state;
    pthread_mutex_lock(&state.lock);
    state.cond_is_monotonic = is_monotonic;
    pthread_mutex_unlock(&state.lock);
}

void ada_metrics_reporter_test_set_timedwait_hook(ada_metrics_reporter_timedwait_hook_fn hook) {
    g_test_timedwait_hook.store(hook, std::memory_order_release);
}

void ada_metrics_reporter_test_force_collect_failure(bool should_fail) {
    g_test_force_collect_failure.store(should_fail, std::memory_order_release);
}

void ada_metrics_reporter_test_set_thread_states(struct ada_metrics_reporter* reporter,
                                                 bool running, bool paused, bool force_requested) {
    if (!reporter) {
        return;
    }
    auto& state = reporter->state;
    pthread_mutex_lock(&state.lock);
    state.running = running;
    state.paused = paused;
    state.force_requested = force_requested;
    pthread_mutex_unlock(&state.lock);
}

bool ada_metrics_reporter_test_get_thread_states(struct ada_metrics_reporter* reporter,
                                                 bool* running, bool* paused, bool* force_requested) {
    if (!reporter) {
        return false;
    }
    auto& state = reporter->state;
    pthread_mutex_lock(&state.lock);
    if (running) *running = state.running;
    if (paused) *paused = state.paused;
    if (force_requested) *force_requested = state.force_requested;
    pthread_mutex_unlock(&state.lock);
    return true;
}

void ada_metrics_reporter_test_trigger_shutdown(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return;
    }
    auto& state = reporter->state;
    state.shutdown.store(true, std::memory_order_release);
    pthread_mutex_lock(&state.lock);
    pthread_cond_broadcast(&state.cond);
    pthread_mutex_unlock(&state.lock);
}

void ada_metrics_reporter_test_disable_global_collection(ada_global_metrics_t* global) {
    if (global) {
        ADA_ATOMIC_STORE(global->control.collection_enabled, false, ADA_MEMORY_ORDER_RELAXED);
    }
}

ada_global_metrics_t* ada_metrics_reporter_test_get_global_metrics(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return nullptr;
    }
    return &reporter->state.global;
}

void ada_metrics_reporter_test_force_pthread_create_failure(bool should_fail) {
    g_test_force_pthread_create_failure.store(should_fail, std::memory_order_release);
}

bool ada_metrics_reporter_test_is_thread_started(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return false;
    }
    return reporter->state.thread_started;
}

#endif // ADA_TESTING

struct ada_metrics_reporter* ada_metrics_reporter_create(
    const ada_metrics_reporter_config_t* config) {
    if (!config || !config->registry) {
        return nullptr;
    }

    auto reporter = std::make_unique<ada_metrics_reporter>();
    if (!initialize_state(&reporter->state, config)) {
        return nullptr;
    }
    return reporter.release();
}

void ada_metrics_reporter_destroy(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return;
    }
    ada_metrics_reporter_stop(reporter);
    delete reporter;
}

bool ada_metrics_reporter_start(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return false;
    }
    auto& state = reporter->state;

    if (!state.thread_started) {
        state.shutdown.store(false, std::memory_order_release);
        state.running = true;
        state.summary_emitted = false;
#ifdef ADA_TESTING
        if (g_test_force_thread_start_failure.exchange(false, std::memory_order_acq_rel)) {
            state.running = false;
            return false;
        }
#endif
#ifdef ADA_TESTING
        int rc = 0;
        if (g_test_force_pthread_create_failure.exchange(false, std::memory_order_acq_rel)) {
            rc = EAGAIN;  // Simulate resource shortage
        } else {
            rc = pthread_create(&state.thread, nullptr, reporter_thread_main, &state);
        }
#else
        int rc = pthread_create(&state.thread, nullptr, reporter_thread_main, &state);
#endif
        if (rc != 0) {
            state.running = false;
            return false;
        }
        state.thread_started = true;
        pthread_mutex_lock(&state.lock);
        pthread_cond_broadcast(&state.cond);
        pthread_mutex_unlock(&state.lock);
        return true;
    }

    pthread_mutex_lock(&state.lock);
    state.running = true;
    state.summary_emitted = false;
    pthread_cond_broadcast(&state.cond);
    pthread_mutex_unlock(&state.lock);
    return true;
}

void ada_metrics_reporter_stop(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return;
    }
    auto& state = reporter->state;

    if (state.thread_started) {
        state.shutdown.store(true, std::memory_order_release);
        pthread_mutex_lock(&state.lock);
        pthread_cond_broadcast(&state.cond);
        pthread_mutex_unlock(&state.lock);
        pthread_join(state.thread, nullptr);
        state.thread_started = false;
    } else {
        // Emit summary once even if thread never started
        emit_report(&state, ADA_METRICS_REPORT_KIND_SUMMARY);
    }
    state.running = false;
}

void ada_metrics_reporter_pause(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return;
    }
    auto& state = reporter->state;
    pthread_mutex_lock(&state.lock);
    state.paused = true;
    pthread_mutex_unlock(&state.lock);
}

void ada_metrics_reporter_resume(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return;
    }
    auto& state = reporter->state;
    pthread_mutex_lock(&state.lock);
    state.paused = false;
    state.force_requested = true; // ensure an immediate report after resume
    pthread_cond_broadcast(&state.cond);
    pthread_mutex_unlock(&state.lock);
}

bool ada_metrics_reporter_is_paused(const struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return false;
    }
    return reporter->state.paused;
}

bool ada_metrics_reporter_force_report(struct ada_metrics_reporter* reporter) {
    if (!reporter) {
        return false;
    }
    auto& state = reporter->state;
    pthread_mutex_lock(&state.lock);
    state.force_requested = true;
    pthread_cond_broadcast(&state.cond);
    pthread_mutex_unlock(&state.lock);
    return true;
}

void ada_metrics_reporter_set_interval(struct ada_metrics_reporter* reporter,
                                       uint64_t interval_ms) {
    if (!reporter || interval_ms == 0ull) {
        return;
    }
    auto& state = reporter->state;
    pthread_mutex_lock(&state.lock);
    state.interval_ms = interval_ms;
    pthread_cond_broadcast(&state.cond);
    pthread_mutex_unlock(&state.lock);
    ada_global_metrics_set_interval(&state.global, interval_ms * 1000000ull);
}

void ada_metrics_reporter_enable_json_output(struct ada_metrics_reporter* reporter,
                                             const char* path) {
    if (!reporter || !path) {
        return;
    }
    reporter->state.json_path = path;
}

} // extern "C"
