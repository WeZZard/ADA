# Repository Guidelines

## Project Structure & Modules

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

## Build, Test, and Dev Commands

- Build all: `cargo build --release`
- Test all crates: `cargo test --all`
- Coverage dashboard: `./utils/run_coverage.sh`
- Install hooks: `./utils/install_hooks.sh` (pre‑commit quality gates)
- Query engine (optional local dev): `maturin develop -m query_engine/Cargo.toml` then `pytest -q` in `query_engine/`
- MCP server (optional): `pip install -e mcp_server[dev]` then `pytest -q` in `mcp_server/`

## Coding Style & Naming

- Rust: `rustfmt` defaults; lint with `cargo clippy -- -D warnings`. Files/Modules: `snake_case`.
- Python: `black` (88 cols), `ruff`, `mypy` (no untyped defs). Packages/files: `snake_case`.
- C/C++: `clang-format` (LLVM style if unspecified). Files: `snake_case.{c,cpp,h}`.
- Keep functions small, explicit errors via `anyhow/thiserror` (Rust) and status returns (C/C++).

## Testing Guidelines

- Rust: unit in `src` with `#[cfg(test)]`; integration in `tests/`. Run with `cargo test`.
- C++: GoogleTest in `tracer_backend/tests/`; prefer behavioral names like `component__case__then_expected`.
- Python: `pytest` with files `tests/test_*.py`. Coverage on changed lines must be 100% (see coverage script).

## Commit & Pull Request Guidelines

- Use Conventional Commits: `feat:`, `fix:`, `refactor(scope):`, `test:`, `docs:`, `chore:`.
- PRs must: describe changes, link issues, include tests, pass CI, and keep coverage at 100% on changed lines. Add logs/screenshots for trace‑related changes.

## Security & Platform Notes

- macOS tracing may require entitlements/signing; see `docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md`.
- Tests set `ADA_WORKSPACE_ROOT`/`ADA_BUILD_PROFILE` automatically; avoid hard‑coding paths.
