# M1_E2_I7_SELECTIVE_PERSISTENCE Test Plan

## Test Strategy Overview

Tests the selective persistence mechanism using TDD approach with focus on:
- Marked event detection accuracy across different patterns and data
- Selective dump decision logic under various ring states  
- Window management and boundary tracking
- Integration with CLI configuration and ring pool swap
- Performance under continuous load and edge cases

## Unit Tests

### 1. Pattern Matching Tests
**Test Class:** `MarkingPolicyTest`

#### Literal Pattern Matching
```c
// Test: literal_pattern__exact_match__then_detected
void test_literal_pattern_exact_match() {
    MarkingPolicy policy = create_literal_policy("ERROR");
    TraceEvent event = {.data = "ERROR: Connection failed"};
    
    bool result = check_marked_event(&event, &policy);
    assert_true(result);
}

// Test: literal_pattern__case_sensitive__then_no_match
void test_literal_pattern_case_sensitive() {
    MarkingPolicy policy = create_literal_policy_case_sensitive("ERROR");
    TraceEvent event = {.data = "error: Connection failed"};
    
    bool result = check_marked_event(&event, &policy);
    assert_false(result);
}

// Test: literal_pattern__case_insensitive__then_matches
void test_literal_pattern_case_insensitive() {
    MarkingPolicy policy = create_literal_policy_case_insensitive("ERROR");
    TraceEvent event = {.data = "error: Connection failed"};
    
    bool result = check_marked_event(&event, &policy);
    assert_true(result);
}
```

#### Regex Pattern Matching
```c
// Test: regex_pattern__valid_regex__then_matches
void test_regex_pattern_valid() {
    MarkingPolicy policy = create_regex_policy("ERROR|FATAL|CRITICAL");
    TraceEvent event = {.data = "CRITICAL: System overload"};
    
    bool result = check_marked_event(&event, &policy);
    assert_true(result);
}

// Test: regex_pattern__invalid_regex__then_falls_back_to_literal
void test_regex_pattern_invalid_fallback() {
    MarkingPolicy policy = create_regex_policy("[invalid_regex");
    TraceEvent event = {.data = "[invalid_regex found"};
    
    bool result = check_marked_event(&event, &policy);
    assert_true(result); // Should match literally
}

// Test: regex_pattern__complex_pattern__then_matches_correctly
void test_regex_pattern_complex() {
    MarkingPolicy policy = create_regex_policy("\\b(error|warning)\\s+\\d+\\b");
    TraceEvent event = {.data = "Found error 404 in response"};
    
    bool result = check_marked_event(&event, &policy);
    assert_true(result);
}
```

#### Multiple Pattern Tests
```c
// Test: multiple_patterns__any_matches__then_detected
void test_multiple_patterns_any_match() {
    const char* patterns[] = {"ERROR", "WARNING", "FATAL"};
    MarkingPolicy policy = create_multi_pattern_policy(patterns, 3);
    TraceEvent event = {.data = "WARNING: Low memory"};
    
    bool result = check_marked_event(&event, &policy);
    assert_true(result);
}

// Test: multiple_patterns__none_match__then_not_detected
void test_multiple_patterns_no_match() {
    const char* patterns[] = {"ERROR", "WARNING", "FATAL"};
    MarkingPolicy policy = create_multi_pattern_policy(patterns, 3);
    TraceEvent event = {.data = "INFO: System starting"};
    
    bool result = check_marked_event(&event, &policy);
    assert_false(result);
}
```

### 2. Selective Dump Logic Tests
**Test Class:** `SelectiveDumpTest`

#### Ring State and Marking Combinations
```c
// Test: ring_not_full__marked_event_seen__then_no_dump
void test_ring_not_full_with_marked_event() {
    DetailLaneControl control = create_test_control_with_marking();
    fill_ring_partially(&control, 0.7); // 70% full
    atomic_store(&control.marked_event_seen_since_last_dump, true);
    
    bool should_dump = should_dump_detail_ring(&control);
    assert_false(should_dump);
}

// Test: ring_full__no_marked_event__then_discard_and_continue
void test_ring_full_no_marked_event() {
    DetailLaneControl control = create_test_control_with_marking();
    fill_ring_completely(&control);
    atomic_store(&control.marked_event_seen_since_last_dump, false);
    
    uint64_t initial_discarded = atomic_load(&control.windows_discarded);
    bool should_dump = should_dump_detail_ring(&control);
    
    assert_false(should_dump);
    assert_equal(initial_discarded + 1, atomic_load(&control.windows_discarded));
}

// Test: ring_full__marked_event_seen__then_dump_triggered
void test_ring_full_with_marked_event() {
    DetailLaneControl control = create_test_control_with_marking();
    fill_ring_completely(&control);
    atomic_store(&control.marked_event_seen_since_last_dump, true);
    
    bool should_dump = should_dump_detail_ring(&control);
    assert_true(should_dump);
}
```

#### State Management Tests
```c
// Test: marked_flag__after_dump__then_reset
void test_marked_flag_reset_after_dump() {
    DetailLaneControl control = create_test_control_with_marking();
    atomic_store(&control.marked_event_seen_since_last_dump, true);
    
    start_new_window(&control, get_current_timestamp());
    
    bool flag = atomic_load(&control.marked_event_seen_since_last_dump);
    assert_false(flag);
}

// Test: concurrent_marking__multiple_threads__then_thread_safe
void test_concurrent_marking_thread_safety() {
    DetailLaneControl control = create_test_control_with_marking();
    const int thread_count = 4;
    const int events_per_thread = 1000;
    
    // Launch multiple threads setting marked flag concurrently
    pthread_t threads[thread_count];
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&threads[i], NULL, mark_events_worker, &control);
    }
    
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Should be true if any thread marked an event
    bool final_state = atomic_load(&control.marked_event_seen_since_last_dump);
    assert_true(final_state);
}
```

### 3. Window Management Tests
**Test Class:** `WindowManagementTest`

#### Window Lifecycle Tests
```c
// Test: new_window__start_timestamp_set__then_tracking_begins
void test_new_window_start() {
    DetailLaneControl control = create_test_control();
    uint64_t start_time = get_current_timestamp();
    
    start_new_window(&control, start_time);
    
    assert_equal(start_time, control.window_start_timestamp);
    assert_false(atomic_load(&control.marked_event_seen_since_last_dump));
}

// Test: close_window__timestamps_updated__then_dump_ready
void test_close_window_for_dump() {
    DetailLaneControl control = create_test_control();
    uint64_t start_time = 1000;
    uint64_t end_time = 2000;
    
    start_new_window(&control, start_time);
    close_window_for_dump(&control, end_time);
    
    assert_equal(start_time, control.window_start_timestamp);
    assert_equal(end_time, control.window_end_timestamp);
    assert_equal(end_time, control.last_dump_timestamp);
}

// Test: overlapping_windows__boundaries_maintained__then_consistent
void test_overlapping_window_boundaries() {
    DetailLaneControl control = create_test_control();
    
    // Start first window
    start_new_window(&control, 1000);
    add_events_to_window(&control, 10);
    close_window_for_dump(&control, 1500);
    
    // Start second window immediately
    start_new_window(&control, 1500);
    add_events_to_window(&control, 15);
    
    // Verify no gap between windows
    assert_equal(control.last_dump_timestamp, control.window_start_timestamp);
}
```

#### Window Metadata Tests
```c
// Test: window_metadata__calculated_correctly__then_accurate
void test_window_metadata_calculation() {
    PersistenceWindow window = create_test_window();
    window.start_timestamp = 1000;
    window.end_timestamp = 2000;
    window.total_events = 150;
    window.marked_events = 3;
    window.marked_timestamp = 1500;
    
    ATFMetadata metadata = create_window_metadata(&window);
    
    assert_equal(ATF_WINDOW_METADATA, metadata.type);
    assert_equal(1000, metadata.window_start);
    assert_equal(2000, metadata.window_end);
    assert_equal(1500, metadata.marked_timestamp);
    assert_equal(150, metadata.event_count);
    assert_equal(3, metadata.marked_count);
}
```

## Integration Tests

### 1. CLI Configuration Integration Tests
**Test Class:** `CLIIntegrationTest`

```c
// Test: cli_config__to_marking_policy__then_patterns_loaded
void test_cli_config_to_marking_policy() {
    CLIConfig config = create_test_cli_config();
    add_trigger_to_config(&config, "ERROR", false, false);
    add_trigger_to_config(&config, "FATAL.*crash", true, false);
    config.case_sensitive_triggers = true;
    
    MarkingPolicy* policy = create_marking_policy_from_config(&config);
    
    assert_equal(2, policy->pattern_count);
    assert_true(policy->case_sensitive);
    assert_false(policy->regex_mode);
    assert_string_equal("ERROR", policy->trigger_patterns[0]);
    assert_string_equal("FATAL.*crash", policy->trigger_patterns[1]);
    
    free_marking_policy(policy);
}

// Test: invalid_cli_config__graceful_handling__then_default_policy
void test_invalid_cli_config_handling() {
    CLIConfig config = create_invalid_cli_config();
    config.trigger_count = 0; // No triggers
    
    MarkingPolicy* policy = create_marking_policy_from_config(&config);
    
    assert_not_null(policy);
    assert_equal(0, policy->pattern_count);
    assert_false(atomic_load(&policy->enabled));
    
    free_marking_policy(policy);
}
```

### 2. Ring Pool Swap Integration Tests
**Test Class:** `RingSwapIntegrationTest`

```c
// Test: selective_swap__conditions_met__then_swap_performed
void test_selective_swap_conditions_met() {
    DetailLaneControl control = create_test_control_with_full_ring();
    atomic_store(&control.marked_event_seen_since_last_dump, true);
    
    SwapResult result = perform_selective_swap(&control);
    
    assert_equal(SWAP_SUCCESS, result);
    assert_equal(1, atomic_load(&control.selective_dumps_performed));
    assert_false(atomic_load(&control.marked_event_seen_since_last_dump));
}

// Test: selective_swap__conditions_not_met__then_swap_skipped
void test_selective_swap_conditions_not_met() {
    DetailLaneControl control = create_test_control_with_full_ring();
    atomic_store(&control.marked_event_seen_since_last_dump, false);
    
    SwapResult result = perform_selective_swap(&control);
    
    assert_equal(SWAP_SKIPPED, result);
    assert_equal(0, atomic_load(&control.selective_dumps_performed));
    assert_equal(1, atomic_load(&control.windows_discarded));
}

// Test: swap_failure__state_consistent__then_retry_possible
void test_swap_failure_state_consistency() {
    DetailLaneControl control = create_test_control_with_failing_swap();
    atomic_store(&control.marked_event_seen_since_last_dump, true);
    
    SwapResult result = perform_selective_swap(&control);
    
    assert_equal(SWAP_FAILED, result);
    // State should remain unchanged for retry
    assert_true(atomic_load(&control.marked_event_seen_since_last_dump));
    assert_equal(0, atomic_load(&control.selective_dumps_performed));
}
```

### 3. ATF Writer Integration Tests
**Test Class:** `ATFIntegrationTest`

```c
// Test: window_metadata__written_to_atf__then_readable
void test_window_metadata_atf_integration() {
    ATFWriter writer = create_test_atf_writer();
    PersistenceWindow window = create_sample_window();
    
    write_window_metadata(&writer, &window);
    
    // Verify metadata was written correctly
    ATFMetadata read_metadata = read_next_metadata(&writer);
    assert_equal(ATF_WINDOW_METADATA, read_metadata.type);
    assert_equal(window.start_timestamp, read_metadata.window_start);
    assert_equal(window.end_timestamp, read_metadata.window_end);
    
    close_atf_writer(&writer);
}

// Test: multiple_windows__sequential_metadata__then_ordered
void test_multiple_windows_atf_ordering() {
    ATFWriter writer = create_test_atf_writer();
    
    PersistenceWindow window1 = create_window(1000, 2000);
    PersistenceWindow window2 = create_window(2000, 3000);
    
    write_window_metadata(&writer, &window1);
    write_window_metadata(&writer, &window2);
    
    // Read back and verify ordering
    ATFMetadata meta1 = read_next_metadata(&writer);
    ATFMetadata meta2 = read_next_metadata(&writer);
    
    assert_true(meta1.window_start < meta2.window_start);
    assert_equal(meta1.window_end, meta2.window_start);
    
    close_atf_writer(&writer);
}
```

## System Tests

### 1. End-to-End Selective Persistence Tests
**Test Class:** `E2ESelectivePersistenceTest`

```c
// Test: full_pipeline__marked_events__then_selective_dumps
void test_full_pipeline_selective_dumps() {
    // Setup complete pipeline
    CLIConfig config = create_test_config_with_triggers();
    DetailLaneControl control = setup_detail_lane_with_config(&config);
    ATFWriter writer = create_output_writer("test_selective.atf");
    
    // Generate mixed event stream
    generate_normal_events(&control, 1000);   // Should not trigger dump
    generate_marked_event(&control, "ERROR: Critical failure");
    generate_normal_events(&control, 500);    // Fill to capacity
    
    // Should trigger selective dump
    process_ring_full_condition(&control);
    
    // Verify selective dump occurred
    assert_equal(1, atomic_load(&control.selective_dumps_performed));
    assert_equal(0, atomic_load(&control.windows_discarded));
    
    // Verify ATF output contains window metadata
    ATFMetadata metadata = read_metadata_from_file("test_selective.atf");
    assert_equal(ATF_WINDOW_METADATA, metadata.type);
    assert_equal(1501, metadata.event_count); // 1000 + 1 + 500
    assert_equal(1, metadata.marked_count);
    
    cleanup_test_pipeline(&control, &writer);
}

// Test: continuous_operation__multiple_windows__then_efficient_storage
void test_continuous_operation_multiple_windows() {
    DetailLaneControl control = setup_detail_lane_for_continuous_test();
    
    for (int cycle = 0; cycle < 10; cycle++) {
        // Generate events until ring is full
        fill_ring_with_mixed_events(&control, cycle % 3 == 0); // Every 3rd has marked event
        
        // Process full condition
        process_ring_full_condition(&control);
    }
    
    // Should have 4 dumps (cycles 0, 3, 6, 9) and 6 discards
    assert_equal(4, atomic_load(&control.selective_dumps_performed));
    assert_equal(6, atomic_load(&control.windows_discarded));
    
    cleanup_continuous_test(&control);
}
```

### 2. Performance Tests
**Test Class:** `PerformanceTest`

```c
// Test: high_throughput__pattern_matching__then_acceptable_overhead
void test_high_throughput_pattern_matching() {
    DetailLaneControl control = setup_high_throughput_test();
    const int event_count = 100000;
    const int marked_frequency = 1000; // Every 1000th event is marked
    
    uint64_t start_time = get_timestamp_ns();
    
    for (int i = 0; i < event_count; i++) {
        TraceEvent event = create_test_event(i);
        if (i % marked_frequency == 0) {
            add_marker_to_event(&event);
        }
        
        process_event_for_marking(&control, &event);
    }
    
    uint64_t end_time = get_timestamp_ns();
    uint64_t duration_ns = end_time - start_time;
    
    // Should process >10K events/ms with marking overhead
    uint64_t events_per_ms = (event_count * 1000000) / duration_ns;
    assert_greater_than(10000, events_per_ms);
    
    // Verify correct marking detection
    assert_equal(event_count / marked_frequency, 
                 atomic_load(&control.marked_events_detected));
    
    cleanup_high_throughput_test(&control);
}

// Test: memory_usage__continuous_operation__then_bounded
void test_memory_usage_bounded() {
    DetailLaneControl control = setup_memory_test();
    size_t initial_memory = get_process_memory_usage();
    
    // Run for extended period
    for (int hour = 0; hour < 24; hour++) {
        simulate_hour_of_operation(&control);
        
        size_t current_memory = get_process_memory_usage();
        size_t memory_growth = current_memory - initial_memory;
        
        // Memory growth should be bounded (< 10MB growth over 24h)
        assert_less_than(10 * 1024 * 1024, memory_growth);
    }
    
    cleanup_memory_test(&control);
}
```

## Test Environment Setup

### Test Data Generation
```c
TraceEvent create_marked_test_event(const char* pattern) {
    TraceEvent event = {
        .timestamp = get_current_timestamp(),
        .thread_id = pthread_self(),
        .data_length = strlen(pattern) + 20,
        .data = malloc(event.data_length)
    };
    snprintf(event.data, event.data_length, "Test event: %s occurred", pattern);
    return event;
}

void generate_realistic_event_stream(DetailLaneControl* control, 
                                   int total_events, 
                                   double marked_ratio) {
    for (int i = 0; i < total_events; i++) {
        TraceEvent event;
        if ((double)rand() / RAND_MAX < marked_ratio) {
            event = create_marked_test_event("ERROR");
        } else {
            event = create_normal_test_event();
        }
        
        process_event_with_selective_persistence(control, &event);
        free(event.data);
    }
}
```

### Test Utilities
```c
void assert_window_boundaries_valid(const PersistenceWindow* window) {
    assert_true(window->start_timestamp <= window->end_timestamp);
    assert_true(window->marked_timestamp >= window->start_timestamp);
    assert_true(window->marked_timestamp <= window->end_timestamp);
    assert_true(window->marked_events <= window->total_events);
}

void verify_selective_persistence_metrics(const DetailLaneControl* control,
                                        uint64_t expected_dumps,
                                        uint64_t expected_discards) {
    assert_equal(expected_dumps, atomic_load(&control->selective_dumps_performed));
    assert_equal(expected_discards, atomic_load(&control->windows_discarded));
}
```

## Test Execution Strategy

### Phase 1: Unit Tests (Day 1)
- Pattern matching functionality
- Selective dump decision logic  
- Window management operations
- Thread safety verification

### Phase 2: Integration Tests (Day 2)
- CLI configuration integration
- Ring pool swap coordination
- ATF writer integration
- Cross-component data flow

### Phase 3: System Tests (Day 3)
- End-to-end selective persistence
- Performance under load
- Memory usage validation
- Long-running stability

### Test Automation
```bash
# Run all selective persistence tests
./test_runner --suite=selective_persistence

# Performance benchmarks
./test_runner --suite=selective_persistence --benchmark --iterations=10

# Memory leak detection
./test_runner --suite=selective_persistence --valgrind --long-running

# Integration test with real CLI
./test_runner --suite=selective_persistence --integration --config=test_triggers.yaml
```

This comprehensive test plan ensures the selective persistence mechanism works correctly under all conditions and integrates properly with the existing ADA architecture.