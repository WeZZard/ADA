# M1_E3_I1 Backlogs: Backpressure Handling

## Summary

Implement per-thread backpressure mechanisms for graceful handling of ring pool exhaustion. The system must detect pool exhaustion, apply drop policies, track metrics, and recover automatically when load decreases.

## Dependencies

- **M1_E1_I6**: Ring pool swap mechanism working
- **M1_E2_I2**: Per-thread drain operational
- **M1_E1_I3**: Thread registry with per-thread contexts
- **M1_E1_I5**: Ring pool management

## Deliverables Checklist

- [ ] Backpressure state management
- [ ] Pool exhaustion detection
- [ ] Drop policy implementation
- [ ] Event counter tracking
- [ ] Recovery detection logic
- [ ] Configuration system
- [ ] Integration with existing components
- [ ] Performance benchmarks
- [ ] Monitoring and logging

## Prioritized Task List

### P0 - Critical Path (Day 1)

#### 1. Backpressure State Structure [2 hours]
```c
// File: src/backpressure/backpressure_state.h
typedef struct backpressure_state {
    _Atomic(bp_state_t) current_state;
    _Atomic(uint64_t) events_dropped;
    _Atomic(uint64_t) bytes_dropped;
    // ... full structure
} backpressure_state_t;
```
- Define state enum (NORMAL, PRESSURE, DROPPING, RECOVERY)
- Create state structure with atomic fields
- Implement state creation/destruction
- Add configuration structure

**Acceptance**: Compiles, state transitions work with atomics

#### 2. State Transition Logic [2 hours]
```c
// File: src/backpressure/state_transitions.c
void bp_transition_to_pressure(backpressure_state_t* bp);
void bp_transition_to_dropping(backpressure_state_t* bp);
void bp_transition_to_recovery(backpressure_state_t* bp);
void bp_transition_to_normal(backpressure_state_t* bp);
```
- Implement atomic CAS transitions
- Add transition validation
- Log state changes
- Update transition counters

**Acceptance**: All transitions thread-safe, proper memory ordering

#### 3. Pool Exhaustion Detection [2 hours]
```c
// File: src/backpressure/exhaustion_detector.c
bool bp_check_exhaustion(backpressure_state_t* bp,
                         ring_pool_t* pool);
```
- Check pool free count
- Compare against thresholds
- Trigger state transitions
- Update watermark tracking

**Acceptance**: Detects exhaustion accurately, transitions correctly

#### 4. Drop-Oldest Policy [2 hours]
```c
// File: src/backpressure/drop_policy.c
void bp_drop_oldest(ring_buffer_t* ring,
                   backpressure_state_t* bp);
```
- Find oldest event in ring
- Remove from head
- Update drop counters
- Log drop events

**Acceptance**: Drops oldest events, maintains ring integrity

### P0 - Critical Path (Day 2)

#### 5. Integration with Recording [3 hours]
```c
// File: src/trace/trace_record.c
bool trace_record_with_backpressure(thread_context_t* ctx,
                                   const void* data,
                                   uint32_t size);
```
- Check backpressure before recording
- Apply drop policy if needed
- Update counters
- Fallback handling

**Acceptance**: Recording works under backpressure

#### 6. Recovery Detection [2 hours]
```c
// File: src/backpressure/recovery.c
void bp_check_recovery(backpressure_state_t* bp,
                      ring_pool_t* pool);
```
- Monitor pool health
- Check stability period
- Transition to recovery/normal
- Log recovery events

**Acceptance**: Detects recovery, waits for stability

#### 7. Thread Context Integration [2 hours]
```c
// File: src/thread/thread_context.c
void thread_context_init_backpressure(thread_context_t* ctx,
                                     const bp_config_t* config);
```
- Add backpressure state to context
- Initialize per-thread state
- Configure drop policy
- Wire up with registry

**Acceptance**: Each thread has independent backpressure

#### 8. Unit Test Suite [2 hours]
```c
// File: tests/unit/test_backpressure.cpp
TEST(BackpressureState, state__transitions__then_correct);
TEST(DropPolicy, oldest__drops__then_space_freed);
TEST(Recovery, load_decrease__then_recovers);
```
- State transition tests
- Drop policy tests
- Counter tracking tests
- Recovery tests

**Acceptance**: 100% unit test coverage

### P1 - Extended Features (Day 3)

#### 9. Drop-Newest Policy [2 hours]
```c
// File: src/backpressure/drop_newest.c
bool bp_should_drop_new(const event_header_t* event,
                        const backpressure_state_t* bp);
```
- Implement tail drop policy
- Configure policy selection
- Test both policies
- Performance comparison

**Acceptance**: Both policies work, configurable

#### 10. Monitoring and Metrics [2 hours]
```c
// File: src/backpressure/monitoring.c
void bp_log_drop_event(backpressure_state_t* bp, uint64_t count);
void bp_log_state_change(bp_state_t old, bp_state_t new);
void bp_export_metrics(backpressure_state_t* bp, metrics_t* out);
```
- Drop event logging
- State change logging
- Metrics export
- Watermark tracking

**Acceptance**: Complete monitoring, logs generated

#### 11. Integration Tests [3 hours]
```c
// File: tests/integration/test_bp_integration.cpp
TEST(Integration, pool_exhaustion__detected__then_drops);
TEST(Integration, thread_isolation__independent__then_no_interference);
TEST(Integration, recovery__after_load__then_normal);
```
- Pool integration tests
- Thread isolation tests
- Recovery scenario tests
- Component coordination

**Acceptance**: All integration tests pass

#### 12. Configuration System [2 hours]
```c
// File: src/backpressure/config.c
bp_config_t bp_config_from_env(void);
bp_config_t bp_config_from_file(const char* path);
void bp_config_validate(const bp_config_t* config);
```
- Environment variable parsing
- Config file support
- Validation logic
- Default values

**Acceptance**: Configurable via env/file

### P2 - Performance & Polish (Day 4)

#### 13. Performance Benchmarks [2 hours]
```c
// File: tests/bench/bench_backpressure.cpp
BENCHMARK(Overhead, normal_state);
BENCHMARK(Overhead, dropping_state);
BENCHMARK(Scalability, concurrent_threads);
```
- Measure check overhead
- Drop performance
- Scalability testing
- Recovery timing

**Acceptance**: Meets performance targets

#### 14. Behavioral Test Suite [3 hours]
```c
// File: tests/behavioral/test_load_scenarios.cpp
TEST(LoadScenario, burst__then_drops_controlled);
TEST(LoadScenario, sustained__then_graceful_degradation);
TEST(LoadScenario, oscillating__then_stays_protected);
```
- Burst load tests
- Sustained load tests
- Recovery scenarios
- Edge cases

**Acceptance**: All behavioral tests pass

#### 15. Documentation [2 hours]
```markdown
# File: docs/design/backpressure.md
## Backpressure System Design
- Architecture overview
- State machine
- Drop policies
- Configuration guide
```
- Architecture documentation
- Configuration guide
- Integration guide
- Performance notes

**Acceptance**: Complete documentation

#### 16. Final Integration [2 hours]
- Integrate with production code
- Update thread registry
- Verify with existing tests
- Performance validation

**Acceptance**: Fully integrated, all tests pass

## Testing Requirements

### Unit Test Coverage
```bash
# Minimum coverage requirements
backpressure_state.c: 100%
state_transitions.c: 100%
drop_policy.c: 100%
exhaustion_detector.c: 100%
recovery.c: 100%
```

### Integration Points
- Ring pool integration
- Ring swapper coordination
- Thread registry integration
- Drain pipeline interaction

### Performance Targets
- Normal state overhead: < 10ns
- Drop execution: < 50ns
- Recovery detection: < 100ns
- Zero blocking guaranteed

## Risk Mitigation

### Technical Risks

1. **State Race Conditions**
   - Mitigation: Atomic CAS operations
   - Validation: Thread sanitizer testing

2. **Drop Policy Correctness**
   - Mitigation: Extensive testing
   - Validation: Behavioral test suite

3. **Recovery False Positives**
   - Mitigation: Stability period requirement
   - Validation: Load scenario testing

### Schedule Risks

1. **Integration Complexity**
   - Buffer: 4 hours in schedule
   - Fallback: Reduce policy options

2. **Performance Requirements**
   - Buffer: Extra optimization time
   - Fallback: Accept 20ns overhead

## Definition of Done

### Code Complete
- [ ] All components implemented
- [ ] Unit tests passing (100% coverage)
- [ ] Integration tests passing
- [ ] Performance benchmarks met
- [ ] Code reviewed and approved

### Quality Gates
- [ ] Build succeeds on all platforms
- [ ] No memory leaks (Valgrind/ASAN)
- [ ] No data races (TSAN clean)
- [ ] Coverage > 95% overall
- [ ] Documentation complete

### Integration Verified
- [ ] Works with ring pool
- [ ] Works with ring swapper
- [ ] Thread isolation maintained
- [ ] Drain pipeline compatible

## Time Estimates

| Phase | Tasks | Estimated Time | Buffer |
|-------|-------|---------------|--------|
| P0 Core (Day 1) | Tasks 1-4 | 8 hours | 1 hour |
| P0 Core (Day 2) | Tasks 5-8 | 9 hours | 1 hour |
| P1 Extended (Day 3) | Tasks 9-12 | 9 hours | 1 hour |
| P2 Performance (Day 4) | Tasks 13-16 | 9 hours | 1 hour |
| **Total** | **16 tasks** | **35 hours** | **4 hours** |

## Success Metrics

### Functional Metrics
- Pool exhaustion detected: 100% accuracy
- Events dropped correctly: 100% verification
- Recovery successful: 100% cases
- Thread isolation: Zero interference

### Performance Metrics
- Normal overhead: < 10ns achieved
- Drop overhead: < 50ns achieved
- Recovery time: < 1 second
- Scalability: 64 threads verified

### Quality Metrics
- Test coverage: > 95%
- Zero memory leaks
- Zero data races
- All integration points verified

## Follow-up Iterations

### M1_E3_I2: Advanced Policies
- Priority-based dropping
- Smart event filtering
- Dynamic thresholds
- Predictive backpressure

### M1_E3_I3: Monitoring Dashboard
- Real-time metrics
- Historical analysis
- Alert system
- Performance profiling