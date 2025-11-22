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
  - Arrange: short symbol count; computed unified timeout.
  - Act: load completes before deadline.
  - Assert: no error; readiness gate waits; no immediate resume.

- `detach__during_load__then_abort_and_fail`
  - Arrange: trigger session `detached` signal `tracer_backend/src/controller/frida_controller.cpp:426–431`.
  - Act: fire detach while loop active.
  - Assert: loop quits; controller failed; no resume.

- `timeout_compute__from_symbol_count__then_uses_tolerance`
  - Arrange: set `startup_ms`, `per_symbol_ms`, `tolerance_pct`.
  - Act: compute `timeout_ms` for various counts.
  - Assert: tolerance applied; logs include parameters.

- `env_override__ADA_STARTUP_TIMEOUT__then_bypass_estimation`
  - Arrange: set `ADA_STARTUP_TIMEOUT`.
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

- `cli_override__startup_timeout__then_timeout_set`
  - Arrange: `--startup-timeout 90000`.
  - Act: compute timeout.
  - Assert: timeout is 90000 ms and estimation bypassed.

- `env_calibration__startup_params__then_estimation_uses_values`
  - Arrange: set `ADA_STARTUP_WARM_UP_DURATION`, `ADA_STARTUP_PER_SYMBOL_COST`, `ADA_STARTUP_TIMEOUT_TOLERANCE`.
  - Act: compute timeout from symbol count.
  - Assert: uses env values; logs include parameters.

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

- Capture logs for symbol count, computed timeout, and phases.
- Verify readiness flag transitions in `control_block_`.

## Pass Criteria

- All unit and integration tests pass.
- Timeout behavior and gating verified with unified policy and CLI/env overrides.
- No premature resume; no resource leaks; telemetry is present.
