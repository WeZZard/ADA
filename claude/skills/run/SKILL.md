---
name: run
description: "Launch application with ADA tracing - captures execution traces, voice narration, and screen recording"
---

# Run Application with ADA Capture

## Purpose

Launch an application with ADA tracing enabled, capturing execution traces, voice narration, and screen recording for later analysis. Screen and voice recording are enabled by default and can be disabled with `--no-screen` and `--no-voice` flags.

## Workflow

### Step 1: Project Detection

Check for project files in current directory:
- `*.xcodeproj` or `*.xcworkspace` → Xcode project
- `Cargo.toml` → Rust/Cargo project
- `Package.swift` → Swift Package Manager
- User-specified binary path → Generic binary

### Step 2: Build (if applicable)

```bash
# For Cargo projects:
cargo build --release

# For Xcode projects:
xcodebuild -scheme <scheme> -configuration Release build

# For Swift Package:
swift build -c release
```

### Step 3: Start Capture

```bash
# Full capture (trace + screen + voice) - default
ada capture start <binary_path>

# Trace + screen only (no voice)
ada capture start <binary_path> --no-voice

# Trace + voice only (no screen)
ada capture start <binary_path> --no-screen

# Trace only (no screen or voice)
ada capture start <binary_path> --no-screen --no-voice
```

### Step 4: Provide Feedback

Report to user:

- Session directory path
- Capture status
- How to stop: `ada capture stop`
- How to analyze: Use `ada:analyze` skill

## Error Handling

- **Build failure**: Show build errors, suggest fixes
- **Binary not found**: Guide user to specify path manually
- **Permission denied**: Check code signing (macOS)
- **ADA not installed**: Show installation instructions
