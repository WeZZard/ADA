#include <tracer_backend/selective_persistence/detail_lane_control.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <new>

// TODO: Update to use ATF V2 API
// #include <tracer_backend/atf/atf_v4_writer.h>
#include <tracer_backend/selective_persistence/metrics.h>
#include <tracer_backend/selective_persistence/persistence_window.h>
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/utils/thread_registry.h>

namespace {

struct DetailLaneControlImpl {
    ThreadRegistry* registry{nullptr};
    ThreadLaneSet* lanes{nullptr};
    RingPool* pool{nullptr};
    MarkingPolicy* policy{nullptr};
    Lane* lane{nullptr};

    std::atomic<uint64_t> marked_events_detected{0};
    std::atomic<uint64_t> selective_dumps_performed{0};
    std::atomic<uint64_t> windows_discarded{0};
    std::atomic<uint64_t> events_processed_total{0};
    std::atomic<uint64_t> total_window_duration_ns{0};
    std::atomic<uint64_t> total_window_events{0};
    std::atomic<uint64_t> windows_completed{0};
    std::atomic<uint64_t> metadata_write_failures{0};

    std::atomic<uint64_t> next_window_id{1};
    std::atomic<uint64_t> current_window_id{0};
    std::atomic<uint64_t> window_start_timestamp{0};
    std::atomic<uint64_t> window_end_timestamp{0};
    std::atomic<uint64_t> last_event_timestamp{0};
    std::atomic<uint64_t> first_mark_timestamp{0};
    std::atomic<uint64_t> window_total_events{0};
    std::atomic<uint64_t> window_marked_events{0};

    std::atomic<bool> marked_event_seen_since_last_dump{false};
    std::atomic<uint64_t> last_mark_timestamp{0};
    std::atomic<uint64_t> last_dump_timestamp{0};
    std::atomic<uint64_t> last_closed_window_duration_ns{0};
    std::atomic<uint64_t> last_closed_window_events{0};
    std::atomic<uint64_t> last_closed_window_id{0};
    std::atomic<bool> pending_window_metrics{false};
    std::atomic<int> last_error{DETAIL_LANE_CONTROL_ERROR_NONE};
};

bool is_ring_full(RingPool* pool) {
    if (!pool) return false;
    RingBufferHeader* header = ring_pool_get_active_header(pool);
    if (!header) return false;
    return ring_buffer_available_write_raw(header) == 0;
}

void set_error(DetailLaneControlImpl& impl, DetailLaneControlError error) {
    impl.last_error.store(static_cast<int>(error), std::memory_order_release);
}

void clear_error(DetailLaneControlImpl& impl) {
    impl.last_error.store(DETAIL_LANE_CONTROL_ERROR_NONE, std::memory_order_release);
}

bool record_io_failure(DetailLaneControlImpl& impl) {
    impl.metadata_write_failures.fetch_add(1, std::memory_order_relaxed);
    set_error(impl, DETAIL_LANE_CONTROL_ERROR_IO_FAILURE);
    return false;
}

void populate_window_snapshot(const DetailLaneControlImpl& impl,
                              SelectivePersistenceWindow* window,
                              uint64_t end_override) {
    if (!window) return;

    SelectivePersistenceWindow snapshot{};
    snapshot.window_id = impl.current_window_id.load(std::memory_order_acquire);
    snapshot.start_timestamp_ns = impl.window_start_timestamp.load(std::memory_order_acquire);
    snapshot.last_event_timestamp_ns = impl.last_event_timestamp.load(std::memory_order_acquire);
    uint64_t recorded_end = impl.window_end_timestamp.load(std::memory_order_acquire);
    if (end_override != 0) {
        recorded_end = end_override;
    }
    snapshot.end_timestamp_ns = recorded_end;
    snapshot.first_mark_timestamp_ns = impl.first_mark_timestamp.load(std::memory_order_acquire);
    snapshot.total_events = impl.window_total_events.load(std::memory_order_acquire);
    snapshot.marked_events = impl.window_marked_events.load(std::memory_order_acquire);
    snapshot.mark_seen = impl.marked_event_seen_since_last_dump.load(std::memory_order_acquire);

    *window = snapshot;
}

}  // namespace

struct DetailLaneControl {
    DetailLaneControlImpl impl;
};

DetailLaneControl* detail_lane_control_create(ThreadRegistry* registry,
                                              ThreadLaneSet* lanes,
                                              RingPool* pool,
                                              MarkingPolicy* policy) {
    if (!registry || !lanes || !pool || !policy) {
        return nullptr;
    }
    Lane* lane = thread_lanes_get_detail_lane(lanes);
    if (!lane) {
        // LCOV_EXCL_START - Defensive check: lanes is validated above, can't be null here
        return nullptr;
        // LCOV_EXCL_STOP
    }
    auto* control = new (std::nothrow) DetailLaneControl();
    if (!control) {
        return nullptr;
    }
    control->impl.registry = registry;
    control->impl.lanes = lanes;
    control->impl.pool = pool;
    control->impl.policy = policy;
    control->impl.lane = lane;
    clear_error(control->impl);
    control->impl.pending_window_metrics.store(false, std::memory_order_relaxed);
    control->impl.marked_event_seen_since_last_dump.store(false, std::memory_order_relaxed);
    lane_clear_marked_event(lane);
    return control;
}

void detail_lane_control_destroy(DetailLaneControl* control) {
    delete control;
}

void detail_lane_control_start_new_window(DetailLaneControl* control, uint64_t timestamp_ns) {
    if (!control) return;
    DetailLaneControlImpl& impl = control->impl;
    clear_error(impl);
    uint64_t new_id = impl.next_window_id.fetch_add(1, std::memory_order_acq_rel);
    impl.current_window_id.store(new_id, std::memory_order_release);
    impl.window_start_timestamp.store(timestamp_ns, std::memory_order_release);
    impl.window_end_timestamp.store(0, std::memory_order_release);
    impl.last_event_timestamp.store(timestamp_ns, std::memory_order_release);
    impl.first_mark_timestamp.store(0, std::memory_order_release);
    impl.window_total_events.store(0, std::memory_order_release);
    impl.window_marked_events.store(0, std::memory_order_release);
    impl.marked_event_seen_since_last_dump.store(false, std::memory_order_release);
    impl.last_mark_timestamp.store(0, std::memory_order_release);
    impl.pending_window_metrics.store(false, std::memory_order_release);
    if (impl.lane) {
        lane_clear_marked_event(impl.lane);
    }
}

void detail_lane_control_snapshot_window(const DetailLaneControl* control,
                                         SelectivePersistenceWindow* out_window) {
    if (!control || !out_window) return;
    populate_window_snapshot(control->impl, out_window, 0);
}

bool detail_lane_control_mark_event(DetailLaneControl* control,
                                    const AdaMarkingProbe* probe,
                                    uint64_t timestamp_ns) {
    if (!control || !probe) {
        if (control) {
            set_error(control->impl, DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);
        }
        return false;
    }

    DetailLaneControlImpl& impl = control->impl;
    uint64_t start_ns = impl.window_start_timestamp.load(std::memory_order_acquire);
    if (timestamp_ns < start_ns) {
        set_error(impl, DETAIL_LANE_CONTROL_ERROR_STATE);
        return false;
    }
    impl.window_total_events.fetch_add(1, std::memory_order_relaxed);
    impl.events_processed_total.fetch_add(1, std::memory_order_relaxed);
    impl.last_event_timestamp.store(timestamp_ns, std::memory_order_release);
    clear_error(impl);

    if (!marking_policy_match(impl.policy, probe)) {
        return false;
    }

    impl.marked_event_seen_since_last_dump.store(true, std::memory_order_release);
    impl.marked_events_detected.fetch_add(1, std::memory_order_relaxed);
    impl.window_marked_events.fetch_add(1, std::memory_order_relaxed);
    impl.last_mark_timestamp.store(timestamp_ns, std::memory_order_release);

    uint64_t expected_zero = 0;
    impl.first_mark_timestamp.compare_exchange_strong(
        expected_zero, timestamp_ns, std::memory_order_acq_rel, std::memory_order_acquire);

    if (impl.lane) {
        lane_mark_event(impl.lane);
    }
    return true;
}

bool detail_lane_control_should_dump(DetailLaneControl* control) {
    if (!control) {
        return false;
    }

    DetailLaneControlImpl& impl = control->impl;
    clear_error(impl);
    if (!is_ring_full(impl.pool)) {
        return false;
    }

    bool marked = impl.marked_event_seen_since_last_dump.load(std::memory_order_acquire);
    bool lane_marked = impl.lane && lane_has_marked_event(impl.lane);
    if (!marked || !lane_marked) {
        impl.windows_discarded.fetch_add(1, std::memory_order_relaxed);
        impl.marked_event_seen_since_last_dump.store(false, std::memory_order_release);
        if (impl.lane) {
            lane_clear_marked_event(impl.lane);
        }
        impl.window_marked_events.store(0, std::memory_order_release);
        impl.first_mark_timestamp.store(0, std::memory_order_release);
        impl.pending_window_metrics.store(false, std::memory_order_release);
        impl.last_closed_window_duration_ns.store(0, std::memory_order_release);
        impl.last_closed_window_events.store(0, std::memory_order_release);
        return false;
    }

    uint64_t last_event_ts = impl.last_event_timestamp.load(std::memory_order_acquire);
    uint64_t last_mark_ts = impl.last_mark_timestamp.load(std::memory_order_acquire);
    uint64_t end_ts = std::max(last_event_ts, last_mark_ts);
    impl.window_end_timestamp.store(end_ts, std::memory_order_release);
    return true;
}

bool detail_lane_control_close_window_for_dump(DetailLaneControl* control,
                                               uint64_t timestamp_ns,
                                               SelectivePersistenceWindow* out_window) {
    if (!control || !out_window) {
        if (control) {
            set_error(control->impl, DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);
        }
        return false;
    }

    DetailLaneControlImpl& impl = control->impl;
    if (!impl.marked_event_seen_since_last_dump.load(std::memory_order_acquire)) {
        set_error(impl, DETAIL_LANE_CONTROL_ERROR_STATE);
        return false;
    }

    uint64_t start_ts = impl.window_start_timestamp.load(std::memory_order_acquire);
    if (timestamp_ns < start_ts) {
        set_error(impl, DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);
        return false;
    }

    uint64_t last_event_ts = impl.last_event_timestamp.load(std::memory_order_acquire);
    uint64_t end_ts = std::max(timestamp_ns, last_event_ts);
    impl.window_end_timestamp.store(end_ts, std::memory_order_release);

    populate_window_snapshot(impl, out_window, end_ts);
    out_window->mark_seen = true;
    uint64_t resolved_window_id = out_window->window_id != 0
                                      ? out_window->window_id
                                      : impl.current_window_id.load(std::memory_order_acquire);
    impl.last_closed_window_id.store(resolved_window_id, std::memory_order_release);
    uint64_t duration = 0;
    if (out_window->end_timestamp_ns >= out_window->start_timestamp_ns) {
        duration = out_window->end_timestamp_ns - out_window->start_timestamp_ns;
    }
    impl.last_closed_window_duration_ns.store(duration, std::memory_order_release);
    impl.last_closed_window_events.store(out_window->total_events, std::memory_order_release);
    impl.pending_window_metrics.store(true, std::memory_order_release);
    clear_error(impl);
    return true;
}

bool detail_lane_control_perform_selective_swap(DetailLaneControl* control,
                                                uint32_t* out_submitted_ring_idx) {
    if (!control) {
        return false;
    }
    DetailLaneControlImpl& impl = control->impl;
    if (!out_submitted_ring_idx) {
        set_error(impl, DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT);
        return false;
    }
    if (!impl.pool) {
        // LCOV_EXCL_START - Pool is required at construction, can't be null here
        set_error(impl, DETAIL_LANE_CONTROL_ERROR_STATE);
        return false;
        // LCOV_EXCL_STOP
    }
    if (!impl.marked_event_seen_since_last_dump.load(std::memory_order_acquire)) {
        set_error(impl, DETAIL_LANE_CONTROL_ERROR_STATE);
        return false;
    }
    if (!ring_pool_swap_active(impl.pool, out_submitted_ring_idx)) {
        // LCOV_EXCL_START - Pool swap failure is rare, requires exhausted pool
        set_error(impl, DETAIL_LANE_CONTROL_ERROR_STATE);
        return false;
        // LCOV_EXCL_STOP
    }
    clear_error(impl);
    return true;
}

bool detail_lane_control_write_window_metadata(const DetailLaneControl* control,
                                               const SelectivePersistenceWindow* window,
                                               AtfThreadWriter* writer) {
    // TODO: Update to use ATF V2 session API
    (void)control;
    (void)window;
    (void)writer;
    return false;
}

void detail_lane_control_mark_dump_complete(DetailLaneControl* control,
                                            uint64_t next_window_start_ns) {
    if (!control) return;
    DetailLaneControlImpl& impl = control->impl;
    bool pending = impl.pending_window_metrics.exchange(false, std::memory_order_acq_rel);
    if (pending) {
        uint64_t duration = impl.last_closed_window_duration_ns.load(std::memory_order_acquire);
        uint64_t events = impl.last_closed_window_events.load(std::memory_order_acquire);
        impl.total_window_duration_ns.fetch_add(duration, std::memory_order_relaxed);
        impl.total_window_events.fetch_add(events, std::memory_order_relaxed);
        impl.windows_completed.fetch_add(1, std::memory_order_relaxed);
    }
    impl.selective_dumps_performed.fetch_add(1, std::memory_order_relaxed);
    impl.last_dump_timestamp.store(next_window_start_ns, std::memory_order_release);
    detail_lane_control_start_new_window(control, next_window_start_ns);
}

uint64_t detail_lane_control_marked_events_detected(const DetailLaneControl* control) {
    if (!control) return 0;
    return control->impl.marked_events_detected.load(std::memory_order_relaxed);
}

uint64_t detail_lane_control_selective_dumps_performed(const DetailLaneControl* control) {
    if (!control) return 0;
    return control->impl.selective_dumps_performed.load(std::memory_order_relaxed);
}

uint64_t detail_lane_control_windows_discarded(const DetailLaneControl* control) {
    if (!control) return 0;
    return control->impl.windows_discarded.load(std::memory_order_relaxed);
}

void detail_lane_control_collect_metrics(const DetailLaneControl* control,
                                         SelectivePersistenceMetrics* out_metrics) {
    if (!out_metrics) {
        return;
    }
    selective_persistence_metrics_reset(out_metrics);
    if (!control) {
        return;
    }

    const DetailLaneControlImpl& impl = control->impl;
    uint64_t events_processed = impl.events_processed_total.load(std::memory_order_acquire);
    uint64_t marked_events = impl.marked_events_detected.load(std::memory_order_acquire);
    uint64_t dumps = impl.selective_dumps_performed.load(std::memory_order_acquire);
    uint64_t discards = impl.windows_discarded.load(std::memory_order_acquire);
    uint64_t duration_total = impl.total_window_duration_ns.load(std::memory_order_acquire);
    uint64_t window_events_total = impl.total_window_events.load(std::memory_order_acquire);
    uint64_t windows_completed = impl.windows_completed.load(std::memory_order_acquire);
    bool pending = impl.pending_window_metrics.load(std::memory_order_acquire);
    if (pending) {
        duration_total += impl.last_closed_window_duration_ns.load(std::memory_order_acquire);
        window_events_total += impl.last_closed_window_events.load(std::memory_order_acquire);
        windows_completed += 1;
    }

    out_metrics->events_processed = events_processed;
    out_metrics->marked_events_detected = marked_events;
    out_metrics->selective_dumps_performed = dumps;
    out_metrics->windows_discarded = discards;
    out_metrics->metadata_write_failures = impl.metadata_write_failures.load(std::memory_order_acquire);

    if (windows_completed > 0) {
        out_metrics->avg_window_duration_ns = duration_total / windows_completed;
        out_metrics->avg_events_per_window = window_events_total / windows_completed;
    }
}

DetailLaneControlError detail_lane_control_last_error(const DetailLaneControl* control) {
    if (!control) {
        return DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT;
    }
    int error_value = control->impl.last_error.load(std::memory_order_acquire);
    return static_cast<DetailLaneControlError>(error_value);
}

void detail_lane_control_clear_error(DetailLaneControl* control) {
    if (!control) {
        return;
    }
    clear_error(control->impl);
}
