# Xcode Project Tracing Guide

## Overview

This guide covers tracing Swift and Objective-C applications built with Xcode.

## Prerequisites

1. Xcode Command Line Tools installed
2. Project builds successfully in Xcode
3. ADA tracer binary signed with debugger entitlements

## Building for Tracing

### Debug Configuration

Always use Debug configuration for tracing:

```bash
xcodebuild -project MyApp.xcodeproj \
    -scheme MyApp \
    -configuration Debug \
    build
```

### Build Settings for Best Results

Ensure these build settings in your Xcode project:

| Setting | Value | Purpose |
|---------|-------|---------|
| DEBUG_INFORMATION_FORMAT | dwarf-with-dsym | Generates dSYM bundle |
| GCC_GENERATE_DEBUGGING_SYMBOLS | YES | Includes debug symbols |
| STRIP_INSTALLED_PRODUCT | NO | Keeps symbols in binary |
| DEPLOYMENT_POSTPROCESSING | NO | Prevents stripping |

### Finding the Built Binary

```bash
# Get build settings
xcodebuild -project MyApp.xcodeproj -scheme MyApp -showBuildSettings

# Key variables:
# BUILT_PRODUCTS_DIR = /path/to/DerivedData/.../Build/Products/Debug
# FULL_PRODUCT_NAME = MyApp.app
# EXECUTABLE_PATH = MyApp.app/Contents/MacOS/MyApp
```

## Swift Symbol Demangling

Swift uses name mangling. ADA automatically demangles:

```bash
# Mangled Swift name
_$s5MyApp14ViewControllerC10viewDidLoadyyF

# Demangled by ada
ada symbols demangle "_$s5MyApp14ViewControllerC10viewDidLoadyyF"
# Output: MyApp.ViewController.viewDidLoad() -> ()
```

## dSYM Location

Xcode generates dSYM bundles in the build products directory:

```
DerivedData/
└── MyApp-xxxxx/
    └── Build/
        └── Products/
            └── Debug/
                ├── MyApp.app
                └── MyApp.app.dSYM/
```

## Tracing iOS Simulator Apps

For iOS Simulator apps, the process is similar but targets the Simulator SDK:

```bash
xcodebuild -project MyApp.xcodeproj \
    -scheme MyApp \
    -sdk iphonesimulator \
    -configuration Debug \
    build

# The binary is in the .app bundle
ada trace start ./Build/Products/Debug-iphonesimulator/MyApp.app/MyApp
```

## Common Issues

### "Code signature invalid"

Sign the app for local development:
```bash
codesign --force --sign - MyApp.app
```

### "Operation not permitted"

Enable Developer Mode on macOS:
```bash
sudo DevToolsSecurity -enable
```

### Swift symbols not resolving

Ensure `swift demangle` is in PATH:
```bash
which swift
xcrun --find swift-demangle
```
