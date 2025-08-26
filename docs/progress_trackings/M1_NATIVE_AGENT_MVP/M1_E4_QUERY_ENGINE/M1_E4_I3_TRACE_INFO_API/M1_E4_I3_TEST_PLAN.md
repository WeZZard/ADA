# Test Plan — M1 E4 I3 Trace Info API

## Objective
Validate the trace.info JSON-RPC endpoint provides accurate, fast trace metadata with proper caching, error handling, and optional features like checksums and event samples.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Handler Logic | ✓ | ✓ | ✓ |
| Caching System | ✓ | ✓ | ✓ |
| File Operations | ✓ | ✓ | - |
| Checksum Calculation | ✓ | - | ✓ |
| Event Sampling | ✓ | ✓ | - |
| Error Handling | ✓ | ✓ | - |

## Test Execution Sequence
1. Unit Tests → 2. Integration Tests → 3. Caching Tests → 4. Performance Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| handler__valid_trace__then_metadata | traceId: "test123" | TraceInfoResponse | All fields populated |
| handler__missing_trace__then_error | traceId: "nonexistent" | Error -32000 | Trace not found error |
| cache__fresh_entry__then_hit | Cached trace | From cache | <10ms response |
| checksums__enabled__then_md5 | includeChecksums: true | MD5 hashes | Correct checksums |
| samples__enabled__then_events | includeSamples: true | Event samples | Representative events |

## Test Categories

### 1. Basic Handler Tests

#### Test: `trace_info_handler__valid_trace__then_metadata`
```rust
#[tokio::test]
async fn test_valid_trace_metadata() {
    let handler = create_test_handler().await;
    let params = TraceInfoParams {
        trace_id: "valid_trace".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    let result = handler.get_trace_info(params).await.unwrap();
    
    assert_eq!(result.trace_id, "valid_trace");
    assert_eq!(result.os, "Darwin");
    assert_eq!(result.arch, "arm64");
    assert!(result.event_count > 0);
    assert!(result.span_count > 0);
    assert!(result.duration_ns > 0);
    assert!(result.files.total_size > 0);
}
```

#### Test: `trace_info_handler__missing_trace__then_error`
```rust
#[tokio::test]
async fn test_missing_trace() {
    let handler = create_test_handler().await;
    let params = TraceInfoParams {
        trace_id: "nonexistent_trace".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    let result = handler.get_trace_info(params).await;
    assert!(result.is_err());
    
    let error = result.unwrap_err();
    assert_eq!(error.code, -32000); // Trace not found
    assert!(error.message.contains("not found"));
}
```

#### Test: `trace_info_handler__corrupted_manifest__then_error`
```rust
#[tokio::test]
async fn test_corrupted_manifest() {
    let handler = create_test_handler_with_corrupted_manifest().await;
    let params = TraceInfoParams {
        trace_id: "corrupted_trace".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    let result = handler.get_trace_info(params).await;
    assert!(result.is_err());
    
    let error = result.unwrap_err();
    assert_eq!(error.code, -32603); // Internal error
}
```

### 2. Caching Tests

#### Test: `cache__first_request__then_miss_and_cache`
```rust
#[tokio::test]
async fn test_cache_miss_and_store() {
    let handler = create_test_handler().await;
    let params = TraceInfoParams {
        trace_id: "cache_test".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    // First request should be a cache miss
    let start = Instant::now();
    let result1 = handler.get_trace_info(params.clone()).await.unwrap();
    let first_duration = start.elapsed();
    
    // Second request should be a cache hit
    let start = Instant::now();
    let result2 = handler.get_trace_info(params).await.unwrap();
    let second_duration = start.elapsed();
    
    // Results should be identical
    assert_eq!(result1.trace_id, result2.trace_id);
    assert_eq!(result1.event_count, result2.event_count);
    
    // Second request should be much faster
    assert!(second_duration < first_duration / 2);
    assert!(second_duration < Duration::from_millis(10));
}
```

#### Test: `cache__ttl_expired__then_refresh`
```rust
#[tokio::test]
async fn test_cache_ttl_expiration() {
    let handler = create_test_handler_with_short_ttl(Duration::from_millis(100)).await;
    let params = TraceInfoParams {
        trace_id: "ttl_test".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    // First request
    handler.get_trace_info(params.clone()).await.unwrap();
    
    // Wait for TTL to expire
    tokio::time::sleep(Duration::from_millis(150)).await;
    
    // Second request should reload from filesystem
    let start = Instant::now();
    handler.get_trace_info(params).await.unwrap();
    let duration = start.elapsed();
    
    // Should take longer than a cache hit
    assert!(duration > Duration::from_millis(10));
}
```

#### Test: `cache__file_modified__then_invalidate`
```rust
#[tokio::test]
async fn test_cache_invalidation_on_file_change() {
    let temp_dir = create_temp_trace_directory();
    let handler = create_test_handler_with_dir(&temp_dir).await;
    let params = TraceInfoParams {
        trace_id: "mod_test".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    // First request
    let result1 = handler.get_trace_info(params.clone()).await.unwrap();
    
    // Modify the trace.json file
    modify_trace_manifest(&temp_dir.join("mod_test/trace.json"), "eventCount", 999999);
    
    // Second request should detect file change and reload
    let result2 = handler.get_trace_info(params).await.unwrap();
    
    assert_ne!(result1.event_count, result2.event_count);
    assert_eq!(result2.event_count, 999999);
}
```

### 3. Checksum Tests

#### Test: `checksums__enabled__then_correct_md5`
```rust
#[tokio::test]
async fn test_checksum_calculation() {
    let handler = create_test_handler().await;
    let params = TraceInfoParams {
        trace_id: "checksum_test".to_string(),
        include_checksums: true,
        include_samples: false,
    };
    
    let result = handler.get_trace_info(params).await.unwrap();
    
    assert!(result.checksums.is_some());
    let checksums = result.checksums.unwrap();
    
    // Validate MD5 format (32 hex characters)
    assert_eq!(checksums.manifest_md5.len(), 32);
    assert_eq!(checksums.events_md5.len(), 32);
    assert!(checksums.manifest_md5.chars().all(|c| c.is_ascii_hexdigit()));
    assert!(checksums.events_md5.chars().all(|c| c.is_ascii_hexdigit()));
    
    // Checksums should be consistent across calls
    let result2 = handler.get_trace_info(params).await.unwrap();
    let checksums2 = result2.checksums.unwrap();
    assert_eq!(checksums.manifest_md5, checksums2.manifest_md5);
    assert_eq!(checksums.events_md5, checksums2.events_md5);
}
```

#### Test: `checksums__disabled__then_none`
```rust
#[tokio::test]
async fn test_checksums_optional() {
    let handler = create_test_handler().await;
    let params = TraceInfoParams {
        trace_id: "no_checksum_test".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    let result = handler.get_trace_info(params).await.unwrap();
    assert!(result.checksums.is_none());
}
```

### 4. Event Sampling Tests

#### Test: `samples__enabled__then_representative_events`
```rust
#[tokio::test]
async fn test_event_sampling() {
    let handler = create_test_handler_with_large_trace().await; // 1000+ events
    let params = TraceInfoParams {
        trace_id: "large_trace".to_string(),
        include_checksums: false,
        include_samples: true,
    };
    
    let result = handler.get_trace_info(params).await.unwrap();
    
    assert!(result.samples.is_some());
    let samples = result.samples.unwrap();
    
    // Should have up to 5 events of each type
    assert!(samples.first_events.len() <= 5);
    assert!(samples.last_events.len() <= 5);
    assert!(samples.random_events.len() <= 5);
    
    // First events should have earliest timestamps
    if samples.first_events.len() >= 2 {
        assert!(samples.first_events[0].timestamp_ns <= samples.first_events[1].timestamp_ns);
    }
    
    // Events should have required fields
    for event in &samples.first_events {
        assert!(event.timestamp_ns > 0);
        assert!(event.thread_id > 0);
        assert!(!event.event_type.is_empty());
    }
}
```

#### Test: `samples__small_trace__then_all_events`
```rust
#[tokio::test]
async fn test_sampling_small_trace() {
    let handler = create_test_handler_with_small_trace(3).await; // Only 3 events
    let params = TraceInfoParams {
        trace_id: "small_trace".to_string(),
        include_checksums: false,
        include_samples: true,
    };
    
    let result = handler.get_trace_info(params).await.unwrap();
    let samples = result.samples.unwrap();
    
    // Should return all events as first_events, none as last/random
    assert_eq!(samples.first_events.len(), 3);
    assert!(samples.last_events.is_empty() || samples.last_events.len() <= 3);
}
```

### 5. File Information Tests

#### Test: `file_info__calculation__then_accurate`
```rust
#[tokio::test]
async fn test_file_info_calculation() {
    let handler = create_test_handler().await;
    let params = TraceInfoParams {
        trace_id: "file_test".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    let result = handler.get_trace_info(params).await.unwrap();
    let files = result.files;
    
    // File sizes should be positive
    assert!(files.manifest_size > 0);
    assert!(files.events_size > 0);
    assert_eq!(files.total_size, files.manifest_size + files.events_size);
    
    // Average event size should be reasonable
    if result.event_count > 0 {
        assert_eq!(files.avg_event_size, files.events_size / result.event_count);
        assert!(files.avg_event_size > 10); // Events should be at least 10 bytes
        assert!(files.avg_event_size < 1000); // Events shouldn't be huge
    }
}
```

### 6. Integration Tests

#### Test: `integration__json_rpc_request__then_valid_response`
```rust
#[tokio::test]
async fn test_json_rpc_integration() {
    let server = start_test_server_with_trace_handler().await;
    let client = reqwest::Client::new();
    
    let request = json!({
        "jsonrpc": "2.0",
        "method": "trace.info",
        "params": {
            "traceId": "integration_test",
            "includeChecksums": true,
            "includeSamples": true
        },
        "id": 1
    });
    
    let response = client
        .post(&format!("http://{}/rpc", server.addr))
        .header("content-type", "application/json")
        .json(&request)
        .send()
        .await
        .unwrap();
    
    assert_eq!(response.status(), 200);
    
    let json_response: JsonRpcResponse = response.json().await.unwrap();
    assert_eq!(json_response.jsonrpc, "2.0");
    assert!(json_response.result.is_some());
    assert!(json_response.error.is_none());
    
    let result: TraceInfoResponse = serde_json::from_value(json_response.result.unwrap()).unwrap();
    assert_eq!(result.trace_id, "integration_test");
    assert!(result.checksums.is_some());
    assert!(result.samples.is_some());
}
```

### 7. Performance Tests

#### Test: `performance__cached_requests__then_fast`
```rust
#[tokio::test]
async fn test_cached_request_performance() {
    let handler = create_test_handler().await;
    let params = TraceInfoParams {
        trace_id: "perf_test".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    // Warm up cache
    handler.get_trace_info(params.clone()).await.unwrap();
    
    // Measure cached performance
    let mut durations = Vec::new();
    for _ in 0..100 {
        let start = Instant::now();
        handler.get_trace_info(params.clone()).await.unwrap();
        durations.push(start.elapsed());
    }
    
    let avg_duration = durations.iter().sum::<Duration>() / durations.len() as u32;
    assert!(avg_duration < Duration::from_millis(5),
           "Average cached response time {:?} above target", avg_duration);
}
```

#### Test: `performance__concurrent_requests__then_cached_efficiently`
```rust
#[tokio::test]
async fn test_concurrent_cache_access() {
    let handler = Arc::new(create_test_handler().await);
    let params = TraceInfoParams {
        trace_id: "concurrent_test".to_string(),
        include_checksums: false,
        include_samples: false,
    };
    
    // Warm up cache
    handler.get_trace_info(params.clone()).await.unwrap();
    
    // Launch concurrent requests
    let mut handles = Vec::new();
    for _ in 0..50 {
        let handler = handler.clone();
        let params = params.clone();
        
        let handle = tokio::spawn(async move {
            let start = Instant::now();
            let result = handler.get_trace_info(params).await;
            (result, start.elapsed())
        });
        
        handles.push(handle);
    }
    
    let results = futures::future::join_all(handles).await;
    
    // All requests should succeed
    for result in results {
        let (trace_result, duration) = result.unwrap();
        assert!(trace_result.is_ok());
        assert!(duration < Duration::from_millis(50)); // Should be fast due to caching
    }
}
```

## Stress Test Scenarios
1. **Large Trace Files**: >1GB traces with millions of events
2. **Cache Pressure**: Many different traces accessed simultaneously
3. **File System Stress**: Rapid file modifications during requests
4. **Memory Usage**: Long-running server with cache turnover

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Cached response time | <5ms | Average latency |
| Uncached response time | <500ms | Cold load time |
| Checksum calculation | <2s for 1GB | MD5 computation |
| Sample extraction | <100ms | Event sampling |
| Memory usage per cache entry | <1MB | Process RSS |

## Error Condition Testing
| Error Type | Test Scenario | Expected Response |
|------------|---------------|-------------------|
| Trace not found | Invalid traceId | Code -32000 |
| Corrupted manifest | Malformed JSON | Code -32603 |
| File permission error | Unreadable files | Code -32603 |
| Invalid parameters | Missing traceId | Code -32602 |
| Disk full | Cannot calculate checksums | Code -32603 |

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] Accurate trace metadata extraction
- [ ] Efficient caching with TTL and modification detection
- [ ] Optional checksum calculation working
- [ ] Representative event sampling
- [ ] Proper JSON-RPC error handling
- [ ] Performance targets met
- [ ] Memory usage bounded
- [ ] Concurrent access safe
- [ ] Coverage ≥ 100% on changed lines