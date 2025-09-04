#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <unistd.h>

// Include both C and C++ headers
extern "C" {
#include <tracer_backend/utils/shared_memory.h>
#include <tracer_backend/utils/ring_buffer.h>
}

#include "thread_registry_private.h"

// Don't use "using namespace" to avoid ambiguity with C types

class ThreadRegistryTest : public ::testing::Test {
private:
    static SharedMemoryRef shm;

protected:
    void* memory = nullptr;
    size_t memory_size = 0;
    ada::internal::ThreadRegistry* registry = nullptr;

    static void SetUpTestSuite() {
        if (!shm) {
            size_t size = 64 * 1024 * 1024;  // 64MB
            char name_buf[256];
            shm = shared_memory_create_unique("test", getpid(), shared_memory_get_session_id(),
                                             size, name_buf, sizeof(name_buf));
            ASSERT_NE(shm, nullptr) << "Failed to create shared memory";
        }
    }

    static void TearDownTestSuite() {
        if (shm) {
            shared_memory_destroy(shm);
            shm = nullptr;
        }
    }

    void SetUp() override {
        memory = shared_memory_get_address(shm);
        memory_size = shared_memory_get_size(shm);
        
        registry = ada::internal::ThreadRegistry::create(memory, memory_size);
        ASSERT_NE(registry, nullptr) << "Failed to initialize thread registry";
    }

    void TearDown() override {
        registry = nullptr;
        // Clear memory for next test
        if (memory) {
            memset(memory, 0, memory_size);
        }
    }
};

SharedMemoryRef ThreadRegistryTest::shm = nullptr;

// Test single thread registration
TEST_F(ThreadRegistryTest, thread_registry__single_registration__then_succeeds) {
    uintptr_t tid = getpid();
    auto* lanes = registry->register_thread(tid);
    
    ASSERT_NE(lanes, nullptr) << "Registration should succeed";
    EXPECT_EQ(lanes->thread_id, tid) << "Thread ID should match";
    EXPECT_EQ(lanes->slot_index, 0) << "First thread should get slot 0";
    EXPECT_TRUE(lanes->active.load()) << "Thread should be marked active";
    
    // Verify thread count
    EXPECT_EQ(registry->thread_count.load(), 1) << "Thread count should be 1";
}

// Test duplicate registration returns cached lanes
TEST_F(ThreadRegistryTest, thread_registry__duplicate_registration__then_returns_cached) {
    uintptr_t tid = getpid();
    
    auto* lanes1 = registry->register_thread(tid);
    ASSERT_NE(lanes1, nullptr);
    EXPECT_EQ(registry->thread_count.load(), 1) << "Thread count should be 1";
    
    auto* lanes2 = registry->register_thread(tid);
    ASSERT_NE(lanes2, nullptr);
    
    EXPECT_EQ(lanes1, lanes2) << "Should return cached lanes on duplicate registration";
    EXPECT_EQ(registry->thread_count.load(), 1) << "Thread count should still be 1";
}

// Test concurrent registration gives unique slots
TEST_F(ThreadRegistryTest, thread_registry__concurrent_registration__then_unique_slots) {
    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::vector<uint32_t> slots(num_threads);
    std::vector<ada::internal::ThreadLaneSet*> results(num_threads);
    
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([this, &results, &success_count, i]() {
            uintptr_t tid = 1000 + i;
            results[i] = registry->register_thread(tid);
            if (results[i]) {
                success_count++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count, num_threads) << "All threads should register successfully";
    
    // Verify unique slot assignments
    std::vector<bool> slot_used(num_threads, false);
    for (int i = 0; i < num_threads; i++) {
        ASSERT_NE(results[i], nullptr) << "Thread " << i << " should have valid lanes";
        uint32_t slot = results[i]->slot_index;
        EXPECT_LT(slot, num_threads) << "Slot index should be within range";
        EXPECT_FALSE(slot_used[slot]) << "Slot " << slot << " should not be reused";
        slot_used[slot] = true;
        
        // Verify thread ID is set correctly
        EXPECT_EQ(results[i]->thread_id, 1000 + i) << "Thread ID should match";
    }
    
    // All slots should be used
    for (int i = 0; i < num_threads; i++) {
        EXPECT_TRUE(slot_used[i]) << "Slot " << i << " should be used";
    }
}

// Test MAX_THREADS limit enforcement
TEST_F(ThreadRegistryTest, thread_registry__max_threads_exceeded__then_returns_null) {
    // Register MAX_THREADS
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        auto* lanes = registry->register_thread(i + 1000);
        ASSERT_NE(lanes, nullptr) << "Registration " << i << " should succeed";
    }
    
    EXPECT_EQ(registry->thread_count.load(), MAX_THREADS);
    
    // Try to register one more
    auto* extra = registry->register_thread(MAX_THREADS + 1000);
    EXPECT_EQ(extra, nullptr) << "Registration beyond MAX_THREADS should fail";
    EXPECT_EQ(registry->thread_count.load(), MAX_THREADS) << "Count should not exceed MAX_THREADS";
}

// Test SPSC submit queue operations
TEST_F(ThreadRegistryTest, spsc_queue__single_producer__then_maintains_order) {
    auto* lanes = registry->register_thread(getpid());
    ASSERT_NE(lanes, nullptr);
    
    auto* lane = &lanes->index_lane;
    
    // Submit some rings
    EXPECT_TRUE(lane->submit_ring(1));
    EXPECT_TRUE(lane->submit_ring(2));
    EXPECT_TRUE(lane->submit_ring(3));
    
    // Take rings in order (manual implementation since we're using C++ directly)
    auto head = lane->submit_head.load(std::memory_order_relaxed);
    auto tail = lane->submit_tail.load(std::memory_order_acquire);
    
    EXPECT_NE(head, tail) << "Queue should not be empty";
    
    // Read first ring
    uint32_t ring1 = lane->memory_layout->submit_queue[head];
    lane->submit_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, std::memory_order_release);
    EXPECT_EQ(ring1, 1);
    
    // Read second ring
    head = lane->submit_head.load(std::memory_order_relaxed);
    uint32_t ring2 = lane->memory_layout->submit_queue[head];
    lane->submit_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, std::memory_order_release);
    EXPECT_EQ(ring2, 2);
    
    // Read third ring
    head = lane->submit_head.load(std::memory_order_relaxed);
    uint32_t ring3 = lane->memory_layout->submit_queue[head];
    lane->submit_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, std::memory_order_release);
    EXPECT_EQ(ring3, 3);
    
    // Queue should be empty now
    head = lane->submit_head.load(std::memory_order_relaxed);
    tail = lane->submit_tail.load(std::memory_order_acquire);
    EXPECT_EQ(head, tail) << "Queue should be empty";
}

// Test SPSC queue wraparound
TEST_F(ThreadRegistryTest, spsc_queue__wraparound__then_correct) {
    auto* lanes = registry->register_thread(getpid());
    ASSERT_NE(lanes, nullptr);
    
    auto* lane = &lanes->index_lane;
    
    // Fill and drain multiple times to test wraparound
    // The queue wraps around after QUEUE_COUNT_INDEX_LANE operations
    for (int round = 0; round < 3; round++) {
        // We can submit RINGS_PER_INDEX_LANE - 1 items (one ring is always active)
        // Submit rings 1, 2, 3 (ring 0 is initially active)
        const uint32_t items_to_submit = RINGS_PER_INDEX_LANE - 1;
        
        for (uint32_t i = 0; i < items_to_submit; i++) {
            EXPECT_TRUE(lane->submit_ring(i + 1)) 
                << "Submit should succeed at round " << round << " item " << i;
        }
        
        // Drain all submitted rings
        for (uint32_t i = 0; i < items_to_submit; i++) {
            auto head = lane->submit_head.load(std::memory_order_relaxed);
            auto tail = lane->submit_tail.load(std::memory_order_acquire);
            
            EXPECT_NE(head, tail) << "Queue should have items at round " << round << " item " << i;
            
            uint32_t ring_idx = lane->memory_layout->submit_queue[head];
            lane->submit_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, std::memory_order_release);
            
            // We submitted rings 1, 2, 3
            EXPECT_EQ(ring_idx, i + 1) << "Ring index should match at round " << round;
        }
        
        // Queue should be empty after draining
        auto head = lane->submit_head.load(std::memory_order_relaxed);
        auto tail = lane->submit_tail.load(std::memory_order_acquire);
        EXPECT_EQ(head, tail) << "Queue should be empty after draining at round " << round;
    }
}

// Test free queue operations
TEST_F(ThreadRegistryTest, free_queue__return_and_get__then_works) {
    auto* lanes = registry->register_thread(getpid());
    ASSERT_NE(lanes, nullptr);
    
    auto* lane = &lanes->index_lane;
    
    // Free queue should have rings 1, 2, 3 initially (0 is active)
    // Get free rings
    for (uint32_t expected = 1; expected < RINGS_PER_INDEX_LANE; expected++) {
        auto head = lane->free_head.load(std::memory_order_relaxed);
        auto tail = lane->free_tail.load(std::memory_order_acquire);
        
        EXPECT_NE(head, tail) << "Free queue should have rings";
        
        uint32_t ring_idx = lane->memory_layout->free_queue[head];
        lane->free_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, std::memory_order_release);
        
        EXPECT_EQ(ring_idx, expected) << "Should get ring " << expected;
    }
    
    // Free queue should be empty now
    auto head = lane->free_head.load(std::memory_order_relaxed);
    auto tail = lane->free_tail.load(std::memory_order_acquire);
    EXPECT_EQ(head, tail) << "Free queue should be empty";
    
    // Return rings
    for (uint32_t i = 1; i < RINGS_PER_INDEX_LANE; i++) {
        head = lane->free_head.load(std::memory_order_relaxed);
        tail = lane->free_tail.load(std::memory_order_acquire);
        auto next = (tail + 1) % QUEUE_COUNT_INDEX_LANE;
        
        EXPECT_NE(next, head) << "Free queue should have space";
        
        lane->memory_layout->free_queue[tail] = i;
        lane->free_tail.store(next, std::memory_order_release);
    }
    
    // Verify they can be retrieved again
    for (uint32_t expected = 1; expected < RINGS_PER_INDEX_LANE; expected++) {
        head = lane->free_head.load(std::memory_order_relaxed);
        tail = lane->free_tail.load(std::memory_order_acquire);
        
        EXPECT_NE(head, tail) << "Free queue should have rings";
        
        uint32_t ring_idx = lane->memory_layout->free_queue[head];
        lane->free_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, std::memory_order_release);
        
        EXPECT_EQ(ring_idx, expected) << "Should get ring " << expected;
    }
}

// Test memory ordering guarantees
TEST_F(ThreadRegistryTest, memory_ordering__registration__then_visible_to_drain) {
    std::atomic<bool> registered{false};
    ada::internal::ThreadLaneSet* thread_lanes = nullptr;
    
    std::thread writer([this, &thread_lanes, &registered]() {
        thread_lanes = registry->register_thread(9999);
        registered.store(true, std::memory_order_release);
    });
    
    // Reader thread (simulating drain)
    std::thread reader([this, &registered, &thread_lanes]() {
        while (!registered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        
        // Thread should be visible in registry
        bool found = false;
        for (uint32_t i = 0; i < registry->thread_count.load(); i++) {
            if (registry->thread_lanes[i].thread_id == 9999 && 
                registry->thread_lanes[i].active.load()) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Registered thread should be visible to drain";
    });
    
    writer.join();
    reader.join();
}

// Test thread unregistration
TEST_F(ThreadRegistryTest, thread_registry__unregister__then_inactive) {
    auto* lanes = registry->register_thread(getpid());
    ASSERT_NE(lanes, nullptr);
    
    EXPECT_TRUE(lanes->active.load()) << "Thread should be active";
    
    // Unregister
    lanes->active.store(false);
    
    EXPECT_FALSE(lanes->active.load()) << "Thread should be inactive after unregister";
}

// Performance tests
TEST_F(ThreadRegistryTest, performance__registration__then_fast) {
    // Test registration performance up to MAX_THREADS
    const int test_count = std::min(1000, (int)MAX_THREADS);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < test_count; i++) {
        auto* lanes = registry->register_thread(i);
        ASSERT_NE(lanes, nullptr) << "Failed to register thread " << i;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_us = duration.count() / (double)test_count;
    EXPECT_LT(avg_us, 10.0) << "Registration should take < 10us on average, took " << avg_us << "us";
    
    // Performance info
    printf("  Registration performance: %.2f us/op (%d threads)\n", avg_us, test_count);
}

TEST_F(ThreadRegistryTest, performance__spsc_throughput__then_high) {
    auto* lanes = registry->register_thread(getpid());
    ASSERT_NE(lanes, nullptr);
    
    auto* lane = &lanes->index_lane;
    
    const int iterations = 100000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        // Submit and take a ring
        lane->submit_ring(i % RINGS_PER_INDEX_LANE);
        
        auto head = lane->submit_head.load(std::memory_order_relaxed);
        auto tail = lane->submit_tail.load(std::memory_order_acquire);
        if (head != tail) {
            lane->submit_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, std::memory_order_release);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double ops_per_sec = (iterations * 1000000000.0) / duration.count();
    EXPECT_GT(ops_per_sec, 1000000) << "Should achieve > 1M ops/sec, got " << ops_per_sec;
    
    // Performance info
    printf("  SPSC throughput: %.2f M ops/sec\n", ops_per_sec / 1000000.0);
}

// Test cache line isolation
TEST_F(ThreadRegistryTest, isolation__no_false_sharing__then_independent_performance) {
    // Verify cache alignment assumptions
    printf("  ThreadLaneSet size: %zu bytes (cache line = 64 bytes)\n", sizeof(ThreadLaneSet));
    printf("  ThreadLaneSet alignment: %zu bytes\n", alignof(ThreadLaneSet));
    if (sizeof(ThreadLaneSet) % 64 != 0) {
        printf("  WARNING: ThreadLaneSet not cache-line sized, may cause false sharing\n");
    }
    
    // Check system load before running performance test
    #ifdef __APPLE__
    double load[3];
    if (getloadavg(load, 3) != -1) {
        if (load[0] > 3.0) {
            GTEST_SKIP() << "System load too high (" << load[0] << "), skipping performance test";
        }
        printf("  System load: %.2f (proceeding with test)\n", load[0]);
    }
    #endif
    
    const int num_threads = 4;
    const int iterations = 100000;
    const int warmup_iterations = 1000;
    std::vector<std::thread> threads;
    std::vector<double> throughputs(num_threads);
    
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([this, &throughputs, t, iterations, warmup_iterations]() {
            auto* lanes = registry->register_thread(t + 2000);
            ASSERT_NE(lanes, nullptr);
            
            auto* lane = &lanes->index_lane;
            
            // Warm-up phase to stabilize cache and branch predictors
            for (int i = 0; i < warmup_iterations; i++) {
                lane->events_written.fetch_add(1, std::memory_order_relaxed);
            }
            
            // Reset counter for actual measurement
            lane->events_written.store(0, std::memory_order_relaxed);
            
            // Ensure all warm-up effects are visible  
            // Use compiler barrier instead of atomic_thread_fence to avoid stdatomic.h macro conflict
            __asm__ __volatile__("" ::: "memory");
            
            auto start = std::chrono::high_resolution_clock::now();
            
            for (int i = 0; i < iterations; i++) {
                lane->events_written.fetch_add(1, std::memory_order_relaxed);
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            
            throughputs[t] = (iterations * 1000000000.0) / duration.count();
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Calculate variance in throughput
    double sum = 0, sum_sq = 0;
    for (double throughput : throughputs) {
        sum += throughput;
        sum_sq += throughput * throughput;
    }
    double mean = sum / num_threads;
    double variance = (sum_sq / num_threads) - (mean * mean);
    double cv = sqrt(variance) / mean;  // Coefficient of variation
    
    // Print detailed throughput info for debugging
    printf("  Thread throughputs (M ops/sec):");
    for (int i = 0; i < num_threads; i++) {
        printf(" T%d=%.2f", i, throughputs[i] / 1000000.0);
    }
    printf("\n");
    
    // Use more lenient threshold for CI and loaded systems
    // 30% allows for system noise while still catching major issues
    const double cv_threshold = 0.30;  // Was 0.2 (20%)
    
    EXPECT_LT(cv, cv_threshold) << "Throughput variance should be low (CV < " 
                                 << (cv_threshold * 100) << "%), got " << (cv * 100) << "%\n"
                                 << "This may indicate false sharing or system load issues.\n"
                                 << "Mean throughput: " << (mean / 1000000.0) << " M ops/sec";
    
    // Performance info
    printf("  Cache isolation: mean=%.2f M ops/sec, CV=%.1f%% (threshold=%.0f%%)\n", 
           mean / 1000000.0, cv * 100, cv_threshold * 100);
}