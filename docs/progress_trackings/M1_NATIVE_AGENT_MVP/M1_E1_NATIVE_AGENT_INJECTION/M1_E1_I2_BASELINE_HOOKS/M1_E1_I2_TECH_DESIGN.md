# Tech Design — M1 E1 I2 Baseline Hooks

## Objective
Install a deterministic set of hooks from the native agent and emit index events into the ring buffer with TLS reentrancy guard.

## Design
- Use existing `frida_agent.c`:
  - TLS for reentrancy; thread id; call depth
  - Hook exports: `open, close, read, write, malloc, free, memcpy, memset`
  - Emit `IndexEvent` on enter/leave; capture minimal ABI registers and optional 128B stack window (guarded)
- Keep small set in MVP; reserve full coverage to a later epic.

## Shared Memory
- Open SHM via `shared_memory_open_unique(role, pid, session_id)` using ids provided by loader.
- Fail fast if ids mismatch or SHM open fails; surface clear error via controller.

## References
- docs/tech_designs/SHARED_MEMORY_IPC_MECHANISM.md

## Out of Scope
- Dynamic module tracking; symbol table scanning; key-symbol lane.
