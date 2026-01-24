# E2: Format Adapter Layer

## Layer

ADA Core - `ada query events` output formats

## Why First

- Only epic requiring ADA Rust code changes
- Defines the contract between ADA and Claude's reasoning
- All higher layers depend on this output format

## Interface Contract

```
ada query <session> events --format <FORMAT>
```

### Formats

| Format | Output | Use Case |
|--------|--------|----------|
| `line-complete` | `cycle=... \| T=... \| thread:... \| path:... \| depth:... \| func()` | Default, grep-friendly, LLM reasoning |
| `json` | `{cycle, time_sec, thread_id, path, depth, function}` | Programmatic access |
| `folded` | `main;login;validate` | Brendan Gregg flamegraph style |
| `dot` | `digraph { main -> login }` | Graph visualization |

### Line-Complete Format Spec

```
cycle=1000 | T=0.001s | thread:0 | path:0.0 | depth:1 | main()
cycle=1050 | T=0.001s | thread:0 | path:0.0.0 | depth:2 | login()
```

Fields:
- `cycle=` - CPU cycle count (ground truth timecode)
- `T=` - Human-readable seconds (derived)
- `thread:` - Thread ID
- `path:` - Index path in call tree (thread.call.child...)
- `depth:` - Call stack depth
- Function name with parentheses

## Location

`ada-cli/src/query/output.rs`

## Deliverables

1. Add `--format` flag to `ada query events`
2. Implement `line-complete` formatter with cycle timecode
3. Compute `path` from depth + thread + sibling index
4. Implement `json` formatter (structured version)

## Open Questions

- [ ] What format works best for LLM reasoning? (experiment needed)
- [ ] Should we support time-range filtering in query? (defer to E3 if needed)

## Status

Not started
