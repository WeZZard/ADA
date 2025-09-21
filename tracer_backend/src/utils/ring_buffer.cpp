#include <tracer_backend/utils/ring_buffer.h>
#include "ring_buffer_private.h"
#include <cstdlib>

// ============================================================================
// C API Implementation (extern "C")
// ============================================================================

extern "C" {

// Create ring buffer (initializes header)
RingBuffer* ring_buffer_create(void* memory, size_t size, size_t event_size) {
    auto* rb = new ada::internal::RingBuffer();
    if (!rb) {
        return nullptr;
    }
    
    if (!rb->initialize(memory, size, event_size)) {
        delete rb;
        return nullptr;
    }
    
    return reinterpret_cast<RingBuffer*>(rb);
}

// Attach to existing ring buffer (does not initialize header)
RingBuffer* ring_buffer_attach(void* memory, size_t size, size_t event_size) {
    auto* rb = new ada::internal::RingBuffer();
    if (!rb) {
        return nullptr;
    }
    
    if (!rb->attach(memory, size, event_size)) {
        delete rb;
        return nullptr;
    }
    
    return reinterpret_cast<RingBuffer*>(rb);
}

// Producer operations
bool ring_buffer_write(RingBuffer* rb, const void* event) {
    if (!rb) return false;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->write(event);
}

size_t ring_buffer_available_write(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->available_write();
}

// Consumer operations
bool ring_buffer_read(RingBuffer* rb, void* event) {
    if (!rb) return false;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->read(event);
}

size_t ring_buffer_available_read(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->available_read();
}

size_t ring_buffer_read_batch(RingBuffer* rb, void* events, size_t max_count) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->read_batch(events, max_count);
}

// Status
bool ring_buffer_is_empty(RingBuffer* rb) {
    if (!rb) return true;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->is_empty();
}

bool ring_buffer_is_full(RingBuffer* rb) {
    if (!rb) return true;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->is_full();
}

void ring_buffer_reset(RingBuffer* rb) {
    if (!rb) return;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    impl->reset();
}

// Cleanup
void ring_buffer_destroy(RingBuffer* rb) {
    if (rb) {
        auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
        delete impl;
    }
}

// New accessor functions
size_t ring_buffer_get_event_size(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->get_event_size();
}

size_t ring_buffer_get_capacity(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->get_capacity();
}

RingBufferHeader* ring_buffer_get_header(RingBuffer* rb) {
    if (!rb) return nullptr;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->get_header();
}

} // extern "C"

// Additional C API (not in extern "C" block above to maintain grouping)
extern "C" {

uint64_t ring_buffer_get_overflow_count(RingBuffer* rb) {
    if (!rb) return 0;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    auto* hdr = impl->get_header();
    if (!hdr) return 0;
    return __atomic_load_n(&hdr->overflow_count, __ATOMIC_ACQUIRE);
}

bool ring_buffer_drop_oldest(RingBuffer* rb) {
    if (!rb) return false;
    auto* impl = reinterpret_cast<ada::internal::RingBuffer*>(rb);
    return impl->drop_oldest();
}

static inline uint32_t rb_mask_from_header(const RingBufferHeader* hdr) {
    return hdr->capacity ? (hdr->capacity - 1u) : 0u;
}

static inline uint8_t* rb_buffer_from_header(RingBufferHeader* hdr) {
    return reinterpret_cast<uint8_t*>(hdr) + sizeof(RingBufferHeader);
}

bool ring_buffer_write_raw(RingBufferHeader* header, size_t event_size, const void* event) {
    if (!header || !event || header->capacity == 0) return false;
    uint32_t mask = rb_mask_from_header(header);
    uint32_t write_pos = __atomic_load_n(&header->write_pos, __ATOMIC_ACQUIRE);
    uint32_t next_pos = (write_pos + 1) & mask;
    uint32_t read_pos = __atomic_load_n(&header->read_pos, __ATOMIC_ACQUIRE);
    if (next_pos == read_pos) {
        __atomic_fetch_add(&header->overflow_count, (uint64_t)1, __ATOMIC_RELAXED);
        return false;
    }
    uint8_t* buf = rb_buffer_from_header(header);
    void* dest = buf + (write_pos * event_size);
    std::memcpy(dest, event, event_size);
    __atomic_store_n(&header->write_pos, next_pos, __ATOMIC_RELEASE);
    return true;
}

bool ring_buffer_read_raw(RingBufferHeader* header, size_t event_size, void* event) {
    if (!header || !event || header->capacity == 0) return false;
    uint32_t mask = rb_mask_from_header(header);
    uint32_t read_pos = __atomic_load_n(&header->read_pos, __ATOMIC_ACQUIRE);
    uint32_t write_pos = __atomic_load_n(&header->write_pos, __ATOMIC_ACQUIRE);
    if (read_pos == write_pos) return false;
    uint8_t* buf = rb_buffer_from_header(header);
    void* src = buf + (read_pos * event_size);
    std::memcpy(event, src, event_size);
    uint32_t next_pos = (read_pos + 1) & mask;
    __atomic_store_n(&header->read_pos, next_pos, __ATOMIC_RELEASE);
    return true;
}

size_t ring_buffer_read_batch_raw(RingBufferHeader* header, size_t event_size, void* events, size_t max_count) {
    if (!header || !events || max_count == 0) return 0;
    size_t n = 0;
    uint8_t* dest = static_cast<uint8_t*>(events);
    while (n < max_count) {
        if (!ring_buffer_read_raw(header, event_size, dest + n * event_size)) break;
        n++;
    }
    return n;
}

size_t ring_buffer_available_read_raw(RingBufferHeader* header) {
    if (!header || header->capacity == 0) return 0;
    uint32_t mask = rb_mask_from_header(header);
    (void)mask; // mask unused explicitly as arithmetic wraps; keep for symmetry
    uint32_t write_pos = __atomic_load_n(&header->write_pos, __ATOMIC_ACQUIRE);
    uint32_t read_pos = __atomic_load_n(&header->read_pos, __ATOMIC_ACQUIRE);
    return (write_pos - read_pos) & rb_mask_from_header(header);
}

size_t ring_buffer_available_write_raw(RingBufferHeader* header) {
    if (!header || header->capacity == 0) return 0;
    uint32_t mask = rb_mask_from_header(header);
    (void)mask;
    uint32_t write_pos = __atomic_load_n(&header->write_pos, __ATOMIC_ACQUIRE);
    uint32_t read_pos = __atomic_load_n(&header->read_pos, __ATOMIC_ACQUIRE);
    return (read_pos - write_pos - 1u) & rb_mask_from_header(header);
}

}
