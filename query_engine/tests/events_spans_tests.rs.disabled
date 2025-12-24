#![allow(non_snake_case)]

use std::{fs::File, io::Write, path::PathBuf};

use prost::Message;
use query_engine::{
    atf::event::{
        event::Payload, Event, FunctionCall, FunctionReturn, SignalDelivery, TraceEnd, TraceStart,
    },
    handlers::{
        events::{EventsGetHandler, EventsGetResponse},
        spans::{SpansListHandler, SpansListResponse},
    },
    server::{handler::JsonRpcHandler, JsonRpcServer},
};
use serde_json::json;
use tempfile::TempDir;

fn timestamp(ts: u64) -> prost_types::Timestamp {
    prost_types::Timestamp {
        seconds: (ts / 1_000_000_000) as i64,
        nanos: (ts % 1_000_000_000) as i32,
    }
}

fn trace_start_event(timestamp_ns: u64, thread_id: i32) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(timestamp(timestamp_ns)),
        payload: Some(Payload::TraceStart(TraceStart {
            executable_path: "/bin/demo".into(),
            args: vec!["--test".into()],
            operating_system: "linux".into(),
            cpu_architecture: "x86_64".into(),
        })),
    }
}

fn trace_end_event(timestamp_ns: u64, thread_id: i32) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(timestamp(timestamp_ns)),
        payload: Some(Payload::TraceEnd(TraceEnd { exit_code: 0 })),
    }
}

fn function_call_event(timestamp_ns: u64, thread_id: i32, symbol: &str) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(timestamp(timestamp_ns)),
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
        timestamp: Some(timestamp(timestamp_ns)),
        payload: Some(Payload::FunctionReturn(FunctionReturn {
            symbol: symbol.to_string(),
            address: 0,
            return_registers: Default::default(),
        })),
    }
}

fn signal_delivery_event(timestamp_ns: u64, thread_id: i32, name: &str) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(timestamp(timestamp_ns)),
        payload: Some(Payload::SignalDelivery(SignalDelivery {
            number: 9,
            name: name.to_string(),
            registers: Default::default(),
        })),
    }
}

fn unknown_event(timestamp_ns: u64, thread_id: i32) -> Event {
    Event {
        event_id: timestamp_ns,
        thread_id,
        timestamp: Some(timestamp(timestamp_ns)),
        payload: None,
    }
}

struct TraceFixture {
    root: TempDir,
    trace_id: String,
}

impl TraceFixture {
    fn new(trace_id: impl Into<String>) -> Self {
        let root = TempDir::new().expect("tempdir");
        let trace_id = trace_id.into();
        std::fs::create_dir_all(root.path().join(&trace_id)).expect("trace dir");
        Self { root, trace_id }
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

    fn write_manifest(&self, event_count: u64) {
        let manifest = json!({
            "os": "linux",
            "arch": "x86_64",
            "pid": 1,
            "sessionId": 1,
            "timeStartNs": 100,
            "timeEndNs": 10_000,
            "eventCount": event_count,
            "bytesWritten": 1024,
        });
        std::fs::write(
            self.manifest_path(),
            serde_json::to_vec_pretty(&manifest).expect("serialize"),
        )
        .expect("write manifest");
    }

    fn write_events(&self, events: &[Event]) {
        let mut file = File::create(self.events_path()).expect("events file");
        for event in events {
            let mut buffer = Vec::new();
            event
                .encode_length_delimited(&mut buffer)
                .expect("encode event");
            file.write_all(&buffer).expect("write event");
        }
        file.flush().expect("flush events");
    }
}

fn standard_events() -> Vec<Event> {
    vec![
        trace_start_event(100, 1),
        function_return_event(150, 3, "lonely"),
        function_call_event(200, 1, "foo"),
        function_call_event(250, 1, "bar"),
        function_return_event(300, 1, "bar"),
        function_return_event(400, 1, "foo"),
        function_call_event(450, 2, "qux"),
        function_return_event(650, 2, "qux"),
        function_call_event(700, 1, ""),
        trace_end_event(900, 1),
    ]
}

#[tokio::test]
async fn events_handler__filters_and_projection__then_returns_expected_events() {
    let fixture = TraceFixture::new("trace_events");
    let events = standard_events();
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_events",
        "filters": {
            "eventTypes": ["functionCall"],
            "threadIds": [1],
            "timeStartNs": 150,
            "timeEndNs": 450,
            "functionNames": ["foo"]
        },
        "projection": {
            "functionName": true
        },
        "orderBy": "timestamp",
        "ascending": true
    });

    let value = handler
        .call(Some(params))
        .await
        .expect("handler should succeed");

    let response: EventsGetResponse = serde_json::from_value(value).expect("decode response");
    assert_eq!(response.events.len(), 1);
    let event = &response.events[0];
    assert_eq!(event.function_name.as_deref(), Some("foo"));
    assert_eq!(event.event_type.as_deref(), Some("FunctionCall"));
    assert_eq!(event.thread_id, Some(1));
    assert_eq!(event.timestamp_ns, Some(200));
    assert_eq!(response.metadata.total_count, 1);
    assert_eq!(response.metadata.returned_count, 1);
    assert!(!response.metadata.has_more);
}

#[tokio::test]
async fn events_handler__register__then_handler_available() {
    let fixture = TraceFixture::new("trace_events_register");
    let server = JsonRpcServer::new();

    EventsGetHandler::new(fixture.trace_root()).register(&server);

    let registry = server.handler_registry();
    assert!(registry.contains("events.get"));
}

#[tokio::test]
async fn events_handler__event_type_filters__then_selects_each_variant() {
    let fixture = TraceFixture::new("trace_event_types");
    let events = vec![
        signal_delivery_event(500, 1, "SIGUSR1"),
        trace_start_event(100, 1),
        function_return_event(450, 1, "end_symbol"),
        trace_end_event(900, 1),
    ];
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let cases = [
        ("traceStart", "TraceStart"),
        ("traceEnd", "TraceEnd"),
        ("functionReturn", "FunctionReturn"),
        ("signalDelivery", "SignalDelivery"),
    ];

    for (event_type, expected_label) in cases {
        let params = json!({
            "traceId": "trace_event_types",
            "filters": {
                "eventTypes": [event_type]
            },
            "projection": {
                "eventType": true
            }
        });

        let value = handler.call(Some(params)).await.expect("handler");
        let response: EventsGetResponse = serde_json::from_value(value).expect("decode");

        assert_eq!(response.events.len(), 1, "filter: {event_type}");
        let event = &response.events[0];
        assert_eq!(event.event_type.as_deref(), Some(expected_label));
    }
}

#[tokio::test]
async fn events_handler__order_by_timestamp__then_applies_sorting() {
    let fixture = TraceFixture::new("trace_order_timestamp");
    let events = vec![
        function_call_event(500, 2, "later"),
        function_call_event(150, 2, "earlier"),
        function_call_event(300, 2, "middle"),
    ];
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_order_timestamp",
        "filters": {
            "eventTypes": ["functionCall"],
            "threadIds": [2]
        },
        "projection": {
            "functionName": true,
            "timestampNs": true
        },
        "ascending": true
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: EventsGetResponse = serde_json::from_value(value).expect("decode");

    let sequence: Vec<_> = response
        .events
        .iter()
        .map(|event| event.function_name.as_deref().expect("name"))
        .collect();
    assert_eq!(sequence, ["earlier", "middle", "later"]);
}

#[tokio::test]
async fn events_handler__projection_all_disabled__then_returns_empty_fields() {
    let fixture = TraceFixture::new("trace_projection_disabled");
    let events = vec![function_call_event(200, 3, "visible")];
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_projection_disabled",
        "filters": {
            "eventTypes": ["functionCall"]
        },
        "projection": {
            "timestampNs": false,
            "threadId": false,
            "eventType": false,
            "functionName": false
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: EventsGetResponse = serde_json::from_value(value).expect("decode");

    assert_eq!(response.events.len(), 1);
    let event = &response.events[0];
    assert!(event.timestamp_ns.is_none());
    assert!(event.thread_id.is_none());
    assert!(event.event_type.is_none());
    assert!(event.function_name.is_none());
}

#[tokio::test]
async fn events_handler__function_names_filter_missing_symbol__then_excludes_event() {
    let fixture = TraceFixture::new("trace_function_filter");
    let events = vec![
        function_call_event(120, 4, ""),
        function_return_event(220, 4, ""),
    ];
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_function_filter",
        "filters": {
            "eventTypes": ["functionCall"],
            "functionNames": ["foo"]
        },
        "projection": {
            "functionName": true
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: EventsGetResponse = serde_json::from_value(value).expect("decode");

    assert!(response.events.is_empty());
    assert_eq!(response.metadata.total_count, 0);
    assert_eq!(response.metadata.returned_count, 0);
}

#[tokio::test]
async fn events_handler__order_by_thread_descending__then_sorts_results() {
    let fixture = TraceFixture::new("trace_ordering");
    let mut events = standard_events();
    events.push(function_call_event(500, 3, "zap"));
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_ordering",
        "filters": {
            "eventTypes": ["functionCall"]
        },
        "orderBy": "threadId",
        "ascending": false,
        "limit": 2
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: EventsGetResponse = serde_json::from_value(value).expect("decode");

    assert_eq!(response.events.len(), 2);
    let thread_ids: Vec<_> = response
        .events
        .iter()
        .map(|event| event.thread_id.expect("thread"))
        .collect();
    assert!(thread_ids[0] >= thread_ids[1]);
    assert!(response.metadata.has_more);
}

#[tokio::test]
async fn events_handler__invalid_event_type__then_error() {
    let fixture = TraceFixture::new("trace_invalid_type");
    fixture.write_manifest(0);
    fixture.write_events(&[]);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_invalid_type",
        "filters": {
            "eventTypes": ["not-a-type"]
        }
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, -32602);
    assert!(err.message.contains("Invalid params"));
}

#[tokio::test]
async fn events_handler__limit_exceeds__then_error() {
    let fixture = TraceFixture::new("trace_limit");
    fixture.write_manifest(0);
    fixture.write_events(&[]);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_limit",
        "limit": 20_000
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, -32602);
}

#[tokio::test]
async fn events_handler__time_range_invalid__then_error() {
    let fixture = TraceFixture::new("trace_time");
    fixture.write_manifest(0);
    fixture.write_events(&[]);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_time",
        "filters": {
            "timeStartNs": 500,
            "timeEndNs": 100
        }
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, -32602);
    assert!(err.data.unwrap().to_string().contains("timeStartNs"));
}

#[tokio::test]
async fn events_handler__missing_trace__then_not_found() {
    let handler = EventsGetHandler::new(PathBuf::from("/tmp/does/not/exist"));
    let params = json!({
        "traceId": "missing"
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, -32000);
}

#[tokio::test]
async fn events_handler__unknown_event_type_filter__then_returns_unknown_events() {
    let fixture = TraceFixture::new("trace_unknown_events");
    let events = vec![unknown_event(1_000, 7)];
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_unknown_events",
        "filters": {
            "eventTypes": ["unknown"]
        },
        "projection": {
            "eventType": true,
            "threadId": true,
            "timestampNs": true
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: EventsGetResponse = serde_json::from_value(value).expect("decode");

    assert_eq!(response.events.len(), 1);
    let event = &response.events[0];
    assert_eq!(event.event_type.as_deref(), Some("Unknown"));
    assert_eq!(event.thread_id, Some(7));
    assert_eq!(event.timestamp_ns, Some(1_000));
}

#[tokio::test]
async fn events_handler__empty_trace_id__then_invalid_params() {
    let temp_dir = TempDir::new().expect("tempdir");
    let handler = EventsGetHandler::new(temp_dir.path().to_path_buf());

    let params = json!({
        "traceId": "   "
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected invalid params");
    assert_eq!(err.code, -32602);
    let data = err.data.expect("data").to_string();
    assert!(data.contains("traceId must not be empty"), "data: {data}");
}

#[tokio::test]
async fn events_handler__event_decode_failure__then_internal_error() {
    let fixture = TraceFixture::new("trace_decode_failure");
    fixture.write_manifest(1);
    std::fs::write(fixture.events_path(), vec![0xFF, 0xFF]).expect("write invalid bytes");

    let handler = EventsGetHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_decode_failure"
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected internal error");
    assert_eq!(err.code, -32603);
    assert_eq!(err.message, "Internal error");
    let data = err.data.expect("data").to_string();
    assert!(data.contains("failed to load trace"), "data: {data}");
}

#[tokio::test]
async fn spans_handler__include_children_false__then_filters_nested() {
    let fixture = TraceFixture::new("trace_spans_children");
    let events = standard_events();
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_children",
        "includeChildren": false,
        "projection": {
            "childCount": true,
            "depth": true,
            "threadId": true
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: SpansListResponse = serde_json::from_value(value).expect("decode");

    assert_eq!(response.spans.len(), 2);
    assert_eq!(response.spans[0].depth, Some(0));
    assert_eq!(response.spans[0].child_count, Some(1));
    assert_eq!(response.spans[1].depth, Some(0));
}

#[tokio::test]
async fn spans_handler__register__then_handler_available() {
    let fixture = TraceFixture::new("trace_spans_register");
    let server = JsonRpcServer::new();

    SpansListHandler::new(fixture.trace_root()).register(&server);

    let registry = server.handler_registry();
    assert!(registry.contains("spans.list"));
}

#[tokio::test]
async fn spans_handler__filters_by_function_and_duration__then_returns_expected() {
    let fixture = TraceFixture::new("trace_spans_filter");
    let events = standard_events();
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_filter",
        "filters": {
            "threadIds": [2],
            "functionNames": ["qux"],
            "minDurationNs": 150,
            "maxDurationNs": 500,
            "minDepth": 0,
            "maxDepth": 1
        },
        "projection": {
            "threadId": true,
            "depth": true,
            "childCount": true
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: SpansListResponse = serde_json::from_value(value).expect("decode");

    assert_eq!(response.spans.len(), 1);
    let span = &response.spans[0];
    assert_eq!(span.function_name.as_deref(), Some("qux"));
    assert_eq!(span.thread_id, Some(2));
    assert_eq!(span.duration_ns, Some(200));
    assert_eq!(span.depth, Some(0));
}

#[tokio::test]
async fn spans_handler__projection_all_disabled__then_returns_empty_fields() {
    let fixture = TraceFixture::new("trace_spans_projection_disabled");
    let events = standard_events();
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_projection_disabled",
        "projection": {
            "spanId": false,
            "functionName": false,
            "startTimeNs": false,
            "endTimeNs": false,
            "durationNs": false,
            "threadId": false,
            "depth": false,
            "childCount": false
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: SpansListResponse = serde_json::from_value(value).expect("decode");

    assert!(!response.spans.is_empty());
    let span = &response.spans[0];
    assert!(span.span_id.is_none());
    assert!(span.function_name.is_none());
    assert!(span.start_time_ns.is_none());
    assert!(span.end_time_ns.is_none());
    assert!(span.duration_ns.is_none());
    assert!(span.thread_id.is_none());
    assert!(span.depth.is_none());
    assert!(span.child_count.is_none());
}

#[tokio::test]
async fn spans_handler__filter_thresholds__then_exclude_spans() {
    let fixture = TraceFixture::new("trace_spans_thresholds");
    let events = standard_events();
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = SpansListHandler::new(fixture.trace_root());
    let cases = vec![
        (
            json!({
                "traceId": "trace_spans_thresholds",
                "filters": {
                    "minDurationNs": 300
                }
            }),
            Vec::<&str>::new(),
            "minDurationNs",
        ),
        (
            json!({
                "traceId": "trace_spans_thresholds",
                "filters": {
                    "maxDurationNs": 100
                },
                "projection": {
                    "functionName": true
                }
            }),
            vec!["bar"],
            "maxDurationNs",
        ),
        (
            json!({
                "traceId": "trace_spans_thresholds",
                "filters": {
                    "minDepth": 1
                },
                "projection": {
                    "functionName": true,
                    "depth": true
                }
            }),
            vec!["bar"],
            "minDepth",
        ),
        (
            json!({
                "traceId": "trace_spans_thresholds",
                "filters": {
                    "maxDepth": 0
                },
                "projection": {
                    "functionName": true,
                    "depth": true
                }
            }),
            vec!["foo", "qux"],
            "maxDepth",
        ),
    ];

    for (params, expected_names, label) in cases {
        let value = handler.call(Some(params)).await.expect("handler");
        let response: SpansListResponse = serde_json::from_value(value).expect("decode");

        let names: Vec<_> = response
            .spans
            .iter()
            .filter_map(|span| span.function_name.as_deref())
            .collect();

        assert_eq!(names, expected_names, "case: {label}");
    }
}

#[tokio::test]
async fn spans_handler__time_range_invalid__then_error() {
    let fixture = TraceFixture::new("trace_spans_time");
    fixture.write_manifest(0);
    fixture.write_events(&[]);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_time",
        "filters": {
            "timeStartNs": 1000,
            "timeEndNs": 100
        }
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, -32602);
}

#[tokio::test]
async fn spans_handler__limit_exceeds__then_error() {
    let fixture = TraceFixture::new("trace_spans_limit");
    fixture.write_manifest(0);
    fixture.write_events(&[]);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_limit",
        "limit": 50_000
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, -32602);
}

#[tokio::test]
async fn spans_handler__invalid_filters_type__then_error() {
    let fixture = TraceFixture::new("trace_spans_invalid_filters");
    fixture.write_manifest(0);
    fixture.write_events(&[]);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_invalid_filters",
        "filters": {
            "threadIds": "not-a-list"
        }
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected invalid params");
    assert_eq!(err.code, -32602);
    assert!(err.message.contains("Invalid params"));
}

#[tokio::test]
async fn spans_handler__missing_trace__then_not_found() {
    let handler = SpansListHandler::new(PathBuf::from("/tmp/does/not/exist"));
    let params = json!({
        "traceId": "missing"
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected error");
    assert_eq!(err.code, -32000);
}

#[tokio::test]
async fn spans_handler__pagination__then_returns_correct_slice() {
    let fixture = TraceFixture::new("trace_spans_pagination");
    let mut events = standard_events();
    events.push(function_call_event(950, 1, "late"));
    events.push(function_return_event(1_050, 1, "late"));
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_pagination",
        "offset": 1,
        "limit": 2,
        "projection": {
            "spanId": true,
            "startTimeNs": true
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: SpansListResponse = serde_json::from_value(value).expect("decode");

    assert_eq!(response.spans.len(), 2);
    assert!(response.metadata.has_more);
    assert_eq!(response.metadata.offset, 1);
    assert_eq!(response.metadata.limit, 2);
}

#[tokio::test]
async fn spans_handler__empty_trace_id__then_invalid_params() {
    let temp_dir = TempDir::new().expect("tempdir");
    let handler = SpansListHandler::new(temp_dir.path().to_path_buf());

    let params = json!({
        "traceId": "\t"
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected invalid params");
    assert_eq!(err.code, -32602);
    let data = err.data.expect("data").to_string();
    assert!(data.contains("traceId must not be empty"), "data: {data}");
}

#[tokio::test]
async fn spans_handler__min_depth_exceeds_max__then_invalid_params() {
    let fixture = TraceFixture::new("trace_spans_depth_validation");
    fixture.write_manifest(0);
    fixture.write_events(&[]);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_depth_validation",
        "filters": {
            "minDepth": 2,
            "maxDepth": 1
        }
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected invalid params");
    assert_eq!(err.code, -32602);
    let data = err.data.expect("data").to_string();
    assert!(
        data.contains("minDepth must be <= maxDepth"),
        "data: {data}"
    );
}

#[tokio::test]
async fn spans_handler__event_decode_failure__then_internal_error() {
    let fixture = TraceFixture::new("trace_spans_decode_failure");
    fixture.write_manifest(1);
    std::fs::write(fixture.events_path(), vec![0xAA]).expect("write invalid bytes");

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_decode_failure"
    });

    let err = handler
        .call(Some(params))
        .await
        .expect_err("expected internal error");
    assert_eq!(err.code, -32603);
    assert_eq!(err.message, "Internal error");
    let data = err.data.expect("data").to_string();
    assert!(data.contains("failed to load trace"), "data: {data}");
}

#[tokio::test]
async fn spans_handler__time_filters__then_returns_spans_within_window() {
    let fixture = TraceFixture::new("trace_spans_time_window");
    let events = standard_events();
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_time_window",
        "filters": {
            "timeStartNs": 250,
            "timeEndNs": 500
        },
        "projection": {
            "functionName": true,
            "startTimeNs": true,
            "endTimeNs": true
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: SpansListResponse = serde_json::from_value(value).expect("decode");

    assert_eq!(response.spans.len(), 1);
    let span = &response.spans[0];
    assert_eq!(span.function_name.as_deref(), Some("bar"));
    assert_eq!(span.start_time_ns, Some(250));
    assert_eq!(span.end_time_ns, Some(300));
}

#[tokio::test]
async fn spans_handler__function_name_absent__then_excluded_by_filter() {
    let fixture = TraceFixture::new("trace_spans_missing_function");
    let mut events = standard_events();
    events.push(function_call_event(1_100, 4, ""));
    events.push(function_return_event(1_200, 4, ""));
    fixture.write_manifest(events.len() as u64);
    fixture.write_events(&events);

    let handler = SpansListHandler::new(fixture.trace_root());
    let params = json!({
        "traceId": "trace_spans_missing_function",
        "filters": {
            "functionNames": ["qux"]
        },
        "projection": {
            "functionName": true,
            "threadId": true
        }
    });

    let value = handler.call(Some(params)).await.expect("handler");
    let response: SpansListResponse = serde_json::from_value(value).expect("decode");

    assert_eq!(response.spans.len(), 1);
    let span = &response.spans[0];
    assert_eq!(span.function_name.as_deref(), Some("qux"));
    assert_eq!(span.thread_id, Some(2));
}
