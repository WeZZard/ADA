"""ATF reader module for ADA query engine."""

from .errors import (
    ATFError,
    ManifestError,
    MemoryMapError,
    EventDecodingError,
    ReaderClosedError,
    HeaderValidationError,
)
from .manifest import ManifestInfo
from .memory_map import MemoryMap
from .iterator import EventIterator, INDEX_RECORD_SIZE
from .reader import ATFReader, EVENT_TYPE_TO_CODE, CODE_TO_EVENT_TYPE

__all__ = [
    "ATFError",
    "ManifestError",
    "MemoryMapError",
    "EventDecodingError",
    "ReaderClosedError",
    "HeaderValidationError",
    "ManifestInfo",
    "MemoryMap",
    "EventIterator",
    "INDEX_RECORD_SIZE",
    "ATFReader",
    "EVENT_TYPE_TO_CODE",
    "CODE_TO_EVENT_TYPE",
]
