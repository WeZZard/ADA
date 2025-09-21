"""Iterators over ATF index events."""

from __future__ import annotations

import struct
from typing import Iterator, Optional, Iterable, Set

from interfaces import IndexEvent, EventType, TimeRange
from .errors import EventDecodingError
from .memory_map import MemoryMap

INDEX_STRUCT = struct.Struct("<QIIIqI")
INDEX_RECORD_SIZE = INDEX_STRUCT.size

_EVENT_TYPE_BY_CODE = {
    0: EventType.FUNCTION_ENTER,
    1: EventType.FUNCTION_EXIT,
    2: EventType.MEMORY_ALLOC,
    3: EventType.MEMORY_FREE,
    4: EventType.SYSCALL,
    5: EventType.EXCEPTION,
    6: EventType.CUSTOM,
}

class EventIterator(Iterator[IndexEvent]):
    """Iterator over index events with optional filtering."""

    __slots__ = (
        "_memory_map",
        "_index_offset",
        "_count",
        "_position",
        "_time_range",
        "_thread_filter",
    )

    def __init__(
        self,
        memory_map: MemoryMap,
        index_offset: int,
        count: int,
        *,
        time_range: Optional[TimeRange] = None,
        thread_ids: Optional[Iterable[int]] = None,
    ) -> None:
        self._memory_map = memory_map
        self._index_offset = index_offset
        self._count = max(count, 0)
        self._position = 0
        self._time_range = time_range
        self._thread_filter: Optional[Set[int]] = None
        if thread_ids is not None:
            self._thread_filter = {int(thread_id) for thread_id in thread_ids}

    def __iter__(self) -> "EventIterator":
        return self

    def __next__(self) -> IndexEvent:
        while self._position < self._count:
            current_offset = self._index_offset + self._position * INDEX_RECORD_SIZE
            raw = self._memory_map.read(current_offset, INDEX_RECORD_SIZE)
            self._position += 1
            try:
                (
                    timestamp_ns,
                    thread_id,
                    function_id,
                    event_type_code,
                    detail_offset,
                    flags,
                ) = INDEX_STRUCT.unpack(raw)
            except struct.error as exc:
                raise EventDecodingError(f"Malformed index record at offset {current_offset}") from exc

            event_type = _EVENT_TYPE_BY_CODE.get(event_type_code)
            if event_type is None:
                raise EventDecodingError(
                    f"Unknown event type code {event_type_code} at offset {current_offset}"
                )

            event = IndexEvent(
                timestamp_ns=timestamp_ns,
                thread_id=thread_id,
                function_id=function_id,
                event_type=event_type,
                detail_offset=detail_offset,
                flags=flags,
            )

            if self._time_range and not self._time_range.contains(timestamp_ns):
                continue
            if self._thread_filter and thread_id not in self._thread_filter:
                continue
            return event

        raise StopIteration

__all__ = ["EventIterator", "INDEX_RECORD_SIZE", "INDEX_STRUCT", "_EVENT_TYPE_BY_CODE"]
