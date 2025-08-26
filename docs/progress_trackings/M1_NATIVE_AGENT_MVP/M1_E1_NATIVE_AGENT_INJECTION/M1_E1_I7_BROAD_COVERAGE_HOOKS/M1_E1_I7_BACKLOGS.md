# M1_E1_I7 Backlogs: Broad Coverage Hooks

## Implementation Tasks

### Priority 1: DSO Management & Exclude System [Day 1]

#### Task 1.1: Exclude List Implementation (3 hours)
**Description**: Implement high-performance exclude list with hash table
**Acceptance Criteria**:
- [ ] Create ExcludeList hash table structure
- [ ] Implement hash-based symbol lookup (<10ns)
- [ ] Load default exclude list (objc_msgSend, malloc, etc.)
- [ ] Add CLI --exclude flag parsing support
- [ ] Test exclude effectiveness and performance

**Implementation Checklist**:
```c
// exclude_list.h
#define EXCLUDE_HASH_SIZE 256
typedef struct ExcludeList {
    ExcludeEntry* buckets[EXCLUDE_HASH_SIZE];
    size_t count;
    _Atomic(uint64_t) check_count;
    _Atomic(uint64_t) exclude_count;
} ExcludeList;

bool is_symbol_excluded(const char* symbol_name);
int init_exclude_list(const char** cli_excludes, size_t count);
```

#### Task 1.2: DSO Interception Setup (3 hours)
**Description**: Implement dlopen/dlclose interceptors for DSO detection
**Acceptance Criteria**:
- [ ] Hook dlopen to detect new library loads
- [ ] Hook dlclose for cleanup tracking
- [ ] Implement DSO registry for loaded libraries
- [ ] Test DSO load/unload cycle handling

**Implementation Checklist**:
```c
// dso_management.h
typedef struct DSOEntry {
    char* path;
    void* handle;
    uint32_t hook_count;
    uint32_t export_count;
    uint64_t load_timestamp;
    struct DSOEntry* next;
} DSOEntry;

static void dlopen_hook_leave(GumInvocationContext* ctx);
static int install_dso_hooks(void* handle, const char* path);
```

#### Task 1.3: Hook Context Setup (2 hours)
**Description**: Implement TLS-based hook context and reentrancy guard
**Acceptance Criteria**:
- [ ] Define HookContext structure with atomics
- [ ] Implement enter_hook/exit_hook with proper memory ordering
- [ ] Add per-thread hook state management
- [ ] Test reentrancy protection

**Implementation Checklist**:
```c
// hook_context.h
typedef struct HookContext {
    _Atomic(uint32_t) in_hook;
    uint64_t saved_regs[8];
    uint8_t stack_snapshot[128];
    size_t stack_size;
    uint64_t hook_count;
    uint64_t skip_count;
    uint64_t swap_count;
} HookContext;

__thread HookContext g_hook_ctx = {0};
```

### Priority 2: Dynamic Hook Management [Day 1-2]

#### Task 2.1: Event Generation (3 hours)
**Description**: Implement IndexEvent structure and generation logic
**Acceptance Criteria**:
- [ ] Define IndexEvent with 24-byte layout
- [ ] Implement timestamp capture (TSC/monotonic)
- [ ] Add function ID mapping
- [ ] Test event field population

**Implementation Checklist**:
```c
// index_event.h
typedef struct IndexEvent {
    uint64_t timestamp;
    uint32_t thread_id;
    uint16_t event_type;
    uint16_t function_id;
    uint64_t context;
} IndexEvent;

void generate_entry_event(IndexEvent* event, uint16_t func_id);
```

#### Task 2.2: Dynamic Hook Registry (3 hours)
**Description**: Implement dynamic hook target management for DSO exports
**Acceptance Criteria**:
- [ ] Create HookRegistry for dynamic targets
- [ ] Implement auto-incrementing function IDs
- [ ] Add symbol-to-DSO mapping
- [ ] Thread-safe registry operations

**Implementation Checklist**:
```c
// hook_registry.c
typedef struct HookTarget {
    char* symbol_name;
    char* dso_path;
    uint16_t function_id;
    void* symbol_address;
    void* original_ptr;
    _Atomic(uint64_t) call_count;
    _Atomic(uint64_t) event_count;
    struct HookTarget* next;
} HookTarget;

static HookRegistry g_hook_registry = {0};
```

#### Task 2.3: Ring Buffer Emission (2 hours)
**Description**: Implement event emission to per-thread rings
**Acceptance Criteria**:
- [ ] Get thread slot from registry
- [ ] Write event to ring with SPSC semantics
- [ ] Handle ring-full condition
- [ ] Trigger swap when needed

**Implementation Checklist**:
```c
// ring_emit.c
static int emit_event(IndexEvent* event) {
    ThreadSlot* slot = thread_registry_get_current();
    RingBuffer* ring = atomic_load_explicit(&slot->active_ring, 
                                           memory_order_acquire);
    // SPSC write logic
    // Swap on full
    return 0;
}
```

### Priority 3: Comprehensive Hook Installation [Day 2]

#### Task 3.1: Export Enumeration with Filtering (4 hours)
**Description**: Enumerate DSO exports and filter through exclude list
**Acceptance Criteria**:
- [ ] Implement export enumeration callback
- [ ] Integrate exclude list filtering
- [ ] Skip non-function exports
- [ ] Track enumeration statistics

**Implementation Checklist**:
```c
// export_enum.c
static gboolean enumerate_export_callback(const GumExportDetails* details,
                                         gpointer user_data) {
    if (details->type != GUM_EXPORT_FUNCTION) return TRUE;
    if (is_symbol_excluded(details->name)) return TRUE;
    return install_single_hook(details) == 0;
}
```

#### Task 3.2: Comprehensive Installation (2 hours)
**Description**: Install hooks across main binary and all DSOs
**Acceptance Criteria**:
- [ ] Hook main binary exports first
- [ ] Hook all loaded DSO exports
- [ ] Set up DSO interceptors for future loads
- [ ] Report comprehensive metrics

#### Task 3.3: CLI Integration (2 hours)
**Description**: Support --exclude flag from command line
**Acceptance Criteria**:
- [ ] Parse --exclude flag arguments
- [ ] Merge with default exclude list
- [ ] Validate exclude patterns
- [ ] Report final exclude count

### Priority 4: Frida Integration [Day 2-3]

#### Task 4.1: Hook Wrapper Implementation (3 hours)
**Description**: Create Frida interceptor callbacks
**Acceptance Criteria**:
- [ ] Implement hook_enter callback
- [ ] Implement hook_exit callback
- [ ] Save/restore register context
- [ ] Platform-specific handling (x64/ARM64)

**Implementation Checklist**:
```c
// hook_wrapper.c
static void hook_enter(GumInvocationContext* ctx) {
    if (!enter_hook()) return;
    
    // Save registers
    #ifdef __aarch64__
        save_arm64_context(ctx);
    #else
        save_x64_context(ctx);
    #endif
    
    // Generate and emit event
    IndexEvent event = {...};
    emit_event(&event);
    
    exit_hook();
}
```

#### Task 4.2: Hook Installation (2 hours)
**Description**: Install hooks using Frida interceptor with exclude filtering
**Acceptance Criteria**:
- [ ] Install single hook with exclude check
- [ ] Handle dynamic target creation
- [ ] Report hook installation status
- [ ] Handle installation failures gracefully

**Implementation Checklist**:
```c
// hook_install.c
static int install_single_hook(const GumExportDetails* details) {
    if (is_symbol_excluded(details->name)) return -1;
    
    GumInterceptor* interceptor = gum_interceptor_obtain();
    return gum_interceptor_attach(interceptor, 
                                 GSIZE_TO_POINTER(details->address),
                                 hook_enter, hook_exit,
                                 create_hook_target(details));
}
```

#### Task 4.3: Stack Capture (1 hour)
**Description**: Optional stack snapshot capability
**Acceptance Criteria**:
- [ ] Capture up to 128 bytes of stack
- [ ] Boundary checking
- [ ] Performance optimization
- [ ] Configuration flag

**Implementation Checklist**:
```c
// stack_capture.c
static void capture_stack_snapshot(GumInvocationContext* ctx) {
    if (!g_capture_stack) return;
    
    void* sp = gum_invocation_context_get_stack_pointer(ctx);
    size_t size = MIN(128, available_stack_size(sp));
    memcpy(g_hook_ctx.stack_snapshot, sp, size);
    g_hook_ctx.stack_size = size;
}
```

### Priority 5: Testing Infrastructure [Day 3]

#### Task 5.1: Unit Test Suite (5 hours)
**Description**: Comprehensive unit tests for all components
**Acceptance Criteria**:
- [ ] DSO management tests
- [ ] Exclude system tests (including performance)
- [ ] Hook installation tests
- [ ] Reentrancy guard tests
- [ ] Event generation tests
- [ ] Ring emission tests

**Test Files**:
```
tests/
├── test_dso_management.cpp
├── test_exclude_system.cpp
├── test_hook_install.cpp
├── test_reentrancy.cpp
├── test_event_gen.cpp
└── test_ring_emit.cpp
```

#### Task 5.2: Integration Tests (4 hours)
**Description**: End-to-end hook flow testing with DSO coverage
**Acceptance Criteria**:
- [ ] Test main binary and DSO hooks
- [ ] Runtime DSO loading scenarios
- [ ] Exclude list effectiveness
- [ ] Multi-threaded scenarios
- [ ] High-frequency calls
- [ ] Comprehensive detach

**Test Implementation**:
```c
// test_integration.cpp
TEST(Integration, comprehensive_hooks_generate_events) {
    install_comprehensive_hooks(nullptr, 0);
    int result = fibonacci(10);  // Main binary
    double math = sin(3.14);     // DSO function
    EXPECT_GT(get_event_count("fibonacci"), 0);
    EXPECT_GT(get_event_count("sin"), 0);
    EXPECT_EQ(get_event_count("malloc"), 0);  // Excluded
    uninstall_comprehensive_hooks();
}
```

#### Task 5.3: Performance Benchmarks (3 hours)
**Description**: Measure hook overhead, exclude performance, and throughput
**Acceptance Criteria**:
- [ ] Hook overhead < 100ns
- [ ] Exclude check < 10ns
- [ ] DSO enumeration < 1ms per library
- [ ] Event generation > 10M/sec
- [ ] Memory ordering verification
- [ ] Stress test with 1000+ hooked functions

**Benchmark Implementation**:
```c
// bench_hooks.cpp
BENCHMARK(hook_overhead) {
    // Measure with/without hooks
    // Calculate per-call overhead
}

BENCHMARK(exclude_lookup_performance) {
    // Measure exclude hash table lookup
    // Target < 10ns per lookup
}

BENCHMARK(dso_enumeration_speed) {
    // Measure DSO export enumeration
    // Target < 1ms per DSO
}
```

### Priority 6: Error Handling & Polish [Day 3]

#### Task 6.1: Failure Recovery (2 hours)
**Description**: Graceful error handling for DSO and exclude scenarios
**Acceptance Criteria**:
- [ ] Handle DSO load failures
- [ ] Handle symbol not found
- [ ] Handle exclude list errors
- [ ] Handle attach failure
- [ ] Handle ring full
- [ ] Clean error reporting

#### Task 6.2: Resource Cleanup (2 hours)
**Description**: Proper resource management with DSO tracking
**Acceptance Criteria**:
- [ ] Detach all hooks from main binary and DSOs
- [ ] Clean up DSO registry
- [ ] Free exclude list memory
- [ ] Clear TLS state
- [ ] Release interceptors
- [ ] Memory leak free

#### Task 6.3: Monitoring & Reporting (2 hours)
**Description**: Comprehensive statistics and reporting
**Acceptance Criteria**:
- [ ] Track hook counts per DSO
- [ ] Track exclude hit counts
- [ ] Track skip counts
- [ ] Track swap counts
- [ ] Track DSO load events
- [ ] Report comprehensive metrics via on_message

## Testing Tasks

### Test Development [Day 3]
1. **Unit Tests** (4 hours)
   - Hook mechanics
   - Reentrancy logic
   - Event generation
   - Ring operations

2. **Integration Tests** (3 hours)
   - End-to-end flows
   - Multi-threading
   - Error paths
   - Cleanup

3. **Performance Tests** (2 hours)
   - Overhead measurement
   - Throughput testing
   - Stress scenarios

### Test Execution [Day 4]
1. **Test Running** (2 hours)
   - Run all unit tests
   - Run integration suite
   - Run benchmarks
   - Generate coverage

2. **Bug Fixing** (3 hours)
   - Fix test failures
   - Address coverage gaps
   - Performance tuning

3. **Documentation** (1 hour)
   - Update design docs
   - API documentation
   - Test results

## Time Estimates

### Development Breakdown
- **Day 1**: DSO management & exclude system (8 hours)
  - Exclude list implementation: 3 hours
  - DSO interception setup: 3 hours
  - Hook context setup: 2 hours

- **Day 2**: Dynamic hooks & comprehensive installation (8 hours)
  - Event generation: 3 hours
  - Dynamic hook registry: 3 hours
  - Comprehensive installation: 2 hours

- **Day 3**: Testing, integration & polish (8 hours)
  - Unit tests: 5 hours
  - Integration tests: 4 hours
  - Performance benchmarks: 3 hours
  - Error handling: 2 hours
  - (Note: Total > 8 hours, tasks will overlap)
  - Frida integration: 6 hours
  - Error handling & polish: 6 hours

- **Day 4**: Polish & validation (8 hours)
  - Bug fixes: 3 hours
  - Error handling: 2 hours
  - Documentation: 1 hour
  - Final validation: 2 hours

**Total: 24 hours (3 days)**

**Note**: This iteration maintains the 3-day target while expanding scope through:
- More focused task prioritization
- Parallel development of related components
- Streamlined testing approach
- Efficient reuse of existing patterns

## Definition of Done

### Code Complete
- [ ] DSO interception mechanisms implemented
- [ ] Exclude list with hash table working
- [ ] All hook functions implemented
- [ ] Dynamic hook registry functional
- [ ] Comprehensive installation working
- [ ] Reentrancy guard working
- [ ] Event generation complete
- [ ] Ring emission functional
- [ ] CLI --exclude flag support
- [ ] Error handling for DSO scenarios in place

### Testing Complete
- [ ] 100% unit test coverage (DSO, exclude, hooks)
- [ ] Integration tests passing (main + DSO coverage)
- [ ] Performance targets met (hook <100ns, exclude <10ns)
- [ ] DSO enumeration performance <1ms
- [ ] Exclude list effectiveness validated
- [ ] No memory leaks
- [ ] Thread sanitizer clean

### Documentation Complete
- [ ] Technical design updated
- [ ] API docs complete
- [ ] Test results documented
- [ ] Performance data recorded

### Integration Complete
- [ ] Works with ThreadRegistry
- [ ] Works with RingPool
- [ ] DSO registry integration functional
- [ ] Exclude list CLI integration working
- [ ] Comprehensive message reporting functional
- [ ] Clean shutdown with DSO cleanup supported

## Risk Register

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| Exclude list hash collisions | Low | Medium | Good hash function, collision chains |
| DSO enumeration too slow | Medium | High | Optimize enumeration, parallel processing |
| Hook overhead too high | Medium | High | Fast path optimization, inline functions |
| Exclude check overhead | Medium | High | O(1) hash lookup, cache-friendly design |
| DSO load/unload races | Medium | High | Proper synchronization, atomic operations |
| Memory leaks from dynamic targets | Medium | Medium | Proper cleanup, reference counting |
| CLI exclude parsing errors | Low | Medium | Input validation, error reporting |

## Dependencies

### External Dependencies
- Frida Gum interceptor API
- Frida module enumeration API
- dlopen/dlclose system calls
- ThreadRegistry (M1_E1_I5)
- RingPool (M1_E1_I6)
- Platform ABI (x64/ARM64)
- Command line argument parsing

### Internal Dependencies
- DSO registry structure
- Exclude list hash table
- Dynamic hook registry
- Hook context structure
- IndexEvent format
- Ring buffer interface
- Message protocol

## Next Iteration Preview

**M1_E1_I8: Advanced Hook Features**
- Argument capture for selected functions
- Return value recording
- Call graph tracking across DSOs
- Performance profiling hooks
- Custom exclude patterns (regex support)
- Hot-swappable exclude lists