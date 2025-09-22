use std::{collections::HashMap, path::PathBuf, time::Instant};

use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::{
    atf::{AtfError, AtfReader, ParsedEventKind},
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
pub struct SpansListParams {
    #[serde(rename = "traceId")]
    pub trace_id: String,
    #[serde(default)]
    pub filters: SpanFilters,
    #[serde(default)]
    pub projection: SpanProjection,
    #[serde(default)]
    pub offset: u64,
    #[serde(default = "default_limit")]
    pub limit: u64,
    #[serde(default = "default_true")]
    pub include_children: bool,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpanFilters {
    #[serde(rename = "timeStartNs")]
    pub time_start_ns: Option<u64>,
    #[serde(rename = "timeEndNs")]
    pub time_end_ns: Option<u64>,
    #[serde(rename = "threadIds")]
    pub thread_ids: Option<Vec<u32>>,
    #[serde(rename = "functionNames")]
    pub function_names: Option<Vec<String>>,
    #[serde(rename = "minDurationNs")]
    pub min_duration_ns: Option<u64>,
    #[serde(rename = "maxDurationNs")]
    pub max_duration_ns: Option<u64>,
    #[serde(rename = "minDepth")]
    pub min_depth: Option<u32>,
    #[serde(rename = "maxDepth")]
    pub max_depth: Option<u32>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct SpanProjection {
    #[serde(rename = "spanId", default = "default_true")]
    pub span_id: bool,
    #[serde(rename = "functionName", default = "default_true")]
    pub function_name: bool,
    #[serde(rename = "startTimeNs", default = "default_true")]
    pub start_time_ns: bool,
    #[serde(rename = "endTimeNs", default = "default_true")]
    pub end_time_ns: bool,
    #[serde(rename = "durationNs", default = "default_true")]
    pub duration_ns: bool,
    #[serde(rename = "threadId")]
    pub thread_id: bool,
    #[serde(rename = "moduleName")]
    pub module_name: bool,
    #[serde(rename = "depth")]
    pub depth: bool,
    #[serde(rename = "childCount")]
    pub child_count: bool,
}

impl Default for SpanProjection {
    fn default() -> Self {
        Self {
            span_id: true,
            function_name: true,
            start_time_ns: true,
            end_time_ns: true,
            duration_ns: true,
            thread_id: false,
            module_name: false,
            depth: false,
            child_count: false,
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpansListResponse {
    pub spans: Vec<SpanResult>,
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
pub struct SpanResult {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub span_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub function_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub start_time_ns: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub end_time_ns: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub duration_ns: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub thread_id: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub module_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub depth: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub child_count: Option<u32>,
}

#[derive(Debug, Clone)]
struct SpanCandidate {
    span_id: String,
    function_name: Option<String>,
    start_time_ns: u64,
    end_time_ns: u64,
    duration_ns: u64,
    thread_id: u32,
    depth: u32,
    child_count: u32,
}

#[derive(Debug, Clone)]
struct ActiveSpan {
    function_name: Option<String>,
    start_time_ns: u64,
    depth: u32,
    child_count: u32,
    span_sequence: u64,
}

#[derive(Clone)]
pub struct SpansListHandler {
    trace_root_dir: PathBuf,
}

impl SpansListHandler {
    pub fn new(trace_root_dir: PathBuf) -> Self {
        Self { trace_root_dir }
    }

    pub fn register(self, server: &crate::server::JsonRpcServer) {
        server
            .handler_registry()
            .register_handler("spans.list", self);
    }

    fn validate_params(&self, params: &SpansListParams) -> Result<(), JsonRpcError> {
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
        if let (Some(min_depth), Some(max_depth)) =
            (params.filters.min_depth, params.filters.max_depth)
        {
            if min_depth > max_depth {
                return Err(JsonRpcError::invalid_params("minDepth must be <= maxDepth"));
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

    fn span_matches_filters(
        &self,
        span: &SpanCandidate,
        filters: &SpanFilters,
        include_children: bool,
    ) -> bool {
        if !include_children && span.depth > 0 {
            return false;
        }
        if let Some(thread_ids) = filters.thread_ids.as_ref() {
            if !thread_ids.contains(&span.thread_id) {
                return false;
            }
        }
        if let Some(start) = filters.time_start_ns {
            if span.start_time_ns < start {
                return false;
            }
        }
        if let Some(end) = filters.time_end_ns {
            if span.end_time_ns > end {
                return false;
            }
        }
        if let Some(min_duration) = filters.min_duration_ns {
            if span.duration_ns < min_duration {
                return false;
            }
        }
        if let Some(max_duration) = filters.max_duration_ns {
            if span.duration_ns > max_duration {
                return false;
            }
        }
        if let Some(min_depth) = filters.min_depth {
            if span.depth < min_depth {
                return false;
            }
        }
        if let Some(max_depth) = filters.max_depth {
            if span.depth > max_depth {
                return false;
            }
        }
        if let Some(function_names) = filters.function_names.as_ref() {
            match span.function_name.as_ref() {
                Some(name) => {
                    if !function_names.iter().any(|candidate| candidate == name) {
                        return false;
                    }
                }
                None => return false,
            }
        }
        true
    }

    fn project_span(&self, span: &SpanCandidate, projection: &SpanProjection) -> SpanResult {
        SpanResult {
            span_id: if projection.span_id {
                Some(span.span_id.clone())
            } else {
                None
            },
            function_name: if projection.function_name {
                span.function_name.clone()
            } else {
                None
            },
            start_time_ns: if projection.start_time_ns {
                Some(span.start_time_ns)
            } else {
                None
            },
            end_time_ns: if projection.end_time_ns {
                Some(span.end_time_ns)
            } else {
                None
            },
            duration_ns: if projection.duration_ns {
                Some(span.duration_ns)
            } else {
                None
            },
            thread_id: if projection.thread_id {
                Some(span.thread_id)
            } else {
                None
            },
            module_name: None,
            depth: if projection.depth {
                Some(span.depth)
            } else {
                None
            },
            child_count: if projection.child_count {
                Some(span.child_count)
            } else {
                None
            },
        }
    }
}

#[async_trait]
impl JsonRpcHandler for SpansListHandler {
    async fn call(&self, params: Option<Value>) -> JsonRpcResult {
        let params_value = params.unwrap_or_else(|| json!({}));
        let params: SpansListParams =
            serde_json::from_value(params_value.clone()).map_err(|err| {
                JsonRpcError::invalid_params(format!("invalid spans.list params: {err}"))
            })?;

        self.validate_params(&params)?;

        let trace_dir = self.trace_root_dir.join(params.trace_id.trim());
        let start_time = Instant::now();

        let reader = AtfReader::open(&trace_dir).map_err(Self::map_atf_error)?;
        let mut stream = reader.event_stream().map_err(Self::map_atf_error)?;

        let mut call_stacks: HashMap<u32, Vec<ActiveSpan>> = HashMap::new();
        let mut spans = Vec::new();
        let mut span_sequence: u64 = 0;

        while let Some(item) = stream.next() {
            let event = item.map_err(Self::map_atf_error)?;
            match &event.kind {
                ParsedEventKind::FunctionCall { symbol } => {
                    let stack = call_stacks.entry(event.thread_id).or_default();
                    let depth = stack.len() as u32;
                    span_sequence = span_sequence.wrapping_add(1);
                    stack.push(ActiveSpan {
                        function_name: symbol.clone(),
                        start_time_ns: event.timestamp_ns,
                        depth,
                        child_count: 0,
                        span_sequence,
                    });
                }
                ParsedEventKind::FunctionReturn { .. } => {
                    if let Some(stack) = call_stacks.get_mut(&event.thread_id) {
                        if let Some(frame) = stack.pop() {
                            let duration = event.timestamp_ns.saturating_sub(frame.start_time_ns);
                            let span_id = format!(
                                "{}:{}:{}",
                                event.thread_id, frame.start_time_ns, frame.span_sequence
                            );
                            spans.push(SpanCandidate {
                                span_id,
                                function_name: frame.function_name.clone(),
                                start_time_ns: frame.start_time_ns,
                                end_time_ns: event.timestamp_ns,
                                duration_ns: duration,
                                thread_id: event.thread_id,
                                depth: frame.depth,
                                child_count: frame.child_count,
                            });

                            if let Some(parent) = stack.last_mut() {
                                parent.child_count = parent.child_count.saturating_add(1);
                            }
                        }
                    }
                }
                _ => {}
            }
        }

        spans.sort_by(|a, b| {
            a.start_time_ns
                .cmp(&b.start_time_ns)
                .then_with(|| a.thread_id.cmp(&b.thread_id))
                .then_with(|| a.span_id.cmp(&b.span_id))
        });

        let filtered: Vec<SpanCandidate> = spans
            .into_iter()
            .filter(|span| {
                self.span_matches_filters(span, &params.filters, params.include_children)
            })
            .collect();

        let total_count = filtered.len() as u64;
        let offset = usize::try_from(params.offset)
            .map_err(|_| JsonRpcError::invalid_params("offset exceeds supported range"))?;
        let limit = usize::try_from(params.limit)
            .map_err(|_| JsonRpcError::invalid_params("limit exceeds supported range"))?;

        let start_index = offset.min(filtered.len());
        let end_index = start_index.saturating_add(limit).min(filtered.len());
        let slice = &filtered[start_index..end_index];

        let spans: Vec<SpanResult> = slice
            .iter()
            .map(|span| self.project_span(span, &params.projection))
            .collect();

        let has_more = total_count > params.offset + spans.len() as u64;
        let metadata = QueryMetadata {
            total_count,
            returned_count: spans.len() as u64,
            offset: params.offset,
            limit: params.limit,
            has_more,
            execution_time_ms: start_time.elapsed().as_millis() as u64,
        };

        let response = SpansListResponse { spans, metadata };

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
    use crate::atf::event::{event::Payload, Event, FunctionCall, FunctionReturn};

    fn timestamp(ts: u64) -> prost_types::Timestamp {
        prost_types::Timestamp {
            seconds: (ts / 1_000_000_000) as i64,
            nanos: (ts % 1_000_000_000) as i32,
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
    fn span_matches_filters__depth_checks() {
        let handler = SpansListHandler::new(PathBuf::from("."));
        let span = SpanCandidate {
            span_id: "1".into(),
            function_name: Some("foo".into()),
            start_time_ns: 100,
            end_time_ns: 200,
            duration_ns: 100,
            thread_id: 1,
            depth: 2,
            child_count: 0,
        };
        let filters = SpanFilters {
            min_depth: Some(1),
            max_depth: Some(3),
            ..Default::default()
        };
        assert!(handler.span_matches_filters(&span, &filters, true));
        assert!(!handler.span_matches_filters(&span, &filters, false));
    }

    #[tokio::test]
    async fn spans_handler__limit_exceeds_max__then_returns_error() {
        // Test limit validation error for spans handler
        let fixture = TraceFixture::new("spans_limit_error");
        fixture.write_manifest(2);

        let events = vec![
            Event {
                event_id: 100,
                thread_id: 1,
                timestamp: Some(timestamp(100)),
                payload: Some(Payload::FunctionCall(FunctionCall {
                    symbol: "test".to_string(),
                    address: 0,
                    argument_registers: Default::default(),
                    stack_shallow_copy: Vec::new(),
                })),
            },
            Event {
                event_id: 200,
                thread_id: 1,
                timestamp: Some(timestamp(200)),
                payload: Some(Payload::FunctionReturn(FunctionReturn {
                    symbol: "test".to_string(),
                    address: 0,
                    return_registers: Default::default(),
                })),
            },
        ];
        fixture.write_events(&events);

        let handler = SpansListHandler::new(fixture.trace_root());

        // Use a limit that exceeds MAX_LIMIT (10,000)
        let params = json!({
            "traceId": "spans_limit_error",
            "offset": 0,
            "limit": 50_000
        });

        let err = handler.call(Some(params)).await.expect_err("should fail");
        assert_eq!(err.code, -32602); // Invalid params
    }

    #[tokio::test]
    async fn spans_handler__basic_functionality__then_returns_spans() {
        // Test basic handler functionality to ensure good path coverage
        let fixture = TraceFixture::new("spans_basic");
        fixture.write_manifest(2);

        let events = vec![
            Event {
                event_id: 100,
                thread_id: 1,
                timestamp: Some(timestamp(100)),
                payload: Some(Payload::FunctionCall(FunctionCall {
                    symbol: "test_func".to_string(),
                    address: 0,
                    argument_registers: Default::default(),
                    stack_shallow_copy: Vec::new(),
                })),
            },
            Event {
                event_id: 200,
                thread_id: 1,
                timestamp: Some(timestamp(200)),
                payload: Some(Payload::FunctionReturn(FunctionReturn {
                    symbol: "test_func".to_string(),
                    address: 0,
                    return_registers: Default::default(),
                })),
            },
        ];
        fixture.write_events(&events);

        let handler = SpansListHandler::new(fixture.trace_root());

        let params = json!({
            "traceId": "spans_basic",
            "offset": 0,
            "limit": 10
        });

        let result = handler.call(Some(params)).await.expect("should succeed");
        assert!(result.get("spans").is_some());
        assert!(result.get("metadata").is_some());
    }
}
