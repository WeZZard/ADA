# Backlogs — M1 E4 I1 ATF Reader

## Sprint Overview
**Duration**: 2 days (16 hours)
**Priority**: P0 (Critical - foundation for query engine data access)
**Dependencies**: M1_E2_I3 (ATF V4 Writer)

## Day 1: Core Parsing Infrastructure (8 hours)

### ATF-001: Project structure setup (P0, 0.5h)
- [ ] Create query_engine/src/atf/ module
- [ ] Add protobuf and serde_json dependencies
- [ ] Add memmap2 dependency for memory mapping
- [ ] Set up error types module
- [ ] Create lib.rs exports

### ATF-002: Define core data structures (P0, 1.5h)
- [ ] Define AtfError enum with all error types
- [ ] Create ManifestInfo struct with serde derives
- [ ] Define Event protobuf structure
- [ ] Add EventType and function entry/return structs
- [ ] Implement Display traits for debugging

### ATF-003: Manifest parser implementation (P0, 2h)
- [ ] Implement ManifestInfo::from_json() method
- [ ] Add validation for required fields
- [ ] Handle missing fields gracefully
- [ ] Add duration calculation helper
- [ ] Test with sample trace.json files

### ATF-004: Memory mapping implementation (P0, 2h)
- [ ] Create MemoryMap struct
- [ ] Implement mmap file access
- [ ] Add bounds checking
- [ ] Handle file size validation
- [ ] Add proper resource cleanup (Drop trait)

### ATF-005: Varint decoder (P0, 2h)
- [ ] Implement read_varint() method
- [ ] Handle multi-byte encoding correctly
- [ ] Add bounds checking for buffer access
- [ ] Validate maximum 32-bit values
- [ ] Add error handling for malformed varints

## Day 2: Event Iteration & Integration (8 hours)

### ATF-006: Event iterator core (P0, 2.5h)
- [ ] Create EventIterator struct
- [ ] Implement next() method with protobuf parsing
- [ ] Add position tracking
- [ ] Implement remaining_count() method
- [ ] Handle end-of-stream properly

### ATF-007: Seek functionality (P0, 1.5h)
- [ ] Implement seek(offset) method
- [ ] Add bounds validation
- [ ] Update position correctly
- [ ] Handle seek to beginning/end
- [ ] Add position() getter method

### ATF-008: ATFReader integration (P0, 2h)
- [ ] Create main ATFReader struct
- [ ] Implement open(trace_dir) method
- [ ] Coordinate manifest and events loading
- [ ] Add event_iterator() method
- [ ] Implement timestamp estimation helper

### ATF-009: Error handling robustness (P0, 1h)
- [ ] Add comprehensive error propagation
- [ ] Handle corrupted file scenarios
- [ ] Add descriptive error messages
- [ ] Test error paths thoroughly
- [ ] Add Result types to all public methods

### ATF-010: Performance optimizations (P1, 1h)
- [ ] Add zero-copy parsing where possible
- [ ] Optimize varint decoding hot path
- [ ] Pre-calculate manifest values
- [ ] Add branch prediction hints
- [ ] Profile memory access patterns

## Testing Tasks

### ATF-011: Unit test suite (4h)
- [ ] test_manifest_parser__valid_json__then_parsed
- [ ] test_manifest_parser__missing_field__then_error
- [ ] test_memory_map__valid_file__then_accessible
- [ ] test_memory_map__large_file__then_efficient
- [ ] test_varint__single_byte__then_correct
- [ ] test_varint__multi_byte__then_correct
- [ ] test_varint__maximum_value__then_correct
- [ ] test_varint__truncated__then_error
- [ ] test_event_iterator__sequential_read__then_ordered
- [ ] test_event_iterator__empty_stream__then_none
- [ ] test_seek__valid_offset__then_positioned
- [ ] test_seek__beyond_end__then_error

### ATF-012: Integration tests (2h)
- [ ] Create test ATF files with known content
- [ ] test_integration__full_atf_file__then_complete_read
- [ ] test_integration__corrupted_file__then_graceful_error
- [ ] test_integration__large_file__then_handles
- [ ] Verify manifest-events consistency

### ATF-013: Performance tests (1h)
- [ ] test_performance__sequential_throughput__then_fast (>100MB/s)
- [ ] test_performance__random_seeks__then_low_latency (<1ms)
- [ ] test_performance__memory_usage__then_bounded (<10MB)
- [ ] Benchmark with realistic trace sizes

### ATF-014: Error condition tests (1h)
- [ ] test_missing_manifest_file
- [ ] test_missing_events_file  
- [ ] test_corrupted_json
- [ ] test_truncated_protobuf
- [ ] test_invalid_varint_encoding
- [ ] Verify graceful error handling

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Protobuf parsing performance | High | Medium | Profile and optimize hot paths |
| Large file memory usage | High | Low | Use memory mapping, not loading |
| Corrupted file handling | Medium | Medium | Robust error handling, validation |
| Varint decoding edge cases | Medium | Low | Comprehensive test cases |
| Memory mapping portability | Low | Low | Use proven memmap2 crate |

## Success Metrics

- [ ] Parse all valid ATF V4 files correctly
- [ ] Sequential read throughput >100 MB/s
- [ ] Random access latency <1ms
- [ ] Memory overhead <10MB for any file size
- [ ] Handle files up to 10GB without issues
- [ ] All error conditions handled gracefully
- [ ] Zero unsafe code (use safe Rust APIs)
- [ ] All tests passing

## Definition of Done

- [ ] All code implemented and reviewed
- [ ] All unit tests passing (100% coverage on new code)
- [ ] Integration tests with sample ATF files passing
- [ ] Performance benchmarks meet targets
- [ ] Error handling tested and robust
- [ ] Documentation comments added
- [ ] Code follows Rust best practices
- [ ] Memory safety verified (no unsafe blocks)
- [ ] Approved by technical lead

## Notes

- This iteration creates the foundational data access layer
- Performance is critical - this will be called frequently
- Error handling must be robust for production use
- Memory mapping provides efficiency for large files
- Iterator pattern allows streaming access
- Seek capability enables random access queries

## Dependencies

### Depends On:
- M1_E2_I3: ATF V4 Writer (defines file format)
- protobuf-rust crate (Event parsing)
- serde_json crate (manifest parsing)
- memmap2 crate (memory mapping)

### Depended By:
- M1_E4_I2: JSON-RPC Server (uses ATFReader)
- M1_E4_I3: Trace Info API (uses manifest data)
- M1_E4_I4: Events/Spans API (uses event iterator)