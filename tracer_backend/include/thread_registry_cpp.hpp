#ifndef THREAD_REGISTRY_CPP_HPP
#define THREAD_REGISTRY_CPP_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <array>
#include <type_traits>
#include "tracer_types.h"
#include "ring_buffer.h"

// ============================================================================
// CRTP Base for objects with tail-allocated data (LLVM-style)
// ============================================================================

namespace ada {

// Base class for objects with trailing storage (similar to LLVM's TrailingObjects)
template<typename Derived, typename... TrailingTypes>
class TrailingObjects {
protected:
    // Calculate offset for the Nth trailing type
    template<size_t N>
    static constexpr size_t getTrailingOffset() {
        return getTrailingOffsetImpl<N, 0, TrailingTypes...>();
    }
    
private:
    template<size_t Target, size_t Current, typename First, typename... Rest>
    static constexpr size_t getTrailingOffsetImpl() {
        if constexpr (Current == Target) {
            return 0;
        } else if constexpr (sizeof...(Rest) > 0) {
            return sizeof(First) + getTrailingOffsetImpl<Target, Current + 1, Rest...>();
        } else {
            return sizeof(First);
        }
    }
    
public:
    // Get pointer to trailing object at index
    template<typename T>
    T* getTrailingObject(size_t index = 0) {
        auto* base = reinterpret_cast<uint8_t*>(static_cast<Derived*>(this) + 1);
        return reinterpret_cast<T*>(base + sizeof(T) * index);
    }
    
    template<typename T>
    const T* getTrailingObject(size_t index = 0) const {
        auto* base = reinterpret_cast<const uint8_t*>(static_cast<const Derived*>(this) + 1);
        return reinterpret_cast<const T*>(base + sizeof(T) * index);
    }
    // Calculate total size needed for object with trailing data
    template<typename... Counts>
    static size_t totalSizeNeeded(Counts... counts) {
        return sizeof(Derived) + totalSizeForTrailing<TrailingTypes...>(counts...);
    }
    
private:
    template<typename First, typename... Rest, typename... Counts>
    static size_t totalSizeForTrailing(size_t firstCount, Counts... restCounts) {
        size_t size = sizeof(First) * firstCount;
        if constexpr (sizeof...(Rest) > 0) {
            size += totalSizeForTrailing<Rest...>(restCounts...);
        }
        return size;
    }
};

// ============================================================================
// Structured Lane Memory Layout (debugger-friendly)
// ============================================================================

// Memory layout for a single lane - EXPLICIT structure, not pointer arithmetic
struct LaneMemoryLayout {
    // Ring buffer pointers array
    RingBuffer* ring_ptrs[RINGS_PER_INDEX_LANE];
    
    // Ring buffer memory - explicitly sized
    alignas(CACHE_LINE_SIZE) 
    uint8_t ring_memory[RINGS_PER_INDEX_LANE][64 * 1024];
    
    // SPSC queues - explicit arrays
    alignas(CACHE_LINE_SIZE)
    uint32_t submit_queue[QUEUE_COUNT_INDEX_LANE];
    
    alignas(CACHE_LINE_SIZE)
    uint32_t free_queue[QUEUE_COUNT_INDEX_LANE];
    
    // Debug helper: print layout info
    void debug_print() const {
        printf("LaneMemoryLayout at %p:\n", this);
        printf("  ring_ptrs:    %p - %p\n", ring_ptrs, ring_ptrs + RINGS_PER_INDEX_LANE);
        printf("  ring_memory:  %p - %p\n", ring_memory, ring_memory + RINGS_PER_INDEX_LANE);
        printf("  submit_queue: %p - %p\n", submit_queue, submit_queue + QUEUE_COUNT_INDEX_LANE);
        printf("  free_queue:   %p - %p\n", free_queue, free_queue + QUEUE_COUNT_INDEX_LANE);
    }
};

// ============================================================================
// Enhanced Lane with proper C++ abstractions
// ============================================================================

class LaneCpp {
public:
    // Ring pool management
    std::atomic<uint32_t> active_idx{0};
    uint32_t ring_count{0};
    
    // SPSC queues with proper atomics
    std::atomic<uint32_t> submit_head{0};
    std::atomic<uint32_t> submit_tail{0};
    std::atomic<uint32_t> free_head{0};
    std::atomic<uint32_t> free_tail{0};
    
    // Statistics
    std::atomic<uint64_t> events_written{0};
    std::atomic<uint64_t> bytes_written{0};
    std::atomic<uint32_t> ring_swaps{0};
    
    // Memory pointers - but now they point to STRUCTURED data
    LaneMemoryLayout* memory_layout{nullptr};
    
    // Initialize lane with structured memory
    void initialize(LaneMemoryLayout* layout, uint32_t num_rings, size_t ring_size, size_t event_size) {
        memory_layout = layout;
        ring_count = num_rings;
        
        // Initialize ring buffers with clear structure
        for (uint32_t i = 0; i < num_rings; ++i) {
            layout->ring_ptrs[i] = ring_buffer_create(
                layout->ring_memory[i], 
                ring_size, 
                event_size
            );
        }
        
        // Initialize free queue with all rings except active (0)
        for (uint32_t i = 1; i < num_rings; ++i) {
            layout->free_queue[i - 1] = i;
        }
        free_tail.store(num_rings - 1, std::memory_order_release);
    }
    
    // Submit ring for draining (with bounds checking)
    bool submit_ring(uint32_t ring_idx) {
        if (ring_idx >= ring_count) return false;
        
        auto head = submit_head.load(std::memory_order_relaxed);
        auto tail = submit_tail.load(std::memory_order_acquire);
        auto next = (tail + 1) % QUEUE_COUNT_INDEX_LANE;
        
        if (next == head) return false; // Queue full
        
        memory_layout->submit_queue[tail] = ring_idx;
        submit_tail.store(next, std::memory_order_release);
        return true;
    }
    
    // Get active ring buffer
    RingBuffer* get_active_ring() {
        auto idx = active_idx.load(std::memory_order_relaxed);
        return memory_layout->ring_ptrs[idx];
    }
    
    // Debug print
    void debug_print() const {
        printf("Lane at %p:\n", this);
        printf("  active_idx: %u\n", active_idx.load());
        printf("  ring_count: %u\n", ring_count);
        printf("  events_written: %llu\n", (unsigned long long)events_written.load());
        if (memory_layout) {
            memory_layout->debug_print();
        }
    }
};

// ============================================================================
// ThreadLaneSet with explicit memory layout
// ============================================================================

class alignas(CACHE_LINE_SIZE) ThreadLaneSetCpp {
public:
    // Thread identification
    uint32_t thread_id{0};
    uint32_t slot_index{0};
    std::atomic<bool> active{false};
    
    // Lanes with structured memory
    LaneCpp index_lane;
    LaneCpp detail_lane;
    
    // Statistics
    std::atomic<uint64_t> events_generated{0};
    std::atomic<uint64_t> last_event_timestamp{0};
    
    // Initialize with structured memory blocks
    void initialize(uint32_t tid, uint32_t slot,
                   LaneMemoryLayout* index_memory,
                   LaneMemoryLayout* detail_memory) {
        thread_id = tid;
        slot_index = slot;
        
        index_lane.initialize(index_memory, RINGS_PER_INDEX_LANE, 64 * 1024, sizeof(IndexEvent));
        detail_lane.initialize(detail_memory, RINGS_PER_DETAIL_LANE, 256 * 1024, sizeof(DetailEvent));
        
        active.store(true, std::memory_order_release);
    }
    
    // Debug helper
    void debug_print() const {
        printf("ThreadLaneSet[%u] (tid=%u):\n", slot_index, thread_id);
        printf("  active: %s\n", active.load() ? "yes" : "no");
        printf("  events_generated: %llu\n", (unsigned long long)events_generated.load());
        printf("  Index Lane:\n");
        index_lane.debug_print();
        printf("  Detail Lane:\n");
        detail_lane.debug_print();
    }
};

// ============================================================================
// ThreadRegistry with CRTP-style tail allocation
// ============================================================================

// Forward declaration for CRTP
class ThreadRegistryCpp;

class ThreadRegistryCpp : public TrailingObjects<ThreadRegistryCpp, 
                                                  LaneMemoryLayout, 
                                                  LaneMemoryLayout> {
    using Base = TrailingObjects<ThreadRegistryCpp, LaneMemoryLayout, LaneMemoryLayout>;
public:
    // Registry state
    std::atomic<uint32_t> thread_count{0};
    std::atomic<bool> accepting_registrations{true};
    std::atomic<bool> shutdown_requested{false};
    
    // Thread lane sets - fixed array, not dynamic
    alignas(CACHE_LINE_SIZE)
    ThreadLaneSetCpp thread_lanes[MAX_THREADS];
    
    // Factory method for creating with proper memory layout
    static ThreadRegistryCpp* create(void* memory, size_t size) {
        // Calculate memory needed
        size_t needed = sizeof(ThreadRegistryCpp) + 
                       (MAX_THREADS * sizeof(LaneMemoryLayout) * 2); // index + detail
        
        if (size < needed) {
            fprintf(stderr, "Insufficient memory: need %zu, got %zu\n", needed, size);
            return nullptr;
        }
        
        // Placement new with clear memory
        std::memset(memory, 0, size);
        auto* registry = new (memory) ThreadRegistryCpp();
        
        // Get structured tail memory
        auto* index_layouts = registry->getTrailingObject<LaneMemoryLayout>(0);
        auto* detail_layouts = registry->getTrailingObject<LaneMemoryLayout>(MAX_THREADS);
        
        // Initialize each thread lane with structured memory
        for (uint32_t i = 0; i < MAX_THREADS; ++i) {
            // Don't call initialize yet - just set slot index
            registry->thread_lanes[i].slot_index = i;
            registry->thread_lanes[i].thread_id = 0;
            registry->thread_lanes[i].active.store(false);
            
            // Store memory pointers for later initialization
            registry->thread_lanes[i].index_lane.memory_layout = &index_layouts[i];
            registry->thread_lanes[i].detail_lane.memory_layout = &detail_layouts[i];
        }
        
        return registry;
    }
    
    // Register thread with better error handling
    ThreadLaneSetCpp* register_thread(uint32_t thread_id) {
        if (!accepting_registrations.load(std::memory_order_acquire)) {
            return nullptr;
        }
        
        // Try to find existing registration
        for (uint32_t i = 0; i < thread_count.load(); ++i) {
            if (thread_lanes[i].thread_id == thread_id && 
                thread_lanes[i].active.load()) {
                return &thread_lanes[i];  // Already registered
            }
        }
        
        // Allocate new slot
        uint32_t slot = thread_count.fetch_add(1, std::memory_order_acq_rel);
        if (slot >= MAX_THREADS) {
            thread_count.fetch_sub(1, std::memory_order_acq_rel);
            return nullptr;
        }
        
        // Initialize slot (only if not already initialized)
        if (thread_lanes[slot].thread_id == 0) {
            thread_lanes[slot].initialize(
                thread_id, 
                slot,
                thread_lanes[slot].index_lane.memory_layout,
                thread_lanes[slot].detail_lane.memory_layout
            );
        } else {
            thread_lanes[slot].thread_id = thread_id;
            thread_lanes[slot].active.store(true, std::memory_order_release);
        }
        
        return &thread_lanes[slot];
    }
    
    // Debug dump - actually useful!
    void debug_dump() const {
        printf("=== ThreadRegistryCpp Debug Dump ===\n");
        printf("Address: %p\n", this);
        printf("Thread count: %u\n", thread_count.load());
        printf("Accepting: %s\n", accepting_registrations.load() ? "yes" : "no");
        printf("Shutdown: %s\n", shutdown_requested.load() ? "yes" : "no");
        
        for (uint32_t i = 0; i < thread_count.load(); ++i) {
            if (thread_lanes[i].active.load()) {
                thread_lanes[i].debug_print();
            }
        }
        printf("=================================\n");
    }
    
    // Validation helper
    bool validate() const {
        // Check alignment
        if (reinterpret_cast<uintptr_t>(this) % CACHE_LINE_SIZE != 0) {
            fprintf(stderr, "Registry not cache-aligned\n");
            return false;
        }
        
        // Check thread lanes alignment
        for (uint32_t i = 0; i < MAX_THREADS; ++i) {
            auto addr = reinterpret_cast<uintptr_t>(&thread_lanes[i]);
            if (addr % CACHE_LINE_SIZE != 0) {
                fprintf(stderr, "ThreadLane[%u] not cache-aligned\n", i);
                return false;
            }
        }
        
        return true;
    }
};

// ============================================================================
// C compatibility layer
// ============================================================================

extern "C" {

// C-compatible creation function
ThreadRegistry* thread_registry_create_cpp(void* memory, size_t size) {
    auto* cpp_registry = ThreadRegistryCpp::create(memory, size);
    return reinterpret_cast<ThreadRegistry*>(cpp_registry);
}

// C-compatible registration
ThreadLaneSet* thread_registry_register_cpp(ThreadRegistry* registry, uint32_t thread_id) {
    auto* cpp_registry = reinterpret_cast<ThreadRegistryCpp*>(registry);
    auto* cpp_lanes = cpp_registry->register_thread(thread_id);
    return reinterpret_cast<ThreadLaneSet*>(cpp_lanes);
}

// C-compatible debug dump
void thread_registry_dump_cpp(ThreadRegistry* registry) {
    auto* cpp_registry = reinterpret_cast<ThreadRegistryCpp*>(registry);
    cpp_registry->debug_dump();
}

} // extern "C"

} // namespace ada

#endif // THREAD_REGISTRY_CPP_HPP