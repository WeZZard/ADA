//! ADA Tracer Control Plane - Interface Definition
//! 
//! This module defines the COMPLETE interface contract for the Rust tracer.
//! All implementations MUST compile against these traits.
//! 
//! Design Principles:
//! - Trait-based for testability and modularity
//! - Async-first for I/O operations
//! - Zero-copy where possible
//! - Clear ownership boundaries with C++ backend

use std::path::Path;
use std::sync::Arc;
use async_trait::async_trait;

// ============================================================================
// Core Types
// ============================================================================

/// Process identifier
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ProcessId(pub u32);

/// Thread identifier  
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ThreadId(pub usize);

/// Event lane type
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LaneType {
    /// Index lane - always-on lightweight events
    Index,
    /// Detail lane - selective rich events
    Detail,
}

/// Tracer configuration
#[derive(Debug, Clone)]
pub struct TracerConfig {
    /// Output directory for trace files
    pub output_dir: String,
    /// Size of index lane ring buffers (bytes)
    pub index_ring_size: usize,
    /// Size of detail lane ring buffers (bytes)
    pub detail_ring_size: usize,
    /// Number of rings per lane
    pub rings_per_lane: u32,
    /// Drain interval (milliseconds)
    pub drain_interval_ms: u64,
}

impl Default for TracerConfig {
    fn default() -> Self {
        Self {
            output_dir: "/tmp/ada_traces".into(),
            index_ring_size: 64 * 1024,      // 64KB per ring
            detail_ring_size: 256 * 1024,     // 256KB per ring  
            rings_per_lane: 4,                // Double buffering + 2
            drain_interval_ms: 100,           // 100ms drain cycle
        }
    }
}

/// Tracer statistics
#[derive(Debug, Default, Clone)]
pub struct TracerStats {
    pub events_captured: u64,
    pub events_dropped: u64,
    pub bytes_written: u64,
    pub active_threads: u32,
    pub hooks_installed: u32,
}

// ============================================================================
// Error Types
// ============================================================================

#[derive(Debug, thiserror::Error)]
pub enum TracerError {
    #[error("Failed to initialize backend: {0}")]
    BackendInit(String),
    
    #[error("Process spawn failed: {0}")]
    SpawnFailed(String),
    
    #[error("Process attach failed: {0}")]
    AttachFailed(String),
    
    #[error("Hook installation failed: {0}")]
    HooksFailed(String),
    
    #[error("Shared memory error: {0}")]
    SharedMemory(String),
    
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    
    #[error("FFI error: {0}")]
    Ffi(String),
}

pub type TracerResult<T> = Result<T, TracerError>;

// ============================================================================
// Main Tracer Control Interface
// ============================================================================

/// Main tracer control trait
/// 
/// This trait defines the control plane interface that orchestrates
/// the C++ backend for process tracing.
#[async_trait]
pub trait TracerControl: Send + Sync {
    /// Initialize the tracer with configuration
    async fn initialize(&mut self, config: TracerConfig) -> TracerResult<()>;
    
    /// Spawn a new process in suspended state
    async fn spawn_process(&mut self, 
                          path: &Path, 
                          args: &[String]) -> TracerResult<ProcessId>;
    
    /// Attach to an existing process
    async fn attach_process(&mut self, pid: ProcessId) -> TracerResult<()>;
    
    /// Detach from current process
    async fn detach_process(&mut self) -> TracerResult<()>;
    
    /// Resume a suspended process
    async fn resume_process(&mut self) -> TracerResult<()>;
    
    /// Install hooks in the target process
    async fn install_hooks(&mut self) -> TracerResult<()>;
    
    /// Start drain thread for event persistence
    async fn start_draining(&mut self) -> TracerResult<()>;
    
    /// Stop drain thread
    async fn stop_draining(&mut self) -> TracerResult<()>;
    
    /// Get current statistics
    async fn get_stats(&self) -> TracerResult<TracerStats>;
    
    /// Shutdown the tracer
    async fn shutdown(&mut self) -> TracerResult<()>;
}

// ============================================================================
// Event Persistence Interface
// ============================================================================

/// Raw event data from ring buffers
#[derive(Debug)]
pub struct RawEvents {
    /// Lane type these events came from
    pub lane: LaneType,
    /// Thread that generated events
    pub thread_id: ThreadId,
    /// Raw event bytes
    pub data: Vec<u8>,
    /// Timestamp of first event
    pub timestamp_ns: u64,
}

/// Event persistence trait
/// 
/// This trait defines how events are persisted to disk in the ATF format.
#[async_trait]
pub trait EventPersistence: Send + Sync {
    /// Persist a batch of raw events
    async fn persist_events(&mut self, events: Vec<RawEvents>) -> TracerResult<()>;
    
    /// Flush any buffered events to disk
    async fn flush(&mut self) -> TracerResult<()>;
    
    /// Get total bytes written
    fn bytes_written(&self) -> u64;
    
    /// Close the persistence layer
    async fn close(&mut self) -> TracerResult<()>;
}

// ============================================================================
// Drain Service Interface
// ============================================================================

/// Drain service that pulls events from ring buffers
/// 
/// This trait defines the drain thread that consumes events from
/// thread-local ring buffers and hands them to persistence.
#[async_trait]
pub trait DrainService: Send + Sync {
    /// Start the drain service
    async fn start(&mut self, 
                  persistence: Arc<dyn EventPersistence>) -> TracerResult<()>;
    
    /// Stop the drain service
    async fn stop(&mut self) -> TracerResult<()>;
    
    /// Check if service is running
    fn is_running(&self) -> bool;
    
    /// Get drain statistics
    fn get_stats(&self) -> DrainStats;
}

#[derive(Debug, Default, Clone)]
pub struct DrainStats {
    pub cycles_completed: u64,
    pub events_drained: u64,
    pub bytes_drained: u64,
    pub drain_errors: u64,
}

// ============================================================================
// Backend FFI Interface
// ============================================================================

/// FFI interface to C++ backend
/// 
/// This trait wraps the unsafe FFI calls to the C++ tracer backend.
/// Implementations handle the raw pointer management and error translation.
pub trait BackendFFI: Send + Sync {
    /// Create backend instance
    fn create(&mut self, output_dir: &str) -> TracerResult<()>;
    
    /// Destroy backend instance
    fn destroy(&mut self) -> TracerResult<()>;
    
    /// Spawn process via backend
    fn spawn(&self, path: &str, args: &[&str]) -> TracerResult<u32>;
    
    /// Attach to process via backend
    fn attach(&self, pid: u32) -> TracerResult<()>;
    
    /// Detach from process
    fn detach(&self) -> TracerResult<()>;
    
    /// Resume process
    fn resume(&self) -> TracerResult<()>;
    
    /// Install hooks
    fn install_hooks(&self) -> TracerResult<()>;
    
    /// Get ring buffer header pointer
    fn get_ring_buffer_header(&self, lane_type: LaneType) -> TracerResult<*mut RingBufferHeader>;
    
    /// Get ring buffer size
    fn get_ring_buffer_size(&self, lane_type: LaneType) -> TracerResult<usize>;
    
    /// Create drain handle
    fn create_drain(&self) -> TracerResult<*mut std::ffi::c_void>;
    
    /// Drain events into buffer
    fn drain_events(&self, 
                   drain: *mut std::ffi::c_void, 
                   buffer: &mut [u8]) -> TracerResult<usize>;
    
    /// Destroy drain handle
    fn destroy_drain(&self, drain: *mut std::ffi::c_void) -> TracerResult<()>;
    
    /// Get statistics from backend
    fn get_stats(&self) -> TracerResult<TracerStats>;
}

// ============================================================================
// Shared Memory Types (matching C++ layout)
// ============================================================================

/// Ring buffer header - must match C++ layout exactly
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct RingBufferHeader {
    pub magic: u32,
    pub version: u32,
    pub capacity: u32,
    pub write_pos: u32,
    pub read_pos: u32,
    pub flags: u32,
    pub _reserved: [u32; 10],
}

impl RingBufferHeader {
    pub const MAGIC: u32 = 0xADA0;
    pub const VERSION: u32 = 1;
}

// ============================================================================
// Factory Functions
// ============================================================================

/// Create default tracer implementation
pub fn create_tracer() -> Box<dyn TracerControl> {
    todo!("Implement DefaultTracer")
}

/// Create file-based event persistence
pub fn create_file_persistence(_output_dir: &Path) -> Box<dyn EventPersistence> {
    todo!("Implement FilePersistence")
}

/// Create default drain service
pub fn create_drain_service() -> Box<dyn DrainService> {
    todo!("Implement DefaultDrainService")
}

/// Create FFI backend wrapper
pub fn create_backend_ffi() -> Box<dyn BackendFFI> {
    todo!("Implement BackendFFIWrapper")
}

// ============================================================================
// Module Tests (Interface Compilation)
// ============================================================================

#[cfg(test)]
mod interface_tests {
    use super::*;
    
    /// Test that all interfaces compile
    #[test]
    fn test_interfaces_compile() {
        // This test just ensures our interfaces are valid Rust
        fn _test_tracer_control<T: TracerControl>(_t: &T) {}
        fn _test_event_persistence<T: EventPersistence>(_t: &T) {}
        fn _test_drain_service<T: DrainService>(_t: &T) {}
        fn _test_backend_ffi<T: BackendFFI>(_t: &T) {}
        
        // Test that factory functions have correct signatures
        let _ = create_tracer;
        let _ = create_file_persistence;
        let _ = create_drain_service;
        let _ = create_backend_ffi;
    }
}