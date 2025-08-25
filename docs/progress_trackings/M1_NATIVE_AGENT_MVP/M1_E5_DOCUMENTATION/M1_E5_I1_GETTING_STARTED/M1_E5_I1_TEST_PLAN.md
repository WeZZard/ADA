# Test Plan — M1 E5 I1 Getting Started

## Objective
Ensure a new user can follow GETTING_STARTED documentation to successfully build, trace, and summarize.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Build Instructions | - | ✓ | - |
| Prerequisites | - | ✓ | - |
| First Trace | - | ✓ | - |
| Troubleshooting | - | ✓ | - |

## Test Execution Sequence
1. Fresh Environment → 2. Follow Docs → 3. Validate Results

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| docs__prerequisites__then_installable | Doc commands | All deps installed | No errors |
| docs__build_steps__then_compilable | Build instructions | Successful build | Binaries created |
| docs__first_trace__then_works | Example commands | Trace generated | Files present |

## Test Categories

### 1. Documentation Completeness
- All prerequisites listed
- Platform-specific sections (macOS/Linux)
- Apple Developer requirement prominent
- Code signing instructions clear

### 2. Build Process Validation
- Clone repository steps work
- `cargo build --release` succeeds
- Third-party init documented
- Output locations correct

### 3. First Run Experience
- Example commands run without error
- Output matches documentation
- Common errors addressed
- Success indicators clear

## Acceptance Criteria
- [ ] New user can build from scratch
- [ ] All commands in docs work
- [ ] Platform requirements clear
- [ ] Troubleshooting section helpful
