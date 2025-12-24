---
status: active
date: 2025-12-24
depends_on:
  - M1_E5_I1_ATF_V2_WRITER
  - M1_E5_I2_ATF_V2_READER
---

# M1_E5_I3 Technical Design: ATF V2 Integration & Cleanup

## Overview

This iteration integrates the ATF V2 Writer into the drain thread infrastructure and removes legacy protobuf dependencies to fully close the ATF V2 migration. It consolidates deferred items from M1_E5_I1 and M1_E5_I2.

## Goals

1. Integrate ATF V2 Writer with drain thread for production use
2. Remove protobuf dependencies from tracer_backend
3. Remove protobuf dependencies from query_engine
4. Update Query Engine API to export V2 types
5. Validate end-to-end with performance benchmarks

## Architecture

### Drain Thread Integration

```
┌─────────────────────────────────────────────────────────────────┐
│                    Tracer Runtime                                │
│                                                                  │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐         │
│  │  Thread 0   │    │  Thread 1   │    │  Thread N   │         │
│  │ Ring Buffer │    │ Ring Buffer │    │ Ring Buffer │         │
│  └──────┬──────┘    └──────┬──────┘    └──────┬──────┘         │
│         │                  │                  │                  │
│         └──────────────────┼──────────────────┘                  │
│                            │                                     │
│                            ▼                                     │
│                   ┌─────────────────┐                           │
│                   │   Drain Thread   │                           │
│                   │                  │                           │
│                   │  for each thread:│                           │
│                   │    drain ring    │                           │
│                   │    write ATF V2  │                           │
│                   └────────┬─────────┘                           │
│                            │                                     │
│              ┌─────────────┼─────────────┐                      │
│              ▼             ▼             ▼                      │
│    ┌──────────────┐ ┌──────────────┐ ┌──────────────┐          │
│    │ AtfThread    │ │ AtfThread    │ │ AtfThread    │          │
│    │ Writer 0     │ │ Writer 1     │ │ Writer N     │          │
│    └──────────────┘ └──────────────┘ └──────────────┘          │
└─────────────────────────────────────────────────────────────────┘
                            │
                            ▼
                    session_dir/
                    ├── manifest.json
                    ├── thread_0/
                    │   ├── index.atf
                    │   └── detail.atf
                    ├── thread_1/
                    │   ├── index.atf
                    │   └── detail.atf
                    └── thread_N/
                        ├── index.atf
                        └── detail.atf
```

### Integration Points

1. **Session Lifecycle**
   - On trace start: Create session directory, initialize manifest
   - Per thread registration: Create AtfThreadWriter
   - On drain: Write events from ring buffer to ATF V2 files
   - On trace stop: Finalize all writers, close manifest

2. **Ring Buffer → ATF V2 Writer**
   ```c
   // In drain thread
   void drain_thread_events(uint32_t thread_id) {
       AtfThreadWriter* writer = get_or_create_writer(thread_id);

       while (ring_buffer_has_events(thread_id)) {
           RingEvent* event = ring_buffer_pop(thread_id);

           // Write index event
           atf_thread_writer_write_index_event(
               writer,
               event->timestamp_ns,
               event->function_id,
               event->event_kind,
               event->call_depth,
               event->detail_seq  // u32::MAX if no detail
           );

           // Write detail event if present
           if (event->has_detail) {
               atf_thread_writer_write_detail_event(
                   writer,
                   event->detail_type,
                   event->detail_payload,
                   event->detail_length
               );
           }
       }
   }
   ```

3. **Session Finalization**
   ```c
   void finalize_trace_session(void) {
       // Finalize all thread writers (writes footers)
       for (uint32_t i = 0; i < thread_count; i++) {
           atf_thread_writer_finalize(writers[i]);
           atf_thread_writer_close(writers[i]);
       }

       // Write manifest.json
       write_session_manifest();
   }
   ```

## Protobuf Removal

### tracer_backend Cleanup

Files to remove:
- `tracer_backend/src/atf_v4/` (entire directory)
- References to protobuf-c in CMakeLists.txt
- Protobuf schema files if any

Files to modify:
- `tracer_backend/CMakeLists.txt` - Remove protobuf-c dependency
- `tracer_backend/src/drain/` - Update to use ATF V2 writer

### query_engine Cleanup

Files to remove:
- `query_engine/src/atf/reader.rs` (old V4 reader)
- `query_engine/python/query_engine/atf/reader.py` (old V4 reader)
- prost dependency from Cargo.toml

Files to modify:
- `query_engine/src/atf/mod.rs` - Export V2 types as default
- `query_engine/src/lib.rs` - Update public API
- Python package `__init__.py` files

## Query Engine API Updates

### Rust API

```rust
// query_engine/src/lib.rs
pub mod atf {
    // V2 is now the default
    pub use v2::{
        SessionReader,
        ThreadReader,
        IndexReader,
        DetailReader,
        IndexEvent,
        DetailEvent,
        AtfV2Error,
    };

    // V2 module still accessible for explicit imports
    pub mod v2;
}
```

### Python API

```python
# query_engine/python/query_engine/atf/__init__.py
from .v2 import (
    SessionReader,
    ThreadReader,
    IndexReader,
    DetailReader,
    IndexEvent,
    DetailEvent,
)

__all__ = [
    'SessionReader',
    'ThreadReader',
    'IndexReader',
    'DetailReader',
    'IndexEvent',
    'DetailEvent',
]
```

## Performance Validation

### Write Throughput Benchmark

Target: 10M+ events/sec

```c
// Benchmark: measure events written per second
void benchmark_write_throughput(void) {
    AtfThreadWriter* writer = atf_thread_writer_create(...);

    uint64_t start = get_monotonic_ns();

    for (int i = 0; i < 10_000_000; i++) {
        atf_thread_writer_write_index_event(writer, ...);
    }

    uint64_t elapsed = get_monotonic_ns() - start;
    double events_per_sec = 10_000_000.0 / (elapsed / 1e9);

    assert(events_per_sec > 10_000_000.0);
}
```

### Read Throughput Benchmark

Target: >1 GB/sec sequential read

```rust
#[test]
fn benchmark_read_throughput() {
    let reader = IndexReader::open(&large_file).unwrap();

    let start = Instant::now();
    let count = reader.iter().count();
    let elapsed = start.elapsed();

    let bytes = count * 32;
    let throughput = bytes as f64 / elapsed.as_secs_f64();

    assert!(throughput > 1_000_000_000.0); // 1 GB/sec
}
```

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Drain thread integration breaks existing functionality | High | Feature flag to switch between V4 and V2 |
| Performance regression after protobuf removal | Medium | Benchmark before/after comparison |
| API breaking changes | Medium | Deprecation warnings, migration guide |

## Success Criteria

1. Drain thread writes ATF V2 files during trace session
2. No protobuf dependencies in tracer_backend or query_engine
3. Query engine API exports V2 types as default
4. Write throughput exceeds 10M events/sec
5. Full end-to-end test: trace → drain → read → analyze

## References

- **M1_E5_I1**: ATF V2 Writer implementation
- **M1_E5_I2**: ATF V2 Reader implementation
- **Format Spec**: `docs/specs/TRACE_SCHEMA.md`
