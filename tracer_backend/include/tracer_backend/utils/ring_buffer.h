#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "tracer_types.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lock-free SPSC ring buffer - opaque type
typedef struct RingBuffer RingBuffer;

// Create ring buffer (initializes header)
//
// Memory layout and alignment semantics:
// - The RingBufferHeader is placed at the next CACHE_LINE_SIZE boundary within
//   the provided [memory, memory + size) region to ensure that the producer
//   and consumer fields (write_pos/read_pos) are 64-byte aligned and reside on
//   distinct cache lines (to avoid false sharing).
// - The payload buffer begins immediately after the header.
// - The effective event capacity is computed from the remaining bytes after
//   (aligned header + payload), rounded down to the nearest power of two.
// - Callers should treat the provided region as opaque; do not assume the
//   header starts exactly at the base address.
RingBuffer* ring_buffer_create(void* memory, size_t size, size_t event_size);

// Attach to existing ring buffer (does not initialize header)
//
// This function mirrors the alignment rule used by ring_buffer_create(): the
// header is expected to be located at the next CACHE_LINE_SIZE boundary within
// the provided region, with payload immediately after. The [memory, size]
// region must be identical to or consistent with the one used at creation.
RingBuffer* ring_buffer_attach(void* memory, size_t size, size_t event_size);

// Producer operations
bool ring_buffer_write(RingBuffer* rb, const void* event);
size_t ring_buffer_available_write(RingBuffer* rb);

// Consumer operations
bool ring_buffer_read(RingBuffer* rb, void* event);
size_t ring_buffer_available_read(RingBuffer* rb);
size_t ring_buffer_read_batch(RingBuffer* rb, void* events, size_t max_count);

// Status
bool ring_buffer_is_empty(RingBuffer* rb);
bool ring_buffer_is_full(RingBuffer* rb);
void ring_buffer_reset(RingBuffer* rb);

// Cleanup
void ring_buffer_destroy(RingBuffer* rb);

// Accessor functions
size_t ring_buffer_get_event_size(RingBuffer* rb);
size_t ring_buffer_get_capacity(RingBuffer* rb);
RingBufferHeader* ring_buffer_get_header(RingBuffer* rb);

// Metrics accessors
uint64_t ring_buffer_get_overflow_count(RingBuffer* rb);

// Drop oldest event (consumer side operation to free space)
// Returns true if an event was dropped, false if buffer was empty
bool ring_buffer_drop_oldest(RingBuffer* rb);

// Raw, header-only helpers (no handle) for offsets-only SHM materialization
// These operate directly on RingBufferHeader and adjacent payload buffer.
// Event size must match the ring's event type size.
bool ring_buffer_write_raw(RingBufferHeader* header, size_t event_size, const void* event);
bool ring_buffer_read_raw(RingBufferHeader* header, size_t event_size, void* event);
size_t ring_buffer_read_batch_raw(RingBufferHeader* header, size_t event_size, void* events, size_t max_count);
size_t ring_buffer_available_read_raw(RingBufferHeader* header);
size_t ring_buffer_available_write_raw(RingBufferHeader* header);

#ifdef __cplusplus
}
#endif

#endif // RING_BUFFER_H
