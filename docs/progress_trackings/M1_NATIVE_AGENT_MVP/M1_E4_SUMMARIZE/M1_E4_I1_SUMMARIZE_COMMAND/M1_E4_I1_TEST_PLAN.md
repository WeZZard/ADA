# Test Plan — M1 E4 I1 Summarize Command

## Objective
Validate the summarize command reads trace files, computes accurate aggregates, and presents clear summaries efficiently.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| File Parser | ✓ | ✓ | ✓ |
| Statistics Engine | ✓ | ✓ | - |
| Output Formatter | ✓ | ✓ | - |
| Error Handling | ✓ | ✓ | - |
| Memory Efficiency | - | ✓ | ✓ |

## Test Execution Sequence
1. Unit Tests → 2. Integration Tests → 3. Performance Tests → 4. Large File Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| summarize__valid_file__then_stats | index.ada | Complete statistics | All sections present |
| summarize__invalid_path__then_error | missing.ada | Error message | Clear error, exit 1 |
| summarize__large_file__then_fast | 1GB trace | Summary < 1s | Performance target met |
| summarize__per_thread__then_breakdown | Multi-thread trace | Thread statistics | Per-thread data shown |

## Test Categories

### 1. Basic Functionality Tests

#### Test: `summarize__index_file__then_complete_stats`
- **Setup**: Generate trace with known events
- **Execute**: `tracer summarize traces/test/index.ada`
- **Verify output contains**:
  ```
  === Trace Summary ===
  File: traces/test/index.ada
  Duration: 5.023s
  Total Events: 125,432
  Unique Functions: 47
  Threads: 4
  
  === Per-Thread Breakdown ===
  Thread 12345: 50,000 events (39.8%)
  Thread 12346: 35,432 events (28.2%)
  Thread 12347: 25,000 events (19.9%)
  Thread 12348: 15,000 events (12.0%)
  
  === Top Functions ===
  1. fibonacci (45,231 calls, 36.1%)
  2. process_file (23,456 calls, 18.7%)
  3. calculate_pi (12,345 calls, 9.8%)
  
  === Call Depth Statistics ===
  Max Depth: 127
  Avg Depth: 12.3
  Depth Distribution:
    0-10: 45%
    11-20: 30%
    21-50: 20%
    51+: 5%
  ```
- **Teardown**: N/A

#### Test: `summarize__detail_file__then_window_stats`
- **Setup**: Trace with detail events
- **Execute**: `tracer summarize traces/test/detail.ada`
- **Verify**:
  - Marked event windows identified
  - Window durations calculated
  - Stack snapshots summarized
  - Memory patterns shown
- **Teardown**: N/A

### 2. Error Handling Tests

#### Test: `summarize__missing_file__then_clear_error`
- **Setup**: No file at path
- **Execute**: `tracer summarize /nonexistent/file.ada`
- **Verify**:
  - Exit code = 1
  - Error: "File not found: /nonexistent/file.ada"
  - Suggestion to check path
- **Teardown**: N/A

#### Test: `summarize__corrupted_file__then_diagnostic`
- **Setup**: Truncated/corrupted trace
- **Execute**: `tracer summarize corrupted.ada`
- **Verify**:
  - Detects corruption
  - Reports last valid position
  - Partial results if possible
  - Clear corruption indicator
- **Teardown**: N/A

#### Test: `summarize__wrong_format__then_identifies`
- **Setup**: Non-ADA file
- **Execute**: `tracer summarize random.bin`
- **Verify**:
  - Detects invalid magic
  - Reports "Not an ADA trace file"
  - Suggests correct format
- **Teardown**: N/A

### 3. Performance Tests

#### Test: `performance__large_file__then_sub_second`
- **Setup**: 1GB trace file
- **Execute**: Time summarize command
- **Verify**:
  - Completes < 1 second
  - Memory usage < 100MB
  - No full file load
  - Streaming processing
- **Teardown**: Performance report

#### Test: `performance__many_threads__then_scales`
- **Setup**: Trace with 64 threads
- **Execute**: Summarize multi-thread trace
- **Verify**:
  - Linear time complexity
  - All threads processed
  - No thread limit hit
- **Teardown**: Scaling analysis

### 4. Output Format Tests

#### Test: `format__json_output__then_parseable`
- **Setup**: Standard trace
- **Execute**: `tracer summarize --format json trace.ada`
- **Verify JSON structure:
  ```json
  {
    "summary": {
      "duration_ms": 5023,
      "total_events": 125432,
      "threads": [
        {"id": 12345, "events": 50000}
      ],
      "top_functions": [
        {"name": "fibonacci", "count": 45231}
      ]
    }
  }
  ```
- **Teardown**: JSON validation

#### Test: `format__csv_export__then_importable`
- **Setup**: Trace file
- **Execute**: `tracer summarize --format csv trace.ada`
- **Verify**:
  - Valid CSV headers
  - Proper escaping
  - Importable to Excel
- **Teardown**: CSV validation

### 5. Advanced Statistics Tests

#### Test: `stats__percentiles__then_accurate`
- **Setup**: Known distribution
- **Execute**: Calculate percentiles
- **Verify**:
  - P50, P95, P99 correct
  - Min/max accurate
  - Standard deviation
- **Teardown**: Statistical validation

#### Test: `stats__time_series__then_bucketed`
- **Setup**: Long trace
- **Execute**: Time-based analysis
- **Verify**:
  - Events per second
  - Rate changes detected
  - Burst patterns identified
- **Teardown**: Time series plot

### 6. Integration Tests

#### Test: `integration__pipeline_output__then_summarizable`
- **Setup**: Full tracer run
- **Execute**: 
  1. Trace application
  2. Summarize output
- **Verify**:
  - Files parseable
  - Stats match reality
  - No data corruption
- **Teardown**: End-to-end validation

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| 100MB file | < 100ms | Summarize time |
| 1GB file | < 1s | Summarize time |
| 10GB file | < 10s | Summarize time |
| Memory usage | < 100MB | Peak RSS |
| CPU efficiency | > 90% | Single core usage |

## Edge Cases
1. **Empty file**: Handle gracefully
2. **Single event**: Correct statistics
3. **Huge depth**: No stack overflow
4. **Unicode functions**: Proper display
5. **Interrupted file**: Partial results

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] Basic statistics accurate
- [ ] Per-thread breakdown correct
- [ ] Error messages helpful
- [ ] Performance targets met
- [ ] Large files handled efficiently
- [ ] Output formats validated
- [ ] No memory leaks (Valgrind)
- [ ] Coverage ≥ 100% on changed lines