# /analyze - Analyze ADA Capture Session

## Purpose

Analyze a captured ADA session using time-correlated multimodal data: execution traces, voice transcripts, and screenshots. Synthesize findings into actionable diagnostics.

## Workflow Overview

1. **Select Session** - Identify which capture session to analyze
2. **Gather Context** - Get session time bounds and available data
3. **Time Correlation** - Map voice descriptions to trace timestamps
4. **Query Events** - Retrieve relevant trace data for time windows
5. **Multimodal Synthesis** - Combine traces, screenshots, and transcript
6. **Present Diagnosis** - Deliver unified analysis to user

## Usage

When user invokes `/analyze`, execute the following:

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

# List available screenshots
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

## Options

- `@latest` - Analyze most recent session (default)
- `@<session_id>` - Analyze specific session
- `--focus <time>` - Focus on specific timestamp
- `--issue <description>` - Describe the issue to investigate

## Error Handling

- **No session found**: Guide user to run `/run` first
- **No voice recording**: Analyze using events and screenshots only
- **No screen recording**: Analyze using events and transcript only
- **Empty trace**: Check if capture was running during issue

## Example Session

```
User: /analyze

Claude: I'll analyze your latest capture session.

Session: ~/.ada/sessions/2025-01-25T10-30-00_ada
Duration: 45.2 seconds
Data available: traces, voice, screenshots

Reading transcript...
You mentioned: "It freezes when I click the submit button"

Correlating timestamp... Found at T+12.3s

Querying events around T+12.3s...

## Analysis

**Issue**: UI freeze on submit button click

**Timeline**:
- T+12.300s: Button click event received
- T+12.301s: Network request initiated
- T+12.302s: Main thread blocked on synchronous call
- T+15.800s: Response received, UI unfreezes

**Root Cause**: Synchronous network call blocking main thread

**Suggestion**: Move network call to background thread using async/await
```

## Detailed Design

See E6 epic for complete workflow design including:
- Time correlation algorithms
- Multimodal synthesis logic
- Diagnostic output formatting
- User interaction patterns
