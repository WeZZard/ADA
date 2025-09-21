"""High-performance ADA Trace Format reader implementation."""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional

from interfaces import (
    ATFReader as ATFReaderProtocol,
    DetailEvent,
    EventType,
    IndexEvent,
    TimeRange,
)
from .errors import (
    ATFError,
    EventDecodingError,
    HeaderValidationError,
    ReaderClosedError,
)
from .iterator import EventIterator, INDEX_RECORD_SIZE, INDEX_STRUCT, _EVENT_TYPE_BY_CODE
from .manifest import ManifestInfo
from .memory_map import MemoryMap

_HEADER_STRUCT = struct.Struct("<4sHHQQQQ")
_HEADER_SIZE = _HEADER_STRUCT.size
_MAGIC = b"ATF0"
_VERSION = 1

EVENT_TYPE_TO_CODE: Dict[EventType, int] = {
    EventType.FUNCTION_ENTER: 0,
    EventType.FUNCTION_EXIT: 1,
    EventType.MEMORY_ALLOC: 2,
    EventType.MEMORY_FREE: 3,
    EventType.SYSCALL: 4,
    EventType.EXCEPTION: 5,
    EventType.CUSTOM: 6,
}
CODE_TO_EVENT_TYPE: Dict[int, EventType] = {code: event for event, code in EVENT_TYPE_TO_CODE.items()}

class ATFReader(ATFReaderProtocol):
    """Read-only ATF reader backed by memory mapping."""

    __slots__ = (
        "_memory_map",
        "_manifest",
        "_index_offset",
        "_index_count",
        "_detail_offset",
        "_metadata_cache",
        "_path",
    )

    def __init__(self) -> None:
        self._memory_map = MemoryMap()
        self._manifest: Optional[ManifestInfo] = None
        self._index_offset = 0
        self._index_count = 0
        self._detail_offset = 0
        self._metadata_cache: Optional[Dict[str, object]] = None
        self._path: Optional[Path] = None

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def open(self, path: Path) -> None:
        """Open the requested ATF file."""
        self._memory_map.open(path)
        self._path = path
        header = self._memory_map.read(0, _HEADER_SIZE)
        try:
            (
                magic,
                version,
                flags,
                manifest_length,
                index_offset,
                index_count,
                detail_offset,
            ) = _HEADER_STRUCT.unpack(header)
        except struct.error as exc:
            raise HeaderValidationError("ATF header is malformed") from exc

        if magic != _MAGIC:
            raise HeaderValidationError("ATF file magic does not match")
        if version != _VERSION:
            raise HeaderValidationError(
                f"Unsupported ATF version {version}, expected {_VERSION}"
            )
        if manifest_length == 0:
            raise HeaderValidationError("Manifest length is zero")

        manifest_offset = _HEADER_SIZE
        expected_index_offset = manifest_offset + manifest_length
        if index_offset != expected_index_offset:
            raise HeaderValidationError("Index offset does not follow manifest")

        index_section_length = index_count * INDEX_RECORD_SIZE
        expected_detail_offset = index_offset + index_section_length
        if detail_offset < expected_detail_offset:
            raise HeaderValidationError("Detail section overlaps index section")

        if detail_offset > self._memory_map.size:
            raise HeaderValidationError("Detail section offset exceeds file size")

        manifest_bytes = self._memory_map.read(manifest_offset, manifest_length)
        self._manifest = ManifestInfo.from_bytes(manifest_bytes)
        if self._manifest.event_count == 0:
            self._manifest = ManifestInfo(
                metadata=self._manifest.metadata,
                time_range=self._manifest.time_range,
                thread_ids=self._manifest.thread_ids,
                event_count=index_count,
            )

        self._index_offset = index_offset
        self._index_count = index_count
        self._detail_offset = detail_offset
        self._metadata_cache = None

    def close(self) -> None:
        """Close the underlying file and release resources."""
        self._memory_map.close()
        self._manifest = None
        self._metadata_cache = None
        self._index_offset = 0
        self._index_count = 0
        self._detail_offset = 0
        self._path = None

    # ------------------------------------------------------------------
    # Accessors
    # ------------------------------------------------------------------

    def _ensure_open(self) -> None:
        if self._manifest is None:
            raise ReaderClosedError("ATF reader is not open")

    def get_metadata(self) -> Dict[str, object]:
        self._ensure_open()
        if self._metadata_cache is None and self._manifest is not None:
            self._metadata_cache = dict(self._manifest.metadata)
            if self._path is not None:
                self._metadata_cache.setdefault("path", str(self._path))
            self._metadata_cache.setdefault("event_count", self._index_count)
        return dict(self._metadata_cache)

    def get_time_range(self) -> TimeRange:
        self._ensure_open()
        assert self._manifest is not None
        return self._manifest.time_range

    def get_thread_ids(self) -> List[int]:
        self._ensure_open()
        assert self._manifest is not None
        return list(self._manifest.thread_ids)

    # ------------------------------------------------------------------
    # Iteration
    # ------------------------------------------------------------------

    def read_index_events(
        self,
        time_range: Optional[TimeRange] = None,
        thread_ids: Optional[List[int]] = None,
    ) -> Iterator[IndexEvent]:
        self._ensure_open()
        return EventIterator(
            self._memory_map,
            self._index_offset,
            self._index_count,
            time_range=time_range,
            thread_ids=thread_ids,
        )

    # ------------------------------------------------------------------
    # Detail access
    # ------------------------------------------------------------------

    def read_detail_event(self, offset: int) -> DetailEvent:
        self._ensure_open()
        if offset < self._detail_offset:
            raise EventDecodingError("Detail offset precedes detail section")
        header = self._memory_map.read(offset, 4)
        try:
            (payload_length,) = struct.unpack("<I", header)
        except struct.error as exc:
            raise EventDecodingError("Detail entry has invalid length prefix") from exc

        total_size = 4 + payload_length
        if offset + total_size > self._memory_map.size:
            raise EventDecodingError("Detail entry extends beyond file")

        payload = self._memory_map.read(offset + 4, payload_length)
        detail_dict = self._decode_detail_payload(payload)
        index_event_dict = detail_dict.get("index_event")
        if not isinstance(index_event_dict, dict):
            raise EventDecodingError("Detail payload missing index_event")

        try:
            event_type = EventType(index_event_dict["event_type"])
        except KeyError as exc:  # pragma: no cover - defensive
            raise EventDecodingError("Detail payload missing event_type") from exc
        except ValueError as exc:
            raise EventDecodingError("Unsupported event type in detail payload") from exc

        index_event = IndexEvent(
            timestamp_ns=int(index_event_dict["timestamp_ns"]),
            thread_id=int(index_event_dict["thread_id"]),
            function_id=int(index_event_dict["function_id"]),
            event_type=event_type,
            detail_offset=int(index_event_dict.get("detail_offset", offset)),
            flags=int(index_event_dict.get("flags", 0)),
        )

        return DetailEvent(
            index_event=index_event,
            call_stack=self._ensure_list(detail_dict.get("call_stack"), int),
            arguments=self._ensure_dict(detail_dict.get("arguments")),
            return_value=detail_dict.get("return_value"),
            metadata=self._ensure_dict(detail_dict.get("metadata")),
        )

    # ------------------------------------------------------------------
    # Utilities
    # ------------------------------------------------------------------

    def estimate_event_count(self, time_range: Optional[TimeRange] = None) -> int:
        self._ensure_open()
        if time_range is None:
            return int(self._index_count)
        return sum(1 for _ in self.read_index_events(time_range=time_range))

    # ------------------------------------------------------------------

    @staticmethod
    def _ensure_list(value: Optional[Iterable[int]], caster) -> List[int]:
        if value is None:
            return []
        try:
            return [caster(item) for item in value]
        except (TypeError, ValueError) as exc:
            raise EventDecodingError("Detail payload contains invalid list entry") from exc

    @staticmethod
    def _ensure_dict(value: Optional[Dict[str, object]]) -> Dict[str, object]:
        if value is None:
            return {}
        if not isinstance(value, dict):
            raise EventDecodingError("Detail payload dictionary field is invalid")
        return dict(value)

    @staticmethod
    def _decode_detail_payload(payload: bytes) -> Dict[str, object]:
        try:
            if _ORJSON is not None:  # pragma: no cover - depends on environment
                return _ORJSON.loads(payload)
        except _ORJSON.JSONDecodeError as exc:  # type: ignore[attr-defined]
            raise EventDecodingError("Detail payload contains invalid JSON") from exc
        try:
            return _JSON.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as exc:
            raise EventDecodingError("Detail payload contains invalid JSON") from exc

# ----------------------------------------------------------------------
# JSON helpers
# ----------------------------------------------------------------------

try:  # pragma: no cover - prefer orjson when available
    import orjson as _ORJSON
except ModuleNotFoundError:  # pragma: no cover - fallback path
    _ORJSON = None  # type: ignore

import json as _JSON

__all__ = [
    "ATFReader",
    "EVENT_TYPE_TO_CODE",
    "CODE_TO_EVENT_TYPE",
]
