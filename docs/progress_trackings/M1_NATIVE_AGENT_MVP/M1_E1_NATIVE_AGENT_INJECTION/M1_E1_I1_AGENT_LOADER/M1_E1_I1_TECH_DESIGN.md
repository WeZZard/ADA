# Tech Design — M1 E1 I1 Agent Loader

## Objective
Load the native agent (.dylib) into the target via Frida QuickJS and invoke agent initialization with pid/session_id parameters.

## Design
- Compute absolute `agent_path` to `frida_agent` built by CMake under Cargo (predictable under `target/{profile}/.../libfrida_agent.*`).
- Create QuickJS script with:
  - `const mod = Module.load(agent_path);`
  - `const init = mod.getExportByName('frida_agent_init_with_ids');`
  - `if (init) new NativeFunction(init, 'void', ['uint32', 'uint32'])(pid, sid);`
  - Basic `rpc.exports.ping = () => 'ok'` for health.
- Keep script reference on controller; hook `message` for logs/errors.

## Interfaces
- `frida_controller_install_hooks(controller)` will: create and load loader script; return non-zero on error.
- Controller provides `pid` and `session_id` to the loader (spawn: env via Frida, attach: inline args).

## Risks
- Agent path resolution; OS-level permission errors; loader runtime mismatch.

## References
- docs/tech_designs/SHARED_MEMORY_IPC_MECHANISM.md

## Out of Scope
- JS-based event capture; detailed lane.
