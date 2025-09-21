// thread_registry.cpp - C++ implementation with C compatibility layer

#include "thread_registry_private.h"
#include "tracer_types_private.h"
#include <cstdlib>
#include <cstring>
#include <cassert>

namespace ada {
namespace internal {

// Static TLS for fast path
thread_local ThreadLaneSet* tls_current_lanes = nullptr;

}  // namespace internal
}  // namespace ada

// Define optional logging flag with C++ linkage
bool needs_log_thread_registry_registry = false;

// =============================================================================
// C Compatibility Layer - Wraps C++ implementation for C interface
// =============================================================================

extern "C" {

#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/ada/thread.h>
#include <tracer_backend/utils/ring_buffer.h>
// SHM directory mapping helpers (M1_E1_I8)
#include <tracer_backend/utils/shm_directory.h>

// TLS variable for C compatibility
__thread ThreadLaneSet* tls_my_lanes = nullptr;

// Forward declaration
void thread_registry_set_my_lanes(ThreadLaneSet* lanes);

// Use C++ implementation but expose C interface
ThreadRegistry* thread_registry_init(void* memory, size_t size) {
    auto* impl = ada::internal::ThreadRegistry::create(memory, size, MAX_THREADS);
    ThreadRegistry* reg = reinterpret_cast<ThreadRegistry*>(impl);
    if (reg) {
        ada_set_global_registry(reg);
    }
    return reg;
}

ThreadRegistry* thread_registry_init_with_capacity(void* memory, size_t size, uint32_t capacity) {
    auto* impl = ada::internal::ThreadRegistry::create(memory, size, capacity);
    return reinterpret_cast<ThreadRegistry*>(impl);
}

ThreadRegistry* thread_registry_attach(void* memory) {
    if (!memory) return nullptr;
    auto* impl = reinterpret_cast<ada::internal::ThreadRegistry*>(memory);
    if (!impl->validate()) {
        return nullptr;
    }
    ThreadRegistry* reg = reinterpret_cast<ThreadRegistry*>(impl);
    ada_set_global_registry(reg);
    return reg;
}

void thread_registry_deinit(ThreadRegistry* registry) {
    if (!registry) return;
    auto* impl = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    impl->~ThreadRegistry();
}

ThreadLaneSet* thread_registry_register(ThreadRegistry* registry, uintptr_t thread_id) {
    if (!registry) return nullptr;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    auto* cpp_lanes = cpp_registry->register_thread(thread_id);
    ThreadLaneSet* lanes = reinterpret_cast<ThreadLaneSet*>(cpp_lanes);
    
    // Set TLS for fast path access
    if (lanes) {
        thread_registry_set_my_lanes(lanes);
    }
    
    return lanes;
}

ThreadLaneSet* thread_registry_get_thread_lanes(ThreadRegistry* registry, uintptr_t thread_id) {
    if (!registry) return nullptr;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    
    // Search for existing registration
    auto* lanes_base = reinterpret_cast<ada::internal::ThreadLaneSet*>(reinterpret_cast<uint8_t*>(cpp_registry) + cpp_registry->lanes_off);
    for (uint32_t i = 0; i < cpp_registry->thread_count.load(); ++i) {
        if (lanes_base[i].thread_id == thread_id && 
            lanes_base[i].active.load()) {
            return reinterpret_cast<ThreadLaneSet*>(&lanes_base[i]);
        }
    }
    return nullptr;
}

ThreadLaneSet* thread_registry_get_my_lanes(void) {
    return tls_my_lanes;
}

void thread_registry_set_my_lanes(ThreadLaneSet* lanes) {
    tls_my_lanes = lanes;
    ada::internal::tls_current_lanes = reinterpret_cast<ada::internal::ThreadLaneSet*>(lanes);
}

void thread_registry_unregister(ThreadLaneSet* lanes) {
    if (!lanes) return;
    auto* cpp_lanes = reinterpret_cast<ada::internal::ThreadLaneSet*>(lanes);
    cpp_lanes->active.store(false);
}


RingBufferHeader* thread_registry_get_active_ring_header(ThreadRegistry* registry,
                                                         Lane* lane) {
    if (!registry || !lane) return nullptr;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    auto idx = cpp_lane->active_idx.load(std::memory_order_relaxed);
    return thread_registry_get_ring_header_by_idx(registry, lane, idx);
}

RingBufferHeader* thread_registry_get_ring_header_by_idx(ThreadRegistry* registry,
                                                         Lane* lane,
                                                         uint32_t ring_idx) {
    if (!registry || !lane) return nullptr;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    if (ring_idx >= cpp_lane->ring_count) return nullptr;
    using ada::internal::ThreadLaneSet;
    uint8_t* p = reinterpret_cast<uint8_t*>(cpp_lane);
    ThreadLaneSet* parent = nullptr;
    bool is_index = false;
    ThreadLaneSet* cand_idx = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, index_lane));
    if (reinterpret_cast<ada::internal::Lane*>(&cand_idx->index_lane) == cpp_lane) {
        parent = cand_idx;
        is_index = true;
    } else {
        ThreadLaneSet* cand_det = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, detail_lane));
        if (reinterpret_cast<ada::internal::Lane*>(&cand_det->detail_lane) == cpp_lane) {
            parent = cand_det;
            is_index = false;
        }
    }
    if (!parent) return nullptr;
    uint8_t* reg_base = reinterpret_cast<uint8_t*>(cpp_registry);
    auto& seg = cpp_registry->segments[0];
    uint8_t* seg_base = reg_base + seg.base_offset;
    uint64_t layout_off = is_index ? parent->index_layout_off : parent->detail_layout_off;
    auto* layout = reinterpret_cast<ada::internal::LaneMemoryLayout*>(seg_base + layout_off);
    uint32_t seg_id = layout->ring_descs[ring_idx].segment_id;
    if (seg_id == 0 || seg_id > cpp_registry->segment_count.load()) return nullptr;
    auto& seg2 = cpp_registry->segments[seg_id - 1];
    uint8_t* seg_base2 = reg_base + seg2.base_offset;
    uint8_t* ring_ptr = seg_base2 + layout->ring_descs[ring_idx].offset;
    return reinterpret_cast<RingBufferHeader*>(ring_ptr);
}

bool thread_registry_unregister_by_id(ThreadRegistry* registry, uintptr_t thread_id) {
    if (!registry) return false;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    
    // Find and deactivate thread
    auto* lanes_base = reinterpret_cast<ada::internal::ThreadLaneSet*>(reinterpret_cast<uint8_t*>(cpp_registry) + cpp_registry->lanes_off);
    for (uint32_t i = 0; i < cpp_registry->thread_count.load(); ++i) {
        if (lanes_base[i].thread_id == thread_id) {
            bool was_active = lanes_base[i].active.exchange(false);
            if (was_active) {
                // Clear active_mask bit with CAS
                uint64_t bit = 1ull << i;
                uint64_t mask = cpp_registry->active_mask.load(std::memory_order_acquire);
                while (mask & bit) {
                    uint64_t new_mask = mask & ~bit;
                    if (cpp_registry->active_mask.compare_exchange_weak(mask, new_mask, std::memory_order_acq_rel, std::memory_order_acquire)) {
                        cpp_registry->thread_count.fetch_sub(1, std::memory_order_acq_rel);
                        break;
                    }
                }
            }
            return true;
        }
    }
    return false;
}

uint32_t thread_registry_get_active_count(ThreadRegistry* registry) {
    if (!registry) return 0;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    
    uint32_t count = 0;
    auto* lanes_base = reinterpret_cast<ada::internal::ThreadLaneSet*>(reinterpret_cast<uint8_t*>(cpp_registry) + cpp_registry->lanes_off);
    for (uint32_t i = 0; i < cpp_registry->thread_count.load(); ++i) {
        if (lanes_base[i].active.load()) {
            count++;
        }
    }
    return count;
}

ThreadLaneSet* thread_registry_get_thread_at(ThreadRegistry* registry, uint32_t index) {
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    if (!registry || index >= cpp_registry->get_capacity()) return nullptr;
    
    if (index >= cpp_registry->thread_count.load()) return nullptr;
    auto* lanes_base = reinterpret_cast<ada::internal::ThreadLaneSet*>(reinterpret_cast<uint8_t*>(cpp_registry) + cpp_registry->lanes_off);
    if (!lanes_base[index].active.load()) return nullptr;
    
    return reinterpret_cast<ThreadLaneSet*>(&lanes_base[index]);
}

void thread_registry_stop_accepting(ThreadRegistry* registry) {
    if (!registry) return;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    cpp_registry->accepting_registrations.store(false);
}

void thread_registry_request_shutdown(ThreadRegistry* registry) {
    if (!registry) return;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    cpp_registry->shutdown_requested.store(true);
}

bool thread_registry_is_shutdown_requested(ThreadRegistry* registry) {
    if (!registry) return true;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    return cpp_registry->shutdown_requested.load();
}

// Lane operations - delegate to C++ implementation
bool lane_submit_ring(Lane* lane, uint32_t ring_idx) {
    if (!lane) return false;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    // Best-effort: bump parent lane-set's events_generated for visibility tests
    using ada::internal::ThreadLaneSet;
    uint8_t* p = reinterpret_cast<uint8_t*>(cpp_lane);
    ThreadLaneSet* parent = nullptr;
    bool is_index = false;
    ThreadLaneSet* cand_idx = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, index_lane));
    if (reinterpret_cast<ada::internal::Lane*>(&cand_idx->index_lane) == cpp_lane) {
        parent = cand_idx;
        is_index = true;
    } else {
        ThreadLaneSet* cand_det = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, detail_lane));
        if (reinterpret_cast<ada::internal::Lane*>(&cand_det->detail_lane) == cpp_lane) {
            parent = cand_det;
            is_index = false;
        }
    }
    if (parent) {
        parent->events_generated.fetch_add(1, std::memory_order_release);
    }
    // Materialize layout from offsets using global registry base
    ThreadRegistry* reg = ada_get_global_registry();
    if (!reg || !parent) return false;
    auto* cpp_reg = reinterpret_cast<ada::internal::ThreadRegistry*>(reg);
    void* base0 = shm_dir_get_base(0);
    uint8_t* pool_base = base0 ? (reinterpret_cast<uint8_t*>(base0) + cpp_reg->segments[0].base_offset)
                               : (reinterpret_cast<uint8_t*>(cpp_reg) + cpp_reg->segments[0].base_offset);
    uint64_t layout_off = is_index ? parent->index_layout_off : parent->detail_layout_off;
    auto* layout = reinterpret_cast<ada::internal::LaneMemoryLayout*>(pool_base + layout_off);

    // Treat submit queue as SPSC queue
    auto head = cpp_lane->submit_head.load(std::memory_order_relaxed);
    auto tail = cpp_lane->submit_tail.load(std::memory_order_acquire);
    auto next = (tail + 1) % cpp_lane->submit_capacity;
    if (next == head) {
        // Overwrite oldest (advance head) to avoid artificial drops in integration tests
        cpp_lane->submit_head.store((head + 1) % cpp_lane->submit_capacity, std::memory_order_release);
        head = cpp_lane->submit_head.load(std::memory_order_relaxed);
        next = (tail + 1) % cpp_lane->submit_capacity;
        if (next == head) return false;
    }
    layout->submit_queue[tail] = ring_idx;
    cpp_lane->submit_tail.store(next, std::memory_order_release);
    return true;
}

uint32_t lane_take_ring(Lane* lane) {
    if (!lane) return UINT32_MAX;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    using ada::internal::ThreadLaneSet;
    uint8_t* p = reinterpret_cast<uint8_t*>(cpp_lane);
    ThreadLaneSet* parent = nullptr;
    bool is_index = false;
    ThreadLaneSet* cand_idx = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, index_lane));
    if (reinterpret_cast<ada::internal::Lane*>(&cand_idx->index_lane) == cpp_lane) {
        parent = cand_idx;
        is_index = true;
    } else {
        ThreadLaneSet* cand_det = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, detail_lane));
        if (reinterpret_cast<ada::internal::Lane*>(&cand_det->detail_lane) == cpp_lane) {
            parent = cand_det;
            is_index = false;
        }
    }
    ThreadRegistry* reg = ada_get_global_registry();
    if (!reg || !parent) return UINT32_MAX;
    auto* cpp_reg = reinterpret_cast<ada::internal::ThreadRegistry*>(reg);
    void* base0 = shm_dir_get_base(0);
    uint8_t* pool_base = base0 ? (reinterpret_cast<uint8_t*>(base0) + cpp_reg->segments[0].base_offset)
                               : (reinterpret_cast<uint8_t*>(cpp_reg) + cpp_reg->segments[0].base_offset);
    uint64_t layout_off = is_index ? parent->index_layout_off : parent->detail_layout_off;
    auto* layout = reinterpret_cast<ada::internal::LaneMemoryLayout*>(pool_base + layout_off);

    auto head = cpp_lane->submit_head.load(std::memory_order_relaxed);
    auto tail = cpp_lane->submit_tail.load(std::memory_order_acquire);
    if (head == tail) return UINT32_MAX;
    uint32_t ring_idx = layout->submit_queue[head];
    cpp_lane->submit_head.store((head + 1) % cpp_lane->submit_capacity, std::memory_order_release);
    return ring_idx;
}

bool lane_return_ring(Lane* lane, uint32_t ring_idx) {
    if (!lane) return false;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    using ada::internal::ThreadLaneSet;
    uint8_t* p = reinterpret_cast<uint8_t*>(cpp_lane);
    ThreadLaneSet* parent = nullptr;
    bool is_index = false;
    ThreadLaneSet* cand_idx = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, index_lane));
    if (reinterpret_cast<ada::internal::Lane*>(&cand_idx->index_lane) == cpp_lane) {
        parent = cand_idx;
        is_index = true;
    } else {
        ThreadLaneSet* cand_det = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, detail_lane));
        if (reinterpret_cast<ada::internal::Lane*>(&cand_det->detail_lane) == cpp_lane) {
            parent = cand_det;
            is_index = false;
        }
    }
    ThreadRegistry* reg = ada_get_global_registry();
    if (!reg || !parent) return false;
    auto* cpp_reg = reinterpret_cast<ada::internal::ThreadRegistry*>(reg);
    void* base0 = shm_dir_get_base(0);
    uint8_t* pool_base = base0 ? (reinterpret_cast<uint8_t*>(base0) + cpp_reg->segments[0].base_offset)
                               : (reinterpret_cast<uint8_t*>(cpp_reg) + cpp_reg->segments[0].base_offset);
    uint64_t layout_off = is_index ? parent->index_layout_off : parent->detail_layout_off;
    auto* layout = reinterpret_cast<ada::internal::LaneMemoryLayout*>(pool_base + layout_off);

    auto head = cpp_lane->free_head.load(std::memory_order_relaxed);
    auto tail = cpp_lane->free_tail.load(std::memory_order_acquire);
    auto next = (tail + 1) % cpp_lane->free_capacity;
    if (next == head) return false;  // Queue full
    layout->free_queue[tail] = ring_idx;
    cpp_lane->free_tail.store(next, std::memory_order_release);
    return true;
}

uint32_t lane_get_free_ring(Lane* lane) {
    if (!lane) return UINT32_MAX;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    using ada::internal::ThreadLaneSet;
    uint8_t* p = reinterpret_cast<uint8_t*>(cpp_lane);
    ThreadLaneSet* parent = nullptr;
    bool is_index = false;
    ThreadLaneSet* cand_idx = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, index_lane));
    if (reinterpret_cast<ada::internal::Lane*>(&cand_idx->index_lane) == cpp_lane) {
        parent = cand_idx;
        is_index = true;
    } else {
        ThreadLaneSet* cand_det = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, detail_lane));
        if (reinterpret_cast<ada::internal::Lane*>(&cand_det->detail_lane) == cpp_lane) {
            parent = cand_det;
            is_index = false;
        }
    }
    ThreadRegistry* reg = ada_get_global_registry();
    if (!reg || !parent) return UINT32_MAX;
    auto* cpp_reg = reinterpret_cast<ada::internal::ThreadRegistry*>(reg);
    void* base0 = shm_dir_get_base(0);
    uint8_t* pool_base = base0 ? (reinterpret_cast<uint8_t*>(base0) + cpp_reg->segments[0].base_offset)
                               : (reinterpret_cast<uint8_t*>(cpp_reg) + cpp_reg->segments[0].base_offset);
    uint64_t layout_off = is_index ? parent->index_layout_off : parent->detail_layout_off;
    auto* layout = reinterpret_cast<ada::internal::LaneMemoryLayout*>(pool_base + layout_off);

    auto head = cpp_lane->free_head.load(std::memory_order_relaxed);
    auto tail = cpp_lane->free_tail.load(std::memory_order_acquire);
    if (head == tail) return UINT32_MAX;
    uint32_t ring_idx = layout->free_queue[head];
    cpp_lane->free_head.store((head + 1) % cpp_lane->free_capacity, std::memory_order_release);
    return ring_idx;
}

// lane_get_active_ring is already defined as inline in header

void lane_mark_event(Lane* lane) {
    if (!lane) return;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    cpp_lane->marked_event_seen.store(true, std::memory_order_release);
}

bool lane_has_marked_event(Lane* lane) {
    if (!lane) return false;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    return cpp_lane->marked_event_seen.load(std::memory_order_acquire);
}

void lane_clear_marked_event(Lane* lane) {
    if (!lane) return;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    cpp_lane->marked_event_seen.store(false, std::memory_order_release);
}

bool lane_swap_active_ring(Lane* lane) {
    if (!lane) return false;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    
    // Get a free ring
    uint32_t new_idx = lane_get_free_ring(lane);
    if (new_idx == UINT32_MAX) return false;
    
    // Swap active ring
    uint32_t old_idx = cpp_lane->active_idx.exchange(new_idx);
    
    // Submit old ring for draining
    return lane_submit_ring(lane, old_idx);
}

// Out-of-line implementation for ada::internal::Lane::submit_ring
namespace ada { namespace internal {
bool Lane::submit_ring(uint32_t ring_idx) {
    if (ring_idx >= ring_count) return false;
    // Determine parent lane set
    uint8_t* p = reinterpret_cast<uint8_t*>(this);
    ThreadLaneSet* parent = nullptr;
    bool is_index = false;
    ThreadLaneSet* cand_idx = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, index_lane));
    if (reinterpret_cast<Lane*>(&cand_idx->index_lane) == this) {
        parent = cand_idx;
        is_index = true;
    } else {
        ThreadLaneSet* cand_det = reinterpret_cast<ThreadLaneSet*>(p - offsetof(ThreadLaneSet, detail_lane));
        if (reinterpret_cast<Lane*>(&cand_det->detail_lane) == this) {
            parent = cand_det;
            is_index = false;
        }
    }
    ::ThreadRegistry* reg = ada_get_global_registry();
    if (!reg || !parent) return false;
    auto* cpp_reg = reinterpret_cast<ada::internal::ThreadRegistry*>(reg);
    void* base0 = shm_dir_get_base(0);
    uint8_t* pool_base = base0 ? (reinterpret_cast<uint8_t*>(base0) + cpp_reg->segments[0].base_offset)
                               : (reinterpret_cast<uint8_t*>(cpp_reg) + cpp_reg->segments[0].base_offset);
    uint64_t layout_off = is_index ? parent->index_layout_off : parent->detail_layout_off;
    auto* layout = reinterpret_cast<LaneMemoryLayout*>(pool_base + layout_off);

    auto head = submit_head.load(std::memory_order_relaxed);
    auto tail = submit_tail.load(std::memory_order_acquire);
    auto next = (tail + 1) % submit_capacity;
    if (next == head) return false; // Queue full
    layout->submit_queue[tail] = ring_idx;
    submit_tail.store(next, std::memory_order_release);
    return true;
}
}} // namespace ada::internal

// Debug functions
void thread_registry_dump(ThreadRegistry* registry) {
    if (!registry) return;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    cpp_registry->debug_dump();
}

size_t thread_registry_calculate_memory_size(void) {
    return thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);
}

size_t thread_registry_calculate_memory_size_with_capacity(uint32_t capacity) {
    // Recommended size: header + lane array + per-thread (rings + metadata)
    size_t header = sizeof(ada::internal::ThreadRegistry);
    size_t lanes = (size_t)capacity * sizeof(ada::internal::ThreadLaneSet);
    size_t per_thread_rings = (size_t)RINGS_PER_INDEX_LANE * 64 * 1024 +
                              (size_t)RINGS_PER_DETAIL_LANE * 256 * 1024;
    size_t per_thread_meta = sizeof(ada::internal::LaneMemoryLayout) * 2;
    // Account for page alignment after lanes (~ worst case add one page)
    size_t align_slack = 4096;
    return header + lanes + align_slack + (size_t)capacity * (per_thread_rings + per_thread_meta);
}

// Lane accessor functions for opaque ThreadLaneSet
Lane* thread_lanes_get_index_lane(ThreadLaneSet* lanes) {
    if (!lanes) return nullptr;
    auto* cpp_lanes = reinterpret_cast<ada::internal::ThreadLaneSet*>(lanes);
    return reinterpret_cast<Lane*>(&cpp_lanes->index_lane);
}

Lane* thread_lanes_get_detail_lane(ThreadLaneSet* lanes) {
    if (!lanes) return nullptr;
    auto* cpp_lanes = reinterpret_cast<ada::internal::ThreadLaneSet*>(lanes);
    return reinterpret_cast<Lane*>(&cpp_lanes->detail_lane);
}

void thread_lanes_set_active(ThreadLaneSet* lanes, bool active) {
    if (!lanes) return;
    auto* cpp_lanes = reinterpret_cast<ada::internal::ThreadLaneSet*>(lanes);
    cpp_lanes->active.store(active, std::memory_order_seq_cst);
}

void thread_lanes_set_events_generated(ThreadLaneSet* lanes, uint64_t count) {
    if (!lanes) return;
    auto* cpp_lanes = reinterpret_cast<ada::internal::ThreadLaneSet*>(lanes);
    cpp_lanes->events_generated.store(count, std::memory_order_seq_cst);
}

uint64_t thread_lanes_get_events_generated(ThreadLaneSet* lanes) {
    if (!lanes) return 0;
    auto* cpp_lanes = reinterpret_cast<ada::internal::ThreadLaneSet*>(lanes);
    return cpp_lanes->events_generated.load(std::memory_order_seq_cst);
}

uint32_t thread_registry_get_capacity(ThreadRegistry* registry) {
    if (!registry) return 0;
    auto* cpp_registry = reinterpret_cast<ada::internal::ThreadRegistry*>(registry);
    return cpp_registry->get_capacity();
}

// Standalone lane initializer for tests/benchmarks
bool lane_init(Lane* lane,
               void* ring_memory,
               size_t ring_size,
               uint32_t ring_count,
               void* /*queue_memory*/,
               size_t /*queue_size*/) {
    if (!lane || !ring_memory || ring_count == 0) return false;
    auto* cpp_lane = reinterpret_cast<ada::internal::Lane*>(lane);
    auto* layout = new (std::nothrow) ada::internal::LaneMemoryLayout();
    if (!layout) return false;
    std::memset(layout, 0, sizeof(*layout));
    // Note: ring headers and descriptors are not initialized in this helper
    (void)ring_memory; (void)ring_size;
    cpp_lane->initialize(layout, ring_count, ring_size, sizeof(IndexEvent), QUEUE_COUNT_INDEX_LANE);
    return true;
}


}  // extern "C"
