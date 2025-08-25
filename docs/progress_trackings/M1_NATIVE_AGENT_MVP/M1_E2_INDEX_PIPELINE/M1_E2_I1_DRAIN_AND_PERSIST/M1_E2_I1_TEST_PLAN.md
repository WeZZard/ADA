# Test Plan — M1 E2 I1 Drain and Persist

## Objective
Verify per-thread ring-pool swap-and-dump works for both lanes with correct persistence and bounded spares.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Drain Thread | ✓ | ✓ | ✓ |
| Per-Thread Lanes | ✓ | ✓ | ✓ |
| File Persistence | ✓ | ✓ | - |
| Pool Exhaustion | ✓ | ✓ | ✓ |
| Memory Ordering | ✓ | - | - |

## Test Execution Sequence
1. Unit Tests → 2. Integration Tests → 3. Stress Tests → 4. Performance Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| drain__thread_index_full__then_dumps | Full index ring | Single dump per thread | File contains thread's events |
| drain__thread_detail_marked__then_dumps | Full detail + mark | Single dump | Mark flag cleared |
| drain__thread_pool_exhausted__then_drops | All rings busy | Drop oldest | Counter incremented |
| drain__multi_thread__then_isolated | N threads writing | N separate dumps | No cross-thread mixing |

## Test Categories

### 1. Functional Tests

#### Test: `drain__per_thread_index__then_persists`
- **Setup**: Initialize 4 threads with index lanes
- **Execute**: Each thread fills its index ring
- **Verify**:
  - Each thread's ring dumped separately
  - Thread IDs preserved in events
  - File header contains correct thread_id
  - Event ordering maintained per thread
- **Teardown**: Clean trace files

#### Test: `drain__per_thread_detail__then_mark_policy`
- **Setup**: Initialize threads with detail lanes
- **Execute**: 
  1. Fill detail ring without mark → no dump
  2. Fill detail ring with mark → dump occurs
- **Verify**:
  - No dump without mark
  - Exactly one dump with mark
  - Mark flag cleared after dump
  - Thread isolation maintained
- **Teardown**: Reset lanes

#### Test: `drain__thread_pool_exhaustion__then_graceful`
- **Setup**: Limit to 2 spare rings per thread
- **Execute**: Throttle drain, fill all rings
- **Verify**:
  - `pool_exhaustion_count` increments per thread
  - Drop-oldest behavior per thread
  - Other threads continue unaffected
  - No deadlock or starvation
- **Teardown**: Restore normal drain rate

### 2. Synchronization Tests

#### Test: `drain__spsc_ordering__then_consistent`
- **Setup**: ThreadSanitizer enabled
- **Execute**: Multi-thread ring operations
- **Verify**:
  - ACQUIRE on queue tail read
  - RELEASE on queue tail write
  - No data races detected
  - Per-thread SPSC maintained
- **Teardown**: N/A

#### Test: `drain__ring_snapshot__then_atomic`
- **Setup**: Active ring with writes
- **Execute**: Take snapshot during writes
- **Verify**:
  - Snapshot internally consistent
  - No torn reads
  - Thread ID preserved
- **Teardown**: N/A

### 3. Performance Tests

#### Test: `drain__throughput_per_thread__then_scales`
- **Setup**: 1, 4, 8, 16 threads
- **Execute**: Measure events/sec per thread
- **Verify**:
  - Linear scaling with thread count
  - No degradation per thread
  - Target: 100K events/sec/thread
- **Teardown**: Report metrics

#### Test: `drain__latency_per_thread__then_bounded`
- **Setup**: Instrumented ring swap
- **Execute**: Measure swap latency
- **Verify**:
  - P50 < 100μs per thread
  - P99 < 1ms per thread
  - No cross-thread impact
- **Teardown**: Latency histogram

### 4. File Format Tests

#### Test: `persist__index_header__then_valid`
- **Setup**: Write index events
- **Execute**: Read file header
- **Verify**:
  ```c
  IndexFileHeader {
    magic: "ADAIDX1\0",
    version: 1,
    record_size: sizeof(IndexEvent),
    pid: <process_id>,
    session_id: <session>
  }
  ```
- **Teardown**: N/A

#### Test: `persist__detail_format__then_structured`
- **Setup**: Write detail events
- **Execute**: Parse dump structure
- **Verify**:
  ```c
  DetailDumpHeader {
    dump_size: <bytes>,
    timestamp: <when>,
    event_count: <count>,
    thread_id: <source_thread>
  }
  ```
- **Teardown**: N/A

### 5. Integration Tests

#### Test: `drain__full_pipeline__then_end_to_end`
- **Setup**: Complete tracer with test app
- **Execute**: 
  1. Spawn multi-threaded test_cli
  2. Run for 10 seconds
  3. Generate mixed load per thread
- **Verify**:
  - All threads registered
  - Events from each thread persisted
  - Files contain valid headers
  - No data loss under load
  - Clean shutdown
- **Teardown**: Validate output files

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Events/sec per thread | 100K | Via throughput test |
| Ring swap latency | < 1ms | P99 per thread |
| File write throughput | > 50MB/s | Sustained rate |
| Memory per thread | < 2MB | Ring pools + queues |
| CPU overhead | < 5% | Drain thread usage |

## Stress Test Scenarios
1. **Thread Storm**: Create 64 threads simultaneously
2. **Burst Load**: 1M events/sec burst per thread
3. **Slow I/O**: Throttle disk writes to 1MB/s
4. **Memory Pressure**: Limit available memory

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] ThreadSanitizer reports no races
- [ ] Per-thread isolation verified
- [ ] Linear scaling with thread count
- [ ] File format validation passes
- [ ] Pool exhaustion handled gracefully
- [ ] No memory leaks (Valgrind/ASAN)
- [ ] Performance targets met
- [ ] Coverage ≥ 100% on changed lines
