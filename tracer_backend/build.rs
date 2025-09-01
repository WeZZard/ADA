use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rerun-if-changed=include/");
    println!("cargo:rerun-if-changed=CMakeLists.txt");
    
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
        println!("cargo:warning=Coverage instrumentation enabled for C/C++ code");
        cmake_config.define("ENABLE_COVERAGE", "ON");
        // Don't set LLVM_PROFILE_FILE here - it should be set at runtime when tests execute
    }
    
    // Enable C++ registry if feature is set
    
    let dst = cmake_config.build();
    
    // Report the location of compile_commands.json for IDE consumption
    let compile_commands_path = dst.join("build").join("compile_commands.json");
    if compile_commands_path.exists() {
        println!("cargo:warning=compile_commands.json generated at: {}", compile_commands_path.display());
        
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
                println!("cargo:warning=compile_commands.json symlinked to: {}", link_path.display());
            }
        }
        
        #[cfg(windows)]
        {
            if let Err(e) = std::os::windows::fs::symlink_file(&compile_commands_path, &link_path) {
                println!("cargo:warning=Failed to create compile_commands.json symlink: {}", e);
            } else {
                println!("cargo:warning=compile_commands.json symlinked to: {}", link_path.display());
            }
        }
    }
    
    // Link the libraries we built
    println!("cargo:rustc-link-search=native={}/lib", dst.display());
    println!("cargo:rustc-link-lib=static=tracer_controller");
    println!("cargo:rustc-link-lib=static=tracer_utils");
    
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
        ("bin/test_cli", "test/test_cli"),
        ("bin/test_runloop", "test/test_runloop"),
        // Google Test executables (multiple possible source locations)
        ("build/test_ring_buffer", "test/test_ring_buffer"),
        ("build/tests/test_ring_buffer", "test/test_ring_buffer"),
        ("out/bin/test_ring_buffer", "test/test_ring_buffer"),
        ("build/test_ring_buffer_attach", "test/test_ring_buffer_attach"),
        ("build/tests/test_ring_buffer_attach", "test/test_ring_buffer_attach"),
        ("out/bin/test_ring_buffer_attach", "test/test_ring_buffer_attach"),
        ("build/test_shared_memory", "test/test_shared_memory"),
        ("build/tests/test_shared_memory", "test/test_shared_memory"),
        ("out/bin/test_shared_memory", "test/test_shared_memory"),
        ("build/test_spawn_method", "test/test_spawn_method"),
        ("build/tests/test_spawn_method", "test/test_spawn_method"),
        ("out/bin/test_spawn_method", "test/test_spawn_method"),
        ("build/test_thread_registry", "test/test_thread_registry"),
        ("build/tests/test_thread_registry", "test/test_thread_registry"),
        ("out/bin/test_thread_registry", "test/test_thread_registry"),
        ("build/test_thread_registry_cpp", "test/test_thread_registry_cpp"),
        ("build/tests/test_thread_registry_cpp", "test/test_thread_registry_cpp"),
        ("out/bin/test_thread_registry_cpp", "test/test_thread_registry_cpp"),
        ("build/test_thread_registry_integration", "test/test_thread_registry_integration"),
        ("build/tests/test_thread_registry_integration", "test/test_thread_registry_integration"),
        ("out/bin/test_thread_registry_integration", "test/test_thread_registry_integration"),
        ("build/test_controller_full_lifecycle", "test/test_controller_full_lifecycle"),
        ("build/tests/test_controller_full_lifecycle", "test/test_controller_full_lifecycle"),
        ("out/bin/test_controller_full_lifecycle", "test/test_controller_full_lifecycle"),
        ("build/test_integration", "test/test_integration"),
        ("build/tests/test_integration", "test/test_integration"),
        ("out/bin/test_integration", "test/test_integration"),
        ("build/test_agent_loader", "test/test_agent_loader"),
        ("build/tests/test_agent_loader", "test/test_agent_loader"),
        ("out/bin/test_agent_loader", "test/test_agent_loader"),
        ("build/test_baseline_hooks", "test/test_baseline_hooks"),
        ("build/tests/test_baseline_hooks", "test/test_baseline_hooks"),
        ("out/bin/test_baseline_hooks", "test/test_baseline_hooks"),
    ];
    
    for (src_path, dst_path) in binaries {
        let src = dst.join(src_path);
        let dst_file = predictable_dir.join(dst_path);
        if src.exists() {
            if let Err(e) = std::fs::copy(&src, &dst_file) {
                println!("cargo:warning=Failed to copy {}: {}", src_path, e);
            } else {
                println!("cargo:warning=Copied {} to {}", src_path, dst_file.display());
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
                        println!("cargo:warning=Copied {} to {}", path.display(), dst_file.display());
                    }
                }
            }
        }
    }
    
    // Report the predictable directory location
    println!("cargo:warning=");
    println!("cargo:warning=All binaries and libraries copied to: {}", predictable_dir.display());
    println!("cargo:warning=  Binaries:  {}/bin/", predictable_dir.display());
    println!("cargo:warning=  Tests:     {}/test/", predictable_dir.display());
    println!("cargo:warning=  Libraries: {}/lib/", predictable_dir.display());
    
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