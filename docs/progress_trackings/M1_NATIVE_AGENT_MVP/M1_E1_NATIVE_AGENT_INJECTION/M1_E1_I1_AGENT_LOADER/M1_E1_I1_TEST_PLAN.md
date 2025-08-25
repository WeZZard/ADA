# Test Plan — M1 E1 I1 Agent Loader

## Objective
Verify the native agent loads via the QuickJS loader and runs `frida_agent_main`.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| QuickJS Loader | ✓ | ✓ | - |
| Agent Injection | ✓ | ✓ | - |
| SHM Handshake | ✓ | ✓ | - |
| Error Handling | ✓ | ✓ | - |

## Test Execution Sequence
1. Unit Tests → 2. Integration Tests → 3. Error Injection Tests

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| agent_loader__valid_path__then_loads | Valid dylib path | Agent loads successfully | `[Agent] Installed` logged |
| agent_loader__invalid_path__then_error | Missing dylib | User-friendly error | Clear error message |
| agent_loader__receives_params__then_opens_shm | pid/session_id | SHM opened with unique names | Handshake completed |
| agent_loader__permission_denied__then_error | No permissions | Permission error | Suggests code signing |

## Test Categories

### 1. Functional Tests

#### Test: `agent_loader__spawn_mode__then_injects`
- **Setup**: Build tracer with `cargo build --release`
- **Execute**: `tracer spawn ./target/debug/test_cli`
- **Verify**: 
  - Agent prints `[Agent] Installed`
  - Hook count > 0
  - Process continues execution
- **Teardown**: Clean trace files

#### Test: `agent_loader__attach_mode__then_injects`
- **Setup**: Start test_runloop process
- **Execute**: `tracer attach <pid>`
- **Verify**: 
  - Agent attaches to running process
  - Hooks installed without crash
  - Process continues normally
- **Teardown**: Stop process, clean traces

#### Test: `agent_loader__shm_handshake__then_connects`
- **Setup**: Initialize SHM before injection
- **Execute**: Load agent with pid/session_id
- **Verify**:
  - Agent opens `/ada_shm_<pid>_<session_id>`
  - Control block accessible
  - Ring buffers mapped
- **Teardown**: Unmap SHM

### 2. Error Handling Tests

#### Test: `agent_loader__missing_dylib__then_user_error`
- **Setup**: Remove agent dylib
- **Execute**: Attempt injection
- **Verify**: Error message includes:
  - File path attempted
  - Suggestion to rebuild
  - No crash
- **Teardown**: Restore dylib

#### Test: `agent_loader__unsigned_binary__then_suggests_signing`
- **Setup**: Use unsigned test binary on macOS
- **Execute**: Attempt tracing
- **Verify**: Error suggests:
  - Run `./utils/sign_binary.sh`
  - Apple Developer requirement
  - Platform-specific help
- **Teardown**: N/A

### 3. Integration Tests

#### Test: `agent_loader__full_lifecycle__then_clean_shutdown`
- **Setup**: Build all components
- **Execute**: 
  1. Spawn with agent injection
  2. Run for 5 seconds
  3. Send SIGTERM
- **Verify**:
  - Agent loads successfully
  - Events captured to SHM
  - Clean shutdown logged
  - No memory leaks (ASAN)
- **Teardown**: Clean all artifacts

### 4. Platform-Specific Tests

#### macOS Tests
- Code signing validation
- Entitlements verification
- SIP bypass handling

#### Linux Tests
- ptrace capabilities check
- SELinux context validation

## Performance Benchmarks
| Metric | Target | Measurement |
|--------|--------|-------------|
| Injection time | < 100ms | Time from spawn to first hook |
| Memory overhead | < 10MB | RSS increase from agent |
| CPU overhead | < 1% | During idle after injection |

## Acceptance Criteria Checklist
- [ ] All unit tests pass
- [ ] Integration tests complete successfully
- [ ] No memory leaks detected (Valgrind/ASAN)
- [ ] Error messages are user-friendly
- [ ] Platform-specific requirements documented
- [ ] Agent logs contain expected messages
- [ ] SHM connection established with unique names
- [ ] Process continues execution after injection
- [ ] Coverage ≥ 100% on changed lines
