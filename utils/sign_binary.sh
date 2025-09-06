#!/bin/bash
# Platform-aware binary signing script for ADA
# Usage: ./utils/sign_binary.sh <binary_path>
#
# This script handles platform-specific signing requirements:
# - macOS: Code signing with entitlements for Frida operations
# - Linux: Checks/sets ptrace capabilities  
# - Other: No-op with informational message

set -e

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <binary_path>"
    echo "Example: $0 target/debug/test_integration"
    exit 1
fi

BINARY_PATH="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLATFORM="$(uname -s)"

# Check if binary exists
if [[ ! -f "$BINARY_PATH" ]]; then
    echo -e "${RED}Error: Binary not found: $BINARY_PATH${NC}"
    exit 1
fi

case "$PLATFORM" in
    Darwin)
        # macOS code signing
        echo -e "${BLUE}Platform: macOS - Code signing required for tracing${NC}"
        
        ENTITLEMENTS_FILE="$SCRIPT_DIR/ada_entitlements.plist"
        
        # Create entitlements if not exists
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
            echo -e "${GREEN}Created entitlements file: $ENTITLEMENTS_FILE${NC}"
        fi
        
        # Check current signature
        CURRENT_SIG=$(codesign -dv "$BINARY_PATH" 2>&1 | grep "Signature=" | cut -d'=' -f2 || echo "none")
        echo "Current signature: $CURRENT_SIG"
        
        # Detect if we're in SSH session
        if [[ -n "$SSH_CLIENT" ]]; then
            echo -e "${YELLOW}⚠️  SSH session detected - Developer ID certificate required${NC}"
            NEEDS_DEVELOPER_ID=true
        else
            echo "Local session - ad-hoc signing sufficient"
            NEEDS_DEVELOPER_ID=false
        fi
        
        # Determine signing identity
        DEVELOPER_ID="${APPLE_DEVELOPER_ID:-}"
        
        if [[ -n "$DEVELOPER_ID" ]] && [[ "$DEVELOPER_ID" != "-" ]]; then
            # Use provided Developer ID
            echo -e "${GREEN}Using Developer ID: $DEVELOPER_ID${NC}"
            
            # Remove existing signature first to avoid conflicts
            codesign --remove-signature "$BINARY_PATH" 2>/dev/null || true
            
            # Try signing in place first
            if codesign --force \
                     --sign "$DEVELOPER_ID" \
                     --entitlements "$ENTITLEMENTS_FILE" \
                     "$BINARY_PATH" 2>&1; then
                echo -e "${GREEN}✅ Binary signed with Developer ID certificate${NC}"
            else
                # If in-place signing fails (common in build directories), try temp location
                echo -e "${YELLOW}In-place signing failed, trying temp location workaround${NC}"
                TEMP_BINARY="/tmp/$(basename "$BINARY_PATH")_$$"
                cp "$BINARY_PATH" "$TEMP_BINARY"
                
                # Remove any existing signature on the copy
                codesign --remove-signature "$TEMP_BINARY" 2>/dev/null || true
                
                if codesign --force \
                         --sign "$DEVELOPER_ID" \
                         --entitlements "$ENTITLEMENTS_FILE" \
                         "$TEMP_BINARY" 2>&1; then
                    # Move signed binary back
                    mv "$TEMP_BINARY" "$BINARY_PATH"
                    echo -e "${GREEN}✅ Binary signed via temp location workaround${NC}"
                else
                    exit 1
                fi
            fi
            
        elif [[ "$NEEDS_DEVELOPER_ID" == "true" ]]; then
            # SSH session but no Developer ID
            echo -e "${RED}ERROR: SSH session requires Developer ID certificate${NC}"
            echo -e "${YELLOW}Options:${NC}"
            echo "  1. Set APPLE_DEVELOPER_ID environment variable"
            echo "  2. Get certificate: https://developer.apple.com ($99/year)"
            echo "  3. Use local Terminal.app instead of SSH"
            echo ""
            echo "Example:"
            echo "  export APPLE_DEVELOPER_ID='Developer ID Application: Your Name'"
            echo "  $0 $BINARY_PATH"
            exit 1
            
        else
            # Local session, use ad-hoc signing with entitlements
            echo "Using ad-hoc signing for local testing"
            codesign --remove-signature "$BINARY_PATH" 2>/dev/null || true
            if codesign --force \
                     --sign - \
                     --entitlements "$ENTITLEMENTS_FILE" \
                     "$BINARY_PATH" 2>&1; then
                echo -e "${GREEN}✅ Binary ad-hoc signed with entitlements${NC}"
            else
                echo -e "${RED}Ad-hoc signing failed${NC}"
                exit 1
            fi
        fi
        
        # Verify signature
        echo ""
        echo "Signature verification:"
        codesign -dv "$BINARY_PATH" 2>&1 | grep -E "Signature|Authority|TeamIdentifier|Identifier" || true
        ;;
        
    Linux)
        # Linux ptrace capabilities
        echo -e "${BLUE}Platform: Linux - Checking ptrace capabilities${NC}"
        
        # Check ptrace_scope
        PTRACE_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo "unknown")
        echo "Current ptrace_scope: $PTRACE_SCOPE"
        
        case "$PTRACE_SCOPE" in
            0)
                echo -e "${GREEN}✅ Ptrace unrestricted - tracing should work${NC}"
                ;;
            1)
                echo -e "${YELLOW}⚠️  Ptrace restricted - may need sudo or capabilities${NC}"
                echo "Options:"
                echo "  1. Run with sudo: sudo $BINARY_PATH"
                echo "  2. Add capability: sudo setcap cap_sys_ptrace=eip $BINARY_PATH"
                echo "  3. Temporarily allow: echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope"
                
                # Offer to add capability
                read -p "Add CAP_SYS_PTRACE capability to binary? (requires sudo) [y/N]: " -n 1 -r
                echo
                if [[ $REPLY =~ ^[Yy]$ ]]; then
                    sudo setcap cap_sys_ptrace=eip "$BINARY_PATH"
                    echo -e "${GREEN}✅ CAP_SYS_PTRACE capability added${NC}"
                fi
                ;;
            2|3)
                echo -e "${RED}ERROR: Ptrace heavily restricted (admin only or disabled)${NC}"
                echo "Tracing requires root or system reconfiguration"
                exit 1
                ;;
            *)
                echo -e "${YELLOW}⚠️  Cannot determine ptrace restrictions${NC}"
                ;;
        esac
        
        # Check for AppArmor/SELinux
        if command -v aa-status >/dev/null 2>&1; then
            echo -e "${YELLOW}Note: AppArmor detected - may need profile exceptions${NC}"
        fi
        if command -v sestatus >/dev/null 2>&1; then
            echo -e "${YELLOW}Note: SELinux detected - may need policy exceptions${NC}"
        fi
        ;;
        
    *)
        # Other platforms
        echo -e "${BLUE}Platform: $PLATFORM - No special signing required${NC}"
        echo -e "${GREEN}✅ Binary ready for tracing${NC}"
        ;;
esac

echo ""
echo -e "${BLUE}Binary prepared: $BINARY_PATH${NC}"
echo "Next step: Run your tracing/testing commands"

exit 0
