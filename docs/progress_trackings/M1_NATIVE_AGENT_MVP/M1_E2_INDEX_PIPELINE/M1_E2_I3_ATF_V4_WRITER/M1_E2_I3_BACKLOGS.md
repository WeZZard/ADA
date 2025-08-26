# M1_E2_I3 Backlogs: ATF V4 Writer

## Sprint Overview

**Goal**: Implement ATF V4 Protobuf-compliant file writer for drained ring snapshots with standardized Event schema and JSON manifest.

**Duration**: 2 days (16 hours)

**Dependencies**:
- M1_E2_I2: Per-thread drain completed
- Ring buffer snapshots available
- Thread registry functional
- ATF V4 TRACE_SCHEMA.md specification

## Task Breakdown

### Day 1: ATF V4 Protobuf Integration (8 hours)

#### Task 1.1: Protobuf Schema Integration (2 hours)
- **Priority**: P0
- **Assignee**: Backend Developer
- **Description**: Integrate protobuf-c library and generate C bindings from TRACE_SCHEMA.md
- **Acceptance Criteria**:
  - [ ] protobuf-c library integrated into build
  - [ ] trace_schema.pb-c.h/.c generated from .proto
  - [ ] Event, FunctionCall, FunctionReturn, SignalDelivery structs available
  - [ ] Protobuf initialization macros functional
  - [ ] Basic serialization/deserialization working
- **Dependencies**: None
- **Risks**: protobuf-c API changes, build system complexity

#### Task 1.2: Ring Buffer to Protobuf Conversion (3 hours)
- **Priority**: P0
- **Assignee**: Backend Developer
- **Description**: Convert ring buffer events to ATF V4 Event protobuf messages
- **Acceptance Criteria**:
  - [ ] convert_ring_to_proto() function for all event types
  - [ ] FunctionCall conversion with registers and stack
  - [ ] FunctionReturn conversion with return values
  - [ ] SignalDelivery conversion with full register dump
  - [ ] TraceStart/TraceEnd conversion with metadata
  - [ ] Timestamp conversion to protobuf Timestamp
- **Dependencies**: Task 1.1
- **Risks**: Data loss in conversion, performance impact

#### Task 1.3: Length-Delimited Writer (2 hours)
- **Priority**: P0
- **Assignee**: Backend Developer
- **Description**: Implement length-delimited protobuf stream writer
- **Acceptance Criteria**:
  - [ ] Varint encoding for length prefixes
  - [ ] [length][protobuf] format implementation
  - [ ] Single events.bin file output
  - [ ] Atomic write operations maintained
  - [ ] Error handling for serialization failures
- **Dependencies**: Task 1.2
- **Risks**: Varint encoding bugs, stream corruption

#### Task 1.4: Manifest Generation (1 hour)
- **Priority**: P0
- **Assignee**: Backend Developer
- **Description**: Generate trace.json manifest on session close
- **Acceptance Criteria**:
  - [ ] JSON manifest with os, arch, timeStartNs, timeEndNs
  - [ ] Event count tracking and reporting
  - [ ] Module UUID array support
  - [ ] Atomic manifest write on session finalization
  - [ ] Cross-platform OS/arch detection
- **Dependencies**: Task 1.3
- **Risks**: JSON formatting errors, incomplete metadata

### Day 2: Testing and Integration (8 hours)

#### Task 2.1: ATF V4 Unit Tests (3 hours)
- **Priority**: P0
- **Assignee**: Test Engineer
- **Description**: Comprehensive unit tests for ATF V4 components
- **Test Coverage**:
  - [ ] Protobuf encoding/decoding round-trip tests
  - [ ] Varint encoding edge cases (0, 127, 128, UINT64_MAX)
  - [ ] Ring-to-protobuf conversion accuracy
  - [ ] Schema validation (required fields, payload types)
  - [ ] Manifest generation with various scenarios
  - [ ] Error handling for malformed data
- **Dependencies**: Day 1 tasks
- **Coverage Target**: 100% for new ATF V4 code

#### Task 2.2: ATF V4 Integration Tests (2 hours)
- **Priority**: P0
- **Assignee**: Test Engineer
- **Description**: Integration tests for drain worker and session management
- **Test Coverage**:
  - [ ] Drain snapshot to ATF V4 pipeline
  - [ ] Session lifecycle (init, write, finalize)
  - [ ] Multi-event type processing
  - [ ] Cross-platform file format consistency
  - [ ] Large batch performance testing
- **Dependencies**: Task 2.1
- **Coverage Target**: 100% for integration paths

#### Task 2.3: Schema Compliance Validation (2 hours)
- **Priority**: P0
- **Assignee**: Backend Developer
- **Description**: Validate output against TRACE_SCHEMA.md V4
- **Acceptance Criteria**:
  - [ ] End-to-end schema compliance test
  - [ ] External tool can deserialize events.bin
  - [ ] All Event payload types validated
  - [ ] Timestamp precision verification (nanoseconds)
  - [ ] Register data preservation verified
  - [ ] Stack data binary preservation verified
- **Dependencies**: Task 2.2
- **Risks**: Schema drift, tool incompatibility

#### Task 2.4: Performance Benchmarking (1 hour)
- **Priority**: P1
- **Assignee**: Performance Engineer
- **Description**: Measure ATF V4 performance vs custom binary format
- **Benchmarks**:
  - [ ] Write latency < 2ms for 1KB protobuf batch
  - [ ] Throughput > 500K events/sec with protobuf overhead
  - [ ] Memory usage profiling with protobuf allocations
  - [ ] Serialization overhead measurement
  - [ ] Concurrent writer scaling verification
- **Dependencies**: Task 2.3
- **Risks**: Performance regression, memory leaks

## Implementation Priority

### P0 - Critical Path (Must Complete)
1. Protobuf-c integration and schema generation
2. Ring buffer to Event conversion
3. Length-delimited stream writer
4. JSON manifest generation
5. Unit tests for all components
6. Integration tests for pipeline
7. Schema compliance validation

### P1 - Important (Should Complete)
1. Performance benchmarking vs binary format
2. Memory pool allocator for protobuf
3. Error path optimization
4. Cross-platform compatibility testing
5. Documentation updates

### P2 - Nice to Have (If Time Permits)
1. Batch optimization for serialization
2. Compression evaluation for protobuf
3. Schema evolution planning
4. Tool integration examples

## Risk Register

### High Risk
1. **Protobuf Performance Impact**
   - **Impact**: Significant latency increase over binary format
   - **Mitigation**: Memory pools, batching, performance profiling
   - **Owner**: Performance Engineer

2. **Schema Compliance**
   - **Impact**: Tools cannot parse ATF files
   - **Mitigation**: Automated validation, external tool testing
   - **Owner**: Backend Developer

### Medium Risk
1. **Memory Management**
   - **Impact**: Memory leaks, fragmentation with protobuf
   - **Mitigation**: Pool allocators, proper cleanup, leak detection
   - **Owner**: Backend Developer

2. **Cross-Platform Issues**
   - **Impact**: Format differences between darwin/linux
   - **Mitigation**: Endianness testing, platform-specific validation
   - **Owner**: Test Engineer

### Low Risk
1. **Build System Complexity**
   - **Impact**: protobuf-c integration issues
   - **Mitigation**: Clear build documentation, dependency management
   - **Owner**: Build Engineer

## Success Metrics

### Functional Metrics
- [ ] All ATF V4 Event types serialize/deserialize correctly
- [ ] External tools can parse generated events.bin files
- [ ] Manifest JSON is valid and complete
- [ ] Zero data corruption in concurrent scenarios
- [ ] Schema validation passes for all test cases

### Performance Metrics (Adjusted for Protobuf)
- [ ] Write latency P50 < 1ms (vs 500μs binary)
- [ ] Write latency P99 < 2ms (vs 1ms binary)  
- [ ] Throughput > 500K events/sec @ 8 threads (vs 1M binary)
- [ ] CPU overhead < 10% (vs 5% binary)
- [ ] Memory usage < 2MB per thread (vs 1MB binary)

### Quality Metrics
- [ ] Code coverage > 95% for ATF V4 components
- [ ] Changed line coverage = 100%
- [ ] Schema compliance = 100%
- [ ] Cross-platform compatibility = 100%
- [ ] Performance regression < 50% vs binary

## Dependencies

### Upstream Dependencies
- M1_E2_I2: Drain implementation complete
- Ring buffer snapshot API stable  
- Thread registry operational
- TRACE_SCHEMA.md V4 finalized

### Downstream Impact
- Query engine must parse ATF V4 instead of binary
- Analysis tools need ATF V4 deserializer
- Network streaming will use ATF V4 format
- Storage requirements may increase

## Technical Changes from Binary Format

### Removed Components
- Custom .idx (index lane) binary format
- Custom .dtl (detail lane) binary format
- Binary file headers (ada_file_header_t)
- Per-thread file separation
- Custom correlation IDs between lanes

### Added Components
- ATF V4 Event protobuf schema
- Length-delimited stream format ([varint][Event])
- Single events.bin file per session
- trace.json manifest with metadata
- protobuf-c library dependency
- Varint encoding/decoding functions

### Migration Path
- Query engine needs ATF V4 parser
- Existing binary files remain readable via legacy parser
- New sessions generate ATF V4 format only
- Documentation updated for new format

## Review Checklist

### Design Review
- [ ] ATF V4 schema compliance verified
- [ ] Event conversion accuracy confirmed
- [ ] Performance impact acceptable
- [ ] Cross-platform compatibility ensured

### Code Review  
- [ ] protobuf-c integration correct
- [ ] Memory management safe (no leaks)
- [ ] Error paths handle protobuf failures
- [ ] Thread safety maintained

### Test Review
- [ ] Schema validation comprehensive
- [ ] Performance benchmarks completed
- [ ] Cross-platform tests passing
- [ ] External tool compatibility verified

### Documentation Review
- [ ] ATF V4 format specification complete
- [ ] Migration guide for tools
- [ ] Performance characteristics documented
- [ ] Schema evolution plan outlined

## Notes

### Implementation Considerations
1. Use protobuf-c allocator hooks for memory management
2. Batch protobuf operations where possible
3. Validate schema compliance in tests, not runtime
4. Keep manifest generation lightweight
5. Plan for schema versioning from day one

### Testing Focus
1. Schema compliance with external tools
2. Performance regression analysis
3. Memory usage profiling
4. Cross-platform serialization consistency
5. Large-scale event processing

### Performance Notes
1. Protobuf serialization overhead is inherent
2. Length-delimited format adds varint cost
3. JSON manifest is small overhead
4. Memory pools can reduce allocation cost
5. Batch operations improve efficiency

## Completion Criteria

### Iteration Complete When:
1. All P0 tasks completed within 2 days
2. Schema compliance validated with external tools
3. Performance regression < 50% vs binary format
4. All tests passing with 100% coverage
5. Cross-platform compatibility verified
6. Documentation updated for ATF V4
7. Integration verified with drain worker
8. No P0 bugs remaining

### Handoff Package Includes:
1. ATF V4 technical specification
2. Performance comparison analysis
3. Schema compliance test results
4. Migration guide for downstream tools
5. Known limitations and future improvements
6. External tool integration examples