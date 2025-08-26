# Backlogs — M1 E4 I4 Events/Spans API

## Sprint Overview
**Duration**: 3 days (24 hours)
**Priority**: P0 (Critical - core query functionality)
**Dependencies**: M1_E4_I1 (ATF Reader), M1_E4_I2 (JSON-RPC Server), M1_E4_I3 (Trace Info API)

## Day 1: Events API Foundation (8 hours)

### ES-001: Events handler structure (P0, 1h)
- [ ] Create EventsHandler struct with query engine
- [ ] Define EventsGetParams and EventsGetResponse structs
- [ ] Add EventFilters and EventProjection structures
- [ ] Implement JsonRpcHandler trait for events.get
- [ ] Add parameter validation methods
- [ ] Register handler with JSON-RPC server

### ES-002: Basic query engine (P0, 2.5h)
- [ ] Create QueryEngine struct with ATF integration
- [ ] Implement execute_events_query() method
- [ ] Add basic event iteration and collection
- [ ] Implement simple filtering pipeline
- [ ] Add result counting and metadata generation
- [ ] Handle trace not found errors

### ES-003: Filter implementation (P0, 2h)
- [ ] Implement time range filtering (start_ns, end_ns)
- [ ] Add thread ID filtering
- [ ] Implement event type filtering (ENTRY/RETURN)
- [ ] Add function name filtering
- [ ] Implement module name filtering
- [ ] Add function ID filtering

### ES-004: Field projection (P0, 1.5h)
- [ ] Implement project_event() method
- [ ] Add conditional field inclusion based on projection
- [ ] Create EventResult with optional fields
- [ ] Handle missing data gracefully
- [ ] Optimize projection for performance

### ES-005: Pagination logic (P0, 1h)
- [ ] Implement offset/limit pagination
- [ ] Add has_more calculation
- [ ] Handle edge cases (offset beyond end)
- [ ] Add result count validation
- [ ] Implement proper page boundary handling

## Day 2: Spans API & Advanced Features (8 hours)

### ES-006: Spans handler structure (P0, 1h)
- [ ] Create SpansHandler struct
- [ ] Define SpansListParams and SpansListResponse
- [ ] Add SpanFilters and SpanProjection structures
- [ ] Implement JsonRpcHandler trait for spans.list
- [ ] Add span-specific parameter validation

### ES-007: Span builder core (P0, 3h)
- [ ] Create SpanBuilder struct
- [ ] Implement call stack tracking per thread
- [ ] Add CallFrame and CallStack structures
- [ ] Implement function entry handling
- [ ] Add function return handling with span completion
- [ ] Handle orphaned returns gracefully

### ES-008: Span construction logic (P0, 2h)
- [ ] Implement span creation from entry/return pairs
- [ ] Calculate span durations correctly
- [ ] Add call depth tracking
- [ ] Generate unique span IDs
- [ ] Handle nested function calls properly
- [ ] Add span metadata (thread, module, etc.)

### ES-009: Span filtering (P0, 1.5h)
- [ ] Implement duration-based filtering (min/max)
- [ ] Add time range filtering for spans
- [ ] Implement depth-based filtering
- [ ] Add function/module name filtering for spans
- [ ] Handle span-specific filter edge cases

### ES-010: Span result projection (P0, 0.5h)
- [ ] Implement span field projection
- [ ] Create SpanResult with optional fields
- [ ] Add child count calculation (optional)
- [ ] Handle span projection edge cases

## Day 3: Performance & Integration (8 hours)

### ES-011: Query caching system (P1, 2h)
- [ ] Add LRU cache for query results
- [ ] Implement cache key generation from parameters
- [ ] Add cache hit/miss logic
- [ ] Implement cache invalidation strategy
- [ ] Handle cache memory limits

### ES-012: Performance optimizations (P1, 2h)
- [ ] Optimize event iteration for large traces
- [ ] Add early termination when limit reached
- [ ] Implement streaming processing
- [ ] Optimize memory usage during query execution
- [ ] Add query timeout handling

### ES-013: Error handling robustness (P0, 1h)
- [ ] Add comprehensive parameter validation
- [ ] Handle corrupted trace data gracefully
- [ ] Implement proper JSON-RPC error responses
- [ ] Add query timeout protection
- [ ] Handle memory exhaustion scenarios

### ES-014: Advanced query features (P1, 1.5h)
- [ ] Add result ordering (timestamp, thread_id)
- [ ] Implement ascending/descending sort
- [ ] Add query execution time tracking
- [ ] Implement query result metadata
- [ ] Add query complexity limits

### ES-015: Integration and testing (P0, 1.5h)
- [ ] Register both handlers with server
- [ ] Test end-to-end JSON-RPC requests
- [ ] Validate response format compliance
- [ ] Test with various trace sizes
- [ ] Add integration with existing server setup

## Testing Tasks

### ES-016: Unit test suite (6h)
- [ ] test_events_handler__basic_query__then_results
- [ ] test_events_handler__time_filter__then_subset
- [ ] test_events_handler__function_filter__then_matches
- [ ] test_events_handler__pagination__then_correct_pages
- [ ] test_spans_handler__build_spans__then_matched_pairs
- [ ] test_spans_handler__nested_calls__then_proper_depth
- [ ] test_spans_handler__duration_filter__then_filtered
- [ ] test_query_engine__complex_filter__then_correct_subset
- [ ] test_query_engine__projection__then_only_requested_fields
- [ ] test_span_builder__simple_calls__then_correct_spans
- [ ] test_span_builder__orphaned_returns__then_handled

### ES-017: Integration tests (3h)
- [ ] test_integration__events_get__then_valid_json_rpc
- [ ] test_integration__spans_list__then_valid_json_rpc
- [ ] Create comprehensive test trace files
- [ ] Test complex query combinations
- [ ] Validate JSON response formats
- [ ] Test error conditions through API

### ES-018: Performance tests (3h)
- [ ] test_performance__large_trace_query__then_reasonable_time (<5s for 1M events)
- [ ] test_performance__concurrent_queries__then_scalable (>50 queries/s)
- [ ] test_performance__span_building__then_efficient (<3s for complex nesting)
- [ ] test_performance__memory_usage__then_bounded (<100MB peak)
- [ ] Benchmark filtering performance
- [ ] Test cache effectiveness

### ES-019: Stress tests (1h)
- [ ] Test with 10M+ event traces
- [ ] Test deep function call nesting (100+ levels)
- [ ] Test concurrent access under load
- [ ] Test memory behavior over time
- [ ] Test cache behavior under pressure

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Large trace performance | High | Medium | Streaming, early termination, timeouts |
| Memory usage explosion | High | Medium | Bounded results, memory monitoring |
| Span pairing complexity | Medium | Medium | Robust call stack tracking |
| Query cache effectiveness | Medium | Low | Profile and tune cache parameters |
| Concurrent access issues | High | Low | Proper synchronization, testing |

## Success Metrics

- [ ] Basic event queries <1s response time
- [ ] Large trace queries (1M events) <5s with filtering
- [ ] Span construction <3s for complex nesting
- [ ] Memory usage <100MB peak during queries
- [ ] Concurrent throughput >50 queries/s
- [ ] Accurate span durations and nesting
- [ ] Proper filtering for all parameter types
- [ ] Pagination works correctly
- [ ] Field projection reduces response size significantly

## Definition of Done

- [ ] All code implemented and reviewed
- [ ] All unit tests passing (100% coverage on new code)
- [ ] Integration tests with JSON-RPC server passing
- [ ] Performance benchmarks meet targets
- [ ] Large trace handling validated
- [ ] Concurrent access tested
- [ ] Error handling robust and tested
- [ ] Memory usage profiled and bounded
- [ ] Query caching functional
- [ ] Documentation comments added
- [ ] Approved by technical lead

## Notes

- This iteration completes the core query functionality for the MVP
- Performance is critical - these endpoints will handle production workloads
- Span construction is complex but essential for call flow analysis
- Query caching provides significant performance benefits
- Proper error handling and timeouts prevent server overload
- Memory management is crucial for large trace processing

## Dependencies

### Depends On:
- M1_E4_I1: ATF Reader (trace data access)
- M1_E4_I2: JSON-RPC Server (handler infrastructure)
- M1_E4_I3: Trace Info API (handler registration patterns)
- LRU cache crate (query result caching)

### Depended By:
- Query engine integration tests
- Performance validation tests
- User acceptance testing
- Production deployment