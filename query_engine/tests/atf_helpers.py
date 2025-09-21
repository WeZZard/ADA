"""Shared helpers for constructing synthetic ATF fixtures in tests."""

from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Dict, Iterable, List, Optional

HEADER_STRUCT = struct.Struct("<4sHHQQQQ")
MAGIC = b"ATF0"
VERSION = 1
INDEX_STRUCT = struct.Struct("<QIIIqI")
INDEX_RECORD_SIZE = INDEX_STRUCT.size


class ATFTestFileBuilder:
    """Helper for constructing ATF files for testing."""

    def __init__(self, manifest: Dict[str, object], events: Iterable[Dict[str, object]]) -> None:
        self.manifest = manifest
        self.events = list(events)

    def build(self, path: Path) -> Path:
        manifest_bytes = json.dumps(self.manifest).encode("utf-8")
        index_count = len(self.events)
        header_size = HEADER_STRUCT.size
        manifest_offset = header_size
        index_offset = manifest_offset + len(manifest_bytes)
        detail_offset = index_offset + index_count * INDEX_RECORD_SIZE

        index_records: List[bytes] = []
        detail_records = bytearray()
        current_detail_offset = detail_offset

        for event in self.events:
            index_info = event["index"]
            detail_info: Optional[Dict[str, object]] = event.get("detail")

            detail_pointer = 0
            if detail_info is not None:
                payload_bytes = json.dumps(detail_info).encode("utf-8")
                detail_pointer = current_detail_offset
                detail_records.extend(struct.pack("<I", len(payload_bytes)))
                detail_records.extend(payload_bytes)
                current_detail_offset += 4 + len(payload_bytes)

            event_type_code = event.get("event_type_code")
            if event_type_code is None:
                event_type_code = index_info["event_type_code"]

            index_records.append(
                INDEX_STRUCT.pack(
                    index_info["timestamp_ns"],
                    index_info["thread_id"],
                    index_info["function_id"],
                    event_type_code,
                    detail_pointer,
                    index_info.get("flags", 0),
                )
            )

        with path.open("wb") as file_handle:
            file_handle.write(b"\x00" * HEADER_STRUCT.size)
            file_handle.write(manifest_bytes)
            for record in index_records:
                file_handle.write(record)
            file_handle.write(detail_records)
            file_handle.seek(0)
            header_bytes = HEADER_STRUCT.pack(
                MAGIC,
                VERSION,
                0,
                len(manifest_bytes),
                index_offset,
                index_count,
                detail_offset,
            )
            file_handle.write(header_bytes)

        return path


def base_manifest(event_count: int = 1) -> Dict[str, object]:
    return {
        "metadata": {"source": "tests"},
        "time_range": {"start_ns": 10, "end_ns": 20},
        "thread_ids": [7, 7, 8],
        "event_count": event_count,
    }
