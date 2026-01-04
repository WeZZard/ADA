#!/usr/bin/env python3
"""
ATF Event Dumper - Dump raw trace events with symbol resolution.

Usage:
    python3 utils/atf_dump_events.py <session_dir> [--limit N]

Example:
    python3 utils/atf_dump_events.py ./traces/session_*/pid_* --limit 50
"""

import struct
import json
import sys
import argparse
from pathlib import Path

# ATF Event Kinds
EVENT_KINDS = {
    1: "CALL",
    2: "RETURN",
    3: "EXCEPTION",
}


def main():
    parser = argparse.ArgumentParser(description="Dump ATF trace events with symbol resolution")
    parser.add_argument("session_dir", help="Path to session directory containing manifest.json")
    parser.add_argument("--limit", "-n", type=int, default=100, help="Maximum events to dump (default: 100)")
    parser.add_argument("--raw", "-r", action="store_true", help="Show raw function IDs without resolution")
    args = parser.parse_args()

    session_dir = Path(args.session_dir)

    if not session_dir.exists():
        print(f"Error: Session directory not found: {session_dir}", file=sys.stderr)
        sys.exit(1)

    # Load manifest
    manifest_path = session_dir / "manifest.json"
    if not manifest_path.exists():
        print(f"Error: manifest.json not found in {session_dir}", file=sys.stderr)
        sys.exit(1)

    with open(manifest_path) as f:
        manifest = json.load(f)

    # Build symbol table
    symbol_table = {}
    for sym in manifest.get('symbols', []):
        fid = int(sym['function_id'], 16)
        symbol_table[fid] = sym['name']

    # Print header
    print(f"Session: {session_dir}")
    print(f"Format:  {manifest.get('format_version', 'unknown')}")
    print(f"Symbols: {len(symbol_table)}")
    print()
    print(f"{'Seq':<6} {'Timestamp (ns)':<20} {'Depth':<6} {'Kind':<8} {'Function ID':<20} {'Symbol'}")
    print("-" * 100)

    # Find and read thread index files
    event_count = 0
    thread_dirs = sorted(session_dir.glob("thread_*"))

    for thread_dir in thread_dirs:
        index_path = thread_dir / "index.atf"
        if not index_path.exists():
            continue

        with open(index_path, 'rb') as f:
            data = f.read()

        if len(data) < 64 or data[0:4] != b'ATI2':
            continue

        events_offset = struct.unpack('<Q', data[32:40])[0]
        footer_offset = struct.unpack('<Q', data[40:48])[0]

        offset = events_offset
        seq = 0
        while offset + 32 <= footer_offset:
            event_data = data[offset:offset+32]
            timestamp_ns, function_id, thread_id, event_kind, call_depth, detail_seq = \
                struct.unpack('<QQIIII', event_data)

            if function_id != 0 and event_kind in EVENT_KINDS:
                kind_str = EVENT_KINDS.get(event_kind, f"?{event_kind}")

                if args.raw:
                    symbol = ""
                else:
                    symbol = symbol_table.get(function_id, "<unknown>")

                print(f"{seq:<6} {timestamp_ns:<20} {call_depth:<6} {kind_str:<8} 0x{function_id:016x} {symbol}")

                event_count += 1
                if event_count >= args.limit:
                    print(f"\n... (limited to {args.limit} events)")
                    return

            offset += 32
            seq += 1

    print(f"\nTotal events: {event_count}")


if __name__ == "__main__":
    main()
