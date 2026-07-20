# WebSocket Examples

WebSocket server implementations for real-time communication.

## Examples

### ws_demo.cpp
Basic WebSocket server:
- WebSocket handshake
- Message sending/receiving
- Connection management

### hight_ws.cpp
High-performance WebSocket server:
- Optimized message handling
- Binary and text frames
- Ping/pong support

### multicore_ws.cpp
Multi-core WebSocket server:
- Concurrent connection handling
- Thread pool utilization
- Scalable architecture

## Building

```bash
cd build
cmake --build . --target <example_name>
```

## Running

```bash
./websocket/<example_name>
# Connect using WebSocket client to ws://localhost:8080
```

## Testing

You can test WebSocket servers using:
- Browser console: `new WebSocket('ws://localhost:8080')`
- wscat: `wscat -c ws://localhost:8080`
- Any WebSocket client library
