# E2: Session Management Layer

## Layer

Session lifecycle and state persistence

## Depends On

E1_Format_Adapter (uses `ada query` output)

## Interface Contract

### Session State File

Location: `~/.ada/active_session.json`

```json
{
  "session_path": "/path/to/session.adabundle",
  "start_time": "2024-01-24T10:30:00Z",
  "app_info": {
    "name": "MyApp",
    "bundle_id": "com.example.myapp"
  },
  "status": "running" | "complete" | "failed"
}
```

### Session Bundle Structure (existing)

```
session.adabundle/
├── manifest.json       # Paths to all files
├── screen.mp4          # Screen recording
├── voice.wav           # Voice recording
└── trace/session_XXX/  # Trace data
```

### manifest.json Schema (existing)

```json
{
  "version": 1,
  "created_at_ms": 1706097000000,
  "finished_at_ms": 1706097300000,
  "session_name": "session_20240124_103000",
  "trace_root": "trace",
  "trace_session": "session_XXX/pid_12345",
  "screen_path": "screen.mp4",
  "voice_path": "voice.wav"
}
```

## Deliverables

1. Define `active_session.json` schema (above)
2. Session state write on `ada capture start`
3. Session state update on capture completion
4. Session discovery logic for skills

## Key Insight

Session state file enables **context-resilient handoff** - even after context compaction, Claude can recover the active session from disk.

## Open Questions

- [ ] Should ADA CLI write `active_session.json` or should the skill do it?
- [ ] How to detect capture completion? (poll, signal, status file?)

## Status

Not started
