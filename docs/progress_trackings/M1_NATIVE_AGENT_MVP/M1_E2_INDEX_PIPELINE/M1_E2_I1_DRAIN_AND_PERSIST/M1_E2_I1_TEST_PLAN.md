# Test Plan — M1 E2 I1 Drain and Persist

## Goals
Verify file creation, header correctness, and non-zero record count.

## Cases
- File exists with correct magic, record size, pid, and session_id
- Monotonic timestamps across sequence
- `events_captured` matches approximate record count

## Procedure
1. Run tracer for fixed 3s; stop.
2. Read first 32 bytes; validate magic/record_size/pid/session_id.
3. Count records by file size; compare to stats (tolerance ±20%).

## Acceptance
- Header valid; record count > 0 and within tolerance.
