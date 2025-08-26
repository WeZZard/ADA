# Two-Lane Selective Persistence Architecture Explained

**Date**: 2025-08-15  
**Type**: Technical Explanation  
**Purpose**: Clarify index vs detail events in the tracer

## Core Concept: Two-Lane Ring Buffer

The ADA Tracer uses a **two-lane architecture** with selective persistence. Think of it like having two separate recording devices running simultaneously:

1. **Index Lane**: Lightweight, always-on recording with continuous persistence
2. **Detail Lane**: Heavy, always-on recording with windowed persistence

```
Timeline:  [=====================================>]
           
Index Lane: ●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●  (continuous capture & dump)
Detail Lane: ●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●  (continuous capture)
             ............██████████.............  (windowed persistence)
                         ↑        ↑
                      marked    marked
                      event     event
```

**Key Insight**: Both lanes are ALWAYS capturing data. The difference is when they persist (dump) to storage.

## What is an "Event"?

An **event** is anything that happens during program execution that we want to record:
- Function call (entering a function)
- Function return (exiting a function)
- Exception thrown
- Memory allocation
- System call

## Index Events vs Detail Events

### Index Event (24 bytes) - The "Black Box"
```c
struct IndexEvent {
    uint64_t timestamp;    // WHEN: Nanosecond timestamp
    uint64_t function_id;  // WHAT: Which function (address/ID)
    uint32_t thread_id;    // WHERE: Which thread
    uint32_t event_kind;   // TYPE: Call/Return/Exception
}
```

**Purpose**: Minimal recording to understand program flow
**Example**: "Function malloc() was called at time T on thread 1"

### Detail Event (512 bytes) - The "Crash Recorder"
```c
struct DetailEvent {
    uint64_t timestamp;         // WHEN: Same as index
    uint64_t function_id;       // WHAT: Same as index
    uint32_t thread_id;         // WHERE: Same as index
    uint32_t event_kind;        // TYPE: Same as index
    
    // ADDITIONAL DETAIL:
    uint64_t x_regs[8];         // ARM64 registers (function arguments)
    uint64_t lr;                // Link register (return address)
    uint64_t fp;                // Frame pointer
    uint64_t sp;                // Stack pointer
    uint8_t stack_snapshot[128]; // Actual stack memory contents
    uint32_t stack_size;
    uint8_t _padding[272];      // (wasted space - to be optimized)
}
```

**Purpose**: Full context for debugging crashes
**Example**: "Function malloc() called with size=1024, from address 0x1234, stack contains [...]"

## Real-World Example

Let's trace this simple program:
```c
int main() {
    char* buffer = malloc(1024);  // Event 1
    strcpy(buffer, "Hello");      // Event 2
    printf("%s\n", buffer);        // Event 3
    free(buffer);                  // Event 4
    return 0;
}
```

### What Gets Recorded:

#### In Index Lane (ALWAYS):
```
Event 1: [timestamp: 1000, function: malloc,  thread: 1, type: CALL]
Event 2: [timestamp: 1100, function: malloc,  thread: 1, type: RETURN]
Event 3: [timestamp: 1200, function: strcpy,  thread: 1, type: CALL]
Event 4: [timestamp: 1300, function: strcpy,  thread: 1, type: RETURN]
Event 5: [timestamp: 1400, function: printf,  thread: 1, type: CALL]
Event 6: [timestamp: 1500, function: printf,  thread: 1, type: RETURN]
Event 7: [timestamp: 1600, function: free,    thread: 1, type: CALL]
Event 8: [timestamp: 1700, function: free,    thread: 1, type: RETURN]
```

#### In Detail Lane (ALWAYS CAPTURED, PERSISTED WHEN MARKED):
All events are captured with full detail, but only persisted when marked events occur:

```
Event 3 DETAILED: [
    timestamp: 1200,
    function: strcpy,
    thread: 1,
    type: CALL,
    arg0: 0x7fff5000 (destination buffer address),
    arg1: 0x1000dead (source string address),
    return_address: 0x10001234 (where strcpy will return to),
    stack_pointer: 0x7fff4ff0,
    stack_dump: [actual 128 bytes of stack memory],
    ...
]
```

This event would mark the detail buffer for persistence if `strcpy` is in the marking policy.

## Why Two Lanes?

### The Problem with Single Recording:
- **Full detail always**: 512 bytes × 1M events/sec = 512 MB/sec (too much!)
- **Minimal only**: Not enough info when crash happens

### The Two-Lane Solution:

| Aspect | Index Lane | Detail Lane |
|--------|------------|-------------|
| **Purpose** | See everything | Debug problems |
| **Size** | 24 bytes/event | 512 bytes/event |
| **Capture** | Always recording | Always recording |
| **Persistence** | Dump when full | Dump when full AND marked |
| **Buffer** | 8 MB → 1.5 GB | 64 MB → 1 GB |
| **Duration** | Minutes to hours | Seconds to minutes |
| **Use Case** | "What happened?" | "Why did it crash?" |

## Black Box Analogy

Like an aircraft black box:

1. **Flight Data Recorder** (Index Lane)
   - Records basic parameters continuously
   - Saves everything to persistent storage
   - Shows complete flight path

2. **Cockpit Voice Recorder** (Detail Lane)
   - Records full audio continuously in memory
   - Only saves to persistent storage when important events occur
   - Preserves critical context around incidents

## Marking Policy System

The marking policy determines when to persist the detail lane buffer:

```rust
// Example marking policies
MarkingPolicy::FunctionName("strcpy")     // Dangerous function
MarkingPolicy::ExceptionThrown            // Any exception
MarkingPolicy::MemoryPressure(90)         // Memory > 90%
MarkingPolicy::Manual                     // User pressed button
```

When a marked event occurs:
1. The current detail ring buffer is marked for persistence
2. When the buffer fills, it gets dumped to storage (with all events before and after the mark)
3. A new ring buffer continues capturing

## Memory Usage Comparison

### Scenario: 1 Million Events/Second

**Index-Only Mode:**
- Data rate: 1M × 24 bytes = 24 MB/sec
- 1 GB buffer holds: 43 seconds

**Detail-Only Mode:**
- Data rate: 1M × 512 bytes = 512 MB/sec
- 1 GB buffer holds: 2 seconds

**Mixed Mode (10% detail):**
- Index: 900K × 24 bytes = 21.6 MB/sec
- Detail: 100K × 512 bytes = 51.2 MB/sec
- Total: 72.8 MB/sec
- Duration: ~14 seconds

## Summary

- **Event**: Any traced program activity (function call, return, etc.)
- **Index Event**: Minimal 24-byte record, always captured and persisted
- **Detail Event**: Full 512-byte context, always captured, selectively persisted
- **Index Lane**: Continuous capture and persistence of lightweight events
- **Detail Lane**: Continuous capture with windowed persistence based on marking policy

The two-lane design allows us to:
1. Always know what happened (index lane persists everything)
2. Always have full context available (detail lane captures everything)
3. Selectively persist expensive detail data (only when marked events occur)
4. Use memory and storage efficiently
5. Maintain low overhead during normal operation

Think of it as having both a security camera (index) that saves everything at low quality, and a high-resolution camera (detail) that records everything but only saves footage when something important happens.