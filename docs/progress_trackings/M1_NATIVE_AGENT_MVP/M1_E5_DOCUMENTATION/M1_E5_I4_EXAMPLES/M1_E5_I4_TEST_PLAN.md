# Test Plan — M1 E5 I4 Examples

## Objective
Validate all example code compiles, runs correctly, produces expected output, and accurately demonstrates ADA tracing capabilities.

## Test Coverage Map
| Component | Unit Tests | Integration Tests | Performance Tests |
|-----------|------------|-------------------|-------------------|
| Basic Examples | ✓ | ✓ | - |
| Multi-thread Examples | ✓ | ✓ | ✓ |
| Performance Examples | ✓ | ✓ | ✓ |
| Debug Examples | ✓ | ✓ | - |
| Signal Examples | ✓ | ✓ | - |
| Filter Examples | ✓ | ✓ | ✓ |
| Output Formats | ✓ | ✓ | - |
| CI Integration | - | ✓ | - |

## Test Execution Sequence
1. Example Compilation → 2. Output Validation → 3. Command Testing → 4. Documentation Accuracy

## Test Matrix
| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|-----------------|---------------|
| examples__hello_world__then_traces | hello_trace.c | Function calls traced | Trace contains main, greet, printf |
| examples__threads__then_shows_concurrency | thread_trace.c | 4 threads visible | Thread timeline shows parallelism |
| examples__performance__then_identifies_hotspot | perf_profile.c | bubble_sort dominates | >90% time in bubble_sort |
| examples__memory__then_detects_leaks | memory_debug.c | Leaks reported | All 3 leaks identified |
| examples__signals__then_captures_handlers | signal_trace.c | Signal flow visible | Handler execution traced |

## Test Categories

### 1. Example Compilation Tests

#### Test: `examples__all_compile__then_no_errors`
```c
void test_all_examples_compile() {
    const char* examples[] = {
        "hello_trace.c",
        "thread_trace.c",
        "perf_profile.c",
        "memory_debug.c",
        "signal_trace.c"
    };
    
    for (int i = 0; i < 5; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "gcc -o test_%d %s", i, examples[i]);
        
        int result = system(cmd);
        ASSERT_EQ(result, 0);  // Compilation succeeds
        
        // Check binary exists
        char binary[64];
        snprintf(binary, sizeof(binary), "test_%d", i);
        ASSERT_TRUE(file_exists(binary));
    }
}
```

#### Test: `examples__warnings_check__then_clean`
```c
void test_no_compiler_warnings() {
    const char* compile_cmd = "gcc -Wall -Wextra -Werror -o test hello_trace.c";
    
    int result = system(compile_cmd);
    ASSERT_EQ(result, 0);  // No warnings with strict flags
}
```

### 2. Basic Example Tests

#### Test: `hello_trace__execution__then_expected_output`
```c
void test_hello_trace_output() {
    // Run example
    system("./hello_trace > output.txt");
    
    // Verify output
    FILE* fp = fopen("output.txt", "r");
    char line[256];
    
    ASSERT_TRUE(fgets(line, sizeof(line), fp) != NULL);
    ASSERT_STREQ(line, "Hello, World!\n");
    
    ASSERT_TRUE(fgets(line, sizeof(line), fp) != NULL);
    ASSERT_STREQ(line, "Hello, ADA!\n");
    
    ASSERT_TRUE(fgets(line, sizeof(line), fp) != NULL);
    ASSERT_STREQ(line, "Hello, Tracer!\n");
    
    fclose(fp);
}
```

#### Test: `hello_trace__with_tracing__then_generates_atf`
```c
void test_hello_trace_with_ada() {
    // Run with ADA tracing
    system("ada trace --output test.atf ./hello_trace");
    
    // Verify trace file created
    ASSERT_TRUE(file_exists("test.atf"));
    
    // Verify trace contains expected functions
    TraceFile* trace = load_trace("test.atf");
    ASSERT_NOT_NULL(trace);
    
    ASSERT_TRUE(trace_contains_function(trace, "main"));
    ASSERT_TRUE(trace_contains_function(trace, "greet"));
    ASSERT_TRUE(trace_contains_function(trace, "printf"));
    ASSERT_TRUE(trace_contains_function(trace, "usleep"));
    
    // Verify timing
    ASSERT_GE(trace->duration_ms, 300.0);  // At least 3x100ms
    ASSERT_LE(trace->duration_ms, 350.0);  // Reasonable overhead
}
```

### 3. Multi-threading Example Tests

#### Test: `thread_trace__creates_threads__then_all_visible`
```c
void test_thread_trace_parallelism() {
    // Run with thread tracking
    system("ada trace --output threads.atf --thread-stats ./thread_trace");
    
    TraceFile* trace = load_trace("threads.atf");
    
    // Verify thread count
    ASSERT_EQ(trace->thread_count, 5);  // main + 4 workers
    
    // Verify each worker thread
    for (int i = 0; i < 4; i++) {
        Thread* thread = find_thread_by_index(trace, i);
        ASSERT_NOT_NULL(thread);
        
        // Each thread should have worker function
        ASSERT_TRUE(thread_executed_function(thread, "worker"));
        
        // Each thread should run 5 iterations
        ASSERT_EQ(count_function_calls(thread, "printf"), 5);
    }
}
```

#### Test: `thread_trace__timeline__then_shows_concurrency`
```c
void test_thread_timeline() {
    system("ada analyze threads.atf --thread-timeline > timeline.txt");
    
    FILE* fp = fopen("timeline.txt", "r");
    char buffer[4096];
    size_t bytes = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);
    
    // Verify timeline shows parallel execution
    ASSERT_TRUE(strstr(buffer, "Thread 0") != NULL);
    ASSERT_TRUE(strstr(buffer, "Thread 1") != NULL);
    ASSERT_TRUE(strstr(buffer, "Thread 2") != NULL);
    ASSERT_TRUE(strstr(buffer, "Thread 3") != NULL);
    
    // Verify overlap indicators
    ASSERT_TRUE(strstr(buffer, "||||") != NULL);  // Parallel bars
}
```

### 4. Performance Example Tests

#### Test: `perf_profile__sorting__then_identifies_bubble_sort`
```c
void test_performance_hotspot() {
    system("ada trace --output perf.atf ./perf_profile");
    system("ada analyze perf.atf --hotspots --top 5 > hotspots.txt");
    
    FILE* fp = fopen("hotspots.txt", "r");
    char line[256];
    
    // First hotspot should be bubble_sort
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "1.")) {
            ASSERT_TRUE(strstr(line, "bubble_sort") != NULL);
            ASSERT_TRUE(strstr(line, ">90%") != NULL);
            break;
        }
    }
    fclose(fp);
}
```

#### Test: `perf_profile__comparison__then_shows_difference`
```c
void test_algorithm_comparison() {
    TraceFile* trace = load_trace("perf.atf");
    
    double bubble_time = get_function_total_time(trace, "bubble_sort");
    double quick_time = get_function_total_time(trace, "quicksort");
    
    // Bubble sort should be much slower
    ASSERT_GT(bubble_time, quick_time * 10);
    
    // Verify O(n²) vs O(n log n) behavior
    int bubble_calls = count_function_calls(trace, "bubble_sort");
    int quick_calls = count_function_calls(trace, "quicksort");
    
    ASSERT_EQ(bubble_calls, 1);  // Called once
    ASSERT_GT(quick_calls, 10);   // Recursive calls
}
```

### 5. Memory Debugging Tests

#### Test: `memory_debug__leaks__then_all_detected`
```c
void test_memory_leak_detection() {
    system("ada trace --output memory.atf --track-memory ./memory_debug");
    system("ada analyze memory.atf --memory-leaks > leaks.txt");
    
    FILE* fp = fopen("leaks.txt", "r");
    char buffer[4096];
    size_t bytes = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);
    
    // Verify all leaks detected
    ASSERT_TRUE(strstr(buffer, "1024 bytes leaked") != NULL);  // leaky_function
    ASSERT_TRUE(strstr(buffer, "node_t") != NULL);             // Linked list
    ASSERT_TRUE(strstr(buffer, "100 bytes") != NULL);          // node->data
    
    // Verify leak locations
    ASSERT_TRUE(strstr(buffer, "leaky_function") != NULL);
    ASSERT_TRUE(strstr(buffer, "line") != NULL);  // Line numbers
}
```

#### Test: `memory_debug__use_after_free__then_detected`
```c
void test_use_after_free_detection() {
    system("ada trace --output uaf.atf --track-memory ./memory_debug 2>&1 | "
           "grep 'use-after-free' > uaf.txt");
    
    // Verify detection
    ASSERT_TRUE(file_size("uaf.txt") > 0);
    
    FILE* fp = fopen("uaf.txt", "r");
    char line[256];
    fgets(line, sizeof(line), fp);
    fclose(fp);
    
    ASSERT_TRUE(strstr(line, "use-after-free") != NULL);
    ASSERT_TRUE(strstr(line, "buffer") != NULL);
}
```

### 6. Signal Handling Tests

#### Test: `signal_trace__handlers__then_executed`
```c
void test_signal_handler_tracing() {
    // Start tracing in background
    pid_t trace_pid = fork();
    if (trace_pid == 0) {
        execl("ada", "ada", "trace", "--output", "sig.atf",
              "--track-signals", "./signal_trace", NULL);
        exit(1);
    }
    
    sleep(1);  // Let it start
    
    // Send signals
    pid_t app_pid = get_child_pid(trace_pid);
    kill(app_pid, SIGINT);
    usleep(100000);
    kill(app_pid, SIGINT);
    usleep(100000);
    kill(app_pid, SIGTERM);
    
    wait(NULL);  // Wait for completion
    
    // Analyze trace
    TraceFile* trace = load_trace("sig.atf");
    
    ASSERT_EQ(count_signal_deliveries(trace, SIGINT), 2);
    ASSERT_EQ(count_signal_deliveries(trace, SIGTERM), 1);
    
    ASSERT_TRUE(trace_contains_function(trace, "sigint_handler"));
    ASSERT_TRUE(trace_contains_function(trace, "sigterm_handler"));
}
```

### 7. Filter Example Tests

#### Test: `filter__by_function__then_only_matched`
```c
void test_function_filtering() {
    // Create test program with various functions
    system("ada trace --filter 'malloc|free' --output filtered.atf ./app");
    
    TraceFile* trace = load_trace("filtered.atf");
    
    // Should only contain malloc/free calls
    FunctionList* funcs = get_all_functions(trace);
    
    for (int i = 0; i < funcs->count; i++) {
        const char* name = funcs->names[i];
        ASSERT_TRUE(strstr(name, "malloc") != NULL ||
                   strstr(name, "free") != NULL ||
                   is_ada_internal(name));
    }
}
```

#### Test: `filter__by_thread__then_only_selected`
```c
void test_thread_filtering() {
    system("ada trace --thread-filter 1,3 --output thread_filter.atf ./thread_app");
    
    TraceFile* trace = load_trace("thread_filter.atf");
    
    // Should only have threads 1 and 3
    ASSERT_TRUE(has_thread(trace, 1));
    ASSERT_FALSE(has_thread(trace, 2));
    ASSERT_TRUE(has_thread(trace, 3));
    ASSERT_FALSE(has_thread(trace, 4));
}
```

### 8. Output Format Tests

#### Test: `format__json__then_valid_json`
```c
void test_json_output_format() {
    system("ada trace --format json --output trace.json ./hello_trace");
    
    // Verify valid JSON
    FILE* fp = fopen("trace.json", "r");
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* json = malloc(size + 1);
    fread(json, 1, size, fp);
    json[size] = '\0';
    fclose(fp);
    
    // Parse JSON
    json_t* root = json_parse(json);
    ASSERT_NOT_NULL(root);
    
    // Verify structure
    ASSERT_TRUE(json_has_key(root, "version"));
    ASSERT_TRUE(json_has_key(root, "events"));
    ASSERT_TRUE(json_has_key(root, "threads"));
    
    free(json);
}
```

#### Test: `format__csv__then_valid_csv`
```c
void test_csv_output_format() {
    system("ada analyze trace.atf --format csv > trace.csv");
    
    FILE* fp = fopen("trace.csv", "r");
    char header[256];
    fgets(header, sizeof(header), fp);
    
    // Verify CSV header
    ASSERT_TRUE(strstr(header, "timestamp") != NULL);
    ASSERT_TRUE(strstr(header, "thread_id") != NULL);
    ASSERT_TRUE(strstr(header, "function") != NULL);
    ASSERT_TRUE(strstr(header, "event_type") != NULL);
    
    // Verify data rows
    int row_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        row_count++;
        
        // Count commas for field count
        int commas = 0;
        for (char* p = line; *p; p++) {
            if (*p == ',') commas++;
        }
        ASSERT_EQ(commas, 3);  // 4 fields = 3 commas
    }
    
    ASSERT_GT(row_count, 0);
    fclose(fp);
}
```

### 9. CI Integration Tests

#### Test: `ci__github_action__then_succeeds`
```c
void test_github_action_example() {
    // Verify workflow file is valid
    const char* workflow = ".github/workflows/trace.yml";
    ASSERT_TRUE(file_exists(workflow));
    
    // Validate YAML syntax
    int result = system("yamllint .github/workflows/trace.yml");
    ASSERT_EQ(result, 0);
    
    // Simulate CI run
    system("act -j trace");  // Using act for local GitHub Action testing
    
    // Verify artifacts created
    ASSERT_TRUE(file_exists("pr_trace.atf"));
    ASSERT_TRUE(file_exists("metrics.json"));
}
```

### 10. Documentation Accuracy Tests

#### Test: `docs__commands__then_all_work`
```c
void test_all_documented_commands() {
    const char* commands[] = {
        "ada trace --output trace.atf ./app",
        "ada analyze trace.atf --summary",
        "ada analyze trace.atf --thread-timeline",
        "ada analyze trace.atf --flamegraph",
        "ada analyze trace.atf --hotspots --top 10",
        "ada trace --filter 'malloc|free' --output mem.atf ./app",
        "ada trace --sample-rate 100 --output sampled.atf ./app",
        NULL
    };
    
    for (int i = 0; commands[i]; i++) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s --help", commands[i]);
        
        int result = system(cmd);
        ASSERT_EQ(result, 0);  // Command exists and shows help
    }
}
```

#### Test: `docs__troubleshooting__then_solutions_work`
```c
void test_troubleshooting_solutions() {
    // Test permission check
    int result = system("ada diagnose --check-permissions");
    ASSERT_EQ(result, 0);
    
    // Test signing verification
    result = system("ada diagnose --verify-signing");
    ASSERT_EQ(result, 0);
    
    // Test buffer diagnostics
    result = system("ada diagnose --buffer-stats");
    ASSERT_EQ(result, 0);
}
```

## Performance Benchmarks

```c
void benchmark_example_overhead() {
    typedef struct {
        const char* example;
        double target_overhead;
        double actual_overhead;
    } Benchmark;
    
    Benchmark results[] = {
        {"hello_trace", 5.0, 0.0},
        {"thread_trace", 5.0, 0.0},
        {"perf_profile", 10.0, 0.0},  // Higher due to intensive ops
        {"memory_debug", 15.0, 0.0},  // Memory tracking overhead
        {"signal_trace", 5.0, 0.0},
    };
    
    for (int i = 0; i < 5; i++) {
        double baseline = run_without_tracing(results[i].example);
        double traced = run_with_tracing(results[i].example);
        
        results[i].actual_overhead = ((traced - baseline) / baseline) * 100;
        
        printf("%s: %.1f%% overhead (target: %.1f%%)\n",
               results[i].example,
               results[i].actual_overhead,
               results[i].target_overhead);
        
        ASSERT_LE(results[i].actual_overhead, results[i].target_overhead);
    }
}
```

## Acceptance Criteria
- [ ] All 8 example categories have working code
- [ ] All examples compile without warnings
- [ ] Each example demonstrates its intended feature
- [ ] Tracing overhead < 5% for basic examples
- [ ] Tracing overhead < 15% for debug examples
- [ ] All documented commands execute successfully
- [ ] Output formats are valid and parseable
- [ ] CI integration example runs in GitHub Actions
- [ ] Troubleshooting guide resolves common issues
- [ ] Performance meets specified targets