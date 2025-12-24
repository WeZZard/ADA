// ATF V2 is now the primary format
pub mod v2;

// Re-export V2 types as top-level for convenience
pub use v2::{
    error::{AtfV2Error, Result as AtfV2Result},
    types::{IndexEvent, DetailEvent},
    session::{SessionReader, Manifest, ThreadInfo},
    thread::ThreadReader,
    index::IndexReader,
    detail::DetailReader,
};
