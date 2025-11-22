# Objective

- Implement asynchronous script loading for the agent loader with a deadline enforced via `GCancellable`, computed from the estimated number of hooks to install plus tolerance.
- Keep the target process suspended until hooks are ready; resume only after an explicit readiness signal.
- Use a single principled startup timeout derived from the estimated work × tolerance; allow an expert override via CLI.

# Scope

- Replace synchronous load at `tracer_backend/src/controller/frida_controller.cpp:633` with asynchronous `frida_script_load` plus temporary `GMainLoop` and a GLib timeout.
- Add CLI flags and environment variables to control `max_ms` and user type.
- Add symbol-count estimation for timeout computation (agent export preferred; loader fallback acceptable).
- Harden error handling and telemetry.

# User Experience

- Human developer runs: conservative cap of 60s, fast feedback, clear logs on progress/timeouts.
- AI agent runs: cap of 180s, tolerates longer installation on large targets, detailed telemetry.
- CLI flags to customize behavior; environment variables for non-CLI integration.

# Architecture Changes

- Controller
  - Use `frida_script_load(script_, cancellable, callback, user_data)` instead of `frida_script_load_sync` at `tracer_backend/src/controller/frida_controller.cpp:633`.
  - Create `GMainLoop` bound to the controller’s `GMainContext`; schedule `g_timeout_add(timeout_ms, ...)` that cancels `GCancellable` and quits the loop on deadline.
  - In the async `callback`, invoke `frida_script_load_finish`, capture `GError*`, and quit the loop.
  - Classify failures: timeout (`FRIDA_ERROR_TIMED_OUT` or `G_IO_ERROR_CANCELLED` from deadline), other errors; unload script and keep target suspended on failure.
  - Gate `resume()` (`tracer_backend/src/controller/frida_controller.cpp:470–483`) on readiness.

- Loader Script
  - Before heavy agent work, call `agent_estimate_hooks()` (new native export) to obtain planned hook count.
  - If not available, use Frida JS APIs to estimate count with the same matching rules as the agent; log the chosen path.

- Agent (optional, recommended)
  - Add `agent_estimate_hooks()` native export returning the planned number of hooks.
  - Emit readiness signal after hooks are installed (e.g., shared-memory flag or a message the controller listens for).

# Timeout Computation

- Inputs (defaults; all tunable via env):
  - `startup_ms`=3000 (`ADA_STARTUP_WARM_UP_DURATION`)
  - `per_symbol_ms`=20 (`ADA_STARTUP_PER_SYMBOL_COST`)
  - `tolerance_pct`=0.15 (`ADA_STARTUP_TIMEOUT_TOLERANCE`)
  - Expert override `ADA_STARTUP_TIMEOUT` bypasses estimation.

- Formula
  - `estimated_ms = startup_ms + (symbol_count * per_symbol_ms)`
  - `timeout_ms = estimated_ms * (1 + tolerance_pct)`

# Async Flow

- Compute `timeout_ms` using symbol count and parameters.
- Create `GCancellable` and start async load:
  - `frida_script_load(script_, cancellable, on_load_finished, &ctx)` at `tracer_backend/src/controller/frida_controller.cpp:633`.
- Start temporary `GMainLoop` and `g_timeout_add(timeout_ms, on_load_timeout, &ctx)`.
- Quit loop on either callback finish or timeout; inspect `ctx.error`.
- On success: wait for explicit readiness; then resume.
- On failure: unload script; keep suspended; update controller state.

# Resume Gate

- Resume only after an explicit readiness signal from the agent:
  - Controller checks a shared-memory flag (`control_block_` readiness field) or a message indicating hooks are ready.
  - Resume path remains `tracer_backend/src/controller/frida_controller.cpp:470–483` but gated on readiness.

# CLI / Env Interface

- CLI addition (parsed in `tracer_backend/src/controller/main.c:57–84`):
  - `--startup-timeout <ms>`: expert override to set the startup timeout directly (milliseconds).
- Usage text updated in `tracer_backend/src/controller/cli_usage.c:25–27` to include the new option.
- Env variables for non-CLI contexts:
  - `ADA_STARTUP_TIMEOUT` (expert override)
  - `ADA_STARTUP_WARM_UP_DURATION`, `ADA_STARTUP_PER_SYMBOL_COST`, `ADA_STARTUP_TIMEOUT_TOLERANCE`

# Error Handling

- Timeout-class failures:
  - `error->domain == FRIDA_ERROR && error->code == FRIDA_ERROR_TIMED_OUT` or `G_IO_ERROR_CANCELLED` caused by our timeout.
  - Unload script; clear `script_`; mark controller failed; keep target suspended.
- Other errors: log, mark failed; do not resume.
- `detached` signal (`tracer_backend/src/controller/frida_controller.cpp:426–431`) immediately aborts load and marks failure.

# Telemetry

- Log:
  - Symbol count, computed `timeout_ms`, tolerance, caps
  - Phase messages: enumerating, installing, installed, ready
  - Elapsed checkpoints during wait
  - Outcome and parameters

# Integration Points

- Replace sync load: `tracer_backend/src/controller/frida_controller.cpp:633`
- Debug line after load: `tracer_backend/src/controller/frida_controller.cpp:634`
- Attach setup: `tracer_backend/src/controller/frida_controller.cpp:411–431`
- Resume path: `tracer_backend/src/controller/frida_controller.cpp:470–483`
- Detach signal: `tracer_backend/src/controller/frida_controller.cpp:426–431`
- CLI parsing: `tracer_backend/src/controller/main.c:57–84`
- Usage text: `tracer_backend/src/controller/cli_usage.c:25–27`

# Risks & Mitigations

- Inaccurate symbol estimation → Prefer agent-reported count; calibrate `per_symbol_ms`.
- Silent hangs if agent deadlocks → Enforce cap and detach abort; detailed telemetry.
- Overly aggressive timeouts for large apps → User-type defaults; CLI override; tolerance.
- Resume before readiness → Gate strictly on explicit ready signal.

# Definition of Done

- Async load implemented with `GCancellable` deadline and readiness gating.
- CLI and env controls wired; usage updated.
- Tests cover timeout, detach, readiness, human vs AI defaults, overrides.
- Telemetry provides sufficient insight.
- Documentation updated.
