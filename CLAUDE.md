# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## MANDATORY: Development Philosophy

### Core Value (BUILD) vs Engineering Efficiency (USE EXISTING)

**BUILD - Focus Engineering Effort on Here:**
**Core components found at the root directory** like:

- **tracer** (Rust): Dual-lane flight recorder control plane
- **tracer_backend** (C/C++): Native tracer backend.
- **query_engine** (Python): Token-budget-aware analysis
- **mcp_server** (Python): Model Context Protocol interface
- **ATF format**: Custom trace format

These are ADA's innovation. Build carefully with focus on performance and correctness.

**USE EXISTING - Never Build These:**
**Engineering efficiency components** like:

- Coverage tools → Use `diff-cover`, `lcov`, `genhtml`
- Testing frameworks → Use Google Test, pytest, cargo test
- Linting/formatting → Use clippy, black, clang-format
- CI/CD → Use GitHub Actions
- Documentation generators → Use mdBook, Sphinx

**Decision Rule**: If it's not part of the ADA system shipped to the user, prefer to use an existing solution firstly.

## MANDATORY: Developer Requirements

### macOS Development

- **Apple Developer Certificate ($99/year)** - NO EXCEPTIONS
- Required for ALL testing and development
- See `docs/GETTING_STARTED.md#platform-specific-requirements`
- Without this: Quality gate fails, tests cannot run

## MANDATORY: Project Structure

Core components and critical directories:

```plaintext
project-root/
├── Cargo.toml                     # CRITICAL: Root workspace manifest - orchestrates ALL builds
├── docs/
│   ├── business/                  # Business analysis
│   ├── user_stories/              # User stories  
│   ├── specs/                     # Technical specifications
│   ├── technical_insights/        # Technical insights
│   │   ├── ada/                   # Technical insights for ADA
│   │   └── engineering_process/   # Technical insights for engineering_process
│   └── progress_trackings/        # CRITICAL FOR PLANNERS: Development workflow artifacts
│       └── M{X}_{MILESTONE_NAME}/           # Milestone folders (X = milestone number)
│           ├── M{X}_{MILESTONE_NAME}.md     # Milestone target document
│           └── M{X}_E{Y}_{EPIC_NAME}/       # Epic folders (Y = epic number)
│               ├── M{X}_E{Y}_{EPIC_NAME}.md # Epic target document
│               └── M{X}_E{Y}_I{Z}_{ITERATION_NAME}/ # Iteration folders (Z = iteration number)
│                   ├── M{X}_E{Y}_I{Z}_TECH_DESIGN.md
│                   ├── M{X}_E{Y}_I{Z}_TEST_PLAN.md
│                   └── M{X}_E{Y}_I{Z}_BACKLOGS.md
│
├── tracer/                       # Rust tracer (control plane)
│   └── Cargo.toml                # Component manifest
├── tracer_backend/               # C/C++ backend (data plane) - MODULAR STRUCTURE
│   ├── Cargo.toml                # CRITICAL: Rust manifest that orchestrates CMake
│   ├── build.rs                  # CRITICAL: Invokes CMake via cmake crate
│   ├── CMakeLists.txt            # Root CMake - includes subdirectories
│   ├── include/tracer_backend/   # PUBLIC headers only (opaque types)
│   │   └── {module}/             # {module} public API
│   ├── src/                      # PRIVATE implementation
│   │   ├── CMakeLists.txt        # Source modules build
│   │   └── {module}/             # {module} implementation
│   │       ├── CMakeLists.txt    # {module} build
│   │       ├── *.c/cpp           # Implementation files
│   │       └── *_private.h       # Private headers
│   └── tests/                    # Test files
│       ├── CMakeLists.txt        # Test modules build
│       ├── *.h                   # Shared headers in tests
│       ├── bench/                # Benchmarks
│       │   └── {module}/             # {module} benchmark
│       │       ├── CMakeLists.txt    # {module} benchmark build
│       │       ├── *.c/cpp           # Tests files
│       │       └── *.h               # Private headers for {module} benchmark
│       ├── unit/                 # Unit tests
│       │   └── {module}/             # {module} unit tests
│       │       ├── CMakeLists.txt    # {module} unit tests build
│       │       ├── *.c/cpp           # Tests files
│       │       └── *.h               # Private headers for {module} unit tests
│       └── integration/          # Integration tests
│           └── {module}/             # {module} unit tests
│               ├── CMakeLists.txt    # {module} unit tests build
│               ├── *.c/cpp           # Tests files
│               └── *.h               # Private headers for {module} integration tests
├── query_engine/                 # Python query engine
│   ├── Cargo.toml                # Rust manifest for Python binding
│   └── pyproject.toml            # Python config (built via maturin)
├── mcp_server/                   # Python MCP server
│   ├── Cargo.toml                # Rust manifest (if using maturin)
│   └── pyproject.toml            # Python config
├── utils/                        # Engineering efficiency scripts
├── third_parties/               # Frida SDK and dependencies
└── target/                      # Build outputs (git-ignored)
```

### Modular Directory Pattern (MANDATORY)

**Public/Private Separation:**

- `include/<component>/` - Public headers with opaque types ONLY
- `src/<module>/` - Private implementation with concrete definitions
- `src/<module>/*_private.h` - Private headers for internal use and tests

**CMake Modularity:**

- Each directory has its own CMakeLists.txt
- Root CMakeLists.txt uses `add_subdirectory()`
- Clear target dependencies between modules
- Tests in separate CMakeLists.txt

**Include Path Convention:**

- Public: `#include <tracer_backend/module/header.h>`
- Private: `#include "header_private.h"` (within module)
- Tests: Include private headers from `src/` for internals

**Rules:**

1. Component directories use snake_case
2. Never commit binaries to git
3. All outputs go to target/
4. Progress tracking documents are MANDATORY for planning
5. ALL builds go through root Cargo.toml - NEVER run CMake/pytest directly

## 🔴 CRITICAL: Build System & Common Mistakes

NOTICE: **CARGO IS THE SINGLE DRIVER - NO EXCEPTIONS**

```bash
cargo build --release           # Builds everything
cargo test --all               # Runs all tests
./utils/run_coverage.sh        # Coverage with existing tools
```

**⚠️ CRITICAL GOTCHA - C/C++ TESTS NEED TWO PLACES:**
When adding C/C++ tests:

1. ✅ CMakeLists.txt: `add_executable(test_xyz ...)`  
2. ✅ **build.rs: `("build/test_xyz", "test/test_xyz")`** ← EASY TO FORGET!

Missing step 2 = test won't be accessible via Cargo!

### Test Robustness Features (Updated 2025-01-03)

The C++/Rust test integration in `src/lib.rs` includes:

- **Crash Detection**: Segfaults/signals clearly reported with debugging steps
- **Timeout Protection**: 60s for unit tests, 120s for integration tests
- **Process Groups**: Unix tests run in separate groups for clean termination
- **Enhanced Diagnostics**: Shows environment, core dumps, debugging commands
- **Permission Checks**: Verifies test binaries are executable before running

Example error output:
```
❌ CRASH DETECTED: test_xyz terminated by signal 11 (SIGSEGV)
Debugging steps:
1. Run directly: /path/to/test_xyz
2. Debug with lldb: lldb /path/to/test_xyz
3. Check for core dump: ls -la core*
```

## Interface-Driven Development Workflow

### Interface Compilation as Gate 0

**MANDATORY**: All interfaces must compile before implementation begins.

1. **Define Interfaces First**
   - Complete, compilable interface definitions in:
     - `tracer_backend/include/tracer_backend/interfaces/` (C/C++)
     - `tracer/src/lib.rs` (Rust traits)
     - `query_engine/src/interfaces.py` (Python protocols)
     - `mcp_server/src/interfaces.py` (Python protocols)

2. **Skeleton Implementations**
   - Minimal skeletons that compile and link:
     - Return appropriate error values
     - Allow early integration testing
     - Located in `src/interfaces/` for validation

3. **Triple-Check Protocol for ADA**
   - **Check 1**: Interfaces compile in isolation
   - **Check 2**: Skeletons link with test harness
   - **Check 3**: Cross-language FFI boundaries validate

4. **Performance Contracts in Interfaces**
   - `<1μs` registration latency (enforced by benchmark)
   - `<10ns` fast path access (TLS)
   - Documented in interface headers as constants

### Cross-Language Interface Rules

1. **C++ ↔ Rust**: Opaque handles with C API
   - C++ owns concrete types
   - Rust sees only opaque pointers
   - Shared memory with atomic operations

2. **Rust ↔ Python**: PyO3 bindings
   - Rust controls lifecycle
   - Python gets safe wrappers

3. **Shared Memory**: Plain memory with atomics
   - No complex types across boundaries
   - Atomic operations for synchronization
   - Cache-line aligned for performance

## Interface-Driven Development Workflow (MANDATORY)

### Core Principle: Interfaces Compile First
**No implementation work begins until interfaces compile successfully.**

### Triple-Check Protocol for ADA
When failures occur, agents automatically perform:
1. **Interface Check** → Verify C API/Rust traits/Python Protocols compile
2. **Implementation Check** → Validate against interface contracts
3. **Test Check** → Ensure tests use interfaces correctly

### Automatic Agent Invocation

Claude Code will automatically invoke the appropriate global agents based on context:
- **Interface issues** → `@interface-enforcer` (highest priority)
- **Build/test failures** → `@{language}-integrator` → root cause analysis
- **Planning tasks** → `@iteration-planner` 
- **Architecture decisions** → `@architect` (must provide compilable interfaces)
- **Design reviews** → `@design-reviewer`
- **Language-specific work** → `@{language}-developer`
- **Git/PR operations** → `@integration-engineer`
- **Cross-language issues** → See `.claude/ada-integrator.yaml`

See `.claude/auto-agents.yaml` for trigger configuration.

### Available Agents

For specific development tasks, use the appropriate specialized agent in `.claude/agents/`:

**Planning & Architecture:**

- `iteration-planner` - Iteration planning with TDD workflow
- `architect` - System design and technical decisions
- `design-reviewer` - Reviews designs for maintainability and proposes alternatives

**Language-Specific Development:**

- `cpp-developer` - C/C++ implementation
- `rust-developer` - Rust implementation
- `python-developer` - Python implementation

**Testing:**

- `cpp-test-engineer` - C/C++ unit/integration tests
- `rust-test-engineer` - Rust tests
- `python-test-engineer` - Python tests
- `ffi-test-engineer` - Cross-language boundary testing
- `system-test-engineer` - End-to-end testing

**Debugging:**

- `cpp-debugger` - C/C++ debugging
- `rust-debugger` - Rust debugging
- `python-debugger` - Python debugging

**Performance & Integration:**

- `performance-benchmark-engineer` - Performance testing
- `integration-test-engineer` - Integration testing
- `unit-test-engineer` - Unit testing

## MANDATORY: Quality Requirements

**100% Mandatory - No Exceptions:**

1. Build Success: ALL components must build
2. Test Success: ALL tests must pass
3. Test Coverage: 100% coverage on changed lines
4. Integration Score: 100/100

**No Bypass Mechanisms:**

- NO `git commit --no-verify`
- NO ignoring failing tests
- NO reducing coverage requirements
- NO "temporary" workarounds

## Development Model

### Iteration Workflow (MANDATORY for planners)

Each iteration follows this TDD-based workflow:

1. **Plan** - Create Tech Design, Test Plan, Backlogs
2. **Prepare** - Create minimal compilable skeletons
3. **Specify** - Write failing unit tests first
4. **Build** - Implement until tests pass (TDD)
5. **Verify** - Module integration tests
6. **Validate** - System integration tests
7. **Accept** - User story validation
8. **Prove** - Performance benchmarks
9. **Close** - 100% coverage, docs updated

All artifacts go in: `docs/progress_trackings/M{X}_*/M{X}_E{Y}_*/M{X}_E{Y}_I{Z}_*/`
Where: X = milestone number, Y = epic number, Z = iteration number

## Key Documentation References

**Essential documents** - READ these when referenced:

- **Setup/Build**: [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md)
- **Architecture**: [`docs/specs/ARCHITECTURE.md`](docs/specs/ARCHITECTURE.md)
- **Engineering Process**: `docs/technical_insights/engineering_process/active/`
  - [Planning](docs/technical_insights/engineering_process/active/PLANNING.md)
  - [Coding](docs/technical_insights/engineering_process/active/CODING.md)
  - [Testing](docs/technical_insights/engineering_process/active/TESTING.md)
  - [Integration](docs/technical_insights/engineering_process/active/INTEGRATION.md)
  - [Debugging](docs/technical_insights/engineering_process/active/DEBUGGING.md)

**Additional references**:

- Business analysis: `docs/business/`
- User stories: `docs/user_stories/`
- ADA Core insights: `docs/technical_insights/ada_core/`
- Progress tracking: `docs/progress_trackings/`

Do NOT duplicate content from these documents - reference them.

## Test Naming Convention

Use behavioral naming for all tests:

```pseudo-code
<unit>__<condition>__then_<expected>
```

Example: `ring_buffer__overflow__then_wraps_around`

## Important Constraints

1. **C/C++ Testing**: MUST use Google Test framework
2. **Coverage**: MUST use diff-cover for enforcement
3. **Python**: Built via maturin, orchestrated by Cargo
4. **Frida**: Initialize with `./utils/init_third_parties.sh`
5. **Platform Security**: Platform-specific requirements for tracing
   - macOS: Run `./utils/sign_binary.sh <path>` for SSH/CI tracing
   - Linux: May need ptrace capabilities
   - See `docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md`

## Focus Areas

When working on this project:

1. **Prioritize** core tracing components (tracer, backend, query engine)
2. **Reuse** existing tools for everything else
3. **Maintain** 100% quality gates
4. **Follow** existing patterns in the codebase
