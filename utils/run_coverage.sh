#!/bin/bash
# Run full coverage collection and reporting

set -e

WORKSPACE_ROOT=$(git rev-parse --show-toplevel)
cd "$WORKSPACE_ROOT"

echo "=== ADA Coverage Collection ==="
echo

# Clean previous coverage data
echo "Cleaning previous coverage data..."
rm -rf target/coverage
rm -rf target/coverage_report
mkdir -p target/coverage

# Build with coverage
echo "Building with coverage instrumentation..."
export RUSTFLAGS="-C instrument-coverage"
export LLVM_PROFILE_FILE="$WORKSPACE_ROOT/target/coverage/unified-%p-%m.profraw"
cargo build --release --all --features tracer_backend/coverage,query_engine/coverage

# Run all tests with unified coverage (Rust + C++ via wrappers)
echo "Running all tests with coverage (Rust + C++)..."
# LLVM_PROFILE_FILE already exported above
cargo test --release --all --features tracer_backend/coverage,query_engine/coverage || true

# Collect coverage
echo
echo "Collecting coverage data..."
"$WORKSPACE_ROOT/target/release/coverage_helper" collect

# Generate HTML report
echo
echo "Generating HTML report..."
"$WORKSPACE_ROOT/target/release/coverage_helper" report --format html

# Run diff-cover
echo
echo "Checking coverage on changed lines..."
if which diff-cover > /dev/null 2>&1; then
    diff-cover "$WORKSPACE_ROOT/target/coverage_report/merged.lcov" \
               --compare-branch=main \
               --fail-under=100 || {
        echo "Warning: Some changed lines lack coverage"
    }
else
    echo "diff-cover not installed, skipping changed lines check"
fi

echo
echo "=== Coverage Collection Complete ==="
echo "Dashboard: file://$WORKSPACE_ROOT/target/coverage_report/index.html"
echo "Full Report: file://$WORKSPACE_ROOT/target/coverage_report/full/index.html"