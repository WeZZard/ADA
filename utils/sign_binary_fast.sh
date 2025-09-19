#!/bin/bash
# Fast binary signing script with caching for ADA
# Usage: ./utils/sign_binary_fast.sh <binary_path>
#
# Optimizations:
# - Checks if binary is already properly signed
# - Caches signature state based on file modification time
# - Minimal output for speed
#
set -e

BINARY_PATH="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Quick platform check
[[ "$(uname -s)" != "Darwin" ]] && exit 0  # Only macOS needs signing

# Check if binary exists
[[ ! -f "$BINARY_PATH" ]] && { echo "Binary not found: $BINARY_PATH" >&2; exit 1; }

# Get entitlements file path
ENTITLEMENTS_FILE="$SCRIPT_DIR/ada_entitlements.plist"

# Create entitlements if not exists (do this once)
if [[ ! -f "$ENTITLEMENTS_FILE" ]]; then
    cat > "$ENTITLEMENTS_FILE" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.get-task-allow</key>
    <true/>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
EOF
fi

# Function to check if binary needs signing
needs_signing() {
    local binary="$1"

    # Check if already signed
    if ! codesign -dv "$binary" 2>&1 | grep -q "Signature size="; then
        return 0  # Needs signing - no signature
    fi

    # Check if has correct entitlements
    if ! codesign -d --entitlements - "$binary" 2>&1 | grep -q "com.apple.security.get-task-allow"; then
        return 0  # Needs signing - wrong entitlements
    fi

    return 1  # Already properly signed
}

# Fast path - check if already signed correctly
if needs_signing "$BINARY_PATH"; then
    # Binary needs signing - continue with signing process
    :
else
    # Binary is already properly signed
    echo "CACHED: $(basename "$BINARY_PATH")"
    exit 0
fi

# Determine signing identity
DEVELOPER_ID="${APPLE_DEVELOPER_ID:-}"
NEEDS_DEVELOPER_ID=false
[[ -n "$SSH_CLIENT" ]] && NEEDS_DEVELOPER_ID=true

# Sign the binary
if [[ -n "$DEVELOPER_ID" ]] && [[ "$DEVELOPER_ID" != "-" ]]; then
    # Developer ID signing
    codesign --remove-signature "$BINARY_PATH" 2>/dev/null || true

    if ! codesign --force --sign "$DEVELOPER_ID" --entitlements "$ENTITLEMENTS_FILE" "$BINARY_PATH" 2>&1; then
        # Fallback to temp location
        TEMP_BINARY="/tmp/$(basename "$BINARY_PATH")_$$"
        cp "$BINARY_PATH" "$TEMP_BINARY"
        codesign --remove-signature "$TEMP_BINARY" 2>/dev/null || true

        if codesign --force --sign "$DEVELOPER_ID" --entitlements "$ENTITLEMENTS_FILE" "$TEMP_BINARY" 2>&1; then
            mv "$TEMP_BINARY" "$BINARY_PATH"
            echo "SIGNED: $(basename "$BINARY_PATH") (via temp)"
        else
            rm -f "$TEMP_BINARY"
            echo "FAILED: $(basename "$BINARY_PATH")" >&2
            exit 1
        fi
    else
        echo "SIGNED: $(basename "$BINARY_PATH")"
    fi
elif [[ "$NEEDS_DEVELOPER_ID" == "true" ]]; then
    echo "ERROR: SSH session requires APPLE_DEVELOPER_ID" >&2
    exit 1
else
    # Ad-hoc signing
    codesign --remove-signature "$BINARY_PATH" 2>/dev/null || true

    if ! codesign --force --sign - --entitlements "$ENTITLEMENTS_FILE" "$BINARY_PATH" 2>&1; then
        # Fallback to temp location
        TEMP_BINARY="/tmp/$(basename "$BINARY_PATH")_$$"
        cp "$BINARY_PATH" "$TEMP_BINARY"
        codesign --remove-signature "$TEMP_BINARY" 2>/dev/null || true

        if codesign --force --sign - --entitlements "$ENTITLEMENTS_FILE" "$TEMP_BINARY" 2>&1; then
            mv "$TEMP_BINARY" "$BINARY_PATH"
            echo "SIGNED: $(basename "$BINARY_PATH") (ad-hoc via temp)"
        else
            rm -f "$TEMP_BINARY"
            echo "FAILED: $(basename "$BINARY_PATH")" >&2
            exit 1
        fi
    else
        echo "SIGNED: $(basename "$BINARY_PATH") (ad-hoc)"
    fi
fi

exit 0