# Test Plan — M1 E1 I2 Thread Registry

## Objective
Validate ThreadRegistry provides lock-free per-thread lane allocation with true SPSC semantics and zero inter-thread contention.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Thread Registration | ✓ | ✓ | ✓ |
| Slot Allocation | ✓ | ✓ | - |
| SPSC Queues | ✓ | ✓ | ✓ |
| Memory Ordering | ✓ | ✓ | - |
| Thread Isolation | ✓ | ✓ | ✓ |

## Test Execution Sequence
1. Unit Tests → 2. Concurrency Tests → 3. Integration Tests → 4. Performance Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| registry__first_thread__then_slot_zero | Single thread | Slot index = 0 | Deterministic allocation |
| registry__concurrent_threads__then_unique_slots | 10 threads | Unique slots 0-9 | No conflicts |
| registry__max_threads__then_rejects | 65 threads | 64 succeed, 1 fails | Graceful limit |
| spsc__producer_consumer__then_ordered | Sequence 1-100 | Same order out | FIFO preserved |

## Test Categories

### 1. Thread Registration Tests

#### Test: `thread_registry__single_registration__then_succeeds`
```c
void test_single_registration() {
    ThreadRegistry* registry = create_registry();
    
    // First thread registers
    ThreadLaneSet* lanes = register_thread(registry);
    
    ASSERT_NOT_NULL(lanes);
    ASSERT_EQ(lanes->slot_index, 0);
    ASSERT_EQ(registry->thread_count, 1);
    ASSERT_TRUE(lanes->active);
}
```

#### Test: `thread_registry__concurrent_registration__then_unique_slots`
```c
void test_concurrent_registration() {
    ThreadRegistry* registry = create_registry();
    ThreadLaneSet* results[10];
    pthread_t threads[10];
    
    // Launch 10 threads simultaneously
    for (int i = 0; i < 10; i++) {
        pthread_create(&threads[i], NULL, register_worker, registry);
    }
    
    // Wait and collect results
    for (int i = 0; i < 10; i++) {
        pthread_join(threads[i], (void**)&results[i]);
    }
    
    // Verify unique slots
    bool slots_used[10] = {false};
    for (int i = 0; i < 10; i++) {
        ASSERT_NOT_NULL(results[i]);
        ASSERT_LT(results[i]->slot_index, 10);
        ASSERT_FALSE(slots_used[results[i]->slot_index]);
        slots_used[results[i]->slot_index] = true;
    }
}
```

#### Test: `thread_registry__max_threads_exceeded__then_returns_null`
```c
void test_max_threads() {
    ThreadRegistry* registry = create_registry();
    
    // Fill all 64 slots
    for (int i = 0; i < MAX_THREADS; i++) {
        ThreadLaneSet* lanes = register_thread(registry);
        ASSERT_NOT_NULL(lanes);
        ASSERT_EQ(lanes->slot_index, i);
    }
    
    // 65th thread should fail
    ThreadLaneSet* overflow = register_thread(registry);
    ASSERT_NULL(overflow);
    ASSERT_EQ(registry->thread_count, MAX_THREADS);
}
```

### 2. SPSC Queue Tests

#### Test: `spsc_queue__single_producer__then_maintains_order`
```c
void test_spsc_order() {
    Lane lane;
    init_lane(&lane, 4);
    
    // Producer enqueues
    for (uint32_t i = 0; i < 100; i++) {
        enqueue_submit(&lane, i);
    }
    
    // Consumer dequeues
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t val = dequeue_submit(&lane);
        ASSERT_EQ(val, i);
    }
}
```

#### Test: `spsc_queue__wraparound__then_correct`
```c
void test_queue_wraparound() {
    Lane lane;
    init_lane(&lane, 4);  // Small queue for wraparound
    
    for (int cycle = 0; cycle < 10; cycle++) {
        // Fill queue
        for (int i = 0; i < 4; i++) {
            enqueue_submit(&lane, cycle * 4 + i);
        }
        
        // Drain queue
        for (int i = 0; i < 4; i++) {
            uint32_t val = dequeue_submit(&lane);
            ASSERT_EQ(val, cycle * 4 + i);
        }
    }
}
```

### 3. Memory Ordering Tests

#### Test: `memory_ordering__registration__then_visible_to_drain`
```c
void test_registration_visibility() {
    ThreadRegistry* registry = create_registry();
    
    // Thread 1: Register
    pthread_t t1;
    pthread_create(&t1, NULL, [](void* r) {
        ThreadLaneSet* lanes = register_thread((ThreadRegistry*)r);
        // Write test pattern
        lanes->events_generated = 0xDEADBEEF;
        atomic_store_explicit(&lanes->active, true, memory_order_release);
        return lanes;
    }, registry);
    
    // Thread 2: Drain should see registration
    pthread_t t2;
    pthread_create(&t2, NULL, [](void* r) {
        ThreadRegistry* reg = (ThreadRegistry*)r;
        // Wait for registration
        while (atomic_load(&reg->thread_count) == 0) {
            usleep(1);
        }
        
        // Should see test pattern
        ThreadLaneSet* lanes = &reg->thread_lanes[0];
        if (atomic_load_explicit(&lanes->active, memory_order_acquire)) {
            ASSERT_EQ(lanes->events_generated, 0xDEADBEEF);
        }
        return NULL;
    }, registry);
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
}
```

### 4. Thread Isolation Tests

#### Test: `isolation__no_false_sharing__then_independent_performance`
```c
void test_cache_line_isolation() {
    ThreadRegistry* registry = create_registry();
    
    // Verify lane sets are cache-line aligned
    for (int i = 0; i < MAX_THREADS - 1; i++) {
        ThreadLaneSet* lane1 = &registry->thread_lanes[i];
        ThreadLaneSet* lane2 = &registry->thread_lanes[i + 1];
        
        size_t distance = (char*)lane2 - (char*)lane1;
        ASSERT_GE(distance, 64);  // At least cache line apart
    }
}
```

### 5. Integration Tests

#### Test: `integration__multi_thread_registration__then_all_visible`
```c
void test_drain_sees_all_threads() {
    ThreadRegistry* registry = create_registry();
    const int NUM_THREADS = 20;
    
    // Launch threads
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, worker_thread, registry);
    }
    
    // Wait for all to register
    while (atomic_load(&registry->thread_count) < NUM_THREADS) {
        usleep(100);
    }
    
    // Drain thread iterates
    int active_count = 0;
    for (int i = 0; i < registry->thread_count; i++) {
        if (atomic_load(&registry->thread_lanes[i].active)) {
            active_count++;
        }
    }
    
    ASSERT_EQ(active_count, NUM_THREADS);
    
    // Cleanup
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
}
```

### 6. Performance Tests

#### Test: `performance__registration_time__then_sub_microsecond`
```c
void test_registration_performance() {
    ThreadRegistry* registry = create_registry();
    
    uint64_t start = get_timestamp_ns();
    ThreadLaneSet* lanes = register_thread(registry);
    uint64_t end = get_timestamp_ns();
    
    ASSERT_NOT_NULL(lanes);
    ASSERT_LT(end - start, 1000);  // < 1μs
}
```

#### Test: `performance__zero_contention__then_linear_scaling`
```c
void test_scaling() {
    for (int thread_count = 1; thread_count <= 32; thread_count *= 2) {
        ThreadRegistry* registry = create_registry();
        
        uint64_t start = get_timestamp_ns();
        
        // Launch threads
        pthread_t* threads = malloc(sizeof(pthread_t) * thread_count);
        for (int i = 0; i < thread_count; i++) {
            pthread_create(&threads[i], NULL, throughput_worker, registry);
        }
        
        // Run for 1 second
        sleep(1);
        
        // Collect results
        uint64_t total_events = 0;
        for (int i = 0; i < thread_count; i++) {
            void* result;
            pthread_join(threads[i], &result);
            total_events += (uint64_t)result;
        }
        
        uint64_t events_per_thread = total_events / thread_count;
        printf("Threads: %d, Events/thread: %lu\n", 
               thread_count, events_per_thread);
        
        // Should maintain constant per-thread throughput
        if (thread_count > 1) {
            ASSERT_GT(events_per_thread, BASELINE_THROUGHPUT * 0.95);
        }
        
        free(threads);
    }
}
```

## Stress Test Scenarios
1. **Registration Storm**: 64 threads register simultaneously
2. **Queue Pressure**: Continuous enqueue/dequeue at max rate
3. **Memory Ordering**: TSan validation under load
4. **Cache Effects**: Measure false sharing impact

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Registration time | < 1μs | Time to allocate slot |
| SPSC throughput | > 10M ops/sec | Per queue pair |
| Inter-thread scaling | Linear | No degradation |
| Memory overhead | < 4KB/thread | Per ThreadLaneSet |

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] Concurrent registration works correctly
- [ ] MAX_THREADS limit enforced
- [ ] SPSC queues maintain order
- [ ] No false sharing detected
- [ ] ThreadSanitizer reports no races
- [ ] Performance targets met
- [ ] Zero contention measured
- [ ] Coverage ≥ 100% on changed lines