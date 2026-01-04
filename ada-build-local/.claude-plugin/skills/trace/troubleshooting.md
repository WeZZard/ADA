# Troubleshooting Guide

## Build Issues

### "tracer binary not found"

**Solution**: Build the tracer and add to PATH:
```bash
cd /path/to/ADA
cargo build --release -p tracer
export PATH="$PATH:$(pwd)/target/release"
```

### "ada command not found"

**Solution**: Build ada-cli:
```bash
cargo build --release -p ada-cli
export PATH="$PATH:$(pwd)/target/release"
```

## Tracing Issues

### "Failed to attach to process"

**Causes**:
1. Insufficient permissions
2. System Integrity Protection (SIP)
3. Target process is protected

**Solutions**:

1. Enable Developer Mode:
   ```bash
   sudo DevToolsSecurity -enable
   ```

2. Sign tracer with entitlements:
   ```bash
   codesign -s - --entitlements utils/ada_entitlements.plist target/release/tracer
   ```

3. Check if process is Apple-signed:
   ```bash
   codesign -dv /path/to/binary
   # Apple-signed binaries may require SIP disabled
   ```

### "Frida SDK not found"

**Solution**: Initialize third-party dependencies:
```bash
./utils/init_third_parties.sh
```

### "No hooks installed"

**Causes**:
1. Binary is stripped
2. All symbols on exclude list
3. Wrong binary architecture

**Solutions**:

1. Check if binary has symbols:
   ```bash
   nm /path/to/binary | head
   ```

2. Check exclude list:
   ```bash
   cat ~/.ada/exclude_list.txt
   ```

3. Verify architecture:
   ```bash
   file /path/to/binary
   lipo -info /path/to/binary
   ```

## Symbol Resolution Issues

### "Symbol not found for function_id"

**Causes**:
1. Manifest doesn't contain symbols
2. Wrong session directory
3. Trace captured before hooks installed

**Solutions**:

1. Check manifest format version:
   ```bash
   cat session_*/manifest.json | grep format_version
   # Should be "2.1" for symbol support
   ```

2. Check if symbols are present:
   ```bash
   cat session_*/manifest.json | grep -c '"symbols"'
   ```

### "dSYM not found"

**Causes**:
1. Build didn't generate dSYM
2. Spotlight not indexing dSYM location
3. dSYM UUID doesn't match binary

**Solutions**:

1. Verify dSYM exists:
   ```bash
   ls -la *.dSYM
   mdfind "kMDItemContentType == com.apple.xcode.dsym"
   ```

2. Check UUID match:
   ```bash
   # Get binary UUID
   dwarfdump --uuid /path/to/binary

   # Get dSYM UUID
   dwarfdump --uuid /path/to/binary.dSYM
   ```

3. Rebuild dSYM index:
   ```bash
   mdimport /path/to/DerivedData
   ```

### "Demangling failed"

**Causes**:
1. swift-demangle not in PATH
2. Malformed symbol name

**Solutions**:

1. Check swift availability:
   ```bash
   xcrun --find swift-demangle
   ```

2. Try manual demangling:
   ```bash
   echo "_ZN3foo3barEv" | c++filt
   swift demangle "_\$s5MyApp4testyyF"
   ```

## Performance Issues

### "Trace files too large"

**Solutions**:

1. Use selective tracing with exclude list
2. Reduce trace duration
3. Enable only index events (disable detail)

### "High overhead during tracing"

**Solutions**:

1. Reduce hook count with exclude list
2. Disable stack capture:
   ```bash
   export ADA_CAPTURE_STACK=0
   ```

## Session Issues

### "Session directory empty"

**Causes**:
1. Tracer crashed
2. No events captured
3. Target exited immediately

**Solutions**:

1. Check tracer logs:
   ```bash
   export ADA_AGENT_VERBOSE=1
   ada trace start ./binary
   ```

2. Verify target ran:
   ```bash
   # Check manifest for thread info
   cat session_*/manifest.json
   ```

### "Cannot read manifest.json"

**Solutions**:

1. Check file permissions:
   ```bash
   ls -la session_*/manifest.json
   ```

2. Validate JSON:
   ```bash
   python3 -m json.tool session_*/manifest.json
   ```

## Platform-Specific Issues

### macOS: "Operation not permitted" on system binaries

Apple-signed system binaries cannot be traced without disabling SIP.
Trace your own application code instead.

### macOS: Notarization issues

For distributed binaries, ensure proper code signing:
```bash
codesign -s "Developer ID Application: Your Name" --options runtime ./tracer
```

## Getting Help

1. Enable verbose logging:
   ```bash
   export ADA_AGENT_VERBOSE=1
   export RUST_LOG=debug
   ```

2. Check system logs:
   ```bash
   log show --predicate 'process == "tracer"' --last 5m
   ```

3. Report issues: https://github.com/agentic-infra/ada/issues
