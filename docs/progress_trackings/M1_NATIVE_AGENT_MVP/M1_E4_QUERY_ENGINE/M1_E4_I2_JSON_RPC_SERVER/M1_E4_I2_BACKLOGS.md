# Backlogs — M1 E4 I2 JSON-RPC Server

## Sprint Overview
**Duration**: 3 days (24 hours)
**Priority**: P0 (Critical - HTTP API infrastructure for query engine)
**Dependencies**: M1_E4_I1 (ATF Reader)

## Day 1: HTTP Server Foundation (8 hours)

### RPC-001: Project dependencies and structure (P0, 0.5h)
- [ ] Add hyper and tokio dependencies to Cargo.toml
- [ ] Add serde and serde_json for JSON handling
- [ ] Create query_engine/src/server/ module
- [ ] Set up async runtime configuration
- [ ] Create error types module

### RPC-002: Core JSON-RPC data structures (P0, 1h)
- [ ] Define JsonRpcRequest struct with serde derives
- [ ] Define JsonRpcResponse struct with serde derives
- [ ] Define JsonRpcError struct with standard codes
- [ ] Add Display and Error traits for debugging
- [ ] Create helper methods for standard errors

### RPC-003: HTTP server foundation (P0, 2.5h)
- [ ] Create JsonRpcServer struct
- [ ] Implement HTTP server with hyper
- [ ] Add request routing to /rpc endpoint
- [ ] Handle HTTP method validation (POST only)
- [ ] Add content-type validation (application/json)
- [ ] Implement basic request/response cycle

### RPC-004: Request parsing pipeline (P0, 2h)
- [ ] Implement JSON request body parsing
- [ ] Add request size validation
- [ ] Validate JSON-RPC 2.0 format
- [ ] Handle malformed JSON gracefully
- [ ] Add comprehensive error responses

### RPC-005: Response building (P0, 2h)
- [ ] Implement JSON response serialization
- [ ] Add proper HTTP headers (content-type, CORS)
- [ ] Handle both success and error responses
- [ ] Preserve request IDs in responses
- [ ] Add response compression support

## Day 2: Method Dispatch & Connection Management (8 hours)

### RPC-006: Handler registry system (P0, 2h)
- [ ] Create JsonRpcHandler trait
- [ ] Implement HandlerRegistry with HashMap
- [ ] Add register() method for handler registration
- [ ] Implement dispatch() method with method lookup
- [ ] Handle method not found errors

### RPC-007: Method dispatcher (P0, 2h)
- [ ] Create async method dispatcher
- [ ] Add parameter validation
- [ ] Implement timeout handling for handlers
- [ ] Add error propagation from handlers
- [ ] Support both notification and request calls

### RPC-008: Connection management (P0, 2h)
- [ ] Create ConnectionManager struct
- [ ] Track active connections per IP
- [ ] Add connection limit enforcement
- [ ] Implement connection cleanup for idle connections
- [ ] Add connection metrics (count, duration)

### RPC-009: Rate limiting (P0, 1.5h)
- [ ] Create RateLimiter struct
- [ ] Implement per-IP rate limiting
- [ ] Add sliding window algorithm
- [ ] Handle rate limit exceeded responses
- [ ] Add configurable limits per endpoint

### RPC-010: CORS and security headers (P0, 0.5h)
- [ ] Add CORS middleware
- [ ] Implement preflight OPTIONS handling
- [ ] Add security headers (HSTS, etc.)
- [ ] Configure allowed origins
- [ ] Add request logging

## Day 3: Integration & Performance (8 hours)

### RPC-011: Server configuration (P0, 1h)
- [ ] Create ServerConfig struct
- [ ] Add configurable bind address and port
- [ ] Add timeout configurations
- [ ] Add connection and rate limit settings
- [ ] Implement configuration validation

### RPC-012: Graceful shutdown (P0, 1h)
- [ ] Implement signal handling (SIGTERM, SIGINT)
- [ ] Add graceful connection draining
- [ ] Close server socket cleanly
- [ ] Add shutdown timeout
- [ ] Ensure all requests complete or timeout

### RPC-013: Error handling robustness (P0, 1.5h)
- [ ] Add comprehensive error mapping
- [ ] Handle panics in handlers gracefully
- [ ] Add error logging with context
- [ ] Implement circuit breaker pattern
- [ ] Add health check endpoint

### RPC-014: Performance optimizations (P1, 1.5h)
- [ ] Add connection keep-alive support
- [ ] Implement response caching for repeated queries
- [ ] Optimize JSON serialization/deserialization
- [ ] Add request pipelining support
- [ ] Profile and optimize hot paths

### RPC-015: Integration with ATF reader (P0, 2h)
- [ ] Create test handlers using ATFReader
- [ ] Add trace file loading functionality
- [ ] Implement basic trace.info endpoint
- [ ] Add proper error handling for file operations
- [ ] Test end-to-end request/response flow

### RPC-016: Metrics and monitoring (P1, 1h)
- [ ] Add request/response metrics collection
- [ ] Track handler execution times
- [ ] Monitor connection counts
- [ ] Add error rate tracking
- [ ] Implement metrics export endpoint

## Testing Tasks

### RPC-017: Unit test suite (4h)
- [ ] test_json_rpc_parser__valid_request__then_parsed
- [ ] test_json_rpc_parser__missing_version__then_error
- [ ] test_method_dispatcher__known_method__then_called
- [ ] test_method_dispatcher__unknown_method__then_not_found
- [ ] test_error_handler__parse_error__then_standard_code
- [ ] test_error_handler__preserves_request_id__then_correct_id
- [ ] test_connection_manager__track_requests__then_counted
- [ ] test_connection_manager__max_connections__then_cleanup
- [ ] test_rate_limiter__within_limit__then_allowed
- [ ] test_rate_limiter__exceeds_limit__then_rejected

### RPC-018: HTTP integration tests (3h)
- [ ] test_http_server__valid_post__then_json_response
- [ ] test_http_server__invalid_method__then_405
- [ ] test_http_server__cors_headers__then_present
- [ ] test_http_server__large_request__then_handled
- [ ] test_http_server__malformed_json__then_error
- [ ] test_http_server__timeout__then_closed

### RPC-019: Concurrency tests (2h)
- [ ] test_concurrent__multiple_requests__then_all_handled
- [ ] test_concurrent__rate_limiting__then_enforced
- [ ] test_concurrent__connection_limits__then_enforced
- [ ] test_concurrent__handler_errors__then_isolated

### RPC-020: Performance tests (2h)
- [ ] test_performance__request_throughput__then_meets_target (>1000 req/s)
- [ ] test_performance__response_latency__then_low (<10ms)
- [ ] test_performance__memory_usage__then_bounded (<50MB)
- [ ] test_performance__connection_scaling__then_linear

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Async runtime complexity | High | Medium | Use well-tested tokio patterns |
| Memory leaks from connections | High | Low | Implement proper cleanup, timeouts |
| JSON parsing performance | Medium | Low | Profile and optimize hot paths |
| Rate limiting accuracy | Medium | Medium | Use proven algorithms, test thoroughly |
| CORS configuration issues | Low | Medium | Test with browser clients |

## Success Metrics

- [ ] Handle 1000+ concurrent requests per second
- [ ] Response latency <10ms for simple queries
- [ ] Memory usage <50MB under normal load
- [ ] JSON-RPC 2.0 specification compliance
- [ ] Zero request loss under normal conditions
- [ ] Graceful degradation under overload
- [ ] Clean shutdown within 5 seconds
- [ ] All standard JSON-RPC errors handled

## Definition of Done

- [ ] All code implemented and reviewed
- [ ] All unit tests passing (100% coverage on new code)
- [ ] HTTP integration tests passing
- [ ] Concurrency tests passing
- [ ] Performance benchmarks meet targets
- [ ] JSON-RPC 2.0 compliance verified
- [ ] Error handling tested and robust
- [ ] Documentation comments added
- [ ] Security headers configured
- [ ] Rate limiting functional
- [ ] Approved by technical lead

## Notes

- This iteration creates the HTTP API foundation for all query endpoints
- Performance is critical - this handles all client requests
- JSON-RPC 2.0 compliance ensures standard behavior
- Connection management prevents resource exhaustion
- Rate limiting protects against abuse
- Proper error handling ensures reliable client experience

## Dependencies

### Depends On:
- M1_E4_I1: ATF Reader (for accessing trace data)
- hyper crate (HTTP server)
- tokio crate (async runtime)
- serde/serde_json (JSON serialization)

### Depended By:
- M1_E4_I3: Trace Info API (uses JSON-RPC server)
- M1_E4_I4: Events/Spans API (uses JSON-RPC server)
- Future query endpoints