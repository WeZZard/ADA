use serde::Deserialize;

use super::error::{AtfError, AtfResult};

#[derive(Debug, Clone)]
pub struct ManifestInfo {
    pub os: String,
    pub arch: String,
    pub pid: u32,
    pub session_id: u64,
    pub time_start_ns: u64,
    pub time_end_ns: u64,
    pub event_count: u64,
    pub span_count: Option<u64>,
    pub bytes_written: u64,
    pub modules: Vec<String>,
}

impl ManifestInfo {
    pub fn from_bytes(payload: &[u8]) -> AtfResult<Self> {
        if payload.is_empty() {
            return Err(AtfError::Manifest("manifest payload is empty".into()));
        }

        let raw: RawManifest = serde_json::from_slice(payload)?;

        if raw.time_end_ns < raw.time_start_ns {
            return Err(AtfError::manifest("manifest end time precedes start time"));
        }

        Ok(Self {
            os: raw.os,
            arch: raw.arch,
            pid: raw.pid,
            session_id: raw.session_id,
            time_start_ns: raw.time_start_ns,
            time_end_ns: raw.time_end_ns,
            event_count: raw.event_count,
            span_count: raw.span_count,
            bytes_written: raw.bytes_written,
            modules: raw.modules.unwrap_or_default(),
        })
    }

    pub fn duration_ns(&self) -> u64 {
        self.time_end_ns.saturating_sub(self.time_start_ns)
    }

    pub fn resolved_span_count(&self) -> u64 {
        self.span_count.unwrap_or_else(|| self.event_count / 2)
    }
}

#[derive(Debug, Deserialize)]
struct RawManifest {
    #[serde(rename = "os")]
    os: String,
    #[serde(rename = "arch")]
    arch: String,
    #[serde(rename = "pid")]
    pid: u32,
    #[serde(rename = "sessionId")]
    session_id: u64,
    #[serde(rename = "timeStartNs")]
    time_start_ns: u64,
    #[serde(rename = "timeEndNs")]
    time_end_ns: u64,
    #[serde(rename = "eventCount")]
    event_count: u64,
    #[serde(rename = "bytesWritten")]
    bytes_written: u64,
    #[serde(rename = "spanCount")]
    span_count: Option<u64>,
    modules: Option<Vec<String>>,
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;
    use serde_json::json;

    fn valid_manifest_json() -> serde_json::Value {
        json!({
            "os": "linux",
            "arch": "x86_64",
            "pid": 9000,
            "sessionId": 5,
            "timeStartNs": 100,
            "timeEndNs": 600,
            "eventCount": 20,
            "bytesWritten": 1024,
        })
    }

    #[test]
    fn manifest_from_bytes__empty_input__then_returns_error() {
        let err = ManifestInfo::from_bytes(&[]).expect_err("expected error");
        match err {
            AtfError::Manifest(message) => assert!(message.contains("empty")),
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn manifest_from_bytes__invalid_json__then_returns_manifest_error() {
        let err = ManifestInfo::from_bytes(b"not json").expect_err("expected error");
        match err {
            AtfError::Manifest(message) => assert!(!message.is_empty()),
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn manifest_from_bytes__end_before_start__then_returns_manifest_error() {
        let mut value = valid_manifest_json();
        value["timeStartNs"] = json!(1000);
        value["timeEndNs"] = json!(999);
        let bytes = serde_json::to_vec(&value).expect("serialize");

        let err = ManifestInfo::from_bytes(&bytes).expect_err("expected error");
        match err {
            AtfError::Manifest(message) => assert!(message.contains("end time")),
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn manifest_info__valid_payload__then_parses_and_computes_fields() {
        let mut value = valid_manifest_json();
        value["spanCount"] = serde_json::Value::Null;
        value["modules"] = serde_json::Value::Null;
        let bytes = serde_json::to_vec(&value).expect("serialize");

        let manifest = ManifestInfo::from_bytes(&bytes).expect("manifest");
        assert_eq!(manifest.duration_ns(), 500);
        assert_eq!(manifest.resolved_span_count(), 10);
        assert_eq!(manifest.modules, Vec::<String>::new());
        assert_eq!(manifest.os, "linux");
    }

    #[test]
    fn manifest_info__resolved_span_count_with_value__then_returns_value() {
        let mut value = valid_manifest_json();
        value["spanCount"] = json!(7);
        let bytes = serde_json::to_vec(&value).expect("serialize");

        let manifest = ManifestInfo::from_bytes(&bytes).expect("manifest");
        assert_eq!(manifest.resolved_span_count(), 7);
    }
}
