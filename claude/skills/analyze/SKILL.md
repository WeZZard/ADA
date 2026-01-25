---
name: analyze
description: "Analyze ADA capture session - correlates voice, screen, and trace data for diagnosis"
---

# Analyze ADA Capture Session

## Purpose

Analyze a captured ADA session using time-correlated multimodal data: execution traces, voice transcripts, and screenshots.

## Workflow

### Step 1: Session Selection

```bash
# Get latest session info
ada query @latest time-info

# Or list available sessions
ada query --list-sessions
```

### Step 2: Gather Session Data

```bash
# Get session time bounds
ada query @latest time-info

# Get transcript (if voice was recorded)
ada query @latest transcribe segments

# Get screenshot at specific time
ada query @latest screenshot --time <seconds>
```

### Step 3: Time Correlation

Map user's verbal descriptions to nanosecond-precision timestamps:

1. Parse transcript for temporal markers ("when I clicked...", "after loading...")
2. Identify time windows of interest
3. Calculate corresponding nanosecond ranges

### Step 4: Query Trace Events

```bash
# Query events in time window
ada query @latest events --since-ns <ns> --until-ns <ns>

# Query with result limit
ada query @latest events --since-ns <ns> --until-ns <ns> --limit 100

# Query specific function
ada query @latest events --since-ns <ns> --until-ns <ns> --function <name>
```

### Step 5: Multimodal Analysis

For each time window of interest:
1. Retrieve screenshot at timestamp (if available)
2. Collect trace events
3. Correlate UI state with execution behavior
4. Identify anomalies or issues

### Step 6: Present Findings

Deliver structured analysis:
- **Summary**: One-line issue description
- **Timeline**: Sequence of relevant events
- **Evidence**: Screenshots and trace excerpts
- **Diagnosis**: Root cause analysis
- **Suggestions**: Recommended fixes

## Error Handling

- **No session found**: Guide user to use `ada:run` skill first
- **No voice recording**: Session captured with `--no-voice`; analyze using events and screenshots
- **No screen recording**: Session captured with `--no-screen`; analyze using events and transcript
- **Empty trace**: Check if capture was running during issue
