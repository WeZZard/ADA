# PROJECT GUIDANCE FOR CLAUDE

This file provides guidance to Claude Code when working with code in this repository.

## MANDATORY: Project Structure

You **MUST** put files by stricly following the project structure definition mentioned below:

```plaintext
project-root/
├── Cargo.toml
├── docs/                           # Project-level documentation
│   ├── business/                   # Business analysis
│   │    └── BUSINESS_ANALYSIS_DOC_TITLE.md
│   ├── user_stories/               # User stories
│   │    └── USER_STORY_DOC_TITLE.md
│   ├── specs/                      # Tech specifications
│   │    └── SPEC_DOC_TITLE.md
│   ├── technical_insights/         # Technical insights
│   │    └── TECHNICAL_INSIGHT_DOC_TITLE.md
│   ├── engineering_efficiency/     # Best practices and tooling guides
│   │    └── ENGINEERING_EFFICIENCY_DOC_TITLE.md
│   └── progress_trackings/         # Project progress trackings
│        ├─── M1_MILESTONE_NAME_1/                       # Milestone 1 folder
│        │     ├─── M1_MILESTONE_NAME_1.md               # Milestone 1 description
│        │     ├─── M1_E1_EPIC_NAME_1/                   # Milestone 1 epic 1 folder
│        │     │     ├─── M1_E1_EPIC_NAME_1.md           # Milestone 1 epic 1 description
│        │     │     ├─── M1_E1_I1_ITERATION_NAME_1/     # Milestone 1 epic 1 iteration 1 folder
│        │     │     │    ├─── M1_E1_I1_TECH_DESIGN.md   # Milestone 1 epic 1 iteration 1 tech design doc
│        │     │     │    ├─── M1_E1_I1_TEST_PLAN.md     # Milestone 1 epic 1 iteration 1 test plan doc
│        │     │     │    └─── M1_E1_I1_BACKLOGS.md      # Milestone 1 epic 1 iteration 1 backlogs doc
│        │     │     └─── M1_E1_I2_ITERATION_NAME_2/     # Milestone 1 epic 1 iteration 2 folder
│        │     │          ├─── M1_E1_I2_TECH_DESIGN.md   # Milestone 1 epic 1 iteration 2 tech design doc
│        │     │          ├─── M1_E1_I2_TEST_PLAN.md     # Milestone 1 epic 1 iteration 2 test plan doc
│        │     │          └─── M1_E1_I2_BACKLOGS.md      # Milestone 1 epic 1 iteration 2 backlogs doc
│        │     └─── M1_E2_EPIC_NAME_2/                   # Milestone 1 epic 2 folder
│        │           ├─── M1_E2_EPIC_NAME_2.md           # Milestone 1 epic 2 description
│        │           ├─── M1_E2_I1_ITERATION_NAME_1/     # Milestone 1 epic 2 iteration 1 folder
│        │           │    ├─── M1_E2_I1_TECH_DESIGN.md   # Milestone 1 epic 2 iteration 1 tech design doc
│        │           │    ├─── M1_E2_I1_TEST_PLAN.md     # Milestone 1 epic 2 iteration 1 test plan doc
│        │           │    └─── M1_E2_I1_BACKLOGS.md      # Milestone 1 epic 2 iteration 1 backlogs doc
│        │           └─── M1_E2_I2_ITERATION_NAME_2/     # Milestone 1 epic 2 iteration 2 folder
│        │                ├─── M1_E2_I2_TECH_DESIGN.md   # Milestone 1 epic 2 iteration 2 tech design doc
│        │                ├─── M1_E2_I2_TEST_PLAN.md     # Milestone 1 epic 2 iteration 2 test plan doc
│        │                └─── M1_E2_I2_BACKLOGS.md      # Milestone 1 epic 2 iteration 2 backlogs doc
│        └─── M2_MILESTONE_NAME_2/                       # Milestone 2 folder
│              ├─── M2_MILESTONE_NAME_2.md               # Milestone 2 description
│              ├─── M2_E1_EPIC_NAME_1/                   # Milestone 2 epic 1 folder
│              │     ├─── M2_E1_EPIC_NAME_1.md           # Milestone 2 epic 1 description
│              │     ├─── M2_E1_I1_ITERATION_NAME_1/     # Milestone 2 epic 1 iteration 1 folder
│              │     │    ├─── M2_E1_I1_TECH_DESIGN.md   # Milestone 2 epic 1 iteration 1 tech design doc
│              │     │    ├─── M2_E1_I1_TEST_PLAN.md     # Milestone 2 epic 1 iteration 1 test plan doc
│              │     │    └─── M2_E1_I1_BACKLOGS.md      # Milestone 2 epic 1 iteration 1 backlogs doc
│              │     └─── M2_E1_I2_ITERATION_NAME_2/     # Milestone 2 epic 1 iteration 2 folder
│              │          ├─── M2_E1_I2_TECH_DESIGN.md   # Milestone 2 epic 1 iteration 2 tech design doc
│              │          ├─── M2_E1_I2_TEST_PLAN.md     # Milestone 2 epic 1 iteration 2 test plan doc
│              │          └─── M2_E1_I2_BACKLOGS.md      # Milestone 2 epic 1 iteration 2 backlogs doc
│              └─── M2_E2_EPIC_NAME_2/                   # Milestone 2 epic 2 folder
│                    ├─── M2_E2_EPIC_NAME_2.md           # Milestone 2 epic 2 description
│                    ├─── M2_E2_I1_ITERATION_NAME_1/     # Milestone 2 epic 2 iteration 1 folder
│                    │    ├─── M2_E2_I1_TECH_DESIGN.md   # Milestone 2 epic 2 iteration 1 tech design doc
│                    │    ├─── M2_E2_I1_TEST_PLAN.md     # Milestone 2 epic 2 iteration 1 test plan doc
│                    │    └─── M2_E2_I1_BACKLOGS.md      # Milestone 2 epic 2 iteration 1 backlogs doc
│                    └─── M2_E2_I2_ITERATION_NAME_2/     # Milestone 2 epic 2 iteration 2 folder
│                         ├─── M2_E2_I2_TECH_DESIGN.md   # Milestone 2 epic 2 iteration 2 tech design doc
│                         ├─── M2_E2_I2_TEST_PLAN.md     # Milestone 2 epic 2 iteration 2 test plan doc
│                         └─── M2_E2_I2_BACKLOGS.md      # Milestone 2 epic 2 iteration 2 backlogs doc
│
├── [python_component_name]/        # Python component
│   ├── Cargo.toml
│   ├── pyproject.toml
│   ├── src/                        # Python component Rust cargo crate dir
│   │   └── lib.rs                  # Python component Rust cargo crate definition
│   ├── docs/                       # Python component documentations
│   │   └── design/                 # Python component tech designs
│   ├── [python_component_name]/    # Python component sources
│   │   └── [modules]/              # Python component module soruces
│   └── tests/                      # Python component tests
│       ├── bench/                  # Python component benchmarks
│       ├── integration/            # Python component integration tests
│       ├── unit/                   # Python component unit tests  
│       └── fixtures/               # Python component test data/programs
│
├── [c_cpp_component_name]/         # C/C++ component
│   ├── CMakeLists.txt              # C/C++ component CMakeLists
│   ├── docs/                       # C/C++ component documentations
│   │   └── design/                 # C/C++ component tech designs
│   ├── include/                    # C/C++ component headers
│   │   └── [modules]/              # C/C++ component module public headers
│   ├── src/                        # C/C++ component sources
│   │   └── [modules]/              # C/C++ component module sources
│   └── tests/                      # C/C++ component tests
│       ├── bench/                  # C/C++ component benchmarks
│       ├── integration/            # C/C++ component integration tests
│       ├── unit/                   # C/C++ component unit tests
│       └── fixtures/               # C/C++ component test data/programs
│
├── [rust_component_name]/          # Rust component
│   ├── Cargo.toml
│   ├── docs/                       # Rust component documentation
│   │   └── design/                 # Rust component technical designs
│   ├── src/                        # Rust component sources
│   │   └── [modules]/              # Rust component source modules
│   └── tests/                      # Rust component tests
│       ├── bench/                  # Rust component benchmarks
│       ├── integration/            # Rust component integration tests
│       ├── unit/                   # Rust component unit tests  
│       └── fixtures/               # Rust component test data/programs
│
├── utils/                          # Scripts for engineering efficiency
│
├── tools/                          # Tools to complete or enhance the product workflow.
│
└── target/                         # Built products of the project (ignored in git)
```

**Document deprecations**:

Deprecated documents shall be renamed with a `[DEPRECATED]` prefix.

**Rules for maintaining the project structure:**

1. **MUST place project-level docs in the /docs/ directory and component-level docs in the {component}/docs directory** - Use appropriate subdirectories
2. **MUST place all the progress tracking documents in the /docs/progress_trackings directory** - Use appropriate subdirectories
3. **NEVER commit compiled binaries** - Add to .gitignore  

## MANDATORY: Components

- tracer: The tracer, written in Rust.
- tracer-backend: The backend of the traer, written in C/C++.
- query-engine: The query engine, written in Python.
- mcp_server: The MCP server of the entire system, written in Python.

## MANDATORY: Development Model

The development of this repository is driven by user story map, user stories and specifications.

### Workflow

This workflow organizes work into three levels:

- Milestones – High-level objectives with a target document describing overall goals.
- Epics – Thematic bodies of work aligned with a milestone, each with an epic target document.
- Iterations – Execution units within an epic, each containing:
  1. Technical design (how the solution is structured)
  2. Test plan (how the solution is validated)
  3. Backlogs (the tasks and stories to deliver)

## MANDATORY: Engineering

All code in this repository must be covered by automated tests and measured with code coverage tools:

- Rust – Use cargo built-in support (e.g., cargo tarpaulin or cargo llvm-cov).
- C / C++ – Organize with CMake and use LLVM coverage tools (e.g., llvm-profdata, llvm-cov) to calculate line coverage.
- Python – Use community standard tools (e.g., pytest, coverage.py) for reporting.

### Mission & Operating Rules

- **Single driver**: Cargo orchestrates everything. C/C++ is built as a leaf via build.rs (cmake crate). Python wheels are produced with maturin.
- **Idempotent & reproducible**: Never write outside target/ or the chosen build dir. Prefer pinned tool versions where possible.
- **Fail fast with context**: If a step fails, surface the exact command, tool versions, env vars, and last 50 lines of logs.

### Toolchain & Package Management Systems

- Rust: stable
- C/C++: CMake and clang
- Python: maturin

### Implementation Order

Build the project in the following sequence:  

1. **Architectural skeleton** – Establish the overall structure and ensure compilation succeeds.  
2. **Module skeletons** – Define modules with minimal stubs; compilation must succeed.  
3. **Class and function skeletons** – Define public interfaces and stubs; compilation must succeed.  
4. **Function implementations with TDD** – Implement algorithms incrementally with tests reflecting technical specifications.  
   - All tests must pass before proceeding.  
5. **Module-level integration tests** – Verify cooperation among classes within each module.  
   - All tests must pass before proceeding.  
6. **System-level integration tests** – Verify cooperation among modules.  
   - All tests must pass before proceeding.  
7. **User story validation** – Write integration tests against user stories to ensure functional requirements are met.  
   - All tests must pass before proceeding.  
8. **Benchmarking** – Add performance tests to confirm compliance with performance specifications.  
   - All tests must pass before finalizing.  

### Cross-Platform Notes

- macOS universal2: maturin build --release --target universal2-apple-darwin  
- Linux wheels: maturin build --release --manylinux 2014  
- Windows: ensure MSVC matches Python ABI.

Use .cargo/config.toml for non-default linkers/targets.  

### Versioning & Releases

- Rust crates: bump Cargo.toml, tag vX.Y.Z.  
- Python: bump pyproject.toml.  
- Keep CHANGELOG.md at root.

### Troubleshooting

- Old/missing CMake: cmake --version.  
- Link errors: check println!(cargo:rustc-link-lib=...).  
- Python import fails: ensure maturin develop ran in correct venv.  
- ABI mismatch: sync C headers with Rust extern signatures.  
- Crash and unexpected behaviors: use LLDB or pdb to conduct an interactive debug.

### Non-Goals

- Don’t introduce top-level CMake as driver.  
- Don’t write artifacts outside target/.  
- Don’t expose unstable Rust internals.  

## MANDATORY: Code Quality

### Pre-commit Hooks

Install a pre-commit hook to check the test coverage of the increased codes such that we can guarantee the code quality is improved each time we commit.

Install a post-commit hook to generate an overall test coverage report to read at local.

### Quality Gate Requirements (100% Mandatory)

1. **Build Success**: 100% - ALL builds of all the components must pass
2. **Test Success**: 100% - ALL tests of all the components must pass
3. **Test Coverage**: 100% - NO coverage gaps allowed for the increased codes.
4. **Integration Score**: 100/100 - NO exceptions, NO compromises

### ⚠️ NO BYPASS MECHANISMS

- **NO ignoring tests** - Fix root cause instead
- **NO reducing requirements** - 100% is mandatory  
- **NO "temporary" quality compromises** - Quality is permanent
- **NO git commit --no-verify** - Hooks are mandatory
