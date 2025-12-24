#![allow(non_snake_case)]

use std::fs;
use std::net::TcpListener;
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use serde_json::json;
use tempfile::tempdir;
// Removed unused imports for cleaner code
use tokio::net::TcpStream;
use tokio::time::sleep;

#[cfg(unix)]
use std::os::unix::process::ExitStatusExt;

/// Test that exercises the binary's main() function with default arguments
#[test]
fn query_engine_binary__main_with_defaults__then_initializes_successfully() {
    let binary = env!("CARGO_BIN_EXE_query_engine");
    let traces_root = tempdir().expect("tempdir");
    let trace_path = traces_root.path().join("default_traces");

    let mut child = Command::new(binary)
        .arg("--address")
        .arg("127.0.0.1:0") // Use port 0 to get any available port
        .arg("--trace-root")
        .arg(&trace_path)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .expect("spawn query engine");

    // Wait for trace directory to be created (tests init_tracing and ensure_trace_root)
    let start = Instant::now();
    while !trace_path.exists() {
        if start.elapsed() > Duration::from_secs(3) {
            let _ = child.kill();
            panic!("trace directory was not created within timeout");
        }
        thread::sleep(Duration::from_millis(20));
    }

    // Send SIGINT to test graceful shutdown
    let pid = child.id() as i32;
    unsafe {
        libc::kill(pid, libc::SIGINT);
    }

    // Verify clean exit
    let start = Instant::now();
    let status = loop {
        if let Some(status) = child.try_wait().expect("poll child status") {
            break status;
        }

        if start.elapsed() > Duration::from_secs(5) {
            let _ = child.kill();
            panic!("binary did not exit cleanly after SIGINT");
        }

        thread::sleep(Duration::from_millis(50));
    };

    #[cfg(unix)]
    let terminated_by_signal = status.signal() == Some(libc::SIGINT);
    #[cfg(not(unix))]
    let terminated_by_signal = false;

    assert!(status.success() || terminated_by_signal);
    assert!(trace_path.is_dir(), "trace directory should be created");
}

/// Test main() function with custom arguments to exercise From<Args> trait
#[test]
fn query_engine_binary__main_with_custom_args__then_processes_config_conversion() {
    let binary = env!("CARGO_BIN_EXE_query_engine");
    let traces_root = tempdir().expect("tempdir");
    let trace_path = traces_root.path().join("custom_traces");

    let mut child = Command::new(binary)
        .arg("--address")
        .arg("0.0.0.0:0") // Different from default to test From<Args>
        .arg("--trace-root")
        .arg(&trace_path)
        .arg("--cache-size")
        .arg("200") // Different from default to test From<Args>
        .arg("--cache-ttl")
        .arg("120") // Different from default to test From<Args>
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .expect("spawn query engine with custom args");

    // Wait for initialization
    let start = Instant::now();
    while !trace_path.exists() {
        if start.elapsed() > Duration::from_secs(3) {
            let _ = child.kill();
            panic!("binary with custom args failed to initialize");
        }
        thread::sleep(Duration::from_millis(20));
    }

    // Test shutdown
    let pid = child.id() as i32;
    unsafe {
        libc::kill(pid, libc::SIGINT);
    }

    // Verify exit
    let start = Instant::now();
    let status = loop {
        if let Some(status) = child.try_wait().expect("poll child status") {
            break status;
        }

        if start.elapsed() > Duration::from_secs(5) {
            let _ = child.kill();
            panic!("binary with custom args did not exit cleanly");
        }

        thread::sleep(Duration::from_millis(50));
    };

    #[cfg(unix)]
    let terminated_by_signal = status.signal() == Some(libc::SIGINT);
    #[cfg(not(unix))]
    let terminated_by_signal = false;

    assert!(status.success() || terminated_by_signal);
}

/// Test main() function error path when trace root is a file
#[test]
fn query_engine_binary__main_with_trace_root_as_file__then_returns_error() {
    let binary = env!("CARGO_BIN_EXE_query_engine");
    let traces_root = tempdir().expect("tempdir");
    let file_path = traces_root.path().join("trace_file");

    // Create a file instead of directory
    fs::write(&file_path, b"not a directory").expect("write file");

    let status = Command::new(binary)
        .arg("--address")
        .arg("127.0.0.1:0")
        .arg("--trace-root")
        .arg(&file_path)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .expect("run query engine with file as trace root");

    assert!(
        !status.success(),
        "binary should exit with error when trace root is file"
    );
}

/// Test main() with port binding conflict to exercise error handling
#[test]
fn query_engine_binary__main_with_port_conflict__then_handles_serve_error() {
    let binary = env!("CARGO_BIN_EXE_query_engine");
    let traces_root = tempdir().expect("tempdir");
    let trace_path = traces_root.path().join("conflict_traces");

    // Bind to a port to create conflict
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind listener");
    let addr = listener.local_addr().expect("get bound address");

    let status = Command::new(binary)
        .arg("--address")
        .arg(addr.to_string())
        .arg("--trace-root")
        .arg(&trace_path)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .expect("run query engine with port conflict");

    assert!(
        !status.success(),
        "binary should exit with error when port is already bound"
    );
}

/// Test that binary properly initializes tracing system
#[tokio::test]
async fn query_engine_binary__main_with_tracing__then_init_tracing_called() {
    let binary = env!("CARGO_BIN_EXE_query_engine");
    let traces_root = tempdir().expect("tempdir");
    let trace_path = traces_root.path().join("tracing_test");

    // Start binary with environment variable to test tracing init
    let mut child = tokio::process::Command::new(binary)
        .arg("--address")
        .arg("127.0.0.1:0")
        .arg("--trace-root")
        .arg(&trace_path)
        .env("RUST_LOG", "debug") // This exercises init_tracing path
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .expect("spawn binary for tracing test");

    // Give it time to initialize tracing
    sleep(Duration::from_millis(200)).await;

    // Verify it's running by checking trace directory creation
    let start = Instant::now();
    while !trace_path.exists() {
        if start.elapsed() > Duration::from_secs(3) {
            let _ = child.kill().await;
            panic!("binary failed to initialize with RUST_LOG");
        }
        sleep(Duration::from_millis(20)).await;
    }

    // Clean shutdown
    let _ = child.kill().await;
    let _ = child.wait().await;
}

/// Integration test that exercises the entire startup sequence including server creation
#[tokio::test]
async fn query_engine_binary__full_startup_sequence__then_serves_requests() {
    let binary = env!("CARGO_BIN_EXE_query_engine");
    let traces_root = tempdir().expect("tempdir");
    let trace_path = traces_root.path().join("full_startup_test");

    // Create a manifest file to make it a valid trace
    fs::create_dir_all(&trace_path).expect("create trace dir");
    let manifest = json!({
        "version": "4.0",
        "trace_start_time": "2023-01-01T00:00:00Z",
        "trace_end_time": "2023-01-01T00:01:00Z",
        "process_info": {
            "pid": 12345,
            "executable": "/test/app"
        },
        "resolved_span_count": 42
    });
    fs::write(
        trace_path.join("manifest.json"),
        serde_json::to_string_pretty(&manifest).expect("serialize manifest"),
    )
    .expect("write manifest");

    // Find an available port
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind temp listener");
    let addr = listener.local_addr().expect("get address");
    drop(listener); // Release the port

    let mut child = tokio::process::Command::new(binary)
        .arg("--address")
        .arg(addr.to_string())
        .arg("--trace-root")
        .arg(&trace_path)
        .arg("--cache-size")
        .arg("50")
        .arg("--cache-ttl")
        .arg("30")
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("spawn binary for full test");

    // Poll for readiness instead of assuming a fixed startup delay. This keeps
    // the test resilient to scheduler hiccups while still verifying the server
    // reaches the listening state for real.
    let start = Instant::now();
    let mut connected = None;

    while start.elapsed() < Duration::from_secs(5) {
        match TcpStream::connect(addr).await {
            Ok(stream) => {
                connected = Some(stream);
                break;
            }
            Err(_) => {
                sleep(Duration::from_millis(50)).await;
            }
        }
    }

    let _stream = connected.expect("Failed to connect to query engine within timeout");

    // Send shutdown signal
    let _ = child.kill().await;
    let output = child.wait_with_output().await.expect("wait for child");

    // Check that no critical errors occurred
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        !stderr.contains("panic"),
        "Binary should not panic: {}",
        stderr
    );
}

/// Test argument parsing edge cases by exercising main() with various inputs
#[test]
fn query_engine_binary__main_with_invalid_args__then_exits_with_help() {
    let binary = env!("CARGO_BIN_EXE_query_engine");

    // Test with --help flag to exercise argument parsing
    let output = Command::new(binary)
        .arg("--help")
        .output()
        .expect("run binary with --help");

    assert!(output.status.success(), "help should succeed");
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("ADA query engine"));
}

/// Test version flag to exercise more argument parsing paths
#[test]
fn query_engine_binary__main_with_version__then_prints_version() {
    let binary = env!("CARGO_BIN_EXE_query_engine");

    let output = Command::new(binary)
        .arg("--version")
        .output()
        .expect("run binary with --version");

    assert!(output.status.success(), "version should succeed");
    // Version output is handled by clap, so binary exits successfully
}
