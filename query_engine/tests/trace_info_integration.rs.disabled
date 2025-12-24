#![allow(non_snake_case)]

use std::{fs, time::Duration};

use query_engine::handlers::trace_info::{TraceInfoHandler, TraceInfoResponse};
use query_engine::server::handler::JsonRpcHandler;
use serde_json::json;
use tempfile::TempDir;

#[tokio::test]
async fn trace_info_handler_integration__base_request__then_returns_response() {
    let temp = TempDir::new().expect("temp dir");
    let trace_id = "integration";
    let trace_dir = temp.path().join(trace_id);
    fs::create_dir_all(&trace_dir).expect("trace dir");

    let manifest = json!({
        "os": "linux",
        "arch": "x86_64",
        "pid": 1234,
        "sessionId": 42,
        "timeStartNs": 100,
        "timeEndNs": 1_100,
        "eventCount": 4,
        "bytesWritten": 2048,
        "modules": ["libfoo.so"],
    });
    fs::write(
        trace_dir.join("trace.json"),
        serde_json::to_vec(&manifest).expect("bytes"),
    )
    .expect("manifest");
    fs::write(trace_dir.join("events.bin"), &[1, 2, 3, 4, 5]).expect("events");

    let handler = TraceInfoHandler::new(temp.path().to_path_buf(), 8, Duration::from_secs(30));
    let params = json!({
        "traceId": trace_id,
        "include_checksums": true,
        "include_samples": false,
    });

    let value = JsonRpcHandler::call(&handler, Some(params))
        .await
        .expect("handler result");

    let response: TraceInfoResponse = serde_json::from_value(value).expect("decode response");
    assert_eq!(response.trace_id, trace_id);
    assert_eq!(response.duration_ns, 1_000);
    assert!(response.checksums.is_some());
    assert!(response.samples.is_none());
}
