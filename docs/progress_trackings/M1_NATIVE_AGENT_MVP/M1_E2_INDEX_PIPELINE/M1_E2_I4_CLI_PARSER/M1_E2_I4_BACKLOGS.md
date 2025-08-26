# Backlogs — M1 E2 I4 CLI Parser

## Iteration Goal
Implement command-line interface with robust argument parsing for tracer commands, supporting both spawn and attach modes with configurable output, duration, stack capture, selective persistence, trigger conditions, and module exclusions.

## Task Breakdown

### Day 1: Core Parser Implementation

#### Morning Session (4 hours)
1. **Task: Parser Structure Setup** [2 hours]
   - [ ] Create cli_parser.h with data structures
   - [ ] Define ExecutionMode enum
   - [ ] Define TracerConfig structure
   - [ ] Define CLIParser structure
   - [ ] Define FlagDefinition structure
   - [ ] Set up flag registry array
   - Priority: P0
   - Dependencies: None

2. **Task: Mode Detection Logic** [2 hours]
   - [ ] Implement cli_parser_create()
   - [ ] Implement cli_detect_mode()
   - [ ] Handle spawn/attach keywords
   - [ ] Handle help/version flags
   - [ ] Add error reporting for invalid modes
   - Priority: P0
   - Dependencies: Parser Structure

#### Afternoon Session (4 hours)
3. **Task: Positional Argument Parsing** [2 hours]
   - [ ] Implement cli_parse_mode_args()
   - [ ] Extract executable for spawn mode
   - [ ] Extract PID for attach mode
   - [ ] Collect remaining arguments
   - [ ] Handle missing required arguments
   - Priority: P0
   - Dependencies: Mode Detection

4. **Task: Flag Parsing Engine** [2 hours]
   - [ ] Implement cli_parse_flags()
   - [ ] Process long form flags (--flag)
   - [ ] Process short form flags (-f)
   - [ ] Handle flag=value syntax
   - [ ] Handle flag value syntax
   - [ ] Detect unknown flags
   - [ ] Support multiple --trigger flags
   - Priority: P0
   - Dependencies: Positional Arguments

### Day 2: Validation and Error Handling

#### Morning Session (4 hours)
5. **Task: Basic Flag Handlers** [2 hours]
   - [ ] Implement handle_output_flag()
   - [ ] Implement handle_duration_flag()
   - [ ] Implement handle_stack_flag()
   - [ ] Implement handle_help_flag()
   - [ ] Implement handle_version_flag()
   - [ ] Parse and store values in config
   - Priority: P0
   - Dependencies: Flag Parsing Engine

5a. **Task: Advanced Flag Handlers** [2 hours]
    - [ ] Implement handle_pre_roll_flag()
    - [ ] Implement handle_post_roll_flag()
    - [ ] Implement handle_trigger_flag()
    - [ ] Implement handle_exclude_flag()
    - [ ] Parse trigger conditions (symbol=, crash, time=)
    - [ ] Parse comma-separated module lists
    - [ ] Support multiple trigger accumulation
    - Priority: P0
    - Dependencies: Basic Flag Handlers

6. **Task: Basic Validation Functions** [2 hours]
   - [ ] Implement validate_executable_path()
   - [ ] Implement validate_output_directory()
   - [ ] Implement validate_pid()
   - [ ] Implement validate_duration()
   - [ ] Implement validate_stack_bytes() (0-512 range)
   - [ ] Add permission checks
   - Priority: P0
   - Dependencies: Advanced Flag Handlers

6a. **Task: Advanced Validation Functions** [2 hours]
    - [ ] Implement validate_persistence_settings()
    - [ ] Implement validate_trigger_config()
    - [ ] Implement validate_module_names()
    - [ ] Validate symbol name formats
    - [ ] Validate time ranges for triggers
    - [ ] Check for duplicate triggers
    - Priority: P0
    - Dependencies: Basic Validation Functions

#### Afternoon Session (4 hours)
7. **Task: Error Handling and Messages** [2 hours]
   - [ ] Create error message templates
   - [ ] Implement cli_print_usage()
   - [ ] Implement cli_print_error()
   - [ ] Implement cli_print_examples()
   - [ ] Format help text properly
   - [ ] Add version information
   - Priority: P0
   - Dependencies: Validation Functions

8. **Task: Config Management** [3 hours]
   - [ ] Implement cli_config_set_defaults()
   - [ ] Implement cli_get_config()
   - [ ] Implement cli_config_destroy()
   - [ ] Add config validation logic
   - [ ] Handle memory cleanup
   - [ ] Initialize trigger arrays dynamically
   - [ ] Manage module exclusion lists
   - [ ] Handle complex structure cleanup
   - Priority: P0
   - Dependencies: Advanced Validation Functions

### Day 3: Integration and Core Testing

#### Morning Session (4 hours)
9. **Task: Unit Tests - Core Parser** [3 hours]
   - [ ] Write mode detection tests (8 tests)
   - [ ] Write basic flag parsing tests (20 tests)
   - [ ] Write persistence flag tests (8 tests)
   - [ ] Write trigger parsing tests (12 tests)
   - [ ] Write exclusion parsing tests (6 tests)
   - [ ] Write validation tests (16 tests)
   - [ ] Write error handling tests (12 tests)
   - [ ] Achieve 100% coverage
   - Priority: P0
   - Dependencies: Core Implementation

10. **Task: Unit Tests - Edge Cases** [3 hours]
    - [ ] Test empty arguments
    - [ ] Test malformed flags
    - [ ] Test missing values
    - [ ] Test invalid paths
    - [ ] Test unicode handling
    - [ ] Test path traversal prevention
    - [ ] Test complex flag combinations (10 tests)
    - [ ] Test trigger edge cases (scoped symbols, zero times)
    - [ ] Test module name validation
    - [ ] Test memory limits and cleanup
    - Priority: P0
    - Dependencies: Core Implementation

#### Afternoon Session (4 hours)
11. **Task: Integration with Tracer** [3 hours]
    - [ ] Implement cli_to_tracer_params()
    - [ ] Add thread-safe config handoff
    - [ ] Implement cli_publish_config()
    - [ ] Implement cli_consume_config()
    - [ ] Convert persistence time units (seconds to milliseconds)
    - [ ] Transfer trigger arrays safely
    - [ ] Transfer module exclusion lists
    - [ ] Test with existing tracer code
    - Priority: P0
    - Dependencies: Config Management

12. **Task: Integration Tests** [3 hours]
    - [ ] Write spawn mode tests (8 tests)
    - [ ] Write attach mode tests (8 tests)
    - [ ] Write config handoff tests (6 tests)
    - [ ] Write trigger integration tests (8 tests)
    - [ ] Test complex command lines
    - [ ] Test all flag combinations
    - [ ] Test persistence configuration transfer
    - [ ] Test trigger array transfer
    - [ ] Test module exclusion transfer
    - Priority: P0
    - Dependencies: Tracer Integration

### Day 4: Performance, Security, and Documentation

#### Morning Session (4 hours)
13. **Task: Performance Optimization** [3 hours]
    - [ ] Profile parsing performance
    - [ ] Optimize string operations
    - [ ] Reduce memory allocations
    - [ ] Optimize trigger array operations
    - [ ] Implement fast path for common cases
    - [ ] Verify < 1ms parse time for basic commands
    - [ ] Verify < 10ms for complex commands with many triggers
    - Priority: P1
    - Dependencies: All Tests Pass

14. **Task: Security Hardening** [3 hours]
    - [ ] Add input sanitization
    - [ ] Prevent buffer overflows
    - [ ] Validate all numeric ranges (stack-bytes: 0-512)
    - [ ] Check file permissions properly
    - [ ] Add privilege verification
    - [ ] Sanitize symbol names and module names
    - [ ] Prevent injection through trigger parameters
    - [ ] Validate comma-separated lists safely
    - [ ] Add bounds checking for dynamic arrays
    - Priority: P0
    - Dependencies: Performance Tests

#### Afternoon Session (4 hours)
15. **Task: Performance and Security Tests** [3 hours]
    - [ ] Write performance benchmarks (4 tests)
    - [ ] Memory usage tests (3 tests)
    - [ ] Write security tests (8 tests)
    - [ ] Test injection attempts
    - [ ] Test trigger parameter injection
    - [ ] Test module name injection
    - [ ] Run Valgrind checks
    - [ ] Test memory cleanup with complex configurations
    - Priority: P0
    - Dependencies: Security Hardening

16. **Task: Documentation and Cleanup** [3 hours]
    - [ ] Update README with CLI usage
    - [ ] Document all flags and options
    - [ ] Add examples to help text including new flags
    - [ ] Document trigger syntax and formats
    - [ ] Document module exclusion patterns
    - [ ] Add usage examples for complex scenarios
    - [ ] Clean up code comments
    - [ ] Verify 100% coverage
    - Priority: P1
    - Dependencies: All Tests Pass

## Testing Checklist

### Unit Testing
- [ ] Mode detection (8 tests)
- [ ] Basic flag parsing (20 tests)
- [ ] Persistence flags (8 tests)
- [ ] Trigger parsing (12 tests)
- [ ] Exclusion parsing (6 tests)
- [ ] Validation logic (16 tests)
- [ ] Error handling (12 tests)
- [ ] Help/version (4 tests)

### Integration Testing
- [ ] Spawn mode workflows (8 tests)
- [ ] Attach mode workflows (8 tests)
- [ ] Config generation (10 tests)
- [ ] Tracer handoff (6 tests)
- [ ] Trigger integration (8 tests)

### Behavioral Testing
- [ ] Invalid inputs (16 tests)
- [ ] Edge cases (12 tests)
- [ ] Security checks (8 tests)
- [ ] Flag combinations (10 tests)

### Performance Testing
- [ ] Parse speed benchmarks (4 tests)
- [ ] Validation speed (3 tests)
- [ ] Memory efficiency (3 tests)

## Definition of Done

- [ ] All code compiles without warnings
- [ ] All unit tests pass (86 tests)
- [ ] All integration tests pass (40 tests)
- [ ] All behavioral tests pass (46 tests)
- [ ] All performance tests pass (10 tests)
- [ ] 100% code coverage achieved
- [ ] No memory leaks (Valgrind clean)
- [ ] Parse time < 1ms for typical commands
- [ ] Parse time < 10ms for complex commands with many flags
- [ ] Memory usage < 4KB
- [ ] Security tests pass
- [ ] Documentation updated
- [ ] Code review completed
- [ ] Integration with tracer verified

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Complex flag combinations | Medium | Comprehensive test matrix |
| Platform-specific paths | Medium | Abstract path validation |
| Permission issues | High | Early permission checks |
| Memory leaks | High | Valgrind in CI |
| Security vulnerabilities | High | Input sanitization, fuzzing |
| Performance regression | Low | Benchmark tests |
| Integration conflicts | Medium | Early integration testing |

## Dependencies

- Existing tracer components (ThreadRegistry, RingBuffer)
- POSIX system calls for process management
- Standard C library for string operations
- No external parsing libraries (build from scratch)

## Notes

- Focus on clear error messages for better UX
- Maintain consistency with existing tracer patterns
- Prioritize security over convenience
- Keep parser single-threaded for simplicity
- Prepare for future extension (new flags/modes)
- Consider backwards compatibility from day 1
- Design trigger system for extensibility
- Support complex flag combinations gracefully
- Maintain efficient memory usage with dynamic structures

## Success Metrics

1. **Functionality**: All command formats parse correctly including new flags
2. **Usability**: Clear help and error messages with comprehensive examples
3. **Performance**: < 1ms typical parse time, < 10ms complex commands
4. **Reliability**: 100% test coverage, zero crashes, perfect memory cleanup
5. **Security**: All injection attempts blocked including trigger/module parameters
6. **Integration**: Seamless tracer initialization with all new configuration
7. **Maintainability**: Clean, documented code with comprehensive tests