use std::{
    fs,
    io::Cursor,
    path::{Path, PathBuf},
};

use prost::Message;

use super::{
    error::{AtfError, AtfResult},
    event::{Event, ParsedEvent},
    manifest::ManifestInfo,
};

#[derive(Clone, Debug)]
pub struct AtfReader {
    trace_dir: PathBuf,
    manifest: ManifestInfo,
}

impl AtfReader {
    pub fn open(path: impl AsRef<Path>) -> AtfResult<Self> {
        let trace_dir = path.as_ref();
        if !trace_dir.exists() {
            return Err(AtfError::TraceNotFound(trace_dir.display().to_string()));
        }
        if !trace_dir.is_dir() {
            return Err(AtfError::TraceNotFound(trace_dir.display().to_string()));
        }

        let manifest_path = trace_dir.join("trace.json");
        let manifest_bytes = fs::read(&manifest_path).map_err(|err| {
            if err.kind() == std::io::ErrorKind::NotFound {
                AtfError::ManifestNotFound(manifest_path.display().to_string())
            } else {
                AtfError::io(manifest_path, err)
            }
        })?;

        let manifest = ManifestInfo::from_bytes(&manifest_bytes)?;

        Ok(Self {
            trace_dir: trace_dir.to_path_buf(),
            manifest,
        })
    }

    pub fn trace_dir(&self) -> &Path {
        &self.trace_dir
    }

    pub fn manifest(&self) -> &ManifestInfo {
        &self.manifest
    }

    pub fn manifest_path(&self) -> PathBuf {
        self.trace_dir.join("trace.json")
    }

    pub fn events_path(&self) -> PathBuf {
        self.trace_dir.join("events.bin")
    }

    pub fn event_stream(&self) -> AtfResult<EventStream> {
        let events_path = self.events_path();
        let data = fs::read(&events_path).map_err(|err| {
            if err.kind() == std::io::ErrorKind::NotFound {
                AtfError::EventsNotFound(events_path.display().to_string())
            } else {
                AtfError::io(events_path, err)
            }
        })?;

        Ok(EventStream::new(data))
    }

    pub fn load_all_events(&self) -> AtfResult<Vec<ParsedEvent>> {
        let mut stream = self.event_stream()?;
        let mut events = Vec::new();
        while let Some(item) = stream.next() {
            events.push(item?);
        }
        Ok(events)
    }
}

pub struct EventStream {
    data: Vec<u8>,
    position: usize,
}

impl EventStream {
    pub fn new(data: Vec<u8>) -> Self {
        Self { data, position: 0 }
    }

    pub fn is_empty(&self) -> bool {
        self.position >= self.data.len()
    }
}

impl Iterator for EventStream {
    type Item = AtfResult<ParsedEvent>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.position >= self.data.len() {
            return None;
        }

        let slice = &self.data[self.position..];
        let mut cursor = Cursor::new(slice);

        match Event::decode_length_delimited(&mut cursor) {
            Ok(event) => {
                self.position += cursor.position() as usize;
                Some(Ok(ParsedEvent::from_proto(event)))
            }
            Err(err) => {
                self.position = self.data.len();
                Some(Err(AtfError::decode(err)))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use prost::Message;
    use tempfile::{NamedTempFile, TempDir};

    use crate::atf::event::{event::Payload, Event as ProtoEvent, TraceStart};

    fn write_manifest(dir: &Path, payload: serde_json::Value) {
        let bytes = serde_json::to_vec(&payload).expect("serialize manifest");
        std::fs::write(dir.join("trace.json"), bytes).expect("write manifest");
    }

    fn write_events(dir: &Path, events: &[ProtoEvent]) {
        let mut buffer = Vec::new();
        for event in events {
            event
                .encode_length_delimited(&mut buffer)
                .expect("encode event");
        }
        std::fs::write(dir.join("events.bin"), buffer).expect("write events");
    }

    fn sample_manifest(event_count: u64) -> serde_json::Value {
        serde_json::json!({
            "os": "linux",
            "arch": "x86_64",
            "pid": 42,
            "sessionId": 1,
            "timeStartNs": 100,
            "timeEndNs": 200,
            "eventCount": event_count,
            "bytesWritten": 512,
        })
    }

    fn sample_event() -> ProtoEvent {
        ProtoEvent {
            event_id: 1,
            thread_id: 1,
            timestamp: None,
            payload: Some(Payload::TraceStart(TraceStart {
                executable_path: "a".into(),
                args: Vec::new(),
                operating_system: "linux".into(),
                cpu_architecture: "x86".into(),
            })),
        }
    }

    #[test]
    fn atf_reader_open__missing_directory__then_trace_not_found() {
        let err = AtfReader::open("/tmp/does/not/exist").expect_err("expected error");
        assert!(matches!(err, AtfError::TraceNotFound(_)));
    }

    #[test]
    fn atf_reader_open__path_is_file__then_trace_not_found() {
        let file = NamedTempFile::new().expect("temp file");
        let err = AtfReader::open(file.path()).expect_err("expected error");
        assert!(matches!(err, AtfError::TraceNotFound(_)));
    }

    #[test]
    fn atf_reader_open__manifest_missing__then_manifest_not_found() {
        let temp = TempDir::new().expect("temp dir");
        let err = AtfReader::open(temp.path()).expect_err("expected error");
        match err {
            AtfError::ManifestNotFound(path) => {
                assert!(path.ends_with("trace.json"), "path: {path}")
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn atf_reader_open__manifest_read_io_error__then_returns_io_error() {
        let temp = TempDir::new().expect("temp dir");
        std::fs::create_dir(temp.path().join("trace.json")).expect("create dir");

        let err = AtfReader::open(temp.path()).expect_err("expected error");
        match err {
            AtfError::Io { path, .. } => {
                assert!(path.display().to_string().ends_with("trace.json"))
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn atf_reader_open__manifest_validation_error__then_propagates() {
        let temp = TempDir::new().expect("temp dir");
        write_manifest(
            temp.path(),
            serde_json::json!({
                "os": "linux",
                "arch": "x86_64",
                "pid": 1,
                "sessionId": 1,
                "timeStartNs": 200,
                "timeEndNs": 100,
                "eventCount": 0,
                "bytesWritten": 0,
            }),
        );

        let err = AtfReader::open(temp.path()).expect_err("expected error");
        match err {
            AtfError::Manifest(message) => assert!(message.contains("end time")),
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn atf_reader_open__valid_manifest__then_loads_manifest() {
        let temp = TempDir::new().expect("temp dir");
        write_manifest(temp.path(), sample_manifest(2));

        let reader = AtfReader::open(temp.path()).expect("reader");
        assert_eq!(reader.manifest().event_count, 2);
        assert_eq!(reader.trace_dir(), temp.path());
        assert!(reader
            .manifest_path()
            .display()
            .to_string()
            .ends_with("trace.json"));
    }

    #[test]
    fn event_stream__missing_events__then_returns_not_found() {
        let temp = TempDir::new().expect("temp dir");
        write_manifest(temp.path(), sample_manifest(0));
        let reader = AtfReader::open(temp.path()).expect("reader");

        let err = match reader.event_stream() {
            Err(err) => err,
            Ok(_) => panic!("expected error"),
        };
        assert!(matches!(err, AtfError::EventsNotFound(_)));
    }

    #[test]
    fn event_stream__io_error__then_returns_io_variant() {
        let temp = TempDir::new().expect("temp dir");
        write_manifest(temp.path(), sample_manifest(0));
        std::fs::create_dir(temp.path().join("events.bin")).expect("create dir");
        let reader = AtfReader::open(temp.path()).expect("reader");

        let err = match reader.event_stream() {
            Err(err) => err,
            Ok(_) => panic!("expected error"),
        };
        match err {
            AtfError::Io { path, .. } => {
                assert!(path.display().to_string().ends_with("events.bin"))
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn event_stream__decode_error__then_consumes_stream() {
        let temp = TempDir::new().expect("temp dir");
        write_manifest(temp.path(), sample_manifest(1));
        std::fs::write(temp.path().join("events.bin"), vec![0xFF, 0xFF]).expect("write bytes");
        let reader = AtfReader::open(temp.path()).expect("reader");
        let mut stream = reader.event_stream().expect("stream");

        let err = stream.next().expect("item").expect_err("expected error");
        assert!(matches!(err, AtfError::Decode(_)));
        assert!(stream.next().is_none());
        assert!(stream.is_empty());
    }

    #[test]
    fn event_stream__valid_events__then_load_all_events() {
        let temp = TempDir::new().expect("temp dir");
        write_manifest(temp.path(), sample_manifest(1));
        write_events(temp.path(), &[sample_event()]);
        let reader = AtfReader::open(temp.path()).expect("reader");

        let events = reader.load_all_events().expect("events");
        assert_eq!(events.len(), 1);
        assert_eq!(events[0].kind.as_str(), "TraceStart");
    }

    #[test]
    fn event_stream__empty_data__then_is_empty() {
        let stream = EventStream::new(Vec::new());
        assert!(stream.is_empty());
    }
}
