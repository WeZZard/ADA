---
status: active
date: 2025-12-24
depends_on:
  - M1_E5_I1_ATF_V2_WRITER
  - M1_E5_I2_ATF_V2_READER
---

# M1_E5_I3 Test Plan: ATF V2 Integration & Cleanup

## Test Coverage Map

| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Drain Thread Integration | - | ✓ | - |
| Session Lifecycle | - | ✓ | - |
| Ring Buffer → ATF Writer | - | ✓ | - |
| Protobuf Removal (Backend) | - | ✓ | - |
| Protobuf Removal (Query Engine) | - | ✓ | - |
| Query Engine API Exports | ✓ | ✓ | - |
| End-to-End Trace Flow | - | ✓ | - |
| Write Throughput | - | - | ✓ |
| Read Throughput | - | - | ✓ |

## Integration Tests

### 1. Drain Thread Integration Tests

```c
// test_drain_thread_atf_v2_integration.cpp

// ATF2-I-001: Drain thread creates ATF V2 files
void test_drain_thread__trace_session__then_creates_atf_v2_files() {
    // Setup: Start trace session
    start_trace_session("/tmp/test_session");

    // Execute: Generate events and drain
    generate_test_events(1000);
    drain_all_threads();
    stop_trace_session();

    // Assert: ATF V2 files exist
    assert_file_exists("/tmp/test_session/manifest.json");
    assert_file_exists("/tmp/test_session/thread_0/index.atf");
    assert_valid_atf_v2_header("/tmp/test_session/thread_0/index.atf");
}

// ATF2-I-001: Multiple threads create separate files
void test_drain_thread__multi_thread__then_per_thread_files() {
    start_trace_session("/tmp/test_session");

    // Generate events on multiple threads
    parallel_generate_events(4, 1000);  // 4 threads, 1000 events each
    drain_all_threads();
    stop_trace_session();

    // Assert: Each thread has its own files
    for (int i = 0; i < 4; i++) {
        char path[256];
        sprintf(path, "/tmp/test_session/thread_%d/index.atf", i);
        assert_file_exists(path);
    }
}

// ATF2-I-001: Session finalization writes footers
void test_drain_thread__session_finalize__then_footers_written() {
    start_trace_session("/tmp/test_session");
    generate_test_events(100);
    drain_all_threads();
    stop_trace_session();  // Should finalize

    // Assert: Footer is valid
    assert_valid_atf_v2_footer("/tmp/test_session/thread_0/index.atf");
}
```

### 2. End-to-End Trace Flow Tests

```rust
// test_e2e_trace_flow.rs

#[test]
fn test_e2e__trace_to_query__events_readable() {
    // Setup: Run a trace session (via C API)
    let session_dir = run_trace_session();

    // Execute: Read with query engine
    let reader = SessionReader::open(&session_dir).unwrap();

    // Assert: Events are readable and valid
    assert!(reader.event_count() > 0);
    for event in reader.merged_iter() {
        assert!(event.timestamp_ns > 0);
        assert!(event.function_id > 0);
    }
}

#[test]
fn test_e2e__bidirectional_links__all_valid() {
    let session_dir = run_trace_session_with_detail();
    let reader = SessionReader::open(&session_dir).unwrap();

    for thread in reader.threads() {
        for index_event in thread.index.iter() {
            if index_event.detail_seq != u32::MAX {
                let detail = thread.get_detail_for(index_event).unwrap();
                let back = thread.get_index_for(&detail).unwrap();
                assert_eq!(back.timestamp_ns, index_event.timestamp_ns);
            }
        }
    }
}

#[test]
fn test_e2e__merge_sort__globally_ordered() {
    let session_dir = run_multi_thread_trace(4);  // 4 threads
    let reader = SessionReader::open(&session_dir).unwrap();

    let mut prev_ts = 0;
    for (_, event) in reader.merged_iter() {
        assert!(event.timestamp_ns >= prev_ts);
        prev_ts = event.timestamp_ns;
    }
}
```

### 3. Protobuf Removal Verification Tests

```rust
// test_no_protobuf_dependencies.rs

#[test]
fn test_query_engine__no_prost_dependency() {
    // Check Cargo.toml doesn't have prost
    let cargo_toml = std::fs::read_to_string("query_engine/Cargo.toml").unwrap();
    assert!(!cargo_toml.contains("prost"));
}

#[test]
fn test_query_engine__atf_v2_is_default() {
    // Verify v2 types are exported at top level
    use query_engine::atf::{SessionReader, IndexReader, DetailReader};

    // These should compile - v2 is the default
    let _ = std::any::type_name::<SessionReader>();
}
```

```bash
# test_no_protobuf_backend.sh

# Verify no protobuf-c in tracer_backend
grep -r "protobuf" tracer_backend/CMakeLists.txt && exit 1
grep -r "protobuf-c" tracer_backend/src/ && exit 1

# Verify clean build without protobuf
cargo build -p tracer_backend --release
echo "No protobuf dependencies verified"
```

### 4. Query Engine API Tests

```rust
// test_query_engine_api.rs

#[test]
fn test_api__v2_types_exported_at_top_level() {
    // These should all work without explicit v2 module
    use query_engine::atf::SessionReader;
    use query_engine::atf::ThreadReader;
    use query_engine::atf::IndexReader;
    use query_engine::atf::DetailReader;
    use query_engine::atf::IndexEvent;
    use query_engine::atf::DetailEvent;
}

#[test]
fn test_api__python_imports_work() {
    use std::process::Command;

    let output = Command::new("python3")
        .args(["-c", "from query_engine.atf import SessionReader, IndexReader"])
        .output()
        .unwrap();

    assert!(output.status.success());
}
```

## Performance Tests

### 1. Write Throughput Benchmark

```c
// bench_write_throughput.cpp

void bench_write__10m_events__exceeds_target() {
    AtfThreadWriter* writer = atf_thread_writer_create(...);

    uint64_t start = mach_absolute_time();

    for (int i = 0; i < 10000000; i++) {
        atf_thread_writer_write_index_event(writer, ...);
    }

    atf_thread_writer_finalize(writer);
    uint64_t elapsed = mach_absolute_time() - start;

    double events_per_sec = 10000000.0 / (elapsed_ns / 1e9);

    printf("Write throughput: %.2f M events/sec\n", events_per_sec / 1e6);
    assert(events_per_sec > 10000000.0);  // Target: 10M/sec
}
```

### 2. Read Throughput Benchmark

```rust
// bench_read_throughput.rs

#[test]
fn bench_read__large_file__exceeds_1gb_sec() {
    let reader = IndexReader::open(&large_test_file).unwrap();
    let file_size = reader.len() as u64 * 32;  // 32 bytes per event

    let start = Instant::now();
    let count = reader.iter().count();
    let elapsed = start.elapsed();

    let throughput = file_size as f64 / elapsed.as_secs_f64();

    println!("Read throughput: {:.2} GB/sec", throughput / 1e9);
    assert!(throughput > 1_000_000_000.0);  // Target: 1 GB/sec
}
```

### 3. Latency Benchmark

```rust
#[test]
fn bench_random_access__under_1us() {
    let reader = IndexReader::open(&large_test_file).unwrap();
    let mut rng = rand::thread_rng();

    let mut latencies = Vec::new();
    for _ in 0..10000 {
        let seq = rng.gen_range(0..reader.len());

        let start = Instant::now();
        let _ = reader.get(seq);
        latencies.push(start.elapsed());
    }

    let avg = latencies.iter().sum::<Duration>() / latencies.len() as u32;

    println!("Avg random access latency: {:?}", avg);
    assert!(avg < Duration::from_micros(1));
}
```

## Test Fixtures

### Session Directory Setup

```rust
fn create_test_session(thread_count: usize, events_per_thread: u32) -> PathBuf {
    let dir = tempdir().unwrap();

    // Create manifest
    let manifest = json!({
        "threads": (0..thread_count).map(|i| {
            json!({"id": i, "has_detail": true})
        }).collect::<Vec<_>>(),
        "time_start_ns": 1000,
        "time_end_ns": 1000 + events_per_thread as u64 * 100
    });
    std::fs::write(dir.path().join("manifest.json"), manifest.to_string()).unwrap();

    // Create thread directories with ATF files
    for i in 0..thread_count {
        create_thread_files(&dir.path().join(format!("thread_{}", i)), events_per_thread);
    }

    dir.into_path()
}
```

## Success Criteria

1. All drain thread integration tests pass
2. End-to-end trace flow works (write → read → analyze)
3. No protobuf dependencies in either component
4. Query engine exports V2 types as default
5. Write throughput exceeds 10M events/sec
6. Read throughput exceeds 1 GB/sec
7. Random access latency under 1 microsecond
8. 100% coverage on new integration code
