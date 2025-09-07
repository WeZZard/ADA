# Global Backlog

## Native (Scriptless) Agent Injection on macOS

- Harden direct agent injection using Frida's Injector/device APIs without QuickJS.
- Collect dyld logs (DYLD_PRINT_LIBRARIES/APIS) and analyze loader failures such as "Module not found" / "Malformed dependency ordinal".
- Validate required signing/policy (consider disable-library-validation only for experiments).
- Replace QuickJS loader when stable across spawn/attach targets; update tests/docs; remove legacy code.

## macOS Codesigning Developer Experience

- Continue improving utils/sign_binary.sh ergonomics and integration gate timeouts/logging.
- Document common failure modes (keychain unlock, SSH/CI env).

## Performance/Observability

- Add targeted metrics/logging for injection timing and failure classification to speed diagnosis.

