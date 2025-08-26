# ADA Query Engine Specification (MVP)

Defines the programmatic interface and behavior of the ADA Query Engine that consumes ATF traces and serves machine-consumable results for agentic coders.

This spec complements:
- Tracer: `docs/specs/TRACER_SPEC.md`
- Schema: `docs/specs/TRACE_SCHEMA.md`
- Span semantics: `docs/specs/SPAN_SEMANTICS_AND_CORRELATION.md`
- Runtime mappings: `docs/specs/runtime_mappings/`

## 1) Scope and Goals
- Provide compact, token-budget-aware narratives and search tools over ATF traces.
- Expose frame spans (MVP) and prefer logical spans when available (post-MVP).
- Support structured filtering by function, type, and argument names via DWARF/dSYM indexes.
 - Support flight-recorder traces: distinguish index-lane vs detail-lane data; surface window boundaries and trigger metadata.

Non-goals (MVP): UI, variable value decoding, live stepping. The engine is a read-only service over completed traces.

## 2) Inputs and Data Model
- Input: ATF trace directory produced by the tracer, containing a length-delimited Protobuf `Event` stream and sidecar indexes (if present).
- Core entities:
  - Event: FunctionCall / FunctionReturn / SignalDelivery.
  - Span: frame span (ENTRY↔RETURN). Logical spans are derived (post-MVP exposure).
  - FunctionId: 64-bit composite `(moduleId << 32) | symbolIndex` (dense per image). Module UUID→moduleId mapping and function table hash recorded in manifest.

## 3) Span Semantics (Normative)
- Prefer logical spans over frame spans for async when available; otherwise fall back to frame spans. See `SPAN_SEMANTICS_AND_CORRELATION.md`.
- Unmatched entries allowed; annotate status = unmatched/canceled/crashed when inferable.

## 4) API Protocol
- Transport: JSON-RPC 2.0 over stdio or TCP (implementation choice), UTF‑8 JSON, gzip compression recommended.
- All list/search methods accept: `tokenBudget?`, `projection?`, `limit?`, `cursor?`. Responses include `didTruncate` and `nextCursor?`.
- Projections: `minimal` (default) excludes heavy fields (regs/stack). `full` explicitly requested.

## 5) Methods (MVP)

### 5.1 trace.info (MUST)
- Input: `{ tracePath }`
- Output: `{ os, arch, timeStartNs, timeEndNs, eventCount, spanCount, threadCount, taskCount?, dropMetrics }
`

### 5.2 narration.summary (MUST)
- Input: `{ tracePath, tokenBudget?, includeCrashes=true, includeHotspots=true, maxFindings=5 }`
- Output: `{ bullets: string[], findings: Finding[], didTruncate }`
- Finding: `{ title, rationale, evidenceRefs: EvidenceRef[] }`
- Notes: compress repetitive patterns; surface anomalies (crash, unmatched, p95/p99 outliers).
 - Selective persistence: include persistence window markers and detail coverage ratios; state if summary is based on index-lane only.

### 5.3 findings.search (MUST)
- Purpose: symptom-driven retrieval using user/agent text.
- Input: `{ text, hints?: { functionPattern?, modulePattern?, timeRange? }, tokenBudget? }`
- Output: ranked `Finding[]` as in 5.2.
- Heuristics: crash, hang/timeout, latency spike, leak-like patterns; boost direct matches on names/modules.
 - Output addendum: optional `keySymbolPlan` containing moduleId→container blobs (bitset/roaring/hash, versioned) to feed tracer live.

### 5.4 spans.list (MUST)
- Input: `{ type: "frame" | "logical"="frame", timeRange?, functionIds?, functionPattern?, modulePattern?, tid?, taskId?, durationMinNs?, durationMaxNs?, status?, limit?, cursor?, projection?, tokenBudget? }`
- Output: `{ items: SpanRow[], nextCursor?, didTruncate }`
- SpanRow (minimal): `{ spanId, type, functionId, name, module, tid, taskId?, startNs, endNs?, durationNs?, status }`
 - Options: `lane?: "index"|"detail"|"auto"="auto"` (auto prefers detail; falls back to index).

### 5.5 stats.functionsTopN (MUST)
- Input: `{ metric: "count" | "totalDuration" | "p95", topN, timeRange?, type?: "frame"|"logical"="frame" }`
- Output: `[{ functionId, name, module, count, totalDurationNs, p50, p95, p99 }]`

### 5.6 events.get (MUST)
- Input: `{ eventIds: string[], projection? }`
- Output: `{ items: EventRow[] }` (minimal fields only by default)
 - Options: `lane?: "index"|"detail"|"auto"`.

### 5.7 plan.keySymbols (MUST)
- Input: `{ symptomText, hints?: { files?, modules?, functions? }, topK=500 }`
- Behavior: Build candidate symbols via DWARF/name/type/param matching; expand async-adjacent neighbors; rank; emit per-module containers chosen by density (bitset/roaring/hash).
- Output: `{ moduleContainers: { [moduleId]: { type: "bitset"|"roaring"|"hash", data: base64 }, version, hash, rationale: [{functionId, score, reasons[]}] }`

## 6) Budget-Aware Tools (MVP)

### 6.1 search.regex (SHOULD)
- Structured search over indexes (not raw blobs).
- Input: `{ pattern, fields?: ["name"|"module"|"file"|"type"|"param"], timeRange?, topK?, tokenBudget? }`
- Output: `{ matches: Match[], didTruncate, nextCursor? }`, Match: `{ functionId, name, module, file?, line?, score }`

### 6.2 search.scoped (MUST)
- Neighborhood slice around an anchor span/time.
- Input: `{ anchor: { spanId } | { timeNs, tid }, lookBackNs, lookAheadNs, filters?, tokenBudget? }`
- Output: summarized window: `{ spans: SpanRow[], exemplars?: SpanRow[], evidenceRefs: EvidenceRef[], didTruncate }`
 - Selective persistence: prefer `lane="detail"` within persisted windows; otherwise return index-lane summaries with pointers to nearest window.

### 6.3 graph.taskTree (SHOULD)
- Compact async causality graph with folding.
- Input: `{ roots: { spanIds?: string[], taskIds?: string[] }, maxHops=2, maxNodes=200, edgeTypes?: ["call","async"], summarize=true, tokenBudget? }`
- Output: `{ nodes: Node[], edges: Edge[], folded?: FoldInfo, didTruncate }`

## 7) Symbolization, Demangling, and DWARF/dSYM (MUST)
- QE-FR-001 Demangling (Swift, C++, Rust): expose both mangled and demangled names; normalize for search.
- QE-FR-002 DWARF/dSYM index: resolve function signatures and parameters.
  - Support filters: `functionPattern`, `paramNamePattern`, `typePattern`, `modulePattern`.
  - Fallback gracefully when dSYM is unavailable; retain name-based matching.
- Implementation notes:
  - macOS: match Mach-O UUID to dSYM; parse DWARF (gimli/object); demangle via toolchain libs.
  - Maintain side indexes: `functionId → {module, name_demangled, file:line?, params[{name,type}], returnType}`.

## 8) Indexes and Storage (MUST)
- Time index: event/spans offsets by coarse buckets (e.g., 1s) for fast range scans.
- Thread/Task index: postings per tid/taskId.
- Function index: functionId → event/spans postings.
- Derived spans table: compact sidecar for spans with `{start,end,duration,status}`.
- Symbol/DWARF index: described above.
 - Windows table: list of `{startNs, endNs, triggerKind, keySymbolVersion}` for selective persistence windows.

## 9) Performance and Limits (MUST)
- P-001 Token budgets: all narrative/list/search methods must attempt to fit within `tokenBudget` using compaction (fold repeats, exemplars, elide fields). If still too large, set `didTruncate=true` and return `nextCursor`.
- P-002 Latency targets: p50 ≤ 100 ms, p95 ≤ 500 ms on typical queries against medium traces (≤ 1e6 events) on dev hardware.
- P-003 Payload caps: server-side hard cap (e.g., ≤ 256 KiB per response) in addition to tokenBudget.
- P-004 Deterministic paging: stable ordering and cursors for reproducibility.

## 10) Observability (MUST)
- Export per-query metrics: latency, rows scanned, rows returned, payload bytes, compaction ratio, didTruncate, cache hit rate.
- Health endpoints or status method for basic liveness and cache stats.

## 11) Security and Safety (MUST)
- Read-only access to provided trace directories; path sanitization and sandboxing where possible.
- Resource limits on CPU/memory per query; timeouts with clear error codes.

## 12) Acceptance Criteria (MVP)
- A1: Demangling and DWARF filters
  - Given traces with dSYMs, queries by demangled name, param name, and type match the expected functions; without dSYMs, name-only queries still work.
- A2: Budget-constrained summaries
  - `narration.summary` and `findings.search` return ≤ specified tokenBudget (±10%) or set `didTruncate` and `nextCursor`.
- A3: Symptom search efficacy
  - For golden tasks (crash, hang, latency spike), `findings.search` surfaces the correct area (top‑3) with rationale and evidenceRefs.
- A4: Scoped windows
  - `search.scoped` returns a compact, correctly ordered neighborhood around anchors with exemplars for repeats.
- A5: Performance
  - Meets P-002 latency on medium traces; respects payload caps; deterministic pagination verified.

## 13) Test Plan Outline
- Golden traces: sync tree, async chain, timer+I/O, crash, unmatched.
- DWARF unit tests: type/param extraction across Swift/C++/Rust samples.
- Narrative budget tests: varying tokenBudget values; ensure compaction kicks in.
- Paging determinism: repeated queries yield identical `nextCursor` sequences.

## 14) Open Issues
- Logical-span public exposure timeline; initial internal derivation only.
- Cross-language demangling fidelity and toolchain version handling.
- Value-aware queries (filter by argument values) — post‑MVP.

## 15) Glossary
- EvidenceRef: stable identifier pointing to events/spans/aux docs, retrievable via `events.get` or similar.
- Compaction: server-side reduction (folding, exemplar selection, field elision) to meet budgets.
