# Test Plan — M1 E2 I2 CLI Flags and Shutdown

## Objective
Verify CLI flags work correctly and shutdown synchronization ensures clean termination with all per-thread data flushed.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| CLI Parser | ✓ | ✓ | - |
| Output Path | ✓ | ✓ | - |
| Duration Timer | ✓ | ✓ | - |
| Shutdown Sync | ✓ | ✓ | ✓ |
| Thread Flush | ✓ | ✓ | ✓ |
| Signal Handling | ✓ | ✓ | - |

## Test Execution Sequence
1. Unit Tests → 2. Integration Tests → 3. Signal Tests → 4. Race Condition Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| cli__output_flag__then_creates_dir | --output /tmp/traces | Files in /tmp/traces | Directory created, files present |
| cli__duration_flag__then_exits | --duration 5 | Exit after 5s | Clean exit at time limit |
| shutdown__sigint__then_flushes | SIGINT signal | All threads flush | No data loss |
| shutdown__per_thread__then_synchronized | Multi-thread app | Each thread's data saved | All events persisted |

## Test Categories

### 1. CLI Flag Tests

#### Test: `cli__output_path__then_uses_directory`
- **Setup**: Clean test directory
- **Execute**: `tracer spawn test_app --output /tmp/ada_test`
- **Verify**:
  - Directory `/tmp/ada_test` created
  - Index file: `/tmp/ada_test/index.ada`
  - Detail file: `/tmp/ada_test/detail.ada`
  - Permissions: rw-r--r--
- **Teardown**: Remove test directory

#### Test: `cli__duration__then_timed_shutdown`
- **Setup**: Long-running test app
- **Execute**: `tracer spawn test_app --duration 3`
- **Verify**:
  - Process runs for 3 seconds ±100ms
  - Automatic shutdown triggered
  - All threads flushed before exit
  - Summary statistics printed
- **Teardown**: Clean trace files

#### Test: `cli__stack_bytes__then_captures_size`
- **Setup**: Test app with deep stacks
- **Execute**: `tracer spawn test_app --stack-bytes 256`
- **Verify**:
  - Stack snapshots are 256 bytes
  - No buffer overflows
  - Stack data preserved per event
- **Teardown**: N/A

### 2. Shutdown Synchronization Tests

#### Test: `shutdown__phase_ordering__then_no_loss`
- **Setup**: Multi-threaded test app
- **Execute**: Trigger shutdown sequence
- **Verify phases in order:
  ```c
  // Phase 1: Stop accepting
  accepting_events = false (RELEASE)
  // Phase 2: Flush all threads
  flush_all_thread_rings()
  // Phase 3: Signal drain
  shutdown_requested = true (RELEASE)
  // Phase 4: Join drain thread
  pthread_join(drain_thread)
  // Phase 5: Final sync
  fsync(index_fd); fsync(detail_fd)
  ```
- **Teardown**: Verify all data persisted

#### Test: `shutdown__per_thread_flush__then_complete`
- **Setup**: 10 threads with partial rings
- **Execute**: Initiate shutdown
- **Verify**:
  - Each thread's active ring flushed
  - Submit queues drained per thread
  - No events lost from any thread
  - Thread isolation maintained
- **Teardown**: Count events per thread

#### Test: `shutdown__marked_detail__then_dumps`
- **Setup**: Detail lane with marked events
- **Execute**: Shutdown with marked flag set
- **Verify**:
  - Marked detail lanes dump on shutdown
  - Unmarked lanes don't dump
  - Per-thread marking respected
- **Teardown**: Validate detail file

### 3. Signal Handling Tests

#### Test: `signal__sigint__then_graceful`
- **Setup**: Running tracer
- **Execute**: Send SIGINT (Ctrl+C)
- **Verify**:
  - Signal handler sets flag atomically
  - Shutdown sequence initiated
  - No partial writes
  - Summary printed to stderr
- **Teardown**: Check exit code = 0

#### Test: `signal__sigterm__then_clean_exit`
- **Setup**: Running tracer
- **Execute**: Send SIGTERM
- **Verify**:
  - Same as SIGINT behavior
  - SA_RESTART prevents EINTR
  - All threads notified
- **Teardown**: Validate files

#### Test: `signal__during_io__then_continues`
- **Setup**: Slow disk I/O
- **Execute**: Signal during write
- **Verify**:
  - SA_RESTART flag works
  - Write completes or retries
  - No corruption
- **Teardown**: File integrity check

### 4. Race Condition Tests

#### Test: `race__event_during_shutdown__then_rejected`
- **Setup**: High-frequency events
- **Execute**: Shutdown during burst
- **Verify**:
  - Events after `accepting_events=false` dropped
  - Each thread checks flag (ACQUIRE)
  - Clean cutoff point
- **Teardown**: N/A

#### Test: `race__new_thread_during_shutdown__then_blocked`
- **Setup**: Thread creation loop
- **Execute**: Shutdown while creating threads
- **Verify**:
  - No new registrations after shutdown
  - Existing threads handled
  - Thread count stable
- **Teardown**: N/A

#### Test: `race__ring_swap_during_flush__then_consistent`
- **Setup**: Active ring swaps
- **Execute**: Flush during swap
- **Verify**:
  - Atomic index capture per thread
  - No double-dump
  - No missing rings
- **Teardown**: Ring accounting

### 5. Integration Tests

#### Test: `shutdown__full_lifecycle__then_clean`
- **Setup**: Complete system
- **Execute**:
  1. Spawn with all flags
  2. Generate multi-thread load
  3. Trigger shutdown (duration or signal)
- **Verify**:
  - All CLI flags respected
  - Per-thread data complete
  - Files properly formatted
  - Metrics accurate
  - No resource leaks
- **Teardown**: Full validation

### 6. Memory Ordering Tests

#### Test: `ordering__shutdown_flags__then_synchronized`
- **Setup**: ThreadSanitizer enabled
- **Execute**: Concurrent shutdown paths
- **Verify**:
  - `accepting_events`: RELEASE/ACQUIRE
  - `shutdown_requested`: RELEASE/ACQUIRE  
  - Per-thread queue ops: proper fences
  - No data races detected
- **Teardown**: TSan report

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Shutdown latency | < 100ms | Time to flush all threads |
| Per-thread flush | < 10ms | Time per thread |
| Signal response | < 1ms | Flag set time |
| File sync time | < 50ms | fsync() duration |

## Stress Test Scenarios
1. **Shutdown under load**: 1M events/sec during shutdown
2. **Many threads**: Shutdown with 64 active threads
3. **Slow I/O**: Shutdown with throttled disk
4. **Rapid signals**: Multiple SIGINTs quickly

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] CLI flags work as specified
- [ ] Duration timer accurate to ±100ms
- [ ] Signal handling is graceful
- [ ] Per-thread shutdown synchronized
- [ ] No data loss on shutdown
- [ ] ThreadSanitizer clean
- [ ] Memory leaks: none (ASAN)
- [ ] Files contain all events
- [ ] Summary statistics accurate
- [ ] Coverage ≥ 100% on changed lines