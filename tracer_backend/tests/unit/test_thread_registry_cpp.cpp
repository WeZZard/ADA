#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <chrono>

// Need to include both to compare
extern "C" {
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/shared_memory.h>
}

#include "thread_registry_private.h"

using namespace ada;

class ThreadRegistryCppTest : public ::testing::Test {
protected:
    static SharedMemoryRef shm;
    void* memory = nullptr;
    size_t memory_size = 0;
    ThreadRegistryCpp* registry = nullptr;

    static void SetUpTestSuite() {
        if (!shm) {
            size_t size = 64 * 1024 * 1024;  // 64MB
            char name_buf[256];
            shm = shared_memory_create_unique("test_cpp", shared_memory_get_pid(), 
                                             shared_memory_get_session_id(),
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

        registry = ThreadRegistryCpp::create(memory, memory_size);
        ASSERT_NE(registry, nullptr) << "Failed to create C++ registry";
        
        // Validate structure
        ASSERT_TRUE(registry->validate()) << "Registry validation failed";
    }

    void TearDown() override {
        registry = nullptr;
        // Clear memory for next test
        if (memory) {
            memset(memory, 0, memory_size);
        }
    }
};

SharedMemoryRef ThreadRegistryCppTest::shm = nullptr;

// Test basic registration
TEST_F(ThreadRegistryCppTest, cpp_registry__single_registration__then_succeeds) {
    uintptr_t tid = 12345;
    
    auto* lanes = registry->register_thread(tid);
    
    ASSERT_NE(lanes, nullptr) << "Registration should succeed";
    EXPECT_EQ(lanes->thread_id, tid) << "Thread ID should match";
    EXPECT_EQ(lanes->slot_index, 0) << "First thread should get slot 0";
    EXPECT_TRUE(lanes->active.load()) << "Thread should be active";
    EXPECT_EQ(registry->thread_count.load(), 1) << "Thread count should be 1";
}

// Test duplicate registration returns same lanes
TEST_F(ThreadRegistryCppTest, cpp_registry__duplicate_registration__then_returns_same) {
    uintptr_t tid = 12345;
    
    auto* lanes1 = registry->register_thread(tid);
    ASSERT_NE(lanes1, nullptr);
    
    auto* lanes2 = registry->register_thread(tid);
    ASSERT_NE(lanes2, nullptr);
    
    EXPECT_EQ(lanes1, lanes2) << "Should return same lanes for same thread";
    EXPECT_EQ(registry->thread_count.load(), 1) << "Thread count should still be 1";
}

// Test memory layout is debuggable
TEST_F(ThreadRegistryCppTest, cpp_registry__memory_layout__then_debuggable) {
    auto* lanes = registry->register_thread(12345);
    ASSERT_NE(lanes, nullptr);
    
    // Can we access structured memory?
    EXPECT_NE(lanes->index_lane.memory_layout, nullptr);
    EXPECT_NE(lanes->detail_lane.memory_layout, nullptr);
    
    // Are the memory regions properly initialized?
    auto* index_layout = lanes->index_lane.memory_layout;
    for (uint32_t i = 0; i < RINGS_PER_INDEX_LANE; i++) {
        EXPECT_NE(index_layout->ring_ptrs[i], nullptr) 
            << "Ring " << i << " should be initialized";
    }
    
    // Can we actually write to the queues?
    index_layout->submit_queue[0] = 42;
    EXPECT_EQ(index_layout->submit_queue[0], 42) << "Should be able to write to queue";
}

// Test concurrent registration
TEST_F(ThreadRegistryCppTest, cpp_registry__concurrent_registration__then_unique_slots) {
    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::vector<ThreadLaneSetCpp*> results(num_threads);
    
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([this, &results, i]() {
            results[i] = registry->register_thread(1000 + i);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All should succeed
    for (int i = 0; i < num_threads; i++) {
        EXPECT_NE(results[i], nullptr) << "Thread " << i << " should register";
    }
    
    // All should have unique slots
    std::vector<uint32_t> slots;
    for (auto* lanes : results) {
        if (lanes) {
            slots.push_back(lanes->slot_index);
        }
    }
    std::sort(slots.begin(), slots.end());
    
    for (size_t i = 0; i < slots.size(); i++) {
        EXPECT_EQ(slots[i], i) << "Slots should be 0 through " << (num_threads - 1);
    }
    
    EXPECT_EQ(registry->thread_count.load(), num_threads);
}

// Test SPSC queue operations
TEST_F(ThreadRegistryCppTest, cpp_lane__queue_operations__then_correct) {
    auto* lanes = registry->register_thread(12345);
    ASSERT_NE(lanes, nullptr);
    
    // Submit some rings
    EXPECT_TRUE(lanes->index_lane.submit_ring(1));
    EXPECT_TRUE(lanes->index_lane.submit_ring(2));
    EXPECT_TRUE(lanes->index_lane.submit_ring(3));
    
    // Verify through direct memory access (debuggable!)
    auto* layout = lanes->index_lane.memory_layout;
    EXPECT_EQ(layout->submit_queue[0], 1);
    EXPECT_EQ(layout->submit_queue[1], 2);
    EXPECT_EQ(layout->submit_queue[2], 3);
}

// Test alignment guarantees
TEST_F(ThreadRegistryCppTest, cpp_registry__alignment__then_cache_aligned) {
    // Registry itself
    EXPECT_EQ(reinterpret_cast<uintptr_t>(registry) % CACHE_LINE_SIZE, 0)
        << "Registry should be cache-aligned";
    
    // Each thread lane set
    for (uint32_t i = 0; i < MAX_THREADS; i++) {
        auto addr = reinterpret_cast<uintptr_t>(&registry->thread_lanes[i]);
        EXPECT_EQ(addr % CACHE_LINE_SIZE, 0) 
            << "ThreadLane[" << i << "] should be cache-aligned";
    }
    
    // Memory layouts should also be aligned
    auto* lanes = registry->register_thread(12345);
    ASSERT_NE(lanes, nullptr);
    
    auto* index_layout = lanes->index_lane.memory_layout;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(index_layout->submit_queue) % CACHE_LINE_SIZE, 0)
        << "Submit queue should be cache-aligned";
    EXPECT_EQ(reinterpret_cast<uintptr_t>(index_layout->free_queue) % CACHE_LINE_SIZE, 0)
        << "Free queue should be cache-aligned";
}

// Test debug utilities work
TEST_F(ThreadRegistryCppTest, cpp_registry__debug_dump__then_produces_output) {
    auto* lanes1 = registry->register_thread(12345);
    auto* lanes2 = registry->register_thread(67890);
    
    ASSERT_NE(lanes1, nullptr);
    ASSERT_NE(lanes2, nullptr);
    
    // Capture debug output
    testing::internal::CaptureStdout();
    registry->debug_dump();
    std::string output = testing::internal::GetCapturedStdout();
    
    // Debug: Print the actual output
    printf("Debug output:\n%s\n", output.c_str());
    
    // Verify output contains expected information
    EXPECT_NE(output.find("ThreadRegistryCpp Debug Dump"), std::string::npos);
    EXPECT_NE(output.find("Thread count: 2"), std::string::npos);
    EXPECT_NE(output.find("tid=3039"), std::string::npos);  // 12345 in hex
    EXPECT_NE(output.find("tid=10932"), std::string::npos);  // 67890 in hex
}

// Test C compatibility layer
TEST_F(ThreadRegistryCppTest, cpp_registry__c_compatibility__then_works) {
    // Create through C interface
    ThreadRegistry* c_registry = thread_registry_init(memory, memory_size);
    ASSERT_NE(c_registry, nullptr);
    
    // Register through C interface
    ThreadLaneSet* c_lanes = thread_registry_register(c_registry, 12345);
    ASSERT_NE(c_lanes, nullptr);
    
    // Verify it's actually our C++ implementation
    auto* cpp_registry = reinterpret_cast<ThreadRegistryCpp*>(c_registry);
    EXPECT_EQ(cpp_registry->thread_count.load(), 1);
    
    // Debug dump through C interface
    thread_registry_dump(c_registry);
}

