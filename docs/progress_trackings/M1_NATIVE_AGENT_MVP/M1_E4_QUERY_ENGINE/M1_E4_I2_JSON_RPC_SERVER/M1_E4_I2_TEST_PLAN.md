# Test Plan — M1 E4 I2 JSON-RPC Server

## Objective
Validate JSON-RPC 2.0 server provides robust, scalable HTTP API infrastructure with proper error handling, rate limiting, and concurrent request processing for the query engine.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Request Parser | ✓ | ✓ | - |
| Method Dispatcher | ✓ | ✓ | ✓ |
| Error Handling | ✓ | ✓ | - |
| Connection Manager | ✓ | ✓ | ✓ |
| Rate Limiter | ✓ | ✓ | ✓ |
| HTTP Server | - | ✓ | ✓ |

## Test Execution Sequence
1. Unit Tests → 2. Integration Tests → 3. Concurrency Tests → 4. Performance Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| parser__valid_json_rpc__then_parsed | Valid JSON-RPC 2.0 | JsonRpcRequest | Correct deserialization |
| dispatcher__known_method__then_called | "trace.info" | Handler invoked | Method found and executed |
| error__invalid_json__then_parse_error | Invalid JSON | Code -32700 | Standard error code |
| rate_limit__exceeded__then_rejected | >limit requests | HTTP 429 | Rate limiting active |
| concurrent__multiple_requests__then_handled | 100 requests | All responses | No request lost |

## Test Categories

### 1. JSON-RPC Request Parsing Tests

#### Test: `json_rpc_parser__valid_request__then_parsed`
```rust
#[tokio::test]
async fn test_valid_json_rpc_parsing() {
    let request_json = r#"{
        "jsonrpc": "2.0",
        "method": "trace.info",
        "params": {"traceId": "abc123"},
        "id": 1
    }"#;
    
    let request: JsonRpcRequest = serde_json::from_str(request_json).unwrap();
    
    assert_eq!(request.jsonrpc, "2.0");
    assert_eq!(request.method, "trace.info");
    assert!(request.params.is_some());
    assert_eq!(request.id, Some(serde_json::Value::Number(serde_json::Number::from(1))));
}
```

#### Test: `json_rpc_parser__missing_version__then_error`
```rust
#[tokio::test]
async fn test_missing_jsonrpc_version() {
    let invalid_json = r#"{
        "method": "trace.info",
        "id": 1
    }"#;
    
    let result: Result<JsonRpcRequest, _> = serde_json::from_str(invalid_json);
    assert!(result.is_err());
}
```

#### Test: `json_rpc_parser__notification_request__then_no_id`
```rust
#[tokio::test]
async fn test_notification_request() {
    let notification_json = r#"{
        "jsonrpc": "2.0",
        "method": "trace.info"
    }"#;
    
    let request: JsonRpcRequest = serde_json::from_str(notification_json).unwrap();
    assert!(request.id.is_none()); // Notification has no ID
}
```

### 2. Method Dispatch Tests

#### Test: `method_dispatcher__known_method__then_called`
```rust
#[tokio::test]
async fn test_method_dispatch() {
    let mut registry = HandlerRegistry::new();
    
    // Mock handler that returns test data
    struct TestHandler;
    impl JsonRpcHandler for TestHandler {
        async fn handle(&self, _params: Option<serde_json::Value>) 
                       -> Result<serde_json::Value, JsonRpcError> {
            Ok(serde_json::json!({"test": "response"}))
        }
    }
    
    registry.register("test.method", TestHandler);
    
    let result = registry.dispatch("test.method", None).await;
    assert!(result.is_ok());
    
    let response = result.unwrap();
    assert_eq!(response["test"], "response");
}
```

#### Test: `method_dispatcher__unknown_method__then_not_found`
```rust
#[tokio::test]
async fn test_unknown_method() {
    let registry = HandlerRegistry::new();
    
    let result = registry.dispatch("nonexistent.method", None).await;
    assert!(result.is_err());
    
    let error = result.unwrap_err();
    assert_eq!(error.code, -32601); // Method not found
    assert_eq!(error.message, "Method not found");
}
```

#### Test: `method_dispatcher__handler_error__then_propagated`
```rust
#[tokio::test]
async fn test_handler_error_propagation() {
    let mut registry = HandlerRegistry::new();
    
    struct ErrorHandler;
    impl JsonRpcHandler for ErrorHandler {
        async fn handle(&self, _params: Option<serde_json::Value>) 
                       -> Result<serde_json::Value, JsonRpcError> {
            Err(JsonRpcError::invalid_params("Test error"))
        }
    }
    
    registry.register("error.method", ErrorHandler);
    
    let result = registry.dispatch("error.method", None).await;
    assert!(result.is_err());
    
    let error = result.unwrap_err();
    assert_eq!(error.code, -32602);
    assert_eq!(error.message, "Invalid params");
}
```

### 3. Error Handling Tests

#### Test: `error_handler__parse_error__then_standard_code`
```rust
#[tokio::test]
async fn test_parse_error_response() {
    let server = create_test_server().await;
    
    let invalid_json = "{ invalid json }";
    let response = server.process_request_body(invalid_json).await;
    
    let json_response: JsonRpcResponse = serde_json::from_str(&response).unwrap();
    assert!(json_response.error.is_some());
    
    let error = json_response.error.unwrap();
    assert_eq!(error.code, -32700); // Parse error
    assert_eq!(error.message, "Parse error");
}
```

#### Test: `error_handler__invalid_version__then_invalid_request`
```rust
#[tokio::test]
async fn test_invalid_jsonrpc_version() {
    let server = create_test_server().await;
    
    let request = r#"{
        "jsonrpc": "1.0",
        "method": "test.method",
        "id": 1
    }"#;
    
    let response = server.process_request_body(request).await;
    let json_response: JsonRpcResponse = serde_json::from_str(&response).unwrap();
    
    let error = json_response.error.unwrap();
    assert_eq!(error.code, -32600); // Invalid Request
}
```

#### Test: `error_handler__preserves_request_id__then_correct_id`
```rust
#[tokio::test]
async fn test_error_preserves_request_id() {
    let server = create_test_server().await;
    
    let request = r#"{
        "jsonrpc": "2.0",
        "method": "nonexistent.method",
        "id": "test-id-123"
    }"#;
    
    let response = server.process_request_body(request).await;
    let json_response: JsonRpcResponse = serde_json::from_str(&response).unwrap();
    
    assert_eq!(json_response.id, Some(serde_json::Value::String("test-id-123".to_string())));
}
```

### 4. Connection Management Tests

#### Test: `connection_manager__track_requests__then_counted`
```rust
#[tokio::test]
async fn test_connection_tracking() {
    let manager = ConnectionManager::new(100);
    let addr = "127.0.0.1:12345".parse().unwrap();
    
    manager.track_request(&addr);
    manager.track_request(&addr);
    manager.track_request(&addr);
    
    let connections = manager.active_connections.lock().unwrap();
    let info = connections.get(&addr).unwrap();
    assert_eq!(info.request_count, 3);
}
```

#### Test: `connection_manager__max_connections__then_cleanup`
```rust
#[tokio::test]
async fn test_connection_limit_cleanup() {
    let manager = ConnectionManager::new(2); // Small limit for testing
    
    let addr1 = "127.0.0.1:11111".parse().unwrap();
    let addr2 = "127.0.0.1:22222".parse().unwrap();
    let addr3 = "127.0.0.1:33333".parse().unwrap();
    
    manager.track_request(&addr1);
    manager.track_request(&addr2);
    
    // Sleep to age the connections
    tokio::time::sleep(Duration::from_millis(100)).await;
    
    manager.track_request(&addr3); // Should trigger cleanup
    
    let connections = manager.active_connections.lock().unwrap();
    assert!(connections.len() <= 2);
}
```

### 5. Rate Limiting Tests

#### Test: `rate_limiter__within_limit__then_allowed`
```rust
#[tokio::test]
async fn test_rate_limiting_allowed() {
    let limiter = RateLimiter::new(10); // 10 requests per minute
    let addr = "127.0.0.1:12345".parse().unwrap();
    
    for _ in 0..9 {
        assert!(limiter.check_request(&addr).await);
    }
}
```

#### Test: `rate_limiter__exceeds_limit__then_rejected`
```rust
#[tokio::test]
async fn test_rate_limiting_exceeded() {
    let limiter = RateLimiter::new(5); // 5 requests per minute
    let addr = "127.0.0.1:12345".parse().unwrap();
    
    // Fill up the limit
    for _ in 0..5 {
        assert!(limiter.check_request(&addr).await);
    }
    
    // Next request should be rejected
    assert!(!limiter.check_request(&addr).await);
}
```

#### Test: `rate_limiter__different_ips__then_independent`
```rust
#[tokio::test]
async fn test_rate_limiting_per_ip() {
    let limiter = RateLimiter::new(2);
    let addr1 = "127.0.0.1:11111".parse().unwrap();
    let addr2 = "127.0.0.1:22222".parse().unwrap();
    
    // Each IP should have independent limits
    for _ in 0..2 {
        assert!(limiter.check_request(&addr1).await);
        assert!(limiter.check_request(&addr2).await);
    }
    
    // Both should now be rate limited
    assert!(!limiter.check_request(&addr1).await);
    assert!(!limiter.check_request(&addr2).await);
}
```

### 6. HTTP Integration Tests

#### Test: `http_server__valid_post__then_json_response`
```rust
#[tokio::test]
async fn test_http_post_request() {
    let server = start_test_server().await;
    let client = reqwest::Client::new();
    
    let request_body = r#"{
        "jsonrpc": "2.0",
        "method": "test.echo",
        "params": {"message": "hello"},
        "id": 1
    }"#;
    
    let response = client
        .post(&format!("http://{}/rpc", server.addr))
        .header("content-type", "application/json")
        .body(request_body)
        .send()
        .await
        .unwrap();
    
    assert_eq!(response.status(), 200);
    assert_eq!(response.headers().get("content-type").unwrap(), "application/json");
    
    let body = response.text().await.unwrap();
    let json_response: JsonRpcResponse = serde_json::from_str(&body).unwrap();
    assert_eq!(json_response.jsonrpc, "2.0");
    assert_eq!(json_response.id, Some(serde_json::Value::Number(serde_json::Number::from(1))));
}
```

#### Test: `http_server__invalid_method__then_405`
```rust
#[tokio::test]
async fn test_invalid_http_method() {
    let server = start_test_server().await;
    let client = reqwest::Client::new();
    
    let response = client
        .get(&format!("http://{}/rpc", server.addr))
        .send()
        .await
        .unwrap();
    
    // Should return JSON-RPC error, not HTTP 405
    assert_eq!(response.status(), 200);
    
    let body = response.text().await.unwrap();
    let json_response: JsonRpcResponse = serde_json::from_str(&body).unwrap();
    let error = json_response.error.unwrap();
    assert_eq!(error.code, -32600); // Invalid Request
}
```

#### Test: `http_server__cors_headers__then_present`
```rust
#[tokio::test]
async fn test_cors_headers() {
    let mut config = ServerConfig::default();
    config.enable_cors = true;
    let server = start_test_server_with_config(config).await;
    let client = reqwest::Client::new();
    
    let response = client
        .post(&format!("http://{}/rpc", server.addr))
        .header("content-type", "application/json")
        .body(r#"{"jsonrpc": "2.0", "method": "test.echo", "id": 1}"#)
        .send()
        .await
        .unwrap();
    
    assert_eq!(response.headers().get("access-control-allow-origin").unwrap(), "*");
}
```

### 7. Concurrency Tests

#### Test: `concurrent__multiple_requests__then_all_handled`
```rust
#[tokio::test]
async fn test_concurrent_request_handling() {
    let server = start_test_server().await;
    let client = reqwest::Client::new();
    
    let mut handles = vec![];
    
    for i in 0..100 {
        let client = client.clone();
        let server_addr = server.addr.clone();
        
        let handle = tokio::spawn(async move {
            let request = format!(r#"{{
                "jsonrpc": "2.0",
                "method": "test.echo",
                "params": {{"id": {}}},
                "id": {}
            }}"#, i, i);
            
            client
                .post(&format!("http://{}/rpc", server_addr))
                .header("content-type", "application/json")
                .body(request)
                .send()
                .await
        });
        
        handles.push(handle);
    }
    
    let results = futures::future::join_all(handles).await;
    
    // All requests should succeed
    for result in results {
        let response = result.unwrap().unwrap();
        assert_eq!(response.status(), 200);
    }
}
```

### 8. Performance Tests

#### Test: `performance__request_throughput__then_meets_target`
```rust
#[tokio::test]
async fn test_request_throughput() {
    let server = start_test_server().await;
    let client = reqwest::Client::new();
    
    let start = Instant::now();
    let mut handles = vec![];
    
    for _ in 0..1000 {
        let client = client.clone();
        let server_addr = server.addr.clone();
        
        let handle = tokio::spawn(async move {
            client
                .post(&format!("http://{}/rpc", server_addr))
                .header("content-type", "application/json")
                .body(r#"{"jsonrpc": "2.0", "method": "test.fast", "id": 1}"#)
                .send()
                .await
        });
        
        handles.push(handle);
    }
    
    let results = futures::future::join_all(handles).await;
    let duration = start.elapsed();
    
    // All should succeed
    for result in results {
        assert!(result.unwrap().unwrap().status().is_success());
    }
    
    let requests_per_second = 1000.0 / duration.as_secs_f64();
    assert!(requests_per_second > 1000.0, 
           "Throughput {} req/s below target", requests_per_second);
}
```

#### Test: `performance__response_latency__then_low`
```rust
#[tokio::test]
async fn test_response_latency() {
    let server = start_test_server().await;
    let client = reqwest::Client::new();
    
    let mut latencies = vec![];
    
    for _ in 0..100 {
        let start = Instant::now();
        
        let response = client
            .post(&format!("http://{}/rpc", server.addr))
            .header("content-type", "application/json")
            .body(r#"{"jsonrpc": "2.0", "method": "test.fast", "id": 1}"#)
            .send()
            .await
            .unwrap();
            
        let latency = start.elapsed();
        latencies.push(latency);
        
        assert_eq!(response.status(), 200);
    }
    
    let avg_latency = latencies.iter().sum::<Duration>() / latencies.len() as u32;
    assert!(avg_latency < Duration::from_millis(10),
           "Average latency {:?} above target", avg_latency);
}
```

## Stress Test Scenarios
1. **High Concurrent Load**: 1000 simultaneous connections
2. **Rate Limit Testing**: Exceed limits from multiple IPs
3. **Memory Pressure**: Large request/response bodies
4. **Connection Churning**: Rapid connect/disconnect cycles

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Request throughput | >1000 req/s | Concurrent requests |
| Response latency | <10 ms | Round-trip time |
| Memory usage | <50 MB | Process RSS |
| Connection limit | 1000 concurrent | Active connections |

## Error Condition Testing
| Error Type | Test Scenario | Expected Response |
|------------|---------------|-------------------|
| Parse error | Invalid JSON | Code -32700 |
| Invalid request | Missing jsonrpc | Code -32600 |
| Method not found | Unknown method | Code -32601 |
| Invalid params | Bad parameters | Code -32602 |
| Internal error | Handler exception | Code -32603 |
| Rate limited | Too many requests | HTTP 429 |

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] JSON-RPC 2.0 specification compliance
- [ ] Handle concurrent requests efficiently
- [ ] Proper error handling and codes
- [ ] Rate limiting works per IP
- [ ] CORS support functional
- [ ] Request timeout handling
- [ ] Performance targets met
- [ ] Memory usage bounded
- [ ] Coverage ≥ 100% on changed lines