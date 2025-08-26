# M1_E2_I1 Test Plan: Drain Thread Implementation

## Test Strategy
Comprehensive testing of the drain thread focusing on lifecycle management, fair scheduling, concurrent operation, and performance under various workloads. All tests emphasize thread safety and SPSC semantics.

## Test Coverage Map

### Coverage Requirements
```yaml
coverage_targets:
  unit_tests: 100%
  integration_tests: 100%
  performance_tests: 100%
  stress_tests: 100%

component_coverage:
  drain_thread.c:
    - lifecycle: 100%
    - state_machine: 100%
    - worker_thread: 100%
    - polling: 100%
    - metrics: 100%
  
  poll_strategy.c:
    - round_robin: 100%
    - fairness: 100%
    - batch_processing: 100%
  
  drain_metrics.c:
    - counter_updates: 100%
    - thread_tracking: 100%
```

## Test Matrix

| Test Category | Test Case | Priority | Coverage Area |
|--------------|-----------|----------|---------------|
| **Lifecycle** | Create/Destroy | P0 | Memory management |
| | Start/Stop | P0 | State transitions |
| | Multiple start attempts | P0 | State machine |
| | Concurrent stop | P0 | Thread safety |
| **Polling** | Empty lanes | P0 | Idle behavior |
| | Single producer | P0 | Basic consumption |
| | Multiple producers | P0 | Fair scheduling |
| | Burst traffic | P1 | Batch processing |
| **Fairness** | Round-robin | P0 | Equal processing |
| | Quantum limits | P0 | Thread switching |
| | Starvation prevention | P0 | All threads served |
| **Performance** | Throughput | P0 | Rings/second |
| | Latency | P0 | Poll cycle time |
| | CPU usage | P1 | Efficiency |
| | Memory usage | P1 | Resource limits |
| **Stress** | Max threads | P0 | Scalability |
| | Continuous load | P0 | Stability |
| | Start/stop cycles | P1 | Leak detection |
| **Error** | Registry failure | P1 | Error recovery |
| | Ring corruption | P1 | Skip and continue |
| | Resource exhaustion | P2 | Graceful degradation |

## Detailed Test Cases

### Unit Tests

#### Lifecycle Management
```c
// Test: drain_thread__create_with_valid_config__then_returns_handle
void test_drain_thread_create_valid() {
    // Arrange
    DrainConfig config = {
        .poll_interval_us = 1000,
        .max_batch_size = 16,
        .fairness_quantum = 32,
        .yield_on_empty = false
    };
    ThreadRegistry* registry = thread_registry_create();
    
    // Act
    DrainThread* dt = drain_thread_create(&config, registry);
    
    // Assert
    ASSERT_NE(dt, nullptr);
    ASSERT_EQ(atomic_load(&dt->state), DRAIN_STATE_INITIALIZED);
    ASSERT_EQ(dt->config->poll_interval_us, 1000);
    
    // Cleanup
    drain_thread_destroy(dt);
    thread_registry_destroy(registry);
}

// Test: drain_thread__start_when_initialized__then_transitions_to_running
void test_drain_thread_start_success() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    
    // Act
    int result = drain_thread_start(dt);
    
    // Assert
    ASSERT_EQ(result, 0);
    ASSERT_EQ(atomic_load(&dt->state), DRAIN_STATE_RUNNING);
    
    // Verify worker thread is running
    usleep(1000);
    ASSERT_GT(atomic_load(&dt->metrics->cycles_completed), 0);
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
}

// Test: drain_thread__stop_when_running__then_drains_and_stops
void test_drain_thread_stop_with_pending_work() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    drain_thread_start(dt);
    
    // Add work to lanes
    for (int i = 0; i < 10; i++) {
        RingBuffer* ring = create_filled_ring(100);
        submit_to_thread(i % 4, ring);
    }
    
    // Act
    int result = drain_thread_stop(dt);
    
    // Assert
    ASSERT_EQ(result, 0);
    ASSERT_EQ(atomic_load(&dt->state), DRAIN_STATE_STOPPED);
    ASSERT_EQ(atomic_load(&dt->metrics->rings_processed), 10);
    
    // Cleanup
    drain_thread_destroy(dt);
}
```

#### State Machine Tests
```c
// Test: drain_thread__start_when_already_running__then_returns_error
void test_drain_thread_double_start() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    drain_thread_start(dt);
    
    // Act
    int result = drain_thread_start(dt);
    
    // Assert
    ASSERT_EQ(result, -EINVAL);
    ASSERT_EQ(atomic_load(&dt->state), DRAIN_STATE_RUNNING);
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
}

// Test: drain_thread__concurrent_stop_calls__then_handled_safely
void test_drain_thread_concurrent_stop() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    drain_thread_start(dt);
    
    pthread_t stoppers[4];
    int results[4];
    
    // Act - Multiple threads try to stop
    for (int i = 0; i < 4; i++) {
        pthread_create(&stoppers[i], NULL, 
                      stop_thread_worker, dt);
    }
    
    for (int i = 0; i < 4; i++) {
        pthread_join(stoppers[i], (void**)&results[i]);
    }
    
    // Assert - Only one should succeed
    int success_count = 0;
    for (int i = 0; i < 4; i++) {
        if (results[i] == 0) success_count++;
    }
    ASSERT_EQ(success_count, 1);
    ASSERT_EQ(atomic_load(&dt->state), DRAIN_STATE_STOPPED);
    
    // Cleanup
    drain_thread_destroy(dt);
}
```

#### Polling Behavior Tests
```c
// Test: drain_worker__empty_lanes__then_sleeps_configured_interval
void test_drain_worker_empty_sleep() {
    // Arrange
    DrainConfig config = {
        .poll_interval_us = 5000,  // 5ms
        .max_batch_size = 16,
        .fairness_quantum = 32,
        .yield_on_empty = false
    };
    DrainThread* dt = drain_thread_create(&config, registry);
    
    // Act
    drain_thread_start(dt);
    usleep(50000);  // Let it run for 50ms
    drain_thread_stop(dt);
    
    // Assert
    uint64_t wait_time = atomic_load(&dt->metrics->total_wait_us);
    uint64_t empty_cycles = atomic_load(&dt->metrics->empty_cycles);
    
    // Should have slept approximately empty_cycles * 5ms
    ASSERT_GT(empty_cycles, 5);
    ASSERT_NEAR(wait_time, empty_cycles * 5000, 1000);
    
    // Cleanup
    drain_thread_destroy(dt);
}

// Test: drain_worker__single_producer__then_consumes_all_rings
void test_drain_worker_single_producer() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    drain_thread_start(dt);
    
    // Act - Submit 100 rings from thread 0
    for (int i = 0; i < 100; i++) {
        RingBuffer* ring = create_filled_ring(10);
        submit_to_thread(0, ring);
    }
    
    // Wait for processing
    wait_for_rings_processed(dt, 100, 1000);
    
    // Assert
    ASSERT_EQ(atomic_load(&dt->metrics->rings_processed), 100);
    ASSERT_EQ(atomic_load(&dt->metrics->rings_per_thread[0]), 100);
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
}
```

#### Fairness Tests
```c
// Test: drain_worker__multiple_producers__then_processes_fairly
void test_drain_worker_fairness() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    drain_thread_start(dt);
    
    // Act - Submit rings from 4 threads
    for (int thread = 0; thread < 4; thread++) {
        for (int i = 0; i < 25; i++) {
            RingBuffer* ring = create_filled_ring(10);
            submit_to_thread(thread, ring);
        }
    }
    
    // Wait for processing
    wait_for_rings_processed(dt, 100, 2000);
    
    // Assert - Each thread should get fair share
    for (int thread = 0; thread < 4; thread++) {
        uint64_t processed = atomic_load(&dt->metrics->rings_per_thread[thread]);
        ASSERT_NEAR(processed, 25, 5);  // Within 20% variance
    }
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
}

// Test: drain_worker__quantum_limit__then_switches_threads
void test_drain_worker_quantum_switching() {
    // Arrange
    DrainConfig config = {
        .poll_interval_us = 1000,
        .max_batch_size = 16,
        .fairness_quantum = 10,  // Small quantum
        .yield_on_empty = false
    };
    DrainThread* dt = drain_thread_create(&config, registry);
    drain_thread_start(dt);
    
    // Act - Thread 0 has 100 rings, Thread 1 has 10
    for (int i = 0; i < 100; i++) {
        submit_to_thread(0, create_filled_ring(10));
    }
    for (int i = 0; i < 10; i++) {
        submit_to_thread(1, create_filled_ring(10));
    }
    
    // Wait for some processing
    usleep(10000);
    
    // Assert - Thread 1 should get service despite thread 0 having more
    uint64_t thread1_processed = atomic_load(&dt->metrics->rings_per_thread[1]);
    ASSERT_GT(thread1_processed, 0);
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
}
```

### Integration Tests

#### Multi-Thread Coordination
```c
// Test: drain_thread__with_active_producers__then_maintains_throughput
void test_drain_with_active_producers() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    drain_thread_start(dt);
    
    pthread_t producers[8];
    ProducerContext contexts[8];
    
    // Start producers
    for (int i = 0; i < 8; i++) {
        contexts[i] = (ProducerContext){
            .thread_id = i,
            .ring_count = 1000,
            .ring_size = 100,
            .delay_us = 100
        };
        pthread_create(&producers[i], NULL, producer_worker, &contexts[i]);
    }
    
    // Let system run
    sleep(2);
    
    // Stop producers
    for (int i = 0; i < 8; i++) {
        pthread_join(producers[i], NULL);
    }
    
    // Wait for drain to catch up
    wait_for_rings_processed(dt, 8000, 5000);
    
    // Assert
    ASSERT_EQ(atomic_load(&dt->metrics->rings_processed), 8000);
    
    // Verify no leaks
    size_t memory_after = get_process_memory();
    ASSERT_LT(memory_after, initial_memory + 1024*1024);  // < 1MB growth
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
}
```

### Performance Tests

#### Throughput Benchmarks
```c
// Test: drain_thread__maximum_throughput__then_meets_target
void test_drain_throughput_benchmark() {
    // Arrange
    DrainConfig config = {
        .poll_interval_us = 0,  // No sleep
        .max_batch_size = 32,
        .fairness_quantum = 128,
        .yield_on_empty = false
    };
    DrainThread* dt = drain_thread_create(&config, registry);
    
    // Pre-fill queues
    for (int i = 0; i < 10000; i++) {
        submit_to_thread(i % 16, create_small_ring());
    }
    
    // Act
    uint64_t start_ns = get_time_ns();
    drain_thread_start(dt);
    
    wait_for_rings_processed(dt, 10000, 5000);
    
    uint64_t duration_ns = get_time_ns() - start_ns;
    drain_thread_stop(dt);
    
    // Assert
    double throughput = 10000.0 / (duration_ns / 1e9);
    printf("Throughput: %.0f rings/sec\n", throughput);
    ASSERT_GT(throughput, 100000);  // > 100K rings/sec
    
    // Cleanup
    drain_thread_destroy(dt);
}

// Test: drain_thread__poll_cycle_latency__then_under_threshold
void test_drain_poll_cycle_latency() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    LatencyTracker tracker;
    
    // Instrument poll cycle
    hook_poll_cycle_timing(dt, &tracker);
    
    // Act
    drain_thread_start(dt);
    
    // Run with moderate load
    for (int i = 0; i < 1000; i++) {
        submit_to_thread(i % 8, create_small_ring());
        usleep(100);
    }
    
    sleep(1);
    drain_thread_stop(dt);
    
    // Assert
    ASSERT_LT(tracker.p50_us, 50);
    ASSERT_LT(tracker.p99_us, 100);
    ASSERT_LT(tracker.max_us, 200);
    
    // Cleanup
    drain_thread_destroy(dt);
}
```

### Stress Tests

#### Maximum Thread Count
```c
// Test: drain_thread__64_threads__then_handles_all
void test_drain_max_threads() {
    // Arrange
    ThreadRegistry* registry = thread_registry_create();
    
    // Register maximum threads
    for (int i = 0; i < 64; i++) {
        thread_registry_register(registry, pthread_self() + i);
    }
    
    DrainThread* dt = drain_thread_create(&default_config, registry);
    drain_thread_start(dt);
    
    // Act - Submit from all threads
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 10; j++) {
            submit_to_thread(i, create_small_ring());
        }
    }
    
    wait_for_rings_processed(dt, 640, 5000);
    
    // Assert
    ASSERT_EQ(atomic_load(&dt->metrics->rings_processed), 640);
    
    // Verify all threads were serviced
    for (int i = 0; i < 64; i++) {
        ASSERT_GT(atomic_load(&dt->metrics->rings_per_thread[i]), 0);
    }
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
    thread_registry_destroy(registry);
}

// Test: drain_thread__continuous_operation__then_no_degradation
void test_drain_continuous_operation() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    drain_thread_start(dt);
    
    uint64_t checkpoints[10];
    
    // Act - Run for extended period
    for (int hour = 0; hour < 10; hour++) {
        // Simulate one hour of operation
        for (int min = 0; min < 60; min++) {
            for (int sec = 0; sec < 60; sec++) {
                // Submit work
                for (int i = 0; i < 10; i++) {
                    submit_to_thread(i % 8, create_small_ring());
                }
                usleep(1000);  // 1ms between batches
            }
        }
        
        // Record metrics
        checkpoints[hour] = atomic_load(&dt->metrics->rings_processed);
    }
    
    // Assert - Verify consistent throughput
    for (int i = 1; i < 10; i++) {
        uint64_t hourly_rate = checkpoints[i] - checkpoints[i-1];
        ASSERT_NEAR(hourly_rate, checkpoints[0], checkpoints[0] * 0.1);  // Within 10%
    }
    
    // Verify no memory leaks
    size_t final_memory = get_process_memory();
    ASSERT_LT(final_memory, initial_memory * 1.1);  // < 10% growth
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
}
```

### Error Handling Tests

```c
// Test: drain_thread__registry_temporarily_unavailable__then_continues
void test_drain_registry_failure_recovery() {
    // Arrange
    DrainThread* dt = create_test_drain_thread();
    drain_thread_start(dt);
    
    // Act - Simulate registry unavailable
    simulate_registry_failure(dt->registry, true);
    usleep(10000);
    
    uint64_t rings_before = atomic_load(&dt->metrics->rings_processed);
    
    // Restore registry
    simulate_registry_failure(dt->registry, false);
    
    // Submit new work
    for (int i = 0; i < 10; i++) {
        submit_to_thread(0, create_small_ring());
    }
    
    wait_for_rings_processed(dt, rings_before + 10, 1000);
    
    // Assert - Should recover and process new work
    ASSERT_EQ(atomic_load(&dt->metrics->rings_processed), rings_before + 10);
    
    // Cleanup
    drain_thread_stop(dt);
    drain_thread_destroy(dt);
}
```

## Acceptance Criteria

### Functional Requirements
- [ ] Drain thread starts and stops cleanly
- [ ] All rings from all threads are consumed
- [ ] Fair scheduling prevents starvation
- [ ] Graceful shutdown drains pending work
- [ ] Metrics accurately track operations

### Performance Requirements
- [ ] Poll cycle < 100μs under load
- [ ] Throughput > 100K rings/second
- [ ] CPU usage < 5% when idle
- [ ] Memory usage bounded and stable

### Reliability Requirements
- [ ] No crashes under stress
- [ ] No memory leaks over time
- [ ] Recovery from transient failures
- [ ] Thread-safe state transitions

## Test Execution Plan

### Phase 1: Unit Tests (Day 1)
1. Lifecycle management tests
2. State machine tests
3. Basic polling tests
4. Metrics validation

### Phase 2: Integration Tests (Day 1-2)
1. Multi-thread coordination
2. Producer-consumer scenarios
3. Fairness validation
4. Shutdown scenarios

### Phase 3: Performance Tests (Day 2)
1. Throughput benchmarks
2. Latency measurements
3. CPU usage profiling
4. Memory usage analysis

### Phase 4: Stress Tests (Day 2)
1. Maximum thread count
2. Continuous operation
3. Rapid start/stop cycles
4. Resource exhaustion

## Success Metrics

| Metric | Target | Actual |
|--------|--------|--------|
| Test Coverage | 100% | - |
| Test Pass Rate | 100% | - |
| Poll Cycle Latency (p99) | < 100μs | - |
| Throughput | > 100K rings/sec | - |
| Memory Growth (24hr) | < 1MB | - |
| CPU Usage (idle) | < 5% | - |