"""Unit tests for the ATF reader implementation."""

import json
import struct
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional

import pytest

PROJECT_ROOT = Path(__file__).parent.parent
SRC_PATH = PROJECT_ROOT / "src"
if str(SRC_PATH) not in sys.path:
    sys.path.insert(0, str(SRC_PATH))

from atf import (  # noqa: E402
    ATFReader,
    EventDecodingError,
    HeaderValidationError,
    INDEX_RECORD_SIZE,
    EVENT_TYPE_TO_CODE,
)
from interfaces import EventType, TimeRange  # noqa: E402
from atf.iterator import INDEX_STRUCT  # noqa: E402

HEADER_STRUCT = struct.Struct("<4sHHQQQQ")
MAGIC = b"ATF0"
VERSION = 1

class ATFTestFileBuilder:
    """Helper for constructing synthetic ATF files for tests."""

    def __init__(self,
                 manifest: Dict[str, object],
                 events: Iterable[Dict[str, object]]) -> None:
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
                detail_pointer = current_detail_offset
                detail_payload = {
                    "index_event": {
                        "timestamp_ns": index_info["timestamp_ns"],
                        "thread_id": index_info["thread_id"],
                        "function_id": index_info["function_id"],
                        "event_type": index_info["event_type"].value,
                        "detail_offset": detail_pointer,
                        "flags": index_info["flags"],
                    },
                    "call_stack": detail_info.get("call_stack", []),
                    "arguments": detail_info.get("arguments", {}),
                    "return_value": detail_info.get("return_value"),
                    "metadata": detail_info.get("metadata", {}),
                }
                payload_bytes = json.dumps(detail_payload).encode("utf-8")
                detail_records.extend(struct.pack("<I", len(payload_bytes)))
                detail_records.extend(payload_bytes)
                current_detail_offset += 4 + len(payload_bytes)
            event_type_code = EVENT_TYPE_TO_CODE[index_info["event_type"]]
            index_record = INDEX_STRUCT.pack(
                index_info["timestamp_ns"],
                index_info["thread_id"],
                index_info["function_id"],
                event_type_code,
                detail_pointer,
                index_info["flags"],
            )
            index_records.append(index_record)

        with path.open("wb") as file_handle:
            file_handle.write(b"\x00" * HEADER_STRUCT.size)
            file_handle.write(manifest_bytes)
            for record in index_records:
                file_handle.write(record)
            file_handle.write(detail_records)
            file_handle.seek(0)
            header = HEADER_STRUCT.pack(
                MAGIC,
                VERSION,
                0,
                len(manifest_bytes),
                index_offset,
                index_count,
                detail_offset,
            )
            file_handle.write(header)

        return path

def _make_manifest(start_ns: int, end_ns: int, thread_ids: Iterable[int], event_count: int) -> Dict[str, object]:
    return {
        "metadata": {
            "process_name": "unit_tests",
            "host": "localhost",
        },
        "time_range": {
            "start_ns": start_ns,
            "end_ns": end_ns,
        },
        "thread_ids": list(thread_ids),
        "event_count": event_count,
    }

@pytest.fixture()
def sample_atf_file(tmp_path: Path) -> Path:
    manifest = _make_manifest(100, 500, [11, 12], 3)
    events = [
        {
            "index": {
                "timestamp_ns": 100,
                "thread_id": 11,
                "function_id": 1,
                "event_type": EventType.FUNCTION_ENTER,
                "flags": 0,
            },
            "detail": {
                "call_stack": [100, 200],
                "arguments": {"arg": 1},
                "return_value": None,
                "metadata": {"lane": "index"},
            },
        },
        {
            "index": {
                "timestamp_ns": 250,
                "thread_id": 12,
                "function_id": 2,
                "event_type": EventType.SYSCALL,
                "flags": 4,
            },
            "detail": None,
        },
        {
            "index": {
                "timestamp_ns": 480,
                "thread_id": 11,
                "function_id": 3,
                "event_type": EventType.CUSTOM,
                "flags": 1,
            },
            "detail": {
                "call_stack": [300],
                "arguments": {"payload": "hello"},
                "return_value": "done",
                "metadata": {"lane": "detail"},
            },
        },
    ]
    builder = ATFTestFileBuilder(manifest, events)
    path = tmp_path / "sample.atf"
    builder.build(path)
    return path

def test_ATFReader_read_index_events_should_return_all_events(sample_atf_file: Path) -> None:
    reader = ATFReader()
    reader.open(sample_atf_file)
    events = list(reader.read_index_events())
    assert len(events) == 3
    assert events[0].thread_id == 11
    assert events[1].event_type is EventType.SYSCALL
    assert events[2].detail_offset > 0
    reader.close()

def test_ATFReader_read_index_events_should_apply_filters(sample_atf_file: Path) -> None:
    reader = ATFReader()
    reader.open(sample_atf_file)
    filtered = list(reader.read_index_events(time_range=TimeRange(200, 400)))
    assert len(filtered) == 1
    assert filtered[0].timestamp_ns == 250

    filtered_threads = list(reader.read_index_events(thread_ids=[11]))
    assert len(filtered_threads) == 2
    reader.close()

def test_ATFReader_read_detail_event_should_return_full_detail(sample_atf_file: Path) -> None:
    reader = ATFReader()
    reader.open(sample_atf_file)
    first_event = next(reader.read_index_events())
    detail = reader.read_detail_event(first_event.detail_offset)
    assert detail.index_event.thread_id == 11
    assert detail.call_stack == [100, 200]
    assert detail.arguments["arg"] == 1
    assert detail.metadata["lane"] == "index"
    reader.close()

def test_ATFReader_read_detail_event_should_raise_on_invalid_offset(sample_atf_file: Path) -> None:
    reader = ATFReader()
    reader.open(sample_atf_file)
    with pytest.raises(EventDecodingError):
        reader.read_detail_event(24)
    reader.close()

def test_ATFReader_open_should_raise_on_corrupt_header(tmp_path: Path) -> None:
    manifest = _make_manifest(0, 1, [], 0)
    builder = ATFTestFileBuilder(manifest, [])
    path = tmp_path / "corrupt.atf"
    builder.build(path)
    with path.open("r+b") as fh:
        fh.write(b"BAD0")
    reader = ATFReader()
    with pytest.raises(HeaderValidationError):
        reader.open(path)

    reader.close()

def test_ATFReader_estimate_event_count_should_respect_filters(sample_atf_file: Path) -> None:
    reader = ATFReader()
    reader.open(sample_atf_file)
    assert reader.estimate_event_count() == 3
    assert reader.estimate_event_count(TimeRange(0, 200)) == 1
    reader.close()

def test_ATFReader_get_metadata_should_expose_manifest_data(sample_atf_file: Path) -> None:
    reader = ATFReader()
    reader.open(sample_atf_file)
    metadata = reader.get_metadata()
    assert metadata["process_name"] == "unit_tests"
    assert metadata["event_count"] == 3
    reader.close()
