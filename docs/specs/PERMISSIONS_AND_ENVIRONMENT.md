# Permissions and Environment (macOS-first)

This spec defines the preflight diagnostics, capability detection, and remediation mapping required to prepare a system and target binary for Frida-based tracing on macOS. Keep user stories free of jargon; this doc centralizes the technical procedures.

## Goals

- Deterministic, fast preflight: clear pass/fail with reasons
- Least-privilege remediations; no SIP relaxations
- Human-in-the-loop for any code-signing/entitlement changes

## Preflight Diagnostics

Input: target path or PID. Output: JSON report consumed by the AI agent and CLI.

Checks:

- System binary detection: is target in SIP-protected paths (/System, /bin, /sbin, /usr/bin)?
- Developer Tools permission (TCC): can we attach to a spawned helper owned by the same user?
- Need for sudo: attempt attach with/without sudo; classify based on error codes
- Target signing flags: hardened runtime, library validation, team id
- Entitlements: get-task-allow, cs.allow-jit, cs.disable-library-validation
- Agent signing: team id and signature presence
- JIT capability: required for Gum-based hooks
- Protected/SIP process: detected via path check and bundle identifiers

Schema (example):

```json
{
  "devtools_ok": false,
  "sudo_recommended": true,
  "target": {
    "path": "/path/app",
    "is_system_binary": false,
    "sip_protected": false,
    "signed": true,
    "team_id": "ABCDE12345",
    "hardened_runtime": true,
    "library_validation": true,
    "entitlements": {
      "get_task_allow": false,
      "cs_allow_jit": false,
      "cs_disable_library_validation": false
    }
  },
  "agent": {
    "path": "libagent.dylib",
    "signed": false,
    "team_id": null
  },
  "constraints": {
    "protected_process": false,
    "jit_required": true
  },
  "remediation": [
    { "id": "enable_devtools", "summary": "Enable Developer Tools permission for Terminal/iTerm", "steps": ["Open System Settings → Privacy & Security → Developer Tools", "Enable for your terminal app" ] },
    { "id": "run_with_sudo", "summary": "Run tracer with sudo", "steps": ["sudo ada start spawn ..."] }
  ]
}
```

## Error → Action Mapping

- Unable to access process / permission denied
  - If `devtools_ok` is false → enable Developer Tools
  - Else → run with sudo
- Code signature invalid / library validation failed
  - Option A (preferred for dev builds): disable Library Validation on target or add `cs.disable-library-validation`
  - Option B: sign agent with same Team ID as target
- JIT blocked / protection faults
  - Add `cs.allow-jit` to target (dev builds only)
- System binary detected
  - Report: "Cannot trace system binaries on macOS - they are protected by System Integrity Protection"
  - Suggest: "Please use your own binaries or development builds instead"
  - Do NOT suggest disabling SIP or other security-compromising workarounds
- Protected/SIP process
  - Declare unsupported; suggest tracing a dev build instead

## Remediation Procedures (macOS)

- Developer Tools permission
  - UI path: System Settings → Privacy & Security → Developer Tools → enable Terminal/iTerm
- Sudo
  - Re-run commands with `sudo -E` to preserve environment as needed
- Remote sessions (SSH)
  - GUI Developer Tools permission for Terminal.app does not apply to SSH shells (launched by `sshd`). Enable remote developer access and add your user to the developer group:

```bash
sudo /usr/sbin/DevToolsSecurity -enable
sudo dseditgroup -o edit -t user -a "$USER" _developer
```

- Even with the above, some targets still require `sudo` for attach/injection. Hardened Runtime or Library Validation constraints still apply.
- Optional lab setup: run `frida-server` as root on the target and connect remotely (`frida -H host:port`). This avoids TCC prompts but increases privilege and operational risk; use only in controlled environments.
- Code signing / entitlements (dev builds)
  - Generate minimal entitlements plist (example):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>com.apple.security.get-task-allow</key><true/>
  <key>com.apple.security.cs.allow-jit</key><true/>
  <key>com.apple.security.cs.disable-library-validation</key><true/>
</dict>
</plist>
```

## Runtime Controls (Startup Timeout)

The tracer enforces a unified startup timeout derived from the estimated hook installation workload plus tolerance. For expert scenarios, you may override or calibrate:

- CLI: `--startup-timeout <ms>` (milliseconds)
- Environment overrides:
  - `ADA_STARTUP_TIMEOUT` — hard override of the computed timeout (milliseconds)
  - `ADA_STARTUP_WARM_UP_DURATION` — warm-up duration used in estimation (milliseconds)
  - `ADA_STARTUP_PER_SYMBOL_COST` — per-symbol cost used in estimation (milliseconds)
  - `ADA_STARTUP_TIMEOUT_TOLERANCE` — fractional tolerance (e.g., 0.15 for 15%)

Use overrides sparingly; the default computed timeout is designed to serve the debugging task’s purpose: install hooks completely, then observe behavior.

- Re-sign (example):

```bash
codesign -s "Apple Development: Your Name (TEAMID)" \
  --entitlements entitlements.plist --force \
  /path/to/YourApp.app/Contents/MacOS/YourApp
```

- Agent signing (optional):

```bash
codesign -s "Apple Development: Your Name (TEAMID)" libagent.dylib
```

## API

- `GET /diagnose?path=<path>&pid=<pid>` → returns the JSON report above
- CLI: `ada diagnose <path|pid>` prints a human summary and writes JSON with `--json` flag

## Guardrails

- Do not suggest SIP relaxation or dtrace entitlements for Frida-based backend
- Never auto re-sign; only generate commands and ask for explicit user consent
- Keep messages concise; link to this spec for technical background

## Cross-Platform Notes (placeholders)

- Linux: CAP_SYS_PTRACE or `ptrace_scope`; SELinux/AppArmor contexts; eBPF permissions
- Windows: Debug privilege (SeDebugPrivilege), code integrity constraints, ETW alternatives
