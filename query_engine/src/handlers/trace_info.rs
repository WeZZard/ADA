use std::{
    collections::VecDeque,
    fs::{self, Metadata},
    io::{self, Read},
    num::NonZeroUsize,
    path::PathBuf,
    sync::Arc,
    time::{Duration, Instant, SystemTime},
};

use async_trait::async_trait;
use lru::LruCache;
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tokio::task;

use crate::{
    atf::{AtfError, AtfReader, ManifestInfo, ParsedEvent},
    server::{
        handler::{JsonRpcHandler, JsonRpcResult},
        types::JsonRpcError,
        JsonRpcServer,
    },
};

const SAMPLE_COUNT: usize = 5;

#[derive(Clone)]
pub struct TraceInfoHandler {
    trace_root_dir: PathBuf,
    cache_ttl: Duration,
    cache: Option<Arc<Mutex<LruCache<String, CachedTraceInfo>>>>,
}

#[derive(Clone, Debug)]
struct CachedTraceInfo {
    base: TraceInfoResponse,
    checksums: Option<TraceChecksums>,
    samples: Option<TraceSamples>,
    cached_at: Instant,
    manifest_mtime: Option<SystemTime>,
    events_mtime: Option<SystemTime>,
}

#[derive(Debug, Deserialize)]
struct TraceInfoParams {
    #[serde(rename = "traceId")]
    trace_id: String,
    #[serde(default)]
    include_checksums: bool,
    #[serde(default)]
    include_samples: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TraceInfoResponse {
    #[serde(rename = "traceId")]
    pub trace_id: String,
    pub os: String,
    pub arch: String,
    #[serde(rename = "timeStartNs")]
    pub time_start_ns: u64,
    #[serde(rename = "timeEndNs")]
    pub time_end_ns: u64,
    #[serde(rename = "durationNs")]
    pub duration_ns: u64,
    #[serde(rename = "eventCount")]
    pub event_count: u64,
    #[serde(rename = "spanCount")]
    pub span_count: u64,
    pub files: TraceFileInfo,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub checksums: Option<TraceChecksums>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub samples: Option<TraceSamples>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TraceFileInfo {
    #[serde(rename = "manifestSize")]
    pub manifest_size: u64,
    #[serde(rename = "eventsSize")]
    pub events_size: u64,
    #[serde(rename = "totalSize")]
    pub total_size: u64,
    #[serde(rename = "avgEventSize")]
    pub avg_event_size: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TraceChecksums {
    #[serde(rename = "manifestMd5")]
    pub manifest_md5: String,
    #[serde(rename = "eventsMd5")]
    pub events_md5: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
pub struct TraceSamples {
    #[serde(rename = "firstEvents")]
    pub first_events: Vec<EventSample>,
    #[serde(rename = "lastEvents")]
    pub last_events: Vec<EventSample>,
    #[serde(rename = "randomEvents")]
    pub random_events: Vec<EventSample>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct EventSample {
    #[serde(rename = "timestampNs")]
    pub timestamp_ns: u64,
    #[serde(rename = "threadId")]
    pub thread_id: u32,
    #[serde(rename = "eventType")]
    pub event_type: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub function_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub module_name: Option<String>,
}

struct CacheSnapshot {
    base: TraceInfoResponse,
    checksums: Option<TraceChecksums>,
    samples: Option<TraceSamples>,
}

impl TraceInfoHandler {
    pub fn new(trace_root_dir: PathBuf, cache_capacity: usize, cache_ttl: Duration) -> Self {
        let cache = if cache_capacity == 0 || cache_ttl.is_zero() {
            None
        } else {
            let capacity = NonZeroUsize::new(cache_capacity).expect("validated non-zero");
            Some(Arc::new(Mutex::new(LruCache::new(capacity))))
        };
        Self {
            trace_root_dir,
            cache_ttl,
            cache,
        }
    }

    pub fn register(self, server: &JsonRpcServer) {
        server
            .handler_registry()
            .register_handler("trace.info", self);
    }

    async fn get_trace_info(
        &self,
        params: TraceInfoParams,
    ) -> Result<TraceInfoResponse, JsonRpcError> {
        let trace_id = params.trace_id.trim();
        if trace_id.is_empty() {
            return Err(JsonRpcError::invalid_params("traceId must not be empty"));
        }

        let trace_dir = self.trace_root_dir.join(trace_id);
        if !trace_dir.exists() || !trace_dir.is_dir() {
            return Err(JsonRpcError::trace_not_found());
        }

        let manifest_path = trace_dir.join("trace.json");
        let events_path = trace_dir.join("events.bin");

        let manifest_meta =
            fs::metadata(&manifest_path).map_err(|err| map_metadata_error("manifest", err))?;
        let events_meta =
            fs::metadata(&events_path).map_err(|err| map_metadata_error("events", err))?;

        let manifest_mtime = manifest_meta.modified().ok();
        let events_mtime = events_meta.modified().ok();

        if let Some(snapshot) = self.fetch_from_cache(trace_id, manifest_mtime, events_mtime) {
            return self
                .build_response_from_cache(trace_id, snapshot, &params, manifest_path, events_path)
                .await;
        }

        let reader = AtfReader::open(&trace_dir).map_err(|err| self.map_atf_error(err))?;
        let base =
            self.build_base_response(trace_id, reader.manifest(), &manifest_meta, &events_meta);

        let mut response = base.clone();
        let mut cached_checksums = None;
        let mut cached_samples = None;

        if params.include_checksums {
            let checksums = self
                .compute_checksums(manifest_path.clone(), events_path.clone())
                .await?;
            response.checksums = Some(checksums.clone());
            cached_checksums = Some(checksums);
        }

        if params.include_samples {
            let samples = self.compute_samples(reader.clone()).await?;
            response.samples = Some(samples.clone());
            cached_samples = Some(samples);
        }

        self.cache_response(
            trace_id,
            base,
            manifest_mtime,
            events_mtime,
            cached_checksums.clone(),
            cached_samples.clone(),
        );

        Ok(response)
    }

    fn fetch_from_cache(
        &self,
        trace_id: &str,
        manifest_mtime: Option<SystemTime>,
        events_mtime: Option<SystemTime>,
    ) -> Option<CacheSnapshot> {
        let cache = self.cache.as_ref()?;
        let mut cache_lock = cache.lock();
        let entry = cache_lock.get(trace_id)?;

        if entry.cached_at.elapsed() > self.cache_ttl {
            cache_lock.pop(trace_id);
            return None;
        }

        if let (Some(current), Some(stored)) = (manifest_mtime, entry.manifest_mtime) {
            if current > stored {
                cache_lock.pop(trace_id);
                return None;
            }
        }

        if let (Some(current), Some(stored)) = (events_mtime, entry.events_mtime) {
            if current > stored {
                cache_lock.pop(trace_id);
                return None;
            }
        }

        Some(CacheSnapshot {
            base: entry.base.clone(),
            checksums: entry.checksums.clone(),
            samples: entry.samples.clone(),
        })
    }

    async fn build_response_from_cache(
        &self,
        trace_id: &str,
        snapshot: CacheSnapshot,
        params: &TraceInfoParams,
        manifest_path: PathBuf,
        events_path: PathBuf,
    ) -> Result<TraceInfoResponse, JsonRpcError> {
        let mut response = snapshot.base.clone();
        let mut new_checksums = None;
        let mut new_samples = None;

        if params.include_checksums {
            if let Some(checksums) = snapshot.checksums.clone() {
                response.checksums = Some(checksums);
            } else {
                let checksums = self
                    .compute_checksums(manifest_path.clone(), events_path.clone())
                    .await?;
                response.checksums = Some(checksums.clone());
                new_checksums = Some(checksums);
            }
        }

        if params.include_samples {
            if let Some(samples) = snapshot.samples.clone() {
                response.samples = Some(samples);
            } else {
                let reader = AtfReader::open(self.trace_root_dir.join(trace_id))
                    .map_err(|err| self.map_atf_error(err))?;
                let samples = self.compute_samples(reader).await?;
                response.samples = Some(samples.clone());
                new_samples = Some(samples);
            }
        }

        if let Some(checksums) = new_checksums {
            self.update_cached_checksums(trace_id, checksums);
        }
        if let Some(samples) = new_samples {
            self.update_cached_samples(trace_id, samples);
        }

        Ok(response)
    }

    fn cache_response(
        &self,
        trace_id: &str,
        mut base: TraceInfoResponse,
        manifest_mtime: Option<SystemTime>,
        events_mtime: Option<SystemTime>,
        checksums: Option<TraceChecksums>,
        samples: Option<TraceSamples>,
    ) {
        let cache = match &self.cache {
            Some(cache) => cache,
            None => return,
        };

        base.checksums = None;
        base.samples = None;

        let entry = CachedTraceInfo {
            base,
            checksums,
            samples,
            cached_at: Instant::now(),
            manifest_mtime,
            events_mtime,
        };

        cache.lock().put(trace_id.to_string(), entry);
    }

    fn update_cached_checksums(&self, trace_id: &str, checksums: TraceChecksums) {
        if let Some(cache) = &self.cache {
            if let Some(entry) = cache.lock().get_mut(trace_id) {
                entry.checksums = Some(checksums);
            }
        }
    }

    fn update_cached_samples(&self, trace_id: &str, samples: TraceSamples) {
        if let Some(cache) = &self.cache {
            if let Some(entry) = cache.lock().get_mut(trace_id) {
                entry.samples = Some(samples);
            }
        }
    }

    fn build_base_response(
        &self,
        trace_id: &str,
        manifest: &ManifestInfo,
        manifest_meta: &Metadata,
        events_meta: &Metadata,
    ) -> TraceInfoResponse {
        let manifest_size = manifest_meta.len();
        let events_size = events_meta.len();
        let avg_event_size = if manifest.event_count > 0 {
            events_size / manifest.event_count
        } else {
            0
        };

        TraceInfoResponse {
            trace_id: trace_id.to_string(),
            os: manifest.os.clone(),
            arch: manifest.arch.clone(),
            time_start_ns: manifest.time_start_ns,
            time_end_ns: manifest.time_end_ns,
            duration_ns: manifest.duration_ns(),
            event_count: manifest.event_count,
            span_count: manifest.resolved_span_count(),
            files: TraceFileInfo {
                manifest_size,
                events_size,
                total_size: manifest_size + events_size,
                avg_event_size,
            },
            checksums: None,
            samples: None,
        }
    }

    async fn compute_checksums(
        &self,
        manifest_path: PathBuf,
        events_path: PathBuf,
    ) -> Result<TraceChecksums, JsonRpcError> {
        let (manifest_md5, events_md5) = tokio::try_join!(
            compute_file_md5(manifest_path.clone()),
            compute_file_md5(events_path.clone())
        )?;

        Ok(TraceChecksums {
            manifest_md5,
            events_md5,
        })
    }

    async fn compute_samples(&self, reader: AtfReader) -> Result<TraceSamples, JsonRpcError> {
        let result = task::spawn_blocking(move || sample_events(&reader)).await;
        match result {
            Ok(Ok(samples)) => Ok(samples),
            Ok(Err(err)) => Err(self.map_atf_error(err)),
            Err(err) => Err(JsonRpcError::internal(format!(
                "sampling task failed: {err}"
            ))),
        }
    }

    fn map_atf_error(&self, err: AtfError) -> JsonRpcError {
        match err {
            AtfError::TraceNotFound(_)
            | AtfError::ManifestNotFound(_)
            | AtfError::EventsNotFound(_) => JsonRpcError::trace_not_found(),
            AtfError::Manifest(message) => {
                JsonRpcError::internal(format!("failed to parse manifest: {message}"))
            }
            AtfError::Decode(message) => {
                JsonRpcError::internal(format!("failed to decode events: {message}"))
            }
            AtfError::Io { path, source } => {
                JsonRpcError::internal(format!("io error at {}: {}", path.display(), source))
            }
            AtfError::Join(err) => JsonRpcError::internal(format!("blocking task failed: {err}")),
        }
    }
}

#[async_trait]
impl JsonRpcHandler for TraceInfoHandler {
    async fn call(&self, params: Option<Value>) -> JsonRpcResult {
        let params: TraceInfoParams = match params {
            Some(value) => serde_json::from_value(value).map_err(|err| {
                JsonRpcError::invalid_params(format!("invalid trace.info parameters: {err}"))
            })?,
            None => {
                return Err(JsonRpcError::invalid_params(
                    "missing trace.info parameters",
                ))
            }
        };

        let response = self.get_trace_info(params).await?;
        serde_json::to_value(response)
            .map_err(|err| JsonRpcError::internal(format!("failed to serialize response: {err}")))
    }
}

impl From<&ParsedEvent> for EventSample {
    fn from(event: &ParsedEvent) -> Self {
        Self {
            timestamp_ns: event.timestamp_ns,
            thread_id: event.thread_id,
            event_type: event.kind.as_str().to_string(),
            function_name: event.kind.function_symbol().map(|s| s.to_string()),
            module_name: None,
        }
    }
}

fn sample_events(reader: &AtfReader) -> Result<TraceSamples, AtfError> {
    let mut stream = match reader.event_stream() {
        Ok(stream) => stream,
        Err(AtfError::EventsNotFound(_)) => return Ok(TraceSamples::default()),
        Err(err) => return Err(err),
    };

    let total_events = reader.manifest().event_count.max(1);
    let mut first_events = Vec::new();
    let mut random_events = Vec::new();
    let mut last_events: VecDeque<EventSample> = VecDeque::with_capacity(SAMPLE_COUNT);
    let mut count: u64 = 0;
    let sample_interval = std::cmp::max(1, total_events / 50);

    while let Some(event) = stream.next() {
        let parsed = event?;
        count += 1;
        let sample = EventSample::from(&parsed);

        if first_events.len() < SAMPLE_COUNT {
            first_events.push(sample.clone());
        }

        if count > SAMPLE_COUNT as u64
            && random_events.len() < SAMPLE_COUNT
            && count % sample_interval == 0
        {
            random_events.push(sample.clone());
        }

        if last_events.len() == SAMPLE_COUNT {
            last_events.pop_front();
        }
        last_events.push_back(sample);
    }

    if count == 0 {
        return Ok(TraceSamples::default());
    }

    let mut random_events = random_events;
    random_events.truncate(SAMPLE_COUNT);

    let last_events = if count <= SAMPLE_COUNT as u64 {
        first_events.clone()
    } else {
        last_events.into_iter().collect()
    };

    Ok(TraceSamples {
        first_events,
        last_events,
        random_events,
    })
}

async fn compute_file_md5(path: PathBuf) -> Result<String, JsonRpcError> {
    let display_path = path.display().to_string();
    let result = task::spawn_blocking(move || -> io::Result<String> {
        let mut file = std::fs::File::open(&path)?;
        let mut context = md5::Context::new();
        let mut buffer = [0u8; 8192];
        loop {
            let read = file.read(&mut buffer)?;
            if read == 0 {
                break;
            }
            context.consume(&buffer[..read]);
        }
        let digest = context.compute();
        Ok(format!("{:x}", digest))
    })
    .await
    .map_err(|err| JsonRpcError::internal(format!("checksum task failed: {err}")))?;

    result.map_err(|err| {
        JsonRpcError::internal(format!("failed to read {display_path} for checksum: {err}"))
    })
}

fn map_metadata_error(kind: &str, err: io::Error) -> JsonRpcError {
    if err.kind() == io::ErrorKind::NotFound {
        JsonRpcError::trace_not_found()
    } else {
        JsonRpcError::internal(format!("failed to read {kind} metadata: {err}"))
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use crate::atf::{
        event::{event::Payload, Event, FunctionCall, FunctionReturn, TraceEnd, TraceStart},
        AtfReader,
    };
    use crate::server::{server::JsonRpcServer, types::JsonRpcError};
    use prost::Message;
    use serde_json::json;
    use std::{
        fs::{self, File},
        io::{self, Write},
        path::PathBuf,
        time::Duration,
    };
    use tempfile::TempDir;

    struct HandlerTestFixture {
        temp: TempDir,
        trace_id: String,
    }

    impl HandlerTestFixture {
        fn new(trace_id: &str) -> io::Result<Self> {
            let temp = TempDir::new()?;
            let trace_id = trace_id.to_string();
            fs::create_dir_all(temp.path().join(&trace_id))?;
            Ok(Self { temp, trace_id })
        }

        fn trace_root(&self) -> PathBuf {
            self.temp.path().to_path_buf()
        }

        fn trace_dir(&self) -> PathBuf {
            self.temp.path().join(&self.trace_id)
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

        fn overwrite_events_bytes(&self, bytes: &[u8]) -> io::Result<()> {
            fs::write(self.events_path(), bytes)
        }
    }

    fn sample_manifest(event_count: u64, span_count: Option<u64>) -> Value {
        json!({
            "os": "linux",
            "arch": "x86_64",
            "pid": 9000,
            "sessionId": 1,
            "timeStartNs": 100,
            "timeEndNs": 2100,
            "eventCount": event_count,
            "bytesWritten": 4096,
            "modules": ["mod"],
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

    fn dummy_response(trace_id: &str) -> TraceInfoResponse {
        TraceInfoResponse {
            trace_id: trace_id.to_string(),
            os: "linux".into(),
            arch: "x86_64".into(),
            time_start_ns: 0,
            time_end_ns: 0,
            duration_ns: 0,
            event_count: 0,
            span_count: 0,
            files: TraceFileInfo {
                manifest_size: 0,
                events_size: 0,
                total_size: 0,
                avg_event_size: 0,
            },
            checksums: None,
            samples: None,
        }
    }

    #[test]
    fn trace_info_handler_new__zero_capacity__then_cache_disabled() {
        let handler = TraceInfoHandler::new(PathBuf::new(), 0, Duration::from_secs(60));
        assert!(handler.cache.is_none());
    }

    #[test]
    fn trace_info_handler_new__zero_ttl__then_cache_disabled() {
        let handler = TraceInfoHandler::new(PathBuf::new(), 4, Duration::from_secs(0));
        assert!(handler.cache.is_none());
    }

    #[test]
    fn trace_info_handler_register__then_handler_present_in_registry() {
        let server = JsonRpcServer::new();
        TraceInfoHandler::new(PathBuf::from("/tmp"), 4, Duration::from_secs(60))
            .register(&server);

        assert!(server.handler_registry().contains("trace.info"));
    }

    #[test]
    fn map_metadata_error__missing_file__then_trace_not_found() {
        let err = map_metadata_error(
            "manifest",
            io::Error::new(io::ErrorKind::NotFound, "missing"),
        );
        assert_eq!(err.code, JsonRpcError::trace_not_found().code);
    }

    #[test]
    fn map_metadata_error__other_io_error__then_internal_error() {
        let err = map_metadata_error(
            "events",
            io::Error::new(io::ErrorKind::PermissionDenied, "denied"),
        );
        assert_eq!(err.code, JsonRpcError::internal(String::new()).code);
        assert_eq!(err.message, "Internal error");
        let detail = err
            .data
            .as_ref()
            .and_then(|value| value.as_str())
            .expect("detail");
        assert!(detail.contains("failed to read events metadata"));
        assert!(detail.contains("denied"));
    }

    #[tokio::test]
    async fn compute_file_md5__existing_file__then_returns_hash() {
        let file = tempfile::NamedTempFile::new().expect("temp file");
        std::fs::write(file.path(), b"hello").expect("write");

        let digest = compute_file_md5(file.path().to_path_buf())
            .await
            .expect("digest");
        assert_eq!(digest, "5d41402abc4b2a76b9719d911017c592");
    }

    #[tokio::test]
    async fn compute_file_md5__missing_file__then_returns_internal_error() {
        let err = compute_file_md5(PathBuf::from("/tmp/never_exists.bin"))
            .await
            .expect_err("expected error");
        assert_eq!(err.code, JsonRpcError::internal(String::new()).code);
        let detail = err
            .data
            .as_ref()
            .and_then(|value| value.as_str())
            .expect("detail");
        assert!(detail.contains("failed to read /tmp/never_exists.bin"));
    }

    #[tokio::test]
    async fn map_atf_error__variants__then_expected_mappings() {
        let handler = TraceInfoHandler::new(PathBuf::new(), 0, Duration::from_secs(0));

        let not_found = handler.map_atf_error(AtfError::TraceNotFound("/tmp".into()));
        assert_eq!(not_found.code, JsonRpcError::trace_not_found().code);

        let manifest_missing = handler.map_atf_error(AtfError::ManifestNotFound("/tmp".into()));
        assert_eq!(manifest_missing.code, JsonRpcError::trace_not_found().code);

        let events_missing = handler.map_atf_error(AtfError::EventsNotFound("/tmp".into()));
        assert_eq!(events_missing.code, JsonRpcError::trace_not_found().code);

        let manifest_err = handler.map_atf_error(AtfError::Manifest("bad".into()));
        assert_eq!(
            manifest_err.code,
            JsonRpcError::internal(String::new()).code
        );
        let manifest_detail = manifest_err
            .data
            .as_ref()
            .and_then(|value| value.as_str())
            .expect("detail");
        assert!(manifest_detail.contains("failed to parse manifest"));

        let decode_err = handler.map_atf_error(AtfError::Decode("corrupt".into()));
        assert_eq!(decode_err.code, JsonRpcError::internal(String::new()).code);
        let decode_detail = decode_err
            .data
            .as_ref()
            .and_then(|value| value.as_str())
            .expect("detail");
        assert!(decode_detail.contains("failed to decode events"));

        let io_err = handler.map_atf_error(AtfError::io(
            "/tmp/file",
            io::Error::new(io::ErrorKind::Other, "boom"),
        ));
        let io_detail = io_err
            .data
            .as_ref()
            .and_then(|value| value.as_str())
            .expect("detail");
        assert!(io_detail.contains("io error"));

        let join_err = tokio::spawn(async { panic!("boom") }).await.unwrap_err();
        let mapped_join = handler.map_atf_error(AtfError::Join(join_err));
        assert_eq!(mapped_join.code, JsonRpcError::internal(String::new()).code);
        let join_detail = mapped_join
            .data
            .as_ref()
            .and_then(|value| value.as_str())
            .expect("detail");
        assert!(join_detail.contains("blocking task failed"));
    }

    #[tokio::test]
    async fn build_response_from_cache__missing_optional_fields__then_populates_and_updates_cache()
    {
        let fixture = HandlerTestFixture::new("lazy").expect("fixture");
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

        let handler = TraceInfoHandler::new(fixture.trace_root(), 4, Duration::from_secs(60));
        let trace_id = fixture.trace_id.clone();

        handler
            .get_trace_info(TraceInfoParams {
                trace_id: trace_id.clone(),
                include_checksums: false,
                include_samples: false,
            })
            .await
            .expect("initial call");

        {
            let cache = handler.cache.as_ref().expect("cache").lock();
            let entry = cache.peek(&trace_id).expect("entry");
            assert!(entry.checksums.is_none());
            assert!(entry.samples.is_none());
        }

        let manifest_meta = fs::metadata(fixture.manifest_path()).expect("meta");
        let events_meta = fs::metadata(fixture.events_path()).expect("meta");
        let snapshot = handler
            .fetch_from_cache(
                &trace_id,
                manifest_meta.modified().ok(),
                events_meta.modified().ok(),
            )
            .expect("snapshot");

        let response = handler
            .build_response_from_cache(
                &trace_id,
                snapshot,
                &TraceInfoParams {
                    trace_id: trace_id.clone(),
                    include_checksums: true,
                    include_samples: true,
                },
                fixture.manifest_path(),
                fixture.events_path(),
            )
            .await
            .expect("response");

        assert!(response.checksums.is_some());
        assert!(response.samples.is_some());

        {
            let cache = handler.cache.as_ref().expect("cache").lock();
            let entry = cache.peek(&trace_id).expect("entry");
            assert!(entry.checksums.is_some());
            assert!(entry.samples.is_some());
        }
    }

    #[tokio::test]
    async fn fetch_from_cache__file_modified__then_entry_invalidated() {
        let fixture = HandlerTestFixture::new("invalidate").expect("fixture");
        let events = vec![function_call_event(100, 1, "foo")];
        fixture
            .write_manifest(sample_manifest(events.len() as u64, Some(1)))
            .expect("manifest");
        fixture.write_events(&events).expect("events");

        let handler = TraceInfoHandler::new(fixture.trace_root(), 4, Duration::from_secs(120));
        let trace_id = fixture.trace_id.clone();

        handler
            .get_trace_info(TraceInfoParams {
                trace_id: trace_id.clone(),
                include_checksums: false,
                include_samples: false,
            })
            .await
            .expect("initial call");

        let original_events_meta = fs::metadata(fixture.events_path()).expect("meta");

        tokio::time::sleep(Duration::from_millis(20)).await;
        fixture
            .write_manifest(sample_manifest(42, Some(3)))
            .expect("manifest update");

        let manifest_meta = fs::metadata(fixture.manifest_path()).expect("meta");
        let snapshot = handler.fetch_from_cache(
            &trace_id,
            manifest_meta.modified().ok(),
            original_events_meta.modified().ok(),
        );
        assert!(
            snapshot.is_none(),
            "manifest change should invalidate cache"
        );

        handler
            .get_trace_info(TraceInfoParams {
                trace_id: trace_id.clone(),
                include_checksums: false,
                include_samples: false,
            })
            .await
            .expect("repopulate");

        tokio::time::sleep(Duration::from_millis(20)).await;
        fixture
            .write_events(&[
                function_call_event(100, 1, "foo"),
                function_return_event(200, 1, "foo"),
            ])
            .expect("events update");

        let manifest_meta = fs::metadata(fixture.manifest_path()).expect("meta");
        let events_meta = fs::metadata(fixture.events_path()).expect("meta");
        let snapshot = handler.fetch_from_cache(
            &trace_id,
            manifest_meta.modified().ok(),
            events_meta.modified().ok(),
        );
        assert!(snapshot.is_none(), "events change should invalidate cache");
    }

    #[test]
    fn cache_response__cache_disabled__then_no_entry_created() {
        let handler = TraceInfoHandler::new(PathBuf::new(), 0, Duration::from_secs(60));
        handler.cache_response("trace", dummy_response("trace"), None, None, None, None);
        assert!(handler.cache.is_none());
    }

    #[test]
    fn update_cached_entries__then_optional_fields_saved() {
        let handler = TraceInfoHandler::new(PathBuf::new(), 4, Duration::from_secs(60));
        handler.cache_response("trace", dummy_response("trace"), None, None, None, None);

        let checksums = TraceChecksums {
            manifest_md5: "aa".repeat(16),
            events_md5: "bb".repeat(16),
        };
        handler.update_cached_checksums("trace", checksums.clone());

        let samples = TraceSamples {
            first_events: vec![EventSample {
                timestamp_ns: 1,
                thread_id: 1,
                event_type: "call".into(),
                function_name: Some("foo".into()),
                module_name: None,
            }],
            last_events: vec![],
            random_events: vec![],
        };
        handler.update_cached_samples("trace", samples.clone());

        let cache = handler.cache.as_ref().expect("cache").lock();
        let entry = cache.peek("trace").expect("entry");
        assert_eq!(entry.checksums.as_ref(), Some(&checksums));
        assert_eq!(entry.samples.as_ref(), Some(&samples));
    }

    #[test]
    fn sample_events__missing_events_file__then_returns_default_samples() {
        let fixture = HandlerTestFixture::new("missing").expect("fixture");
        fixture
            .write_manifest(sample_manifest(10, Some(5)))
            .expect("manifest");

        let reader = AtfReader::open(fixture.trace_dir()).expect("reader");
        let samples = sample_events(&reader).expect("samples");
        assert!(samples.first_events.is_empty());
        assert!(samples.last_events.is_empty());
        assert!(samples.random_events.is_empty());
    }

    #[test]
    fn sample_events__limited_events__then_last_matches_first() {
        let fixture = HandlerTestFixture::new("limited").expect("fixture");
        let events = vec![
            function_call_event(100, 1, "foo"),
            function_return_event(200, 1, "foo"),
            function_call_event(300, 1, "bar"),
        ];
        fixture
            .write_manifest(sample_manifest(events.len() as u64, Some(2)))
            .expect("manifest");
        fixture.write_events(&events).expect("events");

        let reader = AtfReader::open(fixture.trace_dir()).expect("reader");
        let samples = sample_events(&reader).expect("samples");
        assert_eq!(samples.first_events.len(), 3);
        assert_eq!(samples.last_events, samples.first_events);
        assert!(samples.random_events.is_empty());
    }

    #[test]
    fn sample_events__many_events__then_collects_random_and_last_samples() {
        let fixture = HandlerTestFixture::new("many").expect("fixture");
        let mut events = Vec::new();
        for i in 0..60 {
            events.push(function_call_event(100 * (i + 1) as u64, 1, "foo"));
        }
        fixture
            .write_manifest(sample_manifest(events.len() as u64, Some(30)))
            .expect("manifest");
        fixture.write_events(&events).expect("events");

        let reader = AtfReader::open(fixture.trace_dir()).expect("reader");
        let samples = sample_events(&reader).expect("samples");
        assert_eq!(samples.first_events.len(), SAMPLE_COUNT);
        assert_eq!(samples.random_events.len(), SAMPLE_COUNT);
        assert_eq!(samples.last_events.len(), SAMPLE_COUNT);
        assert_eq!(samples.random_events[0].timestamp_ns, 600);
        assert_eq!(
            samples.last_events[0].timestamp_ns,
            100 * (60 - SAMPLE_COUNT + 1) as u64,
        );
    }

    #[test]
    fn sample_events__decode_failure__then_propagates_error() {
        let fixture = HandlerTestFixture::new("decode").expect("fixture");
        fixture
            .write_manifest(sample_manifest(1, Some(0)))
            .expect("manifest");
        fixture
            .overwrite_events_bytes(&[0xFF, 0xFF, 0xFF])
            .expect("events");

        let reader = AtfReader::open(fixture.trace_dir()).expect("reader");
        let err = sample_events(&reader).expect_err("expected error");
        match err {
            AtfError::Decode(_) => {}
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[tokio::test]
    async fn compute_samples__decode_error__then_maps_to_json_rpc_error() {
        let fixture = HandlerTestFixture::new("decode_map").expect("fixture");
        fixture
            .write_manifest(sample_manifest(1, Some(0)))
            .expect("manifest");
        fixture
            .overwrite_events_bytes(&[0xAA, 0xBB, 0xCC])
            .expect("events");

        let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));
        let reader = AtfReader::open(fixture.trace_dir()).expect("reader");
        let err = handler
            .compute_samples(reader)
            .await
            .expect_err("expected error");
        assert_eq!(err.message, "Internal error");
        let detail = err
            .data
            .as_ref()
            .and_then(|value| value.as_str())
            .expect("detail");
        assert!(detail.contains("failed to decode events"));
    }

    #[tokio::test]
    async fn compute_samples__io_error__then_maps_to_internal_error() {
        let fixture = HandlerTestFixture::new("io_map").expect("fixture");
        fixture
            .write_manifest(sample_manifest(1, Some(0)))
            .expect("manifest");
        fs::create_dir_all(fixture.events_path()).expect("events dir");

        let handler = TraceInfoHandler::new(fixture.trace_root(), 2, Duration::from_secs(60));
        let reader = AtfReader::open(fixture.trace_dir()).expect("reader");
        let err = handler
            .compute_samples(reader)
            .await
            .expect_err("expected error");
        assert_eq!(err.message, "Internal error");
        let detail = err
            .data
            .as_ref()
            .and_then(|value| value.as_str())
            .expect("detail");
        assert!(detail.contains("io error"));
    }
}
