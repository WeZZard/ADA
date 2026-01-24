# M1: ADA + Claude Code V1 Integration

## Goal

Validate the hypothesis: "Multimodal capture (voice + screen + trace) improves AI-assisted debugging efficiency"

## Happy Path (V1 Minimal Flow)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ PHASE 1: CAPTURE                                                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  User: "run my app"                                                         │
│           ↓                                                                 │
│  /run skill detects project type                                            │
│           ↓                                                                 │
│  ada capture start <binary> --output ~/.ada/sessions/<id>                   │
│           ↓                                                                 │
│  ┌─────────────────────────────────────────┐                               │
│  │ ADA captures simultaneously:            │                               │
│  │  • Function calls (trace)               │                               │
│  │  • Screen recording (video)             │                               │
│  │  • Voice recording (audio)              │                               │
│  └─────────────────────────────────────────┘                               │
│           ↓                                                                 │
│  User interacts with app, speaks: "I'm tapping login... it froze"          │
│           ↓                                                                 │
│  User stops capture (Ctrl+C)                                                │
│           ↓                                                                 │
│  ADA writes session.adabundle/ with manifest.json                           │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│ PHASE 2: ANALYSIS                                                           │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  User: "why did it freeze?"                                                 │
│           ↓                                                                 │
│  /analyze skill reads session path                                          │
│           ↓                                                                 │
│  whisper voice.wav → timestamped transcript                                 │
│  "at 30 seconds I tapped login and it froze"                               │
│           ↓                                                                 │
│  Claude extracts: time=30s, observation="tapped login, froze"              │
│           ↓                                                                 │
│  ada query <session> events --format line-complete                          │
│  → Events around T=30s (all threads)                                        │
│           ↓                                                                 │
│  ffmpeg -ss 30 -i screen.mp4 -frames:v 1 screenshot.png                    │
│  → Screenshot at T=30s                                                      │
│           ↓                                                                 │
│  Claude synthesizes: screenshot + events + user description                 │
│           ↓                                                                 │
│  "The freeze occurred because NetworkManager.sendRequest was called        │
│   on the main thread, blocking the UI for 2.3 seconds..."                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Required ADA Commands (derived from happy path)

### Phase 1: Capture

| Command | Purpose | Output |
|---------|---------|--------|
| `ada capture start <binary>` | Start multimodal capture | Runs until Ctrl+C |
| (implicit) | Write session bundle | `session.adabundle/` directory |
| (implicit) | Write bundle manifest | `manifest.json` with file paths |

### Phase 2: Analysis

| Command | Purpose | Output |
|---------|---------|--------|
| `ada query <session> summary` | Session overview | Thread count, event count, duration |
| `ada query <session> events` | Get trace events | Timestamped function calls |
| `ada query <session> events --format line-complete` | LLM-friendly format | `cycle=... \| T=... \| thread:... \| func()` |

### External Tools (not ADA)

| Tool | Purpose | Command |
|------|---------|---------|
| Whisper | Speech-to-text | `whisper voice.wav --model base --output_format json` |
| ffmpeg | Screenshot extraction | `ffmpeg -ss <time> -i screen.mp4 -frames:v 1 out.png` |

## Epics

| Epic | Purpose |
|------|---------|
| E1_Happy_Path_Spike | Run happy path manually, discover what works/breaks |
| E2_Format_Adapter | (Scope TBD after E1) |
| E3_Session_Management | (Scope TBD after E1) |
| E4_Analysis_Pipeline | (Scope TBD after E1) |
| E5_Skills | (Scope TBD after E1) |

**Note**: E2-E5 scopes will be refined based on E1 findings. We only build what's actually needed.

## Execution Order

```
E1_Happy_Path_Spike
    ↓ findings inform
E2-E5 (scoped based on what E1 reveals)
```

## Success Criteria

- [ ] Happy path works end-to-end with a real app
- [ ] User can describe issues naturally, Claude correlates to code
- [ ] Time alignment between voice, screen, and trace is accurate
