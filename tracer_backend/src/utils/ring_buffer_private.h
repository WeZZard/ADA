#ifndef RING_BUFFER_PRIVATE_H
#define RING_BUFFER_PRIVATE_H

#include <cstdint>
#include <cstring>
#include <cstddef>

// We need to carefully handle the C/C++ compatibility
// RingBufferHeader is defined in tracer_types.h with _Atomic
// Do NOT include <atomic> as it conflicts with stdatomic.h
extern "C" {
#include <tracer_backend/utils/tracer_types.h>
}

#define RING_BUFFER_MAGIC 0xADA0
#define RING_BUFFER_VERSION 1

namespace ada {
namespace internal {

// ============================================================================
// RingBuffer Implementation Class
// ============================================================================

class RingBuffer {
public:
    // Constructor/destructor
    RingBuffer() = default;
    ~RingBuffer() = default;
    
    // Initialize as new ring buffer (creates header)
    bool initialize(void* memory, size_t size, size_t event_size) {
        if (!memory || size < sizeof(RingBufferHeader) + event_size) {
            return false;
        }
        // Align header placement to CACHE_LINE_SIZE within provided memory block
        auto base = reinterpret_cast<uint8_t*>(memory);
        auto end  = base + size;
        auto aligned = reinterpret_cast<uint8_t*>(
            (reinterpret_cast<uintptr_t>(base) + (CACHE_LINE_SIZE - 1)) & ~static_cast<uintptr_t>(CACHE_LINE_SIZE - 1)
        );
        if (aligned + sizeof(RingBufferHeader) + event_size > end) {
            return false;
        }

        header_ = reinterpret_cast<RingBufferHeader*>(aligned);
        buffer_ = aligned + sizeof(RingBufferHeader);
        event_size_ = event_size;
        buffer_size_ = static_cast<size_t>(end - buffer_);
        
        // Initialize header
        header_->magic = RING_BUFFER_MAGIC;
        header_->version = RING_BUFFER_VERSION;
        {
            // Compute events capacity and round down to nearest power-of-two (>=2)
            uint32_t events = static_cast<uint32_t>(buffer_size_ / event_size);
            if (events < 2) return false;
            // Round down to pow2
            uint32_t p2 = 1u << (31 - __builtin_clz(events));
            header_->capacity = p2;
            mask_ = p2 - 1u;
        }
        // Use C11 atomic operations on _Atomic members
        __atomic_store_n(&header_->write_pos, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&header_->read_pos, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&header_->overflow_count, (uint64_t)0, __ATOMIC_RELAXED);
        
        return true;
    }
    
    // Attach to existing ring buffer (doesn't create header)
    bool attach(void* memory, size_t size, size_t event_size) {
        if (!memory || size < sizeof(RingBufferHeader) + event_size) {
            return false;
        }
        // Locate header at the next CACHE_LINE_SIZE boundary as created by initialize()
        auto base = reinterpret_cast<uint8_t*>(memory);
        auto end  = base + size;
        auto aligned = reinterpret_cast<uint8_t*>(
            (reinterpret_cast<uintptr_t>(base) + (CACHE_LINE_SIZE - 1)) & ~static_cast<uintptr_t>(CACHE_LINE_SIZE - 1)
        );
        if (aligned + sizeof(RingBufferHeader) + event_size > end) {
            return false;
        }
        header_ = reinterpret_cast<RingBufferHeader*>(aligned);
        buffer_ = aligned + sizeof(RingBufferHeader);
        event_size_ = event_size;
        buffer_size_ = static_cast<size_t>(end - buffer_);
        
        // Verify magic number to ensure it's a valid ring buffer
        if (header_->magic != RING_BUFFER_MAGIC) {
            return false;
        }
        // Compute mask from capacity (assume power-of-two)
        if (header_->capacity == 0) return false;
        mask_ = header_->capacity - 1u;
        
        return true;
    }
    
    // Producer operations
    bool write(const void* event) {
        if (!event) return false;
        
        uint32_t write_pos = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);
        uint32_t next_pos = (write_pos + 1) & mask_;
        uint32_t read_pos = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
        
        // Check if full
        if (next_pos == read_pos) {
            // Buffer full: increment overflow counter and reject write
            __atomic_fetch_add(&header_->overflow_count, (uint64_t)1, __ATOMIC_RELAXED);
            return false;
        }
        
        // Copy event
        void* dest = buffer_ + (write_pos * event_size_);
        std::memcpy(dest, event, event_size_);
        
        // Update write position
        __atomic_store_n(&header_->write_pos, next_pos, __ATOMIC_RELEASE);
        
        return true;
    }
    
    size_t available_write() {
        uint32_t write_pos = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);
        uint32_t read_pos = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
        return (read_pos - write_pos - 1u) & mask_;
    }
    
    // Consumer operations
    bool read(void* event) {
        if (!event) return false;
        
        uint32_t read_pos = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
        uint32_t write_pos = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);
        
        // Check if empty
        if (read_pos == write_pos) {
            return false; // Buffer empty
        }
        
        // Copy event
        void* src = buffer_ + (read_pos * event_size_);
        std::memcpy(event, src, event_size_);
        
        // Update read position
        uint32_t next_pos = (read_pos + 1) & mask_;
        __atomic_store_n(&header_->read_pos, next_pos, __ATOMIC_RELEASE);
        
        return true;
    }
    
    size_t read_batch(void* events, size_t max_count) {
        if (!events || max_count == 0) return 0;
        
        size_t count = 0;
        uint8_t* dest = static_cast<uint8_t*>(events);
        
        while (count < max_count) {
            if (!read(dest + (count * event_size_))) {
                break;
            }
            count++;
        }
        
        return count;
    }
    
    size_t available_read() {
        uint32_t write_pos = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);
        uint32_t read_pos = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
        return (write_pos - read_pos) & mask_;
    }
    
    // Status operations
    bool is_empty() {
        uint32_t write_pos = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);
        uint32_t read_pos = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
        
        return write_pos == read_pos;
    }
    
    bool is_full() {
        uint32_t write_pos = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);
        uint32_t read_pos = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
        
        uint32_t next_pos = (write_pos + 1) & mask_;
        return next_pos == read_pos;
    }
    
    void reset() {
        __atomic_store_n(&header_->write_pos, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&header_->read_pos, 0, __ATOMIC_RELEASE);
    }

    // Drop the oldest event to free space
    bool drop_oldest() {
        uint32_t read_pos = __atomic_load_n(&header_->read_pos, __ATOMIC_ACQUIRE);
        uint32_t write_pos = __atomic_load_n(&header_->write_pos, __ATOMIC_ACQUIRE);

        // Check if empty
        if (read_pos == write_pos) {
            return false; // Nothing to drop
        }

        // Advance read position to drop the oldest event
        uint32_t next_pos = (read_pos + 1) & mask_;
        __atomic_store_n(&header_->read_pos, next_pos, __ATOMIC_RELEASE);

        return true;
    }

    // Accessors for internal state
    size_t get_event_size() const { return event_size_; }
    size_t get_capacity() const { return header_ ? header_->capacity : 0; }
    RingBufferHeader* get_header() { return header_; }
    const RingBufferHeader* get_header() const { return header_; }
    
private:
    RingBufferHeader* header_{nullptr};
    uint8_t* buffer_{nullptr};
    size_t event_size_{0};
    size_t buffer_size_{0};
    uint32_t mask_{0};
};

} // namespace internal
} // namespace ada

#endif // RING_BUFFER_PRIVATE_H
