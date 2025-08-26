# M1_E2_I5 Backlogs: Duration Timer

## Implementation Tasks

### Priority 0 (Core Functionality)

#### T1: Timer Module Structure [2 hours]
**Description**: Create timer module with core data structures and initialization
**Acceptance Criteria**:
- [ ] timer.h header with API declarations
- [ ] timer.c with timer_manager structure
- [ ] timer_init() and timer_cleanup() functions
- [ ] Global timer instance initialized
- [ ] Atomic flags for thread safety

**Implementation Notes**:
```c
// Key structures to implement
- timer_manager_t with all atomic fields
- timer_config_t for configuration
- Thread state tracking atomics
- Start time using CLOCK_MONOTONIC
```

#### T2: Timer Thread Implementation [3 hours]
**Description**: Implement timer thread with duration monitoring
**Acceptance Criteria**:
- [ ] timer_thread_func() implementation
- [ ] Accurate time tracking with clock_gettime()
- [ ] Check interval loop (100ms default)
- [ ] Cancellation checking in loop
- [ ] Shutdown trigger on expiration

**Dependencies**: T1
**Implementation Notes**:
```c
// Key functions to implement
- timer_thread_func() as pthread entry
- interruptible_sleep_ms() with nanosleep
- Check cancellation flag each iteration
- Trigger shutdown on duration expiry
```

#### T3: Time Calculation Functions [2 hours]
**Description**: Implement accurate time calculation utilities
**Acceptance Criteria**:
- [ ] calculate_elapsed_ms() function
- [ ] timer_remaining_ms() function
- [ ] Handle nanosecond precision
- [ ] Handle time wraparound
- [ ] CLOCK_MONOTONIC for stability

**Dependencies**: T1
**Implementation Notes**:
```c
// Calculations needed
- Elapsed = (now - start) in milliseconds
- Handle tv_nsec underflow correctly
- Convert timespec to milliseconds
- Return 0 for expired/inactive timer
```

#### T4: Timer Management API [3 hours]
**Description**: Implement public API for timer control
**Acceptance Criteria**:
- [ ] timer_start() with thread creation
- [ ] timer_cancel() with thread join
- [ ] timer_is_active() status check
- [ ] Proper error handling
- [ ] Thread-safe operations

**Dependencies**: T1, T2
**Implementation Notes**:
```c
// API requirements
- Check for already running timer
- Handle zero duration (no timer)
- Atomic state management
- Clean pthread_create/join
- Set errno appropriately
```

#### T5: Shutdown Integration [2 hours]
**Description**: Integrate timer with shutdown system
**Acceptance Criteria**:
- [ ] trigger_timed_shutdown() function
- [ ] Pass SHUTDOWN_REASON_TIMER
- [ ] Coordinate with drain thread
- [ ] Update shutdown statistics
- [ ] Clean timer exit

**Dependencies**: T2, T4
**Implementation Notes**:
```c
// Integration points
- Call shutdown_initiate() on expiry
- Set appropriate shutdown reason
- Let timer thread exit naturally
- Avoid double-shutdown scenarios
```

### Priority 1 (Integration & Polish)

#### T6: CLI Integration [2 hours]
**Description**: Connect timer to CLI duration flag
**Acceptance Criteria**:
- [ ] Parse --duration flag in main()
- [ ] Convert to milliseconds
- [ ] Start timer if duration > 0
- [ ] Print timer status message
- [ ] Handle parse errors

**Dependencies**: T4, M1_E2_I4
**Implementation Notes**:
```c
// Main integration
- Check args.duration_ms after parse
- Call timer_start() if needed
- Error handling for timer creation
- Status output to user
```

#### T7: Signal Handler Integration [2 hours]
**Description**: Cancel timer on manual shutdown signals
**Acceptance Criteria**:
- [ ] Modify signal_handler()
- [ ] Check timer_is_active()
- [ ] Call timer_cancel() if active
- [ ] Proceed with shutdown
- [ ] No race conditions

**Dependencies**: T4, T5
**Implementation Notes**:
```c
// Signal handler updates
- Add timer cancellation logic
- Ensure proper ordering
- Avoid deadlocks with atomics
- Clean cancellation path
```

#### T8: Sub-second Precision Support [1 hour]
**Description**: Support millisecond-precision durations
**Acceptance Criteria**:
- [ ] Parse decimal durations (e.g., 1.5s)
- [ ] Handle sub-second values
- [ ] Accurate sub-second timing
- [ ] Update help text
- [ ] Validate input range

**Dependencies**: T6
**Implementation Notes**:
```c
// Precision handling
- Parse float/double from CLI
- Convert to milliseconds
- Maintain precision in calculations
- Test with 100ms, 500ms durations
```

## Testing Tasks

### Priority 0 (Critical Tests)

#### TT1: Unit Test Suite [3 hours]
**Description**: Comprehensive unit tests for timer module
**Test Coverage**:
- [ ] Timer creation tests
- [ ], Duration tracking tests
- [ ] Cancellation tests
- [ ] Time calculation tests
- [ ] Thread state tests

**Dependencies**: T1-T4
**Test Files**: tests/unit/test_timer.c

#### TT2: Integration Test Suite [2 hours]
**Description**: Integration tests with shutdown system
**Test Coverage**:
- [ ] Timer-triggered shutdown
- [ ] Signal cancellation
- [ ] CLI integration
- [ ] Resource cleanup
- [ ] State coordination

**Dependencies**: T5-T7
**Test Files**: tests/integration/test_timer_shutdown.c

#### TT3: Performance Benchmarks [2 hours]
**Description**: Validate performance requirements
**Test Coverage**:
- [ ] Timing accuracy measurements
- [ ] CPU usage validation
- [ ] Memory overhead check
- [ ] Thread creation timing
- [ ] Cancellation timing

**Dependencies**: T1-T4
**Test Files**: tests/perf/bench_timer.c

### Priority 1 (Extended Tests)

#### TT4: Stress Tests [1 hour]
**Description**: Stress and edge case testing
**Test Coverage**:
- [ ] Rapid start/stop cycles
- [ ] Very short durations
- [ ] Very long durations
- [ ] Overflow scenarios
- [ ] Concurrent operations

**Dependencies**: TT1
**Test Files**: tests/stress/stress_timer.c

#### TT5: Thread Safety Validation [1 hour]
**Description**: Verify thread safety with sanitizers
**Test Coverage**:
- [ ] ThreadSanitizer clean
- [ ] No data races
- [ ] Proper atomics usage
- [ ] Memory ordering correct
- [ ] No deadlocks

**Dependencies**: TT1, TT2
**Tools**: ThreadSanitizer, Helgrind

## Documentation Tasks

### Priority 1 (User Documentation)

#### DT1: API Documentation [1 hour]
**Description**: Document timer API in headers
**Deliverables**:
- [ ] Function documentation in timer.h
- [ ] Usage examples
- [ ] Error codes documented
- [ ] Thread safety notes
- [ ] Integration guide

**Dependencies**: T1-T4

#### DT2: CLI Usage Documentation [30 minutes]
**Description**: Update CLI help and README
**Deliverables**:
- [ ] --duration flag documentation
- [ ] Examples with different durations
- [ ] Precision specifications
- [ ] Behavior documentation
- [ ] Troubleshooting guide

**Dependencies**: T6

## Bug Fixes & Improvements

### Priority 2 (Nice to Have)

#### BF1: Enhanced Precision Mode [2 hours]
**Description**: Optional high-precision timing mode
**Improvements**:
- [ ] Shorter check intervals (10ms)
- [ ] Higher resolution timing
- [ ] Configurable precision
- [ ] CPU usage tradeoff
- [ ] Performance metrics

**Dependencies**: T2

#### BF2: Timer Statistics [1 hour]
**Description**: Collect and report timer statistics
**Improvements**:
- [ ] Number of checks performed
- [ ] Actual vs requested duration
- [ ] Timer accuracy percentage
- [ ] Resource usage stats
- [ ] Debug output option

**Dependencies**: T2, T3

## Iteration Timeline

### Day 1 (8 hours)
- Morning:
  - [ ] T1: Timer Module Structure (2h)
  - [ ] T2: Timer Thread Implementation (3h)
- Afternoon:
  - [ ] T3: Time Calculation Functions (2h)
  - [ ] TT1: Unit Test Suite - Part 1 (1h)

### Day 2 (8 hours)
- Morning:
  - [ ] T4: Timer Management API (3h)
  - [ ] T5: Shutdown Integration (2h)
- Afternoon:
  - [ ] TT1: Unit Test Suite - Part 2 (2h)
  - [ ] TT3: Performance Benchmarks (1h)

### Day 3 (8 hours)
- Morning:
  - [ ] T6: CLI Integration (2h)
  - [ ] T7: Signal Handler Integration (2h)
- Afternoon:
  - [ ] TT2: Integration Test Suite (2h)
  - [ ] TT3: Performance Benchmarks - Complete (1h)
  - [ ] T8: Sub-second Precision Support (1h)

### Day 4 (4 hours)
- Morning:
  - [ ] TT4: Stress Tests (1h)
  - [ ] TT5: Thread Safety Validation (1h)
  - [ ] DT1: API Documentation (1h)
  - [ ] DT2: CLI Usage Documentation (0.5h)
  - [ ] Final validation and cleanup (0.5h)

## Success Metrics

### Completion Criteria
- [ ] All P0 tasks completed
- [ ] All P0 tests passing
- [ ] 100% code coverage achieved
- [ ] Performance targets met
- [ ] Documentation updated

### Performance Targets
- Timer accuracy: ±100ms
- CPU usage: < 0.1%
- Memory overhead: < 1KB
- Thread creation: < 10ms
- Cancellation: < 10ms

### Quality Targets
- ThreadSanitizer: Clean
- Valgrind: No leaks
- Helgrind: No races
- Code review: Approved
- CI pipeline: Green

## Risk Mitigation

### Technical Risks
1. **Clock drift on virtual machines**
   - Mitigation: Use CLOCK_MONOTONIC
   - Fallback: Document VM limitations

2. **Signal delivery delays**
   - Mitigation: Use atomic flags
   - Fallback: Polling with timeout

3. **Thread creation failures**
   - Mitigation: Proper error handling
   - Fallback: Warn and continue without timer

### Schedule Risks
1. **Integration complexity**
   - Mitigation: Early integration testing
   - Buffer: 4 hours allocated for issues

2. **Performance tuning**
   - Mitigation: Early benchmarking
   - Buffer: Can defer BF1/BF2 if needed

## Dependencies on Other Iterations

### Required (Must Have)
- M1_E2_I4: CLI parser with --duration flag
- M1_E2_I3: Shutdown handler system
- M1_E2_I1: Drain thread for coordination

### Optional (Nice to Have)
- M1_E2_I6: Process monitoring (future)
- M1_E3: Query engine integration (future)

## Notes and Considerations

### Design Decisions
1. Using CLOCK_MONOTONIC for immunity to time adjustments
2. 100ms check interval balances accuracy vs CPU usage
3. Atomic operations avoid mutex overhead
4. Single timer instance keeps design simple

### Future Enhancements
1. Multiple timer support for different events
2. Pause/resume capability
3. Timer callback system
4. Configurable check intervals
5. Integration with trace windowing

### Known Limitations
1. Single global timer instance
2. Fixed check interval (100ms)
3. No pause/resume support
4. Limited to millisecond precision
5. No timer event callbacks