"""
ADA Query Engine Interface Definitions

This module defines the COMPLETE interface contracts for the query engine.
All implementations MUST comply with these protocols.

Design Principles:
- Token-budget-aware analysis
- Streaming processing for large traces
- Protocol-based for type safety
- Clear separation between parsing and analysis
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import (
    AsyncIterator, Iterator, Optional, Protocol, List, Dict, Any, 
    Union, Tuple, BinaryIO
)
from datetime import datetime

# Optional numpy import for type hints
try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

# ============================================================================
# Core Types
# ============================================================================

class EventType(Enum):
    """Event types in ADA traces"""
    FUNCTION_ENTER = "function_enter"
    FUNCTION_EXIT = "function_exit"
    MEMORY_ALLOC = "memory_alloc"
    MEMORY_FREE = "memory_free"
    SYSCALL = "syscall"
    EXCEPTION = "exception"
    CUSTOM = "custom"

class LaneType(Enum):
    """Lane types for dual-lane architecture"""
    INDEX = "index"     # Lightweight always-on events
    DETAIL = "detail"   # Rich selective events

@dataclass(frozen=True)
class TimeRange:
    """Time range for filtering"""
    start_ns: int
    end_ns: int
    
    def contains(self, timestamp_ns: int) -> bool:
        return self.start_ns <= timestamp_ns <= self.end_ns
    
    def duration_ms(self) -> float:
        return (self.end_ns - self.start_ns) / 1_000_000

@dataclass(frozen=True)
class TokenBudget:
    """Token budget for LLM-aware analysis"""
    max_tokens: int
    current_tokens: int = 0
    
    def remaining(self) -> int:
        return self.max_tokens - self.current_tokens
    
    def can_fit(self, tokens: int) -> bool:
        return self.remaining() >= tokens
    
    def consume(self, tokens: int) -> 'TokenBudget':
        return TokenBudget(self.max_tokens, self.current_tokens + tokens)

# ============================================================================
# Event Data Structures
# ============================================================================

@dataclass(frozen=True)
class IndexEvent:
    """Lightweight index event (fixed 32 bytes)"""
    timestamp_ns: int      # 8 bytes
    thread_id: int        # 4 bytes
    function_id: int      # 4 bytes  
    event_type: EventType # 4 bytes
    detail_offset: int    # 8 bytes (offset to detail if exists)
    flags: int           # 4 bytes
    
    def has_detail(self) -> bool:
        return self.detail_offset != 0

@dataclass(frozen=True)
class DetailEvent:
    """Rich detail event (variable size)"""
    index_event: IndexEvent
    call_stack: List[int]      # Function IDs
    arguments: Dict[str, Any]   # Serialized arguments
    return_value: Optional[Any] # Serialized return
    metadata: Dict[str, Any]    # Additional data
    
    def estimate_tokens(self) -> int:
        """Estimate token count for LLM processing"""
        # Rough estimation: 1 token per 4 characters
        text_size = len(str(self.arguments)) + len(str(self.return_value))
        return text_size // 4

# ============================================================================
# ATF Format Interface
# ============================================================================

class ATFReader(Protocol):
    """Protocol for reading ADA Trace Format files"""
    
    def open(self, path: Path) -> None:
        """Open an ATF file for reading"""
        ...
    
    def close(self) -> None:
        """Close the ATF file"""
        ...
    
    def get_metadata(self) -> Dict[str, Any]:
        """Get trace metadata (process info, timestamps, etc)"""
        ...
    
    def get_time_range(self) -> TimeRange:
        """Get time range covered by trace"""
        ...
    
    def get_thread_ids(self) -> List[int]:
        """Get all thread IDs in trace"""
        ...
    
    def read_index_events(self, 
                         time_range: Optional[TimeRange] = None,
                         thread_ids: Optional[List[int]] = None) -> Iterator[IndexEvent]:
        """Stream index events with optional filtering"""
        ...
    
    def read_detail_event(self, offset: int) -> DetailEvent:
        """Read a specific detail event by offset"""
        ...
    
    def estimate_event_count(self, 
                            time_range: Optional[TimeRange] = None) -> int:
        """Estimate number of events in range"""
        ...

class ATFWriter(Protocol):
    """Protocol for writing ADA Trace Format files"""
    
    def open(self, path: Path, metadata: Dict[str, Any]) -> None:
        """Open an ATF file for writing"""
        ...
    
    def close(self) -> None:
        """Close and finalize the ATF file"""
        ...
    
    def write_index_event(self, event: IndexEvent) -> None:
        """Write an index event"""
        ...
    
    def write_detail_event(self, event: DetailEvent) -> int:
        """Write a detail event, return offset"""
        ...
    
    def flush(self) -> None:
        """Flush buffers to disk"""
        ...

# ============================================================================
# Query Engine Interface
# ============================================================================

@dataclass
class QueryRequest:
    """Query request with token budget awareness"""
    query: str                          # Natural language or structured query
    time_range: Optional[TimeRange]    # Time filter
    thread_ids: Optional[List[int]]    # Thread filter  
    token_budget: TokenBudget          # LLM token budget
    include_details: bool = False      # Include detail events
    max_events: Optional[int] = None   # Limit event count

@dataclass
class QueryResult:
    """Query result with token accounting"""
    events: List[Union[IndexEvent, DetailEvent]]
    summary: str                       # Human-readable summary
    tokens_used: int                  # Tokens consumed
    truncated: bool                   # Was result truncated for budget?
    execution_time_ms: float          # Query execution time

class QueryEngine(ABC):
    """Abstract base for query engine implementations"""
    
    @abstractmethod
    async def initialize(self, trace_path: Path) -> None:
        """Initialize engine with trace file"""
        ...
    
    @abstractmethod
    async def execute_query(self, request: QueryRequest) -> QueryResult:
        """Execute a query with token budget awareness"""
        ...
    
    @abstractmethod
    async def get_summary(self, token_budget: TokenBudget) -> str:
        """Get trace summary within token budget"""
        ...
    
    @abstractmethod
    async def find_anomalies(self, token_budget: TokenBudget) -> QueryResult:
        """Find anomalies in trace within budget"""
        ...
    
    @abstractmethod
    async def close(self) -> None:
        """Close the query engine"""
        ...

# ============================================================================
# Analysis Interface
# ============================================================================

class TraceAnalyzer(Protocol):
    """Protocol for trace analysis algorithms"""
    
    def analyze_performance(self, 
                           events: List[IndexEvent]) -> Dict[str, Any]:
        """Analyze performance characteristics"""
        ...
    
    def detect_bottlenecks(self,
                          events: List[IndexEvent],
                          threshold_ms: float = 100) -> List[Tuple[int, float]]:
        """Detect performance bottlenecks"""
        ...
    
    def build_call_graph(self,
                        events: List[IndexEvent]) -> Dict[int, List[int]]:
        """Build function call graph"""
        ...
    
    def compute_statistics(self,
                          events: List[IndexEvent]) -> Dict[str, float]:
        """Compute trace statistics"""
        ...

# ============================================================================
# Token Estimation Interface
# ============================================================================

class TokenEstimator(Protocol):
    """Protocol for estimating LLM token usage"""
    
    def estimate_event_tokens(self, event: Union[IndexEvent, DetailEvent]) -> int:
        """Estimate tokens for a single event"""
        ...
    
    def estimate_summary_tokens(self, num_events: int) -> int:
        """Estimate tokens for summarizing N events"""
        ...
    
    def estimate_query_tokens(self, query: str) -> int:
        """Estimate tokens for query string"""
        ...

# ============================================================================
# Index Building Interface
# ============================================================================

class TraceIndex(Protocol):
    """Protocol for trace indexing"""
    
    def build_time_index(self, events: List[IndexEvent]) -> None:
        """Build time-based index"""
        ...
    
    def build_function_index(self, events: List[IndexEvent]) -> None:
        """Build function-based index"""
        ...
    
    def build_thread_index(self, events: List[IndexEvent]) -> None:
        """Build thread-based index"""
        ...
    
    def query_time_range(self, time_range: TimeRange) -> List[int]:
        """Query events in time range, return indices"""
        ...
    
    def query_function(self, function_id: int) -> List[int]:
        """Query events for function, return indices"""
        ...

# ============================================================================
# Factory Functions
# ============================================================================

def create_atf_reader() -> ATFReader:
    """Create default ATF reader implementation"""
    from atf.reader import ATFReader as DefaultATFReader

    return DefaultATFReader()

def create_atf_writer() -> ATFWriter:
    """Create default ATF writer implementation"""
    from .atf_writer_impl import DefaultATFWriter
    return DefaultATFWriter()

def create_query_engine() -> QueryEngine:
    """Create default query engine implementation"""
    from .query_engine_impl import DefaultQueryEngine
    return DefaultQueryEngine()

def create_token_estimator() -> TokenEstimator:
    """Create default token estimator"""
    from .token_estimator_impl import DefaultTokenEstimator
    return DefaultTokenEstimator()

def create_trace_analyzer() -> TraceAnalyzer:
    """Create default trace analyzer"""
    from .trace_analyzer_impl import DefaultTraceAnalyzer
    return DefaultTraceAnalyzer()

# ============================================================================
# Interface Validation
# ============================================================================

def validate_interfaces():
    """Validate that all interfaces are properly defined"""
    # This ensures interfaces compile at import time
    required_protocols = [
        ATFReader, ATFWriter, TraceAnalyzer, 
        TokenEstimator, TraceIndex
    ]
    
    required_classes = [
        QueryEngine, EventType, LaneType,
        TimeRange, TokenBudget, IndexEvent, DetailEvent,
        QueryRequest, QueryResult
    ]
    
    for protocol in required_protocols:
        assert hasattr(protocol, '__annotations__')
    
    for cls in required_classes:
        assert cls is not None
    
    return True

# Run validation at import
assert validate_interfaces()
