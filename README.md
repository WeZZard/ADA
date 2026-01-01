# ADA - AI Agent Debugging Architecture

A high-performance tracing system designed for debugging AI agents with minimal overhead and token-budget-aware analysis.

## Quick Start

See [Getting Started Guide](docs/GETTING_STARTED.md) for setup instructions.

## Project Overview

ADA provides a dual-lane flight recorder architecture for tracing AI agent execution:
- **Index Lane**: Always-on lightweight event capture
- **Detail Lane**: Selective persistence of rich debugging data
- **Token-Budget-Aware**: Optimized for LLM context window constraints

## Core Components

- **tracer**: Rust control plane for trace management
- **tracer_backend**: High-performance C/C++ data plane
- **query_engine**: Python-based token-budget-aware analysis
- **mcp_server**: Model Context Protocol interface

## Development Workflow

### Engineering Efficiency Documentation

For human developers, comprehensive guides are available:

#### Standards & Requirements

- [Engineering Standards](docs/sops/ENGINEERING_STANDARDS.md)
- [Getting Started](docs/GETTING_STARTED.md)

### For AI Agents

When using Claude Code or other AI assistants, agents will be spawned automatically based on the task. See [CLAUDE.md](CLAUDE.md) for AI-specific instructions.

## Build System

**Cargo orchestrates everything:**

```bash
cargo build --release    # Build all components
cargo test --all        # Run all tests
./utils/run_coverage.sh # Generate coverage reports
```

## Quality Requirements

- **Build Success**: 100% (all components must build)
- **Test Success**: 100% (all tests must pass)
- **Coverage**: 100% on changed lines
- **Integration Score**: 100/100

## Platform Requirements

### macOS
- Apple Developer Certificate ($99/year) required for tracing
- See [Platform Constraints](docs/definitions/constraints/) for details

### Linux
- May require ptrace capabilities
- See platform-specific setup in Getting Started guide

## Documentation Structure

```
docs/
├── definitions/             # MEI-supporting artifacts
│   ├── behaviors/          # BDD behaviors (BH-XXX)
│   ├── constraints/        # System constraints (CN-XXX)
│   ├── environments/       # Environment specs (EV-XXX)
│   ├── user_stories/       # User stories (US-XXX)
│   ├── personas/           # User personas (PS-XXX)
│   └── enablers/           # Technical enablers (EN-XXX)
├── design/                  # Design documentation
│   └── architecture/       # System architecture
├── business/               # Business analysis
├── proposals/              # Design proposals (PP-XXXX)
├── sops/                   # Standard operating procedures
└── progress_trackings/     # M/E/I tracking artifacts
```

## Contributing

This project follows a strict TDD workflow with mandatory quality gates. All contributions must:
1. Pass 100% of tests
2. Achieve 100% coverage on changed lines
3. Follow existing patterns and conventions
4. Include appropriate documentation

## License

[License information to be added]

## Support

For issues or questions, please refer to the documentation or create an issue in the repository.
## Quality Metrics

![Quality Gate](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/agentic-infra/ADA/main/.github/badges/integration-score.json)
![Rust Coverage](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/agentic-infra/ADA/main/.github/badges/rust-coverage.json)
![C/C++ Coverage](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/agentic-infra/ADA/main/.github/badges/cpp-coverage.json)
![Build Status](https://github.com/agentic-infra/ADA/workflows/Quality%20Gate%20CI/badge.svg)

