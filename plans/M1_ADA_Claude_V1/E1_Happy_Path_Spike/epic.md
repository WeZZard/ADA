# E1: Happy Path Spike

## Purpose

Run the V1 happy path manually, end-to-end. Document what works, what breaks, what's missing. Let reality inform E2-E5.

## Approach

No code changes. Just run commands and observe.

## The Spike

### Step 1: Capture

```bash
# Build ADA if needed
cd /Users/wezzard/Projects/ADA-codex
cargo build --release

# Pick a test app (any macOS app with symbols)
TEST_APP="/path/to/some/app"

# Run capture (~30 seconds, speak observations, then Ctrl+C)
./target/release/ada capture start "$TEST_APP" --output /tmp/spike_test
```

**Observe**:
- [ ] Does capture start without error?
- [ ] Does Ctrl+C stop gracefully?
- [ ] What's in the output directory?

```bash
ls -la /tmp/spike_test/
ls -la /tmp/spike_test/*.adabundle/
```

---

### Step 2: Check Bundle Structure

```bash
# Bundle manifest
cat /tmp/spike_test/*.adabundle/manifest.json | jq .

# ATF manifest (symbols)
cat /tmp/spike_test/*.adabundle/trace/*/pid_*/manifest.json | jq .
```

**Observe**:
- [ ] Does bundle manifest have expected fields?
- [ ] Does ATF manifest have symbols with readable names?
- [ ] What's the actual directory structure?

---

### Step 3: Check Multimedia

```bash
# Screen recording
ffprobe /tmp/spike_test/*.adabundle/screen.mp4

# Voice recording
ffprobe /tmp/spike_test/*.adabundle/voice.wav
```

**Observe**:
- [ ] Are files playable?
- [ ] What's the duration?
- [ ] Any encoding issues?

---

### Step 4: Query Events

```bash
# Summary
./target/release/ada query /tmp/spike_test/*.adabundle summary

# Events (text)
./target/release/ada query /tmp/spike_test/*.adabundle events limit:20

# Events (JSON)
./target/release/ada query /tmp/spike_test/*.adabundle events limit:20 --format json | jq .
```

**Observe**:
- [ ] Are function names resolved (not hex IDs)?
- [ ] What timestamp format? Nanoseconds? Cycles?
- [ ] Is the JSON structure usable?
- [ ] What fields are present?

---

### Step 5: Run Whisper

```bash
# Check if Whisper is installed
which whisper || pip install openai-whisper

# Transcribe voice
whisper /tmp/spike_test/*.adabundle/voice.wav --model base --output_format json --output_dir /tmp/spike_whisper/

# Check output
cat /tmp/spike_whisper/*.json | jq .
```

**Observe**:
- [ ] Does Whisper run?
- [ ] What's the output format?
- [ ] Are timestamps present?
- [ ] Is accuracy usable?

---

### Step 6: Extract Screenshot

```bash
# Pick a timestamp from the voice transcript (e.g., 10 seconds)
ffmpeg -ss 10 -i /tmp/spike_test/*.adabundle/screen.mp4 -frames:v 1 /tmp/spike_screenshot.png

# View it
open /tmp/spike_screenshot.png
```

**Observe**:
- [ ] Does extraction work?
- [ ] Is the image usable?

---

### Step 7: Feed to Claude (manually)

Open Claude (web or API) and provide:
1. The voice transcript text
2. The screenshot image
3. A sample of events JSON
4. Ask: "What was the code doing when the user reported the issue?"

**Observe**:
- [ ] Can Claude correlate the data?
- [ ] What format does Claude prefer?
- [ ] What's missing for Claude to reason well?
- [ ] What's the actual diagnosis quality?

---

## Findings Template

After running the spike, document:

### What Worked
```
-
```

### What Broke
```
-
```

### What's Missing
```
-
```

### Insights for E2-E5
```
E2 (Format Adapter):
-

E3 (Session Management):
-

E4 (Analysis Pipeline):
-

E5 (Skills):
-
```

---

## Deliverables

1. Completed spike with observations
2. Findings document
3. Updated E2-E5 based on reality

## Status

Not started
