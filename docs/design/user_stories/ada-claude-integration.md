# ADA ↔ Claude Code Communication Architecture Analysis

## Overview

This document analyzes two architectural options for how Claude Code communicates with ADA to enable AI-assisted debugging.

---

## V1 Decision: Validation-First Approach

### Hypothesis to Validate

**"Multimodal capture (voice + screen + trace) improves AI-assisted debugging efficiency"**

### V1 Architecture

```
ADA (Simple Capture)              Claude Code (Multimodal Analysis)
┌──────────────────────┐          ┌────────────────────────────────┐
│ Files on disk:       │          │ Task Tool + Claude Multimodal: │
│ • voice.wav          │ ───────> │ • Process audio directly       │
│ • screen.mp4         │  paths   │ • Analyze video frames         │
│ • trace/*.atf        │          │ • Read trace events            │
│ • manifest.json      │          │ • Correlate & reason           │
└──────────────────────┘          └────────────────────────────────┘
```

### V1 Trade-off

| Aspect | V1 Choice | Trade-off |
|--------|-----------|-----------|
| Data control | Low | Claude parses raw files |
| Precision | Lower | May miss subtle details |
| Engineering | Minimal | Fast to build |
| Purpose | **Validate hypothesis** | Learn before investing |

### V1 Constraints

- **Entry-only tracing**: No function exit events (no profiling capability)
- **No stack unwinding**: Cannot reconstruct call stack
- **No exception detection**: Inline hooks can't catch exceptions across languages

### V1 Core Value

Not profiling. Not performance analysis.

**Correlation**: What code was running when the user observed the problem?

```
Voice: "I tapped login and it froze"
Screen: [shows button press, then freeze]
Trace: onTap() → validateInput() → fetchUser() → ...
         ↑
    "What happened here?"
```

### V1 Skills

| Skill | ADA Usage |
|-------|-----------|
| `/build` | None - compiler output only |
| `/run` | Capture voice + screen + trace |
| `/analyze` | Task tool with multimodal prompt |

### V1 Learnings Will Inform

- What data is most useful for debugging?
- Where does Claude struggle with raw data?
- What needs structured formatting for V2/Option B?

---

## Part 1: Discussion Chronology

### 1.1 The Initial Question

After designing user stories for how users interact with Claude Code + ADA, we identified a gap: **How does Claude Code actually communicate with ADA?**

The communication points identified:

```
1. START   - Claude launches ADA
2. DURING  - Status updates?
3. END     - How does Claude know ADA finished?
4. RESULTS - How does Claude get the bundle path?
```

### 1.2 Initial Options Proposed

Three approaches were initially considered:

- **File-based signaling** - ADA writes completion marker
- **Log output parsing** - ADA writes markers to log
- **Daemon JSON protocol** - Use existing `capture_daemon.rs` IPC

### 1.3 User's Reframe: Two Fundamental Options

The user reframed the discussion into two architecturally distinct approaches:

> "We have two options. The first is to provide ADA several sub-commands to output deterministic formats, and Claude Code fetches and organizes the content. The second is to make ADA work like an AI agent - Claude communicates with ADA in natural language, and ADA gives feedback mixing natural language with diagrams and tables."

**Option A: Deterministic CLI Interface**

- ADA provides structured commands (`ada report`, `ada query`)
- Claude Code fetches raw/processed data
- Claude Code does ALL reasoning

**Option B: ADA as AI Agent**

- ADA has its own context window
- ADA reasons about the data internally
- Claude Code receives ADA's analysis
- Two agents collaborating

### 1.4 Initial Multi-Framework Analysis

| Framework | Option A (CLI) | Option B (Agent) |
|-----------|---------------|------------------|
| Separation of Concerns | Clear | Blurred |
| Token Efficiency | Raw data large | Pre-processed |
| Flexibility | Claude queries freely | Limited by agent |
| Development Cost | Simple | Complex |
| Reasoning Quality | Single chain | Two chains |
| Debuggability | Inspect CLI output | Agent opaque |

Initial recommendation leaned toward Option A with smart summarization.

### 1.5 The Monetization Challenge

User raised the critical question:

> "But how to monetize ADA if we take the first option?"

This shifted the analysis from pure technical merit to business viability.

### 1.6 Push Against Echo Chamber

When the assistant quickly agreed Option B was better for monetization, user pushed back:

> "Don't be a yes man. Don't be an echo chamber. Could we have monetization options for Option A?"

This led to identifying multiple Option A monetization models:

- Data volume pricing
- Storage & retention
- Team/collaboration features
- Enterprise features
- Integration ecosystem
- Performance tiers
- Open core model
- White label/OEM licensing

### 1.7 First Principles Request

User requested:

> "Could you analyze these two options from first principles?"

---

## Part 2: Structured Analysis

### 2.1 The Fundamental Problem

```
Developer has a bug
        ↓
Symptoms manifest (crash, slow, wrong behavior)
        ↓
Need to understand what ACTUALLY happened (ground truth)
        ↓
Diagnosis (identify root cause)
        ↓
Fix
```

**Core insight:** The developer pays for the journey from "I have a bug" to "I know how to fix it."

### 2.2 Component Breakdown

| Component | Function | Scarce? |
|-----------|----------|---------|
| **Capture** | Record what happened | No - Many tracers exist |
| **Storage** | Persist the data | No - Commodity |
| **Query** | Access specific data | No - Mature tech |
| **Analysis** | Understand meaning | Yes - Requires intelligence |
| **Diagnosis** | Identify root cause | Yes - Requires expertise |
| **Communication** | Explain to developer | Yes - Requires understanding |

**Economic principle:** Value accrues to whoever provides the SCARCE resource.

### 2.3 The Two Options Defined

#### Option A: ADA as Deterministic CLI

```
┌─────────────┐         ┌─────────────┐         ┌──────────┐
│ Claude Code │ ──────> │  ADA CLI    │ ──────> │ Raw Data │
│ (reasoning) │ <────── │ (commands)  │ <────── │          │
└─────────────┘  data   └─────────────┘         └──────────┘
```

- ADA provides: Capture, Storage, Query (non-scarce)
- Claude provides: Analysis, Diagnosis, Communication (scarce)
- **Value accrues to:** Claude (Anthropic)

#### Option B: ADA as AI Agent

```
┌─────────────┐         ┌─────────────────────┐         ┌──────────┐
│ Claude Code │ ──────> │     ADA Agent       │ ──────> │ Raw Data │
│ (reasoning) │ <────── │ (reasoning + data)  │ <────── │          │
└─────────────┘ insight └─────────────────────┘         └──────────┘
```

- ADA provides: Capture, Storage, Query, AND specialized Analysis/Diagnosis
- Claude provides: High-level reasoning, user interaction
- **Value accrues to:** ADA (your product)

### 2.4 Monetization Comparison

#### Option A Monetization Models

| Model | Description | Risk |
|-------|-------------|------|
| Data Volume | Charge per events captured | Price pressure |
| Storage | Charge for cloud retention | Commodity |
| Team Features | Charge for collaboration | Feature wars |
| Enterprise | SSO, compliance, audit | Long sales cycle |
| Integrations | Charge for IDE/CI plugins | Fragmented market |
| Performance | Charge for low-overhead mode | Niche |
| Open Core | Free core, paid advanced | Community building |
| OEM/White Label | License to other companies | B2B complexity |

**Option A Business Model:** Infrastructure company competing on engineering excellence.

#### Option B Monetization Models

| Model | Description | Advantage |
|-------|-------------|-----------|
| Per-Analysis | Charge per debugging session | Usage-based, scalable |
| Subscription | Monthly/annual plans | Predictable revenue |
| Tiered Intelligence | Basic vs advanced analysis | Upsell path |
| Enterprise | Custom models, private deployment | High ACV |

**Option B Business Model:** Intelligence-as-a-service, capturing high-value layer.

### 2.5 The Critical Question

**Can ADA provide intelligence that Claude CANNOT easily replicate?**

| Potential Unique Intelligence | Defensibility |
|------------------------------|---------------|
| Pre-trained on millions of debugging sessions | HIGH - data moat |
| Platform-specific pattern databases | MEDIUM - buildable |
| Real-time trace interpretation | MEDIUM - engineering |
| Historical codebase context | HIGH - customer lock-in |
| Proprietary diagnostic algorithms | MEDIUM - copyable |

### 2.6 Decision Framework

```
                    Can ADA provide UNIQUE intelligence?
                                   │
                    ┌──────────────┴──────────────┐
                   YES                            NO
                    │                              │
                    ▼                              ▼
              Option B                        Option A
        (Intelligence layer)              (Infrastructure layer)
                    │                              │
                    ▼                              ▼
         High value capture              Commodity competition
         Defensible moat                 Compete on:
         Usage-based pricing              - Price
                                          - Performance
                                          - Integrations
```

### 2.7 Honest Trade-offs

| Factor | Option A | Option B |
|--------|----------|----------|
| Development effort | Lower | Higher |
| Time to market | Faster | Slower |
| Revenue ceiling | Lower | Higher |
| Defensibility | Engineering moat | AI moat |
| Risk | Commoditization | AI quality |
| Dependency | High (on Claude) | Lower |

### 2.8 Conclusion

Option A and Option B are not mutually exclusive. Option A is the foundation for Option B.

**Recommended Path:**

```
Phase 1: Option A (CLI)          Phase 2: Option B (Agent)
┌─────────────────────┐          ┌─────────────────────────────┐
│ - Ship CLI first    │          │ - Add agent layer on top    │
│ - Faster to market  │   ───>   │ - CLI becomes agent backend │
│ - Learn user needs  │          │ - Phase 1 data trains agent │
│ - Infrastructure $  │          │ - Existing users convert    │
└─────────────────────┘          └─────────────────────────────┘
```

**Rationale:**
- CLI doesn't disappear - it becomes the foundation
- Phase 1 revenue funds Phase 2 development
- Real usage data informs agent design
- Lower risk than betting everything on agent upfront

---

## Part 3: Open Questions for Decision

1. Does ADA have access to unique training data (debugging sessions) that would give the AI agent a moat?

2. What is the development timeline and resource availability for building an AI agent?

3. Is the market more receptive to "best tracer" or "AI debugging assistant" positioning?

4. How important is independence from Claude/Anthropic in the long term?

5. What is the acceptable time-to-revenue?

---

## Related Documents

- User Stories: See plan file at `~/.claude/plans/crystalline-beaming-naur.md`
