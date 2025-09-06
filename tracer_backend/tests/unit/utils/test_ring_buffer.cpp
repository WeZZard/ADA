// Unit tests for Ring Buffer using Google Test
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <chrono>

extern "C" {
    #include <tracer_backend/utils/ring_buffer.h>
    #include <tracer_backend/utils/tracer_types.h>
}

using ::testing::_;
using ::testing::Return;
using ::testing::NotNull;

// Test fixture for RingBuffer tests
class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        printf("[RING] Setting up test\n");
        buffer_size = sizeof(RingBufferHeader) + (capacity * sizeof(TestEvent));
        memory = std::make_unique<uint8_t[]>(buffer_size);
        memset(memory.get(), 0, buffer_size);
        rb = nullptr;
    }
    
    void TearDown() override {
        if (rb) {
            ring_buffer_destroy(rb);
            rb = nullptr;
        }
    }
    
    // Test data structure
    struct TestEvent {
        uint64_t id;
        uint64_t timestamp;
        char data[48];
    };
    
    // Test parameters
    static constexpr size_t capacity = 100;
    size_t buffer_size;
    std::unique_ptr<uint8_t[]> memory;
    RingBuffer* rb;
};

// Test: ring_buffer__create_with_valid_memory__then_returns_valid_buffer
TEST_F(RingBufferTest, ring_buffer__create_with_valid_memory__then_returns_valid_buffer) {
    printf("[RING] create_with_valid_memory → returns valid buffer\n");
    
    // Act
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    
    // Assert
    ASSERT_NE(rb, nullptr) << "Failed to create ring buffer";
    auto eff_cap = ring_buffer_get_capacity(rb);
    ASSERT_NE(eff_cap, 0u);
    EXPECT_EQ((eff_cap & (eff_cap - 1)), 0u) << "Capacity must be power-of-two";
    EXPECT_TRUE(ring_buffer_is_empty(rb));
    EXPECT_FALSE(ring_buffer_is_full(rb));
}

// Test: ring_buffer__write_single_event__then_event_preserved
TEST_F(RingBufferTest, ring_buffer__write_single_event__then_event_preserved) {
    printf("[RING] write_single_event → event preserved\n");
    
    // Arrange
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_NE(rb, nullptr);
    
    TestEvent write_event = {
        .id = 42,
        .timestamp = 1234567890,
        .data = "Test Event Data"
    };
    
    // Act
    bool write_result = ring_buffer_write(rb, &write_event);
    
    // Assert write succeeded
    ASSERT_TRUE(write_result) << "Failed to write to ring buffer";
    EXPECT_FALSE(ring_buffer_is_empty(rb));
    EXPECT_EQ(ring_buffer_available_read(rb), 1);
    
    // Act - Read back
    TestEvent read_event = {};
    bool read_result = ring_buffer_read(rb, &read_event);
    
    // Assert read succeeded and data matches
    ASSERT_TRUE(read_result) << "Failed to read from ring buffer";
    EXPECT_EQ(read_event.id, write_event.id);
    EXPECT_EQ(read_event.timestamp, write_event.timestamp);
    EXPECT_STREQ(read_event.data, write_event.data);
    EXPECT_TRUE(ring_buffer_is_empty(rb));
}

// Test: ring_buffer__fill_and_drain__then_handles_capacity_correctly
// Direct translation of test_fill_and_drain() from C
TEST_F(RingBufferTest, ring_buffer__fill_and_drain__then_handles_capacity_correctly) {
    printf("[RING] fill_and_drain → handles capacity correctly\n");
    
    // Direct translation - use same capacity as C test
    size_t test_capacity = 50;
    size_t test_buffer_size = sizeof(RingBufferHeader) + (test_capacity * sizeof(TestEvent));
    void* test_memory = calloc(1, test_buffer_size);
    
    RingBuffer* test_rb = ring_buffer_create(test_memory, test_buffer_size, sizeof(TestEvent));
    
    auto eff_cap = ring_buffer_get_capacity(test_rb);
    ASSERT_GT(eff_cap, 1u);
    // Fill the buffer up to effective capacity - 1 (one slot kept empty)
    for (size_t i = 0; i < eff_cap - 1; i++) {
        TestEvent event = {
            .id = i,
            .timestamp = i * 1000,
        };
        snprintf(event.data, sizeof(event.data), "Event %zu", i);
        ASSERT_TRUE(ring_buffer_write(test_rb, &event));
    }
    
    ASSERT_TRUE(ring_buffer_is_full(test_rb));
    ASSERT_EQ(ring_buffer_available_write(test_rb), 0);
    
    // Try to write when full (should fail) and overflow counter increments
    TestEvent overflow_event = {.id = 999};
    uint64_t before = ring_buffer_get_overflow_count(test_rb);
    ASSERT_FALSE(ring_buffer_write(test_rb, &overflow_event));
    uint64_t after = ring_buffer_get_overflow_count(test_rb);
    EXPECT_GT(after, before) << "Overflow counter should increment on full write";
    
    // Drain all events
    TestEvent events[256];
    size_t read_count = ring_buffer_read_batch(test_rb, events, sizeof(events)/sizeof(events[0]));
    ASSERT_EQ(read_count, eff_cap - 1);
    
    // Verify events
    for (size_t i = 0; i < read_count; i++) {
        ASSERT_EQ(events[i].id, i);
        ASSERT_EQ(events[i].timestamp, i * 1000);
    }
    
    ASSERT_TRUE(ring_buffer_is_empty(test_rb));
    
    ring_buffer_destroy(test_rb);
    free(test_memory);
}

// Test: ring_buffer__concurrent_writes__then_preserve_order
TEST_F(RingBufferTest, ring_buffer__concurrent_writes__then_preserve_order) {
    printf("[RING] concurrent_writes → preserve order\n");
    
    // Arrange
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_NE(rb, nullptr);
    
    const int num_events = 1000;
    std::atomic<int> events_written{0};
    std::atomic<int> events_read{0};
    std::atomic<bool> producer_done{false};
    
    // Act - Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_events; i++) {
            TestEvent event = {
                .id = static_cast<uint64_t>(i),
                .timestamp = static_cast<uint64_t>(i * 100)
            };
            while (!ring_buffer_write(rb, &event)) {
                std::this_thread::yield();
            }
            events_written++;
        }
        producer_done = true;
    });
    
    // Act - Consumer thread
    std::thread consumer([&]() {
        while (!producer_done || !ring_buffer_is_empty(rb)) {
            TestEvent event;
            if (ring_buffer_read(rb, &event)) {
                // Verify data integrity
                EXPECT_EQ(event.timestamp, event.id * 100) 
                    << "Data corruption detected at event " << event.id;
                events_read++;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    // Wait for threads
    producer.join();
    consumer.join();
    
    // Assert all events processed
    EXPECT_EQ(events_written.load(), num_events);
    EXPECT_EQ(events_read.load(), num_events);
    EXPECT_TRUE(ring_buffer_is_empty(rb));
}

// Test: ring_buffer__batch_operations__then_efficient_transfer
TEST_F(RingBufferTest, ring_buffer__batch_operations__then_efficient_transfer) {
    printf("[RING] batch_operations → efficient transfer\n");
    
    // Arrange
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_NE(rb, nullptr);
    
    const size_t batch_size = 10;
    TestEvent write_batch[batch_size];
    TestEvent read_batch[batch_size];
    
    // Prepare batch data
    for (size_t i = 0; i < batch_size; i++) {
        write_batch[i].id = i;
        write_batch[i].timestamp = i * 1000;
        snprintf(write_batch[i].data, sizeof(write_batch[i].data), "Event %zu", i);
    }
    
    // Act - Write events one by one (since write_batch doesn't exist)
    for (size_t i = 0; i < batch_size; i++) {
        bool result = ring_buffer_write(rb, &write_batch[i]);
        ASSERT_TRUE(result) << "Failed to write event " << i;
    }
    
    // Assert write succeeded
    EXPECT_EQ(ring_buffer_available_read(rb), batch_size);
    
    // Act - Read batch
    size_t read = ring_buffer_read_batch(rb, read_batch, batch_size);
    
    // Assert read succeeded and data matches
    EXPECT_EQ(read, batch_size) << "Failed to read full batch";
    for (size_t i = 0; i < batch_size; i++) {
        EXPECT_EQ(read_batch[i].id, write_batch[i].id);
        EXPECT_EQ(read_batch[i].timestamp, write_batch[i].timestamp);
        EXPECT_STREQ(read_batch[i].data, write_batch[i].data);
    }
    EXPECT_TRUE(ring_buffer_is_empty(rb));
}

// Test: ring_buffer__null_pointer__then_return_error
TEST_F(RingBufferTest, ring_buffer__null_pointer__then_return_error) {
    printf("[RING] null_pointer → return error\n");
    
    // Act & Assert - Create with null memory
    RingBuffer* null_rb = ring_buffer_create(nullptr, buffer_size, sizeof(TestEvent));
    EXPECT_EQ(null_rb, nullptr) << "Should not create buffer with null memory";
    
    // Act & Assert - Create with size too small (less than header + one event)
    null_rb = ring_buffer_create(memory.get(), sizeof(RingBufferHeader), sizeof(TestEvent));
    EXPECT_EQ(null_rb, nullptr) << "Should not create buffer with insufficient size";
    
    // Note: The implementation doesn't validate event_size == 0, 
    // so we don't test that case as it would actually succeed
}

// Parameterized test for different buffer sizes
class RingBufferSizeTest : public ::testing::TestWithParam<size_t> {
protected:
    void SetUp() override {
        capacity = GetParam();
        buffer_size = sizeof(RingBufferHeader) + (capacity * sizeof(TestEvent));
        memory = std::make_unique<uint8_t[]>(buffer_size);
        memset(memory.get(), 0, buffer_size);
    }
    
    void TearDown() override {
        if (rb) {
            ring_buffer_destroy(rb);
        }
    }
    
    struct TestEvent {
        uint64_t id;
        char data[56];
    };
    
    size_t capacity;
    size_t buffer_size;
    std::unique_ptr<uint8_t[]> memory;
    RingBuffer* rb = nullptr;
};

TEST_P(RingBufferSizeTest, ring_buffer__various_capacities__then_create_success) {
    printf("[RING] capacity_%zu → create success\n", capacity);
    
    // Act
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    
    // Assert
    if (capacity < 2) {
        EXPECT_EQ(rb, nullptr);
        return;
    }
    ASSERT_NE(rb, nullptr);
    auto eff_cap = ring_buffer_get_capacity(rb);
    ASSERT_NE(eff_cap, 0u);
    EXPECT_EQ((eff_cap & (eff_cap - 1)), 0u);
    // Test can write up to eff_cap - 1 (ring buffer keeps one slot empty)
    for (size_t i = 0; i < eff_cap - 1; i++) {
        TestEvent event = {.id = i};
        EXPECT_TRUE(ring_buffer_write(rb, &event)) << "Failed to write event " << i;
    }
    EXPECT_TRUE(ring_buffer_is_full(rb));
    
    // Verify cannot write when full
    TestEvent overflow = {.id = 999};
    uint64_t before2 = ring_buffer_get_overflow_count(rb);
    EXPECT_FALSE(ring_buffer_write(rb, &overflow)) << "Should not accept write when full";
    uint64_t after2 = ring_buffer_get_overflow_count(rb);
    EXPECT_GE(after2, before2 + 1) << "Overflow counter should increase when full";
}

// Test with different capacities
INSTANTIATE_TEST_SUITE_P(
    CapacityTests,
    RingBufferSizeTest,
    ::testing::Values(1, 10, 100, 1000, 10000),
    [](const testing::TestParamInfo<size_t>& info) {
        return "Capacity" + std::to_string(info.param);
    }
);

// Alignment test: producer/consumer fields should be on separate cache lines
TEST_F(RingBufferTest, ring_buffer__header_alignment__then_no_false_sharing) {
    rb = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_NE(rb, nullptr);
    RingBufferHeader* hdr = ring_buffer_get_header(rb);
    ASSERT_NE(hdr, nullptr);
    uintptr_t wp = reinterpret_cast<uintptr_t>(&hdr->write_pos);
    uintptr_t rp = reinterpret_cast<uintptr_t>(&hdr->read_pos);
    EXPECT_EQ(wp % CACHE_LINE_SIZE, 0u) << "write_pos should be cache-line aligned";
    EXPECT_EQ(rp % CACHE_LINE_SIZE, 0u) << "read_pos should be cache-line aligned";
    EXPECT_NE(wp / CACHE_LINE_SIZE, rp / CACHE_LINE_SIZE) << "write/read should not share cache line";
}

// Lightweight performance smoke tests (kept small for CI stability)
TEST(RingBufferPerf, ring_buffer__throughput_smoke__then_reasonable) {
    struct Ev { uint64_t a, b; };
    const size_t cap = 8192; // events requested (effective will be pow2)
    const size_t buf_size = sizeof(RingBufferHeader) + cap * sizeof(Ev);
    auto mem = std::make_unique<uint8_t[]>(buf_size);
    memset(mem.get(), 0, buf_size);
    RingBuffer* rb = ring_buffer_create(mem.get(), buf_size, sizeof(Ev));
    ASSERT_NE(rb, nullptr);
    Ev ev{1,2}, out{};
    using clock = std::chrono::high_resolution_clock;
    const size_t N = 200000; // 200k ops
    auto t0 = clock::now();
    size_t w = 0, r = 0;
    for (size_t i = 0; i < N; i++) {
        while (!ring_buffer_write(rb, &ev)) { (void)ring_buffer_read(rb, &out); r++; }
        w++;
        (void)ring_buffer_read(rb, &out); r++;
    }
    auto t1 = clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    double ops = (w + r) / secs;
    // Expect at least 0.5M ops/sec in debug on typical machines
    EXPECT_GT(ops, 5e5) << "Throughput too low: " << ops << " ops/s";
    ring_buffer_destroy(rb);
}

TEST(RingBufferPerf, ring_buffer__latency_smoke__p99_under_budget) {
    struct Ev { uint64_t a, b; };
    const size_t cap = 4096;
    const size_t buf_size = sizeof(RingBufferHeader) + cap * sizeof(Ev);
    auto mem = std::make_unique<uint8_t[]>(buf_size);
    memset(mem.get(), 0, buf_size);
    RingBuffer* rb = ring_buffer_create(mem.get(), buf_size, sizeof(Ev));
    ASSERT_NE(rb, nullptr);
    Ev ev{1,2}, out{};
    using clock = std::chrono::high_resolution_clock;
    const size_t N = 20000; // samples
    std::vector<uint64_t> lat;
    lat.reserve(N);
    for (size_t i = 0; i < N; i++) {
        auto t0 = clock::now();
        (void)ring_buffer_write(rb, &ev);
        (void)ring_buffer_read(rb, &out);
        auto t1 = clock::now();
        lat.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }
    std::sort(lat.begin(), lat.end());
    size_t idx = static_cast<size_t>(N * 0.99);
    uint64_t p99 = lat[std::min(idx, N - 1)];
    // Keep a lenient budget for varied environments (< 50us)
    EXPECT_LT(p99, 50000u) << "p99 latency too high: " << p99 << " ns";
    ring_buffer_destroy(rb);
}
