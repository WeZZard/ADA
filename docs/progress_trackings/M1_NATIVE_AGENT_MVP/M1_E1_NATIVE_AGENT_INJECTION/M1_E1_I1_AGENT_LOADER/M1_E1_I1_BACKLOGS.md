# Backlogs — M1 E1 I1 Agent Loader

- Implement controller absolute agent path resolution (debug/release)
- Generate QuickJS loader with inline path; load via frida_session_create_script_sync
- Wire `on_message` to surface loader logs and errors
- Keep script ref; unload on detach/destroy
- Add failure messages for missing agent or permission errors
- Provide pid/session_id to loader (spawn: env via Frida; attach: inline args)
- Add exported `frida_agent_init_with_ids(uint32_t pid, uint32_t sid)` entrypoint in agent
