#ifndef THREAD_REGISTRY_H
#define THREAD_REGISTRY_H

#include "tracer_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Thread Registry API - Per-thread lane management with lock-free SPSC queues
// ============================================================================

// Initialize thread registry in shared memory
// memory: pointer to shared memory region for the registry
// size: total size available for registry and all lanes
// Returns: pointer to initialized ThreadRegistry, or NULL on failure
ThreadRegistry* thread_registry_init(void* memory, size_t size);

// Initialize thread registry with a runtime capacity (pressure cap)
// capacity: desired maximum number of concurrently registered threads
// Note: Memory size must be sufficient for the computed layout
ThreadRegistry* thread_registry_init_with_capacity(void* memory, size_t size, uint32_t capacity);

/// Deinitialize thread registry
/// registry: pointer to ThreadRegistry to deinitialize
/// memory: pointer to shared memory region for the registry
///
/// - Note: ONLY FOR TESTING
void thread_registry_deinit(ThreadRegistry* registry);

// Attach to existing thread registry in shared memory
// memory: pointer to existing ThreadRegistry in shared memory
// Returns: pointer to ThreadRegistry, or NULL if invalid
ThreadRegistry* thread_registry_attach(void* memory);

// Register current thread and get its lane set
// registry: pointer to ThreadRegistry
// thread_id: system thread ID for this thread
// Returns: pointer to ThreadLaneSet for this thread, or NULL if full
// Memory ordering: Uses memory_order_acq_rel for slot allocation
ThreadLaneSet* thread_registry_register(ThreadRegistry* registry, uintptr_t thread_id);


// Get lanes for current thread (fast path with TLS caching)
// Returns: cached ThreadLaneSet pointer, or NULL if not registered
// Note: This is inlined for < 10ns overhead on fast path
static inline ThreadLaneSet* thread_registry_get_lanes(void);

// Unregister current thread
// lanes: pointer to this thread's ThreadLaneSet
// Memory ordering: Uses memory_order_release to signal thread exit
void thread_registry_unregister(ThreadLaneSet* lanes);

// ============================================================================
// Lane operations - SPSC queue management
// ============================================================================

// Initialize a lane with ring buffers
// lane: lane to initialize
// ring_memory: memory for ring buffers
// ring_size: size of each ring buffer
// ring_count: number of rings in pool
// queue_memory: memory for SPSC queues
// queue_size: capacity of each queue
// Returns: true on success, false on failure
bool lane_init(Lane* lane, void* ring_memory, size_t ring_size, 
               uint32_t ring_count, void* queue_memory, size_t queue_size);

// Submit a ring for draining (thread -> drain)
// lane: lane containing the ring
// ring_idx: index of ring to submit
// Returns: true if submitted, false if queue full
// Memory ordering: Uses memory_order_release for publishing
bool lane_submit_ring(Lane* lane, uint32_t ring_idx);

// Take a ring for draining (drain thread side)
// lane: lane to check for submitted rings
// Returns: ring index to drain, or UINT32_MAX if queue empty
// Memory ordering: Uses memory_order_acquire for consuming
uint32_t lane_take_ring(Lane* lane);

// Return a free ring (drain -> thread)
// lane: lane to return ring to
// ring_idx: index of ring that is now free
// Returns: true if returned, false if queue full
// Memory ordering: Uses memory_order_release for publishing
bool lane_return_ring(Lane* lane, uint32_t ring_idx);

// Get a free ring (thread side)
// lane: lane to get free ring from
// Returns: ring index that can be used, or UINT32_MAX if none available
// Memory ordering: Uses memory_order_acquire for consuming
uint32_t lane_get_free_ring(Lane* lane);

// Detail lane selective persistence helpers.
void lane_mark_event(Lane* lane);
bool lane_has_marked_event(Lane* lane);
void lane_clear_marked_event(Lane* lane);


// ============================================================================
// Drain thread operations
// ============================================================================

// Iterator for drain thread to discover all active threads
// registry: ThreadRegistry to iterate
// Returns: number of active threads found
uint32_t thread_registry_get_active_count(ThreadRegistry* registry);

// Get thread lane set by index (for drain thread iteration)
// registry: ThreadRegistry
// index: slot index (0 to thread_count-1)
// Returns: ThreadLaneSet pointer if active, NULL if inactive
ThreadLaneSet* thread_registry_get_thread_at(ThreadRegistry* registry, uint32_t index);

// Get configured runtime capacity (pressure cap) for this registry
uint32_t thread_registry_get_capacity(ThreadRegistry* registry);

// Unregister a thread by system thread id; updates active set and counts
bool thread_registry_unregister_by_id(ThreadRegistry* registry, uintptr_t thread_id);


// Get active ring header directly (for raw, header-only operations)
RingBufferHeader* thread_registry_get_active_ring_header(ThreadRegistry* registry,
                                                        Lane* lane);

// Get ring header by index (for drop-oldest operations)
RingBufferHeader* thread_registry_get_ring_header_by_idx(ThreadRegistry* registry,
                                                        Lane* lane,
                                                        uint32_t ring_idx);

// ============================================================================
// Thread-local storage
// ============================================================================

// Lane accessor functions for ThreadLaneSet (needed for drain operations)
// lanes: ThreadLaneSet pointer
// Returns: pointer to the lane, or NULL if lanes is NULL
Lane* thread_lanes_get_index_lane(ThreadLaneSet* lanes);
Lane* thread_lanes_get_detail_lane(ThreadLaneSet* lanes);

// Set active status for a thread lane set
// lanes: ThreadLaneSet pointer
// active: true if active, false if inactive
void thread_lanes_set_active(ThreadLaneSet* lanes, bool active);

// Set events generated counter for a thread lane set  
// lanes: ThreadLaneSet pointer
// count: number of events generated
void thread_lanes_set_events_generated(ThreadLaneSet* lanes, uint64_t count);

// Get events generated counter for a thread lane set
// lanes: ThreadLaneSet pointer
// Returns: number of events generated
uint64_t thread_lanes_get_events_generated(ThreadLaneSet* lanes);


// Thread-local pointer for fast lane access
extern __thread ThreadLaneSet* tls_my_lanes;

// Fast inline accessor for TLS
static inline ThreadLaneSet* thread_registry_get_lanes(void) {
    return tls_my_lanes;
}

// ============================================================================
// Statistics and debugging
// ============================================================================

// Get thread registry statistics
void thread_registry_get_stats(ThreadRegistry* registry, TracerStats* stats);

// Print thread registry state (for debugging)
void thread_registry_dump(ThreadRegistry* registry);

// Calculate the memory size for a given capacity (registry structure + per-thread layouts + ring pools)
size_t thread_registry_calculate_memory_size_with_capacity(uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif // THREAD_REGISTRY_H
