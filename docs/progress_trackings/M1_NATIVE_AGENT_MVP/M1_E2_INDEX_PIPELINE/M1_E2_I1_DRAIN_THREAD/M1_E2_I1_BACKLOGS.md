# M1_E2_I1 Backlogs: Drain Thread Implementation

## Overview
This iteration implements the core drain thread that continuously polls all thread lanes for filled rings, ensuring fair processing and graceful shutdown. Total estimated time: 2-3 days.

## Task Breakdown

### Day 1: Core Implementation (8-10 hours)

#### Morning: Foundation (4-5 hours)
| Task ID | Task Description | Priority | Estimate | Status | Notes |
|---------|-----------------|----------|----------|--------|-------|
| DT-001 | Create drain_thread.h with structure definitions | P0 | 30m | [ ] | DrainThread, DrainConfig, DrainState |
| DT-002 | Implement drain_thread_create/destroy | P0 | 1h | [ ] | Memory allocation, initialization |
| DT-003 | Implement state machine transitions | P0 | 1h | [ ] | start/stop with atomic operations |
| DT-004 | Create drain_worker thread function skeleton | P0 | 30m | [ ] | Basic pthread setup |
| DT-005 | Write unit tests for lifecycle management | P0 | 1h | [ ] | create/destroy/start/stop tests |
| DT-006 | Implement basic poll loop structure | P0 | 1h | [ ] | While loop with state checking |

#### Afternoon: Polling Logic (4-5 hours)
| Task ID | Task Description | Priority | Estimate | Status | Notes |
|---------|-----------------|----------|----------|--------|-------|
| DT-007 | Implement thread enumeration via registry | P0 | 1h | [ ] | Iterate over all threads |
| DT-008 | Implement ring consumption logic | P0 | 1h | [ ] | Dequeue and process rings |
| DT-009 | Add sleep/yield behavior for idle | P0 | 30m | [ ] | Configurable poll interval |
| DT-010 | Implement batch processing limits | P0 | 1h | [ ] | max_batch_size per thread |
| DT-011 | Write polling behavior unit tests | P0 | 1h | [ ] | Empty/single/multiple producer tests |
| DT-012 | Add basic metrics collection | P0 | 30m | [ ] | Cycles, rings processed |

### Day 2: Fairness and Integration (8-10 hours)

#### Morning: Fair Scheduling (4-5 hours)
| Task ID | Task Description | Priority | Estimate | Status | Notes |
|---------|-----------------|----------|----------|--------|-------|
| DT-013 | Create PollContext for scheduling state | P0 | 30m | [ ] | Round-robin tracking |
| DT-014 | Implement fairness quantum logic | P0 | 1h | [ ] | Switch threads after quantum |
| DT-015 | Add per-thread metrics tracking | P0 | 1h | [ ] | Rings per thread counters |
| DT-016 | Write fairness unit tests | P0 | 1h | [ ] | Verify equal processing |
| DT-017 | Implement starvation prevention | P0 | 1h | [ ] | Ensure all threads served |
| DT-018 | Add last_seen timestamp tracking | P1 | 30m | [ ] | Debug/monitoring aid |

#### Afternoon: Integration and Performance (4-5 hours)
| Task ID | Task Description | Priority | Estimate | Status | Notes |
|---------|-----------------|----------|----------|--------|-------|
| DT-019 | Implement graceful shutdown with final drain | P0 | 1h | [ ] | Process remaining work |
| DT-020 | Write integration tests with producers | P0 | 1h | [ ] | Multi-thread scenarios |
| DT-021 | Add performance benchmarks | P0 | 1h | [ ] | Throughput and latency |
| DT-022 | Implement cycle time tracking | P1 | 30m | [ ] | Poll loop performance |
| DT-023 | Write stress tests for max threads | P0 | 1h | [ ] | 64 thread scalability |
| DT-024 | Add memory leak detection tests | P0 | 30m | [ ] | Valgrind/ASAN validation |

### Day 3: Polish and Optimization (Optional, 4-6 hours)

#### Optimization Tasks
| Task ID | Task Description | Priority | Estimate | Status | Notes |
|---------|-----------------|----------|----------|--------|-------|
| DT-025 | Cache-align critical structures | P1 | 1h | [ ] | Reduce false sharing |
| DT-026 | Add adaptive polling intervals | P2 | 2h | [ ] | Dynamic based on load |
| DT-027 | Implement NUMA-aware scheduling | P2 | 2h | [ ] | Thread affinity optimization |
| DT-028 | Add configurable poll strategies | P2 | 2h | [ ] | Priority, weighted, etc |
| DT-029 | Create performance profiling harness | P1 | 1h | [ ] | CPU and memory profiling |
| DT-030 | Document performance tuning guide | P1 | 1h | [ ] | Configuration best practices |

## Implementation Priority

### Critical Path (Must Complete)
1. **Core Infrastructure** (DT-001 to DT-006)
   - Basic drain thread with lifecycle management
   - State machine for clean start/stop
   - Worker thread skeleton

2. **Basic Polling** (DT-007 to DT-012)
   - Thread enumeration and ring consumption
   - Idle behavior and batch limits
   - Essential metrics

3. **Fairness Guarantee** (DT-013 to DT-017)
   - Round-robin scheduling
   - Quantum-based switching
   - Starvation prevention

4. **Integration Ready** (DT-019 to DT-020)
   - Graceful shutdown
   - Multi-thread testing

### Nice to Have
- Performance optimizations (DT-025 to DT-027)
- Alternative polling strategies (DT-028)
- Advanced monitoring (DT-018, DT-022)

## Testing Checklist

### Unit Test Coverage
- [ ] Lifecycle: create, start, stop, destroy
- [ ] State machine: all transitions
- [ ] Polling: empty, single, multiple producers
- [ ] Fairness: round-robin, quantum limits
- [ ] Metrics: accurate counting
- [ ] Error cases: invalid states, failures

### Integration Test Coverage
- [ ] Multi-threaded producers
- [ ] Continuous operation
- [ ] Shutdown with pending work
- [ ] Registry integration
- [ ] Memory leak validation

### Performance Validation
- [ ] Throughput > 100K rings/sec
- [ ] Poll cycle < 100μs
- [ ] CPU usage < 5% idle
- [ ] Memory stable over time
- [ ] 64 thread scalability

## Dependencies

### Required Before Start
- ThreadRegistry implementation (M1_E1_I2)
- Ring pool with submit queues (M1_E1_I6)
- Basic ring buffer structure

### Provides to Next Iteration
- Running drain infrastructure
- Ring consumption interface
- Performance baseline
- Integration patterns

## Risk Mitigation

### Technical Risks
| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| Poll cycle too slow | Medium | High | Batch processing, optimize loops |
| Unfair scheduling | Low | High | Quantum limits, round-robin |
| Memory leaks | Low | High | ASAN, valgrind, stress tests |
| Thread starvation | Medium | Medium | Fairness quantum, monitoring |
| CPU spinning | Medium | Medium | Configurable sleep/yield |

### Schedule Risks
| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| Complex debugging | Medium | Medium | Extensive logging, metrics |
| Performance tuning | High | Low | Basic version first, optimize later |
| Integration issues | Low | Medium | Clear interfaces, mocks |

## Success Criteria

### Functional
- [ ] Drain thread starts and stops cleanly
- [ ] Consumes rings from all threads
- [ ] Fair scheduling prevents starvation
- [ ] Graceful shutdown completes
- [ ] Metrics track operations

### Performance
- [ ] Meets throughput target (100K rings/sec)
- [ ] Poll cycle under threshold (100μs)
- [ ] Acceptable CPU usage (< 5% idle)
- [ ] Stable memory usage

### Quality
- [ ] 100% unit test coverage
- [ ] Integration tests pass
- [ ] No memory leaks
- [ ] Thread-safe operations
- [ ] Clean documentation

## Notes and Considerations

### Architecture Decisions
1. **Single drain thread**: Simpler than multiple, sufficient for v1
2. **Round-robin default**: Fair and predictable
3. **Configurable polling**: Balance CPU vs latency
4. **Batch processing**: Amortize overhead

### Future Enhancements
1. Multiple drain threads with partitioning
2. Priority-based scheduling
3. Backpressure mechanisms
4. Dynamic tuning based on workload
5. Advanced monitoring and alerts

### Implementation Tips
1. Start with simplest working version
2. Add fairness after basic polling works
3. Optimize only after correctness proven
4. Use extensive logging during development
5. Test with various workload patterns

## Team Coordination

### Code Review Points
1. After core infrastructure (DT-006)
2. After fairness implementation (DT-017)
3. Final review with all tests (DT-024)

### Integration Points
1. Registry interface alignment
2. Ring consumption API
3. Metrics exposure format
4. Shutdown coordination

### Documentation Updates
1. API reference for drain_thread.h
2. Configuration guide
3. Performance tuning notes
4. Integration examples