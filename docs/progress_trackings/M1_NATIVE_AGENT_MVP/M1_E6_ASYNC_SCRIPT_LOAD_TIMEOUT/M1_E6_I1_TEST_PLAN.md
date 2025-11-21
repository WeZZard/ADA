# Test Plan

## Objectives

- Validate asynchronous loader behavior, timeout computation, readiness gating, CLI/env controls, and error handling.

## Test Matrix

### Unit Tests (Controller)

- `async_load__timeout_enforced__then_cancellable_triggers`
  - Arrange: mock symbol count and compute timeout with tolerance.
  - Act: start `frida_script_load` with `GCancellable`; force timeout.
  - Assert: `frida_script_load_finish` returns error with timeout-class; script unloaded; controller failed.

- `async_load__success__then_no_timeout_and_no_error`
  - Arrange: short symbol count, long `max_ms`.
  - Act: load completes before deadline.
  - Assert: no error; readiness gate waits; no immediate resume.

- `detach__during_load__then_abort_and_fail`
  - Arrange: trigger session `detached` signal `tracer_backend/src/controller/frida_controller.cpp:426–431`.
  - Act: fire detach while loop active.
  - Assert: loop quits; controller failed; no resume.

- `timeout_compute__from_symbol_count__then_clamped_with_tolerance`
  - Arrange: set `startup_ms`, `per_symbol_ms`, `tolerance_pct`, `min_ms`, `max_ms`.
  - Act: compute `timeout_ms` for various counts.
  - Assert: clamps applied; tolerance added; logs include parameters.

- `env_override__ADA_SCRIPT_LOAD_TIMEOUT_MS__then_bypass_estimation`
  - Arrange: set `ADA_SCRIPT_LOAD_TIMEOUT_MS`.
  - Act: compute timeout.
  - Assert: uses override value; estimation skipped.

### Integration Tests (Loader + Agent)

- `loader_calls_estimate__agent_export_available__then_accurate_count`
  - Arrange: agent implements `agent_estimate_hooks()`.
  - Act: loader calls export before heavy work.
  - Assert: count returned; timeout computed accordingly.

- `loader_estimate_fallback__no_export__then_js_enumeration_used`
  - Arrange: agent lacks export.
  - Act: loader estimates via Frida APIs.
  - Assert: count derived; log path taken.

- `readiness_gate__signals_ready__then_resume_allowed`
  - Arrange: agent flips shared-memory flag or posts message.
  - Act: controller waits; then resumes at `tracer_backend/src/controller/frida_controller.cpp:470–483`.
  - Assert: resume only after ready; state transitions consistent.

- `timeout_class_failure__keep_suspended__then_no_resume`
  - Arrange: induce long hook install to exceed cap.
  - Act: observe timeout.
  - Assert: target remains suspended; controller failed; script unloaded.

### CLI / Env Behavior

- `cli_max_ms__human__default_60s__then_applied`
  - Arrange: `--user-type human`; no explicit `--hook-timeout-max-ms`.
  - Act: compute timeout.
  - Assert: `max_ms=60000` applied.

- `cli_max_ms__ai__default_180s__then_applied`
  - Arrange: `--user-type ai` or `--ai-agent`; no explicit max.
  - Act: compute timeout.
  - Assert: `max_ms=180000` applied.

- `cli_override__hook_timeout_max_ms__then_cap_changed`
  - Arrange: `--hook-timeout-max-ms 90000`.
  - Act: compute timeout.
  - Assert: cap is 90000.

- `env_user_type__ADA_USER_TYPE__then_defaults_match`
  - Arrange: set `ADA_USER_TYPE` and omit CLI.
  - Act/Assert: defaults match expected cap.

### Stress & Reliability

- `long_hook_install__progress_extension__then_no_premature_abort`
  - Arrange: loader/agent emit progress ticks; extend deadline in increments.
  - Act: observe dynamic deadline extension up to cap.
  - Assert: no abort while progress continues; final success or timeout at cap.

- `rapid_attach_detach__then_no_resource_leaks`
  - Arrange: repeated async loads and cancels.
  - Act: run under sanitizers.
  - Assert: no leaks; state reset between cycles.

## Instrumentation

- Capture logs for symbol count, computed timeout, caps, and phases.
- Verify readiness flag transitions in `control_block_`.

## Pass Criteria

- All unit and integration tests pass.
- Timeout behavior and gating verified under both human and AI defaults, with CLI/env overrides.
- No premature resume; no resource leaks; telemetry is present.