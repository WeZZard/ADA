#ifndef THREAD_TYPES_PRIVATE_H
#define THREAD_TYPES_PRIVATE_H

// Private header with concrete struct definitions for thread registry
// This header should only be included by implementation files and tests

#include <tracer_backend/utils/tracer_types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

// Private constants for internal use
#define RINGS_PER_INDEX_LANE 4
#define RINGS_PER_DETAIL_LANE 2
#define QUEUE_COUNT_INDEX_LANE 1024
#define QUEUE_COUNT_DETAIL_LANE 256

// Forward declaration - RingBuffer is defined in ring_buffer.h
struct RingBuffer;

// Lane structure - manages rings for one lane (index or detail)
// Aligned to cache line to prevent false sharing
struct Lane {
    // Ring pool for this lane
    struct RingBuffer** rings;       // Array of ring buffer pointers
    uint32_t ring_count;             // Number of rings in pool
    _Atomic(uint32_t) active_idx;   // Currently active ring index
    
    // SPSC submit queue (thread -> drain)
    // The last element in the queue is reserved as the sentinel.
    _Atomic(uint32_t) submit_head;  // Consumer position (drain reads)
    _Atomic(uint32_t) submit_tail;  // Producer position (thread writes)
    uint32_t* submit_queue;          // Queue of ring indices ready to drain
    uint32_t submit_queue_size;      // Queue capacity
    
    // SPSC free queue (drain -> thread)  
    // The last element in the queue is reserved as the sentinel.
    _Atomic(uint32_t) free_head;    // Consumer position (thread reads)
    _Atomic(uint32_t) free_tail;    // Producer position (drain writes)
    uint32_t* free_queue;            // Queue of empty ring indices
    uint32_t free_queue_size;        // Queue capacity
    
    // Lane-specific state
    _Atomic(bool) marked_event_seen; // For detail lane trigger detection
    
    // Lane metrics
    _Atomic(uint64_t) events_written;
    _Atomic(uint64_t) events_dropped;
    _Atomic(uint32_t) ring_swaps;
    _Atomic(uint32_t) pool_exhaustions;
} __attribute__((aligned(CACHE_LINE_SIZE)));

// ThreadLaneSet - per-thread structure containing both lanes
// Aligned to cache line for optimal performance
struct ThreadLaneSet {
    // Thread identification
    uintptr_t thread_id;             // System thread ID
    uint32_t slot_index;             // Index in registry (0-63)
    _Atomic(bool) active;            // Thread still alive
    
    // Per-thread lanes
    struct Lane index_lane;          // Index events (4 rings)
    struct Lane detail_lane;         // Detail events (2 rings)
    
    // Thread-local metrics
    _Atomic(uint64_t) events_generated;
    _Atomic(uint64_t) last_event_timestamp;
} __attribute__((aligned(CACHE_LINE_SIZE)));

// ThreadRegistry - global registry of all threads
// This is the main structure allocated in shared memory
struct ThreadRegistry {
    // Global thread registry
    _Atomic(uint32_t) thread_count;       // Number of registered threads
    
    // Global control flags
    _Atomic(bool) accepting_registrations; // Still accepting new threads
    _Atomic(bool) shutdown_requested;      // Shutdown in progress
    
    // Array of thread lane sets (bulk of the structure)
    struct ThreadLaneSet thread_lanes[MAX_THREADS]; // Array of thread lane sets
} __attribute__((aligned(CACHE_LINE_SIZE)));

#endif // THREAD_TYPES_PRIVATE_H
