use std::env;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rerun-if-changed=include/");
    println!("cargo:rerun-if-changed=CMakeLists.txt");
    println!("cargo:rerun-if-changed=tests/");
    
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let profile = env::var("PROFILE").unwrap();
    
    // Determine build type based on profile
    let build_type = match profile.as_str() {
        "debug" => "Debug",
        "release" => "Release",
        _ => "Debug",
    };
    
    // Discover git top-level (workspace root) for deterministic paths
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let workspace_root = git_top_level(&manifest_dir)
        .unwrap_or_else(|| manifest_dir.parent().unwrap_or(&manifest_dir).to_path_buf());

    // Expose workspace root and build profile
    println!("cargo:rustc-env=ADA_WORKSPACE_ROOT={}", workspace_root.display());
    println!("cargo:rustc-env=ADA_BUILD_PROFILE={}", profile);

    // Check if coverage is enabled via Cargo feature
    let coverage_enabled = env::var("CARGO_FEATURE_COVERAGE").is_ok();
    
    // Build the C/C++ components using cmake
    let mut cmake_config = cmake::Config::new(".");
    cmake_config
        .define("CMAKE_BUILD_TYPE", build_type)
        .define("CMAKE_EXPORT_COMPILE_COMMANDS", "ON") // Generate compile_commands.json
        .define("BUILD_TESTING", "ON") // Build tests including Google Test
        .define("CMAKE_CXX_STANDARD", "17") // Ensure C++17 for Google Test
        .define("ADA_WORKSPACE_ROOT", workspace_root.display().to_string())
        .define("ADA_BUILD_PROFILE", &profile);
    
    // Add coverage flags if enabled
    if coverage_enabled {
        println!("cargo:info=Coverage instrumentation enabled for C/C++ code");
        cmake_config.define("ENABLE_COVERAGE", "ON");
        // Don't set LLVM_PROFILE_FILE here - it should be set at runtime when tests execute
    }
    // Opt-in sanitizers via env toggles
    if env::var("ADA_ENABLE_THREAD_SANITIZER").is_ok() {
        println!("cargo:info=ThreadSanitizer enabled for C/C++ code");
        cmake_config.define("ENABLE_THREAD_SANITIZER", "ON");
    }
    if env::var("ADA_ENABLE_ADDRESS_SANITIZER").is_ok() {
        println!("cargo:info=AddressSanitizer enabled for C/C++ code");
        cmake_config.define("ENABLE_ADDRESS_SANITIZER", "ON");
    }
    
    // Enable C++ registry if feature is set
    
    let dst = cmake_config.build();
    
    // Report the location of compile_commands.json for IDE consumption
    let compile_commands_path = dst.join("build").join("compile_commands.json");
    if compile_commands_path.exists() {
        println!("cargo:info=compile_commands.json generated at: {}", compile_commands_path.display());
        
        // Also create a symlink in a predictable location for easier IDE configuration
        let target_dir = env::var("CARGO_TARGET_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|_| {
                // Get the workspace root by going up from OUT_DIR
                let out_path = Path::new(&out_dir);
                // OUT_DIR is typically: target/{debug,release}/build/tracer_backend-{hash}/out
                // We want: target/
                out_path.ancestors()
                    .find(|p| p.file_name() == Some(std::ffi::OsStr::new("target")))
                    .map(|p| p.to_path_buf())
                    .unwrap_or_else(|| PathBuf::from("target"))
            });
        let link_path = target_dir.join("compile_commands.json");
        
        // Remove old symlink if it exists
        let _ = std::fs::remove_file(&link_path);
        
        // Create new symlink (platform-specific)
        #[cfg(unix)]
        {
            if let Err(e) = std::os::unix::fs::symlink(&compile_commands_path, &link_path) {
                println!("cargo:warning=Failed to create compile_commands.json symlink: {}", e);
            } else {
                println!("cargo:info=compile_commands.json symlinked to: {}", link_path.display());
            }
        }
        
        #[cfg(windows)]
        {
            if let Err(e) = std::os::windows::fs::symlink_file(&compile_commands_path, &link_path) {
                println!("cargo:warning=Failed to create compile_commands.json symlink: {}", e);
            } else {
                println!("cargo:info=compile_commands.json symlinked to: {}", link_path.display());
            }
        }
    }
    
    // Link the libraries we built
    println!("cargo:rustc-link-search=native={}/lib", dst.display());
    println!("cargo:rustc-link-lib=static=tracer_controller");
    println!("cargo:rustc-link-lib=static=tracer_utils");
    println!("cargo:rustc-link-lib=static=tracer_drain_thread");
    println!("cargo:rustc-link-lib=static=tracer_atf_writer");
    
    // Link C++ standard library (needed for ring_buffer.cpp and thread_registry.cpp)
    println!("cargo:rustc-link-lib=c++");
    
    // Link Frida libraries - use absolute paths
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let third_parties = manifest_dir.parent().unwrap().join("third_parties");
    println!("cargo:rustc-link-search=native={}/frida-core", third_parties.display());
    println!("cargo:rustc-link-search=native={}/frida-gum", third_parties.display());
    println!("cargo:rustc-link-lib=static=frida-core");
    println!("cargo:rustc-link-lib=static=frida-gum");
    
    // Platform-specific linking
    if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=framework=Foundation");
        println!("cargo:rustc-link-lib=framework=AppKit");
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=IOKit");
        println!("cargo:rustc-link-lib=framework=Security");
        println!("cargo:rustc-link-lib=bsm");
        println!("cargo:rustc-link-lib=resolv");
    } else if cfg!(target_os = "linux") {
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=dl");
        println!("cargo:rustc-link-lib=m");
    }
    
    // Copy built artifacts to predictable locations in target/{profile}/tracer_backend/
    let profile = env::var("PROFILE").unwrap();
    let target_base = Path::new(&out_dir)
        .ancestors()
        .find(|p| p.file_name() == Some(std::ffi::OsStr::new("target")))
        .unwrap_or_else(|| Path::new("target"));
    
    let predictable_dir = target_base.join(&profile).join("tracer_backend");
    
    // Create directories
    std::fs::create_dir_all(predictable_dir.join("bin")).ok();
    std::fs::create_dir_all(predictable_dir.join("lib")).ok();
    std::fs::create_dir_all(predictable_dir.join("test")).ok();
    
    // Copy binaries
    let binaries = vec![
        ("bin/tracer_poc", "bin/tracer_poc"),
        ("out/bin/tracer_poc", "bin/tracer_poc"),
        ("build/bin/test_cli", "test/test_cli"),
        ("build/bin/test_runloop", "test/test_runloop"),
        ("bin/test_cli", "test/test_cli"),
        ("bin/test_runloop", "test/test_runloop"),
        // Google Test executables (multiple possible source locations)
        // Utils unit tests
        ("build/test_ring_buffer", "test/test_ring_buffer"),
        ("build/tests/unit/utils/test_ring_buffer", "test/test_ring_buffer"),
        ("out/bin/test_ring_buffer", "test/test_ring_buffer"),
        ("build/test_ring_buffer_attach", "test/test_ring_buffer_attach"),
        ("build/tests/unit/utils/test_ring_buffer_attach", "test/test_ring_buffer_attach"),
        ("out/bin/test_ring_buffer_attach", "test/test_ring_buffer_attach"),
        ("build/test_shared_memory", "test/test_shared_memory"),
        ("build/tests/unit/utils/test_shared_memory", "test/test_shared_memory"),
        ("out/bin/test_shared_memory", "test/test_shared_memory"),
        ("build/test_thread_registry", "test/test_thread_registry"),
        ("build/tests/unit/utils/test_thread_registry", "test/test_thread_registry"),
        ("out/bin/test_thread_registry", "test/test_thread_registry"),
        ("build/test_thread_registry_accessors", "test/test_thread_registry_accessors"),
        ("build/tests/unit/utils/test_thread_registry_accessors", "test/test_thread_registry_accessors"),
        ("out/bin/test_thread_registry_accessors", "test/test_thread_registry_accessors"),
        ("build/test_cli_modes", "test/test_cli_modes"),
        ("build/tests/unit/utils/test_cli_modes", "test/test_cli_modes"),
        ("out/bin/test_cli_modes", "test/test_cli_modes"),
        ("build/tests/unit/cli_parser/test_cli_parser_mode_detection", "test/test_cli_parser_mode_detection"),
        ("build/tests/unit/cli_parser/test_cli_parser_args", "test/test_cli_parser_args"),
        ("build/tests/unit/cli_parser/test_cli_parser_comprehensive", "test/test_cli_parser_comprehensive"),
        ("build/tests/unit/selective_persistence/test_marking_policy", "test/test_marking_policy"),
        ("build/tests/unit/selective_persistence/test_detail_lane_control", "test/test_detail_lane_control"),
        ("build/tests/unit/selective_persistence/test_selective_persistence_support", "test/test_selective_persistence_support"),
        ("build/test_coverage_gaps", "test/test_coverage_gaps"),
        ("build/tests/unit/selective_persistence/test_coverage_gaps", "test/test_coverage_gaps"),
        ("out/bin/test_coverage_gaps", "test/test_coverage_gaps"),
        ("build/tests/unit/selective_persistence/test_force_coverage", "test/test_force_coverage"),
        // Metrics unit tests
        ("build/test_thread_metrics", "test/test_thread_metrics"),
        ("build/tests/unit/metrics/test_thread_metrics", "test/test_thread_metrics"),
        ("out/bin/test_thread_metrics", "test/test_thread_metrics"),
        ("build/test_metrics_coverage", "test/test_metrics_coverage"),
        ("build/tests/unit/metrics/test_metrics_coverage", "test/test_metrics_coverage"),
        ("out/bin/test_metrics_coverage", "test/test_metrics_coverage"),
        ("build/test_metrics_reporter", "test/test_metrics_reporter"),
        ("build/tests/unit/metrics/test_metrics_reporter", "test/test_metrics_reporter"),
        ("out/bin/test_metrics_reporter", "test/test_metrics_reporter"),
        ("build/test_cli_malloc_failure", "test/test_cli_malloc_failure"),
        ("build/tests/unit/utils/test_cli_malloc_failure", "test/test_cli_malloc_failure"),
        ("out/bin/test_cli_malloc_failure", "test/test_cli_malloc_failure"),
        ("build/test_control_block_ipc", "test/test_control_block_ipc"),
        ("build/tests/unit/utils/test_control_block_ipc", "test/test_control_block_ipc"),
        ("out/bin/test_control_block_ipc", "test/test_control_block_ipc"),
        ("build/test_shm_directory", "test/test_shm_directory"),
        ("build/tests/unit/utils/test_shm_directory", "test/test_shm_directory"),
        ("out/bin/test_shm_directory", "test/test_shm_directory"),
        ("build/test_thread_registration_tls", "test/test_thread_registration_tls"),
        ("build/tests/unit/utils/test_thread_registration_tls", "test/test_thread_registration_tls"),
        ("out/bin/test_thread_registration_tls", "test/test_thread_registration_tls"),
        ("build/test_offsets_materialization", "test/test_offsets_materialization"),
        ("build/tests/unit/utils/test_offsets_materialization", "test/test_offsets_materialization"),
        ("out/bin/test_offsets_materialization", "test/test_offsets_materialization"),
        ("build/test_spsc_queue", "test/test_spsc_queue"),
        ("build/tests/unit/utils/test_spsc_queue", "test/test_spsc_queue"),
        ("out/bin/test_spsc_queue", "test/test_spsc_queue"),
        ("build/test_drain_thread", "test/test_drain_thread"),
        ("build/tests/unit/drain_thread/test_drain_thread", "test/test_drain_thread"),
        ("out/bin/test_drain_thread", "test/test_drain_thread"),
        ("build/test_drain_metrics", "test/test_drain_metrics"),
        ("build/tests/unit/drain_thread/test_drain_metrics", "test/test_drain_metrics"),
        ("out/bin/test_drain_metrics", "test/test_drain_metrics"),
        ("build/test_timer", "test/test_timer"),
        ("build/tests/unit/timer/test_timer", "test/test_timer"),
        ("out/bin/test_timer", "test/test_timer"),
        ("build/test_atf_v4_writer", "test/test_atf_v4_writer"),
        ("build/tests/unit/atf_v4/test_atf_v4_writer", "test/test_atf_v4_writer"),
        ("out/bin/test_atf_v4_writer", "test/test_atf_v4_writer"),
        ("build/test_atf_v4_writer_integration", "test/test_atf_v4_writer_integration"),
        ("build/tests/integration/atf_v4/test_atf_v4_writer_integration", "test/test_atf_v4_writer_integration"),
        ("out/bin/test_atf_v4_writer_integration", "test/test_atf_v4_writer_integration"),
        ("build/test_per_thread_drain", "test/test_per_thread_drain"),
        ("build/tests/unit/drain_thread/test_per_thread_drain", "test/test_per_thread_drain"),
        ("out/bin/test_per_thread_drain", "test/test_per_thread_drain"),
        ("build/test_ring_pool_swap", "test/test_ring_pool_swap"),
        ("build/tests/unit/utils/test_ring_pool_swap", "test/test_ring_pool_swap"),
        ("out/bin/test_ring_pool_swap", "test/test_ring_pool_swap"),
        ("build/test_thread_pools", "test/test_thread_pools"),
        ("build/tests/unit/utils/test_thread_pools", "test/test_thread_pools"),
        ("out/bin/test_thread_pools", "test/test_thread_pools"),
        // Note: test_thread_registry_cpp is not built; entries removed
        // Controller unit tests
        ("build/test_spawn_method", "test/test_spawn_method"),
        ("build/tests/unit/controller/test_spawn_method", "test/test_spawn_method"),
        ("out/bin/test_spawn_method", "test/test_spawn_method"),
        ("build/test_controller_heartbeat", "test/test_controller_heartbeat"),
        ("build/tests/unit/controller/test_controller_heartbeat", "test/test_controller_heartbeat"),
        ("out/bin/test_controller_heartbeat", "test/test_controller_heartbeat"),
        ("build/test_controller_env_propagation", "test/test_controller_env_propagation"),
        ("build/tests/unit/controller/test_controller_env_propagation", "test/test_controller_env_propagation"),
        ("out/bin/test_controller_env_propagation", "test/test_controller_env_propagation"),
        ("build/test_controller_coverage", "test/test_controller_coverage"),
        ("build/tests/unit/controller/test_controller_coverage", "test/test_controller_coverage"),
        ("out/bin/test_controller_coverage", "test/test_controller_coverage"),
        ("build/test_controller_usage", "test/test_controller_usage"),
        ("build/tests/unit/controller/test_controller_usage", "test/test_controller_usage"),
        ("out/bin/test_controller_usage", "test/test_controller_usage"),
        ("build/test_controller_main", "test/test_controller_main"),
        ("build/tests/unit/controller/test_controller_main", "test/test_controller_main"),
        ("out/bin/test_controller_main", "test/test_controller_main"),
        ("build/test_signal_shutdown", "test/test_signal_shutdown"),
        ("build/tests/unit/controller/test_signal_shutdown", "test/test_signal_shutdown"),
        ("out/bin/test_signal_shutdown", "test/test_signal_shutdown"),
        // Agent unit tests
        ("build/test_agent_state_machine", "test/test_agent_state_machine"),
        ("build/tests/unit/agent/test_agent_state_machine", "test/test_agent_state_machine"),
        ("out/bin/test_agent_state_machine", "test/test_agent_state_machine"),
        ("build/test_agent_coverage", "test/test_agent_coverage"),
        ("build/tests/unit/agent/test_agent_coverage", "test/test_agent_coverage"),
        ("out/bin/test_agent_coverage", "test/test_agent_coverage"),
        ("build/test_agent_mode_tick", "test/test_agent_mode_tick"),
        ("build/tests/unit/agent/test_agent_mode_tick", "test/test_agent_mode_tick"),
        ("out/bin/test_agent_mode_tick", "test/test_agent_mode_tick"),
        ("build/test_exclude_list", "test/test_exclude_list"),
        ("build/tests/unit/agent/test_exclude_list", "test/test_exclude_list"),
        ("build/tests/unit/agent/test_dso_management", "test/test_dso_management"),
        ("build/tests/unit/agent/test_hook_registry", "test/test_hook_registry"),
        ("build/tests/unit/agent/test_comprehensive_hooks", "test/test_comprehensive_hooks"),
        ("out/bin/test_exclude_list", "test/test_exclude_list"),
        ("out/bin/test_dso_management", "test/test_dso_management"),
        ("out/bin/test_hook_registry", "test/test_hook_registry"),
        ("out/bin/test_comprehensive_hooks", "test/test_comprehensive_hooks"),
        // Utils integration tests
        ("build/test_thread_registry_integration", "test/test_thread_registry_integration"),
        ("build/tests/integration/utils/test_thread_registry_integration", "test/test_thread_registry_integration"),
        ("out/bin/test_thread_registry_integration", "test/test_thread_registry_integration"),
        ("build/test_drain_thread_integration", "test/test_drain_thread_integration"),
        ("build/tests/integration/drain_thread/test_drain_thread_integration", "test/test_drain_thread_integration"),
        ("out/bin/test_drain_thread_integration", "test/test_drain_thread_integration"),
        // Controller integration tests
        ("build/test_controller_full_lifecycle", "test/test_controller_full_lifecycle"),
        ("build/tests/integration/controller/test_controller_full_lifecycle", "test/test_controller_full_lifecycle"),
        ("out/bin/test_controller_full_lifecycle", "test/test_controller_full_lifecycle"),
        ("build/test_integration", "test/test_integration"),
        ("build/tests/integration/controller/test_integration", "test/test_integration"),
        ("out/bin/test_integration", "test/test_integration"),
        ("build/test_registry_lifecycle", "test/test_registry_lifecycle"),
        ("build/tests/integration/controller/test_registry_lifecycle", "test/test_registry_lifecycle"),
        ("out/bin/test_registry_lifecycle", "test/test_registry_lifecycle"),
        // Selective persistence integration tests
        ("build/test_selective_persistence_integration", "test/test_selective_persistence_integration"),
        ("build/tests/integration/selective_persistence/test_selective_persistence_integration", "test/test_selective_persistence_integration"),
        ("out/bin/test_selective_persistence_integration", "test/test_selective_persistence_integration"),
        // Metrics integration tests
        ("build/test_metrics_integration", "test/test_metrics_integration"),
        ("build/tests/integration/metrics/test_metrics_integration", "test/test_metrics_integration"),
        ("out/bin/test_metrics_integration", "test/test_metrics_integration"),
        // Agent integration tests
        ("build/test_agent_loader", "test/test_agent_loader"),
        ("build/tests/integration/agent/test_agent_loader", "test/test_agent_loader"),
        ("out/bin/test_agent_loader", "test/test_agent_loader"),
        ("build/test_baseline_hooks", "test/test_baseline_hooks"),
        ("build/tests/integration/agent/test_baseline_hooks", "test/test_baseline_hooks"),
        ("out/bin/test_baseline_hooks", "test/test_baseline_hooks"),
        ("build/test_agent_registry_modes", "test/test_agent_registry_modes"),
        ("build/tests/integration/agent/test_agent_registry_modes", "test/test_agent_registry_modes"),
        ("out/bin/test_agent_registry_modes", "test/test_agent_registry_modes"),
        // System integration tests
        ("build/test_system_integration", "test/test_system_integration"),
        ("build/tests/integration/system/test_system_integration", "test/test_system_integration"),
        ("out/bin/test_system_integration", "test/test_system_integration"),
        // Registry benchmark tests
        ("build/test_registry_bench", "test/test_registry_bench"),
        ("build/tests/bench/registry/test_registry_bench", "test/test_registry_bench"),
        ("out/bin/test_registry_bench", "test/test_registry_bench"),
        ("build/bench_backpressure_overhead", "test/bench_backpressure_overhead"),
        (
            "build/tests/bench/backpressure/bench_backpressure_overhead",
            "test/bench_backpressure_overhead",
        ),
        ("out/bin/bench_backpressure_overhead", "test/bench_backpressure_overhead"),
        // Backpressure unit tests
        ("build/test_backpressure_state", "test/test_backpressure_state"),
        ("build/tests/unit/backpressure/test_backpressure_state", "test/test_backpressure_state"),
        ("out/bin/test_backpressure_state", "test/test_backpressure_state"),
    ];
    
    for (src_path, dst_path) in binaries {
        let src = dst.join(src_path);
        let dst_file = predictable_dir.join(dst_path);
        if src.exists() {
            if let Err(e) = std::fs::copy(&src, &dst_file) {
                println!("cargo:warning=Failed to copy {}: {}", src_path, e);
            } else {
                println!("cargo:info=Copied {} to {}", src_path, dst_file.display());
            }
        }
    }
    
    // Copy libraries
    let lib_ext = if cfg!(target_os = "macos") {
        vec![("dylib", "dylib"), ("a", "a")]
    } else if cfg!(target_os = "windows") {
        vec![("dll", "dll"), ("lib", "lib")]
    } else {
        vec![("so", "so"), ("a", "a")]
    };
    
    for (src_ext, _dst_ext) in lib_ext {
        let _pattern = format!("lib/*.{}", src_ext);
        if let Ok(entries) = std::fs::read_dir(dst.join("lib")) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.extension().and_then(|s| s.to_str()) == Some(src_ext) {
                    let filename = path.file_name().unwrap();
                    let dst_file = predictable_dir.join("lib").join(filename);
                    if let Err(e) = std::fs::copy(&path, &dst_file) {
                        println!("cargo:warning=Failed to copy library: {}", e);
                    } else {
                        println!("cargo:info=Copied {} to {}", path.display(), dst_file.display());
                    }
                }
            }
        }
    }
    
    // Report the predictable directory location
    println!("cargo:info=");
    println!("cargo:info=All binaries and libraries copied to: {}", predictable_dir.display());
    println!("cargo:info=  Binaries:  {}/bin/", predictable_dir.display());
    println!("cargo:info=  Tests:     {}/test/", predictable_dir.display());
    println!("cargo:info=  Libraries: {}/lib/", predictable_dir.display());

    // Generate Rust test wrappers for discovered C++ gtests
    if let Err(e) = generate_gtest_wrappers(&out_dir, &predictable_dir, &dst) {
        println!("cargo:warning=Failed to generate gtest wrappers: {}", e);
    }
    
    // Generate bindings (optional - for better Rust integration)
    generate_bindings(&out_dir);
}

fn generate_bindings(out_dir: &Path) {
    // Check if bindgen is available
    if let Ok(output) = std::process::Command::new("bindgen")
        .arg("--version")
        .output()
    {
        if output.status.success() {
            // Generate bindings for our C headers
            let bindings = bindgen::Builder::default()
                .header("include/frida_controller.h")
                .header("include/shared_memory.h")
                .header("include/ring_buffer.h")
                .header("include/tracer_types.h")
                .clang_arg("-I../third_parties/frida-core")
                .clang_arg("-I../third_parties/frida-gum")
                .allowlist_type("FridaController")
                .allowlist_type("SharedMemory")
                .allowlist_type("RingBuffer")
                .allowlist_type("IndexEvent")
                .allowlist_type("DetailEvent")
                .allowlist_type("TracerStats")
                .allowlist_function("frida_controller_.*")
                .allowlist_function("shared_memory_.*")
                .allowlist_function("ring_buffer_.*")
                .derive_default(true)
                .derive_debug(true)
                .generate()
                .expect("Unable to generate bindings");
            
            let bindings_path = out_dir.join("bindings.rs");
            bindings
                .write_to_file(bindings_path)
                .expect("Couldn't write bindings!");
        }
    }
}

fn git_top_level(start: &Path) -> Option<PathBuf> {
    // Try using `git rev-parse --show-toplevel`
    if let Ok(output) = Command::new("git")
        .current_dir(start)
        .arg("rev-parse")
        .arg("--show-toplevel")
        .output()
    {
        if output.status.success() {
            let s = String::from_utf8_lossy(&output.stdout).trim().to_string();
            if !s.is_empty() { return Some(PathBuf::from(s)); }
        }
    }
    None
}

fn generate_gtest_wrappers(out_dir: &Path, predictable_dir: &Path, dst: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let mut test_bin_dirs: Vec<PathBuf> = Vec::new();
    let copied = predictable_dir.join("test");
    if copied.is_dir() { test_bin_dirs.push(copied); }
    let out_bin = dst.join("out").join("bin");
    if out_bin.is_dir() { test_bin_dirs.push(out_bin); }
    let build_bin = dst.join("build").join("bin");
    if build_bin.is_dir() { test_bin_dirs.push(build_bin); }
    let root_bin = dst.join("bin");
    if root_bin.is_dir() { test_bin_dirs.push(root_bin); }

    // Discover test executables starting with "test_", dedup by filename
    // Preference order is the order of test_bin_dirs (copied dir first)
    let mut test_bins: Vec<PathBuf> = Vec::new();
    let mut seen: std::collections::HashSet<String> = std::collections::HashSet::new();
    for dir in &test_bin_dirs {
        if let Ok(entries) = fs::read_dir(dir) {
            for e in entries.flatten() {
                let p = e.path();
                if !p.is_file() { continue; }
                let name = match p.file_name().and_then(|s| s.to_str()) { Some(s) => s.to_string(), None => continue };
                if !name.starts_with("test_") { continue; }
                if seen.insert(name.clone()) {
                    test_bins.push(p);
                }
            }
        }
    }

    // Helper to sanitize Rust identifiers
    fn sanitize_ident(s: &str) -> String {
        let mut out = String::with_capacity(s.len());
        for ch in s.chars() {
            if ch.is_ascii_alphanumeric() { out.push(ch); } else { out.push('_'); }
        }
        if out.is_empty() { out.push_str("_" ); }
        if !out.chars().next().unwrap().is_ascii_alphabetic() { out.insert(0, 'x'); }
        out
    }

    let mut generated = String::new();
    generated.push_str("// @generated by build.rs: C++ gtest wrappers\n");
    generated.push_str("// DO NOT EDIT MANUALLY\n\n");

    let mut total_cases = 0usize;

    for bin in &test_bins {
        // List tests via --gtest_list_tests
        let output = Command::new(bin)
            .arg("--gtest_list_tests")
            .output();

        let output = match output {
            Ok(o) if o.status.success() => o,
            _ => {
                println!("cargo:warning=Skipping {} (failed to list gtests)", bin.display());
                continue;
            }
        };

        let stdout = String::from_utf8_lossy(&output.stdout);
        let mut current_suite: Option<String> = None;
        let mut cases_for_bin: Vec<(String, String)> = Vec::new();

        for line in stdout.lines() {
            let line = line.trim_end();
            if line.is_empty() { continue; }
            if !line.starts_with(' ') && line.ends_with('.') {
                // New test suite
                let suite = line.trim_end_matches('.').trim().to_string();
                current_suite = Some(suite);
            } else if line.starts_with(' ') {
                if let Some(ref suite) = current_suite {
                    // Test case under current suite
                    let case = line.trim();
                    // Handle parameterized names (which may have comments); use the part before first space
                    let case = case.split_whitespace().next().unwrap_or(case).to_string();
                    cases_for_bin.push((suite.clone(), case));
                }
            }
        }

        if cases_for_bin.is_empty() {
            println!("cargo:warning=No gtest cases found in {}", bin.display());
            continue;
        }

        let bin_str = bin.display().to_string();
        let bin_name = bin.file_name().and_then(|s| s.to_str()).unwrap_or("test_bin");
        let bin_ident = sanitize_ident(bin_name);

        for (suite, case) in cases_for_bin {
            total_cases += 1;
            let suite_ident = sanitize_ident(&suite);
            let case_ident = sanitize_ident(&case);
            let fn_name = format!("cpp__{}__{}__{}", bin_ident, suite_ident, case_ident);
            let filter = format!("{}.{}", suite, case);

            generated.push_str(&format!(
                "#[test]\n#[serial_test::serial]\n#[allow(non_snake_case)]\nfn {}() {{\n    run_gtest(\"{}\", \"{}\").expect(\"gtest failed\");\n}}\n\n",
                fn_name,
                bin_str.replace('\\', "\\\\"),
                filter.replace('\\', "\\\\")
            ));
        }
    }

    let out_file = out_dir.join("generated_cpp.rs");
    if total_cases > 0 {
        let mut f = fs::File::create(&out_file)?;
        f.write_all(generated.as_bytes())?;
        println!("cargo:info=Generated {} C++ gtest wrappers at {}", total_cases, out_file.display());
        // Expose a compile-time env var so Rust tests can skip the legacy aggregator
        println!("cargo:rustc-env=ADA_CPP_TESTS_GENERATED=1");
    } else {
        // Always write a placeholder file so include! does not fail
        let mut f = fs::File::create(&out_file)?;
        f.write_all(b"// No generated C++ gtest wrappers - check cpp_tests_status test for details\n")?;

        // Provide detailed warning to help developers understand the situation
        println!("cargo:warning=═══════════════════════════════════════════════════════════");
        println!("cargo:warning=⚠️  No C++ test wrappers generated!");
        println!("cargo:warning=");
        println!("cargo:warning=This means 'cargo test' won't run C++ tests automatically.");
        println!("cargo:warning=");
        println!("cargo:warning=Possible causes:");
        println!("cargo:warning=  • No test binaries found in predictable test directory");
        println!("cargo:warning=  • Test binaries couldn't be executed (cross-compilation?)");
        println!("cargo:warning=  • Tests aren't using Google Test framework");
        println!("cargo:warning=");
        println!("cargo:warning=You can still run tests directly or via IDE test runners.");
        println!("cargo:warning=═══════════════════════════════════════════════════════════");

        // Don't set ADA_CPP_TESTS_GENERATED so the status test can show its warning
    }

    Ok(())
}
