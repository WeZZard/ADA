# M1_E5: ATF V2 - Raw Binary Trace Format

## Epic Goal

Replace protobuf-based ATF V4 with raw binary ATF V2 format to close the M1 MVP loop.

## Why a New Epic?

- Completed epics (E2, E4) should not be extended
- ATF format is cross-cutting (touches both writer and reader)
- Clean separation of concerns

## Supersession Map

| New Iteration | Supersedes | Reason |
|---------------|------------|--------|
| M1_E5_I1_ATF_V2_WRITER | M1_E2_I3_ATF_V4_WRITER | Protobuf encoding overhead incompatible with streaming throughput |
| M1_E5_I2_ATF_V2_READER | M1_E4_I1_ATF_READER | Reader updated for raw binary format |

## Iterations

- **M1_E5_I1**: ATF V2 Writer (C/C++ in tracer_backend) ✅
- **M1_E5_I2**: ATF V2 Reader (Rust/Python in query_engine) ✅
- **M1_E5_I3**: ATF V2 Integration & Cleanup (Drain thread + Protobuf removal)

## Key Design Decisions

### 1. Two Separate Files
- `index.atf` - All function call/return events at full throughput
- `detail.atf` - Rich context (registers, stack) when recording is active

### 2. Per-Thread Files
- No interleaved events
- Each thread writes to `thread_N/index.atf` + `thread_N/detail.atf`
- Perfect cache locality for per-thread analysis

### 3. Bidirectional Linking
- `IndexEvent.detail_seq` → forward link to detail event
- `DetailEventHeader.index_seq` → backward link to index event
- O(1) lookup in both directions

### 4. Platform Timing API (Genlock)
- `timestamp_ns` from platform continuous clock
- Cross-core synchronized by OS
- Continuous through sleep (critical for mobile)

### 5. Raw Binary Format
- Zero encoding overhead (direct memcpy)
- Fixed-size 32-byte index events
- Memory-mappable for O(1) random access

## Success Criteria

1. Tracer writes ATF v2 that query engine can read
2. Bidirectional O(1) lookup between index and detail events
3. 10M+ events/sec write throughput
4. Full round-trip tested (writer → reader)
5. Cross-thread merge-sort by `timestamp_ns` works correctly

## Dependencies

```
M1_E5_I1 (Writer) ──────────────→ M1_E5_I2 (Reader)
     │                                  │
     │ supersedes                       │ supersedes
     ▼                                  ▼
M1_E2_I3 (ATF V4 Writer)         M1_E4_I1 (ATF Reader)
```

## References

- **Format Specification**: `docs/specs/TRACE_SCHEMA.md`
- **Architecture**: `docs/specs/ARCHITECTURE.md`
- **Timing Synchronization**: Platform APIs per `ada-tracer-timing-spec.md`
