#![cfg(unix)]
#![allow(non_snake_case)]

use std::net::TcpListener;
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use tempfile::tempdir;

#[test]
fn query_engine_binary__sigint_shutdown__then_exits_cleanly() {
    let binary = env!("CARGO_BIN_EXE_query_engine");

    let traces_root = tempdir().expect("tempdir");
    let trace_path = traces_root.path().join("generated_traces");

    let mut child = Command::new(binary)
        .arg("--address")
        .arg("127.0.0.1:0")
        .arg("--trace-root")
        .arg(&trace_path)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .expect("spawn query engine");

    let start = Instant::now();
    while !trace_path.exists() {
        if start.elapsed() > Duration::from_secs(2) {
            let _ = child.kill();
            panic!("trace directory was not prepared before timeout");
        }
        thread::sleep(Duration::from_millis(20));
    }

    let pid = child.id() as i32;
    unsafe {
        libc::kill(pid, libc::SIGINT);
    }

    let start = Instant::now();
    let status = loop {
        if let Some(status) = child.try_wait().expect("poll child status") {
            break status;
        }

        if start.elapsed() > Duration::from_secs(5) {
            let _ = child.kill();
            panic!("query engine did not exit after SIGINT");
        }

        thread::sleep(Duration::from_millis(50));
    };

    assert!(status.success(), "process exit status: {status:?}");
    assert!(trace_path.is_dir(), "trace directory was not created");
}

#[test]
fn query_engine_binary__address_in_use__then_reports_error() {
    let binary = env!("CARGO_BIN_EXE_query_engine");

    let listener = TcpListener::bind("127.0.0.1:0").expect("bind listener");
    let address = listener.local_addr().expect("local addr");

    let traces_root = tempdir().expect("tempdir");
    let trace_path = traces_root.path().join("generated_traces");

    let status = Command::new(binary)
        .arg("--address")
        .arg(address.to_string())
        .arg("--trace-root")
        .arg(&trace_path)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .expect("run query engine");

    assert!(
        !status.success(),
        "process should exit with error when port is unavailable"
    );
}
