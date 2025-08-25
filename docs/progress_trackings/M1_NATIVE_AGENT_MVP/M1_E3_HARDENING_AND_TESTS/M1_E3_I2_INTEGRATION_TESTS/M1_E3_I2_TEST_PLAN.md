# Test Plan — M1 E3 I2 Integration Tests

## Objective
End-to-end verification of complete tracer system with fixtures, deterministic assertions, and validated artifacts.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Spawn Mode | - | ✓ | ✓ |
| Attach Mode | - | ✓ | ✓ |
| Full Pipeline | - | ✓ | ✓ |
| Multi-Thread Apps | - | ✓ | ✓ |
| File Validation | - | ✓ | - |

## Test Execution Sequence
1. Component Tests → 2. Mode Tests → 3. Pipeline Tests → 4. Stress Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| integration__spawn_mode__then_traces | test_cli app | Valid trace files | Headers valid, events present |
| integration__attach_mode__then_hooks | Running process | Hooks installed | Clean attach/detach |
| integration__multi_thread__then_isolated | Threaded app | Per-thread data | Thread isolation verified |
| integration__full_pipeline__then_e2e | Complex workload | Complete traces | All components working |

## Test Categories

### 1. Spawn Mode Integration

#### Test: `spawn__test_cli__then_complete_trace`
- **Setup**: Build test fixtures
- **Execute**: 
  ```bash
  tracer spawn ./target/debug/test_cli \
    --duration 5 \
    --output traces/run1 \
    --stack-bytes 128
  ```
- **Verify**:
  - Process spawns successfully
  - Agent injection confirmed
  - Events captured for 5 seconds
  - Files created:
    - `traces/run1/index.ada`
    - `traces/run1/detail.ada`
  - File headers valid
  - Event count > 1000
  - Clean shutdown
- **Teardown**: Archive trace files

#### Test: `spawn__test_runloop__then_async_events`
- **Setup**: Build runloop fixture
- **Execute**: `tracer spawn ./target/debug/test_runloop --duration 10`
- **Verify**:
  - Timer events captured
  - Signal handler events present
  - Dispatch work events logged
  - Multiple threads tracked
  - Async patterns preserved
- **Teardown**: Validate event ordering

### 2. Attach Mode Integration

#### Test: `attach__running_process__then_non_intrusive`
- **Setup**: Start test_cli in background
- **Execute**:
  ```bash
  ./test_cli &
  PID=$!
  tracer attach $PID --duration 3
  ```
- **Verify**:
  - Attach without stopping process
  - Hooks installed dynamically
  - Events captured immediately
  - Detach leaves process running
  - No crashes or hangs
- **Teardown**: Kill test process

#### Test: `attach__multi_thread__then_all_threads`
- **Setup**: Start multi-threaded app
- **Execute**: Attach to running app
- **Verify**:
  - All threads detected
  - Each thread gets lanes
  - Events from all threads
  - Thread IDs preserved
- **Teardown**: Stop application

### 3. Full Pipeline Tests

#### Test: `pipeline__spawn_to_persist__then_complete`
- **Setup**: Clean environment
- **Execute**: Full spawn → trace → persist flow
- **Verify each stage:
  1. **Spawn**: Process starts with agent
  2. **Hook**: Functions instrumented
  3. **Capture**: Events in ring buffers
  4. **Drain**: Per-thread drain active
  5. **Persist**: Files written correctly
  6. **Shutdown**: Clean termination
- **Teardown**: Validate all outputs

#### Test: `pipeline__high_volume__then_handles_load`
- **Setup**: Load generator app
- **Execute**: Trace with 1M events/sec
- **Verify**:
  - No pipeline stalls
  - Backpressure activates if needed
  - Files grow continuously
  - Metrics show throughput
  - No memory leaks
- **Teardown**: Performance report

### 4. Multi-Thread Integration

#### Test: `threads__isolation__then_no_interference`
- **Setup**: App with 10 threads
- **Execute**: Trace all threads
- **Verify**:
  - Each thread registered
  - Separate ring pools
  - No cross-contamination
  - Per-thread statistics
  - Linear scaling
- **Teardown**: Thread report

#### Test: `threads__dynamic__then_handles_churn`
- **Setup**: App creating/destroying threads
- **Execute**: Trace during churn
- **Verify**:
  - New threads registered
  - Dead threads cleaned up
  - No resource leaks
  - Stable operation
- **Teardown**: Resource audit

### 5. File Format Validation

#### Test: `format__index_file__then_parseable`
- **Setup**: Generate trace
- **Execute**: Parse index file
- **Verify structure:
  ```c
  // Header (32 bytes)
  magic[8] == "ADAIDX1\0"
  version == 1
  record_size == sizeof(IndexEvent)
  
  // Records follow
  IndexEvent records[N]
  ```
- **Teardown**: N/A

#### Test: `format__detail_file__then_structured`
- **Setup**: Generate trace with marks
- **Execute**: Parse detail file
- **Verify**:
  - Dump headers present
  - Event batches valid
  - Thread IDs in headers
  - Timestamps monotonic
- **Teardown**: N/A

### 6. Error Recovery Tests

#### Test: `recovery__disk_full__then_graceful`
- **Setup**: Fill disk to 95%
- **Execute**: Run tracer
- **Verify**:
  - Detects disk full
  - Stops writing cleanly
  - Reports error clearly
  - Process continues
- **Teardown**: Free disk space

#### Test: `recovery__permission_denied__then_informative`
- **Setup**: Read-only output dir
- **Execute**: Attempt tracing
- **Verify**:
  - Clear error message
  - Suggests fix
  - No crash
- **Teardown**: Fix permissions

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| End-to-end latency | < 1ms | Event to disk |
| Sustained throughput | > 100K/sec | Events per second |
| Memory usage | < 50MB | Total RSS |
| CPU overhead | < 10% | Tracer overhead |

## Stress Test Scenarios
1. **Long Duration**: Run for 1 hour continuously
2. **Many Processes**: Trace 10 processes simultaneously  
3. **Large Binary**: Trace Chrome/Firefox
4. **Rapid Spawn**: Start/stop 100 times
5. **System Load**: Trace under CPU/memory pressure

## Deterministic Assertions
```python
def validate_trace(trace_dir):
    # Fixed assertions for test_cli
    assert event_count("fibonacci") >= 100
    assert event_count("process_file") >= 10
    assert thread_count() >= 1
    
    # File size checks
    assert index_size() > 1024  # At least 1KB
    assert header_valid()
    
    # Timing assertions
    assert duration() == requested_duration ± 0.1
```

## Acceptance Criteria Checklist
- [ ] All spawn mode tests pass
- [ ] All attach mode tests pass
- [ ] Pipeline end-to-end verified
- [ ] Multi-thread support confirmed
- [ ] File formats validated
- [ ] Error recovery tested
- [ ] Performance targets met
- [ ] No memory leaks (Valgrind)
- [ ] No data races (ThreadSanitizer)
- [ ] Deterministic assertions pass
- [ ] Coverage ≥ 100% on integration paths