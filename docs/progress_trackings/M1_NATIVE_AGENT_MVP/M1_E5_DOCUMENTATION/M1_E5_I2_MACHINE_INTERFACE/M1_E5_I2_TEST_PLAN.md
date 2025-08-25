# Test Plan — M1 E5 I2 Machine Interface

## Objective
Validate CLI specification is precise, machine-consumable, and enables automation.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| CLI Grammar | ✓ | ✓ | - |
| JSON Output | ✓ | ✓ | - |
| Exit Codes | ✓ | ✓ | - |
| Error Format | ✓ | ✓ | - |

## Test Execution Sequence
1. Parse Spec → 2. Generate Tests → 3. Validate Behavior

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| cli__help_format__then_parseable | --help | Structured output | Machine readable |
| cli__json_mode__then_valid | --format json | Valid JSON | Schema compliant |
| cli__exit_codes__then_documented | Various errors | Specific codes | Matches spec |

## Test Categories

### 1. CLI Grammar Tests
- BNF/EBNF notation correct
- All flags documented
- Positional args specified
- Mutual exclusions clear

### 2. Output Format Tests  
- JSON schema provided
- Field types specified
- Null handling defined
- Version compatibility

### 3. Error Specification
- Error codes enumerated
- Error messages structured
- Recovery actions suggested
- Diagnostic info included

## Acceptance Criteria
- [ ] CLI grammar machine-parseable
- [ ] JSON output validates against schema
- [ ] Exit codes comprehensive
- [ ] Automation possible from spec
