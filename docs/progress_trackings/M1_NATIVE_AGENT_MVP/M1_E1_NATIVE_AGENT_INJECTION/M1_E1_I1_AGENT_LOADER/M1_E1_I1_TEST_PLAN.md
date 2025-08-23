# Test Plan — M1 E1 I1 Agent Loader

## Goals
Verify the native agent loads via the QuickJS loader and runs `frida_agent_main`.

## Cases
- Loader creates without error; `on_message` logs contain loader banners.
- Agent prints `[Agent] Installed` line and hook count.
- Agent receives correct pid/session_id and opens SHM via unique names
- Failure modes: missing dylib path, permission denied — user-friendly error.

## Procedure
1. Build with `./utils/init_third_parties.sh && cargo build --release`.
2. Run `tracer spawn ./target/debug/test_cli --output traces/run1`.
3. Observe logs; confirm agent hook logs.
4. Validate agent saw pid/session_id (log or control block handshake)

## Acceptance
- No crashes; agent hook log visible; agent opened SHM with unique names; process resumes and runs for N seconds.
