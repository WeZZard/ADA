# M1 Native Agent MVP

## Summary
Deliver an end-to-end native-agent tracing MVP on macOS (Apple Silicon): spawn/attach, inject C agent, collect index-lane events via shared-memory ring buffers, drain to disk, and provide a minimal summarize command.

## Scope (In)
- Native agent injection via Frida (QuickJS loader only)
- Baseline hooks on a deterministic set of functions
- Index lane collection → drain → durable binary file
- CLI flags: spawn/attach, output path, duration
- Minimal summarize over the produced file

## Out of Scope (M1)
- Detail lane capture and triggers (flight recorder)
- Full-coverage hooking, dynamic module tracking (beyond minimal set)
- ATF Protobuf encoding (binary fixed-record file for MVP)

## Acceptance Criteria
- Spawn/attach works against fixtures; agent loads and installs hooks
- Non-zero index events captured and persisted; timestamps monotonic
- Summarize prints totals, unique threads, top function IDs
- Unique shared memory names (role+pid+session_id) used end-to-end; spawn/attach parameterization proven
- Trace header includes pid and session_id
- docs/GETTING_STARTED.md created and README updated to link to it
- All tests pass; coverage 100% on newly added code

## Dependencies
- Frida SDK: 17.2.16 (macOS arm64) — initialized via `./utils/init_third_parties.sh`
- macOS: ≥ 15 (Sequoia); recommended ≥ 26 (Tahoe)
- Xcode Command Line Tools: ≥ 16, recommended ≥ 26
- CMake: ≥ 3.15 (backend), ≥ 3.10 (examples)
- Rust: stable (≥ 1.79)

## Risks
- Injection permissions; path resolution to agent dylib
- Timing nondeterminism; use bounded durations and range assertions

## Performance Considerations
- This milestone prioritizes correctness and stability over performance. Include a smoke check:
  - On `test_cli` over ~5s, events/sec should exceed a small floor (e.g., > 10k/s) with 0 drops under nominal settings
  - Drain is batched and buffered; avoid fsync-per-batch

## References
- specs/TRACER_SPEC.md (§4, FR-002/003/004/007, TD-001/002/003)
- specs/TRACE_SCHEMA.md (contract reference; MVP uses fixed-record file)
- tech_designs/NATIVE_TRACER_BACKEND_ARCHITECTURE.md
