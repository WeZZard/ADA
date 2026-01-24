# E4: Skills Layer

## Layer

Claude Code skills - user-facing commands

## Depends On

- E2_Session_Management (writes/reads `active_session.json`)
- E3_Analysis_Pipeline (orchestrates analysis stages)

## Interface Contract

### Skill Files

Location in repo: `claude/commands/`
Deployed to: `~/.claude/commands/`

| Skill | Trigger | ADA Usage |
|-------|---------|-----------|
| `/run` | "run my app", "start the app" | `ada capture start` |
| `/analyze` | "why did it freeze?", "analyze" | `ada query events` + Whisper + ffmpeg |
| `/build` | "build my app" | None (build system only) |

### /run Skill

```markdown
# Detects project type, launches with capture
1. Find app binary (xcodebuild, cargo, etc.)
2. Launch: nohup ada capture start <binary> --output ~/.ada/sessions/<id> &
3. Write ~/.ada/active_session.json
4. Return: "App running with capture. Describe issues when ready."
```

### /analyze Skill

```markdown
# Runs E3 pipeline, presents diagnosis
1. Read ~/.ada/active_session.json
2. Run Whisper on voice.wav
3. Extract time points from transcript (LLM)
4. For each time point:
   - Extract screenshot (ffmpeg)
   - Query events in window (ada query)
5. Synthesize with Task tool (multimodal)
6. Present diagnosis
```

### /build Skill

```markdown
# Standard build, no ADA
1. Detect build system
2. Run build command
3. Parse errors, suggest fixes
```

## Deployment

`utils/deploy.sh`:
```bash
mkdir -p ~/.claude/commands
cp claude/commands/*.md ~/.claude/commands/
```

## Deliverables

1. `claude/commands/run.md`
2. `claude/commands/analyze.md`
3. `claude/commands/build.md`
4. `utils/deploy.sh`

## Testing

Headless mode validation:
```bash
claude --print -p "/run" --allowedTools Bash,Read,Write
claude --print -p "/analyze" --allowedTools Bash,Read,Task
```

## Open Questions

- [ ] Natural language triggers - how to detect "it crashed" without explicit /analyze?
- [ ] Project type detection - heuristics for iOS vs macOS vs Rust?

## Status

Not started
