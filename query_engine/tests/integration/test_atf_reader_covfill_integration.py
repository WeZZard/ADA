"""Integration tests validating ATF reader happy paths for coverage."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, Iterable, List
import typing

PACKAGE_ROOT = Path(__file__).resolve().parents[2]
SRC_PATH = PACKAGE_ROOT / "src"
if str(SRC_PATH) not in sys.path:
    sys.path.insert(0, str(SRC_PATH))

TESTS_PATH = PACKAGE_ROOT / "tests"
if str(TESTS_PATH) not in sys.path:
    sys.path.insert(0, str(TESTS_PATH))

if not hasattr(typing.Protocol, "__annotations__"):
    typing.Protocol.__annotations__ = {}

from atf.reader import ATFReader, EVENT_TYPE_TO_CODE  # noqa: E402
from atf_helpers import ATFTestFileBuilder, base_manifest  # noqa: E402
from interfaces import EventType, TimeRange  # noqa: E402


def _build_atf(path: Path, manifest: Dict[str, object], events: Iterable[Dict[str, object]]) -> Path:
    builder = ATFTestFileBuilder(manifest, events)
    return builder.build(path)


def _integration_events() -> List[Dict[str, object]]:
    detail_payload = {
        "index_event": {
            "timestamp_ns": 100,
            "thread_id": 11,
            "function_id": 3,
            "event_type": EventType.CUSTOM.value,
            "detail_offset": 0,
            "flags": 2,
        },
        "call_stack": ["100", "200"],
        "arguments": {"value": 5},
        "metadata": {"lane": "integration"},
    }

    return [
        {
            "index": {
                "timestamp_ns": 50,
                "thread_id": 7,
                "function_id": 1,
            "event_type_code": EVENT_TYPE_TO_CODE[EventType.FUNCTION_ENTER],
                "flags": 0,
            },
            "detail": None,
        },
        {
            "index": {
                "timestamp_ns": 100,
                "thread_id": 11,
                "function_id": 3,
            "event_type_code": EVENT_TYPE_TO_CODE[EventType.CUSTOM],
                "flags": 2,
            },
            "detail": detail_payload,
        },
    ]


def test_atf_reader_integration_covfill__metadata_includes_path_and_count__then_returns_expected(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=0)
    events = _integration_events()
    path = _build_atf(tmp_path / "integration.atf", manifest, events)

    reader = ATFReader()
    reader.open(path)

    metadata = reader.get_metadata()
    assert metadata["event_count"] == len(events)
    assert Path(metadata["path"]) == path

    reader.close()


def test_atf_reader_integration_covfill__detail_decoding_casts_types__then_returns_expected(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=0)
    events = _integration_events()
    path = _build_atf(tmp_path / "integration_detail.atf", manifest, events)

    reader = ATFReader()
    reader.open(path)

    detail_offsets = [event.detail_offset for event in reader.read_index_events() if event.detail_offset]
    detail = reader.read_detail_event(detail_offsets[0])

    assert detail.index_event.event_type is EventType.CUSTOM
    assert detail.call_stack == [100, 200]
    assert detail.arguments["value"] == 5
    assert detail.metadata["lane"] == "integration"

    reader.close()


def test_atf_reader_integration_covfill__estimate_event_count_with_range__then_filters(tmp_path: Path) -> None:
    manifest = base_manifest(event_count=0)
    events = _integration_events()
    path = _build_atf(tmp_path / "integration_range.atf", manifest, events)

    reader = ATFReader()
    reader.open(path)

    assert reader.estimate_event_count() == len(events)
    assert reader.estimate_event_count(TimeRange(60, 150)) == 1

    reader.close()


def test_atf_reader_integration_covfill_ext__time_range_and_threads__then_match_manifest(
    tmp_path: Path,
) -> None:
    manifest = base_manifest(event_count=0)
    events = _integration_events()
    path = _build_atf(tmp_path / "integration_time_range.atf", manifest, events)

    reader = ATFReader()
    reader.open(path)

    time_range = reader.get_time_range()
    assert time_range.start_ns == manifest["time_range"]["start_ns"]
    assert time_range.end_ns == manifest["time_range"]["end_ns"]
    assert reader.get_thread_ids() == [7, 8]

    reader.close()
