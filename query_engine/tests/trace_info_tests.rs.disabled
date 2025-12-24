#![allow(non_snake_case)]

use std::{
    fs::{self, File},
    io::{self, Write},
    path::PathBuf,
    time::{Duration, Instant},
};

#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;

use prost::Message;
use query_engine::{
    atf::{
        event::{event::Payload, Event, FunctionCall, FunctionReturn, TraceEnd, TraceStart},
        AtfError, AtfReader,
    },
    handlers::TraceInfoHandler,
    server::{handler::JsonRpcHandler, JsonRpcServer, JsonRpcServerConfig},
};
use serde_json::{json, Value};
use tempfile::TempDir;
use tokio::time::sleep;

use query_engine::handlers::trace_info::TraceInfoResponse;
use query_engine::server::types::JsonRpcError;

struct TraceFixture {
    root: TempDir,
    trace_id: String,
}

impl TraceFixture {
    fn new(trace_id: impl Into<String>) -> io::Result<Self> {
        let root = TempDir::new()?;
        let trace_id = trace_id.into();
        fs::create_dir_all(root.path().join(&trace_id))?;
        Ok(Self { root, trace_id })
    }

    fn trace_root(&self) -> PathBuf {
        self.root.path().to_path_buf()
    }

    fn trace_dir(&self) -> PathBuf {
        self.root.path().join(&self.trace_id)
    }

    fn manifest_path(&self) -> PathBuf {
        self.trace_dir().join("trace.json")
    }

    fn events_path(&self) -> PathBuf {
        self.trace_dir().join("events.bin")
    }

    fn write_manifest(&self, manifest: Value) -> io::Result<()> {
        let bytes = serde_json::to_vec_pretty(&manifest)?;
        fs::write(self.manifest_path(), bytes)
    }

    fn write_events(&self, events: &[Event]) -> io::Result<()> {
        let mut file = File::create(self.events_path())?;
        for event in events {
            let mut buffer = Vec::new();
            event
                .encode_length_delimited(&mut buffer)
                .expect("encode event");
            file.write_all(&buffer)?;
        }
        file.flush()
    }
}

fn sample_manifest(event_count: u64, span_count: Option<u64>) -> Value {
    json!({
        "os": "linux",
        "arch": "x86_64",
        "pid": 4242,
        "sessionId": 1,
        "timeStartNs": 100,
        "timeEndNs": 2100,
        "eventCount": event_count,
        "bytesWritten": 4096,
        "modules": ["test_mod"],
        "spanCount": span_count,
    })
}

fn function_call_event(timestamp_ns: u64, thread_id: i32, symbol: &str) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(prost_types::Timestamp {
            seconds: (timestamp_ns / 1_000_000_000) as i64,
            nanos: (timestamp_ns % 1_000_000_000) as i32,
        }),
        payload: Some(Payload::FunctionCall(FunctionCall {
            symbol: symbol.to_string(),
            address: 0,
            argument_registers: Default::default(),
            stack_shallow_copy: Vec::new(),
        })),
    }
}

fn function_return_event(timestamp_ns: u64, thread_id: i32, symbol: &str) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(prost_types::Timestamp {
            seconds: (timestamp_ns / 1_000_000_000) as i64,
            nanos: (timestamp_ns % 1_000_000_000) as i32,
        }),
        payload: Some(Payload::FunctionReturn(FunctionReturn {
            symbol: symbol.to_string(),
            address: 0,
            return_registers: Default::default(),
        })),
    }
}

fn trace_start_event(timestamp_ns: u64, thread_id: i32) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(prost_types::Timestamp {
            seconds: (timestamp_ns / 1_000_000_000) as i64,
            nanos: (timestamp_ns % 1_000_000_000) as i32,
        }),
        payload: Some(Payload::TraceStart(TraceStart {
            executable_path: "/bin/test".into(),
            args: vec!["--flag".into()],
            operating_system: "linux".into(),
            cpu_architecture: "x86_64".into(),
        })),
    }
}

fn trace_end_event(timestamp_ns: u64, thread_id: i32) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(prost_types::Timestamp {
            seconds: (timestamp_ns / 1_000_000_000) as i64,
            nanos: (timestamp_ns % 1_000_000_000) as i32,
        }),
        payload: Some(Payload::TraceEnd(TraceEnd { exit_code: 0 })),
    }
}

#[tokio::test]
async fn atf_reader__valid_manifest__then_parsed() {
    let fixture = TraceFixture::new("traceA").expect("fixture");
    let events = vec![
        trace_start_event(100, 1),
        function_call_event(200, 1, "foo"),
        function_return_event(300, 1, "foo"),
        trace_end_event(400, 1),
    ];
    fixture
        .write_manifest(sample_manifest(events.len() as u64, Some(2)))
        .expect("manifest");
    fixture.write_events(&events).expect("events");

    let reader = AtfReader::open(fixture.trace_dir()).expect("reader");
    let manifest = reader.manifest();

    assert_eq!(manifest.os, "linux");
    assert_eq!(manifest.arch, "x86_64");
    assert_eq!(manifest.event_count, 4);
    assert_eq!(manifest.resolved_span_count(), 2);
    assert_eq!(manifest.duration_ns(), 2000);

    let parsed_events = reader.load_all_events().expect("events parsed");
    assert_eq!(parsed_events.len(), 4);
}

#[tokio::test]
async fn atf_reader__missing_manifest__then_error() {
    let fixture = TraceFixture::new("traceB").expect("fixture");
    let err = AtfReader::open(fixture.trace_dir()).expect_err("expected error");
    match err {
        AtfError::ManifestNotFound(path) => assert!(path.ends_with("trace.json")),
        other => panic!("unexpected error variant: {:?}", other),
    }
}

#[tokio::test]
async fn trace_info_handler__base_request__then_returns_metadata() {
    let fixture = TraceFixture::new("traceC").expect("fixture");
    let events = vec![
        function_call_event(100, 1, "foo"),
        function_return_event(200, 1, "foo"),
    ];
    fixture
        .write_manifest(sample_manifest(events.len() as u64, Some(1)))
        .expect("manifest");
    fixture.write_events(&events).expect("events");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 4, Duration::from_secs(60));
    let response_value = handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect("handler response");

    let response: TraceInfoResponse = serde_json::from_value(response_value).expect("deserialize");
    assert_eq!(response.trace_id, fixture.trace_id);
    assert_eq!(response.event_count, 2);
    assert_eq!(response.span_count, 1);
    assert_eq!(response.duration_ns, 2000);
    assert!(response.checksums.is_none());
    assert!(response.samples.is_none());
}

#[tokio::test]
async fn trace_info_handler__include_checksums_and_samples__then_returns_optional_fields() {
    let fixture = TraceFixture::new("traceD").expect("fixture");
    let events = vec![
        function_call_event(100, 1, "foo"),
        function_return_event(200, 1, "foo"),
        function_call_event(300, 2, "bar"),
        function_return_event(400, 2, "bar"),
    ];
    fixture
        .write_manifest(sample_manifest(events.len() as u64, Some(2)))
        .expect("manifest");
    fixture.write_events(&events).expect("events");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 4, Duration::from_secs(60));
    let params = json!({
        "traceId": fixture.trace_id,
        "include_checksums": true,
        "include_samples": true,
    });
    let response_value = handler.call(Some(params)).await.expect("response");
    let response: TraceInfoResponse = serde_json::from_value(response_value).expect("deserialize");

    let checksums = response.checksums.expect("checksums present");
    assert_eq!(checksums.manifest_md5.len(), 32);
    assert_eq!(checksums.events_md5.len(), 32);

    let samples = response.samples.expect("samples present");
    assert!(!samples.first_events.is_empty());
    assert!(!samples.last_events.is_empty());
}

#[tokio::test]
async fn trace_info_handler__cache_hit__then_latency_under_target() {
    let fixture = TraceFixture::new("traceE").expect("fixture");
    let events = vec![function_call_event(100, 1, "foo")];
    fixture
        .write_manifest(sample_manifest(events.len() as u64, Some(0)))
        .expect("manifest");
    fixture.write_events(&events).expect("events");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 1, Duration::from_secs(60));
    handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect("priming call");

    let start = Instant::now();
    handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect("cached call");
    let elapsed = start.elapsed();

    assert!(elapsed < Duration::from_millis(50));
}

#[tokio::test]
async fn trace_info_handler__ttl_expired__then_reloads_manifest() {
    let fixture = TraceFixture::new("traceF").expect("fixture");
    fixture
        .write_manifest(sample_manifest(2, Some(1)))
        .expect("manifest");
    fixture
        .write_events(&[
            function_call_event(100, 1, "foo"),
            function_return_event(200, 1, "foo"),
        ])
        .expect("events");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_millis(10));
    handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect("first call");

    sleep(Duration::from_millis(15)).await;

    fixture
        .write_manifest(sample_manifest(10, Some(5)))
        .expect("manifest update");

    let response_value = handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect("reloaded");
    let response: TraceInfoResponse = serde_json::from_value(response_value).expect("deserialize");
    assert_eq!(response.event_count, 10);
    assert_eq!(response.span_count, 5);
}

#[tokio::test]
async fn trace_info_handler__missing_trace__then_error() {
    let fixture = TraceFixture::new("traceG").expect("fixture");
    let handler = TraceInfoHandler::new(fixture.trace_root(), 1, Duration::from_secs(60));
    let err = handler
        .call(Some(json!({"traceId": "unknown"})))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, JsonRpcError::trace_not_found().code);
}

#[tokio::test]
async fn trace_info_handler__include_samples_without_events__then_returns_empty_samples() {
    let fixture = TraceFixture::new("traceH").expect("fixture");
    fixture
        .write_manifest(sample_manifest(0, Some(0)))
        .expect("manifest");
    fixture.write_events(&[]).expect("empty events");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));
    let value = handler
        .call(Some(json!({
            "traceId": fixture.trace_id,
            "include_samples": true
        })))
        .await
        .expect("response");
    let response: TraceInfoResponse = serde_json::from_value(value).expect("deserialize");
    let samples = response.samples.expect("samples present");
    assert!(samples.first_events.is_empty());
    assert!(samples.last_events.is_empty());
}

#[tokio::test]
async fn trace_info_handler__checksums_missing_events__then_internal_error() {
    let fixture = TraceFixture::new("traceI").expect("fixture");
    fixture
        .write_manifest(sample_manifest(0, Some(0)))
        .expect("manifest");
    fs::create_dir(fixture.events_path()).expect("events directory");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));
    let err = handler
        .call(Some(json!({
            "traceId": fixture.trace_id,
            "include_checksums": true
        })))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, -32603);
}

#[tokio::test]
async fn json_rpc_server__trace_info_handler_registered__then_serves_request() {
    let fixture = TraceFixture::new("traceJ").expect("fixture");
    let events = vec![function_call_event(100, 1, "foo")];
    fixture
        .write_manifest(sample_manifest(events.len() as u64, Some(0)))
        .expect("manifest");
    fixture.write_events(&events).expect("events");

    let server = JsonRpcServer::with_config(JsonRpcServerConfig::default());
    TraceInfoHandler::new(fixture.trace_root(), 4, Duration::from_secs(60)).register(&server);

    let registry = server.handler_registry();
    let response = registry
        .call(
            "trace.info",
            Some(json!({
                "traceId": fixture.trace_id,
                "include_samples": false
            })),
        )
        .await
        .expect("rpc response");

    let parsed: TraceInfoResponse = serde_json::from_value(response).expect("deserialize");
    assert_eq!(parsed.event_count, 1);
}

#[tokio::test]
async fn trace_info_handler__cache_disabled__then_reloads_after_manifest_change() {
    let fixture = TraceFixture::new("traceK").expect("fixture");
    fixture
        .write_manifest(sample_manifest(1, Some(0)))
        .expect("manifest");
    fixture
        .write_events(&[function_call_event(100, 1, "foo")])
        .expect("events");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 0, Duration::from_secs(120));

    let first: TraceInfoResponse = serde_json::from_value(
        handler
            .call(Some(json!({"traceId": fixture.trace_id.clone()})))
            .await
            .expect("first response"),
    )
    .expect("deserialize");
    assert_eq!(first.event_count, 1);

    sleep(Duration::from_millis(20)).await;
    fixture
        .write_manifest(sample_manifest(4, Some(2)))
        .expect("manifest update");

    let second: TraceInfoResponse = serde_json::from_value(
        handler
            .call(Some(json!({"traceId": fixture.trace_id.clone()})))
            .await
            .expect("second response"),
    )
    .expect("deserialize");
    assert_eq!(second.event_count, 4);
    assert_eq!(second.span_count, 2);
}

#[tokio::test]
async fn trace_info_handler__blank_trace_id__then_invalid_params() {
    let fixture = TraceFixture::new("traceL").expect("fixture");
    let handler = TraceInfoHandler::new(fixture.trace_root(), 4, Duration::from_secs(60));

    let err = handler
        .call(Some(json!({"traceId": "   "})))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, JsonRpcError::invalid_params("_").code);
    assert_eq!(err.message, "Invalid params");
    let detail = err
        .data
        .and_then(|value| value.as_str().map(|s| s.to_string()))
        .expect("detail");
    assert!(detail.contains("traceId must not be empty"));
}

#[tokio::test]
async fn trace_info_handler__invalid_params_type__then_invalid_params() {
    let fixture = TraceFixture::new("traceM").expect("fixture");
    let handler = TraceInfoHandler::new(fixture.trace_root(), 4, Duration::from_secs(60));

    let err = handler
        .call(Some(json!({"traceId": 42})))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, JsonRpcError::invalid_params("_").code);
    assert_eq!(err.message, "Invalid params");
    let detail = err
        .data
        .and_then(|value| value.as_str().map(|s| s.to_string()))
        .expect("detail");
    assert!(detail.contains("invalid trace.info parameters"));
}

#[tokio::test]
async fn trace_info_handler__missing_manifest_metadata__then_trace_not_found() {
    let fixture = TraceFixture::new("traceN").expect("fixture");
    let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));

    let err = handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, JsonRpcError::trace_not_found().code);
}

#[tokio::test]
async fn trace_info_handler__missing_events_metadata__then_trace_not_found() {
    let fixture = TraceFixture::new("traceO").expect("fixture");
    fixture
        .write_manifest(sample_manifest(0, Some(0)))
        .expect("manifest");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));
    let err = handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, JsonRpcError::trace_not_found().code);
}

#[cfg(unix)]
#[tokio::test]
async fn trace_info_handler__metadata_permission_denied__then_internal_error() {
    let fixture = TraceFixture::new("traceP").expect("fixture");
    fixture
        .write_manifest(sample_manifest(1, Some(0)))
        .expect("manifest");
    fixture
        .write_events(&[function_call_event(100, 1, "foo")])
        .expect("events");

    let trace_dir = fixture.trace_dir();
    fs::set_permissions(&trace_dir, fs::Permissions::from_mode(0o000)).expect("chmod");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));
    let err = handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, JsonRpcError::internal("_").code);
    assert_eq!(err.message, "Internal error");
    let detail = err
        .data
        .and_then(|value| value.as_str().map(|s| s.to_string()))
        .expect("detail");
    assert!(detail.contains("failed to read manifest metadata"));

    fs::set_permissions(&trace_dir, fs::Permissions::from_mode(0o700)).expect("restore perms");
}

#[tokio::test]
async fn trace_info_handler__invalid_manifest_payload__then_internal_error() {
    let fixture = TraceFixture::new("traceQ").expect("fixture");
    fs::write(fixture.manifest_path(), b"").expect("invalid manifest");
    fixture
        .write_events(&[function_call_event(100, 1, "foo")])
        .expect("events");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));
    let err = handler
        .call(Some(json!({"traceId": fixture.trace_id.clone()})))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, JsonRpcError::internal("_").code);
    assert_eq!(err.message, "Internal error");
    let detail = err
        .data
        .and_then(|value| value.as_str().map(|s| s.to_string()))
        .expect("detail");
    assert!(detail.contains("failed to parse manifest"));
}

#[tokio::test]
async fn trace_info_handler__invalid_event_payload__then_decode_error() {
    let fixture = TraceFixture::new("traceR").expect("fixture");
    fixture
        .write_manifest(sample_manifest(1, Some(0)))
        .expect("manifest");
    fs::write(fixture.events_path(), [0xAA, 0xBB, 0xCC]).expect("invalid events");

    let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));
    let err = handler
        .call(Some(json!({
            "traceId": fixture.trace_id.clone(),
            "include_samples": true
        })))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, JsonRpcError::internal("_").code);
    assert_eq!(err.message, "Internal error");
    let detail = err
        .data
        .and_then(|value| value.as_str().map(|s| s.to_string()))
        .expect("detail");
    assert!(detail.contains("failed to decode events"));
}
