# Test Plan — M1 E3 I1 Backpressure and Metrics

## Objective
Validate per-thread backpressure handling, drop accounting under load, and comprehensive metrics visibility.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Backpressure Logic | ✓ | ✓ | ✓ |
| Per-Thread Metrics | ✓ | ✓ | - |
| Drop Accounting | ✓ | ✓ | ✓ |
| Metrics Reporting | ✓ | ✓ | - |
| Pool Exhaustion | ✓ | ✓ | ✓ |

## Test Execution Sequence
1. Unit Tests → 2. Pressure Tests → 3. Integration Tests → 4. Stress Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| backpressure__thread_pool_full__then_drops | Full rings | Drop oldest per thread | events_dropped increments |
| metrics__per_thread__then_isolated | Multi-thread load | Separate metrics | No cross-thread pollution |
| backpressure__slow_drain__then_managed | Throttled I/O | Graceful degradation | System remains responsive |
| metrics__reporting__then_periodic | Running system | 5s updates | Accurate, monotonic values |

## Test Categories

### 1. Backpressure Tests

#### Test: `backpressure__per_thread_exhaustion__then_drops`
- **Setup**: Limit each thread to 2 spare rings
- **Execute**: Generate burst exceeding capacity
- **Verify**:
  - Per-thread `pool_exhaustion_count` increments
  - `events_dropped` tracked per thread
  - Drop-oldest policy per thread
  - Other threads unaffected
- **Teardown**: Reset pool sizes

#### Test: `backpressure__gradual_load__then_adapts`
- **Setup**: Variable load generator
- **Execute**: Ramp from 1K to 1M events/sec
- **Verify**:
  - Smooth transition to dropping
  - No sudden failures
  - Metrics reflect pressure
  - Recovery when load decreases
- **Teardown**: Load generator off

#### Test: `backpressure__drain_throttle__then_survives`
- **Setup**: Inject drain delays (dev knob)
- **Execute**: Normal event rate
- **Verify**:
  - System detects slow drain
  - Activates backpressure
  - Continues operation
  - No deadlock/livelock
- **Teardown**: Remove throttling

### 2. Per-Thread Metrics Tests

#### Test: `metrics__thread_isolation__then_accurate`
- **Setup**: 5 threads, different rates
- **Execute**: Each thread unique load
- **Verify per thread:
  ```c
  ThreadMetrics {
    events_written: <per_thread>,
    events_dropped: <per_thread>,
    pool_exhaustion_count: <per_thread>,
    bytes_written: <per_thread>,
    ring_swaps: <per_thread>
  }
  ```
- **Teardown**: Validate totals

#### Test: `metrics__atomicity__then_consistent`
- **Setup**: High-frequency updates
- **Execute**: Concurrent metric updates
- **Verify**:
  - No torn reads
  - Monotonic increments
  - Thread-safe operations
  - Memory ordering correct
- **Teardown**: N/A

### 3. Drop Accounting Tests

#### Test: `drops__counted_accurately__then_reconciles`
- **Setup**: Known event generation
- **Execute**: Force drops via small rings
- **Verify**:
  - drops + written = generated
  - Per-thread accounting correct
  - Global totals match sum
- **Teardown**: Reset ring sizes

#### Test: `drops__by_priority__then_oldest_first`
- **Setup**: Timestamped events
- **Execute**: Fill ring to capacity
- **Verify**:
  - Oldest events dropped first
  - Newer events preserved
  - Timestamp ordering maintained
- **Teardown**: N/A

### 4. Metrics Reporting Tests

#### Test: `reporting__periodic_output__then_timely`
- **Setup**: Metrics thread enabled
- **Execute**: Run for 30 seconds
- **Verify**:
  - Updates every 5s ±100ms
  - Format consistent
  - Values non-decreasing
  - All threads represented
- **Teardown**: Stop metrics thread

#### Test: `reporting__format__then_readable`
- **Setup**: Capture metrics output
- **Execute**: Parse log lines
- **Verify format:
  ```
  [METRICS] T+5.0s: threads=10 events_written=50000 events_dropped=0 
  [METRICS] Per-thread: T1(w:5000,d:0) T2(w:5000,d:0) ...
  [METRICS] Drain: cycles=100 bytes=10MB latency_ms=2.3
  ```
- **Teardown**: N/A

### 5. Integration Tests

#### Test: `backpressure__full_system__then_degrades_gracefully`
- **Setup**: Complete tracer
- **Execute**:
  1. Start with normal load
  2. Increase to overload
  3. Throttle drain
  4. Monitor behavior
- **Verify**:
  - Gradual degradation
  - Metrics reflect state
  - No crashes/hangs
  - Recovery possible
- **Teardown**: Full cleanup

#### Test: `metrics__stress_accuracy__then_precise`
- **Setup**: Known workload generator
- **Execute**: 1M events across 10 threads
- **Verify**:
  - Total events match expected
  - Per-thread sums correct
  - Drop + written = generated
  - No lost events
- **Teardown**: Validate counts

### 6. Performance Impact Tests

#### Test: `metrics__overhead__then_minimal`
- **Setup**: Baseline without metrics
- **Execute**: Compare with metrics enabled
- **Verify**:
  - CPU overhead < 1%
  - Memory overhead < 100KB
  - No impact on event rate
  - Atomic ops optimized
- **Teardown**: Performance report

## Stress Test Scenarios
1. **Burst Storm**: 10M events in 1 second
2. **Sustained Overload**: 2x capacity for 60s
3. **Oscillating Load**: Sine wave pattern
4. **Thread Churn**: Threads starting/stopping
5. **I/O Starvation**: Disk at 100% usage

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Drop decision time | < 10ns | Per event |
| Metric update cost | < 5ns | Atomic increment |
| Reporting overhead | < 0.1% CPU | Metrics thread |
| Memory per thread | < 1KB | Metric storage |

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] Backpressure activates correctly
- [ ] Per-thread metrics isolated
- [ ] Drop accounting accurate
- [ ] Metrics reporting periodic
- [ ] No deadlocks under pressure
- [ ] Graceful degradation verified
- [ ] Recovery after overload
- [ ] ThreadSanitizer clean
- [ ] No memory leaks (ASAN)
- [ ] Performance targets met
- [ ] Coverage ≥ 100% on changed lines