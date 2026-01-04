#!/usr/bin/env python3
"""
ATF Trace Viewer - Visualize ADA trace sessions with symbol resolution.

Usage:
    python3 utils/atf_trace_viewer.py <session_dir>
    python3 utils/atf_trace_viewer.py /path/to/session_*/pid_*

Example:
    python3 utils/atf_trace_viewer.py ./traces/session_123/session_20260104_120000/pid_456
"""

import struct
import json
import sys
from pathlib import Path
from collections import defaultdict

# ATF Event Kinds
EVENT_KIND_CALL = 1
EVENT_KIND_RETURN = 2


def load_manifest(session_dir: Path) -> dict:
    """Load manifest.json from session directory."""
    manifest_path = session_dir / "manifest.json"
    if not manifest_path.exists():
        raise FileNotFoundError(f"manifest.json not found in {session_dir}")

    with open(manifest_path) as f:
        return json.load(f)


def build_symbol_table(manifest: dict) -> dict:
    """Build function_id -> symbol name lookup table."""
    symbol_table = {}
    for sym in manifest.get('symbols', []):
        fid = int(sym['function_id'], 16)
        symbol_table[fid] = sym['name']
    return symbol_table


def read_atf_index(index_path: Path) -> list:
    """Read ATF index file and return list of events."""
    with open(index_path, 'rb') as f:
        data = f.read()

    if len(data) < 64:
        raise ValueError(f"ATF file too small: {len(data)} bytes")

    # Validate magic
    magic = data[0:4]
    if magic != b'ATI2':
        raise ValueError(f"Invalid ATF magic: {magic}")

    # Parse header
    events_offset = struct.unpack('<Q', data[32:40])[0]
    footer_offset = struct.unpack('<Q', data[40:48])[0]

    # Read events
    events = []
    offset = events_offset
    while offset + 32 <= footer_offset:
        event_data = data[offset:offset+32]
        timestamp_ns, function_id, thread_id, event_kind, call_depth, detail_seq = \
            struct.unpack('<QQIIII', event_data)

        if function_id != 0 and event_kind in (EVENT_KIND_CALL, EVENT_KIND_RETURN):
            events.append({
                'timestamp_ns': timestamp_ns,
                'function_id': function_id,
                'thread_id': thread_id,
                'event_kind': event_kind,
                'call_depth': call_depth,
            })
        offset += 32

    return events


def print_call_tree(events: list, symbol_table: dict):
    """Print call tree with timing information."""
    print("\n" + "─" * 70)
    print(" Call Tree with Timing")
    print("─" * 70 + "\n")

    call_stack = []  # (symbol, start_time, depth)

    for event in events:
        fid = event['function_id']
        symbol = symbol_table.get(fid, f"<0x{fid:x}>")
        ts = event['timestamp_ns']
        depth = event['call_depth']
        indent = "│   " * (depth - 1) if depth > 0 else ""

        if event['event_kind'] == EVENT_KIND_CALL:
            call_stack.append((symbol, ts, depth))
            print(f"{indent}├── → {symbol}()")
        else:  # RETURN
            duration_us = None
            for i in range(len(call_stack) - 1, -1, -1):
                if call_stack[i][0] == symbol and call_stack[i][2] == depth:
                    duration_us = (ts - call_stack[i][1]) / 1000
                    call_stack.pop(i)
                    break

            if duration_us is not None:
                print(f"{indent}└── ← {symbol}() [{duration_us:.1f}μs]")
            else:
                print(f"{indent}└── ← {symbol}()")


def print_function_summary(events: list, symbol_table: dict):
    """Print function call summary with statistics."""
    print("\n" + "─" * 70)
    print(" Function Summary")
    print("─" * 70 + "\n")

    func_stats = defaultdict(lambda: {'calls': 0, 'total_us': 0.0})
    call_starts = {}

    for event in events:
        fid = event['function_id']
        symbol = symbol_table.get(fid, f"<0x{fid:x}>")
        ts = event['timestamp_ns']
        depth = event['call_depth']

        if event['event_kind'] == EVENT_KIND_CALL:
            if fid not in call_starts:
                call_starts[fid] = []
            call_starts[fid].append((ts, depth))
        else:  # RETURN
            if fid in call_starts and call_starts[fid]:
                start_ts, _ = call_starts[fid].pop()
                duration_us = (ts - start_ts) / 1000
                func_stats[symbol]['calls'] += 1
                func_stats[symbol]['total_us'] += duration_us

    print(f"{'Function':<30} {'Calls':>8} {'Total Time':>12} {'Avg Time':>12}")
    print("─" * 70)

    for func, stats in sorted(func_stats.items(), key=lambda x: -x[1]['total_us']):
        avg = stats['total_us'] / stats['calls'] if stats['calls'] > 0 else 0
        print(f"{func:<30} {stats['calls']:>8} {stats['total_us']:>10.1f}μs {avg:>10.1f}μs")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    session_dir = Path(sys.argv[1])

    if not session_dir.exists():
        print(f"Error: Session directory not found: {session_dir}")
        sys.exit(1)

    # Load manifest
    try:
        manifest = load_manifest(session_dir)
    except FileNotFoundError as e:
        print(f"Error: {e}")
        sys.exit(1)

    symbol_table = build_symbol_table(manifest)

    # Print header
    print("═" * 70)
    print(" ADA Trace Viewer")
    print("═" * 70)
    print(f"\n📁 Session: {session_dir}")
    print(f"📋 Format: {manifest.get('format_version', 'unknown')}")
    print(f"🔧 Modules: {len(manifest.get('modules', []))}, Symbols: {len(manifest.get('symbols', []))}")

    # Find and read thread index files
    all_events = []
    thread_dirs = sorted(session_dir.glob("thread_*"))

    for thread_dir in thread_dirs:
        index_path = thread_dir / "index.atf"
        if index_path.exists():
            try:
                events = read_atf_index(index_path)
                all_events.extend(events)
                print(f"📊 Thread {thread_dir.name}: {len(events)} events")
            except Exception as e:
                print(f"⚠️  Error reading {index_path}: {e}")

    if not all_events:
        print("\nNo events found in session.")
        sys.exit(0)

    # Sort events by timestamp
    all_events.sort(key=lambda e: e['timestamp_ns'])

    # Print visualizations
    print_call_tree(all_events, symbol_table)
    print_function_summary(all_events, symbol_table)

    print("\n" + "═" * 70)


if __name__ == "__main__":
    main()
