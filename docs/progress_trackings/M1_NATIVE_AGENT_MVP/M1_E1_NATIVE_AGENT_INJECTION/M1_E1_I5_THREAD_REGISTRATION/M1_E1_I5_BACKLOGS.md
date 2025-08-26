# M1_E1_I5 Backlogs: Thread Registration

## Sprint Overview
**Duration**: 3 days (24 hours of work)
**Priority**: P0 - Critical path for trace emission
**Dependencies**: M1_E1_I2 (ThreadRegistry), M1_E1_I3 (Ring Buffers), M1_E1_I4 (Agent Loading)

## Task Breakdown

### Day 1: TLS Foundation and Registration Logic (8 hours)

#### TASK-001: Thread-Local Storage Structure [2 hours]
**Priority**: P0
**Status**: Pending
**Description**: Implement TLS state structure and accessors
**Acceptance Criteria**:
- [ ] Define ada_tls_state_t structure with all fields
- [ ] Implement __thread declaration with proper initialization
- [ ] Add ada_get_tls_state() accessor function
- [ ] Verify TLS works on Linux and macOS
- [ ] Add fallback for platforms without __thread support

**Technical Notes**:
```c
// Implementation approach
static __thread ada_tls_state_t tls_state = {0};
// Consider pthread_key_t as fallback
```

---

#### TASK-002: Fast Path Lane Access [2 hours]
**Priority**: P0
**Status**: Pending
**Description**: Implement optimized lane access with registration trigger
**Acceptance Criteria**:
- [ ] Implement ada_get_thread_lane() with fast path check
- [ ] Add branch prediction hints (LIKELY/UNLIKELY)
- [ ] Ensure inlining for hot path
- [ ] Verify < 10ns latency on cached access
- [ ] Add slow path fallback to registration

**Dependencies**: TASK-001
**Technical Notes**:
```c
// Must be inlined for performance
static inline ThreadLane* ada_get_thread_lane(void)
```

---

#### TASK-003: Atomic Slot Allocation [2 hours]
**Priority**: P0
**Status**: Pending
**Description**: Implement thread-safe slot allocation in registry
**Acceptance Criteria**:
- [ ] Implement ada_thread_registry_register() with CAS loop
- [ ] Update active_mask atomically
- [ ] Handle slot exhaustion gracefully
- [ ] Verify no race conditions with TSan
- [ ] Return initialized lane pointer

**Dependencies**: M1_E1_I2 ThreadRegistry
**Technical Notes**:
```c
// Use atomic_compare_exchange_weak for efficiency
// Retry loop for CAS failures
```

---

#### TASK-004: Thread Registration Flow [2 hours]
**Priority**: P0
**Status**: Pending
**Description**: Complete registration sequence with TLS update
**Acceptance Criteria**:
- [ ] Implement ada_register_current_thread()
- [ ] Capture thread ID and timestamp
- [ ] Initialize lane through registry
- [ ] Update TLS state atomically
- [ ] Add double-check pattern for races

**Dependencies**: TASK-001, TASK-002, TASK-003
**Technical Notes**:
```c
// Use memory_order_release for TLS update
// Ensure all fields visible after registration
```

---

### Day 2: Reentrancy and Thread Safety (8 hours)

#### TASK-005: Reentrancy Guard Implementation [2 hours]
**Priority**: P0
**Status**: Pending
**Description**: Implement reentrancy detection and handling
**Acceptance Criteria**:
- [ ] Define ada_reentrancy_guard_t structure
- [ ] Implement ada_enter_trace() with atomic increment
- [ ] Implement ada_exit_trace() with atomic decrement
- [ ] Track call depth and reentry count
- [ ] Add RAII helper for C++ code

**Dependencies**: TASK-001
**Technical Notes**:
```c
// Use atomic_fetch_add for reentrancy counter
// Track statistics for debugging
```

---

#### TASK-006: Unit Tests - TLS and Registration [3 hours]
**Priority**: P0
**Status**: Pending
**Description**: Comprehensive unit tests for TLS and registration
**Acceptance Criteria**:
- [ ] Test initial TLS state
- [ ] Test first event registration trigger
- [ ] Test cached lane access
- [ ] Test reentrancy detection
- [ ] Achieve 100% code coverage

**Dependencies**: TASK-001 through TASK-005
**Test Files**:
- `test/unit/test_thread_registration.cc`
- `test/unit/test_tls_state.cc`

---

#### TASK-007: Integration Tests - Multi-threading [3 hours]
**Priority**: P0
**Status**: Pending
**Description**: Multi-threaded registration and concurrency tests
**Acceptance Criteria**:
- [ ] Test concurrent registration of 16+ threads
- [ ] Test slot allocation uniqueness
- [ ] Test registration under contention
- [ ] Test maximum thread saturation
- [ ] Verify with ThreadSanitizer

**Dependencies**: TASK-006
**Test Files**:
- `test/integration/test_concurrent_registration.cc`
- `test/integration/test_thread_saturation.cc`

---

### Day 3: Performance and Cleanup (8 hours)

#### TASK-008: Performance Benchmarks [2 hours]
**Priority**: P0
**Status**: Pending
**Description**: Benchmark critical paths
**Acceptance Criteria**:
- [ ] Benchmark fast path access time (< 10ns)
- [ ] Benchmark registration overhead (< 1μs)
- [ ] Benchmark TLS field access (< 2ns)
- [ ] Create performance baseline
- [ ] Add regression detection

**Dependencies**: TASK-001 through TASK-005
**Benchmark Files**:
- `bench/bench_thread_registration.cc`
- `bench/bench_tls_access.cc`

---

#### TASK-009: Thread Cleanup Handler [2 hours]
**Priority**: P1
**Status**: Pending
**Description**: Implement thread exit cleanup
**Acceptance Criteria**:
- [ ] Implement thread destructor function
- [ ] Flush pending events on exit
- [ ] Release slot in registry
- [ ] Clear TLS state
- [ ] Test cleanup behavior

**Dependencies**: TASK-004
**Technical Notes**:
```c
// Use __attribute__((destructor)) or pthread_key_create
// Ensure cleanup even on abnormal termination
```

---

#### TASK-010: Platform Compatibility [2 hours]
**Priority**: P1
**Status**: Pending
**Description**: Ensure cross-platform compatibility
**Acceptance Criteria**:
- [ ] Test on Linux x86_64
- [ ] Test on macOS ARM64
- [ ] Add platform-specific optimizations
- [ ] Document platform requirements
- [ ] Add CI matrix for platforms

**Dependencies**: All previous tasks
**Platforms**:
- Linux (glibc 2.17+)
- macOS (10.15+)

---

#### TASK-011: Documentation and Code Review [2 hours]
**Priority**: P1
**Status**: Pending
**Description**: Complete documentation and review
**Acceptance Criteria**:
- [ ] Update inline code documentation
- [ ] Add usage examples
- [ ] Document memory ordering rationale
- [ ] Pass code review
- [ ] Update integration guide

**Dependencies**: All previous tasks
**Deliverables**:
- API documentation
- Integration examples
- Performance guide

---

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| TLS not supported on platform | High | Low | Implement pthread_key fallback |
| Registration race conditions | High | Medium | Extensive TSan testing |
| Fast path performance regression | High | Medium | Continuous benchmarking |
| Slot exhaustion under load | Medium | Low | Graceful degradation |
| Memory ordering bugs | High | Medium | Explicit ordering + verification |

## Dependencies

### Upstream Dependencies
- M1_E1_I2: ThreadRegistry structure must be complete
- M1_E1_I3: Ring buffer interface must be stable
- M1_E1_I4: Agent loading must be functional

### Downstream Dependencies
- M1_E1_I6: Event emission depends on thread registration
- M1_E2_I1: Drain requires registered threads

## Definition of Done

### Code Quality
- [ ] All tests passing (100% success rate)
- [ ] Code coverage > 95% for new code
- [ ] No memory leaks (Valgrind clean)
- [ ] No race conditions (TSan clean)
- [ ] Performance targets met

### Documentation
- [ ] API documentation complete
- [ ] Integration examples provided
- [ ] Performance characteristics documented
- [ ] Platform requirements specified

### Review
- [ ] Code review approved
- [ ] Design review passed
- [ ] Performance review passed

## Performance Targets

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Fast path latency | < 10ns | - | Pending |
| Registration overhead | < 1μs | - | Pending |
| TLS access time | < 2ns | - | Pending |
| Memory per thread | < 1KB | - | Pending |
| Max concurrent threads | 64 | - | Pending |

## Notes

### Design Decisions
1. **Thread-local storage over shared state**: Eliminates contention
2. **Lazy registration**: Reduces startup overhead
3. **Atomic slot allocation**: Simple and efficient
4. **Cached lane pointer**: Optimizes hot path
5. **Memory ordering**: Explicit for correctness

### Implementation Priority
1. Fast path must be optimized first
2. Registration can be slower (one-time cost)
3. Reentrancy handling is critical for correctness
4. Platform compatibility can use fallbacks

### Testing Strategy
1. Start with unit tests for TLS
2. Add integration tests for concurrency
3. Benchmark continuously during development
4. Use sanitizers throughout

### Known Limitations
1. Maximum 64 threads (by design)
2. TLS requires compiler support
3. Registration cannot be undone
4. Thread ID is platform-specific