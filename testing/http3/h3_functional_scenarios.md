# Phase 3: HTTP/3 Functional Test Scenarios

## Overview

RFC 9114 compliance test suite for cnetmod HTTP/3 implementation.

## Priority Levels

- **P0** (MUST PASS): Core request/response, concurrent streams, GOAWAY
- **P1** (SHOULD PASS): Error handling, SETTINGS, timeout

## Test Matrix

### P0 Tests

| # | Test | RFC Section | Description | Pass Criteria |
|---|------|-------------|-------------|---------------|
| 1 | GET no body | §4.1 | HEADERS frame + empty DATA | Response received |
| 2 | POST with body | §4.1 | HEADERS + DATA with FIN | Body echoed |
| 3 | Large body | §4.5 | >64KB triggers flow control | Auto MAX_STREAM_DATA |
| 4 | Multi headers | §4.1.2 | Multiple header fields | QPACK encode OK |
| 5 | Concurrent 10/100 | §2.1 | Multiple streams on 1 conn | No blocking |
| 6 | GOAWAY | §5.2 | Graceful shutdown | Existing streams finish |

### P1 Tests

| # | Test | RFC Section | Description | Pass Criteria |
|---|------|-------------|-------------|---------------|
| 7 | Stream isolation | §2.3 | RST_STREAM doesn't affect others | Other streams OK |
| 8 | SETTINGS | §7.2.4 | Parameter exchange | Applied correctly |
| 9 | Invalid HEADERS | §4.1.2 | Malformed frame | H3_FRAME_UNEXPECTED |
| 10 | Header overflow | §4.1.1.3 | >max_field_section | H3_EXCESSIVE_LOAD |
| 11 | Unknown stream | §2.1 | Unknown uni stream type | Silently ignored |
| 12 | Idle timeout | §10.1 | No activity > timeout | Connection closed |

## QPACK Encoding Strategy

For test purposes, we use a minimal QPACK implementation:
- Static table lookups (99 entries)
- Literal with name reference
- Literal without name reference
- No dynamic table (SETTINGS_QPACK_MAX_TABLE_CAPACITY=0)

### Static Table Coverage

The QPACK static table contains 99 pre-defined entries from RFC 9204 Appendix A,
covering common HTTP headers such as:
- Pseudo-headers (`:method`, `:path`, `:scheme`, `:authority`, `:status`)
- Common request headers (`accept`, `accept-encoding`, `cache-control`, `user-agent`)
- Common response headers (`content-type`, `content-length`, `server`, `vary`)
- Security headers (`strict-transport-security`, `x-frame-options`)

### Encoding Modes

1. **Indexed Field Line** (0xC0 prefix): Exact match in static table
2. **Literal with Name Reference** (0x50 prefix): Name match, custom value
3. **Literal without Name Reference** (0x20 prefix): Fully custom header

## Test Descriptions

### P0-1: GET No Body

The simplest HTTP/3 request. Client sends a single HEADERS frame with END_STREAM set.
Server responds with HEADERS + DATA frames.

```
Client → Server: HEADERS[:method=GET, :path=/echo/test, :scheme=https]  [END_STREAM]
Server → Client: HEADERS[:status=200] + DATA[echo response]  [END_STREAM]
```

### P0-2: POST with Body

Client sends HEADERS (no END_STREAM) followed by DATA frame with body content.
Server echoes the body back.

```
Client → Server: HEADERS[:method=POST, :path=/echo]
Client → Server: DATA["Hello, HTTP/3!"]  [END_STREAM]
Server → Client: HEADERS[:status=200] + DATA[echoed body]  [END_STREAM]
```

### P0-3: Large Body (Flow Control)

128KB body sent in 16KB chunks. Exceeds typical 64KB flow control window,
requiring automatic MAX_STREAM_DATA updates.

```
Client → Server: HEADERS[:method=POST, :path=/upload]
Client → Server: DATA[16KB chunk 1]
Client → Server: DATA[16KB chunk 2]
...
Client → Server: DATA[16KB chunk 8]  [END_STREAM]
Server: (auto MAX_STREAM_DATA updates during receive)
Server → Client: HEADERS[:status=200]  [END_STREAM]
```

### P0-4: Multiple Headers

Request with 9 header fields to verify QPACK encoding correctness.
Mix of static table hits and literal encoding.

### P0-5: Concurrent Streams

- Phase 1: 10 simultaneous GET requests on separate streams
- Phase 2: 100 simultaneous GET requests
- Verifies stream multiplexing without head-of-line blocking

### P0-6: GOAWAY Graceful Shutdown

Server sends GOAWAY frame to initiate graceful shutdown.
In-flight requests complete, new requests rejected.

```
Client → Server: HEADERS[:path=/before-goaway]  [END_STREAM]
Server → Client: GOAWAY[last_stream_id=N]
Client: (stops creating new streams)
Server → Client: response for stream N  [END_STREAM]
```

### P1-1: Stream Error Isolation

Three streams open simultaneously. Middle stream gets RST_STREAM.
Verifies other two streams complete normally.

### P1-2: SETTINGS Exchange

Client opens unidirectional control stream and sends SETTINGS frame.
Verifies server receives and applies settings parameters.

### P1-5: Connection Idle Timeout

Connection with 3-second idle timeout. No activity after handshake.
Connection should auto-close after timeout period.

## Running Tests

```bash
# Run P0 tests only
python h3_functional_test.py --port 4433 --priority p0

# Run all tests (P0 + P1)
python h3_functional_test.py --port 4433 --priority all

# Run P1 tests only
python h3_functional_test.py --port 4433 --priority p1

# Via CTest
ctest --test-dir build -R h3_functional_p0 -V
ctest --test-dir build -R h3_functional_all -V
```

## Expected Performance

| Test | Expected Latency | Notes |
|------|-----------------|-------|
| GET | < 5ms | Loopback |
| POST (1KB) | < 10ms | |
| Large body (128KB) | < 100ms | Flow control updates |
| 10 concurrent | < 20ms | |
| 100 concurrent | < 200ms | Stream multiplexing |

## Dependencies

- Python 3.10+
- aioquic >= 1.0.0
- cnetmod HTTP/3 server (h3_interop_server) running on target port

## Output

Results are saved to `h3_functional_results.json` in the working directory:

```json
{
  "timestamp": "2025-01-15 10:30:00",
  "priority": "all",
  "results": [
    {
      "name": "GET No Body",
      "passed": true,
      "message": "OK",
      "duration_ms": 3.45,
      "details": {}
    }
  ]
}
```

## Error Codes Reference

| Code | Name | Description |
|------|------|-------------|
| 0x0100 | H3_NO_ERROR | No error |
| 0x0101 | H3_GENERAL_PROTOCOL_ERROR | General protocol error |
| 0x0102 | H3_INTERNAL_ERROR | Internal error |
| 0x0103 | H3_STREAM_CREATION_ERROR | Stream creation error |
| 0x0104 | H3_CLOSED_CRITICAL_STREAM | Critical stream closed |
| 0x0105 | H3_FRAME_UNEXPECTED | Unexpected frame |
| 0x0106 | H3_FRAME_ERROR | Frame error |
| 0x0107 | H3_EXCESSIVE_LOAD | Excessive load |
| 0x0108 | H3_ID_ERROR | ID error |
| 0x0109 | H3_SETTINGS_ERROR | Settings error |
| 0x010A | H3_MISSING_SETTINGS | Missing settings |
| 0x010B | H3_REQUEST_REJECTED | Request rejected |
| 0x010C | H3_REQUEST_CANCELLED | Request cancelled |
| 0x010D | H3_REQUEST_INCOMPLETE | Request incomplete |
| 0x010E | H3_MESSAGE_ERROR | Message error |
| 0x010F | H3_CONNECT_ERROR | Connect error |
| 0x0110 | H3_VERSION_FALLBACK | Version fallback |
