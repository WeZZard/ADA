# Business Analysis — ADA Platform (Filled from Repo Docs)

This document consolidates business reasoning backed by the repository’s user stories, specifications, platform constraints, architecture decisions, and governance.

---

## 1. Executive summary

- **What**: ADA is a cross-platform, agent-integrated debugging platform that exposes a simple, structured API over system debuggers/instrumentation (macOS-first via Frida; Linux via eBPF post‑MVP).
- **Why now**: Agentic coding is moving beyond codegen into execution and debugging; direct tool use is brittle (CLI parsing, platform drift, entitlements). A unified, machine-readable tracing/debug API reduces TCO and increases reliability.
- **Outcome**: Reliable autonomous debugging on developer machines; measurable reduction in MTTR and agent skill development cost.

## Vision: Observer Era — From Debugger to Runtime Observer

- The large language models may someday approach near bug-free code generation (few crashes, minimal API misuse), the core failure mode shifts from “can’t run” to “doesn’t behave as intended.” Natural language intent remains ambiguous compared to product requirements, and everyday users are not trained to engineer precise prompts.
- Operating systems may soon embed first‑party agents that can build software from a single sentence. Users will then report unexpected behaviors, not compile errors.
- Ground truth lives at runtime: execution evidence is more reliable than guessing from source. ADA evolves from a debugger to an observer that captures, explains, and correlates program behavior while it runs.
- Strategic role: provide OS agents and users with a stable runtime observation layer—always‑on lightweight signals with on‑demand deep capture—emitting machine‑readable narratives and evidence that shorten the path from symptom to explanation.
- Design implications (aligned with this doc): telemetry‑first two‑lane architecture (index always on; detail windowed), symptom→instrumentation planning, zero‑friction preflight/remediation with consent, and privacy‑aware policies.


## 2. Problem and jobs-to-be-done (JTBD)

- **Primary JTBD**: "Enable an AI agent to reliably diagnose and fix runtime issues on any developer machine with minimal setup."
- **Secondary JTBDs**:
  - Human supervisor can understand agent conclusions and take over when needed.
  - Platform constraints are detected early with deterministic remediation.
- **Pain points (from specs/limits)**
  - macOS entitlements/SIP block naive attach; permissions unclear by default.
  - Tool diversity and version drift; fragile text parsing; non-portable logic.
  - Full-detail tracing is too heavy; minimal tracing lacks crash context.
- **Acceptance tests (derived from MVP specs)**
  - Preflight `diagnose` returns deterministic remediation steps for common failures; never suggests SIP relaxation.
  - Hook coverage startup time meets PR-001; sustained throughput meets PR-002; overhead within PR-003.
  - Flight‑recorder windows capture registers + stack around triggers; index lane persists continuously.

## 3. Target segments and ICPs (repo-inferred)

- **ICPs**
  - AI coding agents/IDEs needing structured debug data (agent-first APIs).
  - Internal platform/devtools teams standardizing debugging workflows across OSs.
- **Buyer personas**
  - Staff DevTools engineer and platform TL (reliability, integration, compliance).
  - AI product lead (velocity, capability breadth).

## 4. Market sizing (placeholders; not specified in repo)

- Add real TAM/SAM/SOM once external data is collected. The repo focuses on technical feasibility and governance rather than market numbers.

## 5. Competitive landscape and alternatives

- **Status quo / alternatives**
  - Raw debugger access (LLDB/GDB): brittle parsing, OS lock‑in, high TCO.
  - Human‑first IDE debuggers: not agent‑API‑first, limited machine‑readable output.
  - APM/eBPF only: production‑oriented; lacks interactive local agent flows.
- **ADA differentiators (from specs/architecture)**
  - Structured protocol (JSON‑RPC) and ATF schema; no CLI scraping.
  - Two‑lane selective persistence: always‑on index capture/persistence + always-on detail capture with selective persistence; high throughput with bounded overhead.
  - Deterministic preflight diagnostics and error→remediation mapping with guardrails.
  - Cross‑platform adapter strategy (Frida now; eBPF/Windows later) under a stable API.

## 6. Product scope

- **MVP (macOS-first)**
  - Full‑coverage function tracing; FunctionCall/Return with ABI registers + shallow stack.
  - Selective persistence (pre/post roll context, triggers, key‑symbol marking); durable ATF output; metrics.
  - Preflight `diagnose` with permission/entitlement checks and clear remediation; consent guardrails.
  - CLI and JSON‑RPC control plane; configurable output and capture settings.
- **V1 (cross‑platform parity)**
  - Linux parity via eBPF uprobes/uretprobes; Windows attach fundamentals where feasible.
  - DWARF/dSYM‑aware query filtering; logical spans exposure.
- **Out‑of‑scope (MVP)**
  - Interactive breakpoints/stepping; system binaries with SIP; Windows.

## 7. Pricing and unit economics (not defined in repo)

- Define packaging and margins after pilot validation. COGS drivers likely include multi‑OS CI, code‑sign infra, crash telemetry, and adapter maintenance.

## 8. Go‑to‑market (repo‑aligned)

- **Land**
  - Enforced runnable examples and end‑to‑end tests per Integration Process Specification; demonstrate reliability on macOS dev machines.
  - Design‑partner style pilots with devtools teams to validate preflight and flight‑recorder workflows.
- **Expand**
  - Cross‑OS adapters; IDE/agent vendor integrations; adapter marketplace concept post‑V1.
- **Proof points**
  - Hook install time, sustained throughput, drop rates, attach success after remediation, narrative summaries within token budgets.

## 9. Moat and defensibility

- High‑reliability adapter layer with deterministic preflight/remediation playbooks and guardrails.
- Performance architecture (two‑lane + SPSC rings + shared memory) that meets strict throughput/overhead targets.
- Governance: top‑down validation + integration gates that prevent API/example drift and enforce runnable artifacts.

## 10. Risks and mitigations

- **Platform volatility (macOS entitlements, SIP changes)** — Mitigate with preflight diagnostics, signed fixtures, and continuous multi‑OS validation.
- **Frida dependency fragility** — Encapsulate behind adapter boundary; maintain alternates (eBPF on Linux); compatibility CI.
- **Security posture/user trust** — Enforce consent and least privilege; never suggest SIP relaxation; clear human‑readable messaging.
- **Performance regressions** — Bake PR‑001..PR‑004 into CI perf tests; expose metrics and reasoned drop policies.

## 11. KPIs and leading indicators (from specs)

- **Activation**: Attach/preflight pass rate; time‑to‑first‑event; hook coverage startup time.
- **Reliability**: Events/sec sustained, drop rate with reasons, encoder/drain latency, crash‑tolerant flush success.
- **Value**: Frequency of successful flight‑recorder windows with actionable detail; MTTR reduction in pilots.
- **Governance**: Example execution pass rate; no stub/public API drift; cross‑layer test health.

## 12. 12‑month milestone plan (mapped to repo phases)

- M1–M2: macOS MVP green against A1–A6 acceptance criteria; preflight `diagnose` returns correct remediations; tracer metrics exported.
- M3–M4: Two‑lane tuning and key‑symbol lane live updates; CLI/JSON‑RPC parity; doc and example validation enforced in CI.
- M5–M6: Query Engine MVP: narration, findings, spans.list, DWARF filters; token‑budget compliance; golden traces established.
- M7–M9: Linux adapter (eBPF) parity path; cross‑OS example matrix; stability and performance regression suite.
- M10–M12: Pilot deployments; measure attach success ≥ defined target, drop rate bounds, narrative quality; plan Windows attach feasibility.

## 13. Investment and ROI (not specified in repo)

- Define after MVP pilot metrics; base on adapter maintenance cost, multi‑OS CI, and support.

## 14. Dependencies and kill/redirect criteria

- **Dependencies**: Frida compatibility; macOS codesign/entitlement flows; multi‑OS CI; DWARF/dSYM availability for advanced queries.
- **Kill/redirect if by M6**: Acceptance criteria A1–A6 not consistently met on dev hardware; preflight remediation accuracy < target; sustained throughput far below PR‑002 despite optimization.

## 15. Alignment to repo governance

- Aligned with `AGENT.md` intent; fulfills `docs/user_stories/USER_STORIES.md` (AI Agent first; human supervisor second).
- Implements `docs/specs/TRACER_SPEC.md`, `docs/specs/QUERY_ENGINE_SPEC.md`, and `docs/specs/PERMISSIONS_AND_ENVIRONMENT.md` with `docs/specs/PLATFORM_LIMITATIONS.md` guardrails.
- Enforced by `docs/engineering_efficiency/TOP_DOWN_VALIDATION_FRAMEWORK.md` and `docs/engineering_efficiency/INTEGRATION_PROCESS_SPECIFICATION.md` to prevent drift.

---

### Appendix: Technical underpinnings referenced

- Two‑lane architecture (index/detail) and SPSC ring design: see `docs/tech_designs/NATIVE_TRACER_ARCHITECTURE.md` and `docs/technical_insights/TWO_LANE_ARCHITECTURE_EXPLAINED.md`.
- Platform constraints and remediation: see `docs/specs/PERMISSIONS_AND_ENVIRONMENT.md` and `docs/specs/PLATFORM_LIMITATIONS.md`.
- Query Engine contract and budget‑aware narratives: see `docs/specs/QUERY_ENGINE_SPEC.md`.
