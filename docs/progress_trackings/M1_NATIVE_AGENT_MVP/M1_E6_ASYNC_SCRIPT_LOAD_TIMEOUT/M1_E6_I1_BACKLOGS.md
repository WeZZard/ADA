# Backlogs

## Overview

- Implement asynchronous agent loader with computed timeout and user-type caps.
- Integrate symbol-count estimation, readiness gating, and CLI/env configuration.
- Deliver tests and telemetry.

## Work Items

| ID | Task | Priority | Estimate | Dependencies | Acceptance |
|----|------|----------|----------|--------------|------------|
| AL-001 | Switch to async load with `GCancellable` and `GMainLoop` | P0 | 6h | None | `frida_script_load` used at `tracer_backend/src/controller/frida_controller.cpp:633`; loop quits on finish/timeout; errors classified |
| AL-002 | Compute timeout from symbol count + tolerance | P0 | 4h | AL-005 | Formula applied; env/CLI caps enforced; logs include parameters |
| AL-003 | Gate resume on readiness signal | P0 | 4h | AL-001 | Resume only after ready; `tracer_backend/src/controller/frida_controller.cpp:470–483` gated |
| AL-004 | CLI parsing for `--hook-timeout-max-ms` and `--user-type` | P0 | 3h | None | Options parsed in `tracer_backend/src/controller/main.c:57–84`; usage updated |
| AL-005 | Symbol count retrieval (agent export and loader fallback) | P0 | 6h | None | `agent_estimate_hooks()` implemented; loader fallback enumerates and logs |
| AL-006 | Error handling and detach abort | P0 | 2h | AL-001 | Timeout recognized; script unloaded; detach aborts; no resume on failure |
| AL-007 | Telemetry (phase, parameters, elapsed) | P1 | 3h | AL-001 | Logs show symbol count, computed timeout, caps, progress |
| AL-008 | Progress-driven deadline extension (optional) | P2 | 4h | AL-005, AL-007 | Deadline extends on progress ticks, capped at `max_ms` |
| AL-009 | Environment variable support | P1 | 2h | AL-004 | `ADA_*` vars read; `ADA_SCRIPT_LOAD_TIMEOUT_MS` hard override |
| AL-010 | Tests: unit and integration | P0 | 8h | AL-001..AL-006 | Test plan implemented and passing |

## Acceptance Criteria

- Async load completes or times out cleanly; controller state consistent.
- Timeout computed from symbol count; respects min/max and tolerance; user-type defaults applied; CLI/env override works.
- Resume occurs only after readiness; no resume on failure/detach.
- Logs provide actionable telemetry.
- Tests cover required scenarios and pass.

## Risks

- Estimation variance → calibrate `per_symbol_ms`; collect metrics.
- Platform-specific hook costs → provide env overrides; conservative defaults.
- Integration complexity → incremental rollout; robust logging.

## Milestones

- M1_E6_I1 Async Script Load Timeout: design, implementation, tests, docs.