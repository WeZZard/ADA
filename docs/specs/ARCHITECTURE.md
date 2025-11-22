# ADA System Architecture Specification

## Overview

ADA is a comprehensive tracing and analysis platform that captures program execution with minimal overhead using a two-lane architecture with selective persistence inspired by aircraft black boxes.

## System Architecture

### High-Level Components

```
┌─────────────────────────────────────────────────────────────┐
│                     User Interface Layer                    │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐    │
│  │   CLI Tool  │  │  MCP Server  │  │   Query Engine   │    │
│  └──────┬──────┘  └──────┬───────┘  └────────┬─────────┘    │
└─────────┼─────────────────┼──────────────────┼──────────────┘
          │                 │                  │
┌─────────▼─────────────────▼──────────────────▼──────────────┐
│                      Control Plane (Rust)                   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                     Tracer Core                      │   │
│  │  - Shared Memory Management                          │   │
│  │  - Event Collection & Aggregation                    │   │
│  │  - ATF File Writing                                  │   │
│  └──────────────────────────────────────────────────────┘   │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────┐
│                   Data Plane (C/C++)                         │
│  ┌──────────────────────────────────────────────────────┐    │
│  │              Tracer Backend (Native Agent)           │    │
│  │  - Process Spawning/Attachment (Frida Core)          │    │
│  │  - Function Hooking (Frida Gum)                      │    │
│  │  - Ring Buffer Management                            │    │
│  │  - Event Logging to Shared Memory                    │    │
│  └──────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

## Two-Lane Selective Persistence Architecture

### Design Principles

The system uses a dual-lane recording approach to balance comprehensive coverage with performance:

### Lane 1: Index Lane (Always-On)

**Purpose**: Continuous, lightweight recording of all function calls

**Event Structure** (24 bytes):
```c
struct IndexEvent {
    uint64_t timestamp;     // 8 bytes - nanoseconds since epoch
    uint32_t function_id;   // 4 bytes - hashed function identifier
    uint32_t thread_id;     // 4 bytes - thread identifier
    uint16_t event_type;    // 2 bytes - enter/exit/error
    uint16_t flags;         // 2 bytes - additional flags
    uint32_t sequence;      // 4 bytes - sequence number
};
```

**Characteristics**:
- Fixed-size events for predictable performance
- Ring buffer with overwrite on overflow
- Target: <1% overhead at 5M events/sec
- Provides complete execution timeline

### Lane 2: Detail Lane (Always Captured, Selectively Persisted)

**Purpose**: Deep context always captured to ring buffer, persisted when marked events occur

**Event Structure** (512 bytes):
```c
struct DetailEvent {
    IndexEvent header;          // 24 bytes - same as index event
    uint64_t registers[16];     // 128 bytes - CPU registers
    uint8_t stack_snapshot[256]; // 256 bytes - stack around SP
    uint64_t return_address;    // 8 bytes
    uint64_t heap_pointers[8];  // 64 bytes - relevant heap refs
    uint32_t error_code;        // 4 bytes
    uint8_t padding[28];        // 28 bytes - alignment/future use
};
```

**Trigger Conditions**:
- Process crashes or signals
- Specific function calls (user-defined)
- Performance thresholds exceeded
- Error return codes
- Manual triggers

**Recording Window**:
- Pre-roll: 1000 events before trigger
- Post-roll: 1000 events after trigger
- Separate ring buffer from index lane

## Component Details

### Tracer (Rust) - Control Plane

**Responsibilities**:
- Lifecycle management of tracing sessions
- Shared memory allocation and management
- Collection of events from shared memory
- Writing ATF (ADA Trace Format) files
- Configuration and policy enforcement

**Key Modules**:
```rust
mod shared_memory;  // Shared memory lifecycle
mod collector;      // Event collection from agents
mod writer;         // ATF file format writer
mod config;         // Configuration management
mod control;        // RPC interface to backend
```

### Tracer Backend (C/C++) - Data Plane

**Responsibilities**:
- Process spawning with injection
- Process attachment via ptrace/task_for_pid
- Native agent injection using Frida
- Function hooking and instrumentation
- Hot-path event logging

**Key Components**:
```cpp
class FridaController;      // Frida lifecycle management
class NativeAgent;          // Injected agent code
class RingBuffer;           // Lock-free ring buffer
class SharedMemory;         // Shared memory interface
class HookManager;          // Function hook installation
```

### Asynchronous Loader (Startup)

The `FridaController` performs agent script loading asynchronously using `frida_script_load` with `GCancellable` and a temporary GLib `GMainLoop`. A unified startup timeout is computed from the estimated hook installation workload plus tolerance; cancellation enforces this deadline. The controller gates resume on an explicit readiness signal from the agent to ensure hooks are fully installed before execution continues.

### Query Engine (Python)

**Responsibilities**:
- ATF file parsing and indexing
- Symbol resolution via DWARF
- Token-budget-aware narrative generation
- Time-window queries
- Statistical analysis

**Key Modules**:
```python
atf_parser.py       # ATF format parser
symbol_resolver.py  # DWARF/debug info resolution
narrative.py        # AI-friendly narrative generation
query_dsl.py        # Query language implementation
statistics.py       # Performance analysis
```

### MCP Server (Python)

**Responsibilities**:
- Model Context Protocol implementation
- Session management for AI agents
- Query translation and optimization
- Result formatting for LLMs

## Shared Memory Architecture

### Layout

```
┌─────────────────────────────────────┐
│         Control Block (4KB)         │
│  - Magic number                     │
│  - Version                          │
│  - Ring buffer offsets              │
│  - Statistics                       │
├─────────────────────────────────────┤
│     Index Lane Ring Buffer          │
│         (50MB default)              │
│  - Circular buffer                  │
│  - Lock-free write                  │
│  - Multiple reader support          │
├─────────────────────────────────────┤
│     Detail Lane Ring Buffer         │
│         (50MB default)              │
│  - Circular buffer                  │
│  - Triggered writes only            │
│  - Preserved on trigger             │
└─────────────────────────────────────┘
```

### Addressing

- Offsets-only: all pointers in SHM are represented as offsets (and sizes) relative to the mapped arena base.
- Per-call materialization: each reader/writer computes real addresses per call as `addr = base + offset` using inline helpers; no persistent materialized-address caches are stored.
- Cross-process consistency: both agent and controller map the same arena and compute addresses locally; indices for additional SHMs are introduced in later iterations.

### Synchronization

- **Single Writer**: Each process has one agent (writer)
- **Multiple Readers**: Tracer can read while agent writes
- **Lock-Free**: Using atomic operations for indices
- **Memory Barriers**: Proper ordering for cross-CPU visibility

## ATF (ADA Trace Format)

### File Structure

```
[Header]
- Magic: "ADATRACE"
- Version: 1.0
- Metadata size
- Index offset
- Detail offset

[Metadata]
- Process info (PID, name, arguments)
- System info (OS, architecture)
- Symbol table
- String table

[Index Events]
- Sequential index events
- Compressed with zstd

[Detail Events]
- Triggered detail events
- Compressed with zstd

[Index]
- Time-based index for fast seeking
- Function-based index
- Thread-based index
```

## Performance Targets

### Overhead Limits
- **Index Lane**: <1% CPU overhead
- **Memory**: 100MB shared memory per process
- **Disk I/O**: Asynchronous, batched writes
- **Network**: No network I/O in hot path

### Throughput Targets
- **Event Rate**: 5M+ events/second sustained
- **Processes**: Support 10+ simultaneous traces
- **File Size**: ~1GB per hour of tracing

## Security Considerations

### Process Isolation
- Each traced process has separate shared memory
- No cross-process data leakage
- Secure cleanup on process termination

### Platform-Specific Security

**macOS**:
- Requires codesigning for injection
- Entitlements for task_for_pid
- SIP restrictions for system processes

**Linux**:
- CAP_SYS_PTRACE capability required
- Respects ptrace_scope settings
- SELinux/AppArmor compatibility

## Error Handling

### Graceful Degradation
1. If detail lane fails → continue with index lane only
2. If shared memory full → drop oldest events
3. If injection fails → report error, don't crash target
4. If symbol resolution fails → use addresses

### Recovery Mechanisms
- Automatic reconnection on agent crash
- Checkpoint/restore for long traces
- Partial trace recovery from shared memory

## Future Enhancements

### Planned Features
1. **Distributed Tracing**: Trace across process boundaries
2. **GPU Tracing**: CUDA/OpenCL kernel instrumentation
3. **Network Correlation**: TCP/UDP flow tracking
4. **Custom Probes**: User-defined trace points
5. **Real-time Streaming**: Live trace analysis

### Scalability Improvements
1. **Hierarchical Buffers**: Per-thread → per-core → global
2. **Compression**: Real-time event compression
3. **Sampling**: Adaptive sampling under load
4. **Cloud Storage**: Direct upload to S3/GCS

## Non-Goals

- **Not a Debugger**: No breakpoints or state modification
- **Not a Profiler**: No sampling-based profiling
- **Not a Logger**: No application-level logging
- **Not a Metrics System**: No long-term metrics storage
