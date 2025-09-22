use std::{fmt, io, path::PathBuf};

use thiserror::Error;

#[derive(Debug, Error)]
pub enum AtfError {
    #[error("trace directory not found: {0}")]
    TraceNotFound(String),
    #[error("manifest not found: {0}")]
    ManifestNotFound(String),
    #[error("events file not found: {0}")]
    EventsNotFound(String),
    #[error("manifest parse error: {0}")]
    Manifest(String),
    #[error("event decode error: {0}")]
    Decode(String),
    #[error("io error at {path:?}: {source}")]
    Io {
        path: PathBuf,
        #[source]
        source: io::Error,
    },
    #[error(transparent)]
    Join(#[from] tokio::task::JoinError),
}

pub type AtfResult<T> = Result<T, AtfError>;

impl AtfError {
    pub fn io(path: impl Into<PathBuf>, source: io::Error) -> Self {
        Self::Io {
            path: path.into(),
            source,
        }
    }

    pub fn manifest(details: impl fmt::Display) -> Self {
        Self::Manifest(details.to_string())
    }

    pub fn decode(details: impl fmt::Display) -> Self {
        Self::Decode(details.to_string())
    }
}

impl From<prost::DecodeError> for AtfError {
    fn from(err: prost::DecodeError) -> Self {
        AtfError::Decode(err.to_string())
    }
}

impl From<serde_json::Error> for AtfError {
    fn from(err: serde_json::Error) -> Self {
        AtfError::Manifest(err.to_string())
    }
}

#[cfg(test)]
mod tests {
    #![allow(non_snake_case)]

    use super::*;

    #[test]
    fn atf_error__io_constructor__then_preserves_path_and_source() {
        let source = io::Error::new(io::ErrorKind::Other, "kaboom");
        let err = AtfError::io("/tmp/trace.json", io::Error::new(source.kind(), source.to_string()));

        let message = err.to_string();
        match &err {
            AtfError::Io { path, source } => {
                assert!(path.display().to_string().ends_with("trace.json"));
                assert_eq!(source.kind(), io::ErrorKind::Other);
                assert!(format!("{source}").contains("kaboom"));
            }
            other => panic!("unexpected variant: {other:?}"),
        }
        assert!(message.contains("trace.json"));
        assert!(message.contains("kaboom"));
    }

    #[test]
    fn atf_error__manifest_constructor__then_formats_message() {
        let err = AtfError::manifest("missing field");
        assert!(matches!(err, AtfError::Manifest(_)));
        assert!(format!("{err}").contains("missing field"));
    }

    #[test]
    fn atf_error__decode_constructor__then_formats_message() {
        let err = AtfError::decode("bad payload");
        assert!(matches!(err, AtfError::Decode(_)));
        assert!(format!("{err}").contains("bad payload"));
    }

    #[test]
    fn atf_error__from_prost_decode_error__then_wraps_message() {
        let source = prost::DecodeError::new("garbage");
        let err = AtfError::from(source);
        assert!(matches!(err, AtfError::Decode(_)));
        assert!(format!("{err}").contains("garbage"));
    }

    #[test]
    fn atf_error__from_serde_json_error__then_wraps_message() {
        let source: serde_json::Error = serde_json::from_str::<u32>("not a number").unwrap_err();
        let err = AtfError::from(source);
        assert!(matches!(err, AtfError::Manifest(_)));
        assert!(format!("{err}").contains("manifest parse error"));
    }
}
