use std::{path::PathBuf, time::Instant};

use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::{
    atf::{AtfError, AtfReader, ParsedEvent, ParsedEventKind},
    server::{
        handler::{JsonRpcHandler, JsonRpcResult},
        types::JsonRpcError,
    },
};

const DEFAULT_LIMIT: u64 = 1000;
const MAX_LIMIT: u64 = 10_000;

fn default_limit() -> u64 {
    DEFAULT_LIMIT
}

fn default_true() -> bool {
    true
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EventsGetParams {
    #[serde(rename = "traceId")]
    pub trace_id: String,
    #[serde(default)]
    pub filters: EventFilters,
    #[serde(default)]
    pub projection: EventProjection,
    #[serde(default)]
    pub offset: u64,
    #[serde(default = "default_limit")]
    pub limit: u64,
    #[serde(default)]
    pub order_by: EventOrderBy,
    #[serde(default = "default_true")]
    pub ascending: bool,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EventFilters {
    #[serde(rename = "timeStartNs")]
    pub time_start_ns: Option<u64>,
    #[serde(rename = "timeEndNs")]
    pub time_end_ns: Option<u64>,
    #[serde(rename = "threadIds")]
    pub thread_ids: Option<Vec<u32>>,
    #[serde(rename = "eventTypes")]
    pub event_types: Option<Vec<EventTypeFilter>>,
    #[serde(rename = "functionNames")]
    pub function_names: Option<Vec<String>>,
}

#[derive(Debug, Clone, Copy, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "camelCase")]
pub enum EventTypeFilter {
    TraceStart,
    TraceEnd,
    FunctionCall,
    FunctionReturn,
    SignalDelivery,
    Unknown,
}

impl EventTypeFilter {
    fn matches(&self, kind: &ParsedEventKind) -> bool {
        match (self, kind) {
            (EventTypeFilter::TraceStart, ParsedEventKind::TraceStart) => true,
            (EventTypeFilter::TraceEnd, ParsedEventKind::TraceEnd) => true,
            (EventTypeFilter::FunctionCall, ParsedEventKind::FunctionCall { .. }) => true,
            (EventTypeFilter::FunctionReturn, ParsedEventKind::FunctionReturn { .. }) => true,
            (EventTypeFilter::SignalDelivery, ParsedEventKind::SignalDelivery { .. }) => true,
            (EventTypeFilter::Unknown, ParsedEventKind::Unknown) => true,
            _ => false,
        }
    }
}

#[derive(Debug, Clone, Copy, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum EventOrderBy {
    Timestamp,
    ThreadId,
}

impl Default for EventOrderBy {
    fn default() -> Self {
        EventOrderBy::Timestamp
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct EventProjection {
    #[serde(rename = "timestampNs", default = "default_true")]
    pub timestamp_ns: bool,
    #[serde(rename = "threadId", default = "default_true")]
    pub thread_id: bool,
    #[serde(rename = "eventType", default = "default_true")]
    pub event_type: bool,
    #[serde(rename = "functionName")]
    pub function_name: bool,
}

impl Default for EventProjection {
    fn default() -> Self {
        Self {
            timestamp_ns: true,
            thread_id: true,
            event_type: true,
            function_name: false,
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EventsGetResponse {
    pub events: Vec<EventResult>,
    pub metadata: QueryMetadata,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct QueryMetadata {
    pub total_count: u64,
    pub returned_count: u64,
    pub offset: u64,
    pub limit: u64,
    pub has_more: bool,
    pub execution_time_ms: u64,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EventResult {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub timestamp_ns: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub thread_id: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub event_type: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub function_name: Option<String>,
}

#[derive(Clone)]
pub struct EventsGetHandler {
    trace_root_dir: PathBuf,
}

impl EventsGetHandler {
    pub fn new(trace_root_dir: PathBuf) -> Self {
        Self { trace_root_dir }
    }

    pub fn register(self, server: &crate::server::JsonRpcServer) {
        server
            .handler_registry()
            .register_handler("events.get", self);
    }

    fn validate_params(&self, params: &EventsGetParams) -> Result<(), JsonRpcError> {
        if params.trace_id.trim().is_empty() {
            return Err(JsonRpcError::invalid_params("traceId must not be empty"));
        }
        if params.limit > MAX_LIMIT {
            return Err(JsonRpcError::invalid_params("limit cannot exceed 10000"));
        }
        if let (Some(start), Some(end)) = (params.filters.time_start_ns, params.filters.time_end_ns)
        {
            if start >= end {
                return Err(JsonRpcError::invalid_params(
                    "timeStartNs must be less than timeEndNs",
                ));
            }
        }
        Ok(())
    }

    fn map_atf_error(err: AtfError) -> JsonRpcError {
        match err {
            AtfError::TraceNotFound(_)
            | AtfError::ManifestNotFound(_)
            | AtfError::EventsNotFound(_) => JsonRpcError::trace_not_found(),
            other => JsonRpcError::internal(format!("failed to load trace: {other}")),
        }
    }

    fn event_matches_filters(&self, event: &ParsedEvent, filters: &EventFilters) -> bool {
        if let Some(start) = filters.time_start_ns {
            if event.timestamp_ns < start {
                return false;
            }
        }
        if let Some(end) = filters.time_end_ns {
            if event.timestamp_ns > end {
                return false;
            }
        }
        if let Some(thread_ids) = filters.thread_ids.as_ref() {
            if !thread_ids.contains(&event.thread_id) {
                return false;
            }
        }
        if let Some(event_types) = filters.event_types.as_ref() {
            let kind = &event.kind;
            if !event_types.iter().any(|filter| filter.matches(kind)) {
                return false;
            }
        }
        if let Some(names) = filters.function_names.as_ref() {
            match event.kind.function_symbol() {
                Some(symbol) => {
                    if !names.iter().any(|candidate| candidate == symbol) {
                        return false;
                    }
                }
                None => return false,
            }
        }
        true
    }

    fn project_event(&self, event: &ParsedEvent, projection: &EventProjection) -> EventResult {
        let timestamp_ns = if projection.timestamp_ns {
            Some(event.timestamp_ns)
        } else {
            None
        };
        let thread_id = if projection.thread_id {
            Some(event.thread_id)
        } else {
            None
        };
        let event_type = if projection.event_type {
            Some(event.kind.as_str().to_string())
        } else {
            None
        };
        let function_name = if projection.function_name {
            event.kind.function_symbol().map(|s| s.to_string())
        } else {
            None
        };

        EventResult {
            timestamp_ns,
            thread_id,
            event_type,
            function_name,
        }
    }
}

#[async_trait]
impl JsonRpcHandler for EventsGetHandler {
    async fn call(&self, params: Option<Value>) -> JsonRpcResult {
        let params_value = params.unwrap_or_else(|| json!({}));
        let params: EventsGetParams =
            serde_json::from_value(params_value.clone()).map_err(|err| {
                JsonRpcError::invalid_params(format!("invalid events.get params: {err}"))
            })?;

        self.validate_params(&params)?;

        let trace_dir = self.trace_root_dir.join(params.trace_id.trim());
        let start_time = Instant::now();

        let reader = AtfReader::open(&trace_dir).map_err(Self::map_atf_error)?;
        let mut stream = reader.event_stream().map_err(Self::map_atf_error)?;

        let mut matched_events = Vec::new();
        while let Some(item) = stream.next() {
            let event = item.map_err(Self::map_atf_error)?;
            if self.event_matches_filters(&event, &params.filters) {
                matched_events.push(event);
            }
        }

        matched_events.sort_by(|a, b| match params.order_by {
            EventOrderBy::Timestamp => a.timestamp_ns.cmp(&b.timestamp_ns),
            EventOrderBy::ThreadId => a.thread_id.cmp(&b.thread_id),
        });

        if !params.ascending {
            matched_events.reverse();
        }

        let total_count = matched_events.len() as u64;
        let offset = usize::try_from(params.offset)
            .map_err(|_| JsonRpcError::invalid_params("offset exceeds supported range"))?;
        let limit = usize::try_from(params.limit)
            .map_err(|_| JsonRpcError::invalid_params("limit exceeds supported range"))?;

        let start_index = offset.min(matched_events.len());
        let end_index = start_index.saturating_add(limit).min(matched_events.len());
        let slice = &matched_events[start_index..end_index];

        let events: Vec<EventResult> = slice
            .iter()
            .map(|event| self.project_event(event, &params.projection))
            .collect();

        let has_more = total_count > params.offset + events.len() as u64;
        let metadata = QueryMetadata {
            total_count,
            returned_count: events.len() as u64,
            offset: params.offset,
            limit: params.limit,
            has_more,
            execution_time_ms: start_time.elapsed().as_millis() as u64,
        };

        let response = EventsGetResponse { events, metadata };

        serde_json::to_value(response)
            .map_err(|err| JsonRpcError::internal(format!("serialization failed: {err}")))
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use std::{fs::File, io::Write, path::PathBuf};
    use prost::Message;
    use serde_json::json;
    use tempfile::TempDir;
    use crate::atf::event::{event::Payload, Event, FunctionCall};

    fn timestamp(ts: u64) -> prost_types::Timestamp {
        prost_types::Timestamp {
            seconds: (ts / 1_000_000_000) as i64,
            nanos: (ts % 1_000_000_000) as i32,
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

    #[test]
    fn event_type_filter_matches_variants() {
        assert!(
            EventTypeFilter::FunctionCall.matches(&ParsedEventKind::FunctionCall {
                symbol: Some("foo".into()),
            })
        );
        assert!(!EventTypeFilter::FunctionReturn
            .matches(&ParsedEventKind::FunctionCall { symbol: None }));
    }

    #[tokio::test]
    async fn events_handler__limit_exceeds_max__then_returns_error() {
        // Test limit validation error
        let fixture = TraceFixture::new("trace_limit_error");
        fixture.write_manifest(1);

        let events = vec![function_call_event(100, 1, "test")];
        fixture.write_events(&events);

        let handler = EventsGetHandler::new(fixture.trace_root());

        // Use a limit that exceeds MAX_LIMIT (10,000)
        let params = json!({
            "traceId": "trace_limit_error",
            "offset": 0,
            "limit": 50_000
        });

        let err = handler.call(Some(params)).await.expect_err("should fail");
        assert_eq!(err.code, -32602); // Invalid params
    }

    #[tokio::test]
    async fn events_handler__basic_functionality__then_returns_events() {
        // Test basic handler functionality to ensure good path coverage
        let fixture = TraceFixture::new("trace_basic");
        fixture.write_manifest(2);

        let events = vec![
            function_call_event(100, 1, "test_func_1"),
            function_call_event(200, 1, "test_func_2"),
        ];
        fixture.write_events(&events);

        let handler = EventsGetHandler::new(fixture.trace_root());

        let params = json!({
            "traceId": "trace_basic",
            "offset": 0,
            "limit": 10
        });

        let result = handler.call(Some(params)).await.expect("should succeed");
        assert!(result.get("events").is_some());
        assert!(result.get("metadata").is_some());
    }
}
