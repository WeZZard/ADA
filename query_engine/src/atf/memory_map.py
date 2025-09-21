"""Lightweight memory map wrapper for ATF traces."""

from __future__ import annotations

import mmap
import os
from pathlib import Path
from typing import Optional

from .errors import MemoryMapError, ReaderClosedError

class MemoryMap:
    """RAII-style wrapper around ``mmap`` for read-only trace access."""

    __slots__ = ("_file", "_mmap", "path", "size")

    def __init__(self) -> None:
        self._file: Optional[object] = None
        self._mmap: Optional[mmap.mmap] = None
        self.path: Optional[Path] = None
        self.size: int = 0

    def open(self, path: Path) -> None:
        """Open and memory-map ``path`` in read-only mode."""
        self.close()
        try:
            file_handle = path.open("rb")
        except OSError as exc:  # pragma: no cover - filesystem failure
            raise MemoryMapError(f"Unable to open ATF file: {exc}") from exc

        try:
            stat_result = os.fstat(file_handle.fileno())
        except OSError as exc:  # pragma: no cover - unlikely
            file_handle.close()
            raise MemoryMapError(f"Unable to stat ATF file: {exc}") from exc

        if stat_result.st_size == 0:
            file_handle.close()
            raise MemoryMapError("ATF file is empty")

        try:
            mm = mmap.mmap(file_handle.fileno(), 0, access=mmap.ACCESS_READ)
        except (BufferError, OSError) as exc:
            file_handle.close()
            raise MemoryMapError(f"Unable to memory-map ATF file: {exc}") from exc

        self._file = file_handle
        self._mmap = mm
        self.size = stat_result.st_size
        self.path = path

    def close(self) -> None:
        """Release the mapped file if open."""
        if self._mmap is not None:
            self._mmap.close()
            self._mmap = None
        if self._file is not None:
            try:
                self._file.close()
            finally:
                self._file = None
        self.size = 0
        self.path = None

    def _ensure_open(self) -> mmap.mmap:
        if self._mmap is None:
            raise ReaderClosedError("ATF file is not open")
        return self._mmap

    def read(self, offset: int, size: int) -> bytes:
        """Read ``size`` bytes starting at ``offset``."""
        if size < 0 or offset < 0:
            raise MemoryMapError("Negative offset or size requested")
        mm = self._ensure_open()
        if offset + size > self.size:
            raise MemoryMapError("Read exceeds mapped file size")
        mm.seek(offset)
        return mm.read(size)

    def slice(self, offset: int, size: int) -> memoryview:
        """Return a zero-copy view of the mapped range."""
        if size < 0 or offset < 0:
            raise MemoryMapError("Negative offset or size requested")
        mm = self._ensure_open()
        end = offset + size
        if end > self.size:
            raise MemoryMapError("Slice exceeds mapped file size")
        return memoryview(mm)[offset:end]

    def __enter__(self) -> "MemoryMap":  # pragma: no cover - convenience
        if self._mmap is None:
            raise ReaderClosedError("ATF file is not open")
        return self

    def __exit__(self, *_) -> None:  # pragma: no cover - convenience
        self.close()
