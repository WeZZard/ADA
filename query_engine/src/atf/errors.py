"""Custom exceptions for the ATF reader implementation."""

from __future__ import annotations

class ATFError(Exception):
    """Base class for ATF reader errors."""

class ManifestError(ATFError):
    """Raised when the manifest cannot be parsed."""

class MemoryMapError(ATFError):
    """Raised when memory mapping of the ATF file fails."""

class EventDecodingError(ATFError):
    """Raised when an event record cannot be decoded."""

class ReaderClosedError(ATFError):
    """Raised when operations are performed on a closed reader."""

class HeaderValidationError(ATFError):
    """Raised when the ATF header is invalid or corrupted."""
