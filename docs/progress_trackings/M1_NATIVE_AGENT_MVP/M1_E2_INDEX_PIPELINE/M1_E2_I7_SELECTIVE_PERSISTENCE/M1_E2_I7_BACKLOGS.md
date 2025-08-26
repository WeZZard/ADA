# M1_E2_I7_SELECTIVE_PERSISTENCE Backlogs

## Iteration Overview

**Duration:** 3 days  
**Focus:** Implement selective persistence mechanism for detail lane with "always capture, selectively persist" architecture  
**Goal:** Enable true pre-roll capability by only persisting trace segments containing marked events

## Day 1: Pattern Matching and Detection (TDD Foundation)

### Morning (4 hours)
**Objective:** Implement marked event detection with comprehensive test coverage

#### Tasks:
1. **Create MarkingPolicy structure and initialization** (1h)
   - Define MarkingPolicy struct with trigger patterns and runtime state
   - Implement policy creation from CLI configuration
   - Add policy memory management functions
   - **Test:** `test_marking_policy_creation_and_cleanup`

2. **Implement literal pattern matching** (1.5h)
   - Create `check_marked_event()` function for literal string matching
   - Support case-sensitive and case-insensitive modes
   - Handle null/empty inputs gracefully
   - **Tests:** 
     - `literal_pattern__exact_match__then_detected`
     - `literal_pattern__case_sensitive__then_no_match`
     - `literal_pattern__case_insensitive__then_matches`

3. **Add regex pattern support** (1h)
   - Extend pattern matching to support regex mode
   - Implement regex compilation caching for performance
   - Add fallback to literal matching for invalid regexes
   - **Tests:**
     - `regex_pattern__valid_regex__then_matches`
     - `regex_pattern__invalid_regex__then_falls_back_to_literal`
     - `regex_pattern__complex_pattern__then_matches_correctly`

4. **Support multiple patterns** (30m)
   - Handle array of trigger patterns with any-match logic
   - Optimize pattern iteration with early termination
   - **Tests:**
     - `multiple_patterns__any_matches__then_detected`
     - `multiple_patterns__none_match__then_not_detected`

### Afternoon (4 hours)
**Objective:** Extend detail lane control structure with selective persistence state

#### Tasks:
5. **Extend DetailLaneControl structure** (1h)
   - Add selective persistence fields to control block
   - Include atomic marked_event_seen flag and window timestamps
   - Add metrics counters for observability
   - **Test:** `test_detail_lane_control_initialization`

6. **Implement CLI configuration integration** (1.5h)
   - Create `create_marking_policy_from_config()` function
   - Map CLI trigger settings to MarkingPolicy structure
   - Handle configuration validation and error cases
   - **Tests:**
     - `cli_config__to_marking_policy__then_patterns_loaded`
     - `invalid_cli_config__graceful_handling__then_default_policy`

7. **Add thread-safe marked event detection** (1h)
   - Integrate pattern matching into event processing pipeline
   - Use atomic operations for marked_event_seen flag updates
   - Ensure lock-free operation for high performance
   - **Tests:**
     - `concurrent_marking__multiple_threads__then_thread_safe`
     - `marked_flag__atomic_updates__then_consistent`

8. **Create comprehensive unit test suite** (30m)
   - Organize tests into logical groups by functionality
   - Add test data generators for realistic event streams
   - Set up test utilities for common assertions
   - **Goal:** 100% code coverage for pattern matching logic

### **Day 1 Deliverables:**
- MarkingPolicy structure with pattern matching capabilities
- Thread-safe marked event detection integrated with detail lane
- CLI configuration to policy conversion
- Complete unit test suite with >95% coverage
- Performance benchmarks for pattern matching overhead

---

## Day 2: Selective Dump Logic and Window Management

### Morning (4 hours)
**Objective:** Implement selective dump decision logic and window lifecycle management

#### Tasks:
1. **Implement selective dump decision logic** (1.5h)
   - Create `should_dump_detail_ring()` function with dual conditions
   - Handle ring-full-but-no-marked-event scenario with discard logic
   - Update metrics for dumps vs discards
   - **Tests:**
     - `ring_not_full__marked_event_seen__then_no_dump`
     - `ring_full__no_marked_event__then_discard_and_continue`
     - `ring_full__marked_event_seen__then_dump_triggered`

2. **Implement window lifecycle management** (1h)
   - Create `start_new_window()` and `close_window_for_dump()` functions
   - Track window boundaries with start/end timestamps
   - Reset marked event flags on window transitions
   - **Tests:**
     - `new_window__start_timestamp_set__then_tracking_begins`
     - `close_window__timestamps_updated__then_dump_ready`
     - `marked_flag__after_dump__then_reset`

3. **Add persistence window metadata** (1h)
   - Define PersistenceWindow structure with event counts and timing
   - Calculate window statistics during dump operations
   - Validate window boundary consistency
   - **Tests:**
     - `window_metadata__calculated_correctly__then_accurate`
     - `overlapping_windows__boundaries_maintained__then_consistent`

4. **Create state management tests** (30m)
   - Test state transitions under various conditions
   - Verify consistency during concurrent operations
   - **Test:** `state_transitions__concurrent_access__then_consistent`

### Afternoon (4 hours)
**Objective:** Integrate with ring pool swap protocol and ATF writer

#### Tasks:
5. **Integrate with ring pool swap** (2h)
   - Create `perform_selective_swap()` function extending M1_E1_I6 protocol
   - Coordinate swap triggers with selective dump decisions
   - Handle swap failures gracefully with state preservation
   - **Tests:**
     - `selective_swap__conditions_met__then_swap_performed`
     - `selective_swap__conditions_not_met__then_swap_skipped`
     - `swap_failure__state_consistent__then_retry_possible`

6. **Integrate with ATF writer** (1.5h)
   - Create `write_window_metadata()` function for persistence metadata
   - Define ATF_WINDOW_METADATA format extending M1_E2_I3
   - Ensure metadata written before event data
   - **Tests:**
     - `window_metadata__written_to_atf__then_readable`
     - `multiple_windows__sequential_metadata__then_ordered`

7. **Create integration test framework** (30m)
   - Set up test harnesses for cross-component testing
   - Create mock objects for external dependencies
   - Add end-to-end test scenarios

### **Day 2 Deliverables:**
- Complete selective dump decision logic with window management
- Integration with ring pool swap protocol from M1_E1_I6
- ATF metadata format for persistence windows
- Integration tests verifying cross-component coordination
- Performance tests for dump decision overhead

---

## Day 3: System Integration and Performance Validation

### Morning (4 hours)
**Objective:** End-to-end system integration and comprehensive testing

#### Tasks:
1. **Create end-to-end test scenarios** (2h)
   - Build complete pipeline from CLI config to ATF output
   - Test realistic event streams with varying marked event density
   - Verify selective persistence reduces storage by expected ratio
   - **Tests:**
     - `full_pipeline__marked_events__then_selective_dumps`
     - `continuous_operation__multiple_windows__then_efficient_storage`

2. **Performance benchmarking and optimization** (1.5h)
   - Measure pattern matching overhead under high throughput
   - Profile memory usage during continuous operation
   - Optimize critical paths identified in profiling
   - **Tests:**
     - `high_throughput__pattern_matching__then_acceptable_overhead`
     - `memory_usage__continuous_operation__then_bounded`

3. **Add comprehensive error handling** (30m)
   - Handle pattern compilation failures gracefully
   - Recover from temporary I/O errors during dumps
   - Validate all input parameters and state consistency
   - **Test:** `error_conditions__graceful_handling__then_recoverable`

### Afternoon (4 hours)
**Objective:** Documentation, metrics, and final validation

#### Tasks:
4. **Implement observability and metrics** (1.5h)
   - Add SelectivePersistenceMetrics structure with key counters
   - Implement metrics collection and reporting functions
   - Create diagnostic output for debugging and monitoring
   - **Test:** `metrics__accurate_counting__then_observable`

5. **Create performance monitoring tools** (1h)
   - Build utilities to measure selective persistence efficiency
   - Add CLI commands to query persistence statistics
   - Create automated performance regression tests
   - **Deliverable:** Performance monitoring dashboard integration

6. **Final integration and system testing** (1h)
   - Run complete test suite with maximum coverage
   - Execute long-running stability tests
   - Validate against acceptance criteria from tech design
   - **Goal:** 100% test pass rate, >98% coverage

7. **Documentation and knowledge transfer** (30m)
   - Update technical documentation with implementation details
   - Create troubleshooting guide for common issues
   - Document performance characteristics and tuning parameters

### **Day 3 Deliverables:**
- Complete end-to-end selective persistence implementation
- Performance validation showing <10% overhead vs simple capture
- Comprehensive metrics and observability capabilities
- System integration tests with 100% pass rate
- Updated documentation and troubleshooting guides

---

## Acceptance Criteria

### Functional Requirements
- [ ] **Pattern Detection:** Accurately detect marked events using CLI-configured patterns
- [ ] **Selective Dumps:** Only dump when ring is full AND marked event seen since last dump
- [ ] **Window Management:** Track precise window boundaries for each persistence segment
- [ ] **Pre-roll Capability:** Maintain continuous capture with pre-roll data before marked events
- [ ] **Integration:** Seamless coordination with CLI parser, ring swap, and ATF writer

### Performance Requirements
- [ ] **Pattern Matching Overhead:** <10% performance impact vs no pattern matching
- [ ] **Memory Efficiency:** Bounded memory growth during continuous operation
- [ ] **Dump Decision Speed:** <1ms to decide dump vs continue for full ring
- [ ] **Storage Efficiency:** >80% reduction in storage when marked events are sparse

### Quality Requirements
- [ ] **Test Coverage:** >98% line and branch coverage
- [ ] **Thread Safety:** All operations safe under concurrent access
- [ ] **Error Handling:** Graceful degradation for invalid patterns/configurations
- [ ] **Observability:** Complete metrics for monitoring and debugging

### Integration Requirements
- [ ] **CLI Configuration:** Supports all trigger pattern types from M1_E2_I4
- [ ] **Ring Pool Swap:** Compatible with swap protocol from M1_E1_I6
- [ ] **ATF Format:** Metadata written in ATF v4 format from M1_E2_I3
- [ ] **Backward Compatibility:** Existing capture behavior unchanged when marking disabled

## Risk Mitigation

### Technical Risks
- **Pattern Matching Performance:** Mitigated by regex compilation caching and optimized matching
- **State Consistency:** Mitigated by atomic operations and comprehensive concurrency testing
- **Memory Leaks:** Mitigated by careful resource management and automated leak detection

### Integration Risks
- **CLI Config Changes:** Mitigated by flexible policy structure adaptable to config evolution
- **ATF Format Evolution:** Mitigated by versioned metadata format with backward compatibility
- **Ring Swap Protocol:** Mitigated by extending rather than modifying existing protocol

### Timeline Risks
- **Pattern Matching Complexity:** Allocated extra time on Day 1 for comprehensive testing
- **Integration Challenges:** Day 2 focused primarily on integration with adequate buffer time
- **Performance Issues:** Day 3 includes optimization time based on benchmarking results

This 3-day iteration delivers a production-ready selective persistence mechanism that enables efficient trace collection with true pre-roll capabilities while maintaining high performance and reliability.