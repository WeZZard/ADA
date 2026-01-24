# E4: Analysis Pipeline Layer

## Layer

Time-based analysis with external tools (Whisper, ffmpeg)

## Depends On

- E1_Format_Adapter (consumes `ada query events --format line-complete`)
- E2_Session_Management (reads `active_session.json` and `manifest.json`)

## Interface Contract

### Input (from E2)

- Session path from `~/.ada/active_session.json`
- File paths from `manifest.json`: voice.wav, screen.mp4, trace/

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

### Stage 1: Session Recovery
```bash
cat ~/.ada/active_session.json → session_path
cat <session_path>/manifest.json → file paths
```

### Stage 2: Speech-to-Text
```bash
whisper voice.wav --model base --output_format json
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
ffmpeg -ss 30 -i screen.mp4 -frames:v 1 screenshot.png

# Events around time point (±2s window)
ada query <session> events --format line-complete
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

Not started
