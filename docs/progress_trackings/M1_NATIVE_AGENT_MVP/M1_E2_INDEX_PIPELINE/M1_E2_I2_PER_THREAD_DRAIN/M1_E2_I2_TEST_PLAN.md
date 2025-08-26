# M1_E2_I2: Per-Thread Drain - Test Plan

## Test Coverage Map

### Component Coverage Requirements
| Component | Unit Tests | Integration Tests | Performance Tests | Coverage Target |
|-----------|------------|------------------|------------------|-----------------|
| DrainIterator | 15 | 8 | 5 | 100% |
| DrainScheduler | 12 | 6 | 4 | 100% |
| ThreadDrainState | 10 | 5 | 3 | 100% |
| DrainMetrics | 8 | 4 | 3 | 100% |
| Submit Queue Coordination | 10 | 8 | 4 | 100% |
| Fairness Algorithm | 8 | 6 | 5 | 100% |
| Error Recovery | 10 | 8 | 2 | 100% |

## Test Matrix

### Functional Test Matrix
| Test Category | Test Cases | Priority | Dependencies |
|--------------|------------|----------|--------------|
| Thread Registration | 8 | P0 | ThreadRegistry |
| Drain Iteration | 12 | P0 | DrainThread |
| Fair Scheduling | 10 | P0 | Scheduler |
| Lane Drainage | 15 | P0 | Lanes |
| Batch Processing | 8 | P1 | Buffer |
| Metrics Collection | 6 | P1 | Metrics |
| Error Handling | 10 | P0 | Recovery |
| Dynamic Thread Management | 8 | P0 | Registry |

### Concurrency Test Matrix
| Scenario | Threads | Events/Thread | Duration | Expected Behavior |
|----------|---------|---------------|----------|------------------|
| Light Load | 4 | 100/s | 60s | Fair drainage, low latency |
| Medium Load | 16 | 1000/s | 60s | Maintained fairness |
| Heavy Load | 64 | 10000/s | 60s | No event loss |
| Burst Load | 32 | 100K burst | 10s | Recovery to steady state |
| Dynamic Threads | 1-64 | 1000/s | 120s | Smooth scaling |

## Unit Tests

### DrainIterator Tests
```c
// Test: Basic iteration with single thread
void test_drain_iterator__single_thread__then_drains_all_events() {
    // Setup
    DrainIterator* iter = drain_iterator_create(test_config());
    ThreadHandle* thread = create_test_thread();
    register_thread(iter, thread);
    
    // Submit events
    submit_test_events(thread, 100);
    
    // Execute
    DrainResult result = drain_iteration(iter);
    
    // Verify
    ASSERT_EQ(result.events_drained, 100);
    ASSERT_EQ(result.threads_processed, 1);
    ASSERT_EQ(get_pending_events(thread), 0);
}

// Test: Fair scheduling across multiple threads
void test_drain_iterator__multiple_threads__then_fair_drainage() {
    // Setup
    DrainIterator* iter = drain_iterator_create(test_config());
    ThreadHandle* threads[4];
    uint32_t events_per_thread = 1000;
    
    for (int i = 0; i < 4; i++) {
        threads[i] = create_test_thread();
        register_thread(iter, threads[i]);
        submit_test_events(threads[i], events_per_thread);
    }
    
    // Execute multiple iterations
    uint32_t drained_per_thread[4] = {0};
    for (int iter_count = 0; iter_count < 10; iter_count++) {
        DrainResult result = drain_iteration(iter);
        
        // Track per-thread drainage
        for (int i = 0; i < 4; i++) {
            drained_per_thread[i] += get_thread_drained_count(iter, i);
        }
    }
    
    // Verify fairness (Jain's index)
    double fairness = calculate_jains_fairness(drained_per_thread, 4);
    ASSERT_GT(fairness, 0.9);
}

// Test: Thread registration during drainage
void test_drain_iterator__register_during_drain__then_included_next_iteration() {
    // Setup
    DrainIterator* iter = drain_iterator_create(test_config());
    ThreadHandle* thread1 = create_test_thread();
    register_thread(iter, thread1);
    submit_test_events(thread1, 100);
    
    // Start drain in background
    std::thread drain_thread([iter]() {
        drain_iteration(iter);
    });
    
    // Register new thread during drain
    ThreadHandle* thread2 = create_test_thread();
    register_thread(iter, thread2);
    submit_test_events(thread2, 50);
    
    drain_thread.join();
    
    // Second iteration should include new thread
    DrainResult result = drain_iteration(iter);
    
    // Verify
    ASSERT_EQ(result.threads_processed, 2);
    ASSERT_EQ(get_pending_events(thread2), 0);
}

// Test: Handle thread deregistration
void test_drain_iterator__thread_deregisters__then_final_drain_complete() {
    // Setup
    DrainIterator* iter = drain_iterator_create(test_config());
    ThreadHandle* thread = create_test_thread();
    register_thread(iter, thread);
    submit_test_events(thread, 100);
    
    // Mark thread for deregistration
    begin_thread_deregistration(thread);
    
    // Execute drain
    DrainResult result = drain_iteration(iter);
    
    // Verify
    ASSERT_EQ(result.events_drained, 100);
    ASSERT_TRUE(is_thread_fully_drained(iter, thread));
    ASSERT_TRUE(can_safely_deregister(thread));
}
```

### DrainScheduler Tests
```c
// Test: Round-robin scheduling
void test_scheduler__round_robin__then_equal_selection() {
    // Setup
    DrainScheduler* sched = create_scheduler(SCHED_ROUND_ROBIN);
    ThreadDrainState states[4];
    init_thread_states(states, 4);
    
    // All threads have pending work
    for (int i = 0; i < 4; i++) {
        atomic_store(&states[i].index_pending, 100);
    }
    
    // Execute selections
    uint32_t selections[4] = {0};
    for (int i = 0; i < 100; i++) {
        uint32_t selected = select_next_thread_fair(sched, states, 4);
        selections[selected]++;
    }
    
    // Verify equal distribution
    for (int i = 0; i < 4; i++) {
        ASSERT_NEAR(selections[i], 25, 2);
    }
}

// Test: Priority-based scheduling
void test_scheduler__priority_based__then_high_priority_first() {
    // Setup
    DrainScheduler* sched = create_scheduler(SCHED_PRIORITY_BASED);
    ThreadDrainState states[3];
    init_thread_states(states, 3);
    
    // Set different priorities
    atomic_store(&states[0].priority, 1);   // Low
    atomic_store(&states[1].priority, 10);  // High
    atomic_store(&states[2].priority, 5);   // Medium
    
    // All have work
    for (int i = 0; i < 3; i++) {
        atomic_store(&states[i].index_pending, 50);
    }
    
    // Execute
    uint32_t first = select_next_thread_fair(sched, states, 3);
    uint32_t second = select_next_thread_fair(sched, states, 3);
    uint32_t third = select_next_thread_fair(sched, states, 3);
    
    // Verify priority order
    ASSERT_EQ(first, 1);   // High priority
    ASSERT_EQ(second, 2);  // Medium priority
    ASSERT_EQ(third, 0);   // Low priority
}

// Test: Adaptive scheduling under load
void test_scheduler__adaptive__then_adjusts_to_load() {
    // Setup
    DrainScheduler* sched = create_scheduler(SCHED_ADAPTIVE);
    ThreadDrainState states[4];
    init_thread_states(states, 4);
    
    // Simulate different load patterns
    atomic_store(&states[0].index_pending, 10000);   // Heavy
    atomic_store(&states[1].index_pending, 100);     // Light
    atomic_store(&states[2].index_pending, 5000);    // Medium
    atomic_store(&states[3].index_pending, 0);       // Empty
    
    // Execute multiple selections
    uint32_t selections[4] = {0};
    for (int i = 0; i < 100; i++) {
        uint32_t selected = select_next_thread_fair(sched, states, 4);
        if (selected != INVALID_THREAD_ID) {
            selections[selected]++;
            // Simulate draining some events
            uint32_t current = atomic_load(&states[selected].index_pending);
            atomic_store(&states[selected].index_pending, current * 0.9);
        }
    }
    
    // Verify adaptive behavior (more selections for heavy load)
    ASSERT_GT(selections[0], selections[1]);  // Heavy > Light
    ASSERT_GT(selections[2], selections[1]);  // Medium > Light
    ASSERT_EQ(selections[3], 0);             // Empty never selected
}
```

### Submit Queue Coordination Tests
```c
// Test: Non-blocking drain acquisition
void test_submit_queue__try_acquire__then_non_blocking() {
    // Setup
    SubmitQueue* queue = create_submit_queue();
    
    // Producer acquires first
    ASSERT_TRUE(submit_queue_acquire_producer(queue));
    
    // Drain should fail (non-blocking)
    ASSERT_FALSE(submit_queue_try_acquire_drain(queue));
    
    // Release producer
    submit_queue_release_producer(queue);
    
    // Now drain should succeed
    ASSERT_TRUE(submit_queue_try_acquire_drain(queue));
    submit_queue_release_drain(queue);
}

// Test: Pending event notification
void test_submit_queue__notify_pending__then_wakes_drain() {
    // Setup
    SubmitQueue* queue = create_submit_queue();
    std::atomic<bool> drain_woken(false);
    
    // Start drain wait in background
    std::thread waiter([queue, &drain_woken]() {
        uint32_t pending = drain_wait_for_events(queue, 1000);
        if (pending > 0) {
            drain_woken = true;
        }
    });
    
    // Give waiter time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Notify pending events
    submit_queue_notify_pending(queue, 10);
    
    waiter.join();
    
    // Verify wake occurred
    ASSERT_TRUE(drain_woken);
}
```

## Integration Tests

### Multi-Thread Drain Integration
```c
void test_integration__concurrent_producers_single_drain__then_no_loss() {
    // Setup
    const int PRODUCER_COUNT = 8;
    const int EVENTS_PER_PRODUCER = 10000;
    
    SystemFixture* fixture = create_system_fixture();
    DrainIterator* iter = fixture->drain_iterator;
    
    std::atomic<uint64_t> total_submitted(0);
    std::atomic<uint64_t> total_drained(0);
    
    // Start producers
    std::vector<std::thread> producers;
    for (int i = 0; i < PRODUCER_COUNT; i++) {
        producers.emplace_back([&, i]() {
            ThreadHandle* thread = register_producer_thread(fixture, i);
            
            for (int j = 0; j < EVENTS_PER_PRODUCER; j++) {
                submit_index_event(thread, create_test_event(i, j));
                total_submitted++;
                
                // Vary submission rate
                if (j % 100 == 0) {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(rand() % 100));
                }
            }
            
            deregister_producer_thread(thread);
        });
    }
    
    // Start drain thread
    std::thread drainer([&]() {
        while (total_drained < PRODUCER_COUNT * EVENTS_PER_PRODUCER) {
            DrainResult result = drain_iteration(iter);
            total_drained += result.events_drained;
            
            // Small delay between iterations
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    // Wait for completion
    for (auto& p : producers) {
        p.join();
    }
    drainer.join();
    
    // Verify
    ASSERT_EQ(total_submitted.load(), total_drained.load());
    ASSERT_EQ(total_drained.load(), PRODUCER_COUNT * EVENTS_PER_PRODUCER);
}
```

### Dynamic Thread Management
```c
void test_integration__threads_join_and_leave__then_all_events_captured() {
    // Setup
    SystemFixture* fixture = create_system_fixture();
    DrainIterator* iter = fixture->drain_iterator;
    
    std::atomic<uint64_t> events_submitted(0);
    std::atomic<uint64_t> events_drained(0);
    std::atomic<bool> stop_drain(false);
    
    // Drain thread
    std::thread drainer([&]() {
        while (!stop_drain) {
            DrainResult result = drain_iteration(iter);
            events_drained += result.events_drained;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        
        // Final drain
        DrainResult final_result = drain_iteration(iter);
        events_drained += final_result.events_drained;
    });
    
    // Dynamic thread creation/destruction
    for (int wave = 0; wave < 5; wave++) {
        std::vector<std::thread> wave_threads;
        
        // Create threads for this wave
        int thread_count = 4 + (wave * 2);  // Increasing thread count
        for (int i = 0; i < thread_count; i++) {
            wave_threads.emplace_back([&, wave, i]() {
                ThreadHandle* thread = register_producer_thread(fixture, 
                                                               wave * 100 + i);
                
                // Submit events
                int event_count = 1000 * (wave + 1);  // Increasing load
                for (int j = 0; j < event_count; j++) {
                    submit_index_event(thread, create_test_event(wave, j));
                    events_submitted++;
                }
                
                // Deregister
                deregister_producer_thread(thread);
            });
        }
        
        // Wait for wave completion
        for (auto& t : wave_threads) {
            t.join();
        }
        
        // Brief pause between waves
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Stop drain thread
    stop_drain = true;
    drainer.join();
    
    // Verify all events captured
    ASSERT_EQ(events_submitted.load(), events_drained.load());
}
```

### Fairness Validation
```c
void test_integration__uneven_load__then_maintains_fairness() {
    // Setup
    SystemFixture* fixture = create_system_fixture();
    DrainIterator* iter = fixture->drain_iterator;
    
    const int THREAD_COUNT = 8;
    std::atomic<uint64_t> events_per_thread[THREAD_COUNT];
    std::atomic<uint64_t> drained_per_thread[THREAD_COUNT];
    
    for (int i = 0; i < THREAD_COUNT; i++) {
        events_per_thread[i] = 0;
        drained_per_thread[i] = 0;
    }
    
    // Start threads with different loads
    std::vector<std::thread> producers;
    for (int i = 0; i < THREAD_COUNT; i++) {
        producers.emplace_back([&, i]() {
            ThreadHandle* thread = register_producer_thread(fixture, i);
            
            // Exponentially different loads
            int event_count = 100 * (1 << i);  // 100, 200, 400, ..., 12800
            
            for (int j = 0; j < event_count; j++) {
                IndexEvent* event = create_test_event(i, j);
                event->thread_id = i;  // Tag with thread ID
                submit_index_event(thread, event);
                events_per_thread[i]++;
            }
        });
    }
    
    // Drain with fairness tracking
    std::thread drainer([&]() {
        for (int iter_num = 0; iter_num < 100; iter_num++) {
            DrainResult result = drain_iteration_with_tracking(iter);
            
            // Track per-thread drainage
            for (int i = 0; i < THREAD_COUNT; i++) {
                drained_per_thread[i] += get_thread_drain_count(iter, i);
            }
        }
    });
    
    // Wait for producers
    for (auto& p : producers) {
        p.join();
    }
    
    drainer.join();
    
    // Calculate fairness metrics
    double drain_rates[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        drain_rates[i] = (double)drained_per_thread[i] / events_per_thread[i];
    }
    
    double fairness_index = calculate_jains_fairness(drain_rates, THREAD_COUNT);
    
    // Verify fairness maintained despite uneven load
    ASSERT_GT(fairness_index, 0.85);  // Good fairness despite 128x load difference
}
```

## Performance Tests

### Throughput Benchmark
```c
void benchmark_drain_throughput() {
    const int THREAD_COUNT = 64;
    const int EVENTS_PER_THREAD = 100000;
    const int WARMUP_EVENTS = 10000;
    
    SystemFixture* fixture = create_system_fixture();
    DrainIterator* iter = fixture->drain_iterator;
    
    // Setup threads
    ThreadHandle* threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        threads[i] = register_producer_thread(fixture, i);
    }
    
    // Warmup
    for (int i = 0; i < THREAD_COUNT; i++) {
        for (int j = 0; j < WARMUP_EVENTS; j++) {
            submit_index_event(threads[i], create_test_event(i, j));
        }
    }
    
    // Drain warmup events
    while (get_total_pending(iter) > 0) {
        drain_iteration(iter);
    }
    
    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();
    
    // Submit all events
    for (int i = 0; i < THREAD_COUNT; i++) {
        for (int j = 0; j < EVENTS_PER_THREAD; j++) {
            submit_index_event(threads[i], create_test_event(i, j));
        }
    }
    
    // Drain all events
    uint64_t total_drained = 0;
    while (total_drained < THREAD_COUNT * EVENTS_PER_THREAD) {
        DrainResult result = drain_iteration(iter);
        total_drained += result.events_drained;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();
    
    // Calculate throughput
    double events_per_sec = (double)(THREAD_COUNT * EVENTS_PER_THREAD) / 
                           (duration / 1000.0);
    
    // Report
    printf("Throughput: %.2f M events/sec\n", events_per_sec / 1000000);
    printf("Duration: %ld ms\n", duration);
    printf("Total events: %d\n", THREAD_COUNT * EVENTS_PER_THREAD);
    
    // Verify performance target
    ASSERT_GT(events_per_sec, 1000000);  // > 1M events/sec
}
```

### Latency Benchmark
```c
void benchmark_drain_latency() {
    SystemFixture* fixture = create_system_fixture();
    DrainIterator* iter = fixture->drain_iterator;
    
    const int ITERATIONS = 10000;
    std::vector<uint64_t> latencies;
    latencies.reserve(ITERATIONS);
    
    // Single thread for latency measurement
    ThreadHandle* thread = register_producer_thread(fixture, 0);
    
    for (int i = 0; i < ITERATIONS; i++) {
        // Submit single event
        auto submit_time = std::chrono::high_resolution_clock::now();
        IndexEvent* event = create_timestamped_event(i);
        submit_index_event(thread, event);
        
        // Drain immediately
        DrainResult result = drain_iteration(iter);
        auto drain_time = std::chrono::high_resolution_clock::now();
        
        if (result.events_drained > 0) {
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                drain_time - submit_time).count();
            latencies.push_back(latency);
        }
    }
    
    // Calculate percentiles
    std::sort(latencies.begin(), latencies.end());
    
    uint64_t p50 = latencies[latencies.size() * 0.50];
    uint64_t p90 = latencies[latencies.size() * 0.90];
    uint64_t p99 = latencies[latencies.size() * 0.99];
    uint64_t p999 = latencies[latencies.size() * 0.999];
    
    // Report
    printf("Drain Latency:\n");
    printf("  P50:  %lu µs\n", p50);
    printf("  P90:  %lu µs\n", p90);
    printf("  P99:  %lu µs\n", p99);
    printf("  P99.9: %lu µs\n", p999);
    
    // Verify latency targets
    ASSERT_LT(p99, 10000);  // P99 < 10ms
    ASSERT_LT(p50, 1000);   // P50 < 1ms
}
```

### CPU Usage Benchmark
```c
void benchmark_cpu_usage() {
    SystemFixture* fixture = create_system_fixture();
    DrainIterator* iter = fixture->drain_iterator;
    
    // Setup steady-state load
    const int THREAD_COUNT = 16;
    const int EVENTS_PER_SECOND = 10000;
    
    std::atomic<bool> stop(false);
    std::vector<std::thread> producers;
    
    // Start producers with steady rate
    for (int i = 0; i < THREAD_COUNT; i++) {
        producers.emplace_back([&, i]() {
            ThreadHandle* thread = register_producer_thread(fixture, i);
            
            auto next_submit = std::chrono::steady_clock::now();
            int events_per_thread = EVENTS_PER_SECOND / THREAD_COUNT;
            auto submit_interval = std::chrono::microseconds(
                1000000 / events_per_thread);
            
            while (!stop) {
                submit_index_event(thread, create_test_event(i, 0));
                
                next_submit += submit_interval;
                std::this_thread::sleep_until(next_submit);
            }
        });
    }
    
    // Start drain thread with CPU monitoring
    std::thread drainer([&]() {
        CPUMonitor monitor;
        monitor.start();
        
        while (!stop) {
            drain_iteration(iter);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        monitor.stop();
        double cpu_usage = monitor.get_average_usage();
        
        printf("Drain thread CPU usage: %.2f%%\n", cpu_usage);
        
        // Verify CPU target
        ASSERT_LT(cpu_usage, 5.0);  // < 5% CPU in steady state
    });
    
    // Run for measurement period
    std::this_thread::sleep_for(std::chrono::seconds(30));
    
    stop = true;
    
    // Cleanup
    for (auto& p : producers) {
        p.join();
    }
    drainer.join();
}
```

## Stress Tests

### Thread Churn Stress Test
```c
void stress_test__high_thread_churn__then_stable_operation() {
    SystemFixture* fixture = create_system_fixture();
    DrainIterator* iter = fixture->drain_iterator;
    
    std::atomic<bool> stop(false);
    std::atomic<uint64_t> total_submitted(0);
    std::atomic<uint64_t> total_drained(0);
    std::atomic<uint32_t> active_threads(0);
    
    // Drain thread
    std::thread drainer([&]() {
        while (!stop) {
            DrainResult result = drain_iteration(iter);
            total_drained += result.events_drained;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    // Thread churn generator
    std::thread churner([&]() {
        uint32_t thread_id = 0;
        
        while (!stop) {
            int thread_count = rand() % 32 + 1;  // 1-32 threads
            std::vector<std::thread> threads;
            
            for (int i = 0; i < thread_count; i++) {
                threads.emplace_back([&, id = thread_id++]() {
                    active_threads++;
                    ThreadHandle* thread = register_producer_thread(fixture, id);
                    
                    // Submit random number of events
                    int event_count = rand() % 1000 + 100;
                    for (int j = 0; j < event_count; j++) {
                        submit_index_event(thread, create_test_event(id, j));
                        total_submitted++;
                    }
                    
                    // Random lifetime
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(rand() % 100));
                    
                    deregister_producer_thread(thread);
                    active_threads--;
                });
            }
            
            // Let threads run
            std::this_thread::sleep_for(
                std::chrono::milliseconds(rand() % 50));
            
            // Join random subset
            for (auto& t : threads) {
                if (t.joinable()) {
                    t.join();
                }
            }
        }
    });
    
    // Run stress test
    std::this_thread::sleep_for(std::chrono::seconds(60));
    stop = true;
    
    churner.join();
    
    // Wait for final drainage
    while (active_threads > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Final drain
    DrainResult final_result = drain_iteration(iter);
    total_drained += final_result.events_drained;
    
    drainer.join();
    
    // Verify no event loss despite churn
    ASSERT_EQ(total_submitted.load(), total_drained.load());
}
```

## Acceptance Criteria

### Functional Acceptance
- [ ] All registered threads are drained fairly (Jain's index > 0.9)
- [ ] Zero events lost during normal operation
- [ ] Thread registration/deregistration handled correctly
- [ ] Index lanes always drained when events present
- [ ] Detail lanes only drained when marked
- [ ] Metrics accurately track drainage statistics

### Performance Acceptance
- [ ] Throughput > 1M events/second with 64 threads
- [ ] P99 drain latency < 10ms
- [ ] CPU usage < 5% in steady state
- [ ] Memory bounded regardless of thread count
- [ ] Fair drainage maintained under uneven load

### Reliability Acceptance
- [ ] Stable operation with high thread churn
- [ ] Recovery from drain failures
- [ ] No deadlocks or race conditions
- [ ] Graceful handling of thread death
- [ ] Clean shutdown with final drain

### Integration Acceptance
- [ ] Integrates with ThreadRegistry callbacks
- [ ] Coordinates with submit queues correctly
- [ ] Persistence layer handles batches properly
- [ ] Metrics exported for monitoring
- [ ] Configuration via runtime parameters