# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## MANDATORY: Development Philosophy

### Core Value (BUILD) vs Engineering Efficiency (USE EXISTING)

**BUILD - Focus Engineering Effort on Here:**
**Core components found at the root directory** like:

- **tracer** (Rust): Dual-lane flight recorder control plane
- **tracer_backend** (C/C++): Native tracer backend.
- **query_engine** (Rust): Token-budget-aware analysis
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
│   ├── definitions/               # MEI-supporting artifacts (specs, stories, personas)
│   │   ├── behaviors/             # BDD behaviors (BH-XXX)
│   │   ├── constraints/           # System constraints (CN-XXX)
│   │   ├── environments/          # Environment specs (EV-XXX)
│   │   ├── user_stories/          # User story definitions (US-XXX)
│   │   ├── personas/              # User personas (PS-XXX)
│   │   └── enablers/              # Technical enablers (EN-XXX)
│   ├── design/                    # Design documentation (synthesis)
│   │   └── architecture/          # System architecture and decisions
│   ├── business/                  # Business analysis
│   ├── proposals/                 # Design proposals (PP-XXXX)
│   ├── sops/                      # Standard operating procedures
│   └── progress_trackings/        # CRITICAL FOR PLANNERS: Development workflow artifacts
│       └── M{X}_{MILESTONE_NAME}/           # Milestone folders (X = milestone number)
│           ├── milestone.md                 # Milestone target document
│           └── M{X}_E{Y}_{EPIC_NAME}/       # Epic folders (Y = epic number)
│               ├── epic.md                  # Epic target document
│               └── M{X}_E{Y}_I{Z}_{ITERATION_NAME}/ # Iteration folders (Z = iteration number)
│                   ├── 00-status.md         # Status tracking
│                   ├── tech-design.md       # Technical design
│                   ├── test-plan.md         # Test plan
│                   └── tasks.md             # Implementation tasks
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
├── query_engine/                 # Rust query engine
│   ├── Cargo.toml                # Rust manifest
│   └── pyproject.toml            # Python config (for legacy tests)
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
     - `query_engine/src/` (Rust traits)

2. **Skeleton Implementations**
   - Minimal skeletons that compile and link:
     - Return appropriate error values
     - Allow early integration testing
     - Located in `src/interfaces/` for validation

3. **Triple-Check Protocol for ADA**
   - **Check 1**: Interfaces compile in isolation
   - **Check 2**: Skeletons link with test harness
   - **Check 3**: Cross-language FFI boundaries validate

   When failures occur, automatically perform:
   - Interface Check → Verify C API/Rust traits/Python Protocols compile
   - Implementation Check → Validate against interface contracts
   - Test Check → Ensure tests use interfaces correctly

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
- **Architecture**: [`docs/design/architecture/`](docs/design/architecture/)
- **Definitions**: `docs/definitions/` (behaviors, constraints, user stories, personas)

**Additional references**:

- Business analysis: `docs/business/`
- User stories: `docs/definitions/user_stories/`
- Proposals: `docs/proposals/`
- Progress tracking: `docs/progress_trackings/`
- SOPs: `docs/sops/`

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
   - See `docs/definitions/constraints/` for platform constraints

## Focus Areas

When working on this project:

1. **Prioritize** core tracing components (tracer, backend, query engine)
2. **Reuse** existing tools for everything else
3. **Maintain** 100% quality gates
4. **Follow** existing patterns in the codebase
