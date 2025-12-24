---
status: completed
date: 2025-12-24
consolidates:
  - M1_E5_I1_BACKLOGS (ATF2-W-009, ATF2-W-013)
  - M1_E5_I2_BACKLOGS (ATF2-R-016, ATF2-R-017)
---

# M1_E5_I3 Backlogs: ATF V2 Integration & Cleanup

## Sprint Overview

**Duration**: 2 days (13 hours)
**Priority**: P0 (Critical - completes ATF V2 migration)
**Dependencies**: M1_E5_I1 (Writer), M1_E5_I2 (Reader)

## Consolidated Deferred Items

This iteration consolidates all deferred items from M1_E5_I1 and M1_E5_I2:

| Original ID | Description | New ID |
|-------------|-------------|--------|
| ATF2-W-009 | Drain Thread Integration | ATF2-I-001 |
| ATF2-W-013 | Remove Protobuf (Backend) | ATF2-I-002 |
| ATF2-R-016 | Remove Protobuf (Query Engine) | ATF2-I-003 |
| ATF2-R-017 | Update Query Engine API | ATF2-I-004 |
| (new) | Write Throughput Benchmark | ATF2-I-005 |
| (new) | End-to-End Integration Test | ATF2-I-006 |

---

## Day 1: Drain Thread Integration (8 hours)

### ATF2-I-001: Drain Thread Integration (P0, 4h)

**Files**: `tracer_backend/src/drain/`

This is the critical integration that connects the ATF V2 Writer to the production trace pipeline.

- [ ] Create session management API
  - [ ] `trace_session_start(session_dir)` - Initialize session, create manifest
  - [ ] `trace_session_stop()` - Finalize all writers, close session
  - [ ] `trace_session_get_stats()` - Return event counts, file sizes

- [ ] Integrate with thread registration
  - [ ] On thread register: Create `AtfThreadWriter` for thread
  - [ ] Store writer in thread-local or thread map
  - [ ] Handle thread deregistration (finalize writer)

- [ ] Modify drain loop to use ATF V2 writer
  - [ ] Replace existing ATF V4 write calls
  - [ ] Drain index events from ring buffer
  - [ ] Drain detail events when detail recording active
  - [ ] Handle back-pressure if writer is slow

- [ ] Implement session finalization
  - [ ] Finalize all thread writers (writes footers)
  - [ ] Generate `manifest.json` with thread list
  - [ ] Include time range and event counts

### ATF2-I-002: Remove Protobuf from tracer_backend (P1, 2h)

**Files**: `tracer_backend/CMakeLists.txt`, `tracer_backend/src/`

- [ ] Remove protobuf-c from CMakeLists.txt
  - [ ] Delete `find_package(Protobuf)` or similar
  - [ ] Remove protobuf include directories
  - [ ] Remove protobuf link libraries

- [ ] Delete ATF V4 writer code
  - [ ] Remove `tracer_backend/src/atf_v4/` directory (if exists)
  - [ ] Remove any `.proto` schema files
  - [ ] Remove generated protobuf code

- [ ] Update drain thread to not reference V4
  - [ ] Remove V4 writer includes
  - [ ] Remove V4 writer calls
  - [ ] Ensure V2 is the only path

- [ ] Verify clean build
  - [ ] `cargo build -p tracer_backend --release`
  - [ ] No protobuf-related warnings or errors

### ATF2-I-006: End-to-End Integration Test (P0, 2h)

**Files**: `tracer_backend/tests/integration/`

- [ ] Create end-to-end test harness
  - [ ] Start trace session programmatically
  - [ ] Generate known events via test hooks
  - [ ] Drain and finalize session
  - [ ] Read back with query engine
  - [ ] Verify event content matches

- [ ] Test multi-thread trace
  - [ ] Spawn 4 threads with distinct events
  - [ ] Verify per-thread files created
  - [ ] Verify merge-sort produces correct ordering

- [ ] Test detail event pairing
  - [ ] Generate events with detail data
  - [ ] Verify bidirectional links are valid
  - [ ] Test round-trip navigation

---

## Day 2: Query Engine Cleanup & Benchmarks (5 hours)

### ATF2-I-003: Remove Protobuf from query_engine (P1, 1h)

**Files**: `query_engine/Cargo.toml`, `query_engine/src/atf/`

- [ ] Remove prost dependency from Cargo.toml
  - [ ] Delete `prost = "..."` line
  - [ ] Delete `prost-build` from build-dependencies (if any)

- [ ] Delete old ATF V4 reader code
  - [ ] Remove `query_engine/src/atf/reader.rs` (V4 reader)
  - [ ] Remove `query_engine/python/query_engine/atf/reader.py` (V4 reader)
  - [ ] Remove any `.proto` files

- [ ] Update module exports
  - [ ] `query_engine/src/atf/mod.rs` - Only export v2

- [ ] Verify clean build
  - [ ] `cargo build -p query_engine --release`
  - [ ] `cargo test -p query_engine`

### ATF2-I-004: Update Query Engine API (P1, 1h)

**Files**: `query_engine/src/lib.rs`, Python `__init__.py`

- [ ] Export V2 types at top level (Rust)
  ```rust
  // query_engine/src/lib.rs
  pub mod atf {
      pub use v2::{SessionReader, ThreadReader, IndexReader, DetailReader};
      pub use v2::{IndexEvent, DetailEvent, AtfV2Error};
      pub mod v2;  // Still accessible for explicit imports
  }
  ```

- [ ] Export V2 types at top level (Python)
  ```python
  # query_engine/python/query_engine/atf/__init__.py
  from .v2 import SessionReader, ThreadReader, IndexReader, DetailReader
  ```

- [ ] Update documentation
  - [ ] Update docstrings to reference V2
  - [ ] Update any README examples

- [ ] Ensure backward compatibility
  - [ ] Existing code using `atf::v2::*` still works
  - [ ] New code can use `atf::*` directly

### ATF2-I-005: Write Throughput Benchmark (P2, 2h)

**Files**: `tracer_backend/tests/bench/`

- [ ] Implement write throughput benchmark
  - [ ] Measure events/sec for 10M events
  - [ ] Target: >10M events/sec
  - [ ] Report results in standardized format

- [ ] Implement read throughput benchmark
  - [ ] Measure GB/sec for sequential read
  - [ ] Target: >1 GB/sec
  - [ ] Use large test file (10M+ events)

- [ ] Implement latency benchmark
  - [ ] Measure random access latency
  - [ ] Target: <1 microsecond average
  - [ ] Test with various file sizes

- [ ] Add benchmark to CI (optional)
  - [ ] Run benchmarks on release builds
  - [ ] Track performance over time

---

## Definition of Done

### Functional Requirements

- [ ] Drain thread writes ATF V2 files during trace session
- [ ] Session lifecycle works (start → drain → stop → finalize)
- [ ] Multi-thread traces create per-thread files
- [ ] Manifest.json generated with correct thread list
- [ ] Footers written on finalization

### Cleanup Requirements

- [ ] No protobuf-c dependency in tracer_backend
- [ ] No prost dependency in query_engine
- [ ] Old V4 reader/writer code deleted
- [ ] Clean build with no warnings

### API Requirements

- [ ] Query engine exports V2 types as default
- [ ] Python imports work without explicit v2 module
- [ ] Backward compatible with explicit v2 imports

### Performance Requirements

- [ ] Write throughput exceeds 10M events/sec
- [ ] Read throughput exceeds 1 GB/sec
- [ ] Random access latency under 1 microsecond

### Quality Requirements

- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] 100% coverage on new code

---

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Drain thread integration breaks existing traces | High | Medium | Feature flag, gradual rollout |
| Performance regression after integration | Medium | Low | Benchmark before/after |
| API changes break downstream code | Medium | Low | Deprecation period, migration guide |
| Protobuf removal leaves orphaned code | Low | Medium | Thorough code search |

## Dependencies

### Depends On:
- M1_E5_I1: ATF V2 Writer (provides writer API)
- M1_E5_I2: ATF V2 Reader (provides reader for verification)

### Blocks:
- Full M1 MVP completion
- Production trace collection

---

## Task Summary

| Task ID | Description | Priority | Effort | Status |
|---------|-------------|----------|--------|--------|
| ATF2-I-001 | Drain Thread Integration | P0 | 4h | [ ] |
| ATF2-I-002 | Remove Protobuf (Backend) | P1 | 2h | [ ] |
| ATF2-I-003 | Remove Protobuf (Query Engine) | P1 | 1h | [ ] |
| ATF2-I-004 | Update Query Engine API | P1 | 1h | [ ] |
| ATF2-I-005 | Write Throughput Benchmark | P2 | 2h | [ ] |
| ATF2-I-006 | End-to-End Integration Test | P0 | 2h | [ ] |
| **Total** | | | **13h** | |
