#ifndef TRACER_TYPES_H
#define TRACER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

// Alignment helper
#if defined(__cplusplus)
  #define ADA_ALIGNAS(x) alignas(x)
#else
  #define ADA_ALIGNAS(x) __attribute__((aligned(x)))
#endif

// Public constants that may be needed by API users
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif
#ifndef MAX_THREADS
#define MAX_THREADS 64
#endif

// Event kinds
typedef enum {
    EVENT_KIND_CALL = 1,
    EVENT_KIND_RETURN = 2,
    EVENT_KIND_EXCEPTION = 3
} EventKind;

// Process state
typedef enum {
    PROCESS_STATE_UNINITIALIZED = 0,
    PROCESS_STATE_INITIALIZED,
    PROCESS_STATE_SPAWNING,
    PROCESS_STATE_SUSPENDED,
    PROCESS_STATE_ATTACHING,
    PROCESS_STATE_ATTACHED,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_DETACHING,
    PROCESS_STATE_FAILED
} ProcessState;

// Flight recorder state
typedef enum {
    FLIGHT_RECORDER_IDLE = 0,
    FLIGHT_RECORDER_ARMED,
    FLIGHT_RECORDER_PRE_ROLL,
    FLIGHT_RECORDER_RECORDING,
    FLIGHT_RECORDER_POST_ROLL
} FlightRecorderState;

// Compact index event (32 bytes)
typedef struct __attribute__((packed)) {
    uint64_t timestamp;      // Monotonic timestamp
    uint64_t function_id;    // (moduleId << 32) | symbolIndex
    uint32_t thread_id;      // Thread identifier
    uint32_t event_kind;     // EventKind
    uint32_t call_depth;     // Call stack depth
    uint32_t _padding;       // Alignment padding
} IndexEvent;

// Rich detail event (512 bytes)
typedef struct __attribute__((packed)) {
    uint64_t timestamp;
    uint64_t function_id;
    uint32_t thread_id;
    uint32_t event_kind;
    uint32_t call_depth;
    uint32_t _pad1;
    
    // ARM64 ABI registers (x0-x7 for arguments)
    uint64_t x_regs[8];
    uint64_t lr;            // Link register
    uint64_t fp;            // Frame pointer
    uint64_t sp;            // Stack pointer
    
    // Stack snapshot (128 bytes default)
    uint8_t stack_snapshot[128];
    uint32_t stack_size;
    
    // Padding to 512 bytes
    uint8_t _padding[512 - 248];
} DetailEvent;

// Ring buffer header - uses plain memory for cross-language atomics
// IMPORTANT: Access write_pos/read_pos with atomic operations, NOT directly!
// See docs/technical_insights/ada/ATOMIC_OPERATIONS_CROSS_LANGUAGE.md
#ifndef RING_BUFFER_HEADER_DEFINED
#define RING_BUFFER_HEADER_DEFINED
typedef struct {
    uint32_t magic;         // 0xADA0
    uint32_t version;       // Format version
    uint32_t capacity;      // Number of events (power-of-two)
    uint32_t _reserved0;    // Reserved/padding

    // Producer cache line
    ADA_ALIGNAS(CACHE_LINE_SIZE) uint32_t write_pos; // Write position (use atomic ops!)
    uint32_t _pad_producer[15];

    // Consumer cache line
    ADA_ALIGNAS(CACHE_LINE_SIZE) uint32_t read_pos;  // Read position (use atomic ops!)
    uint32_t _pad_consumer[15];

    // Metrics/cache
    uint64_t overflow_count;  // Incremented when writes occur on full buffer
    uint32_t _reserved[8];
} RingBufferHeader;
#endif

// Thread info for registry
typedef struct {
    uint32_t thread_id;
    uint32_t status;        // 0=inactive, 1=active
    uint64_t ring_offset;   // Offset to SPSC ring
    uint32_t ring_size;     // Size of ring buffer
    uint32_t _padding;
} ThreadInfo;

// Control block for shared state
typedef struct {
    ProcessState process_state;
    FlightRecorderState flight_state;
    uint32_t pre_roll_ms;
    uint32_t post_roll_ms;
    uint64_t trigger_time;
    uint32_t index_lane_enabled;
    uint32_t detail_lane_enabled;
    uint32_t capture_stack_snapshot;  // Enable 128-byte stack capture
    uint32_t _reserved[7];  // Reserved for future flags
} ControlBlock;

// Statistics
#ifndef TRACER_STATS_DEFINED
#define TRACER_STATS_DEFINED
typedef struct {
    uint64_t events_captured;
    uint64_t events_dropped;
    uint64_t bytes_written;
    uint32_t active_threads;
    uint32_t hooks_installed;
} TracerStats;
#endif

// ============================================================================
// Thread Registry - Opaque types for public API
// ============================================================================

// Forward declarations for opaque types
typedef struct Lane Lane;
typedef struct ThreadLaneSet ThreadLaneSet;
typedef struct ThreadRegistry ThreadRegistry;

#endif // TRACER_TYPES_H
