# M1_E3_I2 Backlogs: Per-Thread Metrics Collection

## Sprint Overview
**Duration**: 4 days (32 hours)  
**Dependencies**: M1_E1_I2 (ThreadRegistry), M1_E3_I1 (Backpressure)  
**Goal**: Implement comprehensive per-thread metrics with zero contention

## Task Breakdown

### Day 1: Core Metrics Infrastructure (8 hours)

#### Task 1.1: Data Structure Definition (2 hours)
**Priority**: P0  
**Type**: Implementation  
**Description**: Define metrics structures with proper cache alignment
- [ ] Create `ada_thread_metrics_t` structure
- [ ] Define counter fields with 64B alignment
- [ ] Add rate calculation fields
- [ ] Implement swap tracking fields
- [ ] Add pressure indicator fields
**Acceptance**: Compiles, proper alignment verified
**Estimate**: 2 hours

#### Task 1.2: Counter Operations (2 hours)
**Priority**: P0  
**Type**: Implementation  
**Description**: Implement atomic counter operations
- [ ] Implement `metrics_increment_written()`
- [ ] Implement `metrics_increment_dropped()`
- [ ] Add byte counter updates
- [ ] Ensure relaxed memory ordering
- [ ] Create inline functions for hot path
**Acceptance**: Unit tests pass, <5ns overhead
**Estimate**: 2 hours

#### Task 1.3: Rate Calculation Logic (2 hours)
**Priority**: P0  
**Type**: Implementation  
**Description**: Implement rate calculation with window sliding
- [ ] Create `metrics_update_rate()` function
- [ ] Implement 100ms update window
- [ ] Add compare-exchange coordination
- [ ] Calculate events per second
- [ ] Calculate bytes per second
**Acceptance**: Rate accuracy ±1%
**Estimate**: 2 hours

#### Task 1.4: Unit Tests - Counters (2 hours)
**Priority**: P0  
**Type**: Testing  
**Description**: Write unit tests for counter operations
- [ ] Test `metrics_counter__increment__then_increases`
- [ ] Test `metrics_counter__concurrent_updates__then_accurate_total`
- [ ] Test atomic operation correctness
- [ ] Verify no torn reads
**Acceptance**: 100% coverage, all tests pass
**Estimate**: 2 hours

### Day 2: Thread Integration (8 hours)

#### Task 2.1: ThreadRegistry Integration (2 hours)
**Priority**: P0  
**Type**: Integration  
**Description**: Integrate metrics with ThreadRegistry
- [ ] Add metrics field to `ada_thread_context_t`
- [ ] Allocate metrics in `thread_register()`
- [ ] Initialize metrics on thread creation
- [ ] Clean up metrics on thread destruction
**Acceptance**: Metrics properly lifecycle managed
**Estimate**: 2 hours

#### Task 2.2: Event Path Integration (2 hours)
**Priority**: P0  
**Type**: Integration  
**Description**: Add metrics to event write path
- [ ] Update `ada_write_event()` with metrics
- [ ] Track successful writes
- [ ] Track dropped events
- [ ] Update byte counters
- [ ] Add backpressure tracking
**Acceptance**: All events tracked accurately
**Estimate**: 2 hours

#### Task 2.3: Swap Tracking Implementation (2 hours)
**Priority**: P1  
**Type**: Implementation  
**Description**: Implement ring swap metrics
- [ ] Create `metrics_swap_begin()` function
- [ ] Create `metrics_swap_end()` function
- [ ] Track swap count
- [ ] Measure swap duration
- [ ] Calculate average swap time
**Acceptance**: Swap metrics accurate
**Estimate**: 2 hours

#### Task 2.4: Integration Tests (2 hours)
**Priority**: P0  
**Type**: Testing  
**Description**: Write integration tests
- [ ] Test `write_event__success__then_metrics_updated`
- [ ] Test `write_event__dropped__then_drop_count_increases`
- [ ] Test `thread_registry__with_metrics__then_initialized`
- [ ] Test backpressure metric tracking
**Acceptance**: 100% coverage, thread safety verified
**Estimate**: 2 hours

### Day 3: Global Aggregation (8 hours)

#### Task 3.1: Global Metrics Structure (2 hours)
**Priority**: P0  
**Type**: Implementation  
**Description**: Implement global aggregation structure
- [ ] Create `ada_global_metrics_t` structure
- [ ] Define snapshot array
- [ ] Add collection control fields
- [ ] Implement initialization function
- [ ] Add cleanup function
**Acceptance**: Structure properly initialized
**Estimate**: 2 hours

#### Task 3.2: Collection Logic (3 hours)
**Priority**: P0  
**Type**: Implementation  
**Description**: Implement periodic collection
- [ ] Create `metrics_collect_global()` function
- [ ] Implement collection interval checking
- [ ] Add single-collector pattern
- [ ] Aggregate per-thread metrics
- [ ] Calculate system-wide rates
**Acceptance**: Collection works correctly
**Estimate**: 3 hours

#### Task 3.3: Snapshot Creation (1 hour)
**Priority**: P0  
**Type**: Implementation  
**Description**: Implement snapshot mechanism
- [ ] Create snapshot structure
- [ ] Implement atomic snapshot creation
- [ ] Add point-in-time consistency
- [ ] Calculate derived metrics (drop rate, etc.)
**Acceptance**: Snapshots consistent
**Estimate**: 1 hour

#### Task 3.4: Aggregation Tests (2 hours)
**Priority**: P0  
**Type**: Testing  
**Description**: Test global aggregation
- [ ] Test `global_metrics__collect__then_sums_all_threads`
- [ ] Test `global_metrics__snapshot__then_point_in_time`
- [ ] Test collection interval enforcement
- [ ] Test multi-thread aggregation
**Acceptance**: Aggregation accurate
**Estimate**: 2 hours

### Day 4: Performance & Polish (8 hours)

#### Task 4.1: Performance Optimization (2 hours)
**Priority**: P0  
**Type**: Optimization  
**Description**: Optimize hot paths
- [ ] Inline critical functions
- [ ] Verify cache alignment
- [ ] Optimize memory ordering
- [ ] Reduce branching in hot path
- [ ] Profile and measure overhead
**Acceptance**: <5ns per event overhead
**Estimate**: 2 hours

#### Task 4.2: Performance Tests (2 hours)
**Priority**: P0  
**Type**: Testing  
**Description**: Write performance benchmarks
- [ ] Test `event_write__with_metrics__then_under_5ns`
- [ ] Test `aggregation__64_threads__then_under_100us`
- [ ] Measure memory overhead
- [ ] Test cache efficiency
**Acceptance**: All performance targets met
**Estimate**: 2 hours

#### Task 4.3: Stress Testing (2 hours)
**Priority**: P1  
**Type**: Testing  
**Description**: Stress test implementation
- [ ] Test `events_1m_per_sec__sustained__then_accurate`
- [ ] Test counter overflow handling
- [ ] Test rapid thread churn
- [ ] Test collection contention
**Acceptance**: System stable under load
**Estimate**: 2 hours

#### Task 4.4: Documentation & Cleanup (2 hours)
**Priority**: P1  
**Type**: Documentation  
**Description**: Finalize documentation and code
- [ ] Update API documentation
- [ ] Add usage examples
- [ ] Clean up TODO items
- [ ] Run static analysis
- [ ] Verify 100% coverage
**Acceptance**: Code production ready
**Estimate**: 2 hours

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Cache line conflicts | High | Medium | Careful alignment, padding |
| Collection contention | Medium | Low | Single collector pattern |
| Counter overflow | Low | Low | Saturating arithmetic |
| Memory ordering bugs | High | Medium | Explicit ordering, TSan |
| Performance regression | High | Medium | Continuous benchmarking |

## Dependencies

### Required Before Start
- M1_E1_I2: ThreadRegistry with per-thread contexts
- M1_E3_I1: Backpressure counter infrastructure

### Provides for Next
- M1_E3_I3: Runtime monitoring capabilities
- M1_E4_I2: Performance analysis data

## Success Metrics

### Performance Targets
- Per-event overhead: < 5ns
- Collection latency: < 100μs for 64 threads
- Memory overhead: < 1KB per thread
- Cache efficiency: Zero false sharing

### Quality Targets
- Code coverage: 100% on new code
- Test suite: All tests passing
- Static analysis: Zero warnings
- Thread sanitizer: Clean run

## Technical Debt

### Items to Address
1. Consider SIMD for aggregation optimization
2. Add histogram support for latency distribution
3. Implement metric export formats (Prometheus, etc.)
4. Add runtime configuration for collection interval

### Future Enhancements
1. Percentile tracking for latencies
2. Moving average calculations
3. Anomaly detection algorithms
4. Metric persistence for crash analysis

## Review Checklist

### Code Review
- [ ] All atomics have explicit memory ordering
- [ ] Cache alignment verified
- [ ] No false sharing between threads
- [ ] Hot path optimized
- [ ] Error handling complete

### Testing Review
- [ ] Unit test coverage 100%
- [ ] Integration tests complete
- [ ] Performance benchmarks pass
- [ ] Stress tests stable
- [ ] Thread sanitizer clean

### Documentation Review
- [ ] API documentation complete
- [ ] Usage examples provided
- [ ] Architecture diagrams updated
- [ ] Performance characteristics documented

## Sprint Retrospective

### What Went Well
- (To be filled after sprint)

### What Could Be Improved
- (To be filled after sprint)

### Action Items
- (To be filled after sprint)

## Notes

### Implementation Priority
1. Core counter operations (critical path)
2. Thread integration (enables testing)
3. Global aggregation (monitoring capability)
4. Performance optimization (production readiness)

### Key Design Decisions
- Relaxed memory ordering for counters (performance)
- Cache-line alignment (prevent false sharing)
- Single collector pattern (avoid contention)
- 100ms rate calculation window (balance accuracy/overhead)

### Testing Focus
- Atomicity of operations
- Thread isolation verification
- Performance overhead validation
- Long-running stability