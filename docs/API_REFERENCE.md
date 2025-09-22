# ADA API Reference

This document provides comprehensive API documentation for the ADA (Application Debugging Assistant) system.

## Table of Contents

1. [Command Line Interface](#command-line-interface)
2. [JSON-RPC API](#json-rpc-api)
3. [Data Formats](#data-formats)
4. [Configuration](#configuration)
5. [Error Codes](#error-codes)

## Command Line Interface

### query_engine

The query engine provides a JSON-RPC server for querying trace data.

```bash
query_engine [OPTIONS]
```

#### Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `--address` | `SocketAddr` | `127.0.0.1:9090` | Address to bind the JSON-RPC server to |
| `--trace-root` | `PATH` | `./traces` | Root directory containing trace artifacts |
| `--cache-size` | `usize` | `100` | Maximum number of cached trace entries |
| `--cache-ttl` | `u64` | `300` | Cache time-to-live in seconds |

#### Examples

```bash
# Start server with default settings
query_engine

# Custom address and trace directory
query_engine --address 0.0.0.0:8080 --trace-root /var/traces

# Disable caching
query_engine --cache-size 0

# Custom cache settings
query_engine --cache-size 250 --cache-ttl 60
```

## JSON-RPC API

The query engine exposes a JSON-RPC 2.0 API for querying trace data. All endpoints accept HTTP POST requests with JSON payloads.

### Base URL

```
http://127.0.0.1:9090/
```

### Common Request Format

```json
{
  "jsonrpc": "2.0",
  "method": "method_name",
  "params": { ... },
  "id": 1
}
```

### Common Response Format

**Success Response:**
```json
{
  "jsonrpc": "2.0",
  "result": { ... },
  "id": 1
}
```

**Error Response:**
```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": -32000,
    "message": "Error description",
    "data": "Optional error details"
  },
  "id": 1
}
```

### Endpoints

#### trace.info

Get metadata and summary information about a trace.

**Method:** `trace.info`

**Parameters:**
```json
{
  "traceId": "string",
  "includeChecksums": false,
  "includeSamples": false
}
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `traceId` | `string` | Yes | - | Unique identifier for the trace |
| `includeChecksums` | `boolean` | No | `false` | Include MD5 checksums of trace files |
| `includeSamples` | `boolean` | No | `false` | Include sample events from the trace |

**Response:**
```json
{
  "traceId": "string",
  "os": "string",
  "arch": "string",
  "timeStartNs": 1234567890000000000,
  "timeEndNs": 1234567890123456789,
  "durationNs": 123456789,
  "eventCount": 10000,
  "spanCount": 500,
  "files": {
    "manifestSize": 2048,
    "eventsSize": 1048576,
    "totalSize": 1050624,
    "avgEventSize": 104
  },
  "checksums": {
    "manifestMd5": "a1b2c3d4e5f6789012345678901234ab",
    "eventsMd5": "b2c3d4e5f67890123456789012345abc"
  },
  "samples": {
    "firstEvents": [...],
    "lastEvents": [...],
    "randomEvents": [...]
  }
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `traceId` | `string` | Echo of the requested trace ID |
| `os` | `string` | Operating system where trace was captured |
| `arch` | `string` | CPU architecture (e.g., "x86_64", "aarch64") |
| `timeStartNs` | `u64` | Trace start time in nanoseconds since epoch |
| `timeEndNs` | `u64` | Trace end time in nanoseconds since epoch |
| `durationNs` | `u64` | Total trace duration in nanoseconds |
| `eventCount` | `u64` | Total number of events in the trace |
| `spanCount` | `u64` | Number of resolved function call spans |
| `files` | `object` | File size information |
| `checksums` | `object` | MD5 checksums (if requested) |
| `samples` | `object` | Sample events (if requested) |

**Example:**
```bash
curl -X POST http://127.0.0.1:9090 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "trace.info",
    "params": {
      "traceId": "my-trace-123",
      "includeChecksums": true
    },
    "id": 1
  }'
```

#### events.get

Query and filter trace events with pagination support.

**Method:** `events.get`

**Parameters:**
```json
{
  "traceId": "string",
  "filters": {
    "timeStartNs": 1234567890000000000,
    "timeEndNs": 1234567890123456789,
    "threadIds": [1, 2, 3],
    "eventTypes": ["functionCall", "functionReturn"],
    "functionNames": ["main", "foo"]
  },
  "projection": {
    "timestampNs": true,
    "threadId": true,
    "eventType": true,
    "functionName": false
  },
  "offset": 0,
  "limit": 1000,
  "orderBy": "timestamp",
  "ascending": true
}
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `traceId` | `string` | Yes | - | Unique identifier for the trace |
| `filters` | `object` | No | `{}` | Event filtering criteria |
| `projection` | `object` | No | See below | Fields to include in response |
| `offset` | `u64` | No | `0` | Number of events to skip |
| `limit` | `u64` | No | `1000` | Maximum events to return (max: 10,000) |
| `orderBy` | `string` | No | `"timestamp"` | Sort field: "timestamp" or "threadId" |
| `ascending` | `boolean` | No | `true` | Sort order |

**Filter Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `timeStartNs` | `u64` | Filter events after this timestamp |
| `timeEndNs` | `u64` | Filter events before this timestamp |
| `threadIds` | `u32[]` | Include only events from these thread IDs |
| `eventTypes` | `string[]` | Event types: "traceStart", "traceEnd", "functionCall", "functionReturn", "signalDelivery", "unknown" |
| `functionNames` | `string[]` | Include only events with these function names |

**Projection Fields (default values):**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `timestampNs` | `boolean` | `true` | Include event timestamp |
| `threadId` | `boolean` | `true` | Include thread ID |
| `eventType` | `boolean` | `true` | Include event type |
| `functionName` | `boolean` | `false` | Include function name (if available) |

**Response:**
```json
{
  "events": [
    {
      "timestampNs": 1234567890123456789,
      "threadId": 1,
      "eventType": "functionCall",
      "functionName": "main"
    }
  ],
  "metadata": {
    "totalCount": 10000,
    "returnedCount": 100,
    "offset": 0,
    "limit": 1000,
    "hasMore": true,
    "executionTimeMs": 15
  }
}
```

#### spans.list

List function call spans with filtering and projection capabilities.

**Method:** `spans.list`

**Parameters:**
```json
{
  "traceId": "string",
  "filters": {
    "timeStartNs": 1234567890000000000,
    "timeEndNs": 1234567890123456789,
    "threadIds": [1, 2],
    "functionNames": ["main", "foo"],
    "minDurationNs": 1000000,
    "maxDurationNs": 10000000000,
    "minDepth": 0,
    "maxDepth": 10
  },
  "projection": {
    "spanId": true,
    "functionName": true,
    "startTimeNs": true,
    "endTimeNs": true,
    "durationNs": true,
    "threadId": false,
    "moduleName": false,
    "depth": false,
    "childCount": false
  },
  "offset": 0,
  "limit": 1000,
  "includeChildren": true
}
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `traceId` | `string` | Yes | - | Unique identifier for the trace |
| `filters` | `object` | No | `{}` | Span filtering criteria |
| `projection` | `object` | No | See below | Fields to include in response |
| `offset` | `u64` | No | `0` | Number of spans to skip |
| `limit` | `u64` | No | `1000` | Maximum spans to return (max: 10,000) |
| `includeChildren` | `boolean` | No | `true` | Include nested function calls |

**Filter Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `timeStartNs` | `u64` | Filter spans starting after this timestamp |
| `timeEndNs` | `u64` | Filter spans ending before this timestamp |
| `threadIds` | `u32[]` | Include only spans from these thread IDs |
| `functionNames` | `string[]` | Include only spans with these function names |
| `minDurationNs` | `u64` | Minimum span duration in nanoseconds |
| `maxDurationNs` | `u64` | Maximum span duration in nanoseconds |
| `minDepth` | `u32` | Minimum call stack depth |
| `maxDepth` | `u32` | Maximum call stack depth |

**Projection Fields (default values):**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `spanId` | `boolean` | `true` | Unique span identifier |
| `functionName` | `boolean` | `true` | Function symbol name |
| `startTimeNs` | `boolean` | `true` | Span start timestamp |
| `endTimeNs` | `boolean` | `true` | Span end timestamp |
| `durationNs` | `boolean` | `true` | Span duration in nanoseconds |
| `threadId` | `boolean` | `false` | Thread ID |
| `moduleName` | `boolean` | `false` | Module/library name |
| `depth` | `boolean` | `false` | Call stack depth |
| `childCount` | `boolean` | `false` | Number of child spans |

**Response:**
```json
{
  "spans": [
    {
      "spanId": "1:1234567890123456789:1",
      "functionName": "main",
      "startTimeNs": 1234567890123456789,
      "endTimeNs": 1234567890234567890,
      "durationNs": 111111101,
      "threadId": 1,
      "depth": 0,
      "childCount": 5
    }
  ],
  "metadata": {
    "totalCount": 500,
    "returnedCount": 50,
    "offset": 0,
    "limit": 1000,
    "hasMore": true,
    "executionTimeMs": 25
  }
}
```

## Data Formats

### ATF V4 Binary Format

ADA uses the ATF (Application Trace Format) V4 binary format for efficient storage of trace events.

#### File Structure

Each trace consists of two files:
- `trace.json` - Manifest file with metadata
- `events.bin` - Binary event data in protobuf format

#### Manifest Format

The manifest file contains trace metadata in JSON format:

```json
{
  "os": "linux",
  "arch": "x86_64",
  "pid": 12345,
  "sessionId": 1,
  "timeStartNs": 1234567890000000000,
  "timeEndNs": 1234567890123456789,
  "eventCount": 10000,
  "bytesWritten": 1048576,
  "modules": ["module-uuid-1", "module-uuid-2"],
  "spanCount": 500
}
```

#### Event Binary Format

Events are stored as length-delimited Protocol Buffer messages. Each event contains:

**Event Structure:**
```protobuf
message Event {
  uint64 event_id = 1;
  int32 thread_id = 2;
  google.protobuf.Timestamp timestamp = 3;
  oneof payload {
    TraceStart trace_start = 4;
    TraceEnd trace_end = 5;
    FunctionCall function_call = 6;
    FunctionReturn function_return = 7;
    SignalDelivery signal_delivery = 8;
  }
}
```

**Event Types:**

1. **TraceStart** - Beginning of trace capture
2. **TraceEnd** - End of trace capture
3. **FunctionCall** - Function entry with registers and stack
4. **FunctionReturn** - Function exit with return registers
5. **SignalDelivery** - Signal handling events

#### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ATF_V4_MAX_REGISTERS` | 16 | Maximum registers per event |
| `ATF_V4_MAX_MODULES` | 64 | Maximum modules per trace |
| `ATF_V4_MAX_STACK_BYTES` | 256 | Maximum stack data per call |
| `ATF_V4_UUID_STRING_SIZE` | 37 | Module UUID string size |

### JSON Response Schemas

#### Event Sample Format

Used in `trace.info` response samples:

```json
{
  "timestampNs": 1234567890123456789,
  "threadId": 1,
  "eventType": "functionCall",
  "functionName": "main",
  "moduleName": null
}
```

#### Span ID Format

Spans are identified using the format: `{threadId}:{startTimeNs}:{sequenceNumber}`

Example: `"1:1234567890123456789:42"`

## Configuration

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `RUST_LOG` | Logging level configuration | `info` |

### Cache Configuration

The query engine uses an LRU cache with configurable size and TTL:

- **Cache Size**: Number of trace info entries to cache
- **Cache TTL**: Time-to-live for cached entries in seconds
- **Cache Invalidation**: Automatic invalidation on file modification

### File System Layout

```
{trace_root}/
├── {trace_id_1}/
│   ├── trace.json
│   └── events.bin
├── {trace_id_2}/
│   ├── trace.json
│   └── events.bin
└── ...
```

## Error Codes

### Standard JSON-RPC Errors

| Code | Message | Description |
|------|---------|-------------|
| `-32700` | Parse error | Invalid JSON received |
| `-32600` | Invalid request | Request does not conform to JSON-RPC |
| `-32601` | Method not found | Requested method does not exist |
| `-32602` | Invalid params | Invalid method parameters |
| `-32603` | Internal error | Server internal error |

### Application-Specific Errors

| Code | Message | Description |
|------|---------|-------------|
| `-32000` | Trace not found | Requested trace ID does not exist |
| `-32001` | Too many requests | Rate limit exceeded |
| `-32002` | Too many concurrent connections | Connection limit reached |

### Error Response Format

```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": -32000,
    "message": "Trace not found",
    "data": "optional error details"
  },
  "id": 1
}
```

### Parameter Validation Errors

Common parameter validation errors with `-32602` (Invalid params):

- `"traceId must not be empty"`
- `"limit cannot exceed 10000"`
- `"timeStartNs must be less than timeEndNs"`
- `"offset exceeds supported range"`
- `"minDepth must be <= maxDepth"`

### File System Errors

File system related errors are mapped to appropriate JSON-RPC codes:

- **File not found**: `-32000` (Trace not found)
- **Permission denied**: `-32603` (Internal error)
- **I/O errors**: `-32603` (Internal error)

## Signal Handling

The query engine gracefully handles shutdown signals:

- **SIGINT** (Ctrl+C): Graceful shutdown
- **SIGTERM**: Graceful shutdown (Unix only)

## Rate Limiting

The server implements connection-based rate limiting:

- Maximum concurrent connections configurable
- Rate limiting per connection
- Automatic connection cleanup

## Performance Considerations

### Query Performance

- **Events queries**: Linear scan with filtering, suitable for moderate trace sizes
- **Spans queries**: Reconstructed from events on-demand, cache results when possible
- **Trace info**: Cached with file modification detection

### Memory Usage

- Events are streamed during processing to minimize memory usage
- Caching is bounded by configured limits
- Large traces are processed incrementally

### Concurrency

- Multiple concurrent requests supported
- Thread-safe caching implementation
- Async I/O for file operations