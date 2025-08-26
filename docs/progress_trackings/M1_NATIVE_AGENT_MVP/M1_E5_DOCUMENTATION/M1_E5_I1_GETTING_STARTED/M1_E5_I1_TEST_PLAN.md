# M1_E5_I1 Test Plan: Getting Started Guide

## Test Coverage Map

### Component Coverage Requirements
| Component | Files | Coverage Target | Priority |
|-----------|-------|-----------------|----------|
| Documentation Generator | doc_builder.c | 100% | P0 |
| Example Runner | example_runner.c | 100% | P0 |
| Troubleshooting Assistant | troubleshoot.c | 100% | P0 |
| Platform Validators | platform_check.c | 100% | P0 |
| Quick Reference | quick_ref.c | 100% | P1 |
| Output Formatters | formatters.c | 100% | P1 |

### Documentation Coverage Areas
| Section | Test Focus | Validation Method |
|---------|------------|-------------------|
| Prerequisites | Platform detection accuracy | Automated checks |
| Build Instructions | Command correctness | Build verification |
| First Trace | Example functionality | End-to-end test |
| Troubleshooting | Issue resolution rate | Scenario testing |
| Platform Guides | Platform-specific accuracy | Platform CI |
| Quick Reference | Command validity | Command execution |

## Test Matrix

### Platform Matrix
| Platform | Architecture | OS Version | Dev Cert | Test Suite |
|----------|--------------|------------|----------|------------|
| macOS | x86_64 | 12+ | Required | Full |
| macOS | arm64 | 12+ | Required | Full |
| Linux | x86_64 | Ubuntu 20.04+ | N/A | Full |
| Linux | x86_64 | RHEL 8+ | N/A | Full |
| Linux | arm64 | Ubuntu 22.04+ | N/A | Reduced |

### Documentation Validation Matrix
| Document Type | Format | Link Check | Code Compile | Output Verify |
|---------------|--------|------------|--------------|---------------|
| Getting Started | Markdown | Yes | Yes | Yes |
| Quick Reference | Markdown | Yes | N/A | Yes |
| Troubleshooting | Markdown | Yes | Yes | Yes |
| Examples | C/Shell | N/A | Yes | Yes |
| Platform Guides | Markdown | Yes | Yes | Yes |

## Test Scenarios

### 1. Unit Tests

#### Documentation Generator Tests
```c
// Test: doc_builder__init__with_valid_path__then_success
void test_doc_builder_init_success() {
    doc_builder_t builder;
    const char* output_dir = "/tmp/test_docs";
    
    int ret = doc_builder__init(&builder, output_dir);
    
    assert(ret == 0);
    assert(builder.config.output_dir == output_dir);
    assert(strcmp(builder.config.format, "markdown") == 0);
    assert(builder.config.include_examples == true);
    
    getting_started_guide_t* guide = atomic_load(&builder.guide);
    assert(guide != NULL);
    assert(strcmp(guide->metadata.version, "1.0.0") == 0);
}

// Test: doc_builder__generate__with_complete_guide__then_valid_markdown
void test_doc_builder_generate_markdown() {
    doc_builder_t builder;
    doc_builder__init(&builder, "/tmp/test_docs");
    
    // Populate guide
    getting_started_guide_t* guide = atomic_load(&builder.guide);
    guide->prerequisites.platform.os_version = "macOS 12+";
    guide->prerequisites.platform.min_memory_gb = 8;
    guide->build.clone_command = "git clone https://github.com/ada/ada.git";
    guide->build.build_command = "cargo build --release";
    
    int ret = doc_builder__generate_guide(&builder);
    
    assert(ret == 0);
    assert(access("GETTING_STARTED.md", F_OK) == 0);
    
    // Verify content
    FILE* fp = fopen("GETTING_STARTED.md", "r");
    char buffer[1024];
    fgets(buffer, sizeof(buffer), fp);
    assert(strstr(buffer, "# ADA Getting Started Guide") != NULL);
    fclose(fp);
}

// Test: doc_builder__section_access__concurrent__then_thread_safe
void test_doc_builder_concurrent_access() {
    doc_builder_t builder;
    doc_builder__init(&builder, "/tmp/test_docs");
    
    const int NUM_THREADS = 8;
    pthread_t threads[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, concurrent_generate_worker, &builder);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    uint32_t generation = atomic_load(&builder.generation);
    assert(generation == NUM_THREADS);
}
```

#### Example Runner Tests
```c
// Test: example_runner__execute__valid_example__then_success
void test_example_runner_valid_example() {
    example_runner_t runner = {0};
    example_gallery_t gallery = {0};
    
    // Create test example
    struct example ex = {
        .title = "Hello World Trace",
        .code.source = "#include <stdio.h>\nint main() { printf(\"Hello\\n\"); }",
        .code.build_command = "gcc -o hello hello.c",
        .trace.trace_command = "./tracer ./hello",
        .trace.expected_count = 10
    };
    
    gallery.examples = &ex;
    gallery.example_count = 1;
    runner.gallery = &gallery;
    
    int ret = example_runner__execute(&runner, 0);
    
    assert(ret == 0);
    assert(runner.output.stdout_size > 0);
}

// Test: example_runner__execute__invalid_index__then_error
void test_example_runner_invalid_index() {
    example_runner_t runner = {0};
    example_gallery_t gallery = {0};
    gallery.example_count = 5;
    runner.gallery = &gallery;
    
    int ret = example_runner__execute(&runner, 10);
    
    assert(ret == -1);
}

// Test: example_runner__build__compile_error__then_fail_gracefully
void test_example_runner_build_failure() {
    example_runner_t runner = {0};
    example_gallery_t gallery = {0};
    
    struct example ex = {
        .title = "Bad Example",
        .code.source = "invalid C code {",
        .code.build_command = "gcc -o bad bad.c"
    };
    
    gallery.examples = &ex;
    gallery.example_count = 1;
    runner.gallery = &gallery;
    
    int ret = example_runner__execute(&runner, 0);
    
    assert(ret == -1);
    assert(strstr(runner.output.stderr_buffer, "Build failed") != NULL);
}
```

#### Troubleshooting Assistant Tests
```c
// Test: troubleshoot__diagnose__known_issue__then_solution_found
void test_troubleshoot_known_issue() {
    troubleshoot_assistant_t assistant = {0};
    troubleshooting_guide_t guide = {0};
    
    struct issue issues[] = {
        {
            .symptom = "Build fails with 'cargo not found'",
            .category = "build",
            .diagnostics = (struct diagnostic[]){
                {
                    .check_command = "which cargo",
                    .expected_output = "/usr/local/bin/cargo"
                }
            },
            .diagnostic_count = 1,
            .solutions = (struct solution[]){
                {
                    .description = "Install Rust toolchain",
                    .commands = {"curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"},
                    .command_count = 1
                }
            },
            .solution_count = 1
        }
    };
    
    guide.issues = issues;
    guide.issue_count = 1;
    assistant.guide = &guide;
    
    // Simulate diagnosis
    assistant.state.current_issue = 0;
    int ret = troubleshoot__apply_solution(&assistant, 0);
    
    assert(ret == 0);
    assert(assistant.history.visited_count > 0);
}

// Test: troubleshoot__flowchart__navigation__then_correct_path
void test_troubleshoot_flowchart_navigation() {
    troubleshoot_assistant_t assistant = {0};
    troubleshooting_guide_t guide = {0};
    
    struct flow_node nodes[] = {
        {.question = "Is cargo installed?", .yes_next = "check_version", .no_next = "install_rust"},
        {.question = "Is version >= 1.70?", .yes_next = "build", .no_next = "update_rust"}
    };
    
    guide.diagnostic_flow.nodes = nodes;
    guide.diagnostic_flow.node_count = 2;
    assistant.guide = &guide;
    
    const char* next = troubleshoot__navigate(&assistant, "no");
    
    assert(strcmp(next, "install_rust") == 0);
}
```

### 2. Integration Tests

#### End-to-End Documentation Build
```c
// Test: docs__full_build__all_sections__then_complete_guide
void test_docs_full_build() {
    // Initialize all components
    doc_builder_t builder;
    example_gallery_t gallery;
    troubleshooting_guide_t troubleshoot;
    quick_reference_t reference;
    
    doc_builder__init(&builder, "./docs_output");
    
    // Generate all sections
    int ret = doc_builder__generate_all(&builder);
    assert(ret == 0);
    
    // Verify all files exist
    assert(access("./docs_output/GETTING_STARTED.md", F_OK) == 0);
    assert(access("./docs_output/QUICK_REFERENCE.md", F_OK) == 0);
    assert(access("./docs_output/TROUBLESHOOTING.md", F_OK) == 0);
    assert(access("./docs_output/EXAMPLES.md", F_OK) == 0);
    
    // Verify cross-references
    ret = verify_internal_links("./docs_output");
    assert(ret == 0);
}

// Test: examples__all__compile_and_run__then_expected_output
void test_all_examples_execution() {
    example_gallery_t gallery;
    load_example_gallery(&gallery, "./examples");
    
    size_t passed = 0;
    size_t failed = 0;
    
    for (size_t i = 0; i < gallery.example_count; i++) {
        example_runner_t runner = {0};
        runner.gallery = &gallery;
        
        int ret = example_runner__execute(&runner, i);
        if (ret == 0) {
            passed++;
        } else {
            failed++;
            printf("Failed example: %s\n", gallery.examples[i].title);
        }
    }
    
    printf("Examples: %zu passed, %zu failed\n", passed, failed);
    assert(failed == 0);
}
```

### 3. Platform-Specific Tests

#### macOS Code Signing Validation
```c
// Test: macos__code_signing__unsigned_binary__then_guide_helps
void test_macos_code_signing_guide() {
    #ifdef __APPLE__
    // Create unsigned test binary
    system("gcc -o test_binary test.c");
    
    // Verify signing detection
    int is_signed = check_code_signature("test_binary");
    assert(is_signed == 0);
    
    // Apply signing guide
    doc_builder_t builder;
    doc_builder__init(&builder, "/tmp");
    
    const char* signing_guide = get_platform_guide(&builder, "macos_code_signing");
    assert(signing_guide != NULL);
    
    // Follow guide to sign
    int ret = system("./utils/sign_binary.sh test_binary");
    assert(ret == 0);
    
    // Verify signed
    is_signed = check_code_signature("test_binary");
    assert(is_signed == 1);
    #endif
}

// Test: linux__capabilities__missing_ptrace__then_guide_resolves
void test_linux_capabilities_guide() {
    #ifdef __linux__
    // Check ptrace capability
    int has_ptrace = check_capability(CAP_SYS_PTRACE);
    
    if (!has_ptrace) {
        // Get troubleshooting guide
        troubleshoot_assistant_t assistant;
        const char* solution = troubleshoot__get_solution(&assistant, 
                                                         "missing_ptrace_capability");
        assert(solution != NULL);
        
        // Verify solution contains setcap command
        assert(strstr(solution, "setcap cap_sys_ptrace") != NULL);
    }
    #endif
}
```

### 4. User Journey Tests

#### New User Onboarding Flow
```c
// Test: user_journey__new_user__complete_flow__then_first_trace_success
void test_new_user_complete_journey() {
    // Simulate new user environment
    setup_clean_environment();
    
    // Step 1: Prerequisites check
    int prereq_met = check_prerequisites();
    if (!prereq_met) {
        // Follow prerequisite guide
        follow_prerequisite_guide();
        prereq_met = check_prerequisites();
        assert(prereq_met == 1);
    }
    
    // Step 2: Build system
    int build_ret = system("cargo build --release");
    assert(build_ret == 0);
    
    // Step 3: Run verification
    int verify_ret = system("cargo test --all");
    assert(verify_ret == 0);
    
    // Step 4: First trace
    create_hello_world_program();
    int trace_ret = system("./target/release/tracer ./hello");
    assert(trace_ret == 0);
    
    // Step 5: Verify output
    assert(access("trace.atf", F_OK) == 0);
}

// Test: user_journey__build_failure__troubleshoot__then_resolve
void test_user_troubleshooting_journey() {
    // Simulate build failure
    setenv("PATH", "/invalid/path", 1);
    
    int build_ret = system("cargo build");
    assert(build_ret != 0);
    
    // Use troubleshooting guide
    troubleshoot_assistant_t assistant;
    init_troubleshoot_assistant(&assistant);
    
    const char* diagnosis = troubleshoot__diagnose_build_failure(&assistant);
    assert(strstr(diagnosis, "cargo not found") != NULL);
    
    // Apply solution
    const char* solution = troubleshoot__get_solution(&assistant, diagnosis);
    system(solution);
    
    // Retry build
    build_ret = system("cargo build");
    assert(build_ret == 0);
}
```

## Performance Benchmarks

### Documentation Generation Performance
```c
// Benchmark: Guide generation throughput
void benchmark_guide_generation() {
    doc_builder_t builder;
    doc_builder__init(&builder, "/tmp/bench");
    
    uint64_t start = get_timestamp_ns();
    
    for (int i = 0; i < 100; i++) {
        doc_builder__generate_guide(&builder);
    }
    
    uint64_t elapsed = get_timestamp_ns() - start;
    double ms_per_gen = (elapsed / 100.0) / 1000000.0;
    
    printf("Guide generation: %.2f ms/generation\n", ms_per_gen);
    assert(ms_per_gen < 100.0);  // Target: < 100ms
}

// Benchmark: Example execution time
void benchmark_example_execution() {
    example_gallery_t gallery;
    load_standard_examples(&gallery);
    
    uint64_t total_time = 0;
    
    for (size_t i = 0; i < gallery.example_count; i++) {
        uint64_t start = get_timestamp_ns();
        
        example_runner_t runner = {.gallery = &gallery};
        example_runner__execute(&runner, i);
        
        uint64_t elapsed = get_timestamp_ns() - start;
        total_time += elapsed;
    }
    
    double avg_ms = (total_time / gallery.example_count) / 1000000.0;
    printf("Average example execution: %.2f ms\n", avg_ms);
    assert(avg_ms < 500.0);  // Target: < 500ms per example
}

// Benchmark: Troubleshooting lookup speed
void benchmark_troubleshoot_lookup() {
    troubleshooting_guide_t guide;
    load_full_troubleshooting_guide(&guide);
    
    const char* symptoms[] = {
        "build fails",
        "trace crashes",
        "no output",
        "permission denied",
        "code signing error"
    };
    
    uint64_t start = get_timestamp_ns();
    
    for (int i = 0; i < 1000; i++) {
        const char* symptom = symptoms[i % 5];
        struct issue* issue = troubleshoot__find_issue(&guide, symptom);
        assert(issue != NULL);
    }
    
    uint64_t elapsed = get_timestamp_ns() - start;
    double us_per_lookup = (elapsed / 1000.0) / 1000.0;
    
    printf("Troubleshooting lookup: %.2f μs/lookup\n", us_per_lookup);
    assert(us_per_lookup < 10000.0);  // Target: < 10ms
}
```

## Acceptance Criteria

### Documentation Quality Criteria
1. **Completeness**
   - All prerequisite steps documented
   - All platforms covered
   - All common issues addressed
   - All examples compile and run

2. **Accuracy**
   - Commands execute correctly
   - Output matches examples
   - Platform-specific notes accurate
   - Version requirements current

3. **Usability**
   - New user success rate > 90%
   - Time to first trace < 10 minutes
   - Issue resolution rate > 90%
   - Example success rate = 100%

### Technical Acceptance Criteria
1. **Build Success**
   - All documentation generators compile
   - All examples compile without warnings
   - All platform variants build

2. **Test Coverage**
   - 100% line coverage on documentation code
   - All examples have tests
   - All troubleshooting paths tested
   - Platform-specific code fully covered

3. **Performance Targets**
   - Guide generation < 100ms
   - Example execution < 500ms
   - Troubleshooting lookup < 10ms
   - Full documentation build < 5s

4. **Link Validation**
   - All internal links valid
   - All external links reachable
   - All code references correct
   - All command examples verified

### User Experience Criteria
1. **First-Time User**
   - Clear prerequisite checklist
   - Step-by-step build instructions
   - Working first trace in < 10 minutes
   - Troubleshooting for common issues

2. **Platform Support**
   - macOS guide with code signing
   - Linux guide with capabilities
   - Container deployment guide
   - SSH tracing instructions

3. **Error Recovery**
   - Clear error messages
   - Diagnostic commands provided
   - Solution steps documented
   - Escalation path defined

## Test Execution Schedule

### Phase 1: Documentation Development (Day 1)
- [ ] Implement doc_builder component
- [ ] Create example_runner system
- [ ] Build troubleshoot_assistant
- [ ] Write platform validators

### Phase 2: Content Creation (Day 2)
- [ ] Write Getting Started guide
- [ ] Create Quick Reference
- [ ] Build Troubleshooting guide
- [ ] Develop Example Gallery

### Phase 3: Testing (Day 3)
- [ ] Run unit test suite
- [ ] Execute integration tests
- [ ] Validate on all platforms
- [ ] User journey testing

### Phase 4: Polish (Day 4)
- [ ] Fix identified issues
- [ ] Optimize performance
- [ ] Final validation
- [ ] Release preparation