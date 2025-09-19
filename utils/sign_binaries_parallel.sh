#!/bin/bash
# Parallel binary signing orchestrator for maximum performance
# Usage: Called from integration_quality.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$REPO_ROOT/utils"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Configuration
MAX_PARALLEL="${SIGN_PARALLEL:-4}"  # Number of parallel signing jobs
SIGN_SCRIPT="$SCRIPT_DIR/sign_binary_fast.sh"

# Make scripts executable
chmod +x "$SIGN_SCRIPT" 2>/dev/null || true

sign_binaries_parallel() {
    local test_dirs="$1"

    echo -e "${BLUE}[INFO]${NC} Collecting binaries to sign..."

    # Collect all test binaries
    local binaries=()
    while IFS= read -r binary; do
        binaries+=("$binary")
    done < <(find $test_dirs -name "test_*" -type f -perm +111 2>/dev/null)

    local total=${#binaries[@]}

    if [[ $total -eq 0 ]]; then
        echo -e "${YELLOW}[INFO]${NC} No test binaries found to sign"
        return 0
    fi

    echo -e "${BLUE}[INFO]${NC} Found $total test binaries to sign"
    echo -e "${BLUE}[INFO]${NC} Using $MAX_PARALLEL parallel workers"

    # Track progress
    local completed=0
    local failed=0
    local cached=0
    local signed=0
    local start_time=$(date +%s%N)

    # Create temp directory for job tracking
    local job_dir=$(mktemp -d)
    trap "rm -rf $job_dir" EXIT

    # Function to sign a single binary and track result
    sign_one() {
        local binary="$1"
        local index="$2"
        local result_file="$job_dir/result_$index"

        # Run signing and capture output
        local output
        if output=$("$SIGN_SCRIPT" "$binary" 2>&1); then
            echo "SUCCESS" > "$result_file"

            # Categorize result
            if [[ "$output" == *"CACHED:"* ]]; then
                echo "CACHED" >> "$result_file"
            else
                echo "SIGNED" >> "$result_file"
            fi

            # Short output for progress
            echo "$output" | head -1
        else
            echo "FAILED" > "$result_file"
            echo -e "${RED}✗${NC} $(basename "$binary"): $output" >&2
        fi
    }

    # Start parallel signing jobs
    echo -e "${CYAN}[SIGNING]${NC} Starting parallel signing..."

    for i in "${!binaries[@]}"; do
        binary="${binaries[$i]}"

        # Wait if we have too many jobs running
        while [[ $(jobs -r | wc -l) -ge $MAX_PARALLEL ]]; do
            sleep 0.01
        done

        # Start background job
        sign_one "$binary" "$i" &

        # Show progress every 10 binaries
        if [[ $(( (i + 1) % 10 )) -eq 0 ]] || [[ $((i + 1)) -eq $total ]]; then
            # Count completed jobs
            local done_count=$(ls -1 "$job_dir"/result_* 2>/dev/null | wc -l)
            printf "\r${CYAN}[PROGRESS]${NC} %d/%d (%.0f%%) " \
                "$done_count" "$total" \
                "$((done_count * 100 / total))" >&2
        fi
    done

    # Wait for all jobs to complete
    echo -e "\n${CYAN}[SIGNING]${NC} Waiting for jobs to complete..."
    wait

    # Count results
    for i in "${!binaries[@]}"; do
        if [[ -f "$job_dir/result_$i" ]]; then
            result=$(head -1 "$job_dir/result_$i")
            case "$result" in
                SUCCESS)
                    ((completed++))
                    if grep -q "CACHED" "$job_dir/result_$i"; then
                        ((cached++))
                    else
                        ((signed++))
                    fi
                    ;;
                FAILED)
                    ((failed++))
                    ;;
            esac
        fi
    done

    # Calculate timing
    local end_time=$(date +%s%N)
    local duration_ns=$((end_time - start_time))
    local duration_ms=$((duration_ns / 1000000))
    local duration_s=$((duration_ms / 1000))
    local duration_display=""

    if [[ $duration_s -gt 0 ]]; then
        duration_display="${duration_s}.$(printf "%03d" $((duration_ms % 1000)))s"
    else
        duration_display="${duration_ms}ms"
    fi

    # Report results
    echo -e "\n${CYAN}=== SIGNING SUMMARY ===${NC}"
    echo -e "  Total binaries:  $total"
    echo -e "  Cached (skip):   ${GREEN}$cached${NC}"
    echo -e "  Newly signed:    ${GREEN}$signed${NC}"
    if [[ $failed -gt 0 ]]; then
        echo -e "  Failed:          ${RED}$failed${NC}"
    fi
    echo -e "  Time taken:      $duration_display"
    echo -e "  Parallel workers: $MAX_PARALLEL"

    # Calculate speed improvement
    local sequential_estimate=$((total * 380))  # ~380ms per binary sequential
    local speedup=$((sequential_estimate / duration_ms))
    if [[ $speedup -gt 1 ]]; then
        echo -e "  Speed improvement: ~${speedup}x faster"
    fi

    echo -e "${CYAN}=====================${NC}\n"

    if [[ $failed -gt 0 ]]; then
        echo -e "${RED}[ERROR]${NC} Failed to sign $failed binaries"
        return 1
    else
        echo -e "${GREEN}[✓]${NC} All binaries signed successfully"
        return 0
    fi
}

# Main execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # Script is being run directly
    test_dirs="${1:-${REPO_ROOT}/target/release/tracer_backend/test}"
    sign_binaries_parallel "$test_dirs"
fi