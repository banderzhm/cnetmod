# HTTP Examples

HTTP/1.1 and HTTP/2 server implementations.

## Examples

### http_demo.cpp
Basic HTTP/1.1 server:
- Request handling
- Response generation
- Routing

### http2_demo.cpp
HTTP/2 server with multiplexing:
- HTTP/2 protocol support
- Stream multiplexing
- Server push

### hight_http.cpp
High-performance HTTP server:
- Optimized request processing
- Connection pooling
- Performance tuning

### hight_plus_http.cpp
Advanced HTTP server with ORM integration:
- Database integration
- RESTful API
- JSON handling
- MyBatis-style XML mappers

### multicore_http.cpp
Multi-core HTTP server:
- Thread pool utilization
- Load balancing
- Scalability demonstration

### tfb_benchmark.cpp
TechEmpower Framework Benchmark implementation:
- JSON serialization
- Database queries
- Fortunes template rendering
- Updates benchmark

## Building

```bash
cd build
cmake --build . --target <example_name>
```

## Running

```bash
./http/<example_name>
# Server typically listens on http://localhost:8080
```
