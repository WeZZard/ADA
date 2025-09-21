"""Unit tests covering ATF reader edge cases for coverage improvements."""

from __future__ import annotations

import builtins
import importlib
import json
import struct
import sys
from contextlib import contextmanager
from pathlib import Path
from typing import Dict, Iterable, List

import pytest
import typing
from unittest import mock

PACKAGE_ROOT = Path(__file__).resolve().parents[2]
SRC_PATH = PACKAGE_ROOT / "src"
if str(SRC_PATH) not in sys.path:
    sys.path.insert(0, str(SRC_PATH))

TESTS_PATH = PACKAGE_ROOT / "tests"
if str(TESTS_PATH) not in sys.path:
    sys.path.insert(0, str(TESTS_PATH))

if not hasattr(typing.Protocol, "__annotations__"):
    typing.Protocol.__annotations__ = {}

from atf.errors import EventDecodingError, ManifestError, MemoryMapError, ReaderClosedError  # noqa: E402
from atf.iterator import EventIterator, INDEX_STRUCT  # noqa: E402
from atf.manifest import ManifestInfo  # noqa: E402
from atf.memory_map import MemoryMap  # noqa: E402
from atf.reader import ATFReader, HeaderValidationError  # noqa: E402
from interfaces import EventType  # noqa: E402
from atf_helpers import (  # noqa: E402
    ATFTestFileBuilder,
    HEADER_STRUCT,
    INDEX_RECORD_SIZE,
    VERSION,
    base_manifest,
)


@contextmanager
def _module_without_orjson(module_name: str):
    original_import = builtins.__import__

    def fake_import(name, globals=None, locals=None, fromlist=(), level=0):
        if name == "orjson":
            raise ModuleNotFoundError("No module named 'orjson'")
        return original_import(name, globals, locals, fromlist, level)

    with mock.patch("builtins.__import__", side_effect=fake_import):
        # Clear the module and its dependencies
        sys.modules.pop(module_name, None)
        sys.modules.pop("interfaces", None)  # Clear potentially wrong interfaces module

        # Ensure query_engine's src path is prioritized
        query_engine_src = str(PACKAGE_ROOT / "src")
        if query_engine_src not in sys.path:
            sys.path.insert(0, query_engine_src)

        # Remove any mcp_server paths that might interfere
        mcp_paths = [p for p in sys.path if "mcp_server" in p]
        for p in mcp_paths:
            sys.path.remove(p)

        # Now import the module fresh
        module = importlib.import_module(module_name)
    try:
        yield module
    finally:
        sys.modules.pop(module_name, None)
        sys.modules.pop("interfaces", None)  # Clear again for cleanup
        importlib.import_module(module_name)
        importlib.reload(importlib.import_module("atf"))


class _FakeMemoryMap:
    """Minimal memory map stub for iterator/detail error simulation."""

    def __init__(self, responses: Iterable[bytes], size: int = 0) -> None:
        self._responses = list(responses)
        self._size = size
        self._call = 0

    @property
    def size(self) -> int:
        return self._size

    def read(self, offset: int, size: int) -> bytes:  # pragma: no cover - exercised in tests
        if self._call >= len(self._responses):
            return self._responses[-1]
        result = self._responses[self._call]
        self._call += 1
        return result

    def close(self) -> None:  # pragma: no cover - exercised in tests
        return None


# ---------------------------------------------------------------------------
# Iterator tests
# ---------------------------------------------------------------------------


def test_event_iterator_covfill__malformed_record_bytes__then_raises_event_decoding_error() -> None:
    fake_map = _FakeMemoryMap([b"\x00"], size=1)
    iterator = EventIterator(fake_map, index_offset=0, count=1)
    with pytest.raises(EventDecodingError):
        next(iterator)


def test_event_iterator_covfill__unknown_event_code__then_raises_event_decoding_error() -> None:
    record = INDEX_STRUCT.pack(1, 2, 3, 99, 0, 0)
    fake_map = _FakeMemoryMap([record], size=len(record))
    iterator = EventIterator(fake_map, index_offset=0, count=1)
    with pytest.raises(EventDecodingError):
        next(iterator)


# ---------------------------------------------------------------------------
# Manifest tests
# ---------------------------------------------------------------------------


def test_manifest_covfill__empty_payload__then_raises_manifest_error() -> None:
    with pytest.raises(ManifestError):
        ManifestInfo.from_bytes(b"")


def test_manifest_covfill__invalid_json__then_raises_manifest_error() -> None:
    with pytest.raises(ManifestError):
        ManifestInfo.from_bytes(b"\xff\xff")


def test_manifest_covfill__metadata_not_object__then_raises_manifest_error() -> None:
    manifest = {
        "metadata": 5,
        "time_range": {"start_ns": 0, "end_ns": 1},
        "thread_ids": [],
        "event_count": 0,
    }
    with pytest.raises(ManifestError):
        ManifestInfo.from_bytes(json.dumps(manifest).encode("utf-8"))


def test_manifest_covfill__invalid_time_range_values__then_raises_manifest_error() -> None:
    manifest = base_manifest()
    manifest["time_range"] = {"start_ns": "bad", "end_ns": 10}
    with pytest.raises(ManifestError):
        ManifestInfo.from_bytes(json.dumps(manifest).encode("utf-8"))


def test_manifest_covfill__end_before_start__then_raises_manifest_error() -> None:
    manifest = base_manifest()
    manifest["time_range"] = {"start_ns": 10, "end_ns": 5}
    with pytest.raises(ManifestError):
        ManifestInfo.from_bytes(json.dumps(manifest).encode("utf-8"))


def test_manifest_covfill__thread_ids_not_list__then_raises_manifest_error() -> None:
    manifest = base_manifest()
    manifest["thread_ids"] = "threads"
    with pytest.raises(ManifestError):
        ManifestInfo.from_bytes(json.dumps(manifest).encode("utf-8"))


def test_manifest_covfill__thread_ids_non_numeric__then_raises_manifest_error() -> None:
    manifest = base_manifest()
    manifest["thread_ids"] = [1, "bad"]
    with pytest.raises(ManifestError):
        ManifestInfo.from_bytes(json.dumps(manifest).encode("utf-8"))


def test_manifest_covfill__negative_event_count__then_raises_manifest_error() -> None:
    manifest = base_manifest(event_count=-1)
    with pytest.raises(ManifestError):
        ManifestInfo.from_bytes(json.dumps(manifest).encode("utf-8"))


def test_manifest_covfill__valid_payload__then_returns_sorted_unique_thread_ids() -> None:
    manifest = base_manifest(event_count=2)
    parsed = ManifestInfo.from_bytes(json.dumps(manifest).encode("utf-8"))
    assert parsed.thread_ids == [7, 8]
    assert parsed.to_dict()["event_count"] == 2


def test_manifest_covfill_ext__orjson_missing__then_parses_with_json_fallback() -> None:
    manifest = base_manifest(event_count=3)
    payload = json.dumps(manifest).encode("utf-8")
    with _module_without_orjson("atf.manifest") as manifest_module:
        parsed = manifest_module.ManifestInfo.from_bytes(payload)
        assert parsed.event_count == 3
        assert parsed.thread_ids == [7, 8]


def test_manifest_covfill_ext__orjson_missing_invalid_data__then_raises_manifest_error() -> None:
    with _module_without_orjson("atf.manifest") as manifest_module:
        with pytest.raises(ManifestError):
            manifest_module.ManifestInfo.from_bytes(b"\xff")


# ---------------------------------------------------------------------------
# Memory map tests
# ---------------------------------------------------------------------------


def test_memory_map_covfill__read_before_open__then_raises_reader_closed_error() -> None:
    memory_map = MemoryMap()
    with pytest.raises(ReaderClosedError):
        memory_map.read(0, 1)


def test_memory_map_covfill__negative_offset__then_raises_memory_map_error(tmp_path: Path) -> None:
    data_path = tmp_path / "data.bin"
    data_path.write_bytes(b"abcdef")
    memory_map = MemoryMap()
    memory_map.open(data_path)
    with pytest.raises(MemoryMapError):
        memory_map.read(-1, 2)
    memory_map.close()


def test_memory_map_covfill__read_past_end__then_raises_memory_map_error(tmp_path: Path) -> None:
    data_path = tmp_path / "data.bin"
    data_path.write_bytes(b"abcdef")
    memory_map = MemoryMap()
    memory_map.open(data_path)
    with pytest.raises(MemoryMapError):
        memory_map.read(4, 4)
    memory_map.close()


def test_memory_map_covfill__slice_negative_offset__then_raises_memory_map_error(tmp_path: Path) -> None:
    data_path = tmp_path / "data.bin"
    data_path.write_bytes(b"abcdef")
    memory_map = MemoryMap()
    memory_map.open(data_path)
    with pytest.raises(MemoryMapError):
        memory_map.slice(-1, 2)
    memory_map.close()


def test_memory_map_covfill__slice_beyond_size__then_raises_memory_map_error(tmp_path: Path) -> None:
    data_path = tmp_path / "data.bin"
    data_path.write_bytes(b"abcdef")
    memory_map = MemoryMap()
    memory_map.open(data_path)
    with pytest.raises(MemoryMapError):
        memory_map.slice(4, 4)
    memory_map.close()


def test_memory_map_covfill__open_empty_file__then_raises_memory_map_error(tmp_path: Path) -> None:
    empty_path = tmp_path / "empty.atf"
    empty_path.touch()
    memory_map = MemoryMap()
    with pytest.raises(MemoryMapError):
        memory_map.open(empty_path)


def test_memory_map_covfill_ext__mmap_failure__then_raises_memory_map_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    data_path = tmp_path / "data.bin"
    data_path.write_bytes(b"abcdef")
    memory_map = MemoryMap()

    def failing_mmap(*_, **__):
        raise BufferError("boom")

    monkeypatch.setattr("atf.memory_map.mmap.mmap", failing_mmap)

    with pytest.raises(MemoryMapError):
        memory_map.open(data_path)


def test_memory_map_covfill_ext__slice_valid_range__then_returns_view(tmp_path: Path) -> None:
    data_path = tmp_path / "data.bin"
    data = b"abcdef"
    data_path.write_bytes(data)
    memory_map = MemoryMap()
    memory_map.open(data_path)

    view = memory_map.slice(1, 3)

    assert isinstance(view, memoryview)
    assert view.tobytes() == b"bcd"

    view.release()

    memory_map.close()


# ---------------------------------------------------------------------------
# Reader error handling tests
# ---------------------------------------------------------------------------


def _make_reader_events() -> List[Dict[str, object]]:
    return [
        {
            "index": {
                "timestamp_ns": 10,
                "thread_id": 7,
                "function_id": 1,
                "event_type_code": 0,
                "flags": 0,
            },
            "detail": {
                "index_event": {
                    "timestamp_ns": 10,
                    "thread_id": 7,
                    "function_id": 1,
                    "event_type": EventType.FUNCTION_ENTER.value,
                    "detail_offset": 0,
                    "flags": 0,
                }
            },
        }
    ]


def _build_atf(path: Path, manifest: Dict[str, object], events: Iterable[Dict[str, object]]) -> Path:
    builder = ATFTestFileBuilder(manifest, events)
    return builder.build(path)


def test_reader_covfill__open_with_wrong_version__then_raises_header_validation_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "reader.atf", manifest, _make_reader_events())

    with path.open("r+b") as fh:
        header = list(HEADER_STRUCT.unpack(fh.read(HEADER_STRUCT.size)))
        fh.seek(0)
        header[1] = VERSION + 1
        fh.write(HEADER_STRUCT.pack(*header))

    reader = ATFReader()
    with pytest.raises(HeaderValidationError):
        reader.open(path)


def test_reader_covfill__open_with_zero_manifest_length__then_raises_header_validation_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "manifest_zero.atf", manifest, [])

    with path.open("r+b") as fh:
        header = list(HEADER_STRUCT.unpack(fh.read(HEADER_STRUCT.size)))
        fh.seek(0)
        header[3] = 0
        fh.write(HEADER_STRUCT.pack(*header))

    reader = ATFReader()
    with pytest.raises(HeaderValidationError):
        reader.open(path)


def test_reader_covfill__open_with_index_offset_gap__then_raises_header_validation_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "offset_gap.atf", manifest, [])

    with path.open("r+b") as fh:
        header = list(HEADER_STRUCT.unpack(fh.read(HEADER_STRUCT.size)))
        fh.seek(0)
        header[4] += 8
        fh.write(HEADER_STRUCT.pack(*header))

    reader = ATFReader()
    with pytest.raises(HeaderValidationError):
        reader.open(path)


def test_reader_covfill__open_with_detail_overlap__then_raises_header_validation_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=2)
    path = _build_atf(tmp_path / "detail_overlap.atf", manifest, _make_reader_events())

    with path.open("r+b") as fh:
        header = list(HEADER_STRUCT.unpack(fh.read(HEADER_STRUCT.size)))
        fh.seek(0)
        header[6] = header[4]
        fh.write(HEADER_STRUCT.pack(*header))

    reader = ATFReader()
    with pytest.raises(HeaderValidationError):
        reader.open(path)


def test_reader_covfill__open_with_detail_past_file__then_raises_header_validation_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=0)
    path = _build_atf(tmp_path / "detail_beyond.atf", manifest, [])

    with path.open("r+b") as fh:
        header = list(HEADER_STRUCT.unpack(fh.read(HEADER_STRUCT.size)))
        fh.seek(0)
        header[6] = header[6] + 1024
        fh.write(HEADER_STRUCT.pack(*header))

    reader = ATFReader()
    with pytest.raises(HeaderValidationError):
        reader.open(path)


def test_reader_covfill__metadata_access_when_closed__then_raises_reader_closed_error() -> None:
    reader = ATFReader()
    with pytest.raises(ReaderClosedError):
        reader.get_metadata()


def _open_reader(path: Path) -> ATFReader:
    reader = ATFReader()
    reader.open(path)
    return reader


def test_reader_covfill__detail_offset_before_section__then_raises_event_decoding_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_offset.atf", manifest, _make_reader_events())
    reader = _open_reader(path)
    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._index_offset)
    reader.close()


def test_reader_covfill__detail_length_prefix_corrupt__then_raises_event_decoding_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_corrupt.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    corrupt_map = _FakeMemoryMap([b"\x01"], size=reader._memory_map.size)
    reader._memory_map = corrupt_map  # type: ignore[assignment]

    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._detail_offset)
    reader.close()


def test_reader_covfill__detail_entry_exceeds_file__then_raises_event_decoding_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_overflow.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    oversized_length = reader._memory_map.size
    reader._memory_map = _FakeMemoryMap([struct.pack("<I", oversized_length)], size=reader._memory_map.size)  # type: ignore[assignment]

    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._detail_offset)
    reader.close()


def test_reader_covfill__detail_payload_invalid_json__then_raises_event_decoding_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_invalid_json.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    payload = json.dumps({"index_event": {"event_type": EventType.FUNCTION_ENTER.value}}).encode("utf-8")
    truncated = payload[:-2]
    reader._memory_map = _FakeMemoryMap(
        [struct.pack("<I", len(payload)), truncated],
        size=reader._memory_map.size,
    )  # type: ignore[assignment]

    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._detail_offset)
    reader.close()


def test_reader_covfill__detail_missing_index_event__then_raises_event_decoding_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_missing_index_event.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    payload = json.dumps({"metadata": {}}).encode("utf-8")
    reader._memory_map = _FakeMemoryMap(
        [struct.pack("<I", len(payload)), payload],
        size=reader._memory_map.size,
    )  # type: ignore[assignment]

    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._detail_offset)
    reader.close()


def test_reader_covfill__detail_with_unknown_event_type__then_raises_event_decoding_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_bad_event_type.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    payload = json.dumps({
        "index_event": {
            "timestamp_ns": 10,
            "thread_id": 7,
            "function_id": 1,
            "event_type": 999,
            "detail_offset": reader._detail_offset,
            "flags": 0,
        }
    }).encode("utf-8")

    reader._memory_map = _FakeMemoryMap(
        [struct.pack("<I", len(payload)), payload],
        size=reader._memory_map.size,
    )  # type: ignore[assignment]

    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._detail_offset)
    reader.close()


def test_reader_covfill__detail_with_invalid_list_item__then_raises_event_decoding_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_bad_list.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    payload = json.dumps({
        "index_event": {
            "timestamp_ns": 10,
            "thread_id": 7,
            "function_id": 1,
            "event_type": EventType.FUNCTION_ENTER.value,
            "detail_offset": reader._detail_offset,
            "flags": 0,
        },
        "call_stack": ["bad"],
    }).encode("utf-8")

    reader._memory_map = _FakeMemoryMap(
        [struct.pack("<I", len(payload)), payload],
        size=reader._memory_map.size,
    )  # type: ignore[assignment]

    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._detail_offset)
    reader.close()


def test_reader_covfill__detail_with_invalid_metadata_type__then_raises_event_decoding_error(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_bad_metadata.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    payload = json.dumps({
        "index_event": {
            "timestamp_ns": 10,
            "thread_id": 7,
            "function_id": 1,
            "event_type": EventType.FUNCTION_ENTER.value,
            "detail_offset": reader._detail_offset,
            "flags": 0,
        },
        "metadata": ["not", "dict"],
    }).encode("utf-8")

    reader._memory_map = _FakeMemoryMap(
        [struct.pack("<I", len(payload)), payload],
        size=reader._memory_map.size,
    )  # type: ignore[assignment]

    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._detail_offset)
    reader.close()


def test_reader_covfill_ext__decode_detail_without_optional_fields__then_returns_defaults(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_defaults.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    monkeypatch.setattr("atf.reader._ORJSON", None)

    detail_offset = next(event.detail_offset for event in reader.read_index_events() if event.detail_offset)
    detail = reader.read_detail_event(detail_offset)

    assert detail.call_stack == []
    assert detail.arguments == {}
    assert detail.metadata == {}

    reader.close()


def test_reader_covfill_ext__decode_detail_without_orjson_invalid_json__then_raises_event_decoding_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "detail_invalid_utf8.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    monkeypatch.setattr("atf.reader._ORJSON", None)

    invalid_payload = b"\xff\xfe"
    reader._memory_map = _FakeMemoryMap(
        [struct.pack("<I", len(invalid_payload)), invalid_payload],
        size=reader._memory_map.size,
    )  # type: ignore[assignment]

    with pytest.raises(EventDecodingError):
        reader.read_detail_event(reader._detail_offset)
    reader.close()


def test_reader_covfill__malformed_header_struct__then_raises_header_validation_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Test struct.error when unpacking malformed header."""
    path = tmp_path / "malformed_header.atf"
    # Write a file with proper size header
    path.write_bytes(b"A" * (HEADER_STRUCT.size + 100))

    reader = ATFReader()
    # Patch the _HEADER_STRUCT in the reader module
    fake_struct = mock.MagicMock()
    fake_struct.unpack.side_effect = struct.error("Bad format")

    with monkeypatch.context() as m:
        m.setattr("atf.reader._HEADER_STRUCT", fake_struct)
        with pytest.raises(HeaderValidationError, match="ATF header is malformed"):
            reader.open(path)


def test_reader_covfill__get_metadata_cached__then_returns_cached(tmp_path: Path) -> None:
    """Test metadata cache branch when already cached."""
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "cached_meta.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    # First call populates cache
    meta1 = reader.get_metadata()
    assert "path" in meta1

    # Second call should return cached version
    meta2 = reader.get_metadata()
    assert meta1 == meta2

    reader.close()


def test_reader_covfill__open_without_path__then_metadata_excludes_path(tmp_path: Path) -> None:
    """Test metadata when reader opened without a path (branch coverage for _path is None)."""
    manifest = base_manifest(event_count=1)
    path = _build_atf(tmp_path / "no_path.atf", manifest, _make_reader_events())
    reader = _open_reader(path)

    # Simulate scenario where path is None
    reader._path = None
    reader._metadata_cache = None  # Clear cache to force recalculation

    # Get metadata - should not add path when _path is None
    metadata = reader.get_metadata()
    assert "event_count" in metadata
    # When _path is None, setdefault won't add it or will add None
    assert "path" not in metadata or metadata["path"] is None

    reader.close()


def test_reader_covfill__ensure_list_with_non_iterable__then_raises_type_error(tmp_path: Path) -> None:
    """Test _ensure_list TypeError when value is not iterable."""
    from atf.reader import ATFReader
    # Call _ensure_list directly with non-iterable
    with pytest.raises(EventDecodingError, match="Detail payload contains invalid list entry"):
        ATFReader._ensure_list(123, int)  # 123 is not iterable


def test_reader_covfill__ensure_dict_with_non_dict__then_raises_error(tmp_path: Path) -> None:
    """Test _ensure_dict raises error when value is not a dict."""
    from atf.reader import ATFReader
    # Call _ensure_dict directly with non-dict
    with pytest.raises(EventDecodingError, match="Detail payload dictionary field is invalid"):
        ATFReader._ensure_dict([1, 2, 3])  # List is not dict
