#!/bin/bash
# Unified Integration Quality Gate with Agent-Friendly Output
# Single script that combines human and agent reporting
# Zero dependencies except standard Unix tools

set -uo pipefail

# ============================================================================
# CONFIGURATION
# ============================================================================

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="${1:-full}"  # full, incremental, score-only
AGENT_MODE="${AGENT_MODE:-true}"  # Enable agent reporting by default
VERBOSE="${VERBOSE:-false}"

# Paths
AGENT_REPORT_DIR="${REPO_ROOT}/target/agent_reports"
AGENT_REPORT_FILE="${AGENT_REPORT_DIR}/current_report.json"
COVERAGE_DIR="${REPO_ROOT}/target/coverage"
COVERAGE_REPORT_DIR="${REPO_ROOT}/target/coverage_report"

# Colors for human output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Exit codes
CHECK_RESULT_PASSED=0
CHECK_RESULT_BLOCKED=1
CHECK_RESULT_DOCUMENT_ONLY=2

# Score tracking
INTEGRATION_SCORE=100
CRITICAL_FAILURES=()

# Temporary files for capturing output
BUILD_OUTPUT=$(mktemp)
TEST_OUTPUT=$(mktemp)
COVERAGE_OUTPUT=$(mktemp)
trap "rm -f $BUILD_OUTPUT $TEST_OUTPUT $COVERAGE_OUTPUT" EXIT

# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[⚠]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

deduct_points() {
    local points=$1
    local reason=$2
    INTEGRATION_SCORE=$((INTEGRATION_SCORE - points))
    CRITICAL_FAILURES+=("$reason (-$points points)")
    log_error "$reason"
}

# ============================================================================
# AGENT REPORTING FUNCTIONS
# ============================================================================

init_agent_report() {
    if [[ "$AGENT_MODE" != "true" ]]; then
        return
    fi
    
    mkdir -p "$AGENT_REPORT_DIR"
    
    cat > "$AGENT_REPORT_FILE" << EOF
{
    "timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
    "mode": "$MODE",
    "status": "running",
    "failures": [],
    "suggestions": [],
    "next_actions": [],
    "metrics": {
        "integration_score": 100
    }
}
EOF
}

add_agent_failure() {
    if [[ "$AGENT_MODE" != "true" ]] || [[ ! -f "$AGENT_REPORT_FILE" ]]; then
        return
    fi
    
    local failure_type="$1"
    local failure_json="$2"
    
    # Add failure to report
    local tmp_file=$(mktemp)
    jq --argjson failure "$failure_json" \
       '.failures += [$failure]' \
       "$AGENT_REPORT_FILE" > "$tmp_file"
    mv "$tmp_file" "$AGENT_REPORT_FILE"
}

add_agent_action() {
    if [[ "$AGENT_MODE" != "true" ]] || [[ ! -f "$AGENT_REPORT_FILE" ]]; then
        return
    fi
    
    local action_json="$1"
    
    # Add action to report
    local tmp_file=$(mktemp)
    jq --argjson action "$action_json" \
       '.next_actions += [$action]' \
       "$AGENT_REPORT_FILE" > "$tmp_file"
    mv "$tmp_file" "$AGENT_REPORT_FILE"
}

finalize_agent_report() {
    if [[ "$AGENT_MODE" != "true" ]] || [[ ! -f "$AGENT_REPORT_FILE" ]]; then
        return
    fi
    
    local status="passed"
    if [[ $INTEGRATION_SCORE -lt 100 ]]; then
        status="failed"
    fi
    
    local tmp_file=$(mktemp)
    jq --arg status "$status" \
       --argjson score "$INTEGRATION_SCORE" \
       '.status = $status | .metrics.integration_score = $score' \
       "$AGENT_REPORT_FILE" > "$tmp_file"
    mv "$tmp_file" "$AGENT_REPORT_FILE"
}

# ============================================================================
# PLATFORM DETECTION
# ============================================================================

is_ci() {
    [ -n "${CI:-}" ] || \
    [ -n "${GITHUB_ACTIONS:-}" ] || \
    [ -n "${GITLAB_CI:-}" ]
}

is_ssh() {
    [ -n "${SSH_CONNECTION:-}" ] || \
    [ -n "${SSH_TTY:-}" ] || \
    [ -n "${SSH_CLIENT:-}" ]
}

# ============================================================================
# SOURCE CHANGE DETECTION
# ============================================================================

is_documentation_only() {
    local changed_files
    if git diff --cached --name-only &>/dev/null; then
        changed_files=$(git diff --cached --name-only)
    elif git diff HEAD~1 --name-only &>/dev/null; then
        changed_files=$(git diff HEAD~1 --name-only)
    else
        return 1
    fi
    
    if [[ -z "$changed_files" ]]; then
        return 1
    fi
    
    # Check if ALL changed files are documentation
    local non_doc_files=$(echo "$changed_files" | \
        grep -v -E '\.(md|txt|rst|adoc|org)$' | \
        grep -v -E '^(docs/|documentation/|README|LICENSE|CHANGELOG|AUTHORS|CONTRIBUTORS|\.gitignore|\.gitattributes)' || true)
    
    [[ -z "$non_doc_files" ]]
}

has_source_changes() {
    local changed_files
    if git diff --cached --name-only &>/dev/null; then
        changed_files=$(git diff --cached --name-only)
    elif git diff HEAD~1 --name-only &>/dev/null; then
        changed_files=$(git diff HEAD~1 --name-only)
    else
        return 1
    fi
    
    # Filter for actual source code files (not tests, not config, not docs)
    local code_files=$(echo "$changed_files" | grep -E '\.(rs|c|cpp|cc|cxx|h|hpp|hxx|py)$' || true)
    
    # Exclude test files and utility scripts
    local source_files=$(echo "$code_files" | \
        grep -v -E '(^tests?/|/tests?/|_test\.|\.test\.|test_.*\.rs$|.*_test\.py$|/fixtures?/|/bench/|/benches/|build\.rs$)' | \
        grep -v -E '(^utils/|^tools/|^scripts/|^\.github/)' | \
        grep -E '(^tracer/|^tracer_backend/|^query_engine/|^mcp_server/|^src/|^include/|^lib/)' || true)
    
    if [[ -z "$source_files" ]]; then
        return 1
    else
        echo "$source_files"
        return 0
    fi
}

# ============================================================================
# BUILD PHASE
# ============================================================================

build_all() {
    log_info "Building all components through Cargo..."
    
    local BUILD_FLAGS=""
    
    # Enable coverage instrumentation if not in score-only mode
    if [[ "$MODE" != "score-only" ]]; then
        BUILD_FLAGS="--features tracer_backend/coverage,query_engine/coverage"
        export RUSTFLAGS="-C instrument-coverage"
        export LLVM_PROFILE_FILE="${COVERAGE_DIR}/prof-%p-%m.profraw"
        mkdir -p "$COVERAGE_DIR"
    fi
    
    # Clean previous build artifacts
    cargo clean

    # Run build and capture output
    if ! cargo build --all --release $BUILD_FLAGS 2>&1 | tee "$BUILD_OUTPUT"; then
        deduct_points 100 "Build failed"
        
        # Parse build errors for agent
        if [[ "$AGENT_MODE" == "true" ]]; then
            parse_build_errors_for_agent
        fi
        
        return 1
    fi
    
    log_success "Build completed successfully"
    
    # Sign binaries on macOS
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sign_test_binaries
    fi
}

parse_build_errors_for_agent() {
    local build_output="$(cat "$BUILD_OUTPUT")"
    
    # Parse compilation errors
    while IFS= read -r line; do
        if [[ "$line" =~ ^([^:]+):([0-9]+):([0-9]+):[[:space:]]*error:[[:space:]]*(.+)$ ]]; then
            local file="${BASH_REMATCH[1]}"
            local line_num="${BASH_REMATCH[2]}"
            local col="${BASH_REMATCH[3]}"
            local message="${BASH_REMATCH[4]}"
            
            # Determine agent based on file extension
            local agent="cpp-developer"
            [[ "$file" == *.rs ]] && agent="rust-developer"
            [[ "$file" == *.py ]] && agent="python-developer"
            
            # Add failure record
            add_agent_failure "compilation" "{
                \"type\": \"compilation\",
                \"file\": \"$file\",
                \"line\": $line_num,
                \"column\": $col,
                \"message\": \"$message\"
            }"
            
            # Add suggested action
            add_agent_action "{
                \"agent\": \"$agent\",
                \"priority\": 1,
                \"action\": \"fix_compilation\",
                \"target\": {
                    \"file\": \"$file\",
                    \"line\": $line_num,
                    \"error\": \"$message\"
                },
                \"command\": \"Fix compilation error at $file:$line_num - $message\"
            }"
        fi
    done <<< "$build_output"
    
    # Parse linker errors
    if echo "$build_output" | grep -q "undefined reference\|symbol not found"; then
        add_agent_failure "linking" "{
            \"type\": \"linking\",
            \"message\": \"Undefined symbols or references found\"
        }"
        
        add_agent_action "{
            \"agent\": \"cpp-developer\",
            \"priority\": 1,
            \"action\": \"fix_linking\",
            \"command\": \"Fix linking errors - check for missing implementations or incorrect FFI exports\"
        }"
    fi
}

sign_test_binaries() {
    log_info "Signing test binaries (required on macOS)..."

    if (is_ci || is_ssh) && [ -z "${APPLE_DEVELOPER_ID:-}" ]; then
        log_error "APPLE_DEVELOPER_ID not set - required for macOS development"
        deduct_points 100 "No Apple Developer certificate"
        return 1
    fi

    local sign_count=0
    local sign_failures=0
    local failed_binaries=""

    # Create temp files for counting and error tracking (sh-compatible approach)
    local count_file=$(mktemp)
    local error_file=$(mktemp)
    echo "0 0" > "$count_file"

    # Use predictable paths from tracer_backend build.rs
    # This avoids signing duplicate binaries in CMake build directories
    local test_dirs=""

    # Check both release and debug predictable test directories
    # Build phase uses --release, but tests might be in debug too
    local found_predictable=false

    # Primary location: check release first (since build uses --release)
    if [ -d "${REPO_ROOT}/target/release/tracer_backend/test" ]; then
        test_dirs="${REPO_ROOT}/target/release/tracer_backend/test"
        log_info "Using optimized path: target/release/tracer_backend/test/"
        found_predictable=true
    # Also check debug directory
    elif [ -d "${REPO_ROOT}/target/debug/tracer_backend/test" ]; then
        test_dirs="${REPO_ROOT}/target/debug/tracer_backend/test"
        log_info "Using optimized path: target/debug/tracer_backend/test/"
        found_predictable=true
    fi

    # Fallback: search entire target directory (slower, may find duplicates)
    if [ "$found_predictable" = "false" ] || [ $(find $test_dirs -name "test_*" -type f -perm +111 2>/dev/null | wc -l | tr -d ' ') -eq 0 ]; then
        if [ "$found_predictable" = "true" ]; then
            log_info "Predictable test directory empty, falling back to full search"
        else
            log_info "Predictable test directory not found, falling back to full search"
        fi
        test_dirs="${REPO_ROOT}/target"
    fi

    # First count total binaries to sign
    local total_binaries=$(find $test_dirs -name "test_*" -type f -perm +111 2>/dev/null | wc -l | tr -d ' ')

    if [ "$total_binaries" -eq 0 ]; then
        log_info "No test binaries found to sign"
        rm -f "$count_file" "$error_file"
        return 0
    fi

    log_info "Found $total_binaries test binaries to sign..."

    find $test_dirs -name "test_*" -type f -perm +111 2>/dev/null | while IFS= read -r binary; do
        read sign_count sign_failures < "$count_file"
        sign_count=$((sign_count + 1))

        # Show progress every 10 binaries or at specific percentages
        if [ $((sign_count % 10)) -eq 0 ] || [ $((sign_count * 100 / total_binaries)) -gt $((((sign_count - 1) * 100 / total_binaries))) ]; then
            printf "\r${BLUE}[SIGNING]${NC} Progress: %d/%d (%.0f%%) - Failures: %d" \
                "$sign_count" "$total_binaries" \
                "$((sign_count * 100 / total_binaries))" \
                "$sign_failures" >&2
        fi

        # Capture signing output for debugging
        local sign_output=$("${REPO_ROOT}/utils/sign_binary.sh" "$binary" 2>&1)
        local sign_result=$?

        if [ $sign_result -ne 0 ]; then
            sign_failures=$((sign_failures + 1))
            # Store error details
            echo "FAILED: $(basename "$binary")" >> "$error_file"
            echo "  Path: $binary" >> "$error_file"
            echo "  Error: $sign_output" | head -5 >> "$error_file"
            echo "---" >> "$error_file"
        fi

        echo "$sign_count $sign_failures" > "$count_file"
    done

    # Clear progress line
    printf "\r%80s\r" " " >&2

    # Read final counts
    read sign_count sign_failures < "$count_file"

    if [ "$sign_failures" -gt 0 ]; then
        log_error "Failed to sign $sign_failures out of $sign_count binaries"

        # Show detailed error report
        if [ -f "$error_file" ] && [ -s "$error_file" ]; then
            echo "" >&2
            echo "${RED}=== Signing Failure Report ===${NC}" >&2
            cat "$error_file" >&2
            echo "${RED}==============================${NC}" >&2

            # Provide helpful suggestions based on common errors
            if grep -q "Developer ID" "$error_file" 2>/dev/null; then
                echo "" >&2
                echo "${YELLOW}Suggestion: Set APPLE_DEVELOPER_ID environment variable${NC}" >&2
                echo "  export APPLE_DEVELOPER_ID='Developer ID Application: Your Name'" >&2
            fi
            if grep -q "no identity found" "$error_file" 2>/dev/null; then
                echo "" >&2
                echo "${YELLOW}Suggestion: Check available certificates with:${NC}" >&2
                echo "  security find-identity -v -p codesigning" >&2
            fi
        fi

        rm -f "$count_file" "$error_file"
        deduct_points 100 "Binary signing failed"
        return 1
    fi

    rm -f "$count_file" "$error_file"
    log_success "Successfully signed all $sign_count test binaries"
    return 0
}

# ============================================================================
# TEST PHASE
# ============================================================================

run_tests() {
    log_info "Running all tests through Cargo..."
    
    local TEST_FLAGS=""
    if [[ "$MODE" != "score-only" ]]; then
        TEST_FLAGS="--features tracer_backend/coverage,query_engine/coverage"
    fi
    
    # Run tests and capture output
    if ! cargo test --all $TEST_FLAGS 2>&1 | tee "$TEST_OUTPUT"; then
        deduct_points 100 "Tests failed"
        
        # Parse test failures for agent
        if [[ "$AGENT_MODE" == "true" ]]; then
            parse_test_failures_for_agent
        fi
        
        return 1
    fi
    
    log_success "All tests passed"
}

parse_test_failures_for_agent() {
    local test_output="$(cat "$TEST_OUTPUT")"
    
    # Parse GoogleTest failures
    while IFS= read -r line; do
        if [[ "$line" =~ \[\ \ FAILED\ \ \]\ (.+) ]]; then
            local test_name="${BASH_REMATCH[1]}"
            
            # Find test file location if possible
            local test_location=$(echo "$test_output" | grep -B10 "$test_name" | grep -oE "[^/]+\.(cpp|cc|c):[0-9]+" | head -1)
            
            add_agent_failure "test" "{
                \"type\": \"test\",
                \"test_name\": \"$test_name\",
                \"location\": \"$test_location\"
            }"
            
            add_agent_action "{
                \"agent\": \"cpp-developer\",
                \"priority\": 1,
                \"action\": \"fix_test\",
                \"target\": {
                    \"test\": \"$test_name\"
                },
                \"command\": \"Fix failing test $test_name - analyze whether test or implementation needs fixing\"
            }"
        fi
    done <<< "$test_output"
    
    # Parse Rust test failures
    while IFS= read -r line; do
        if [[ "$line" =~ test\ (.+)\ \.\.\.\ FAILED ]]; then
            local test_name="${BASH_REMATCH[1]}"
            
            add_agent_failure "test" "{
                \"type\": \"test\",
                \"test_name\": \"$test_name\",
                \"language\": \"rust\"
            }"
            
            add_agent_action "{
                \"agent\": \"rust-developer\",
                \"priority\": 1,
                \"action\": \"fix_test\",
                \"target\": {
                    \"test\": \"$test_name\"
                },
                \"command\": \"Fix failing Rust test $test_name\"
            }"
        fi
    done <<< "$test_output"
}

# ============================================================================
# COVERAGE PHASE
# ============================================================================

collect_coverage() {
    if [[ "$MODE" == "score-only" ]]; then
        return 0
    fi
    
    log_info "Collecting coverage data..."
    
    # Run coverage helper
    if ! cargo run --manifest-path "${REPO_ROOT}/utils/coverage_helper/Cargo.toml" -- full; then
        log_warning "Coverage collection failed"
        return 1
    fi
    
    # Check incremental coverage for changed files
    if [[ "$MODE" == "incremental" ]]; then
        check_incremental_coverage
    fi
}

check_incremental_coverage() {
    log_info "Checking incremental coverage for changed lines..."
    
    local changed_files=$(has_source_changes || echo "")
    if [[ -z "$changed_files" ]]; then
        log_info "No source files changed"
        return 0
    fi
    
    log_info "Changed source files:"
    echo "$changed_files" | sed 's/^/  - /'
    
    local merged_lcov="${COVERAGE_REPORT_DIR}/merged.lcov"
    if [[ ! -f "$merged_lcov" ]]; then
        log_error "No coverage data found"
        deduct_points 100 "Coverage data missing"
        return 1
    fi
    
    local compare_branch="HEAD~1"
    if git rev-parse --verify origin/main &>/dev/null; then
        compare_branch="origin/main"
    fi
    
    # Run diff-cover and capture output
    if ! diff-cover "$merged_lcov" \
                  --fail-under=100 \
                  --compare-branch="$compare_branch" 2>&1 | tee "$COVERAGE_OUTPUT"; then
        
        log_error "Some changed lines lack coverage"
        deduct_points 100 "Changed lines must have 100% test coverage"
        
        # Parse coverage failures for agent
        if [[ "$AGENT_MODE" == "true" ]]; then
            parse_coverage_failures_for_agent
        fi
        
        return 1
    fi
    
    log_success "All changed lines have 100% coverage"
}

parse_coverage_failures_for_agent() {
    local coverage_output="$(cat "$COVERAGE_OUTPUT")"
    
    # Extract files with insufficient coverage
    while IFS= read -r line; do
        if [[ "$line" =~ ^([^[:space:]]+)[[:space:]]+\(([0-9]+)%\) ]]; then
            local file="${BASH_REMATCH[1]}"
            local coverage="${BASH_REMATCH[2]}"
            
            # Try to extract specific uncovered lines
            local uncovered_lines=$(echo "$coverage_output" | grep -A20 "$file" | grep "^!" | cut -d: -f1 | tr '\n' ',' | sed 's/,$//')
            
            add_agent_failure "coverage" "{
                \"type\": \"coverage\",
                \"file\": \"$file\",
                \"coverage_percentage\": $coverage,
                \"uncovered_lines\": \"$uncovered_lines\"
            }"
            
            # Determine test engineer based on file type
            local agent="cpp-test-engineer"
            [[ "$file" == *.rs ]] && agent="rust-test-engineer"
            [[ "$file" == *.py ]] && agent="python-test-engineer"
            
            add_agent_action "{
                \"agent\": \"$agent\",
                \"priority\": 1,
                \"action\": \"write_tests\",
                \"target\": {
                    \"file\": \"$file\",
                    \"lines\": \"$uncovered_lines\"
                },
                \"command\": \"Write tests to cover uncovered lines in $file. Focus on lines: $uncovered_lines\"
            }"
        fi
    done <<< "$coverage_output"
}

# ============================================================================
# REPORTING
# ============================================================================

generate_final_report() {
    # Finalize agent report
    finalize_agent_report
    
    # Human-readable report
    echo ""
    echo "========================================="
    echo "       INTEGRATION QUALITY REPORT        "
    echo "========================================="
    echo ""
    
    if [[ ${#CRITICAL_FAILURES[@]} -gt 0 ]]; then
        echo -e "${RED}CRITICAL FAILURES:${NC}"
        for failure in "${CRITICAL_FAILURES[@]}"; do
            echo "  • $failure"
        done
        echo ""
    fi
    
    local score_color=$GREEN
    if [[ $INTEGRATION_SCORE -lt 100 ]]; then
        score_color=$RED
    fi
    
    echo -e "Integration Score: ${score_color}${INTEGRATION_SCORE}/100${NC}"
    
    # Show agent report location if failures exist
    if [[ "$AGENT_MODE" == "true" ]] && [[ $INTEGRATION_SCORE -lt 100 ]] && [[ -f "$AGENT_REPORT_FILE" ]]; then
        echo ""
        echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${BLUE}  AGENT ANALYSIS AVAILABLE${NC}"
        echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        
        # Show recommended actions
        local action_count=$(jq '.next_actions | length' "$AGENT_REPORT_FILE")
        if [[ $action_count -gt 0 ]]; then
            echo "Recommended agent actions:"
            jq -r '.next_actions[] | "  • \(.agent): \(.command)"' "$AGENT_REPORT_FILE" | head -5
        fi
        
        echo ""
        echo "Full agent report: $AGENT_REPORT_FILE"
        echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    fi
    
    if [[ $INTEGRATION_SCORE -eq 100 ]]; then
        echo -e "${GREEN}✓ Quality Gate PASSED${NC}"
        return $CHECK_RESULT_PASSED
    else
        echo -e "${RED}✗ Quality Gate FAILED${NC}"
        return $CHECK_RESULT_BLOCKED
    fi
}

# ============================================================================
# MAIN EXECUTION
# ============================================================================

main() {
    # Initialize agent reporting
    init_agent_report
    
    log_info "Running quality gate in $MODE mode (Agent: $AGENT_MODE)"
    
    # Check for documentation-only changes
    if [[ "$MODE" == "incremental" ]] && is_documentation_only; then
        log_success "Documentation-only changes detected"
        echo -e "${GREEN}Skipping quality checks for documentation${NC}"
        return $CHECK_RESULT_DOCUMENT_ONLY
    fi
    
    # Check for source changes
    local has_sources=true
    if [[ "$MODE" == "incremental" ]]; then
        local source_changes=$(has_source_changes || echo "")
        if [[ -z "$source_changes" ]]; then
            has_sources=false
            log_info "No source code changes - reduced checks"
        fi
    fi
    
    # Run quality checks
    build_all || true
    run_tests || true
    
    # Only check coverage if source files changed
    if [[ "$has_sources" == "true" ]] && [[ "$MODE" != "score-only" ]]; then
        collect_coverage || true
    fi
    
    # Generate final report
    generate_final_report
    return $?
}

# Run main
main
EXIT_CODE=$?

# Clean exit
exit $EXIT_CODE