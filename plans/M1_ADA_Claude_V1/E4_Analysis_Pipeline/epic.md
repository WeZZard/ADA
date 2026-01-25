# E4: Analysis Pipeline Layer

## Layer

Time-based analysis with external tools (Whisper, ffmpeg)

## Depends On

- E2_Format_Adapter (consumes `ada query events --format line-complete`)
- E3_Session_Management (uses `ada session latest` and `@latest` path resolution)

## Interface Contract

### Input (from E3)

- Session path via `ada session latest` or `@latest` in queries
- File paths from session directory: voice.wav, screen.mp4, trace/

**Note:** No `active_session.json` needed - use `@latest` directly in queries or get path via `ada session latest`.

### Output (to E4 Skills)

Structured observations with correlated data:

```json
{
  "observations": [
    {
      "time_sec": 30,
      "cycle": 90000000000,
      "description": "tapped login, app froze",
      "screenshot_path": "/tmp/ada_analysis/screenshot_30s.png",
      "events": [
        "cycle=89999000000 | T=29.99s | thread:0 | path:0.0 | depth:1 | main()",
        "cycle=89999500000 | T=29.99s | thread:0 | path:0.0.0 | depth:2 | login()"
      ]
    }
  ]
}
```

## Pipeline Stages

### Stage 1: Session Resolution
```bash
# Option A: Get session path explicitly
SESSION=$(ada session latest)

# Option B: Use @latest directly in queries (preferred)
ada query @latest events --format line-complete

# Get session directory contents
ls $(ada session latest)/   # → trace/, voice.wav, screen.mp4, etc.
```

### Stage 2: Speech-to-Text
```bash
SESSION=$(ada session latest)
whisper $SESSION/voice.wav --model base --output_format json
→ [{text, start, end}, ...]
```

### Stage 3: Time Extraction (LLM)
Claude extracts time points from transcript:
- Input: "At 30 seconds I tapped login and it froze"
- Output: `{time_sec: 30, description: "tapped login, app froze"}`

### Stage 4: Time-Based Query
For each observation:
```bash
# Screenshot at time point
SESSION=$(ada session latest)
ffmpeg -ss 30 -i $SESSION/screen.mp4 -frames:v 1 screenshot.png

# Events around time point (±2s window) - use @latest directly
ada query @latest events --format line-complete
# Filter by cycle range (28s-32s converted to cycles)
```

### Stage 5: Synthesis (LLM + Multimodal)
Task tool with screenshot + events + user description → diagnosis

## External Dependencies

| Tool | Install | Purpose |
|------|---------|---------|
| whisper | `pip install openai-whisper` | Speech-to-text |
| ffmpeg | `brew install ffmpeg` | Screenshot extraction |

## Deliverables

1. Pipeline orchestration logic (in skill or helper script)
2. Time-to-cycle conversion utility
3. Event window filtering
4. Synthesis prompt template

## Open Questions

- [ ] Where does pipeline run? (skill markdown vs helper script)
- [ ] Time filtering: post-query in skill or add to `ada query`?
- [ ] Window size: fixed ±2s or configurable?

## Status

**Complete** - Time filtering (`--since-ns`, `--until-ns`), media queries (`screenshot`, `transcribe`), and capabilities detection implemented in ada-cli. Pipeline orchestration and synthesis prompts belong in E5/E6 skill layer.
