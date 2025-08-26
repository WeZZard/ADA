# ADA: User Stories and Story Map

This document defines the user personas, the high-level user journey (story map), and the detailed user stories for the Agent Debugging Assistant (ADA).

## 1. User Personas

### Persona 1: The AI Agent (Primary User)

This is a non-human, programmatic user. Its goal is to autonomously debug a program by analyzing execution data. It requires clean, structured, and rich data feeds. It prioritizes completeness and semantic context over human readability.

### Persona 2: The Human Developer (Supervisor)

This is the developer who deploys and supervises the AI Agent. They need to understand the agent's conclusions, verify its findings, and occasionally use the debugger's output themselves. They prioritize human-readable summaries and clear visualizations.

## 2. User Story Map: The Backbone

The backbone represents the major stages of the debugging journey for both personas. The map is organized by these activities, with detailed stories for each persona hanging below them.

| Setup & Configuration                | Environment & Permissions            | Execution & Tracing                  | Trace Analysis                       | Interactive Debugging & Exploration |
| :----------------------------------- | :----------------------------------- | :----------------------------------- | :----------------------------------- | :---------------------------------- |
| *(Stories about starting a session)* | *(Stories about getting trace-ready)* | *(Stories about running the program)* | *(Stories about automated analysis)* | *(Stories about active investigation)* |

## 3. Detailed User Stories

---

### Activity: Setup & Configuration

This activity covers the initial setup of the tool and the project to be debugged.

#### Persona: AI Agent

1. As an **AI Agent**
    - I want to query the debugger for its capabilities and the API schema it supports
    - So that I can understand how to interact with it effectively.

2. As an **AI Agent**
    - I want a clear and stable endpoint (e.g., a TCP port or a Unix socket) to connect to the debugger service
    - So that I can initiate a communication session.

#### Persona: Human Developer

1. As a **Human Developer**
    - I want to install the debugger with a single command using a standard package manager
    - So that I can get started quickly.

2. As a **Human Developer**
    - I want the debugger to automatically find and load the debug symbols for my program
    - So that I don't have to configure symbol paths manually.

3. As a **Human Developer**
    - I want to tell the debugger where my project's source code is located
    - So that it can link trace events back to the original code and documentation.

---

### Activity: Environment & Permissions

This activity ensures the environment is ready for tracing before launching.

#### Persona: AI Agent

1. As an **AI Agent**
    - I want to run a preflight diagnostic against a target
    - So that I can determine if tracing is possible and obtain a machine-readable remediation plan when it is not.

2. As an **AI Agent**
    - I want deterministic error-to-action mapping for common failures (e.g., Developer Tools permission missing, sudo required, code-signing/library validation issues, protected processes)
    - So that I can guide the user with exact next steps.

3. As an **AI Agent**
    - I want to detect system binaries and platform-protected processes before attempting attachment
    - So that I can inform users immediately about platform limitations without wasting time on impossible operations.

#### Persona: Human Developer

1. As a **Human Developer**
    - I want a one-command check that clearly explains what permissions or settings are missing
    - So that I can quickly bring my machine to a trace-ready state.

2. As a **Human Developer**
    - I want explicit consent prompts before any code-signing or entitlement changes are suggested
    - So that I can control security-impacting actions.

3. As a **Human Developer**
    - I want clear, user-friendly explanations when system binaries cannot be traced
    - So that I understand this is a platform security feature, not a bug in the tool.

4. As a **Human Developer**
    - I want to be informed when a binary cannot be traced due to platform security restrictions
    - So that I can take the appropriate platform-specific actions to enable tracing.

5. As a **Human Developer**
    - I want to deploy a local daemon server with full tracing permissions
    - So that I can connect remotely and trace binaries through the daemon as if I were using the system locally.

Notes:
- Keep user-facing language plain and platform-agnostic in stories; technical procedures live in `docs/specs/PERMISSIONS_AND_ENVIRONMENT.md` and `docs/specs/PLATFORM_LIMITATIONS.md`.
- Platform-specific security requirements (e.g., macOS code signing, Linux ptrace) are detailed in `docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md`.
- Daemon deployment mode is documented in `docs/specs/DAEMON_ARCHITECTURE.md`.

---

### Activity: Execution & Tracing

This activity is focused on the process of running a program to generate a trace file.

#### Persona: AI Agent

1. As an **AI Agent**
    - I want to programmatically start a program with tracing enabled
    - So that I can begin a debugging session on demand.

2. As an **AI Agent**
    - I want to specify command-line arguments and environment variables for the program being traced
    - So that I can control the conditions of the execution.

3. As an **AI Agent**
    - I want to be notified when the trace is complete and receive a path to the resulting trace file
    - So that I can proceed with the analysis phase.

4. As an **AI Agent**
    - I want to arm tracing triggers (by symbol, duration threshold, crash/signal, time window) and set pre/post roll for selective persistence without restarting the target
    - So that I can capture full-detail windows around key events with minimal perturbation.

5. As an **AI Agent**
    - I want to select “key symbols” using function IDs and update them live for focused detailed capture
    - So that detail capture focuses on relevant functions while the rest of the run retains a compact index.

#### Persona: Human Developer

1. As a **Human Developer**
    - I want a simple command-line interface to start tracing a program
    - So that I can easily generate a trace without complex setup.

2. As a **Human Developer**
    - I want to attach the tracer to an already running process
    - So that I can debug issues without restarting the application.

3. As a **Human Developer**
    - I want to specify the output location for the trace files
    - So that I can organize my debugging artifacts.

4. As a **Human Developer**
    - I want to enable a flight-recorder mode with a time-bounded detailed window (pre/post roll) and arm triggers from the CLI
    - So that I can capture rich context around suspicious moments without drowning in data.

5. As a **Human Developer**
    - I want to see tracer impact metrics (overhead %, ring utilization, sustained write rate) during/after a run
    - So that I can judge whether tracing perturbed program timing.

---

### Activity: Trace Analysis

This activity is focused on the initial, automated analysis of a completed trace.

#### Persona: AI Agent

1. As an **AI Agent**
    - I want to be notified of an exceptional termination (e.g., a crash) in the initial trace summary
    - So that I can autonomously begin my investigation at the point of failure.

2. As an **AI Agent**
    - Upon seeing a normal program termination, I want to ask the human developer for the observed symptoms
    - So that I can understand the goal of the debugging session.

3. As an **AI Agent**
    - After receiving a symptom description from a human, I want to execute a series of structured queries against the trace
    - So that I can gather evidence related to my hypothesis about the bug.

4. As an **AI Agent**
    - I want the query engine to suggest triggers and windows (functions, thresholds, pre/post roll) based on an index-only run and symptom text
    - So that I can configure a focused follow-up capture automatically.

#### Persona: Human Developer

1. As a **Human Developer**
    - I want to see the same high-level summary the AI Agent sees (e.g., termination status)
    - So that I can be on the same page as the agent from the start.

2.  As a **Human Developer**
    - After reviewing the initial summary for a non-crashing run, I want to describe the buggy behavior I observed in natural language
    - So that I can provide the AI Agent with the starting point for its investigation.

3.  As a **Human Developer**
    - I want to easily see the OS and architecture for a given trace
    - So that I have the full context for my analysis.

---

### Activity: Interactive Debugging & Exploration

This activity is focused on actively controlling a live program or exploring a completed trace.

#### Persona: AI Agent

1. As an **AI Agent**
    - I want to perform fast, random-access, and filtered queries (e.g., by function name or event type)
    - So that I can efficiently navigate trace data to find specific evidence.

2. As an **AI Agent**
    - I want to set breakpoints at specific functions or source code lines
    - So that I can pause a live execution to analyze a specific state.

3. As an **AI Agent**
    - I want to step through code line-by-line (step over, step into, step out)
    - So that I can observe a live program's behavior in fine detail.

4. As an **AI Agent**
    - I want to inspect the values of variables and CPU registers when a live execution is paused
    - So that I can understand the program's state at a specific moment.

#### Persona: Human Developer

1. As a **Human Developer**
    - I want to initiate a new debugging session in a manual mode, set up a specific context that I believe the agent missed, and then hand control to the agent
    - So that I can combine my own strategic insight with the agent's high-speed tactical analysis.

2. As a **Human Developer**
    - When starting a manual session after a failed agent attempt, I want to review the breakpoints and watchpoints the agent used
    - So that I can decide whether to reuse, modify, or discard them for my own investigation.

3. As a **Human Developer**
    - During a manual session, I want the ability to freely add, remove, and modify my own breakpoints and watchpoints
    - So that I have complete control over the debugging setup, independent of the agent's prior configuration.

4. As a **Human Developer**
    - I want to use a fast, interactive search interface to manually query a completed trace history
    - So that I can conduct my own investigation to find evidence the agent may have missed.
