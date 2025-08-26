# M1_E3_I3 Backlogs: Metrics Reporting

## Overview
Implementation of periodic metrics reporting for visibility and monitoring of the ADA tracing system. This iteration provides real-time observability through configurable reporting intervals, multiple output formats, and comprehensive metrics aggregation.

## Dependencies
- **M1_E3_I2**: Metrics collection infrastructure complete
- **M1_E1_I1**: ThreadRegistry for thread enumeration
- **M1_E2_I1**: Drain thread for consumption metrics

## Success Criteria
- [ ] Reporter thread manages periodic reporting
- [ ] Human-readable format with rates and percentages
- [ ] JSON output for tooling integration
- [ ] Configurable 5-second default interval
- [ ] Summary report on shutdown
- [ ] Zero impact on trace performance
- [ ] stderr output (non-interfering)

## Tasks

### 1. Infrastructure Setup (2 hours)

#### 1.1 Create Module Structure
- [ ] Create `reporting/` directory
- [ ] Add `metrics_reporter.h` header
- [ ] Add `metrics_reporter.c` implementation
- [ ] Add `formatter.h` for output formatting
- [ ] Add `formatter.c` implementation
- [ ] Update CMakeLists.txt

**Acceptance**: Module compiles with skeleton implementation

#### 1.2 Define Core Data Structures
- [ ] Define `ReporterConfig` structure
- [ ] Define `AggregatedMetrics` structure
- [ ] Define `MetricsReporter` state
- [ ] Define format options structures
- [ ] Add atomic state fields

**Acceptance**: All structures defined with proper alignment

### 2. Reporter Thread Implementation (4 hours)

#### 2.1 Thread Lifecycle Management
- [ ] Implement `metrics_reporter_init()`
- [ ] Implement `metrics_reporter_init_with_config()`
- [ ] Create reporter thread with pthread
- [ ] Implement thread main loop
- [ ] Add interval timing logic
- [ ] Implement shutdown signaling

**Acceptance**: Thread starts and stops cleanly

#### 2.2 Thread Synchronization
- [ ] Add mutex for state protection
- [ ] Add condition variable for signaling
- [ ] Implement timed wait for intervals
- [ ] Add pause/resume capability
- [ ] Implement force report trigger

**Acceptance**: Thread responds to control signals

### 3. Metrics Collection (3 hours)

#### 3.1 Thread Enumeration
- [ ] Implement `collect_all_thread_metrics()`
- [ ] Iterate ThreadRegistry entries
- [ ] Check thread active status
- [ ] Get per-thread snapshots
- [ ] Handle inactive threads

**Acceptance**: All active threads enumerated

#### 3.2 Metrics Aggregation
- [ ] Sum total traces across threads
- [ ] Sum total drops across threads
- [ ] Sum total bytes across threads
- [ ] Store per-thread breakdown
- [ ] Add timestamp to collection

**Acceptance**: Accurate aggregation of all metrics

#### 3.3 History Management
- [ ] Implement history ring buffer
- [ ] Store previous metrics for rates
- [ ] Calculate time intervals
- [ ] Manage ring buffer wraparound

**Acceptance**: Rate calculation has valid history

### 4. Output Formatting (4 hours)

#### 4.1 Human-Readable Format
- [ ] Implement `format_human_output()`
- [ ] Add header with timestamp
- [ ] Format global summary section
- [ ] Calculate and display drop percentages
- [ ] Format per-thread breakdown table
- [ ] Add column alignment

**Acceptance**: Clean, readable text output

#### 4.2 Rate Calculations
- [ ] Implement `calculate_rates()`
- [ ] Calculate traces per second
- [ ] Calculate bytes per second
- [ ] Calculate drops per second
- [ ] Handle first report (no previous)
- [ ] Format rate displays

**Acceptance**: Accurate rate calculations

#### 4.3 JSON Format
- [ ] Implement `format_json_output()`
- [ ] Add timestamp and sequence fields
- [ ] Format global metrics object
- [ ] Add rates to JSON when available
- [ ] Format thread array
- [ ] Ensure valid JSON syntax

**Acceptance**: Valid, parseable JSON output

### 5. Output Handling (2 hours)

#### 5.1 stderr Output
- [ ] Configure stderr as default stream
- [ ] Implement buffered writes
- [ ] Handle write failures gracefully
- [ ] Ensure non-blocking output
- [ ] Add output throttling if needed

**Acceptance**: Clean stderr output without blocking

#### 5.2 JSON File Output
- [ ] Add JSON file path configuration
- [ ] Implement file opening/creation
- [ ] Write JSON to file atomically
- [ ] Handle file write errors
- [ ] Implement file rotation (optional)

**Acceptance**: JSON file writing works correctly

### 6. Shutdown Summary (2 hours)

#### 6.1 Summary Generation
- [ ] Implement `generate_summary_report()`
- [ ] Collect final metrics snapshot
- [ ] Calculate total runtime
- [ ] Generate comprehensive statistics
- [ ] Format shutdown summary

**Acceptance**: Complete summary on shutdown

#### 6.2 Cleanup Coordination
- [ ] Signal reporter thread to stop
- [ ] Wait for final report completion
- [ ] Free allocated resources
- [ ] Close any open files
- [ ] Join reporter thread with timeout

**Acceptance**: Clean shutdown without hangs

### 7. Configuration Management (2 hours)

#### 7.1 Runtime Configuration
- [ ] Implement `metrics_reporter_set_interval()`
- [ ] Implement `metrics_reporter_set_format()`
- [ ] Implement `metrics_reporter_enable_json_file()`
- [ ] Add configuration validation
- [ ] Apply changes without restart

**Acceptance**: Configuration changes apply immediately

#### 7.2 Control Operations
- [ ] Implement `metrics_reporter_pause()`
- [ ] Implement `metrics_reporter_resume()`
- [ ] Implement `metrics_reporter_force_report()`
- [ ] Add state transition validation

**Acceptance**: All control operations work correctly

### 8. Testing Implementation (6 hours)

#### 8.1 Unit Tests (2 hours)
- [ ] Reporter initialization tests (5 tests)
- [ ] Thread lifecycle tests (4 tests)
- [ ] Collection logic tests (6 tests)
- [ ] Format generation tests (8 tests)
- [ ] Rate calculation tests (4 tests)
- [ ] Configuration tests (5 tests)

**Acceptance**: All unit tests pass with 100% coverage

#### 8.2 Integration Tests (2 hours)
- [ ] Multi-thread aggregation test
- [ ] Configuration change test
- [ ] Output handling test
- [ ] Shutdown summary test
- [ ] Error recovery test

**Acceptance**: Integration tests verify component interaction

#### 8.3 Performance Tests (2 hours)
- [ ] Reporter overhead benchmark
- [ ] Format performance benchmark
- [ ] Memory usage validation
- [ ] CPU usage measurement
- [ ] Stress test with 64 threads

**Acceptance**: Performance meets requirements

### 9. Documentation (1 hour)

#### 9.1 API Documentation
- [ ] Document all public functions
- [ ] Add usage examples
- [ ] Document configuration options
- [ ] Add format specifications

**Acceptance**: Complete API documentation

#### 9.2 Integration Guide
- [ ] Document integration points
- [ ] Add configuration examples
- [ ] Document output formats
- [ ] Add troubleshooting section

**Acceptance**: Clear integration guidance

## Time Estimates

| Phase | Task | Estimate | Actual |
|-------|------|----------|--------|
| **Setup** | Infrastructure | 2h | - |
| **Core** | Reporter Thread | 4h | - |
| **Collection** | Metrics Aggregation | 3h | - |
| **Formatting** | Output Generation | 4h | - |
| **Output** | Stream Handling | 2h | - |
| **Shutdown** | Summary & Cleanup | 2h | - |
| **Config** | Runtime Management | 2h | - |
| **Testing** | Complete Test Suite | 6h | - |
| **Docs** | Documentation | 1h | - |
| **Total** | | **26h** | - |

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Thread timing drift | Medium | Use monotonic clock, periodic recalibration |
| Output buffer overflow | Low | Pre-allocate buffers, truncate if needed |
| Reporter thread hang | Medium | Timeout on shutdown, force termination |
| Format performance | Low | Optimize hot paths, cache computations |
| JSON validity | Low | Careful escaping, validation tests |

## Definition of Done

### Code Complete
- [ ] All functions implemented
- [ ] All tests written and passing
- [ ] No compiler warnings
- [ ] Memory leak free (Valgrind clean)
- [ ] Thread sanitizer clean

### Performance Validated
- [ ] < 1% overhead on trace path
- [ ] < 100μs format generation
- [ ] < 1MB memory usage
- [ ] Stable over 24-hour run

### Documentation Complete
- [ ] Technical design reviewed
- [ ] Test plan executed
- [ ] API documentation complete
- [ ] Integration guide written

### Quality Gates
- [ ] 100% code coverage on changes
- [ ] All integration tests pass
- [ ] Performance benchmarks pass
- [ ] Code review approved

## Notes

### Design Decisions
1. **Single reporter thread**: Simpler than multiple reporters, sufficient for monitoring
2. **5-second default interval**: Balance between visibility and overhead
3. **stderr output**: Doesn't interfere with stdout trace data
4. **JSON option**: Enables tooling integration and automation

### Future Enhancements
1. **Prometheus export**: Direct metrics export format
2. **Network reporting**: Send metrics to monitoring service
3. **Custom formatters**: Plugin architecture for formats
4. **Adaptive intervals**: Adjust based on activity level
5. **Metric alerting**: Threshold-based alerts

### Integration Considerations
- Reporter thread must not impact trace performance
- Metrics collection must be read-only
- Output must not block trace operations
- Configuration changes must be atomic
- Shutdown must complete within timeout