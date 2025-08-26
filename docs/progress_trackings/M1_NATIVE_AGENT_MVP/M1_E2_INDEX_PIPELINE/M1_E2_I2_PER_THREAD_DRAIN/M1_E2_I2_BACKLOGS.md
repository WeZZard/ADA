# M1_E2_I2: Per-Thread Drain - Backlogs

## Sprint Overview
**Duration**: 3 days  
**Complexity**: High  
**Risk Level**: Medium  
**Dependencies**: M1_E2_I1 (Drain Thread), M1_E1_I2 (ThreadRegistry)

## Day 1: Core Components (8 hours)

### Task 1.1: DrainIterator Implementation (2.5 hours)
**Priority**: P0  
**Owner**: Core Team

**Subtasks**:
- [ ] Define DrainIterator structure with configuration (30 min)
- [ ] Implement drain_iterator_create() with initialization (30 min)
- [ ] Implement basic drain_iteration() skeleton (45 min)
- [ ] Add thread state tracking array (30 min)
- [ ] Write unit tests for iterator lifecycle (15 min)

**Definition of Done**:
- DrainIterator compiles without warnings
- Basic structure initialized correctly
- Unit tests pass for creation/destruction

### Task 1.2: ThreadDrainState Management (2 hours)
**Priority**: P0  
**Owner**: Core Team

**Subtasks**:
- [ ] Define ThreadDrainState structure (20 min)
- [ ] Implement register_thread_for_drain() (30 min)
- [ ] Implement deregister_thread_from_drain() (30 min)
- [ ] Add state transition helpers (20 min)
- [ ] Write unit tests for state management (20 min)

**Definition of Done**:
- Thread states properly tracked
- Registration/deregistration thread-safe
- Memory ordering correct

### Task 1.3: DrainScheduler Framework (2 hours)
**Priority**: P0  
**Owner**: Core Team

**Subtasks**:
- [ ] Define DrainScheduler interface (30 min)
- [ ] Implement round-robin scheduler (45 min)
- [ ] Add thread selection logic (30 min)
- [ ] Write unit tests for scheduling (15 min)

**Definition of Done**:
- Scheduler selects threads correctly
- Round-robin fairness demonstrated
- Tests verify selection pattern

### Task 1.4: Submit Queue Coordination (1.5 hours)
**Priority**: P0  
**Owner**: Core Team

**Subtasks**:
- [ ] Implement submit_queue_try_acquire_drain() (30 min)
- [ ] Add pending event tracking (20 min)
- [ ] Implement notification mechanism (25 min)
- [ ] Write coordination tests (15 min)

**Definition of Done**:
- Non-blocking acquisition works
- Pending events tracked accurately
- Producer/consumer coordination correct

## Day 2: Drainage Logic & Fairness (8 hours)

### Task 2.1: Per-Thread Lane Drainage (2.5 hours)
**Priority**: P0  
**Owner**: Core Team

**Subtasks**:
- [ ] Implement drain_thread_lanes() function (45 min)
- [ ] Add index lane drainage logic (30 min)
- [ ] Add detail lane conditional drainage (30 min)
- [ ] Implement batch collection (30 min)
- [ ] Write drainage unit tests (25 min)

**Definition of Done**:
- Lanes drained correctly per spec
- Index always drained when present
- Detail only drained when marked
- Zero event loss verified

### Task 2.2: Fair Scheduling Implementation (2 hours)
**Priority**: P0  
**Owner**: Core Team

**Subtasks**:
- [ ] Implement weighted fair scheduler (45 min)
- [ ] Add credit-based fairness (30 min)
- [ ] Implement priority adjustments (25 min)
- [ ] Write fairness tests (20 min)

**Definition of Done**:
- Jain's fairness index > 0.9
- Credits distributed fairly
- Priority adjustments working

### Task 2.3: Iteration Control Flow (2 hours)
**Priority**: P0  
**Owner**: Core Team

**Subtasks**:
- [ ] Implement complete drain_iteration() (45 min)
- [ ] Add iteration timing control (20 min)
- [ ] Implement batch persistence calls (30 min)
- [ ] Add iteration metrics collection (25 min)

**Definition of Done**:
- Full iteration flow working
- Timing constraints met
- Metrics collected accurately

### Task 2.4: Dynamic Thread Handling (1.5 hours)
**Priority**: P0  
**Owner**: Core Team

**Subtasks**:
- [ ] Handle thread registration during drain (30 min)
- [ ] Implement final drain for deregistering threads (30 min)
- [ ] Add thread state transitions (20 min)
- [ ] Write dynamic thread tests (10 min)

**Definition of Done**:
- New threads included in next iteration
- Deregistering threads fully drained
- No race conditions

## Day 3: Integration & Performance (8 hours)

### Task 3.1: ThreadRegistry Integration (2 hours)
**Priority**: P0  
**Owner**: Integration Team

**Subtasks**:
- [ ] Implement registration callbacks (30 min)
- [ ] Add deregistration coordination (30 min)
- [ ] Wire up notification system (30 min)
- [ ] Write integration tests (30 min)

**Definition of Done**:
- Callbacks properly registered
- Thread lifecycle coordinated
- Integration tests pass

### Task 3.2: Metrics & Monitoring (1.5 hours)
**Priority**: P1  
**Owner**: Core Team

**Subtasks**:
- [ ] Implement DrainMetrics collection (30 min)
- [ ] Add per-thread statistics (20 min)
- [ ] Calculate fairness metrics (20 min)
- [ ] Add performance counters (20 min)

**Definition of Done**:
- All metrics collected accurately
- Fairness index calculated correctly
- Performance data available

### Task 3.3: Performance Testing (2.5 hours)
**Priority**: P0  
**Owner**: Test Team

**Subtasks**:
- [すみません]Write throughput benchmark (30 min)
- [ ] Write latency benchmark (30 min)
- [ ] Write CPU usage benchmark (30 min)
- [ ] Run and analyze benchmarks (45 min)
- [ ] Optimize based on results (15 min)

**Definition of Done**:
- Throughput > 1M events/sec
- P99 latency < 10ms
- CPU usage < 5%

### Task 3.4: Stress Testing (2 hours)
**Priority**: P0  
**Owner**: Test Team

**Subtasks**:
- [ ] Write thread churn stress test (30 min)
- [ ] Write burst load test (30 min)
- [ ] Run extended stress tests (45 min)
- [ ] Analyze and fix issues (15 min)

**Definition of Done**:
- No event loss under stress
- Stable operation verified
- Memory leaks fixed

## Risk Mitigation

### Identified Risks

#### Risk 1: Fairness Algorithm Complexity
**Impact**: High  
**Probability**: Medium  
**Mitigation**:
- Start with simple round-robin
- Add weighted fairness incrementally
- Profile and optimize hot paths

#### Risk 2: Lock Contention on Submit Queues
**Impact**: High  
**Probability**: Low  
**Mitigation**:
- Use try_acquire (non-blocking)
- Skip busy threads in iteration
- Increase priority for skipped threads

#### Risk 3: Memory Ordering Bugs
**Impact**: High  
**Probability**: Medium  
**Mitigation**:
- Careful review of all atomic operations
- Use TSan for testing
- Conservative ordering initially

## Technical Debt Items

### Debt 1: Adaptive Scheduling
**Description**: Current scheduler uses fixed algorithm  
**Impact**: Medium performance impact under varying loads  
**Resolution**: Implement adaptive scheduling in next iteration

### Debt 2: Batch Size Optimization
**Description**: Fixed batch sizes may not be optimal  
**Impact**: Low - some throughput left on table  
**Resolution**: Add dynamic batch sizing based on load

### Debt 3: NUMA Awareness
**Description**: No NUMA optimization for thread selection  
**Impact**: Low on current hardware  
**Resolution**: Consider for future scalability

## Dependencies

### External Dependencies
- M1_E2_I1: DrainThread basic implementation
- M1_E1_I2: ThreadRegistry with all threads
- M1_E1_I1: Per-thread lane structures

### Internal Dependencies
- DrainIterator depends on DrainScheduler
- DrainScheduler depends on ThreadDrainState
- All depend on Submit Queue coordination

## Success Metrics

### Quantitative Metrics
- **Throughput**: > 1M events/sec with 64 threads
- **Latency**: P99 < 10ms, P50 < 1ms
- **Fairness**: Jain's index > 0.9
- **CPU Usage**: < 5% steady state
- **Event Loss**: 0 events lost

### Qualitative Metrics
- Code maintainability score > 8/10
- Test coverage > 95%
- Documentation completeness
- Team confidence in design

## Review Checklist

### Code Review
- [ ] Memory ordering reviewed by expert
- [ ] Fairness algorithm validated
- [ ] Error paths tested
- [ ] Resource cleanup verified

### Testing Review
- [ ] Unit test coverage > 95%
- [ ] Integration tests comprehensive
- [ ] Performance targets met
- [ ] Stress tests pass consistently

### Documentation Review
- [ ] API documentation complete
- [ ] Integration guide written
- [ ] Performance tuning guide
- [ ] Troubleshooting section

## Next Iteration Planning

### M1_E2_I3: Advanced Scheduling
- Priority-based scheduling
- Adaptive batch sizing
- NUMA-aware thread selection
- Advanced fairness algorithms

### M1_E2_I4: Monitoring & Observability
- Real-time metrics export
- Drain performance dashboard
- Anomaly detection
- Auto-tuning parameters