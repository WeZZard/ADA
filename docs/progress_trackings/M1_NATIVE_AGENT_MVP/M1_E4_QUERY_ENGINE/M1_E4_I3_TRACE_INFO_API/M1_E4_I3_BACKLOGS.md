# Backlogs — M1 E4 I3 Trace Info API

## Sprint Overview
**Duration**: 2 days (16 hours)
**Priority**: P0 (Critical - first query endpoint implementation)
**Dependencies**: M1_E4_I1 (ATF Reader), M1_E4_I2 (JSON-RPC Server)

## Day 1: Core Implementation (8 hours)

### TI-001: Handler structure and registration (P0, 1h)
- [ ] Create TraceInfoHandler struct
- [ ] Add LruCache dependency to Cargo.toml
- [ ] Implement JsonRpcHandler trait
- [ ] Add handler registration to server
- [ ] Create TraceInfoParams and TraceInfoResponse structs
- [ ] Add serde derives for JSON serialization

### TI-002: Basic trace metadata extraction (P0, 2h)
- [ ] Implement get_trace_info() method
- [ ] Add trace directory validation
- [ ] Integrate with ATFReader for manifest loading
- [ ] Extract basic info (os, arch, timing)
- [ ] Calculate event and span counts
- [ ] Add proper error handling for missing traces

### TI-003: File information calculation (P0, 1.5h)
- [ ] Implement calculate_file_info() method
- [ ] Get manifest and events file sizes
- [ ] Calculate total size and average event size
- [ ] Handle file access errors gracefully
- [ ] Add file metadata validation

### TI-004: Caching system foundation (P0, 2h)
- [ ] Implement LRU cache with configurable size
- [ ] Add cache key generation (trace_id)
- [ ] Implement check_cache() method
- [ ] Add TTL-based cache expiration
- [ ] Handle cache synchronization (Mutex)

### TI-005: Cache invalidation logic (P0, 1.5h)
- [ ] Add file modification time tracking
- [ ] Implement cache freshness checking
- [ ] Handle stale cache entry detection
- [ ] Add cache_response() method
- [ ] Test cache hit/miss scenarios

## Day 2: Advanced Features & Integration (8 hours)

### TI-006: Checksum calculation (P1, 2h)
- [ ] Add MD5 dependency to Cargo.toml
- [ ] Implement calculate_checksums() method
- [ ] Add parallel checksum calculation (tokio::try_join!)
- [ ] Implement calculate_file_md5() helper
- [ ] Add optional checksum inclusion logic
- [ ] Handle large files efficiently

### TI-007: Event sampling system (P1, 2.5h)
- [ ] Implement extract_samples() method
- [ ] Add first/last event collection
- [ ] Implement middle event sampling with intervals
- [ ] Create event_to_sample() conversion
- [ ] Add StreamingSampler for memory efficiency
- [ ] Handle edge cases (small traces, empty traces)

### TI-008: Error handling robustness (P0, 1h)
- [ ] Add comprehensive error mapping
- [ ] Handle corrupted manifest files
- [ ] Add file permission error handling
- [ ] Implement proper JSON-RPC error responses
- [ ] Add error logging with context

### TI-009: Performance optimizations (P1, 1.5h)
- [ ] Optimize hot paths in handler
- [ ] Add async file I/O for checksums
- [ ] Implement efficient event sampling
- [ ] Add memory usage optimizations
- [ ] Profile cache access patterns

### TI-010: Integration and testing (P0, 1h)
- [ ] Register handler with JSON-RPC server
- [ ] Test end-to-end JSON-RPC requests
- [ ] Validate response format compliance
- [ ] Add integration with existing server setup
- [ ] Test with multiple trace formats

## Testing Tasks

### TI-011: Unit test suite (4h)
- [ ] test_trace_info_handler__valid_trace__then_metadata
- [ ] test_trace_info_handler__missing_trace__then_error
- [ ] test_trace_info_handler__corrupted_manifest__then_error
- [ ] test_cache__first_request__then_miss_and_cache
- [ ] test_cache__ttl_expired__then_refresh
- [ ] test_cache__file_modified__then_invalidate
- [ ] test_checksums__enabled__then_correct_md5
- [ ] test_checksums__disabled__then_none
- [ ] test_samples__enabled__then_representative_events
- [ ] test_samples__small_trace__then_all_events
- [ ] test_file_info__calculation__then_accurate

### TI-012: Integration tests (2h)
- [ ] test_integration__json_rpc_request__then_valid_response
- [ ] Create test trace files with known content
- [ ] Test parameter validation
- [ ] Test optional feature toggles
- [ ] Verify JSON response format

### TI-013: Performance tests (1.5h)
- [ ] test_performance__cached_requests__then_fast (<5ms)
- [ ] test_performance__concurrent_requests__then_cached_efficiently
- [ ] test_performance__checksum_calculation__then_reasonable (<2s for 1GB)
- [ ] test_performance__large_trace_sampling__then_bounded (<100ms)

### TI-014: Error condition tests (0.5h)
- [ ] test_missing_trace_directory
- [ ] test_unreadable_files
- [ ] test_invalid_parameters
- [ ] test_cache_overflow
- [ ] test_disk_space_errors

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Cache memory usage | Medium | Medium | LRU eviction, configurable limits |
| Checksum performance | Medium | Low | Optional feature, async calculation |
| File I/O blocking | High | Low | Use tokio::fs for async I/O |
| Large trace handling | Medium | Medium | Streaming sampling, memory bounds |
| Cache synchronization | High | Low | Proper mutex usage, test thoroughly |

## Success Metrics

- [ ] Cached responses <5ms average
- [ ] Uncached responses <500ms for reasonable traces
- [ ] Checksum calculation <2s for 1GB files
- [ ] Event sampling <100ms for large traces
- [ ] Memory usage <1MB per cache entry
- [ ] Accurate metadata extraction for all ATF files
- [ ] Proper error handling for all failure modes
- [ ] JSON-RPC 2.0 compliant responses

## Definition of Done

- [ ] All code implemented and reviewed
- [ ] All unit tests passing (100% coverage on new code)
- [ ] Integration tests with JSON-RPC server passing
- [ ] Performance benchmarks meet targets
- [ ] Error handling tested and robust
- [ ] Caching system functional and tested
- [ ] Optional features (checksums, samples) working
- [ ] Documentation comments added
- [ ] Memory usage validated
- [ ] Approved by technical lead

## Notes

- This is the first concrete query endpoint - sets pattern for others
- Caching is crucial for performance with repeated requests
- Optional features should not impact base performance
- Error handling must be robust for production use
- Response format establishes API conventions

## Dependencies

### Depends On:
- M1_E4_I1: ATF Reader (trace data access)
- M1_E4_I2: JSON-RPC Server (handler infrastructure)
- lru crate (LRU cache implementation)
- md5 crate (checksum calculation)

### Depended By:
- M1_E4_I4: Events/Spans API (similar handler pattern)
- Future query endpoints (caching pattern)
- Query engine integration tests