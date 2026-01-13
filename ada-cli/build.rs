//! Build script for ada-cli
//!
//! Links against the symbol_resolver library from tracer_backend.

use std::env;
use std::path::PathBuf;

fn main() {
    let profile = env::var("PROFILE").expect("PROFILE not set");

    // Find the workspace root
    let workspace_root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .parent()
        .unwrap()
        .to_path_buf();

    // Find the symbol_resolver library in the tracer_backend build output
    // The library is built by CMake and ends up in the build directory
    let target_dir = workspace_root.join("target").join(&profile);
    let build_dir = target_dir.join("build");

    // Search for libsymbol_resolver.a in the build directory
    if let Some(lib_path) = find_library(&build_dir, "libsymbol_resolver.a") {
        let lib_dir = lib_path.parent().unwrap();
        println!("cargo:rustc-link-search=native={}", lib_dir.display());
        println!("cargo:warning=Found symbol_resolver at: {}", lib_path.display());
    } else {
        panic!(
            "libsymbol_resolver.a not found under {}; \
             build tracer_backend first (ada-cli has a build-dependency on it)",
            build_dir.display()
        );
    }

    // Link against symbol_resolver
    println!("cargo:rustc-link-lib=static=symbol_resolver");

    // On macOS, link against CoreFoundation for dSYM discovery
    #[cfg(target_os = "macos")]
    {
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
    }

    // Link against C++ standard library
    #[cfg(target_os = "macos")]
    {
        println!("cargo:rustc-link-lib=c++");
    }
    #[cfg(target_os = "linux")]
    {
        println!("cargo:rustc-link-lib=stdc++");
    }

    // Rebuild if the library changes
    println!("cargo:rerun-if-changed=build.rs");
}

/// Recursively search for a library file
fn find_library(dir: &PathBuf, name: &str) -> Option<PathBuf> {
    if !dir.exists() {
        return None;
    }

    for entry in walkdir(dir) {
        if entry.file_name().map(|n| n.to_str() == Some(name)).unwrap_or(false) {
            return Some(entry);
        }
    }

    None
}

/// Simple recursive directory walker
fn walkdir(dir: &PathBuf) -> Vec<PathBuf> {
    let mut results = Vec::new();

    if let Ok(entries) = std::fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                results.extend(walkdir(&path));
            } else {
                results.push(path);
            }
        }
    }

    results
}
