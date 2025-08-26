# Test Plan — M1 E4 I4 Events/Spans API

## Objective
Validate the events.get and spans.list JSON-RPC endpoints provide accurate, efficient querying capabilities with proper filtering, pagination, projection, and span construction from event pairs.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Events Handler | ✓ | ✓ | ✓ |
| Spans Handler | ✓ | ✓ | ✓ |
| Query Engine | ✓ | ✓ | ✓ |
| Filter Pipeline | ✓ | - | ✓ |
| Span Builder | ✓ | ✓ | - |
| Pagination | ✓ | ✓ | ✓ |
| Projection | ✓ | ✓ | - |
| Query Caching | ✓ | ✓ | ✓ |

## Test Execution Sequence
1. Unit Tests → 2. Filter Tests → 3. Integration Tests → 4. Performance Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| events__basic_query__then_results | traceId: "test" | Event array | Matching events returned |
| events__time_filter__then_subset | timeStartNs: 1000 | Filtered events | Only events >= 1000ns |
| spans__build_pairs__then_durations | Entry/return events | Span array | Correct duration calc |
| pagination__offset_limit__then_page | offset: 100, limit: 50 | 50 events | Correct page returned |
| projection__selected_fields__then_partial | timestamp only | Minimal events | Only requested fields |

## Test Categories

### 1. Events Handler Tests

#### Test: `events_handler__basic_query__then_results`
```rust
#[tokio::test]
async fn test_basic_events_query() {
    let handler = create_test_events_handler().await;
    let params = EventsGetParams {
        trace_id: "basic_test".to_string(),
        filters: EventFilters::default(),
        projection: EventProjection::default(),
        offset: 0,
        limit: 1000,
        order_by: OrderBy::Timestamp,
        ascending: true,
    };
    
    let result = handler.get_events(params).await.unwrap();
    
    assert!(result.events.len() > 0);
    assert!(result.metadata.total_count > 0);
    assert_eq!(result.metadata.returned_count, result.events.len() as u64);
    assert_eq!(result.metadata.offset, 0);
    assert_eq!(result.metadata.limit, 1000);
    
    // Verify events are ordered by timestamp
    for window in result.events.windows(2) {
        if let (Some(t1), Some(t2)) = (window[0].timestamp_ns, window[1].timestamp_ns) {
            assert!(t1 <= t2, "Events should be ordered by timestamp");
        }
    }
}
```

#### Test: `events_handler__time_filter__then_subset`
```rust
#[tokio::test]
async fn test_time_range_filtering() {
    let handler = create_test_events_handler().await;
    let params = EventsGetParams {
        trace_id: "time_filter_test".to_string(),
        filters: EventFilters {
            time_start_ns: Some(5000),
            time_end_ns: Some(10000),
            ..Default::default()
        },
        projection: EventProjection::default(),
        offset: 0,
        limit: 1000,
        order_by: OrderBy::Timestamp,
        ascending: true,
    };
    
    let result = handler.get_events(params).await.unwrap();
    
    // All returned events should be within time range
    for event in &result.events {
        if let Some(timestamp) = event.timestamp_ns {
            assert!(timestamp >= 5000 && timestamp <= 10000,
                   "Event timestamp {} outside filter range", timestamp);
        }
    }
}
```

#### Test: `events_handler__function_filter__then_matches`
```rust
#[tokio::test]
async fn test_function_name_filtering() {
    let handler = create_test_events_handler().await;
    let target_functions = vec!["malloc".to_string(), "free".to_string()];
    let params = EventsGetParams {
        trace_id: "function_filter_test".to_string(),
        filters: EventFilters {
            function_names: Some(target_functions.clone()),
            ..Default::default()
        },
        projection: EventProjection {
            function_name: true,
            ..Default::default()
        },
        offset: 0,
        limit: 1000,
        order_by: OrderBy::Timestamp,
        ascending: true,
    };
    
    let result = handler.get_events(params).await.unwrap();
    
    // All returned events should match function filter
    for event in &result.events {
        if let Some(ref function_name) = event.function_name {
            assert!(target_functions.contains(function_name),
                   "Event function {} not in filter list", function_name);
        }
    }
}
```

#### Test: `events_handler__pagination__then_correct_pages`
```rust
#[tokio::test]
async fn test_event_pagination() {
    let handler = create_test_events_handler().await;
    let page_size = 100;
    
    // Get first page
    let page1_params = EventsGetParams {
        trace_id: "pagination_test".to_string(),
        filters: EventFilters::default(),
        projection: EventProjection::default(),
        offset: 0,
        limit: page_size,
        order_by: OrderBy::Timestamp,
        ascending: true,
    };
    
    let page1 = handler.get_events(page1_params).await.unwrap();
    assert_eq!(page1.events.len(), page_size as usize);
    assert_eq!(page1.metadata.offset, 0);
    assert!(page1.metadata.has_more);
    
    // Get second page
    let page2_params = EventsGetParams {
        offset: page_size,
        ..page1_params.clone()
    };
    
    let page2 = handler.get_events(page2_params).await.unwrap();
    assert_eq!(page2.metadata.offset, page_size);
    
    // Pages should not overlap
    if let (Some(last_t1), Some(first_t2)) = (
        page1.events.last().and_then(|e| e.timestamp_ns),
        page2.events.first().and_then(|e| e.timestamp_ns)
    ) {
        assert!(last_t1 <= first_t2, "Page overlap detected");
    }
}
```

### 2. Spans Handler Tests

#### Test: `spans_handler__build_spans__then_matched_pairs`
```rust
#[tokio::test]
async fn test_span_construction() {
    let handler = create_test_spans_handler().await;
    let params = SpansListParams {
        trace_id: "span_test".to_string(),
        filters: SpanFilters::default(),
        projection: SpanProjection::default(),
        offset: 0,
        limit: 1000,
        include_children: false,
        min_duration_ns: None,
    };
    
    let result = handler.list_spans(params).await.unwrap();
    
    assert!(result.spans.len() > 0);
    
    // Verify each span has valid timing
    for span in &result.spans {
        assert!(span.span_id.is_some());
        assert!(span.function_name.is_some());
        
        if let (Some(start), Some(end), Some(duration)) = (
            span.start_time_ns, span.end_time_ns, span.duration_ns
        ) {
            assert!(end >= start, "Span end before start");
            assert_eq!(duration, end - start, "Duration calculation incorrect");
        }
    }
}
```

#### Test: `spans_handler__nested_calls__then_proper_depth`
```rust
#[tokio::test]
async fn test_nested_span_depth() {
    let handler = create_test_spans_handler_with_nested_calls().await;
    let params = SpansListParams {
        trace_id: "nested_test".to_string(),
        filters: SpanFilters::default(),
        projection: SpanProjection {
            depth: true,
            ..Default::default()
        },
        offset: 0,
        limit: 1000,
        include_children: true,
        min_duration_ns: None,
    };
    
    let result = handler.list_spans(params).await.unwrap();
    
    // Should have spans at different depths
    let depths: Vec<u32> = result.spans.iter()
        .filter_map(|s| s.depth)
        .collect();
    
    assert!(depths.contains(&0), "Should have root level spans");
    assert!(depths.iter().any(|&d| d > 0), "Should have nested spans");
    
    // Verify depth consistency (child spans should have higher depth)
    // This requires ordering by start time and checking overlap
    let mut sorted_spans = result.spans.clone();
    sorted_spans.sort_by_key(|s| s.start_time_ns);
    
    // Add more sophisticated depth validation here
}
```

#### Test: `spans_handler__duration_filter__then_filtered`
```rust
#[tokio::test]
async fn test_span_duration_filtering() {
    let handler = create_test_spans_handler().await;
    let min_duration = 1000000; // 1ms in nanoseconds
    
    let params = SpansListParams {
        trace_id: "duration_filter_test".to_string(),
        filters: SpanFilters {
            min_duration_ns: Some(min_duration),
            ..Default::default()
        },
        projection: SpanProjection::default(),
        offset: 0,
        limit: 1000,
        include_children: false,
        min_duration_ns: Some(min_duration),
    };
    
    let result = handler.list_spans(params).await.unwrap();
    
    // All spans should meet minimum duration requirement
    for span in &result.spans {
        if let Some(duration) = span.duration_ns {
            assert!(duration >= min_duration,
                   "Span duration {} below minimum {}", duration, min_duration);
        }
    }
}
```

### 3. Query Engine Tests

#### Test: `query_engine__complex_filter__then_correct_subset`
```rust
#[tokio::test]
async fn test_complex_filtering() {
    let engine = create_test_query_engine().await;
    let params = EventsGetParams {
        trace_id: "complex_filter_test".to_string(),
        filters: EventFilters {
            time_start_ns: Some(1000),
            time_end_ns: Some(10000),
            thread_ids: Some(vec![1, 2, 3]),
            event_types: Some(vec![EventType::FunctionEntry]),
            function_names: Some(vec!["malloc".to_string(), "calloc".to_string()]),
            ..Default::default()
        },
        projection: EventProjection::default(),
        offset: 0,
        limit: 1000,
        order_by: OrderBy::Timestamp,
        ascending: true,
    };
    
    let result = engine.execute_events_query(params.clone()).await.unwrap();
    
    // Verify all filters are applied correctly
    for event in &result.events {
        // Time range
        if let Some(ts) = event.timestamp_ns {
            assert!(ts >= 1000 && ts <= 10000);
        }
        
        // Thread filter
        if let Some(tid) = event.thread_id {
            assert!(vec![1, 2, 3].contains(&tid));
        }
        
        // Event type
        if let Some(ref event_type) = event.event_type {
            assert_eq!(event_type, "ENTRY");
        }
        
        // Function name
        if let Some(ref func_name) = event.function_name {
            assert!(vec!["malloc", "calloc"].contains(&func_name.as_str()));
        }
    }
}
```

#### Test: `query_engine__projection__then_only_requested_fields`
```rust
#[tokio::test]
async fn test_field_projection() {
    let engine = create_test_query_engine().await;
    let params = EventsGetParams {
        trace_id: "projection_test".to_string(),
        filters: EventFilters::default(),
        projection: EventProjection {
            timestamp_ns: true,
            function_name: true,
            // All other fields false/default
            ..Default::default()
        },
        offset: 0,
        limit: 100,
        order_by: OrderBy::Timestamp,
        ascending: true,
    };
    
    let result = engine.execute_events_query(params).await.unwrap();
    
    for event in &result.events {
        // Should have projected fields
        assert!(event.timestamp_ns.is_some());
        assert!(event.function_name.is_some());
        
        // Should NOT have non-projected fields
        assert!(event.thread_id.is_none());
        assert!(event.event_type.is_none());
        assert!(event.module_name.is_none());
        assert!(event.function_id.is_none());
        assert!(event.return_address.is_none());
        assert!(event.return_value.is_none());
    }
}
```

### 4. Span Builder Tests

#### Test: `span_builder__simple_calls__then_correct_spans`
```rust
#[tokio::test]
async fn test_simple_span_construction() {
    let builder = create_test_span_builder();
    
    // Create test events: entry -> return
    let events = vec![
        create_function_entry_event(1000, 1, 100, "test_func", "test_module"),
        create_function_return_event(2000, 1, 100, 42),
    ];
    
    let spans = builder.build_spans_from_events(events).await.unwrap();
    
    assert_eq!(spans.len(), 1);
    let span = &spans[0];
    
    assert_eq!(span.start_time_ns, Some(1000));
    assert_eq!(span.end_time_ns, Some(2000));
    assert_eq!(span.duration_ns, Some(1000));
    assert_eq!(span.function_name, Some("test_func".to_string()));
    assert_eq!(span.thread_id, Some(1));
    assert_eq!(span.depth, Some(0));
}
```

#### Test: `span_builder__orphaned_returns__then_handled`
```rust
#[tokio::test]
async fn test_orphaned_return_handling() {
    let builder = create_test_span_builder();
    
    // Return without matching entry
    let events = vec![
        create_function_return_event(1000, 1, 100, 42),
        create_function_entry_event(2000, 1, 101, "test_func", "test_module"),
        create_function_return_event(3000, 1, 101, 0),
    ];
    
    let spans = builder.build_spans_from_events(events).await.unwrap();
    
    // Should only create span for matched entry/return pair
    assert_eq!(spans.len(), 1);
    assert_eq!(spans[0].start_time_ns, Some(2000));
    assert_eq!(spans[0].end_time_ns, Some(3000));
}
```

### 5. Integration Tests

#### Test: `integration__events_get__then_valid_json_rpc`
```rust
#[tokio::test]
async fn test_events_get_integration() {
    let server = start_test_server_with_handlers().await;
    let client = reqwest::Client::new();
    
    let request = json!({
        "jsonrpc": "2.0",
        "method": "events.get",
        "params": {
            "traceId": "integration_test",
            "filters": {
                "timeStartNs": 1000,
                "timeEndNs": 5000
            },
            "projection": {
                "timestampNs": true,
                "functionName": true
            },
            "offset": 0,
            "limit": 100
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
    assert!(json_response.result.is_some());
    
    let result: EventsGetResponse = serde_json::from_value(json_response.result.unwrap()).unwrap();
    assert!(result.events.len() <= 100);
    assert_eq!(result.metadata.offset, 0);
    assert_eq!(result.metadata.limit, 100);
}
```

#### Test: `integration__spans_list__then_valid_json_rpc`
```rust
#[tokio::test]
async fn test_spans_list_integration() {
    let server = start_test_server_with_handlers().await;
    let client = reqwest::Client::new();
    
    let request = json!({
        "jsonrpc": "2.0",
        "method": "spans.list",
        "params": {
            "traceId": "integration_test",
            "filters": {
                "minDurationNs": 1000000
            },
            "projection": {
                "spanId": true,
                "functionName": true,
                "durationNs": true
            },
            "offset": 0,
            "limit": 50
        },
        "id": 2
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
    assert!(json_response.result.is_some());
    
    let result: SpansListResponse = serde_json::from_value(json_response.result.unwrap()).unwrap();
    assert!(result.spans.len() <= 50);
    
    // All spans should meet duration filter
    for span in &result.spans {
        if let Some(duration) = span.duration_ns {
            assert!(duration >= 1000000);
        }
    }
}
```

### 6. Performance Tests

#### Test: `performance__large_trace_query__then_reasonable_time`
```rust
#[tokio::test]
async fn test_large_trace_performance() {
    let handler = create_test_events_handler_with_large_trace(1_000_000).await; // 1M events
    let params = EventsGetParams {
        trace_id: "large_trace_test".to_string(),
        filters: EventFilters {
            function_names: Some(vec!["malloc".to_string()]),
            ..Default::default()
        },
        projection: EventProjection::default(),
        offset: 0,
        limit: 1000,
        order_by: OrderBy::Timestamp,
        ascending: true,
    };
    
    let start = Instant::now();
    let result = handler.get_events(params).await.unwrap();
    let duration = start.elapsed();
    
    assert!(result.events.len() <= 1000);
    assert!(duration < Duration::from_secs(5), 
           "Query took too long: {:?}", duration);
    assert!(result.metadata.execution_time_ms < 5000);
}
```

#### Test: `performance__concurrent_queries__then_scalable`
```rust
#[tokio::test]
async fn test_concurrent_query_performance() {
    let handler = Arc::new(create_test_events_handler().await);
    let mut handles = Vec::new();
    
    for i in 0..20 {
        let handler = handler.clone();
        let handle = tokio::spawn(async move {
            let params = EventsGetParams {
                trace_id: "concurrent_test".to_string(),
                filters: EventFilters {
                    thread_ids: Some(vec![i % 4 + 1]),
                    ..Default::default()
                },
                projection: EventProjection::default(),
                offset: (i * 100) as u64,
                limit: 100,
                order_by: OrderBy::Timestamp,
                ascending: true,
            };
            
            let start = Instant::now();
            let result = handler.get_events(params).await;
            (result, start.elapsed())
        });
        
        handles.push(handle);
    }
    
    let results = futures::future::join_all(handles).await;
    
    for result in results {
        let (query_result, duration) = result.unwrap();
        assert!(query_result.is_ok());
        assert!(duration < Duration::from_secs(2));
    }
}
```

#### Test: `performance__span_building__then_efficient`
```rust
#[tokio::test]
async fn test_span_building_performance() {
    let handler = create_test_spans_handler_with_deep_nesting(10000, 50).await; // 10k events, 50 depth
    let params = SpansListParams {
        trace_id: "deep_nesting_test".to_string(),
        filters: SpanFilters::default(),
        projection: SpanProjection::default(),
        offset: 0,
        limit: 1000,
        include_children: true,
        min_duration_ns: None,
    };
    
    let start = Instant::now();
    let result = handler.list_spans(params).await.unwrap();
    let duration = start.elapsed();
    
    assert!(result.spans.len() > 0);
    assert!(duration < Duration::from_secs(3),
           "Span building took too long: {:?}", duration);
}
```

## Stress Test Scenarios
1. **Large Traces**: 10M+ events with complex filtering
2. **Deep Nesting**: 100+ levels of function call nesting  
3. **Concurrent Load**: 100+ simultaneous queries
4. **Memory Pressure**: Sustained querying without memory leaks
5. **Cache Thrashing**: Rapid queries with different parameters

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Basic query latency | <1s | Time to first result |
| Large trace query | <5s | 1M events with filters |
| Span construction | <3s | Complex nested calls |
| Memory usage | <100MB | Peak RSS during query |
| Concurrent throughput | >50 queries/s | Parallel execution |

## Error Condition Testing
| Error Type | Test Scenario | Expected Response |
|------------|---------------|-------------------|
| Invalid traceId | Non-existent trace | Code -32000 |
| Invalid parameters | limit > 10000 | Code -32602 |
| Corrupted trace data | Malformed events | Code -32603 |
| Query timeout | Very long query | Code -32603 |
| Memory exhaustion | Huge result set | Code -32603 |

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] events.get with all filter types working
- [ ] spans.list with proper entry/return pairing
- [ ] Pagination works correctly
- [ ] Field projection reduces response size
- [ ] Query caching improves repeat performance
- [ ] Large trace handling (1M+ events)
- [ ] Concurrent query support
- [ ] Proper error handling
- [ ] Performance targets met
- [ ] Memory usage bounded
- [ ] Coverage ≥ 100% on changed lines