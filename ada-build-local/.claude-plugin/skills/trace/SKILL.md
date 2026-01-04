---
name: trace
description: Build, trace, and analyze application performance. Use when profiling apps, finding slow functions, or debugging performance issues.
allowed-tools: Bash, Read, Glob, Grep, Write, AskUserQuestion
---

# Trace Skill

Orchestrates the build→trace→query workflow for performance analysis of macOS applications.

## Overview

This skill helps you:
1. Build applications with debug symbols
2. Trace function calls during execution
3. Resolve function IDs to human-readable symbol names
4. Analyze performance bottlenecks

## Prerequisites

- ADA CLI tools installed (`ada` command available)
- For Xcode projects: Xcode Command Line Tools
- macOS (required for Frida-based tracing)

## Workflow

### Step 1: Detect Project Type

First, determine what kind of project you're working with:

```bash
# Check for Xcode project
ls *.xcodeproj *.xcworkspace 2>/dev/null

# Check for CMake project
ls CMakeLists.txt 2>/dev/null

# Check for Cargo project
ls Cargo.toml 2>/dev/null

# Check for existing binary
file <binary_path>
```

### Step 2: Build with Debug Symbols

#### For Xcode Projects

```bash
# Build Debug configuration (includes dSYM)
xcodebuild -project MyApp.xcodeproj -scheme MyApp -configuration Debug build

# Find the built binary
xcodebuild -project MyApp.xcodeproj -scheme MyApp -showBuildSettings | grep -E "BUILT_PRODUCTS_DIR|EXECUTABLE_NAME"
```

#### For CMake Projects

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build
```

#### For Cargo Projects

```bash
cargo build
# Debug symbols are included by default in debug builds
```

### Step 3: Locate dSYM (macOS)

For symbol resolution with source locations, locate the dSYM bundle:

```bash
# Use ada CLI to find dSYM by UUID
ada symbols locate-dsym <UUID>

# Or manually check adjacent to binary
ls -la MyApp.app.dSYM/
ls -la MyApp.dSYM/
```

### Step 4: Start Tracing

```bash
# Trace a binary directly
ada trace start ./MyApp --output ./traces

# Trace an Xcode project
ada trace start-xcode MyApp.xcodeproj --scheme MyApp --output ./traces

# Attach to running process
ada trace attach <PID> --output ./traces
```

### Step 5: Interact with the Application

While tracing:
- Exercise the code paths you want to analyze
- Perform the actions that exhibit performance issues
- Press Ctrl+C when done to stop tracing

### Step 6: Analyze Results

```bash
# Show session information
ada symbols info ./traces/session_*

# Dump all captured symbols
ada symbols dump ./traces/session_*

# Resolve a specific function ID
ada symbols resolve ./traces/session_* 0x0000001c00000001

# Demangle a symbol name
ada symbols demangle "_ZN3foo3barEv"
```

## Example Workflows

### Profiling an Xcode App

```bash
# 1. Build the app
xcodebuild -project MyApp.xcodeproj -scheme MyApp -configuration Debug build

# 2. Start tracing
ada trace start-xcode MyApp.xcodeproj --scheme MyApp

# 3. (User interacts with app)
# 4. Press Ctrl+C to stop

# 5. Analyze results
ada symbols dump ./traces/session_* --format json
```

### Profiling a Command-Line Tool

```bash
# 1. Build with debug symbols
cargo build

# 2. Trace execution
ada trace start ./target/debug/my_tool --output ./traces -- arg1 arg2

# 3. Analyze results
ada symbols info ./traces/session_*
```

### Resolving Symbols from Trace Files

```bash
# List all sessions
ada trace list ./traces

# Get session info
ada symbols info ./traces/session_1234567890_my_tool

# Resolve specific function IDs found in trace events
ada symbols resolve ./traces/session_* 0x0000001c00000001
ada symbols resolve ./traces/session_* 0x0000001c00000002
```

## Troubleshooting

### "tracer not found"

Ensure the tracer is built and in PATH:
```bash
cargo build --release -p tracer
export PATH="$PATH:$(pwd)/target/release"
```

### "dSYM not found"

For Xcode projects, ensure Debug configuration is used:
```bash
xcodebuild -configuration Debug build
```

Check Spotlight indexing:
```bash
mdutil -s /
```

### "Permission denied" during tracing

macOS requires code signing for debugging:
```bash
# Sign the tracer binary
codesign -s - --entitlements utils/ada_entitlements.plist target/release/tracer
```

### Symbols showing as hex instead of names

Ensure the manifest.json has format_version "2.1" with modules/symbols arrays:
```bash
cat ./traces/session_*/manifest.json | jq '.format_version, .modules, .symbols'
```

## Output Format

The trace session produces:
- `manifest.json` - Session metadata with symbol table (v2.1)
- `thread_<N>_index.atf` - Function entry/exit timestamps
- `thread_<N>_detail.atf` - Detailed event data

### Manifest v2.1 Format

```json
{
  "format_version": "2.1",
  "threads": [{"id": 0, "has_detail": true}],
  "modules": [
    {
      "module_id": 123456789,
      "path": "/path/to/binary",
      "base_address": "0x100000000",
      "uuid": "550E8400-E29B-41D4-A716-446655440000"
    }
  ],
  "symbols": [
    {
      "function_id": "0x0000001c00000001",
      "module_id": 123456789,
      "name": "_main"
    }
  ]
}
```

## Integration with Query Engine

For advanced analysis, use the query engine:

```bash
# Coming soon: Natural language queries
ada query ./traces/session_* "show 10 slowest functions"
ada query ./traces/session_* "find functions called more than 1000 times"
```
