# ADA Tracer Specification (MVP)

This document specifies the behavior and requirements of the ADA Tracer component (instrumentation backend + control plane). It does not define the event schema (see `docs/specs/TRACE_SCHEMA.md`), but the tracer must emit events conforming to that schema.

## 1. Scope and Goals
- Provide full-coverage function tracing for developer-owned macOS targets (Apple Silicon preferred) as the first MVP platform.
- Emit high-fidelity `FunctionCall` and `FunctionReturn` events with ABI-relevant registers and a shallow stack snapshot to the ADA Trace Format (ATF) using the V4 Protobuf schema.
- Prioritize performance and completeness for agentic debugging workflows.

## 2. Non-Goals (MVP)
- Hardened Runtime, system apps, or Mac App Store binaries.
- Interactive breakpoints/stepping.
- Windows support.

## 3. Definitions
- Tracer: Instrumentation backend + control plane that attaches/spawns, installs hooks, collects events, applies backpressure and overflow handling, and writes ATF.
- Full coverage: By default, hook all resolvable functions in the target program and all currently loaded—and subsequently loaded—dynamic libraries (DSOs).
- ATF: ADA Trace Format; directory containing length-delimited Protobuf `Event` stream and indexes (indexes may be minimal in MVP).

### 3.1 Terminology: Marked events and marking policy (M1)
- Marked event: an event that matches the current marking policy configured by the controller/agent. Marking does not reduce hook coverage; it annotates events of interest for detail persistence decisions.
- Marking policy: a rule set (symbols, patterns, heuristics) defining which events are considered “marked.” The policy can be updated between runs; live updates are future work.

## 4. Platform and Mechanisms
- macOS (MVP):
  - Mechanism: Frida Gum native (C/C++) hooks with a shared-memory ring buffer for hot-path logging.
  - Control plane: Rust process responsible for injection/attach, shared-memory setup, draining, encoding to ATF, and metrics.
  - Timestamps: Monotonic (mach_absolute_time) converted to ns.
  - Requirements: Dev/debug targets with entitlements that allow injection/JIT as needed.
  - Function identity: assign `moduleId` (small int per image) and `functionId = (moduleId << 32) | symbolIndex` (dense ordinal per image). Persist module UUID→moduleId mapping in manifest; record function tables’ hash.
- Linux (post-MVP parity):
  - Mechanism: eBPF uprobes/uretprobes (libbpf CO-RE) with perf ring buffer.
  - Control plane: Rust.

## 5. Functional Requirements (FR)
- FR-001 (MUST) Full-coverage by default
  - On start, install hooks on all resolvable functions in the main binary and all loaded DSOs; attach hooks for DSOs loaded later during runtime.
  - No allowlist is required to achieve coverage. Optional exclusions may exist but are off by default.
- FR-002 (MUST) Event emission
  - For each hooked function: emit a `FunctionCall` on entry and a `FunctionReturn` on exit per `TRACE_SCHEMA.md`.
- FR-003 (MUST) ABI registers
  - Capture only ABI-relevant argument registers on call and return-value registers on return; name keys by arch (e.g., `X0`, `RDI`, `RAX`).
- FR-004 (MUST) Shallow stack snapshot
  - Capture a shallow stack window from SP on entry. Default 128 bytes; configurable 0–512 bytes. Avoid reading below SP.
- FR-005 (MUST) Dynamic module tracking
  - Detect and hook functions from modules loaded after startup; avoid duplicates.
- FR-006 (MUST) Symbol strategy
  - Resolve function symbols from the main binary and DSOs. For inline/non-exported sites, coverage is best-effort; record IP always; symbolize offline where needed.
- FR-007 (MUST) Durable output
  - Write a length-delimited V4 Protobuf stream (`events.bin`) and a minimal `trace.json` manifest with OS/arch/start/end; tolerate process crashes (best-effort flush).
- FR-008 (SHOULD) Reentrancy guard
  - Prevent tracer logging from re-entering hot APIs being traced (e.g., allocator) to avoid recursion.
- FR-009 (SHOULD) Config interface
  - CLI/API to set output path, stack window size, optional excludes, sampling rate, and max depth.
- FR-010 (MUST) Key-symbol lane
  - Accept live-updatable per-module key-symbol selections with an adaptive container per module: choose dense bitset for small/dense symbol spaces, Roaring bitmap for clustered IDs, or a cacheline-friendly flat hash set (SwissTable-style) for sparse/large spaces. Membership checks must be O(1) on the hot path. Version and hash recorded in manifest; updates via RCU pointer swap.
- FR-011 (MUST) Platform limitation pre-check
  - Before attempting to spawn or attach, detect if target is a system binary or otherwise protected by platform security features.
  - Provide immediate user-friendly feedback without attempting doomed operations.
- FR-012 (MUST) Error message clarity
  - Error messages must distinguish between:
    - Fixable permission issues (e.g., missing Developer Tools permission)
    - Unfixable platform constraints (e.g., system binary protection)
  - Use plain language appropriate for developers, not kernel-level diagnostics.

## 6. Performance and Scale Requirements (PR)
- PR-001 (MUST) Hook coverage startup time
  - On an Apple Silicon dev machine (M3-class), installing hooks for ~5k symbols must complete in ≤ 3 seconds for the main binary; overall startup including DSOs ≤ 8 seconds under typical app loads.
- PR-002 (MUST) Sustained throughput
  - Sustain ≥ 5 million events/second across an 8‑core dev machine at default settings (trace‑all, stack window ≤ 128 bytes) with < 1% drop rate. On a per‑core basis, sustain ≥ 0.6–3.0 million events/second depending on workload (see §6.1 throughput budgeting). Defaults should be chosen to meet this bar on typical Apple Silicon dev hardware.
- PR-003 (MUST) Overhead bound
  - Add ≤ 10% CPU overhead on representative workloads at default settings (trace-all, 128-byte stack) when excluding extremely hot runtime symbols (see PR-005).
- PR-004 (MUST) Latency
  - Median end-to-end encode-to-disk latency ≤ 25 ms; 99th percentile ≤ 250 ms.
- PR-005 (SHOULD) Hot site policy
  - Document known pathological symbols (e.g., `objc_msgSend`, allocators) and provide guidance to re-run with targeted allowlists/excludes when traces exceed declared budgets.

### 6.1 Throughput budgeting (informative)
- Approximate event rate given CPU frequency, average cycles-per-call, and core count:
  - Calls/sec per core ≈ `freq_hz / cycles_per_call`
  - Events/sec per core (enter+leave) ≈ `2 × (freq_hz / cycles_per_call)`
  - Events/sec (N cores) ≈ `N × 2 × (freq_hz / cycles_per_call)`
- Example (3 GHz core):
  - 2,000 cycles/call → ≈ 3.0 M events/s per core; 8 cores → ≈ 24 M events/s
  - 10,000 cycles/call → ≈ 0.6 M events/s per core; 8 cores → ≈ 4.8 M events/s
- Planning guidance
  - Index lane should aim to sustain multi‑million events/sec across typical dev hardware (e.g., 5–25 M events/s depending on workload).
  - Detail lane must be windowed and gated (flight recorder) to keep aggregate throughput within storage budgets.
  - Size ring buffers and WAL throughput for the target peak rate R and burst window Tb (see BP‑004), using the above model to choose conservative defaults.

## 7. Backpressure and Overflow (BP)
- BP-001 (MUST) Sharded rings and priority lane
  - Use per-thread SPSC rings (L1) feeding an MPSC drain (L2). Provide a must-not-drop lane for crash/signal diagnostics.
- BP-002 (MUST) Watermarks and bounded spill (no sampling)
  - Enforce high/low watermarks per ring. Use a bounded pool of fixed-size rings per lane to achieve bounded spill without sampling. Do not apply random sampling or quality shedding.
- BP-003 (MUST) Drop policy and accounting
  - If overflow persists, drop-oldest on the lowest-priority lane only, and record explicit reason codes (e.g., ring full, encoder lag). Maintain bounded latency.

### 7.1 Lane-specific dump and ring-pool semantics (M1)
- Index lane (always-on persistence):
  - Use a bounded ring-pool: 1 active + K_index spares (small, e.g., 2–3). When the active ring becomes full, the agent submits the ring to the controller and atomically swaps to a spare. The controller dumps the submitted ring and returns it to the free pool. If the pool is exhausted, the agent continues with drop-oldest until a spare returns. Index lane must not block.
- Detail lane (always-on capture, windowed persistence):
  - Use a bounded ring-pool: 1 active + K_detail spares (small, e.g., 1–2). Dump is triggered only when both conditions hold: (a) the active detail ring is full and (b) at least one marked event has been seen since the last dump. On trigger, the agent submits the ring and swaps to a spare; the controller dumps and returns the ring. If the pool is exhausted, the agent continues with drop-oldest until a spare returns and coalesces further submissions.
- Metrics: track `*_dumps_total`, `*_dump_bytes_last`, `*_dump_ms_last`, `*_pool_free_min`, `*_pool_exhausted_count` for both lanes.
- BP-004 (MUST) Capacity planning
  - Support configuration of target peak rate (Rp) and burst window (Tb). Pre-size and pre-fault rings accordingly; document computed event capacity K = floor(R / s).

## 8. Throughput and Durability (TD)
- TD-001 (MUST) Compact event layout
  - Use IDs over strings, 32-bit address deltas from module base, varint timestamp deltas, presence bitmaps for ABI registers, and cap/disable stack snapshots (default 128B, configurable 0–512B).
- TD-002 (MUST) Memory and CPU efficiency
  - Cacheline-align records, single-writer/single-reader rings, batch drain, and pin the drain thread. Pre-fault and mlock rings; consider huge pages where available.
- TD-003 (MUST) Write-ahead logging
  - Append to preallocated segments with configurable flush policy: off, interval_ms, bytes, or on segment rotate. Avoid synchronous fsync on every batch; expose a safe mode separately.
- TD-004 (SHOULD) Offline transcode
  - Optionally write compact fixed records to WAL and transcode to Protobuf offline to decouple hot-path cost.
- TD-005 (MUST) Two-lane persistence (M1 semantics)
  - Index lane: always-on rolling ring with dump-on-full using a bounded ring-pool swap protocol.
  - Detail lane: always-on rolling ring with dump-on-full-and-marked using a bounded ring-pool swap protocol; “marked” is determined by the marking policy. This realizes windowed persistence without enabling/disabling capture.
  - Rings are fixed-size to bound dump time; the pool bounds memory usage.

### 8.1 Control block and memory model for ring-pool (M1)
- Control block (shared memory) includes for each lane:
  - `RingDescriptor rings[N]` containing base address, capacity, record size, id
  - `active_ring_idx` (atomic u32)
  - `submit_q` (agent→controller SPSC queue of ring indices) and `free_q` (controller→agent SPSC queue)
  - Detail-only: `marked_event_seen_since_last_dump` (atomic bool)
  - Metrics counters as listed in §7.1
- Memory ordering:
  - Agent: release when pushing to `submit_q` and when swapping `active_ring_idx`
  - Controller: acquire on `submit_q` pop and prior to snapshotting ring headers/pointers
  - Event writes use SPSC ring discipline; no OS locks on the hot path

## 9. Observability and Metrics (OB)
- OB-001 (MUST) Metrics
  - Expose: events/sec, drops/sec with reasons, buffer fill %, hook counts, attach time, encoder latency, process CPU/mem.
- OB-002 (SHOULD) Diagnostics
  - Optional verbose logs for attach/hook operations, module arrivals, and failure reasons.

## 10. Configuration and Interfaces (CF)
- CF-001 (MUST) CLI
  - `spawn <path> [-- args...]`, `attach <pid>`, `--out <dir>`, `--stack-bytes N`, `--exclude module[,module...]`.
- CF-002 (SHOULD) JSON-RPC
  - Control plane API allowing remote trigger of spawn/attach and configuration; returns trace directory path and metrics snapshot.
- CF-003 (MUST) Defaults
  - Full coverage ON; stack window 128 bytes; sampling OFF; excludes NONE.
 - CF-004 (MUST) Selective persistence mode
   - Configure always-on index lane capture/persistence and always-on detail lane capture with selective persistence using `--pre-roll-sec`, `--post-roll-sec`, and triggers (`--trigger symbol=foo`, `--trigger p99=function:bar>50ms`, `--trigger crash`, `--trigger time=10:30-10:45`). Triggers are armable without restart when supported.
- CF-005 (MUST) Key-symbol updates
  - Accept moduleId→container blobs (bitset/roaring/hash) over CLI/API to update key symbols live (double-buffer + RCU swap), without restart. Manifest records container type, version, and hash.

### 10.1 Permissions & Preflight (new)
- CF-006 (MUST) Preflight diagnostics API
  - Provide `/diagnose` (and CLI `diagnose <path|pid>`) that runs capability checks and returns a machine-readable report with ordered remediation steps.
  - Keep user-facing messages high-level; detailed procedures and platform terms are defined in `docs/specs/PERMISSIONS_AND_ENVIRONMENT.md`.
- CF-007 (MUST) Deterministic error-to-action mapping
  - Map common failures to exact remediations: Developer Tools permission, sudo recommendation, code-signing/library validation issues, JIT allowance, protected/SIP processes.
- CF-008 (MUST) Consent and guardrails
  - Never auto re-sign binaries; require explicit user confirmation for commands that alter code signatures or entitlements. Do not suggest SIP relaxations.

## 11. Reliability and Safety (RL)
- RL-001 (MUST) Crash-tolerance
  - Best-effort close/flush of trace on target/crash or tracer termination; manifest records abnormal termination.
- RL-002 (MUST) Stability
  - Instruction cache flush and thread-suspend semantics handled by Frida Gum; ensure multi-thread safe hook installation.
- RL-003 (MUST) Reentrancy and recursion limits
  - Guard against logging recursion; implement per-thread flags or TLS as needed.

## 12. Security and Permissions (SC)
- SC-001 (MUST) macOS entitlements
  - Document required entitlements (`get-task-allow`, `cs.disable-library-validation`, `cs.allow-jit`) for dev targets.
- SC-002 (MUST) Process boundaries
  - Only attach to processes owned by the user; deny privilege escalation.
- SC-003 (MUST) Platform limitation detection
  - Detect and report platform-specific limitations before attempting operations. See `docs/specs/PLATFORM_LIMITATIONS.md` for details.
- SC-004 (MUST) System binary handling
  - On macOS with SIP enabled, detect system binaries and provide user-friendly error messages explaining the limitation.
  - SHALL NOT attempt to attach to system binaries in protected paths.
  - SHALL NOT require or suggest disabling System Integrity Protection.
- SC-005 (MUST) Clear limitation communication
  - Distinguish between fixable permission issues and unfixable platform security constraints in error messages.
  - Provide platform-appropriate guidance without suggesting security-compromising workarounds.

## 13. Data Contract (DC)
- DC-001 (MUST) Schema compliance
  - Every event written must conform to `docs/specs/TRACE_SCHEMA.md` V4.
- DC-002 (MUST) Time base
  - Use monotonic clocks (mach time) for timestamps; encode as `google.protobuf.Timestamp` after conversion.
- DC-003 (SHOULD) Index stubs
  - Provide minimal indexes if feasible (e.g., time range, counts) to accelerate later queries.

## 14. Acceptance Criteria (MVP)
- A1: Full-coverage default
  - On a dev build of a sample app with ~5k exported/resolvable symbols, tracer installs hooks without user-provided allowlist; subsequent DSOs are detected and hooked.
- A2: Event integrity
  - Matched entry/return pairs for hooked functions with ABI registers present; stack bytes non-empty when configured > 0.
- A3: Performance
  - Meets PR-001..PR-004 on Apple Silicon dev hardware.
- A4: Backpressure behavior
  - Under synthetic overload, tracer maintains bounded latency using ring watermarks. If drops occur, they follow the drop-oldest policy with reason codes. No sampling or quality shedding is applied.
- A5: Output validity
  - Produces an ATF directory with length-delimited `Event` stream; passes a validator for schema compliance.
 - A6: Selective persistence
   - With Wpre/Wpost configured and a trigger fired, the trace contains full-detail persistence windows around marked events while the index lane is always persisted. Detail events are always captured but only persisted when marked events occur. Mode and window parameters are recorded in the manifest.

## 15. Test Plan Outline
- TP-001 Coverage startup test: measure hook count and time-to-hook on sample app + DSOs.
- TP-002 Throughput test: synthetic call generator to drive ≥ 100k events/sec; verify drop rate and latency.
- TP-003 Stability test: long-run (30–60 min) with mixed workloads; verify no deadlocks/crashes; metrics bounded.
- TP-004 Crash test: induced crash (signal) while tracing; verify best-effort flush and manifest.
- TP-005 Reentrancy test: instrument allocator hotspots; verify guards prevent recursion collapse.

## 16. References
- `docs/specs/TRACE_SCHEMA.md`
- `docs/TECHNICAL_ROADMAP.md`
- `docs/DEVELOPMENT_PLAN.md`
- `docs/technical_insights/EBPF_AND_FRIDA.md`
- `docs/technical_insights/BACKEND_SELECTION_GUIDE.md`

## 17. Open Issues
- OI-001: Default exclude list for known pathological hot symbols (while keeping policy “trace-all by default”).
- OI-002: Portable index format for ATF (beyond MVP).
- OI-003: Dynamic symbol resolution cost vs. precomputed symbol caches.
