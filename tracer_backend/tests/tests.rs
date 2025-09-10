use std::env;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

fn profile_dir() -> &'static str {
    if cfg!(debug_assertions) { "debug" } else { "release" }
}

fn workspace_root() -> PathBuf {
    if let Ok(root) = env::var("ADA_WORKSPACE_ROOT") {
        return PathBuf::from(root);
    }
    // Fallback: walk up from manifest dir to find target/
    let mut p = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    while let Some(parent) = p.parent() {
        if parent.join("target").exists() { return parent.to_path_buf(); }
        p = parent.to_path_buf();
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).parent().unwrap().to_path_buf()
}

fn test_dirs() -> Vec<PathBuf> {
    let root = workspace_root();
    let prof = env::var("ADA_BUILD_PROFILE").unwrap_or_else(|_| profile_dir().to_string());
    let mut dirs = Vec::new();
    // Main copied test dir
    let tdir = root.join("target").join(&prof).join("tracer_backend").join("test");
    if tdir.is_dir() { dirs.push(tdir); }
    // Also scan CMake out/bin directory (hash varies)
    let build_dir = root.join("target").join(&prof).join("build");
    if let Ok(entries) = fs::read_dir(&build_dir) {
        for e in entries.flatten() {
            let p = e.path().join("out").join("bin");
            if p.is_dir() { dirs.push(p); }
        }
    }
    dirs
}

fn list_cpp_tests(dir: &Path) -> io::Result<Vec<PathBuf>> {
    let mut bins = Vec::new();
    for entry in fs::read_dir(dir)? { 
        let path = entry?.path();
        if !path.is_file() { continue; }
        if let Some(name) = path.file_name().and_then(|s| s.to_str()) {
            if name.starts_with("test_") { bins.push(path.clone()); }
        }
    }
    Ok(bins)
}

fn run_test_bin(bin: &Path) -> io::Result<()> {
    let mut cmd = Command::new(bin);
    cmd.arg("--gtest_brief=1").stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
    let mut child = cmd.spawn()?;
    let output = child.wait_with_output()?;
    if !output.status.success() {
        eprintln!("FAILED: {}", bin.display());
        eprintln!("stdout:\n{}", String::from_utf8_lossy(&output.stdout));
        eprintln!("stderr:\n{}", String::from_utf8_lossy(&output.stderr));
        panic!("cpp test failed: {}", bin.display());
    }
    Ok(())
}

#[test]
fn run_cpp_gtests() {
    let dirs = test_dirs();
    assert!(!dirs.is_empty(), "No C++ test directories found");
    let mut ran = 0usize;
    for d in dirs {
        if let Ok(bins) = list_cpp_tests(&d) {
            for b in bins {
                // Skip duplicates across dirs by preferring the copied test dir
                run_test_bin(&b).expect("failed to run cpp test");
                ran += 1;
            }
        }
    }
    assert!(ran > 0, "No C++ test binaries discovered");
}
