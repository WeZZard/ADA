#ifndef ADA_SELECTIVE_PERSISTENCE_DETAIL_LANE_CONTROL_H
#define ADA_SELECTIVE_PERSISTENCE_DETAIL_LANE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include <tracer_backend/selective_persistence/marking_policy.h>
#include <tracer_backend/selective_persistence/metrics.h>
#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/selective_persistence/persistence_window.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MarkingPolicy;
struct ThreadRegistry;
struct ThreadLaneSet;
struct AtfThreadWriter;

// Opaque detail lane control state.
typedef struct DetailLaneControl DetailLaneControl;

typedef enum {
    DETAIL_LANE_CONTROL_ERROR_NONE = 0,
    DETAIL_LANE_CONTROL_ERROR_INVALID_ARGUMENT = 1,
    DETAIL_LANE_CONTROL_ERROR_STATE = 2,
    DETAIL_LANE_CONTROL_ERROR_IO_FAILURE = 3,
} DetailLaneControlError;

// Create a detail lane control instance.
DetailLaneControl* detail_lane_control_create(struct ThreadRegistry* registry,
                                              struct ThreadLaneSet* lanes,
                                              RingPool* pool,
                                              struct MarkingPolicy* policy);

// Destroy a detail lane control instance.
void detail_lane_control_destroy(DetailLaneControl* control);

// Start a new capture window at the provided timestamp.
void detail_lane_control_start_new_window(DetailLaneControl* control, uint64_t timestamp_ns);

// Take a snapshot of the active window state. out_window must be non-NULL.
void detail_lane_control_snapshot_window(const DetailLaneControl* control,
                                         SelectivePersistenceWindow* out_window);

// Notify the control logic that an event has been observed.
// Returns true if the event triggered the marked flag.
bool detail_lane_control_mark_event(DetailLaneControl* control,
                                    const struct AdaMarkingProbe* probe,
                                    uint64_t timestamp_ns);

// Determine whether the detail lane should persist the active ring.
bool detail_lane_control_should_dump(DetailLaneControl* control);

// Close the active window for persistence and populate metadata.
bool detail_lane_control_close_window_for_dump(DetailLaneControl* control,
                                               uint64_t timestamp_ns,
                                               SelectivePersistenceWindow* out_window);

// Swap the active ring when a selective dump is scheduled.
bool detail_lane_control_perform_selective_swap(DetailLaneControl* control,
                                                uint32_t* out_submitted_ring_idx);

// Persist window metadata to an ATF writer.
bool detail_lane_control_write_window_metadata(const DetailLaneControl* control,
                                               const SelectivePersistenceWindow* window,
                                               struct AtfThreadWriter* writer);

// Record that a selective dump has been performed and prepare a new window.
void detail_lane_control_mark_dump_complete(DetailLaneControl* control,
                                            uint64_t next_window_start_ns);

// Collect aggregated metrics for observability/testing.
void detail_lane_control_collect_metrics(const DetailLaneControl* control,
                                         SelectivePersistenceMetrics* out_metrics);

// Access last error code recorded by the control logic.
DetailLaneControlError detail_lane_control_last_error(const DetailLaneControl* control);

// Reset last error code to DETAIL_LANE_CONTROL_ERROR_NONE.
void detail_lane_control_clear_error(DetailLaneControl* control);

// Backwards-compatible wrappers.
static inline void detail_lane_control_start_window(DetailLaneControl* control,
                                                    uint64_t timestamp_ns) {
    detail_lane_control_start_new_window(control, timestamp_ns);
}

static inline void detail_lane_control_record_dump(DetailLaneControl* control,
                                                   uint64_t timestamp_ns) {
    detail_lane_control_mark_dump_complete(control, timestamp_ns);
}

// Expose counters for observability/testing.
uint64_t detail_lane_control_marked_events_detected(const DetailLaneControl* control);
uint64_t detail_lane_control_selective_dumps_performed(const DetailLaneControl* control);
uint64_t detail_lane_control_windows_discarded(const DetailLaneControl* control);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ADA_SELECTIVE_PERSISTENCE_DETAIL_LANE_CONTROL_H
