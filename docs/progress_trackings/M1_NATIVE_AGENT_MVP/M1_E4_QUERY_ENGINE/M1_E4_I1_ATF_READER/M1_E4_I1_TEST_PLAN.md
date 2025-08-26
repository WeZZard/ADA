# Test Plan — M1 E4 I1 ATF Reader

## Objective
Validate ATF V4 file parsing provides efficient, accurate access to trace data with proper error handling and performance characteristics for the query engine foundation.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Manifest Parser | ✓ | ✓ | - |
| Memory Mapping | ✓ | ✓ | ✓ |
| Varint Decoder | ✓ | - | ✓ |
| Event Iterator | ✓ | ✓ | ✓ |
| Seek Operations | ✓ | ✓ | ✓ |
| Error Handling | ✓ | ✓ | - |

## Test Execution Sequence
1. Unit Tests → 2. Integration Tests → 3. Performance Tests → 4. Stress Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| manifest__valid_json__then_parsed | Valid trace.json | ManifestInfo struct | All fields populated |
| memory_map__file_exists__then_mapped | 1MB file | MemoryMap | Accessible buffer |
| varint__single_byte__then_decoded | [0x08] | 8 | Correct value |
| iterator__sequential__then_ordered | Event stream | Events 1,2,3... | Maintains order |
| seek__valid_offset__then_positioned | offset=1000 | position=1000 | Accurate position |

## Test Categories

### 1. Manifest Parsing Tests

#### Test: `manifest_parser__valid_json__then_parsed`
```rust
#[test]
fn test_valid_manifest() {
    let manifest_json = r#"{
        "os": "Darwin",
        "arch": "arm64", 
        "timeStartNs": 1634567890123456789,
        "timeEndNs": 1634567895123456789,
        "eventCount": 50000,
        "spanCount": 12500,
        "firstEventOffset": 0,
        "lastEventOffset": 1048576,
        "eventsFileSize": 1048580
    }"#;
    
    let manifest = ManifestInfo::from_json(manifest_json).unwrap();
    
    assert_eq!(manifest.os, "Darwin");
    assert_eq!(manifest.arch, "arm64");
    assert_eq!(manifest.event_count, 50000);
    assert_eq!(manifest.span_count, 12500);
    assert_eq!(manifest.time_start_ns, 1634567890123456789);
    assert_eq!(manifest.time_end_ns, 1634567895123456789);
}
```

#### Test: `manifest_parser__missing_field__then_error`
```rust
#[test]
fn test_missing_required_field() {
    let incomplete_json = r#"{
        "os": "Darwin",
        "arch": "arm64"
    }"#;
    
    let result = ManifestInfo::from_json(incomplete_json);
    assert!(result.is_err());
    assert!(matches!(result.unwrap_err(), AtfError::Json(_)));
}
```

#### Test: `manifest_parser__invalid_json__then_error`
```rust
#[test] 
fn test_malformed_json() {
    let invalid_json = r#"{ "os": "Darwin", "arch": "arm64"#; // Missing closing brace
    
    let result = ManifestInfo::from_json(invalid_json);
    assert!(result.is_err());
}
```

### 2. Memory Mapping Tests

#### Test: `memory_map__valid_file__then_accessible`
```rust
#[test]
fn test_memory_mapping() {
    // Create test file
    let temp_file = create_test_file_with_content(b"Hello, ATF!");
    
    let mmap = MemoryMap::new(&temp_file).unwrap();
    
    assert_eq!(mmap.size(), 12);
    assert_eq!(mmap.as_slice(), b"Hello, ATF!");
}
```

#### Test: `memory_map__large_file__then_efficient`
```rust
#[test]
fn test_large_file_mapping() {
    let large_file = create_test_file_with_size(100 * 1024 * 1024); // 100MB
    
    let start = Instant::now();
    let mmap = MemoryMap::new(&large_file).unwrap();
    let mapping_time = start.elapsed();
    
    assert_eq!(mmap.size(), 100 * 1024 * 1024);
    assert!(mapping_time < Duration::from_millis(10)); // Should be instant
    
    // Test random access
    let byte_at_middle = mmap.as_slice()[50 * 1024 * 1024];
    assert_eq!(byte_at_middle, 0); // Filled with zeros
}
```

#### Test: `memory_map__nonexistent_file__then_error`
```rust
#[test]
fn test_missing_file() {
    let result = MemoryMap::new(Path::new("/nonexistent/file.bin"));
    assert!(result.is_err());
    assert!(matches!(result.unwrap_err(), AtfError::Io(_)));
}
```

### 3. Varint Decoding Tests

#### Test: `varint__single_byte__then_correct`
```rust
#[test]
fn test_single_byte_varint() {
    let mut iterator = create_test_iterator(&[0x08]);
    let result = iterator.read_varint().unwrap();
    assert_eq!(result, 8);
    assert_eq!(iterator.position(), 1);
}
```

#### Test: `varint__multi_byte__then_correct`
```rust
#[test]
fn test_multi_byte_varint() {
    let mut iterator = create_test_iterator(&[0x96, 0x01]); // 150 in varint
    let result = iterator.read_varint().unwrap();
    assert_eq!(result, 150);
    assert_eq!(iterator.position(), 2);
}
```

#### Test: `varint__maximum_value__then_correct`
```rust
#[test]
fn test_maximum_varint() {
    // Maximum 32-bit value: 0xFFFFFFFF = [0xFF, 0xFF, 0xFF, 0xFF, 0x0F]
    let mut iterator = create_test_iterator(&[0xFF, 0xFF, 0xFF, 0xFF, 0x0F]);
    let result = iterator.read_varint().unwrap();
    assert_eq!(result, 0xFFFFFFFF);
}
```

#### Test: `varint__truncated__then_error`
```rust
#[test]
fn test_truncated_varint() {
    let mut iterator = create_test_iterator(&[0x96]); // Incomplete
    let result = iterator.read_varint();
    assert!(result.is_err());
    assert!(matches!(result.unwrap_err(), AtfError::UnexpectedEof));
}
```

### 4. Event Iterator Tests

#### Test: `event_iterator__sequential_read__then_ordered`
```rust
#[test]
fn test_sequential_event_reading() {
    let test_events = create_test_event_stream(100);
    let mut iterator = EventIterator::new(&test_events.buffer, test_events.count);
    
    for i in 0..100 {
        let event = iterator.next().unwrap();
        assert_eq!(event.timestamp_ns, 1000 + i); // Incremental timestamps
        assert_eq!(event.thread_id, 1);
    }
    
    // Should be exhausted
    assert!(iterator.next().is_none());
}
```

#### Test: `event_iterator__empty_stream__then_none`
```rust
#[test]
fn test_empty_event_stream() {
    let mut iterator = EventIterator::new(&[], 0);
    assert!(iterator.next().is_none());
    assert_eq!(iterator.remaining_count(), 0);
}
```

#### Test: `event_iterator__position_tracking__then_accurate`
```rust
#[test]
fn test_position_tracking() {
    let test_events = create_test_event_stream(10);
    let mut iterator = EventIterator::new(&test_events.buffer, test_events.count);
    
    let initial_pos = iterator.position();
    assert_eq!(initial_pos, 0);
    
    let _event = iterator.next().unwrap();
    let pos_after_first = iterator.position();
    assert!(pos_after_first > 0);
    
    let _event = iterator.next().unwrap();
    let pos_after_second = iterator.position();
    assert!(pos_after_second > pos_after_first);
}
```

### 5. Seek Operation Tests

#### Test: `seek__valid_offset__then_positioned`
```rust
#[test]
fn test_seek_to_valid_position() {
    let test_events = create_test_event_stream(100);
    let mut iterator = EventIterator::new(&test_events.buffer, test_events.count);
    
    let target_offset = 500;
    iterator.seek(target_offset).unwrap();
    assert_eq!(iterator.position(), target_offset);
}
```

#### Test: `seek__beyond_end__then_error`
```rust
#[test]
fn test_seek_beyond_end() {
    let test_events = create_test_event_stream(10);
    let mut iterator = EventIterator::new(&test_events.buffer, test_events.count);
    
    let result = iterator.seek(test_events.buffer.len() as u64 + 1);
    assert!(result.is_err());
    assert!(matches!(result.unwrap_err(), AtfError::SeekBeyondEnd));
}
```

#### Test: `seek__zero_position__then_reset`
```rust
#[test]
fn test_seek_to_beginning() {
    let test_events = create_test_event_stream(10);
    let mut iterator = EventIterator::new(&test_events.buffer, test_events.count);
    
    // Read a few events
    iterator.next();
    iterator.next();
    assert!(iterator.position() > 0);
    
    // Reset to beginning
    iterator.seek(0).unwrap();
    assert_eq!(iterator.position(), 0);
}
```

### 6. Integration Tests

#### Test: `integration__full_atf_file__then_complete_read`
```rust
#[test]
fn test_complete_atf_file() {
    // Create a complete ATF file with manifest + events
    let test_dir = create_test_atf_directory();
    
    let reader = ATFReader::open(&test_dir).unwrap();
    
    // Verify manifest
    assert_eq!(reader.manifest().event_count, 1000);
    assert_eq!(reader.manifest().os, "Darwin");
    
    // Read all events
    let mut events = Vec::new();
    let mut iterator = reader.event_iterator();
    
    while let Some(event) = iterator.next() {
        events.push(event);
    }
    
    assert_eq!(events.len(), 1000);
    
    // Verify first and last events
    assert_eq!(events[0].timestamp_ns, reader.manifest().time_start_ns);
    assert!(events[999].timestamp_ns <= reader.manifest().time_end_ns);
}
```

#### Test: `integration__corrupted_file__then_graceful_error`
```rust
#[test]
fn test_corrupted_events_file() {
    let test_dir = create_corrupted_atf_directory(); // Truncated events.bin
    
    let reader = ATFReader::open(&test_dir).unwrap();
    let mut iterator = reader.event_iterator();
    
    // Should read some events successfully
    let mut event_count = 0;
    while let Some(_event) = iterator.next() {
        event_count += 1;
        if event_count > 10 { break; } // Don't wait forever
    }
    
    // Eventually should hit corruption and stop
    assert!(event_count < reader.manifest().event_count);
}
```

### 7. Performance Tests

#### Test: `performance__sequential_throughput__then_fast`
```rust
#[test]
fn test_sequential_read_performance() {
    let large_trace = create_large_test_trace(10_000_000); // 10M events
    let reader = ATFReader::open(&large_trace.dir).unwrap();
    
    let start = Instant::now();
    let mut event_count = 0;
    let mut iterator = reader.event_iterator();
    
    while let Some(_event) = iterator.next() {
        event_count += 1;
    }
    
    let duration = start.elapsed();
    let throughput_mb_per_sec = (large_trace.file_size as f64 / 1_024_000.0) 
                               / duration.as_secs_f64();
    
    assert_eq!(event_count, 10_000_000);
    assert!(throughput_mb_per_sec > 100.0, 
           "Throughput {} MB/s below target", throughput_mb_per_sec);
}
```

#### Test: `performance__random_seeks__then_low_latency`
```rust
#[test]
fn test_random_access_performance() {
    let test_events = create_test_event_stream(1000);
    let mut iterator = EventIterator::new(&test_events.buffer, test_events.count);
    
    let mut seek_times = Vec::new();
    
    for _ in 0..100 {
        let random_offset = rand::random::<u64>() % (test_events.buffer.len() as u64);
        
        let start = Instant::now();
        iterator.seek(random_offset).unwrap();
        let seek_time = start.elapsed();
        
        seek_times.push(seek_time);
    }
    
    let avg_seek_time = seek_times.iter().sum::<Duration>() / seek_times.len() as u32;
    assert!(avg_seek_time < Duration::from_millis(1), 
           "Average seek time {:?} above target", avg_seek_time);
}
```

#### Test: `performance__memory_usage__then_bounded`
```rust
#[test]
fn test_memory_overhead() {
    let large_trace = create_large_test_trace(1_000_000);
    
    let memory_before = get_process_memory_usage();
    let reader = ATFReader::open(&large_trace.dir).unwrap();
    let memory_after = get_process_memory_usage();
    
    let overhead_mb = (memory_after - memory_before) / 1_048_576;
    assert!(overhead_mb < 10, "Memory overhead {} MB above target", overhead_mb);
}
```

## Stress Test Scenarios
1. **Large Files**: 10GB trace files with 100M+ events
2. **Concurrent Access**: Multiple readers on same file
3. **Memory Pressure**: Reading under low memory conditions
4. **Corrupted Data**: Various corruption patterns

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Sequential throughput | >100 MB/s | Read speed |
| Random access latency | <1 ms | Seek operation |
| Memory overhead | <10 MB | Process RSS |
| File size limit | 10 GB | Maximum supported |

## Error Conditions Testing
| Error Type | Test Scenario | Expected Behavior |
|------------|---------------|-------------------|
| Missing manifest | No trace.json | AtfError::Io |
| Invalid JSON | Malformed trace.json | AtfError::Json |
| Missing events file | No events.bin | AtfError::Io |
| Corrupted protobuf | Invalid event data | AtfError::Protobuf |
| Truncated file | Incomplete varint | AtfError::UnexpectedEof |
| Invalid seek | Offset beyond EOF | AtfError::SeekBeyondEnd |

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] Parse valid ATF V4 files correctly  
- [ ] Handle all error conditions gracefully
- [ ] Sequential throughput >100 MB/s
- [ ] Random access latency <1ms
- [ ] Memory overhead <10MB
- [ ] Support files up to 10GB
- [ ] Iterator maintains proper state
- [ ] Seek operations are accurate
- [ ] Coverage ≥ 100% on changed lines