# M1_E2_I7_SELECTIVE_PERSISTENCE Technical Design

## Overview

Implements the selective persistence mechanism for the detail lane using "always capture, selectively persist" architecture. The detail lane continuously captures events to its ring buffer but only persists (dumps) when both conditions are met: ring is full AND a marked event has been seen since the last dump.

This enables true pre-roll capability by maintaining a continuous capture window while only persisting relevant segments containing marked events.

## Architecture

### Core Components

#### 1. MarkingPolicy Structure
```c
typedef struct {
    // Trigger configuration from CLI
    char** trigger_patterns;     // Array of trigger patterns
    size_t pattern_count;        // Number of patterns
    bool case_sensitive;         // Pattern matching mode
    bool regex_mode;            // Use regex vs literal matching
    
    // Runtime state
    atomic_bool enabled;         // Policy is active
    uint64_t last_check_time;    // Performance optimization
} MarkingPolicy;
```

#### 2. Detail Lane Control Extension
```c
typedef struct {
    // Existing detail lane fields...
    
    // Selective persistence state
    atomic_bool marked_event_seen_since_last_dump;
    uint64_t window_start_timestamp;    // Start of current capture window
    uint64_t window_end_timestamp;      // End marker for persistence
    uint64_t last_dump_timestamp;       // Last successful dump
    
    // Marking policy reference
    MarkingPolicy* marking_policy;
    
    // Metrics
    atomic_uint64_t marked_events_detected;
    atomic_uint64_t selective_dumps_performed;
    atomic_uint64_t windows_discarded;
} DetailLaneControl;
```

### Event Detection Flow

#### 1. Event Processing Pipeline
```
Incoming Event
    ↓
Event Capture (always)
    ↓
Ring Buffer Write
    ↓
Marked Event Check
    ↓
Update marked_event_seen flag (if matched)
    ↓
Ring Full Check
    ↓
Selective Dump Decision
    ↓
[Dump] or [Continue Capture]
```

#### 2. Marked Event Detection
```c
bool check_marked_event(const TraceEvent* event, const MarkingPolicy* policy) {
    if (!policy->enabled) return false;
    
    for (size_t i = 0; i < policy->pattern_count; i++) {
        if (policy->regex_mode) {
            if (regex_match(event->data, policy->trigger_patterns[i])) {
                return true;
            }
        } else {
            if (string_contains(event->data, policy->trigger_patterns[i], 
                              policy->case_sensitive)) {
                return true;
            }
        }
    }
    return false;
}
```

#### 3. Selective Dump Logic
```c
bool should_dump_detail_ring(DetailLaneControl* control) {
    // Check if ring is full (primary condition)
    if (!is_ring_buffer_full(control->ring)) {
        return false;
    }
    
    // Check if marked event seen since last dump
    bool marked_seen = atomic_load(&control->marked_event_seen_since_last_dump);
    if (!marked_seen) {
        // Ring is full but no marked event - discard oldest, continue
        atomic_fetch_add(&control->windows_discarded, 1);
        advance_ring_buffer_head(control->ring);
        return false;
    }
    
    return true; // Both conditions met - trigger dump
}
```

### Window Management

#### 1. Capture Window Boundaries
```c
typedef struct {
    uint64_t start_timestamp;    // Window start (first event after last dump)
    uint64_t end_timestamp;      // Window end (last event before dump)
    uint64_t marked_timestamp;   // First marked event in window
    size_t total_events;         // Events in window
    size_t marked_events;        // Marked events in window
} PersistenceWindow;
```

#### 2. Window Lifecycle Management
```c
void start_new_window(DetailLaneControl* control, uint64_t timestamp) {
    control->window_start_timestamp = timestamp;
    atomic_store(&control->marked_event_seen_since_last_dump, false);
}

void close_window_for_dump(DetailLaneControl* control, uint64_t timestamp) {
    control->window_end_timestamp = timestamp;
    control->last_dump_timestamp = timestamp;
}
```

### Integration Points

#### 1. CLI Configuration Integration (M1_E2_I4)
```c
// Initialize marking policy from parsed CLI config
MarkingPolicy* create_marking_policy_from_config(const CLIConfig* config) {
    MarkingPolicy* policy = malloc(sizeof(MarkingPolicy));
    
    // Copy trigger patterns from config
    policy->pattern_count = config->trigger_count;
    policy->trigger_patterns = malloc(policy->pattern_count * sizeof(char*));
    
    for (size_t i = 0; i < policy->pattern_count; i++) {
        policy->trigger_patterns[i] = strdup(config->triggers[i].pattern);
    }
    
    policy->case_sensitive = config->case_sensitive_triggers;
    policy->regex_mode = config->regex_triggers;
    atomic_store(&policy->enabled, true);
    
    return policy;
}
```

#### 2. Ring Pool Swap Integration (M1_E1_I6)
```c
// Enhanced swap protocol with selective persistence
SwapResult perform_selective_swap(DetailLaneControl* control) {
    if (!should_dump_detail_ring(control)) {
        return SWAP_SKIPPED; // Continue capture, no persistence
    }
    
    // Perform standard ring pool swap
    SwapResult result = swap_ring_pools(control->ring_pool);
    if (result == SWAP_SUCCESS) {
        // Update persistence state
        atomic_fetch_add(&control->selective_dumps_performed, 1);
        start_new_window(control, get_current_timestamp());
    }
    
    return result;
}
```

#### 3. ATF Writer Integration (M1_E2_I3)
```c
// Write persistence window metadata
void write_window_metadata(ATFWriter* writer, const PersistenceWindow* window) {
    ATFMetadata metadata = {
        .type = ATF_WINDOW_METADATA,
        .window_start = window->start_timestamp,
        .window_end = window->end_timestamp,
        .marked_timestamp = window->marked_timestamp,
        .event_count = window->total_events,
        .marked_count = window->marked_events
    };
    
    atf_write_metadata(writer, &metadata);
}
```

## Performance Considerations

### 1. Event Processing Overhead
- Marked event checking only on detail lane events
- Pattern matching optimized with early termination
- Regex compilation cached per policy
- Lock-free atomic operations for state updates

### 2. Memory Management
- Ring buffer reuse prevents allocation overhead
- Pattern storage shared across threads
- Window metadata lightweight (64 bytes)

### 3. I/O Optimization
- Batch writes when dumping full rings
- Metadata written once per window
- Async dump operations to prevent blocking capture

## Error Handling

### 1. Pattern Matching Failures
```c
// Graceful degradation for invalid patterns
bool safe_pattern_match(const char* data, const char* pattern, bool regex_mode) {
    if (!data || !pattern) return false;
    
    if (regex_mode) {
        regex_t compiled;
        if (regcomp(&compiled, pattern, REG_EXTENDED) != 0) {
            // Invalid regex - fall back to literal match
            return strstr(data, pattern) != NULL;
        }
        bool match = regexec(&compiled, data, 0, NULL, 0) == 0;
        regfree(&compiled);
        return match;
    }
    
    return strstr(data, pattern) != NULL;
}
```

### 2. State Consistency
- Atomic operations prevent race conditions
- Window boundaries validated before persistence
- Recovery mechanisms for corrupted state

### 3. Resource Exhaustion
- Pattern count limits (max 64 patterns)
- Window size bounds checking
- Memory allocation failure handling

## Testing Strategy

### 1. Unit Tests
- Pattern matching accuracy
- Selective dump decision logic
- Window boundary management
- State consistency under concurrency

### 2. Integration Tests
- CLI config to marking policy conversion
- Ring pool swap coordination
- ATF writer metadata generation

### 3. Performance Tests
- Event processing throughput with marking
- Memory usage under continuous capture
- Dump latency measurements

## Metrics and Observability

### 1. Core Metrics
```c
typedef struct {
    uint64_t events_processed;           // Total events through detail lane
    uint64_t marked_events_detected;     // Events matching triggers
    uint64_t selective_dumps_performed;  // Successful dumps with marked events
    uint64_t windows_discarded;          // Full rings without marked events
    uint64_t avg_window_duration_ns;     // Average time between dumps
    uint64_t avg_events_per_window;      // Average events per persistence window
} SelectivePersistenceMetrics;
```

### 2. Diagnostic Information
- Last marked event timestamp and pattern
- Current window start time and event count
- Ring utilization when dumps occur
- Pattern matching performance statistics

This design enables efficient pre-roll capture while minimizing storage overhead by only persisting relevant trace segments containing marked events.