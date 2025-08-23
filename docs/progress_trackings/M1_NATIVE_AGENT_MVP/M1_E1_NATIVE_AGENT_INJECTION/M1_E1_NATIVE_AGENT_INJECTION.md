# M1 E1: Native Agent Injection

## Goal
Reliably load the native agent (.dylib) into the target via Frida, pass pid/session_id to the agent, invoke initialization, then install baseline hooks.

## Deliverables
- QuickJS loader script created and loaded via frida-core
- Controller computes absolute agent path and handles errors/lifecycle
- Controller provides pid and session_id (spawn: env; attach: loader args)
- Agent exposes `frida_agent_init_with_ids(uint32_t pid, uint32_t sid)` and uses unique SHM names
- Agent runs and reports installed hooks

## Acceptance
- On `spawn test_cli`, loader logs appear; agent prints hook installation summary
- Agent opens SHM via `shared_memory_open_unique(role, pid, sid)` successfully
- No double-resume; detach cleans up without crashes

## References
- specs/TRACER_SPEC.md (§4, RL-002)
- docs/tech_designs/SHARED_MEMORY_IPC_MECHANISM.md
- tracer_backend/src/frida_controller.c, frida_agent.c
