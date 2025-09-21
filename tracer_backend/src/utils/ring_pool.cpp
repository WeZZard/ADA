#include <tracer_backend/utils/ring_pool.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/tracer_types.h>
#include "thread_registry_private.h"
#include <tracer_backend/ada/thread.h>
#include <tracer_backend/backpressure/backpressure.h>

struct AdaRingPool {
    ::ThreadRegistry* reg;
    ::ThreadLaneSet* lanes;
    int lane_type; // 0 = index, 1 = detail
    ada_backpressure_state_t* backpressure;
};

namespace {

static inline ::Lane* pool_get_lane(AdaRingPool* pool) {
    if (!pool) return nullptr;
    return (pool->lane_type == 0) ? thread_lanes_get_index_lane(pool->lanes)
                                  : thread_lanes_get_detail_lane(pool->lanes);
}

static uint32_t lane_free_count(::Lane* lane) {
    if (!lane) return 0;
    auto* cpp_lane = ada::internal::to_cpp(lane);
    uint32_t head = cpp_lane->free_head.load(std::memory_order_acquire);
    uint32_t tail = cpp_lane->free_tail.load(std::memory_order_acquire);
    uint32_t capacity = cpp_lane->free_capacity;
    if (capacity == 0) return 0;
    if (tail >= head) {
        return tail - head;
    }
    return capacity - (head - tail);
}

static void bp_bind_lane(AdaRingPool* pool, ::Lane* lane) {
    if (!pool || !pool->backpressure || !lane) return;
    auto* cpp_lane = ada::internal::to_cpp(lane);
    ada_backpressure_state_set_total_rings(pool->backpressure, cpp_lane->ring_count);
}

static void bp_sample_lane(AdaRingPool* pool, ::Lane* lane, uint64_t now_ns = 0) {
    if (!pool || !pool->backpressure || !lane) return;
    bp_bind_lane(pool, lane);
    ada_backpressure_state_sample(pool->backpressure, lane_free_count(lane), now_ns);
}

static void bp_mark_exhaustion(AdaRingPool* pool, uint64_t now_ns = 0) {
    if (!pool || !pool->backpressure) return;
    ada_backpressure_state_on_exhaustion(pool->backpressure, now_ns);
}

static void bp_mark_drop(AdaRingPool* pool, size_t bytes, uint64_t now_ns = 0) {
    if (!pool || !pool->backpressure) return;
    ada_backpressure_state_on_drop(pool->backpressure, bytes, now_ns);
}

} // namespace

extern "C" {

#ifdef ADA_TESTING
// Test hooks: default weak definitions allow tests to override behavior
extern "C" bool ada_test_should_fail_ring_pool_create(int lane_type) __attribute__((weak));
extern "C" void ada_test_on_ring_pool_destroy(int lane_type) __attribute__((weak));
// Provide default implementations when tests don't override
bool ada_test_should_fail_ring_pool_create(int) { return false; }
void ada_test_on_ring_pool_destroy(int) {}
#endif

RingPool* ring_pool_create(ThreadRegistry* registry, ThreadLaneSet* lanes, int lane_type) {
    if (!registry || !lanes) return nullptr;
    if (lane_type != 0 && lane_type != 1) return nullptr;
#ifdef ADA_TESTING
    if (ada_test_should_fail_ring_pool_create(lane_type)) {
        return nullptr;
    }
#endif
    ada_backpressure_state_t* bp_state = nullptr;
    ada_tls_state_t* tls = ada_get_tls_state();
    if (tls) {
        bp_state = &tls->backpressure[lane_type];
    }
    auto* p = new (std::nothrow) AdaRingPool{registry, lanes, lane_type, bp_state};
    if (!p) {
        return nullptr;
    }
    if (bp_state) {
        ::Lane* lane = pool_get_lane(p);
        if (lane) {
            bp_sample_lane(p, lane, 0);
        }
    }
    return reinterpret_cast<RingPool*>(p);
}

void ring_pool_destroy(RingPool* pool) {
    if (!pool) return;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
#ifdef ADA_TESTING
    ada_test_on_ring_pool_destroy(p->lane_type);
#endif
    delete p;
}

bool ring_pool_swap_active(RingPool* pool, uint32_t* out_old_idx) {
    if (!pool) return false;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    ::Lane* lane = pool_get_lane(p);
    if (!lane) return false;

    bp_sample_lane(p, lane, 0);

    uint32_t new_idx = lane_get_free_ring(lane);
    if (new_idx == UINT32_MAX) {
        if (ring_pool_handle_exhaustion(pool)) {
            new_idx = lane_get_free_ring(lane);
        }
        if (new_idx == UINT32_MAX) {
            auto* cpp_lane_fallback = ada::internal::to_cpp(lane);
            if (cpp_lane_fallback->ring_count > 1) {
                uint32_t cur = cpp_lane_fallback->active_idx.load(std::memory_order_acquire);
                new_idx = (cur + 1) % cpp_lane_fallback->ring_count;
            } else {
                bp_sample_lane(p, lane, 0);
                return false; // pool truly has no alternative
            }
        }
    }

    auto* cpp_lane = ada::internal::to_cpp(lane);
    uint32_t old_idx = cpp_lane->active_idx.exchange(new_idx, std::memory_order_acq_rel);
    if (out_old_idx) *out_old_idx = old_idx;

    bool submitted = lane_submit_ring(lane, old_idx);
    (void)submitted;

    bp_sample_lane(p, lane, 0);
    return true;
}

RingBufferHeader* ring_pool_get_active_header(RingPool* pool) {
    if (!pool) return nullptr;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    Lane* lane = (p->lane_type == 0) ? thread_lanes_get_index_lane(p->lanes)
                                     : thread_lanes_get_detail_lane(p->lanes);
    if (!lane) return nullptr;
    return thread_registry_get_active_ring_header(p->reg, lane);
}

bool ring_pool_handle_exhaustion(RingPool* pool) {
    if (!pool) return false;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    ::Lane* lane = pool_get_lane(p);
    if (!lane) return false;

    bp_sample_lane(p, lane, 0);
    bp_mark_exhaustion(p, 0);

    // Take the oldest ring from the submit queue
    uint32_t oldest = lane_take_ring(lane);
    if (oldest == UINT32_MAX) {
        bp_sample_lane(p, lane, 0);
        return false;
    }

    // Get the ring buffer header and drop the oldest event
    RingBufferHeader* hdr = thread_registry_get_ring_header_by_idx(p->reg, lane, oldest);
    if (hdr) {
        // Calculate the event size based on lane type
        size_t event_size = (p->lane_type == 0) ? sizeof(IndexEvent) : sizeof(DetailEvent);

        // Create a temporary ring buffer handle to drop the oldest event
        void* ring_mem = reinterpret_cast<void*>(hdr);
        // Attach to the ring buffer (doesn't reinitialize)
        RingBuffer* rb = ring_buffer_attach(ring_mem,
                                           (p->lane_type == 0) ? 64 * 1024 : 256 * 1024,
                                           event_size);
        if (rb) {
            // Drop the oldest event if the ring has any events
            bool dropped = ring_buffer_drop_oldest(rb);
            // Mark drop sequence even if ring was empty - this counts exhaustion attempts
            bp_mark_drop(p, dropped ? event_size : 0, 0);
            ring_buffer_destroy(rb);
        }
    }

    // Return the ring to the free queue
    bool returned = lane_return_ring(lane, oldest);
    bp_sample_lane(p, lane, 0);
    return returned;
}

bool ring_pool_mark_detail(RingPool* pool) {
    if (!pool) return false;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    if (p->lane_type != 1) return true; // no-op for index lanes
    Lane* lane = thread_lanes_get_detail_lane(p->lanes);
    if (!lane) return false;
    lane_mark_event(lane);
    return true;
}

bool ring_pool_is_detail_marked(RingPool* pool) {
    if (!pool) return false;
    auto* p = reinterpret_cast<AdaRingPool*>(pool);
    if (p->lane_type != 1) return false;
    Lane* lane = thread_lanes_get_detail_lane(p->lanes);
    if (!lane) return false;
    return lane_has_marked_event(lane);
}

} // extern "C"
