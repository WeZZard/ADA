"""Manifest parsing helpers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List

from interfaces import TimeRange
from .errors import ManifestError

try:  # Prefer orjson for performance when available
    import orjson
except ModuleNotFoundError:  # pragma: no cover - fallback path
    orjson = None  # type: ignore

if orjson is not None:  # pragma: no cover - exercised when orjson available
    def _loads(data: bytes) -> Dict[str, Any]:
        try:
            return orjson.loads(data)
        except orjson.JSONDecodeError as exc:  # type: ignore[attr-defined]
            raise ManifestError(f"Failed to parse manifest JSON: {exc}") from exc
else:
    import json

    def _loads(data: bytes) -> Dict[str, Any]:
        try:
            return json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ManifestError(f"Failed to parse manifest JSON: {exc}") from exc

@dataclass(frozen=True)
class ManifestInfo:
    """Parsed manifest information for an ATF trace."""

    metadata: Dict[str, Any]
    time_range: TimeRange
    thread_ids: List[int]
    event_count: int

    @classmethod
    def from_bytes(cls, payload: bytes) -> "ManifestInfo":
        """Parse manifest bytes into a structured object."""
        if not payload:
            raise ManifestError("Manifest payload is empty")

        manifest_dict = _loads(payload)

        metadata = manifest_dict.get("metadata") or {}
        if not isinstance(metadata, dict):
            raise ManifestError("Manifest metadata must be an object")

        time_range_dict = manifest_dict.get("time_range") or {}
        try:
            start_ns = int(time_range_dict.get("start_ns", 0))
            end_ns = int(time_range_dict.get("end_ns", start_ns))
        except (TypeError, ValueError) as exc:
            raise ManifestError("Invalid time range in manifest") from exc

        if end_ns < start_ns:
            raise ManifestError("Manifest end time precedes start time")

        thread_ids_raw = manifest_dict.get("thread_ids") or []
        if not isinstance(thread_ids_raw, list):
            raise ManifestError("Manifest thread_ids must be a list")
        try:
            thread_ids = sorted({int(thread_id) for thread_id in thread_ids_raw})
        except (TypeError, ValueError) as exc:
            raise ManifestError("Manifest thread_ids must contain integers") from exc

        event_count_raw = manifest_dict.get("event_count")
        event_count = int(event_count_raw) if event_count_raw is not None else 0
        if event_count < 0:
            raise ManifestError("Manifest event_count must be non-negative")

        return cls(
            metadata=metadata,
            time_range=TimeRange(start_ns=start_ns, end_ns=end_ns),
            thread_ids=thread_ids,
            event_count=event_count,
        )

    def to_dict(self) -> Dict[str, Any]:
        """Return a JSON-serialisable dictionary representation."""
        return {
            "metadata": self.metadata,
            "time_range": {
                "start_ns": self.time_range.start_ns,
                "end_ns": self.time_range.end_ns,
            },
            "thread_ids": self.thread_ids,
            "event_count": self.event_count,
        }
