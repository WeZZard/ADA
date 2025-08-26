# Backlogs — M1 E5 I4 Examples

## Sprint Overview
**Duration**: 3 days (24 hours)
**Priority**: P0 (Critical - user-facing documentation)
**Dependencies**: All M1 features (E1-E4) working

## Day 1: Basic and Threading Examples (8 hours)

### EX-001: Create hello_trace example (P0, 1h)
- [ ] Write hello_trace.c with simple function calls
- [ ] Add timing delays for visibility
- [ ] Include clear comments explaining each part
- [ ] Test compilation with gcc

### EX-002: Document basic tracing commands (P0, 1h)
- [ ] Write trace command with output options
- [ ] Document analyze command with summary
- [ ] Show expected output format
- [ ] Include interpretation guide

### EX-003: Create thread_trace example (P0, 2h)
- [ ] Implement multi-threaded example with 4 workers
- [ ] Use pthread for portability
- [ ] Add thread synchronization points
- [ ] Include thread-local work simulation

### EX-004: Document thread analysis commands (P0, 1h)
- [ ] Thread timeline visualization command
- [ ] Thread statistics command
- [ ] Concurrency analysis options
- [ ] Race condition detection hints

### EX-005: Create performance profiling example (P0, 2h)
- [ ] Implement bubble_sort (inefficient)
- [ ] Implement quicksort (efficient)
- [ ] Create comparison scenario
- [ ] Add measurable workload (1000 elements)

### EX-006: Document performance analysis (P0, 1h)
- [ ] Flamegraph generation command
- [ ] Hotspot analysis command
- [ ] Performance regression detection
- [ ] Optimization opportunity identification

## Day 2: Debug and Advanced Examples (8 hours)

### EX-007: Create memory debugging example (P0, 2h)
- [ ] Implement intentional memory leak
- [ ] Add use-after-free example
- [ ] Include double-free scenario (commented)
- [ ] Create leaked linked list

### EX-008: Document memory analysis commands (P0, 1h)
- [ ] Memory leak detection command
- [ ] Memory timeline visualization
- [ ] Allocation pattern analysis
- [ ] Memory pressure indicators

### EX-009: Create signal handling example (P0, 1.5h)
- [ ] Implement SIGINT handler with counter
- [ ] Implement SIGTERM handler with cleanup
- [ ] Add signal state tracking
- [ ural main loop with visible work

### EX-010: Document signal tracing (P0, 0.5h)
- [ ] Signal tracking command options
- [ ] Signal timeline analysis
- [ ] Handler execution tracing
- [ ] Signal masking effects

### EX-011: Create filter examples (P0, 1.5h)
- [ ] Function name filtering example
- [ ] Thread ID filtering example
- [ ] Time window filtering example
- [ ] Syscall exclusion example
- [ ] Sample-based tracing example

### EX-012: Create output format examples (P0, 1h)
- [ ] Binary ATF format example
- [ ] JSON format for integration
- [ ] CSV for spreadsheet analysis
- [ ] HTML report generation
- [ ] Text format for scripts

### EX-013: Create CI/CD integration example (P0, 1.5h)
- [ ] GitHub Actions workflow file
- [ ] Performance regression detection
- [ ] Artifact upload configuration
- [ ] Baseline comparison logic

## Day 3: Testing and Documentation (8 hours)

### EX-014: Test all examples compile (P0, 1h)
- [ ] Verify all C examples compile without warnings
- [ ] Test with -Wall -Wextra -Werror
- [ ] Ensure portability (Linux/macOS)
- [ ] Check for required dependencies

### EX-015: Test all tracing commands (P0, 2h)
- [ ] Execute each documented command
- [ ] Verify output files created
- [ ] Check output format validity
- [ ] Measure actual overhead

### EX-016: Create troubleshooting guide (P0, 2h)
- [ ] Document "no trace output" issue
- [ ] Document high overhead mitigation
- [ ] Document buffer exhaustion handling
- [ ] Document permission issues
- [ ] Include platform-specific solutions

### EX-017: Create quick reference card (P1, 1h)
- [ ] Common commands cheat sheet
- [ ] Filter syntax reference
- [ ] Output format options
- [ ] Performance tuning tips

### EX-018: Validate example outputs (P0, 1h)
- [ ] Run each example with tracing
- [ ] Capture actual output
- [ ] Update documentation with real output
- [ ] Verify accuracy of descriptions

### EX-019: Create example test suite (P0, 1h)
- [ ] Write automated test for each example
- [ ] Verify compilation success
- [ ] Check output correctness
- [ ] Measure performance overhead

## Acceptance Criteria Checklist
- [ ] 8 complete example programs
- [ ] All examples compile without warnings
- [ ] All examples run successfully
- [ ] All tracing commands documented
- [ ] Expected output shown for each example
- [ ] Troubleshooting covers common issues
- [ ] CI/CD integration example works
- [ ] Performance overhead < 5% for basic examples
- [ ] Memory debugging detects all issues
- [ ] Signal handling properly traced

## Risk Mitigation
| Risk | Impact | Mitigation |
|------|--------|------------|
| Example reveals ADA bug | High | Test thoroughly before documenting |
| Overhead exceeds targets | Medium | Provide sampling alternatives |
| Platform differences | Medium | Test on both Linux and macOS |
| Complex examples confuse users | Low | Start with simplest cases |

## Dependencies
- M1_E1: Agent injection working
- M1_E2: Index pipeline operational
- M1_E3: Metrics and backpressure functional
- M1_E4: Analysis tools complete
- M1_E5_I1-I3: Previous documentation done

## Success Metrics
- All examples execute without errors
- Test suite passes 100%
- Documentation coverage 100%
- User can follow examples independently
- Overhead within specified limits

## Notes
- Focus on clarity over complexity
- Each example should teach one concept
- Provide complete, runnable code
- Include expected output for validation
- Test on fresh environment