#!/bin/bash
# Distribute ADA skills and binaries
#
# Creates distribution packages with path substitution for either:
# - standalone: Hardcoded paths to ADA project location
# - plugin: Uses ${CLAUDE_PLUGIN_ROOT} variable for paths

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Parse arguments
FORM=""
OUTPUT_DIR="$PROJECT_ROOT/dist"
BUILD_PROFILE="release"
DO_BUILD=true  # Build by default (incremental, so cheap if nothing changed)

usage() {
    echo "Usage: $0 --form <plugin|standalone> [options]"
    echo ""
    echo "Create distribution packages for ADA skills."
    echo ""
    echo "Builds binaries by default (incremental - fast if nothing changed)."
    echo ""
    echo "Options:"
    echo "  --form <plugin|standalone>  Required. Distribution form:"
    echo "                                standalone - Hardcode paths to ADA project location"
    echo "                                plugin     - Use \${CLAUDE_PLUGIN_ROOT} variable for paths"
    echo "  --output <dir>              Output directory (default: ./dist)"
    echo "  --debug                     Use debug build instead of release"
    echo "  --no-build                  Skip build, use existing binaries"
    echo "  -h, --help                  Show this help message"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --form)
            FORM="$2"
            if [[ "$FORM" != "plugin" && "$FORM" != "standalone" ]]; then
                echo "Error: --form must be 'plugin' or 'standalone'"
                exit 1
            fi
            shift 2
            ;;
        --output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --debug)
            BUILD_PROFILE="debug"
            shift
            ;;
        --no-build)
            DO_BUILD=false
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

[[ -z "$FORM" ]] && { echo "Error: --form is required"; usage; }

# Set target directory based on profile
TARGET_DIR="$PROJECT_ROOT/target/$BUILD_PROFILE"

# Build if requested
if [[ "$DO_BUILD" == true ]]; then
    echo "Building ADA ($BUILD_PROFILE profile)..."

    if [[ "$BUILD_PROFILE" == "release" ]]; then
        cargo build --release -p ada-cli
    else
        cargo build -p ada-cli
    fi

    # Build ada-recorder (Swift)
    echo "Building ada-recorder..."
    pushd "$PROJECT_ROOT/ada-recorder/macos" > /dev/null
    if [[ "$BUILD_PROFILE" == "release" ]]; then
        swift build -c release
        cp .build/release/ada-recorder "$TARGET_DIR/"
    else
        swift build
        cp .build/debug/ada-recorder "$TARGET_DIR/"
    fi
    popd > /dev/null

    echo "Build complete."
fi

# Verify binaries exist
if [[ ! -f "$TARGET_DIR/ada" ]]; then
    echo "Error: ada binary not found at $TARGET_DIR/ada"
    echo "Run with --build flag or manually build first:"
    if [[ "$BUILD_PROFILE" == "release" ]]; then
        echo "  cargo build --release -p ada-cli"
    else
        echo "  cargo build -p ada-cli"
    fi
    exit 1
fi

if [[ ! -f "$TARGET_DIR/tracer_backend/lib/libfrida_agent.dylib" ]]; then
    echo "Error: libfrida_agent.dylib not found"
    echo ""
    echo "This may be because Frida SDK is not initialized. Try:"
    echo "  1. ./utils/init_third_parties.sh"
    echo "  2. cargo build --release"
    echo ""
    echo "Or if Frida is already initialized, just run:"
    if [[ "$BUILD_PROFILE" == "release" ]]; then
        echo "  cargo build --release"
    else
        echo "  cargo build"
    fi
    exit 1
fi

# Determine path prefix for substitution
if [[ "$FORM" == "standalone" ]]; then
    ADA_ROOT="$TARGET_DIR"
    ADA_LIB_DIR="$TARGET_DIR/tracer_backend/lib"
    ADA_BIN_DIR="$TARGET_DIR"
else
    ADA_ROOT="\${CLAUDE_PLUGIN_ROOT}"
    ADA_LIB_DIR="\${CLAUDE_PLUGIN_ROOT}/lib"
    ADA_BIN_DIR="\${CLAUDE_PLUGIN_ROOT}/bin"
fi

# Create output structure
rm -rf "$OUTPUT_DIR"

if [[ "$FORM" == "plugin" ]]; then
    mkdir -p "$OUTPUT_DIR"/{.claude-plugin,bin,lib}

    # Get version from workspace root Cargo.toml
    VERSION=$(grep -A1 '^\[workspace\.package\]' "$PROJECT_ROOT/Cargo.toml" | grep '^version' | sed 's/.*"\(.*\)".*/\1/')
    [[ -z "$VERSION" ]] && VERSION="0.1.0"

    # Generate plugin.json with marketplace-compatible fields
    cat > "$OUTPUT_DIR/.claude-plugin/plugin.json" << EOF
{
  "name": "walkthrough",
  "version": "$VERSION",
  "description": "Walk through the bug, let AI find the cause",
  "author": {
    "name": "WeZZard",
    "url": "https://github.com/WeZZard"
  },
  "repository": "https://github.com/WeZZard/ADA",
  "homepage": "https://github.com/WeZZard/ADA",
  "license": "MIT",
  "keywords": [
    "debugging",
    "tracing",
    "profiling",
    "frida",
    "macos"
  ]
}
EOF

    # Copy binaries
    cp "$TARGET_DIR/ada" "$OUTPUT_DIR/bin/"
    chmod +x "$OUTPUT_DIR/bin/ada"

    # Copy ada-recorder if it exists
    if [[ -f "$TARGET_DIR/ada-recorder" ]]; then
        cp "$TARGET_DIR/ada-recorder" "$OUTPUT_DIR/bin/"
        chmod +x "$OUTPUT_DIR/bin/ada-recorder"
    else
        echo "Warning: ada-recorder not found at $TARGET_DIR/ada-recorder"
    fi

    # Copy ada-capture-daemon if it exists
    if [[ -f "$TARGET_DIR/ada-capture-daemon" ]]; then
        cp "$TARGET_DIR/ada-capture-daemon" "$OUTPUT_DIR/bin/"
        chmod +x "$OUTPUT_DIR/bin/ada-capture-daemon"
    fi

    # Copy library
    cp "$TARGET_DIR/tracer_backend/lib/libfrida_agent.dylib" "$OUTPUT_DIR/lib/"

    # Sign binaries with developer certificate and entitlements for macOS tracing
    if [[ "$(uname -s)" == "Darwin" ]]; then
        ENTITLEMENTS_FILE="$SCRIPT_DIR/ada_entitlements.plist"
        SIGNING_TYPE=""

        # Find developer signing identity
        SIGNING_IDENTITY=$(security find-identity -v -p codesigning | grep "Developer ID Application" | head -1 | sed 's/.*"\(.*\)".*/\1/')

        if [[ -z "$SIGNING_IDENTITY" ]]; then
            echo -e "${YELLOW}Warning: No Developer ID certificate found${NC}"
            echo "Using ad-hoc signing (binaries will work locally but may trigger Gatekeeper warnings)"
            SIGNING_IDENTITY="-"
            SIGNING_TYPE="ad-hoc"
        else
            echo -e "${GREEN}Found Developer ID certificate: $SIGNING_IDENTITY${NC}"
            SIGNING_TYPE="developer-id"
        fi

        echo "Code signing binaries with debugging entitlements..."

        for binary in "$OUTPUT_DIR/bin/"*; do
            if [[ -f "$binary" && -x "$binary" ]]; then
                codesign --force --sign "$SIGNING_IDENTITY" --entitlements "$ENTITLEMENTS_FILE" --options runtime "$binary"
                echo "  Signed: $(basename "$binary")"
            fi
        done

        # Also sign the Frida agent library
        if [[ -f "$OUTPUT_DIR/lib/libfrida_agent.dylib" ]]; then
            codesign --force --sign "$SIGNING_IDENTITY" --options runtime "$OUTPUT_DIR/lib/libfrida_agent.dylib"
            echo "  Signed: libfrida_agent.dylib"
        fi

        # Verify signatures
        echo ""
        echo "Verifying signatures..."
        VERIFY_FAILED=false

        for binary in "$OUTPUT_DIR/bin/"*; do
            if [[ -f "$binary" && -x "$binary" ]]; then
                if codesign --verify --verbose "$binary" 2>/dev/null; then
                    # Check for debugging entitlement
                    if codesign -d --entitlements - --xml "$binary" 2>/dev/null | grep -q "get-task-allow"; then
                        echo -e "  ${GREEN}✓${NC} $(basename "$binary"): signed with debugging entitlements"
                    else
                        echo -e "  ${YELLOW}⚠${NC} $(basename "$binary"): signed but missing debugging entitlements"
                        VERIFY_FAILED=true
                    fi
                else
                    echo -e "  ${YELLOW}✗${NC} $(basename "$binary"): signature verification failed"
                    VERIFY_FAILED=true
                fi
            fi
        done

        if [[ -f "$OUTPUT_DIR/lib/libfrida_agent.dylib" ]]; then
            if codesign --verify --verbose "$OUTPUT_DIR/lib/libfrida_agent.dylib" 2>/dev/null; then
                echo -e "  ${GREEN}✓${NC} libfrida_agent.dylib: signed"
            else
                echo -e "  ${YELLOW}✗${NC} libfrida_agent.dylib: signature verification failed"
                VERIFY_FAILED=true
            fi
        fi

        if [[ "$VERIFY_FAILED" == true ]]; then
            echo ""
            echo -e "${YELLOW}Warning: Some signature verifications failed${NC}"
        fi

        echo ""
        echo -e "${BLUE}Signing Summary:${NC}"
        if [[ "$SIGNING_TYPE" == "developer-id" ]]; then
            echo -e "  Type: ${GREEN}Developer ID${NC}"
            echo "  Identity: $SIGNING_IDENTITY"
            echo "  Note: Signed with your certificate. Gatekeeper will recognize this signature."
        else
            echo -e "  Type: ${YELLOW}Ad-hoc${NC}"
            echo "  Note: No certificate. Users may need to right-click → Open on first run."
        fi
    fi

    # Copy and substitute all .md files in claude directory
    # For plugin form, files go to root level (skills/, commands/)
    # not nested under claude/
    find "$PROJECT_ROOT/claude" -name "*.md" -type f | while read -r src_file; do
        # Get relative path from claude directory
        rel_path="${src_file#$PROJECT_ROOT/claude/}"
        # Plugin form: files at root level (skills/X/SKILL.md, commands/X.md)
        dst_file="$OUTPUT_DIR/$rel_path"

        # Create target directory if needed
        mkdir -p "$(dirname "$dst_file")"

        # Apply substitution
        sed -e "s|\${ADA_ROOT}|$ADA_ROOT|g" \
            -e "s|\${ADA_LIB_DIR}|$ADA_LIB_DIR|g" \
            -e "s|\${ADA_BIN_DIR}|$ADA_BIN_DIR|g" \
            "$src_file" > "$dst_file"
    done

    echo ""
    echo "Distribution created at: $OUTPUT_DIR"
    echo "Form: plugin"
    echo "Profile: $BUILD_PROFILE"
    echo "ADA_ROOT: $ADA_ROOT"
    echo ""
    echo "Contents:"
    find "$OUTPUT_DIR" -type f | sort | sed 's|^|  |'

else
    # Standalone: all .md files with hardcoded paths
    mkdir -p "$OUTPUT_DIR"

    # Copy and substitute all .md files in claude directory
    find "$PROJECT_ROOT/claude" -name "*.md" -type f | while read -r src_file; do
        rel_path="${src_file#$PROJECT_ROOT/claude/}"
        dst_file="$OUTPUT_DIR/claude/$rel_path"

        mkdir -p "$(dirname "$dst_file")"

        sed -e "s|\${ADA_ROOT}|$ADA_ROOT|g" \
            -e "s|\${ADA_LIB_DIR}|$ADA_LIB_DIR|g" \
            -e "s|\${ADA_BIN_DIR}|$ADA_BIN_DIR|g" \
            "$src_file" > "$dst_file"
    done

    echo ""
    echo "Distribution created at: $OUTPUT_DIR"
    echo "Form: standalone"
    echo "Profile: $BUILD_PROFILE"
    echo "ADA_ROOT: $ADA_ROOT"
    echo ""
    echo "Contents:"
    find "$OUTPUT_DIR" -type f | sort | sed 's|^|  |'
fi
